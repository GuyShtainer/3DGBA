/* rg_hspecs_lilycove.c -- Lilycove City recipes (3DGBA original work, GPLv3). Phase 36 slice H3.
 *
 * Every number was read off `romgen author roms/emerald.gba art 6 ...`. Kanto-style profile prisms: every face the front
 * camera sees is a PROJ edge. See docs/phase36-hoenn/BUILDLOG-H3.md for the per-building notes.
 *
 *   lilycove_house     4x4 cells (64x64), rect (54, 12): the blue slatted-roof house (five of them in town)
 *   lilycove_house_w   5x4 cells (80x64), rect (37, 11): the same kit, a cell wider
 *   lilycove_store     9x7 cells (144x112), rect (23, 0): the Department Store, a facade whose roof is off the map
 *   lilycove_museum    10x6 cells (160x96), rect (7, 0): the museum, a stepped tower over two wings
 *   lilycove_hall      7x7 cells (112x112), rect (20, 18): the Contest Hall, a bevelled skylit roof over a red facade
 *   lilycove_pavilion  7x6 cells (112x96), rect (9, 27): the big orange-roofed hall on the harbour road
 *   lilycove_wood      6x5 cells (96x80), rect (36, 20): the wooden-roofed house
 *   lilycove_cave      3x2 cells (48x32), rect (69, 4): the stone arch of the sea cave in the cliff */
#include "rg_hspecs.h"

/* The blue slatted roof, 64 art rows: the rail and its light bar 0-14, the slatted slope 14-37, the facade 37-64. */
bool rg_h_lilycove_house(const RgSpec *spec, int width, int a1, RgPartList *out)
{
    (void)spec; (void)a1;
    return rg_h_gable(out, width, 64, 37, 14);
}

/* The Department Store, 112 art rows: it is a facade only, the map ends above it, so the top 8 rows stand for the roof.
 * Wall rows 8-112 (its ledge 104-112), the roof rows 0-8. */
bool rg_h_lilycove_store(const RgSpec *spec, int a0, int a1, RgPartList *out)
{
    (void)spec; (void)a0; (void)a1;
    return rg_h_flat(out, 144, 112, 8);
}

/* The museum, 160 px wide, rows 0-96 of its 112 (the cell row below, the balustrade over the cliff foot, stays the map's
 * flat art, so the steps and the doors' approach are open ground). Three levels in one flush front (z = 95): the wings
 * 0-40 and 120-160 (wall rows 65-95, flat roof 33-65) and the tower 40-120 whose wall climbs to row 41 (roof 9-41). */
bool rg_h_lilycove_museum(const RgSpec *spec, int a0, int a1, RgPartList *out)
{
    static const double pts_w[5][2] = {{96, 0}, {96, 31}, {64, 31}, {63, 31}, {63, 0}};
    static const double rows_w[5][2] = {{65, 96}, {33, 65}, {0, 0}, {0, 0}, {0, 0}};
    static const double pts_t[5][2] = {{96, 0}, {96, 55}, {64, 55}, {63, 55}, {63, 0}};
    static const double rows_t[5][2] = {{41, 96}, {9, 41}, {0, 0}, {0, 0}, {0, 0}};

    (void)spec; (void)a0; (void)a1;
    return rg_h_profile(out, "wing_w", 0, 40, true, 5, pts_w, rows_w) &&
           rg_h_profile(out, "tower", 40, 120, true, 5, pts_t, rows_t) &&
           rg_h_profile(out, "wing_e", 120, 160, true, 5, pts_w, rows_w) && !out->failed;
}

/* The Contest Hall, 112 art rows: rows 0-8 are ground, the skylit roof 8-76 and the grey fascia 76-86 (one flat top,
 * its top corners bevelled), the red facade with the doors 86-112. */
bool rg_h_lilycove_hall(const RgSpec *spec, int a0, int a1, RgPartList *out)
{
    (void)spec; (void)a0; (void)a1;
    return rg_h_flat_t(out, 112, 112, 86, 8);
}

/* The pavilion, 96 art rows: the scalloped orange roof 0-58, its red eave 58-62, the facade with the sign band, the
 * windows and the doorway 62-96. */
bool rg_h_lilycove_pavilion(const RgSpec *spec, int a0, int a1, RgPartList *out)
{
    (void)spec; (void)a0; (void)a1;
    return rg_h_flat(out, 112, 96, 62);
}

/* The cave mouth, 32 art rows: the stone arch over the dark doorway 8-32 on the cliff face, the arch's crown 0-8. A low
 * block, 24 px tall, standing out from the rock. */
bool rg_h_lilycove_cave(const RgSpec *spec, int a0, int a1, RgPartList *out)
{
    (void)spec; (void)a0; (void)a1;
    return rg_h_flat(out, 48, 32, 8);
}

