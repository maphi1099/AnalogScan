"""Sichtprüfung echter Scans durch die Pipeline (D-14), bis die Adapter (M4) stehen.

Ablauf je Fall aus cases.json:
  1. RAW mit rawpy dekodieren: halbe Auflösung, linear, Weißabgleich der Aufnahme, Ausgabe XYZ,
     umgerechnet in linear Rec. 2020 (Arbeitsfarbraum) -> PFM
  2. Parameter schätzen, die ab M4 die Automatiken liefern: Basis aus dem Basis-Rechteck
     (Median), Dichtegrenzen als 0,1-/99,9-Perzentil im Bildfeld; bei Dias Lichtquelle aus dem
     Perforationsloch oder Perzentile der Transmission
  3. autoneg_render_pfm (C++, echte Pipeline mit CpuBackend) rechnet das Positiv
  4. Ergebnis nach sRGB (PNG) und Übersicht Scan | Positiv

Testwerkzeug, keine Laufzeitabhängigkeit. Testdaten werden nur gelesen.

  python -X utf8 tools/visual_check/visual_check.py [--decoder <autoneg_decode_raw>] <Testdaten> <Ausgabeordner> <render-Befehl...>

Mit --decoder dekodiert der echte Adapter LibRawDecoder (ab M4) statt rawpy; der Weißabgleich
der Aufnahme und die Umrechnung nach Rec. 2020 macht dann der Adapter.

Ein mehrteiliger render-Befehl (z. B. "wsl -d archlinux -- /pfad/autoneg_render_pfm") bekommt
die Dateipfade als WSL-Pfade (/mnt/<laufwerk>/...).
"""
import json
import os
import subprocess
import sys

import numpy as np
import rawpy
from PIL import Image, ImageDraw

XYZ_TO_REC2020 = np.array([[1.7166512, -0.3556708, -0.2533663],
                           [-0.6666844, 1.6164812, 0.0157685],
                           [0.0176399, -0.0427706, 0.9421031]])
REC2020_TO_REC709 = np.array([[1.6605, -0.5876, -0.0728],
                              [-0.1246, 1.1329, -0.0083],
                              [-0.0182, -0.1006, 1.1187]])
LUMINANCE = np.array([0.2627, 0.6780, 0.0593])


def decode(path):
    with rawpy.imread(path) as r:
        # Weißabgleich der Aufnahme vor der Farbmatrix: mit Faktoren 1 wird Blau unter der
        # Orangemaske nach der Matrix großflächig negativ (Befund Sichtprüfung M3, Vorgabe für M4)
        rgb = r.postprocess(half_size=True, gamma=(1, 1), no_auto_bright=True, output_bps=16,
                            use_camera_wb=True, output_color=rawpy.ColorSpace.XYZ)
    xyz = rgb.astype(np.float64) / 65535.0
    return np.clip(xyz @ XYZ_TO_REC2020.T, 0.0, None).astype(np.float32), float((rgb >= 65535).mean())


