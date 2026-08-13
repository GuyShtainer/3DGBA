// fieldpath.c — see fieldpath.h. PURE C (CLAUDE.md #4): no libctru, no citro, no mGBA.
//
// Every constant below was read out of pret this phase, per engine, and is cited at the line
// that uses it. Nothing here is derived from the other engine's numbering: the RSE and FRLG
// 0x60-0x71 blocks disagree (RSE 0x6C = MB_WATER_DOOR, a step warp; FRLG 0x6C =
// MB_UP_RIGHT_STAIR_WARP, hold EAST), so the tables are separate on purpose.
#include <stdlib.h>   // abs
#include <string.h>   // memset
#include "fieldpath.h"

#define MAP_OFFSET   7    // pokeemerald include/fieldmap.h:18 == pokefirered :21
#define MAP_OFFSET_W 15   // MAP_OFFSET*2+1
#define MAP_OFFSET_H 14   // MAP_OFFSET*2
#define MAPGRID_UNDEFINED 0x03FFu   // == MAPGRID_METATILE_ID_MASK (global.fieldmap.h:7,14/31)
#define ELEV_TRANSITION   0         // "compatible with anything"
#define ELEV_MULTI_LEVEL  15        // bridges

// ============================ behaviour -> warp kind =====================================
// RSE (pokeemerald include/constants/metatile_behaviors.h; the enum's own MB_UNUSED_xx names
// pin the numbering). Which behaviours warp AT ALL is IsWarpMetatileBehavior /
// IsArrowWarpMetatileBehavior in src/field_control_avatar.c — a warp EVENT on a tile whose
// behaviour is not in one of those sets never triggers (measured: 94 Emerald and 239 FireRed
// warp events sit on MB_NORMAL — they are arrival destinations, not triggers), which is why
// the BEHAVIOUR is the classifier and the warp list is only a confirmation.
static FpKind kind_rse(int b, int* dir) {
	*dir = FP_NODIR;
	switch (b) {
	// --- IsWarpDoor -> TryDoorWarp, gated `direction == DIR_NORTH` -> bump from the SOUTH
	case 0x69: *dir = FP_U; return FP_WK_DOOR;              // MB_ANIMATED_DOOR
	// --- IsArrowWarpMetatileBehavior: stand on the tile, hold the matching direction
	case 0x1B: *dir = FP_U; return FP_WK_DIR;               // MB_STAIRS_OUTSIDE_ABANDONED_SHIP (IsNorthArrowWarp)
	case 0x64: *dir = FP_U; return FP_WK_DIR;               // MB_NORTH_ARROW_WARP
	case 0x1C: *dir = FP_D; return FP_WK_DIR;               // MB_SHOAL_CAVE_ENTRANCE (IsSouthArrowWarp)
	case 0x65: *dir = FP_D; return FP_WK_DIR;               // MB_SOUTH_ARROW_WARP  <- the building exit mat
	case 0x6D: *dir = FP_D; return FP_WK_DIR;               // MB_WATER_SOUTH_ARROW_WARP
	case 0x62: *dir = FP_R; return FP_WK_DIR;               // MB_EAST_ARROW_WARP
	case 0x63: *dir = FP_L; return FP_WK_DIR;               // MB_WEST_ARROW_WARP
	// --- IsWarpMetatileBehavior, non-door: TryStartWarpEventScript on tookStep
	case 0x0E: return FP_WK_STEP;                            // MB_MOSSDEEP_GYM_WARP
	case 0x0F: return FP_WK_STEP;                            // MB_MT_PYRE_HOLE
	case 0x29: return FP_WK_STEP;                            // MB_LAVARIDGE_GYM_B1F_WARP
	case 0x60: return FP_WK_STEP;                            // MB_NON_ANIMATED_DOOR  (IsNonAnimDoor)
	case 0x61: return FP_WK_STEP;                            // MB_LADDER
	case 0x67: return FP_WK_STEP;                            // MB_AQUA_HIDEOUT_WARP
	case 0x68: return FP_WK_STEP;                            // MB_LAVARIDGE_GYM_1F_WARP
	case 0x6A: return FP_WK_STEP;                            // MB_UP_ESCALATOR
	case 0x6B: return FP_WK_STEP;                            // MB_DOWN_ESCALATOR
	case 0x6C: return FP_WK_STEP;                            // MB_WATER_DOOR        (IsNonAnimDoor)
	case 0x6E: return FP_WK_STEP;                            // MB_DEEP_SOUTH_WARP   (IsNonAnimDoor)
	case 0x70: return FP_WK_STEP;                            // MB_BRIDGE_OVER_OCEAN (IsUnionRoomWarp in RSE)
	default:   return FP_WK_NONE;
	}
	// Deliberately NOT warps in RSE: 0x66 MB_CRACKED_FLOOR_HOLE (a script hole, absent from
	// IsWarpMetatileBehavior), 0x71 MB_BRIDGE_OVER_POND_LOW, and every script door
	// (MB_PETALBURG_GYM_DOOR 0x8D, MB_SKY_PILLAR_CLOSED_DOOR 0xEA...).
}

