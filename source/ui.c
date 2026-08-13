// ui.c — see ui.h. The shared widget kit for the v2 (1:1) UI pass.
#include "ui.h"
#include "theme.h"
#include "uigeom.h"
#include "assets.h"   // W4.2 (OQ5, "add the include"): chip labels use the BAKED font at its native
                      // size — the system font at scale 0.32 is what made the "3D" badge a smudge.

// Phase 17 (SPEC-widgets W2): the shape DECISION lives in the pure-C uigeom module (host-tested by
// test/host/test_uigeom.c); this file only blits it. The old three-rect approximation was
// "rectangle minus four SQUARE corners" and degenerated into a literal plus/cross whenever
// r -> min(w,h)/2 — the HUD dot, the pause chip row and the gamepad swatches (sweep D6).
// See ui.h — the app-wide "a UI frame was drawn" counter the emutest harness polls for liveness.
volatile unsigned int g_renderSeq = 0;

void ui_fill(float x, float y, float w, float h, u32 col, float r) {
	UiQuad q[UI_ROUND_MAX_QUADS];
	int n = ui_round_rect_quads(x, y, w, h, r, q, UI_ROUND_MAX_QUADS);
	for (int i = 0; i < n; i++) C2D_DrawRectSolid(q[i].x, q[i].y, 0.0f, q[i].w, q[i].h, col);
}

void ui_border_round(float x, float y, float w, float h, u32 col, float t, float r) {
	UiQuad q[UI_OUTLINE_MAX_QUADS];
	int n = ui_round_outline_quads(x, y, w, h, r, t, q, UI_OUTLINE_MAX_QUADS);
	for (int i = 0; i < n; i++) C2D_DrawRectSolid(q[i].x, q[i].y, 0.0f, q[i].w, q[i].h, col);
}

void ui_border(float x, float y, float w, float h, u32 col, float t) {
	C2D_DrawRectSolid(x, y, 0.0f, w, t, col);                  // top
	C2D_DrawRectSolid(x, y + h - t, 0.0f, w, t, col);          // bottom
	C2D_DrawRectSolid(x, y + t, 0.0f, t, h - 2.0f * t, col);   // left
	C2D_DrawRectSolid(x + w - t, y + t, 0.0f, t, h - 2.0f * t, col);   // right
}

void ui_panel(float x, float y, float w, float h, u32 fill, u32 borderCol, float r) {
	if (fill) ui_fill(x, y, w, h, fill, r);
	ui_border(x, y, w, h, borderCol, 1.5f);
}

void ui_text(C2D_TextBuf buf, const char* s, float x, float y, float sz, u32 col) {
	C2D_Text t; C2D_TextParse(&t, buf, s); C2D_TextOptimize(&t);
	C2D_DrawText(&t, C2D_WithColor, x, y, 0.0f, sz, sz, col);
}

float ui_text_w(C2D_TextBuf buf, const char* s, float sz) {
	C2D_Text t; C2D_TextParse(&t, buf, s); C2D_TextOptimize(&t);
	float w, h; C2D_TextGetDimensions(&t, sz, sz, &w, &h);
	return w;
}

void ui_text_c(C2D_TextBuf buf, const char* s, float cx, float y, float sz, u32 col) {
	C2D_Text t; C2D_TextParse(&t, buf, s); C2D_TextOptimize(&t);
	float w, h; C2D_TextGetDimensions(&t, sz, sz, &w, &h);
	C2D_DrawText(&t, C2D_WithColor, cx - w / 2.0f, y, 0.0f, sz, sz, col);
}

void ui_text_r(C2D_TextBuf buf, const char* s, float rx, float y, float sz, u32 col) {
	C2D_Text t; C2D_TextParse(&t, buf, s); C2D_TextOptimize(&t);
	float w, h; C2D_TextGetDimensions(&t, sz, sz, &w, &h);
	C2D_DrawText(&t, C2D_WithColor, rx - w, y, 0.0f, sz, sz, col);
}

// PHASE 17 / SPEC-widgets W4.2 (sweep D11, and D19's HUD half). Two things were wrong with every
// chip label, and only both together explain the unreadable "3D" badge:
//   (a) the FRAME and the INK were one colour. The handoff fixes the 3D badge at THEME_3D_TEXT
//       (#a9d4ff) ON THEME_GAME_B (#3E86D6) — the constant existed in theme.h and was never used,
//       so the badge drew #3E86D6 glyphs on the near-black HUD bar.
//   (b) the label was the SYSTEM font at scale 0.32. A stroke then covers about a third of a device
//       pixel and the rasteriser blends the ink toward the background: measured on the sweep frame,
//       the brightest "3D" pixels were (26,40,65)…(51,83,130) over a bar at (6,5,10) = 1.4:1…2.8:1,
//       while the nominal colour pair is 5.6:1. A naive contrast check PASSES an unreadable screen;
//       the baked TXT_CHIP face draws at texel scale EXACTLY 1.0 (phase 18 / SPEC-crisp R1: the
//       face's lineFeed IS its draw px), so coverage is 1.0 and the nominal contrast is the real
//       one. NB phase 17 asserted this via the learn skill's design-handoff invariant 4, whose
//       measurement recipe was wrong — the real scale then was 0.667x. See typography.h.
// The frame also becomes ui_border_round (art `chip-focus` measures r=3), matching the design's
// rounded chips instead of the square 4-strip outline.
#define UI_CHIP_H  13.0f   // the chip box; its label is TXT_CHIP (7 px, typography.h)

