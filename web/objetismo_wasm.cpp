// WebAssembly entry points for the browser front end (web/worker.js).
//
// PNGs are decoded with libpng to preserve straight RGBA, just like the CLI.
// The caller decodes other formats and draws the mosaic itself; this turns
// RGBA pixels into cell colors and cell colors into tile indices, with the
// same code as the CLI. Splitting the two lets the page match the cells on
// several workers at once, the way the CLI uses OpenMP.

#include <emscripten/emscripten.h>
#include <png.h>

#include <cstdlib>
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

// Returns an error string or nullptr. On success, result holds an allocated
// RGBA pointer, width, and height; the caller owns and frees the pixel buffer.
EMSCRIPTEN_KEEPALIVE const char* objetismo_decode_png(const uint8_t* data, size_t size,
                                                     uint32_t* result) {
    png_image png{};
    png.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_memory(&png, data, size)) {
        error = png.message;
        png_image_free(&png);
        return error.c_str();
    }
    // Match the web input budget before allocating the decoded pixels.
    if ((uint64_t)png.width * png.height > 50000000) {
        error = "image too large (" + std::to_string(png.width) + " x " +
                std::to_string(png.height) + ")";
        png_image_free(&png);
        return error.c_str();
    }
    png.format = PNG_FORMAT_RGBA;
    auto* rgba = static_cast<uint8_t*>(std::malloc(PNG_IMAGE_SIZE(png)));
    if (!rgba) {
        png_image_free(&png);
        error = "not enough memory to decode the image";
        return error.c_str();
    }
    if (!png_image_finish_read(&png, nullptr, rgba, 0, nullptr)) {
        error = png.message;
        std::free(rgba);
        png_image_free(&png);
        return error.c_str();
    }
    result[0] = (uint32_t)(uintptr_t)rgba;
    result[1] = png.width;
    result[2] = png.height;
    png_image_free(&png);
    return nullptr;
}

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
