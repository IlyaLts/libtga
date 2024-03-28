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

#include "tga.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TGA_TYPE_NO_IMAGE       0
#define TGA_TYPE_MAPPED         1
#define TGA_TYPE_RGB            2
#define TGA_TYPE_BW             3
#define TGA_TYPE_MAPPED_RLE     9
#define TGA_TYPE_RGB_RLE        10
#define TGA_TYPE_BW_RLE         11

#if defined(_WIN64) || defined(_WIN32)
#include <locale.h>

static char *saved_locale;
#endif

static void swap_byte(byte *a, byte *b)
{
    byte temp = *a;
    *a = *b;
    *b = temp;
}

static void rgb_to_bgr(const byte *origin, byte *dest, int channels)
{
    // Do not reorder the following code below as the order is very important

    // Alpha
    if (channels == 4)
        dest[3] = origin[3];

    dest[2] = origin[0];    // B
    dest[1] = origin[1];    // G
    dest[0] = origin[2];    // R
}

static void rgb_to_rgb16(const byte *data, word *pixel, int channels)
{
    *pixel = 0;
    *pixel |= (data[0] >> 3) << 10;     // R
    *pixel |= (data[1] >> 3) << 5;      // G
    *pixel |= (data[2] >> 3);           // B

    // Alpha
    if (channels == 4)
        *pixel |= data[3] ? 1 << 15 : 0 << 15;
    else
        *pixel |= 1 << 15;
}

static void rgb16_to_rgb(const word *pixel, byte *data, int channels)
{
    data[0] = ((*pixel >> 10) & 0x1f) << 3;      // R
    data[1] = ((*pixel >> 5) & 0x1f) << 3;       // G
    data[2] = (*pixel & 0x1f) << 3;              // B

    // Alpha
    if (channels == 4)
        data[3] = *pixel & 0x8000 ? 255 : 0;
}

static void rgb_to_bw(const byte *data, byte *pixel, int channels, int pixel_size)
{
    pixel[0] = (data[0] + data[1] + data[2]) / 3;

    // Alpha
    if (channels == 4)
        pixel[1] = data[3];
    else if (pixel_size == 2)
        pixel[1] = 255;
}

static void bw_to_rgb(const byte *pixel, byte *data, int channels)
{
    // Do not reorder the following code below as the order is very important

    // Alpha
    if (channels == 4)
        data[3] = pixel[1];

    // Colors
    data[2] = pixel[0];
    data[1] = pixel[0];
    data[0] = pixel[0];
}

void flip_tga_horizontally(tga_image *tga)
{
    if (!tga || !tga->data)
        return;

    for (unsigned int i = 0; i < tga->height; i++)
    {
        for (unsigned int j = 0; j < tga->width / 2; j++)
        {
            for (unsigned int k = 0; k < tga->channels; k++)
            {
                swap_byte(&tga->data[((i * tga->width) + j) * tga->channels + k],
                          &tga->data[((i * tga->width) + (tga->width - j - 1)) * tga->channels + k]);
            }
        }
    }
}

void flip_tga_vertically(tga_image *tga)
{
    if (!tga || !tga->data)
        return;

    for (unsigned int i = 0; i < tga->width * tga->channels; i++)
    {
        for (unsigned int j = 0; j < tga->height / 2; j++)
        {
            swap_byte(&tga->data[i + tga->width * tga->channels * j],
                      &tga->data[i + tga->width * tga->channels * (tga->height - j - 1)]);
        }
    }
}

static void *fopen_wrapper(const char *filename, char const *mode, const void *stream)
{
    return fopen(filename, mode);
}

bool load_tga(const char *filename, tga_image *tga)
{
    tga_func_def func_def;

    func_def.open_file = fopen_wrapper;
    func_def.read_file = fread;
    func_def.seek_file = fseek;
    func_def.close_file = fclose;

    return load_tga_ext(filename, tga, &func_def);
}

