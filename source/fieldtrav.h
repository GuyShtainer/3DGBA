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
	FT_HM_WATERFALL,  // PHASE 26 / lane W — a MULTI-TILE metatile edge. The player is already
	                  //   surfing, faces a MB_WATERFALL tile, and ONE interaction carries them up
	                  //   the WHOLE contiguous column of them (the game rides while the tile it
	                  //   lands on is still a waterfall — pokeemerald src/field_effect.c:1880-1893
	                  //   WaterfallFieldEffect_ContinueRideOrEnd). So a waterfall edge is one
	                  //   FtMove that moves the player K+1 tiles, not one tile.
	FT_HM_STRENGTH,   // PHASE 26 / lane W — a TERMINAL-ONLY edge: it exists ONLY when the tapped
	                  //   GOAL tile is the boulder itself, and it ACTIVATES Strength (the game's
	                  //   FLAG_SYS_USE_STRENGTH latch) without pushing anything. The router never
	                  //   passes THROUGH a boulder: pushing is a puzzle, and a puzzle is the
	                  //   player's to solve (see the argument on fieldtrav.c's `edge_at`).
	FT_HM_DIVE,       // PHASE 26 — a MAP TRANSITION, not a tile edge (SPEC-hm-dive §1)
	FT_HM_COUNT
} FtHm;

// Movement layer. The whole point of the layered search: water is elevation 1 and land is
// elevation 3, so ONE elevation cannot describe a route that crosses a pond and lands again.
enum { FT_MODE_FOOT = 0, FT_MODE_SURF = 1, FT_MODE_COUNT = 2 };

// WHICH FLAG NUMBERING A SAVE USES — and why this is NOT `FpEngine` (fix, 2026-08-14).
//
// Gen 3 has THREE flag numberings, not two. `FpEngine` (fieldpath.h) is a METATILE-BEHAVIOUR
// discriminator: Ruby, Sapphire and Emerald share one behaviour table, so RSE is exactly right
// there and that header is frozen. But the SAVE side does not follow the behaviour side:
//
//   * pokeruby puts `SaveBlock1.flags[]` at **0x1220** (include/global.h:701) where pokeemerald
//     puts it at 0x1270, and
//   * pokeruby's `SYSTEM_FLAGS` base is **0x800** (include/constants/flags.h:779) where
//     pokeemerald's is 0x860 — so every badge id, and the Running-Shoes flag, shift by 0x60.
//
// Selecting Emerald's row for a Ruby cart therefore read `sb1 + 0x1270 + (0x867>>3)` = `sb1+0x137C`
// — past the end of Ruby's `flags[]` (0x1220..0x1340) and INSIDE `vars[]`, i.e. the HM eligibility
// gates were answering from game VARIABLES on 2 of the 5 shipped titles.
//
// The values are deliberately numerically compatible with FpEngine (`FT_VAR_EMERALD` == FP_ENG_RSE,
// `FT_VAR_FRLG` == FP_ENG_FRLG, asserted in fieldtrav.c), so an FpEngine that reaches one of these
// parameters by accident still selects exactly the row it selected before this fix — the third row
// can only ever be reached deliberately, by title code.
typedef enum {
	FT_VAR_EMERALD = 0,   // BPEE            — pokeemerald
	FT_VAR_FRLG    = 1,   // BPRE / BPGE     — pokefirered
	FT_VAR_RS      = 2,   // AXVE / AXPE     — pokeruby (ONE codebase builds both; Makefile:164)
	FT_VAR_COUNT
} FtVariant;

// The ONE place the title code -> flag numbering mapping lives. `code4` is the 4-char ROM game
// code (GameProfile.code). Anything unrecognised answers FT_VAR_EMERALD — the module's existing
// "unknown engine falls back to the RSE table" convention, and harmless because an unrecognised
// title has no profile, hence no SaveBlock1, hence no flag reads at all.
FtVariant fieldtrav_variant(const char* code4);

