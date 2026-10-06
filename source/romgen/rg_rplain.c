/* Modified for 3DGBA (GPLv3), 2026.
 * Original: ZallaxDev/pokeemerald-3Ds-dualscreen @ c330c0a, 3ds_port/scripts/gen_voxel_relief.py (solve, awash,
 * ledges_on_ground, pier_ends, layout_heights: rel:189-398, 2466-2595), MIT License - see source/voxel/NOTICE.md.
 * Portions Copyright (c) Dust Zallax, MIT. */
#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "rg_pyset.h"
#include "rg_rcanvas.h"
#include "rg_rsolve.h"
#include "rg_rrock.h"
#include "rg_rtables.h"
#include "voxel_regions.h"

enum { K_LAND = 0, K_RELIEF, K_WATER };
#define LEVEL 16

/* Everything solve() keeps for one layout: kind/role per cell, the level of each cell (RG_LV_NONE = None). */
typedef struct Plain {
    int w, h;
    const uint8_t *role;
    uint8_t *kind;
    int32_t *cell;
    int32_t *region;
    unsigned nRegions;
    int32_t *stack;              /* w*h scratch */
} Plain;

static bool is_relief_cell(const Plain *s, int i)
{
    return s->kind[i] == K_RELIEF && s->role[i] != VOXEL_ROLE_STAIR;     /* relief and not a stair */
}

/* rel:196-213: land regions, labelled in first-cell row-major order (stack flood; the labels do not depend on pop order). */
static void land_regions(Plain *s)
{
    static const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
    int x, y, n = s->w * s->h, i;

    for (i = 0; i < n; i++)
        s->region[i] = -1;
    s->nRegions = 0;
    for (y = 0; y < s->h; y++)
        for (x = 0; x < s->w; x++) {
            int sp = 0;

            if (s->kind[y * s->w + x] != K_LAND || s->region[y * s->w + x] >= 0)
                continue;
            s->stack[sp++] = y * s->w + x;
            s->region[y * s->w + x] = (int32_t)s->nRegions;
            while (sp > 0) {
                int c = s->stack[--sp], cx = c % s->w, cy = c / s->w, k;

                assert(sp < n);
                for (k = 0; k < 4; k++) {
                    int nx = cx + dx[k], ny = cy + dy[k], j = ny * s->w + nx;

                    if (nx >= 0 && nx < s->w && ny >= 0 && ny < s->h && s->kind[j] == K_LAND && s->region[j] < 0) {
                        s->region[j] = (int32_t)s->nRegions;
                        s->stack[sp++] = j;
                    }
                }
            }
            s->nRegions++;
        }
}

typedef struct Edge { int32_t up, down; } Edge;

/* rel:215-232: land above a rock run and land below it, counted in dict insertion order (counts are not read). */
static bool south_faces(const Plain *s, Edge **edges, unsigned *nEdges)
{
    unsigned cap = 64;
    int x, y;
    Edge *e = (Edge *)malloc(cap * sizeof *e);

    *nEdges = 0;
    if (e == NULL)
        return false;
    for (x = 0; x < s->w; x++) {
        y = 0;
        while (y < s->h) {
            int y0, up, down;
            unsigned k;

            if (!is_relief_cell(s, y * s->w + x)) {
                y++;
                continue;
            }
            y0 = y;
            while (y < s->h && is_relief_cell(s, y * s->w + x))
                y++;
            if (y0 > 0 && y < s->h && s->kind[(y0 - 1) * s->w + x] == K_LAND && s->kind[y * s->w + x] == K_LAND) {
                up = s->region[(y0 - 1) * s->w + x];
                down = s->region[y * s->w + x];
                if (up == down)
                    continue;
                for (k = 0; k < *nEdges && !(e[k].up == up && e[k].down == down); k++) {}
                if (k == *nEdges) {
                    if (*nEdges == cap) {
                        Edge *ne = (Edge *)realloc(e, cap * 2u * sizeof *e);

                        if (ne == NULL) {
                            free(e);
                            return false;
                        }
                        e = ne;
                        cap *= 2u;
                    }
                    e[(*nEdges)++] = (Edge){up, down};
                }
            }
        }
    }
    *edges = e;
    return true;
}

