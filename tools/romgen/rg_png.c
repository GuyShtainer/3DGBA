/* rg_png.c -- a minimal PNG writer for the authoring tool (3DGBA original work, GPLv3). Host only, pure C, no dependency.
 * RGBA8, filter type 0 on every row, the zlib stream made of STORED deflate blocks (no compression), CRC-32 on the
 * chunks and Adler-32 on the stream. Spec: docs/phase34-frlg/SPEC.md section 5.2. */
#include "rg_png.h"

#include <stdlib.h>
#include <string.h>

static uint32_t sCrcTable[256];
static int sCrcReady;

static void crc_init(void)
{
    uint32_t n, k, c;

    if (sCrcReady)
        return;
    for (n = 0; n < 256u; n++) {
        c = n;
        for (k = 0; k < 8u; k++)
            c = (c & 1u) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        sCrcTable[n] = c;
    }
    sCrcReady = 1;
}

uint32_t rg_png_crc32(const uint8_t *p, size_t n, uint32_t crc)
{
    size_t i;

    crc_init();
    crc = ~crc;
    for (i = 0; i < n; i++)
        crc = sCrcTable[(crc ^ p[i]) & 0xFFu] ^ (crc >> 8);
    return ~crc;
}

uint32_t rg_png_adler32(const uint8_t *p, size_t n)
{
    uint32_t a = 1, b = 0;
    size_t i;

    for (i = 0; i < n; i++) {
        a = (a + p[i]) % 65521u;
        b = (b + a) % 65521u;
    }
    return (b << 16) | a;
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

size_t rg_png_encode(const uint8_t *rgba, int w, int h, uint8_t **out)
{
    size_t rowBytes, raw, nBlocks, z, total, pos, i;
    uint8_t *rawBuf, *zbuf, *buf;
    int y;
    uint32_t c;

    *out = NULL;
    if (rgba == NULL || w <= 0 || h <= 0 || w > 16384 || h > 16384)
        return 0;
    rowBytes = 1u + 4u * (size_t)w;
    raw = rowBytes * (size_t)h;
    rawBuf = (uint8_t *)malloc(raw);
    if (rawBuf == NULL)
        return 0;
    for (y = 0; y < h; y++) {
        rawBuf[rowBytes * (size_t)y] = 0;
        memcpy(rawBuf + rowBytes * (size_t)y + 1, rgba + 4u * (size_t)w * (size_t)y, 4u * (size_t)w);
    }
    nBlocks = (raw + 65534u) / 65535u;
    z = 2u + raw + 5u * nBlocks + 4u;
    zbuf = (uint8_t *)malloc(z);
    if (zbuf == NULL) {
        free(rawBuf);
        return 0;
    }
    zbuf[0] = 0x78;
    zbuf[1] = 0x01;
    pos = 2;
    for (i = 0; i < raw; i += 65535u) {
        size_t len = raw - i < 65535u ? raw - i : 65535u;

        zbuf[pos++] = (uint8_t)(i + len >= raw);          /* BFINAL, BTYPE 00 */
        zbuf[pos++] = (uint8_t)(len & 0xFFu);
        zbuf[pos++] = (uint8_t)(len >> 8);
        zbuf[pos++] = (uint8_t)(~len & 0xFFu);
        zbuf[pos++] = (uint8_t)((~len >> 8) & 0xFFu);
        memcpy(zbuf + pos, rawBuf + i, len);
        pos += len;
    }
    put32(zbuf + pos, rg_png_adler32(rawBuf, raw));
    pos += 4;
    free(rawBuf);

    total = 8u + (12u + 13u) + (12u + pos) + 12u;
    buf = (uint8_t *)malloc(total);
    if (buf == NULL) {
        free(zbuf);
        return 0;
    }
    {
        static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
        uint8_t ihdr[13];
        size_t o = 0;

        memcpy(buf, sig, 8);
        o = 8;
        put32(ihdr, (uint32_t)w);
        put32(ihdr + 4, (uint32_t)h);
        ihdr[8] = 8;        /* bit depth */
        ihdr[9] = 6;        /* colour type: RGBA */
        ihdr[10] = ihdr[11] = ihdr[12] = 0;
        put32(buf + o, 13u);
        memcpy(buf + o + 4, "IHDR", 4);
        memcpy(buf + o + 8, ihdr, 13);
        c = rg_png_crc32(buf + o + 4, 4u + 13u, 0);
        put32(buf + o + 21, c);
        o += 25;
        put32(buf + o, (uint32_t)pos);
        memcpy(buf + o + 4, "IDAT", 4);
        memcpy(buf + o + 8, zbuf, pos);
        c = rg_png_crc32(buf + o + 4, 4u + pos, 0);
        put32(buf + o + 8 + pos, c);
        o += 12u + pos;
        put32(buf + o, 0);
        memcpy(buf + o + 4, "IEND", 4);
        c = rg_png_crc32(buf + o + 4, 4, 0);
        put32(buf + o + 8, c);
        o += 12;
        total = o;
    }
    free(zbuf);
    *out = buf;
    return total;
}

int rg_png_write_rgba(const char *path, const uint8_t *rgba, int w, int h)
{
    uint8_t *buf;
    size_t n = rg_png_encode(rgba, w, h, &buf);
    FILE *fp;
    int ok;

    if (n == 0)
        return 0;
    fp = fopen(path, "wb");
    if (fp == NULL) {
        free(buf);
        return 0;
    }
    ok = fwrite(buf, 1, n, fp) == n;
    ok = (fclose(fp) == 0) && ok;
    free(buf);
    return ok;
}
