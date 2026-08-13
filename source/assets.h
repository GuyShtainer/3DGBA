// assets.h — device-native art pack loader (design_handoff_3dgba_ui/assets_3ds).
// Plates (chrome PNGs) + widgets (sprites) are baked to t3x and the fonts to bcfnt by
// tools/build_assets.sh, embedded into the ELF via data/*.bin, and loaded here. Draw model
// per frame/screen: (1) game video into the video rect, (2) the chrome plate, (3) dynamic
// text + widgets from the manifest on top. See assets_3ds/README-IMPLEMENTATION.md.
#pragma once
#include <citro2d.h>
// PHASE 18 / SPEC-crisp: the type ladder lives in typography.h (pure C, host-tested). Text is
// addressed by ROLE — TXT_TITLE / TXT_BUTTON / TXT_BODY / TXT_SEG / TXT_SECTION / TXT_CHIP /
// TXT_VALUE — and the role carries its own size, so there is no `px` argument to get wrong.
// Every role's face is baked with lineFeed == its draw px, i.e. every glyph draws at texel
// scale exactly 1.0. Read typography.h before adding a size.
#include "typography.h"

// Load every plate/widget/font. Call once after C2D_Init(). Returns false if the pack is
// missing (data/ not built) so callers can fall back to the code-drawn UI.
bool assets_init(void);
bool assets_ready(void);

// Plate/widget by its manifest id ("splash-top", "btn-primary", ...). If absent, the returned
// image has tex==NULL (drawing it is a safe no-op) — always guard with assets_ready().
C2D_Image assets_plate(const char* id);
C2D_Image assets_wgt(const char* id);

// Blit a plate at the screen origin (0,0). No-op if not ready.
void assets_draw_plate(const char* id);
// Blit a widget's top-left at (x,y). No-op if the id is unknown.
void assets_draw_wgt(const char* id, float x, float y);
// Blit a widget stretched to fill (x,y,w,h) — for 9-slice-ish fills / focus rings.
void assets_draw_wgt_fit(const char* id, float x, float y, float w, float h);

// Text via a baked bcfnt, at the role's own size (typography.h). `y` is the TOP of the line
// box, whose height is exactly typo_role_px(r). Origins are rounded to whole pixels inside
// assets_text, so `_c` / `_r` cannot land a glyph on a half-pixel either (SPEC-crisp C4).
C2D_Font assets_font(TxtRole r);
// Harness/self-check: publish the texel scale + lineFeed each role actually draws at into
// g_txtTexelScale[] / g_txtLineFeed[], so `gdbio read` can prove R1 holds in the SHIPPED binary
// (a screenshot cannot). Call once after assets_init.
extern float g_txtTexelScale[TXT_COUNT];
extern int   g_txtLineFeed[TXT_COUNT];
void assets_dbg_publish_scales(void);
void  assets_text  (C2D_TextBuf buf, TxtRole r, const char* s, float x,  float y, u32 col);
void  assets_text_c(C2D_TextBuf buf, TxtRole r, const char* s, float cx, float y, u32 col);
void  assets_text_r(C2D_TextBuf buf, TxtRole r, const char* s, float rx, float y, u32 col);
float assets_text_w(C2D_TextBuf buf, TxtRole r, const char* s);

// ---- composite widgets (used across screens) ----
// The corner radius every `fill-*-r8` 9-slice is drawn with (the sprite name's own "-r8"). Public
// so a caller that draws a RING around a button uses the button's radius rather than guessing —
// see assets_button's focus ring and main.c's selection rings (fix pass, findings 1 + 8).
#define ASSETS_BTN_R 8.0f
// A button: blit `sprite` (or `sprite`+"-focus" when focus) stretched to (x,y,w,h) + centered label.
void assets_button(C2D_TextBuf buf, const char* sprite, float x, float y, float w, float h,
                   const char* label, TxtRole r, u32 col, int focus);
// A segmented control: a g_art.panel track at (x,y,w,h), a g_ui.acc pill over the active cell and
// centred labels. Procedural (phase 17 W1) — the `seg-active` sprite carries a baked example label.
// `inkA` is drawn on the ACCENT pill (pass g_ui.ink), `dim` on the TRACK, which is a baked-art
// surface (pass g_art.dim — g_ui.dim on it measured 2.83:1 on Daylight; see theme.h's rule).
void assets_seg(C2D_TextBuf buf, float x, float y, float w, float h,
                const char* const* opts, int n, int active, u32 inkA, u32 dim);
// A toggle at (x,y): toggle-on / toggle-off sprite.
void assets_toggle(int on, float x, float y);
// 9-slice a widget (fill-*-r8) to any (x,y,w,h) keeping its r-px corners crisp.
void assets_fill9(const char* id, float x, float y, float w, float h, float r);

// ---- sub-cell of a sheet (phase 15 SPEC-avatar A1.4.3) ----
// Cut the pixel rect (px,py,pw,ph) out of `src`'s OWN subtexture rect and hand back a drawable
// C2D_Image referring to it. This is the subtexture arithmetic draw_9slice/draw_hslice already
// use, promoted to a public helper rather than copied a third time for the co-op avatar's 3x3
// sprite sheet. `st` must outlive the returned image (the C2D_Image points AT it), which is why
// the caller owns it — the same pattern render_game uses for its own frame subtexture.
// Works for any image whose subtex is valid, including one built by hand over a plain C3D_Tex
// (the placeholder sheet), so the placeholder and the real baked art take one code path.
// Returns false (and leaves *out zeroed) when `src` has no texture.
bool assets_img_cell(C2D_Image src, float px, float py, float pw, float ph,
                     C2D_Image* out, Tex3DS_SubTexture* st);
