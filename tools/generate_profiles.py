"""Genera i profili di mercato in Resources/profiles/.

Curve tonali in potenza per terzo d'ottava, normalizzate come nel plugin (media 63 Hz - 12.5 kHz = 0 dB).

Fonti e calibrazione:
- Medi e alti (da ~100 Hz in su): modello quadratico dello spettro medio di 12345 brani di musica popolare,
  A. Elowsson, A. Friberg, "Long-term Average Spectrum in Popular Music and its Relation to the Level of
  the Percussion", AES 142nd Convention (2017), eq. 6. La PSD per Hz è convertita in potenza per terzo
  d'ottava (+10 log10 f).
- Bassi e sub (sotto ~125 Hz): forma per genere relativa al livello a 125 Hz. Il fit dei bassi del paper
  (eq. 5) risulta troppo ripido per le produzioni moderne, quindi la forma Pop è calibrata su un master
  pop commerciale misurato con questo stesso motore (picco a ~63 Hz, -6 dB a 40 Hz, -12 dB a 31.5 Hz);
  gli altri generi sono derivati con più o meno sub.
- Aria: il dataset comprende decenni di produzioni; i generi moderni hanno un leggero lift sopra i 10 kHz.

Rock e Pop sono invece misurati su master commerciali da CD,
analizzati con questo stesso motore (ottobre 2026): mediana per album, poi mediana tra gli album, levigata
1-2-1 tra i terzi vicini da 31.5 Hz a 16 kHz. Rispetto al modello del paper sono più brillanti (tilt -1.4/-1.5
contro -2.3 dB/oct) e con meno low-mid. La tolleranza per banda lascia dentro l'85% dei brani, mai più stretta
di quella generica e al massimo 7 dB.
Le curve si rigenerano dai brani (elenco in tools/study_sources.json):
    python tools/study.py              misure per brano in tools/study_data/measurements.json
    python tools/generate_profiles.py  questo script
Hip-hop, EDM e Techno diventano misurati appena hanno almeno due album in MasterExample/<genere>/.
- Rock: AC/DC "Black Ice", Avril Lavigne "Under My Skin", Blink-182 "Greatest Hits", Bon Jovi "Have a Nice Day",
  Red Hot Chili Peppers "Stadium Arcadium" (2 CD), The Rolling Stones "A Bigger Bang", Tool "Lateralus",
  Dream Theater "Dream Theater" (2001-2013, 109 brani).
- Pop: Anastacia "Pieces of a Dream", Santana "All That I Am", Madonna "Confessions on a Dance Floor",
  Dua Lipa "Levitating" (41 brani; campione ancora piccolo).
Gli stessi master hanno calibrato anche larghezza stereo e LRA; loudness e dinamica restano invece quelle della
prassi attuale, perché quei CD sono dell'epoca della loudness war.

Le curve restano indicative: per un riferimento preciso usa un brano di riferimento o un profilo creato
da uno (pulsante "Salva profilo" nel plugin).

Generi e consegne ("kind"): il plugin compone il target da un genere e una consegna (TargetProfile.cpp, composeTarget).
- Genere: come deve suonare il master (curva tonale, stereo, LRA, tilt). Loudness e dinamica dei generi sono quelle
  dei master forti per CD e club: le usa la consegna "Master forte" ("dynamicsFromGenre").
- Consegna: dove andrà il master (integrated, true peak, PLR, PSR, DR), senza curva tonale.
  Piattaforme streaming ("context": "streaming") con la loro normalizzazione, broadcast e podcast sotto.

Broadcast e podcast ("context": "broadcast" / "podcast") sono specifiche di consegna:
- EBU R128 (2020): -23 LUFS, scarto ammesso ±0.5 LU (±1 LU solo per la diretta), true peak max -1 dBTP.
- ATSC A/85 (2013): -24 LKFS, tolleranza ±2 dB usata per il CALM Act, true peak max -2 dBTP.
- Podcast: Apple Podcasts -16 LKFS ±1 dB, true peak max -1 dBTP; AES TD1004.1.15-10 indica -16 LUFS come massimo.
Il margine "warn" di 0.2 dB copre le differenze tra i meter: oltre, il file non passa il QC.
"""
import json, math, os