// Per-variant constants. The three numberings genuinely disagree on all of them — the badge that
// gates Cut is BADGE01 in Emerald and BADGE02 in FireRed, the flags array sits at a different
// SaveBlock1 offset in all three, and the object graphics ids are 82/86/87 vs 95/96/97 — so,
// exactly as fieldpath's behaviour tables, these are tables that must never merge.
typedef struct {
	uint16_t flagsOff;        // SaveBlock1.flags[] byte offset
	uint16_t badgeCut, badgeSmash, badgeSurf, badgeWaterfall, badgeStrength;
	uint16_t strengthLatch;   // FLAG_SYS_USE_STRENGTH (per-map-load latch; read, never assumed)
	uint8_t  gfxCutTree, gfxRock, gfxBoulder;
	// PHASE 24 / lane A2 (decision D2, walk vs run). APPENDED — the two tables in fieldtrav.c are
	// positionally initialised, so a new field may only go at the END. This is a FLAG ID and flag
	// ids are exactly what this struct is for; the run decision itself is pure geometry and lives
	// in touchgeom.c (rungeom_*), while the ONE thing it needs from the save is
	// FlagGet(FLAG_SYS_B_DASH) — "have the Running Shoes been received".
	uint16_t runShoes;        // FLAG_SYS_B_DASH
	// PHASE 26 / lane V (SPEC-hm-dive). APPENDED for the same positional-init reason as `runShoes`.
	// **0 means "this engine has no Dive at all"** and is a NAMED value, not a missing one: it is
	// the only honest FRLG row, because FireRed/LeafGreen never reach a dive script (pokefirered
	// src/field_control_avatar.c ProcessPlayerFieldInput has no dive hook, and
	// data/scripts/field_moves.inc:210 labels the whole block "@ Unused leftover from R/S").
	// Carrying Emerald's 0x86D across would be worse than useless: FRLG's OWN badge07 is
	// **0x826, the WATERFALL badge** (this struct's `badgeWaterfall`), so an FRLG save with the
	// Soul Badge would read as dive-eligible. That is the c2a58db mistake in a new costume.
	uint16_t badgeDive;       // FLAG_BADGE07_GET in RSE; 0 = engine has no Dive
} FtEngCfg;

// Never NULL: an unknown variant returns the Emerald table (fieldpath's own fallback convention).
const FtEngCfg* fieldtrav_cfg(FtVariant var);

// Is this metatile behaviour a tile Surf can float on? Per engine, per the game's own table.
// Deliberately NOT the game's set verbatim: the four CURRENT behaviours (0x50-0x53) are surfable
// in both engines and are EXCLUDED here, because a current moves the player on its own and the
// router must never steer into a tile that takes the controls away (COVERAGE §3, rule T5.8).
// MB_WATERFALL 0x13 is likewise excluded from free surfing — and PHASE 26 turned that from a
// design preference into a MEASURED necessity. On the user's own Emerald ROM every climbable
// waterfall metatile reads collision 0 / elevation 1, i.e. exactly the same as the ocean above and
// below it (62 columns across 7 maps, read through this file's own ROM adapter). Nothing about the
// TILE stops a surfing player entering it. What stops them is
// `sForcedMovementTestFuncs[14] = MetatileBehavior_IsWaterfall`
// (pokeemerald src/field_player_avatar.c:159) -> `sForcedMovementFuncs[15] =
// ForcedMovement_PushedSouthByCurrent` (:185/:434): step on, get flushed straight back down. So a
// waterfall left in the surfable set would not be a slow route — it would be an infinite loop.
bool fieldtrav_is_surfable(FpEngine eng, int behaviour);

// --- WATERFALL: the vertical edge (PHASE 26 / lane W, SPEC-hm-waterfall) -----------------------

// Is this behaviour MB_WATERFALL? **0x13 in all three engines**, and read in each engine's own
// header rather than carried across (the c2a58db rule): pokeemerald
// include/constants/metatile_behaviors.h:24 (the enum, counted from MB_NORMAL at :5),
// pokefirered :17 `#define MB_WATERFALL 0x13`, pokeruby :23 `#define MB_WATERFALL 0x13`. Each
// engine's `MetatileBehavior_IsWaterfall` is the single-value compare this mirrors
// (pokeemerald src/metatile_behavior.c:995, pokefirered :594, pokeruby :1062).
//
// Takes an FpEngine and not an FtVariant on purpose: this is a METATILE question, and Ruby,
// Sapphire and Emerald really do share one behaviour table. It is the SAVE side that splits.
bool fieldtrav_is_waterfall(FpEngine eng, int behaviour);

