/* Modified for 3DGBA (GPLv3), 2026.
 * Original: ZallaxDev/pokeemerald-3Ds-dualscreen @ c330c0a, 3ds_port/scripts/gen_voxel_relief.py (rim_cells, spread,
 * cell_shapes: rel:3002-3682), MIT License - see source/voxel/NOTICE.md.
 * Portions Copyright (c) Dust Zallax, MIT. */
#include "rg_rshape.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "rg_behavior.h"
#include "rg_ledge.h"
#include "rg_rrock.h"

#define P RG_P
#define STEPPX 4.0                   /* STEP (rel:167) */
#define LEVELPX 16.0                 /* LEVEL (rel:166) */
#define TSTEEP (STEPPX * RG_SPREAD)  /* T = STEP * SPREAD = 5.0 */

/* Python min/max on two floats: the first argument unless the second is strictly smaller / larger. */
static double pymin(double a, double b) { return b < a ? b : a; }
static double pymax(double a, double b) { return b > a ? b : a; }

typedef struct Sh {
    const RgShapeIn *in;
    RgCutArt *art;
    const RgLayout *L;
    int W, H;
    const RgLat *h;
    uint8_t *content, *keepOut, *taken;      /* w*h */
    const uint16_t **maskOf;                 /* w*h: the cut record's mask or NULL (upstream `masks`) */
    int32_t *cutGround;                      /* w*h: cut_behind / cut_ground (-1 = absent) */
    RgShapes *out;
    uint8_t *haveFill;                       /* w*h: a fill exists (upstream `have`) */
} Sh;

static size_t cidx(const Sh *S, int x, int y) { return (size_t)y * (size_t)S->W + (size_t)x; }
static bool inside(const Sh *S, int x, int y) { return x >= 0 && y >= 0 && x < S->W && y < S->H; }
static bool in_content(const Sh *S, int x, int y) { return inside(S, x, y) && S->content[cidx(S, x, y)]; }

/* upstream `grid(c)`: the cell's own grid where it has one, else the shared lattice's. */
static void grid_of(const Sh *S, int x, int y, RgGrid *g)
{
    size_t c = cidx(S, x, y);

    if (S->out->has[c])
        *g = S->out->grid[c];
    else
        rg_cell_grid(S->h, x, y, g);
}

static void set_grid(Sh *S, int x, int y, const RgGrid *g)
{
    size_t c = cidx(S, x, y);

    S->out->grid[c] = *g;
    S->out->has[c] = 1;
}

/* `if g != grid(c): grids[c] = g` */
static void store_if_changed(Sh *S, int x, int y, const RgGrid *g)
{
    RgGrid cur;

    grid_of(S, x, y, &cur);
    if (!rg_grid_equal(g, &cur))
        set_grid(S, x, y, g);
}

static double grid_max(const RgGrid *g, int j0, int j1)
{
    double m = g->g[j0][0];
    int i, j;

    for (j = j0; j <= j1; j++)
        for (i = 0; i < RG_SIDE; i++)
            if (g->g[j][i] > m)
                m = g->g[j][i];
    return m;
}

static double grid_min(const RgGrid *g, int j0, int j1)
{
    double m = g->g[j0][0];
    int i, j;

    for (j = j0; j <= j1; j++)
        for (i = 0; i < RG_SIDE; i++)
            if (g->g[j][i] < m)
                m = g->g[j][i];
    return m;
}

void rg_shapes_free(RgShapes *s)
{
    if (s == NULL)
        return;
    free(s->grid);
    free(s->has);
    free(s->fills);
    free(s->hasWall);
    free(s->wallFace);
    free(s->wallBits);
    memset(s, 0, sizeof(*s));
}

/* ---- spread (rel:3092-3146) ---- */

typedef struct Pt { int i, j; } Pt;

static void even_run(RgLat *L, const Pt *pts, unsigned n)
{
    unsigned k;
    double v0, v1, maxd = 0.0;
    int steps;

    assert(n >= 3u);
    v0 = *rg_lat_at(L, pts[0].i, pts[0].j);
    v1 = *rg_lat_at(L, pts[n - 1].i, pts[n - 1].j);
    for (k = 0; k + 1 < n; k++) {
        double d = fabs(*rg_lat_at(L, pts[k + 1].i, pts[k + 1].j) - *rg_lat_at(L, pts[k].i, pts[k].j));

        if (d > maxd)
            maxd = d;
    }
    if (maxd <= STEPPX + 0.01)
        return;
    steps = (int)(n - 1u);
    if (fabs(v1 - v0) / (double)steps > STEPPX * RG_SPREAD + 0.01)
        return;
    for (k = 1; k + 1 < n; k++)
        *rg_lat_at(L, pts[k].i, pts[k].j) = v0 + (v1 - v0) * (double)k / (double)steps;
}

static bool rock_pt(const uint8_t *content, int w, int h, int a, int b)
{
    int cx = a / P, cy = b / P;

    return cx >= 0 && cy >= 0 && cx < w && cy < h && content[(size_t)cy * (size_t)w + (size_t)cx];
}

RgErr rg_spread(const RgLat *h, const uint8_t *content, RgLat *out)
{
    int W = h->w, Hh = h->h, i, j, sign;
    size_t n = (size_t)(W * P + 1) * (size_t)(Hh * P + 1);
    Pt *run;
    unsigned nr;

    assert(h != NULL && content != NULL && out != NULL);
    if (!rg_lat_new(out, W, Hh))
        return RG_ERR_NOMEM;
    memcpy(out->v, h->v, n * sizeof(double));
    run = (Pt *)malloc(((size_t)(W > Hh ? W : Hh) * P + 4u) * sizeof(Pt));
    if (run == NULL) {
        rg_lat_free(out);
        return RG_ERR_NOMEM;
    }
    for (i = 1; i < W * P; i++) {
        nr = 0;
        for (j = 0; j < Hh * P; j++) {
            bool in = (rock_pt(content, W, Hh, i - 1, j) || rock_pt(content, W, Hh, i, j)) && j > 0 && j < Hh * P;
            bool fall = *rg_lat_at(out, i, j + 1) < *rg_lat_at(out, i, j) - 0.01;

            if (in && fall) {
                if (nr == 0) {
                    run[nr].i = i;
                    run[nr++].j = j;
                }
                run[nr].i = i;
                run[nr++].j = j + 1;
                continue;
            }
            if (nr > 2)
                even_run(out, run, nr);
            nr = 0;
        }
        if (nr > 2)
            even_run(out, run, nr);
    }
    for (j = 1; j < Hh * P; j++) {
        for (sign = 1; sign >= -1; sign -= 2) {
            nr = 0;
            for (i = 0; i < W * P; i++) {
                bool in = (rock_pt(content, W, Hh, i, j - 1) || rock_pt(content, W, Hh, i, j)) && i > 0 && i < W * P;
                double d = (*rg_lat_at(out, i + 1, j) - *rg_lat_at(out, i, j)) * (double)sign;

                if (in && d < -0.01) {
                    if (nr == 0) {
                        run[nr].i = i;
                        run[nr++].j = j;
                    }
                    run[nr].i = i + 1;
                    run[nr++].j = j;
                    continue;
                }
                if (nr > 2)
                    even_run(out, run, nr);
                nr = 0;
            }
            if (nr > 2)
                even_run(out, run, nr);
        }
    }
    free(run);
    return RG_OK;
}

