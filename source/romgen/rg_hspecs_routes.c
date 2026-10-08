/* rg_hspecs_routes.c -- Hoenn route and landmark recipes (3DGBA original work, GPLv3). Phase 36 slice H5.
 *
 * Every number was read off `romgen author roms/emerald.gba art LAYOUT ...`. Kanto-style profile prisms: every face the front
 * camera sees is a PROJ edge. See docs/phase36-hoenn/BUILDLOG-H5.md for the per-building notes.
 *
 *   league_gate        11x6 cells (176x96), rect (13, 0) on layout 9: the Pokemon League's facade, which runs off the map's top
 *   seashore_house     5x4 cells (80x64), rect (10, 2) on layout 25: the Seashore House on the beach
 *   trick_house        5x6 cells (80x96), rect (9, 61) on layout 26: the pink Trick House with its two lower wings
 *   cycling_gate       6x5 cells (96x80), layout 26 (rects (14, 12) and (15, 84)): the Cycling Road gates (a yellow brick front)
 *   winstrate_house    5x5 cells (80x80), rect (11, 109) on layout 27: the Winstrate family's house on Route 111, drawn in
 *                      the gates' yellow-brick kit a cell narrower
 *   cable_car_station  6x5 cells (96x80), layout 28 (rect (27, 23)) and layout 136 (rect (14, 32)): the Mt. Chimney cable car
 *                      stations, drawn with an oblique side wall (right on Route 112, left on the mountain)
 *   glass_workshop     4x4 cells (64x64), rect (32, 2) on layout 29: the ash-covered Glass Workshop
 *   route114_house     4x4 cells (64x64), rect (28, 2) on layout 30: the orange-roofed house in the Route 114 cliffs
 *   weather_institute  11x7 cells (176x112), rect (1, 26) on layout 35: the Weather Institute with its two domes
 *   entrance_gate      5x6 cells (80x96), rect (35, 0) on layout 37: the glass-doored gate whose roof runs off the map's top
 *   entrance_gate_wide 10x5 cells (160x80), rect (30, 29) on layout 241: the long gate at the map's right edge */
#include "rg_hspecs.h"

/* The League facade, 96 art rows: the map ends above it, so the top 8 rows stand for the roof; the wall is the window
 * bands, the awning and the glass entrance (rows 8-96). */
bool rg_h_league_gate(const RgSpec *spec, int a0, int a1, RgPartList *out)
{
    (void)spec; (void)a0; (void)a1;
    return rg_h_flat(out, 176, 96, 8);
}

