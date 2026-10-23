"""Unabhängige Prüfung des von QtDocumentRenderer geschriebenen Kontaktabzugs (K-15).

Aufruf durch qtprint_test.cpp:  python -X utf8 verify_pdf.py <pdf> <expected.txt>

expected.txt enthält eine Zeile "page <breite_mm> <höhe_mm> <dpi>" und je Bild
"slot <x> <y> <w> <h>" in mm (Ursprung oben links, wie im Layout). Gelesen wird mit pypdf;
der Inhaltsstrom wird selbst ausgewertet (q/Q/cm/Do), damit die Lage jedes Bildes aus der
Transformationsmatrix folgt. Geprüft:
  - Seitengröße wie erwartet (±0,1 mm),
  - zu jedem Slot ein Bild an derselben Stelle in derselben Größe (±0,2 mm),
  - effektive Auflösung jedes Bildes ≥ dpi,
  - ICC-Profil eingebettet (Output Intent mit DestOutputProfile oder ICCBased-Farbraum),
  - Beschriftung als Text vorhanden (Zeilen "text ..." in expected.txt, z. B. Rolle, Seitenzähler).
"""
import sys

from pypdf import PdfReader
from pypdf.generic import ContentStream

MM_PER_PT = 25.4 / 72.0
failures = []


def fail(msg):
    failures.append(msg)
    print('  FEHLER:', msg)


def mul(a, b):
    """Matrixprodukt a·b für PDF-Matrizen [a b c d e f]."""
    return [a[0] * b[0] + a[1] * b[2], a[0] * b[1] + a[1] * b[3],
            a[2] * b[0] + a[3] * b[2], a[2] * b[1] + a[3] * b[3],
            a[4] * b[0] + a[5] * b[2] + b[4], a[4] * b[1] + a[5] * b[3] + b[5]]


def apply(m, x, y):
    return m[0] * x + m[2] * y + m[4], m[1] * x + m[3] * y + m[5]


def images_on(page, reader):
    """Liste (bbox_pt, breite_px, höhe_px, objekt) aller gezeichneten Bilder der Seite."""
    out = []

    def walk(contents, resources, ctm):
        stack = []
        ops = ContentStream(contents, reader).operations
        for operands, op in ops:
            if op == b'q':
                stack.append(ctm)
            elif op == b'Q':
                ctm = stack.pop()
            elif op == b'cm':
                ctm = mul([float(v) for v in operands], ctm)
            elif op == b'Do':
                xobjs = resources.get('/XObject', {})
                xobj = xobjs[operands[0]].get_object()
                sub = xobj.get('/Subtype')
                if sub == '/Image':
                    pts = [apply(ctm, x, y) for x, y in ((0, 0), (1, 0), (0, 1), (1, 1))]
                    xs = [p[0] for p in pts]
                    ys = [p[1] for p in pts]
                    out.append(((min(xs), min(ys), max(xs), max(ys)),
                                int(xobj['/Width']), int(xobj['/Height']), xobj))
                elif sub == '/Form':
                    m = [float(v) for v in xobj.get('/Matrix', [1, 0, 0, 1, 0, 0])]
                    walk(xobj, xobj.get('/Resources', resources).get_object(), mul(m, ctm))

    walk(page.get_contents(), page['/Resources'].get_object(), [1, 0, 0, 1, 0, 0])
    return out


def has_icc(cs):
    if cs is None:
        return False
    cs = cs.get_object()
    if isinstance(cs, list) and len(cs) >= 2 and cs[0] == '/ICCBased':
        return len(cs[1].get_object().get_data()) > 128
    return False


def main():
    pdf_path, expected_path = sys.argv[1], sys.argv[2]
    page_w = page_h = dpi = None
    slots = []
    texts = []
    with open(expected_path, encoding='utf-8') as f:
        for line in f:
            parts = line.split()
            if parts and parts[0] == 'page':
                page_w, page_h, dpi = float(parts[1]), float(parts[2]), int(parts[3])
            elif parts and parts[0] == 'slot':
                slots.append(tuple(float(v) for v in parts[1:5]))
            elif parts and parts[0] == 'text':
                texts.append(line.rstrip('\n')[5:])

    reader = PdfReader(pdf_path)
    print(f'  {pdf_path}: {len(reader.pages)} Seite(n), {len(slots)} Slots erwartet')
    if len(reader.pages) != 1:
        fail(f'{len(reader.pages)} Seiten statt 1')
    page = reader.pages[0]
    box = page.mediabox
    w_mm = float(box.width) * MM_PER_PT
    h_mm = float(box.height) * MM_PER_PT
    if abs(w_mm - page_w) > 0.1 or abs(h_mm - page_h) > 0.1:
        fail(f'Seitengröße {w_mm:.2f} × {h_mm:.2f} mm statt {page_w} × {page_h}')

    imgs = images_on(page, reader)
    print(f'  {len(imgs)} Bilder im Inhaltsstrom')
    # Bilder in mm mit Ursprung oben links (Layout-Koordinaten)
    placed = []
    for (x0, y0, x1, y1), wpx, hpx, obj in imgs:
        rect = (x0 * MM_PER_PT, page_h - y1 * MM_PER_PT, (x1 - x0) * MM_PER_PT, (y1 - y0) * MM_PER_PT)
        placed.append((rect, wpx, hpx, obj))

    for s in slots:
        best = min(placed, key=lambda p: sum(abs(a - b) for a, b in zip(p[0], s)), default=None)
        if best is None:
            fail(f'kein Bild für Slot {s}')
            continue
        rect, wpx, hpx, obj = best
        err = max(abs(a - b) for a, b in zip(rect, s))
        if err > 0.2:
            fail(f'Slot {s}: Bild bei {tuple(round(v, 3) for v in rect)}, Abweichung {err:.3f} mm')
        eff_x = wpx / (rect[2] / 25.4)
        eff_y = hpx / (rect[3] / 25.4)
        if min(eff_x, eff_y) < dpi - 0.5:
            fail(f'Slot {s}: Auflösung {eff_x:.1f} × {eff_y:.1f} dpi < {dpi}')

    # ICC: Output Intent im Katalog oder ICCBased-Farbraum der Bilder
    root = reader.trailer['/Root'].get_object()
    intent_icc = False
    for oi in root.get('/OutputIntents', []):
        prof = oi.get_object().get('/DestOutputProfile')
        if prof is not None and len(prof.get_object().get_data()) > 128:
            intent_icc = True
    image_icc = all(has_icc(obj.get('/ColorSpace')) for _, _, _, obj in placed) if placed else False
    print(f'  ICC: Output Intent {intent_icc}, Bilder ICCBased {image_icc}')
    if not (intent_icc or image_icc):
        fail('kein ICC-Profil eingebettet')

    content = page.extract_text() or ''
    # Leerraum vereinheitlichen: je nach Schrift-Engine trennt pypdf Wörter mit Tabulatoren
    flat = ' '.join(content.split())
    missing = [t for t in texts if ' '.join(t.split()) not in flat]
    for t in missing:
        fail(f'Text fehlt: {t!r}')
    if missing:
        fonts = page['/Resources'].get_object().get('/Font', {})
        ops = [op for _, op in ContentStream(page.get_contents(), reader).operations]
        print(f'    Diagnose: Schriften {list(fonts.keys())}, Tj/TJ {ops.count(b"Tj") + ops.count(b"TJ")}, '
              f'Pfade f {ops.count(b"f")}, Text {content[:120]!r}')

    if failures:
        print(f'  {len(failures)} Fehler')
        return 1
    print('  K-15 bestanden')
    return 0


if __name__ == '__main__':
    sys.exit(main())