/* rel:234-255: breadth-first (the queue is popped from the END: a stack) "up is down + 1", first answer kept; each
 * component lowered to 0. adj[r] in insertion order = the edges scanned in order. */
static bool region_levels(const Plain *s, const Edge *e, unsigned nE, int32_t *level)
{
    int32_t *comp = (int32_t *)malloc(((size_t)s->nRegions + 1u) * sizeof(int32_t));
    int32_t *queue = (int32_t *)malloc(((size_t)s->nRegions + 1u) * sizeof(int32_t));
    unsigned start;

    if (comp == NULL || queue == NULL) {
        free(comp);
        free(queue);
        return false;
    }
    for (start = 0; start < s->nRegions; start++)
        level[start] = RG_LV_NONE;
    for (start = 0; start < s->nRegions; start++) {
        unsigned nc = 0, nq = 0, k;
        int32_t low;

        if (level[start] != RG_LV_NONE)
            continue;
        level[start] = 0;
        comp[nc++] = (int32_t)start;
        queue[nq++] = (int32_t)start;
        while (nq > 0) {
            int32_t r = queue[--nq];

            for (k = 0; k < nE; k++) {
                int32_t o = RG_LV_NONE, dd = 0;

                if (e[k].up == r) {
                    o = e[k].down;
                    dd = -1;
                } else if (e[k].down == r) {
                    o = e[k].up;
                    dd = +1;
                }
                if (o != RG_LV_NONE && level[o] == RG_LV_NONE) {
                    level[o] = level[r] + dd;
                    comp[nc++] = o;
                    queue[nq++] = o;
                }
            }
        }
        low = level[comp[0]];
        for (k = 1; k < nc; k++)
            if (level[comp[k]] < low)
                low = level[comp[k]];
        for (k = 0; k < nc; k++)
            level[comp[k]] -= low;
    }
    free(comp);
    free(queue);
    return true;
}

/* The sides some map using this layout is connected on, sorted by name (down, left, right, up) = ROM dirs 1,3,4,2. */
static void connected_sides(const RgWorld *w, uint16_t id, bool sides[5])
{
    unsigned m, n, k;
    RgConn cn[32];

    memset(sides, 0, 5u * sizeof(bool));
    for (m = 0; m < w->mapCount; m++) {
        if (w->maps[m].layoutId != id)
            continue;
        n = rg_map_connections(w, w->maps[m].group, w->maps[m].num, cn, 32u);
        for (k = 0; k < n && k < 32u; k++)
            if (cn[k].dir >= 1 && cn[k].dir <= 4)
                sides[cn[k].dir] = true;
    }
}

/* rel:256-269: level 0 is the land the map meets its neighbours with. */
static bool shift_to_edge_level(const RgWorld *w, Plain *s, uint16_t id)
{
    static const int order[4] = {1, 3, 4, 2};
    bool sides[5];
    int32_t *edge = (int32_t *)malloc(((size_t)(s->w + s->h) * 4u + 1u) * sizeof(int32_t));
    unsigned n = 0, k;
    int x, y, ref, i;
    void *scratch;

    if (edge == NULL)
        return false;
    connected_sides(w, id, sides);
    for (k = 0; k < 4; k++) {
        if (!sides[order[k]])
            continue;
        if (order[k] == 2 || order[k] == 1)
            for (x = 0; x < s->w; x++) {
                y = order[k] == 2 ? 0 : s->h - 1;
                if (s->kind[y * s->w + x] == K_LAND)
                    edge[n++] = s->cell[y * s->w + x];
            }
        else
            for (y = 0; y < s->h; y++) {
                x = order[k] == 3 ? 0 : s->w - 1;
                if (s->kind[y * s->w + x] == K_LAND)
                    edge[n++] = s->cell[y * s->w + x];
            }
    }
    if (n > 0) {
        scratch = malloc(rg_commonest_scratch_bytes(n) + 8u);
        if (scratch == NULL) {
            free(edge);
            return false;
        }
        ref = rg_commonest_int((const int *)edge, n, scratch);
        free(scratch);
        for (i = 0; i < s->w * s->h; i++)
            if (s->cell[i] != RG_LV_NONE)
                s->cell[i] -= ref;
    }
    free(edge);
    return true;
}