/* ---- the layout's content / keep-out / cut bookkeeping ---- */

static bool sh_alloc(Sh *S, const RgShapeIn *in, RgShapes *out)
{
    size_t n;

    memset(S, 0, sizeof(*S));
    memset(out, 0, sizeof(*out));
    S->in = in;
    S->art = in->art;
    S->L = in->L;
    S->W = (int)in->L->w;
    S->H = (int)in->L->h;
    S->h = in->h;
    S->out = out;
    n = (size_t)S->W * (size_t)S->H;
    out->w = S->W;
    out->h = S->H;
    out->grid = (RgGrid *)calloc(n, sizeof(RgGrid));
    out->has = (uint8_t *)calloc(n, 1);
    out->hasWall = (uint8_t *)calloc(n, 1);
    out->wallFace = (uint16_t *)calloc(n, sizeof(uint16_t));
    out->wallBits = (uint8_t *)calloc(n, 1);
    S->content = (uint8_t *)calloc(n, 1);
    S->keepOut = (uint8_t *)calloc(n, 1);
    S->taken = (uint8_t *)calloc(n, 1);
    S->haveFill = (uint8_t *)calloc(n, 1);
    S->maskOf = (const uint16_t **)calloc(n, sizeof(uint16_t *));
    S->cutGround = (int32_t *)malloc(n * sizeof(int32_t));
    if (out->grid == NULL || out->has == NULL || out->hasWall == NULL || out->wallFace == NULL ||
        out->wallBits == NULL || S->content == NULL || S->keepOut == NULL || S->taken == NULL ||
        S->haveFill == NULL || S->maskOf == NULL || S->cutGround == NULL)
        return false;
    memset(S->cutGround, 0xFF, n * sizeof(int32_t));
    return true;
}

static void sh_free(Sh *S)
{
    free(S->content);
    free(S->keepOut);
    free(S->taken);
    free(S->haveFill);
    free(S->maskOf);
    free(S->cutGround);
}

/* content = rock cells in the layout | footprint cells that are not soil (rel:3022-3024, 3169-3172). */
static void sh_content(Sh *S)
{
    const RgSolvedGroup *g = S->in->grp;
    int x, y;

    for (y = 0; y < S->H; y++) {
        for (x = 0; x < S->W; x++) {
            size_t k = (size_t)(S->in->oy + y) * (size_t)g->cw + (size_t)(S->in->ox + x);

            S->content[cidx(S, x, y)] = (uint8_t)(g->tile[k] != 0 || (g->cell[k] != RG_LV_NONE && !g->soil[k]));
        }
    }
}

static void sh_cuts(Sh *S)
{
    unsigned i;

    for (i = 0; i < S->in->nCut; i++) {
        const RgCutRec *c = &S->in->cut[i];
        size_t k = cidx(S, c->x, c->y);

        S->taken[k] = 1;
        S->maskOf[k] = c->mask;
        S->cutGround[k] = c->behind;
    }
}

static bool is_tile(const Sh *S, int x, int y, unsigned kind)
{
    const RgSolvedGroup *g = S->in->grp;
    size_t k = (size_t)(S->in->oy + y) * (size_t)g->cw + (size_t)(S->in->ox + x);

    return g->tile[k] == kind;
}

static bool is_rock(const Sh *S, int x, int y)
{
    const RgSolvedGroup *g = S->in->grp;

    return g->tile[(size_t)(S->in->oy + y) * (size_t)g->cw + (size_t)(S->in->ox + x)] != 0;
}

/* ---- rim_cells (rel:3002-3086) ---- */

typedef struct Fills {
    uint8_t *on;
    double *foot;
    int32_t *ground;
} Fills;

/* level(): a tile loses only its back (rel:3036-3049). */
static void rim_level(Sh *S, const RgLat *rockH, int x, int y)
{
    RgGrid g, orig;
    bool corner = is_tile(S, x, y, RG_RT_CORNER);
    int i, j;

    rg_cell_grid(rockH, x, y, &g);
    for (j = corner ? P - 1 : 0; j >= 0; j--)
        for (i = 0; i <= P; i++)
            g.g[j][i] = pymax(g.g[j][i], g.g[j + 1][i]);
    rg_cell_grid(S->h, x, y, &orig);
    if (!rg_grid_equal(&g, &orig))
        set_grid(S, x, y, &g);
}