static bool read_mapped(tga_image *tga, const byte **color_data, const tga_func_def *func_def)
{
    int pixels = tga->width * tga->height;

    tga->data = (byte *)malloc(pixels * tga->channels);
    if (!tga->data)
        return false;

    if (func_def->read_file(tga->data, sizeof(byte), pixels, func_def->file) != pixels)
        return false;

    for (int i = pixels - 1; i >= 0; i--)
        rgb_to_bgr(&(*color_data)[tga->data[i] * tga->channels], &tga->data[i * tga->channels], tga->channels);

    return true;
}

static bool read_rgb(tga_image *tga, const tga_func_def *func_def)
{
    int pixels = tga->width * tga->height;

    tga->data = (byte *)malloc(pixels * tga->channels);
    if (!tga->data)
        return false;

    if (func_def->read_file(tga->data, sizeof(byte), pixels * tga->channels, func_def->file) != (pixels * tga->channels))
        return false;

    for (unsigned int y = 0; y < tga->height; y++)
    {
        byte *pixel = &(tga->data[tga->width * tga->channels * y]);

        for (unsigned int i = 0; i < tga->width * tga->channels; i += tga->channels)
            swap_byte(&pixel[i], &pixel[i + 2]);
    }

    return true;
}

static bool read_rgb16(tga_image *tga, const tga_func_def *func_def)
{
    int pixels = tga->width * tga->height;

    tga->data = (byte *)malloc(pixels * tga->channels);
    if (!tga->data)
        return false;

    if (func_def->read_file(tga->data, sizeof(word), pixels, func_def->file) != pixels)
        return false;

    for (int i = pixels - 1; i >= 0; i--)
        rgb16_to_rgb((word *)&tga->data[i * sizeof(word)], &tga->data[i * tga->channels], tga->channels);

    return true;
}

static bool read_bw(tga_image *tga, const tga_func_def *func_def)
{
    int bytes = tga->channels == 4 ? sizeof(word) : sizeof(byte);
    int pixels = tga->width * tga->height;

    tga->data = (byte *)malloc(pixels * tga->channels);
    if (!tga->data)
        return false;

    if (func_def->read_file(tga->data, sizeof(byte), pixels * bytes, func_def->file) != (pixels * bytes))
        return false;

    for (int i = pixels - 1; i >= 0; i--)
        bw_to_rgb(&tga->data[i * bytes], &tga->data[i * tga->channels], tga->channels);

    return true;
}

static bool read_mapped_rle(tga_image *tga, const byte **color_data, const tga_func_def *func_def)
{
    int pixels = tga->width * tga->height;
    int data_size = pixels * tga->channels;
    int rle_size = pixels * sizeof(byte) + pixels;
    int index_to_temp = data_size;

    tga->data = (byte *)malloc(data_size + rle_size);
    if (!tga->data)
        return false;

    if (!func_def->read_file(&tga->data[index_to_temp], sizeof(byte), rle_size, func_def->file))
        return false;

    for (unsigned int i = 0; i < tga->width * tga->height;)
    {
        byte rle_id = tga->data[index_to_temp];
        index_to_temp++;

        // Run-length packet
        if (rle_id & 0x80)
        {
            rle_id -= 127;

            while (rle_id)
            {
                rgb_to_bgr(&(*color_data)[tga->data[index_to_temp] * tga->channels], &tga->data[i * tga->channels], tga->channels);

                i++;
                rle_id--;
            }

            index_to_temp += sizeof(byte);
        }
        // Raw packet
        else
        {
            rle_id++;

            for (int j = 0; j < rle_id; j++)
            {
                rgb_to_bgr(&(*color_data)[tga->data[index_to_temp] * tga->channels], &tga->data[(i + j) * tga->channels], tga->channels);
                index_to_temp += sizeof(byte);
            }

            i += rle_id;
        }
    }

    char *tmp = realloc(tga->data, data_size);
    if (tmp)
    {
        tga->data = tmp;
    }

    return true;
}

