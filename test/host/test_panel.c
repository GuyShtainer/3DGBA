// test_panel.c — PC host suite for the phase-32 touch panel (source/panel.c).
//
//   clang -std=c11 -Wall -Wextra -O2 -I source test/host/test_panel.c source/panel.c \
//         source/gbatext.c -o /tmp/tpanel && /tmp/tpanel
//
//   T1  column geometry: every column pixel maps to exactly one row; rects inside their row
//   T2  game view mapping (the 240x160 frame at y 40)
//   T3  START list read + action mapping + dimming (EM and FRLG, TRAINER's link alternate)
//   T4  sequencer: field -> BAG (START, write tick, write+A)
//   T5  sequencer: START already open -> OPTION, no START press
//   T6  sequencer: from a full-screen menu -> SAVE backs out with paced B, then opens
//   T7  sequencer: absent action aborts; locked field aborts; battle aborts; link aborts
//   T8  sequencer: timeouts; a physical key PRESS cancels, a key already held does not
//   T9  MAP: POKéNAV, then UP until the main-menu cursor is 0, then A
//   T10 battle view: names, PP-bonus math, empty slots, invalid mon, unverified names
//   T11 battle panel geometry + the smart-touch cell centres hit smart touch's own cells
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "panel.h"
#include "gbatext.h"

static int g_checks = 0, g_fail = 0;
#define CHECK(c, ...) do { g_checks++; if (!(c)) { g_fail++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

// ---- fake bus: a sparse little-endian memory -------------------------------------------------
#define MEM_N 4096
static struct { uint32_t addr; uint8_t v; } g_mem[MEM_N]; static int g_memN;
static void mem_clear(void) { g_memN = 0; }
static void poke8(uint32_t a, uint8_t v) {
	for (int i = 0; i < g_memN; i++) if (g_mem[i].addr == a) { g_mem[i].v = v; return; }
	g_mem[g_memN].addr = a; g_mem[g_memN].v = v; g_memN++;
}
static void poke16(uint32_t a, uint16_t v) { poke8(a, v & 0xFF); poke8(a + 1, v >> 8); }
static uint8_t rd8(void* c, uint32_t a) { (void)c; for (int i = 0; i < g_memN; i++) if (g_mem[i].addr == a) return g_mem[i].v; return 0; }
static uint16_t rd16(void* c, uint32_t a) { return (uint16_t)(rd8(c, a) | (rd8(c, a + 1) << 8)); }
static uint32_t rd32(void* c, uint32_t a) { return (uint32_t)rd16(c, a) | ((uint32_t)rd16(c, a + 2) << 16); }
static const PanelBus BUS = { rd8, rd16, rd32, NULL };

static const PanelAddrs EM = { PFAM_EM, 0x0203760F, 0x02037610, 0x02024084, 0x0831977C, 0x0831C898, 0x0831AE38 };
static const PanelAddrs FR = { PFAM_FRLG, 0x020370F5, 0x020370F6, 0x02023BE4, 0x08247104, 0x08250C74, 0x0824F210 };

static void set_list(const PanelAddrs* a, const uint8_t* acts, int n) {
	poke8(a->startCount, (uint8_t)n);
	for (int i = 0; i < n; i++) poke8(a->startActions + (uint32_t)i, acts[i]);
}
// Gen-3 charset encode of A-Z / space / digits (enough for the fixtures).
static void put_text(uint32_t a, const char* s, int n) {
	int i = 0;
	for (; s[i] && i < n; i++) {
		char c = s[i]; uint8_t b = 0;
		if (c >= 'A' && c <= 'Z') b = (uint8_t)(0xBB + (c - 'A'));
		else if (c >= '0' && c <= '9') b = (uint8_t)(0xA1 + (c - '0'));
		else if (c == ' ') b = 0x00;
		poke8(a + (uint32_t)i, b);
	}
	if (i < n) poke8(a + (uint32_t)i, 0xFF);
}

static PanelSeqIn in_field(void) { PanelSeqIn in; memset(&in, 0, sizeof in); in.ctx = PCTX_FIELD; in.startCursor = -1; in.pnCursor = -1; return in; }

