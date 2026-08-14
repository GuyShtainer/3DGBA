// fieldtrav.c — see fieldtrav.h. PURE C (CLAUDE.md #4): no libctru, no citro, no mGBA.
//
// Every constant is cited at the line that uses it and was RE-READ from pret this session
// (2026-08-14). Nothing here is derived from the other engine's numbering — the Cut badge alone
// (BADGE01 in Emerald, BADGE02 in FireRed) is enough to make a shared table a silent bug that
// only shows up as "the game refused a prompt we were sure it would accept".
#include <string.h>   // memset
#include <stdlib.h>   // abs
#include "fieldtrav.h"

// ============================ per-engine constants ==========================================
//
// RSE (Emerald). SaveBlock1.flags: pokeemerald include/global.h:1020 `/*0x1270*/ u8
// flags[NUM_FLAG_BYTES]`. Flag ids: include/constants/flags.h:1348 SYSTEM_FLAGS = 0x860,
// :1359-1366 FLAG_BADGE01_GET..FLAG_BADGE08_GET = SYSTEM_FLAGS + 0x7..0xE,
// :1399 FLAG_SYS_USE_STRENGTH = SYSTEM_FLAGS + 0x29.
// Which badge gates which move is read off the SCRIPTS, not assumed:
//   data/scripts/field_move_scripts.inc:4   EventScript_CutTree      -> FLAG_BADGE01_GET (0x867)
//                                    :63    EventScript_RockSmash    -> FLAG_BADGE03_GET (0x869)
//                                    :126   EventScript_StrengthBoulder -> FLAG_BADGE04_GET (0x86A)
//   src/field_control_avatar.c:450          the Surf gate            -> FLAG_BADGE05_GET (0x86B)
//   src/field_control_avatar.c:453-458      the Waterfall gate       -> FLAG_BADGE08_GET (0x86E)
// Object graphics ids: include/constants/event_objects.h:89/93/94.
static const FtEngCfg s_cfgRse = {
	/* flagsOff      */ 0x1270,
	/* badgeCut      */ 0x867,   // BADGE01
	/* badgeSmash    */ 0x869,   // BADGE03
	/* badgeSurf     */ 0x86B,   // BADGE05
	/* badgeWaterfall*/ 0x86E,   // BADGE08
	/* badgeStrength */ 0x86A,   // BADGE04
	/* strengthLatch */ 0x889,   // FLAG_SYS_USE_STRENGTH = 0x860 + 0x29
	/* gfxCutTree    */ 82,      // OBJ_EVENT_GFX_CUTTABLE_TREE
	/* gfxRock       */ 86,      // OBJ_EVENT_GFX_BREAKABLE_ROCK
	/* gfxBoulder    */ 87,      // OBJ_EVENT_GFX_PUSHABLE_BOULDER
};

// FRLG (FireRed / LeafGreen). SaveBlock1.flags: pokefirered include/global.h:790 `/*0x0EE0*/`.
// Flag ids: include/constants/flags.h:1324 SYS_FLAGS = 0x800, :1364-1371 badges = SYS_FLAGS +
// 0x20..0x27, :1332 FLAG_SYS_USE_STRENGTH = SYS_FLAGS + 0x5. Badge-to-move again off the scripts,
// data/scripts/field_moves.inc:
//   :4   EventScript_CutTree        -> FLAG_BADGE02_GET (0x821)   <- NOT 01, the Emerald trap
//   :62  EventScript_RockSmash      -> FLAG_BADGE06_GET (0x825)   <- NOT 03
//   :122 EventScript_StrengthBoulder-> FLAG_BADGE04_GET (0x823)
//   src/field_control_avatar.c:605  the Surf gate -> FLAG_BADGE05_GET (0x824)
//   src/field_control_avatar.c:608  the Waterfall gate -> FLAG_BADGE07_GET (0x826)  <- NOT 08
// Object graphics ids: include/constants/event_objects.h:101-103.
static const FtEngCfg s_cfgFrlg = {
	/* flagsOff      */ 0x0EE0,
	/* badgeCut      */ 0x821,   // BADGE02
	/* badgeSmash    */ 0x825,   // BADGE06
	/* badgeSurf     */ 0x824,   // BADGE05
	/* badgeWaterfall*/ 0x826,   // BADGE07
	/* badgeStrength */ 0x823,   // BADGE04
	/* strengthLatch */ 0x805,   // FLAG_SYS_USE_STRENGTH = 0x800 + 0x5
	/* gfxCutTree    */ 95,      // OBJ_EVENT_GFX_CUT_TREE
	/* gfxRock       */ 96,      // OBJ_EVENT_GFX_ROCK_SMASH_ROCK
	/* gfxBoulder    */ 97,      // OBJ_EVENT_GFX_PUSHABLE_BOULDER
};

const FtEngCfg* fieldtrav_cfg(FpEngine eng) {
	return (eng == FP_ENG_FRLG) ? &s_cfgFrlg : &s_cfgRse;
}

// ============================ the surfable metatile sets ====================================
//
// RSE: pokeemerald src/metatile_behavior.c sTileBitAttributes[] & TILE_FLAG_SURFABLE (:25-90),
// numeric values re-derived from the include/constants/metatile_behaviors.h enum this session.
// The game's set also carries 0x50-0x53 (the four CURRENTs), 0x6C/0x6D (water door / water arrow
// warp) and 0x6F (MB_UNUSED_6F). We take NONE of those:
//   * currents move the player on their own (COVERAGE §3 rule T5.8: never route into a tile that
//     takes the controls away),
//   * 0x6C/0x6D are WARPS and fieldpath_classify already owns them — classification has
//     precedence (SPEC H1.1); treating them as free water would let a route drift into a warp,
//   * 0x13 MB_WATERFALL is a waterfall EDGE (slice 2), never a tile to drift onto,
//   * 0x6F is unused-by-any-map.
static bool surfable_rse(int b) {
	switch (b) {
	case 0x10:   // MB_POND_WATER
	case 0x11:   // MB_INTERIOR_DEEP_WATER
	case 0x12:   // MB_DEEP_WATER            (also the Dive trigger — Dive deferred, SPEC §5)
	case 0x14:   // MB_SOOTOPOLIS_DEEP_WATER
	case 0x15:   // MB_OCEAN_WATER
	case 0x19:   // MB_NO_SURFACING
	case 0x22:   // MB_SEAWEED
	case 0x2A:   // MB_SEAWEED_NO_SURFACING
		return true;
	default:
		return false;
	}
}

