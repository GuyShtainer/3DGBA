// panelui.c — phase 32 track T, the device half of the touch PANEL. See panelui.h.
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include "panelui.h"
#include "gbatext.h"
#include "theme.h"
#include "ui.h"
#include "assets.h"

// ---- binding ------------------------------------------------------------------------------------
// Addresses: SPEC-touch-panel T2/T3 (re-read from the pret sym files for every revision; the user's
// FireRed is rev1). The ROM tables move between revisions, so the header revision byte 0xBC picks
// the row; RAM (the START list, gBattleMons) is revision-identical.
typedef struct { uint32_t moveNames, battleMoves, typeNames; } RomTables;
static const RomTables ROM_EM       = { 0x0831977Cu, 0x0831C898u, 0x0831AE38u };
static const RomTables ROM_FR[2]    = { { 0x08247094u, 0x08250C04u, 0x0824F1A0u },
                                        { 0x08247104u, 0x08250C74u, 0x0824F210u } };
static const RomTables ROM_LG[2]    = { { 0x08247070u, 0x08250BE0u, 0x0824F17Cu },
                                        { 0x082470E0u, 0x08250C50u, 0x0824F1ECu } };

bool panelui_bind(PanelUi* u, GbaCore* core) {
	assert(u);
	if (u->core == core && core) return u->ok;
	memset(u, 0, sizeof *u);
	u->core = core; u->item = -1; u->fbItem = -1; u->replayFrames = 0;
	if (!core) return false;
	char code[5] = { 0 };
	gbacore_game_code(core, code);
	PanelAddrs* a = &u->addrs;
	uint8_t rev = gbacore_read8(core, 0x080000BCu);
	if (!memcmp(code, "BPEE", 4)) {
		a->fam = PFAM_EM; a->startCount = 0x0203760Fu; a->startActions = 0x02037610u;
		a->battleMons = 0x02024084u;
		a->moveNames = ROM_EM.moveNames; a->battleMoves = ROM_EM.battleMoves; a->typeNames = ROM_EM.typeNames;
	} else if (!memcmp(code, "BPRE", 4) || !memcmp(code, "BPGE", 4)) {
		a->fam = PFAM_FRLG; a->startCount = 0x020370F5u; a->startActions = 0x020370F6u;
		a->battleMons = 0x02023BE4u;
		if (rev <= 1) {   // an unknown revision keeps names off ("MOVE n"), never a wrong name
			const RomTables* t = (code[2] == 'R') ? &ROM_FR[rev] : &ROM_LG[rev];
			a->moveNames = t->moveNames; a->battleMoves = t->battleMoves; a->typeNames = t->typeNames;
		}
	} else {
		return false;
	}
	u->ok = true;
	return true;
}

// ---- bus ----------------------------------------------------------------------------------------
static uint8_t  rd8 (void* c, uint32_t a) { return gbacore_read8 ((GbaCore*)c, a); }
static uint16_t rd16(void* c, uint32_t a) { return gbacore_read16((GbaCore*)c, a); }
static uint32_t rd32(void* c, uint32_t a) { return gbacore_read32((GbaCore*)c, a); }

// ---- context ------------------------------------------------------------------------------------
static bool task_live(const GameState* gs, uint32_t fn, uint32_t alt) {
	for (int i = 0; i < gs->nTask && i < 8; i++) {
		uint32_t t = gs->taskFp[i] & ~1u;
		if ((fn && t == (fn & ~1u)) || (alt && t == (alt & ~1u))) return true;
	}
	return false;
}

static PanelCtx map_ctx(const PanelUi* u, const GameProfile* gp, const GameState* gs) {
	switch (gs->ctx) {
	case GCTX_BATTLE_ACTION: case GCTX_BATTLE_MOVE: case GCTX_BATTLE_TARGET: case GCTX_BATTLE_OTHER:
		return PCTX_BATTLE;
	case GCTX_OVERWORLD:
		return PCTX_FIELD;
	case GCTX_FIELDMENU:   // START, or a script yes/no / multichoice (never backed out of with B)
		return task_live(gs, gp->startMenuTask, gp->startMenuTaskAlt) ? PCTX_START : PCTX_OTHER;
	case GCTX_PARTY: case GCTX_BAG: case GCTX_FULLUI: case GCTX_MAP:
		return u->inBattle ? PCTX_BATTLE : PCTX_MENU;
	case GCTX_POKENAV:
		return (gs->pnMenuIdx == 0 && gs->pnCursor >= 0) ? PCTX_POKENAV_MAIN : PCTX_MENU;
	default:   // title, naming, lists, storage, inert: B there is not a safe "back out"
		return PCTX_OTHER;
	}
}

