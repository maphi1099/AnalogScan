"""K-02 (LF-IMP-06): LibRawDecoder dekodiert linear.

Ohne Graukeil-Aufnahme mit bekannten Belichtungsstufen (T-12) wird die Linearität gegen die
Sensordaten selbst geprüft, die rawpy unabhängig liest: Bei halber Auflösung entspricht jedes
Ausgabepixel genau einem 2 x 2-Quadrat des Sensors. Eine lineare Dekodierung ist dann
out = A · (Sensor − Schwarzwert) mit einer festen 3 x 3-Matrix A (Weißabgleich mal Farbmatrix).
Eine Tonkurve, Helligkeitsautomatik oder ein Offset ließe sich so nicht beschreiben.

Kriterium: 99 % der Bildpunkte im nutzbaren Bereich weichen höchstens 1 % ab.

  python -X utf8 k02_linearity.py <autoneg_decode_raw> <raw-datei> <arbeitsordner>
"""
import os
import subprocess
import sys

import numpy as np
import rawpy


def read_pfm(path):
    with open(path, 'rb') as f:
        assert f.readline().strip() == b'PF'
        w, h = map(int, f.readline().split())
        scale = float(f.readline())
        data = np.frombuffer(f.read(), dtype='<f4' if scale < 0 else '>f4')
    return data.reshape(h, w, 3)[::-1]


def main():
    tool, raw_path, work = sys.argv[1], sys.argv[2], sys.argv[3]
    os.makedirs(work, exist_ok=True)
    out_path = os.path.join(work, 'k02_half.pfm')
    r = subprocess.run([tool, raw_path, out_path, 'half'], capture_output=True, text=True)
    if r.returncode != 0:
        print('Dekodierung fehlgeschlagen:', r.stderr)
        return 1
    out = read_pfm(out_path).astype(np.float64)

    with rawpy.imread(raw_path) as raw:
        sensor = raw.raw_image_visible.astype(np.float64)
        colors = raw.raw_colors_visible
        black = np.array(raw.black_level_per_channel, dtype=np.float64)
        white = float(raw.white_level)
        flip = raw.sizes.flip
    if flip != 0:
        print('Ausrichtung', flip, 'nicht vorgesehen; Datei ohne Drehung wählen')
        return 1

    h, w = out.shape[0], out.shape[1]
    sensor = sensor[:2 * h, :2 * w]
    colors = colors[:2 * h, :2 * w]
    sub = sensor - black[colors]

    def quad(c_index):
        """Mittel der Sensorwerte mit Farbindex c_index je 2 x 2-Quadrat."""
        mask = (colors == c_index).astype(np.float64)
        num = (sub * mask).reshape(h, 2, w, 2).sum(axis=(1, 3))
        den = mask.reshape(h, 2, w, 2).sum(axis=(1, 3))
        return np.where(den > 0, num / np.maximum(den, 1), np.nan)

    red, blue = quad(0), quad(2)
    # LibRaw mittelt bei halber Auflösung beide Grünwerte eines Quadrats (gemessen); die
    # Einzelwerte bleiben als Varianten für Sensoren mit nur einem Grün-Index
    greens = [(quad(1) + quad(3)) / 2, quad(1), quad(3)] if np.any(colors == 3) else [quad(1)]
    best = None
    for green in greens:
        cam = np.stack([red, green, blue], axis=-1).reshape(-1, 3)
        o = out.reshape(-1, 3)
        rng = white - black.max()
        usable = (np.all(cam > 0.02 * rng, axis=1) & np.all(cam < 0.8 * rng, axis=1)
                  & np.all(o < 0.98, axis=1) & np.all(o > 0.002, axis=1) & np.all(np.isfinite(cam), axis=1))
        if usable.sum() < 1000:
            continue
        a, *_ = np.linalg.lstsq(cam[usable], o[usable], rcond=None)
        pred = cam[usable] @ a
        rel = np.abs(o[usable] - pred) / np.maximum(np.abs(o[usable]), 1e-6)
        p99 = float(np.percentile(rel.max(axis=1), 99))
        if best is None or p99 < best[0]:
            best = (p99, int(usable.sum()), a)
    if best is None:
        print('zu wenige nutzbare Bildpunkte')
        return 1
    p99, n, a = best
    print(f'Bildpunkte {n}, 99. Perzentil der relativen Abweichung {p99 * 100:.3f} %')
    print('Matrix A (Sensor -> Ausgabe):\n', np.array2string(a.T, precision=6))
    ok = p99 <= 0.01
    print('Ergebnis:', 'linear (<= 1 %)' if ok else 'NICHT linear')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
