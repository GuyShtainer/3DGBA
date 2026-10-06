/* Modified for 3DGBA (GPLv3), 2026.
 * Original: ZallaxDev/pokeemerald-3Ds-dualscreen @ c330c0a, 3ds_port/scripts/gen_voxel_relief.py (solve_drawn:
 * rel:1851-2336), MIT License - see source/voxel/NOTICE.md. Portions Copyright (c) Dust Zallax, MIT. */
#include "rg_rsolve.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "rg_rrock.h"

#define LEVEL 16
#define SIDE_RISE 16                 /* rel:584 */
#define OPEN (INT32_MIN + 1)         /* counted()'s "open" */

typedef struct SD {
    int cw, ch;
    size_t n;
    const RgPrep *p;
    const int32_t *level;
    const int8_t *side;
    const uint8_t *flat, *freeMid, *faceLow;
    const uint16_t *meta;
    int32_t *cell, *top, *floorOf, *foundV;
    uint8_t *voidc, *soil, *thin, *lfn, *crest, *tile, *foundSet;
    int32_t *rock;                   /* rock cell indices, row-major */
    unsigned nRock;
} SD;

static size_t at(const SD *s, int x, int y) { return (size_t)y * (size_t)s->cw + (size_t)x; }
static bool inb(const SD *s, int x, int y) { return x >= 0 && x < s->cw && y >= 0 && y < s->ch; }

static int tile_of(const SD *s, int x, int y)
{
    size_t i = at(s, x, y);

    if (s->side[i] != 0)
        return RG_RT_BAND;
    if (rg_is_face_south(s->meta[i]) || s->freeMid[i] != 0 || s->flat[i] == RG_FLAT_FALL || s->faceLow[i] != 0)
        return RG_RT_FACE;
    return RG_RT_CORNER;
}

/* ---- phase A: footprints (rel:1905-1943) ---- */

static bool ridge_end(const SD *s, int cx, int cy)
{
    return (cy - 1 >= 0 && s->side[at(s, cx, cy - 1)] != 0) || (cy + 1 < s->ch && s->side[at(s, cx, cy + 1)] != 0);
}

/* max(counts, key=counts.get): the first region of the largest pixel count (counts are in first-seen order). */
static int32_t best_region(const RgPrep *p, const RgCellStat *st)
{
    unsigned k, best = 0;

    for (k = 1; k < st->nCounts; k++)
        if (p->counts[st->cOff + k].n > p->counts[st->cOff + best].n)
            best = k;
    return p->counts[st->cOff + best].region;
}

static void footprint_cell(SD *s, int cx, int cy)
{
    size_t i = at(s, cx, cy);
    const RgCellStat *st = &s->p->stats[i];
    double fp = 0.6 * (double)st->n;
    bool footTop = (double)st->top >= fp, footGround = (double)st->ground >= fp;
    unsigned m = s->meta[i];
    bool boulder = rg_is_boulder(m), plain = !(footTop || footGround);

    s->voidc[i] = 0;
    if (rg_is_sea_cap(m) || (boulder && ridge_end(s, cx, cy)))
        return;
    if (s->flat[i] == RG_FLAT_BRIDGE || (s->flat[i] == RG_FLAT_FLOOR && plain)) {
        s->lfn[i] = 1;
        s->soil[i] = 1;
    } else if ((s->flat[i] == RG_FLAT_WATER || s->flat[i] == RG_FLAT_SIGNPOST) && plain) {
        if (st->nCounts != 0)
            s->cell[i] = s->level[best_region(s->p, st)];
        else
            s->lfn[i] = 1;
        s->soil[i] = 1;
    } else if (boulder || footTop || footGround) {
        if (st->nCounts != 0) {
            s->cell[i] = s->level[best_region(s->p, st)];
            s->soil[i] = !boulder && !footTop;
        } else if (footTop) {
            s->thin[i] = 1;
        }
    }
}

/* ---- phase B: the ledges between bands (rel:1944-1969) ---- */

static bool thin_cell(SD *s, int cx, int cy, int32_t *out)
{
    int dx, dy;

    for (dx = -1; dx <= 1; dx += 2) {
        int x = cx + dx, n = 0;

        while (inb(s, x, cy) && s->side[at(s, x, cy)] != 0 && s->cell[at(s, x, cy)] == RG_LV_NONE) {
            n += s->side[at(s, x, cy)] * dx;
            x += dx;
        }
        if (x != cx + dx && inb(s, x, cy) && s->cell[at(s, x, cy)] != RG_LV_NONE) {
            *out = s->cell[at(s, x, cy)] - SIDE_RISE * n;
            return true;
        }
    }
    for (dy = -1; dy <= 1; dy += 2) {
        int y = cy + dy;

        if (inb(s, cx, y) && s->cell[at(s, cx, y)] != RG_LV_NONE) {
            *out = s->cell[at(s, cx, y)];
            return true;
        }
    }
    return false;
}

/* Both fix-point loops apply their results after the whole pass (upstream's `found` dict): the set order is neutral. */
static void apply_found(SD *s, uint8_t *set, bool *any)
{
    size_t i;

    *any = false;
    for (i = 0; i < s->n; i++)
        if (s->foundSet[i]) {
            s->cell[i] = s->foundV[i];
            set[i] = 0;
            s->foundSet[i] = 0;
            *any = true;
        }
}

