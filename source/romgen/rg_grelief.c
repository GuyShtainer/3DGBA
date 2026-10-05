/* Modified for 3DGBA (GPLv3), 2026.
 * Original: ZallaxDev/pokeemerald-3Ds-dualscreen @ c330c0a, 3ds_port/scripts/voxel_building.py
 * (Relief, _subtract, Mound), MIT License - see source/voxel/NOTICE.md.
 * Portions Copyright (c) Dust Zallax, MIT. */
/* rg_grelief.c -- the two parts that read their solid off their own drawing, pixel by pixel (3DGBA, GPLv3).
 * Pure C. SPEC-S2 section 1.3. Written statement for statement against vb:877-1383 (same operations, same order). */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "rg_geom.h"

#define RG_PI 3.141592653589793
#define LOOP_CAP 100000u

static void tag_of(char *dst, const char *a, const char *b)
{
    size_t i = 0, k;

    for (k = 0; a[k] != '\0' && i < RG_TAG_LEN - 1; k++)
        dst[i++] = a[k];
    for (k = 0; b[k] != '\0' && i < RG_TAG_LEN - 1; k++)
        dst[i++] = b[k];
    dst[i] = '\0';
}

static RgVtx vtx(double x, double y, double z, double u, double v)
{
    RgVtx r;

    r.x = x; r.y = y; r.z = z; r.u = u; r.v = v;
    return r;
}

static double dmax(double a, double b) { return b > a ? b : a; }   /* Python max(a, b): the first wins a tie */
static double dmin(double a, double b) { return b < a ? b : a; }

/* ---- Relief: the runs of every pixel column ------------------------------------------------ */

typedef struct Run { int16_t a, b; } Run;
typedef struct Col { unsigned n; const Run *r; } Col;

typedef struct RCtx {
    const RgRelief *R;
    RgMesh *m;
    const char *name;
    double H;
    unsigned W;
    unsigned maxRuns;
    Col *cols;
    bool solid;                  /* Relief.solid after `back is not None` forces it */
} RCtx;

static bool cols_equal(const Col *a, const Col *b)
{
    return a->n == b->n && (a->n == 0 || memcmp(a->r, b->r, a->n * sizeof(Run)) == 0);
}

/* the drawn runs of every column, gaps closed up to `hull` (vb:911-924) */
static void runs_scan(const RgRelief *R, unsigned W, unsigned H, unsigned maxRuns, Run *pool, Col *cols)
{
    unsigned u, v;

    for (u = 0; u < W; u++) {
        Run *rs = pool + (size_t)u * maxRuns;
        unsigned n = 0;

        v = 0;
        while (v < H) {
            if (R->art->px[((size_t)v * (size_t)R->art->w + u) * 4u + 3u] >= 128) {
                unsigned v0 = v;

                while (v < H && R->art->px[((size_t)v * (size_t)R->art->w + u) * 4u + 3u] >= 128)
                    v++;
                rs[n].a = (int16_t)v0;
                rs[n].b = (int16_t)v;
                n++;
            } else {
                v++;
            }
        }
        if (R->hull != 0 && n > 0) {
            unsigned k, w = 1;

            for (k = 1; k < n; k++) {
                if ((int)rs[k].a - (int)rs[w - 1].b <= R->hull)
                    rs[w - 1].b = rs[k].b;
                else
                    rs[w++] = rs[k];
            }
            n = w;
        }
        cols[u].n = n;
        cols[u].r = rs;
    }
}

/* an empty column between two identical ones takes their run (vb:926-934) */
static void runs_bridge(int bridge, unsigned W, Col *cols)
{
    unsigned u;

    for (u = 1; u + 1 < W; u++) {
        const Col *left = NULL, *right = NULL;
        int k, lo, hi;

        if (cols[u].n != 0)
            continue;
        lo = (int)u - bridge;
        if (lo < 0) lo = 0;
        for (k = (int)u - 1; k >= lo; k--)
            if (cols[k].n != 0) { left = &cols[k]; break; }
        hi = (int)u + bridge;
        if (hi > (int)W - 1) hi = (int)W - 1;
        for (k = (int)u + 1; k <= hi; k++)
            if (cols[k].n != 0) { right = &cols[k]; break; }
        if (left != NULL && right != NULL && cols_equal(left, right))
            cols[u] = *left;
    }
}

static bool relief_runs(const RgRelief *R, unsigned W, Col *cols, unsigned *maxRunsOut)
{
    unsigned H = (unsigned)R->art->h, maxRuns, u;
    Run *pool, *pool2;

    if (R->hasFlank)
        H = (unsigned)R->flank.rect[1];            /* the bars' band below is no drawing */
    maxRuns = H / 2u + 2u;
    pool = (Run *)malloc((size_t)W * maxRuns * sizeof(Run));
    pool2 = (Run *)malloc((size_t)W * maxRuns * sizeof(Run));
    if (pool == NULL || pool2 == NULL) {
        free(pool); free(pool2);
        return false;
    }
    runs_scan(R, W, H, maxRuns, pool, cols);
    if (R->bridge != 0)
        runs_bridge(R->bridge, W, cols);
    if (R->hasFoot) {
        for (u = 0; u < W; u++) {
            Run *o = pool2 + (size_t)u * maxRuns;

            if (cols[u].n == 0)
                continue;
            o[0].a = cols[u].r[0].a;
            o[0].b = (int16_t)(R->foot > cols[u].r[cols[u].n - 1].b ? R->foot : cols[u].r[cols[u].n - 1].b);
            cols[u].n = 1;
            cols[u].r = o;
        }
    }
    if (R->hasSeam) {
        for (u = 0; u < W; u++) {
            Run *o = pool2 + (size_t)u * maxRuns;
            unsigned k, n = 0;
            bool inN = R->openN != NULL && R->openN[u];

            for (k = 0; k < cols[u].n; k++) {
                Run r = cols[u].r[k];

                if (r.a < R->seamRows && !(inN && r.a == 0 && (double)(r.b - r.a) <= R->height)) {
                    if (n < maxRuns)
                        o[n++] = r;
                }
            }
            cols[u].n = n;
            cols[u].r = o;
        }
    }
    *maxRunsOut = maxRuns;
    cols[W].r = pool;                               /* slots W and W + 1 carry the two pools to the free */
    cols[W].n = 0;
    cols[W + 1].r = pool2;
    cols[W + 1].n = 0;
    return true;
}

