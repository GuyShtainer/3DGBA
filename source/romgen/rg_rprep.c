/* Modified for 3DGBA (GPLv3), 2026.
 * Original: ZallaxDev/pokeemerald-3Ds-dualscreen @ c330c0a, 3ds_port/scripts/gen_voxel_relief.py (drawn_prepare:
 * rel:1564-1849), MIT License - see source/voxel/NOTICE.md.
 * Portions Copyright (c) Dust Zallax, MIT. */
/* rg_rprep.c -- drawn_prepare: regions, drops, runs, ties, per-cell stats, map edges (3DGBA, GPLv3). Pure C. */
#include "rg_rprep.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "rg_rrock.h"
#include "rg_rtables.h"

static bool vgrow(void **buf, size_t *cap, size_t need, size_t esz)
{
    void *n;
    size_t c;

    if (need <= *cap)
        return true;
    c = *cap ? *cap : 16u;
    while (c < need)
        c *= 2u;
    n = realloc(*buf, c * esz);
    if (n == NULL)
        return false;
    *buf = n;
    *cap = c;
    return true;
}

/* ---- regions ---- */

bool rg_prep_add_region(RgPrep *p, uint32_t count)
{
    size_t cap = p->capRegions;
    uint32_t *sizes = p->sizes;
    uint8_t *big = p->big, *cut = p->cutApart;

    assert(p != NULL && p->nRegions <= p->capRegions);
    if (p->nRegions >= p->capRegions) {
        size_t nc = cap ? cap * 2u : 64u;

        sizes = (uint32_t *)realloc(p->sizes, nc * sizeof(uint32_t));
        if (sizes != NULL) p->sizes = sizes;
        big = (uint8_t *)realloc(p->big, nc);
        if (big != NULL) p->big = big;
        cut = (uint8_t *)realloc(p->cutApart, nc);
        if (cut != NULL) p->cutApart = cut;
        if (sizes == NULL || big == NULL || cut == NULL)
            return false;
        p->capRegions = (uint32_t)nc;
    }
    p->sizes[p->nRegions] = count;
    p->big[p->nRegions] = 0;
    p->cutApart[p->nRegions] = 0;
    p->nRegions++;
    return true;
}

/* band_seam / apart (rel:1617-1651): both are False inside one cell and symmetric in their two pixels. */
static bool apart(const RgCanvas *cv, int a, int b, int i, int j)
{
    size_t c = (size_t)(b / 16) * (size_t)cv->cw + (size_t)(a / 16), d = (size_t)(j / 16) * (size_t)cv->cw + (size_t)(i / 16);
    size_t other;

    if (c == d || (cv->pier[c] != 0) == (cv->pier[d] != 0))
        return false;
    other = cv->pier[c] ? d : c;
    return cv->flat[other] != RG_FLAT_FLOOR && cv->flat[other] != RG_FLAT_BRIDGE;
}

static bool band_seam(const RgCanvas *cv, int a, int b, int i, int j)
{
    int sa, sb, cx, cy, low, s;
    const int cw = cv->cw;

    if (b / 16 == j / 16)
        return false;
    sa = cv->side[(size_t)(b / 16) * (size_t)cw + (size_t)(a / 16)];
    sb = cv->side[(size_t)(j / 16) * (size_t)cw + (size_t)(i / 16)];
    if ((sa != 0) == (sb != 0))
        return false;
    cx = sa ? a / 16 : i / 16;                 /* only a ridge at sea: the band falls to water */
    cy = sa ? b / 16 : j / 16;
    s = cv->side[(size_t)cy * (size_t)cw + (size_t)cx];
    low = cx - s;
    while (low >= 0 && low < cw && cv->side[(size_t)cy * (size_t)cw + (size_t)low] == s)
        low -= s;
    return low >= 0 && low < cw && cv->flat[(size_t)cy * (size_t)cw + (size_t)low] == RG_FLAT_WATER;
}

