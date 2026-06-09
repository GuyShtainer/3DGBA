// touch.h — touchscreen control for the bottom game (v1.1).
//   OFF    — touchscreen opens the pause menu (no game input).
//   PAD    — translucent virtual gamepad: fixed D-pad/A/B/START/L/R zones (screen-space).
//   SMART  — the touchscreen acts as a POINTER on the REAL game UI (no overlay buttons):
//            * Battle action menu: tap the real FIGHT/BAG/POKEMON/RUN -> drives the game's own
//              action cursor there and presses A.
//            * Battle move menu: tap the real move -> drives the move cursor + A (empty slots ignored).
//            * Overworld: tap a tile -> walk there (axis path + stall-detection); tap yourself = A.
//            Driven by live RAM state (gamestate.c); operates in GBA-screen pixel space.
#pragma once
#include <3ds.h>
#include <citro2d.h>
#include "gamestate.h"   // GameCtx

typedef enum { TOUCH_OFF = 0, TOUCH_PAD = 1, TOUCH_SMART = 2 } TouchMode;
extern const char* const TOUCH_NAMES[3];   // "Off" / "Gamepad" / "Smart"

// Live state of the bottom game the SMART pointer reacts to (filled from gamestate.c + main).
typedef struct {
	bool     valid;
	GameCtx  ctx;
	int      actionCursor;   // 0..3 or -1 (debug only)
	int      moveCursor;     // 0..3 or -1 (debug only)
	bool     moveValid[4];
	int      px, py;         // player tile (for tap-to-walk), -1 if unknown
	GbaCore* core;           // bottom game's core, for deterministic menu-cursor writes + RAM reads
	uint32_t actionAddr;     // gActionSelectionCursor[0]
	uint32_t moveAddr;       // gMoveSelectionCursor[0]
	const GameProfile* prof; // all other addresses (party / target / map)
	int      partyCount;     // gPlayerPartyCount, -1 if N/A
	int      partyLayout;    // 0 single / 1 double / 2 multi, -1 if N/A
	int      battlersCount;  // -1 if N/A
	uint8_t  absentMask;     // gAbsentBattlerFlags
	uint8_t  battlerPos[4];  // gBattlerPositions[0..3]
	uint32_t bagListTaskBase; // live bag ListMenu task (+24 scroll, +26 row), 0 if N/A
} TouchSmart;

// Advance touch one frame; returns the GBA key mask to OR into the bottom game.
//   (sx,sy) = raw bottom-screen touch (320x240) — used by PAD's fixed zones.
//   (gx,gy) = that touch mapped to GBA pixels (0..239,0..159); gvalid=false if off-frame — used by SMART.
// Stateful (menu-cursor driver + tap-to-walk), so call EVERY gameplay frame when enabled.
u16  touch_update(TouchMode mode, bool touching, int sx, int sy, int gx, int gy, bool gvalid,
                  const TouchSmart* sm);

// Draw the overlay for the mode on the bound bottom target. PAD draws the gamepad; SMART draws
// nothing (it points at the real game UI). `held` lights pressed PAD zones.
void touch_draw(TouchMode mode, u16 held, const TouchSmart* sm);
