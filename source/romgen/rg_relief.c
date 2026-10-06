/* Modified for 3DGBA (GPLv3), 2026.
 * Original: ZallaxDev/pokeemerald-3Ds-dualscreen @ c330c0a, 3ds_port/scripts/gen_voxel_relief.py (layout_heights,
 * relief_cells, cell_grid, export), MIT License - see source/voxel/NOTICE.md. Portions Copyright (c) Dust Zallax, MIT. */
#include "rg_relief.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <time.h>

#include "rg_ledge.h"
#include "rg_ralias.h"
#include "rg_rcut.h"
#include "rg_rdrawn.h"
#include "rg_relief_write.h"
#include "rg_rshape.h"
#include "rg_rsolve.h"
#include "rg_rtables.h"
#include "rg_rworld.h"

/* H14: max(-128, min(127, int(round(v)))), half-even (nearbyint under FE_TONEAREST). */
static int8_t stored(double v, int unit)
{
    double r = nearbyint(v / (double)unit);

    return (int8_t)(r < -128.0 ? -128 : (r > 127.0 ? 127 : (int)r));
}

int rg_relief_cells(const RgLat *lat, int unit, uint8_t **out)
{
    unsigned n = 0, cap = 64;
    uint8_t *buf = (uint8_t *)malloc((size_t)cap * RG_RELIEF_CELL_BYTES);
    int x, y;

    assert(lat != NULL && out != NULL && unit >= 1);
    *out = NULL;
    if (buf == NULL)
        return -1;
    for (y = 0; y < lat->h; y++) {
        for (x = 0; x < lat->w; x++) {
            RgGrid g;
            int i, j;
            bool any = false;

            rg_cell_grid(lat, x, y, &g);
            for (j = 0; j < RG_SIDE; j++)
                for (i = 0; i < RG_SIDE; i++)
                    any = any || fabs(g.g[j][i]) > 0.25;
            if (!any)
                continue;
            if (n == cap) {
                uint8_t *nb = (uint8_t *)realloc(buf, (size_t)cap * 2u * RG_RELIEF_CELL_BYTES);

                if (nb == NULL) {
                    free(buf);
                    return -1;
                }
                buf = nb;
                cap *= 2u;
            }
            buf[(size_t)n * RG_RELIEF_CELL_BYTES] = (uint8_t)x;
            buf[(size_t)n * RG_RELIEF_CELL_BYTES + 1] = (uint8_t)y;
            for (j = 0; j < RG_SIDE; j++)
                for (i = 0; i < RG_SIDE; i++)
                    buf[(size_t)n * RG_RELIEF_CELL_BYTES + 2u + (size_t)(j * RG_SIDE + i)] = (uint8_t)stored(g.g[j][i], unit);
            n++;
        }
    }
    if (n == 0) {
        free(buf);
        return 0;
    }
    *out = buf;
    return (int)n;
}

/* One ledge layout: flat lattice + berms -> a row (S3a: base 0, flags clear; a row only when it has cells). */
static RgErr ledge_row(const RgWorld *w, RgPair *pair, const RgLayout *L, RgReliefRow *row, uint8_t **cells,
                       RgReliefStats *st)
{
    RgLat lat;
    int led, n;
    RgErr e = RG_OK;

    (void)w;
    if (L->w > 255u || L->h > 255u)
        return RG_ERR_RELIEF;
    if (!rg_lat_new(&lat, (int)L->w, (int)L->h))
        return RG_ERR_NOMEM;
    led = rg_ledge_berms(L, pair, &lat);
    n = led < 0 ? -1 : rg_relief_cells(&lat, 1, cells);
    rg_lat_free(&lat);
    if (n < 0)
        return RG_ERR_NOMEM;
    st->ledgeCells += (unsigned)led;
    row->id = L->id;
    row->w = L->w;
    row->hFlags = L->h;
    row->base = 0;
    row->nCells = (uint32_t)n;
    row->cells = *cells;
    return e;
}

typedef struct Ledges {
    uint16_t ids[512];
    unsigned n;
    RgReliefRow rows[512];
    uint8_t *cells[512];
    bool have[512];
} Ledges;