static bool read_rgb_rle(tga_image *tga, const tga_func_def *func_def)
{
    int data_size = tga->width * tga->height * tga->channels;
    int rle_size = data_size + tga->width * tga->height;
    int index_to_temp = data_size;

    tga->data = (byte *)malloc(data_size + rle_size);
    if (!tga->data)
        return false;

    if (!func_def->read_file(&tga->data[index_to_temp], sizeof(byte), rle_size, func_def->file))
        return false;

    for (unsigned int i = 0; i < tga->width * tga->height;)
    {
        byte rle_id = tga->data[index_to_temp];
        index_to_temp++;

        // Run-length packet
        if (rle_id & 0x80)
        {
            rle_id -= 127;

            while (rle_id)
            {
                rgb_to_bgr(&tga->data[index_to_temp], &tga->data[i * tga->channels], tga->channels);

                i++;
                rle_id--;
            }

            index_to_temp += tga->channels;
        }
        // Raw packet
        else
        {
            rle_id++;

            for (unsigned int j = 0; j < rle_id * tga->channels; j++)
            {
                tga->data[i * tga->channels + j] = tga->data[index_to_temp];
                index_to_temp++;
            }

            while (rle_id)
            {
                swap_byte(&tga->data[i * tga->channels], &tga->data[i * tga->channels + 2]);

                i++;
                rle_id--;
            }
        }
    }

    char *tmp = realloc(tga->data, data_size);
    if (tmp)
    {
        tga->data = tmp;
    }

    return true;
}

static bool read_rgb16_rle(tga_image *tga, const tga_func_def *func_def)
{
    int pixels = tga->width * tga->height;
    int data_size = pixels * tga->channels;
    int rle_size = pixels * sizeof(word) + pixels;
    int index_to_temp = data_size;

    tga->data = (byte *)malloc(data_size + rle_size);
    if (!tga->data)
        return false;

    if (!func_def->read_file(&tga->data[index_to_temp], sizeof(byte), rle_size, func_def->file))
        return false;

    for (unsigned int i = 0; i < tga->width * tga->height;)
    {
        byte rle_id = tga->data[index_to_temp];
        index_to_temp++;

        // Run-length packet
        if (rle_id & 0x80)
        {
            rle_id -= 127;

            while (rle_id)
            {
                rgb16_to_rgb((word *)&tga->data[index_to_temp], &tga->data[i * tga->channels], tga->channels);

                i++;
                rle_id--;
            }

            index_to_temp += sizeof(word);
        }
        // Raw packet
        else
        {
            rle_id++;

            for (int j = rle_id - 1; j >= 0; j--)
            {
                rgb16_to_rgb((word *)&tga->data[index_to_temp], &tga->data[(i + j) * tga->channels], tga->channels);
                index_to_temp += sizeof(word);
            }

            i += rle_id;
        }
    }

    char *tmp = realloc(tga->data, data_size);
    if (tmp)
    {
        tga->data = tmp;
    }

    return true;
}

static bool read_bw_rle(tga_image *tga, const tga_func_def *func_def)
{
    int bytes = tga->channels == 4 ? sizeof(word) : sizeof(byte);
    int pixels = tga->width * tga->height;
    int data_size = pixels * tga->channels;
    int rle_size = pixels * bytes + pixels;
    int index_to_temp = data_size;

    tga->data = (byte *)malloc(data_size + rle_size);
    if (!tga->data)
        return false;

    if (!func_def->read_file(&tga->data[index_to_temp], sizeof(byte), rle_size, func_def->file))
        return false;

    for (unsigned int i = 0; i < tga->width * tga->height;)
    {
        byte rle_id = tga->data[index_to_temp];
        index_to_temp++;

        // Run-length packet
        if (rle_id & 0x80)
        {
            rle_id -= 127;

            while (rle_id)
            {
                bw_to_rgb(&tga->data[index_to_temp], &tga->data[i * tga->channels], tga->channels);

                i++;
                rle_id--;
            }

            index_to_temp += bytes;
        }
        // Raw packet
        else
        {
            rle_id++;

            for (int j = 0; j < rle_id; j++)
            {
                bw_to_rgb(&tga->data[index_to_temp], &tga->data[(i + j) * tga->channels], tga->channels);
                index_to_temp += bytes;
            }

            i += rle_id;
        }
    }

    char *tmp = realloc(tga->data, data_size);
    if (tmp)
    {
        tga->data = tmp;
    }

    return true;
}

