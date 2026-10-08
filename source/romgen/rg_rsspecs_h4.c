/* rg_rsspecs_h4.c -- Ruby / Sapphire recipes of Phase 36 slice H4 (3DGBA original work, GPLv3).
 *
 * Mossdeep's Space Center is another building on RS: a green block two cell rows higher on the map (rect 60,6,9,8, door 64,13), with
 * no gantry or rocket above it. This row takes the Emerald row's place on RS (rg_rsspecs.c kReplace). Numbers read off
 * `romgen author roms/ruby.gba art 7 60 6 9 8`; Ruby and Sapphire share the layout byte for byte. */
#include "rg_hspecs.h"

/* The Space Center, 9x8 cells (144x128): the roof deck with its two dishes 0-32, the facade of window bands with the glazed
 * tower and the entrance 32-128. */
static bool rs_space(const RgSpec *spec, int a0, int a1, RgPartList *out)
{
    (void)spec; (void)a0; (void)a1;
    return rg_h_flat(out, 144, 128, 32);
}

static const RgExact kSpaceExact[2] = {
    {0, 32, 144, 128, false},   /* facade */
    {0, 0, 144, 32, false},     /* deck */
};

/* look L5 (rg_close_sides): the ends are dressed from the facade's rows; these patches are the fallback. */
static const RgSideCfg kSpaceSide[1] = {
    {"block", {10, 40, 20, 48}, {20, 10, 30, 20}, 32, true},
};

const RgSpec rg_rsspecs_mossdeep[] = {
    {"rs_mossdeep_space", RG_SPEC_DIRECT, 7, 0x9CB79195u, {60, 6, 9, 8}, {0, 0}, {0x001}, 1, kSpaceExact, 2, rs_space, 0, 0,
     kSpaceSide},
};
const unsigned rg_rsspecs_mossdeep_count = sizeof(rg_rsspecs_mossdeep) / sizeof(rg_rsspecs_mossdeep[0]);
