/* Modified for 3DGBA (GPLv3), 2026.
 * Original: ZallaxDev/pokeemerald-3Ds-dualscreen @ c330c0a, 3ds_port/scripts/voxel_building.py
 * (Raster, ortho_check, density_check), MIT License - see source/voxel/NOTICE.md.
 * Portions Copyright (c) Dust Zallax, MIT. */
#include "rg_bcheck.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

bool rg_raster_init(RgRaster *r, int w, int h)
{
    size_t n, i;

    memset(r, 0, sizeof(*r));
    if (w <= 0 || h <= 0 || (size_t)w * (size_t)h > 64u * 1024u * 1024u)
        return false;
    n = (size_t)w * (size_t)h;
    r->rgb = (uint8_t *)calloc(n, 3);
    r->depth = (double *)malloc(n * sizeof(double));
    r->owner = (int16_t *)malloc(n * sizeof(int16_t));
    if (r->rgb == NULL || r->depth == NULL || r->owner == NULL) {
        rg_raster_free(r);
        return false;
    }
    for (i = 0; i < n; i++) {
        r->depth[i] = -1e30;
        r->owner[i] = -1;
    }
    r->w = w;
    r->h = h;
    return true;
}

void rg_raster_free(RgRaster *r)
{
    free(r->rgb);
    free(r->depth);
    free(r->owner);
    memset(r, 0, sizeof(*r));
}

static double min3(double a, double b, double c) { double m = a < b ? a : b; return m < c ? m : c; }
static double max3(double a, double b, double c) { double m = a > b ? a : b; return m > c ? m : c; }

void rg_raster_draw(RgRaster *r, const double vs[3][6], const RgImage *tex, double shade, int16_t owner)
{
    double x0 = vs[0][0], y0 = vs[0][1], d0 = vs[0][2], w0 = vs[0][3], u0 = vs[0][4], v0 = vs[0][5];
    double x1 = vs[1][0], y1 = vs[1][1], d1 = vs[1][2], w1 = vs[1][3], u1 = vs[1][4], v1 = vs[1][5];
    double x2 = vs[2][0], y2 = vs[2][1], d2 = vs[2][2], w2 = vs[2][3], u2 = vs[2][4], v2 = vs[2][5];
    double area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0), inv;
    double fminx, fmaxx, fminy, fmaxy;
    int minx, maxx, miny, maxy, py, px;

    if (fabs(area) < 1e-9)
        return;
    fminx = floor(min3(x0, x1, x2));
    fmaxx = ceil(max3(x0, x1, x2));
    fminy = floor(min3(y0, y1, y2));
    fmaxy = ceil(max3(y0, y1, y2));
    if (!(fminx > -1e9 && fmaxx < 1e9 && fminy > -1e9 && fmaxy < 1e9))
        return;
    minx = fminx > 0 ? (int)fminx : 0;
    maxx = fmaxx < r->w - 1 ? (int)fmaxx : r->w - 1;
    miny = fminy > 0 ? (int)fminy : 0;
    maxy = fmaxy < r->h - 1 ? (int)fmaxy : r->h - 1;
    inv = 1.0 / area;
    for (py = miny; py <= maxy; py++) {
        double cy = py + 0.5;

        for (px = minx; px <= maxx; px++) {
            double cx = px + 0.5;
            double a = ((x1 - cx) * (y2 - cy) - (x2 - cx) * (y1 - cy)) * inv;
            double b = ((x2 - cx) * (y0 - cy) - (x0 - cx) * (y2 - cy)) * inv;
            double c = 1.0 - a - b, d, iw, u, v, fu, fv;
            size_t idx;
            const uint8_t *t;
            long tu, tv;

            if (a < -1e-7 || b < -1e-7 || c < -1e-7)
                continue;
            d = a * d0 + b * d1 + c * d2;
            idx = (size_t)py * (size_t)r->w + (size_t)px;
            if (d <= r->depth[idx])
                continue;
            iw = a * w0 + b * w1 + c * w2;
            u = (a * u0 + b * u1 + c * u2) / iw;
            v = (a * v0 + b * v1 + c * v2) / iw;
            fu = floor(u);
            fv = floor(v);
            if (!(fu >= 0 && fv >= 0 && fu < tex->w && fv < tex->h))
                continue;
            tu = (long)fu;
            tv = (long)fv;
            t = tex->px + ((size_t)tv * (size_t)tex->w + (size_t)tu) * 4u;
            if (t[3] < 128)
                continue;
            r->depth[idx] = d;
            r->owner[idx] = owner;
            r->rgb[idx * 3u + 0u] = (uint8_t)(int)(t[0] * shade);
            r->rgb[idx * 3u + 1u] = (uint8_t)(int)(t[1] * shade);
            r->rgb[idx * 3u + 2u] = (uint8_t)(int)(t[2] * shade);
        }
    }
}

