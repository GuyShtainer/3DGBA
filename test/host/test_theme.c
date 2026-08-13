// test_theme.c — PC host suite for the phase-17 theme/contrast layer (source/theme.{c,h}).
// Covers SPEC-widgets W4.4 TH1-TH6: every (ink token, surface token, draw scale) triple the app
// actually draws, graded for all five fixed presets AND the custom builder swept over its whole
// hue x contrast grid; the fixed role colours; the D11 "3D badge" regression; the D10 ink-on-
// baked-art regression; the theme-invariant scrim inks; and the custom builder's own guarantees.
// Pure-C dual-compile per CLAUDE.md rule #4 — theme.c is colour arithmetic, so under
// -DTHEME_HOST_SHIM its only libctru surface (u32 + C2D_Color32) comes from ctr_shim.h.
//
//   clang -std=c11 -Wall -Wextra -O2 -I source -DTHEME_HOST_SHIM \
//         -include test/host/ctr_shim.h test/host/test_theme.c source/theme.c -lm -o /tmp/tth && /tmp/tth
//
// WHY THE MODEL HAS A COVERAGE TERM (this is the part that matters). A naive WCAG check on the
// nominal colour pair PASSES the unreadable 3D badge: #3E86D6 on black is nominally 5.6:1, while
// the shipped pixels measured 1.4:1-2.8:1. The difference is DRAW SCALE. The badge's label was the
// 3DS system font drawn at scale 0.32, so a stroke covered about a third of a device pixel and the
// rasteriser blended the ink toward the background. So every inventory row carries the scale it is
// really drawn at, and the grader blends fg toward bg by an estimated coverage before measuring.
//
// PHASE 18 FIX PASS — HOW THAT COLUMN IS DERIVED NOW (review finding 4). It used to be
// `px / the face's native px`, with BOTH numbers taken from phase 17: `SC_SG_BOLD(13.0f)` and the
// rest. Phase 18 deleted that world. There is no `px` argument any more (assets_text takes a ROLE),
// the four phase-17 faces are gone, and the "native px" divisors 14/13/9/10 were themselves the
// measurement error SPEC-crisp existed to fix — the real lineFeeds were 19/17/12/14. Grading at
// SC_SG_BOLD(13.0f) therefore graded a size the app had stopped drawing, at a scale that was never
// right. Every row now names the ROLE it draws through (source/typography.h), and:
//
//   * a ROLE row is graded at texel scale 1.0. That is not an assumption made here: it is the
//     crispness law R1, and test_typography T1/T2/T3 prove it from the SHIPPED data/fnt_*.bin
//     bytes (px == FINF.lineFeed for all seven rungs => typo_texel_scale == 1.0f exactly). This
//     suite would be lying if it re-derived it from the same table it is checking, so it cites the
//     suite that measures it instead, and asserts here only that the row names a real rung (TH0).
//   * a SYSTEM-FONT row carries its literal scale in `sysScale`. Those are the draws that do NOT
//     go through the ladder, so they are the only ones the coverage term still moves — which is
//     exactly the D11 mechanism, kept alive on the rows that can still exhibit it.
//
// So "red unless both land" holds in the shape the new architecture gives it: the colour pair is
// graded here, and reintroducing a tiny system-font label is caught by test_typography T12, which
// scans source/ and fails on any ui_text / C2D_DrawText draw that is not on a named allow-list.
//
// NOT IN THE INVENTORY, on purpose: the two DEV readouts (the net-diag line's dense right-hand
// half is here as a row, but the phase-15 CO-OP readout at scale 0.32 over raw game pixels is not).
// It is a debug line drawn over an unknown frame with no scrim of its own, so there is no honest
// base colour to grade it against; inventing a mid-grey one would produce a number that means
// nothing. Its readability is a diagnostic concern, not a chrome one — see main.c's own comment.
//
// THE THRESHOLD, chosen honestly (SPEC-widgets OQ6). The gate is 3.0:1, not WCAG's 4.5:1 for body
// text, for one reason: the handoff's OWN shipped presets do not clear 4.5 on their ink-on-accent
// pairs (Daylight's white on #BE7A16 is 3.5:1), so a 4.5 gate would be measuring the designer's
// palette rather than this phase's defect. 3.0 is WCAG's non-text/UI-component level, it is what
// the design comfortably clears everywhere, and it still catches D11 (1.56 measured here) and D10
// (1.21) red — which is the whole point of the suite. Body-text pairs (text/dim on a panel or a
// background) are held to the stricter 4.5 separately in TH1b, and the palettes do clear that.

#include <stdio.h>
#include <math.h>
#include <string.h>
#include "../../source/theme.h"
#include "../../source/typography.h"   // FIX PASS: the ladder IS the draw-scale column now

