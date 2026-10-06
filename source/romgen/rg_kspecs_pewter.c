/* rg_kspecs_pewter.c -- Pewter City recipes (FireRed / LeafGreen).
 * 3DGBA original work, GPLv3, "Kanto recipes". Phase 34 slice K4 (docs/phase34-frlg/SPEC.md sections 5.3-5.6).
 *
 * Every number below was read off the `romgen author ... art` pictures of the user's own ROM (art row = z - y for the
 * renderer's oblique camera). The builders reuse the Emerald part library (rg_geom) unchanged; most faces are PROJ
 * edges, which copy the art by row, so the ortho check holds by construction where the geometry is right.
 *
 *   k_pewter_house     5x4 cells (80x64): the grey slate house, two placements (layout 80).
 *   k_pewter_museum    16x7 cells: the Pewter Museum with its right wing (layout 80). */
#include "rg_bspecs.h"

#include <string.h>

#define L_PEWTER 80, 0xAAB0C96Cu

static void pw_pt(double (*poly)[2], unsigned i, double z, double y)
{
    poly[i][0] = z;
    poly[i][1] = y;
}

/* ---- k_pewter_house: 80x64 art (rect (32,8), 5x4) ------------------------------------------------------------------ */
/* Rows: grass 0-8, white ridge cap 9-21, slate front slope 22-41, fascia 42-47, facade with door and window 48-63. */
static bool k_pewter_house(const RgSpec *spec, int a0, int a1, RgPartList *out)
{
    RgPart *body = rg_parts_add(out, RG_P_PRISM, "body");
    RgPart *roof = rg_parts_add(out, RG_P_PRISM, "roof");
    RgPrism *pr;

    (void)spec; (void)a0; (void)a1;
    if (body == NULL || roof == NULL)
        return false;
    pr = &body->u.prism;
    pr->x0 = 0; pr->x1 = 80;
    pr->west = pr->east = true;
    pr->nPoly = 4;
    pw_pt(pr->poly, 0, 64, 0);
    pw_pt(pr->poly, 1, 64, 16);
    pw_pt(pr->poly, 2, 24, 16);
    pw_pt(pr->poly, 3, 24, 0);
    pr->edges[0].kind = RG_EM_PROJ;
    pr->edges[0].proj = rg_proj_rows(48, 64);
    pr->skip = (1u << 1) | (1u << 2) | (1u << 3);

    pr = &roof->u.prism;
    pr->x0 = 0; pr->x1 = 80;
    pr->west = pr->east = true;
    pr->nPoly = 5;
    pw_pt(pr->poly, 0, 64, 16);
    pw_pt(pr->poly, 1, 64, 22);
    pw_pt(pr->poly, 2, 52, 30);
    pw_pt(pr->poly, 3, 39, 30);
    pw_pt(pr->poly, 4, 39, 16);
    pr->edges[0].kind = RG_EM_PROJ;
    pr->edges[0].proj = rg_proj_rows(42, 48);       /* fascia */
    pr->edges[1].kind = RG_EM_PROJ;
    pr->edges[1].proj = rg_proj_rows(22, 42);       /* front slope */
    pr->edges[2].kind = RG_EM_PROJ;
    pr->edges[2].proj = rg_proj_rows(9, 22);        /* the level ridge cap */
    pr->skip = (1u << 3) | (1u << 4);
    return !out->failed;
}


/* A slice of a stepped profile: poly points (z, y), each edge PROJ-textured from the listed art rows (0,0 = skipped). */
static bool pw_profile(RgPartList *out, const char *name, double x0, double x1, unsigned n, const double (*pts)[2],
                       const double (*rows)[2])
{
    RgPart *pt = rg_parts_add(out, RG_P_PRISM, name);
    RgPrism *pr;
    unsigned i;

    if (pt == NULL)
        return false;
    pr = &pt->u.prism;
    pr->x0 = x0; pr->x1 = x1;
    pr->west = pr->east = true;
    pr->nPoly = n;
    pr->skip = 0;
    for (i = 0; i < n; i++) {
        pw_pt(pr->poly, i, pts[i][0], pts[i][1]);
        if (rows[i][0] == 0 && rows[i][1] == 0) {
            pr->skip |= 1u << i;
        } else {
            pr->edges[i].kind = RG_EM_PROJ;
            pr->edges[i].proj = rg_proj_rows(rows[i][0], rows[i][1]);
        }
    }
    return true;
}

