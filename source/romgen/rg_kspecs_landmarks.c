/* rg_kspecs_landmarks.c -- the Kanto landmark recipes: Pokemon Center, Poke Mart, Gym (FireRed / LeafGreen).
 * 3DGBA original work, GPLv3, "Kanto recipes". Phase 34 slice K2 (docs/phase34-frlg/SPEC.md sections 4 and 5).
 *
 * Every number below was read off the `romgen author ... art` pictures of the user's own ROM (16-px cells, art row =
 * z - y for the oblique camera). The builders reuse the Emerald part library (rg_geom) unchanged. */
#include "rg_bspecs.h"

#include <string.h>

#define LM_GRASS 0x001
/* Viridian (layout 79): the donor layout of all three landmark rects; FNV-1a-32 of its blockdata. */
#define L_VIRIDIAN 79, 0x9925DC24u

/* [(front, ylo), (front, yhi), (back, yhi), (back, ylo)] */
static void lm_box(RgPrism *p, double front, double ylo, double yhi, double back)
{
    p->nPoly = 4;
    p->poly[0][0] = front; p->poly[0][1] = ylo;
    p->poly[1][0] = front; p->poly[1][1] = yhi;
    p->poly[2][0] = back;  p->poly[2][1] = yhi;
    p->poly[3][0] = back;  p->poly[3][1] = ylo;
}

static RgStrip lm_strip2(double a, double b)
{
    RgStrip s;

    memset(&s, 0, sizeof(s));
    s.fixed[0] = a;
    s.fixed[1] = b;
    return s;
}

static RgStrip lm_wrap(RgStrip s, double lo, double hi)
{
    s.hasWrap = true;
    s.wrap[0] = lo;
    s.wrap[1] = hi;
    return s;
}

static RgStrip lm_repeat(RgStrip s, double c, double d)
{
    s.hasRepeat = true;
    s.repeat[0] = c;
    s.repeat[1] = d;
    return s;
}

static void lm_pt(double (*poly)[2], unsigned i, double a, double b)
{
    poly[i][0] = a;
    poly[i][1] = b;
}

/* ---- k_center: 80x64 art (rows 23-26 of the Center; the roof's top lip in the row above is outside) ---------------- */
/* Art rows: roof top 0-28 (a 4x4 pane grid), front slope 29-36 (dark red), eave line 36, facade 37-63 with the
 * Poke Ball plate over the entrance. The shape is Emerald's center_or_mart frustum: a chamfered plan, a wall, a band
 * (the slope) and a flat top. A drawn (front-facing) face is projected, so the picture is exact by construction. */
static bool k_center(const RgSpec *spec, int a0, int a1, RgPartList *out)
{
    double front = 65, back = 28;
    RgPart *body = rg_parts_add(out, RG_P_FRUSTUM, "body");
    RgFrustum *f;

    (void)spec; (void)a0; (void)a1;
    if (body == NULL)
        return false;
    f = &body->u.frustum;
    f->nPlan = 8;
    lm_pt(f->plan, 0, 8, front);
    lm_pt(f->plan, 1, 72, front);
    lm_pt(f->plan, 2, 80, front - 8);
    lm_pt(f->plan, 3, 80, back + 8);
    lm_pt(f->plan, 4, 72, back);
    lm_pt(f->plan, 5, 8, back);
    lm_pt(f->plan, 6, 0, back + 8);
    lm_pt(f->plan, 7, 0, front - 8);
    f->wallTop = 28;
    f->wallSide = rg_strip_fin(lm_wrap(lm_strip2(37, 64), 8, 16));
    f->bandRise = 4;
    f->bandSide = rg_strip_fin(lm_wrap(lm_strip2(29, 37), 8, 16));
    f->top = rg_strip_fin(lm_wrap(lm_repeat(lm_strip2(0, 29), 0, 8), 8, 72));
    return !out->failed;
}

static const RgExact kCenterExact[3] = {
    {9, 0, 71, 8, false},       /* roof top, its first rows */
    {8, 8, 72, 28, false},      /* roof top */
    {0, 28, 80, 64, false},     /* front slope, eave and facade, with the chamfered corners */
};