static RgErr rim_collect(Sh *S, const RgLat *rockH, Fills *F)
{
    int x, y;

    for (y = 0; y < S->H; y++) {
        for (x = 0; x < S->W; x++) {
            RgGrid g, behind, below;
            double foot, top, rowSpan;
            int32_t ground;
            int k, n;

            if (!S->content[cidx(S, x, y)] || y < 1 || S->content[cidx(S, x, y - 1)])
                continue;
            rg_cell_grid(rockH, x, y, &g);
            rg_cell_grid(S->h, x, y - 1, &behind);
            foot = grid_max(&behind, 0, P);
            if (grid_min(&behind, 0, P) < foot - 0.5)
                continue;               /* not flat ground behind */
            if (y + 1 < S->H)
                rg_cell_grid(rockH, x, y + 1, &below);
            else
                below = g;
            top = pymax(grid_max(&g, 0, P), grid_max(&below, 0, P));
            rowSpan = grid_max(&g, 1, P - 1) - grid_min(&g, 1, P - 1);
            if (S->in->wrap && rowSpan <= 1.0)
                top = grid_max(&g, 1, P - 1);
            if (top <= foot + 0.5)
                continue;
            rim_level(S, rockH, x, y);
            if (!S->out->has[cidx(S, x, y)])
                continue;
            ground = S->cutGround[cidx(S, x, y)];
            if (ground <= 0)            /* `cut_behind.get(...) or plainest(...)`: None and metatile 0 are both falsy */
                ground = rg_cut_plain_ground(S->art, S->content, x, y - 1, 'S', false, 0);
            if (ground == -2)
                return RG_ERR_NOMEM;
            n = (int)ceil((top - foot) / LEVELPX);
            for (k = 0; k < n; k++) {
                int cy = y + k;
                size_t c;
                RgGrid pg;

                if (cy >= S->H)
                    break;
                if (S->in->wrap && k) {
                    rg_cell_grid(rockH, x, cy - 1, &pg);
                    if (grid_min(&pg, 1, P) > foot + 0.5)
                        break;          /* a raised top stands behind it */
                }
                c = cidx(S, x, cy);
                if (S->taken[c])
                    continue;           /* a cut tile lies flat at its own foot */
                if (!F->on[c] || foot < F->foot[c]) {
                    F->on[c] = 1;
                    F->foot[c] = foot;
                    F->ground[c] = ground;
                }
            }
        }
    }
    return RG_OK;
}

static RgErr fills_push(RgShapes *s, int x, int y, double foot, int32_t ground, uint8_t goesOn)
{
    if (s->nFills == s->capFills) {
        unsigned cap = s->capFills ? s->capFills * 2u : 64u;
        RgFill *nf = (RgFill *)realloc(s->fills, cap * sizeof(RgFill));

        if (nf == NULL)
            return RG_ERR_NOMEM;
        s->fills = nf;
        s->capFills = cap;
    }
    s->fills[s->nFills].x = (uint8_t)x;
    s->fills[s->nFills].y = (uint8_t)y;
    s->fills[s->nFills].foot = foot;
    s->fills[s->nFills].ground = ground;
    s->fills[s->nFills++].goesOn = goesOn;
    return RG_OK;
}

static RgErr rim_run(Sh *S, const RgLat *base)
{
    const RgLat *rockH = base != NULL ? base : S->h;
    size_t n = (size_t)S->W * (size_t)S->H;
    Fills F;
    RgErr e;
    int x, y;

    F.on = (uint8_t *)calloc(n, 1);
    F.foot = (double *)calloc(n, sizeof(double));
    F.ground = (int32_t *)calloc(n, sizeof(int32_t));
    if (F.on == NULL || F.foot == NULL || F.ground == NULL) {
        free(F.on);
        free(F.foot);
        free(F.ground);
        return RG_ERR_NOMEM;
    }
    e = rim_collect(S, rockH, &F);
    /* sorted(fills.items()): by (x, y) */
    for (x = 0; x < S->W && e == RG_OK; x++) {
        for (y = 0; y < S->H && e == RG_OK; y++) {
            size_t c = cidx(S, x, y);

            if (F.on[c]) {
                e = fills_push(S->out, x, y, F.foot[c], F.ground[c], 0);
                S->haveFill[c] = 1;
            }
        }
    }
    free(F.on);
    free(F.foot);
    free(F.ground);
    return e;
}

RgErr rg_rim_cells(const RgShapeIn *in, const RgLat *base, RgShapes *out)
{
    Sh S;
    RgErr e = RG_OK;

    assert(in != NULL && out != NULL);
    if (!sh_alloc(&S, in, out)) {
        sh_free(&S);
        rg_shapes_free(out);
        return RG_ERR_NOMEM;
    }
    if (in->grp != NULL) {
        sh_content(&S);
        sh_cuts(&S);
        e = rim_run(&S, base);
    }
    sh_free(&S);
    if (e != RG_OK)
        rg_shapes_free(out);
    return e;
}

/* ---- cell_shapes (rel:3148-3682) ---- */

static bool clear_quad(const uint16_t *mask, int a, int b)
{
    int i, j;

    for (j = b * 4; j < b * 4 + 4; j++)
        for (i = a * 4; i < a * 4 + 4; i++)
            if (!((mask[j] >> i) & 1))
                return false;
    return true;
}

static bool rock_quad(const uint16_t *mask, int a, int b)
{
    int i, j;

    for (j = b * 4; j < b * 4 + 4; j++)
        for (i = a * 4; i < a * 4 + 4; i++)
            if ((mask[j] >> i) & 1)
                return false;
    return true;
}

/* drawn(c, a, b) */
static bool quad_drawn(const Sh *S, int x, int y, int a, int b)
{
    const uint16_t *m = S->maskOf[cidx(S, x, y)];

    return m == NULL || !clear_quad(m, a, b);
}

/* seam(c, i, j): a point on the map's own edge */
static bool seam(const Sh *S, int x, int y, int i, int j)
{
    int px = x * P + i, py = y * P + j;

    return px == 0 || px == S->W * P || py == 0 || py == S->H * P;
}

/* keep_out: ledges and berry soil are not mountains (rel:3180-3185) */
static RgErr sh_keep_out(Sh *S)
{
    RgLedgeSet ls;
    int x, y;

    if (!rg_ledge_cells(S->L, true, &ls))
        return RG_ERR_NOMEM;
    for (y = 0; y < S->H; y++) {
        for (x = 0; x < S->W; x++) {
            size_t c = cidx(S, x, y);

            if (ls.at[c] >= 0 || rg_is_dirt(rg_cut_meta(S->art, x, y)) || rg_behaviour(S->L, x, y) == RG_MB_BERRY_TREE_SOIL)
                S->keepOut[c] = 1;
        }
    }
    rg_ledge_set_free(&ls);
    return RG_OK;
}

