/* Modified for 3DGBA (GPLv3), 2026.
 * Original: ZallaxDev/pokeemerald-3Ds-dualscreen @ c330c0a, 3ds_port/scripts/gen_voxel_relief.py (find_drawn,
 * drawn_group, drawn_ok, DRAWN_EXCLUDED, map_links, _seam_cells: rel:633-851), MIT License - see source/voxel/NOTICE.md.
 * Portions Copyright (c) Dust Zallax, MIT. */
/* rg_rdrawn.c -- find_drawn, groups, map links, seam cells (3DGBA, GPLv3). Pure C. */
#include "rg_rdrawn.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "rg_ralias.h"
#include "rg_rrock.h"
#include "rg_rtables.h"

#define SLOTS 512u

typedef struct DrawCtx {
    const RgWorld *w;
    uint8_t *rock[SLOTS];            /* per kept layout: w*h flags, 1 = rock tile (alias applied) */
} DrawCtx;

static void ctx_free(DrawCtx *c)
{
    unsigned i;

    for (i = 0; i < SLOTS; i++) {
        free(c->rock[i]);
        c->rock[i] = NULL;
    }
}

/* rel:640-660: outdoor General-tileset layouts that are not alternates and carry at least one rock tile. */
static RgErr build_blocks(DrawCtx *c, RgDrawn *d)
{
    unsigned i;
    const uint32_t general = c->w->layouts[RG_WORLD_ROOT - 1u].ts[0]->addr;

    assert(c->w->layoutCount < SLOTS);
    for (i = 0; i < RG_OUTDOOR_COUNT; i++) {
        uint16_t id = RG_OUTDOOR_BY_NAME[i];
        const RgLayout *L;
        RgAlias *al = NULL;
        uint8_t *bm;
        unsigned n = 0, x, y;
        RgErr e;

        assert(id >= 1 && id <= c->w->layoutCount);
        L = &c->w->layouts[id - 1u];
        if (rg_relief_alt_base(id) != 0 || L->ts[0]->addr != general || !L->present)
            continue;
        e = rg_alias_of(c->w, id, &al);
        if (e != RG_OK)
            return e;
        bm = (uint8_t *)calloc((size_t)L->w * L->h, 1);
        if (bm == NULL) {
            rg_alias_free(al);
            return RG_ERR_NOMEM;
        }
        for (y = 0; y < L->h; y++)
            for (x = 0; x < L->w; x++)
                if (rg_is_rock_tile(rg_alias_metatile(al, L, (int)x, (int)y))) {
                    bm[y * L->w + x] = 1;
                    n++;
                }
        rg_alias_free(al);
        if (n == 0) {
            free(bm);
            continue;
        }
        c->rock[id] = bm;
        d->blockLayout[id] = 1;
        d->rockCount[id] = (uint16_t)n;
        if (n >= RG_DRAWN_MIN) {
            d->seed[id] = 1;
            d->nSeeds++;
        }
    }
    return RG_OK;
}

/* rel:662-667 rock_near: any rock tile in [x0,x1) x [y0,y1), clamped to the layout. */
static bool rock_near(const DrawCtx *c, uint16_t id, int x0, int y0, int x1, int y1)
{
    const RgLayout *L = &c->w->layouts[id - 1u];
    const uint8_t *bm = c->rock[id];
    int x, y;

    assert(bm != NULL);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > (int)L->w) x1 = (int)L->w;
    if (y1 > (int)L->h) y1 = (int)L->h;
    for (y = y0; y < y1; y++)
        for (x = x0; x < x1; x++)
            if (bm[(unsigned)y * L->w + (unsigned)x])
                return true;
    return false;
}

/* rel:688-707: where b stands against a for one connection, and whether rock meets rock at the seam. */
static bool seam_for(const DrawCtx *c, uint16_t a, uint16_t b, unsigned dir, int off, int *dx, int *dy)
{
    const RgLayout *A = &c->w->layouts[a - 1u], *B = &c->w->layouts[b - 1u];
    const int S = RG_DRAWN_SEAM, aw = A->w, ah = A->h, bw = B->w, bh = B->h;

    switch (dir) {
    case 1:                                       /* down */
        *dx = off; *dy = ah;
        return rock_near(c, a, off, ah - S, off + bw, ah) && rock_near(c, b, -off, 0, -off + aw, S);
    case 2:                                       /* up */
        *dx = off; *dy = -bh;
        return rock_near(c, a, off, 0, off + bw, S) && rock_near(c, b, -off, bh - S, -off + aw, bh);
    case 4:                                       /* right */
        *dx = aw; *dy = off;
        return rock_near(c, a, aw - S, off, aw, off + bh) && rock_near(c, b, 0, -off, S, -off + ah);
    case 3:                                       /* left */
        *dx = -bw; *dy = off;
        return rock_near(c, a, 0, off, S, off + bh) && rock_near(c, b, bw - S, -off, bw, -off + ah);
    default:
        return false;
    }
}

