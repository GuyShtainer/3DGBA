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
    {NULL, {4, 120, 8, 124}, {4, 20, 10, 70}, 154, true},
};

const RgSpec rg_kspecs_saffron[] = {
    {"k_saffron_silph", RG_SPEC_DIRECT, L_SAFFRON, {29, 16, 9, 15}, {0, 0}, SF_GROUND, 1, kSilphExact, 4,
     k_saffron_silph, 0, 0, kSilphSide},
};
const unsigned rg_kspecs_saffron_count = sizeof(rg_kspecs_saffron) / sizeof(rg_kspecs_saffron[0]);