// FRLG: pokefirered src/metatile_behavior.c:5-17 sBehaviorSurfable[], numeric values from
// include/constants/metatile_behaviors.h:14-25/62-65. Same two exclusions (currents 0x50-0x53,
// waterfall 0x13). Note FRLG's set has 0x11 MB_FAST_WATER, 0x1A MB_UNUSED_WATER and 0x1B
// MB_CYCLING_ROAD_WATER where RSE has the Sootopolis/seaweed family: the two sets are NOT the
// same numbers with different names, which is exactly why they are two functions.
static bool surfable_frlg(int b) {
	switch (b) {
	case 0x10:   // MB_POND_WATER
	case 0x11:   // MB_FAST_WATER
	case 0x12:   // MB_DEEP_WATER
	case 0x15:   // MB_OCEAN_WATER
	case 0x1A:   // MB_UNUSED_WATER
	case 0x1B:   // MB_CYCLING_ROAD_WATER
		return true;
	default:
		return false;
	}
}

bool fieldtrav_is_surfable(FpEngine eng, int behaviour) {
	if (behaviour < 0) return false;                       // unreadable -> never "yes"
	return (eng == FP_ENG_FRLG) ? surfable_frlg(behaviour) : surfable_rse(behaviour);
}

// ============================ eligibility ===================================================

bool fieldtrav_flag_get(const FpBus* bus, FpEngine eng, uint32_t sb1, int flagId) {
	if (!sb1 || (sb1 >> 24) != 0x02u) return false;        // no save loaded -> no badge, never a guess
	if (flagId < 0) return false;
	const FtEngCfg* c = fieldtrav_cfg(eng);
	// pokeemerald src/event_data.c FlagGet: gSaveBlock1Ptr->flags[id / 8] & (1 << (id & 7)).
	uint32_t byteAddr = sb1 + (uint32_t)c->flagsOff + ((uint32_t)flagId >> 3);
	return (bus->read8(bus->ctx, byteAddr) >> (flagId & 7)) & 1u;
}

// sSubstructTable, pokeemerald src/pokemon.c:3609-3632 (the SUBSTRUCT_CASE list). Row
// `personality % 24`; column = substruct TYPE (0 Growth, 1 Attacks, 2 EVs, 3 Misc); the value is
// the SLOT that type occupies inside the 48-byte secure block. Read straight off the macro
// expansion (:3586-3600: `case <type>: substruct = &substructsN[vN+1]`), so the table below is
// the game's own permutation, not a restatement of the community table.
static const uint8_t s_substruct[24][4] = {
	{0,1,2,3}, {0,1,3,2}, {0,2,1,3}, {0,3,1,2}, {0,2,3,1}, {0,3,2,1},
	{1,0,2,3}, {1,0,3,2}, {2,0,1,3}, {3,0,1,2}, {2,0,3,1}, {3,0,2,1},
	{1,2,0,3}, {1,3,0,2}, {2,1,0,3}, {3,1,0,2}, {2,3,0,1}, {3,2,0,1},
	{1,2,3,0}, {1,3,2,0}, {2,1,3,0}, {3,1,2,0}, {2,3,1,0}, {3,2,1,0},
};

int fieldtrav_substruct_slot(uint32_t personality, int type) {
	if (type < 0 || type > 3) return -1;
	return (int)s_substruct[personality % 24u][type];
}

// struct BoxPokemon (pokefirered include/pokemon.h, byte-identical in pokeemerald):
//   +0x00 u32 personality, +0x04 u32 otId, +0x08 nickname[10], +0x12 language,
//   +0x13 bitfield { isBadEgg:1, hasSpecies:1, isEgg:1, blockBoxRS:1, unused:4 },
//   +0x14 otName[7], +0x1B markings, +0x1C u16 checksum, +0x1E u16 unknown,
//   +0x20 secure block: 4 x 12-byte substructs, each u32 XOR (personality ^ otId)
//         (src/pokemon.c DecryptBoxMon: raw[i] ^= otId; raw[i] ^= personality).
// struct Pokemon = BoxPokemon (80 B) + the battle-stats tail; the party stride is 100 B.
#define FT_MON_STRIDE   100u
#define FT_PARTY_SIZE   6
#define FT_SECURE_OFF   0x20u
#define FT_SUBSTRUCT_SZ 12u

// THE decrypt rail — one implementation, three callers (party has-move, the census's party pass,
// the census's PC pass). PHASE 24 factored it out of fieldtrav_party_has_move without changing a
// single rule: same hasSpecies/isEgg/isBadEgg rejections, same whole-block decrypt, same checksum
// gate BEFORE any field is believed.
bool fieldtrav_read_mon(const FpBus* bus, uint32_t monAddr, FtMon* out) {
	if (!bus || !out || !monAddr) return false;
	uint32_t bank = monAddr >> 24;
	// gPlayerParty lives in EWRAM in Emerald/FRLG (0x02...) and in IWRAM in Ruby/Sapphire
	// (0x03004360, pokeruby.sym) — accept both rather than hard-coding one game's memory map.
	if (bank != 0x02u && bank != 0x03u) return false;

	uint8_t bits = bus->read8(bus->ctx, monAddr + 0x13u);
	if (!((bits >> 1) & 1u)) return false;         // hasSpecies == 0 -> empty slot
	if ((bits >> 2) & 1u) return false;            // isEgg -> the game refuses it too
	if (bits & 1u) return false;                   // isBadEgg -> never trust its data

	uint32_t pers = bus->read32(bus->ctx, monAddr + 0x00u);
	uint32_t otId = bus->read32(bus->ctx, monAddr + 0x04u);
	uint32_t key  = pers ^ otId;

	// Decrypt the whole 48-byte secure block once, so the checksum can be verified BEFORE a
	// single move id is believed. 24 u16 summed == BoxPokemon.checksum
	// (src/pokemon.c CalculateBoxMonChecksum sums all four substructs' raw[] u16s).
	uint16_t dec[24];
	uint16_t sum = 0;
	for (int w = 0; w < 12; w++) {                       // 12 u32 = 48 bytes
		uint32_t v = bus->read32(bus->ctx, monAddr + FT_SECURE_OFF + 4u * (uint32_t)w) ^ key;
		dec[2 * w + 0] = (uint16_t)(v & 0xFFFFu);
		dec[2 * w + 1] = (uint16_t)(v >> 16);
		sum = (uint16_t)(sum + dec[2 * w + 0] + dec[2 * w + 1]);
	}
	if (sum != bus->read16(bus->ctx, monAddr + 0x1Cu)) return false;   // decrypt-integrity rail

	// Growth (type 0) +0x00 = species; Attacks (type 1) +0x00 = moves[4].
	int g = fieldtrav_substruct_slot(pers, 0) * (int)(FT_SUBSTRUCT_SZ / 2);   // u16 units
	int a = fieldtrav_substruct_slot(pers, 1) * (int)(FT_SUBSTRUCT_SZ / 2);
	out->species = dec[g];
	for (int mv = 0; mv < 4; mv++) out->moves[mv] = dec[a + mv];
	return true;
}