static RgErr add_link(RgDrawn *d, uint16_t a, uint16_t b, int dx, int dy)
{
    if (d->nLinks >= RG_DRAWN_MAX_LINKS)
        return RG_ERR_TABLES;
    d->links[d->nLinks].a = a;
    d->links[d->nLinks].b = b;
    d->links[d->nLinks].dx = (int16_t)dx;
    d->links[d->nLinks].dy = (int16_t)dy;
    d->nLinks++;
    return RG_OK;
}

/* rel:668-712: maps in folder order, each connection between two kept layouts that has rock at its seam. */
static RgErr build_links(const DrawCtx *c, RgDrawn *d)
{
    unsigned i, k, n;
    RgConn cn[16];

    for (i = 0; i < RG_OUTDOOR_MAP_COUNT; i++) {
        const RgMap *m = rg_world_map(c->w, RG_OUTDOOR_MAPS_BY_FOLDER[i].group, RG_OUTDOOR_MAPS_BY_FOLDER[i].num);
        uint16_t a;

        if (m == NULL)
            return RG_ERR_TABLES;
        a = m->layoutId;
        if (a >= SLOTS || !d->blockLayout[a])
            continue;
        n = rg_map_connections(c->w, m->group, m->num, cn, 16);
        assert(n <= 16);
        for (k = 0; k < n; k++) {
            const RgMap *t = rg_world_map(c->w, cn[k].group, cn[k].num);
            int dx = 0, dy = 0;
            RgErr e;

            if (t == NULL || t->layoutId >= SLOTS || !d->blockLayout[t->layoutId] || t->layoutId == a)
                continue;
            if (!seam_for(c, a, t->layoutId, cn[k].dir, cn[k].offset, &dx, &dy))
                continue;
            e = add_link(d, a, t->layoutId, dx, dy);
            if (e == RG_OK)
                e = add_link(d, t->layoutId, a, -dx, -dy);
            if (e != RG_OK)
                return e;
        }
    }
    return RG_OK;
}

typedef struct Placed { int has; int x, y; } Placed;

/* rel:713-722: FIFO walk over the links from one seed; members in discovery order, shifted to min (0, 0). */
static RgErr bfs_group(const RgWorld *w, RgDrawn *d, uint16_t seedId, uint8_t *seen)
{
    Placed pos[SLOTS];
    uint16_t queue[SLOTS];
    unsigned head = 0, tail = 0, i, first = d->nPool;
    RgDrawnGroup *g;
    int mx = 0, my = 0;

    memset(pos, 0, sizeof pos);
    pos[seedId].has = 1;
    queue[tail++] = seedId;
    while (head < tail) {
        uint16_t a = queue[head++];

        for (i = 0; i < d->nLinks; i++) {
            const RgDrawnLink *l = &d->links[i];

            if (l->a != a || pos[l->b].has)
                continue;
            pos[l->b].has = 1;
            pos[l->b].x = pos[a].x + l->dx;
            pos[l->b].y = pos[a].y + l->dy;
            assert(tail < SLOTS);
            queue[tail++] = l->b;
        }
    }
    if (d->nPool + tail > RG_DRAWN_MAX_MEMBERS || d->nGroups >= RG_DRAWN_MAX_GROUPS)
        return RG_ERR_TABLES;
    for (i = 0; i < tail; i++) {
        mx = (i == 0 || pos[queue[i]].x < mx) ? pos[queue[i]].x : mx;
        my = (i == 0 || pos[queue[i]].y < my) ? pos[queue[i]].y : my;
        seen[queue[i]] = 1;
    }
    g = &d->groups[d->nGroups++];
    memset(g, 0, sizeof *g);
    g->key = seedId;
    g->first = first;
    g->nMembers = tail;
    for (i = 0; i < tail; i++) {
        const RgLayout *L = &w->layouts[queue[i] - 1u];
        RgDrawnMember *m = &d->pool[d->nPool++];

        m->layout = queue[i];
        m->x = (int16_t)(pos[queue[i]].x - mx);
        m->y = (int16_t)(pos[queue[i]].y - my);
        if ((unsigned)m->x + L->w > g->cellsW) g->cellsW = (unsigned)m->x + L->w;
        if ((unsigned)m->y + L->h > g->cellsH) g->cellsH = (unsigned)m->y + L->h;
    }
    return RG_OK;
}

