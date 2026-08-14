// touch.h — touchscreen control for the bottom game (v1.1).
//   OFF    — touchscreen opens the pause menu (no game input).
//   PAD    — translucent virtual gamepad: fixed D-pad/A/B/START/L/R zones (screen-space).
//   SMART  — the touchscreen acts as a POINTER on the REAL game UI (no overlay buttons):
//            * Battle action menu: tap the real FIGHT/BAG/POKEMON/RUN -> drives the game's own
//              action cursor there and presses A.
//            * Battle move menu: tap the real move -> drives the move cursor + A (empty slots ignored).
//            * Overworld: touch/hold -> steer toward the touch (directional, any speed incl. bike);
//              tap yourself = START (the field menu), hold yourself = SELECT (the registered
//              item) — phase 24 lane A2, DECISIONS-overworld-gestures.md D1. Release to stop.
//              A routed leg of >= 4 tiles also holds B and RUNS (D2).
//            Driven by live RAM state (gamestate.c); operates in GBA-screen pixel space.
#pragma once
#include <3ds.h>
#include <citro2d.h>
#include "gamestate.h"   // GameCtx
#include "fieldtrav.h"   // phase 24: FtCensus (the party/PC mon mirror below)

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
	uint32_t cb2;             // raw gMain.callback2 (Thumb stripped) — the undetected-screen fingerprint.
	                          //   PHASE 23: no longer logging-only. FAM-DLG compares it against
	                          //   GameProfile.cb2Pager to decide whether the left/right EDGE ZONES
	                          //   are live on this screen (touchgeom.h). Compare-only, never
	                          //   dereferenced, and a 0/unknown cb2 simply means "not a pager".
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
	// --- phase 24 (lane B1). Appended; 0 = the pre-phase-24 behaviour, so an un-updated filler is
	// inert rather than wrong. ---
	bool     fieldLock;       // GameState.fieldLock — sLockFieldControls: a script owns the field
	                          //   for the WHOLE sequence (dialog, cutscene, forced walk). This is
	                          //   the signal that routes a field dialog to FAM-DLG; textDlg above
	                          //   only covers the frames the text is still PRINTING.
	// --- phase 24 (lane B2), FAM-MAP. Appended; 0 = the wall-map (cursor-only) reading, which is
	// the SAFE half — a driver that never confirms can only move a cursor.
	bool     mapFly;          // GCTX_MAP: 1 = the FLY map, where an arrival A is a fly confirm;
	                          //   0 = the field/wall map, where A CLOSES the screen (touchgeom.h
	                          //   FAM-MAP rule M2), so the driver emits none there.
} TouchSmart;

