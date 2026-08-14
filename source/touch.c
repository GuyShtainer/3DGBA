// touch.c — see touch.h.
#include <stdlib.h>     // abs
#include <stdio.h>      // FILE / fprintf (the touch-event SD log dump)
#include <string.h>     // memset
#include <sys/stat.h>   // mkdir (ensure the netlogs dir exists)
#include <citro2d.h>
#include "gbacore.h"    // GBAKEY_*
#include "touch.h"
#include "theme.h"     // pad color/edge prefs (g_prefs) + the fixed PAD_COLOR_* tints
#include "ui.h"        // shared widget kit (borders, chips, centered text)
#include "assets.h"    // baked fonts: the mode/menu chips draw TXT_CHIP at its native size
#include "fieldpath.h" // phase 18: warp classification + the elevation-correct router (pure C)
#include "uihit.h"     // phase 19 / L3.2.8: the chip boxes, shared with the host suite
#include "touchgeom.h" // phase 22.1: keyboard + list hit geometry (pure C, host-tested)

const char* const TOUCH_NAMES[3] = { "Off", "Gamepad", "Smart" };

// Pad tint comes from g_prefs.padColor (5 options, theme.h); idle zones draw at alpha 0x40,
// pressed at 0xB0 — the same alphas the old fixed-gold overlay used.
static u32 pad_tint(u8 alpha) {
	const u32 cols[5] = { PAD_COLOR_0, PAD_COLOR_1, PAD_COLOR_2, PAD_COLOR_3, PAD_COLOR_4 };
	u32 c = cols[((unsigned)g_prefs.padColor) % 5];
	return (c & 0x00FFFFFFu) | ((u32)alpha << 24);
}

// =============================== PAD (virtual gamepad) ======================
// The "≡ menu" chip: ONE rect for the draw and the hit test (touch.h explains why it needed to
// become a real control). Bottom-right, clear of the drawn A key (252,150,60,60 -> rows 150..209).
// PHASE 19 / SPEC-legible L3.2.8: the box grows 18 -> 20 for the taller TXT_CHIP cell. It moves
// UP two rows with it (220 -> 218) rather than growing downward: at y=220 a 20 px box ends
// exactly on row 240, i.e. its rounded bottom corners fall off the panel. W stays 48 (bars 8 +
// gap 4 + "menu" 20 = 32 <= 48). One rect, so the hit test follows the art automatically.
#define MCHIP_X ((float)UIHIT_MCHIP_X)
#define MCHIP_Y ((float)UIHIT_MCHIP_Y)
#define MCHIP_W ((float)UIHIT_MCHIP_W)
#define MCHIP_H ((float)UIHIT_MCHIP_H)

int touch_menu_chip(TouchMode mode, int px, int py) {
	// SPEC-layout L6.3.5: SMART now draws the chip too, so it must hit-test in SMART as well.
	// (In SMART a bare tap is a POINTER event on the real game UI, so without this the only way
	//  back to the pause menu was the START+SELECT combo — the mode with the least on-screen
	//  affordance had the least reachable menu. OFF is unchanged: any tap opens the menu there.)
	if (mode != TOUCH_PAD && mode != TOUCH_SMART) return 0;
	return (px >= (int)MCHIP_X && px < (int)(MCHIP_X + MCHIP_W) &&
	        py >= (int)MCHIP_Y && py < (int)(MCHIP_Y + MCHIP_H)) ? 1 : 0;
}

// The dark translucent chip both touch modes label themselves with (screenshots 06 + 07). Split
// out of pad_overlay so PAD and SMART cannot drift, and drawn with the BAKED font at its native
// size — the system font at scale 0.32 is what made small chip labels a smudge (W4.2).
// PHASE 19 / SPEC-legible L3.2.8: box 14 -> 17 (the TXT_CHIP cell is 15 px now, so 14 clipped
// it outright), label centred by ink instead of the hand-typed +3.
#define TCHIP_H ((float)UIHIT_TOUCH_CHIP_H)
static void touch_chip(C2D_TextBuf buf, const char* s, float cx, float y, u32 ink) {
	float w = ui_chip_measure(buf, s) + 2.0f;
	ui_fill(cx - w / 2.0f, y, w, TCHIP_H, C2D_Color32(0x00, 0x00, 0x00, 0x96), 4.0f);
	assets_text_c(buf, TXT_CHIP, s, cx, typo_center_y(TXT_CHIP, y, TCHIP_H), ink);
}

// The "menu" affordance, drawn at exactly the rect touch_menu_chip() hit-tests.
// SEEN IN CAPTURE: the chip used to print the string "≡ menu", and once the label moved to the
// BAKED font (W4.2) the "≡" came out as a solid tofu block — U+2261 is not in JetBrains Mono's
// coverage, so mkbcfnt cannot bake it. (It survived before only because the system font has it,
// at the blurry 0.32 scale this phase is removing.) The hamburger is three quads instead: no font
// dependency, no tofu, and it is what the design's chip actually draws.
static void touch_menu_chip_draw(C2D_TextBuf buf, u32 ink) {
	ui_fill(MCHIP_X, MCHIP_Y, MCHIP_W, MCHIP_H, C2D_Color32(0x00, 0x00, 0x00, 0x96), 4.0f);
	const float BARW = 8.0f, GAP = 4.0f;
	float lw = assets_text_w(buf, TXT_CHIP, "menu");
	float bx = MCHIP_X + (MCHIP_W - (BARW + GAP + lw)) / 2.0f;
	float by = MCHIP_Y + MCHIP_H / 2.0f - 3.0f;
	for (int i = 0; i < 3; i++) C2D_DrawRectSolid(bx, by + (float)i * 2.5f, 0.0f, BARW, 1.0f, ink);
	assets_text(buf, TXT_CHIP, "menu", bx + BARW + GAP, typo_center_y(TXT_CHIP, MCHIP_Y, MCHIP_H), ink);
}

static u16 pad_keys(int px, int py) {
	if (touch_menu_chip(TOUCH_PAD, px, py)) return 0;    // the chip is not the A key
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

// Rounded key fill (Round=6 / Soft=3 / Sharp=1). Phase 17 (SPEC-widgets W2): this used to be a
// private COPY of the old three-rect "rectangle minus four SQUARE corners" approximation, so every
// pad key was a square with four hard corner bites that showed the dark screen through them —
// sweep D5's "opaque black corner blocks", present only at padEdge=Round where r is largest.
// ui_fill is now the corrected staircase AND keeps the non-overlap guarantee this call site needs
// (these fills are translucent; overlapping quads would double-blend the interior). One
// implementation of "rounded rect" in the binary.
static void pad_zone(float x, float y, float w, float h, u32 col, float r) {
	ui_fill(x, y, w, h, col, r);
}

// Centered glyph label on a zone (buf may be NULL -> zones only).
// FIX PASS: the BAKED TXT_BUTTON at texel scale 1.0, not the system font at 0.42. These are the
// A / B / L / R / START glyphs on the virtual gamepad — chrome the user stares at for a whole
// session in Gamepad mode, and the last on-screen control labels still going through the blurry
// path SPEC-crisp removed everywhere else. TXT_BUTTON is the role typography.h names for button
// labels and is the closest rung to the old size (cap height 7 px vs ~8), so no key changes shape.
// Centred on the ROLE's px rather than a measured line height: at scale 1.0 the line box is the
// bake's own, and this is the same idiom menu_ov_label uses.
// PHASE 19 / SPEC-legible L3.2.7 + L4.6: TXT_BUTTON goes cap 7 -> cap 9 here, the chrome a
// Gamepad-mode player stares at for a whole session, and NO key rect changes ("START" is 37 px
// in a 64 px key). The centre is the ink box's, not the line box's — at cap 9 in a 22 px key
// the old line-box centre put the glyphs 3 px low, visibly riding the key's bottom edge.
static void pad_label(C2D_TextBuf buf, const char* s, float x, float y, float w, float h, u32 col) {
	if (!buf) return;
	assets_text_c(buf, TXT_BUTTON, s, x + w / 2.0f, typo_center_y(TXT_BUTTON, y, h), col);
}

static void pad_overlay(u16 held, C2D_TextBuf buf) {
	// 1:1 (screenshot 06): each zone = a translucent tint fill + a brighter tint OUTLINE + a
	// tint glyph; a dark "TOUCH · GAMEPAD" chip top-center and a dark "≡ menu" chip bottom-right.
	static const float EDGE_R[3] = { 6.0f, 3.0f, 1.0f };   // Round / Soft / Sharp
	float r = EDGE_R[((unsigned)g_prefs.padEdge) % 3];
	u32 faint = pad_tint(0x36), lit = pad_tint(0x92);
	u32 line  = pad_tint(0xB8), glyph = pad_tint(0xE6);
	// W2: the outline follows the SAME rounded silhouette as the fill (ui_border's square corners
	// would poke out past a rounded key, and screenshot 06 draws rounded keys with rounded frames).
	#define ZONE(x,y,w,h,k,g) do { \
		pad_zone((x), (y), (w), (h), (held & (1 << (k))) ? lit : faint, r); \
		ui_border_round((x), (y), (w), (h), line, 1.5f, r); \
		pad_label(buf, (g), (x), (y), (w), (h), glyph); \
	} while (0)
	#define DZONE(x,y,w,h,k,d) do { \
		pad_zone((x), (y), (w), (h), (held & (1 << (k))) ? lit : faint, r); \
		ui_border_round((x), (y), (w), (h), line, 1.5f, r); \
		ui_tri((x) + (w) / 2.0f, (y) + (h) / 2.0f, 5.0f, (d), glyph); \
	} while (0)
	DZONE(33, 138, 44, 30, GBAKEY_UP, 2);    DZONE(33, 192, 44, 30, GBAKEY_DOWN, 3);
	DZONE(7, 162, 32, 36, GBAKEY_LEFT, 1);   DZONE(71, 162, 32, 36, GBAKEY_RIGHT, 0);
	#undef DZONE
	ZONE(252, 150, 60, 60, GBAKEY_A, "A");    ZONE(198, 176, 50, 44, GBAKEY_B, "B");
	ZONE(128, 214, 64, 22, GBAKEY_START, "START");
	ZONE(4, 4, 52, 22, GBAKEY_L, "L");        ZONE(264, 4, 52, 22, GBAKEY_R, "R");
	#undef ZONE
	if (buf) {
		touch_chip(buf, "TOUCH · GAMEPAD", 160.0f, (float)UIHIT_TOUCH_CHIP_Y, glyph);
		touch_menu_chip_draw(buf, pad_tint(0xC8));
	}
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
// QUICK TAP a tile -> route there over the live grid. Tap your own tile = A. The camera centers the
// player at screen tile (7,5).
//
// PHASE 18 / SPEC-door. The classification, the walkability rule and the search now live in
// fieldpath.c (pure C, host-tested against the user's real ROM maps); this file keeps the gesture
// handling, the per-frame emit and the new TERMINAL HOLD. What changed, and why:
//   * a DOOR is retargeted to the tile SOUTH of it and finished with a HELD UP, because
//     TryDoorWarp is gated `direction == DIR_NORTH` in both engines. Before, the path's last step
//     was INTO the impassable door tile and the route just stalled and pressed A.
//   * an ARROW / stair warp is routed ONTO and finished with its own held direction. Before, the
//     router arrived and stopped — which is why "tap the exit mat to leave the building" did
//     nothing at all, in every building, in both games (512 + 263 such tiles).
//   * a STEP warp (ladder / escalator / warp pad) needs no hold: arriving fires it.
//   * the route dies the moment SaveBlock1.location changes, so the warp it just triggered cannot
//     leave a stale route driving the player around the arrival map.
#define TAP_FRAMES 12     // released within this many frames AND barely moved => a "tap" -> route
#define DOUBLE_FRAMES 16  // second tap-on-self within this many frames => START
// How long to HOLD the terminal direction. The game needs heldDirection2 AND
// dpadDirection == playerDirection (pokeemerald FieldGetPlayerInput / ProcessPlayerFieldInput), and
// the first frames are spent turning the avatar to face the door — so this is a sustained press,
// never a pulse. 30 emulated frames ~ 0.5 s at 60 fps. VERIFY-ON-HW-PENDING in the same sense as
// control.h's tap constants (SPEC-door T4.5.4 / Open Q6): it must be re-counted at the app's real
// frame rate and at the degraded rate of a wireless session.
#define TERM_FRAMES 30
#define REPLAN_MAX  2     // a stalled route re-reads the NPCs and re-plans at most this many times
static const u16 s_keyDir[4] = { 1 << GBAKEY_RIGHT, 1 << GBAKEY_LEFT, 1 << GBAKEY_DOWN, 1 << GBAKEY_UP };

