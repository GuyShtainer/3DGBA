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


// ============================================================================================
// TEST 14 — PHASE 24 / lane B2: FAM-MAP hit geometry (touchgeom.h mapgeom_hit). The whole
// tap-to-fly family rests on one claim: the pixel a finger lands on maps to the region-map cell
// the GAME would put its cursor on. That claim is not a fitted rectangle — it is the exact
// INVERSE of the engine's own cursor formula (CreateRegionMapCursor, pokeemerald
// src/region_map.c:1418-1419: cursorSprite->x = 8*cursorPosX + 4, y = 8*cursorPosY + 4), so the
// test grades it as a round trip: for every legal cell, the pixel the game draws its cursor at
// must classify back to that cell, and every pixel of the cell's 8x8 body must too.
//
// The second half is the DEAD ZONE, and it matters more than it looks: the fly map draws the
// destination-name window along the bottom and a border at the top. A tap there must be dead, not
// clamped to the nearest square — clamping would fly the player somewhere they did not point at,
// which is the one failure this family must never have (rule M1).
static void test_map_hit(void) {
	puts("TEST 14: FAM-MAP hit geometry (round trip against the engine's own cursor formula)");
	int bad = 0;
	for (int cy = MAPGEOM_Y_MIN; cy <= MAPGEOM_Y_MAX; cy++) {
		for (int cx = MAPGEOM_X_MIN; cx <= MAPGEOM_X_MAX; cx++) {
			int gx = 8 * cx + 4, gy = 8 * cy + 4;   // the pixel the GAME puts the cursor at
			int rx = -1, ry = -1;
			if (!mapgeom_hit(gx, gy, &rx, &ry) || rx != cx || ry != cy) bad++;
			else g_checks++;
		}
	}
	CHECK(bad == 0, "every legal cell round-trips through its own cursor pixel (%d bad)", bad);
	// Every pixel of every cell body resolves to that cell — no gutters inside the map, because
	// the map has none: the cursor can sit on any of the 28x15 squares.
	bad = 0;
	for (int cy = MAPGEOM_Y_MIN; cy <= MAPGEOM_Y_MAX; cy++)
		for (int cx = MAPGEOM_X_MIN; cx <= MAPGEOM_X_MAX; cx++)
			for (int dy = 0; dy < 8; dy++)
				for (int dx = 0; dx < 8; dx++) {
					int rx = -1, ry = -1;
					if (!mapgeom_hit(8 * cx + dx, 8 * cy + dy, &rx, &ry) || rx != cx || ry != cy) bad++;
					else g_checks++;
				}
	CHECK(bad == 0, "every pixel of every cell body resolves to that cell (%d bad)", bad);
	// The dead zone: everything outside the cursor's legal pixel range, swept over the whole frame.
	int leaks = 0, dead = 0;
	for (int gy = 0; gy < 160; gy++)
		for (int gx = 0; gx < 240; gx++) {
			int inside = (gx >= MAPGEOM_X_MIN * 8 && gx < (MAPGEOM_X_MAX + 1) * 8 &&
			              gy >= MAPGEOM_Y_MIN * 8 && gy < (MAPGEOM_Y_MAX + 1) * 8);
			int cx = -1, cy = -1, hit = mapgeom_hit(gx, gy, &cx, &cy);
			if (hit != inside) leaks++;
			else if (!inside) dead++;
		}
	CHECK(leaks == 0, "the map body is exactly x[8,232) y[16,136); everything else is DEAD "
	      "(%d misclassified px, %d dead px swept)", leaks, dead);
	CHECK(mapgeom_hit(120, 150, 0, 0) == 0,
	      "a tap on the fly map's destination-name window is dead, NOT clamped to the nearest "
	      "square (rule M1: never fly somewhere the finger did not point)");
	CHECK(mapgeom_hit(4, 80, 0, 0) == 0, "the left margin (cursorPosX 0 is illegal) is dead");
	CHECK(mapgeom_hit(120, 8, 0, 0) == 0, "the top border (cursorPosY 0/1 are illegal) is dead");
	CHECK(mapgeom_hit(236, 80, 0, 0) == 0, "the right margin (cursorPosX 29 is illegal) is dead");
}

