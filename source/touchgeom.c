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

// ------------------------------------------------------------------- storage GRID (phase 22.2) --
// SPEC-family-grid §1.4. Cell math from CreateBoxMonIconAtPos (icon centers 100+24c / 44+24r):
// cell (c,r) rect = x [88+24c, 112+24c) x y [32+24r, 56+24r); grid body x 88..232, y 32..152.
int storgeom_hit(int gx, int gy, int inParty, int* pos) {
	// Party panel FIRST while sInPartyMenu: it slides OVER the grid/title area (capture E12e), so
	// its rects take priority; grid cols 0..3 (x < 184) go DEAD under it, cols 4..5 stay tappable.
	// Rects are cursor-anchor derived (spec §1.4: pos0 (104,52), pos1-5 (152,(p-1)*24+4),
	// pos6 (152,132)) — capture-derived bands, verify-in-emulator.
	if (inParty) {
		if (gx >= 88 && gx < 120 && gy >= 44 && gy < 76) { if (pos) *pos = 0; return SGH_PARTY; }
		if (gx >= 136 && gx < 168 && gy >= 8 && gy < 128) {
			int p = 1 + (gy - 8) / 24;
			if (p >= 1 && p <= 5) { if (pos) *pos = p; return SGH_PARTY; }
			return SGH_NONE;
		}
		if (gx >= 128 && gx < 176 && gy >= 124 && gy < 148) { if (pos) *pos = 6; return SGH_PARTY; }
		if (gx >= 184 && gx < 232 && gy >= 32 && gy < 152) {
			int c = (gx - 88) / 24, r = (gy - 32) / 24;
			if (pos) *pos = r * 6 + c;
			return SGH_SLOT;
		}
		return SGH_NONE;
	}
	// Top buttons row (cursor anchors x = pos*88 + 120, GetCursorCoordsByPos :5849-5851; widths
	// capture-derived from E12-storage-boxes.top.png, verify-in-emulator).
	if (gy >= 0 && gy < 18) {
		if (gx >= 96  && gx < 176) return SGH_BTN_PARTY;
		if (gx >= 180 && gx < 236) return SGH_BTN_CLOSE;
		return SGH_NONE;
	}
	// Title bar band: scroll arrows at (92,28)/(228,28) (CreateBoxScrollArrows :5644), the box
	// name between them.
	if (gy >= 20 && gy < 36) {
		if (gx >= 84  && gx < 100) return SGH_ARROW_L;
		if (gx >= 220 && gx < 236) return SGH_ARROW_R;
		if (gx >= 100 && gx < 220) return SGH_TITLE;
		return SGH_NONE;
	}
	// The 6x5 grid.
	if (gx >= 88 && gx < 232 && gy >= 32 && gy < 152) {
		int c = (gx - 88) / 24, r = (gy - 32) / 24;
		if (pos) *pos = r * 6 + c;
		return SGH_SLOT;
	}
	return SGH_NONE;
}

// SPEC-family-grid G2/G3 + §1.2 (the engine's own transition table, cited per case).
int stornav_step(int curArea, int curPos, int tgtArea, int tgtPos) {
	if (curArea == tgtArea && curPos == tgtPos) return SN_NONE;   // arrived
	switch (curArea) {
	case 0:   // IN_BOX (InBoxInput_Normal)
		if (tgtArea == 2) return SN_START;                        // START jumps to the title
		if (tgtArea == 3) return SN_DOWN;                         // repeated DOWN exits the grid bottom
		if (tgtArea == 0) {
			int c = curPos % 6, r = curPos / 6, tc = tgtPos % 6, tr = tgtPos / 6;
			if (r < tr) return SN_DOWN;                           // r<tr<=4 -> stays inside (G3)
			if (r > tr) return SN_UP;                             // r>tr>=0 -> stays inside
			int d = (tc - c + 6) % 6;                             // LEFT/RIGHT wrap within the row
			return (d <= 3) ? SN_RIGHT : SN_LEFT;
		}
		return SN_NONE;                                           // party: unroutable from the box
	case 2:   // BOX_TITLE (HandleInput_OnBox): pos is always 0
		if (tgtArea == 0) return SN_DOWN;                         // -> IN_BOX pos 2, grid nav follows
		if (tgtArea == 3) return SN_UP;                           // -> BUTTONS 0
		return SN_NONE;
	case 3:   // BUTTONS (HandleInput_OnButtons)
		if (tgtArea == 3) return (tgtPos > curPos) ? SN_RIGHT : SN_LEFT;
		if (tgtArea == 2) return SN_DOWN;
		if (tgtArea == 0) return SN_UP;                           // -> box pos 24/29, grid nav follows
		return SN_NONE;
	case 1:   // IN_PARTY (HandleInput_InParty): UP/DOWN cycle 0..6
		if (tgtArea == 1) {
			int d = (tgtPos - curPos + 7) % 7;
			return (d <= 3) ? SN_DOWN : SN_UP;
		}
		return SN_RIGHT;                                          // RIGHT leaves the party -> box
	default:
		return SN_NONE;
	}
}

