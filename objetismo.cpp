// g++ -g tests.cpp -o tests
// valgrind --leak-check=full ./tests

// #include "mini_test.h"
// #include "messineria.h"
#include <math.h>
#include <fstream>
#include <iostream>
#include <string>

#include "png.h"

#define CIE_8 0.008856 // 216.0/24389.0

using namespace std;

struct RGB {
    int red;
    int green;
    int blue;

    // soy negro.
    RGB() : red(-1337), green(0), blue(0) {}

    RGB(int r, int g, int b) : red(r), green(g), blue(b) {
        if (longColor() == 16777215) red = green = blue = 0; 
    }

    RGB(const RGB& c) : red(c.red), green(c.green), blue(c.blue) {
        if (longColor() == 16777215) red = green = blue = 0;
    }

    // soy negro.
    bool valid() { return red >= 0; }

    int longColor() {
        return red + green*256 + blue*65536;
    }
};

struct XYZ {
    double x;
    double y;
    double z;

    XYZ(double x, double y, double z) : x(x), y(y), z(z) {}

};

struct LAB {
    double l;
    double a;
    double b;

    LAB(double l, double a, double b) : l(l), a(a), b(b) {}

};

int cache[256 + 256*256 + 256*65536];

// 100% negro.
RGB objects[2000];


double xyzTransformation(double v) {
    if (v > 0.04045) {
        v = pow((v + 0.055) / 1.055, 2.4);
    } else {
        v = v / 12.92;
    } 
    return v;
}

XYZ RGB_to_XYZ(const RGB& c) {
    XYZ xyz(
        xyzTransformation(c.red / 255.0),
        xyzTransformation(c.green / 255.0),
        xyzTransformation(c.blue / 255.0)
    );

    return xyz;
}

double labTransformation(double v) {
    if (v > 0.008856) {
        // v = pow(v, 1/3.0);
        v = pow(v, 1/3.0);
    } else {
        v = (7.787 * v) + (16.0 / 116.0);
    }
    return v;
}

LAB XYZ_to_Lab(const XYZ& c) {
    double x = c.x / 0.95047;
    double y = c.y / 1.0;
    double z = c.z / 1.08883;

    x = labTransformation(x);
    y = labTransformation(y);
    z = labTransformation(z);

    LAB lab((116.0 * y) - 16, 500.0 * (x - y), 200.0 * (y - z));

    return lab;
}

int cie1976(const RGB& color1, const RGB& color2) {
    LAB lab1 = XYZ_to_Lab(RGB_to_XYZ(color1));
    LAB lab2 = XYZ_to_Lab(RGB_to_XYZ(color2));

    double d = sqrt( pow(lab1.l - lab2.l, 2) + pow(lab1.a - lab2.a, 2) + pow(lab1.b - lab2.b, 2) );

    return (int) d;
}

int cie1994(const RGB& color1, const RGB& color2) {

    LAB lab1 = XYZ_to_Lab(RGB_to_XYZ(color1));
    LAB lab2 = XYZ_to_Lab(RGB_to_XYZ(color2));

    double dL = lab1.l - lab2.l;
    double dA = lab1.a - lab2.a;
    double dB = lab1.b - lab2.b;
    double c1 = sqrt(pow(lab1.a, 2) + pow(lab1.b, 2));
    double c2 = sqrt(pow(lab2.a, 2) + pow(lab2.b, 2));
    double dC = c1 - c2;
    double dH2 = pow(dA, 2) + pow(dB, 2) - pow(dC, 2);
    double dH = 0;
    if (dH2 > 0.0) {
        // dH = math.sqrt(dA**2 + dB**2 - dC**2)
        dH = sqrt(dH2);
    }
    double sL = 1.0;
    double k1 = 0.045; //0.048
    double k2 = 0.015; // #0.014
    double sC = 1.0 + k1 * c1;
    double sH = 1.0 + k2 * c1;
    
    double kL = 1.0;
    double kC = 1.0;
    double kH = 1.0;
    
    // e = pow(dL / (kL * sL), 2) + pow(dC / (kC * sC), 2) + pow(dH / (kH * sH), 2)
    return (int) sqrt(pow(dL / (kL * sL), 2) + pow(dC / (kC * sC), 2) + pow(dH / (kH * sH), 2));
}

int colorDistance(const RGB& c1, const RGB& c2) {
    // return cie1994(c1, c2);
    return cie1976(c1, c2);
}

int getObjectForColor(RGB c) {
    int colorValue = c.longColor();
    int r = cache[colorValue];
    
    if (r != 0) {
        return r;
    }

    int i, cD;
    int d = 16777215;
    int best = 0;

    for (i = 0; i < 2000; i++) {
        if (!objects[i].valid()) continue;
        cD = colorDistance(c, objects[i]);
        // Same color!
        if (cD == 0) {
            best = i;
            break;
        }
        if (cD < d) {
            d = cD;
            best = i;
        }
    }
    cache[colorValue] = best;
    return cache[colorValue];
}

int main(int argc, const char *argv[]) {
    if (argc < 3) {
        std::cout << "Usage ./objetismo image output" << std::endl;
    }


    ::std::ifstream in("resources/colors", ::std::ios::binary);

    while (!in.eof()) {
        int id1 = in.get();
        if (id1 == -1) break;
        int id2 = in.get();
        int id = id1 + (id2 * 256);
        int r = in.get();
        int g = in.get();
        int b = in.get();
        // std::cout << id << " => (" << r << "," << g << "," << b << ")" << endl;
        objects[id] = RGB(r, g, b);
    }

    std::ofstream out(argv[2], std::ofstream::binary);

    PNG img;
    img.read(argv[1]);

    short w = img.width();
    short h = img.height();

    out.write(reinterpret_cast<const char *>(&w), sizeof(w));
    out.write(reinterpret_cast<const char *>(&h), sizeof(h));

    short dst_matrix [w][h];
    int x,y;
#pragma omp parallel for
    for (x = 0; x < w; x++) {
        for (y = 0; y < h; y++) {
            png_bytep p = img.getPixel(x, y);
            RGB c(p[0],p[1],p[2]);
            dst_matrix[x][y] = getObjectForColor(c);
        }
    }

    for (x = 0; x < w; x++) {
        std::cout << x+1 << "/" << w << endl;
        for (y = 0; y < h; y++) {
            short objId = dst_matrix[x][y];;
            out.write(reinterpret_cast<const char *>(&objId), sizeof(objId));
        }
    }
    out.close();




    return 0;
}