def decode_with(tool, path, work):
    """Dekodierung mit dem echten Adapter (autoneg_decode_raw, halbe Auflösung)."""
    out = os.path.join(work, 'decoded.pfm')
    r = subprocess.run([tool, path, out, 'half'], capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError(r.stderr.strip())
    img = np.ascontiguousarray(read_pfm(out))
    return img, float((img >= 1.0).mean())


def write_pfm(path, img):
    h, w, _ = img.shape
    with open(path, 'wb') as f:
        f.write(f'PF\n{w} {h}\n-1\n'.encode('ascii'))
        f.write(np.ascontiguousarray(img[::-1].astype('<f4')).tobytes())


def read_pfm(path):
    with open(path, 'rb') as f:
        assert f.readline().strip() == b'PF'
        w, h = map(int, f.readline().split())
        scale = float(f.readline())
        data = np.frombuffer(f.read(), dtype='<f4' if scale < 0 else '>f4')
    return data.reshape(h, w, 3)[::-1]


def region(img, rect, block=1):
    """Pixel im Rechteck; mit block > 1 als Mittel über block x block (unterdrückt Rauschspitzen)."""
    h, w, _ = img.shape
    x, y, rw, rh = rect
    r = img[int(y * h):int((y + rh) * h), int(x * w):int((x + rw) * w)]
    if block > 1:
        bh, bw = r.shape[0] // block, r.shape[1] // block
        r = r[:bh * block, :bw * block].reshape(bh, block, bw, block, 3).mean(axis=(1, 3))
    return r.reshape(-1, 3)


def to_srgb(img):
    v = np.clip(img.astype(np.float64) @ REC2020_TO_REC709.T, 0.0, 1.0)
    v = np.where(v <= 0.0031308, 12.92 * v, 1.055 * np.power(v, 1 / 2.4) - 0.055)
    return Image.fromarray((v * 255 + 0.5).astype(np.uint8))


def fmt(v):
    return ','.join(f'{x:.6g}' for x in v)


def estimate(img, case):
    """Parameter wie von den Automatiken (M4): Einträge mit Herkunft „automatisch“."""
    crop = case['crop']
    inner = [crop[0] + 0.03 * crop[2], crop[1] + 0.03 * crop[3], 0.94 * crop[2], 0.94 * crop[3]]
    frame = region(img, inner, block=8)
    args = [f'crop.rect={fmt(crop)}']
    info = {}
    if case['film'] == 'slide':
        if 'light' in case:
            hi = np.median(region(img, case['light']), axis=0)
            lo = np.zeros(3)
        else:
            lo = np.percentile(frame, 0.1, axis=0)
            hi = np.percentile(frame, 99.9, axis=0)
        args += [f'densityrange.dMin={fmt(lo)}', f'densityrange.dMax={fmt(hi)}']
        info.update(lo=lo, hi=hi)
    else:
        base = np.median(region(img, case['base']), axis=0)
        if case['film'] == 'bw':
            # S/W: Basis nach der Kanalmischung (wie die Messung ab M4 hinter autoneg.bwmix)
            frame = (frame @ LUMINANCE)[:, None].repeat(3, axis=1)
            base = np.full(3, base @ LUMINANCE)
        # Pixel mit einem Kanal <= 0 (Klemmung nach der Farbraumumrechnung) tragen keine Dichte
        frame = frame[(frame > 0).all(axis=1)]
        d = -np.log10(frame / base)
        lo = np.percentile(d, 0.1, axis=0)
        hi = np.percentile(d, 99.9, axis=0)
        args += [f'filmbase.base={fmt(base)}', f'densityrange.dMin={fmt(lo)}', f'densityrange.dMax={fmt(hi)}']
        info.update(base=base, dMin=lo, dMax=hi)
    return args, info


def wsl_path(p):
    p = os.path.abspath(p).replace(os.sep, '/')
    return '/mnt/' + p[0].lower() + p[2:]


def main():
    args = sys.argv[1:]
    decoder = None
    if args and args[0] == '--decoder':
        decoder, args = args[1], args[2:]
    data, out_dir, render = args[0], args[1], args[2:]
    # Programmpfade absolut (Windows findet relative Pfade mit / nicht)
    if decoder:
        decoder = os.path.abspath(decoder)
    if render and os.path.exists(render[0]):
        render[0] = os.path.abspath(render[0])
    os.makedirs(out_dir, exist_ok=True)
    here = os.path.dirname(os.path.abspath(__file__))
    with open(os.path.join(here, 'cases.json'), encoding='utf-8') as f:
        cases = json.load(f)['cases']
    to_path = (lambda p: p) if len(render) == 1 else wsl_path
    tiles = []
    for i, case in enumerate(cases):
        raw_path = os.path.join(data, case['file'])
        img, clipped = decode_with(decoder, raw_path, out_dir) if decoder else decode(raw_path)
        src = os.path.join(out_dir, f'{i:02d}_scan.pfm')
        dst = os.path.join(out_dir, f'{i:02d}_positiv.pfm')
        write_pfm(src, img)
        args, info = estimate(img, case)
        r = subprocess.run(render + [to_path(src), to_path(dst), case['film']] + args,
                           capture_output=True, text=True)
        print(case['file'], case['film'], f'beschnitten {clipped:.4%}',
              {k: np.round(v, 4).tolist() for k, v in info.items()}, r.stdout.strip(), r.stderr.strip())
        if r.returncode != 0:
            continue
        pos = to_srgb(read_pfm(dst))
        pos.save(os.path.join(out_dir, f'{i:02d}_positiv.png'))
        scan = to_srgb(img / max(float(np.percentile(img, 99.5)), 1e-6))
        for im in (scan, pos):
            im.thumbnail((640, 640))
        tile = Image.new('RGB', (1290, 670), 'white')
        tile.paste(scan, (0, 0))
        tile.paste(pos, (650, 0))
        ImageDraw.Draw(tile).text((4, 652), f"{case['file']} ({case['film']})", fill='black')
        tiles.append(tile)
    sheet = Image.new('RGB', (1290, 670 * max(len(tiles), 1)), 'white')
    for i, t in enumerate(tiles):
        sheet.paste(t, (0, 670 * i))
    sheet.save(os.path.join(out_dir, 'uebersicht.png'))


if __name__ == '__main__':
    main()