static int g_checks = 0, g_fail = 0;
#define CHECK(cond, ...) do { \
	g_checks++; \
	if (!(cond)) { g_fail++; printf("  [FAIL] "); printf(__VA_ARGS__); printf("   (at %s:%d)\n", __FILE__, __LINE__); } \
} while (0)

// ---------------------------------------------------------------- the contrast model
#define RGBC(r,g,b) ((u32)(0xFF000000u | ((u32)(b) << 16) | ((u32)(g) << 8) | (u32)(r)))

// Coverage estimate from the DRAW SCALE. Only SYSTEM-FONT rows can be below 1.0 now (a baked
// ladder rung draws at texel scale 1.0 by R1), so this is the term that keeps D11's mechanism
// modelled on exactly the draws that can still exhibit it. Calibrated on the one
// measurement we have: the 3D badge at scale 0.32 realised ~0.35 of its nominal ink. At scale >=
// 0.80 a stroke covers a whole device pixel, so coverage is 1.0. Linear in between.
static float coverage(float scale) {
	if (scale >= 0.80f) return 1.0f;
	if (scale <= 0.32f) return 0.35f;
	return 0.35f + 0.65f * (scale - 0.32f) / (0.80f - 0.32f);
}

static u32 blend(u32 bg, u32 fg, float a) {
	int r = (int)((bg & 0xFF)         + a * (float)((int)(fg & 0xFF)         - (int)(bg & 0xFF)));
	int g = (int)(((bg >> 8) & 0xFF)  + a * (float)((int)((fg >> 8) & 0xFF)  - (int)((bg >> 8) & 0xFF)));
	int b = (int)(((bg >> 16) & 0xFF) + a * (float)((int)((fg >> 16) & 0xFF) - (int)((bg >> 16) & 0xFF)));
	return RGBC(r, g, b);
}

// A translucent scrim over unknown game pixels: the app's HUD bar is black at alpha 0x90, the
// pause dim black at 0xC0. What the ink actually sits on depends on the game frame underneath, so
// the model needs a stated base. `scrim_over` grades against a MID-GREY (128) frame — the design's
// nominal case, and darker than that only helps. `scrim_over_white` is the adversarial case and is
// reported, not gated: near-white ink on a 0x90 bar over a WHITE game frame is 2.2:1, which is a
// real (pre-existing, theme-invariant) weakness of the bar's alpha, not something this phase's ink
// routing can fix — flagged in the BUILDLOG rather than hidden behind a threshold.
static u32 scrim_over(u8 alpha)       { return blend(RGBC(128,128,128), RGBC(0,0,0), (float)alpha / 255.0f); }
static u32 scrim_over_white(u8 alpha) { return blend(RGBC(255,255,255), RGBC(0,0,0), (float)alpha / 255.0f); }

// citro2d packs BGR in the low 24 bits; print as #RRGGBB so a log line can be pasted into the art.
static unsigned hexrgb(u32 c) { return ((c & 0xFF) << 16) | (c & 0xFF00) | ((c >> 16) & 0xFF); }

static float contrast_at(u32 fg, u32 bg, float scale) {
	float eff = rel_lum(blend(bg, fg, coverage(scale)));
	float lbg = rel_lum(bg);
	float hi = eff > lbg ? eff : lbg, lo = eff > lbg ? lbg : eff;
	return (hi + 0.05f) / (lo + 0.05f);
}

// ---------------------------------------------------------------- the token inventory
// Where a colour comes from. SRC_UI = the ACTIVE theme (procedural surfaces), SRC_ART = the theme
// the embedded art was baked from (plates, fill-*-r8 bodies, sprites), SRC_K = a constant.
// SRC_DIS = a DISABLED label: theme_ink_disabled(fg token, bg token) — both taken from the ART
// palette, because every disabled control in the app sits on a baked fill (fix pass, finding 5).
typedef enum { SRC_UI = 0, SRC_ART = 1, SRC_K = 2, SRC_DIS = 3 } Src;
// Theme field index; MUST match the Theme struct's declaration order in theme.h.
enum { T_BG = 0, T_PANEL, T_PANEL2, T_LINE, T_ACC, T_INK, T_TEXT, T_DIM, T_BOX };

static u32 field(const Theme* t, int i) {
	const u32 v[9] = { t->bg, t->panel, t->panel2, t->line, t->acc, t->ink, t->text, t->dim, t->box };
	return v[i];
}

typedef struct {
	const char* what;    // the call site, so a failure names a screen and not a number
	Src   fgSrc; int fgIdx; u32 fgK;
	Src   bgSrc; int bgIdx; u32 bgK;
	TxtRole role;        // the ladder rung this call site draws through (texel scale 1.0)
	float sysScale;      // > 0 => a SYSTEM-font draw at this literal scale; `role` then unused
	int   body;          // 1 = body text (held to 4.5 in TH1b), 0 = a UI component label
} Pair;