/* "the rock to its edge in its own shape, beside lower ground" (rel:3190-3205) */
static void ph_rock_edge(Sh *S)
{
    int x, y, d, j;

    for (y = 0; y < S->H; y++) {
        for (x = 0; x < S->W; x++) {
            RgGrid g, cur;

            if (!S->content[cidx(S, x, y)])
                continue;
            grid_of(S, x, y, &g);
            for (d = 0; d < 2; d++) {
                int dx = d == 0 ? -1 : 1, e = d == 0 ? 0 : P, a = d == 0 ? 1 : P - 1, b = d == 0 ? 2 : P - 2;

                if (!inside(S, x + dx, y) || S->content[cidx(S, x + dx, y)])
                    continue;
                for (j = 0; j <= P; j++) {
                    double v = pymin(g.g[j][a], 2 * g.g[j][a] - g.g[j][b]);

                    if (v >= g.g[j][e] + STEPPX)      /* a step, not a pebble's swell */
                        g.g[j][e] = v;
                }
            }
            grid_of(S, x, y, &cur);
            if (!rg_grid_equal(&g, &cur))
                set_grid(S, x, y, &g);
        }
    }
}

/* a cut tile's clear background is no rock (rel:3206-3230): in the cut list's order */
static void ph_cut_clear(Sh *S)
{
    static const int8_t nb[3][2] = {{-1, 0}, {1, 0}, {0, 1}};
    unsigned ci;
    int q, k;

    for (ci = 0; ci < S->in->nCut; ci++) {
        const RgCutRec *cr = &S->in->cut[ci];
        const uint16_t *mask = cr->mask;
        int x = cr->x, y = cr->y;
        RgGrid g;

        grid_of(S, x, y, &g);
        for (q = 0; q < 3; q++) {
            int dx = nb[q][0], dy = nb[q][1], nx = x + dx, ny = y + dy;
            RgGrid o;

            if (!inside(S, nx, ny) || S->content[cidx(S, nx, ny)])
                continue;
            grid_of(S, nx, ny, &o);
            for (k = 0; k <= P; k++) {
                int i, j, oi, oj, a, b;
                bool all = true;

                if (dx) {
                    i = dx < 0 ? 0 : P;
                    j = k;
                    oi = dx < 0 ? P : 0;
                    oj = k;
                } else {
                    i = k;
                    j = P;
                    oi = k;
                    oj = 0;
                }
                for (a = i - 1; a <= i; a++)
                    for (b = j - 1; b <= j; b++)
                        if (a >= 0 && a < P && b >= 0 && b < P && !clear_quad(mask, a, b))
                            all = false;
                if (all)
                    g.g[j][i] = pymin(g.g[j][i], o.g[oj][oi]);
            }
        }
        store_if_changed(S, x, y, &g);
    }
}

