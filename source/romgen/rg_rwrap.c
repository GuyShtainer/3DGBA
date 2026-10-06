/* Modified for 3DGBA (GPLv3), 2026.
 * Original: ZallaxDev/pokeemerald-3Ds-dualscreen @ c330c0a, 3ds_port/scripts/gen_voxel_relief.py (split_wrapped,
 * _wrapped, _clusters, _rim_gaps, _neck: rel:1327-1561), MIT License - see source/voxel/NOTICE.md.
 * Portions Copyright (c) Dust Zallax, MIT. */
/* rg_rwrap.c -- terraces the flood joined, cut where a face says they are two (3DGBA, GPLv3). Pure C.
 *
 * Python sets of pixels become sorted arrays; the max-flow of _neck keeps upstream's unit-capacity BFS augmenting
 * paths, and its result (the residual-reachable side, the flow value) does not depend on the order the BFS visits
 * the start set in (SPEC-S3 2.3, "neutral"), so any fixed order reproduces upstream. */
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "rg_rprep.h"
#include "rg_rrock.h"

typedef struct Pair { int32_t hx, hy, lx, ly; } Pair;        /* ((hx, hy), (lx, ly)): a face's high end and low end */
typedef struct Rec { int32_t r; uint32_t seq; Pair p; } Rec;

typedef struct Wrap {
    RgPrep *p;
    int W, H;
    const uint8_t *south;            /* cw*ch: faceLow or FACE_SOUTH metatile */
    uint8_t *gaps;                   /* W*H, or NULL until first needed */
    Rec *recs;
    size_t nRecs, capRecs;
} Wrap;

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

static int pair_cmp(const Pair *a, const Pair *b)
{
    if (a->hx != b->hx) return a->hx < b->hx ? -1 : 1;
    if (a->hy != b->hy) return a->hy < b->hy ? -1 : 1;
    if (a->lx != b->lx) return a->lx < b->lx ? -1 : 1;
    if (a->ly != b->ly) return a->ly < b->ly ? -1 : 1;
    return 0;
}

static int pair_qcmp(const void *a, const void *b) { return pair_cmp((const Pair *)a, (const Pair *)b); }

static int rec_qcmp(const void *a, const void *b)
{
    const Rec *x = (const Rec *)a, *y = (const Rec *)b;

    if (x->r != y->r) return x->r < y->r ? -1 : 1;
    return x->seq < y->seq ? -1 : x->seq > y->seq ? 1 : 0;
}

static bool add_rec(Wrap *w, int32_t r, int hx, int hy, int lx, int ly)
{
    Rec *e;

    assert(r >= 0);
    if (!vgrow((void **)&w->recs, &w->capRecs, w->nRecs + 1u, sizeof(Rec)))
        return false;
    e = &w->recs[w->nRecs];
    e->r = r;
    e->seq = (uint32_t)w->nRecs;
    e->p.hx = hx; e->p.hy = hy; e->p.lx = lx; e->p.ly = ly;
    w->nRecs++;
    return true;
}

/* ---- _wrapped (rel:1338-1385): faces with the same region at both ends ---- */

static bool wrapped_south_columns(Wrap *w)
{
    const RgPrep *p = w->p;
    const uint8_t *kind = p->cv.kind;
    const int32_t *region = p->region;
    int x, y, y0, j;

    for (x = 0; x < w->W; x++) {
        y = 0;
        while (y < w->H) {
            unsigned south = 0;

            if (kind[(size_t)y * w->W + x] != RG_K_FACE) { y++; continue; }
            y0 = y;
            while (y < w->H && kind[(size_t)y * w->W + x] == RG_K_FACE) y++;
            if (y0 == 0 || y >= w->H || y - y0 < RG_WRAP_DROP)
                continue;
            if (region[(size_t)y * w->W + x] < 0 || region[(size_t)(y0 - 1) * w->W + x] != region[(size_t)y * w->W + x])
                continue;
            for (j = y0; j < y; j++)
                south += w->south[(size_t)(j / 16) * (size_t)p->cv.cw + (size_t)(x / 16)] ? 1u : 0u;
            if (south * 2u > (unsigned)(y - y0) && !add_rec(w, region[(size_t)y * w->W + x], x, y0 - 1, x, y))
                return false;
        }
    }
    return true;
}

