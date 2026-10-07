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
#define L_TWO 231, 0x9AB6DD1Fu
#define L_THREE 232, 0x76E27C1Eu
#define L_BRINK 239, 0u
#define L_PORT 241, 0x9A123C3Eu
#define SV_GROUND {0x001}
#define SV_SEA_PORT {613, 627, 619, 628}   /* Three Isle Port's sea: 613 beside the pier, the rest as Two Island's */
#define SV_SEA {627, 619, 628, 632}        /* the sea's open-water metatiles (the water is the ground; at most 4 entries) */

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

/* ==== KS1: Two Island (layout 231, map 3/13) ============================================================================ */
/* (the one-block helper sv_block and the ferry's pieces are used by One Island's harbor below, so they sit here) */

/* A flat-roofed block over x0..x1: the facade runs art rows ftop..zf (a vertical face, height zf - ftop), the level roof
 * top runs rows rtop..ftop behind it. */
static bool sv_block(RgPartList *out, const char *name, double x0, double x1, double zf, double ftop, double rtop)
{
    double h = zf - ftop;
    double pts[4][2], rows[4][2];

    sv_pt(pts, 0, zf, 0);
    sv_pt(pts, 1, zf, h);
    sv_pt(pts, 2, rtop + h, h);
    sv_pt(pts, 3, rtop + h, 0);
    rows[0][0] = ftop; rows[0][1] = zf;
    rows[1][0] = rtop; rows[1][1] = ftop;
    rows[2][0] = rows[2][1] = rows[3][0] = rows[3][1] = 0;
    return sv_profile(out, name, x0, x1, 4, (const double (*)[2])pts, (const double (*)[2])rows);
}

/* ---- k_two_gamecorner: 80x64 art (rect (37,6), 5x4 on layout 231; door (39,9)) -------------------------------------- */
/* The Joyful Game Corner: a flat-roofed hall. Rows: the grey frame line 0, the cream parapet 1-5, the blue skylight
 * 6-33 inside it, the cream rim 34-41, the dark facade line 42, the facade 43-62 (yellow diamond bands, the glass door
 * under a pink awning with yellow lights at x 26-48) and the base 62-64. One block 22 high. */
static bool k_two_gamecorner(const RgSpec *spec, int a0, int a1, RgPartList *out)
{
    (void)spec; (void)a0; (void)a1;
    return sv_block(out, "hall", 0, 80, 64, 42, 0) && !out->failed;
}
static const RgExact kGameCornerExact[1] = {{0, 0, 80, 64, false}};
static const RgSideCfg kGameCornerSide[1] = {
    {NULL, {2, 10, 4, 46}, {2, 10, 4, 46}, 999, true},
};

/* ---- k_two_harbor: 112x112 art (rect (7,7), 7x7 on layout 231; door (10,8)) ----------------------------------------- */
/* The Two Island harbor: the Seagallop ferry moored at a plank pier, drawn as an elevation over water (the sea tiles
 * are ground quarters, so they are transparent). Rows: the pier deck 0-28 and its grey ramp 28-33 (x 32-80), the ferry's
 * red corrugated roof 33-80 over its grey hull with the red-triangle sign 80-112 (x 16-96), and two red cranes at each
 * side (x 0-16 and 96-112, rows 42-74 and 74-106). Four flat blocks and the pier slab. */
static bool sv_ferry(RgPartList *out)
{
    return sv_block(out, "pier", 32, 80, 33, 28, 0) &&
           sv_block(out, "ferry", 16, 96, 104, 80, 33) &&
           sv_block(out, "crane_nw", 0, 16, 66, 56, 42) && sv_block(out, "crane_sw", 0, 16, 98, 88, 74) &&
           sv_block(out, "crane_ne", 96, 112, 66, 56, 42) && sv_block(out, "crane_se", 96, 112, 98, 88, 74) &&
           !out->failed;
}
static bool k_two_harbor(const RgSpec *spec, int a0, int a1, RgPartList *out)
{
    (void)spec; (void)a0; (void)a1;
    return sv_ferry(out);
}
static const RgExact kTwoHarborExact[6] = {
    {32, 0, 80, 33, false},         /* the pier deck and its ramp */
    {16, 33, 96, 104, false},       /* the ferry: roof, hull, sign */
    {0, 42, 16, 66, false},         /* the west cranes (the water reflections below each are not modelled) */
    {0, 74, 16, 98, false},
    {96, 42, 112, 66, false},       /* the east cranes */
    {96, 74, 112, 98, false},
};
static const RgSideCfg kTwoHarborSide[1] = {
    {NULL, {20, 82, 24, 86}, {23, 36, 26, 42}, 999, true},
};