bool fieldtrav_party_has_move(const FpBus* bus, const FtParty* pty, uint16_t moveId) {
	if (!pty || !pty->partyBase || pty->partyCount <= 0) return false;
	int n = pty->partyCount; if (n > FT_PARTY_SIZE) n = FT_PARTY_SIZE;
	for (int i = 0; i < n; i++) {
		FtMon m;
		if (!fieldtrav_read_mon(bus, pty->partyBase + FT_MON_STRIDE * (uint32_t)i, &m)) continue;
		for (int mv = 0; mv < 4; mv++)
			if (m.moves[mv] == moveId) return true;
	}
	return false;
}

// The census. Party: every live slot, recorded whole. PC: 14 x 30 slots, and a record is kept
// ONLY for a mon that knows one of the caller's moves — that is what keeps a 420-slot sweep
// inside a 24-record struct. Pure reads; nothing here can influence a route.
int fieldtrav_census(const FpBus* bus, uint32_t partyBase, int partyCount, uint32_t storageBase,
                     const uint16_t* moves, int nMoves, FtCensus* out) {
	if (!bus || !out) return 0;
	for (int i = 0; i < (int)sizeof *out; i++) ((uint8_t*)out)[i] = 0;
	out->partyCount = partyCount;

	if (partyBase && partyCount > 0) {
		int n = partyCount > FT_PARTY_SIZE ? FT_PARTY_SIZE : partyCount;
		for (int i = 0; i < n; i++) {
			FtMon m;
			if (!fieldtrav_read_mon(bus, partyBase + FT_MON_STRIDE * (uint32_t)i, &m)) continue;
			FtMonRec* r = &out->party[out->nParty++];
			r->box = -1; r->slot = (int16_t)i; r->species = m.species;
			for (int mv = 0; mv < 4; mv++) r->moves[mv] = m.moves[mv];
		}
	}

	if (storageBase && (storageBase >> 24) == 0x02u && moves && nMoves > 0) {
		for (int b = 0; b < FT_BOX_COUNT; b++) {
			for (int s = 0; s < FT_BOX_SLOTS; s++) {
				uint32_t mon = storageBase + 4u
				             + 80u * (uint32_t)(b * FT_BOX_SLOTS + s);   // BoxPokemon stride 80
				if (!((bus->read8(bus->ctx, mon + 0x13u) >> 1) & 1u)) continue;   // hasSpecies
				out->boxLive++;
				FtMon m;
				if (!fieldtrav_read_mon(bus, mon, &m)) continue;   // egg / bad egg / bad checksum
				out->boxOk++;
				bool want = false;
				for (int mv = 0; mv < 4 && !want; mv++)
					for (int w = 0; w < nMoves; w++)
						if (m.moves[mv] && m.moves[mv] == moves[w]) { want = true; break; }
				if (!want || out->nBox >= FT_CENSUS_MAX) continue;
				FtMonRec* r = &out->box[out->nBox++];
				r->box = (int16_t)b; r->slot = (int16_t)s; r->species = m.species;
				for (int mv = 0; mv < 4; mv++) r->moves[mv] = m.moves[mv];
			}
		}
	}
	return out->nParty + out->nBox;
}

// Move ids (engine-invariant; pokeemerald include/constants/moves.h:19/61/74/131/253).
#define FT_MOVE_CUT         15
#define FT_MOVE_SURF        57
#define FT_MOVE_STRENGTH    70
#define FT_MOVE_WATERFALL  127
#define FT_MOVE_ROCK_SMASH 249

uint32_t fieldtrav_usable(const FpBus* bus, FpEngine eng, const FtParty* pty) {
	uint32_t m = 0;
	if (!bus || !pty) return 0;
	const FtEngCfg* c = fieldtrav_cfg(eng);
	if (fieldtrav_flag_get(bus, eng, pty->sb1, c->badgeCut) &&
	    fieldtrav_party_has_move(bus, pty, FT_MOVE_CUT))        m |= 1u << FT_HM_CUT;
	if (fieldtrav_flag_get(bus, eng, pty->sb1, c->badgeSmash) &&
	    fieldtrav_party_has_move(bus, pty, FT_MOVE_ROCK_SMASH)) m |= 1u << FT_HM_SMASH;
	if (fieldtrav_flag_get(bus, eng, pty->sb1, c->badgeSurf) &&
	    fieldtrav_party_has_move(bus, pty, FT_MOVE_SURF))       m |= 1u << FT_HM_SURF;
	if (fieldtrav_flag_get(bus, eng, pty->sb1, c->badgeWaterfall) &&
	    fieldtrav_party_has_move(bus, pty, FT_MOVE_WATERFALL))  m |= 1u << FT_HM_WATERFALL;
	if (fieldtrav_flag_get(bus, eng, pty->sb1, c->badgeStrength) &&
	    fieldtrav_party_has_move(bus, pty, FT_MOVE_STRENGTH))   m |= 1u << FT_HM_STRENGTH;
	return m;
}

// ============================ edge objects ==================================================
// The blocker for Cut / Rock Smash / Strength is NOT the metatile — those tiles are ordinary
// ground — it is an OBJECT EVENT standing on them, which is why the phase-18 router files them
// in the NPC block list and gives up. graphicsId (+0x05) IS the binding: every such object's
// template carries the matching field-move script (pokeemerald data/maps/*/map.json — checked
// across Route 103/116 and Rusturf Tunnel: every CUTTABLE_TREE carries EventScript_CutTree).
// struct ObjectEvent offsets: +0x00 bit0 active, +0x05 graphicsId, +0x10/+0x12 currentCoords —
// identical in pokeemerald and pokefirered include/global.fieldmap.h (re-read this session), and
// the +0x00/+0x10/+0x12 trio is the one touch.c's read_npcs already walks on hardware.
#define FT_OBJ_STRIDE 0x24u
#define MAP_OFFSET    7      // pokeemerald include/fieldmap.h:18 == pokefirered :21
// Slice 2's ROM-grid adapter forges gBackupMapLayout space, so it needs the same two constants
// fieldpath.c defines privately — kept here with their own citations rather than exported, so
// fieldpath.c stays untouched (its 1808-check suite must keep passing verbatim).
#define MAP_OFFSET_W  15     // MAP_OFFSET*2+1 (fieldpath.c:12)
#define MAP_OFFSET_H  14     // MAP_OFFSET*2   (fieldpath.c:13)
#define MAPGRID_UNDEFINED 0x03FFu   // == MAPGRID_METATILE_ID_MASK (global.fieldmap.h:7,31)