/* rel:723-732: a layout a script swaps in is solved in its map's place, as a group of its own. */
static RgErr alt_groups(const RgWorld *w, RgDrawn *d)
{
    const unsigned nOrig = d->nGroups;
    unsigned order[RG_ALT_COUNT], k, j, i;

    for (k = 0; k < RG_ALT_COUNT; k++)
        order[k] = k;
    for (k = 1; k < RG_ALT_COUNT; k++)            /* sorted(alternates.items()): by alt name = A.3 rank */
        for (j = k; j > 0 && rg_name_rank(RG_OUTDOOR_ALTS[order[j]].alt) < rg_name_rank(RG_OUTDOOR_ALTS[order[j - 1]].alt); j--) {
            unsigned t = order[j]; order[j] = order[j - 1]; order[j - 1] = t;
        }
    for (k = 0; k < RG_ALT_COUNT; k++) {
        const RgOutdoorAlt *al = &RG_OUTDOOR_ALTS[order[k]];

        for (j = 0; j < nOrig; j++) {
            const RgDrawnGroup *src = &d->groups[j];
            RgDrawnGroup *g;
            bool has = false;

            for (i = 0; i < src->nMembers; i++)
                has = has || d->pool[src->first + i].layout == al->base;
            if (!has)
                continue;
            if (d->nPool + src->nMembers > RG_DRAWN_MAX_MEMBERS || d->nGroups >= RG_DRAWN_MAX_GROUPS)
                return RG_ERR_TABLES;
            g = &d->groups[d->nGroups++];
            *g = *src;
            g->key = al->alt;
            g->isAlt = 1;
            g->first = d->nPool;
            for (i = 0; i < src->nMembers; i++) {
                d->pool[d->nPool] = d->pool[src->first + i];
                if (d->pool[d->nPool].layout == al->base)
                    d->pool[d->nPool].layout = al->alt;
                d->nPool++;
            }
        }
    }
    (void)w;
    return RG_OK;
}

RgErr rg_drawn_find(const RgWorld *w, RgDrawn *out)
{
    DrawCtx *c;
    uint8_t seen[SLOTS];
    unsigned i;
    RgErr e;

    assert(w != NULL && out != NULL);
    memset(out, 0, sizeof *out);
    memset(seen, 0, sizeof seen);
    c = (DrawCtx *)calloc(1, sizeof *c);
    if (c == NULL)
        return RG_ERR_NOMEM;
    c->w = w;
    e = build_blocks(c, out);
    if (e == RG_OK)
        e = build_links(c, out);
    for (i = 0; e == RG_OK && i < RG_OUTDOOR_COUNT; i++) {       /* sorted(seeds): A.3 name order */
        uint16_t id = RG_OUTDOOR_BY_NAME[i];

        if (out->seed[id] && !seen[id])
            e = bfs_group(w, out, id, seen);
    }
    if (e == RG_OK)
        e = alt_groups(w, out);
    ctx_free(c);
    free(c);
    return e;
}

const RgDrawnGroup *rg_drawn_group(const RgDrawn *d, uint16_t layoutId, bool checked, const uint8_t *regionOk)
{
    unsigned g, i;

    assert(d != NULL);
    for (g = 0; g < d->nGroups; g++) {
        const RgDrawnGroup *gr = &d->groups[g];

        for (i = 0; i < gr->nMembers; i++) {
            if (d->pool[gr->first + i].layout != layoutId)
                continue;
            if (checked && (rg_drawn_excluded(gr) || (regionOk != NULL && !regionOk[g])))
                return NULL;
            return gr;
        }
    }
    return NULL;
}

