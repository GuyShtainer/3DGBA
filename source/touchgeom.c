// touchgeom.c — see touchgeom.h. Pure C, stateless; host-tested by test/host/test_touchgeom.c.
#include "touchgeom.h"

// ---------------------------------------------------------------------------------- keyboard --
// currentPage -> keyboard id (pret naming_screen.c sPageToKeyboardId :598-603).
// page 0 = KBPAGE_SYMBOLS -> KEYBOARD_SYMBOLS (2); 1 = UPPER -> 1; 2 = LOWER -> 0.
static int page_to_kb(int page) {
	if (page == 0) return 2;
	if (page == 1) return 1;
	if (page == 2) return 0;
	return -1;
}

// Cell centers = sPageColumnXPos + 38 (SetCursorPos). Letters table is shared by kb 0 and kb 1.
static const int16_t LETTER_CX[8]  = { 38, 50, 62, 94, 106, 118, 130, 161 };
static const int16_t SYMBOL_CX[6]  = { 38, 60, 82, 104, 126, 148 };
// Symbols row lengths (sKeyboardChars: "01234" "56789" "!?♂♀/-" "…“”‘’" -> 5/5/6/5).
static const int8_t  SYMROW_LEN[4] = { 5, 5, 6, 5 };

int namegeom_colcount(int page) {
	int kb = page_to_kb(page);
	if (kb < 0) return 0;
	return (kb == 2) ? 6 : 8;
}

int namegeom_cell_valid(int page, int col, int row) {
	int kb = page_to_kb(page);
	if (kb < 0 || row < 0 || row > 3 || col < 0) return 0;
	if (kb == 2) return col < SYMROW_LEN[row];            // symbols: 5/5/6/5
	// letters: rows 0-1 have all 8 (col 6 = space, col 7 = '.'/','); rows 2-3 have 7 real chars
	// (col 7 is charmap padding, spec R2 — dead).
	return col < ((row < 2) ? 8 : 7);
}

int namegeom_hit(int page, int gx, int gy, int* col, int* row) {
	int kb = page_to_kb(page);
	if (kb < 0) return NGH_NONE;
	// Button column first (R3): gx in [176,232), three contiguous y bands.
	if (gx >= 176 && gx < 232) {
		if (gy >= 76  && gy < 102) return NGH_PAGE;
		if (gy >= 102 && gy < 128) return NGH_BACK;
		if (gy >= 128 && gy < 154) return NGH_OK;
		return NGH_NONE;
	}
	// Character grid (R1): rows are exact 16-px bands; columns snap to the nearest center within
	// a half-cell tolerance, so the inter-group gutters (e.g. gx 68..88 on letter pages) stay dead.
	if (gy < 80 || gy >= 144) return NGH_NONE;
	int r = (gy - 80) / 16;
	const int16_t* cx = (kb == 2) ? SYMBOL_CX : LETTER_CX;
	int n   = (kb == 2) ? 6 : 8;
	int tol = (kb == 2) ? 11 : 6;
	int best = -1, bestD = tol + 1;
	for (int i = 0; i < n; i++) {
		int d = gx - cx[i]; if (d < 0) d = -d;
		if (d < bestD) { bestD = d; best = i; }
	}
	if (best < 0 || !namegeom_cell_valid(page, best, r)) return NGH_NONE;
	if (col) *col = best;
	if (row) *row = r;
	return NGH_CHAR;
}

int namegeom_cursor_px(int page, int col, int row, int* x, int* y) {
	int kb = page_to_kb(page);
	if (kb < 0 || !namegeom_cell_valid(page, col, row)) return 0;
	if (x) *x = (kb == 2) ? SYMBOL_CX[col] : LETTER_CX[col];
	if (y) *y = row * 16 + 88;
	return 1;
}

// ------------------------------------------------------------------------------------- lists --
int listgeom_valid(const ListGeom* g) {
	if (!g) return 0;
	if (g->totalItems <= 0 || g->totalItems > 1024) return 0;
	if (g->maxShowed  <= 0 || g->maxShowed  > 16)   return 0;
	if (g->scrollOffset < 0 || g->scrollOffset > g->totalItems) return 0;
	if (g->w <= 0 || g->h <= 0) return 0;
	if (g->x0 < 0 || g->y0 < 0 || g->x0 + g->w > 240 || g->y0 + g->h > 160) return 0;
	return 1;
}

int listgeom_tap_row(const ListGeom* g, int gx, int gy) {
	if (!listgeom_valid(g)) return -1;
	if (gx < g->x0 || gx >= g->x0 + g->w) return -1;
	if (gy < g->y0 || gy >= g->y0 + g->h) return -1;
	int row = (gy - g->y0) / LISTGEOM_PITCH;
	// The L3 clamp: a row past the end of a short list (or past the window's own showed count)
	// is BLANK -> dead. This is the fix for "tap blank space -> hits CLOSE BAG".
	int live = g->totalItems - g->scrollOffset;
	if (live > g->maxShowed) live = g->maxShowed;
	if (row < 0 || row >= live) return -1;
	return row;
}

int listgeom_arrow_band(const ListGeom* g, int gx, int gy) {
	if (!listgeom_valid(g)) return -1;
	if (gx < g->x0 || gx >= g->x0 + g->w) return -1;
	if (gy >= g->y0 - 16 && gy < g->y0)
		return (g->scrollOffset > 0) ? 0 : -1;                       // UP arrow band
	int yBot = g->y0 + g->maxShowed * LISTGEOM_PITCH;
	if (gy >= yBot && gy < yBot + 16)
		return (g->scrollOffset + g->maxShowed < g->totalItems) ? 1 : -1;   // DOWN arrow band
	return -1;
}

int listgeom_fling_frames(int vAbs) {
	if (vAbs < 0) vAbs = -vAbs;
	if (vAbs <= LISTGEOM_FLING_V) return 0;
	int f = vAbs * 8;
	return (f > LISTGEOM_FLING_CAP) ? LISTGEOM_FLING_CAP : f;
}

// ------------------------------------------------------------------------- bag pocket tabs ----
int baggeom_em_pocket_dot(int gx, int gy) {
	if (gy < 20 || gy >= 36) return -1;
	if (gx < 40 || gx >= 80) return -1;
	return (gx - 40) / 8;   // dot i at x [40+8i, 48+8i) — tile (i+5,3), item_menu.c:1407-1414
}

int baggeom_fr_pocket_arrow(int gx, int gy) {
	if (gy < 56 || gy >= 88) return -1;                    // sprite anchors at y=72, band ±16
	if (gx >= 0  && gx < 24) return 0;                     // LEFT arrow at x=8
	if (gx >= 64 && gx < 96) return 1;                     // RIGHT arrow at x=72
	return -1;
}
