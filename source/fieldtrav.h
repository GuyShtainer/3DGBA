// fieldtrav.h — HM-aware traversal planning for SMART touch (phase 22.2, SPEC-family-traversal).
//
// THE ASK (user, verbatim): "if I touch a place in the overworld which requires a HM, such as a
// spot in a pond where i CAN go, or a place beyond some rocks or a tree, that the player auto
// does that as well (surfs, break brick, cut etc)."
//
// The phase-18 router (fieldpath.c) answers ONE question — "walk me there" — and its world is
// binary: a tile is enterable or it is not. Water is impassable by ELEVATION, a cut tree is an
// object event that lands in the NPC block list, a Rock-Smash rock likewise. So every HM-gated
// destination reads as FP_OUT_UNREACHABLE and the tap does nothing.
//
// This module adds the CONDITIONAL edge: a tile that is not enterable NOW but becomes enterable
// after a specific, predictable, in-game interaction the player is actually allowed to perform.
// It plans over a LAYERED state space (tile, mode) with mode in {FOOT, SURF}, charges each
// conditional edge one INTERACT, and emits a MOVE PROGRAM: a flat list of steps where a step is
// either "walk one tile" or "perform this HM here, then the tile is yours".
//
// TWO NON-NEGOTIABLE PROPERTIES (SPEC §2.2, §1.5):
//   1. DRY PATHS WIN. Tier 0 (today's plain BFS, zero conditional edges) runs FIRST and, if it
//      reaches the goal, SHIPS unchanged — bit-identical to the shipped router. A wet or
//      destructive detour may never displace a walkable route, however much longer the walk.
//   2. NEVER PROMPT WHAT THE GAME WILL REFUSE. Every conditional edge is gated on the game's OWN
//      eligibility test, mirrored exactly: the badge flag AND a party mon that really knows the
//      move (read through the Gen-3 party encryption, with the checksum rail). Ineligible => the
//      edge does not exist => the tap plans NOTHING and injects NOTHING. The executor in touch.c
//      never answers a yes/no prompt it did not predict.
//
// CLAUDE.md rule #4: PURE C. No libctru / citro / mGBA headers — only <stdint.h>/<stdbool.h> and
// fieldpath.h (itself pure). Every bus access goes through the FpBus vtable the caller supplies,
// so test/host/test_fieldtrav.c dual-compiles this on the PC against ROM-derived fixtures.
//
// EVERY CONSTANT BELOW WAS RE-READ FROM pret THIS SESSION (2026-08-14) at the line that uses it —
// never copied from the spec (house zero-guess rule). Sources: the local pokeemerald clone
// (gba-toolkit/projects/PokeDNA/daycare map/pokeemerald), the local partial pokefirered checkout
// (PokeDNA/reference/pokefirered), the cached pret extracts in /tmp/pret/{em,fr}, and
// raw.githubusercontent.com/pret/pokefirered/master for the two files neither checkout had.
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "fieldpath.h"

// The field moves this module can plan through. Ordered so FT_HM_NONE == 0 is "just walk".
// Waterfall is slice 2; Strength is activation-only and never a planned edge (SPEC §5: pushing
// boulders is a Sokoban problem and mis-planning one can soft-lock a puzzle).
typedef enum {
	FT_HM_NONE = 0,
	FT_HM_CUT,        // an OBJECT edge: the cuttable tree's object slot deactivates
	FT_HM_SMASH,      // an OBJECT edge: the breakable rock's object slot deactivates
	FT_HM_SURF,       // a METATILE edge: mount at the shore, the game steps you onto the water
	FT_HM_WATERFALL,  // slice 2 — mid-surf vertical edge
	FT_HM_STRENGTH,   // activation only (never a planned edge in v1)
	FT_HM_COUNT
} FtHm;

// Movement layer. The whole point of the layered search: water is elevation 1 and land is
// elevation 3, so ONE elevation cannot describe a route that crosses a pond and lands again.
enum { FT_MODE_FOOT = 0, FT_MODE_SURF = 1, FT_MODE_COUNT = 2 };