// ================================ FAM-DLG — TAP-ADVANCE (phase 23) ==============================
// See touchgeom.h for the three design calls (hold=B, drag=direct-manipulation, edge zones gated
// on the per-screen pager list). Stateless, like everything else in this file — touch.c owns the
// frame counters and the accumulated drag delta.

int dlggeom_tap(int gx, int gy, int pager) {
	(void)gy;   // the side zones are FULL-HEIGHT bands on purpose: the screens that opt in (summary,
	            // options) draw content edge to edge, so an x-only rule has no dead corner to
	            // explain and the same tap means the same thing wherever the finger lands.
	if (pager) {
		if (gx <  DLGGEOM_EDGE_PX)        return DLGH_PAGE_PREV;
		if (gx >= 240 - DLGGEOM_EDGE_PX)  return DLGH_PAGE_NEXT;
	}
	return DLGH_ADVANCE;
}

int dlggeom_drag_dir(int dx, int dy) {
	int ax = (dx < 0) ? -dx : dx;
	int ay = (dy < 0) ? -dy : dy;
	// Dominant axis wins OUTRIGHT (>=, so a perfect diagonal resolves horizontally rather than
	// emitting nothing): one key edge per notch, never a two-key chord.
	if (ax >= ay) {
		if (ax < DLGGEOM_DRAG_PX) return DLGD_NONE;
		return (dx > 0) ? DLGD_RIGHT : DLGD_LEFT;
	}
	if (ay < DLGGEOM_DRAG_PX) return DLGD_NONE;
	return (dy > 0) ? DLGD_DOWN : DLGD_UP;
}

// PHASE 24 / lane B1 — the one-line rule that ends the field-dialog walk-key leak. See the long
// note in touchgeom.h: an overworld frame with a field textbox up is a FAM-DLG frame. Written as
// an explicit two-clause test rather than `return ctx == ... && textDlg;` so the negative half
// (every other context keeps its own handler, unconditionally) is the thing the reader sees.
int dlggeom_route(int ctx, int textDlg, int fieldLock) {
	if (ctx != DLGGEOM_CTX_FIELD) return DLGROUTE_WALK;   // not the fall-through ctx: never ours
	return (textDlg || fieldLock) ? DLGROUTE_DLG : DLGROUTE_WALK;
}

// ============== FAM-MAP — the REGION MAP / TAP-TO-FLY family (phase 24, lane B2) ===============
// See touchgeom.h for the full pret derivation. Stateless like the rest of this file: touch.c owns
// the armed target, the pacing gap and the arrival A.

int mapgeom_hit(int gx, int gy, int* cx, int* cy) {
	if (gx < MAPGEOM_X_MIN * 8 || gx >= (MAPGEOM_X_MAX + 1) * 8) return 0;
	if (gy < MAPGEOM_Y_MIN * 8 || gy >= (MAPGEOM_Y_MAX + 1) * 8) return 0;
	if (cx) *cx = gx >> 3;   // the exact inverse of the game's own 8*cursorPos + 4 cursor formula
	if (cy) *cy = gy >> 3;
	return 1;
}

int mapnav_step(int curX, int curY, int tgtX, int tgtY) {
	// Refuse to drive from or to a coordinate the engine cannot hold (a mid-init struct, a zoomed
	// map, a bad pointer): emitting nothing is always safe, guessing is not.
	if (curX < MAPGEOM_X_MIN || curX > MAPGEOM_X_MAX || curY < MAPGEOM_Y_MIN || curY > MAPGEOM_Y_MAX) return 0;
	if (tgtX < MAPGEOM_X_MIN || tgtX > MAPGEOM_X_MAX || tgtY < MAPGEOM_Y_MIN || tgtY > MAPGEOM_Y_MAX) return 0;
	int k = 0;
	if (tgtX > curX) k |= MN_RIGHT;
	else if (tgtX < curX) k |= MN_LEFT;
	if (tgtY > curY) k |= MN_DOWN;
	else if (tgtY < curY) k |= MN_UP;
	return k;
}
