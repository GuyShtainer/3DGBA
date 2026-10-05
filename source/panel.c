// panel.c — phase 32 track T: the single-game touch PANEL. See panel.h and
// docs/phase32-voxel/SPEC-touch-panel.md. Pure C, read-only.
#include "panel.h"
#include <string.h>

// GBA keypad bits (KEYINPUT order, gbacore.h GBAKEY_*).
#define K_A      (1u << 0)
#define K_B      (1u << 1)
#define K_START  (1u << 3)
#define K_UP     (1u << 6)

const char* const PANEL_BUTTON_LABEL[PB_COUNT] = {
	"MAP", "POKéMON", "BAG", "TRAINER", "POKéDEX", "POKéNAV", "SAVE", "OPTION"
};

// ---- geometry -------------------------------------------------------------------------------
static const PanelButton ORDER_EM[8]   = { PB_MAP, PB_POKEMON, PB_BAG, PB_TRAINER,
                                           PB_POKEDEX, PB_POKENAV, PB_SAVE, PB_OPTION };
static const PanelButton ORDER_FRLG[6] = { PB_POKEMON, PB_BAG, PB_TRAINER,
                                           PB_POKEDEX, PB_SAVE, PB_OPTION };

int panel_rows(PanelFamily fam) { return fam == PFAM_FRLG ? 6 : 8; }
static int row_pitch(PanelFamily fam) { return PANEL_VIEW_H / panel_rows(fam); }   // 30 / 40

PanelButton panel_row_button(PanelFamily fam, int row) {
	if (row < 0 || row >= panel_rows(fam)) return PB_COUNT;
	return fam == PFAM_FRLG ? ORDER_FRLG[row] : ORDER_EM[row];
}

bool panel_row_rect(PanelFamily fam, int row, PanelRect* r) {
	if (row < 0 || row >= panel_rows(fam)) return false;
	int pitch = row_pitch(fam);
	r->x = PANEL_COL_X; r->w = PANEL_COL_W;
	r->y = row * pitch + 1; r->h = pitch - 2;
	return true;
}

int panel_column_hit(PanelFamily fam, int sx, int sy) {
	if (sx < PANEL_COL_HIT_X || sx >= 320 || sy < 0 || sy >= PANEL_VIEW_H) return -1;
	int row = sy / row_pitch(fam);
	if (row >= panel_rows(fam)) row = panel_rows(fam) - 1;
	return row;
}

bool panel_view_to_gba(int sx, int sy, int* gx, int* gy) {
	if (sx < 0 || sx >= 240 || sy < PANEL_FRAME_Y || sy >= PANEL_FRAME_Y + 160) return false;
	*gx = sx; *gy = sy - PANEL_FRAME_Y;
	return true;
}

// ---- START list -----------------------------------------------------------------------------
void panel_read_start(const PanelBus* bus, const PanelAddrs* a, PanelStartList* out) {
	memset(out, 0, sizeof *out);
	if (!bus || !a || !a->startCount || !a->startActions) return;
	int n = bus->rd8(bus->ctx, a->startCount);
	if (n <= 0 || n > PANEL_START_MAX) return;
	out->n = (uint8_t)n;
	for (int i = 0; i < n; i++) out->act[i] = bus->rd8(bus->ctx, a->startActions + (uint32_t)i);
	out->valid = true;
}

int panel_list_index(const PanelStartList* list, int action) {
	if (!list || !list->valid || action < 0) return -1;
	for (int i = 0; i < list->n; i++) if (list->act[i] == action) return i;
	return -1;
}

// pret src/start_menu.c action enums. EM: POKEDEX 0, POKEMON 1, BAG 2, POKENAV 3, PLAYER 4,
// SAVE 5, OPTION 6, PLAYER_LINK 9. FRLG: POKEDEX 0, POKEMON 1, BAG 2, PLAYER 3, SAVE 4,
// OPTION 5, PLAYER2 8.
static int primary_action(PanelFamily fam, PanelButton b, int* alt) {
	*alt = -1;
	if (fam == PFAM_FRLG) {
		switch (b) {
		case PB_POKEDEX: return 0; case PB_POKEMON: return 1; case PB_BAG: return 2;
		case PB_TRAINER: *alt = 8; return 3; case PB_SAVE: return 4; case PB_OPTION: return 5;
		default: return -1;
		}
	}
	switch (b) {
	case PB_POKEDEX: return 0; case PB_POKEMON: return 1; case PB_BAG: return 2;
	case PB_POKENAV: case PB_MAP: return 3;
	case PB_TRAINER: *alt = 9; return 4; case PB_SAVE: return 5; case PB_OPTION: return 6;
	default: return -1;
	}
}

