// test_uihit.c — PC host unit test for the phase-17 hit-test + gesture core (source/uihit.{c,h}).
// Covers SPEC-input I4.1: the D2 regression (a tap at (0,0) must resolve to NOTHING and a tap on
// the right half of the mode pill must be segment 1), draw==hit as a golden table of the manifest
// numbers, four-corner coverage for every picker target in both modes, the dead gutter, the
// single-mode absence of slot B, the gesture machine's tap-vs-drag/one-way-latch/reset rules, and
// the scroll clamp + highlight-follow arithmetic.
// Phase-17 slice F3 adds §I2.4 / I4.1.10: the pause+settings CONTENT scroll (derived content
// height, the "a tab that fits never scrolls" rule, the 1:1 clamp, d-pad focus follow), the
// scrolled hit test (including "the status-hint band is never a control"), and the honest
// scrollbar (REPORT D18: the baked thumb sits at the BOTTOM at scroll-top). 1489 checks.
// Pure-C dual-compile per CLAUDE.md rule #4 (uihit.c is header-free by design).
//
//   clang -std=c11 -Wall -Wextra -O2 -I source test/host/test_uihit.c source/uihit.c -o /tmp/tuh && /tmp/tuh

#include <stdio.h>
#include <string.h>
#include "../../source/uihit.h"

static int g_checks = 0, g_fail = 0;
#define CHECK(cond, ...) do { \
	g_checks++; \
	if (!(cond)) { g_fail++; printf("  [FAIL] "); printf(__VA_ARGS__); printf("   (at %s:%d)\n", __FILE__, __LINE__); } \
} while (0)

static const char* TNAME[PICK_COUNT] = { "MODE", "SLOT_A", "SLOT_B", "START", "LINKED", "SETTINGS" };

// ---- T1: the golden rect table (I4.1.1) --------------------------------------------------------
// These literals are the manifest `dynamic` rects + the plate-measured card bodies, retyped here on
// purpose: uihit.c and this file must agree, and the drawing code reads uihit.c, so a coordinate
// edit on either side fails HERE, on the PC, in 50 ms.
static void t1_golden(void) {
	printf("T1  golden rect table (draw == hit)\n");
	const UiRect goldDual[PICK_COUNT] = {
		{  12,  12, 296, 30 }, {  12,  51, 296, 52 }, {  12, 111, 296, 52 },
		{  12, 174, 124, 37 }, { 144, 174, 164, 37 }, {   6, 223,  88, 16 },
	};
	const UiRect goldSingle[PICK_COUNT] = {
		{  12,  12, 296, 30 }, {  12,  51, 296, 59 }, {   0,   0,   0,  0 },
		{  12, 175, 130, 37 }, { 150, 175, 158, 37 }, {   6, 223,  88, 16 },
	};
	int n = 0;
	const UiRect* d = uihit_pick_rects(0, &n);
	CHECK(n == PICK_COUNT, "dual table has %d entries, want %d", n, PICK_COUNT);
	const UiRect* s = uihit_pick_rects(1, &n);
	CHECK(n == PICK_COUNT, "single table has %d entries, want %d", n, PICK_COUNT);
	for (int i = 0; i < PICK_COUNT; i++) {
		CHECK(d[i].x == goldDual[i].x && d[i].y == goldDual[i].y &&
		      d[i].w == goldDual[i].w && d[i].h == goldDual[i].h,
		      "dual %s = %d,%d,%d,%d want %d,%d,%d,%d", TNAME[i], d[i].x, d[i].y, d[i].w, d[i].h,
		      goldDual[i].x, goldDual[i].y, goldDual[i].w, goldDual[i].h);
		CHECK(s[i].x == goldSingle[i].x && s[i].y == goldSingle[i].y &&
		      s[i].w == goldSingle[i].w && s[i].h == goldSingle[i].h,
		      "single %s = %d,%d,%d,%d want %d,%d,%d,%d", TNAME[i], s[i].x, s[i].y, s[i].w, s[i].h,
		      goldSingle[i].x, goldSingle[i].y, goldSingle[i].w, goldSingle[i].h);
		// uihit_pick_rect must agree with the table it indexes
		UiRect a = uihit_pick_rect(0, i), b = uihit_pick_rect(1, i);
		CHECK(!memcmp(&a, &d[i], sizeof a), "pick_rect(dual,%s) != table", TNAME[i]);
		CHECK(!memcmp(&b, &s[i], sizeof b), "pick_rect(single,%s) != table", TNAME[i]);
	}
	// No two live targets may overlap — otherwise "first match wins" hides one of them.
	for (int mode = 0; mode < 2; mode++) {
		const UiRect* t = uihit_pick_rects(mode, &n);
		for (int i = 0; i < n; i++) for (int j = i + 1; j < n; j++) {
			if (t[i].w == 0 || t[j].w == 0) continue;
			int ox = (t[i].x < t[j].x + t[j].w) && (t[j].x < t[i].x + t[i].w);
			int oy = (t[i].y < t[j].y + t[j].h) && (t[j].y < t[i].y + t[i].h);
			CHECK(!(ox && oy), "%s mode: %s overlaps %s", mode ? "single" : "dual", TNAME[i], TNAME[j]);
		}
	}
}

