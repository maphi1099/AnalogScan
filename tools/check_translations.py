"""Prüft, dass jeder übersetzbare Text der Oberfläche eine englische Übersetzung hat (LF-UI-03).

Sucht alle tr("…")-Literale in src/ui/src/*.cpp und vergleicht sie mit der Tabelle in
src/ui/src/translations.cpp (Zeilen der Form {"Deutsch", "English"},). Meldet fehlende und
überzählige Einträge. Aufruf: python -X utf8 check_translations.py <quellwurzel> [--list]
"""
import glob
import os
import re
import sys

TR = re.compile(r'\btr\(\s*"((?:[^"\\]|\\.)*)"')
ENTRY = re.compile(r'^\s*\{\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*\},?\s*$')


def sources(root):
    found = set()
    for f in sorted(glob.glob(os.path.join(root, 'src', 'ui', 'src', '*.cpp'))):
        if os.path.basename(f) == 'translations.cpp':
            continue
        with open(f, encoding='utf-8') as fh:
            found.update(m.group(1) for m in TR.finditer(fh.read()))
    return found


def table(root):
    """Einträge der Tabelle; zweites Ergebnis: Texte aus dem Kern (Abschnitt "Schrittnamen"),
    die nicht über tr() laufen und deshalb nicht als überzählig gelten."""
    entries = {}
    core = set()
    in_core = False
    with open(os.path.join(root, 'src', 'ui', 'src', 'translations.cpp'), encoding='utf-8') as fh:
        for line in fh:
            if 'Schrittnamen' in line:
                in_core = True
            m = ENTRY.match(line)
            if m:
                entries[m.group(1)] = m.group(2)
                if in_core:
                    core.add(m.group(1))
    return entries, core


def main():
    root = sys.argv[1]
    texts = sources(root)
    if '--list' in sys.argv:
        for t in sorted(texts):
            print(t)
        return 0
    known, core = table(root)
    missing = sorted(texts - set(known))
    extra = sorted(set(known) - texts - core)
    empty = sorted(k for k, v in known.items() if not v)
    for t in missing:
        print(f'  fehlt: "{t}"')
    for t in extra:
        print(f'  überzählig: "{t}"')
    for t in empty:
        print(f'  leer: "{t}"')
    print(f'{len(texts)} Texte, {len(known)} Übersetzungen, {len(missing)} fehlen')
    return 1 if missing or empty else 0


if __name__ == '__main__':
    sys.exit(main())
