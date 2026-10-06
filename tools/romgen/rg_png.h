/* rg_png.h -- minimal PNG writer for the authoring tool (3DGBA original work, GPLv3). Host only. */
#ifndef RG_PNG_H
#define RG_PNG_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* RGBA8 (w*h*4 bytes, row major) -> a PNG in a malloc'd buffer (stored deflate blocks). Returns its size, 0 on error. */
size_t rg_png_encode(const uint8_t *rgba, int w, int h, uint8_t **out);
/* The same, written to `path`. 1 = ok, 0 = error. */
int rg_png_write_rgba(const char *path, const uint8_t *rgba, int w, int h);
uint32_t rg_png_crc32(const uint8_t *p, size_t n, uint32_t crc);
uint32_t rg_png_adler32(const uint8_t *p, size_t n);

#endif