/* rel:271-310: every body of water at the lowest land it touches (or one level under land across a rock band). */
static void water_levels(Plain *s)
{
    static const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
    int n = s->w * s->h, x, y;
    uint8_t *seen = (uint8_t *)calloc((size_t)n, 1);
    int32_t *body = (int32_t *)malloc((size_t)n * sizeof(int32_t));

    if (seen == NULL || body == NULL) {
        free(seen);
        free(body);
        return;                          /* caller checks for NONE water cells */
    }
    for (y = 0; y < s->h; y++)
        for (x = 0; x < s->w; x++) {
            int sp = 0, nb = 0, touch = 0, have = 0, k, i;

            if (s->kind[y * s->w + x] != K_WATER || seen[y * s->w + x])
                continue;
            s->stack[sp++] = y * s->w + x;
            seen[y * s->w + x] = 1;
            while (sp > 0) {
                int c = s->stack[--sp], cx = c % s->w, cy = c / s->w;

                body[nb++] = c;
                for (k = 0; k < 4; k++) {
                    int nx = cx + dx[k], ny = cy + dy[k], j;

                    if (nx < 0 || nx >= s->w || ny < 0 || ny >= s->h)
                        continue;
                    j = ny * s->w + nx;
                    if (s->kind[j] == K_WATER && !seen[j]) {
                        seen[j] = 1;
                        s->stack[sp++] = j;
                    } else if (s->kind[j] == K_LAND) {
                        if (!have || s->cell[j] < touch)
                            touch = s->cell[j];
                        have = 1;
                    } else if (s->kind[j] == K_RELIEF && s->role[j] != VOXEL_ROLE_STAIR) {
                        int ax = nx, ay = ny, step;

                        for (step = 0; step < 3; step++) {
                            ax += dx[k];
                            ay += dy[k];
                            if (ax < 0 || ax >= s->w || ay < 0 || ay >= s->h || s->kind[ay * s->w + ax] == K_WATER)
                                break;
                            if (s->kind[ay * s->w + ax] == K_LAND) {
                                int t = s->cell[ay * s->w + ax] - 1;

                                if (!have || t < touch)
                                    touch = t;
                                have = 1;
                                break;
                            }
                        }
                    }
                }
            }
            for (i = 0; i < nb; i++)
                s->cell[body[i]] = have ? touch : 0;
        }
    free(seen);
    free(body);
}

/* ---- awash (rel:2580-2595): a rock drawn mostly in the colours of the water round it lies at the water line ---- */

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

static bool awash(const Plain *s, const RgLayout *L, RgPair *pair, const int32_t *mass, unsigned nMass)
{
    static const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
    uint8_t *water = (uint8_t *)calloc(4096, 1);
    uint16_t col[256];
    unsigned i, k, c, same = 0, total = 0;

    if (water == NULL)
        return false;
    for (i = 0; i < nMass; i++) {
        int x = mass[i] % s->w, y = mass[i] / s->w;

        for (k = 0; k < 4; k++) {
            int nx = x + dx[k], ny = y + dy[k];

            if (nx < 0 || nx >= s->w || ny < 0 || ny >= s->h || s->kind[ny * s->w + nx] != K_WATER)
                continue;
            cell_colours(pair, rg_metatile(L, nx, ny), col);
            for (c = 0; c < 256; c++)
                water[col[c] >> 3] |= (uint8_t)(1u << (col[c] & 7u));
        }
    }
    for (i = 0; i < nMass; i++) {
        cell_colours(pair, rg_metatile(L, mass[i] % s->w, mass[i] / s->w), col);
        for (c = 0; c < 256; c++) {
            total++;
            same += (water[col[c] >> 3] >> (col[c] & 7u)) & 1u;
        }
    }
    free(water);
    return total != 0 && same > total / 2u;
}

/* rel:312-336: rock masses with one level all round are not steps. Wet and not awash: a mound (returned, in order);
 * else level with the ground round them. Mass cells are int32 cell indices in one pool, masses in discovery order. */
typedef struct Mounds { int32_t *pool; unsigned nPool; unsigned *start; unsigned nMounds; unsigned awashN; } Mounds;