// ============================================================================================
// TEST 15 — PHASE 24 / lane B2: the FAM-MAP navigator, graded against a MODEL OF THE ENGINE
// rather than against itself. The model is pokeemerald's own input pair, transcribed:
//   ProcessRegionMapInput_Full (src/region_map.c:653-690) — JOY_HELD, X and Y read INDEPENDENTLY
//     (so a diagonal is one frame), each axis clamped at its MAPCURSOR bound, and a move sets
//     cursorMovementFrameCounter = 4 + swaps the callback;
//   MoveRegionMapCursor_Full (:700-730) — while the counter is non-zero the input is NOT polled
//     at all; when it hits 0 the cursorPos fields are written and input resumes.
// Driving that model with mapnav_step is what proves the two things the emulator cannot show
// exhaustively: it CONVERGES from every cell to every cell, and it converges in exactly the
// Chebyshev distance (max(|dx|,|dy|)) — i.e. the diagonal is really being used and no press is
// wasted. 420 starts x 420 targets = 176 400 routes.
#define RM_SLIDE 4
static void test_map_nav(void) {
	puts("TEST 15: FAM-MAP navigator convergence (engine-model oracle, 176 400 routes)");
	int worst = 0, bad = 0, overshoot = 0, routes = 0;
	for (int sy = MAPGEOM_Y_MIN; sy <= MAPGEOM_Y_MAX; sy++)
	for (int sx = MAPGEOM_X_MIN; sx <= MAPGEOM_X_MAX; sx++)
	for (int ty = MAPGEOM_Y_MIN; ty <= MAPGEOM_Y_MAX; ty++)
	for (int tx = MAPGEOM_X_MIN; tx <= MAPGEOM_X_MAX; tx++) {
		int cx = sx, cy = sy, slide = 0, presses = 0, frames = 0;
		routes++;
		int dxn = (tx > sx) ? tx - sx : sx - tx, dyn = (ty > sy) ? ty - sy : sy - ty;
		int want = (dxn > dyn) ? dxn : dyn;                 // Chebyshev: diagonals are free
		while ((cx != tx || cy != ty) && frames < 600) {
			frames++;
			if (slide > 0) { if (--slide == 0) { /* cursorPos written at slide end */ } continue; }
			int k = mapnav_step(cx, cy, tx, ty);
			if (!k) break;                                   // navigator gave up
			presses++;
			int ddx = 0, ddy = 0;                            // the engine's own independent reads
			if ((k & MN_UP)    && cy > MAPGEOM_Y_MIN) ddy = -1;
			if ((k & MN_DOWN)  && cy < MAPGEOM_Y_MAX) ddy = +1;
			if ((k & MN_LEFT)  && cx > MAPGEOM_X_MIN) ddx = -1;
			if ((k & MN_RIGHT) && cx < MAPGEOM_X_MAX) ddx = +1;
			if (!ddx && !ddy) break;                         // pressed into a wall: would hang
			cx += ddx; cy += ddy; slide = RM_SLIDE;
			if ((ddx > 0 && cx > tx) || (ddx < 0 && cx < tx) ||
			    (ddy > 0 && cy > ty) || (ddy < 0 && cy < ty)) overshoot++;
		}
		if (cx != tx || cy != ty) bad++;
		else if (presses != want) bad++;
		else g_checks++;
		if (presses > worst) worst = presses;
	}
	CHECK(bad == 0, "every one of %d routes converges in EXACTLY max(|dx|,|dy|) presses "
	      "(%d failures)", routes, bad);
	CHECK(overshoot == 0, "no single-frame press ever steps past the target (%d overshoots) — "
	      "this is why the driver presses one frame per cell instead of holding the key: a held "
	      "key keeps moving while our read of cursorPosX/Y is still one frame behind", overshoot);
	CHECK(worst == MAPGEOM_X_MAX - MAPGEOM_X_MIN, "the widest route costs %d presses "
	      "(28-1 columns; at MAPNAV_GAP+1 = 6 frames each that is ~2.7 s)", worst);
	// Arrived / illegal inputs must emit NOTHING — the driver treats 0 as "stop", so a wrong read
	// (a mid-init struct, a zoomed map) can only ever produce silence.
	CHECK(mapnav_step(5, 5, 5, 5) == 0, "arrived -> no key");
	CHECK(mapnav_step(0, 5, 5, 5) == 0, "cursorPosX 0 is illegal (MAPCURSOR_X_MIN 1) -> no key");
	CHECK(mapnav_step(5, 1, 5, 5) == 0, "cursorPosY 1 is illegal (MAPCURSOR_Y_MIN 2) -> no key");
	CHECK(mapnav_step(29, 5, 5, 5) == 0, "cursorPosX 29 is out of range -> no key");
	CHECK(mapnav_step(5, 17, 5, 5) == 0, "cursorPosY 17 is out of range -> no key");
	CHECK(mapnav_step(5, 5, 0, 5) == 0, "an illegal TARGET is refused too");
	CHECK(mapnav_step(5, 5, 6, 6) == (MN_RIGHT | MN_DOWN), "a diagonal is ONE frame, both bits");
	CHECK(mapnav_step(5, 5, 4, 4) == (MN_LEFT | MN_UP), "…in every quadrant");
	CHECK((MN_RIGHT & MN_LEFT) == 0 && (MN_DOWN & MN_UP) == 0, "the four bits are disjoint");
	CHECK(MAPSECTYPE_CITY_CANFLY == 2 && MAPSECTYPE_BATTLE_FRONTIER == 4,
	      "the two mapSecType values the fly map accepts (region_map.h:19-26) — the driver makes "
	      "the game's OWN test before it emits a confirm A");
}

