/* rg_kspecs_cerulean.c -- Cerulean City, Route 4 and Route 25 recipes (FireRed / LeafGreen).
 * 3DGBA original work, GPLv3, "Kanto recipes". Phase 34 slice K5 (docs/phase34-frlg/SPEC.md sections 5.3-5.6).
 *
 * Every number below was read off the `romgen author ... art` pictures of the user's own ROM (art row = z - y for the
 * renderer's oblique camera). The builders reuse the Emerald part library (rg_geom) unchanged; every face is a PROJ
 * edge, which copies the art by row, so the ortho check holds by construction where the geometry is right.
 *
 *   k_cerulean_house_a..e  the blue-roofed houses of Cerulean (layout 81); one builder, five rows (the cell contents
 *                          differ, so each is its own placement: 7x4, 6x4, 7x4, 7x4, 6x4 cells)
 *   k_cerulean_bike        4x6 cells (64x96): the Bike Shop, a flat glass roof over an awning (layout 81)
 *   k_route25_cottage      5x4 cells (80x64): the Sea Cottage, a green hip roof with a chimney (layout 113) */
#include "rg_bspecs.h"

#include <string.h>

#define L_CERULEAN 81, 0xB952CCF4u
#define L_ROUTE25 113, 0xBBAC050Au
#define CE_GROUND {0x001}

static void ce_pt(double (*poly)[2], unsigned i, double z, double y)
{
    poly[i][0] = z;
    poly[i][1] = y;
}

/* A profile prism: poly points (z, y); each edge PROJ-textured from the listed art rows (0,0 = skipped). */
static bool ce_profile(RgPartList *out, const char *name, double x0, double x1, bool ends, unsigned n,
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
        ce_pt(pr->poly, i, pts[i][0], pts[i][1]);
        if (rows[i][0] == 0 && rows[i][1] == 0) {
            pr->skip |= 1u << i;
        } else {
            pr->edges[i].kind = RG_EM_PROJ;
            pr->edges[i].proj = rg_proj_rows(rows[i][0], rows[i][1]);
        }
    }
    return true;
}

/* ---- the blue-roofed houses: width arg0 px, art 64 rows ------------------------------------------------------------ */
/* Rows: the fence posts and the back-door box above the roof 0-7 (not modelled), roof top edge 8, slate front slope
 * 9-39, fascia 40-42, facade with the door, the two green planters and the window 43-63. */
static bool k_cerulean_house(const RgSpec *spec, int a0, int a1, RgPartList *out)
{
    static const double pts[6][2] = {{64, 0}, {64, 21}, {64, 24}, {48, 40}, {40, 40}, {40, 0}};
    static const double rows[6][2] = {{43, 64}, {40, 43}, {8, 40}, {0, 0}, {0, 0}, {0, 0}};

    (void)spec; (void)a1;
    return ce_profile(out, "house", 0, a0, true, 6, pts, rows) && !out->failed;
}

/* ---- k_cerulean_bike: 64x96 art (rect (12,23), 4x6) ---------------------------------------------------------------- */
/* Rows: grass 0-7, frame 8-12, the checkered glass roof 13-51, frame 52-57, grey wall with three windows 58-71, the
 * striped awning 72-85, dark undercroft and the legs 86-95. A flat-roofed block: a level top at y 38 over a wall. */
static bool k_cerulean_bike(const RgSpec *spec, int a0, int a1, RgPartList *out)
{
    static const double pts[4][2] = {{96, 0}, {96, 38}, {46, 38}, {46, 0}};
    static const double rows[4][2] = {{58, 96}, {8, 58}, {0, 0}, {0, 0}};

    (void)spec; (void)a0; (void)a1;
    return ce_profile(out, "shop", 0, 64, true, 4, pts, rows) && !out->failed;
}

/* ---- k_route25_cottage: 80x64 art (rect (49,1), 5x4) --------------------------------------------------------------- */
/* A hip-roofed cottage: roof rows (top edge differs per column because the hip ends are diagonal: the rock cliff
 * behind shows in the top corners) down to row 34, eave band 35-45, facade with pillars and door 46-63. The roof is cut
 * into x-slices whose first row follows the diagonal, so the cliff pixels are not drawn on the roof. */
