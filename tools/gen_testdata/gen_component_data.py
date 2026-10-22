"""Erzeugt die kleinen Testdateien der Komponententests (K-03, K-04, K-07) unabhängig von
OpenImageIO und Little CMS: TIFF/PNG mit tifffile/imagecodecs, JPEG mit Pillow, ICC-Profil
von Hand nach ICC.1:2001 (Version 2).

Die Dateien sind eingecheckt (tests/component/data); das Skript dokumentiert, wie sie
entstanden sind, und kann sie neu erzeugen:

  python -X utf8 tools/gen_testdata/gen_component_data.py tests/component/data

Pixelwerte (64 x 48, Kanal c):  8 bit: (x * 7 + y * 13 + c * 50) % 256
                               16 bit: (x * 771 + y * 1031 + c * 20000) % 65536
Die Formeln stehen gleichlautend in tests/component/adapters/test_files.hpp.
"""
import io
import os
import struct
import sys

import imagecodecs
import numpy as np
import tifffile
from PIL import Image, ImageCms

W, H = 64, 48


def pattern8(channels):
    y, x = np.mgrid[0:H, 0:W]
    return np.stack([(x * 7 + y * 13 + c * 50) % 256 for c in range(channels)], axis=-1).astype(np.uint8)


def pattern16(channels):
    y, x = np.mgrid[0:H, 0:W]
    return np.stack([(x * 771 + y * 1031 + c * 20000) % 65536 for c in range(channels)], axis=-1).astype(np.uint16)


# --- ICC-Profil von Hand (Version 2, Matrix/TRC) -------------------------------------------

def s15f16(v):
    return struct.pack('>i', int(round(v * 65536)))


def xyz_tag(xyz):
    return b'XYZ ' + b'\0' * 4 + b''.join(s15f16(v) for v in xyz)


def curve_tag(gamma):
    return b'curv' + b'\0' * 4 + struct.pack('>I', 1) + struct.pack('>H', int(round(gamma * 256)))


def desc_tag(text):
    a = text.encode('ascii') + b'\0'
    return (b'desc' + b'\0' * 4 + struct.pack('>I', len(a)) + a + struct.pack('>II', 0, 0)
            + struct.pack('>HB', 0, 0) + b'\0' * 67)


def text_tag(text):
    return b'text' + b'\0' * 4 + text.encode('ascii') + b'\0'


def bradford(src_white, dst_white):
    m = np.array([[0.8951, 0.2664, -0.1614], [-0.7502, 1.7135, 0.0367], [0.0389, -0.0685, 1.0296]])
    s = m @ src_white
    d = m @ dst_white
    return np.linalg.inv(m) @ np.diag(d / s) @ m


def xy_to_xyz(x, y):
    return np.array([x / y, 1.0, (1 - x - y) / y])


def rgb_to_xyz(primaries, white):
    p = np.stack([xy_to_xyz(*xy) for xy in primaries], axis=1)
    w = xy_to_xyz(*white)
    return p * np.linalg.solve(p, w)


def matrix_profile(name, primaries, white, gamma):
    d50 = np.array([0.9642, 1.0, 0.8249])
    m = bradford(xy_to_xyz(*white), d50) @ rgb_to_xyz(primaries, white)
    tags = [(b'desc', desc_tag(name)), (b'cprt', text_tag('Testprofil AutoNeg, gemeinfrei')),
            (b'wtpt', xyz_tag(d50)), (b'rXYZ', xyz_tag(m[:, 0])), (b'gXYZ', xyz_tag(m[:, 1])),
            (b'bXYZ', xyz_tag(m[:, 2])), (b'rTRC', curve_tag(gamma)), (b'gTRC', curve_tag(gamma)),
            (b'bTRC', curve_tag(gamma))]
    offset = 128 + 4 + 12 * len(tags)
    table = struct.pack('>I', len(tags))
    data = b''
    for sig, body in tags:
        while (offset + len(data)) % 4:
            data += b'\0'
        table += sig + struct.pack('>II', offset + len(data), len(body))
        data += body
    while len(data) % 4:
        data += b'\0'
    size = offset + len(data)
    header = (struct.pack('>I', size) + b'\0' * 4 + struct.pack('>I', 0x02100000) + b'mntrRGB XYZ '
              + struct.pack('>6H', 2026, 10, 22, 0, 0, 0) + b'acsp' + b'\0' * 4 + struct.pack('>I', 0)
              + b'\0' * 8 + b'\0' * 8 + struct.pack('>I', 0) + b''.join(s15f16(v) for v in d50)
              + b'\0' * 4 + b'\0' * 16 + b'\0' * 28)
    assert len(header) == 128
    return header + table + data


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    srgb = ImageCms.ImageCmsProfile(ImageCms.createProfile('sRGB')).tobytes()

    tifffile.imwrite(os.path.join(out, 'rgb8_srgb.tif'), pattern8(3), photometric='rgb', iccprofile=srgb)
    tifffile.imwrite(os.path.join(out, 'rgb16.tif'), pattern16(3), photometric='rgb')
    tifffile.imwrite(os.path.join(out, 'gray16.tif'), pattern16(1)[:, :, 0], photometric='minisblack')
    with open(os.path.join(out, 'rgb16.png'), 'wb') as f:
        f.write(imagecodecs.png_encode(pattern16(3)))
    Image.fromarray(pattern8(3)).save(os.path.join(out, 'rgb8_icc.jpg'), quality=100, subsampling=0,
                                      icc_profile=srgb)
    Image.fromarray(pattern8(3)).save(os.path.join(out, 'rgb8.jpg'), quality=100, subsampling=0)

    # defekte Dateien (K-03)
    with open(os.path.join(out, 'rgb16.tif'), 'rb') as f:
        whole = f.read()
    with open(os.path.join(out, 'truncated.tif'), 'wb') as f:
        f.write(whole[:len(whole) // 3])
    with open(os.path.join(out, 'not_an_image.tif'), 'wb') as f:
        f.write(b'Dies ist kein Bild, nur Text mit falscher Endung.\n' * 20)
    with open(os.path.join(out, 'not_a_raw.ARW'), 'wb') as f:
        f.write(b'Dies ist keine RAW-Datei.\n' * 40)

    # Monitorprofil (K-07): Display P3, D65, Gamma 2,2
    p3 = matrix_profile('Test-Monitor Display P3 Gamma 2.2', [(0.680, 0.320), (0.265, 0.690), (0.150, 0.060)],
                        (0.3127, 0.3290), 2.2)
    with open(os.path.join(out, 'monitor_p3_g22.icc'), 'wb') as f:
        f.write(p3)
    # Selbstprüfung des Profils mit Pillow (Little CMS in Pillow, unabhängig vom Adapter)
    ImageCms.ImageCmsProfile(io.BytesIO(p3))
    print('geschrieben nach', out)


if __name__ == '__main__':
    main()
