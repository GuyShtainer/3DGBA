/* rg_kspecs.h -- the Kanto (FireRed / LeafGreen) building recipe table and its aggregator (3DGBA original work, GPLv3).
 * Pure C. Phase 34 B0: the table is empty; the K slices add rg_kspecs_<town>.c files and list them in rg_kspecs.c.
 * Spec: docs/phase34-frlg/SPEC.md section 5.3. */
#ifndef RG_KSPECS_H
#define RG_KSPECS_H

#include "rg_bspecs.h"
#include "rg_gameprof.h"

/* The recipe table of a profile's game: the Kanto table for FireRed and LeafGreen (FR and LG share it, their blockdata is
 * identical), NULL with *n = 0 for any other game. The table is the town files concatenated in the fixed story order of
 * rg_kspecs.c, so models and pages are emitted deterministically (SPEC principle 6). Never NULL for FRLG, even empty. */
const RgSpec *rg_kspecs_table(const GameProfile *prof, unsigned *n);

#endif
