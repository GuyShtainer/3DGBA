/* rg_kspecs_viridian.c -- Viridian City, Route 2 and Viridian Forest gate recipes (FireRed / LeafGreen).
 * 3DGBA original work, GPLv3, "Kanto recipes". Phase 34 slice K3 (docs/phase34-frlg/SPEC.md sections 5.3-5.6).
 *
 * Every number below was read off the `romgen author ... art` pictures of the user's own ROM (art row = z - y for the
 * renderer's oblique camera). The builders reuse the Emerald part library (rg_geom) unchanged; most faces are PROJ
 * edges, which copy the art by row, so the ortho check holds by construction where the geometry is right.
 *
 *   k_viridian_house   5x4 cells (80x64): gabled shingle house with a chimney (layout 79).
 *   k_viridian_house2  the same house with flower boxes (5x5 cells).
 *   k_route2_house     5x3 cells (80x48): blue hip-roofed house on Route 2 (layout 90).
 *   k_route2_gate      6x7 cells (96x112): the flat-roofed gatehouse with a porch on each side (layout 90). */
#include "rg_bspecs.h"

#include <string.h>

#define VR_GRASS 0x001
#define L_VIRIDIAN_K3 79, 0x9925DC24u

static void vr_pt(double (*poly)[2], unsigned i, double z, double y)
{
    poly[i][0] = z;
    poly[i][1] = y;
}

static RgStrip vr_strip2(double a, double b)
{
    RgStrip s;

    memset(&s, 0, sizeof(s));
    s.fixed[0] = a;
    s.fixed[1] = b;
    return s;
}

static RgStrip vr_wrap(RgStrip s, double lo, double hi)
{
    s.hasWrap = true;
    s.wrap[0] = lo;
    s.wrap[1] = hi;
    return s;
}