/* ---- k_mart: 64x64 art (4x4 cells; rows 1-3 are matched, row 0 is the roof's top lip, outside the match) ----------- */
/* The Center's shape in blue: a chamfered frustum, the sign plate over the entrance is part of the projected facade. */
static bool k_mart(const RgSpec *spec, int a0, int a1, RgPartList *out)
{
    double front = 65, back = 22, wall = 20;
    RgPart *body = rg_parts_add(out, RG_P_FRUSTUM, "body");
    RgFrustum *f;

    (void)spec; (void)a0; (void)a1;
    if (body == NULL)
        return false;
    f = &body->u.frustum;
    f->nPlan = 8;
    lm_pt(f->plan, 0, 8, front);
    lm_pt(f->plan, 1, 56, front);
    lm_pt(f->plan, 2, 64, front - 8);
    lm_pt(f->plan, 3, 64, back + 8);
    lm_pt(f->plan, 4, 56, back);
    lm_pt(f->plan, 5, 8, back);
    lm_pt(f->plan, 6, 0, back + 8);
    lm_pt(f->plan, 7, 0, front - 8);
    f->wallTop = wall;
    f->wallSide = rg_strip_fin(lm_wrap(lm_strip2(45, 64), 50, 56));
    f->bandRise = 4;
    f->bandSide = rg_strip_fin(lm_wrap(lm_strip2(37, 45), 50, 56));
    f->top = rg_strip_fin(lm_wrap(lm_repeat(lm_strip2(2, 37), 2, 10), 8, 56));
    return !out->failed;
}

/* ---- k_gym: 96/112/128 x 80 art (the Gym; width = arg0 pixels) --------------------------------------------------- */
/* The seven Kanto gyms share their left five cells (the door stands in the fourth) and differ in the roof's top lip
 * and in how many middle cells (metatile 341) sit before the right end: 6 cells (Viridian, Cinnabar), 7 (Pewter,
 * Cerulean, Vermilion, Fuchsia), 8 (Celadon). So the recipe is one builder with the width as its argument.
 * Art rows: roof 1-41 (a slatted slab), roof front edge 41-47, facade 47-72 (windows, a plain wall), porch over the
 * entrance 49-80 (the Poke Ball plate 49-63, the doors 63-80). Shape after Emerald's gym: body, roof slab, porch. */
static bool k_gym(const RgSpec *spec, int width, int a1, RgPartList *out)
{
    double w = width, front = 72, porch = 80, back = 30, wall_top = 25, roof_top = 31, roof_back = 32;
    double px0 = (w - 32) / 2 - 8 + 8, px1;
    RgPart *bd = rg_parts_add(out, RG_P_PRISM, "body");
    RgPart *rf = rg_parts_add(out, RG_P_PRISM, "roof");
    RgPart *pf = rg_parts_add(out, RG_P_PRISM, "porch");
    RgPrism *pr;
    RgTile panel, lip;
    RgBand b;
    RgStrip s;

    (void)spec; (void)a1;
    if (bd == NULL || rf == NULL || pf == NULL)
        return false;
    px0 = 40;                                   /* the porch: columns 40-72 in every width (it sits by the left cells) */
    px1 = 72;
    panel = rg_tile_top(32, 47, 40, 72, wall_top);
    lip = rg_tile_top(8, 41, 16, 47, roof_top);

    pr = &bd->u.prism;
    pr->x0 = 0; pr->x1 = w;
    pr->west = pr->east = true;
    lm_box(pr, front, -1, wall_top, back);
    pr->edges[0].kind = RG_EM_PROJ;
    pr->edges[0].proj = rg_proj_rows(47, 72);
    pr->edges[2].kind = RG_EM_TILE;
    pr->edges[2].tile = panel;
    pr->skip = (1u << 1) | (1u << 3);
    b = rg_band(-1, wall_top, panel, front);
    b.hasFront = true; b.front = panel;
    b.hasBack = true;  b.back = panel;
    rg_band_z1(&b, back);
    pr->hasCaps = true;
    pr->caps[pr->nCaps++] = b;

    pr = &rf->u.prism;
    pr->x0 = 0; pr->x1 = w;
    pr->west = pr->east = true;
    lm_box(pr, front, wall_top, roof_top, roof_back);
    pr->edges[0].kind = RG_EM_PROJ;
    pr->edges[0].proj = rg_proj_rows(41, 47);
    memset(&s, 0, sizeof(s));
    s.fixed[0] = 1; s.fixed[1] = 41;
    pr->edges[1].kind = RG_EM_STRIP;
    pr->edges[1].strip = rg_strip_fin(s);
    pr->edges[2].kind = RG_EM_TILE;
    pr->edges[2].tile = lip;
    pr->skip = 1u << 3;
    pr->hasCaps = true;
    pr->caps[pr->nCaps++] = rg_band(wall_top, roof_top + 1, lip, front);

    pr = &pf->u.prism;
    pr->x0 = px0; pr->x1 = px1;
    pr->west = pr->east = true;
    lm_box(pr, porch, -1, roof_top, front);
    pr->edges[0].kind = RG_EM_PROJ;
    pr->edges[0].proj = rg_proj_rows(49, 80);
    pr->skip = (1u << 1) | (1u << 2) | (1u << 3);
    pr->hasCaps = true;
    pr->caps[pr->nCaps++] = rg_band(-1, roof_top + 1, panel, porch);
    return !out->failed;
}

