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
/* PHASE 30 — HARDWARE DEFECT H4. 6 px was a STYLUS threshold. A finger tap drifts while the
 * contact patch grows and rolls, routinely 8-14 px, so most real taps crossed it and became
 * DRAGS: on a dialog that emitted a D-pad direction instead of A (the user had to tap several
 * times before one 'took'), and in a list it scrolled instead of selecting the row. Same shape
 * as H1/H2 — a constant that is correct for a synthesized point and wrong for a thumb. 12 px is
 * still far below a deliberate drag, which travels 30 px or more. */
#define LISTGEOM_SLOP_PX    12   // tap-vs-drag one-way latch (UIHIT_DRAG_PX convention, L3)
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
/* PHASE 30 — HARDWARE DEFECT H4. 6 px was a STYLUS threshold. A finger tap drifts while the
 * contact patch grows and rolls, routinely 8-14 px, so most real taps crossed it and became
 * DRAGS: on a dialog that emitted a D-pad direction instead of A (the user had to tap several
 * times before one 'took'), and in a list it scrolled instead of selecting the row. Same shape
 * as H1/H2 — a constant that is correct for a synthesized point and wrong for a thumb. 12 px is
 * still far below a deliberate drag, which travels 30 px or more. */
#define DLGGEOM_SLOP_PX      12   // tap-vs-drag one-way latch (the shared UIHIT_DRAG_PX convention)
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

// ============== FAM-MAP — the REGION MAP / TAP-TO-FLY family (phase 24, lane B2) ===============
// TOUCH-PLAN.md rows B7 (field/wall region map) and B8 (fly map) — "the single most obviously
// touch-shaped screen in the game". Ground truth = pret pokeemerald src/region_map.c +
// include/region_map.h, read from the LOCAL clone this session (the same clone the keyboard and
// grid families were derived from), cross-checked against the local pokeemerald.sym:
//
//   struct RegionMap (include/region_map.h:28-83, byte-identical to vanilla — the fork only
//   typedefs mapSecId's u16):
//     +0x000 u16 mapSecId · +0x002 u8 mapSecType · +0x054 u16 cursorPosX · +0x056 u16 cursorPosY
//     +0x078 bool8 zoomed
//   sRegionMap (0x0203A144, `l 00000004` = a POINTER) is set by InitRegionMapData(regionMap,…)
//   for EVERY instance — the wall map, the fly map's embedded `sFlyMap->regionMap`, PokeNav — so
//   ONE address covers them all and it is always the live one while a map cb2 is up.
//
//   Cursor bounds (src/region_map.c:41-46): MAP_WIDTH 28 / MAP_HEIGHT 15 / MAPCURSOR_X_MIN 1 /
//   MAPCURSOR_Y_MIN 2  =>  cursorPosX 1..28, cursorPosY 2..16.
//   Cell -> pixel (CreateRegionMapCursor, :1418-1419): cursorSprite->x = 8*cursorPosX + 4,
//   y = 8*cursorPosY + 4. The sprite is 16x16 with centerToCorner -8, so that px pair is the
//   CENTRE of the 8x8 cell => cell (cx,cy) owns exactly [8cx, 8cx+8) x [8cy, 8cy+8), and the
//   inverse a tap needs is a plain shift. Deriving the hit from the game's own cursor formula is
//   what makes this exact rather than a fitted rectangle.
//
//   Movement model (ProcessRegionMapInput_Full, :653-690): JOY_**HELD**, one cell per input frame,
//   X and Y are read INDEPENDENTLY (both deltas can be set on the same frame => a diagonal step is
//   one frame, not two), and a move sets cursorMovementFrameCounter = 4 and swaps inputCallback to
//   MoveRegionMapCursor_Full for the 4-frame slide. cursorPosX/Y are only written when that slide
//   ENDS (:700-730), and the input callback is not polled during it.
//   ==> the driver presses ONE FRAME per cell and then waits: a single-frame press is guaranteed
//   to move exactly one cell no matter what happens next, so the closed loop can never overshoot,
//   which a held key absolutely can (our read lags the emulated frame by one).
//
// A-BUTTON semantics DIFFER between the two screens, which is why the family carries a `fly` bit:
//   fly map   (CB_HandleFlyMapInput, :1746-1775): A on a MAPSECTYPE_CITY_CANFLY (2) or
//             MAPSECTYPE_BATTLE_FRONTIER (4) sets choseFlyLocation and exits -> the warp. A on
//             anything else does NOTHING.
//   wall map  (field_region_map.c CB_HandleInput): A **and** B both EXIT the map.
// So an arrival A is emitted on the fly map only, and only when the live mapSecType says the game
// will accept it — tapping a route moves the cursor and reads its name, exactly like the D-pad.
#define MAPGEOM_X_MIN   1   // MAPCURSOR_X_MIN
#define MAPGEOM_X_MAX  28   // MAPCURSOR_X_MIN + MAP_WIDTH  - 1
#define MAPGEOM_Y_MIN   2   // MAPCURSOR_Y_MIN
#define MAPGEOM_Y_MAX  16   // MAPCURSOR_Y_MIN + MAP_HEIGHT - 1
// mapSecType values the fly map accepts (region_map.h:19-26).
#define MAPSECTYPE_CITY_CANFLY     2
#define MAPSECTYPE_BATTLE_FRONTIER 4

