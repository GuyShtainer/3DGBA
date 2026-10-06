/* rg_kspecs_viridian.c -- Viridian City, Route 2 and Viridian Forest gate recipes (FireRed / LeafGreen).
 * 3DGBA original work, GPLv3, "Kanto recipes". Phase 34 slice K3 (docs/phase34-frlg/SPEC.md sections 5.3-5.6).
 *
 * Every number below was read off the `romgen author ... art` pictures of the user's own ROM (art row = z - y for the
 * renderer's oblique camera). The builders reuse the Emerald part library (rg_geom) unchanged; most faces are PROJ
 * edges, which copy the art by row, so the ortho check holds by construction where the geometry is right.
 *
 *   k_viridian_house   5x4 cells (80x64): gabled shingle house with a chimney (layout 79). */
#include "rg_bspecs.h"

#include <string.h>

#define VR_GRASS 0x001
#define L_VIRIDIAN_K3 79, 0x9925DC24u

static void vr_pt(double (*poly)[2], unsigned i, double z, double y)
{
    poly[i][0] = z;
    poly[i][1] = y;
}

/* One x-slice of the gable roof. The slice under the chimney takes a clean strip instead of the art, so the painted
 * chimney does not ghost onto the slope. Edge 0 fascia, 1 front slope, 2 back slope (a strip), the rest skipped. */
static void vr_roof(RgPart *part, double x0, double x1, double over, double eave, double wall, double zr, double ridge,
                    double zb, bool clean)
{
    RgPrism *pr = &part->u.prism;
    RgStrip st;

    pr->x0 = x0; pr->x1 = x1;
    pr->west = (x0 == 0);
    pr->east = (x1 == 80);
    pr->nPoly = 5;
    vr_pt(pr->poly, 0, over, wall);
    vr_pt(pr->poly, 1, over, eave);
    vr_pt(pr->poly, 2, zr, ridge);
    vr_pt(pr->poly, 3, zb, eave);
    vr_pt(pr->poly, 4, zb, wall);
    pr->edges[0].kind = RG_EM_PROJ;
    pr->edges[0].proj = rg_proj_rows(42, 48);       /* fascia */
    if (!clean) {
        pr->edges[1].kind = RG_EM_PROJ;
        pr->edges[1].proj = rg_proj_rows(20, 42);   /* front slope */
    } else {
        memset(&st, 0, sizeof(st));
        st.fixed[0] = 22; st.fixed[1] = 24;
        st.hasRepeat = true; st.repeat[0] = 22; st.repeat[1] = 24;
        st.hasWrap = true; st.wrap[0] = 1; st.wrap[1] = 45;     /* clean columns, a multiple of the stripe period */
        pr->edges[1].kind = RG_EM_STRIP;
        pr->edges[1].strip = rg_strip_fin(st);
    }
    memset(&st, 0, sizeof(st));
    st.fixed[0] = 16; st.fixed[1] = 20;             /* the ridge cap */
    st.hasRepeat = true; st.repeat[0] = 10; st.repeat[1] = 16;
    if (clean) {
        st.hasWrap = true; st.wrap[0] = 1; st.wrap[1] = 45;
    }
    pr->edges[2].kind = RG_EM_STRIP;
    pr->edges[2].strip = rg_strip_fin(st);          /* back slope */
    pr->skip = (1u << 3) | (1u << 4);
}

/* ---- k_viridian_house: 80x64 art ---------------------------------------------------------------------------------- */
/* Art rows (rect rows): back slope 8-17, ridge cap 16-19, front slope 20-41, fascia 42-47, facade 48-63. The chimney
 * stands on the right: top face rows 8-21 (columns 47-64), front face rows 22-39. */
