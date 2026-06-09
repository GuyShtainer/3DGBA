// touch.c — see touch.h.
#include <citro2d.h>
#include "gbacore.h"    // GBAKEY_*
#include "touch.h"

const char* const TOUCH_NAMES[3] = { "Off", "Gamepad", "Smart" };

#define COL_FAINT  C2D_Color32(0xF5, 0xD0, 0x42, 0x40)   // faint gold (idle zone)
#define COL_LIT    C2D_Color32(0xF5, 0xD0, 0x42, 0xB0)   // bright gold (pressed)

// =============================== PAD (virtual gamepad) ======================
// Fixed zones in 320x240 bottom-screen space. Single-touch: one key at a time.
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
	ZONE(33, 138, 44, 30, GBAKEY_UP);
	ZONE(33, 192, 44, 30, GBAKEY_DOWN);
	ZONE(7, 162, 32, 36, GBAKEY_LEFT);
	ZONE(71, 162, 32, 36, GBAKEY_RIGHT);
	ZONE(252, 150, 60, 60, GBAKEY_A);
	ZONE(198, 176, 50, 44, GBAKEY_B);
	ZONE(128, 214, 64, 22, GBAKEY_START);
	ZONE(4, 4, 52, 22, GBAKEY_L);
	ZONE(264, 4, 52, 22, GBAKEY_R);
	#undef ZONE
}

// ===================== SMART: real-UI battle menu pointer ===================
// Both Gen-3 menus are a 2x2 grid whose cursor encodes cell = (col & 1) | (row << 1):
//   0 = top-left, 1 = top-right, 2 = bottom-left, 3 = bottom-right.
// On-screen pixel rects (GBA 240x160, identical Emerald/FRLG; from the pret decomps):
//   ACTION: window x 136..231, y 120..151  (FIGHT/BAG top, POKEMON/RUN bottom)  split col @184
//   MOVE:   moves x 16..151,  y 120..151   (move1/2 top, move3/4 bottom)        split col @84
static int hit_action(int gx, int gy) {
	if (gx < 136 || gx >= 232 || gy < 120 || gy >= 152) return -1;
	return ((gx >= 184) ? 1 : 0) | ((gy >= 136) ? 2 : 0);
}
static int hit_move(int gx, int gy) {
	if (gx < 16 || gx >= 152 || gy < 120 || gy >= 152) return -1;
	return ((gx >= 84) ? 1 : 0) | ((gy >= 136) ? 2 : 0);
	// (no empty-slot gate: the move menu only draws real moves, and the gate's RAM read was
	//  over-rejecting slots 1-3 -> "only top-left worked")
}

// Deterministic select (robust): instead of a closed-loop that reads the cursor (fragile vs the
// gActiveBattler index / read latency — the cause of "only FIGHT / only top-left worked"), WRITE the
// menu cursor straight to RAM, then press A. We write BOTH player battler slots (index 0 and 2 cover
// a single battle and a standard double battle without having to guess gActiveBattler). The worker
// runs AFTER this on the main thread, so the write is in place before the frame reads it.
static int s_target = -1;     // queued cell (0..3), or -1 idle
static int s_selTick = 0;
static void battle_reset(void) { s_target = -1; s_selTick = 0; }

static u16 menu_select(GbaCore* core, uint32_t base) {
	if (s_target < 0 || !core || !base) return 0;
	gbacore_write8(core, base + 0, (uint8_t)s_target);
	gbacore_write8(core, base + 2, (uint8_t)s_target);
	// tick0: write only (guarantees A is a fresh press). tick1: press A (selects), then clear — clearing
	// BEFORE the menu changes (e.g. FIGHT -> move menu) so a stale target can't auto-pick in the next menu.
	u16 k = (s_selTick == 1) ? (1 << GBAKEY_A) : 0;
	if (++s_selTick >= 2) battle_reset();
	return k;
}

