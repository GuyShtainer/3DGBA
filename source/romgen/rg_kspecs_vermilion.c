/* rg_kspecs_vermilion.c -- Vermilion City and Routes 5-8 recipes (FireRed / LeafGreen).
 * 3DGBA original work, GPLv3, "Kanto recipes". Phase 34 slice K6 (docs/phase34-frlg/SPEC.md sections 5.3-5.6).
 *
 * Every number below was read off the `romgen author ... art` pictures of the user's own ROM (art row = z - y for the
 * renderer's oblique camera). The builders reuse the Emerald part library (rg_geom) unchanged; every face is a PROJ
 * edge, which copies the art by row, so the ortho check holds by construction where the geometry is right.
 *
 *   k_vermilion_fanclub  5x4 cells (80x64): the Pokemon Fan Club, a corrugated yellow roof over a long facade (layout 83) */
#include "rg_bspecs.h"

#include <string.h>

#define L_VERMILION 83, 0x82FF5FADu
#define L_ROUTE5 93, 0xA0C68725u
#define L_ROUTE6 94, 0x00000000u
#define L_ROUTE7 95, 0x00000000u
#define L_ROUTE8 96, 0x00000000u
#define VM_GROUND {0x001}

static void vm_pt(double (*poly)[2], unsigned i, double z, double y)
{
    poly[i][0] = z;
    poly[i][1] = y;
}

/* A profile prism: poly points (z, y); each edge PROJ-textured from the listed art rows (0,0 = skipped). */
static bool vm_profile(RgPartList *out, const char *name, double x0, double x1, bool ends, unsigned n,
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
        vm_pt(pr->poly, i, pts[i][0], pts[i][1]);
        if (rows[i][0] == 0 && rows[i][1] == 0) {
            pr->skip |= 1u << i;
        } else {
            pr->edges[i].kind = RG_EM_PROJ;
            pr->edges[i].proj = rg_proj_rows(rows[i][0], rows[i][1]);
        }
    }
    return true;
}

static void vm_box(RgPrism *p, double front, double ylo, double yhi, double back)
{
    p->nPoly = 4;
    vm_pt(p->poly, 0, front, ylo);
    vm_pt(p->poly, 1, front, yhi);
    vm_pt(p->poly, 2, back, yhi);
    vm_pt(p->poly, 3, back, ylo);
}

/* A thin upright plane at depth z, heights y0..y1, PROJ-textured from art rows rlo..rhi. */
static void vm_vplane(RgPartList *out, const char *name, double x0, double x1, double z, double y0, double y1,
                      double rlo, double rhi)
{
    RgPart *pt = rg_parts_add(out, RG_P_PRISM, name);
    RgPrism *pr;

    if (pt == NULL)
        return;
    pr = &pt->u.prism;
    pr->x0 = x0; pr->x1 = x1;
    pr->west = pr->east = false;
    vm_box(pr, z, y0, y1, z - 1);
    pr->edges[0].kind = RG_EM_PROJ;
    pr->edges[0].proj = rg_proj_rows(rlo, rhi);
    pr->skip = (1u << 1) | (1u << 2) | (1u << 3);
}

/* A level face at height y spanning depths z0..z1: art rows z0 - y .. z1 - y. */
static void vm_hplane(RgPartList *out, const char *name, double x0, double x1, double y, double z0, double z1)
{
    RgPart *pt = rg_parts_add(out, RG_P_PRISM, name);
    RgPrism *pr;

    if (pt == NULL)
        return;
    pr = &pt->u.prism;
    pr->x0 = x0; pr->x1 = x1;
    pr->west = pr->east = false;
    vm_box(pr, z1, y - 1, y, z0);
    pr->edges[1].kind = RG_EM_PROJ;
    pr->edges[1].proj = rg_proj_rows(z0 - y, z1 - y);
    pr->skip = (1u << 0) | (1u << 2) | (1u << 3);
}

/* ---- k_vermilion_fanclub (80x64, rect (8,3), 5x4) and k_vermilion_house (64x64, rect (18,14), 4x4) ----------------- */
/* One builder, arg0 = width. The house's rect starts one row above its census seed (the roof edge starts 10 px into the
 * cell row above) and matches only rows 1-4, because that top row differs between its two placements. */
/* Rows: rock and water behind 0-9, the scalloped yellow roof 9-41, fascia 41-43, the facade with the door, window and
 * the two planters 43-63. A low sloped roof over a one-storey wall. */
static bool k_vermilion_flat(const RgSpec *spec, int a0, int a1, RgPartList *out)
{
    static const double pts[6][2] = {{64, 0}, {64, 21}, {64, 24}, {48, 40}, {40, 40}, {40, 0}};
    static const double rows[6][2] = {{43, 64}, {40, 43}, {8, 40}, {0, 0}, {0, 0}, {0, 0}};

    (void)spec; (void)a1;
    return vm_profile(out, "house", 0, a0, true, 6, pts, rows) && !out->failed;
}