centres = [1000 * 2 ** ((i - 17) / 3) for i in range(31)]


def paper_band_db(f):
    """Elowsson & Friberg eq. 6: PSD su griglia log (60 bin/ottava da 30 Hz), convertita a potenza per terzo d'ottava."""
    x = 1 + 60 * math.log2(f / 30.0)
    psd = -0.000183 * x * x + 0.0213 * x - 16.735
    return psd + 10 * math.log10(f)


# forma dei bassi in dB relativi al livello a 125 Hz, per i terzi d'ottava 20..100 Hz
LOW_SHAPES = {
    #            20     25    31.5    40     50     63     80    100
    "pop":     [-22.0, -17.0, -12.0, -6.0, -2.0,  2.0,  1.0,  0.0],
    "rock":    [-26.0, -21.0, -16.0, -10.0, -5.0, -1.0,  0.0,  0.0],
    "hiphop":  [-13.0,  -7.0,  -2.0,  3.0,  5.0,  5.0,  3.0,  1.0],
    "edm":     [-15.0,  -9.0,  -4.0,  1.0,  4.0,  4.0,  2.5,  0.5],
    "techno":  [-14.0,  -9.0,  -4.0,  1.0,  4.0,  4.5,  3.0,  1.0],
}


def curve(low_shape, air_lift=0.0, presence=0.0):
    out = [paper_band_db(f) for f in centres]
    ref125 = out[8]   # terzo d'ottava a 125 Hz
    for i, rel in enumerate(LOW_SHAPES[low_shape]):
        out[i] = ref125 + rel
    for i, f in enumerate(centres):
        if f >= 6000:
            out[i] += air_lift * min(1.0, math.log2(f / 6000) / math.log2(12500 / 6000))   # shelf morbido
        out[i] += presence * math.exp(-0.5 * (math.log2(f / 3000) / 0.7) ** 2)
    mean = sum(out[5:29]) / 24
    return [round(v - mean, 2) for v in out]


def tilt(c):
    """Stessa regressione del plugin: dB vs log2(f) sui terzi 100 Hz - 10 kHz."""
    xs = [math.log2(centres[i] / 1000) for i in range(7, 28)]
    ys = c[7:28]
    n = len(xs); sx = sum(xs); sy = sum(ys)
    sxx = sum(x * x for x in xs); sxy = sum(x * y for x, y in zip(xs, ys))
    return (n * sxy - sx * sy) / (n * sxx - sx * sx)


# Curve misurate: si rigenerano dalle misure dei brani di riferimento (tools/study.py scrive
# tools/study_data/measurements.json). Un genere con almeno due album di riferimento diventa misurato da solo.
import profiles_lib

TRACKS = profiles_lib.load_tracks()


def measured(genre, model):
    """Curva e tolleranza per banda misurate, se il genere ha abbastanza album; altrimenti il modello."""
    tonal = profiles_lib.measured_tonal(TRACKS, genre) if TRACKS else None
    if tonal is None:
        return model, {}
    print(f"{genre}: curva misurata su {tonal['tracks']} brani di {tonal['albums']} album")
    return tonal["tonalCurve"], {"bandTolerance": tonal["bandTolerance"]}


def m(lo, hi, target, warn):
    return {"min": lo, "max": hi, "target": target, "warn": warn}


def tilt_range(c):
    t = round(tilt(c), 2)
    return m(round(t - 0.7, 2), round(t + 0.7, 2), t, 0.6)


pop_curve, pop_extra = measured("pop", curve("pop"))
rock_curve, rock_extra = measured("rock", curve("rock"))
hiphop_curve, hiphop_extra = measured("hiphop", curve("hiphop", air_lift=1.5))
edm_curve, edm_extra = measured("edm", curve("edm", air_lift=2.5))
techno_curve, techno_extra = measured("techno", curve("techno", air_lift=1.5))