// --- PHASE 18 / SPEC-door T4.11: the gdb-readable route mirror (LOGGING ONLY) ------------------
// A screenshot cannot prove a warp fired: a door animation without a warp looks identical for
// several frames. The objective instrument is this struct, read over the emutest gdb channel
// (`tools/emutest/run gdbio read-u32 g_fieldDbg+N`, `gdbio poll g_fieldDbg --changed`,
// `see rec --with-state g_fieldDbg`) alongside the game's own SaveBlock1.location. `planSeq`
// increments once per planning ATTEMPT and `endSeq` once per route end, so a poll can latch on
// either. All int32_t so every field is one aligned gdb word at a known offset.
enum { FDBG_END_NONE = 0, FDBG_END_ARRIVED, FDBG_END_MOVED, FDBG_END_TIMEOUT,
       FDBG_END_STALLED, FDBG_END_REPLANNED, FDBG_END_MAPCHANGE,
       // PHASE 24 / lane A2: a route killed by the player's OWN-TILE gesture (decision D1 — a tap
       // on yourself opens START, and opening START while the avatar is mid-route means stop).
       // APPENDED, so every value above keeps its number and A1's traces still read the same.
       FDBG_END_CANCELLED };
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
	int32_t progSurf;       // +0x98  live PLAYER_AVATAR_FLAG_SURFING bit (1 = afloat). PHASE 24:
	                        //   restamped on EVERY overworld frame (it used to be written only from
	                        //   inside a running program, so "did the mount land?" was unreadable
	                        //   the moment the program ended — which is exactly when it is asked).
	int32_t progMapSeq;     // +0x9C  slice 2: warp legs completed in the current excursion
	// --- phase 24 (lane A) — the ANSWER instrument. A screenshot of an open YES/NO cannot say
	// whether we pressed A and the game ignored it, or we never pressed at all; these two counters
	// separate those cases in one gdb read, and they are what proved the 5-frame arming window
	// (touch.c TPH_ANSWER). APPENDED, so every offset above is unchanged. ---
	int32_t progAKeys;      // +0xA0  frames on which the program injected A (cumulative, per boot)
	int32_t progAnswers;    // +0xA4  YES presses aimed at a predicted yes/no (cumulative)
	int32_t progFacing;     // +0xA8  the game's OWN gObjectEvents[0] facing (1 D / 2 U / 3 L / 4 R,
	                        //   -1 unreadable), restamped every overworld frame. THE reason a
	                        //   correctly-planned Cut can still do nothing: an A is aimed by the
	                        //   avatar's facing, and a fixed-length direction hold does not
	                        //   guarantee it (touch.c TPH_FACE).
	// --- PHASE 24 / lane A2 — the two BANKED GESTURE DECISIONS (DECISIONS-overworld-gestures.md).
	// APPENDED, so every offset above is unchanged and A1's harness scripts keep working verbatim.
	// D1 needs an app-side channel because the own-tile verbs write no game RAM of their own (the
	// GAME-side proof is what the injected key then does: ctx -> GCTX_FIELDMENU for START, the
	// registered item firing for SELECT). D2's three fields are the difference between "it didn't
	// run" and knowing WHICH gate said no. LOGGING ONLY — nothing reads them back. ---
	int32_t ownStarts;      // +0xAC  own-tile TAPS resolved to START (cumulative, per boot)
	int32_t ownSelects;     // +0xB0  own-tile HOLDS resolved to SELECT (cumulative)
	int32_t runLeg;         // +0xB4  1 = the leg being followed decided to RUN (latched at plan time)
	int32_t runElig;        // +0xB8  the RUNG_* eligibility mask, restamped on every live re-test
	                        //   (0x1F = all five gates open; any clear bit names the veto)
	int32_t runFrames;      // +0xBC  frames on which B was actually injected (cumulative)
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
	// --- phase 23 FAM-DLG (tap-advance) mirror. APPENDED, so every offset above is unchanged and
	// the phase-22 harness scripts keep working verbatim. This family writes NO game RAM, so unlike
	// every other family there is no "the cursor moved" channel to read — the proof that a tap
	// registered has to come from the app side, which is what these five counters are. They are
	// MONOTONIC per session (dlg_reset deliberately leaves them alone) so a proof arc reads them
	// once before and once after and the DELTA is the evidence; `dlgPager` says which rule the
	// screen under the finger is running, which is how a "why did my tap turn the page" is
	// diagnosed without a rebuild. LOGGING ONLY — nothing reads them back.
	int32_t  dlgTaps;        // +0xB0  clean taps that became an A pulse
	int32_t  dlgHolds;       // +0xB4  holds that became a level-triggered B
	int32_t  dlgPages;       // +0xB8  edge-zone taps that became LEFT/RIGHT (pager screens only)
	int32_t  dlgSteps;       // +0xBC  drag notches that became a D-pad edge
	int32_t  dlgPager;       // +0xC0  1 = the live cb2 is in this game's cb2Pager whitelist
	// --- PHASE 24 / lane B1: the FIELD-DIALOG routing channel. APPENDED (every offset above is
	// unchanged). The B3 fix hangs entirely on one game byte being right, and that byte had never
	// been read on this project — GameState.textDlg feeds the tilt G8 gate and the DoF band split
	// and NOTHING ever verified it live. So publish all three layers of the decision: the raw byte
	// the profile address points at, the boolean gamestate made of it, and the route the
	// dispatcher took. A tap that "did nothing" is then one gdb read from a diagnosis instead of a
	// theory. LOGGING ONLY.
	int32_t  msgMode;        // +0xC4  RAW gbacore_read8(prof->fieldMsgMode), -1 = no address for this game
	int32_t  textDlg;        // +0xC8  GameState.textDlg as the dispatcher saw it this frame
	int32_t  fieldLock;      // +0xCC  GameState.fieldLock (sLockFieldControls) — the real signal
	int32_t  dlgOwns;        // +0xD0  dlggeom_route verdict: 1 = FAM-DLG claimed an OVERWORLD frame
	// --- PHASE 24 / lane B2: the FAM-MAP (region map / tap-to-fly) channel. APPENDED, so every
	// offset above is unchanged. Same proof pattern as the storage grid: the counters say what the
	// DRIVER did and the four live reads say what the GAME did, so "the cursor did not move" and
	// "we never pressed" are one gdb read apart. `mapSecId` is the destination the game itself
	// resolved for the cell under the finger — the before/after channel TOUCH-PLAN 22.4 asks for
	// ("tap Littleroot, read the cursor mapsec before/after, confirm the warp via
	// SaveBlock1.location", which g_fieldDbg.curMapGroup/curMapNum already carries). LOGGING ONLY.
	int32_t  mapTaps;        // +0xD4  clean taps that armed a destination cell
	int32_t  mapSteps;       // +0xD8  single-frame cursor presses emitted (one per cell)
	int32_t  mapArrive;      // +0xDC  targets the live cursor actually reached
	int32_t  mapFlies;       // +0xE0  arrivals that became a FLY CONFIRM (A on a flyable mapsec)
	int32_t  mapHolds;       // +0xE4  holds that became a B (close the map)
	int32_t  mapCurX;        // +0xE8  live cursorPosX (+0x054), -1 = the struct read failed
	int32_t  mapCurY;        // +0xEC  live cursorPosY (+0x056)
	int32_t  mapSecId;       // +0xF0  live mapSecId (+0x000) — the game's own name for the cell
	int32_t  mapSecType;     // +0xF4  live mapSecType (+0x002): 2 = CITY_CANFLY, 4 = FRONTIER
	int32_t  mapTgtX;        // +0xF8  the armed target cell (-1 = idle)
	int32_t  mapTgtY;        // +0xFC
	int32_t  mapIsFly;       // +0x100 TouchSmart.mapFly as the dispatcher saw it (1 = fly map)
	// --- PHASE 25 / lane C1: the INERT class's proof channel. APPENDED (every offset above is
	// unchanged). `ctx == 15 (GCTX_INERT)` says touch went silent; this says WHICH rule did it:
	// a quest-log playback state (2 or 3) or, with qlState 0/1, the credits cb2 list. -1 = this
	// game has no quest log (Emerald / RS) or the profile was unreadable. LOGGING ONLY.
	int32_t  qlState;        // +0x104 raw GameProfile.questLog byte (gQuestLogState on FRLG)
} TouchDbg;
extern TouchDbg g_touchDbg;