static const RgExact kFanExact[1] = {{0, 12, 80, 64, false}};
/* ---- k_vermilion_green: 80x64 art (rect (11,14), 5x4) -------------------------------------------------------------- */
/* Rows: the green roof slope 0-34 (a level top edge at row 0), the dark fascia with the orange rivets 34-40, the
 * facade with the arched emblem, door, window and planters 40-64. */
static bool k_vermilion_green(const RgSpec *spec, int a0, int a1, RgPartList *out)
{
    static const double pts[6][2] = {{64, 0}, {64, 24}, {64, 30}, {47, 47}, {40, 47}, {40, 0}};
    static const double rows[6][2] = {{40, 64}, {34, 40}, {0, 34}, {0, 0}, {0, 0}, {0, 0}};

    (void)spec; (void)a1;
    return vm_profile(out, "green", 0, a0, true, 6, pts, rows) && !out->failed;
}

/* ---- k_path_hut: 48x64 art (rect (30,28), 3x4 on Route 5) ----------------------------------------------------------- */
/* The Underground Path hut: a flat grey roof, a level face seen from above (rows 0-35, a roof-top edge at row 0),
 * the cornice 35-42, and the brick facade with pillars and the wooden door 42-64. The rect starts one row above the
 * census seed (the roof starts in the row above) and matches only rows 1-4, so the same cells on Routes 6, 7 and 8
 * are found whatever their top row holds. */
static bool k_path_hut(const RgSpec *spec, int a0, int a1, RgPartList *out)
{
    static const double pts[6][2] = {{64, 0}, {64, 22}, {64, 29}, {29, 29}, {29, 0}, {29, 0}};
    static const double rows[6][2] = {{42, 64}, {35, 42}, {0, 35}, {0, 0}, {0, 0}, {0, 0}};

    (void)spec; (void)a0; (void)a1;
    return vm_profile(out, "hut", 0, 48, true, 5, pts, rows) && !out->failed;
}

/* ---- k_daycare: 80x80 art (rect (21,21), 5x5 on Route 5) ------------------------------------------------------------ */
/* The Day Care: an orange hip roof whose end faces are drawn as 2-px diagonal steps (top row 14 at the corners down to
 * row 8 along the ridge), an eave band 40-43, and the cream facade with two windows and the door 43-80. The roof is cut
 * into x-slices whose first row follows the diagonal, as the Route 25 cottage does. */
static const struct { int x0, x1, r0; } kDaySl[13] = {
    {0, 1, 14}, {1, 3, 13}, {3, 5, 12}, {5, 7, 11}, {7, 9, 10}, {9, 11, 9}, {11, 69, 8},
    {69, 71, 9}, {71, 73, 10}, {73, 75, 11}, {75, 77, 12}, {77, 79, 13}, {79, 80, 14},
};

static bool k_daycare(const RgSpec *spec, int a0, int a1, RgPartList *out)
{
    unsigned i;

    (void)spec; (void)a0; (void)a1;
    for (i = 0; i < 13; i++) {
        double d = (40.0 - kDaySl[i].r0) / 2;
        double pts[6][2], rows[6][2];

        pts[0][0] = 80; pts[0][1] = 0;
        pts[1][0] = 80; pts[1][1] = 37;
        pts[2][0] = 80; pts[2][1] = 40;
        pts[3][0] = 80 - d; pts[3][1] = 40 + d;
        pts[4][0] = 40; pts[4][1] = 40 + d;
        pts[5][0] = 40; pts[5][1] = 0;
        rows[0][0] = 43; rows[0][1] = 80;           /* facade */
        rows[1][0] = 40; rows[1][1] = 43;           /* eave band */
        rows[2][0] = kDaySl[i].r0; rows[2][1] = 40; /* roof slope */
        rows[3][0] = rows[3][1] = rows[4][0] = rows[4][1] = rows[5][0] = rows[5][1] = 0;
        if (!vm_profile(out, "daycare", kDaySl[i].x0, kDaySl[i].x1, false, 6, (const double (*)[2])pts,
                        (const double (*)[2])rows))
            return false;
    }
    return !out->failed;
}

/* ---- k_route5_gate: 96x112 art (rect (22,32), 6x7 on Route 5) -------------------------------------------------------- */
/* The Route 5 south gatehouse (dest 17/1) seen from Route 5: the raised lip over the north door at rows 12-16, the flat
 * roof top 16-59, the cornice, a window band and brick facade down to row 104, a canopy (top 84-91, front 92-103)
 * between two white pillars, and the floor apron 104-111. The same shape as the Route 2 south half (k_route2_gate_s),
 * 96 px wide. The log posts and the sand path in the top and bottom rows stay flat ground. */