/* ==== KS1: Three Island (layout 232, map 3/14) =========================================================================== */

/* ---- k_three_house_red: 80x64 art (rect (2,28), 5x4 on layout 232; door (3,31)) ------------------------------------- */
/* The one red-roofed house of Three Island: the purple house's elevation with a red roof (same rows: lip 8-9, roof
 * 9-42, eave 42-46, facade 46-64), so it reuses k_sevii_house's builder. */
static const RgSideCfg kHouseRedSide[1] = {
    {NULL, {40, 58, 44, 60}, {7, 24, 9, 30}, 22, true},
};

/* ---- k_three_port: 112x112 art (rect (9,12), 7x7 on layout 241; door (12,13)) -------------------------------------- */
/* Three Isle Port: the same ferry, pier and cranes as Two Island's harbor (the boat cells are identical), moored at a
 * sand quay instead of a plank deck. The quay is two low blocks beside the pier (x 0-32 and 80-112): sand and the
 * grey bollards on top (rows 0-16), the dark edge as the face (rows 16-20). */
static bool k_three_port(const RgSpec *spec, int a0, int a1, RgPartList *out)
{
    (void)spec; (void)a0; (void)a1;
    return sv_block(out, "quay_w", 0, 32, 20, 16, 0) && sv_block(out, "quay_e", 80, 112, 20, 16, 0) && sv_ferry(out);
}
static const RgExact kPortExact[8] = {
    {32, 0, 80, 33, false},         /* the pier: sand, planks, ramp */
    {0, 0, 32, 20, false},          /* the west quay */
    {80, 0, 112, 20, false},        /* the east quay */
    {16, 33, 96, 104, false},       /* the ferry */
    {0, 42, 16, 66, false},         /* the cranes */
    {0, 74, 16, 98, false},
    {96, 42, 112, 66, false},
    {96, 74, 112, 98, false},
};

const RgSpec rg_kspecs_sevii[] = {
    {"k_sevii_house", RG_SPEC_DIRECT, L_ONE, {18, 6, 5, 4}, {1, 4}, SV_GROUND, 1, kHouseExact, 1,
     k_sevii_house, 0, 0, kHouseSide},
    {"k_one_network", RG_SPEC_DIRECT, L_ONE, {11, 0, 7, 6}, {0, 0}, SV_GROUND, 1, kNetworkExact, 5,
     k_one_network, 0, 0, NULL},
    {"k_two_gamecorner", RG_SPEC_DIRECT, L_TWO, {37, 6, 5, 4}, {0, 0}, SV_GROUND, 1, kGameCornerExact, 1,
     k_two_gamecorner, 0, 0, kGameCornerSide},
    {"k_two_harbor", RG_SPEC_DIRECT, L_TWO, {7, 7, 7, 7}, {0, 0}, SV_SEA, 4, kTwoHarborExact, 6,
     k_two_harbor, 0, 0, kTwoHarborSide},
    {"k_three_house_red", RG_SPEC_DIRECT, L_THREE, {2, 28, 5, 4}, {1, 4}, SV_GROUND, 1, kHouseExact, 1,
     k_sevii_house, 0, 0, kHouseRedSide},
    {"k_three_port", RG_SPEC_DIRECT, L_PORT, {9, 12, 7, 7}, {0, 0}, SV_SEA_PORT, 4, kPortExact, 8,
     k_three_port, 0, 0, kTwoHarborSide},
};
const unsigned rg_kspecs_sevii_count = sizeof(rg_kspecs_sevii) / sizeof(rg_kspecs_sevii[0]);
