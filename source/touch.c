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
#include "fieldtrav.h" // phase 22.2: HM-aware conditional-edge planning (pure C, host-tested)
#include "uihit.h"     // phase 19 / L3.2.8: the chip boxes, shared with the host suite
#include "touchgeom.h" // phase 22.1: keyboard + list hit geometry (pure C, host-tested)
#include "progseq.h"   // phase 25 / audit O2: the interact sequencer (pure C, host-tested)
#include "excseq.h"    // phase 25 / audit O2: the excursion leg machine (pure C, host-tested)
#include "progtap.h"   // phase 28 / audit O3: the tap gate's decision (pure C, host-tested)

const char* const TOUCH_NAMES[3] = { "Off", "Gamepad", "Smart" };

// PHASE 24 / lane B1: touchgeom.c must host-compile without gamestate.h (which pulls gbacore.h and
// with it libctru), so DLGGEOM_CTX_FIELD mirrors the enum value. Pin the two together HERE, the
// same way main.c:1220 pins TILT_CTX_FIELD — if GameCtx is ever re-ordered this fires at compile
// time instead of silently routing field dialogs back into the walker.
_Static_assert(GCTX_OVERWORLD == DLGGEOM_CTX_FIELD,
               "touchgeom.h DLGGEOM_CTX_FIELD drifted from GameCtx GCTX_OVERWORLD");
// PHASE 24 / lane A2: the same mirror, for the same reason, for the walk-vs-run engine split.
// touchgeom.c cannot include fieldpath.h's FpEngine without dragging the router into a geometry
// unit test, so it carries its own two names — and this pins them.
_Static_assert((int)FP_ENG_RSE == RUNGEOM_ENG_RSE && (int)FP_ENG_FRLG == RUNGEOM_ENG_FRLG,
               "touchgeom.h RUNGEOM_ENG_* drifted from fieldpath.h FpEngine");

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
// QUICK TAP a tile -> route there over the live grid. The camera centers the player at screen
// tile (7,5).
// PHASE 24 / lane A2 — the OWN TILE now has its own two verbs (DECISIONS-overworld-gestures.md D1):
// TAP yourself = START, HOLD yourself = SELECT. It used to be tap = A / double-tap = START.
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
// PHASE 24 / lane A2: the double-tap survives ONLY on the no-map screens (px < 0 — title, intro,
// main menu), where there is no player tile and decision D1 therefore does not apply. On the
// overworld it is GONE, replaced by tap = START / hold = SELECT (touchgeom.h owngest_step), for
// the reason D1 records: a double-tap binding makes EVERY single tap wait out its window.
#define DOUBLE_FRAMES 16  // (no-map screens only) second tap within this many frames => START
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
static int  s_tick = 0, s_lastSelfTap = -999, s_startPulse = 0;   // (px<0 screens only) double-tap -> START
static int  s_npcN = 0; static short s_npcG[16][2];              // active object-event grid coords (+7 space)
// PHASE 24 / lane A2, decision D1: the OWN-TILE gesture. tap = START (on release), hold = SELECT
// (the moment the threshold is crossed). The resolver is pure and host-graded (touchgeom.c
// owngest_step); this is only its state plus the two pulses it queues.
static OwnGest s_own;
static int  s_selPulse = 0;
// PHASE 24 / lane A2, decision D2: does THIS leg run? Latched by walk_plan (distance +
// eligibility) and re-tested against the live gates on every frame B would actually be held.
static bool s_runLeg = false;
static void walk_reset(void) {
	s_aPulse = 0; s_startPulse = 0; s_selPulse = 0; s_walking = false; s_pathLen = s_pathPos = 0;
	s_touchFrames = 0; s_moved = false;
	memset(&s_own, 0, sizeof s_own); s_runLeg = false;
	s_termActive = false; s_termDir = FP_NODIR; s_termFrames = 0; s_kind = FP_WK_NONE; s_replans = 0;
}

// --- the gdb/harness mirror (LOGGING ONLY; nothing reads it back) -------------------------------
// SPEC-door T4.11. A screenshot cannot prove a warp fired — a door animation without a warp looks
// the same for several frames — so the objective instrument is this struct plus the game's own
// SaveBlock1.location, read over the emutest gdb channel (`run gdbio read-u32 g_fieldDbg+N`,
// `poll --changed`, `see rec --with-state g_fieldDbg`). planSeq increments once per planning
// ATTEMPT so a poll can latch on it. Deliberately non-static: the harness resolves it by name out
// of 3DGBA.elf.
// PHASE 29 / lane F: `planForced` is an INDEX, so its "nothing found" value is -1, not 0. Stated
// at the definition (and re-stated at the all_reset memset) because a zeroed mirror that means
// "refused at step 0" would be a false positive on the very first read of a fresh boot.
FieldDbg g_fieldDbg = { .planForced = -1 };
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

// ...and the SAVE side, which does NOT follow the behaviour side (fix, 2026-08-14). Ruby and
// Sapphire share Emerald's metatile numbering — so `fp_engine` is right to answer FP_ENG_RSE for
// AXVE/AXPE — but they do NOT share its SaveBlock1.flags offset (0x1220 vs 0x1270) or its
// SYSTEM_FLAGS base (0x800 vs 0x860). Every badge / Running-Shoes read therefore goes through the
// TITLE, not the engine: fieldtrav.h owns the mapping, and this is its one call site.
static FtVariant ft_variant(const GameProfile* p) {
	return p ? fieldtrav_variant(p->code) : FT_VAR_EMERALD;
}

// PHASE 24 / lane A2 (decision D2). Defined with the other live game reads further down (they all
// share prog_sb1 / fp_r8), but CALLED from walk_plan and walk_update_inner, which come first.
static unsigned run_elig(GbaCore* core, const GameProfile* p);          // the five run gates
static u16 run_key(GbaCore* core, const GameProfile* p, bool runLeg);   // ...and the B they earn

// Plan a route to the tapped tile. Returns true and arms the follow loop; false means NOTHING is
// injected — which for a door with no reachable approach is the whole point (a documented no-op
// beats tackling the wall next to it).
// ---- PHASE 23 / SPEC-family-traversal §3: the EXCURSION leg machine --------------------------
// A cross-map route is not one plan, it is a PROGRAM of legs, and the executor's whole job is to
// notice a leg boundary and re-plan the next one FROM REALITY (H3.4). The legs themselves are
// ordinary fieldpath routes — walking to a warp with its classified terminal is exactly what the
// shipped router already does — so this adds a supervisor, not a second walker.
//
//   leg 0  current map  -> the OUT warp (Wi)
//   leg 1  the interior -> the RETURN warp (Wj)          [re-planned live on arrival]
//   leg 2  current map  -> the tapped goal               [re-planned live on arrival]
//
// EVERY transition is gated on SaveBlock1.location becoming EXACTLY the map the plan predicted.
// Any other map, or a leg that cannot be re-planned live (an NPC camped in the doorway), ends the
// excursion where it stands: visible, honest, recoverable. Never a wander (H3.5).
static FtExcursion s_exc;
// PHASE 25 / audit O2: the leg SUPERVISOR itself is `source/excseq.c` — pure, host-tested, and the
// shipped one (there is no second copy). What stays here is what it is not allowed to do: read the
// game, run a BFS, or draw. Its whole state is this struct.
// DESIGNATED, not positional: fieldtrav.h's own comment names the trap ("the two tables are
// positionally initialised, so a new field may only go at the END") and a state struct should not
// inherit it. Everything not named here is 0, and every field is read only once `on` is set.
static ExcSeq s_excS = { .homeG = -1, .homeN = -1, .curG = -1, .curN = -1,
                         .pendG = -1, .pendN = -1, .lastW = -1, .lastH = -1 };
static int  s_excSeq = 0;
static const char* s_excChip = 0;

static void exc_reset(void) {
	excseq_reset(&s_excS);
	s_excChip = 0;
}
// "VIA DOOR - LEG n/3" (T4.2). The leg counter is the honest v1: map NAMES would need the ROM
// region-map strings, which is its own slice.
static const char* exc_chip_for(int leg) {
	switch (leg) {
	case 0:  return "VIA DOOR - LEG 1/3";
	case 1:  return "VIA DOOR - LEG 2/3";
	default: return "VIA DOOR - LEG 3/3";
	}
}
static void exc_dbg_stamp(void) { g_fieldDbg.progMapSeq = s_excS.leg; }
// Forward declarations: the two leg-machine entry points are DEFINED next to prog_plan (they need
// the bus helpers that live there) but are CALLED from walk_update_inner, which comes first.
static int  exc_plan(GbaCore* core, const GameProfile* p, int px, int py, int gx, int gy,
                     int mapG, int mapN);
static void exc_leg_boundary(GbaCore* core, const GameProfile* p, int px, int py,
                             int mapG, int mapN);

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
	// PHASE 29 / lane F — DEFECT X2, and SPEC-hm-waterfall §4.2's refusal applied where the FROZEN
	// router's answer is consumed. `fieldpath` has no forced-movement table: a waterfall and each
	// of the four currents is collision 0 and elevation-compatible with the water around it, so the
	// tier-0 BFS plots a straight swim UP a fall and reports ARRIVED at a tile the player never
	// reaches. Measured live (phase 28, Route 114): a fall tap with Waterfall unusable produced
	// `plan pathLen=4 end=ARRIVED` with the player never leaving (12,13).
	//
	// This is the ONE place to fix it. Every leg of every route in this app is planned through this
	// function — a tap, a re-plan, an excursion leg, a traversal program's hand-off — and the
	// screen therefore covers the SHIPPED DEFAULT (`smartTraverse = 0`) as well as the tap gate's
	// `gate == 2` fallback, which is exactly what §4.2 says is otherwise unreachable. The gate does
	// NOT restate the rule: one implementation, one copy (the phase-28 audit's O2 lesson).
	{
		int forced = fieldtrav_path_forced(&bus, &m, px, py, pl.path, pl.pathLen);
		g_fieldDbg.planForced = forced;
		if (forced >= 0) {
			g_fieldDbg.planForcedN++;
			return false;   // refuse the whole plan: a route through a flush is not a route
		}
	}
	s_mapW = w; s_mapH = h; s_mapPtr = ptr; s_mapG = mapG; s_mapN = mapN;
	s_goalX = pl.goalX; s_goalY = pl.goalY;
	s_appX = pl.approachX; s_appY = pl.approachY;
	s_kind = pl.kind; s_termDir = pl.termDir;
	s_pathLen = pl.pathLen; s_pathPos = 0;
	for (int i = 0; i < pl.pathLen; i++) s_pathDir[i] = pl.path[i];
	s_termActive = false; s_termFrames = 0;
	// PHASE 24 / lane A2 (decision D2): "close = walk, far = run", decided HERE — i.e. once per
	// LEG, because every leg of every route in this app is planned through this one function
	// (a tap, a re-plan, an excursion leg, a traversal program's hand-off). Deciding it anywhere
	// else would be deciding it per ROUTE, which D2 explicitly rules out.
	{
		unsigned elig = run_elig(core, p);
		s_runLeg = rungeom_decide(pl.pathLen, elig) != 0;
		g_fieldDbg.runElig = (int32_t)elig;
		g_fieldDbg.runLeg  = s_runLeg ? 1 : 0;
	}
	return true;
}

static int prog_plan(GbaCore* core, const GameProfile* p, int px, int py, int gx, int gy,
                     int mapG, int mapN);   // fwd: the phase-22.2 conditional-edge planner
static bool prog_surfing(GbaCore* core, const GameProfile* p);   // fwd: gPlayerAvatar surf bit
static int  prog_facing(GbaCore* core, const GameProfile* p);    // fwd: gObjectEvents[0] facing
static int  prog_tap_gate(GbaCore* core, const GameProfile* p, int px, int py, int* gx, int* gy);
static int  prog_retargeted_goal(int* gx, int* gy);              // fwd: the last plan's real goal
static bool prog_strength_on(GbaCore* core, const GameProfile* p);   // fwd: the live latch
// PHASE 28 / lane X: the object slot the last STRENGTH plan aimed at, so the live mirror can watch
// the boulder that is supposed to STAY PUT. Latched at plan time, kept afterwards (the question is
// asked once the program has ended); -1 until a Strength plan happens.
static int  s_strObj = -1;