// ---- T2: the D2 regression (I4.1.2) ------------------------------------------------------------
static void t2_d2(void) {
	printf("T2  REPORT D2 — right half selects 2 Games, (0,0) selects nothing\n");
	CHECK(uihit_pick(0, 240, 20) == PICK_MODE, "dual (240,20) is not PICK_MODE");
	CHECK(uihit_pick_mode_seg(0, 240, 20) == 1, "dual (240,20) must be segment 1 (2 Games)");
	CHECK(uihit_pick(0, 80, 20) == PICK_MODE, "dual (80,20) is not PICK_MODE");
	CHECK(uihit_pick_mode_seg(0, 80, 20) == 0, "dual (80,20) must be segment 0 (1 Game)");
	CHECK(uihit_pick_mode_seg(0, 159, 20) == 0, "boundary: 159 must be segment 0");
	CHECK(uihit_pick_mode_seg(0, 160, 20) == 1, "boundary: 160 must be segment 1");
	CHECK(uihit_pick_mode_seg(1, 240, 20) == 1, "single (240,20) must be segment 1");
	CHECK(uihit_pick_mode_seg(1, 80, 20) == 0, "single (80,20) must be segment 0");
	// The anti-test: the coordinate the release-edge bug manufactured must resolve to NOTHING, so
	// even a regressed edge handler could not silently flip the mode.
	CHECK(uihit_pick(0, 0, 0) == PICK_NONE, "dual (0,0) must be PICK_NONE");
	CHECK(uihit_pick(1, 0, 0) == PICK_NONE, "single (0,0) must be PICK_NONE");
	CHECK(uihit_pick_mode_seg(0, 0, 0) == -1, "(0,0) must not resolve to a mode segment");
	// The pill's own outside edges
	CHECK(uihit_pick(0, 11, 20) == PICK_NONE, "x=11 is outside the pill");
	CHECK(uihit_pick(0, 308, 20) == PICK_NONE, "x=308 is outside the pill");
	CHECK(uihit_pick(0, 160, 11) == PICK_NONE, "y=11 is above the pill");
	CHECK(uihit_pick(0, 160, 42) == PICK_NONE, "y=42 is below the pill");
}

// ---- T3: every target, four corners in and four just out (I4.1.3) ------------------------------
static void t3_corners(void) {
	printf("T3  four-corner coverage, both modes\n");
	for (int mode = 0; mode < 2; mode++) {
		int n = 0;
		const UiRect* t = uihit_pick_rects(mode, &n);
		for (int i = 0; i < n; i++) {
			UiRect r = t[i];
			if (r.w == 0) continue;
			const int inx[4] = { r.x, r.x + r.w - 1, r.x, r.x + r.w - 1 };
			const int iny[4] = { r.y, r.y, r.y + r.h - 1, r.y + r.h - 1 };
			for (int c = 0; c < 4; c++)
				CHECK(uihit_pick(mode, inx[c], iny[c]) == i,
				      "%s %s: inside corner (%d,%d) resolved to %d",
				      mode ? "single" : "dual", TNAME[i], inx[c], iny[c], uihit_pick(mode, inx[c], iny[c]));
			const int ox[4] = { r.x - 1, r.x + r.w, r.x, r.x };
			const int oy[4] = { r.y, r.y, r.y - 1, r.y + r.h };
			for (int c = 0; c < 4; c++)
				CHECK(uihit_pick(mode, ox[c], oy[c]) != i,
				      "%s %s: just-outside (%d,%d) still hit it", mode ? "single" : "dual", TNAME[i], ox[c], oy[c]);
		}
	}
	// The dead gutter between START and START—LINKED (I1.5.7): a miss must do NOTHING, not pick
	// the scarier button.
	for (int x = 136; x <= 143; x++)
		CHECK(uihit_pick(0, x, 190) == PICK_NONE, "dual gutter x=%d is not dead", x);
	for (int x = 142; x <= 149; x++)
		CHECK(uihit_pick(1, x, 190) == PICK_NONE, "single gutter x=%d is not dead", x);
	// Single mode has NO slot B: the whole dual-B band must be dead there (that band is the
	// non-interactive info panel).
	CHECK(uihit_pick(1, 160, 135) == PICK_NONE, "single mode: the info panel must not be tappable");
	CHECK(uihit_pick(0, 160, 135) == PICK_SLOT_B, "dual mode: (160,135) must be slot B");
	// The baked footer band right of the settings chip is dead (I1.5.8).
	CHECK(uihit_pick(0, 200, 230) == PICK_NONE, "footer hint must not be tappable");
	CHECK(uihit_pick(0, 40, 230) == PICK_SETTINGS, "settings chip centre must hit");
	CHECK(uihit_pick(1, 40, 230) == PICK_SETTINGS, "settings chip centre must hit (single)");
	// SPEC-layout L2: the recomposed footer. The lifted hint band occupies rows 212..221 and must
	// stay DEAD (it is art, not a control); the chip owns 223..238 and nothing above it.
	for (int y = 212; y <= 221; y++)
		CHECK(uihit_pick(0, 40, y) == PICK_NONE, "the lifted hint row y=%d must not be tappable", y);
	CHECK(uihit_pick(0, 40, 222) == PICK_NONE, "the 1 px gap above the chip is dead");
	CHECK(uihit_pick(0, 40, 223) == PICK_SETTINGS, "the chip's first row hits");
	CHECK(uihit_pick(0, 40, 238) == PICK_SETTINGS, "the chip's last row hits");
	CHECK(uihit_pick(0, 40, 239) == PICK_NONE, "one row below the chip is dead");
}

