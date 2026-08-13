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
//
// ---------------------------------------------------------------------------------------
// PHASE 19 / SPEC-legible — THE LADDER MOVED UP, THE LAW DID NOT MOVE.
//
// The user, on real hardware: "Make the text bigger in the app, everything is simply too
// small to be written clearly". Phase 18 was right about px == lineFeed and wrong about WHICH
// px: it read the design pack's type table (which is in **em px**) as lineFeed. mkbcfnt -s N
// takes POINTS at 96 dpi, so em = N*4/3 and lineFeed ~= 1.28*em — every phase-18 rung therefore
// shipped at em = designPx/1.28 = 0.78x its own design, uniformly (SPEC-legible L1.4).
// Measured cap heights confirmed it: six of the seven rungs sat below the ISO 9241-303 /
// ANSI-HFES 16-arcmin floor at 30 cm on the 133 ppi bottom panel, TXT_CHIP at 8.7' (54% of the
// minimum), while the Pokemon text the user reads happily on the SAME panel is cap 12 px.
//
// Phase 19 moves every rung up 1-2 steps to the size the design always specified, and merges
// the two rungs that stop earning a distinction once everything is bigger:
//
//   role         phase 18            phase 19            cap px    arcmin@30cm
//   TXT_TITLE    sg-bold  17          sg-bold  19          10 -> 11   24.0'
//   TXT_BUTTON   sg-bold  12          sg-bold  15           7 ->  9   19.6'
//   TXT_BODY     sg-med   12          sg-med   15           7 ->  9   19.6'
//   TXT_SEG      sg-med   10   -----> sg-med   15 (= BODY)  6 ->  9   19.6'
//   TXT_SECTION  jbm-med   9          jbm-med  12           5 ->  7   15.3'
//   TXT_CHIP     jbm-med   7   -----> jbm-med  12 (= SEC)   4 ->  7   15.3'
//   TXT_VALUE    jbm-bold 11          jbm-bold 12           7 ->  7   15.3'   <-- NOT a size step
//
// The cap column is a BEFORE -> AFTER pair on purpose (PHASE 19 FIX PASS, verify note O1): six
// rungs gain 1-3 cap px (+10% to +75%) and the seventh gains NOTHING. TXT_VALUE's 11 -> 12 pt is a
// LINE-HEIGHT move, not a size move — jbm-bold rasterises to cap 7 / x-height 5 at BOTH pt 6
// (lineFeed 11, the phase-18 rung) and pt 7 (lineFeed 12), measured by baking both from the same
// TTF and reading the glyph sheets (tools/fontlab/bcfnt.py; test_typography T17 prints the same
// "(was N)" comparison every run). Listing it in the table without that column read as a seventh
// step, which it is not. It moved anyway for a real reason: 12 makes the mono line box IDENTICAL
// to TXT_SECTION / TXT_CHIP, and the app puts them on a shared grid — the AUDIO tab's volume
// readout is drawn at `rowY - 18` on the caption's own baseline (main.c L4.1), and the wireless
// RTT/LOSS tiles sit beside cap-7 mono captions. A rung 1 px shorter than its neighbours would
// have shown there. The honest way to make TXT_VALUE bigger is jbm-bold -s 8 (lineFeed 14, cap 8,
// 17.5') and it is REJECTED for the same reason the mono rung is capped at 12 below: the advance
// goes 5 -> 7 px (+40%), which the fixed-width readout boxes and the pause-top pill row cannot
// absorb, and a cap-8 bold number beside a cap-7 mono caption breaks the "weight, not size"
// contract the role exists to express.
//
// The two ALIASES are why this phase makes the app SMALLER: seven faces become five, and a
// bcfnt is a fixed 1024x1024 A4 sheet (~527 KB) whatever its point size, so data/ drops ~1.01 MB
// and the runtime linear heap drops ~2.0 MB (C2D_FontLoadFromMem linearAllocs a full copy of
// every face). The roles STAY in the enum and TXT_COUNT stays 7 so that no call site changes,
// g_txtTexelScale[]/g_txtLineFeed[] keep the shape the GDB harness reads, and re-splitting a
// merged role later is one line here plus one bake line. assets.c MUST load each distinct sym
// once and point both slots at the same C2D_Font (test_typography T15 asserts the contract).
//
// Known deviation, stated so nobody rediscovers it: the mono rung lands at 15.3', under the 16'
// floor. jbm-med -s 8 (cap 8, 17.5') was measured and rejected — mono advance 5->7 px blows the
// pause-top pill row to 475 px on a 400 px screen and desyncs from the BAKED cap-7 plate
// captions, which are frozen art. The mitigation is SPEC-legible L3.3: prose leaves the mono
// rung entirely for TXT_BODY (cap 9), so nothing the user READS sits at 15.3' — only captions,
// key legends, codes and readouts, which are glanced at.
#pragma once

typedef enum {
	TXT_TITLE = 0,  // Space Grotesk Bold  19 px — screen titles ("Settings", "No games found")
	TXT_BUTTON,     // Space Grotesk Bold  15 px — every button label, +/- steppers
	TXT_BODY,       // Space Grotesk Med   15 px — list rows, game names, prose
	TXT_SEG,        // = TXT_BODY          15 px — segmented-control labels (merged, phase 19)
	TXT_SECTION,    // JetBrains Mono Med  12 px — section labels, HUD clock/fps, key legends
	TXT_CHIP,       // = TXT_SECTION       12 px — chips, badges, ROM codes, fine print (merged)
	TXT_VALUE,      // JetBrains Mono Bold 12 px — caps / emphasised values (weight, not size)
	TXT_COUNT
} TxtRole;