// ---- touch routing ------------------------------------------------------------------------------
static int region_at(const PanelUi* u, int x, int y) {
	if (u->battlePanel) {
		if (u->gctx == GCTX_BATTLE_ACTION) return panel_battle_action_hit(x, y) >= 0 ? PREG_BATTLE : PREG_NONE;
		return panel_battle_move_hit(x, y) >= 0 ? PREG_BATTLE : PREG_NONE;
	}
	if (x >= PANEL_COL_HIT_X) return PREG_COL;
	if (x >= PANELUI_CHIP_X && x < PANELUI_CHIP_X + PANELUI_CHIP_W &&
	    y >= PANELUI_CHIP_Y && y < PANELUI_CHIP_Y + PANELUI_CHIP_H) return PREG_CHIP;
	return PREG_VIEW;
}

static int item_at(const PanelUi* u, int region, int x, int y) {
	switch (region) {
	case PREG_COL:    return panel_column_hit(u->addrs.fam, x, y);
	case PREG_CHIP:   return (region_at(u, x, y) == PREG_CHIP) ? 0 : -1;
	case PREG_BATTLE: return (u->gctx == GCTX_BATTLE_ACTION) ? panel_battle_action_hit(x, y)
	                                                         : panel_battle_move_hit(x, y);
	default:          return -1;
	}
}

// A released, undragged button tap. Returns true when the MENU chip fired.
static bool accept_tap(PanelUi* u) {
	u->fbRegion = u->region; u->fbItem = u->item; u->fbTimer = PANELUI_FB;
	if (u->region == PREG_CHIP) return true;
	if (u->region == PREG_COL) {
		PanelButton b = panel_row_button(u->addrs.fam, u->item);
		if (b < PB_COUNT && panel_button_enabled(u->addrs.fam, b, &u->list))
			panel_seq_start(&u->seq, u->addrs.fam, b);
		return false;
	}
	if (u->region == PREG_BATTLE) {
		if (u->gctx == GCTX_BATTLE_MOVE && u->item == 4) { u->bPulse = 2; return false; }
		u->replayKind = (u->gctx == GCTX_BATTLE_ACTION) ? 0 : 1;
		u->replaySlot = u->item;
		u->replayFrames = PANELUI_SETTLE + 3;   // SPEC T3: touching for TOUCH_SETTLE + 3 frames
	}
	return false;
}

// Tracks the contact; fills the contact SMART should see this frame. Returns the MENU request.
static bool track_contact(PanelUi* u, bool touching, int px, int py,
                          bool* sTouch, int* sx, int* sy, int* gx, int* gy, bool* gvalid) {
	bool menu = false;
	*sTouch = false; *gvalid = false;
	if (touching) {
		if (!u->wasTouching) {
			u->age = 0; u->drag = false; u->item = -1;
			u->region = region_at(u, px, py);
			if (u->region == PREG_VIEW && panel_seq_busy(&u->seq)) panel_seq_cancel(&u->seq);
		}
		u->age++;
		u->lx = px; u->ly = py;
		if (u->age == PANELUI_SETTLE) { u->x0 = px; u->y0 = py; u->item = item_at(u, u->region, px, py); }
		if (u->age > PANELUI_SETTLE && (abs(px - u->x0) > PANELUI_SLOP || abs(py - u->y0) > PANELUI_SLOP))
			u->drag = true;
		if (u->region == PREG_VIEW) {
			*sTouch = true; *sx = px; *sy = py;
			*gvalid = panel_view_to_gba(px, py, gx, gy);
		}
	} else if (u->wasTouching) {
		if (u->region != PREG_VIEW && u->region != PREG_NONE && !u->drag && u->item >= 0 &&
		    item_at(u, u->region, u->lx, u->ly) == u->item)
			menu = accept_tap(u);
		u->region = PREG_NONE; u->item = -1;
	}
	u->wasTouching = touching;
	return menu;
}

