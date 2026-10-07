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

/* ---- k_one_harbor: 112x80 art (rect (9,15), 7x5 on layout 230; door (12,18)) --------------------------------------- */
/* One Island's harbor (an island-One model; it sits here because it shares Two Island's ferry helper). The same ferry seen
 * from the other end: the map ends 80 rows down, so only the pier and the front of the ferry's roof are in the art. Rows:
 * rocks beside the stairs 0-20 (x 0-32 and 80-112), the grey stairs 0-16 and the plank deck 16-59 (x 32-80), its ramp
 * 59-64, the red roof from 64 (x 16-96) and the tops of the two crane frames at 76-80. The ferry and the crane tops are
 * clipped by the map edge, so their faces are one row. */
static bool k_one_harbor(const RgSpec *spec, int a0, int a1, RgPartList *out)
{
    (void)spec; (void)a0; (void)a1;
    return sv_block(out, "rock_w", 0, 32, 20, 16, 0) && sv_block(out, "rock_e", 80, 112, 20, 16, 0) &&
           sv_block(out, "pier", 32, 80, 64, 59, 0) && sv_block(out, "ferry", 16, 96, 80, 79, 64) &&
           sv_block(out, "crane_w", 0, 16, 80, 79, 76) && sv_block(out, "crane_e", 96, 112, 80, 79, 76) && !out->failed;
}
static const RgExact kOneHarborExact[4] = {
    {0, 0, 32, 20, false},          /* the west rocks */
    {80, 0, 112, 20, false},        /* the east rocks */
    {32, 0, 80, 64, false},         /* the stairs, deck and ramp */
    {16, 64, 96, 80, false},        /* the ferry's roof */
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

/* ==== KS2: Four Island (layout 233, map 3/15) and Five Island (layout 234, map 3/16) =================================== */
/* Four Island's Center (17,18) and Mart (21,24), Five Island's Center are the K2 landmark models (k_center / k_mart). */

#define L_FOUR 233, 0xF927FC39u
#define L_FIVE 234, 0x2AA31FFFu
#define L_RESORT 246, 0x7C0F16BEu
#define L_MEADOW 248, 0x70F9C5B3u

/* A gabled house seen from the front, as one profile prism: the facade rows wallTop..front (a wall front - wallTop
 * high), a dark eave strip eaveTop..wallTop, and the roof as a 45-degree slope from art row eaveTop up to roofTop (the
 * rows run z - y). `paraTop` (-1 = none) adds a thin parapet standing on the roof's back edge, drawn from art rows
 * paraTop..roofTop. Every house of KS2 (orange, purple, the Five Island edge house, Lorelei's house, the Rocket
 * Warehouse) is this shape with other rows. */
static bool sv_gable(RgPartList *out, const char *name, double width, double front, double wallTop, double eaveTop,
                     double roofTop, double paraTop)
{
    double pts[7][2], rows[7][2];
    unsigned n = 0;
    double y1 = front - wallTop;            /* the wall top */
    double y2 = front - eaveTop;            /* the eave top, where the roof starts */
    double d = (eaveTop - roofTop) / 2.0;   /* the 45-degree roof: d deep and d high */
    double rise = paraTop >= 0 ? roofTop - paraTop : 0;

    memset(rows, 0, sizeof(rows));
    sv_pt(pts, n, front, 0);
    rows[n][0] = wallTop; rows[n][1] = front;
    n++;
    sv_pt(pts, n, front, y1);
    rows[n][0] = eaveTop; rows[n][1] = wallTop;
    n++;
    sv_pt(pts, n, front, y2);
    rows[n][0] = roofTop; rows[n][1] = eaveTop;
    n++;
    sv_pt(pts, n, front - d, y2 + d);
    if (rise > 0) {
        rows[n][0] = paraTop; rows[n][1] = roofTop;
        n++;
        sv_pt(pts, n, front - d, y2 + d + rise);
    }
    n++;
    sv_pt(pts, n, front - d - 7, y2 + d + rise);
    n++;
    sv_pt(pts, n, front - d - 7, 0);
    n++;
    return sv_profile(out, name, 0, width, n, (const double (*)[2])pts, (const double (*)[2])rows) && !out->failed;
}

/* ---- k_four_house_orange: 64x64 art (rect (11,10), 4x4 on layout 233; door (12,13)) --------------------------------- */
/* Four Island's orange-roofed house: grass 0-8, the yellow-lipped orange roof 8-42, the dark eave 42-46, a blue door and
 * a window in a pale facade 46-64. The same rows as the One Island house, at 64 wide. */
static bool k_four_house_orange(const RgSpec *spec, int a0, int a1, RgPartList *out)
{
    (void)spec; (void)a0; (void)a1;
    return sv_gable(out, "house", 64, 64, 46, 42, 8, -1);
}
static const RgExact kFourOrangeExact[1] = {{0, 8, 64, 64, false}};
static const RgSideCfg kFourOrangeSide[1] = {
    {NULL, {10, 50, 14, 60}, {10, 24, 14, 28}, 22, true},
};

/* ---- k_four_house: 80x64 art (rect (24,23), 5x4 on layout 233; door (25,26)) ---------------------------------------- */
/* The lilac-roofed house that stands on Four Island (three times) and Five Island: rows 1-3 of the rect are the same cells
 * everywhere (649-653 / 657-661 / 665-669), only the top row differs (a grass roof-top cell, a cliff-side one), so
 * matchRows is (1, 4) and the art is taken from the grass-topped one at (24,23). Grass 0-8, the pale roof lip 9-19,
 * the lilac roof 20-42, the dark eave 43-45, a barred door and a window in the facade 46-64. */
static bool k_four_house(const RgSpec *spec, int a0, int a1, RgPartList *out)
{
    (void)spec; (void)a0; (void)a1;
    return sv_gable(out, "house", 80, 64, 46, 43, 9, -1);
}
static const RgExact kFourPurpleExact[1] = {{0, 9, 80, 64, false}};
static const RgSideCfg kFourPurpleSide[1] = {
    {NULL, {50, 58, 54, 60}, {77, 24, 79, 28}, 21, true},
};

/* ---- k_five_house_edge: 48x64 art (rect (21,6), 3x4 on layout 234; door (22,9)) ------------------------------------- */
/* Five Island's lilac house at the map's east edge: the same house as k_four_house, but its right two cells lie outside
 * the 24-wide map, so the rect is only three cells (48 px) wide and the top row holds bushes. Same rows as k_four_house
 * (roof lip 9-19, roof 20-42, eave 43-45, facade 46-64) at width 48; the bushes (rows 0-8) are above the exact rect. */
static bool k_five_house_edge(const RgSpec *spec, int a0, int a1, RgPartList *out)
{
    (void)spec; (void)a0; (void)a1;
    return sv_gable(out, "house", 48, 64, 46, 43, 9, -1);
}
static const RgExact kFiveEdgeExact[1] = {{0, 9, 48, 64, false}};
static const RgSideCfg kFiveEdgeSide[1] = {
    {NULL, {41, 58, 45, 60}, {45, 24, 47, 28}, 21, true},
};

/* ---- k_four_harbor: 112x96 art (rect (7,28), 7x6 on layout 233; door (10,28)) -------------------------------------- */
/* Four Island's harbor: the Two Island ferry (sv_ferry's blocks) with the map's pier cut 17 rows lower, so the rect is
 * 6 cells tall and starts at the pier's deck. Rows (Two Island's minus 17): the plank pier x 32-80 rows 0-11 with its
 * ramp 11-16, the red roof 16-63 over the grey hull 63-87 (x 16-96), and a crane at each side (x 0-16 and 96-112, rows
 * 25-49 and 57-81). The sea quarters (metatile 627) are the ground; the reflections under the cranes are not modelled. */
static bool sv_ferry_cut(RgPartList *out, double dy)
{
    return sv_block(out, "pier", 32, 80, 33 - dy, 28 - dy, 0) && sv_block(out, "ferry", 16, 96, 104 - dy, 80 - dy, 33 - dy) &&
           sv_block(out, "crane_nw", 0, 16, 66 - dy, 56 - dy, 42 - dy) && sv_block(out, "crane_sw", 0, 16, 98 - dy, 88 - dy, 74 - dy) &&
           sv_block(out, "crane_ne", 96, 112, 66 - dy, 56 - dy, 42 - dy) && sv_block(out, "crane_se", 96, 112, 98 - dy, 88 - dy, 74 - dy) &&
           !out->failed;
}
static bool k_four_harbor(const RgSpec *spec, int a0, int a1, RgPartList *out)
{
    (void)spec; (void)a0; (void)a1;
    return sv_ferry_cut(out, 17);
}
static const RgExact kFourHarborExact[6] = {
    {32, 0, 80, 16, false},         /* the pier deck and its ramp */
    {16, 16, 96, 87, false},        /* the ferry: roof, hull, sign */
    {0, 25, 16, 49, false},         /* the west cranes (the water reflections below each are not modelled) */
    {0, 57, 16, 81, false},
    {96, 25, 112, 49, false},       /* the east cranes */
    {96, 57, 112, 81, false},
};
static const RgSideCfg kFourHarborSide[1] = {
    {NULL, {20, 70, 24, 74}, {18, 24, 22, 30}, 999, true},
};

/* ---- k_five_harbor: 112x96 art (rect (9,14), 7x6 on layout 234; door (12,14)) -------------------------------------- */
/* Five Island's harbor: the same pier, ferry and cranes as k_four_harbor (the cells match except the crane-side sea
 * reflections 710 / 726 / 718 / 734 / 711 / 727 / 719 / 735 against Four's 646 / 662 / 654 / 670 / 647 / 663 / 655), over
 * the sea metatile 299. So it shares Four's builder, exact rects and side tiles under its own pin and rect. */

/* ==== KS2: Resort Gorgeous (layout 246, map 3/54) ======================================================================= */

/* ---- k_lorelei_house: 80x64 art (rect (38,5), 5x4 on layout 246; door (39,8)) --------------------------------------- */
/* Lorelei's house: a brown tiled roof under a row of white and grey battlement pillars (rows 2-9), over a pale facade
 * with a barred door and a window. Rows: grass 0-1, the battlements 2-9, the roof lip 9-19, the roof 20-42, the dark eave
 * 43-45, the facade 46-64. The gabled house of sv_gable plus its parapet. */
static bool k_lorelei_house(const RgSpec *spec, int a0, int a1, RgPartList *out)
{
    (void)spec; (void)a0; (void)a1;
    return sv_gable(out, "house", 80, 64, 46, 43, 9, 2);
}
static const RgExact kLoreleiExact[1] = {{0, 2, 80, 64, false}};
static const RgSideCfg kLoreleiSide[1] = {
    {NULL, {73, 50, 76, 56}, {77, 24, 79, 28}, 21, true},
};

/* ==== KS2: Five Isle Meadow (layout 248, map 3/56) ====================================================================== */

/* ---- k_rocket_warehouse: 96x80 art (rect (9,17), 6x5 on layout 248; door (12,21)) ----------------------------------- */
/* The Rocket Warehouse: a wide brown-roofed hall with a row of treetops along its roof ridge (rows 0-8). Rows: the ridge
 * trees 0-8, the roof 8-53 (an orange sheet with a brown rim), the dark eave 53-56, a tan wall with a glass double door
 * (x 48-64) and a bush at the lower left 56-80. The sv_gable shape with a taller wall (24) and a parapet of 8 rows. */
static bool k_rocket_warehouse(const RgSpec *spec, int a0, int a1, RgPartList *out)
{
    (void)spec; (void)a0; (void)a1;
    return sv_gable(out, "hall", 96, 80, 56, 53, 8, 0);
}
static const RgExact kWarehouseExact[1] = {{0, 0, 96, 80, false}};
static const RgSideCfg kWarehouseSide[1] = {
    {NULL, {88, 60, 92, 70}, {88, 14, 91, 40}, 27, true},
};

/* ==== KS3: Six Island (layout 236, map 3/18), Seven Island (235, 3/17), Water Path (252, 3/60), Trainer Tower (254, 3/62),
 *      Sevault Canyon (256, 3/64), Navel Rock (343, 2/0), Birth Island (342, 2/56) ====================================== */

#define L_SEVEN 235, 0x64A249C1u
#define L_SIX 236, 0u
#define L_WATER 252, 0u
#define L_TOWER 254, 0u
#define L_CANYON 256, 0u
#define L_NAVEL 343, 0x45EADA9Bu
#define L_BIRTH 342, 0xA2E502C5u

/* ---- k_seven_house: 80x64 art (rect (10,6), 5x4 on layout 235; door (11,9)) --------------------------------------- */
/* The lilac-roofed house of Seven Island, Six Island and Water Path (rows 1-3 of the rect are the cells 649-669 on every
 * one: the 5x3 census signature A0A45131; the top row is grass or cliff and varies, so matchRows is (1, 4)). Same art as
 * k_four_house pixel for pixel, but these layouts use another secondary tileset, so Four Island's model does not land
 * here (placements checks the tileset): this row, with its own pin, reuses the builder. */

const RgSpec rg_kspecs_sevii[] = {
    {"k_sevii_house", RG_SPEC_DIRECT, L_ONE, {18, 6, 5, 4}, {1, 4}, SV_GROUND, 1, kHouseExact, 1,
     k_sevii_house, 0, 0, kHouseSide},
    {"k_one_network", RG_SPEC_DIRECT, L_ONE, {11, 0, 7, 6}, {0, 0}, SV_GROUND, 1, kNetworkExact, 5,
     k_one_network, 0, 0, NULL},
    {"k_one_harbor", RG_SPEC_DIRECT, L_ONE, {9, 15, 7, 5}, {0, 0}, SV_SEA, 4, kOneHarborExact, 4,
     k_one_harbor, 0, 0, kTwoHarborSide},
    {"k_two_gamecorner", RG_SPEC_DIRECT, L_TWO, {37, 6, 5, 4}, {0, 0}, SV_GROUND, 1, kGameCornerExact, 1,
     k_two_gamecorner, 0, 0, kGameCornerSide},
    {"k_two_harbor", RG_SPEC_DIRECT, L_TWO, {7, 7, 7, 7}, {0, 0}, SV_SEA, 4, kTwoHarborExact, 6,
     k_two_harbor, 0, 0, kTwoHarborSide},
    {"k_three_house_red", RG_SPEC_DIRECT, L_THREE, {2, 28, 5, 4}, {1, 4}, SV_GROUND, 1, kHouseExact, 1,
     k_sevii_house, 0, 0, kHouseRedSide},
    {"k_three_port", RG_SPEC_DIRECT, L_PORT, {9, 12, 7, 7}, {0, 0}, SV_SEA_PORT, 4, kPortExact, 8,
     k_three_port, 0, 0, kTwoHarborSide},
    {"k_four_house_orange", RG_SPEC_DIRECT, L_FOUR, {11, 10, 4, 4}, {0, 0}, SV_GROUND, 1, kFourOrangeExact, 1,
     k_four_house_orange, 0, 0, kFourOrangeSide},
    {"k_four_house", RG_SPEC_DIRECT, L_FOUR, {24, 23, 5, 4}, {1, 4}, SV_GROUND, 1, kFourPurpleExact, 1,
     k_four_house, 0, 0, kFourPurpleSide},
    {"k_five_house_edge", RG_SPEC_DIRECT, L_FIVE, {21, 6, 3, 4}, {0, 0}, SV_GROUND, 1, kFiveEdgeExact, 1,
     k_five_house_edge, 0, 0, kFiveEdgeSide},
    {"k_four_harbor", RG_SPEC_DIRECT, L_FOUR, {7, 28, 7, 6}, {0, 0}, {627}, 1, kFourHarborExact, 6,
     k_four_harbor, 0, 0, kFourHarborSide},
    {"k_five_harbor", RG_SPEC_DIRECT, L_FIVE, {9, 14, 7, 6}, {0, 0}, {299}, 1, kFourHarborExact, 6,
     k_four_harbor, 0, 0, kFourHarborSide},
    {"k_lorelei_house", RG_SPEC_DIRECT, L_RESORT, {38, 5, 5, 4}, {0, 0}, SV_GROUND, 1, kLoreleiExact, 1,
     k_lorelei_house, 0, 0, kLoreleiSide},
    {"k_rocket_warehouse", RG_SPEC_DIRECT, L_MEADOW, {9, 17, 6, 5}, {0, 0}, SV_GROUND, 1, kWarehouseExact, 1,
     k_rocket_warehouse, 0, 0, kWarehouseSide},
    {"k_seven_house", RG_SPEC_DIRECT, L_SEVEN, {10, 6, 5, 4}, {1, 4}, SV_GROUND, 1, kFourPurpleExact, 1,
     k_four_house, 0, 0, kFourPurpleSide},
    {"k_seven_harbor", RG_SPEC_DIRECT, L_SEVEN, {13, 13, 7, 6}, {0, 0}, {619}, 1, kFourHarborExact, 6,
     k_four_harbor, 0, 0, kFourHarborSide},
    {"k_navel_harbor", RG_SPEC_DIRECT, L_NAVEL, {6, 16, 7, 6}, {0, 0}, {630}, 1, kFourHarborExact, 6,
     k_four_harbor, 0, 0, kFourHarborSide},
    {"k_birth_harbor", RG_SPEC_DIRECT, L_BIRTH, {12, 24, 7, 6}, {0, 0}, {630}, 1, kFourHarborExact, 6,
     k_four_harbor, 0, 0, kFourHarborSide},
};
const unsigned rg_kspecs_sevii_count = sizeof(rg_kspecs_sevii) / sizeof(rg_kspecs_sevii[0]);
