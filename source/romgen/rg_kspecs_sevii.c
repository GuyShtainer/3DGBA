/* rg_kspecs_sevii.c -- the Sevii Islands recipes (FireRed / LeafGreen).
 * 3DGBA original work, GPLv3, "Kanto recipes". Phase 34 slices KS1-KS3 (docs/phase34-frlg/SPEC.md sections 5.3-5.6).
 * KS1 owns One Island, Two Island (+ Cape Brink) and Three Island (+ Three Isle Port); KS2 / KS3 append below.
 *
 * Every number below was read off the `romgen author ... art` pictures of the user's own ROM (art row = z - y for the
 * renderer's oblique camera). The builders reuse the Emerald part library (rg_geom) unchanged; every face is a PROJ
 * edge, which copies the art by row, so the ortho check holds by construction where the geometry is right. Side walls
 * come from RgSideCfg (rg_close_sides): a plain patch of each model's own wall art.
 *
 * ---- KS1: One Island (layout 230, map 3/12) -----------------------------------------------------------------------
 *   k_sevii_house        5x4 cells (80x64): the purple-roofed house (every purple house of KS1) */
#include "rg_bspecs.h"

#include <string.h>

#define L_ONE 230, 0x60EA96AFu
#define SV_GROUND {0x001}

static void sv_pt(double (*poly)[2], unsigned i, double z, double y)
{
    poly[i][0] = z;
    poly[i][1] = y;
}

/* Strip helpers for the frustum faces (the Center / Mart family): fixed rows a..b, an optional repeat and wrap. */
static RgStrip sv_strip2(double a, double b)
{
    RgStrip s;

    memset(&s, 0, sizeof(s));
    s.fixed[0] = a;
    s.fixed[1] = b;
    return s;
}

static RgStrip sv_wrap(RgStrip s, double lo, double hi)
{
    s.hasWrap = true;
    s.wrap[0] = lo;
    s.wrap[1] = hi;
    return s;
}

static RgStrip sv_repeat(RgStrip s, double c, double d)
{
    s.hasRepeat = true;
    s.repeat[0] = c;
    s.repeat[1] = d;
    return s;
}

/* A profile prism: poly points (z, y); each edge PROJ-textured from the listed art rows (0,0 = skipped). */
static bool sv_profile(RgPartList *out, const char *name, double x0, double x1, unsigned n, const double (*pts)[2],
                       const double (*rows)[2])
{
    RgPart *pt = rg_parts_add(out, RG_P_PRISM, name);
    RgPrism *pr;
    unsigned i;

    if (pt == NULL)
        return false;
    pr = &pt->u.prism;
    pr->x0 = x0; pr->x1 = x1;
    pr->nPoly = n;
    pr->skip = 0;
    for (i = 0; i < n; i++) {
        sv_pt(pr->poly, i, pts[i][0], pts[i][1]);
        if (rows[i][0] == 0 && rows[i][1] == 0) {
            pr->skip |= 1u << i;
        } else {
            pr->edges[i].kind = RG_EM_PROJ;
            pr->edges[i].proj = rg_proj_rows(rows[i][0], rows[i][1]);
        }
    }
    return true;
}

/* ---- k_sevii_house: 80x64 art (rect (18,6), 5x4 on layout 230; door (19,9)) ---------------------------------------- */
/* Rows (the rect's own y, the first 8 px are grass): the pink roof lip 8-9, the purple roof slope 9-42, the dark eave
 * 42-46, the facade with two pilasters, the door and the window 46-64. A low roof over a one-storey wall. */
static bool k_sevii_house(const RgSpec *spec, int a0, int a1, RgPartList *out)
{
    static const double pts[6][2] = {{64, 0}, {64, 18}, {64, 22}, {47, 39}, {40, 39}, {40, 0}};
    static const double rows[6][2] = {{46, 64}, {42, 46}, {8, 42}, {0, 0}, {0, 0}, {0, 0}};

    (void)spec; (void)a0; (void)a1;
    return sv_profile(out, "house", 0, 80, 6, pts, rows) && !out->failed;
}
static const RgExact kHouseExact[1] = {{0, 8, 80, 64, false}};
static const RgSideCfg kHouseSide[1] = {
    {NULL, {50, 58, 54, 60}, {50, 24, 54, 28}, 22, true},
};

/* ---- k_one_network: 112x96 art (rect (11,0), 7x6 on layout 230; door (14,5)) ---------------------------------------- */
/* The One Island Network Center, a big Pokemon Center: an orange tile roof with the dish logo (rows 11-60, a level top
 * with rounded corners), the dark front slope 60-68, the eave line at 68, the pale facade with its windows 68-96 and a
 * domed porch over the glass door (x 40-72). The shape is Emerald's center_or_mart frustum (a chamfered plan, a wall, a
 * band for the slope and a flat top), and the porch a box in front of it. */
static bool k_one_network(const RgSpec *spec, int a0, int a1, RgPartList *out)
{
    double front = 96, back = 39;
    RgPart *body = rg_parts_add(out, RG_P_FRUSTUM, "body");
    RgFrustum *f;

    (void)spec; (void)a0; (void)a1;
    if (body == NULL)
        return false;
    f = &body->u.frustum;
    f->nPlan = 8;
    sv_pt(f->plan, 0, 4, front);
    sv_pt(f->plan, 1, 108, front);
    sv_pt(f->plan, 2, 110, front - 2);
    sv_pt(f->plan, 3, 110, back + 8);
    sv_pt(f->plan, 4, 102, back);
    sv_pt(f->plan, 5, 10, back);
    sv_pt(f->plan, 6, 2, back + 8);
    sv_pt(f->plan, 7, 2, front - 2);
    f->wallTop = 28;
    f->wallSide = rg_strip_fin(sv_wrap(sv_strip2(68, 96), 12, 20));
    f->bandRise = 4;
    f->bandSide = rg_strip_fin(sv_wrap(sv_strip2(60, 68), 12, 20));
    f->top = rg_strip_fin(sv_wrap(sv_repeat(sv_strip2(11, 60), 11, 19), 10, 102));
    return !out->failed;
}
static const RgExact kNetworkExact[5] = {
    {10, 12, 102, 60, false},       /* roof top */
    {6, 60, 106, 65, false},        /* front slope, inside the rounded corners */
    {2, 65, 110, 91, false},        /* front slope, eave, facade (the base corners are cut below) */
    {6, 91, 106, 96, false},        /* the facade base, one pixel inside the corner diagonals */
    {40, 66, 72, 96, false},        /* the porch */
};

const RgSpec rg_kspecs_sevii[] = {
    {"k_sevii_house", RG_SPEC_DIRECT, L_ONE, {18, 6, 5, 4}, {1, 4}, SV_GROUND, 1, kHouseExact, 1,
     k_sevii_house, 0, 0, kHouseSide},
    {"k_one_network", RG_SPEC_DIRECT, L_ONE, {11, 0, 7, 6}, {0, 0}, SV_GROUND, 1, kNetworkExact, 5,
     k_one_network, 0, 0, NULL},
};
const unsigned rg_kspecs_sevii_count = sizeof(rg_kspecs_sevii) / sizeof(rg_kspecs_sevii[0]);
