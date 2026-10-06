/* rg_kspecs_lavender.c -- Lavender Town and Route 10 recipes (FireRed / LeafGreen).
 * 3DGBA original work, GPLv3, "Kanto recipes". Phase 34 slice K7 (docs/phase34-frlg/SPEC.md sections 5.3-5.6).
 *
 * Every number below was read off the `romgen author ... art` pictures of the user's own ROM (art row = z - y for the
 * renderer's oblique camera). The builders reuse the Emerald part library (rg_geom) unchanged; every face is a PROJ
 * edge, which copies the art by row, so the ortho check holds by construction where the geometry is right.
 *
 *   k_lavender_house   5x5 cells (80x80): the purple-roofed houses (layout 82; three placements, two of them side by
 *                      side) */
#include "rg_bspecs.h"

#include <math.h>
#include <string.h>

#define L_ROUTE10 98, 0xEB232F5Au
#define L_LAVENDER 82, 0xB6187344u
#define LV_GROUND {0x001}

static void lv_pt(double (*poly)[2], unsigned i, double z, double y)
{
    poly[i][0] = z;
    poly[i][1] = y;
}

/* A profile prism: poly points (z, y); each edge PROJ-textured from the listed art rows (0,0 = skipped). */
static bool lv_profile(RgPartList *out, const char *name, double x0, double x1, bool ends, unsigned n,
                       const double (*pts)[2], const double (*rows)[2])
{
    RgPart *pt = rg_parts_add(out, RG_P_PRISM, name);
    RgPrism *pr;
    unsigned i;

    if (pt == NULL)
        return false;
    pr = &pt->u.prism;
    pr->x0 = x0; pr->x1 = x1;
    pr->west = pr->east = ends;
    pr->nPoly = n;
    pr->skip = 0;
    for (i = 0; i < n; i++) {
        lv_pt(pr->poly, i, pts[i][0], pts[i][1]);
        if (rows[i][0] == 0 && rows[i][1] == 0) {
            pr->skip |= 1u << i;
        } else {
            pr->edges[i].kind = RG_EM_PROJ;
            pr->edges[i].proj = rg_proj_rows(rows[i][0], rows[i][1]);
        }
    }
    return true;
}

/* ---- k_lavender_house: 80x80 art (rect (8,8), 5x5; cell rows 9-11 are matched) -------------------------------------- */
/* Rows: roof top edge 10, the lilac hip roof seen from above 10-45, the dark eave 45-47, the facade with the two
 * windows, the door and the yellow trim 47-68. A low roof over a one-storey wall. */
static bool k_lavender_house(const RgSpec *spec, int a0, int a1, RgPartList *out)
{
    static const double pts[6][2] = {{68, 0}, {68, 21}, {68, 23}, {50.5, 40.5}, {40, 40.5}, {40, 0}};
    static const double rows[6][2] = {{47, 68}, {45, 47}, {10, 45}, {0, 0}, {0, 0}, {0, 0}};

    (void)spec; (void)a0; (void)a1;
    return lv_profile(out, "house", 0, 80, true, 6, pts, rows) && !out->failed;
}

/* ---- k_pokemon_tower: 144x112 art (rect (14,0), 9x7 on Lavender; the map top cuts the tower) -------------------------- */
/* The Pokemon Tower stands on a stone platform: the front wall (rows 81-112: dark grey stone, a yellow stripe on the
 * foot) is 31 high, its top face (rows 55-81 in front, the rim strips beside) carries the tower. The tower is one tall
 * block with a chamfered plan: the front face (x 39-105: window bays, pilasters and the plinth bands, rows 0-71) is a
 * single vertical plane, the two chamfers (x 23-39 and 105-121) are 2-px slices whose bottom edge follows the 45-degree
 * diagonal (row = x + 31). The art is cut at the map top, so the faces run up to y 102 (row 0) and are clamped there;
 * the roof is a smear of the top row. The door is a shallow box: a flat top (rows 90-95) over the front face (95-111). */
static bool tw_chamfer(RgPartList *out, double x0, double x1, double zf)
{
    double pts[4][2] = {{0, 31}, {0, 102}, {0, 102}, {0, 31}};
    double rows[4][2] = {{0, 0}, {0, 1}, {0, 0}, {0, 0}};

    pts[0][0] = zf; pts[1][0] = zf; pts[2][0] = zf - 52; pts[3][0] = zf - 52;
    rows[0][0] = 0; rows[0][1] = zf - 31;           /* the face, its top clamped at row 0 */
    rows[1][0] = 8; rows[1][1] = 9;                 /* the roof: the olive band between two window rows */
    return lv_profile(out, "chamfer", x0, x1, true, 4, (const double (*)[2])pts, (const double (*)[2])rows);
}

