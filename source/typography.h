// typography.h — the type ladder, and the ONE piece of arithmetic that makes text sharp.
//
// PHASE 18 / SPEC-crisp. Pure C (CLAUDE.md rule #4): no libctru, no citro2d, so the host
// suite test/host/test_typography.c calls exactly this code instead of a copy of it.
//
// ---------------------------------------------------------------------------------------
// R1 — THE CRISPNESS LAW
//
//   A bcfnt glyph is pixel-perfect on screen iff its texel scale is EXACTLY 1.0 and its
//   draw origin is an integer. Anything else resamples a bitmap and blurs it.
//
// citro2d hides a factor from you (disassembled from the installed libcitro2d.a):
//   C2Di_PostLoadFont : font->textScale = 30.0f / TGLP.cellHeight
//   C2D_DrawText      : multiplies the caller's scaleX/Y by font->textScale
//   C2D_TextGetDimensions(t, 1.0f, 1.0f, ..) : returns ceil(FINF.lineFeed * 30 / cellHeight)
// so `C2D_TextGetDimensions` at scale 1.0 does NOT return the bake's pixel height — it
// returns citro2d's normalised "a line is 30 px" height, for EVERY font. Measuring with it
// and dividing (what assets.c did in phases 16-17) is a no-op that looks like a correction.
//
// Substituting citro2d's own factor into assets.c's `sc = px / s_native`:
//
//   texel_scale = sc * 30/cellH = px * 30 / (s_native * cellH)
//   and with s_native = lineFeed * 30 / cellH   (typo_s_native below, NOT ceil'd)
//   texel_scale = px / lineFeed                                       <-- exact
//
// So R1 reduces to: **px == lineFeed(face)**, and integer origins. cellHeight cancels out
// entirely, which is why a glyph whitelist (which moves cellHeight but not lineFeed) cannot
// change the result.
//
// ---------------------------------------------------------------------------------------
// THE LADDER
//
// `mkbcfnt -s <pt>` produces quantised line heights (measured on this toolchain, 2026-08-12;
// identical for the Medium and Bold cut of a family):
//   Space Grotesk  pt 4->lf7  5->9  6->10  7->12  8->14  9->15  10->17  11->19  12->20
//   JetBrains Mono pt 4->lf7  5->9  6->11  7->12  8->14  9->16  10->18  11->19  12->21
// There is no 8 px and no 13 px rung for either family, which is why the phase-17 call sites
// (px 8, 8.5, 13 ...) could never have been 1:1 with the four faces that shipped.
//
// Each ROLE below therefore names ONE face baked at ONE size and drawn at exactly that size.
// The role IS the size: assets_text() takes no `px` argument any more, so no call site can
// reintroduce the defect by typing a literal.
//
// Two independent guards keep the bake and this table in step:
//   * tools/build_assets.sh parses FINF.lineFeed out of every .bin it produces and FAILS the
//     bake if it is not the px this table declares;
//   * test/host/test_typography.c re-parses data/fnt_*.bin and asserts the same thing, plus
//     that every role resolves to scale 1.0 through typo_texel_scale().
#pragma once

typedef enum {
	TXT_TITLE = 0,  // Space Grotesk Bold  17 px — screen titles ("Settings", "No games found")
	TXT_BUTTON,     // Space Grotesk Bold  12 px — every button label, +/- steppers
	TXT_BODY,       // Space Grotesk Med   12 px — list rows, game names, prose
	TXT_SEG,        // Space Grotesk Med   10 px — segmented-control labels (tight cells)
	TXT_SECTION,    // JetBrains Mono Med   9 px — section labels, HUD clock/fps, hint lines
	TXT_CHIP,       // JetBrains Mono Med   7 px — chips, badges, ROM codes, fine print
	TXT_VALUE,      // JetBrains Mono Bold 11 px — caps / emphasised values
	TXT_COUNT
} TxtRole;

// One ladder rung. `sym` is the data/fnt_<sym>.bin stem AND the bin2s symbol stem;
// `px` is both the bake's lineFeed and the size it is drawn at (R1); `ttf`/`pt` are what
// tools/build_assets.sh feeds mkbcfnt.
typedef struct {
	const char* sym;
	const char* ttf;
	int         pt;
	float       px;
} TxtFace;

static inline TxtFace typo_face(TxtRole r) {
	switch (r) {
	case TXT_TITLE:   { TxtFace f = { "sg_bold_17",  "sg-bold",  10, 17.0f }; return f; }
	case TXT_BUTTON:  { TxtFace f = { "sg_bold_12",  "sg-bold",   7, 12.0f }; return f; }
	case TXT_BODY:    { TxtFace f = { "sg_med_12",   "sg-med",    7, 12.0f }; return f; }
	case TXT_SEG:     { TxtFace f = { "sg_med_10",   "sg-med",    6, 10.0f }; return f; }
	case TXT_SECTION: { TxtFace f = { "jbm_med_9",   "jbm-med",   5,  9.0f }; return f; }
	case TXT_CHIP:    { TxtFace f = { "jbm_med_7",   "jbm-med",   4,  7.0f }; return f; }
	case TXT_VALUE:   { TxtFace f = { "jbm_bold_11", "jbm-bold",  6, 11.0f }; return f; }
	default:          { TxtFace f = { "", "", 0, 0.0f }; return f; }
	}
}

static inline float typo_role_px(TxtRole r) { return typo_face(r).px; }

// The scale assets_text hands to C2D_DrawText. Written as px*cellH/(lineFeed*30) rather than the
// algebraically identical px / (lineFeed*30/cellH): the grouped form is EXACT in float for every
// rung on the ladder, while routing through a normalised line height loses an ULP on TXT_CHIP
// (lineFeed 7 / cellH 9 -> 0.99999994). One ULP is optically irrelevant, but "the texel scale is
// exactly 1.0" is the invariant this phase exists to enforce, and an invariant you have to assert
// with a tolerance is an invariant that drifts.
static inline float typo_draw_scale(float px, int lineFeed, int cellH) {
	return (lineFeed > 0 && cellH > 0) ? (px * (float)cellH / ((float)lineFeed * 30.0f)) : 1.0f;
}

// What actually lands on the texture sampler, after citro2d multiplies by its hidden
// textScale = 30/cellHeight. THIS is the number R1 pins at 1.0.
static inline float typo_texel_scale(float px, int lineFeed, int cellH) {
	return (cellH > 0) ? (typo_draw_scale(px, lineFeed, cellH) * (30.0f / (float)cellH)) : 1.0f;
}

// What C2D_TextGetDimensions(t, 1.0f, 1.0f, ...) actually returns for a face — citro2d's
// NORMALISED line height, not the bake's pixel height. Kept because it is the quantity the
// phase-16/17 code mistook for "the native px size": it is ~26 for every face regardless of bake,
// so dividing by it produced scales of 0.577x-1.049x while looking like a measurement. The host
// suite uses it to reproduce the old defect rather than merely assert it (T4).
static inline float typo_s_native_citro(int lineFeed, int cellH) {
	return (cellH > 0) ? ((float)lineFeed * 30.0f / (float)cellH) : 1.0f;
}
