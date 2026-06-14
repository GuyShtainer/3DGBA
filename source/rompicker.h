// rompicker.h — boot-time ROM picker.
// Lists *.gba in sdmc:/3DGBA and lets the user choose two games (A = top screen,
// B = bottom). Writes full sdmc paths into pathA/pathB (each >= cap bytes). Returns
// false if cancelled (START) or no ROMs found, so the caller can fall back to defaults.
// Requires gfx + citro2d already initialized; renders on the passed targets.
#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <citro2d.h>
#include <citro3d.h>

#define ROM_DIR "sdmc:/3DGBA"

bool rompicker_run(C3D_RenderTarget* top, C3D_RenderTarget* bot, C2D_TextBuf txtBuf,
                   char* pathA, char* pathB, size_t cap);

// Derive a friendly name for a .gba file from its header (known Gen-3 codes, else the
// internal 12-char title, else the filename). `path` is a full sdmc path. Used by the
// picker and the in-game HUD label.
void rom_display_name(const char* path, char* out, size_t cap);

// Remember the last A+B pairing so the picker can offer a one-button resume next boot.
void rompicker_save_recent(const char* pathA, const char* pathB);

// Pick a .sav file from sdmc:/3DGBA to load into a running game. Writes the full path to
// `out`; returns false if cancelled or none found.
bool savpicker_run(C3D_RenderTarget* top, C3D_RenderTarget* bot, C2D_TextBuf txtBuf,
                   char* out, size_t cap);
