/* Modified for 3DGBA (GPLv3), 2026.
 * Original: ZallaxDev/pokeemerald-3Ds-dualscreen @ c330c0a, 3ds_port/scripts/voxel_building.py
 * (geometry kernel, texture mappings, every part class), MIT License - see source/voxel/NOTICE.md.
 * Portions Copyright (c) Dust Zallax, MIT. */
#include "rg_geom.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define RG_PI 3.141592653589793
#define RG_DEG2RAD 0.017453292519943295   /* math.radians(x) == x * (pi / 180) */
#define MAXV RG_CLIP_MAX
#define SEG_MAX 512u
#define UP_MAX 64u

/* ---- small helpers ------------------------------------------------------------------------ */

double rg_pysum(const double *v, unsigned n)
{
    double f = 0.0;
    unsigned i;
#if RG_PYSUM_COMPENSATED
    double c = 0.0;                                   /* CPython 3.12 builtin_sum: Neumaier */

    for (i = 0; i < n; i++) {
        double x = v[i], t = f + x;

        if (fabs(f) >= fabs(x))
            c += (f - t) + x;
        else
            c += (x - t) + f;
        f = t;
    }
    if (c != 0.0 && isfinite(c))
        f += c;
#else
    for (i = 0; i < n; i++)
        f += v[i];
#endif
    return f;
}

int rg_floordiv(int a, int b)
{
    int q = a / b;

    if ((a % b != 0) && ((a < 0) != (b < 0)))
        q--;
    return q;
}

int rg_floormod(int a, int b)
{
    int r = a % b;

    if (r != 0 && ((r < 0) != (b < 0)))
        r += b;
    return r;
}

bool rg_stable_sort(void *base, size_t n, size_t size, int (*cmp)(const void *, const void *))
{
    uint8_t *a = (uint8_t *)base, *tmp, *src, *dst;
    size_t width;

    if (n < 2)
        return true;
    tmp = (uint8_t *)malloc(n * size);
    if (tmp == NULL)
        return false;
    src = a;
    dst = tmp;
    for (width = 1; width < n; width *= 2) {
        size_t lo;

        for (lo = 0; lo < n; lo += 2 * width) {
            size_t mid = lo + width < n ? lo + width : n, hi = lo + 2 * width < n ? lo + 2 * width : n;
            size_t i = lo, j = mid, k = lo;

            while (i < mid && j < hi) {
                if (cmp(src + j * size, src + i * size) < 0)   /* strictly less from the right: ties keep the left */
                    memcpy(dst + (k++) * size, src + (j++) * size, size);
                else
                    memcpy(dst + (k++) * size, src + (i++) * size, size);
            }
            while (i < mid)
                memcpy(dst + (k++) * size, src + (i++) * size, size);
            while (j < hi)
                memcpy(dst + (k++) * size, src + (j++) * size, size);
        }
        {
            uint8_t *s = src;
            src = dst;
            dst = s;
        }
    }
    if (src != a)
        memcpy(a, src, n * size);
    free(tmp);
    return true;
}

/* Tag names are built without stdio. */
static void tag_cat(char *dst, const char *a, const char *b)
{
    size_t i = 0, k;

    for (k = 0; a[k] != '\0' && i < RG_TAG_LEN - 1; k++)
        dst[i++] = a[k];
    for (k = 0; b[k] != '\0' && i < RG_TAG_LEN - 1; k++)
        dst[i++] = b[k];
    dst[i] = '\0';
}

static void tag_edge(char *dst, const char *name, const char *mid, unsigned n)
{
    char digits[12], base[RG_TAG_LEN];
    unsigned len = 0;

    do {
        digits[len++] = (char)('0' + n % 10u);
        n /= 10u;
    } while (n != 0 && len < 11);
    tag_cat(base, name, mid);
    {
        size_t i = strlen(base);

        while (len > 0 && i < RG_TAG_LEN - 1)
            base[i++] = digits[--len];
        base[i] = '\0';
    }
    memcpy(dst, base, RG_TAG_LEN);
}

static RgVtx V(double x, double y, double z, double u, double v)
{
    RgVtx r;

    r.x = x; r.y = y; r.z = z; r.u = u; r.v = v;
    return r;
}

/* ---- mesh ---------------------------------------------------------------------------------- */

void rg_mesh_init(RgMesh *m)
{
    memset(m, 0, sizeof(*m));
}

void rg_mesh_free(RgMesh *m)
{
    free(m->t);
    free(m->names);
    memset(m, 0, sizeof(*m));
}

static uint8_t tag_flags(const char *s)
{
    size_t n = strlen(s);
    uint8_t f = 0;

    if (n >= 6 && strcmp(s + n - 6, "~clamp") == 0) f |= RG_TAG_CLAMP;
    if (n >= 5 && strcmp(s + n - 5, "~proj") == 0) f |= RG_TAG_PROJ;
    if (strstr(s, "~depth") != NULL) f |= RG_TAG_DEPTH;
    if (strstr(s, "~behind") != NULL) f |= RG_TAG_BEHIND;
    return f;
}

uint16_t rg_mesh_tag(RgMesh *m, const char *name)
{
    unsigned i;

    if (strlen(name) >= RG_TAG_LEN) {
        m->failed = true;
        return RG_NO_TAG;
    }
    for (i = 0; i < m->nNames; i++)
        if (strcmp(m->names[i], name) == 0)
            return (uint16_t)i;
    if (m->nNames >= 0xFFFEu) {
        m->failed = true;
        return RG_NO_TAG;
    }
    if (m->nNames == m->capNames) {
        unsigned cap = m->capNames ? m->capNames * 2u : 32u;
        char (*nn)[RG_TAG_LEN] = (char (*)[RG_TAG_LEN])realloc(m->names, (size_t)cap * RG_TAG_LEN);

        if (nn == NULL) {
            m->failed = true;
            return RG_NO_TAG;
        }
        m->names = nn;
        m->capNames = cap;
    }
    memcpy(m->names[m->nNames], name, strlen(name) + 1u);
    return (uint16_t)m->nNames++;
}

void rg_mesh_tri(RgMesh *m, const RgVtx *a, const RgVtx *b, const RgVtx *c, double shade, uint16_t tag)
{
    RgTri *t;

    if (m->failed || tag == RG_NO_TAG)
        return;
    if (m->n == m->cap) {
        unsigned cap = m->cap ? m->cap * 2u : 256u;
        RgTri *nt = (RgTri *)realloc(m->t, (size_t)cap * sizeof(RgTri));

        if (nt == NULL) {
            m->failed = true;
            return;
        }
        m->t = nt;
        m->cap = cap;
    }
    t = &m->t[m->n++];
    t->p[0] = *a; t->p[1] = *b; t->p[2] = *c;
    t->shade = shade;
    t->tag = tag;
    t->flags = tag_flags(m->names[tag]);
}

void rg_mesh_poly(RgMesh *m, const RgVtx *pts, unsigned n, double shade, uint16_t tag)
{
    unsigned i;

    for (i = 1; i + 1 < n; i++)
        rg_mesh_tri(m, &pts[0], &pts[i], &pts[i + 1], shade, tag);
}

/* ---- kernel -------------------------------------------------------------------------------- */