// ---- T4: the gesture machine (I4.1.4) ----------------------------------------------------------
static void t4_gesture(void) {
	printf("T4  gesture — tap vs drag, one-way latch, reset\n");
	UiGesture g; memset(&g, 0, sizeof g);
	CHECK(uihit_gesture_step(&g, 1, 1, 0, 100, 100) == GEST_DOWN, "press must emit GEST_DOWN");
	CHECK(uihit_gesture_step(&g, 0, 1, 0, 100, 105) == GEST_NONE, "5 px is under the threshold");
	CHECK(uihit_gesture_step(&g, 0, 0, 1, 0, 0) == GEST_TAP, "release under threshold must tap");
	CHECK(g.x == 100 && g.y == 105, "tap point must be the LAST VALID sample, got %d,%d", g.x, g.y);
	CHECK(g.active == 0 && g.dragged == 0, "gesture must fully reset on release");

	memset(&g, 0, sizeof g);
	uihit_gesture_step(&g, 1, 1, 0, 100, 100);
	CHECK(uihit_gesture_step(&g, 0, 1, 0, 100, 106) == GEST_NONE, "exactly 6 px is NOT a drag");
	CHECK(uihit_gesture_step(&g, 0, 1, 0, 100, 107) == GEST_DRAG, "7 px must be a drag");
	CHECK(uihit_gesture_step(&g, 0, 1, 0, 100, 100) == GEST_DRAG, "one-way latch: back at origin is still a drag");
	CHECK(uihit_gesture_step(&g, 0, 0, 1, 0, 0) == GEST_NONE, "a drag must NOT emit a tap");
	CHECK(g.dragged == 0 && g.active == 0, "reset after a drag");

	// Horizontal-only: the py-only-threshold regression (a swipe across a slot card was a "tap").
	memset(&g, 0, sizeof g);
	uihit_gesture_step(&g, 1, 1, 0, 100, 100);
	CHECK(uihit_gesture_step(&g, 0, 1, 0, 110, 100) == GEST_DRAG, "horizontal 10 px must be a drag");
	CHECK(uihit_gesture_step(&g, 0, 0, 1, 0, 0) == GEST_NONE, "horizontal drag must not tap");

	// A release with no press at all is inert (the app's first frame).
	memset(&g, 0, sizeof g);
	CHECK(uihit_gesture_step(&g, 0, 0, 1, 0, 0) == GEST_NONE, "release without press must be inert");
	CHECK(uihit_gesture_step(&g, 0, 1, 0, 50, 50) == GEST_NONE, "held without press must be inert");
	CHECK(g.x == 0 && g.y == 0, "held without press must not latch a point");

	// The full D2 scenario end to end: press on the RIGHT half, release. The handler must see the
	// right half — never (0,0).
	memset(&g, 0, sizeof g);
	uihit_gesture_step(&g, 1, 1, 0, 240, 20);
	UiGestEv ev = uihit_gesture_step(&g, 0, 0, 1, 0, 0);
	CHECK(ev == GEST_TAP, "a one-frame tap must still be a tap");
	CHECK(uihit_pick_mode_seg(0, g.x, g.y) == 1, "the D2 tap must resolve to segment 1 (2 Games)");
}

// ---- T5: scroll clamp (I4.1.5) ------------------------------------------------------------------
static void t5_scroll(void) {
	printf("T5  scroll clamp + rate\n");
	const int VIS = 8, N = 11;                       // the harness stages 11 dummy ROMs
	CHECK(uihit_scroll_max(N, VIS) == 3, "maxTop must be 3");
	CHECK(uihit_scroll_max(4, VIS) == 0, "a short list cannot scroll");
	CHECK(uihit_scroll_max(0, VIS) == 0, "an empty list cannot scroll");
	CHECK(uihit_scroll_clamp(0, 1000, UIHIT_PICK_ROW_PX, N, VIS) == 3, "over-scroll down clamps at max");
	CHECK(uihit_scroll_clamp(3, -1000, UIHIT_PICK_ROW_PX, N, VIS) == 0, "over-scroll up clamps at 0");
	CHECK(uihit_scroll_clamp(0, 24, UIHIT_PICK_ROW_PX, N, VIS) == 1, "24 px = exactly one row");
	CHECK(uihit_scroll_clamp(0, 23, UIHIT_PICK_ROW_PX, N, VIS) == 0, "23 px = zero rows");
	CHECK(uihit_scroll_clamp(0, 47, UIHIT_PICK_ROW_PX, N, VIS) == 1, "47 px = one row");
	CHECK(uihit_scroll_clamp(0, 48, UIHIT_PICK_ROW_PX, N, VIS) == 2, "48 px = two rows");
	CHECK(uihit_scroll_clamp(0, 72, UIHIT_PICK_ROW_PX, N, VIS) == 3, "72 px = three rows (T2's number)");
	CHECK(uihit_scroll_clamp(3, -24, UIHIT_PICK_ROW_PX, N, VIS) == 2, "upward drag is symmetric");
	CHECK(uihit_scroll_clamp(3, -23, UIHIT_PICK_ROW_PX, N, VIS) == 3, "upward truncation is symmetric");
	CHECK(uihit_scroll_clamp(0, 1000, UIHIT_PICK_ROW_PX, 4, VIS) == 0, "a short list never scrolls");
	CHECK(uihit_scroll_clamp(0, -1000, UIHIT_PICK_ROW_PX, 4, VIS) == 0, "…in either direction");
	for (int d = -400; d <= 400; d += 7) {
		int t = uihit_scroll_clamp(1, d, UIHIT_PICK_ROW_PX, N, VIS);
		CHECK(t >= 0 && t <= 3, "topRow %d out of range for drag %d", t, d);
	}
}