static bool one_mass(Plain *s, const RgLayout *L, RgPair *pair, uint8_t *seen, int32_t *mass, int x0, int y0, Mounds *mo)
{
    static const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
    int sp = 0, nm = 0, k, i, around = 0, nAround = 0;
    bool wet = false, aw = false;

    s->stack[sp++] = y0 * s->w + x0;
    seen[y0 * s->w + x0] = 1;
    while (sp > 0) {
        int c = s->stack[--sp], cx = c % s->w, cy = c / s->w;

        mass[nm++] = c;
        for (k = 0; k < 4; k++) {
            int nx = cx + dx[k], ny = cy + dy[k], j;

            if (nx < 0 || nx >= s->w || ny < 0 || ny >= s->h)
                continue;
            j = ny * s->w + nx;
            if (is_relief_cell(s, j)) {
                if (!seen[j]) {
                    seen[j] = 1;
                    s->stack[sp++] = j;
                }
            } else if (s->cell[j] != RG_LV_NONE) {
                if (nAround == 0) {                      /* the set of distinct levels: only "0, 1 or more" is read */
                    around = s->cell[j];
                    nAround = 1;
                } else if (nAround == 1 && s->cell[j] != around) {
                    nAround = 2;
                }
                wet = wet || s->kind[j] == K_WATER;
            }
        }
    }
    if (nAround > 1)
        return true;
    if (wet) {
        aw = awash(s, L, pair, mass, (unsigned)nm);
        mo->awashN += aw;
    }
    if (wet && !aw) {
        for (i = 0; i < nm; i++)
            mo->pool[mo->nPool + (unsigned)i] = mass[i];
        mo->nPool += (unsigned)nm;
        mo->start[++mo->nMounds] = mo->nPool;
    } else {
        for (i = 0; i < nm; i++)
            s->cell[mass[i]] = nAround ? around : 0;
    }
    return true;
}

static bool masses(Plain *s, const RgLayout *L, RgPair *pair, Mounds *mo)
{
    int n = s->w * s->h, x, y;
    uint8_t *seen = (uint8_t *)calloc((size_t)n, 1);
    int32_t *mass = (int32_t *)malloc((size_t)n * sizeof(int32_t));
    bool ok = seen != NULL && mass != NULL;

    mo->pool = (int32_t *)malloc((size_t)n * sizeof(int32_t));
    mo->start = (unsigned *)malloc(((size_t)n + 2u) * sizeof(unsigned));
    ok = ok && mo->pool != NULL && mo->start != NULL;
    mo->nPool = mo->nMounds = mo->awashN = 0;
    if (ok)
        mo->start[0] = 0;
    for (y = 0; ok && y < s->h; y++)
        for (x = 0; ok && x < s->w; x++)
            if (is_relief_cell(s, y * s->w + x) && !seen[y * s->w + x])
                ok = one_mass(s, L, pair, seen, mass, x, y, mo);
    free(seen);
    free(mass);
    return ok;
}

/* rel:337-354: the lattice: fixed where any non-relief cell touches, the highest of them. */
static void fix_points(const Plain *s, RgLat *lat, uint8_t *fixed)
{
    int LW = s->w * RG_P + 1, LH = s->h * RG_P + 1, i, j;

    for (j = 0; j < LH; j++)
        for (i = 0; i < LW; i++) {
            int x0 = i % RG_P == 0 ? i / RG_P - 1 : i / RG_P, x1 = i / RG_P;
            int y0 = j % RG_P == 0 ? j / RG_P - 1 : j / RG_P, y1 = j / RG_P;
            int cx, cy, best = 0, have = 0;

            for (cx = x0; cx <= x1; cx++)
                for (cy = y0; cy <= y1; cy++)
                    if (cx >= 0 && cx < s->w && cy >= 0 && cy < s->h && s->cell[cy * s->w + cx] != RG_LV_NONE) {
                        int v = s->cell[cy * s->w + cx];

                        if (!have || v > best)
                            best = v;
                        have = 1;
                    }
            if (have) {
                lat->v[(size_t)j * (size_t)LW + (size_t)i] = (double)(best * LEVEL);
                fixed[(size_t)j * (size_t)LW + (size_t)i] = 1;
            }
        }
}

