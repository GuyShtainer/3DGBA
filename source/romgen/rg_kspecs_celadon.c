/* rg_kspecs_celadon.c -- Celadon City recipes (FireRed / LeafGreen).
 * 3DGBA original work, GPLv3, "Kanto recipes". Phase 34 slice K8 (docs/phase34-frlg/SPEC.md sections 5.3-5.6).
 *
 * Every number below was read off the `romgen author ... art` pictures of the user's own ROM (art row = z - y for the
 * renderer's oblique camera). The builders reuse the Emerald part library (rg_geom) unchanged; every face is a PROJ
 * edge, which copies the art by row, so the ortho check holds by construction where the geometry is right. Side walls
 * come from RgSideCfg (rg_close_sides): a plain patch of each model's own wall art.
 *
 *   k_celadon_house   4x5 cells (64x80): the green-walled flat-roofed blocks with a wooden door (three placements) */
#include "rg_bspecs.h"

#include <string.h>

#define L_CELADON 84, 0x6B8BA7E4u
#define CL_GROUND {0x001}

static void cl_pt(double (*poly)[2], unsigned i, double z, double y)
{
    poly[i][0] = z;
    poly[i][1] = y;
}

/* A profile prism: poly points (z, y); each edge PROJ-textured from the listed art rows (0,0 = skipped). */
static bool cl_profile(RgPartList *out, const char *name, double x0, double x1, unsigned n,
                       const double (*pts)[2], const double (*rows)[2])
{
    RgPart *pt = rg_parts_add(out, RG_P_PRISM, name);
    RgPrism *pr;
    unsigned i;

    if (pt == NULL)
        return false;
    pr = &pt->u.prism;
    pr->x0 = x0; pr->x1 = x1;
    pr->west = pr->east = false;
    pr->nPoly = n;
    pr->skip = 0;
    for (i = 0; i < n; i++) {
        cl_pt(pr->poly, i, pts[i][0], pts[i][1]);
        if (rows[i][0] == 0 && rows[i][1] == 0) {
            pr->skip |= 1u << i;
        } else {
            pr->edges[i].kind = RG_EM_PROJ;
            pr->edges[i].proj = rg_proj_rows(rows[i][0], rows[i][1]);
        }
    }
    return true;
}

/* A flat-roofed block over x0..x1: the facade runs art rows ftop..zf (a vertical face, height zf - ftop), the level roof
 * top runs rows rtop..ftop behind it. */
static bool cl_block(RgPartList *out, const char *name, double x0, double x1, double zf, double ftop, double rtop)
{
    double h = zf - ftop;
    double pts[4][2], rows[4][2];

    cl_pt(pts, 0, zf, 0);
    cl_pt(pts, 1, zf, h);
    cl_pt(pts, 2, rtop + h, h);
    cl_pt(pts, 3, rtop + h, 0);
    rows[0][0] = ftop; rows[0][1] = zf;
    rows[1][0] = rtop; rows[1][1] = ftop;
    rows[2][0] = rows[2][1] = rows[3][0] = rows[3][1] = 0;
    return cl_profile(out, name, x0, x1, 4, (const double (*)[2])pts, (const double (*)[2])rows);
}

/* ---- k_celadon_house: 64x80 art (rect (36,25), 4x5) -------------------------------------------------------------------- */
/* Rows: road 0-7, the olive roof seen from above 8-42 (a level top edge at row 8), the facade with two storeys of
 * windows and the wooden door 42-80. */
static bool k_celadon_house(const RgSpec *spec, int a0, int a1, RgPartList *out)
{
    (void)spec; (void)a0; (void)a1;
    return cl_block(out, "house", 0, 64, 80, 42, 8) && !out->failed;
}
static const RgExact kHouseExact[1] = {{0, 8, 64, 80, false}};
static const RgSideCfg kHouseSide[1] = {
    {NULL, {14, 61, 34, 63}, {14, 61, 34, 63}, 999, true},
};

const RgSpec rg_kspecs_celadon[] = {
    {"k_celadon_house", RG_SPEC_DIRECT, L_CELADON, {36, 25, 4, 5}, {1, 5}, CL_GROUND, 1, kHouseExact, 1,
     k_celadon_house, 0, 0, kHouseSide},
};
const unsigned rg_kspecs_celadon_count = sizeof(rg_kspecs_celadon) / sizeof(rg_kspecs_celadon[0]);