// A row's realised draw scale. TXT_* rungs are 1.0 by R1 (proved from the shipped font bytes in
// test_typography, not here); the system-font rows carry their own.
#define SYS(sc) (sc)                 // reads at the call site as "system font at <sc>"
// Every baked ladder rung draws at texel scale EXACTLY 1.0 (R1; measured from the shipped font
// bytes by test_typography T1/T2/T3, not asserted here). The individual regression tests below
// name it rather than re-deriving a per-face ratio, which is what went stale in phase 18.
#define SC_BAKED 1.0f
static float pair_scale(const Pair* p) { return p->sysScale > 0.0f ? p->sysScale : SC_BAKED; }

static const Pair INV[] = {
	// --- baked-art surfaces: ink must come from g_art (W4.1 line 2 / sweep D10) ---
	{ "pause PK_BTN label on fill-secondary-r8", SRC_ART,T_TEXT,0, SRC_ART,T_PANEL2,0, TXT_BUTTON, 0, 1 },
	{ "pause Resume/Wireless on fill-primary-r8",SRC_ART,T_INK, 0, SRC_ART,T_ACC,   0, TXT_BUTTON, 0, 1 },
	{ "splash TAP TO START on fill-primary-r8",  SRC_ART,T_INK, 0, SRC_ART,T_ACC,   0, TXT_TITLE,  0, 1 },
	{ "picker START on fill-primary-r8",         SRC_ART,T_INK, 0, SRC_ART,T_ACC,   0, TXT_BUTTON, 0, 1 },
	// FIX PASS (review finding 5). The picker's DEFAULT state — nothing picked yet, so this is the
	// first thing every boot shows. It was `g_art.dim` on the baked gold: 1.51:1, in all six themes.
	// body = 0: a DISABLED label is a component state, not body text. Holding it to TH1b's 4.5
	// would be asking a deliberately-weakened affordance to read as strongly as an active one; the
	// 3.0 gate is the floor that matters, and TH7 separately asserts it stays UNDER the enabled ink.
	{ "picker START disabled on fill-primary-r8",SRC_DIS,T_INK, 0, SRC_ART,T_ACC,   0, TXT_BUTTON, 0, 0 },
	{ "picker slot name on the plate",           SRC_ART,T_TEXT,0, SRC_ART,T_BG,    0, TXT_BODY,   0, 1 },
	{ "picker empty slot hint on the plate",     SRC_ART,T_DIM, 0, SRC_ART,T_BG,    0, TXT_BODY,   0, 1 },
	{ "ROM list row title on the plate",         SRC_ART,T_TEXT,0, SRC_ART,T_BG,    0, TXT_BODY,   0, 1 },
	{ "ROM list game code on the plate",         SRC_ART,T_DIM, 0, SRC_ART,T_BG,    0, TXT_CHIP,   0, 0 },
	{ "PK_STEP -/+ on fill-secondary-r8",        SRC_ART,T_TEXT,0, SRC_ART,T_PANEL2,0, TXT_BUTTON, 0, 0 },
	{ "PK_STEP volume level on the plate",       SRC_ART,T_DIM, 0, SRC_ART,T_BG,    0, TXT_VALUE,  0, 0 },
	{ "overlay row label (L8.3 OV_ROW)",         SRC_ART,T_TEXT,0, SRC_ART,T_BG,    0, TXT_BODY,   0, 1 },
	{ "overlay caption (L8.3 OV_SECTION)",       SRC_ART,T_DIM, 0, SRC_ART,T_BG,    0, TXT_SECTION,0, 0 },
	{ "touch-mode explainer on the plate",       SRC_ART,T_DIM, 0, SRC_ART,T_BG,    0, TXT_SECTION,0, 0 },
	{ "pause status hint on the plate",          SRC_ART,T_DIM, 0, SRC_ART,T_BG,    0, TXT_SECTION,0, 0 },
	{ "empty state title on the plate",          SRC_ART,T_TEXT,0, SRC_ART,T_BG,    0, TXT_TITLE,  0, 1 },
	{ "empty state body on the plate",           SRC_ART,T_DIM, 0, SRC_ART,T_BG,    0, TXT_SECTION,0, 0 },
	{ "lobby seat name on the plate",            SRC_ART,T_TEXT,0, SRC_ART,T_BG,    0, TXT_BODY,   0, 1 },
	{ "lobby ghost button on the plate",         SRC_ART,T_DIM, 0, SRC_ART,T_BG,    0, TXT_BODY,   0, 1 },

	// --- procedural surfaces: ink comes from g_ui (W4.1 line 1) ---
	{ "segmented control: selected label",       SRC_UI, T_INK, 0, SRC_UI, T_ACC,   0, TXT_SEG,    0, 0 },
	// FIX PASS (review finding 9): the seg TRACK moved g_ui.panel -> g_art.panel (it is a surface
	// sitting on a baked plate), so the label on it moved g_ui.dim -> g_art.dim in the same edit.
	// Graded as an ART pair now, which is what assets_seg draws.
	{ "segmented control: unselected label",     SRC_ART,T_DIM, 0, SRC_ART,T_PANEL, 0, TXT_SEG,    0, 0 },
	{ "settings screen title (bg clear)",        SRC_UI, T_TEXT,0, SRC_UI, T_BG,    0, TXT_TITLE,  0, 1 },
	{ "settings screen hint (bg clear)",         SRC_UI, T_DIM, 0, SRC_UI, T_BG,    0, TXT_SECTION,0, 0 },
	{ "settings Done chip on the accent fill",   SRC_UI, T_INK, 0, SRC_UI, T_ACC,   0, TXT_BUTTON, 0, 0 },
	{ "picker settings chip on panel2",          SRC_UI, T_DIM, 0, SRC_UI, T_PANEL2,0, TXT_CHIP,   0, 0 },
	{ "presence card name on ui_panel",          SRC_UI, T_TEXT,0, SRC_UI, T_PANEL, 0, TXT_BUTTON, 0, 1 },
	{ "presence card note on ui_panel",          SRC_UI, T_DIM, 0, SRC_UI, T_PANEL, 0, TXT_CHIP,   0, 0 },

	// --- constant scrims / raw game pixels: theme-INVARIANT ink (W4.1 line 3) ---
	{ "HUD game name on the HUD bar",            SRC_K,0,0/*set*/, SRC_K,0,0, TXT_BODY,   0,        0 },
	// The dev net-diag readout is the ONE remaining system-font draw in the app's chrome, and it
	// keeps the coverage term (and the D11 mechanism) live in this suite. main.c justifies it:
	// ~72 characters into 400 px, which no 12 px face holds.
	{ "net-diag stat line on the HUD bar (dev)", SRC_K,0,0,        SRC_K,0,0, TXT_COUNT,  SYS(0.40f), 0 },
	{ "HUD clock on the HUD bar",                SRC_K,0,0,        SRC_K,0,0, TXT_SECTION,0,        0 },
	{ "3D badge ink on the HUD bar (W4.2 fix)",  SRC_K,0,0,        SRC_K,0,0, TXT_CHIP,   0,        0 },
	{ "paused summary name over the dim",        SRC_K,0,0,        SRC_K,0,0, TXT_BODY,   0,        1 },
	{ "HUD focused fps: the LIFTED accent",      SRC_K,0,0,        SRC_K,0,0, TXT_SECTION,0,        0 },
};
#define N_INV ((int)(sizeof INV / sizeof INV[0]))