// FRLG (pokefirered include/constants/metatile_behaviors.h — plain #defines).
static FpKind kind_frlg(int b, int* dir) {
	*dir = FP_NODIR;
	switch (b) {
	case 0x69: *dir = FP_U; return FP_WK_DOOR;               // MB_WARP_DOOR
	case 0x64: *dir = FP_U; return FP_WK_DIR;                // MB_NORTH_ARROW_WARP
	case 0x65: *dir = FP_D; return FP_WK_DIR;                // MB_SOUTH_ARROW_WARP
	case 0x62: *dir = FP_R; return FP_WK_DIR;                // MB_EAST_ARROW_WARP
	case 0x63: *dir = FP_L; return FP_WK_DIR;                // MB_WEST_ARROW_WARP
	// FRLG-only: IsDirectionalStairWarpMetatileBehavior (src/field_control_avatar.c) —
	// DIR_EAST matches the two *_RIGHT_STAIR_WARPs, DIR_WEST the two *_LEFT_ ones.
	case 0x6C: *dir = FP_R; return FP_WK_DIR;                // MB_UP_RIGHT_STAIR_WARP
	case 0x6E: *dir = FP_R; return FP_WK_DIR;                // MB_DOWN_RIGHT_STAIR_WARP
	case 0x6D: *dir = FP_L; return FP_WK_DIR;                // MB_UP_LEFT_STAIR_WARP
	case 0x6F: *dir = FP_L; return FP_WK_DIR;                // MB_DOWN_LEFT_STAIR_WARP
	case 0x60: return FP_WK_STEP;                            // MB_CAVE_DOOR   (IsNonAnimDoor)
	case 0x61: return FP_WK_STEP;                            // MB_LADDER
	case 0x66: return FP_WK_STEP;                            // MB_FALL_WARP   (IsFallWarp)
	case 0x67: return FP_WK_STEP;                            // MB_REGULAR_WARP (warp pad)
	case 0x68: return FP_WK_STEP;                            // MB_LAVARIDGE_1F_WARP
	case 0x6A: return FP_WK_STEP;                            // MB_UP_ESCALATOR
	case 0x6B: return FP_WK_STEP;                            // MB_DOWN_ESCALATOR
	case 0x71: return FP_WK_STEP;                            // MB_UNION_ROOM_WARP
	default:   return FP_WK_NONE;
	}
}

FpKind fieldpath_kind_for_behaviour(FpEngine eng, int behaviour, int* termDir) {
	int d = FP_NODIR;
	FpKind k = (behaviour < 0) ? FP_WK_NONE
	         : (eng == FP_ENG_FRLG ? kind_frlg(behaviour, &d) : kind_rse(behaviour, &d));
	if (termDir) *termDir = d;
	return k;
}