/* rim with top on both sides of it, down a column (rel:1362-1376) */
static bool rim_tops_both(const Wrap *w, int x, int y0, int y, int32_t r)
{
    const uint8_t *kind = w->p->cv.kind;
    const int32_t *region = w->p->region;
    int k;

    for (k = 1; k <= RG_WRAP_DEEP; k++)
        if (kind[(size_t)(y0 - k) * w->W + x] != RG_K_TOP || region[(size_t)(y0 - k) * w->W + x] != r)
            return false;
    for (k = 0; k < RG_WRAP_DEEP; k++)
        if (kind[(size_t)(y + k) * w->W + x] != RG_K_TOP || region[(size_t)(y + k) * w->W + x] != r)
            return false;
    return true;
}

static bool wrapped_rim_columns(Wrap *w)
{
    const uint8_t *kind = w->p->cv.kind;
    const int32_t *region = w->p->region;
    int x, y, y0;

    for (x = 0; x < w->W; x++) {
        y = 0;
        while (y < w->H) {
            int32_t r;

            if (kind[(size_t)y * w->W + x] != RG_K_RIM) { y++; continue; }
            y0 = y;
            while (y < w->H && kind[(size_t)y * w->W + x] == RG_K_RIM) y++;
            r = y < w->H ? region[(size_t)y * w->W + x] : -1;
            if (r >= 0 && y0 >= RG_WRAP_DEEP && y + RG_WRAP_DEEP <= w->H && rim_tops_both(w, x, y0, y, r) &&
                !add_rec(w, r, x, y, x, y0 - 1))
                return false;
        }
    }
    return true;
}

static bool wrapped_rows(Wrap *w)
{
    const RgPrep *p = w->p;
    const uint8_t *kind = p->cv.kind;
    const int32_t *region = p->region;
    int x, y, x0;

    for (y = 0; y < w->H; y++) {
        const uint8_t *row = kind + (size_t)y * w->W;
        const int8_t *srow = p->cv.side + (size_t)(y / 16) * (size_t)p->cv.cw;

        x = 0;
        while (x < w->W) {
            int32_t r;

            if (row[x] != RG_K_FACE) { x++; continue; }
            x0 = x;
            while (x < w->W && row[x] == RG_K_FACE) x++;
            if (x0 == 0 || x >= w->W || x - x0 < RG_WRAP_DROP)
                continue;
            r = region[(size_t)y * w->W + x];
            if (r < 0 || region[(size_t)y * w->W + x0 - 1] != r || !srow[x0 / 16] || !srow[(x - 1) / 16])
                continue;
            /* the high edge first: a band turned west (+1) rises to the east */
            if (!(srow[x0 / 16] > 0 ? add_rec(w, r, x, y, x0 - 1, y) : add_rec(w, r, x0 - 1, y, x, y)))
                return false;
        }
    }
    return true;
}

/* All of `_wrapped`'s output as records (region, pair) in generation order, then grouped by region (stable). */
static bool collect_pairs(Wrap *w)
{
    w->nRecs = 0;
    if (!wrapped_south_columns(w) || !wrapped_rim_columns(w) || !wrapped_rows(w))
        return false;
    qsort(w->recs, w->nRecs, sizeof(Rec), rec_qcmp);
    return true;
}

/* ---- _clusters (rel:1388-1400) ---- */

typedef struct Cell { int cx, cy; } Cell;
typedef struct Group {
    Cell *cells; size_t nc, capc;
    Pair *pairs; size_t np, capp;
} Group;

static void group_free(Group *g)
{
    free(g->cells);
    free(g->pairs);
    memset(g, 0, sizeof(*g));
}

static bool group_near(const Group *g, Cell c)
{
    size_t i;

    for (i = 0; i < g->nc; i++) {
        int dx = abs(c.cx - g->cells[i].cx), dy = abs(c.cy - g->cells[i].cy);

        if ((dx > dy ? dx : dy) <= 2)
            return true;
    }
    return false;
}

