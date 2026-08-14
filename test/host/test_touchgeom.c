// test_touchgeom.c — PC host unit test for source/touchgeom.c (phase 22.1: the KEYBOARD +
// LISTS touch-family geometry; SPEC-family-keyboard §2.1-2.2, SPEC-family-lists §1.1).
//
//   clang -std=c11 -Wall -Wextra -O2 -I source test/host/test_touchgeom.c source/touchgeom.c \
//         -o /tmp/ttg && /tmp/ttg
//
// The tables under test are the GAME'S OWN (pret naming_screen.c column tables, SetCursorPos
// math, ListMenu template fields), so most expectations below are restatements of engine facts
// with their source citations — the suite pins our transcription of them, exhaustively where
// cheap (every px of the keyboard area x page).
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "touchgeom.h"

static int g_checks = 0, g_fails = 0;
static void check(int ok, const char* fmt, ...) {
	g_checks++;
	if (!ok) {
		g_fails++;
		va_list ap; va_start(ap, fmt);
		printf("  [FAIL] "); vprintf(fmt, ap); printf("\n");
		va_end(ap);
	}
}
#define CHECK(c, ...) check((c) ? 1 : 0, __VA_ARGS__)

// The game's own tables, restated once for the oracle (naming_screen.c :301-310):
static const int LET_X[8] = { 0, 12, 24, 56, 68, 80, 92, 123 };   // + 38 = center
static const int SYM_X[6] = { 0, 22, 44, 66, 88, 110 };
static const int SYMROW[4] = { 5, 5, 6, 5 };

// TEST 1 — column counts by currentPage (page 0 = SYMBOLS -> 6; 1/2 = letters -> 8; else 0).
static void test_colcount(void) {
	puts("TEST 1: namegeom_colcount per page");
	CHECK(namegeom_colcount(0) == 6, "page 0 (SYMBOLS) = 6");
	CHECK(namegeom_colcount(1) == 8, "page 1 (UPPER) = 8");
	CHECK(namegeom_colcount(2) == 8, "page 2 (LOWER) = 8");
	CHECK(namegeom_colcount(3) == 0, "page 3 = invalid");
	CHECK(namegeom_colcount(-1) == 0, "page -1 = invalid");
}

// TEST 2 — cell validity (R2): letters rows 2-3 have no col 7; symbols rows are 5/5/6/5.
static void test_validity(void) {
	puts("TEST 2: namegeom_cell_valid (the R2 dead cells)");
	for (int page = 1; page <= 2; page++) {
		for (int r = 0; r < 4; r++)
			for (int c = 0; c < 8; c++) {
				int want = (r < 2) ? 1 : (c < 7);
				CHECK(namegeom_cell_valid(page, c, r) == want,
				      "letters p%d r%d c%d valid=%d", page, r, c, want);
			}
	}
	for (int r = 0; r < 4; r++)
		for (int c = 0; c < 6; c++)
			CHECK(namegeom_cell_valid(0, c, r) == (c < SYMROW[r]),
			      "symbols r%d c%d valid=%d", r, c, c < SYMROW[r]);
	CHECK(!namegeom_cell_valid(0, 6, 2), "symbols r2 c6 out of table");
	CHECK(!namegeom_cell_valid(1, 0, 4), "row 4 never valid");
	CHECK(!namegeom_cell_valid(3, 0, 0), "page 3 never valid");
}

