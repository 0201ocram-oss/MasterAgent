"""
Banco di prova dell'accuratezza delle diagnosi su master commerciali.

Uso:
    python tools/bench.py [--jobs 6] [--per-album 3] [--max-minutes 8] [--report-only] [--name baseline]

Prima: python tools/study.py (misure e snapshot dei brani) e python tools/generate_profiles.py.

1. Profili "senza l'album del brano" (validazione incrociata): il profilo di genere con cui si giudica un brano
   non contiene il suo album, come succede con un mix nuovo. In build/bench/profiles/.
2. Job:
   - tutti i brani di riferimento (e i controlli) con la sola versione originale: misurano i falsi allarmi;
   - per ogni album alcuni brani con tutti i difetti noti: misurano quanti difetti l'app trova e con che precisione.
3. MasterAgentAnalyze --bench in parallelo; risultati in %LOCALAPPDATA%/MasterAgent/bench/results/
   (si riprende da dove si era fermato).
4. tools/bench_report.py scrive il report in tools/bench_results/.
"""
import argparse
import json
import re
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

import profiles_lib as lib

# priorità bassa: lo studio non rallenta il resto del PC
LOW_PRIORITY = getattr(subprocess, "BELOW_NORMAL_PRIORITY_CLASS", 0)
ROOT = lib.ROOT
WORK = lib.CACHE_ROOT / "bench"
PROFILES = ROOT / "Resources" / "profiles"


def slug(text):
    return re.sub(r"[^a-z0-9]+", "_", text.lower()).strip("_")


def leave_one_album_out_profiles(tracks):
    """Per ogni album di un genere misurato: il profilo del genere ricalcolato senza quell'album."""
    out = {}
    (WORK / "profiles").mkdir(parents=True, exist_ok=True)
    for genre, profile_id in lib.PROFILE_OF_GENRE.items():
        if not lib.is_measured(tracks, genre):
            continue
        base = json.loads((PROFILES / f"{profile_id}.json").read_text(encoding="utf-8"))
        for album in lib.by_album(lib.references(tracks, genre)):
            tonal = lib.measured_tonal(tracks, genre, exclude_album=album)
            if tonal is None:
                continue
            profile = dict(base, tonalCurve=tonal["tonalCurve"], bandTolerance=tonal["bandTolerance"])
            path = WORK / "profiles" / f"{profile_id}__{slug(album)}.json"
            path.write_text(json.dumps(profile, indent=1, ensure_ascii=False), encoding="utf-8")
            out[(genre, album)] = path
    return out


def evenly(items, n):
    if len(items) <= n:
        return list(items)
    step = (len(items) - 1) / (n - 1) if n > 1 else 0
    return [items[round(i * step)] for i in range(n)]


def make_jobs(tracks, per_album, max_minutes, loao):
    snapshots = json.loads((lib.CACHE_ROOT / "study" / "snapshots.json").read_text(encoding="utf-8"))
    jobs = []
    for album, album_tracks in lib.by_album(tracks).items():
        genre = album_tracks[0]["genre"]
        role = album_tracks[0]["role"]
        if genre not in lib.PROFILE_OF_GENRE:
            continue
        short = [t for t in album_tracks if t["seconds"] <= max_minutes * 60]
        full = set(t["file"] for t in evenly(short, per_album)) if role == "reference" else set()
        for t in album_tracks:
            others = [snapshots[o["file"]] for o in album_tracks if o["file"] != t["file"] and o["file"] in snapshots]
            job = {
                "id": slug(t["file"]),
                "file": str(ROOT / t["file"]),
                "genre": lib.PROFILE_OF_GENRE[genre],
                "album": album,
                "role": role,
                "albumSnapshots": others,
                "variants": "all" if t["file"] in full else "original",
            }
            if (genre, album) in loao:
                job["genreProfile"] = str(loao[(genre, album)])
            jobs.append(job)
    return jobs


def run(chunk_file, results):
    exe = lib.EXE
    done = subprocess.run([str(exe), "--bench", str(chunk_file), str(results)], capture_output=True, text=True,
                          encoding="utf-8", errors="replace",
                          creationflags=LOW_PRIORITY)
    return chunk_file.name, done.returncode, done.stdout.count("\n")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--jobs", type=int, default=6)
    parser.add_argument("--per-album", type=int, default=3)
    parser.add_argument("--max-minutes", type=float, default=8.0)
    parser.add_argument("--report-only", action="store_true")
    parser.add_argument("--name", default="", help="nome del report (es. baseline)")
    parser.add_argument("--fresh", action="store_true", help="cancella i risultati precedenti")
    parser.add_argument("--originals-first", action="store_true", help="prima gli originali, poi i brani con i difetti")
    args = parser.parse_args()

    results = WORK / "results"
    if not args.report_only:
        tracks = lib.load_tracks()
        if not tracks:
            sys.exit("Mancano le misure: lancia prima python tools/study.py")
        if args.fresh and results.exists():
            for f in results.glob("*.json"):
                f.unlink()
        results.mkdir(parents=True, exist_ok=True)

        loao = leave_one_album_out_profiles(tracks)
        jobs = make_jobs(tracks, args.per_album, args.max_minutes, loao)
        # i job lunghi (tutte le varianti) per primi e distribuiti a turno tra i processi;
        # con --originals-first prima gli originali (brevi): i falsi allarmi sono pronti presto
        jobs.sort(key=lambda j: (j["variants"] == "all") == args.originals_first)
        chunks = [jobs[i:: args.jobs] for i in range(args.jobs)]
        files = []
        for i, chunk in enumerate(chunks):
            path = WORK / f"jobs_{i}.json"
            path.write_text(json.dumps(chunk, indent=1, ensure_ascii=False), encoding="utf-8")
            files.append(path)
        full = sum(1 for j in jobs if j["variants"] == "all")
        print(f"{len(jobs)} brani ({full} con tutti i difetti), {len(loao)} profili senza un album; {args.jobs} processi")
        with ThreadPoolExecutor(max_workers=args.jobs) as pool:
            for name, code, lines in pool.map(lambda f: run(f, results), files):
                print(f"  {name}: {'ok' if code == 0 else 'ERRORE'} ({lines} brani)")

    report = subprocess.run([sys.executable, str(ROOT / "tools" / "bench_report.py"), str(results)] + ([args.name] if args.name else []))
    sys.exit(report.returncode)


if __name__ == "__main__":
    main()
