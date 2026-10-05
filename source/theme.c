// theme.c — the 6 UI palettes + the custom (HSL) builder + the active-theme state.
// See theme.h. citro2d colors are u32s packed r | g<<8 | b<<16 | a<<24. C2D_Color32 is a
// (non-constant) inline fn here, so static palette tables use the constant-expression RGB() macro.
#include "theme.h"
#include <math.h>

#define RGB(r,g,b) ((u32)(0xFF000000u | ((u32)(b) << 16) | ((u32)(g) << 8) | (u32)(r)))

// The ACTIVE theme. Statically initialised to Indigo+Gold so the very first splash/picker frames
// (before settings load) already look right.
Theme g_ui = {
	RGB(0x20,0x18,0x30), RGB(0x2A,0x20,0x42), RGB(0x33,0x26,0x5A), RGB(0x3B,0x2E,0x60),
	RGB(0xF5,0xD0,0x42), RGB(0x20,0x18,0x2F), RGB(0xF4,0xF1,0xFB), RGB(0xB0,0xA8,0xC8),
	RGB(0x0E,0x0B,0x16),
};

// The palette the BAKED ART was made from (SPEC-widgets W4.3.a). Statically Indigo — which is what
// tools/build_assets.sh bakes by default — and re-derived from ASSET_THEME_ID in assets_init(), so
// it FOLLOWS the build rather than hardcoding a guess.
Theme g_art = {
	RGB(0x20,0x18,0x30), RGB(0x2A,0x20,0x42), RGB(0x33,0x26,0x5A), RGB(0x3B,0x2E,0x60),
	RGB(0xF5,0xD0,0x42), RGB(0x20,0x18,0x2F), RGB(0xF4,0xF1,0xFB), RGB(0xB0,0xA8,0xC8),
	RGB(0x0E,0x0B,0x16),
};
static int s_artId = THEME_INDIGO;

// Persisted UI prefs with shipped defaults (Indigo, dual mode, gold pad, round edges; the custom
// seed = the "Teal" chip 205,168 + contrast 14). The trailing 0 is phase 14's tiltLevel = Off
// (SPEC-integration I5.8: the other HD-2D effects default ON because they are hardware-proven;
// this one is not, so upgrading changes no existing user's frame budget).
// The trailing zeros are the SHIPPED DEFAULTS and are written out rather than left to C's
// zero-fill so each one is a visible decision: tiltLevel 0 = Off (phase 14, hardware-unproven
// effect), smartTraverse 0 = Off (phase 22.2, SPEC-family-traversal T4.1 — a feature that MOVES
// the player and answers the game's own yes/no prompts does not switch itself on for everyone).
UiPrefs g_prefs = { THEME_INDIGO, 205, 168, 14, 0, 0, 0, /* tiltLevel */ 0, /* smartTraverse */ 0, /* voxel */ 0, /* voxPitch */ 2, /* voxZoom */ 1 };

const char* const THEME_NAMES[THEME_PRESET_COUNT] = {
	"Indigo + Gold", "Midnight OLED", "Daylight", "Per-game Duo", "Retro Purple", "Custom",
};

