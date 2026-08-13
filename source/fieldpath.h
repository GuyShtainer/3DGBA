// fieldpath.h — Gen-3 overworld warp classification + tile routing for SMART touch.
//
// PHASE 18 / SPEC-door. The router used to BFS to the tapped tile with one special case
// ("a blocked GOAL is allowed as the terminal") and then stop. That is wrong for every warp
// in the game:
//
//   * a DOOR (MB_ANIMATED_DOOR / MB_WARP_DOOR) only warps when you bump it from the SOUTH —
//     `TryDoorWarp` is gated `if (direction == DIR_NORTH)` in BOTH engines (pokeemerald
//     src/field_control_avatar.c TryDoorWarp; pokefirered same function). Arriving from the
//     side just walks into a wall, which is exactly the user's phase-17 hardware report.
//   * an ARROW warp (the mat you stand on to leave a building) and an FRLG DIRECTIONAL STAIR
//     warp need you to STAND ON the tile and HOLD a direction (`TryArrowWarp` /
//     `IsDirectionalStairWarpMetatileBehavior`). Routing onto it and stopping does nothing —
//     and `MB_SOUTH_ARROW_WARP` is the single most common warp tile in both games (512 in
//     Emerald, 263 in FireRed, measured on the user's own ROMs), i.e. "tap the exit to leave"
//     was 100 % broken in every building.
//   * a STEP warp (ladder / escalator / warp pad / cave door) fires on arrival by itself.
//
// So the ARRIVAL is a CONSTRAINT of the search, not an accident of the BFS tie-break, and the
// route needs a terminal HOLD. This module owns that decision. It also fixes walkability:
// `walkable()` read the collision bits only, and Gen-3 water is collision 0 — impassable
// purely by ELEVATION — so the old router happily plotted a course straight across a pond.
//
// CLAUDE.md rule #4: PURE C. No libctru/citro/mGBA headers — only <stdint.h>/<stdbool.h> — so
// test/host/test_fieldpath.c dual-compiles it on the PC against real ROM-derived fixtures.
// All bus access goes through the FpBus callback vtable the caller supplies.
#pragma once
#include <stdint.h>
#include <stdbool.h>

// Which engine's metatile-behaviour numbering applies. The 0x60-0x71 block is NOT shared:
// RSE 0x6C is MB_WATER_DOOR (a step warp) while FRLG 0x6C is MB_UP_RIGHT_STAIR_WARP (hold
// EAST) — a common table would be a bug, and test_fieldpath TEST 1 fails if one creeps in.
typedef enum { FP_ENG_RSE = 0, FP_ENG_FRLG = 1 } FpEngine;

// What the tapped tile is, and therefore how the route has to end.
typedef enum {
	FP_WK_NONE = 0,   // not a warp -> legacy behaviour (blocked goal allowed as the terminal)
	FP_WK_DOOR,       // north-bump door: walk to the tile SOUTH of it, then hold UP
	FP_WK_DIR,        // arrow / FRLG stair warp: stand ON it, then hold `termDir`
	FP_WK_STEP        // ladder / escalator / warp pad / hole: arriving is enough
} FpKind;

// Direction indices, deliberately the same order as touch.c's s_keyDir[] so the caller can
// index straight into it: 0=RIGHT 1=LEFT 2=DOWN 3=UP.
enum { FP_R = 0, FP_L = 1, FP_D = 2, FP_U = 3, FP_NODIR = -1 };

// Why a plan attempt ended the way it did (mirrored into g_fieldDbg + the touch log).
typedef enum {
	FP_OUT_PLANNED = 0,   // a route (possibly zero-step) exists
	FP_OUT_UNREACHABLE,   // BFS found no path to the terminal tile
	FP_OUT_NO_APPROACH,   // a DOOR whose south tile is not enterable -> deliberately do NOTHING
	FP_OUT_WINDOW,        // goal outside the +-FP_WHALF search window
	FP_OUT_BADMAP         // map pointer/dimensions not sane
} FpOutcome;

// The bus the classifier reads the game through (gbacore_read* on device, a fixture image in
// the host suite). Every read is bounds-agnostic: the module range-checks the POINTERS it
// walks and degrades to FP_WK_NONE rather than trusting a garbage fetch.
typedef struct {
	uint8_t  (*read8 )(void* ctx, uint32_t addr);
	uint16_t (*read16)(void* ctx, uint32_t addr);
	uint32_t (*read32)(void* ctx, uint32_t addr);
	void* ctx;
} FpBus;

// Everything about the live map the classifier needs. `mapHeader` is gMapHeader (0 = the
// profile has no verified address -> classification is skipped and the router keeps its old
// behaviour; never a guessed pointer). `mapObjects` is gObjectEvents (0 -> player elevation
// reads as ELEVATION_TRANSITION, which makes the elevation rule a no-op = legacy behaviour).
typedef struct {
	FpEngine engine;
	uint32_t mapHeader;
	uint32_t mapObjects;
	uint32_t gridPtr;        // gBackupMapLayout.map
	int      backupW, backupH;   // gBackupMapLayout width/height (map size + MAP_OFFSET_W/H)
} FpMap;

