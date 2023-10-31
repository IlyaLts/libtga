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

static rgb_bgr_convert(byte *origin, byte *dest, int channels)
{
    // Do not reorder the following code below as the order is very important

    // Alpha
    if (channels == 4)
        dest[3] = origin[3];

    dest[2] = origin[0];    // B
    dest[1] = origin[1];    // G
    dest[0] = origin[2];    // R
}

static rgb_to_pixel(byte *data, word *pixel, int channels)
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

static pixel_to_rgb(word pixel, byte *data, int channels)
{
    data[0] = ((pixel >> 10) & 0x1f) << 3;      // R
    data[1] = ((pixel >> 5) & 0x1f) << 3;       // G
    data[2] = (pixel & 0x1f) << 3;              // B

    // Alpha
    if (channels == 4)
        data[3] = pixel & 0x8000 ? 255 : 0;
}

static void rgb_to_bw(byte *data, word *pixel, int channels)
{
    *pixel = (data[0] + data[1] + data[2]) / 3;

    // Alpha
    if (channels == 4)
        *pixel |= data[3] << 8;
    else
        *pixel |= 255 << 8;
}

static void bw_to_rgb(word *pixel, byte *data, int channels)
{
    // Do not reorder the following code below as the order is very important

    // Alpha
    data[3] = *pixel >> 8;

    // Colors
    data[2] = *pixel & 0xff;
    data[1] = *pixel & 0xff;
    data[0] = *pixel & 0xff;
}

void flip_tga_horizontally(tga_image *ptga)
{
    for (unsigned int i = 0; i < ptga->height; i++)
    {
        for (unsigned int j = 0; j < ptga->width / 2; j++)
        {
            for (unsigned int k = 0; k < ptga->channels; k++)
            {
                swap_byte(&ptga->data[((i * ptga->width) + j) * ptga->channels + k],
                          &ptga->data[((i * ptga->width) + (ptga->width - j - 1)) * ptga->channels + k]);
            }
        }
    }
}

void flip_tga_vertically(tga_image *ptga)
{
    for (unsigned int i = 0; i < ptga->width * ptga->channels; i++)
    {
        for (unsigned int j = 0; j < ptga->height / 2; j++)
        {
            swap_byte(&ptga->data[i + ptga->width * ptga->channels * j],
                      &ptga->data[i + ptga->width * ptga->channels * (ptga->height - j - 1)]);
        }
    }
}

static void *fopen_wrapper(const char *filename, char const *mode, void *stream)
{
    return fopen(filename, mode);
}

bool load_tga(const char *filename, tga_image *ptga)
{
    tga_filefunc_def ptga_filefunc_def;
    
    ptga_filefunc_def.open_file = fopen_wrapper;
    ptga_filefunc_def.read_file = fread;
    ptga_filefunc_def.seek_file = fseek;
    ptga_filefunc_def.close_file = fclose;
    
    return load_tga2(filename, ptga, &ptga_filefunc_def);
}