// Tap -> region-map cell. Returns 1 and fills *cx/*cy (the game's own cursorPosX/cursorPosY
// coords) when (gx,gy) is inside the map body; 0 = DEAD. The body is exactly the cursor's legal
// range in pixels — x [8,232), y [16,136) — so the bottom name window, the top border and the
// side margins are dead rather than clamped: a tap on the "PETALBURG CITY" label must not fly you
// to the nearest map square (M1).
int mapgeom_hit(int gx, int gy, int* cx, int* cy);

// One frame of closed-loop navigation: which D-pad keys move the LIVE cursor (curX,curY) toward
// (tgtX,tgtY)? A BITMASK, because the engine reads X and Y independently and a diagonal step
// costs one frame instead of two (this is the opposite of stornav_step, whose engine handles one
// axis per frame). 0 = arrived, or either coordinate out of the legal range (never guess).
enum { MN_RIGHT = 1, MN_LEFT = 2, MN_DOWN = 4, MN_UP = 8 };
int mapnav_step(int curX, int curY, int tgtX, int tgtY);

// ---- PHASE 25 / lane D1: the family gets a SECOND ENGINE (pokefirered's region map) -----------
// FireRed does not run pokeemerald's region map with a different entry point; it is a separate
// implementation (pokefirered src/region_map.c) that disagrees with Emerald about the cell
// bounds, the cell->pixel formula, where the cursor LIVES and how the screen names its own mode.
// What it does NOT disagree about is the part the driver is built on, and that is why one family
// covers both — quoting FireRed's own input pair:
//
//   HandleRegionMapInput (:2754-2831): JOY_HELD per direction, X and Y read INDEPENDENTLY (so a
//     diagonal is one frame), and a move sets `sMapCursor->moveCounter = 4` + swaps
//     `sMapCursor->inputHandler` to MoveMapCursor.
//   MoveMapCursor (:2833-2853): polls NO input while moveCounter != 0 (SpriteCB_MapCursor ticks it
//     down 1/frame while sliding the sprite 2 px), then commits x/y and restores the handler.
//   ==> a SINGLE-FRAME press moves exactly one cell here too: overshoot is impossible and the
//       logical position is written only at the END of the slide, so writing the cursor would
//       desync the sprite exactly as it does on Emerald. Same closed loop, same "never write".
//
// The differences are pure parameters, and they live in MapGeom so a wrong one cannot be shared:
//
//   |                | pokeemerald                    | pokefirered                            |
//   |----------------|--------------------------------|----------------------------------------|
//   | cell bounds    | x 1..28, y 2..16               | x 0..21, y 0..14 (MAP_WIDTH 22/HEIGHT 15)|
//   | cell -> px     | 8*x + 4                        | 8*x + 36  (CreateMapCursor :2696-2697) |
//   | body in px     | x [8,232) y [16,136)           | x [32,208) y [32,152)                  |
//   | mode           | two cb2s                       | ONE cb2 + sRegionMap->type (+0x4796)   |
//   | A accepted     | mapSecType 2 CITY_CANFLY /     | selectedMapsecType 2 MAPSECTYPE_VISITED|
//   |                |             4 BATTLE_FRONTIER  |                    / 4 _UNKNOWN, and   |
//   |                |                                | only when type == FLY (Task_FlyMap     |
//   |                |                                | :3955, MAPPERM_HAS_FLY_DESTINATIONS)   |
//
// The two acceptance sets happen to be the same NUMBERS and are not the same RULE, so they are
// written out per variant rather than shared — a third engine that used {2,3} would otherwise
// inherit a silently wrong constant.
typedef struct {
	int xMin, xMax, yMin, yMax;   // the engine's own legal cursor-cell range
	int pxOrgX, pxOrgY;           // GBA pixel of cell (xMin,yMin)'s LEFT/TOP edge
	int flyA, flyB;               // the two mapSecType values THIS engine's fly map accepts
	int cancelX, cancelY;         // an on-screen CANCEL button cell, or -1/-1 if the engine has none
} MapGeom;
extern const MapGeom MAPGEOM_EM;   // pokeemerald  (the shipped geometry, unchanged)
extern const MapGeom MAPGEOM_FR;   // pokefirered
// FireRed draws a CANCEL button at cell (21,13) and HandleRegionMapInput :2795-2799 turns A there
// into MAP_INPUT_CANCEL **unconditionally, in every mode** — the test is on the cursor cell, not
// on a permission. So "tap the on-screen CANCEL button and the map closes" is the game's own
// semantics on the town map, the wall map and the fly map alike. (SWITCH at (21,11) is
// permission-gated and deliberately NOT wired: it swaps in the Sevii layouts, a different table
// than the one this hit test was derived from.)
#define MAPGEOM_FR_CANCEL_X 21
#define MAPGEOM_FR_CANCEL_Y 13

