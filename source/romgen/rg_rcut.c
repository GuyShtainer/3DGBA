/* Modified for 3DGBA (GPLv3), 2026.
 * Original: ZallaxDev/pokeemerald-3Ds-dualscreen @ c330c0a, 3ds_port/scripts/gen_voxel_relief.py (_cut_mask, _lifted,
 * cut_cells, plain_ground, _plain_tile, _plain_background, _behind: rel:2810-3000), MIT License - see
 * source/voxel/NOTICE.md. Portions Copyright (c) Dust Zallax, MIT. */
#include "rg_rcut.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "rg_bimg.h"
#include "rg_pyset.h"
#include "rg_rrock.h"
#include "rg_signs.h"

#define PER_CELL RG_P
#define STEPPX 4                     /* STEP: lattice spacing in pixels (rel:167) */

/* ---- shared and per-layout state ---- */

bool rg_cut_shared_init(RgCutShared *s)
{
    assert(s != NULL);
    memset(s, 0, sizeof(*s));
    s->scratch = malloc(rg_commonest_scratch_bytes(256u));
    return s->scratch != NULL;
}

void rg_cut_shared_free(RgCutShared *s)
{
    assert(s != NULL);
    free(s->scratch);
    s->scratch = NULL;
}

void rg_cut_art_init(RgCutArt *a, RgCutShared *shared, const RgWorld *w, const RgLayout *L, RgPair *pair,
                     const RgAlias *alias)
{
    assert(a != NULL && shared != NULL && w != NULL && L != NULL && pair != NULL);
    memset(a, 0, sizeof(*a));
    a->sh = shared;
    a->w = w;
    a->L = L;
    a->pair = pair;
    a->alias = alias;
    memset(a->plainTile, -1, sizeof(a->plainTile));
}

void rg_cut_art_free(RgCutArt *a)
{
    unsigned m;

    assert(a != NULL);
    for (m = 0; m < RG_MT_MAX; m++) {
        free(a->img[m]);
        a->img[m] = NULL;
    }
}

uint16_t rg_cut_meta(const RgCutArt *a, int x, int y)
{
    return rg_alias_metatile(a->alias, a->L, x, y);
}

uint16_t rg_cut_own(const RgCutArt *a, int x, int y)
{
    return rg_metatile(a->L, x, y);
}

const uint32_t *rg_cut_image(RgCutArt *a, uint16_t m)
{
    RgImage im;
    unsigned k;
    uint32_t *out;

    assert(a != NULL && m < RG_MT_MAX);
    if (a->img[m] != NULL)
        return a->img[m];
    if (!rg_alias_cell_image(a->alias, a->pair, m, false, &im)) {
        a->sh->oom = true;
        return NULL;
    }
    out = (uint32_t *)malloc(256u * sizeof(uint32_t));
    if (out == NULL) {
        a->sh->oom = true;
        rg_img_free(&im);
        return NULL;
    }
    for (k = 0; k < 256u; k++) {
        const uint8_t *p = im.px + 4u * k;

        out[k] = ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
    }
    rg_img_free(&im);
    a->img[m] = out;
    return out;
}

static bool rgb_in(const uint8_t (*set)[3], unsigned n, uint32_t rgb)
{
    unsigned i;

    for (i = 0; i < n; i++)
        if (rgb == (((uint32_t)set[i][0] << 16) | ((uint32_t)set[i][1] << 8) | set[i][2]))
            return true;
    return false;
}

bool rg_rock_light_rgb(uint32_t rgb)
{
    return rgb_in(RG_ROCK_TOP, 2, rgb) || rgb_in(RG_ROCK_RIM, 1, rgb);
}

bool rg_rock_all_rgb(uint32_t rgb)
{
    return rg_rock_light_rgb(rgb) || rgb_in(RG_ROCK_FACE, 3, rgb) || rgb_in(RG_ROCK_FLECK, 1, rgb);
}

uint32_t rg_cut_commonest(RgCutShared *s, const uint32_t *px, unsigned n)
{
    int64_t keys[256], hashes[256];
    unsigned i;

    assert(s != NULL && px != NULL && n >= 1u && n <= 256u);
    for (i = 0; i < n; i++) {
        int64_t t[3];

        t[0] = (int64_t)((px[i] >> 16) & 0xFFu);
        t[1] = (int64_t)((px[i] >> 8) & 0xFFu);
        t[2] = (int64_t)(px[i] & 0xFFu);
        keys[i] = (int64_t)px[i];
        hashes[i] = rg_hash64_tuple(t, 3);
    }
    return (uint32_t)rg_commonest(keys, hashes, n, s->scratch);
}