bool load_tga2(const char *filename, tga_image *ptga, tga_filefunc_def *ptga_filefunc_def)
{
    if (!ptga || !filename || !ptga_filefunc_def)
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

    ptga_filefunc_def->file = ptga_filefunc_def->open_file(filename, "rb", ptga_filefunc_def->file);
    if (!ptga_filefunc_def->file) return false;

    ptga_filefunc_def->read_file(&header, sizeof(header), 1, ptga_filefunc_def->file);

    image_type = header[2];

    if (image_type == TGA_TYPE_NO_IMAGE)
    {
        ptga_filefunc_def->close_file(ptga_filefunc_def->file);
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
    if (id_length) ptga_filefunc_def->seek_file(ptga_filefunc_def->file, id_length, SEEK_CUR);

    // Load color map data if exists
    if (color_map_type)
    {
        word first_entry_index = (word)header[4] << 8 | (word)header[3];
        word color_map_length = (word)header[6] << 8 | (word)header[5];
        byte color_map_entry_size = header[7];

        color_channels = (color_map_entry_size / 8);
        color_data = (byte *)malloc(color_map_length * color_channels);
        ptga_filefunc_def->read_file(color_data, sizeof(byte), color_map_length * color_channels, ptga_filefunc_def->file);
    }

    // Color-mapped image
    if (image_type == TGA_TYPE_MAPPED)
    {
        if (bits_per_pixel == 8)
        {
            channels = color_channels;
            ptga->data = (byte *)malloc(width * height * channels);
            ptga_filefunc_def->read_file(ptga->data, sizeof(byte), width * height, ptga_filefunc_def->file);

            for (int i = width * height - 1; i >= 0; i--)
                rgb_bgr_convert(&color_data[ptga->data[i] * color_channels], &ptga->data[i * channels], channels);

            success = true;
        }
    }
    // True-color image
    else if (image_type == TGA_TYPE_RGB)
    {
        if (bits_per_pixel == 24 || bits_per_pixel == 32)
        {
            channels = bits_per_pixel / 8;
            ptga->data = (byte *)malloc(width * height * channels);
            ptga_filefunc_def->read_file(ptga->data, sizeof(byte), width * height * channels, ptga_filefunc_def->file);

            for (int y = 0; y < height; y++)
            {
                byte *pLine = &(ptga->data[width * channels * y]);

                for (int i = 0; i < width * channels; i += channels)
                    swap_byte(&pLine[i], &pLine[i + 2]);
            }

            success = true;
        }
        else if (bits_per_pixel == 15 || bits_per_pixel == 16)
        {
            if (bits_per_pixel == 16)
                channels = 4;
            else
                channels = 3;

            ptga->data = (byte *)malloc(width * height * channels);
            ptga_filefunc_def->read_file(ptga->data, sizeof(word), width * height, ptga_filefunc_def->file);

            for (int i = width * height - 1; i >= 0; i--)
            {
                word pixel = (word)ptga->data[i * sizeof(word) + 1] << 8 |
                             (word)ptga->data[i * sizeof(word)];

                pixel_to_rgb(pixel, &ptga->data[i * channels], channels);
            }

            success = true;
        }
    }
    // Black and white image
    else if (image_type == TGA_TYPE_BW)
    {
        if (bits_per_pixel == 16)
        {
            channels = 4;
            ptga->data = (byte *)malloc(width * height * channels);
            ptga_filefunc_def->read_file(ptga->data, sizeof(byte), width * height * sizeof(word), ptga_filefunc_def->file);

            for (int i = width * height - 1; i >= 0; i--)
                bw_to_rgb((word *)&ptga->data[i * sizeof(word)], &ptga->data[i * channels], channels);

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
            ptga->data = (byte *)malloc(width * height * channels);

            for (int i = 0; i < width * height;)
            {
                ptga_filefunc_def->read_file(&rle_id, sizeof(byte), 1, ptga_filefunc_def->file);

                // Run-length packet
                if (rle_id & 0x80)
                {
                    byte pixel = 0;
                    rle_id -= 127;
                    ptga_filefunc_def->read_file(&pixel, sizeof(byte), 1, ptga_filefunc_def->file);

                    while (rle_id)
                    {
                        rgb_bgr_convert(&color_data[pixel * color_channels], &ptga->data[i * channels], channels);

                        i++;
                        rle_id--;
                    }
                }
                // Raw packet
                else
                {
                    rle_id++;
                    ptga_filefunc_def->read_file(&ptga->data[i * channels], sizeof(byte), rle_id, ptga_filefunc_def->file);

                    for (int j = rle_id - 1; j >= 0; j--)
                        rgb_bgr_convert(&color_data[ptga->data[i * channels + j] * color_channels], &ptga->data[(i + j) * channels], channels);

                    i += rle_id;
                }
            }

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
            ptga->data = (byte *)malloc(width * height * channels);

            for (int i = 0; i < width * height;)
            {
                ptga_filefunc_def->read_file(&rle_id, sizeof(byte), 1, ptga_filefunc_def->file);

                // Run-length packet
                if (rle_id & 0x80)
                {
                    byte colors[4];
                    rle_id -= 127;
                    ptga_filefunc_def->read_file(&colors, sizeof(byte), channels, ptga_filefunc_def->file);

                    while (rle_id)
                    {
                        rgb_bgr_convert(&colors[0], &ptga->data[i * channels], channels);

                        i++;
                        rle_id--;
                    }
                }
                // Raw packet
                else
                {
                    rle_id++;
                    ptga_filefunc_def->read_file(&ptga->data[i * channels], sizeof(byte), rle_id * channels, ptga_filefunc_def->file);

                    while (rle_id)
                    {
                        swap_byte(&ptga->data[i * channels], &ptga->data[i * channels + 2]);

                        i++;
                        rle_id--;
                    }
                }
            }

            success = true;
        }
        else if (bits_per_pixel == 15 || bits_per_pixel == 16)
        {
            if (bits_per_pixel == 16)
                channels = 4;
            else
                channels = 3;

            ptga->data = (byte *)malloc(width * height * channels);

            for (int i = 0; i < width * height;)
            {
                ptga_filefunc_def->read_file(&rle_id, sizeof(byte), 1, ptga_filefunc_def->file);

                // Run-length packet
                if (rle_id & 0x80)
                {
                    word pixel = 0;
                    rle_id -= 127;
                    ptga_filefunc_def->read_file(&pixel, sizeof(word), 1, ptga_filefunc_def->file);

                    while (rle_id)
                    {
                        pixel_to_rgb(pixel, &ptga->data[i * channels], channels);

                        i++;
                        rle_id--;
                    }
                }
                // Raw packet
                else
                {
                    rle_id++;
                    ptga_filefunc_def->read_file(&ptga->data[i * channels], sizeof(byte), rle_id * sizeof(word), ptga_filefunc_def->file);

                    for (int j = rle_id - 1; j >= 0; j--)
                    {
                        word pixel = (word)ptga->data[i * channels + j * sizeof(word) + 1] << 8 |
                                     (word)ptga->data[i * channels + j * sizeof(word)];

                        pixel_to_rgb(pixel, &ptga->data[(i + j) * channels], channels);
                    }

                    i += rle_id;
                }
            }

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
            ptga->data = (byte *)malloc(width * height * channels);

            for (int i = 0; i < width * height;)
            {
                ptga_filefunc_def->read_file(&rle_id, sizeof(byte), 1, ptga_filefunc_def->file);

                // Run-length packet
                if (rle_id & 0x80)
                {
                    word pixel;
                    rle_id -= 127;
                    ptga_filefunc_def->read_file(&pixel, sizeof(word), 1, ptga_filefunc_def->file);

                    while (rle_id)
                    {
                        bw_to_rgb(&pixel, &ptga->data[i * channels], channels);

                        i++;
                        rle_id--;
                    }
                }
                // Raw packet
                else
                {
                    rle_id++;
                    ptga_filefunc_def->read_file(&ptga->data[i * channels], sizeof(byte), rle_id * sizeof(word), ptga_filefunc_def->file);

                    for (int j = rle_id - 1; j >= 0; j--)
                        bw_to_rgb((word *)&ptga->data[i * channels + j * sizeof(word)], &ptga->data[(i + j) * channels], channels);

                    i += rle_id;
                }
            }

            success = true;
        }
    }

    if (!success)
    {
        ptga_filefunc_def->close_file(ptga_filefunc_def->file);
        free(color_data);
        return false;
    }

    ptga->channels = channels;
    ptga->width = width;
    ptga->height = height;

    if (x_origin)
        flip_tga_horizontally(ptga);

    if (y_origin)
        flip_tga_vertically(ptga);

    ptga_filefunc_def->close_file(ptga_filefunc_def->file);
    free(color_data);
    return true;
}

void free_tga(tga_image *ptga)
{
    if (ptga && ptga->data)
        free(ptga->data);
}

bool write_tga(const char *filename, tga_image *ptga, tga_type type)
{
    tga_filefunc_def ptga_filefunc_def;

    ptga_filefunc_def.open_file = fopen_wrapper;
    ptga_filefunc_def.write_file = fwrite;
    ptga_filefunc_def.close_file = fclose;

    return write_tga2(filename, ptga, type, &ptga_filefunc_def);
}

bool write_tga2(const char *filename, tga_image *ptga, tga_type type, tga_filefunc_def *ptga_filefunc_def)
{
    byte image_type;
    byte bits;
    int size = ptga->width * ptga->height * ptga->channels;

    byte color_map_type = 0;
    word first_entry_index = 0;
    word color_map_length = 0;
    byte color_map_entry_size = 0;
    byte *palette_data = NULL;
    byte *color_data = NULL;
    int palette_size = 0;

    if (!filename || !ptga)
        return false;
    
    ptga_filefunc_def->file = ptga_filefunc_def->open_file(filename, "wb", ptga_filefunc_def->file);
    if (!ptga_filefunc_def->file) return false;

    // Generate color palette
    if (type == TGA_MAPPED || type == TGA_MAPPED_RLE)
    {
        palette_data = (byte *)malloc(ptga->width * ptga->height * ptga->channels);
        color_data = (byte *)malloc(ptga->width * ptga->height);

        for (int i = 0, pixel = 0; i < size; i += ptga->channels, pixel++)
        {
            bool found = false;

            for (unsigned int j = 0, color = 0; j < palette_size * ptga->channels; j += ptga->channels, color++)
            {
                if (memcmp(&ptga->data[i], &palette_data[j], ptga->channels) != 0)
                    continue;

                color_data[pixel] = color;
                found = true;
                break;
            }

            if (!found)
            {
                memcpy(&palette_data[palette_size * ptga->channels], &ptga->data[i], ptga->channels);
                color_data[pixel] = palette_size;
                palette_size++;
            }
        }

        // RGB to BGR
        for (unsigned int j = 0, color = 0; j < palette_size * ptga->channels; j += ptga->channels)
            swap_byte(&palette_data[j], &palette_data[j + 2]);

        // Supports only 256 colors
        if (palette_size > 256)
        {
            free(palette_data);
            free(color_data);
            ptga_filefunc_def->close_file(ptga_filefunc_def->file);
            return false;
        }

        color_map_type = 1;
        first_entry_index = 0;
        color_map_length = palette_size;
        color_map_entry_size = ptga->channels * 8;
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
        bits = ptga->channels * 8;
    else if (type == TGA_RGB16 || type == TGA_RGB16_RLE)
        bits = ptga->channels == 4 ? 16 : 15;
    else if (type == TGA_BW || type == TGA_BW_RLE)
        bits = 16;

    byte header[18] = {0, color_map_type, image_type,
                      (unsigned char)first_entry_index % 256,
                      (unsigned char)first_entry_index / 256,
                      (unsigned char)color_map_length % 256,
                      (unsigned char)color_map_length / 256,
                      color_map_entry_size, 0, 0, 0, 0,
                      (unsigned char)(ptga->width % 256),
                      (unsigned char)(ptga->width / 256),
                      (unsigned char)(ptga->height % 256),
                      (unsigned char)(ptga->height / 256),
                      bits,
                      0};

    ptga_filefunc_def->write_file(header, sizeof(header), 1, ptga_filefunc_def->file);

    if (type == TGA_MAPPED)
    {
        ptga_filefunc_def->write_file(palette_data, sizeof(byte), palette_size * ptga->channels, ptga_filefunc_def->file);
        ptga_filefunc_def->write_file(color_data, sizeof(byte), ptga->width * ptga->height, ptga_filefunc_def->file);

        free(palette_data);
        free(color_data);
    }
    else if (type == TGA_RGB)
    {
        for (int i = 0; i < size; i += ptga->channels)
        {
            unsigned char colors[4];
            rgb_bgr_convert(&ptga->data[i], &colors[0], ptga->channels);
            ptga_filefunc_def->write_file(colors, sizeof(byte), ptga->channels, ptga_filefunc_def->file);
        }
    }
    else if (type == TGA_RGB16)
    {
        for (int i = 0; i < size; i += ptga->channels)
        {
            word pixel;
            rgb_to_pixel(&ptga->data[i], &pixel, ptga->channels);
            ptga_filefunc_def->write_file(&pixel, sizeof(word), 1, ptga_filefunc_def->file);
        }
    }
    else if (type == TGA_BW)
    {
        for (int i = 0; i < size; i += ptga->channels)
        {
            word pixel;
            rgb_to_bw(&ptga->data[i], &pixel, ptga->channels);
            ptga_filefunc_def->write_file(&pixel, sizeof(pixel), 1, ptga_filefunc_def->file);
        }
    }
    else if (type == TGA_MAPPED_RLE)
    {
        ptga_filefunc_def->write_file(palette_data, sizeof(byte), palette_size *ptga->channels, ptga_filefunc_def->file);

        int duplicates = 0;
        int different = 0;

        for (unsigned int i = 0; i < ptga->height; i++)
        {
            for (unsigned int j = 0; j < ptga->width; j++)
            {
                int index = i * ptga->width + j;

                // Count duplicate pixels
                if (!different)
                {
                    if (j + 1 < ptga->width && color_data[index] == color_data[index + 1])
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

                    ptga_filefunc_def->write_file(&rle_id, sizeof(byte), 1, ptga_filefunc_def->file);
                    ptga_filefunc_def->write_file(&color_data[index], sizeof(byte), 1, ptga_filefunc_def->file);

                    duplicates = 0;
                    continue;
                }

                // Count different pixels
                if (!duplicates)
                {
                    // A packet cannot contain more than 128 pixels
                    if (different + 1 < 128 && j + 1 < ptga->width)
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
                ptga_filefunc_def->write_file(&rle_id, sizeof(rle_id), 1, ptga_filefunc_def->file);
                ptga_filefunc_def->write_file(&color_data[index - different], sizeof(byte), different + 1, ptga_filefunc_def->file);

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

        for (unsigned int i = 0; i < ptga->height; i++)
        {
            for (unsigned int j = 0; j < ptga->width; j++)
            {
                int index = (i * ptga->width + j) * ptga->channels;

                // Count duplicate pixels
                if (!different)
                {
                    // A packet cannot contain more than 128 pixels
                    if (j + 1 < ptga->width && duplicates + 1 < 128)
                    {
                        if (memcmp(&ptga->data[index], &ptga->data[index + ptga->channels], ptga->channels) == 0)
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
                    rgb_bgr_convert(&ptga->data[index], &colors[0], ptga->channels);

                    ptga_filefunc_def->write_file(&rle_id, sizeof(byte), 1, ptga_filefunc_def->file);
                    ptga_filefunc_def->write_file(&colors, sizeof(byte), ptga->channels, ptga_filefunc_def->file);

                    duplicates = 0;
                    continue;
                }

                // Count different pixels
                if (!duplicates)
                {
                    // A packet cannot contain more than 128 pixels
                    if (different + 1 < 128 && j + 1 < ptga->width)
                    {
                        if (memcmp(&ptga->data[index], &ptga->data[index + ptga->channels], ptga->channels) != 0)
                        {
                            different++;
                            continue;
                        }
                        else
                        {
                            different--;
                            j--;
                            index -= ptga->channels;
                        }
                    }
                }

                // Write a raw packet
                byte rle_id = different;
                ptga_filefunc_def->write_file(&rle_id, sizeof(rle_id), 1, ptga_filefunc_def->file);

                for (int k = 0; k < different + 1; k++)
                {
                    byte colors[4];
                    rgb_bgr_convert(&ptga->data[index - (different - k) * ptga->channels], &colors[0], ptga->channels);

                    ptga_filefunc_def->write_file(&colors, sizeof(byte), ptga->channels, ptga_filefunc_def->file);
                }

                different = 0;
            }
        }
    }
    else if (type == TGA_RGB16_RLE)
    {
        int duplicates = 0;
        int different = 0;

        for (unsigned int i = 0; i < ptga->height; i++)
        {
            for (unsigned int j = 0; j < ptga->width; j++)
            {
                int index = (i * ptga->width + j) * ptga->channels;

                // Count duplicate pixels
                if (!different)
                {
                    // A packet cannot contain more than 128 pixels
                    if (j + 1 < ptga->width && duplicates + 1 < 128)
                    {
                        if (memcmp(&ptga->data[index], &ptga->data[index + ptga->channels], ptga->channels) == 0)
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
                    rgb_to_pixel(&ptga->data[index], &pixel, ptga->channels);

                    ptga_filefunc_def->write_file(&rle_id, sizeof(byte), 1, ptga_filefunc_def->file);
                    ptga_filefunc_def->write_file(&pixel, sizeof(word), 1, ptga_filefunc_def->file);

                    duplicates = 0;
                    continue;
                }

                // Count different pixels
                if (!duplicates)
                {
                    // A packet cannot contain more than 128 pixels
                    if (different + 1 < 128 && j + 1 < ptga->width)
                    {
                        if (memcmp(&ptga->data[index], &ptga->data[index + ptga->channels], ptga->channels) != 0)
                        {
                            different++;
                            continue;
                        }
                        else
                        {
                            different--;
                            j--;
                            index -= ptga->channels;
                        }
                    }
                }

                // Write a raw packet
                byte rle_id = different;
                ptga_filefunc_def->write_file(&rle_id, sizeof(rle_id), 1, ptga_filefunc_def->file);

                for (int k = 0; k < different + 1; k++)
                {
                    word pixel;
                    rgb_to_pixel(&ptga->data[index - (different - k) * ptga->channels], &pixel, ptga->channels);
                    ptga_filefunc_def->write_file(&pixel, sizeof(word), 1, ptga_filefunc_def->file);
                }

                different = 0;
            }
        }
    }
    else if (type == TGA_BW_RLE)
    {
        int bw_size = ptga->width * ptga->height;
        word *bw_data = (word *)malloc(bw_size * sizeof(word));

        int duplicates = 0;
        int different = 0;

        // Convert RGB image to BW image
        for (int i = 0, j = 0; i < size; i += ptga->channels, j++)
            rgb_to_bw(&ptga->data[i], &bw_data[j], ptga->channels);

        for (unsigned int i = 0; i < ptga->height; i++)
        {
            for (unsigned int j = 0; j < ptga->width; j++)
            {
                int index = i * ptga->width + j;

                // Count duplicate pixels
                if (!different)
                {
                    if (j + 1 < ptga->width && bw_data[index] == bw_data[index + 1])
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

                    ptga_filefunc_def->write_file(&rle_id, sizeof(byte), 1, ptga_filefunc_def->file);
                    ptga_filefunc_def->write_file(&bw_data[index], sizeof(word), 1, ptga_filefunc_def->file);

                    duplicates = 0;
                    continue;
                }

                // Count different pixels
                if (!duplicates)
                {
                    // A packet cannot contain more than 128 pixels
                    if (different + 1 < 128 && j + 1 < ptga->width)
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
                ptga_filefunc_def->write_file(&rle_id, sizeof(rle_id), 1, ptga_filefunc_def->file);
                ptga_filefunc_def->write_file(&bw_data[index - different], sizeof(word), different + 1, ptga_filefunc_def->file);

                different = 0;
            }
        }

        free(bw_data);
    }

    ptga_filefunc_def->close_file(ptga_filefunc_def->file);
    return true;
}