/* ---- Relief: pieces of one column ---------------------------------------------------------- */

typedef struct Piece { double zd, z0, z1, top, va; } Piece;   /* drawn z0, solid z0, z1, top, first row */

static unsigned col_pieces(const RCtx *c, const Col *col, Piece *out)
{
    unsigned i;

    for (i = 0; i < col->n; i++) {
        double va = col->r[i].a, vb = col->r[i].b, depth = vb - va - c->H;
        double zd, z1, top, z0;

        if (depth > 0) { zd = va + c->H; z1 = vb; top = c->H; }
        else           { zd = vb;        z1 = vb; top = vb - va; }
        z0 = zd;
        if (c->R->hasBack && top == c->H && c->R->back < z0)
            z0 = c->R->back;
        else if (c->R->against && c->R->hasBack && top == c->H && z0 < c->R->back && c->R->back < z1)
            z0 = c->R->back;
        out[i].zd = zd; out[i].z0 = z0; out[i].z1 = z1; out[i].top = top; out[i].va = va;
    }
    return col->n;
}

static void quad_uv_zy(RgMesh *m, const double q[4][3], double shade, uint16_t tag)
{
    RgVtx p[4];
    unsigned k;

    for (k = 0; k < 4; k++)
        p[k] = vtx(q[k][0], q[k][1], q[k][2], q[k][0], q[k][2] - q[k][1]);
    rg_mesh_poly(m, p, 4, shade, tag);
}

typedef struct TileCtx { RgMesh *m; double shade; uint16_t tag; } TileCtx;

static void tile_cb(void *vc, const RgPt *p, unsigned n)
{
    TileCtx *t = (TileCtx *)vc;
    RgVtx out[8];
    unsigned k;

    for (k = 0; k < n && k < 8; k++)
        out[k] = vtx(p[k].c[2], p[k].c[3], p[k].c[4], p[k].c[5], p[k].c[6]);
    rg_mesh_poly(t->m, out, n, t->shade, t->tag);
}

/* Relief._tile (vb:1119-1125): a 1:1 side tile laid along the face from `origin`, top row at `top`. */
static void relief_tile(RgMesh *m, const double quad[4][3], const double origin[2], const double sdir[2], double top,
                        double shade, uint16_t tag, const RgTile *tile)
{
    RgTile t = rg_tile(tile->rect[0], tile->rect[1], tile->rect[2], tile->rect[3]);
    RgPt pts[4];
    TileCtx tc;
    unsigned k;

    memset(pts, 0, sizeof(pts));
    for (k = 0; k < 4; k++) {
        double x = quad[k][0], y = quad[k][1], z = quad[k][2];

        pts[k].c[0] = (x - origin[0]) * sdir[0] + (z - origin[1]) * sdir[1];
        pts[k].c[1] = top - y;
        pts[k].c[2] = x; pts[k].c[3] = y; pts[k].c[4] = z;
    }
    tc.m = m; tc.shade = shade; tc.tag = tag;
    (void)rg_tile_pieces_x(pts, 4, 5, &t, tile_cb, &tc);
}

/* ---- Relief: one merged span of identical columns ------------------------------------------ */

static void emit_top_depth(const RCtx *c, double x0, double x1, const Piece *p, uint16_t tag)
{
    double rows = p->z1 - p->zd, first = rows >= 2 ? p->va + 1.0 : p->va;
    double course = dmax(1.0, rows - (first - p->va));
    double edge = (rows >= 2 && p->zd - p->z0 > 1) ? 1.0 : 0.0, far = p->zd;
    unsigned guard = 0;

    while (far > p->z0 + edge + 1e-6 && guard++ < LOOP_CAP) {
        double near = far, va0, q[4][3];
        RgVtx v[4];
        unsigned k;

        far = dmax(p->z0 + edge, far - course);
        va0 = first + (course - (near - far));
        q[0][0] = x0; q[0][1] = p->top; q[0][2] = far;
        q[1][0] = x1; q[1][1] = p->top; q[1][2] = far;
        q[2][0] = x1; q[2][1] = p->top; q[2][2] = near;
        q[3][0] = x0; q[3][1] = p->top; q[3][2] = near;
        for (k = 0; k < 4; k++)
            v[k] = vtx(q[k][0], q[k][1], q[k][2], q[k][0], va0 + (q[k][2] - far));
        rg_mesh_poly(c->m, v, 4, RG_SHADE_ART, tag);
    }
    if (edge != 0.0) {
        double q[4][3];
        RgVtx v[4];
        unsigned k;

        q[0][0] = x0; q[0][1] = p->top; q[0][2] = p->z0;
        q[1][0] = x1; q[1][1] = p->top; q[1][2] = p->z0;
        q[2][0] = x1; q[2][1] = p->top; q[2][2] = p->z0 + 1;
        q[3][0] = x0; q[3][1] = p->top; q[3][2] = p->z0 + 1;
        for (k = 0; k < 4; k++)
            v[k] = vtx(q[k][0], q[k][1], q[k][2], q[k][0], p->va + (q[k][2] - p->z0));
        rg_mesh_poly(c->m, v, 4, RG_SHADE_ART, tag);
    }
}

