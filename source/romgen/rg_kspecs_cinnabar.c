/* rg_kspecs_cinnabar.c -- Cinnabar Island, Indigo Plateau and Route 22 / 23 recipes (FireRed / LeafGreen).
 * 3DGBA original work, GPLv3, "Kanto recipes". Phase 34 slice K11 (docs/phase34-frlg/SPEC.md sections 5.3-5.6).
 *
 * Every number below was read off the `romgen author ... art` pictures of the user's own ROM (art row = z - y for the
 * renderer's oblique camera). The builders reuse the Emerald part library (rg_geom) unchanged; every face is a PROJ
 * edge, which copies the art by row, so the ortho check holds by construction where the geometry is right. Side walls
 * come from RgSideCfg (rg_close_sides): a plain patch of each model's own wall art.
 *
 *   k_cinnabar_mansion   7x4 cells (112x64): the Pokemon Mansion, a two-storey hall under a brown roof
 *   k_cinnabar_lab       7x4 cells (112x64): the Pokemon Lab, a rounded barrel hall (stepped ends)
 *   k_indigo_league      11x7 cells (176x112): the Pokemon League, a long hall with a gabled pavilion
 *   k_route22_gate       9x7 cells (144x112): the south half of the Route 22 / 23 gatehouse
 *   k_route23_gate       9x7 cells (144x112): the north half (roof slab only) */
#include "rg_bspecs.h"

#include <string.h>

#define L_CINNABAR 86, 0xC8348D4Bu
#define L_INDIGO 87, 0x7014A55Cu
#define L_ROUTE22 110, 0x5424564Fu
#define L_ROUTE23 111, 0xC64404D8u
#define CB_GROUND {0x001}

static void cb_pt(double (*poly)[2], unsigned i, double z, double y)
{
    poly[i][0] = z;
    poly[i][1] = y;
}

/* A profile prism: poly points (z, y); each edge PROJ-textured from the listed art rows (0,0 = skipped). A model whose
 * inner steps show an end face the side check cannot see (a lower part beside a taller one) sets sCap and passes
 * CB_W / CB_E: that end is then closed with a plain patch of the model's own wall. */
static double sCap[4];
#define CB_W 1
#define CB_E 2
static bool cb_profile_e(RgPartList *out, const char *name, double x0, double x1, unsigned ends, unsigned n,
                         const double (*pts)[2], const double (*rows)[2])
{
    RgPart *pt = rg_parts_add(out, RG_P_PRISM, name);
    RgPrism *pr;
    unsigned i;

    if (pt == NULL)
        return false;
    pr = &pt->u.prism;
    pr->x0 = x0; pr->x1 = x1;
    pr->west = (ends & CB_W) != 0;
    pr->east = (ends & CB_E) != 0;
    pr->nPoly = n;
    pr->skip = 0;
    if (ends != 0) {            /* the end faces are drawn only through a cap band: a tile over the whole profile */
        double ytop = 0;

        for (i = 0; i < n; i++)
            if (pts[i][1] > ytop)
                ytop = pts[i][1];
        pr->hasCaps = true;
        pr->nCaps = 1;
        pr->caps[0] = rg_band(-1, ytop + 1, rg_tile_top(sCap[0], sCap[1], sCap[2], sCap[3], ytop), pts[0][0]);
    }
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

static bool cb_profile(RgPartList *out, const char *name, double x0, double x1, unsigned n,
                       const double (*pts)[2], const double (*rows)[2])
{
    return cb_profile_e(out, name, x0, x1, 0, n, pts, rows);
}

/* A flat-roofed block over x0..x1: the facade runs art rows ftop..zf (a vertical face, height zf - ftop), the level roof
 * top runs rows rtop..ftop behind it. */
static bool cb_block_e(RgPartList *out, const char *name, double x0, double x1, unsigned ends, double zf, double ftop,
                       double rtop)
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
    return cb_profile_e(out, name, x0, x1, ends, 4, (const double (*)[2])pts, (const double (*)[2])rows);
}
static bool cb_block(RgPartList *out, const char *name, double x0, double x1, double zf, double ftop, double rtop)
{
    return cb_block_e(out, name, x0, x1, 0, zf, ftop, rtop);
}