// The constant rows, in inventory order (kept out of the table because C2D_Color32 is not a
// constant expression). Index = the row's position among SRC_K rows.
static void const_pair(int kIdx, u32* fg, u32* bg) {
	switch (kIdx) {
	case 0: *fg = THEME_ON_DARK;     *bg = scrim_over(0x90); break;   // game name
	case 1: *fg = THEME_ON_DARK;     *bg = scrim_over(0x90); break;   // net-diag stat line (dev)
	case 2: *fg = THEME_ON_DARK_DIM; *bg = scrim_over(0x90); break;   // clock / fps dim
	// The badge is an OUTLINE with a transparent interior (ui_border_round), so its ink sits on the
	// HUD BAR, not on the frame colour — grading it against THEME_GAME_B would measure a filled
	// chip the app does not draw.
	case 3: *fg = THEME_3D_TEXT;     *bg = scrim_over(0x90); break;
	case 4: *fg = THEME_ON_DARK;     *bg = scrim_over(0xC0); break;   // paused summary
	// The in-game accent, lifted for the scrim (theme_on_scrim). Graded on the same nominal base as
	// every other scrim row; the adversarial bright-game number is printed by TH5, where it belongs
	// with the other bar-alpha measurements.
	default:*fg = theme_on_scrim(g_ui.acc); *bg = scrim_over(0x90); break;
	}
}

static u32 pair_col(Src s, int idx, u32 k, const Theme* ui, const Theme* art) {
	(void)k;
	return (s == SRC_UI) ? field(ui, idx) : field(art, idx);
}

// Grade the whole inventory for one (active theme, art theme) combination.
static void grade(const char* label, const Theme* ui, const Theme* art, float gate, int bodyOnly) {
	int kIdx = 0;
	for (int i = 0; i < N_INV; i++) {
		const Pair* p = &INV[i];
		u32 fg, bg;
		if (p->fgSrc == SRC_K) { const_pair(kIdx++, &fg, &bg); }
		else {
			bg = pair_col(p->bgSrc, p->bgIdx, p->bgK, ui, art);
			fg = (p->fgSrc == SRC_DIS) ? theme_ink_disabled(field(art, p->fgIdx), bg)
			                           : pair_col(p->fgSrc, p->fgIdx, p->fgK, ui, art);
		}
		if (bodyOnly && !p->body) continue;
		float sc = pair_scale(p);
		float c = contrast_at(fg, bg, sc);
		CHECK(c >= gate, "%s: \"%s\" contrast %.2f < %.2f (fg %06X on bg %06X, scale %.2f)",
		      label, p->what, (double)c, (double)gate, hexrgb(fg), hexrgb(bg), (double)sc);
	}
}

