// objetismo core: color conversion, color difference and tile matching.
//
// Shared by the command line tool (objetismo.cpp) and the WebAssembly build
// (web/objetismo_wasm.cpp). Nothing here does file or PNG I/O.

#ifndef OBJETISMO_H
#define OBJETISMO_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <istream>
#include <sstream>
#include <string>
#include <vector>

namespace objetismo {

const uint32_t EMPTY = 0xFFFFFFFF;  // cell with a transparent input pixel
const double PI = 3.14159265358979323846;

struct Lab {
    double L, a, b;
};

// Tile colors from tiles.tsv. The tile pixels live in tiles.png, which each
// front end reads on its own.
struct TileSet {
    int size = 0, cols = 0;
    std::vector<Lab> lab;
};

// ---- color ----------------------------------------------------------------

inline double srgb_to_linear(int c) {
    double v = c / 255.0;
    return v > 0.04045 ? std::pow((v + 0.055) / 1.055, 2.4) : v / 12.92;
}

inline int linear_to_srgb(double l) {
    l = std::min(1.0, std::max(0.0, l));
    double v = l > 0.0031308 ? 1.055 * std::pow(l, 1 / 2.4) - 0.055 : 12.92 * l;
    return (int)std::lround(v * 255);
}

inline double lab_f(double t) {
    return t > 216.0 / 24389.0 ? std::cbrt(t) : (24389.0 / 27.0 * t + 16) / 116;
}

// sRGB (D65) -> XYZ -> CIELAB.
inline Lab rgb_to_lab(int r8, int g8, int b8) {
    double r = srgb_to_linear(r8), g = srgb_to_linear(g8), b = srgb_to_linear(b8);
    double x = 0.4124564 * r + 0.3575761 * g + 0.1804375 * b;
    double y = 0.2126729 * r + 0.7151522 * g + 0.0721750 * b;
    double z = 0.0193339 * r + 0.1191920 * g + 0.9503041 * b;
    double fx = lab_f(x / 0.95047), fy = lab_f(y), fz = lab_f(z / 1.08883);
    return {116 * fy - 16, 500 * (fx - fy), 200 * (fy - fz)};
}

inline double cie76(const Lab& p, const Lab& q) {
    double dL = p.L - q.L, da = p.a - q.a, db = p.b - q.b;
    return std::sqrt(dL * dL + da * da + db * db);
}

inline double deg(double r) { return r * 180.0 / PI; }
inline double rad(double d) { return d * PI / 180.0; }

// CIEDE2000, following Sharma, Wu & Dalal (2005).
inline double ciede2000(const Lab& p, const Lab& q) {
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

// ---- tiles ----------------------------------------------------------------

// Reads tiles.tsv. Returns an empty string on success, or what is wrong.
inline std::string parse_tiles(std::istream& in, TileSet* tiles) {
    int count = -1;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        if (line[0] == '#') {
            std::sscanf(line.c_str(), "# objetismo tiles: tile_size=%d cols=%d count=%d",
                        &tiles->size, &tiles->cols, &count);
            continue;
        }
        std::istringstream row(line);
        int index;
        Lab lab;
        if (!(row >> index >> lab.L >> lab.a >> lab.b) || index != (int)tiles->lab.size())
            return "bad row: " + line;
        tiles->lab.push_back(lab);
    }
    if (tiles->size <= 0 || tiles->cols <= 0 || count != (int)tiles->lab.size() || count == 0)
        return "missing or inconsistent header";
    return "";
}

// ---- mosaic ---------------------------------------------------------------

// Mosaic height in tiles for an input of width x height averaged down to cols
// columns, keeping the aspect ratio.
inline int mosaic_rows(int width, int height, int cols) {
    return std::max(1, (int)std::lround((double)height * cols / width));
}

// Averages the RGBA input into cols x rows cells, in linear light, composited
// on black. Returns one packed 0xRRGGBB per cell, or EMPTY if mostly transparent.
inline std::vector<uint32_t> cell_colors(const uint8_t* rgba, int width, int height, int cols,
                                         int rows) {
    double lin[256];
    for (int i = 0; i < 256; i++) lin[i] = srgb_to_linear(i);

    std::vector<uint32_t> cells((size_t)cols * rows);
    for (int cy = 0; cy < rows; cy++) {
        int y0 = (int)((int64_t)cy * height / rows);
        int y1 = std::max(y0 + 1, (int)((int64_t)(cy + 1) * height / rows));
        for (int cx = 0; cx < cols; cx++) {
            int x0 = (int)((int64_t)cx * width / cols);
            int x1 = std::max(x0 + 1, (int)((int64_t)(cx + 1) * width / cols));
            double r = 0, g = 0, b = 0, a = 0;
            for (int y = y0; y < y1; y++) {
                const uint8_t* p = &rgba[((size_t)y * width + x0) * 4];
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
inline std::vector<int> match(const std::vector<uint32_t>& cells, const TileSet& tiles,
                              bool use_de2000, size_t* distinct) {
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

}  // namespace objetismo

#endif