bool rg_drawn_excluded(const RgDrawnGroup *g)
{
    assert(g != NULL);
    return g->key == RG_EXCLUDED_GROUP_SEED && !g->isAlt;
}

/* ---- map_links (rel:787-831) ---- */

static int link_cmp(const void *pa, const void *pb)
{
    const RgMapLink *a = (const RgMapLink *)pa, *b = (const RgMapLink *)pb;
    unsigned ra = rg_name_rank(a->a), rb = rg_name_rank(b->a);

    if (ra != rb) return ra < rb ? -1 : 1;
    ra = rg_name_rank(a->b); rb = rg_name_rank(b->b);
    if (ra != rb) return ra < rb ? -1 : 1;
    ra = rg_dir_sort_key(a->dir); rb = rg_dir_sort_key(b->dir);
    if (ra != rb) return ra < rb ? -1 : 1;
    return a->offset < b->offset ? -1 : a->offset > b->offset ? 1 : 0;
}

static unsigned push_link(RgMapLink *out, unsigned n, unsigned max, uint16_t a, uint16_t b, uint8_t dir, int32_t off)
{
    assert(n < max);
    out[n].a = a; out[n].b = b; out[n].dir = dir; out[n].offset = off;
    return n + 1;
}

static unsigned dedupe(RgMapLink *out, unsigned n)
{
    unsigned i, k = 0;

    for (i = 0; i < n; i++)
        if (k == 0 || link_cmp(&out[k - 1], &out[i]) != 0)
            out[k++] = out[i];
    return k;
}

unsigned rg_map_links_sorted(const RgWorld *w, RgMapLink *out, unsigned max)
{
    unsigned i, k, n = 0, n0, j;
    RgConn cn[16];

    assert(w != NULL && out != NULL && max >= 2);
    for (i = 0; i < RG_OUTDOOR_MAP_COUNT; i++) {
        const RgMap *m = rg_world_map(w, RG_OUTDOOR_MAPS_BY_FOLDER[i].group, RG_OUTDOOR_MAPS_BY_FOLDER[i].num);
        unsigned nc;

        if (m == NULL || !rg_relief_outdoor(m->layoutId))
            continue;
        nc = rg_map_connections(w, m->group, m->num, cn, 16);
        assert(nc <= 16);
        for (k = 0; k < nc; k++) {
            const RgMap *t = rg_world_map(w, cn[k].group, cn[k].num);

            if (t != NULL && rg_relief_outdoor(t->layoutId) && t->layoutId != m->layoutId && n + 1 < max / 2)
                n = push_link(out, n, max, m->layoutId, t->layoutId, cn[k].dir, cn[k].offset);
        }
    }
    qsort(out, n, sizeof out[0], link_cmp);
    n = dedupe(out, n);
    n0 = n;
    for (k = 0; k < RG_ALT_COUNT; k++)                 /* mirror an alternate's base's links (snapshot of n0) */
        for (j = 0; j < n0; j++) {
            const RgOutdoorAlt *al = &RG_OUTDOOR_ALTS[k];
            RgMapLink l = out[j];

            if (l.a == al->base && n < max)
                n = push_link(out, n, max, al->alt, l.b, l.dir, l.offset);
            if (l.b == al->base && n < max)
                n = push_link(out, n, max, l.a, al->alt, l.dir, l.offset);
        }
    qsort(out, n, sizeof out[0], link_cmp);
    return dedupe(out, n);
}

/* ---- _seam_cells (rel:834-851) ---- */

unsigned rg_seam_cells(uint8_t dir, int offset, int aw, int ah, int bw, int bh, RgSeamCell *out, unsigned max)
{
    unsigned n = 0;
    int i, lo, hi;
    const bool vertical = (dir == 1 || dir == 2);

    assert(dir >= 1 && dir <= 4 && out != NULL);
    lo = offset > 0 ? offset : 0;
    hi = vertical ? (aw < offset + bw ? aw : offset + bw) : (ah < offset + bh ? ah : offset + bh);
    for (i = lo; i < hi; i++) {
        assert(n < max);
        out[n].aEdge = dir;
        out[n].aIdx = (int16_t)i;
        out[n].bEdge = (uint8_t)(dir == 1 ? 2 : dir == 2 ? 1 : dir == 3 ? 4 : 3);
        out[n].bIdx = (int16_t)(i - offset);
        n++;
    }
    return n;
}
