"""
Report del banco di prova (tools/bench.py): falsi allarmi sui master originali, difetti noti trovati,
precisione delle mosse EQ e dei tratti localizzati.

Uso:
    python tools/bench_report.py <cartella risultati> [nome]
Scrive tools/bench_results/<data>[_nome].md e stampa il riepilogo.

Confronti:
- genere: profilo del genere calcolato senza l'album del brano (come per un mix nuovo);
- album: profilo dagli altri brani dello stesso album (come un buon reference);
- reference: l'originale stesso come reference (il target perfetto: misura la sensibilità del confronto).
Un difetto conta come "trovato" se compare la diagnosi giusta (chiave, verso) almeno "da verificare".
Nei confronti genere e album si contano a parte i brani in cui l'originale aveva già quella diagnosi.
"""
import json
import math
import os
import statistics
import sys
from datetime import date
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BANDS = [20, 60, 250, 500, 2000, 4000, 6000, 12000, 20000]
BAND_NAMES = ["Sub", "Bass", "Low-mid", "Mid", "High-mid", "Presence", "Brilliance", "Air"]
MODES = ["genre", "album", "reference"]
MODE_NAMES = {"genre": "profilo di genere", "album": "profilo dell'album", "reference": "originale come reference"}
WARNING, CRITICAL = 2, 3
CATEGORIES = ["Loudness", "Peak", "Dinamica", "Tonale", "Stereo", "Tecnico"]


def band_of(freq):
    for b in range(8):
        if freq < BANDS[b + 1]:
            return b
    return 7


def findings(result, min_severity=WARNING):
    return [f for f in result.get("findings", []) if f["severity"] >= min_severity]


def has(result, predicate, min_severity=WARNING):
    return any(predicate(f) for f in findings(result, min_severity))


def overlap(a0, a1, b0, b1):
    inter = max(0.0, min(a1, b1) - max(a0, b0))
    union = max(a1, b1) - min(a0, b0)
    return inter / union if union > 0 else 0.0


# --- che cosa deve trovare l'app per ogni difetto ------------------------------------------------------------

def expectation(variant, params):
    """Predicato sul risultato di un confronto (True = difetto trovato), o None se la variante non ha attesa."""
    name = variant
    if name == "eq":
        b, up = band_of(params["freq"]), params["gain"] > 0
        return lambda r: has(r, lambda f: f["key"] == f"band:{b}" and (f["delta"] or 0) * (1 if up else -1) > 0)
    if name in ("tilt+", "tilt-"):
        brighter = name == "tilt+"

        def tilt(r):
            if has(r, lambda f: f["key"] == "tilt" and (f["delta"] or 0) * (1 if brighter else -1) > 0):
                return True
            # oppure una mossa EQ a mensola nel verso giusto
            for m in r.get("eqMoves", []):
                if m["type"].lower().startswith("high") and (m["gainDb"] < 0) == brighter:
                    return True
                if m["type"].lower().startswith("low") and (m["gainDb"] > 0) == brighter:
                    return True
            return False
        return tilt
    if name in ("width+", "width-"):
        wider = name == "width+"
        return lambda r: has(r, lambda f: (f["key"] == "width" or f["key"].startswith("bandWidth:"))
                             and (f["delta"] or 0) * (1 if wider else -1) > 0)
    if name == "lowSide":
        return lambda r: has(r, lambda f: f["key"] in ("lowEndWidth", "bandCorr:0", "bandCorr:1", "bandWidth:0", "bandWidth:1"))
    if name == "polarity":
        return lambda r: has(r, lambda f: f["metric"] == "Fase invertita", CRITICAL)
    if name == "balance":
        need = CRITICAL if params["db"] >= 2.5 else WARNING
        return lambda r: has(r, lambda f: f["key"] == "balance" and f["metric"].startswith("Bilanciamento"), need)
    if name == "limit":
        return lambda r: has(r, lambda f: f["key"] in ("plr", "psr", "dr", "crest:0", "crest:1", "crest:2") and (f["delta"] or 0) < 0)
    if name == "clip":
        return lambda r: has(r, lambda f: f["key"] == "clip", CRITICAL)
    if name == "gain":
        return lambda r: has(r, lambda f: f["key"] == "integrated" and (f["delta"] or 0) < 0)
    if name == "dc":
        return lambda r: has(r, lambda f: f["key"] == "dc")
    if name == "mute":
        return lambda r: has(r, lambda f: f["metric"].startswith("Canale R muto"), CRITICAL)
    if name == "rumble":
        return lambda r: has(r, lambda f: f["key"] == "rumble")
    if name == "dropouts":
        return lambda r: sum(1 for m in r.get("markers", []) if m["type"] == 2) >= 3 if "markers" in r else None
    if name == "resonance":
        target = params["freq"]
        return lambda r: has(r, lambda f: f["key"].startswith("resonance:")
                             and abs(math.log2(max(1.0, float(f["key"].split(":")[1])) / target)) < 0.05)
    if name == "sibilance":
        return lambda r: has(r, lambda f: f["key"] == "band:6" and (f["delta"] or 0) > 0)
    if name == "section":
        def section(r):
            for f in findings(r):
                if f["key"] == "band:2" and (f["delta"] or 0) > 0:
                    return True
            return False
        return section
    return None