// ---- the frame ----------------------------------------------------------------------------------
u16 panelui_update(PanelUi* u, const GameProfile* gp, const GameState* gs, const TouchSmart* sm,
                   bool touching, int px, int py, u16 padKeys, bool link, bool* menuReq) {
	assert(u && menuReq);
	*menuReq = false;
	if (!u->ok || !gp || !gs || !gs->valid) {
		u->ctx = PCTX_OTHER; u->battlePanel = false;
		if (panel_seq_busy(&u->seq)) panel_seq_cancel(&u->seq);
		return 0;
	}
	PanelBus bus = { rd8, rd16, rd32, u->core };
	u->gctx = gs->ctx;
	// "In battle" is a LATCH, not gBattleTypeFlags: the flags are zeroed at battle setup, not at the
	// end, so they linger into the field (HANDOFF key decisions). A battle context sets it and only
	// the free field clears it, so the in-battle bag/party (own cb2s) still count as battle.
	if (gs->ctx == GCTX_BATTLE_ACTION || gs->ctx == GCTX_BATTLE_MOVE ||
	    gs->ctx == GCTX_BATTLE_TARGET || gs->ctx == GCTX_BATTLE_OTHER) u->inBattle = true;
	else if (gs->ctx == GCTX_OVERWORLD || gs->ctx == GCTX_TITLE)    u->inBattle = false;
	u->ctx = map_ctx(u, gp, gs);
	u->fieldRun = (u->ctx == PCTX_FIELD) ? (u->fieldRun < 1000 ? u->fieldRun + 1 : 1000) : 0;
	if (u->ctx == PCTX_START) panel_read_start(&bus, &u->addrs, &u->list);

	// The battle panel: singles only. In a double battle the second mon's turn would show the
	// first mon's moves (no active-battler read yet), so doubles keep the game view.
	bool cmd = (gs->ctx == GCTX_BATTLE_ACTION || gs->ctx == GCTX_BATTLE_MOVE) && gs->battlersCount != 4;
	if (cmd) panel_read_battle(&bus, &u->addrs, gbatext_decode, 0, 1, &u->battle);
	bool wasPanel = u->battlePanel;
	u->battlePanel = cmd && u->battle.self.valid;
	if (wasPanel != u->battlePanel && u->wasTouching) u->region = PREG_NONE;   // layout swapped mid-contact

	bool sTouch; int sx = 0, sy = 0, gx = -1, gy = -1; bool gvalid;
	*menuReq = track_contact(u, touching, px, py, &sTouch, &sx, &sy, &gx, &gy, &gvalid);

	if (u->replayFrames > 0) {   // synthetic contact at the cell centre, then released
		if (panel_battle_cell(u->replayKind, u->replaySlot, &gx, &gy)) {
			sTouch = true; gvalid = true; sx = gx; sy = gy + PANEL_FRAME_Y;
		}
		u->replayFrames--;
	}
	u16 keys = touch_update(TOUCH_SMART, sTouch, sx, sy, gx, gy, gvalid, sm);
	if (u->bPulse > 0) { if (u->bPulse == 2) keys |= 1u << GBAKEY_B; u->bPulse--; }

	if (panel_seq_busy(&u->seq)) {
		PanelSeqIn in;
		memset(&in, 0, sizeof in);
		in.ctx = u->ctx; in.fieldLock = gs->fieldLock; in.padKeys = padKeys; in.link = link;
		in.startCursor = (u->ctx == PCTX_START && gp->startCursor) ? gbacore_read8(u->core, gp->startCursor) : -1;
		in.pnCursor = (u->ctx == PCTX_POKENAV_MAIN) ? gs->pnCursor : -1;
		in.list = u->list;
		PanelSeqOut o = panel_seq_step(&u->seq, &in);
		if (o.startCursor >= 0 && gp->sMenuBase) {   // the shipped fmenu_select writes (touch.c)
			gbacore_write8(u->core, gp->sMenuBase + 2, (uint8_t)o.startCursor);
			uint32_t cb = gbacore_read32(u->core, gp->startCb) & ~1u;
			if (gp->startCursor && (cb == gp->startCbInput || (gp->startCbInputAlt && cb == gp->startCbInputAlt)))
				gbacore_write8(u->core, gp->startCursor, (uint8_t)o.startCursor);
		}
		keys |= o.keys;
		if (!panel_seq_busy(&u->seq)) u->lastAbort = u->seq.abortWhy;
	}
	if (u->fbTimer > 0) u->fbTimer--;
	return keys;
}

