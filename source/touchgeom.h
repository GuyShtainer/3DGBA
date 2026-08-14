// touchgeom.h — phase 22.1 (lane A): PURE-C hit geometry + gesture math for the first two touch
// families, KEYBOARD (the Gen-3 naming screen) and VERTICAL LISTS (the engine-wide ListMenu).
// ============================================================================================
// PURE C (CLAUDE.md rule #4): <stdint.h> only — no libctru, no citro2d, no mGBA — so this module
// dual-compiles on the PC host harness (test/host/test_touchgeom.c) and every cell boundary,
// dead gutter, page column table and drag/fling constant is unit-tested BEFORE it ever meets the
// emulator. touch.c owns all state and all RAM I/O; this file is stateless functions + tables.
//
// Coordinates are GBA-frame pixels (240x160) — the same space every hit_* in touch.c works in.
//
// KEYBOARD ground truth (docs/phase21-touch-census/SPEC-family-keyboard.md §1.2, verified against
// pret pokeemerald src/naming_screen.c THIS session, local clone, and pokefirered master which is
// value-identical in every cited table):
//   * sPageColumnXPos letters {0,12,24,56,68,80,92,123} / symbols {0,22,44,66,88,110}, cell
//     center = xPos + 38 (SetCursorPos :1130-1144: cursorSprite->x = xPos[kb][col] + 38).
//   * rows y = row*16 + 88 (same function); 4 rows.
//   * sPageColumnCounts: letters 8, symbols 6 (:301-305).
//   * sKeyboardChars row content (:280-299): letters rows 2-3 have SEVEN chars (col 7 is charmap
//     padding, NOT a key — a tap there must be DEAD, spec R2); symbols row lengths 5/5/6/5.
//   * currentPage -> keyboard id via sPageToKeyboardId (:598-603): page 0=SYMBOLS->kb 2,
//     1=UPPER->kb 1, 2=LOWER->kb 0 ("GF didn't keep the indexing order consistent", :84-85).
//   * button column sprites PAGE(204,88) BACK(204,116) OK(204,140) — spec §1.2, bands per R3.
//
// LIST ground truth (SPEC-family-lists.md §1.1, pret include/list_menu.h:60-91): struct ListMenu
// = 24-byte template + scrollOffset(+24) + selectedRow(+26) embedded at gTasks[taskId]+8;
// template fields read live: totalItems(+12 u16), maxShowed(+14 u16), windowId(+16 u8); window
// rect from gWindows + 12*windowId bytes +1..+4 (tilemapLeft/Top/width/height, x8 px) — the same
// live-window read hit_fieldmenu ships (touch.c:534-546). Row pitch 16 px on every v1 screen.
#pragma once
#include <stdint.h>

// ---------------------------------------------------------------------------------- keyboard --
// What a tap resolved to (spec §2.2 R5-R8).
enum {
	NGH_NONE = 0,   // dead: gutters, padding cells, title, backdrop (R1/R2/R4)
	NGH_CHAR,       // a character cell -> write-then-A (R5); col/row are the game's own cursor coords
	NGH_PAGE,       // PAGE button band -> SELECT pulse (R6)
	NGH_BACK,       // BACK button band -> B pulse (R7)
	NGH_OK          // OK button band -> START, release, A (R8)
};

// Column count for a currentPage value (0=SYMBOLS 1=UPPER 2=LOWER): 6/8/8. Page >= 3 -> 0.
int namegeom_colcount(int page);

// Is (col,row) a real key on this page (spec R2: letters rows 2-3 have no col 7; symbols rows
// are 5/5/6/5 long)? 1 = yes.
int namegeom_cell_valid(int page, int col, int row);

// Classify a tap at GBA px (gx,gy) for the given currentPage. Returns NGH_*; for NGH_CHAR,
// *col/*row get the game-cursor coords of the tapped cell. Snap tolerance is ±6 px on letter
// pages (12 px cells) and ±11 px on symbols (22 px cells); anything outside a tolerance or on an
// invalid cell is NGH_NONE — mistypes are worse than ignored taps (R1).
int namegeom_hit(int page, int gx, int gy, int* col, int* row);

// The pixel position the game's own SetCursorPos would give this cell (for the sprite x/y write,
// R5): x = column center, y = row*16 + 88. Returns 0 if the cell is invalid on this page.
int namegeom_cursor_px(int page, int col, int row, int* x, int* y);

// ------------------------------------------------------------------------------------- lists --
// Live list geometry, filled by the caller from RAM reads (touch.c owns the bus).
typedef struct {
	int totalItems;    // ListMenu template +12 (u16)
	int maxShowed;     // +14 (u16)
	int scrollOffset;  // +24 (u16) — read for the blank-row CLAMP only, never written (L3/L4)
	int x0, y0, w, h;  // window rect in px (tilemapLeft*8, tilemapTop*8, width*8, height*8)
} ListGeom;

#define LISTGEOM_PITCH     16   // row pitch, all v1 screens (font 16 + itemVerticalPadding 0, L2)
#define LISTGEOM_DRAG_PX   14   // one UP/DOWN key edge per this many drag px (the shipped bag
                                // constant — touch.c bag_update; ~1 row per row-height, L4)
