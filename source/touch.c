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

// ===================== SMART: hybrid tap-to-walk + steer =====================
// HOLD / SLIDE the screen -> steer toward the touch (dominant of the 4 axes), at any speed incl. bike.
// QUICK TAP a tile -> BFS-route there over the live collision grid (doors/ladders/NPCs included as the
// path terminal). Tap your own tile = A. The camera centers the player at screen tile (7,5).
#define WBOX  65          // BFS window edge (tiles); ~+-32 around the player
#define WHALF 32
#define TAP_FRAMES 12     // released within this many frames AND barely moved => a "tap" -> BFS route
static const u16 s_keyDir[4] = { 1 << GBAKEY_RIGHT, 1 << GBAKEY_LEFT, 1 << GBAKEY_DOWN, 1 << GBAKEY_UP };

static int s_aPulse = 0;
static bool s_walking = false;                          // BFS route active (from a tap)
static int  s_goalX = -1, s_goalY = -1, s_stall, s_lpx, s_lpy;
static int  s_mapW, s_mapH; static uint32_t s_mapPtr;
static int8_t s_pathDir[WBOX * WBOX]; static int s_pathLen, s_pathPos;
static int  s_touchFrames = 0, s_downGx, s_downGy, s_downPx, s_downPy; static bool s_moved;
static void walk_reset(void) {
	s_aPulse = 0; s_walking = false; s_pathLen = s_pathPos = 0; s_touchFrames = 0; s_moved = false;
}

static bool map_read(GbaCore* core, const GameProfile* p, int* w, int* h, uint32_t* ptr) {
	if (!p) return false;
	*w = (int32_t)gbacore_read32(core, p->mapLayout + 0);
	*h = (int32_t)gbacore_read32(core, p->mapLayout + 4);
	*ptr =        gbacore_read32(core, p->mapLayout + 8);
	return (*ptr >> 24) == 0x02 && *w > 0 && *w <= 512 && *h > 0 && *h <= 512;
}
static bool walkable(GbaCore* core, uint32_t ptr, int w, int h, int wx, int wy) {
	int gx_ = wx + 7, gy_ = wy + 7;                      // MAP_OFFSET border
	if (gx_ < 0 || gx_ >= w || gy_ < 0 || gy_ >= h) return false;
	uint16_t block = gbacore_read16(core, ptr + 2u * (uint32_t)(gx_ + w * gy_));
	if (block == 0x03FF) return false;                  // MAPGRID_UNDEFINED
	return ((block & 0x0C00) >> 10) == 0;               // collision bits clear
}
// BFS from (sx,sy) to (gxw,gyw) within +-WHALF. A blocked GOAL is allowed as the terminal (door/NPC).
static bool plan_bfs(GbaCore* core, const GameProfile* p, int sx, int sy, int gxw, int gyw) {
	int w, h; uint32_t ptr;
	if (!map_read(core, p, &w, &h, &ptr)) return false;
	if (abs(gxw - sx) > WHALF || abs(gyw - sy) > WHALF) return false;
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
			if (nidx != goal && !walkable(core, ptr, w, h, sx + nlx - WHALF, sy + nly - WHALF)) continue;
			parent[nidx] = cur; dirOf[nidx] = (int8_t)d; q[tail++] = nidx;
		}
	}
	if (!found) return false;
	int n = 0, cur = goal;
	static int8_t tmp[WBOX * WBOX];
	while (cur != start) { tmp[n++] = dirOf[cur]; cur = parent[cur]; if (n >= WBOX * WBOX) return false; }
	s_pathLen = n; s_pathPos = 0;
	for (int i = 0; i < n; i++) s_pathDir[i] = tmp[n - 1 - i];   // start->goal order
	s_goalX = gxw; s_goalY = gyw;
	return true;
}

