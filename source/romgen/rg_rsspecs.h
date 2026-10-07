/* rg_rsspecs.h -- the Ruby / Sapphire building recipe table, and the per-game table selector (3DGBA, GPLv3). Pure C.
 * Phase 35 S0: docs/phase35-rs/PHASE.md. */
#ifndef RG_RSSPECS_H
#define RG_RSSPECS_H

#include "rg_bspecs.h"
#include "rg_gameprof.h"

/* Ruby / Sapphire reuse Emerald's exterior recipes (rg_specs) as they are: each one is still keyed by its layout id AND the
 * FNV-1a pin of that layout's blockdata, so a recipe builds only where the Ruby / Sapphire layout is byte-identical to the
 * Emerald one it was authored on; everywhere else rg_build_models skips it and the device draws its extruded-box fallback.
 * Two rows change: the interior recipes are left out (Ruby / Sapphire interiors stay 2D, `interiors3d` false), and a
 * components expander's secondary tileset is the same tileset at this cartridge's address. NULL with *n = 0 for any other
 * game. Not thread-safe on its first call (like rg_kspecs_table). */
const RgSpec *rg_rsspecs_table(const GameProfile *prof, unsigned *n);

/* The recipe table romgen builds for a profile: rg_specs on Emerald, rg_rsspecs_table on Ruby / Sapphire, rg_kspecs_table on
 * FireRed / LeafGreen. NULL with *n = 0 for an unknown game. */
const RgSpec *rg_game_specs(const GameProfile *prof, unsigned *n);

#endif
