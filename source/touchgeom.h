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

// ================================ FAM-DLG — TAP-ADVANCE (phase 23) ==============================
// TOUCH-PLAN.md §2 FAM-DLG, the residual killer's second half. Phase 22.0 promoted ~40 screens out
// of the GCTX_OVERWORLD fall-through into GCTX_TITLE / GCTX_FULLUI, which stopped the walk-key
// LEAK — but those two contexts hit touch.c's `default:` and inject NOTHING, so every dialog,
// cutscene, PSA, Hall of Fame, credits, TV, quest-log, evolution and egg-hatch screen is currently
// touch-DEAD. This family is the one that makes them usable, and it is deliberately the whole of
// what a cursorless screen can honestly support:
//
//   tap  (clean, <= DLGGEOM_SLOP_PX of travel, any duration below the hold)  -> A pulse
//   hold (finger down DLGGEOM_HOLD_FRAMES unmoved)                           -> B, level-triggered
//   drag (> slop)                                                            -> D-pad edges
//
// THREE design calls, all recorded here because they are taste, not fact:
//
//  1. HOLD = B, not "hold = A to speed text up". The plan's §FAM-DLG picked B and the reasons are
//     that B is the only way a pointer can DECLINE (a yes/no default), CANCEL an evolution, or
//     back out of a viewer — and A is already reachable by tapping repeatedly, which is what a
//     player does to advance text anyway. `hold` is level-triggered (the seam is an additive
//     per-frame mask, COVERAGE §5), so it is a true held button, not a pulse train.
//     Open question 5 in TOUCH-PLAN is therefore ANSWERED-BY-DEFAULT here, not by the user.
//
//  2. DRAG = DIRECT MANIPULATION (drag right emits RIGHT), the OPPOSITE of FAM-LIST's
//     content-follows-finger rule (there, drag DOWN emits UP because the list scrolls under the
//     finger). The two are not inconsistent: FAM-LIST screens scroll CONTENT under a fixed
//     viewport, whereas nearly every screen in the FULLUI class that reacts to the D-pad at all
//     moves a CURSOR (fly/region map, options rows, PokeNav ring, FR storage hand, wall clock) —
//     and a cursor should follow the finger. Screens that ignore the D-pad (dialogs, cutscenes,
//     credits) are unaffected either way, which is why this is safe as the class-wide default.
//
//  3. The EDGE ZONES are gated on a PER-SCREEN pager list (GameProfile.cb2Pager), never
//     class-wide. On a dialog, a tap near the screen edge MUST still be A — turning it into LEFT
//     would read as "the box didn't advance". Only screens where LEFT/RIGHT is a real page/value
//     verb opt in (v1: the summary screen and the options menu, both games).
#define DLGGEOM_SLOP_PX      6   // tap-vs-drag one-way latch (the shared UIHIT_DRAG_PX convention)
#define DLGGEOM_HOLD_FRAMES 30   // ~0.5 s at 60 fps before an unmoved finger becomes a held B
#define DLGGEOM_DRAG_PX     14   // one D-pad edge per this many px of drag (the FAM-LIST cadence)
#define DLGGEOM_EDGE_PX     44   // pager side-zone width, px (18% of the 240 px frame per side)

// What a CLEAN tap (no drag, released before the hold) resolves to.
enum {
	DLGH_ADVANCE = 0,   // A pulse — the class default, and the only result on a non-pager screen
	DLGH_PAGE_PREV,     // LEFT  pulse — pager screens only, left edge zone
	DLGH_PAGE_NEXT      // RIGHT pulse — pager screens only, right edge zone
};
// `pager` = 1 when the live cb2 is in this game's GameProfile.cb2Pager list. Off-frame taps never
// reach here (touch.c gates on gvalid), so every (gx,gy) inside 240x160 classifies.
int dlggeom_tap(int gx, int gy, int pager);

// Drag -> the D-pad edge for an accumulated (dx,dy) since the last emitted notch, or DLGD_NONE
// when neither axis has travelled DLGGEOM_DRAG_PX yet. The dominant axis wins outright (a
// diagonal drag never emits two keys in one frame — a chord would move two cursors at once on
// screens that read both axes). Direct manipulation: +dx -> RIGHT, +dy -> DOWN.
enum { DLGD_NONE = -1, DLGD_RIGHT = 0, DLGD_LEFT = 1, DLGD_DOWN = 2, DLGD_UP = 3 };
int dlggeom_drag_dir(int dx, int dy);

// --- PHASE 24 / lane B1: WHICH HANDLER OWNS AN OVERWORLD FRAME -------------------------------
// The TAP-class verification (docs/phase21-touch-census/LANE-B-TAPVERIFY.md) found that ~19 of the
// 48 `TAP` rows are FIELD DIALOGS — a script textbox, a TV, an NPC trade, a cutscene, the Hall of
// Fame — which run under `CB2_Overworld` and therefore never reached FAM-DLG at all. They landed
// in `walk_update`, whose verbs on a frozen game are exactly wrong:
//
//   tap off-self -> a BFS route is planned and a HELD direction key is delivered to a game that
//                   cannot move (witnessed live: curKeys 0x10 for 20+ consecutive emulated frames,
//                   planSeq 3->6 from ONE tap, routeEnd = STALLED, avatar never moved)
//   hold         -> a HELD steering key (curKeys 0x20), where TOUCH-PLAN B3 specifies B
//   tap on self  -> the only correct verb, and the accidental stall-out A ~1.2 s late
//
// The rule below is the whole fix, kept as a pure predicate so it is graded by the host suite
// rather than only by an emulator arc: **an overworld frame with a field textbox up belongs to
// FAM-DLG.** Everything else routes exactly as before — this can never take a frame away from a
// context that has its own handler, because `ctx` has already been resolved by `game_read` and
// only the OVERWORLD fall-through is touched.
//
// `textDlg` is `GameState.textDlg` = `sFieldMessageBoxMode`(EM) / `sMessageBoxType`(FRLG) != 0
// (gamestate.c:737). It is not a new signal: the tilt gate G8 has consumed the same bit since
// phase 17 (fieldgate.h:45) and the traversal sequencer waits on it (touch.c:901/914/945).
// Games whose profile leaves `fieldMsgMode` at 0 (RS today) read textDlg = 0 forever and keep the
// old behaviour — a named degradation, not a silent one.
enum { DLGROUTE_WALK = 0,   // the shipped tap-to-walk / steer machinery
       DLGROUTE_DLG  = 1 }; // FAM-DLG: tap = A, hold = B, drag = D-pad, and NO route is armed
// The GameCtx value this rule keys on, mirrored the way tilt.h/fieldgate.h already mirror it
// (touchgeom.c must stay libctru-free so `clang -I source touchgeom.c` host-compiles); touch.c
// carries the _Static_assert that pins it to the real enum, exactly as main.c:1220 does for tilt.
#define DLGGEOM_CTX_FIELD 1   /* == GCTX_OVERWORLD == FIELD_CTX_OVERWORLD == TILT_CTX_FIELD */
// `ctx` is a GameCtx; `textDlg` = GameState.textDlg (text is PRINTING); `fieldLock` =
// GameState.fieldLock (sLockFieldControls — a script owns the field, INCLUDING every frame the
// box just sits there waiting for A). Either one is sufficient: textDlg alone would miss exactly
// the frames a player touches (measured, phase 24 lane B1), and fieldLock alone would miss any
// game whose profile has no sLockFieldControls address yet. Pure: no state, no bus, no clock.
int dlggeom_route(int ctx, int textDlg, int fieldLock);