/* rel:355-370: harmonic fill of the relief, in place, naive += (explicit loop upstream), warm start 0.0. */
static unsigned harmonic_fill(RgLat *lat, const uint8_t *fixed)
{
    int LW = lat->w * RG_P + 1, LH = lat->h * RG_P + 1, i, j;
    size_t nFree = 0, f, total = (size_t)LW * (size_t)LH;
    int32_t *freeIx = (int32_t *)malloc((total + 1u) * sizeof(int32_t));
    unsigned sweep;

    if (freeIx == NULL)
        return 0;
    for (f = 0; f < total; f++)
        if (!fixed[f])
            freeIx[nFree++] = (int32_t)f;
    for (sweep = 0; sweep < RG_SOLVE_SWEEPS; sweep++) {
        double delta = 0.0;

        for (f = 0; f < nFree; f++) {
            static const int ox[4] = {1, -1, 0, 0}, oy[4] = {0, 0, 1, -1};
            int k, nn = 0;
            double sum = 0.0, v, dd;

            i = (int)(freeIx[f] % LW);
            j = (int)(freeIx[f] / LW);
            for (k = 0; k < 4; k++) {
                int a = i + ox[k], b = j + oy[k];

                if (a >= 0 && a < LW && b >= 0 && b < LH) {
                    sum += lat->v[(size_t)b * (size_t)LW + (size_t)a];
                    nn++;
                }
            }
            v = sum / (double)nn;
            dd = fabs(v - lat->v[freeIx[f]]);
            if (dd > delta)
                delta = dd;
            lat->v[freeIx[f]] = v;
        }
        if (delta < RG_SOLVE_TOL) {
            sweep++;
            break;
        }
    }
    free(freeIx);
    return sweep;
}

/* rel:372-397: mounds, rocks at sea, raised by their distance (lattice steps) from the nearest fixed point. */
static bool raise_mounds(const Plain *s, RgLat *lat, const uint8_t *fixed, const Mounds *mo)
{
    static const int ox[4] = {1, -1, 0, 0}, oy[4] = {0, 0, 1, -1};
    int LW = lat->w * RG_P + 1, LH = lat->h * RG_P + 1;
    size_t total = (size_t)LW * (size_t)LH;
    uint8_t *inside = (uint8_t *)calloc(total, 1);
    int32_t *dist = (int32_t *)calloc(total, sizeof(int32_t));
    int32_t *queue = (int32_t *)malloc((total + 1u) * sizeof(int32_t));
    unsigned m, a;

    if (inside == NULL || dist == NULL || queue == NULL) {
        free(inside);
        free(dist);
        free(queue);
        return false;
    }
    for (m = 0; m < mo->nMounds; m++) {
        size_t nq = 0, head = 0, k;
        int cx, cy, i, j;

        for (a = mo->start[m]; a < mo->start[m + 1]; a++) {
            cx = mo->pool[a] % s->w;
            cy = mo->pool[a] / s->w;
            for (j = cy * RG_P; j <= cy * RG_P + RG_P; j++)
                for (i = cx * RG_P; i <= cx * RG_P + RG_P; i++)
                    if (!fixed[(size_t)j * (size_t)LW + (size_t)i])
                        inside[(size_t)j * (size_t)LW + (size_t)i] = 1;
        }
        for (a = mo->start[m]; a < mo->start[m + 1]; a++) {
            cx = mo->pool[a] % s->w;
            cy = mo->pool[a] / s->w;
            for (j = cy * RG_P; j <= cy * RG_P + RG_P; j++)
                for (i = cx * RG_P; i <= cx * RG_P + RG_P; i++) {
                    size_t p = (size_t)j * (size_t)LW + (size_t)i;
                    int q, touch = 0;

                    if (!inside[p] || dist[p] != 0)
                        continue;
                    for (q = 0; q < 4; q++) {
                        int u = i + ox[q], v = j + oy[q];

                        touch = touch || (u >= 0 && u < LW && v >= 0 && v < LH && fixed[(size_t)v * (size_t)LW + (size_t)u]);
                    }
                    if (touch) {
                        dist[p] = 1;
                        queue[nq++] = (int32_t)p;
                    }
                }
        }
        while (head < nq) {
            int32_t p = queue[head++];
            int q, i0 = (int)(p % LW), j0 = (int)(p / LW);

            for (q = 0; q < 4; q++) {
                int u = i0 + ox[q], v = j0 + oy[q];
                size_t qp;

                if (u < 0 || u >= LW || v < 0 || v >= LH)
                    continue;
                qp = (size_t)v * (size_t)LW + (size_t)u;
                if (inside[qp] && dist[qp] == 0) {
                    dist[qp] = dist[p] + 1;
                    queue[nq++] = (int32_t)qp;
                }
            }
        }
        for (k = 0; k < nq; k++) {
            int d = dist[queue[k]] * RG_MOUND;

            lat->v[queue[k]] += (double)(d < RG_MOUND_MAX ? d : RG_MOUND_MAX);
        }
        for (k = 0; k < nq; k++) {                  /* reset only what this mound touched */
            inside[queue[k]] = 0;
            dist[queue[k]] = 0;
        }
        for (a = mo->start[m]; a < mo->start[m + 1]; a++) {
            cx = mo->pool[a] % s->w;
            cy = mo->pool[a] / s->w;
            for (j = cy * RG_P; j <= cy * RG_P + RG_P; j++)
                for (i = cx * RG_P; i <= cx * RG_P + RG_P; i++)
                    inside[(size_t)j * (size_t)LW + (size_t)i] = 0;
        }
    }
    free(inside);
    free(dist);
    free(queue);
    return true;
}