// ============================================================================================
// TEST 19 — PHASE 25 / lane D1: FAM-MAP's SECOND ENGINE (pokefirered's region map). Lane B2's two
// tests grade Emerald; this one grades the parameterised forms against BOTH engines, so a change
// that quietly assumed Emerald's numbers cannot pass.
//
// The engines disagree about the grid (FR: x 0..21, y 0..14), the cell->pixel formula (FR:
// 8*cell + 36, CreateMapCursor :2696-2697) and therefore about which pixels are DEAD (FR body:
// x [32,208), y [32,152)) — but not about the input model, which is the part the driver is built
// on. So the same two properties are re-proved per engine:
//   (a) every legal cell round-trips through the pixel the GAME draws its cursor at, every pixel
//       of every cell body resolves to that cell, and everything else on the 240x160 frame is
//       DEAD rather than clamped (rule M1: never fly somewhere the finger did not point);
//   (b) the navigator converges from every cell to every cell in EXACTLY max(|dx|,|dy|) presses,
//       with zero overshoots, driven through a transcription of FIRERED'S OWN input pair
//       (HandleRegionMapInput :2754-2831 + MoveMapCursor :2833-2853 — a 4-frame slide that polls
//       no input, which is what makes a single-frame press exactly one cell).
// Plus the two cross-engine safety properties: the geometries must not overlap in their bounds
// (a coordinate legal in one is not silently legal in the other), and each engine's fly-acceptance
// set is its OWN (the numbers coincide; the rules do not).
static void map_roundtrip(const MapGeom* g, const char* who) {
	int bad = 0, k = (g == &MAPGEOM_EM) ? 4 : 36;   // the engine's own cell->sprite constant
	for (int cy = g->yMin; cy <= g->yMax; cy++)
		for (int cx = g->xMin; cx <= g->xMax; cx++) {
			int rx = -1, ry = -1;
			if (!mapgeom_hit_g(g, 8 * cx + k, 8 * cy + k, &rx, &ry) || rx != cx || ry != cy) bad++;
			else g_checks++;
		}
	CHECK(bad == 0, "%s: every legal cell round-trips through the pixel the GAME draws its cursor "
	      "at (8*cell + %d) (%d bad)", who, k, bad);
	bad = 0;
	for (int cy = g->yMin; cy <= g->yMax; cy++)
		for (int cx = g->xMin; cx <= g->xMax; cx++)
			for (int dy = 0; dy < 8; dy++)
				for (int dx = 0; dx < 8; dx++) {
					int px = g->pxOrgX + 8 * (cx - g->xMin) + dx;
					int py = g->pxOrgY + 8 * (cy - g->yMin) + dy;
					int rx = -1, ry = -1;
					if (!mapgeom_hit_g(g, px, py, &rx, &ry) || rx != cx || ry != cy) bad++;
					else g_checks++;
				}
	CHECK(bad == 0, "%s: every pixel of every cell body resolves to that cell (%d bad)", who, bad);
	int leaks = 0, dead = 0;
	int w = 8 * (g->xMax - g->xMin + 1), h = 8 * (g->yMax - g->yMin + 1);
	for (int gy = 0; gy < 160; gy++)
		for (int gx = 0; gx < 240; gx++) {
			int inside = (gx >= g->pxOrgX && gx < g->pxOrgX + w &&
			              gy >= g->pxOrgY && gy < g->pxOrgY + h);
			if (mapgeom_hit_g(g, gx, gy, 0, 0) != inside) leaks++;
			else if (!inside) dead++;
		}
	CHECK(leaks == 0, "%s: the body is exactly x[%d,%d) y[%d,%d) and every other pixel of the "
	      "240x160 frame is DEAD (%d misclassified, %d dead px swept)", who,
	      g->pxOrgX, g->pxOrgX + w, g->pxOrgY, g->pxOrgY + h, leaks, dead);
}

