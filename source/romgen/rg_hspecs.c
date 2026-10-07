/* rg_hspecs.c -- the shared helpers of the Phase 36 Hoenn recipes (3DGBA original work, GPLv3). Pure C. See rg_hspecs.h. */
#include "rg_hspecs.h"

#include <math.h>

bool rg_h_profile(RgPartList *out, const char *name, double x0, double x1, bool ends, unsigned n,
                  const double (*pts)[2], const double (*rows)[2])
{
    RgPart *pt;
    RgPrism *pr;
    unsigned i;

    if (n < 3 || n > RG_PRISM_PTS)
        return false;
    pt = rg_parts_add(out, RG_P_PRISM, name);
    if (pt == NULL)
        return false;
    pr = &pt->u.prism;
    pr->x0 = x0;
    pr->x1 = x1;
    pr->west = pr->east = ends;
    pr->nPoly = n;
    pr->skip = 0;
    for (i = 0; i < n; i++) {
        pr->poly[i][0] = pts[i][0];
        pr->poly[i][1] = pts[i][1];
        if (rows[i][0] == 0 && rows[i][1] == 0) {
            pr->skip |= 1u << i;
        } else {
            pr->edges[i].kind = RG_EM_PROJ;
            pr->edges[i].proj = rg_proj_rows(rows[i][0], rows[i][1]);
        }
    }
    return true;
}

void rg_h_slope(double z0, double y0, double rows, double deg, double *z1, double *y1)
{
    double t = tan(deg * 0.017453292519943295), dz = rows / (1.0 + t);

    *z1 = z0 - dz;
    *y1 = y0 + rows - dz;
}

bool rg_h_gable_t(RgPartList *out, double width, double height, double eave, double ridge, double top)
{
    double pts[6][2], rows[6][2] = {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}};
    double zs, ys, zr, yr;

    rg_h_slope(height, height - eave, eave - ridge, RG_H_PITCH, &zs, &ys);
    rg_h_slope(zs, ys, ridge - top, 5, &zr, &yr);
    pts[0][0] = height; pts[0][1] = 0;              rows[0][0] = eave; rows[0][1] = height;
    pts[1][0] = height; pts[1][1] = height - eave;  rows[1][0] = ridge; rows[1][1] = eave;
    pts[2][0] = zs; pts[2][1] = ys;                 rows[2][0] = top; rows[2][1] = ridge;
    pts[3][0] = zr; pts[3][1] = yr;
    pts[4][0] = zr - 1; pts[4][1] = yr;
    pts[5][0] = zr - 1; pts[5][1] = 0;
    return rg_h_profile(out, "house", 0, width, true, 6, (const double (*)[2])pts, (const double (*)[2])rows) &&
           !out->failed;
}

bool rg_h_gable(RgPartList *out, double width, double height, double eave, double ridge)
{
    return rg_h_gable_t(out, width, height, eave, ridge, 0);
}

bool rg_h_flat_t(RgPartList *out, double width, double height, double wallTop, double top)
{
    double h = height - wallTop, zb = top + h;
    const double pts[5][2] = {{height, 0}, {height, h}, {zb, h}, {zb - 1, h}, {zb - 1, 0}};
    const double rows[5][2] = {{wallTop, height}, {top, wallTop}, {0, 0}, {0, 0}, {0, 0}};

    return rg_h_profile(out, "block", 0, width, true, 5, pts, rows) && !out->failed;
}

bool rg_h_flat(RgPartList *out, double width, double height, double wallTop)
{
    return rg_h_flat_t(out, width, height, wallTop, 0);
}