// TEST 3 — EXHAUSTIVE hit sweep: every gx in 0..239 x every row band x all 3 pages must agree
// with a brute-force oracle (nearest center within tolerance + validity), and the button
// column must resolve to its 3 bands. This is the whole R1/R3 geometry, pinned px-by-px.
static void test_hit_sweep(void) {
	puts("TEST 3: exhaustive namegeom_hit sweep (3 pages x 240 x 160)");
	int bad = 0;
	for (int page = 0; page <= 2; page++) {
		const int* xs = (page == 0) ? SYM_X : LET_X;
		int n   = (page == 0) ? 6 : 8;
		int tol = (page == 0) ? 11 : 6;
		for (int gy = 0; gy < 160; gy++) {
			for (int gx = 0; gx < 240; gx++) {
				int col = -1, row = -1;
				int got = namegeom_hit(page, gx, gy, &col, &row);
				int want = NGH_NONE, wc = -1, wr = -1;
				if (gx >= 176 && gx < 232) {                     // button column (R3)
					if      (gy >= 76  && gy < 102) want = NGH_PAGE;
					else if (gy >= 102 && gy < 128) want = NGH_BACK;
					else if (gy >= 128 && gy < 154) want = NGH_OK;
				} else if (gy >= 80 && gy < 144) {
					int r = (gy - 80) / 16, best = -1, bd = tol + 1;
					for (int i = 0; i < n; i++) {
						int d = gx - (xs[i] + 38); if (d < 0) d = -d;
						if (d < bd) { bd = d; best = i; }
					}
					if (best >= 0 && namegeom_cell_valid(page, best, r)) {
						want = NGH_CHAR; wc = best; wr = r;
					}
				}
				if (got != want || (want == NGH_CHAR && (col != wc || row != wr))) {
					if (bad++ < 5)
						check(0, "hit p%d (%d,%d): got %d c%d r%d want %d c%d r%d",
						      page, gx, gy, got, col, row, want, wc, wr);
				} else {
					g_checks++;   // counted, silent
				}
			}
		}
	}
	CHECK(bad == 0, "sweep disagreements: %d", bad);
}

// TEST 4 — cursor px math == SetCursorPos (x = xPos+38, y = row*16+88).
static void test_cursor_px(void) {
	puts("TEST 4: namegeom_cursor_px == the game's SetCursorPos math");
	int x, y;
	for (int c = 0; c < 8; c++)
		for (int r = 0; r < 4; r++) {
			if (!namegeom_cell_valid(1, c, r)) continue;
			CHECK(namegeom_cursor_px(1, c, r, &x, &y) == 1 &&
			      x == LET_X[c] + 38 && y == r * 16 + 88,
			      "letters c%d r%d -> (%d,%d)", c, r, x, y);
		}
	for (int c = 0; c < 6; c++)
		for (int r = 0; r < 4; r++) {
			if (!namegeom_cell_valid(0, c, r)) continue;
			CHECK(namegeom_cursor_px(0, c, r, &x, &y) == 1 &&
			      x == SYM_X[c] + 38 && y == r * 16 + 88,
			      "symbols c%d r%d -> (%d,%d)", c, r, x, y);
		}
	CHECK(namegeom_cursor_px(1, 7, 2, &x, &y) == 0, "invalid cell -> 0");
}