static void thin_loop(SD *s)
{
    size_t cap;

    for (cap = 0; cap <= s->n; cap++) {
        int x, y;
        bool any = false, left = false;
        size_t i;

        for (i = 0; i < s->n; i++)
            left = left || s->thin[i];
        if (!left)
            return;
        for (y = 0; y < s->ch; y++)
            for (x = 0; x < s->cw; x++)
                if (s->thin[at(s, x, y)] && thin_cell(s, x, y, &s->foundV[at(s, x, y)]))
                    s->foundSet[at(s, x, y)] = 1;
        apply_found(s, s->thin, &any);
        if (!any)
            return;
    }
    assert(!"thin loop bound");
}

/* ---- phase C: bridges and flat cells drawn in rock colours (rel:1970-1986) ---- */

static bool neighbour_level(const SD *s, int cx, int cy, int32_t *out)
{
    static const int ox[4] = {0, 0, -1, 1}, oy[4] = {-1, 1, 0, 0};
    int32_t v[4];
    int cnt[4], nv = 0, k, j, best = 0;

    for (k = 0; k < 4; k++) {
        int x = cx + ox[k], y = cy + oy[k];
        size_t i;

        if (!inb(s, x, y))
            continue;
        i = at(s, x, y);
        if (s->cell[i] == RG_LV_NONE || s->flat[i] == RG_FLAT_WATER || s->lfn[i] != 0)
            continue;
        for (j = 0; j < nv && v[j] != s->cell[i]; j++) {}
        if (j == nv) {
            v[nv] = s->cell[i];
            cnt[nv++] = 0;
        }
        cnt[j]++;
    }
    if (nv == 0)
        return false;
    for (k = 1; k < nv; k++)
        if (cnt[k] > cnt[best])
            best = k;
    *out = v[best];
    return true;
}

static void lfn_loop(SD *s)
{
    size_t cap;

    for (cap = 0; cap <= s->n; cap++) {
        int x, y;
        bool any = false, left = false;
        size_t i;

        for (i = 0; i < s->n; i++)
            left = left || s->lfn[i];
        if (!left)
            return;
        for (y = 0; y < s->ch; y++)
            for (x = 0; x < s->cw; x++)
                if (s->lfn[at(s, x, y)] && neighbour_level(s, x, y, &s->foundV[at(s, x, y)]))
                    s->foundSet[at(s, x, y)] = 1;
        apply_found(s, s->lfn, &any);
        if (!any)
            return;
    }
    assert(!"lfn loop bound");
}

/* ---- phase D: every body of water at one level (rel:1988-2030) ---- */

typedef struct Cnt { int32_t v; unsigned n; } Cnt;

/* Counter(...).most_common(1): the first value of the largest count, in first-seen order. */
static void cnt_add(Cnt *c, unsigned *nc, int32_t v)
{
    unsigned k;

    for (k = 0; k < *nc && c[k].v != v; k++) {}
    if (k == *nc) {
        c[k].v = v;
        c[k].n = 0;
        (*nc)++;
    }
    c[k].n++;
}

static int32_t cnt_best(const Cnt *c, unsigned nc)
{
    unsigned k, best = 0;

    for (k = 1; k < nc; k++)
        if (c[k].n > c[best].n)
            best = k;
    return c[best].v;
}

/* rel:2018-2030: a patch of shore (<= SHORE_STRIP cells of floor, reached from nowhere else) is the water's. */
static void shore_strips(SD *s, int x, int y, int32_t lv)
{
    static const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
    int d;

    for (d = 0; d < 4; d++) {
        int qx = x + dx[d], qy = y + dy[d], sp = 0, ns = 0, k;
        int stk[48], strip[48];

        if (!inb(s, qx, qy) || s->flat[at(s, qx, qy)] != RG_FLAT_FLOOR)
            continue;
        strip[ns++] = (int)at(s, qx, qy);
        stk[sp++] = (int)at(s, qx, qy);
        while (sp > 0 && ns <= RG_SHORE_STRIP) {
            int c = stk[--sp], cx = c % s->cw, cy = c / s->cw, e;

            for (e = 0; e < 4; e++) {
                int rx = cx + dx[e], ry = cy + dy[e], j, dup = 0;

                if (!inb(s, rx, ry) || s->flat[at(s, rx, ry)] != RG_FLAT_FLOOR)
                    continue;
                for (j = 0; j < ns; j++)
                    dup = dup || strip[j] == (int)at(s, rx, ry);
                if (!dup) {
                    strip[ns++] = (int)at(s, rx, ry);
                    stk[sp++] = (int)at(s, rx, ry);
                }
            }
        }
        if (ns <= RG_SHORE_STRIP)
            for (k = 0; k < ns; k++) {
                s->cell[strip[k]] = lv;
                s->soil[strip[k]] = 1;
            }
    }
}

static void one_water(SD *s, uint8_t *seen, int32_t *stack, int32_t *body, int x0, int y0, Cnt *cnts)
{
    static const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
    int sp = 0, nb = 0, k, i;
    unsigned nc = 0;

    stack[sp++] = (int32_t)at(s, x0, y0);
    seen[at(s, x0, y0)] = 1;
    while (sp > 0) {
        int c = stack[--sp], cx = c % s->cw, cy = c / s->cw;

        if (s->flat[c] == RG_FLAT_WATER)
            body[nb++] = c;                  /* body = [c for c in body if flat == water]: the pop order, water only */
        for (k = 0; k < 4; k++) {
            int qx = cx + dx[k], qy = cy + dy[k];

            if (inb(s, qx, qy) && !seen[at(s, qx, qy)] &&
                (s->flat[at(s, qx, qy)] == RG_FLAT_WATER || s->flat[at(s, qx, qy)] == RG_FLAT_BRIDGE)) {
                seen[at(s, qx, qy)] = 1;
                stack[sp++] = (int32_t)at(s, qx, qy);
            }
        }
    }
    for (i = 0; i < nb; i++)
        if (s->cell[body[i]] != RG_LV_NONE)
            cnt_add(cnts, &nc, s->cell[body[i]]);
    if (nc != 0) {
        int32_t lv = cnt_best(cnts, nc);

        for (i = 0; i < nb; i++) {
            s->cell[body[i]] = lv;
            s->soil[body[i]] = 1;
        }
        for (i = 0; i < nb; i++)
            shore_strips(s, body[i] % s->cw, body[i] / s->cw, lv);
    }
}