bool load_tga_ext(const char *filename, tga_image *tga, tga_func_def *func_def)
{
    if (!tga || !filename || !func_def)
        return false;

    byte header[18];

    byte id_length = 0;
    byte color_map_type = 0;
    byte image_type = 0;
    word x_origin = 0;
    word y_origin = 0;
    word width = 0;
    word height = 0;
    byte bits_per_pixel = 0;

    byte *color_data = NULL;
    int color_channels = 0;
    bool success = false;

    func_def->file = func_def->open_file(filename, "rb", func_def->file);
    if (!func_def->file)
        return false;

    if (!func_def->read_file(&header, sizeof(header), 1, func_def->file))
        return false;

    image_type = header[2];

    if (image_type == TGA_TYPE_NO_IMAGE)
    {
        func_def->close_file(func_def->file);
        return false;
    }

    id_length = header[0];
    color_map_type = header[1];
    x_origin = (word)header[9] << 8 | (word)header[8];
    y_origin = (word)header[11] << 8 | (word)header[10];
    width = (word)header[13] << 8 | (word)header[12];
    height = (word)header[15] << 8 | (word)header[14];
    bits_per_pixel = header[16];

    // Skip optional image ID field
    if (id_length)
        func_def->seek_file(func_def->file, id_length, SEEK_CUR);

    // Load color map data if exists
    if (color_map_type)
    {
        word first_entry_index = (word)header[4] << 8 | (word)header[3];
        word color_map_length = (word)header[6] << 8 | (word)header[5];
        byte color_map_entry_size = header[7];
        color_channels = (color_map_entry_size / 8);
        int palette_size = color_map_length * color_channels;

        color_data = (byte *)malloc(color_map_length * color_channels);
        if (!color_data)
        {
            func_def->close_file(func_def->file);
            return false;
        }

        if (func_def->read_file(color_data, sizeof(byte), palette_size, func_def->file) != palette_size)
        {
            free(color_data);
            func_def->close_file(func_def->file);
            return false;
        }
    }

    tga->channels = 0;
    tga->width = width;
    tga->height = height;

    if (image_type == TGA_TYPE_MAPPED || image_type == TGA_TYPE_MAPPED_RLE)
    {
        tga->channels = color_channels;
    }
    else if (image_type == TGA_TYPE_RGB || image_type == TGA_TYPE_RGB_RLE)
    {
        if (bits_per_pixel == 32 || bits_per_pixel == 16)
            tga->channels = 4;
        else
            tga->channels = 3;
    }
    else if (image_type == TGA_TYPE_BW || image_type == TGA_TYPE_BW_RLE)
    {
        if (bits_per_pixel == 16)
            tga->channels = 4;
        else
            tga->channels = 3;
    }

    // Color-mapped image
    if (image_type == TGA_TYPE_MAPPED)
    {
        if (bits_per_pixel == 8)
            success = read_mapped(tga, &color_data, func_def);
    }
    // True-color image
    else if (image_type == TGA_TYPE_RGB)
    {
        if (bits_per_pixel == 24 || bits_per_pixel == 32)
            success = read_rgb(tga, func_def);
        else if (bits_per_pixel == 15 || bits_per_pixel == 16)
            success = read_rgb16(tga, func_def);
    }
    // Black and white image
    else if (image_type == TGA_TYPE_BW)
    {
        if (bits_per_pixel == 16 || bits_per_pixel == 8)
            success = read_bw(tga, func_def);
    }
    // Run-length encoded color-mapped image
    else if (image_type == TGA_TYPE_MAPPED_RLE)
    {
        if (bits_per_pixel == 8)
            success = read_mapped_rle(tga, &color_data, func_def);
    }
    // Run-length encoded true-color image
    else if (image_type == TGA_TYPE_RGB_RLE)
    {
        if (bits_per_pixel == 24 || bits_per_pixel == 32)
            success = read_rgb_rle(tga, func_def);
        else if (bits_per_pixel == 15 || bits_per_pixel == 16)
            success = read_rgb16_rle(tga, func_def);
    }
    // Run-length encoded black and white image
    else if (image_type == TGA_TYPE_BW_RLE)
    {
        if (bits_per_pixel == 16)
            success = read_bw_rle(tga, func_def);
    }

    func_def->close_file(func_def->file);

    if (success)
    {
        if (x_origin)
            flip_tga_horizontally(tga);

        if (y_origin)
            flip_tga_vertically(tga);
    }
    else
    {
        free_tga(tga);
    }

    if (image_type == TGA_TYPE_MAPPED || image_type == TGA_TYPE_MAPPED_RLE)
        free(color_data);

#if defined(_WIN64) || defined(_WIN32)
    if (saved_locale)
    {
        setlocale(LC_ALL, saved_locale);
        free(saved_locale);
        saved_locale = NULL;
    }
#endif

    return success;
}

