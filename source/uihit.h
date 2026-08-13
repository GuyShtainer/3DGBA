// uihit.h — pure-C hit-testing + gesture state for the 3DGBA UI (phase-17 SPEC-input §0.1).
//
// WHY THIS EXISTS. Every screen used to hard-code its hit rects as magic numbers in an `if`
// chain, SEPARATELY from the magic numbers it drew with. That duplication is the direct cause of
// REPORT D2 (the picker's dead touch) and of the mismatches SPEC-input §I1.6/§I3.5/§I3.6
// document. The fix is not "retype the numbers more carefully" — it is to make the drawn rect and
// the hit rect literally THE SAME ARRAY. Draw code and hit code both go through the accessors
// below; a coordinate can no longer drift between them without failing test_uihit.c.
//
// CLAUDE.md rule #4: header-free pure C (<stdint.h> only — no 3ds.h, no citro2d.h) so the whole
// module dual-compiles on the PC and its geometry is unit-tested in ~50 ms instead of on hardware.
#pragma once
#include <stdint.h>

// ---- rects ------------------------------------------------------------------------------------
// Device pixels, origin top-left, y down. Half-open on BOTH axes: a point is inside iff
// x <= px < x+w and y <= py < y+h — matching how the art is rasterised (SPEC-input §I1.5.9).
typedef struct { int16_t x, y, w, h; } UiRect;

int uihit_in(UiRect r, int px, int py);
// Segment index 0..nseg-1 for a point inside `r`; -1 when the point is OUTSIDE r (ui_seg_hit
// clamps instead, which is only safe because it is always called after a rect test).
int uihit_seg(UiRect r, int nseg, int px);
// First rect in `tbl` containing the point, else -1. Empty rects (w or h == 0) never match, which
// is how a table encodes "this control does not exist in this mode".
int uihit_index(const UiRect* tbl, int n, int px, int py);

// ---- the gesture state machine (SPEC-input §I2.1) ----------------------------------------------
// The 3DS reports a VALID touch point only while the touch is down: on the release frame
// hidTouchRead returns (0,0). A handler that reads the position on the release edge therefore
// hit-tests (0,0) — REPORT D2 exactly. This machine consumes the raw point ONLY on the down/held
// frames and latches it, so GEST_TAP always carries the last valid sample and the bug is
// structurally unrepresentable.
#define UIHIT_DRAG_PX 6      // same threshold touch.c:458 already uses for the same judgement

typedef struct {
	int active;      // a touch is down
	int x0, y0;      // press point (latched, always valid)
	int x,  y;       // latest valid point — read this on GEST_TAP
	int base;        // caller-owned: the scroll offset at press time
	int dragged;     // threshold crossed during THIS gesture (one-way latch)
} UiGesture;

typedef enum { GEST_NONE = 0, GEST_DOWN, GEST_DRAG, GEST_TAP } UiGestEv;

// down/held/up = the three KEY_TOUCH edges of one frame; px,py = hidTouchRead's raw point (only
// consumed while down/held). Returns GEST_DOWN on the press frame, GEST_DRAG on every held frame
// after the threshold, GEST_TAP on the release frame iff the threshold was never crossed.
UiGestEv uihit_gesture_step(UiGesture* g, int down, int held, int up, int px, int py);

// ---- the ROM picker (SPEC-input §I1.5) ---------------------------------------------------------
// Rects are the manifest `dynamic` rects the draw code already uses, plus the plate-measured slot
// cards. In SINGLE mode PICK_SLOT_B is an EMPTY rect: one table shape, index == PickTarget, and
// "slot B does not exist" is expressed as a rect nothing can hit.
typedef enum {
	PICK_NONE = -1,
	PICK_MODE = 0,
	PICK_SLOT_A,
	PICK_SLOT_B,
	PICK_START,
	PICK_LINKED,
	PICK_SETTINGS,
	PICK_COUNT
} PickTarget;

const UiRect* uihit_pick_rects(int single, int* n);   // the table itself (n == PICK_COUNT)
UiRect        uihit_pick_rect (int single, int target);
int           uihit_pick      (int single, int px, int py);       // PickTarget or PICK_NONE
// Mode segment under px: 0 = "1 Game" (gameMode 1), 1 = "2 Games" (gameMode 0); -1 = outside.
int           uihit_pick_mode_seg(int single, int px, int py);

// ---- list scrolling (SPEC-input §I2.2) ---------------------------------------------------------
// 24 px of finger travel = one row: the top list's row pitch is 23.5 px, so this is 1:1 between
// what the finger does and what the list does (the shipped 12 scrolled at double rate).
#define UIHIT_PICK_ROW_PX 24

int uihit_scroll_max  (int n, int visRows);                        // max topRow, >= 0
// topRow = clamp(base + dyPx/rowPx, 0, max). dyPx = y0 - y (finger up => list moves down).
int uihit_scroll_clamp(int base, int dyPx, int rowPx, int n, int visRows);
int uihit_clamp_sel   (int sel, int topRow, int visRows, int n);    // highlight follows the scroll
int uihit_follow_sel  (int topRow, int sel, int visRows, int n);    // scroll follows the highlight