// The variant-aware forms. `mapgeom_hit`/`mapnav_step` above are these with &MAPGEOM_EM.
int mapgeom_hit_g(const MapGeom* g, int gx, int gy, int* cx, int* cy);
int mapnav_step_g(const MapGeom* g, int curX, int curY, int tgtX, int tgtY);
// Would THIS engine's fly map accept an A on a cell whose live mapSecType is `secType`? This is
// the game's own test, restated: pressing A anywhere else is ignored by the engine, so emitting
// it would be noise indistinguishable from a bug.
int mapgeom_fly_ok(const MapGeom* g, int secType);

// ============== PHASE 25 / lane D2 — FAM-NAV: the PokéNav MENUS (TOUCH-PLAN G1) ================
// One vertical column of option labels, and a cursor that WRAPS. Everything below is read off
// pokeemerald, never off a screenshot.
//
// GEOMETRY. `CreateMenuOptionSprites` (src/pokenav_menu_handler_gfx.c:817-829) makes
// MAX_POKENAV_MENUITEMS rows x NUM_OPTION_SUBSPRITES 4 sprites of SPRITE_SHAPE/SIZE(32x16), and
// `DrawOptionLabelGfx` (:854-884) then places row i at `x = OPTION_DEFAULT_X 140`,
// `y = yStart + deltaY*i`, with subsprite j offset `x2 = 32*j`. The SELECTED row slides to
// `OPTION_SELECTED_X 130` (:34-35, :944-946). A 32x16 OAM's centre-to-corner is (-16,-8), so the
// label spans GBA x [124,252) unselected / [114,242) selected — clipped by the 240-px screen.
// Hence NAVGEOM_X0 below: the union, plus a lead-in, and everything to the right edge.
//
// The per-menuType table is `sPokenavMenuOptionLabelGfx` (:192-253) and the row COUNT is
// `sLastCursorPositions[] = {2,3,4,2,5}` + 1 (src/pokenav_menu_handler.c:34-41):
//
//   | menuType                 | rows | yStart | deltaY | centres                    |
//   |--------------------------|------|--------|--------|----------------------------|
//   | 0 DEFAULT                |  3   |  42    |  20    | 42 62 82                   |
//   | 1 UNLOCK_MC              |  4   |  42    |  20    | 42 62 82 102               |
//   | 2 UNLOCK_MC_RIBBONS      |  5   |  42    |  20    | 42 62 82 102 122           |
//   | 3 CONDITION              |  3   |  56    |  20    | 56 76 96                   |
//   | 4 CONDITION_SEARCH       |  6   |  40    |  16    | 40 56 72 88 104 120        |
//
// Rows are hit-tested at the row PITCH (centre +/- deltaY/2), not at the 16-px sprite height:
// that tiles the column with no dead gaps between adjacent options, while a tap above the first
// row or below the last still misses cleanly. The option-description window is at tilemapTop 17
// (y >= 136), so it is never inside the band.
//
// INPUT MODEL — and the one thing no earlier family had. `UpdateMenuCursorPos`
// (src/pokenav_menu_handler.c:464-487) is JOY_NEW(DPAD_UP/DOWN) with NO auto-repeat, so one key
// EDGE moves exactly one row (the driver must therefore RELEASE between presses), and the list
// **WRAPS BOTH WAYS**: down past the last row lands on 0, up from 0 lands on the last. So the
// shortest route from row 4 to row 0 of a 5-row menu is ONE press up, not four down —
// `navnav_step` implements that, and it is the first family member whose optimal route is not
// the straight-line distance.
//
// WHY THE CURSOR IS STILL NEVER WRITTEN (TOUCH-PLAN G1 says "cursor write + A"; the source says
// no, twice): A acts on `cursorPos` (:212 `sMenuItems[menu->menuType][menu->cursorPos]`) while
// the highlight and the description are driven by `currMenuItem` and the gfx layer's own
// `gfx->cursorPos`, which only move when the handler RETURNS POKENAV_MENU_FUNC_MOVE_CURSOR
// (gfx :891-946). A RAM write updates neither — it would leave a screen whose highlighted row is
// not the row A picks. Same closed loop as FAM-MAP, for a sharper reason.
#define NAVGEOM_X0        104   // left edge of the tappable band (label starts at 114 selected /
                                //   124 not; the 10-px lead-in is slack, not a guess)
