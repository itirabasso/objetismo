#!/usr/bin/env python3
"""Native/WASM matching and PNG decoding parity, including partial alpha."""
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

        # Expose RGB rounding when partial-alpha PNGs pass through a canvas.
        # Compare direct PNG decoding for both metrics and averaged cells.
        width, height = 64, 8
        rng = np.random.default_rng(42)
        pixels = rng.integers(0, 256, (height, width, 4), dtype=np.uint8)
        pixels[:, :, 3] = rng.choice([0, 64, 127, 128, 129, 180, 254, 255], (height, width))
        rgba = Image.fromarray(pixels)
        palette = Image.fromarray(pixels[:, :, 0], 'P')
        palette.putpalette(rng.integers(0, 256, 768, dtype=np.uint8).tolist())
        palette.info['transparency'] = bytes(range(256))
        for image in (rgba, rgba.convert('LA'), palette):
            image.save(src)
            for cols, metric in ((64, 'de2000'), (23, 'de2000'), (64, 'cie76')):
                run([a.binary, '-i', src, '-o', os.path.join(tmp, 'out.png'),
                     '-t', a.tiles, '-w', str(cols), '-m', metric, '--dump-indices', idx])
                native = np.loadtxt(idx, dtype=int, delimiter='\t', ndmin=2)
                out = run(['node', os.path.join(HERE, 'run_wasm.mjs'), a.module,
                           os.path.join(a.tiles, 'tiles.tsv'), src,
                           str(width), str(height), str(cols), metric, 'png'])
                wasm = np.array([[int(v) for v in line.split('\t')] for line in out.splitlines()])
                np.testing.assert_array_equal(wasm, native)
    print('ok')


if __name__ == '__main__':
    main()