typedef struct TopCtx { RgMesh *m; double top, zd; uint16_t tag; } TopCtx;

/* mesh.poly([(s, top, zd - t, uu, vv)]) over the pieces of a plain (s, t) face */
static void topdepth_cb(void *vc, const RgPt *p, unsigned n)
{
    TopCtx *t = (TopCtx *)vc;
    RgVtx out[8];
    unsigned k;

    for (k = 0; k < n && k < 8; k++)
        out[k] = vtx(p[k].c[0], t->top, t->zd - p[k].c[1], p[k].c[2], p[k].c[3]);
    rg_mesh_poly(t->m, out, n, RG_SHADE_ART, t->tag);
}

static void emit_span_top(const RCtx *c, double x0, double x1, const Piece *p)
{
    RgMesh *m = c->m;
    char tg[RG_TAG_LEN];
    double zt = dmax(p->zd, p->z0);
    double q[4][3];

    if (p->z1 > zt) {
        q[0][0] = x0; q[0][1] = p->top; q[0][2] = zt;
        q[1][0] = x1; q[1][1] = p->top; q[1][2] = zt;
        q[2][0] = x1; q[2][1] = p->top; q[2][2] = p->z1;
        q[3][0] = x0; q[3][1] = p->top; q[3][2] = p->z1;
        tag_of(tg, c->name, ".top");
        quad_uv_zy(m, q, RG_SHADE_ART, rg_mesh_tag(m, tg));
    }
    if (p->z0 > p->zd) {
        double zr = p->z0 + 0.5, rise = zr - p->va;

        q[0][0] = x0; q[0][1] = p->top; q[0][2] = zr;
        q[1][0] = x1; q[1][1] = p->top; q[1][2] = zr;
        q[2][0] = x1; q[2][1] = rise;   q[2][2] = zr;
        q[3][0] = x0; q[3][1] = rise;   q[3][2] = zr;
        tag_of(tg, c->name, ".riser");
        quad_uv_zy(m, q, RG_SHADE_ART, rg_mesh_tag(m, tg));
    }
    tag_of(tg, c->name, ".top~depth");
    if (p->zd > p->z0 && c->R->hasTopTile) {
        RgPt face[4];
        TopCtx tc;
        RgTile t = c->R->topTile;

        memset(face, 0, sizeof(face));
        face[0].c[0] = x0; face[0].c[1] = 0.0;
        face[1].c[0] = x1; face[1].c[1] = 0.0;
        face[2].c[0] = x1; face[2].c[1] = p->zd - p->z0;
        face[3].c[0] = x0; face[3].c[1] = p->zd - p->z0;
        tc.m = m; tc.top = p->top; tc.zd = p->zd; tc.tag = rg_mesh_tag(m, tg);
        (void)rg_tile_pieces(face, 4, &t, topdepth_cb, &tc);
    } else if (p->zd > p->z0) {
        emit_top_depth(c, x0, x1, p, rg_mesh_tag(m, tg));
    }
}

static bool in_flags(const uint8_t *f, unsigned u)
{
    return f != NULL && f[u] != 0;
}

static void emit_span_faces(const RCtx *c, double x0, double x1, unsigned u, const Piece *p)
{
    const RgRelief *R = c->R;
    RgMesh *m = c->m;
    char tg[RG_TAG_LEN];
    double q[4][3];
    bool seamS = R->hasSeam && in_flags(R->openS, u) && p->z1 >= (double)R->seamRows + c->H - 1e-6;
    bool seamN = R->hasSeam && in_flags(R->openN, u) && p->va == 0;

    if (!seamS) {
        q[0][0] = x0; q[0][1] = -1.0; q[0][2] = p->z1;
        q[1][0] = x1; q[1][1] = -1.0; q[1][2] = p->z1;
        q[2][0] = x1; q[2][1] = p->top; q[2][2] = p->z1;
        q[3][0] = x0; q[3][1] = p->top; q[3][2] = p->z1;
        tag_of(tg, c->name, ".front");
        quad_uv_zy(m, q, RG_SHADE_ART, rg_mesh_tag(m, tg));
    }
    if (p->z1 > p->z0 && !seamN) {
        q[0][0] = x1; q[0][1] = -1.0; q[0][2] = p->z0;
        q[1][0] = x0; q[1][1] = -1.0; q[1][2] = p->z0;
        q[2][0] = x0; q[2][1] = p->top; q[2][2] = p->z0;
        q[3][0] = x1; q[3][1] = p->top; q[3][2] = p->z0;
        if (R->hull != 0 || (R->hasFoot && !c->solid)) {
            tag_of(tg, c->name, ".back~proj");
            quad_uv_zy(m, q, RG_SHADE_BACK, rg_mesh_tag(m, tg));
        } else {
            double origin[2], sdir[2] = {-1.0, 0.0};

            origin[0] = x1; origin[1] = p->z0;
            tag_of(tg, c->name, R->hasBack ? ".back~depth" : ".back");
            relief_tile(m, q, origin, sdir, p->top, RG_SHADE_BACK, rg_mesh_tag(m, tg), &R->side);
        }
    }
}