// ---------------------------------------------------------------- tests
static void th1_every_theme(void) {
	printf("TEST 1: every inventory pair clears 3.0:1 in all 6 themes (art = what is baked)\n");
	Theme art = g_themePresets[THEME_INDIGO];        // ASSET_THEME_ID today
	for (int t = 0; t < THEME_FIXED_COUNT; t++) {
		theme_apply(t, 0, 0, 0);
		grade(THEME_NAMES[t], &g_ui, &art, 3.0f, 0);
	}
	// custom, swept over its whole parameter grid
	for (int bh = 0; bh < 360; bh += 30)
		for (int ah = 0; ah < 360; ah += 30)
			for (int ct = 6; ct <= 24; ct += 6) {
				Theme c = theme_make_custom(bh, ah, ct);
				char nm[64]; snprintf(nm, sizeof nm, "custom b%d a%d c%d", bh, ah, ct);
				grade(nm, &c, &art, 3.0f, 0);
			}
}

static void th1b_body_text(void) {
	printf("TEST 1b: BODY text pairs clear the stricter 4.5:1 in all 6 themes\n");
	Theme art = g_themePresets[THEME_INDIGO];
	for (int t = 0; t < THEME_FIXED_COUNT; t++) {
		theme_apply(t, 0, 0, 0);
		grade(THEME_NAMES[t], &g_ui, &art, 4.5f, 1);
	}
	for (int bh = 0; bh < 360; bh += 60)
		for (int ah = 0; ah < 360; ah += 60) {
			Theme c = theme_make_custom(bh, ah, 14);
			char nm[64]; snprintf(nm, sizeof nm, "custom b%d a%d", bh, ah);
			grade(nm, &c, &art, 4.5f, 1);
		}
}

static void th2_role_colours(void) {
	printf("TEST 2: the fixed role colours never move with the theme\n");
	const u32 A = THEME_GAME_A, B = THEME_GAME_B, D = THEME_3D_TEXT;
	const u32 Q = THEME_QUIT, QT = THEME_QUIT_TEXT;
	const u32 P[5] = { PAD_COLOR_0, PAD_COLOR_1, PAD_COLOR_2, PAD_COLOR_3, PAD_COLOR_4 };
	for (int t = 0; t < THEME_FIXED_COUNT; t++) {
		theme_apply(t, 0, 0, 0);
		CHECK(THEME_GAME_A == A && THEME_GAME_B == B && THEME_3D_TEXT == D, "role colours moved in %s", THEME_NAMES[t]);
		CHECK(THEME_QUIT == Q && THEME_QUIT_TEXT == QT, "quit colours moved in %s", THEME_NAMES[t]);
		for (int i = 0; i < 5; i++) {
			const u32 now[5] = { PAD_COLOR_0, PAD_COLOR_1, PAD_COLOR_2, PAD_COLOR_3, PAD_COLOR_4 };
			CHECK(now[i] == P[i], "PAD_COLOR_%d moved in %s", i, THEME_NAMES[t]);
		}
	}
	// The handoff's documented values, so a "harmless" palette tidy-up cannot silently change them.
	CHECK(THEME_GAME_A  == RGBC(0x63,0xB2,0x3C), "GAME_A is not #63B23C");
	CHECK(THEME_GAME_B  == RGBC(0x3E,0x86,0xD6), "GAME_B is not #3E86D6");
	CHECK(THEME_3D_TEXT == RGBC(0xA9,0xD4,0xFF), "3D_TEXT is not #a9d4ff");
	// L8.1.1: the cart-cap palette must not contain a ROLE colour, or a list row's cart can
	// impersonate the A/B badge drawn on the same row.
	const u32 CART[3] = { PAD_COLOR_0, PAD_COLOR_3, PAD_COLOR_4 };
	for (int i = 0; i < 3; i++) {
		CHECK(CART[i] != THEME_GAME_A, "cart tint %d is the Game-A role green", i);
		CHECK(CART[i] != THEME_GAME_B, "cart tint %d is the Game-B role blue", i);
	}
}