// TEST 5 — list geometry: the sanity gate (L10) and the blank-row clamp (L3).
static void test_list_geom(void) {
	puts("TEST 5: listgeom_valid + tap-row clamp");
	ListGeom g = { 30, 8, 0, 112, 16, 120, 128 };   // the EM bag shape (source-derived table §4)
	CHECK(listgeom_valid(&g), "EM bag shape valid");
	// full window: rows 0..7 live when 30 items are scrolled at 0
	CHECK(listgeom_tap_row(&g, 112, 16) == 0, "top-left px -> row 0");
	CHECK(listgeom_tap_row(&g, 231, 143) == 7, "bottom px -> row 7");
	CHECK(listgeom_tap_row(&g, 111, 20) == -1, "left of window dead");
	CHECK(listgeom_tap_row(&g, 232, 20) == -1, "right of window dead");
	CHECK(listgeom_tap_row(&g, 150, 15) == -1, "above window dead");
	CHECK(listgeom_tap_row(&g, 150, 144) == -1, "below window dead");
	// the clamp: 3 live items (say a 3-item pocket) -> rows 3+ DEAD (the old wart)
	g.totalItems = 3;
	CHECK(listgeom_tap_row(&g, 150, 16 + 2 * 16) == 2, "row 2 live");
	CHECK(listgeom_tap_row(&g, 150, 16 + 3 * 16) == -1, "row 3 blank -> DEAD (L3)");
	CHECK(listgeom_tap_row(&g, 150, 16 + 7 * 16) == -1, "row 7 blank -> DEAD");
	// scrolled: 10 items, scroll 4 -> 6 live rows
	g.totalItems = 10; g.scrollOffset = 4;
	CHECK(listgeom_tap_row(&g, 150, 16 + 5 * 16) == 5, "row 5 live (6 remain)");
	CHECK(listgeom_tap_row(&g, 150, 16 + 6 * 16) == -1, "row 6 blank");
	// insanity -> gate closes
	ListGeom bad = g; bad.totalItems = 2000;
	CHECK(!listgeom_valid(&bad), "totalItems 2000 rejected");
	bad = g; bad.maxShowed = 17;
	CHECK(!listgeom_valid(&bad), "maxShowed 17 rejected");
	bad = g; bad.x0 = 200; bad.w = 100;
	CHECK(!listgeom_valid(&bad), "rect off-frame rejected");
	bad = g; bad.scrollOffset = 11;
	CHECK(!listgeom_valid(&bad), "scroll > total rejected");
}

// TEST 6 — arrow bands (L7): 16 px above/below the body, gated on scrollability.
static void test_arrow_bands(void) {
	puts("TEST 6: listgeom_arrow_band");
	ListGeom g = { 30, 8, 4, 112, 16, 120, 128 };
	CHECK(listgeom_arrow_band(&g, 150, 8) == 0, "band above -> UP (scrolled)");
	CHECK(listgeom_arrow_band(&g, 150, 16 + 8 * 16 + 4) == 1, "band below -> DOWN (more items)");
	CHECK(listgeom_arrow_band(&g, 100, 8) == -1, "outside x-band dead");
	g.scrollOffset = 0;
	CHECK(listgeom_arrow_band(&g, 150, 8) == -1, "UP dead at scroll 0");
	g.scrollOffset = 22;   // 22 + 8 == 30: fully scrolled
	CHECK(listgeom_arrow_band(&g, 150, 16 + 8 * 16 + 4) == -1, "DOWN dead at the end");
}

// TEST 7 — fling curve (L5): v<=3 no fling; then v*8 capped at 60.
static void test_fling(void) {
	puts("TEST 7: listgeom_fling_frames");
	CHECK(listgeom_fling_frames(0) == 0,  "v 0 -> 0");
	CHECK(listgeom_fling_frames(3) == 0,  "v 3 -> 0 (at the threshold)");
	CHECK(listgeom_fling_frames(4) == 32, "v 4 -> 32");
	CHECK(listgeom_fling_frames(-5) == 40, "v -5 -> 40 (abs)");
	CHECK(listgeom_fling_frames(7) == 56, "v 7 -> 56");
	CHECK(listgeom_fling_frames(8) == 60, "v 8 -> capped 60");
	CHECK(listgeom_fling_frames(100) == 60, "v 100 -> capped 60");
}

// TEST 8 — bag pocket tabs: EM dots (tile math) + FR arrows (sprite anchors).
static void test_pocket_tabs(void) {
	puts("TEST 8: baggeom pocket tabs");
	for (int i = 0; i < 5; i++) {
		CHECK(baggeom_em_pocket_dot(40 + 8 * i, 24) == i, "EM dot %d center", i);
		CHECK(baggeom_em_pocket_dot(47 + 8 * i, 35) == i, "EM dot %d corner", i);
	}
	CHECK(baggeom_em_pocket_dot(39, 24) == -1, "left of strip dead");
	CHECK(baggeom_em_pocket_dot(80, 24) == -1, "right of strip dead");
	CHECK(baggeom_em_pocket_dot(50, 19) == -1, "above dead");
	CHECK(baggeom_em_pocket_dot(50, 36) == -1, "below dead");
	CHECK(baggeom_fr_pocket_arrow(8, 72) == 0,  "FR LEFT anchor");
	CHECK(baggeom_fr_pocket_arrow(72, 72) == 1, "FR RIGHT anchor");
	CHECK(baggeom_fr_pocket_arrow(40, 72) == -1, "between arrows dead");
	CHECK(baggeom_fr_pocket_arrow(8, 55) == -1, "above band dead");
	CHECK(baggeom_fr_pocket_arrow(8, 88) == -1, "below band dead");
}

