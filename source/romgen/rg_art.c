/* Modified for 3DGBA (GPLv3), 2026.
 * Original: ZallaxDev/pokeemerald-3Ds-dualscreen @ c330c0a, 3ds_port/scripts/voxel_art.py and the
 * art questions of voxel_cells.py, MIT License - see source/voxel/NOTICE.md.
 * Portions Copyright (c) Dust Zallax, MIT. */
#include "rg_art.h"

#include <stdlib.h>
#include <string.h>

#include "vx_lz77.h"

#define RG_METATILES 1024u

static double rg_fabs(double v) { return v < 0.0 ? -v : v; }

struct RgPair {
    const RgTileset *ts[2];
    uint8_t *tiles[2];
    uint32_t tilesLen[2];
    RgLayer *layer[RG_METATILES][2];
    uint8_t feat[RG_METATILES][3];   /* 0 unknown, 1 false, 2 true: foliage, treads, covers */
};

static const RgLayer sEmptyLayer;

RgPair *rg_pair_open(const RgWorld *w, uint16_t pairIndex)
{
    RgPair *p;
    unsigned k;

    if (w == NULL || pairIndex >= w->pairCount)
        return NULL;
    p = (RgPair *)calloc(1, sizeof(*p));
    if (p == NULL)
        return NULL;
    for (k = 0; k < 2; k++) {
        const RgTileset *t = &w->tilesets[w->pairs[pairIndex].ts[k]];
        p->ts[k] = t;
        if (t->addr == 0 || t->tilesBytes == 0)
            continue;
        p->tiles[k] = (uint8_t *)malloc(t->tilesBytes);
        if (p->tiles[k] == NULL) {
            rg_pair_close(p);
            return NULL;
        }
        if (t->compressed) {
            size_t avail = w->romSize - (size_t)(t->tilesRom - w->rom);
            if (vx_lz77_decode(t->tilesRom, avail, p->tiles[k], t->tilesBytes) != t->tilesBytes) {
                rg_pair_close(p);
                return NULL;
            }
        } else {
            memcpy(p->tiles[k], t->tilesRom, t->tilesBytes);
        }
        p->tilesLen[k] = t->tilesBytes;
    }
    return p;
}

void rg_pair_close(RgPair *p)
{
    unsigned m;

    if (p == NULL)
        return;
    for (m = 0; m < RG_METATILES; m++) {
        free(p->layer[m][0]);
        free(p->layer[m][1]);
    }
    free(p->tiles[0]);
    free(p->tiles[1]);
    free(p);
}

static void build_layer(const RgPair *p, uint16_t metatile, int layer, RgLayer *out)
{
    unsigned which = metatile < RG_NUM_PRIMARY ? 0u : 1u;
    const RgTileset *t = p->ts[which];
    unsigned index = metatile - which * RG_NUM_PRIMARY;
    unsigned quad;

    memset(out, 0, sizeof(*out));
    if (t->metatiles == NULL || index >= t->metatileCount)
        return;
    for (quad = 0; quad < 4; quad++) {
        uint16_t entry = rg_rd16(t->metatiles + 16u * index + 2u * (unsigned)(layer * 4 + (int)quad));
        unsigned tile = entry & 0x3FFu, pal = entry >> 12;
        unsigned tw = tile < 512u ? 0u : 1u;
        unsigned local = tile - tw * 512u;
        const RgTileset *pt = p->ts[pal < 6u ? 0u : 1u];
        const uint8_t *src;
        unsigned x, y, ox = (quad & 1u) * 8u, oy = (quad >> 1) * 8u;

        if (p->tiles[tw] == NULL || local * 32u + 32u > p->tilesLen[tw] || pt->palettes == NULL)
            continue;
        src = p->tiles[tw] + local * 32u;
        for (y = 0; y < 8; y++) {
            for (x = 0; x < 8; x++) {
                unsigned sx = (entry & 0x400u) ? 7u - x : x;
                unsigned sy = (entry & 0x800u) ? 7u - y : y;
                uint8_t packed = src[sy * 4u + sx / 2u];
                unsigned idx = (sx & 1u) ? (unsigned)(packed >> 4) : (unsigned)(packed & 0xFu);

                if (idx == 0)
                    continue;
                out->drawn[oy + y] |= (uint16_t)(1u << (ox + x));
                out->c[oy + y][ox + x] = rg_rd16(pt->palettes + 32u * pal + 2u * idx) & 0x7FFFu;
                out->count++;
            }
        }
    }
}

