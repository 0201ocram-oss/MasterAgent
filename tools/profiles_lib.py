"""
Calcoli condivisi della pipeline dei profili misurati (generate_profiles.py, bench.py).

Le misure per brano vengono da tools/study.py (tools/study_data/measurements.json). Gli scostamenti di banda
si calcolano con il motore del plugin (MasterAgentAnalyze --band-deltas), così l'allineamento è identico a
quello del confronto nell'app.
"""
import json
import os
import subprocess
import tempfile
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parent.parent
EXE = ROOT / "build" / "MasterAgentAnalyze_artefacts" / "Release" / "MasterAgentAnalyze.exe"
MEASUREMENTS = ROOT / "tools" / "study_data" / "measurements.json"

# Cache dei risultati del motore (scritti da MasterAgentAnalyze). Fuori dalla cartella del progetto: l'antivirus o
# l'accesso controllato alle cartelle di Windows possono impedire a un eseguibile appena compilato di scrivere in
# Download o Documenti. Si cambia con la variabile d'ambiente MA_STUDY_CACHE.
CACHE_ROOT = Path(os.environ.get("MA_STUDY_CACHE", Path(os.environ.get("LOCALAPPDATA", ROOT / "build")) / "MasterAgent"))

# genere delle misure -> id del profilo
PROFILE_OF_GENRE = {"pop": "pop", "rock": "rock", "hiphop": "hiphop_trap", "edm": "edm_club", "techno": "techno_house"}

# come tonalToleranceForBand (Comparator.cpp): tolleranza generica per banda = tonalTolerance x scala
BAND_SCALE = [1.6, 1.15, 1.0, 1.0, 1.0, 1.1, 1.2, 1.4]
MIN_ALBUMS = 2   # sotto questo numero di album il genere resta un modello


def load_tracks():
    if not MEASUREMENTS.exists():
        return []
    return json.loads(MEASUREMENTS.read_text(encoding="utf-8"))["tracks"]


def references(tracks, genre, exclude_album=None):
    """Brani di riferimento di un genere (senza un album, per la validazione incrociata)."""
    return [t for t in tracks if t["genre"] == genre and t["role"] == "reference" and t["album"] != exclude_album]


def by_album(tracks):
    albums = {}
    for t in tracks:
        albums.setdefault(t["album"], []).append(t)
    return albums


def is_measured(tracks, genre):
    return len(by_album(references(tracks, genre))) >= MIN_ALBUMS


def smooth_121(curve):
    """
    Media 1-2-1 tra terzi vicini, da 31.5 Hz a 16 kHz. I terzi di 20 e 25 Hz e di 20 kHz restano come misurati:
    con un solo vicino (o con la pendenza ripida del sub) la media li sposterebbe di diversi dB.
    """
    c = np.asarray(curve, dtype=float)
    out = c.copy()
    out[2:30] = (c[1:29] + 2 * c[2:30] + c[3:31]) / 4
    return out


def normalise(curve):
    """Come nel plugin: media dei terzi 63 Hz - 12.5 kHz = 0 dB."""
    c = np.asarray(curve, dtype=float)
    return c - np.mean(c[5:29])


def median_curve(tracks, field="thirdOctaveDb"):
    """Mediana per album, poi mediana tra gli album (ogni album pesa uguale), levigata e normalizzata."""
    per_album = []
    for album_tracks in by_album([t for t in tracks if t.get(field)]).values():
        per_album.append(np.median(np.array([t[field] for t in album_tracks], dtype=float), axis=0))
    if not per_album:
        return None
    return normalise(smooth_121(np.median(np.array(per_album), axis=0)))


def band_deltas(target, curves):
    """Scostamento di ogni banda (righe = curve) con l'allineamento di compare()."""
    with tempfile.TemporaryDirectory() as d:
        src, dst = Path(d) / "in.json", Path(d) / "out.json"
        src.write_text(json.dumps([{"target": [float(v) for v in target], "curves": [list(map(float, c)) for c in curves]}]))
        subprocess.run([str(EXE), "--band-deltas", str(src), str(dst)], check=True, capture_output=True)
        return np.array(json.loads(dst.read_text())[0], dtype=float)


MAX_BAND_TOLERANCE = 7.0   # oltre, la banda non segnalerebbe più nulla (Air dei master da CD: filtri diversi)


def band_tolerance(target, tracks, percentile, field="thirdOctaveDb", tonal_tolerance=2.5):
    """Percentile dello scostamento assoluto per banda, mai sotto la tolleranza generica e al massimo 7 dB."""
    d = np.abs(band_deltas(target, [t[field] for t in tracks if t.get(field)]))
    floor = [tonal_tolerance * s for s in BAND_SCALE]
    return [min(MAX_BAND_TOLERANCE, max(float(np.percentile(d[:, b], percentile)), floor[b])) for b in range(8)]


def measured_tonal(tracks, genre, exclude_album=None):
    """Curva e tolleranza per banda (p85) di un genere misurato, oppure None se gli album sono troppo pochi."""
    refs = references(tracks, genre, exclude_album)
    if len(by_album(refs)) < MIN_ALBUMS:
        return None
    curve = median_curve(refs)
    return {
        "tonalCurve": [round(float(v), 2) for v in curve],
        "bandTolerance": [round(v, 2) for v in band_tolerance(curve, refs, 85)],
        "tracks": len(refs),
        "albums": len(by_album(refs)),
    }
