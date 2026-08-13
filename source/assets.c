// assets.c — see assets.h. Loads the embedded t3x plates/widgets + bcfnt fonts.
#include "assets.h"
#include "assets_gen.h"   // ASSET_PLATES / ASSET_WIDGETS X-macro lists (generated)
#include <string.h>
#include <stdio.h>
#include <math.h>    // floorf — SPEC-crisp C4 integer draw origins
#include "ui.h"      // ui_border for accent-outline/destructive frames, ui_fill for the segmented control
#include "uigeom.h"  // ui_seg_radius — the pill radius ladder measured off the design art
#include "theme.h"   // g_ui: the segmented control is drawn from the ACTIVE theme, not baked art

// The Makefile's bin2s rule turns data/<sym>.bin into: <sym>_bin[] + <sym>_bin_end[].
#define X(id, sym) extern const u8 sym##_bin[]; extern const u8 sym##_bin_end[];
ASSET_PLATES
ASSET_WIDGETS
#undef X
// PHASE 18 / SPEC-crisp: one bcfnt per ladder rung, each baked with lineFeed == its draw px.
// Order MUST match TxtRole in typography.h (asserted at load, see assets_init).
//
// PHASE 19 / SPEC-legible L2.3: two rungs are now ALIASES — TXT_SEG draws the BODY face and
// TXT_CHIP draws the SECTION face — so the same _bin symbol appears on two rows. That is
// deliberate (it is what removes two ~527 KB faces), and it makes de-duplication MANDATORY at
// load: C2D_FontLoadFromMem does linearAlloc(size) + memcpy of the WHOLE .bcfnt (disassembled
// from the shipped libcitro2d.a font.o), so loading a symbol twice burns another ~527 KB of
// linear heap in an app that already carries two GBA cores and eight render targets. The loop
// below loads each DISTINCT symbol once and points both role slots at the one handle.
#define ASSET_FONTS \
	X(TXT_TITLE,   fnt_sg_bold_19)  \
	X(TXT_BUTTON,  fnt_sg_bold_15)  \
	X(TXT_BODY,    fnt_sg_med_15)   \
	X(TXT_SEG,     fnt_sg_med_15)   \
	X(TXT_SECTION, fnt_jbm_med_12)  \
	X(TXT_CHIP,    fnt_jbm_med_12)  \
	X(TXT_VALUE,   fnt_jbm_bold_12)
#define X(role, sym) extern const u8 sym##_bin[]; extern const u8 sym##_bin_end[];
ASSET_FONTS
#undef X

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

static C2D_Font s_fonts[TXT_COUNT];
// Each face's own FINF.lineFeed / TGLP.cellHeight, read out of the LOADED font (not assumed from
// the bake script), so the draw scale is derived from the bytes that actually shipped.
static int  s_lineFeed[TXT_COUNT];
static int  s_cellH[TXT_COUNT];
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
	// PHASE 19: load each DISTINCT face once (see ASSET_FONTS above). The identity test is the
	// _bin symbol's ADDRESS, not a strcmp of typo_face().sym: it is the thing that would actually
	// be loaded twice, it costs nothing, and it cannot drift from the X-macro the way a parallel
	// name table could. Seven rows, five loads.
	{
		static const struct { const u8* data; const u8* end; } FSRC[TXT_COUNT] = {
#define X(role, sym) [role] = { sym##_bin, sym##_bin_end },
			ASSET_FONTS
#undef X
		};
		for (int f = 0; f < TXT_COUNT; f++) {
			s_fonts[f] = NULL;
			for (int g = 0; g < f; g++)
				if (FSRC[g].data == FSRC[f].data) { s_fonts[f] = s_fonts[g]; break; }   // alias
			if (!s_fonts[f])
				s_fonts[f] = C2D_FontLoadFromMem(FSRC[f].data, (size_t)(FSRC[f].end - FSRC[f].data));
		}
	}
	// PHASE 18 / SPEC-crisp C2.1.1 + C3.1. Two things per face, both load-time, both one-shot:
	//
	//  (1) The draw scale is DERIVED from the face's own FINF/TGLP, not measured through
	//      citro2d. C2D_TextGetDimensions(1.0) returns ceil(lineFeed*30/cellH) — citro2d's
	//      NORMALISED height, ~26 for every face whatever its bake — so the phase-16/17 "measure
	//      the native size" recipe reported 26/27/24/24 for faces whose lineFeeds were 19/17/12/14
	//      and every draw came out at 0.58x-1.05x. typo_draw_scale(px, lineFeed, cellH) makes the
	//      texel scale exactly px/lineFeed, which the ladder pins at 1.0.
	//
	//  (2) NEAREST on the glyph sheets. citro2d writes param 0x1106 (MAG|MIN = GPU_LINEAR) into
	//      every sheet in C2Di_PostLoadFont, so the filter is the font's own, not inherited from
	//      the game blit — and C2D_FontSetFilter is the only supported way to change it. At
	//      texel scale 1.0 NEAREST and LINEAR are BIT-IDENTICAL (the bilinear taps collapse onto
	//      texel centres), so this line does not improve today's pixels: it is a tripwire that
	//      makes any future off-1.0 draw look obviously wrong instead of quietly soft. Do not
	//      "clean it up", and never land it without (1) — NEAREST at 0.67x drops one texel row
	//      in three and shatters a 9 px glyph.
	for (int f = 0; f < TXT_COUNT; f++) {
		s_lineFeed[f] = 0; s_cellH[f] = 0;           // 0/0 => typo_draw_scale returns 1.0
		if (!s_fonts[f]) continue;
		C2D_FontSetFilter(s_fonts[f], GPU_NEAREST, GPU_NEAREST);
		FINF_s* fi = C2D_FontGetInfo(s_fonts[f]);
		if (fi && fi->tglp && fi->tglp->cellHeight) {
			s_lineFeed[f] = fi->lineFeed;
			s_cellH[f]    = fi->tglp->cellHeight;
		}
	}
	// ready iff at least the splash plates + one font loaded (a torn/absent pack -> code-drawn fallback)
	s_ready = s_plates[0].ss && s_fonts[TXT_TITLE];
	return s_ready;
}