static void ledges_free(Ledges *l)
{
    unsigned i;

    for (i = 0; i < l->n; i++)
        free(l->cells[i]);
    free(l);
}

/* All ledge layouts, one open pair at a time (a pair's layouts are consecutive in this walk). */
static RgErr ledges_run(const RgWorld *w, Ledges *l, RgProgressFn progress, void *ctx, const volatile int *cancel,
                        RgReliefStats *st)
{
    unsigned pi, i, done = 0;
    RgErr e = RG_OK;

    for (pi = 0; pi < w->pairCount && e == RG_OK; pi++) {
        RgPair *pair = NULL;

        for (i = 0; i < l->n && e == RG_OK; i++) {
            const RgLayout *L = &w->layouts[l->ids[i] - 1u];

            if (L->pairIndex != pi)
                continue;
            if (cancel != NULL && *cancel != 0) {
                e = RG_ERR_CANCELLED;
                break;
            }
            if (pair == NULL && (pair = rg_pair_open(w, (uint16_t)pi)) == NULL) {
                e = RG_ERR_NOMEM;
                break;
            }
            e = ledge_row(w, pair, L, &l->rows[i], &l->cells[i], st);
            l->have[i] = e == RG_OK;
            if (e == RG_OK && progress != NULL)
                progress(ctx, ++done, l->n);
        }
        if (pair != NULL)
            rg_pair_close(pair);
    }
    return e;
}

static RgErr ledges_serialise(Ledges *l, uint8_t **out, size_t *outSize, RgReliefStats *st)
{
    RgReliefRow rows[512];
    unsigned i, n = 0;
    size_t sz = 0;
    RgErr e;

    /* ids ascend (rg_ledge_layouts walks layouts by id); a row exists only with cells (rel export `continue`) */
    for (i = 0; i < l->n; i++) {
        if (!l->have[i] || l->rows[i].nCells == 0)
            continue;
        rows[n++] = l->rows[i];
        st->cells += l->rows[i].nCells;
    }
    st->rows = n;
    e = rg_relief_write(rows, n, NULL, 0, NULL, 0, NULL, 0, &sz);
    if (e != RG_OK)
        return e;
    *out = (uint8_t *)malloc(sz);
    if (*out == NULL)
        return RG_ERR_NOMEM;
    e = rg_relief_write(rows, n, NULL, 0, NULL, 0, *out, sz, outSize);
    if (e != RG_OK) {
        free(*out);
        *out = NULL;
    }
    return e;
}

/* ---- FULL: export (rel:3683-3808) ---- */

#define RG_HEIGHT_UNIT 2             /* rel:2811: pixels per stored step of a drawn map's height */

typedef struct Full {
    const RgWorld *w;
    const RgRoles *r;
    RgDrawn *d;
    RgLevels *lv;
    RgSolved *sv;
    RgCutShared shared;
    RgVariant *var;
    uint32_t *varAddr;               /* the own tileset address of each variant's half */
    unsigned nVar, capVar;
    RgCut *cuts;
    unsigned nCuts, capCuts;
    RgReliefRow rows[512];
    uint8_t *cells[512];
    uint8_t have[512];
    RgReliefStats *st;
} Full;

/* One layout being exported. */
typedef struct Lay {
    Full *X;
    uint16_t lid;
    const RgLayout *L;
    RgPair *pair;
    RgAlias *alias;
    RgCutArt art;
    const RgDrawnGroup *G;
    RgLat h;
    int W, H, unit;
    RgGrid *cg;                      /* the cells that will be written, w*h */
    uint8_t *have;
    int32_t *got;                    /* w*h: index into X->cuts of this layout's cut at the cell, or -1 */
} Lay;

static bool relief_cell(const RgGrid *g)
{
    int i, j;

    for (j = 0; j < RG_SIDE; j++)
        for (i = 0; i < RG_SIDE; i++)
            if (fabs(g->g[j][i]) > 0.25)
                return true;
    return false;
}

static void lay_add_cell(Lay *Y, int x, int y)
{
    size_t c = (size_t)y * (size_t)Y->W + (size_t)x;

    if (!Y->have[c]) {
        Y->have[c] = 1;
        rg_cell_grid(&Y->h, x, y, &Y->cg[c]);
    }
}