// ============================================================================================
// TEST 9 — phase 22.2 storage GRID rects (SPEC-family-grid §1.4): exhaustive 240x160 sweep,
// party panel up and down, against a brute-force restatement of the spec's rects.
// ============================================================================================
static int stor_oracle(int gx, int gy, int inParty, int* pos) {
	if (inParty) {   // party rects first (the panel overlays the buttons/title/grid area)
		if (gx >= 88 && gx < 120 && gy >= 44 && gy < 76) { *pos = 0; return SGH_PARTY; }
		if (gx >= 136 && gx < 168 && gy >= 8 && gy < 128) { *pos = 1 + (gy - 8) / 24; return SGH_PARTY; }
		if (gx >= 128 && gx < 176 && gy >= 124 && gy < 148) { *pos = 6; return SGH_PARTY; }
		if (gx >= 184 && gx < 232 && gy >= 32 && gy < 152) { *pos = (gy - 32) / 24 * 6 + (gx - 88) / 24; return SGH_SLOT; }
		return SGH_NONE;
	}
	if (gy < 18) {
		if (gx >= 96 && gx < 176) return SGH_BTN_PARTY;
		if (gx >= 180 && gx < 236) return SGH_BTN_CLOSE;
		return SGH_NONE;
	}
	if (gy >= 20 && gy < 36) {
		if (gx >= 84 && gx < 100) return SGH_ARROW_L;
		if (gx >= 220 && gx < 236) return SGH_ARROW_R;
		if (gx >= 100 && gx < 220) return SGH_TITLE;
		return SGH_NONE;
	}
	if (gx >= 88 && gx < 232 && gy >= 32 && gy < 152) { *pos = (gy - 32) / 24 * 6 + (gx - 88) / 24; return SGH_SLOT; }
	return SGH_NONE;
}
static void test_storage_rects(void) {
	puts("TEST 9: storage grid hit rects (exhaustive sweep, party up/down)");
	int bad = 0;
	for (int party = 0; party <= 1; party++)
		for (int gy = 0; gy < 160; gy++)
			for (int gx = 0; gx < 240; gx++) {
				int p1 = -1, p2 = -1;
				int k1 = storgeom_hit(gx, gy, party, &p1);
				int k2 = stor_oracle(gx, gy, party, &p2);
				if (k1 != k2 || ((k1 == SGH_SLOT || k1 == SGH_PARTY) && p1 != p2)) bad++;
			}
	CHECK(bad == 0, "sweep disagreements: %d", bad);
	// spot anchors from the engine's own coordinates (icon centers, arrow sprites, cursor x)
	int pos = -1;
	CHECK(storgeom_hit(100, 44, 0, &pos) == SGH_SLOT && pos == 0, "icon center (100,44) = slot 0");
	CHECK(storgeom_hit(100 + 24 * 5, 44 + 24 * 4, 0, &pos) == SGH_SLOT && pos == 29, "icon center c5r4 = slot 29");
	CHECK(storgeom_hit(92, 28, 0, &pos) == SGH_ARROW_L, "(92,28) = left scroll arrow");
	CHECK(storgeom_hit(228, 28, 0, &pos) == SGH_ARROW_R, "(228,28) = right scroll arrow");
	CHECK(storgeom_hit(160, 28, 0, &pos) == SGH_TITLE, "(160,28) = title band");
	CHECK(storgeom_hit(120, 8, 0, &pos) == SGH_BTN_PARTY, "(120,8) = PARTY POKEMON");
	CHECK(storgeom_hit(208, 8, 0, &pos) == SGH_BTN_CLOSE, "(208,8) = CLOSE BOX");
	CHECK(storgeom_hit(104, 52, 1, &pos) == SGH_PARTY && pos == 0, "party lead rect");
	CHECK(storgeom_hit(152, 132, 1, &pos) == SGH_PARTY && pos == 6, "party back/cancel rect");
	CHECK(storgeom_hit(100, 100, 1, &pos) == SGH_NONE, "grid col 0 dead under the party panel");
	CHECK(storgeom_hit(208, 100, 1, &pos) == SGH_SLOT, "grid col 5 alive beside the panel");
}

