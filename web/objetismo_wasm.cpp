// WebAssembly entry points for the browser front end (web/worker.js).
//
// The caller decodes the image and draws the mosaic itself; this only turns
// RGBA pixels into cell colors and cell colors into tile indices, with the
// same code as the CLI. Splitting the two lets the page match the cells on
// several workers at once, the way the CLI uses OpenMP.

#include <emscripten/emscripten.h>

#include <sstream>

#include "../objetismo.h"

using namespace objetismo;

namespace {

TileSet tiles;
std::string error;
std::vector<uint32_t> cells;
std::vector<int> indices;

}  // namespace

extern "C" {

// Parses the contents of tiles.tsv. Returns nullptr on success, or an error.
EMSCRIPTEN_KEEPALIVE const char* objetismo_load_tiles(const char* tsv) {
    std::istringstream in(tsv);
    tiles = TileSet();
    error = parse_tiles(in, &tiles);
    return error.empty() ? nullptr : error.c_str();
}

EMSCRIPTEN_KEEPALIVE int objetismo_rows(int width, int height, int cols) {
    return mosaic_rows(width, height, cols);
}

// Averages a width x height RGBA image into cols x rows packed 0xRRGGBB cell
// colors (0xFFFFFFFF for empty cells), valid until the next call.
EMSCRIPTEN_KEEPALIVE const uint32_t* objetismo_cells(const uint8_t* rgba, int width, int height,
                                                     int cols, int rows) {
    cells = cell_colors(rgba, width, height, cols, rows);
    return cells.data();
}

// Matches n cell colors to tile indices (-1 for empty cells), valid until the
// next call.
EMSCRIPTEN_KEEPALIVE const int* objetismo_match_cells(const uint32_t* colors, int n,
                                                      int use_de2000) {
    size_t distinct;
    indices = match(std::vector<uint32_t>(colors, colors + n), tiles, use_de2000, &distinct);
    return indices.data();
}

}  // extern "C"