static bool k_route25_cottage(const RgSpec *spec, int a0, int a1, RgPartList *out)
{
    static const struct { int x0, x1, r0; } sl[9] = {{0, 4, 12}, {4, 8, 10}, {8, 12, 8}, {12, 16, 6}, {16, 20, 4},
                                                    {20, 24, 2}, {24, 72, 0}, {72, 76, 9}, {76, 80, 11}};
    unsigned i;

    (void)spec; (void)a0; (void)a1;
    for (i = 0; i < 9; i++) {
        double d = (35.0 - sl[i].r0) / 2;
        double pts[6][2], rows[6][2];

        pts[0][0] = 64; pts[0][1] = 0;
        pts[1][0] = 64; pts[1][1] = 18;
        pts[2][0] = 64; pts[2][1] = 29;
        pts[3][0] = 64 - d; pts[3][1] = 29 + d;
        pts[4][0] = 40; pts[4][1] = 29 + d;
        pts[5][0] = 40; pts[5][1] = 0;
        rows[0][0] = 46; rows[0][1] = 64;           /* facade */
        rows[1][0] = 35; rows[1][1] = 46;           /* eave band */
        rows[2][0] = sl[i].r0; rows[2][1] = 35;     /* roof slope */
        rows[3][0] = rows[3][1] = rows[4][0] = rows[4][1] = rows[5][0] = rows[5][1] = 0;
        if (!ce_profile(out, "cottage", sl[i].x0, sl[i].x1, false, 6, (const double (*)[2])pts, (const double (*)[2])rows))
            return false;
    }
    return !out->failed;
}

static const RgExact kHouse7[3] = {{8, 8, 104, 64, false}, {0, 12, 8, 64, false}, {104, 14, 112, 64, false}};
static const RgExact kHouse6[3] = {{8, 8, 88, 64, false}, {0, 12, 8, 64, false}, {88, 14, 96, 64, false}};
static const RgExact kBikeExact[1] = {{0, 8, 64, 96, false}};
static const RgExact kCottageExact[9] = {
    {0, 12, 4, 64, false}, {4, 10, 8, 64, false}, {8, 8, 12, 64, false}, {12, 6, 16, 64, false}, {16, 4, 20, 64, false},
    {20, 2, 24, 64, false}, {24, 0, 72, 64, false}, {72, 9, 76, 64, false}, {76, 11, 80, 64, false},
};

const RgSpec rg_kspecs_cerulean[] = {
    {"k_cerulean_house_a", RG_SPEC_DIRECT, L_CERULEAN, {8, 8, 7, 4}, {0, 0}, CE_GROUND, 1, kHouse7, 3,
     k_cerulean_house, 112, 0, NULL},
    {"k_cerulean_house_b", RG_SPEC_DIRECT, L_CERULEAN, {15, 8, 6, 4}, {0, 0}, CE_GROUND, 1, kHouse6, 3,
     k_cerulean_house, 96, 0, NULL},
    {"k_cerulean_house_c", RG_SPEC_DIRECT, L_CERULEAN, {28, 8, 7, 4}, {0, 0}, CE_GROUND, 1, kHouse7, 3,
     k_cerulean_house, 112, 0, NULL},
    {"k_cerulean_house_d", RG_SPEC_DIRECT, L_CERULEAN, {13, 14, 7, 4}, {0, 0}, CE_GROUND, 1, kHouse7, 3,
     k_cerulean_house, 112, 0, NULL},
    {"k_cerulean_house_e", RG_SPEC_DIRECT, L_CERULEAN, {21, 25, 6, 4}, {0, 0}, CE_GROUND, 1, kHouse6, 3,
     k_cerulean_house, 96, 0, NULL},
    {"k_cerulean_bike", RG_SPEC_DIRECT, L_CERULEAN, {12, 23, 4, 6}, {0, 0}, CE_GROUND, 1, kBikeExact, 1,
     k_cerulean_bike, 0, 0, NULL},
    {"k_route25_cottage", RG_SPEC_DIRECT, L_ROUTE25, {49, 1, 5, 4}, {0, 0}, CE_GROUND, 1, kCottageExact, 9,
     k_route25_cottage, 0, 0, NULL},
};
const unsigned rg_kspecs_cerulean_count = sizeof(rg_kspecs_cerulean) / sizeof(rg_kspecs_cerulean[0]);
