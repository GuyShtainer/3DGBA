/* rg_rsspecs.c -- the Ruby / Sapphire building recipe table (3DGBA, GPLv3). Pure C. See rg_rsspecs.h.
 *
 * The rows are Emerald's (rg_bspecs.c, whose Zallax-derived builders keep their MIT notice there), copied, with the interior
 * rows left out and the components expanders pointed at this cartridge's tileset. Phase 35 S4 moves the pins of the rows
 * whose buildings are unchanged on RS (kRetarget) and puts RS-own recipes in the place of the rows RS draws differently
 * (kReplace). */
#include "rg_rsspecs.h"

#include <string.h>

#include "rg_kspecs.h"

#define RG_RSSPECS_MAX 96u          /* Emerald's table has 46 rows after Phase 36 H1 */
#define RG_RS_COMP_MAX 8u

/* The secondary tilesets Emerald's components expanders name, at their Ruby and Sapphire addresses: Petalburg (layout 1's
 * secondary) and Rustboro (layout 4's). ROM-measured (docs/phase35-rs/PHASE.md, recon): same tileset by layout, and the same
 * metatile count (144, 350). Sapphire's tileset structs sit 0x70 below Ruby's. */
static const struct { uint32_t emerald, ruby, sapphire; } kTsMap[] = {
    {0x083DF71Cu, 0x08286D24u, 0x08286CB4u},   /* Petalburg */
    {0x083DF734u, 0x08286D3Cu, 0x08286CCCu},   /* Rustboro */
};

/* Phase 35 S4: Emerald rows whose layout is not byte-identical on Ruby / Sapphire, but whose building cells are
 * (ROM-measured: `romgen author ROM art` cell dumps of both carts). Oldale differs from Emerald's in one tree cell (18,3);
 * Rustboro in 24 cells, none inside a recipe's rect (Devon's entrance posts stand one row higher, two trees and a few path
 * cells). The row keeps its builder, rect and art gates; only the pin moves to this cartridge's layout. */
static const struct { const char *name; uint16_t layoutId; uint32_t fnv; } kRetarget[] = {
    {"oldale_house", 11, 0x37D810BEu},
    {"rustboro_stone", 4, 0x5FF68C82u},
    {"rustboro_olive", 4, 0x5FF68C82u},
    {"gym_rustboro", 4, 0x5FF68C82u},
    {"devon_corporation", 4, 0x5FF68C82u},
    {"rustboro_fountain", 4, 0x5FF68C82u},
    /* Phase 36 H1: Verdanturf's houses (the Contest Hall is RS-own, below) */
    {"verdanturf_house", 15, 0x92F55851u},
    {"verdanturf_house_w", 15, 0x92F55851u},
};

/* Phase 35 S4: RS-own recipes (rg_rsspecs_<town>.c, the Kanto files' pattern) in the place of an Emerald row whose art gate
 * fails on this cartridge. */
extern const RgSpec rg_rsspecs_littleroot[];
extern const unsigned rg_rsspecs_littleroot_count;
extern const RgSpec rg_rsspecs_verdanturf[];       /* rg_rsspecs_h1.c, Phase 36 H1 */
extern const unsigned rg_rsspecs_verdanturf_count;

static const struct { const char *name; const RgSpec *rows; const unsigned *count; } kReplace[] = {
    {"littleroot_lab", rg_rsspecs_littleroot, &rg_rsspecs_littleroot_count},   /* RS draws the lab roof differently */
    {"battle_tent_verdanturf", rg_rsspecs_verdanturf, &rg_rsspecs_verdanturf_count},  /* RS: the Contest Hall */
};

static RgSpec sTable[2][RG_RSSPECS_MAX];
static RgComponentsCfg sComp[2][RG_RS_COMP_MAX];
static unsigned sCount[2];
static bool sBuilt[2];

static uint32_t map_ts(uint32_t emerald, bool sapphire)
{
    unsigned i;

    for (i = 0; i < sizeof kTsMap / sizeof kTsMap[0]; i++)
        if (kTsMap[i].emerald == emerald)
            return sapphire ? kTsMap[i].sapphire : kTsMap[i].ruby;
    return 0;   /* an unknown tileset: the expander then finds no layout and builds nothing */
}

static void build(unsigned g, bool sapphire)
{
    unsigned i, nComp = 0;

    sCount[g] = 0;
    for (i = 0; i < rg_spec_count && sCount[g] < RG_RSSPECS_MAX; i++) {
        RgSpec s = rg_specs[i];
        unsigned k, j;

        if (s.kind == RG_SPEC_INTERIOR)
            continue;
        for (k = 0; k < sizeof kReplace / sizeof kReplace[0]; k++)
            if (strcmp(s.name, kReplace[k].name) == 0)
                break;
        if (k < sizeof kReplace / sizeof kReplace[0]) {   /* the RS rows, in the Emerald row's place */
            for (j = 0; j < *kReplace[k].count && sCount[g] < RG_RSSPECS_MAX; j++)
                sTable[g][sCount[g]++] = kReplace[k].rows[j];
            continue;
        }
        for (k = 0; k < sizeof kRetarget / sizeof kRetarget[0]; k++)
            if (strcmp(s.name, kRetarget[k].name) == 0) {
                s.layoutId = kRetarget[k].layoutId;
                s.layoutFnv = kRetarget[k].fnv;
            }
        if (s.kind == RG_SPEC_COMPONENTS) {
            if (s.ext == NULL || nComp >= RG_RS_COMP_MAX)
                continue;
            sComp[g][nComp] = *(const RgComponentsCfg *)s.ext;
            sComp[g][nComp].secondaryAddr = map_ts(sComp[g][nComp].secondaryAddr, sapphire);
            s.ext = &sComp[g][nComp++];
        }
        sTable[g][sCount[g]++] = s;
    }
    sBuilt[g] = true;
}

const RgSpec *rg_rsspecs_table(const GameProfile *prof, unsigned *n)
{
    unsigned g;

    if (n == NULL)
        return NULL;
    *n = 0;
    if (prof == NULL || !gp_is_rs(prof))
        return NULL;
    g = prof->game == GP_SAPPHIRE ? 1u : 0u;
    if (!sBuilt[g])
        build(g, g == 1u);
    *n = sCount[g];
    return sTable[g];
}

const RgSpec *rg_game_specs(const GameProfile *prof, unsigned *n)
{
    if (n == NULL)
        return NULL;
    *n = 0;
    if (prof == NULL)
        return NULL;
    if (prof->game == GP_EMERALD) {
        *n = rg_spec_count;
        return rg_specs;
    }
    if (gp_is_rs(prof))
        return rg_rsspecs_table(prof, n);
    return rg_kspecs_table(prof, n);
}