static void emit_spans(const RCtx *c, Piece *pa, Piece *pb)
{
    unsigned u = 0, i;

    while (u < c->W) {
        unsigned u1 = u + 1, n;

        while (u1 < c->W && cols_equal(&c->cols[u1], &c->cols[u]))
            u1++;
        n = col_pieces(c, &c->cols[u], pa);
        (void)pb;
        for (i = 0; i < n; i++) {
            double x0 = (double)u, x1 = (double)u1;

            emit_span_top(c, x0, x1, &pa[i]);
            emit_span_faces(c, x0, x1, u, &pa[i]);
        }
        u = u1;
    }
}

/* ---- Relief: the flanks -------------------------------------------------------------------- */

typedef struct Span { double a, b; } Span;

/* _subtract (vb:1128-1141): the parts of (a, b) not covered by any of `others`. `out` holds nOthers + 2. */
static unsigned subtract_spans(double a, double b, const Span *others, unsigned nOthers, Span *out, Span *tmp)
{
    unsigned n = 1, i, k, nn, res = 0;

    out[0].a = a; out[0].b = b;
    for (i = 0; i < nOthers; i++) {
        double c = others[i].a, d = others[i].b;

        nn = 0;
        for (k = 0; k < n; k++) {
            double sa = out[k].a, sb = out[k].b;

            if (d <= sa || c >= sb) { tmp[nn].a = sa; tmp[nn].b = sb; nn++; continue; }
            if (c > sa) { tmp[nn].a = sa; tmp[nn].b = c; nn++; }
            if (d < sb) { tmp[nn].a = d; tmp[nn].b = sb; nn++; }
        }
        memcpy(out, tmp, nn * sizeof(Span));
        n = nn;
    }
    for (k = 0; k < n; k++)
        if (out[k].b - out[k].a > 1e-6)
            out[res++] = out[k];
    return res;
}

static unsigned flag_count(const uint8_t *f, unsigned n)
{
    unsigned i, c = 0;

    for (i = 0; f != NULL && i < n; i++)
        c += f[i] != 0;
    return c;
}

/* `all(r in edge for r in range(int(za) // 16, ceil(zb) // 16 + 1) if r * 16 < zb)` */
static bool run_goes_on(const RgRelief *R, const uint8_t *edge, double za, double zb)
{
    int r0 = rg_floordiv((int)za, 16), r1 = rg_floordiv((int)ceil(zb), 16) + 1, r;

    for (r = r0; r < r1; r++) {
        if ((double)(r * 16) >= zb)
            continue;
        if (r < 0 || (unsigned)r >= R->nSeamRows || edge[r] == 0)
            return false;
    }
    return true;
}

static void flank_face(const RCtx *c, unsigned u, double facing, double za, double zb, double t)
{
    const RgRelief *R = c->R;
    RgMesh *m = c->m;
    char tg[RG_TAG_LEN];
    double q[4][3], x = (double)u;
    double origin[2], sdir[2];
    const uint8_t *edge = u == 0 ? R->seamW : u == c->W ? R->seamE : NULL;

    if (edge != NULL && flag_count(edge, R->nSeamRows) != 0 && run_goes_on(R, edge, za, zb))
        return;                                   /* the run goes on into the next block */
    if (R->hasFlank && zb - za >= 16) {
        if (facing > 0) {
            q[0][0] = x; q[0][1] = -1.0; q[0][2] = zb; q[1][0] = x; q[1][1] = -1.0; q[1][2] = za;
            q[2][0] = x; q[2][1] = t;    q[2][2] = za; q[3][0] = x; q[3][1] = t;    q[3][2] = zb;
        } else {
            q[0][0] = x; q[0][1] = -1.0; q[0][2] = za; q[1][0] = x; q[1][1] = -1.0; q[1][2] = zb;
            q[2][0] = x; q[2][1] = t;    q[2][2] = zb; q[3][0] = x; q[3][1] = t;    q[3][2] = za;
        }
        origin[0] = x; origin[1] = 0.0; sdir[0] = 0.0; sdir[1] = 1.0;
        tag_of(tg, c->name, ".flank");
        relief_tile(m, q, origin, sdir, t, facing > 0 ? RG_SHADE_EAST : RG_SHADE_WEST, rg_mesh_tag(m, tg), &R->flank);
        return;
    }
    if (R->hull != 0 || (R->hasFoot && !c->solid)) {
        /* the end of a railing: the drawn column there, projected */
        int ci = (int)u - (facing > 0 ? 1 : 0);
        double col;
        RgVtx v[4];
        unsigned k;

        if (ci < 0) ci = 0;
        if (ci > (int)c->W - 1) ci = (int)c->W - 1;
        col = (double)ci + 0.5;
        q[0][0] = x; q[0][1] = -1.0; q[0][2] = za; q[1][0] = x; q[1][1] = -1.0; q[1][2] = zb;
        q[2][0] = x; q[2][1] = t;    q[2][2] = zb; q[3][0] = x; q[3][1] = t;    q[3][2] = za;
        for (k = 0; k < 4; k++)
            v[k] = vtx(q[k][0], q[k][1], q[k][2], col, q[k][2] - q[k][1]);
        tag_of(tg, c->name, ".flank~proj");
        rg_mesh_poly(m, v, 4, facing > 0 ? RG_SHADE_EAST : RG_SHADE_WEST, rg_mesh_tag(m, tg));
        return;
    }
    tag_of(tg, c->name, R->hasBack ? ".flank~depth" : ".flank");
    if (facing > 0) {
        q[0][0] = x; q[0][1] = -1.0; q[0][2] = zb; q[1][0] = x; q[1][1] = -1.0; q[1][2] = za;
        q[2][0] = x; q[2][1] = t;    q[2][2] = za; q[3][0] = x; q[3][1] = t;    q[3][2] = zb;
        origin[0] = x; origin[1] = zb; sdir[0] = 0.0; sdir[1] = -1.0;
        relief_tile(m, q, origin, sdir, t, RG_SHADE_EAST, rg_mesh_tag(m, tg), &R->side);
    } else {
        q[0][0] = x; q[0][1] = -1.0; q[0][2] = za; q[1][0] = x; q[1][1] = -1.0; q[1][2] = zb;
        q[2][0] = x; q[2][1] = t;    q[2][2] = zb; q[3][0] = x; q[3][1] = t;    q[3][2] = za;
        origin[0] = x; origin[1] = za; sdir[0] = 0.0; sdir[1] = 1.0;
        relief_tile(m, q, origin, sdir, t, RG_SHADE_WEST, rg_mesh_tag(m, tg), &R->side);
    }
}