// ================================ tile reads =============================================
static bool rom_ptr(uint32_t a) { return (a >> 24) == 0x08u || (a >> 24) == 0x09u; }

// The raw gBackupMapLayout word for a MAP-LOCAL tile, or MAPGRID_UNDEFINED off the grid.
static uint16_t grid_word(const FpBus* bus, const FpMap* m, int x, int y) {
	int gx = x + MAP_OFFSET, gy = y + MAP_OFFSET;
	if (gx < 0 || gx >= m->backupW || gy < 0 || gy >= m->backupH) return (uint16_t)MAPGRID_UNDEFINED;
	return bus->read16(bus->ctx, m->gridPtr + 2u * (uint32_t)(gx + m->backupW * gy));
}

int fieldpath_behaviour_at(const FpBus* bus, const FpMap* m, int x, int y) {
	if (!m->mapHeader) return -1;                 // no verified gMapHeader for this game
	// Outside the CURRENT map's own bounds the tile came from a map CONNECTION, and its
	// metatile id indexes this map's tilesets, not the neighbour's — pret has the identical
	// limitation, so refuse rather than answer confidently wrong (SPEC-door T5.9).
	int mw = m->backupW - MAP_OFFSET_W, mh = m->backupH - MAP_OFFSET_H;
	if (x < 0 || x >= mw || y < 0 || y >= mh) return -1;

	uint16_t blk = grid_word(bus, m, x, y);
	uint32_t mid = blk & 0x03FFu;
	if (mid == MAPGRID_UNDEFINED || mid >= 1024u) return -1;

	uint32_t layout = bus->read32(bus->ctx, m->mapHeader + 0x00u);   // MapHeader.mapLayout
	if (!rom_ptr(layout)) return -1;
	// NUM_METATILES_IN_PRIMARY: 512 in RSE (fieldmap.h:6), 640 in FRLG (fieldmap.h:8).
	uint32_t nprim = (m->engine == FP_ENG_FRLG) ? 640u : 512u;
	uint32_t tsOff = (mid < nprim) ? 0x10u : 0x14u;                  // primary / secondary Tileset*
	uint32_t idx   = (mid < nprim) ? mid : mid - nprim;
	uint32_t ts = bus->read32(bus->ctx, layout + tsOff);
	if (!rom_ptr(ts)) return -1;
	// struct Tileset: RSE keeps metatileAttributes at +0x10 (const u16*), FRLG at +0x14
	// (const u32*, +0x10 being the callback) — global.fieldmap.h in each repo.
	uint32_t attrs = bus->read32(bus->ctx, ts + ((m->engine == FP_ENG_FRLG) ? 0x14u : 0x10u));
	if (!rom_ptr(attrs)) return -1;
	// Behaviour mask: RSE METATILE_ATTR_BEHAVIOR_MASK 0x00FF (global.fieldmap.h:39);
	// FRLG sMetatileAttrMasks[METATILE_ATTRIBUTE_BEHAVIOR] 0x000001FF (src/fieldmap.c:64).
	if (m->engine == FP_ENG_FRLG)
		return (int)(bus->read32(bus->ctx, attrs + 4u * idx) & 0x1FFu);
	return (int)(bus->read16(bus->ctx, attrs + 2u * idx) & 0x00FFu);
}

int fieldpath_player_elev(const FpBus* bus, const FpMap* m) {
	if (!m->mapObjects) return 0;
	// struct ObjectEvent +0x0B: u8 currentElevation:4 / previousElevation:4 — the same offset
	// in pokeemerald and pokefirered include/global.fieldmap.h. Slot 0 is the player.
	return (int)(bus->read8(bus->ctx, m->mapObjects + 0x0Bu) & 0x0Fu);
}

