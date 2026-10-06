/* Modified for 3DGBA (GPLv3), 2026.
 * Original: ZallaxDev/pokeemerald-3Ds-dualscreen @ c330c0a, 3ds_port/scripts/gen_voxel_relief.py (ledge_cells,
 * ledge_berms, ledge_layouts), MIT License - see source/voxel/NOTICE.md.
 * Portions Copyright (c) Dust Zallax, MIT. */
#include "rg_ledge.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "rg_behavior.h"
#include "rg_geom.h"
#include "rg_roles.h"
#include "rg_rtables.h"

#define GROUND_BYTES 4096u       /* a 32768-bit set of BGR555 colours */

/* rel:181-183 JUMPS: 0x38 E, 0x39 W, 0x3A N, 0x3B S, 0x3C-0x3F a horizontal and a vertical arm. */
static unsigned jump_dirs(unsigned b, int8_t dx[4], int8_t dy[4])
{
    static const int8_t kH[8] = {1, -1, 0, 0, 1, -1, 1, -1};
    static const int8_t kV[8] = {0, 0, -1, 1, -1, -1, 1, 1};
    unsigned k = b - 0x38u, n = 0;

    assert(rg_is_jump(b));
    if (kH[k] != 0) {
        dx[n] = kH[k];
        dy[n++] = 0;
    }
    if (kV[k] != 0) {
        dx[n] = 0;
        dy[n++] = kV[k];
    }
    return n;
}

bool rg_ledge_enabled(uint16_t layoutId)
{
    return rg_is_enabled_layout(layoutId);
}

void rg_ledge_set_free(RgLedgeSet *s)
{
    assert(s != NULL);
    free(s->c);
    free(s->at);
    memset(s, 0, sizeof(*s));
}

static bool jump_pass(const RgLayout *L, RgLedgeSet *s)
{
    int x, y;

    for (y = 0; y < (int)L->h; y++) {
        for (x = 0; x < (int)L->w; x++) {
            unsigned b = rg_behaviour(L, x, y);
            RgLedgeCell *c;

            if (!rg_is_jump(b))
                continue;
            c = &s->c[s->n];
            c->x = (int16_t)x;
            c->y = (int16_t)y;
            c->nDirs = (uint8_t)jump_dirs(b, c->dx, c->dy);
            s->at[y * (int)L->w + x] = (int32_t)s->n++;
        }
    }
    s->nJump = s->n;
    return true;
}

/* A junction takes the directions of the jump cells beside it (rel:2355-2363): dirs.extend(d not in dirs). */
static void join_dirs(RgLedgeCell *j, const RgLedgeCell *from)
{
    unsigned a, b;

    for (a = 0; a < from->nDirs; a++) {
        bool have = false;

        for (b = 0; b < j->nDirs; b++)
            have = have || (j->dx[b] == from->dx[a] && j->dy[b] == from->dy[a]);
        if (!have && j->nDirs < 4u) {
            j->dx[j->nDirs] = from->dx[a];
            j->dy[j->nDirs++] = from->dy[a];
        }
    }
}

