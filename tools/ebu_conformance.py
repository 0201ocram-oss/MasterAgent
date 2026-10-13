"""
Conformità con i file di prova ufficiali EBU (Loudness Test Set v5): EBU Tech 3341 (loudness, true peak)
e Tech 3342 (Loudness Range), con i valori attesi e le tolleranze delle due norme (edizione 2023).

Il pacchetto si scarica dal browser (il sito blocca i download automatici):
    https://tech.ebu.ch/publications/ebu_loudness_test_set
e va estratto in external/ebu-loudness-test-set/ (cartella esclusa da git).

Uso:
    python tools/ebu_conformance.py [cartella]

I casi generabili in modo esatto sono anche nei test Catch2 (Tests/LoudnessTests.cpp, Tests/Tech3341Tests.cpp);
qui contano soprattutto i programmi reali (3341 #7 e #8 = 3342 #5 e #6) e i file ufficiali di true peak.
Si saltano il caso multicanale (3341 #6) e quelli per i meter "live" (#11, #14).
"""
import json
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
EXE = ROOT / "build" / "MasterAgentAnalyze_artefacts" / "Release" / "MasterAgentAnalyze.exe"

# (misura, atteso, tolleranza sotto, tolleranza sopra)
TECH3341 = {
    1: ("integratedLufs", -23.0, 0.1, 0.1), 2: ("integratedLufs", -33.0, 0.1, 0.1),
    3: ("integratedLufs", -23.0, 0.1, 0.1), 4: ("integratedLufs", -23.0, 0.1, 0.1), 5: ("integratedLufs", -23.0, 0.1, 0.1),
    7: ("integratedLufs", -23.0, 0.1, 0.1), 8: ("integratedLufs", -23.0, 0.1, 0.1),
    9: ("maxShortTermLufs", -23.0, 0.1, 0.1), 10: ("maxShortTermLufs", -23.0, 0.1, 0.1),
    12: ("maxMomentaryLufs", -23.0, 0.1, 0.1), 13: ("maxMomentaryLufs", -23.0, 0.1, 0.1),
    15: ("truePeakMaxDb", -6.0, 0.4, 0.2), 16: ("truePeakMaxDb", -6.0, 0.4, 0.2), 17: ("truePeakMaxDb", -6.0, 0.4, 0.2),
    18: ("truePeakMaxDb", -6.0, 0.4, 0.2), 19: ("truePeakMaxDb", 3.0, 0.4, 0.2), 20: ("truePeakMaxDb", 0.0, 0.4, 0.2),
    21: ("truePeakMaxDb", 0.0, 0.4, 0.2), 22: ("truePeakMaxDb", 0.0, 0.4, 0.2), 23: ("truePeakMaxDb", 0.0, 0.4, 0.2),
}
TECH3342 = {1: 10.0, 2: 5.0, 3: 20.0, 4: 15.0, 5: 5.0, 6: 15.0}


def expectations(name):
    """Controlli per un file in base al nome (es. seq-3341-7_seq-3342-5-24bit.wav)."""
    checks = []
    for case in re.findall(r"3341-(?:\d{4}-)?(\d+)", name):
        if int(case) in TECH3341:
            measure, expected, below, above = TECH3341[int(case)]
            checks.append((f"3341 #{case}", measure, expected, below, above))
    for case in re.findall(r"3342-(?:\d{4}-)?(\d+)", name):
        if int(case) in TECH3342:
            checks.append((f"3342 #{case}", "loudnessRange", TECH3342[int(case)], 1.0, 1.0))
    return checks


def main():
    folder = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "external" / "ebu-loudness-test-set"
    files = sorted(f for f in folder.rglob("*.wav") if expectations(f.name))
    if not files:
        sys.exit(f"Nessun file di prova EBU in {folder}: scarica il pacchetto da "
                 "https://tech.ebu.ch/publications/ebu_loudness_test_set ed estrailo lì.")

    with tempfile.TemporaryDirectory() as tmp:
        out = Path(tmp) / "ebu.json"
        subprocess.run([str(EXE), "--study", str(out)] + [str(f) for f in files], check=True, capture_output=True)
        measured = {Path(m["file"]).name: m for m in json.loads(out.read_text(encoding="utf-8"))}

    failures = 0
    print(f"{'file':48s} {'caso':10s} {'misura':18s} {'app':>8s} {'atteso':>8s}  esito")
    for f in files:
        m = measured.get(f.name)
        for case, measure, expected, below, above in expectations(f.name):
            value = m.get(measure) if m else None
            ok = value is not None and expected - below - 1e-6 <= value <= expected + above + 1e-6
            failures += 0 if ok else 1
            shown = f"{value:8.2f}" if value is not None else "       -"
            print(f"{f.name[:48]:48s} {case:10s} {measure:18s} {shown} {expected:8.1f}  {'ok' if ok else 'FUORI'}")
    print(f"\n{failures} fuori tolleranza")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