/* May the flood go from pixel (a, b) to (i, j) of the same kind? */
static bool joins(const RgCanvas *cv, int a, int b, int i, int j)
{
    if ((a >> 4) == (i >> 4) && (b >> 4) == (j >> 4))
        return true;
    return !apart(cv, a, b, i, j) && !band_seam(cv, a, b, i, j);
}

/* One region from (sx, sy), LIFO as upstream; returns its pixel count, or -1 on no memory. */
static int64_t flood_one(RgPrep *p, int W, int H, int sx, int sy, int32_t n, int32_t **stack, size_t *cap)
{
    static const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
    const uint8_t *kind = p->cv.kind;
    const uint8_t k = kind[(size_t)sy * W + sx];
    size_t sp = 0;
    int64_t count = 0;
    unsigned d;

    assert(k == RG_K_GROUND || k == RG_K_TOP);
    if (!vgrow((void **)stack, cap, 1, sizeof(int32_t)))
        return -1;
    (*stack)[sp++] = sy * W + sx;
    p->region[(size_t)sy * W + sx] = n;
    while (sp > 0) {
        int c = (*stack)[--sp], a = c % W, b = c / W;

        count++;
        for (d = 0; d < 4; d++) {
            int i = a + dx[d], j = b + dy[d];

            if (i < 0 || j < 0 || i >= W || j >= H || p->region[(size_t)j * W + i] >= 0 ||
                kind[(size_t)j * W + i] != k || !joins(&p->cv, a, b, i, j))
                continue;
            if (sp + 1u > *cap && !vgrow((void **)stack, cap, sp + 1u, sizeof(int32_t)))
                return -1;
            p->region[(size_t)j * W + i] = n;
            (*stack)[sp++] = j * W + i;
        }
    }
    return count;
}

/* rel:1653-1664: labels are in first-pixel row-major order. */
static bool region_flood(RgPrep *p)
{
    const int W = p->cv.cw * 16, H = p->cv.ch * 16;
    int32_t *stack = NULL;
    size_t cap = 0, i;
    int x, y;
    bool ok = true;

    for (i = 0; i < (size_t)W * (size_t)H; i++)
        p->region[i] = -1;
    for (y = 0; ok && y < H; y++)
        for (x = 0; x < W; x++) {
            uint8_t k = p->cv.kind[(size_t)y * W + x];
            int64_t n;

            if ((k != RG_K_GROUND && k != RG_K_TOP) || p->region[(size_t)y * W + x] >= 0)
                continue;
            n = flood_one(p, W, H, x, y, (int32_t)p->nRegions, &stack, &cap);
            if (n < 0 || !rg_prep_add_region(p, (uint32_t)n)) {
                ok = false;
                break;
            }
        }
    free(stack);
    return ok;
}

/* rel:1665-1676: big = REGION_MIN pixels and not a strip thinner than THIN columns on average (or cut apart). */
static bool compute_big(RgPrep *p)
{
    const int W = p->cv.cw * 16, H = p->cv.ch * 16;
    int32_t *last = (int32_t *)malloc((p->nRegions ? p->nRegions : 1u) * sizeof(int32_t));
    uint32_t *cols = (uint32_t *)calloc(p->nRegions ? p->nRegions : 1u, sizeof(uint32_t));
    uint32_t n;
    int x, y;

    if (last == NULL || cols == NULL) {
        free(last);
        free(cols);
        return false;
    }
    for (n = 0; n < p->nRegions; n++)
        last[n] = -1;
    for (x = 0; x < W; x++)
        for (y = 0; y < H; y++) {
            int32_t r = p->region[(size_t)y * W + x];

            if (r >= 0 && last[r] != x) {
                last[r] = x;
                cols[r]++;
            }
        }
    for (n = 0; n < p->nRegions; n++)
        p->big[n] = (p->sizes[n] >= RG_REGION_MIN && (p->sizes[n] >= RG_THIN * cols[n] || p->cutApart[n])) ? 1 : 0;
    free(last);
    free(cols);
    return true;
}

