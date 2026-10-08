/* rg_hspecs_mossdeep.c -- Mossdeep City recipes (3DGBA original work, GPLv3). Phase 36 slice H4.
 *
 * Every number was read off `romgen author roms/emerald.gba art 7 ...`. Kanto-style profile prisms: every face the front
 * camera sees is a PROJ edge. See docs/phase36-hoenn/BUILDLOG-H4.md for the per-building notes.
 *
 *   mossdeep_house   4x4 cells (64x64), rect (17, 13): the red-roofed brick house (five placements in town)
 *   mossdeep_wide    5x5 cells (80x80), rect (35, 20): the wide house with the pillared roof
 *   mossdeep_space   9x8 cells (144x128), rect (60, 8): the Space Center, a flat deck over a glazed facade (Emerald; RS has
 *                    its own building, rg_rsspecs_h4.c) */
#include "rg_hspecs.h"

/* The brick house, 64 art rows: the roof rail 0-14, the red slope 14-38, the facade 38-64. */
bool rg_h_mossdeep_house(const RgSpec *spec, int width, int a1, RgPartList *out)
{
    (void)spec; (void)a1;
    return rg_h_gable(out, width, 64, 38, 14);
}

/* The Space Center, 128 art rows: the flat deck with its domes and dish 0-46, the glazed, pylon-studded facade 46-128. */
bool rg_h_mossdeep_space(const RgSpec *spec, int a0, int a1, RgPartList *out)
{
    (void)spec; (void)a0; (void)a1;
    return rg_h_flat(out, 144, 128, 46);
}

/* The wide house, 5x5 cells (80x80): the top 8 rows are ground, the pillared roof 8-51 (rail 8-22, slope 22-51), the
 * facade with the door and the two arched windows 51-80. */
bool rg_h_mossdeep_wide(const RgSpec *spec, int a0, int a1, RgPartList *out)
{
    (void)spec; (void)a0; (void)a1;
    return rg_h_gable_t(out, 80, 80, 51, 22, 8);
}
const RgExact rg_h_mossdeep_wide_exact[3] = {
    {0, 51, 80, 80, false},
    {0, 8, 80, 51, false},
    {0, 0, 80, 8, true},
};
const RgSideCfg rg_h_mossdeep_wide_side[1] = {
    {"house", {20, 58, 30, 62}, {20, 20, 30, 30}, 51, true},
};

const RgExact rg_h_mossdeep_house_exact[2] = {
    {0, 38, 64, 64, false},
    {0, 0, 64, 38, false},
};
const RgExact rg_h_mossdeep_space_exact[5] = {
    {0, 46, 144, 128, false},
    {0, 8, 144, 46, false},
    {8, 0, 136, 8, false},
    {0, 0, 8, 8, true}, {136, 0, 144, 8, true},
};

/* look L5 (rg_close_sides): the ends are dressed from the facade's rows; these patches are the fallback. */
const RgSideCfg rg_h_mossdeep_house_side[1] = {
    {"house", {20, 58, 30, 62}, {20, 20, 30, 30}, 30, true},
};
const RgSideCfg rg_h_mossdeep_space_side[1] = {
    {"block", {10, 66, 20, 74}, {20, 20, 30, 30}, 46, true},
};