bool rg_ortho_check(const RgMesh *m, const RgImage *art, const RgExact *exact, unsigned nExact,
                    const RgImage *reference, RgOrthoResult *out)
{
    enum { M = 32 };
    RgRaster ras;
    const RgImage *ref = reference != NULL ? reference : art;
    RgExact whole;
    unsigned i, ri;

    out->wrong = out->missing = out->extra = 0;
    if (!rg_raster_init(&ras, art->w + 2 * M, art->h + M))
        return false;
    for (i = 0; i < m->n; i++) {
        const RgTri *t = &m->t[i];
        double vs[3][6];
        unsigned k;

        if (t->flags & (RG_TAG_DEPTH | RG_TAG_BEHIND))
            continue;
        for (k = 0; k < 3; k++) {
            vs[k][0] = t->p[k].x + M;
            vs[k][1] = t->p[k].z - t->p[k].y + M;
            vs[k][2] = t->p[k].y + t->p[k].z;
            vs[k][3] = 1.0;
            vs[k][4] = t->p[k].u;
            vs[k][5] = t->p[k].v;
        }
        rg_raster_draw(&ras, vs, art, t->shade, (int16_t)t->tag);
    }
    if (exact == NULL || nExact == 0) {
        whole.x0 = 0; whole.y0 = 0; whole.x1 = (int16_t)art->w; whole.y1 = (int16_t)art->h; whole.behind = false;
        exact = &whole;
        nExact = 1;
    }
    for (ri = 0; ri < nExact; ri++) {
        int x, y;

        for (y = exact[ri].y0; y < exact[ri].y1; y++) {
            for (x = exact[ri].x0; x < exact[ri].x1; x++) {
                const uint8_t *a;
                size_t idx;

                if (x < 0 || y < 0 || x >= ref->w || y >= ref->h)
                    continue;
                a = ref->px + ((size_t)y * (size_t)ref->w + (size_t)x) * 4u;
                idx = (size_t)(y + M) * (size_t)ras.w + (size_t)(x + M);
                if (a[3] >= 128) {
                    if (ras.owner[idx] < 0)
                        out->missing++;
                    else if (ras.rgb[idx * 3u] != a[0] || ras.rgb[idx * 3u + 1u] != a[1] || ras.rgb[idx * 3u + 2u] != a[2])
                        out->wrong++;
                } else if (ras.owner[idx] >= 0 && !exact[ri].behind) {
                    out->extra++;
                }
            }
        }
    }
    rg_raster_free(&ras);
    return true;
}