// Where the game's own ride ENDS, given that (x, yFall) is a waterfall tile.
//
// `FLDEFF_USE_WATERFALL` is not a one-tile step. pokeemerald src/field_effect.c:1873-1893:
// `WaterfallFieldEffect_RideUp` issues ONE slow walk north, and
// `WaterfallFieldEffect_ContinueRideOrEnd` then asks `MetatileBehavior_IsWaterfall(objectEvent->
// currentMetatileBehavior)` — if the tile the player just landed on is STILL a waterfall it rides
// again. So the ride climbs the whole contiguous column and stops on the first tile above it that
// is not a waterfall. (pokefirered src/field_effect.c and pokeruby src/field_effect.c carry the
// identical five-step task table.)
//
// Returns that landing y, or -1 if (x,yFall) is not a waterfall / the column runs off the search
// window / the behaviour cannot be read. NEVER a guess: an unreadable column means no edge.
int fieldtrav_waterfall_top(const FpBus* bus, const FpMap* m, int x, int yFall);

// --- DIVE: the two metatile tests, per variant (PHASE 26, SPEC-hm-dive §2) ---------------------
//
// These are the game's OWN two predicates, and they are NOT symmetric:
//   * `MetatileBehavior_IsDiveable`  — a SHORT allow-list (three deep-water behaviours). Anything
//     else on the surface is not a dive spot.
//   * `MetatileBehavior_IsUnableToEmerge` — a short DENY-list, so every other underwater tile can
//     surface. Mirroring that asymmetry is the whole point of two functions.
// They take an `FtVariant`, not an `FpEngine`, because the FRLG answer is "there is no Dive in
// this engine at all" — a fact about the GAME, not about its metatile numbering (see `badgeDive`).
bool fieldtrav_is_diveable(FtVariant var, int behaviour);
bool fieldtrav_can_emerge(FtVariant var, int behaviour);

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

// FlagGet(f) == bit (f&7) of SaveBlock1.flags[f>>3]. pokeemerald src/event_data.c FlagGet (the
// identical function in pokeruby src/event_data.c and pokefirered src/event_data.c). BOTH halves —
// the flags[] offset AND the flag id itself — are per-VARIANT, which is why this takes one.
bool fieldtrav_flag_get(const FpBus* bus, FtVariant var, uint32_t sb1, int flagId);

// Does any non-egg party mon know `moveId`? This is `ScrCmd_checkpartymove`'s test
// (pokeemerald src/scrcmd.c) done from outside: walk gPlayerParty, decrypt each mon's Attacks
// substruct, compare moves[0..3]. Gen-3 party data is encrypted (substruct order =
// personality % 24, u32 XOR key = personality ^ otId) and this verifies the mon's own CHECKSUM
// before trusting a single move id — a mismatch makes that mon ineligible rather than turning a
// garbage read into a phantom HM.
bool fieldtrav_party_has_move(const FpBus* bus, const FtParty* pty, uint16_t moveId);

// --- ONE decrypted mon (the rail above, factored out so a reader can also ASK what it found) ---
// PHASE 24 / lane A. `fieldtrav_party_has_move` answers yes/no for the party; the traversal arc
// also has to answer "WHERE is the mon that knows Fly, and is it in the party or a PC box?" —
// a question the yes/no rail cannot express. Same decrypt, same checksum gate, one exported
// struct instead of an internal bool. Reads 80 bytes; a party mon (100-byte stride) and a boxed
// mon (80-byte stride) share this identical BoxPokemon head, which is why one reader serves both.
typedef struct {
	uint16_t species;   // Growth substruct (type 0) +0x00
	uint16_t moves[4];  // Attacks substruct (type 1) +0x00..+0x06
	// Deliberately NOT level: a party mon carries it in the battle tail (+0x54) and a BOXED mon
	// does not carry it at all (it is recomputed from experience), so one struct cannot honestly
	// hold it for both. Species + moves is what every caller here actually asks for.
} FtMon;