// One ladder rung. `sym` is the data/fnt_<sym>.bin stem AND the bin2s symbol stem;
// `px` is both the bake's lineFeed and the size it is drawn at (R1); `ttf`/`pt` are what
// tools/build_assets.sh feeds mkbcfnt.
//
// PHASE 19 / SPEC-legible L3.2.7 adds the INK BOX. A citro2d run at texel scale 1.0 occupies a
// quad `cellH` tall, but the glyphs only cover part of it — and the padding is NOT symmetric
// (a face reserves more room under the baseline for descenders than it does above the cap).
// Every "centre a label in a box" site in the app centred the LINE box, which is why a cap-9
// label in an 18 px toggle row sat ~4 px low. `cellH` / `inkTop` / `inkH` are measured off the
// SHIPPED .bin at bake time over the string "AHgpy1:9" (caps, ascenders, descenders, digits,
// punctuation — the tallest and deepest thing any UI string can contain); they are guarded the
// same way lineFeed is, by test_typography re-reading the bytes (T2/T18) and failing on drift.
typedef struct {
	const char* sym;
	const char* ttf;
	int         pt;
	float       px;
	int         cellH;    // the line-box / quad height at scale 1.0 (TGLP.cellHeight)
	int         inkTop;   // first ink row INSIDE the line box, over "AHgpy1:9"
	int         inkH;     // ink rows, over the same string  => ink occupies [inkTop, inkTop+inkH)
} TxtFace;

static inline TxtFace typo_face(TxtRole r) {
	switch (r) {
	case TXT_TITLE:   { TxtFace f = { "sg_bold_19",  "sg-bold",  11, 19.0f, 22, 6, 14 }; return f; }
	case TXT_BUTTON:  { TxtFace f = { "sg_bold_15",  "sg-bold",   9, 15.0f, 18, 5, 12 }; return f; }
	case TXT_BODY:    { TxtFace f = { "sg_med_15",   "sg-med",    9, 15.0f, 18, 5, 12 }; return f; }
	case TXT_SEG:     { TxtFace f = { "sg_med_15",   "sg-med",    9, 15.0f, 18, 5, 12 }; return f; }  // alias of BODY
	case TXT_SECTION: { TxtFace f = { "jbm_med_12",  "jbm-med",   7, 12.0f, 15, 4,  9 }; return f; }
	case TXT_CHIP:    { TxtFace f = { "jbm_med_12",  "jbm-med",   7, 12.0f, 15, 4,  9 }; return f; }  // alias of SECTION
	case TXT_VALUE:   { TxtFace f = { "jbm_bold_12", "jbm-bold",  7, 12.0f, 15, 4,  9 }; return f; }
	default:          { TxtFace f = { "", "", 0, 0.0f, 0, 0, 0 }; return f; }
	}
}

static inline float typo_role_px(TxtRole r) { return typo_face(r).px; }
static inline int   typo_cell_h  (TxtRole r) { return typo_face(r).cellH; }
static inline int   typo_ink_top (TxtRole r) { return typo_face(r).inkTop; }
static inline int   typo_ink_h   (TxtRole r) { return typo_face(r).inkH; }
// Bottom of the ink box relative to the draw origin, EXCLUSIVE — the first row a string at
// `y` cannot touch. `y + typo_ink_bottom(r) <= boxBottom` is the vertical-fit rule T16 grades.
static inline int   typo_ink_bottom(TxtRole r) { return typo_face(r).inkTop + typo_face(r).inkH; }

// SPEC-legible L3.2.7 — THE ONE VERTICAL-CENTRING RULE. Returns the y to pass assets_text* so
// that the role's INK box is centred in [boxY, boxY+boxH), on an integer row.
//
// The five call sites that used `boxY + (boxH - typo_role_px(r)) / 2` were centring the LINE
// box: correct only for a face whose padding happens to be symmetric, which none of ours is.
// With the phase-19 rungs the error is 3-4 px — enough to push a button label onto its own
// bottom edge — so the arithmetic lives here once instead of being re-derived per widget.
// The rounding is part of the contract, not a convenience: R1's second half is an INTEGER
// origin, and a caller that adds 0.5 px of its own would resample the glyph even at scale 1.0.
static inline float typo_center_y(TxtRole r, float boxY, float boxH) {
	float y = boxY + (boxH - (float)typo_ink_h(r)) * 0.5f - (float)typo_ink_top(r);
	return (float)(int)(y >= 0.0f ? y + 0.5f : y - 0.5f);
}

// How many DISTINCT baked faces the ladder resolves to — the number of .bcfnt files the bake
// produces, the number of C2D_Font handles assets_init may allocate, and (x ~527 KB) the number
// this phase is trying not to grow. Aliased roles (SEG->BODY, CHIP->SECTION) do not count twice.
// Kept next to the table so a future re-split updates it in the same edit; T15 grades it.
#define TXT_DISTINCT_FACES 5

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