static bool vec_grow(void **p, unsigned *cap, unsigned n, size_t sz)
{
    void *nb;
    unsigned nc;

    if (n < *cap)
        return true;
    nc = *cap ? *cap * 2u : 256u;
    nb = realloc(*p, (size_t)nc * sz);
    if (nb == NULL)
        return false;
    *p = nb;
    *cap = nc;
    return true;
}

static int variant_of(Full *X, uint32_t addr, uint16_t m, const uint16_t mask[16], uint16_t layout)
{
    unsigned i;

    for (i = 0; i < X->nVar; i++)
        if (X->varAddr[i] == addr && X->var[i].metatile == m && memcmp(X->var[i].mask, mask, 32) == 0)
            return (int)i;
    if (!vec_grow((void **)&X->var, &X->capVar, X->nVar, sizeof(RgVariant)))
        return -1;
    {
        uint32_t *na = (uint32_t *)realloc(X->varAddr, (size_t)X->capVar * sizeof(uint32_t));

        if (na == NULL)
            return -1;
        X->varAddr = na;
    }
    X->var[X->nVar].firstLayout = layout;
    X->var[X->nVar].metatile = m;
    memcpy(X->var[X->nVar].mask, mask, 32);
    X->varAddr[X->nVar] = addr;
    return (int)X->nVar++;
}

static int16_t foot_px(double foot, int unit)
{
    return (int16_t)(unit * (int)nearbyint(foot / (double)unit));
}

static uint16_t own_id(const Lay *Y, int32_t m)
{
    return m < 0 ? 0xFFFFu : rg_alias_own_id(Y->alias, (uint16_t)m);
}

static RgErr cut_push(Lay *Y, uint8_t x, uint8_t y, uint16_t variant, int16_t foot, uint16_t behind)
{
    Full *X = Y->X;
    RgCut *c;

    if (!vec_grow((void **)&X->cuts, &X->capCuts, X->nCuts, sizeof(RgCut)))
        return RG_ERR_NOMEM;
    c = &X->cuts[X->nCuts];
    c->layout = Y->lid;
    c->x = x;
    c->y = y;
    c->variant = variant;
    c->foot = foot;
    c->behind = behind;
    c->wall = 0xFFFFu;
    c->sides = 0xFFu;
    c->flags = 0;
    Y->got[(size_t)y * (size_t)Y->W + x] = (int32_t)X->nCuts;
    X->nCuts++;
    return RG_OK;
}

/* the cut tiles first (rel:3719-3737); flat[] gets the plain-background flag */
static RgErr export_cuts(Lay *Y, const RgCutRec *cut, unsigned n, uint8_t *flat)
{
    unsigned i;

    for (i = 0; i < n; i++) {
        const RgCutRec *c = &cut[i];
        uint16_t own = rg_cut_own(&Y->art, c->x, c->y);
        int v;
        RgErr e;

        if (c->behind >= 0 && rg_cut_plain_background(&Y->art, c->meta, c->mask, (uint16_t)c->behind))
            flat[(size_t)c->y * (size_t)Y->W + c->x] = 1;
        v = variant_of(Y->X, own < 512u ? Y->L->ts[0]->addr : Y->L->ts[1]->addr, own, c->mask, Y->lid);
        if (v < 0)
            return RG_ERR_NOMEM;
        e = cut_push(Y, c->x, c->y, (uint16_t)v, foot_px(c->foot, Y->unit), own_id(Y, c->behind));
        if (e != RG_OK)
            return e;
        lay_add_cell(Y, c->x, c->y);
    }
    return RG_OK;
}

