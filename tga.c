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

static void swap_byte(byte *a, byte *b)
{
    byte temp = *a;
    *a = *b;
    *b = temp;
}

static void rgb_bgr_invert(const byte *origin, byte *dest, int channels)
{
    // Do not reorder the following code below as the order is very important

    // Alpha
    if (channels == 4)
        dest[3] = origin[3];

    dest[2] = origin[0];    // B
    dest[1] = origin[1];    // G
    dest[0] = origin[2];    // R
}

static void rgb_to_pixel(const byte *data, word *pixel, int channels)
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

static void pixel_to_rgb(const word *pixel, byte *data, int channels)
{
    data[0] = ((*pixel >> 10) & 0x1f) << 3;      // R
    data[1] = ((*pixel >> 5) & 0x1f) << 3;       // G
    data[2] = (*pixel & 0x1f) << 3;              // B

    // Alpha
    if (channels == 4)
        data[3] = *pixel & 0x8000 ? 255 : 0;
}

static void rgb_to_bw(const byte *data, word *pixel, int channels)
{
    *pixel = (data[0] + data[1] + data[2]) / 3;

    // Alpha
    if (channels == 4)
        *pixel |= data[3] << 8;
    else
        *pixel |= 255 << 8;
}

static void bw_to_rgb(const word *pixel, byte *data, int channels)
{
    // Do not reorder the following code below as the order is very important

    // Alpha
    if (channels == 4)
        data[3] = *pixel >> 8;

    // Colors
    data[2] = *pixel & 0xff;
    data[1] = *pixel & 0xff;
    data[0] = *pixel & 0xff;
}

void flip_tga_horizontally(tga_image *tga)
{
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
    for (unsigned int i = 0; i < tga->width * tga->channels; i++)
    {
        for (unsigned int j = 0; j < tga->height / 2; j++)
        {
            swap_byte(&tga->data[i + tga->width * tga->channels * j],
                      &tga->data[i + tga->width * tga->channels * (tga->height - j - 1)]);
        }
    }
}

static void *fopen_wrapper(const char *filename, char const *mode, void *stream)
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