unsigned rg_density_check(const RgMesh *m, const RgImage *art, RgDensityBad *bad, unsigned maxBad)
{
    unsigned i, count = 0;

    (void)art;
    for (i = 0; i < m->n; i++) {
        const RgTri *t = &m->t[i];
        double p[3][3], q[3][2], e1[3], e2[3], nrm[3], sq[3], nl, h[3], f[3], beta, expect;
        double a1, b1, a2, b2, det, du1, dv1, du2, dv2, dh[2], df[2], along, down, shear;
        double tmp[3];
        unsigned k;

        if (t->flags & (RG_TAG_CLAMP | RG_TAG_PROJ))
            continue;
        for (k = 0; k < 3; k++) {
            p[k][0] = t->p[k].x; p[k][1] = t->p[k].y; p[k][2] = t->p[k].z;
            q[k][0] = t->p[k].u; q[k][1] = t->p[k].v;
        }
        for (k = 0; k < 3; k++) {
            e1[k] = p[1][k] - p[0][k];
            e2[k] = p[2][k] - p[0][k];
        }
        nrm[0] = e1[1] * e2[2] - e1[2] * e2[1];
        nrm[1] = e1[2] * e2[0] - e1[0] * e2[2];
        nrm[2] = e1[0] * e2[1] - e1[1] * e2[0];
        for (k = 0; k < 3; k++)
            sq[k] = nrm[k] * nrm[k];
        nl = sqrt(rg_pysum(sq, 3));
        if (nl < 1e-9)
            continue;
        for (k = 0; k < 3; k++)
            nrm[k] = nrm[k] / nl;
        if (fabs(nrm[1]) > 1 - 1e-9) {
            h[0] = 1.0; h[1] = 0.0; h[2] = 0.0;
            f[0] = 0.0; f[1] = 0.0; f[2] = 1.0;
        } else {
            double hv[3];

            hv[0] = nrm[2]; hv[1] = 0.0; hv[2] = -nrm[0];
            rg_unit3(hv, h);
            f[0] = h[1] * nrm[2] - h[2] * nrm[1];
            f[1] = h[2] * nrm[0] - h[0] * nrm[2];
            f[2] = h[0] * nrm[1] - h[1] * nrm[0];
        }
        {
            double s = sqrt(pow(nrm[0], 2) + pow(nrm[2], 2));

            beta = asin(s < 1.0 ? s : 1.0);
        }
        expect = cos(beta) + sin(beta);
        for (k = 0; k < 3; k++) tmp[k] = e1[k] * h[k];
        a1 = rg_pysum(tmp, 3);
        for (k = 0; k < 3; k++) tmp[k] = e1[k] * f[k];
        b1 = rg_pysum(tmp, 3);
        for (k = 0; k < 3; k++) tmp[k] = e2[k] * h[k];
        a2 = rg_pysum(tmp, 3);
        for (k = 0; k < 3; k++) tmp[k] = e2[k] * f[k];
        b2 = rg_pysum(tmp, 3);
        det = a1 * b2 - a2 * b1;
        if (fabs(det) < 1e-9)
            continue;
        du1 = q[1][0] - q[0][0]; dv1 = q[1][1] - q[0][1];
        du2 = q[2][0] - q[0][0]; dv2 = q[2][1] - q[0][1];
        dh[0] = (du1 * b2 - du2 * b1) / det; dh[1] = (dv1 * b2 - dv2 * b1) / det;
        df[0] = (du2 * a1 - du1 * a2) / det; df[1] = (dv2 * a1 - dv1 * a2) / det;
        along = hypot(dh[0], dh[1]);
        down = hypot(df[0], df[1]);
        shear = fabs(dh[0] * df[0] + dh[1] * df[1]);
        if (fabs(along - 1.0) > 1e-3 || fabs(down - expect) > 1e-3 || shear > 1e-3) {
            if (count < maxBad && bad != NULL) {
                size_t len = strlen(m->names[t->tag]);

                memcpy(bad[count].tag, m->names[t->tag], len + 1u);
                bad[count].along = along;
                bad[count].down = down / expect;
                bad[count].shear = shear;
            }
            count++;
        }
    }
    return count;
}

/* ---- side closure ---------------------------------------------------------------------------------------------- */

typedef struct SideGrid { uint8_t *cov; int z0, y0, zw, yh; RgSideResult *out; } SideGrid;

static void side_cell(void *vg, double z, double y)
{
    SideGrid *g = (SideGrid *)vg;
    int zz = (int)floor(z) - g->z0, yy = (int)floor(y) - g->y0;
    size_t at;

    if (zz < 0 || zz >= g->zw || yy < 0 || yy >= g->yh)
        return;
    at = (size_t)yy * (size_t)g->zw + (size_t)zz;
    if (g->cov[at] & 2u)
        return;
    g->cov[at] |= 2u;
    g->out->expected++;
    if (!(g->cov[at] & 1u))
        g->out->open++;
}