// Decrypt the mon at `monAddr`. false = empty slot / egg-bad-egg / CHECKSUM MISMATCH — i.e. the
// caller can never be handed a move id that the game's own integrity check would reject.
bool fieldtrav_read_mon(const FpBus* bus, uint32_t monAddr, FtMon* out);

// --- the party+PC census (PHASE 24 / lane A: a diagnostic, never a gameplay input) ------------
// struct PokemonStorage (pokeemerald include/pokemon_storage.h / pokefirered ditto):
//   +0x0000 u8 currentBox, +0x0004 BoxPokemon boxes[14][30] (80 B each = 0x8340 bytes, so
//   boxNames lands at +0x8344 — the arithmetic that cross-checks the +4).
// `storageBase` is the ALREADY-DEREFERENCED struct address (the caller derefs
// gPokemonStoragePtr, GameProfile.pcStoragePtr). 0 = no PC scan, party only.
#define FT_BOX_COUNT   14
#define FT_BOX_SLOTS   30
#define FT_CENSUS_MAX  24    // box hits kept; enough to name every field-move mon in a real save

typedef struct {
	int16_t  box;       // -1 = party
	int16_t  slot;      // party slot 0..5, or box slot 0..29
	uint16_t species;
	uint16_t moves[4];
} FtMonRec;

typedef struct {
	int      partyCount;               // slots the caller said are live
	int      nParty;                   // party records actually decrypted (checksum-verified)
	FtMonRec party[6];
	int      boxLive;                  // box slots with hasSpecies set
	int      boxOk;                    // of those, slots whose checksum verified
	int      nBox;                     // records kept (<= FT_CENSUS_MAX)
	FtMonRec box[FT_CENSUS_MAX];
} FtCensus;

// Census the party (every live slot) and the PC (only mons knowing one of `moves[0..nMoves-1]`,
// so a full 420-slot scan still fits a small fixed record set). Returns nParty + nBox.
int fieldtrav_census(const FpBus* bus, uint32_t partyBase, int partyCount, uint32_t storageBase,
                     const uint16_t* moves, int nMoves, FtCensus* out);

// Bitmask (1 << FT_HM_*) of the field moves the player may use RIGHT NOW: badge AND party move,
// both read live. This is evaluated once per plan and re-checked at each INTERACT (SPEC H1.6).
uint32_t fieldtrav_usable(const FpBus* bus, FtVariant var, const FtParty* pty);

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
	FT_OUT_CAP,            // a route exists but needs more than FT_MAX_INTERACTS activations
	// --- slice 2 (SPEC §3.5), appended so every value above keeps its number ---
	FT_OUT_NOEXC,          // excursion search ran and NO out-and-back candidate survived
	// --- phase 26 / DIVE (SPEC-hm-dive §4.4), appended for the same reason ---
	FT_OUT_NODIVE          // the dive search ran and refused: no eligibility, no paired map, no
	                       //   reachable dive spot, or no surfacing spot that reaches the goal
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
	// PHASE 26 / lane W. APPENDED (this struct is memset + named-assigned, never positionally
	// initialised, so the end is the only safe place and a new field defaults to 0).
	// 1 = the TAP landed on a MB_WATERFALL tile and `goalX/goalY` is NOT the tile the user
	// touched: it is the tile the game's own ride would leave them on. See the retarget block in
	// fieldtrav_plan. The caller MUST re-plan against `goalX/goalY`, never against the raw tap.
	int      wfRetarget;
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
// `var` is the SAVE's flag numbering (see FtVariant). It is a separate argument from `m->engine`
// on purpose: the map speaks the RSE metatile numbering for Ruby, Sapphire AND Emerald, while the
// save does not — this is the one seam where a Ruby cart stops being "RSE".
//
// Returns out->ok. out->outcome always says why, including the two "declined on purpose"
// answers (FT_OUT_TIER0, FT_OUT_NOEDGE) that must never turn into a partial program.
bool fieldtrav_plan(const FpBus* bus, const FpMap* m, FtVariant var, const FtParty* pty,
                    int sx, int sy, int gx, int gy, bool startSurfing,
                    const short (*npc)[2], int npcN, FtProgram* out);