bool fieldpath_enterable(const FpBus* bus, const FpMap* m, int x, int y, int pElev) {
	uint16_t blk = grid_word(bus, m, x, y);
	if (blk == (uint16_t)MAPGRID_UNDEFINED) return false;
	if (((blk & 0x0C00u) >> 10) != 0) return false;            // MAPGRID_COLLISION_MASK
	// pret IsElevationMismatchAt (pokeemerald src/event_object_movement.c): a mismatch blocks
	// the step unless either side is ELEVATION_TRANSITION or the tile is ELEVATION_MULTI_LEVEL.
	// This is what keeps the router out of the water: Gen-3 water metatiles are COLLISION 0 and
	// are impassable to a walker purely by elevation (water 1 vs land 3).
	if (pElev == ELEV_TRANSITION) return true;
	int tElev = (blk & 0xF000u) >> 12;
	if (tElev == ELEV_TRANSITION || tElev == ELEV_MULTI_LEVEL) return true;
	return tElev == pElev;
}

// The confirming warp event, if the map has one on this tile. Confirmation ONLY: it may
// downgrade a classification to FP_WK_NONE, never promote one (a warp event on an MB_NORMAL
// tile is an arrival destination, not a trigger).
static bool warp_at(const FpBus* bus, const FpMap* m, int x, int y, int* grp, int* num) {
	*grp = *num = -1;
	if (!m->mapHeader) return false;
	uint32_t ev = bus->read32(bus->ctx, m->mapHeader + 0x04u);       // MapHeader.events
	if (!rom_ptr(ev)) return false;
	int n = (int)bus->read8(bus->ctx, ev + 0x01u);                   // MapEvents.warpCount
	if (n <= 0 || n > 64) return false;                              // sane-count guard
	uint32_t wp = bus->read32(bus->ctx, ev + 0x08u);                 // MapEvents.warps
	if (!rom_ptr(wp)) return false;
	for (int i = 0; i < n; i++) {                                    // struct WarpEvent, 8 bytes
		uint32_t b = wp + 8u * (uint32_t)i;
		int wx = (int)(int16_t)bus->read16(bus->ctx, b + 0u);
		int wy = (int)(int16_t)bus->read16(bus->ctx, b + 2u);
		if (wx != x || wy != y) continue;
		*num = (int)bus->read8(bus->ctx, b + 6u);
		*grp = (int)bus->read8(bus->ctx, b + 7u);
		return true;
	}
	return false;
}

FpClass fieldpath_classify(const FpBus* bus, const FpMap* m, int tapX, int tapY) {
	FpClass c;
	memset(&c, 0, sizeof c);
	c.kind = FP_WK_NONE; c.termDir = FP_NODIR;
	c.goalX = c.approachX = tapX; c.goalY = c.approachY = tapY;
	c.behaviour = -1; c.warpGroup = c.warpNum = -1;
	c.block = grid_word(bus, m, tapX, tapY);

	int gx = tapX, gy = tapY;
	int beh = fieldpath_behaviour_at(bus, m, gx, gy);
	int tapBeh = beh;                 // what the TAPPED tile reads, for the diagnostics row
	int dir = FP_NODIR;
	FpKind k = fieldpath_kind_for_behaviour(m->engine, beh, &dir);
	bool head = false;

	// A Gen-3 door graphic is TWO metatiles tall and only the BOTTOM one carries the warp; the
	// top one is ordinary wall. Tapping the visible top half of a door is the natural gesture
	// and today does nothing at all, so when the tap lands on an IMPASSABLE non-warp tile whose
	// SOUTH neighbour is a real door, take the door. Strictly an improvement: the alternative
	// outcome for that tile is "walk at a wall, stall, press A".
	if (k == FP_WK_NONE && ((c.block & 0x0C00u) >> 10) != 0) {
		int beh2 = fieldpath_behaviour_at(bus, m, gx, gy + 1);
		int dir2 = FP_NODIR;
		if (fieldpath_kind_for_behaviour(m->engine, beh2, &dir2) == FP_WK_DOOR) {
			gy += 1; beh = beh2; dir = dir2; k = FP_WK_DOOR; head = true;
		}
	}
	c.behaviour = beh;
	if (k == FP_WK_NONE) return c;   // untouched: goal/approach stay on the tapped tile

	// Confirmation (never promotion): a door-looking tile with no warp EVENT is scenery, and a
	// warp event on a non-warp behaviour is an arrival destination, not a trigger (T3.4).
	int wg, wn;
	if (!warp_at(bus, m, gx, gy, &wg, &wn)) {
		// Not a warp after all. Report the TAPPED tile's own behaviour, not the neighbour we
		// speculatively looked at — a log row saying "beh=0x69, kind=none" would send the next
		// hardware investigation down the wrong path.
		c.behaviour = tapBeh;
		return c;                                       // stays FP_WK_NONE on the ORIGINAL tap
	}

	c.kind = k; c.termDir = dir; c.headRetarget = head;
	c.warpGroup = wg; c.warpNum = wn; c.block = grid_word(bus, m, gx, gy);
	c.goalX = gx; c.goalY = gy;
	c.approachX = gx;
	c.approachY = (k == FP_WK_DOOR) ? gy + 1 : gy;   // a door is entered from the SOUTH
	return c;
}