static bool k_pokemon_tower(const RgSpec *spec, int a0, int a1, RgPartList *out)
{
    static const double plat[4][2] = {{112, 0}, {112, 31}, {86, 31}, {86, 0}};
    static const double platr[4][2] = {{81, 112}, {55, 81}, {0, 0}, {0, 0}};
    static const double rim[4][2] = {{86, 0}, {86, 31}, {31, 31}, {31, 0}};
    static const double rimr[4][2] = {{0, 0}, {0, 55}, {0, 0}, {0, 0}};
    static const double body[4][2] = {{102, 31}, {102, 102}, {50, 102}, {50, 31}};
    static const double bodyr[4][2] = {{0, 71}, {8, 9}, {0, 0}, {0, 0}};
    static const double tier2[4][2] = {{94, 102}, {94, 128}, {60, 128}, {60, 102}};
    static const double tier3[4][2] = {{88, 128}, {88, 150}, {66, 150}, {66, 128}};
    static const double tierr[4][2] = {{8, 9}, {8, 9}, {0, 0}, {0, 0}};
    static const double door[4][2] = {{113, 1}, {113, 18}, {108, 18}, {108, 1}};
    static const double doorr[4][2] = {{95, 112}, {90, 95}, {0, 0}, {0, 0}};
    unsigned i;

    (void)spec; (void)a0; (void)a1;
    if (!lv_profile(out, "platform", 16, 128, true, 4, plat, platr) ||
        !lv_profile(out, "rim_w", 16, 24, true, 4, rim, rimr) ||
        !lv_profile(out, "rim_e", 120, 128, true, 4, rim, rimr) ||
        !lv_profile(out, "tower", 39, 105, false, 4, body, bodyr) ||
        !lv_profile(out, "tier2", 48, 96, true, 4, tier2, tierr) ||
        !lv_profile(out, "tier3", 58, 86, true, 4, tier3, tierr))
        return false;
    for (i = 0; i < 8; i++) {                       /* the chamfers: left x 23-39, right x 105-121 */
        double xl = 23 + 2 * i, zf = floor(xl + 1 + 62 + 0.5);

        if (!tw_chamfer(out, xl, xl + 2, zf) || !tw_chamfer(out, 144 - xl - 2, 144 - xl, zf))
            return false;
    }
    return lv_profile(out, "door", 59, 85, true, 4, door, doorr) && !out->failed;
}

static const RgExact kTowerExact[7] = {
    {39, 0, 105, 71, false},        /* the front face: window bays, pilasters, plinth */
    {25, 0, 39, 56, false},         /* the left chamfer, above its diagonal */
    {105, 0, 119, 56, false},       /* the right chamfer */
    {16, 81, 126, 112, false},      /* the platform's front wall */
    {16, 71, 126, 81, false},       /* the platform's top in front of the tower */
    {16, 0, 24, 55, false},         /* the left rim strip */
    {120, 0, 126, 55, false},       /* the right rim strip */
};

/* ---- k_power_plant: 176x128 art (rect (2,34), 11x8 on Route 10) ----------------------------------------------------- */
/* The Power Plant: a flat-roofed hall (x 16-160) with chamfered corners and four turbine hoods on the roof. The wall is
 * 37 rows (blue-grey louvres on the wings, glass on the centre block that stands 8 rows further forward), under a
 * 12-row bevelled rim (white edge, yellow slab), then the pink roof (rows 26 to the rim) behind a parapet face (rows
 * 16-26). Each x-range is one profile prism whose front depth is the wall's bottom row: the flat wings and the centre
 * are one prism each, the four chamfers are 2-px slices that follow the diagonal. The hoods are boxes on the roof:
 * front face rows 26-54, the dark opening on top rows 12-26. */