static u16 walk_update_inner(bool touching, bool newPress, bool gvalid, int gx, int gy,
                             int px, int py, int mapG, int mapN, GbaCore* core, const GameProfile* p,
                             int traverse) {
	s_tick++;
	if (s_startPulse > 0) { s_startPulse--; return 1 << GBAKEY_START; }
	if (s_selPulse > 0)   { s_selPulse--;   return 1 << GBAKEY_SELECT; }
	if (s_aPulse > 0)     { s_aPulse--;     return 1 << GBAKEY_A; }

	if (newPress && gvalid) { s_downGx = gx; s_downGy = gy; s_downPx = px; s_downPy = py; s_touchFrames = 0; s_moved = false;
	                          s_downMapG = mapG; s_downMapN = mapN; }
	if (touching && gvalid) { s_touchFrames++;
	                          /* H4: was 8 px — a stylus number. This gates the H1 steer promotion,
	                             so a tap that drifted 9 px used to become a directional hold. */
	                          if (abs(gx - s_downGx) > LISTGEOM_SLOP_PX || abs(gy - s_downGy) > LISTGEOM_SLOP_PX) s_moved = true; }

	// No loaded overworld map (title / intro / main menu): a tap = A, a double-tap = START. No
	// walking — and NOT decision D1 either: D1 is about the player's own TILE, and on a screen with
	// no player there is no such tile. A gesture must never straddle the boundary, so the own-tile
	// machine is cleared here rather than left half-armed for the frame the map finishes loading.
	if (px < 0) {
		memset(&s_own, 0, sizeof s_own);
		if (!touching && s_touchFrames > 0) {
			bool tap = s_touchFrames <= TAP_FRAMES && !s_moved; s_touchFrames = 0;
			if (tap) { if (s_tick - s_lastSelfTap < DOUBLE_FRAMES) s_startPulse = 3; else s_aPulse = 3; s_lastSelfTap = s_tick; }
		}
		if (s_startPulse > 0) { s_startPulse--; return 1 << GBAKEY_START; }
		if (s_aPulse > 0)     { s_aPulse--;     return 1 << GBAKEY_A; }
		return 0;
	}

	// ---- PHASE 24 / lane A2, decision D1: the OWN-TILE gesture --------------------------------
	// TAP your own tile -> START (the field menu), on RELEASE. HOLD your own tile -> SELECT (the
	// registered item), the moment the threshold is crossed, and the release that ends it stays
	// silent. This REPLACES `tap-self = A` and `double-tap-self = START`.
	//
	// Resolved from the PRESS-TIME tile (s_downGx/s_downGy), for the same reason the tap-to-walk
	// arm below uses it: the camera is anchored on the player when the finger goes down, so that
	// is the anchor the user pointed with — and a hold that drifts a pixel is still the same
	// gesture. It runs BEFORE the steer and the tap-to-walk arms so exactly one of the three can
	// claim a gesture, and it cancels any route in flight: pressing START while the avatar is
	// walking somewhere you no longer want is the normal way to say "stop".
	{
		// PHASE 30 / H3: a radius about the avatar, not the exact tile — the tile is 16x16 SCREEN px
		// at 1:1 and a fingertip is ~3x that, so START/SELECT were unhittable on hardware.
		int onSelf = owngeom_on_self(s_downGx, s_downGy);
		int ev = owngest_step(&s_own, touching ? 1 : 0, (newPress && gvalid) ? 1 : 0,
		                      onSelf, s_moved ? 1 : 0);
		if (ev == OWNG_SELECT)     { s_selPulse = 3;   g_fieldDbg.ownSelects++; }
		else if (ev == OWNG_START) { s_startPulse = 3; g_fieldDbg.ownStarts++; }
		if (ev != OWNG_NONE) {
			s_touchFrames = 0;                       // spent: it must not ALSO arm a route below
			if (s_walking) route_end(core, FDBG_END_CANCELLED);
			s_walking = false; s_termActive = false;
		}
	}
	if (s_startPulse > 0) { s_startPulse--; return 1 << GBAKEY_START; }
	if (s_selPulse > 0)   { s_selPulse--;   return 1 << GBAKEY_SELECT; }

	if (touching && gvalid) {                            // HOLD -> steer toward the touch (cancels a route)
		// PHASE 30 — HARDWARE DEFECT H1. This arm used to fire on the FIRST frame of every touch,
		// so it was not a HOLD at all: it was "any contact". The header two hundred lines up has
		// always described the intended split ("QUICK TAP a tile -> route there; HOLD / SLIDE ->
		// steer"), and the release handler below already implements the tap half against
		// TAP_FRAMES — but nothing ever gated the steer half, so both halves ran on every tap.
		//
		// It never showed up in the emulator because a synthesized tap is 1-2 frames and the
		// harness releases on a known frame. A human thumb on the real digitiser is ~5-9 frames,
		// every one of which emitted a direction key AND ran `s_walking = false`, so a tap:
		//   * walked the avatar a tile off the tile you pointed at, before the route was planned,
		//   * cancelled any route already in flight, and
		//   * made the screen behave as four directional quadrants (the `abs(ddx) > abs(ddy)`
		//     split below) instead of a pointer — which is what a tap-to-walk UI must never do.
		// The user reported exactly that, in those words, from hardware.
		//
		// The gate is the SAME pair the release handler uses to define a tap, so the two arms can
		// no longer disagree: while a gesture could still turn out to be a tap, this arm emits
		// nothing and cancels nothing. Movement past the slop promotes to a steer immediately,
		// which keeps a deliberate slide responsive.
		if (s_touchFrames <= TAP_FRAMES && !s_moved) return 0;
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
			// PHASE 24 / lane A2: the OWN tile is decision D1's, resolved above and never here.
			// The guard is belt-and-braces — an own-tile gesture zeroes s_touchFrames when it
			// fires, so this branch cannot be reached with (0,0) — but it says out loud that
			// exactly one handler owns a self-tap, which is the property that broke when the old
			// double-tap window and the route arm both looked at the same release.
			if (ddx == 0 && ddy == 0) {
				/* nothing: START / SELECT already resolved it */
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
				// PHASE 26 / lane W. The one question that has to be asked BEFORE the dry router
				// rather than after it (see prog_tap_gate): a tap on a waterfall tile, a mid-surf
				// tap aimed UPWARD, or a tap on a boulder — the three taps where tier 0 either
				// answers WRONGLY (it will plot a swim up a fall) or answers so well that the
				// interaction can never be reached (a blocked goal is a legal terminal). Everything
				// else keeps the shipped tier order exactly — gate == 0.
				int tgx = s_downPx + ddx, tgy = s_downPy + ddy;
				int gate = (traverse >= 1) ? prog_tap_gate(core, p, px, py, &tgx, &tgy) : 0;
				int ftr  = (gate > 0) ? prog_plan(core, p, px, py, tgx, tgy, mapG, mapN) : 0;
				if (gate < 0) {
					/* a waterfall column we cannot resolve: plan NOTHING rather than hand the tap
					   to a router that would swim into the fall and be flushed back down. */
				} else if (ftr == 1) {
					/* armed by the conditional planner */
				} else if (gate == 2) {
					// The tapped tile IS a fall, and the conditional planner declined (no badge, no
					// mon, or the top already reached). The raw tile can never go to the dry router,
					// so fall back to the EFFECTIVE goal fieldtrav resolved — the top of the column
					// — and only if it really retargeted one. Otherwise: nothing, which is what the
					// user sees today for an unreachable tap.
					// PHASE 29 / lane F — DEFECT X2. This branch used to hand that top to the FROZEN
					// router, which has no forced-movement table and answered with a 4-step swim UP
					// the column, reporting ARRIVED from a player who never moved. The refusal is
					// NOT restated here: `walk_plan` screens every path it returns
					// (fieldtrav_path_forced), so the swim is refused at the one place every route
					// in the app is planned — including the shipped default, where this gate does
					// not run at all. If a route to the top exists that does not cross the fall, it
					// still ships, which is the behaviour SPEC-hm-waterfall §4.2 asked for.
					int rgx, rgy;
					if (prog_retargeted_goal(&rgx, &rgy) &&
					    walk_plan(core, p, px, py, rgx, rgy, mapG, mapN)) {
						s_walking = true; s_lpx = px; s_lpy = py; s_stall = 0; s_replans = 0;
					}
				} else if (walk_plan(core, p, px, py, tgx, tgy, mapG, mapN)) {
					s_walking = true; s_lpx = px; s_lpy = py; s_stall = 0; s_replans = 0;
				} else if (traverse >= 1) {
					// PHASE 22.2 / SPEC-family-traversal H1.7 — the TIER ORDER, made structural:
					// the conditional-edge planner is only ever consulted when the ordinary dry
					// walk has ALREADY failed. A wet or destructive detour can therefore never
					// displace a walkable route, however much longer the walk would have been.
					// Declining (return 0) leaves the tap doing exactly what it does today.
					// (`gate > 0` means it has ALREADY been consulted above, and re-running the
					// same BFS to get the same answer would only cost a frame.)
					if (gate == 0) ftr = prog_plan(core, p, px, py, tgx, tgy, mapG, mapN);
					if (ftr == 0 && traverse >= 2) {
						// PHASE 23 / SPEC H3.2: excursions are the LAST tier, consulted only once
						// the dry walk AND the conditional-edge planner have both failed — so a
						// same-map route can never be displaced by a trip through a building.
						exc_plan(core, p, px, py, tgx, tgy, mapG, mapN);
					}
				}
			}
		}
		if (s_startPulse > 0) { s_startPulse--; return 1 << GBAKEY_START; }
		if (s_aPulse > 0)     { s_aPulse--;     return 1 << GBAKEY_A; }
	}

	// PHASE 24: a leg boundary is a MAP CHANGE, and it has to be noticed whether or not a route is
	// still being followed. The LAST leg's terminal is a STEP warp: the walker arrives on the warp
	// tile, declares the route finished (s_walking = false) and only THEN does the warp fire — so
	// the kill-switch inside the follow loop below never saw it, and the excursion parked on the
	// terrace it had just stepped out onto. One watcher, for the whole machine.
	if (excseq_boundary_due(&s_excS, mapG, mapN)) {
		s_walking = false; s_termActive = false;
		exc_leg_boundary(core, p, px, py, mapG, mapN);
	}

	// PHASE 24: the armed leg of an excursion, planned as soon as the arrival map is really loaded
	// (see exc_leg_boundary). Retried on a cadence rather than every frame — each attempt is a BFS
	// on the render thread — and given up on loudly instead of leaving the player parked. The rule
	// for WHEN is excseq's (pure, host-graded, including the dims-stability half); the map read and
	// the BFS are the two things it is not allowed to do, so they stay here.
	if (s_excS.on && s_excS.pend && !s_walking && core && p) {
		int mw = 0, mh = 0; uint32_t mp = 0;
		bool haveMap = map_read(core, p, &mw, &mh, &mp);
		if (excseq_settle_try(&s_excS, mapG, mapN, px, haveMap ? 1 : 0, mw, mh) == EXC_S_PLAN &&
		    walk_plan(core, p, px, py, s_excS.pendX, s_excS.pendY, mapG, mapN)) {
			s_walking = true; s_lpx = px; s_lpy = py; s_stall = 0; s_replans = 0;
			s_mapG = mapG; s_mapN = mapN;      // the leg is walked on THIS map now
			excseq_settle_planned(&s_excS, mapG, mapN);
		} else {
			if (excseq_settle_wait(&s_excS)) s_excChip = 0;   // gave up: the chip goes with it
		}
	}
	// The last leg's route ending IS the end of the excursion — otherwise the machine (and its
	// "VIA DOOR - LEG 3/3" chip) would linger until some later map change happened to clear it.
	if (excseq_home_done(&s_excS, s_walking ? 1 : 0)) s_excChip = 0;

	if (!s_walking || !core || !p) return 0;            // path-follow (released, routing)
	int w, h; uint32_t ptr;
	// THE WARP KILL-SWITCH (T4.6). SaveBlock1.location changing IS the proof the warp fired, and it
	// is the only reliable signal: gBackupMapLayout.map is a FIXED EWRAM buffer, so the old
	// ptr/w/h check cannot see a same-size map swap and a stale route survives into the arrival
	// map. Both nets are kept.
	if (mapG != s_mapG || mapN != s_mapN) {
		s_walking = false; s_termActive = false; route_end(core, FDBG_END_MAPCHANGE);
		// PHASE 23/24: for an excursion this is the END OF A LEG, not the end of the route — but
		// the boundary is now watched ABOVE, unconditionally, because the last leg's terminal is a
		// STEP warp the walker finishes before the warp fires (so this branch never saw it).
		return 0;
	}
	if (!map_read(core, p, &w, &h, &ptr) || ptr != s_mapPtr || w != s_mapW || h != s_mapH) {
		s_walking = false; s_termActive = false; route_end(core, FDBG_END_MAPCHANGE);
		// PHASE 24: for an excursion this is recoverable — the LAYOUT changed under a leg that was
		// planned a moment too early. Re-arm the same leg (its target is still held) and let the
		// settle rule above plan it again on the finished map, instead of throwing away a route
		// that is two doors along.
		excseq_layout_kill(&s_excS);
		return 0;
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
	// PHASE 24 / lane A2 (decision D2) — THE ONE PLACE THE ROUTE FOLLOWER PRESSES B. Only on a
	// plain path step, never on the TERMINAL HOLD above (a door/warp hold is one tile: there is no
	// speed to win, and B at a door is a keypress with a meaning of its own), and never anywhere
	// the sequencer is talking to a script. run_key re-asks the live gates every frame, exactly as
	// the engine does, so this degrades to a plain walk the moment any of them closes.
	return s_keyDir[d] | run_key(core, p, s_runLeg);
}

// LOGGING ONLY. Everything latched in g_fieldDbg above is from PLAN time; this stamps the LIVE
// state every overworld frame, which is what makes a warp provable from outside the emulated
// console — the harness reads the game's own SaveBlock1.location over gdb and watches it change.
static u16 walk_update(bool touching, bool newPress, bool gvalid, int gx, int gy,
                       int px, int py, int mapG, int mapN, GbaCore* core, const GameProfile* p,
                       int traverse) {
	u16 k = walk_update_inner(touching, newPress, gvalid, gx, gy, px, py, mapG, mapN, core, p, traverse);
	g_fieldDbg.curMapGroup = mapG; g_fieldDbg.curMapNum = mapN;
	g_fieldDbg.curPx = px; g_fieldDbg.curPy = py;
	g_fieldDbg.curKeys = k;
	g_fieldDbg.curFrame = core ? (int32_t)gbacore_frame_counter(core) : 0;
	g_fieldDbg.walking = s_termActive ? 2 : (s_walking ? 1 : 0);
	// PHASE 24: the surf bit belongs in the LIVE half. It was only written from inside a running
	// program, so the one question a Surf proof asks — "is the player afloat NOW?" — was
	// unreadable the moment the program ended. It is a read of the game's own gPlayerAvatar.
	g_fieldDbg.progSurf = prog_surfing(core, p) ? 1 : 0;
	g_fieldDbg.progFacing = prog_facing(core, p);   // ...and which way we are aiming an A
	// PHASE 28 / lane X: the two never-executed HMs' OWN observables, in the live half for the same
	// reason the surf bit is — they are asked about exactly when the program is over. See touch.h.
	g_fieldDbg.progBeh   = (core && p && p->mapObjects)
	                     ? (int32_t)gbacore_read8(core, p->mapObjects + 0x1Eu) : -1;
	g_fieldDbg.progLatch = prog_strength_on(core, p) ? 1 : 0;
	g_fieldDbg.progObjSlot = s_strObj;
	if (s_strObj >= 0 && core && p && p->mapObjects) {
		uint32_t b = p->mapObjects + 0x24u * (uint32_t)s_strObj;
		g_fieldDbg.progObjX = (int32_t)(int16_t)gbacore_read16(core, b + 0x10u) - 7;
		g_fieldDbg.progObjY = (int32_t)(int16_t)gbacore_read16(core, b + 0x12u) - 7;
	}
	return k;
}

// ================= SMART: the TRAVERSAL family (HM-aware route programs) =================
// PHASE 22.2 / SPEC-family-traversal. The user's ask: "if I touch a place in the overworld which
// requires a HM ... that the player auto does that as well (surfs, break brick, cut etc)."
//
// The planning half is pure C in fieldtrav.c (host-tested); THIS is the executor — the closed-loop
// program runner that sits ABOVE the unchanged single-leg walker above. Three properties matter
// more than any feature here:
//
//  1. IT NEVER ANSWERS A PROMPT IT DID NOT PREDICT. Every A press is aimed at an object whose
//     graphicsId AND eligibility (badge + a party mon that really knows the move) we checked
//     first, and the YES is only written when the yes/no we expected actually appeared. If a
//     different dialog shows up, one A closes it and the WHOLE program dies. That is the safety
//     property this family lives or dies by: a stray YES in Gen 3 can sell an item or delete a
//     save file.
//  2. IT YIELDS. The injection seam is ADDITIVE (COVERAGE §5) — we cannot suppress the player —
//     so any physical key, any new touch, any context change, any budget expiry stops the program
//     dead, injects nothing further, and leaves the avatar wherever it got to. Visible, honest,
//     recoverable.
//  3. IT IS NEVER FRAME-COUNT-BLIND. Every wait is on a real observable read out of the game:
//     sFieldMessageBoxMode for "the script is talking", the yes/no task for the decision point,
//     the tree's OWN object slot going inactive for "the Cut landed", gPlayerAvatar's surf bit for
//     "we are afloat". Budgets exist only to give up, never to advance.
// The phases (TPH_*), the end reasons (TPE_*) and every budget/cadence now live in progseq.h with
// the machine that uses them — PHASE 25 / audit O2. This file keeps the reads, the writes, the BFS
// and the HUD; `progseq.c` keeps the decisions, and is host-graded frame by frame.

static FtProgram s_prog;                 // ~17 KB: static for the fieldpath reason (render thread)
static ProgSeq s_seq;                    // the sequencer's whole state (pure; see progseq.h)
static bool s_progSwallow = false;
static int  s_progReplans;
static int  s_progMapG = -1, s_progMapN = -1, s_progGoalX, s_progGoalY;
static int  s_progSeq = 0, s_progEndSeq = 0;
static int  s_progChipHold = 0;
// PHASE 23: the "one A to close a dangling textbox" of SPEC T2.4 has to OUTLIVE the program, and
// the sequencer's own A-pulse cannot — the end clears `s_seq.on`, and from the very next frame the
// dispatcher stops calling prog_update at all, so a pulse queued alongside the end is never
// emitted. Live evidence: the Battle Frontier surf prompt sat open with YES highlighted and
// nothing ever pressed it. This counter is drained ABOVE the s_seq.on gate, so the farewell lands.
static int  s_progFarewell = 0;
static const char* s_progChip = 0;

static void prog_reset(void) {
	progseq_reset(&s_seq);
	s_progSwallow = false; s_progFarewell = 0;
	s_progReplans = 0;
	s_progMapG = s_progMapN = -1;
}

// --- the live reads the sequencer closes its loop on ------------------------------------------
// SaveBlock1, resolved the one way gamestate.c resolves it (sbDirect = Ruby/Sapphire, where the
// column IS the struct rather than a pointer to it — gamestate.c game_read, call site 1 of 2).
static uint32_t prog_sb1(GbaCore* core, const GameProfile* p) {
	if (!core || !p || !p->sb1ptr) return 0;
	uint32_t sb1 = p->sbDirect ? p->sb1ptr : gbacore_read32(core, p->sb1ptr);
	return ((sb1 >> 24) == 0x02u) ? sb1 : 0;
}

// --- PHASE 24 / lane A3 (RS-P24): the flag-rail probe. See touch.h for why it exists. ---------
// LOGGING ONLY. Every read below goes through the SHIPPED fieldtrav_flag_get + fieldtrav_cfg +
// fieldtrav_variant; this function contributes no numbering of its own, which is the whole point
// (a probe that re-derives the offsets could only ever agree with itself).
BadgeProbe g_badgeProbe[2];

