"""
Verifica di coerenza delle misure: ricalcola con un'implementazione indipendente (numpy/scipy)
le misure che MasterAgent mostra e le confronta con quelle dell'app.

Uso:
    MasterAgentAnalyze --verify <cartella> <file audio> [...]      (decodifica + misure dell'app in JSON)
    python tools/verify_analysis.py <cartella>

Il riferimento non riusa niente del codice C++:
- loudness BS.1770-4 con i coefficienti K pubblicati dall'ITU a 48 kHz (gli altri sample rate vengono
  prima ricampionati a 48 kHz: la loudness di un segnale a banda limitata non cambia);
- true peak con sovracampionamento polifase di scipy (4x come BS.1770, più 8x come stima del picco analogico);
- LRA secondo EBU Tech 3342; DR con l'algoritmo TT (blocchi da 3 s, 20% più forti, secondo picco);
- spettro e stereo da densità spettrali e cross-spettri di Welch.
"""

import json
import math
import sys
from pathlib import Path

import numpy as np
from scipy import signal
from scipy.io import wavfile

# --- loudness ------------------------------------------------------------------

# ITU-R BS.1770-4, tabelle 1 e 2 (48 kHz)
K_SHELF_B = [1.53512485958697, -2.69169618940638, 1.19839281085285]
K_SHELF_A = [1.0, -1.69065929318241, 0.73248077421585]
K_HP_B = [1.0, -2.0, 1.0]
K_HP_A = [1.0, -1.99004745483398, 0.99007225036621]