unsigned rg_clip(const RgPt *in, unsigned n, unsigned nc, unsigned axis, double val, bool keep, RgPt *out)
{
    unsigned i, k, o = 0;

    if (n > MAXV / 2u || nc > 5u || axis >= nc)
        return 0;
    for (i = 0; i < n; i++) {
        const RgPt *a = &in[i], *b = &in[(i + 1) % n];
        bool ina = keep ? (a->c[axis] >= val - RG_EPS) : (a->c[axis] <= val + RG_EPS);
        bool inb = keep ? (b->c[axis] >= val - RG_EPS) : (b->c[axis] <= val + RG_EPS);

        if (ina)
            out[o++] = *a;
        if (ina != inb) {
            double t = (val - a->c[axis]) / (b->c[axis] - a->c[axis]);

            memset(&out[o], 0, sizeof(RgPt));
            for (k = 0; k < nc; k++)
                out[o].c[k] = a->c[k] + (b->c[k] - a->c[k]) * t;
            o++;
        }
    }
    return o;
}

static double poly_area(const double (*zy)[2], unsigned n)
{
    double terms[16];
    unsigned i;

    for (i = 0; i < n; i++) {
        const double *p = zy[i], *q = zy[(i + 1) % n];

        terms[i] = p[0] * q[1] - q[0] * p[1];
    }
    return rg_pysum(terms, n);
}

bool rg_polygon_ccw(const double (*zy)[2], unsigned n)
{
    return n >= 3 && n <= 16 && poly_area(zy, n) > 0;
}

static double cross2(const double *o, const double *a, const double *b)
{
    return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0]);
}

unsigned rg_triangulate(const double (*zy)[2], unsigned n, unsigned (*tri)[3])
{
    unsigned pts[16], np = n, nt = 0, guard = 0, i;
    double sign;

    if (n < 3 || n > 16)
        return 0;
    for (i = 0; i < n; i++)
        pts[i] = i;
    sign = poly_area(zy, n) > 0 ? 1.0 : -1.0;
    while (np > 3 && guard < 10000) {
        bool found = false;
        unsigned k;

        guard++;
        for (k = 0; k < np && !found; k++) {
            unsigned i0 = pts[(k + np - 1) % np], i1 = pts[k], i2 = pts[(k + 1) % np], j;
            bool inside = false;

            if (cross2(zy[i0], zy[i1], zy[i2]) * sign <= RG_EPS)
                continue;
            for (j = 0; j < np; j++) {
                unsigned q = pts[j];

                if (q == i0 || q == i1 || q == i2)
                    continue;
                if (cross2(zy[i0], zy[i1], zy[q]) * sign > RG_EPS && cross2(zy[i1], zy[i2], zy[q]) * sign > RG_EPS
                    && cross2(zy[i2], zy[i0], zy[q]) * sign > RG_EPS) {
                    inside = true;
                    break;
                }
            }
            if (inside)
                continue;
            tri[nt][0] = i0; tri[nt][1] = i1; tri[nt][2] = i2;
            nt++;
            for (j = k; j + 1 < np; j++)
                pts[j] = pts[j + 1];
            np--;
            found = true;
        }
        if (!found)
            break;
    }
    if (np == 3) {
        tri[nt][0] = pts[0]; tri[nt][1] = pts[1]; tri[nt][2] = pts[2];
        nt++;
    }
    return nt;
}

RgTile rg_tile(double u0, double v0, double u1, double v1)
{
    RgTile t;

    memset(&t, 0, sizeof(t));
    t.rect[0] = u0; t.rect[1] = v0; t.rect[2] = u1; t.rect[3] = v1;
    return t;
}

RgTile rg_tile_top(double u0, double v0, double u1, double v1, double top)
{
    RgTile t = rg_tile(u0, v0, u1, v1);

    t.hasTop = true;
    t.top = top;
    return t;
}

RgProj rg_proj(void)
{
    RgProj p;

    memset(&p, 0, sizeof(p));
    return p;
}

RgProj rg_proj_rows(double lo, double hi)
{
    RgProj p;

    p.hasLo = p.hasHi = true;
    p.lo = lo;
    p.hi = hi;
    return p;
}

RgStrip rg_strip_fin(RgStrip s)
{
    if (s.hasRepeat && !s.hasStart) {
        s.hasStart = true;
        s.start = s.repeat[1] - 1;
    }
    return s;
}

RgBand rg_band(double y0, double y1, RgTile tile, double z0)
{
    RgBand b;

    memset(&b, 0, sizeof(b));
    b.y0 = y0; b.y1 = y1; b.tile = tile; b.z0 = z0;
    b.length = 1e9;
    return b;
}

void rg_band_z1(RgBand *b, double z1)
{
    b->length = b->z0 - z1;
}

bool rg_tile_pieces(const RgPt *poly2d, unsigned n, const RgTile *tile, RgPieceFn cb, void *ctx)
{
    double u0 = tile->rect[0], v0 = tile->rect[1], pw = tile->rect[2] - u0, ph = tile->rect[3] - v0;
    double smin, smax, tmin, tmax, fi0, fi1, fj0, fj1;
    long i, j, i0, i1, j0, j1;
    unsigned k;

    if (n < 3 || n > 8 || !(pw > 0.0) || !(ph > 0.0))
        return false;
    smin = smax = poly2d[0].c[0];
    tmin = tmax = poly2d[0].c[1];
    for (k = 1; k < n; k++) {
        if (poly2d[k].c[0] < smin) smin = poly2d[k].c[0];
        if (poly2d[k].c[0] > smax) smax = poly2d[k].c[0];
        if (poly2d[k].c[1] < tmin) tmin = poly2d[k].c[1];
        if (poly2d[k].c[1] > tmax) tmax = poly2d[k].c[1];
    }
    fi0 = floor((smin - tile->s0) / pw + RG_EPS);
    fi1 = ceil((smax - tile->s0) / pw - RG_EPS);
    fj0 = floor(tmin / ph + RG_EPS);
    fj1 = ceil(tmax / ph - RG_EPS);
    if (!(fabs(fi0) < 1e6 && fabs(fi1) < 1e6 && fabs(fj0) < 1e6 && fabs(fj1) < 1e6)
        || (fi1 - fi0) * (fj1 - fj0) > 1e6)
        return false;
    i0 = (long)fi0; i1 = (long)fi1; j0 = (long)fj0; j1 = (long)fj1;
    for (i = i0; i < i1; i++) {
        for (j = j0; j < j1; j++) {
            RgPt a[MAXV], b[MAXV], res[MAXV];
            double sa = tile->s0 + (double)i * pw, sb = tile->s0 + (double)(i + 1) * pw;
            double ta = (double)j * ph, tb = (double)(j + 1) * ph;
            unsigned np;

            memcpy(a, poly2d, n * sizeof(RgPt));
            np = rg_clip(a, n, 2, 0, sa, true, b);
            if (np >= 3)
                np = rg_clip(b, np, 2, 0, sb, false, a);
            else
                memcpy(a, b, np * sizeof(RgPt));
            if (np >= 3)
                np = rg_clip(a, np, 2, 1, ta, true, b);
            else
                memcpy(b, a, np * sizeof(RgPt));
            if (np >= 3)
                np = rg_clip(b, np, 2, 1, tb, false, a);
            else
                memcpy(a, b, np * sizeof(RgPt));
            if (np < 3)
                continue;
            for (k = 0; k < np; k++) {
                double fs = a[k].c[0] - sa;

                if (tile->flip)
                    fs = pw - fs;
                memset(&res[k], 0, sizeof(RgPt));
                res[k].c[0] = a[k].c[0];
                res[k].c[1] = a[k].c[1];
                res[k].c[2] = u0 + fs;
                res[k].c[3] = v0 + (a[k].c[1] - ta);
            }
            cb(ctx, res, np);
        }
    }
    return true;
}