static bool water_loop(SD *s)
{
    uint8_t *seen = (uint8_t *)calloc(s->n, 1);
    int32_t *stack = (int32_t *)malloc(s->n * sizeof(int32_t));
    int32_t *body = (int32_t *)malloc(s->n * sizeof(int32_t));
    Cnt *cnts = (Cnt *)malloc((s->n + 1u) * sizeof(Cnt));
    int x, y;
    bool ok = seen != NULL && stack != NULL && body != NULL && cnts != NULL;

    for (y = 0; ok && y < s->ch; y++)
        for (x = 0; x < s->cw; x++)
            if (s->flat[at(s, x, y)] == RG_FLAT_WATER && !seen[at(s, x, y)])
                one_water(s, seen, stack, body, x, y, cnts);
    free(seen);
    free(stack);
    free(body);
    free(cnts);
    return ok;
}

/* ---- phase E: ridge tops counted across bands (rel:2032-2073) ---- */

static bool rock_end(const SD *s, int x, int cy)
{
    return x >= 0 && x < s->cw && s->cell[at(s, x, cy)] == RG_LV_NONE && s->side[at(s, x, cy)] == 0 &&
           s->voidc[at(s, x, cy)] == 0 && cy + 1 >= 0 && cy + 1 < s->ch && s->side[at(s, x, cy + 1)] != 0;
}

static int32_t counted(const SD *s, int x, int cy, int dx)
{
    int n = 0;

    while (x >= 0 && x < s->cw && s->side[at(s, x, cy)] == -dx && s->cell[at(s, x, cy)] == RG_LV_NONE) {
        n++;
        x += dx;
    }
    if (n != 0 && x >= 0 && x < s->cw && s->cell[at(s, x, cy)] != RG_LV_NONE)
        return s->cell[at(s, x, cy)] + LEVEL * n;
    if (rock_end(s, x, cy))
        return OPEN;
    return RG_LV_NONE;
}

static bool ridge_run_free(const SD *s, int x, int cy)
{
    size_t i = at(s, x, cy);

    return s->cell[i] != RG_LV_NONE && s->soil[i] == 0 && s->side[i] == 0;
}

static void ridge_row(SD *s, int cy)
{
    int cx = 0;

    while (cx < s->cw) {
        int x0, x;
        int32_t west, east;

        if (!ridge_run_free(s, cx, cy)) {
            cx++;
            continue;
        }
        x0 = cx;
        while (cx < s->cw && ridge_run_free(s, cx, cy))
            cx++;
        west = counted(s, x0 - 1, cy, -1);
        east = counted(s, cx, cy, 1);
        if (west == OPEN && east == OPEN) {
            bool ok = cy + 1 < s->ch;
            int32_t first = RG_LV_NONE;

            for (x = x0; ok && x < cx; x++) {
                int32_t b = s->cell[at(s, x, cy + 1)];

                if (x == x0)
                    first = b;
                ok = b != RG_LV_NONE && b == first;
            }
            west = east = ok ? first : RG_LV_NONE;
        }
        if (west == OPEN)
            west = east;
        if (east == OPEN)
            east = west;
        if (west != RG_LV_NONE && west == east && cx - x0 <= RG_RIDGE_TOP)
            for (x = x0; x < cx; x++)
                s->cell[at(s, x, cy)] = west;
    }
}

/* ---- phase F: rock cells, what they hang from (rel:2075-2175) ---- */

static unsigned hangs_from(const SD *s, int cx, int cy, int hx[8], int hy[8])
{
    int dx, dy;
    unsigned n = 0;
    int t = tile_of(s, cx, cy);

    if (t == RG_RT_FACE) {
        hx[0] = cx;
        hy[0] = cy - 1;
        return 1;
    }
    if (t == RG_RT_BAND) {
        hx[0] = s->crest[at(s, cx, cy)] ? cx : cx + s->side[at(s, cx, cy)];
        hy[0] = s->crest[at(s, cx, cy)] ? cy - 1 : cy;
        return 1;
    }
    for (dy = -1; dy <= 1; dy++)
        for (dx = -1; dx <= 1; dx++)
            if (dx != 0 || dy != 0) {
                hx[n] = cx + dx;
                hy[n++] = cy + dy;
            }
    return n;
}

static int32_t height_of(const SD *s, int x, int y, int ax, int ay)
{
    size_t i;

    if (!inb(s, x, y) || s->voidc[at(s, x, y)] != 0)
        return RG_LV_NONE;
    i = at(s, x, y);
    if (s->cell[i] != RG_LV_NONE)
        return s->cell[i];
    if (s->top[i] == RG_LV_NONE)
        return RG_LV_NONE;
    if (s->crest[i] != 0 && !(ax == x - s->side[i] && ay == y))
        return s->top[i];
    if (s->crest[at(s, ax, ay)] != 0 && ax == x && ay == y + 1 && s->side[i] != 0)
        return s->top[i];
    return s->top[i] - LEVEL;
}

