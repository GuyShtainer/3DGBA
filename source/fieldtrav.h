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
	// PHASE 24 / lane A2 (decision D2, walk vs run). APPENDED — the two tables in fieldtrav.c are
	// positionally initialised, so a new field may only go at the END. This is a FLAG ID and flag
	// ids are exactly what this struct is for; the run decision itself is pure geometry and lives
	// in touchgeom.c (rungeom_*), while the ONE thing it needs from the save is
	// FlagGet(FLAG_SYS_B_DASH) — "have the Running Shoes been received".
	uint16_t runShoes;        // FLAG_SYS_B_DASH
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
	FT_OUT_CAP,            // a route exists but needs more than FT_MAX_INTERACTS activations
	// --- slice 2 (SPEC §3.5), appended so every value above keeps its number ---
	FT_OUT_NOEXC           // excursion search ran and NO out-and-back candidate survived
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