// --- exposed for the host suite ---------------------------------------------------------------
// A conditional edge found on the live map: an ACTIVE object event whose graphicsId is one this
// engine uses for a cuttable tree / breakable rock / pushable boulder.
typedef struct { int16_t slot, x, y; uint8_t hm; } FtEdge;
#define FT_MAX_EDGES 16      // OBJECT_EVENTS_COUNT is 16 in both engines

// Scan gObjectEvents for edge objects. Returns the count written to `out` (<= FT_MAX_EDGES).
// Coordinates are MAP-LOCAL (the +MAP_OFFSET grid bias the raw struct carries is removed here),
// which is the space fieldtrav_plan and fieldpath_enterable both work in. Takes the same `var`
// its caller planned with, so exactly ONE rule selects the constant row.
int fieldtrav_scan_edges(const FpBus* bus, const FpMap* m, FtVariant var, FtEdge* out);

// The Attacks-substruct slot for a given personality: sSubstructTable[personality % 24][type].
// Exposed so the host suite grades the shipped permutation table rather than a restatement of it.
int fieldtrav_substruct_slot(uint32_t personality, int type);

// --- STRENGTH: what "Strength by touch" means, and the one probe it needs ----------------------
// PHASE 26 / lane W, SPEC-hm-waterfall §5. Settled rather than deferred:
//
// A BOULDER IS A PUZZLE, NOT AN OBSTACLE. Every other conditional edge REMOVES its blocker (the
// tree and the rock deactivate; the water becomes rideable) and leaves the world in the state the
// player would have chosen anyway. A boulder does not vanish — it MOVES, one tile per bump
// (pokeemerald src/field_player_avatar.c TryPushBoulder), and where it lands is the answer to a
// puzzle. Auto-pushing one to shorten a walk can strand it against a wall and lock the puzzle
// until the map is re-entered. So the router NEVER pushes, and never plans a path THROUGH a
// boulder — the boulder stays a plain blocker for transit, exactly as it is today.
//
// What "by touch" then means is the other half of the game's own model, which the shipped code was
// missing entirely: `EventScript_StrengthBoulder` (pokeemerald data/scripts/field_move_scripts.inc
// :123-133) is an ACTIVATION — badge, party move, a yes/no, and then `setflag FLAG_SYS_USE_STRENGTH`
// (:145). Nothing moves. It is per-map-visit (`ClearTempFieldEventData`, src/event_data.c:45,
// runs on every map load), so it is also the thing a player forgets and has to walk back for.
// Tapping the boulder itself is unambiguous intent, costs nothing, and is undoable by walking out
// of the map. THAT is the feature, and `FT_HM_STRENGTH` is a TERMINAL edge only.
//
// This probe is the tier gate touch.c needs BEFORE it consults the dry router: tapping a boulder
// already succeeds at tier 0 (fieldpath deliberately allows a blocked GOAL as the terminal), so
// without asking first, a Strength program could never be reached. Cheap by construction — the
// object scan runs first and the expensive party decrypt only if a boulder really is there.
//
// Returns the gObjectEvents slot of a boulder ON (gx,gy) that the game would ACTUALLY prompt for,
// or -1. -1 covers all four honest refusals: no boulder there; no badge / no mon that knows
// STRENGTH; and — the one that is easy to miss — FLAG_SYS_USE_STRENGTH ALREADY SET, where the
// script prints `Text_StrengthActivated` with no yes/no at all (:157 EventScript_
// CheckActivatedBoulder), so a program aimed at it would sit waiting for a prompt that never comes.
int fieldtrav_strength_tap(const FpBus* bus, const FpMap* m, FtVariant var, const FtParty* pty,
                           int gx, int gy);

