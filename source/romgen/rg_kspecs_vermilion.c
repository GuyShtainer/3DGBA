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

static const RgExact kHouseExact[1] = {{0, 12, 64, 64, false}};
static const RgExact kGreenExact[1] = {{0, 0, 80, 64, false}};

const RgSpec rg_kspecs_vermilion[] = {
    {"k_vermilion_fanclub", RG_SPEC_DIRECT, L_VERMILION, {8, 3, 5, 4}, {0, 0}, VM_GROUND, 1, kFanExact, 1,
     k_vermilion_flat, 80, 0, NULL},
    {"k_vermilion_house", RG_SPEC_DIRECT, L_VERMILION, {18, 14, 4, 4}, {1, 4}, VM_GROUND, 1, kHouseExact, 1,
     k_vermilion_flat, 64, 0, NULL},
    {"k_vermilion_green", RG_SPEC_DIRECT, L_VERMILION, {11, 14, 5, 4}, {0, 0}, VM_GROUND, 1, kGreenExact, 1,
     k_vermilion_green, 80, 0, NULL},
};
const unsigned rg_kspecs_vermilion_count = sizeof(rg_kspecs_vermilion) / sizeof(rg_kspecs_vermilion[0]);
