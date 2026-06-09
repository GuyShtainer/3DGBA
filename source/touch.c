// touch.c — see touch.h.
#include <stdlib.h>     // abs
#include <citro2d.h>
#include "gbacore.h"    // GBAKEY_*
#include "touch.h"

const char* const TOUCH_NAMES[3] = { "Off", "Gamepad", "Smart" };

#define COL_FAINT  C2D_Color32(0xF5, 0xD0, 0x42, 0x40)   // faint gold (idle zone)
#define COL_LIT    C2D_Color32(0xF5, 0xD0, 0x42, 0xB0)   // bright gold (pressed)

// =============================== PAD (virtual gamepad) ======================
static u16 pad_keys(int px, int py) {
	if (px < 112 && py > 118) {                          // D-pad cross, bottom-left
		int dx = px - 55, dy = py - 180;
		if (dx > -24 && dx < 24 && dy < -16) return 1 << GBAKEY_UP;
		if (dx > -24 && dx < 24 && dy >  16) return 1 << GBAKEY_DOWN;
		if (dy > -24 && dy < 24 && dx < -16) return 1 << GBAKEY_LEFT;
		if (dy > -24 && dy < 24 && dx >  16) return 1 << GBAKEY_RIGHT;
		return 0;
	}
	if (px > 252 && py > 150) return 1 << GBAKEY_A;
	if (px > 198 && px <= 252 && py > 176) return 1 << GBAKEY_B;
	if (px > 128 && px < 192 && py > 214) return 1 << GBAKEY_START;
	if (py < 28 && px < 56)  return 1 << GBAKEY_L;
	if (py < 28 && px > 264) return 1 << GBAKEY_R;
	return 0;
}

static void pad_overlay(u16 held) {
	#define ZONE(x,y,w,h,k) C2D_DrawRectSolid((x),(y),0.0f,(w),(h),(held & (1<<(k))) ? COL_LIT : COL_FAINT)
	ZONE(33, 138, 44, 30, GBAKEY_UP);   ZONE(33, 192, 44, 30, GBAKEY_DOWN);
	ZONE(7, 162, 32, 36, GBAKEY_LEFT);  ZONE(71, 162, 32, 36, GBAKEY_RIGHT);
	ZONE(252, 150, 60, 60, GBAKEY_A);   ZONE(198, 176, 50, 44, GBAKEY_B);
	ZONE(128, 214, 64, 22, GBAKEY_START);
	ZONE(4, 4, 52, 22, GBAKEY_L);       ZONE(264, 4, 52, 22, GBAKEY_R);
	#undef ZONE
}

// ============= SMART: deterministic "write cursor, pulse A" select ===========
// Shared by battle action/move, party, and target select: write the game's own cursor to RAM, then
// inject A on the next frame (the worker runs after this, so the write lands first). tick0 writes
// only (A is a fresh press); tick1 writes + A, then clears before the menu can change.
static int s_target = -1, s_selTick = 0;       // battle action/move cell (0..3)
static void battle_reset(void) { s_target = -1; s_selTick = 0; }
static int s_party = -1, s_partyTick = 0;      // party slot (0..5, 7=Cancel)
static void party_reset(void) { s_party = -1; s_partyTick = 0; }
static int s_tgt = -1, s_tgtTick = 0;          // target battler index (0..3)
static void target_reset(void) { s_tgt = -1; s_tgtTick = 0; }

static u16 select_pulse(GbaCore* core, uint32_t addr, int* val, int* tick) {
	if (*val < 0 || !core || !addr) return 0;
	gbacore_write8(core, addr, (uint8_t)*val);
	u16 k = (*tick == 1) ? (1 << GBAKEY_A) : 0;
	if (++(*tick) >= 2) { *val = -1; *tick = 0; }
	return k;
}