static void th3_the_3d_badge(void) {
	printf("TEST 3: the D11 regression — the OLD 3D badge fails, the NEW one passes\n");
	u32 bar = scrim_over(0x90);
	// Worst realistic case: the bar over a DARK game frame, which is what the sweep measured.
	u32 barDark = blend(RGBC(20,18,26), RGBC(0,0,0), 0x90 / 255.0f);
	// BEFORE: ui_chip("3D", …, THEME_GAME_B) — one colour for frame AND ink, system font at 0.32.
	float before = contrast_at(THEME_GAME_B, barDark, 0.32f);
	CHECK(before < 3.0f, "the pre-fix 3D badge should FAIL the gate, measured %.2f", (double)before);
	CHECK(before > 1.2f && before < 3.0f, "pre-fix badge %.2f outside the measured 1.4-2.8 band", (double)before);
	// AFTER: ui_chip_2("3D", …, THEME_GAME_B frame, THEME_3D_TEXT ink) with FNT_JBM_MED at 8 px.
	float after = contrast_at(THEME_3D_TEXT, barDark, SC_BAKED);
	CHECK(after >= 3.0f, "the fixed 3D badge must pass, measured %.2f", (double)after);
	CHECK(after > before * 3.0f, "the fix should be a large step, %.2f -> %.2f", (double)before, (double)after);
	// Which half does what, stated honestly rather than asserted as a slogan:
	//   the FONT is the load-bearing half for CONTRAST — the old colour at the new scale already
	//   clears the gate (its nominal 5.6:1 is finally realised),
	CHECK(contrast_at(THEME_GAME_B, barDark, SC_BAKED) >= 3.0f, "font alone should clear the gate");
	//   while the new colour at the OLD scale still fails, because coverage, not hue, was the fault,
	CHECK(contrast_at(THEME_3D_TEXT, barDark, 0.32f) < after, "the role pair alone cannot fix coverage");
	//   and the role pair is what makes it the handoff's #a9d4ff-on-#3E86D6 badge AND adds the
	//   headroom that survives the real panel's gamma (REPORT §6: D11 is the judgement most likely
	//   to get WORSE on hardware).
	CHECK(after > contrast_at(THEME_GAME_B, barDark, SC_BAKED), "the role pair must add headroom");
	(void)bar;
}

static void th4_ink_on_baked_art(void) {
	printf("TEST 4: the D10 regression — ACTIVE ink on BAKED art fails, g_art ink passes\n");
	Theme art = g_themePresets[THEME_INDIGO];
	for (int t = 0; t < THEME_FIXED_COUNT; t++) {
		theme_apply(t, 0, 0, 0);
		float wrong = contrast_at(g_ui.text, art.panel2, SC_BAKED);   // the shipped bug
		float right = contrast_at(art.text,  art.panel2, SC_BAKED);   // W4.3.a
		CHECK(right >= 3.0f, "%s: g_art ink on the baked button must pass, got %.2f",
		      THEME_NAMES[t], (double)right);
		if (t == THEME_DAYLIGHT)
			CHECK(wrong < 3.0f, "Daylight ink on indigo art must FAIL (this IS D10), got %.2f", (double)wrong);
	}
	// theme_init_art must FOLLOW the build stamp, not a hardcoded guess.
	for (int t = 0; t < THEME_FIXED_COUNT; t++) {
		theme_init_art(t);
		CHECK(g_art.text == g_themePresets[t].text && g_art.acc == g_themePresets[t].acc,
		      "theme_init_art(%d) did not adopt that preset", t);
		CHECK(theme_art_is(t), "theme_art_is(%d) false right after init", t);
	}
	theme_init_art(THEME_CUSTOM);
	Theme seed = theme_make_custom(205, 168, 14);
	CHECK(g_art.acc == seed.acc, "custom art must map to the pack's own seed palette");
	theme_init_art(99);   // out of range
	CHECK(g_art.text == g_themePresets[THEME_INDIGO].text, "a bogus art id must fall back to indigo");
	theme_init_art(THEME_INDIGO);
}