int fieldtrav_scan_edges(const FpBus* bus, const FpMap* m, FtEdge* out) {
	int n = 0;
	if (!bus || !m || !m->mapObjects || !out) return 0;
	const FtEngCfg* c = fieldtrav_cfg(m->engine);
	for (int i = 1; i < 16 && n < FT_MAX_EDGES; i++) {          // slot 0 is the player
		uint32_t e = m->mapObjects + FT_OBJ_STRIDE * (uint32_t)i;
		if (!(bus->read32(bus->ctx, e) & 1u)) continue;         // active:1
		uint8_t gfx = bus->read8(bus->ctx, e + 0x05u);
		uint8_t hm;
		if      (gfx == c->gfxCutTree) hm = FT_HM_CUT;
		else if (gfx == c->gfxRock)    hm = FT_HM_SMASH;
		else if (gfx == c->gfxBoulder) hm = FT_HM_STRENGTH;
		else continue;                                          // a berry tree / NPC / anything else
		// LIVE coords, not the ROM template's: a boulder may have been pushed off its spot
		// already, and the tile it is on NOW is the one that blocks (SPEC H1.3).
		out[n].slot = (int16_t)i;
		out[n].x    = (int16_t)((int)(int16_t)bus->read16(bus->ctx, e + 0x10u) - MAP_OFFSET);
		out[n].y    = (int16_t)((int)(int16_t)bus->read16(bus->ctx, e + 0x12u) - MAP_OFFSET);
		out[n].hm   = hm;
		n++;
	}
	return n;
}

// ============================ the layered planner ===========================================
//
// State = (tile, mode, interactsUsed). Minimising (interacts, steps) LEXICOGRAPHICALLY is the
// whole cost model (SPEC H1.7/H1.8), and it is implemented as iterative deepening: pass K allows
// at most K interacts and BFSes by step count, so the FIRST pass that reaches the goal is by
// construction the fewest-interact answer, and BFS makes it the shortest such route. K starts at
// 0, which is a plain dry walk — so a dry path can never be displaced by a wet one.
#define NT (FP_WBOX * FP_WBOX)
#define NS (NT * FT_MODE_COUNT * (FT_MAX_INTERACTS + 1))

// STATIC, not stack: ~125 KB of working set, and this runs on the render thread deep inside the
// per-frame call chain (the fieldpath precedent, touch.c walk_plan). Single-threaded by
// construction — touch_update is only ever called from the main/render thread (CLAUDE.md #2).
static int16_t s_parent[NS];
static uint8_t s_meta[NS];      // bits 0-1 = dir, bits 2-4 = FtHm of the edge taken INTO this state
static int16_t s_queue[NS];
// Per-tile caches: the behaviour read walks a 5-deep ROM pointer chain, and an uncached search
// would do it ~100 000 times per tap. -2 = not yet computed.
static int16_t s_behCache[NT];
static int8_t  s_footCache[NT];   // -1 unknown, 0 no, 1 yes  (collision+elevation, FOOT mode)
static int8_t  s_surfCache[NT];   // -1 unknown, 0 no, 1 yes  (surfable behaviour + collision 0)

static const int s_dxs[4] = { 1, -1, 0, 0 };   // R, L, D, U — the FP_R/FP_L/FP_D/FP_U order
static const int s_dys[4] = { 0, 0, 1, -1 };

typedef struct {
	const FpBus* bus;
	const FpMap* m;
	int sx, sy;              // window origin (the player's tile)
	int footElev;
	const short (*npc)[2];
	int npcN;
	FtEdge edge[FT_MAX_EDGES];
	int nEdge;
	uint32_t usable;
} FtCtx;

static int tile_idx(const FtCtx* c, int x, int y) {
	int lx = x - c->sx + FP_WHALF, ly = y - c->sy + FP_WHALF;
	if (lx < 0 || lx >= FP_WBOX || ly < 0 || ly >= FP_WBOX) return -1;
	return lx + FP_WBOX * ly;
}

static int beh_at(FtCtx* c, int ti, int x, int y) {
	if (s_behCache[ti] == -2) s_behCache[ti] = (int16_t)fieldpath_behaviour_at(c->bus, c->m, x, y);
	return s_behCache[ti];
}

static bool foot_ok(FtCtx* c, int ti, int x, int y) {
	if (s_footCache[ti] < 0)
		s_footCache[ti] = fieldpath_enterable(c->bus, c->m, x, y, c->footElev) ? 1 : 0;
	return s_footCache[ti] != 0;
}

// Water the player may float on: the behaviour is in this engine's surfable set AND the tile has
// no collision. Elevation is deliberately NOT tested — on water the game's elevation is the water
// layer's own, and testing it against the FOOT elevation is exactly the bug that makes tier 0
// refuse every pond.
static bool surf_ok(FtCtx* c, int ti, int x, int y) {
	if (s_surfCache[ti] < 0) {
		bool ok = fieldtrav_is_surfable(c->m->engine, beh_at(c, ti, x, y)) &&
		          fieldpath_enterable(c->bus, c->m, x, y, 0 /* elevation disarmed */);
		s_surfCache[ti] = ok ? 1 : 0;
	}
	return s_surfCache[ti] != 0;
}

static bool npc_blocks(const FtCtx* c, int x, int y) {
	for (int i = 0; i < c->npcN; i++)
		if (c->npc[i][0] == x + MAP_OFFSET && c->npc[i][1] == y + MAP_OFFSET) return true;
	return false;
}

// The eligible edge object standing on (x,y), or NULL. Strength is excluded on purpose: pushing
// a boulder is a Sokoban problem and a mis-planned push can soft-lock a puzzle until the map is
// re-entered (SPEC §5), so a boulder stays a plain blocker for the router.
static const FtEdge* edge_at(const FtCtx* c, int x, int y) {
	for (int i = 0; i < c->nEdge; i++) {
		if (c->edge[i].x != x || c->edge[i].y != y) continue;
		if (c->edge[i].hm != FT_HM_CUT && c->edge[i].hm != FT_HM_SMASH) return 0;
		if (!(c->usable & (1u << c->edge[i].hm))) return 0;      // no badge / no mon -> plain blocker
		return &c->edge[i];
	}
	return 0;
}