void rg_unit3(const double v[3], double out[3])
{
    double sq[3], n;
    unsigned k;

    for (k = 0; k < 3; k++)
        sq[k] = v[k] * v[k];
    n = sqrt(rg_pysum(sq, 3));
    for (k = 0; k < 3; k++)
        out[k] = v[k] / n;
}

/* ---- Strip --------------------------------------------------------------------------------- */

typedef struct { double t0, t1, v0; bool constant; } Seg;

/* The segments of a strip without its tail (vb:560-590). */
static unsigned segs_core(const RgStrip *s, double total, Seg *out)
{
    double a = s->fixed[0], b = s->fixed[1], t = 0.0;
    unsigned n = 0, guard = 0;

    if (b > a) {
        out[n].t0 = 0.0; out[n].t1 = total < b - a ? total : b - a; out[n].v0 = b; out[n].constant = false;
        n++;
        t = b - a;
    }
    if (t >= total - RG_EPS)
        return n;
    if (!s->hasRepeat) {
        out[n].t0 = t; out[n].t1 = total; out[n].v0 = a + 0.5; out[n].constant = true;
        return n + 1;
    }
    {
        double c = s->repeat[0], d = s->repeat[1], first = s->start + 1 - c;

        out[n].t0 = t; out[n].t1 = total < t + first ? total : t + first; out[n].v0 = s->start + 1; out[n].constant = false;
        n++;
        t += first;
        while (t < total - RG_EPS && n < SEG_MAX && guard++ < SEG_MAX) {
            out[n].t0 = t; out[n].t1 = total < t + (d - c) ? total : t + (d - c); out[n].v0 = d; out[n].constant = false;
            n++;
            t += d - c;
        }
    }
    return n;
}

static unsigned strip_segments(const RgStrip *s, double total, Seg *out)
{
    if (s->hasTail) {
        double length = s->tail[1] - s->tail[0];

        if (total > length + (s->fixed[1] - s->fixed[0])) {
            RgStrip body = *s;
            unsigned n;

            body.hasTail = false;
            n = segs_core(&body, total - length, out);
            if (n < SEG_MAX) {
                out[n].t0 = total - length; out[n].t1 = total; out[n].v0 = s->tail[1]; out[n].constant = false;
                n++;
            }
            return n;
        }
    }
    return segs_core(s, total, out);
}

typedef struct { double s0, s1, du; } UPiece;

static unsigned u_pieces(const RgStrip *s, double s0, double s1, UPiece *out)
{
    unsigned n = 0, guard = 0;
    double lo, hi, period;
    long k;

    if (!s->hasWrap) {
        out[0].s0 = s0; out[0].s1 = s1; out[0].du = s->offset;
        return 1;
    }
    lo = s->wrap[0];
    hi = s->wrap[1];
    period = hi - lo;
    if (!(period > 0.0))
        return 0;
    k = (long)floor((s0 - lo) / period + RG_EPS);
    while (lo + (double)k * period < s1 - RG_EPS && guard++ < UP_MAX) {
        double a = s0 > lo + (double)k * period ? s0 : lo + (double)k * period;
        double b = s1 < lo + (double)(k + 1) * period ? s1 : lo + (double)(k + 1) * period;

        if (b > a + RG_EPS && n < UP_MAX) {
            out[n].s0 = a; out[n].s1 = b; out[n].du = (double)(-k) * period;
            n++;
        }
        k++;
    }
    return n;
}

void rg_strip_face(RgMesh *m, const double (*pts)[3], unsigned n, const double origin[3], const double sdir[3],
                   const double tdir[3], const RgStrip *strip, double shade, const char *tag)
{
    RgPt loc[8];
    Seg segs[SEG_MAX];
    UPiece ups[UP_MAX];
    double dens, tmax, smin, smax, fixedLen;
    unsigned i, ns, nu, a, b;
    char clampTag[RG_TAG_LEN];
    uint16_t plain, clamped;

    if (n < 3 || n > 8) {
        m->failed = true;
        return;
    }
    {
        double h[2] = {tdir[0], tdir[2]};

        dens = fabs(tdir[1]) + hypot(h[0], h[1]);
    }
    for (i = 0; i < n; i++) {
        double d[3], ps[3], pt[3];
        unsigned k;

        for (k = 0; k < 3; k++) {
            d[k] = pts[i][k] - origin[k];
            ps[k] = d[k] * sdir[k];
            pt[k] = d[k] * tdir[k];
        }
        loc[i].c[0] = rg_pysum(ps, 3);
        loc[i].c[1] = rg_pysum(pt, 3) * dens;
        loc[i].c[2] = pts[i][0]; loc[i].c[3] = pts[i][1]; loc[i].c[4] = pts[i][2];
    }
    tmax = loc[0].c[1];
    smin = smax = loc[0].c[0];
    for (i = 1; i < n; i++) {
        if (loc[i].c[1] > tmax) tmax = loc[i].c[1];
        if (loc[i].c[0] < smin) smin = loc[i].c[0];
        if (loc[i].c[0] > smax) smax = loc[i].c[0];
    }
    fixedLen = strip->fixed[1] - strip->fixed[0];
    ns = strip_segments(strip, tmax, segs);
    tag_cat(clampTag, tag, "~clamp");
    plain = rg_mesh_tag(m, tag);
    clamped = rg_mesh_tag(m, clampTag);
    for (a = 0; a < ns; a++) {
        nu = u_pieces(strip, smin, smax, ups);
        for (b = 0; b < nu; b++) {
            RgPt p1[MAXV], p2[MAXV];
            RgVtx out[MAXV];
            double du = ups[b].du;
            unsigned np, k;

            if (strip->hasRepeatOffset && segs[a].t0 >= fixedLen - RG_EPS)
                du = strip->repeatOffset;
            np = rg_clip(loc, n, 5, 1, segs[a].t0, true, p1);
            if (np >= 3) { np = rg_clip(p1, np, 5, 1, segs[a].t1, false, p2); memcpy(p1, p2, np * sizeof(RgPt)); }
            if (np >= 3) { np = rg_clip(p1, np, 5, 0, ups[b].s0, true, p2); memcpy(p1, p2, np * sizeof(RgPt)); }
            if (np >= 3) { np = rg_clip(p1, np, 5, 0, ups[b].s1, false, p2); memcpy(p1, p2, np * sizeof(RgPt)); }
            if (np < 3)
                continue;
            for (k = 0; k < np; k++) {
                double v = segs[a].constant ? segs[a].v0 : segs[a].v0 - (p1[k].c[1] - segs[a].t0);

                out[k] = V(p1[k].c[2], p1[k].c[3], p1[k].c[4], p1[k].c[0] + du, v);
            }
            rg_mesh_poly(m, out, np, shade, segs[a].constant ? clamped : plain);
        }
    }
}

/* ---- Prism --------------------------------------------------------------------------------- */

typedef struct { RgMesh *m; double shade; uint16_t tag; double upper[2], lower[2], length; } TileCtx;

static void prism_tile_cb(void *vc, const RgPt *p, unsigned n)
{
    TileCtx *c = (TileCtx *)vc;
    RgVtx out[MAXV];
    unsigned k;

    for (k = 0; k < n; k++) {
        double f = c->length > RG_EPS ? p[k].c[1] / c->length : 0.0;
        double z = c->upper[0] + (c->lower[0] - c->upper[0]) * f;
        double y = c->upper[1] + (c->lower[1] - c->upper[1]) * f;

        out[k] = V(p[k].c[0], y, z, p[k].c[2], p[k].c[3]);
    }
    rg_mesh_poly(c->m, out, n, c->shade, c->tag);
}