/* A thin vertical card at depth z, height y0..y1, textured from the art rows rlo..rhi. */
static bool pw_card(RgPartList *out, const char *name, double x0, double x1, double z, double y0, double y1,
                    double rlo, double rhi)
{
    RgPart *pt = rg_parts_add(out, RG_P_PRISM, name);
    RgPrism *pr;

    if (pt == NULL)
        return false;
    pr = &pt->u.prism;
    pr->x0 = x0; pr->x1 = x1;
    pr->west = pr->east = false;
    pr->nPoly = 4;
    pw_pt(pr->poly, 0, z, y0);
    pw_pt(pr->poly, 1, z, y1);
    pw_pt(pr->poly, 2, z - 1, y1);
    pw_pt(pr->poly, 3, z - 1, y0);
    pr->edges[0].kind = RG_EM_PROJ;
    pr->edges[0].proj = rg_proj_rows(rlo, rhi);
    pr->edges[1].kind = RG_EM_PROJ;                 /* the 1-px top: gives the cell a height (rg_cell_heights) */
    pr->edges[1].proj = rg_proj_rows(rlo - 1, rlo);
    pr->skip = (1u << 2) | (1u << 3);
    return true;
}

/* ---- k_pewter_museum: 256x112 art (rect (12,0), 16x7) -------------------------------------------------------------- */
/* Left hall x 0-175: trees 0-8, grey edge 9, light pink cap 10-23, magenta front slope 24-47, cornice with windows
 * 48-54, lower awning 55-71, facade with arched windows 72-95. The entrance porch (x 64-111) stands in front: its roof
 * rows 76-95 (a low ramp, so the hall wall does not hide it), its arches and door rows 96-111. The right wing (x
 * 176-255): grey edge 17, light cap 18-31, slope 32-57, fascia 58-61, facade with window and red door 62-79. Rows
 * 96-111 are hedge bushes, grass and the log fence: they stay flat ground. */
static bool k_pewter_museum(const RgSpec *spec, int a0, int a1, RgPartList *out)
{
    static const double hall[7][2] = {{96, 0}, {96, 24}, {88, 33}, {88, 40}, {76, 52}, {61, 52}, {61, 0}};
    static const double hallRows[7][2] = {{72, 96}, {55, 72}, {48, 55}, {24, 48}, {9, 24}, {0, 0}, {0, 0}};
    static const double wing[6][2] = {{80, 0}, {80, 18}, {80, 22}, {66, 34}, {51, 34}, {51, 0}};
    static const double wingRows[6][2] = {{62, 80}, {58, 62}, {32, 58}, {17, 32}, {0, 0}, {0, 0}};
    static const double porch[4][2] = {{112, 0}, {112, 16}, {98, 22}, {98, 0}};
    static const double porchRows[4][2] = {{96, 112}, {76, 96}, {0, 0}, {0, 0}};

    unsigned k;

    (void)spec; (void)a0; (void)a1;
    for (k = 0; k < 8; k++) {                       /* the hedge: one flat card per bush cell (x 0-63 and 112-175) */
        double x0 = (k < 4 ? 0 : 112 - 64) + k * 16.0;

        if (!pw_card(out, "bush", x0, x0 + 16, 112, 0, 16, 96, 112))
            return false;
    }
    return pw_profile(out, "hall", 0, 176, 7, hall, hallRows) && pw_profile(out, "wing", 176, 256, 6, wing, wingRows) &&
           pw_profile(out, "porch", 64, 112, 4, porch, porchRows) && !out->failed;
}

static const RgExact kPewterHouseExact[2] = {
    {0, 9, 80, 48, false},      /* ridge cap, slope, fascia */
    {0, 48, 80, 64, false},     /* facade */
};

static const RgExact kPewterMuseumExact[5] = {
    {0, 9, 176, 96, false},     /* the hall: roof, cornice, awning, facade (and the porch roof) */
    {176, 17, 256, 80, false},  /* the wing */
    {64, 96, 112, 112, false},  /* the porch arches and door */
    {0, 96, 64, 112, false},    /* hedge, west */
    {112, 96, 176, 112, false}, /* hedge, east */
};

/* Phase 34 side walls: the end-face patches of each model (art coordinates), see rg_close_sides. */
static const RgSideCfg kPewterHouseSide[1] = {
    {NULL, {55, 51, 72, 54}, {4, 24, 76, 27}, 16, true},
};
static const RgSideCfg kMuseumSide[3] = {
    {"hall", {71, 84, 107, 89}, {16, 24, 19, 48}, 40, false},
    {"wing", {199, 64, 208, 74}, {16, 24, 19, 48}, 22, false},
    {"porch", {71, 84, 107, 89}, {71, 84, 107, 89}, 16, true},
};

const RgSpec rg_kspecs_pewter[] = {
    {"k_pewter_house", RG_SPEC_DIRECT, L_PEWTER, {32, 8, 5, 4}, {0, 0}, {0x001}, 1, kPewterHouseExact, 2,
     k_pewter_house, 0, 0, kPewterHouseSide},
    {"k_pewter_museum", RG_SPEC_DIRECT, L_PEWTER, {12, 0, 16, 7}, {0, 0}, {0x001}, 1, kPewterMuseumExact, 5,
     k_pewter_museum, 0, 0, kMuseumSide},
};
const unsigned rg_kspecs_pewter_count = sizeof(rg_kspecs_pewter) / sizeof(rg_kspecs_pewter[0]);
