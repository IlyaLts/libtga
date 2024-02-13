libtga
======
A library for loading, saving, and manipulating Targa (.tga) image files in a variety of formats, including uncompressed, run-length encoded, color-mapped, black-and-white, and 32-bit or 16-bit true-color images.

### Usage
```
#include "tga.h"
#include <stdio.h>

int main()
{
	tga_image image;
	
	if (!load_tga("image.tga", &image))
	{
		printf("Error: Failed to load TGA image.\n");
		return 1;
	}
	
	flip_tga_horizontally(&image);

	if (!save_tga("flipped.tga", &image, TGA_RGB))
	{
		printf("Error: Failed to save TGA image.\n");
		return 1;
	}

	free_tga(&image);

	return 0;
}
```

## Documentation
  
| TGA Types | Descriptions |
| --- | --- |
| TGA_MAPPED | Uncompressed, 8-bit color-mapped image. |
| TGA_RGB | Uncompressed, 24-bit or 32-bit true-color image. |
| TGA_RGB16 | Uncompressed, 15-bit or 16-bit true-color image. |
| TGA_BW | Uncompressed, 16-bit black-and-white image. |
| TGA_BW8 | Uncompressed, 8-bit black-and-white image. |
| TGA_MAPPED_RLE | Run-length encoded, 8-bit color-mapped image. |
| TGA_RGB_RLE | Run-length encoded, 24-bit or 32-bit true-color image. |
| TGA_RGB16_RLE | Run-length encoded, 15-bit or 16-bit true-color image. |
| TGA_BW_RLE | Run-length encoded, 16-bit black-and-white image. |
| TGA_BW8_RLE | Run-length encoded, 8-bit black-and-white image. |

| Functions | Descriptions |
| --- | --- |
| flip_tga_horizontally(tga_image *ptga) | Flips the TGA image horizontally. |
| flip_tga_vertically(tga_image *ptga) | Flips the TGA image vertically. |
| load_tga(const char *filename, tga_image *ptga) | Loads a TGA image from the specified file. |
| load_tga_ext(const char *filename, tga_image *tga, tga_func_def *func_def) | Loads a TGA image from the specified file using the custom file functions specified in the tga_func_def structure. |
| free_tga(tga_image *ptga) | Frees the memory allocated for the TGA image. |
| save_tga(const char *filename, tga_image *ptga, tga_type type) | Saves a TGA image to the specified file in the specified format. |
| save_tga_ext(const char *filename, tga_image *ptga, tga_type type, tga_func_def *func_def) | Saves a TGA image to the specified file in the specified format using the custom file functions specified in the tga_func_def structure. |

### Notes
Using ```save_tga``` and ```save_tga_ext``` with any mapped type argument will fail if the image has over 256 colors.

## License

libtga is licensed under the MIT License, see LICENSE.txt for more information.