bool panelui_hold_top(const PanelUi* u) {
	return u && u->ok && !u->inBattle && (u->ctx == PCTX_MENU || u->ctx == PCTX_POKENAV_MAIN);
}
bool panelui_is_field(const PanelUi* u) {
	return u && u->ok && u->ctx == PCTX_FIELD && u->fieldRun >= PANELUI_HOLD_SETTLE;
}

// ---- drawing ------------------------------------------------------------------------------------
static u32 rgba_to_c2d(uint32_t c) {
	return C2D_Color32((u8)(c >> 24), (u8)(c >> 16), (u8)(c >> 8), (u8)c);
}
static float text_y(TxtRole r, float top, float h) { return top + (h - (float)g_txtLineFeed[r]) * 0.5f; }

static bool fb_on(const PanelUi* u, int region, int item) {
	return u->fbTimer > 0 && u->fbRegion == region && u->fbItem == item;
}
static bool held_on(const PanelUi* u, int region, int item) {
	return u->wasTouching && !u->drag && u->region == region && u->item == item;
}

static void draw_button(C2D_TextBuf buf, const PanelRect* r, const char* label, bool lit, bool dim, TxtRole role) {
	u32 fill = lit ? THEME_GOLD : THEME_PANEL;
	u32 ink  = lit ? THEME_SELTXT : (dim ? THEME_DIM : THEME_TEXT);
	ui_panel((float)r->x, (float)r->y, (float)r->w, (float)r->h, fill, THEME_LINE, 4.0f);
	assets_text_c(buf, role, label, (float)r->x + r->w * 0.5f, text_y(role, (float)r->y, (float)r->h), ink);
}

static void draw_column(const PanelUi* u, C2D_TextBuf buf) {
	C2D_DrawRectSolid((float)PANEL_COL_HIT_X, 0.0f, 0.0f, 320.0f - PANEL_COL_HIT_X, 240.0f, THEME_BG);
	int n = panel_rows(u->addrs.fam);
	for (int row = 0; row < n; row++) {
		PanelRect r;
		if (!panel_row_rect(u->addrs.fam, row, &r)) continue;
		PanelButton b = panel_row_button(u->addrs.fam, row);
		bool en  = panel_button_enabled(u->addrs.fam, b, &u->list);
		bool run = panel_seq_busy(&u->seq) && u->seq.btn == b;
		bool lit = en && (run || fb_on(u, PREG_COL, row) || held_on(u, PREG_COL, row));
		PanelRect in = { r.x, r.y + 1, r.w, r.h - 2 };
		draw_button(buf, &in, PANEL_BUTTON_LABEL[b], lit, !en, TXT_VALUE);
	}
}

static void draw_hp(float x, float y, float w, int hp, int maxHp) {
	ui_fill(x, y, w, 6.0f, THEME_LETTERBOX, 3.0f);
	if (maxHp <= 0) return;
	float f = (float)hp / (float)maxHp;
	if (f < 0.0f) f = 0.0f;
	if (f > 1.0f) f = 1.0f;
	u32 c = (f > 0.5f) ? C2D_Color32(0x58, 0xD0, 0x60, 0xFF)
	      : (f > 0.2f) ? C2D_Color32(0xF0, 0xC0, 0x30, 0xFF) : C2D_Color32(0xE8, 0x50, 0x40, 0xFF);
	if (f > 0.0f) ui_fill(x, y, w * f, 6.0f, c, 3.0f);
}

static void draw_mon(C2D_TextBuf buf, const PanelMon* m, float x, float w, bool numbers) {
	if (!m->valid) return;
	char lv[16];
	snprintf(lv, sizeof lv, "Lv%d", m->level);
	assets_text(buf, TXT_BODY, m->nick, x, 6.0f, THEME_TEXT);
	assets_text_r(buf, TXT_VALUE, lv, x + w, 8.0f, THEME_DIM);
	draw_hp(x, 28.0f, w, m->hp, m->maxHp);
	if (numbers) {
		char hp[24];
		snprintf(hp, sizeof hp, "%d/%d", m->hp, m->maxHp);
		assets_text_r(buf, TXT_VALUE, hp, x + w, 38.0f, THEME_TEXT);
	}
}

static void draw_type_chip(C2D_TextBuf buf, int type, float x, float y) {
	if (type < 0) return;
	ui_fill(x, y, 52.0f, 16.0f, rgba_to_c2d(panel_type_rgba(type)), 4.0f);
	assets_text_c(buf, TXT_CHIP, panel_type_label(type), x + 26.0f, text_y(TXT_CHIP, y, 16.0f),
	              C2D_Color32(0xFF, 0xFF, 0xFF, 0xFF));
}