// ============================================================================================
// TEST 10 — phase 22.2 storage navigator (SPEC-family-grid G2/G3): a pure-C model of the
// engine's own transition table (§1.2, InBoxInput_Normal / HandleInput_OnBox / _OnButtons /
// _InParty) is driven by stornav_step from EVERY start to EVERY target: it must converge,
// quickly, and a step toward an in-box target must never leave the box.
// ============================================================================================
static void stor_model_apply(int* a, int* p, int key) {
	switch (*a) {
	case 0: { int c = *p % 6, r = *p / 6;
		if      (key == SN_UP)    { if (r > 0) *p -= 6; else { *a = 2; *p = 0; } }
		else if (key == SN_DOWN)  { if (r < 4) *p += 6; else { *a = 3; *p = c / 3; } }
		else if (key == SN_LEFT)  { *p = (c > 0) ? *p - 1 : *p + 5; }
		else if (key == SN_RIGHT) { *p = (c < 5) ? *p + 1 : *p - 5; }
		else if (key == SN_START) { *a = 2; *p = 0; }
		break; }
	case 2:
		if      (key == SN_DOWN)  { *a = 0; *p = 2; }
		else if (key == SN_UP)    { *a = 3; *p = 0; }
		break;
	case 3:
		if      (key == SN_UP)    { *a = 0; *p = (*p == 0) ? 24 : 29; }
		else if (key == SN_DOWN || key == SN_START) { *a = 2; *p = 0; }
		else if (key == SN_LEFT)  { *p = (*p == 0) ? 1 : 0; }
		else if (key == SN_RIGHT) { *p = (*p == 1) ? 0 : 1; }
		break;
	case 1:
		if      (key == SN_UP)    { *p = (*p == 0) ? 6 : *p - 1; }
		else if (key == SN_DOWN)  { *p = (*p == 6) ? 0 : *p + 1; }
		else if (key == SN_RIGHT) { if (*p != 0) { *a = 0; *p = 0; } else *p = 3; }   // pos0: cursorPrevHorizPos (any 1..6)
		break;
	}
}
static void test_storage_nav(void) {
	puts("TEST 10: storage navigator convergence (engine-model oracle)");
	// every start x every target across the non-party areas (+ party-to-party and party-to-box)
	static const int AREAS[3] = { 0, 2, 3 };
	static const int NPOS[4]  = { 30, 7, 1, 2 };   // per area id
	for (int sai = 0; sai < 3; sai++) for (int sp = 0; sp < NPOS[AREAS[sai]]; sp++)
	for (int tai = 0; tai < 3; tai++) for (int tp = 0; tp < NPOS[AREAS[tai]]; tp++) {
		int a = AREAS[sai], p = sp, ta = AREAS[tai];
		int steps = 0, ok = 0;
		while (steps < 24) {
			int k = stornav_step(a, p, ta, tp);
			if (k == SN_NONE) { ok = (a == ta && p == tp); break; }
			if (ta == 0 && a == 0)   // G3: a step toward an in-box target must stay in the box
				{ int a2 = a, p2 = p; stor_model_apply(&a2, &p2, k); CHECK(a2 == 0, "in-box step left the box (%d,%d)->(%d,%d) key %d", a, p, ta, tp, k); }
			stor_model_apply(&a, &p, k);
			steps++;
		}
		if (!ok) CHECK(0, "no convergence (%d,%d) -> (%d,%d) after %d steps", AREAS[sai], sp, ta, tp, steps);
		else g_checks++;
	}
	// party-to-party and party-out
	for (int sp = 0; sp <= 6; sp++) for (int tp = 0; tp <= 6; tp++) {
		int a = 1, p = sp, steps = 0, ok = 0;
		while (steps < 12) {
			int k = stornav_step(a, p, 1, tp);
			if (k == SN_NONE) { ok = (a == 1 && p == tp); break; }
			stor_model_apply(&a, &p, k);
			steps++;
		}
		CHECK(ok, "party nav %d -> %d", sp, tp);
	}
	for (int sp = 0; sp <= 6; sp++) {   // party -> a box slot (leaves via RIGHT, then grid nav)
		int a = 1, p = sp, steps = 0, ok = 0;
		while (steps < 24) {
			int k = stornav_step(a, p, 0, 17);
			if (k == SN_NONE) { ok = (a == 0 && p == 17); break; }
			stor_model_apply(&a, &p, k);
			steps++;
		}
		CHECK(ok, "party %d -> box slot 17", sp);
	}
	// unroutable: box -> party returns SN_NONE immediately (the handler drops the target)
	CHECK(stornav_step(0, 5, 1, 2) == SN_NONE, "box -> party is unroutable (handler drops)");
}