// ---- battle action/move (2x2 grid; write BOTH player slots 0 and 2) ----
static int hit_action(int gx, int gy) {
	if (gx < 136 || gx >= 232 || gy < 120 || gy >= 152) return -1;
	return ((gx >= 184) ? 1 : 0) | ((gy >= 136) ? 2 : 0);
}
static int hit_move(int gx, int gy) {
	if (gx < 16 || gx >= 152 || gy < 120 || gy >= 152) return -1;
	return ((gx >= 84) ? 1 : 0) | ((gy >= 136) ? 2 : 0);
}
static u16 menu_select(GbaCore* core, uint32_t base) {
	if (s_target < 0 || !core || !base) return 0;
	gbacore_write8(core, base + 0, (uint8_t)s_target);
	gbacore_write8(core, base + 2, (uint8_t)s_target);
	u16 k = (s_selTick == 1) ? (1 << GBAKEY_A) : 0;
	if (++s_selTick >= 2) battle_reset();
	return k;
}

// ---- party menu: tap a slot -> write gPartyMenu.slotId + A (single vs double layout) ----
static int hit_party(int gx, int gy, int layout) {
	if (gx >= 192 && gx < 240 && gy >= 136 && gy < 152) return 7;   // Cancel (both layouts)
	if (layout == 1) {                                              // DOUBLE
		if (gx >= 8 && gx < 88) { if (gy >= 8 && gy < 64) return 0; if (gy >= 64 && gy < 120) return 1; }
		if (gx >= 96 && gx < 240) {
			if (gy >= 8  && gy < 32)  return 2; if (gy >= 40  && gy < 64)  return 3;
			if (gy >= 72 && gy < 96)  return 4; if (gy >= 104 && gy < 128) return 5;
		}
	} else {                                                       // SINGLE (+ field fallback)
		if (gx >= 8 && gx < 88 && gy >= 24 && gy < 80) return 0;
		if (gx >= 96 && gx < 240) {
			if (gy >= 8   && gy < 32)  return 1; if (gy >= 32  && gy < 56)  return 2;
			if (gy >= 56  && gy < 80)  return 3; if (gy >= 80  && gy < 104) return 4;
			if (gy >= 104 && gy < 128) return 5;
		}
	}
	return -1;
}

// ---- double-battle target select: tap a battler -> write gMultiUsePlayerCursor + A ----
// positions (sBattlerCoords centers): 0 PLAYER_LEFT, 1 OPPONENT_LEFT, 2 PLAYER_RIGHT, 3 OPPONENT_RIGHT
static int hit_battler(int gx, int gy) {
	if (gx >= 4   && gx < 60  && gy >= 52 && gy < 108) return 0;
	if (gx >= 62  && gx < 118 && gy >= 60 && gy < 116) return 2;
	if (gx >= 172 && gx < 228 && gy >= 12 && gy < 68)  return 1;
	if (gx >= 124 && gx < 180 && gy >= 4  && gy < 60)  return 3;
	return -1;
}
static int battler_index_for_pos(const TouchSmart* sm, int pos) {
	for (int i = 0; i < sm->battlersCount && i < 4; i++)
		if (sm->battlerPos[i] == pos && !(sm->absentMask & (1 << i))) return i;
	return -1;
}

// =================== SMART: tap-to-walk with BFS pathfinding =================
// Tap (or slide) a tile -> BFS over the live collision grid -> follow the route, holding the D-pad
// toward each node. Continuously RE-TARGETS while the screen is touched (drag to steer; on a bike the
// held screen tile maps to a moving world tile, so you keep riding that way). Tap your own tile = A.
#define WBOX  33          // BFS window edge (tiles); goal is always within ~+-7 of the player
#define WHALF 16
#define KEY_DIR(d) (s_keyDir[d])
static const u16 s_keyDir[4] = { 1 << GBAKEY_RIGHT, 1 << GBAKEY_LEFT, 1 << GBAKEY_DOWN, 1 << GBAKEY_UP };

static bool s_walking = false;
static int  s_goalX = -1, s_goalY = -1, s_stall, s_lpx, s_lpy, s_aPulse, s_replans;
static int  s_mapW, s_mapH; static uint32_t s_mapPtr;
static int8_t s_pathDir[WBOX * WBOX]; static int s_pathLen, s_pathPos;
static void walk_reset(void) { s_walking = false; s_stall = 0; s_pathLen = s_pathPos = 0; s_goalX = s_goalY = -1; }