static int32_t landing(const SD *s, int cx, int cy)
{
    int t = tile_of(s, cx, cy);

    if (t == RG_RT_CORNER) {
        int hx[8], hy[8], k, n = (int)hangs_from(s, cx, cy, hx, hy);
        int32_t best = RG_LV_NONE;

        for (k = 0; k < n; k++)
            if (inb(s, hx[k], hy[k]) && s->cell[at(s, hx[k], hy[k])] != RG_LV_NONE &&
                (best == RG_LV_NONE || s->cell[at(s, hx[k], hy[k])] < best))
                best = s->cell[at(s, hx[k], hy[k])];
        return best;
    }
    {
        int dx = t == RG_RT_FACE ? 0 : -s->side[at(s, cx, cy)], dy = t == RG_RT_FACE ? 1 : 0;
        int x = cx + dx, y = cy + dy;

        while (inb(s, x, y) && s->voidc[at(s, x, y)] == 0) {
            if (s->cell[at(s, x, y)] != RG_LV_NONE)
                return s->cell[at(s, x, y)];
            x += dx;
            y += dy;
        }
    }
    return RG_LV_NONE;
}

static void mark_crests(SD *s, unsigned *nCrests)
{
    int x, y;

    *nCrests = 0;
    for (y = 0; y < s->ch; y++)
        for (x = 0; x < s->cw; x++) {
            size_t i = at(s, x, y);
            int sd = s->side[i], nx = x + sd;

            if (sd != 0 && s->cell[i] == RG_LV_NONE && s->voidc[i] == 0 && nx >= 0 && nx < s->cw &&
                s->side[at(s, nx, y)] == -sd && s->cell[at(s, nx, y)] == RG_LV_NONE) {
                s->crest[i] = 1;
                (*nCrests)++;
            }
        }
}

/* rel:2132-2147: a crest stands a level over its foot for every band down to it; its two halves are one line. */
static void crest_tops(SD *s)
{
    int x, y;

    for (x = 0; x < s->cw; x++)                       /* sorted(crests): (cx, cy) tuple order */
        for (y = 0; y < s->ch; y++) {
            int sd, xx, n = 0;

            if (!s->crest[at(s, x, y)])
                continue;
            sd = s->side[at(s, x, y)];
            xx = x;
            while (xx >= 0 && xx < s->cw && s->side[at(s, xx, y)] == sd && s->cell[at(s, xx, y)] == RG_LV_NONE) {
                n++;
                xx -= sd;
            }
            if (xx >= 0 && xx < s->cw && s->cell[at(s, xx, y)] != RG_LV_NONE)
                s->top[at(s, x, y)] = s->cell[at(s, xx, y)] + LEVEL * n;
        }
    for (x = 0; x < s->cw; x++)
        for (y = 0; y < s->ch; y++) {
            size_t i = at(s, x, y), o;

            if (!s->crest[i])
                continue;
            o = at(s, x + s->side[i], y);
            if (s->top[i] != RG_LV_NONE && s->top[o] != RG_LV_NONE) {
                int32_t m = s->top[i] > s->top[o] ? s->top[i] : s->top[o];

                s->top[i] = s->top[o] = m;
            } else if (s->top[o] != RG_LV_NONE && s->crest[o]) {
                s->top[i] = s->top[o];
            }
        }
}

static void band_neighbour_tops(const SD *s, int cx, int cy, int32_t *got, unsigned *ng)
{
    size_t i = at(s, cx, cy);
    int sd = s->side[i], y;

    if (!(sd != 0 && !s->crest[i] && cx + sd >= 0 && cx + sd < s->cw && s->cell[at(s, cx + sd, cy)] == RG_LV_NONE &&
          tile_of(s, cx + sd, cy) == RG_RT_CORNER))
        return;
    for (y = cy - 1; y <= cy + 1; y += 2)
        if (y >= 0 && y < s->ch && s->side[at(s, cx, y)] == sd && s->top[at(s, cx, y)] != RG_LV_NONE &&
            !s->crest[at(s, cx, y)] && s->cell[at(s, cx, y)] == RG_LV_NONE)
            got[(*ng)++] = s->top[at(s, cx, y)];
}

/* rel:2149-2164: tops of the rock cells, raised until nothing changes (in place, row-major). */
static void rock_tops(SD *s)
{
    size_t cap;

    for (cap = 0; cap <= s->n + 1u; cap++) {
        bool changed = false;
        unsigned r;

        for (r = 0; r < s->nRock; r++) {
            int cx = s->rock[r] % s->cw, cy = s->rock[r] / s->cw, hx[8], hy[8], k, nh;
            int32_t got[12], v;
            unsigned ng = 0;
            size_t i = at(s, cx, cy);

            nh = (int)hangs_from(s, cx, cy, hx, hy);
            for (k = 0; k < nh; k++) {
                int32_t hv = height_of(s, hx[k], hy[k], cx, cy);

                if (hv != RG_LV_NONE)
                    got[ng++] = hv;
            }
            band_neighbour_tops(s, cx, cy, got, &ng);
            if (ng == 0)
                continue;
            v = got[0];
            for (k = 1; k < (int)ng; k++)
                if (got[k] > v)
                    v = got[k];
            if (s->floorOf[i] != RG_LV_NONE && s->floorOf[i] > v)
                v = s->floorOf[i];
            if (s->top[i] == RG_LV_NONE || v > s->top[i]) {
                s->top[i] = v;
                changed = true;
            }
        }
        if (!changed)
            return;
    }
    assert(!"rock_tops bound");
}

/* ---- phase G/H: lattice, shapes, lay() (rel:2177-2297) ---- */

