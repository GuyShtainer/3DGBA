// ui.c — see ui.h. The shared widget kit for the v2 (1:1) UI pass.
#include "ui.h"
#include "theme.h"

void ui_fill(float x, float y, float w, float h, u32 col, float r) {
	if (r <= 0.0f || w <= 2.0f * r || h <= 2.0f * r) { C2D_DrawRectSolid(x, y, 0.0f, w, h, col); return; }
	C2D_DrawRectSolid(x + r, y, 0.0f, w - 2.0f * r, h, col);           // full-height center
	C2D_DrawRectSolid(x, y + r, 0.0f, r, h - 2.0f * r, col);           // left strip
	C2D_DrawRectSolid(x + w - r, y + r, 0.0f, r, h - 2.0f * r, col);   // right strip
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

float ui_chip_w(C2D_TextBuf buf, const char* s, float x, float y, float w, u32 col) {
	ui_border(x, y, w, 13.0f, col, 1.0f);
	ui_text(buf, s, x + 6.0f, y + 1.5f, 0.32f, col);
	return w;
}

float ui_chip_fill_w(C2D_TextBuf buf, const char* s, float x, float y, float w, u32 bg, u32 fg) {
	ui_fill(x, y, w, 13.0f, bg, 3.0f);
	ui_text(buf, s, x + 6.0f, y + 1.5f, 0.32f, fg);
	return w;
}

float ui_chip(C2D_TextBuf buf, const char* s, float x, float y, u32 col) {
	return ui_chip_w(buf, s, x, y, ui_text_w(buf, s, 0.32f) + 12.0f, col);
}

float ui_chip_fill(C2D_TextBuf buf, const char* s, float x, float y, u32 bg, u32 fg) {
	return ui_chip_fill_w(buf, s, x, y, ui_text_w(buf, s, 0.32f) + 12.0f, bg, fg);
}

void ui_segmented(C2D_TextBuf buf, float x, float y, float w, float h,
                  const char* const* opts, int n, int active, u32 track, u32 acc, u32 ink, u32 dim) {
	ui_fill(x, y, w, h, track, 4.0f);
	float ow = w / (float)n;
	for (int i = 0; i < n; i++) {
		float ox = x + i * ow;
		if (i == active) ui_fill(ox + 2.0f, y + 2.0f, ow - 4.0f, h - 4.0f, acc, 4.0f);
		ui_text_c(buf, opts[i], ox + ow / 2.0f, y + (h - 13.0f) / 2.0f, 0.38f, i == active ? ink : dim);
	}
}

int ui_seg_hit(float x, float w, int n, float px) {
	int i = (int)((px - x) / (w / (float)n));
	if (i < 0) i = 0; if (i >= n) i = n - 1;
	return i;
}

void ui_toggle(float x, float y, int on, u32 acc, u32 off) {
	ui_fill(x, y, 26.0f, 13.0f, on ? acc : off, 5.0f);
	float kx = on ? x + 14.0f : x + 2.0f;
	ui_fill(kx, y + 1.5f, 10.0f, 10.0f, C2D_Color32(0xFF, 0xFF, 0xFF, 0xFF), 4.0f);
}

void ui_cart(float x, float y, u32 col) {
	ui_fill(x, y, 12.0f, 13.0f, col, 2.0f);                                        // shell
	C2D_DrawRectSolid(x + 2.0f, y + 2.0f, 0.0f, 8.0f, 4.0f,                       // label window
	                  C2D_Color32(0xFF, 0xFF, 0xFF, 0x50));
	C2D_DrawRectSolid(x + 2.0f, y + 9.0f, 0.0f, 8.0f, 1.5f,                       // grip line
	                  C2D_Color32(0x00, 0x00, 0x00, 0x50));
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
