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

// The two engines, as data. Every number is cited in touchgeom.h's table; nothing here is fitted.
const MapGeom MAPGEOM_EM = {
	MAPGEOM_X_MIN, MAPGEOM_X_MAX, MAPGEOM_Y_MIN, MAPGEOM_Y_MAX,   // 1..28 / 2..16
	MAPGEOM_X_MIN * 8, MAPGEOM_Y_MIN * 8,                          // cell (1,2)'s left/top edge
	MAPSECTYPE_CITY_CANFLY, MAPSECTYPE_BATTLE_FRONTIER,
	-1, -1                                                         // Emerald draws no CANCEL button
};
const MapGeom MAPGEOM_FR = {
	0, 21, 0, 14,          // MAP_WIDTH 22 / MAP_HEIGHT 15, and FireRed's cursor is 0-based
	32, 32,                // cell (0,0)'s left/top edge: the sprite CENTRE is 8*0+36, so the cell
	                       // owns [32,40) — the exact inverse of CreateMapCursor's own formula
	2, 4,                  // MAPSECTYPE_VISITED / MAPSECTYPE_UNKNOWN (Task_FlyMap :3955)
	MAPGEOM_FR_CANCEL_X, MAPGEOM_FR_CANCEL_Y
};

int mapgeom_hit_g(const MapGeom* g, int gx, int gy, int* cx, int* cy) {
	if (!g) return 0;
	int w = g->xMax - g->xMin + 1, h = g->yMax - g->yMin + 1;
	if (gx < g->pxOrgX || gx >= g->pxOrgX + 8 * w) return 0;
	if (gy < g->pxOrgY || gy >= g->pxOrgY + 8 * h) return 0;
	if (cx) *cx = g->xMin + ((gx - g->pxOrgX) >> 3);   // the exact inverse of the engine's own
	if (cy) *cy = g->yMin + ((gy - g->pxOrgY) >> 3);   // 8*cell + k cursor formula
	return 1;
}

int mapnav_step_g(const MapGeom* g, int curX, int curY, int tgtX, int tgtY) {
	// Refuse to drive from or to a coordinate the engine cannot hold (a mid-init struct, a zoomed
	// map, a bad pointer): emitting nothing is always safe, guessing is not.
	if (!g) return 0;
	if (curX < g->xMin || curX > g->xMax || curY < g->yMin || curY > g->yMax) return 0;
	if (tgtX < g->xMin || tgtX > g->xMax || tgtY < g->yMin || tgtY > g->yMax) return 0;
	int k = 0;
	if (tgtX > curX) k |= MN_RIGHT;
	else if (tgtX < curX) k |= MN_LEFT;
	if (tgtY > curY) k |= MN_DOWN;
	else if (tgtY < curY) k |= MN_UP;
	return k;
}

int mapgeom_fly_ok(const MapGeom* g, int secType) {
	return g && (secType == g->flyA || secType == g->flyB);
}

int mapgeom_hit(int gx, int gy, int* cx, int* cy) {
	return mapgeom_hit_g(&MAPGEOM_EM, gx, gy, cx, cy);
}

int mapnav_step(int curX, int curY, int tgtX, int tgtY) {
	return mapnav_step_g(&MAPGEOM_EM, curX, curY, tgtX, tgtY);
}

// ================= PHASE 25 / lane D2 — FAM-NAV: the PokéNav menus (G1) ========================
// The derivation, with citations, is in touchgeom.h. This is a straight transcription of
// sLastCursorPositions[]+1 and sPokenavMenuOptionLabelGfx[] — three numbers per menu type.
static const struct { short rows, yStart, deltaY; } NAVGEOM[NAVGEOM_NTYPES] = {
	{ 3, 42, 20 },   // POKENAV_MENU_TYPE_DEFAULT            map / condition / switch off
	{ 4, 42, 20 },   // POKENAV_MENU_TYPE_UNLOCK_MC          + match call
	{ 5, 42, 20 },   // POKENAV_MENU_TYPE_UNLOCK_MC_RIBBONS  + ribbons
	{ 3, 56, 20 },   // POKENAV_MENU_TYPE_CONDITION          party / search / cancel
	{ 6, 40, 16 },   // POKENAV_MENU_TYPE_CONDITION_SEARCH   cool..tough / cancel
};

int navgeom_rows(int menuType) {
	if (menuType < 0 || menuType >= NAVGEOM_NTYPES) return 0;
	return NAVGEOM[menuType].rows;
}

int navgeom_hit(int menuType, int gx, int gy, int* row) {
	if (menuType < 0 || menuType >= NAVGEOM_NTYPES) return 0;
	if (gx < NAVGEOM_X0 || gx >= NAVGEOM_X1) return 0;
	int d = NAVGEOM[menuType].deltaY;
	int top = NAVGEOM[menuType].yStart - d / 2;    // the top edge of row 0's band
	if (gy < top) return 0;
	int r = (gy - top) / d;
	if (r >= NAVGEOM[menuType].rows) return 0;
	if (row) *row = r;
	return 1;
}