// ==============================================================================================
// SLICE 2 — CROSS-MAP EXCURSIONS (SPEC-family-traversal §3, the "Lavaridge class")
// ==============================================================================================
// THE ASK (user, same message): "A step beyond could be to include paths in the BFS algorithm
// through doors. A great example would be lavaridge town, where the jakuzi is seen from outside
// of it, but freely entered through the pokecenter, being able to simply touch that and the game
// understanding how to reach that place would be outstanding."
//
// The shape, and why it is bounded so hard: a tap is a SCREEN OFFSET from the player
// (touch.c:432), so the goal is always on the CURRENT map by construction. Every useful
// excursion therefore RETURNS to the current map, and the whole v1 search is exactly one
// out-and-back — leave through warp Wi, cross the interior D on foot, come back through a
// DIFFERENT warp Wj that lands somewhere the goal is reachable from (SPEC H3.1).
//
// Honesty limits, stated rather than discovered later (SPEC §3.1):
//   * neighbour maps are NOT in RAM, so D's grid is read from ROM — it cannot see runtime layout
//     changes and carries NO object events, i.e. NPCs are invisible at plan time. Plan-time
//     connectivity is therefore optimistic-but-static, and the executor RE-PLANS every leg on the
//     LIVE grid at arrival (H3.4), where NPCs exist again.
//   * map CONNECTIONS (seamless route edges) are NOT modelled — that is the T5.9 refusal, and
//     pret's own behaviour reads have the identical limitation.
//   * failure is total: no candidate survives => plan NOTHING (H3.5). Never a partial program,
//     never "walk toward the door and hope".

// One WarpEvent, as it sits in ROM: struct WarpEvent { s16 x, y; u8 elevation, warpId, mapNum,
// mapGroup; } — 8 bytes, the same record fieldpath.c's warp_at already indexes.
typedef struct { int16_t x, y; uint8_t elev, warpId, mapNum, mapGroup; } FtWarp;
#define FT_MAX_WARPS 64      // fieldpath's own sane-count guard, kept identical

// Read a map's whole warp table out of its MapEvents. `events` is MapHeader.events (+0x04).
// Returns the count written (<= cap), or 0 if any pointer/count fails validation.
int fieldtrav_warps(const FpBus* bus, uint32_t events, FtWarp* out, int cap);

// A destination map resolved out of ROM through gMapGroups. gMapGroups is
// `const struct MapHeader *const *const gMapGroups[]`: index by GROUP to get that group's array
// of MapHeader pointers, then by NUM (pret Overworld_GetMapHeaderByGroupAndId).
typedef struct {
	uint32_t header;      // struct MapHeader*
	uint32_t layout;      // MapHeader.mapLayout   (+0x00)
	uint32_t events;      // MapHeader.events      (+0x04)
	uint32_t grid;        // MapLayout.map         (+0x0C) — raw w*h u16, NO border padding
	int      w, h;        // MapLayout.width/height (+0x00/+0x04)
} FtRomMap;

// Resolve (group,num) through gMapGroups. Every pointer is ROM-range checked and the dimensions
// are sanity-capped, so a bad table degrades to "no excursion", never to a wander.
bool fieldtrav_rom_map(const FpBus* bus, uint32_t mapGroupsRom, int group, int num, FtRomMap* out);

// The ROM-grid BUS ADAPTER (SPEC §3.1). fieldpath_enterable / fieldpath_behaviour_at /
// fieldpath_classify all speak gBackupMapLayout space (+MAP_OFFSET, border-padded). Rather than
// fork those rules for ROM maps — the one thing that would let the two grids drift apart — this
// wraps the caller's bus so reads inside a synthetic backup-space window resolve into the ROM
// grid, and hands back an FpMap the SHIPPED functions accept verbatim. fieldpath.c is untouched.
typedef struct { const FpBus* inner; FtRomMap rm; } FtRomBus;
void fieldtrav_rom_bus(FtRomBus* rb, const FpBus* inner, const FtRomMap* rm, FpEngine eng,
                       FpBus* busOut, FpMap* mapOut);

