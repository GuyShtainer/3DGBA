/* rg_hspecs_slateport.c -- Slateport City recipes (3DGBA original work, GPLv3). Phase 36 slice H1.
 *
 * Every number was read off `romgen author roms/emerald.gba art 2 ...`. Kanto-style profile prisms: every face the front
 * camera sees is a PROJ edge.
 *
 *   slateport_house      4x4 cells (64x64), rect (4, 16) and (20, 41): the purple-roofed houses
 *   slateport_house_w    6x4 cells (96x64), rect (24, 41): the same kit, wide and doorless, beside the second one
 *   slateport_house_g    5x5 cells (80x80), rect (2, 22): the green-roofed house (Ruby / Sapphire paint it yellow)
 *   slateport_fan_club   7x6 cells (112x96), rect (25, 7): the Pokemon Fan Club, red roof under the forest
 *   oceanic_museum       6x6 cells (96x96), rect (28, 22): the museum, a flat roof over a colonnade
 *   slateport_shipyard   8x7 cells (128x112), rect (24, 32): Stern's shipyard, a sheet-roofed shed by the water
 *   battle_tent_slateport 5x5 cells (80x80), rect (8, 8): the Battle Tent (Verdanturf's builder: the same tent) */
#include "rg_hspecs.h"

/* The purple tiled roof, 64 art rows: the light cap 0-10 and its board 10-14, the tiled slope 14-37, the facade 37-64. */
bool rg_h_slateport_house(const RgSpec *spec, int width, int a1, RgPartList *out)
{
    (void)spec; (void)a1;
    return rg_h_gable(out, width, 64, 37, 14);
}

/* The green house, 80 art rows: rows 0-8 are grass, the ribbed roof 8-40 and its eave bands 40-54, the facade 54-80. */
bool rg_h_slateport_house_g(const RgSpec *spec, int a0, int a1, RgPartList *out)
{
    (void)spec; (void)a0; (void)a1;
    return rg_h_gable_t(out, 80, 80, 54, 10, 8);
}

/* The Fan Club, 96 art rows: rows 0-2 are the forest's overhang, the red roof 2-58, its fascia and the facade 58-96. */
bool rg_h_fan_club(const RgSpec *spec, int a0, int a1, RgPartList *out)
{
    (void)spec; (void)a0; (void)a1;
    return rg_h_gable_t(out, 112, 96, 58, 4, 2);
}

/* The museum, 96 art rows: rows 0-8 are the plaza fence, the flat green roof 8-55, the colonnade 55-88; the plaza and
 * its steps 88-96 stay on the ground. */
bool rg_h_oceanic_museum(const RgSpec *spec, int a0, int a1, RgPartList *out)
{
    (void)spec; (void)a0; (void)a1;
    return rg_h_flat_t(out, 96, 88, 55, 8);
}

/* The shipyard, 112 art rows: row 0 is the sea, the skylit ridge band 1-13, the sheet slope 13-64 with its gutter
 * 64-70, the facade with the shutter door 70-112. */
bool rg_h_shipyard(const RgSpec *spec, int a0, int a1, RgPartList *out)
{
    (void)spec; (void)a0; (void)a1;
    return rg_h_gable_t(out, 128, 112, 70, 13, 1);
}

const RgExact rg_h_slateport_house_exact[2] = {
    {0, 37, 64, 64, false},
    {0, 0, 64, 37, false},
};
const RgExact rg_h_slateport_house_w_exact[2] = {
    {0, 37, 96, 64, false},
    {0, 0, 96, 37, false},
};
const RgExact rg_h_slateport_house_g_exact[3] = {
    {0, 54, 80, 80, false},
    {0, 8, 80, 54, false},
    {0, 0, 80, 8, true},        /* grass: the rear slope may show through */
};
const RgExact rg_h_fan_club_exact[2] = {
    {0, 58, 112, 96, false},
    {0, 2, 112, 58, false},
};
/* The roof's rim (rows 8-10) and its rounded corners have gaps of the fence's colour: the closure may show through. */
const RgExact rg_h_oceanic_museum_exact[6] = {
    {0, 55, 96, 88, false}, {0, 16, 96, 55, false}, {4, 10, 92, 16, false},
    {0, 8, 96, 10, true}, {0, 10, 4, 16, true}, {92, 10, 96, 16, true},
};
const RgExact rg_h_shipyard_exact[2] = {
    {0, 70, 128, 112, false},
    {0, 1, 128, 70, false},
};

/* look L5 (rg_close_sides): the ends are dressed from the facade's rows; these patches are the fallback. */
const RgSideCfg rg_h_slateport_house_side[1] = {
    {"house", {20, 58, 30, 62}, {20, 20, 30, 30}, 27, true},
};
const RgSideCfg rg_h_slateport_house_g_side[1] = {
    {"house", {20, 70, 30, 76}, {20, 20, 30, 30}, 26, true},
};
const RgSideCfg rg_h_fan_club_side[1] = {
    {"house", {20, 84, 30, 90}, {20, 20, 30, 30}, 38, true},
};
const RgSideCfg rg_h_oceanic_museum_side[1] = {
    {"block", {40, 70, 50, 76}, {40, 20, 50, 30}, 33, true},
};
const RgSideCfg rg_h_shipyard_side[1] = {
    {"house", {10, 80, 20, 86}, {20, 30, 30, 40}, 42, true},
};
