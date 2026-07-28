#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "image.h"

#ifdef _WIN32
  #include <windows.h>
  #ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
    #define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
  #endif
#else
  #include <sys/ioctl.h>
  #include <unistd.h>
#endif

static unsigned short read_u16_le(const unsigned char *p) {
    return (unsigned short)((unsigned int)p[0] | ((unsigned int)p[1] << 8));
}
static unsigned int read_u32_le(const unsigned char *p) {
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8) |
           ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}
static int read_i32_le(const unsigned char *p) {
    return (int)read_u32_le(p);
}

static int load_bmp(FILE *f, Image *out) {
    unsigned char file_header[14];
    if (fread(file_header, 1, 14, f) != 14) { fprintf(stdout, "error truncated BMP header\n"); return -1; }
    unsigned int pixel_offset = read_u32_le(file_header + 10);

    unsigned char dib[40];
    if (fread(dib, 1, 40, f) != 40) { fprintf(stdout, "error unsupported BMP variant (header too short)\n"); return -1; }

    int width = read_i32_le(dib + 4);
    int height_raw = read_i32_le(dib + 8);
    unsigned short bpp = read_u16_le(dib + 14);
    unsigned int compression = read_u32_le(dib + 16);

    if (compression != 0 || (bpp != 24 && bpp != 32) || width <= 0 || height_raw == 0) {
        fprintf(stdout, "error unsupported BMP variant (only uncompressed 24/32-bit supported)\n");
        return -1;
    }

    int top_down = height_raw < 0;
    int height = top_down ? -height_raw : height_raw;
    int bytes_per_pixel = bpp / 8;
    int row_size = ((width * bpp + 31) / 32) * 4;

    if (fseek(f, (long)pixel_offset, SEEK_SET) != 0) {
        fprintf(stdout, "error could not seek to BMP pixel data\n");
        return -1;
    }

    unsigned char *rgb = malloc((size_t)width * height * 3);
    unsigned char *row = malloc((size_t)row_size);
    if (!rgb || !row) { free(rgb); free(row); fprintf(stdout, "error out of memory\n"); return -1; }

    for (int r = 0; r < height; r++) {
        if (fread(row, 1, (size_t)row_size, f) != (size_t)row_size) {
            fprintf(stdout, "error truncated BMP pixel data\n");
            free(rgb); free(row);
            return -1;
        }
        int dest_row = top_down ? r : (height - 1 - r);
        for (int c = 0; c < width; c++) {
            unsigned char b = row[c * bytes_per_pixel + 0];
            unsigned char g = row[c * bytes_per_pixel + 1];
            unsigned char rr = row[c * bytes_per_pixel + 2];
            size_t idx = ((size_t)dest_row * width + c) * 3;
            rgb[idx + 0] = rr;
            rgb[idx + 1] = g;
            rgb[idx + 2] = b;
        }
    }
    free(row);

    out->width = width;
    out->height = height;
    out->rgb = rgb;
    return 0;
}

static int ppm_next_token(FILE *f, char *buf, size_t bufsz) {
    int c;
    /* skip whitespace and # comments */
    for (;;) {
        c = fgetc(f);
        if (c == EOF) return -1;
        if (c == '#') { while ((c = fgetc(f)) != EOF && c != '\n') ; continue; }
        if (!isspace(c)) break;
    }
    size_t i = 0;
    while (c != EOF && !isspace(c) && i < bufsz - 1) {
        buf[i++] = (char)c;
        c = fgetc(f);
    }
    buf[i] = '\0';
    return 0;
}

static int load_ppm(FILE *f, Image *out) {
    char magic[3];
    if (fread(magic, 1, 2, f) != 2) { fprintf(stdout, "error truncated PPM header\n"); return -1; }
    magic[2] = '\0';
    if (strcmp(magic, "P6") != 0) {
        fprintf(stdout, "error only binary P6 PPM is supported\n");
        return -1;
    }

    char tok[32];
    if (ppm_next_token(f, tok, sizeof(tok)) != 0) { fprintf(stdout, "error truncated PPM header\n"); return -1; }
    int width = atoi(tok);
    if (ppm_next_token(f, tok, sizeof(tok)) != 0) { fprintf(stdout, "error truncated PPM header\n"); return -1; }
    int height = atoi(tok);
    if (ppm_next_token(f, tok, sizeof(tok)) != 0) { fprintf(stdout, "error truncated PPM header\n"); return -1; }
    int maxval = atoi(tok);

    if (width <= 0 || height <= 0) { fprintf(stdout, "error invalid PPM dimensions\n"); return -1; }
    if (maxval != 255) {
        fprintf(stdout, "error only 8-bit PPM (maxval 255) is supported\n");
        return -1;
    }

    /* ppm_next_token already consumed the single whitespace byte that
     * separates the header from binary pixel data, as part of
     * detecting where the maxval token ended - nothing more to skip. */
    unsigned char *rgb = malloc((size_t)width * height * 3);
    if (!rgb) { fprintf(stdout, "error out of memory\n"); return -1; }
    size_t need = (size_t)width * height * 3;
    if (fread(rgb, 1, need, f) != need) {
        fprintf(stdout, "error truncated PPM pixel data\n");
        free(rgb);
        return -1;
    }

    out->width = width;
    out->height = height;
    out->rgb = rgb;
    return 0;
}

