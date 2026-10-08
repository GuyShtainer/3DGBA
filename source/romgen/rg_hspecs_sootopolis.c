/* rg_hspecs_sootopolis.c -- Sootopolis City recipes (3DGBA original work, GPLv3). Phase 36 slice H4.
 *
 * Every number was read off `romgen author roms/emerald.gba art 8 ...`. The census seeds of the cliff dwellings are
 * clamped at 16 cells and are not the buildings: the rects below were found from the art. Kanto-style profile prisms: every
 * face the front camera sees is a PROJ edge. See docs/phase36-hoenn/BUILDLOG-H4.md.
 *
 *   sootopolis_tower   3x4 cells (48x64), rect (43, 14): the pointed dwelling (dark pyramid roof on a cream octagon), door
 *                      at the bottom middle; three in town (the two with a diamond window and no door are left as art)
 *   sootopolis_box     3x4 cells (48x64), rect (44, 3): the pale box dwelling with its door porch; six in town
 *   gym_sootopolis     6x5 cells, rect (28, 28): the Petalburg gym kit (rg_gym) on the islet in the lake */
#include "rg_hspecs.h"

/* The pointed dwelling, 64 art rows (its top cell row, the apex and a cliff piece either side, is left out: the cliff there
 * is opaque art that no roof covers): the pyramid roof 0-46 (its corners are ground: the prism is the bounding gable, the
 * corners are cut by the texture's alpha), the cream walls with the door 46-64. */
bool rg_h_sootopolis_tower(const RgSpec *spec, int a0, int a1, RgPartList *out)
{
    (void)spec; (void)a0; (void)a1;
    return rg_h_gable_t(out, 48, 64, 46, 16, 0);
}

/* The box dwelling, 64 art rows: the cream roof with its panels 0-36, the wall with the door porch 36-64. */
bool rg_h_sootopolis_box(const RgSpec *spec, int a0, int a1, RgPartList *out)
{
    (void)spec; (void)a0; (void)a1;
    return rg_h_flat(out, 48, 64, 36);
}

const RgExact rg_h_sootopolis_tower_exact[2] = {
    {0, 46, 48, 64, false},
    {0, 0, 48, 46, true},
};
const RgExact rg_h_sootopolis_box_exact[2] = {
    {0, 36, 48, 64, false},
    {0, 0, 48, 36, true},
};

/* look L5 (rg_close_sides): the ends are dressed from the facade's rows; these patches are the fallback. */
const RgSideCfg rg_h_sootopolis_tower_side[1] = {
    {"house", {12, 66, 20, 74}, {20, 20, 30, 30}, 62, true},
};
const RgSideCfg rg_h_sootopolis_box_side[1] = {
    {"block", {10, 40, 20, 56}, {20, 10, 30, 20}, 36, true},
};
