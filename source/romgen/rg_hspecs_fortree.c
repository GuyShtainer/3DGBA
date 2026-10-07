/* rg_hspecs_fortree.c -- Fortree City recipes (3DGBA original work, GPLv3). Phase 36 slice H2.
 *
 * Every number was read off `romgen author roms/emerald.gba art 5 ...` (Ruby and Sapphire share the layout byte for byte,
 * docs/phase36-hoenn/PHASE.md). The gym is the Petalburg gym (rg_gym, rg_hspecs.h).
 *
 *   fortree_hut   5x3 cells (80x48), layout 5 rect (8, 1): the treehouse at door (10,3). The six treehouses share the
 *                 hut's two lower metatile rows (the frond row's corners and the deck below differ: neighbours, the
 *                 ladder), so one model, matched on rows 1-2, takes all six placements.
 *
 * Fortree's huts stand on log decks up in the trees, and the drawing says so only through the ladders: the deck is
 * walked on at the player's own height, so a raised deck would bury the player. The recipe is therefore the hut alone, on
 * the ground: the plank hut with its open doorway, the palm fronds over it as a low pitched canopy, and the log stacks
 * either side as thin walls. The deck, the ladder and the forest stay the map's flat drawing. */
#include "rg_hspecs.h"

/* A pitched profile over x0..x1, `height` art rows tall (front face at z = height): the wall carries rows
 * eave..height, a RG_H_PITCH slope rows ridge..eave, a 5-degree cap rows 0..ridge; the top ends one pixel short of the
 * skipped back drop (rg_close_backs mirrors the slope there). rg_h_gable_t's shape over an x range. */
static bool h_pitched(RgPartList *out, const char *name, double x0, double x1, double height, double eave, double ridge)
{
    double pts[6][2], rows[6][2] = {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}};
    double zs, ys, zr, yr;

    rg_h_slope(height, height - eave, eave - ridge, RG_H_PITCH, &zs, &ys);
    rg_h_slope(zs, ys, ridge, 5, &zr, &yr);
    pts[0][0] = height; pts[0][1] = 0;              rows[0][0] = eave; rows[0][1] = height;
    pts[1][0] = height; pts[1][1] = height - eave;  rows[1][0] = ridge; rows[1][1] = eave;
    pts[2][0] = zs; pts[2][1] = ys;                 rows[2][0] = 0; rows[2][1] = ridge;
    pts[3][0] = zr; pts[3][1] = yr;
    pts[4][0] = zr - 1; pts[4][1] = yr;
    pts[5][0] = zr - 1; pts[5][1] = 0;
    return rg_h_profile(out, name, x0, x1, true, 6, (const double (*)[2])pts, (const double (*)[2])rows);
}

/* A standing card over x0..x1: the front carries rows top..height, a one-pixel top. */
static bool h_card(RgPartList *out, const char *name, double x0, double x1, double height, double top)
{
    double h = height - top;
    const double pts[4][2] = {{height, 0}, {height, h}, {height - 1, h}, {height - 1, 0}};
    const double rows[4][2] = {{top, height}, {0, 0}, {0, 0}, {0, 0}};

    return rg_h_profile(out, name, x0, x1, true, 4, pts, rows);
}

/* The hut, 48 art rows: the fronds 0-24 (the cap 0-8, the 15-degree canopy 8-24 over the hut's top), the hut front
 * with the doorway 24-48 (its two bottom rows are the deck's edge); the log stacks either side 19-48. */
bool rg_h_fortree_hut(const RgSpec *spec, int a0, int a1, RgPartList *out)
{
    (void)spec; (void)a0; (void)a1;
    /* three spans, so L5 dresses each end from its own plank panel and not from the dark doorway between them */
    return h_pitched(out, "hut_w", 18, 32, 48, 24, 8) && h_pitched(out, "hut_door", 32, 48, 48, 24, 8) &&
           h_pitched(out, "hut_e", 48, 62, 48, 24, 8) && h_card(out, "logs_w", 8, 18, 48, 19) &&
           h_card(out, "logs_e", 62, 72, 48, 19) && !out->failed;
}

const RgExact rg_h_fortree_hut_exact[3] = {
    {18, 24, 62, 48, false},    /* the hut front, the doorway */
    {22, 2, 58, 24, false},     /* the fronds (their ragged rim shows the forest behind) */
    {8, 20, 72, 48, false},     /* the log stacks either side */
};

/* look L5 (rg_close_sides): the ends are dressed from the front's rows; these patches are the fallback. */
const RgSideCfg rg_h_fortree_hut_side[2] = {
    {"hut", {20, 30, 30, 44}, {30, 10, 40, 20}, 24, false},
    {"logs", {9, 24, 17, 44}, {9, 24, 17, 44}, 48, true},
};