void badgeprobe_stamp(int seat, GbaCore* core, const GameProfile* p) {
	if (seat < 0 || seat > 1) return;
	BadgeProbe* b = &g_badgeProbe[seat];
	if (!core || !p) { b->seq++; b->sb1 = 0; return; }

	FpBus bus = { fp_r8, fp_r16, fp_r32, core };
	FtVariant var = ft_variant(p);                       // the shipped title -> numbering map
	const FtEngCfg* cN = fieldtrav_cfg(var);             // the row this title really selects
	const FtEngCfg* cE = fieldtrav_cfg(FT_VAR_EMERALD);  // ...and the row the DEFECT selected
	uint32_t sb1 = prog_sb1(core, p);

	b->code = (int32_t)((uint32_t)(uint8_t)p->code[0]        |
	                    ((uint32_t)(uint8_t)p->code[1] << 8) |
	                    ((uint32_t)(uint8_t)p->code[2] << 16)|
	                    ((uint32_t)(uint8_t)p->code[3] << 24));
	b->variant     = (int32_t)var;
	b->sb1         = (int32_t)sb1;
	b->flagsOffNew = (int32_t)cN->flagsOff;
	b->flagsOffOld = (int32_t)cE->flagsOff;
	b->badge01New  = (int32_t)cN->badgeCut;   // FLAG_BADGE01_GET — the Cut badge in RS and Emerald
	b->badge01Old  = (int32_t)cE->badgeCut;
	// pret: FLAG_BADGE01_GET..FLAG_BADGE08_GET = SYSTEM_FLAGS + 0x07..0x0E, consecutive. Graded
	// against the SAME row's other four ids rather than asserted (pokeruby flags.h:789-796 /
	// pokeemerald flags.h:1359-1366: SMASH = BADGE03, STRENGTH = BADGE04, SURF = BADGE05,
	// WATERFALL = BADGE08).
	b->rowConsec = (cN->badgeSmash     == (uint16_t)(cN->badgeCut + 2) &&
	                cN->badgeStrength  == (uint16_t)(cN->badgeCut + 3) &&
	                cN->badgeSurf      == (uint16_t)(cN->badgeCut + 4) &&
	                cN->badgeWaterfall == (uint16_t)(cN->badgeCut + 7)) ? 1 : 0;

	int32_t mN = 0, mO = 0;
	for (int i = 0; i < 8; i++) {
		if (fieldtrav_flag_get(&bus, var,            sb1, cN->badgeCut + i)) mN |= 1 << i;
		if (fieldtrav_flag_get(&bus, FT_VAR_EMERALD, sb1, cE->badgeCut + i)) mO |= 1 << i;
	}
	b->badgesNew = mN;
	b->badgesOld = mO;
	b->shoesNew  = fieldtrav_flag_get(&bus, var,            sb1, cN->runShoes) ? 1 : 0;
	b->shoesOld  = fieldtrav_flag_get(&bus, FT_VAR_EMERALD, sb1, cE->runShoes) ? 1 : 0;
	b->addrNew   = (int32_t)(sb1 + cN->flagsOff + ((uint32_t)cN->badgeCut >> 3));
	b->addrOld   = (int32_t)(sb1 + cE->flagsOff + ((uint32_t)cE->badgeCut >> 3));
	b->runElig   = (int32_t)run_elig(core, p);
	b->seq++;
}

// gPlayerAvatar +0x00 flags, bit 3 = PLAYER_AVATAR_FLAG_SURFING (pokeemerald / pokefirered
// include/global.fieldmap.h — bit 3 in both, re-read this session). 0 addr -> "not surfing",
// which is the safe answer: it only ever suppresses a mount we would otherwise plan.
static bool prog_surfing(GbaCore* core, const GameProfile* p) {
	if (!core || !p || !p->playerAvatar) return false;
	return (gbacore_read8(core, p->playerAvatar) & 0x08u) != 0;
}
// The tracked object slot's active:1 bit. This going 0 IS the proof a Cut / Rock Smash landed
// (the scripts' `removeobject VAR_LAST_TALKED`) — not a frame count, not a screenshot.
// The avatar's OWN facing, read exactly where gamestate.c reads it (gObjectEvents[0].facingDirection
// low nibble, +0x18 — gamestate.c game_read). 1 = D, 2 = U, 3 = L, 4 = R; -1 = unreadable, which the
// sequencer treats as "fall back to the old fixed-frame hold" rather than as a wrong direction.
static int prog_facing(GbaCore* core, const GameProfile* p) {
	if (!core || !p || !p->mapObjects) return -1;
	int f = gbacore_read8(core, p->mapObjects + 0x18) & 0x0F;
	return (f >= 1 && f <= 4) ? f : -1;
}

// PHASE 26 / lane W — "is the avatar standing on a waterfall RIGHT NOW". This is not our idea of
// where the player is: it is `gObjectEvents[0].currentMetatileBehavior`, the game's OWN cached
// behaviour byte at +0x1E (pokeemerald include/global.fieldmap.h:248, pokefirered :same block,
// pokeruby :215 — one offset, three engines), which is the exact field the waterfall ride tests to
// decide whether to keep climbing (src/field_effect.c:1885). One byte, no ROM chain walk, and the
// same read `run_elig`'s terrain gate already makes every frame B is held.
static bool prog_on_waterfall(GbaCore* core, const GameProfile* p) {
	if (!core || !p || !p->mapObjects) return false;
	int beh = (int)gbacore_read8(core, p->mapObjects + 0x1Eu);
	return fieldtrav_is_waterfall(fp_engine(p), beh);
}
// ...and FlagGet(FLAG_SYS_USE_STRENGTH), the ONLY thing an activation changes. Same flag rail as
// the badges (per TITLE, not per engine — the c2a58db split), so Ruby's 0x829@0x1220 and Emerald's
// 0x889@0x1270 are both reached by the one table.
static bool prog_strength_on(GbaCore* core, const GameProfile* p) {
	if (!core || !p) return false;
	FpBus bus = { fp_r8, fp_r16, fp_r32, core };
	FtVariant var = ft_variant(p);
	return fieldtrav_flag_get(&bus, var, prog_sb1(core, p), fieldtrav_cfg(var)->strengthLatch);
}

// PHASE 24 / lane A2 (decision D2) — the five gates of "may this leg RUN", each read off the
// game's own state, each defaulting to CLEAR. The rule being mirrored is quoted with its pret
// lines in touchgeom.h; this function is only the reads.
//
// EVERY MISSING ADDRESS CLEARS ITS BIT AND THE LEG WALKS. That is the whole degradation story
// D2 asks for ("degrade SILENTLY to walking when any gate fails; never stall, never spam B") and
// it is why the gates are a MASK rather than a bool: `runElig` in g_fieldDbg names which one
// failed, so "why didn't it run" is one gdb read instead of a rebuild.
//
// Ruby/Sapphire (`sbDirect`) deliberately never sets RUNG_MAP: RS has no `allowRunning` bit at
// all — its rule is `gMapHeader.mapType == MAP_TYPE_INDOOR` (the RS_IsRunningDisallowed that
// pokeemerald src/bike.c:893-899 preserves) — and no RS ROM exists on this machine to verify the
// header layout against. Reading Emerald's bit out of an RS header would be a guess, so RS keeps
// today's walk-only behaviour: a NAMED degradation, exactly like every other RS row in gamestate.h.
static unsigned run_elig(GbaCore* core, const GameProfile* p) {
	if (!core || !p) return 0;
	unsigned e = 0;
	FpEngine eng = fp_engine(p);
	// The flag rail is per-TITLE, not per-engine: Ruby/Sapphire keep FP_ENG_RSE for the map but
	// carry their own flags[] offset and FLAG_SYS_B_DASH id (0x860, not Emerald's 0x8C0).
	FtVariant var = ft_variant(p);
	const FtEngCfg* c = fieldtrav_cfg(var);

	// 1. RUNG_SHOES — FlagGet(FLAG_SYS_B_DASH). Same flag rail the HM badges use.
	{
		FpBus bus = { fp_r8, fp_r16, fp_r32, core };
		uint32_t sb1 = prog_sb1(core, p);
		if (sb1 && fieldtrav_flag_get(&bus, var, sb1, c->runShoes)) e |= RUNG_SHOES;
	}
	// 2. RUNG_MAP — gMapHeader.allowRunning. A BITFIELD, and the two engines lay it out
	//    differently (global.fieldmap.h): RSE byte 0x1A bit 2, FRLG byte 0x19 bit 1.
	if (p->mapHeaderPath && !p->sbDirect) {
		uint32_t off = (eng == FP_ENG_FRLG) ? 0x19u : 0x1Au;
		unsigned bit = (eng == FP_ENG_FRLG) ? 1u : 2u;
		if ((gbacore_read8(core, p->mapHeaderPath + off) >> bit) & 1u) e |= RUNG_MAP;
	}
	// 3/4. RUNG_ONFOOT + RUNG_FREE — gPlayerAvatar.flags (+0x00), bit values identical in both
	//    engines (PLAYER_AVATAR_FLAG_*: MACH_BIKE 1<<1, ACRO_BIKE 1<<2, SURFING 1<<3,
	//    UNDERWATER 1<<4, FORCED_MOVE 1<<6). Surfing is ALREADY run speed and never reads B;
	//    a bike never reaches the dash branch at all and there B is the acro WHEELIE, which is
	//    why biking is a hard veto rather than a harmless no-op.
	if (p->playerAvatar) {
		uint8_t f = gbacore_read8(core, p->playerAvatar);
		if (!(f & (0x02u | 0x04u | 0x08u | 0x10u))) e |= RUNG_ONFOOT;
		if (!(f & 0x40u)) e |= RUNG_FREE;
	}
	// 5. RUNG_TERRAIN — the game's own IsRunningDisallowed metatile half, asked about the tile the
	//    player is standing on RIGHT NOW (gObjectEvents[0].currentMetatileBehavior +0x1E, and
	//    currentElevation = the low nibble of +0x0B for the Fortree-bridge clause). Re-read every
	//    frame B would be held, because the engine re-reads it on every step: a route that walks
	//    into long grass simply stops holding B instead of fighting the game.
	if (p->mapObjects) {
		int beh  = (int)gbacore_read8(core, p->mapObjects + 0x1Eu);
		int elev = (int)(gbacore_read8(core, p->mapObjects + 0x0Bu) & 0x0Fu);
		if (rungeom_tile_ok((int)eng, beh, elev)) e |= RUNG_TERRAIN;
	}
	return e;
}

// The B half of a walking frame's key mask. `s_runLeg` is the per-LEG distance decision (latched
// at plan time); the live re-test is the engine's own — it re-evaluates eligibility on every step,
// so we do too, and a leg that becomes ineligible mid-route just stops holding B.
static u16 run_key(GbaCore* core, const GameProfile* p, bool runLeg) {
	if (!runLeg) return 0;
	unsigned elig = run_elig(core, p);
	g_fieldDbg.runElig = (int32_t)elig;
	if (!rungeom_eligible(elig)) return 0;
	g_fieldDbg.runFrames++;
	return 1u << GBAKEY_B;
}

static bool prog_obj_active(GbaCore* core, const GameProfile* p, int slot) {
	if (!core || !p || !p->mapObjects || slot < 0 || slot >= 16) return false;
	return (gbacore_read32(core, p->mapObjects + 0x24u * (uint32_t)slot) & 1u) != 0;
}
static FtParty prog_party(GbaCore* core, const GameProfile* p) {
	FtParty pt = { 0, 0, 0 };
	if (!core || !p) return pt;
	pt.sb1 = prog_sb1(core, p);
	pt.partyBase = p->partyBase;
	if (p->partyCount) {
		int n = (int)gbacore_read8(core, p->partyCount);
		pt.partyCount = (n >= 0 && n <= 6) ? n : 0;
	}
	return pt;
}

static void prog_dbg_stamp_at(int step, int phase) {
	g_fieldDbg.progMoves     = s_prog.nMoves;
	g_fieldDbg.progInteracts = s_prog.nInteracts;
	g_fieldDbg.progStep      = step;
	g_fieldDbg.progHm        = (step >= 0 && step < s_prog.nMoves) ? s_prog.mv[step].hm : 0;
	g_fieldDbg.progPhase     = phase;
}
// The per-frame stamp reports the state the frame STARTED in (progseq hands those two values back
// in the act) — an end stamp reports where it finished. That asymmetry is the shipped one, and the
// phase-24 diagnoses were read off it, so the split preserves it exactly.
static void prog_dbg_stamp(void) { prog_dbg_stamp_at(s_seq.step, s_seq.phase); }
static void prog_end(int why, const char* chip) {
	s_seq.on = 0;
	g_fieldDbg.progEnd = why; g_fieldDbg.progEndSeq = ++s_progEndSeq;
	prog_dbg_stamp();
	s_progChip = chip; s_progChipHold = 60;   // ~1 s of DONE / STOPPED, per T4.2
}

// The HUD verb for the step in flight — the user sees "SURF >" BEFORE the walk to the shore
// starts, which is the whole no-preview compromise (SPEC T4.2 / Open Q2).
static const char* prog_chip_for(int hm) {
	switch (hm) {
	case FT_HM_CUT:       return "CUT >";
	case FT_HM_SMASH:     return "SMASH >";
	case FT_HM_SURF:      return "SURF >";
	case FT_HM_WATERFALL: return "FALLS >";
	case FT_HM_STRENGTH:  return "STRENGTH";   // no "->": the program ENDS at the boulder (lane W)
	default:              return "ROUTE >";
	}
}
static void prog_chip_refresh(void) {
	if (!s_seq.on) return;
	int hm = FT_HM_NONE;
	for (int i = s_seq.step; i < s_prog.nMoves; i++)      // the NEXT verb, not the current step
		if (s_prog.mv[i].hm != FT_HM_NONE) { hm = s_prog.mv[i].hm; break; }
	s_progChip = prog_chip_for(hm);
	s_progChipHold = 0;                                    // sticky while the program runs
}

