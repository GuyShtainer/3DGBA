// touch.h — touchscreen control for the bottom game (v1.1).
//   OFF    — touchscreen opens the pause menu (no game input).
//   PAD    — translucent virtual gamepad: fixed D-pad/A/B/START/L/R zones (screen-space).
//   SMART  — the touchscreen acts as a POINTER on the REAL game UI (no overlay buttons):
//            * Battle action menu: tap the real FIGHT/BAG/POKEMON/RUN -> drives the game's own
//              action cursor there and presses A.
//            * Battle move menu: tap the real move -> drives the move cursor + A (empty slots ignored).
//            * Overworld: touch/hold -> steer toward the touch (directional, any speed incl. bike);
//              tap yourself = A (interact/advance). Release to stop.
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
	// --- instrumentation passthrough (LOGGING ONLY; never gates touch) — the screen fingerprint the touch
	// log records so NOT-YET-DETECTED contexts (map/PokeNav/Pokemon PC/move-learn/intro/Battle Frontier)
	// that fall through to ctx=overworld/none are identifiable by cb2 + active-task pointers. ---
	uint32_t cb2;             // raw gMain.callback2 (Thumb stripped) — the undetected-screen fingerprint
	bool     ctxResolved;     // ctx from a POSITIVE menu/battle match (true) vs the bare overworld fall-through (false)
	uint8_t  nTask;           // count of active-task func ptrs in taskFp[]
	uint32_t taskFp[8];       // active gTasks func pointers (Thumb stripped, sorted) — disambiguate cb2-ambiguous screens
} TouchSmart;

// --- Touch-event instrumentation logger (LOGGING ONLY — never changes touch/gameplay) ---------------
// Self-contained ring in touch.c: touch_update records a row on each touch EVENT (a new press; plus
// drags/holds where they matter — bag + overworld/PC). Each row captures the ctx fingerprint and the
// context-relevant game cursor read BEFORE and AFTER the handler (the "does the cursor update" proof),
// plus the injected key mask. Flushed to ONE SD file on session close (the gs_log one-shot pattern).
void touch_log_reset(void);              // clear the ring (call once when a session starts)
void touch_log_dump(const char* path);   // flush the ring to an SD file (mkdir's sdmc:/cias/netlogs)

// Advance touch one frame; returns the GBA key mask to OR into the bottom game.
//   (sx,sy) = raw bottom-screen touch (320x240) — used by PAD's fixed zones.
//   (gx,gy) = that touch mapped to GBA pixels (0..239,0..159); gvalid=false if off-frame — used by SMART.
// Stateful (menu-cursor driver + tap-to-walk), so call EVERY gameplay frame when enabled.
u16  touch_update(TouchMode mode, bool touching, int sx, int sy, int gx, int gy, bool gvalid,
                  const TouchSmart* sm);

// Draw the overlay for the mode on the bound bottom target. PAD draws the gamepad; SMART draws
// nothing (it points at the real game UI). `held` lights pressed PAD zones.
void touch_draw(TouchMode mode, u16 held, const TouchSmart* sm, C2D_TextBuf buf);