/* The Seashore House, 64 art rows: the purple slatted roof with its rail 0-14, the slope 14-37, the facade 37-64. */
bool rg_h_seashore_house(const RgSpec *spec, int width, int a1, RgPartList *out)
{
    (void)spec; (void)a1;
    return rg_h_gable(out, width, 64, 37, 14);
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

/* The Trick House, 96 art rows over 80 px: the grey facade with its yellow pillars 68-96 across the width; over it the
 * centre's pink roof 0-68 (x 16-64) and the two lower wing roofs 16-68 (x 0-16 and 64-80). */
bool rg_h_trick_house(const RgSpec *spec, int a0, int a1, RgPartList *out)
{
    (void)spec; (void)a0; (void)a1;
    return h_pitched_t(out, "trick_w", 0, 16, 96, 68, 28, 16) && h_pitched_t(out, "trick_c", 16, 64, 96, 68, 12, 0) &&
           h_pitched_t(out, "trick_e", 64, 80, 96, 68, 28, 16) && !out->failed;
}

/* The Cycling Road gate, 80 art rows: the grey flat roof inside its battlements 0-37, the yellow brick facade with the two
 * pink doors and the arched windows 37-80. */
bool rg_h_cycling_gate(const RgSpec *spec, int width, int a1, RgPartList *out)
{
    (void)spec; (void)a1;
    return rg_h_flat(out, width, 80, 37);
}

/* The Mt. Chimney cable car stations, 80 art rows: the orange roof with its oblique side wall 0-45, the glazed facade with
 * the garage opening 45-80. */
bool rg_h_cable_car(const RgSpec *spec, int a0, int a1, RgPartList *out)
{
    (void)spec; (void)a0; (void)a1;
    return rg_h_flat(out, 96, 80, 45);
}

/* The Glass Workshop and the Route 114 house, 64 art rows: the slatted flat roof with its rail 0-37, the facade 37-64. */
bool rg_h_slat_house(const RgSpec *spec, int a0, int a1, RgPartList *out)
{
    (void)spec; (void)a0; (void)a1;
    return rg_h_flat(out, 64, 64, 37);
}

/* The Weather Institute, 112 art rows: the roof deck with its two domes 0-46, the pillared facade with the glazed ends
 * 46-112. */
bool rg_h_weather_institute(const RgSpec *spec, int a0, int a1, RgPartList *out)
{
    (void)spec; (void)a0; (void)a1;
    return rg_h_flat(out, 176, 112, 46);
}

/* The glass-doored gates: the tan patterned roof, the cornice, then the facade with its windows and the glass entrance. */
bool rg_h_entrance_gate(const RgSpec *spec, int width, int a1, RgPartList *out)
{
    (void)spec; (void)a1;
    return width == 80 ? rg_h_flat(out, 80, 96, 62) : rg_h_flat(out, 160, 80, 44);
}

const RgExact rg_h_league_gate_exact[2] = {
    {0, 8, 176, 96, false},
    {0, 0, 176, 8, false},
};
const RgExact rg_h_seashore_house_exact[2] = {
    {0, 37, 80, 64, false},
    {0, 0, 80, 37, false},
};
const RgExact rg_h_trick_house_exact[3] = {
    {0, 68, 80, 96, false},
    {16, 0, 64, 68, false},
    {0, 16, 80, 68, false},
};
const RgExact rg_h_cycling_gate_exact[2] = {
    {0, 37, 96, 80, false},
    {0, 0, 96, 37, false},
};
const RgExact rg_h_winstrate_house_exact[2] = {
    {0, 37, 80, 80, false},
    {0, 0, 80, 37, false},
};
const RgExact rg_h_cable_car_exact[2] = {
    {0, 45, 96, 80, false},
    {0, 0, 96, 45, false},
};
const RgExact rg_h_slat_house_exact[2] = {
    {0, 37, 64, 64, false},
    {0, 0, 64, 37, false},
};
const RgExact rg_h_weather_institute_exact[2] = {
    {0, 46, 176, 112, false},
    {0, 0, 176, 46, false},
};
const RgExact rg_h_entrance_gate_exact[2] = {
    {0, 62, 80, 96, false},
    {0, 0, 80, 62, false},
};
const RgExact rg_h_entrance_gate_wide_exact[2] = {
    {0, 44, 160, 80, false},
    {0, 0, 160, 44, false},
};

/* look L5 (rg_close_sides): the ends are dressed from the facade's rows; these patches are the fallback. */
const RgSideCfg rg_h_league_gate_side[1] = {
    {"block", {50, 18, 60, 28}, {50, 2, 60, 6}, 8, true},
};
const RgSideCfg rg_h_seashore_house_side[1] = {
    {"house", {20, 58, 30, 62}, {20, 20, 30, 30}, 37, true},
};
const RgSideCfg rg_h_trick_house_side[1] = {
    {"trick", {20, 74, 26, 90}, {20, 20, 26, 30}, 68, true},
};
const RgSideCfg rg_h_gate_side[1] = {
    {"block", {10, 56, 20, 64}, {20, 20, 30, 30}, 37, true},
};
const RgSideCfg rg_h_cable_car_side[1] = {
    {"block", {10, 56, 20, 64}, {20, 10, 30, 20}, 45, true},
};
const RgSideCfg rg_h_slat_house_side[1] = {
    {"block", {10, 44, 20, 52}, {20, 20, 30, 30}, 37, true},
};
const RgSideCfg rg_h_weather_institute_side[1] = {
    {"block", {60, 60, 70, 70}, {60, 20, 70, 30}, 46, true},
};
const RgSideCfg rg_h_entrance_gate_side[1] = {
    {"block", {10, 70, 20, 78}, {20, 20, 30, 30}, 62, true},
};