// TEST 11 — phase 23 FAM-DLG tap classification (touchgeom.h dlggeom_tap). The whole family's
// safety rests on ONE property: on a NON-pager screen every px of the 240x160 frame is A. A single
// px that resolved to LEFT would read, on a dialog, as "the box didn't advance" — so this is swept
// exhaustively rather than spot-checked.
static void test_dlg_tap(void) {
	puts("TEST 11: FAM-DLG tap zones (exhaustive over the frame)");
	int bad = 0;
	for (int y = 0; y < 160; y++)
		for (int x = 0; x < 240; x++)
			if (dlggeom_tap(x, y, 0) != DLGH_ADVANCE) bad++;
	CHECK(bad == 0, "non-pager: every px is ADVANCE (%d exceptions)", bad);

	// Pager: three full-height bands, boundaries exactly at DLGGEOM_EDGE_PX and 240-EDGE_PX.
	int wrong = 0;
	for (int y = 0; y < 160; y++)
		for (int x = 0; x < 240; x++) {
			int want = (x < DLGGEOM_EDGE_PX) ? DLGH_PAGE_PREV
			         : (x >= 240 - DLGGEOM_EDGE_PX) ? DLGH_PAGE_NEXT : DLGH_ADVANCE;
			if (dlggeom_tap(x, y, 1) != want) wrong++;
			else g_checks++;   // counted, silent
		}
	CHECK(wrong == 0, "pager: the three bands are exact (%d exceptions)", wrong);
	// The boundary pixels themselves, named (a fencepost here silently shrinks the centre).
	CHECK(dlggeom_tap(DLGGEOM_EDGE_PX - 1, 80, 1) == DLGH_PAGE_PREV, "x=EDGE-1 is PREV");
	CHECK(dlggeom_tap(DLGGEOM_EDGE_PX,     80, 1) == DLGH_ADVANCE,   "x=EDGE is ADVANCE");
	CHECK(dlggeom_tap(240 - DLGGEOM_EDGE_PX - 1, 80, 1) == DLGH_ADVANCE,   "x=239-EDGE is ADVANCE");
	CHECK(dlggeom_tap(240 - DLGGEOM_EDGE_PX,     80, 1) == DLGH_PAGE_NEXT, "x=240-EDGE is NEXT");
	// The centre band must be the majority of the frame — an edge zone that ate the screen would
	// still pass the band test above but would be a design bug.
	CHECK(240 - 2 * DLGGEOM_EDGE_PX >= 120, "centre band keeps >= half the frame");
}

