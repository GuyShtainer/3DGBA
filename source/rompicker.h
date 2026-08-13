// rompicker.h — boot-time ROM picker.
// Lists *.gba in sdmc:/3DGBA and lets the user choose two games (A = top screen,
// B = bottom). Writes full sdmc paths into pathA/pathB (each >= cap bytes). Returns
// false if cancelled (START) or no ROMs found, so the caller can fall back to defaults.
// Requires gfx + citro2d already initialized; renders on the passed targets.
#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <citro2d.h>
#include <citro3d.h>

#define ROM_DIR "sdmc:/3DGBA"

// ---- harness test seam (phase-17 SPEC-input §0.2.1) -------------------------------------------
// The picker's whole state is stack-local, so the emutest harness could not read it over GDB and
// could not prove a tap landed — which is exactly why REPORT D2 (every tap silently resolving to
// "1 Game") survived to a shipped build. This mirror is written ONCE PER FRAME at the end of the
// picker's input block and is LOGGING ONLY: nothing reads it back, no branch depends on it. It is
// present in the shipping build on purpose — the harness IS the regression gate, and a debug-only
// symbol would be untestable in the artifact the user installs.
// Offsets are contract (gdbio reads them by number): magic 0x00 … startN 0x34.
typedef struct {
	int32_t magic;      // 0x00  'PIK1' = 0x50494B31 — proves the symbol/offsets are the ones you think
	int32_t frame;      // 0x04  picker frame counter (liveness)
	int32_t sel;        // 0x08  highlighted ROM row
	int32_t topRow;     // 0x0c  first visible row = the scroll offset
	int32_t nRoms;      // 0x10  rows scanned
	int32_t idxA;       // 0x14  slot A row, -1 = empty
	int32_t idxB;       // 0x18  slot B row, -1 = empty
	int32_t mode;       // 0x1c  mirror of g_prefs.gameMode
	int32_t lastHit;    // 0x20  PickTarget of the last resolved tap, -1 = none
	int32_t lastTap;    // 0x24  packed (x << 16) | y of the last LATCHED touch point
	int32_t tapN;       // 0x28  taps resolved (drag-rejected gestures not counted)
	int32_t dragN;      // 0x2c  drags that crossed the threshold
	int32_t dragRows;   // 0x30  rows moved by the current/last drag (signed)
	int32_t startN;     // 0x34  START / START—LINKED activations
	// PHASE 18 FIX PASS (review finding 2). APPENDED — every offset above is unchanged.
	// The boot "Resume this pairing" prompt returns recent.a/recent.b straight to main() and
	// never passes through the picker's same-file refusal, so a recent.bin written by a pre-fix
	// build could put ONE FILE IN BOTH SLOTS with one tap. `recentDup` counts the times
	// load_recent has caught and degraded that pairing; `recentB` is 1 when the loaded pairing
	// still has a second game after the guard. Both survive a "Resume" (which returns before
	// g_pickDiag is cleared), which is what makes the fix provable from outside the console.
	int32_t recentDup;  // 0x38  same-file pairings degraded to one game (0 = the file was clean)
	int32_t recentB;    // 0x3c  1 = the loaded pairing still names a second game
} PickDiag;
extern PickDiag g_pickDiag;

// startLinked (optional out): true when the user chose "START - LINKED" (attach the in-process
// link cable at session start). In 1-game mode (g_prefs.gameMode == 1) pathB is set to "".
bool rompicker_run(C3D_RenderTarget* top, C3D_RenderTarget* bot, C2D_TextBuf txtBuf,
                   char* pathA, char* pathB, size_t cap, bool* startLinked);

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