const RgLayer *rg_layer(RgPair *p, uint16_t metatile, int layer)
{
    RgLayer **slot;

    if (p == NULL || metatile >= RG_METATILES || (layer != 0 && layer != 1))
        return &sEmptyLayer;
    slot = &p->layer[metatile][layer];
    if (*slot == NULL) {
        *slot = (RgLayer *)malloc(sizeof(RgLayer));
        if (*slot == NULL)
            return &sEmptyLayer;
        build_layer(p, metatile, layer, *slot);
    }
    return *slot;
}

void rg_merged(RgPair *p, uint16_t metatile, RgLayer *out)
{
    const RgLayer *l0 = rg_layer(p, metatile, 0), *l1 = rg_layer(p, metatile, 1);
    unsigned y, x;

    *out = *l0;
    for (y = 0; y < 16; y++) {
        for (x = 0; x < 16; x++) {
            if (!rg_layer_has(l1, (int)x, (int)y))
                continue;
            if (!rg_layer_has(out, (int)x, (int)y))
                out->count++;
            out->drawn[y] |= (uint16_t)(1u << x);
            out->c[y][x] = l1->c[y][x];
        }
    }
}

bool rg_layer_equal(const RgLayer *a, const RgLayer *b)
{
    unsigned y, x;

    for (y = 0; y < 16; y++) {
        if (a->drawn[y] != b->drawn[y])
            return false;
        for (x = 0; x < 16; x++)
            if (rg_layer_has(a, (int)x, (int)y) && a->c[y][x] != b->c[y][x])
                return false;
    }
    return true;
}

/* ---- features (cells:206-245) ------------------------------------------------------------ */
static void rgb888(uint16_t c, unsigned *r, unsigned *g, unsigned *b)
{
    *r = (c & 31u) * 255u / 31u;
    *g = ((c >> 5) & 31u) * 255u / 31u;
    *b = ((c >> 10) & 31u) * 255u / 31u;
}

static bool foliage_of(const RgLayer *m)
{
    unsigned x, y, green = 0;

    for (y = 0; y < 16; y++) {
        for (x = 0; x < 16; x++) {
            unsigned r, g, b;
            if (!rg_layer_has(m, (int)x, (int)y))
                continue;
            rgb888(m->c[y][x], &r, &g, &b);
            green += g > 64u && g > r + 16u && g > b + 16u;
        }
    }
    return m->count > 0 && 2u * green >= m->count;
}

static bool covers_of(const RgLayer *upper)
{
    unsigned y, rows = 0;

    for (y = 0; y < 16; y++) {
        unsigned n = 0, v = upper->drawn[y];
        while (v) {
            n += v & 1u;
            v >>= 1;
        }
        rows += n >= 8u;
    }
    return rows >= 16u - 2u;
}

bool rg_treads_measure(const RgLayer *m, double *across, double *down)
{
    double lum[16][16];
    double a = 0.0, d = 0.0;
    unsigned x, y;

    *across = *down = 0.0;
    if (m->count < 240)
        return false;
    for (y = 0; y < 16; y++) {
        for (x = 0; x < 16; x++) {
            unsigned r = 0, g = 0, b = 0;
            if (rg_layer_has(m, (int)x, (int)y))
                rgb888(m->c[y][x], &r, &g, &b);
            lum[y][x] = 0.3 * (double)r + 0.59 * (double)g + 0.11 * (double)b;
        }
    }
    for (y = 0; y < 16; y++)
        for (x = 0; x < 15; x++)
            a += rg_fabs(lum[y][x] - lum[y][x + 1]);
    for (y = 0; y < 15; y++)
        for (x = 0; x < 16; x++)
            d += rg_fabs(lum[y][x] - lum[y + 1][x]);
    *across = a / 240.0;
    *down = d / 240.0;
    return *across <= 4.0 && *down >= 15.0;
}

static unsigned feature(RgPair *p, uint16_t metatile, unsigned which)
{
    uint8_t *f;
    RgLayer merged;
    double a, d;
    bool v;

    if (p == NULL || metatile >= RG_METATILES)
        return 1;
    f = &p->feat[metatile][which];
    if (*f != 0)
        return *f;
    if (which == 0) {
        rg_merged(p, metatile, &merged);
        v = foliage_of(&merged);
    } else if (which == 1) {
        rg_merged(p, metatile, &merged);
        v = rg_treads_measure(&merged, &a, &d);
    } else {
        v = covers_of(rg_layer(p, metatile, 1));
    }
    *f = v ? 2 : 1;
    return *f;
}

