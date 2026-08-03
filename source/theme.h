// theme.h — themeable UI color system for 3DGBA.
// One source of truth for colors used across the ROM picker, pause menu, HUD, splash,
// and touch overlay. The redesign turns the old flat macros into a runtime-switchable
// palette: an ACTIVE `Theme` (g_ui) that the existing THEME_* macros now read from, plus
// 6 presets + a custom (HSL) builder. Fixed role colors (game A/B, quit) stay constant.
#pragma once
#include <citro2d.h>

// ---- The active theme's tokens (one struct drives every UI color) ----
typedef struct {
	u32 bg;      // app background
	u32 panel;   // primary panel / card
	u32 panel2;  // raised / secondary panel
	u32 line;    // hairline / divider / inactive track
	u32 acc;     // accent (primary button, focus bar, highlight)
	u32 ink;     // text drawn ON the accent
	u32 text;    // primary text
	u32 dim;     // muted / secondary text
	u32 box;     // game letterbox / bezel inner (near-black)
} Theme;

// Preset order MUST match g_themePresets[] in theme.c (custom is generated, not stored there).
typedef enum {
	THEME_INDIGO = 0,   // Indigo + Gold (the shipped default)
	THEME_OLED,         // Midnight OLED
	THEME_DAYLIGHT,     // Daylight (light)
	THEME_DUO,          // Per-game Duo
	THEME_RETRO,        // Retro Purple
	THEME_CUSTOM,       // user builder (baseHue/accentHue/contrast)
	THEME_PRESET_COUNT  // = number of *fixed* presets (custom is last, generated)
} ThemeId;
#define THEME_FIXED_COUNT 5   // THEME_INDIGO..THEME_RETRO are stored; THEME_CUSTOM is generated

// Persisted UI-chrome preferences (added by the redesign). Kept in a global so main.c,
// touch.c, rompicker.c and wireless.c all read the same values without threading params
// through the (already long) settings_load/save signatures. Loaded/saved via Settings.
typedef struct {
	int theme;            // ThemeId
	int customBaseHue;    // custom builder: background hue   0..360
	int customAccentHue;  // custom builder: accent hue       0..360
	int customContrast;   // custom builder: panel contrast lift 6..24
	int gameMode;         // 0 = dual (two games), 1 = single (one game + touch controller)
	int padColor;         // gamepad tint index into PAD_COLORS[]
	int padEdge;          // 0 = round, 1 = soft, 2 = sharp
	// phase 14 (HD-2D diorama tilt, SPEC-integration I4.8): the SAVED preference, 0 = Off ..
	// TILT_LEVELS-1. Lives here rather than in the settings_load/save parameter list for the
	// documented reason above — zero call-site churn across the ~12 settings_save() calls. The
	// LIVE level is clamped per tier (Old 3DS / link / frameskip / stereo) inside
	// tilt_target_level(); this value is NEVER rewritten by a clamp (I5.1, gen1recomp
	// Game.lua:841-857), so an SD card moved to a New 3DS still has what the user picked.
	// Ships 0 (I5.8): unlike dofOn/bloomOn/lightOn this effect is not hardware-proven yet.
	int tiltLevel;
} UiPrefs;

extern Theme   g_ui;                              // the ACTIVE theme (read by all UI draws)
extern UiPrefs g_prefs;                           // persisted UI-chrome prefs
extern const Theme g_themePresets[THEME_FIXED_COUNT];
extern const char* const THEME_NAMES[THEME_PRESET_COUNT];

// Set g_ui from a theme id (custom hues/contrast used only when id==THEME_CUSTOM).
void  theme_apply(int id, int baseHue, int accentHue, int contrast);
// Build a custom palette from 3 params (exposed for a live preview in the builder).
Theme theme_make_custom(int baseHue, int accentHue, int contrast);

// ---- Fixed role colors (constant across EVERY theme) ----
#define THEME_GAME_A    C2D_Color32(0x63, 0xB2, 0x3C, 0xFF)   // green — slot A / top accent / "A" badge
#define THEME_GAME_B    C2D_Color32(0x3E, 0x86, 0xD6, 0xFF)   // blue  — slot B / bottom accent / "B" badge
#define THEME_3D_TEXT   C2D_Color32(0xA9, 0xD4, 0xFF, 0xFF)   // "3D" chip text
#define THEME_QUIT      C2D_Color32(0xE4, 0x46, 0x2E, 0xFF)   // destructive fill
#define THEME_QUIT_TEXT C2D_Color32(0xE4, 0x79, 0x6B, 0xFF)   // destructive text

// The 5 gamepad tint options (README: gold, green, blue, coral, violet).
#define PAD_COLOR_0 C2D_Color32(0xF5, 0xD0, 0x42, 0xFF)
#define PAD_COLOR_1 C2D_Color32(0x63, 0xB2, 0x3C, 0xFF)
#define PAD_COLOR_2 C2D_Color32(0x3E, 0x86, 0xD6, 0xFF)
#define PAD_COLOR_3 C2D_Color32(0xFF, 0x7A, 0x59, 0xFF)
#define PAD_COLOR_4 C2D_Color32(0xB9, 0x8C, 0xFF, 0xFF)

// ---- Back-compat macros: existing call sites keep writing THEME_*, now routed through g_ui. ----
// (All current uses are LOCAL `const u32 x = THEME_*;` reads — a runtime read is fine there.)
#define THEME_GOLD      (g_ui.acc)
#define THEME_BG        (g_ui.bg)
#define THEME_LETTERBOX (g_ui.box)
#define THEME_PANEL     (g_ui.panel)
#define THEME_PANEL2    (g_ui.panel2)
#define THEME_LINE      (g_ui.line)
#define THEME_TEXT      (g_ui.text)
#define THEME_DIM       (g_ui.dim)
#define THEME_SELTXT    (g_ui.ink)
#define THEME_BEZEL     (g_ui.box)
#define THEME_MENU_DIM  C2D_Color32(0x00, 0x00, 0x00, 0xC0)   // dim overlay behind the pause menu (constant)
#define THEME_HUD_BAR   C2D_Color32(0x00, 0x00, 0x00, 0x90)   // translucent HUD bar (constant)
// Text drawn ON the constant-black scrims/HUD bars/raw game pixels must NOT follow the theme
// (Daylight makes g_ui.text dark, which vanishes there) - use these theme-INVARIANT near-whites.
#define THEME_ON_DARK     C2D_Color32(0xF4, 0xF1, 0xFB, 0xFF)
#define THEME_ON_DARK_DIM C2D_Color32(0xB0, 0xA8, 0xC8, 0xFF)
