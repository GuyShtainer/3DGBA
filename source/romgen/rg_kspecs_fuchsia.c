/* rg_kspecs_fuchsia.c -- Fuchsia City, Safari Zone, Routes 11 / 12 / 16 / 18 recipes (FireRed / LeafGreen).
 * 3DGBA original work, GPLv3, "Kanto recipes". Phase 34 slice K9 (docs/phase34-frlg/SPEC.md sections 5.3-5.6).
 *
 * Every number below was read off the `romgen author ... art` pictures of the user's own ROM (art row = z - y for the
 * renderer's oblique camera). The builders reuse the Emerald part library (rg_geom) unchanged; every face is a PROJ
 * edge, which copies the art by row, so the ortho check holds by construction where the geometry is right. Side walls
 * come from RgSideCfg (rg_close_sides): a plain patch of each model's own wall art.
 *
 *   k_fuchsia_hall   5x4 cells (80x64): the grey-roofed brick halls of Fuchsia's south row (four placements) */
#include "rg_bspecs.h"

#include <string.h>

#define L_FUCHSIA 85, 0xE2428371u
#define L_SAFARI_C 147, 0x1A2757E4u
#define FZ_GROUND {0x001}

static void fz_pt(double (*poly)[2], unsigned i, double z, double y)
{
    poly[i][0] = z;
    poly[i][1] = y;
}

/* A profile prism: poly points (z, y); each edge PROJ-textured from the listed art rows (0,0 = skipped). */
static bool fz_profile(RgPartList *out, const char *name, double x0, double x1, unsigned n,
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
        fz_pt(pr->poly, i, pts[i][0], pts[i][1]);
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
static bool fz_block(RgPartList *out, const char *name, double x0, double x1, double zf, double ftop, double rtop)
{
    double h = zf - ftop;
    double pts[4][2], rows[4][2];

    fz_pt(pts, 0, zf, 0);
    fz_pt(pts, 1, zf, h);
    fz_pt(pts, 2, rtop + h, h);
    fz_pt(pts, 3, rtop + h, 0);
    rows[0][0] = ftop; rows[0][1] = zf;
    rows[1][0] = rtop; rows[1][1] = ftop;
    rows[2][0] = rows[2][1] = rows[3][0] = rows[3][1] = 0;
    return fz_profile(out, name, x0, x1, 4, (const double (*)[2])pts, (const double (*)[2])rows);
}

/* ---- k_fuchsia_hall: 80x64 art (rect (13,28), 5x4) ---------------------------------------------------------------------- */
/* Rows: rock 0-7, the light-grey flat roof with its glass panel 8-42 (a level top edge at row 8), a dark rim 42-43, the
 * brick facade with the yellow door and two windows 43-64. One flat block, 21 high. */
static bool k_fuchsia_hall(const RgSpec *spec, int a0, int a1, RgPartList *out)
{
    (void)spec; (void)a0; (void)a1;
    return fz_block(out, "hall", 0, 80, 64, 43, 8) && !out->failed;
}
static const RgExact kHallExact[1] = {{0, 8, 80, 64, false}};
static const RgSideCfg kHallSide[1] = {
    {NULL, {40, 58, 66, 60}, {6, 10, 72, 11}, 999, true},
};

/* ---- k_fuchsia_house: 96x80 art (rect (26,12), 6x5) -------------------------------------------------------------------- */
/* The gold-roofed house: grass 0-7, the tiled gold roof with its F-shaped relief 8-55 (a level top edge at row 8, a
 * darker eave 52-55), the facade with the grey awning, two arched windows and the door 56-80. One flat block, 24 high. */
static bool k_fuchsia_house(const RgSpec *spec, int a0, int a1, RgPartList *out)
{
    (void)spec;
    return fz_block(out, "house", 0, a0, 8 + a1 + 24, 8 + a1, 8) && !out->failed;   /* a0 = width, a1 = roof rows */
}
static const RgExact kHouseExact[1] = {{0, 8, 96, 80, false}};
static const RgSideCfg kHouseSide[1] = {
    {NULL, {8, 57, 88, 59}, {8, 10, 40, 12}, 999, true},
};

/* ---- k_safari_rest: 80x64 art (rect (28,22), 5x4 on the Safari Zone centre area) -------------------------------------- */
/* The Safari Zone rest houses: the same gold-roofed house as Fuchsia's, one cell narrower. Rows: grass 0-7, the gold roof
 * 8-40 (a level top edge at row 8), the facade with the grey awning, two arched windows and the door 40-64. */
static const RgExact kRestExact[1] = {{0, 8, 80, 64, false}};
static const RgSideCfg kRestSide[1] = {
    {NULL, {8, 41, 72, 43}, {8, 10, 40, 12}, 999, true},
};

const RgSpec rg_kspecs_fuchsia[] = {
    {"k_fuchsia_hall", RG_SPEC_DIRECT, L_FUCHSIA, {13, 28, 5, 4}, {1, 4}, FZ_GROUND, 1, kHallExact, 1,
     k_fuchsia_hall, 0, 0, kHallSide},
    {"k_fuchsia_house", RG_SPEC_DIRECT, L_FUCHSIA, {26, 12, 6, 5}, {0, 0}, FZ_GROUND, 1, kHouseExact, 1,
     k_fuchsia_house, 96, 48, kHouseSide},
    {"k_safari_rest", RG_SPEC_DIRECT, L_SAFARI_C, {28, 22, 5, 4}, {1, 3}, FZ_GROUND, 1, kRestExact, 1,
     k_fuchsia_house, 80, 32, kRestSide},
};
const unsigned rg_kspecs_fuchsia_count = sizeof(rg_kspecs_fuchsia) / sizeof(rg_kspecs_fuchsia[0]);