static u16 walk_update(bool touching, bool newPress, bool gvalid, int gx, int gy,
                       int px, int py, GbaCore* core, const GameProfile* p) {
	if (s_aPulse > 0) { s_aPulse--; return 1 << GBAKEY_A; }

	if (newPress && gvalid) { s_downGx = gx; s_downGy = gy; s_downPx = px; s_downPy = py; s_touchFrames = 0; s_moved = false; }

	if (touching && gvalid) {                            // HOLD -> steer toward the touch (cancels a route)
		s_touchFrames++;
		if (abs(gx - s_downGx) > 8 || abs(gy - s_downGy) > 8) s_moved = true;
		s_walking = false;
		int ddx = gx / 16 - 7, ddy = gy / 16 - 5;
		if (ddx == 0 && ddy == 0) return 0;              // on the player tile -> stand (tap-self A on release)
		if (abs(ddx) > abs(ddy)) return ddx > 0 ? s_keyDir[0] : s_keyDir[1];
		return ddy > 0 ? s_keyDir[2] : s_keyDir[3];
	}

	if (s_touchFrames > 0) {                             // just released
		bool tap = (s_touchFrames <= TAP_FRAMES) && !s_moved;
		int ddx = s_downGx / 16 - 7, ddy = s_downGy / 16 - 5;
		s_touchFrames = 0;
		if (tap && core && p) {
			if (ddx == 0 && ddy == 0) s_aPulse = 3;        // tapped self -> A (interact/advance)
			else if (plan_bfs(core, p, px, py, s_downPx + ddx, s_downPy + ddy)) {   // route to the tapped tile
				s_walking = true; s_lpx = px; s_lpy = py; s_stall = 0;
			}
		}
		if (s_aPulse > 0) { s_aPulse--; return 1 << GBAKEY_A; }
	}

	if (!s_walking || !core || !p) return 0;            // BFS path-follow (released, routing)
	int w, h; uint32_t ptr;
	if (!map_read(core, p, &w, &h, &ptr) || ptr != s_mapPtr || w != s_mapW || h != s_mapH) { s_walking = false; return 0; }
	if ((px == s_goalX && py == s_goalY) || s_pathPos >= s_pathLen) { s_walking = false; return 0; }
	if (px != s_lpx || py != s_lpy) {                   // a step completed -> advance the path
		s_pathPos++; s_lpx = px; s_lpy = py; s_stall = 0;
		if (s_pathPos >= s_pathLen) { s_walking = false; return 0; }
	} else if (++s_stall > 24) { s_walking = false; s_aPulse = 3; return 0; }   // blocked -> face + A
	int d = s_pathDir[s_pathPos];
	if (d < 0 || d > 3) { s_walking = false; return 0; }
	return s_keyDir[d];
}

// =================== SMART: general field menu (sMenu) =======================
// START / YES-NO / multichoice share one cursor. Tap a row -> write sMenu.cursorPos (+ the START
// mirror) + pulse A. Reads the live window template + pitch, so it follows whatever menu is up.
static int s_fmenu = -1, s_fmenuTick = 0;
static void fmenu_reset(void) { s_fmenu = -1; s_fmenuTick = 0; }

static int hit_fieldmenu(GbaCore* core, const GameProfile* p, int gx, int gy) {
	uint8_t wid = gbacore_read8(core, p->sMenuBase + 5);
	if (wid == 0xFF) return -1;
	uint32_t win = p->gWindowsBase + 12u * (uint32_t)wid;
	int wl = gbacore_read8(core, win + 1), wt = gbacore_read8(core, win + 2), ww = gbacore_read8(core, win + 3);
	int top = gbacore_read8(core, p->sMenuBase + 1), pitch = gbacore_read8(core, p->sMenuBase + 8);
	int maxc = (int8_t)gbacore_read8(core, p->sMenuBase + 4);
	if (pitch <= 0) return -1;
	int x0 = wl * 8, y0 = wt * 8 + top;
	if (gx < x0 || gx >= x0 + ww * 8) return -1;
	int i = (gy - y0) / pitch;
	return (i >= 0 && i <= maxc) ? i : -1;
}
static u16 fmenu_select(GbaCore* core, const GameProfile* p) {
	if (s_fmenu < 0 || !core) return 0;
	gbacore_write8(core, p->sMenuBase + 2, (uint8_t)s_fmenu);          // sMenu.cursorPos
	if ((gbacore_read32(core, p->startCb) & ~1u) == p->startCbInput)   // START menu dispatches on its mirror
		gbacore_write8(core, p->startCursor, (uint8_t)s_fmenu);
	u16 k = (s_fmenuTick == 1) ? (1 << GBAKEY_A) : 0;
	if (++s_fmenuTick >= 2) fmenu_reset();
	return k;
}

// ============================ SMART: bag menu ===============================
// Tap a visible item row -> write the live ListMenu row (+26) + A. Visible rows only (no scroll v1);
// the bottom row is CLOSE BAG. EM: 8 rows from y16; FR/LG: 6 rows from y8 (pitch 16, both).
static int s_bag = -1, s_bagTick = 0;            // pending tap-select (visible row) + A pulse
static bool s_bagDown = false, s_bagDrag = false;
static int s_bagDownX, s_bagDownY, s_bagLastX, s_bagLastY, s_bagRow;
static void bag_reset(void) { s_bag = -1; s_bagTick = 0; s_bagDown = false; s_bagDrag = false; }