// TEST 12 — phase 23 FAM-DLG drag direction. Two properties matter: the dominant axis wins
// OUTRIGHT (never a two-key chord, which would move two cursors at once), and the enum order is
// pinned to touch.c's s_keyDir table {RIGHT, LEFT, DOWN, UP} — a silent reorder there would make
// every drag go the wrong way with no compile error.
static void test_dlg_drag(void) {
	puts("TEST 12: FAM-DLG drag direction + the s_keyDir enum contract");
	const int T = DLGGEOM_DRAG_PX;
	CHECK(DLGD_RIGHT == 0 && DLGD_LEFT == 1 && DLGD_DOWN == 2 && DLGD_UP == 3,
	      "enum order matches touch.c s_keyDir {RIGHT,LEFT,DOWN,UP}");
	CHECK(dlggeom_drag_dir(0, 0) == DLGD_NONE, "no movement -> NONE");
	CHECK(dlggeom_drag_dir(T - 1, 0) == DLGD_NONE, "just under threshold -> NONE");
	CHECK(dlggeom_drag_dir(T, 0) == DLGD_RIGHT, "+x at threshold -> RIGHT");
	CHECK(dlggeom_drag_dir(-T, 0) == DLGD_LEFT, "-x at threshold -> LEFT");
	CHECK(dlggeom_drag_dir(0, T) == DLGD_DOWN, "+y -> DOWN (screen y grows downward)");
	CHECK(dlggeom_drag_dir(0, -T) == DLGD_UP, "-y -> UP");
	CHECK(dlggeom_drag_dir(T, T) == DLGD_RIGHT, "perfect diagonal resolves horizontally, not NONE");
	CHECK(dlggeom_drag_dir(T, -T) == DLGD_RIGHT, "diagonal up-right -> RIGHT");
	CHECK(dlggeom_drag_dir(3, 40) == DLGD_DOWN, "dominant axis wins even when the minor axis is 0-crossing");
	CHECK(dlggeom_drag_dir(40, 3) == DLGD_RIGHT, "and symmetrically");
	// The minor axis NEVER matters once the major one is decided: sweep it.
	int bad = 0;
	for (int minor = -(T - 1); minor <= T - 1; minor++) {
		if (dlggeom_drag_dir(T + 5, minor) != DLGD_RIGHT) bad++;
		if (dlggeom_drag_dir(minor, T + 5) != DLGD_DOWN) bad++;
		else g_checks++;
	}
	CHECK(bad == 0, "minor axis never flips or suppresses the dominant one (%d exceptions)", bad);
	// Sub-threshold on BOTH axes is always NONE — the gesture accumulator must keep accumulating.
	int leaks = 0;
	for (int dx = -(T - 1); dx <= T - 1; dx++)
		for (int dy = -(T - 1); dy <= T - 1; dy++)
			if (dlggeom_drag_dir(dx, dy) != DLGD_NONE) leaks++;
			else g_checks++;
	CHECK(leaks == 0, "both axes sub-threshold -> NONE, always (%d leaks)", leaks);
}