static void th5_invariant_inks(void) {
	printf("TEST 5: THEME_ON_DARK/_DIM stay readable on the constant scrims in EVERY theme\n");
	u32 hud = scrim_over(0x90), dim = scrim_over(0xC0);
	for (int t = 0; t < THEME_FIXED_COUNT; t++) {
		theme_apply(t, 0, 0, 0);
		CHECK(contrast_at(THEME_ON_DARK, hud, 0.40f) >= 3.0f, "%s: ON_DARK on the HUD bar", THEME_NAMES[t]);
		CHECK(contrast_at(THEME_ON_DARK, dim, SC_BAKED) >= 3.0f, "%s: ON_DARK on the pause dim", THEME_NAMES[t]);
		CHECK(contrast_at(THEME_ON_DARK_DIM, hud, 0.38f) >= 2.0f, "%s: ON_DARK_DIM on the HUD bar", THEME_NAMES[t]);
		// The trap this test exists to catch: a future "consistency" refactor routing these to the
		// theme. On Daylight g_ui.text is near-black and vanishes on a black scrim.
		if (t == THEME_DAYLIGHT)
			CHECK(contrast_at(g_ui.text, hud, 0.40f) < 3.0f,
			      "Daylight g_ui.text on the HUD bar must be BAD — that is why ON_DARK exists");
	}
	// Reported, NOT gated (see scrim_over_white): the adversarial base. This is a real property of
	// the bar's 0x90 alpha and belongs to whoever revisits the HUD composition, not to this phase.
	printf("  [info] worst case, HUD bar over a WHITE game frame: ON_DARK %.2f:1, ON_DARK_DIM %.2f:1\n",
	       (double)contrast_at(THEME_ON_DARK, scrim_over_white(0x90), 0.40f),
	       (double)contrast_at(THEME_ON_DARK_DIM, scrim_over_white(0x90), SC_BAKED));
	// theme_on_scrim's property, asserted rather than eyeballed: it never DARKENS an accent, it
	// leaves a bright one alone, and it lifts every accent that would otherwise be under the bar's
	// own luminance. (The absolute worst case on a white game frame stays low for every mid-tone
	// hue — that is the bar's alpha, printed above, not the accent's.)
	for (int t = 0; t < THEME_FIXED_COUNT; t++) {
		theme_apply(t, 0, 0, 0);
		u32 lifted = theme_on_scrim(g_ui.acc);
		CHECK(rel_lum(lifted) >= rel_lum(g_ui.acc), "%s: theme_on_scrim darkened the accent", THEME_NAMES[t]);
		CHECK(rel_lum(lifted) >= 0.30f, "%s: lifted accent still below the scrim floor", THEME_NAMES[t]);
		if (rel_lum(g_ui.acc) >= 0.30f)
			CHECK(lifted == g_ui.acc, "%s: a bright accent must pass through unchanged", THEME_NAMES[t]);
		printf("  [info] %-14s accent %06X -> %06X on the bar: %.2f:1 (nominal) / %.2f:1 (white game)\n",
		       THEME_NAMES[t], hexrgb(g_ui.acc), hexrgb(lifted),
		       (double)contrast_at(lifted, scrim_over(0x90), SC_BAKED),
		       (double)contrast_at(lifted, scrim_over_white(0x90), SC_BAKED));
	}
}

static void th6_custom_builder(void) {
	printf("TEST 6: the custom builder — opaque, distinct, and its ink follows its accent\n");
	for (int bh = 0; bh < 360; bh += 15)
		for (int ah = 0; ah < 360; ah += 15)
			for (int ct = 6; ct <= 24; ct += 2) {
				Theme c = theme_make_custom(bh, ah, ct);
				for (int f = 0; f < 9; f++)
					CHECK((field(&c, f) >> 24) == 0xFF, "custom b%d a%d c%d field %d is not opaque", bh, ah, ct, f);
				float ink = contrast_at(c.ink, c.acc, SC_BAKED);
				CHECK(ink >= 3.0f, "custom b%d a%d c%d: ink on accent only %.2f", bh, ah, ct, (double)ink);
				CHECK(contrast_at(c.text, c.bg, SC_BAKED) >= 4.5f,
				      "custom b%d a%d c%d: body text on bg", bh, ah, ct);
			}
	// Clamping: out-of-range contrast must not produce a different palette than the clamp value.
	Theme lo = theme_make_custom(200, 100, -5), lo2 = theme_make_custom(200, 100, 6);
	Theme hi = theme_make_custom(200, 100, 99), hi2 = theme_make_custom(200, 100, 24);
	CHECK(lo.panel == lo2.panel && hi.panel == hi2.panel, "custom contrast is not clamped to 6..24");
	// The rule the W4 fix added: a dark accent gets light ink, a bright accent dark ink.
	Theme blue = theme_make_custom(240, 240, 14), gold = theme_make_custom(240, 50, 14);
	CHECK(rel_lum(blue.ink) > rel_lum(blue.acc), "a DARK accent must take LIGHT ink");
	CHECK(rel_lum(gold.ink) < rel_lum(gold.acc), "a BRIGHT accent must take DARK ink");
}