/* the covered (b > a) solid pieces of a column */
static unsigned covered_pieces(const RCtx *c, const Col *col, Piece *tmp, Piece *out)
{
    unsigned n = col_pieces(c, col, tmp), i, k = 0;

    for (i = 0; i < n; i++)
        if (tmp[i].z1 > tmp[i].z0)
            out[k++] = tmp[i];
    return k;
}

static void emit_flanks(const RCtx *c, Piece *scratch, Piece *left, Piece *right)
{
    unsigned u;
    Span *others = (Span *)malloc((size_t)(c->maxRuns + 2u) * 3u * sizeof(Span));
    Span *parts, *tmp;

    if (others == NULL) {
        c->m->failed = true;
        return;
    }
    parts = others + c->maxRuns + 2u;
    tmp = parts + c->maxRuns + 2u;
    for (u = 0; u <= c->W; u++) {
        unsigned nl = u > 0 ? covered_pieces(c, &c->cols[u - 1], scratch, left) : 0;
        unsigned nr = u < c->W ? covered_pieces(c, &c->cols[u], scratch, right) : 0;
        unsigned side;

        for (side = 0; side < 2; side++) {
            const Piece *pieces = side == 0 ? left : right, *oth = side == 0 ? right : left;
            unsigned np = side == 0 ? nl : nr, no = side == 0 ? nr : nl, i, k, j;
            double facing = side == 0 ? 1.0 : -1.0;

            for (i = 0; i < np; i++) {
                unsigned nOthers = 0, ns;

                for (k = 0; k < no; k++)
                    if (oth[k].top >= pieces[i].top) {
                        others[nOthers].a = oth[k].z0;
                        others[nOthers].b = oth[k].z1;
                        nOthers++;
                    }
                ns = subtract_spans(pieces[i].z0, pieces[i].z1, others, nOthers, parts, tmp);
                for (j = 0; j < ns; j++)
                    flank_face(c, u, facing, parts[j].a, parts[j].b, pieces[i].top);
            }
        }
    }
    free(others);
}

bool rg_emit_relief(const RgRelief *R, const char *name, RgMesh *m)
{
    RCtx c;
    Col *cols;
    Piece *pa, *pb, *pc;
    unsigned W;

    if (R == NULL || R->art == NULL || R->art->w <= 0 || R->art->h <= 0)
        return false;
    W = (unsigned)R->art->w;
    cols = (Col *)calloc((size_t)W + 2u, sizeof(Col));
    if (cols == NULL)
        return false;
    memset(&c, 0, sizeof(c));
    c.R = R; c.m = m; c.name = name; c.H = R->height; c.W = W; c.cols = cols;
    c.solid = R->solid || R->hasBack;
    if (!relief_runs(R, W, cols, &c.maxRuns)) {
        free(cols);
        return false;
    }
    pa = (Piece *)malloc((size_t)c.maxRuns * 3u * sizeof(Piece));
    if (pa == NULL) {
        free((void *)cols[W].r); free((void *)cols[W + 1].r); free(cols);
        return false;
    }
    pb = pa + c.maxRuns;
    pc = pb + c.maxRuns;
    emit_spans(&c, pa, pb);
    emit_flanks(&c, pa, pb, pc);
    free(pa);
    free((void *)cols[W].r); free((void *)cols[W + 1].r); free(cols);
    return !m->failed;
}

/* ---- Mound --------------------------------------------------------------------------------- */

static bool in_ring(const int (*ring)[3], unsigned n, const uint8_t *rgb)
{
    unsigned i;

    for (i = 0; i < n; i++)
        if (ring[i][0] == rgb[0] && ring[i][1] == rgb[1] && ring[i][2] == rgb[2])
            return true;
    return false;
}

