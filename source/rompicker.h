// rompicker.h — boot-time ROM picker.
// Lists *.gba in sdmc:/dual-gba and lets the user choose two games (A = top screen,
// B = bottom). Writes full sdmc paths into pathA/pathB (each >= cap bytes). Returns
// false if cancelled (START) or no ROMs found, so the caller can fall back to defaults.
// Requires gfx + citro2d already initialized; renders on the passed targets.
#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <citro2d.h>
#include <citro3d.h>

#define ROM_DIR "sdmc:/dual-gba"

bool rompicker_run(C3D_RenderTarget* top, C3D_RenderTarget* bot, C2D_TextBuf txtBuf,
                   char* pathA, char* pathB, size_t cap);