/* ---- runs and ties ---- */

static RgRunKey *run_key(RgPrep *p, int32_t a, int32_t b)
{
    unsigned i;

    assert(a >= 0 && b >= 0);
    for (i = 0; i < p->nRuns; i++)
        if (p->runs[i].a == a && p->runs[i].b == b)
            return &p->runs[i];
    size_t cap = p->capRuns;
    if (!vgrow((void **)&p->runs, &cap, (size_t)p->nRuns + 1u, sizeof(RgRunKey)))
        return NULL;
    p->capRuns = (unsigned)cap;
    memset(&p->runs[p->nRuns], 0, sizeof(RgRunKey));
    p->runs[p->nRuns].a = a;
    p->runs[p->nRuns].b = b;
    return &p->runs[p->nRuns++];
}

/* runs[(a, b)].extend([drop] * count) */
static bool run_add(RgPrep *p, int32_t a, int32_t b, int32_t drop, uint32_t count)
{
    RgRunKey *k = run_key(p, a, b);
    size_t cap, i;

    if (k == NULL)
        return false;
    cap = k->cap;
    if (!vgrow((void **)&k->v, &cap, (size_t)k->n + count, sizeof(int32_t)))
        return false;
    k->cap = (uint32_t)cap;
    for (i = 0; i < count; i++)
        k->v[k->n++] = drop;
    return true;
}

static bool both_big(const RgPrep *p, int32_t a, int32_t b)
{
    return a >= 0 && b >= 0 && a != b && p->big[a] && p->big[b];
}

/* rel:1678-1696: down the columns, FACE and RIM runs between two terraces. */
static bool runs_columns(RgPrep *p)
{
    const RgCanvas *cv = &p->cv;
    const int W = cv->cw * 16, H = cv->ch * 16;
    int x, y, y0, j;

    for (x = 0; x < W; x++) {
        y = 0;
        while (y < H) {
            uint8_t k = cv->kind[(size_t)y * W + x];
            int32_t a, b;
            unsigned sideN = 0;

            if (k != RG_K_FACE && k != RG_K_RIM) { y++; continue; }
            y0 = y;
            while (y < H && cv->kind[(size_t)y * W + x] == k) y++;
            if (y0 == 0 || y == H)
                continue;
            a = p->region[(size_t)(y0 - 1) * W + x];
            b = p->region[(size_t)y * W + x];
            if (!both_big(p, a, b))
                continue;
            if (cv->kind[(size_t)y * W + x] == RG_K_GROUND && cv->blocked[(size_t)(y / 16) * cv->cw + (size_t)(x / 16)])
                continue;                 /* its foot is behind a roof or a tree: not all of it shows */
            for (j = y0; j < y; j++)
                sideN += cv->side[(size_t)(j / 16) * cv->cw + (size_t)(x / 16)] != 0 ? 1u : 0u;
            if (k == RG_K_FACE && sideN * 2u > (unsigned)(y - y0))
                continue;                 /* down a west or east face, not across a south one */
            if (!run_add(p, a, b, k == RG_K_FACE ? y - y0 : -RG_RIM_RISE, 1))
                return false;
        }
    }
    return true;
}

