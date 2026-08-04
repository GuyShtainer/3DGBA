// assets.c — see assets.h. Loads the embedded t3x plates/widgets + bcfnt fonts.
#include "assets.h"
#include "assets_gen.h"   // ASSET_PLATES / ASSET_WIDGETS X-macro lists (generated)
#include <string.h>
#include <stdio.h>
#include "ui.h"   // ui_border for accent-outline/destructive frames

// The Makefile's bin2s rule turns data/<sym>.bin into: <sym>_bin[] + <sym>_bin_end[].
#define X(id, sym) extern const u8 sym##_bin[]; extern const u8 sym##_bin_end[];
ASSET_PLATES
ASSET_WIDGETS
#undef X
extern const u8 fnt_sg_bold_bin[],  fnt_sg_bold_bin_end[];
extern const u8 fnt_sg_med_bin[],   fnt_sg_med_bin_end[];
extern const u8 fnt_jbm_med_bin[],  fnt_jbm_med_bin_end[];
extern const u8 fnt_jbm_bold_bin[], fnt_jbm_bold_bin_end[];

typedef struct { const char* id; const u8* data; const u8* end; C2D_SpriteSheet ss; } Asset;
static Asset s_plates[] = {
#define X(id, sym) { id, sym##_bin, sym##_bin_end, NULL },
	ASSET_PLATES
#undef X
};
static Asset s_wgts[] = {
#define X(id, sym) { id, sym##_bin, sym##_bin_end, NULL },
	ASSET_WIDGETS
#undef X
};
static const int N_PLATES = (int)(sizeof s_plates / sizeof s_plates[0]);
static const int N_WGTS   = (int)(sizeof s_wgts   / sizeof s_wgts[0]);

static C2D_Font s_fonts[FNT_COUNT];
static float s_native[FNT_COUNT];   // MEASURED px line-height at scale 1.0 (auto, not the bake pt)
static bool s_ready = false;

static void load_group(Asset* a, int n) {
	for (int i = 0; i < n; i++)
		a[i].ss = C2D_SpriteSheetLoadFromMem(a[i].data, (size_t)(a[i].end - a[i].data));
}

bool assets_init(void) {
	load_group(s_plates, N_PLATES);
	load_group(s_wgts, N_WGTS);
	s_fonts[FNT_SG_BOLD]  = C2D_FontLoadFromMem(fnt_sg_bold_bin,  (size_t)(fnt_sg_bold_bin_end  - fnt_sg_bold_bin));
	s_fonts[FNT_SG_MED]   = C2D_FontLoadFromMem(fnt_sg_med_bin,   (size_t)(fnt_sg_med_bin_end   - fnt_sg_med_bin));
	s_fonts[FNT_JBM_MED]  = C2D_FontLoadFromMem(fnt_jbm_med_bin,  (size_t)(fnt_jbm_med_bin_end  - fnt_jbm_med_bin));
	s_fonts[FNT_JBM_BOLD] = C2D_FontLoadFromMem(fnt_jbm_bold_bin, (size_t)(fnt_jbm_bold_bin_end - fnt_jbm_bold_bin));
	// MEASURE each face's native px height at scale 1.0 so assets_text draws near scale 1.0 (crisp,
	// no blurry up/downscaling) regardless of mkbcfnt's pt->px conversion.
	{
		C2D_TextBuf mb = C2D_TextBufNew(64);
		for (int f = 0; f < FNT_COUNT; f++) {
			s_native[f] = 14.0f;
			if (!s_fonts[f]) continue;
			C2D_Text t; C2D_TextFontParse(&t, s_fonts[f], mb, "Ag"); C2D_TextOptimize(&t);
			float w, h; C2D_TextGetDimensions(&t, 1.0f, 1.0f, &w, &h);
			if (h > 1.0f) s_native[f] = h;
			C2D_TextBufClear(mb);
		}
		C2D_TextBufDelete(mb);
	}
	// ready iff at least the splash plates + one font loaded (a torn/absent pack -> code-drawn fallback)
	s_ready = s_plates[0].ss && s_fonts[FNT_SG_BOLD];
	return s_ready;
}
bool assets_ready(void) { return s_ready; }

static C2D_Image find_img(Asset* a, int n, const char* id) {
	for (int i = 0; i < n; i++)
		if (a[i].ss && !strcmp(a[i].id, id)) return C2D_SpriteSheetGetImage(a[i].ss, 0);
	C2D_Image z = { 0 }; return z;
}
C2D_Image assets_plate(const char* id) { return find_img(s_plates, N_PLATES, id); }
C2D_Image assets_wgt(const char* id)   { return find_img(s_wgts,   N_WGTS,   id); }

