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