void free_tga(tga_image *tga)
{
    if (!tga)
        return;

    if (tga->data)
        free(tga->data);

    memset(tga, 0, sizeof(tga_image));
}

bool save_tga(const char *filename, tga_image *tga, const tga_type type)
{
    tga_func_def func_def;

    func_def.open_file = fopen_wrapper;
    func_def.write_file = fwrite;
    func_def.close_file = fclose;

    return save_tga_ext(filename, tga, type, &func_def);
}

static int generate_palette(const tga_image *tga, int size, byte **palette_data, byte **color_data, const tga_func_def *func_def)
{
    int palette_size = 0;

    *palette_data = (byte *)malloc(tga->width * tga->height * tga->channels);
    if (!*palette_data)
        return 0;

    *color_data = (byte *)malloc(tga->width * tga->height);
    if (!*color_data)
    {
        free(*palette_data);
        return 0;
    }

    for (int i = 0, pixel = 0; i < size; i += tga->channels, pixel++)
    {
        bool found = false;

        for (unsigned int j = 0, color = 0; j < palette_size * tga->channels; j += tga->channels, color++)
        {
            if (memcmp(&tga->data[i], &(*palette_data)[j], tga->channels) != 0)
                continue;

            (*color_data)[pixel] = color;
            found = true;
            break;
        }

        if (!found)
        {
            memcpy(&(*palette_data)[palette_size * tga->channels], &tga->data[i], tga->channels);
            (*color_data)[pixel] = palette_size;
            palette_size++;

            // Supports only 256 colors
            if (palette_size > 256)
            {
                free(*palette_data);
                free(*color_data);
                func_def->close_file(func_def->file);
                return 0;
            }
        }
    }

    // RGB to BGR
    for (unsigned int j = 0, color = 0; j < palette_size * tga->channels; j += tga->channels)
        swap_byte(&(*palette_data)[j], &(*palette_data)[j + 2]);

    return palette_size;
}

static bool write_mapped(const tga_image *tga, const byte *palette_data, const byte *color_data, int palette_size, const tga_func_def *func_def)
{
    size_t pixels = tga->width * tga->height;

    if (func_def->write_file((byte *)palette_data, sizeof(byte), palette_size, func_def->file) != palette_size)
        return false;

    if (func_def->write_file((byte *)color_data, sizeof(byte), pixels, func_def->file) != pixels)
        return false;

    return true;
}

static bool write_rgb(const tga_image *tga, int size, const tga_func_def *func_def)
{
    bool success = true;

    byte *data = (byte *)malloc(size);
    if (!data)
        return false;

    for (int i = 0; i < size; i += tga->channels)
        rgb_to_bgr(&tga->data[i], &data[i], tga->channels);

    if (func_def->write_file(data, sizeof(byte), size, func_def->file) != size)
        success = false;

    free(data);
    return success;
}

static bool write_rgb16(const tga_image *tga, int size, const tga_func_def *func_def)
{
    bool success = true;
    int image_size = tga->width * tga->height;

    word *data = (word *)malloc(image_size * sizeof(word));
    if (!data)
        return false;

    for (int i = 0, j = 0; i < size; i += tga->channels, j++)
        rgb_to_rgb16(&tga->data[i], &data[j], tga->channels);

    if (func_def->write_file(data, sizeof(word), image_size, func_def->file) != image_size)
        success = false;

    free(data);
    return success;
}