static void draw_battle(const PanelUi* u, C2D_TextBuf buf) {
	const PanelBattle* b = &u->battle;
	C2D_DrawRectSolid(0.0f, 0.0f, 0.0f, 320.0f, 240.0f, THEME_BG);
	draw_mon(buf, &b->self, 8.0f, 148.0f, true);
	draw_mon(buf, &b->foe, 164.0f, 148.0f, false);
	static const char* const ACT[4] = { "FIGHT", "BAG", "POKéMON", "RUN" };
	if (u->gctx == GCTX_BATTLE_ACTION) {
		for (int s = 0; s < 4; s++) {
			PanelRect r;
			if (!panel_battle_action_rect(s, &r)) continue;
			bool lit = fb_on(u, PREG_BATTLE, s) || held_on(u, PREG_BATTLE, s);
			draw_button(buf, &r, ACT[s], lit, false, TXT_BUTTON);
			if (s == 0) {   // the active mon's move types as small chips under FIGHT
				float cx = (float)r.x + 12.0f;
				for (int i = 0; i < 4; i++) {
					if (!b->mv[i].id || b->mv[i].type < 0) continue;
					draw_type_chip(buf, b->mv[i].type, cx, (float)(r.y + r.h - 22));
					cx += 58.0f;
				}
			}
		}
		return;
	}
	for (int s = 0; s < 4; s++) {
		const PanelMove* m = &b->mv[s];
		PanelRect r;
		if (!m->id || !panel_battle_move_rect(s, &r)) continue;
		bool lit = fb_on(u, PREG_BATTLE, s) || held_on(u, PREG_BATTLE, s);
		bool dim = m->pp == 0;
		ui_panel((float)r.x, (float)r.y, (float)r.w, (float)r.h, lit ? THEME_GOLD : THEME_PANEL, THEME_LINE, 4.0f);
		u32 ink = lit ? THEME_SELTXT : (dim ? THEME_DIM : THEME_TEXT);
		assets_text(buf, TXT_BUTTON, m->name, (float)r.x + 8.0f, (float)r.y + 8.0f, ink);
		draw_type_chip(buf, m->type, (float)r.x + 8.0f, (float)(r.y + r.h - 24));
		char pp[16];
		if (m->maxPp >= 0) snprintf(pp, sizeof pp, "PP %d/%d", m->pp, m->maxPp);
		else               snprintf(pp, sizeof pp, "PP %d", m->pp);
		assets_text_r(buf, TXT_VALUE, pp, (float)(r.x + r.w - 8), (float)(r.y + r.h - 22), ink);
	}
	PanelRect back;
	panel_battle_back_rect(&back);
	draw_button(buf, &back, "BACK", fb_on(u, PREG_BATTLE, 4) || held_on(u, PREG_BATTLE, 4), false, TXT_BUTTON);
}

void panelui_draw(const PanelUi* u, C3D_Tex* frame, C2D_TextBuf buf) {
	if (!u || !u->ok) return;
	if (u->battlePanel) { draw_battle(u, buf); return; }
	C2D_DrawRectSolid(0.0f, 0.0f, 0.0f, (float)PANEL_VIEW_W, (float)PANEL_FRAME_Y, THEME_LETTERBOX);
	C2D_DrawRectSolid(0.0f, (float)(PANEL_FRAME_Y + 160), 0.0f, (float)PANEL_VIEW_W, 40.0f, THEME_LETTERBOX);
	if (frame) {
		Tex3DS_SubTexture s = { 240, 160, 0.0f, 1.0f, 240.0f / 256.0f, 1.0f - 160.0f / 256.0f };
		C2D_Image img = { frame, &s };
		C2D_DrawImageAt(img, 0.0f, (float)PANEL_FRAME_Y, 0.0f, NULL, 1.0f, 1.0f);
	}
	PanelRect chip = { PANELUI_CHIP_X, PANELUI_CHIP_Y, PANELUI_CHIP_W, PANELUI_CHIP_H };
	draw_button(buf, &chip, "MENU", fb_on(u, PREG_CHIP, 0) || held_on(u, PREG_CHIP, 0), false, TXT_VALUE);
	draw_column(u, buf);
}