/* merged = ({c}, [pr] + the hits' pairs in list order); the hits leave the list, merged is appended. Everything
 * that can fail is allocated before the list is touched. */
static bool merge_hits(Group **gs, size_t *ng, size_t *capg, const unsigned char *hit, Cell c, const Pair *pr)
{
    Group m;
    size_t i, k = 0, nc = 1, np = 1;

    for (i = 0; i < *ng; i++)
        if (hit[i]) {
            nc += (*gs)[i].nc;
            np += (*gs)[i].np;
        }
    memset(&m, 0, sizeof(m));
    m.cells = (Cell *)malloc(nc * sizeof(Cell));
    m.pairs = (Pair *)malloc(np * sizeof(Pair));
    if (m.cells == NULL || m.pairs == NULL || !vgrow((void **)gs, capg, *ng + 1u, sizeof(Group))) {
        group_free(&m);
        return false;
    }
    m.cells[m.nc++] = c;
    m.pairs[m.np++] = *pr;
    for (i = 0; i < *ng; i++) {
        Group *g = &(*gs)[i];

        if (!hit[i]) {
            (*gs)[k++] = *g;
            continue;
        }
        memcpy(m.cells + m.nc, g->cells, g->nc * sizeof(Cell));
        memcpy(m.pairs + m.np, g->pairs, g->np * sizeof(Pair));
        m.nc += g->nc;
        m.np += g->np;
        group_free(g);
    }
    *ng = k;
    (*gs)[(*ng)++] = m;
    return true;
}

/* `pairs` is sorted in place. On false every group made so far is freed. */
static bool clusters(Pair *pairs, size_t n, Group **out, size_t *nOut)
{
    Group *gs = NULL;
    size_t ng = 0, capg = 0, i, k;
    unsigned char *hit = NULL;

    qsort(pairs, n, sizeof(Pair), pair_qcmp);
    for (i = 0; i < n; i++) {
        Cell c;

        c.cx = pairs[i].hx / 16;
        c.cy = pairs[i].hy / 16;
        free(hit);
        hit = (unsigned char *)calloc(ng + 1u, 1);
        if (hit == NULL)
            break;
        for (k = 0; k < ng; k++)
            hit[k] = group_near(&gs[k], c) ? 1 : 0;
        if (!merge_hits(&gs, &ng, &capg, hit, c, &pairs[i]))
            break;
    }
    free(hit);
    if (i < n) {
        for (k = 0; k < ng; k++)
            group_free(&gs[k]);
        free(gs);
        return false;
    }
    *out = gs;
    *nOut = ng;
    return true;
}

/* ---- _rim_gaps (rel:1403-1420): top or ground pixels between two rim pixels close by in a row, column or diagonal ---- */

static bool rim_within(const Wrap *w, int x, int y, int dx, int dy)
{
    int k;

    for (k = 1; k <= 4; k++) {
        int qx = x + k * dx, qy = y + k * dy;

        if (qx >= 0 && qx < w->W && qy >= 0 && qy < w->H && w->p->cv.kind[(size_t)qy * w->W + qx] == RG_K_RIM)
            return true;
    }
    return false;
}

static uint8_t *rim_gaps(const Wrap *w)
{
    static const int dx[4] = {1, 0, 1, 1}, dy[4] = {0, 1, 1, -1};
    uint8_t *out = (uint8_t *)calloc((size_t)w->W * (size_t)w->H, 1);
    int x, y, d;

    if (out == NULL)
        return NULL;
    for (y = 0; y < w->H; y++)
        for (x = 0; x < w->W; x++) {
            uint8_t k = w->p->cv.kind[(size_t)y * w->W + x];

            if (k != RG_K_TOP && k != RG_K_GROUND)
                continue;
            for (d = 0; d < 4; d++)
                if (rim_within(w, x, y, -dx[d], -dy[d]) && rim_within(w, x, y, dx[d], dy[d])) {
                    out[(size_t)y * w->W + x] = 1;
                    break;
                }
        }
    return out;
}

/* ---- _neck (rel:1423-1496): the narrowest cut between tops and feet, by unit-capacity max-flow ---- */

typedef struct PSet { int32_t *v; size_t n, cap; uint8_t *mark; } PSet;   /* pixels y * W + x, + a W*H membership map */