#define NAVGEOM_X1        240   // ...to the screen edge
#define NAVGEOM_NTYPES      5   // POKENAV_MENU_TYPE_COUNT
// Rows for a menuType, or 0 if the type is out of range (which is itself the "do not drive this
// screen" answer — a caller that gets 0 must emit nothing).
int navgeom_rows(int menuType);
// Which option row is at (gx,gy)? 1 + *row on a hit, 0 on a miss (outside the band, or below the
// last live row). `menuType` out of range always misses.
int navgeom_hit(int menuType, int gx, int gy, int* row);
// The GBA-pixel centre of a row (for the debug mirror and for the tests' round trip).
int navgeom_row_px(int menuType, int row, int* x, int* y);
// One press towards `tgt` from `cur` on a `rows`-long WRAPPING list: NAVNAV_UP / NAVNAV_DOWN, or
// 0 when already there or when any argument is out of range. Ties (exactly half way round an
// even-length list) resolve DOWN, deterministically.
#define NAVNAV_DOWN 1
#define NAVNAV_UP   2
int navnav_step(int cur, int tgt, int rows);

// ========== PHASE 24 / lane A2 — the OVERWORLD OWN-TILE GESTURE (user decision D1) =============
// docs/phase21-touch-census/DECISIONS-overworld-gestures.md §D1, verbatim: a TAP on the player's
// own tile is START (the field menu) and fires on RELEASE; a HOLD on the player's own tile is
// SELECT (the registered item) and fires the MOMENT the hold threshold is crossed, while the
// finger is still down — and the release that follows must NOT also fire the tap. This REPLACES
// the shipped `tap-self = A` / `double-tap-self = START` pair (COVERAGE.md §1).
//
// WHY NOT DOUBLE-TAP (the user's own reasoning, recorded because it is the load-bearing part):
// a double-tap binding forces EVERY single tap to sit out the double-tap window before it can
// fire, so the most-used action becomes the laggiest. Tap-vs-hold costs nothing — the tap
// resolves on release, the hold resolves on its own timer, and neither waits for the other.
//
// WHERE THE `A` WENT. Nowhere: a tap on the THING you want still routes to it and interacts
// (the phase-18 door/NPC terminals end with A), and every dialog is a FAM-DLG tap-advance screen.
// D1 explicitly forbids re-adding a third self-gesture without a live gap being demonstrated.
//
// THRESHOLDS ARE REUSED, NEVER INVENTED (D1's own instruction): the hold is the FAM-DLG hold, and
// "did not move" is the walker's existing press-vs-drag latch (touch.c owns the px comparison —
// this module never sees pixels, only the boolean the caller already computes for the tap test).
#define OWNGEOM_HOLD_FRAMES DLGGEOM_HOLD_FRAMES   /* 30 ~ 0.5 s at 60 fps — ONE hold in the app */

