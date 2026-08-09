// rompicker.c — boot-time game select + resume prompt (UI redesign v2, 1:1 pass).
// Screens 02 + 03 of design_handoff_3dgba_ui: the LIBRARY lives on the TOP screen
// (title + status + rows with cartridge chips and A/B badges), the BOTTOM (touch)
// screen holds the 1/2-game segmented control, the A/B slot cards, and the START /
// START—LINKED buttons.
//
// PHASE 17 (SPEC-input I1/I3): input is now uihit-driven. Every rect below is DRAWN from the same
// uihit_pick_rect() the tap is hit-tested against (§0.1.3), the touch point is latched while the
// touch is valid and only acted on via GEST_TAP (§I1.2 — the release-edge read was REPORT D2), and
// every bottom-screen control is reachable by d-pad + A with a white focus ring (§I3.2 / Q2), not
// just by its hidden Y/X/SELECT/ZR shortcut.

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
#include "uihit.h"
#include "assets.h"

// The harness's window into the picker (rompicker.h §0.2.1). Logging only.
PickDiag g_pickDiag;

// The app's focus ring colour (same value assets_button draws for `focus`).
#define FOCUS_COL C2D_Color32(0xFF, 0xFF, 0xFF, 0xE0)

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

// Stable per-game tint for the cartridge chips (hash the game code onto a fixed palette).
// PHASE 17 / SPEC-layout L8.1.1 (sweep D20): the palette USED to be all five PAD_COLOR_* tints,
// two of which are the app's fixed ROLE colours — PAD_COLOR_1 is the Game-A green and PAD_COLOR_2
// the Game-B blue, the exact colours the "A"/"B" badges are drawn in at x=360 on the same row. A
// cart cap could therefore impersonate a slot badge. The three remaining tints (gold / coral /
// violet) are role-free, so a cap can never be mistaken for an assignment.
static u32 cart_tint(const char* code) {
	const u32 cols[3] = { PAD_COLOR_0, PAD_COLOR_3, PAD_COLOR_4 };
	// SEEN IN CAPTURE, and the reason this is not the spec's `code[0] + code[3]`: in a real GBA
	// game code, char 0 is the type ('A'/'B') and char 3 the region ('E' for every US release), so
	// that hash gave *every* US cartridge the same colour — Emerald (BPEE) and FireRed (BPRE) came
	// out identical in runs/p17-f5-rom/top_00150.png. Chars 1-2 are the title, which is what
	// actually distinguishes titles, so weight all four (FNV-style, so an ordering swap differs).
	unsigned h = 2166136261u;
	for (int i = 0; i < 4 && code[i]; i++) h = (h ^ (unsigned char)code[i]) * 16777619u;
	return cols[(h >> 8) % 3u];
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
	ui_text(buf, label, x + 10.0f, y + 5.0f, 0.32f, hasGame ? col : g_art.dim);
	if (hasGame) ui_fill(x + 10.0f, y + 21.0f, 10.0f, 12.0f, col, 2.0f);
	ui_text(buf, name, x + (hasGame ? 26.0f : 10.0f), y + 20.0f, 0.45f, hasGame ? g_art.text : g_art.dim);
}