/* the shapes: fills, walls, flags, the shaped grids (rel:3738-3767) */
static RgErr export_shapes(Lay *Y, const RgShapes *s, const uint8_t *flat)
{
    Full *X = Y->X;
    uint8_t *goesOn = (uint8_t *)calloc((size_t)Y->W * (size_t)Y->H, 1);
    unsigned i;
    int x, y;
    RgErr e = RG_OK;

    if (goesOn == NULL)
        return RG_ERR_NOMEM;
    for (i = 0; i < s->nFills && e == RG_OK; i++) {
        const RgFill *f = &s->fills[i];

        e = cut_push(Y, f->x, f->y, 0xFFFFu, foot_px(f->foot, Y->unit), own_id(Y, f->ground));
        if (f->goesOn == RG_GOES_ON)
            goesOn[(size_t)f->y * (size_t)Y->W + f->x] = 1;
    }
    for (y = 0; y < Y->H && e == RG_OK; y++)
        for (x = 0; x < Y->W && e == RG_OK; x++)
            if (s->hasWall[(size_t)y * (size_t)Y->W + x] && Y->got[(size_t)y * (size_t)Y->W + x] < 0)
                e = cut_push(Y, (uint8_t)x, (uint8_t)y, 0xFFFFu, 0, 0xFFFFu);
    for (y = 0; y < Y->H && e == RG_OK; y++) {
        for (x = 0; x < Y->W; x++) {
            size_t c = (size_t)y * (size_t)Y->W + (size_t)x;
            RgCut *cut;

            if (Y->got[c] < 0)
                continue;
            cut = &X->cuts[Y->got[c]];
            if (s->hasWall[c]) {
                uint16_t face = s->wallFace[c];

                cut->wall = (face == 0xFFFFu || face == RG_NO_FACE) ? face : own_id(Y, face);
                cut->sides = s->wallBits[c];
            }
            cut->flags = (uint8_t)((flat[c] ? 1u : 0u) | (goesOn[c] ? (unsigned)RG_GOES_ON : 0u));
            lay_add_cell(Y, x, y);
        }
    }
    for (y = 0; y < Y->H; y++)
        for (x = 0; x < Y->W; x++) {
            size_t c = (size_t)y * (size_t)Y->W + (size_t)x;

            if (s->has[c])
                lay_add_cell(Y, x, y);
        }
    for (y = 0; y < Y->H; y++)
        for (x = 0; x < Y->W; x++) {
            size_t c = (size_t)y * (size_t)Y->W + (size_t)x;

            if (s->has[c])
                Y->cg[c] = s->grid[c];
        }
    free(goesOn);
    return e;
}

static unsigned role_relief(const RgRoles *r, uint16_t lid, int w, int x, int y)
{
    const uint8_t *ro = rg_roles_of(r, lid);
    uint8_t v = ro != NULL ? ro[(size_t)y * (size_t)w + (size_t)x] : 0;

    return v == 9u || v == 7u || v == 3u;       /* VOXEL_ROLE cliff, shelf, stair = RELIEF_ROLES */
}

/* rel:3769-3779: a blocked cell of relief role, or one the group's shift lifts, is written level or not */
static void export_blocked(Lay *Y)
{
    int x, y;

    for (y = 0; y < Y->H; y++) {
        for (x = 0; x < Y->W; x++) {
            RgGrid sg;

            if (Y->have[(size_t)y * (size_t)Y->W + (size_t)x])
                continue;
            if (rg_blocked(Y->L, x, y) && role_relief(Y->X->r, Y->lid, Y->W, x, y)) {
                lay_add_cell(Y, x, y);
                continue;
            }
            rg_cell_grid(&Y->X->sv->shift[Y->lid], x, y, &sg);
            if (relief_cell(&sg))
                lay_add_cell(Y, x, y);
        }
    }
}

static bool all_within_120(const RgLat *h)
{
    size_t i, n = (size_t)(h->w * RG_P + 1) * (size_t)(h->h * RG_P + 1);

    for (i = 0; i < n; i++)
        if (h->v[i] < -120.0 || h->v[i] > 120.0)
            return false;
    return true;
}