// ================================== the search ===========================================
// BFS over the live grid, unchanged in shape from the router's original (same FIFO, same
// {R,L,D,U} child order — with the arrival now constrained the tie-break no longer decides
// anything that matters, and changing it would perturb every route that works today).
static int16_t s_parent[FP_WBOX * FP_WBOX];
static int8_t  s_dirOf [FP_WBOX * FP_WBOX];
static int16_t s_queue [FP_WBOX * FP_WBOX];
static int8_t  s_tmp   [FP_WBOX * FP_WBOX];

static bool blocked_by_npc(const short (*npc)[2], int npcN, int x, int y) {
	for (int i = 0; i < npcN; i++)
		if (npc[i][0] == x + MAP_OFFSET && npc[i][1] == y + MAP_OFFSET) return true;
	return false;
}

// goalOpen: the goal tile may be impassable and still be the terminal (the legacy FP_WK_NONE
// rule — a sign, an NPC, a cuttable tree). For every classified warp the terminal must be a
// tile we can really stand on, so goalOpen is false and an unstandable terminal FAILS.
static bool bfs(const FpBus* bus, const FpMap* m, int sx, int sy, int tx, int ty,
                const short (*npc)[2], int npcN, int pElev, bool goalOpen, FpPlan* out) {
	static const int dxs[4] = { 1, -1, 0, 0 }, dys[4] = { 0, 0, 1, -1 };   // R, L, D, U
	for (int i = 0; i < FP_WBOX * FP_WBOX; i++) s_parent[i] = -1;
	int start = FP_WHALF + FP_WBOX * FP_WHALF;
	int goal  = (tx - sx + FP_WHALF) + FP_WBOX * (ty - sy + FP_WHALF);
	int head = 0, tail = 0;
	s_parent[start] = (int16_t)start; s_queue[tail++] = (int16_t)start;
	bool found = false;
	while (head < tail) {
		int cur = s_queue[head++];
		if (cur == goal) { found = true; break; }
		int clx = cur % FP_WBOX, cly = cur / FP_WBOX;
		for (int d = 0; d < 4; d++) {
			int nlx = clx + dxs[d], nly = cly + dys[d];
			if (nlx < 0 || nlx >= FP_WBOX || nly < 0 || nly >= FP_WBOX) continue;
			int nidx = nlx + FP_WBOX * nly;
			if (s_parent[nidx] != -1) continue;
			int wx = sx + nlx - FP_WHALF, wy = sy + nly - FP_WHALF;
			if (!(nidx == goal && goalOpen)) {
				if (!fieldpath_enterable(bus, m, wx, wy, pElev)) continue;
				if (blocked_by_npc(npc, npcN, wx, wy)) continue;
			}
			s_parent[nidx] = (int16_t)cur; s_dirOf[nidx] = (int8_t)d; s_queue[tail++] = (int16_t)nidx;
		}
	}
	if (!found) return false;
	int n = 0, cur = goal;
	while (cur != start) {
		s_tmp[n++] = s_dirOf[cur];
		cur = s_parent[cur];
		if (n >= FP_WBOX * FP_WBOX) return false;
	}
	out->pathLen = n;
	for (int i = 0; i < n; i++) out->path[i] = s_tmp[n - 1 - i];   // start -> goal order
	return true;
}

