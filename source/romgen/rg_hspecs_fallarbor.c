/* rg_hspecs_fallarbor.c -- Fallarbor Town recipes (3DGBA original work, GPLv3). Phase 36 slice H1.
 *
 * Every number was read off `romgen author roms/emerald.gba art 14 ...`. Kanto-style profile prisms: every face the front
 * camera sees is a PROJ edge.
 *
 *   fallarbor_house_n     4x4 cells (64x64), rect (0, 3): the orange plank-roofed house under the cliff
 *   fallarbor_house_s     4x4 cells (64x64), rect (5, 14): the same kit, with Professor Cozmo's antenna on the roof
 *   battle_tent_fallarbor 5x5 cells (80x80), rect (6, 3): the Battle Tent (Verdanturf's builder: the same tent) */
#include "rg_hspecs.h"

/* The plank roof, 64 art rows: the ribbed cap and its light board 0-12, the plank slope 12-39 (its dark eave line at
 * the foot), the olive facade 39-64. The south house's antenna is painted on its slope and cap. */
bool rg_h_fallarbor_house(const RgSpec *spec, int a0, int a1, RgPartList *out)
{
    (void)spec; (void)a0; (void)a1;
    return rg_h_gable(out, 64, 64, 39, 12);
}

/* Row 0 is the cap's notched rim (the dirt shows between the ribs): the rear slope may show through it. */
const RgExact rg_h_fallarbor_house_exact[3] = {
    {0, 39, 64, 64, false},     /* facade */
    {0, 1, 64, 39, false},      /* cap, slope */
    {0, 0, 64, 1, true},
};

/* look L5 (rg_close_sides): the ends are dressed from the facade's rows; these patches are the fallback. */
const RgSideCfg rg_h_fallarbor_house_side[1] = {
    {"house", {2, 44, 12, 52}, {20, 20, 30, 30}, 25, true},
};