static RgErr export_drawn(Lay *Y)
{
    Full *X = Y->X;
    const RgSolvedGroup *grp = X->sv->grp[Y->G - X->d->groups];
    size_t n = (size_t)Y->W * (size_t)Y->H;
    uint8_t *rock = (uint8_t *)calloc(n, 1), *flat = (uint8_t *)calloc(n, 1);
    RgCutRec *cut = NULL;
    RgShapes shp;
    RgShapeIn in;
    unsigned i;
    int x, y, nc = 0;
    bool found = false;
    RgErr e = RG_OK;

    if (rock == NULL || flat == NULL || grp == NULL) {
        free(rock);
        free(flat);
        return grp == NULL ? RG_ERR_RELIEF : RG_ERR_NOMEM;
    }
    memset(&in, 0, sizeof(in));
    for (i = 0; i < grp->nMem; i++)
        if (grp->mem[i].layout == Y->lid) {
            in.ox = grp->mem[i].ox;
            in.oy = grp->mem[i].oy;
            found = true;
        }
    if (!found) {
        free(rock);
        free(flat);
        return RG_ERR_RELIEF;           /* the group lists every layout it owns */
    }
    for (y = 0; y < Y->H; y++)
        for (x = 0; x < Y->W; x++)
            rock[(size_t)y * (size_t)Y->W + (size_t)x] =
                grp->tile[(size_t)(in.oy + y) * (size_t)grp->cw + (size_t)(in.ox + x)] != 0;
    nc = rg_cut_cells(&Y->art, rock, &Y->h, &cut);
    if (nc < 0)
        e = RG_ERR_NOMEM;
    if (e == RG_OK)
        e = export_cuts(Y, cut, (unsigned)nc, flat);
    in.art = &Y->art;
    in.L = Y->L;
    in.grp = grp;
    in.wrap = rg_group_is_wrap(Y->G);
    in.h = &Y->h;
    in.cut = cut;
    in.nCut = (unsigned)nc;
    if (e == RG_OK)
        e = rg_cell_shapes(&in, &shp);
    if (e == RG_OK) {
        e = export_shapes(Y, &shp, flat);
        rg_shapes_free(&shp);
    }
    if (e == RG_OK)
        export_blocked(Y);
    free(cut);
    free(rock);
    free(flat);
    return e;
}

/* one layout's row, or no row when it has no cells and no lift (rel:3780-3783) */
static RgErr lay_row(Lay *Y, bool drawn)
{
    Full *X = Y->X;
    size_t n = (size_t)Y->W * (size_t)Y->H, c, cnt = 0;
    int32_t lift = drawn ? (X->sv->have[Y->lid] ? X->sv->base[Y->lid] : 0) : (X->lv->hasBase[Y->lid] ? X->lv->base[Y->lid] : 0);
    uint8_t *buf;
    int x, y, i, j;

    for (c = 0; c < n; c++)
        cnt += Y->have[c];
    if (cnt == 0 && lift == 0)
        return RG_OK;
    buf = (uint8_t *)malloc(cnt ? cnt * RG_RELIEF_CELL_BYTES : 1u);
    if (buf == NULL)
        return RG_ERR_NOMEM;
    cnt = 0;
    for (y = 0; y < Y->H; y++) {
        for (x = 0; x < Y->W; x++) {
            c = (size_t)y * (size_t)Y->W + (size_t)x;
            if (!Y->have[c])
                continue;
            buf[cnt * RG_RELIEF_CELL_BYTES] = (uint8_t)x;
            buf[cnt * RG_RELIEF_CELL_BYTES + 1] = (uint8_t)y;
            for (j = 0; j < RG_SIDE; j++)
                for (i = 0; i < RG_SIDE; i++)
                    buf[cnt * RG_RELIEF_CELL_BYTES + 2u + (size_t)(j * RG_SIDE + i)] =
                        (uint8_t)stored(Y->cg[c].g[j][i], Y->unit);
            cnt++;
        }
    }
    if (Y->W > 255 || Y->H > 255 || lift < -32768 || lift > 32767) {
        free(buf);
        return RG_ERR_RELIEF;
    }
    X->cells[Y->lid] = buf;
    X->rows[Y->lid].id = Y->lid;
    X->rows[Y->lid].w = (uint16_t)Y->W;
    X->rows[Y->lid].hFlags = (uint16_t)(Y->H | (drawn ? 0x8000 : 0) | (Y->unit == 2 ? 0x4000 : 0));
    X->rows[Y->lid].base = (int16_t)lift;
    X->rows[Y->lid].nCells = (uint32_t)cnt;
    X->rows[Y->lid].cells = buf;
    X->have[Y->lid] = 1;
    X->st->drawnRows += drawn ? 1u : 0u;
    return RG_OK;
}