bool fieldpath_plan(const FpBus* bus, const FpMap* m, int sx, int sy, int gx, int gy,
                    const short (*npc)[2], int npcN, FpPlan* out) {
	memset(out, 0, sizeof *out);
	out->kind = FP_WK_NONE; out->termDir = FP_NODIR;
	out->goalX = gx; out->goalY = gy; out->approachX = gx; out->approachY = gy;
	out->behaviour = -1; out->warpGroup = out->warpNum = -1;
	if (m->backupW <= 0 || m->backupW > 512 || m->backupH <= 0 || m->backupH > 512 ||
	    (m->gridPtr >> 24) != 0x02u) { out->outcome = FP_OUT_BADMAP; return false; }

	FpClass c = fieldpath_classify(bus, m, gx, gy);
	out->kind = c.kind; out->termDir = c.termDir; out->behaviour = c.behaviour;
	out->warpGroup = c.warpGroup; out->warpNum = c.warpNum; out->headRetarget = c.headRetarget;
	out->goalX = c.goalX; out->goalY = c.goalY;
	out->approachX = c.approachX; out->approachY = c.approachY;

	// Player elevation, with the self-consistency rail: if the tile the player is standing on
	// fails our own rule, the read is wrong (or the avatar is mid-transition) and a confident
	// elevation filter would make the whole map unreachable — so disarm it for this plan.
	int pElev = fieldpath_player_elev(bus, m);
	if (pElev != 0 && !fieldpath_enterable(bus, m, sx, sy, pElev)) pElev = 0;
	out->pElev = pElev;

	if (abs(out->approachX - sx) > FP_WHALF || abs(out->approachY - sy) > FP_WHALF) {
		out->outcome = FP_OUT_WINDOW; return false;
	}

	if (c.kind == FP_WK_DOOR) {
		// A door whose south tile is not standable cannot be entered at all. Emit NOTHING —
		// a documented no-op beats walking into the wall beside it (which is the phase-17 bug).
		if (!fieldpath_enterable(bus, m, c.approachX, c.approachY, pElev) ||
		    blocked_by_npc(npc, npcN, c.approachX, c.approachY)) {
			out->outcome = FP_OUT_NO_APPROACH; return false;
		}
	} else if (c.kind == FP_WK_DIR || c.kind == FP_WK_STEP) {
		// Our table says warp, the live grid says you cannot stand there: a contradiction must
		// not produce a confident wrong move. Fall back to the legacy blocked-goal terminal.
		if (!fieldpath_enterable(bus, m, c.approachX, c.approachY, pElev)) {
			out->kind = FP_WK_NONE; out->termDir = FP_NODIR; c.kind = FP_WK_NONE;
		}
	}

	bool goalOpen = (out->kind == FP_WK_NONE);   // legacy: a blocked goal is a legal terminal
	if (sx == out->approachX && sy == out->approachY) {
		out->pathLen = 0;                        // already there: the plan is the terminal hold
		out->ok = true; out->outcome = FP_OUT_PLANNED;
		return true;
	}
	if (!bfs(bus, m, sx, sy, out->approachX, out->approachY, npc, npcN, pElev, goalOpen, out)) {
		out->outcome = FP_OUT_UNREACHABLE; return false;
	}
	out->ok = true; out->outcome = FP_OUT_PLANNED;
	return true;
}