// ============================================================================================
// PHASE 26 / lane W — THE TAP GATE: the three taps the DRY router cannot answer honestly.
// ============================================================================================
// The shipped tier order (dry walk first, conditional edges only if it fails — SPEC H1.7) exists
// so a wet or destructive detour can never displace a walkable route. It assumes something that is
// true for Cut, Rock Smash and Surf and FALSE for the two HMs this lane adds: that when the dry
// router answers, its answer is right.
//
//   * WATERFALL. A waterfall metatile is collision 0 and elevation 1 — measured, 62 columns across
//     7 maps on the user's own Emerald ROM — i.e. indistinguishable from the ocean above and below
//     it to `fieldpath_enterable`. The frozen tier-0 router therefore plots a straight swim UP a
//     fall and calls it a route, and the game's forced movement (pokeemerald
//     src/field_player_avatar.c:159 -> :185 ForcedMovement_PushedSouthByCurrent) flushes the player
//     straight back down, forever. Tier 0 does not merely miss the waterfall edge — it wins with a
//     wrong answer, so asking it first would make the whole HM unreachable.
//   * STRENGTH. Tapping a boulder ALREADY succeeds at tier 0, because fieldpath deliberately allows
//     a blocked GOAL as the terminal (fieldpath.h:146-152) — so a Strength activation could never
//     be reached either. Here the route is IDENTICAL with or without the gate (walk to the tile
//     beside the boulder); only the TERMINAL differs, a hold versus the game's own prompt. Nothing
//     is displaced, which is why this is not the tier inversion H1.7 forbids.
//
// Returns  2 = the tap landed ON a waterfall: ask the conditional planner FIRST, and NEVER hand the
//              raw tile to a router afterwards (that tile is the flush),
//          1 = ask the conditional planner FIRST, ordinary fallback afterwards,
//          0 = shipped order, unchanged,
//         -1 = plan NOTHING (a waterfall tap whose column will not resolve).
//
// It CLASSIFIES only; it does not move the goal. The retarget has exactly one implementation, in
// `fieldtrav_plan`, which is also what makes it host-testable and what makes `FtProgram.wfRetarget`
// (and the `progRetarget` mirror) mean something on a live run — a gate that quietly pre-retargeted
// would leave the planner with nothing to report.
//
// Cost discipline: this runs on every tap while the toggle is on, so the cheap tests come first —
// one behaviour read, then a coordinate compare and the avatar's surf bit, and only then anything
// that walks the party.
// PHASE 28 / lane X (phase-26 audit O3): the DECISION now lives in `progtap.c`, where a host suite
// compiles it; this function is only the READS, in the order the cost discipline above demands —
// one behaviour read, then a coordinate compare and the avatar's surf bit, and only then anything
// that walks the party. The scope argument for `PT_GATE_FIRST` is written out in progtap.c.
static int prog_tap_gate(GbaCore* core, const GameProfile* p, int px, int py, int* gx, int* gy) {
	(void)px;
	if (!core || !p || !gx || !gy) return PT_GATE_SHIPPED;
	int w, h; uint32_t ptr;
	if (!map_read(core, p, &w, &h, &ptr)) return PT_GATE_SHIPPED;
	FpBus bus = { fp_r8, fp_r16, fp_r32, core };
	FpMap m = { fp_engine(p), p->mapHeaderPath, p->mapObjects, ptr, w, h };

	PtObs o;
	o.py = py; o.whalf = FP_WHALF;
	o.goalIsWaterfall = fieldtrav_is_waterfall(m.engine, fieldpath_behaviour_at(&bus, &m, *gx, *gy)) ? 1 : 0;
	o.topY = o.goalIsWaterfall ? fieldtrav_waterfall_top(&bus, &m, *gx, *gy) : -1;
	o.upward = (*gy < py) ? 1 : 0;
	o.surfing = 0; o.waterfallUsable = 0; o.strengthTap = 0;
	// PHASE 29 / lane F (phase-28 audit O2): the cost discipline is `progtap.c`'s too now. This file
	// no longer restates rule (2)'s predicate to decide what to read — it ASKS, so a narrowed rule
	// and its read-suppression can never drift apart, and a mutation of the covered file moves both.
	{
		FtVariant var = ft_variant(p);
		if (progtap_needs_waterfall(&o)) {
			o.surfing = prog_surfing(core, p) ? 1 : 0;
			if (o.surfing) {
				FtParty pty = prog_party(core, p);
				o.waterfallUsable = (fieldtrav_usable(&bus, var, &pty) & (1u << FT_HM_WATERFALL)) ? 1 : 0;
			}
		}
		if (progtap_needs_strength(&o)) {
			FtParty pty = prog_party(core, p);
			o.strengthTap = (fieldtrav_strength_tap(&bus, &m, var, &pty, *gx, *gy) >= 0) ? 1 : 0;
		}
	}
	return progtap_gate(&o);
}

// The goal the LAST `fieldtrav_plan` actually resolved, and 1 only when it really moved it: a tap
// on a waterfall aims at the tile the game's own ride ends on, not at the tile the finger touched.
// The tap handler needs it for one case — a fall the conditional planner declined — where the raw
// tile must not be handed to the dry router but the top of the column safely can be.
static int prog_retargeted_goal(int* gx, int* gy) {
	return progtap_retargeted_goal(s_prog.wfRetarget, s_prog.goalX, s_prog.goalY, gx, gy);
}

// Plan (or re-plan) a conditional route to (gx,gy). Returns:
//    1 = a program is armed;  0 = declined/failed (nothing injected);
//   -1 = a PLAIN walk reaches it now -> the caller hands the tap to the shipped fieldpath router.
static int prog_plan(GbaCore* core, const GameProfile* p, int px, int py, int gx, int gy,
                     int mapG, int mapN) {
	int w, h; uint32_t ptr;
	if (!core || !p || !map_read(core, p, &w, &h, &ptr)) return 0;
	read_npcs(core, p);                       // the same block list the shipped router uses
	FpBus bus = { fp_r8, fp_r16, fp_r32, core };
	FpMap m = { fp_engine(p), p->mapHeaderPath, p->mapObjects, ptr, w, h };
	FtParty pty = prog_party(core, p);
	bool surfing = prog_surfing(core, p);
	fieldtrav_plan(&bus, &m, ft_variant(p), &pty, px, py, gx, gy, surfing, s_npcG, s_npcN, &s_prog);

	g_fieldDbg.progOutcome = s_prog.outcome;
	g_fieldDbg.progUsable  = (int32_t)s_prog.usable;
	g_fieldDbg.progEdges   = s_prog.nEdges;
	g_fieldDbg.progSurf    = surfing ? 1 : 0;
	// PHASE 26 / lane W: the tap and the destination, side by side. On a waterfall tap these
	// DIFFER, and the difference is the retarget doing its job.
	g_fieldDbg.progRetarget = s_prog.wfRetarget;
	g_fieldDbg.progGoalX    = s_prog.goalX;
	g_fieldDbg.progGoalY    = s_prog.goalY;
	if (s_prog.outcome == FT_OUT_TIER0) return -1;         // dry paths win (SPEC H1.7)
	if (!s_prog.ok) return 0;

	progseq_arm(&s_seq, px, py);
	s_progMapG = mapG; s_progMapN = mapN;
	// PHASE 26 / lane W: the EFFECTIVE goal, not the raw tap. `fieldtrav_plan` retargets a tap that
	// landed on a waterfall tile onto the tile the game's own ride ends on (`wfRetarget`), and every
	// re-plan after an interact has to aim at that same tile. Aiming at the raw tap instead would
	// send the post-ride re-plan back at the waterfall — and the frozen tier-0 router, which cannot
	// see forced movement, would cheerfully swim into it. For every non-retargeted tap these two
	// are identical (fieldtrav_plan sets goalX/goalY = gx/gy on entry).
	s_progGoalX = s_prog.goalX; s_progGoalY = s_prog.goalY;
	// PHASE 28 / lane X: remember which object a STRENGTH program will prompt at, so the live mirror
	// can publish that boulder's coordinates. LOGGING ONLY — nothing plans off it.
	for (int i = 0; i < s_prog.nMoves; i++)
		if (s_prog.mv[i].hm == FT_HM_STRENGTH) { s_strObj = s_prog.mv[i].objSlot; break; }
	g_fieldDbg.progSeq = ++s_progSeq;
	g_fieldDbg.progEnd = TPE_NONE;
	prog_dbg_stamp();
	prog_chip_refresh();
	return 1;
}

// PHASE 23 — arm an excursion. Runs ONLY after tier 0 (dry walk) and tier 1 (conditional edges)
// have both declined, so it can never displace a same-map route. Returns 1 if a program is armed.
static int exc_plan(GbaCore* core, const GameProfile* p, int px, int py, int gx, int gy,
                    int mapG, int mapN) {
	exc_reset();
	int w, h; uint32_t ptr;
	if (!core || !p || !p->mapGroupsRom || !map_read(core, p, &w, &h, &ptr)) return 0;
	read_npcs(core, p);
	FpBus bus = { fp_r8, fp_r16, fp_r32, core };
	FpMap m = { fp_engine(p), p->mapHeaderPath, p->mapObjects, ptr, w, h };
	if (!fieldtrav_excursion(&bus, &m, p->mapGroupsRom, mapG, mapN, px, py, gx, gy,
	                         s_npcG, s_npcN, &s_exc)) {
		g_fieldDbg.progOutcome = s_exc.outcome;      // FT_OUT_NOEXC = "excursion-none" (H3.5)
		return 0;
	}
	// Leg 0 is an ORDINARY warp route: walk to Wi and let fieldpath's own terminal semantics
	// (door hold / arrow hold / step) fire it. If even that cannot be planned, plan nothing.
	if (!walk_plan(core, p, px, py, s_exc.wiX, s_exc.wiY, mapG, mapN)) return 0;
	s_walking = true; s_lpx = px; s_lpy = py; s_stall = 0; s_replans = 0;
	excseq_arm(&s_excS, mapG, mapN, gx, gy);
	s_excChip = exc_chip_for(0);
	g_fieldDbg.progSeq = ++s_excSeq;
	g_fieldDbg.progOutcome = s_exc.outcome;
	g_fieldDbg.progEnd = TPE_NONE;
	exc_dbg_stamp();
	return 1;
}

// A leg ended because SaveBlock1.location changed. Verify we are where the plan SAID we would be,
// then re-plan the next leg on the LIVE grid of the map we actually landed on (H3.4) — never from
// the ROM plan, which has no NPCs in it and cannot see runtime layout changes.
static void exc_leg_boundary(GbaCore* core, const GameProfile* p, int px, int py,
                             int mapG, int mapN) {
	(void)core; (void)p; (void)px; (void)py;
	// The rule is excseq's (host-graded): leg 0 must land on the excursion plan's D map, leg 1 back
	// on the home map, and anything else ends the excursion where it stands. All this side does is
	// the two things a pure module must not: the HUD chip and the debug mirror.
	int r = excseq_boundary(&s_excS, mapG, mapN, s_exc.dGroup, s_exc.dNum, s_exc.wjX, s_exc.wjY);
	if (r == EXC_B_RESET) { s_excChip = 0; return; }
	if (r == EXC_B_ARMED) {
		s_excChip = exc_chip_for(s_excS.leg);
		exc_dbg_stamp();
	}
}

// Re-plan from LIVE state after every INTERACT (SPEC H0.1: never execute a plan's assumptions
// once the world has changed under it). The commonest outcome after a Cut is that the rest of the
// route is now a PLAIN WALK, which hands straight back to the shipped fieldpath walker — the
// cheapest possible way to keep one router in charge of ordinary walking.
static bool prog_replan(GbaCore* core, const GameProfile* p, int px, int py, int mapG, int mapN) {
	if (++s_progReplans > TP_MAX_REPLANS) { prog_end(TPE_REPLAN, "STOPPED"); return false; }
	int keepSeq = s_progReplans;
	int r = prog_plan(core, p, px, py, s_progGoalX, s_progGoalY, mapG, mapN);
	s_progReplans = keepSeq;                        // prog_plan resets it; the budget must survive
	if (r == 1) return true;
	if (r == -1) {                                  // the rest is walkable: hand it to fieldpath
		if (walk_plan(core, p, px, py, s_progGoalX, s_progGoalY, mapG, mapN)) {
			s_walking = true; s_lpx = px; s_lpy = py; s_stall = 0; s_replans = 0;
			prog_end(TPE_HANDOFF, "DONE");
			return false;
		}
	}
	prog_end(TPE_REPLAN, "STOPPED");
	return false;
}