// ---- T6: highlight follows the scroll and vice versa (I4.1.6) ----------------------------------
static void t6_follow(void) {
	printf("T6  highlight follow\n");
	const int VIS = 8, N = 11;
	CHECK(uihit_clamp_sel(0, 3, VIS, N) == 3, "after a drag to topRow 3, sel clamps up into view");
	CHECK(uihit_clamp_sel(10, 3, VIS, N) == 10, "sel 10 is visible at topRow 3");
	CHECK(uihit_clamp_sel(2, 3, VIS, N) == 3, "sel above the window is pulled to the first row");
	CHECK(uihit_clamp_sel(5, 0, VIS, N) == 5, "sel inside the window is untouched");
	CHECK(uihit_clamp_sel(9, 0, VIS, N) == 7, "sel below the window is pulled to the last row");
	CHECK(uihit_clamp_sel(0, 0, VIS, 0) == 0, "empty list is safe");
	CHECK(uihit_follow_sel(3, 0, VIS, N) == 0, "a d-pad move to row 0 pulls the scroll back to 0");
	CHECK(uihit_follow_sel(0, 10, VIS, N) == 3, "a d-pad move to the last row scrolls to maxTop");
	CHECK(uihit_follow_sel(0, 7, VIS, N) == 0, "the last visible row needs no scroll");
	CHECK(uihit_follow_sel(0, 8, VIS, N) == 1, "one past the window scrolls by one");
	CHECK(uihit_follow_sel(3, 3, VIS, N) == 3, "already visible: no change");
	for (int s = 0; s < N; s++) {
		int top = uihit_follow_sel(0, s, VIS, N);
		CHECK(s >= top && s < top + VIS, "sel %d not visible at topRow %d", s, top);
		CHECK(top >= 0 && top <= 3, "follow produced topRow %d", top);
	}
}

// ---- T8: pause/settings CONTENT scroll (I4.1.10, SPEC-input I2.4) --------------------------------
// The golden tables are the manifest `dynamic` rects for the two tabs that overflow, retyped here
// so a coordinate squeezed back up to "make it fit" (which is what produced REPORT D8/D9) fails on
// the PC. PT_DISPLAY/PT_TOUCH in main.c must equal these.
static const UiRect GOLD_DISPLAY[6] = {
	{  93,  26, 208, 30 }, {  93,  81, 208, 30 }, {  93, 136, 208, 30 }, {  93, 191, 208, 30 },
	{ 268, 234,  33, 18 },                    // toggle: swap screens   (manifest y=234, NOT 224)
	{ 268, 263,  33, 18 },                    // toggle: frameskip      (manifest y=263 — below the fold)
};
static const UiRect GOLD_TOUCH[5] = {
	{  93,  26, 208, 30 }, {  93, 109, 101, 44 }, { 201, 109, 101, 44 },
	{  93, 195, 208, 31 },                    // pad colour swatches    (manifest y=195 h=31)
	{  93, 253, 208, 30 },                    // seg: pad edges         (manifest y=253 — below the fold)
};
static const UiRect GOLD_SESSION[3] = { { 93, 10, 216, 40 }, { 93, 59, 216, 40 }, { 93, 108, 216, 43 } };