// Boot prompt offering the last pairing (screen 02). 1 = use recent, 0 = pick new, -1 = defaults.
static int recent_prompt(C3D_RenderTarget* top, C3D_RenderTarget* bot, C2D_TextBuf txtBuf,
                         const RecentPair* r) {
	char na[NAME_LEN], nb[NAME_LEN];
	rom_display_name(r->a, na, sizeof na);
	if (r->b[0]) rom_display_name(r->b, nb, sizeof nb);
	else         snprintf(nb, sizeof nb, "(single mode)");

	// manifest resume-bot button rects — DRAWN from this table too, so draw == hit (§0.1.3).
	static const UiRect BTN[3] = { { 20, 36, 280, 44 }, { 20, 89, 280, 43 }, { 20, 143, 280, 35 } };
	static const int    RET[3] = { 1, 0, -1 };          // Resume / Pick new / Use defaults
	int sel = 0;                                        // I3.1: the prompt had NO focus at all
	UiGesture gest; memset(&gest, 0, sizeof gest);

	while (aptMainLoop()) {
		hidScanInput();
		u32 k = hidKeysDown(), kHeld = hidKeysHeld(), kUp = hidKeysUp();
		if (k & (KEY_DDOWN | KEY_CPAD_DOWN)) sel = (sel + 1) % 3;
		if (k & (KEY_DUP   | KEY_CPAD_UP))   sel = (sel + 2) % 3;
		if (k & KEY_A)     return RET[sel];
		if (k & (KEY_X | KEY_B)) return 0;
		if (k & KEY_START) return -1;
		{	// the latch idiom (§I1.2.2): raw is consumed ONLY while the point is valid
			touchPosition raw; hidTouchRead(&raw);
			UiGestEv ev = uihit_gesture_step(&gest, (k & KEY_TOUCH) != 0,
			                                 (kHeld & KEY_TOUCH) != 0, (kUp & KEY_TOUCH) != 0,
			                                 raw.px, raw.py);
			if (ev == GEST_TAP) {
				int i = uihit_index(BTN, 3, gest.x, gest.y);
				if (i >= 0) return RET[i];
			}
		}

		C2D_TextBufClear(txtBuf);
		g_renderSeq++;   // app liveness for the emutest harness (ui.h)
		C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
		// TOP: chrome plate (heading + A/B cards baked) + the two dynamic game names.
		C2D_TargetClear(top, g_ui.bg); C2D_SceneBegin(top);
		assets_draw_plate("resume-top");
		assets_text(txtBuf, FNT_SG_MED, na, 42.0f, 120.0f, 13.0f, g_art.text);
		assets_text(txtBuf, FNT_SG_MED, nb, 236.0f, 120.0f, 13.0f, r->b[0] ? g_art.text : g_art.dim);
		// BOTTOM: chrome plate + the three button widgets with labels.
		C2D_TargetClear(bot, g_ui.bg); C2D_SceneBegin(bot);
		assets_draw_plate("resume-bot");
		assets_button(txtBuf, "btn-primary",   BTN[0].x, BTN[0].y, BTN[0].w, BTN[0].h, "Resume this pairing", FNT_SG_BOLD, 13.0f, g_art.ink,  sel == 0);
		assets_button(txtBuf, "btn-secondary", BTN[1].x, BTN[1].y, BTN[1].w, BTN[1].h, "Pick new games",      FNT_SG_BOLD, 13.0f, g_art.text, sel == 1);
		assets_button(txtBuf, "btn-ghost",     BTN[2].x, BTN[2].y, BTN[2].w, BTN[2].h, "Use defaults",        FNT_SG_MED,  12.0f, g_art.dim,  sel == 2);
		C3D_FrameEnd(0);
	}
	return -1;
}

// The picker's d-pad focus chain (§I3.2, answering Q2 the way the task asked: "every target also
// reachable by d-pad + A with a visible focus"). The top-screen list is ONE stop on the chain —
// inside it up/down move the highlight and only leave at the ends — then each bottom-screen
// control in turn. Circular; SLOT_B is skipped in single mode, where it does not exist.
enum { NAV_LIST = 0, NAV_MODE, NAV_SLOTA, NAV_SLOTB, NAV_ACT, NAV_SET, NAV_COUNT };

static int nav_step(int row, int dir, int single) {
	do { row = (row + dir + NAV_COUNT) % NAV_COUNT; } while (single && row == NAV_SLOTB);
	return row;
}

// A focus ring on a uihit rect — the same white the button sprites use, so the two focus idioms
// (button sprite vs code-drawn control) read identically. Drawn 2 px OUTSIDE the control so the
// ring lands on the plate rather than on the control's own fill: the settings chip's fill is
// near-white in the light themes, and a white ring straddling its edge was nearly invisible there
// (SEEN in runs/p17-f2-targets/bottom_00134.png). On the plate it reads in all six themes.
static void focus_ring(UiRect r, float rad) {
	ui_border_round((float)r.x - 2.0f, (float)r.y - 2.0f, (float)r.w + 4.0f, (float)r.h + 4.0f,
	                FOCUS_COL, 1.5f, rad);
}