/* ---- k_cinnabar_mansion: 112x64 art (rect (5,0), 7x4; door (8,3)) --------------------------------------------------- */
static bool k_cinnabar_mansion(const RgSpec *spec, int a0, int a1, RgPartList *out)
{
    (void)spec; (void)a0; (void)a1;
    return cb_block(out, "hall", 5, 112, 64, 28, 0) && !out->failed;
}
/* The Pokemon Mansion, a front elevation cut by the map top. Rows: the brown roof with three dormers 0-28 (a level top),
 * the eave frieze 28-34, then two beige storeys with salmon bands and the framed central panel over the dark entrance
 * 34-64. One flat block 36 high; the panel's checker is transparent art (the ground shows through). */
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

/* ---- k_indigo_league: 176x112 art (rect (6,0), 11x7 on layout 87; door (11,6)) ----------------------------------------- */
/* The Pokemon League building, the biggest block of Kanto: 174 px wide, drawn as an elevation. Rows: the pale green
 * ribbed roof 0-57 (the map top clips it), the cornice 57-64, the orange shuttered wall with its blue windows and the
 * corner pilasters 64-104, a grey base line at 103. The centre pavilion (x 64-116) stands 6 px proud of the wall: its
 * front face (rows 80-110) holds the glass door between two pilasters, its top (rows 65-80) is the pediment, and a
 * riser (rows 56-65) joins it to the roof. The wings (x 2-64 and 116-176) are two flat blocks 40 high, 64 deep. */
static bool k_indigo_league(const RgSpec *spec, int a0, int a1, RgPartList *out)
{
    static const double mid[6][2] = {{110, 0}, {110, 30}, {95, 30}, {95, 40}, {40, 40}, {40, 0}};
    static const double midr[6][2] = {{80, 110}, {65, 80}, {56, 65}, {0, 56}, {0, 0}, {0, 0}};

    (void)spec; (void)a0; (void)a1;
    sCap[0] = 37; sCap[1] = 66; sCap[2] = 43; sCap[3] = 80;      /* the orange shutter wall */
    return cb_block_e(out, "wing_w", 2, 64, CB_W | CB_E, 104, 64, 0) &&
           cb_block_e(out, "wing_e", 116, 176, CB_W | CB_E, 104, 64, 0) &&
           cb_profile(out, "pavilion", 64, 116, 6, mid, midr) && !out->failed;
}
static const RgExact kLeagueExact[3] = {
    {2, 0, 64, 104, false},         /* west wing: roof, cornice, shutters, pilaster */
    {116, 0, 176, 104, false},      /* east wing */
    {64, 0, 116, 110, false},       /* the pavilion, the pediment and the riser */
};
static const RgSideCfg kLeagueSide[1] = {
    {NULL, {37, 66, 43, 80}, {162, 0, 166, 56}, 999, true},
};

/* ---- k_route22_gate: 144x112 art (rect (4,0), 9x7 on layout 110; doors (8,5), (9,5)) ----------------------------------- */
/* The south half of the Route 22 / Route 23 gatehouse (one building across the two maps): the pale green ribbed roof
 * 0-48 (the map top clips it), the cornice 48-56, the orange shuttered wall with its blue windows 56-96 (40 high,
 * x 0-144). The centre porch (x 52-92) is a profile prism: front face rows 78-100 (the pillars and the dark door
 * under the canopy, 22 high, 4 px proud of the wall), the cream canopy top rows 58-78, a riser rows 40-58 up to the
 * roof. The wings carry end caps on both ends (the porch is lower than they are, so their inner faces show). */
