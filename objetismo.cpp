// objetismo: rebuilds an image as a mosaic of Argentum Online item sprites.
//
// Each input pixel (or each block of pixels, with --width) becomes the 32x32
// tile whose color, as it looks on the black background, is closest in CIELAB.
// The tile set (tiles.png + tiles.tsv) is built by tools/build_tiles.py.

#include <png.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

const uint32_t EMPTY = 0xFFFFFFFF;  // cell with a transparent input pixel

[[noreturn]] void die(const std::string& msg) {
    std::cerr << "objetismo: " << msg << std::endl;
    std::exit(1);
}

struct Lab {
    double L, a, b;
};

struct Image {
    int width = 0, height = 0;
    std::vector<uint8_t> px;  // RGB or RGBA, row-major
};

struct Tiles {
    int size = 0, cols = 0;
    std::vector<Lab> lab;
    Image atlas;  // RGB
};

// ---- color ----------------------------------------------------------------

double srgb_to_linear(int c) {
    double v = c / 255.0;
    return v > 0.04045 ? std::pow((v + 0.055) / 1.055, 2.4) : v / 12.92;
}

int linear_to_srgb(double l) {
    l = std::min(1.0, std::max(0.0, l));
    double v = l > 0.0031308 ? 1.055 * std::pow(l, 1 / 2.4) - 0.055 : 12.92 * l;
    return (int)std::lround(v * 255);
}

double lab_f(double t) {
    return t > 216.0 / 24389.0 ? std::cbrt(t) : (24389.0 / 27.0 * t + 16) / 116;
}

// sRGB (D65) -> XYZ -> CIELAB.
Lab rgb_to_lab(int r8, int g8, int b8) {
    double r = srgb_to_linear(r8), g = srgb_to_linear(g8), b = srgb_to_linear(b8);
    double x = 0.4124564 * r + 0.3575761 * g + 0.1804375 * b;
    double y = 0.2126729 * r + 0.7151522 * g + 0.0721750 * b;
    double z = 0.0193339 * r + 0.1191920 * g + 0.9503041 * b;
    double fx = lab_f(x / 0.95047), fy = lab_f(y), fz = lab_f(z / 1.08883);
    return {116 * fy - 16, 500 * (fx - fy), 200 * (fy - fz)};
}

double cie76(const Lab& p, const Lab& q) {
    double dL = p.L - q.L, da = p.a - q.a, db = p.b - q.b;
    return std::sqrt(dL * dL + da * da + db * db);
}

double deg(double r) { return r * 180.0 / M_PI; }
double rad(double d) { return d * M_PI / 180.0; }

// CIEDE2000, following Sharma, Wu & Dalal (2005).
double ciede2000(const Lab& p, const Lab& q) {
    const double pow25_7 = 6103515625.0;  // 25^7
    double c1 = std::hypot(p.a, p.b), c2 = std::hypot(q.a, q.b);
    double cbar7 = std::pow((c1 + c2) / 2, 7);
    double g = 0.5 * (1 - std::sqrt(cbar7 / (cbar7 + pow25_7)));
    double a1 = (1 + g) * p.a, a2 = (1 + g) * q.a;
    double c1p = std::hypot(a1, p.b), c2p = std::hypot(a2, q.b);
    double h1p = (a1 == 0 && p.b == 0) ? 0 : deg(std::atan2(p.b, a1));
    double h2p = (a2 == 0 && q.b == 0) ? 0 : deg(std::atan2(q.b, a2));
    if (h1p < 0) h1p += 360;
    if (h2p < 0) h2p += 360;

    double dLp = q.L - p.L, dCp = c2p - c1p, dhp = 0;
    if (c1p * c2p != 0) {
        dhp = h2p - h1p;
        if (dhp > 180) dhp -= 360;
        else if (dhp < -180) dhp += 360;
    }
    double dHp = 2 * std::sqrt(c1p * c2p) * std::sin(rad(dhp / 2));

    double lbar = (p.L + q.L) / 2, cbarp = (c1p + c2p) / 2, hbarp = h1p + h2p;
    if (c1p * c2p != 0) {
        if (std::fabs(h1p - h2p) <= 180) hbarp /= 2;
        else hbarp = hbarp < 360 ? (hbarp + 360) / 2 : (hbarp - 360) / 2;
    }
    double t = 1 - 0.17 * std::cos(rad(hbarp - 30)) + 0.24 * std::cos(rad(2 * hbarp)) +
               0.32 * std::cos(rad(3 * hbarp + 6)) - 0.20 * std::cos(rad(4 * hbarp - 63));
    double dtheta = 30 * std::exp(-std::pow((hbarp - 275) / 25, 2));
    double cbarp7 = std::pow(cbarp, 7);
    double rc = 2 * std::sqrt(cbarp7 / (cbarp7 + pow25_7));
    double l50 = (lbar - 50) * (lbar - 50);
    double sl = 1 + 0.015 * l50 / std::sqrt(20 + l50);
    double sc = 1 + 0.045 * cbarp, sh = 1 + 0.015 * cbarp * t;
    double rt = -std::sin(rad(2 * dtheta)) * rc;
    double vl = dLp / sl, vc = dCp / sc, vh = dHp / sh;
    return std::sqrt(vl * vl + vc * vc + vh * vh + rt * vc * vh);
}