static void pset_free(PSet *s)
{
    free(s->v);
    free(s->mark);
    memset(s, 0, sizeof(*s));
}

typedef struct Neck {
    const Wrap *w;
    int32_t r;
    int bx0, by0, bw, bh;            /* the search box, clamped to the canvas */
    int32_t *prev;                   /* box index: -2 unvisited, -1 root, else the box index it came from */
    int8_t *fl;                      /* box index * 4 + direction: the flow on that edge */
    uint8_t *isFoot;                 /* box index */
    int32_t *queue;
} Neck;

static const int kDx[4] = {1, -1, 0, 0}, kDy[4] = {0, 0, 1, -1};

static int bidx(const Neck *n, int x, int y)
{
    assert(x >= n->bx0 && x < n->bx0 + n->bw && y >= n->by0 && y < n->by0 + n->bh);
    return (y - n->by0) * n->bw + (x - n->bx0);
}

static bool in_box(const Neck *n, int x, int y)
{
    return x >= n->bx0 && x < n->bx0 + n->bw && y >= n->by0 && y < n->by0 + n->bh;
}

/* steps (rel:1445-1451): the 4-neighbours q of p in region r, neither of them a gap, inside the canvas. */
static bool step_ok(const Neck *n, int px, int py, int qx, int qy)
{
    const Wrap *w = n->w;

    if (qx < 0 || qy < 0 || qx >= w->W || qy >= w->H)
        return false;
    return w->p->region[(size_t)qy * w->W + qx] == n->r && !w->gaps[(size_t)qy * w->W + qx] &&
           !w->gaps[(size_t)py * w->W + px];
}

/* reach() (rel:1454-1464): BFS from tops through unsaturated edges inside the box. Returns the box index of the first
 * foot popped, or -1. prev then holds everything reached (the residual-reachable "near" set when it fails). */
static int reach(Neck *n, const PSet *tops)
{
    size_t head = 0, tail = 0, i;
    int d;

    for (i = 0; i < (size_t)n->bw * (size_t)n->bh; i++)
        n->prev[i] = -2;
    for (i = 0; i < tops->n; i++) {
        int x = tops->v[i] % n->w->W, y = tops->v[i] / n->w->W, b = bidx(n, x, y);

        n->prev[b] = -1;
        n->queue[tail++] = b;
    }
    while (head < tail) {
        int b = n->queue[head++], px = n->bx0 + b % n->bw, py = n->by0 + b / n->bw;

        if (n->isFoot[b])
            return b;
        for (d = 0; d < 4; d++) {
            int qx = px + kDx[d], qy = py + kDy[d], qb;

            if (!step_ok(n, px, py, qx, qy) || !in_box(n, qx, qy))
                continue;
            qb = bidx(n, qx, qy);
            if (n->prev[qb] == -2 && n->fl[b * 4 + d] < 1) {
                n->prev[qb] = b;
                n->queue[tail++] = qb;
            }
        }
    }
    return -1;
}

static void augment(Neck *n, int end)
{
    while (n->prev[end] != -1) {
        int p = n->prev[end], px = p % n->bw, py = p / n->bw, ex = end % n->bw, ey = end / n->bw, d;

        for (d = 0; d < 4; d++)
            if (px + kDx[d] == ex && py + kDy[d] == ey)
                break;
        assert(d < 4);
        n->fl[p * 4 + d]++;
        n->fl[end * 4 + (d ^ 1)]--;
        end = p;
    }
}

static bool pset_push(PSet *s, int32_t v)
{
    if (!vgrow((void **)&s->v, &s->cap, s->n + 1u, sizeof(int32_t)))
        return false;
    s->v[s->n++] = v;
    return true;
}

/* spread (rel:1466-1483). mode 0: from the tops, allowed = near(q) or q outside the box or p not near; None (0) when
 * a foot is reached. mode 1: from the feet, allowed = q not in `side`, stopping once `cap` pixels are held.
 * Returns 1 with *out filled, 0 for None, -1 on no memory. */