static void t8_content_scroll(void) {
	printf("T8  content height + max scroll + 1:1 clamp\n");
	CHECK(uihit_content_h(GOLD_DISPLAY, 6) == 281, "DISPLAY content is 263+18 = 281 tall");
	CHECK(uihit_content_h(GOLD_TOUCH,   5) == 283, "TOUCH content is 253+30 = 283 tall");
	CHECK(uihit_content_h(GOLD_SESSION, 3) == 240, "a tab that fits is floored at the screen height");
	CHECK(uihit_content_h(0, 3) == 240, "a null table is safe");
	{ UiRect absent[2] = { { 93, 10, 216, 40 }, { 0, 0, 0, 0 } };
	  CHECK(uihit_content_h(absent, 2) == 240, "an empty rect contributes no height"); }

	// PHASE 19 / SPEC-legible L3.2.2: the viewport shrank 228 -> 226 to give the grown hint-band
	// face its two rows back, so every number below is written against UIHIT_MENU_VIEW_H rather
	// than the constant it happened to have. The arithmetic is the assertion; the literal was not.
	const int mD = uihit_max_scroll(281), mT = uihit_max_scroll(283), mS = uihit_max_scroll(240);
	CHECK(mD == 281 - UIHIT_MENU_VIEW_H, "DISPLAY maxScroll = 281-%d, got %d", UIHIT_MENU_VIEW_H, mD);
	CHECK(mT == 283 - UIHIT_MENU_VIEW_H, "TOUCH maxScroll = 283-%d, got %d", UIHIT_MENU_VIEW_H, mT);
	CHECK(mS == 0,  "a tab that FITS must not scroll at all, got %d", mS);
	CHECK(uihit_max_scroll(0) == 0 && uihit_max_scroll(-5) == 0, "degenerate heights are inert");
	// the point of the number: at max scroll the below-fold row clears the hint line at y=231
	CHECK(GOLD_DISPLAY[5].y - mD + GOLD_DISPLAY[5].h <= UIHIT_MENU_VIEW_H,
	      "the frameskip toggle must end above the hint band at max scroll");
	CHECK(GOLD_TOUCH[4].y - mT + GOLD_TOUCH[4].h <= UIHIT_MENU_VIEW_H,
	      "the pad-edges row must end above the hint band at max scroll");
	// …and it is still fully on-screen (the D8 "clipped by the screen edge" half)
	CHECK(GOLD_DISPLAY[5].y - mD >= 0 && GOLD_TOUCH[4].y - mT >= 0, "no row scrolls off the top");

	CHECK(uihit_scroll_px(0, 40, mD) == 40, "1:1 — 40 px of finger is 40 px of content");
	CHECK(uihit_scroll_px(0, 1000, mD) == mD, "over-scroll down clamps at maxScroll");
	CHECK(uihit_scroll_px(mD, -1000, mD) == 0, "over-scroll up clamps at 0");
	CHECK(uihit_scroll_px(20, -20, mD) == 0, "a drag back to the origin returns to the top");
	CHECK(uihit_scroll_px(0, 100, 0) == 0, "a non-scrolling tab ignores any drag");
	CHECK(uihit_scroll_px(0, -100, 0) == 0, "…in either direction");
	for (int d = -400; d <= 400; d += 3) {
		int s = uihit_scroll_px(20, d, mD);
		CHECK(s >= 0 && s <= mD, "scroll %d out of [0,%d] for drag %d", s, mD, d);
	}

	printf("T8b d-pad focus follow (I2.4.5)\n");
	CHECK(uihit_follow_rect(0, GOLD_DISPLAY[5], mD) == mD, "focusing the below-fold toggle scrolls to it");
	CHECK(uihit_follow_rect(0, GOLD_DISPLAY[4], mD) == 234 + 18 - UIHIT_MENU_VIEW_H,
	      "the swap toggle needs 234+18-%d", UIHIT_MENU_VIEW_H);
	CHECK(uihit_follow_rect(mD, GOLD_DISPLAY[0], mD) == 0, "focusing the first row scrolls back to the top");
	CHECK(uihit_follow_rect(mD, GOLD_DISPLAY[1], mD) == mD, "…a row already in view at max scroll does not move");
	CHECK(uihit_follow_rect(80, GOLD_DISPLAY[1], 200) == 81 - UIHIT_MENU_LEAD, "…a row above the viewport pulls its caption band in too");
	CHECK(uihit_follow_rect(30, GOLD_DISPLAY[2], mD) == 30, "an already-visible row does not move");
	CHECK(uihit_follow_rect(0, GOLD_TOUCH[4], mT) == mT, "TOUCH's pad-edges row scrolls to max");
	{ UiRect absent = { 0, 0, 0, 0 };
	  CHECK(uihit_follow_rect(17, absent, mD) == 17, "an absent control never moves the scroll"); }
	for (int i = 0; i < 6; i++) {
		int s = uihit_follow_rect(0, GOLD_DISPLAY[i], mD);
		CHECK(GOLD_DISPLAY[i].y - s >= 0 && GOLD_DISPLAY[i].y - s + GOLD_DISPLAY[i].h <= UIHIT_MENU_VIEW_H,
		      "row %d is not fully in the viewport at scroll %d", i, s);
	}
}