// Per-engine constants. RSE and FRLG genuinely disagree on all of them — the badge that gates Cut
// is BADGE01 in Emerald and BADGE02 in FireRed, the flags array sits at a different SaveBlock1
// offset, and the object graphics ids are 82/86/87 vs 95/96/97 — so, exactly as fieldpath's
// behaviour tables, these are two tables that must never merge.
typedef struct {
	uint16_t flagsOff;        // SaveBlock1.flags[] byte offset
	uint16_t badgeCut, badgeSmash, badgeSurf, badgeWaterfall, badgeStrength;
	uint16_t strengthLatch;   // FLAG_SYS_USE_STRENGTH (per-map-load latch; read, never assumed)
	uint8_t  gfxCutTree, gfxRock, gfxBoulder;
} FtEngCfg;

// Never NULL: an unknown engine returns the RSE table (fieldpath's own fallback convention).
const FtEngCfg* fieldtrav_cfg(FpEngine eng);

// Is this metatile behaviour a tile Surf can float on? Per engine, per the game's own table.
// Deliberately NOT the game's set verbatim: the four CURRENT behaviours (0x50-0x53) are surfable
// in both engines and are EXCLUDED here, because a current moves the player on its own and the
// router must never steer into a tile that takes the controls away (COVERAGE §3, rule T5.8).
// MB_WATERFALL 0x13 is likewise excluded from free surfing: it is a waterfall EDGE (slice 2), not
// a tile to drift onto.
bool fieldtrav_is_surfable(FpEngine eng, int behaviour);

// --- eligibility: the game's own gates, mirrored --------------------------------------------

// Where the player's badges and party live. `sb1` is the ALREADY-RESOLVED SaveBlock1 base (the
// caller derefs gSaveBlock1Ptr, or passes it straight through for Ruby/Sapphire — the sbDirect
// split gamestate.c already owns). Any field 0 = "not mapped for this game" and the corresponding
// eligibility answer is FALSE, never a guess: a game we cannot read badges for simply gets no
// conditional edges and keeps the shipped walk-only behaviour.
typedef struct {
	uint32_t sb1;
	uint32_t partyBase;    // gPlayerParty (100-byte stride, PARTY_SIZE 6)
	int      partyCount;   // gPlayerPartyCount, clamped 0..6 by the reader
} FtParty;

// FlagGet(f) == bit (f&7) of SaveBlock1.flags[f>>3]. pokeemerald src/event_data.c FlagGet.
bool fieldtrav_flag_get(const FpBus* bus, FpEngine eng, uint32_t sb1, int flagId);

// Does any non-egg party mon know `moveId`? This is `ScrCmd_checkpartymove`'s test
// (pokeemerald src/scrcmd.c) done from outside: walk gPlayerParty, decrypt each mon's Attacks
// substruct, compare moves[0..3]. Gen-3 party data is encrypted (substruct order =
// personality % 24, u32 XOR key = personality ^ otId) and this verifies the mon's own CHECKSUM
// before trusting a single move id — a mismatch makes that mon ineligible rather than turning a
// garbage read into a phantom HM.
bool fieldtrav_party_has_move(const FpBus* bus, const FtParty* pty, uint16_t moveId);

// Bitmask (1 << FT_HM_*) of the field moves the player may use RIGHT NOW: badge AND party move,
// both read live. This is evaluated once per plan and re-checked at each INTERACT (SPEC H1.6).
uint32_t fieldtrav_usable(const FpBus* bus, FpEngine eng, const FtParty* pty);

// --- the move program ------------------------------------------------------------------------