// The result of one out-and-back search. Coordinates are map-local in each map's own space.
typedef struct {
	bool     ok;
	int      outcome;             // FtOutcome (FT_OUT_PLANNED / FT_OUT_NOEXC / FT_OUT_BADMAP / …)
	int      wi;                  // index into the CURRENT map's warp table — the way out
	int      wiX, wiY;            // that warp's tile (current map)
	int      dGroup, dNum;        // the interior we pass through
	int      arrX, arrY;          // where the game drops us inside D
	int      wj;                  // index into D's warp table — the way back
	int      wjX, wjY;            // that warp's tile (D's space)
	int      backX, backY;        // where we land again on the current map
	int      stepsOut, stepsMid, stepsBack;   // BFS step counts of the three legs (ranking only)
} FtExcursion;

#define FT_EXC_CAND 8        // SPEC H3.2: at most 8 candidate exits, nearest first

// Search for a single out-and-back excursion from (sx,sy) to (gx,gy) on the CURRENT map.
//
// PRECONDITION, enforced by the caller (touch.c) and not re-litigated here: same-map planning
// (tier 0 dry walk, then tier 1 conditional edges) has ALREADY failed. Excursions are the last
// tier, so a same-map route can never be displaced by one.
//
// `npc` / `npcN` are the live occupied tiles, in gBackupMapLayout (+MAP_OFFSET) space, exactly as
// fieldtrav_plan takes them: they block the CURRENT map's two legs. D's leg has no NPC data at
// all (it is ROM), which is precisely why the executor re-plans it live on arrival.
bool fieldtrav_excursion(const FpBus* bus, const FpMap* m, uint32_t mapGroupsRom,
                         int curGrp, int curNum, int sx, int sy, int gx, int gy,
                         const short (*npc)[2], int npcN, FtExcursion* out);

// ==============================================================================================
// PHASE 26 / LANE V — DIVE (SPEC-hm-dive)
// ==============================================================================================
// THE ARCHITECTURAL POINT, settled from pret before a line of this was written: **Dive is not a
// tile edge.** Cut, Rock Smash and Surf all end with the player still on the map they started on.
// Dive MOVES THE PLAYER TO A DIFFERENT MAP — and it does it through a mechanism that is neither a
// tile edge nor a warp event:
//
//   pokeemerald src/overworld.c:756-782 `SetDiveWarp(dir, x, y)` looks up
//   `GetMapConnection(CONNECTION_DIVE|CONNECTION_EMERGE)` — a MAP CONNECTION, the same table that
//   carries the north/south/east/west route seams — and calls
//   `SetWarpDestination(connection->mapGroup, connection->mapNum, WARP_ID_NONE, x, y)` with the
//   player's OWN tile. There is no warp record, no warpId and no destination coordinate of any
//   kind: **the coordinate map is the IDENTITY**. You dive at (x,y) and you arrive at (x,y).
//
// That makes Dive a THIRD kind of map transition (tile edge / warp event / connection), and it is
// why this is not `fieldtrav_excursion` with a different constant: the excursion machinery is
// keyed on warp INDICES and on `ft_warp_approach`'s terminal semantics, and a dive has neither.
// What it does share is the SHAPE — leave the map, cross the other one, come back somewhere new —
// so the search below is the same out-and-back with the warp pair replaced by (a) any diveable
// tile here and (b) any surfacing tile there.
//
// Coordinates are identity, so the two maps are DIMENSION-ALIGNED. Measured on pret's own layout
// data this session, all seven Emerald pairs agree exactly (Route 105/124/125/126/127/128/129 vs
// their UNDERWATER_ twins), every diveable surface tile has a walkable underwater counterpart, and
// every emergeable underwater tile has a surfable, collision-free surface counterpart.

// MapHeader.connections direction ids — pokeemerald include/constants/global.h:153/154
// (identical values in pokefirered include/constants/global.h:125/126 and pokeruby).
#define FT_CONN_DIVE    5
#define FT_CONN_EMERGE  6
// The widest connection list any vanilla map carries is 5 (MAP_ROUTE124, measured across all 869
// pokeemerald data/maps/*/map.json this session); 16 is the loose structural cap that keeps a bad
// pointer from walking the ROM.
#define FT_MAX_CONN 16

// Which way a dive plan goes. Derived, never guessed: `FT_DIVE_UP` iff the LIVE map header says
// MAP_TYPE_UNDERWATER, which is the game's own emerge gate (pokeemerald
// src/field_control_avatar.c:475 `gMapHeader.mapType == MAP_TYPE_UNDERWATER`).
enum { FT_DIVE_DOWN = 0, FT_DIVE_UP = 1 };