static void map_converge(const MapGeom* g, const char* who) {
	int bad = 0, overshoot = 0, routes = 0, worst = 0;
	for (int sy = g->yMin; sy <= g->yMax; sy++)
	for (int sx = g->xMin; sx <= g->xMax; sx++)
	for (int ty = g->yMin; ty <= g->yMax; ty++)
	for (int tx = g->xMin; tx <= g->xMax; tx++) {
		int cx = sx, cy = sy, slide = 0, presses = 0, frames = 0;
		routes++;
		int dxn = (tx > sx) ? tx - sx : sx - tx, dyn = (ty > sy) ? ty - sy : sy - ty;
		int want = (dxn > dyn) ? dxn : dyn;
		while ((cx != tx || cy != ty) && frames < 600) {
			frames++;
			if (slide > 0) { slide--; continue; }         // MoveMapCursor: polls NO input
			int k = mapnav_step_g(g, cx, cy, tx, ty);
			if (!k) break;
			presses++;
			int ddx = 0, ddy = 0;                          // the engine's independent axis reads
			if ((k & MN_UP)    && cy > g->yMin) ddy = -1;
			if ((k & MN_DOWN)  && cy < g->yMax) ddy = +1;
			if ((k & MN_LEFT)  && cx > g->xMin) ddx = -1;
			if ((k & MN_RIGHT) && cx < g->xMax) ddx = +1;
			if (!ddx && !ddy) break;
			cx += ddx; cy += ddy; slide = RM_SLIDE;
			if ((ddx > 0 && cx > tx) || (ddx < 0 && cx < tx) ||
			    (ddy > 0 && cy > ty) || (ddy < 0 && cy < ty)) overshoot++;
		}
		if (cx != tx || cy != ty || presses != want) bad++;
		else g_checks++;
		if (presses > worst) worst = presses;
	}
	CHECK(bad == 0, "%s: every one of %d routes converges in EXACTLY max(|dx|,|dy|) presses "
	      "(%d failures)", who, routes, bad);
	CHECK(overshoot == 0, "%s: no single-frame press ever steps past the target (%d overshoots)",
	      who, overshoot);
	CHECK(worst == g->xMax - g->xMin, "%s: the widest route costs %d presses", who, worst);
}

static void test_map_engines(void) {
	puts("TEST 19: FAM-MAP's second engine — pokefirered's region map, graded like pokeemerald's");
	// The numbers, from pret, restated here so a silent edit to the table fails the suite.
	CHECK(MAPGEOM_EM.xMin == 1 && MAPGEOM_EM.xMax == 28 && MAPGEOM_EM.yMin == 2 && MAPGEOM_EM.yMax == 16,
	      "EM cursor bounds x 1..28 y 2..16 (MAPCURSOR_X_MIN 1 / Y_MIN 2, MAP_WIDTH 28/HEIGHT 15)");
	CHECK(MAPGEOM_FR.xMin == 0 && MAPGEOM_FR.xMax == 21 && MAPGEOM_FR.yMin == 0 && MAPGEOM_FR.yMax == 14,
	      "FR cursor bounds x 0..21 y 0..14 (MAP_WIDTH 22 / MAP_HEIGHT 15, 0-based)");
	CHECK(MAPGEOM_EM.pxOrgX == 8 && MAPGEOM_EM.pxOrgY == 16, "EM body starts at px (8,16)");
	CHECK(MAPGEOM_FR.pxOrgX == 32 && MAPGEOM_FR.pxOrgY == 32, "FR body starts at px (32,32) — the "
	      "sprite CENTRE of cell (0,0) is 8*0+36, so the cell owns [32,40)");
	map_roundtrip(&MAPGEOM_EM, "EM");
	map_roundtrip(&MAPGEOM_FR, "FR");
	map_converge(&MAPGEOM_EM, "EM");
	map_converge(&MAPGEOM_FR, "FR");
	// The legacy entry points must still BE the Emerald geometry — lane B2's TEST 14/15 grade
	// them, and this pins that they did not quietly become something else.
	int ax = -1, ay = -1, bx = -1, by = -1;
	CHECK(mapgeom_hit(100, 100, &ax, &ay) == mapgeom_hit_g(&MAPGEOM_EM, 100, 100, &bx, &by) &&
	      ax == bx && ay == by, "mapgeom_hit IS mapgeom_hit_g(&MAPGEOM_EM, ...)");
	CHECK(mapnav_step(5, 5, 9, 3) == mapnav_step_g(&MAPGEOM_EM, 5, 5, 9, 3),
	      "mapnav_step IS mapnav_step_g(&MAPGEOM_EM, ...)");
	// Cross-engine independence: a coordinate one engine cannot hold must be refused by it even
	// though the OTHER engine is perfectly happy with it. This is the check that would have caught
	// a shared bounds macro surviving the split.
	CHECK(mapnav_step_g(&MAPGEOM_FR, 0, 0, 5, 5) != 0, "FR cell (0,0) is legal on FireRed");
	CHECK(mapnav_step(0, 0, 5, 5) == 0, "…and illegal on Emerald (MAPCURSOR_X_MIN 1 / Y_MIN 2)");
	CHECK(mapnav_step_g(&MAPGEOM_EM, 5, 5, 28, 16) != 0, "EM cell (28,16) is legal on Emerald");
	CHECK(mapnav_step_g(&MAPGEOM_FR, 5, 5, 28, 16) == 0, "…and illegal on FireRed (x max 21)");
	CHECK(mapgeom_hit_g(&MAPGEOM_FR, 12, 80, 0, 0) == 0, "FR: the left margin is dead…");
	CHECK(mapgeom_hit(12, 80, 0, 0) != 0, "…while the SAME pixel is live map on Emerald");
	CHECK(mapgeom_hit_g(&MAPGEOM_FR, 120, 155, 0, 0) == 0,
	      "FR: a tap below the map body is dead, not clamped (rule M1 on the second engine)");
	// The acceptance sets are per engine. They happen to be the same NUMBERS and are not the same
	// RULE — EM's are CITY_CANFLY/BATTLE_FRONTIER, FR's are MAPSECTYPE_VISITED/_UNKNOWN.
	CHECK(mapgeom_fly_ok(&MAPGEOM_EM, MAPSECTYPE_CITY_CANFLY) &&
	      mapgeom_fly_ok(&MAPGEOM_EM, MAPSECTYPE_BATTLE_FRONTIER),
	      "EM accepts an A on CITY_CANFLY (2) and BATTLE_FRONTIER (4)");
	CHECK(mapgeom_fly_ok(&MAPGEOM_FR, 2) && mapgeom_fly_ok(&MAPGEOM_FR, 4),
	      "FR accepts an A on MAPSECTYPE_VISITED (2) and MAPSECTYPE_UNKNOWN (4)");
	for (int t = 0; t < 256; t++)
		if (t != 2 && t != 4) {
			if (mapgeom_fly_ok(&MAPGEOM_EM, t) || mapgeom_fly_ok(&MAPGEOM_FR, t)) {
				CHECK(0, "mapSecType %d must be refused by both engines", t); break;
			}
			g_checks++;
		}
	CHECK(!mapgeom_fly_ok(0, 2), "a NULL geometry accepts nothing — never guess");
	// FireRed's on-screen CANCEL button. HandleRegionMapInput :2795-2799 makes A there
	// MAP_INPUT_CANCEL in EVERY mode (the test is the cursor cell, not a permission), so the
	// family carries it as geometry; Emerald draws no such button and must say so with -1.
	CHECK(MAPGEOM_FR.cancelX == 21 && MAPGEOM_FR.cancelY == 13,
	      "FR CANCEL button lives at cell (21,13) (CANCEL_BUTTON_X/Y, region_map.c:24-25)");
	CHECK(MAPGEOM_EM.cancelX < 0 && MAPGEOM_EM.cancelY < 0,
	      "EM has NO cancel button — the -1 is what keeps the driver's cancel arm from ever firing "
	      "on Emerald, where A on the wall map already exits and A on the fly map is a confirm");
	{   // the button is inside the map body, i.e. it is genuinely tappable
		int cx = -1, cy = -1;
		CHECK(mapgeom_hit_g(&MAPGEOM_FR, 8 * MAPGEOM_FR.cancelX + 36, 8 * MAPGEOM_FR.cancelY + 36,
		                    &cx, &cy) && cx == MAPGEOM_FR.cancelX && cy == MAPGEOM_FR.cancelY,
		      "the CANCEL cell is inside the FR map body and round-trips like any other cell");
	}
}