// The 5 fixed presets (index by ThemeId; THEME_CUSTOM is generated, not stored here).
// Field order matches Theme: bg, panel, panel2, line, acc, ink, text, dim, box.
const Theme g_themePresets[THEME_FIXED_COUNT] = {
	{ RGB(0x20,0x18,0x30), RGB(0x2A,0x20,0x42), RGB(0x33,0x26,0x5A), RGB(0x3B,0x2E,0x60),   // Indigo + Gold
	  RGB(0xF5,0xD0,0x42), RGB(0x20,0x18,0x2F), RGB(0xF4,0xF1,0xFB), RGB(0xB0,0xA8,0xC8), RGB(0x0E,0x0B,0x16) },
	{ RGB(0x0B,0x0D,0x12), RGB(0x14,0x17,0x1F), RGB(0x1C,0x21,0x30), RGB(0x24,0x2A,0x3A),   // Midnight OLED
	  RGB(0xF5,0xD0,0x42), RGB(0x0B,0x0D,0x12), RGB(0xEA,0xEC,0xF2), RGB(0x8A,0x90,0xA2), RGB(0x00,0x00,0x00) },
	{ RGB(0xED,0xEB,0xE4), RGB(0xFF,0xFF,0xFF), RGB(0xF1,0xED,0xE4), RGB(0xDA,0xD5,0xCB),   // Daylight (light)
	  RGB(0xBE,0x7A,0x16), RGB(0xFF,0xFF,0xFF), RGB(0x24,0x1C,0x33), RGB(0x6E,0x67,0x84), RGB(0xDE,0xD9,0xCE) },
	{ RGB(0x12,0x16,0x1C), RGB(0x1B,0x22,0x2C), RGB(0x23,0x2C,0x38), RGB(0x2E,0x38,0x46),   // Per-game Duo
	  RGB(0x63,0xB2,0x3C), RGB(0x0A,0x14,0x0A), RGB(0xEA,0xF0,0xF2), RGB(0x8F,0xA0,0xA6), RGB(0x0A,0x0D,0x11) },
	{ RGB(0x24,0x12,0x46), RGB(0x34,0x1C,0x5E), RGB(0x43,0x27,0x7A), RGB(0x4C,0x35,0x78),   // Retro Purple
	  RGB(0xE7,0xB8,0x4A), RGB(0x24,0x12,0x46), RGB(0xF3,0xED,0xFF), RGB(0xB2,0x9E,0xD9), RGB(0x16,0x0A,0x2E) },
};

// ---- small color helpers (channels are r|g<<8|b<<16|a<<24) ----
static inline u8 clamp8(int v) { return v < 0 ? 0 : (v > 255 ? 255 : (u8)v); }

static u32 hsl_rgb(float h, float s, float l) {
	// h in [0,360), s,l in [0,1]
	float c = (1.0f - fabsf(2.0f*l - 1.0f)) * s;
	float hp = h / 60.0f;
	float x = c * (1.0f - fabsf(fmodf(hp, 2.0f) - 1.0f));
	float r1 = 0, g1 = 0, b1 = 0;
	if      (hp < 1) { r1 = c; g1 = x; }
	else if (hp < 2) { r1 = x; g1 = c; }
	else if (hp < 3) { g1 = c; b1 = x; }
	else if (hp < 4) { g1 = x; b1 = c; }
	else if (hp < 5) { r1 = x; b1 = c; }
	else             { r1 = c; b1 = x; }
	float m = l - c / 2.0f;
	return RGB(clamp8((int)((r1+m)*255.0f+0.5f)),
	           clamp8((int)((g1+m)*255.0f+0.5f)),
	           clamp8((int)((b1+m)*255.0f+0.5f)));
}

// lerp each channel of `c` toward `toward` by t (0..1).
static u32 lerp_rgb(u32 c, u32 toward, float t) {
	if (t < 0) t = 0; if (t > 1) t = 1;
	int r = (c & 0xFF), g = (c >> 8) & 0xFF, b = (c >> 16) & 0xFF;
	int tr = (toward & 0xFF), tg = (toward >> 8) & 0xFF, tb = (toward >> 16) & 0xFF;
	return RGB(clamp8(r + (int)((tr - r) * t)),
	           clamp8(g + (int)((tg - g) * t)),
	           clamp8(b + (int)((tb - b) * t)));
}