static int s_aPulse = 0;
static bool s_walking = false;                          // route active (from a tap)
static int  s_goalX = -1, s_goalY = -1, s_stall, s_lpx, s_lpy;
static int  s_appX = -1, s_appY = -1;                   // terminal tile the path actually ends on
static int  s_mapW, s_mapH; static uint32_t s_mapPtr;
static int  s_mapG = -1, s_mapN = -1;                   // SaveBlock1.location at plan time
static int8_t s_pathDir[FP_WBOX * FP_WBOX]; static int s_pathLen, s_pathPos;
static int  s_termDir = FP_NODIR, s_termFrames = 0; static bool s_termActive = false;
static FpKind s_kind = FP_WK_NONE; static int s_replans = 0;
static int  s_touchFrames = 0, s_downGx, s_downGy, s_downPx, s_downPy; static bool s_moved;
static int  s_downMapG = -1, s_downMapN = -1;                    // map at press time (tap staleness)
static int  s_tick = 0, s_lastSelfTap = -999, s_startPulse = 0;   // double-tap-self -> START
static int  s_npcN = 0; static short s_npcG[16][2];              // active object-event grid coords (+7 space)
static void walk_reset(void) {
	s_aPulse = 0; s_startPulse = 0; s_walking = false; s_pathLen = s_pathPos = 0; s_touchFrames = 0; s_moved = false;
	s_termActive = false; s_termDir = FP_NODIR; s_termFrames = 0; s_kind = FP_WK_NONE; s_replans = 0;
}

// --- the gdb/harness mirror (LOGGING ONLY; nothing reads it back) -------------------------------
// SPEC-door T4.11. A screenshot cannot prove a warp fired — a door animation without a warp looks
// the same for several frames — so the objective instrument is this struct plus the game's own
// SaveBlock1.location, read over the emutest gdb channel (`run gdbio read-u32 g_fieldDbg+N`,
// `poll --changed`, `see rec --with-state g_fieldDbg`). planSeq increments once per planning
// ATTEMPT so a poll can latch on it. Deliberately non-static: the harness resolves it by name out
// of 3DGBA.elf.
FieldDbg g_fieldDbg = { 0 };
static void fdbg_plan(int px, int py, int mapG, int mapN, const FpPlan* pl) {
	g_fieldDbg.px = px; g_fieldDbg.py = py; g_fieldDbg.mapGroup = mapG; g_fieldDbg.mapNum = mapN;
	g_fieldDbg.goalX = pl->goalX; g_fieldDbg.goalY = pl->goalY;
	g_fieldDbg.approachX = pl->approachX; g_fieldDbg.approachY = pl->approachY;
	g_fieldDbg.kind = (int32_t)pl->kind; g_fieldDbg.termDir = pl->termDir;
	g_fieldDbg.pathLen = pl->pathLen; g_fieldDbg.pElev = pl->pElev;
	g_fieldDbg.behaviour = pl->behaviour; g_fieldDbg.outcome = pl->outcome;
	g_fieldDbg.warpGroup = pl->warpGroup; g_fieldDbg.warpNum = pl->warpNum;
	g_fieldDbg.headRetarget = pl->headRetarget ? 1 : 0;
	g_fieldDbg.routeEnd = FDBG_END_NONE;
	g_fieldDbg.planSeq++;
}
static void fdbg_end(int how) { g_fieldDbg.routeEnd = how; g_fieldDbg.endSeq++; }
static void fplog_push(GbaCore* core, int isEnd);
static void route_end(GbaCore* core, int how) { fdbg_end(how); fplog_push(core, 1); }

// --- the SD-side plan log (SPEC-door T4.10) -----------------------------------------------------
// g_fieldDbg is the EMULATOR instrument (one live snapshot over gdb). On real hardware there is no
// gdb, so every planning attempt and every route end also lands in this ring, flushed with the
// touch log on session close. These columns are the difference between "the user says it bumped"
// and knowing WHICH of the candidate causes it was: the tapped tile, the behaviour byte we read
// there, the kind we decided, where the path was actually aimed, and how the route finished.
#define FPLOG_N 128
typedef struct {
	uint32_t frame; int16_t px, py, goalX, goalY, appX, appY;
	int16_t mapG, mapN, behaviour, pathLen, pElev, warpG, warpN;
	uint8_t kind, termDir, outcome, head, end, isEnd;
} FpLogEntry;
static FpLogEntry s_fpLog[FPLOG_N];
static uint32_t   s_fpLogN = 0;
static void fplog_push(GbaCore* core, int isEnd) {
	FpLogEntry* e = &s_fpLog[s_fpLogN % FPLOG_N];
	memset(e, 0, sizeof *e);
	e->frame = core ? gbacore_frame_counter(core) : 0;
	e->px = (int16_t)g_fieldDbg.px; e->py = (int16_t)g_fieldDbg.py;
	e->goalX = (int16_t)g_fieldDbg.goalX; e->goalY = (int16_t)g_fieldDbg.goalY;
	e->appX = (int16_t)g_fieldDbg.approachX; e->appY = (int16_t)g_fieldDbg.approachY;
	e->mapG = (int16_t)g_fieldDbg.mapGroup; e->mapN = (int16_t)g_fieldDbg.mapNum;
	e->behaviour = (int16_t)g_fieldDbg.behaviour; e->pathLen = (int16_t)g_fieldDbg.pathLen;
	e->pElev = (int16_t)g_fieldDbg.pElev;
	e->warpG = (int16_t)g_fieldDbg.warpGroup; e->warpN = (int16_t)g_fieldDbg.warpNum;
	e->kind = (uint8_t)g_fieldDbg.kind; e->termDir = (uint8_t)(g_fieldDbg.termDir & 0xFF);
	e->outcome = (uint8_t)g_fieldDbg.outcome; e->head = (uint8_t)g_fieldDbg.headRetarget;
	e->end = (uint8_t)g_fieldDbg.routeEnd; e->isEnd = (uint8_t)isEnd;
	s_fpLogN++;
}

// Read active overworld object-events (NPCs) so the BFS routes AROUND them. Skips slot 0 (the player).
static void read_npcs(GbaCore* core, const GameProfile* p) {
	s_npcN = 0;
	if (!p->mapObjects) return;
	for (int i = 1; i < 16; i++) {
		uint32_t e = p->mapObjects + 0x24u * (uint32_t)i;
		if (!(gbacore_read32(core, e) & 1u)) continue;                 // active:1
		s_npcG[s_npcN][0] = (short)(int16_t)gbacore_read16(core, e + 0x10);   // currentCoords.x (grid, +7)
		s_npcG[s_npcN][1] = (short)(int16_t)gbacore_read16(core, e + 0x12);   // currentCoords.y
		s_npcN++;
	}
}