static void junction_pass(const RgLayout *L, RgLedgeSet *s)
{
    static const int kN[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    unsigned i, k;

    assert(s->n == s->nJump);
    for (i = 0; i < s->nJump; i++) {
        for (k = 0; k < 4; k++) {
            int nx = s->c[i].x + kN[k][0], ny = s->c[i].y + kN[k][1];
            int32_t at;

            if (rg_off(L, nx, ny) || !rg_blocked(L, nx, ny))
                continue;
            at = s->at[ny * (int)L->w + nx];
            if (at >= 0 && (unsigned)at < s->nJump)
                continue;               /* already a jump cell (the pre-update `out`) */
            if (!rg_is_ledge_junction(L, nx, ny))
                continue;
            if (at < 0) {
                RgLedgeCell *c = &s->c[s->n];

                c->x = (int16_t)nx;
                c->y = (int16_t)ny;
                c->nDirs = 0;
                s->at[ny * (int)L->w + nx] = (int32_t)s->n++;
                at = s->at[ny * (int)L->w + nx];
            }
            join_dirs(&s->c[at], &s->c[i]);
        }
    }
}

bool rg_ledge_cells(const RgLayout *L, bool junctions, RgLedgeSet *out)
{
    size_t cells;
    size_t i;

    assert(L != NULL && out != NULL);
    memset(out, 0, sizeof(*out));
    out->w = (int)L->w;
    out->h = (int)L->h;
    cells = (size_t)L->w * L->h;
    out->c = (RgLedgeCell *)calloc(cells ? cells : 1u, sizeof(RgLedgeCell));
    out->at = (int32_t *)malloc((cells ? cells : 1u) * sizeof(int32_t));
    if (out->c == NULL || out->at == NULL) {
        rg_ledge_set_free(out);
        return false;
    }
    for (i = 0; i < cells; i++)
        out->at[i] = -1;
    (void)jump_pass(L, out);
    if (junctions && !rg_ledge_enabled(L->id))
        junction_pass(L, out);
    return true;
}

unsigned rg_ledge_layouts(const RgWorld *w, uint16_t *out, unsigned cap)
{
    unsigned i, n = 0;
    int x, y;

    assert(w != NULL);
    for (i = 0; i < w->layoutCount; i++) {
        const RgLayout *L = &w->layouts[i];
        bool any = false;

        if (!L->present || !rg_relief_outdoor(L->id))
            continue;
        for (y = 0; y < (int)L->h && !any; y++)
            for (x = 0; x < (int)L->w && !any; x++)
                any = rg_is_jump(rg_behaviour(L, x, y));
        if (!any)
            continue;
        if (out != NULL && n < cap)
            out[n] = L->id;
        n++;
    }
    return n;
}

/* ---- the berm (rel:2367-2464) ---- */

typedef struct Lip {
    uint8_t *bits;               /* (16w x 16h) pixels, one bit each */
    int w16, h16;
} Lip;

static bool lip_at(const Lip *l, int px, int py)
{
    size_t i;

    if (px < 0 || py < 0 || px >= l->w16 || py >= l->h16)
        return false;
    i = (size_t)py * (size_t)l->w16 + (size_t)px;
    return (l->bits[i >> 3] >> (i & 7u)) & 1u;
}

static void lip_set(Lip *l, int px, int py)
{
    size_t i = (size_t)py * (size_t)l->w16 + (size_t)px;

    assert(px >= 0 && py >= 0 && px < l->w16 && py < l->h16);
    l->bits[i >> 3] |= (uint8_t)(1u << (i & 7u));
}

/* The 256 BGR555 colours of a whole metatile as the ground renderer draws it (vb:198-216 cell_image). */
static void cell_colours(RgPair *p, uint16_t m, uint16_t out[256])
{
    RgCellPx lo, hi;
    int x, y;

    rg_cell_px(p, m, 0, &lo);
    rg_cell_px(p, m, 1, &hi);
    for (y = 0; y < 16; y++)
        for (x = 0; x < 16; x++)
            out[y * 16 + x] = (uint16_t)((hi.idx[y][x] != 0 ? hi.c[y][x] : lo.c[y][x]) & 0x7FFFu);
}

static bool ground_add(RgPair *p, const RgLayout *L, int nx, int ny, uint8_t *set)
{
    uint16_t col[256];
    unsigned k;

    cell_colours(p, rg_metatile(L, nx, ny), col);
    for (k = 0; k < 256; k++)
        set[col[k] >> 3] |= (uint8_t)(1u << (col[k] & 7u));
    return true;
}

/* rel:2379-2392: the colours of the cells the jump leaves and lands on, else of the ring round it. */
static bool ground_colours(RgPair *p, const RgLayout *L, const RgLedgeSet *s, const RgLedgeCell *c, uint8_t *set)
{
    unsigned ring, k;
    bool any = false;

    memset(set, 0, GROUND_BYTES);
    for (ring = 0; ring < 2 && !any; ring++) {
        int ox[9], oy[9];
        unsigned n = 0;

        if (ring == 0) {
            for (k = 0; k < c->nDirs; k++) {
                ox[n] = -c->dx[k];
                oy[n++] = -c->dy[k];
            }
            for (k = 0; k < c->nDirs; k++) {
                ox[n] = c->dx[k];
                oy[n++] = c->dy[k];
            }
        } else {
            for (k = 0; k < 9; k++) {
                ox[n] = (int)(k / 3u) - 1;
                oy[n++] = (int)(k % 3u) - 1;
            }
        }
        for (k = 0; k < n; k++) {
            int nx = c->x + ox[k], ny = c->y + oy[k];

            if (rg_off(L, nx, ny) || s->at[ny * (int)L->w + nx] >= 0 || rg_blocked(L, nx, ny))
                continue;
            any = ground_add(p, L, nx, ny, set) || any;
        }
    }
    return any;
}

/* rel:2393-2401: every pixel of the ledge cell whose colour no ground round it has is lip. */
static bool lip_map(const RgLayout *L, RgPair *p, const RgLedgeSet *s, Lip *lip)
{
    uint8_t *ground = (uint8_t *)malloc(GROUND_BYTES);
    unsigned i;

    if (ground == NULL)
        return false;
    for (i = 0; i < s->n; i++) {
        const RgLedgeCell *c = &s->c[i];
        uint16_t col[256];
        unsigned k;

        if (!ground_colours(p, L, s, c, ground))
            continue;
        cell_colours(p, rg_metatile(L, c->x, c->y), col);
        for (k = 0; k < 256; k++)
            if (!((ground[col[k] >> 3] >> (col[k] & 7u)) & 1u))
                lip_set(lip, c->x * 16 + (int)(k & 15u), c->y * 16 + (int)(k >> 4));
    }
    free(ground);
    return true;
}

static int lip_run(const Lip *l, int px, int py, int dx, int dy)
{
    int n = 0;

    while (n < 64 && lip_at(l, px + n * dx, py + n * dy))
        n++;
    return n;
}

static int lip_first(const Lip *l, int px, int py, int dx, int dy, int limit)
{
    int k;

    for (k = 0; k < limit; k++)
        if (lip_at(l, px + k * dx, py + k * dy))
            return k;
    return -1;
}

static double lip_height(int f)
{
    return (double)(RG_LIP * (f < RG_LIP_WIDTH ? f : RG_LIP_WIDTH)) / (double)RG_LIP_WIDTH;
}

typedef struct Read {
    int sx, sy, dx, dy, f, b;
} Read;

/* rel:2412-2441: the berm height at one lattice point of one ledge cell. */
static double point_height(const Lip *lip, const RgLedgeCell *c, int i, int j)
{
    Read rd[4] = {{0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0}};
    int px = c->x * 16 + i * 4, py = c->y * 16 + j * 4;
    unsigned k;
    bool allF = true, inFront = false;
    double best;

    assert(c->nDirs >= 1 && c->nDirs <= 4 && i >= 0 && i <= RG_P && j >= 0 && j <= RG_P);
    for (k = 0; k < c->nDirs; k++) {
        int dx = c->dx[k], dy = c->dy[k], sx, sy;

        if (dx == 0) {
            sx = px < c->x * 16 ? c->x * 16 : (px > c->x * 16 + 15 ? c->x * 16 + 15 : px);
            sy = py - (dy < 0);
        } else {
            sx = px - (dx < 0);
            sy = py < c->y * 16 ? c->y * 16 : (py > c->y * 16 + 15 ? c->y * 16 + 15 : py);
        }
        rd[k] = (Read){sx, sy, dx, dy, lip_run(lip, sx, sy, dx, dy), lip_run(lip, sx - dx, sy - dy, -dx, -dy)};
        allF = allF && rd[k].f != 0;
        inFront = inFront || (rd[k].b != 0 && rd[k].f == 0);
    }
    if (allF) {
        best = lip_height(rd[0].f);
        for (k = 1; k < c->nDirs; k++)
            if (lip_height(rd[k].f) < best)
                best = lip_height(rd[k].f);
        return best;
    }
    if (inFront)
        return 0.0;
    best = 0.0;
    for (k = 0; k < c->nDirs; k++) {
        int q = lip_first(lip, rd[k].sx, rd[k].sy, rd[k].dx, rd[k].dy, RG_LIP_BACK);

        if (q >= 0) {
            double top = lip_height(lip_run(lip, rd[k].sx + q * rd[k].dx, rd[k].sy + q * rd[k].dy, rd[k].dx, rd[k].dy));
            double v = top * (1.0 - (double)q / (double)RG_LIP_BACK);

            if (v > best)
                best = v;
        }
    }
    return best;
}

/* rel:2442-2463: add the raised points, except where a cell that is no ledge shares the point. */
static void apply_raised(const RgLayout *L, const RgLedgeSet *s, const double *raised, RgLat *h)
{
    int gx, gy, gw = (int)L->w * RG_P + 1, gh = (int)L->h * RG_P + 1;

    for (gy = 0; gy < gh; gy++) {
        for (gx = 0; gx < gw; gx++) {
            double v = raised[(size_t)gy * (size_t)gw + (size_t)gx];
            int cx[2], cy[2], a, b;
            bool all = true;

            if (v < 0.0)
                continue;               /* never reached by a ledge cell */
            cx[0] = rg_floordiv(gx - 1, RG_P);
            cx[1] = rg_floordiv(gx, RG_P);
            cy[0] = rg_floordiv(gy - 1, RG_P);
            cy[1] = rg_floordiv(gy, RG_P);
            for (a = 0; a < 2; a++)
                for (b = 0; b < 2; b++)
                    if (cx[a] >= 0 && cx[a] < (int)L->w && cy[b] >= 0 && cy[b] < (int)L->h
                        && s->at[cy[b] * (int)L->w + cx[a]] < 0)
                        all = false;
            if (all)
                *rg_lat_at(h, gx, gy) += v;
        }
    }
}

static bool raise_points(const RgLayout *L, const RgLedgeSet *s, const Lip *lip, RgLat *h)
{
    size_t np = (size_t)((int)L->w * RG_P + 1) * (size_t)((int)L->h * RG_P + 1), q;
    double *raised = (double *)malloc(np * sizeof(double));
    unsigned i;
    int gw = (int)L->w * RG_P + 1;

    if (raised == NULL)
        return false;
    for (q = 0; q < np; q++)
        raised[q] = -1.0;
    for (i = 0; i < s->n; i++) {
        const RgLedgeCell *c = &s->c[i];
        int a, b;

        for (b = 0; b <= RG_P; b++) {
            for (a = 0; a <= RG_P; a++) {
                double *slot = &raised[(size_t)(c->y * RG_P + b) * (size_t)gw + (size_t)(c->x * RG_P + a)];
                double v = point_height(lip, c, a, b);

                if (v > *slot)
                    *slot = v;
            }
        }
    }
    apply_raised(L, s, raised, h);
    free(raised);
    return true;
}

int rg_ledge_berms(const RgLayout *L, RgPair *pair, RgLat *h)
{
    RgLedgeSet s;
    Lip lip;
    bool ok;
    int n;

    assert(L != NULL && pair != NULL && h != NULL);
    assert(h->w == (int)L->w && h->h == (int)L->h);
    if (!rg_ledge_cells(L, true, &s))
        return -1;
    if (s.n == 0) {
        rg_ledge_set_free(&s);
        return 0;
    }
    lip.w16 = (int)L->w * 16;
    lip.h16 = (int)L->h * 16;
    lip.bits = (uint8_t *)calloc(((size_t)lip.w16 * (size_t)lip.h16 + 7u) / 8u, 1u);
    ok = lip.bits != NULL && lip_map(L, pair, &s, &lip) && raise_points(L, &s, &lip, h);
    n = (int)s.n;
    free(lip.bits);
    rg_ledge_set_free(&s);
    return ok ? n : -1;
}