int main(void) {
	// ---- T1 ----------------------------------------------------------------------------------
	for (int fam = 0; fam < 2; fam++) {
		int rows = panel_rows((PanelFamily)fam);
		CHECK(rows == (fam ? 6 : 8), "rows fam %d = %d", fam, rows);
		int counts[8] = {0};
		for (int y = 0; y < 240; y++) for (int x = 240; x < 320; x++) {
			int r = panel_column_hit((PanelFamily)fam, x, y);
			if (r < 0 || r >= rows) { CHECK(0, "pixel %d,%d unmapped fam %d", x, y, fam); goto t1done; }
			counts[r]++;
			PanelRect rc; panel_row_rect((PanelFamily)fam, r, &rc);
			if (x >= rc.x && x < rc.x + rc.w && y >= rc.y && y < rc.y + rc.h) {
				for (int o = 0; o < rows; o++) if (o != r) {
					PanelRect q; panel_row_rect((PanelFamily)fam, o, &q);
					CHECK(!(x >= q.x && x < q.x + q.w && y >= q.y && y < q.y + q.h), "rect overlap %d/%d", r, o);
				}
			}
		}
		for (int r = 0; r < rows; r++) CHECK(counts[r] == 80 * (240 / rows), "row %d area %d", r, counts[r]);
		CHECK(panel_column_hit((PanelFamily)fam, 239, 10) == -1, "x239 is the view, not the column");
		for (int r = 0; r < rows; r++) { PanelRect rc; CHECK(panel_row_rect((PanelFamily)fam, r, &rc) && rc.h >= 28 && rc.x + rc.w <= 320, "rect %d", r); }
		CHECK(panel_row_button((PanelFamily)fam, rows) == PB_COUNT, "row past end");
	}
t1done:
	CHECK(panel_row_button(PFAM_EM, 0) == PB_MAP && panel_row_button(PFAM_EM, 7) == PB_OPTION, "EM order");
	CHECK(panel_row_button(PFAM_FRLG, 0) == PB_POKEMON, "FR has no MAP");
	for (int r = 0; r < 6; r++) CHECK(panel_row_button(PFAM_FRLG, r) != PB_POKENAV && panel_row_button(PFAM_FRLG, r) != PB_MAP, "FR row %d", r);

	// ---- T2 ----------------------------------------------------------------------------------
	{ int gx, gy;
	  CHECK(panel_view_to_gba(0, 40, &gx, &gy) && gx == 0 && gy == 0, "frame origin");
	  CHECK(panel_view_to_gba(239, 199, &gx, &gy) && gx == 239 && gy == 159, "frame corner");
	  CHECK(!panel_view_to_gba(10, 39, &gx, &gy) && !panel_view_to_gba(10, 200, &gx, &gy), "bands are dead");
	  CHECK(!panel_view_to_gba(240, 100, &gx, &gy), "column is not the frame"); }

	// ---- T3 ----------------------------------------------------------------------------------
	mem_clear();
	{ PanelStartList L;
	  uint8_t em[] = { 1, 2, 4, 5, 6, 7 };   // no POKéDEX, no POKéNAV yet (early game)
	  set_list(&EM, em, 6); panel_read_start(&BUS, &EM, &L);
	  CHECK(L.valid && L.n == 6, "read list");
	  CHECK(!panel_button_enabled(PFAM_EM, PB_POKEDEX, &L), "no dex -> dimmed");
	  CHECK(!panel_button_enabled(PFAM_EM, PB_POKENAV, &L) && !panel_button_enabled(PFAM_EM, PB_MAP, &L), "no nav -> nav+map dimmed");
	  CHECK(panel_button_enabled(PFAM_EM, PB_BAG, &L) && panel_list_index(&L, panel_button_action(PFAM_EM, PB_BAG, &L)) == 1, "bag idx 1");
	  uint8_t emlink[] = { 0, 1, 2, 3, 9, 5, 6, 7 };
	  set_list(&EM, emlink, 8); panel_read_start(&BUS, &EM, &L);
	  CHECK(panel_button_action(PFAM_EM, PB_TRAINER, &L) == 9, "trainer -> PLAYER_LINK alternate");
	  CHECK(panel_button_action(PFAM_EM, PB_MAP, &L) == 3, "map -> pokenav");
	  PanelStartList none; memset(&none, 0, sizeof none);
	  CHECK(panel_button_enabled(PFAM_EM, PB_POKENAV, &none), "unread list enables everything");
	  CHECK(!panel_button_enabled(PFAM_FRLG, PB_POKENAV, &none), "FR has no pokenav action at all");
	  uint8_t fr[] = { 0, 1, 2, 3, 4, 5, 6 };
	  set_list(&FR, fr, 7); panel_read_start(&BUS, &FR, &L);
	  CHECK(panel_list_index(&L, panel_button_action(PFAM_FRLG, PB_SAVE, &L)) == 4, "FR save idx");
	  poke8(EM.startCount, 0); panel_read_start(&BUS, &EM, &L); CHECK(!L.valid, "count 0 invalid");
	  poke8(EM.startCount, 12); panel_read_start(&BUS, &EM, &L); CHECK(!L.valid, "count 12 invalid"); }

	// ---- T4 field -> BAG ----------------------------------------------------------------------
	{ PanelSeq s; panel_seq_start(&s, PFAM_EM, PB_BAG);
	  PanelSeqIn in = in_field();
	  PanelSeqOut o = panel_seq_step(&s, &in);
	  CHECK(o.keys == (1u << 3) && o.startCursor == -1, "field: START first (keys %x)", o.keys);
	  o = panel_seq_step(&s, &in); CHECK(o.keys == 0 && panel_seq_busy(&s), "waiting for the menu");
	  in.ctx = PCTX_START; in.startCursor = 0;
	  uint8_t em[] = { 0, 1, 2, 3, 4, 5, 6, 7 }; in.list.valid = true; in.list.n = 8; memcpy(in.list.act, em, 8);
	  o = panel_seq_step(&s, &in); CHECK(o.startCursor == 2 && o.keys == 0, "tick0 write only");
	  o = panel_seq_step(&s, &in); CHECK(o.startCursor == 2 && o.keys == 1, "tick1 write + A");
	  CHECK(s.status == PSEQ_DONE, "done");
	  o = panel_seq_step(&s, &in); CHECK(o.keys == 0 && o.startCursor == -1, "inert after done"); }

	// ---- T5 START open -> OPTION -------------------------------------------------------------
	{ PanelSeq s; panel_seq_start(&s, PFAM_FRLG, PB_OPTION);
	  PanelSeqIn in = in_field(); in.ctx = PCTX_START; in.startCursor = 3;
	  uint8_t fr[] = { 0, 1, 2, 3, 4, 5, 6 }; in.list.valid = true; in.list.n = 7; memcpy(in.list.act, fr, 7);
	  PanelSeqOut o = panel_seq_step(&s, &in);
	  CHECK(!(o.keys & (1u << 3)) && o.startCursor == 5, "no START when already open; cursor 5");
	  o = panel_seq_step(&s, &in); CHECK(o.keys == 1 && s.status == PSEQ_DONE, "A"); }

	// ---- T6 menu -> SAVE ---------------------------------------------------------------------
	{ PanelSeq s; panel_seq_start(&s, PFAM_EM, PB_SAVE);
	  PanelSeqIn in = in_field(); in.ctx = PCTX_MENU;
	  int bs = 0, frames = 0; PanelSeqOut o;
	  for (; frames < 20; frames++) { o = panel_seq_step(&s, &in); if (o.keys == 2) bs++; }
	  CHECK(bs == 3, "B paced every %d frames (got %d in 20)", PANEL_B_GAP, bs);
	  in.ctx = PCTX_START; in.startCursor = 1;
	  uint8_t em[] = { 1, 2, 4, 5, 6, 7 }; in.list.valid = true; in.list.n = 6; memcpy(in.list.act, em, 6);
	  o = panel_seq_step(&s, &in); CHECK(o.startCursor == 3, "save at idx 3 (got %d)", o.startCursor);
	  o = panel_seq_step(&s, &in); CHECK(o.keys == 1 && s.status == PSEQ_DONE, "A");
	  // a menu that will not close: gives up
	  panel_seq_start(&s, PFAM_EM, PB_SAVE); in.ctx = PCTX_MENU; bs = 0;
	  for (frames = 0; frames < 500 && panel_seq_busy(&s); frames++) { o = panel_seq_step(&s, &in); if (o.keys == 2) bs++; }
	  CHECK(s.status == PSEQ_ABORTED && s.abortWhy == PABORT_EXITFAIL && bs == PANEL_B_MAX, "exit gives up after %d B (got %d)", PANEL_B_MAX, bs); }

	// ---- T7 aborts ---------------------------------------------------------------------------
	{ PanelSeq s; PanelSeqIn in;
	  panel_seq_start(&s, PFAM_EM, PB_POKEDEX); in = in_field(); in.ctx = PCTX_START;
	  uint8_t em[] = { 1, 2, 4, 5, 6, 7 }; in.list.valid = true; in.list.n = 6; memcpy(in.list.act, em, 6);
	  panel_seq_step(&s, &in); CHECK(s.status == PSEQ_ABORTED && s.abortWhy == PABORT_ABSENT, "absent dex aborts");
	  panel_seq_start(&s, PFAM_EM, PB_BAG); in = in_field(); in.fieldLock = true;
	  PanelSeqOut o = panel_seq_step(&s, &in); CHECK(o.keys == 0 && s.abortWhy == PABORT_LOCKED, "locked field never presses START");
	  panel_seq_start(&s, PFAM_EM, PB_BAG); in = in_field(); in.ctx = PCTX_BATTLE;
	  o = panel_seq_step(&s, &in); CHECK(o.keys == 0 && s.abortWhy == PABORT_CTX, "battle aborts");
	  panel_seq_start(&s, PFAM_EM, PB_BAG); in = in_field(); in.ctx = PCTX_OTHER;
	  o = panel_seq_step(&s, &in); CHECK(o.keys == 0 && s.abortWhy == PABORT_CTX, "script prompt / title aborts, never B");
	  panel_seq_start(&s, PFAM_EM, PB_BAG); in = in_field(); in.link = true;
	  o = panel_seq_step(&s, &in); CHECK(o.keys == 0 && s.abortWhy == PABORT_LINK, "link aborts");
	  // START menu closes under us mid-select
	  panel_seq_start(&s, PFAM_EM, PB_BAG); in = in_field(); in.ctx = PCTX_START; in.list.valid = true; in.list.n = 6; memcpy(in.list.act, em, 6);
	  panel_seq_step(&s, &in); in.ctx = PCTX_FIELD; o = panel_seq_step(&s, &in);
	  CHECK(o.keys == 0 && s.abortWhy == PABORT_CTX, "menu vanished between ticks -> no stray A"); }

	// ---- T8 timeout + user cancel ------------------------------------------------------------
	{ PanelSeq s; PanelSeqIn in = in_field(); int f;
	  panel_seq_start(&s, PFAM_EM, PB_BAG);
	  for (f = 0; f < 200 && panel_seq_busy(&s); f++) panel_seq_step(&s, &in);
	  CHECK(s.abortWhy == PABORT_TIMEOUT && f == PANEL_WAIT_FRAMES + 2, "START that never opens times out at %d (f=%d)", PANEL_WAIT_FRAMES + 2, f);
	  panel_seq_start(&s, PFAM_EM, PB_BAG); in = in_field(); in.padKeys = 1u << 7;   // DOWN already held
	  panel_seq_step(&s, &in); panel_seq_step(&s, &in); panel_seq_step(&s, &in);
	  CHECK(panel_seq_busy(&s), "a key held at tap time (and kept held) is not a cancel");
	  in.padKeys = (1u << 7) | 1u; panel_seq_step(&s, &in);
	  CHECK(s.abortWhy == PABORT_USER, "a NEW physical press cancels");
	  panel_seq_start(&s, PFAM_EM, PB_BAG); panel_seq_cancel(&s); CHECK(s.status == PSEQ_ABORTED, "explicit cancel"); }

	// ---- T9 MAP ------------------------------------------------------------------------------
	{ PanelSeq s; panel_seq_start(&s, PFAM_EM, PB_MAP);
	  PanelSeqIn in = in_field(); in.ctx = PCTX_START; in.startCursor = 0;
	  uint8_t em[] = { 0, 1, 2, 3, 4, 5, 6, 7 }; in.list.valid = true; in.list.n = 8; memcpy(in.list.act, em, 8);
	  PanelSeqOut o = panel_seq_step(&s, &in); CHECK(o.startCursor == 3, "map opens pokenav idx 3");
	  o = panel_seq_step(&s, &in); CHECK(o.keys == 1 && panel_seq_busy(&s), "A, still running");
	  in.ctx = PCTX_MENU; for (int f = 0; f < 30; f++) { o = panel_seq_step(&s, &in); CHECK(o.keys == 0, "no keys during the fade"); }
	  in.ctx = PCTX_POKENAV_MAIN; in.pnCursor = 2; int ups = 0, f;
	  for (f = 0; f < 40 && panel_seq_busy(&s); f++) {
	      o = panel_seq_step(&s, &in);
	      if (o.keys == (1u << 6)) { ups++; in.pnCursor--; }
	      if (o.keys == 1) break;
	  }
	  CHECK(ups == 2 && o.keys == 1 && s.status == PSEQ_DONE, "2 UPs then A (ups %d)", ups); }

	// ---- T10 battle view ---------------------------------------------------------------------
	mem_clear();
	{ uint32_t b0 = EM.battleMons, b1 = EM.battleMons + 0x58;
	  poke16(b0, 280); poke16(b0 + 0x28, 30); poke8(b0 + 0x2A, 12); poke16(b0 + 0x2C, 41);
	  put_text(b0 + 0x30, "RALTS", 11);
	  poke16(b0 + 0x0C, 45); poke16(b0 + 0x0E, 0); poke16(b0 + 0x10, 93); poke16(b0 + 0x12, 0);
	  poke8(b0 + 0x24, 40); poke8(b0 + 0x26, 25);
	  poke8(b0 + 0x3B, (uint8_t)(3 << 4));                 // slot 2: 3 PP-ups
	  put_text(EM.moveNames + 13 * 45, "GROWL", 13); put_text(EM.moveNames + 13 * 93, "CONFUSION", 13);
	  poke8(EM.battleMoves + 12 * 45 + 2, 0);  poke8(EM.battleMoves + 12 * 45 + 4, 40);
	  poke8(EM.battleMoves + 12 * 93 + 2, 14); poke8(EM.battleMoves + 12 * 93 + 4, 25);
	  poke16(b1, 263); poke16(b1 + 0x28, 10); poke8(b1 + 0x2A, 3); poke16(b1 + 0x2C, 14); put_text(b1 + 0x30, "ZIGZAGOON", 11);
	  PanelBattle B; panel_read_battle(&BUS, &EM, gbatext_decode, 0, 1, &B);
	  CHECK(B.self.valid && strcmp(B.self.nick, "RALTS") == 0 && B.self.level == 12 && B.self.hp == 30, "self (%s)", B.self.nick);
	  CHECK(B.foe.valid && strcmp(B.foe.nick, "ZIGZAGOON") == 0, "foe (%s)", B.foe.nick);
	  CHECK(B.nMoves == 2 && B.mv[1].id == 0 && B.mv[3].id == 0, "empty slots skipped");
	  CHECK(strcmp(B.mv[0].name, "GROWL") == 0 && B.mv[0].type == 0 && B.mv[0].maxPp == 40, "growl");
	  CHECK(strcmp(B.mv[2].name, "CONFUSION") == 0 && B.mv[2].type == 14 && B.mv[2].maxPp == 40 && B.mv[2].pp == 25, "confusion 25/40 (max %d)", B.mv[2].maxPp);
	  CHECK(panel_max_pp(5, 3) == 8 && panel_max_pp(35, 1) == 42 && panel_max_pp(10, 0) == 10 && panel_max_pp(1, 3) == 1, "pp math");
	  PanelAddrs un = EM; un.moveNames = 0; un.battleMoves = 0;
	  panel_read_battle(&BUS, &un, gbatext_decode, 0, 1, &B);
	  CHECK(strcmp(B.mv[0].name, "MOVE 1") == 0 && strcmp(B.mv[2].name, "MOVE 3") == 0 && B.mv[0].type == -1 && B.mv[0].maxPp == -1, "unverified names -> MOVE n, no type");
	  poke16(b0 + 0x28, 50);                               // hp > maxHp: garbage
	  panel_read_battle(&BUS, &EM, gbatext_decode, 0, 1, &B);
	  CHECK(!B.self.valid && B.nMoves == 0, "invalid self -> no moves");
	  CHECK(panel_type_rgba(10) != panel_type_rgba(11) && panel_type_rgba(99) == 0x808080FFu, "type colours");
	  CHECK(strcmp(panel_type_label(9), "???") == 0, "mystery type"); }

	// ---- T11 battle geometry -----------------------------------------------------------------
	{ // replicas of touch.c hit_action / hit_move (the cells the synthetic contact must land in)
	  for (int s = 0; s < 4; s++) {
		int gx, gy;
		CHECK(panel_battle_cell(0, s, &gx, &gy), "cell");
		int a = (gx < 136 || gx >= 232 || gy < 120 || gy >= 152) ? -1 : (((gx >= 184) ? 1 : 0) | ((gy >= 136) ? 2 : 0));
		CHECK(a == s, "action cell %d lands in %d", s, a);
		{ int cx0 = (s & 1) ? 184 : 136, cy0 = (s & 2) ? 136 : 120;   // centre margin >= 8 px (finger-safe replay)
		  CHECK(gx - cx0 >= 8 && cx0 + 48 - gx >= 8 && gy - cy0 >= 4 && cy0 + 16 - gy >= 4, "action cell %d centred (%d,%d)", s, gx, gy); }
		CHECK(panel_battle_cell(1, s, &gx, &gy), "cell");
		int m = (gx < 16 || gx >= 152 || gy < 120 || gy >= 152) ? -1 : (((gx >= 84) ? 1 : 0) | ((gy >= 136) ? 2 : 0));
		CHECK(m == s, "move cell %d lands in %d", s, m);
		{ int cx0 = (s & 1) ? 84 : 16, cy0 = (s & 2) ? 136 : 120;
		  CHECK(gx - cx0 >= 8 && cx0 + 68 - gx >= 8 && gy - cy0 >= 4 && cy0 + 16 - gy >= 4, "move cell %d centred (%d,%d)", s, gx, gy); }
		PanelRect r; CHECK(panel_battle_action_rect(s, &r), "rect");
		CHECK(panel_battle_action_hit(r.x + r.w / 2, r.y + r.h / 2) == s, "action hit %d", s);
		CHECK(r.h >= 64 && r.x >= 0 && r.x + r.w <= 320 && r.y + r.h <= 240, "action rect size %d", s);
		CHECK(panel_battle_move_rect(s, &r) && panel_battle_move_hit(r.x + r.w / 2, r.y + r.h / 2) == s, "move hit %d", s);
		CHECK(r.h >= 64 && r.x + r.w <= 320, "move rect size %d", s);
	  }
	  PanelRect r; panel_battle_back_rect(&r);
	  CHECK(panel_battle_move_hit(r.x + 5, r.y + 5) == 4, "BACK");
	  CHECK(panel_battle_action_hit(2, 2) == -1 && panel_battle_move_hit(2, 2) == -1, "header is dead");
	  CHECK(!panel_battle_cell(2, 0, &r.x, &r.y) && !panel_battle_cell(0, 4, &r.x, &r.y), "bad cell args"); }

	printf("test_panel: %d checks, %d failed\n", g_checks, g_fail);
	return g_fail ? 1 : 0;
}
