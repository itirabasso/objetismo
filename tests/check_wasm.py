#!/usr/bin/env python3
"""Checks that the WebAssembly build picks the same tiles as the native binary.

Runs both on a synthetic image (with transparent pixels) and on test.png, at
full size and downscaled, with both metrics, and compares every cell.

Usage: python3 tests/check_wasm.py ./objetismo web/objetismo.js [--tiles resources]
"""
import argparse
import os
import subprocess
import sys
import tempfile

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))


def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit('FAIL: %s\n%s' % (' '.join(cmd), r.stderr))
    return r.stdout


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('binary')
    ap.add_argument('module')
    ap.add_argument('--tiles', default='resources')
    a = ap.parse_args()
    tsv = os.path.join(a.tiles, 'tiles.tsv')
    failures = 0

    rng = np.random.default_rng(2)
    w, h = 48, 40
    synthetic = np.dstack([rng.integers(0, 256, (h, w, 3)), np.full((h, w), 255)]).astype(np.uint8)
    synthetic[rng.random((h, w)) < 0.05, 3] = 0
    photo = np.asarray(Image.open(os.path.join(HERE, '..', 'test.png')).convert('RGBA'))

    with tempfile.TemporaryDirectory() as tmp:
        for name, img in (('synthetic', synthetic), ('test.png', photo)):
            ih, iw = img.shape[:2]
            src, raw = os.path.join(tmp, 'in.png'), os.path.join(tmp, 'in.rgba')
            Image.fromarray(img, 'RGBA').save(src)
            img.tofile(raw)
            for cols in (iw, 37, 12):
                for metric in ('de2000', 'cie76'):
                    idx = os.path.join(tmp, 'native.tsv')
                    run([a.binary, '-i', src, '-o', os.path.join(tmp, 'out.png'), '-t', a.tiles,
                         '-w', str(cols), '-m', metric, '--dump-indices', idx])
                    native = np.loadtxt(idx, dtype=int, delimiter='\t', ndmin=2)
                    out = run(['node', os.path.join(HERE, 'run_wasm.mjs'), a.module, tsv, raw,
                               str(iw), str(ih), str(cols), metric])
                    wasm = np.array([[int(v) for v in line.split('\t')]
                                     for line in out.splitlines()])
                    if wasm.shape != native.shape:
                        print('FAIL: %s cols=%d %s: wasm gave %s cells, native %s'
                              % (name, cols, metric, wasm.shape, native.shape))
                        failures += 1
                    elif (wasm != native).any():
                        print('FAIL: %s cols=%d %s: %d of %d cells differ'
                              % (name, cols, metric, (wasm != native).sum(), wasm.size))
                        failures += 1

    print('FAIL: %d check(s)' % failures if failures else 'ok')
    sys.exit(1 if failures else 0)


if __name__ == '__main__':
    main()
