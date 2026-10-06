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

/* A tall slab seen only from the front: the facade is art rows ftop..zf (a vertical face), the top is a smear of the
 * art rows just under the roof line, and the slab runs `depth` back from the facade. The ROM art has no roof for these
 * (the camera sees the front only), so the top is invented. */
static bool cl_slab(RgPartList *out, const char *name, double x0, double x1, double zf, double ftop, double depth,
                    double smear0)
{
    double h = zf - ftop;
    double pts[4][2], rows[4][2];

    cl_pt(pts, 0, zf, 0);
    cl_pt(pts, 1, zf, h);
    cl_pt(pts, 2, zf - depth, h);
    cl_pt(pts, 3, zf - depth, 0);
    rows[0][0] = ftop; rows[0][1] = zf;
    rows[1][0] = smear0; rows[1][1] = smear0 + 1;
    rows[2][0] = rows[2][1] = rows[3][0] = rows[3][1] = 0;
    return cl_profile(out, name, x0, x1, 4, (const double (*)[2])pts, (const double (*)[2])rows);
}

/* A shallow box standing in front of a facade: front face art rows fr0..fr1 (height fr1 - fr0, front depth zf), top
 * face art rows top0..top1 (depth top1 - top0). */
static bool cl_porch(RgPartList *out, const char *name, double x0, double x1, double zf, double fr0, double fr1,
                     double top0, double top1)
{
    double h = fr1 - fr0;
    double pts[4][2], rows[4][2];

    cl_pt(pts, 0, zf, 0);
    cl_pt(pts, 1, zf, h);
    cl_pt(pts, 2, zf - (top1 - top0), h);
    cl_pt(pts, 3, zf - (top1 - top0), 0);
    rows[0][0] = fr0; rows[0][1] = fr1;
    rows[1][0] = top0; rows[1][1] = top1;
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

/* ---- k_celadon_game_corner: 112x96 art (rect (31,17), 7x6) ----------------------------------------------------------- */
/* The Rocket Game Corner: a wide building drawn as an elevation. Rows: the fence posts 0-7, the cream upper wall with
 * two rows of windows 9-46, a dark tan cornice 47-55, the orange base with its diamond reliefs 55-72 and the green
 * ledge 72-79. The purple arched entrance stands in front: its dome (rows 44-55) over the face with the door and the
 * two grey pillars (rows 55-85). One slab (71 high, 40 deep) plus the porch box. */
static bool k_celadon_game_corner(const RgSpec *spec, int a0, int a1, RgPartList *out)
{
    (void)spec; (void)a0; (void)a1;
    return cl_slab(out, "hall", 0, 112, 79, 8, 40, 26) &&
           cl_porch(out, "porch", 39, 73, 86, 56, 86, 44, 56) && !out->failed;
}
static const RgExact kGameExact[2] = {{0, 8, 112, 79, false}, {39, 44, 73, 86, false}};
static const RgSideCfg kGameSide[2] = {
    {"porch", {40, 61, 48, 64}, {40, 61, 48, 64}, 999, false},
    {NULL, {2, 24, 110, 32}, {2, 24, 110, 32}, 999, true},
};

/* ---- k_celadon_prize: 48x64 art (rect (38,17), 3x4) -------------------------------------------------------------------- */
/* The Prize Room: the Game Corner's little sibling in the same style: cream wall with a window pair 9-31, tan cornice
 * 32-39, orange base with the double door 40-55, green ledge 56-63. One slab, 55 high, 30 deep. */
static bool k_celadon_prize(const RgSpec *spec, int a0, int a1, RgPartList *out)
{
    (void)spec; (void)a0; (void)a1;
    return cl_slab(out, "prize", 0, 48, 63, 8, 30, 26) && !out->failed;
}
static const RgExact kPrizeExact[1] = {{0, 8, 48, 63, false}};
static const RgSideCfg kPrizeSide[1] = {
    {NULL, {16, 10, 32, 30}, {16, 10, 32, 30}, 999, true},
};

/* ---- k_celadon_dept: 336x192 art (rect (4,4), 21x12) ------------------------------------------------------------------- */
/* The Dept. Store and the two plain wings beside it (the census seed is one flood fill of all three). The ROM draws it
 * as a true elevation: rows 16-78 the grey louvred roof seen from above (a yellow-panelled penthouse on it, front
 * rows 56-70, top rows 17-56), a bevelled cornice 78-82, then six storeys of window bays down to the ledge at 178-181.
 * The store is a flat block 103 high; its penthouse stands 14 higher on the roof. The wings (x 0-80 and 224-336) are
 * flat blocks 69 high: roof rows 8-59, green facade 59-128. The doors at (11,14) and (15,14) are the two awnings. */
static bool k_celadon_dept(const RgSpec *spec, int a0, int a1, RgPartList *out)
{
    static const double pen[4][2] = {{173, 0}, {173, 117}, {134, 117}, {134, 0}};
    static const double penr[4][2] = {{56, 173}, {17, 56}, {0, 0}, {0, 0}};

    (void)spec; (void)a0; (void)a1;
    return cl_block(out, "dept", 81, 223, 181, 78, 16) &&
           cl_profile(out, "pent", 112, 192, 4, pen, penr) &&
           cl_block(out, "wing_w", 0, 80, 128, 59, 8) &&
           cl_block(out, "wing_e", 224, 336, 128, 59, 8) && !out->failed;
}
static const RgExact kDeptExact[4] = {
    {81, 16, 223, 181, false},      /* roof, cornice, the six storeys */
    {112, 17, 192, 71, false},      /* the penthouse */
    {0, 8, 80, 128, false},         /* the west wing */
    {224, 8, 336, 128, false},      /* the east wing */
};
static const RgSideCfg kDeptSide[3] = {
    {"pent", {113, 59, 191, 61}, {113, 59, 191, 61}, 999, false},
    {"dept", {128, 82, 176, 87}, {128, 82, 176, 87}, 999, false},
    {NULL, {3, 60, 5, 121}, {3, 60, 5, 121}, 999, true},
};

/* ---- k_celadon_mansion: 112x144 art (rect (27,3), 7x9) ----------------------------------------------------------------- */
/* Celadon Mansion (the Condominiums): a flat-roofed block, a roof of olive tiles seen from above (rows 24-75, a vent
 * hood on it) over four storeys of windows between two purple pilaster strips, the wooden door at the foot (rows
 * 128-144). One flat block 69 high. The three roof doors above it (cells (29,5), (30,4), (31,5)) are ground art. */
static bool k_celadon_mansion(const RgSpec *spec, int a0, int a1, RgPartList *out)
{
    (void)spec; (void)a0; (void)a1;
    return cl_block(out, "mansion", 0, 112, 144, 75, 24) && !out->failed;
}
static const RgExact kMansionExact[1] = {{0, 24, 112, 144, false}};
static const RgSideCfg kMansionSide[1] = {
    {NULL, {3, 76, 5, 136}, {3, 76, 5, 136}, 999, true},
};

const RgSpec rg_kspecs_celadon[] = {
    {"k_celadon_house", RG_SPEC_DIRECT, L_CELADON, {36, 25, 4, 5}, {1, 5}, CL_GROUND, 1, kHouseExact, 1,
     k_celadon_house, 0, 0, kHouseSide},
    {"k_celadon_game_corner", RG_SPEC_DIRECT, L_CELADON, {31, 17, 7, 6}, {0, 0}, CL_GROUND, 1, kGameExact, 2,
     k_celadon_game_corner, 0, 0, kGameSide},
    {"k_celadon_prize", RG_SPEC_DIRECT, L_CELADON, {38, 17, 3, 4}, {0, 0}, CL_GROUND, 1, kPrizeExact, 1,
     k_celadon_prize, 0, 0, kPrizeSide},
    {"k_celadon_dept", RG_SPEC_DIRECT, L_CELADON, {4, 4, 21, 12}, {0, 0}, CL_GROUND, 1, kDeptExact, 4,
     k_celadon_dept, 0, 0, kDeptSide},
    {"k_celadon_mansion", RG_SPEC_DIRECT, L_CELADON, {27, 3, 7, 9}, {0, 0}, CL_GROUND, 1, kMansionExact, 1,
     k_celadon_mansion, 0, 0, kMansionSide},
};
const unsigned rg_kspecs_celadon_count = sizeof(rg_kspecs_celadon) / sizeof(rg_kspecs_celadon[0]);