static int spread(const Neck *n, const PSet *start, int mode, const PSet *side, size_t cap, PSet *out)
{
    const int W = n->w->W, H = n->w->H;
    size_t head = 0, i;
    int d;

    memset(out, 0, sizeof(*out));
    out->mark = (uint8_t *)calloc((size_t)W * (size_t)H, 1);
    if (out->mark == NULL)
        return -1;
    for (i = 0; i < start->n; i++)
        if (!out->mark[start->v[i]]) {
            out->mark[start->v[i]] = 1;
            if (!pset_push(out, start->v[i]))
                return -1;
        }
    while (head < out->n) {
        int32_t p = out->v[head++];
        int px = p % W, py = p / W;
        bool pNear = in_box(n, px, py) && n->prev[bidx(n, px, py)] != -2;

        for (d = 0; d < 4; d++) {
            int qx = px + kDx[d], qy = py + kDy[d];
            int32_t q;
            bool qNear, ok;

            if (!step_ok(n, px, py, qx, qy))
                continue;
            q = qy * W + qx;
            if (out->mark[q])
                continue;
            qNear = in_box(n, qx, qy) && n->prev[bidx(n, qx, qy)] != -2;
            ok = mode == 0 ? (qNear || !in_box(n, qx, qy) || !pNear) : !side->mark[q];
            if (!ok)
                continue;
            if (mode == 0 && in_box(n, qx, qy) && n->isFoot[bidx(n, qx, qy)])
                return 0;
            out->mark[q] = 1;
            if (!pset_push(out, q))
                return -1;
            if (cap && out->n >= cap)
                return 1;
        }
    }
    return 1;
}

static bool neck_alloc(Neck *n, const PSet *tops, const PSet *feet)
{
    const int W = n->w->W, H = n->w->H;
    int x0 = W, y0 = H, x1 = -1, y1 = -1;
    size_t i, cells;
    const PSet *sets[2];
    unsigned s;

    sets[0] = tops;
    sets[1] = feet;
    for (s = 0; s < 2; s++)
        for (i = 0; i < sets[s]->n; i++) {
            int x = sets[s]->v[i] % W, y = sets[s]->v[i] / W;

            x0 = x < x0 ? x : x0; x1 = x > x1 ? x : x1;
            y0 = y < y0 ? y : y0; y1 = y > y1 ? y : y1;
        }
    assert(x1 >= 0);
    n->bx0 = x0 - RG_WRAP_REACH < 0 ? 0 : x0 - RG_WRAP_REACH;
    n->by0 = y0 - RG_WRAP_REACH < 0 ? 0 : y0 - RG_WRAP_REACH;
    n->bw = (x1 + RG_WRAP_REACH >= W ? W - 1 : x1 + RG_WRAP_REACH) - n->bx0 + 1;
    n->bh = (y1 + RG_WRAP_REACH >= H ? H - 1 : y1 + RG_WRAP_REACH) - n->by0 + 1;
    cells = (size_t)n->bw * (size_t)n->bh;
    n->prev = (int32_t *)malloc(cells * sizeof(int32_t));
    n->queue = (int32_t *)malloc(cells * sizeof(int32_t));
    n->fl = (int8_t *)calloc(cells * 4u, 1);
    n->isFoot = (uint8_t *)calloc(cells, 1);
    if (!n->prev || !n->queue || !n->fl || !n->isFoot)
        return false;
    for (i = 0; i < feet->n; i++)
        n->isFoot[bidx(n, feet->v[i] % W, feet->v[i] / W)] = 1;
    return true;
}

/* _neck: 1 = (side, far, width) found, 0 = None (every cut wider than the limit, or the tops' side meets a foot),
 * -1 = no memory. side/far are filled only for 1 (and must be freed with pset_free in every case). */
static int neck(const Wrap *w, int32_t r, const PSet *tops, const PSet *feet, PSet *side, PSet *far, int *width)
{
    Neck n;
    int rc = -1, k, end;

    assert(tops->n > 0 && feet->n > 0 && w->gaps != NULL);
    memset(&n, 0, sizeof(n));
    n.w = w;
    n.r = r;
    if (neck_alloc(&n, tops, feet)) {
        rc = 0;
        for (k = 0; k <= RG_WRAP_CUT && rc == 0; k++) {
            end = reach(&n, tops);
            if (end >= 0) {
                augment(&n, end);
                continue;
            }
            rc = spread(&n, tops, 0, NULL, 0, side);
            if (rc == 1) {
                rc = spread(&n, feet, 1, side, side->n < RG_WRAP_POCKET ? RG_WRAP_POCKET : 0, far);
                *width = k;
                break;
            }
            break;
        }
    }
    free(n.prev); free(n.queue); free(n.fl); free(n.isFoot);
    return rc;
}