static void plain_free(Plain *s)
{
    free(s->kind);
    free(s->cell);
    free(s->region);
    free(s->stack);
}

static void plain_stats(const RgLat *lat, RgPlainStats *st)
{
    size_t i, n = (size_t)(lat->w * RG_P + 1) * (size_t)(lat->h * RG_P + 1);

    st->minH = st->maxH = lat->v[0];
    for (i = 1; i < n; i++) {
        if (lat->v[i] < st->minH)
            st->minH = lat->v[i];
        if (lat->v[i] > st->maxH)
            st->maxH = lat->v[i];
    }
}

RgErr rg_solve_plain(const RgWorld *w, const RgRoles *r, const RgLayout *L, RgPair *pair, RgLat *out, RgPlainStats *st)
{
    Plain s;
    Edge *edges = NULL;
    unsigned nE = 0, i;
    int32_t *rlevel = NULL;
    Mounds mo = {0};
    uint8_t *fixed = NULL;
    RgPlainStats dummy;
    RgErr e = RG_ERR_NOMEM;
    int n;

    assert(w != NULL && r != NULL && L != NULL && pair != NULL && out != NULL);
    if (st == NULL)
        st = &dummy;
    memset(&s, 0, sizeof s);
    memset(st, 0, sizeof *st);
    memset(out, 0, sizeof *out);
    s.w = (int)L->w;
    s.h = (int)L->h;
    n = s.w * s.h;
    s.role = rg_roles_of(r, L->id);
    if (s.role == NULL || n <= 0)
        return RG_ERR_TABLES;
    s.kind = (uint8_t *)malloc((size_t)n);
    s.cell = (int32_t *)malloc((size_t)n * sizeof(int32_t));
    s.region = (int32_t *)malloc((size_t)n * sizeof(int32_t));
    s.stack = (int32_t *)malloc((size_t)n * sizeof(int32_t));
    if (s.kind == NULL || s.cell == NULL || s.region == NULL || s.stack == NULL || !rg_lat_new(out, s.w, s.h))
        goto done;
    for (i = 0; i < (unsigned)n; i++) {
        uint8_t ro = s.role[i];

        s.kind[i] = (ro == VOXEL_ROLE_CLIFF || ro == VOXEL_ROLE_SHELF || ro == VOXEL_ROLE_STAIR) ? K_RELIEF
                    : ro == VOXEL_ROLE_WATER ? K_WATER : K_LAND;
        s.cell[i] = RG_LV_NONE;
    }
    land_regions(&s);
    st->regions = s.nRegions;
    rlevel = (int32_t *)malloc(((size_t)s.nRegions + 1u) * sizeof(int32_t));
    if (rlevel == NULL || !south_faces(&s, &edges, &nE) || !region_levels(&s, edges, nE, rlevel))
        goto done;
    for (i = 0; i < (unsigned)n; i++)
        if (s.kind[i] == K_LAND)
            s.cell[i] = rlevel[s.region[i]];
    if (!shift_to_edge_level(w, &s, L->id))
        goto done;
    water_levels(&s);
    if (!masses(&s, L, pair, &mo))
        goto done;
    st->mounds = mo.nMounds;
    st->awashMasses = mo.awashN;
    fixed = (uint8_t *)calloc((size_t)(s.w * RG_P + 1) * (size_t)(s.h * RG_P + 1), 1);
    if (fixed == NULL)
        goto done;
    fix_points(&s, out, fixed);
    st->sweeps = harmonic_fill(out, fixed);
    if (!raise_mounds(&s, out, fixed, &mo))
        goto done;
    plain_stats(out, st);
    e = RG_OK;
done:
    if (e != RG_OK && out->v != NULL)
        rg_lat_free(out);
    free(edges);
    free(rlevel);
    free(mo.pool);
    free(mo.start);
    free(fixed);
    plain_free(&s);
    return e;
}

