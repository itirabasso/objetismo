// objetismo: rebuilds an image as a mosaic of Argentum Online item sprites.
//
// Each input pixel (or each block of pixels, with --width) becomes the 32x32
// tile whose color, as it looks on the black background, is closest in CIELAB.
// The tile set (tiles.png + tiles.tsv) is built by tools/build_tiles.py, and
// the matching itself lives in objetismo.h.

#include <png.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <unistd.h>

#include "objetismo.h"

namespace {

using namespace objetismo;

[[noreturn]] void die(const std::string& msg) {
    std::cerr << "objetismo: " << msg << std::endl;
    std::exit(1);
}

struct Image {
    int width = 0, height = 0;
    std::vector<uint8_t> px;  // RGB or RGBA, row-major
};

struct Tiles {
    TileSet set;
    Image atlas;  // RGB
};

// ---- files ----------------------------------------------------------------

Image read_png(const std::string& path, png_uint_32 format,
               const std::function<void(int, int)>& check_size = {}) {
    png_image im;
    std::memset(&im, 0, sizeof im);
    im.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_file(&im, path.c_str()))
        die(path + ": " + im.message);
    if (check_size) check_size((int)im.width, (int)im.height);
    im.format = format;
    Image out;
    out.width = (int)im.width;
    out.height = (int)im.height;
    out.px.resize(PNG_IMAGE_SIZE(im));
    if (!png_image_finish_read(&im, nullptr, out.px.data(), 0, nullptr))
        die(path + ": " + im.message);
    return out;
}

Tiles load_tiles(const std::string& dir) {
    std::string tsv = dir + "/tiles.tsv";
    std::ifstream in(tsv);
    if (!in) die("cannot open " + tsv + " (run tools/build_tiles.py, or use --tiles DIR)");

    Tiles tiles;
    std::string err = parse_tiles(in, &tiles.set);
    if (!err.empty()) die(tsv + ": " + err);

    tiles.atlas = read_png(dir + "/tiles.png", PNG_FORMAT_RGB);
    const TileSet& s = tiles.set;
    int rows = ((int)s.lab.size() + s.cols - 1) / s.cols;
    if (tiles.atlas.width != s.cols * s.size || tiles.atlas.height < rows * s.size)
        die(dir + "/tiles.png: size does not match tiles.tsv");
    return tiles;
}

// ---- output ---------------------------------------------------------------

// Validate the grid before decoding pixels, allocating cells, or matching.
// Keep the aspect-ratio calculation wide until the pixel limits are checked.
int checked_rows(int width, int height, int cols, int tile_size) {
    const int64_t rows = std::max<int64_t>(1, ((int64_t)height * cols + width / 2) / width);
    const int64_t max_width = std::min<int64_t>(1000000, PNG_USER_WIDTH_MAX);
    const int64_t max_height = std::min<int64_t>(1000000, PNG_USER_HEIGHT_MAX);
    if (cols > max_width / tile_size || rows > max_height / tile_size)
        die("output too large (" + std::to_string(cols) + " x " + std::to_string(rows) +
            " tiles); PNG limit is " + std::to_string(max_width) + " x " +
            std::to_string(max_height) + " pixels, use --width to reduce it");
    return (int)rows;
}

// A sibling temporary file keeps the destination intact until encoding and
// closing have succeeded. Explicit cleanup is needed because die() exits.
struct OutputFile {
    std::string path, temporary;
    FILE* fp = nullptr;

    explicit OutputFile(const std::string& destination) : path(destination) {
        std::string pattern = path + ".tmp.XXXXXX";
        std::vector<char> name(pattern.begin(), pattern.end());
        name.push_back('\0');
        int fd = mkstemp(name.data());
        if (fd < 0) die("cannot write " + path + ": " + std::strerror(errno));
        temporary = name.data();
        fp = fdopen(fd, "wb");
        if (!fp) {
            const std::string reason = std::strerror(errno);
            close(fd);
            fail(reason);
        }
    }

    [[noreturn]] void fail(const std::string& reason) {
        if (fp) std::fclose(fp);
        std::remove(temporary.c_str());
        die("cannot write " + path + ": " + reason);
    }

    void commit() {
        const int result = std::fclose(fp);
        fp = nullptr;
        if (result != 0) fail(std::strerror(errno));
        if (std::rename(temporary.c_str(), path.c_str()) != 0)
            fail(std::strerror(errno));
    }
};

