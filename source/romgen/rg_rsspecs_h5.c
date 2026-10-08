/* rg_rsspecs_h5.c -- Ruby / Sapphire recipes of Phase 36 slice H5 (3DGBA original work, GPLv3).
 *
 * The Pokemon League facade outside Ever Grande (layout 266, group 26 map 4) exists only on RS: the same map number is the
 * Battle Frontier on Emerald (slice H6), so no Emerald row has a counterpart. This row is added to the RS table as it
 * stands (rg_rsspecs.c kExtra). Numbers read off `romgen author roms/ruby.gba art 266 ...`; Ruby and Sapphire share the
 * layout byte for byte. */
#include "rg_hspecs.h"

/* The League facade, 11x9 cells (176x144): the map ends above it, so the top 8 rows stand for the roof; the wall is the glazed
 * wings, the red-brick panels and the central glass tower, down to the awning's pillars and the entrance (rows 8-144). */
static bool rs_league(const RgSpec *spec, int a0, int a1, RgPartList *out)
{
    (void)spec; (void)a0; (void)a1;
    return rg_h_flat(out, 176, 144, 8);
}

static const RgExact kLeagueExact[2] = {
    {0, 8, 176, 144, false},
    {0, 0, 176, 8, false},
};

/* look L5 (rg_close_sides): the ends are dressed from the facade's rows; these patches are the fallback. */
static const RgSideCfg kLeagueSide[1] = {
    {"block", {50, 18, 60, 28}, {50, 2, 60, 6}, 8, true},
};

const RgSpec rg_rsspecs_league[] = {
    {"rs_league", RG_SPEC_DIRECT, 266, 0x21EB0971u, {9, 0, 11, 9}, {0, 0}, {0x001}, 1, kLeagueExact, 2, rs_league, 0, 0,
     kLeagueSide},
};
const unsigned rg_rsspecs_league_count = sizeof(rg_rsspecs_league) / sizeof(rg_rsspecs_league[0]);