// ================== PHASE 24 / lane A2 — the OWN-TILE GESTURE (decision D1) ====================
// A gesture is a TIMELINE, so the oracle is a timeline driver: it replays a synthetic touch as the
// app sees it (one call per frame, `touching` / `newPress` / the caller's slop latch) and records
// every event the resolver emitted. The properties D1 actually asks for are then assertions about
// the RECORDED SEQUENCE, not about a single call: "the release after a hold is silent" cannot be
// stated any other way.
typedef struct { int start, select, first; } OwnRun;   // counts + the first event seen

// frames = how long the finger is down; onSelf = press-time tile is the player's; moveAt = the
// frame index the slop latch flips (<0 = never). One extra frame is played with the finger up so
// the release is always delivered.
static OwnRun own_drive(int frames, int onSelf, int moveAt) {
	OwnGest g; memset(&g, 0, sizeof g);
	OwnRun r = { 0, 0, OWNG_NONE };
	int moved = 0;
	for (int f = 0; f < frames; f++) {
		if (moveAt >= 0 && f >= moveAt) moved = 1;
		int ev = owngest_step(&g, 1, f == 0, onSelf, moved);
		if (ev == OWNG_START)  { r.start++;  if (r.first == OWNG_NONE) r.first = ev; }
		if (ev == OWNG_SELECT) { r.select++; if (r.first == OWNG_NONE) r.first = ev; }
	}
	int ev = owngest_step(&g, 0, 0, onSelf, moved);
	if (ev == OWNG_START)  { r.start++;  if (r.first == OWNG_NONE) r.first = ev; }
	if (ev == OWNG_SELECT) { r.select++; if (r.first == OWNG_NONE) r.first = ev; }
	return r;
}