// WCAG relative luminance of a packed colour (r | g<<8 | b<<16). Shared with the host contrast
// suite by construction: test/host/test_theme.c compiles THIS file, so the model that picks `ink`
// and the model that grades it are one implementation.
float rel_lum(u32 c) {
	const float ch[3] = { (float)(c & 0xFF) / 255.0f, (float)((c >> 8) & 0xFF) / 255.0f,
	                      (float)((c >> 16) & 0xFF) / 255.0f };
	float lin[3];
	for (int i = 0; i < 3; i++)
		lin[i] = (ch[i] <= 0.03928f) ? ch[i] / 12.92f : powf((ch[i] + 0.055f) / 1.055f, 2.4f);
	return 0.2126f * lin[0] + 0.7152f * lin[1] + 0.0722f * lin[2];
}

Theme theme_make_custom(int baseHue, int accentHue, int contrast) {
	const u32 WHITE = RGB(0xFF,0xFF,0xFF);
	const u32 BLACK = RGB(0x00,0x00,0x00);
	if (contrast < 6)  contrast = 6;
	if (contrast > 24) contrast = 24;
	// lift = contrast as a % of WHITE mixed into the bg for the panels. Matches the prototype
	// exactly (line 635): panel=mix(bg,100-lift,white) => lerp(bg->white, lift/100); panel2/line
	// lift more but are clamped so bright themes don't blow out; text/dim are mostly white (readable
	// on the dark bg); box is bg nudged toward black. (README's text/dim/box %s were inverted.)
	// PHASE 17 / W4.4 TH6. The caps used to be 0.50 / 0.60, and `dim` was a FIXED 54 % white — so
	// at the top of the contrast slider panel2 reached 45.6 % white while dim stayed at 54 %, i.e.
	// dim text on a raised panel came out at 1.30:1. The picker's "settings · ZR" chip and the
	// presence card's note are exactly that pair, so the highest-contrast custom theme made them
	// invisible. Two changes, both derivations rather than new magic numbers: the panels stop
	// climbing at 0.34/0.44, and `dim` is now derived FROM panel2 (62 % of the remaining way to
	// white), so it cannot converge on the surface it is drawn on at any slider position.
	float lift = contrast / 100.0f;
	float t2 = lift * 1.9f; if (t2 > 0.34f) t2 = 0.34f;
	float t3 = lift * 2.7f; if (t3 > 0.44f) t3 = 0.44f;
	u32 bg = hsl_rgb((float)(baseHue % 360), 0.24f, 0.09f);
	Theme t;
	t.bg     = bg;
	t.acc    = hsl_rgb((float)(accentHue % 360), 0.72f, 0.60f);
	t.panel  = lerp_rgb(bg, WHITE, lift);
	t.panel2 = lerp_rgb(bg, WHITE, t2);
	t.line   = lerp_rgb(bg, WHITE, t3);
	t.text   = lerp_rgb(bg, WHITE, 0.90f);   // mix(bg,10,white)  = 90% white
	t.dim    = lerp_rgb(t.panel2, WHITE, 0.62f);   // always clear of the panel it sits on (see above)
	t.box    = lerp_rgb(bg, BLACK, 0.14f);   // mix(bg,86,black)  = 14% black
	// PHASE 17 / SPEC-widgets W4 (found by test_theme.c TH6, not by eye). `ink` is the text drawn ON
	// the accent — the selected segmented pill, the primary button, the "Done" chip — and it used to
	// be a hardcoded near-black whatever the user's accent hue was. The custom accent is
	// hsl(hue, 0.72, 0.60), whose relative luminance runs from 0.13 (blue, hue≈240) to 0.72
	// (yellow): near-black on the blue end measures 3.2:1, i.e. the one theme the user builds
	// themselves could be the least readable. Pick the ink by the accent's own luminance instead —
	// the same rule the five FIXED presets already follow by hand (Daylight, the only light accent,
	// is the only one with a white ink).
	// 0.179 is the exact black/white crossover: contrast to black is (L+.05)/.05 and to white is
	// 1.05/(L+.05), which cross at (L+.05)^2 = .0525. Picking the wrong side of it is not a small
	// error — at L = 0.395 (a mid orange accent) white gives 2.1:1 and black 8.1:1. The worst case
	// under this rule is 4.58:1, at the crossover itself.
	t.ink    = (rel_lum(t.acc) > 0.179f) ? RGB(0x0B,0x10,0x14) : RGB(0xF4,0xF1,0xFB);
	return t;
}