/* ---- pixel sets as sorted unique arrays ---- */

static int i32_cmp(const void *a, const void *b)
{
    int32_t x = *(const int32_t *)a, y = *(const int32_t *)b;

    return x < y ? -1 : x > y ? 1 : 0;
}

static bool pset_from_pairs(PSet *s, const Pair *g, size_t n, bool high, int W)
{
    size_t i, k = 0;

    memset(s, 0, sizeof(*s));
    s->v = (int32_t *)malloc((n ? n : 1u) * sizeof(int32_t));
    if (s->v == NULL)
        return false;
    s->cap = n ? n : 1u;
    for (i = 0; i < n; i++)
        s->v[i] = high ? g[i].hy * W + g[i].hx : g[i].ly * W + g[i].lx;
    qsort(s->v, n, sizeof(int32_t), i32_cmp);
    for (i = 0; i < n; i++)
        if (k == 0 || s->v[k - 1] != s->v[i])
            s->v[k++] = s->v[i];
    s->n = k;
    return true;
}

/* a - b into a fresh set (both sorted unique, no marks). */
static bool pset_minus(PSet *out, const PSet *a, const PSet *b)
{
    size_t i;

    memset(out, 0, sizeof(*out));
    out->v = (int32_t *)malloc((a->n ? a->n : 1u) * sizeof(int32_t));
    if (out->v == NULL)
        return false;
    out->cap = a->n ? a->n : 1u;
    for (i = 0; i < a->n; i++)
        if (bsearch(&a->v[i], b->v, b->n, sizeof(int32_t), i32_cmp) == NULL)
            out->v[out->n++] = a->v[i];
    return true;
}

/* s -= other (other carries a membership map). */
static void pset_remove(PSet *s, const PSet *other)
{
    size_t i, k = 0;

    assert(other->mark != NULL);
    for (i = 0; i < s->n; i++)
        if (!other->mark[s->v[i]])
            s->v[k++] = s->v[i];
    s->n = k;
}

/* The `while tops and feet` loop of split_wrapped (rel:1531-1545): lets go the ends a pocket shuts in. 1 = a cut
 * (side, far valid), 0 = none, -1 = no memory. */
static int find_cut(Wrap *w, int32_t r, PSet *tops, PSet *feet, PSet *side, PSet *far)
{
    while (tops->n > 0 && feet->n > 0) {
        int width = 0, got;

        pset_free(side);
        pset_free(far);
        got = neck(w, r, tops, feet, side, far, &width);
        if (got != 1)
            return got;
        if (width == 0)
            return 0;                   /* a rim's gaps alone part them: nothing to cut */
        if (side->n < RG_WRAP_POCKET)
            pset_remove(tops, side);
        else if (far->n < RG_WRAP_POCKET)
            pset_remove(feet, far);     /* a pocket among the face's pixels */
        else
            return 1;
    }
    return 0;
}

/* rel:1549-1559: the feet's side is the new region; what the gaps shut in stays with the tops' side. */
static bool apply_cut(RgPrep *p, int32_t r, const PSet *far)
{
    uint32_t nn = p->nRegions;
    size_t i;

    if (!rg_prep_add_region(p, (uint32_t)far->n))
        return false;
    for (i = 0; i < far->n; i++)
        p->region[far->v[i]] = (int32_t)nn;
    p->sizes[r] -= (uint32_t)far->n;
    p->cutApart[r] = 1;
    p->cutApart[nn] = 1;
    p->wrapCuts++;
    return true;
}

/* ---- the todo list: (len, region, group, key) ---- */

typedef struct Todo { size_t len; int32_t r; Pair *group; Pair *key; size_t nKey; } Todo;
typedef struct Tried { int32_t r; Pair *key; size_t nKey; } Tried;