// One directed transition INTO (nx,ny) (tile index nti), taken while in `mode`. Returns the HM
// it costs (FT_HM_NONE = free) or -1 = impossible; `*nmode` receives the mode AFTER the step.
// Only the DESTINATION matters: every rule below is a property of the tile being entered, which is
// also why the source tile is not a parameter.
static int transition(FtCtx* c, int mode, int nx, int ny, int nti, int* nmode) {
	if (mode == FT_MODE_FOOT) {
		if (foot_ok(c, nti, nx, ny)) {
			if (npc_blocks(c, nx, ny)) {
				// The tile is walkable but something stands on it. If that something is an
				// eligible Cut tree / Smash rock, the block is CONDITIONAL, not final.
				const FtEdge* e = edge_at(c, nx, ny);
				if (!e) return -1;
				*nmode = FT_MODE_FOOT;
				return e->hm;
			}
			*nmode = FT_MODE_FOOT;
			return FT_HM_NONE;
		}
		// FOOT -> SURF: the mount. The game's gate (pokeemerald src/field_control_avatar.c:450)
		// is badge + PartyHasMonWithSurf + IsPlayerFacingSurfableFishableWater; we mirror all
		// three (the facing half is what the executor's FACE step provides).
		if ((c->usable & (1u << FT_HM_SURF)) && surf_ok(c, nti, nx, ny)) {
			*nmode = FT_MODE_SURF;
			return FT_HM_SURF;
		}
		return -1;
	}
	// mode == FT_MODE_SURF
	if (surf_ok(c, nti, nx, ny)) {
		if (npc_blocks(c, nx, ny)) return -1;   // a surfing NPC / a rock in the water: not ours
		*nmode = FT_MODE_SURF;
		return FT_HM_NONE;
	}
	// SURF -> FOOT: the dismount. FREE — the game hops the player ashore on the movement itself
	// (no prompt, no cutscene), so this costs no interact.
	if (foot_ok(c, nti, nx, ny) && !npc_blocks(c, nx, ny)) {
		*nmode = FT_MODE_FOOT;
		return FT_HM_NONE;
	}
	return -1;
}

static int state_idx(int ti, int mode, int used) {
	return ((used * FT_MODE_COUNT) + mode) * NT + ti;
}

// One iterative-deepening pass. Returns the goal STATE index reached, or -1.
static int bfs_pass(FtCtx* c, int gx, int gy, int startMode, int maxUsed) {
	int nStates = NT * FT_MODE_COUNT * (maxUsed + 1);
	for (int i = 0; i < nStates; i++) s_parent[i] = -1;

	int sti = tile_idx(c, c->sx, c->sy);
	int gti = tile_idx(c, gx, gy);
	if (sti < 0 || gti < 0) return -1;
	int start = state_idx(sti, startMode, 0);
	int head = 0, tail = 0;
	s_parent[start] = (int16_t)start; s_meta[start] = 0; s_queue[tail++] = (int16_t)start;

	while (head < tail) {
		int cur = s_queue[head++];
		int ti   = cur % NT;
		int rest = cur / NT;
		int mode = rest % FT_MODE_COUNT;
		int used = rest / FT_MODE_COUNT;
		if (ti == gti) return cur;                       // arrival in EITHER mode is arrival (H1.9)
		int lx = ti % FP_WBOX, ly = ti / FP_WBOX;
		int x = c->sx + lx - FP_WHALF, y = c->sy + ly - FP_WHALF;
		for (int d = 0; d < 4; d++) {
			int nlx = lx + s_dxs[d], nly = ly + s_dys[d];
			if (nlx < 0 || nlx >= FP_WBOX || nly < 0 || nly >= FP_WBOX) continue;
			int nti = nlx + FP_WBOX * nly;
			int nx = x + s_dxs[d], ny = y + s_dys[d];
			int nmode = mode;
			int cost = transition(c, mode, nx, ny, nti, &nmode);
			if (cost < 0) continue;
			int nused = used + (cost == FT_HM_NONE ? 0 : 1);
			if (nused > maxUsed) continue;
			int nidx = state_idx(nti, nmode, nused);
			if (s_parent[nidx] != -1) continue;
			s_parent[nidx] = (int16_t)cur;
			s_meta[nidx]   = (uint8_t)((d & 3) | ((cost & 7) << 2));
			s_queue[tail++] = (int16_t)nidx;
		}
	}
	return -1;
}

bool fieldtrav_plan(const FpBus* bus, const FpMap* m, const FtParty* pty,
                    int sx, int sy, int gx, int gy, bool startSurfing,
                    const short (*npc)[2], int npcN, FtProgram* out) {
	memset(out, 0, sizeof *out);
	out->goalX = gx; out->goalY = gy;
	out->outcome = FT_OUT_BADMAP;
	if (!bus || !m || !out) return false;
	if (m->backupW <= 0 || m->backupW > 512 || m->backupH <= 0 || m->backupH > 512 ||
	    (m->gridPtr >> 24) != 0x02u) return false;
	if (abs(gx - sx) > FP_WHALF || abs(gy - sy) > FP_WHALF) { out->outcome = FT_OUT_WINDOW; return false; }

	FtCtx c;
	memset(&c, 0, sizeof c);
	c.bus = bus; c.m = m; c.sx = sx; c.sy = sy; c.npc = npc; c.npcN = npc ? npcN : 0;
	c.usable = fieldtrav_usable(bus, m->engine, pty);
	out->usable = c.usable;

	// The FOOT-mode elevation, with fieldpath_plan's own self-consistency rail: if the tile the
	// player stands on fails our rule the read is wrong (or the avatar is mid-transition) and a
	// confident elevation filter would make the whole map unreachable — disarm it instead.
	// While SURFING the live elevation IS the water layer's, so it says nothing about where the
	// player may come ashore: disarm it there too and let collision alone decide the dismount
	// (the game auto-hops ashore, and the executor re-plans from reality after every interact).
	int pElev = fieldpath_player_elev(bus, m);
	if (startSurfing) pElev = 0;
	else if (pElev != 0 && !fieldpath_enterable(bus, m, sx, sy, pElev)) pElev = 0;
	c.footElev = pElev;

	c.nEdge = fieldtrav_scan_edges(bus, m, c.edge);
	out->nEdges = c.nEdge;

	for (int i = 0; i < NT; i++) { s_behCache[i] = -2; s_footCache[i] = -1; s_surfCache[i] = -1; }

	int startMode = startSurfing ? FT_MODE_SURF : FT_MODE_FOOT;
	out->startMode = startMode;

	int goalState = -1, usedK = 0;
	for (int K = 0; K <= FT_MAX_INTERACTS; K++) {
		goalState = bfs_pass(&c, gx, gy, startMode, K);
		if (goalState >= 0) { usedK = K; break; }
	}
	if (goalState < 0) {
		// Nothing reached it even with every eligible edge. Distinguish the two honest causes so
		// the log says which: no eligible edge existed at all vs. a genuinely unreachable tile.
		out->outcome = (c.usable == 0 && c.nEdge == 0) ? FT_OUT_NOEDGE : FT_OUT_UNREACHABLE;
		return false;
	}
	if (usedK == 0) {
		// A plain walk reaches it. DECLINE: the shipped fieldpath router owns this tap, and a
		// conditional-edge program must never displace a dry route (SPEC H1.7).
		out->outcome = FT_OUT_TIER0;
		return false;
	}

	// --- reconstruct, goal -> start, then reverse ------------------------------------------
	int n = 0, cur = goalState;
	int start = state_idx(tile_idx(&c, sx, sy), startMode, 0);
	static FtMove tmp[FT_MAXMOVES];
	while (cur != start) {
		if (n >= FT_MAXMOVES) { out->outcome = FT_OUT_UNREACHABLE; return false; }
		uint8_t meta = s_meta[cur];
		tmp[n].dir = (int8_t)(meta & 3);
		tmp[n].hm  = (uint8_t)((meta >> 2) & 7);
		tmp[n].objSlot = -1;
		if (tmp[n].hm == FT_HM_CUT || tmp[n].hm == FT_HM_SMASH) {
			int ti = cur % NT;
			int x = sx + (ti % FP_WBOX) - FP_WHALF, y = sy + (ti / FP_WBOX) - FP_WHALF;
			const FtEdge* e = edge_at(&c, x, y);
			tmp[n].objSlot = e ? e->slot : -1;
		}
		n++;
		cur = s_parent[cur];
	}
	out->nMoves = n;
	out->nInteracts = 0;
	for (int i = 0; i < n; i++) {
		out->mv[i] = tmp[n - 1 - i];
		if (out->mv[i].hm != FT_HM_NONE) out->nInteracts++;
	}
	out->endMode = (goalState / NT) % FT_MODE_COUNT;
	out->ok = true;
	out->outcome = FT_OUT_PLANNED;
	return true;
}