static bool in_map(const RgCutArt *a, int x, int y)
{
    return x >= 0 && y >= 0 && x < (int)a->L->w && y < (int)a->L->h;
}

/* ---- _cut_mask (rel:2828-2847) ---- */

/* The colours of the non-rock pixels of the cells round (x, y) that stand on no rock: a set, kept as a short list
 * of distinct values. Returns the count, or -1 on no memory. */
static int ground_colours(RgCutArt *a, int x, int y, const uint8_t *rock, uint32_t *list, unsigned cap)
{
    unsigned n = 0, k, q;
    int dx, dy;

    for (dy = -1; dy <= 1; dy++) {
        for (dx = -1; dx <= 1; dx++) {
            int nx = x + dx, ny = y + dy;
            const uint32_t *im;

            if ((dx == 0 && dy == 0) || !in_map(a, nx, ny) || rock[(size_t)ny * a->L->w + (size_t)nx])
                continue;
            im = rg_cut_image(a, rg_cut_meta(a, nx, ny));
            if (im == NULL)
                return -1;
            for (k = 0; k < 256u; k++) {
                if (rg_rock_all_rgb(im[k]))
                    continue;
                for (q = 0; q < n && list[q] != im[k]; q++) {}
                if (q == n) {
                    assert(n < cap);
                    list[n++] = im[k];
                }
            }
        }
    }
    return (int)n;
}

bool rg_cut_mask(RgCutArt *a, int x, int y, const uint8_t *rock, uint16_t rows[16])
{
    uint32_t list[9 * 256];
    int nList;
    unsigned i, j, k;
    const uint32_t *img;
    uint16_t solid[16];

    assert(a != NULL && rock != NULL && rows != NULL);
    nList = ground_colours(a, x, y, rock, list, 9u * 256u);
    if (nList <= 0)
        return false;
    img = rg_cut_image(a, rg_cut_meta(a, x, y));
    if (img == NULL)
        return false;
    memset(solid, 0, sizeof(solid));
    for (j = 0; j < 16; j++) {
        for (i = 0; i < 16; i++) {
            uint32_t c = img[j * 16 + i];
            bool g = false;

            for (k = 0; k < (unsigned)nList && !g; k++)
                g = list[k] == c;
            if (!g)
                solid[j] |= (uint16_t)(1u << i);
        }
    }
    rg_cutout_mask(solid, rows);
    for (j = 0; j < 16; j++)
        rows[j] = (uint16_t)(~rows[j] & 0xFFFFu);
    return true;
}

/* ---- _lifted (rel:2850-2866) ---- */

unsigned rg_cut_lifted(const RgGrid *g, const uint16_t mask[16], double *foot)
{
    double f = g->g[0][0];
    unsigned n = 0;
    int i, j;

    for (j = 0; j < RG_SIDE; j++)
        for (i = 0; i < RG_SIDE; i++)
            if (g->g[j][i] < f)
                f = g->g[j][i];
    for (j = 0; j < 16; j++) {
        for (i = 0; i < 16; i++) {
            double fx, fy, tx, ty, v;
            int a, b;

            if (!((mask[j] >> i) & 1))
                continue;
            fx = ((double)i + 0.5) / (double)STEPPX;
            fy = ((double)j + 0.5) / (double)STEPPX;
            a = (int)fx < PER_CELL - 1 ? (int)fx : PER_CELL - 1;
            b = (int)fy < PER_CELL - 1 ? (int)fy : PER_CELL - 1;
            tx = fx - (double)a;
            ty = fy - (double)b;
            v = ((g->g[b][a] * (1 - tx) + g->g[b][a + 1] * tx) * (1 - ty)
                 + (g->g[b + 1][a] * (1 - tx) + g->g[b + 1][a + 1] * tx) * ty);
            n += (unsigned)(v - f > RG_CUT_LIFT);
        }
    }
    *foot = f;
    return n;
}

/* ---- _plain_tile, _plain_background, plain_ground (rel:2904-2969) ---- */

bool rg_cut_plain_tile(RgCutArt *a, uint16_t m)
{
    assert(a != NULL && m < RG_MT_MAX);
    if (a->plainTile[m] < 0) {
        const uint32_t *im = rg_cut_image(a, m);
        unsigned k, n = 0;

        if (im == NULL)
            return false;
        for (k = 0; k < 256u; k++)
            n += rg_rock_all_rgb(im[k]);
        a->plainTile[m] = (int8_t)(n < 256u / 10u);          /* len(px) // 10 = 25 */
    }
    return a->plainTile[m] != 0;
}