static int todo_desc(const void *a, const void *b)       /* sorted(todo, reverse=True) on (len, r, group) */
{
    const Todo *x = (const Todo *)a, *y = (const Todo *)b;
    size_t i;

    if (x->len != y->len) return x->len < y->len ? 1 : -1;
    if (x->r != y->r) return x->r < y->r ? 1 : -1;
    for (i = 0; i < x->len; i++) {
        int c = pair_cmp(&x->group[i], &y->group[i]);

        if (c != 0) return c < 0 ? 1 : -1;
    }
    return 0;
}

static bool was_tried(const Tried *t, size_t nt, int32_t r, const Pair *key, size_t nKey)
{
    size_t i;

    for (i = 0; i < nt; i++)
        if (t[i].r == r && t[i].nKey == nKey && memcmp(t[i].key, key, nKey * sizeof(Pair)) == 0)
            return true;
    return false;
}

/* frozenset(group): the sorted unique pairs. */
static Pair *make_key(const Pair *group, size_t n, size_t *nKey)
{
    Pair *k = (Pair *)malloc((n ? n : 1u) * sizeof(Pair));
    size_t i, m = 0;

    if (k == NULL)
        return NULL;
    memcpy(k, group, n * sizeof(Pair));
    qsort(k, n, sizeof(Pair), pair_qcmp);
    for (i = 0; i < n; i++)
        if (m == 0 || pair_cmp(&k[m - 1], &k[i]) != 0)
            k[m++] = k[i];
    *nKey = m;
    return k;
}

static void todo_free(Todo *t, size_t n)
{
    size_t i;

    for (i = 0; i < n; i++) {
        free(t[i].group);
        free(t[i].key);
    }
    free(t);
}

/* One region's clusters into the todo list: len >= WRAP_COLUMNS and not tried (rel:1507-1513). Takes the groups'
 * pair arrays. */
static bool todo_region(Todo **todo, size_t *nt, size_t *capt, const Tried *tried, size_t nTried, int32_t r,
                        Pair *pairs, size_t n)
{
    Group *gs = NULL;
    size_t ng = 0, i;
    bool ok = clusters(pairs, n, &gs, &ng);

    for (i = 0; ok && i < ng; i++) {
        size_t nKey = 0;
        Pair *key;

        if (gs[i].np < RG_WRAP_COLUMNS)
            continue;
        key = make_key(gs[i].pairs, gs[i].np, &nKey);
        if (key == NULL || !vgrow((void **)todo, capt, *nt + 1u, sizeof(Todo))) {
            free(key);
            ok = false;
            break;
        }
        if (was_tried(tried, nTried, r, key, nKey)) {
            free(key);
            continue;
        }
        (*todo)[*nt].len = gs[i].np;
        (*todo)[*nt].r = r;
        (*todo)[*nt].group = gs[i].pairs;
        (*todo)[*nt].key = key;
        (*todo)[*nt].nKey = nKey;
        gs[i].pairs = NULL;
        (*nt)++;
    }
    for (i = 0; i < ng; i++)
        group_free(&gs[i]);
    free(gs);
    return ok;
}

static bool build_todo(Wrap *w, const Tried *tried, size_t nTried, Todo **todo, size_t *nt)
{
    size_t i = 0, capt = 0, j;

    *todo = NULL;
    *nt = 0;
    while (i < w->nRecs) {
        int32_t r = w->recs[i].r;
        Pair *pairs;
        size_t n = 0;

        for (j = i; j < w->nRecs && w->recs[j].r == r; j++)
            n++;
        pairs = (Pair *)malloc(n * sizeof(Pair));
        if (pairs == NULL) {
            todo_free(*todo, *nt);
            return false;
        }
        for (j = 0; j < n; j++)
            pairs[j] = w->recs[i + j].p;
        if (!todo_region(todo, nt, &capt, tried, nTried, r, pairs, n)) {
            free(pairs);
            todo_free(*todo, *nt);
            return false;
        }
        free(pairs);
        i += n;
    }
    qsort(*todo, *nt, sizeof(Todo), todo_desc);
    return true;
}