typedef struct { RgMesh *m; double x, top, z0, shade; uint16_t tag; } CapCtx;

static void cap_cb(void *vc, const RgPt *p, unsigned n)
{
    CapCtx *c = (CapCtx *)vc;
    RgVtx out[MAXV];
    unsigned k;

    for (k = 0; k < n; k++)
        out[k] = V(c->x, c->top - p[k].c[1], c->z0 - p[k].c[0], p[k].c[2], p[k].c[3]);
    rg_mesh_poly(c->m, out, n, c->shade, c->tag);
}

static void emit_cap_piece(RgMesh *m, const double (*piece)[2], unsigned np, const RgBand *band, double x, bool west,
                           double shade, uint16_t tag)
{
    RgTile tiles[3];
    double sa[3], sb[3];
    unsigned nt = 0, t;
    double top = band->tile.hasTop ? band->tile.top : band->y1;
    double sFront = 0.0, sBack = 1e9, bw = 0.0;

    if (band->hasFront) {
        double fw = band->front.rect[2] - band->front.rect[0];

        tiles[nt] = band->front; sa[nt] = 0.0; sb[nt] = fw; nt++;
        sFront = fw;
    }
    if (band->hasBack) {
        bw = band->back.rect[2] - band->back.rect[0];
        sBack = band->length - bw;
    }
    tiles[nt] = band->tile; sa[nt] = sFront; sb[nt] = sBack; nt++;
    if (band->hasBack) {
        tiles[nt] = band->back; sa[nt] = sBack; sb[nt] = sBack + bw; nt++;
    }
    for (t = 0; t < nt; t++) {
        RgPt pts[MAXV], c1[MAXV], c2[MAXV];
        RgTile shifted;
        CapCtx cc;
        unsigned k, cnt = np;

        for (k = 0; k < np; k++) {
            memset(&pts[k], 0, sizeof(RgPt));
            pts[k].c[0] = band->z0 - piece[k][0];
            pts[k].c[1] = top - piece[k][1];
        }
        cnt = rg_clip(pts, cnt, 2, 0, sa[t], true, c1);
        if (cnt >= 3) cnt = rg_clip(c1, cnt, 2, 0, sb[t], false, c2); else memcpy(c2, c1, cnt * sizeof(RgPt));
        if (cnt < 3)
            continue;
        shifted = rg_tile(tiles[t].rect[0], tiles[t].rect[1], tiles[t].rect[2], tiles[t].rect[3]);
        shifted.s0 = sa[t];
        shifted.flip = tiles[t].flip != west;
        cc.m = m; cc.x = x; cc.top = top; cc.z0 = band->z0; cc.shade = shade; cc.tag = tag;
        if (!rg_tile_pieces(c2, cnt, &shifted, cap_cb, &cc))
            m->failed = true;
    }
}

static bool emit_prism(const RgPrism *pr, const char *name, RgMesh *m)
{
    const double (*poly)[2] = pr->poly;
    unsigned n = pr->nPoly, i;
    double x0 = pr->x0, x1 = pr->x1;

    if (n < 3 || n > RG_PRISM_PTS || !rg_polygon_ccw(poly, n))
        return false;
    for (i = 0; i < n; i++) {
        const double *a, *b;
        double dz, dy, len, nz, ny, facing, shade;
        const RgEdgeMat *mat;
        RgEdgeMat dflt;
        char tg[RG_TAG_LEN];

        if (pr->skip & (1u << i))
            continue;
        a = poly[i];
        b = poly[(i + 1) % n];
        dz = b[0] - a[0];
        dy = b[1] - a[1];
        len = hypot(dz, dy);
        nz = dy / len;
        ny = -dz / len;
        mat = &pr->edges[i];
        facing = nz + ny;
        if (mat->kind == RG_EM_NONE) {
            if (ny < -0.5 || facing <= RG_EPS)
                continue;
            memset(&dflt, 0, sizeof(dflt));
            dflt.kind = RG_EM_PROJ;
            dflt.proj = rg_proj();
            mat = &dflt;
        }
        shade = facing > RG_EPS ? RG_SHADE_ART : RG_SHADE_BACK;
        tag_edge(tg, name, ".e", i);
        if (mat->kind == RG_EM_PROJ) {
            double cuts[4] = {0.0, 1.0, 0.0, 0.0};
            unsigned nc = 2, k, j;
            double va = a[0] - a[1], vb = b[0] - b[1];
            char ctg[RG_TAG_LEN];
            uint16_t plain, clamped;

            for (k = 0; k < 2; k++) {
                bool has = k == 0 ? mat->proj.hasLo : mat->proj.hasHi;
                double lim = k == 0 ? mat->proj.lo : mat->proj.hi;

                if (has && fabs(vb - va) > RG_EPS) {
                    double t = (lim - va) / (vb - va);

                    if (RG_EPS < t && t < 1 - RG_EPS)
                        cuts[nc++] = t;
                }
            }
            for (k = 1; k < nc; k++) {          /* insertion sort: tiny, ties irrelevant for floats */
                double key = cuts[k];

                for (j = k; j > 0 && cuts[j - 1] > key; j--)
                    cuts[j] = cuts[j - 1];
                cuts[j] = key;
            }
            tag_cat(ctg, tg, "~clamp");
            plain = rg_mesh_tag(m, tg);
            clamped = rg_mesh_tag(m, ctg);
            for (k = 0; k + 1 < nc; k++) {
                double ta = cuts[k], tb = cuts[k + 1];
                double pa[2], pb[2], midv;
                bool clampLo, clampHi;
                RgVtx quad[4];
                static const int xs[4] = {0, 1, 1, 0}, ps[4] = {0, 0, 1, 1};

                pa[0] = a[0] + (b[0] - a[0]) * ta;  pa[1] = a[1] + (b[1] - a[1]) * ta;
                pb[0] = a[0] + (b[0] - a[0]) * tb;  pb[1] = a[1] + (b[1] - a[1]) * tb;
                midv = (pa[0] - pa[1] + pb[0] - pb[1]) / 2;
                clampLo = mat->proj.hasLo && midv < mat->proj.lo;
                clampHi = mat->proj.hasHi && midv > mat->proj.hi;
                for (j = 0; j < 4; j++) {
                    double x = xs[j] ? x1 : x0;
                    const double *p = ps[j] ? pb : pa;
                    double v;

                    if (clampLo)
                        v = mat->proj.lo + 0.5;
                    else if (clampHi)
                        v = mat->proj.hi - 0.5;
                    else
                        v = p[0] - p[1];
                    quad[j] = V(x, p[1], p[0], x, v);
                }
                rg_mesh_poly(m, quad, 4, shade, clampLo || clampHi ? clamped : plain);
            }
        } else if (mat->kind == RG_EM_STRIP) {
            const double *start = mat->strip.fromEnd ? b : a, *end = mat->strip.fromEnd ? a : b;
            double tv[3] = {0.0, end[1] - start[1], end[0] - start[0]}, tdir[3];
            double pts[4][3] = {{x0, a[1], a[0]}, {x1, a[1], a[0]}, {x1, b[1], b[0]}, {x0, b[1], b[0]}};
            double origin[3] = {0.0, start[1], start[0]}, sdir[3] = {1.0, 0.0, 0.0};

            rg_unit3(tv, tdir);
            rg_strip_face(m, pts, 4, origin, sdir, tdir, &mat->strip, shade, tg);
        } else {
            TileCtx tc;
            RgPt face[4];
            double length = hypot(b[0] - a[0], b[1] - a[1]);
            const double *upper = a[1] >= b[1] ? a : b, *lower = a[1] >= b[1] ? b : a;

            memset(face, 0, sizeof(face));
            face[0].c[0] = x0; face[0].c[1] = 0.0;
            face[1].c[0] = x1; face[1].c[1] = 0.0;
            face[2].c[0] = x1; face[2].c[1] = length;
            face[3].c[0] = x0; face[3].c[1] = length;
            tc.m = m; tc.shade = shade; tc.tag = rg_mesh_tag(m, tg); tc.length = length;
            tc.upper[0] = upper[0]; tc.upper[1] = upper[1]; tc.lower[0] = lower[0]; tc.lower[1] = lower[1];
            if (!rg_tile_pieces(face, 4, &mat->tile, prism_tile_cb, &tc))
                return false;
        }
    }
    if (!pr->hasCaps)
        return !m->failed;
    {
        unsigned tris[16][3], nt = rg_triangulate(poly, n, tris), side, b, t;
        char tg[2][RG_TAG_LEN];

        tag_cat(tg[0], name, ".capw");
        tag_cat(tg[1], name, ".cape");
        for (side = 0; side < 2; side++) {
            bool west = side == 0;
            double x = west ? x0 : x1, shade = west ? RG_SHADE_WEST : RG_SHADE_EAST;
            uint16_t tag;

            if ((west && !pr->west) || (!west && !pr->east))
                continue;
            tag = rg_mesh_tag(m, tg[side]);
            for (b = 0; b < pr->nCaps; b++) {
                const RgBand *band = &pr->caps[b];

                for (t = 0; t < nt; t++) {
                    RgPt p1[MAXV], p2[MAXV], p3[MAXV];
                    double piece[MAXV][2];
                    unsigned k, cnt = 3;

                    for (k = 0; k < 3; k++) {
                        memset(&p1[k], 0, sizeof(RgPt));
                        p1[k].c[0] = poly[tris[t][k]][0];
                        p1[k].c[1] = poly[tris[t][k]][1];
                    }
                    cnt = rg_clip(p1, cnt, 2, 1, band->y0, true, p2);
                    if (cnt >= 3) cnt = rg_clip(p2, cnt, 2, 1, band->y1, false, p3); else memcpy(p3, p2, cnt * sizeof(RgPt));
                    if (cnt < 3)
                        continue;
                    for (k = 0; k < cnt; k++) {
                        piece[k][0] = p3[k].c[0];
                        piece[k][1] = p3[k].c[1];
                    }
                    emit_cap_piece(m, piece, cnt, band, x, west, shade, tag);
                }
            }
        }
    }
    return !m->failed;
}