static void test_own_gesture(void) {
	puts("TEST 16: the own-tile gesture — tap = START on release, hold = SELECT on the threshold");

	// The two verbs exist, they are distinct, and neither is the idle value.
	CHECK(OWNG_NONE == 0 && OWNG_START != OWNG_SELECT && OWNG_START != OWNG_NONE,
	      "OWNG_NONE/START/SELECT are three distinct values with NONE == 0");
	CHECK(OWNGEOM_HOLD_FRAMES == DLGGEOM_HOLD_FRAMES,
	      "D1: the hold threshold is REUSED from FAM-DLG (%d frames), not a third invention",
	      OWNGEOM_HOLD_FRAMES);

	// --- the whole duration axis, exhaustively: every press length from 1 frame to 4x the hold.
	int tapLo = 0, tapHi = 0, holdLo = 0;
	for (int n = 1; n <= OWNGEOM_HOLD_FRAMES * 4; n++) {
		OwnRun r = own_drive(n, 1, -1);
		CHECK(r.start + r.select == 1,
		      "a %d-frame clean press on the player's own tile resolves to EXACTLY ONE event "
		      "(got start=%d select=%d) — never both, never none", n, r.start, r.select);
		if (n < OWNGEOM_HOLD_FRAMES) {
			if (r.start == 1 && r.select == 0) tapLo++; else tapHi++;
		} else {
			// D1: the hold fires the MOMENT the threshold is crossed, and the release that ends it
			// must stay silent. Both halves are in this one assertion.
			if (r.select == 1 && r.start == 0) holdLo++;
			CHECK(r.first == OWNG_SELECT,
			      "a %d-frame press (>= the %d-frame hold) resolves as SELECT, and the release "
			      "that follows it fires NOTHING (start=%d)", n, OWNGEOM_HOLD_FRAMES, r.start);
		}
	}
	CHECK(tapLo == OWNGEOM_HOLD_FRAMES - 1 && tapHi == 0,
	      "every press shorter than the hold (%d of them) is a START on release", tapLo);
	CHECK(holdLo == OWNGEOM_HOLD_FRAMES * 3 + 1,
	      "every press at or past the hold is a SELECT (%d of them)", holdLo);

	// --- the boundary, stated on its own so a threshold change shows up here first.
	CHECK(own_drive(OWNGEOM_HOLD_FRAMES - 1, 1, -1).start == 1,  "hold-1 frames = TAP -> START");
	CHECK(own_drive(OWNGEOM_HOLD_FRAMES,     1, -1).select == 1, "hold frames exactly = SELECT");
	CHECK(own_drive(OWNGEOM_HOLD_FRAMES,     1, -1).start == 0,  "...and NOT also a START");

	// --- a press that did not start on the player's own tile is not this family's business at all
	//     (it is a tap-to-walk route or a steer, both of which touch.c resolves elsewhere).
	for (int n = 1; n <= OWNGEOM_HOLD_FRAMES * 2; n++) {
		OwnRun r = own_drive(n, 0, -1);
		CHECK(r.start == 0 && r.select == 0,
		      "a %d-frame press that started OFF the player's tile emits nothing here", n);
	}

	// --- D1: "a finger that MOVES past the slop is a drag/steer, not a tap or a hold".
	for (int mv = 0; mv < OWNGEOM_HOLD_FRAMES * 2; mv++) {
		OwnRun r = own_drive(OWNGEOM_HOLD_FRAMES * 2, 1, mv);
		if (mv < OWNGEOM_HOLD_FRAMES) {
			CHECK(r.start == 0 && r.select == 0,
			      "slop crossed on frame %d (before the hold) kills BOTH verbs", mv);
		} else {
			// The SELECT had already fired before the finger moved. It is not retroactively
			// cancelled — the button was pressed — but the release still stays silent.
			CHECK(r.select == 1 && r.start == 0,
			      "slop crossed on frame %d (after SELECT already fired) leaves the fired SELECT "
			      "alone and still suppresses the release", mv);
		}
	}

	// --- a finger already down when the resolver starts looking (mode switch, ctx change) must
	//     never resolve: we did not see the press, so we do not know what tile it began on.
	{
		OwnGest g; memset(&g, 0, sizeof g);
		int fired = 0;
		for (int f = 0; f < OWNGEOM_HOLD_FRAMES * 3; f++)
			if (owngest_step(&g, 1, 0, 1, 0) != OWNG_NONE) fired++;
		if (owngest_step(&g, 0, 0, 1, 0) != OWNG_NONE) fired++;
		CHECK(fired == 0, "a touch that was ALREADY down when the machine started is inert");
	}

	// --- an idle machine is silent forever, and a release with no press is not a tap.
	{
		OwnGest g; memset(&g, 0, sizeof g);
		int fired = 0;
		for (int f = 0; f < 100; f++) if (owngest_step(&g, 0, 0, 1, 0) != OWNG_NONE) fired++;
		CHECK(fired == 0, "no touch at all -> no events (100 frames)");
	}

	// --- back-to-back gestures: state from gesture N never leaks into N+1.
	{
		OwnGest g; memset(&g, 0, sizeof g);
		int sel = 0, st = 0;
		for (int rep = 0; rep < 4; rep++) {
			for (int f = 0; f < OWNGEOM_HOLD_FRAMES + 5; f++) {
				int ev = owngest_step(&g, 1, f == 0, 1, 0);
				if (ev == OWNG_SELECT) sel++;
				if (ev == OWNG_START) st++;
			}
			owngest_step(&g, 0, 0, 1, 0);
			for (int f = 0; f < 3; f++) {           // a short tap between the holds
				int ev = owngest_step(&g, 1, f == 0, 1, 0);
				if (ev == OWNG_SELECT) sel++;
				if (ev == OWNG_START) st++;
			}
			if (owngest_step(&g, 0, 0, 1, 0) == OWNG_START) st++;
		}
		CHECK(sel == 4 && st == 4, "four hold+tap pairs -> exactly 4 SELECTs and 4 STARTs "
		      "(got %d/%d): the `fired` latch is per gesture, not sticky", sel, st);
	}
}