// Debug/self-check surface for the harness: the texel scale a role actually draws at, read live
// over gdb. R1 says every one of these is 1.0. (Costs 7 floats; it is the only way to prove the
// law holds in the SHIPPED binary rather than in the bake.)
float g_txtTexelScale[TXT_COUNT];
int   g_txtLineFeed[TXT_COUNT];
void assets_dbg_publish_scales(void) {
	for (int f = 0; f < TXT_COUNT; f++) {
		g_txtLineFeed[f]   = s_lineFeed[f];
		g_txtTexelScale[f] = s_cellH[f]
			? typo_texel_scale(typo_role_px((TxtRole)f), s_lineFeed[f], s_cellH[f]) : 0.0f;
	}
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

C2D_Font assets_font(TxtRole r) { return (r >= 0 && r < TXT_COUNT) ? s_fonts[r] : NULL; }

void assets_text(C2D_TextBuf buf, TxtRole r, const char* s, float x, float y, u32 col) {
	if (!s_ready || r < 0 || r >= TXT_COUNT || !s_fonts[r]) return;
	float sc = typo_draw_scale(typo_role_px(r), s_lineFeed[r], s_cellH[r]);
	// SPEC-crisp C4: SNAP THE ORIGIN. A glyph quad starting at x=12.5 is sampled halfway
	// between two texel columns, and a half-pixel origin at perfect scale measured as blurry
	// as the 0.71x downscale we just removed (22-32 grey levels vs 14). Rounding here rather
	// than at the call sites means _c/_r inherit it and no caller can opt out. Once scale is
	// 1.0 the per-glyph advances are whole numbers too (integer charWidth x 1.0), so one
	// rounded origin keeps the entire run on the pixel grid — there is nothing to accumulate.
	x = floorf(x + 0.5f);
	y = floorf(y + 0.5f);
	C2D_Text t; C2D_TextFontParse(&t, s_fonts[r], buf, s); C2D_TextOptimize(&t);
	C2D_DrawText(&t, C2D_WithColor, x, y, 0.0f, sc, sc, col);
}
float assets_text_w(C2D_TextBuf buf, TxtRole r, const char* s) {
	if (r < 0 || r >= TXT_COUNT || !s_fonts[r]) return 0.0f;
	float sc = typo_draw_scale(typo_role_px(r), s_lineFeed[r], s_cellH[r]);
	C2D_Text t; C2D_TextFontParse(&t, s_fonts[r], buf, s); C2D_TextOptimize(&t);
	float w, h; C2D_TextGetDimensions(&t, sc, sc, &w, &h); return w;
}
// C4.2: the measured width is NOT rounded — it also drives layout flow (the HUD's right-to-left
// run, uihit_wrap), where rounding every step would accumulate drift. Only the final origin snaps,
// which assets_text does for us.
void assets_text_c(C2D_TextBuf buf, TxtRole r, const char* s, float cx, float y, u32 col) {
	assets_text(buf, r, s, cx - assets_text_w(buf, r, s) / 2.0f, y, col);
}
void assets_text_r(C2D_TextBuf buf, TxtRole r, const char* s, float rx, float y, u32 col) {
	assets_text(buf, r, s, rx - assets_text_w(buf, r, s), y, col);
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
                   const char* label, TxtRole r, u32 col, int focus) {
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
	// Phase 18 C4.3: the `- 0.5f` nudge is GONE. It was there to fight the blur this phase
	// removed (a resampled glyph looked low, so someone lifted it half a pixel); with the line
	// box exactly typo_role_px(r) tall, (h - px)/2 is the true optical centre and assets_text
	// snaps it to the grid.
	// PHASE 19 / SPEC-legible L3.2.7: centre the INK box, not the line box. `(h - px)/2` put a
	// cap-9 label ~4 px low in a 40 px button (the face reserves more room under the baseline
	// than above the cap), which reads as "the label is falling out of the bottom" at the sizes
	// this phase ships. typo_center_y is the single implementation; it rounds, so R1's integer
	// origin still holds for the `_c` path.
	if (label && label[0])
		assets_text_c(buf, r, label, x + w / 2.0f, typo_center_y(r, y, h), col);
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
	// TXT_SEG is an ALIAS of TXT_BODY since phase 19 (typography.h): once every rung grew, the
	// segmented cells were no longer the tightest boxes in the app and a separate 527 KB face
	// bought nothing. Vertical placement is typo_center_y — the ink box centred in the cell
	// (L3.2.7), not the line box, which sat 4 px low in a 30 px seg.
	for (int i = 0; i < n; i++)
		assets_text_c(buf, TXT_SEG, opts[i], x + i * ow + ow / 2.0f,
		              typo_center_y(TXT_SEG, y, h), i == active ? inkA : dim);
}

void assets_toggle(int on, float x, float y) { assets_draw_wgt(on ? "toggle-on" : "toggle-off", x, y); }