// ---- files ----------------------------------------------------------------

Image read_png(const std::string& path, png_uint_32 format) {
    png_image im;
    std::memset(&im, 0, sizeof im);
    im.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_file(&im, path.c_str()))
        die(path + ": " + im.message);
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
    int count = -1;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        if (line[0] == '#') {
            std::sscanf(line.c_str(), "# objetismo tiles: tile_size=%d cols=%d count=%d",
                        &tiles.size, &tiles.cols, &count);
            continue;
        }
        std::istringstream row(line);
        int index;
        Lab lab;
        if (!(row >> index >> lab.L >> lab.a >> lab.b) || index != (int)tiles.lab.size())
            die(tsv + ": bad row: " + line);
        tiles.lab.push_back(lab);
    }
    if (tiles.size <= 0 || tiles.cols <= 0 || count != (int)tiles.lab.size() || count == 0)
        die(tsv + ": missing or inconsistent header");

    tiles.atlas = read_png(dir + "/tiles.png", PNG_FORMAT_RGB);
    int rows = (count + tiles.cols - 1) / tiles.cols;
    if (tiles.atlas.width != tiles.cols * tiles.size || tiles.atlas.height < rows * tiles.size)
        die(dir + "/tiles.png: size does not match tiles.tsv");
    return tiles;
}

// ---- mosaic ---------------------------------------------------------------

// Averages the RGBA input into cols x rows cells, in linear light, composited
// on black. Returns one packed 0xRRGGBB per cell, or EMPTY if mostly transparent.
std::vector<uint32_t> cell_colors(const Image& in, int cols, int rows) {
    double lin[256];
    for (int i = 0; i < 256; i++) lin[i] = srgb_to_linear(i);

    std::vector<uint32_t> cells((size_t)cols * rows);
    for (int cy = 0; cy < rows; cy++) {
        int y0 = (int)((int64_t)cy * in.height / rows);
        int y1 = std::max(y0 + 1, (int)((int64_t)(cy + 1) * in.height / rows));
        for (int cx = 0; cx < cols; cx++) {
            int x0 = (int)((int64_t)cx * in.width / cols);
            int x1 = std::max(x0 + 1, (int)((int64_t)(cx + 1) * in.width / cols));
            double r = 0, g = 0, b = 0, a = 0;
            for (int y = y0; y < y1; y++) {
                const uint8_t* p = &in.px[((size_t)y * in.width + x0) * 4];
                for (int x = x0; x < x1; x++, p += 4) {
                    double alpha = p[3] / 255.0;
                    r += lin[p[0]] * alpha;
                    g += lin[p[1]] * alpha;
                    b += lin[p[2]] * alpha;
                    a += alpha;
                }
            }
            double n = (double)(y1 - y0) * (x1 - x0);
            cells[(size_t)cy * cols + cx] =
                a / n < 0.5 ? EMPTY
                            : (uint32_t)linear_to_srgb(r / n) << 16 |
                                  (uint32_t)linear_to_srgb(g / n) << 8 |
                                  (uint32_t)linear_to_srgb(b / n);
        }
    }
    return cells;
}