/* rel:1520-1560 for one todo entry: 1 = it cut a region, 0 = it did not, -1 = no memory. */
static int try_todo(Wrap *w, const Todo *t)
{
    PSet hi, lo, tops, feet, side, far;
    int rc = -1;

    memset(&side, 0, sizeof(side));
    memset(&far, 0, sizeof(far));
    memset(&tops, 0, sizeof(tops));
    memset(&feet, 0, sizeof(feet));
    memset(&hi, 0, sizeof(hi));
    memset(&lo, 0, sizeof(lo));
    /* an end both above one face and below another (a band's tip in a tile not drawn as a band) says nothing */
    if (pset_from_pairs(&hi, t->group, t->len, true, w->W) && pset_from_pairs(&lo, t->group, t->len, false, w->W) &&
        pset_minus(&tops, &hi, &lo) && pset_minus(&feet, &lo, &hi)) {
        rc = find_cut(w, t->r, &tops, &feet, &side, &far);
        if (rc == 1 && !apply_cut(w->p, t->r, &far))
            rc = -1;
    }
    pset_free(&hi); pset_free(&lo); pset_free(&tops); pset_free(&feet); pset_free(&side); pset_free(&far);
    return rc;
}

static bool wrap_south(Wrap *w, uint8_t **south)
{
    const RgCanvas *cv = &w->p->cv;
    size_t cells = (size_t)cv->cw * (size_t)cv->ch, i;

    *south = (uint8_t *)malloc(cells);
    if (*south == NULL)
        return false;
    for (i = 0; i < cells; i++)
        (*south)[i] = (cv->faceLow[i] || (cv->meta[i] != 0xFFFFu && rg_is_face_south(cv->meta[i]))) ? 1 : 0;
    return true;
}

/* One pass over the sorted todo list: the first entry that cuts ends it (the regions changed). Marks every entry
 * tried up to and including it. 1 = a cut was made, 0 = none, -1 = no memory. */
static int run_todo(Wrap *w, Todo *todo, size_t nt, Tried **tried, size_t *nTried, size_t *capTried)
{
    size_t i;

    for (i = 0; i < nt; i++) {
        int rc;

        if (!vgrow((void **)tried, capTried, *nTried + 1u, sizeof(Tried)))
            return -1;
        (*tried)[*nTried].r = todo[i].r;
        (*tried)[*nTried].key = todo[i].key;            /* ownership moves to the tried list */
        (*tried)[*nTried].nKey = todo[i].nKey;
        todo[i].key = NULL;
        (*nTried)++;
        rc = try_todo(w, &todo[i]);
        if (rc != 0)
            return rc;
    }
    return 0;
}

bool rg_split_wrapped(RgPrep *p)
{
    Wrap w;
    Tried *tried = NULL;
    size_t nTried = 0, capTried = 0, i, guard;
    uint8_t *south = NULL;
    bool ok;

    assert(p != NULL && p->region != NULL && p->cv.kind != NULL);
    memset(&w, 0, sizeof(w));
    w.p = p;
    w.W = p->cv.cw * 16;
    w.H = p->cv.ch * 16;
    ok = wrap_south(&w, &south);
    w.south = south;
    /* Every pass either returns or cuts a region (and tries one more entry): bounded by the pixel count. */
    for (guard = 0; ok && guard <= (size_t)w.W * (size_t)w.H; guard++) {
        Todo *todo = NULL;
        size_t nt = 0;
        int rc;

        ok = collect_pairs(&w) && build_todo(&w, tried, nTried, &todo, &nt);
        if (!ok || nt == 0) {
            todo_free(todo, nt);
            break;
        }
        if (w.gaps == NULL && (w.gaps = rim_gaps(&w)) == NULL) {
            todo_free(todo, nt);
            ok = false;
            break;
        }
        rc = run_todo(&w, todo, nt, &tried, &nTried, &capTried);
        todo_free(todo, nt);
        if (rc < 0) ok = false;
        if (rc <= 0) break;
    }
    for (i = 0; i < nTried; i++)
        free(tried[i].key);
    free(tried); free(w.recs); free(w.gaps); free(south);
    return ok;
}