static void lay_close(Lay *Y)
{
    rg_cut_art_free(&Y->art);
    rg_alias_free(Y->alias);
    if (Y->pair != NULL)
        rg_pair_close(Y->pair);
    if (Y->h.v != NULL)
        rg_lat_free(&Y->h);
    free(Y->cg);
    free(Y->have);
    free(Y->got);
    memset(Y, 0, sizeof(*Y));
}

static RgErr lay_open(Lay *Y, Full *X, uint16_t lid)
{
    size_t n;
    int led = 0;
    RgErr e;

    memset(Y, 0, sizeof(*Y));
    Y->X = X;
    Y->lid = lid;
    Y->L = &X->w->layouts[lid - 1u];
    Y->W = (int)Y->L->w;
    Y->H = (int)Y->L->h;
    Y->unit = 1;
    n = (size_t)Y->W * (size_t)Y->H;
    Y->G = rg_drawn_group(X->d, lid, true, X->lv->ok);
    e = rg_layout_heights(X->w, X->r, X->d, X->lv, X->sv, lid, &Y->h, &led);
    if (e != RG_OK)
        return e;
    X->st->ledgeCells += (unsigned)led;
    Y->pair = rg_pair_open(X->w, Y->L->pairIndex);
    Y->cg = (RgGrid *)calloc(n, sizeof(RgGrid));
    Y->have = (uint8_t *)calloc(n, 1);
    Y->got = (int32_t *)malloc(n * sizeof(int32_t));
    if (Y->pair == NULL || Y->cg == NULL || Y->have == NULL || Y->got == NULL) {
        lay_close(Y);
        return RG_ERR_NOMEM;
    }
    memset(Y->got, 0xFF, n * sizeof(int32_t));
    if (Y->G != NULL && (e = rg_alias_of(X->w, lid, &Y->alias)) != RG_OK) {
        lay_close(Y);
        return e;
    }
    rg_cut_art_init(&Y->art, &X->shared, X->w, Y->L, Y->pair, Y->alias);
    return RG_OK;
}

static RgErr export_layout(Full *X, uint16_t lid)
{
    Lay Y;
    RgErr e = lay_open(&Y, X, lid);
    bool drawn;
    int x, y;

    if (e != RG_OK)
        return e;
    drawn = Y.G != NULL;
    for (y = 0; y < Y.H; y++)
        for (x = 0; x < Y.W; x++) {
            RgGrid g;

            rg_cell_grid(&Y.h, x, y, &g);
            if (relief_cell(&g))
                lay_add_cell(&Y, x, y);
        }
    if (drawn) {
        Y.unit = (Y.alias != NULL && all_within_120(&Y.h)) ? 1 : RG_HEIGHT_UNIT;
        e = export_drawn(&Y);
    }
    if (e == RG_OK && X->shared.oom)
        e = RG_ERR_NOMEM;
    if (e == RG_OK)
        e = lay_row(&Y, drawn);
    lay_close(&Y);
    return e;
}

/* the layout sequence (rel:3824-3830 + 3698): ENABLED, drawn members, ledge layouts, then the world-lifted rest */
static unsigned layout_sequence(Full *X, uint16_t *seq)
{
    uint8_t seen[512];
    uint16_t ids[512], rest[512];
    unsigned n = 0, i, nl, nr = 0, j;

    memset(seen, 0, sizeof(seen));
#define ADD(l) do { if (!seen[(l)]) { seen[(l)] = 1; seq[n++] = (uint16_t)(l); } } while (0)
    ADD(RG_ENABLED[0]);
    ADD(RG_ENABLED[1]);
    for (i = 0; i < X->d->nPool; i++)
        ADD(X->d->pool[i].layout);
    nl = rg_ledge_layouts(X->w, ids, 512);
    for (i = 0; i < nl; i++)
        ADD(ids[i]);
    for (i = 1; i < 512u; i++)
        if (!seen[i] && X->lv->hasBase[i] && X->lv->base[i] != 0 && rg_drawn_group(X->d, (uint16_t)i, true, X->lv->ok) == NULL)
            rest[nr++] = (uint16_t)i;
    for (i = 1; i < nr; i++) {                  /* insertion sort by A.3 name rank: stable, n is small */
        uint16_t v = rest[i];

        for (j = i; j > 0 && rg_name_rank(rest[j - 1]) > rg_name_rank(v); j--)
            rest[j] = rest[j - 1];
        rest[j] = v;
    }
    for (i = 0; i < nr; i++)
        ADD(rest[i]);
#undef ADD
    return n;
}