// One program step. `dir` is the direction of travel (FP_R/L/D/U, indexable straight into
// touch.c's s_keyDir[] exactly as fieldpath's path is). `hm` == FT_HM_NONE means "just walk one
// tile"; anything else means "perform this interaction facing `dir` FIRST".
//
// The two interaction shapes differ in who moves the avatar:
//   * FT_HM_SURF   — the game's FLDEFF_USE_SURF cutscene steps the player ONTO the water tile,
//                    so the interaction CONSUMES the move: no walk key follows.
//   * FT_HM_CUT / FT_HM_SMASH — the object vanishes and the player stays put, so the walk key
//                    still has to be pressed afterwards.
// `objSlot` is the gObjectEvents index the executor watches for the deactivation that PROVES a
// Cut/Smash landed (-1 for Surf, which proves itself with the avatar's own surf bit).
typedef struct {
	int8_t  dir;
	uint8_t hm;
	int16_t objSlot;
} FtMove;

typedef enum {
	FT_OUT_TIER0 = 0,      // a plain walk reaches it — traversal declined, the shipped router owns this tap
	FT_OUT_PLANNED,        // a conditional-edge program exists
	FT_OUT_UNREACHABLE,    // even with every eligible edge, no route
	FT_OUT_WINDOW,         // goal outside the +-FP_WHALF search window
	FT_OUT_BADMAP,         // map pointer/dimensions not sane
	FT_OUT_NOEDGE,         // edges exist on the map but none are eligible (no badge / no mon)
	FT_OUT_CAP             // a route exists but needs more than FT_MAX_INTERACTS activations
} FtOutcome;

#define FT_MAX_INTERACTS 2                  // SPEC H1.8 hard cap: a 3-HM route fails honestly
#define FT_MAXMOVES      (FP_WBOX * FP_WBOX)

typedef struct {
	bool     ok;
	int      outcome;        // FtOutcome
	int      nMoves;
	int      nInteracts;
	int      goalX, goalY;
	int      startMode, endMode;    // FT_MODE_*
	uint32_t usable;         // the eligibility mask this plan was built against (diagnostics)
	int      nEdges;         // conditional edges FOUND on the map (0 => nothing to plan through)
	FtMove   mv[FT_MAXMOVES];
} FtProgram;

// Plan a route from (sx,sy) to (gx,gy) that may use eligible HM edges.
//
// `startSurfing` is the live PLAYER_AVATAR_FLAG_SURFING bit: a tap taken while already afloat
// plans in the SURF layer from the first step and never plans a second mount (the game's own
// PartyHasMonWithSurf returns FALSE while surfing — pokeemerald src/field_player_avatar.c).
//
// `npc` / `npcN` are the occupied tiles touch.c already collects (gBackupMapLayout +MAP_OFFSET
// space). Edge objects appear in that list too; this module re-reads gObjectEvents itself and
// lets an ELIGIBLE edge object override its own block entry — nothing else does.
//
// Returns out->ok. out->outcome always says why, including the two "declined on purpose"
// answers (FT_OUT_TIER0, FT_OUT_NOEDGE) that must never turn into a partial program.
bool fieldtrav_plan(const FpBus* bus, const FpMap* m, const FtParty* pty,
                    int sx, int sy, int gx, int gy, bool startSurfing,
                    const short (*npc)[2], int npcN, FtProgram* out);

// --- exposed for the host suite ---------------------------------------------------------------
// A conditional edge found on the live map: an ACTIVE object event whose graphicsId is one this
// engine uses for a cuttable tree / breakable rock / pushable boulder.
typedef struct { int16_t slot, x, y; uint8_t hm; } FtEdge;
#define FT_MAX_EDGES 16      // OBJECT_EVENTS_COUNT is 16 in both engines

// Scan gObjectEvents for edge objects. Returns the count written to `out` (<= FT_MAX_EDGES).
// Coordinates are MAP-LOCAL (the +MAP_OFFSET grid bias the raw struct carries is removed here),
// which is the space fieldtrav_plan and fieldpath_enterable both work in.
int fieldtrav_scan_edges(const FpBus* bus, const FpMap* m, FtEdge* out);

// The Attacks-substruct slot for a given personality: sSubstructTable[personality % 24][type].
// Exposed so the host suite grades the shipped permutation table rather than a restatement of it.
int fieldtrav_substruct_slot(uint32_t personality, int type);
