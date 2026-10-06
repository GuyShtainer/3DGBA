/* rg_kspecs_lavender.c -- Lavender Town and Route 10 recipes (FireRed / LeafGreen).
 * 3DGBA original work, GPLv3, "Kanto recipes". Phase 34 slice K7 (docs/phase34-frlg/SPEC.md sections 5.3-5.6).
 *
 * Every number below was read off the `romgen author ... art` pictures of the user's own ROM (art row = z - y for the
 * renderer's oblique camera). The builders reuse the Emerald part library (rg_geom) unchanged; every face is a PROJ
 * edge, which copies the art by row, so the ortho check holds by construction where the geometry is right.
 *
 *   k_lavender_house   5x5 cells (80x80): the purple-roofed houses (layout 82; three placements, two of them side by
 *                      side) */
#include "rg_bspecs.h"

#include <string.h>

#define L_LAVENDER 82, 0xB6187344u
#define LV_GROUND {0x001}

static void lv_pt(double (*poly)[2], unsigned i, double z, double y)
{
    poly[i][0] = z;
    poly[i][1] = y;
}

/* A profile prism: poly points (z, y); each edge PROJ-textured from the listed art rows (0,0 = skipped). */
static bool lv_profile(RgPartList *out, const char *name, double x0, double x1, bool ends, unsigned n,
                       const double (*pts)[2], const double (*rows)[2])
{
    RgPart *pt = rg_parts_add(out, RG_P_PRISM, name);
    RgPrism *pr;
    unsigned i;

    if (pt == NULL)
        return false;
    pr = &pt->u.prism;
    pr->x0 = x0; pr->x1 = x1;
    pr->west = pr->east = ends;
    pr->nPoly = n;
    pr->skip = 0;
    for (i = 0; i < n; i++) {
        lv_pt(pr->poly, i, pts[i][0], pts[i][1]);
        if (rows[i][0] == 0 && rows[i][1] == 0) {
            pr->skip |= 1u << i;
        } else {
            pr->edges[i].kind = RG_EM_PROJ;
            pr->edges[i].proj = rg_proj_rows(rows[i][0], rows[i][1]);
        }
    }
    return true;
}

/* ---- k_lavender_house: 80x80 art (rect (8,8), 5x5; cell rows 9-11 are matched) -------------------------------------- */
/* Rows: roof top edge 10, the lilac hip roof seen from above 10-45, the dark eave 45-47, the facade with the two
 * windows, the door and the yellow trim 47-68. A low roof over a one-storey wall. */
static bool k_lavender_house(const RgSpec *spec, int a0, int a1, RgPartList *out)
{
    static const double pts[6][2] = {{68, 0}, {68, 21}, {68, 23}, {50.5, 40.5}, {40, 40.5}, {40, 0}};
    static const double rows[6][2] = {{47, 68}, {45, 47}, {10, 45}, {0, 0}, {0, 0}, {0, 0}};

    (void)spec; (void)a0; (void)a1;
    return lv_profile(out, "house", 0, 80, true, 6, pts, rows) && !out->failed;
}

static const RgExact kHouseExact[1] = {{0, 10, 80, 68, false}};

const RgSpec rg_kspecs_lavender[] = {
    {"k_lavender_house", RG_SPEC_DIRECT, L_LAVENDER, {8, 8, 5, 5}, {1, 4}, LV_GROUND, 1, kHouseExact, 1,
     k_lavender_house, 0, 0, NULL},
};
const unsigned rg_kspecs_lavender_count = sizeof(rg_kspecs_lavender) / sizeof(rg_kspecs_lavender[0]);
