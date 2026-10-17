#!/usr/bin/env python3
"""Checks the objetismo binary against an independent numpy implementation.

Renders a synthetic image and verifies, for both metrics, that every cell got
a tile at the minimum color distance, that transparent pixels give empty cells,
and that the output PNG is made of the right tiles.

Usage: python3 tests/check_matches.py ./objetismo [--tiles resources]
"""
import argparse
import os
import subprocess
import sys
import tempfile

import numpy as np
from PIL import Image

M = np.array([[0.4124564, 0.3575761, 0.1804375],
              [0.2126729, 0.7151522, 0.0721750],
              [0.0193339, 0.1191920, 0.9503041]])
WHITE = np.array([0.95047, 1.0, 1.08883])


def rgb_to_lab(rgb):
    c = np.asarray(rgb, float) / 255.0
    lin = np.where(c > 0.04045, ((c + 0.055) / 1.055) ** 2.4, c / 12.92)
    t = (lin @ M.T) / WHITE
    f = np.where(t > 216 / 24389, np.cbrt(t), (24389 / 27 * t + 16) / 116)
    return np.stack([116 * f[..., 1] - 16, 500 * (f[..., 0] - f[..., 1]),
                     200 * (f[..., 1] - f[..., 2])], -1)


def cie76(p, q):
    return np.linalg.norm(p - q, axis=-1)


def ciede2000(p, q):
    l1, a1, b1 = np.moveaxis(p, -1, 0)
    l2, a2, b2 = np.moveaxis(q, -1, 0)
    cbar7 = ((np.hypot(a1, b1) + np.hypot(a2, b2)) / 2) ** 7
    g = 0.5 * (1 - np.sqrt(cbar7 / (cbar7 + 25.0 ** 7)))
    a1p, a2p = (1 + g) * a1, (1 + g) * a2
    c1p, c2p = np.hypot(a1p, b1), np.hypot(a2p, b2)
    h1p = np.degrees(np.arctan2(b1, a1p)) % 360
    h2p = np.degrees(np.arctan2(b2, a2p)) % 360
    zero = c1p * c2p == 0
    dh = h2p - h1p
    dh = np.where(dh > 180, dh - 360, np.where(dh < -180, dh + 360, dh))
    dh = np.where(zero, 0, dh)
    dHp = 2 * np.sqrt(c1p * c2p) * np.sin(np.radians(dh / 2))
    hs = h1p + h2p
    hbar = np.where(zero, hs, np.where(np.abs(h1p - h2p) <= 180, hs / 2,
                                       np.where(hs < 360, (hs + 360) / 2, (hs - 360) / 2)))
    t = (1 - 0.17 * np.cos(np.radians(hbar - 30)) + 0.24 * np.cos(np.radians(2 * hbar))
         + 0.32 * np.cos(np.radians(3 * hbar + 6)) - 0.20 * np.cos(np.radians(4 * hbar - 63)))
    cbarp = (c1p + c2p) / 2
    rc = 2 * np.sqrt(cbarp ** 7 / (cbarp ** 7 + 25.0 ** 7))
    rt = -np.sin(np.radians(60 * np.exp(-((hbar - 275) / 25) ** 2))) * rc
    l50 = ((l1 + l2) / 2 - 50) ** 2
    vl = (l2 - l1) / (1 + 0.015 * l50 / np.sqrt(20 + l50))
    vc = (c2p - c1p) / (1 + 0.045 * cbarp)
    vh = dHp / (1 + 0.015 * cbarp * t)
    return np.sqrt(vl ** 2 + vc ** 2 + vh ** 2 + rt * vc * vh)


# Reference pairs from Sharma, Wu & Dalal (2005), "The CIEDE2000 color-difference formula".
SHARMA = [((50, 2.6772, -79.7751), (50, 0, -82.7485), 2.0425),
          ((50, 3.1571, -77.2803), (50, 0, -82.7485), 2.8615),
          ((50, -1.3802, -84.2814), (50, 0, -82.7485), 1.0000),
          ((50, 0, 0), (50, -1, 2), 2.3669),
          ((50, 2.5, 0), (73, 25, -18), 27.1492),
          ((50, 2.5, 0), (61, -5, 29), 22.8977),
          ((50, 2.5, 0), (56, -27, -3), 31.9030),
          ((50, 2.5, 0), (58, 24, 15), 19.4535)]


