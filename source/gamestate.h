// gamestate.h — game-aware touch (v1.1 Stage 2): read live Gen-3 state from the running core.
// Per-game RAM addresses are candidates to confirm on-device; once locked, smart touch regions
// (battle FIGHT/BAG/POKEMON/RUN, tap-to-walk) are built on these reads.
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "gbacore.h"

// sb1ptr = gSaveBlock1Ptr (its value points to the live SaveBlock1; player x/y are the first two
// s16). battleFlags = gBattleTypeFlags (nonzero while in a battle).
typedef struct { char code[5]; uint32_t sb1ptr; uint32_t battleFlags; } GameProfile;

// Profile for a core's ROM (by header game code), or NULL if unknown.
const GameProfile* profile_for(GbaCore* c);

// Fills px/py (player tile; -1 if the SaveBlock pointer isn't sane yet) + inBattle. false if no profile.
bool game_state(GbaCore* c, const GameProfile* p, int* px, int* py, bool* inBattle);