/* ---- HipRoof ------------------------------------------------------------------------------- */

void rg_hip_init(RgHip *h)
{
    double cd = h->cap[1] - h->cap[0];

    h->ye = h->y0 + (h->fascia.fixed[1] - h->fascia.fixed[0]);
    h->zrf = (h->zf + h->zb) / 2.0 + cd / 2.0;
    h->zrb = h->zrf - cd;
    h->rise = tan(h->pitch * RG_DEG2RAD) * (h->zf - h->zrf);
    h->yr = h->ye + h->rise;
    h->yt = h->yr + (h->teeth[1] - h->teeth[0]);
}

void rg_hip_slope_point(const RgHip *h, double texels, double *z, double *y)
{
    double run = h->zf - h->zrf, length = hypot(run, h->rise);
    double t = texels / ((h->rise + run) / length);

    *z = h->zf - t * run / length;
    *y = h->ye + t * h->rise / length;
}

static void unit_of(double x, double y, double z, double out[3])
{
    double v[3] = {x, y, z};

    rg_unit3(v, out);
}

static bool emit_hip(const RgHip *h, const char *name, RgMesh *m)
{
    double x0 = h->x0, x1 = h->x1, zf = h->zf, zb = h->zb, y0 = h->y0, ye = h->ye;
    double xa = x0 + h->run, xb = x1 - h->run, zr = h->zrf, zq = h->zrb, yr = h->yr, yt = h->yt;
    double ex[3] = {1, 0, 0}, ez[3] = {0, 0, 1}, up[3] = {0.0, 1.0, 0.0}, back[3] = {0, 0, -1};
    double ts[3], tn[3], tw[3], te[3];
    char tg[RG_TAG_LEN];
    const RgStrip *fas = &h->fascia, *sl = &h->slope;
    double o[3];
    double fs[4][3] = {{x0, y0, zf}, {x1, y0, zf}, {x1, ye, zf}, {x0, ye, zf}};
    double fn[4][3] = {{x1, y0, zb}, {x0, y0, zb}, {x0, ye, zb}, {x1, ye, zb}};
    double fw[4][3] = {{x0, y0, zb}, {x0, y0, zf}, {x0, ye, zf}, {x0, ye, zb}};
    double fe[4][3] = {{x1, y0, zf}, {x1, y0, zb}, {x1, ye, zb}, {x1, ye, zf}};

    o[0] = 0; o[1] = y0; o[2] = zf;
    tag_cat(tg, name, ".fascia_s"); rg_strip_face(m, fs, 4, o, ex, up, fas, RG_SHADE_ART, tg);
    o[0] = 0; o[1] = y0; o[2] = zb;
    tag_cat(tg, name, ".fascia_n"); rg_strip_face(m, fn, 4, o, ex, up, fas, RG_SHADE_BACK, tg);
    o[0] = x0; o[1] = y0; o[2] = 0;
    tag_cat(tg, name, ".fascia_w"); rg_strip_face(m, fw, 4, o, ez, up, fas, RG_SHADE_WEST, tg);
    o[0] = x1; o[1] = y0; o[2] = 0;
    tag_cat(tg, name, ".fascia_e"); rg_strip_face(m, fe, 4, o, ez, up, fas, RG_SHADE_EAST, tg);
    unit_of(0, h->rise, -(zf - zr), ts);
    unit_of(0, h->rise, (zf - zr), tn);
    unit_of(h->run, h->rise, 0, tw);
    unit_of(-h->run, h->rise, 0, te);
    {
        double ss[4][3] = {{x0, ye, zf}, {x1, ye, zf}, {xb, yr, zr}, {xa, yr, zr}};
        double sn[4][3] = {{x1, ye, zb}, {x0, ye, zb}, {xa, yr, zq}, {xb, yr, zq}};
        double hw[4][3] = {{x0, ye, zb}, {x0, ye, zf}, {xa, yr, zr}, {xa, yr, zq}};
        double he[4][3] = {{x1, ye, zf}, {x1, ye, zb}, {xb, yr, zq}, {xb, yr, zr}};

        o[0] = 0; o[1] = ye; o[2] = zf;
        tag_cat(tg, name, ".slope_s"); rg_strip_face(m, ss, 4, o, ex, ts, sl, RG_SHADE_ART, tg);
        o[0] = 0; o[1] = ye; o[2] = zb;
        tag_cat(tg, name, ".slope_n"); rg_strip_face(m, sn, 4, o, ex, tn, sl, RG_SHADE_BACK, tg);
        o[0] = x0; o[1] = ye; o[2] = 0;
        tag_cat(tg, name, ".hip_w"); rg_strip_face(m, hw, 4, o, ez, tw, sl, RG_SHADE_WEST, tg);
        o[0] = x1; o[1] = ye; o[2] = 0;
        tag_cat(tg, name, ".hip_e"); rg_strip_face(m, he, 4, o, ez, te, sl, RG_SHADE_EAST, tg);
    }
    if (!h->ridge)
        return !m->failed;
    {
        double lo = h->ridgeU[0], hi = h->ridgeU[1], mid = (xa + xb) / 2.0;
        double spA[2], spB[2], spOff[2];
        bool spHasOff[2];
        unsigned ns, k;

        if (h->hasRidgeWrap) {
            spA[0] = xa; spB[0] = xb; spHasOff[0] = false; spOff[0] = 0; ns = 1;
        } else {
            spA[0] = xa; spB[0] = mid; spOff[0] = lo - xa; spHasOff[0] = true;
            spA[1] = mid; spB[1] = xb; spOff[1] = hi - xb; spHasOff[1] = true;
            ns = 2;
        }
        for (k = 0; k < ns; k++) {
            RgStrip teeth, cap;
            double sa = spA[k], sb = spB[k];
            double ft[4][3] = {{sa, yr, zr}, {sb, yr, zr}, {sb, yt, zr}, {sa, yt, zr}};
            double fc[4][3] = {{sa, yt, zr}, {sb, yt, zr}, {sb, yt, zq}, {sa, yt, zq}};

            memset(&teeth, 0, sizeof(teeth));
            teeth.fixed[0] = h->teeth[0]; teeth.fixed[1] = h->teeth[1];
            cap = teeth;
            cap.fixed[0] = h->cap[0]; cap.fixed[1] = h->cap[1];
            if (!spHasOff[k]) {
                teeth.hasWrap = cap.hasWrap = true;
                teeth.wrap[0] = cap.wrap[0] = h->ridgeWrap[0];
                teeth.wrap[1] = cap.wrap[1] = h->ridgeWrap[1];
            } else {
                teeth.offset = cap.offset = spOff[k];
            }
            teeth = rg_strip_fin(teeth);
            cap = rg_strip_fin(cap);
            o[0] = 0; o[1] = yr; o[2] = zr;
            tag_cat(tg, name, ".teeth"); rg_strip_face(m, ft, 4, o, ex, up, &teeth, RG_SHADE_ART, tg);
            o[0] = 0; o[1] = yt; o[2] = zr;
            tag_cat(tg, name, ".cap"); rg_strip_face(m, fc, 4, o, ex, back, &cap, RG_SHADE_ART, tg);
        }
    }
    {
        double u0 = h->endTile.rect[0], v0 = h->endTile.rect[1];
        double w = zr - zq, hh = yt - yr;
        uint16_t tag;
        unsigned k;

        tag_cat(tg, name, ".ridge_end");
        tag = rg_mesh_tag(m, tg);
        for (k = 0; k < 2; k++) {
            double x = k == 0 ? xa : xb, shade = k == 0 ? RG_SHADE_WEST : RG_SHADE_EAST;
            bool flip = k == 1;
            double pz[4] = {zq, zr, zr, zq}, py[4] = {yr, yr, yt, yt};
            double us[4] = {u0, u0 + w, u0 + w, u0}, vs[4] = {v0 + hh, v0 + hh, v0, v0};
            RgVtx q[4];
            unsigned j;

            for (j = 0; j < 4; j++) {
                double u = us[j];

                if (flip)
                    u = u0 + w - (u - u0);
                q[j] = V(x, py[j], pz[j], u, vs[j]);
            }
            rg_mesh_poly(m, q, 4, shade, tag);
        }
    }
    return !m->failed;
}