// ================== PHASE 24 / lane A2 — WALK vs RUN (decision D2) =============================
// The oracle is the engine's own predicate, restated from pret and kept deliberately separate from
// the implementation (a copy of the code under test proves nothing): run is allowed iff the
// behaviour is not in that engine's disallowed set.
static int oracle_tile_ok(int eng, int b, int elev) {
	if (b == 0x0A) return 0;                                   // both engines
	if (eng == RUNGEOM_ENG_FRLG) return 1;                     // FR/LG: the rest are FALSE stubs
	if (b == 0x03 || b == 0x28) return 0;                      // MB_LONG_GRASS / MB_HOT_SPRINGS
	if (b >= 0x74 && b <= 0x77) return 0;                      // IsPacifidlogLog
	if (b == 0x78) return (elev >= 0) && (elev & 1);           // MB_FORTREE_BRIDGE, odd elevation
	return 1;
}

static void test_run_tile(void) {
	puts("TEST 17: rungeom_tile_ok — the engine's own IsRunningDisallowed metatile half, exhaustive");
	int rseNo = 0, frNo = 0;
	for (int b = 0; b < 256; b++) {
		for (int e = -1; e <= 15; e++) {
			CHECK(rungeom_tile_ok(RUNGEOM_ENG_RSE, b, e) == oracle_tile_ok(RUNGEOM_ENG_RSE, b, e),
			      "RSE behaviour 0x%02X elev %d", b, e);
			CHECK(rungeom_tile_ok(RUNGEOM_ENG_FRLG, b, e) == oracle_tile_ok(RUNGEOM_ENG_FRLG, b, e),
			      "FRLG behaviour 0x%02X elev %d", b, e);
		}
		if (!rungeom_tile_ok(RUNGEOM_ENG_RSE, b, 1)) rseNo++;
		if (!rungeom_tile_ok(RUNGEOM_ENG_FRLG, b, 1)) frNo++;
	}
	// The named tiles, called out individually so a wrong constant reads as itself in the log.
	CHECK(!rungeom_tile_ok(RUNGEOM_ENG_RSE, 0x0A, 3), "MB_NO_RUNNING 0x0A blocks a dash (RSE)");
	CHECK(!rungeom_tile_ok(RUNGEOM_ENG_FRLG, 0x0A, 3), "MB_RUNNING_DISALLOWED 0x0A blocks it (FRLG)");
	CHECK(!rungeom_tile_ok(RUNGEOM_ENG_RSE, 0x03, 3), "MB_LONG_GRASS 0x03 blocks it (RSE only)");
	CHECK(rungeom_tile_ok(RUNGEOM_ENG_FRLG, 0x03, 3), "...and does NOT in FRLG (0x03 is not its list)");
	CHECK(!rungeom_tile_ok(RUNGEOM_ENG_RSE, 0x28, 3),
	      "MB_HOT_SPRINGS 0x28 blocks it — the same behaviour byte lane A1's P4 proof read at "
	      "Lavaridge (`beh=0x28`), so the spring excursion's last leg can never hold B");
	CHECK(!rungeom_tile_ok(RUNGEOM_ENG_RSE, 0x74, 3) && !rungeom_tile_ok(RUNGEOM_ENG_RSE, 0x77, 3),
	      "the four Pacifidlog logs 0x74..0x77 block it");
	CHECK(rungeom_tile_ok(RUNGEOM_ENG_RSE, 0x73, 3) == 1,
	      "0x73 is NOT one of the four logs (the set is exactly 0x74..0x77) and runs fine");
	CHECK(rungeom_tile_ok(RUNGEOM_ENG_RSE, 0x78, 3) == 1, "Fortree bridge, ODD elevation -> allowed");
	CHECK(rungeom_tile_ok(RUNGEOM_ENG_RSE, 0x78, 2) == 0, "Fortree bridge, EVEN elevation -> blocked");
	CHECK(rungeom_tile_ok(RUNGEOM_ENG_RSE, 0x78, -1) == 0,
	      "unknown elevation on the bridge -> blocked (the conservative half: never guess a dash)");
	CHECK(rungeom_tile_ok(RUNGEOM_ENG_RSE, -1, 3) == 0 && rungeom_tile_ok(RUNGEOM_ENG_RSE, 256, 3) == 0,
	      "an unreadable behaviour walks rather than guesses");
	CHECK(rseNo == 7 && frNo == 1,
	      "RSE blocks 7 behaviours at odd elevation and FRLG blocks 1 (got %d/%d) — the two "
	      "engines genuinely disagree and the tables must never merge", rseNo, frNo);
}