int image_load(const char *path, Image *out) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stdout, "error file %s not found\n", path); return -1; }

    unsigned char magic[2];
    if (fread(magic, 1, 2, f) != 2) { fprintf(stdout, "error file too small to be an image\n"); fclose(f); return -1; }
    fseek(f, 0, SEEK_SET);

    int rc;
    if (magic[0] == 'B' && magic[1] == 'M') {
        rc = load_bmp(f, out);
    } else if (magic[0] == 'P' && (magic[1] == '6' || magic[1] == '3')) {
        rc = load_ppm(f, out);
    } else {
        fprintf(stdout, "error unsupported file type (only .bmp and .ppm are supported)\n");
        rc = -1;
    }
    fclose(f);
    return rc;
}

void image_free(Image *img) {
    if (img->rgb) free(img->rgb);
    img->rgb = NULL;
    img->width = 0;
    img->height = 0;
}

static void get_terminal_size(int *cols, int *rows) {
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
        *cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        *rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
        return;
    }
#else
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return;
    }
#endif
    *cols = 80;
    *rows = 24;
}

void image_render_terminal(const Image *img) {
#ifdef _WIN32
    /* Windows consoles don't interpret ANSI/VT escape sequences (used
     * below for truecolor) unless this mode flag is explicitly enabled.
     * Without it, the escape codes print as literal garbage characters
     * instead of being interpreted as colors. */
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (GetConsoleMode(hOut, &mode)) {
        SetConsoleMode(hOut, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }
    /* Also switch the output codepage to UTF-8 so the U+2580 "▀"
     * half-block character (emitted as raw UTF-8 bytes below) displays
     * correctly instead of as a box/question-mark glyph. */
    SetConsoleOutputCP(CP_UTF8);
#endif
    int term_cols, term_rows;
    get_terminal_size(&term_cols, &term_rows);

    int max_cols = term_cols > 4 ? term_cols : 4;
    int max_rows = (term_rows - 2) > 2 ? (term_rows - 2) : 2;
    int height_budget = max_rows * 2; /* two source rows per printed row */

    double scale = 1.0;
    if (img->width > max_cols) {
        double s = (double)max_cols / img->width;
        if (s < scale) scale = s;
    }
    if (img->height > height_budget) {
        double s = (double)height_budget / img->height;
        if (s < scale) scale = s;
    }

    int dest_w = (int)(img->width * scale);
    int dest_h = (int)(img->height * scale);
    if (dest_w < 1) dest_w = 1;
    if (dest_h < 2) dest_h = 2;
    if (dest_h % 2 != 0) dest_h -= 1;

    for (int y = 0; y < dest_h; y += 2) {
        for (int x = 0; x < dest_w; x++) {
            int sx = (int)((long)x * img->width / dest_w);
            int sy_top = (int)((long)y * img->height / dest_h);
            int sy_bot = (int)((long)(y + 1) * img->height / dest_h);
            if (sx >= img->width) sx = img->width - 1;
            if (sy_top >= img->height) sy_top = img->height - 1;
            if (sy_bot >= img->height) sy_bot = img->height - 1;

            size_t top_idx = ((size_t)sy_top * img->width + sx) * 3;
            size_t bot_idx = ((size_t)sy_bot * img->width + sx) * 3;

            printf("\x1b[38;2;%d;%d;%dm\x1b[48;2;%d;%d;%dm\xe2\x96\x80",
                   img->rgb[top_idx], img->rgb[top_idx + 1], img->rgb[top_idx + 2],
                   img->rgb[bot_idx], img->rgb[bot_idx + 1], img->rgb[bot_idx + 2]);
        }
        printf("\x1b[0m\n");
    }
    fflush(stdout);
}