/* A part's triangles are tagged "<name>" or "<name>.<what>". */
static bool tag_of_part(const char *tag, const char *name)
{
    size_t n = strlen(name);

    return strncmp(tag, name, n) == 0 && (tag[n] == '\0' || tag[n] == '.');
}

#define SIDE_REACH 2.5      /* a prism or part whose end lies within this many px of the model's edge is on the side (a roof's overhang) */

bool rg_side_check(const RgPartList *parts, const RgMesh *m, bool east, RgSideResult *out)
{
    double xedge, zlo = 1e30, zhi = -1e30, ylo = 1e30, yhi = -1e30;
    unsigned i, k;
    int zw, yh, z0, y0, zz, yy;
    uint8_t *cov;

    memset(out, 0, sizeof(*out));
    if (m->n == 0)
        return true;
    xedge = east ? -1e30 : 1e30;
    for (i = 0; i < m->n; i++)
        for (k = 0; k < 3; k++) {
            double x = m->t[i].p[k].x, z = m->t[i].p[k].z, y = m->t[i].p[k].y;

            if (east ? x > xedge : x < xedge)
                xedge = x;
            if (z < zlo) zlo = z;
            if (z > zhi) zhi = z;
            if (y < ylo) ylo = y;
            if (y > yhi) yhi = y;
        }
    for (i = 0; i < parts->n; i++) {      /* a prism with no face toward the camera still owns its section: grow the grid to it */
        const RgPart *p = rg_parts_at(parts, i);

        if (p != NULL && p->kind == RG_P_PRISM)
            for (k = 0; k < p->u.prism.nPoly; k++) {
                double z = p->u.prism.poly[k][0], y = p->u.prism.poly[k][1];

                if (z < zlo) zlo = z;
                if (z > zhi) zhi = z;
                if (y < ylo) ylo = y;
                if (y > yhi) yhi = y;
            }
    }
    z0 = (int)floor(zlo); y0 = (int)floor(ylo);
    zw = (int)ceil(zhi) - z0 + 1; yh = (int)ceil(yhi) - y0 + 1;
    if (zw <= 0 || yh <= 0 || (size_t)zw * (size_t)yh > 4u * 1024u * 1024u)
        return false;
    cov = (uint8_t *)calloc((size_t)zw * (size_t)yh, 1);
    if (cov == NULL)
        return false;
    for (i = 0; i < m->n; i++) {         /* every triangle projected along x: its (z, y) area covers the cells it holds */
        const RgTri *t = &m->t[i];
        double az = t->p[0].z, ay = t->p[0].y, bz = t->p[1].z, by = t->p[1].y, cz = t->p[2].z, cy = t->p[2].y;
        double area = (bz - az) * (cy - ay) - (cz - az) * (by - ay);
        double mz0 = az < bz ? (az < cz ? az : cz) : (bz < cz ? bz : cz), mz1 = az > bz ? (az > cz ? az : cz) : (bz > cz ? bz : cz);
        double my0 = ay < by ? (ay < cy ? ay : cy) : (by < cy ? by : cy), my1 = ay > by ? (ay > cy ? ay : cy) : (by > cy ? by : cy);
        int iz0 = (int)floor(mz0) - z0, iz1 = (int)ceil(mz1) - z0, iy0 = (int)floor(my0) - y0, iy1 = (int)ceil(my1) - y0;

        if (fabs(area) < 1e-9 || (t->flags & (RG_TAG_DEPTH | RG_TAG_BEHIND)))
            continue;
        if (iz0 < 0) iz0 = 0;
        if (iy0 < 0) iy0 = 0;
        if (iz1 > zw - 1) iz1 = zw - 1;
        if (iy1 > yh - 1) iy1 = yh - 1;
        for (yy = iy0; yy <= iy1; yy++)
            for (zz = iz0; zz <= iz1; zz++) {
                double pz = z0 + zz + 0.5, py = y0 + yy + 0.5;
                double w0 = ((bz - pz) * (cy - py) - (cz - pz) * (by - py)) / area;
                double w1 = ((cz - pz) * (ay - py) - (az - pz) * (cy - py)) / area;
                double w2 = 1.0 - w0 - w1;

                if (w0 >= -1e-7 && w1 >= -1e-7 && w2 >= -1e-7)
                    cov[(size_t)yy * (size_t)zw + (size_t)zz] = 1;
            }
    }
    for (i = 0; i < parts->n; i++) {      /* the expected cells: the union of the parts on the side, eroded at their borders */
        const RgPart *p = rg_parts_at(parts, i);

        if (p == NULL)
            continue;
        if (p->kind == RG_P_PRISM) {
            SideGrid g;

            g.cov = cov; g.z0 = z0; g.y0 = y0; g.zw = zw; g.yh = yh; g.out = out;
            (void)rg_prism_exposed(parts, i, east, side_cell, &g);
        } else if (p->kind == RG_P_HIPROOF || p->kind == RG_P_FRUSTUM) {
            /* a roof or a chamfered block: its section is everything under its own upper outline, from its lowest point */
            double pzlo = 1e30, pzhi = -1e30, pylo = 1e30, pxlo = 1e30, pxhi = -1e30;
            double *top = (double *)malloc((size_t)zw * sizeof(double));
            bool any = false;
            unsigned j;

            if (top == NULL) {
                free(cov);
                return false;
            }
            for (zz = 0; zz < zw; zz++)
                top[zz] = -1e30;
            for (j = 0; j < m->n; j++) {
                const RgTri *t = &m->t[j];

                if (!tag_of_part(m->names[t->tag], p->name))
                    continue;
                any = true;
                for (k = 0; k < 3; k++) {
                    const RgVtx *a = &t->p[k], *b = &t->p[(k + 1) % 3];
                    unsigned s;

                    if (a->x < pxlo) pxlo = a->x;
                    if (a->x > pxhi) pxhi = a->x;
                    if (a->z < pzlo) pzlo = a->z;
                    if (a->z > pzhi) pzhi = a->z;
                    if (a->y < pylo) pylo = a->y;
                    for (s = 0; s <= 8u; s++) {
                        double z = a->z + (b->z - a->z) * s / 8.0, y = a->y + (b->y - a->y) * s / 8.0;
                        int c = (int)floor(z) - z0;

                        if (c >= 0 && c < zw && y > top[c])
                            top[c] = y;
                    }
                }
            }
            if (any && (east ? pxhi >= xedge - SIDE_REACH : pxlo <= xedge + SIDE_REACH)) {
                for (yy = 0; yy < yh; yy++)
                    for (zz = 0; zz < zw; zz++) {
                        size_t at = (size_t)yy * (size_t)zw + (size_t)zz;
                        double zc = z0 + zz + 0.5, yc = y0 + yy + 0.5;

                        if ((cov[at] & 2u) || zc < pzlo + 0.75 || zc > pzhi - 0.75 || yc < pylo + 0.75 || yc > top[zz] - 0.75)
                            continue;
                        out->expected++;
                        cov[at] |= 2u;
                        if (!(cov[at] & 1u))
                            out->open++;
                    }
            }
            free(top);
        }
    }
    out->applicable = out->expected > 0;
    free(cov);
    return true;
}

