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
	// PHASE 18 / SPEC-door T4.6: SaveBlock1.location, already read every frame by gamestate.c —
	// no new addresses. A route must DIE the instant the map changes, which is exactly what
	// happens when the warp it just triggered fires; the old `ptr/w/h` check cannot see that
	// (gBackupMapLayout.map is a FIXED EWRAM buffer, so the pointer never changes across a warp)
	// and a surviving route keeps driving the player around inside the building it just entered.
	int      mapGroup, mapNum;   // -1 if the save block is not ready
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
	// phase 22.1: GCTX_LIST resolution (gamestate.h LK_*). listBase 0 with a positive ctx =>
	// the driver emits NOTHING (SPEC-family-lists L10 — never fall through to walk keys).
	uint32_t listBase;
	uint8_t  listKind;
	// --- instrumentation passthrough (LOGGING ONLY; never gates touch) — the screen fingerprint the touch
	// log records so NOT-YET-DETECTED contexts (map/PokeNav/Pokemon PC/move-learn/intro/Battle Frontier)
	// that fall through to ctx=overworld/none are identifiable by cb2 + active-task pointers. ---
	uint32_t cb2;             // raw gMain.callback2 (Thumb stripped) — the undetected-screen fingerprint
	bool     ctxResolved;     // ctx from a POSITIVE menu/battle match (true) vs the bare overworld fall-through (false)
	uint8_t  nTask;           // count of active-task func ptrs in taskFp[]
	uint32_t taskFp[8];       // active gTasks func pointers (Thumb stripped, sorted) — disambiguate cb2-ambiguous screens
	// --- phase 22.2 TRAVERSAL (SPEC-family-traversal). Appended; every existing filler leaves
	// them 0, which is the inert value for all three. ---
	bool     textDlg;         // GameState.textDlg — a field textbox is up. The HM sequencer is
	                          //   CLOSED-LOOP on this (never frame-count-blind): "the script is
	                          //   talking" is what proves the A press landed on the tree.
	uint16_t padKeys;         // the PHYSICAL GBA keys held this frame (to_gba_keys(kHeld)). The
	                          //   injection seam is ADDITIVE (COVERAGE §5) — we cannot suppress
	                          //   the user, so a running route YIELDS to them: any physical key
	                          //   edge cancels the program (SPEC H0.2). 0 = nothing held.
	int      traverse;        // g_prefs.smartTraverse (0 Off / 1 HM / 2 HM+Via), passed rather
	                          //   than read so touch.c stays free of the settings module.
} TouchSmart;

// --- PHASE 18 / SPEC-door T4.11: the gdb-readable route mirror (LOGGING ONLY) ------------------
// A screenshot cannot prove a warp fired: a door animation without a warp looks identical for
// several frames. The objective instrument is this struct, read over the emutest gdb channel
// (`tools/emutest/run gdbio read-u32 g_fieldDbg+N`, `gdbio poll g_fieldDbg --changed`,
// `see rec --with-state g_fieldDbg`) alongside the game's own SaveBlock1.location. `planSeq`
// increments once per planning ATTEMPT and `endSeq` once per route end, so a poll can latch on
// either. All int32_t so every field is one aligned gdb word at a known offset.
enum { FDBG_END_NONE = 0, FDBG_END_ARRIVED, FDBG_END_MOVED, FDBG_END_TIMEOUT,
       FDBG_END_STALLED, FDBG_END_REPLANNED, FDBG_END_MAPCHANGE };