/* A pitched profile over x0..x1 (rg_h_gable_t's shape over an x range): the wall carries rows eave..height, a RG_H_PITCH
 * slope rows ridge..eave, a 5-degree cap rows top..ridge. */
static bool h_pitched_t(RgPartList *out, const char *name, double x0, double x1, double height, double eave, double ridge,
                        double top)
{
    double pts[6][2], rows[6][2] = {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}};
    double zs, ys, zr, yr;

    rg_h_slope(height, height - eave, eave - ridge, RG_H_PITCH, &zs, &ys);
    rg_h_slope(zs, ys, ridge - top, 5, &zr, &yr);
    pts[0][0] = height; pts[0][1] = 0;              rows[0][0] = eave; rows[0][1] = height;
    pts[1][0] = height; pts[1][1] = height - eave;  rows[1][0] = ridge; rows[1][1] = eave;
    pts[2][0] = zs; pts[2][1] = ys;                 rows[2][0] = top; rows[2][1] = ridge;
    pts[3][0] = zr; pts[3][1] = yr;
    pts[4][0] = zr - 1; pts[4][1] = yr;
    pts[5][0] = zr - 1; pts[5][1] = 0;
    return rg_h_profile(out, name, x0, x1, true, 6, (const double (*)[2])pts, (const double (*)[2])rows);
}

/* The wooden house, 80 art rows: the facade 52-80 across 96 px; over it the centre's slatted roof 0-52 (x 16-80) and
 * the two lower side roofs 16-52 (x 0-16 and 80-96). */
bool rg_h_lilycove_wood(const RgSpec *spec, int a0, int a1, RgPartList *out)
{
    (void)spec; (void)a0; (void)a1;
    return h_pitched_t(out, "wood_w", 0, 16, 80, 52, 28, 16) && h_pitched_t(out, "wood_c", 16, 80, 80, 52, 16, 0) &&
           h_pitched_t(out, "wood_e", 80, 96, 80, 52, 28, 16) && !out->failed;
}

const RgExact rg_h_lilycove_house_exact[2] = {
    {0, 37, 64, 64, false},
    {0, 0, 64, 37, false},
};
const RgExact rg_h_lilycove_house_w_exact[2] = {
    {0, 37, 80, 64, false},
    {0, 0, 80, 37, false},
};
const RgExact rg_h_lilycove_store_exact[2] = {
    {0, 8, 144, 112, false},
    {0, 0, 144, 8, false},
};
const RgExact rg_h_lilycove_museum_exact[4] = {
    {0, 65, 160, 96, false},
    {40, 41, 120, 65, false},
    {0, 33, 160, 65, false},
    {40, 9, 120, 41, false},
};
const RgExact rg_h_lilycove_hall_exact[4] = {
    {0, 86, 112, 112, false},
    {0, 16, 112, 86, false},
    {8, 8, 104, 16, false},
    {0, 8, 112, 16, true},
};
const RgExact rg_h_lilycove_pavilion_exact[3] = {
    {0, 62, 112, 96, false},
    {0, 6, 112, 62, false},
    {0, 0, 112, 6, true},
};
const RgExact rg_h_lilycove_cave_exact[2] = {
    {0, 8, 48, 32, false},
    {0, 0, 48, 8, false},
};
const RgExact rg_h_lilycove_wood_exact[3] = {
    {0, 52, 96, 80, false},
    {16, 0, 80, 52, false},
    {0, 16, 96, 52, false},
};

/* look L5 (rg_close_sides): the ends are dressed from the facade's rows; these patches are the fallback. */
const RgSideCfg rg_h_lilycove_house_side[1] = {
    {"house", {20, 58, 30, 62}, {20, 20, 30, 30}, 27, true},
};
const RgSideCfg rg_h_lilycove_store_side[1] = {
    {"block", {50, 18, 60, 28}, {50, 2, 60, 6}, 8, true},
};
const RgSideCfg rg_h_lilycove_museum_side[1] = {
    {"wing", {10, 70, 20, 80}, {10, 40, 20, 50}, 65, true},
};
const RgSideCfg rg_h_lilycove_hall_side[1] = {
    {"block", {10, 90, 20, 106}, {20, 20, 30, 30}, 86, true},
};
const RgSideCfg rg_h_lilycove_pavilion_side[1] = {
    {"block", {10, 66, 20, 74}, {20, 20, 30, 30}, 62, true},
};
const RgSideCfg rg_h_lilycove_wood_side[1] = {
    {"wood", {40, 56, 46, 76}, {40, 20, 46, 30}, 52, true},
};
const RgSideCfg rg_h_lilycove_cave_side[1] = {
    {"block", {2, 12, 8, 24}, {2, 2, 8, 6}, 8, true},
};