static unsigned l1(uint32_t p, uint32_t c)
{
    int dr = (int)((p >> 16) & 0xFFu) - (int)((c >> 16) & 0xFFu);
    int dg = (int)((p >> 8) & 0xFFu) - (int)((c >> 8) & 0xFFu);
    int db = (int)(p & 0xFFu) - (int)(c & 0xFFu);

    return (unsigned)(abs(dr) + abs(dg) + abs(db));
}

bool rg_cut_plain_background(RgCutArt *a, uint16_t m, const uint16_t mask[16], uint16_t ground)
{
    const uint32_t *img = rg_cut_image(a, m), *g = rg_cut_image(a, ground);
    uint32_t bg[256], gc;
    unsigned n = 0, near = 0, i, j;

    assert(mask != NULL);
    if (img == NULL || g == NULL)
        return false;
    gc = rg_cut_commonest(a->sh, g, 256u);
    for (j = 0; j < 16; j++)
        for (i = 0; i < 16; i++)
            if ((mask[j] >> i) & 1)
                bg[n++] = img[j * 16 + i];
    if (n < 8u)
        return false;
    for (i = 0; i < n; i++)
        near += (unsigned)(l1(bg[i], gc) <= 150u);
    return (double)near >= 0.9 * (double)n;
}

/* main(m) of plain_ground: (commonest colour, its pixel count, foreign pixels); a module-level cache keyed by the
 * metatile id alone, first computation wins (rel:2916-2923). */
static bool plain_main(RgCutArt *a, uint16_t m, uint32_t *c, unsigned *count, unsigned *foreign)
{
    RgCutShared *s = a->sh;

    assert(m < RG_MT_MAX);
    if (!s->plainHave[m]) {
        const uint32_t *px = rg_cut_image(a, m);
        unsigned k, cnt = 0, fr = 0;
        uint32_t cc;

        if (px == NULL)
            return false;
        cc = rg_cut_commonest(s, px, 256u);
        for (k = 0; k < 256u; k++) {
            cnt += px[k] == cc;
            fr += l1(px[k], cc) > 150u;
        }
        s->plainC[m] = cc;
        s->plainCount[m] = (uint16_t)cnt;
        s->plainForeign[m] = (uint16_t)fr;
        s->plainHave[m] = 1;
    }
    *c = s->plainC[m];
    *count = s->plainCount[m];
    *foreign = s->plainForeign[m];
    return true;
}

/* _SIDE_PIXELS (rel:2892-2897): the part of a tile that faces the rock. */
static bool side_pixel(char side, int i, int j)
{
    switch (side) {
    case 'S': return j >= 10;
    case 'E': return i >= 10;
    case 'W': return i < 6;
    default: return true;
    }
}

int32_t rg_cut_plain_ground(RgCutArt *a, const uint8_t *content, int x, int y, char side, bool forced, uint32_t kind)
{
    uint16_t m0 = rg_cut_meta(a, x, y);
    uint32_t c0;
    unsigned cnt0, fr0;
    int32_t best = -1;
    unsigned bf = 0, bn = 0, bd = 0;      /* the best key (foreign, -count, |dx|+|dy|) as (foreign, count, dist) */
    int dx, dy;

    assert(a != NULL && content != NULL);
    if (!forced) {
        if (side == 0) {
            if (!plain_main(a, m0, &c0, &cnt0, &fr0))
                return -2;
            kind = c0;
        } else {
            const uint32_t *im = rg_cut_image(a, m0);
            uint32_t part[256];
            unsigned n = 0, i, j;

            if (im == NULL)
                return -2;
            for (j = 0; j < 16; j++)
                for (i = 0; i < 16; i++)
                    if (side_pixel(side, (int)i, (int)j))
                        part[n++] = im[j * 16 + i];
            kind = rg_cut_commonest(a->sh, part, n);
        }
    }
    for (dy = -24; dy <= 24; dy++) {
        for (dx = -24; dx <= 24; dx++) {
            int cx = x + dx, cy = y + dy;
            uint16_t m;
            uint32_t mc;
            unsigned mn, mf, dist;

            if (!in_map(a, cx, cy) || content[(size_t)cy * a->L->w + (size_t)cx])
                continue;
            m = rg_cut_meta(a, cx, cy);
            if (!plain_main(a, m, &mc, &mn, &mf))
                return -2;
            if (mc != kind)
                continue;
            dist = (unsigned)(abs(dx) + abs(dy));
            /* k = (foreign, -count, dist): strictly smaller wins, the first in scan order on a tie */
            if (best < 0 || mf < bf || (mf == bf && (mn > bn || (mn == bn && dist < bd)))) {
                best = (int32_t)m;
                bf = mf;
                bn = mn;
                bd = dist;
            }
        }
    }
    if (best >= 0)
        return best;
    return forced ? -1 : (int32_t)m0;
}