// ---- T9: hit-testing UNDER scroll (I4.1.10) ------------------------------------------------------
static void t9_scrolled_hit(void) {
	printf("T9  scrolled hit test\n");
	const UiRect fs = GOLD_DISPLAY[5];                  // y=263 h=18 — unreachable at scroll 0
	CHECK(uihit_in_scrolled(fs, 0, 284, 263) == 0, "at scroll 0 the below-fold toggle is unreachable");
	CHECK(uihit_in_scrolled(fs, 0, 284, 227) == 0, "…and nothing else answers for it");
	CHECK(uihit_in_scrolled(fs, 53, 284, 210) == 1, "at scroll 53 it is hit at y = 263-53 = 210");
	CHECK(uihit_in_scrolled(fs, 53, 284, 263) == 0, "…and NOT at its unscrolled y");
	CHECK(uihit_in_scrolled(fs, 40, 284, 223) == 1, "the hit box tracks every intermediate offset");
	// the spec's own example: menuScroll = 40 => a control at y=263 is hit at py=223
	CHECK(uihit_in_scrolled(fs, 40, 284, 263) == 0, "…and only there");
	// the hint band is chrome (I2.4.3): nothing in it may resolve to a control
	for (int s = 0; s <= 55; s++)
		for (int py = UIHIT_MENU_VIEW_H; py < 240; py++)
			CHECK(uihit_index_scrolled(GOLD_DISPLAY, 6, s, 284, py) == -1,
			      "py %d at scroll %d must never hit a control", py, s);
	// a straddling row is hittable only in its VISIBLE part
	CHECK(uihit_in_scrolled(GOLD_DISPLAY[4], 20, 284, 214) == 1, "swap toggle visible part at scroll 20");
	CHECK(uihit_in_scrolled(GOLD_DISPLAY[4], 10, 284, 230) == 0, "the part under the hint band is dead");
	// a row scrolled off the TOP is gone
	CHECK(uihit_in_scrolled(GOLD_DISPLAY[0], 53, 150, 30) == 0, "row 0 is above the viewport at max scroll");
	CHECK(uihit_index_scrolled(GOLD_DISPLAY, 6, 53, 150, 30) == 1, "…and row 1 (81-53=28..57) answers there");
	CHECK(uihit_index_scrolled(0, 6, 0, 1, 1) == -1, "a null table is safe");
	// every control, at the scroll offset that reveals it, resolves to ITSELF and nothing else
	for (int i = 0; i < 6; i++) {
		int s = uihit_follow_rect(0, GOLD_DISPLAY[i], uihit_max_scroll(281));
		int cx = GOLD_DISPLAY[i].x + GOLD_DISPLAY[i].w / 2;
		int cy = GOLD_DISPLAY[i].y - s + GOLD_DISPLAY[i].h / 2;
		CHECK(uihit_index_scrolled(GOLD_DISPLAY, 6, s, cx, cy) == i,
		      "control %d does not answer at its own centre (scroll %d, py %d)", i, s, cy);
	}
	for (int i = 0; i < 5; i++) {
		int s = uihit_follow_rect(0, GOLD_TOUCH[i], uihit_max_scroll(283));
		int cx = GOLD_TOUCH[i].x + GOLD_TOUCH[i].w / 2;
		int cy = GOLD_TOUCH[i].y - s + GOLD_TOUCH[i].h / 2;
		CHECK(uihit_index_scrolled(GOLD_TOUCH, 5, s, cx, cy) == i,
		      "TOUCH control %d does not answer at its own centre", i);
	}
}

// ---- T10: the honest scrollbar (I4.1 / REPORT D18) -----------------------------------------------
static void t10_scrollbar(void) {
	printf("T10 scrollbar thumb\n");
	const int TY = 6, TH = 216;                          // the track this slice draws (y6..222)
	int thD = uihit_thumb_h(TH, UIHIT_MENU_VIEW_H, 281);
	CHECK(thD == UIHIT_MENU_VIEW_H * TH / 281, "DISPLAY thumb is the visible fraction of the content");
	CHECK(thD > UIHIT_THUMB_MIN && thD < TH, "…and it is a real object: %d px of %d", thD, TH);
	CHECK(uihit_thumb_h(TH, UIHIT_MENU_VIEW_H, UIHIT_MENU_VIEW_H) == TH, "content == viewport gets a full-length thumb");
	CHECK(uihit_thumb_h(TH, UIHIT_MENU_VIEW_H, 200) == TH, "content SHORTER than the viewport too");
	CHECK(uihit_thumb_h(TH, UIHIT_MENU_VIEW_H, 4000) == UIHIT_THUMB_MIN, "a huge content floor-clamps the thumb");
	CHECK(uihit_thumb_h(0, UIHIT_MENU_VIEW_H, 281) == 0 && uihit_thumb_h(-3, UIHIT_MENU_VIEW_H, 281) == 0,
	      "a degenerate track is 0");

	const int mD = uihit_max_scroll(281);
	CHECK(uihit_thumb_y(TY, TH, thD, 0,  mD) == TY, "AT SCROLL TOP THE THUMB IS AT THE TOP (D18)");
	CHECK(uihit_thumb_y(TY, TH, thD, mD, mD) == TY + TH - thD, "at max scroll it is flush with the bottom");
	{ int mid = uihit_thumb_y(TY, TH, thD, mD / 2, mD);
	  CHECK(mid > TY && mid < TY + TH - thD, "mid-scroll lands strictly between the ends"); }
	CHECK(uihit_thumb_y(TY, TH, thD, -50, mD) == TY, "a negative offset cannot push the thumb off");
	CHECK(uihit_thumb_y(TY, TH, thD, 999, mD) == TY + TH - thD, "…nor can an over-scroll");
	CHECK(uihit_thumb_y(TY, TH, TH, 30, mD) == TY, "a full-length thumb never moves");
	CHECK(uihit_thumb_y(TY, TH, thD, 30, 0) == TY, "a non-scrolling tab pins the thumb at the top");
	// monotone and always inside the track
	int prev = -1;
	for (int s = 0; s <= mD; s++) {
		int y = uihit_thumb_y(TY, TH, thD, s, mD);
		CHECK(y >= prev, "thumb went backwards at scroll %d", s);
		CHECK(y >= TY && y + thD <= TY + TH, "thumb left the track at scroll %d", s);
		prev = y;
	}
	// the ROM list reuses the same pair with row units (visRows / nRows)
	const int LY = 41, LH = 188, VIS = 8, N = 13;
	int thL = uihit_thumb_h(LH, VIS, N);
	CHECK(thL == VIS * LH / N, "the list thumb is visRows/nRows of the track");
	CHECK(uihit_thumb_y(LY, LH, thL, 0, uihit_scroll_max(N, VIS)) == LY, "topRow 0 => thumb at the top");
	CHECK(uihit_thumb_y(LY, LH, thL, 5, uihit_scroll_max(N, VIS)) == LY + LH - thL,
	      "topRow 5 (= maxTop for 13 rows) => thumb at the bottom");
	CHECK(uihit_thumb_h(LH, VIS, 8) == LH, "a list that fits gets a full-length thumb");
}