typedef struct Shape {
    int16_t cx, cy;
    int32_t hi, floor;
    uint8_t nr, nh, hasHeld, isBand;
    int16_t rx[8], ry[8];
    int16_t hi_[20], hj[20];
    int32_t hv[20];
    uint8_t hset[20];
} Shape;

static void init_lattice(const SD *s, double *h, uint8_t *fixed)
{
    int LW = s->cw * RG_P + 1, cx, cy, i, j;

    for (cy = 0; cy < s->ch; cy++)
        for (cx = 0; cx < s->cw; cx++) {
            size_t c = at(s, cx, cy);
            double lv;

            if (s->cell[c] == RG_LV_NONE)
                continue;
            lv = (double)s->cell[c];
            for (j = cy * RG_P; j <= (cy + 1) * RG_P; j++)
                for (i = cx * RG_P; i <= (cx + 1) * RG_P; i++) {
                    size_t p = (size_t)j * (size_t)LW + (size_t)i;

                    if (s->soil[c]) {
                        if (!fixed[p] || lv > h[p])
                            h[p] = lv;
                        fixed[p] = 1;
                    } else if (!fixed[p] && (isnan(h[p]) || lv > h[p])) {
                        h[p] = lv;
                    }
                }
        }
}

static void held_add(Shape *sh, int i, int j, int32_t v, bool has)
{
    unsigned k;

    for (k = 0; k < sh->nh; k++)
        if (sh->hi_[k] == i && sh->hj[k] == j)
            return;                          /* dict.fromkeys: first occurrence kept */
    sh->hi_[sh->nh] = (int16_t)i;
    sh->hj[sh->nh] = (int16_t)j;
    sh->hv[sh->nh] = v;
    sh->hset[sh->nh++] = has;
}

static void held_edges(Shape *sh, const SD *s, int cx, int cy, bool crest, bool flatOn, int t)
{
    int i0 = cx * RG_P, j0 = cy * RG_P, k, sd = s->side[at(s, cx, cy)];

    sh->nh = 0;
    sh->hasHeld = 1;
    if (crest) {
        for (k = 0; k <= RG_P; k++)
            held_add(sh, sd > 0 ? i0 + RG_P : i0, j0 + k, sh->hi, true);
    } else if (flatOn) {
        sh->hasHeld = 0;
    } else if (t == RG_RT_FACE) {
        for (k = 0; k <= RG_P; k++)
            held_add(sh, i0 + k, j0, 0, false);
    } else if (t == RG_RT_BAND) {
        for (k = 0; k <= RG_P; k++)
            held_add(sh, sd > 0 ? i0 + RG_P : i0, j0 + k, 0, false);
    } else {
        for (k = 0; k <= RG_P; k++) held_add(sh, i0 + k, j0, 0, false);
        for (k = 0; k <= RG_P; k++) held_add(sh, i0 + k, j0 + RG_P, 0, false);
        for (k = 0; k <= RG_P; k++) held_add(sh, i0, j0 + k, 0, false);
        for (k = 0; k <= RG_P; k++) held_add(sh, i0 + RG_P, j0 + k, 0, false);
    }
}

static bool make_shape(const SD *s, int cx, int cy, Shape *sh)
{
    int hx[8], hy[8], nh, k, t, bx[8], by[8], nb = 0;
    int32_t hi = s->top[at(s, cx, cy)], land = RG_LV_NONE;
    bool flatOn, crest = s->crest[at(s, cx, cy)] != 0;

    if (hi == RG_LV_NONE)
        return false;
    memset(sh, 0, sizeof *sh);
    sh->cx = (int16_t)cx;
    sh->cy = (int16_t)cy;
    sh->hi = hi;
    nh = (int)hangs_from(s, cx, cy, hx, hy);
    for (k = 0; k < nh; k++)
        if (height_of(s, hx[k], hy[k], cx, cy) == hi) {
            sh->rx[sh->nr] = (int16_t)hx[k];
            sh->ry[sh->nr++] = (int16_t)hy[k];
        }
    flatOn = sh->nr == 0;
    if (flatOn)
        for (k = 0; k < nh; k++)
            if (height_of(s, hx[k], hy[k], cx, cy) != RG_LV_NONE) {
                sh->rx[sh->nr] = (int16_t)hx[k];
                sh->ry[sh->nr++] = (int16_t)hy[k];
            }
    if (crest) {
        sh->nr = 1;
        sh->rx[0] = (int16_t)(cx + s->side[at(s, cx, cy)]);
        sh->ry[0] = (int16_t)cy;
    }
    t = tile_of(s, cx, cy);
    if (t == RG_RT_FACE) {
        bx[0] = cx; by[0] = cy + 1; nb = 1;
    } else if (t == RG_RT_BAND) {
        bx[0] = cx - s->side[at(s, cx, cy)]; by[0] = cy; nb = 1;
    } else {
        for (k = 0; k < nh; k++) {
            unsigned q;
            bool inR = false;

            for (q = 0; q < sh->nr; q++)
                inR = inR || (sh->rx[q] == hx[k] && sh->ry[q] == hy[k]);
            if (!inR) {
                bx[nb] = hx[k];
                by[nb++] = hy[k];
            }
        }
    }
    for (k = 0; k < nb; k++)
        if (inb(s, bx[k], by[k]) && s->cell[at(s, bx[k], by[k])] != RG_LV_NONE && s->cell[at(s, bx[k], by[k])] <= hi &&
            (land == RG_LV_NONE || s->cell[at(s, bx[k], by[k])] < land))
            land = s->cell[at(s, bx[k], by[k])];
    sh->floor = land != RG_LV_NONE ? land : hi - LEVEL;
    sh->isBand = t == RG_RT_BAND;
    held_edges(sh, s, cx, cy, crest, flatOn, t);
    return true;
}

