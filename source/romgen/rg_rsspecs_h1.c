/* rg_rsspecs_h1.c -- Ruby / Sapphire recipes of Phase 36 slice H1 (3DGBA original work, GPLv3).
 *
 * Where Emerald puts a Battle Tent, Ruby and Sapphire put a Contest Hall: other cells, other art. These rows take the
 * Emerald tent rows' place on RS (rg_rsspecs.c kReplace). Numbers read off `romgen author roms/ruby.gba art ...`; Ruby
 * and Sapphire share these layouts byte for byte (docs/phase36-hoenn/PHASE.md). */
#include "rg_hspecs.h"

/* The Contest Hall, 5x5 cells (80x80): the grey skylight roof 0-40 and its bevelled rim 40-46 (one flat top), the
 * fascia band and the red facade with the doors 46-80. */
static bool rs_contest_hall(const RgSpec *spec, int a0, int a1, RgPartList *out)
{
    (void)spec; (void)a0; (void)a1;
    return rg_h_flat(out, 80, 80, 46);
}

static const RgExact kContestExact[2] = {
    {0, 46, 80, 80, false},     /* fascia, facade */
    {0, 0, 80, 46, false},      /* roof (the cliff's foot shows in its top corners) */
};

/* look L5 (rg_close_sides): the ends are dressed from the facade's rows; these patches are the fallback. */
static const RgSideCfg kContestSide[1] = {
    {"block", {10, 66, 20, 72}, {20, 10, 30, 20}, 34, true},
};

const RgSpec rg_rsspecs_verdanturf[] = {
    {"rs_contest_verdanturf", RG_SPEC_DIRECT, 15, 0x92F55851u, {2, 3, 5, 5}, {0, 0}, {0x001, 0x204, 0x205}, 3,
     kContestExact, 2, rs_contest_hall, 0, 0, kContestSide},
};
const unsigned rg_rsspecs_verdanturf_count = sizeof(rg_rsspecs_verdanturf) / sizeof(rg_rsspecs_verdanturf[0]);
