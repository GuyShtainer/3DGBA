/* rg_rsspecs.c -- the Ruby / Sapphire building recipe table (3DGBA, GPLv3). Pure C. See rg_rsspecs.h.
 *
 * No recipe of its own: the rows are Emerald's (rg_bspecs.c, whose Zallax-derived builders keep their MIT notice there),
 * copied, with the interior rows left out and the components expanders pointed at this cartridge's tileset. */
#include "rg_rsspecs.h"

#include <string.h>

#include "rg_kspecs.h"

#define RG_RSSPECS_MAX 64u          /* Emerald's table has 34 rows */
#define RG_RS_COMP_MAX 8u

/* The secondary tilesets Emerald's components expanders name, at their Ruby and Sapphire addresses: Petalburg (layout 1's
 * secondary) and Rustboro (layout 4's). ROM-measured (docs/phase35-rs/PHASE.md, recon): same tileset by layout, and the same
 * metatile count (144, 350). Sapphire's tileset structs sit 0x70 below Ruby's. */
static const struct { uint32_t emerald, ruby, sapphire; } kTsMap[] = {
    {0x083DF71Cu, 0x08286D24u, 0x08286CB4u},   /* Petalburg */
    {0x083DF734u, 0x08286D3Cu, 0x08286CCCu},   /* Rustboro */
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

        if (s.kind == RG_SPEC_INTERIOR)
            continue;
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