profiles = [
    # consegna "Master forte": loudness e dinamica le dà il genere (con un reference: "Come il reference")
    ("loud", "Master forte (CD, club)", "Loudness e dinamica tipiche del genere, per CD, club e download: nessuna normalizzazione.",
     {}, None, {"kind": "delivery", "shortName": "Master forte", "dynamicsFromGenre": True}),
    ("streaming_generic", "Streaming (generico)", "Master bilanciato per tutte le piattaforme: -14 LUFS, -1 dBTP, dinamica preservata.",
     {"integratedLufs": m(-15, -12, -14, 2), "truePeakMax": m(-6, -1, -1, 0.5),
      "plr": m(9, 14, 11, 2), "minPsr": m(7, 12, 9, 1.5), "dr": m(7, 12, 9, 2)}, None,
     {"kind": "delivery", "shortName": "Streaming", "context": "streaming", "normalizationLufs": -14}),
    ("spotify", "Spotify", "Normalizzazione a -14 LUFS, true peak consigliato -1 dBTP (-2 se oltre -14 LUFS).",
     {"integratedLufs": m(-15, -13, -14, 2), "truePeakMax": m(-6, -1, -1, 0.5),
      "plr": m(9, 14, 11, 2), "minPsr": m(7, 12, 9, 1.5)}, None,
     {"kind": "delivery", "context": "streaming", "normalizationLufs": -14, "loudTruePeakMax": -2}),
    ("apple_music", "Apple Music", "Sound Check a -16 LUFS, true peak -1 dBTP: premia i master più dinamici.",
     {"integratedLufs": m(-17, -14, -16, 2), "truePeakMax": m(-6, -1, -1, 0.5),
      "plr": m(10, 15, 12, 2), "minPsr": m(8, 13, 10, 1.5)}, None,
     {"kind": "delivery", "context": "streaming", "normalizationLufs": -16}),
    ("youtube", "YouTube", "Normalizza a -14 LUFS (solo in riduzione), true peak -1 dBTP.",
     {"integratedLufs": m(-15, -12, -14, 2), "truePeakMax": m(-6, -1, -1, 0.5),
      "plr": m(9, 14, 11, 2), "minPsr": m(7, 12, 9, 1.5)}, None,
     {"kind": "delivery", "context": "streaming", "normalizationLufs": -14}),
    ("pop", "Pop commerciale", "Valori tipici delle release pop attuali (loudness da mercato, non da normalizzazione).",
     {"integratedLufs": m(-9, -6.5, -7.5, 1.5), "truePeakMax": m(-2, -0.3, -1, 0.5), "loudnessRange": m(3, 9, 5, 3),
      "plr": m(7, 10, 8.5, 1.5), "minPsr": m(6, 9, 7.5, 1.5), "dr": m(5, 9, 7, 1.5), "spectralTilt": tilt_range(pop_curve),
      "correlation": m(0.35, 0.9, 0.6, 0.15), "widthPercent": m(7, 25, 13, 6), "lowEndWidthPercent": m(0, 8, 2, 6)},
     pop_curve, {"kind": "genre", "shortName": "Pop", **pop_extra}),
    ("rock", "Rock", "Rock/alternative moderno: denso ma con più transiente sui medi.",
     {"integratedLufs": m(-10, -7, -8.5, 1.5), "truePeakMax": m(-2, -0.3, -1, 0.5), "loudnessRange": m(3, 10, 6, 3),
      "plr": m(8, 11, 9.5, 1.5), "minPsr": m(6.5, 10, 8, 1.5), "dr": m(6, 10, 8, 1.5), "spectralTilt": tilt_range(rock_curve),
      "correlation": m(0.3, 0.9, 0.55, 0.15), "widthPercent": m(9, 30, 16, 6), "lowEndWidthPercent": m(0, 10, 3, 6)},
     rock_curve, {"kind": "genre", **rock_extra}),
    ("hiphop_trap", "Hip-hop / Trap", "Sub-bass dominante (808), alto loudness, low-end mono.",
     {"integratedLufs": m(-9, -6, -7.5, 1.5), "truePeakMax": m(-2, -0.3, -1, 0.5), "loudnessRange": m(3, 7, 5, 2),
      "plr": m(7, 10, 8.5, 1.5), "minPsr": m(6, 9, 7.5, 1.5), "dr": m(5, 9, 7, 1.5), "spectralTilt": tilt_range(hiphop_curve),
      "correlation": m(0.4, 0.95, 0.65, 0.15), "widthPercent": m(10, 32, 20, 8), "lowEndWidthPercent": m(0, 5, 1, 5)},
     hiphop_curve, {"kind": "genre", "shortName": "Hip-hop", **hiphop_extra}),
    ("edm_club", "EDM / Club", "Master da club: molto denso, sub forte e mono, alte brillanti.",
     {"integratedLufs": m(-8, -5, -6.5, 1.5), "truePeakMax": m(-1.5, -0.1, -0.5, 0.5), "loudnessRange": m(2, 6, 4, 2),
      "plr": m(6, 9, 7.5, 1.5), "minPsr": m(5, 8, 6.5, 1.5), "dr": m(4, 8, 6, 1.5), "spectralTilt": tilt_range(edm_curve),
      "correlation": m(0.35, 0.9, 0.6, 0.15), "widthPercent": m(12, 38, 24, 8), "lowEndWidthPercent": m(0, 5, 1, 5)},
     edm_curve, {"kind": "genre", "shortName": "EDM", **edm_extra}),
    ("techno_house", "Techno / House", "Kick e sub protagonisti, dinamica controllata ma transiente del kick preservato.",
     {"integratedLufs": m(-9, -6, -7.5, 1.5), "truePeakMax": m(-1.5, -0.1, -0.5, 0.5), "loudnessRange": m(2, 5, 3.5, 2),
      "plr": m(7, 10, 8.5, 1.5), "minPsr": m(6, 9, 7.5, 1.5), "dr": m(5, 9, 7, 1.5), "spectralTilt": tilt_range(techno_curve),
      "correlation": m(0.35, 0.9, 0.6, 0.15), "widthPercent": m(12, 38, 24, 8), "lowEndWidthPercent": m(0, 5, 1, 5)},
     techno_curve, {"kind": "genre", "shortName": "Techno", **techno_extra}),
    # specifiche di consegna: solo loudness e true peak, senza curva tonale
    ("ebu_r128", "EBU R128 (TV e radio)", "Consegna broadcast europea: -23 LUFS ±0.5 LU, true peak max -1 dBTP. Fuori tolleranza il file non passa il QC.",
     {"integratedLufs": m(-23.5, -22.5, -23, 0.2), "truePeakMax": m(-6, -1, -1, 0.2)}, None,
     {"kind": "delivery", "shortName": "EBU R128", "context": "broadcast"}),
    ("atsc_a85", "ATSC A/85 (TV USA)", "Consegna broadcast USA: -24 LKFS ±2 dB, true peak max -2 dBTP. Fuori tolleranza il file non passa il QC.",
     {"integratedLufs": m(-26, -22, -24, 0.2), "truePeakMax": m(-6, -2, -2, 0.2)}, None,
     {"kind": "delivery", "shortName": "ATSC A/85", "context": "broadcast"}),
    ("podcast", "Podcast", "-16 LUFS ±1 LU, true peak max -1 dBTP (Apple Podcasts, AES TD1004).",
     {"integratedLufs": m(-17, -15, -16, 1), "truePeakMax": m(-6, -1, -1, 0.5)}, None,
     {"kind": "delivery", "context": "podcast"}),
]

outdir = os.path.join(os.path.dirname(__file__), "..", "Resources", "profiles")
os.makedirs(outdir, exist_ok=True)
for pid, name, desc, metrics, tonal, *extra in profiles:
    data = {"id": pid, "name": name, "description": desc, "metrics": metrics}
    if tonal is not None:
        data.update({"tonalCurve": tonal, "tonalTolerance": 2.5})
    data.update(extra[0] if extra else {})
    with open(os.path.join(outdir, pid + ".json"), "w", encoding="utf-8") as f:
        json.dump(data, f, indent=2, ensure_ascii=False)
    if tonal is None:
        print(f"{pid:18s} consegna {data.get('context', 'music')}")
    else:
        print(f"{pid:18s} tilt {tilt(tonal):6.2f}  sub(31.5/40/50) {tonal[2]:6.1f} {tonal[3]:6.1f} {tonal[4]:6.1f}  63 {tonal[5]:5.1f}  125 {tonal[8]:5.1f}  1k {tonal[17]:5.1f}  10k {tonal[27]:6.1f}")
