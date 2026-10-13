"""
Pipeline dei profili misurati, passo 1: analizza i brani di riferimento con lo stesso motore del plugin.

Uso:
    python tools/study.py [--jobs 6] [--force] [--only rock,pop]

Le cartelle dei brani sono in tools/study_sources.json (l'audio non è nel repository). Una cartella con
sottocartelle conta un album per sottocartella: per un genere nuovo basta copiare gli album in
MasterExample/<genere>/<album>/ e rilanciare.

Scrive:
    tools/study_data/measurements.json   misure per brano (solo numeri), da versionare
    %LOCALAPPDATA%/MasterAgent/study/     cache dei risultati per album e snapshot completi (banco di prova);
                                          fuori dal progetto perché l'antivirus può impedire all'analizzatore
                                          di scrivere in Download (cartella diversa con MA_STUDY_CACHE)
Poi: python tools/generate_profiles.py
"""
import argparse
import fnmatch
import json
import os
import re
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from datetime import date
from pathlib import Path

# priorità bassa: lo studio non rallenta il resto del PC
LOW_PRIORITY = getattr(subprocess, "BELOW_NORMAL_PRIORITY_CLASS", 0)
ROOT = Path(__file__).resolve().parent.parent
EXE = ROOT / "build" / "MasterAgentAnalyze_artefacts" / "Release" / "MasterAgentAnalyze.exe"
SOURCES = ROOT / "tools" / "study_sources.json"
CACHE = Path(os.environ.get("MA_STUDY_CACHE", Path(os.environ.get("LOCALAPPDATA", ROOT / "build")) / "MasterAgent")) / "study"
SNAPSHOTS = CACHE / "snapshots"
OUT = ROOT / "tools" / "study_data" / "measurements.json"
AUDIO = {".wav", ".flac", ".aif", ".aiff", ".mp3", ".ogg"}
CHUNK = 6   # brani per processo: gli album lunghi si dividono tra più processi


def slug(text):
    return re.sub(r"[^a-z0-9]+", "_", text.lower()).strip("_")


def albums():
    """Album da analizzare: file diretti della cartella (con filtro) più un album per ogni sottocartella."""
    sources = json.loads(SOURCES.read_text(encoding="utf-8"))
    found = []
    for a in sources["albums"]:
        folder = ROOT / a["path"]
        if not folder.is_dir():
            continue
        pattern = a.get("files", "*")
        direct = sorted(f for f in folder.iterdir()
                        if f.is_file() and f.suffix.lower() in AUDIO and fnmatch.fnmatch(f.name, pattern))
        if direct:
            found.append(dict(a, tracks=direct, key=slug(a["title"])))
        for sub in sorted(d for d in folder.iterdir() if d.is_dir()):
            tracks = sorted(f for f in sub.rglob("*") if f.is_file() and f.suffix.lower() in AUDIO)
            if tracks:
                found.append(dict(a, title=f"{a['title']}: {sub.name}", tracks=tracks, key=slug(a["title"] + "_" + sub.name)))
    return found


def run_chunk(out, tracks):
    cmd = [str(EXE), "--study", str(out), "--snapshots", str(SNAPSHOTS)] + [str(t) for t in tracks]
    done = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace",
                          creationflags=LOW_PRIORITY)
    skipped = [line for line in done.stdout.splitlines() if line.startswith("Saltato")]
    return out, done.returncode, skipped


def rounded(v):
    if isinstance(v, float):
        return round(v, 3)
    if isinstance(v, list):
        return [rounded(x) for x in v]
    return v


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--jobs", type=int, default=6)
    parser.add_argument("--force", action="store_true", help="rianalizza anche gli album già in cache")
    parser.add_argument("--only", default="", help="generi da analizzare, separati da virgola")
    args = parser.parse_args()

    if not EXE.exists():
        sys.exit(f"Manca {EXE}: compila prima MasterAgentAnalyze")
    CACHE.mkdir(parents=True, exist_ok=True)
    only = {g for g in args.only.split(",") if g}
    selected = [a for a in albums() if not only or a["genre"] in only]

    # un processo per gruppo di brani; la cache vale finché brani e motore non cambiano
    work = []
    engine_time = EXE.stat().st_mtime
    for a in selected:
        for i in range(0, len(a["tracks"]), CHUNK):
            chunk = a["tracks"][i: i + CHUNK]
            out = CACHE / f"{a['key']}_{i // CHUNK}.json"
            newest = max([engine_time] + [t.stat().st_mtime for t in chunk])
            if args.force or not out.exists() or out.stat().st_mtime < newest:
                work.append((out, chunk))

    print(f"{len(selected)} album, {sum(len(a['tracks']) for a in selected)} brani; da analizzare: {sum(len(c) for _, c in work)}")
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for out, code, skipped in pool.map(lambda w: run_chunk(*w), work):
            print(f"  {out.name}: {'ok' if code == 0 else 'ERRORE'}")
            for line in skipped:
                print("    " + line)

    # misure per brano con album, genere e ruolo; gli snapshot restano nella cache locale
    tracks, index = [], {}
    for a in albums():
        for i in range(0, len(a["tracks"]), CHUNK):
            part = CACHE / f"{a['key']}_{i // CHUNK}.json"
            if not part.exists():
                continue
            for m in json.loads(part.read_text(encoding="utf-8")):
                rel = Path(m.pop("file")).resolve().relative_to(ROOT).as_posix()
                snap = m.pop("snapshot", None)
                if snap:
                    index[rel] = snap
                tracks.append({"file": rel, "album": a["title"], "genre": a["genre"], "role": a["role"], "year": a.get("year", 0),
                               **{k: rounded(v) for k, v in m.items()}})

    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(json.dumps({"generated": date.today().isoformat(), "tracks": tracks}, indent=1, ensure_ascii=False),
                   encoding="utf-8")
    (CACHE / "snapshots.json").write_text(json.dumps(index, indent=1), encoding="utf-8")

    by_genre = {}
    for t in tracks:
        key = (t["genre"], t["role"])
        by_genre[key] = by_genre.get(key, 0) + 1
    print(f"\n{len(tracks)} brani in {OUT.relative_to(ROOT).as_posix()}")
    for (genre, role), n in sorted(by_genre.items()):
        print(f"  {genre:8s} {role:9s} {n}")


if __name__ == "__main__":
    main()