/* Mound.ring_pixels (vb:1195-1208): per column the ring-coloured pixels below the lowest pixel of the rock itself.
 * `px` = RGBA rows of width W; marks[y*W+x] = 1 for each. */
static void ring_pixels(const uint8_t *px, int W, int H, const int (*ring)[3], unsigned nRing, uint8_t *marks)
{
    int x, y;

    memset(marks, 0, (size_t)W * (size_t)H);
    for (x = 0; x < W; x++) {
        int foot = -1;

        for (y = 0; y < H; y++) {
            const uint8_t *p = px + ((size_t)y * (size_t)W + (size_t)x) * 4u;

            if (p[3] >= 128 && !in_ring(ring, nRing, p))
                foot = y;
        }
        for (y = foot + 1; y < H; y++) {
            const uint8_t *p = px + ((size_t)y * (size_t)W + (size_t)x) * 4u;

            if (p[3] >= 128 && in_ring(ring, nRing, p))
                marks[(size_t)y * (size_t)W + (size_t)x] = 1;
        }
    }
}

bool rg_mound_with_ring(const RgImage *art, const int (*ring)[3], unsigned nRing, RgImage *out)
{
    int W = art->w, H = art->h, x, y;
    size_t n = (size_t)W * (size_t)H;
    uint8_t *foam = (uint8_t *)malloc(n), *grown = (uint8_t *)calloc(n, 1);
    uint8_t *col = (uint8_t *)malloc(n * 3u);
    int *queue = (int *)malloc(n * sizeof(int));
    size_t qh = 0, qt = 0;

    if (foam == NULL || grown == NULL || col == NULL || queue == NULL || !rg_img_new(out, W, H * 3)) {
        free(foam); free(grown); free(col); free(queue);
        return false;
    }
    ring_pixels(art->px, W, H, ring, nRing, foam);
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++) {
            size_t i = (size_t)y * (size_t)W + (size_t)x;
            const uint8_t *a = art->px + i * 4u;

            if (a[3] >= 128) {
                memcpy(out->px + (((size_t)(y + (foam[i] ? H : 0))) * (size_t)W + (size_t)x) * 4u, a, 4);
                if (!foam[i]) {
                    grown[i] = 1;
                    memcpy(col + i * 3u, a, 3);
                    queue[qt++] = (int)i;
                }
            }
        }
    while (qh < qt) {                               /* FIFO = upstream's level by level, same neighbour order */
        int i = queue[qh++], px = i % W, py = i / W, k;
        static const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};

        for (k = 0; k < 4; k++) {
            int nx = px + dx[k], ny = py + dy[k];
            size_t j;

            if (nx < 0 || nx >= W || ny < 0 || ny >= H)
                continue;
            j = (size_t)ny * (size_t)W + (size_t)nx;
            if (grown[j])
                continue;
            grown[j] = 1;
            memcpy(col + j * 3u, col + (size_t)i * 3u, 3);
            queue[qt++] = (int)j;
        }
    }
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++) {
            size_t i = (size_t)y * (size_t)W + (size_t)x;
            uint8_t *o;

            if (!grown[i])
                continue;
            o = out->px + (((size_t)(y + 2 * H)) * (size_t)W + (size_t)x) * 4u;
            o[0] = col[i * 3u]; o[1] = col[i * 3u + 1]; o[2] = col[i * 3u + 2]; o[3] = 255;
        }
    free(foam); free(grown); free(col); free(queue);
    return true;
}

typedef struct Line { double x; int top, foot; } Line;

/* Mound._spans (vb:1241-1281) over the body (the first `rows` rows of the art, ring pixels cleared). */
static bool straight(const Line *lines, unsigned i, unsigned j)
{
    const Line *a = &lines[i], *b = &lines[j];
    unsigned k;

    for (k = i + 1; k < j; k++) {
        const Line *l = &lines[k];
        double g = (l->x - a->x) / (b->x - a->x);

        if (fabs((double)a->top + (double)(b->top - a->top) * g - (double)l->top) > 0.5 ||
            fabs((double)a->foot + (double)(b->foot - a->foot) * g - (double)l->foot) > 0.5)
            return false;
    }
    return true;
}

static unsigned mound_spans(const RgImage *body, int W, int H, int step, Line *keepOut)
{
    int *top = (int *)malloc((size_t)W * 2u * sizeof(int)), *foot;
    Line *lines = (Line *)malloc(((size_t)W + 2u) * sizeof(Line));
    unsigned nl = 0, nk = 0, i, j;
    int u, u0 = -1, u1 = -1;

    if (top == NULL || lines == NULL) {
        free(top); free(lines);
        return 0;
    }
    foot = top + W;
    for (u = 0; u < W; u++) {
        int y, f = -1, t = -1;

        for (y = 0; y < H; y++)
            if (body->px[((size_t)y * (size_t)W + (size_t)u) * 4u + 3u] >= 128) {
                if (t < 0) t = y;
                f = y;
            }
        top[u] = t;
        foot[u] = f + 1;
        if (t >= 0) {
            if (u0 < 0) u0 = u;
            u1 = u + 1;
        }
    }
    if (u0 < 0) {
        free(top); free(lines);
        return 0;
    }
    for (u = u0; u < u1; u++)
        if (top[u] < 0) {                           /* a gap down the middle: bridged */
            top[u] = top[u - 1];
            foot[u] = foot[u - 1];
        }
    lines[nl].x = (double)u0; lines[nl].top = top[u0]; lines[nl].foot = foot[u0]; nl++;
    for (u = u0; u < u1; u++) {
        lines[nl].x = (double)u + 0.5; lines[nl].top = top[u]; lines[nl].foot = foot[u]; nl++;
    }
    lines[nl].x = (double)u1; lines[nl].top = top[u1 - 1]; lines[nl].foot = foot[u1 - 1]; nl++;
    keepOut[nk++] = lines[0];
    i = 0;
    while (i + 1 < nl) {
        j = i + 1;
        while (j + 1 < nl && lines[j + 1].x - lines[i].x <= (double)step && straight(lines, i, j + 1))
            j++;
        keepOut[nk++] = lines[j];
        i = j;
    }
    free(top); free(lines);
    return nk;
}