// ---- pause / settings CONTENT scroll (SPEC-input §I2.4) ----------------------------------------
// The tab content panel is a VIEWPORT onto content that can be taller than the 240 px screen: the
// manifest puts DISPLAY's frameskip toggle at y=263 and TOUCH's pad-edges row at y=253. The
// shipped code squeezed both up to y=224 to make them "fit", which is exactly why they collided
// with the status hint at y=231 and were clipped by the screen edge (REPORT D8 / D9). Restoring
// the manifest y and scrolling the viewport is the fix; these are its arithmetic.
#define UIHIT_SCREEN_H       240
#define UIHIT_MENU_VIEW_H    228   // content viewport: the status-hint line's baseline is y=231
#define UIHIT_MENU_RAIL_W     82   // measured on the plate art: the tab rail owns x0..81
#define UIHIT_MENU_CONTENT_X  93   // §I2.4.4 — a scroll drag must start in the content column
#define UIHIT_THUMB_MIN       24   // §I2.4.6 — a thumb below this is not a grabbable object
#define UIHIT_MENU_LEAD       14   // caption band pulled in with a focused control (see follow_rect)

// ---- PHASE 18 FIX PASS (review finding 3): the pre-game LINK tab's "why are these faded" note --
// It is content (it scrolls) but it is drawn OUTSIDE the control table, so uihit_content_h cannot
// see it and menu_draw_chrome's repaint of y >= UIHIT_MENU_VIEW_H clipped it. Its y and its string
// live here rather than as literals at the draw site so the host suite grades the SHIPPED values:
// test_typography T11 asserts the line box (y + the face's own cellHeight, read out of
// data/fnt_jbm_med_9.bin) clears UIHIT_MENU_VIEW_H, and T10 asserts the string fits the column.
// The LINK tab's last control is PT_LINK's presence toggle at y196 h18 -> bottom edge 214, and its
// contentH is therefore 240 => maxScroll 0, so this line NEVER scrolls: it has to fit where it is.
#define SET_LINK_NOTE_Y      215
#define SET_LINK_DISABLED_NOTE "Link actions need a running game"

// Content height = max(y+h) over the tab's controls, floored at the screen height. Derived, not
// tabulated, so it CANNOT drift from the control table the way PTABN[] historically did.
int uihit_content_h (const UiRect* tbl, int n);
// A tab whose content fits never scrolls (0). One that overflows scrolls just far enough to lift
// its last row clear of the hint line — hence VIEW_H, not SCREEN_H, on the overflow branch.
int uihit_max_scroll(int contentH);
int uihit_scroll_px (int base, int dyPx, int maxScroll);    // 1:1 finger travel, clamped both ends
// §I2.4.5 — keep `r` fully inside the viewport, so d-pad focus can never land below the fold.
int uihit_follow_rect(int scroll, UiRect r, int maxScroll);
// §I2.4.3 — the rect moves with the content, and the status-hint band is never part of it.
int uihit_in_scrolled   (UiRect r, int scroll, int px, int py);
int uihit_index_scrolled(const UiRect* tbl, int n, int scroll, int px, int py);

// ---- an honest scrollbar (§I2.4.6 / REPORT D18) -------------------------------------------------
// The baked strip on pause-bot-display/touch is a fixed thumb frozen at the BOTTOM of the track
// while the tab renders at scroll-top, i.e. the little it communicates is wrong. These two make
// the thumb a function of the real offset. Units cancel, so the same pair serves a pixel scroll
// (menuScroll/maxScroll) and a row scroll (topRow/maxTop, viewH=visRows, contentH=nRows).
int uihit_thumb_h(int trackH, int viewH, int contentH);
int uihit_thumb_y(int trackY, int trackH, int thumbH, int scroll, int maxScroll);

// ---- word wrap (phase 17 / SPEC-layout L5.2) -----------------------------------------------------
// The TOUCH tab's mode explainer is a paragraph that has to fit a 208x32 manifest rect in ANY of the
// three copies, in a proportional face, in six themes. Laying it out with a hand-counted character
// budget is how a 4th line lands on the Preview buttons, so the layout is DERIVED from the real
// measured width — but `assets_text_w` needs citro2d, so the measurement is a callback and the
// arithmetic lives here, where the host suite can prove it.
//   `out` is a flat maxLines*lineCap byte buffer; each line is written NUL-terminated.
//   `meas(s, ctx)` returns the rendered width of `s` in px.
// Greedy by words; a single word wider than `maxW` is hard-broken on a UTF-8 boundary (never mid
// sequence — the copy carries "—" and "·"). If the text does not fit in `maxLines`, the last line
// is ellipsised with "..." rather than overflowing the rect. Returns the line count written.
int uihit_wrap(const char* s, int maxW, int maxLines, char* out, int lineCap,
               int (*meas)(const char*, void*), void* ctx);
