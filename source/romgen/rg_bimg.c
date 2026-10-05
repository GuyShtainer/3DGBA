/* Modified for 3DGBA (GPLv3), 2026.
 * Original: ZallaxDev/pokeemerald-3Ds-dualscreen @ c330c0a, 3ds_port/scripts/voxel_building.py
 * (LayoutArt.building_art, LayoutArt.cell_image) and gen_voxel_buildings.py (the PIL image helpers),
 * MIT License - see source/voxel/NOTICE.md.
 * Portions Copyright (c) Dust Zallax, MIT. */
#include "rg_bimg.h"

#include <stdlib.h>
#include <string.h>

#define RG_IMG_MAX 4096

bool rg_img_new(RgImage *im, int w, int h)
{
    if (im == NULL)
        return false;
    im->w = im->h = 0;
    im->px = NULL;
    if (w <= 0 || h <= 0 || w > RG_IMG_MAX || h > RG_IMG_MAX)
        return false;
    im->px = (uint8_t *)calloc((size_t)w * (size_t)h, 4);
    if (im->px == NULL)
        return false;
    im->w = w;
    im->h = h;
    return true;
}

void rg_img_free(RgImage *im)
{
    if (im == NULL)
        return;
    free(im->px);
    im->px = NULL;
    im->w = im->h = 0;
}

void rg_img_paste(RgImage *dst, const RgImage *src, int x, int y)
{
    int sy;

    if (dst == NULL || src == NULL || dst->px == NULL || src->px == NULL)
        return;
    for (sy = 0; sy < src->h; sy++) {
        int dy = y + sy, x0 = 0, x1 = src->w;

        if (dy < 0 || dy >= dst->h)
            continue;
        if (x + x0 < 0)
            x0 = -x;
        if (x + x1 > dst->w)
            x1 = dst->w - x;
        if (x1 <= x0)
            continue;
        memcpy(dst->px + 4 * ((size_t)dy * (size_t)dst->w + (size_t)(x + x0)),
               src->px + 4 * ((size_t)sy * (size_t)src->w + (size_t)x0), 4 * (size_t)(x1 - x0));
    }
}

bool rg_img_bbox(const RgImage *im, int bb[4])
{
    int x, y, x0 = im->w, y0 = im->h, x1 = 0, y1 = 0;

    for (y = 0; y < im->h; y++) {
        for (x = 0; x < im->w; x++) {
            if (im->px[4 * ((size_t)y * (size_t)im->w + (size_t)x) + 3] == 0)
                continue;
            if (x < x0) x0 = x;
            if (y < y0) y0 = y;
            if (x + 1 > x1) x1 = x + 1;
            if (y + 1 > y1) y1 = y + 1;
        }
    }
    if (x1 <= x0 || y1 <= y0)
        return false;
    bb[0] = x0; bb[1] = y0; bb[2] = x1; bb[3] = y1;
    return true;
}

bool rg_img_crop(const RgImage *im, const int bb[4], RgImage *out)
{
    int y;

    if (bb[0] < 0 || bb[1] < 0 || bb[2] > im->w || bb[3] > im->h || bb[2] <= bb[0] || bb[3] <= bb[1])
        return false;
    if (!rg_img_new(out, bb[2] - bb[0], bb[3] - bb[1]))
        return false;
    for (y = bb[1]; y < bb[3]; y++)
        memcpy(out->px + 4 * (size_t)(y - bb[1]) * (size_t)out->w,
               im->px + 4 * ((size_t)y * (size_t)im->w + (size_t)bb[0]), 4 * (size_t)out->w);
    return true;
}

uint64_t rg_img_hash(const RgImage *im)
{
    uint64_t h = 1469598103934665603ull;
    size_t i, n = (size_t)im->w * (size_t)im->h * 4u;
    uint32_t dims[2];
    const uint8_t *b = (const uint8_t *)dims;

    dims[0] = (uint32_t)im->w;
    dims[1] = (uint32_t)im->h;
    for (i = 0; i < sizeof(dims); i++)
        h = (h ^ b[i]) * 1099511628211ull;
    for (i = 0; i < n; i++)
        h = (h ^ im->px[i]) * 1099511628211ull;
    return h;
}

bool rg_img_equal(const RgImage *a, const RgImage *b)
{
    return a->w == b->w && a->h == b->h && memcmp(a->px, b->px, (size_t)a->w * (size_t)a->h * 4u) == 0;
}

void rg_c5_rgba(uint16_t c, uint8_t a, uint8_t out[4])
{
    out[0] = rg_c5_to_8(c & 31u);
    out[1] = rg_c5_to_8((c >> 5) & 31u);
    out[2] = rg_c5_to_8((c >> 10) & 31u);
    out[3] = a;
}

