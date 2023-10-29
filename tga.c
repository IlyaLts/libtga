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

static void rgb2bw(byte *data, word *pixel, int channels)
{
    *pixel = (data[0] + data[1] + data[2]) / 3;

    // Alpha
    if (channels == 4)
        *pixel |= data[3] << 8;
    else
        *pixel |= 255 << 8;
}

static void bw2rgb(word *pixel, byte *data, int channels)
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

bool load_tga(const char *filename, tga_image *ptga)
{
    tga_filefunc_def ptga_filefunc_def;
    
    ptga_filefunc_def.open_file = fopen;
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

    void *file = ptga_filefunc_def->open_file(filename, "rb");
    if (!file) return false;

    ptga_filefunc_def->read_file(&header, sizeof(header), 1, file);

    image_type = header[2];

    if (image_type == TGA_TYPE_NO_IMAGE)
        return false;

    id_length = header[0];
    color_map_type = header[1];
    x_origin = (word)header[9] << 8 | (word)header[8];
    y_origin = (word)header[11] << 8 | (word)header[10];
    width = (word)header[13] << 8 | (word)header[12];
    height = (word)header[15] << 8 | (word)header[14];
    bits_per_pixel = header[16];

    // Skip optional image ID field
    if (id_length) ptga_filefunc_def->seek_file(file, id_length, SEEK_CUR);

    // Load color map data if exists
    if (color_map_type)
    {
        word first_entry_index = (word)header[4] << 8 | (word)header[3];
        word color_map_length = (word)header[6] << 8 | (word)header[5];
        byte color_map_entry_size = header[7];

        color_channels = (color_map_entry_size / 8);
        color_data = (byte *)malloc(color_map_length * color_channels);
        ptga_filefunc_def->read_file(color_data, sizeof(byte), color_map_length * color_channels, file);
    }

    // Color-mapped image
    if (image_type == TGA_TYPE_MAPPED)
    {
        if (bits_per_pixel == 8)
        {
            channels = color_channels;
            ptga->data = (byte *)malloc(width * height * channels);
            ptga_filefunc_def->read_file(ptga->data, sizeof(byte), width * height, file);

            for (int i = width * height - 1; i >= 0; i--)
            {
                // Do not reorder the following code below as the order is very important

                if (channels == 4)
                {
                    ptga->data[i * channels + 3] = color_data[ptga->data[i] * color_channels + 3];
                }

                ptga->data[i * channels + 2] = color_data[ptga->data[i] * color_channels + 0]; // B
                ptga->data[i * channels + 1] = color_data[ptga->data[i] * color_channels + 1]; // G
                ptga->data[i * channels + 0] = color_data[ptga->data[i] * color_channels + 2]; // R
            }

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
            ptga_filefunc_def->read_file(ptga->data, sizeof(byte), width * height * channels, file);

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
            ptga_filefunc_def->read_file(ptga->data, sizeof(word), width * height, file);

            for (int i = width * height - 1; i >= 0; i--)
            {
                word pixel = (word)ptga->data[i * sizeof(word) + 1] << 8 |
                             (word)ptga->data[i * sizeof(word)];

                ptga->data[i * channels + 0] = ((pixel >> 10) & 0x1f) << 3;     // R
                ptga->data[i * channels + 1] = ((pixel >> 5) & 0x1f) << 3;      // G
                ptga->data[i * channels + 2] = (pixel & 0x1f) << 3;             // B

                // Alpha
                if (channels == 4)
                {
                    ptga->data[i * channels + 3] = pixel & 0x8000 ? 255 : 0;
                }
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
            ptga_filefunc_def->read_file(ptga->data, sizeof(byte), width * height * sizeof(word), file);

            for (int i = width * height - 1; i >= 0; i--)
                bw2rgb((word *)&ptga->data[i * sizeof(word)], &ptga->data[i * channels], channels);

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
                ptga_filefunc_def->read_file(&rle_id, sizeof(byte), 1, file);

                // Run-length packet
                if (rle_id & 0x80)
                {
                    byte pixel = 0;
                    rle_id -= 127;
                    ptga_filefunc_def->read_file(&pixel, sizeof(byte), 1, file);

                    while (rle_id)
                    {
                        // Alpha
                        if (channels == 4)
                        {
                            ptga->data[i * channels + 3] = color_data[pixel * color_channels + 3];
                        }

                        ptga->data[i * channels + 2] = color_data[pixel * color_channels + 0]; // B
                        ptga->data[i * channels + 1] = color_data[pixel * color_channels + 1]; // G
                        ptga->data[i * channels + 0] = color_data[pixel * color_channels + 2]; // R

                        i++;
                        rle_id--;
                    }
                }
                // Raw packet
                else
                {
                    rle_id++;
                    ptga_filefunc_def->read_file(&ptga->data[i * channels], sizeof(byte), rle_id, file);

                    for (int j = rle_id - 1; j >= 0; j--)
                    {
                        // Alpha
                        if (channels == 4)
                        {
                            ptga->data[(i + j) * channels + 3] = color_data[ptga->data[i * channels + j] * color_channels + 3];
                        }

                        // Colors
                        ptga->data[(i + j) * channels + 2] = color_data[ptga->data[i * channels + j] * color_channels + 0]; // B
                        ptga->data[(i + j) * channels + 1] = color_data[ptga->data[i * channels + j] * color_channels + 1]; // G
                        ptga->data[(i + j) * channels + 0] = color_data[ptga->data[i * channels + j] * color_channels + 2]; // R
                    }

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
                ptga_filefunc_def->read_file(&rle_id, sizeof(byte), 1, file);

                // Run-length packet
                if (rle_id & 0x80)
                {
                    byte colors[4];
                    rle_id -= 127;
                    ptga_filefunc_def->read_file(&colors, sizeof(byte), channels, file);

                    while (rle_id)
                    {
                        ptga->data[i * channels + 0] = colors[2];
                        ptga->data[i * channels + 1] = colors[1];
                        ptga->data[i * channels + 2] = colors[0];

                        if (bits_per_pixel == 32)
                        {
                            ptga->data[i * channels + 3] = colors[3];
                        }

                        i++;
                        rle_id--;
                    }
                }
                // Raw packet
                else
                {
                    rle_id++;
                    ptga_filefunc_def->read_file(&ptga->data[i * channels], sizeof(byte), rle_id * channels, file);

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
                ptga_filefunc_def->read_file(&rle_id, sizeof(byte), 1, file);

                // Run-length packet
                if (rle_id & 0x80)
                {
                    word pixel = 0;
                    rle_id -= 127;
                    ptga_filefunc_def->read_file(&pixel, sizeof(word), 1, file);

                    while (rle_id)
                    {
                        // R
                        ptga->data[i * channels + 0] = ((pixel >> 10) & 0x1f) << 3;
                        ptga->data[i * channels + 0] += ptga->data[i * channels + 0] >> 5;

                        // G
                        ptga->data[i * channels + 1] = ((pixel >> 5) & 0x1f) << 3;
                        ptga->data[i * channels + 1] += ptga->data[i * channels + 1] >> 5;

                        // B
                        ptga->data[i * channels + 2] = (pixel & 0x1f) << 3;
                        ptga->data[i * channels + 2] += ptga->data[i * channels + 2] >> 5;

                        // Alpha
                        if (channels == 4)
                        {
                            ptga->data[i * channels + 3] = pixel & 0x8000 ? 0xff : 0;
                        }

                        i++;
                        rle_id--;
                    }
                }
                // Raw packet
                else
                {
                    rle_id++;
                    ptga_filefunc_def->read_file(&ptga->data[i * channels], sizeof(byte), rle_id * sizeof(word), file);

                    for (int j = rle_id - 1; j >= 0; j--)
                    {
                        word pixel = (word)ptga->data[i * channels + j * sizeof(word) + 1] << 8 |
                                     (word)ptga->data[i * channels + j * sizeof(word)];

                        // R
                        ptga->data[(i + j) * channels + 0] = ((pixel >> 10) & 0x1f) << 3;
                        ptga->data[(i + j) * channels + 0] += ptga->data[(i + j) * channels + 0] >> 5;

                        // G
                        ptga->data[(i + j) * channels + 1] = ((pixel >> 5) & 0x1f) << 3;
                        ptga->data[(i + j) * channels + 1] += ptga->data[(i + j) * channels + 1] >> 5;

                        // B
                        ptga->data[(i + j) * channels + 2] = (pixel & 0x1f) << 3;
                        ptga->data[(i + j) * channels + 2] += ptga->data[(i + j) * channels + 2] >> 5;

                        // Alpha
                        if (channels == 4)
                        {
                            ptga->data[(i + j) * channels + 3] = pixel & 0x8000 ? 0xff : 0;
                        }
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
                ptga_filefunc_def->read_file(&rle_id, sizeof(byte), 1, file);

                // Run-length packet
                if (rle_id & 0x80)
                {
                    word pixel;
                    rle_id -= 127;
                    ptga_filefunc_def->read_file(&pixel, sizeof(word), 1, file);

                    while (rle_id)
                    {
                        bw2rgb(&pixel, &ptga->data[i * channels], channels);
                        i++;
                        rle_id--;
                    }
                }
                // Raw packet
                else
                {
                    rle_id++;
                    ptga_filefunc_def->read_file(&ptga->data[i * channels], sizeof(byte), rle_id * sizeof(word), file);

                    for (int j = rle_id - 1; j >= 0; j--)
                    {
                        bw2rgb((word *)&ptga->data[i * channels + j * sizeof(word)], &ptga->data[(i + j) * channels], channels);
                    }

                    i += rle_id;
                }
            }

            success = true;
        }
    }

    if (!success)
    {
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

    ptga_filefunc_def->close_file(file);
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
    byte image_type;
    byte bits;
    int size = ptga->width * ptga->height * ptga->channels;

    byte color_map_type = 0;
    word first_entry_index = 0;
    word color_map_length = 0;
    byte color_map_entry_size = 0;
    byte *palette_data = NULL;
    byte *color_data = NULL;
    int colors = 0;

    if (!filename || !ptga)
        return false;
    
    FILE *file = fopen(filename, "wb");
    if (!file) return false;

    // Generate color palette
    if (type == TGA_MAPPED || type == TGA_MAPPED_RLE)
    {
        palette_data = (byte *)malloc(ptga->width * ptga->height * ptga->channels);
        color_data = (byte *)malloc(ptga->width * ptga->height);

        for (int i = 0, pixel = 0; i < size; i += ptga->channels, pixel++)
        {
            bool found = false;

            for (unsigned int j = 0, color = 0; j < colors * ptga->channels; j += ptga->channels, color++)
            {
                if (ptga->data[i + 2] != palette_data[j + 0]) continue;
                if (ptga->data[i + 1] != palette_data[j + 1]) continue;
                if (ptga->data[i + 0] != palette_data[j + 2]) continue;

                // Alpha
                if (ptga->channels == 4)
                {
                    if (ptga->data[i + 3] != palette_data[j + 3])
                        continue;
                }

                color_data[pixel] = color;
                found = true;
                break;
            }

            if (!found)
            {
                palette_data[colors * ptga->channels + 2] = ptga->data[i + 0]; // R
                palette_data[colors * ptga->channels + 1] = ptga->data[i + 1]; // G
                palette_data[colors * ptga->channels + 0] = ptga->data[i + 2]; // B

                // Alpha
                if (ptga->channels == 4)
                {
                    palette_data[colors * ptga->channels + 3] = ptga->data[i + 3];
                }

                color_data[pixel] = colors;
                colors++;
            }
        }

        // Supports only 256 colors
        if (colors > 256)
        {
            free(palette_data);
            free(color_data);
            return false;
        }

        color_map_type = 1;
        first_entry_index = 0;
        color_map_length = colors;
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

    fwrite(header, sizeof(header), 1, file);

    if (type == TGA_MAPPED)
    {
        fwrite(palette_data, sizeof(byte), colors * ptga->channels, file);
        fwrite(color_data, sizeof(byte), ptga->width * ptga->height, file);

        free(palette_data);
        free(color_data);
    }
    else if (type == TGA_RGB)
    {
        for (int i = 0; i < size; i += ptga->channels)
        {
            unsigned char pixel[4];

            pixel[0] = ptga->data[i + 2];
            pixel[1] = ptga->data[i + 1];
            pixel[2] = ptga->data[i + 0];

            if (ptga->channels == 4)
            {
                pixel[3] = ptga->data[i + 3];
            }

            fwrite(pixel, sizeof(byte), ptga->channels, file);
        }
    }
    else if (type == TGA_RGB16)
    {
        for (int i = 0; i < size; i += ptga->channels)
        {
            word pixel = 0;

            pixel |= (ptga->data[i + 0] >> 3) << 10;    // R
            pixel |= (ptga->data[i + 1] >> 3) << 5;     // G
            pixel |= (ptga->data[i + 2] >> 3);          // B

            // Alpha
            if (ptga->channels == 4)
                pixel |= ptga->data[i + 3] ? 1 << 15 : 0 << 15;
            else
                pixel |= 1 << 15;

            fwrite(&pixel, sizeof(word), 1, file);
        }
    }
    else if (type == TGA_BW)
    {
        for (int i = 0; i < size; i += ptga->channels)
        {
            word pixel;
            rgb2bw(&ptga->data[i], &pixel, ptga->channels);
            fwrite(&pixel, sizeof(pixel), 1, file);
        }
    }
    else if (type == TGA_MAPPED_RLE)
    {
        fwrite(palette_data, sizeof(byte), colors *ptga->channels, file);

        int duplicates = 0;
        int different = 0;

        for (unsigned int i = 0; i < ptga->height; i++)
        {
            for (unsigned int j = 0; j < ptga->width; j++)
            {
                int index = i * ptga->width + j;

                // Count duplicate colors
                if (!different)
                {
                    if (j + 1 < ptga->width && color_data[index] == color_data[index + 1])
                    {
                        // A packet cannot contain more than 128 colors
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
                    byte rle_id = duplicates + 1;
                    rle_id |= 1 << 7;
                    rle_id--;

                    fwrite(&rle_id, sizeof(byte), 1, file);
                    fwrite(&color_data[index], sizeof(byte), 1, file);

                    duplicates = 0;
                    continue;
                }

                // Count different colors
                if (!duplicates)
                {
                    // A packet cannot contain more than 128 colors
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
                byte rle_id = different + 1;
                rle_id--;

                fwrite(&rle_id, sizeof(rle_id), 1, file);
                fwrite(&color_data[index - different], sizeof(byte), different + 1, file);

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

                // Count duplicate colors
                if (!different)
                {
                    if (j + 1 < ptga->width)
                    {
                        bool duplicate = true;

                        if (ptga->data[index] != ptga->data[index + ptga->channels])
                            duplicate = false;
                        if (ptga->data[index + 1] != ptga->data[index + ptga->channels + 1])
                            duplicate = false;
                        if (ptga->data[index + 2] != ptga->data[index + ptga->channels + 2])
                            duplicate = false;

                        if (ptga->channels == 4)
                        {
                            if (ptga->data[index + 3] != ptga->data[index + ptga->channels + 3])
                                duplicate = false;
                        }

                        // A packet cannot contain more than 128 colors
                        if (duplicate && duplicates + 1 < 128)
                        {
                            duplicates++;
                            continue;
                        }
                    }
                }

                // Write a run-length packet
                if (duplicates)
                {
                    byte rle_id = duplicates + 1;
                    rle_id |= 1 << 7;
                    rle_id--;

                    byte pixel[4];

                    pixel[0] = ptga->data[index + 2];
                    pixel[1] = ptga->data[index + 1];
                    pixel[2] = ptga->data[index + 0];

                    if (ptga->channels == 4)
                        pixel[3] = ptga->data[index + 3];

                    fwrite(&rle_id, sizeof(byte), 1, file);
                    fwrite(&pixel, sizeof(byte), ptga->channels, file);

                    duplicates = 0;
                    continue;
                }

                // Count different colors
                if (!duplicates)
                {
                    // A packet cannot contain more than 128 colors
                    if (different + 1 < 128 && j + 1 < ptga->width)
                    {
                        bool duplicate = true;

                        if (ptga->data[index] != ptga->data[index + ptga->channels])
                            duplicate = false;
                        if (ptga->data[index + 1] != ptga->data[index + ptga->channels + 1])
                            duplicate = false;
                        if (ptga->data[index + 2] != ptga->data[index + ptga->channels + 2])
                            duplicate = false;

                        if (ptga->channels == 4)
                        {
                            if (ptga->data[index + 3] != ptga->data[index + ptga->channels + 3])
                                duplicate = false;
                        }

                        if (!duplicate)
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
                byte rle_id = different + 1;
                rle_id--;
                fwrite(&rle_id, sizeof(rle_id), 1, file);

                for (int k = 0; k < different + 1; k++)
                {
                    byte pixel[4];

                    pixel[0] = ptga->data[index - (different - k) * ptga->channels + 2];
                    pixel[1] = ptga->data[index - (different - k) * ptga->channels + 1];
                    pixel[2] = ptga->data[index - (different - k) * ptga->channels + 0];

                    if (ptga->channels == 4)
                        pixel[3] = ptga->data[index - (different - k) * ptga->channels + 3];

                    fwrite(&pixel, sizeof(byte), ptga->channels, file);
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

                // Count duplicate colors
                if (!different)
                {
                    if (j + 1 < ptga->width)
                    {
                        bool duplicate = true;

                        if (ptga->data[index] != ptga->data[index + ptga->channels])
                            duplicate = false;
                        if (ptga->data[index + 1] != ptga->data[index + ptga->channels + 1])
                            duplicate = false;
                        if (ptga->data[index + 2] != ptga->data[index + ptga->channels + 2])
                            duplicate = false;

                        if (ptga->channels == 4)
                        {
                            if (ptga->data[index + 3] != ptga->data[index + ptga->channels + 3])
                                duplicate = false;
                        }

                        // A packet cannot contain more than 128 colors
                        if (duplicate && duplicates + 1 < 128)
                        {
                            duplicates++;
                            continue;
                        }
                    }
                }

                // Write a run-length packet
                if (duplicates)
                {
                    byte rle_id = duplicates + 1;
                    rle_id |= 1 << 7;
                    rle_id--;

                    word pixel = 0;

                    pixel |= ptga->data[index + 0] >> 3 << 10;
                    pixel |= ptga->data[index + 1] >> 3 << 5;
                    pixel |= ptga->data[index + 2] >> 3;

                    // Alpha
                    if (ptga->channels == 4)
                        pixel |= ptga->data[index + 3] ? 0x8000 : 0;
                    else
                        pixel |= 1 << 15;

                    fwrite(&rle_id, sizeof(byte), 1, file);
                    fwrite(&pixel, sizeof(word), 1, file);

                    duplicates = 0;
                    continue;
                }

                // Count different colors
                if (!duplicates)
                {
                    // A packet cannot contain more than 128 colors
                    if (different + 1 < 128 && j + 1 < ptga->width)
                    {
                        bool duplicate = true;

                        if (ptga->data[index] != ptga->data[index + ptga->channels])
                            duplicate = false;
                        if (ptga->data[index + 1] != ptga->data[index + ptga->channels + 1])
                            duplicate = false;
                        if (ptga->data[index + 2] != ptga->data[index + ptga->channels + 2])
                            duplicate = false;

                        if (ptga->channels == 4)
                        {
                            if (ptga->data[index + 3] != ptga->data[index + ptga->channels + 3])
                                duplicate = false;
                        }

                        if (!duplicate)
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
                byte rle_id = different + 1;
                rle_id--;
                fwrite(&rle_id, sizeof(rle_id), 1, file);

                for (int k = 0; k < different + 1; k++)
                {
                    word pixel = 0;

                    pixel |= ptga->data[index - (different - k) * ptga->channels + 0] >> 3 << 10;
                    pixel |= ptga->data[index - (different - k) * ptga->channels + 1] >> 3 << 5;
                    pixel |= ptga->data[index - (different - k) * ptga->channels + 2] >> 3;

                    // Alpha
                    if (ptga->channels == 4)
                        pixel |= ptga->data[index - (different - k) * ptga->channels + 3] ? 0x8000 : 0;
                    else
                        pixel |= 1 << 15;

                    fwrite(&pixel, sizeof(word), 1, file);
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
            rgb2bw(&ptga->data[i], &bw_data[j], ptga->channels);

        for (unsigned int i = 0; i < ptga->height; i++)
        {
            for (unsigned int j = 0; j < ptga->width; j++)
            {
                int index = i * ptga->width + j;

                // Count duplicate colors
                if (!different)
                {
                    if (j + 1 < ptga->width && bw_data[index] == bw_data[index + 1])
                    {
                        // A packet cannot contain more than 128 colors
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
                    byte rle_id = duplicates + 1;
                    rle_id |= 1 << 7;
                    rle_id--;

                    fwrite(&rle_id, sizeof(byte), 1, file);
                    fwrite(&bw_data[index], sizeof(word), 1, file);

                    duplicates = 0;
                    continue;
                }

                // Count different colors
                if (!duplicates)
                {
                    // A packet cannot contain more than 128 colors
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
                byte rle_id = different + 1;
                rle_id--;

                fwrite(&rle_id, sizeof(rle_id), 1, file);
                fwrite(&bw_data[index - different], sizeof(word), different + 1, file);

                different = 0;
            }
        }

        free(bw_data);
    }

    fclose(file);
    return true;
}