/* ---- Frustum, Vault, Walls and the small parts --------------------------------------------- */

static void plan_normal(const double *p, const double *q, double *nx, double *nz)
{
    double dx = q[0] - p[0], dz = q[1] - p[1], n = hypot(dx, dz);

    *nx = -dz / n;
    *nz = dx / n;
}

static bool inset_plan(const double (*plan)[2], unsigned count, double d, double (*out)[2])
{
    double lp[RG_PLAN_PTS][2], ld[RG_PLAN_PTS][2];
    unsigned i;

    for (i = 0; i < count; i++) {
        const double *p = plan[i], *q = plan[(i + 1) % count];
        double nx, nz;

        plan_normal(p, q, &nx, &nz);
        lp[i][0] = p[0] - nx * d;  lp[i][1] = p[1] - nz * d;
        ld[i][0] = q[0] - p[0];    ld[i][1] = q[1] - p[1];
    }
    for (i = 0; i < count; i++) {
        unsigned prev = (i + count - 1) % count;
        const double *p1 = lp[prev], *d1 = ld[prev], *p2 = lp[i], *d2 = ld[i];
        double den = d1[0] * d2[1] - d1[1] * d2[0];
        double t = ((p2[0] - p1[0]) * d2[1] - (p2[1] - p1[1]) * d2[0]) / den;

        out[i][0] = p1[0] + d1[0] * t;
        out[i][1] = p1[1] + d1[1] * t;
    }
    return true;
}

static void proj_face(RgMesh *m, const double (*q)[3], unsigned n, double shade, uint16_t tag)
{
    RgVtx v[MAXV];
    unsigned i;

    for (i = 0; i < n; i++)
        v[i] = V(q[i][0], q[i][1], q[i][2], q[i][0], q[i][2] - q[i][1]);
    rg_mesh_poly(m, v, n, shade, tag);
}

static bool emit_frustum(const RgFrustum *f, const char *name, RgMesh *m)
{
    const double (*plan)[2] = f->plan;
    unsigned count = f->nPlan, i;
    double top[RG_PLAN_PTS][2], y0 = -1.0, y1 = f->wallTop, y2, zfront;
    char tg[RG_TAG_LEN];

    if (count < 3 || count > RG_PLAN_PTS)
        return false;
    y2 = y1 + f->bandRise;
    inset_plan(plan, count, f->bandRise, top);
    for (i = 0; i < count; i++) {
        const double *p = plan[i], *q = plan[(i + 1) % count], *pi = top[i], *qi = top[(i + 1) % count];
        double nx, nz, shade, sdirv[3], sdir[3], o[3];
        bool drawn, diag;
        double wall[4][3] = {{p[0], y0, p[1]}, {q[0], y0, q[1]}, {q[0], y1, q[1]}, {p[0], y1, p[1]}};
        double band[4][3] = {{p[0], y1, p[1]}, {q[0], y1, q[1]}, {qi[0], y2, qi[1]}, {pi[0], y2, pi[1]}};

        plan_normal(p, q, &nx, &nz);
        drawn = nz > 0.1;
        diag = fabs(nx) > 1e-6;
        shade = drawn ? RG_SHADE_ART : (nx < -0.3 ? RG_SHADE_WEST : nx > 0.3 ? RG_SHADE_EAST : RG_SHADE_BACK);
        sdirv[0] = q[0] - p[0]; sdirv[1] = 0.0; sdirv[2] = q[1] - p[1];
        rg_unit3(sdirv, sdir);
        if (drawn) {
            tag_cat(tg, name, diag ? ".wall~proj" : ".wall");
            proj_face(m, wall, 4, RG_SHADE_ART, rg_mesh_tag(m, tg));
            tag_cat(tg, name, diag ? ".band~proj" : ".band");
            proj_face(m, band, 4, RG_SHADE_ART, rg_mesh_tag(m, tg));
        } else {
            double tv[3] = {-nx, 1.0, -nz}, tdir[3], uy[3] = {0.0, 1.0, 0.0};

            o[0] = p[0]; o[1] = y0; o[2] = p[1];
            tag_cat(tg, name, ".wall");
            rg_strip_face(m, wall, 4, o, sdir, uy, &f->wallSide, shade, tg);
            rg_unit3(tv, tdir);
            o[0] = p[0]; o[1] = y1; o[2] = p[1];
            tag_cat(tg, name, ".band");
            rg_strip_face(m, band, 4, o, sdir, tdir, &f->bandSide, shade, tg);
        }
    }
    zfront = top[0][1];
    for (i = 1; i < count; i++)
        if (top[i][1] > zfront)
            zfront = top[i][1];
    {
        double q[RG_PLAN_PTS][3], o[3] = {0.0, y2, zfront}, ex[3] = {1.0, 0.0, 0.0}, td[3] = {0.0, 0.0, -1.0};

        for (i = 0; i < count; i++) {
            q[i][0] = top[count - 1 - i][0];
            q[i][1] = y2;
            q[i][2] = top[count - 1 - i][1];
        }
        tag_cat(tg, name, ".top");
        if (count > 8) {
            m->failed = true;
            return false;
        }
        rg_strip_face(m, q, count, o, ex, td, &f->top, RG_SHADE_ART, tg);
    }
    return !m->failed;
}