/* ---- _behind (rel:2975-3000) ---- */

int32_t rg_cut_behind(RgCutArt *a, const uint8_t *rock, int x, int y, const uint16_t *mask)
{
    static const struct { int8_t dx, dy; char side; } kN[4] = {{0, -1, 'S'}, {-1, 0, 'E'}, {1, 0, 'W'}, {0, 0, 0}};
    unsigned k;

    assert(a != NULL && rock != NULL);
    if (mask != NULL) {
        const uint32_t *img = rg_cut_image(a, rg_cut_meta(a, x, y));
        uint32_t bg[256], kind;
        unsigned n = 0, i, j;

        if (img == NULL)
            return -2;
        for (j = 0; j < 16; j++)
            for (i = 0; i < 16; i++)
                if ((mask[j] >> i) & 1)
                    bg[n++] = img[j * 16 + i];
        if (n >= 8u) {
            kind = rg_cut_commonest(a->sh, bg, n);
            for (k = 0; k < 4; k++) {
                int nx = x + kN[k].dx, ny = y + kN[k].dy;
                /* (in_map and not rock) or (dx, dy) == (0, 0) */
                if ((in_map(a, nx, ny) && !rock[(size_t)ny * a->L->w + (size_t)nx]) || (kN[k].dx == 0 && kN[k].dy == 0)) {
                    int32_t m = rg_cut_plain_ground(a, rock, nx, ny, kN[k].side, true, kind);

                    if (m != -1)
                        return m;
                }
            }
        }
    }
    for (k = 0; k < 3; k++) {
        int nx = x + kN[k].dx, ny = y + kN[k].dy;

        if (in_map(a, nx, ny) && !rock[(size_t)ny * a->L->w + (size_t)nx])
            return rg_cut_plain_ground(a, rock, nx, ny, kN[k].side, false, 0);
    }
    return -1;
}

/* ---- cut_cells (rel:2870-2900) ---- */

int rg_cut_cells(RgCutArt *a, const uint8_t *rock, const RgLat *h, RgCutRec **out)
{
    unsigned n = 0, cap = 16;
    RgCutRec *buf = (RgCutRec *)malloc(cap * sizeof(RgCutRec));
    int x, y;

    assert(a != NULL && rock != NULL && h != NULL && out != NULL);
    *out = NULL;
    if (buf == NULL)
        return -1;
    for (y = 0; y < (int)a->L->h; y++) {
        for (x = 0; x < (int)a->L->w; x++) {
            uint16_t mask[16];
            RgGrid g;
            double foot;
            unsigned any = 0, j, lifted;
            RgCutRec *r;

            if (!rock[(size_t)y * a->L->w + (size_t)x] || !rg_cut_mask(a, x, y, rock, mask))
                continue;
            for (j = 0; j < 16; j++)
                any |= mask[j];
            if (any == 0)
                continue;
            rg_cell_grid(h, x, y, &g);
            lifted = rg_cut_lifted(&g, mask, &foot);
            if (lifted < RG_CUT_PIXELS)
                continue;
            if (n == cap) {
                RgCutRec *nb = (RgCutRec *)realloc(buf, cap * 2u * sizeof(RgCutRec));

                if (nb == NULL) {
                    free(buf);
                    return -1;
                }
                buf = nb;
                cap *= 2u;
            }
            r = &buf[n++];
            r->x = (uint8_t)x;
            r->y = (uint8_t)y;
            r->meta = rg_cut_meta(a, x, y);
            memcpy(r->mask, mask, sizeof(mask));
            r->foot = foot;
            r->behind = rg_cut_behind(a, rock, x, y, mask);
            if (r->behind == -2) {
                free(buf);
                return -1;
            }
        }
    }
    if (n == 0) {
        free(buf);
        return 0;
    }
    *out = buf;
    return (int)n;
}
