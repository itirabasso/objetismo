/*
 * A simple libpng example program
 * http://zarb.org/~gc/html/libpng.html
 *
 * Modified by Yoshimasa Niwa to make it much simpler
 * and support all defined color_type.
 *
 * To build, use the next instruction on OS X.
 * $ brew install libpng
 * $ clang -lz -lpng15 libpng_test.c
 *
 * Copyright 2002-2010 Guillaume Cottenceau.
 *
 * This software may be freely redistributed under the terms
 * of the X11 license.
 *
 */

#ifndef __PNGO_H__
#define __PNGO_H__
#include <stdlib.h>
#include <stdio.h>
#include <png.h>

class PNG {
    public:
        PNG();
        ~PNG();
        void read(const char *filename);
        void write(const char *filename);
        void paste(PNG& src, int x, int y);
        void create(int width, int height);
        int width();
        int height();
        png_bytep getPixel(int x, int y);
        void setPixel(png_bytep p, int x, int y);

    private:

        int _width;
        int _height;
        png_byte color_type;
        png_byte bit_depth;
        png_bytep *row_pointers;
};

int PNG::width() {
    return _width;
}
int PNG::height() {
    return _height;
}

PNG::PNG() : _height(0), _width(0) {}
PNG::~PNG() {
    for(int y = 0; y < _height; y++) {
        free(row_pointers[y]);
    }
    free(row_pointers);
}

void PNG::read(const char *filename) {
    
    std::cout << "Cargando " << filename << std::endl;

    FILE *fp = fopen(filename, "rb");

    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if(!png) abort();
 
    png_infop info = png_create_info_struct(png);
    if(!info) abort();
 
    if(setjmp(png_jmpbuf(png))) abort();
 
    png_init_io(png, fp);
 
    png_read_info(png, info);
 
    _width            = png_get_image_width(png, info);
    _height           = png_get_image_height(png, info);
    color_type       = png_get_color_type(png, info);
    bit_depth        = png_get_bit_depth(png, info);
 
    // Read any color_type into 8bit depth, RGBA format.
    // See http://www.libpng.org/pub/png/libpng-manual.txt
 
    if(bit_depth == 16)
        png_set_strip_16(png);
 
    if(color_type == PNG_COLOR_TYPE_PALETTE)
        png_set_palette_to_rgb(png);
 
    // PNG_COLOR_TYPE_GRAY_ALPHA is always 8 or 16bit depth.
    if(color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8)
        png_set_expand_gray_1_2_4_to_8(png);
 
    if(png_get_valid(png, info, PNG_INFO_tRNS))
        png_set_tRNS_to_alpha(png);
 
    // These color_type don't have an alpha channel then fill it with 0xff.
    if(color_type == PNG_COLOR_TYPE_RGB ||
         color_type == PNG_COLOR_TYPE_GRAY ||
         color_type == PNG_COLOR_TYPE_PALETTE)
        png_set_filler(png, 0xFF, PNG_FILLER_AFTER);
 
    if(color_type == PNG_COLOR_TYPE_GRAY ||
         color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
        png_set_gray_to_rgb(png);
 
    png_read_update_info(png, info);
 
    row_pointers = (png_bytep*)malloc(sizeof(png_bytep) * _height);
    for(int y = 0; y < _height; y++) {
        row_pointers[y] = (png_byte*)malloc(png_get_rowbytes(png,info));
    }
 
    png_read_image(png, row_pointers);
 
    fclose(fp);
}
 
void PNG::write(const char *filename) {
 
    std::cout << "Guardando " << filename << std::endl;

    FILE *fp = fopen(filename, "wb");

    if(!fp) abort();
 
    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png) abort();
 
    png_infop info = png_create_info_struct(png);
    if (!info) abort();
 
    if (setjmp(png_jmpbuf(png))) abort();
 
    png_init_io(png, fp);
 
    // Output is 8bit depth, RGBA format.
    png_set_IHDR(
        png,
        info,
        _width, _height,
        8,
        PNG_COLOR_TYPE_RGBA,
        PNG_INTERLACE_NONE,
        PNG_COMPRESSION_TYPE_DEFAULT,
        PNG_FILTER_TYPE_DEFAULT
    );
    png_write_info(png, info);
 
    png_write_image(png, row_pointers);
    png_write_end(png, NULL);
 
    fclose(fp);
}

png_bytep PNG::getPixel(int x, int y) {
    png_bytep row = row_pointers[y];
    return &(row[x * 4]);
}


void PNG::setPixel(png_bytep p, int x, int y) {
    png_bytep row = row_pointers[y];
    png_byte* ptr = &(row[x*4]);
    ptr[0] = p[0];
    ptr[1] = p[1];
    ptr[2] = p[2];
    ptr[3] = p[3];
    // row[x*4] = *p;
} 

void PNG::paste(PNG& src, int x, int y) {
    for(int i = 0; i < src.width(); i++) {
        for(int j = 0; j < src.height(); j++) {
            png_byte* p = src.getPixel(i, j);
            setPixel(p, x+i, y+j);
            // getPixel(x+i, y+j) = p;
        }
    }
}

void PNG::create(int width, int height) {
    _width = width;
    _height = height;

    // png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    // if (!png) abort();
 
    // png_infop info = png_create_info_struct(png);
    // if (!info) abort();
 
    // if (setjmp(png_jmpbuf(png))) abort();
    // png_init_io(png, fp);
 
    // Output is 8bit depth, RGBA format.
    // png_set_IHDR(
    //     png,
    //     info,
    //     _width, _height,
    //     8,
    //     PNG_COLOR_TYPE_RGBA,
    //     PNG_INTERLACE_NONE,
    //     PNG_COMPRESSION_TYPE_DEFAULT,
    //     PNG_FILTER_TYPE_DEFAULT
    // );
    // png_write_info(png, info);
 
    row_pointers = (png_bytep*)malloc(sizeof(png_bytep) * _height);
    for(int y = 0; y < _height; y++) {
        // row_pointers[y] = (png_byte*)malloc(png_get_rowbytes(png,info));
        row_pointers[y] = (png_byte*)malloc(width);
    }

}

 #endif /* __PNGO_H__ */