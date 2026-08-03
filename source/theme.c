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

// Persisted UI prefs with shipped defaults (Indigo, dual mode, gold pad, round edges; the custom
// seed = the "Teal" chip 205,168 + contrast 14).
UiPrefs g_prefs = { THEME_INDIGO, 205, 168, 14, 0, 0, 0 };

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

Theme theme_make_custom(int baseHue, int accentHue, int contrast) {
	const u32 WHITE = RGB(0xFF,0xFF,0xFF);
	const u32 BLACK = RGB(0x00,0x00,0x00);
	if (contrast < 6)  contrast = 6;
	if (contrast > 24) contrast = 24;
	// lift = contrast as a % of WHITE mixed into the bg for the panels. Matches the prototype
	// exactly (line 635): panel=mix(bg,100-lift,white) => lerp(bg->white, lift/100); panel2/line
	// lift more but are clamped so bright themes don't blow out; text/dim are mostly white (readable
	// on the dark bg); box is bg nudged toward black. (README's text/dim/box %s were inverted.)
	float lift = contrast / 100.0f;
	float t2 = lift * 1.9f; if (t2 > 0.50f) t2 = 0.50f;
	float t3 = lift * 2.7f; if (t3 > 0.60f) t3 = 0.60f;
	u32 bg = hsl_rgb((float)(baseHue % 360), 0.24f, 0.09f);
	Theme t;
	t.bg     = bg;
	t.acc    = hsl_rgb((float)(accentHue % 360), 0.72f, 0.60f);
	t.panel  = lerp_rgb(bg, WHITE, lift);
	t.panel2 = lerp_rgb(bg, WHITE, t2);
	t.line   = lerp_rgb(bg, WHITE, t3);
	t.text   = lerp_rgb(bg, WHITE, 0.90f);   // mix(bg,10,white)  = 90% white
	t.dim    = lerp_rgb(bg, WHITE, 0.54f);   // mix(bg,46,white)  = 54% white
	t.box    = lerp_rgb(bg, BLACK, 0.14f);   // mix(bg,86,black)  = 14% black
	t.ink    = RGB(0x0B,0x10,0x14);
	return t;
}

void theme_apply(int id, int baseHue, int accentHue, int contrast) {
	if (id == THEME_CUSTOM) {
		g_ui = theme_make_custom(baseHue, accentHue, contrast);
	} else {
		if (id < 0 || id >= THEME_FIXED_COUNT) id = THEME_INDIGO;
		g_ui = g_themePresets[id];
	}
}