int panel_button_action(PanelFamily fam, PanelButton b, const PanelStartList* list) {
	int alt, act = primary_action(fam, b, &alt);
	if (act < 0) return -1;
	if (list && list->valid && panel_list_index(list, act) < 0 && alt >= 0 &&
	    panel_list_index(list, alt) >= 0) return alt;
	return act;
}

bool panel_button_enabled(PanelFamily fam, PanelButton b, const PanelStartList* list) {
	int act = panel_button_action(fam, b, list);
	if (act < 0) return false;
	if (!list || !list->valid) return true;
	return panel_list_index(list, act) >= 0;
}

// ---- sequencer ------------------------------------------------------------------------------
enum { ST_EXIT = 0, ST_OPEN, ST_NAV, ST_MAPWAIT, ST_MAPCONFIRM };

void panel_seq_start(PanelSeq* s, PanelFamily fam, PanelButton b) {
	memset(s, 0, sizeof *s);
	s->status = PSEQ_RUNNING; s->fam = fam; s->btn = b; s->step = ST_EXIT; s->prevPad = 0xFFFF;
}
void panel_seq_cancel(PanelSeq* s) { if (s->status == PSEQ_RUNNING) { s->status = PSEQ_ABORTED; s->abortWhy = PABORT_USER; } }
bool panel_seq_busy(const PanelSeq* s) { return s->status == PSEQ_RUNNING; }

static PanelSeqOut none(void) { PanelSeqOut o = { 0, -1 }; return o; }
static PanelSeqOut fail(PanelSeq* s, int why) { s->status = PSEQ_ABORTED; s->abortWhy = why; return none(); }
static void go(PanelSeq* s, int step) { s->step = step; s->timer = 0; s->sub = 0; }

PanelSeqOut panel_seq_step(PanelSeq* s, const PanelSeqIn* in) {
	if (s->status != PSEQ_RUNNING) return none();
	// A physical key PRESS (an edge, not a key already held when the tap began) yields (SPEC T2.6).
	if (s->prevPad != 0xFFFF && (in->padKeys & (uint16_t)~s->prevPad)) { s->prevPad = in->padKeys; return fail(s, PABORT_USER); }
	s->prevPad = in->padKeys;
	if (in->link) return fail(s, PABORT_LINK);

	PanelSeqOut o = none();
	switch (s->step) {
	case ST_EXIT:
		if (in->ctx == PCTX_FIELD) {
			if (in->fieldLock) return fail(s, PABORT_LOCKED);
			go(s, ST_OPEN);
			o.keys = K_START;                         // press START this frame
			s->sub = 1;
			return o;
		}
		if (in->ctx == PCTX_START) { go(s, ST_NAV); break; }
		if (in->ctx == PCTX_MENU || in->ctx == PCTX_POKENAV_MAIN) {   // back out with B, paced
			if (s->sub < PANEL_B_MAX) {
				if (s->timer % PANEL_B_GAP == 0) { o.keys = K_B; s->sub++; }
			} else if (s->timer >= PANEL_B_MAX * PANEL_B_GAP + PANEL_WAIT_FRAMES) {
				return fail(s, PABORT_EXITFAIL);
			}
			s->timer++;
			return o;
		}
		return fail(s, PABORT_CTX);

	case ST_OPEN:
		if (in->ctx == PCTX_START) { go(s, ST_NAV); break; }
		if (++s->timer > PANEL_WAIT_FRAMES) return fail(s, PABORT_TIMEOUT);
		return o;

	case ST_MAPWAIT:
		if (in->ctx == PCTX_POKENAV_MAIN && in->pnCursor >= 0) { go(s, ST_MAPCONFIRM); break; }
		if (++s->timer > PANEL_NAV_WAIT) return fail(s, PABORT_TIMEOUT);
		return o;

	default: break;
	}

	switch (s->step) {
	case ST_NAV: {
		if (in->ctx != PCTX_START) return fail(s, PABORT_CTX);
		int act = panel_button_action(s->fam, s->btn, &in->list);
		int idx = panel_list_index(&in->list, act);
		if (idx < 0) return fail(s, PABORT_ABSENT);
		o.startCursor = idx;                          // tick 0: write only; tick 1: write + A
		if (s->sub == 1) {
			o.keys = K_A;
			if (s->btn == PB_MAP) go(s, ST_MAPWAIT);
			else s->status = PSEQ_DONE;
		} else s->sub = 1;
		return o;
	}
	case ST_MAPCONFIRM:
		if (in->ctx != PCTX_POKENAV_MAIN) return fail(s, PABORT_CTX);
		if (++s->timer > PANEL_WAIT_FRAMES) return fail(s, PABORT_TIMEOUT);
		if (in->pnCursor == 0) { o.keys = K_A; s->status = PSEQ_DONE; return o; }
		if ((s->sub++ & 3) == 0) o.keys = K_UP;       // one UP edge per 4 frames, cursor re-read
		return o;
	default:
		return fail(s, PABORT_CTX);
	}
}