// --- PHASE 24 / lane A: the party + PC MON mirror (LOGGING ONLY) -------------------------------
// Why it exists: the traversal family's whole eligibility model is "badge AND a party mon that
// knows the move", and when a proof arc fails the first question is always *which mon, and is it
// even in the party?* `progUsable` answers only the 5-bit summary, and no screenshot can answer
// "Lugia is in box 3 slot 7". This restamps `fieldtrav_census` — the SAME decrypt+checksum rail
// the eligibility gate uses, never a second parser — into one gdb-readable struct.
// Re-run on a THROTTLE (MONDBG_EVERY frames) so a withdraw/deposit shows up within a second or
// two; a full 420-slot PC sweep is ~5k bus reads, i.e. far under a frame even at that cadence.
// Nothing reads it back: it can never change a route.
typedef struct {
	int32_t  seq;         // +0x00  bumped once per census run (0 = never ran)
	int32_t  storage;     // +0x04  the resolved PokemonStorage base (0 = unmapped for this game)
	int32_t  partyBase;   // +0x08  GameProfile.partyBase, echoed so a bad address is visible
	int32_t  pad;         // +0x0C  keeps `c` 4-aligned and the offsets below stable
	FtCensus c;           // +0x10  partyCount/nParty/party[6] then boxLive/boxOk/nBox/box[24]
} MonDbg;
extern MonDbg g_monDbg;