// Maps each distinct cell color to its closest tile, then each cell to a tile
// index (-1 for empty cells).
std::vector<int> match(const std::vector<uint32_t>& cells, const Tiles& tiles, bool use_de2000,
                       size_t* distinct) {
    std::vector<uint32_t> colors(cells);
    std::sort(colors.begin(), colors.end());
    colors.erase(std::unique(colors.begin(), colors.end()), colors.end());
    if (!colors.empty() && colors.back() == EMPTY) colors.pop_back();
    *distinct = colors.size();

    std::vector<int> best(colors.size());
#pragma omp parallel for schedule(dynamic, 64)
    for (long i = 0; i < (long)colors.size(); i++) {
        uint32_t c = colors[i];
        Lab lab = rgb_to_lab(c >> 16, (c >> 8) & 0xFF, c & 0xFF);
        double best_d = INFINITY;
        int best_k = 0;
        for (size_t k = 0; k < tiles.lab.size(); k++) {
            double d = use_de2000 ? ciede2000(lab, tiles.lab[k]) : cie76(lab, tiles.lab[k]);
            if (d < best_d) {
                best_d = d;
                best_k = (int)k;
            }
        }
        best[i] = best_k;
    }

    std::vector<int> out(cells.size());
    for (size_t i = 0; i < cells.size(); i++) {
        if (cells[i] == EMPTY) {
            out[i] = -1;
            continue;
        }
        auto it = std::lower_bound(colors.begin(), colors.end(), cells[i]);
        out[i] = best[it - colors.begin()];
    }
    return out;
}

// Writes the mosaic one row of tiles at a time, so the full output image is
// never held in memory.
void write_mosaic(const std::string& path, const std::vector<int>& map, int cols, int rows,
                  const Tiles& tiles) {
    const int t = tiles.size;
    const size_t width = (size_t)cols * t, height = (size_t)rows * t;
    if (width > PNG_UINT_31_MAX || height > PNG_UINT_31_MAX)
        die("output too large, use --width to reduce it");
    std::vector<uint8_t> band(width * 3 * t);

    FILE* fp = std::fopen(path.c_str(), "wb");
    if (!fp) die("cannot write " + path + ": " + std::strerror(errno));
    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    png_infop info = png ? png_create_info_struct(png) : nullptr;
    if (!info) die("out of memory");
    if (setjmp(png_jmpbuf(png))) {
        std::fclose(fp);
        die("failed to write " + path);
    }
    png_init_io(png, fp);
    png_set_IHDR(png, info, (png_uint_32)width, (png_uint_32)height, 8, PNG_COLOR_TYPE_RGB,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);

    const size_t tile_row = (size_t)t * 3;
    for (int cy = 0; cy < rows; cy++) {
        std::fill(band.begin(), band.end(), 0);
        for (int cx = 0; cx < cols; cx++) {
            int k = map[(size_t)cy * cols + cx];
            if (k < 0) continue;
            size_t ax = (size_t)(k % tiles.cols) * t, ay = (size_t)(k / tiles.cols) * t;
            for (int y = 0; y < t; y++)
                std::memcpy(&band[(size_t)y * width * 3 + cx * tile_row],
                            &tiles.atlas.px[((ay + y) * tiles.atlas.width + ax) * 3], tile_row);
        }
        for (int y = 0; y < t; y++) png_write_row(png, &band[(size_t)y * width * 3]);
    }
    png_write_end(png, nullptr);
    png_destroy_write_struct(&png, &info);
    if (std::fclose(fp) != 0) die("failed to write " + path);
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
    Image in = read_png(input, PNG_FORMAT_RGBA);

    int cols = width ? width : in.width;
    int rows = width ? std::max(1, (int)std::lround((double)in.height * cols / in.width))
                     : in.height;

    size_t distinct;
    std::vector<int> map = match(cell_colors(in, cols, rows), tiles, metric == "de2000", &distinct);
    write_mosaic(output, map, cols, rows, tiles);
    if (!dump.empty()) write_indices(dump, map, cols, rows);

    std::vector<int> used(map);
    std::sort(used.begin(), used.end());
    used.erase(std::unique(used.begin(), used.end()), used.end());
    if (!used.empty() && used.front() < 0) used.erase(used.begin());
    std::cout << input << " (" << in.width << "x" << in.height << ") -> " << cols << "x" << rows
              << " tiles, " << distinct << " distinct colors, " << used.size() << " of "
              << tiles.lab.size() << " tiles used\n"
              << "wrote " << output << " (" << cols * tiles.size << "x" << rows * tiles.size
              << ")\n";
    return 0;
}