#define LISTGEOM_SWIPE_PX  30   // horizontal swipe threshold (bag pocket switch, L6)
#define LISTGEOM_SLOP_PX    6   // tap-vs-drag one-way latch (UIHIT_DRAG_PX convention, L3)
#define LISTGEOM_FLING_V    3   // release velocity (px/frame over the last 4 frames) above which
                                // a drag becomes a fling-hold (L5)
#define LISTGEOM_FLING_CAP 60   // fling hold cap, frames (L5; hardware-tune pending, spec Q1)

// Sanity gate (L10): 1 = the live geometry is believable (totalItems<=1024, 1<=maxShowed<=16,
// rect on the 240x160 frame, pitch rows fit). 0 => the caller must emit NOTHING for this screen.
int listgeom_valid(const ListGeom* g);

// Tap -> VISIBLE row index (0-based from the window top), or -1 if outside the window body or on
// a blank row below a short list (the L3 clamp: row < min(maxShowed, totalItems - scrollOffset)).
// This is what fixes "tap blank -> CLOSE BAG" everywhere at once.
int listgeom_tap_row(const ListGeom* g, int gx, int gy);

// Scroll-arrow bands (L7): a 16-px band directly ABOVE the window body (-> UP, returns 0) and
// directly BELOW it (-> DOWN, returns 1) inside the window's x-band; -1 = neither. Only bands
// that can actually scroll are live (above needs scrollOffset>0; below needs
// scrollOffset+maxShowed < totalItems).
int listgeom_arrow_band(const ListGeom* g, int gx, int gy);

// Fling hold length in frames for a release velocity (px/frame, absolute): 0 = not a fling
// (v <= LISTGEOM_FLING_V), else min(LISTGEOM_FLING_CAP, v*8) (L5).
int listgeom_fling_frames(int vAbs);

// ------------------------------------------------------------------------- bag pocket tabs ----
// EM: the 5 pocket-indicator dots under the title. DrawPocketIndicatorSquare (pret pokeemerald
// src/item_menu.c:1407-1414) fills tile (pocket+5, 3) => dot i occupies px x [40+8i, 48+8i),
// y [24,32). The touch band is grown to y [20,36) and snaps to the nearest dot (VERIFIED-SRC;
// visually confirmed on evidence/emerald/E3-bag-items.top.png).
// Returns the tapped pocket 0..4, or -1.
int baggeom_em_pocket_dot(int gx, int gy);

// FR/LG: the pocket-switch arrow pair flanking the bag image — sPocketSwitchArrowPairTemplate
// (pret pokefirered src/item_menu.c:287-299): LEFT at (8,72), RIGHT at (72,72). Bands are grown
// to 24x32 px around each sprite anchor (verify-on-emulator). Returns -1 none / 0 LEFT / 1 RIGHT.
int baggeom_fr_pocket_arrow(int gx, int gy);

// ------------------------------------------------------------------- storage GRID (phase 22.2) --
// SPEC-family-grid.md. Ground truth = pret pokeemerald src/pokemon_storage_system.c (line cites in
// the spec §1.4): mon icon centers x=100+24c / y=44+24r (CreateBoxMonIconAtPos :4484-4485) => 24x24
// cell rects; box scroll arrows at (92,28)/(228,28) (:5644); top buttons cursor anchors x=pos*88+120
// (:5849-5851); party panel anchors pos0 (104,52), pos1-5 (152,(p-1)*24+4), pos6 (152,132)
// (:5828-5843, capture-derived bands, verify-in-emulator).
enum {
	SGH_NONE = 0,
	SGH_SLOT,        // *pos = box slot 0..29 (col = pos%6, row = pos/6)
	SGH_TITLE,       // box title band -> cursor to TITLE + A (box options menu)
	SGH_ARROW_L,     // left box-scroll arrow -> cursor to TITLE + held LEFT (scroll box -1)
	SGH_ARROW_R,     // right arrow -> +1
	SGH_BTN_PARTY,   // "PARTY POKEMON" button -> (BUTTONS,0) + A
	SGH_BTN_CLOSE,   // "CLOSE BOX" button -> (BUTTONS,1) + A
	SGH_PARTY        // *pos = party slot 0..6 (6 = the back/CANCEL slot); only when the panel is up
};
// Classify a tap. `inParty` = the live sInPartyMenu read: the party panel occludes grid cols 0..3
// (spec §1.4) — those cells go DEAD and the party rects go live. Exactly one class per px.
int storgeom_hit(int gx, int gy, int inParty, int* pos);

// The closed-loop navigator's single step (SPEC-family-grid G2/G3): which key edge moves the
// LIVE cursor (curArea,curPos) one step toward (tgtArea,tgtPos)? Returns SN_NONE when arrived
// OR when the target is unroutable from here (the caller drops it on timeout). Transition table
// = the engine's own input handlers (spec §1.2, cited): never SELECT, never B, vertical-first
// inside the box so a step can never exit the grid except via the modeled TITLE/BUTTONS edges.
enum { SN_NONE = 0, SN_UP, SN_DOWN, SN_LEFT, SN_RIGHT, SN_START };
int stornav_step(int curArea, int curPos, int tgtArea, int tgtPos);