/* PHASE 30 — HARDWARE DEFECT H3: START and SELECT could not be hit.
 *
 * D1's two verbs live on the player's OWN TILE, and "own tile" was tested as the exact 16x16 GBA
 * tile the avatar stands on. Do the arithmetic in the units the FINGER works in and the bug is
 * obvious: the bottom screen is 320x240 and the user runs the game at 1:1, so a GBA pixel IS a
 * screen pixel and the whole target is 16x16 SCREEN PIXELS. A fingertip contact patch is ~40-50 px
 * across. The user has to land within 8 px of the avatar's centre or the gesture silently becomes
 * a walk route — which is exactly what they reported: "start and select doesnt work".
 *
 * Even at Aspect-fit (1.5x) it is only 24 px, still under half a fingertip. The emulator never saw
 * it because a synthesized tap is a mathematical point placed at the tile centre.
 *
 * So the self test becomes a RADIUS about the avatar's tile centre rather than tile equality.
 * 14 px is chosen, not fitted: it is the largest radius that still leaves an ADJACENT tile's own
 * centre (16 px away) outside the self region, so "tap the tile next to me to step there" keeps
 * working. That is the constraint the number has to satisfy; comfort is what is left over.
 */
#define OWNGEOM_SELF_R 14   /* GBA px from the avatar tile's centre that still counts as "self" */

/* The avatar is always drawn at screen tile (7,5) — the camera centres it — so its tile centre in
 * GBA pixels is fixed. Named here so touch.c and the host suite cannot drift apart. */
#define OWNGEOM_SELF_CX (7 * 16 + 8)   /* 120 */
#define OWNGEOM_SELF_CY (5 * 16 + 8)   /* 88  */

/* True when a touch at (gx,gy) should be read as "on myself" rather than as a destination. */
int owngeom_on_self(int gx, int gy);

enum { OWNG_NONE = 0,   // nothing resolved this frame
       OWNG_START,      // release of a clean tap on the player's own tile -> START
       OWNG_SELECT };   // the hold threshold was crossed on the player's own tile -> SELECT