/* rel:1698-1716: across the rows, the west and east faces. */
static bool runs_rows(RgPrep *p)
{
    const RgCanvas *cv = &p->cv;
    const int W = cv->cw * 16, H = cv->ch * 16;
    int x, y, x0, i;

    for (y = 0; y < H; y++) {
        const uint8_t *row = cv->kind + (size_t)y * W;

        x = 0;
        while (x < W) {
            int turn = 0;
            int32_t left, right;

            if (row[x] != RG_K_FACE) { x++; continue; }
            x0 = x;
            while (x < W && row[x] == RG_K_FACE) x++;
            if (x0 == 0 || x == W)
                continue;
            for (i = x0; i < x; i++)
                turn += cv->side[(size_t)(y / 16) * cv->cw + (size_t)(i / 16)];
            if (abs(turn) * 2 <= x - x0)
                continue;
            left = p->region[(size_t)y * W + x0 - 1];
            right = p->region[(size_t)y * W + x];
            if (!both_big(p, left, right))
                continue;
            if (!(turn > 0 ? run_add(p, right, left, RG_SIDE_RISE, 1) : run_add(p, left, right, RG_SIDE_RISE, 1)))
                return false;
        }
    }
    return true;
}

/* rel:1718-1737: a flight of two bands or more down a row, all turned the same way. */
static bool runs_flights(RgPrep *p)
{
    const RgCanvas *cv = &p->cv;
    const int W = cv->cw * 16, H = cv->ch * 16, CW = cv->cw;
    int y, cx, x0;

    for (y = 0; y < H; y++) {
        const int8_t *srow = cv->side + (size_t)(y / 16) * (size_t)CW;

        cx = 0;
        while (cx < CW) {
            int s = srow[cx];
            int32_t west, east;

            if (!s) { cx++; continue; }
            x0 = cx;
            while (cx < CW && srow[cx] == s) cx++;
            if (cx - x0 < 2 || x0 == 0 || cx == CW)
                continue;
            west = p->region[(size_t)y * W + (size_t)(x0 * 16 - 8)];
            east = p->region[(size_t)y * W + (size_t)(cx * 16 + 8)];
            if (!both_big(p, west, east))
                continue;
            if (!(s > 0 ? run_add(p, east, west, RG_SIDE_RISE * (cx - x0), 1)
                        : run_add(p, west, east, RG_SIDE_RISE * (cx - x0), 1)))
                return false;
        }
    }
    return true;
}

/* rel:1739-1757 south_face. Upstream tests `flat[cy][cx] in (None, "fall")` against a flat that is the Python
 * `False` for a plain cell, and `False in (None, "fall")` is False: only a waterfall cell can be one. Ported as it
 * behaves, not as it reads. */
static bool south_face(const RgCanvas *cv, int cx, int cy)
{
    size_t i = (size_t)cy * (size_t)cv->cw + (size_t)cx;

    return cv->side[i] == 0 && cv->flat[i] == RG_FLAT_FALL;
}

/* rel:1758-1770: a column of two south faces or more stacked one on the other. */
static bool runs_stacks(RgPrep *p)
{
    const RgCanvas *cv = &p->cv;
    const int W = cv->cw * 16, CH = cv->ch;
    int x, cx, cy, y0, i;

    for (x = 8; x < W; x += 16) {
        cx = x / 16;
        cy = 0;
        while (cy < CH) {
            if (!south_face(cv, cx, cy)) { cy++; continue; }
            y0 = cy;
            while (cy < CH && south_face(cv, cx, cy)) cy++;
            if (cy - y0 < 2 || y0 == 0 || cy == CH)
                continue;
            for (i = x - 6; i < x + 7; i += 3) {
                int32_t up = p->region[(size_t)(y0 * 16 - 4) * W + (size_t)i];
                int32_t down = p->region[(size_t)(cy * 16 + 4) * W + (size_t)i];

                if (both_big(p, up, down) && !run_add(p, up, down, RG_LEVEL * (cy - y0), 1))
                    return false;
            }
        }
    }
    return true;
}

/* ---- the walked ground (rel:1772-1812) ---- */

#define WALK_NONE (-2)               /* the cell is not walk_region's (flat is not floor, bridge or water) */
#define WALK_NO_REGION (-1)          /* walk_region[cell] is None */