// ---- T7: uihit_in / uihit_seg / uihit_index primitives ------------------------------------------
static void t7_prims(void) {
	printf("T7  primitives\n");
	UiRect r = { 10, 20, 30, 40 };
	CHECK(uihit_in(r, 10, 20) == 1, "top-left is inside (half-open)");
	CHECK(uihit_in(r, 39, 59) == 1, "bottom-right-1 is inside");
	CHECK(uihit_in(r, 40, 59) == 0, "x == x+w is OUTSIDE");
	CHECK(uihit_in(r, 39, 60) == 0, "y == y+h is OUTSIDE");
	CHECK(uihit_in(r, 9, 20) == 0, "x-1 is outside");
	CHECK(uihit_in(r, 10, 19) == 0, "y-1 is outside");
	UiRect empty = { 0, 0, 0, 0 };
	CHECK(uihit_in(empty, 0, 0) == 0, "an empty rect is never hit");
	CHECK(uihit_seg(r, 3, 10) == 0 && uihit_seg(r, 3, 19) == 0, "seg 0 spans 10 px");
	CHECK(uihit_seg(r, 3, 20) == 1 && uihit_seg(r, 3, 29) == 1, "seg 1");
	CHECK(uihit_seg(r, 3, 30) == 2 && uihit_seg(r, 3, 39) == 2, "seg 2 keeps the remainder");
	CHECK(uihit_seg(r, 3, 9) == -1, "outside left is -1, NOT clamped to 0");
	CHECK(uihit_seg(r, 3, 40) == -1, "outside right is -1");
	CHECK(uihit_seg(r, 0, 15) == -1, "nseg 0 is -1");
	UiRect tbl[3] = { { 0, 0, 10, 10 }, { 0, 0, 0, 0 }, { 20, 0, 10, 10 } };
	CHECK(uihit_index(tbl, 3, 5, 5) == 0, "first rect");
	CHECK(uihit_index(tbl, 3, 25, 5) == 2, "third rect (empty one skipped)");
	CHECK(uihit_index(tbl, 3, 15, 5) == -1, "between rects is -1");
	CHECK(uihit_index(0, 3, 5, 5) == -1, "null table is safe");
}


// ---- T11: uihit_wrap — the TOUCH-tab explainer paragraph (SPEC-layout L5.2) ---------------------
// A fake proportional measure: `W_PER` px per BYTE, with UTF-8 continuation bytes free, so a "—"
// costs the same as one glyph. Monotone in length, which is all the wrapper assumes about a font.
#define WRAP_LINES 4
#define WRAP_CAP   72
static int wm_cost(const char* s, void* ctx) {
	int per = *(const int*)ctx, n = 0;
	for (const unsigned char* p = (const unsigned char*)s; *p; p++)
		if ((*p & 0xC0) != 0x80) n++;
	return n * per;
}
static int wrap_glyphs(const char* s) { int per = 1; return wm_cost(s, &per); }

// The three shipped strings, verbatim from prototypes/3DGBA Prototype.dc.html:721-723.
static const char* const EXPLAIN[3] = {
	"Off \xE2\x80\x94 a touch opens the pause menu. No game input from the touch screen.",
	"Gamepad \xE2\x80\x94 a translucent virtual controller (D-pad, A/B, L/R, START) over game B.",
	("Smart \xE2\x80\x94 the touch screen is a pointer on the real Gen-3 UI: tap-to-walk, tap "
	 "menus/party/targets, double-tap = START."),
};