bool load_tga_ext(const char *filename, tga_image *tga, tga_func_def *func_def)
{
    if (!tga || !filename || !func_def)
        return false;

    bool success = false;

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
    int channels = 0;

    func_def->file = func_def->open_file(filename, "rb", func_def->file);
    if (!func_def->file) return false;

    func_def->read_file(&header, sizeof(header), 1, func_def->file);

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
    if (id_length) func_def->seek_file(func_def->file, id_length, SEEK_CUR);

    // Load color map data if exists
    if (color_map_type)
    {
        word first_entry_index = (word)header[4] << 8 | (word)header[3];
        word color_map_length = (word)header[6] << 8 | (word)header[5];
        byte color_map_entry_size = header[7];

        color_channels = (color_map_entry_size / 8);
        color_data = (byte *)malloc(color_map_length * color_channels);
        func_def->read_file(color_data, sizeof(byte), color_map_length * color_channels, func_def->file);
    }

    // Color-mapped image
    if (image_type == TGA_TYPE_MAPPED)
    {
        if (bits_per_pixel == 8)
        {
            channels = color_channels;
            tga->data = (byte *)malloc(width * height * channels);
            func_def->read_file(tga->data, sizeof(byte), width * height, func_def->file);

            for (int i = width * height - 1; i >= 0; i--)
                rgb_bgr_invert(&color_data[tga->data[i] * color_channels], &tga->data[i * channels], channels);

            success = true;
        }
    }
    // True-color image
    else if (image_type == TGA_TYPE_RGB)
    {
        if (bits_per_pixel == 24 || bits_per_pixel == 32)
        {
            channels = bits_per_pixel / 8;
            tga->data = (byte *)malloc(width * height * channels);
            func_def->read_file(tga->data, sizeof(byte), width * height * channels, func_def->file);

            for (int y = 0; y < height; y++)
            {
                byte *pixel = &(tga->data[width * channels * y]);

                for (int i = 0; i < width * channels; i += channels)
                    swap_byte(&pixel[i], &pixel[i + 2]);
            }

            success = true;
        }
        else if (bits_per_pixel == 15 || bits_per_pixel == 16)
        {
            if (bits_per_pixel == 16)
                channels = 4;
            else
                channels = 3;

            tga->data = (byte *)malloc(width * height * channels);
            func_def->read_file(tga->data, sizeof(word), width * height, func_def->file);

            for (int i = width * height - 1; i >= 0; i--)
                pixel_to_rgb((word *)&tga->data[i * sizeof(word)], &tga->data[i * channels], channels);

            success = true;
        }
    }
    // Black and white image
    else if (image_type == TGA_TYPE_BW)
    {
        if (bits_per_pixel == 16)
        {
            channels = 4;
            tga->data = (byte *)malloc(width * height * channels);
            func_def->read_file(tga->data, sizeof(byte), width * height * sizeof(word), func_def->file);

            for (int i = width * height - 1; i >= 0; i--)
                bw_to_rgb((word *)&tga->data[i * sizeof(word)], &tga->data[i * channels], channels);

            success = true;
        }
    }
    // Run-length encoded color-mapped image
    else if (image_type == TGA_TYPE_MAPPED_RLE)
    {
        byte rle_id = 0;

        if (bits_per_pixel == 8)
        {
            channels = color_channels;

            int pixels = width * height;
            int data_size = pixels * channels;
            int rle_size = pixels * sizeof(byte) + pixels;
            int index_to_temp = data_size;

            tga->data = (byte *)malloc(data_size + rle_size);
            func_def->read_file(&tga->data[index_to_temp], sizeof(byte), rle_size, func_def->file);

            for (int i = 0; i < width * height;)
            {
                rle_id = tga->data[index_to_temp];
                index_to_temp++;

                // Run-length packet
                if (rle_id & 0x80)
                {
                    rle_id -= 127;

                    while (rle_id)
                    {
                        rgb_bgr_invert(&color_data[tga->data[index_to_temp] * color_channels], &tga->data[i * channels], channels);

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
                        rgb_bgr_invert(&color_data[tga->data[index_to_temp] * color_channels], &tga->data[(i + j) * channels], channels);
                        index_to_temp += sizeof(byte);
                    }

                    i += rle_id;
                }
            }

            realloc(tga->data, data_size);
            success = true;
        }
    }
    // Run-length encoded true-color image
    else if (image_type == TGA_TYPE_RGB_RLE)
    {
        byte rle_id = 0;

        if (bits_per_pixel == 24 || bits_per_pixel == 32)
        {
            channels = bits_per_pixel / 8;

            int data_size = width * height * channels;
            int rle_size = data_size + width * height;
            int index_to_temp = data_size;

            tga->data = (byte *)malloc(data_size + rle_size);
            func_def->read_file(&tga->data[index_to_temp], sizeof(byte), rle_size, func_def->file);
            
            for (int i = 0; i < width * height;)
            {
                rle_id = tga->data[index_to_temp];
                index_to_temp++;

                // Run-length packet
                if (rle_id & 0x80)
                {
                    rle_id -= 127;

                    while (rle_id)
                    {
                        rgb_bgr_invert(&tga->data[index_to_temp], &tga->data[i * channels], channels);

                        i++;
                        rle_id--;
                    }

                    index_to_temp += channels;
                }
                // Raw packet
                else
                {
                    rle_id++;

                    for (int j = 0; j < rle_id * channels; j++)
                    {
                        tga->data[i * channels + j] = tga->data[index_to_temp];
                        index_to_temp++;
                    }

                    while (rle_id)
                    {
                        swap_byte(&tga->data[i * channels], &tga->data[i * channels + 2]);

                        i++;
                        rle_id--;
                    }
                }
            }

            realloc(tga->data, data_size);
            success = true;
        }
        else if (bits_per_pixel == 15 || bits_per_pixel == 16)
        {
            if (bits_per_pixel == 16)
                channels = 4;
            else
                channels = 3;

            int pixels = width * height;
            int data_size = pixels * channels;
            int rle_size = pixels * sizeof(word) + pixels;
            int index_to_temp = data_size;

            tga->data = (byte *)malloc(data_size + rle_size);
            func_def->read_file(&tga->data[index_to_temp], sizeof(byte), rle_size, func_def->file);

            for (int i = 0; i < width * height;)
            {
                rle_id = tga->data[index_to_temp];
                index_to_temp++;

                // Run-length packet
                if (rle_id & 0x80)
                {
                    rle_id -= 127;

                    while (rle_id)
                    {
                        pixel_to_rgb((word *)&tga->data[index_to_temp], &tga->data[i * channels], channels);
                        
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
                        pixel_to_rgb((word *)&tga->data[index_to_temp], &tga->data[(i + j) * channels], channels);
                        index_to_temp += sizeof(word);
                    }

                    i += rle_id;
                }
            }

            realloc(tga->data, data_size);
            success = true;
        }
    }
    // Run-length encoded black and white image
    else if (image_type == TGA_TYPE_BW_RLE)
    {
        byte rle_id = 0;

        if (bits_per_pixel == 16)
        {
            channels = 4;

            int pixels = width * height;
            int data_size = pixels * channels;
            int rle_size = pixels * sizeof(word) + pixels;
            int index_to_temp = data_size;

            tga->data = (byte *)malloc(data_size + rle_size);
            func_def->read_file(&tga->data[index_to_temp], sizeof(byte), rle_size, func_def->file);

            for (int i = 0; i < width * height;)
            {
                rle_id = tga->data[index_to_temp];
                index_to_temp++;

                // Run-length packet
                if (rle_id & 0x80)
                {
                    rle_id -= 127;

                    while (rle_id)
                    {
                        bw_to_rgb((word *)&tga->data[index_to_temp], &tga->data[i * channels], channels);

                        i++;
                        rle_id--;
                    }

                    index_to_temp += sizeof(word);
                }
                // Raw packet
                else
                {
                    rle_id++;

                    for (int j = 0; j < rle_id; j++)
                    {
                        bw_to_rgb((word *)&tga->data[index_to_temp], &tga->data[(i + j) * channels], channels);
                        index_to_temp += sizeof(word);
                    }

                    i += rle_id;
                }
            }

            realloc(tga->data, data_size);
            success = true;
        }
    }

    if (!success)
    {
        func_def->close_file(func_def->file);
        free(color_data);
        return false;
    }

    tga->channels = channels;
    tga->width = width;
    tga->height = height;

    if (x_origin)
        flip_tga_horizontally(tga);

    if (y_origin)
        flip_tga_vertically(tga);

    func_def->close_file(func_def->file);
    free(color_data);
    return true;
}

