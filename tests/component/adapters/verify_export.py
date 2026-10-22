"""Unabhängige Prüfung der von OiioImageWriter geschriebenen Dateien (K-05).

Aufruf durch oiio_test.cpp:  python -X utf8 verify_export.py <ordner> <breite> <höhe>

Im Ordner liegen rgb.f32 und gray.f32 (Quellwerte, float32) sowie rgb16.tif, rgb8.jpg,
rgb16.png, rgb16.jxl und gray16.tif. Gelesen wird mit tifffile, Pillow und imagecodecs, nicht
mit OpenImageIO. Verlustfreie Formate müssen bitgleich der Quantisierung floor(v * max + 0,5)
entsprechen (in float32 gerechnet); ICC-Profil und Metadaten (Hersteller, Bildbeschreibung mit Filmangabe) müssen
vorhanden sein; Graustufen sind einkanalig.
"""
import io
import os
import sys

import imagecodecs
import numpy as np
import tifffile
from PIL import Image

DESCRIPTION = 'Kodak Portra 400, Rolle 12, Bild 7'
failures = []


def describe_diff(data, src, bits):
    """Diagnose bei Abweichung: Verteilung der Differenz und Vergleich mit anderen Rundungen."""
    m = (1 << bits) - 1
    v = np.clip(src.astype(np.float64), 0.0, 1.0) * m
    d = data.astype(np.int64) - quantize(src, bits).astype(np.int64)
    print(f'        Differenz: min {d.min()} max {d.max()} Anteil ungleich {np.mean(d != 0):.4f}')
    for name, q in (('abschneiden', np.floor(v)), ('gerade runden', np.round(v)),
                    ('double', np.floor(v + 0.5))):
        print(f'        {name}: Anteil ungleich {np.mean(data.astype(np.int64) != q.astype(np.int64)):.4f}')


def check(cond, text):
    print(('  ok    ' if cond else '  FEHLER ') + text)
    if not cond:
        failures.append(text)


def quantize(v, bits):
    """Sollquantisierung des Schreibers: floor(v * max + 0,5) in float32 (die Quelle ist float32;
    in double gerechnet wichen 0,14 % der Werte um eine Stufe ab)."""
    m = np.float32((1 << bits) - 1)
    q = np.floor(np.clip(v, 0.0, 1.0).astype(np.float32) * m + np.float32(0.5))
    return q.astype(np.uint16 if bits == 16 else np.uint8)


def main():
    folder, w, h = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
    rgb = np.fromfile(os.path.join(folder, 'rgb.f32'), dtype='<f4').reshape(h, w, 3)
    gray = np.fromfile(os.path.join(folder, 'gray.f32'), dtype='<f4').reshape(h, w)

    # TIFF 16 bit RGB
    with tifffile.TiffFile(os.path.join(folder, 'rgb16.tif')) as t:
        page = t.pages[0]
        data = page.asarray()
        check(data.dtype == np.uint16 and data.shape == (h, w, 3), f'TIFF 16 bit RGB Form {data.dtype} {data.shape}')
        check(np.array_equal(data, quantize(rgb, 16)), 'TIFF 16 bit bitgleich')
        if not np.array_equal(data, quantize(rgb, 16)):
            describe_diff(data, rgb, 16)
        check(34675 in page.tags, 'TIFF ICC-Profil vorhanden')
        make = page.tags.get(271)
        desc = page.tags.get(270)
        check(make is not None and make.value == 'AutoNeg-Test', f'TIFF Hersteller {make.value if make else None}')
        check(desc is not None and DESCRIPTION in desc.value, 'TIFF Bildbeschreibung mit Filmangabe')

    # Graustufen-TIFF
    with tifffile.TiffFile(os.path.join(folder, 'gray16.tif')) as t:
        data = t.pages[0].asarray()
        check(data.ndim == 2 and data.shape == (h, w), f'Graustufen-TIFF einkanalig {data.shape}')
        check(np.array_equal(data, quantize(gray, 16)), 'Graustufen-TIFF bitgleich')
        check(34675 in t.pages[0].tags, 'Graustufen-TIFF ICC-Profil vorhanden')

    # PNG 16 bit
    with open(os.path.join(folder, 'rgb16.png'), 'rb') as f:
        raw = f.read()
    data = imagecodecs.png_decode(raw)
    check(data.dtype == np.uint16 and data.shape == (h, w, 3), f'PNG 16 bit Form {data.dtype} {data.shape}')
    check(np.array_equal(data, quantize(rgb, 16)), 'PNG 16 bit bitgleich')
    png = Image.open(io.BytesIO(raw))
    check('icc_profile' in png.info, 'PNG ICC-Profil vorhanden')
    text = ' '.join(str(v) for v in png.info.values() if isinstance(v, str))
    exif = png.getexif()
    check(DESCRIPTION in text or DESCRIPTION in str(exif.get(270, '')), 'PNG Filmangabe vorhanden')

    # JPEG 8 bit (verlustbehaftet, Qualität 95)
    jpg = Image.open(os.path.join(folder, 'rgb8.jpg'))
    data = np.asarray(jpg)
    check(data.shape == (h, w, 3), f'JPEG Form {data.shape}')
    err = np.abs(data.astype(np.float64) - quantize(rgb, 8).astype(np.float64)).mean()
    check(err < 1.5, f'JPEG mittlere Abweichung {err:.3f} Stufen < 1,5')
    check('icc_profile' in jpg.info, 'JPEG ICC-Profil vorhanden')
    exif = jpg.getexif()
    check(exif.get(271) == 'AutoNeg-Test', f'JPEG EXIF Hersteller {exif.get(271)}')
    check(DESCRIPTION in str(exif.get(270, '')), 'JPEG EXIF Bildbeschreibung mit Filmangabe')

    # JPEG XL verlustfrei 16 bit
    with open(os.path.join(folder, 'rgb16.jxl'), 'rb') as f:
        data = imagecodecs.jpegxl_decode(f.read())
    check(data.shape == (h, w, 3), f'JPEG XL Form {data.dtype} {data.shape}')
    check(data.dtype == np.uint16 and np.array_equal(data, quantize(rgb, 16)), 'JPEG XL verlustfrei bitgleich')

    print('Ergebnis:', 'FEHLER' if failures else 'alles in Ordnung')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
