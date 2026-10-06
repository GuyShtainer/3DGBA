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
};
const unsigned rg_kspecs_landmarks_count = sizeof(rg_kspecs_landmarks) / sizeof(rg_kspecs_landmarks[0]);