/* ---- ledges_on_ground (rel:2466-2504) ---- */

RgErr rg_ledges_on_ground(const RgLayout *L, RgLat *h)
{
    RgLedgeSet cells;
    unsigned k;

    assert(L != NULL && h != NULL && h->w == (int)L->w && h->h == (int)L->h);
    if (!rg_ledge_cells(L, true, &cells))
        return RG_ERR_NOMEM;
    for (k = 0; k < cells.n; k++) {
        int x = cells.c[k].x, y = cells.c[k].y, x0 = x * RG_P, y0 = y * RG_P, i, j;
        double c00 = *rg_lat_at(h, x0, y0), c10 = *rg_lat_at(h, x0 + RG_P, y0);
        double c01 = *rg_lat_at(h, x0, y0 + RG_P), c11 = *rg_lat_at(h, x0 + RG_P, y0 + RG_P);

        for (j = 0; j <= RG_P; j++)
            for (i = 0; i <= RG_P; i++) {
                int sx[2], sy[2], ns = 0, q, keep = 0;
                double t, s;

                if ((i == 0 || i == RG_P) && (j == 0 || j == RG_P))
                    continue;
                if (i == 0) { sx[ns] = x - 1; sy[ns++] = y; }
                if (i == RG_P) { sx[ns] = x + 1; sy[ns++] = y; }
                if (j == 0) { sx[ns] = x; sy[ns++] = y - 1; }
                if (j == RG_P) { sx[ns] = x; sy[ns++] = y + 1; }
                for (q = 0; q < ns; q++)         /* keeps(): not a ledge cell, on the map, blocked */
                    keep = keep || (!rg_off(L, sx[q], sy[q]) && cells.at[sy[q] * (int)L->w + sx[q]] < 0 &&
                                    rg_blocked(L, sx[q], sy[q]));
                if (keep)
                    continue;
                t = (double)i / (double)RG_P;
                s = (double)j / (double)RG_P;
                *rg_lat_at(h, x0 + i, y0 + j) = ((c00 * (1 - t) + c10 * t) * (1 - s) + (c01 * (1 - t) + c11 * t) * s);
            }
    }
    rg_ledge_set_free(&cells);
    return RG_OK;
}

/* ---- pier_ends (rel:2508-2541) ---- */

typedef struct PierWrite { uint8_t set; double v; } PierWrite;

static bool plank_near(const RgLayer *up, int x, int y)
{
    int a, b;

    for (a = x - RG_PIER_REACH; a < x + RG_PIER_REACH; a++)
        for (b = y - RG_PIER_REACH; b < y + RG_PIER_REACH; b++)
            if (a >= 0 && a < 16 && b >= 0 && b < 16 && rg_layer_has(up, a, b))
                return true;
    return false;
}

