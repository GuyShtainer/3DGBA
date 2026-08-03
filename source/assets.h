// assets.h — device-native art pack loader (design_handoff_3dgba_ui/assets_3ds).
// Plates (chrome PNGs) + widgets (sprites) are baked to t3x and the fonts to bcfnt by
// tools/build_assets.sh, embedded into the ELF via data/*.bin, and loaded here. Draw model
// per frame/screen: (1) game video into the video rect, (2) the chrome plate, (3) dynamic
// text + widgets from the manifest on top. See assets_3ds/README-IMPLEMENTATION.md.
#pragma once
#include <citro2d.h>

// The four baked faces (Space Grotesk + JetBrains Mono, OFL). Sizes per FONTS.md roles.
typedef enum { FNT_SG_BOLD, FNT_SG_MED, FNT_JBM_MED, FNT_JBM_BOLD, FNT_COUNT } AFont;

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

// Text via a baked bcfnt. `px` = target cap/line pixel height (scaled from the native bake).
C2D_Font assets_font(AFont f);
void  assets_text  (C2D_TextBuf buf, AFont f, const char* s, float x,  float y, float px, u32 col);
void  assets_text_c(C2D_TextBuf buf, AFont f, const char* s, float cx, float y, float px, u32 col);
void  assets_text_r(C2D_TextBuf buf, AFont f, const char* s, float rx, float y, float px, u32 col);
float assets_text_w(C2D_TextBuf buf, AFont f, const char* s, float px);

// ---- composite widgets (used across screens) ----
// A button: blit `sprite` (or `sprite`+"-focus" when focus) stretched to (x,y,w,h) + centered label.
void assets_button(C2D_TextBuf buf, const char* sprite, float x, float y, float w, float h,
                   const char* label, AFont f, float px, u32 col, int focus);
// A segmented control: seg-track fit to (x,y,w,h), seg-active over the active cell, labels centered.
void assets_seg(C2D_TextBuf buf, float x, float y, float w, float h,
                const char* const* opts, int n, int active, u32 inkA, u32 dim);
// A toggle at (x,y): toggle-on / toggle-off sprite.
void assets_toggle(int on, float x, float y);
// 9-slice a widget (fill-*-r8) to any (x,y,w,h) keeping its r-px corners crisp.
void assets_fill9(const char* id, float x, float y, float w, float h, float r);
