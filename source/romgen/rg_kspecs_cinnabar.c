/* rg_kspecs_cinnabar.c -- Cinnabar Island, Indigo Plateau and Route 22 / 23 recipes (FireRed / LeafGreen).
 * 3DGBA original work, GPLv3, "Kanto recipes". Phase 34 slice K11 (docs/phase34-frlg/SPEC.md sections 5.3-5.6).
 *
 * Every number below was read off the `romgen author ... art` pictures of the user's own ROM (art row = z - y for the
 * renderer's oblique camera). The builders reuse the Emerald part library (rg_geom) unchanged; every face is a PROJ
 * edge, which copies the art by row, so the ortho check holds by construction where the geometry is right. Side walls
 * come from RgSideCfg (rg_close_sides): a plain patch of each model's own wall art.
 *
 *   k_cinnabar_mansion   7x4 cells (112x64): the Pokemon Mansion, a two-storey hall under a brown roof */
#include "rg_bspecs.h"

#include <string.h>

#define L_CINNABAR 86, 0xC8348D4Bu
#define CB_GROUND {0x001}

static void cb_pt(double (*poly)[2], unsigned i, double z, double y)
{
    poly[i][0] = z;
    poly[i][1] = y;
}

/* A profile prism: poly points (z, y); each edge PROJ-textured from the listed art rows (0,0 = skipped). */
static bool cb_profile(RgPartList *out, const char *name, double x0, double x1, unsigned n,
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
        cb_pt(pr->poly, i, pts[i][0], pts[i][1]);
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
static bool cb_block(RgPartList *out, const char *name, double x0, double x1, double zf, double ftop, double rtop)
{
    double h = zf - ftop;
    double pts[4][2], rows[4][2];

    cb_pt(pts, 0, zf, 0);
    cb_pt(pts, 1, zf, h);
    cb_pt(pts, 2, rtop + h, h);
    cb_pt(pts, 3, rtop + h, 0);
    rows[0][0] = ftop; rows[0][1] = zf;
    rows[1][0] = rtop; rows[1][1] = ftop;
    rows[2][0] = rows[2][1] = rows[3][0] = rows[3][1] = 0;
    return cb_profile(out, name, x0, x1, 4, (const double (*)[2])pts, (const double (*)[2])rows);
}

/* A shallow box standing in front of a facade: front face art rows fr0..fr1 (height fr1 - fr0, front depth zf), top
 * face art rows top0..top1 (depth top1 - top0). */
static bool cb_porch(RgPartList *out, const char *name, double x0, double x1, double zf, double fr0, double fr1,
                     double top0, double top1)
{
    double h = fr1 - fr0;
    double pts[4][2], rows[4][2];

    cb_pt(pts, 0, zf, 0);
    cb_pt(pts, 1, zf, h);
    cb_pt(pts, 2, zf - (top1 - top0), h);
    cb_pt(pts, 3, zf - (top1 - top0), 0);
    rows[0][0] = fr0; rows[0][1] = fr1;
    rows[1][0] = top0; rows[1][1] = top1;
    rows[2][0] = rows[2][1] = rows[3][0] = rows[3][1] = 0;
    return cb_profile(out, name, x0, x1, 4, (const double (*)[2])pts, (const double (*)[2])rows);
}

/* ---- k_cinnabar_mansion: 112x64 art (rect (5,0), 7x4; door (8,3)) --------------------------------------------------- */
static bool k_cinnabar_mansion(const RgSpec *spec, int a0, int a1, RgPartList *out)
{
    (void)spec; (void)a0; (void)a1;
    return cb_block(out, "hall", 5, 112, 64, 28, 0) && !out->failed;
}
static const RgExact kMansionExact[1] = {{5, 0, 112, 64, false}};
static const RgSideCfg kMansionSide[1] = {
    {NULL, {8, 45, 40, 48}, {64, 0, 80, 24}, 999, true},
};

/* ---- k_cinnabar_lab: 112x64 art (rect (5,6), 7x4; door (8,9)) --------------------------------------------------------- */
/* The Pokemon Lab, a rounded hall drawn as a front elevation: the roof is a cream barrel with a red walkway down the
 * middle (rows 0-40, x 48-64), the wall below it carries the small windows and the glass door (rows 40-64), four
 * pilasters run the full height. The silhouette is round at both ends, so the ends are 4-px slices that step in and
 * get lower: slice i has its roof top at row t[i] and its wall bottom at row b[i]; the centre is one block. The art
 * outside the silhouette is the sand and grass of the ground, which the model never covers. */
static bool k_cinnabar_lab(const RgSpec *spec, int a0, int a1, RgPartList *out)
{
    static const double t[4] = {14, 8, 5, 2};
    static const double b[4] = {50, 54, 57, 60};
    unsigned i;

    (void)spec; (void)a0; (void)a1;
    for (i = 0; i < 4; i++)
        if (!cb_block(out, "end_w", 4.0 * i, 4.0 * i + 4, b[i], 40, t[i]) ||
            !cb_block(out, "end_e", 112.0 - 4.0 * i - 4, 112.0 - 4.0 * i, b[i], 40, t[i]))
            return false;
    return cb_block(out, "body", 16, 96, 64, 40, 0) && !out->failed;
}
static const RgExact kLabExact[9] = {
    {16, 0, 96, 64, false},         /* the body: roof, walkway, pilasters, wall, door */
    {1, 15, 4, 49, false},          /* the rounded ends, one margin pixel inside the silhouette */
    {5, 9, 8, 53, false},
    {9, 6, 12, 56, false},
    {13, 3, 16, 59, false},
    {108, 15, 111, 49, false},
    {104, 9, 107, 53, false},
    {100, 6, 103, 56, false},
    {96, 3, 99, 59, false},
};
static const RgSideCfg kLabSide[1] = {
    {NULL, {17, 41, 23, 62}, {26, 26, 40, 32}, 999, true},
};

const RgSpec rg_kspecs_cinnabar[] = {
    {"k_cinnabar_mansion", RG_SPEC_DIRECT, L_CINNABAR, {5, 0, 7, 4}, {0, 0}, CB_GROUND, 1, kMansionExact, 1,
     k_cinnabar_mansion, 0, 0, kMansionSide},
    {"k_cinnabar_lab", RG_SPEC_DIRECT, L_CINNABAR, {5, 6, 7, 4}, {0, 0}, CB_GROUND, 1, kLabExact, 9,
     k_cinnabar_lab, 0, 0, kLabSide},
};
const unsigned rg_kspecs_cinnabar_count = sizeof(rg_kspecs_cinnabar) / sizeof(rg_kspecs_cinnabar[0]);