static bool write_bw(const tga_image *tga, int size, int bits, const tga_func_def *func_def)
{
    bool success = true;
    int image_size = tga->width * tga->height;
    int bytes = (bits == 16) ? sizeof(word) : sizeof(byte);

    byte *data = (byte *)malloc(image_size * sizeof(word));
    if (!data)
        return false;

    for (int i = 0, j = 0; i < size; i += tga->channels, j += bytes)
        rgb_to_bw(&tga->data[i], &data[j], tga->channels, bytes);

    if (func_def->write_file(data, sizeof(byte), image_size * bytes, func_def->file) != image_size * bytes)
        success = false;

    free(data);
    return success;
}

static int write_rle(const tga_image *tga, const byte *data, int channels, int index, byte *rle)
{
    int duplicates = 0;
    int different = 0;

    unsigned int row_size = tga->width * tga->channels;
    unsigned int end_row = index + (row_size - (index) % row_size);

    for (unsigned int j = index; j < end_row; j += channels)
    {
        // Duplicate pixels
        if (!different)
        {
            if (j + channels < end_row && memcmp(&data[j], &data[j + channels], channels) == 0)
            {
                // A packet cannot contain more than 128 pixels
                if (duplicates + 1 < 128)
                {
                    duplicates++;
                    continue;
                }
            }

            // Write a run-length packet
            if (duplicates)
            {
                (*rle) = duplicates;
                (*rle) |= 1 << 7;
                return (*rle) - 127;
            }
        }

        // A packet cannot contain more than 128 pixels
        if (different + 1 < 128 && j + channels < end_row)
        {
            if (memcmp(&data[j], &data[j + channels], channels) != 0)
            {
                different++;
                continue;
            }
            else
            {
                different--;
            }
        }

        // Write a raw packet
        (*rle) = different;
        return -(*rle) - 1;
    }

    return 0;
}

static bool write_mapped_rle(tga_image *tga, const byte *palette_data, const byte *color_data, int palette_size, const tga_func_def *func_def)
{
    if (func_def->write_file((byte *)palette_data, sizeof(byte), palette_size, func_def->file) != palette_size)
        return false;

    bool success = true;
    int data_size = 0;
    int size = tga->width * tga->height;

    byte *data = (byte *)malloc(size * 2);
    if (!data)
        return false;

    for (int i = 0, n; i < size; i += n)
    {
        if (!(n = write_rle(tga, color_data, sizeof(byte), i, &data[data_size])))
        {
            free(data);
            return false;
        }

        data_size++;

        if (n > 0)
        {
            data[data_size] = color_data[i];
            data_size++;
        }
        else
        {
            n = -n;

            memcpy(&data[data_size], &color_data[i], n);
            data_size += n;
        }
    }

    if (func_def->write_file(data, sizeof(byte), data_size, func_def->file) != data_size)
        success = false;

    free(data);
    return success;
}

static bool write_rgb_rle(const tga_image *tga, int size, const tga_func_def *func_def)
{
    bool success = true;
    int data_size = 0;

    byte *data = (byte *)malloc(size + tga->width * tga->height);
    if (!data)
        return false;

    for (int i = 0, n; i < size; i += n * tga->channels)
    {
        if (!(n = write_rle(tga, tga->data, tga->channels, i, &data[data_size])))
        {
            free(data);
            return false;
        }

        data_size++;

        if (n > 0)
        {
            rgb_to_bgr(&tga->data[i], &data[data_size], tga->channels);
            data_size += tga->channels;
        }
        else
        {
            n = -n;

            for (int j = 0; j < n; j++)
            {
                rgb_to_bgr(&tga->data[i + j * tga->channels], &data[data_size], tga->channels);
                data_size += tga->channels;
            }
        }
    }

    if (func_def->write_file(data, sizeof(byte), data_size, func_def->file) != data_size)
        success = false;

    free(data);
    return success;
}

static bool write_rgb16_rle(const tga_image *tga, int size, const tga_func_def *func_def)
{
    bool success = true;
    int data_size = 0;

    byte *data = (byte *)malloc(tga->width * tga->height * sizeof(word) + tga->width * tga->height);
    if (!data)
        return false;

    for (int i = 0, n; i < size; i += n * tga->channels)
    {
        if (!(n = write_rle(tga, tga->data, tga->channels, i, &data[data_size])))
        {
            free(data);
            return false;
        }

        data_size++;

        if (n > 0)
        {
            rgb_to_rgb16(&tga->data[i], (word *)&data[data_size], tga->channels);
            data_size += sizeof(word);
        }
        else
        {
            n = -n;

            for (int j = 0; j < n; j++)
            {
                rgb_to_rgb16(&tga->data[i + j * tga->channels], (word *)&data[data_size], tga->channels);
                data_size += sizeof(word);
            }
        }
    }

    if (func_def->write_file(data, sizeof(byte), data_size, func_def->file) != data_size)
        success = false;

    free(data);
    return success;
}