/* max((count, region)) over the cell's 64 sample pixels, big regions only (rel:1783-1787). */
static int32_t walk_pick(const RgPrep *p, int cx, int cy)
{
    const int W = p->cv.cw * 16;
    int32_t rs[64];
    uint32_t ns[64];
    unsigned nr = 0, i;
    int pi, pj;
    int32_t best = WALK_NO_REGION;
    uint32_t bestN = 0;

    for (pj = 0; pj < 16; pj += 2)
        for (pi = 0; pi < 16; pi += 2) {
            int32_t r = p->region[(size_t)(cy * 16 + pj) * W + (size_t)(cx * 16 + pi)];

            if (r < 0 || !p->big[r])
                continue;
            for (i = 0; i < nr && rs[i] != r; i++) {}
            if (i == nr) {
                assert(nr < 64);
                rs[nr] = r;
                ns[nr++] = 0;
            }
            ns[i]++;
        }
    for (i = 0; i < nr; i++)
        if (ns[i] > bestN || (ns[i] == bestN && rs[i] > best)) {
            bestN = ns[i];
            best = rs[i];
        }
    return best;
}

static bool tie_add(RgPrep *p, int32_t a, int32_t b)
{
    size_t cap = p->capTies;
    unsigned i;

    for (i = 0; i < p->nTies; i++)
        if (p->ties[i].a == a && p->ties[i].b == b)
            return true;
    if (!vgrow((void **)&p->ties, &cap, (size_t)p->nTies + 1u, sizeof(RgTie)))
        return false;
    p->capTies = (unsigned)cap;
    p->ties[p->nTies].a = a;
    p->ties[p->nTies].b = b;
    p->nTies++;
    return true;
}

static int i32_cmp(const void *a, const void *b)
{
    int32_t x = *(const int32_t *)a, y = *(const int32_t *)b;

    return x < y ? -1 : x > y ? 1 : 0;
}

/* One walked component (LIFO): the walk regions it touches, sorted unique, tie-linked to the first (rel:1800-1810). */
static bool walk_component(RgPrep *p, const int32_t *walk, uint8_t *seen, int32_t *stack, int start,
                           int32_t *found)
{
    static const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
    const int CW = p->cv.cw, CH = p->cv.ch;
    size_t sp = 0, nf = 0, i, k = 0;
    unsigned d;

    stack[sp++] = start;
    seen[start] = 1;
    while (sp > 0) {
        int c = stack[--sp], cx = c % CW, cy = c / CW;

        if (walk[c] >= 0)
            found[nf++] = walk[c];
        for (d = 0; d < 4; d++) {
            int qx = cx + dx[d], qy = cy + dy[d], q;
            uint8_t fa, fb;

            if (qx < 0 || qy < 0 || qx >= CW || qy >= CH)
                continue;
            q = qy * CW + qx;
            fa = p->cv.flat[q];
            fb = p->cv.flat[c];
            if (walk[q] == WALK_NONE || seen[q] ||
                ((fa == RG_FLAT_BRIDGE && fb == RG_FLAT_WATER) || (fa == RG_FLAT_WATER && fb == RG_FLAT_BRIDGE)))
                continue;
            seen[q] = 1;
            stack[sp++] = q;
        }
    }
    qsort(found, nf, sizeof(int32_t), i32_cmp);
    for (i = 0; i < nf; i++)
        if (k == 0 || found[k - 1] != found[i])
            found[k++] = found[i];
    for (i = 1; i < k; i++)
        if (!run_add(p, found[0], found[i], 0, RG_WALKED) || !tie_add(p, found[0], found[i]))
            return false;
    return true;
}