typedef struct {
	FpKind   kind;
	int      termDir;        // FP_R/L/D/U to hold on arrival, or FP_NODIR
	int      goalX, goalY;   // the warp tile itself (== the tap unless headRetarget moved it)
	int      approachX, approachY;   // the tile to actually WALK to (== goal unless DOOR)
	uint16_t block;          // raw gBackupMapLayout word of the effective goal tile
	int      behaviour;      // metatile behaviour, or -1 if it could not be read
	int      warpGroup, warpNum;     // destination of the confirming warp event, or -1
	bool     headRetarget;   // the tap hit the WALL directly above a door (the door graphic is
	                         // two metatiles tall) and was retargeted down onto the door itself
} FpClass;

#define FP_WBOX  65      // BFS window edge in tiles (+-FP_WHALF around the player)
#define FP_WHALF 32

typedef struct {
	bool     ok;             // a route exists (pathLen may legitimately be 0)
	FpKind   kind;
	int      outcome;        // FpOutcome
	int      termDir;        // hold this on arrival, or FP_NODIR
	int      goalX, goalY;   // the tile the user tapped (after any head retarget)
	int      approachX, approachY;   // the tile the path ends on
	int      pathLen;
	int8_t   path[FP_WBOX * FP_WBOX];
	int      pElev;          // player elevation actually used (0 = rule disarmed)
	int      behaviour;      // for the diagnostics row
	int      warpGroup, warpNum;
	bool     headRetarget;
} FpPlan;

// --- classification -------------------------------------------------------------------
// Behaviour -> kind, per engine. Pure table lookup, no bus access; exposed so the host suite
// can assert every row of both engines' tables (and so a shared table cannot creep back in).
FpKind fieldpath_kind_for_behaviour(FpEngine eng, int behaviour, int* termDir);

// Read the metatile behaviour of a map-local tile by walking
// gMapHeader -> MapLayout -> primary/secondary Tileset -> metatileAttributes.
// Returns -1 when anything is unavailable or out of range (never a plausible-looking wrong
// value): no mapHeader, tile outside the CURRENT map's own bounds (tiles from a connected map
// index the wrong tileset pair — pret has the same limitation), MAPGRID_UNDEFINED, a metatile
// id >= 1024, or a pointer that does not look like ROM.
int fieldpath_behaviour_at(const FpBus* bus, const FpMap* m, int x, int y);

// Full classification of the tapped tile, including the two-metatile-tall door-graphic
// retarget and the warp-event confirmation.
FpClass fieldpath_classify(const FpBus* bus, const FpMap* m, int gx, int gy);

// --- walkability + routing -------------------------------------------------------------
// pret's rule, both engines: collision bits clear AND elevations compatible
// (IsElevationMismatchAt: ELEVATION_TRANSITION=0 on either side, or ELEVATION_MULTI_LEVEL=15
// on the tile, or an exact match). `pElev` 0 disarms the elevation half.
bool fieldpath_enterable(const FpBus* bus, const FpMap* m, int x, int y, int pElev);

// gObjectEvents[0].currentElevation (+0x0B low nibble, identical offset in both engines).
// 0 when unavailable — which makes the elevation rule a no-op rather than a wrong answer.
int fieldpath_player_elev(const FpBus* bus, const FpMap* m);

// Plan a route from (sx,sy) to the tapped tile (gx,gy). `npc` holds npcN occupied tiles in
// gBackupMapLayout (+MAP_OFFSET) space, exactly as touch.c already collects them.
// Returns out->ok; out->outcome always says why.
bool fieldpath_plan(const FpBus* bus, const FpMap* m, int sx, int sy, int gx, int gy,
                    const short (*npc)[2], int npcN, FpPlan* out);

// --- the follow loop's stall rule (PHASE 18 FIX PASS, review finding 1) ------------------
// When the router has stalled (the avatar has not changed tile for N frames) the follow loop
// may re-plan — NPCs are sampled once at plan time, so the commonest cause of a mid-route
// stall is one that has since walked into the path. But an FP_WK_NONE route deliberately ends
// ON A BLOCKED TILE (goalOpen in fieldpath_plan: a sign, an NPC, a cuttable tree IS the
// terminal), so its LAST step can never complete and the stall there is the route ARRIVING,
// not an obstruction. Re-planning there always "succeeds" — the same blocked-goal-allowed BFS
// re-finds a one-step path from the tile we are already standing on — so the bounded replan
// tripled the latency of the commonest smart-touch action and held the avatar bumping the
// obstacle for the extra second before the A press the route exists for.
//
// Pure, so test/host/test_fieldpath.c grades the shipped rule instead of a restatement.
// `pathLen == 0` (the zero-step plan) is covered: pathPos 0 >= -1.
static inline int fieldpath_should_replan(FpKind kind, int pathPos, int pathLen) {
	return !(kind == FP_WK_NONE && pathPos >= pathLen - 1);
}
