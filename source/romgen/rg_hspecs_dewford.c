/* rg_hspecs_dewford.c -- Dewford Town recipes (3DGBA original work, GPLv3). Phase 36 slice H1.
 *
 * Every number was read off `romgen author roms/emerald.gba art 12 ...` (Ruby and Sapphire share the layout byte for byte,
 * docs/phase36-hoenn/PHASE.md). Kanto-style profile prisms: every face the front camera sees is a PROJ edge (art row =
 * z - y), so the ortho gate holds by construction where the geometry is right.
 *
 *   dewford_house_w   5x4 cells (80x64), layout 12 rect (1, 0): the blue tiled-roof house in the north-west
 *   dewford_house     4x4 cells (64x64), layout 12: the two smaller tiled-roof houses */
#include "rg_hspecs.h"

/* A blue tiled roof over a plastered facade, `width` px wide, 64 art rows: the ridge cap 0-16 (a 5-degree rise), the
 * tiled slope 16-35 (15 degrees, rg_close_backs' L6b pitch, so it is not laid down again), the eave 35-38, the facade
 * with posts, door and window 38-64 (the right shadow columns ride on the wall). */
bool rg_h_dewford_house(const RgSpec *spec, int width, int a1, RgPartList *out)
{
    double pts[7][2], rows[7][2] = {{38, 64}, {35, 38}, {16, 35}, {2, 16}, {0, 0}, {0, 0}, {0, 0}};
    double zs, ys, zr, yr;

    (void)spec; (void)a1;
    rg_h_slope(64, 29, 19, RG_H_PITCH, &zs, &ys);
    rg_h_slope(zs, ys, 16, 5, &zr, &yr);
    pts[0][0] = 64; pts[0][1] = 0;
    pts[1][0] = 64; pts[1][1] = 26;
    pts[2][0] = 64; pts[2][1] = 29;
    pts[3][0] = zs; pts[3][1] = ys;
    pts[4][0] = zr; pts[4][1] = yr;
    pts[5][0] = zr - 1; pts[5][1] = yr;
    pts[6][0] = zr - 1; pts[6][1] = 0;
    return rg_h_profile(out, "house", 0, width, true, 7, (const double (*)[2])pts, (const double (*)[2])rows) &&
           !out->failed;
}

const RgExact rg_h_dewford_house_w_exact[RG_H_DEWFORD_HOUSE_W_NEXACT] = {
    {0, 38, 80, 64, false},     /* facade, posts, the right shadow */
    {0, 2, 80, 38, false},      /* ridge cap, slope, eave (rows 0-1 carry the forest's overhang) */
};

const RgExact rg_h_dewford_house_exact[RG_H_DEWFORD_HOUSE_NEXACT] = {
    {0, 38, 64, 64, false},
    {0, 2, 64, 38, false},
};

/* look L5 (rg_close_sides): the ends are dressed from the facade's rows; these patches are the fallback. */
const RgSideCfg rg_h_dewford_house_side[1] = {
    {"house", {40, 56, 48, 62}, {40, 20, 48, 30}, 26, true},
};
