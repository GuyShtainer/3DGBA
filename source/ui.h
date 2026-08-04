// ui.h — the shared citro2d widget kit for the 3DGBA UI redesign (v2, 1:1 pass).
// One drawing language for main.c (HUD/pause/splash), rompicker.c, wireless.c and touch.c:
// fake-rounded fills, 1.5px borders, outlined chips, segmented pills, toggles, cartridge
// glyphs, and alignment helpers. Everything draws to the CURRENTLY BOUND C2D scene.
#pragma once
#include <citro2d.h>

// ---- primitives ----
// Fake-rounded filled rect (three non-overlapping rects; r = corner cut, safe for alpha).
void ui_fill(float x, float y, float w, float h, u32 col, float r);
// 4-strip outline, `t` thick, drawn INSIDE the rect. Square corners (citro2d has no arcs).
void ui_border(float x, float y, float w, float h, u32 col, float t);
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
// Segmented control: track panel + n options; the active one is an acc-filled pill.
// Returns nothing; hit-test with ui_seg_hit using the same geometry.
void ui_segmented(C2D_TextBuf buf, float x, float y, float w, float h,
                  const char* const* opts, int n, int active, u32 track, u32 acc, u32 ink, u32 dim);
int  ui_seg_hit(float x, float w, int n, float px);   // tap x -> option index (0..n-1)
// Toggle switch, 26x13: track acc when on / `off` when off, white knob.
void ui_toggle(float x, float y, int on, u32 acc, u32 off);
// Tiny cartridge glyph (game chip in lists), ~12x13 at (x,y), tinted `col`.
void ui_cart(float x, float y, u32 col);
// Small status dot (HUD/list bullets), 5x5.
void ui_dot(float x, float y, u32 col);
// Filled triangle centered at (cx,cy), half-size s, pointing dir (0=right 1=left 2=up 3=down).
// Font-safe substitute for ▶/◀ (not in the 3DS shared font).
void ui_tri(float cx, float cy, float s, int dir, u32 col);