// ==============================================================================================
// SLICE 2 — CROSS-MAP EXCURSIONS (SPEC-family-traversal §3)
// ==============================================================================================
// Structure offsets, all re-read from pret this session at the line that uses them:
//   struct MapHeader   pokeemerald include/global.fieldmap.h — /*0x00*/ mapLayout,
//                      /*0x04*/ events (identical block in pokefirered).
//   struct MapEvents   /*0x00*/ objectEventCount, /*0x01*/ warpCount, /*0x08*/ warps.
//   struct WarpEvent   /*0x00*/ s16 x, /*0x02*/ s16 y, /*0x04*/ u8 elevation, /*0x05*/ u8 warpId,
//                      /*0x06*/ u8 mapNum, /*0x07*/ u8 mapGroup  — 8 bytes, the record
//                      fieldpath.c's warp_at already indexes with the same +6/+7 reads.
//   struct MapLayout   /*0x00*/ s32 width, /*0x04*/ s32 height, /*0x08*/ border, /*0x0C*/ map.
//   gMapGroups         `const struct MapHeader *const *const gMapGroups[]` — group index first,
//                      then map number (pret Overworld_GetMapHeaderByGroupAndId, src/overworld.c).

static bool ft_rom_ptr(uint32_t a) { return (a >> 24) == 0x08u || (a >> 24) == 0x09u; }

int fieldtrav_warps(const FpBus* bus, uint32_t events, FtWarp* out, int cap) {
	if (!bus || !out || cap <= 0 || !ft_rom_ptr(events)) return 0;
	int n = (int)bus->read8(bus->ctx, events + 0x01u);            // MapEvents.warpCount
	if (n <= 0 || n > FT_MAX_WARPS) return 0;                     // fieldpath's own sane-count rail
	uint32_t wp = bus->read32(bus->ctx, events + 0x08u);          // MapEvents.warps
	if (!ft_rom_ptr(wp)) return 0;
	if (n > cap) n = cap;
	for (int i = 0; i < n; i++) {
		uint32_t b = wp + 8u * (uint32_t)i;
		out[i].x        = (int16_t)bus->read16(bus->ctx, b + 0u);
		out[i].y        = (int16_t)bus->read16(bus->ctx, b + 2u);
		out[i].elev     = bus->read8(bus->ctx, b + 4u);
		out[i].warpId   = bus->read8(bus->ctx, b + 5u);
		out[i].mapNum   = bus->read8(bus->ctx, b + 6u);
		out[i].mapGroup = bus->read8(bus->ctx, b + 7u);
	}
	return n;
}

bool fieldtrav_rom_map(const FpBus* bus, uint32_t mapGroupsRom, int group, int num, FtRomMap* out) {
	memset(out, 0, sizeof *out);
	// MAP_GROUPS_COUNT is 34 in Emerald and 43 in FRLG; 64/256 are the loose structural caps that
	// keep a garbage warp record from indexing halfway across the ROM. A warp's mapGroup/mapNum
	// are u8, so 255 is the widest a legitimate record can be anyway.
	if (!bus || !out || !ft_rom_ptr(mapGroupsRom) || group < 0 || group > 63 || num < 0 || num > 255)
		return false;
	uint32_t grp = bus->read32(bus->ctx, mapGroupsRom + 4u * (uint32_t)group);
	if (!ft_rom_ptr(grp)) return false;
	uint32_t hdr = bus->read32(bus->ctx, grp + 4u * (uint32_t)num);
	if (!ft_rom_ptr(hdr)) return false;
	uint32_t lay = bus->read32(bus->ctx, hdr + 0x00u);
	uint32_t ev  = bus->read32(bus->ctx, hdr + 0x04u);
	if (!ft_rom_ptr(lay) || !ft_rom_ptr(ev)) return false;
	int32_t w = (int32_t)bus->read32(bus->ctx, lay + 0x00u);
	int32_t h = (int32_t)bus->read32(bus->ctx, lay + 0x04u);
	uint32_t grid = bus->read32(bus->ctx, lay + 0x0Cu);
	// The largest vanilla layout is well under 256x256; anything outside that is a bad pointer,
	// and answering "no excursion" is the whole failure mode this guard exists to produce.
	if (w <= 0 || w > 256 || h <= 0 || h > 256 || !ft_rom_ptr(grid)) return false;
	out->header = hdr; out->layout = lay; out->events = ev; out->grid = grid;
	out->w = (int)w; out->h = (int)h;
	return true;
}

// --- the ROM-grid bus adapter -----------------------------------------------------------------
// fieldpath speaks gBackupMapLayout space: grid_word() reads
// `gridPtr + 2*((x+MAP_OFFSET) + backupW*(y+MAP_OFFSET))` and treats MAPGRID_UNDEFINED as "off
// the map". A ROM layout is a raw w*h array with no border padding, so instead of forking the
// rule (the one change that would let the live and ROM grids silently drift apart) we forge an
// address window and translate inside the bus. fieldpath.c never learns ROM maps exist.
#define FT_ROMGRID_BASE 0x0F000000u   // unmapped on a GBA: no real read can collide with it