// Caller-owned, one per seat. Zero-initialise; touch.c's walk_reset() clears it.
typedef struct {
	uint8_t active;   // a gesture is in flight (the finger is down)
	uint8_t armed;    // that gesture STARTED on the player's own tile
	uint8_t moved;    // the press-vs-drag slop was crossed at some point (one-way latch)
	uint8_t fired;    // SELECT already fired for this gesture -> the release must stay silent
	int16_t frames;   // frames the finger has been down
} OwnGest;

// One frame of the own-tile gesture. `touching` = the finger is down THIS frame; `newPress` = it
// went down THIS frame; `onSelf` = the PRESS-TIME tile was the player's own (screen offset 0,0 —
// press-time, not live, so a hold that drifts a pixel is still the same gesture); `movedNow` = the
// caller's slop latch. Returns OWNG_* — at most one event per gesture, and a gesture that fired
// SELECT can never also fire START.
int owngest_step(OwnGest* g, int touching, int newPress, int onSelf, int movedNow);

// ========== PHASE 24 / lane A2 — WALK vs RUN, decided by distance (user decision D2) ===========
// DECISIONS-overworld-gestures.md §D2: "a tap-to-walk route RUNS when the routed path length is at
// or beyond a threshold and WALKS below it — close = walk, far = run", decided PER LEG.
//
// THE MECHANISM ALREADY EXISTS AND IS PROVEN: the D4 control grammar's sprint tokens hold KEY_B in
// the same mask as the direction (control.c:151). The touch route follower simply never pressed B.
//
// THE GAME'S OWN RULE, mirrored (this is the whole safety story — we press B only where the engine
// would have accepted a human's B, so a route can never fight the game):
//
//   pokeemerald src/field_player_avatar.c:658-663  (PlayerNotOnBikeMoving)
//       if (!(gPlayerAvatar.flags & PLAYER_AVATAR_FLAG_UNDERWATER) && (heldKeys & B_BUTTON)
//        && FlagGet(FLAG_SYS_B_DASH)
//        && IsRunningDisallowed(gObjectEvents[gPlayerAvatar.objectEventId].currentMetatileBehavior) == 0)
//            PlayerRun(direction);
//   pokefirered src/field_player_avatar.c:516-523 — the same three conjuncts (FR adds only a
//       rock-stairs slow-run variant, which is a speed, not a gate).
//   ...and the branch ABOVE it, in both engines: `if (flags & PLAYER_AVATAR_FLAG_SURFING)
//       { PlayerWalkFast(direction); return; }` — surfing is already run speed and B is never
//       even read, so a surf leg must not hold B (D2's "the surf leg has its own speed").
//   A BIKE never reaches this function at all (MovePlayerOnBike owns those frames) and there B is
//       the ACRO WHEELIE / hop — which is why biking is a hard NO rather than a no-op.
//
//   IsRunningDisallowed — pokeemerald src/bike.c:1056-1062, pokefirered src/bike.c:253-261:
//       !gMapHeader.allowRunning  ||  MetatileBehaviorForbidsBiking(behaviour)
//   MetatileBehaviorForbidsBiking = MetatileBehavior_IsRunningDisallowed(b)
//                                   || (MetatileBehavior_IsFortreeBridge(b) && !(elevation & 1))
//   MetatileBehavior_IsRunningDisallowed:
//       RSE (pokeemerald src/metatile_behavior.c:1258-1266): MB_NO_RUNNING 0x0A, MB_LONG_GRASS
//           0x03, MB_HOT_SPRINGS 0x28, or IsPacifidlogLog = 0x74..0x77.
//       FRLG (pokefirered src/metatile_behavior.c:695-701): MB_RUNNING_DISALLOWED 0x0A only —
//           FR's IsFortreeBridge and IsPacifidlogLog are literal `return FALSE` stubs (:602-607).
//   gMapHeader.allowRunning is a BITFIELD and the two engines lay it out differently
//   (global.fieldmap.h): RSE byte 0x1A = {allowCycling:1, allowEscaping:1, allowRunning:1,
//   showMapName:5} -> bit 2; FRLG byte 0x18 = bikingAllowed (a whole byte), byte 0x19 =
//   {allowEscaping:1, allowRunning:1, showMapName:6} -> bit 1. touch.c owns those two reads.
//
// EVERY GATE DEGRADES SILENTLY TO WALKING (D2: "never stall, never spam B"). An address this game
// does not have, a save not yet loaded, a behaviour we could not read — all of them clear their
// bit, the leg walks, and nothing else about the route changes.
#define RUNGEOM_MIN_TILES 4   /* D2's starting threshold: >= this many PATH tiles -> run.
                               * Path length, not straight-line distance, so a short hop around a
                               * corner still walks. Named so a hardware feel-test can retune it
                               * without hunting for a literal (VERIFY-ON-HW-PENDING, exactly like
                               * TERM_FRAMES and the FAM-DLG timings). */