def to_48k(x, fs):
    if fs == 48000:
        return x
    g = math.gcd(48000, int(fs))
    return signal.resample_poly(x, 48000 // g, int(fs) // g, axis=0)


def k_weighted_power(x48):
    """Potenza K-pesata per campione, sommata sui canali (pesi 1.0 per L e R)."""
    y = signal.lfilter(K_SHELF_B, K_SHELF_A, x48, axis=0)
    y = signal.lfilter(K_HP_B, K_HP_A, y, axis=0)
    return np.sum(y * y, axis=1)


def window_energies(p, length, hop):
    c = np.concatenate([[0.0], np.cumsum(p)])
    starts = np.arange(0, len(p) - length + 1, hop)
    return (c[starts + length] - c[starts]) / length


def lufs(e):
    return -0.691 + 10.0 * np.log10(np.maximum(e, 1e-30))


def loudness_measures(x, fs):
    p = k_weighted_power(to_48k(x, fs))
    m = window_energies(p, 19200, 4800)        # 400 ms, passo 100 ms
    st = window_energies(p, 144000, 4800)      # 3 s, passo 100 ms

    gated = m[lufs(m) > -70.0]
    integrated = float("-inf")
    if len(gated):
        rel = lufs(np.mean(gated)) - 10.0
        g2 = gated[lufs(gated) > rel]
        integrated = float(lufs(np.mean(g2)))

    lra = 0.0
    st_abs = st[lufs(st) > -70.0]
    if len(st_abs):
        rel = lufs(np.mean(st_abs)) - 20.0
        v = np.sort(lufs(st_abs[lufs(st_abs) > rel]))
        if len(v) > 1:
            lra = float(np.percentile(v, 95) - np.percentile(v, 10))

    return {
        "integratedLufs": integrated,
        "loudnessRange": lra,
        "maxMomentaryLufs": float(np.max(lufs(m))) if len(m) else None,
        "maxShortTermLufs": float(np.max(lufs(st))) if len(st) else None,
        "shortTermSeries": lufs(st),
    }


# --- picchi e dinamica ------------------------------------------------------------

def true_peak_blocks(x, fs, factor, delay=24):
    """
    True peak massimo per blocco da 100 ms (dBTP) e massimo per canale.
    delay: il filtro dell'app è causale e ritarda di 24 campioni (0.5 ms a 48 kHz); per confrontare i blocchi
    il riferimento (a fase zero) viene ritardato allo stesso modo.
    """
    # a pezzi da ~10 s con un margine ai lati: sovracampionare un brano intero 8x occuperebbe gigabyte
    b = int(round(fs * 0.1))
    n = len(x) // b
    margin = 256
    padded = np.concatenate([np.zeros((delay + margin, x.shape[1])), x, np.zeros((margin, x.shape[1]))])
    per_channel = np.zeros(x.shape[1])
    blocks = np.zeros(n)
    chunk = 100
    for k0 in range(0, n + 1, chunk):
        k1 = min(k0 + chunk, n + 1)
        lo, hi = k0 * b, min(k1 * b, len(x) + delay)   # campioni del segnale ritardato
        if hi <= lo:
            break
        seg = padded[lo: hi + 2 * margin]
        a = np.abs(signal.resample_poly(seg, factor, 1, axis=0))[margin * factor: (margin + hi - lo) * factor]
        per_channel = np.maximum(per_channel, a.max(axis=0))
        full = min(k1, n) - k0
        if full > 0:
            blocks[k0: k0 + full] = a[: full * b * factor].max(axis=1).reshape(full, b * factor).max(axis=1)
    return 20 * np.log10(np.maximum(per_channel, 1e-12)), 20 * np.log10(np.maximum(blocks, 1e-12))


def db(v):
    return 20 * math.log10(v) if v > 0 else float("-inf")


def dr_tt(x, fs):
    block = int(round(fs * 3.0))
    n = len(x) // block
    if n < 3:
        return None
    vals = []
    for ch in range(x.shape[1]):
        b = x[: n * block, ch].reshape(n, block)
        rms = np.sort(np.sqrt(2.0 * np.mean(b * b, axis=1)))[::-1]
        peaks = np.sort(np.max(np.abs(b), axis=1))[::-1]
        top = max(1, int(round(n * 0.2)))
        top_rms = math.sqrt(np.mean(rms[:top] ** 2))
        vals.append(db(peaks[1] / top_rms))
    return float(np.mean(vals))


def band_crest(x, fs):
    mono = 0.5 * (x[:, 0] + x[:, 1])
    lp200 = signal.butter(4, 200, "low", fs=fs, output="sos")     # Butterworth 4° ordine come riferimento
    hp200 = signal.butter(4, 200, "high", fs=fs, output="sos")
    lp4k = signal.butter(4, 4000, "low", fs=fs, output="sos")
    hp4k = signal.butter(4, 4000, "high", fs=fs, output="sos")
    bands = [signal.sosfilt(lp200, mono), signal.sosfilt(lp4k, signal.sosfilt(hp200, mono)), signal.sosfilt(hp4k, mono)]
    block = int(round(fs * 3.0))
    out = []
    for y in bands:
        n = len(y) // block
        b = y[: n * block].reshape(n, block)
        rms = np.sqrt(np.mean(b * b, axis=1))
        pk = np.max(np.abs(b), axis=1)
        keep = rms > 10 ** (-60 / 20)
        out.append(float(np.median(20 * np.log10(pk[keep] / rms[keep]))) if keep.any() else None)
    return out


# --- spettro e stereo -------------------------------------------------------------

def third_centre(i):
    return 1000.0 * 2.0 ** ((i - 17) / 3.0)


def spectral_measures(x, fs):
    nper = 1 << (15 if fs <= 50000 else 16)
    f, pl = signal.welch(x[:, 0], fs, window="hann", nperseg=nper, noverlap=nper // 2, scaling="spectrum")
    _, pr = signal.welch(x[:, 1], fs, window="hann", nperseg=nper, noverlap=nper // 2, scaling="spectrum")
    _, c = signal.csd(x[:, 0], x[:, 1], fs, window="hann", nperseg=nper, noverlap=nper // 2, scaling="spectrum")
    c = np.real(c)
    p = 0.5 * (pl + pr)

    df = f[1] - f[0]

    def band(arr, lo, hi):
        # ogni bin copre [f - df/2, f + df/2]: ai bordi conta la parte che cade nella banda
        overlap = np.clip(np.minimum(hi, f + df / 2) - np.maximum(lo, f - df / 2), 0.0, None) / df
        return float(np.sum(arr * overlap))

    third = np.array([10 * np.log10(max(band(p, third_centre(i) * 2 ** (-1 / 6), third_centre(i) * 2 ** (1 / 6)), 1e-12))
                      for i in range(31)])
    third_norm = third - np.mean(third[5:29])
    xs = np.log2(np.array([third_centre(i) for i in range(7, 28)]) / 1000.0)
    tilt = float(np.polyfit(xs, third[7:28], 1)[0])

    sel = (f >= 20) & (f <= 20000)
    centroid = float(np.sum(f[sel] * p[sel]) / np.sum(p[sel]))
    total = band(p, 20, 20000)
    edges = [20, 60, 250, 500, 2000, 4000, 6000, 12000, 20000]
    band_energy = [10 * np.log10(band(p, edges[b], edges[b + 1]) / total) for b in range(8)]
    rumble = 10 * np.log10(max(band(p, 5, 30), 1e-20) / total)

    def stereo(lo, hi):
        a, b, cc = band(pl, lo, hi), band(pr, lo, hi), band(c, lo, hi)
        s = a + b
        corr = cc / math.sqrt(a * b) if a * b > 0 else 0.0
        width = 100.0 * (s - 2 * cc) / (2 * s)
        loss = 10 * math.log10(max((s + 2 * cc) / (2 * s), 1e-6))
        return corr, width, loss

    corr, width, loss = stereo(20, 20000)
    low_corr, low_width, _ = stereo(20, 100)
    balance = 10 * math.log10(band(pl, 20, 20000) / band(pr, 20, 20000))
    return {
        "thirdOctaveDb": third_norm.tolist(),
        "spectralTilt": tilt,
        "spectralCentroidHz": centroid,
        "bandEnergyDb": band_energy,
        "subRumbleDb": rumble,
        "correlation": corr,
        "widthPercent": width,
        "monoLossDb": loss,
        "lowEndCorrelation": low_corr,
        "lowEndWidthPercent": low_width,
        "balanceDb": balance,
    }


# --- eventi tecnici -----------------------------------------------------------------

def runs(mask):
    """(inizio, fine) delle sequenze di True."""
    d = np.diff(np.concatenate([[0], mask.astype(np.int8), [0]]))
    return np.flatnonzero(d == 1), np.flatnonzero(d == -1)


def clip_analysis(x, fs):
    """
    Regola dell'app: >= 3 campioni a fondo scala, oppure >= 3 campioni identici sopra -6 dBFS a cui il segnale
    arriva ripido (passo > 6 quanti) o che durano più di quanto un picco lento a 20 Hz possa restare su un valore.
    Il quanto è la differenza minima tra campioni del file (qui calcolata sul file intero).
    Restituisce (inizio, fine, canale, tipo): tipo "fondo scala" o "plateau".
    """
    d = np.abs(np.diff(x, axis=0))
    q = min(float(d[d > 0].min()) if (d > 0).any() else 1 / 32768, 1 / 32768)
    w = 2 * math.pi * 20 / fs
    natural_len = 2 + math.ceil(2 * math.sqrt(q / (0.5 * w * w / 2)))
    events = []
    for ch in range(x.shape[1]):
        v = x[:, ch]
        a = np.abs(v)
        same = np.concatenate([[False], v[1:] == v[:-1]]) & (a >= 0.5) & np.concatenate([[False], a[:-1] >= 0.5])
        s, e = runs(same)
        flat = []
        for st, en in zip(s, e):
            start, length = st - 1, en - st + 1
            if length < 3:
                continue
            entry = max(abs(v[start] - v[start - 1]) if start >= 1 else 0.0, abs(v[start - 1] - v[start - 2]) if start >= 2 else 0.0)
            if entry > 6 * q:
                flat.append((start, en, "plateau"))
            elif length > natural_len:
                flat.append((start, en, "plateau"))
        fs_s, fs_e = runs(a >= 0.99995)
        full = [(st, en, "fondo scala") for st, en in zip(fs_s, fs_e) if en - st >= 3]
        last_end = -10
        for st, en, kind in sorted(flat + full):
            if st <= last_end:          # sovrapposti o contigui: stesso evento
                continue
            last_end = en
            events.append((int(st), int(en), ch, kind))
    return events


def dropout_analysis(x, fs):
    """Buchi di silenzio digitale >= 10 ms preceduti da audio sopra -40 dBFS entro 10 ms (no inizio/fine)."""
    out = []
    min_len = int(round(fs * 0.01))
    for ch in range(x.shape[1]):
        v = x[:, ch]
        nz = np.flatnonzero(v != 0.0)
        if len(nz) == 0:
            continue
        first, last = nz[0], nz[-1]
        s, e = runs(v == 0.0)
        for st, en in zip(s, e):
            if st <= first or en > last or en - st < min_len:
                continue
            pre = np.abs(v[max(0, st - min_len): st])
            if len(pre) and pre.max() > 10 ** (-40 / 20):
                out.append((int(st), int(en), ch))
    return out


# --- confronto ------------------------------------------------------------------------

def check(rows, name, app, ref, tol, unit=""):
    if app is None or ref is None or (isinstance(ref, float) and not math.isfinite(ref)):
        rows.append((name, app, ref, None, "n/d"))
        return True
    diff = app - ref
    ok = abs(diff) <= tol
    rows.append((name, app, ref, diff, "ok" if ok else f"FUORI (tol {tol}{unit})"))
    return ok


def fmt(v):
    if v is None:
        return "-"
    if isinstance(v, (int, np.integer)):
        return str(v)
    return f"{v:8.2f}"


def verify(json_path):
    meta = json.loads(Path(json_path).read_text(encoding="utf-8"))
    fs, raw = wavfile.read(str(Path(json_path).with_suffix(".wav")))
    x = raw.astype(np.float64)
    if x.ndim == 1:
        x = x[:, None]
    if x.shape[1] == 1:
        x = np.repeat(x, 2, axis=1)     # mono: l'app duplica il canale
    m = meta["metrics"]
    tl = meta["timeline"]
    rows = []

    loud = loudness_measures(x, fs)
    check(rows, "Integrated LUFS", m["integratedLufs"], loud["integratedLufs"], 0.1)
    check(rows, "LRA LU", m["loudnessRange"], loud["loudnessRange"], 0.3)
    check(rows, "Max momentary", m["maxMomentaryLufs"], loud["maxMomentaryLufs"], 0.15)
    check(rows, "Max short-term", m["maxShortTermLufs"], loud["maxShortTermLufs"], 0.15)

    sp = 20 * np.log10(np.maximum(np.abs(x).max(axis=0), 1e-12))
    for ch in range(2):
        check(rows, f"Sample peak {'LR'[ch]}", m["samplePeakDb"][ch], float(sp[ch]), 0.01)
    tp4, tp4_blocks = true_peak_blocks(x, fs, 4 if fs < 88000 else 2)
    tp8, _ = true_peak_blocks(x, fs, 8 if fs < 88000 else 4)
    for ch in range(2):
        check(rows, f"True peak {'LR'[ch]} (rif. 4x)", m["truePeakDb"][ch], float(tp4[ch]), 0.1)
    check(rows, "True peak max (rif. 8x)", m["truePeakMaxDb"], float(tp8.max()), 0.25)

    rms = 10 * math.log10(np.mean(x * x))
    check(rows, "RMS dB", m["rmsDb"], rms, 0.05)
    check(rows, "PLR", m["plr"], float(tp4.max()) - loud["integratedLufs"], 0.15)

    # PSR minimo: per ogni short-term > -40 LUFS, true peak degli ultimi 3 s (30 blocchi) meno lo short-term
    st = loud["shortTermSeries"]
    psr = []
    for k, v in enumerate(st):
        if v > -40.0:
            hi = min(k + 30, len(tp4_blocks))
            if hi > 0:
                psr.append(float(np.max(tp4_blocks[max(0, hi - 30): hi])) - v)
    check(rows, "PSR minimo", m["minPsr"], min(psr) if psr else None, 0.3)

    dr = dr_tt(x, fs)
    check(rows, "DR (TT)", m["drValue"] if m["drValid"] else None, dr, 0.3)
    for i, (a, r) in enumerate(zip(m["bandCrestDb"], band_crest(x, fs))):
        check(rows, f"Crest {['low', 'mid', 'high'][i]}", a, r, 0.7)

    spec = spectral_measures(x, fs)
    worst_third = max(range(31), key=lambda i: abs(m["thirdOctaveDb"][i] - spec["thirdOctaveDb"][i]))
    check(rows, f"Terzi d'ottava (peggiore, banda {worst_third})", m["thirdOctaveDb"][worst_third], spec["thirdOctaveDb"][worst_third], 0.5)
    check(rows, "Tilt dB/oct", m["spectralTilt"], spec["spectralTilt"], 0.1)
    check(rows, "Centroide Hz", m["spectralCentroidHz"], spec["spectralCentroidHz"], max(30.0, 0.03 * spec["spectralCentroidHz"]))
    worst_band = max(range(8), key=lambda b: abs(m["bandEnergyDb"][b] - spec["bandEnergyDb"][b]))
    check(rows, f"Energia di banda (peggiore, {worst_band})", m["bandEnergyDb"][worst_band], spec["bandEnergyDb"][worst_band], 0.3)
    check(rows, "Sub < 30 Hz", m["subRumbleDb"], spec["subRumbleDb"], 1.0)
    check(rows, "Correlazione", m["correlation"], spec["correlation"], 0.02)
    check(rows, "Larghezza %", m["widthPercent"], spec["widthPercent"], 1.0)
    check(rows, "Perdita in mono dB", m["monoLossDb"], spec["monoLossDb"], 0.1)
    check(rows, "Correlazione < 100 Hz", m["lowEndCorrelation"], spec["lowEndCorrelation"], 0.03)
    check(rows, "Larghezza < 100 Hz %", m["lowEndWidthPercent"], spec["lowEndWidthPercent"], 1.5)
    check(rows, "Bilanciamento L-R dB", m["balanceDb"], spec["balanceDb"], 0.05)
    for ch in range(2):
        dc = db(abs(float(np.mean(x[:, ch]))))
        if dc > -100:
            check(rows, f"DC offset {'LR'[ch]}", m["dcOffsetDb"][ch], dc, 1.0)

    # cronologia della forma d'onda (100 ms)
    app_st = np.array([v if v is not None else -150 for v in tl["shortTermLufs"]], dtype=float)
    n = min(len(app_st), len(st))
    loud_part = (st[:n] > -50)
    st_err = float(np.max(np.abs(app_st[:n][loud_part] - st[:n][loud_part]))) if loud_part.any() else 0.0
    rows.append(("Short-term nel tempo (max scarto)", None, None, st_err, "ok" if st_err <= 0.2 and abs(len(app_st) - len(st)) <= 1 else "FUORI"))
    app_tp = np.array([v if v is not None else -150 for v in tl["truePeakDb"]], dtype=float)
    n = min(len(app_tp), len(tp4_blocks))
    loud_part = tp4_blocks[:n] > -60
    tp_err = float(np.max(np.abs(app_tp[:n][loud_part] - tp4_blocks[:n][loud_part]))) if loud_part.any() else 0.0
    rows.append(("True peak nel tempo (max scarto)", None, None, tp_err, "ok" if tp_err <= 0.15 else "FUORI"))

    clips = clip_analysis(x, fs)
    rows.append(("Eventi di clipping", m["clipEvents"], len(clips), m["clipEvents"] - len(clips),
                 "ok" if m["clipEvents"] == len(clips) else "FUORI"))
    app_clips = sorted((c[0], c[2]) for c in tl["clips"])
    ref_clips = sorted((c[0], c[2]) for c in clips)
    if len(app_clips) == m["clipEvents"]:
        same = app_clips == ref_clips
        rows.append(("Posizione dei clip", None, None, None, "ok" if same else "FUORI"))
    drops = dropout_analysis(x, fs)
    app_drops = sorted((d[0], d[2]) for d in tl["dropouts"])
    ref_drops = sorted((d[0], d[2]) for d in drops)
    rows.append(("Buchi di silenzio", len(app_drops), len(ref_drops), len(app_drops) - len(ref_drops),
                 "ok" if app_drops == ref_drops else "FUORI"))

    return meta, rows, clips, tp4, tp8, loud


def main():
    folder = Path(sys.argv[1])
    failures = 0
    for jp in sorted(folder.glob("*.json")):
        meta, rows, clips, tp4, tp8, loud = verify(jp)
        name = Path(meta["file"]).name
        print(f"\n=== {name}  ({meta['sampleRate']:.0f} Hz, {meta['seconds']:.0f} s) ===")
        print(f"{'misura':42s} {'app':>8s} {'rif.':>8s} {'scarto':>8s}")
        for r in rows:
            print(f"{r[0]:42s} {fmt(r[1]):>8s} {fmt(r[2]):>8s} {fmt(r[3]):>8s}  {r[4]}")
            failures += r[4].startswith("FUORI")
        if clips:
            full = sum(1 for c in clips if c[3] == "fondo scala")
            print(f"  clip: {len(clips)} eventi ({full} a fondo scala, {len(clips) - full} plateau sotto il fondo scala)")
        print(f"  true peak 4x {tp4.max():.2f} / 8x {tp8.max():.2f} dBTP")
    print(f"\n{failures} misure fuori tolleranza")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