// ---- battle view model ----------------------------------------------------------------------
#define BMON_STRIDE  0x58u
#define BMON_MOVES   0x0Cu
#define BMON_PP      0x24u
#define BMON_HP      0x28u
#define BMON_LEVEL   0x2Au
#define BMON_MAXHP   0x2Cu
#define BMON_NICK    0x30u
#define BMON_PPBONUS 0x3Bu
#define NICK_BYTES   11
#define MOVE_NAME_B  13
#define MOVE_STRIDE  12u
#define MOVES_COUNT  355
#define SPECIES_MAX  450

int panel_max_pp(int basePp, int bonus) {
	if (basePp <= 0) return 0;
	if (bonus < 0) bonus = 0;
	if (bonus > 3) bonus = 3;
	return basePp + (basePp * bonus) / 5;
}

static void read_mon(const PanelBus* bus, uint32_t base, PanelDecodeFn dec, PanelMon* m) {
	memset(m, 0, sizeof *m);
	m->species = bus->rd16(bus->ctx, base);
	m->hp      = bus->rd16(bus->ctx, base + BMON_HP);
	m->level   = bus->rd8 (bus->ctx, base + BMON_LEVEL);
	m->maxHp   = bus->rd16(bus->ctx, base + BMON_MAXHP);
	if (m->species == 0 || m->species >= SPECIES_MAX || m->maxHp <= 0 || m->hp > m->maxHp ||
	    m->level < 1 || m->level > 100) { m->valid = false; return; }
	uint8_t raw[NICK_BYTES];
	for (int i = 0; i < NICK_BYTES; i++) raw[i] = bus->rd8(bus->ctx, base + BMON_NICK + (uint32_t)i);
	if (dec) dec(raw, NICK_BYTES, m->nick, PANEL_NAME_MAX);
	m->valid = true;
}

void panel_read_battle(const PanelBus* bus, const PanelAddrs* a, PanelDecodeFn dec,
                       int selfBattler, int foeBattler, PanelBattle* out) {
	memset(out, 0, sizeof *out);
	for (int i = 0; i < 4; i++) { out->mv[i].type = -1; out->mv[i].maxPp = -1; }
	if (!bus || !a || !a->battleMons || selfBattler < 0 || selfBattler > 3) return;
	uint32_t self = a->battleMons + BMON_STRIDE * (uint32_t)selfBattler;
	read_mon(bus, self, dec, &out->self);
	if (foeBattler >= 0 && foeBattler <= 3)
		read_mon(bus, a->battleMons + BMON_STRIDE * (uint32_t)foeBattler, dec, &out->foe);
	if (!out->self.valid) return;
	uint8_t bonuses = bus->rd8(bus->ctx, self + BMON_PPBONUS);
	for (int i = 0; i < 4; i++) {
		PanelMove* mv = &out->mv[i];
		uint16_t id = bus->rd16(bus->ctx, self + BMON_MOVES + 2u * (uint32_t)i);
		if (id == 0 || id >= MOVES_COUNT) { mv->id = 0; continue; }
		mv->id = id;
		mv->pp = bus->rd8(bus->ctx, self + BMON_PP + (uint32_t)i);
		if (a->moveNames && dec) {
			uint8_t raw[MOVE_NAME_B];
			for (int k = 0; k < MOVE_NAME_B; k++) raw[k] = bus->rd8(bus->ctx, a->moveNames + MOVE_NAME_B * (uint32_t)id + (uint32_t)k);
			dec(raw, MOVE_NAME_B, mv->name, PANEL_NAME_MAX);
		}
		if (!mv->name[0]) { mv->name[0] = 'M'; memcpy(mv->name, "MOVE ", 5); mv->name[5] = (char)('1' + i); mv->name[6] = 0; }
		if (a->battleMoves) {
			uint32_t m = a->battleMoves + MOVE_STRIDE * (uint32_t)id;
			int t = bus->rd8(bus->ctx, m + 2);
			mv->type  = (t >= 0 && t <= 17) ? t : -1;
			mv->maxPp = panel_max_pp(bus->rd8(bus->ctx, m + 4), (bonuses >> (2 * i)) & 3);
		}
		out->nMoves++;
	}
}

