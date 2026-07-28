#ifndef RUSH_IMAGE_H
#define RUSH_IMAGE_H

#include <stddef.h>

typedef struct {
    int width;
    int height;
    unsigned char *rgb; /* interleaved R,G,B, top-to-bottom, row-major */
} Image;

/* Loads a .bmp (uncompressed 24/32-bit) or .ppm (P6) file.
 * Returns 0 on success, -1 on failure (an error is already printed
 * via rush_error). Caller must image_free() on success. */
int image_load(const char *path, Image *out);
void image_free(Image *img);

/* Renders the image to the current terminal using ANSI truecolor
 * half-block characters, scaled down to fit the terminal size. */
void image_render_terminal(const Image *img);

#endif
