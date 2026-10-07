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