static bool map_read(GbaCore* core, const GameProfile* p, int* w, int* h, uint32_t* ptr) {
	if (!p) return false;
	*w = (int32_t)gbacore_read32(core, p->mapLayout + 0);
	*h = (int32_t)gbacore_read32(core, p->mapLayout + 4);
	*ptr =        gbacore_read32(core, p->mapLayout + 8);
	return (*ptr >> 24) == 0x02 && *w > 0 && *w <= 512 && *h > 0 && *h <= 512;
}
// --- fieldpath bus adapter: the classifier is pure C and reads the game through these ----------
static uint8_t  fp_r8 (void* c, uint32_t a) { return gbacore_read8 ((GbaCore*)c, a); }
static uint16_t fp_r16(void* c, uint32_t a) { return gbacore_read16((GbaCore*)c, a); }
static uint32_t fp_r32(void* c, uint32_t a) { return gbacore_read32((GbaCore*)c, a); }

// BPRE / BPGE use the FRLG metatile-behaviour numbering, tileset attribute offset (+0x14, u32),
// primary-tileset size (640) and mask (0x1FF); BPEE uses RSE's. The 0x60-0x71 block genuinely
// disagrees between them, so this must never collapse into one table.
static FpEngine fp_engine(const GameProfile* p) {
	return (p->code[2] == 'R' || p->code[2] == 'G') ? FP_ENG_FRLG : FP_ENG_RSE;
}

// Plan a route to the tapped tile. Returns true and arms the follow loop; false means NOTHING is
// injected — which for a door with no reachable approach is the whole point (a documented no-op
// beats tackling the wall next to it).
static bool walk_plan(GbaCore* core, const GameProfile* p, int px, int py, int gx, int gy,
                      int mapG, int mapN) {
	int w, h; uint32_t ptr;
	// STATIC, not a stack local: FpPlan carries the 4225-entry path array (~4.3 KB), and this runs
	// on the render thread, deep inside the per-frame call chain. Single-threaded by construction
	// (touch_update is only ever called from the main/render thread — CLAUDE.md #2: workers never
	// touch this path), so one shared instance is safe and the frame stack stays small.
	static FpPlan pl;
	if (!map_read(core, p, &w, &h, &ptr)) {
		memset(&pl, 0, sizeof pl); pl.outcome = FP_OUT_BADMAP; pl.termDir = FP_NODIR;
		pl.goalX = gx; pl.goalY = gy; pl.approachX = gx; pl.approachY = gy; pl.behaviour = -1;
		pl.warpGroup = pl.warpNum = -1;
		fdbg_plan(px, py, mapG, mapN, &pl);
		fplog_push(core, 0);
		return false;
	}
	read_npcs(core, p);   // NPCs block transit so the route goes AROUND them
	FpBus bus = { fp_r8, fp_r16, fp_r32, core };
	FpMap m = { fp_engine(p), p->mapHeaderPath, p->mapObjects, ptr, w, h };
	bool ok = fieldpath_plan(&bus, &m, px, py, gx, gy, s_npcG, s_npcN, &pl);
	fdbg_plan(px, py, mapG, mapN, &pl);
	fplog_push(core, 0);
	if (!ok) return false;
	s_mapW = w; s_mapH = h; s_mapPtr = ptr; s_mapG = mapG; s_mapN = mapN;
	s_goalX = pl.goalX; s_goalY = pl.goalY;
	s_appX = pl.approachX; s_appY = pl.approachY;
	s_kind = pl.kind; s_termDir = pl.termDir;
	s_pathLen = pl.pathLen; s_pathPos = 0;
	for (int i = 0; i < pl.pathLen; i++) s_pathDir[i] = pl.path[i];
	s_termActive = false; s_termFrames = 0;
	return true;
}

static u16 walk_update_inner(bool touching, bool newPress, bool gvalid, int gx, int gy,
                             int px, int py, int mapG, int mapN, GbaCore* core, const GameProfile* p) {
	s_tick++;
	if (s_startPulse > 0) { s_startPulse--; return 1 << GBAKEY_START; }
	if (s_aPulse > 0)     { s_aPulse--;     return 1 << GBAKEY_A; }

	if (newPress && gvalid) { s_downGx = gx; s_downGy = gy; s_downPx = px; s_downPy = py; s_touchFrames = 0; s_moved = false;
	                          s_downMapG = mapG; s_downMapN = mapN; }
	if (touching && gvalid) { s_touchFrames++; if (abs(gx - s_downGx) > 8 || abs(gy - s_downGy) > 8) s_moved = true; }

	// No loaded overworld map (title / intro / main menu): a tap = A, a double-tap = START. No walking.
	if (px < 0) {
		if (!touching && s_touchFrames > 0) {
			bool tap = s_touchFrames <= TAP_FRAMES && !s_moved; s_touchFrames = 0;
			if (tap) { if (s_tick - s_lastSelfTap < DOUBLE_FRAMES) s_startPulse = 3; else s_aPulse = 3; s_lastSelfTap = s_tick; }
		}
		if (s_startPulse > 0) { s_startPulse--; return 1 << GBAKEY_START; }
		if (s_aPulse > 0)     { s_aPulse--;     return 1 << GBAKEY_A; }
		return 0;
	}

	if (touching && gvalid) {                            // HOLD -> steer toward the touch (cancels a route)
		s_walking = false;
		int ddx = gx / 16 - 7, ddy = gy / 16 - 5;
		if (ddx == 0 && ddy == 0) return 0;              // on the player tile -> stand (tap-self handled on release)
		if (abs(ddx) > abs(ddy)) return ddx > 0 ? s_keyDir[0] : s_keyDir[1];
		return ddy > 0 ? s_keyDir[2] : s_keyDir[3];
	}

	if (s_touchFrames > 0) {                             // just released
		bool tap = (s_touchFrames <= TAP_FRAMES) && !s_moved;
		int ddx = s_downGx / 16 - 7, ddy = s_downGy / 16 - 5;
		s_touchFrames = 0;
		if (tap && core && p) {
			if (ddx == 0 && ddy == 0) {                  // tapped self
				if (s_tick - s_lastSelfTap < DOUBLE_FRAMES) s_startPulse = 3;   // double-tap self -> START
				else s_aPulse = 3;                                              // single -> A (interact/advance)
				s_lastSelfTap = s_tick;
			} else if (mapG == s_downMapG && mapN == s_downMapN &&
			           abs(px - s_downPx) <= 1 && abs(py - s_downPy) <= 1) {
				// SPEC-door T5.12, tightened (see the BUILDLOG deviation note). The tapped WORLD
				// tile is (press-time player tile + screen offset): the camera was anchored on the
				// player when the finger went down, so that anchor is the correct one even though
				// the BFS starts from where the player is NOW. What is genuinely unsafe is a WARP
				// or a teleport between press and release — then the offset names a tile on a map
				// that is no longer on screen. Refuse those (map changed, or the player jumped
				// further than the one tile a step in flight can cover) rather than route to a
				// tile the user never pointed at.
				if (walk_plan(core, p, px, py, s_downPx + ddx, s_downPy + ddy, mapG, mapN)) {
					s_walking = true; s_lpx = px; s_lpy = py; s_stall = 0; s_replans = 0;
				}
			}
		}
		if (s_startPulse > 0) { s_startPulse--; return 1 << GBAKEY_START; }
		if (s_aPulse > 0)     { s_aPulse--;     return 1 << GBAKEY_A; }
	}

	if (!s_walking || !core || !p) return 0;            // path-follow (released, routing)
	int w, h; uint32_t ptr;
	// THE WARP KILL-SWITCH (T4.6). SaveBlock1.location changing IS the proof the warp fired, and it
	// is the only reliable signal: gBackupMapLayout.map is a FIXED EWRAM buffer, so the old
	// ptr/w/h check cannot see a same-size map swap and a stale route survives into the arrival
	// map. Both nets are kept.
	if (mapG != s_mapG || mapN != s_mapN) {
		s_walking = false; s_termActive = false; route_end(core, FDBG_END_MAPCHANGE); return 0;
	}
	if (!map_read(core, p, &w, &h, &ptr) || ptr != s_mapPtr || w != s_mapW || h != s_mapH) {
		s_walking = false; s_termActive = false; route_end(core, FDBG_END_MAPCHANGE); return 0;
	}

	if (s_termActive) {
		// THE TERMINAL HOLD — continuous, never pulsed: the game wants heldDirection2 AND
		// dpadDirection == playerDirection, and the first frames go on turning the avatar.
		if (px != s_lpx || py != s_lpy) {          // a DIR/STEP tile moved us: the hold did its job
			s_walking = false; s_termActive = false; route_end(core, FDBG_END_MOVED); return 0;
		}
		if (++s_termFrames > TERM_FRAMES) {
			// Time out SILENTLY. Never the legacy A pulse here: A at a door is at best a no-op and
			// at worst opens a sign or starts an NPC conversation.
			s_walking = false; s_termActive = false; route_end(core, FDBG_END_TIMEOUT); return 0;
		}
		return s_keyDir[s_termDir];
	}

	if (px != s_lpx || py != s_lpy) { s_pathPos++; s_lpx = px; s_lpy = py; s_stall = 0; }   // a step completed
	else if (++s_stall > 24) {
		// Blocked. NPCs are sampled once at plan time, so the commonest cause is one that has since
		// walked into the path — re-read them and re-plan toward the same terminal before giving
		// up. Only a WK_NONE route (a sign, an NPC, a cuttable tree — what the behaviour was
		// written for) still ends with the A press.
		//
		// FIX PASS (review finding 1): NOT at a WK_NONE terminal. That stall is the route
		// ARRIVING at a deliberately blocked goal, the replan always succeeds, and three rounds
		// of it turned a ~0.4 s tap-a-sign into ~1.25 s of the avatar bumping the sign — plus two
		// whole-window BFS floods on the render thread while both GBA workers are saturated.
		// fieldpath_should_replan is the rule (host-graded, TEST 14); mid-route stalls on a
		// WK_NONE route still replan, and every other kind is untouched.
		if (fieldpath_should_replan(s_kind, s_pathPos, s_pathLen) &&
		    s_replans < REPLAN_MAX && walk_plan(core, p, px, py, s_goalX, s_goalY, mapG, mapN)) {
			s_replans++; s_lpx = px; s_lpy = py; s_stall = 0;
			route_end(core, FDBG_END_REPLANNED);
		} else {
			s_walking = false; s_termActive = false;
			if (s_kind == FP_WK_NONE) s_aPulse = 3;
			route_end(core, FDBG_END_STALLED);
			return 0;
		}
	}

	if (px == s_appX && py == s_appY) s_pathPos = s_pathLen;   // arrived (also covers a zero-step plan)
	if (s_pathPos >= s_pathLen) {
		if (s_termDir >= 0 && s_termDir < 4) { s_termActive = true; s_termFrames = 1; return s_keyDir[s_termDir]; }
		s_walking = false; route_end(core, FDBG_END_ARRIVED); return 0;   // WK_STEP / WK_NONE: nothing more to do
	}
	int d = s_pathDir[s_pathPos];
	if (d < 0 || d > 3) { s_walking = false; route_end(core, FDBG_END_ARRIVED); return 0; }
	return s_keyDir[d];
}