def variant_label(v):
    p = v["params"]
    if v["variant"] == "eq":
        return f"EQ {p['gain']:+.0f} dB a {p['freq']:.0f} Hz ({BAND_NAMES[band_of(p['freq'])]})"
    labels = {
        "tilt+": "Tilt più brillante", "tilt-": "Tilt più scuro", "width+": "Side +4 dB", "width-": "Side -6 dB",
        "lowSide": "Bassi in controfase (side sotto 120 Hz)", "polarity": "Canale R invertito",
        "limit": "+4 dB in un limiter", "clip": "+3 dB con clipping a 0 dBFS", "gain": "Guadagno -6 dB",
        "dc": "DC offset -40 dBFS", "mute": "Canale R muto", "rumble": "Rumble a 15 Hz", "dropouts": "3 buchi da 50 ms",
        "resonance": "Risonanza +10 dB (Q 20) tra due note", "sibilance": "Sibilanti a raffiche 6-9 kHz",
        "section": "+8 dB a 300 Hz tra 30 e 60 s",
    }
    if v["variant"] == "balance":
        return f"Bilanciamento {p['db']:.1f} dB"
    return labels.get(v["variant"], v["variant"])


def pct(n, d):
    return f"{100.0 * n / d:.0f}%" if d else "-"


def main():
    folder = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(os.environ.get("LOCALAPPDATA", ROOT / "build")) / "MasterAgent" / "bench" / "results"
    name = sys.argv[2] if len(sys.argv) > 2 else ""
    tracks = [json.loads(p.read_text(encoding="utf-8")) for p in sorted(folder.glob("*.json"))]
    if not tracks:
        sys.exit("Nessun risultato")

    # ruolo di ogni brano (riferimento o controllo) dalle misure dello studio
    measurements = ROOT / "tools" / "study_data" / "measurements.json"
    roles = {}
    if measurements.exists():
        for m in json.loads(measurements.read_text(encoding="utf-8"))["tracks"]:
            roles[(ROOT / m["file"]).resolve().as_posix().lower()] = m["role"]
    for t in tracks:
        t.setdefault("role", roles.get(Path(t["file"]).resolve().as_posix().lower(), "reference"))

    lines = [f"# Banco di prova dell'accuratezza ({date.today().isoformat()}{', ' + name if name else ''})", ""]

    # --- 1. falsi allarmi sui master originali ---------------------------------------------------------------
    originals = [(t, v) for t in tracks for v in t["variants"] if v["variant"] == "original"]
    refs = [(t, v) for t, v in originals if t.get("role", "reference") == "reference"]
    lines += ["## 1. Falsi allarmi sui master commerciali originali", "",
              f"{len(refs)} brani di riferimento, master. Quota di brani con almeno una diagnosi da verificare (o critica) per area.", ""]
    summary = {}
    for mode in ("genre", "album"):
        rows = [v["results"][mode] for _, v in refs if mode in v["results"]]
        if not rows:
            continue
        lines.append(f"**Confronto con il {MODE_NAMES[mode]}** ({len(rows)} brani, voto medio {statistics.mean(r['score'] for r in rows):.0f})")
        lines.append("")
        lines.append("| area | da verificare o critica | critica |")
        lines.append("|---|---|---|")
        for cat in CATEGORIES:
            warn = sum(1 for r in rows if any(f["category"] == cat for f in findings(r)))
            crit = sum(1 for r in rows if any(f["category"] == cat for f in findings(r, CRITICAL)))
            lines.append(f"| {cat} | {pct(warn, len(rows))} | {pct(crit, len(rows))} |")
        anyw = sum(1 for r in rows if findings(r))
        lines.append(f"| **qualsiasi** | **{pct(anyw, len(rows))}** | {pct(sum(1 for r in rows if findings(r, CRITICAL)), len(rows))} |")
        lines.append("")
        counts = {}
        for r in rows:
            for metric in {f["metric"] for f in findings(r)}:
                counts[metric] = counts.get(metric, 0) + 1
        top = sorted(counts.items(), key=lambda kv: -kv[1])[:12]
        lines.append("Diagnosi più frequenti: " + "; ".join(f"{m} {pct(n, len(rows))}" for m, n in top))
        lines.append("")
        summary[f"falsi allarmi tonali ({mode})"] = pct(sum(1 for r in rows if any(f["category"] == "Tonale" for f in findings(r))), len(rows))
        summary[f"almeno una diagnosi ({mode})"] = pct(anyw, len(rows))

    controls = [(t, v) for t, v in originals if t.get("role") == "control"]
    if controls:
        lines += ["**Controlli** (esempi estremi): diagnosi critiche con il profilo di genere", ""]
        for t, v in controls:
            r = v["results"].get("genre", {})
            crit = sorted({f["metric"] for f in findings(r, CRITICAL)})
            lines.append(f"- {Path(t['file']).name} ({t.get('album', '')}): voto {r.get('score', '-')}, critiche: {', '.join(crit) or 'nessuna'}")
        lines.append("")

    # --- 2. difetti noti trovati ------------------------------------------------------------------------------
    lines += ["## 2. Difetti noti trovati", "",
              "Quota di brani in cui compare la diagnosi giusta. Tra parentesi, per genere e album: i brani in cui "
              "l'originale aveva già quella diagnosi (contati come trovati, ma non dicono nulla).", "",
              "| difetto | " + " | ".join(MODE_NAMES[m] for m in MODES) + " |", "|---|---|---|---|"]
    groups = {}
    for t in tracks:
        # la risonanza cambia frequenza da brano a brano: si raggruppa solo per tipo
        variants = {(v["variant"], "{}" if v["variant"] == "resonance" else json.dumps(v["params"], sort_keys=True)): v
                    for v in t["variants"]}
        original = variants.get(("original", "{}"))
        for key, v in variants.items():
            if v["variant"] == "original":
                continue
            check = expectation(v["variant"], v["params"])
            if check is None:
                continue
            g = groups.setdefault((v["variant"], key[1]), {"label": variant_label(v), "stats": {m: [0, 0, 0] for m in MODES}})
            for mode in MODES:
                if mode not in v["results"]:
                    continue
                found = check(v["results"][mode])
                if found is None:
                    continue
                already = bool(original and mode in original["results"] and mode != "reference" and check(original["results"][mode]))
                s = g["stats"][mode]
                s[0] += 1
                s[1] += 1 if found else 0
                s[2] += 1 if already else 0

    def order(item):
        (variant, params), _ = item
        p = json.loads(params)
        return (variant != "eq", p.get("freq", 0), -abs(p.get("gain", 0)), -p.get("gain", 0), variant)

    detection = {m: [0, 0] for m in MODES}
    for (variant, params), g in sorted(groups.items(), key=order):
        cells = []
        for mode in MODES:
            n, found, already = g["stats"][mode]
            if n == 0:
                cells.append("-")
                continue
            cells.append(pct(found, n) + (f" ({already})" if already else ""))
            detection[mode][0] += n
            detection[mode][1] += found
        lines.append(f"| {g['label']} | " + " | ".join(cells) + " |")
    lines.append("")
    for mode in MODES:
        summary[f"difetti trovati ({mode})"] = pct(detection[mode][1], detection[mode][0])

    # --- 3. precisione dei consigli -------------------------------------------------------------------------------
    lines += ["## 3. Precisione dei consigli", ""]
    for mode in ("reference", "album", "genre"):
        freq_err, gain_err, n_with = [], [], 0
        n_total = 0
        for t in tracks:
            for v in t["variants"]:
                if v["variant"] != "eq" or mode not in v["results"]:
                    continue
                n_total += 1
                f0, g0 = v["params"]["freq"], v["params"]["gain"]
                moves = [m for m in v["results"][mode].get("eqMoves", []) if (m["gainDb"] < 0) == (g0 > 0)]
                if not moves:
                    continue
                best = min(moves, key=lambda m: abs(math.log2(m["frequency"] / f0)))
                n_with += 1
                freq_err.append(abs(math.log2(best["frequency"] / f0)))
                gain_err.append(abs(best["gainDb"]) - abs(g0))
        if n_total:
            line = f"- **Mosse EQ, {MODE_NAMES[mode]}**: una mossa nel verso giusto in {pct(n_with, n_total)} dei casi"
            if freq_err:
                within = sum(1 for e in freq_err if e <= 0.5)
                line += (f"; frequenza entro mezza ottava nel {pct(within, len(freq_err))} (errore mediano {statistics.median(freq_err):.2f} ott.), "
                         f"guadagno stimato meno quello inserito: mediana {statistics.median(gain_err):+.1f} dB")
                if mode == "reference":
                    summary["mosse EQ entro mezza ottava (reference)"] = pct(within, len(freq_err))
            lines.append(line)

    ious = []
    for t in tracks:
        for v in t["variants"]:
            if v["variant"] != "section" or "reference" not in v["results"]:
                continue
            for f in findings(v["results"]["reference"]):
                if f["key"] == "band:2" and f.get("timeStart") is not None:
                    ious.append(overlap(f["timeStart"], f["timeEnd"], 30.0, 60.0))
    if ious:
        lines.append(f"- **Tratto localizzato** (+8 dB a 300 Hz tra 30 e 60 s, reference): indicato in {len(ious)} casi, "
                     f"sovrapposizione mediana {statistics.median(ious):.0%}")
    lines.append("")

    lines += ["## Riepilogo", ""] + [f"- {k}: {v}" for k, v in summary.items()] + [""]

    out_dir = ROOT / "tools" / "bench_results"
    out_dir.mkdir(parents=True, exist_ok=True)
    out = out_dir / f"{date.today().isoformat()}{'_' + name if name else ''}.md"
    out.write_text("\n".join(lines), encoding="utf-8")
    print("\n".join(lines[-(len(summary) + 3):]))
    print(f"\nReport: {out.relative_to(ROOT).as_posix()}")


if __name__ == "__main__":
    main()
