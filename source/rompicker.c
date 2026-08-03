// rompicker.c — boot-time game select + resume prompt (UI redesign v2, 1:1 pass).
// Screens 02 + 03 of design_handoff_3dgba_ui: the LIBRARY lives on the TOP screen
// (title + status + rows with cartridge chips and A/B badges), the BOTTOM (touch)
// screen holds the 1/2-game segmented control, the A/B slot cards, and the START /
// START—LINKED buttons. Selection is d-pad + A (the top screen can't be tapped);
// the bottom's segmented/cards/buttons are tappable.

#include <3ds.h>
#include <citro2d.h>
#include <citro3d.h>
#include <dirent.h>
#include <string.h>
#include <strings.h>   // strcasecmp
#include <stdio.h>
#include <stdlib.h>   // abs

#include "rompicker.h"
#include "theme.h"
#include "ui.h"
#include "assets.h"

#define MAX_ROMS     128
#define NAME_LEN     128
#define VIS_ROWS     13
#define ROW_H        16.0f

// Map the GBA header game code (offset 0xAC) to a friendly name for the games we care about.
static const char* known_game(const char* code) {
	if (!strncmp(code, "BPEE", 4)) return "Pokemon Emerald";
	if (!strncmp(code, "BPRE", 4)) return "Pokemon FireRed";
	if (!strncmp(code, "BPGE", 4)) return "Pokemon LeafGreen";
	if (!strncmp(code, "AXVE", 4)) return "Pokemon Ruby";
	if (!strncmp(code, "AXPE", 4)) return "Pokemon Sapphire";
	return NULL;
}

void rom_display_name(const char* path, char* out, size_t cap) {
	unsigned char h[0xB0];
	FILE* f = fopen(path, "rb");
	if (f) {
		size_t got = fread(h, 1, sizeof h, f);
		fclose(f);
		if (got == sizeof h) {
			char code[5] = {0};
			memcpy(code, h + 0xAC, 4);
			const char* k = known_game(code);
			if (k) { snprintf(out, cap, "%s", k); return; }
			char title[13] = {0};
			memcpy(title, h + 0xA0, 12);
			for (int i = 11; i >= 0; --i) {            // trim trailing spaces/junk
				if (title[i] == ' ' || (unsigned char)title[i] < 0x20) title[i] = '\0';
				else break;
			}
			if (title[0] >= 0x20) { snprintf(out, cap, "%s", title); return; }
		}
	}
	// fall back to the filename without its .gba extension
	const char* base = strrchr(path, '/');
	base = base ? base + 1 : path;
	snprintf(out, cap, "%s", base);
	size_t L = strlen(out);
	if (L > 4 && strcasecmp(out + L - 4, ".gba") == 0) out[L - 4] = '\0';
}

// Read the 4-char game code (header offset 0xAC); "----" when unreadable.
static void rom_game_code(const char* path, char code[5]) {
	memcpy(code, "----", 5);
	FILE* f = fopen(path, "rb");
	if (!f) return;
	unsigned char h[0xB0];
	if (fread(h, 1, sizeof h, f) == sizeof h) { memcpy(code, h + 0xAC, 4); code[4] = '\0'; }
	fclose(f);
	for (int i = 0; i < 4; i++) if ((unsigned char)code[i] < 0x20 || (unsigned char)code[i] > 0x7E) code[i] = '-';
}

static int scan_roms(char names[][NAME_LEN], char disp[][NAME_LEN], char codes[][8]) {
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
			char full[256];
			snprintf(full, sizeof full, "%s/%s", ROM_DIR, nm);
			rom_display_name(full, disp[n], NAME_LEN);
			rom_game_code(full, codes[n]);
			n++;
		}
	}
	closedir(d);
	return n;
}

// Stable per-game tint for the cartridge chips (hash the game code onto the 5 pad tints).
static u32 cart_tint(const char* code) {
	const u32 cols[5] = { PAD_COLOR_1, PAD_COLOR_2, PAD_COLOR_0, PAD_COLOR_3, PAD_COLOR_4 };
	return cols[((unsigned char)code[0] + (unsigned char)code[3]) % 5];
}

#define RECENT_PATH "sdmc:/3DGBA/recent.bin"
typedef struct { char a[256]; char b[256]; } RecentPair;