RgErr rg_pier_ends(const RgSolvedGroup *g, uint16_t layoutId, const RgLayout *L, RgPair *pair, const RgRoles *r,
                   RgLat *h, RgLat *sv, unsigned *conflicts)
{
    int ox = -1, oy = -1, px, py, dx;
    unsigned m;
    const uint8_t *roles = rg_roles_of(r, layoutId);
    PierWrite *wr;
    size_t nPts = (size_t)(L->w * RG_P + 1) * (size_t)(L->h * RG_P + 1);

    assert(g != NULL && L != NULL && pair != NULL && h != NULL);
    for (m = 0; m < g->nMem; m++)
        if (g->mem[m].layout == layoutId) {
            ox = g->mem[m].ox;
            oy = g->mem[m].oy;
            break;
        }
    if (ox < 0 || roles == NULL)
        return RG_OK;                              /* not a member (cannot happen for an owned layout) */
    wr = (PierWrite *)calloc(nPts, sizeof *wr);
    if (wr == NULL)
        return RG_ERR_NOMEM;
    /* upstream iterates a Python set of tuples; the writes never read what they write (level is read at a pier
     * cell, writes land on water cells), so the order is neutral unless two planks write one point differently:
     * that is counted (D4), asserted zero by the test. Row-major here. */
    for (py = 0; py < (int)L->h; py++)
        for (px = 0; px < (int)L->w; px++) {
            double level;

            if (g->pier[(size_t)(oy + py) * (size_t)g->cw + (size_t)(ox + px)] == 0)
                continue;
            level = *rg_lat_at(h, px * RG_P + RG_P / 2, py * RG_P + RG_P / 2);
            for (dx = -1; dx <= 1; dx += 2) {
                int cx = px + dx, i, j;
                const RgLayer *up;

                if (cx < 0 || cx >= (int)L->w)
                    continue;
                if (g->pier[(size_t)(oy + py) * (size_t)g->cw + (size_t)(ox + cx)] != 0 || roles[py * (int)L->w + cx] != VOXEL_ROLE_WATER)
                    continue;
                up = rg_layer(pair, rg_metatile(L, cx, py), 1);
                for (j = 0; j <= RG_P; j++)
                    for (i = 0; i <= RG_P; i++) {
                        size_t at = (size_t)(py * RG_P + j) * (size_t)(L->w * RG_P + 1) + (size_t)(cx * RG_P + i);

                        if (!plank_near(up, i * RG_P, j * RG_P))
                            continue;
                        if (wr[at].set && wr[at].v != level && conflicts != NULL)
                            (*conflicts)++;
                        wr[at].set = 1;
                        wr[at].v = level;
                        h->v[at] = level;
                        if (sv != NULL)
                            sv->v[at] = level;
                    }
            }
        }
    free(wr);
    return RG_OK;
}

/* ---- layout_heights (rel:2544-2562) ---- */

RgErr rg_layout_heights(const RgWorld *w, const RgRoles *r, const RgDrawn *d, const RgLevels *lv, RgSolved *solved,
                        uint16_t layoutId, RgLat *h, int *ledgeCells)
{
    const RgLayout *L;
    const RgDrawnGroup *G;
    RgPair *pair;
    RgErr e = RG_OK;
    int led;

    assert(w != NULL && d != NULL && lv != NULL && solved != NULL && h != NULL);
    if (layoutId == 0 || layoutId > w->layoutCount)
        return RG_ERR_TABLES;
    L = &w->layouts[layoutId - 1];
    if (!L->present)
        return RG_ERR_TABLES;
    pair = rg_pair_open(w, L->pairIndex);
    if (pair == NULL)
        return RG_ERR_NOMEM;
    memset(h, 0, sizeof *h);
    G = rg_drawn_group(d, layoutId, true, lv->ok);
    if (G != NULL && !solved->have[layoutId]) {
        e = RG_ERR_RELIEF;                           /* the group survived the world solve but was not solved */
    } else if (G != NULL) {
        size_t n = (size_t)(L->w * RG_P + 1) * (size_t)(L->h * RG_P + 1);

        if (!rg_lat_new(h, (int)L->w, (int)L->h)) {
            e = RG_ERR_NOMEM;
        } else {
            memcpy(h->v, solved->shift[layoutId].v, n * sizeof(double));
            e = rg_ledges_on_ground(L, h);
            if (e == RG_OK)
                e = rg_pier_ends(solved->grp[solved->groupOf[layoutId]], layoutId, L, pair, r, h,
                                 &solved->shift[layoutId], &solved->pierDouble);
        }
    } else if (rg_is_enabled_layout(layoutId)) {
        e = rg_solve_plain(w, r, L, pair, h, NULL);
        if (e == RG_OK)
            e = rg_ledges_on_ground(L, h);
    } else if (!rg_lat_new(h, (int)L->w, (int)L->h)) {
        e = RG_ERR_NOMEM;
    }
    if (e == RG_OK) {
        led = rg_ledge_berms(L, pair, h);
        if (led < 0)
            e = RG_ERR_NOMEM;
        else if (ledgeCells != NULL)
            *ledgeCells = led;
    }
    if (e != RG_OK && h->v != NULL)
        rg_lat_free(h);
    rg_pair_close(pair);
    return e;
}