static uint8_t ft_rb_read8(void* ctx, uint32_t a) {
	const FtRomBus* rb = (const FtRomBus*)ctx;
	return rb->inner->read8(rb->inner->ctx, a);
}
static uint16_t ft_rb_read16(void* ctx, uint32_t a) {
	const FtRomBus* rb = (const FtRomBus*)ctx;
	if (a >= FT_ROMGRID_BASE) {
		uint32_t off = (a - FT_ROMGRID_BASE) >> 1;
		int bw = rb->rm.w + MAP_OFFSET_W;
		int gx = (int)(off % (uint32_t)bw), gy = (int)(off / (uint32_t)bw);
		int x = gx - MAP_OFFSET, y = gy - MAP_OFFSET;
		if (x < 0 || x >= rb->rm.w || y < 0 || y >= rb->rm.h) return (uint16_t)MAPGRID_UNDEFINED;
		return rb->inner->read16(rb->inner->ctx, rb->rm.grid + 2u * (uint32_t)(x + rb->rm.w * y));
	}
	return rb->inner->read16(rb->inner->ctx, a);
}
static uint32_t ft_rb_read32(void* ctx, uint32_t a) {
	const FtRomBus* rb = (const FtRomBus*)ctx;
	return rb->inner->read32(rb->inner->ctx, a);
}

void fieldtrav_rom_bus(FtRomBus* rb, const FpBus* inner, const FtRomMap* rm, FpEngine eng,
                       FpBus* busOut, FpMap* mapOut) {
	rb->inner = inner; rb->rm = *rm;
	busOut->read8 = ft_rb_read8; busOut->read16 = ft_rb_read16; busOut->read32 = ft_rb_read32;
	busOut->ctx = rb;
	memset(mapOut, 0, sizeof *mapOut);
	mapOut->engine     = eng;
	mapOut->mapHeader  = rm->header;     // behaviour reads walk the REAL tileset chain
	mapOut->mapObjects = 0;              // ROM has no object events — NPCs are invisible here
	mapOut->gridPtr    = FT_ROMGRID_BASE;
	mapOut->backupW    = rm->w + MAP_OFFSET_W;
	mapOut->backupH    = rm->h + MAP_OFFSET_H;
}

// --- plain dry reachability, shared by every leg ----------------------------------------------
// A second, much smaller BFS than fieldtrav_plan's layered one: no modes, no conditional edges,
// just "can a walker get from A to B on this grid, and in how many steps". Used three times per
// candidate (out-leg on the live map, mid-leg on D's ROM grid, back-leg on the live map), so the
// distance field is kept per-call in a static the same way the layered search does.
static int16_t s_dist[NT];
static int16_t s_dq[NT];

static void ft_dry_bfs(const FpBus* bus, const FpMap* m, int ox, int oy, int sx, int sy,
                       int pElev, const short (*npc)[2], int npcN) {
	for (int i = 0; i < NT; i++) s_dist[i] = -1;
	int slx = sx - ox + FP_WHALF, sly = sy - oy + FP_WHALF;
	if (slx < 0 || slx >= FP_WBOX || sly < 0 || sly >= FP_WBOX) return;
	int head = 0, tail = 0;
	s_dist[slx + FP_WBOX * sly] = 0;
	s_dq[tail++] = (int16_t)(slx + FP_WBOX * sly);
	while (head < tail) {
		int ti = s_dq[head++];
		int lx = ti % FP_WBOX, ly = ti / FP_WBOX;
		int x = ox + lx - FP_WHALF, y = oy + ly - FP_WHALF;
		for (int d = 0; d < 4; d++) {
			int nlx = lx + s_dxs[d], nly = ly + s_dys[d];
			if (nlx < 0 || nlx >= FP_WBOX || nly < 0 || nly >= FP_WBOX) continue;
			int nti = nlx + FP_WBOX * nly;
			if (s_dist[nti] >= 0) continue;
			int nx = x + s_dxs[d], ny = y + s_dys[d];
			if (!fieldpath_enterable(bus, m, nx, ny, pElev)) continue;
			bool blocked = false;                       // live NPCs (empty on a ROM map)
			for (int i = 0; i < npcN; i++)
				if (npc[i][0] == (short)(nx + MAP_OFFSET) && npc[i][1] == (short)(ny + MAP_OFFSET))
					{ blocked = true; break; }
			if (blocked) continue;
			s_dist[nti] = (int16_t)(s_dist[ti] + 1);
			s_dq[tail++] = (int16_t)nti;
		}
	}
}

static int ft_dist_at(int ox, int oy, int x, int y) {
	int lx = x - ox + FP_WHALF, ly = y - oy + FP_WHALF;
	if (lx < 0 || lx >= FP_WBOX || ly < 0 || ly >= FP_WBOX) return -1;
	return s_dist[lx + FP_WBOX * ly];
}

// The tile a walker must actually STAND on to use a warp — fieldpath's own terminal semantics,
// reused verbatim so an excursion leg is byte-for-byte the route the shipped router already
// drives: a DOOR is entered from the tile SOUTH of it, everything else is stood on.
static void ft_warp_approach(const FpBus* bus, const FpMap* m, int wx, int wy, int* ax, int* ay) {
	FpClass cl = fieldpath_classify(bus, m, wx, wy);
	*ax = cl.approachX; *ay = cl.approachY;
}

// The tile the game actually leaves the player on after a warp. A destination warp usually sits
// ON the door metatile (collision 1, the graphic), and the engine walks the avatar one step out;
// when the tile itself is walkable (mats, arrow warps, stairs) that is where you stand.
static void ft_arrival_tile(const FpBus* bus, const FpMap* m, int wx, int wy, int* ax, int* ay) {
	*ax = wx; *ay = wy;
	if (!fieldpath_enterable(bus, m, wx, wy, 0)) *ay = wy + 1;   // step out of the doorway
}