// --- PHASE 24 / lane A3 (RS-P24): the FLAG-RAIL probe (LOGGING ONLY) --------------------------
// Why it exists: commit c2a58db gave Ruby/Sapphire their own flag numbering (flags[] at 0x1220,
// SYSTEM_FLAGS 0x800) after they had been reading EMERALD's (0x1270 / 0x860) — a read that landed
// inside `vars[]`. That fix is host-proven, but the host cannot prove it against a REAL Ruby
// cartridge, and no screenshot can: the badge case is invisible on screen except through the
// game's own Trainer Card, and a hand-rolled save parser proves nothing about the SHIPPED reader.
//
// So this restamps the SHIPPED `fieldtrav_flag_get` — the exact function `run_elig` and the HM
// eligibility gates call, with the exact `fieldtrav_cfg` rows and the exact `fieldtrav_variant`
// title map — TWICE per game: once through the row the title really selects, and once through the
// EMERALD row, which is byte-for-byte what the pre-fix code did on an AXVE/AXPE cart. The two
// masks side by side ARE the proof; `sb1`/`addr*` publish the arithmetic so it can be checked by
// hand. Nothing here is a re-implementation and nothing reads it back — it cannot change a route.
//
// The 8 badge ids are derived as BADGE01 + 0..7 from the SHIPPED row's own `badgeCut`, because
// pret defines FLAG_BADGE01_GET..FLAG_BADGE08_GET as SYSTEM_FLAGS + 0x07..0x0E, consecutive, in
// all three games. `rowConsec` grades that derivation against the four OTHER shipped badge ids in
// the same row (SMASH = +2, STRENGTH = +3, SURF = +4, WATERFALL = +7), so the probe can never
// quietly invent a numbering the table under test does not agree with.
typedef struct {
	int32_t seq;          // +0x00  bumped every stamp (0 = never ran)
	int32_t code;         // +0x04  the 4-char game code, byte 0 in the LOW byte ('A','X','V','E')
	int32_t variant;      // +0x08  FtVariant fieldtrav_variant() chose (0 EM / 1 FRLG / 2 RS)
	int32_t sb1;          // +0x0C  SaveBlock1 base as prog_sb1() resolved it (0 = no save loaded)
	int32_t flagsOffNew;  // +0x10  shipped cfg->flagsOff for `variant`
	int32_t flagsOffOld;  // +0x14  the Emerald row's flagsOff (what the defect used)
	int32_t badge01New;   // +0x18  shipped row's FLAG_BADGE01_GET id
	int32_t badge01Old;   // +0x1C  Emerald row's FLAG_BADGE01_GET id
	int32_t badgesNew;    // +0x20  bit i (0..7) = BADGE0(i+1) read through the SHIPPED row
	int32_t badgesOld;    // +0x24  the SAME eight badges read through the EMERALD row (pre-fix)
	int32_t shoesNew;     // +0x28  FLAG_SYS_B_DASH through the shipped row (run_elig's RUNG_SHOES)
	int32_t shoesOld;     // +0x2C  ...and through the Emerald row
	int32_t addrNew;      // +0x30  sb1 + flagsOffNew + (badge01New>>3) — the byte actually read
	int32_t addrOld;      // +0x34  sb1 + flagsOffOld + (badge01Old>>3)
	int32_t rowConsec;    // +0x38  1 = the shipped row's five badge ids are consecutive per pret
	int32_t runElig;      // +0x3C  run_elig()'s live five-gate mask, stamped here too so the
	                      //        Running-Shoes gate is readable without driving a route
} BadgeProbe;
extern BadgeProbe g_badgeProbe[2];   // [0] = seat A, [1] = seat B

// Stamp the probe for one seat. Call once per frame with the workers PARKED (main thread only —
// it reads emulated RAM through the same bus every other live read uses).
void badgeprobe_stamp(int seat, GbaCore* core, const GameProfile* p);

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