// Writes the mosaic one row of tiles at a time, so the full output image is
// never held in memory.
void write_mosaic(const std::string& path, const std::vector<int>& map, int cols, int rows,
                  const Tiles& tiles) {
    const int t = tiles.set.size;
    const size_t width = (size_t)cols * t, height = (size_t)rows * t;
    if (width > PNG_UINT_31_MAX || height > PNG_UINT_31_MAX)
        die("output too large, use --width to reduce it");
    std::vector<uint8_t> band(width * 3 * t);

    OutputFile output(path);
    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    png_infop info = png ? png_create_info_struct(png) : nullptr;
    if (!info) {
        if (png) png_destroy_write_struct(&png, nullptr);
        output.fail("out of memory");
    }
    if (setjmp(png_jmpbuf(png))) {
        png_destroy_write_struct(&png, &info);
        output.fail("PNG encoding failed");
    }
    png_init_io(png, output.fp);
    png_set_IHDR(png, info, (png_uint_32)width, (png_uint_32)height, 8, PNG_COLOR_TYPE_RGB,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);

    const size_t tile_row = (size_t)t * 3;
    for (int cy = 0; cy < rows; cy++) {
        std::fill(band.begin(), band.end(), 0);
        for (int cx = 0; cx < cols; cx++) {
            int k = map[(size_t)cy * cols + cx];
            if (k < 0) continue;
            size_t ax = (size_t)(k % tiles.set.cols) * t, ay = (size_t)(k / tiles.set.cols) * t;
            for (int y = 0; y < t; y++)
                std::memcpy(&band[(size_t)y * width * 3 + cx * tile_row],
                            &tiles.atlas.px[((ay + y) * tiles.atlas.width + ax) * 3], tile_row);
        }
        for (int y = 0; y < t; y++) png_write_row(png, &band[(size_t)y * width * 3]);
    }
    png_write_end(png, nullptr);
    png_destroy_write_struct(&png, &info);
    output.commit();
}

void write_indices(const std::string& path, const std::vector<int>& map, int cols, int rows) {
    std::ofstream out(path);
    if (!out) die("cannot write " + path);
    for (int y = 0; y < rows; y++)
        for (int x = 0; x < cols; x++)
            out << map[(size_t)y * cols + x] << (x + 1 < cols ? '\t' : '\n');
    if (!out) die("failed to write " + path);
}

const char* USAGE =
    "usage: objetismo -i INPUT.png -o OUTPUT.png [options]\n"
    "\n"
    "  -i, --input FILE      image to rebuild\n"
    "  -o, --output FILE     mosaic to write (PNG)\n"
    "  -w, --width N         mosaic width in tiles; the input is averaged down to\n"
    "                        N columns first (default: one tile per input pixel)\n"
    "  -t, --tiles DIR       directory with tiles.png and tiles.tsv (default: resources)\n"
    "  -m, --metric NAME     color difference: de2000 (default) or cie76\n"
    "      --dump-indices F  also write the chosen tile index of each cell as TSV\n"
    "  -h, --help            show this help\n";

}  // namespace

int main(int argc, char** argv) {
    std::string input, output, tiles_dir = "resources", metric = "de2000", dump;
    int width = 0;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        auto value = [&]() -> std::string {
            if (i + 1 >= argc) die("missing value for " + arg);
            return argv[++i];
        };
        if (arg == "-h" || arg == "--help") {
            std::cout << USAGE;
            return 0;
        } else if (arg == "-i" || arg == "--input") input = value();
        else if (arg == "-o" || arg == "--output") output = value();
        else if (arg == "-t" || arg == "--tiles") tiles_dir = value();
        else if (arg == "-m" || arg == "--metric") metric = value();
        else if (arg == "--dump-indices") dump = value();
        else if (arg == "-w" || arg == "--width") {
            std::string v = value();
            char* end;
            long n = std::strtol(v.c_str(), &end, 10);
            if (*end || n <= 0 || n > 100000) die("invalid --width: " + v);
            width = (int)n;
        } else die("unknown option " + arg + "\n" + USAGE);
    }
    if (input.empty() || output.empty()) die(std::string("missing -i or -o\n") + USAGE);
    if (metric != "de2000" && metric != "cie76") die("unknown metric " + metric);

    Tiles tiles = load_tiles(tiles_dir);
    int cols = 0, rows = 0;
    Image in = read_png(input, PNG_FORMAT_RGBA, [&](int w, int h) {
        cols = width ? width : w;
        rows = checked_rows(w, h, cols, tiles.set.size);
    });

    size_t distinct;
    std::vector<int> map = match(cell_colors(in.px.data(), in.width, in.height, cols, rows),
                                 tiles.set, metric == "de2000", &distinct);
    write_mosaic(output, map, cols, rows, tiles);
    if (!dump.empty()) write_indices(dump, map, cols, rows);

    std::vector<int> used(map);
    std::sort(used.begin(), used.end());
    used.erase(std::unique(used.begin(), used.end()), used.end());
    if (!used.empty() && used.front() < 0) used.erase(used.begin());
    std::cout << input << " (" << in.width << "x" << in.height << ") -> " << cols << "x" << rows
              << " tiles, " << distinct << " distinct colors, " << used.size() << " of "
              << tiles.set.lab.size() << " tiles used\n"
              << "wrote " << output << " (" << cols * tiles.set.size << "x"
              << rows * tiles.set.size << ")\n";
    return 0;
}
