/*
===============================================================================
    Copyright (C) 2011-2023 Ilya Lyakhovets

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in all
    copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
    SOFTWARE.
===============================================================================
*/

#ifndef __TGA_H__
#define __TGA_H__

#if defined(_WIN32) && (!(defined(_CRT_SECURE_NO_WARNINGS)))
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stdio.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned char byte;
typedef short word;

typedef enum
{
    TGA_MAPPED,
    TGA_RGB,
    TGA_RGB16,
    TGA_BW,
    TGA_MAPPED_RLE,
    TGA_RGB_RLE,
    TGA_RGB16_RLE,
    TGA_BW_RLE
} tga_type;

typedef struct
{
    unsigned int width;
    unsigned int height;
    unsigned int channels;
    unsigned char *data;
} tga_image;

typedef void *(*open_file_func) (const char *filename, char const *mode, void *stream);
typedef size_t(*read_file_func) (void *buffer, size_t size, size_t count, void *stream);
typedef long(*seek_file_func) (void *stream, long offset, int origin);
typedef int(*close_file_func) (void *stream);

typedef struct
{
    open_file_func open_file;
    read_file_func read_file;
    seek_file_func seek_file;
    close_file_func close_file;

    void *file;
} tga_filefunc_def;

extern void flip_tga_horizontally(tga_image *ptga);
extern void flip_tga_vertically(tga_image *ptga);
extern bool load_tga(const char *filename, tga_image *ptga);
extern bool load_tga2(const char *filename, tga_image *tga, tga_filefunc_def *ptga_filefunc_def);
extern void free_tga(tga_image *ptga);
extern bool write_tga(const char *filename, tga_image *ptga, tga_type type);

#ifdef __cplusplus
}
#endif

#endif // !__TGA_H__