static u16 prog_update(const TouchSmart* sm, bool touching, bool newPress,
                       int px, int py, int mapG, int mapN) {
	GbaCore* core = sm->core; const GameProfile* p = sm->prof;
	(void)touching;
	if (!s_seq.on || !core || !p) return 0;

	// PHASE 25 / audit O2. The phase machine that used to live here — the abort ladder, WALK, the
	// closed-loop FACE, the A settle, the DLG/YESNO/DONE advance cadences and the LEVEL-triggered
	// ANSWER — is `source/progseq.c`: pure, host-graded frame by frame, and the SHIPPED one. What
	// remains here is precisely what a pure module may not do.
	//
	//   READ  the game (this block), then ONE step, then
	//   DO    what it asks: the eligibility re-read, the YES cursor write, the BFS re-plan, the
	//         HUD chip, the gdb mirror and the key mask.
	//
	// `facing` / `surfing` / `objActive` are only CONSULTED by the phase that needs them, so they
	// are only read in that phase — the same call pattern as before the split. (The exception is
	// benign and named: a FACE frame whose move turns out to be a plain walk, or a spent A-pulse
	// frame, now costs one extra side-effect-free EWRAM read.)
	FtMove mv = { -1, FT_HM_NONE, -1 };
	if (s_seq.step >= 0 && s_seq.step < s_prog.nMoves) mv = s_prog.mv[s_seq.step];

	ProgObs o;
	o.ctx        = (sm->ctx == GCTX_OVERWORLD) ? PSQ_CTX_OVERWORLD
	             : (sm->ctx == GCTX_FIELDMENU) ? PSQ_CTX_FIELDMENU : PSQ_CTX_OTHER;
	o.textDlg    = sm->textDlg ? 1 : 0;
	o.padKeys    = sm->padKeys ? 1 : 0;
	o.newPress   = newPress ? 1 : 0;
	o.mapChanged = (mapG != s_progMapG || mapN != s_progMapN) ? 1 : 0;
	o.px = px; o.py = py;
	o.facing     = (s_seq.phase == TPH_FACE) ? prog_facing(core, p) : -1;
	o.surfing    = (s_seq.phase == TPH_DONE) ? (prog_surfing(core, p) ? 1 : 0) : 0;
	o.objActive  = (s_seq.phase == TPH_DONE) ? (prog_obj_active(core, p, mv.objSlot) ? 1 : 0) : 0;
	// PHASE 26 / lane W — the two DONE-phase observables the new HMs prove themselves with, read
	// only for the HM that needs them: the waterfall ride's own loop condition (one EWRAM byte) and
	// the Strength latch (one flag read through the per-title rail). Both default to 0, which is the
	// safe answer everywhere else — "the ride has not finished" and "Strength is not active yet".
	o.onWaterfall = (s_seq.phase == TPH_DONE && mv.hm == FT_HM_WATERFALL)
	              ? (prog_on_waterfall(core, p) ? 1 : 0) : 0;
	o.strengthOn  = (s_seq.phase == TPH_DONE && mv.hm == FT_HM_STRENGTH)
	              ? (prog_strength_on(core, p) ? 1 : 0) : 0;
	// PHASE 29 / lane F — DEFECT X1's one new observation, read in TPH_FACE only, for the same
	// reason every other conditional read here is: it costs a map walk and exactly one phase
	// consults it. "Would a step in the direction this interact faces ENTER the tile?" — which for
	// every obstacle the FACE phase was written against (a Cut tree, a Smash rock, a boulder, the
	// shore) is NO, and for a waterfall under a surfing player is YES. The predicate is the
	// forced-movement family, because those are precisely the tiles that are collision 0 and
	// therefore enterable while nothing about them is walkable: fieldtrav_is_waterfall +
	// fieldtrav_is_current (fieldtrav.h, one citation block per engine). `surfing` is part of it —
	// on FOOT the same tile is refused by the elevation half of the collision test, which is the
	// bump that makes the Surf mount prompt work at all.
	o.faceEnterable = 0;
	if (s_seq.phase == TPH_FACE && mv.hm != FT_HM_NONE && mv.dir >= 0 && mv.dir < 4 &&
	    prog_surfing(core, p)) {
		static const int fdx[4] = { 1, -1, 0, 0 }, fdy[4] = { 0, 0, 1, -1 };   // touch.c s_keyDir order
		int w, h; uint32_t ptr;
		if (map_read(core, p, &w, &h, &ptr)) {
			FpBus bus = { fp_r8, fp_r16, fp_r32, core };
			FpMap m = { fp_engine(p), p->mapHeaderPath, p->mapObjects, ptr, w, h };
			int b = fieldpath_behaviour_at(&bus, &m, px + fdx[mv.dir], py + fdy[mv.dir]);
			o.faceEnterable = (fieldtrav_is_waterfall(m.engine, b) ||
			                   fieldtrav_is_current(m.engine, b)) ? 1 : 0;
		}
	}
	g_fieldDbg.progFaceEnter = o.faceEnterable;

	ProgAct act;
	progseq_step(&s_seq, &s_prog, &o, &act);

	// --- carry out the action, in the order the shipped executor did these things ---
	if (act.stamp) prog_dbg_stamp_at(act.stampStep, act.stampPhase);

	// Re-check eligibility at the START of every interact (H1.6). It cannot realistically change
	// mid-route, but a cheap honest re-read beats an assumption, and a FALSE here means we would
	// have prompted something the game is about to refuse. It is the one read too expensive (a
	// party walk + a flag read) to do speculatively every frame, which is why it is a REQUEST.
	if (act.needElig) {
		FpBus bus = { fp_r8, fp_r16, fp_r32, core };
		FtParty pty = prog_party(core, p);
		uint32_t usable = fieldtrav_usable(&bus, ft_variant(p), &pty);
		g_fieldDbg.progUsable = (int32_t)usable;
		if (!(usable & (1u << act.eligHm))) {
			progseq_elig_fail(&s_seq);
			prog_end(TPE_ELIG, "STOPPED");
			return 0;                      // a refusal voids every other field of the act
		}
	}
	if (act.chipRefresh) prog_chip_refresh();
	if (act.stampSurf)   g_fieldDbg.progSurf = o.surfing ? 1 : 0;
	if (act.writeYes && p->sMenuBase) gbacore_write8(core, p->sMenuBase + 2, 0);
	if (act.answered)    g_fieldDbg.progAnswers++;
	if (act.end) {
		prog_end(act.end, act.end == TPE_ARRIVED ? "DONE" : "STOPPED");
		if (act.swallow)  s_progSwallow = true;
		if (act.farewell) s_progFarewell = 3;   // set AFTER prog_end: it must survive the program
		return 0;
	}
	if (act.replan) { prog_replan(core, p, px, py, mapG, mapN); return 0; }
	if (act.pressA) { g_fieldDbg.progAKeys++; return 1u << GBAKEY_A; }
	if (act.keyDir >= 0 && act.keyDir < 4) {
		if (act.runSpan > 0) {
			// PHASE 24 / lane A2 (decision D2): the LEG's run decision. progseq counts the span (a
			// property of the plan); the five gates are a live read, so they stay here.
			bool legRuns = rungeom_decide(act.runSpan, run_elig(core, p)) != 0;
			g_fieldDbg.runLeg = legRuns ? 1 : 0;
			return s_keyDir[act.keyDir] | run_key(core, p, legRuns);
		}
		return s_keyDir[act.keyDir];
	}
	return 0;
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
static int  s_lSeq = 0, s_lSeqTick = 0; static u16 s_lSeqKey = 0;  // queued tab edges (paced)
// EMULATOR FINDING (lane A, EM bag run 2026-08-14): the bag IGNORES a pocket-switch key that
// lands during its ~16-frame pocket-swap animation — a 2-frame edge cadence delivered 1 of 3
// queued edges (POKE BALLS instead of BERRIES). One edge per 24 frames clears the swap anim
// with margin; a dot tap is a deliberate act, so the extra ~0.4 s for a 3-pocket jump is fine.
#define LIST_SEQ_FRAMES 24
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
	if (s_lSeq > 0) {                                // queued pocket-tab edges, one per LIST_SEQ_FRAMES
		u16 k = (s_lSeqTick == 0) ? s_lSeqKey : 0;
		if (++s_lSeqTick >= LIST_SEQ_FRAMES) { s_lSeqTick = 0; s_lSeq--; }
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

// ===================== SMART: the GRID family (PC storage boxes) =====================
// PHASE 22.2 (SPEC-family-grid). Tap-tap-move honesty: a tap arms a TARGET (cursor area+pos) and
// the driver walks the game's OWN cursor there with single d-pad edges chosen per frame from the
// LIVE sCursorArea/sCursorPosition read (stornav_step = the engine's own transition table), then
// presses the game's own A. Nothing writes storage state, ever; a target the cursor cannot reach
// is dropped silently (G2: A is NEVER emitted while live != target). The storage popups (mon
// MOVE/…, held PLACE/…, box JUMP/WALLPAPER/NAME/CANCEL) are standard sMenu menus
// (HandleMenuInput -> Menu_GetCursorPos, pokemon_storage_system.c:8024-8059), so while one is up
// taps delegate to the SHIPPED fieldmenu write-then-A path (G6).
static int s_stTgtA = -1, s_stTgtP = 0;   // armed navigation target (-1 = idle)
static int s_stAct = 0;                   // on arrival: 0 none / 1 A / 2 scroll LEFT / 3 scroll RIGHT
static int s_stArm = 0;                   // frames left before the arrival action fires (anim settle)
static int s_stATick = 0;                 // remaining A-pulse frames
static int s_stHold = 0; static u16 s_stHoldKey = 0;   // held-key frames (title-area box scroll)
static int s_stGap = 0;                   // idle frames between nav edges (edges need releases)
static int s_stTotal = 0;                 // frames since the target was armed (timeout)
static void storage_reset(void) {
	s_stTgtA = -1; s_stTgtP = 0; s_stAct = 0; s_stArm = 0; s_stATick = 0;
	s_stHold = 0; s_stHoldKey = 0; s_stGap = 0; s_stTotal = 0;
}
#define STOR_EDGE_GAP  8    // release frames between nav edges (the cursor slide swallows edges —
                            // the closed loop just re-emits; census bag-pacing lesson applied)
#define STOR_A_DELAY   20   // arrival -> action delay: SetCursorPosition updates the statics at the
                            // START of the slide (~12f, UpdateCursorPos), and HandleInput is not
                            // polled mid-slide — an instant A would be swallowed
#define STOR_TIMEOUT   360  // drop an unreached target (frames since armed)
#define STOR_HOLD_FRAMES 3  // held frames for the title-area scroll (HandleInput_OnBox JOY_HELD)

typedef struct {
	int area, pos, held, origBox, origPos, inParty, boxOption, boxId, menuOpen;
	uint32_t occ;
} StorState;
static int storage_read(const TouchSmart* sm, StorState* st) {
	const GameProfile* p = sm->prof;
	if (!sm->core || !p || !p->stCursor || !p->stStorage) return 0;
	st->area    = gbacore_read8(sm->core, p->stCursor);
	st->pos     = gbacore_read8(sm->core, p->stCursor + 1);
	st->held    = gbacore_read8(sm->core, p->stCursor + 2);
	st->origBox = gbacore_read8(sm->core, p->stCursor + 3);
	st->origPos = gbacore_read8(sm->core, p->stCursor + 4);
	st->inParty   = gbacore_read8(sm->core, p->stStorage + 4);
	st->boxOption = gbacore_read8(sm->core, p->stStorage + 5);
	st->boxId = -1; st->occ = 0;
	if (p->pcStoragePtr) {
		uint32_t ps = gbacore_read32(sm->core, p->pcStoragePtr);
		if ((ps >> 24) == 0x02) {
			int box = gbacore_read8(sm->core, ps);
			if (box < 14) {
				st->boxId = box;
				uint32_t b = ps + 4u + 2400u * (uint32_t)box;   // boxes[box][0]; BoxPokemon = 80 B
				for (int i = 0; i < 30; i++)                    // hasSpecies: flags byte +19 bit 1
					if (gbacore_read8(sm->core, b + 80u * (uint32_t)i + 19u) & 0x02) st->occ |= 1u << i;
			}
		}
	}
	// G6 gate: a LIVE storage popup. sMenu.windowId is never cleared on RemoveMenu (stale-window
	// trap, spec §1.3), so require the gWindows slot to be allocated (bg != 0xFF) AND the window
	// to have the storage popup's exact bottom-right anchor (AddMenu: tilemapLeft = 29 - width,
	// tilemapTop = 15 - height) — a fingerprint no stale overworld window matches.
	st->menuOpen = 0;
	if (p->sMenuBase && p->gWindowsBase) {
		uint8_t wid = gbacore_read8(sm->core, p->sMenuBase + 5);
		if (wid < 32) {
			uint32_t win = p->gWindowsBase + 12u * (uint32_t)wid;
			uint8_t bg = gbacore_read8(sm->core, win + 0);
			int wl = gbacore_read8(sm->core, win + 1), wt = gbacore_read8(sm->core, win + 2);
			int ww = gbacore_read8(sm->core, win + 3), wh = gbacore_read8(sm->core, win + 4);
			if (bg != 0xFF && ww > 0 && wh >= 2 && wl + ww == 29 && wt + wh == 15) st->menuOpen = 1;
		}
	}
	if (st->area > 3 || st->pos > 29) return 0;   // not in a state the model covers (transitions)
	return 1;
}

static u16 storage_update(const TouchSmart* sm, bool touching, bool newPress, bool gvalid,
                          int gx, int gy) {
	(void)touching;
	StorState st;
	if (!storage_read(sm, &st)) { storage_reset(); return 0; }   // no anchors / mid-anim junk: dead
	if (st.boxOption == 3)      { storage_reset(); return 0; }   // MOVE ITEMS: named v1 limit (G7)
	if (st.menuOpen) {                                           // G6: popup -> the fmenu path
		storage_reset();
		if (newPress && gvalid && sm->prof) {
			int i = hit_fieldmenu(sm->core, sm->prof, gx, gy);
			if (i >= 0) { s_fmenu = i; s_fmenuTick = 0; }
		}
		return sm->prof ? fmenu_select(sm->core, sm->prof) : 0;
	}
	fmenu_reset();                                               // popup gone: never a stale write
	if (s_stATick > 0) { s_stATick--; return 1 << GBAKEY_A; }
	if (s_stHold  > 0) { s_stHold--;  return s_stHoldKey; }
	if (newPress && gvalid) {                                    // a fresh tap (re)arms the target
		int pos = 0;
		int k = storgeom_hit(gx, gy, st.inParty ? 1 : 0, &pos);
		if (k != SGH_NONE) { s_stTotal = 0; s_stGap = 0; s_stArm = 0; }
		switch (k) {
		case SGH_SLOT:      s_stTgtA = 0; s_stTgtP = pos; s_stAct = 1; break;
		case SGH_TITLE:     s_stTgtA = 2; s_stTgtP = 0;   s_stAct = 1; break;
		case SGH_ARROW_L:   s_stTgtA = 2; s_stTgtP = 0;   s_stAct = 2; break;
		case SGH_ARROW_R:   s_stTgtA = 2; s_stTgtP = 0;   s_stAct = 3; break;
		case SGH_BTN_PARTY: s_stTgtA = 3; s_stTgtP = 0;   s_stAct = 1; break;
		case SGH_BTN_CLOSE: s_stTgtA = 3; s_stTgtP = 1;   s_stAct = 1; break;
		case SGH_PARTY:     s_stTgtA = 1; s_stTgtP = pos; s_stAct = 1; break;
		default: break;                                          // dead gutter: target unchanged
		}
	}
	if (s_stTgtA < 0) return 0;
	if (++s_stTotal > STOR_TIMEOUT) { storage_reset(); return 0; }   // G2: drop, never mis-act
	if (st.area == s_stTgtA && st.pos == s_stTgtP) {             // live == target
		if (s_stArm == 0) s_stArm = STOR_A_DELAY;                // let the cursor slide finish
		if (--s_stArm > 0) return 0;
		int act = s_stAct;
		s_stTgtA = -1; s_stAct = 0; s_stArm = 0;
		if (act == 1) { s_stATick = 1; return 1 << GBAKEY_A; }   // 2-frame A pulse
		if (act == 2) { s_stHold = STOR_HOLD_FRAMES - 1; s_stHoldKey = 1 << GBAKEY_LEFT;  return s_stHoldKey; }
		if (act == 3) { s_stHold = STOR_HOLD_FRAMES - 1; s_stHoldKey = 1 << GBAKEY_RIGHT; return s_stHoldKey; }
		return 0;
	}
	s_stArm = 0;                                                 // moved off target mid-settle
	if (s_stGap > 0) { s_stGap--; return 0; }                    // releases between edges
	int sn = stornav_step(st.area, st.pos, s_stTgtA, s_stTgtP);
	if (sn == SN_NONE) { storage_reset(); return 0; }            // unroutable (e.g. party from box)
	s_stGap = STOR_EDGE_GAP;
	switch (sn) {
	case SN_UP:    return 1 << GBAKEY_UP;
	case SN_DOWN:  return 1 << GBAKEY_DOWN;
	case SN_LEFT:  return 1 << GBAKEY_LEFT;
	case SN_RIGHT: return 1 << GBAKEY_RIGHT;
	case SN_START: return 1 << GBAKEY_START;
	default:       return 0;
	}
}

// ===================== FAM-DLG: TAP-ADVANCE (phase 23, lane B) ===============
// TOUCH-PLAN.md §2 FAM-DLG. The gesture rules and the three taste calls live in touchgeom.h; this
// is the state machine and the only place that touches the bus (it doesn't — the family is 100%
// key injection, no RAM writes at all, which is what makes it safe to point at ~40 screens whose
// internals were never harvested).
//
// WHY IT IS SAFE TO POINT AT AN UNKNOWN SCREEN. Every context with a real handler is matched
// BEFORE this one in game_read (naming / list / storage / bag / party / battle / fieldmenu all win
// on a more specific test), so a screen only reaches FAM-DLG when nothing else claimed it. And the
// three verbs are the three a Gen-3 screen cannot misinterpret: A advances or confirms, B backs
// out, the D-pad moves whatever cursor is there. There is no RAM write to land on the wrong
// struct, and there is no walk-key leak — that was the whole point of phase 22.0's promotion.
static int  s_dTick = 0;                      // frames the finger has been down (0 = up)
static bool s_dDown = false, s_dDrag = false, s_dHeld = false;
static int  s_dDownX, s_dDownY, s_dLastX, s_dLastY;
static int  s_dPulse = 0; static u16 s_dPulseKey = 0;   // 2-frame edge: dead frame, then the key
static int  s_dTaps = 0, s_dHolds = 0, s_dPages = 0, s_dSteps = 0;   // PROOF counters (g_touchDbg)
static void dlg_reset(void) {
	s_dTick = 0; s_dDown = false; s_dDrag = false; s_dHeld = false;
	s_dPulse = 0; s_dPulseKey = 0;
	// the four counters are deliberately NOT cleared: they are the gdb proof channel and must
	// survive the ctx changes a proof arc walks through (they only ever reset with the session).
}

// `pager` = the live cb2 is in this game's cb2Pager whitelist (gamestate.h). Compare-only.
static int dlg_is_pager(const TouchSmart* sm) {
	if (!sm->prof || !sm->cb2) return 0;
	for (int i = 0; i < GS_N_PAGER; i++)
		if (sm->prof->cb2Pager[i] && sm->cb2 == sm->prof->cb2Pager[i]) return 1;
	return 0;
}

// `allowB` = 0 on GCTX_TITLE. On the pre-save screens B is either inert (intro, title) or an
// "unselect" the main menu handles anyway, and a hold there is far more likely to be a player
// resting a finger on a screen that is mid-fade than a deliberate cancel — so TITLE gets tap=A and
// nothing else, which is exactly what TOUCH-PLAN rows A1/A2 asked for.
static u16 dlg_update(const TouchSmart* sm, bool touching, bool newPress, bool gvalid,
                      int gx, int gy, int allowB) {
	if (s_dPulse > 0) {                       // finish a queued edge (dead frame, then 1 key frame)
		u16 k = (s_dPulse == 1) ? s_dPulseKey : 0;
		if (--s_dPulse == 0) s_dPulseKey = 0;
		return k;
	}
	int pager = dlg_is_pager(sm);
	if (newPress && gvalid) {
		s_dDown = true; s_dDrag = false; s_dHeld = false; s_dTick = 0;
		s_dDownX = s_dLastX = gx; s_dDownY = s_dLastY = gy;
	}
	if (touching && s_dDown) {
		s_dTick++;
		if (gvalid) {
			if (!s_dDrag && (abs(gx - s_dDownX) > DLGGEOM_SLOP_PX || abs(gy - s_dDownY) > DLGGEOM_SLOP_PX))
				s_dDrag = true;
			if (s_dDrag) {
				int d = dlggeom_drag_dir(gx - s_dLastX, gy - s_dLastY);
				if (d != DLGD_NONE) {
					s_dLastX = gx; s_dLastY = gy; s_dSteps++;
					return s_keyDir[d];       // {RIGHT,LEFT,DOWN,UP} — dlggeom's enum order, pinned
				}                             // by test_touchgeom TEST 17
				return 0;
			}
		}
		// unmoved and still down: the HOLD verb. Level-triggered, so it is a real held B.
		if (!s_dDrag && allowB && s_dTick >= DLGGEOM_HOLD_FRAMES) {
			if (!s_dHeld) { s_dHeld = true; s_dHolds++; }
			return 1 << GBAKEY_B;
		}
		return 0;
	}
	if (!touching && s_dDown) {                // released
		s_dDown = false;
		int wasDrag = s_dDrag, wasHeld = s_dHeld;
		s_dDrag = false; s_dHeld = false; s_dTick = 0;
		if (!wasDrag && !wasHeld) {            // a CLEAN tap — the only thing that becomes A
			switch (dlggeom_tap(s_dDownX, s_dDownY, pager)) {
			case DLGH_PAGE_PREV: s_dPulseKey = 1 << GBAKEY_LEFT;  s_dPages++; break;
			case DLGH_PAGE_NEXT: s_dPulseKey = 1 << GBAKEY_RIGHT; s_dPages++; break;
			default:             s_dPulseKey = 1 << GBAKEY_A;     s_dTaps++;  break;
			}
			s_dPulse = 2;                      // this frame 0, next frame the key => a clean edge
		}
	}
	return 0;
}

// ============== FAM-MAP: the REGION MAP / TAP-TO-FLY family (phase 24, lane B2) ===============
// TOUCH-PLAN rows B7 (wall map) + B8 (fly map). The pret derivation, the cell<->pixel inverse and
// the "one-frame press per cell" argument are all in touchgeom.h; this is the state machine.
//
// It is the STORAGE family's shape, not the dialog family's: a tap arms a TARGET CELL and the
// driver walks the game's OWN cursor there with single-frame D-pad presses chosen each frame from
// the LIVE cursorPosX/cursorPosY read, then — on the fly map only, and only when the live
// mapSecType says the game will accept it — presses A. Nothing is ever written to the game's RAM:
// writing cursorPosX/Y directly would desync the cursor SPRITE, which the engine slides
// incrementally (SpriteCB_CursorMapFull) instead of deriving from the logical position.
//
// Gestures (M3): clean tap = go there (+ fly confirm) · drag = the target FOLLOWS the finger,
// cursor only, no A (this is how you read map names) · hold = B, which cancels any armed target
// and closes the map, matching the FAM-DLG hold verb the screen had before this slice.
// What the arrival A would MEAN on the armed cell (phase 25 lane D1 replaced the old 0/1 flag:
// FireRed adds a second, unconditional reason to press A, and a bare bool could not say which).
enum { MAP_ACT_NONE = 0, MAP_ACT_FLY = 1, MAP_ACT_CANCEL = 2 };
static int s_mpTgtX = -1, s_mpTgtY = 0;    // armed target cell (-1 = idle)
static int s_mpAct = 0;                    // MAP_ACT_* — what to do when the live cursor arrives
static int s_mpArm = 0;                    // arrival settle countdown
static int s_mpATick = 0;                  // remaining A-pulse frames
static int s_mpGap = 0;                    // idle frames between cursor presses
static int s_mpTotal = 0;                  // frames since the target was armed (timeout)
static int s_mpTick = 0; static bool s_mpDown = false, s_mpDrag = false, s_mpHeld = false;
static int s_mpDownX = 0, s_mpDownY = 0;
static int s_mpTaps = 0, s_mpSteps = 0, s_mpArrive = 0, s_mpFly = 0, s_mpHolds = 0;  // PROOF counters
static int s_mpCancels = 0;   // PHASE 25 (lane D1): arrivals on FireRed's on-screen CANCEL button
static void map_reset(void) {
	s_mpTgtX = -1; s_mpTgtY = 0; s_mpAct = 0; s_mpArm = 0; s_mpATick = 0; s_mpGap = 0; s_mpTotal = 0;
	s_mpTick = 0; s_mpDown = false; s_mpDrag = false; s_mpHeld = false;
	// the five counters survive on purpose: they are the gdb proof channel (g_touchDbg +0xD4..).
}
#define MAPNAV_GAP      5   // idle frames after a cursor press. The engine's own slide is 4 frames
                            // (cursorMovementFrameCounter = 4) and it does not poll input during
                            // it, so this is "one press per slide" with a frame of margin.
#define MAPNAV_A_DELAY  6   // arrival -> A. cursorPosX/Y and mapSecType are written by the same
                            // function at slide end, so they agree the moment we see the arrival;
                            // the delay only lets the destination window draw before the confirm.
#define MAPNAV_TIMEOUT 420  // drop an unreached target (~7 s: the widest legal route is 27 cells
                            // x 6 frames = 162, so this only ever fires on a screen that stopped
                            // accepting input — a fade, a zoom, a sub-menu)

// PHASE 25 (lane D1): the state carries its ENGINE, because FireRed's region map is a separate
// implementation with its own struct layout and its own cell grid (touchgeom.h FAM-MAP table).
typedef struct { int curX, curY, secId, secType; const MapGeom* g; int variant; } RMapState;
static int rmap_read(const TouchSmart* sm, RMapState* ms) {
	const GameProfile* p = sm->prof;
	if (!sm->core || !p || !p->rmPtr) return 0;
	uint32_t base = gbacore_read32(sm->core, p->rmPtr);      // sRegionMap: a POINTER, always deref
	if ((base >> 24) != 0x02) return 0;                      // not an EWRAM struct: no map is live
	ms->variant = (p->rmVariant == GS_RMAP_FR) ? GS_RMAP_FR : GS_RMAP_EM;
	if (ms->variant == GS_RMAP_FR) {
		// pokefirered keeps the CURSOR in a second allocation (`sMapCursor`), so this is the one
		// place the family needs two pointers. struct MapCursor (:206-224, size 0x124):
		// x s16 +0x00 · y s16 +0x02 · selectedMapsec u16 +0x14 · selectedMapsecType u16 +0x16.
		if (!p->rmCurPtr) return 0;
		uint32_t cur = gbacore_read32(sm->core, p->rmCurPtr);
		if ((cur >> 24) != 0x02) return 0;                   // the map is up but the cursor is not
		ms->g       = &MAPGEOM_FR;
		ms->curX    = (int16_t)gbacore_read16(sm->core, cur + 0x00);
		ms->curY    = (int16_t)gbacore_read16(sm->core, cur + 0x02);
		ms->secId   = gbacore_read16(sm->core, cur + 0x14);
		ms->secType = gbacore_read16(sm->core, cur + 0x16);
	} else {
		if (gbacore_read8(sm->core, base + 0x78) != 0) return 0;  // zoomed: a DIFFERENT cursor model
		ms->g       = &MAPGEOM_EM;
		ms->curX    = gbacore_read16(sm->core, base + 0x54);
		ms->curY    = gbacore_read16(sm->core, base + 0x56);
		ms->secId   = gbacore_read16(sm->core, base + 0x00);
		ms->secType = gbacore_read8 (sm->core, base + 0x02);
	}
	// The same range test mapnav_step_g makes, applied here too so a mid-init struct never even
	// reaches the navigator (and so the g_touchDbg mirror shows the raw read that failed).
	if (ms->curX < ms->g->xMin || ms->curX > ms->g->xMax) return 0;
	if (ms->curY < ms->g->yMin || ms->curY > ms->g->yMax) return 0;
	return 1;
}

static void map_arm(int cx, int cy, int act) {
	s_mpTgtX = cx; s_mpTgtY = cy; s_mpAct = act;
	s_mpArm = 0; s_mpGap = 0; s_mpTotal = 0;
}

static u16 map_update(const TouchSmart* sm, bool touching, bool newPress, bool gvalid,
                      int gx, int gy) {
	RMapState ms;
	if (!rmap_read(sm, &ms)) { map_reset(); return 0; }        // no anchors / not a live full map
	if (s_mpATick > 0) { s_mpATick--; return 1 << GBAKEY_A; } // finish the 2-frame confirm pulse

	if (newPress && gvalid) {
		s_mpDown = true; s_mpDrag = false; s_mpHeld = false; s_mpTick = 0;
		s_mpDownX = gx; s_mpDownY = gy;
	}
	if (touching && s_mpDown) {
		s_mpTick++;
		if (gvalid && !s_mpDrag &&
		    (abs(gx - s_mpDownX) > DLGGEOM_SLOP_PX || abs(gy - s_mpDownY) > DLGGEOM_SLOP_PX))
			s_mpDrag = true;
		if (s_mpDrag) {
			int cx, cy;                                       // the target follows the finger…
			if (gvalid && mapgeom_hit_g(ms.g, gx, gy, &cx, &cy) && (cx != s_mpTgtX || cy != s_mpTgtY))
				map_arm(cx, cy, MAP_ACT_NONE);                // …with NO arrival A (M3), and that
				                                              // includes dragging onto CANCEL
		} else if (s_mpTick >= DLGGEOM_HOLD_FRAMES) {
			if (!s_mpHeld) { s_mpHeld = true; s_mpHolds++; }
			map_reset(); s_mpDown = true; s_mpHeld = true;    // a hold cancels the route it armed
			return 1 << GBAKEY_B;
		}
	}
	if (!touching && s_mpDown) {
		s_mpDown = false;
		int wasDrag = s_mpDrag, wasHeld = s_mpHeld;
		s_mpDrag = false; s_mpHeld = false; s_mpTick = 0;
		if (!wasDrag && !wasHeld) {                            // a CLEAN tap = go there
			int cx, cy;
			if (mapgeom_hit_g(ms.g, s_mpDownX, s_mpDownY, &cx, &cy)) {
				// What an arrival A would MEAN on this cell, on this engine, in this mode:
				//   CANCEL — FireRed draws a CANCEL button and A there is MAP_INPUT_CANCEL in
				//            every mode (HandleRegionMapInput :2795-2799), so a tap on the button
				//            the player can see does what the button says. Emerald has none
				//            (MAPGEOM_EM.cancelX = -1), so this arm simply never fires there.
				//   FLY    — only on a fly map, and only if the game's own acceptance test passes
				//            when we ARRIVE (checked below against the live mapSecType, not now).
				int act = MAP_ACT_NONE;
				if (ms.g->cancelX >= 0 && cx == ms.g->cancelX && cy == ms.g->cancelY) act = MAP_ACT_CANCEL;
				else if (sm->mapFly) act = MAP_ACT_FLY;       // A only where A means "fly" (M2)
				map_arm(cx, cy, act);
				s_mpTaps++;
			}
		}
	}

	if (s_mpTgtX < 0) return 0;
	if (++s_mpTotal > MAPNAV_TIMEOUT) { map_reset(); return 0; }
	if (ms.curX == s_mpTgtX && ms.curY == s_mpTgtY) {         // live cursor == target
		if (s_mpArm == 0) s_mpArm = MAPNAV_A_DELAY;
		if (--s_mpArm > 0) return 0;
		int act = s_mpAct;
		s_mpTgtX = -1; s_mpAct = MAP_ACT_NONE; s_mpArm = 0; s_mpArrive++;
		// The game's OWN acceptance test, per engine (mapgeom_fly_ok): A on anything else is
		// ignored by the engine, so emitting it would be noise we could not distinguish from a bug.
		if (act == MAP_ACT_FLY && mapgeom_fly_ok(ms.g, ms.secType)) {
			s_mpFly++; s_mpATick = 1; return 1 << GBAKEY_A;
		}
		// The CANCEL button needs no mapsec test — the engine's test is the CURSOR CELL itself.
		if (act == MAP_ACT_CANCEL) { s_mpCancels++; s_mpATick = 1; return 1 << GBAKEY_A; }
		return 0;
	}
	s_mpArm = 0;                                              // moved off target mid-settle
	if (s_mpGap > 0) { s_mpGap--; return 0; }
	int mn = mapnav_step_g(ms.g, ms.curX, ms.curY, s_mpTgtX, s_mpTgtY);
	if (!mn) { map_reset(); return 0; }
	s_mpGap = MAPNAV_GAP; s_mpSteps++;
	u16 k = 0;                                                // X and Y are read independently by
	if (mn & MN_RIGHT) k |= 1 << GBAKEY_RIGHT;                // ProcessRegionMapInput_Full, so a
	if (mn & MN_LEFT)  k |= 1 << GBAKEY_LEFT;                 // diagonal step costs ONE frame
	if (mn & MN_DOWN)  k |= 1 << GBAKEY_DOWN;
	if (mn & MN_UP)    k |= 1 << GBAKEY_UP;
	return k;
}

// ============== FAM-NAV: the POKÉNAV MENUS (phase 25, lane D2) ================================
// TOUCH-PLAN row G1. Shape-wise this is FAM-MAP with one axis: a tap arms a TARGET ROW and the
// driver walks the game's own `cursorPos` there with single-frame D-pad presses chosen from the
// LIVE read each frame, then presses A. What is new — and what makes a naive port measurably
// worse — is that the list WRAPS (touchgeom.h navnav_step), so the route from the bottom row to
// the top is ONE press up, not rows-1 presses down.
//
// Nothing is ever written. On this screen that rule has a sharper justification than anywhere
// else in the family: A acts on `cursorPos`, but the HIGHLIGHT and the option DESCRIPTION are
// driven by `currMenuItem` plus the gfx layer's own copy, and only `UpdateMenuCursorPos` moves
// all three together. A RAM write would leave a screen whose highlighted row is not the row A
// picks — a lie on screen, not merely a desynced sprite.
//
// Gestures: clean tap = go there and PICK · drag = the target follows the finger, cursor only,
// no A (so you can read the option descriptions the way you read map names on FAM-MAP) ·
// hold = B, which is BACK on the condition menus and EXIT on the main menu — the same verb the
// screen already had as a bare GCTX_FULLUI.
//
// Unlike FAM-MAP, the arrival A is ALWAYS armed. Every row here is a labelled button whose label
// says what A does, including `SWITCH OFF` (POKENAV_MENU_FUNC_EXIT — the game's own "close the
// PokéNav"), so there is no cell where the confirm means something the tap did not ask for.
static int s_nvTgt = -1;                   // armed target row (-1 = idle)
static int s_nvArm = 0, s_nvATick = 0, s_nvGap = 0, s_nvTotal = 0;
static int s_nvWait = 0, s_nvFrom = -1;    // a D-pad press IN FLIGHT: frames left, cursor pressed from
static int s_nvPend = 0, s_nvPendT = 0, s_nvPendType = -1;   // A retries left / countdown / latched menuType
static int s_nvTick = 0; static bool s_nvDown = false, s_nvDrag = false, s_nvHeld = false;
static int s_nvDownX = 0, s_nvDownY = 0;
static int s_nvTaps = 0, s_nvSteps = 0, s_nvWraps = 0, s_nvArrive = 0, s_nvPicks = 0, s_nvHolds = 0;
static int s_nvRepress = 0, s_nvRetry = 0;   // presses/A-pulses the ENGINE swallowed and we re-sent
static void nav_reset(void) {
	s_nvTgt = -1; s_nvArm = 0; s_nvATick = 0; s_nvGap = 0; s_nvTotal = 0;
	s_nvWait = 0; s_nvFrom = -1; s_nvPend = 0; s_nvPendT = 0; s_nvPendType = -1;
	s_nvTick = 0; s_nvDown = false; s_nvDrag = false; s_nvHeld = false;
	// the counters survive on purpose: they are the gdb proof channel (g_touchDbg +0x128..).
}
// One press per TWO frames, minimum: UpdateMenuCursorPos is JOY_NEW, so the key must be RELEASED
// before the next edge can register. The gap is 3 to leave a frame of margin either side of the
// engine's own input sampling, exactly as MAPNAV_GAP does for the region map's 4-frame slide.
// MEASURED, then fixed (emulator run 20260815-001057, Entry 7): the engine does not poll input
// for the whole of a cursor move. `Task_Pokenav` case 3 hands a MOVE_CURSOR result to
// `RunMainMenuLoopedTask` and parks in case 2 until that looped task finishes (pokenav.c:449-457),
// and the option slide alone is 4 frames (`StartOptionSlide(..., 4)`, menu_handler_gfx.c:944-946)
// before the description window is even redrawn. A fixed inter-press gap therefore GUESSES, and
// the first measurement caught it doing so: a two-row hop cost THREE presses, one swallowed.
//
// So the pacing is closed-loop on the cursor, like everything else in this family: press, then
// wait until `cursorPos` actually MOVES before pressing again. A press that has not landed after
// NAVNAV_PRESS_WAIT frames is assumed swallowed and re-sent — counted separately (`navRepress`),
// so `navSteps` stays the EXACT ring distance and a regression cannot hide inside it.
#define NAVNAV_GAP        2   // mandatory RELEASED frames after any press: UpdateMenuCursorPos is
                              // JOY_NEW, so two presses with no gap are one held key = one edge
#define NAVNAV_PRESS_WAIT 40  // ...then this long for the cursor to move before assuming a swallow
// The same problem applies to the confirm: an A that arrives while the last slide's looped task is
// still running is never seen by HandleMainMenuInput. It is answered the same way — a bounded
// retry that STOPS the moment the game acts (menuType changes, or the sub-app changes and
// game_read stops claiming the screen at all, which resets this whole module).
#define NAVNAV_A_DELAY 14
#define NAVNAV_A_RETRY 14
#define NAVNAV_A_TRIES  6
#define NAVNAV_TIMEOUT 420   // drop an unreached target (~7 s). The longest legal route is half a
                             // 6-row ring = 3 presses, so this only fires on a screen that stopped
                             // accepting input (a fade, a sub-app hand-off).

// Read the live menu. Returns 0 — and the caller then emits NOTHING — whenever the screen is not
// a driveable menu, which is the same "no upgrade, never blind" contract rmap_read follows.
typedef struct { int type, cur, rows; } NavState;
static int nav_read(const TouchSmart* sm, NavState* ns) {
	if (!sm->core || !sm->pnBase) return 0;         // game_read refused it: nothing is claimed here
	ns->rows = navgeom_rows(sm->pnMenuType);
	if (ns->rows <= 0) return 0;
	ns->type = sm->pnMenuType;
	ns->cur  = (int16_t)gbacore_read16(sm->core, sm->pnBase + GS_PN_CURSOR_OFF);
	if (ns->cur < 0 || ns->cur >= ns->rows) return 0;   // a mid-transition menu: never drive it
	// The menuType is re-read LIVE rather than trusted from the snapshot, because the two condition
	// menus mutate it IN PLACE (HandleMainMenuInput :218-222 sets menuType = CONDITION without any
	// sub-app change), so a target armed against the old geometry must die the moment it changes.
	int live = (int)gbacore_read16(sm->core, sm->pnBase + GS_PN_MENUTYPE_OFF);
	if (live != ns->type) return 0;
	return 1;
}

static u16 nav_update(const TouchSmart* sm, bool touching, bool newPress, bool gvalid,
                      int gx, int gy) {
	NavState ns;
	if (!nav_read(sm, &ns)) { nav_reset(); return 0; }
	if (s_nvATick > 0) { s_nvATick--; return 1 << GBAKEY_A; }   // finish the 2-frame confirm pulse

	if (newPress && gvalid) {
		s_nvDown = true; s_nvDrag = false; s_nvHeld = false; s_nvTick = 0;
		s_nvDownX = gx; s_nvDownY = gy;
	}
	if (touching && s_nvDown) {
		s_nvTick++;
		if (gvalid && !s_nvDrag &&
		    (abs(gx - s_nvDownX) > DLGGEOM_SLOP_PX || abs(gy - s_nvDownY) > DLGGEOM_SLOP_PX))
			s_nvDrag = true;
		if (s_nvDrag) {
			int row;                                    // the target follows the finger, no pick
			if (gvalid && navgeom_hit(ns.type, gx, gy, &row) && row != s_nvTgt) {
				s_nvTgt = row; s_nvArm = 0; s_nvGap = 0; s_nvTotal = 0;
			}
		} else if (s_nvTick >= DLGGEOM_HOLD_FRAMES) {
			if (!s_nvHeld) { s_nvHeld = true; s_nvHolds++; }
			nav_reset(); s_nvDown = true; s_nvHeld = true;   // a hold cancels the route it armed
			return 1 << GBAKEY_B;
		}
	}
	if (!touching && s_nvDown) {
		s_nvDown = false;
		int wasDrag = s_nvDrag, wasHeld = s_nvHeld;
		s_nvDrag = false; s_nvHeld = false; s_nvTick = 0;
		if (!wasDrag && !wasHeld) {                     // a CLEAN tap = go there and pick
			int row;
			if (navgeom_hit(ns.type, s_nvDownX, s_nvDownY, &row)) {
				s_nvTgt = row; s_nvArm = 0; s_nvGap = 0; s_nvTotal = 0; s_nvTaps++;
				s_nvWait = 0; s_nvFrom = -1; s_nvPend = 0;   // a new tap supersedes everything
			}
		}
	}

	// The confirm's bounded retry. It runs OUTSIDE the target logic because by the time it matters
	// the target is already disarmed: the arrival consumed it. It ends the instant the menu
	// mutates — and if the pick opened a FEATURE sub-app instead, game_read stops claiming the
	// screen and nav_read's reset above has already cleared this.
	if (s_nvPend > 0) {
		if (ns.type != s_nvPendType) s_nvPend = 0;                 // the game ACTED on the A
		else if (--s_nvPendT <= 0) {
			s_nvPend--; s_nvPendT = NAVNAV_A_RETRY; s_nvRetry++;
			s_nvATick = 1; return 1 << GBAKEY_A;
		}
	}

	if (s_nvTgt < 0) return 0;
	if (++s_nvTotal > NAVNAV_TIMEOUT) { nav_reset(); return 0; }
	if (ns.cur == s_nvTgt) {                            // live cursorPos == target
		if (s_nvArm == 0) s_nvArm = NAVNAV_A_DELAY;
		if (--s_nvArm > 0) return 0;
		s_nvTgt = -1; s_nvArm = 0; s_nvWait = 0; s_nvArrive++;
		s_nvPicks++; s_nvATick = 1;
		s_nvPend = NAVNAV_A_TRIES - 1; s_nvPendT = NAVNAV_A_RETRY; s_nvPendType = ns.type;
		return 1 << GBAKEY_A;
	}
	s_nvArm = 0;                                        // moved off target mid-settle
	if (s_nvGap > 0) { s_nvGap--; return 0; }           // the mandatory released frame(s)
	// Has the press we already sent landed? The cursor itself is the answer.
	int fresh = 1;
	if (s_nvWait > 0) {
		if (ns.cur != s_nvFrom) s_nvWait = 0;           // it landed -> the next press is a new step
		else if (--s_nvWait > 0) return 0;              // still in flight: press nothing
		else fresh = 0;                                 // deadline: the engine swallowed it
	}
	int st = navnav_step(ns.cur, s_nvTgt, ns.rows);
	if (!st) { nav_reset(); return 0; }
	if (fresh) {
		s_nvSteps++;
		// A press is a WRAP when it moves AWAY from the target in plain-difference terms — i.e.
		// only a navigator that knows the ring can produce it. Counting them is what makes the
		// wrap provable from outside: no straight-line driver can ever bump this.
		if ((st == NAVNAV_DOWN && s_nvTgt < ns.cur) || (st == NAVNAV_UP && s_nvTgt > ns.cur)) s_nvWraps++;
	} else s_nvRepress++;
	s_nvFrom = ns.cur; s_nvWait = NAVNAV_PRESS_WAIT; s_nvGap = NAVNAV_GAP;
	return (st == NAVNAV_DOWN) ? (u16)(1 << GBAKEY_DOWN) : (u16)(1 << GBAKEY_UP);
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
	case GCTX_STORAGE:       if (p && p->stCursor) { *addr = p->stCursor + 1u; *size = 1; } break;   // 22.2 sCursorPosition
	// PHASE 25 (lane D2) FAM-NAV: struct Pokenav_Menu.cursorPos, a SIGNED 16-bit field. The log's
	// before/after pair therefore reads the exact value HandleMainMenuInput indexes sMenuItems[]
	// with — i.e. "did the row the tap asked for become the row A will pick".
	case GCTX_POKENAV:       if (sm->pnBase) { *addr = sm->pnBase + GS_PN_CURSOR_OFF; *size = 2; } break;
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
	g_fieldDbg.planForced = -1;    // an INDEX: "nothing found" is -1, and 0 would read as step 0
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
	// phase 23 FAM-DLG: stamped FIRST, above every early return, because the tap-advance family is
	// the one that runs on screens where `p` may carry nothing else worth reading — its proof
	// channel must survive exactly the situations the other mirrors bail out of.
	d->dlgTaps = s_dTaps; d->dlgHolds = s_dHolds; d->dlgPages = s_dPages; d->dlgSteps = s_dSteps;
	d->dlgPager = dlg_is_pager(sm);
	// PHASE 24 / lane B1 — the field-dialog routing channel, stamped beside the FAM-DLG counters
	// and for the same reason: it must survive every early return below.
	d->textDlg = sm->textDlg ? 1 : 0;
	d->fieldLock = sm->fieldLock ? 1 : 0;
	d->dlgOwns = (dlggeom_route(sm->ctx, d->textDlg, d->fieldLock) == DLGROUTE_DLG);
	d->msgMode = -1;
	// PHASE 24 / lane B2 — FAM-MAP, stamped beside the FAM-DLG block for the same survive-every-
	// early-return reason. The counters are app-side and always valid; the four live reads are -1
	// until rmap_read succeeds, which is itself the diagnosis when a tap "does nothing".
	d->mapTaps = s_mpTaps; d->mapSteps = s_mpSteps; d->mapArrive = s_mpArrive;
	d->mapFlies = s_mpFly; d->mapHolds = s_mpHolds;
	d->mapTgtX = s_mpTgtX; d->mapTgtY = (s_mpTgtX < 0) ? -1 : s_mpTgtY;
	d->mapIsFly = sm->mapFly ? 1 : 0;
	d->mapCurX = d->mapCurY = d->mapSecId = d->mapSecType = -1;
	d->mapVariant = d->mapFrType = -1;
	d->mapCancels = s_mpCancels;
	d->qlState = -1;                     // PHASE 25: -1 = no quest log for this game / no profile
	// PHASE 25 (lane D2) FAM-NAV. The counters are unconditional (they are cumulative proof); the
	// five live reads default to -1 = "not a PokéNav frame", and are filled below for EVERY frame
	// the PokéNav callback is up — the FEATURE sub-apps included, which is the whole point of the
	// sub-state mirror.
	d->navIdx = d->navType = d->navCur = d->navRows = d->navMode = -1;
	d->navTaps = s_nvTaps; d->navSteps = s_nvSteps; d->navWraps = s_nvWraps;
	d->navArrive = s_nvArrive; d->navPicks = s_nvPicks; d->navHolds = s_nvHolds;
	d->navTgt = s_nvTgt; d->navRepress = s_nvRepress; d->navRetry = s_nvRetry;
	const GameProfile* p = sm->prof;
	if (!sm->core || !p) return;
	// PHASE 25 (lane C1): stamped immediately after the null check, i.e. ABOVE the GCTX_MAP /
	// GCTX_NAMING early returns, because "why did touch go silent" must be answerable on every
	// screen — including the ones whose own mirrors bail out.
	if (p->questLog) d->qlState = (int32_t)gbacore_read8(sm->core, p->questLog);
	if (sm->ctx == GCTX_MAP) {
		RMapState ms;
		if (rmap_read(sm, &ms)) {
			d->mapCurX = ms.curX; d->mapCurY = ms.curY;
			d->mapSecId = ms.secId; d->mapSecType = ms.secType;
			d->mapVariant = ms.variant;
			if (ms.variant == GS_RMAP_FR && p->rmPtr) {       // the mode byte, mirrored raw: this
				uint32_t rmb = gbacore_read32(sm->core, p->rmPtr);   // is the value that decides
				if ((rmb >> 24) == 0x02)                             // whether mapIsFly is 1
					d->mapFrType = (int32_t)gbacore_read8(sm->core, rmb + GS_FR_RM_TYPE_OFF);
			}
		}
	}
	// PHASE 25 (lane D2): the PokéNav sub-state mirror. Deliberately keyed on the CALLBACK and not
	// on `ctx == GCTX_POKENAV`, because the values worth capturing are exactly the ones game_read
	// refused to claim: currentMenuIndex >= 6 is a FEATURE sub-app (Hoenn map / condition graph /
	// search results / Match Call / ribbons), and until now nothing on this machine could tell one
	// from another. This is TOUCH-PLAN 22.7's "gdb-capture the pokenav state var across sub-app
	// hops", and it costs three bus reads on one screen. LOGGING ONLY.
	if (p->pokenavCb && p->pokenavPtr &&
	    (sm->cb2 == p->pokenavCb || (p->pokenavCbAlt && sm->cb2 == p->pokenavCbAlt))) {
		uint32_t res = gbacore_read32(sm->core, p->pokenavPtr);
		if ((res >> 24) == 0x02) {
			d->navIdx  = (int32_t)gbacore_read32(sm->core, res + GS_PN_MENUIDX_OFF);
			d->navMode = (int32_t)gbacore_read16(sm->core, res + GS_PN_MODE_OFF);
			uint32_t mh = gbacore_read32(sm->core, res + GS_PN_SUBSTRUCT_OFF + 4u * GS_PN_SUB_MENU);
			if ((mh >> 24) == 0x02) {
				d->navType = (int32_t)gbacore_read16(sm->core, mh + GS_PN_MENUTYPE_OFF);
				d->navCur  = (int32_t)(int16_t)gbacore_read16(sm->core, mh + GS_PN_CURSOR_OFF);
				d->navRows = navgeom_rows(d->navType);
			}
		}
	}
	if (p->fieldMsgMode) d->msgMode = (int32_t)gbacore_read8(sm->core, p->fieldMsgMode);
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
	if (sm->ctx == GCTX_STORAGE) {                    // phase 22.2: the storage GRID mirror (G8)
		StorState st;
		if (storage_read(sm, &st)) {
			d->stArea = st.area; d->stPos = st.pos; d->stHeld = st.held;
			d->stOrigBox = st.origBox; d->stOrigPos = st.origPos;
			d->stBoxId = st.boxId; d->stBoxOption = st.boxOption;
			d->stInParty = st.inParty; d->stMenuOpen = st.menuOpen;
			d->stOccupancy = st.occ;
		}
		d->stTgtArea = s_stTgtA; d->stTgtPos = s_stTgtP;
		return;
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

// --- PHASE 24 / lane A: the party + PC mon census mirror (LOGGING ONLY; touch.h documents it) --
// One call into fieldtrav_census — the SAME decrypt+checksum rail fieldtrav_usable gates on — so
// the answer to "which mon knows Fly, and is it in the party or a box?" comes from the code the
// router already trusts rather than a second parser. Throttled; reads only.
MonDbg g_monDbg = { 0 };
#define MONDBG_EVERY 90        // render frames between sweeps (~1.5 s at 60 fps)
static void mon_census_stamp(const TouchSmart* sm) {
	static int s_tick = 0;
	if (s_tick > 0) { s_tick--; return; }
	s_tick = MONDBG_EVERY;
	const GameProfile* p = sm ? sm->prof : 0;
	if (!sm || !sm->core || !p || !p->partyBase) return;
	// The moves worth naming: the five field moves the traversal family gates on, plus FLY —
	// which the router never uses but the PLAYER needs in the party for a cross-region proof, and
	// which is exactly the mon the census exists to locate. pokeemerald include/constants/moves.h:
	// MOVE_CUT 15, MOVE_FLY 19, MOVE_SURF 57, MOVE_STRENGTH 70, MOVE_DIVE 291, MOVE_WATERFALL 127,
	// MOVE_ROCK_SMASH 249.
	static const uint16_t kWanted[] = { 15, 19, 57, 70, 127, 249, 291 };
	uint32_t storage = 0;
	if (p->pcStoragePtr) {
		uint32_t s = gbacore_read32(sm->core, p->pcStoragePtr);
		if ((s >> 24) == 0x02u) storage = s;          // an unloaded save reads as garbage/0
	}
	// gPlayerPartyCount is read STRAIGHT off the core, the way prog_party does it — NOT from
	// TouchSmart.partyCount. Live evidence (2026-08-14, boot runs/20260814-130304): gamestate.c
	// only fills that field inside GCTX_PARTY (:677), so in the overworld it is -1 and the first
	// draft of this census reported an EMPTY PARTY on a save with a full one. A diagnostic that
	// lies about the thing it exists to measure is worse than no diagnostic.
	int count = 0;
	if (p->partyCount) {
		int n = (int)gbacore_read8(sm->core, p->partyCount);
		count = (n >= 0 && n <= 6) ? n : 0;
	}
	FpBus bus = { fp_r8, fp_r16, fp_r32, sm->core };
	fieldtrav_census(&bus, p->partyBase, count, storage, kWanted,
	                 (int)(sizeof kWanted / sizeof kWanted[0]), &g_monDbg.c);
	g_monDbg.storage = (int32_t)storage;
	g_monDbg.partyBase = (int32_t)p->partyBase;
	g_monDbg.seq++;
}

// ================================ dispatch ==================================
static void all_reset(void) { battle_reset(); walk_reset(); party_reset(); target_reset(); fmenu_reset(); list_reset(); naming_reset(); storage_reset(); dlg_reset(); prog_reset(); map_reset(); nav_reset(); }

u16 touch_update(TouchMode mode, bool touching, int sx, int sy, int gx, int gy, bool gvalid,
                 const TouchSmart* sm) {
	// PHASE 30 — HARDWARE DEFECT H2. `newPress` used to be the literal FIRST frame of contact, and
	// every one of the ten touch families latches its hit-test on it (party slot, list row, dex
	// cell, storage cell, keyboard key, map cell, dialog zone, the walk anchor...). That made all
	// of them share one weakness: on the real 3DS digitiser the first sample after contact is
	// NOISY — the panel reports a position while the finger is still landing and the contact patch
	// is still growing, so the reported point can sit several pixels from where the user believes
	// they touched, and further out for a soft or angled press.
	//
	// The emulator could never show this: a synthesized touch is exact from its first frame, which
	// is precisely why every one of these families passed its live proof and then missed on
	// hardware. The user's report — "party menus miss where I hit", lists not selecting, "glitchy"
	// — is one defect wearing ten costumes.
	//
	// Fix: settle first, THEN latch. `newPress` now fires on frame TOUCH_SETTLE of a contact and
	// carries that frame's coordinates, so every family reads a settled point without any of them
	// changing. The cost is one frame (~17 ms), which is below the threshold of perception and far
	// cheaper than a mis-hit. A tap must last >= TOUCH_SETTLE frames to register; a human tap is
	// ~5-9 frames on hardware, so the margin is large.
	#define TOUCH_SETTLE 2
	static bool wasTouching = false;
	static int  s_pressAge = 0;
	if (touching) { if (!wasTouching) s_pressAge = 0; s_pressAge++; } else s_pressAge = 0;
	bool newPress = touching && s_pressAge == TOUCH_SETTLE;
	wasTouching = touching;

	if (mode == TOUCH_PAD)   { all_reset(); return touching ? pad_keys(sx, sy) : 0; }
	if (mode != TOUCH_SMART) { all_reset(); return 0; }            // TOUCH_OFF
	if (!sm || !sm->valid)   { all_reset(); return 0; }

	mon_census_stamp(sm);      // LOGGING ONLY (phase 24) — throttled, reads only, injects nothing

	// LOGGING ONLY: read the ctx cursor BEFORE the dispatch (and AFTER, below) so a row shows old->new.
	uint32_t cursAddr; int cursSize;
	touch_cursor_addr(sm, &cursAddr, &cursSize);
	uint32_t before = touch_cursor_read(sm, cursAddr, cursSize);

	// PHASE 22.2 / SPEC-family-traversal H0.1. A route PROGRAM runs ABOVE the per-context
	// handlers, not inside GCTX_OVERWORLD, for one structural reason: the yes/no it exists to
	// answer IS a GCTX_FIELDMENU, so a program living in the overworld branch would be reset by
	// the very dialog it opened. While a program owns the frame nothing else is dispatched — the
	// tap that cancels it is swallowed whole (H4.3: the first tap STOPS, only a second tap
	// re-aims, so a mis-tap during a long route cannot redirect it).
	if (s_progSwallow) {
		if (!touching) s_progSwallow = false;
		return 0;
	}
	if (s_progFarewell > 0) {          // the dangling-textbox A, drained after the program is gone
		s_progFarewell--;
		g_fieldDbg.curKeys = 1u << GBAKEY_A;
		g_fieldDbg.progAKeys++;
		return 1u << GBAKEY_A;
	}
	if (s_seq.on) {
		u16 pk = prog_update(sm, touching, newPress, sm->px, sm->py, sm->mapGroup, sm->mapNum);
		g_fieldDbg.curKeys = pk;
		g_fieldDbg.curMapGroup = sm->mapGroup; g_fieldDbg.curMapNum = sm->mapNum;
		g_fieldDbg.curPx = sm->px; g_fieldDbg.curPy = sm->py;
		g_fieldDbg.curFrame = sm->core ? (int32_t)gbacore_frame_counter(sm->core) : 0;
		return pk;
	}

	u16 ret = 0;
	// PHASE 24 / lane B1: a FIELD DIALOG is a FAM-DLG screen. Resolved ONCE here so the reset line
	// below and the OVERWORLD arm cannot disagree about who owns the frame (touchgeom.h documents
	// the rule and the live evidence; test_touchgeom TEST 18 grades it).
	int dlgOwns = (dlggeom_route(sm->ctx, sm->textDlg ? 1 : 0, sm->fieldLock ? 1 : 0) == DLGROUTE_DLG);
	// PHASE 23 / FAM-DLG: one line instead of adding dlg_reset() to nine per-case reset lists — the
	// tap-advance state must die the moment the screen stops being a FAM-DLG screen (a half-finished
	// hold must never leak a B into the battle menu the dialog just opened).
	if (sm->ctx != GCTX_FULLUI && sm->ctx != GCTX_TITLE && !dlgOwns) dlg_reset();
	switch (sm->ctx) {
	case GCTX_BATTLE_ACTION:
	case GCTX_BATTLE_MOVE: {
		walk_reset(); party_reset(); target_reset(); fmenu_reset(); list_reset(); naming_reset(); storage_reset();
		uint32_t base = (sm->ctx == GCTX_BATTLE_ACTION) ? sm->actionAddr : sm->moveAddr;
		if (newPress && gvalid) {
			int cell = (sm->ctx == GCTX_BATTLE_ACTION) ? hit_action(gx, gy) : hit_move(gx, gy);
			if (cell >= 0) { s_target = cell; s_selTick = 0; }
		}
		ret = menu_select(sm->core, base);
		break;
	}
	case GCTX_BATTLE_TARGET:
		battle_reset(); walk_reset(); party_reset(); fmenu_reset(); list_reset(); naming_reset(); storage_reset(); map_reset(); nav_reset();
		if (newPress && gvalid) {
			int pos = hit_battler(gx, gy);
			if (pos >= 0) { int idx = battler_index_for_pos(sm, pos); if (idx >= 0) { s_tgt = idx; s_tgtTick = 0; } }
		}
		ret = select_pulse(sm->core, sm->prof ? sm->prof->multiCursor : 0, &s_tgt, &s_tgtTick);
		break;
	case GCTX_PARTY:
		battle_reset(); walk_reset(); target_reset(); fmenu_reset(); list_reset(); naming_reset(); storage_reset(); map_reset(); nav_reset();
		if (newPress && gvalid) {
			int slot = hit_party(gx, gy, sm->partyLayout);
			if (slot == 7 || (slot >= 0 && slot < sm->partyCount)) { s_party = slot; s_partyTick = 0; }
		}
		ret = select_pulse(sm->core, sm->prof ? sm->prof->partyMenu + 0x09 : 0, &s_party, &s_partyTick);
		break;
	case GCTX_OVERWORLD:
		battle_reset(); party_reset(); target_reset(); fmenu_reset(); list_reset(); naming_reset(); storage_reset(); map_reset(); nav_reset();
		if (dlgOwns) {
			// A script is talking (sFieldMessageBoxMode != 0). FAM-DLG owns the frame: tap = A
			// (the box advances from ANYWHERE, not only from the player's own tile), hold = B,
			// drag = D-pad. Critically it also means walk_update is NOT CALLED, so no route can
			// be planned at a game that cannot move — the leak this arm exists to end.
			//
			// The walker's state is deliberately FROZEN, not reset: a textbox that opens MID-ROUTE
			// (the Match Call the traversal lane lost two boots to) now pauses the route instead of
			// stalling it out, and the route resumes on the frame the box closes. s_stall is not
			// advanced while we are away, so the resume is clean.
			ret = dlg_update(sm, touching, newPress, gvalid, gx, gy, 1);
			// Keep the field mirror honest — it is the proof channel this defect was found with.
			g_fieldDbg.curKeys = ret;
			g_fieldDbg.curMapGroup = sm->mapGroup; g_fieldDbg.curMapNum = sm->mapNum;
			g_fieldDbg.curPx = sm->px; g_fieldDbg.curPy = sm->py;
			g_fieldDbg.curFrame = sm->core ? (int32_t)gbacore_frame_counter(sm->core) : 0;
			break;
		}
		ret = walk_update(touching, newPress, gvalid, gx, gy, sm->px, sm->py,
		                  sm->mapGroup, sm->mapNum, sm->core, sm->prof, sm->traverse);
		break;
	case GCTX_FIELDMENU:
		battle_reset(); walk_reset(); party_reset(); target_reset(); list_reset(); naming_reset(); storage_reset(); map_reset(); nav_reset();
		if (newPress && gvalid && sm->prof) { int i = hit_fieldmenu(sm->core, sm->prof, gx, gy); if (i >= 0) { s_fmenu = i; s_fmenuTick = 0; } }
		ret = sm->prof ? fmenu_select(sm->core, sm->prof) : 0;
		break;
	case GCTX_BAG:
		battle_reset(); walk_reset(); party_reset(); target_reset(); fmenu_reset(); naming_reset(); storage_reset(); map_reset(); nav_reset();
		if (s_lPrevKind != -2) { list_reset(); s_lPrevKind = -2; }   // arriving from another ctx/kind
		ret = list_update(sm, sm->bagListTaskBase, LF_BAG, touching, newPress, gvalid, gx, gy);
		break;
	case GCTX_LIST:
		battle_reset(); walk_reset(); party_reset(); target_reset(); fmenu_reset(); naming_reset(); storage_reset(); map_reset(); nav_reset();
		// a kind change (buy -> qty -> buy) mid-context resets the shared gesture state
		if (s_lPrevKind != (int)sm->listKind) { list_reset(); s_lPrevKind = (int)sm->listKind; }
		switch (sm->listKind) {
		case LK_BUY:
		case LK_PCITEM:
		// PHASE 24 (lane B2): the DISCOVERED list (FR Berry Pouch / TM Case). Deliberately the
		// SAME driver and the same flags as an anchored list — the only thing that differed was
		// how listBase was found, and by the time it reaches here that difference is gone.
		case LK_FULLUI: ret = list_update(sm, sm->listBase, 0, touching, newPress, gvalid, gx, gy); break;
		case LK_QTY:    ret = qty_update(sm, touching, newPress, gvalid, gx, gy); break;
		case LK_DEX:    ret = dex_update(sm, touching, newPress, gvalid, gx, gy); break;
		default:        ret = 0; break;               // unknown kind: emit nothing (L10)
		}
		break;
	case GCTX_NAMING:
		battle_reset(); walk_reset(); party_reset(); target_reset(); fmenu_reset(); list_reset(); storage_reset(); map_reset(); nav_reset();
		ret = naming_update(sm, touching, newPress, gvalid, gx, gy, sx, sy);
		break;
	case GCTX_STORAGE:
		// fmenu state is deliberately NOT reset here: the storage popups delegate to the fmenu
		// machinery (SPEC-family-grid G6) — storage_update owns its lifecycle.
		battle_reset(); walk_reset(); party_reset(); target_reset(); list_reset(); naming_reset(); map_reset(); nav_reset();
		ret = storage_update(sm, touching, newPress, gvalid, gx, gy);
		break;
	// PHASE 24 (lane B2): FAM-MAP — the region map, and with it TAP-TO-FLY. Placed before the
	// GCTX_FULLUI arm it was promoted out of: both map cb2s remain in cb2FullUi, so a game whose
	// profile has no map anchors (FR/LG/RS today) still lands there and keeps tap=A/hold=B/drag.
	case GCTX_MAP:
		battle_reset(); walk_reset(); party_reset(); target_reset(); fmenu_reset(); list_reset(); naming_reset(); storage_reset(); nav_reset();
		ret = map_update(sm, touching, newPress, gvalid, gx, gy);
		break;
	// PHASE 25 (lane D2): FAM-NAV — the PokéNav MENUS. Placed with the other promoted-out-of-FULLUI
	// families for the same reason: CB2_Pokenav stays in cb2FullUi, so a game with no PokéNav
	// anchors (FR/LG/RS) or a FEATURE sub-app (Hoenn map / Match Call / ribbons / graphs, which
	// game_read deliberately does not claim) lands there and keeps tap=A / hold=B / drag.
	case GCTX_POKENAV:
		battle_reset(); walk_reset(); party_reset(); target_reset(); fmenu_reset(); list_reset(); naming_reset(); storage_reset(); map_reset();
		ret = nav_update(sm, touching, newPress, gvalid, gx, gy);
		break;
	case GCTX_BATTLE_OTHER:
		all_reset();
		ret = touching ? (1 << GBAKEY_A) : 0;                      // battle dialog/animation: tap = advance
		break;
	// --- PHASE 23 (lane B): the TAP-ADVANCE class. These two contexts were promoted out of the
	// GCTX_OVERWORLD leak in phase 22.0 and have injected NOTHING ever since — every dialog,
	// cutscene, PSA, Hall of Fame, credits, TV, evolution and egg-hatch screen in the census sits
	// here. FAM-DLG is what makes them usable: tap = A, hold = B, drag = D-pad (TITLE gets tap=A
	// only — see dlg_update's allowB note).
	case GCTX_FULLUI:
		battle_reset(); walk_reset(); party_reset(); target_reset(); fmenu_reset(); list_reset(); naming_reset(); storage_reset(); map_reset(); nav_reset();
		ret = dlg_update(sm, touching, newPress, gvalid, gx, gy, 1);
		break;
	// --- PHASE 25 (lane C1): the INERT class. The `default` arm below would already return 0, but
	// this is written out because "nothing" is the FEATURE here, not a fall-through: the credits
	// (TOUCH-PLAN L2) must not take the FAM-DLG hold verb, since pokeemerald credits.c:349 reads a
	// held B as the credits FAST-FORWARD, and FRLG quest-log playback (K4) must not take anything
	// at all. all_reset() so a half-finished gesture from the screen before cannot leak in.
	case GCTX_INERT:
		all_reset();
		ret = 0;
		break;
	case GCTX_TITLE:
		battle_reset(); walk_reset(); party_reset(); target_reset(); fmenu_reset(); list_reset(); naming_reset(); storage_reset(); map_reset(); nav_reset();
		ret = dlg_update(sm, touching, newPress, gvalid, gx, gy, 0);
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
		// PHASE 22.2 / SPEC-family-traversal T4.2 — the ROUTE CHIP. A traversal route MOVES the
		// player and uses one of their HMs, so it must never be silent: the verb the user is about
		// to watch ("SURF >", "CUT >") appears at PLAN time, i.e. before the walk to the shore even
		// starts, and the terminal states hang around for ~1 s so a route that gave up says so.
		// Drawn one chip-height ABOVE the mode chip so the two never collide.
		// PHASE 23: an EXCURSION uses the same chip slot and the same rule — it moves the player
		// across MAPS, which is even less acceptable to do silently — so it takes precedence while
		// one is running ("VIA DOOR - LEG 1/3", counting up per leg).
		if (s_excS.on && s_excChip) {
			touch_chip(buf, s_excChip, 160.0f, (float)UIHIT_TOUCH_CHIP_Y - TCHIP_H - 3.0f,
			           C2D_Color32(0x7A, 0xE0, 0xFF, 0xE6));
		} else if (s_seq.on || s_progChipHold > 0) {
			if (s_progChipHold > 0) s_progChipHold--;
			if (s_progChip)
				touch_chip(buf, s_progChip, 160.0f, (float)UIHIT_TOUCH_CHIP_Y - TCHIP_H - 3.0f,
				           s_seq.on ? C2D_Color32(0x7A, 0xE0, 0xFF, 0xE6)      // running: cyan
				                    : C2D_Color32(0xF5, 0xD0, 0x42, 0xE6));   // DONE / STOPPED
		}
	}
}