void rompicker_save_recent(const char* pathA, const char* pathB) {
	RecentPair r;
	snprintf(r.a, sizeof r.a, "%s", pathA);
	snprintf(r.b, sizeof r.b, "%s", pathB);
	FILE* f = fopen(RECENT_PATH, "wb");
	if (!f) return;
	fwrite(&r, 1, sizeof r, f);
	fclose(f);
}

// Load the saved pairing into r; true only if the files still exist (b=="" = a 1-game pairing).
static bool load_recent(RecentPair* r) {
	FILE* f = fopen(RECENT_PATH, "rb");
	if (!f) return false;
	size_t n = fread(r, 1, sizeof *r, f);
	fclose(f);
	if (n != sizeof *r) return false;
	r->a[sizeof r->a - 1] = '\0';
	r->b[sizeof r->b - 1] = '\0';
	FILE* fa = fopen(r->a, "rb"); if (!fa) return false; fclose(fa);
	if (r->b[0]) { FILE* fb = fopen(r->b, "rb"); if (!fb) return false; fclose(fb); }
	return true;
}

// A slot card (game select + resume): full colored border, caps label, chip + name.
static void slot_card(C2D_TextBuf buf, float x, float y, float w, float h,
                      u32 col, const char* label, const char* name, int hasGame) {
	ui_panel(x, y, w, h, g_ui.panel, hasGame ? col : g_ui.line, 4.0f);
	ui_text(buf, label, x + 10.0f, y + 5.0f, 0.32f, hasGame ? col : g_ui.dim);
	if (hasGame) ui_fill(x + 10.0f, y + 21.0f, 10.0f, 12.0f, col, 2.0f);
	ui_text(buf, name, x + (hasGame ? 26.0f : 10.0f), y + 20.0f, 0.45f, hasGame ? g_ui.text : g_ui.dim);
}

// Boot prompt offering the last pairing (screen 02). 1 = use recent, 0 = pick new, -1 = defaults.
static int recent_prompt(C3D_RenderTarget* top, C3D_RenderTarget* bot, C2D_TextBuf txtBuf,
                         const RecentPair* r) {
	char na[NAME_LEN], nb[NAME_LEN];
	rom_display_name(r->a, na, sizeof na);
	if (r->b[0]) rom_display_name(r->b, nb, sizeof nb);
	else         snprintf(nb, sizeof nb, "(single mode)");

	while (aptMainLoop()) {
		hidScanInput();
		u32 k = hidKeysDown(), kHeld = hidKeysHeld(), kUp = hidKeysUp();
		if (k & KEY_A)     return 1;
		if (k & (KEY_X | KEY_B)) return 0;
		if (k & KEY_START) return -1;
		if (k & KEY_TOUCH) {                       // manifest resume-bot button rects
			touchPosition tp; hidTouchRead(&tp);
			if (tp.px >= 20 && tp.px < 300) {
				if (tp.py >= 36  && tp.py < 80)  return 1;    // Resume (primary)
				if (tp.py >= 89  && tp.py < 132) return 0;    // Pick new (secondary)
				if (tp.py >= 143 && tp.py < 178) return -1;   // Use defaults (ghost)
			}
		}

		C2D_TextBufClear(txtBuf);
		C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
		// TOP: chrome plate (heading + A/B cards baked) + the two dynamic game names.
		C2D_TargetClear(top, g_ui.bg); C2D_SceneBegin(top);
		assets_draw_plate("resume-top");
		assets_text(txtBuf, FNT_SG_MED, na, 42.0f, 120.0f, 13.0f, g_ui.text);
		assets_text(txtBuf, FNT_SG_MED, nb, 236.0f, 120.0f, 13.0f, r->b[0] ? g_ui.text : g_ui.dim);
		// BOTTOM: chrome plate + the three button widgets with labels.
		C2D_TargetClear(bot, g_ui.bg); C2D_SceneBegin(bot);
		assets_draw_plate("resume-bot");
		assets_button(txtBuf, "btn-primary",   20.0f, 36.0f,  280.0f, 44.0f, "Resume this pairing", FNT_SG_BOLD, 13.0f, g_ui.ink, 0);
		assets_button(txtBuf, "btn-secondary", 20.0f, 89.0f,  280.0f, 43.0f, "Pick new games",      FNT_SG_BOLD, 13.0f, g_ui.text, 0);
		assets_button(txtBuf, "btn-ghost",     20.0f, 143.0f, 280.0f, 35.0f, "Use defaults",        FNT_SG_MED, 12.0f, g_ui.dim, 0);
		C3D_FrameEnd(0);
	}
	return -1;
}