static double ihyp(int dx, int dy)
{
    return sqrt((double)(dx * dx + dy * dy));        /* D3: exact integer square sum, correctly rounded */
}

/* The source points a held shape stands on (rel:2236-2248). Returns false when it has none. */
static bool lay_sources(const Shape *sh, const double *h, int LW, int *sx, int *sy, double *sv, unsigned *nSrc)
{
    unsigned k;

    *nSrc = 0;
    for (k = 0; k < sh->nh; k++) {
        double v = sh->hset[k] ? (double)sh->hv[k] : h[(size_t)sh->hj[k] * (size_t)LW + (size_t)sh->hi_[k]];

        if (!isnan(v) && v >= (double)sh->floor) {
            sx[*nSrc] = sh->hi_[k] * RG_P;           /* p[0] * STEP, STEP == PER_CELL == 4 */
            sy[*nSrc] = sh->hj[k] * RG_P;
            sv[*nSrc] = v < (double)sh->hi ? v : (double)sh->hi;
            (*nSrc)++;
        }
    }
    return *nSrc != 0;
}

static double rect_dist(const Shape *sh, int px, int py)
{
    double d = 0.0;
    unsigned k;

    for (k = 0; k < sh->nr; k++) {
        int x = sh->rx[k], y = sh->ry[k];
        int mx = x * 16 - px, my = y * 16 - py, bx = px - x * 16 - 16, by = py - y * 16 - 16;
        double dd;

        if (mx < 0) mx = 0;
        if (my < 0) my = 0;
        if (bx > mx) mx = bx;
        if (by > my) my = by;
        dd = ihyp(mx, my);
        if (k == 0 || dd < d)
            d = dd;
    }
    return d;
}

static bool lay(const Shape *sh, double *h, const uint8_t *fixed, int LW)
{
    int sx[20], sy[20], i, j, cx = sh->cx, cy = sh->cy;
    double sv[20], slope = (double)(sh->hi - sh->floor) / 16.0;
    unsigned nSrc = 0, k;
    bool changed = false;

    if (sh->hasHeld) {
        if (!lay_sources(sh, h, LW, sx, sy, sv, &nSrc))
            return false;
        if (sh->isBand) {
            double m = sv[0];

            for (k = 1; k < nSrc; k++)
                if (sv[k] > m)
                    m = sv[k];
            slope = ((m < (double)sh->hi ? m : (double)sh->hi) - (double)sh->floor) / 16.0;
        }
        if (slope > 1.0)
            slope = 1.0;
    }
    for (j = cy * RG_P; j <= (cy + 1) * RG_P; j++)
        for (i = cx * RG_P; i <= (cx + 1) * RG_P; i++) {
            int px = i * RG_P, py = j * RG_P;
            size_t p = (size_t)j * (size_t)LW + (size_t)i;
            double v;

            if (fixed[p])
                continue;
            if (!sh->hasHeld) {
                double f = rect_dist(sh, px, py) / 16.0;

                v = (double)sh->hi - (double)(sh->hi - sh->floor) * (1.0 < f ? 1.0 : f);
            } else {
                double best = 0.0, c;

                for (k = 0; k < nSrc; k++) {
                    c = sv[k] - slope * ihyp(px - sx[k], py - sy[k]);
                    if (k == 0 || c > best)
                        best = c;
                }
                v = (double)sh->floor > best ? (double)sh->floor : best;
            }
            if (isnan(h[p]) || v > h[p] + 1e-6) {
                h[p] = v;
                changed = true;
            }
        }
    return changed;
}

static int cmp_shape(const void *a, const void *b)
{
    const Shape *x = (const Shape *)a, *y = (const Shape *)b;

    if (x->hi != y->hi)
        return x->hi > y->hi ? -1 : 1;                /* -hi ascending */
    if (x->cy != y->cy)
        return x->cy < y->cy ? -1 : 1;
    return x->cx < y->cx ? -1 : (x->cx > y->cx ? 1 : 0);
}

static RgErr lay_all(SD *s, double *h, const uint8_t *fixed, RgSolvedGroup *g)
{
    Shape *sh = (Shape *)malloc(((size_t)s->nRock + 1u) * sizeof *sh);
    unsigned r, nS = 0, sweep;
    int LW = s->cw * RG_P + 1;

    if (sh == NULL)
        return RG_ERR_NOMEM;
    for (r = 0; r < s->nRock; r++)
        if (make_shape(s, s->rock[r] % s->cw, s->rock[r] / s->cw, &sh[nS]))
            nS++;
    qsort(sh, nS, sizeof *sh, cmp_shape);
    g->nShapes = nS;
    g->layCapped = 1;
    for (sweep = 0; sweep < RG_LAY_SWEEPS; sweep++) {
        bool any = false;

        for (r = 0; r < nS; r++)
            any = lay(&sh[r], h, fixed, LW) || any;       /* every cell runs each sweep (list comprehension) */
        if (!any) {
            sweep++;
            g->layCapped = 0;
            break;
        }
    }
    g->laySweeps = sweep;
    free(sh);
    return RG_OK;
}

/* ---- the whole solve ---- */

static void sd_free(SD *s)
{
    free(s->cell); free(s->top); free(s->floorOf); free(s->foundV);
    free(s->voidc); free(s->soil); free(s->thin); free(s->lfn); free(s->crest); free(s->tile); free(s->foundSet);
    free(s->rock);
}