static bool runs_walked(RgPrep *p)
{
    const size_t cells = (size_t)p->cv.cw * (size_t)p->cv.ch;
    int32_t *walk = (int32_t *)malloc(cells * sizeof(int32_t));
    uint8_t *seen = (uint8_t *)calloc(cells, 1);
    int32_t *stack = (int32_t *)malloc(cells * sizeof(int32_t));
    int32_t *found = (int32_t *)malloc(cells * sizeof(int32_t));
    bool ok = walk && seen && stack && found;
    size_t i;

    if (ok) {
        for (i = 0; i < cells; i++) {
            uint8_t f = p->cv.flat[i];

            walk[i] = (f == RG_FLAT_FLOOR || f == RG_FLAT_BRIDGE || f == RG_FLAT_WATER)
                          ? walk_pick(p, (int)(i % (size_t)p->cv.cw), (int)(i / (size_t)p->cv.cw))
                          : WALK_NONE;
        }
        for (i = 0; ok && i < cells; i++)
            if (walk[i] != WALK_NONE && !seen[i])
                ok = walk_component(p, walk, seen, stack, (int)i, found);
    }
    free(walk); free(seen); free(stack); free(found);
    return ok;
}

/* ---- per-cell stats and map edges (rel:1814-1849) ---- */

static bool cell_stat(RgPrep *p, int cx, int cy)
{
    const int W = p->cv.cw * 16;
    int32_t rs[256];
    uint32_t ns[256];
    unsigned nr = 0, n = 0, top = 0, ground = 0, i;
    int pi, pj;
    RgCellStat *st = &p->stats[(size_t)cy * (size_t)p->cv.cw + (size_t)cx];

    for (pj = 0; pj < 16; pj++)
        for (pi = 0; pi < 16; pi++) {
            size_t ix = (size_t)(cy * 16 + pj) * W + (size_t)(cx * 16 + pi);
            uint8_t k = p->cv.kind[ix];
            int32_t r = p->region[ix];

            if (k == RG_K_VOID)
                continue;
            n++;
            top += (k == RG_K_TOP || k == RG_K_RIM) ? 1u : 0u;
            ground += k == RG_K_GROUND ? 1u : 0u;
            if (r < 0 || !p->big[r])
                continue;
            for (i = 0; i < nr && rs[i] != r; i++) {}
            if (i == nr) {
                rs[nr] = r;
                ns[nr++] = 0;
            }
            ns[i]++;
        }
    memset(st, 0, sizeof(*st));
    if (n == 0)
        return true;
    st->n = (uint16_t)n; st->top = (uint16_t)top; st->ground = (uint16_t)ground; st->nCounts = (uint16_t)nr;
    st->cOff = (uint32_t)p->nCounts;
    if (!vgrow((void **)&p->counts, &p->capCounts, p->nCounts + nr, sizeof(RgCount)))
        return false;
    for (i = 0; i < nr; i++) {
        p->counts[p->nCounts].region = rs[i];
        p->counts[p->nCounts++].n = ns[i];
    }
    return true;
}

static bool stats_all(RgPrep *p)
{
    const size_t cells = (size_t)p->cv.cw * (size_t)p->cv.ch;
    int cx, cy;

    p->stats = (RgCellStat *)calloc(cells, sizeof(RgCellStat));
    p->freeMid = (uint8_t *)calloc(cells, 1);
    if (p->stats == NULL || p->freeMid == NULL)
        return false;
    for (cy = 0; cy < p->cv.ch; cy++)
        for (cx = 0; cx < p->cv.cw; cx++) {
            if (!cell_stat(p, cx, cy))
                return false;
            p->freeMid[(size_t)cy * p->cv.cw + (size_t)cx] =
                p->cv.kind[(size_t)(cy * 16 + 8) * (p->cv.cw * 16) + (size_t)(cx * 16 + 8)] == RG_K_FREE ? 1 : 0;
        }
    return true;
}

/* The terrace or ground region at pixel (x, y), or -1 (rel:1840-1841). */
static int32_t edge_region(const RgPrep *p, int x, int y)
{
    const size_t ix = (size_t)y * (size_t)(p->cv.cw * 16) + (size_t)x;
    int32_t r = p->region[ix];
    uint8_t k = p->cv.kind[ix];

    return (r >= 0 && p->big[r] && (k == RG_K_GROUND || k == RG_K_TOP)) ? r : -1;
}