static void vr_box(RgPrism *p, double front, double ylo, double yhi, double back)
{
    p->nPoly = 4;
    vr_pt(p->poly, 0, front, ylo);
    vr_pt(p->poly, 1, front, yhi);
    vr_pt(p->poly, 2, back, yhi);
    vr_pt(p->poly, 3, back, ylo);
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

/* ---- k_route2_house: 80x48 art (rect rows = art rows 16-63 of the 5x5 picture) ----------------------------------- */
/* Rows: ridge 0-1, front slope 1-23 (a stripe every 7-8 rows), eave 24-31, facade 32-47. Hip ends at columns 0-11 and
 * 67-77. The hip builder textures its end faces from the slope strip, so the ends are not pinned by the exact rects. */
static bool k_route2_house(const RgSpec *spec, int a0, int a1, RgPartList *out)
{
    double front_z = 48, back_z = 12, over = 2, wall_top = 18;
    RgPart *base = rg_parts_add(out, RG_P_PRISM, "ground_floor");
    RgPart *rf = rg_parts_add(out, RG_P_HIPROOF, "roof");
    RgHip *roof;
    RgPrism *pr;

    (void)spec; (void)a0; (void)a1;
    if (base == NULL || rf == NULL)
        return false;
    roof = &rf->u.hip;
    roof->x0 = 0; roof->x1 = 78;
    roof->zf = front_z + over; roof->zb = 32; roof->y0 = wall_top;
    roof->fascia = rg_strip_fin(vr_wrap(vr_strip2(24, 32), 14, 62));
    roof->slope = rg_strip_fin(vr_wrap(vr_strip2(1, 24), 14, 62));
    roof->teeth[0] = 0; roof->teeth[1] = 1;
    roof->cap[0] = 0; roof->cap[1] = 1;
    roof->pitch = 59.6; roof->run = 11;
    roof->ridgeU[0] = 0; roof->ridgeU[1] = 78;
    roof->endTile = rg_tile(11, 0, 12, 1);
    roof->ridge = true;
    roof->hasRidgeWrap = true; roof->ridgeWrap[0] = 14; roof->ridgeWrap[1] = 62;
    rg_hip_init(roof);

    pr = &base->u.prism;
    pr->x0 = 1; pr->x1 = 80;
    pr->west = pr->east = true;
    vr_box(pr, front_z, 0, wall_top, back_z);
    pr->edges[0].kind = RG_EM_PROJ;
    pr->edges[0].proj = rg_proj_rows(32, 48);
    pr->skip = (1u << 1) | (1u << 2) | (1u << 3);
    return !out->failed;
}

/* ---- k_route2_gate: 96x112 art (cells (16,41), 6x7) --------------------------------------------------------------- */
/* Rows: north door mat 0-15, flat roof top 16-58, cornice 59-63, facade 64-95 with a canopy on the front (top 66-76,
 * front 77-87) over two pillars (88-102), south door mat and porch floor 96-111, log posts either side of both mats.
 * Everything is a PROJ face, so the ortho check holds by construction; the depths (z - y = art row) make it stand. */
static void vr_vplane(RgPartList *out, const char *name, double x0, double x1, double z, double y0, double y1,
                      double rlo, double rhi)
{
    RgPart *pt = rg_parts_add(out, RG_P_PRISM, name);
    RgPrism *pr;

    if (pt == NULL)
        return;
    pr = &pt->u.prism;
    pr->x0 = x0; pr->x1 = x1;
    pr->west = pr->east = false;
    vr_box(pr, z, y0, y1, z - 1);
    pr->edges[0].kind = RG_EM_PROJ;
    pr->edges[0].proj = rg_proj_rows(rlo, rhi);
    pr->skip = (1u << 1) | (1u << 2) | (1u << 3);
}

/* A level face at height y spanning depths z0..z1: art rows z0 - y .. z1 - y. */
static void vr_hplane(RgPartList *out, const char *name, double x0, double x1, double y, double z0, double z1)
{
    RgPart *pt = rg_parts_add(out, RG_P_PRISM, name);
    RgPrism *pr;

    if (pt == NULL)
        return;
    pr = &pt->u.prism;
    pr->x0 = x0; pr->x1 = x1;
    pr->west = pr->east = false;
    vr_box(pr, z1, y - 1, y, z0);
    pr->edges[1].kind = RG_EM_PROJ;
    pr->edges[1].proj = rg_proj_rows(z0 - y, z1 - y);
    pr->skip = (1u << 0) | (1u << 2) | (1u << 3);
}

static bool k_route2_gate(const RgSpec *spec, int a0, int a1, RgPartList *out)
{
    static const double post[4][2] = {{0, 8}, {8, 17}, {79, 88}, {88, 96}};
    RgPart *body = rg_parts_add(out, RG_P_PRISM, "body");
    RgPrism *pr;
    unsigned k;

    (void)spec; (void)a0; (void)a1;
    if (body == NULL)
        return false;
    pr = &body->u.prism;
    pr->x0 = 0; pr->x1 = 96;
    pr->west = pr->east = true;
    vr_box(pr, 96, 0, 37, 53);
    pr->edges[0].kind = RG_EM_PROJ;
    pr->edges[0].proj = rg_proj_rows(59, 96);       /* cornice and facade */
    pr->edges[1].kind = RG_EM_PROJ;
    pr->edges[1].proj = rg_proj_rows(16, 59);       /* roof top */
    pr->skip = (1u << 2) | (1u << 3);

    {
        RgPart *cn = rg_parts_add(out, RG_P_PRISM, "canopy");

        if (cn == NULL)
            return false;
        pr = &cn->u.prism;
        pr->x0 = 22; pr->x1 = 74;
        pr->west = pr->east = false;
        vr_box(pr, 107, 20, 30, 96);
        pr->edges[0].kind = RG_EM_PROJ;
        pr->edges[0].proj = rg_proj_rows(77, 87);
        pr->edges[1].kind = RG_EM_PROJ;
        pr->edges[1].proj = rg_proj_rows(66, 77);
        pr->skip = (1u << 2) | (1u << 3);
    }
    vr_vplane(out, "pillar_w", 22, 30, 102, 0, 14, 88, 102);
    vr_vplane(out, "pillar_e", 68, 76, 102, 0, 14, 88, 102);
    vr_hplane(out, "mat_n", 17, 79, 0, 0, 16);
    vr_hplane(out, "mat_s", 17, 79, 0, 96, 112);
    for (k = 0; k < 4; k++) {
        vr_vplane(out, "post_n", post[k][0], post[k][1], 16, 0, 16, 0, 16);
        vr_vplane(out, "post_s", post[k][0], post[k][1], 112, 0, 16, 96, 112);
    }
    return !out->failed;
}

/* ---- k_route2_gate_s: 128x112 art (cells (2,45), 8x7) -- the south half of the Route 2 gatehouse -------------------- */
/* Rows: roof top 0-58, cornice 59-63, windows and brick 64-103, canopy over the door (top 84-91, front 92-103) between
 * two pillars, floor apron 104-111, side columns to the bottom edge. All PROJ, so ortho holds by construction. */
static bool k_route2_gate_s(const RgSpec *spec, int a0, int a1, RgPartList *out)
{
    RgPart *body = rg_parts_add(out, RG_P_PRISM, "body");
    RgPrism *pr;

    (void)spec; (void)a0; (void)a1;
    if (body == NULL)
        return false;
    pr = &body->u.prism;
    pr->x0 = 0; pr->x1 = 128;
    pr->west = pr->east = true;
    vr_box(pr, 104, 0, 45, 45);
    pr->edges[0].kind = RG_EM_PROJ;
    pr->edges[0].proj = rg_proj_rows(59, 104);      /* cornice and facade */
    pr->edges[1].kind = RG_EM_PROJ;
    pr->edges[1].proj = rg_proj_rows(0, 59);        /* roof top */
    pr->skip = (1u << 2) | (1u << 3);

    {
        RgPart *cn = rg_parts_add(out, RG_P_PRISM, "canopy");

        if (cn == NULL)
            return false;
        pr = &cn->u.prism;
        pr->x0 = 38; pr->x1 = 90;
        pr->west = pr->east = false;
        vr_box(pr, 124, 20, 32, 116);
        pr->edges[0].kind = RG_EM_PROJ;
        pr->edges[0].proj = rg_proj_rows(92, 104);
        pr->edges[1].kind = RG_EM_PROJ;
        pr->edges[1].proj = rg_proj_rows(84, 92);
        pr->skip = (1u << 2) | (1u << 3);
    }
    vr_vplane(out, "pillar_w", 32, 38, 124, 12, 40, 84, 112);
    vr_vplane(out, "pillar_e", 90, 96, 124, 12, 40, 84, 112);
    vr_vplane(out, "col_w", 0, 8, 112, 0, 8, 104, 112);
    vr_vplane(out, "col_e", 120, 128, 112, 0, 8, 104, 112);
    vr_hplane(out, "apron", 8, 120, 0, 104, 112);
    return !out->failed;
}

/* ---- k_route2_gate_n: 128x96 art (cells (2,13), 8x6) -- the north half of the Route 2 gatehouse -------------------- */
/* The roof, cornice and window band of the building as seen from its back; the brick wall under the window band is
 * hidden behind the tree row, so the rect stops at row 96. The log posts and the path above stay flat ground. */
static bool k_route2_gate_n(const RgSpec *spec, int a0, int a1, RgPartList *out)
{
    RgPart *body = rg_parts_add(out, RG_P_PRISM, "body");
    RgPrism *pr;

    (void)spec; (void)a0; (void)a1;
    if (body == NULL)
        return false;
    pr = &body->u.prism;
    pr->x0 = 0; pr->x1 = 128;
    pr->west = pr->east = true;
    vr_box(pr, 96, 0, 21, 37);
    pr->edges[0].kind = RG_EM_PROJ;
    pr->edges[0].proj = rg_proj_rows(75, 96);       /* cornice, window band, brick */
    pr->edges[1].kind = RG_EM_PROJ;
    pr->edges[1].proj = rg_proj_rows(16, 75);       /* roof top */
    pr->skip = (1u << 2) | (1u << 3);
    vr_vplane(out, "slab", 38, 90, 37, 21, 25, 12, 16);     /* the raised lip over the north door */
    return !out->failed;
}

/* ---- Viridian Forest gate halves (layout 117) ---------------------------------------------------------------------- */
/* k_forest_gate_n: 176x96 art (cells (0,4), 11x6), the same flat-roofed gatehouse as Route 2 seen from its front: roof
 * rows 0-43, cornice 44-48, windows and brick, a canopy (top 67-74, front 75-87) between two pillars. The rect stops
 * at row 88: the bushes at the bottom edge stay flat ground. */
static bool k_forest_gate_n(const RgSpec *spec, int a0, int a1, RgPartList *out)
{
    RgPart *body = rg_parts_add(out, RG_P_PRISM, "body");
    RgPrism *pr;

    (void)spec; (void)a0; (void)a1;
    if (body == NULL)
        return false;
    pr = &body->u.prism;
    pr->x0 = 0; pr->x1 = 176;
    pr->west = pr->east = true;
    vr_box(pr, 88, 0, 44, 44);
    pr->edges[0].kind = RG_EM_PROJ;
    pr->edges[0].proj = rg_proj_rows(44, 88);
    pr->edges[1].kind = RG_EM_PROJ;
    pr->edges[1].proj = rg_proj_rows(0, 44);
    pr->skip = (1u << 2) | (1u << 3);
    {
        RgPart *cn = rg_parts_add(out, RG_P_PRISM, "canopy");

        if (cn == NULL)
            return false;
        pr = &cn->u.prism;
        pr->x0 = 54; pr->x1 = 122;
        pr->west = pr->east = false;
        vr_box(pr, 116, 28, 41, 108);
        pr->edges[0].kind = RG_EM_PROJ;
        pr->edges[0].proj = rg_proj_rows(75, 88);
        pr->edges[1].kind = RG_EM_PROJ;
        pr->edges[1].proj = rg_proj_rows(67, 75);
        pr->skip = (1u << 2) | (1u << 3);
    }
    vr_vplane(out, "pillar_w", 48, 54, 116, 28, 49, 67, 88);
    vr_vplane(out, "pillar_e", 122, 128, 116, 28, 49, 67, 88);
    return !out->failed;
}

/* k_forest_gate_s: 160x112 art (cells (24,62), 10x7), the roof of the south half seen from above: a level slab over
 * rows 16-111 with the raised lip over the door at rows 12-15. */
static bool k_forest_gate_s(const RgSpec *spec, int a0, int a1, RgPartList *out)
{
    (void)spec; (void)a0; (void)a1;
    vr_hplane(out, "roof", 0, 160, 45, 61, 157);
    vr_vplane(out, "lip", 54, 122, 61, 45, 49, 12, 16);
    return !out->failed;
}

static const RgExact kViridianHouseExact[4] = {
    {47, 8, 65, 40, false},     /* chimney, top and front face */
    {0, 20, 47, 48, false},     /* front slope, fascia (west of the chimney) */
    {65, 20, 80, 48, false},    /* the same, east of it */
    {0, 48, 80, 64, false},     /* facade */
};

static const RgExact kRoute2HouseExact[2] = {
    {1, 32, 80, 48, false},     /* facade */
    {14, 1, 62, 32, false},     /* front slope and eave between the hip ends (the stripe ends at the hip edge differ by 1-2 px) */
};
static const RgExact kRoute2GateExact[5] = {
    {0, 16, 96, 96, false},     /* roof, cornice, facade, canopy, pillars */
    {17, 0, 79, 16, false},     /* north mat */
    {17, 96, 79, 112, false},   /* south mat and porch floor */
    {0, 0, 17, 16, false},      /* posts, west */
    {79, 0, 96, 16, false},     /* posts, east */
};
static const RgExact kRoute2GateSExact[1] = {
    {0, 0, 128, 112, false},    /* the whole building */
};
static const RgExact kRoute2GateNExact[2] = {
    {0, 16, 128, 96, false},    /* roof, cornice, windows, brick */
    {38, 12, 90, 16, false},    /* the lip over the door */
};
static const RgExact kForestGateNExact[1] = {
    {0, 0, 176, 88, false},     /* roof, cornice, windows, brick, canopy, pillars */
};
static const RgExact kForestGateSExact[2] = {
    {0, 16, 160, 112, false},   /* the roof */
    {54, 12, 122, 16, false},   /* the lip over the door */
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

#define L_ROUTE2 90, 0x5E505C50u
#define L_FOREST 117, 0x1DED0623u

/* Phase 34 side walls: the end-face patches of each model (art coordinates), see rg_close_sides. */
static const RgSideCfg kR2GateSide[1] = {
    {NULL, {23, 68, 73, 74}, {23, 68, 73, 74}, 999, true},
};
static const RgSideCfg kR2GateSSide[1] = {
    {NULL, {39, 84, 89, 90}, {39, 84, 89, 90}, 999, true},
};
static const RgSideCfg kR2GateNSide[1] = {
    {NULL, {8, 20, 15, 70}, {8, 20, 15, 70}, 999, true},
};
static const RgSideCfg kForestGateNSide[1] = {
    {NULL, {55, 68, 121, 74}, {55, 68, 121, 74}, 999, true},
};
static const RgSideCfg kViridianHouseSide[2] = {
    {"chimney", {50, 24, 62, 32}, {50, 24, 62, 32}, 999, false},
    {NULL, {32, 48, 41, 56}, {1, 20, 3, 40}, 18, true},
};
static const RgSideCfg kR2HouseSide[1] = {
    {NULL, {39, 32, 48, 42}, {39, 32, 48, 42}, 999, true},
};

const RgSpec rg_kspecs_viridian[] = {
    {"k_viridian_house", RG_SPEC_DIRECT, L_VIRIDIAN_K3, {24, 8, 5, 4}, {0, 0}, {VR_GRASS}, 1, kViridianHouseExact, 4,
     k_viridian_house, 0, 0, kViridianHouseSide},
    {"k_viridian_house2", RG_SPEC_DIRECT, L_VIRIDIAN_K3, {24, 15, 5, 5}, {0, 0}, {VR_GRASS}, 1, kViridianHouse2Exact, 7,
     k_viridian_house, 1, 0, kViridianHouseSide},
    {"k_route2_house", RG_SPEC_DIRECT, L_ROUTE2, {14, 20, 5, 3}, {0, 0}, {0x010, 0x011}, 2, kRoute2HouseExact, 2,
     k_route2_house, 0, 0, kR2HouseSide},
    {"k_route2_gate", RG_SPEC_DIRECT, L_ROUTE2, {16, 41, 6, 7}, {0, 0}, {0x010, 0x011}, 2, kRoute2GateExact, 5,
     k_route2_gate, 0, 0, kR2GateSide},
    {"k_route2_gate_s", RG_SPEC_DIRECT, L_ROUTE2, {2, 45, 8, 7}, {0, 0}, {0x010, 0x011}, 2, kRoute2GateSExact, 1,
     k_route2_gate_s, 0, 0, kR2GateSSide},
    {"k_route2_gate_n", RG_SPEC_DIRECT, L_ROUTE2, {2, 13, 8, 6}, {0, 0}, {0x010, 0x011}, 2, kRoute2GateNExact, 2,
     k_route2_gate_n, 0, 0, kR2GateNSide},
    {"k_forest_gate_n", RG_SPEC_DIRECT, L_FOREST, {0, 4, 11, 6}, {0, 0}, {0x010, 0x011}, 2, kForestGateNExact, 1,
     k_forest_gate_n, 0, 0, kForestGateNSide},
    {"k_forest_gate_s", RG_SPEC_DIRECT, L_FOREST, {24, 62, 10, 7}, {0, 0}, {0x010, 0x011}, 2, kForestGateSExact, 2,
     k_forest_gate_s, 0, 0, NULL},
};
const unsigned rg_kspecs_viridian_count = sizeof(rg_kspecs_viridian) / sizeof(rg_kspecs_viridian[0]);