// ---- PHASE 17 / SPEC-layout L2 (REPORT D7): the picker footer -----------------------------------
// The plate BAKES its footer hint at rows 219..229 (measured per-pixel on select-dual-bot: rows
// 198..218 and 230..239 are pure background, 219..229 carry "tap a game above ↑ · ⚡ linked = trade
// / battle-ready"), and the settings chip was drawn straight on top of it — the hint read
// "…ove ↑ · ⚡ linked = …" with its first two words under an opaque pill. There is no free 18 px
// band on the plate to move the chip into: START/LINKED end at y=211 and the screen ends at 239.
//
// So the footer is RECOMPOSED, at runtime, from the plate's own pixels:
//   1. blank rows 209..239 by stretching a verified-empty slice of THIS plate (not g_ui.bg — only
//      the indigo art pack is baked today, so on any other theme a token fill would be a visibly
//      different colour from the plate it sits on; this is the menu_plate_bg idiom from slice F3),
//   2. re-blit the baked hint band 10 px higher, so it survives verbatim — glyphs, colours and all
//      six themes' art — instead of being retyped in code with a "↑" and a "⚡" the bcfnt bake does
//      not contain,
//   3. leaving rows 221..238 free for the chip, which is then drawn at exactly its uihit rect.
// The re-blit's top rows land under the START buttons, which are drawn after it, so the two cannot
// fight; the hint's ink (plate rows 221..228) lands at 211..218, clear of both.
// Measured band: dual has ink on rows 219..229 (bulk 221..228), single on 220..229. Rows 212..239
// are the only space the buttons do not already own (START/LINKED end at y=211), and 28 rows hold
// the 10-row hint plus a 16-row chip with 2 px between them. Lifting by 8 puts the hint at
// 212..221 — clear of the buttons, which is why the band is drawn AFTER them (an earlier lift of 10
// ran the hint's top rows under the button fills and the glyph tops came out clipped: SEEN in
// runs/p17-f4-picker/bottom_00090.png).
#define PICK_FOOT_Y      212.0f    // top of the recomposed footer band
#define PICK_HINT_SRC_Y  220.0f    // the baked hint's first inked row on the plate
#define PICK_HINT_H       10.0f
#define PICK_HINT_LIFT     8.0f    // how far up the hint moves
// Step 1+2, drawn AFTER the widgets: the band starts at y=212 and the buttons end at y=211, so it
// overlaps nothing, and drawing it last means the lifted hint can never be half-covered by a fill.
// `withHint` = re-blit the baked "tap a game above ↑ …" line (step 2). FIX PASS (review finding 7):
// the ROM-less empty state must pass 0 — it exists to say there is nothing to tap, and re-blitting
// that sentence under "Rescan / Start without a game" contradicted the screen's own message
// (SEEN in runs/p17-f5-empty/bottom_00040.png). It still needs the BLANKING half, because the
// blanked band is where the settings chip lives.
static void pick_footer_band(const char* plateId, int withHint) {
	C2D_Image img = assets_plate(plateId);
	if (!img.tex || !img.subtex) return;
	C2D_Image sub; Tex3DS_SubTexture st;
	const float SH = 8.0f;                               // rows 232..239 are pure background
	if (assets_img_cell(img, 0.0f, 232.0f, 320.0f, SH, &sub, &st))
		C2D_DrawImageAt(sub, 0.0f, PICK_FOOT_Y, 0.0f, NULL, 1.0f, (240.0f - PICK_FOOT_Y) / SH);
	if (withHint && assets_img_cell(img, 0.0f, PICK_HINT_SRC_Y, 320.0f, PICK_HINT_H, &sub, &st))
		C2D_DrawImageAt(sub, 0.0f, PICK_HINT_SRC_Y - PICK_HINT_LIFT, 0.0f, NULL, 1.0f, 1.0f);
}

// FIX PASS (review finding 3): blank the plate's baked A/B SLOT CARDS. The empty state reuses the
// picker's plate (L7.2.1 says reuse the geometry rather than invent a layout), and that plate bakes
// two card bodies with their "A · TOP SCREEN" / "B · BOTTOM SCREEN" headers and colour swatches —
// so the screen that exists to say "there are no games" showed two labelled, permanently empty
// game slots. Measured per-pixel on select-dual-bot: rows 0..50, 103..110 and 163..173 are pure
// background and rows 51..102 / 111..162 are the two cards, so stretching one verified-empty row
// band over 44..173 removes both and touches nothing else. Same idiom as the footer band above and
// F3's menu_plate_bg — NOT a g_ui.bg fill, because only the indigo pack is baked and a token fill
// would be a visibly different colour from the plate on any other theme.
#define PICK_CARDS_Y  44.0f
#define PICK_CARDS_H  130.0f       // 44..173, i.e. up to the START row at y=174
static void pick_blank_cards(const char* plateId) {
	C2D_Image img = assets_plate(plateId);
	if (!img.tex || !img.subtex) return;
	C2D_Image sub; Tex3DS_SubTexture st;
	const float SH = 6.0f;                               // rows 168..173: verified pure background
	if (assets_img_cell(img, 0.0f, 168.0f, 320.0f, SH, &sub, &st))
		C2D_DrawImageAt(sub, 0.0f, PICK_CARDS_Y, 0.0f, NULL, 1.0f, PICK_CARDS_H / SH);
}
// Step 3, drawn last so the chip is never under a widget.
static void pick_footer_chip(C2D_TextBuf buf, UiRect rSet) {
	ui_fill((float)rSet.x, (float)rSet.y, (float)rSet.w, (float)rSet.h, g_ui.panel2, 5.0f);
	assets_text(buf, FNT_JBM_MED, "settings · ZR", (float)rSet.x + 6.0f,
	            // W4.1: the chip's fill is PROCEDURAL (g_ui.panel2), so its ink is the ACTIVE
	            // theme's — unlike everything drawn on the baked plate around it, which is g_art.
	            (float)rSet.y + ((float)rSet.h - 8.0f) / 2.0f, 8.0f, g_ui.dim);
}