void free_tga(tga_image *tga)
{
    if (tga && tga->data)
        free(tga->data);
}

bool save_tga(const char *filename, tga_image *tga, tga_type type)
{
    tga_func_def func_def;

    func_def.open_file = fopen_wrapper;
    func_def.write_file = fwrite;
    func_def.close_file = fclose;

    return save_tga_ext(filename, tga, type, &func_def);
}

bool save_tga_ext(const char *filename, tga_image *tga, tga_type type, tga_func_def *func_def)
{
    if (!filename || !tga)
        return false;

    byte image_type;
    byte bits;
    int size = tga->width * tga->height * tga->channels;

    byte color_map_type = 0;
    word first_entry_index = 0;
    word color_map_length = 0;
    byte color_map_entry_size = 0;
    byte *palette_data = NULL;
    byte *color_data = NULL;
    int palette_size = 0;
    
    func_def->file = func_def->open_file(filename, "wb", func_def->file);
    if (!func_def->file) return false;

    // Generate color palette
    if (type == TGA_MAPPED || type == TGA_MAPPED_RLE)
    {
        palette_data = (byte *)malloc(tga->width * tga->height * tga->channels);
        color_data = (byte *)malloc(tga->width * tga->height);

        for (int i = 0, pixel = 0; i < size; i += tga->channels, pixel++)
        {
            bool found = false;

            for (unsigned int j = 0, color = 0; j < palette_size * tga->channels; j += tga->channels, color++)
            {
                if (memcmp(&tga->data[i], &palette_data[j], tga->channels) != 0)
                    continue;

                color_data[pixel] = color;
                found = true;
                break;
            }

            if (!found)
            {
                memcpy(&palette_data[palette_size * tga->channels], &tga->data[i], tga->channels);
                color_data[pixel] = palette_size;
                palette_size++;
            }
        }

        // RGB to BGR
        for (unsigned int j = 0, color = 0; j < palette_size * tga->channels; j += tga->channels)
            swap_byte(&palette_data[j], &palette_data[j + 2]);

        // Supports only 256 colors
        if (palette_size > 256)
        {
            free(palette_data);
            free(color_data);
            func_def->close_file(func_def->file);
            return false;
        }

        color_map_type = 1;
        first_entry_index = 0;
        color_map_length = palette_size;
        color_map_entry_size = tga->channels * 8;
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

    if (type == TGA_MAPPED || type == TGA_MAPPED_RLE)
        bits = 8;
    else if (type == TGA_RGB || type == TGA_RGB_RLE)
        bits = tga->channels * 8;
    else if (type == TGA_RGB16 || type == TGA_RGB16_RLE)
        bits = tga->channels == 4 ? 16 : 15;
    else if (type == TGA_BW || type == TGA_BW_RLE)
        bits = 16;

    byte header[18] = {0, color_map_type, image_type,
                      (unsigned char)first_entry_index % 256,
                      (unsigned char)first_entry_index / 256,
                      (unsigned char)color_map_length % 256,
                      (unsigned char)color_map_length / 256,
                      color_map_entry_size, 0, 0, 0, 0,
                      (unsigned char)(tga->width % 256),
                      (unsigned char)(tga->width / 256),
                      (unsigned char)(tga->height % 256),
                      (unsigned char)(tga->height / 256),
                      bits,
                      0};

    func_def->write_file(header, sizeof(header), 1, func_def->file);

    if (type == TGA_MAPPED)
    {
        func_def->write_file(palette_data, sizeof(byte), palette_size * tga->channels, func_def->file);
        func_def->write_file(color_data, sizeof(byte), tga->width * tga->height, func_def->file);

        free(palette_data);
        free(color_data);
    }
    else if (type == TGA_RGB)
    {
        for (int i = 0; i < size; i += tga->channels)
        {
            unsigned char colors[4];
            rgb_bgr_invert(&tga->data[i], &colors[0], tga->channels);
            func_def->write_file(colors, sizeof(byte), tga->channels, func_def->file);
        }
    }
    else if (type == TGA_RGB16)
    {
        for (int i = 0; i < size; i += tga->channels)
        {
            word pixel;
            rgb_to_pixel(&tga->data[i], &pixel, tga->channels);
            func_def->write_file(&pixel, sizeof(word), 1, func_def->file);
        }
    }
    else if (type == TGA_BW)
    {
        for (int i = 0; i < size; i += tga->channels)
        {
            word pixel;
            rgb_to_bw(&tga->data[i], &pixel, tga->channels);
            func_def->write_file(&pixel, sizeof(pixel), 1, func_def->file);
        }
    }
    else if (type == TGA_MAPPED_RLE)
    {
        func_def->write_file(palette_data, sizeof(byte), palette_size *tga->channels, func_def->file);

        int duplicates = 0;
        int different = 0;

        for (unsigned int i = 0; i < tga->height; i++)
        {
            for (unsigned int j = 0; j < tga->width; j++)
            {
                int index = i * tga->width + j;

                // Count duplicate pixels
                if (!different)
                {
                    if (j + 1 < tga->width && color_data[index] == color_data[index + 1])
                    {
                        // A packet cannot contain more than 128 pixels
                        if (duplicates + 1 < 128)
                        {
                            duplicates++;
                            continue;
                        }
                    }
                }

                // Write a run-length packet
                if (duplicates)
                {
                    byte rle_id = duplicates;
                    rle_id |= 1 << 7;

                    func_def->write_file(&rle_id, sizeof(byte), 1, func_def->file);
                    func_def->write_file(&color_data[index], sizeof(byte), 1, func_def->file);

                    duplicates = 0;
                    continue;
                }

                // Count different pixels
                if (!duplicates)
                {
                    // A packet cannot contain more than 128 pixels
                    if (different + 1 < 128 && j + 1 < tga->width)
                    {
                        if (color_data[index] != color_data[index + 1])
                        {
                            different++;
                            continue;
                        }
                        else
                        {
                            different--;
                            j--;
                            index--;
                        }
                    }
                }

                // Write a raw packet
                byte rle_id = different;
                func_def->write_file(&rle_id, sizeof(rle_id), 1, func_def->file);
                func_def->write_file(&color_data[index - different], sizeof(byte), different + 1, func_def->file);

                different = 0;
            }
        }

        free(palette_data);
        free(color_data);
    }
    else if (type == TGA_RGB_RLE)
    {
        int duplicates = 0;
        int different = 0;

        for (unsigned int i = 0; i < tga->height; i++)
        {
            for (unsigned int j = 0; j < tga->width; j++)
            {
                int index = (i * tga->width + j) * tga->channels;

                // Count duplicate pixels
                if (!different)
                {
                    // A packet cannot contain more than 128 pixels
                    if (j + 1 < tga->width && duplicates + 1 < 128)
                    {
                        if (memcmp(&tga->data[index], &tga->data[index + tga->channels], tga->channels) == 0)
                        {
                            duplicates++;
                            continue;
                        }
                    }
                }

                // Write a run-length packet
                if (duplicates)
                {
                    byte rle_id = duplicates;
                    rle_id |= 1 << 7;

                    byte colors[4];
                    rgb_bgr_invert(&tga->data[index], &colors[0], tga->channels);

                    func_def->write_file(&rle_id, sizeof(byte), 1, func_def->file);
                    func_def->write_file(&colors, sizeof(byte), tga->channels, func_def->file);

                    duplicates = 0;
                    continue;
                }

                // Count different pixels
                if (!duplicates)
                {
                    // A packet cannot contain more than 128 pixels
                    if (different + 1 < 128 && j + 1 < tga->width)
                    {
                        if (memcmp(&tga->data[index], &tga->data[index + tga->channels], tga->channels) != 0)
                        {
                            different++;
                            continue;
                        }
                        else
                        {
                            different--;
                            j--;
                            index -= tga->channels;
                        }
                    }
                }

                // Write a raw packet
                byte rle_id = different;
                func_def->write_file(&rle_id, sizeof(rle_id), 1, func_def->file);

                for (int k = 0; k < different + 1; k++)
                {
                    byte colors[4];
                    rgb_bgr_invert(&tga->data[index - (different - k) * tga->channels], &colors[0], tga->channels);

                    func_def->write_file(&colors, sizeof(byte), tga->channels, func_def->file);
                }

                different = 0;
            }
        }
    }
    else if (type == TGA_RGB16_RLE)
    {
        int duplicates = 0;
        int different = 0;

        for (unsigned int i = 0; i < tga->height; i++)
        {
            for (unsigned int j = 0; j < tga->width; j++)
            {
                int index = (i * tga->width + j) * tga->channels;

                // Count duplicate pixels
                if (!different)
                {
                    // A packet cannot contain more than 128 pixels
                    if (j + 1 < tga->width && duplicates + 1 < 128)
                    {
                        if (memcmp(&tga->data[index], &tga->data[index + tga->channels], tga->channels) == 0)
                        {
                            duplicates++;
                            continue;
                        }
                    }
                }

                // Write a run-length packet
                if (duplicates)
                {
                    byte rle_id = duplicates;
                    rle_id |= 1 << 7;

                    word pixel;
                    rgb_to_pixel(&tga->data[index], &pixel, tga->channels);

                    func_def->write_file(&rle_id, sizeof(byte), 1, func_def->file);
                    func_def->write_file(&pixel, sizeof(word), 1, func_def->file);

                    duplicates = 0;
                    continue;
                }

                // Count different pixels
                if (!duplicates)
                {
                    // A packet cannot contain more than 128 pixels
                    if (different + 1 < 128 && j + 1 < tga->width)
                    {
                        if (memcmp(&tga->data[index], &tga->data[index + tga->channels], tga->channels) != 0)
                        {
                            different++;
                            continue;
                        }
                        else
                        {
                            different--;
                            j--;
                            index -= tga->channels;
                        }
                    }
                }

                // Write a raw packet
                byte rle_id = different;
                func_def->write_file(&rle_id, sizeof(rle_id), 1, func_def->file);

                for (int k = 0; k < different + 1; k++)
                {
                    word pixel;
                    rgb_to_pixel(&tga->data[index - (different - k) * tga->channels], &pixel, tga->channels);
                    func_def->write_file(&pixel, sizeof(word), 1, func_def->file);
                }

                different = 0;
            }
        }
    }
    else if (type == TGA_BW_RLE)
    {
        int bw_size = tga->width * tga->height;
        word *bw_data = (word *)malloc(bw_size * sizeof(word));

        int duplicates = 0;
        int different = 0;

        // Convert RGB image to BW image
        for (int i = 0, j = 0; i < size; i += tga->channels, j++)
            rgb_to_bw(&tga->data[i], &bw_data[j], tga->channels);

        for (unsigned int i = 0; i < tga->height; i++)
        {
            for (unsigned int j = 0; j < tga->width; j++)
            {
                int index = i * tga->width + j;

                // Count duplicate pixels
                if (!different)
                {
                    if (j + 1 < tga->width && bw_data[index] == bw_data[index + 1])
                    {
                        // A packet cannot contain more than 128 pixels
                        if (duplicates + 1 < 128)
                        {
                            duplicates++;
                            continue;
                        }
                    }
                }

                // Write a run-length packet
                if (duplicates)
                {
                    byte rle_id = duplicates;
                    rle_id |= 1 << 7;

                    func_def->write_file(&rle_id, sizeof(byte), 1, func_def->file);
                    func_def->write_file(&bw_data[index], sizeof(word), 1, func_def->file);

                    duplicates = 0;
                    continue;
                }

                // Count different pixels
                if (!duplicates)
                {
                    // A packet cannot contain more than 128 pixels
                    if (different + 1 < 128 && j + 1 < tga->width)
                    {
                        if (bw_data[index] != bw_data[index + 1])
                        {
                            different++;
                            continue;
                        }
                        else
                        {
                            different--;
                            j--;
                            index--;
                        }
                    }
                }

                // Write a raw packet
                byte rle_id = different;
                func_def->write_file(&rle_id, sizeof(rle_id), 1, func_def->file);
                func_def->write_file(&bw_data[index - different], sizeof(word), different + 1, func_def->file);

                different = 0;
            }
        }

        free(bw_data);
    }

    func_def->close_file(func_def->file);
    return true;
}
