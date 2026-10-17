objetismo
=========

Rebuilds an image as a mosaic of [Argentum Online](https://www.argentumonline.com.ar/)
item sprites. Every pixel of the input (or every block of pixels, with `--width`)
becomes the 32x32 item whose color, as it looks on the black background, is
closest in CIELAB.

## Build

Needs a C++17 compiler, libpng and pkg-config (OpenMP is used if available).

    make

## Usage

    ./objetismo -i photo.png -o mosaic.png --width 80

| Option | |
|---|---|
| `-i, --input FILE` | image to rebuild (PNG, any color type; transparent pixels give empty cells) |
| `-o, --output FILE` | mosaic to write (PNG) |
| `-w, --width N` | mosaic width in tiles; the input is averaged down to N columns first. Without it, each input pixel becomes a tile, so a 200x300 image gives a 6400x9600 mosaic |
| `-t, --tiles DIR` | directory with `tiles.png` and `tiles.tsv` (default `resources`) |
| `-m, --metric NAME` | color difference: `de2000` (CIEDE2000, default) or `cie76` |
| `--dump-indices FILE` | also write the tile index chosen for each cell, as TSV |

The tile set is dark (the brightest tile has a lightness of about 68 out of
100), so bright images come out dimmer than the original.

## Tile set

`resources/tiles.png` holds the 477 distinct 32x32 item tiles, 16 per row, and
`resources/tiles.tsv` their CIELAB color, coverage and the objects that use
each one. Both are generated from the game data and committed, so building and
running needs nothing else.

To regenerate them (Python 3 with numpy and Pillow, see `tools/requirements.txt`):

    make tiles

This reads `resources/obj.dat` (objects), `resources/Graficos3.ind` (sprite
index) and the sprites in `resources/graficos/graficos.tar.bz2`; pass
`--sprites DIR` to `tools/build_tiles.py` to use a directory of sprites
instead. Sprites use pure black as the transparent color; a tile's color is
the average, in linear light, of the whole 32x32 cell drawn on black.

## Tests

    make test   # checks every match against an independent numpy implementation
    make asan   # builds objetismo-asan with address and UB sanitizers
