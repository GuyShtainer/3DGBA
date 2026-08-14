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

int main(void) {
	test_colcount();
	test_validity();
	test_hit_sweep();
	test_cursor_px();
	test_list_geom();
	test_arrow_bands();
	test_fling();
	test_pocket_tabs();
	printf("\n=== %d checks, %d failures ===\n", g_checks, g_fails);
	return g_fails ? 1 : 0;
}