// ============================= SMART: tap-to-walk ===========================
// The overworld camera centers the player at screen tile (7,5), so a touched GBA pixel maps to a
// world tile: target = playerPos + (tile - (7,5)). We then hold the D-pad toward the target one
// axis at a time; the player steps one tile per held direction (pos changes by 1). Stop on arrival
// or when blocked (pos unchanged for ~24 frames). Tapping your own tile = A (interact/advance text).
static bool s_walking = false;
static int  s_wtx, s_wty, s_stall, s_lpx, s_lpy, s_aPulse;
static void walk_reset(void) { s_walking = false; s_stall = 0; }

static u16 walk_update(bool newPress, bool gvalid, int gx, int gy, int px, int py) {
	if (px < 0 || py < 0) { walk_reset(); s_aPulse = 0; return 0; }
	if (newPress && gvalid) {
		int tcx = gx / 16, tcy = gy / 16;
		if (tcx == 7 && tcy == 5) { s_aPulse = 3; s_walking = false; }   // tapped self -> A
		else { s_wtx = px + (tcx - 7); s_wty = py + (tcy - 5);
		       s_walking = true; s_stall = 0; s_lpx = px; s_lpy = py; s_aPulse = 0; }
	}
	if (s_aPulse > 0) { s_aPulse--; return 1 << GBAKEY_A; }
	if (!s_walking) return 0;
	if (px == s_wtx && py == s_wty) { walk_reset(); return 0; }          // arrived
	// Blocked (an NPC / PC / wall / ledge stopped us): face it and press A to interact / advance text.
	if (px == s_lpx && py == s_lpy) { if (++s_stall > 24) { walk_reset(); s_aPulse = 3; return 0; } }
	else { s_stall = 0; s_lpx = px; s_lpy = py; }
	if (px != s_wtx) return (s_wtx > px) ? (1 << GBAKEY_RIGHT) : (1 << GBAKEY_LEFT);      // X axis first
	return (s_wty > py) ? (1 << GBAKEY_DOWN) : (1 << GBAKEY_UP);                          // then Y
}

// ================================ dispatch ==================================
u16 touch_update(TouchMode mode, bool touching, int sx, int sy, int gx, int gy, bool gvalid,
                 const TouchSmart* sm) {
	static bool wasTouching = false;
	bool newPress = touching && !wasTouching;
	wasTouching = touching;

	if (mode == TOUCH_PAD) { battle_reset(); walk_reset(); return touching ? pad_keys(sx, sy) : 0; }
	if (mode != TOUCH_SMART) { battle_reset(); walk_reset(); return 0; }   // TOUCH_OFF

	// SMART: route by the live on-screen context.
	if (sm && sm->valid && (sm->ctx == GCTX_BATTLE_ACTION || sm->ctx == GCTX_BATTLE_MOVE)) {
		walk_reset();
		uint32_t base = (sm->ctx == GCTX_BATTLE_ACTION) ? sm->actionAddr : sm->moveAddr;
		if (newPress && gvalid) {
			int cell = (sm->ctx == GCTX_BATTLE_ACTION) ? hit_action(gx, gy)
			                                           : hit_move(gx, gy);
			if (cell >= 0) { s_target = cell; s_selTick = 0; }
		}
		return menu_select(sm->core, base);
	}
	if (sm && sm->valid && sm->ctx == GCTX_OVERWORLD) {
		battle_reset();
		return walk_update(newPress, gvalid, gx, gy, sm->px, sm->py);
	}
	if (sm && sm->valid && sm->ctx == GCTX_BATTLE_OTHER) {   // battle text / animation (also party/bag for now)
		battle_reset(); walk_reset();
		return touching ? (1 << GBAKEY_A) : 0;   // tap (or hold) anywhere = advance the dialog (A)
	}
	// Unhandled context (battle sub-screen, party, bag, ...) — no smart input yet (switch to Gamepad
	// mode or use the physical buttons). Intentionally injects nothing rather than mis-driving.
	battle_reset(); walk_reset();
	return 0;
}

void touch_draw(TouchMode mode, u16 held, const TouchSmart* sm) {
	(void)sm;
	if (mode == TOUCH_PAD) pad_overlay(held);
	// SMART: no overlay — the touchscreen points at the real game UI.
}