// LOGGING ONLY. Everything latched in g_fieldDbg above is from PLAN time; this stamps the LIVE
// state every overworld frame, which is what makes a warp provable from outside the emulated
// console — the harness reads the game's own SaveBlock1.location over gdb and watches it change.
static u16 walk_update(bool touching, bool newPress, bool gvalid, int gx, int gy,
                       int px, int py, int mapG, int mapN, GbaCore* core, const GameProfile* p) {
	u16 k = walk_update_inner(touching, newPress, gvalid, gx, gy, px, py, mapG, mapN, core, p);
	g_fieldDbg.curMapGroup = mapG; g_fieldDbg.curMapNum = mapN;
	g_fieldDbg.curPx = px; g_fieldDbg.curPy = py;
	g_fieldDbg.curKeys = k;
	g_fieldDbg.curFrame = core ? (int32_t)gbacore_frame_counter(core) : 0;
	g_fieldDbg.walking = s_termActive ? 2 : (s_walking ? 1 : 0);
	return k;
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

// ================= SMART: the LIST family (bag / mart / PC items / qty / dex) =================
// PHASE 22.1 (SPEC-family-lists). ONE generic driver replaces the bespoke bag handler: the live
// ListMenu template is read every frame (window rect, totalItems, maxShowed — L2), a clean tap
// writes selectedRow (+26) then pulses A (the shipped bag idiom), a vertical drag injects one
// UP/DOWN edge per 14 px, a fast release becomes a capped HELD key (the honest fling, L5), and a
// tap in the blank space below a short list is DEAD (the L3 clamp — the old "tap blank -> CLOSE
// BAG" wart is gone). scrollOffset (+24) is read for the clamp only and NEVER written (L4).
// The same gesture state serves LK_QTY (drag = ±1/±10 roller) and LK_DEX (key-injection only)
// because the kinds are mutually exclusive; a kind change resets it (list_reset).
static int  s_lRow = -1, s_lTick = 0;            // pending tap-select (visible row) + A pulse
static bool s_lDown = false, s_lDrag = false;
static int  s_lDownX, s_lDownY, s_lLastX, s_lLastY;
static int  s_lPressRow = -1, s_lPressArrow = -1, s_lPressTab = -1;
static int  s_lVel[4], s_lVelN = 0;              // per-frame dy ring (fling velocity, L5)
static int  s_lFling = 0; static u16 s_lFlingKey = 0;   // fling-hold frames + key
static int  s_lSeq = 0, s_lSeqTick = 0; static u16 s_lSeqKey = 0;  // queued tab edges (1 per 2 frames)
static int  s_lPrevKind = -1;                    // kind change inside GCTX_LIST -> reset
static void list_reset(void) {
	s_lRow = -1; s_lTick = 0; s_lDown = false; s_lDrag = false;
	s_lPressRow = s_lPressArrow = s_lPressTab = -1;
	s_lVelN = 0; s_lFling = 0; s_lFlingKey = 0; s_lSeq = 0; s_lSeqTick = 0; s_lSeqKey = 0;
	s_lPrevKind = -1;
}

// Live geometry read (L2): template fields out of the ListMenu struct + the window rect out of
// gWindows — the identical read hit_fieldmenu ships for sMenu. Returns 0 => emit NOTHING (L10).
static int list_read_geom(const TouchSmart* sm, uint32_t listBase, ListGeom* g) {
	if (!sm->core || !sm->prof || !listBase || !sm->prof->gWindowsBase) return 0;
	g->totalItems   = gbacore_read16(sm->core, listBase + 12);
	g->maxShowed    = gbacore_read16(sm->core, listBase + 14);
	g->scrollOffset = gbacore_read16(sm->core, listBase + 24);
	uint8_t wid = gbacore_read8(sm->core, listBase + 16);
	if (wid >= 32) return 0;
	uint32_t win = sm->prof->gWindowsBase + 12u * (uint32_t)wid;
	g->x0 = gbacore_read8(sm->core, win + 1) * 8;
	g->y0 = gbacore_read8(sm->core, win + 2) * 8;
	g->w  = gbacore_read8(sm->core, win + 3) * 8;
	g->h  = gbacore_read8(sm->core, win + 4) * 8;
	return listgeom_valid(g);
}

#define LF_BAG 1   // bag flavor: horizontal swipe = pocket switch + the pocket-tab targets (L12)

// The generic tier L-A driver (bag + mart buy + PC items).
static u16 list_update(const TouchSmart* sm, uint32_t listBase, int flags,
                       bool touching, bool newPress, bool gvalid, int gx, int gy) {
	if (s_lRow >= 0) {                               // finish a pending write-then-A select
		if (sm->core && listBase) gbacore_write16(sm->core, listBase + 26, (uint16_t)s_lRow);
		u16 k = (s_lTick == 1) ? (1 << GBAKEY_A) : 0;
		if (++s_lTick >= 2) { s_lRow = -1; s_lTick = 0; }
		return k;
	}
	if (s_lSeq > 0) {                                // queued pocket-tab edges, one per 2 frames
		u16 k = (s_lSeqTick == 0) ? s_lSeqKey : 0;
		if (++s_lSeqTick >= 2) { s_lSeqTick = 0; s_lSeq--; }
		return k;
	}
	if (s_lFling > 0) {                              // fling-hold: the game's own key repeat scrolls
		if (newPress) { s_lFling = 0; }              // any new touch cancels (L5) — dead frame
		else { s_lFling--; return s_lFlingKey; }
		return 0;
	}
	ListGeom g;
	int geomOk = list_read_geom(sm, listBase, &g);
	if (newPress && gvalid) {
		s_lDown = true; s_lDrag = false;
		s_lDownX = s_lLastX = gx; s_lDownY = s_lLastY = gy;
		s_lVelN = 0;
		s_lPressRow   = geomOk ? listgeom_tap_row(&g, gx, gy) : -1;
		s_lPressArrow = geomOk ? listgeom_arrow_band(&g, gx, gy) : -1;
		s_lPressTab   = -1;
		if (flags & LF_BAG) {                        // pocket tabs (L12)
			bool fr = sm->prof && (sm->prof->code[2] == 'R' || sm->prof->code[2] == 'G');
			if (fr) { int a = baggeom_fr_pocket_arrow(gx, gy); if (a >= 0) s_lPressTab = 16 + a; }
			else    { int d = baggeom_em_pocket_dot(gx, gy);   if (d >= 0) s_lPressTab = d; }
		}
	}
	if (touching && s_lDown && gvalid) {
		if (!s_lDrag && (abs(gx - s_lDownX) > LISTGEOM_SLOP_PX || abs(gy - s_lDownY) > LISTGEOM_SLOP_PX))
			s_lDrag = true;
		if (!s_lDrag && s_lPressArrow >= 0)          // held arrow band = held key (L7)
			return (s_lPressArrow == 0) ? (1 << GBAKEY_UP) : (1 << GBAKEY_DOWN);
		if (s_lDrag) {
			int dy = gy - s_lLastY;                  // velocity sample (px/frame, this frame)
			s_lVel[s_lVelN++ & 3] = dy;
			if (dy >= LISTGEOM_DRAG_PX)      { s_lLastX = gx; s_lLastY = gy; return 1 << GBAKEY_UP; }    // drag down -> items above
			if (dy <= -LISTGEOM_DRAG_PX)     { s_lLastX = gx; s_lLastY = gy; return 1 << GBAKEY_DOWN; }  // drag up -> items below
			if (flags & LF_BAG) {
				if (gx - s_lLastX >= LISTGEOM_SWIPE_PX)  { s_lLastX = gx; s_lLastY = gy; return 1 << GBAKEY_LEFT; }
				if (s_lLastX - gx >= LISTGEOM_SWIPE_PX)  { s_lLastX = gx; s_lLastY = gy; return 1 << GBAKEY_RIGHT; }
			}
		}
		return 0;
	}
	if (!touching && s_lDown) {                      // released
		s_lDown = false;
		if (s_lDrag) {                               // fling? (L5: avg of the last 4 dy samples)
			int n = (s_lVelN < 4) ? s_lVelN : 4, sum = 0;
			for (int i = 0; i < n; i++) sum += s_lVel[i];
			int v = n ? sum / n : 0;
			int f = listgeom_fling_frames(v);
			if (f > 0) { s_lFling = f; s_lFlingKey = (v > 0) ? (1 << GBAKEY_UP) : (1 << GBAKEY_DOWN); }
		} else if (s_lPressTab >= 16) {              // FR pocket arrow: one LEFT/RIGHT edge
			s_lSeq = 1; s_lSeqTick = 0;
			s_lSeqKey = (s_lPressTab == 16) ? (1 << GBAKEY_LEFT) : (1 << GBAKEY_RIGHT);
		} else if (s_lPressTab >= 0) {               // EM pocket dot i: |delta| sequenced edges
			if (sm->core && sm->prof && sm->prof->bagPocket) {
				int cur = gbacore_read8(sm->core, sm->prof->bagPocket);
				int d = s_lPressTab - cur;
				if (cur >= 0 && cur < 5 && d != 0) {
					s_lSeq = (d > 0) ? d : -d; s_lSeqTick = 0;
					s_lSeqKey = (d > 0) ? (1 << GBAKEY_RIGHT) : (1 << GBAKEY_LEFT);
				}
			}
		} else if (s_lPressRow >= 0 && geomOk && listBase) {   // clean tap -> select (L3)
			s_lRow = s_lPressRow; s_lTick = 0;
		}
		s_lPressRow = s_lPressArrow = s_lPressTab = -1;
	}
	return 0;
}

// LK_QTY — the "how many?" roller (L8). No RAM writes: the game maps UP/DOWN = ±1 and
// LEFT/RIGHT = ∓/±10 (AdjustQuantityAccordingToDPadInput), so a vertical drag steps ±1 per
// 14 px, a horizontal drag ±10 per 30 px, and a clean tap = A (confirm). DEVIATION from L8's
// window-band taps, reasoned: the qty window rect is not carried in the task data and no
// capture-derived rects are banked for it — the drag roller + tap-confirm needs no rect at all
// and cannot mis-hit. Recorded in OVERNIGHT2-BUILDLOG.
static u16 qty_update(const TouchSmart* sm, bool touching, bool newPress, bool gvalid, int gx, int gy) {
	(void)sm;
	if (newPress && gvalid) {
		s_lDown = true; s_lDrag = false;
		s_lDownX = s_lLastX = gx; s_lDownY = s_lLastY = gy;
	}
	if (touching && s_lDown && gvalid) {
		if (!s_lDrag && (abs(gx - s_lDownX) > LISTGEOM_SLOP_PX || abs(gy - s_lDownY) > LISTGEOM_SLOP_PX))
			s_lDrag = true;
		if (s_lDrag) {
			if (s_lLastY - gy >= LISTGEOM_DRAG_PX)  { s_lLastX = gx; s_lLastY = gy; return 1 << GBAKEY_UP; }    // drag up -> +1
			if (gy - s_lLastY >= LISTGEOM_DRAG_PX)  { s_lLastX = gx; s_lLastY = gy; return 1 << GBAKEY_DOWN; }  // drag down -> -1
			if (gx - s_lLastX >= LISTGEOM_SWIPE_PX) { s_lLastX = gx; s_lLastY = gy; return 1 << GBAKEY_RIGHT; } // -> +10
			if (s_lLastX - gx >= LISTGEOM_SWIPE_PX) { s_lLastX = gx; s_lLastY = gy; return 1 << GBAKEY_LEFT; }  // -> -10
		}
		return 0;
	}
	if (!touching && s_lDown) {
		s_lDown = false;
		if (!s_lDrag) { s_lRow = -2; s_lTick = 0; }  // clean tap -> A pulse (s_lRow -2 = key-only)
	}
	if (s_lRow == -2) {                              // 2-frame A pulse without any RAM write
		u16 k = (s_lTick == 1) ? (1 << GBAKEY_A) : 0;
		if (++s_lTick >= 2) { s_lRow = -1; s_lTick = 0; }
		return k;
	}
	return 0;
}

// LK_DEX — the EM Pokedex list (tier L-B, L20/L21). The cursor model is bespoke and a raw
// selectedPokemon write does NOT redraw, so this screen is KEY-INJECTION ONLY: vertical drag /
// fling scroll (the game's own cursor+scroll), horizontal swipe = the dex's ±page jump, and the
// two footer chips are native buttons ("START MENU" -> START, "SELECT SEARCH" -> SELECT; sprite
// anchors (16,120)/(48,120) and (16,144)/(48,144), pokedex.c:2791-2801 — bands grown to the full
// label, verify-on-emulator). Relative row taps stay DISABLED until the L21 slot formula is
// derived on emulator (g_touchDbg carries the three fields every LK_DEX frame for exactly that).
static u16 dex_update(const TouchSmart* sm, bool touching, bool newPress, bool gvalid, int gx, int gy) {
	(void)sm;
	if (s_lRow == -2) {                              // pending chip pulse (START or SELECT)
		u16 k = (s_lTick == 1) ? s_lSeqKey : 0;
		if (++s_lTick >= 2) { s_lRow = -1; s_lTick = 0; }
		return k;
	}
	if (s_lFling > 0) {
		if (newPress) { s_lFling = 0; }
		else { s_lFling--; return s_lFlingKey; }
		return 0;
	}
	if (newPress && gvalid) {
		s_lDown = true; s_lDrag = false;
		s_lDownX = s_lLastX = gx; s_lDownY = s_lLastY = gy;
		s_lVelN = 0;
		s_lPressTab = -1;
		if (gx < 72 && gy >= 110 && gy < 130) s_lPressTab = 32;      // START MENU chip
		if (gx < 72 && gy >= 132 && gy < 154) s_lPressTab = 33;      // SELECT SEARCH chip
	}
	if (touching && s_lDown && gvalid) {
		if (!s_lDrag && (abs(gx - s_lDownX) > LISTGEOM_SLOP_PX || abs(gy - s_lDownY) > LISTGEOM_SLOP_PX))
			s_lDrag = true;
		if (s_lDrag) {
			int dy = gy - s_lLastY;
			s_lVel[s_lVelN++ & 3] = dy;
			if (dy >= LISTGEOM_DRAG_PX)  { s_lLastX = gx; s_lLastY = gy; return 1 << GBAKEY_UP; }
			if (dy <= -LISTGEOM_DRAG_PX) { s_lLastX = gx; s_lLastY = gy; return 1 << GBAKEY_DOWN; }
			if (gx - s_lLastX >= LISTGEOM_SWIPE_PX) { s_lLastX = gx; s_lLastY = gy; return 1 << GBAKEY_LEFT; }   // page jump
			if (s_lLastX - gx >= LISTGEOM_SWIPE_PX) { s_lLastX = gx; s_lLastY = gy; return 1 << GBAKEY_RIGHT; }
		}
		return 0;
	}
	if (!touching && s_lDown) {
		s_lDown = false;
		if (s_lDrag) {
			int n = (s_lVelN < 4) ? s_lVelN : 4, sum = 0;
			for (int i = 0; i < n; i++) sum += s_lVel[i];
			int v = n ? sum / n : 0;
			int f = listgeom_fling_frames(v);
			if (f > 0) { s_lFling = f; s_lFlingKey = (v > 0) ? (1 << GBAKEY_UP) : (1 << GBAKEY_DOWN); }
		} else if (s_lPressTab == 32) { s_lRow = -2; s_lTick = 0; s_lSeqKey = 1 << GBAKEY_START; }
		else if (s_lPressTab == 33)   { s_lRow = -2; s_lTick = 0; s_lSeqKey = 1 << GBAKEY_SELECT; }
		s_lPressTab = -1;
	}
	return 0;
}

// ===================== SMART: the KEYBOARD family (naming screen) =====================
// PHASE 22.1 (SPEC-family-keyboard). Character taps use the house write-then-A idiom on the
// game's OWN cursor sprite (unlike party/bag we can move the real highlight — it is a sprite,
// not a reprint): tick 0 writes sX/sY/sPrevX/sPrevY + the pixel x/y, tick 1 writes again and
// pulses A; the game's HandleKeyboardEvent reads the key role at the just-written cursor and
// AddTextCharacter commits the tapped char. PAGE/BACK/OK use the game's own advertised key
// shortcuts (SELECT / B / START-then-A) — no RAM write at all (R6-R8). No D-pad key is ever
// injected here (R12), and NOTHING falls through to the walk residual while the ctx holds (R10).
static int s_nmCol = -1, s_nmRow = 0, s_nmTick = 0;   // pending char cell (write-then-A)
static int s_nmPulse = 0; static u16 s_nmKey = 0;     // pending SELECT/B pulse (2 frames)
static int s_nmOk = -1;                               // OK sequence phase: 0 START / 1 gap / 2 A
static void naming_reset(void) { s_nmCol = -1; s_nmRow = 0; s_nmTick = 0; s_nmPulse = 0; s_nmKey = 0; s_nmOk = -1; }

// R9 validation gates (b)-(e); returns 0 = pass (with *ptrOut set) or the first failing gate 1..4.
static int naming_gates(const TouchSmart* sm, uint32_t* ptrOut) {
	const GameProfile* p = sm->prof;
	if (!sm->core || !p || !p->namingPtr || !p->sprites) return 1;
	uint32_t ptr = gbacore_read32(sm->core, p->namingPtr);
	if (ptr < 0x02000000u || ptr >= 0x02040000u) return 1;           // (b) EWRAM range
	if (gbacore_read8(sm->core, ptr + 0x1E23) >= 64) return 2;       // (c) cursorSpriteId < 64
	if (gbacore_read8(sm->core, ptr + 0x1E10) != 2) return 3;        // (d) STATE_HANDLE_INPUT
	if (gbacore_read8(sm->core, ptr + 0x1E22) >= 3) return 4;        // (e) currentPage < 3
	*ptrOut = ptr;
	return 0;
}

static u16 naming_update(const TouchSmart* sm, bool touching, bool newPress, bool gvalid,
                         int gx, int gy, int sx, int sy) {
	uint32_t ptr = 0;
	int gate = naming_gates(sm, &ptr);
	if (gate) {
		// R10: gate (a) holds (the ctx IS the naming screen) but the struct is not actionable
		// (fade, page-swap anim, OK commit, or a bad read). Drop any pending action and behave
		// as PAD for the frame — the virtual gamepad zones still let the user type by hand;
		// the one forbidden outcome is a walk-key leak.
		naming_reset();
		return touching ? pad_keys(sx, sy) : 0;
	}
	if (s_nmOk >= 0) {                                // R8: START (cursor jumps to OK), gap, A
		u16 k = (s_nmOk == 0) ? (1 << GBAKEY_START) : (s_nmOk == 2) ? (1 << GBAKEY_A) : 0;
		if (++s_nmOk > 2) s_nmOk = -1;
		return k;
	}
	if (s_nmCol >= 0) {                               // R5: the write-then-A char select
		int page = gbacore_read8(sm->core, ptr + 0x1E22);
		int cx, cy;
		if (namegeom_cursor_px(page, s_nmCol, s_nmRow, &cx, &cy)) {
			uint8_t id = gbacore_read8(sm->core, ptr + 0x1E23);
			uint32_t spr = sm->prof->sprites + 0x44u * (uint32_t)id;
			gbacore_write16(sm->core, spr + 0x20, (uint16_t)cx);          // x (the visual cursor follows)
			gbacore_write16(sm->core, spr + 0x22, (uint16_t)cy);          // y
			gbacore_write16(sm->core, spr + 0x2E, (uint16_t)s_nmCol);     // sX   (data[0])
			gbacore_write16(sm->core, spr + 0x30, (uint16_t)s_nmRow);     // sY   (data[1])
			gbacore_write16(sm->core, spr + 0x32, (uint16_t)s_nmCol);     // sPrevX (data[2])
			gbacore_write16(sm->core, spr + 0x34, (uint16_t)s_nmRow);     // sPrevY (data[3])
			u16 k = (s_nmTick == 1) ? (1 << GBAKEY_A) : 0;
			if (++s_nmTick >= 2) { s_nmCol = -1; s_nmTick = 0; }
			return k;
		}
		s_nmCol = -1; s_nmTick = 0;                   // page changed under us -> drop, don't guess
		return 0;
	}
	if (s_nmPulse > 0) { s_nmPulse--; return s_nmKey; }   // R6/R7: SELECT / B, 2-frame pulse
	if (newPress && gvalid) {                         // act on press (R14 decision: snappier typing;
		int col, row;                                 //   logged in the BUILDLOG; holds repeat nothing)
		int page = gbacore_read8(sm->core, ptr + 0x1E22);
		switch (namegeom_hit(page, gx, gy, &col, &row)) {
		case NGH_CHAR: s_nmCol = col; s_nmRow = row; s_nmTick = 0; break;
		case NGH_PAGE: s_nmKey = 1 << GBAKEY_SELECT; s_nmPulse = 2; break;
		case NGH_BACK: s_nmKey = 1 << GBAKEY_B;      s_nmPulse = 2; break;
		case NGH_OK:   s_nmOk = 0; break;
		default: break;                               // dead gutters / padding cells / backdrop (R1/R2/R4)
		}
	}
	return 0;
}

// ===================== touch-event instrumentation log =======================
// Decode a GBA key mask to a short string (bit order A0 B1 Sel2 St3 Right4 Left5 Up6 Down7 R8 L9).
static void touch_keystr(uint16_t k, char* out, int cap) {
	static const char* const N[] = { "A","B","s","S",">","<","^","v","R","L" };
	int n = 0;
	for (int i = 0; i < 10 && n < cap - 2; i++) if (k & (1u << i)) { const char* t = N[i]; while (*t && n < cap - 1) out[n++] = *t++; }
	if (n == 0) out[n++] = '-';
	out[n] = '\0';
}
// LOGGING ONLY — records each touch EVENT (press / drag-step / hold / release) with the ctx fingerprint
// and the context-relevant game cursor read BEFORE and AFTER the dispatch (so a row shows old->new = the
// proof the cursor tracks touch). Edge-ish: a new press / release always logs; a hold or drag logs when
// the injected key OR the cursor changes, plus a slow heartbeat, so a steady hold doesn't flood the ring.
// In-memory ring + ONE SD dump on session close (the gs_log pattern); no per-frame file I/O.

// Map the current ctx to its authoritative game cursor (addr, byte-size). size 0 = no cursor (overworld
// has none -> we log the injected walk key; undetected screens have none -> we log cb2/tasks only).
//   BATTLE_ACTION -> actionAddr (u8) ; BATTLE_MOVE -> moveAddr (u8) ; BATTLE_TARGET -> multiCursor (u8) ;
//   PARTY -> partyMenu+0x09 (u8) ; FIELDMENU -> sMenuBase+2 (u8) ; BAG -> bagListTaskBase+26 (u16).
static void touch_cursor_addr(const TouchSmart* sm, uint32_t* addr, int* size) {
	*addr = 0; *size = 0;
	if (!sm) return;
	const GameProfile* p = sm->prof;
	switch (sm->ctx) {
	case GCTX_BATTLE_ACTION: *addr = sm->actionAddr; *size = 1; break;
	case GCTX_BATTLE_MOVE:   *addr = sm->moveAddr;   *size = 1; break;
	case GCTX_BATTLE_TARGET: if (p) { *addr = p->multiCursor;       *size = 1; } break;
	case GCTX_PARTY:         if (p) { *addr = p->partyMenu + 0x09u; *size = 1; } break;
	case GCTX_FIELDMENU:     if (p) { *addr = p->sMenuBase + 2u;    *size = 1; } break;
	case GCTX_BAG:           if (sm->bagListTaskBase) { *addr = sm->bagListTaskBase + 26u; *size = 2; } break;
	case GCTX_LIST:          if (sm->listBase) { *addr = sm->listBase + 26u; *size = 2; } break;   // 22.1 (LK_QTY/LK_DEX carry none; g_touchDbg covers them)
	default: break;   // OVERWORLD / NONE / BATTLE_OTHER / NAMING: no single cursor addr -> key mask + g_touchDbg tell the story
	}
}
static uint32_t touch_cursor_read(const TouchSmart* sm, uint32_t addr, int size) {
	if (!sm || !sm->core || !addr || size == 0) return 0xFFFFFFFFu;   // 0xFFFFFFFF = "no cursor for this ctx"
	if (size == 2) return gbacore_read16(sm->core, addr);
	return gbacore_read8(sm->core, addr);
}

#define TLOG_N 1024
#define TLOG_HEARTBEAT 30u     // while holding, still emit a row every ~0.5s even if nothing changed
typedef struct {
	uint32_t frame, ms;
	uint32_t cb2;
	uint32_t cursAddr;         // the ctx cursor addr (0 = none)
	uint32_t before, after;    // cursor value before/after the handler (0xFFFFFFFF = no cursor for this ctx)
	uint32_t taskFp[8];
	int16_t  sx, sy, gx, gy;
	uint16_t ret;              // injected GBA key mask (the action: A-pulse / D-pad walk dir / etc.)
	uint8_t  ctx, evt;         // evt: 0=press 1=drag/hold 2=release 3=heartbeat
	uint8_t  gvalid, touching, drag, resolved, nTask, cursSize;
} TLogEntry;
static TLogEntry s_tLog[TLOG_N];
static uint32_t  s_tLogN = 0;
// per-touch gesture tracking (also used to set the drag flag the log records).
static bool s_tWasTouch = false, s_tDrag = false;
static int  s_tDownSx = 0, s_tDownSy = 0;
static uint32_t s_tLastRet = 0xFFFFFFFFu, s_tLastCurs = 0xFFFFFFFFu, s_tLastBeat = 0;

void touch_log_reset(void) {
	s_tLogN = 0; s_fpLogN = 0; memset(&g_fieldDbg, 0, sizeof g_fieldDbg);
	memset(&g_touchDbg, 0, sizeof g_touchDbg);
	s_tWasTouch = false; s_tDrag = false;
	s_tDownSx = s_tDownSy = 0;
	s_tLastRet = 0xFFFFFFFFu; s_tLastCurs = 0xFFFFFFFFu; s_tLastBeat = 0;
}

// Record a row. Called from touch_update with the before/after cursor reads bracketing the dispatch.
static void touch_log_sample(const TouchSmart* sm, bool touching, bool newPress, int sx, int sy,
                             int gx, int gy, bool gvalid, uint16_t ret,
                             uint32_t cursAddr, int cursSize, uint32_t before, uint32_t after) {
	if (!sm || !sm->core) return;
	uint32_t frame = gbacore_frame_counter(sm->core);
	bool release = (!touching && s_tWasTouch);
	// gesture drag flag: movement since the press (mirrors the bag/walk move thresholds, generic 6px).
	if (newPress) { s_tDownSx = sx; s_tDownSy = sy; s_tDrag = false; }
	if (touching && (abs(sx - s_tDownSx) > 6 || abs(sy - s_tDownSy) > 6)) s_tDrag = true;

	// What makes a frame loggable: a fresh press, a release, the injected key changing, the cursor
	// changing, or a heartbeat while a hold/drag is sustained. (A steady hold with no change -> no row.)
	bool keyEdge  = touching && ((uint32_t)ret != s_tLastRet);
	bool cursEdge = touching && (after != s_tLastCurs);
	bool beat     = touching && (frame - s_tLastBeat >= TLOG_HEARTBEAT);
	bool loggable = newPress || release || keyEdge || cursEdge || beat;
	if (!loggable) { s_tWasTouch = touching; return; }

	s_tLastRet = touching ? (uint32_t)ret : 0xFFFFFFFFu;
	s_tLastCurs = touching ? after : 0xFFFFFFFFu;
	if (newPress || beat) s_tLastBeat = frame;

	uint8_t evt = newPress ? 0 : release ? 2 : (beat && !keyEdge && !cursEdge) ? 3 : 1;
	TLogEntry* e = &s_tLog[s_tLogN % TLOG_N];
	memset(e, 0, sizeof *e);
	e->frame = frame; e->ms = (uint32_t)osGetTime();
	e->cb2 = sm->cb2; e->resolved = sm->ctxResolved ? 1 : 0; e->nTask = sm->nTask;
	for (int i = 0; i < 8; i++) e->taskFp[i] = sm->taskFp[i];
	e->ctx = (uint8_t)sm->ctx; e->evt = evt;
	e->sx = (int16_t)sx; e->sy = (int16_t)sy; e->gx = (int16_t)gx; e->gy = (int16_t)gy;
	e->gvalid = gvalid ? 1 : 0; e->touching = touching ? 1 : 0; e->drag = s_tDrag ? 1 : 0;
	e->ret = ret; e->cursAddr = cursAddr; e->cursSize = (uint8_t)cursSize;
	e->before = before; e->after = after;
	s_tLogN++;
	s_tWasTouch = touching;
}

// PHASE 18 / SPEC-door T4.10. Appended to the touch log rather than given its own file: a route
// row and the touch rows that produced it belong in one place, and the harness already harvests
// this path. Nothing is written when no plan was ever made.
static void fplog_dump(FILE* f) {
	if (s_fpLogN == 0) return;
	static const char* const KN[4] = { "none", "door", "dir", "step" };
	static const char* const ON[5] = { "planned", "unreachable", "no-approach", "window", "badmap" };
	static const char* const EN[7] = { "-", "arrived", "moved", "timeout", "stalled", "replanned", "mapchange" };
	static const char* const DN[4] = { "R", "L", "D", "U" };
	fprintf(f, "\n# 3DGBA fieldpath PLAN log (phase 18 / SPEC-door): one row per planning attempt (row=plan)\n");
	fprintf(f, "#   and one per route end (row=end). THE DOOR PROOF: a tap on a door must read beh=0x69,\n");
	fprintf(f, "#   kind=door, term=U and an approach one tile SOUTH of the goal; the route then ends\n");
	fprintf(f, "#   'mapchange' (= the warp fired) and NOT 'timeout'. outcome=no-approach means the router\n");
	fprintf(f, "#   deliberately injected nothing because the door's south tile was blocked.\n");
	fprintf(f, "#   beh = the raw metatile behaviour we read (-1 = unreadable -> legacy behaviour);\n");
	fprintf(f, "#   pElev = the player elevation the walkability rule used (0 = rule disarmed);\n");
	fprintf(f, "#   warp = the destination map of the confirming warp EVENT (-1/-1 = none, so kind=none).\n");
	fprintf(f, "row,frame,map,px,py,goal,approach,kind,term,beh,pathLen,pElev,warp,outcome,head,end\n");
	uint32_t n    = (s_fpLogN < FPLOG_N) ? s_fpLogN : FPLOG_N;
	uint32_t base = (s_fpLogN < FPLOG_N) ? 0u : (s_fpLogN % FPLOG_N);
	for (uint32_t i = 0; i < n; i++) {
		const FpLogEntry* e = &s_fpLog[(base + i) % FPLOG_N];
		fprintf(f, "%s,%lu,%d/%d,%d,%d,%d/%d,%d/%d,%s,%s,%d,%d,%d,%d/%d,%s,%u,%s\n",
		        e->isEnd ? "end" : "plan", (unsigned long)e->frame, e->mapG, e->mapN,
		        e->px, e->py, e->goalX, e->goalY, e->appX, e->appY,
		        KN[e->kind & 3], (e->termDir < 4) ? DN[e->termDir] : "-",
		        e->behaviour, e->pathLen, e->pElev, e->warpG, e->warpN,
		        ON[(e->outcome < 5) ? e->outcome : 4], e->head, EN[(e->end < 7) ? e->end : 0]);
	}
}

void touch_log_dump(const char* path) {
	if (s_tLogN == 0) return;            // nothing captured -> don't litter SD with an empty file
	mkdir("sdmc:/cias", 0777);
	mkdir("sdmc:/cias/netlogs", 0777);
	FILE* f = fopen(path, "w");
	if (!f) return;
	fprintf(f, "# 3DGBA touch-event log  (one row per touch event: press / drag-hold / release / heartbeat)\n");
	fprintf(f, "# evt: 0=press 1=drag/hold 2=release 3=heartbeat. ctxName via the GameCtx enum.\n");
	fprintf(f, "# THE CURSOR PROOF: cursAddr=the ctx's authoritative game cursor (0=ctx has none); before/after=its value\n");
	fprintf(f, "#   read just BEFORE and AFTER the touch handler ran (FFFFFFFF=no cursor for this ctx). before!=after => touch moved it.\n");
	fprintf(f, "#   ctx->cursor: b.act=actionCursor b.move=moveCursor b.tgt=gMultiUsePlayerCursor party=gPartyMenu.slotId fmenu=sMenu.cursorPos bag=ListMenu row(+26).\n");
	fprintf(f, "#   field(overworld) has NO cursor -> read 'ret' (the injected key: walk dir / A). undetected screens (map/PokeNav/Pokemon-PC/move-learn/intro/Frontier) fall through to ctx=field/none with resolved=0 -> identify them by cb2 + t0..t7 (active task fps).\n");
	fprintf(f, "# coords: sx,sy=raw bottom-screen(320x240); gx,gy=mapped GBA px(0..239,0..159); gvalid=0 if off-frame. ret=injected GBA key mask. drag=moved>6px since press.\n");
	fprintf(f, "idx,frame,ms,evt,ctx,ctxName,resolved,touching,newPress,drag,gvalid,sx,sy,gx,gy,ret,keys,cursAddr,cursSz,before,after,changed,cb2,nTask,t0,t1,t2,t3,t4,t5,t6,t7\n");
	uint32_t n    = (s_tLogN < TLOG_N) ? s_tLogN : TLOG_N;
	uint32_t base = (s_tLogN < TLOG_N) ? 0u : (s_tLogN % TLOG_N);
	static const char* const EVN[] = { "press", "drag", "release", "beat" };
	for (uint32_t i = 0; i < n; i++) {
		const TLogEntry* e = &s_tLog[(base + i) % TLOG_N];
		char ks[12]; touch_keystr(e->ret, ks, sizeof ks);
		const char* cn = gamestate_ctx_name(e->ctx);
		const char* en = (e->evt < 4) ? EVN[e->evt] : "?";
		(void)en;
		int changed = (e->cursAddr && e->before != e->after) ? 1 : 0;
		fprintf(f, "%lu,%lu,%lu,%u,%u,%s,%u,%u,%u,%u,%u,%d,%d,%d,%d,%04X,%s,%08lX,%u,",
		        (unsigned long)i, (unsigned long)e->frame, (unsigned long)e->ms, e->evt, e->ctx, cn,
		        e->resolved, e->touching, (e->evt == 0) ? 1u : 0u, e->drag, e->gvalid,
		        e->sx, e->sy, e->gx, e->gy, e->ret, ks, (unsigned long)e->cursAddr, e->cursSize);
		if (e->cursAddr) fprintf(f, "%08lX,%08lX,%d", (unsigned long)e->before, (unsigned long)e->after, changed);
		else             fprintf(f, ",,");   // no cursor for this ctx -> empty before/after/changed
		fprintf(f, ",%08lX,%u,%08lX,%08lX,%08lX,%08lX,%08lX,%08lX,%08lX,%08lX\n",
		        (unsigned long)e->cb2, e->nTask,
		        (unsigned long)e->taskFp[0], (unsigned long)e->taskFp[1], (unsigned long)e->taskFp[2], (unsigned long)e->taskFp[3],
		        (unsigned long)e->taskFp[4], (unsigned long)e->taskFp[5], (unsigned long)e->taskFp[6], (unsigned long)e->taskFp[7]);
	}
	fplog_dump(f);
	fclose(f);
}

// --- PHASE 22.1: the keyboard/lists gdb mirror (LOGGING ONLY; touch.h documents offsets) -------
// Restamped every SMART frame with the same bus reads the handlers use. Non-static: the emutest
// harness resolves it by name out of 3DGBA.elf (the g_fieldDbg pattern).
TouchDbg g_touchDbg = { 0 };
static void touch_dbg_stamp(const TouchSmart* sm, u16 ret) {
	TouchDbg* d = &g_touchDbg;
	uint32_t seq = d->seq + 1;
	memset(d, 0, sizeof *d);
	d->seq = seq; d->ctx = sm->ctx; d->lastKeys = ret;
	const GameProfile* p = sm->prof;
	if (!sm->core || !p) return;
	if (sm->ctx == GCTX_NAMING && p->namingPtr) {
		uint32_t ptr = gbacore_read32(sm->core, p->namingPtr);
		d->nsPtr = ptr;
		d->nsGate = naming_gates(sm, &ptr);
		if (d->nsPtr >= 0x02000000u && d->nsPtr < 0x02040000u) {
			d->nsState    = gbacore_read8(sm->core, d->nsPtr + 0x1E10);
			d->nsPage     = gbacore_read8(sm->core, d->nsPtr + 0x1E22);
			d->nsCursorId = gbacore_read8(sm->core, d->nsPtr + 0x1E23);
			for (int i = 0; i < 16; i++)
				d->nsText[i] = gbacore_read8(sm->core, d->nsPtr + 0x1800 + (uint32_t)i);   // P1 proof
		}
		return;
	}
	uint32_t lb = 0;
	if (sm->ctx == GCTX_BAG)       lb = sm->bagListTaskBase;
	else if (sm->ctx == GCTX_LIST) { lb = sm->listBase; d->listKind = sm->listKind; }
	if (lb) {
		ListGeom g;
		d->listBase = lb;
		d->lTotal     = gbacore_read16(sm->core, lb + 12);
		d->lMaxShowed = gbacore_read16(sm->core, lb + 14);
		d->lWindowId  = gbacore_read8(sm->core, lb + 16);
		d->lScroll    = gbacore_read16(sm->core, lb + 24);
		d->lRow       = gbacore_read16(sm->core, lb + 26);
		if (list_read_geom(sm, lb, &g)) { d->lX0 = g.x0; d->lY0 = g.y0; d->lW = g.w; d->lH = g.h; }
	}
	if (sm->ctx == GCTX_LIST && sm->listKind == LK_DEX && p->dexView) {   // the L21 derivation channel
		uint32_t dv = gbacore_read32(sm->core, p->dexView);
		d->dexPtr = dv;
		if (dv >= 0x02000000u && dv < 0x02040000u) {
			d->dexCount    = gbacore_read16(sm->core, dv + 0x60C);
			d->dexSelected = gbacore_read16(sm->core, dv + 0x60E);
			d->dexInitVOff = gbacore_read8(sm->core, dv + 0x62B);
			d->dexListVOff = (int16_t)gbacore_read16(sm->core, dv + 0x62E);
		}
	}
	if (sm->ctx == GCTX_FULLUI && p->lmDummyTask) {   // the P-D discovery probe (FR dex + friends)
		for (int t = 0; t < 16; t++) {
			uint32_t task = p->gTasksBase + 40u * (uint32_t)t;
			if (gbacore_read8(sm->core, task + 4) == 0) continue;
			uint32_t fn = gbacore_read32(sm->core, task + 0) & ~1u;
			if (fn != p->lmDummyTask && (!p->lmDummyTaskAlt || fn != p->lmDummyTaskAlt)) continue;
			d->probeListBase  = task + 8u;
			d->probeTotal     = gbacore_read16(sm->core, task + 8u + 12);
			d->probeMaxShowed = gbacore_read16(sm->core, task + 8u + 14);
			d->probeWindowId  = gbacore_read8(sm->core, task + 8u + 16);
			break;
		}
	}
}

// ================================ dispatch ==================================
static void all_reset(void) { battle_reset(); walk_reset(); party_reset(); target_reset(); fmenu_reset(); list_reset(); naming_reset(); }

u16 touch_update(TouchMode mode, bool touching, int sx, int sy, int gx, int gy, bool gvalid,
                 const TouchSmart* sm) {
	static bool wasTouching = false;
	bool newPress = touching && !wasTouching;
	wasTouching = touching;

	if (mode == TOUCH_PAD)   { all_reset(); return touching ? pad_keys(sx, sy) : 0; }
	if (mode != TOUCH_SMART) { all_reset(); return 0; }            // TOUCH_OFF
	if (!sm || !sm->valid)   { all_reset(); return 0; }

	// LOGGING ONLY: read the ctx cursor BEFORE the dispatch (and AFTER, below) so a row shows old->new.
	uint32_t cursAddr; int cursSize;
	touch_cursor_addr(sm, &cursAddr, &cursSize);
	uint32_t before = touch_cursor_read(sm, cursAddr, cursSize);

	u16 ret = 0;
	switch (sm->ctx) {
	case GCTX_BATTLE_ACTION:
	case GCTX_BATTLE_MOVE: {
		walk_reset(); party_reset(); target_reset(); fmenu_reset(); list_reset(); naming_reset();
		uint32_t base = (sm->ctx == GCTX_BATTLE_ACTION) ? sm->actionAddr : sm->moveAddr;
		if (newPress && gvalid) {
			int cell = (sm->ctx == GCTX_BATTLE_ACTION) ? hit_action(gx, gy) : hit_move(gx, gy);
			if (cell >= 0) { s_target = cell; s_selTick = 0; }
		}
		ret = menu_select(sm->core, base);
		break;
	}
	case GCTX_BATTLE_TARGET:
		battle_reset(); walk_reset(); party_reset(); fmenu_reset(); list_reset(); naming_reset();
		if (newPress && gvalid) {
			int pos = hit_battler(gx, gy);
			if (pos >= 0) { int idx = battler_index_for_pos(sm, pos); if (idx >= 0) { s_tgt = idx; s_tgtTick = 0; } }
		}
		ret = select_pulse(sm->core, sm->prof ? sm->prof->multiCursor : 0, &s_tgt, &s_tgtTick);
		break;
	case GCTX_PARTY:
		battle_reset(); walk_reset(); target_reset(); fmenu_reset(); list_reset(); naming_reset();
		if (newPress && gvalid) {
			int slot = hit_party(gx, gy, sm->partyLayout);
			if (slot == 7 || (slot >= 0 && slot < sm->partyCount)) { s_party = slot; s_partyTick = 0; }
		}
		ret = select_pulse(sm->core, sm->prof ? sm->prof->partyMenu + 0x09 : 0, &s_party, &s_partyTick);
		break;
	case GCTX_OVERWORLD:
		battle_reset(); party_reset(); target_reset(); fmenu_reset(); list_reset(); naming_reset();
		ret = walk_update(touching, newPress, gvalid, gx, gy, sm->px, sm->py,
		                  sm->mapGroup, sm->mapNum, sm->core, sm->prof);
		break;
	case GCTX_FIELDMENU:
		battle_reset(); walk_reset(); party_reset(); target_reset(); list_reset(); naming_reset();
		if (newPress && gvalid && sm->prof) { int i = hit_fieldmenu(sm->core, sm->prof, gx, gy); if (i >= 0) { s_fmenu = i; s_fmenuTick = 0; } }
		ret = sm->prof ? fmenu_select(sm->core, sm->prof) : 0;
		break;
	case GCTX_BAG:
		battle_reset(); walk_reset(); party_reset(); target_reset(); fmenu_reset(); naming_reset();
		if (s_lPrevKind != -2) { list_reset(); s_lPrevKind = -2; }   // arriving from another ctx/kind
		ret = list_update(sm, sm->bagListTaskBase, LF_BAG, touching, newPress, gvalid, gx, gy);
		break;
	case GCTX_LIST:
		battle_reset(); walk_reset(); party_reset(); target_reset(); fmenu_reset(); naming_reset();
		// a kind change (buy -> qty -> buy) mid-context resets the shared gesture state
		if (s_lPrevKind != (int)sm->listKind) { list_reset(); s_lPrevKind = (int)sm->listKind; }
		switch (sm->listKind) {
		case LK_BUY:
		case LK_PCITEM: ret = list_update(sm, sm->listBase, 0, touching, newPress, gvalid, gx, gy); break;
		case LK_QTY:    ret = qty_update(sm, touching, newPress, gvalid, gx, gy); break;
		case LK_DEX:    ret = dex_update(sm, touching, newPress, gvalid, gx, gy); break;
		default:        ret = 0; break;               // unknown kind: emit nothing (L10)
		}
		break;
	case GCTX_NAMING:
		battle_reset(); walk_reset(); party_reset(); target_reset(); fmenu_reset(); list_reset();
		ret = naming_update(sm, touching, newPress, gvalid, gx, gy, sx, sy);
		break;
	case GCTX_BATTLE_OTHER:
		all_reset();
		ret = touching ? (1 << GBAKEY_A) : 0;                      // battle dialog/animation: tap = advance
		break;
	default:
		all_reset();
		ret = 0;
		break;
	}

	touch_dbg_stamp(sm, ret);   // PHASE 22.1 gdb mirror (LOGGING ONLY)

	// LOGGING ONLY: read the cursor AFTER, then record the event (covers every switch case incl. the
	// default/undetected fall-through; cursAddr==0 there -> cb2/tasks are the fingerprint).
	uint32_t after = touch_cursor_read(sm, cursAddr, cursSize);
	touch_log_sample(sm, touching, newPress, sx, sy, gx, gy, gvalid, ret, cursAddr, cursSize, before, after);
	return ret;
}

void touch_draw(TouchMode mode, u16 held, const TouchSmart* sm, C2D_TextBuf buf) {
	(void)sm;
	if (mode == TOUCH_PAD) pad_overlay(held, buf);
	else if (mode == TOUCH_SMART && buf) {
		// SPEC-layout L6 (sweep D17). This branch ALREADY existed and was simply never reached:
		// main.c's call site read `if (tmEff == TOUCH_PAD) touch_draw(...)`, so Smart mode drew no
		// chip at all while a raw cyan `field p=9,4 key=-` developer readout sat on the footer.
		// Smart draws no BUTTONS by design (the point is that you touch the real game UI) — but it
		// must still say what mode it is in and offer a way back to the menu (screenshot 07).
		touch_chip(buf, "TOUCH · SMART POINTER", 160.0f, (float)UIHIT_TOUCH_CHIP_Y, C2D_Color32(0xF5, 0xD0, 0x42, 0xE6));
		touch_menu_chip_draw(buf, C2D_Color32(0xF5, 0xD0, 0x42, 0xC8));
	}
}