// gMapHeader.mapType (+0x17) == MAP_TYPE_UNDERWATER (5). `m->mapHeader` is the EWRAM gMapHeader
// STRUCT, the same pointer fieldtrav_excursion reads MapEvents out of.
bool fieldtrav_underwater(const FpBus* bus, const FpMap* m);

// Follow gMapHeader.connections (+0x0C) and return the map on the other end of direction `dir`.
// False = this map has no such connection — which is the honest answer for BOTH "no connections at
// all" (pret emits a NULL pointer there: tools/mapjson/mapjson.cpp:155-159, and 454 of Emerald's
// 518 maps take that branch — phase-26 audit F3, re-measured on the user's own BPEE ROM by
// phase 28 lane X: 518 maps enumerated through fieldtrav_rom_map, 454 with a non-ROM +0x0C)
// and the SCRIPTED dive maps (Sootopolis, Sealed Chamber, Marine Cave,
// Seafloor Cavern, Abandoned Ship, Route 134), whose destination is set by an ON_DIVE_WARP map
// script running `setdivewarp` rather than by a connection (src/overworld.c:766-769). We do not
// interpret map scripts, so those dive spots are REFUSED, not guessed — a named degradation.
bool fieldtrav_connection(const FpBus* bus, uint32_t mapHeader, int dir, int* grp, int* num);

// One dive out-and-back. Both coordinate pairs are map-local, and each is valid in BOTH maps at
// once — that is what "the coordinate map is the identity" means in practice.
typedef struct {
	bool     ok;
	int      outcome;          // FtOutcome (FT_OUT_PLANNED / FT_OUT_NODIVE / FT_OUT_BADMAP / …)
	int      dir;              // FT_DIVE_DOWN / FT_DIVE_UP, or -1 if the search never got that far
	int      dGroup, dNum;     // the paired map (underwater twin, or the surface above)
	int      diveX, diveY;     // where the HM is used here == where we arrive over there
	int      upX, upY;         // where the HM is used over there == where we arrive back here
	int      stepsOut;         // legs, in steps, for ranking + diagnostics only
	int      stepsMid;
	int      stepsBack;
	int      nSpots;           // reachable HM spots found on THIS map (0 => nothing to plan from)
} FtDive;

#define FT_DIVE_CAND 8       // at most 8 candidate dive spots, nearest first (FT_EXC_CAND's rule)

// Plan a dive out-and-back from (sx,sy) to (gx,gy) on the CURRENT map.
//
// PRECONDITIONS, enforced by the caller and not re-litigated here: same-map planning (tier 0 dry
// walk, tier 1 conditional edges) has ALREADY failed, so a dive can never displace a same-map
// route.
//
// `startSurfing` is the live PLAYER_AVATAR_FLAG_SURFING bit. On a SURFACE map a dive is only
// planned when it is already set: `TrySetDiveWarp` reads the player's OWN tile (pokeemerald
// src/field_control_avatar.c:965-971), and that tile is deep water, so the player is necessarily
// afloat. A tap that would need "mount Surf, THEN dive" is refused whole (FT_OUT_NODIVE) rather
// than half-planned — the mount is tier 1's job and chaining the two is a later slice.
// Underwater the flag is PLAYER_AVATAR_FLAG_UNDERWATER instead, so it is ignored there and the
// map type decides.
//
// `npc`/`npcN` are the live occupied tiles in gBackupMapLayout (+MAP_OFFSET) space, exactly as
// fieldtrav_plan and fieldtrav_excursion take them: they block THIS map's two legs. The paired
// map is read from ROM and therefore carries no object events at all, which is precisely why the
// executor re-plans that leg live on arrival (the H3.4 rule, unchanged).
bool fieldtrav_dive(const FpBus* bus, const FpMap* m, uint32_t mapGroupsRom,
                    int curGrp, int curNum, FtVariant var, const FtParty* pty,
                    int sx, int sy, int gx, int gy, bool startSurfing,
                    const short (*npc)[2], int npcN, FtDive* out);