void assets_draw_plate(const char* id) {
	if (!s_ready) return;
	C2D_Image img = find_img(s_plates, N_PLATES, id);
	if (img.tex) C2D_DrawImageAt(img, 0.0f, 0.0f, 0.0f, NULL, 1.0f, 1.0f);
}
void assets_draw_wgt(const char* id, float x, float y) {
	C2D_Image img = find_img(s_wgts, N_WGTS, id);
	if (img.tex) C2D_DrawImageAt(img, x, y, 0.0f, NULL, 1.0f, 1.0f);
}
void assets_draw_wgt_fit(const char* id, float x, float y, float w, float h) {
	C2D_Image img = find_img(s_wgts, N_WGTS, id);
	if (!img.tex || !img.subtex) return;
	float sx = w / (float)img.subtex->width, sy = h / (float)img.subtex->height;
	C2D_DrawImageAt(img, x, y, 0.0f, NULL, sx, sy);
}

C2D_Font assets_font(AFont f) { return (f >= 0 && f < FNT_COUNT) ? s_fonts[f] : NULL; }

void assets_text(C2D_TextBuf buf, AFont f, const char* s, float x, float y, float px, u32 col) {
	if (!s_ready || f < 0 || f >= FNT_COUNT || !s_fonts[f]) return;
	float sc = px / s_native[f];
	C2D_Text t; C2D_TextFontParse(&t, s_fonts[f], buf, s); C2D_TextOptimize(&t);
	C2D_DrawText(&t, C2D_WithColor, x, y, 0.0f, sc, sc, col);
}
float assets_text_w(C2D_TextBuf buf, AFont f, const char* s, float px) {
	if (f < 0 || f >= FNT_COUNT || !s_fonts[f]) return 0.0f;
	float sc = px / s_native[f];
	C2D_Text t; C2D_TextFontParse(&t, s_fonts[f], buf, s); C2D_TextOptimize(&t);
	float w, h; C2D_TextGetDimensions(&t, sc, sc, &w, &h); return w;
}
void assets_text_c(C2D_TextBuf buf, AFont f, const char* s, float cx, float y, float px, u32 col) {
	assets_text(buf, f, s, cx - assets_text_w(buf, f, s, px) / 2.0f, y, px, col);
}
void assets_text_r(C2D_TextBuf buf, AFont f, const char* s, float rx, float y, float px, u32 col) {
	assets_text(buf, f, s, rx - assets_text_w(buf, f, s, px), y, px, col);
}


// ---- 9-slice: draw a sprite as a stretchable panel, keeping its `r`-px rounded corners crisp ----
static void img_subrect(C2D_Image src, float px, float py, float pw, float ph,
                        C2D_Image* out, Tex3DS_SubTexture* st) {
	const Tex3DS_SubTexture* s = src.subtex;
	float uw = s->right - s->left, vh = s->bottom - s->top;   // works whether top<bottom or not
	st->width = (u16)(pw + 0.5f); st->height = (u16)(ph + 0.5f);
	st->left   = s->left + uw * (px / (float)s->width);
	st->right  = s->left + uw * ((px + pw) / (float)s->width);
	st->top    = s->top  + vh * (py / (float)s->height);
	st->bottom = s->top  + vh * ((py + ph) / (float)s->height);
	out->tex = src.tex; out->subtex = st;
}
static void draw_9slice(C2D_Image img, float x, float y, float w, float h, float r) {
	if (!img.tex || !img.subtex) return;
	float sw = (float)img.subtex->width, sh = (float)img.subtex->height;
	if (r > sw / 2.0f) r = sw / 2.0f;
	if (r > sh / 2.0f) r = sh / 2.0f;
	if (w < 2.0f * r) w = 2.0f * r;
	if (h < 2.0f * r) h = 2.0f * r;
	float smw = sw - 2.0f * r, smh = sh - 2.0f * r, mw = w - 2.0f * r, mh = h - 2.0f * r;
	const float S[9][8] = {   // {srcx,srcy,srcw,srch, dstx,dsty,dstw,dsth}
		{0,0,r,r,               0,0,r,r},               {sw-r,0,r,r,          w-r,0,r,r},
		{0,sh-r,r,r,            0,h-r,r,r},             {sw-r,sh-r,r,r,       w-r,h-r,r,r},
		{r,0,smw,r,             r,0,mw,r},              {r,sh-r,smw,r,        r,h-r,mw,r},
		{0,r,r,smh,             0,r,r,mh},              {sw-r,r,r,smh,        w-r,r,r,mh},
		{r,r,smw,smh,           r,r,mw,mh},
	};
	for (int i = 0; i < 9; i++) {
		float sxw = S[i][2], sxh = S[i][3], dw = S[i][6], dh = S[i][7];
		if (sxw <= 0 || sxh <= 0 || dw <= 0 || dh <= 0) continue;
		C2D_Image sub; Tex3DS_SubTexture st;
		img_subrect(img, S[i][0], S[i][1], sxw, sxh, &sub, &st);
		C2D_DrawImageAt(sub, x + S[i][4], y + S[i][5], 0.0f, NULL, dw / sxw, dh / sxh);
	}
}
void assets_fill9(const char* id, float x, float y, float w, float h, float r) {
	draw_9slice(assets_wgt(id), x, y, w, h, r);
}

