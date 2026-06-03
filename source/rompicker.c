// rompicker.c — scan sdmc:/dual-gba for .gba files and pick two (A then B).
// A small scrolling list picker in citro2d (the first piece of the v0.5 UI layer).

#include <3ds.h>
#include <citro2d.h>
#include <citro3d.h>
#include <dirent.h>
#include <string.h>
#include <strings.h>   // strcasecmp
#include <stdio.h>

#include "rompicker.h"

#define MAX_ROMS     128
#define NAME_LEN     128
#define VIS_ROWS     13
#define ROW_H        16.0f

static int scan_roms(char names[][NAME_LEN]) {
	DIR* d = opendir(ROM_DIR);
	if (!d) return 0;
	int n = 0;
	struct dirent* e;
	while ((e = readdir(d)) != NULL && n < MAX_ROMS) {
		const char* nm = e->d_name;
		size_t L = strlen(nm);
		if (L > 4 && strcasecmp(nm + L - 4, ".gba") == 0) {
			strncpy(names[n], nm, NAME_LEN - 1);
			names[n][NAME_LEN - 1] = '\0';
			n++;
		}
	}
	closedir(d);
	return n;
}

bool rompicker_run(C3D_RenderTarget* top, C3D_RenderTarget* bot, C2D_TextBuf txtBuf,
                   char* pathA, char* pathB, size_t cap) {
	static char names[MAX_ROMS][NAME_LEN];
	int n = scan_roms(names);
	if (n == 0) return false;   // nothing to pick -> caller falls back to defaults

	const u32 clrBg     = C2D_Color32(0x20, 0x18, 0x30, 0xFF);  // GBA-nostalgic indigo
	const u32 clrSel    = C2D_Color32(0xF5, 0xD0, 0x42, 0xFF);  // gold highlight
	const u32 clrTxt    = C2D_Color32(0xFF, 0xFF, 0xFF, 0xFF);
	const u32 clrSelTxt = C2D_Color32(0x20, 0x18, 0x30, 0xFF);
	const u32 clrDim    = C2D_Color32(0xB0, 0xA8, 0xC8, 0xFF);

	int phase = 0;        // 0 = pick A, 1 = pick B
	int idxA  = 0;
	int sel   = 0;
	int topRow = 0;

	while (aptMainLoop()) {
		hidScanInput();
		u32 k = hidKeysDown();
		if (k & KEY_START) return false;                       // cancel -> defaults
		if (k & (KEY_DDOWN | KEY_CPAD_DOWN)) sel = (sel + 1) % n;
		if (k & (KEY_DUP   | KEY_CPAD_UP))   sel = (sel - 1 + n) % n;
		if (k & KEY_A) {
			if (phase == 0) { idxA = sel; phase = 1; }
			else {
				snprintf(pathA, cap, "%s/%s", ROM_DIR, names[idxA]);
				snprintf(pathB, cap, "%s/%s", ROM_DIR, names[sel]);
				return true;
			}
		}
		if (k & KEY_B) { if (phase == 1) phase = 0; else return false; }

		// keep selection in the visible window
		if (sel < topRow) topRow = sel;
		if (sel >= topRow + VIS_ROWS) topRow = sel - VIS_ROWS + 1;

		// --- build all text for this frame into the shared buffer (clear once) ---
		C2D_TextBufClear(txtBuf);
		C2D_Text tTitle, tHelp, tA, rows[VIS_ROWS];
		const char* title = (phase == 0) ? "Pick GAME A  (top screen)"
		                                 : "Pick GAME B  (bottom screen)";
		C2D_TextParse(&tTitle, txtBuf, title);                 C2D_TextOptimize(&tTitle);
		const char* help = (phase == 0) ? "Up/Down: move   A: choose   START: defaults"
		                                : "Up/Down: move   A: choose   B: back";
		C2D_TextParse(&tHelp, txtBuf, help);                   C2D_TextOptimize(&tHelp);
		int shown = 0;
		for (int i = 0; i < VIS_ROWS && topRow + i < n; i++) {
			C2D_TextParse(&rows[i], txtBuf, names[topRow + i]); C2D_TextOptimize(&rows[i]); shown++;
		}
		char alabel[160];
		if (phase == 1) {
			snprintf(alabel, sizeof alabel, "Game A: %s", names[idxA]);
			C2D_TextParse(&tA, txtBuf, alabel);               C2D_TextOptimize(&tA);
		}

		C3D_FrameBegin(C3D_FRAME_SYNCDRAW);

		// top: title + scrolling list
		C2D_TargetClear(top, clrBg);
		C2D_SceneBegin(top);
		C2D_DrawText(&tTitle, C2D_WithColor, 8.0f, 6.0f, 0.0f, 0.6f, 0.6f, clrSel);
		for (int i = 0; i < shown; i++) {
			float y = 30.0f + i * ROW_H;
			bool s = (topRow + i == sel);
			if (s) C2D_DrawRectSolid(6.0f, y - 1.0f, 0.0f, 388.0f, ROW_H - 1.0f, clrSel);
			C2D_DrawText(&rows[i], C2D_WithColor, 12.0f, y, 0.0f, 0.5f, 0.5f, s ? clrSelTxt : clrTxt);
		}

		// bottom: chosen Game A (during phase B) + help
		C2D_TargetClear(bot, clrBg);
		C2D_SceneBegin(bot);
		if (phase == 1) C2D_DrawText(&tA, C2D_WithColor, 8.0f, 8.0f, 0.0f, 0.5f, 0.5f, clrSel);
		C2D_DrawText(&tHelp, C2D_WithColor, 8.0f, 214.0f, 0.0f, 0.45f, 0.45f, clrDim);

		C3D_FrameEnd(0);
	}
	return false;
}
