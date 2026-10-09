"""
Coerenza tra misure, diagnosi, voto e segni sulla forma d'onda, per ogni profilo e fase.

Uso (dopo MasterAgentAnalyze --verify <cartella> ...):
    python tools/verify_findings.py <cartella>

Controlla regole che devono valere sempre, qualunque sia il brano:
- severità di ogni metrica del profilo = posizione del valore rispetto a min/max/warn del profilo, con le
  eccezioni dell'app (LRA mai critico; loudness oltre il riferimento di una piattaforma che normalizza al massimo
  "attenzione"; PLR/PSR gonfiati dai picchi inter-campione solo informativi; true peak per i master forti su Spotify);
- true peak oltre il ceiling  <=>  segni di true peak sulla forma d'onda (e lo stesso per il PSR minimo);
- clip nel report  <=>  segni di clip, con lo stesso numero di eventi; clipping critico solo a fondo scala;
- fase Mix: niente giudizi su loudness, PLR, PSR, DR del master finito;
- mosse EQ di segno opposto allo scostamento della banda che correggono;
- un problema tecnico critico limita il voto a 60.
"""

import json
import math
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PROFILES = {json.loads(p.read_text(encoding="utf-8"))["id"]: json.loads(p.read_text(encoding="utf-8"))
            for p in (ROOT / "Resources" / "profiles").glob("*.json")}

# metrica del profilo -> (chiave dashboard, campo dello snapshot)
METRICS = {
    "integratedLufs": ("integrated", "integratedLufs"),
    "truePeakMax": ("truePeak", "truePeakMaxDb"),
    "loudnessRange": ("lra", "loudnessRange"),
    "plr": ("plr", "plr"),
    "minPsr": ("psr", "minPsr"),
    "dr": ("dr", "drValue"),
    "correlation": ("correlation", "correlation"),
    "widthPercent": ("width", "widthPercent"),
    "lowEndWidthPercent": ("lowEndWidth", "lowEndWidthPercent"),
    "spectralTilt": ("tilt", "spectralTilt"),
    "crestLow": ("crest:0", ("bandCrestDb", 0)),
    "crestMid": ("crest:1", ("bandCrestDb", 1)),
    "crestHigh": ("crest:2", ("bandCrestDb", 2)),
}
MASTERING_ONLY = {"integratedLufs", "truePeakMax", "plr", "minPsr", "dr", "crestLow", "crestMid", "crestHigh"}
BAND_EDGES = [20, 60, 250, 500, 2000, 4000, 6000, 12000, 20000]
OK, INFO, WARNING, CRITICAL = 0, 1, 2, 3


def value_of(m, field):
    if isinstance(field, tuple):
        return m[field[0]][field[1]]
    return m.get(field)


def expected_severity(metric, value, r, prof=None, m=None, phase="master"):
    if r["min"] <= value <= r["max"]:
        delta, sev = 0.0, OK
    else:
        delta = value - r["max"] if value > r["max"] else value - r["min"]
        sev = WARNING if abs(delta) <= r["warn"] else CRITICAL
    if metric == "truePeakMax" and delta < 0:
        sev = INFO
    if metric == "lowEndWidthPercent" and delta < 0:
        sev = OK
    if metric == "loudnessRange":
        sev = min(sev, WARNING)
    if prof is not None and m is not None:
        if metric == "integratedLufs" and delta > 0 and "normalizationLufs" in prof:
            sev = min(sev, WARNING)
        sample_peak = max(m["samplePeakDb"])
        if (metric in ("plr", "minPsr") and phase == "master" and delta > 0 and sev >= WARNING
                and sample_peak > -119 and m["truePeakMaxDb"] - sample_peak > 1.0):
            sev = INFO
        if (metric == "truePeakMax" and phase == "master" and "loudTruePeakMax" in prof
                and m["integratedLufs"] > prof["normalizationLufs"] and value > prof["loudTruePeakMax"] and sev < WARNING):
            sev = WARNING
    return sev