/* ---- back closure (look L6) ---------------------------------------------------------------------------------------- */

#define BACK_MARGIN 0.75    /* px: a ray must reach this far inside a solid to count (grazing an edge is not entering) */
#define BACK_SLACK 1.0      /* px along the ray: a face this close behind the solid's surface still closes it */
#define BACK_SEC RG_SEC_PTS

/* Where the 2D ray o + t*d first gets BACK_MARGIN inside the polygon: the polygon edge it crossed there. 1e30 = never. */
static double back_entry(const double (*poly)[2], unsigned n, const double o[2], const double d[2])
{
    double ts[BACK_SEC + 2];
    unsigned nt = 0, i, j;

    for (i = 0; i < n && nt < BACK_SEC + 2u; i++) {
        const double *a = poly[i], *b = poly[(i + 1) % n];
        double ez = b[0] - a[0], ey = b[1] - a[1], den = d[0] * ey - d[1] * ez, t, s;

        if (fabs(den) < 1e-12)
            continue;
        t = ((a[0] - o[0]) * ey - (a[1] - o[1]) * ez) / den;
        s = ((a[0] - o[0]) * d[1] - (a[1] - o[1]) * d[0]) / den;
        if (s < -1e-9 || s > 1 + 1e-9)
            continue;
        ts[nt++] = t;
    }
    for (i = 1; i < nt; i++) {
        double key = ts[i];

        for (j = i; j > 0 && ts[j - 1] > key; j--)
            ts[j] = ts[j - 1];
        ts[j] = key;
    }
    for (i = 0; i + 1 < nt; i++) {
        double mid = (ts[i] + ts[i + 1]) / 2;

        if (ts[i + 1] - ts[i] > 2 * BACK_MARGIN &&
            rg_poly_inside(poly, n, o[0] + mid * d[0], o[1] + mid * d[1], BACK_MARGIN))
            return ts[i];
    }
    return 1e30;
}