static bool k_route5_gate(const RgSpec *spec, int a0, int a1, RgPartList *out)
{
    RgPart *body = rg_parts_add(out, RG_P_PRISM, "body");
    RgPart *cn = rg_parts_add(out, RG_P_PRISM, "canopy");
    RgPrism *pr;

    (void)spec; (void)a0; (void)a1;
    if (body == NULL || cn == NULL)
        return false;
    pr = &body->u.prism;
    pr->x0 = 0; pr->x1 = 96;
    pr->west = pr->east = true;
    vm_box(pr, 104, 0, 45, 61);
    pr->edges[0].kind = RG_EM_PROJ;
    pr->edges[0].proj = rg_proj_rows(59, 104);      /* cornice, windows, brick */
    pr->edges[1].kind = RG_EM_PROJ;
    pr->edges[1].proj = rg_proj_rows(16, 59);       /* roof top */
    pr->skip = (1u << 2) | (1u << 3);

    pr = &cn->u.prism;
    pr->x0 = 23; pr->x1 = 73;
    pr->west = pr->east = false;
    vm_box(pr, 124, 20, 32, 116);
    pr->edges[0].kind = RG_EM_PROJ;
    pr->edges[0].proj = rg_proj_rows(92, 104);
    pr->edges[1].kind = RG_EM_PROJ;
    pr->edges[1].proj = rg_proj_rows(84, 92);
    pr->skip = (1u << 2) | (1u << 3);

    vm_vplane(out, "pillar_w", 17, 23, 124, 12, 40, 84, 112);
    vm_vplane(out, "pillar_e", 73, 79, 124, 12, 40, 84, 112);
    vm_vplane(out, "col_w", 0, 8, 112, 0, 8, 104, 112);
    vm_vplane(out, "col_e", 88, 96, 112, 0, 8, 104, 112);
    vm_hplane(out, "apron", 8, 88, 0, 104, 112);
    vm_vplane(out, "lip", 22, 74, 61, 45, 49, 12, 16);      /* the raised lip over the north door */
    return !out->failed;
}

static const RgExact kGate5Exact[2] = {
    {0, 16, 96, 112, false},    /* roof, cornice, windows, brick, canopy, pillars, apron */
    {22, 12, 74, 16, false},    /* the lip over the door */
};
static const RgExact kDayExact[13] = {
    {0, 14, 1, 80, false}, {1, 13, 3, 80, false}, {3, 12, 5, 80, false}, {5, 11, 7, 80, false}, {7, 10, 9, 80, false},
    {9, 9, 11, 80, false}, {11, 8, 69, 80, false}, {69, 9, 71, 80, false}, {71, 10, 73, 80, false},
    {73, 11, 75, 80, false}, {75, 12, 77, 80, false}, {77, 13, 79, 80, false}, {79, 14, 80, 80, false},
};
static const RgExact kHutExact[1] = {{0, 0, 48, 64, false}};
static const RgExact kHouseExact[1] = {{0, 12, 64, 64, false}};
static const RgExact kGreenExact[1] = {{0, 0, 80, 64, false}};

const RgSpec rg_kspecs_vermilion[] = {
    {"k_vermilion_fanclub", RG_SPEC_DIRECT, L_VERMILION, {8, 3, 5, 4}, {0, 0}, VM_GROUND, 1, kFanExact, 1,
     k_vermilion_flat, 80, 0, NULL},
    {"k_vermilion_house", RG_SPEC_DIRECT, L_VERMILION, {18, 14, 4, 4}, {1, 4}, VM_GROUND, 1, kHouseExact, 1,
     k_vermilion_flat, 64, 0, NULL},
    {"k_vermilion_green", RG_SPEC_DIRECT, L_VERMILION, {11, 14, 5, 4}, {0, 0}, VM_GROUND, 1, kGreenExact, 1,
     k_vermilion_green, 80, 0, NULL},
    {"k_path_hut", RG_SPEC_DIRECT, L_ROUTE5, {30, 28, 3, 4}, {1, 4}, VM_GROUND, 1, kHutExact, 1,
     k_path_hut, 0, 0, NULL},
    {"k_daycare", RG_SPEC_DIRECT, L_ROUTE5, {21, 21, 5, 5}, {0, 0}, VM_GROUND, 1, kDayExact, 13,
     k_daycare, 0, 0, NULL},
    {"k_route5_gate", RG_SPEC_DIRECT, L_ROUTE5, {22, 32, 6, 7}, {0, 0}, VM_GROUND, 1, kGate5Exact, 2,
     k_route5_gate, 0, 0, NULL},
};
const unsigned rg_kspecs_vermilion_count = sizeof(rg_kspecs_vermilion) / sizeof(rg_kspecs_vermilion[0]);