/* "the ground level to its edge" (rel:3231-3258) */
static void ph_ground_edge(Sh *S)
{
    static const int8_t nb[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
    int x, y, q, k, i, j;

    for (y = 0; y < S->H; y++) {
        for (x = 0; x < S->W; x++) {
            RgGrid g;
            double mn, mx;

            if (S->content[cidx(S, x, y)])
                continue;
            grid_of(S, x, y, &g);
            mn = mx = g.g[1][1];
            for (j = 1; j < P; j++)
                for (i = 1; i < P; i++) {
                    mx = pymax(mx, g.g[j][i]);
                    mn = pymin(mn, g.g[j][i]);
                }
            if (mx - mn > 0.5)
                continue;               /* not level ground: as it is */
            for (q = 0; q < 4; q++) {
                int dx = nb[q][0], dy = nb[q][1];

                if (!in_content(S, x + dx, y + dy))
                    continue;
                for (k = 0; k <= P; k++) {
                    if (dx == -1)
                        g.g[k][0] = g.g[k][1];
                    else if (dx == 1)
                        g.g[k][P] = g.g[k][P - 1];
                    else if (dy == -1)
                        g.g[0][k] = g.g[1][k];
                    else
                        g.g[P][k] = g.g[P - 1][k];
                }
            }
            store_if_changed(S, x, y, &g);
        }
    }
}

/* follow(c) (rel:3287-3315): once per cell */
static void ph_follow(Sh *S, int x, int y)
{
    RgGrid g;
    int i, j, q;
    static const int8_t ep[2][2] = {{0, 1}, {P, P - 1}};

#define FAR(a, b) (fabs((a) - (b)) > LEVELPX + 0.01)
    grid_of(S, x, y, &g);
    for (j = 0; j <= P; j++) {
        for (q = 0; q < 2; q++) {
            int e = ep[q][0], n = ep[q][1];

            if (!seam(S, x, y, e, j) && quad_drawn(S, x, y, e < n ? e : n, j < P - 1 ? j : P - 1) && FAR(g.g[j][e], g.g[j][n]))
                g.g[j][e] = pymin(pymax(g.g[j][e], g.g[j][n] - TSTEEP), g.g[j][n] + TSTEEP);
        }
    }
    for (i = 0; i <= P; i++) {
        int a = i < P - 1 ? i : P - 1;

        if (!seam(S, x, y, i, 0) && quad_drawn(S, x, y, a, 0) && FAR(g.g[0][i], g.g[1][i]))
            g.g[0][i] = pymin(pymax(g.g[0][i], g.g[1][i]), g.g[1][i] + TSTEEP);
        if (!seam(S, x, y, i, P) && quad_drawn(S, x, y, a, P - 1) && FAR(g.g[P][i], g.g[P - 1][i]))
            g.g[P][i] = pymin(pymax(g.g[P][i], g.g[P - 1][i] - TSTEEP), g.g[P - 1][i]);
    }
#undef FAR
    store_if_changed(S, x, y, &g);
}

/* settle(c) (rel:3317-3347): the cell's drawn quads in true shapes, raising only. True when the grid changed. */
static bool ph_settle(Sh *S, int x, int y)
{
    RgGrid g, cur;
    bool moved = true;
    unsigned guard;
    int a, b, q;

    grid_of(S, x, y, &g);
    for (guard = 0; moved && guard < 100000u; guard++) {
        moved = false;
        for (b = 0; b < P; b++) {
            for (a = 0; a < P; a++) {
                if (!quad_drawn(S, x, y, a, b))
                    continue;
                for (q = 0; q < 4; q++) {
                    int p0x, p0y, p1x, p1y;
                    double h0, h1;
                    bool u = q < 2;

                    p0x = a + (q == 3 ? 1 : 0);
                    p0y = b + (q == 1 ? 1 : 0);
                    p1x = (q < 2) ? a + 1 : p0x;
                    p1y = (q < 2) ? p0y : b + 1;
                    h0 = g.g[p0y][p0x];
                    h1 = g.g[p1y][p1x];
                    if (u) {
                        if (h1 < h0 - TSTEEP - 0.01 && !seam(S, x, y, p1x, p1y)) {
                            g.g[p1y][p1x] = h0 - TSTEEP;
                            moved = true;
                        } else if (h0 < h1 - TSTEEP - 0.01 && !seam(S, x, y, p0x, p0y)) {
                            g.g[p0y][p0x] = h1 - TSTEEP;
                            moved = true;
                        }
                    } else {
                        if (h1 > h0 + 0.01 && !seam(S, x, y, p0x, p0y)) {
                            g.g[p0y][p0x] = h1;          /* rising south: its back */
                            moved = true;
                        } else if (h1 < h0 - TSTEEP - 0.01 && !seam(S, x, y, p1x, p1y)) {
                            g.g[p1y][p1x] = h0 - TSTEEP;  /* falling faster than a face */
                            moved = true;
                        }
                    }
                }
            }
        }
    }
    grid_of(S, x, y, &cur);
    if (!rg_grid_equal(&g, &cur)) {
        set_grid(S, x, y, &g);
        return true;
    }
    return false;
}

/* "a top's back is the edge it shares with the rock north of it" (rel:3352-3385) */
static void ph_back(Sh *S)
{
    int x, y, i;

    for (y = 0; y < S->H; y++) {
        for (x = 0; x < S->W; x++) {
            RgGrid g, sg;
            double topMax, innerMax, innerMin, maxRise = -1e300;
            double rise[RG_SIDE];
            unsigned near = 0;
            bool terrace;

            if (!S->content[cidx(S, x, y)] || !in_content(S, x, y + 1) || S->keepOut[cidx(S, x, y)] ||
                S->keepOut[cidx(S, x, y + 1)])
                continue;
            grid_of(S, x, y + 1, &sg);
            grid_of(S, x, y, &g);
            innerMax = grid_max(&g, 1, P - 1);
            innerMin = grid_min(&g, 1, P - 1);
            terrace = S->in->wrap && innerMax - innerMin <= 0.5 && !is_rock(S, x, y) && !is_rock(S, x, y + 1);
            topMax = sg.g[0][0];
            for (i = 0; i < RG_SIDE; i++)
                topMax = pymax(topMax, sg.g[0][i]);
            for (i = 0; i < RG_SIDE; i++)
                near += (unsigned)(sg.g[0][i] >= topMax - 0.5);
            if (near < 3u && !terrace)
                continue;
            for (i = 0; i < RG_SIDE; i++) {
                rise[i] = g.g[P][i] - g.g[P - 1][i];
                maxRise = i == 0 ? rise[0] : pymax(maxRise, rise[i]);
            }
            if (maxRise <= STEPPX + 0.01)
                continue;               /* a slope's step, not a back */
            for (i = 0; i <= P; i++)
                if (rise[i] > 0.01 && !seam(S, x, y, i, P))
                    g.g[P][i] = g.g[P - 1][i];
            store_if_changed(S, x, y, &g);
        }
    }
}

/* the 64-round loop of settle + shared points (rel:3386-3406) */
static void ph_settle_shared(Sh *S)
{
    unsigned round;
    int x, y, j;

    for (round = 0; round < 64u; round++) {
        bool changed = false;

        for (y = 0; y < S->H; y++)
            for (x = 0; x < S->W; x++)
                if (S->content[cidx(S, x, y)])
                    changed |= ph_settle(S, x, y);
        /* shared[(X, cy, j)]: the points (c, P, j) and (c east, 0, j) of two content cells side by side */
        for (y = 0; y < S->H; y++) {
            for (x = 0; x + 1 < S->W; x++) {
                if (!S->content[cidx(S, x, y)] || !S->content[cidx(S, x + 1, y)])
                    continue;
                for (j = 0; j <= P; j++) {
                    RgGrid a, b;
                    double top, lo;

                    grid_of(S, x, y, &a);
                    grid_of(S, x + 1, y, &b);
                    top = pymax(a.g[j][P], b.g[j][0]);
                    lo = pymin(a.g[j][P], b.g[j][0]);
                    if (top - lo > STEPPX + 0.01)
                        continue;       /* a silhouette: each its own */
                    if (a.g[j][P] < top) {
                        a.g[j][P] = top;
                        set_grid(S, x, y, &a);
                        changed = true;
                    }
                    grid_of(S, x + 1, y, &b);
                    if (b.g[j][0] < top) {
                        b.g[j][0] = top;
                        set_grid(S, x + 1, y, &b);
                        changed = true;
                    }
                }
            }
        }
        if (!changed)
            break;
    }
}

/* ---- the flank (rel:3407-3470) ---- */

typedef struct Flank {
    Sh *S;
    int pw, ph;                      /* pixels: W*16, H*16 */
    uint16_t (*bgStore)[16];         /* w*h */
    uint8_t *bgState;                /* 0 unknown, 1 mask in bgStore, 2 None */
    uint8_t *plain;                  /* 0 unknown, 1 false, 2 true */
    uint8_t *beside;                 /* pw*ph: 0 unknown, 1 false, 2 true */
} Flank;

static const uint16_t *fl_bg(Flank *F, int cx, int cy)
{
    Sh *S = F->S;
    size_t c = cidx(S, cx, cy);

    if (F->bgState[c] == 0) {
        if (S->maskOf[c] != NULL) {
            memcpy(F->bgStore[c], S->maskOf[c], sizeof(F->bgStore[c]));
            F->bgState[c] = 1;
        } else {
            F->bgState[c] = rg_cut_mask(S->art, cx, cy, S->content, F->bgStore[c]) ? 1 : 2;
        }
    }
    return F->bgState[c] == 1 ? F->bgStore[c] : NULL;
}

static bool fl_is_ground(Flank *F, int X, int Y)
{
    Sh *S = F->S;
    int cx, cy;
    size_t c;

    if (X < 0 || Y < 0)
        return false;
    cx = X / 16;
    cy = Y / 16;
    if (!inside(S, cx, cy))
        return false;
    c = cidx(S, cx, cy);
    if (S->content[c]) {
        const uint16_t *m = fl_bg(F, cx, cy);

        return m != NULL && ((m[Y % 16] >> (X % 16)) & 1);
    }
    if (F->plain[c] == 0) {
        RgGrid g;

        grid_of(S, cx, cy, &g);
        F->plain[c] = grid_max(&g, 0, P) <= 0.5 ? 2 : 1;
    }
    return F->plain[c] == 2;
}

static bool fl_is_light(Flank *F, int X, int Y)
{
    Sh *S = F->S;
    const uint32_t *img;

    if (X < 0 || Y < 0 || !inside(S, X / 16, Y / 16))
        return false;
    img = rg_cut_image(S->art, rg_cut_meta(S->art, X / 16, Y / 16));
    return img != NULL && rg_rock_light_rgb(img[(Y % 16) * 16 + (X % 16)]);
}

static bool fl_beside(Flank *F, int X, int Y)
{
    static const int8_t off[5] = {0, -2, 2, -5, 5};
    bool v = true;
    int k;
    uint8_t *memo = NULL;

    if (X >= 0 && Y >= 0 && X < F->pw && Y < F->ph) {
        memo = &F->beside[(size_t)Y * (size_t)F->pw + (size_t)X];
        if (*memo)
            return *memo == 2;
    }
    for (k = 0; k < 5 && v; k++)
        v = fl_is_ground(F, X, Y + off[k]);
    if (memo != NULL)
        *memo = v ? 2 : 1;
    return v;
}

/* _crossed(X, Y): how much of the flank on row Y is behind corner X, 0..1; false where the row has none. */
static bool fl_crossed(Flank *F, int X, int Y, double *out)
{
    bool have = false;
    double best = 0.0;
    int sign;

    for (sign = -1; sign <= 1; sign += 2) {
        int xg = 0, step, x0, width, far;
        bool found = false;
        double frac;

        for (step = 0; step <= RG_FLANK; step++) {
            int x = sign < 0 ? X - 1 - step : X + step;

            if (x < 0 || x >= F->pw)
                break;
            if (fl_beside(F, x, Y)) {
                xg = x;
                found = true;
                break;
            }
        }
        if (!found)
            continue;
        x0 = xg + (sign < 0 ? 1 : -1);
        width = 0;
        while (width < RG_FLANK && x0 + sign * width >= 0 && x0 + sign * width < F->pw &&
               !fl_is_ground(F, x0 + sign * width, Y) && !fl_is_light(F, x0 + sign * width, Y))
            width++;
        if (width < 1)
            width = 1;
        far = sign < 0 ? X - x0 : x0 + 1 - X;
        frac = pymin(1.0, pymax(0.0, (double)far / (double)width));
        best = !have ? frac : pymin(best, frac);
        have = true;
    }
    *out = best;
    return have;
}

static RgErr ph_flank(Sh *S)
{
    Flank F;
    size_t n = (size_t)S->W * (size_t)S->H;
    double *ff = (double *)malloc(n * 25u * sizeof(double));
    uint8_t *ffHas = (uint8_t *)calloc(n * 25u, 1);
    int x, y, i, j, r;

    memset(&F, 0, sizeof(F));
    F.S = S;
    F.pw = S->W * 16;
    F.ph = S->H * 16;
    F.bgStore = (uint16_t(*)[16])calloc(n, sizeof(*F.bgStore));
    F.bgState = (uint8_t *)calloc(n, 1);
    F.plain = (uint8_t *)calloc(n, 1);
    F.beside = (uint8_t *)calloc((size_t)F.pw * (size_t)F.ph, 1);
    if (ff == NULL || ffHas == NULL || F.bgStore == NULL || F.bgState == NULL || F.plain == NULL || F.beside == NULL) {
        free(ff);
        free(ffHas);
        free(F.bgStore);
        free(F.bgState);
        free(F.plain);
        free(F.beside);
        return RG_ERR_NOMEM;
    }
    for (y = 0; y < S->H; y++) {
        for (x = 0; x < S->W; x++) {
            if (!S->content[cidx(S, x, y)])
                continue;
            for (j = 0; j <= P; j++) {
                for (i = 0; i <= P; i++) {
                    int X = x * 16 + i * RG_CUT_PIXELS, Y = y * 16 + j * RG_CUT_PIXELS, cnt = 0, nd = 0;
                    double v[4], sum = 0.0;
                    bool ok[4];

                    if (seam(S, x, y, i, j))
                        continue;
                    for (r = 0; r < 4; r++) {
                        ok[r] = fl_crossed(&F, X, Y - 2 + r, &v[r]);
                        cnt += ok[r];
                    }
                    if (cnt < 3)
                        continue;
                    for (r = 1; r <= 2; r++)
                        if (ok[r]) {
                            sum += v[r];
                            nd++;
                        }
                    if (nd) {
                        ff[cidx(S, x, y) * 25u + (size_t)(j * RG_SIDE + i)] = sum / (double)nd;
                        ffHas[cidx(S, x, y) * 25u + (size_t)(j * RG_SIDE + i)] = 1;
                    }
                }
            }
        }
    }
    for (y = 0; y < S->H; y++) {
        for (x = 0; x < S->W; x++) {
            RgGrid g;

            if (!S->content[cidx(S, x, y)])
                continue;
            grid_of(S, x, y, &g);
            for (j = 0; j <= P; j++) {
                for (i = 0; i <= P; i++) {
                    size_t k = cidx(S, x, y) * 25u + (size_t)(j * RG_SIDE + i);

                    if (ffHas[k] && ff[k] < 1.0 && g.g[j][i] > 0)
                        g.g[j][i] = g.g[j][i] * ff[k];
                }
            }
            store_if_changed(S, x, y, &g);
        }
    }
    free(ff);
    free(ffHas);
    free(F.bgStore);
    free(F.bgState);
    free(F.plain);
    free(F.beside);
    return RG_OK;
}

/* a cut tile's cleared points stand as the rock south of them (rel:3480-3535) */
static void ph_cut_points(Sh *S)
{
    unsigned ci;

    for (ci = 0; ci < S->in->nCut; ci++) {
        const RgCutRec *cr = &S->in->cut[ci];
        const uint16_t *mask = cr->mask;
        int x = cr->x, y = cr->y, i, j, a, b, d, k;
        bool whole[RG_SIDE][RG_SIDE], bare[RG_SIDE][RG_SIDE];
        RgGrid g0, g;
        double lo, hi;

        if (!S->content[cidx(S, x, y)] || S->keepOut[cidx(S, x, y)])
            continue;
        grid_of(S, x, y, &g0);
        g = g0;
        for (j = 0; j <= P; j++) {
            for (i = 0; i <= P; i++) {
                bool w = true, e = true;

                for (a = i - 1; a <= i; a++)
                    for (b = j - 1; b <= j; b++)
                        if (a >= 0 && a < P && b >= 0 && b < P) {
                            w = w && rock_quad(mask, a, b);
                            e = e && clear_quad(mask, a, b);
                        }
                whole[j][i] = w;
                bare[j][i] = e;
            }
        }
        lo = grid_min(&g0, 0, P);
        hi = grid_max(&g0, 0, P);
        if (hi - lo < LEVELPX - 0.5)
            continue;                   /* a ledge's lip, not a mountain's rock */
        for (j = P - 1; j >= 0; j--) {
            for (i = 0; i <= P; i++) {
                bool have = false;
                double v = 0.0;

                if (whole[j][i] || seam(S, x, y, i, j))
                    continue;
                if ((i == 0 && in_content(S, x - 1, y)) || (i == P && in_content(S, x + 1, y)))
                    continue;
                if (!bare[j + 1][i]) {
                    v = g.g[j + 1][i];
                    have = true;
                } else {
                    bool haveBest = false;
                    int bestD = 0;
                    double bestV = 0.0;

                    for (d = -1; d <= 1; d += 2) {
                        int k1 = -1, k2 = -1;

                        for (k = i + d; d < 0 ? k >= 0 : k <= P; k += d) {
                            if (!whole[j][k])
                                continue;
                            if (k1 < 0)
                                k1 = k;
                            else {
                                k2 = k;
                                break;
                            }
                        }
                        if (k1 < 0)
                            continue;
                        {
                            double slope = 0.0;
                            int ad = k1 > i ? k1 - i : i - k1;

                            if (k2 >= 0)
                                slope = pymax(-TSTEEP, pymin(TSTEEP, (g0.g[j][k1] - g0.g[j][k2]) / (double)(k1 - k2)));
                            if (!haveBest || ad < bestD) {
                                bestD = ad;
                                bestV = g0.g[j][k1] + slope * (double)(i - k1);
                                haveBest = true;
                            }
                        }
                    }
                    if (haveBest) {
                        v = bestV;
                        have = true;
                    }
                }
                if (have)
                    g.g[j][i] = pymax(lo, pymin(hi, v));
            }
        }
        store_if_changed(S, x, y, &g);
    }
}

/* ---- the ground behind runs on (rel:3536-3600) ---- */

static double grid_all_min(const RgGrid *g) { return grid_min(g, 0, P); }
static double grid_all_max(const RgGrid *g) { return grid_max(g, 0, P); }

static bool near_pick(const Sh *S, int nx, int ny, int *bx, int *by)
{
    int a, b, bestD = 1000;
    bool have = false;

    for (a = -3; a <= 3; a++) {
        for (b = -3; b <= 3; b++) {
            int px = nx + a, py = ny + b, dist = abs(a) + abs(b);

            if (!inside(S, px, py) || S->content[cidx(S, px, py)])
                continue;
            if (!have || dist < bestD || (dist == bestD && (px < *bx || (px == *bx && py < *by)))) {
                have = true;
                bestD = dist;
                *bx = px;
                *by = py;
            }
        }
    }
    return have;
}

static RgErr fill_cell(Sh *S, int x, int y)
{
    RgGrid g, o;
    int nx = x, ny = y - 1, bx = 0, by = 0, k, nk;
    double foot, top;
    int32_t ground, behindN = -1;
    bool level, goesOn;
    int32_t ng;

    if (!in_content(S, nx, ny) || S->taken[cidx(S, x, y)])
        return RG_OK;
    grid_of(S, x, y, &g);
    grid_of(S, nx, ny, &o);
    foot = grid_all_min(&o);
    if (S->in->wrap && !in_content(S, nx, ny - 1))
        foot = grid_min(&o, 1, P);
    if (grid_max(&g, 0, 0) <= foot + 0.5)
        return RG_OK;
    if (!near_pick(S, nx, ny, &bx, &by))
        return RG_OK;
    ng = S->cutGround[cidx(S, nx, ny)];
    ground = ng;
    if (ground < 0)
        ground = rg_cut_plain_ground(S->art, S->content, bx, by, 0, false, 0);
    if (ground == -2)
        return RG_ERR_NOMEM;
    top = grid_all_max(&g);
    level = grid_all_max(&g) - grid_all_min(&g) <= 1.0 && !S->keepOut[cidx(S, x, y)] && !S->keepOut[cidx(S, nx, ny)];
    goesOn = false;
    if (level) {
        bool flat = true;
        int i, j;

        for (j = 0; j <= P; j++)
            for (i = 0; i <= P; i++)
                if (fabs(o.g[j][i] - o.g[P][i]) > 1.0)
                    flat = false;
        goesOn = flat || (S->in->wrap && grid_all_max(&o) <= grid_all_min(&g) - LEVELPX / 2);
        behindN = ng;
        if (behindN >= 0 && !rg_cut_plain_tile(S->art, (uint16_t)behindN))
            behindN = -1;
    }
    nk = (int)ceil((top - foot) / LEVELPX);
    for (k = 0; k < nk; k++) {
        int fx = x, fy = y + k;
        size_t fc;
        RgGrid fg;
        RgErr e;

        if (!inside(S, fx, fy))
            continue;
        fc = cidx(S, fx, fy);
        if (S->taken[fc] || S->haveFill[fc])
            continue;
        grid_of(S, fx, fy, &fg);
        if (grid_all_min(&fg) < foot - 0.5)
            break;
        if (k == 0 && goesOn) {
            int32_t under = (ng >= 0 && rg_cut_plain_tile(S->art, (uint16_t)ng)) ? ng : -1;

            e = fills_push(S->out, fx, fy, foot, under, RG_GOES_ON);
        } else if (behindN >= 0) {
            e = fills_push(S->out, fx, fy, foot, behindN, 0);
        } else {
            e = fills_push(S->out, fx, fy, foot, (int32_t)rg_cut_meta(S->art, fx, fy), 0);
        }
        if (e != RG_OK)
            return e;
        S->haveFill[fc] = 1;
    }
    (void)ground;   /* upstream computes it only for plain_ground's shared cache */
    return RG_OK;
}

/* ---- the cliffs (rel:3601-3682) ---- */

static uint16_t pick_face(const Sh *S, unsigned *ties)
{
    const RgSolvedGroup *g = S->in->grp;
    uint16_t metas[64];
    unsigned counts[64], n = 0, i, best = 0;
    int x, y;

    *ties = 0;
    for (y = 0; y < S->H; y++) {
        for (x = 0; x < S->W; x++) {
            size_t k = (size_t)(S->in->oy + y) * (size_t)g->cw + (size_t)(S->in->ox + x);
            uint16_t m;

            if (g->tile[k] != RG_RT_FACE)
                continue;
            m = rg_cut_meta(S->art, x, y);
            for (i = 0; i < n && metas[i] != m; i++)
                ;
            if (i == n) {
                if (n == 64u)
                    continue;       /* more distinct faces than any drawn layout has */
                metas[n] = m;
                counts[n++] = 0;
            }
            counts[i]++;
        }
    }
    if (n == 0)
        return RG_NO_FACE;
    for (i = 1; i < n; i++)
        if (counts[i] > counts[best])
            best = i;
    for (i = 0; i < n; i++)
        if (i != best && counts[i] == counts[best])
            (*ties)++;
    return metas[best];
}

/* edge_rock(x, y): bits of the west (0-3) and east (4-7) edge columns that have rock */
static unsigned edge_rock(Sh *S, int x, int y)
{
    uint16_t mask[16];
    unsigned bits = 0, k;
    int j;

    if (!rg_cut_mask(S->art, x, y, S->content, mask))
        return 0xFFu;
    for (k = 0; k < 4u; k++) {
        for (j = (int)(4u * k); j < (int)(4u * k + 4u); j++) {
            if (!(mask[j] & 1u))
                bits |= 1u << k;
            if (!((mask[j] >> 15) & 1u))
                bits |= 1u << (4u + k);
        }
    }
    return bits;
}

static void ph_walls(Sh *S)
{
    uint16_t face;
    int x, y, d, k;

    face = pick_face(S, &S->out->faceTies);
    for (y = 0; y < S->H; y++) {
        for (x = 0; x < S->W; x++) {
            RgGrid g;

            if (!S->content[cidx(S, x, y)])
                continue;
            grid_of(S, x, y, &g);
            for (d = 0; d < 3; d++) {
                int dx = d == 0 ? -1 : (d == 1 ? 1 : 0), dy = d == 2 ? 1 : 0, nx = x + dx, ny = y + dy;
                RgGrid o;
                double gap = 0.0;

                if (!inside(S, nx, ny))
                    continue;
                grid_of(S, nx, ny, &o);
                for (k = 0; k <= P; k++) {
                    double v;

                    if (dx < 0)
                        v = g.g[k][0] - o.g[k][P];
                    else if (dx > 0)
                        v = g.g[k][P] - o.g[k][0];
                    else
                        v = g.g[P][k] - o.g[0][k];
                    gap = k == 0 ? v : pymax(gap, v);
                }
                if (gap > 0.5) {
                    size_t c = cidx(S, x, y);

                    S->out->hasWall[c] = 1;
                    S->out->wallFace[c] = face;
                    S->out->wallBits[c] = (uint8_t)edge_rock(S, x, y);
                }
            }
        }
    }
}

static void ph_alias_pop(Sh *S)
{
    unsigned i, o = 0;
    size_t c;

    for (c = 0; c < (size_t)S->W * (size_t)S->H; c++) {
        if (S->keepOut[c]) {
            S->out->has[c] = 0;
            S->out->hasWall[c] = 0;
        }
    }
    for (i = 0; i < S->out->nFills; i++) {
        const RgFill *f = &S->out->fills[i];

        if (!S->keepOut[cidx(S, f->x, f->y)])
            S->out->fills[o++] = *f;
    }
    S->out->nFills = o;
}

static RgErr shapes_body(Sh *S)
{
    RgLat spread;
    RgErr e;
    int x, y;

    sh_content(S);
    sh_cuts(S);
    e = rg_spread(S->h, S->content, &spread);
    if (e != RG_OK)
        return e;
    e = rim_run(S, &spread);
    if (e == RG_OK) {
        for (y = 0; y < S->H; y++) {
            for (x = 0; x < S->W; x++) {
                RgGrid a, b;
                size_t c = cidx(S, x, y);

                if (!S->content[c] || S->out->has[c])
                    continue;
                rg_cell_grid(&spread, x, y, &a);
                rg_cell_grid(S->h, x, y, &b);
                if (!rg_grid_equal(&a, &b))
                    set_grid(S, x, y, &a);
            }
        }
    }
    rg_lat_free(&spread);
    if (e != RG_OK)
        return e;
    e = sh_keep_out(S);
    if (e != RG_OK)
        return e;
    ph_rock_edge(S);
    ph_cut_clear(S);
    ph_ground_edge(S);
    for (y = 0; y < S->H; y++)
        for (x = 0; x < S->W; x++)
            if (S->content[cidx(S, x, y)])
                ph_follow(S, x, y);
    ph_back(S);
    ph_settle_shared(S);
    e = ph_flank(S);
    if (e != RG_OK)
        return e;
    ph_cut_points(S);
    for (y = 0; y < S->H && e == RG_OK; y++)
        for (x = 0; x < S->W && e == RG_OK; x++)
            if (S->content[cidx(S, x, y)])
                e = fill_cell(S, x, y);
    if (e != RG_OK)
        return e;
    ph_walls(S);
    if (S->art->alias != NULL)
        ph_alias_pop(S);
    return RG_OK;
}

RgErr rg_cell_shapes(const RgShapeIn *in, RgShapes *out)
{
    Sh S;
    RgErr e = RG_OK;

    assert(in != NULL && out != NULL);
    if (!sh_alloc(&S, in, out)) {
        sh_free(&S);
        rg_shapes_free(out);
        return RG_ERR_NOMEM;
    }
    if (in->grp != NULL)
        e = shapes_body(&S);
    sh_free(&S);
    if (e != RG_OK)
        rg_shapes_free(out);
    return e;
}