static const uint32_t TYPE_RGBA[18] = {
	0xA8A878FFu, 0xC03028FFu, 0xA890F0FFu, 0xA040A0FFu, 0xE0C068FFu, 0xB8A038FFu,
	0xA8B820FFu, 0x705898FFu, 0xB8B8D0FFu, 0x68A090FFu, 0xF08030FFu, 0x6890F0FFu,
	0x78C850FFu, 0xF8D030FFu, 0xF85888FFu, 0x98D8D8FFu, 0x7038F8FFu, 0x705848FFu
};
static const char* const TYPE_LABEL[18] = {
	"NORMAL", "FIGHT", "FLYING", "POISON", "GROUND", "ROCK", "BUG", "GHOST", "STEEL",
	"???", "FIRE", "WATER", "GRASS", "ELECTR", "PSYCHC", "ICE", "DRAGON", "DARK"
};
uint32_t panel_type_rgba(int type) { return (type >= 0 && type < 18) ? TYPE_RGBA[type] : 0x808080FFu; }
const char* panel_type_label(int type) { return (type >= 0 && type < 18) ? TYPE_LABEL[type] : "?"; }

// smart touch's battle hit geometry (touch.c hit_action / hit_move): 2x2 cells, slot bit 0 =
// right column, bit 1 = bottom row. Centres of each cell.
bool panel_battle_cell(int kind, int slot, int* gx, int* gy) {
	if (slot < 0 || slot > 3) return false;
	int right = slot & 1, bottom = (slot >> 1) & 1;
	if (kind == 0)      { *gx = right ? 208 : 160; }
	else if (kind == 1) { *gx = right ? 118 : 50; }
	else return false;
	*gy = bottom ? 144 : 128;
	return true;
}

bool panel_battle_action_rect(int slot, PanelRect* r) {
	switch (slot) {
	case 0: r->x = 8;   r->y = 64;  r->w = 304; r->h = 72; return true;   // FIGHT
	case 1: r->x = 8;   r->y = 144; r->w = 96;  r->h = 88; return true;   // BAG
	case 2: r->x = 112; r->y = 144; r->w = 96;  r->h = 88; return true;   // POKéMON
	case 3: r->x = 216; r->y = 144; r->w = 96;  r->h = 88; return true;   // RUN
	default: return false;
	}
}
bool panel_battle_move_rect(int slot, PanelRect* r) {
	if (slot < 0 || slot > 3) return false;
	r->x = (slot & 1) ? 164 : 8; r->y = (slot & 2) ? 136 : 64; r->w = 148; r->h = 64;
	return true;
}
void panel_battle_back_rect(PanelRect* r) { r->x = 8; r->y = 208; r->w = 304; r->h = 28; }

static bool in_rect(const PanelRect* r, int x, int y) { return x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h; }

int panel_battle_action_hit(int sx, int sy) {
	PanelRect r;
	for (int i = 0; i < 4; i++) if (panel_battle_action_rect(i, &r) && in_rect(&r, sx, sy)) return i;
	return -1;
}
int panel_battle_move_hit(int sx, int sy) {
	PanelRect r;
	for (int i = 0; i < 4; i++) if (panel_battle_move_rect(i, &r) && in_rect(&r, sx, sy)) return i;
	panel_battle_back_rect(&r);
	return in_rect(&r, sx, sy) ? 4 : -1;
}