static bool k_viridian_house(const RgSpec *spec, int a0, int a1, RgPartList *out)
{
    double front = 64, over = 66, back = 24, wall = 18, eave = 24, zr = 49.6, ridge = 29.6, zb = 32;
    RgPart *body = rg_parts_add(out, RG_P_PRISM, "body");
    RgPart *roof = rg_parts_add(out, RG_P_PRISM, "roof");
    RgPart *roof2 = rg_parts_add(out, RG_P_PRISM, "roof_mid");
    RgPart *roof3 = rg_parts_add(out, RG_P_PRISM, "roof_e");
    RgPart *chim = rg_parts_add(out, RG_P_PRISM, "chimney");
    RgPrism *pr;

    (void)spec; (void)a1;
    if (body == NULL || roof == NULL || roof2 == NULL || roof3 == NULL || chim == NULL)
        return false;
    pr = &body->u.prism;
    pr->x0 = 0; pr->x1 = 80;
    pr->west = pr->east = true;
    pr->nPoly = 4;
    vr_pt(pr->poly, 0, front, 0);
    vr_pt(pr->poly, 1, front, wall);
    vr_pt(pr->poly, 2, back, wall);
    vr_pt(pr->poly, 3, back, 0);
    pr->edges[0].kind = RG_EM_PROJ;
    pr->edges[0].proj = rg_proj_rows(48, 64);
    pr->skip = (1u << 1) | (1u << 2) | (1u << 3);

    vr_roof(roof, 0, 47, over, eave, wall, zr, ridge, zb, false);
    vr_roof(roof2, 47, 65, over, eave, wall, zr, ridge, zb, true);
    vr_roof(roof3, 65, 80, over, eave, wall, zr, ridge, zb, false);

    pr = &chim->u.prism;
    pr->x0 = 47; pr->x1 = 65;
    pr->west = pr->east = true;
    pr->nPoly = 4;
    vr_pt(pr->poly, 0, 65, 25);
    vr_pt(pr->poly, 1, 65, 43);
    vr_pt(pr->poly, 2, 51, 43);
    vr_pt(pr->poly, 3, 51, 25);
    pr->edges[0].kind = RG_EM_PROJ;
    pr->edges[0].proj = rg_proj_rows(22, 40);       /* front face */
    pr->edges[1].kind = RG_EM_PROJ;
    pr->edges[1].proj = rg_proj_rows(8, 22);        /* top face */
    pr->skip = (1u << 2) | (1u << 3);
    if (a0 != 0) {                                          /* the flower boxes: thin planes at the front */
        static const double xs[2][2] = {{0, 16}, {32, 80}};
        unsigned k;

        for (k = 0; k < 2; k++) {
            RgPart *pl = rg_parts_add(out, RG_P_PRISM, k == 0 ? "planter_w" : "planter_e");

            if (pl == NULL)
                return false;
            pr = &pl->u.prism;
            pr->x0 = xs[k][0]; pr->x1 = xs[k][1];
            pr->west = pr->east = false;
            pr->nPoly = 4;
            vr_pt(pr->poly, 0, 72, 0);
            vr_pt(pr->poly, 1, 72, 16);
            vr_pt(pr->poly, 2, 70, 16);
            vr_pt(pr->poly, 3, 70, 0);
            pr->edges[0].kind = RG_EM_PROJ;
            pr->edges[0].proj = rg_proj_rows(56, 72);
            pr->skip = (1u << 1) | (1u << 2) | (1u << 3);
        }
    }
    return !out->failed;
}

static const RgExact kViridianHouseExact[4] = {
    {47, 8, 65, 40, false},     /* chimney, top and front face */
    {0, 20, 47, 48, false},     /* front slope, fascia (west of the chimney) */
    {65, 20, 80, 48, false},    /* the same, east of it */
    {0, 48, 80, 64, false},     /* facade */
};

static const RgExact kViridianHouse2Exact[7] = {
    {47, 8, 65, 40, false},     /* chimney, top and front face */
    {0, 20, 47, 48, false},     /* front slope, fascia */
    {65, 20, 80, 48, false},
    {0, 48, 80, 56, false},     /* facade above the boxes */
    {0, 56, 16, 72, false},     /* flower box, west */
    {16, 56, 32, 64, false},    /* the door */
    {32, 56, 80, 72, false},    /* flower boxes, east */
};

const RgSpec rg_kspecs_viridian[] = {
    {"k_viridian_house", RG_SPEC_DIRECT, L_VIRIDIAN_K3, {24, 8, 5, 4}, {0, 0}, {VR_GRASS}, 1, kViridianHouseExact, 4,
     k_viridian_house, 0, 0, NULL},
    {"k_viridian_house2", RG_SPEC_DIRECT, L_VIRIDIAN_K3, {24, 15, 5, 5}, {0, 0}, {VR_GRASS}, 1, kViridianHouse2Exact, 7,
     k_viridian_house, 1, 0, NULL},
};
const unsigned rg_kspecs_viridian_count = sizeof(rg_kspecs_viridian) / sizeof(rg_kspecs_viridian[0]);