static bool write_bw_rle(const tga_image *tga, int size, int bits, const tga_func_def *func_def)
{
    bool success = true;
    int bytes = (bits == 16) ? sizeof(word) : sizeof(byte);
    int data_size = 0;

    byte *data = (byte *)malloc(tga->width * tga->height * bytes + tga->width * tga->height);
    if (!data)
        return false;

    for (int i = 0, n; i < size; i += n * tga->channels)
    {
        if (!(n = write_rle(tga, tga->data, tga->channels, i, &data[data_size])))
        {
            free(data);
            return false;
        }

        data_size++;

        if (n > 0)
        {
            rgb_to_bw(&tga->data[i], &data[data_size], tga->channels, bytes);
            data_size += bytes;
        }
        else
        {
            n = -n;

            for (int j = 0; j < n; j++)
            {
                rgb_to_bw(&tga->data[i + j * tga->channels], &data[data_size], tga->channels, bytes);
                data_size += bytes;
            }
        }
    }

    if (func_def->write_file(data, sizeof(byte), data_size, func_def->file) != data_size)
        success = false;

    free(data);
    return success;
}

bool save_tga_ext(const char *filename, tga_image *tga, tga_type type, tga_func_def *func_def)
{
    if (!filename || !tga || !tga->data)
        return false;

    byte image_type;
    byte bits;
    int size = tga->width * tga->height * tga->channels;
    bool success = false;

    byte color_map_type = 0;
    word first_entry_index = 0;
    word color_map_length = 0;
    byte color_map_entry_size = 0;
    byte *palette_data = NULL;
    byte *color_data = NULL;
    int palette_size = 0;

    func_def->file = func_def->open_file(filename, "wb", func_def->file);
    if (!func_def->file)
        return false;

    // Generate color palette
    if (type == TGA_MAPPED || type == TGA_MAPPED_RLE)
    {
        if (!(color_map_length = generate_palette(tga, size, &palette_data, &color_data, func_def)))
        {
            func_def->close_file(func_def->file);
            return false;
        }

        color_map_type = 1;
        first_entry_index = 0;
        color_map_entry_size = tga->channels * 8;
        palette_size = color_map_length * tga->channels;
    }

    if (type == TGA_MAPPED)
        image_type = TGA_TYPE_MAPPED;
    else if (type == TGA_MAPPED_RLE)
        image_type = TGA_TYPE_MAPPED_RLE;
    else if (type == TGA_RGB)
        image_type = TGA_TYPE_RGB;
    else if (type == TGA_RGB_RLE)
        image_type = TGA_TYPE_RGB_RLE;
    else if (type == TGA_RGB16)
        image_type = TGA_TYPE_RGB;
    else if (type == TGA_RGB16_RLE)
        image_type = TGA_TYPE_RGB_RLE;
    else if (type == TGA_BW)
        image_type = TGA_TYPE_BW;
    else if (type == TGA_BW_RLE)
        image_type = TGA_TYPE_BW_RLE;
    else if (type == TGA_BW8)
        image_type = TGA_TYPE_BW;
    else if (type == TGA_BW8_RLE)
        image_type = TGA_TYPE_BW_RLE;

    if (type == TGA_MAPPED || type == TGA_MAPPED_RLE)
        bits = 8;
    else if (type == TGA_RGB || type == TGA_RGB_RLE)
        bits = tga->channels * 8;
    else if (type == TGA_RGB16 || type == TGA_RGB16_RLE)
        bits = tga->channels == 4 ? 16 : 15;
    else if (type == TGA_BW || type == TGA_BW_RLE)
        bits = 16;
    else if (type == TGA_BW8 || type == TGA_BW8_RLE)
        bits = 8;

    byte header[18] = { 0, color_map_type, image_type,
                      (byte)first_entry_index % 256,
                      (byte)first_entry_index / 256,
                      (byte)color_map_length % 256,
                      (byte)color_map_length / 256,
                      color_map_entry_size, 0, 0, 0, 0,
                      (byte)(tga->width % 256),
                      (byte)(tga->width / 256),
                      (byte)(tga->height % 256),
                      (byte)(tga->height / 256),
                      bits, 0 };

    if (!func_def->write_file(header, sizeof(header), 1, func_def->file))
    {
        func_def->close_file(func_def->file);
        return false;
    }

    if (type == TGA_MAPPED)
        success = write_mapped(tga, palette_data, color_data, palette_size, func_def);
    else if (type == TGA_RGB)
        success = write_rgb(tga, size, func_def);
    else if (type == TGA_RGB16)
        success = write_rgb16(tga, size, func_def);
    else if (type == TGA_BW || type == TGA_BW8)
        success = write_bw(tga, size, bits, func_def);
    else if (type == TGA_MAPPED_RLE)
        success = write_mapped_rle(tga, palette_data, color_data, palette_size, func_def);
    else if (type == TGA_RGB_RLE)
        success = write_rgb_rle(tga, size, func_def);
    else if (type == TGA_RGB16_RLE)
        success = write_rgb16_rle(tga, size, func_def);
    else if (type == TGA_BW_RLE || type == TGA_BW8_RLE)
        success = write_bw_rle(tga, size, bits, func_def);

    if (type == TGA_MAPPED || type == TGA_MAPPED_RLE)
    {
        free(palette_data);
        free(color_data);
    }

#if defined(_WIN64) || defined(_WIN32)
    if (saved_locale)
    {
        setlocale(LC_ALL, saved_locale);
        free(saved_locale);
        saved_locale = NULL;
    }
#endif

    func_def->close_file(func_def->file);
    return success;
}