static bool emit_vault(const RgVault *v, const char *name, RgMesh *m)
{
    unsigned i;
    char tg[RG_TAG_LEN];
    uint16_t top, front, back;

    tag_cat(tg, name, ".top~proj"); top = rg_mesh_tag(m, tg);
    tag_cat(tg, name, ".front"); front = rg_mesh_tag(m, tg);
    tag_cat(tg, name, ".back"); back = rg_mesh_tag(m, tg);
    for (i = 0; i + 1 < v->nProfile; i++) {
        double xa = v->profile[i][0], ha = v->profile[i][1], xb = v->profile[i + 1][0], hb = v->profile[i + 1][1];
        double zf = v->zf, zb = v->zb, y0 = v->y0;
        double t[4][3] = {{xa, y0 + ha, zf}, {xb, y0 + hb, zf}, {xb, y0 + hb, zb}, {xa, y0 + ha, zb}};
        double fr[4][3] = {{xa, y0, zf}, {xb, y0, zf}, {xb, y0 + hb, zf}, {xa, y0 + ha, zf}};
        double bk[4][3] = {{xb, y0, zb}, {xa, y0, zb}, {xa, y0 + ha, zb}, {xb, y0 + hb, zb}};
        RgVtx q[4];
        unsigned k;

        proj_face(m, t, 4, RG_SHADE_ART, top);
        if (ha <= 0 && hb <= 0)
            continue;
        for (k = 0; k < 4; k++) q[k] = V(fr[k][0], fr[k][1], fr[k][2], fr[k][0], zf - fr[k][1]);
        rg_mesh_poly(m, q, 4, RG_SHADE_ART, front);
        for (k = 0; k < 4; k++) q[k] = V(bk[k][0], bk[k][1], bk[k][2], bk[k][0], zf - bk[k][1]);
        rg_mesh_poly(m, q, 4, RG_SHADE_BACK, back);
    }
    return !m->failed;
}

static bool emit_walls(const RgWalls *w, const char *name, RgMesh *m)
{
    unsigned i;
    char tg[RG_TAG_LEN];
    uint16_t wall, wallp;

    tag_cat(tg, name, ".wall"); wall = rg_mesh_tag(m, tg);
    tag_cat(tg, name, ".wall~proj"); wallp = rg_mesh_tag(m, tg);
    for (i = 0; i + 1 < w->nPlan; i++) {
        const double *p = w->plan[i], *q = w->plan[i + 1];
        double quad[4][3] = {{p[0], w->y0, p[1]}, {q[0], w->y0, q[1]}, {q[0], w->y1, q[1]}, {p[0], w->y1, p[1]}};

        proj_face(m, quad, 4, RG_SHADE_ART, fabs(q[1] - p[1]) > 1e-6 ? wallp : wall);
    }
    return !m->failed;
}

static bool emit_card(const RgCard *c, const char *name, RgMesh *m)
{
    const RgImage *im = c->art;
    int W, H, u, v;
    int cmin = -1, cmax = -1, rmin = -1, rmax = -1;
    double u0, u1, v0, f, h;
    char tg[RG_TAG_LEN];
    double q[4][3];
    RgVtx vt[4];
    unsigned k;

    if (im == NULL)
        return false;
    W = im->w; H = im->h;
    for (v = 0; v < H; v++)
        for (u = 0; u < W; u++)
            if (im->px[((size_t)v * (size_t)W + (size_t)u) * 4u + 3u] >= 128) {
                if (cmin < 0 || u < cmin) cmin = u;
                if (u > cmax) cmax = u;
                if (rmin < 0 || v < rmin) rmin = v;
                if (v > rmax) rmax = v;
            }
    (void)rmax;
    if (cmin < 0)
        return true;
    u0 = (double)cmin; u1 = (double)(cmax + 1);
    v0 = (double)rmin; f = c->foot;
    h = f - v0;
    q[0][0] = u0; q[0][1] = -1.0; q[0][2] = f;
    q[1][0] = u1; q[1][1] = -1.0; q[1][2] = f;
    q[2][0] = u1; q[2][1] = h;    q[2][2] = f;
    q[3][0] = u0; q[3][1] = h;    q[3][2] = f;
    for (k = 0; k < 4; k++)
        vt[k] = V(q[k][0], q[k][1], q[k][2], q[k][0], q[k][2] - q[k][1] + c->voff);
    tag_cat(tg, name, ".card");
    rg_mesh_poly(m, vt, 4, RG_SHADE_ART, rg_mesh_tag(m, tg));
    return !m->failed;
}

static bool emit_facet(const RgFacet *f, const char *name, RgMesh *m)
{
    double q[4][3] = {{f->a[0], -1.0, f->a[1]}, {f->b[0], -1.0, f->b[1]}, {f->b[0], f->hb, f->b[1]},
                      {f->a[0], f->ha, f->a[1]}};
    char tg[RG_TAG_LEN];

    tag_cat(tg, name, ".facet~proj");
    proj_face(m, q, 4, RG_SHADE_ART, rg_mesh_tag(m, tg));
    return !m->failed;
}

typedef struct { RgMesh *m; double xa, za, dx, dz, y1; uint16_t tag; } PlainCtx;

static void plain_cb(void *vc, const RgPt *p, unsigned n)
{
    PlainCtx *c = (PlainCtx *)vc;
    RgVtx out[MAXV];
    unsigned k;

    for (k = 0; k < n; k++)
        out[k] = V(c->xa + c->dx * p[k].c[0], c->y1 - p[k].c[1], c->za + c->dz * p[k].c[0], p[k].c[2], p[k].c[3]);
    rg_mesh_poly(c->m, out, n, RG_SHADE_WEST, c->tag);
}

static bool emit_plain(const RgPlainWall *w, const char *name, RgMesh *m)
{
    double length = hypot(w->b[0] - w->a[0], w->b[1] - w->a[1]);
    double dx = (w->b[0] - w->a[0]) / length, dz = (w->b[1] - w->a[1]) / length;
    RgPt face[4];
    PlainCtx c;
    char tg[RG_TAG_LEN];

    memset(face, 0, sizeof(face));
    face[0].c[0] = 0.0;    face[0].c[1] = 0.0;
    face[1].c[0] = length; face[1].c[1] = 0.0;
    face[2].c[0] = length; face[2].c[1] = w->y1 - w->y0;
    face[3].c[0] = 0.0;    face[3].c[1] = w->y1 - w->y0;
    tag_cat(tg, name, ".plain");
    c.m = m; c.xa = w->a[0]; c.za = w->a[1]; c.dx = dx; c.dz = dz; c.y1 = w->y1; c.tag = rg_mesh_tag(m, tg);
    if (!rg_tile_pieces(face, 4, &w->tile, plain_cb, &c))
        return false;
    return !m->failed;
}