typedef struct {
	int32_t planSeq;        // +0x00  bumped on every planning attempt
	int32_t px, py;         // +0x04  player tile the plan started from
	int32_t mapGroup, mapNum;   // +0x0C  SaveBlock1.location at plan time
	int32_t goalX, goalY;   // +0x14  the warp tile (after any door-graphic head retarget)
	int32_t approachX, approachY;   // +0x1C  the tile the path actually ends on
	int32_t kind;           // +0x24  FpKind: 0 none / 1 door / 2 dir / 3 step
	int32_t termDir;        // +0x28  0 R / 1 L / 2 D / 3 U, -1 none
	int32_t pathLen;        // +0x2C
	int32_t pElev;          // +0x30  player elevation used (0 = the rule was disarmed)
	int32_t behaviour;      // +0x34  raw metatile behaviour, -1 if unreadable
	int32_t outcome;        // +0x38  FpOutcome
	int32_t warpGroup, warpNum;   // +0x3C  the confirming warp event's destination, -1 if none
	int32_t headRetarget;   // +0x44  1 = the tap hit the wall above a door and was moved onto it
	int32_t routeEnd;       // +0x48  FDBG_END_*
	int32_t endSeq;         // +0x4C  bumped on every route end
	// --- the LIVE mirror, restamped every overworld frame (everything above is latched at plan
	// time). THIS is what makes a warp provable from outside the emulated console: the game's own
	// SaveBlock1.location, readable over gdb at any moment. `frame` advances with the emulated
	// core, so a reader can also tell a frozen game from a running one.
	int32_t curMapGroup, curMapNum;   // +0x50
	int32_t curPx, curPy;             // +0x58
	int32_t curKeys;                  // +0x60  the key mask the router injected this frame
	int32_t curFrame;                 // +0x64  gbacore_frame_counter of the bottom game
	int32_t walking;                  // +0x68  1 = a route is being followed, 2 = terminal hold
	// --- phase 22.2 TRAVERSAL (SPEC-family-traversal T2.4 "everything logs"). APPENDED, so every
	// offset above is unchanged and the phase-18 harness scripts keep working verbatim. A route
	// PROGRAM is multi-step, so the instrument needs the step cursor as well as the outcome:
	// `progSeq` latches a poll on program STARTS, `progStep`/`progPhase` say where it got to, and
	// `progEnd` says why it stopped. Reading progSeq/progStep/progHm over gdb is the whole P1-P3
	// proof spine — a screenshot cannot distinguish "the game surfed" from "the game refused". ---
	int32_t progSeq;        // +0x6C  bumped once per PROGRAM start (a planned conditional route)
	int32_t progOutcome;    // +0x70  FtOutcome of the last fieldtrav_plan attempt
	int32_t progMoves;      // +0x74  total moves in the program
	int32_t progInteracts;  // +0x78  HM activations the program costs
	int32_t progStep;       // +0x7C  index of the move being executed
	int32_t progHm;         // +0x80  FtHm of the move being executed (0 = plain walk)
	int32_t progPhase;      // +0x84  TPH_* interaction phase
	int32_t progEnd;        // +0x88  TPE_* end reason (0 = still running)
	int32_t progEndSeq;     // +0x8C  bumped once per program end
	int32_t progUsable;     // +0x90  the eligibility mask (1<<FtHm) the plan was built against
	int32_t progEdges;      // +0x94  conditional edge objects found on the map
	int32_t progSurf;       // +0x98  live PLAYER_AVATAR_FLAG_SURFING bit (1 = afloat)
	int32_t progMapSeq;     // +0x9C  slice 2: warp legs completed in the current excursion
} FieldDbg;
extern FieldDbg g_fieldDbg;

