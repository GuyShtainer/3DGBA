/* rg_hspecs_mauville.c -- Mauville City recipes (3DGBA original work, GPLv3). Phase 36 slice H1.
 *
 * Every number was read off `romgen author roms/emerald.gba art 3 ...` (Ruby and Sapphire share the layout byte for byte,
 * docs/phase36-hoenn/PHASE.md). Kanto-style profile prisms: every face the front camera sees is a PROJ edge.
 *
 *   mauville_house     4x4 cells (64x64), rect (18, 11): the pink-roofed house next to the Mart
 *   mauville_bike      5x4 cells (80x64), rect (34, 2): the Bike Shop (same roof kit)
 *   mauville_house_e   5x4 cells (80x64), rect (31, 11): the pink-roofed house in the south-east (same roof kit)
 *   mauville_block     4x5 cells (64x80), rect (36, 11): the flat grey-roofed yellow block beside it (no door)
 *   game_corner        7x4 cells (112x64), rect (5, 10): the Game Corner, a flat roof over the sign band */
#include "rg_hspecs.h"

/* The pink roof kit (metatiles 643-645 over 651-653), 64 art rows: the light top band 0-14 (a 5-degree rise), the
 * ribbed slope 14-32 (15 degrees), the fascia 32-38 and the facade 38-64 on one wall. */
bool rg_h_mauville_gable(const RgSpec *spec, int width, int a1, RgPartList *out)
{
    double pts[6][2], rows[6][2] = {{32, 64}, {14, 32}, {0, 14}, {0, 0}, {0, 0}, {0, 0}};
    double zs, ys, zr, yr;

    (void)spec; (void)a1;
    rg_h_slope(64, 32, 18, RG_H_PITCH, &zs, &ys);
    rg_h_slope(zs, ys, 14, 5, &zr, &yr);
    pts[0][0] = 64; pts[0][1] = 0;
    pts[1][0] = 64; pts[1][1] = 32;
    pts[2][0] = zs; pts[2][1] = ys;
    pts[3][0] = zr; pts[3][1] = yr;
    pts[4][0] = zr - 1; pts[4][1] = yr;
    pts[5][0] = zr - 1; pts[5][1] = 0;
    return rg_h_profile(out, "house", 0, width, true, 6, (const double (*)[2])pts, (const double (*)[2])rows) &&
           !out->failed;
}

/* A flat-roofed block `width` px wide and `height` art rows tall: the wall carries rows wallTop..height, the level top the
 * rows above it. As in the gables, the top ends one pixel short of the back wall (a skipped step) so the closure face
 * L6 lays there sits behind art row 0 instead of tying with the top's back edge. */
static bool flat_block(RgPartList *out, double width, double height, double wallTop)
{
    double h = height - wallTop, zb = height - wallTop;
    const double pts[5][2] = {{height, 0}, {height, h}, {zb, h}, {zb - 1, h}, {zb - 1, 0}};
    const double rows[5][2] = {{wallTop, height}, {0, wallTop}, {0, 0}, {0, 0}, {0, 0}};

    return rg_h_profile(out, "block", 0, width, true, 5, pts, rows) && !out->failed;
}

/* Rows (x 0-63): the grey roof with its skylight border 0-40, the yellow facade with two windows 40-80. */
bool rg_h_mauville_block(const RgSpec *spec, int a0, int a1, RgPartList *out)
{
    (void)spec; (void)a0; (void)a1;
    return flat_block(out, 64, 80, 40);
}

/* Rows (x 0-111): the corrugated roof 0-26 (its corners rounded off inside the top blocks), the eave 26-32, the sign
 * band 32-40, the facade with the windows, the entrance canopy and the door 40-64. */
bool rg_h_game_corner(const RgSpec *spec, int a0, int a1, RgPartList *out)
{
    (void)spec; (void)a0; (void)a1;
    return flat_block(out, 112, 64, 26);
}

const RgExact rg_h_mauville_house_exact[2] = {
    {0, 32, 64, 64, false},     /* fascia, facade */
    {0, 0, 64, 32, false},      /* top band, slope */
};
const RgExact rg_h_mauville_wide_exact[2] = {
    {0, 32, 80, 64, false},
    {0, 0, 80, 32, false},
};
/* Row 0 is the roofs' crenellated rim: its gaps are ground pixels, so the closure behind the top may show through. */
const RgExact rg_h_mauville_block_exact[2] = {{0, 1, 64, 80, false}, {0, 0, 64, 1, true}};
/* The Game Corner's roof also rounds its two top corners off (8 px); what shows through them is the closure. */
const RgExact rg_h_game_corner_exact[5] = {
    {0, 8, 112, 64, false}, {8, 1, 104, 8, false},
    {0, 0, 112, 1, true}, {0, 1, 8, 8, true}, {104, 1, 112, 8, true},
};

/* look L5 (rg_close_sides): the ends are dressed from the facade's rows; these patches are the fallback. */
const RgSideCfg rg_h_mauville_house_side[1] = {
    {"house", {2, 56, 8, 62}, {20, 16, 30, 30}, 32, true},
};
const RgSideCfg rg_h_mauville_block_side[1] = {
    {"block", {12, 56, 20, 62}, {12, 4, 20, 12}, 40, true},
};
const RgSideCfg rg_h_game_corner_side[1] = {
    {"block", {20, 58, 34, 62}, {20, 4, 34, 12}, 38, true},
};