#if defined(_WIN64) || defined(_WIN32)
static void *wfopen_wrapper(const char *filename, const char *mode, const void *stream)
{
    size_t size = strlen(filename) + 1;

    wchar_t wfilename[512];
    size_t num_of_characters;
    mbstowcs_s(&num_of_characters, wfilename, size, filename, size - 1);

    wchar_t wmode[64];
    size_t num_of_characters2;
    mbstowcs_s(&num_of_characters2, wmode, size, mode, size - 1);

    return _wfopen(wfilename, wmode);
}

bool wload_tga(const wchar_t *filename, tga_image *tga)
{
    tga_func_def func_def;

    func_def.open_file = wfopen_wrapper;
    func_def.read_file = fread;
    func_def.seek_file = fseek;
    func_def.close_file = fclose;

    return wload_tga_ext(filename, tga, &func_def);
}

bool wload_tga_ext(const wchar_t *filename, tga_image *tga, tga_func_def *func_def)
{
    saved_locale = setlocale(LC_ALL, NULL);
    saved_locale = _strdup(saved_locale);
    setlocale(LC_ALL, ".UTF8");

    char buf[1024];
    size_t num_of_characters;
    wcstombs_s(&num_of_characters, buf, sizeof(buf), filename, sizeof(buf) - 1);

    return load_tga_ext(buf, tga, func_def);
}

bool wsave_tga(const wchar_t *filename, tga_image *tga, tga_type type)
{
    tga_func_def func_def;

    func_def.open_file = wfopen_wrapper;
    func_def.write_file = fwrite;
    func_def.close_file = fclose;

    return wsave_tga_ext(filename, tga, type, &func_def);
}

bool wsave_tga_ext(const wchar_t *filename, tga_image *tga, tga_type type, tga_func_def *func_def)
{
    saved_locale = setlocale(LC_ALL, NULL);
    saved_locale = _strdup(saved_locale);
    setlocale(LC_ALL, ".UTF8");
    
    char buf[1024];
    size_t num_of_characters;
    wcstombs_s(&num_of_characters, buf, sizeof(buf), filename, sizeof(buf) - 1);

    return save_tga_ext(buf, tga, type, func_def);
}

#endif