static void test_run_decide(void) {
	puts("TEST 18: rungeom_decide — distance decides, and every gate is a veto");

	CHECK(RUNGEOM_MIN_TILES == 4, "D2's starting threshold is 4 PATH tiles");
	CHECK(RUNG_ALL == (RUNG_SHOES | RUNG_MAP | RUNG_ONFOOT | RUNG_FREE | RUNG_TERRAIN),
	      "RUNG_ALL is exactly the five gates");

	// The distance axis, over every path length a window BFS can produce, fully eligible.
	for (int n = 0; n <= 64; n++)
		CHECK(rungeom_decide(n, RUNG_ALL) == (n >= RUNGEOM_MIN_TILES),
		      "path length %d -> %s", n, n >= RUNGEOM_MIN_TILES ? "RUN" : "walk");
	CHECK(rungeom_decide(RUNGEOM_MIN_TILES - 1, RUNG_ALL) == 0, "3 tiles walks (close = walk)");
	CHECK(rungeom_decide(RUNGEOM_MIN_TILES, RUNG_ALL) == 1, "4 tiles runs (far = run)");
	CHECK(rungeom_decide(-3, RUNG_ALL) == 0, "a negative path length can never run");

	// The eligibility axis, exhaustively: 32 masks x a long path, against BOTH predicates. Every
	// missing LATCHED gate vetoes the leg decision and every missing gate at all withholds the
	// live B — the "degrade SILENTLY to walking" half of D2, and the half a partial read (a game
	// with no gMapHeader address, a save not yet loaded) actually exercises.
	int ran = 0, live = 0;
	for (unsigned m = 0; m <= RUNG_ALL; m++) {
		int d = rungeom_decide(32, m);
		int want = ((m & RUNG_LATCHED) == RUNG_LATCHED);
		CHECK(d == want, "mask 0x%02X with a 32-tile path -> %s", m, d ? "RUN" : "walk");
		CHECK(rungeom_eligible(m) == (m == RUNG_ALL),
		      "the LIVE test needs all five for mask 0x%02X", m);
		if (d) ran++;
		if (rungeom_eligible(m)) live++;
	}
	CHECK(ran == 2, "exactly TWO of the 32 masks decide to run (got %d) — all four latched gates, "
	      "with the tile either way, because the tile is not a property of the leg", ran);
	CHECK(live == 1, "...and exactly ONE emits B (got %d): the live test is the full conjunction", live);

	// The five named single-gate failures, spelled out so a regression names its own cause.
	CHECK(rungeom_decide(20, RUNG_ALL & ~RUNG_SHOES) == 0,   "no Running Shoes -> walk");
	CHECK(rungeom_decide(20, RUNG_ALL & ~RUNG_MAP) == 0,     "gMapHeader.allowRunning clear -> walk");
	CHECK(rungeom_decide(20, RUNG_ALL & ~RUNG_ONFOOT) == 0,  "surfing / underwater / on a bike -> walk");
	CHECK(rungeom_decide(20, RUNG_ALL & ~RUNG_FREE) == 0,    "a forced move owns the avatar -> walk");
	CHECK(rungeom_decide(20, 0) == 0, "nothing readable at all -> walk (never a stall, never a B)");

	// THE TILE IS DIFFERENT, and this is the assertion that says why. Standing on a dash-cancelling
	// tile (a sand bath, long grass, the hot spring) does NOT throw away the rest of the route: the
	// leg still decides to run, and the LIVE test withholds B for exactly as long as the player is
	// on that tile — which is precisely what the engine does, since it re-reads
	// currentMetatileBehavior inside every step (field_player_avatar.c PlayerNotOnBikeMoving).
	CHECK(rungeom_decide(20, RUNG_ALL & ~RUNG_TERRAIN) == 1,
	      "a leg that STARTS on a dash-cancelling tile still decides to run");
	CHECK(rungeom_eligible(RUNG_ALL & ~RUNG_TERRAIN) == 0,
	      "...and emits no B while the player is still standing on it");
	CHECK(rungeom_eligible(RUNG_ALL) == 1, "...and emits one again on the very next tile");
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
	test_map_hit();
	test_map_nav();
	test_own_gesture();
	test_run_tile();
	test_run_decide();
	test_map_engines();          // phase 25 (lane D1) FAM-MAP second engine (FireRed)
	printf("\n=== %d checks, %d failures ===\n", g_checks, g_fails);
	return g_fails ? 1 : 0;
}