bool fieldtrav_excursion(const FpBus* bus, const FpMap* m, uint32_t mapGroupsRom,
                         int curGrp, int curNum, int sx, int sy, int gx, int gy,
                         const short (*npc)[2], int npcN, FtExcursion* out) {
	memset(out, 0, sizeof *out);
	out->outcome = FT_OUT_BADMAP;
	out->wi = out->wj = -1;
	if (!bus || !m || !out) return false;
	// PHASE 24 (lane A, live-proven): `mapHeader` is gMapHeader — the STRUCT the engine keeps in
	// EWRAM (0x02...), not a ROM pointer. Requiring ft_rom_ptr here was a category error that made
	// EVERY excursion attempt on real hardware return BADMAP before it read a single warp; the host
	// suite could not see it because its fixture handed the planner the ROM header directly. The
	// rule is fieldpath's own (fieldpath.c:104/160): a non-zero header is enough, and what actually
	// has to be ROM — the events pointer read OUT of it, the group table, every layout pointer — is
	// still checked at each read (fieldtrav_warps / fieldtrav_rom_map).
	if (!ft_rom_ptr(mapGroupsRom) || !m->mapHeader) return false;
	if (m->backupW <= 0 || m->backupW > 512 || m->backupH <= 0 || m->backupH > 512) return false;
	if (abs(gx - sx) > FP_WHALF || abs(gy - sy) > FP_WHALF) { out->outcome = FT_OUT_WINDOW; return false; }

	FtWarp here[FT_MAX_WARPS];
	uint32_t evHere = bus->read32(bus->ctx, m->mapHeader + 0x04u);
	int nHere = fieldtrav_warps(bus, evHere, here, FT_MAX_WARPS);
	if (nHere <= 0) { out->outcome = FT_OUT_NOEXC; return false; }

	// The FOOT elevation, with fieldpath_plan's self-consistency rail (a read that says the tile
	// the player is standing on is not enterable is a wrong read, not a stuck player).
	int pElev = fieldpath_player_elev(bus, m);
	if (pElev != 0 && !fieldpath_enterable(bus, m, sx, sy, pElev)) pElev = 0;

	// Leg 1 distance field: everything reachable on foot from the player, on the LIVE map.
	ft_dry_bfs(bus, m, sx, sy, sx, sy, pElev, npc, npcN);
	// Snapshot it — the mid/back BFS passes reuse the same static array.
	static int16_t outLeg[NT];
	memcpy(outLeg, s_dist, sizeof outLeg);

	// Rank candidate exits nearest-first and take at most FT_EXC_CAND of them (SPEC H3.2).
	int cand[FT_MAX_WARPS], nCand = 0;
	for (int i = 0; i < nHere; i++) {
		int ax, ay; ft_warp_approach(bus, m, here[i].x, here[i].y, &ax, &ay);
		int lx = ax - sx + FP_WHALF, ly = ay - sy + FP_WHALF;
		if (lx < 0 || lx >= FP_WBOX || ly < 0 || ly >= FP_WBOX) continue;
		if (outLeg[lx + FP_WBOX * ly] < 0) continue;             // not reachable on foot
		cand[nCand++] = i;
	}
	for (int a = 0; a < nCand; a++)                              // insertion sort by out-leg steps
		for (int b = a + 1; b < nCand; b++) {
			int aa, ab, ba, bb;
			ft_warp_approach(bus, m, here[cand[a]].x, here[cand[a]].y, &aa, &ab);
			ft_warp_approach(bus, m, here[cand[b]].x, here[cand[b]].y, &ba, &bb);
			int da = outLeg[(aa - sx + FP_WHALF) + FP_WBOX * (ab - sy + FP_WHALF)];
			int db = outLeg[(ba - sx + FP_WHALF) + FP_WBOX * (bb - sy + FP_WHALF)];
			if (db < da) { int t = cand[a]; cand[a] = cand[b]; cand[b] = t; }
		}
	if (nCand > FT_EXC_CAND) nCand = FT_EXC_CAND;

	int bestScore = 0x7FFFFFFF;
	for (int ci = 0; ci < nCand; ci++) {
		const FtWarp* wi = &here[cand[ci]];
		if (wi->mapGroup == curGrp && wi->mapNum == curNum) continue;   // same-map warp: not an excursion
		FtRomMap rm;
		if (!fieldtrav_rom_map(bus, mapGroupsRom, wi->mapGroup, wi->mapNum, &rm)) continue;
		FtWarp there[FT_MAX_WARPS];
		int nThere = fieldtrav_warps(bus, rm.events, there, FT_MAX_WARPS);
		if (nThere <= 0 || wi->warpId >= nThere) continue;

		FtRomBus rb; FpBus dbus; FpMap dmap;
		fieldtrav_rom_bus(&rb, bus, &rm, m->engine, &dbus, &dmap);

		int arrX, arrY;
		ft_arrival_tile(&dbus, &dmap, there[wi->warpId].x, there[wi->warpId].y, &arrX, &arrY);
		// Inside D the elevation rule is disarmed (0 = ELEVATION_TRANSITION, "compatible with
		// anything"): the arrival elevation is whatever the doorway carries, and an interior is
		// exactly where a confident-but-wrong elevation would make the whole map unreachable.
		ft_dry_bfs(&dbus, &dmap, arrX, arrY, arrX, arrY, 0, 0, 0);
		// SNAPSHOT it. The leg-3 BFS below runs INSIDE the j loop and reuses the same static
		// distance field, so a second return-warp candidate would otherwise be measured against
		// the CURRENT map's distances instead of the interior's — a silent mis-ranking that only
		// shows up on maps with more than one way back.
		static int16_t midLeg[NT];
		memcpy(midLeg, s_dist, sizeof midLeg);

		for (int j = 0; j < nThere; j++) {
			const FtWarp* wj = &there[j];
			if (wj->mapGroup != curGrp || wj->mapNum != curNum) continue;
			if (wj->warpId >= nHere) continue;
			int back = wj->warpId;
			if (back == cand[ci]) continue;                       // returns to the door we left by
			int jx, jy; ft_warp_approach(&dbus, &dmap, wj->x, wj->y, &jx, &jy);
			int jlx = jx - arrX + FP_WHALF, jly = jy - arrY + FP_WHALF;
			if (jlx < 0 || jlx >= FP_WBOX || jly < 0 || jly >= FP_WBOX) continue;
			int dMid = midLeg[jlx + FP_WBOX * jly];
			if (dMid < 0) continue;                               // the interior does not connect

			int bx, by;
			ft_arrival_tile(bus, m, here[back].x, here[back].y, &bx, &by);
			if (bx == sx && by == sy) continue;                   // lands where we already are
			// Leg 3 on the LIVE map, from the return tile to the goal.
			ft_dry_bfs(bus, m, sx, sy, bx, by, 0, npc, npcN);
			int dBack = ft_dist_at(sx, sy, gx, gy);
			if (dBack < 0) continue;

			int ax, ay; ft_warp_approach(bus, m, wi->x, wi->y, &ax, &ay);
			int dOut = outLeg[(ax - sx + FP_WHALF) + FP_WBOX * (ay - sy + FP_WHALF)];
			int score = dOut + dMid + dBack;
			if (score >= bestScore) continue;
			bestScore   = score;
			out->wi     = cand[ci];
			out->wiX    = wi->x;  out->wiY = wi->y;
			out->dGroup = wi->mapGroup; out->dNum = wi->mapNum;
			out->arrX   = arrX;   out->arrY = arrY;
			out->wj     = j;
			out->wjX    = wj->x;  out->wjY = wj->y;
			out->backX  = bx;     out->backY = by;
			out->stepsOut = dOut; out->stepsMid = dMid; out->stepsBack = dBack;
		}
	}

	if (out->wi < 0) { out->outcome = FT_OUT_NOEXC; return false; }
	out->ok = true;
	out->outcome = FT_OUT_PLANNED;
	return true;
}