// Phase 15 (SPEC-avatar A1.4.3): the same arithmetic, made public for the co-op avatar's sprite
// sheet instead of copied. img_subrect stays static and this is its only wrapper, so there is one
// implementation of "a sub-rect of a sheet" in the binary.
bool assets_img_cell(C2D_Image src, float px, float py, float pw, float ph,
                     C2D_Image* out, Tex3DS_SubTexture* st) {
	if (!out || !st) return false;
	if (!src.tex || !src.subtex) { C2D_Image z = { 0 }; *out = z; return false; }
	img_subrect(src, px, py, pw, ph, out, st);
	return true;
}

// Horizontal 3-slice for PILLS (rounded left/right ends, straight top/bottom): left cap + stretched
// middle + right cap, each at FULL height. Avoids the vertical seam a 9-slice makes when r == h/2.
static void draw_hslice(C2D_Image img, float x, float y, float w, float h, float cap) {
	if (!img.tex || !img.subtex) return;
	float sw = (float)img.subtex->width, sh = (float)img.subtex->height;
	if (cap > sw / 2.0f) cap = sw / 2.0f;
	if (w < 2.0f * cap) w = 2.0f * cap;
	float smw = sw - 2.0f * cap, mw = w - 2.0f * cap;
	C2D_Image sub; Tex3DS_SubTexture st;
	img_subrect(img, 0, 0, cap, sh, &sub, &st);        C2D_DrawImageAt(sub, x, y, 0.0f, NULL, 1.0f, h / sh);
	if (smw > 0 && mw > 0) { img_subrect(img, cap, 0, smw, sh, &sub, &st);
	                         C2D_DrawImageAt(sub, x + cap, y, 0.0f, NULL, mw / smw, h / sh); }
	img_subrect(img, sw - cap, 0, cap, sh, &sub, &st); C2D_DrawImageAt(sub, x + w - cap, y, 0.0f, NULL, 1.0f, h / sh);
}

// Button backgrounds: solid roles 9-slice from the fill-*-r8 sprites (crisp corners at any width);
// ghost = transparent; accent-outline/destructive keep their thin accent/red frame (drawn by caller).
void assets_button(C2D_TextBuf buf, const char* sprite, float x, float y, float w, float h,
                   const char* label, AFont f, float px, u32 col, int focus) {
	if (!strcmp(sprite, "btn-primary"))        assets_fill9("fill-primary-r8", x, y, w, h, 8.0f);
	else if (!strcmp(sprite, "btn-secondary")) assets_fill9("fill-secondary-r8", x, y, w, h, 8.0f);
	else if (!strcmp(sprite, "btn-ghost")) { /* transparent */ }
	else if (!strcmp(sprite, "btn-accent-outline") || !strcmp(sprite, "btn-destructive")) {
		assets_fill9("fill-card-r8", x, y, w, h, 8.0f);   // faint card base
		ui_border(x, y, w, h, col, 1.5f);                  // + a 1.5px accent/red frame
	}
	else assets_draw_wgt_fit(sprite, x, y, w, h);
	if (focus) ui_border(x, y, w, h, C2D_Color32(0xFF,0xFF,0xFF,0xE0), 1.5f);
	if (label && label[0]) assets_text_c(buf, f, label, x + w / 2.0f, y + (h - px) / 2.0f - 0.5f, px, col);
}

void assets_seg(C2D_TextBuf buf, float x, float y, float w, float h,
                const char* const* opts, int n, int active, u32 inkA, u32 dim) {
	draw_hslice(assets_wgt("seg-track"), x, y, w, h, h / 2.0f);   // pill: horizontal 3-slice, no seam
	float ow = w / (float)n;
	if (active >= 0 && active < n)
		draw_hslice(assets_wgt("seg-active"), x + active * ow + 2.0f, y + 2.0f, ow - 4.0f, h - 4.0f, (h - 4.0f) / 2.0f);
	for (int i = 0; i < n; i++)
		assets_text_c(buf, FNT_SG_MED, opts[i], x + i * ow + ow / 2.0f, y + h / 2.0f - 5.0f,
		              10.0f, i == active ? inkA : dim);
}

void assets_toggle(int on, float x, float y) { assets_draw_wgt(on ? "toggle-on" : "toggle-off", x, y); }