// Eligibility bits. All five must hold for B to be pressed on a given frame — that is a
// conjunction in the game too, and every clause here is one of the engine's own.
enum {
	RUNG_SHOES   = 1u << 0,   // FlagGet(FLAG_SYS_B_DASH) — the Running Shoes were received
	RUNG_MAP     = 1u << 1,   // gMapHeader.allowRunning
	RUNG_ONFOOT  = 1u << 2,   // not SURFING, not UNDERWATER, not MACH/ACRO bike
	RUNG_FREE    = 1u << 3,   // not PLAYER_AVATAR_FLAG_FORCED_MOVE (a script owns the avatar)
	RUNG_TERRAIN = 1u << 4,   // the tile under the player does not cancel a dash
	RUNG_ALL     = 0x1Fu
};
// THE ONE ASYMMETRY, and it is the engine's: four of those gates describe the SAVE and the
// AVATAR and are properties of the whole leg, but RUNG_TERRAIN is a property of ONE TILE and the
// engine re-reads it on every single step (`IsRunningDisallowed(gObjectEvents[...]
// .currentMetatileBehavior)` is inside PlayerNotOnBikeMoving, which runs per move). So the LEG
// decision is made on the four, and the tile is asked live, every frame B would be held.
//
// Folding terrain into the leg decision instead looks tidier and is wrong in a way a live run
// showed immediately: standing on a sand bath (MB_NO_RUNNING) and tapping seven tiles away, the
// whole route would walk because of the ONE tile the player is standing on when the plan is made.
// The engine would have walked that first step and run the other six. So does this.
#define RUNG_LATCHED (RUNG_SHOES | RUNG_MAP | RUNG_ONFOOT | RUNG_FREE)

// FpEngine mirrored the way DLGGEOM_CTX_FIELD mirrors GameCtx — this file must stay includable by
// a host test with nothing but <stdint.h>. touch.c carries the _Static_assert that pins them.
#define RUNGEOM_ENG_RSE  0
#define RUNGEOM_ENG_FRLG 1

// The engine's own metatile half of IsRunningDisallowed, inverted to "may I dash here": 1 = the
// tile permits a dash. `elevation` is the player's current elevation and only matters for the RSE
// Fortree bridge clause; pass -1 when it is unknown, which makes the bridge tile answer NO (the
// conservative half — a dash the game refuses is never worth guessing at).
int rungeom_tile_ok(int eng, int behaviour, int elevation);

// THE LIVE TEST — all five gates, i.e. the engine's own conjunction, asked on the frame the key
// mask is built. This is what actually emits B.
int rungeom_eligible(unsigned elig);

// THE LEG DECISION, as a pure function of (routed path length, eligibility mask): 1 = this leg
// intends to run, 0 = it walks. Distance plus the FOUR latched gates (see RUNG_LATCHED above);
// the tile is not consulted here because it is not a property of the leg. Decided per LEG, never
// once for a whole multi-leg program.
int rungeom_decide(int pathLen, unsigned elig);