typedef struct MCol { RgVtx *front; RgVtx *back; double zc; } MCol;

/* Mound._tri (vb:1347-1363). a, b, c are (x, y, z, u, v); outward == NULL: drawn, faces the 45 degree view. */
static void mound_tri(RgMesh *m, RgVtx a, RgVtx b, RgVtx c, double shade, uint16_t tag, const double *outward)
{
    double e1[3] = {b.x - a.x, b.y - a.y, b.z - a.z}, e2[3] = {c.x - a.x, c.y - a.y, c.z - a.z};
    double n[3], sq[3];

    n[0] = e1[1] * e2[2] - e1[2] * e2[1];
    n[1] = e1[2] * e2[0] - e1[0] * e2[2];
    n[2] = e1[0] * e2[1] - e1[1] * e2[0];
    sq[0] = n[0] * n[0]; sq[1] = n[1] * n[1]; sq[2] = n[2] * n[2];
    if (rg_pysum(sq, 3) < 1e-12)
        return;
    if (outward == NULL) {
        if (n[1] + n[2] < 0) { RgVtx t = b; b = c; c = t; }
    } else {
        double g[3] = {(a.x + b.x + c.x) / 3.0 - outward[0], (a.y + b.y + c.y) / 3.0 - outward[1],
                       (a.z + b.z + c.z) / 3.0 - outward[2]};

        if (n[0] * g[0] + n[1] * g[1] + n[2] * g[2] < 0) { RgVtx t = b; b = c; c = t; }
    }
    rg_mesh_tri(m, &a, &b, &c, shade, tag);
}

static void mound_quad(RgMesh *m, RgVtx p, RgVtx q, RgVtx r, RgVtx s, double shade, uint16_t tag, const double *outward)
{
    mound_tri(m, p, q, r, shade, tag, outward);
    mound_tri(m, p, r, s, shade, tag, outward);
}

static RgVtx behind(RgVtx p, double band)
{
    p.v += band;
    return p;
}

static void mound_column(const Line *ln, double k, int n, int backSteps, double backBand, MCol *out)
{
    double x = ln->x, vt = (double)ln->top, vb = (double)ln->foot;
    double L = vb - vt, a = L / (1.0 + sqrt(1.0 + k * k)), b = k * a, zc = vb - a;
    double A = 1.0 / (a * a) + 1.0 / (b * b), th0;
    int i;

    for (i = 0; i <= n; i++) {
        double v = vt + L * (double)i / (double)n;
        double B = 2.0 * (v - zc) / (a * a);
        double C = (v - zc) * (v - zc) / (a * a) - 1.0;
        double disc = dmax(0.0, B * B - 4 * A * C);
        double t = dmax(0.0, (-B + sqrt(disc)) / (2 * A));

        out->front[i] = vtx(x, t, v + t, x, v);
    }
    th0 = RG_PI - atan2(b, a);
    for (i = 1; i <= backSteps; i++) {
        double th = th0 + (RG_PI - th0) * (double)i / (double)backSteps;
        double z = zc + a * cos(th), y = b * sin(th);

        if (i == backSteps)
            y = 0.0;
        out->back[i - 1] = vtx(x, y, z, x, dmin(vb - 1e-3, dmax(vt, z - y)) + backBand);
    }
    out->zc = zc;
}

static void mound_ring(const RgMound *M, const char *name, RgMesh *m)
{
    const RgImage *F = M->full;
    int x0 = F->w, y0 = M->rows, x1 = 0, y1 = 0, x, y, yend = F->h < 2 * M->rows ? F->h : 2 * M->rows;
    bool any = false;
    char tg[RG_TAG_LEN];

    for (y = M->rows; y < yend; y++)
        for (x = 0; x < F->w; x++)
            if (F->px[((size_t)y * (size_t)F->w + (size_t)x) * 4u + 3u] != 0) {
                if (!any || x < x0) x0 = x;
                if (!any || x + 1 > x1) x1 = x + 1;
                if (!any || y < y0) y0 = y;
                if (!any || y + 1 > y1) y1 = y + 1;
                any = true;
            }
    if (!any)
        return;
    {
        double e = 0.05;
        double xs[4] = {(double)x0, (double)x1, (double)x1, (double)x0};
        double vs[4] = {(double)(y0 - M->rows), (double)(y0 - M->rows), (double)(y1 - M->rows), (double)(y1 - M->rows)};
        RgVtx q[4];
        unsigned k;

        for (k = 0; k < 4; k++)
            q[k] = vtx(xs[k], e, vs[k] + e, xs[k], vs[k] + (double)M->rows);
        tag_of(tg, name, ".ring~proj");
        mound_quad(m, q[0], q[1], q[2], q[3], RG_SHADE_ART, rg_mesh_tag(m, tg), NULL);
    }
}