static bool edges_all(RgPrep *p)
{
    size_t total = 0, at = 0;
    unsigned m, e, i;

    p->edgeOff = (uint32_t *)calloc((size_t)p->cv.nMem * 4u + 1u, sizeof(uint32_t));
    if (p->edgeOff == NULL)
        return false;
    for (m = 0; m < p->cv.nMem; m++)
        total += 2u * (size_t)p->cv.mem[m].w + 2u * (size_t)p->cv.mem[m].h;
    p->edges = (int32_t *)malloc((total ? total : 1u) * sizeof(int32_t));
    if (p->edges == NULL)
        return false;
    for (m = 0; m < p->cv.nMem; m++) {
        const RgCanvasMember *mb = &p->cv.mem[m];

        for (e = 0; e < 4; e++) {
            unsigned len = e < 2 ? mb->w : mb->h;

            p->edgeOff[m * 4u + e] = (uint32_t)at;
            for (i = 0; i < len; i++) {
                int x, y;

                if (e == RG_EDGE_UP || e == RG_EDGE_DOWN) {
                    x = (mb->ox + (int)i) * 16 + 8;
                    y = e == RG_EDGE_UP ? mb->oy * 16 + 1 : (mb->oy + mb->h) * 16 - 2;
                } else {
                    x = e == RG_EDGE_LEFT ? mb->ox * 16 + 1 : (mb->ox + mb->w) * 16 - 2;
                    y = (mb->oy + (int)i) * 16 + 8;
                }
                p->edges[at++] = edge_region(p, x, y);
            }
        }
    }
    p->edgeOff[p->cv.nMem * 4u] = (uint32_t)at;
    assert(at == total);
    return true;
}

/* ---- drawn_prepare ---- */

void rg_prep_free(RgPrep *p)
{
    unsigned i;

    if (p == NULL)
        return;
    rg_canvas_free(&p->cv);
    free(p->region); free(p->sizes); free(p->big); free(p->cutApart);
    for (i = 0; i < p->nRuns; i++)
        free(p->runs[i].v);
    free(p->runs); free(p->ties); free(p->stats); free(p->counts); free(p->freeMid); free(p->edges); free(p->edgeOff);
    memset(p, 0, sizeof(*p));
}

RgErr rg_prep_run(RgCanvas *cv, bool wrap, RgPrep *out)
{
    bool ok;

    assert(cv != NULL && out != NULL && cv->kind != NULL);
    memset(out, 0, sizeof(*out));
    out->cv = *cv;
    memset(cv, 0, sizeof(*cv));
    out->region = (int32_t *)malloc((size_t)out->cv.cw * 16u * (size_t)out->cv.ch * 16u * sizeof(int32_t));
    if (out->region == NULL)
        return RG_ERR_NOMEM;
    ok = region_flood(out);
    if (ok && wrap) {
        out->wrapped = true;
        ok = rg_split_wrapped(out);
    }
    ok = ok && compute_big(out) && runs_columns(out) && runs_rows(out) && runs_flights(out) && runs_stacks(out) &&
         runs_walked(out) && stats_all(out) && edges_all(out);
    return ok ? RG_OK : RG_ERR_NOMEM;
}

bool rg_group_is_wrap(const RgDrawnGroup *g)
{
    unsigned i;

    assert(g != NULL);
    for (i = 0; i < 3; i++)
        if (!g->isAlt && g->key == RG_WRAP_GROUP_SEEDS[i])
            return true;
    return false;
}

RgErr rg_prepare(RgRCtx *c, const RgDrawn *d, const RgDrawnGroup *g, RgPrep *out)
{
    RgCanvas cv;
    RgErr e = rg_canvas_build(c, d, g, &cv);

    memset(out, 0, sizeof(*out));
    if (e != RG_OK)
        return e;
    return rg_prep_run(&cv, rg_group_is_wrap(g), out);
}