// --- PHASE 22.1: the keyboard/lists gdb mirror (LOGGING ONLY) ----------------------------------
// Same instrument pattern as g_fieldDbg: a screenshot cannot prove "the tap typed the char" — the
// objective proof is the game's OWN naming textBuffer / ListMenu selectedRow, read over the
// emutest gdb channel (`run gdbio read g_touchDbg+0x20 16` etc.). Restamped every SMART frame
// from the SAME bus reads the handlers use; nothing reads it back. Non-static ON PURPOSE: the
// harness resolves it by name out of 3DGBA.elf. All fields fixed-width at documented offsets.
typedef struct {
	uint32_t seq;            // +0x00  bumped every SMART touch_update
	int32_t  ctx;            // +0x04  GameCtx (this frame)
	int32_t  listKind;       // +0x08  LK_* when ctx == GCTX_LIST (else 0)
	// naming-screen mirror (GCTX_NAMING frames; zeroed otherwise)
	uint32_t nsPtr;          // +0x0C  sNamingScreen deref (the live struct NamingScreenData*)
	int32_t  nsState;        // +0x10  +0x1E10 (2 = STATE_HANDLE_INPUT — the only actionable state)
	int32_t  nsPage;         // +0x14  +0x1E22 currentPage (0 SYMBOLS / 1 UPPER / 2 LOWER)
	int32_t  nsCursorId;     // +0x18  +0x1E23 cursorSpriteId
	int32_t  nsGate;         // +0x1C  0 = all R9 gates pass; else the FIRST failing gate 1..5
	uint8_t  nsText[16];     // +0x20  textBuffer copy — THE P1 proof channel (charmap bytes)
	// list mirror (GCTX_BAG + GCTX_LIST frames; zeroed otherwise)
	uint32_t listBase;       // +0x30  the live ListMenu struct (gTasks + 40*id + 8)
	int32_t  lTotal;         // +0x34  totalItems (+12)
	int32_t  lMaxShowed;     // +0x38  maxShowed (+14)
	int32_t  lWindowId;      // +0x3C  windowId (+16)
	int32_t  lX0, lY0, lW, lH;   // +0x40..+0x4C  window rect, px
	int32_t  lScroll;        // +0x50  scrollOffset (+24) — read-only, never written by touch
	int32_t  lRow;           // +0x54  selectedRow (+26) — the P-A before/after channel
	// EM dex mirror (LK_DEX frames) — the L21 slot-formula DERIVATION channel
	uint32_t dexPtr;         // +0x58  sPokedexView deref
	int32_t  dexCount;       // +0x5C  pokemonListCount (+0x60C)
	int32_t  dexSelected;    // +0x60  selectedPokemon (+0x60E)
	int32_t  dexInitVOff;    // +0x64  initialVOffset (+0x62B, u8)
	int32_t  dexListVOff;    // +0x68  listVOffset (+0x62E, s16)
	// P-D discovery probe (GCTX_FULLUI frames): first gTasks entry whose fn == ListMenuDummyTask
	uint32_t probeListBase;  // +0x6C  0 = no live ListMenu found on this screen
	int32_t  probeTotal;     // +0x70
	int32_t  probeMaxShowed; // +0x74
	int32_t  probeWindowId;  // +0x78
	uint32_t lastKeys;       // +0x7C  the mask touch_update returned this frame
	// storage GRID mirror (GCTX_STORAGE frames; zeroed otherwise) — SPEC-family-grid G8. THE
	// move-proof channel: a mon move shows as stOccupancy bit(src) clearing and bit(dst) setting,
	// with stHeld/stOrigPos tracking the hand in between — all read from the game's own statics.
	int32_t  stArea;         // +0x80  sCursorArea (0 box / 1 party / 2 title / 3 buttons)
	int32_t  stPos;          // +0x84  sCursorPosition
	int32_t  stHeld;         // +0x88  sIsMonBeingMoved
	int32_t  stOrigBox;      // +0x8C  sMovingMonOrigBoxId (14 = party)
	int32_t  stOrigPos;      // +0x90  sMovingMonOrigBoxPos
	int32_t  stBoxId;        // +0x94  gPokemonStoragePtr->currentBox
	int32_t  stBoxOption;    // +0x98  sCurrentBoxOption (0 W / 1 D / 2 MOVE / 3 ITEMS)
	int32_t  stInParty;      // +0x9C  sInPartyMenu (the party panel is up)
	int32_t  stMenuOpen;     // +0xA0  1 = a LIVE sMenu popup is up (the G6 delegation gate)
	int32_t  stTgtArea;      // +0xA4  the armed navigation target (-1 = idle)
	int32_t  stTgtPos;       // +0xA8
	uint32_t stOccupancy;    // +0xAC  30-bit hasSpecies mask of the current box (bit = slot)
} TouchDbg;
extern TouchDbg g_touchDbg;

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

// Draw the overlay for the mode on the bound bottom target. PAD draws the gamepad; SMART draws no
// BUTTONS (it points at the real game UI) but does draw its mode chip + the "≡ menu" affordance.
// `held` lights pressed PAD zones. Call it for PAD **and** SMART (SPEC-layout L6.1).
void touch_draw(TouchMode mode, u16 held, const TouchSmart* sm, C2D_TextBuf buf);

// PHASE 17 / SPEC-layout L6.2 (sweep D17). The raw smart-touch readout ("field p=9,4 key=-", cyan,
// at (4,224) on the footer baseline) was a development probe — its own comment said "TEMP debug:
// confirm RAM reads on device" — and it shipped. It is now behind this flag, following control.h's
// CTL_D5_ENABLE convention: 0 for a shipping build, 1 when a hardware run needs the ctx/px/key
// triple on screen. The SD-side logging (touch_log_sample) is untouched — that is the real
// diagnostic channel and it stays on unconditionally.
#ifndef TOUCH_DIAG_HUD
#define TOUCH_DIAG_HUD 0
#endif

// PHASE 17 / SPEC-layout L3.2. The "≡ menu" chip drawn bottom-right of the virtual gamepad was
// DECORATION: nothing hit-tested it, and pad_keys' A zone (`px > 252 && py > 150`, unbounded
// downwards) actually resolved a tap there to an A press. With the centred footer hint gone from
// Gamepad mode (it printed through the START key — REPORT D4) the chip is the only touch route
// back to the pause menu, so it has to BE one. This is that rect, shared by the draw and the hit
// test so the two cannot drift; returns 1 when (px,py) is inside it for `mode`.
int touch_menu_chip(TouchMode mode, int px, int py);
