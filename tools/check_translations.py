"""Controlla che ogni testo traducibile del codice abbia la sua traduzione in Resources/lang/en.txt.

Testi traducibili (sorgente in italiano):
    "testo"_t            letterale tradotto (anche concatenato: "a" "b"_t)
    tr ("testo")         funzione di traduzione con un letterale
    N_("testo")          testo marcato per la traduzione, tradotto più tardi con tr()

Uso:  python tools/check_translations.py [--unused]
Esce con codice 1 se mancano traduzioni.
"""
import io
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCE_DIRS = ["Source"]
LANG_FILE = os.path.join(ROOT, "Resources", "lang", "en.txt")

ESCAPES = {"n": "\n", "t": "\t", "r": "\r", '"': '"', "\\": "\\", "'": "'"}


def unescape(text):
    out, i = [], 0
    while i < len(text):
        c = text[i]
        if c == "\\" and i + 1 < len(text):
            out.append(ESCAPES.get(text[i + 1], text[i + 1]))
            i += 2
        else:
            out.append(c)
            i += 1
    return "".join(out)


def tokens(src):
    """Letterali stringa con posizione e suffisso; salta commenti e caratteri singoli."""
    i, n = 0, len(src)
    while i < n:
        c = src[i]
        if src.startswith("//", i):
            i = src.find("\n", i)
            i = n if i < 0 else i
        elif src.startswith("/*", i):
            i = src.find("*/", i + 2)
            i = n if i < 0 else i + 2
        elif c == "'":
            j = i + 1
            while j < n and src[j] != "'":
                j += 2 if src[j] == "\\" else 1
            i = j + 1
        elif c == '"':
            j = i + 1
            while j < n and src[j] != '"':
                j += 2 if src[j] == "\\" else 1
            m = re.match(r"[A-Za-z_]\w*", src[j + 1:])
            suffix = m.group(0) if m else ""
            yield i, j + 1 + len(suffix), src[i + 1:j], suffix
            i = j + 1 + len(suffix)
        else:
            i += 1


def literal_groups(src):
    """Letterali adiacenti (concatenati dal compilatore) raggruppati: (inizio, fine, testo, suffisso dell'ultimo)."""
    group = []
    for start, end, text, suffix in tokens(src):
        if group and src[group[-1][1]:start].strip() == "" and not group[-1][3]:
            group.append((start, end, text, suffix))
        else:
            if group:
                yield group[0][0], group[-1][1], "".join(unescape(g[2]) for g in group), group[-1][3]
            group = [(start, end, text, suffix)]
    if group:
        yield group[0][0], group[-1][1], "".join(unescape(g[2]) for g in group), group[-1][3]


def extract(path):
    src = io.open(path, encoding="utf-8").read()
    found = []
    for start, end, text, suffix in literal_groups(src):
        before = src[max(0, start - 8):start]
        after = src[end:end + 3].lstrip()
        if not text:
            continue
        if suffix == "_t":
            found.append(text)
        elif re.search(r"\b(tr|N_)\s*\(\s*$", before) and after.startswith(")"):
            found.append(text)
    return found


def load_translations():
    keys = {}
    for line in io.open(LANG_FILE, encoding="utf-8"):
        line = line.strip()
        m = re.match(r'^"((?:[^"\\]|\\.)*)"\s*=\s*"((?:[^"\\]|\\.)*)"\s*$', line)
        if m:
            keys[unescape(m.group(1))] = unescape(m.group(2))
    return keys


def main():
    translations = load_translations()
    used = {}
    for d in SOURCE_DIRS:
        for folder, _, files in os.walk(os.path.join(ROOT, d)):
            for f in files:
                if f.endswith((".cpp", ".h")):
                    path = os.path.join(folder, f)
                    for text in extract(path):
                        used.setdefault(text, os.path.relpath(path, ROOT))

    missing = sorted((t, p) for t, p in used.items() if t not in translations)
    for text, path in missing:
        print("MANCA  %s: %r" % (path, text))

    if "--unused" in sys.argv:
        for key in sorted(k for k in translations if k not in used):
            print("NON USATA  %r" % key)

    print("%d testi traducibili, %d traduzioni, %d mancanti" % (len(used), len(translations), len(missing)))
    return 1 if missing else 0


if __name__ == "__main__":
    sys.exit(main())