static int hit_bag(const TouchSmart* sm, int gx, int gy) {
	bool fr = sm->prof && (sm->prof->code[2] == 'R' || sm->prof->code[2] == 'G');
	int x0 = fr ? 88 : 112, y0 = fr ? 8 : 16, rows = fr ? 6 : 8;
	if (gx < x0 || gx >= 232 || gy < y0 || gy >= y0 + rows * 16) return -1;
	int r = (gy - y0) / 16;
	return (r >= 0 && r < rows) ? r : -1;
}
// Phone-style: TAP a row -> select on release (write live row +26, then A). DRAG vertically ->
// scroll (inject UP/DOWN, using the game's own cursor+scroll). DRAG horizontally -> switch pocket
// (inject LEFT/RIGHT). Never writes the scroll field directly (that path is racy).
static u16 bag_update(const TouchSmart* sm, bool touching, bool newPress, bool gvalid, int gx, int gy) {
	if (s_bag >= 0) {                                  // finish a pending tap-select pulse
		if (sm->core && sm->bagListTaskBase) gbacore_write16(sm->core, sm->bagListTaskBase + 26, (uint16_t)s_bag);
		u16 k = (s_bagTick == 1) ? (1 << GBAKEY_A) : 0;
		if (++s_bagTick >= 2) { s_bag = -1; s_bagTick = 0; }
		return k;
	}
	if (newPress && gvalid) { s_bagDown = true; s_bagDrag = false; s_bagDownX = s_bagLastX = gx; s_bagDownY = s_bagLastY = gy; s_bagRow = hit_bag(sm, gx, gy); }
	if (touching && s_bagDown && gvalid) {
		if (!s_bagDrag && (abs(gx - s_bagDownX) > 6 || abs(gy - s_bagDownY) > 6)) s_bagDrag = true;
		if (s_bagDrag) {
			if (gy - s_bagLastY >= 14) { s_bagLastX = gx; s_bagLastY = gy; return 1 << GBAKEY_UP; }    // drag down -> items above
			if (s_bagLastY - gy >= 14) { s_bagLastX = gx; s_bagLastY = gy; return 1 << GBAKEY_DOWN; }  // drag up -> items below
			if (gx - s_bagLastX >= 30) { s_bagLastX = gx; s_bagLastY = gy; return 1 << GBAKEY_LEFT; }  // swipe right -> prev pocket
			if (s_bagLastX - gx >= 30) { s_bagLastX = gx; s_bagLastY = gy; return 1 << GBAKEY_RIGHT; } // swipe left  -> next pocket
		}
		return 0;
	}
	if (!touching && s_bagDown) {                      // released
		s_bagDown = false;
		if (!s_bagDrag && s_bagRow >= 0) { s_bag = s_bagRow; s_bagTick = 0; }   // a clean tap -> select
	}
	return 0;
}

// ================================ dispatch ==================================
static void all_reset(void) { battle_reset(); walk_reset(); party_reset(); target_reset(); fmenu_reset(); bag_reset(); }

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
		walk_reset(); party_reset(); target_reset(); fmenu_reset(); bag_reset();
		uint32_t base = (sm->ctx == GCTX_BATTLE_ACTION) ? sm->actionAddr : sm->moveAddr;
		if (newPress && gvalid) {
			int cell = (sm->ctx == GCTX_BATTLE_ACTION) ? hit_action(gx, gy) : hit_move(gx, gy);
			if (cell >= 0) { s_target = cell; s_selTick = 0; }
		}
		return menu_select(sm->core, base);
	}
	case GCTX_BATTLE_TARGET:
		battle_reset(); walk_reset(); party_reset(); fmenu_reset(); bag_reset();
		if (newPress && gvalid) {
			int pos = hit_battler(gx, gy);
			if (pos >= 0) { int idx = battler_index_for_pos(sm, pos); if (idx >= 0) { s_tgt = idx; s_tgtTick = 0; } }
		}
		return select_pulse(sm->core, sm->prof ? sm->prof->multiCursor : 0, &s_tgt, &s_tgtTick);
	case GCTX_PARTY:
		battle_reset(); walk_reset(); target_reset(); fmenu_reset(); bag_reset();
		if (newPress && gvalid) {
			int slot = hit_party(gx, gy, sm->partyLayout);
			if (slot == 7 || (slot >= 0 && slot < sm->partyCount)) { s_party = slot; s_partyTick = 0; }
		}
		return select_pulse(sm->core, sm->prof ? sm->prof->partyMenu + 0x09 : 0, &s_party, &s_partyTick);
	case GCTX_OVERWORLD:
		battle_reset(); party_reset(); target_reset(); fmenu_reset(); bag_reset();
		return walk_update(touching, newPress, gvalid, gx, gy, sm->px, sm->py, sm->core, sm->prof);
	case GCTX_FIELDMENU:
		battle_reset(); walk_reset(); party_reset(); target_reset(); bag_reset();
		if (newPress && gvalid && sm->prof) { int i = hit_fieldmenu(sm->core, sm->prof, gx, gy); if (i >= 0) { s_fmenu = i; s_fmenuTick = 0; } }
		return sm->prof ? fmenu_select(sm->core, sm->prof) : 0;
	case GCTX_BAG:
		battle_reset(); walk_reset(); party_reset(); target_reset(); fmenu_reset();
		return bag_update(sm, touching, newPress, gvalid, gx, gy);
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