bool rompicker_run(C3D_RenderTarget* top, C3D_RenderTarget* bot, C2D_TextBuf txtBuf,
                   char* pathA, char* pathB, size_t cap, bool* startLinked) {
	if (startLinked) *startLinked = false;
	RecentPair recent;
	if (load_recent(&recent)) {
		int choice = recent_prompt(top, bot, txtBuf, &recent);
		if (choice == 1) { snprintf(pathA, cap, "%s", recent.a); snprintf(pathB, cap, "%s", recent.b); return true; }
		if (choice == -1) return false;
	}
	static char names[MAX_ROMS][NAME_LEN];
	static char disp[MAX_ROMS][NAME_LEN];
	static char codes[MAX_ROMS][8];
	int n = scan_roms(names, disp, codes);
	if (n == 0) return false;

	// manifest select-*: TOP list x16 y37 w369 h197 (8 rows); BOTTOM seg + slot names + 2 buttons.
	const int LIST_ROWS = 8;
	const float LIST_Y = 41.0f, ROWH = 23.5f;
	int idxA = -1, idxB = -1, sel = 0, topRow = 0;
	int dragY0 = 0, dragTop0 = 0; bool dragging = false;   // touch drag-to-scroll the list

	while (aptMainLoop()) {
		hidScanInput();
		u32 k = hidKeysDown(), kHeld = hidKeysHeld(), kUp = hidKeysUp();
		bool single = (g_prefs.gameMode == 1);
		bool ready = single ? (idxA >= 0) : (idxA >= 0 && idxB >= 0);
		if (k & KEY_START) return false;
		if (k & KEY_ZR) { snprintf(pathA, cap, "%s", "__SETTINGS__"); return true; }   // open settings (no game)
		if (k & KEY_Y) { g_prefs.gameMode ^= 1; if (g_prefs.gameMode == 1) idxB = -1; }
		if (k & (KEY_DDOWN | KEY_CPAD_DOWN)) sel = (sel + 1) % n;
		if (k & (KEY_DUP   | KEY_CPAD_UP))   sel = (sel - 1 + n) % n;
		bool confirmRow = (k & KEY_A) != 0;
		bool doStart = false, doLinked = false;
		if ((k & KEY_X) && ready) doStart = true;
		if ((k & KEY_SELECT) && ready) doLinked = true;
		if (k & KEY_B) { if (idxB >= 0) idxB = -1; else if (idxA >= 0) idxA = -1; else return false; }
		// Touch: a vertical DRAG on the bottom screen scrolls the top list; a clean TAP (no drag)
		// hits the mode segmented / slot cards / action buttons. (The list lives on the top screen,
		// which the 3DS can't touch, so the bottom screen is the scroll+action surface.)
		touchPosition tp; hidTouchRead(&tp);
		if (k & KEY_TOUCH) { dragY0 = tp.py; dragTop0 = topRow; dragging = false; }
		if (kHeld & KEY_TOUCH) {
			if (abs((int)tp.py - dragY0) > 6) dragging = true;
			if (dragging) { int nr = dragTop0 + (dragY0 - (int)tp.py) / 12;
			                if (nr < 0) nr = 0; if (nr > n - 1) nr = n - 1;
			                topRow = (nr > n - LIST_ROWS) ? (n > LIST_ROWS ? n - LIST_ROWS : 0) : nr; }
		}
		if ((kUp & KEY_TOUCH) && !dragging) {              // a clean tap
			if (tp.py >= 214 && tp.px < 96) { snprintf(pathA, cap, "%s", "__SETTINGS__"); return true; }   // settings corner
			else if (tp.py < 42) { g_prefs.gameMode = (tp.px < 160) ? 1 : 0; if (g_prefs.gameMode == 1) idxB = -1; }
			else if (tp.py >= 174) {
				single = (g_prefs.gameMode == 1);
				ready = single ? (idxA >= 0) : (idxA >= 0 && idxB >= 0);
				if (ready) { if (tp.px < (single ? 142 : 136)) doStart = true; else doLinked = true; }
			}
		}
		if (confirmRow) { if (idxA < 0 || single) idxA = sel; else idxB = sel; }
		if (doStart || doLinked) {
			single = (g_prefs.gameMode == 1);
			if (idxA >= 0 && (single || idxB >= 0)) {
				snprintf(pathA, cap, "%s/%s", ROM_DIR, names[idxA]);
				if (single) snprintf(pathB, cap, "%s", ""); else snprintf(pathB, cap, "%s/%s", ROM_DIR, names[idxB]);
				if (startLinked) *startLinked = doLinked;
				return true;
			}
			doStart = doLinked = false;
		}
		if (!dragging) { if (sel < topRow) topRow = sel; if (sel >= topRow + LIST_ROWS) topRow = sel - LIST_ROWS + 1; }
		single = (g_prefs.gameMode == 1);
		ready = single ? (idxA >= 0) : (idxA >= 0 && idxB >= 0);

		C2D_TextBufClear(txtBuf);
		C3D_FrameBegin(C3D_FRAME_SYNCDRAW);

		// ===== TOP: plate + dynamic "NOW PICKING" + the ROM list rows =====
		C2D_TargetClear(top, g_ui.bg); C2D_SceneBegin(top);
		assets_draw_plate(single ? "select-single-top" : "select-dual-top");
		{
			const char* pk = ready ? "READY" : (single ? "1 GAME" : (idxA < 0 ? "A · TOP" : "B · BOTTOM"));
			assets_text_r(txtBuf, FNT_JBM_MED, pk, 384.0f, 15.0f, 9.0f, g_ui.acc);
		}
		for (int i = 0; i < LIST_ROWS && topRow + i < n; i++) {
			int gi = topRow + i;
			float y = LIST_Y + i * ROWH;
			if (gi == sel) assets_fill9("fill-card-r8", 12.0f, y - 1.0f, 377.0f, ROWH - 2.0f, 6.0f);
			assets_draw_wgt("cart-tinted", 18.0f, y + 1.0f);
			assets_text(txtBuf, FNT_SG_MED, disp[gi], 40.0f, y + 3.0f, 12.0f, g_ui.text);
			if (gi == idxA)      assets_draw_wgt("badge-a", 360.0f, y + 2.0f);
			else if (gi == idxB) assets_draw_wgt("badge-b", 360.0f, y + 2.0f);
			else                 assets_text_r(txtBuf, FNT_JBM_MED, codes[gi], 384.0f, y + 4.0f, 8.5f, g_ui.dim);
		}

		// ===== BOTTOM: plate + mode segmented + slot name(s) + START / LINKED =====
		C2D_TargetClear(bot, g_ui.bg); C2D_SceneBegin(bot);
		assets_draw_plate(single ? "select-single-bot" : "select-dual-bot");
		{
			static const char* const MODES[2] = { "1 Game", "2 Games" };
			assets_seg(txtBuf, 12.0f, 12.0f, 296.0f, 30.0f, MODES, 2, single ? 0 : 1, g_ui.ink, g_ui.dim);
		}
		if (single) {
			assets_text(txtBuf, FNT_SG_MED, idxA >= 0 ? disp[idxA] : "pick a game (d-pad + A)", 49.0f, 81.0f, 13.0f, idxA >= 0 ? g_ui.text : g_ui.dim);
			assets_button(txtBuf, "btn-primary",        12.0f,  175.0f, 130.0f, 37.0f, "START", FNT_SG_BOLD, 13.0f, ready ? g_ui.ink : g_ui.dim, 0);
			assets_button(txtBuf, "btn-accent-outline", 150.0f, 175.0f, 158.0f, 37.0f, "LINK A FRIEND", FNT_SG_BOLD, 12.0f, ready ? g_ui.acc : g_ui.dim, 0);
		} else {
			assets_text(txtBuf, FNT_SG_MED, idxA >= 0 ? disp[idxA] : "pick game A", 47.0f, 78.0f, 12.0f, idxA >= 0 ? g_ui.text : g_ui.dim);
			assets_text(txtBuf, FNT_SG_MED, idxB >= 0 ? disp[idxB] : "pick game B", 47.0f, 139.0f, 12.0f, idxB >= 0 ? g_ui.text : g_ui.dim);
			assets_button(txtBuf, "btn-primary",        12.0f,  174.0f, 124.0f, 37.0f, "START", FNT_SG_BOLD, 13.0f, ready ? g_ui.ink : g_ui.dim, 0);
			assets_button(txtBuf, "btn-accent-outline", 144.0f, 174.0f, 164.0f, 37.0f, "START — LINKED", FNT_SG_BOLD, 12.0f, ready ? g_ui.acc : g_ui.dim, 0);
		}
		ui_fill(6.0f, 216.0f, 84.0f, 16.0f, g_ui.panel2, 5.0f);   // discoverable settings affordance (ZR / tap)
		assets_text(txtBuf, FNT_JBM_MED, "settings · ZR", 12.0f, 219.0f, 8.0f, g_ui.dim);
		C3D_FrameEnd(0);
	}
	return false;
}

