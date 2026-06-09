// theme.h — shared GBA-nostalgic UI palette (indigo + gold).
// One source of truth for colors used across the ROM picker, pause menu, HUD, splash,
// and touch overlay, so the look stays consistent.
#pragma once
#include <citro2d.h>

#define THEME_GOLD      C2D_Color32(0xF5, 0xD0, 0x42, 0xFF)   // primary accent / highlight
#define THEME_BG        C2D_Color32(0x20, 0x18, 0x30, 0xFF)   // indigo (picker / splash background)
#define THEME_LETTERBOX C2D_Color32(0x10, 0x10, 0x10, 0xFF)   // near-black behind the game frames
#define THEME_PANEL     C2D_Color32(0x2a, 0x20, 0x42, 0xFF)   // menu panel
#define THEME_TEXT      C2D_Color32(0xFF, 0xFF, 0xFF, 0xFF)   // primary text
#define THEME_DIM       C2D_Color32(0xB0, 0xA8, 0xC8, 0xFF)   // lavender (help / secondary text)
#define THEME_SELTXT    C2D_Color32(0x20, 0x18, 0x30, 0xFF)   // text drawn on a gold highlight
#define THEME_MENU_DIM  C2D_Color32(0x00, 0x00, 0x00, 0xC0)   // dim overlay behind the pause menu
#define THEME_HUD_BAR   C2D_Color32(0x00, 0x00, 0x00, 0x90)   // translucent HUD bar

// Splash secondary palette (mini GBA "screens").
#define THEME_GAME_A    C2D_Color32(0x4a, 0x7a, 0x2a, 0xFF)   // green (game A panel)
#define THEME_GAME_B    C2D_Color32(0x2a, 0x60, 0x96, 0xFF)   // blue  (game B panel)
#define THEME_BEZEL     C2D_Color32(0x12, 0x0e, 0x1c, 0xFF)   // dark screen bezel