def check_track(meta):
    problems = []
    m = meta["metrics"]
    if m.get("truePeakMaxDb") is None:
        m["truePeakMaxDb"] = max(m["truePeakDb"])
    name = Path(meta["file"]).name

    for pid, entry in meta["profiles"].items():
        prof = PROFILES.get(pid)
        if prof is None:
            continue
        for phase in ("mix", "master"):
            res = entry[phase]
            findings = res["findings"]
            where = f"{name} / {pid} / {phase}"

            def by_key(key):
                return [f for f in findings if f["key"] == key]

            # 1. severità delle metriche del profilo
            for metric, (key, field) in METRICS.items():
                r = prof["metrics"].get(metric)
                v = value_of(m, field)
                if r is None or v is None or (metric == "dr" and not m["drValid"]):
                    continue
                fs = by_key(key)
                if phase == "mix" and metric in MASTERING_ONLY:
                    judged = [f for f in fs if f["target"] not in ("deciso dal mastering", "-6 .. -3 dBTP", "> 9 dB")]
                    if judged:
                        problems.append(f"{where}: in fase Mix {metric} giudicato sul target di mastering ({judged[0]['message'][:60]})")
                    continue
                exp = expected_severity(metric, v, r, prof, m, phase)
                if metric == "truePeakMax":
                    fs = [f for f in fs if f["metric"] == "True peak max"]   # non l'avviso sulla codifica lossy
                got = max((f["severity"] for f in fs), default=None)
                if got is None:
                    problems.append(f"{where}: manca la diagnosi per {metric} ({v:.2f})")
                elif got != exp:
                    problems.append(f"{where}: {metric} = {v:.2f} (range {r['min']}..{r['max']}, warn {r['warn']}) "
                                    f"severità {got}, attesa {exp}")

            markers = res["markers"]
            kinds = [mk["type"] for mk in markers]

            # 2. true peak oltre il ceiling <=> segni di true peak
            ceiling = res["ceilingDb"]
            tp_over = m["truePeakMaxDb"] > ceiling + 1e-4
            if tp_over != (1 in kinds):
                problems.append(f"{where}: true peak {m['truePeakMaxDb']:.2f} con ceiling {ceiling}: segni TP = {1 in kinds}")
            if phase == "master" and tp_over:
                tp_f = by_key("truePeak")
                if not tp_f or tp_f[0]["severity"] < WARNING:
                    problems.append(f"{where}: segni di true peak sulla forma d'onda ma nessuna diagnosi nel report")

            # 3. PSR minimo sotto il minimo del profilo <=> zone compresse
            min_psr = res["minPsrSetting"]
            if min_psr and min_psr > 0:
                comp = m["minPsr"] < min_psr - 1e-4
                if comp != (3 in kinds):
                    problems.append(f"{where}: PSR minimo {m['minPsr']:.2f} (soglia {min_psr}): zone compresse = {3 in kinds}")
            elif 3 in kinds:
                problems.append(f"{where}: zone compresse senza soglia PSR")

            # 4. clip: stesso numero di eventi
            clip_markers = [mk for mk in markers if mk["type"] == 0]
            if (m["clipEvents"] > 0) != bool(clip_markers):
                problems.append(f"{where}: {m['clipEvents']} clip nel report, {len(clip_markers)} segni")
            elif clip_markers and m["clipEvents"] < 4000:
                total = sum(int(round(mk["value"])) for mk in clip_markers)
                if total != m["clipEvents"]:
                    problems.append(f"{where}: i segni di clip sommano {total} eventi, il report ne conta {m['clipEvents']}")

            # 4b. clipping critico solo a fondo scala (un clipper al ceiling non limita il voto)
            full_scale = m.get("clipEventsFullScale")
            if full_scale is not None:
                clip_crit = [f for f in findings if f["key"] == "clip" and f["severity"] == CRITICAL]
                if clip_crit and full_scale <= 20:
                    problems.append(f"{where}: clipping critico con {full_scale} eventi a fondo scala")
                if full_scale > 20 and not clip_crit:
                    problems.append(f"{where}: {full_scale} eventi a fondo scala ma nessun clipping critico")

            # 5. mosse EQ opposte allo scostamento delle bande che correggono, mai più forti dello scostamento
            deltas = res["bandTonalDelta"]
            for mv in res["eqMoves"]:
                if not isinstance(mv, dict):
                    continue
                for band in mv["bands"]:
                    d = deltas[int(band)]
                    if d * mv["shownGainDb"] > 0:
                        problems.append(f"{where}: {mv['text']} va nello stesso verso dello scostamento della banda {int(band)} ({d:+.1f} dB)")
                if phase == "master" and abs(mv["shownGainDb"]) > 2.0 + 1e-3:
                    problems.append(f"{where}: in mastering {mv['text']} supera 2 dB")

            # 6. voto limitato da un problema tecnico critico
            tech_crit = [f for f in findings if f["category"] == "Tecnico" and f["severity"] == CRITICAL]
            if tech_crit and res["score"] > 60:
                problems.append(f"{where}: problema tecnico critico ({tech_crit[0]['metric']}) ma voto {res['score']}")
    return problems


def main():
    folder = Path(sys.argv[1])
    total = 0
    for jp in sorted(folder.glob("[0-9]*.json")):
        meta = json.loads(jp.read_text(encoding="utf-8"))
        problems = check_track(meta)
        print(f"{Path(meta['file']).name}: {len(problems)} incoerenze")
        for p in problems:
            print("  - " + p)
        total += len(problems)
    print(f"\n{total} incoerenze")
    return 1 if total else 0


if __name__ == "__main__":
    sys.exit(main())