bool rg_foliage_ge_half(RgPair *p, uint16_t metatile) { return feature(p, metatile, 0) == 2; }
bool rg_treads(RgPair *p, uint16_t metatile) { return feature(p, metatile, 1) == 2; }
bool rg_covers(RgPair *p, uint16_t metatile) { return feature(p, metatile, 2) == 2; }

/* ---- S2.1: full-pixel layers and subtiles (SPEC-S2 section 1.1) ---- */

#define RG_MAGENTA_C5 ((uint16_t)(31u | (31u << 10)))   /* (255,0,255) = BGR555 (31,0,31) */

/* voxel_art Tilesets.subtile: the colour is read from palette (pal < 6 primary, else secondary). */
static void subtile_px(const RgPair *p, uint16_t tile, unsigned pal, uint16_t c[64], uint8_t idx[64])
{
    unsigned tw = tile < RG_NUM_PRIMARY ? 0u : 1u;
    unsigned local = tile - tw * RG_NUM_PRIMARY;
    const RgTileset *pt = p->ts[pal < 6u ? 0u : 1u];
    const uint8_t *src;
    unsigned x, y;

    if (p->tiles[tw] == NULL || local * 32u + 32u > p->tilesLen[tw]) {
        for (x = 0; x < 64; x++) {
            c[x] = RG_MAGENTA_C5;
            idx[x] = 0;
        }
        return;
    }
    src = p->tiles[tw] + local * 32u;
    for (y = 0; y < 8; y++) {
        for (x = 0; x < 8; x++) {
            uint8_t packed = src[y * 4u + x / 2u];
            unsigned i = (x & 1u) ? (unsigned)(packed >> 4) : (unsigned)(packed & 0xFu);

            idx[y * 8u + x] = (uint8_t)i;
            c[y * 8u + x] = pt->palettes != NULL ? (uint16_t)(rg_rd16(pt->palettes + 32u * pal + 2u * i) & 0x7FFFu)
                                                 : RG_MAGENTA_C5;
        }
    }
}

void rg_subtile_px(RgPair *p, uint16_t tile, uint8_t pal, uint16_t c[64], uint8_t idx[64])
{
    unsigned i;

    if (p == NULL || pal > 15u) {
        for (i = 0; i < 64; i++) {
            c[i] = RG_MAGENTA_C5;
            idx[i] = 0;
        }
        return;
    }
    subtile_px(p, (uint16_t)(tile & 0x3FFu), pal, c, idx);
}

void rg_cell_px(RgPair *p, uint16_t metatile, int layer, RgCellPx *out)
{
    unsigned which = metatile < RG_NUM_PRIMARY ? 0u : 1u;
    unsigned quad, i;
    const RgTileset *t;
    unsigned index;

    memset(out, 0, sizeof(*out));
    if (p == NULL || (layer != 0 && layer != 1))
        return;
    t = p->ts[which];
    index = metatile - which * RG_NUM_PRIMARY;
    if (t->metatiles == NULL || index >= t->metatileCount) {
        /* upstream entries() would hand back a short list: the lower layer paints nothing real, so it is magenta */
        if (layer == 0) {
            for (i = 0; i < 16; i++) {
                unsigned x;
                out->drawn[i] = 0xFFFFu;
                for (x = 0; x < 16; x++)
                    out->c[i][x] = RG_MAGENTA_C5;
            }
        }
        return;
    }
    for (quad = 0; quad < 4; quad++) {
        uint16_t entry = rg_rd16(t->metatiles + 16u * index + 2u * (unsigned)(layer * 4 + (int)quad));
        uint16_t sc[64];
        uint8_t si[64];
        unsigned x, y, ox = (quad & 1u) * 8u, oy = (quad >> 1) * 8u;

        subtile_px(p, (uint16_t)(entry & 0x3FFu), (entry >> 12) & 0xFu, sc, si);
        for (y = 0; y < 8; y++) {
            for (x = 0; x < 8; x++) {
                unsigned sx = (entry & 0x400u) ? 7u - x : x;
                unsigned sy = (entry & 0x800u) ? 7u - y : y;

                out->c[oy + y][ox + x] = sc[sy * 8u + sx];
                out->idx[oy + y][ox + x] = si[sy * 8u + sx];
                if (layer == 0 || si[sy * 8u + sx] != 0)
                    out->drawn[oy + y] |= (uint16_t)(1u << (ox + x));
            }
        }
    }
}
