#!/usr/bin/env python3
"""Build the tile set used by objetismo from Argentum Online game data.

Reads obj.dat (objects), Graficos3.ind (sprite index) and the sprite PNGs, cuts
each object's 32x32 cell, drops duplicates and writes:

  <out>/tiles.png  atlas of 32x32 RGB tiles, COLS per row, black background
  <out>/tiles.tsv  one row per tile: index, CIELAB of the tile as rendered on
                   black, coverage, and the objects that use it

Sprites use pure black (0,0,0) as the transparent color key.

Usage: python3 tools/build_tiles.py [--sprites DIR_OR_TAR] [--out DIR] ...
"""
import argparse
import os
import struct
import sys
import tarfile
from collections import Counter

import numpy as np
from PIL import Image

T = 32
COLS = 16

# sRGB (D65) -> XYZ, and the D65 white point
SRGB_TO_XYZ = np.array([[0.4124564, 0.3575761, 0.1804375],
                        [0.2126729, 0.7151522, 0.0721750],
                        [0.0193339, 0.1191920, 0.9503041]])
WHITE = np.array([0.95047, 1.0, 1.08883])


def srgb_to_linear(c):
    c = np.asarray(c, float) / 255.0
    return np.where(c > 0.04045, ((c + 0.055) / 1.055) ** 2.4, c / 12.92)


def linear_to_lab(rgb):
    t = (SRGB_TO_XYZ @ rgb) / WHITE
    f = np.where(t > 216 / 24389, np.cbrt(t), (24389 / 27 * t + 16) / 116)
    return np.array([116 * f[1] - 16, 500 * (f[0] - f[1]), 200 * (f[1] - f[2])])


def parse_ind(path):
    """Graficos3.ind: int32 version, int32 max id, then records until EOF:
    int32 id, int16 frames; if frames > 1: int32 frame ids[frames], float32 speed,
    else int32 file, int16 sx, sy, width, height. Ids have gaps."""
    data = open(path, 'rb').read()
    off, grhs = 8, {}
    while off < len(data):
        gid, frames = struct.unpack_from('<ih', data, off)
        off += 6
        if frames > 1:
            grhs[gid] = None  # animation
            off += 4 * frames + 4
        else:
            grhs[gid] = struct.unpack_from('<ihhhh', data, off)
            off += 12
    if off != len(data):
        sys.exit('error: %s: trailing bytes, unexpected format' % path)
    return grhs


def parse_obj_dat(path):
    """Returns {object id: {key: value}}. Section headers may carry a trailing
    '; comment', which configparser would reject."""
    objs, cur = {}, None
    for line in open(path, encoding='latin-1'):
        line = line.strip()
        if line.startswith('['):
            name = line[1:line.find(']')].upper()
            cur = int(name[3:]) if name.startswith('OBJ') and name[3:].isdigit() else None
            if cur is not None:
                objs[cur] = {}
        elif '=' in line and cur is not None:
            key, value = (x.strip() for x in line.split('=', 1))
            objs[cur].setdefault(key.lower(), value)
    return objs


def load_sprites(source, wanted):
    """Loads <file>.png for each wanted file number from a directory or a tarball."""
    names = {'%d.png' % n: n for n in wanted}
    sprites = {}
    if os.path.isdir(source):
        for name, n in names.items():
            path = os.path.join(source, name)
            if os.path.exists(path):
                sprites[n] = np.asarray(Image.open(path).convert('RGB'))
    else:
        with tarfile.open(source) as tar:
            for member in tar:
                n = names.get(os.path.basename(member.name))
                if n is not None and member.isfile():
                    sprites[n] = np.asarray(Image.open(tar.extractfile(member)).convert('RGB'))
    return sprites


def main():
    ap = argparse.ArgumentParser(description='Build the objetismo tile set.')
    ap.add_argument('--obj-dat', default='resources/obj.dat')
    ap.add_argument('--ind', default='resources/Graficos3.ind')
    ap.add_argument('--sprites', default='resources/graficos/graficos.tar.bz2',
                    help='directory of <file>.png sprites, or a tarball of them')
    ap.add_argument('--out', default='resources')
    a = ap.parse_args()

    grhs = parse_ind(a.ind)
    objs = parse_obj_dat(a.obj_dat)
    skipped = Counter()

    cells = []  # (object id, name, file, sx, sy)
    for oid in sorted(objs):
        grh = objs[oid].get('grhindex', '')
        grh = int(grh) if grh.lstrip('-').isdigit() else 0
        g = grhs.get(grh)
        if grh <= 0 or grh not in grhs:
            skipped['no graphic'] += 1
        elif g is None:
            skipped['animation'] += 1
        elif g[3:] != (T, T):
            skipped['not 32x32'] += 1
        else:
            cells.append((oid, objs[oid].get('name', ''), g[0], g[1], g[2]))

    sprites = load_sprites(a.sprites, {c[2] for c in cells})

    tiles, by_pixels = [], {}
    for oid, name, fn, sx, sy in cells:
        img = sprites.get(fn)
        if img is None:
            skipped['sprite missing'] += 1
            continue
        if sy + T > img.shape[0] or sx + T > img.shape[1]:
            skipped['cell outside sprite'] += 1
            continue
        px = np.ascontiguousarray(img[sy:sy + T, sx:sx + T])
        key = px.tobytes()
        if key not in by_pixels:
            by_pixels[key] = len(tiles)
            opaque = px.any(-1)
            tiles.append({'px': px, 'coverage': opaque.mean(), 'objects': [],
                          # mean in linear light of the tile as drawn on black
                          'lab': linear_to_lab(srgb_to_linear(px.reshape(-1, 3)).mean(0))})
        tiles[by_pixels[key]]['objects'].append((oid, name))

    os.makedirs(a.out, exist_ok=True)
    rows = (len(tiles) + COLS - 1) // COLS
    atlas = np.zeros((rows * T, COLS * T, 3), np.uint8)
    for i, t in enumerate(tiles):
        r, c = divmod(i, COLS)
        atlas[r * T:(r + 1) * T, c * T:(c + 1) * T] = t['px']
    Image.fromarray(atlas, 'RGB').save(os.path.join(a.out, 'tiles.png'), optimize=True)

    with open(os.path.join(a.out, 'tiles.tsv'), 'w', encoding='utf-8', newline='\n') as f:
        f.write('# objetismo tiles: tile_size=%d cols=%d count=%d\n' % (T, COLS, len(tiles)))
        f.write('# index\tL\ta\tb\tcoverage\tobject_ids\tobject_names\n')
        for i, t in enumerate(tiles):
            ids = ','.join(str(o) for o, _ in t['objects'])
            names = ' | '.join(n for _, n in t['objects'])
            f.write('%d\t%.4f\t%.4f\t%.4f\t%.4f\t%s\t%s\n'
                    % (i, *t['lab'], t['coverage'], ids, names))

    used = sum(len(t['objects']) for t in tiles)
    print('%d tiles from %d of %d objects; skipped: %s'
          % (len(tiles), used, len(objs), ', '.join('%s %d' % kv for kv in sorted(skipped.items()))))


if __name__ == '__main__':
    main()
