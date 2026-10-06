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

/* ---- k_vermilion_fanclub: 80x64 art (rect (8,3), 5x4) -------------------------------------------------------------- */
/* Rows: rock and water behind 0-9, the scalloped yellow roof 9-41, fascia 41-43, the facade with the door, window and
 * the two planters 43-63. A low sloped roof over a one-storey wall. */
static bool k_vermilion_fanclub(const RgSpec *spec, int a0, int a1, RgPartList *out)
{
    static const double pts[6][2] = {{64, 0}, {64, 21}, {64, 24}, {48, 40}, {40, 40}, {40, 0}};
    static const double rows[6][2] = {{43, 64}, {40, 43}, {8, 40}, {0, 0}, {0, 0}, {0, 0}};

    (void)spec; (void)a0; (void)a1;
    return vm_profile(out, "fanclub", 0, 80, true, 6, pts, rows) && !out->failed;
}

static const RgExact kFanExact[1] = {{0, 12, 80, 64, false}};

const RgSpec rg_kspecs_vermilion[] = {
    {"k_vermilion_fanclub", RG_SPEC_DIRECT, L_VERMILION, {8, 3, 5, 4}, {0, 0}, VM_GROUND, 1, kFanExact, 1,
     k_vermilion_fanclub, 0, 0, NULL},
};
const unsigned rg_kspecs_vermilion_count = sizeof(rg_kspecs_vermilion) / sizeof(rg_kspecs_vermilion[0]);