// FIX PASS (review finding 5): the red-before-green regression that NAMES the defect, in the same
// shape TH3/TH4 use — the shipped colour must FAIL and the new one must pass, in every theme, or
// this suite is measuring a palette instead of a bug.
static void th7_disabled_ink(void) {
	printf("TEST 7: the disabled-primary regression — g_art.dim on the baked gold fails everywhere\n");
	for (int a = 0; a < THEME_FIXED_COUNT; a++) {
		Theme art = g_themePresets[a];
		float wrong = contrast_at(art.dim, art.acc, SC_BAKED);            // what shipped
		float right = contrast_at(theme_ink_disabled(art.ink, art.acc), art.acc, SC_BAKED);
		float on    = contrast_at(art.ink, art.acc, SC_BAKED);            // the ENABLED label
		CHECK(right >= 3.0f, "%s art: disabled START must be legible, got %.2f", THEME_NAMES[a], (double)right);
		CHECK(right <= on,   "%s art: disabled must read no stronger than enabled (%.2f vs %.2f)",
		      THEME_NAMES[a], (double)right, (double)on);
		// The defect, named per pack: the shipped `dim` token is BELOW the suite's own 3.0 gate on
		// every accent surface — this is not an Indigo accident, it is what `dim` is for (panels).
		CHECK(wrong < 3.0f, "%s art: g_art.dim on the accent must FAIL — this IS the defect, got %.2f",
		      THEME_NAMES[a], (double)wrong);
		printf("  [info] %-14s art: disabled %06X on %06X = %.2f:1 (was %.2f:1, enabled %.2f:1)\n",
		       THEME_NAMES[a], hexrgb(theme_ink_disabled(art.ink, art.acc)), hexrgb(art.acc),
		       (double)right, (double)wrong, (double)on);
	}
	// Properties, not just the one call site: the mix never leaves the ink->surface segment, and a
	// disabled ink is always strictly closer to its surface than the enabled one.
	for (int a = 0; a < THEME_FIXED_COUNT; a++)
		for (int b = 0; b < THEME_FIXED_COUNT; b++) {
			u32 ink = g_themePresets[a].ink, surf = g_themePresets[b].acc;
			u32 d = theme_ink_disabled(ink, surf);
			float li = rel_lum(ink), ls = rel_lum(surf), ld = rel_lum(d);
			float lo = li < ls ? li : ls, hi = li < ls ? ls : li;
			CHECK(ld >= lo - 0.001f && ld <= hi + 0.001f, "disabled ink %d/%d left the segment", a, b);
			CHECK((d >> 24) == 0xFF, "disabled ink %d/%d is not opaque", a, b);
		}
}

// PHASE 18 FIX PASS — the guard on the draw-scale column itself. The column went stale silently
// last phase because it was a pile of magic ratios that nothing checked. Now it is a ROLE, and a
// role is either a real rung of source/typography.h's ladder or a compile error — plus this:
// every non-system row must resolve to a rung with a real px, and a system row must NOT name one
// (a row cannot be half-and-half, which is how "graded at a size the app stopped drawing" starts).
static void th0_scale_column(void) {
	printf("TEST 0: every inventory row's draw scale is derived, not guessed\n");
	for (int i = 0; i < N_INV; i++) {
		const Pair* p = &INV[i];
		if (p->sysScale > 0.0f) {
			CHECK(p->role == TXT_COUNT, "\"%s\": a system-font row must not name a ladder rung", p->what);
			CHECK(p->sysScale > 0.0f && p->sysScale < 1.0f,
			      "\"%s\": system scale %.2f is not a fractional system-font scale", p->what, (double)p->sysScale);
			CHECK(coverage(p->sysScale) < 1.0f,
			      "\"%s\": a system row at %.2f should still be coverage-penalised", p->what, (double)p->sysScale);
		} else {
			CHECK(p->role >= 0 && p->role < TXT_COUNT, "\"%s\": role out of the ladder", p->what);
			CHECK(typo_role_px(p->role) > 0.0f, "\"%s\": role %d has no px", p->what, (int)p->role);
			// The rung must be a REAL baked face — one .bin, one size. This suite cannot open
			// the font files (it links only theme.c), so the bytes-level half of R1
			// (px == FINF.lineFeed => texel scale exactly 1.0) is test_typography T1/T2's job and
			// is cited, not restated: restating it from the same table would be a tautology, which
			// is precisely how the old draw-scale column managed to be wrong AND green.
			CHECK(typo_face(p->role).sym[0] != '\0', "\"%s\": rung %d names no baked face", p->what, (int)p->role);
			CHECK(pair_scale(p) == SC_BAKED, "\"%s\": baked row graded off 1.0", p->what);
			CHECK(coverage(pair_scale(p)) == 1.0f, "\"%s\": baked row is coverage-penalised", p->what);
		}
	}
	// The inventory must keep covering all seven rungs — a role that no row exercises is a role
	// whose ink pairs nothing grades.
	for (int r = 0; r < TXT_COUNT; r++) {
		int n = 0;
		for (int i = 0; i < N_INV; i++) if (INV[i].sysScale <= 0.0f && (int)INV[i].role == r) n++;
		CHECK(n > 0, "no inventory row draws through ladder rung %d", r);
	}
}

int main(void) {
	printf("=== test_theme — phase-17 SPEC-widgets W4.4 (contrast + theme correctness) ===\n");
	th0_scale_column();
	th1_every_theme();
	th1b_body_text();
	th2_role_colours();
	th3_the_3d_badge();
	th4_ink_on_baked_art();
	th5_invariant_inks();
	th6_custom_builder();
	th7_disabled_ink();
	printf("\n%d checks, %d failures\n", g_checks, g_fail);
	return g_fail ? 1 : 0;
}