static void t11_wrap(void) {
	printf("T11 uihit_wrap — explainer layout, no overflow, no split UTF-8\n");
	char L[WRAP_LINES * WRAP_CAP];
	int per = 5;                       // 5 px per glyph -> ~41 glyphs in the manifest's 208 px

	// L5.2's rect: 208 px wide, at most 4 lines. Every copy must fit without overflowing EITHER.
	for (int m = 0; m < 3; m++) {
		memset(L, 0, sizeof L);
		int n = uihit_wrap(EXPLAIN[m], 208, WRAP_LINES, L, WRAP_CAP, wm_cost, &per);
		CHECK(n >= 1 && n <= WRAP_LINES, "mode %d: %d lines, want 1..%d", m, n, WRAP_LINES);
		int total = 0;
		for (int i = 0; i < n; i++) {
			const char* ln = L + i * WRAP_CAP;
			CHECK(wm_cost(ln, &per) <= 208, "mode %d line %d overflows: '%s' (%d px)",
			      m, i, ln, wm_cost(ln, &per));
			CHECK(ln[0] != ' ', "mode %d line %d starts with a space: '%s'", m, i, ln);
			CHECK(ln[0] != '\0', "mode %d line %d is empty", m, i);
			total += wrap_glyphs(ln);
			// no line may end mid-UTF-8-sequence
			int L2 = (int)strlen(ln);
			CHECK(L2 == 0 || ((unsigned char)ln[L2 - 1] & 0xC0) != 0xC0,
			      "mode %d line %d ends on a UTF-8 LEAD byte: '%s'", m, i, ln);
		}
		// Nothing is silently dropped: the words that came out are the words that went in, unless
		// the wrapper said so with an ellipsis.
		int src = wrap_glyphs(EXPLAIN[m]);
		int last = (int)strlen(L + (n - 1) * WRAP_CAP);
		int ell = (last >= 3 && memcmp(L + (n - 1) * WRAP_CAP + last - 3, "...", 3) == 0);
		if (!ell) CHECK(total + (n - 1) >= src - 2 && total <= src,
		                "mode %d: %d glyphs out of %d in", m, total, src);
	}

	// The Smart copy is the long one: at 5 px/glyph it is 117 glyphs = 585 px = 3 lines of 208.
	memset(L, 0, sizeof L);
	int n = uihit_wrap(EXPLAIN[2], 208, WRAP_LINES, L, WRAP_CAP, wm_cost, &per);
	CHECK(n >= 3, "Smart copy must need at least 3 lines, got %d", n);
	CHECK(strncmp(L, "Smart", 5) == 0, "line 0 must start 'Smart', got '%s'", L);

	// Truncation: one line is not enough, so the wrapper must ellipsise rather than overflow.
	memset(L, 0, sizeof L);
	n = uihit_wrap(EXPLAIN[2], 208, 1, L, WRAP_CAP, wm_cost, &per);
	CHECK(n == 1, "maxLines 1 must emit exactly 1 line, got %d", n);
	CHECK(wm_cost(L, &per) <= 208, "the truncated line still fits (%d px)", wm_cost(L, &per));
	{ int l = (int)strlen(L);
	  CHECK(l >= 3 && memcmp(L + l - 3, "...", 3) == 0, "truncated line must end '...': '%s'", L); }

	// A word longer than the line is hard-broken on a UTF-8 boundary, never mid-sequence.
	memset(L, 0, sizeof L);
	n = uihit_wrap("\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9", 15, WRAP_LINES, L, WRAP_CAP, wm_cost, &per);
	CHECK(n >= 2, "a 6-glyph word at 15 px must break, got %d lines", n);
	for (int i = 0; i < n; i++) {
		const char* ln = L + i * WRAP_CAP;
		CHECK((int)strlen(ln) % 2 == 0, "line %d split a 2-byte sequence: len %d", i, (int)strlen(ln));
		CHECK(wm_cost(ln, &per) <= 15, "line %d overflows the hard break", i);
	}

	// Degenerate inputs are safe and say nothing.
	CHECK(uihit_wrap(0, 208, 4, L, WRAP_CAP, wm_cost, &per) == 0, "NULL text -> 0 lines");
	CHECK(uihit_wrap("hi", 208, 4, 0, WRAP_CAP, wm_cost, &per) == 0, "NULL out -> 0 lines");
	CHECK(uihit_wrap("hi", 208, 4, L, WRAP_CAP, 0, &per) == 0, "NULL measure -> 0 lines");
	CHECK(uihit_wrap("hi", 208, 0, L, WRAP_CAP, wm_cost, &per) == 0, "0 lines -> 0 lines");
	CHECK(uihit_wrap("hi", 0, 4, L, WRAP_CAP, wm_cost, &per) == 0, "0 width -> 0 lines");
	CHECK(uihit_wrap("   ", 208, 4, L, WRAP_CAP, wm_cost, &per) == 0, "all-space -> 0 lines");

	// Sweep every plausible font width for all three copies: at 4 lines nothing may EVER overflow,
	// and the line count must be monotone non-increasing as the glyphs get narrower.
	for (int m = 0; m < 3; m++) {
		int prev = 0;
		for (int w = 3; w <= 12; w++) {   // narrower glyphs can never need MORE lines
			memset(L, 0, sizeof L);
			int k = uihit_wrap(EXPLAIN[m], 208, WRAP_LINES, L, WRAP_CAP, wm_cost, &w);
			CHECK(k >= 1 && k <= WRAP_LINES, "mode %d @%dpx: %d lines", m, w, k);
			for (int i = 0; i < k; i++)
				CHECK(wm_cost(L + i * WRAP_CAP, &w) <= 208,
				      "mode %d @%dpx line %d overflows", m, w, i);
			CHECK(k >= prev, "mode %d: %d lines @%dpx < %d @%dpx (not monotone)", m, k, w, prev, w - 1);
			prev = k;
		}
	}

	// L5.3's acceptance in arithmetic: 4 lines at 10 px lead from y=66 end at 106, and the Preview
	// buttons start at y=109. The layout constant and the widget table must not drift apart.
	CHECK(66 + (WRAP_LINES - 1) * 10 + 9 <= 109, "the explainer must clear the Preview buttons");
}

int main(void) {
	printf("== test_uihit (phase-17 SPEC-input I4.1) ==\n");
	t1_golden();
	t2_d2();
	t3_corners();
	t4_gesture();
	t5_scroll();
	t6_follow();
	t7_prims();
	t8_content_scroll();
	t9_scrolled_hit();
	t10_scrollbar();
	t11_wrap();
	printf("\n%d checks, %d failures\n", g_checks, g_fail);
	return g_fail ? 1 : 0;
}