// ---- PHASE 17 / SPEC-layout L7.2 (sweep D13): the ROM-less empty state -------------------------
// With no .gba on the card, `scan_roms` returned 0, `rompicker_run` returned false and main() fell
// through to hard-coded sdmc:/3DGBA/gameA.gba + gameB.gba. Those do not exist either, so both cores
// failed to load and the app dropped — with no screen in between — into a live SESSION over pure
// black: "+ gameB", "59fps", "tap screen · pause menu", and nothing anywhere saying why the screens
// were empty. The user's first-ever boot is exactly this path.
//
// It now stops on a screen that names the problem AND the directory. The "Start without a game"
// button deliberately keeps the old behaviour reachable and LABELLED — the emutest harness's
// zero-permission Tier A is built on booting ROM-less into that dead-core session and driving the
// pause menu (.claude/skills/emutest/SKILL.md §2), so removing the path outright would break the
// gate. One extra button press in the Tier-A movie, and an accident becomes a choice.
enum { ES_RESCAN = 0, ES_NOGAME = 1, ES_SETTINGS = 2 };
static int empty_state_run(C3D_RenderTarget* top, C3D_RenderTarget* bot, C2D_TextBuf txtBuf) {
	UiRect rRe  = uihit_pick_rect(false, PICK_START);     // reuse the picker's own button geometry
	UiRect rGo  = uihit_pick_rect(false, PICK_LINKED);    // (L7.2.1: do not invent a second layout)
	UiRect rSet = uihit_pick_rect(false, PICK_SETTINGS);
	UiGesture gest; memset(&gest, 0, sizeof gest);
	int sel = 0;                                          // 0 = Rescan, 1 = Start without a game
	g_pickDiag.magic = 0x50494B31; g_pickDiag.nRoms = 0;
	g_pickDiag.idxA = g_pickDiag.idxB = -1; g_pickDiag.lastHit = PICK_NONE;

	while (aptMainLoop()) {
		hidScanInput();
		u32 k = hidKeysDown(), kHeld = hidKeysHeld(), kUp = hidKeysUp();
		touchPosition raw; hidTouchRead(&raw);
		UiGestEv ev = uihit_gesture_step(&gest, (k & KEY_TOUCH) != 0, (kHeld & KEY_TOUCH) != 0,
		                                 (kUp & KEY_TOUCH) != 0, raw.px, raw.py);
		if (ev == GEST_TAP) {                             // the D2 lesson: act on the LATCHED point
			g_pickDiag.tapN++;
			g_pickDiag.lastTap = (gest.x << 16) | (gest.y & 0xFFFF);
			if (uihit_in(rRe,  gest.x, gest.y)) { g_pickDiag.lastHit = PICK_START;    return ES_RESCAN; }
			if (uihit_in(rGo,  gest.x, gest.y)) { g_pickDiag.lastHit = PICK_LINKED;   return ES_NOGAME; }
			if (uihit_in(rSet, gest.x, gest.y)) { g_pickDiag.lastHit = PICK_SETTINGS; return ES_SETTINGS; }
			g_pickDiag.lastHit = PICK_NONE;
		}
		if (k & (KEY_DLEFT | KEY_CPAD_LEFT))   sel = 0;
		if (k & (KEY_DRIGHT | KEY_CPAD_RIGHT)) sel = 1;
		if (k & (KEY_DUP | KEY_CPAD_UP | KEY_DDOWN | KEY_CPAD_DOWN)) sel ^= 1;
		if (k & KEY_ZR) return ES_SETTINGS;
		if (k & KEY_A)     return sel ? ES_NOGAME : ES_RESCAN;
		if (k & KEY_START) return ES_RESCAN;              // the fastest "I just copied a ROM over"
		if (k & KEY_X)     return ES_NOGAME;

		g_pickDiag.frame++;
		C2D_TextBufClear(txtBuf);
		g_renderSeq++;   // app liveness for the emutest harness (ui.h)
		C3D_FrameBegin(C3D_FRAME_SYNCDRAW);

		C2D_TargetClear(top, g_ui.bg); C2D_SceneBegin(top);
		assets_draw_plate("select-dual-top");
		// The card sits in the list's own band (manifest x16 y37 w369 h197) — there are no rows to
		// draw, so nothing is covered.
		assets_text_c(txtBuf, FNT_SG_BOLD, "No games found", 200.0f, 96.0f, 16.0f, g_art.text);
		assets_text_c(txtBuf, FNT_JBM_MED, "put .gba files in " ROM_DIR "/", 200.0f, 122.0f, 9.0f, g_art.dim);

		C2D_TargetClear(bot, g_ui.bg); C2D_SceneBegin(bot);
		assets_draw_plate("select-dual-bot");
		pick_blank_cards("select-dual-bot");     // review finding 3: no empty labelled slot cards
		assets_text_c(txtBuf, FNT_JBM_MED, "put .gba files in " ROM_DIR "/, then Rescan",
		              160.0f, 104.0f, 9.0f, g_art.dim);
		assets_button(txtBuf, "btn-primary", (float)rRe.x, (float)rRe.y, (float)rRe.w, (float)rRe.h,
		              "Rescan", FNT_SG_BOLD, 13.0f, g_art.ink, sel == 0);
		assets_button(txtBuf, "btn-secondary", (float)rGo.x, (float)rGo.y, (float)rGo.w, (float)rGo.h,
		              "Start without a game", FNT_SG_MED, 11.0f, g_art.text, sel == 1);
		pick_footer_band("select-dual-bot", 0);  // review finding 7: no "tap a game above" here
		pick_footer_chip(txtBuf, rSet);
		C3D_FrameEnd(0);
	}
	return ES_NOGAME;   // aptMainLoop ended (HOME-exit): behave as before rather than spin
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
	// L7.2.1: an empty library is a STATE, not a failure to report by silently starting a session.
	while (n == 0) {
		int e = empty_state_run(top, bot, txtBuf);
		if (e == ES_SETTINGS) { snprintf(pathA, cap, "%s", "__SETTINGS__"); return true; }
		if (e == ES_NOGAME)   return false;              // the labelled dead-core fallback (Tier A)
		n = scan_roms(names, disp, codes);               // Rescan: the user just copied a file over
	}

	// manifest select-*: TOP list x16 y37 w369 h197 (8 rows); BOTTOM seg + slot names + 2 buttons.
	const int LIST_ROWS = 8;
	const float LIST_Y = 41.0f, ROWH = 23.5f;
	int idxA = -1, idxB = -1, sel = 0, topRow = 0;
	UiGesture gest; memset(&gest, 0, sizeof gest);       // touch drag-to-scroll + tap (§I2.1)
	int navRow = NAV_LIST;                                // d-pad focus zone (§I3.2)
	int actSel = 0;                                       // 0 = START, 1 = START—LINKED
	int hintT  = 0;                                       // frames left of the "pick a game first" hint
	memset(&g_pickDiag, 0, sizeof g_pickDiag);
	g_pickDiag.magic = 0x50494B31;                        // 'PIK1'
	g_pickDiag.lastHit = PICK_NONE;
	g_pickDiag.idxA = g_pickDiag.idxB = -1;
	g_pickDiag.nRoms = n;

	while (aptMainLoop()) {
		hidScanInput();
		u32 k = hidKeysDown(), kHeld = hidKeysHeld(), kUp = hidKeysUp();
		bool single = (g_prefs.gameMode == 1);
		if (single && navRow == NAV_SLOTB) navRow = NAV_ACT;
		if (hintT > 0) hintT--;

		// ---- the ONE place the raw touch point is consumed (§I1.2.2) --------------------------
		// Reading it on the release frame is REPORT D2: HID reports (0,0) there, which used to
		// resolve as "tap the left half of the mode pill" = silently force 1 Game.
		touchPosition raw; hidTouchRead(&raw);
		int wasDragging = gest.dragged;
		UiGestEv ev = uihit_gesture_step(&gest, (k & KEY_TOUCH) != 0, (kHeld & KEY_TOUCH) != 0,
		                                 (kUp & KEY_TOUCH) != 0, raw.px, raw.py);
		if (ev == GEST_DOWN) gest.base = topRow;
		if (!wasDragging && gest.dragged) g_pickDiag.dragN++;
		if (ev == GEST_DRAG) {                            // drag anywhere on the bottom screen
			topRow = uihit_scroll_clamp(gest.base, gest.y0 - gest.y, UIHIT_PICK_ROW_PX, n, LIST_ROWS);
			g_pickDiag.dragRows = topRow - gest.base;
			sel = uihit_clamp_sel(sel, topRow, LIST_ROWS, n);   // §I2.2.6 the highlight follows
			navRow = NAV_LIST;
		}

		// ---- resolve one activation per frame, from a TAP or from a button --------------------
		int  fire       = PICK_NONE;   // a picker target activated this frame
		int  modeSeg    = -1;          // set = a tap chose a specific mode segment
		bool modeToggle = false;       // Y / A-on-the-pill flip the mode
		bool confirmRow = false;       // A on the list assigns the highlighted row to a slot

		if (ev == GEST_TAP) {
			g_pickDiag.tapN++;
			g_pickDiag.lastTap = (gest.x << 16) | (gest.y & 0xFFFF);
			fire = uihit_pick(single, gest.x, gest.y);
			g_pickDiag.lastHit = fire;
			if (fire == PICK_MODE) modeSeg = uihit_seg(uihit_pick_rect(single, PICK_MODE), 2, gest.x);
			switch (fire) {            // touch moves the d-pad focus with it, so the two agree
				case PICK_MODE:     navRow = NAV_MODE;  break;
				case PICK_SLOT_A:   navRow = NAV_SLOTA; break;
				case PICK_SLOT_B:   navRow = NAV_SLOTB; break;
				case PICK_START:    navRow = NAV_ACT; actSel = 0; break;
				case PICK_LINKED:   navRow = NAV_ACT; actSel = 1; break;
				case PICK_SETTINGS: navRow = NAV_SET;   break;
				default: break;
			}
		}

		// d-pad: vertical walks LIST -> MODE -> A -> B -> actions -> settings -> LIST (skipping B
		// in single mode); horizontal adjusts the focused control.
		if (k & (KEY_DDOWN | KEY_CPAD_DOWN)) {
			if (navRow == NAV_LIST && sel < n - 1) sel++;
			else navRow = nav_step(navRow, +1, single);
		}
		if (k & (KEY_DUP | KEY_CPAD_UP)) {
			if (navRow == NAV_LIST && sel > 0) sel--;
			else navRow = nav_step(navRow, -1, single);
		}
		int adj = (k & (KEY_DRIGHT | KEY_CPAD_RIGHT)) ? 1 : ((k & (KEY_DLEFT | KEY_CPAD_LEFT)) ? -1 : 0);
		if (adj && navRow == NAV_MODE) { modeSeg = (adj < 0) ? 0 : 1; fire = PICK_MODE; }
		if (adj && navRow == NAV_ACT)  { actSel += adj; if (actSel < 0) actSel = 0; if (actSel > 1) actSel = 1; }

		if (k & KEY_A) switch (navRow) {
			case NAV_LIST:  confirmRow = true;      break;
			case NAV_MODE:  modeToggle = true;      break;
			case NAV_SLOTA: fire = PICK_SLOT_A;     break;
			case NAV_SLOTB: fire = PICK_SLOT_B;     break;
			case NAV_ACT:   fire = actSel ? PICK_LINKED : PICK_START; break;
			case NAV_SET:   fire = PICK_SETTINGS;   break;
			default: break;
		}
		// The shortcuts stay (they are what the on-screen hints name), and START now activates the
		// button labelled START instead of launching the hard-coded defaults (§I1.7.1).
		if (k & KEY_ZR)     fire = PICK_SETTINGS;
		if (k & KEY_Y)      modeToggle = true;
		if (k & KEY_X)      fire = PICK_START;
		if (k & KEY_SELECT) fire = PICK_LINKED;
		if (k & KEY_START)  fire = PICK_START;
		// B unassigns B, then A, then does nothing — the picker is the app's root screen, there is
		// nowhere to go back to (§I1.7.2; it used to fall through to a defaults session).
		if (k & KEY_B) { if (idxB >= 0) idxB = -1; else if (idxA >= 0) idxA = -1; }

		// ---- dispatch -------------------------------------------------------------------------
		if (modeToggle) { modeSeg = single ? 1 : 0; fire = PICK_MODE; }
		if (fire == PICK_MODE && modeSeg >= 0) {
			g_prefs.gameMode = (modeSeg == 0) ? 1 : 0;    // seg 0 = "1 Game", seg 1 = "2 Games"
			if (g_prefs.gameMode == 1) idxB = -1;
			single = (g_prefs.gameMode == 1);
		}
		if (fire == PICK_SLOT_A) idxA = (idxA == sel) ? -1 : sel;   // a card is its own undo
		if (fire == PICK_SLOT_B && !single) idxB = (idxB == sel) ? -1 : sel;
		if (fire == PICK_SETTINGS) { snprintf(pathA, cap, "%s", "__SETTINGS__"); return true; }
		if (confirmRow) { if (idxA < 0 || single) idxA = sel; else idxB = sel; }

		bool ready = single ? (idxA >= 0) : (idxA >= 0 && idxB >= 0);
		if (fire == PICK_START || fire == PICK_LINKED) {
			if (ready) {
				g_pickDiag.startN++;
				snprintf(pathA, cap, "%s/%s", ROM_DIR, names[idxA]);
				if (single) snprintf(pathB, cap, "%s", ""); else snprintf(pathB, cap, "%s/%s", ROM_DIR, names[idxB]);
				if (startLinked) *startLinked = (fire == PICK_LINKED);
				return true;
			}
			hintT = 120;                                  // ~2 s of "pick a game first"
		}

		if (!gest.active) topRow = uihit_follow_sel(topRow, sel, LIST_ROWS, n);  // §I2.2.6, both ways
		single = (g_prefs.gameMode == 1);
		ready  = single ? (idxA >= 0) : (idxA >= 0 && idxB >= 0);

		g_pickDiag.frame++;
		g_pickDiag.sel = sel; g_pickDiag.topRow = topRow; g_pickDiag.nRoms = n;
		g_pickDiag.idxA = idxA; g_pickDiag.idxB = idxB; g_pickDiag.mode = g_prefs.gameMode;

		C2D_TextBufClear(txtBuf);
		g_renderSeq++;   // app liveness for the emutest harness (ui.h)
		C3D_FrameBegin(C3D_FRAME_SYNCDRAW);

		// ===== TOP: plate + dynamic "NOW PICKING" + the ROM list rows =====
		C2D_TargetClear(top, g_ui.bg); C2D_SceneBegin(top);
		assets_draw_plate(single ? "select-single-top" : "select-dual-top");
		{
			// hintT: START pressed with no game picked yet — say so instead of doing nothing.
			const char* pk = hintT > 0 ? "PICK A GAME FIRST"
			                           : (ready ? "READY" : (single ? "1 GAME" : (idxA < 0 ? "A · TOP" : "B · BOTTOM")));
			assets_text_r(txtBuf, FNT_JBM_MED, pk, 384.0f, 15.0f, 9.0f, hintT > 0 ? THEME_QUIT_TEXT : g_ui.acc);
		}
		for (int i = 0; i < LIST_ROWS && topRow + i < n; i++) {
			int gi = topRow + i;
			float y = LIST_Y + i * ROWH;
			if (gi == sel) {
				assets_fill9("fill-card-r8", 12.0f, y - 1.0f, 377.0f, ROWH - 2.0f, 6.0f);
				// the focus ring says WHICH zone A will act on — list, or a bottom control
				if (navRow == NAV_LIST) ui_border_round(12.0f, y - 1.0f, 377.0f, ROWH - 2.0f, FOCUS_COL, 1.5f, 6.0f);
			}
			// SPEC-layout L8.1 (sweep D20): every row used to blit `cart-tinted`, whose cap band is
			// baked GREEN, so eight different titles carried eight identical carts — and the design
			// (`03-game-select.png`) colours the cap PER TITLE, which is what makes a long list
			// scannable. cart_tint() existed and was called from nowhere. `cart-blank` is the same
			// 15x20 body WITHOUT a cap, so the tint needs no tinting API: the cap band measured on
			// `cart-tinted` is columns 3..12, rows 3..5 — 10x3 at (+3,+3) from the sprite origin.
			assets_draw_wgt("cart-blank", 18.0f, y + 1.0f);
			ui_fill(21.0f, y + 4.0f, 10.0f, 3.0f, cart_tint(codes[gi]), 1.0f);
			assets_text(txtBuf, FNT_SG_MED, disp[gi], 40.0f, y + 3.0f, 12.0f, g_art.text);
			if (gi == idxA)      assets_draw_wgt("badge-a", 360.0f, y + 2.0f);
			else if (gi == idxB) assets_draw_wgt("badge-b", 360.0f, y + 2.0f);
			else                 assets_text_r(txtBuf, FNT_JBM_MED, codes[gi], 384.0f, y + 4.0f, 8.5f, g_art.dim);
		}
		// An honest scrollbar for the list (§I2.4.6's rule applied to the row scroll). Until now a
		// 13-ROM library gave the player NO indication that eight rows were not all of them, and no
		// feedback at all that a drag had moved anything. The plate's right margin (x386..399 for
		// all 198 list rows) is pure background in every theme — verified per-pixel — so this track
		// overpaints no baked art. Drawn only when the list actually overflows.
		if (n > LIST_ROWS) {
			const int TX = 390, TW = 5, TY = (int)LIST_Y, TH = (int)(LIST_ROWS * ROWH) - 4;
			int maxTop = uihit_scroll_max(n, LIST_ROWS);
			int th = uihit_thumb_h(TH, LIST_ROWS, n);
			int ty = uihit_thumb_y(TY, TH, th, topRow, maxTop);
			ui_fill((float)TX, (float)TY, (float)TW, (float)TH, g_ui.line, 2.5f);
			ui_fill((float)TX, (float)ty, (float)TW, (float)th, g_ui.dim,  2.5f);
		}

		// ===== BOTTOM: plate + mode segmented + slot name(s) + START / LINKED =====
		C2D_TargetClear(bot, g_ui.bg); C2D_SceneBegin(bot);
		assets_draw_plate(single ? "select-single-bot" : "select-dual-bot");
		// Every rect below comes from uihit — the SAME array the tap was hit-tested against, so a
		// coordinate can no longer drift between draw and hit (§0.1.3).
		UiRect rMode = uihit_pick_rect(single, PICK_MODE);
		UiRect rA    = uihit_pick_rect(single, PICK_SLOT_A);
		UiRect rB    = uihit_pick_rect(single, PICK_SLOT_B);
		UiRect rSt   = uihit_pick_rect(single, PICK_START);
		UiRect rLk   = uihit_pick_rect(single, PICK_LINKED);
		UiRect rSet  = uihit_pick_rect(single, PICK_SETTINGS);
		{
			static const char* const MODES[2] = { "1 Game", "2 Games" };
			assets_seg(txtBuf, rMode.x, rMode.y, rMode.w, rMode.h, MODES, 2, single ? 0 : 1, g_ui.ink, g_art.dim);
			if (navRow == NAV_MODE) focus_ring(rMode, 15.0f);
		}
		// FIX PASS (review finding 5). START's body is the BAKED gold `fill-primary-r8`, so its
		// not-ready label needs an ink mixed toward THAT surface: `g_art.dim` (#B0A8C8) on #F5D042 is
		// 1.51:1 — the word was a pale ghost, and because g_art is pinned to the art's theme it was
		// 1.51:1 in all six (Daylight had been 3.60:1 with the old g_ui.dim, OLED 2.13:1, so four
		// themes had got WORSE). theme_ink_disabled keeps the "not ready" signal at ~4.2:1. LINKED is
		// left on g_art.dim: its body is `fill-card-r8` (#2A2042), where dim measures ~6.5:1.
		if (single) {
			assets_text(txtBuf, FNT_SG_MED, idxA >= 0 ? disp[idxA] : "pick a game (d-pad + A)", 49.0f, 81.0f, 13.0f, idxA >= 0 ? g_art.text : g_art.dim);
			assets_button(txtBuf, "btn-primary",        rSt.x, rSt.y, rSt.w, rSt.h, "START", FNT_SG_BOLD, 13.0f, ready ? g_art.ink : theme_ink_disabled(g_art.ink, g_art.acc), navRow == NAV_ACT && actSel == 0);
			assets_button(txtBuf, "btn-accent-outline", rLk.x, rLk.y, rLk.w, rLk.h, "LINK A FRIEND", FNT_SG_BOLD, 12.0f, ready ? g_ui.acc : g_art.dim, navRow == NAV_ACT && actSel == 1);
		} else {
			assets_text(txtBuf, FNT_SG_MED, idxA >= 0 ? disp[idxA] : "pick game A", 47.0f, 78.0f, 12.0f, idxA >= 0 ? g_art.text : g_art.dim);
			assets_text(txtBuf, FNT_SG_MED, idxB >= 0 ? disp[idxB] : "pick game B", 47.0f, 139.0f, 12.0f, idxB >= 0 ? g_art.text : g_art.dim);
			assets_button(txtBuf, "btn-primary",        rSt.x, rSt.y, rSt.w, rSt.h, "START", FNT_SG_BOLD, 13.0f, ready ? g_art.ink : theme_ink_disabled(g_art.ink, g_art.acc), navRow == NAV_ACT && actSel == 0);
			assets_button(txtBuf, "btn-accent-outline", rLk.x, rLk.y, rLk.w, rLk.h, "START — LINKED", FNT_SG_BOLD, 12.0f, ready ? g_ui.acc : g_art.dim, navRow == NAV_ACT && actSel == 1);
		}
		if (navRow == NAV_SLOTA) focus_ring(rA, 6.0f);
		if (navRow == NAV_SLOTB) focus_ring(rB, 6.0f);
		// The footer: hint band lifted clear + the settings affordance drawn at its HIT rect
		// (§I1.6.3 + SPEC-layout L2 / REPORT D7 — see pick_footer).
		pick_footer_band(single ? "select-single-bot" : "select-dual-bot", 1);   // L2 steps 1+2
		pick_footer_chip(txtBuf, rSet);
		if (navRow == NAV_SET) focus_ring(rSet, 5.0f);
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

		g_renderSeq++;   // app liveness for the emutest harness (ui.h)
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