// TEST 13 — PHASE 24 / lane B1: WHO OWNS AN OVERWORLD FRAME. This is the rule that ends the
// field-dialog walk-key leak (LANE-B-TAPVERIFY.md Entry 1: one tap under a Gen-3 message box
// planned three routes and held a direction key at a frozen game for 20+ emulated frames). The
// properties worth grading are BOTH halves — the new behaviour AND the guarantee that nothing
// else moved:
//   (a) ctx == GCTX_OVERWORLD && textDlg  -> FAM-DLG owns it
//   (b) ctx == GCTX_OVERWORLD && !textDlg -> the walker still owns it (tap-to-walk is untouched)
//   (c) EVERY other ctx keeps its own handler REGARDLESS of textDlg — the fall-through context is
//       the only one this rule may ever claim, so a stale sFieldMessageBoxMode can never steal a
//       battle / party / bag / storage frame.
//   (d) the mirrored ctx constant still equals the enum value the app pins with a _Static_assert.
static void test_dlg_route(void) {
	puts("TEST 13: FAM-DLG vs the walker — which handler owns an overworld frame");
	CHECK(DLGGEOM_CTX_FIELD == 1, "DLGGEOM_CTX_FIELD mirrors GCTX_OVERWORLD == 1 "
	      "(touch.c carries the _Static_assert; fieldgate.h FIELD_CTX_OVERWORLD agrees)");
	CHECK(DLGROUTE_WALK == 0 && DLGROUTE_DLG == 1, "route enum values are stable");
	CHECK(dlggeom_route(DLGGEOM_CTX_FIELD, 1, 0) == DLGROUTE_DLG,
	      "overworld + text PRINTING -> FAM-DLG (tap=A anywhere, hold=B, NO route armed)");
	CHECK(dlggeom_route(DLGGEOM_CTX_FIELD, 0, 1) == DLGROUTE_DLG,
	      "overworld + sLockFieldControls -> FAM-DLG. THE load-bearing case: textDlg reads 0 for "
	      "every frame the box sits waiting for A (measured live, phase 24 lane B1), so a rule "
	      "keyed on textDlg alone would miss exactly the frames a player touches");
	CHECK(dlggeom_route(DLGGEOM_CTX_FIELD, 1, 1) == DLGROUTE_DLG, "both set -> FAM-DLG");
	CHECK(dlggeom_route(DLGGEOM_CTX_FIELD, 0, 0) == DLGROUTE_WALK,
	      "overworld, no textbox, controls NOT locked -> the walker, unchanged "
	      "(tap-to-walk is not regressed)");
	// (c) exhaustive over every context value the enum can hold today, plus headroom for the
	// appends this project keeps making, and over both textDlg states.
	int stolen = 0;
	for (int ctx = 0; ctx < 32; ctx++) {
		if (ctx == DLGGEOM_CTX_FIELD) continue;
		for (int td = 0; td <= 1; td++) {
			for (int fl = 0; fl <= 1; fl++) {
				if (dlggeom_route(ctx, td, fl) != DLGROUTE_WALK) stolen++;
				else g_checks++;   // counted, silent
			}
		}
	}
	CHECK(stolen == 0, "no other context is ever claimed, at ANY (textDlg, fieldLock) (%d thefts)", stolen);
	// Both are read as truthy ints, not bool8 — a profile read that returns 2 or 0xFF must still
	// mean "a script owns the field" (Emerald's sFieldMessageBoxMode really does read 2).
	CHECK(dlggeom_route(DLGGEOM_CTX_FIELD, 2, 0)   == DLGROUTE_DLG, "textDlg=2 counts");
	CHECK(dlggeom_route(DLGGEOM_CTX_FIELD, 255, 0) == DLGROUTE_DLG, "textDlg=255 counts");
	CHECK(dlggeom_route(DLGGEOM_CTX_FIELD, -1, 0)  == DLGROUTE_DLG, "textDlg=-1 counts");
	CHECK(dlggeom_route(DLGGEOM_CTX_FIELD, 0, 2)   == DLGROUTE_DLG, "fieldLock=2 counts");
	CHECK(dlggeom_route(DLGGEOM_CTX_FIELD, 0, 255) == DLGROUTE_DLG, "fieldLock=255 counts");
}

int main(void) {
	test_colcount();
	test_validity();
	test_hit_sweep();
	test_cursor_px();
	test_list_geom();
	test_arrow_bands();
	test_fling();
	test_pocket_tabs();
	test_storage_rects();
	test_storage_nav();
	test_dlg_tap();
	test_dlg_drag();
	test_dlg_route();
	printf("\n=== %d checks, %d failures ===\n", g_checks, g_fails);
	return g_fails ? 1 : 0;
}