static int hexv(char ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

bool rg_hex_to_c5(const char *hex, uint16_t *c5)
{
    unsigned ch[3], k, v;

    if (hex == NULL || c5 == NULL)
        return false;
    for (k = 0; k < 3; k++) {
        int a = hexv(hex[2 * k]), b = a < 0 ? -1 : hexv(hex[2 * k + 1]);

        if (a < 0 || b < 0)
            return false;
        v = (unsigned)(a * 16 + b);
        for (ch[k] = 0; ch[k] < 32u; ch[k]++)
            if (rg_c5_to_8(ch[k]) == v)
                break;
        if (ch[k] == 32u)
            return false;
    }
    if (hex[6] != '\0')
        return false;
    *c5 = (uint16_t)(ch[0] | (ch[1] << 5) | (ch[2] << 10));
    return true;
}

static void put(RgImage *im, int x, int y, uint16_t c, uint8_t a)
{
    rg_c5_rgba(c, a, im->px + 4 * ((size_t)y * (size_t)im->w + (size_t)x));
}

bool rg_cell_image(RgPair *p, uint16_t metatile, RgImage *out16)
{
    RgCellPx lo, hi;
    int x, y;

    if (p == NULL || !rg_img_new(out16, 16, 16))
        return false;
    rg_cell_px(p, metatile, 0, &lo);
    rg_cell_px(p, metatile, 1, &hi);
    for (y = 0; y < 16; y++)
        for (x = 0; x < 16; x++)
            put(out16, x, y, hi.idx[y][x] != 0 ? hi.c[y][x] : lo.c[y][x], 255);
    return true;
}

#define RG_MAX_GROUND 8u

/* One 8x8 lower-layer block of a cell: 64 colours. */
static void quarter(const RgCellPx *cp, unsigned q, uint16_t out[64])
{
    unsigned k, ox = (q & 1u) * 8u, oy = (q >> 1) * 8u;

    for (k = 0; k < 64; k++)
        out[k] = cp->c[oy + k / 8u][ox + k % 8u];
}

bool rg_building_art(const RgWorld *w, RgPair *p, const RgLayout *L, int x0, int y0, int cw, int ch,
                     const uint16_t *groundTiles, unsigned nGround, const uint8_t *owned,
                     bool upperOnly, RgImage *out)
{
    RgCellPx gcp[RG_MAX_GROUND];
    uint16_t gq[RG_MAX_GROUND * 4u][64];
    unsigned ng, i, nq = 0;
    int cx, cy;

    (void)w;
    if (p == NULL || L == NULL || out == NULL || cw <= 0 || ch <= 0 || nGround > RG_MAX_GROUND)
        return false;
    ng = nGround;
    for (i = 0; i < ng; i++) {
        unsigned q;

        rg_cell_px(p, groundTiles[i], 0, &gcp[i]);
        for (q = 0; q < 4; q++)
            quarter(&gcp[i], q, gq[nq++]);
    }
    if (!rg_img_new(out, cw * 16, ch * 16))
        return false;
    for (cy = 0; cy < ch; cy++) {
        for (cx = 0; cx < cw; cx++) {
            uint16_t m;
            RgCellPx lo, hi;
            unsigned q;

            if (owned != NULL && !owned[cy * cw + cx])
                continue;
            m = rg_metatile(L, x0 + cx, y0 + cy);
            if (m == RG_NONE) {
                rg_img_free(out);
                return false;
            }
            rg_cell_px(p, m, 0, &lo);
            rg_cell_px(p, m, 1, &hi);
            for (q = 0; q < 4; q++) {
                uint16_t blk[64];
                bool isGround = false;
                unsigned k, g;

                quarter(&lo, q, blk);
                for (g = 0; g < nq && !isGround; g++)
                    isGround = memcmp(blk, gq[g], sizeof(blk)) == 0;
                for (k = 0; k < 64; k++) {
                    unsigned lx = (q & 1u) * 8u + k % 8u, ly = (q >> 1) * 8u + k / 8u;
                    int X = cx * 16 + (int)lx, Y = cy * 16 + (int)ly;

                    if (hi.idx[ly][lx] != 0) {
                        put(out, X, Y, hi.c[ly][lx], 255);
                    } else if (upperOnly) {
                        continue;
                    } else if (!isGround) {
                        bool same = false;

                        if (owned != NULL) {
                            for (g = 0; g < ng && !same; g++)
                                same = gcp[g].c[ly][lx] == lo.c[ly][lx];
                        }
                        if (same)
                            continue;
                        put(out, X, Y, lo.c[ly][lx], 255);
                    }
                }
            }
        }
    }
    return true;
}