static bool pp_slice(RgPartList *out, double x0, double x1, double zf, double t)
{
    double pts[7][2], rows[7][2];
    double zb = t + 10 + 43;
    unsigned i;

    pts[0][0] = zf;      pts[0][1] = 0;
    pts[1][0] = zf;      pts[1][1] = 37;
    pts[2][0] = zf - 6;  pts[2][1] = 43;
    pts[3][0] = zb;      pts[3][1] = 43;
    pts[4][0] = zb;      pts[4][1] = 53;
    pts[5][0] = zb - 10; pts[5][1] = 53;
    pts[6][0] = zb - 10; pts[6][1] = 0;
    for (i = 0; i < 7; i++)
        rows[i][0] = rows[i][1] = 0;
    rows[0][0] = zf - 37; rows[0][1] = zf;          /* wall */
    rows[1][0] = zf - 49; rows[1][1] = zf - 37;     /* the bevelled rim */
    rows[2][0] = t + 10;  rows[2][1] = zf - 49;     /* the roof */
    rows[3][0] = t;       rows[3][1] = t + 10;      /* the parapet face */
    return lv_profile(out, "hall", x0, x1, true, 7, (const double (*)[2])pts, (const double (*)[2])rows);
}

static bool pp_hood(RgPartList *out, double x0)
{
    static const double pts[4][2] = {{97, 43}, {97, 71}, {83, 71}, {83, 43}};
    static const double rows[4][2] = {{26, 54}, {12, 26}, {0, 0}, {0, 0}};

    return lv_profile(out, "hood", x0, x0 + 16, true, 4, pts, rows);
}

static bool k_power_plant(const RgSpec *spec, int a0, int a1, RgPartList *out)
{
    unsigned i;
    double xm, zf, t;

    (void)spec; (void)a0; (void)a1;
    for (i = 0; i < 6; i++) {                       /* outer chamfers, x 16-28 and 148-160 */
        double xl = 16 + 2 * i;

        xm = xl + 1;
        zf = floor(106 + 0.5 * (xm - 16) + 0.5);
        t = floor(25 - 0.75 * (xm - 16) + 0.5);
        if (!pp_slice(out, xl, xl + 2, zf, t) || !pp_slice(out, 176 - xl - 2, 176 - xl, zf, t))
            return false;
    }
    if (!pp_slice(out, 28, 48, 112, 16) || !pp_slice(out, 128, 148, 112, 16))
        return false;
    for (i = 0; i < 5; i++) {                       /* inner chamfers, x 48-58 and 118-128 */
        double xl = 48 + 2 * i;

        xm = xl + 1;
        zf = floor(112 + 0.8 * (xm - 48) + 0.5);
        if (!pp_slice(out, xl, xl + 2, zf, 16) || !pp_slice(out, 176 - xl - 2, 176 - xl, zf, 16))
            return false;
    }
    if (!pp_slice(out, 58, 118, 120, 16))
        return false;
    for (i = 0; i < 4; i++)
        if (!pp_hood(out, 32 + 32 * i))
            return false;
    return !out->failed;
}

static const RgExact kPlantExact[11] = {
    {28, 26, 48, 111, false},       /* left wing: roof, rim, louvres */
    {58, 26, 118, 119, false},      /* centre: roof, rim, glass */
    {128, 26, 148, 111, false},     /* right wing */
    {32, 12, 48, 26, false},        /* the four hood openings */
    {64, 12, 80, 26, false},
    {96, 12, 112, 26, false},
    {128, 12, 144, 26, false},
    {48, 26, 58, 76, false},        /* the inner chamfers, above their diagonals */
    {118, 26, 128, 76, false},
    {16, 30, 28, 100, false},       /* the outer chamfers */
    {148, 30, 160, 100, false},
};


static const RgExact kHouseExact[1] = {{0, 10, 80, 68, false}};

const RgSpec rg_kspecs_lavender[] = {
    {"k_pokemon_tower", RG_SPEC_DIRECT, L_LAVENDER, {14, 0, 9, 7}, {0, 0}, LV_GROUND, 1, kTowerExact, 7,
     k_pokemon_tower, 0, 0, NULL},
    {"k_power_plant", RG_SPEC_DIRECT, L_ROUTE10, {2, 34, 11, 8}, {0, 0}, LV_GROUND, 1, kPlantExact, 11,
     k_power_plant, 0, 0, NULL},
    {"k_lavender_house", RG_SPEC_DIRECT, L_LAVENDER, {8, 8, 5, 5}, {1, 4}, LV_GROUND, 1, kHouseExact, 1,
     k_lavender_house, 0, 0, NULL},
};
const unsigned rg_kspecs_lavender_count = sizeof(rg_kspecs_lavender) / sizeof(rg_kspecs_lavender[0]);
