// assets.c — see assets.h. Loads the embedded t3x plates/widgets + bcfnt fonts.
#include "assets.h"
#include "assets_gen.h"   // ASSET_PLATES / ASSET_WIDGETS X-macro lists (generated)
#include <string.h>
#include <stdio.h>
#include "ui.h"      // ui_border for accent-outline/destructive frames, ui_fill for the segmented control
#include "uigeom.h"  // ui_seg_radius — the pill radius ladder measured off the design art
#include "theme.h"   // g_ui: the segmented control is drawn from the ACTIVE theme, not baked art

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

// Belt and braces: an assets_gen.h generated before phase 17 has no stamp. Indigo is what
// build_assets.sh has always defaulted to, so the fallback is the truth for every existing tree.
#ifndef ASSET_THEME_ID
#define ASSET_THEME_ID 0
#endif

bool assets_init(void) {
	// W4.3.a: publish the palette the embedded art was baked from BEFORE anything can draw, so ink
	// on a baked surface never comes from a different theme than the surface (sweep D10/D12).
	theme_init_art(ASSET_THEME_ID);
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

// (Phase 17 SPEC-widgets W1: draw_hslice — the horizontal 3-slice that stretched `seg-track` and
//  `seg-active` — is GONE. It had exactly one caller, assets_seg, and `seg-active.png` is the
//  design pack's EXAMPLE pill with the word "Aspect-fit" rendered into its pixels, so every
//  selected segment in the app printed that word under its live label, resampled to the call
//  site's box (2.46x stretched on the picker, 0.40x squashed on the EDGES row). Sweep defect D1.
//  The control is now drawn procedurally from the active theme — see assets_seg below. Deleting
//  the helper as well is deliberate: a dead sprite path is a re-entry point for the same bug.)

// Button backgrounds: solid roles 9-slice from the fill-*-r8 sprites (crisp corners at any width);
// ghost = transparent; accent-outline/destructive keep their thin accent/red frame (drawn by caller).
void assets_button(C2D_TextBuf buf, const char* sprite, float x, float y, float w, float h,
                   const char* label, AFont f, float px, u32 col, int focus) {
	if (!strcmp(sprite, "btn-primary"))        assets_fill9("fill-primary-r8", x, y, w, h, 8.0f);
	else if (!strcmp(sprite, "btn-secondary")) assets_fill9("fill-secondary-r8", x, y, w, h, 8.0f);
	else if (!strcmp(sprite, "btn-ghost")) { /* transparent */ }
	else if (!strcmp(sprite, "btn-accent-outline") || !strcmp(sprite, "btn-destructive")) {
		assets_fill9("fill-card-r8", x, y, w, h, 8.0f);   // faint card base
		ui_border_round(x, y, w, h, col, 1.5f, ASSETS_BTN_R);   // + a 1.5px accent/red frame
	}
	else assets_draw_wgt_fit(sprite, x, y, w, h);
	// FIX PASS (review findings 1 + 8): the ring FOLLOWS the button's silhouette. Every body above
	// is a `fill-*-r8` 9-slice with r=8, so a square `ui_border` ring left a white right-angle
	// sticking out at each corner over the plate (SEEN in runs/p17-f5-empty/bottom_00040.png on the
	// focused "Rescan"). touch.c:114 and rompicker.c's focus_ring() already round for this reason;
	// the sprite-backed buttons are the majority of the d-pad chain's stops and were the last
	// square idiom left. ASSETS_BTN_R is the 9-slice's own radius, so the two cannot drift.
	if (focus) ui_border_round(x, y, w, h, C2D_Color32(0xFF,0xFF,0xFF,0xE0), 1.5f, ASSETS_BTN_R);
	if (label && label[0]) assets_text_c(buf, f, label, x + w / 2.0f, y + (h - px) / 2.0f - 0.5f, px, col);
}

// Segmented control, drawn procedurally (W1.2). The shipped art is a flat fill of the theme token
// plus a rounded outline plus the example word, so `g_ui.panel` track + `g_ui.acc` pill is
// pixel-equal to the sprite in all five fixed themes — and MORE correct for `custom`, whose baked
// teal cannot follow the user's hues. It also follows the active theme even though only one art
// pack is baked into data/, which is why the sprite route could never satisfy "all six themes".
void assets_seg(C2D_TextBuf buf, float x, float y, float w, float h,
                const char* const* opts, int n, int active, u32 inkA, u32 dim) {
	// FIX PASS (review finding 9). The TRACK is a SURFACE that sits directly on a baked plate, so
	// theme.h's rule ("baked-art surface -> g_art") governs it, not the active theme: with g_ui.panel
	// the Daylight track measured pure #FFFFFF on the indigo plate's (31,24,46) — a white slab, the
	// most visible instance of the one-art-pack gap the phase set out to remove. g_art.panel is
	// IDENTICAL to g_ui.panel on the shipped Indigo theme (so the default look is byte-for-byte
	// unchanged) and surface-consistent on the other five. The `active` pill deliberately stays
	// g_ui.acc — theme.h exempts the accent, and while one art pack ships it is the only thing on
	// screen that says a theme was chosen at all.
	ui_fill(x, y, w, h, g_art.panel, ui_seg_radius(h));
	float ow = w / (float)n;
	if (active >= 0 && active < n)
		ui_fill(x + active * ow + 2.0f, y + 2.0f, ow - 4.0f, h - 4.0f, g_ui.acc, ui_seg_radius(h - 4.0f));
	for (int i = 0; i < n; i++)
		assets_text_c(buf, FNT_SG_MED, opts[i], x + i * ow + ow / 2.0f, y + h / 2.0f - 5.0f,
		              10.0f, i == active ? inkA : dim);
}

void assets_toggle(int on, float x, float y) { assets_draw_wgt(on ? "toggle-on" : "toggle-off", x, y); }