static bool emit_decal(const RgDecal *d, const char *name, RgMesh *m)
{
    double y = d->lift, o = d->vOffset;
    unsigned c;
    char tg[RG_TAG_LEN];
    uint16_t tag;

    tag_cat(tg, name, ".decal");
    tag = rg_mesh_tag(m, tg);
    for (c = 0; c < d->nCells; c++) {
        double i = (double)d->cells[c][0], j = (double)d->cells[c][1];
        double x0 = i * 16.0, z0 = j * 16.0, x1 = i * 16.0 + 16.0, z1 = j * 16.0 + 16.0;
        double qx[4] = {x0, x1, x1, x0}, qz[4] = {z0, z0, z1, z1};
        RgVtx v[4];
        unsigned k;

        for (k = 0; k < 4; k++)
            v[k] = V(qx[k], y, qz[k] + y, qx[k], qz[k] + o);
        rg_mesh_poly(m, v, 4, RG_SHADE_ART, tag);
    }
    return !m->failed;
}

static bool emit_cylinder(const RgCylinder *c, const char *name, RgMesh *m)
{
    double ring[64][2], y0 = c->y0, y1 = c->y1, u0 = c->backTile.rect[0], u1 = c->backTile.rect[2], v1 = c->backTile.rect[3];
    unsigned k, S = c->sides;
    char tg[RG_TAG_LEN];
    uint16_t topT, sideP, side;
    RgVtx top[64];

    if (S < 3 || S > 64)
        return false;
    for (k = 0; k < S; k++) {
        ring[k][0] = c->cx + c->rx * cos(2 * RG_PI * (double)k / (double)S);
        ring[k][1] = c->cz + c->rz * sin(2 * RG_PI * (double)k / (double)S);
    }
    tag_cat(tg, name, ".top~proj"); topT = rg_mesh_tag(m, tg);
    tag_cat(tg, name, ".side~proj"); sideP = rg_mesh_tag(m, tg);
    tag_cat(tg, name, ".side"); side = rg_mesh_tag(m, tg);
    for (k = 0; k < S; k++)
        top[k] = V(ring[k][0], y1, ring[k][1], ring[k][0], ring[k][1] - y1);
    rg_mesh_poly(m, top, S, RG_SHADE_ART, topT);
    for (k = 0; k < S; k++) {
        double xa = ring[k][0], za = ring[k][1], xb = ring[(k + 1) % S][0], zb = ring[(k + 1) % S][1];
        double nx = zb - za, nz = -(xb - xa);
        double qx[4] = {xa, xb, xb, xa}, qy[4] = {y0, y0, y1, y1}, qz[4] = {za, zb, zb, za};
        RgVtx q[4];
        unsigned j;

        if (nx * ((xa + xb) / 2 - c->cx) + nz * ((za + zb) / 2 - c->cz) < 0) {
            nx = -nx;
            nz = -nz;
        }
        nz /= hypot(nx, nz);
        if (nz > 0.2) {
            for (j = 0; j < 4; j++)
                q[j] = V(qx[j], qy[j], qz[j], qx[j], qz[j] - qy[j]);
            rg_mesh_poly(m, q, 4, RG_SHADE_ART, sideP);
        } else {
            double w = hypot(xb - xa, zb - za);
            int mod = (int)(u1 - u0 - w);
            double uaf;
            double us[4], vs[4] = {v1, v1, v1 - (y1 - y0), v1 - (y1 - y0)};

            if (mod < 1)
                mod = 1;
            uaf = u0 + (double)((k * 3u) % (unsigned)mod);       /* float u0 plus an int, as upstream */
            us[0] = uaf; us[1] = uaf + w; us[2] = uaf + w; us[3] = uaf;
            for (j = 0; j < 4; j++)
                q[j] = V(qx[j], qy[j], qz[j], us[j], vs[j]);
            rg_mesh_poly(m, q, 4, nz < 0 ? RG_SHADE_BACK : RG_SHADE_WEST, side);
        }
    }
    return !m->failed;
}

/* ---- dispatch ------------------------------------------------------------------------------ */

bool rg_part_emit(const RgPart *p, RgMesh *m)
{
    switch (p->kind) {
    case RG_P_PRISM: return emit_prism(&p->u.prism, p->name, m);
    case RG_P_HIPROOF: return emit_hip(&p->u.hip, p->name, m);
    case RG_P_FRUSTUM: return emit_frustum(&p->u.frustum, p->name, m);
    case RG_P_VAULT: return emit_vault(&p->u.vault, p->name, m);
    case RG_P_WALLS: return emit_walls(&p->u.walls, p->name, m);
    case RG_P_CARD: return emit_card(&p->u.card, p->name, m);
    case RG_P_FACET: return emit_facet(&p->u.facet, p->name, m);
    case RG_P_PLAINWALL: return emit_plain(&p->u.plain, p->name, m);
    case RG_P_DECAL: return emit_decal(&p->u.decal, p->name, m);
    case RG_P_CYLINDER: return emit_cylinder(&p->u.cyl, p->name, m);
    case RG_P_LIFTED: {
        RgMesh inner;
        unsigned i;
        bool ok;

        if (p->u.lifted.part == NULL)
            return false;
        rg_mesh_init(&inner);
        ok = rg_part_emit(p->u.lifted.part, &inner);
        if (ok && !inner.failed) {
            double b = p->u.lifted.base;

            for (i = 0; i < inner.n; i++) {
                const RgTri *t = &inner.t[i];
                RgVtx a = t->p[0], bb = t->p[1], c = t->p[2];

                a.y += b; a.z += b; bb.y += b; bb.z += b; c.y += b; c.z += b;
                rg_mesh_tri(m, &a, &bb, &c, t->shade, rg_mesh_tag(m, inner.names[t->tag]));
            }
        }
        ok = ok && !inner.failed;
        rg_mesh_free(&inner);
        return ok && !m->failed;
    }
    }
    return false;
}

void rg_parts_init(RgPartList *l)
{
    memset(l, 0, sizeof(*l));
}

void rg_parts_free(RgPartList *l)
{
    unsigned i;

    for (i = 0; i < RG_PART_CHUNKS; i++)
        free(l->chunk[i]);
    memset(l, 0, sizeof(*l));
}

RgPart *rg_parts_add(RgPartList *l, RgPartKind kind, const char *name)
{
    unsigned c = l->n / RG_PART_CHUNK, k = l->n % RG_PART_CHUNK;
    RgPart *p;

    if (c >= RG_PART_CHUNKS || strlen(name) >= RG_NAME_LEN) {
        l->failed = true;
        return NULL;
    }
    if (l->chunk[c] == NULL) {
        l->chunk[c] = (RgPart *)calloc(RG_PART_CHUNK, sizeof(RgPart));
        if (l->chunk[c] == NULL) {
            l->failed = true;
            return NULL;
        }
    }
    p = &l->chunk[c][k];
    memset(p, 0, sizeof(*p));
    p->kind = kind;
    memcpy(p->name, name, strlen(name) + 1u);
    l->n++;
    return p;
}

RgPart *rg_parts_at(const RgPartList *l, unsigned i)
{
    return i < l->n ? &l->chunk[i / RG_PART_CHUNK][i % RG_PART_CHUNK] : NULL;
}

bool rg_parts_emit(const RgPartList *l, RgMesh *m)
{
    unsigned i;

    for (i = 0; i < l->n; i++)
        if (!rg_part_emit(rg_parts_at(l, i), m))
            return false;
    return !m->failed;
}