// One measurement for the chip's TOTAL width (label + 12 px padding) — the same number ui_chip*
// use internally, exposed because a centred/right-flowed chip needs its width before it can be
// placed (main.c's HUD flows right-to-left). Falls back to the system font if the pack is absent.
float ui_chip_measure(C2D_TextBuf buf, const char* s) {
	float w = assets_ready() ? assets_text_w(buf, TXT_CHIP, s) : 0.0f;
	if (w <= 0.0f) w = ui_text_w(buf, s, 0.32f);
	return w + 12.0f;
}

static void chip_label(C2D_TextBuf buf, const char* s, float x, float y, u32 ink) {
	if (assets_ready()) assets_text(buf, TXT_CHIP, s, x, y, ink);
	else                ui_text(buf, s, x, y, 0.32f, ink);
}

float ui_chip_2w(C2D_TextBuf buf, const char* s, float x, float y, float w, u32 frame, u32 ink) {
	ui_border_round(x, y, w, UI_CHIP_H, frame, 1.0f, 3.0f);
	chip_label(buf, s, x + 6.0f, y + 2.0f, ink);
	return w;
}

float ui_chip_2(C2D_TextBuf buf, const char* s, float x, float y, u32 frame, u32 ink) {
	return ui_chip_2w(buf, s, x, y, ui_chip_measure(buf, s), frame, ink);
}

float ui_chip_w(C2D_TextBuf buf, const char* s, float x, float y, float w, u32 col) {
	return ui_chip_2w(buf, s, x, y, w, col, col);
}

float ui_chip_fill_w(C2D_TextBuf buf, const char* s, float x, float y, float w, u32 bg, u32 fg) {
	ui_fill(x, y, w, UI_CHIP_H, bg, 3.0f);
	chip_label(buf, s, x + 6.0f, y + 2.0f, fg);
	return w;
}

float ui_chip(C2D_TextBuf buf, const char* s, float x, float y, u32 col) {
	return ui_chip_2w(buf, s, x, y, ui_chip_measure(buf, s), col, col);
}

float ui_chip_fill(C2D_TextBuf buf, const char* s, float x, float y, u32 bg, u32 fg) {
	return ui_chip_fill_w(buf, s, x, y, ui_chip_measure(buf, s), bg, fg);
}

// Phase 17 (SPEC-widgets OQ4): ui_segmented / ui_toggle / ui_cart had NO call sites — the
// on-screen segmented control is assets_seg, the toggles are the baked art sprites and the cart
// glyph is the tinted `cart-*` sprite. They are deleted rather than carried (and re-fixed) as
// three more ways to reintroduce the plus/cross. ui_seg_hit stays: main.c hit-tests PK_SEG rows
// with it, and it must keep matching assets_seg's cell arithmetic exactly.
int ui_seg_hit(float x, float w, int n, float px) {
	int i = (int)((px - x) / (w / (float)n));
	if (i < 0) i = 0; if (i >= n) i = n - 1;
	return i;
}

void ui_dot(float x, float y, u32 col) {
	ui_fill(x, y, 5.0f, 5.0f, col, 2.0f);
}

void ui_tri(float cx, float cy, float s, int dir, u32 col) {
	float ax, ay, bx, by, cx2, cy2;
	switch (dir) {
	case 1:  ax = cx + s; ay = cy - s; bx = cx + s; by = cy + s; cx2 = cx - s; cy2 = cy; break;   // left
	case 2:  ax = cx - s; ay = cy + s; bx = cx + s; by = cy + s; cx2 = cx; cy2 = cy - s; break;   // up
	case 3:  ax = cx - s; ay = cy - s; bx = cx + s; by = cy - s; cx2 = cx; cy2 = cy + s; break;   // down
	default: ax = cx - s; ay = cy - s; bx = cx - s; by = cy + s; cx2 = cx + s; cy2 = cy; break;   // right
	}
	C2D_DrawTriangle(ax, ay, col, bx, by, col, cx2, cy2, col, 0.0f);
}