static bool k_route22_gate(const RgSpec *spec, int a0, int a1, RgPartList *out)
{
    static const double mid[6][2] = {{100, 0}, {100, 22}, {80, 22}, {80, 40}, {40, 40}, {40, 0}};
    static const double midr[6][2] = {{78, 100}, {58, 78}, {40, 58}, {0, 40}, {0, 0}, {0, 0}};

    (void)spec; (void)a0; (void)a1;
    sCap[0] = 37; sCap[1] = 58; sCap[2] = 43; sCap[3] = 69;      /* the orange shutter wall */
    return cb_block_e(out, "wing_w", 0, 52, CB_W | CB_E, 96, 56, 0) &&
           cb_block_e(out, "wing_e", 92, 144, CB_W | CB_E, 96, 56, 0) &&
           cb_profile(out, "porch", 52, 92, 6, mid, midr) && !out->failed;
}
static const RgExact kGate22Exact[3] = {
    {1, 0, 52, 96, false},          /* west wing: roof, cornice, shutters */
    {92, 0, 143, 96, false},        /* east wing */
    {52, 0, 92, 100, false},        /* the porch, its canopy and the riser */
};
static const RgSideCfg kGate22Side[1] = {
    {NULL, {37, 58, 43, 69}, {130, 0, 134, 40}, 999, true},
};

/* ---- k_route23_gate: 144x112 art (rect (4,153), 9x7 on layout 111; doors (8,153), (9,154)) --------------------------- */
/* The north half of the same gatehouse: only its roof shows, a level slab 40 high (the wall height of the south half)
 * from the parapet at row 17 (the lintel box at x 54-90, the fence above it, are ground and props) down to the map
 * bottom, where it joins the Route 22 half's roof. No front face is drawn: the slab runs on into the next map. */
static bool k_route23_gate(const RgSpec *spec, int a0, int a1, RgPartList *out)
{
    static const double pts[4][2] = {{152, 0}, {152, 40}, {57, 40}, {57, 0}};
    static const double rows[4][2] = {{0, 0}, {17, 112}, {0, 0}, {0, 0}};

    (void)spec; (void)a0; (void)a1;
    return cb_profile(out, "roof", 0, 144, 4, pts, rows) && !out->failed;
}
static const RgExact kGate23Exact[1] = {{1, 17, 143, 112, false}};
static const RgSideCfg kGate23Side[1] = {
    {NULL, {130, 18, 134, 112}, {130, 18, 134, 112}, 999, true},
};

const RgSpec rg_kspecs_cinnabar[] = {
    {"k_cinnabar_mansion", RG_SPEC_DIRECT, L_CINNABAR, {5, 0, 7, 4}, {0, 0}, CB_GROUND, 1, kMansionExact, 1,
     k_cinnabar_mansion, 0, 0, kMansionSide},
    {"k_cinnabar_lab", RG_SPEC_DIRECT, L_CINNABAR, {5, 6, 7, 4}, {0, 0}, CB_GROUND, 1, kLabExact, 9,
     k_cinnabar_lab, 0, 0, kLabSide},
    {"k_indigo_league", RG_SPEC_DIRECT, L_INDIGO, {6, 0, 11, 7}, {0, 0}, CB_GROUND, 1, kLeagueExact, 3,
     k_indigo_league, 0, 0, kLeagueSide},
    {"k_route22_gate", RG_SPEC_DIRECT, L_ROUTE22, {4, 0, 9, 7}, {0, 0}, CB_GROUND, 1, kGate22Exact, 3,
     k_route22_gate, 0, 0, kGate22Side},
    {"k_route23_gate", RG_SPEC_DIRECT, L_ROUTE23, {4, 153, 9, 7}, {0, 0}, CB_GROUND, 1, kGate23Exact, 1,
     k_route23_gate, 0, 0, kGate23Side},
};
const unsigned rg_kspecs_cinnabar_count = sizeof(rg_kspecs_cinnabar) / sizeof(rg_kspecs_cinnabar[0]);