static bool sd_init(SD *s, const RgPrep *p, const int32_t *level)
{
    size_t i;

    memset(s, 0, sizeof *s);
    s->cw = p->cv.cw;
    s->ch = p->cv.ch;
    s->n = (size_t)s->cw * (size_t)s->ch;
    s->p = p;
    s->level = level;
    s->side = p->cv.side;
    s->flat = p->cv.flat;
    s->freeMid = p->freeMid;
    s->faceLow = p->cv.faceLow;
    s->meta = p->cv.meta;
    s->cell = (int32_t *)malloc(s->n * sizeof(int32_t));
    s->top = (int32_t *)malloc(s->n * sizeof(int32_t));
    s->floorOf = (int32_t *)malloc(s->n * sizeof(int32_t));
    s->foundV = (int32_t *)malloc(s->n * sizeof(int32_t));
    s->rock = (int32_t *)malloc(s->n * sizeof(int32_t));
    s->voidc = (uint8_t *)malloc(s->n);
    s->soil = (uint8_t *)calloc(s->n, 1);
    s->thin = (uint8_t *)calloc(s->n, 1);
    s->lfn = (uint8_t *)calloc(s->n, 1);
    s->crest = (uint8_t *)calloc(s->n, 1);
    s->tile = (uint8_t *)calloc(s->n, 1);
    s->foundSet = (uint8_t *)calloc(s->n, 1);
    if (!s->cell || !s->top || !s->floorOf || !s->foundV || !s->rock || !s->voidc || !s->soil || !s->thin || !s->lfn ||
        !s->crest || !s->tile || !s->foundSet)
        return false;
    for (i = 0; i < s->n; i++) {
        s->cell[i] = s->top[i] = s->floorOf[i] = RG_LV_NONE;
        s->voidc[i] = 1;
    }
    return true;
}

/* rel:1905-2164: fills cell, soil, void, then the rock list, tiles, floors, crest tops, rock tops. */
static RgErr shape_cells(SD *s, RgSolvedGroup *g)
{
    int x, y;

    for (y = 0; y < s->ch; y++)
        for (x = 0; x < s->cw; x++)
            if (s->p->stats[at(s, x, y)].n != 0)
                footprint_cell(s, x, y);
    thin_loop(s);
    lfn_loop(s);
    if (!water_loop(s))
        return RG_ERR_NOMEM;
    for (y = s->ch - 1; y >= 0; y--)
        ridge_row(s, y);
    mark_crests(s, &g->nCrests);
    for (y = 0; y < s->ch; y++)
        for (x = 0; x < s->cw; x++)
            if (s->cell[at(s, x, y)] == RG_LV_NONE && !s->voidc[at(s, x, y)])
                s->rock[s->nRock++] = (int32_t)at(s, x, y);
    for (x = 0; x < (int)s->nRock; x++) {
        int cx = s->rock[x] % s->cw, cy = s->rock[x] / s->cw;

        s->tile[s->rock[x]] = (uint8_t)tile_of(s, cx, cy);
        s->floorOf[s->rock[x]] = landing(s, cx, cy);
    }
    crest_tops(s);
    rock_tops(s);
    g->nRock = s->nRock;
    return RG_OK;
}

static int32_t member_base(const RgSolvedGroup *g, const RgCanvasMember *m)
{
    Cnt *c = (Cnt *)malloc(((size_t)m->w * m->h + 1u) * sizeof(Cnt));
    unsigned nc = 0;
    int x, y;
    int32_t r = 0;

    if (c == NULL)
        return 0;
    for (y = 0; y < m->h; y++)
        for (x = 0; x < m->w; x++) {
            size_t i = (size_t)(m->oy + y) * (size_t)g->cw + (size_t)(m->ox + x);

            if (g->cell[i] != RG_LV_NONE && g->soil[i])
                cnt_add(c, &nc, g->cell[i]);
        }
    if (nc != 0)
        r = cnt_best(c, nc);
    free(c);
    return r;
}

static RgErr export_members(const RgDrawn *d, unsigned gi, const SD *s, double *h, RgSolved *out, RgSolvedGroup *g)
{
    int LW = s->cw * RG_P + 1, LH = s->ch * RG_P + 1;
    unsigned mi;
    size_t i;
    int32_t *bases = (int32_t *)malloc(((size_t)g->nMem + 1u) * sizeof(int32_t));

    if (bases == NULL)
        return RG_ERR_NOMEM;
    for (mi = 0; mi < g->nMem; mi++)
        bases[mi] = member_base(g, &g->mem[mi]);
    for (mi = 0; mi < g->nMem; mi++) {                       /* a point nothing stands on is at its map's level */
        const RgCanvasMember *m = &g->mem[mi];
        int i0, j0;

        for (j0 = m->oy * RG_P; j0 <= (m->oy + m->h) * RG_P; j0++)
            for (i0 = m->ox * RG_P; i0 <= (m->ox + m->w) * RG_P; i0++)
                if (isnan(h[(size_t)j0 * (size_t)LW + (size_t)i0]))
                    h[(size_t)j0 * (size_t)LW + (size_t)i0] = (double)bases[mi];
    }
    for (i = 0; i < (size_t)LW * (size_t)LH; i++)
        if (isnan(h[i]))
            h[i] = 0.0;
    g->minH = g->maxH = h[0];
    for (i = 1; i < (size_t)LW * (size_t)LH; i++) {
        if (h[i] < g->minH) g->minH = h[i];
        if (h[i] > g->maxH) g->maxH = h[i];
    }
    for (mi = 0; mi < g->nMem; mi++) {
        const RgCanvasMember *m = &g->mem[mi];
        uint16_t lid = m->layout;
        int x, y;
        RgLat *lat;

        if (rg_drawn_group(d, lid, false, NULL) != &d->groups[gi])
            continue;
        lat = &out->shift[lid];
        if (out->have[lid] || !rg_lat_new(lat, m->w, m->h)) {
            free(bases);
            return RG_ERR_NOMEM;
        }
        for (y = 0; y <= m->h * RG_P; y++)
            for (x = 0; x <= m->w * RG_P; x++)
                lat->v[(size_t)y * (size_t)(m->w * RG_P + 1) + (size_t)x] =
                    h[(size_t)(m->oy * RG_P + y) * (size_t)LW + (size_t)(m->ox * RG_P + x)] - (double)bases[mi];
        out->have[lid] = 1;
        out->groupOf[lid] = (uint8_t)gi;
        out->base[lid] = bases[mi];
    }
    free(bases);
    return RG_OK;
}

