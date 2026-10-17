#!/usr/bin/env python3
"""One small native/WASM comparison, including a transparent cell."""
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

    width, height, cols = 12, 8, 6
    img = np.random.default_rng(2).integers(0, 256, (height, width, 4), dtype=np.uint8)
    img[:, :, 3] = 255
    img[:2, :2, 3] = 0

    with tempfile.TemporaryDirectory() as tmp:
        src, raw, idx = [os.path.join(tmp, f) for f in ('in.png', 'in.rgba', 'native.tsv')]
        Image.fromarray(img).save(src)
        img.tofile(raw)
        run([a.binary, '-i', src, '-o', os.path.join(tmp, 'out.png'), '-t', a.tiles,
             '-w', str(cols), '--dump-indices', idx])
        native = np.loadtxt(idx, dtype=int, delimiter='\t', ndmin=2)
        out = run(['node', os.path.join(HERE, 'run_wasm.mjs'), a.module,
                   os.path.join(a.tiles, 'tiles.tsv'), raw,
                   str(width), str(height), str(cols), 'de2000'])
        wasm = np.array([[int(v) for v in line.split('\t')] for line in out.splitlines()])
        np.testing.assert_array_equal(wasm, native)
    print('ok')


if __name__ == '__main__':
    main()