/* Moller-Trumbore for a ray in the plane x = const: origin (x, oy, oz), direction (0, dy, dz). */
static double back_hit(const RgTri *t, double x, double oz, double oy, double dz, double dy)
{
    double e1[3], e2[3], pv[3], tv[3], qv[3], det, inv, u, v, dir[3];
    const RgVtx *a = &t->p[0], *b = &t->p[1], *c = &t->p[2];

    dir[0] = 0.0; dir[1] = dy; dir[2] = dz;
    e1[0] = b->x - a->x; e1[1] = b->y - a->y; e1[2] = b->z - a->z;
    e2[0] = c->x - a->x; e2[1] = c->y - a->y; e2[2] = c->z - a->z;
    pv[0] = dir[1] * e2[2] - dir[2] * e2[1];
    pv[1] = dir[2] * e2[0] - dir[0] * e2[2];
    pv[2] = dir[0] * e2[1] - dir[1] * e2[0];
    det = e1[0] * pv[0] + e1[1] * pv[1] + e1[2] * pv[2];
    if (fabs(det) < 1e-12)
        return 1e30;
    inv = 1.0 / det;
    tv[0] = x - a->x; tv[1] = oy - a->y; tv[2] = oz - a->z;
    u = (tv[0] * pv[0] + tv[1] * pv[1] + tv[2] * pv[2]) * inv;
    if (u < -1e-7 || u > 1 + 1e-7)
        return 1e30;
    qv[0] = tv[1] * e1[2] - tv[2] * e1[1];
    qv[1] = tv[2] * e1[0] - tv[0] * e1[2];
    qv[2] = tv[0] * e1[1] - tv[1] * e1[0];
    v = (dir[0] * qv[0] + dir[1] * qv[1] + dir[2] * qv[2]) * inv;
    if (v < -1e-7 || u + v > 1 + 1e-7)
        return 1e30;
    return (e2[0] * qv[0] + e2[1] * qv[1] + e2[2] * qv[2]) * inv;
}

const double rg_back_pitch[RG_BACK_VIEWS] = {30.0, 60.0, 90.0};