static void full_free(Full *X)
{
    unsigned i;

    for (i = 0; i < 512; i++)
        free(X->cells[i]);
    free(X->var);
    free(X->varAddr);
    free(X->cuts);
    rg_cut_shared_free(&X->shared);
    if (X->sv != NULL) {
        rg_solved_free(X->sv);
        free(X->sv);
    }
    if (X->lv != NULL) {
        rg_levels_free(X->lv);
        free(X->lv);
    }
    free(X->d);
    free(X);
}

/* find_drawn + canvas + world levels + solve_drawn: everything layout_heights reads */
static RgErr full_prepare(Full *X, RgReliefStats *st)
{
    RgRCtx *ctx;
    RgPrep *preps;
    RgWorldOpts o = rg_world_opts_default();
    clock_t t0 = clock();
    unsigned g;
    RgErr e = RG_OK;

    e = rg_drawn_find(X->w, X->d);
    if (e != RG_OK)
        return e;
    st->msDrawnFind = 1000.0 * (double)(clock() - t0) / CLOCKS_PER_SEC;
    ctx = rg_rctx_new(X->w, X->r, &e);
    if (ctx == NULL)
        return e == RG_OK ? RG_ERR_NOMEM : e;
    preps = (RgPrep *)calloc(X->d->nGroups ? X->d->nGroups : 1u, sizeof(RgPrep));
    if (preps == NULL) {
        rg_rctx_free(ctx);
        return RG_ERR_NOMEM;
    }
    t0 = clock();
    e = rg_world_prep_all(ctx, X->d, preps);
    st->msCanvas = 1000.0 * (double)(clock() - t0) / CLOCKS_PER_SEC;
    t0 = clock();
    if (e == RG_OK)
        e = rg_world_levels(X->w, X->d, preps, &o, X->lv);
    st->msWorld = 1000.0 * (double)(clock() - t0) / CLOCKS_PER_SEC;
    rg_world_prep_free(preps, X->d->nGroups);
    free(preps);
    t0 = clock();
    if (e == RG_OK)
        e = rg_solve_all(ctx, X->w, X->d, X->lv, X->sv);
    st->msSolve = 1000.0 * (double)(clock() - t0) / CLOCKS_PER_SEC;
    rg_rctx_free(ctx);
    st->seeds = X->d->nSeeds;
    st->groups = X->d->nGroups;
    for (g = 0; g < X->d->nGroups; g++) {
        st->groupsOk += X->lv->ok[g] ? 1u : 0u;
        st->excluded += rg_drawn_excluded(&X->d->groups[g]) ? 1u : 0u;
        st->spreadDropped += X->lv->dropped[g] ? 1u : 0u;
    }
    st->seamCellsGivenUp = X->lv->brokenCells;
    st->nearHalfLevel = X->lv->nNear;
    return e;
}

static RgErr full_serialise(Full *X, uint8_t **out, size_t *outSize, RgReliefStats *st)
{
    RgReliefRow rows[512];
    unsigned i, n = 0;
    size_t sz = 0;
    RgErr e;

    for (i = 1; i < 512u; i++)
        if (X->have[i]) {
            rows[n++] = X->rows[i];
            st->cells += X->rows[i].nCells;
        }
    st->rows = n;
    st->variants = X->nVar;
    st->cuts = X->nCuts;
    e = rg_relief_write(rows, n, X->var, X->nVar, X->cuts, X->nCuts, NULL, 0, &sz);
    if (e != RG_OK)
        return e;
    *out = (uint8_t *)malloc(sz);
    if (*out == NULL)
        return RG_ERR_NOMEM;
    e = rg_relief_write(rows, n, X->var, X->nVar, X->cuts, X->nCuts, *out, sz, outSize);
    if (e != RG_OK) {
        free(*out);
        *out = NULL;
    }
    return e;
}