static void mound_ends(const MCol *first, const MCol *last, int n, int backSteps, double backBand, const char *name,
                       RgMesh *m)
{
    unsigned e;
    char tg[RG_TAG_LEN];

    tag_of(tg, name, ".end~behind~proj");
    for (e = 0; e < 2; e++) {
        const MCol *c = e == 0 ? first : last;
        double side = e == 0 ? -1.0 : 1.0;
        RgVtx ring[272];
        unsigned nr = 0, i;
        double centre[3];

        for (i = (unsigned)n + 1; i > 0; i--)
            ring[nr++] = behind(c->front[i - 1], backBand);
        for (i = 0; i < (unsigned)backSteps; i++)
            ring[nr++] = c->back[i];
        if (nr < 3)
            continue;
        centre[0] = ring[0].x - side; centre[1] = 0.0; centre[2] = c->zc;
        for (i = 1; i + 1 < nr; i++)
            mound_tri(m, ring[0], ring[i], ring[i + 1], RG_SHADE_WOUND, rg_mesh_tag(m, tg), centre);
    }
}

static void mound_faces(const RgMound *M, const char *name, RgMesh *m, const MCol *cols, unsigned nc, int n,
                        double backBand)
{
    unsigned c;
    int i;
    char proj[RG_TAG_LEN], bh[RG_TAG_LEN];

    tag_of(proj, name, "~proj");
    tag_of(bh, name, ".back~behind~proj");
    for (c = 0; c + 1 < nc; c++) {
        const MCol *A = &cols[c], *B = &cols[c + 1];
        RgVtx ra[16], rb[16];
        double centre[3];

        for (i = 0; i < n; i++)
            mound_quad(m, A->front[i], B->front[i], B->front[i + 1], A->front[i + 1], RG_SHADE_ART,
                       rg_mesh_tag(m, proj), NULL);
        ra[0] = behind(A->front[0], backBand);
        rb[0] = behind(B->front[0], backBand);
        for (i = 0; i < M->backSteps; i++) {
            ra[i + 1] = A->back[i];
            rb[i + 1] = B->back[i];
        }
        centre[0] = (A->front[0].x + B->front[0].x) / 2.0; centre[1] = 0.0; centre[2] = (A->zc + B->zc) / 2.0;
        for (i = 0; i < M->backSteps; i++)
            mound_quad(m, ra[i], rb[i], rb[i + 1], ra[i + 1], RG_SHADE_WOUND, rg_mesh_tag(m, bh), centre);
    }
}

bool rg_emit_mound(const RgMound *M, const char *name, RgMesh *m)
{
    const RgImage *F;
    RgImage body;
    uint8_t *marks;
    Line *spans;
    MCol *cols;
    RgVtx *pool;
    unsigned ns, c, i;
    int n, W;
    double maxLen = 0.0, backBand;
    bool ok = false;

    if (M == NULL || M->full == NULL)
        return false;
    F = M->full;
    if (M->rows <= 0 || M->rows > F->h || M->backSteps < 1 || M->backSteps > 8 || M->step < 1)
        return false;
    W = F->w;
    /* body = full.crop(0, 0, W, rows), its ring pixels cleared (vb:1182-1188) */
    if (!rg_img_new(&body, W, M->rows))
        return false;
    memcpy(body.px, F->px, (size_t)W * (size_t)M->rows * 4u);
    marks = (uint8_t *)malloc((size_t)W * (size_t)M->rows);
    spans = (Line *)malloc(((size_t)W + 4u) * sizeof(Line));
    if (marks == NULL || spans == NULL)
        goto out;
    ring_pixels(body.px, W, M->rows, M->ring, M->nRing, marks);
    for (i = 0; i < (unsigned)W * (unsigned)M->rows; i++)
        if (marks[i])
            memset(body.px + (size_t)i * 4u, 0, 4);
    backBand = F->h >= 3 * M->rows ? 2.0 * M->rows : 0.0;
    ns = mound_spans(&body, W, M->rows, M->step, spans);
    if (ns < 2)
        goto out;
    for (c = 0; c < ns; c++) {
        double len = (double)(spans[c].foot - spans[c].top);

        if (len > maxLen) maxLen = len;
    }
    n = (int)ceil(maxLen / (double)M->step);
    if (n < 4) n = 4;
    if (n > 256)
        goto out;
    cols = (MCol *)calloc(ns, sizeof(MCol));
    pool = (RgVtx *)malloc((size_t)ns * ((size_t)n + 1u + (size_t)M->backSteps) * sizeof(RgVtx));
    if (cols == NULL || pool == NULL) {
        free(cols); free(pool);
        goto out;
    }
    for (c = 0; c < ns; c++) {
        cols[c].front = pool + (size_t)c * ((size_t)n + 1u + (size_t)M->backSteps);
        cols[c].back = cols[c].front + n + 1;
        mound_column(&spans[c], M->rise, n, M->backSteps, backBand, &cols[c]);
    }
    mound_faces(M, name, m, cols, ns, n, backBand);
    if (M->nRing > 0)
        mound_ring(M, name, m);
    mound_ends(&cols[0], &cols[ns - 1], n, M->backSteps, backBand, name, m);
    free(cols); free(pool);
    ok = !m->failed;
out:
    free(marks); free(spans);
    rg_img_free(&body);
    return ok;
}