def load_tiles(d):
    lab = []
    for line in open(os.path.join(d, 'tiles.tsv'), encoding='utf-8'):
        if line.startswith('#'):
            if 'tile_size=' in line:
                header = dict(kv.split('=') for kv in line.split()[3:])
            continue
        lab.append([float(x) for x in line.split('\t')[1:4]])
    return int(header['tile_size']), int(header['cols']), np.array(lab)


def run(binary, args):
    r = subprocess.run([binary] + args, capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit('FAIL: %s %s\n%s' % (binary, ' '.join(args), r.stderr))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('binary')
    ap.add_argument('--tiles', default='resources')
    a = ap.parse_args()
    failures = 0

    for p, q, want in SHARMA:
        got = float(ciede2000(np.array(p, float), np.array(q, float)))
        if abs(got - want) > 1e-4:
            print('FAIL: reference CIEDE2000 %s %s = %.4f, expected %.4f' % (p, q, got, want))
            failures += 1

    size, cols, tile_lab = load_tiles(a.tiles)
    atlas = np.asarray(Image.open(os.path.join(a.tiles, 'tiles.png')).convert('RGB'))

    rng = np.random.default_rng(1)
    w, h = 48, 40
    img = np.dstack([rng.integers(0, 256, (h, w, 3)), np.full((h, w), 255)]).astype(np.uint8)
    img[0, :6, :3] = [[255, 255, 255], [0, 0, 0], [255, 0, 0], [0, 255, 0], [0, 0, 255], [128] * 3]
    img[rng.random((h, w)) < 0.05, 3] = 0  # some transparent pixels
    img[0, :6, 3] = 255

    with tempfile.TemporaryDirectory() as tmp:
        src = os.path.join(tmp, 'in.png')
        Image.fromarray(img, 'RGBA').save(src)
        lab = rgb_to_lab(img[..., :3])
        empty = img[..., 3] == 0

        for metric, dist in (('de2000', ciede2000), ('cie76', cie76)):
            out, idx = os.path.join(tmp, metric + '.png'), os.path.join(tmp, metric + '.tsv')
            run(a.binary, ['-i', src, '-o', out, '-t', a.tiles, '-m', metric, '--dump-indices', idx])
            chosen = np.loadtxt(idx, dtype=int, delimiter='\t').reshape(h, w)
            d = dist(lab[:, :, None, :], tile_lab[None, None, :, :])  # h x w x tiles
            best = d.min(-1)
            got = np.take_along_axis(d, np.maximum(chosen, 0)[..., None], -1)[..., 0]
            bad = (~empty & ((chosen < 0) | (got > best + 1e-6))).sum()
            bad += (empty & (chosen != -1)).sum()
            if bad:
                print('FAIL: %s: %d of %d cells not matched to a closest tile' % (metric, bad, w * h))
                failures += 1

            mosaic = np.asarray(Image.open(out).convert('RGB'))
            if mosaic.shape != (h * size, w * size, 3):
                print('FAIL: %s: output is %s' % (metric, mosaic.shape))
                failures += 1
                continue
            for y, x in [(0, 0), (0, 2), (h - 1, w - 1), (h // 2, w // 3)] + list(zip(*np.nonzero(empty)))[:3]:
                cell = mosaic[y * size:(y + 1) * size, x * size:(x + 1) * size]
                k = chosen[y, x]
                ty, tx = divmod(k, cols)
                want = (np.zeros_like(cell) if k < 0 else
                        atlas[ty * size:(ty + 1) * size, tx * size:(tx + 1) * size])
                if not np.array_equal(cell, want):
                    print('FAIL: %s: cell (%d,%d) does not contain tile %d' % (metric, x, y, k))
                    failures += 1

        idx = os.path.join(tmp, 'w.tsv')
        run(a.binary, ['-i', src, '-o', os.path.join(tmp, 'w.png'), '-t', a.tiles, '-w', '12',
                       '--dump-indices', idx])
        shape = np.loadtxt(idx, dtype=int, delimiter='\t', ndmin=2).shape
        if shape != (10, 12):
            print('FAIL: --width 12 on 48x40 gave %s cells, expected (10, 12)' % (shape,))
            failures += 1

    print('FAIL: %d check(s)' % failures if failures else 'ok')
    sys.exit(1 if failures else 0)


if __name__ == '__main__':
    main()