static const RgExact kGymExact[3] = {
    {3, 1, 93, 4, false},       /* roof, its rounded top corners */
    {0, 4, 96, 72, false},      /* roof, front edge and facade */
    {40, 72, 72, 80, false},    /* the porch below the facade line */
};

static const RgExact kGym7Exact[3] = {
    {3, 1, 109, 4, false},
    {0, 4, 112, 72, false},
    {40, 72, 72, 80, false},
};

static const RgExact kGym8Exact[3] = {
    {3, 1, 125, 4, false},
    {0, 4, 128, 72, false},
    {40, 72, 72, 80, false},
};

static const RgExact kMartExact[3] = {
    {9, 2, 55, 8, false},       /* roof top, its first rows */
    {8, 8, 56, 36, false},      /* roof top */
    {0, 36, 64, 64, false},     /* front slope, eave and facade, with the chamfered corners */
};

const RgSpec rg_kspecs_landmarks[] = {
    {"k_center", RG_SPEC_DIRECT, L_VIRIDIAN, {24, 23, 5, 4}, {0, 0}, {LM_GRASS, 0x008, 0x16E}, 3, kCenterExact, 3,
     k_center, 0, 0, NULL},
    {"k_mart", RG_SPEC_DIRECT, L_VIRIDIAN, {34, 16, 4, 4}, {1, 4}, {LM_GRASS, 0x008, 0x16E}, 3, kMartExact, 3,
     k_mart, 0, 0, NULL},
    {"k_gym", RG_SPEC_DIRECT, L_VIRIDIAN, {33, 6, 6, 5}, {1, 5}, {LM_GRASS, 0x008, 0x009, 0x16E}, 4, kGymExact, 3,
     k_gym, 96, 0, NULL},
    {"k_gym_7", RG_SPEC_DIRECT, 80, 0xAAB0C96Cu, {12, 12, 7, 5}, {1, 5}, {LM_GRASS, 0x008, 0x009, 0x16E}, 4, kGym7Exact, 3,
     k_gym, 112, 0, NULL},
    {"k_gym_8", RG_SPEC_DIRECT, 84, 0x6B8BA7E4u, {8, 26, 8, 5}, {1, 5}, {LM_GRASS, 0x008, 0x009, 0x16E}, 4, kGym8Exact, 3,
     k_gym, 128, 0, NULL},
};
const unsigned rg_kspecs_landmarks_count = sizeof(rg_kspecs_landmarks) / sizeof(rg_kspecs_landmarks[0]);