/* Hands the arrays S3.7 reads over to the result; the rest is freed by sd_free. */
static bool keep_group(RgSolvedGroup *g, SD *s, const RgPrep *p)
{
    g->cell = s->cell;      s->cell = NULL;
    g->top = s->top;        s->top = NULL;
    g->tile = s->tile;      s->tile = NULL;
    g->soil = s->soil;      s->soil = NULL;
    g->voidCell = s->voidc; s->voidc = NULL;
    g->side = (int8_t *)malloc(s->n);
    g->pier = (uint8_t *)malloc(s->n);
    g->meta = (uint16_t *)malloc(s->n * sizeof(uint16_t));
    g->mem = (RgCanvasMember *)malloc(((size_t)p->cv.nMem + 1u) * sizeof(RgCanvasMember));
    if (g->side == NULL || g->pier == NULL || g->meta == NULL || g->mem == NULL)
        return false;
    memcpy(g->side, p->cv.side, s->n);
    memcpy(g->pier, p->cv.pier, s->n);
    memcpy(g->meta, p->cv.meta, s->n * sizeof(uint16_t));
    memcpy(g->mem, p->cv.mem, (size_t)p->cv.nMem * sizeof(RgCanvasMember));
    g->nMem = p->cv.nMem;
    return true;
}

static void group_free(RgSolvedGroup *g)
{
    if (g == NULL)
        return;
    free(g->cell); free(g->top); free(g->tile); free(g->soil); free(g->pier); free(g->voidCell);
    free(g->side); free(g->meta); free(g->mem);
    free(g);
}

RgErr rg_solve_group(const RgWorld *w, const RgDrawn *d, unsigned gi, const RgPrep *p, const int32_t *level,
                     RgSolved *out)
{
    SD s;
    RgSolvedGroup *g;
    double *h = NULL;
    uint8_t *fixed = NULL;
    RgErr e = RG_ERR_NOMEM;
    size_t lat, i;

    assert(w != NULL && d != NULL && p != NULL && level != NULL && out != NULL && gi < d->nGroups);
    assert(out->grp[gi] == NULL);
    g = (RgSolvedGroup *)calloc(1, sizeof *g);
    if (g == NULL)
        return RG_ERR_NOMEM;
    g->key = d->groups[gi].key;
    g->isAlt = d->groups[gi].isAlt;
    g->cw = p->cv.cw;
    g->ch = p->cv.ch;
    if (!sd_init(&s, p, level))
        goto done;
    e = shape_cells(&s, g);
    if (e != RG_OK)
        goto done;
    e = RG_ERR_NOMEM;
    lat = (size_t)(s.cw * RG_P + 1) * (size_t)(s.ch * RG_P + 1);
    h = (double *)malloc(lat * sizeof(double));
    fixed = (uint8_t *)calloc(lat, 1);
    if (h == NULL || fixed == NULL)
        goto done;
    for (i = 0; i < lat; i++)
        h[i] = (double)NAN;
    init_lattice(&s, h, fixed);
    e = lay_all(&s, h, fixed, g);
    if (e != RG_OK)
        goto done;
    e = RG_ERR_NOMEM;
    if (keep_group(g, &s, p))
        e = export_members(d, gi, &s, h, out, g);
done:
    free(h);
    free(fixed);
    sd_free(&s);
    if (e == RG_OK)
        out->grp[gi] = g;
    else
        group_free(g);
    return e;
}

RgErr rg_solve_all(RgRCtx *c, const RgWorld *w, const RgDrawn *d, const RgLevels *lv, RgSolved *out)
{
    unsigned g;
    RgErr e = RG_OK;

    assert(c != NULL && w != NULL && d != NULL && lv != NULL && out != NULL);
    memset(out, 0, sizeof *out);
    out->nGroups = d->nGroups;
    for (g = 0; g < d->nGroups && e == RG_OK; g++) {
        RgPrep p;

        if (!lv->ok[g])
            continue;
        e = rg_prepare(c, d, &d->groups[g], &p);
        if (e == RG_OK && p.nRegions != lv->nLevel[g])
            e = RG_ERR_TABLES;
        if (e == RG_OK)
            e = rg_solve_group(w, d, g, &p, lv->level[g], out);
        rg_prep_free(&p);
    }
    if (e != RG_OK)
        rg_solved_free(out);
    return e;
}

void rg_solved_free(RgSolved *s)
{
    unsigned i;

    for (i = 0; i < RG_DRAWN_MAX_GROUPS; i++) {
        group_free(s->grp[i]);
        s->grp[i] = NULL;
    }
    for (i = 0; i < 512; i++)
        if (s->have[i]) {
            rg_lat_free(&s->shift[i]);
            s->have[i] = 0;
        }
}