bool rg_back_check(const RgPartList *parts, const RgMesh *m, RgBackResult *out, RgBackRayFn cb, void *ctx)
{
    double xlo = 1e30, xhi = -1e30, zlo = 1e30, zhi = -1e30, ylo = 1e30, yhi = -1e30, cz, cy, R, L;
    unsigned i, k, view;
    int ix, nx;
    unsigned *col;

    memset(out, 0, sizeof(*out));
    if (m->n == 0)
        return true;
    for (i = 0; i < m->n; i++)
        for (k = 0; k < 3; k++) {
            const RgVtx *v = &m->t[i].p[k];

            if (v->x < xlo) xlo = v->x;
            if (v->x > xhi) xhi = v->x;
            if (v->z < zlo) zlo = v->z;
            if (v->z > zhi) zhi = v->z;
            if (v->y < ylo) ylo = v->y;
            if (v->y > yhi) yhi = v->y;
        }
    for (i = 0; i < parts->n; i++) {     /* an open solid may reach past every face: grow the box to the parts */
        const RgPart *p = rg_parts_at(parts, i);

        if (p != NULL && p->kind == RG_P_PRISM && !rg_prism_is_sheet(&p->u.prism)) {
            if (p->u.prism.x0 < xlo) xlo = p->u.prism.x0;
            if (p->u.prism.x1 > xhi) xhi = p->u.prism.x1;
            for (k = 0; k < p->u.prism.nPoly; k++) {
                double z = p->u.prism.poly[k][0], y = p->u.prism.poly[k][1];

                if (z < zlo) zlo = z;
                if (z > zhi) zhi = z;
                if (y < ylo) ylo = y;
                if (y > yhi) yhi = y;
            }
        }
    }
    if (!(xhi - xlo < 4096.0 && zhi - zlo < 4096.0 && yhi - ylo < 4096.0))
        return false;
    cz = (zlo + zhi) / 2;
    cy = (ylo + yhi) / 2;
    R = hypot(zhi - zlo, yhi - ylo) / 2 + 2.0;
    L = 2 * R + 10.0;
    nx = (int)ceil(xhi - floor(xlo));
    col = (unsigned *)malloc((size_t)m->n * sizeof(unsigned));
    if (col == NULL)
        return false;
    for (ix = 0; ix < nx; ix++) {
        double x = floor(xlo) + ix + 0.5;
        unsigned nc = 0;

        for (i = 0; i < m->n; i++) {      /* the triangles this ray column can meet */
            const RgTri *t = &m->t[i];
            double a = t->p[0].x, b = t->p[1].x, c = t->p[2].x;
            double lo = a < b ? (a < c ? a : c) : (b < c ? b : c), hi = a > b ? (a > c ? a : c) : (b > c ? b : c);

            if (!(t->flags & (RG_TAG_DEPTH | RG_TAG_BEHIND)) && lo <= x + 1e-9 && hi >= x - 1e-9)
                col[nc++] = i;
        }
        for (view = 0; view < RG_BACK_VIEWS; view++) {
            double ph = rg_back_pitch[view] * 3.14159265358979323846 / 180.0;
            double d[2], u[2];             /* (z, y): from behind (north), looking south and down */
            int iw, nw = (int)ceil(2 * R);

            d[0] = cos(ph); d[1] = -sin(ph);
            u[0] = sin(ph); u[1] = cos(ph);
            if (fabs(d[0]) < 1e-12) d[0] = 0.0;
            if (fabs(u[1]) < 1e-12) u[1] = 0.0;
            for (iw = 0; iw < nw; iw++) {
                double w = -R + iw + 0.5, o[2], best = 1e30, hit = 1e30;
                int state;

                o[0] = cz + w * u[0] - L * d[0];
                o[1] = cy + w * u[1] - L * d[1];
                for (i = 0; i < parts->n; i++) {
                    const RgPart *p = rg_parts_at(parts, i);
                    double sec[2][BACK_SEC][2];
                    unsigned ns, s, n[2];

                    if (p == NULL)
                        continue;
                    ns = rg_part_section(p, x, BACK_MARGIN, sec, n);
                    for (s = 0; s < ns; s++) {
                        double t = back_entry((const double (*)[2])sec[s], n[s], o, d);

                        if (t < best)
                            best = t;
                    }
                }
                if (best >= 1e29) {
                    state = 0;
                } else {
                    for (i = 0; i < nc; i++) {
                        double t = back_hit(&m->t[col[i]], x, o[0], o[1], d[0], d[1]);

                        if (t > 0.0 && t < hit)
                            hit = t;
                    }
                    out->expected++;
                    if (hit > best + BACK_SLACK) {
                        out->open++;
                        state = 2;
                    } else {
                        state = 1;
                    }
                }
                if (cb != NULL)
                    cb(ctx, view, ix, iw, state);
            }
        }
    }
    free(col);
    out->applicable = out->expected > 0;
    return true;
}
