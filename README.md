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

The CLI checks output dimensions before decoding the full input or matching.
PNG output is limited to 1,000,000 pixels per side (31,250 tiles at 32 px),
or the installed libpng's limit if lower. The PNG is written to a temporary
file beside the destination and renamed into place only after a successful
write and close, preserving an existing output if encoding fails.

## Web version

`web/` is the same tool as a static page: the matching code in `objetismo.h`
is compiled to WebAssembly and runs in the visitor's browser, split across a
pool of Web Workers (the browser's stand-in for OpenMP), and the mosaic is
drawn on a canvas from `tiles.png`. Nothing is uploaded, so the site needs no
server and deploys to Vercel as plain files (see `vercel.json`).

PNG inputs are decoded with libpng in WebAssembly, preserving semi-transparent
pixel colors for the same exact matching as the CLI. Other image formats use
the browser's decoder. Live slider previews remain approximate.

    make serve       # copies the tile set next to the page, serves http://localhost:8000
    make wasm        # rebuilds web/objetismo.{js,wasm} after core or decoder changes
    make test-wasm   # checks the WebAssembly build picks the same tiles as ./objetismo

`make wasm` uses `emcc` if it is installed and the `emscripten/emsdk` Docker
image otherwise. Its output is committed, so serving and deploying need no
WASM toolchain. `make test-wasm` needs Node.js and checks matching plus PNG
decoding, including RGBA, grayscale-alpha and palette transparency.
Nothing rebuilds the WebAssembly automatically: run `make wasm` after editing
`objetismo.h` or `web/objetismo_wasm.cpp`, or the page (and `make test-wasm`)
keep using the old build.

Dragging the width slider shows approximate previews; releasing it runs the
exact match. Color matches are cached across updates.

Browsers limit a canvas to about 16.7 million pixels (Safari), so large
mosaics are previewed with smaller tiles. PNG downloads keep the selected tile
size and are encoded a row of tiles at a time, beyond the canvas limit.

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

    make test   # independent matching checks, output limits and safe replacement
    make asan   # builds objetismo-asan with address and UB sanitizers