// W4.3.a step 2. The art pack's `custom` seed is the handoff's own teal chip (205/168 + contrast
// 14 = g_prefs' shipped default), so THEME_CUSTOM art is reproduced by the same builder rather than
// hardcoded — if the pack is ever regenerated from a different seed, one constant changes here.
void theme_init_art(int artThemeId) {
	s_artId = artThemeId;
	if (artThemeId == THEME_CUSTOM)              g_art = theme_make_custom(205, 168, 14);
	else if (artThemeId >= 0 && artThemeId < THEME_FIXED_COUNT) g_art = g_themePresets[artThemeId];
	else { s_artId = THEME_INDIGO; g_art = g_themePresets[THEME_INDIGO]; }
}

int theme_art_is(int id) { return id == s_artId; }

// SEEN IN CAPTURE (runs/p17-f5-th2/top_00058.png, Daylight). The HUD bar and the pause dim are
// theme-INVARIANT black scrims (W4.1 line 3) — but the ACCENT is deliberately still drawn on them,
// because it is the only thing on the in-game HUD that says which theme is active and which screen
// has focus. Daylight's accent is a mid bronze (#BE7A16, relative luminance 0.25); over the bar on
// a bright game frame that measured about 1.5:1, i.e. the focused "59fps" was the least readable
// thing on the bar. Lift any accent that is too dark for a scrim halfway to white — the hue (and
// so the "this is your theme" signal) survives, the contrast does not depend on the game frame.
// 0.30 is chosen, not derived: it is the luminance at which an accent clears 3:1 against the
// worst-case bar (a 0x90 black scrim over white game pixels).
u32 theme_on_scrim(u32 c) {
	return (rel_lum(c) >= 0.30f) ? c : lerp_rgb(c, RGB(0xFF,0xFF,0xFF), 0.50f);
}

// See theme.h. Deliberately a MIX toward the surface rather than an alpha: the caller's colour goes
// through assets_text, which blends it over whatever is already in the framebuffer, so an alpha
// would make the measured contrast depend on the draw order. A mix states the result.
//
// The mix is as strong as the pair can AFFORD. 38 % is the target — it reads clearly weaker than
// the enabled ink while measuring ~4.2:1 on the shipped gold — but a low-headroom pair cannot pay
// it: the Daylight art pack's own enabled ink (white on #BE7A16) is only 3.5:1, and mixing that by
// 38 % lands at 2.3:1, i.e. the fix would reintroduce the defect on the pack W4.3.b would add next.
// So back off in steps until the result clears the same 3.0:1 gate the host suite grades with, and
// return the full ink if even that is not enough — "as dim as this palette can afford" rather than
// a constant that happens to work for one pack.
static float contrast_of(u32 a, u32 b) {
	float la = rel_lum(a), lb = rel_lum(b);
	float hi = la > lb ? la : lb, lo = la > lb ? lb : la;
	return (hi + 0.05f) / (lo + 0.05f);
}

u32 theme_ink_disabled(u32 ink, u32 surface) {
	for (float t = 0.38f; t > 0.005f; t -= 0.06f) {
		u32 c = lerp_rgb(ink, surface, t);
		if (contrast_of(c, surface) >= 3.0f) return c;
	}
	return ink;
}

void theme_apply(int id, int baseHue, int accentHue, int contrast) {
	if (id == THEME_CUSTOM) {
		g_ui = theme_make_custom(baseHue, accentHue, contrast);
	} else {
		if (id < 0 || id >= THEME_FIXED_COUNT) id = THEME_INDIGO;
		g_ui = g_themePresets[id];
	}
}