static bool map_read(GbaCore* core, const GameProfile* p, int* w, int* h, uint32_t* ptr) {
	if (!p) return false;
	*w   = (int32_t)gbacore_read32(core, p->mapLayout + 0);
	*h   = (int32_t)gbacore_read32(core, p->mapLayout + 4);
	*ptr =          gbacore_read32(core, p->mapLayout + 8);
	return (*ptr >> 24) == 0x02 && *w > 0 && *w <= 512 && *h > 0 && *h <= 512;
}
static bool walkable(GbaCore* core, uint32_t ptr, int w, int h, int wx, int wy) {
	int gx_ = wx + 7, gy_ = wy + 7;                                  // MAP_OFFSET border
	if (gx_ < 0 || gx_ >= w || gy_ < 0 || gy_ >= h) return false;
	uint16_t block = gbacore_read16(core, ptr + 2u * (uint32_t)(gx_ + w * gy_));
	if (block == 0x03FF) return false;                              // MAPGRID_UNDEFINED
	return ((block & 0x0C00) >> 10) == 0;                           // collision bits clear
}

// BFS from (sx,sy) to (gxw,gyw) within the +-WHALF window. Fills s_pathDir/s_pathLen on success.
static bool plan_bfs(GbaCore* core, const GameProfile* p, int sx, int sy, int gxw, int gyw) {
	int w, h; uint32_t ptr;
	if (!map_read(core, p, &w, &h, &ptr)) return false;
	if (abs(gxw - sx) > WHALF || abs(gyw - sy) > WHALF) return false;
	if (!walkable(core, ptr, w, h, gxw, gyw)) return false;
	s_mapW = w; s_mapH = h; s_mapPtr = ptr;

	static int16_t parent[WBOX * WBOX];
	static int8_t  dirOf[WBOX * WBOX];
	static int16_t q[WBOX * WBOX];
	for (int i = 0; i < WBOX * WBOX; i++) parent[i] = -1;
	const int dxs[4] = { 1, -1, 0, 0 }, dys[4] = { 0, 0, 1, -1 };
	int start = WHALF + WBOX * WHALF;
	int goal  = (gxw - sx + WHALF) + WBOX * (gyw - sy + WHALF);
	int head = 0, tail = 0;
	parent[start] = start; q[tail++] = start;
	bool found = false;
	while (head < tail) {
		int cur = q[head++];
		if (cur == goal) { found = true; break; }
		int clx = cur % WBOX, cly = cur / WBOX;
		for (int d = 0; d < 4; d++) {
			int nlx = clx + dxs[d], nly = cly + dys[d];
			if (nlx < 0 || nlx >= WBOX || nly < 0 || nly >= WBOX) continue;
			int nidx = nlx + WBOX * nly;
			if (parent[nidx] != -1) continue;
			if (!walkable(core, ptr, w, h, sx + nlx - WHALF, sy + nly - WHALF)) continue;
			parent[nidx] = cur; dirOf[nidx] = (int8_t)d; q[tail++] = nidx;
		}
	}
	if (!found) return false;
	int n = 0, cur = goal;
	static int8_t tmp[WBOX * WBOX];
	while (cur != start) { tmp[n++] = dirOf[cur]; cur = parent[cur]; if (n >= WBOX * WBOX) return false; }
	s_pathLen = n; s_pathPos = 0;
	for (int i = 0; i < n; i++) s_pathDir[i] = tmp[n - 1 - i];      // reverse to start->goal order
	s_goalX = gxw; s_goalY = gyw;
	return true;
}