// ---- .sav loader: a single-list picker over *.sav in ROM_DIR ----------------
static int scan_ext(char names[][NAME_LEN], const char* ext) {
	DIR* d = opendir(ROM_DIR);
	if (!d) return 0;
	int n = 0;
	size_t el = strlen(ext);
	struct dirent* e;
	while ((e = readdir(d)) != NULL && n < MAX_ROMS) {
		const char* nm = e->d_name;
		size_t L = strlen(nm);
		if (L > el && strcasecmp(nm + L - el, ext) == 0) {
			strncpy(names[n], nm, NAME_LEN - 1);
			names[n][NAME_LEN - 1] = '\0';
			n++;
		}
	}
	closedir(d);
	return n;
}

bool savpicker_run(C3D_RenderTarget* top, C3D_RenderTarget* bot, C2D_TextBuf txtBuf,
                   char* out, size_t cap) {
	static char names[MAX_ROMS][NAME_LEN];
	int n = scan_ext(names, ".sav");
	if (n == 0) return false;   // no .sav files present

	const u32 clrBg     = THEME_BG;
	const u32 clrSel    = THEME_GOLD;
	const u32 clrTxt    = THEME_TEXT;
	const u32 clrSelTxt = THEME_BG;
	const u32 clrDim    = THEME_DIM;

	int sel = 0, topRow = 0;
	while (aptMainLoop()) {
		hidScanInput();
		u32 k = hidKeysDown(), kHeld = hidKeysHeld(), kUp = hidKeysUp();
		if (k & (KEY_B | KEY_START)) return false;
		if (k & (KEY_DDOWN | KEY_CPAD_DOWN)) sel = (sel + 1) % n;
		if (k & (KEY_DUP   | KEY_CPAD_UP))   sel = (sel - 1 + n) % n;
		if (k & KEY_A) { snprintf(out, cap, "%s/%s", ROM_DIR, names[sel]); return true; }

		if (sel < topRow) topRow = sel;
		if (sel >= topRow + VIS_ROWS) topRow = sel - VIS_ROWS + 1;

		C2D_TextBufClear(txtBuf);
		C2D_Text tTitle, tHelp, rows[VIS_ROWS];
		C2D_TextParse(&tTitle, txtBuf, "Pick a .sav to load");            C2D_TextOptimize(&tTitle);
		C2D_TextParse(&tHelp,  txtBuf, "Up/Down: move   A: load   B: cancel"); C2D_TextOptimize(&tHelp);
		int shown = 0;
		for (int i = 0; i < VIS_ROWS && topRow + i < n; i++) {
			C2D_TextParse(&rows[i], txtBuf, names[topRow + i]); C2D_TextOptimize(&rows[i]); shown++;
		}

		C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
		C2D_TargetClear(top, clrBg);
		C2D_SceneBegin(top);
		C2D_DrawText(&tTitle, C2D_WithColor, 8.0f, 6.0f, 0.0f, 0.6f, 0.6f, clrSel);
		for (int i = 0; i < shown; i++) {
			float y = 30.0f + i * ROW_H;
			bool s = (topRow + i == sel);
			if (s) C2D_DrawRectSolid(6.0f, y - 1.0f, 0.0f, 388.0f, ROW_H - 1.0f, clrSel);
			C2D_DrawText(&rows[i], C2D_WithColor, 12.0f, y, 0.0f, 0.5f, 0.5f, s ? clrSelTxt : clrTxt);
		}
		C2D_TargetClear(bot, clrBg);
		C2D_SceneBegin(bot);
		C2D_DrawText(&tHelp, C2D_WithColor, 8.0f, 214.0f, 0.0f, 0.45f, 0.45f, clrDim);
		C3D_FrameEnd(0);
	}
	return false;
}