int navgeom_row_px(int menuType, int row, int* x, int* y) {
	if (menuType < 0 || menuType >= NAVGEOM_NTYPES) return 0;
	if (row < 0 || row >= NAVGEOM[menuType].rows) return 0;
	// x: the centre of the tappable band, which is also inside the label graphic in both the
	// selected and unselected positions. y: the sprite's own centre, straight off the table.
	if (x) *x = (NAVGEOM_X0 + NAVGEOM_X1) / 2;
	if (y) *y = NAVGEOM[menuType].yStart + NAVGEOM[menuType].deltaY * row;
	return 1;
}

int navnav_step(int cur, int tgt, int rows) {
	if (rows <= 0) return 0;
	if (cur < 0 || cur >= rows || tgt < 0 || tgt >= rows) return 0;
	if (cur == tgt) return 0;
	// UpdateMenuCursorPos wraps in BOTH directions, so the cost of a route is a distance around a
	// ring, not a difference. Going DOWN costs (tgt-cur) mod rows; going UP costs (cur-tgt) mod rows.
	int down = ((tgt - cur) % rows + rows) % rows;
	int up   = ((cur - tgt) % rows + rows) % rows;
	return (down <= up) ? NAVNAV_DOWN : NAVNAV_UP;   // exact ties resolve DOWN, deterministically
}

// ========== PHASE 24 / lane A2 — the OWN-TILE GESTURE (D1) and WALK-vs-RUN (D2) ================
// See touchgeom.h for the decision text and the pret citations. Stateless except for the caller-
// owned OwnGest, which exists only because a gesture is a TIMELINE and a pure function of one
// frame cannot express "the release after a hold stays silent".

int owngest_step(OwnGest* g, int touching, int newPress, int onSelf, int movedNow) {
	if (!g) return OWNG_NONE;

	if (newPress) {   // a fresh gesture: everything about the previous one is gone
		g->active = 1;
		g->armed  = onSelf ? 1 : 0;
		g->moved  = 0;
		g->fired  = 0;
		g->frames = 0;
	}

	if (touching) {
		if (!g->active) {              // finger already down when we started looking: never guess
			g->active = 1; g->armed = 0; g->moved = 0; g->fired = 0; g->frames = 0;
			return OWNG_NONE;
		}
		if (g->frames < 0x7FFF) g->frames++;
		if (movedNow) g->moved = 1;    // one-way latch: a finger that DRAGGED is a steer, not a tap
		// The hold fires WHILE STILL HELD, the moment the threshold is crossed (D1) — not on the
		// release, which is what makes it feel like a button rather than a delayed tap.
		if (g->armed && !g->moved && !g->fired && g->frames >= OWNGEOM_HOLD_FRAMES) {
			g->fired = 1;
			return OWNG_SELECT;
		}
		return OWNG_NONE;
	}

	// released (or never down)
	if (!g->active) return OWNG_NONE;
	int armed = g->armed, moved = g->moved, fired = g->fired, frames = g->frames;
	g->active = 0; g->armed = 0; g->moved = 0; g->fired = 0; g->frames = 0;
	// `fired` is the whole point of the second half of D1: the release that ends a HOLD must not
	// also fire the tap. `frames >= 1` keeps a phantom release (a frame with neither press nor
	// hold recorded) from resolving to a START.
	if (armed && !moved && !fired && frames >= 1) return OWNG_START;
	return OWNG_NONE;
}

int rungeom_tile_ok(int eng, int behaviour, int elevation) {
	if (behaviour < 0 || behaviour > 0xFF) return 0;   // unreadable tile -> walk (never guess)
	if (behaviour == 0x0A) return 0;                   // MB_NO_RUNNING / MB_RUNNING_DISALLOWED, both engines
	if (eng == RUNGEOM_ENG_FRLG) return 1;             // FR/LG stop there: the rest are FALSE stubs
	// RSE (pokeemerald metatile_behavior.c:1258-1266 + bike.c:901-907)
	if (behaviour == 0x03) return 0;                   // MB_LONG_GRASS
	if (behaviour == 0x28) return 0;                   // MB_HOT_SPRINGS
	if (behaviour >= 0x74 && behaviour <= 0x77) return 0;   // MB_PACIFIDLOG_*_LOG_* (IsPacifidlogLog)
	if (behaviour == 0x78) return (elevation >= 0) && (elevation & 1);   // MB_FORTREE_BRIDGE
	return 1;
}

int rungeom_eligible(unsigned elig) { return (elig & RUNG_ALL) == RUNG_ALL; }

int rungeom_decide(int pathLen, unsigned elig) {
	if (pathLen < RUNGEOM_MIN_TILES) return 0;   // "close = walk" — the user's rule, first
	// The four LATCHED gates only. A leg that decides to run and then crosses a tile the engine
	// refuses to dash on simply does not get its B on those frames (rungeom_eligible does that);
	// it never fights the game, and it never throws away the other six tiles of the route.
	return (elig & RUNG_LATCHED) == RUNG_LATCHED;
}