static RgErr full_build(const RgWorld *w, const RgRoles *r, RgProgressFn progress, void *ctx, const volatile int *cancel,
                        uint8_t **out, size_t *outSize, RgReliefStats *st)
{
    Full *X = (Full *)calloc(1, sizeof(*X));
    uint16_t seq[512], ids[512];
    unsigned n, i;
    clock_t t0;
    RgErr e = RG_OK;

    if (X == NULL)
        return RG_ERR_NOMEM;
    X->w = w;
    X->r = r;
    X->st = st;
    X->d = (RgDrawn *)malloc(sizeof(RgDrawn));
    X->lv = (RgLevels *)calloc(1, sizeof(RgLevels));
    X->sv = (RgSolved *)calloc(1, sizeof(RgSolved));
    if (X->d == NULL || X->lv == NULL || X->sv == NULL || !rg_cut_shared_init(&X->shared)) {
        full_free(X);
        return RG_ERR_NOMEM;
    }
    st->ledgeLayouts = rg_ledge_layouts(w, ids, 512);
    e = full_prepare(X, st);
    if (e == RG_OK) {
        n = layout_sequence(X, seq);
        t0 = clock();
        for (i = 0; i < n && e == RG_OK; i++) {
            if (cancel != NULL && *cancel != 0)
                e = RG_ERR_CANCELLED;
            else
                e = export_layout(X, seq[i]);
            if (e == RG_OK && progress != NULL)
                progress(ctx, i + 1u, n);
        }
        st->msExport = 1000.0 * (double)(clock() - t0) / CLOCKS_PER_SEC;
    }
    if (e == RG_OK)
        e = full_serialise(X, out, outSize, st);
    full_free(X);
    return e;
}

RgErr rg_relief_build(const RgWorld *w, const RgRoles *r, RgReliefMode mode, RgProgressFn progress, void *ctx,
                      const volatile int *cancel, uint8_t **out, size_t *outSize, RgReliefStats *st)
{
    RgReliefStats local;
    Ledges *l;
    uint16_t ids[512];
    unsigned total;
    RgErr e;

    assert(w != NULL && out != NULL && outSize != NULL);
    if (st == NULL)
        st = &local;
    memset(st, 0, sizeof(*st));
    *out = NULL;
    *outSize = 0;
    if (mode != RG_RELIEF_LEDGES && mode != RG_RELIEF_FULL)
        return RG_ERR_RELIEF;           /* OFF is not a build */
    {
        const char *why = NULL;

        /* the T1-T9 tables describe Emerald; Kanto has only the ledges pass (SPEC-P34 7.1), which reads none of them */
        if (w->prof->game == GP_EMERALD && !rg_rtables_check(w, &why))
            return RG_ERR_TABLES;
    }
    if (w->prof->game != GP_EMERALD)
        mode = RG_RELIEF_LEDGES;        /* no drawn relief for Kanto: FULL means LEDGES there */
    if (mode == RG_RELIEF_FULL) {
        if (r == NULL || r->data == NULL)
            return RG_ERR_RELIEF;       /* FULL reads the roles */
        return full_build(w, r, progress, ctx, cancel, out, outSize, st);
    }
    total = rg_ledge_layouts(w, ids, 512);
    if (total > 512)
        return RG_ERR_RELIEF;
    l = (Ledges *)calloc(1, sizeof(*l));
    if (l == NULL)
        return RG_ERR_NOMEM;
    memcpy(l->ids, ids, total * sizeof(ids[0]));
    l->n = total;
    st->ledgeLayouts = total;
    e = ledges_run(w, l, progress, ctx, cancel, st);
    if (e == RG_OK)
        e = ledges_serialise(l, out, outSize, st);
    ledges_free(l);
    return e;
}
