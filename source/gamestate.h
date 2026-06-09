// gamestate.h — game-aware touch (v1.1): read live Gen-3 state from the running core so the
// touchscreen can act as a POINTER on the real game UI (touch the actual FIGHT/move on screen)
// and drive tap-to-walk. All RAM addresses verified vs pret's byte-matched sym maps; see
// docs/kb/gen3-ram-touch.md.
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "gbacore.h"

// What on-screen context the bottom game is in right now (drives how touch is interpreted).
typedef enum {
	GCTX_NONE = 0,        // no known profile / not determinable
	GCTX_OVERWORLD,       // walking the field
	GCTX_BATTLE_ACTION,   // FIGHT / BAG / POKEMON / RUN menu
	GCTX_BATTLE_MOVE,     // move-selection menu
	GCTX_BATTLE_OTHER     // some other battle screen (no direct-touch handler yet)
} GameCtx;

// Per-game RAM map. sb1ptr=gSaveBlock1Ptr (deref -> player x/y, the first two s16). battleFlags=
// gBattleTypeFlags (nonzero in battle). actionCursor/moveCursor = gActionSelectionCursor[0] /
// gMoveSelectionCursor[0] (u8, 0..3). battleMons=gBattleMons[0] (moves[] at +0x0C, 4x u16).
// bg0y=gBattle_BG0_Y (=160 while the action menu shows, =320 while the move menu shows).
typedef struct {
	char     code[5];
	uint32_t sb1ptr;
	uint32_t battleFlags;
	uint32_t actionCursor;
	uint32_t moveCursor;
	uint32_t battleMons;
	uint32_t bg0y;
} GameProfile;

// One-pass snapshot of the live game.
typedef struct {
	bool    valid;          // a known profile matched the running ROM
	GameCtx ctx;
	int     px, py;         // player tile (-1 if the SaveBlock pointer isn't sane yet)
	int     actionCursor;   // 0..3 or -1
	int     moveCursor;     // 0..3 or -1
	bool    moveValid[4];   // which of the 4 move slots are non-empty (taps on empty slots ignored)
} GameState;

// Profile for a core's ROM (by header game code), or NULL if unknown.
const GameProfile* profile_for(GbaCore* c);

// Read the running game's live state into `out`. Returns false (out->valid=false) if no profile.
bool game_read(GbaCore* c, const GameProfile* p, GameState* out);
