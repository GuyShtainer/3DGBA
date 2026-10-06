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

static const RgExact kPewterHouseExact[2] = {
    {0, 9, 80, 48, false},      /* ridge cap, slope, fascia */
    {0, 48, 80, 64, false},     /* facade */
};

const RgSpec rg_kspecs_pewter[] = {
    {"k_pewter_house", RG_SPEC_DIRECT, L_PEWTER, {32, 8, 5, 4}, {0, 0}, {0x001}, 1, kPewterHouseExact, 2,
     k_pewter_house, 0, 0, NULL},
};
const unsigned rg_kspecs_pewter_count = sizeof(rg_kspecs_pewter) / sizeof(rg_kspecs_pewter[0]);