static u16 walk_update(bool touching, bool newPress, bool gvalid, int gx, int gy,
                       int px, int py, GbaCore* core, const GameProfile* p) {
	if (px < 0 || py < 0 || !core || !p) { walk_reset(); s_aPulse = 0; return 0; }
	bool onSelf = gvalid && (gx / 16 == 7) && (gy / 16 == 5);       // player is centered at tile (7,5)
	if (newPress && onSelf) { s_aPulse = 3; walk_reset(); }         // tap self -> interact/advance (A)
	if (s_aPulse > 0) { s_aPulse--; return 1 << GBAKEY_A; }

	// Continuous (re)target while the finger is down: route to the world tile under it; re-plan only
	// when that tile changes (or we're idle). Released -> keep following the last path to its goal.
	if (touching && gvalid && !onSelf) {
		int twx = px + (gx / 16 - 7), twy = py + (gy / 16 - 5);
		if (!s_walking || twx != s_goalX || twy != s_goalY) {
			if (plan_bfs(core, p, px, py, twx, twy)) {
				s_walking = true; s_stall = 0; s_lpx = px; s_lpy = py; s_replans = 0;
			}
		}
	}
	if (!s_walking) return 0;

	int w, h; uint32_t ptr;                                         // map changed (door/warp) -> stop
	if (!map_read(core, p, &w, &h, &ptr) || ptr != s_mapPtr || w != s_mapW || h != s_mapH) { walk_reset(); return 0; }
	if ((px == s_goalX && py == s_goalY) || s_pathPos >= s_pathLen) { walk_reset(); return 0; }

	if (px != s_lpx || py != s_lpy) {                              // a step completed -> advance the path
		s_pathPos++; s_lpx = px; s_lpy = py; s_stall = 0;
		if (s_pathPos >= s_pathLen) { walk_reset(); return 0; }
	} else if (++s_stall > 24) {                                    // blocked (NPC/ledge/elevation/door)
		if (s_replans < 2 && plan_bfs(core, p, px, py, s_goalX, s_goalY)) { s_replans++; s_stall = 0; }
		else { walk_reset(); s_aPulse = 3; return 0; }             // give up -> face + A (tap an NPC/sign)
	}
	int d = s_pathDir[s_pathPos];
	if (d < 0 || d > 3) { walk_reset(); return 0; }
	return KEY_DIR(d);
}

// ================================ dispatch ==================================
static void all_reset(void) { battle_reset(); walk_reset(); party_reset(); target_reset(); }

u16 touch_update(TouchMode mode, bool touching, int sx, int sy, int gx, int gy, bool gvalid,
                 const TouchSmart* sm) {
	static bool wasTouching = false;
	bool newPress = touching && !wasTouching;
	wasTouching = touching;

	if (mode == TOUCH_PAD)   { all_reset(); return touching ? pad_keys(sx, sy) : 0; }
	if (mode != TOUCH_SMART) { all_reset(); return 0; }            // TOUCH_OFF
	if (!sm || !sm->valid)   { all_reset(); return 0; }

	switch (sm->ctx) {
	case GCTX_BATTLE_ACTION:
	case GCTX_BATTLE_MOVE: {
		walk_reset(); party_reset(); target_reset();
		uint32_t base = (sm->ctx == GCTX_BATTLE_ACTION) ? sm->actionAddr : sm->moveAddr;
		if (newPress && gvalid) {
			int cell = (sm->ctx == GCTX_BATTLE_ACTION) ? hit_action(gx, gy) : hit_move(gx, gy);
			if (cell >= 0) { s_target = cell; s_selTick = 0; }
		}
		return menu_select(sm->core, base);
	}
	case GCTX_BATTLE_TARGET:
		battle_reset(); walk_reset(); party_reset();
		if (newPress && gvalid) {
			int pos = hit_battler(gx, gy);
			if (pos >= 0) { int idx = battler_index_for_pos(sm, pos); if (idx >= 0) { s_tgt = idx; s_tgtTick = 0; } }
		}
		return select_pulse(sm->core, sm->prof ? sm->prof->multiCursor : 0, &s_tgt, &s_tgtTick);
	case GCTX_PARTY:
		battle_reset(); walk_reset(); target_reset();
		if (newPress && gvalid) {
			int slot = hit_party(gx, gy, sm->partyLayout);
			if (slot == 7 || (slot >= 0 && slot < sm->partyCount)) { s_party = slot; s_partyTick = 0; }
		}
		return select_pulse(sm->core, sm->prof ? sm->prof->partyMenu + 0x09 : 0, &s_party, &s_partyTick);
	case GCTX_OVERWORLD:
		battle_reset(); party_reset(); target_reset();
		return walk_update(touching, newPress, gvalid, gx, gy, sm->px, sm->py, sm->core, sm->prof);
	case GCTX_BATTLE_OTHER:
		all_reset();
		return touching ? (1 << GBAKEY_A) : 0;                     // battle dialog/animation: tap = advance
	default:
		all_reset();
		return 0;
	}
}

void touch_draw(TouchMode mode, u16 held, const TouchSmart* sm) {
	(void)sm;
	if (mode == TOUCH_PAD) pad_overlay(held);
	// SMART: no overlay — the touchscreen points at the real game UI.
}
