/* rg_kspecs_saffron.c -- Saffron City recipes (FireRed / LeafGreen).
 * 3DGBA original work, GPLv3, "Kanto recipes". Phase 34 slice K10 (docs/phase34-frlg/SPEC.md sections 5.3-5.6).
 *
 * Every number below was read off the `romgen author ... art` pictures of the user's own ROM (art row = z - y for the
 * renderer's oblique camera). The builders reuse the Emerald part library (rg_geom) unchanged; every face is a PROJ
 * edge, which copies the art by row, so the ortho check holds by construction where the geometry is right. Side walls
 * come from RgSideCfg (rg_close_sides): a plain patch of each model's own wall art.
 *
 *   k_saffron_silph   9x15 cells (144x240): Silph Co., ten storeys under a purple roof and a glass tube */
#include "rg_bspecs.h"

#include <string.h>

#define L_SAFFRON 207, 0x0730F5C6u
#define SF_GROUND {0x001}

static void sf_pt(double (*poly)[2], unsigned i, double z, double y)
{
    poly[i][0] = z;
    poly[i][1] = y;
}

/* A profile prism: poly points (z, y); each edge PROJ-textured from the listed art rows (0,0 = skipped). */
static bool sf_profile(RgPartList *out, const char *name, double x0, double x1, unsigned n,
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
        sf_pt(pr->poly, i, pts[i][0], pts[i][1]);
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
static bool sf_block(RgPartList *out, const char *name, double x0, double x1, double zf, double ftop, double rtop)
{
    double h = zf - ftop;
    double pts[4][2], rows[4][2];

    sf_pt(pts, 0, zf, 0);
    sf_pt(pts, 1, zf, h);
    sf_pt(pts, 2, rtop + h, h);
    sf_pt(pts, 3, rtop + h, 0);
    rows[0][0] = ftop; rows[0][1] = zf;
    rows[1][0] = rtop; rows[1][1] = ftop;
    rows[2][0] = rows[2][1] = rows[3][0] = rows[3][1] = 0;
    return sf_profile(out, name, x0, x1, 4, (const double (*)[2])pts, (const double (*)[2])rows);
}

/* A shallow box standing in front of a facade: front face art rows fr0..fr1 (height fr1 - fr0, front depth zf), top
 * face art rows top0..top1 (depth top1 - top0). */
static bool sf_porch(RgPartList *out, const char *name, double x0, double x1, double zf, double fr0, double fr1,
                     double top0, double top1)
{
    double h = fr1 - fr0;
    double pts[4][2], rows[4][2];

    sf_pt(pts, 0, zf, 0);
    sf_pt(pts, 1, zf, h);
    sf_pt(pts, 2, zf - (top1 - top0), h);
    sf_pt(pts, 3, zf - (top1 - top0), 0);
    rows[0][0] = fr0; rows[0][1] = fr1;
    rows[1][0] = top0; rows[1][1] = top1;
    rows[2][0] = rows[2][1] = rows[3][0] = rows[3][1] = 0;
    return sf_profile(out, name, x0, x1, 4, (const double (*)[2])pts, (const double (*)[2])rows);
}

/* ---- k_saffron_silph: 144x240 art (rect (29,16), 9x15) ----------------------------------------------------------------- */
/* Silph Co. is drawn as a true elevation. Rows: a rounded roof frame 8-12, the purple roof with its vertical ribs seen
 * from above 12-77 (a level top edge at row 8) on both wings, the glass tube on the roof between them (x 48-96, rows
 * 1-77), a dark cornice 77-85, then ten storeys of window bays down to the yellow base lip at 221-231. Wings: flat
 * blocks 154 high (facade rows 77-231, roof rows 8-77). The centre is the same block with the glass tube on top: the
 * tube's front is a vertical glass face (rows 22-77, 55 high) under a level top (rows 1-22). The entrance canopy
 * stands in front of the centre (front face rows 219-238, top rows 205-219). */
static bool k_saffron_silph(const RgSpec *spec, int a0, int a1, RgPartList *out)
{
    static const double core[5][2] = {{231, 0}, {231, 154}, {231, 209}, {210, 209}, {210, 0}};
    static const double corer[5][2] = {{77, 231}, {22, 77}, {1, 22}, {0, 0}, {0, 0}};

    (void)spec; (void)a0; (void)a1;
    return sf_block(out, "wing_w", 0, 48, 231, 77, 8) &&
           sf_block(out, "wing_e", 96, 144, 231, 77, 8) &&
           sf_profile(out, "tube", 48, 96, 5, core, corer) &&
           sf_porch(out, "porch", 48, 96, 238, 219, 238, 205, 219) && !out->failed;
}
static const RgExact kSilphExact[4] = {
    {0, 8, 48, 231, false},         /* west wing: roof, cornice, ten storeys */
    {96, 8, 144, 231, false},       /* east wing */
    {48, 1, 96, 231, false},        /* the glass tube over the centre bays */
    {48, 205, 96, 238, false},      /* the entrance canopy */
};
static const RgSideCfg kSilphSide[2] = {
    {"tube", {67, 39, 77, 52}, {67, 39, 77, 52}, 999, false},
    {NULL, {51, 14, 53, 72}, {4, 15, 11, 76}, 154, true},   /* the largest flat white / purple rects: a cap is one quad per patch repeat (look L7) */
};

/* ---- the green-roofed houses: 64x80 / 48x80 / 80x80 art (rects (21,10) 4x5, (46,17) 4x5, (26,17) 3x5, (41,34) 5x5) ------- */
/* Rows: ground 0-7, the green roof seen from above 8-40 (a level top edge at row 8; the slanted end faces are painted),
 * a dark eave 40-43, the grey band and the yellow facade with its windows and the purple door 43-78, a dark base line
 * 78-80. One flat block 37 high, a0 wide (the third house is cut off by Silph Co. on its east side, the fourth is a
 * cell wider). */
static bool k_saffron_house(const RgSpec *spec, int a0, int a1, RgPartList *out)
{
    (void)spec; (void)a1;
    return sf_block(out, "house", 0, a0, 80, 43, 8) && !out->failed;
}
static const RgExact kHouse4Exact[1] = {{0, 8, 64, 80, false}};
static const RgExact kHouse3Exact[1] = {{0, 8, 48, 80, false}};
static const RgExact kHouse5Exact[1] = {{0, 8, 80, 80, false}};
static const RgSideCfg kHouseSide[1] = {
    {NULL, {8, 57, 56, 60}, {12, 29, 51, 35}, 999, true},
};
static const RgSideCfg kHouse3Side[1] = {          /* 48 px wide: the tiles stay inside the art */
    {NULL, {8, 57, 40, 60}, {12, 29, 40, 35}, 999, true},
};

/* ---- k_saffron_dojo: 96x80 art (rect (37,8), 6x5; door (40,12)) ------------------------------------------------------- */
/* The Fighting Dojo, drawn as an elevation with a large flat tan roof. Rows: trees 0-8, the cream roof rim 9-23, the tan
 * tiles 24-51, a dark eave 51-53, then the low facade with its windows and the grey-green ledge 53-72. The entrance
 * porch (x 40-72) stands in front: its tan roof with the Poke Ball sign (rows 48-63) over the dark door (rows 63-79).
 * One flat block 19 high (roof rows 9-53, so the roof is 44 deep) plus the porch box. */
static bool k_saffron_dojo(const RgSpec *spec, int a0, int a1, RgPartList *out)
{
    (void)spec; (void)a0; (void)a1;
    return sf_block(out, "hall", 0, 96, 72, 53, 9) &&
           sf_porch(out, "porch", 40, 72, 79, 63, 79, 48, 63) && !out->failed;
}
static const RgExact kDojoExact[2] = {{0, 9, 96, 72, false}, {40, 48, 72, 79, false}};
static const RgSideCfg kDojoSide[2] = {
    {"porch", {48, 64, 64, 78}, {48, 64, 64, 78}, 999, false},
    {NULL, {72, 57, 80, 64}, {4, 35, 92, 37}, 999, true},
};

/* ---- k_saffron_gym: 112x80 art (rect (43,8), 7x5; door (46,12)) ------------------------------------------------------- */
/* The Saffron Gym: the Dojo's big sibling, the same style. Rows: the tan roof with its louvres 1-42, a dark bevelled
 * eave 42-47, the white facade with two window triples and the GYM sign 47-71, and the same entrance porch (x 40-72,
 * rows 48-79). One flat block 24 high (roof rows 1-47, 46 deep) plus the porch box. */
static bool k_saffron_gym(const RgSpec *spec, int a0, int a1, RgPartList *out)
{
    (void)spec; (void)a0; (void)a1;
    return sf_block(out, "hall", 0, 112, 71, 47, 1) &&
           sf_porch(out, "porch", 40, 72, 79, 63, 79, 48, 63) && !out->failed;
}
static const RgExact kGymExact[2] = {{0, 1, 112, 71, false}, {40, 48, 72, 79, false}};
static const RgSideCfg kGymSide[2] = {
    {"porch", {49, 68, 55, 73}, {44, 57, 50, 62}, 63, false},
    {NULL, {7, 56, 40, 60}, {6, 39, 106, 41}, 999, true},
};

/* ---- k_saffron_gate / k_saffron_gate_s: 128x96 art (rects (31,0) 8x6 and (31,46) 8x7; doors (34,5), (35,5) and (34,46), (35,46)) ---- */
/* Saffron's own half of the two Route gatehouses (the north one to Route 5, the south one to Route 6): the same 96x96
 * art in both places (x 16-112 of the rect; the rect takes one ground column each side, because the bare 6x6 cells
 * also occur on Routes 5 and 6, whose halves K6 models with a canopy, and a second model must not land there). The south
 * rect starts one row higher, on the door cells at y 46, so every shape row is shifted by 16 (a1). Rows: the slate roof with its ribs 0-43 (level top), the cornice 43-48, the window band 51-62, yellow
 * brick 62-90 and the grey base, over the full 96 px between two white pillars (x 0-8 and 88-96). The lintel and the
 * dark arch (x 16-80, rows 68-96) are painted on the facade. One flat block 53 high. */
static bool k_saffron_gate(const RgSpec *spec, int a0, int a1, RgPartList *out)
{
    (void)spec; (void)a0;
    return sf_block(out, "gate", 16, 112, 96 + a1, 43 + a1, a1) && !out->failed;   /* a1 = the row shift */
}
static const RgExact kGateExact[1] = {{16, 0, 112, 96, false}};
static const RgExact kGateSExact[1] = {{16, 16, 112, 112, false}};
static const RgSideCfg kGateSide[1] = {
    {NULL, {24, 89, 32, 93}, {96, 2, 103, 42}, 999, true},
};
static const RgSideCfg kGateSSide[1] = {
    {NULL, {24, 105, 32, 109}, {96, 18, 103, 58}, 999, true},
};

const RgSpec rg_kspecs_saffron[] = {
    {"k_saffron_silph", RG_SPEC_DIRECT, L_SAFFRON, {29, 16, 9, 15}, {0, 0}, SF_GROUND, 1, kSilphExact, 4,
     k_saffron_silph, 0, 0, kSilphSide},
    {"k_saffron_house", RG_SPEC_DIRECT, L_SAFFRON, {21, 10, 4, 5}, {1, 5}, SF_GROUND, 1, kHouse4Exact, 1,
     k_saffron_house, 64, 0, kHouseSide},
    {"k_saffron_house3", RG_SPEC_DIRECT, L_SAFFRON, {26, 17, 3, 5}, {1, 5}, SF_GROUND, 1, kHouse3Exact, 1,
     k_saffron_house, 48, 0, kHouse3Side},
    {"k_saffron_house5", RG_SPEC_DIRECT, L_SAFFRON, {41, 34, 5, 5}, {1, 5}, SF_GROUND, 1, kHouse5Exact, 1,
     k_saffron_house, 80, 0, kHouseSide},
    {"k_saffron_dojo", RG_SPEC_DIRECT, L_SAFFRON, {37, 8, 6, 5}, {0, 0}, SF_GROUND, 1, kDojoExact, 2,
     k_saffron_dojo, 0, 0, kDojoSide},
    {"k_saffron_gym", RG_SPEC_DIRECT, L_SAFFRON, {43, 8, 7, 5}, {0, 0}, SF_GROUND, 1, kGymExact, 2,
     k_saffron_gym, 0, 0, kGymSide},
    {"k_saffron_gate", RG_SPEC_DIRECT, L_SAFFRON, {31, 0, 8, 6}, {0, 0}, SF_GROUND, 1, kGateExact, 1,
     k_saffron_gate, 0, 0, kGateSide},
    {"k_saffron_gate_s", RG_SPEC_DIRECT, L_SAFFRON, {31, 46, 8, 7}, {0, 0}, SF_GROUND, 1, kGateSExact, 1,
     k_saffron_gate, 0, 16, kGateSSide},
};
const unsigned rg_kspecs_saffron_count = sizeof(rg_kspecs_saffron) / sizeof(rg_kspecs_saffron[0]);
