// ui.h — the shared citro2d widget kit for the 3DGBA UI redesign (v2, 1:1 pass).
// One drawing language for main.c (HUD/pause/splash), rompicker.c, wireless.c and touch.c:
// fake-rounded fills, 1.5px borders, outlined chips, segmented pills, toggles, cartridge
// glyphs, and alignment helpers. Everything draws to the CURRENTLY BOUND C2D scene.
#pragma once
#include <citro2d.h>

// ---- app liveness (the harness's "is anything rendering?" seam) ------------------------------
// PHASE 17 / SPEC-layout L7.2.3, found by smoke.sh. This counter used to live in main.c and was
// bumped ONLY by run_session's frame loop, which was fine while a ROM-less boot fell straight
// through the empty picker into a dead-core session. With the empty state (sweep D13) the app can
// now sit on a real screen for as long as the user likes without a session ever starting — and the
// harness's read-state / see channels, which poll `g_renderSeq` for change to prove the emulated
// app is alive (.claude/skills/emutest/SKILL.md §1), both went red because the number never moved.
// It lives here now and EVERY UI frame loop bumps it, which is what the harness always meant by it.
// (The D1 watchdog's uses are phase-modulo or pass-through context and only run inside a session,
//  so a non-zero starting value changes nothing there.)
extern volatile unsigned int g_renderSeq;

// ---- primitives ----
// Rounded filled rect. The corner staircase comes from uigeom (pure C, host-tested): the quads are
// NON-OVERLAPPING and cover exactly [y, y+h], so translucent fills never double-darken. `r` is
// clamped to min(w,h)/2, so r == h/2 is a true stadium and r == w/2 == h/2 a disc.
void ui_fill(float x, float y, float w, float h, u32 col, float r);
// 4-strip outline, `t` thick, drawn INSIDE the rect. Square corners (citro2d has no arcs).
void ui_border(float x, float y, float w, float h, u32 col, float t);
// The same outline with ROUNDED corners (transparent interior) — the design's outlined pills and
// chips. Same non-overlap guarantee as ui_fill.
void ui_border_round(float x, float y, float w, float h, u32 col, float t, float r);
// Outline + a separate fill inset by the border (fill may be 0 = none).
void ui_panel(float x, float y, float w, float h, u32 fill, u32 borderCol, float r);

// ---- text (parse + draw in one call; buf = the shared frame C2D_TextBuf) ----
void  ui_text(C2D_TextBuf buf, const char* s, float x, float y, float sz, u32 col);
void  ui_text_c(C2D_TextBuf buf, const char* s, float cx, float y, float sz, u32 col); // centered on cx
void  ui_text_r(C2D_TextBuf buf, const char* s, float rx, float y, float sz, u32 col); // right edge at rx
float ui_text_w(C2D_TextBuf buf, const char* s, float sz);                             // measured width

// ---- widgets ----
// Outlined pill chip ("3D", "DoF", "Link Off"...): border+text in `col`, transparent fill.
// Returns the chip's total width so rows of chips can flow. Draw-only (not a hit target).
float ui_chip(C2D_TextBuf buf, const char* s, float x, float y, u32 col);
// Filled pill chip (label chips like "TOUCH - GAMEPAD", badges "A"/"B"): bg + text.
float ui_chip_fill(C2D_TextBuf buf, const char* s, float x, float y, u32 bg, u32 fg);
// ...the same two chips when the caller ALREADY measured the label and needs the width BEFORE it
// can place the chip (a centred pill: x depends on w). ui_chip/ui_chip_fill measure internally, so
// the centred pattern "w = ui_text_w(); x = f(w); ui_chip(x)" parses the same string THREE times —
// once to measure, once inside the chip to measure again, once to draw. Passing the width in drops
// that to one parse per draw. `w` is the TOTAL chip width (label + 12 px of padding), i.e. exactly
// what ui_chip returns.
float ui_chip_w     (C2D_TextBuf buf, const char* s, float x, float y, float w, u32 col);
float ui_chip_fill_w(C2D_TextBuf buf, const char* s, float x, float y, float w, u32 bg, u32 fg);
// PHASE 17 / W4.2: the outlined chip when the FRAME and the INK are different roles — the handoff
// fixes the "3D" badge at THEME_3D_TEXT on a THEME_GAME_B frame, and ui_chip's single `col` could
// not express it (sweep D11). ui_chip(…, col) == ui_chip_2(…, col, col), so every old caller is
// unchanged. All four chip helpers now draw the label with the BAKED FNT_JBM_MED at its native
// size instead of the system font at scale 0.32.
float ui_chip_2 (C2D_TextBuf buf, const char* s, float x, float y,          u32 frame, u32 ink);
float ui_chip_2w(C2D_TextBuf buf, const char* s, float x, float y, float w, u32 frame, u32 ink);
// The chip's total width (label + 12 px padding) for callers that must place before they draw.
float ui_chip_measure(C2D_TextBuf buf, const char* s);
// Segmented control hit-test: tap x -> option index (0..n-1). Must stay in lockstep with
// assets_seg's cell arithmetic (the segmented control itself is drawn by assets_seg).
// (Phase 17 OQ4: the call-site-less ui_segmented / ui_toggle / ui_cart were deleted — the app
//  draws those from assets_seg / the baked toggle + cart sprites.)
int  ui_seg_hit(float x, float w, int n, float px);
// Small status dot (HUD/list bullets), 5x5.
void ui_dot(float x, float y, u32 col);
// Filled triangle centered at (cx,cy), half-size s, pointing dir (0=right 1=left 2=up 3=down).
// Font-safe substitute for ▶/◀ (not in the 3DS shared font).
void ui_tri(float cx, float cy, float s, int dir, u32 col);
