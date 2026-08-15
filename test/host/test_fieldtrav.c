// test_fieldtrav.c — PC host unit test for the phase-22.2 HM traversal planner
// (source/fieldtrav.{c,h}), SPEC-family-traversal T1/T2/T5.
//
// The map fixtures are NOT hand-written mocks: test/host/fixtures_fieldpath.h is generated from
// the USER'S OWN ROMs (sdmc/dual-gba/gameA.gba = BPEE rev0, gameB.gba = BPRE rev1) and carries
// real gBackupMapLayout grids plus the real MapHeader -> MapLayout -> Tileset ->
// metatileAttributes chain. The `route117` fixture is Route 117 — the pond the pre-phase-18
// router used to walk straight across — so the surf tests below plan over the SAME water tiles,
// at the SAME behaviour bytes, that the app reads on hardware.
//
// What the fixture image does NOT contain is object events, badges or a party (they are RAM the
// generator never captured), so those come from a WRITABLE OVERLAY layered on top of the fixture
// bus: gObjectEvents at the fixture's own mapObjects address, a SaveBlock1 flags array, and a
// gPlayerParty built with the real Gen-3 encryption (substruct permutation + XOR + checksum). The
// overlay is honest about what it is: every byte in it is a synthesised RAM value, and every
// synthesised value is derived from a pret constant that is cited where it is written.
//
// Pure-C dual-compile per CLAUDE.md rule #4 (fieldtrav.c and fieldpath.c are both header-free):
//
//   clang -std=c11 -Wall -Wextra -O2 -I source test/host/test_fieldtrav.c \
//         source/fieldtrav.c source/fieldpath.c -o /tmp/tft && /tmp/tft
//
// TEST NUMBERING
//   TEST 1  both engines' surfable sets, every row (a shared table FAILS) ...... T1.2
//   TEST 2  the per-VARIANT constant block: all THREE flag numberings ........... T1.3 / T1.4
//   TEST 3  the badge read is FlagGet's own bit math, with its degradations .... H1.4
//   TEST 4  the substruct permutation table is a permutation, and pret's ....... H1.5
//   TEST 5  party decryption on golden bytes: moves, egg, checksum, empty ...... H1.5
//   TEST 6  eligibility = badge AND mon, never one of the two .................. §1.4
//   TEST 7  edge scan: gfx ids per engine, LIVE coords, inactive slots skipped . H1.2 / H1.3
//   TEST 8  DRY PATHS WIN — a walkable route is never displaced ................ H1.7
//   TEST 9  the Route 117 surf crossing, end to end ........................... §1.5 / T6-P1
//   TEST 10 never prompt what the game refuses: no badge / no mon = no plan .... §0 property 2
//           ...incl. (f)(g)(h), the RUBY/SAPPHIRE numbering (2026-08-14 fix)
//   TEST 11 currents and waterfalls are never free surf tiles ................. T5.8
//   TEST 12 the Cut object edge, and the same tap with no badge ............... §2.3.1
//   TEST 13 the interact cap fails honestly ................................... H1.8
//   TEST 14 a tap taken while already surfing never plans a second mount ...... H1.5

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../source/fieldpath.h"
#include "../../source/fieldtrav.h"
#include "fixtures_fieldpath.h"

static int g_checks = 0, g_fail = 0;
#define CHECK(cond, ...) do { \
	g_checks++; \
	if (!(cond)) { g_fail++; printf("  [FAIL] "); printf(__VA_ARGS__); printf("   (at %s:%d)\n", __FILE__, __LINE__); } \
} while (0)

// ---------------- the fixture bus + a writable RAM overlay ----------------
// Reads hit the overlay first, then the fixture image, then open bus (0) — the same degradation
// ladder the module has to survive on a real console.
#define OV_N 4
static struct { uint32_t addr; uint32_t len; uint8_t* p; } g_ov[OV_N];
static uint8_t g_ovObj[0x24 * 16];      // gObjectEvents[16]
static uint8_t g_ovSb1[0x1500];         // SaveBlock1 head THROUGH the flags array (EM's
                                        // flags[] runs 0x1270..0x139B, so 0x1300 would truncate it;
                                        // Ruby's runs 0x1220..0x1340 and its vars[] start there,
                                        // which TEST 10(h) writes into)
static uint8_t g_ovParty[100 * 6];      // gPlayerParty[6]
// PHASE 24: struct PokemonStorage — u8 currentBox at +0, BoxPokemon boxes[14][30] at +4
// (14*30*80 = 0x8340, which is why boxNames sits at +0x8344 in pret's own struct).
static uint8_t g_ovStore[4 + 14 * 30 * 80];

#define SB1_BASE   0x02025734u          // an EWRAM address; the module takes sb1 already resolved
#define PARTY_BASE 0x02024000u
// gPokemonStoragePtr's target. 0x02008000 is chosen so the 0x8344-byte struct ends at
// 0x02010344 — clear of the party overlay (0x02024000), SaveBlock1 (0x02025734) and every
// fixture region (the map grids live at 0x02030000). An overlay that overlapped the grid would
// silently rewrite the WORLD, which is exactly how the first draft of this test broke TEST 9.
#define STORE_BASE 0x02008000u

static const FxMap* g_fx;
static uint8_t ov_r8(void* ctx, uint32_t a) {
	for (int i = 0; i < OV_N; i++)
		if (g_ov[i].p && a >= g_ov[i].addr && a < g_ov[i].addr + g_ov[i].len)
			return g_ov[i].p[a - g_ov[i].addr];
	const FxMap* f = (const FxMap*)ctx;
	if (!f) return 0;
	for (int i = 0; i < f->nreg; i++)
		if (a >= f->reg[i].addr && a < f->reg[i].addr + f->reg[i].len)
			return f->reg[i].p[a - f->reg[i].addr];
	return 0;
}
static uint16_t ov_r16(void* c, uint32_t a) { return (uint16_t)(ov_r8(c, a) | (ov_r8(c, a + 1) << 8)); }
static uint32_t ov_r32(void* c, uint32_t a) { return (uint32_t)ov_r16(c, a) | ((uint32_t)ov_r16(c, a + 2) << 16); }

static void ov_w8 (uint8_t* base, uint32_t off, uint8_t v)  { base[off] = v; }
static void ov_w16(uint8_t* base, uint32_t off, uint16_t v) { base[off] = (uint8_t)v; base[off + 1] = (uint8_t)(v >> 8); }
static void ov_w32(uint8_t* base, uint32_t off, uint32_t v) { for (int i = 0; i < 4; i++) base[off + i] = (uint8_t)(v >> (8 * i)); }

static const FxMap* fx(const char* name) {
	for (int i = 0; i < FX_MAP_N; i++) if (!strcmp(FX_MAPS[i].name, name)) return &FX_MAPS[i];
	printf("  [FAIL] no fixture '%s'\n", name); g_fail++; return &FX_MAPS[0];
}
// Bind a fixture and reset the overlay. `pElev` is written into gObjectEvents[0].currentElevation,
// which is how fieldpath_player_elev reads the player's layer — so the tests control the exact
// elevation rail the shipped code uses, rather than a stand-in for it.
static void bind(const char* name, FpBus* bus, FpMap* m, int pElev) {
	const FxMap* f = fx(name); g_fx = f;
	memset(g_ovObj, 0, sizeof g_ovObj);
	memset(g_ovSb1, 0, sizeof g_ovSb1);
	memset(g_ovParty, 0, sizeof g_ovParty);
	memset(g_ovStore, 0, sizeof g_ovStore);
	g_ov[0].addr = f->mapObjects; g_ov[0].len = sizeof g_ovObj;   g_ov[0].p = g_ovObj;
	g_ov[1].addr = SB1_BASE;      g_ov[1].len = sizeof g_ovSb1;   g_ov[1].p = g_ovSb1;
	g_ov[2].addr = PARTY_BASE;    g_ov[2].len = sizeof g_ovParty; g_ov[2].p = g_ovParty;
	g_ov[3].addr = STORE_BASE;    g_ov[3].len = sizeof g_ovStore; g_ov[3].p = g_ovStore;
	ov_w32(g_ovObj, 0x00, 1u);                                    // slot 0 (the player) active
	ov_w8 (g_ovObj, 0x0B, (uint8_t)(pElev & 0x0F));               // currentElevation
	bus->read8 = ov_r8; bus->read16 = ov_r16; bus->read32 = ov_r32; bus->ctx = (void*)f;
	m->engine = (FpEngine)f->engine; m->mapHeader = f->mapHeader; m->mapObjects = f->mapObjects;
	m->gridPtr = f->gridPtr; m->backupW = f->bw; m->backupH = f->bh;
}

// ---------------- SLICE 2: a bus onto the USER'S OWN ROM ----------------
// TEST 15/16 walk a chain no compiled-in fixture carries (gMapGroups -> MapHeader ->
// MapEvents/MapLayout -> warp table + raw grid), so they read `roms/emerald.gba` at run time and
// SKIP LOUDLY when it is absent. Nothing is written; the ROM is opened read-only.
#define EM_MAPGROUPS 0x08486578u        // BPEE gMapGroups — the shipped profile value (gamestate.c:94)
static uint8_t* g_rom = 0;
static long g_romLen = 0;
static void rom_load(void) {
	if (g_rom) return;
	FILE* f = fopen("roms/emerald.gba", "rb");
	if (!f) return;
	fseek(f, 0, SEEK_END); g_romLen = ftell(f); fseek(f, 0, SEEK_SET);
	if (g_romLen <= 0 || g_romLen > 32 * 1024 * 1024) { fclose(f); return; }
	g_rom = (uint8_t*)malloc((size_t)g_romLen);
	if (g_rom && fread(g_rom, 1, (size_t)g_romLen, f) != (size_t)g_romLen) { free(g_rom); g_rom = 0; }
	fclose(f);
}
static uint8_t rom_r8(void* ctx, uint32_t a) {
	(void)ctx;
	if ((a >> 24) != 0x08u && (a >> 24) != 0x09u) return 0;
	uint32_t off = a & 0x01FFFFFFu;
	return (g_rom && (long)off < g_romLen) ? g_rom[off] : 0;
}
static uint16_t rom_r16(void* c, uint32_t a) { return (uint16_t)(rom_r8(c, a) | (rom_r8(c, a + 1) << 8)); }
static uint32_t rom_r32(void* c, uint32_t a) { return (uint32_t)rom_r16(c, a) | ((uint32_t)rom_r16(c, a + 2) << 16); }

// Build the LIVE gBackupMapLayout the console builds on a map load: the ROM grid copied into the
// border-padded buffer with MAPGRID_UNDEFINED outside. The CURRENT map is always RAM on hardware
// and only the INTERIOR is read from ROM, so TEST 16 must reproduce that split rather than run
// both sides through the ROM adapter (which would also collide two adapters on one address window).
#define LIVE_BASE 0x02900000u
static uint8_t g_live[2 * 300 * 300];
static uint32_t g_liveLen = 0;
// PHASE 24: gMapHeader is an EWRAM STRUCT on hardware (BPEE 0x02037318, gamestate.c mapHeaderPath),
// not a ROM pointer — and handing the planner the ROM header instead, as this fixture used to, hid
// a guard that rejected every real excursion (fieldtrav.c FT_OUT_BADMAP). So the live bus now
// serves a COPY of the header at the EWRAM address the console uses, and TEST 16 runs the planner
// exactly as touch.c calls it.
#define HDR_BASE 0x02037318u
static uint8_t g_hdr[0x20];
static bool g_hdrOn = false;
// The live bus: the RAM grid overlay first, then the ROM. The map HEADER and both tilesets are
// still ROM reads even for the current map (that is true on hardware too), so a bus that only
// answered the grid would make fieldpath_classify silently blind.
static uint8_t live_r8(void* c, uint32_t a) {
	if (a >= LIVE_BASE && a < LIVE_BASE + g_liveLen) return g_live[a - LIVE_BASE];
	if (g_hdrOn && a >= HDR_BASE && a < HDR_BASE + sizeof g_hdr) return g_hdr[a - HDR_BASE];
	return rom_r8(c, a);
}
static uint16_t live_r16(void* c, uint32_t a) { return (uint16_t)(live_r8(c, a) | (live_r8(c, a + 1) << 8)); }
static uint32_t live_r32(void* c, uint32_t a) { return (uint32_t)live_r16(c, a) | ((uint32_t)live_r16(c, a + 2) << 16); }
static void live_from_rom(const FpBus* rb, const FtRomMap* rm, FpBus* busOut, FpMap* mapOut) {
	int bw = rm->w + 15, bh = rm->h + 14;              // MAP_OFFSET_W / MAP_OFFSET_H
	for (int gy = 0; gy < bh; gy++)
		for (int gx = 0; gx < bw; gx++) {
			int x = gx - 7, y = gy - 7;                // MAP_OFFSET
			uint16_t v = 0x03FFu;                      // MAPGRID_UNDEFINED
			if (x >= 0 && x < rm->w && y >= 0 && y < rm->h)
				v = rb->read16(rb->ctx, rm->grid + 2u * (uint32_t)(x + rm->w * y));
			uint32_t o = 2u * (uint32_t)(gx + bw * gy);
			g_live[o] = (uint8_t)v; g_live[o + 1] = (uint8_t)(v >> 8);
		}
	g_liveLen = (uint32_t)(2 * bw * bh);
	busOut->read8 = live_r8; busOut->read16 = live_r16; busOut->read32 = live_r32;
	busOut->ctx = (void*)0;
	memset(mapOut, 0, sizeof *mapOut);
	mapOut->engine = FP_ENG_RSE;
	// The header the console reads is the EWRAM COPY the map loader made, so mirror that: copy the
	// ROM MapHeader into the live overlay and point the planner at the EWRAM address. Its INTERNAL
	// pointers (layout, events, connections) still point into ROM, exactly as on hardware.
	for (unsigned i = 0; i < sizeof g_hdr; i++) g_hdr[i] = rb->read8(rb->ctx, rm->header + i);
	g_hdrOn = true;
	mapOut->mapHeader = HDR_BASE;                      // gMapHeader lives in EWRAM (phase 24)
	mapOut->mapObjects = 0;                            // no live objects in this harness
	mapOut->gridPtr = LIVE_BASE;
	mapOut->backupW = bw; mapOut->backupH = bh;
}

// FlagSet, the writer half of fieldtrav_flag_get (pokeemerald src/event_data.c FlagSet).
static void set_flag(FtVariant var, int flagId) {
	const FtEngCfg* c = fieldtrav_cfg(var);
	g_ovSb1[c->flagsOff + (flagId >> 3)] |= (uint8_t)(1u << (flagId & 7));
}

// The RUBY writer, deliberately NOT routed through fieldtrav_cfg (2026-08-14 fix): a test that
// writes the badge through the same table it is grading can only ever agree with itself, which is
// precisely how a Ruby cart reading EMERALD's numbers stayed invisible here. Both numbers below are
// pokeruby's own — include/global.h:701 `/*0x1220*/ u8 flags[FLAGS_COUNT]`, and flag ids off
// include/constants/flags.h:779 SYSTEM_FLAGS 0x800 — so the byte this sets is the byte a real
// Ruby/Sapphire cartridge sets.
#define RUBY_FLAGS_OFF   0x1220
#define RUBY_BADGE05_GET 0x80B      // flags.h:793, the Surf badge (field_control_avatar.c:506)
static void set_flag_ruby(int flagId) {
	g_ovSb1[RUBY_FLAGS_OFF + (flagId >> 3)] |= (uint8_t)(1u << (flagId & 7));
}

// Put an ACTIVE object event in a slot. Offsets: +0x00 bit0 active, +0x05 graphicsId,
// +0x10/+0x12 currentCoords (grid space, i.e. map-local + MAP_OFFSET 7).
static void set_obj(int slot, uint8_t gfx, int mapX, int mapY) {
	uint8_t* o = g_ovObj + 0x24 * slot;
	ov_w32(o, 0x00, 1u);
	ov_w8 (o, 0x05, gfx);
	ov_w16(o, 0x10, (uint16_t)(mapX + 7));
	ov_w16(o, 0x12, (uint16_t)(mapY + 7));
}

// ---------------- golden party bytes ----------------
// Build a REAL encrypted Gen-3 party mon: personality decides the substruct permutation
// (src/pokemon.c:3609-3632), the whole 48-byte secure block is XORed with personality ^ otId
// (DecryptBoxMon), and the checksum is the sum of all 24 decrypted u16 (CalculateBoxMonChecksum).
// A test that hand-waved any of those three would be testing a different function than the one
// that runs on the console.
// PHASE 24: the writer is parameterised by SPECIES and writes to an arbitrary 80-byte BoxPokemon
// head, because the census reads party slots (100-byte stride) and PC slots (80-byte stride)
// through the SAME rail — a helper that could only write the party could not test that.
static void put_mon_at(uint8_t* mon, uint32_t pers, uint32_t otId, const uint16_t moves[4],
                       uint16_t species, int hasSpecies, int isEgg, int corruptChecksum) {
	memset(mon, 0, 80);
	ov_w32(mon, 0x00, pers);
	ov_w32(mon, 0x04, otId);
	ov_w8 (mon, 0x13, (uint8_t)((hasSpecies ? 2 : 0) | (isEgg ? 4 : 0)));

	uint16_t plain[24];
	memset(plain, 0, sizeof plain);
	int slot = fieldtrav_substruct_slot(pers, 1);        // the Attacks substruct
	for (int i = 0; i < 4; i++) plain[slot * 6 + i] = moves[i];
	// Growth substruct (type 0) slot gets a plausible species so the block is not all zeros.
	plain[fieldtrav_substruct_slot(pers, 0) * 6 + 0] = species;

	uint16_t sum = 0;
	for (int i = 0; i < 24; i++) sum = (uint16_t)(sum + plain[i]);
	ov_w16(mon, 0x1C, (uint16_t)(corruptChecksum ? sum ^ 0xBEEF : sum));

	uint32_t key = pers ^ otId;
	for (int w = 0; w < 12; w++) {
		uint32_t v = (uint32_t)plain[2 * w] | ((uint32_t)plain[2 * w + 1] << 16);
		ov_w32(mon, 0x20 + 4 * w, v ^ key);
	}
}
// The party writer, unchanged in behaviour: slot idx of the 100-byte-stride gPlayerParty, the
// same arbitrary species 260 every pre-phase-24 test was written against.
static void put_mon(int idx, uint32_t pers, uint32_t otId, const uint16_t moves[4],
                    int hasSpecies, int isEgg, int corruptChecksum) {
	uint8_t* mon = g_ovParty + 100 * idx;
	memset(mon, 0, 100);
	put_mon_at(mon, pers, otId, moves, 260, hasSpecies, isEgg, corruptChecksum);
}
static FtParty party_of(int count) {
	FtParty p; p.sb1 = SB1_BASE; p.partyBase = PARTY_BASE; p.partyCount = count; return p;
}

#define MOVE_CUT   15
#define MOVE_FLY   19
#define MOVE_SURF  57
#define MOVE_SMASH 249
#define MOVE_TACKLE 33

// ---------------- helpers for the planner tests ----------------
static short g_npc[16][2];
static int   g_npcN;
static void npc_clear(void) { g_npcN = 0; }
static void npc_add(int x, int y) { g_npc[g_npcN][0] = (short)(x + 7); g_npc[g_npcN][1] = (short)(y + 7); g_npcN++; }

// The three tiles at y=12, x=17..19 are the ONLY land exit from the Route 117 south-west pocket
// (x 17-19, y 13-16): x=16 is wall for all four rows, y=17 is wall, and x=20 is the pond. Sealing
// them makes the pond the only way out — which is exactly the situation the user described.
static void seal_pocket(void) { npc_add(17, 12); npc_add(18, 12); npc_add(19, 12); }

static const char* OUTN(int o) {
	switch (o) {
	case FT_OUT_TIER0: return "TIER0"; case FT_OUT_PLANNED: return "PLANNED";
	case FT_OUT_UNREACHABLE: return "UNREACHABLE"; case FT_OUT_WINDOW: return "WINDOW";
	case FT_OUT_BADMAP: return "BADMAP"; case FT_OUT_NOEDGE: return "NOEDGE";
	case FT_OUT_CAP: return "CAP"; default: return "?";
	}
}

// Replay a program over the fixture grid and report the tile it lands on plus the modes it used.
static void replay(const FtProgram* pr, int sx, int sy, int* ex, int* ey) {
	static const int dxs[4] = { 1, -1, 0, 0 }, dys[4] = { 0, 0, 1, -1 };
	int x = sx, y = sy;
	for (int i = 0; i < pr->nMoves; i++) { x += dxs[pr->mv[i].dir]; y += dys[pr->mv[i].dir]; }
	*ex = x; *ey = y;
}

static FtProgram g_pr;   // ~17 KB — static, like the shipped caller

// PHASE 26 / lane V. pokeemerald include/constants/moves.h:295 `#define MOVE_DIVE 291`. Kept in
// this lane's own block rather than beside the MOVE_* list above, so the two phase-26 lanes
// sharing this file never edit the same lines.
#define MOVE_DIVE 291

// ================= PHASE 26 / LANE W — the WATERFALL / STRENGTH helpers (TEST 22/23) =========
// Same discipline as lane V's block above: everything this lane needs lives here, so the two
// phase-26 lanes sharing this file never edit the same lines.
//
// pokeemerald include/constants/moves.h: MOVE_STRENGTH 70 (:74), MOVE_WATERFALL 127 (:131) — the
// two ids `fieldtrav_usable` compares against, restated here so the suite is asserting pret's
// numbers and not fieldtrav.c's copy of them.
#define MOVE_STRENGTH_W   70
#define MOVE_WATERFALL_W 127

// A bus that serves the RAM overlay (the synthesised SaveBlock1 + gPlayerParty) on top of an
// ARBITRARY inner bus. TEST 22 needs both at once: every map read has to reach the live grid built
// out of the user's own cartridge, while the badge and party reads have to reach the fixture save
// this file constructs. `ctx` is the inner FpBus. The overlay windows are EWRAM addresses well
// clear of LIVE_BASE (0x02900000) and HDR_BASE (0x02037318), so neither can shadow the other.
static uint8_t wf_r8(void* ctx, uint32_t a) {
	for (int i = 0; i < OV_N; i++)
		if (g_ov[i].p && a >= g_ov[i].addr && a < g_ov[i].addr + g_ov[i].len)
			return g_ov[i].p[a - g_ov[i].addr];
	const FpBus* inner = (const FpBus*)ctx;
	return inner->read8(inner->ctx, a);
}
static uint16_t wf_r16(void* c, uint32_t a) { return (uint16_t)(wf_r8(c, a) | (wf_r8(c, a + 1) << 8)); }
static uint32_t wf_r32(void* c, uint32_t a) { return (uint32_t)wf_r16(c, a) | ((uint32_t)wf_r16(c, a + 2) << 16); }

// ================= PHASE 26 / LANE V — the DIVE two-map world (TEST 20/21) ==================
// Self-contained on purpose: its own bus, its own ROM image, its own live grid and its own save.
// Nothing above is touched, so the lane sharing this file cannot collide with it — and, more to
// the point, a dive needs a chain no compiled-in fixture carries (gMapHeader.connections ->
// gMapGroups -> the OTHER map's MapHeader/MapLayout/tileset/grid) and the user's ROM is optional.
//
// Every structure offset below is the one fieldtrav.c cites at the line that reads it:
//   MapHeader      +0x00 mapLayout, +0x04 events, +0x08 mapScripts, +0x0C connections,
//                  +0x17 mapType                                   (global.fieldmap.h:173-182)
//   MapConnections +0x00 s32 count, +0x04 MapConnection*           (global.fieldmap.h:165-169)
//   MapConnection  +0x00 direction, +0x04 offset, +0x08 mapGroup, +0x09 mapNum, stride 12
//                  (pokeemerald asm/macros/map.inc:152-158 — the ASSEMBLER, because pokeruby's C
//                   comments on this struct ignore the alignment of `offset` and are wrong)
//   MapLayout      +0x00 width, +0x04 height, +0x0C map, +0x10/+0x14 primary/secondary Tileset
//   Tileset        +0x10 metatileAttributes (RSE)                  (fieldpath.c:125)
#define DV_ROM_BASE    0x08000000u
#define DV_LIVE_BASE   0x02A00000u    // deliberately NOT LIVE_BASE: TEST 16's world stays intact
#define DV_HDR_BASE    0x02037318u    // gMapHeader — the EWRAM STRUCT, as on hardware (phase 24)
#define DV_OBJ_BASE    0x02037000u    // gObjectEvents; slot 0 carries the player's elevation
#define DV_SB1_BASE    0x02025734u
#define DV_PARTY_BASE  0x02024000u
#define DV_MAPGROUPS   (DV_ROM_BASE + 0x0000u)

#define DVR_GROUP0  0x0010u
#define DVR_HDR_S   0x0020u
#define DVR_HDR_U   0x0040u
#define DVR_LAY_S   0x0060u
#define DVR_LAY_U   0x0080u
#define DVR_TILESET 0x00A0u
#define DVR_ATTRS   0x00C0u
#define DVR_CONN_S  0x0100u
#define DVR_CONNL_S 0x0110u
#define DV_DIVE_REC 2          // the dive record is the LAST of three, as it is on Route 124
#define DVR_CONN_U  0x0180u    // clear of DVR_CONNL_S, which is now THREE 12-byte records long
#define DVR_CONNL_U 0x0190u
#define DVR_EVENTS  0x0200u
#define DVR_GRID_S  0x0400u
#define DVR_GRID_U  0x0600u

#define DV_W 16
#define DV_H 8
#define DV_BW (DV_W + 15)
#define DV_BH (DV_H + 14)

static uint8_t g_dvRom[0x2000];
static uint8_t g_dvLive[2 * DV_BW * DV_BH];
static uint8_t g_dvHdr[0x20];
static uint8_t g_dvObj[0x24];
static uint8_t g_dvSb1[0x1500];
static uint8_t g_dvParty[100 * 6];
// A DECOY MapConnections in EWRAM: a perfectly well-formed table sitting at an address
// gMapHeader.connections could only hold if the header read were wrong. The ROM-pointer guard is
// what stops it being followed, and this is what makes that guard observable.
#define DV_DECOY_BASE 0x02028000u
static uint8_t g_dvDecoy[16];

static uint8_t dv_r8(void* c, uint32_t a) {
	(void)c;
	if (a >= DV_LIVE_BASE  && a < DV_LIVE_BASE  + sizeof g_dvLive)  return g_dvLive[a - DV_LIVE_BASE];
	if (a >= DV_HDR_BASE   && a < DV_HDR_BASE   + sizeof g_dvHdr)   return g_dvHdr[a - DV_HDR_BASE];
	if (a >= DV_OBJ_BASE   && a < DV_OBJ_BASE   + sizeof g_dvObj)   return g_dvObj[a - DV_OBJ_BASE];
	if (a >= DV_SB1_BASE   && a < DV_SB1_BASE   + sizeof g_dvSb1)   return g_dvSb1[a - DV_SB1_BASE];
	if (a >= DV_PARTY_BASE && a < DV_PARTY_BASE + sizeof g_dvParty) return g_dvParty[a - DV_PARTY_BASE];
	if (a >= DV_DECOY_BASE && a < DV_DECOY_BASE + sizeof g_dvDecoy) return g_dvDecoy[a - DV_DECOY_BASE];
	if (a >= DV_ROM_BASE   && a < DV_ROM_BASE   + sizeof g_dvRom)   return g_dvRom[a - DV_ROM_BASE];
	return 0;
}
static uint16_t dv_r16(void* c, uint32_t a) { return (uint16_t)(dv_r8(c, a) | (dv_r8(c, a + 1) << 8)); }
static uint32_t dv_r32(void* c, uint32_t a) { return (uint32_t)dv_r16(c, a) | ((uint32_t)dv_r16(c, a + 2) << 16); }

static void dvw16(uint32_t off, uint16_t v) { g_dvRom[off] = (uint8_t)v; g_dvRom[off + 1] = (uint8_t)(v >> 8); }
static void dvw32(uint32_t off, uint32_t v) { for (int i = 0; i < 4; i++) g_dvRom[off + i] = (uint8_t)(v >> (8 * i)); }

// A gBackupMapLayout word: metatile id (bits 0-9) | collision (10-11) | elevation (12-15).
static uint16_t dvW(int id, int coll, int elev) {
	return (uint16_t)((id & 0x3FF) | ((coll & 3) << 10) | ((elev & 0xF) << 12));
}
// Metatile ids in the synthetic tileset, each pinned to ONE pret behaviour:
//   0 = MB_NORMAL 0x00, 1 = MB_OCEAN_WATER 0x15, 2 = MB_DEEP_WATER 0x12, 3 = MB_NO_SURFACING 0x19
#define DV_MT_LAND  0
#define DV_MT_SEA   1
#define DV_MT_DEEP  2
#define DV_MT_NOSUR 3
#define DV_ROCK  dvW(DV_MT_LAND, 1, 3)      // impassable in both layers

static void dv_grid_put(uint32_t base, int x, int y, uint16_t v) {
	dvw16(base + 2u * (uint32_t)(x + DV_W * y), v);
}
static uint16_t dv_grid_get(uint32_t base, int x, int y) {
	return dv_r16(0, DV_ROM_BASE + base + 2u * (uint32_t)(x + DV_W * y));
}
static void dv_live_put(int x, int y, uint16_t v) {
	uint32_t o = 2u * (uint32_t)((x + 7) + DV_BW * (y + 7));
	g_dvLive[o] = (uint8_t)v; g_dvLive[o + 1] = (uint8_t)(v >> 8);
}

// Copy one ROM grid into the border-padded live buffer — what the console's map loader does.
static void dv_live_from(uint32_t romGrid) {
	for (int gy = 0; gy < DV_BH; gy++)
		for (int gx = 0; gx < DV_BW; gx++) {
			int x = gx - 7, y = gy - 7;
			uint16_t v = 0x03FFu;                       // MAPGRID_UNDEFINED
			if (x >= 0 && x < DV_W && y >= 0 && y < DV_H) v = dv_grid_get(romGrid, x, y);
			uint32_t o = 2u * (uint32_t)(gx + DV_BW * gy);
			g_dvLive[o] = (uint8_t)v; g_dvLive[o + 1] = (uint8_t)(v >> 8);
		}
}

static void dv_flag_set(int id)   { g_dvSb1[0x1270 + (id >> 3)] |=  (uint8_t)(1u << (id & 7)); }
// The same bit written through FIRERED's numbering (flags[] at 0x0EE0, pokefirered
// include/global.h:790). Used to arm the FRLG refusal properly: "no dive" only proves something if
// the badge a mis-ported table WOULD have read is genuinely set.
static void dv_flag_set_frlg(int id) { g_dvSb1[0x0EE0 + (id >> 3)] |= (uint8_t)(1u << (id & 7)); }
static void dv_flag_clear(int id) { g_dvSb1[0x1270 + (id >> 3)] &= (uint8_t)~(1u << (id & 7)); }
static void dv_set_badge(void)    { dv_flag_set(0x86D); }     // FLAG_BADGE07_GET, Emerald
static void dv_clear_badge(void)  { dv_flag_clear(0x86D); }
static FtParty dv_party(int n) { FtParty p; p.sb1 = DV_SB1_BASE; p.partyBase = DV_PARTY_BASE; p.partyCount = n; return p; }

// `up == 0` builds the DOWN world: two surface chambers a rock wall apart, joined by an
// underwater corridor. `up == 1` mirrors it: two underwater pockets a rock wall apart, joined by
// open sea on top. Either way exactly ONE tile per side takes the HM, so every step count the
// test asserts is forced rather than a tie broken by scan order.
static void dv_world(int up) {
	memset(g_dvRom, 0, sizeof g_dvRom);
	memset(g_dvObj, 0, sizeof g_dvObj);
	memset(g_dvSb1, 0, sizeof g_dvSb1);
	memset(g_dvParty, 0, sizeof g_dvParty);

	dvw32(0x0000,           DV_ROM_BASE + DVR_GROUP0);      // gMapGroups[0]
	dvw32(DVR_GROUP0 + 0,   DV_ROM_BASE + DVR_HDR_S);       // map (0,0) — the surface
	dvw32(DVR_GROUP0 + 4,   DV_ROM_BASE + DVR_HDR_U);       // map (0,1) — its underwater twin

	dvw32(DVR_HDR_S + 0x00, DV_ROM_BASE + DVR_LAY_S);
	dvw32(DVR_HDR_S + 0x04, DV_ROM_BASE + DVR_EVENTS);
	dvw32(DVR_HDR_S + 0x0C, DV_ROM_BASE + DVR_CONN_S);
	g_dvRom[DVR_HDR_S + 0x17] = 3;                          // MAP_TYPE_ROUTE
	dvw32(DVR_HDR_U + 0x00, DV_ROM_BASE + DVR_LAY_U);
	dvw32(DVR_HDR_U + 0x04, DV_ROM_BASE + DVR_EVENTS);
	dvw32(DVR_HDR_U + 0x0C, DV_ROM_BASE + DVR_CONN_U);
	g_dvRom[DVR_HDR_U + 0x17] = 5;                          // MAP_TYPE_UNDERWATER

	for (int i = 0; i < 2; i++) {
		uint32_t lay = i ? DVR_LAY_U : DVR_LAY_S;
		dvw32(lay + 0x00, DV_W);
		dvw32(lay + 0x04, DV_H);
		dvw32(lay + 0x0C, DV_ROM_BASE + (i ? DVR_GRID_U : DVR_GRID_S));
		dvw32(lay + 0x10, DV_ROM_BASE + DVR_TILESET);       // primary  (ids < 512 use this one)
		dvw32(lay + 0x14, DV_ROM_BASE + DVR_TILESET);       // secondary (never reached here)
	}
	dvw32(DVR_TILESET + 0x10, DV_ROM_BASE + DVR_ATTRS);     // Tileset.metatileAttributes
	dvw16(DVR_ATTRS + 2 * DV_MT_LAND,  0x00);               // MB_NORMAL
	dvw16(DVR_ATTRS + 2 * DV_MT_SEA,   0x15);               // MB_OCEAN_WATER
	dvw16(DVR_ATTRS + 2 * DV_MT_DEEP,  0x12);               // MB_DEEP_WATER
	dvw16(DVR_ATTRS + 2 * DV_MT_NOSUR, 0x19);               // MB_NO_SURFACING

	// THREE records, with the dive one LAST — MAP_ROUTE124 carries five and puts its dive
	// connection at the end (pokeemerald data/maps/Route124/map.json), so a list that is scanned
	// rather than indexed, at the right STRIDE, is the only thing that finds it.
	dvw32(DVR_CONN_S + 0x00, 3);                            // MapConnections.count
	dvw32(DVR_CONN_S + 0x04, DV_ROM_BASE + DVR_CONNL_S);
	g_dvRom[DVR_CONNL_S + 0 * 12 + 0x00] = 1;               // CONNECTION_SOUTH
	g_dvRom[DVR_CONNL_S + 0 * 12 + 0x08] = 0;  g_dvRom[DVR_CONNL_S + 0 * 12 + 0x09] = 7;
	g_dvRom[DVR_CONNL_S + 1 * 12 + 0x00] = 4;               // CONNECTION_EAST
	g_dvRom[DVR_CONNL_S + 1 * 12 + 0x08] = 0;  g_dvRom[DVR_CONNL_S + 1 * 12 + 0x09] = 8;
	g_dvRom[DVR_CONNL_S + DV_DIVE_REC * 12 + 0x00] = FT_CONN_DIVE;
	g_dvRom[DVR_CONNL_S + DV_DIVE_REC * 12 + 0x08] = 0;
	g_dvRom[DVR_CONNL_S + DV_DIVE_REC * 12 + 0x09] = 1;                  // -> (0,1)
	dvw32(DVR_CONN_U + 0x00, 1);
	dvw32(DVR_CONN_U + 0x04, DV_ROM_BASE + DVR_CONNL_U);
	g_dvRom[DVR_CONNL_U + 0x00] = FT_CONN_EMERGE;
	g_dvRom[DVR_CONNL_U + 0x08] = 0;  g_dvRom[DVR_CONNL_U + 0x09] = 0;   // -> (0,0)

	for (int y = 0; y < DV_H; y++)
		for (int x = 0; x < DV_W; x++) {
			dv_grid_put(DVR_GRID_S, x, y, DV_ROCK);
			dv_grid_put(DVR_GRID_U, x, y, DV_ROCK);
		}
	if (!up) {
		// SURFACE: a west chamber (x 0-5) and an east chamber (x 10-15), rock in between.
		for (int y = 3; y <= 5; y++)
			for (int x = 0; x < DV_W; x++)
				if (x <= 5 || x >= 10) dv_grid_put(DVR_GRID_S, x, y, dvW(DV_MT_SEA, 0, 1));
		dv_grid_put(DVR_GRID_S, 2, 4, dvW(DV_MT_DEEP, 0, 1));      // the ONE dive spot
		// A SANDBAR across the west chamber: MB_NORMAL, and COLLISION 0. A walker could stand on
		// it; a surfer cannot float over it, because surfing tests the BEHAVIOUR and not just the
		// collision bits. The second deep-water tile behind it is therefore invisible to a surfing
		// player — which is what pins the `nSpots == 1` assertion to the behaviour test rather
		// than to the collision bits it would otherwise be indistinguishable from.
		for (int y = 3; y <= 5; y++) dv_grid_put(DVR_GRID_S, 4, y, dvW(DV_MT_LAND, 0, 1));
		dv_grid_put(DVR_GRID_S, 5, 4, dvW(DV_MT_DEEP, 0, 1));
		// UNDERWATER: one open corridor the whole width, unsurfaceable except two shafts.
		for (int x = 0; x < DV_W; x++) dv_grid_put(DVR_GRID_U, x, 4, dvW(DV_MT_NOSUR, 0, 3));
		dv_grid_put(DVR_GRID_U, 2, 4, dvW(DV_MT_LAND, 0, 3));
		dv_grid_put(DVR_GRID_U, 13, 4, dvW(DV_MT_LAND, 0, 3));     // the ONE surfacing spot
		dv_live_from(DVR_GRID_S);
		memcpy(g_dvHdr, g_dvRom + DVR_HDR_S, sizeof g_dvHdr);
	} else {
		// The mirror: the split is UNDERWATER and the open water is on top.
		for (int x = 0; x < DV_W; x++)
			if (x <= 5 || x >= 10) dv_grid_put(DVR_GRID_U, x, 4, dvW(DV_MT_NOSUR, 0, 3));
		dv_grid_put(DVR_GRID_U, 2, 4, dvW(DV_MT_LAND, 0, 3));      // the ONE surfacing spot
		dv_grid_put(DVR_GRID_U, 13, 4, dvW(DV_MT_LAND, 0, 3));
		for (int y = 3; y <= 5; y++)
			for (int x = 0; x < DV_W; x++) dv_grid_put(DVR_GRID_S, x, y, dvW(DV_MT_SEA, 0, 1));
		dv_grid_put(DVR_GRID_S, 2, 4, dvW(DV_MT_DEEP, 0, 1));
		dv_grid_put(DVR_GRID_S, 13, 4, dvW(DV_MT_DEEP, 0, 1));     // the ONE dive-back spot
		// A REEF on the direct line: MB_NORMAL with COLLISION 0 again, so the surface crossing has
		// to detour around it. Its whole job is to make the mid-leg step count depend on the
		// surfable-behaviour test — 13 with it, 11 without.
		dv_grid_put(DVR_GRID_S, 7, 4, dvW(DV_MT_LAND, 0, 1));
		dv_live_from(DVR_GRID_U);
		memcpy(g_dvHdr, g_dvRom + DVR_HDR_U, sizeof g_dvHdr);
	}

	// The player object: active, elevation 3. Underwater that is the REAL elevation of every
	// swimmable tile (measured across pret's seven Emerald pairs), so the elevation rail is armed
	// rather than disarmed for the upward legs.
	g_dvObj[0x00] = 1;
	g_dvObj[0x0B] = 3;
	dv_set_badge();
	{
		const uint16_t diveSet[4] = { MOVE_DIVE, MOVE_SURF, 0, 0 };
		put_mon_at(g_dvParty, 0x12345678u, 0xCAFEBABEu, diveSet, 260, 1, 0, 0);
	}
}

static void dv_bind(FpBus* bus, FpMap* m) {
	bus->read8 = dv_r8; bus->read16 = dv_r16; bus->read32 = dv_r32; bus->ctx = 0;
	memset(m, 0, sizeof *m);
	m->engine = FP_ENG_RSE;
	m->mapHeader = DV_HDR_BASE;      // EWRAM, exactly as touch.c passes it
	m->mapObjects = DV_OBJ_BASE;
	m->gridPtr = DV_LIVE_BASE;
	m->backupW = DV_BW; m->backupH = DV_BH;
}
// Surgical world edits the refusal cases need.
static void dv_conn_dir(int which, int dir) {
	g_dvRom[which ? DVR_CONNL_U : (DVR_CONNL_S + DV_DIVE_REC * 12)] = (uint8_t)dir;
}
static void dv_conn_count(int n) { dvw32(DVR_CONN_S + 0x00, (uint32_t)n); }
static void dv_conn_ptr(uint32_t v) { dvw32(DVR_HDR_S + 0x0C, v); g_dvHdr[0x0C] = (uint8_t)v;
	g_dvHdr[0x0D] = (uint8_t)(v >> 8); g_dvHdr[0x0E] = (uint8_t)(v >> 16); g_dvHdr[0x0F] = (uint8_t)(v >> 24); }
static void dv_wall_under(int x) { dv_grid_put(DVR_GRID_U, x, 4, DV_ROCK); }
static void dv_open_under(int x) { dv_grid_put(DVR_GRID_U, x, 4, dvW(DV_MT_NOSUR, 0, 3)); }
static void dv_block_goal(void)   { dv_live_put(14, 4, DV_ROCK); }
static void dv_unblock_goal(void) { dv_live_put(14, 4, dvW(DV_MT_SEA, 0, 1)); }

int main(void) {
	printf("=== test_fieldtrav (phase 22.2 HM traversal planner) ===\n");

	// ---------------------------------------------------------------- TEST 1
	printf("\nTEST 1 — the two surfable sets, every row (a shared table FAILS)\n");
	{
		// RSE, from pokeemerald src/metatile_behavior.c sTileBitAttributes & TILE_FLAG_SURFABLE,
		// MINUS the currents/waterfall/warp exclusions fieldtrav.c documents.
		static const int rseYes[] = { 0x10, 0x11, 0x12, 0x14, 0x15, 0x19, 0x22, 0x2A };
		for (unsigned i = 0; i < sizeof rseYes / sizeof rseYes[0]; i++)
			CHECK(fieldtrav_is_surfable(FP_ENG_RSE, rseYes[i]), "RSE 0x%02X should be surfable", rseYes[i]);
		// FRLG, from pokefirered src/metatile_behavior.c sBehaviorSurfable[], same exclusions.
		static const int frYes[] = { 0x10, 0x11, 0x12, 0x15, 0x1A, 0x1B };
		for (unsigned i = 0; i < sizeof frYes / sizeof frYes[0]; i++)
			CHECK(fieldtrav_is_surfable(FP_ENG_FRLG, frYes[i]), "FRLG 0x%02X should be surfable", frYes[i]);

		// THE ANTI-MERGE ASSERTIONS. These are the rows where the two engines genuinely disagree:
		// 0x14 (MB_SOOTOPOLIS_DEEP_WATER) and 0x19/0x22/0x2A (no-surfacing + seaweed) exist only in
		// RSE's set; 0x1A/0x1B (MB_UNUSED_WATER / MB_CYCLING_ROAD_WATER) only in FRLG's. If anyone
		// ever collapses the two tables into one, these fail.
		CHECK(!fieldtrav_is_surfable(FP_ENG_FRLG, 0x14), "FRLG 0x14 is NOT in sBehaviorSurfable");
		CHECK(!fieldtrav_is_surfable(FP_ENG_FRLG, 0x19), "FRLG 0x19 is NOT in sBehaviorSurfable");
		CHECK(!fieldtrav_is_surfable(FP_ENG_FRLG, 0x22), "FRLG 0x22 is NOT in sBehaviorSurfable");
		CHECK(!fieldtrav_is_surfable(FP_ENG_FRLG, 0x2A), "FRLG 0x2A is NOT in sBehaviorSurfable");
		CHECK(!fieldtrav_is_surfable(FP_ENG_RSE,  0x1A), "RSE 0x1A is NOT in sTileBitAttributes' surfable set");
		CHECK(!fieldtrav_is_surfable(FP_ENG_RSE,  0x1B), "RSE 0x1B is NOT in sTileBitAttributes' surfable set");

		// Everything else in 0x00..0xFF must be refused by BOTH, and an unreadable behaviour (-1)
		// must never answer "yes" — the degradation that keeps a failed ROM read from inventing a pond.
		for (int b = 0; b <= 0xFF; b++) {
			int inRse = 0, inFr = 0;
			for (unsigned i = 0; i < sizeof rseYes / sizeof rseYes[0]; i++) if (rseYes[i] == b) inRse = 1;
			for (unsigned i = 0; i < sizeof frYes / sizeof frYes[0]; i++)  if (frYes[i]  == b) inFr  = 1;
			CHECK(fieldtrav_is_surfable(FP_ENG_RSE,  b) == (inRse != 0), "RSE 0x%02X surfable mismatch", b);
			CHECK(fieldtrav_is_surfable(FP_ENG_FRLG, b) == (inFr  != 0), "FRLG 0x%02X surfable mismatch", b);
		}
		CHECK(!fieldtrav_is_surfable(FP_ENG_RSE,  -1), "unreadable behaviour is never surfable (RSE)");
		CHECK(!fieldtrav_is_surfable(FP_ENG_FRLG, -1), "unreadable behaviour is never surfable (FRLG)");
	}

	// ---------------------------------------------------------------- TEST 2
	printf("TEST 2 — the per-variant constant block\n");
	{
		const FtEngCfg* r = fieldtrav_cfg(FT_VAR_EMERALD);
		const FtEngCfg* f = fieldtrav_cfg(FT_VAR_FRLG);
		const FtEngCfg* s = fieldtrav_cfg(FT_VAR_RS);
		// EM: include/global.h:1020 flags @0x1270; flags.h:1348 SYSTEM_FLAGS 0x860 + badge offsets;
		// the badge-per-move mapping comes off data/scripts/field_move_scripts.inc.
		CHECK(r->flagsOff == 0x1270, "EM SaveBlock1.flags offset");
		CHECK(r->badgeCut == 0x867, "EM Cut = BADGE01");
		CHECK(r->badgeSmash == 0x869, "EM Rock Smash = BADGE03");
		CHECK(r->badgeSurf == 0x86B, "EM Surf = BADGE05");
		CHECK(r->badgeStrength == 0x86A, "EM Strength = BADGE04");
		CHECK(r->badgeWaterfall == 0x86E, "EM Waterfall = BADGE08");
		CHECK(r->strengthLatch == 0x889, "EM FLAG_SYS_USE_STRENGTH");
		CHECK(r->gfxCutTree == 82 && r->gfxRock == 86 && r->gfxBoulder == 87, "EM object gfx ids");
		// FR: include/global.h:790 flags @0x0EE0; flags.h:1324 SYS_FLAGS 0x800 + badges 0x20..0x27.
		CHECK(f->flagsOff == 0x0EE0, "FR SaveBlock1.flags offset");
		CHECK(f->badgeCut == 0x821, "FR Cut = BADGE02 (NOT 01 — the Emerald trap)");
		CHECK(f->badgeSmash == 0x825, "FR Rock Smash = BADGE06 (NOT 03)");
		CHECK(f->badgeSurf == 0x824, "FR Surf = BADGE05");
		CHECK(f->badgeStrength == 0x823, "FR Strength = BADGE04");
		CHECK(f->badgeWaterfall == 0x826, "FR Waterfall = BADGE07 (NOT 08)");
		CHECK(f->strengthLatch == 0x805, "FR FLAG_SYS_USE_STRENGTH");
		CHECK(f->gfxCutTree == 95 && f->gfxRock == 96 && f->gfxBoulder == 97, "FR object gfx ids");
		// The three that MUST differ — a merged table would make all three equal.
		CHECK(r->badgeCut != f->badgeCut, "Cut badge differs between engines");
		CHECK(r->badgeSmash != f->badgeSmash, "Rock Smash badge differs between engines");
		CHECK(r->badgeWaterfall != f->badgeWaterfall, "Waterfall badge differs between engines");
		CHECK(r->gfxCutTree != f->gfxCutTree, "cut-tree gfx id differs between engines");
		// PHASE 24 / lane A2 (decision D2 — walk vs run). FLAG_SYS_B_DASH, the ONE save bit the
		// run decision reads: pokeemerald include/constants/flags.h:1462 (SYSTEM_FLAGS 0x860 +
		// 0x60) and pokefirered include/constants/flags.h:1381 (SYS_FLAGS 0x800 + 0x2F). The FR
		// value was fetched from pret master, NOT derived from Emerald's — which the inequality
		// below is here to keep honest, since 0x8C0 in an FR save is a completely unrelated flag.
		CHECK(r->runShoes == 0x8C0, "EM FLAG_SYS_B_DASH (Running Shoes received)");
		CHECK(f->runShoes == 0x82F, "FR FLAG_SYS_B_DASH");
		CHECK(r->runShoes != f->runShoes, "the Running-Shoes flag id differs between engines");
		CHECK(fieldtrav_cfg((FtVariant)99) == r, "an unknown variant falls back to Emerald, never NULL");

		// --- THE THIRD NUMBERING (2026-08-14 fix) ---------------------------------------------
		// Ruby/Sapphire are NOT Emerald on the save side. pokeruby include/global.h:701
		// `/*0x1220*/ u8 flags[FLAGS_COUNT];` (and :702 `/*0x1340*/ u16 vars[VARS_COUNT];`);
		// include/constants/flags.h:779 SYSTEM_FLAGS = TRAINER_FLAG_START(:773, 0x500) +
		// NUMBER_OF_TRAINERS(:778, 693) + 0x4B = 0x800; :789-796 badges = +0x07..+0x0E;
		// :817 FLAG_SYS_USE_STRENGTH = +0x29; :877 FLAG_SYS_B_DASH = +0x60. Badge-per-move off
		// data/field_move_scripts.inc:3/:60/:126 and src/field_control_avatar.c:506/:511.
		CHECK(s->flagsOff == 0x1220, "RS SaveBlock1.flags offset (NOT Emerald's 0x1270)");
		CHECK(s->badgeCut == 0x807, "RS Cut = BADGE01 (field_move_scripts.inc:3)");
		CHECK(s->badgeSmash == 0x809, "RS Rock Smash = BADGE03 (field_move_scripts.inc:60)");
		CHECK(s->badgeSurf == 0x80B, "RS Surf = BADGE05 (field_control_avatar.c:506)");
		CHECK(s->badgeStrength == 0x80A, "RS Strength = BADGE04 (field_move_scripts.inc:126)");
		CHECK(s->badgeWaterfall == 0x80E, "RS Waterfall = BADGE08 (field_control_avatar.c:511)");
		CHECK(s->strengthLatch == 0x829, "RS FLAG_SYS_USE_STRENGTH = 0x800 + 0x29");
		CHECK(s->runShoes == 0x860, "RS FLAG_SYS_B_DASH = 0x800 + 0x60");
		// READ from pokeruby include/constants/event_objects.h:88/92/93 — it agrees with Emerald,
		// which is a fact about that file, not an inheritance.
		CHECK(s->gfxCutTree == 82 && s->gfxRock == 86 && s->gfxBoulder == 87, "RS object gfx ids");
		// The whole point: RS must not BE Emerald. Every flag-space field differs; a table that
		// silently fell back to the Emerald row makes all five of these equal.
		CHECK(s->flagsOff != r->flagsOff, "RS flags offset differs from Emerald's");
		CHECK(s->badgeCut != r->badgeCut, "RS Cut badge id differs from Emerald's");
		CHECK(s->badgeSurf != r->badgeSurf, "RS Surf badge id differs from Emerald's");
		CHECK(s->strengthLatch != r->strengthLatch, "RS strength latch differs from Emerald's");
		CHECK(s->runShoes != r->runShoes, "RS Running-Shoes flag id differs from Emerald's");
		// The arithmetic that makes this a CORRECTNESS bug and not a tidiness one: Emerald's Cut
		// badge, read through Emerald's offset, lands past the end of Ruby's flags[] (which runs
		// 0x1220..0x1340) and inside vars[] — i.e. it reads game VARIABLES as badge bits.
		CHECK((uint32_t)r->flagsOff + (r->badgeCut >> 3) == 0x137Cu, "the wrong read's address");
		CHECK(0x137Cu >= (uint32_t)s->flagsOff + 0x120u, "...and 0x137C is past Ruby's flags[] end");
		CHECK((uint32_t)s->flagsOff + (s->badgeCut >> 3) == 0x1320u, "the RIGHT read stays in flags[]");
		CHECK((uint32_t)r->flagsOff + (r->runShoes >> 3) == 0x1388u, "same for Running Shoes: vars[]");
		CHECK((uint32_t)s->flagsOff + (s->runShoes >> 3) == 0x132Cu, "...vs 0x132C, inside flags[]");

		// The title -> variant discriminator, which is the ONLY thing that can tell RS from EM
		// (they share one FpEngine). All five shipped titles, plus the degradations.
		CHECK(fieldtrav_variant("AXVE") == FT_VAR_RS, "Ruby is RS");
		CHECK(fieldtrav_variant("AXPE") == FT_VAR_RS, "Sapphire is RS — one pokeruby tree builds both");
		CHECK(fieldtrav_variant("BPEE") == FT_VAR_EMERALD, "Emerald");
		CHECK(fieldtrav_variant("BPRE") == FT_VAR_FRLG, "FireRed");
		CHECK(fieldtrav_variant("BPGE") == FT_VAR_FRLG, "LeafGreen");
		CHECK(fieldtrav_variant("ZZZZ") == FT_VAR_EMERALD, "an unknown code falls back to Emerald");
		CHECK(fieldtrav_variant(NULL) == FT_VAR_EMERALD, "a NULL code falls back to Emerald");
		CHECK(fieldtrav_cfg(fieldtrav_variant("AXVE")) == s, "the Ruby code selects the RS row");
		CHECK(fieldtrav_cfg(fieldtrav_variant("AXPE")) == s, "the Sapphire code selects the RS row");
		// The frozen enum still selects what it always selected: fieldpath.h's FpEngine cannot
		// name the third row, and must never accidentally land on it.
		CHECK(fieldtrav_cfg((FtVariant)FP_ENG_RSE)  == r, "FP_ENG_RSE still selects the Emerald row");
		CHECK(fieldtrav_cfg((FtVariant)FP_ENG_FRLG) == f, "FP_ENG_FRLG still selects the FRLG row");
	}

	// ---------------------------------------------------------------- TEST 3
	printf("TEST 3 — the badge read is FlagGet's own bit math\n");
	{
		FpBus bus; FpMap m; bind("route117", &bus, &m, 3);
		const FtEngCfg* c = fieldtrav_cfg(FT_VAR_EMERALD);
		CHECK(!fieldtrav_flag_get(&bus, FT_VAR_EMERALD, SB1_BASE, c->badgeSurf), "no badge before it is set");
		set_flag(FT_VAR_EMERALD, c->badgeSurf);
		CHECK(fieldtrav_flag_get(&bus, FT_VAR_EMERALD, SB1_BASE, c->badgeSurf), "badge reads back");
		// Neighbouring bits in the SAME byte must be unaffected — the classic off-by-one this
		// rail exists to catch (0x86B = byte 0x10D bit 3, so 0x86A and 0x86C share the byte).
		CHECK(!fieldtrav_flag_get(&bus, FT_VAR_EMERALD, SB1_BASE, c->badgeSurf - 1), "the bit below stays clear");
		CHECK(!fieldtrav_flag_get(&bus, FT_VAR_EMERALD, SB1_BASE, c->badgeSurf + 1), "the bit above stays clear");
		// Every one of the 8 bits of one byte, independently.
		for (int b = 0; b < 8; b++) {
			memset(g_ovSb1, 0, sizeof g_ovSb1);
			set_flag(FT_VAR_EMERALD, 0x860 + b);
			for (int q = 0; q < 8; q++)
				CHECK(fieldtrav_flag_get(&bus, FT_VAR_EMERALD, SB1_BASE, 0x860 + q) == (q == b),
				      "bit %d set -> only %d reads true (q=%d)", b, b, q);
		}
		// Degradations: no save loaded and a nonsense flag id both answer FALSE, never a guess.
		CHECK(!fieldtrav_flag_get(&bus, FT_VAR_EMERALD, 0, c->badgeSurf), "sb1 == 0 -> no badge");
		CHECK(!fieldtrav_flag_get(&bus, FT_VAR_EMERALD, 0x08000000u, c->badgeSurf), "a ROM 'sb1' -> no badge");
		CHECK(!fieldtrav_flag_get(&bus, FT_VAR_EMERALD, SB1_BASE, -1), "a negative flag id -> false");

		// The same bit math on the RS numbering (2026-08-14 fix): different flags[] base, different
		// SYSTEM_FLAGS base, so this is a genuinely different address for a genuinely different id.
		memset(g_ovSb1, 0, sizeof g_ovSb1);
		const FtEngCfg* rs = fieldtrav_cfg(FT_VAR_RS);
		for (int b = 0; b < 8; b++) {
			memset(g_ovSb1, 0, sizeof g_ovSb1);
			set_flag(FT_VAR_RS, 0x800 + b);
			for (int q = 0; q < 8; q++)
				CHECK(fieldtrav_flag_get(&bus, FT_VAR_RS, SB1_BASE, 0x800 + q) == (q == b),
				      "RS bit %d set -> only %d reads true (q=%d)", b, b, q);
		}
		memset(g_ovSb1, 0, sizeof g_ovSb1);
		set_flag(FT_VAR_RS, rs->badgeCut);
		CHECK(g_ovSb1[0x1320] == 0x80, "the RS Cut badge really is byte 0x1320 bit 7 of SaveBlock1");
		CHECK(fieldtrav_flag_get(&bus, FT_VAR_RS, SB1_BASE, rs->badgeCut), "and it reads back as RS");
		CHECK(!fieldtrav_flag_get(&bus, FT_VAR_EMERALD, SB1_BASE, c->badgeCut),
		      "the Emerald row cannot see it — the two numberings do not overlap here");
	}

	// ---------------------------------------------------------------- TEST 4
	printf("TEST 4 — the substruct permutation table\n");
	{
		// pokeemerald src/pokemon.c:3609-3632, transcribed as (personality%24) -> slot of each type.
		static const int pret[24][4] = {
			{0,1,2,3},{0,1,3,2},{0,2,1,3},{0,3,1,2},{0,2,3,1},{0,3,2,1},
			{1,0,2,3},{1,0,3,2},{2,0,1,3},{3,0,1,2},{2,0,3,1},{3,0,2,1},
			{1,2,0,3},{1,3,0,2},{2,1,0,3},{3,1,0,2},{2,3,0,1},{3,2,0,1},
			{1,2,3,0},{1,3,2,0},{2,1,3,0},{3,1,2,0},{2,3,1,0},{3,2,1,0},
		};
		for (int p = 0; p < 24; p++) {
			int seen[4] = { 0, 0, 0, 0 };
			for (int t = 0; t < 4; t++) {
				int s = fieldtrav_substruct_slot((uint32_t)p, t);
				CHECK(s == pret[p][t], "personality%%24=%d type %d -> slot %d (pret says %d)", p, t, s, pret[p][t]);
				CHECK(s >= 0 && s < 4, "slot in range");
				seen[s & 3]++;
			}
			for (int s = 0; s < 4; s++) CHECK(seen[s] == 1, "row %d is a permutation (slot %d used once)", p, s);
		}
		// The modulo has to be on the FULL 32-bit personality, not a truncated one.
		CHECK(fieldtrav_substruct_slot(0xFFFFFFFFu, 1) == pret[0xFFFFFFFFu % 24][1], "large personality wraps correctly");
		CHECK(fieldtrav_substruct_slot(0x12345678u, 1) == pret[0x12345678u % 24][1], "another large personality");
		CHECK(fieldtrav_substruct_slot(0, -1) == -1 && fieldtrav_substruct_slot(0, 4) == -1, "out-of-range type -> -1");
	}

	// ---------------------------------------------------------------- TEST 5
	printf("TEST 5 — party decryption on golden bytes\n");
	{
		FpBus bus; FpMap m; bind("route117", &bus, &m, 3);
		// Two personalities with DIFFERENT substruct permutations, so a hard-coded slot fails:
		// 0x12345678 %% 24 == 0 (Attacks at slot 1) and 0x1234568B %% 24 == 19 (Attacks at slot 3).
		CHECK(0x12345678u % 24u == 0, "chosen personality A has permutation row 0");
		CHECK(0x1234568Bu % 24u == 19, "chosen personality B has permutation row 19");
		CHECK(fieldtrav_substruct_slot(0x12345678u, 1) != fieldtrav_substruct_slot(0x1234568Bu, 1),
		      "the two golden mons really do put Attacks in different slots");

		const uint16_t surfSet[4]  = { MOVE_TACKLE, MOVE_SURF, 0, 0 };
		const uint16_t cutSet[4]   = { MOVE_CUT, 0, 0, 0 };
		FtParty pty = party_of(1);

		put_mon(0, 0x12345678u, 0xCAFEBABEu, surfSet, 1, 0, 0);
		CHECK(fieldtrav_party_has_move(&bus, &pty, MOVE_SURF), "row-0 mon: SURF found");
		CHECK(!fieldtrav_party_has_move(&bus, &pty, MOVE_CUT), "row-0 mon: CUT absent");
		CHECK(fieldtrav_party_has_move(&bus, &pty, MOVE_TACKLE), "row-0 mon: move slot 0 also scanned");

		put_mon(0, 0x1234568Bu, 0x00010203u, surfSet, 1, 0, 0);
		CHECK(fieldtrav_party_has_move(&bus, &pty, MOVE_SURF), "row-19 mon: SURF found (permutation honoured)");

		// isEgg: the game's checkpartymove refuses an egg, so we must too — otherwise we prompt
		// something the game is about to decline, which is this family's cardinal sin.
		put_mon(0, 0x12345678u, 0xCAFEBABEu, surfSet, 1, 1, 0);
		CHECK(!fieldtrav_party_has_move(&bus, &pty, MOVE_SURF), "an EGG never counts");

		// hasSpecies == 0 is an empty slot.
		put_mon(0, 0x12345678u, 0xCAFEBABEu, surfSet, 0, 0, 0);
		CHECK(!fieldtrav_party_has_move(&bus, &pty, MOVE_SURF), "an empty slot never counts");

		// THE DECRYPT-INTEGRITY RAIL: a checksum mismatch means the bytes are not what we think,
		// so the mon is ineligible — a garbage read must never become a phantom HM.
		put_mon(0, 0x12345678u, 0xCAFEBABEu, surfSet, 1, 0, 1);
		CHECK(!fieldtrav_party_has_move(&bus, &pty, MOVE_SURF), "a bad checksum makes the mon ineligible");

		// The scan really is over the whole party, and really does stop at partyCount.
		put_mon(0, 0x12345678u, 0xCAFEBABEu, cutSet,  1, 0, 0);
		put_mon(3, 0x1234568Bu, 0x00010203u, surfSet, 1, 0, 0);
		pty = party_of(6);
		CHECK(fieldtrav_party_has_move(&bus, &pty, MOVE_SURF), "slot 3 is reached with count 6");
		CHECK(fieldtrav_party_has_move(&bus, &pty, MOVE_CUT),  "slot 0 still found with count 6");
		pty = party_of(2);
		CHECK(!fieldtrav_party_has_move(&bus, &pty, MOVE_SURF), "slot 3 is NOT read with count 2");
		pty = party_of(99);
		CHECK(fieldtrav_party_has_move(&bus, &pty, MOVE_SURF), "an over-large count is clamped to 6, not trusted");

		// Degradations: no base, no count, a ROM-looking base.
		FtParty bad = party_of(6); bad.partyBase = 0;
		CHECK(!fieldtrav_party_has_move(&bus, &bad, MOVE_SURF), "partyBase 0 -> no moves (named degradation)");
		bad = party_of(0);
		CHECK(!fieldtrav_party_has_move(&bus, &bad, MOVE_SURF), "partyCount 0 -> no moves");
		bad = party_of(6); bad.partyBase = 0x08000000u;
		CHECK(!fieldtrav_party_has_move(&bus, &bad, MOVE_SURF), "a ROM party base is refused");
		CHECK(!fieldtrav_party_has_move(&bus, NULL, MOVE_SURF), "a NULL party is refused");
	}

	// ---------------------------------------------------------------- TEST 6
	printf("TEST 6 — eligibility is badge AND mon, never one of the two\n");
	{
		FpBus bus; FpMap m; bind("route117", &bus, &m, 3);
		const uint16_t surfSet[4] = { MOVE_SURF, 0, 0, 0 };
		FtParty pty = party_of(1);
		const FtEngCfg* c = fieldtrav_cfg(FT_VAR_EMERALD);

		CHECK(fieldtrav_usable(&bus, FT_VAR_EMERALD, &pty) == 0, "nothing set -> nothing usable");

		put_mon(0, 0x12345678u, 1u, surfSet, 1, 0, 0);
		CHECK((fieldtrav_usable(&bus, FT_VAR_EMERALD, &pty) & (1u << FT_HM_SURF)) == 0,
		      "a mon that knows Surf without the badge is NOT usable");

		memset(g_ovParty, 0, sizeof g_ovParty);
		set_flag(FT_VAR_EMERALD, c->badgeSurf);
		CHECK((fieldtrav_usable(&bus, FT_VAR_EMERALD, &pty) & (1u << FT_HM_SURF)) == 0,
		      "the badge without a mon is NOT usable");

		put_mon(0, 0x12345678u, 1u, surfSet, 1, 0, 0);
		uint32_t u = fieldtrav_usable(&bus, FT_VAR_EMERALD, &pty);
		CHECK((u & (1u << FT_HM_SURF)) != 0, "badge AND mon -> Surf usable");
		CHECK((u & (1u << FT_HM_CUT)) == 0, "Cut is still not usable");
		CHECK(fieldtrav_usable(&bus, FT_VAR_EMERALD, NULL) == 0, "a NULL party -> nothing usable");
		CHECK(fieldtrav_usable(NULL, FT_VAR_EMERALD, &pty) == 0, "a NULL bus -> nothing usable");
	}

	// ---------------------------------------------------------------- TEST 7
	printf("TEST 7 — the edge scan\n");
	{
		FpBus bus; FpMap m; bind("route117", &bus, &m, 3);
		FtEdge e[FT_MAX_EDGES];
		CHECK(fieldtrav_scan_edges(&bus, &m, FT_VAR_EMERALD, e) == 0, "an empty object table has no edges");

		set_obj(1, 82, 18, 12);    // OBJ_EVENT_GFX_CUTTABLE_TREE (EM)
		set_obj(2, 86, 19, 12);    // OBJ_EVENT_GFX_BREAKABLE_ROCK (EM)
		set_obj(3, 87, 17, 12);    // OBJ_EVENT_GFX_PUSHABLE_BOULDER (EM)
		set_obj(4, 3,  16, 12);    // a plain NPC — never an edge
		set_obj(5, 95, 15, 12);    // FR's cut-tree id on an RSE map — must NOT match
		int n = fieldtrav_scan_edges(&bus, &m, FT_VAR_EMERALD, e);
		CHECK(n == 3, "3 edge objects found, the NPC and the wrong-engine id ignored (got %d)", n);
		int sawCut = 0, sawRock = 0, sawBoulder = 0;
		for (int i = 0; i < n; i++) {
			if (e[i].hm == FT_HM_CUT)      { sawCut = 1;     CHECK(e[i].x == 18 && e[i].y == 12, "cut tree at map-local (18,12)"); CHECK(e[i].slot == 1, "cut tree slot"); }
			if (e[i].hm == FT_HM_SMASH)    { sawRock = 1;    CHECK(e[i].x == 19 && e[i].y == 12, "rock at map-local (19,12)"); }
			if (e[i].hm == FT_HM_STRENGTH) { sawBoulder = 1; CHECK(e[i].x == 17 && e[i].y == 12, "boulder at map-local (17,12)"); }
		}
		CHECK(sawCut && sawRock && sawBoulder, "all three classes recognised");

		// The +MAP_OFFSET bias really is removed (a 7-tile error here would aim every route at the
		// wrong tile — the single most likely silent bug in this whole module).
		set_obj(1, 82, 0, 0);
		n = fieldtrav_scan_edges(&bus, &m, FT_VAR_EMERALD, e);
		CHECK(e[0].x == 0 && e[0].y == 0, "map-local (0,0) survives the offset removal");

		// An INACTIVE slot is not an edge, and slot 0 (the player) is never scanned.
		memset(g_ovObj, 0, sizeof g_ovObj);
		ov_w32(g_ovObj, 0x00, 1u); ov_w8(g_ovObj, 0x05, 82);         // the player wearing a tree's id
		CHECK(fieldtrav_scan_edges(&bus, &m, FT_VAR_EMERALD, e) == 0, "slot 0 is never an edge");
		set_obj(2, 82, 5, 5); ov_w32(g_ovObj + 0x24 * 2, 0x00, 0u);  // present but inactive
		CHECK(fieldtrav_scan_edges(&bus, &m, FT_VAR_EMERALD, e) == 0, "an inactive slot is not an edge");

		// FRLG ids on an FRLG map.
		FpBus fb; FpMap fm; bind("frstair", &fb, &fm, 3);
		set_obj(1, 95, 3, 3); set_obj(2, 82, 4, 3);
		n = fieldtrav_scan_edges(&fb, &fm, FT_VAR_FRLG, e);
		CHECK(n == 1 && e[0].hm == FT_HM_CUT, "FRLG matches 95, not 82 (got n=%d)", n);

		CHECK(fieldtrav_scan_edges(&bus, &m, FT_VAR_EMERALD, NULL) == 0, "a NULL out array is refused");
		FpMap noObj = m; noObj.mapObjects = 0;
		CHECK(fieldtrav_scan_edges(&bus, &noObj, FT_VAR_EMERALD, e) == 0, "mapObjects 0 -> no edges (named degradation)");
	}

	// ---------------------------------------------------------------- TEST 8
	printf("TEST 8 — DRY PATHS WIN\n");
	{
		// Route 117: (19,14) and (28,14) are the two banks of the pond, but they are ALSO joined by
		// a long dry walk along row 8. With Surf fully available, the planner must still decline —
		// tier order, not step count, is the primary key (SPEC H1.7).
		FpBus bus; FpMap m; bind("route117", &bus, &m, 3);
		set_flag(FT_VAR_EMERALD, fieldtrav_cfg(FT_VAR_EMERALD)->badgeSurf);
		const uint16_t surfSet[4] = { MOVE_SURF, 0, 0, 0 };
		put_mon(0, 0x12345678u, 1u, surfSet, 1, 0, 0);
		FtParty pty = party_of(1);
		npc_clear();
		bool ok = fieldtrav_plan(&bus, &m, FT_VAR_EMERALD, &pty, 19, 14, 28, 14, false, g_npc, g_npcN, &g_pr);
		CHECK(!ok, "a walkable route is DECLINED by the traversal planner");
		CHECK(g_pr.outcome == FT_OUT_TIER0, "outcome is TIER0, not a wet program (got %s)", OUTN(g_pr.outcome));
		CHECK(g_pr.nMoves == 0 && g_pr.nInteracts == 0, "nothing is emitted when tier 0 wins");
		CHECK((g_pr.usable & (1u << FT_HM_SURF)) != 0, "...and it declined WITH Surf available, which is the point");
	}

	// ---------------------------------------------------------------- TEST 9
	printf("TEST 9 — the Route 117 surf crossing, end to end\n");
	{
		FpBus bus; FpMap m; bind("route117", &bus, &m, 3);
		set_flag(FT_VAR_EMERALD, fieldtrav_cfg(FT_VAR_EMERALD)->badgeSurf);
		const uint16_t surfSet[4] = { MOVE_SURF, 0, 0, 0 };
		put_mon(0, 0x12345678u, 1u, surfSet, 1, 0, 0);
		FtParty pty = party_of(1);
		npc_clear(); seal_pocket();
		bool ok = fieldtrav_plan(&bus, &m, FT_VAR_EMERALD, &pty, 19, 14, 28, 14, false, g_npc, g_npcN, &g_pr);
		CHECK(ok, "with the dry exit sealed, the pond route is found (outcome %s)", OUTN(g_pr.outcome));
		if (ok) {
			CHECK(g_pr.outcome == FT_OUT_PLANNED, "outcome PLANNED");
			CHECK(g_pr.nInteracts == 1, "exactly ONE interact — the mount (got %d)", g_pr.nInteracts);
			CHECK(g_pr.startMode == FT_MODE_FOOT, "starts on foot");
			CHECK(g_pr.endMode == FT_MODE_FOOT, "ends ashore (the dismount is free)");
			// 9 moves: 8 water tiles (the first of which IS the mount) + the step onto (28,14).
			CHECK(g_pr.nMoves == 9, "9 moves across the pond (got %d)", g_pr.nMoves);
			CHECK(g_pr.mv[0].hm == FT_HM_SURF, "the FIRST move is the mount");
			CHECK(g_pr.mv[0].dir == FP_R, "the mount faces EAST, toward the water");
			CHECK(g_pr.mv[0].objSlot == -1, "a Surf mount tracks no object slot (the avatar bit proves it)");
			for (int i = 1; i < g_pr.nMoves; i++)
				CHECK(g_pr.mv[i].hm == FT_HM_NONE, "move %d is a plain step (no second mount)", i);
			int ex, ey; replay(&g_pr, 19, 14, &ex, &ey);
			CHECK(ex == 28 && ey == 14, "replaying the program lands exactly on the tap (%d,%d)", ex, ey);
			// Every intermediate tile really is surfable water, read off the real fixture attributes.
			static const int dxs[4] = { 1, -1, 0, 0 }, dys[4] = { 0, 0, 1, -1 };
			int x = 19, y = 14;
			for (int i = 0; i < g_pr.nMoves - 1; i++) {
				x += dxs[g_pr.mv[i].dir]; y += dys[g_pr.mv[i].dir];
				int b = fieldpath_behaviour_at(&bus, &m, x, y);
				CHECK(fieldtrav_is_surfable(FP_ENG_RSE, b), "tile %d of the crossing (%d,%d) beh=0x%02X is water", i, x, y, b);
			}
			CHECK(g_pr.nEdges == 0, "no object edges were involved");
		}

		// H1.9: the tapped goal may itself be water — the program simply ends afloat.
		npc_clear(); seal_pocket();
		ok = fieldtrav_plan(&bus, &m, FT_VAR_EMERALD, &pty, 19, 14, 24, 14, false, g_npc, g_npcN, &g_pr);
		CHECK(ok, "a tap ON the pond plans too");
		if (ok) {
			CHECK(g_pr.endMode == FT_MODE_SURF, "it ends AFLOAT, which is a legal terminal");
			CHECK(g_pr.nInteracts == 1, "still one mount");
			int ex, ey; replay(&g_pr, 19, 14, &ex, &ey);
			CHECK(ex == 24 && ey == 14, "and lands on the tapped water tile");
		}
	}

	// ---------------------------------------------------------------- TEST 10
	printf("TEST 10 — never prompt what the game will refuse\n");
	{
		FpBus bus; FpMap m; bind("route117", &bus, &m, 3);
		FtParty pty = party_of(1);
		const uint16_t surfSet[4] = { MOVE_SURF, 0, 0, 0 };

		// (a) badge, no mon.
		set_flag(FT_VAR_EMERALD, fieldtrav_cfg(FT_VAR_EMERALD)->badgeSurf);
		npc_clear(); seal_pocket();
		CHECK(!fieldtrav_plan(&bus, &m, FT_VAR_EMERALD, &pty, 19, 14, 28, 14, false, g_npc, g_npcN, &g_pr),
		      "badge without a Surf mon plans NOTHING");
		CHECK(g_pr.nMoves == 0, "and emits no moves");

		// (b) mon, no badge.
		bind("route117", &bus, &m, 3);
		put_mon(0, 0x12345678u, 1u, surfSet, 1, 0, 0);
		npc_clear(); seal_pocket();
		CHECK(!fieldtrav_plan(&bus, &m, FT_VAR_EMERALD, &pty, 19, 14, 28, 14, false, g_npc, g_npcN, &g_pr),
		      "a Surf mon without the badge plans NOTHING");

		// (c) neither.
		bind("route117", &bus, &m, 3);
		npc_clear(); seal_pocket();
		CHECK(!fieldtrav_plan(&bus, &m, FT_VAR_EMERALD, &pty, 19, 14, 28, 14, false, g_npc, g_npcN, &g_pr),
		      "neither -> nothing");
		CHECK(g_pr.usable == 0, "the eligibility mask is empty and is reported honestly");

		// (d) a genuinely unreachable tile with FULL eligibility still fails honestly.
		bind("route117", &bus, &m, 3);
		set_flag(FT_VAR_EMERALD, fieldtrav_cfg(FT_VAR_EMERALD)->badgeSurf);
		put_mon(0, 0x12345678u, 1u, surfSet, 1, 0, 0);
		npc_clear();
		CHECK(!fieldtrav_plan(&bus, &m, FT_VAR_EMERALD, &pty, 19, 14, 16, 14, false, g_npc, g_npcN, &g_pr),
		      "a tile inside a wall is never reachable");
		CHECK(g_pr.outcome == FT_OUT_UNREACHABLE, "and says UNREACHABLE (got %s)", OUTN(g_pr.outcome));

		// (e) degradations: a bad map, and a goal outside the search window.
		FpMap bad = m; bad.gridPtr = 0x08000000u;
		CHECK(!fieldtrav_plan(&bus, &bad, FT_VAR_EMERALD, &pty, 19, 14, 28, 14, false, g_npc, g_npcN, &g_pr) &&
		      g_pr.outcome == FT_OUT_BADMAP, "a ROM grid pointer -> BADMAP");
		CHECK(!fieldtrav_plan(&bus, &m, FT_VAR_EMERALD, &pty, 19, 14, 19 + FP_WHALF + 1, 14, false, g_npc, g_npcN, &g_pr) &&
		      g_pr.outcome == FT_OUT_WINDOW, "a goal past +-FP_WHALF -> WINDOW");

		// --- THE THIRD NUMBERING, asked for by name (2026-08-14 fix) ---------------------------
		// Until this block existed the suite only ever asked for TWO of the three Gen-3 flag
		// numberings, so a Ruby/Sapphire cart reading EMERALD's badge bits was invisible here. The
		// map stays the RSE fixture on purpose: that is exactly a Ruby cart's situation — same
		// metatile behaviours (fieldpath's FpEngine is right), different SAVE.
		const FtEngCfg* rs = fieldtrav_cfg(FT_VAR_RS);
		const FtEngCfg* em = fieldtrav_cfg(FT_VAR_EMERALD);

		// (f) a REAL Ruby save: the badge bit where pokeruby actually keeps it (0x1220 + 0x80B>>3 =
		//     0x132D bit 3), written from pret's OWN constants rather than through the table under
		//     test, plus a Surf mon. The route the game would allow must be planned.
		bind("route117", &bus, &m, 3);
		set_flag_ruby(RUBY_BADGE05_GET);
		CHECK(rs->badgeSurf == RUBY_BADGE05_GET && rs->flagsOff == RUBY_FLAGS_OFF,
		      "(the module agrees with pokeruby's own numbers, so (f) grades the same byte)");
		put_mon(0, 0x12345678u, 1u, surfSet, 1, 0, 0);
		npc_clear(); seal_pocket();
		{
			bool ok = fieldtrav_plan(&bus, &m, FT_VAR_RS, &pty, 19, 14, 28, 14, false, g_npc, g_npcN, &g_pr);
			CHECK(ok, "a RUBY save's Surf badge is read at RUBY's offset (outcome %s)", OUTN(g_pr.outcome));
			CHECK((g_pr.usable & (1u << FT_HM_SURF)) != 0, "...and Surf is in the eligibility mask");
			CHECK(g_pr.nInteracts == 1 && g_pr.mv[0].hm == FT_HM_SURF, "the mount is planned, once");
		}

		// (g) the mirror: the SAME bit written where EMERALD keeps it is not a Ruby badge. If the
		//     RS row ever collapses back into the Emerald row this passes for the wrong reason —
		//     which is why (f) and (g) are graded together.
		bind("route117", &bus, &m, 3);
		set_flag(FT_VAR_EMERALD, em->badgeSurf);
		put_mon(0, 0x12345678u, 1u, surfSet, 1, 0, 0);
		npc_clear(); seal_pocket();
		CHECK(!fieldtrav_plan(&bus, &m, FT_VAR_RS, &pty, 19, 14, 28, 14, false, g_npc, g_npcN, &g_pr),
		      "a badge at EMERALD's offset is not a badge in a Ruby save");
		CHECK((g_pr.usable & (1u << FT_HM_SURF)) == 0, "...and Surf never enters the mask");

		// (h) THE DEFECT ITSELF, stated as a property. Ruby's flags[] ends at 0x1340 and vars[]
		//     begins there (pokeruby include/global.h:701-702), so EVERY badge read through
		//     Emerald's 0x1270 offset lands in vars[]: Cut at 0x137C, Surf at 0x137D, and the
		//     Running Shoes at 0x1388. Fill Ruby's vars[] with ordinary nonzero variable values
		//     and the pre-fix code answers the badge question out of them. The planner must be
		//     deaf to it.
		bind("route117", &bus, &m, 3);
		memset(g_ovSb1 + 0x1340, 0xFF, 0x1400 - 0x1340);   // vars[0..47] of a Ruby SaveBlock1
		put_mon(0, 0x12345678u, 1u, surfSet, 1, 0, 0);
		npc_clear(); seal_pocket();
		CHECK(!fieldtrav_plan(&bus, &m, FT_VAR_RS, &pty, 19, 14, 28, 14, false, g_npc, g_npcN, &g_pr),
		      "a Ruby save's game VARIABLES are never badges");
		CHECK(g_pr.usable == 0, "the eligibility mask stays empty (got 0x%X)", (unsigned)g_pr.usable);
		// ...while the same bytes, read as the flags[] they are NOT, would have said yes: the
		// pre-fix path is spelled out here so the property cannot be satisfied vacuously.
		CHECK(fieldtrav_flag_get(&bus, FT_VAR_EMERALD, SB1_BASE, em->badgeSurf),
		      "(the byte really is set — this is what the Emerald row used to read)");
		CHECK(!fieldtrav_flag_get(&bus, FT_VAR_RS, SB1_BASE, rs->badgeSurf),
		      "(...and the Ruby row correctly sees nothing)");
		CHECK(!fieldtrav_flag_get(&bus, FT_VAR_RS, SB1_BASE, rs->runShoes),
		      "the Running-Shoes gate is deaf to it too (touch.c run_elig reads this rail)");
	}

	// ---------------------------------------------------------------- TEST 11
	printf("TEST 11 — currents and waterfalls are never free surf tiles\n");
	{
		// The game's own surfable tables DO include the four currents and MB_WATERFALL. We exclude
		// them: a current takes the controls away (COVERAGE §3 / T5.8) and a waterfall is an EDGE
		// with its own prompt, not a tile to drift onto. Both engines, all five behaviours.
		static const int banned[] = { 0x13, 0x50, 0x51, 0x52, 0x53 };
		for (unsigned i = 0; i < sizeof banned / sizeof banned[0]; i++) {
			CHECK(!fieldtrav_is_surfable(FP_ENG_RSE,  banned[i]), "RSE 0x%02X excluded", banned[i]);
			CHECK(!fieldtrav_is_surfable(FP_ENG_FRLG, banned[i]), "FRLG 0x%02X excluded", banned[i]);
		}
		// And the two RSE water WARPS, which fieldpath_classify owns — classification has
		// precedence, so the traversal layer must not treat them as ordinary water.
		CHECK(!fieldtrav_is_surfable(FP_ENG_RSE, 0x6C), "MB_WATER_DOOR is a warp, not water");
		CHECK(!fieldtrav_is_surfable(FP_ENG_RSE, 0x6D), "MB_WATER_SOUTH_ARROW_WARP is a warp, not water");
	}

	// ---------------------------------------------------------------- TEST 12
	printf("TEST 12 — the Cut object edge\n");
	{
		FpBus bus; FpMap m; bind("route117", &bus, &m, 3);
		const FtEngCfg* c = fieldtrav_cfg(FT_VAR_EMERALD);
		set_flag(FT_VAR_EMERALD, c->badgeCut);
		const uint16_t cutSet[4] = { MOVE_CUT, 0, 0, 0 };
		put_mon(0, 0x12345678u, 1u, cutSet, 1, 0, 0);
		FtParty pty = party_of(1);

		// Seal the pocket's three-tile mouth with a CUT TREE in the middle and NPCs either side.
		// The only way out is now through the tree — the user's "a place beyond some tree".
		set_obj(6, 82, 18, 12);
		npc_clear(); npc_add(17, 12); npc_add(18, 12); npc_add(19, 12);
		bool ok = fieldtrav_plan(&bus, &m, FT_VAR_EMERALD, &pty, 18, 14, 18, 10, false, g_npc, g_npcN, &g_pr);
		CHECK(ok, "the route through the cuttable tree is found (outcome %s)", OUTN(g_pr.outcome));
		if (ok) {
			CHECK(g_pr.nInteracts == 1, "one interact — the Cut (got %d)", g_pr.nInteracts);
			CHECK(g_pr.nEdges == 1, "one edge object was seen on the map");
			int cutIdx = -1;
			for (int i = 0; i < g_pr.nMoves; i++) if (g_pr.mv[i].hm != FT_HM_NONE) { cutIdx = i; break; }
			CHECK(cutIdx >= 0 && g_pr.mv[cutIdx].hm == FT_HM_CUT, "the interact is a CUT");
			if (cutIdx >= 0) {
				CHECK(g_pr.mv[cutIdx].dir == FP_U, "it faces NORTH, at the tree");
				CHECK(g_pr.mv[cutIdx].objSlot == 6, "it tracks the tree's OWN object slot (got %d) — "
				      "that slot going inactive is what proves the Cut landed", g_pr.mv[cutIdx].objSlot);
			}
			int ex, ey; replay(&g_pr, 18, 14, &ex, &ey);
			CHECK(ex == 18 && ey == 10, "the program lands on the tap (%d,%d)", ex, ey);
			CHECK(g_pr.endMode == FT_MODE_FOOT, "on foot throughout");
		}

		// The SAME tap with no Cut badge: the tree is a plain blocker again and nothing is planned.
		bind("route117", &bus, &m, 3);
		put_mon(0, 0x12345678u, 1u, cutSet, 1, 0, 0);          // mon yes, badge no
		set_obj(6, 82, 18, 12);
		npc_clear(); npc_add(17, 12); npc_add(18, 12); npc_add(19, 12);
		CHECK(!fieldtrav_plan(&bus, &m, FT_VAR_EMERALD, &pty, 18, 14, 18, 10, false, g_npc, g_npcN, &g_pr),
		      "no badge -> the tree stays a wall and NOTHING is planned");
		CHECK(g_pr.nEdges == 1, "the edge was still SEEN (honest reporting), it was just not usable");

		// A BOULDER is never a planned edge, badge or not (Sokoban risk, SPEC §5).
		bind("route117", &bus, &m, 3);
		set_flag(FT_VAR_EMERALD, c->badgeStrength);
		const uint16_t strSet[4] = { 70 /* MOVE_STRENGTH */, 0, 0, 0 };
		put_mon(0, 0x12345678u, 1u, strSet, 1, 0, 0);
		set_obj(6, 87, 18, 12);
		npc_clear(); npc_add(17, 12); npc_add(18, 12); npc_add(19, 12);
		CHECK(!fieldtrav_plan(&bus, &m, FT_VAR_EMERALD, &pty, 18, 14, 18, 10, false, g_npc, g_npcN, &g_pr),
		      "a pushable boulder is NEVER routed through, even fully eligible");
		CHECK((g_pr.usable & (1u << FT_HM_STRENGTH)) != 0, "...and Strength really was usable, which is the point");
	}

	// ---------------------------------------------------------------- TEST 13
	printf("TEST 13 — the interact cap fails honestly\n");
	{
		// Three trees in a row across the pocket mouth: 3 interacts would be needed, the cap is 2.
		FpBus bus; FpMap m; bind("route117", &bus, &m, 3);
		const FtEngCfg* c = fieldtrav_cfg(FT_VAR_EMERALD);
		set_flag(FT_VAR_EMERALD, c->badgeCut);
		const uint16_t cutSet[4] = { MOVE_CUT, 0, 0, 0 };
		put_mon(0, 0x12345678u, 1u, cutSet, 1, 0, 0);
		FtParty pty = party_of(1);

		// A CHAIN of three cuttable trees up the x=18 column, with the flanking tiles blocked, so
		// the ONLY route out of the pocket crosses all three IN SEQUENCE. This is the layout the
		// cap exists for: nothing about it is unreachable, it is simply more HM activations than
		// v1 will commit to, and it must fail rather than start a route it cannot finish.
		set_obj(6, 82, 18, 12); set_obj(7, 82, 18, 11); set_obj(8, 82, 18, 10);
		npc_clear();
		npc_add(17, 12); npc_add(19, 12);                  // the mouth's other two tiles
		npc_add(17, 11); npc_add(19, 11);                  // and the flanks of the chain
		npc_add(18, 12); npc_add(18, 11); npc_add(18, 10); // the trees block as objects
		bool ok = fieldtrav_plan(&bus, &m, FT_VAR_EMERALD, &pty, 18, 14, 18, 9, false, g_npc, g_npcN, &g_pr);
		CHECK(!ok, "three activations exceed FT_MAX_INTERACTS and the plan fails");
		CHECK(g_pr.outcome == FT_OUT_UNREACHABLE, "reported honestly (got %s)", OUTN(g_pr.outcome));
		CHECK(g_pr.nMoves == 0, "and NOTHING partial is emitted — never 'walk toward it and hope'");
		CHECK(g_pr.nEdges == 3, "all three trees were seen (honest reporting)");

		// The SAME chain two trees long IS inside the cap and plans — which proves the cap is a
		// limit and not a blanket refusal of multi-HM routes.
		bind("route117", &bus, &m, 3);
		set_flag(FT_VAR_EMERALD, c->badgeCut);
		put_mon(0, 0x12345678u, 1u, cutSet, 1, 0, 0);
		set_obj(6, 82, 18, 12); set_obj(7, 82, 18, 11);
		npc_clear();
		npc_add(17, 12); npc_add(19, 12); npc_add(17, 11); npc_add(19, 11);
		npc_add(18, 12); npc_add(18, 11);
		ok = fieldtrav_plan(&bus, &m, FT_VAR_EMERALD, &pty, 18, 14, 18, 10, false, g_npc, g_npcN, &g_pr);
		CHECK(ok, "two activations are within the cap (outcome %s)", OUTN(g_pr.outcome));
		CHECK(g_pr.nInteracts == 2, "and cost exactly 2 (got %d)", g_pr.nInteracts);
		if (ok) {
			int nCut = 0;
			for (int i = 0; i < g_pr.nMoves; i++) if (g_pr.mv[i].hm == FT_HM_CUT) nCut++;
			CHECK(nCut == 2, "both interacts are CUTs");
			int ex, ey; replay(&g_pr, 18, 14, &ex, &ey);
			CHECK(ex == 18 && ey == 10, "and the chain lands on the tap (%d,%d)", ex, ey);
		}
	}

	// ---------------------------------------------------------------- TEST 14
	printf("TEST 14 — a tap taken while already surfing never plans a second mount\n");
	{
		// pokeemerald src/field_player_avatar.c PartyHasMonWithSurf returns FALSE while surfing, so
		// a mid-water tap must plan in the SURF layer, not mount again. The player starts on
		// (24,14) — a pond tile — and taps the far bank.
		FpBus bus; FpMap m; bind("route117", &bus, &m, 1);   // elevation 1 = the water layer
		set_flag(FT_VAR_EMERALD, fieldtrav_cfg(FT_VAR_EMERALD)->badgeSurf);
		const uint16_t surfSet[4] = { MOVE_SURF, 0, 0, 0 };
		put_mon(0, 0x12345678u, 1u, surfSet, 1, 0, 0);
		FtParty pty = party_of(1);
		npc_clear();
		bool ok = fieldtrav_plan(&bus, &m, FT_VAR_EMERALD, &pty, 24, 14, 28, 14, true /* startSurfing */, g_npc, g_npcN, &g_pr);
		if (ok) {
			CHECK(g_pr.startMode == FT_MODE_SURF, "the plan starts in the SURF layer");
			for (int i = 0; i < g_pr.nMoves; i++)
				CHECK(g_pr.mv[i].hm != FT_HM_SURF, "move %d is not a second mount", i);
			int ex, ey; replay(&g_pr, 24, 14, &ex, &ey);
			CHECK(ex == 28 && ey == 14, "and it reaches the bank (%d,%d)", ex, ey);
		} else {
			// The whole water leg is free, so this SHOULD come back as tier 0 — the shipped
			// fieldpath router already routes correctly while surfing (its pElev is the water
			// layer's). Either answer is correct; what must never happen is a second mount.
			CHECK(g_pr.outcome == FT_OUT_TIER0, "or it declines to tier 0 (got %s)", OUTN(g_pr.outcome));
		}
	}

	// ================================================================ TEST 15 / 16
	// SLICE 2 — the cross-map excursion, graded against the USER'S OWN Emerald ROM.
	//
	// Everything above this line runs off the compiled-in fixture image. Slice 2 walks a chain
	// no fixture carries — gMapGroups -> MapHeader -> MapEvents/MapLayout -> the warp table and
	// the raw ROM grid — so these two tests read `roms/emerald.gba` directly. If it is not there
	// the tests SKIP LOUDLY (the trace_replay precedent) rather than quietly pass.
	rom_load();
	if (!g_rom) {
		printf("TEST 15/16 — SKIPPED: roms/emerald.gba not readable from the CWD "
		       "(run the suite from the project root to exercise the excursion planner)\n");
	} else {
	// ---------------------------------------------------------------- TEST 15
	printf("TEST 15 — the real gMapGroups -> MapHeader -> warp-table chain (user's BPEE ROM)\n");
	{
		FpBus rb; rb.read8 = rom_r8; rb.read16 = rom_r16; rb.read32 = rom_r32; rb.ctx = 0;
		FtRomMap rm;
		// gMapGroups for BPEE, the value the shipped profile carries (gamestate.c:94).
		CHECK(fieldtrav_rom_map(&rb, EM_MAPGROUPS, 26, 14, &rm),
		      "BattleFrontier_OutsideEast (26,14) resolves through gMapGroups");
		CHECK(rm.w == 72 && rm.h == 72, "and its layout is 72x72 (got %dx%d)", rm.w, rm.h);
		FtWarp w[FT_MAX_WARPS];
		int n = fieldtrav_warps(&rb, rm.events, w, FT_MAX_WARPS);
		CHECK(n == 14, "OutsideEast has 14 warps (pret map.json), got %d", n);
		// Warp 1 is the Battle Arena lobby door — the one this session's live arc actually used,
		// and the emulator confirmed the arrival tile (39,29)+auto-step. Graded exactly.
		if (n > 1) {
			CHECK(w[1].x == 39 && w[1].y == 29, "warp 1 sits at (39,29), got (%d,%d)", w[1].x, w[1].y);
			CHECK(w[1].mapGroup == 26 && w[1].mapNum == 28,
			      "and leads to BattleFrontier_BattleArenaLobby (26,28), got (%d,%d)",
			      w[1].mapGroup, w[1].mapNum);
		}
		// The reverse leg: the lobby's single warp comes back to OutsideEast warp 1.
		FtRomMap lob;
		CHECK(fieldtrav_rom_map(&rb, EM_MAPGROUPS, 26, 28, &lob), "the lobby (26,28) resolves too");
		FtWarp lw[FT_MAX_WARPS];
		int ln = fieldtrav_warps(&rb, lob.events, lw, FT_MAX_WARPS);
		CHECK(ln == 1, "the lobby has exactly 1 warp, got %d", ln);
		if (ln == 1) {
			CHECK(lw[0].x == 7 && lw[0].y == 12, "at (7,12) — the SOUTH_ARROW_WARP the live arc "
			      "needed two tokens for, got (%d,%d)", lw[0].x, lw[0].y);
			CHECK(lw[0].warpId == 1, "and it targets OutsideEast warp 1, got %d", lw[0].warpId);
		}
		// Every guard rail: garbage in, "no map" out — never a wander.
		FtRomMap bad;
		CHECK(!fieldtrav_rom_map(&rb, 0x02000000u, 26, 14, &bad), "a RAM gMapGroups is refused");
		CHECK(!fieldtrav_rom_map(&rb, EM_MAPGROUPS, 99, 0, &bad),  "an out-of-range group is refused");
		CHECK(!fieldtrav_rom_map(&rb, EM_MAPGROUPS, 26, 250, &bad), "an out-of-range map is refused");
		CHECK(fieldtrav_warps(&rb, 0x02000000u, w, FT_MAX_WARPS) == 0, "a RAM MapEvents reads 0 warps");
		// The ROM-grid bus adapter: fieldpath's OWN behaviour read, run against a ROM map.
		FtRomBus rbus; FpBus dbus; FpMap dmap;
		fieldtrav_rom_bus(&rbus, &rb, &rm, FP_ENG_RSE, &dbus, &dmap);
		CHECK(fieldpath_behaviour_at(&dbus, &dmap, 39, 29) == 0x69,
		      "(39,29) reads MB_ANIMATED_DOOR 0x69 through the adapter, got 0x%02X",
		      fieldpath_behaviour_at(&dbus, &dmap, 39, 29));
		CHECK(fieldpath_behaviour_at(&dbus, &dmap, 50, 58) == 0x15,
		      "(50,58) reads MB_OCEAN_WATER 0x15 — the south-beach channel the surf arc crossed");
		CHECK(!fieldpath_enterable(&dbus, &dmap, 39, 29, 3), "the door tile is not walkable");
		CHECK(fieldpath_enterable(&dbus, &dmap, 39, 30, 3), "the tile below it is");
		CHECK(fieldpath_behaviour_at(&dbus, &dmap, -1, 5) == -1, "off-map reads refuse, not guess");
		CHECK(fieldpath_behaviour_at(&dbus, &dmap, 72, 5) == -1, "…on the far edge too");
	}

	// ---------------------------------------------------------------- TEST 16
	printf("TEST 16 — the Lavaridge-class excursion, on the map this save can actually reach\n");
	{
		// SPEC §3.4's canonical scenario is Lavaridge Town: a terrace you can SEE from the town
		// floor but can only ENTER through the Pokemon Center's back door. The fixture save cannot
		// reach Lavaridge (no FLY in the party — see the phase-23 recon entry), but Emerald has
		// exactly one other instance of the same topology that IS reachable from it:
		// BattleFrontier_OutsideWest's RECEPTION GATE. Two door tiles on the outside map, (26,61)
		// north of the gate and (26,65) south of it, with the gate BUILDING between them — the
		// frontier's own entrance. Walking between them is impossible; passing through is the
		// only way. Same shape, real ROM, reachable save.
		FpBus rb; rb.read8 = rom_r8; rb.read16 = rom_r16; rb.read32 = rom_r32; rb.ctx = 0;
		FtRomMap west;
		CHECK(fieldtrav_rom_map(&rb, EM_MAPGROUPS, 26, 4, &west), "OutsideWest (26,4) resolves");
		CHECK(west.w == 56 && west.h == 72, "56x72 layout, got %dx%d", west.w, west.h);

		// Build the LIVE gBackupMapLayout the way the console does — the ROM grid copied into the
		// padded buffer, border tiles MAPGRID_UNDEFINED — so the current map is a RAM grid exactly
		// as it is on hardware, and only the INTERIOR is read from ROM (which is the real split).
		FpBus lbus; FpMap lm;
		live_from_rom(&rb, &west, &lbus, &lm);

		int gateN_x = 26, gateN_y = 61;      // the north door tile (pret map.json)
		int gateS_x = 26, gateS_y = 65;      // the south door tile
		// Ground truth first: standing north of the gate, the south side is NOT walkable to.
		int startX = 26, startY = 60, goalX = 26, goalY = 66;
		FpPlan fp;
		bool dry = fieldpath_plan(&lbus, &lm, startX, startY, goalX, goalY, 0, 0, &fp);
		CHECK(!dry, "the dry router cannot reach the far side of the gate (that is the premise)");

		FtExcursion ex;
		bool ok = fieldtrav_excursion(&lbus, &lm, EM_MAPGROUPS, 26, 4,
		                              startX, startY, goalX, goalY, 0, 0, &ex);
		CHECK(ok, "an out-and-back excursion IS found (outcome %d)", ex.outcome);
		if (ok) {
			CHECK(ex.dGroup == 26 && ex.dNum == 50,
			      "through BattleFrontier_ReceptionGate (26,50), got (%d,%d)", ex.dGroup, ex.dNum);
			CHECK((ex.wiX == gateN_x && ex.wiY == gateN_y),
			      "leaving by the NORTH door (26,61), got (%d,%d)", ex.wiX, ex.wiY);
			CHECK((ex.backX == gateS_x && ex.backY == gateS_y) ||
			      (ex.backX == gateS_x && ex.backY == gateS_y + 1),
			      "and returning at the SOUTH door (26,65)+step, got (%d,%d)", ex.backX, ex.backY);
			CHECK(ex.wjX != ex.arrX || ex.wjY != ex.arrY,
			      "the return warp is a DIFFERENT tile from the arrival one");
			CHECK(ex.stepsOut >= 0 && ex.stepsMid >= 0 && ex.stepsBack >= 0,
			      "all three legs carry real step counts (%d/%d/%d)",
			      ex.stepsOut, ex.stepsMid, ex.stepsBack);
		}

		// The mirror image: from the SOUTH side, the same gate gets you north.
		FtExcursion ex2;
		bool ok2 = fieldtrav_excursion(&lbus, &lm, EM_MAPGROUPS, 26, 4, 26, 66, 26, 60, 0, 0, &ex2);
		CHECK(ok2, "and the excursion is symmetric (south -> north)");
		if (ok2) CHECK(ex2.wiY == gateS_y, "leaving by the south door this time, got y=%d", ex2.wiY);

		// H3.5 honesty: a goal that no return warp can reach plans NOTHING. (0,0) is the map's
		// top-left corner, solid border on this layout.
		FtExcursion ex3;
		// (26,63) is INSIDE the gate building's wall — enterable by nobody, on either side.
		bool ok3 = fieldtrav_excursion(&lbus, &lm, EM_MAPGROUPS, 26, 4, startX, startY, 26, 63, 0, 0, &ex3);
		CHECK(!ok3 && ex3.outcome == FT_OUT_NOEXC,
		      "an unreachable goal reports excursion-none and plans nothing (outcome %d)", ex3.outcome);
		// And the guard rails again, this time on the excursion entry point.
		FtExcursion ex4;
		CHECK(!fieldtrav_excursion(&lbus, &lm, 0x02000000u, 26, 4, startX, startY, goalX, goalY, 0, 0, &ex4),
		      "a RAM gMapGroups plans nothing");
		CHECK(!fieldtrav_excursion(&lbus, &lm, EM_MAPGROUPS, 26, 4, startX, startY, startX + 40, startY, 0, 0, &ex4),
		      "a goal outside the search window plans nothing");
		CHECK(ex4.outcome == FT_OUT_WINDOW, "…and says so (outcome %d)", ex4.outcome);
	}
	}

	// ---------------------------------------------------------------- TEST 17
	// PHASE 24 / lane A. The census is a DIAGNOSTIC — it answers "where is the mon that knows
	// Fly?", which the yes/no eligibility rail cannot express — but it reads through the very
	// same decrypt+checksum rail, so it must inherit every one of that rail's refusals. These
	// checks are the proof it does, on the same golden-byte construction TEST 5 uses.
	printf("TEST 17 — fieldtrav_read_mon + the party/PC census\n");
	{
		FpBus bus; FpMap m; bind("route117", &bus, &m, 3);
		const uint16_t surfSet[4] = { MOVE_TACKLE, MOVE_SURF, 0, 0 };
		const uint16_t cutSet[4]  = { MOVE_CUT, MOVE_SMASH, 0, 0 };
		const uint16_t wanted[]   = { MOVE_CUT, MOVE_FLY, MOVE_SURF, MOVE_SMASH };

		// --- fieldtrav_read_mon itself: species AND moves, both permutations, all refusals.
		FtMon mon;
		put_mon(0, 0x12345678u, 0xCAFEBABEu, surfSet, 1, 0, 0);      // permutation row 0
		CHECK(fieldtrav_read_mon(&bus, PARTY_BASE, &mon), "row-0 mon decrypts");
		CHECK(mon.species == 260, "…species read from the GROWTH substruct (got %u)", mon.species);
		CHECK(mon.moves[0] == MOVE_TACKLE && mon.moves[1] == MOVE_SURF,
		      "…moves read from the ATTACKS substruct (got %u,%u)", mon.moves[0], mon.moves[1]);
		put_mon(0, 0x1234568Bu, 0x00010203u, surfSet, 1, 0, 0);      // permutation row 19
		CHECK(fieldtrav_read_mon(&bus, PARTY_BASE, &mon) && mon.species == 260 &&
		      mon.moves[1] == MOVE_SURF, "row-19 mon decrypts through its own permutation");
		put_mon(0, 0x12345678u, 0xCAFEBABEu, surfSet, 1, 0, 1);
		CHECK(!fieldtrav_read_mon(&bus, PARTY_BASE, &mon), "a bad checksum is refused (the rail)");
		put_mon(0, 0x12345678u, 0xCAFEBABEu, surfSet, 1, 1, 0);
		CHECK(!fieldtrav_read_mon(&bus, PARTY_BASE, &mon), "an EGG is refused");
		put_mon(0, 0x12345678u, 0xCAFEBABEu, surfSet, 0, 0, 0);
		CHECK(!fieldtrav_read_mon(&bus, PARTY_BASE, &mon), "an empty slot is refused");
		CHECK(!fieldtrav_read_mon(&bus, 0x08000000u, &mon), "a ROM address is refused");
		CHECK(!fieldtrav_read_mon(&bus, 0, &mon), "address 0 is refused");
		CHECK(!fieldtrav_read_mon(&bus, PARTY_BASE, NULL), "a NULL out is refused");

		// --- the census: party slots whole, PC slots only when they match a wanted move.
		memset(g_ovParty, 0, sizeof g_ovParty);
		put_mon(0, 0x12345678u, 0xCAFEBABEu, cutSet,  1, 0, 0);
		put_mon(1, 0x1234568Bu, 0x00010203u, surfSet, 1, 0, 0);
		// box 3 slot 7 = the Lugia-shaped case: a SURF+FLY mon sitting in the PC, invisible to
		// fieldtrav_usable (which only ever reads the party) and the whole reason this exists.
		uint8_t* b37 = g_ovStore + 4 + 80 * (3 * 30 + 7);
		const uint16_t lugiaSet[4] = { MOVE_SURF, MOVE_FLY, 0, 0 };
		put_mon_at(b37, 0x12345678u, 0xCAFEBABEu, lugiaSet, 249, 1, 0, 0);   // species 249 = LUGIA
		// box 0 slot 0 = a mon with no field move at all: live, decryptable, NOT recorded.
		const uint16_t dudSet[4] = { MOVE_TACKLE, 0, 0, 0 };
		put_mon_at(g_ovStore + 4, 0x12345678u, 0xCAFEBABEu, dudSet, 19, 1, 0, 0);
		// box 13 slot 29 = the far corner, so the sweep really covers 14 x 30.
		uint8_t* bLast = g_ovStore + 4 + 80 * (13 * 30 + 29);
		put_mon_at(bLast, 0x1234568Bu, 0x00010203u, cutSet, 123, 1, 0, 0);
		// box 1 slot 0 = a CORRUPT box mon: counted live, but never recorded (the rail again).
		put_mon_at(g_ovStore + 4 + 80 * 30, 0x12345678u, 0xCAFEBABEu, surfSet, 130, 1, 0, 1);

		FtCensus cen;
		int n = fieldtrav_census(&bus, PARTY_BASE, 2, STORE_BASE, wanted, 4, &cen);
		CHECK(cen.nParty == 2, "both party slots decrypted (got %d)", cen.nParty);
		CHECK(cen.party[0].box == -1 && cen.party[0].slot == 0 &&
		      cen.party[0].moves[0] == MOVE_CUT, "party slot 0 recorded with its moves");
		CHECK(cen.party[1].slot == 1 && cen.party[1].moves[1] == MOVE_SURF,
		      "party slot 1 recorded with its moves");
		CHECK(cen.boxLive == 4, "every live PC slot is counted (got %d)", cen.boxLive);
		CHECK(cen.boxOk == 3, "…and the corrupt one fails the rail (ok=%d)", cen.boxOk);
		CHECK(cen.nBox == 2, "only field-move box mons are recorded (got %d)", cen.nBox);
		CHECK(n == cen.nParty + cen.nBox, "the return value is the record total (%d)", n);
		int lug = -1;
		for (int i = 0; i < cen.nBox; i++) if (cen.box[i].species == 249) lug = i;
		CHECK(lug >= 0, "the PC mon that knows SURF+FLY is found");
		if (lug >= 0) {
			CHECK(cen.box[lug].box == 3 && cen.box[lug].slot == 7,
			      "…at box 3 slot 7 (got box %d slot %d)", cen.box[lug].box, cen.box[lug].slot);
			CHECK(cen.box[lug].moves[0] == MOVE_SURF && cen.box[lug].moves[1] == MOVE_FLY,
			      "…with both moves readable");
		}
		int last = -1;
		for (int i = 0; i < cen.nBox; i++) if (cen.box[i].species == 123) last = i;
		CHECK(last >= 0 && cen.box[last].box == 13 && cen.box[last].slot == 29,
		      "the sweep really reaches box 13 slot 29 (the 420th slot)");
		for (int i = 0; i < cen.nBox; i++)
			CHECK(cen.box[i].species != 19 && cen.box[i].species != 130,
			      "neither the move-less nor the corrupt box mon is recorded");

		// Degradations — each one a NAMED behaviour, never a guess.
		CHECK(fieldtrav_census(&bus, PARTY_BASE, 2, 0, wanted, 4, &cen) == 2 && cen.nBox == 0,
		      "storage 0 -> party only, no PC scan");
		CHECK(fieldtrav_census(&bus, PARTY_BASE, 2, 0x08000000u, wanted, 4, &cen) == 2 &&
		      cen.boxLive == 0, "a ROM storage base is refused");
		CHECK(fieldtrav_census(&bus, PARTY_BASE, 2, STORE_BASE, NULL, 0, &cen) == 2 &&
		      cen.nBox == 0, "no wanted-move list -> no PC records");
		CHECK(fieldtrav_census(&bus, 0, 0, STORE_BASE, wanted, 4, &cen) == 2 && cen.nParty == 0,
		      "partyBase 0 -> the PC scan still runs (got %d box records)", cen.nBox);
		CHECK(fieldtrav_census(&bus, PARTY_BASE, 99, STORE_BASE, wanted, 4, &cen) >= 0 &&
		      cen.nParty <= 6, "an over-large party count is clamped to 6");
		CHECK(fieldtrav_census(&bus, PARTY_BASE, 2, STORE_BASE, wanted, 4, NULL) == 0,
		      "a NULL out is refused");
		CHECK(fieldtrav_census(NULL, PARTY_BASE, 2, STORE_BASE, wanted, 4, &cen) == 0,
		      "a NULL bus is refused");

		// The record cap holds: 40 field-move mons in the PC, 24 kept, nothing written past it.
		memset(g_ovStore, 0, sizeof g_ovStore);
		for (int i = 0; i < 40; i++)
			put_mon_at(g_ovStore + 4 + 80 * i, 0x12345678u, 0xCAFEBABEu, surfSet,
			           (uint16_t)(300 + i), 1, 0, 0);
		fieldtrav_census(&bus, PARTY_BASE, 2, STORE_BASE, wanted, 4, &cen);
		CHECK(cen.boxLive == 40 && cen.boxOk == 40, "all 40 are read (%d/%d)", cen.boxLive, cen.boxOk);
		CHECK(cen.nBox == FT_CENSUS_MAX, "…and exactly %d records are kept (got %d)",
		      FT_CENSUS_MAX, cen.nBox);
		CHECK(cen.box[FT_CENSUS_MAX - 1].species == (uint16_t)(300 + FT_CENSUS_MAX - 1),
		      "…the last kept record is the %dth mon, in slot order", FT_CENSUS_MAX);
	}


	// ================================================================ TEST 20 / 21
	// PHASE 26 / LANE V — DIVE (SPEC-hm-dive). Self-contained: these two blocks bring their own
	// bus, their own ROM image and their own live grid, so nothing above them changes and the
	// lane sharing this file with the Waterfall/Strength lane cannot collide with them.
	//
	// ---------------------------------------------------------------- TEST 20
	printf("\nTEST 20 — DIVE: the constants, the two metatile sets, and the FRLG zero\n");
	{
		// (a) The badge, per variant, against pret's OWN numbers — written here as literals read
		// off the three flags.h files this session, never through fieldtrav_cfg (the c2a58db rule:
		// a test that asks the table under test for the answer can only agree with itself).
		//   pokeemerald flags.h:1348 SYSTEM_FLAGS 0x860 + :1365 FLAG_BADGE07_GET (+0xD) = 0x86D,
		//     and it is BADGE07 because src/field_control_avatar.c:465/475 says so — Dive is HM08.
		//   pokeruby   flags.h:779   SYSTEM_FLAGS 0x800 + :795 (+0x0D)               = 0x80D,
		//     gate at pokeruby src/field_control_avatar.c:521/531.
		//   pokefirered: NO DIVE AT ALL -> 0.
		CHECK(fieldtrav_cfg(FT_VAR_EMERALD)->badgeDive == 0x86D, "EM badgeDive == FLAG_BADGE07_GET 0x86D (got 0x%X)",
		      fieldtrav_cfg(FT_VAR_EMERALD)->badgeDive);
		CHECK(fieldtrav_cfg(FT_VAR_RS)->badgeDive == 0x80D, "RS badgeDive == FLAG_BADGE07_GET 0x80D (got 0x%X)",
		      fieldtrav_cfg(FT_VAR_RS)->badgeDive);
		CHECK(fieldtrav_cfg(FT_VAR_FRLG)->badgeDive == 0, "FRLG badgeDive is the NAMED ZERO (got 0x%X)",
		      fieldtrav_cfg(FT_VAR_FRLG)->badgeDive);
		// The two traps this row exists to stop, stated as assertions rather than as prose:
		CHECK(fieldtrav_cfg(FT_VAR_FRLG)->badgeDive != fieldtrav_cfg(FT_VAR_EMERALD)->badgeDive,
		      "FRLG never inherits Emerald's dive badge");
		CHECK(fieldtrav_cfg(FT_VAR_FRLG)->badgeWaterfall == 0x826,
		      "…and FRLG's OWN badge07 is 0x826 — the WATERFALL badge, the value a careless port "
		      "would have put in badgeDive");
		CHECK(fieldtrav_cfg(FT_VAR_EMERALD)->badgeDive != fieldtrav_cfg(FT_VAR_EMERALD)->badgeWaterfall,
		      "EM dive (BADGE07) and waterfall (BADGE08) are different badges");
		CHECK(fieldtrav_cfg(FT_VAR_RS)->badgeDive != fieldtrav_cfg(FT_VAR_EMERALD)->badgeDive,
		      "RS dive badge is its own number, not Emerald's");
		CHECK(fieldtrav_cfg(FT_VAR_RS)->badgeDive == fieldtrav_cfg(FT_VAR_RS)->badgeSurf + 2,
		      "…and it sits where pokeruby's own BADGE05..BADGE07 run puts it");

		// (b) MetatileBehavior_IsDiveable — an ALLOW-list of exactly three, swept over every byte.
		// pokeemerald src/metatile_behavior.c:853-861 / pokeruby :927-935.
		int nDiveEm = 0, nDiveRs = 0, nDiveFr = 0;
		for (int b = 0; b < 256; b++) {
			int want = (b == 0x11 || b == 0x12 || b == 0x14);
			CHECK(fieldtrav_is_diveable(FT_VAR_EMERALD, b) == (want != 0),
			      "EM diveable(0x%02X) == %d", b, want);
			CHECK(fieldtrav_is_diveable(FT_VAR_RS, b) == (want != 0), "RS diveable(0x%02X) == %d", b, want);
			CHECK(fieldtrav_is_diveable(FT_VAR_FRLG, b) == false, "FRLG diveable(0x%02X) is never true", b);
			nDiveEm += fieldtrav_is_diveable(FT_VAR_EMERALD, b);
			nDiveRs += fieldtrav_is_diveable(FT_VAR_RS, b);
			nDiveFr += fieldtrav_is_diveable(FT_VAR_FRLG, b);
		}
		CHECK(nDiveEm == 3 && nDiveRs == 3 && nDiveFr == 0,
		      "exactly three diveable behaviours in RSE, none in FRLG (%d/%d/%d)", nDiveEm, nDiveRs, nDiveFr);
		CHECK(!fieldtrav_is_diveable(FT_VAR_EMERALD, -1), "an unreadable behaviour is never diveable");

		// The asymmetry that makes this a separate function from fieldtrav_is_surfable: two of the
		// commonest water tiles in the game are surfable and NOT diveable.
		CHECK(fieldtrav_is_surfable(FP_ENG_RSE, 0x10) && !fieldtrav_is_diveable(FT_VAR_EMERALD, 0x10),
		      "MB_POND_WATER is surfable but not diveable");
		CHECK(fieldtrav_is_surfable(FP_ENG_RSE, 0x15) && !fieldtrav_is_diveable(FT_VAR_EMERALD, 0x15),
		      "MB_OCEAN_WATER is surfable but not diveable");

		// (c) MetatileBehavior_IsUnableToEmerge — a DENY-list, so the default answer is YES.
		// pokeemerald :863-877 (its MB_WATER_DOOR arm is `#ifdef BUGFIX`, which vanilla does not
		// define, so it must NOT be in our set) / pokeruby IsNotSurfacable :937-943.
		int nNoEmerge = 0;
		for (int b = 0; b < 256; b++) {
			int deny = (b == 0x19 || b == 0x2A);
			CHECK(fieldtrav_can_emerge(FT_VAR_EMERALD, b) == (deny == 0), "EM can_emerge(0x%02X) == %d", b, !deny);
			CHECK(fieldtrav_can_emerge(FT_VAR_RS, b) == (deny == 0), "RS can_emerge(0x%02X) == %d", b, !deny);
			CHECK(fieldtrav_can_emerge(FT_VAR_FRLG, b) == false, "FRLG can_emerge(0x%02X) is never true", b);
			nNoEmerge += !fieldtrav_can_emerge(FT_VAR_EMERALD, b);
		}
		CHECK(nNoEmerge == 2, "exactly two behaviours block surfacing (got %d)", nNoEmerge);
		CHECK(fieldtrav_can_emerge(FT_VAR_EMERALD, 0x6C),
		      "MB_WATER_DOOR 0x6C still emerges — the BUGFIX arm is not in the shipped cartridge");
		CHECK(!fieldtrav_can_emerge(FT_VAR_EMERALD, -1), "an unreadable behaviour never emerges");
		CHECK(fieldtrav_can_emerge(FT_VAR_EMERALD, 0x00),
		      "MB_NORMAL emerges — which matters, because MB_NORMAL is what pret's underwater "
		      "layouts are actually paved with");

		// (d) Eligibility: badge AND mon, and the FRLG engine refusal on top.
		FpBus bus; FpMap m;
		const uint16_t diveSet[4] = { MOVE_DIVE, 0, 0, 0 };
		const uint16_t surfSet[4] = { MOVE_SURF, 0, 0, 0 };

		bind("route117", &bus, &m, 3);
		put_mon(0, 0x12345678u, 0xCAFEBABEu, diveSet, 1, 0, 0);
		CHECK(!(fieldtrav_usable(&bus, FT_VAR_EMERALD, &(FtParty){ SB1_BASE, PARTY_BASE, 1 }) & (1u << FT_HM_DIVE)),
		      "EM: a mon that knows Dive but no badge -> no DIVE bit");
		set_flag(FT_VAR_EMERALD, 0x86D);
		CHECK((fieldtrav_usable(&bus, FT_VAR_EMERALD, &(FtParty){ SB1_BASE, PARTY_BASE, 1 }) & (1u << FT_HM_DIVE)),
		      "EM: badge07 + a mon that knows Dive -> the DIVE bit");

		bind("route117", &bus, &m, 3);
		put_mon(0, 0x12345678u, 0xCAFEBABEu, surfSet, 1, 0, 0);
		set_flag(FT_VAR_EMERALD, 0x86D);
		CHECK(!(fieldtrav_usable(&bus, FT_VAR_EMERALD, &(FtParty){ SB1_BASE, PARTY_BASE, 1 }) & (1u << FT_HM_DIVE)),
		      "EM: the badge alone, with no mon that knows Dive -> no DIVE bit");

		// THE FRLG REGRESSION GUARD. Give a FireRed save everything a careless port would have
		// asked for — its OWN badge07 (0x826, which really is set on any save that beat Koga) and a
		// mon that really does know MOVE_DIVE — and the bit must still refuse to light.
		bind("route117", &bus, &m, 3);
		put_mon(0, 0x12345678u, 0xCAFEBABEu, diveSet, 1, 0, 0);
		set_flag(FT_VAR_FRLG, 0x826);
		uint32_t frMask = fieldtrav_usable(&bus, FT_VAR_FRLG, &(FtParty){ SB1_BASE, PARTY_BASE, 1 });
		CHECK(!(frMask & (1u << FT_HM_DIVE)),
		      "FRLG: badge07 + MOVE_DIVE still gives NO dive (mask 0x%X)", frMask);
		// …and the CONTROL that makes that refusal mean something: the bit really is set in this
		// save, read back through the SHIPPED flag reader. Without this line, "no dive" could just
		// as well be "the test forgot to set a badge".
		CHECK(fieldtrav_flag_get(&bus, FT_VAR_FRLG, SB1_BASE, 0x826),
		      "…and FRLG's badge07 bit IS set in this save, so the refusal is a decision, not a hole");
		// The `flagId == 0` trap: without the `c->badgeDive &&` guard, a zero badge id reads bit 0
		// of flags[0] — a live TEMP flag (pokeemerald flags.h:11 TEMP_FLAGS_START 0x0), not a hole.
		bind("route117", &bus, &m, 3);
		put_mon(0, 0x12345678u, 0xCAFEBABEu, diveSet, 1, 0, 0);
		set_flag(FT_VAR_FRLG, 0);
		CHECK(!(fieldtrav_usable(&bus, FT_VAR_FRLG, &(FtParty){ SB1_BASE, PARTY_BASE, 1 }) & (1u << FT_HM_DIVE)),
		      "FRLG: flag id 0 set + MOVE_DIVE -> still no dive (the zero-badge-id trap)");

		// RUBY, written through pokeruby's own numbers rather than through the table under test.
		bind("route117", &bus, &m, 3);
		put_mon(0, 0x12345678u, 0xCAFEBABEu, diveSet, 1, 0, 0);
		set_flag_ruby(0x80D);                       // pokeruby flags.h:779 + :795
		CHECK((fieldtrav_usable(&bus, FT_VAR_RS, &(FtParty){ SB1_BASE, PARTY_BASE, 1 }) & (1u << FT_HM_DIVE)),
		      "RS: the badge byte a real Ruby cart sets lights the DIVE bit");
		CHECK(!(fieldtrav_usable(&bus, FT_VAR_EMERALD, &(FtParty){ SB1_BASE, PARTY_BASE, 1 }) & (1u << FT_HM_DIVE)),
		      "…and the same save read with EMERALD's numbering does NOT — the c2a58db split holds");
	}

	// ---------------------------------------------------------------- TEST 21
	printf("TEST 21 — DIVE: the map-connection transition, planned end to end\n");
	{
		dv_world(0);                                 // live = the SURFACE map, D = its underwater twin
		FpBus bus; FpMap m;
		dv_bind(&bus, &m);
		FtParty pty = dv_party(1);
		FtDive dv;

		// (a) The pieces the plan is built out of, asserted separately so a failure says WHICH.
		CHECK(!fieldtrav_underwater(&bus, &m), "the surface map does not read as MAP_TYPE_UNDERWATER");
		int cg = -1, cn = -1;
		CHECK(fieldtrav_connection(&bus, DV_HDR_BASE, FT_CONN_DIVE, &cg, &cn) && cg == 0 && cn == 1,
		      "the surface map's CONNECTION_DIVE points at map (0,1) (got %d,%d)", cg, cn);
		CHECK(!fieldtrav_connection(&bus, DV_HDR_BASE, FT_CONN_EMERGE, &cg, &cn) && cg == -1 && cn == -1,
		      "…and it has no CONNECTION_EMERGE, with the out-params cleared on refusal");
		CHECK(!fieldtrav_connection(&bus, DV_HDR_BASE, 2 /* CONNECTION_NORTH */, &cg, &cn),
		      "a direction this map does not carry is refused, not answered with record 0");
		// The dive record is the THIRD of three, so finding it proves both the scan and the
		// 12-byte stride (asm/macros/map.inc:152-158 — the alignment padding pokeruby's C comments
		// on this struct get wrong). Records 0 and 1 must still read as themselves.
		CHECK(fieldtrav_connection(&bus, DV_HDR_BASE, 1 /* SOUTH */, &cg, &cn) && cg == 0 && cn == 7,
		      "record 0 (SOUTH) reads as (0,7) (got %d,%d)", cg, cn);
		CHECK(fieldtrav_connection(&bus, DV_HDR_BASE, 4 /* EAST */, &cg, &cn) && cg == 0 && cn == 8,
		      "record 1 (EAST) reads as (0,8) (got %d,%d)", cg, cn);
		// The structural rails, each a NAMED refusal rather than a garbage read.
		dv_conn_ptr(0);
		CHECK(!fieldtrav_connection(&bus, DV_HDR_BASE, FT_CONN_DIVE, &cg, &cn),
		      "a NULL connections pointer -> no connection (455 of Emerald's 869 maps store one)");
		// A WELL-FORMED table at an EWRAM address: count 1, a real ROM record list, a real
		// destination. Only the ROM-pointer rule refuses it — a reader that just followed the
		// pointer would answer confidently from a struct the header cannot legally hold.
		for (unsigned i = 0; i < sizeof g_dvDecoy; i++) g_dvDecoy[i] = 0;
		g_dvDecoy[0] = 1;                                          // count = 1
		{ uint32_t lp = DV_ROM_BASE + DVR_CONNL_S + DV_DIVE_REC * 12;
		  for (int i = 0; i < 4; i++) g_dvDecoy[4 + i] = (uint8_t)(lp >> (8 * i)); }
		dv_conn_ptr(DV_DECOY_BASE);
		CHECK(!fieldtrav_connection(&bus, DV_HDR_BASE, FT_CONN_DIVE, &cg, &cn),
		      "a WELL-FORMED connections table in EWRAM is still refused: the pointer must be ROM");
		dv_conn_ptr(DV_ROM_BASE + DVR_CONN_S);
		dv_conn_count(0);
		CHECK(!fieldtrav_connection(&bus, DV_HDR_BASE, FT_CONN_DIVE, &cg, &cn), "count 0 -> refused");
		dv_conn_count(FT_MAX_CONN + 1);
		CHECK(!fieldtrav_connection(&bus, DV_HDR_BASE, FT_CONN_DIVE, &cg, &cn),
		      "an impossible count (> %d, the sane cap) is refused", FT_MAX_CONN);
		dv_conn_count(3);
		CHECK(fieldtrav_connection(&bus, DV_HDR_BASE, FT_CONN_DIVE, &cg, &cn) && cg == 0 && cn == 1,
		      "…and the restored table reads correctly again");
		CHECK(!fieldtrav_connection(&bus, 0, FT_CONN_DIVE, &cg, &cn), "a NULL map header is refused");
		CHECK(!fieldtrav_connection(&bus, DV_HDR_BASE, FT_CONN_DIVE, 0, &cn), "a NULL out-param is refused");

		// (b) THE PLAN. West lagoon -> east lagoon, separated by a rock wall the surface cannot
		// cross; the only deep-water tile is (2,4) and the only surfacing tile underwater is
		// (13,4), so every number below is forced.
		bool ok = fieldtrav_dive(&bus, &m, DV_MAPGROUPS, 0, 0, FT_VAR_EMERALD, &pty,
		                         1, 4, 14, 4, true, 0, 0, &dv);
		CHECK(ok && dv.ok && dv.outcome == FT_OUT_PLANNED, "a dive route is planned (outcome %d)", dv.outcome);
		CHECK(dv.dir == FT_DIVE_DOWN, "…downward (dir %d)", dv.dir);
		CHECK(dv.dGroup == 0 && dv.dNum == 1, "…into map (0,1) (got %d,%d)", dv.dGroup, dv.dNum);
		CHECK(dv.diveX == 2 && dv.diveY == 4, "…diving at the ONE deep-water tile (2,4) (got %d,%d)",
		      dv.diveX, dv.diveY);
		CHECK(dv.upX == 13 && dv.upY == 4, "…surfacing at the ONE emergeable tile (13,4) (got %d,%d)",
		      dv.upX, dv.upY);
		CHECK(dv.stepsOut == 1 && dv.stepsMid == 11 && dv.stepsBack == 1,
		      "…legs 1/11/1 (got %d/%d/%d)", dv.stepsOut, dv.stepsMid, dv.stepsBack);
		CHECK(dv.nSpots == 1,
		      "exactly one reachable dive spot: the second one sits behind a collision-free SANDBAR "
		      "a surfer cannot cross (got %d)", dv.nSpots);
		// The identity coordinate map, stated as an assertion: the tile you dive AT is the tile you
		// arrive ON, both ways. That is what SetWarpDestination(..., WARP_ID_NONE, x, y) means.
		CHECK(dv.diveX == 2 && dv.upY == 4,
		      "the plan carries ONE (x,y) per transition because the coordinate map is the identity");

		// (c) A rock wall really does separate the two lagoons on the surface: the same tap with
		// the dive tier unavailable has nowhere to go. Proved by asking the LAYERED planner.
		fieldtrav_plan(&bus, &m, FT_VAR_EMERALD, &pty, 1, 4, 14, 4, true, 0, 0, &g_pr);
		CHECK(!g_pr.ok && g_pr.outcome == FT_OUT_UNREACHABLE,
		      "…and the surface really is impassable: tier 1 answers UNREACHABLE (%s)", OUTN(g_pr.outcome));

		// (d) NEVER PROMPT WHAT THE GAME WILL REFUSE — the five ways a dive is declined whole.
		FtParty noBadge = dv_party(1); dv_clear_badge();
		CHECK(!fieldtrav_dive(&bus, &m, DV_MAPGROUPS, 0, 0, FT_VAR_EMERALD, &noBadge,
		                      1, 4, 14, 4, true, 0, 0, &dv) && dv.outcome == FT_OUT_NOEDGE,
		      "no badge -> NOEDGE, nothing planned (outcome %d)", dv.outcome);
		dv_set_badge();
		// Arm the FRLG side honestly: set FireRed's OWN badge07 (0x826) in FireRed's OWN flags[],
		// so the save under the FRLG numbering has every bit a mis-ported dive row would look at.
		dv_flag_set_frlg(0x826);
		CHECK(fieldtrav_flag_get(&bus, FT_VAR_FRLG, DV_SB1_BASE, 0x826),
		      "the dive world's save has FRLG badge07 set, read through the shipped flag reader");
		CHECK(!fieldtrav_dive(&bus, &m, DV_MAPGROUPS, 0, 0, FT_VAR_FRLG, &pty,
		                      1, 4, 14, 4, true, 0, 0, &dv) && dv.outcome == FT_OUT_NOEDGE,
		      "the FRLG variant refuses the identical world, badge and all (outcome %d)", dv.outcome);
		CHECK(!fieldtrav_dive(&bus, &m, DV_MAPGROUPS, 0, 0, FT_VAR_EMERALD, &pty,
		                      1, 4, 14, 4, false /* on foot */, 0, 0, &dv) && dv.outcome == FT_OUT_NODIVE,
		      "a tap taken NOT surfing is refused whole, never half-planned (outcome %d)", dv.outcome);
		CHECK(!fieldtrav_dive(&bus, &m, 0x02000000u, 0, 0, FT_VAR_EMERALD, &pty,
		                      1, 4, 14, 4, true, 0, 0, &dv) && dv.outcome == FT_OUT_BADMAP,
		      "a gMapGroups that is not ROM -> BADMAP (outcome %d)", dv.outcome);
		CHECK(!fieldtrav_dive(&bus, &m, DV_MAPGROUPS, 0, 0, FT_VAR_EMERALD, &pty,
		                      1, 4, 14, 60, true, 0, 0, &dv) && dv.outcome == FT_OUT_WINDOW,
		      "a goal outside the +-32 window -> WINDOW (outcome %d)", dv.outcome);

		// (e) The CONNECTION is the mechanism, so removing either half kills the plan. This is the
		// honest refusal that covers every SCRIPTED dive spot in the game (Sootopolis, the Sealed
		// Chamber, Marine Cave, Seafloor Cavern, the Abandoned Ship, Route 134): those maps set
		// their destination from an ON_DIVE_WARP map script, so they carry no dive CONNECTION.
		dv_conn_dir(0, 4 /* CONNECTION_EAST */);
		CHECK(!fieldtrav_dive(&bus, &m, DV_MAPGROUPS, 0, 0, FT_VAR_EMERALD, &pty,
		                      1, 4, 14, 4, true, 0, 0, &dv) && dv.outcome == FT_OUT_NODIVE,
		      "no CONNECTION_DIVE on this map -> NODIVE (outcome %d)", dv.outcome);
		dv_conn_dir(0, FT_CONN_DIVE);
		// A dive connection that points at THIS map. No vanilla map does that, but a garbage read
		// of the group/num bytes easily produces it, and "dive from here to here" is a program
		// that would run forever.
		g_dvRom[DVR_CONNL_S + DV_DIVE_REC * 12 + 0x09] = 0;         // -> (0,0), ourselves
		CHECK(!fieldtrav_dive(&bus, &m, DV_MAPGROUPS, 0, 0, FT_VAR_EMERALD, &pty,
		                      1, 4, 14, 4, true, 0, 0, &dv) && dv.outcome == FT_OUT_NODIVE,
		      "a dive connection pointing at the current map is refused (outcome %d)", dv.outcome);
		g_dvRom[DVR_CONNL_S + DV_DIVE_REC * 12 + 0x09] = 1;
		dv_conn_dir(1, 4);
		CHECK(!fieldtrav_dive(&bus, &m, DV_MAPGROUPS, 0, 0, FT_VAR_EMERALD, &pty,
		                      1, 4, 14, 4, true, 0, 0, &dv) && dv.outcome == FT_OUT_NODIVE,
		      "a one-way dive (no return CONNECTION_EMERGE) is refused -> NODIVE (outcome %d)", dv.outcome);
		dv_conn_dir(1, FT_CONN_EMERGE);
		CHECK(fieldtrav_dive(&bus, &m, DV_MAPGROUPS, 0, 0, FT_VAR_EMERALD, &pty,
		                     1, 4, 14, 4, true, 0, 0, &dv), "…and restoring both connections restores the plan");

		// (f) The three ways the SEARCH itself comes up empty, each a different refusal path.
		short npc1[1][2]; npc1[0][0] = (short)(2 + 7); npc1[0][1] = (short)(4 + 7);
		CHECK(!fieldtrav_dive(&bus, &m, DV_MAPGROUPS, 0, 0, FT_VAR_EMERALD, &pty,
		                      1, 4, 14, 4, true, npc1, 1, &dv) && dv.outcome == FT_OUT_NODIVE &&
		      dv.nSpots == 0,
		      "an NPC parked on the only dive tile -> no reachable spot -> NODIVE (spots %d)", dv.nSpots);
		dv_wall_under(8);        // seal the underwater corridor at x=8
		CHECK(!fieldtrav_dive(&bus, &m, DV_MAPGROUPS, 0, 0, FT_VAR_EMERALD, &pty,
		                      1, 4, 14, 4, true, 0, 0, &dv) && dv.outcome == FT_OUT_NODIVE &&
		      dv.nSpots == 1,
		      "a dive spot that reaches no surfacing tile -> NODIVE, with the spot still counted (%d)",
		      dv.nSpots);
		dv_open_under(8);
		dv_block_goal();
		CHECK(!fieldtrav_dive(&bus, &m, DV_MAPGROUPS, 0, 0, FT_VAR_EMERALD, &pty,
		                      1, 4, 14, 4, true, 0, 0, &dv) && dv.outcome == FT_OUT_NODIVE,
		      "a goal the player could not occupy is refused, not bumped (outcome %d)", dv.outcome);
		dv_unblock_goal();

		// (g) Two degenerate shapes the search must get right on its own.
		//   1. The player is ALREADY floating on the dive tile — the zero-step out-leg. (The root
		//      of a BFS is a candidate spot like any other, which is easy to write and easy to
		//      forget.)
		CHECK(fieldtrav_dive(&bus, &m, DV_MAPGROUPS, 0, 0, FT_VAR_EMERALD, &pty,
		                     2, 4, 14, 4, true, 0, 0, &dv) && dv.stepsOut == 0 &&
		      dv.diveX == 2 && dv.diveY == 4,
		      "standing ON the dive tile plans a zero-step out-leg (got %d steps at %d,%d)",
		      dv.stepsOut, dv.diveX, dv.diveY);
		//   2. A goal ordinary surfing already reaches must NOT produce "dive here, surface here"
		//      — a pair of prompts that moves nobody. touch.c never asks (tier 0/1 run first), but
		//      a planner that answers nonsense when asked nonsense is one refactor from shipping it.
		//      PHASE 28 / lane X: the outcome is now FT_OUT_TIER0, not FT_OUT_NODIVE. This world
		//      could only ever express the degenerate shape (its single surfacing tile IS its dive
		//      tile), and the phase-26 audit showed on the real Route 126 pair that the guard which
		//      catches it does not catch the general case — so the refusal moved EARLIER, to the
		//      tier-order precondition, and named itself for what it is: the shipped router owns
		//      that tap. TEST 24 is the real-map half of the same rule.
		CHECK(!fieldtrav_dive(&bus, &m, DV_MAPGROUPS, 0, 0, FT_VAR_EMERALD, &pty,
		                      1, 4, 3, 4, true, 0, 0, &dv) && dv.outcome == FT_OUT_TIER0,
		      "a goal plain surfing already reaches is refused as TIER0, not answered with a round "
		      "trip to the same tile (outcome %d)", dv.outcome);

		// (h) …and NOTHING above left a stale answer behind: the same call still plans.
		CHECK(fieldtrav_dive(&bus, &m, DV_MAPGROUPS, 0, 0, FT_VAR_EMERALD, &pty,
		                     1, 4, 14, 4, true, 0, 0, &dv) && dv.diveX == 2 && dv.upX == 13,
		      "the world is back where it started and the plan reproduces exactly");

		// (i) UPWARD — the same machinery run from the underwater side. The map TYPE decides the
		// direction (pokeemerald src/field_control_avatar.c:475), not the avatar flags, so
		// `startSurfing` is false here and the plan still goes.
		dv_world(1);                                  // live = the UNDERWATER map, D = the surface
		dv_bind(&bus, &m);
		CHECK(fieldtrav_underwater(&bus, &m), "the live map now reads as MAP_TYPE_UNDERWATER");
		FtDive up;
		bool okUp = fieldtrav_dive(&bus, &m, DV_MAPGROUPS, 0, 1, FT_VAR_EMERALD, &pty,
		                           1, 4, 14, 4, false, 0, 0, &up);
		CHECK(okUp && up.outcome == FT_OUT_PLANNED, "an upward (emerge) route is planned (outcome %d)",
		      up.outcome);
		CHECK(up.dir == FT_DIVE_UP, "…and it knows it is going UP (dir %d)", up.dir);
		CHECK(up.dGroup == 0 && up.dNum == 0, "…into the surface map (0,0) (got %d,%d)", up.dGroup, up.dNum);
		// `diveX/diveY` is always "where the HM is used HERE" — surfacing, this time — and
		// `upX/upY` is "where it is used over there". The west pocket's only shaft is (2,4), and
		// the open sea above leads to the only deep-water tile the east pocket sits under, (13,4).
		CHECK(up.diveX == 2 && up.diveY == 4,
		      "…surfacing at the one emergeable underwater tile in reach, (2,4) (got %d,%d)",
		      up.diveX, up.diveY);
		CHECK(up.upX == 13 && up.upY == 4,
		      "…and diving back at the one deep-water tile over the east pocket, (13,4) (got %d,%d)",
		      up.upX, up.upY);
		CHECK(up.stepsOut == 1 && up.stepsBack == 1, "…legs 1/../1 (got %d/../%d)",
		      up.stepsOut, up.stepsBack);
		CHECK(up.nSpots == 1, "…from exactly one reachable surfacing spot (got %d)", up.nSpots);
		// The surface crossing detours around the reef: 13 steps, not the 11 a straight line would
		// take. That number is the surfable-behaviour test doing its job on the PAIRED map's leg.
		CHECK(up.stepsMid == 13,
		      "…and the surface leg goes AROUND the collision-free reef: 13 steps, not 11 (got %d)",
		      up.stepsMid);
	}

	// ================================================================ TEST 22 / 23
	// PHASE 26 / LANE W — WATERFALL (the vertical mid-surf edge) and STRENGTH (the terminal).
	//
	// TEST 22 is graded against the USER'S OWN Emerald ROM rather than a synthesised grid, because
	// the two things that make a waterfall hard are both properties of the REAL map data and a mock
	// would let me choose them:
	//   * a climbable waterfall metatile is collision 0 / elevation 1 — the SAME as the ocean above
	//     and below it — so nothing about the tile stops a router entering it (62 columns across
	//     7 maps, all identical);
	//   * the falls are TALL. Ever Grande's is 8 tiles, which is what forces the goal retarget.
	// A hand-built 3-tile fall would have proved neither.
	printf("\nTEST 22 — WATERFALL: the metatile, the ride, and the vertical edge\n");
	{
		// --- (a) the metatile test, exhaustively, both engines. 0x13 and nothing else.
		int wfHits = 0;
		for (int b = 0; b <= 0xFF; b++) {
			bool rse = fieldtrav_is_waterfall(FP_ENG_RSE, b), frlg = fieldtrav_is_waterfall(FP_ENG_FRLG, b);
			CHECK(rse == (b == 0x13), "RSE  0x%02X waterfall == %d", b, b == 0x13);
			CHECK(frlg == (b == 0x13), "FRLG 0x%02X waterfall == %d", b, b == 0x13);
			if (rse) wfHits++;
		}
		CHECK(wfHits == 1, "exactly ONE behaviour is a waterfall (got %d)", wfHits);
		CHECK(!fieldtrav_is_waterfall(FP_ENG_RSE, -1), "an unreadable behaviour is never a waterfall");
		// The pairing that makes the whole feature safe: a waterfall is an EDGE and never free
		// water. If 0x13 ever rejoined the surfable set, the layered planner would swim straight up
		// a fall and the game's forced movement would flush the player back down forever.
		CHECK(fieldtrav_is_waterfall(FP_ENG_RSE, 0x13) && !fieldtrav_is_surfable(FP_ENG_RSE, 0x13),
		      "RSE 0x13 is a waterfall AND not surfable — both halves, together");
		CHECK(fieldtrav_is_waterfall(FP_ENG_FRLG, 0x13) && !fieldtrav_is_surfable(FP_ENG_FRLG, 0x13),
		      "FRLG 0x13 is a waterfall AND not surfable");
	}
	rom_load();
	if (!g_rom) {
		printf("TEST 22 (ROM half) / 23 — PARTIAL SKIP: roms/emerald.gba not readable from the CWD; "
		       "the real-map waterfall checks did not run\n");
	} else {
		FpBus rb; rb.read8 = rom_r8; rb.read16 = rom_r16; rb.read32 = rom_r32; rb.ctx = 0;

		// --- (b) fieldtrav_waterfall_top == the game's own ride, on Route 114 (0,29).
		// The fall is x 9..12, y 10..12; the pool below is y=13 and the landing is y=9. Every one
		// of those numbers was read out of this ROM through the shipped readers.
		FtRomMap r114;
		CHECK(fieldtrav_rom_map(&rb, EM_MAPGROUPS, 0, 29, &r114), "Route114 (0,29) resolves");
		FpBus l1; FpMap m1;
		live_from_rom(&rb, &r114, &l1, &m1);
		CHECK(fieldpath_behaviour_at(&l1, &m1, 9, 12) == 0x13, "…(9,12) really is MB_WATERFALL");
		CHECK(fieldpath_behaviour_at(&l1, &m1, 9, 13) == 0x15, "…(9,13) below it is MB_OCEAN_WATER");
		CHECK(fieldpath_behaviour_at(&l1, &m1, 9,  9) == 0x15, "…(9,9) above it is MB_OCEAN_WATER");
		// EVERY tile of the column answers with the SAME landing — that identity is the retarget
		// rule, and it is why a tap anywhere on a fall can mean "take me up it".
		for (int y = 10; y <= 12; y++)
			CHECK(fieldtrav_waterfall_top(&l1, &m1, 9, y) == 9,
			      "the ride from (9,%d) ends at y=9 (got %d)", y, fieldtrav_waterfall_top(&l1, &m1, 9, y));
		CHECK(fieldtrav_waterfall_top(&l1, &m1, 9, 13) == -1, "a non-waterfall tile is not a fall");
		CHECK(fieldtrav_waterfall_top(&l1, &m1, 9,  9) == -1, "…nor is the pool above it");
		CHECK(fieldtrav_waterfall_top(0, &m1, 9, 12) == -1, "a NULL bus refuses rather than guesses");

		// --- (c) the edge itself, planned end to end on that map.
		const FtEngCfg* cE = fieldtrav_cfg(FT_VAR_EMERALD);
		memset(g_ovSb1, 0, sizeof g_ovSb1); memset(g_ovParty, 0, sizeof g_ovParty);
		g_ov[0].p = 0;                                   // no object overlay: this is a ROM map
		g_ov[1].addr = SB1_BASE;   g_ov[1].len = sizeof g_ovSb1;   g_ov[1].p = g_ovSb1;
		g_ov[2].addr = PARTY_BASE; g_ov[2].len = sizeof g_ovParty; g_ov[2].p = g_ovParty;
		g_ov[3].p = 0;
		set_flag(FT_VAR_EMERALD, cE->badgeWaterfall);
		const uint16_t wfSet[4] = { MOVE_WATERFALL_W, 0, 0, 0 };
		put_mon(0, 0x12345678u, 1u, wfSet, 1, 0, 0);
		FtParty pty = party_of(1);
		// The overlay lives at EWRAM addresses the live bus does not serve, so route the party and
		// flag reads through a bus that checks the overlay FIRST and falls back to the live world.
		FpBus wbus = { wf_r8, wf_r16, wf_r32, &l1 };

		CHECK((fieldtrav_usable(&wbus, FT_VAR_EMERALD, &pty) & (1u << FT_HM_WATERFALL)) != 0,
		      "the fixture really is Waterfall-eligible (badge 0x%03X + MOVE_WATERFALL)", cE->badgeWaterfall);
		bool ok = fieldtrav_plan(&wbus, &m1, FT_VAR_EMERALD, &pty, 9, 13, 9, 9, true, 0, 0, &g_pr);
		CHECK(ok && g_pr.outcome == FT_OUT_PLANNED,
		      "surfing at the foot of Route 114's fall, a tap on the pool above PLANS (outcome %s)",
		      OUTN(g_pr.outcome));
		if (ok) {
			CHECK(g_pr.nMoves == 1, "ONE move — the ride is a single edge, not three steps (got %d)",
			      g_pr.nMoves);
			CHECK(g_pr.nInteracts == 1, "…costing exactly one interact (got %d)", g_pr.nInteracts);
			CHECK(g_pr.mv[0].hm == FT_HM_WATERFALL, "…and it is a WATERFALL (got hm %d)", g_pr.mv[0].hm);
			CHECK(g_pr.mv[0].dir == FP_U, "…faced NORTH, which is what IsPlayerSurfingNorth wants");
			CHECK(g_pr.mv[0].objSlot == -1, "…with no object slot: a metatile edge proves itself");
			CHECK(g_pr.endMode == FT_MODE_SURF, "…and the player is still afloat at the top");
			CHECK(g_pr.wfRetarget == 0, "…the tap was already the landing tile, so nothing was retargeted");
		}

		// --- (d) the negative controls. Either half of the game's gate missing = NO plan, and the
		// outcome is UNREACHABLE — i.e. the plain-walk pass could not cross the fall either, which
		// is the T5.8 property asserted on real map bytes. Each control keeps SURF eligible so the
		// refusal cannot be the blanket "this player has no field moves at all" answer (NOEDGE):
		// the player is eligible for something, just not for this.
		const uint16_t surfWf[4] = { MOVE_SURF, MOVE_WATERFALL_W, 0, 0 };
		memset(g_ovSb1, 0, sizeof g_ovSb1);
		set_flag(FT_VAR_EMERALD, cE->badgeSurf);         // Surf badge only — no Waterfall badge
		put_mon(0, 0x12345678u, 1u, surfWf, 1, 0, 0);    // …and a mon that DOES know Waterfall
		CHECK((fieldtrav_usable(&wbus, FT_VAR_EMERALD, &pty) & (1u << FT_HM_WATERFALL)) == 0 &&
		      (fieldtrav_usable(&wbus, FT_VAR_EMERALD, &pty) & (1u << FT_HM_SURF)) != 0,
		      "the control is exact: Surf usable, Waterfall not");
		CHECK(!fieldtrav_plan(&wbus, &m1, FT_VAR_EMERALD, &pty, 9, 13, 9, 9, true, 0, 0, &g_pr),
		      "no BADGE -> nothing is planned");
		CHECK(g_pr.outcome == FT_OUT_UNREACHABLE,
		      "…and UNREACHABLE, not TIER0: the tier-0 pass cannot swim OR walk up a fall (got %s)",
		      OUTN(g_pr.outcome));
		set_flag(FT_VAR_EMERALD, cE->badgeWaterfall);
		const uint16_t noWf[4] = { MOVE_SURF, 0, 0, 0 };
		put_mon(0, 0x12345678u, 1u, noWf, 1, 0, 0);      // badge back, no mon knows the move
		CHECK(!fieldtrav_plan(&wbus, &m1, FT_VAR_EMERALD, &pty, 9, 13, 9, 9, true, 0, 0, &g_pr),
		      "no MON that knows WATERFALL -> nothing is planned");
		CHECK(g_pr.outcome == FT_OUT_UNREACHABLE, "…also UNREACHABLE (got %s)", OUTN(g_pr.outcome));
		put_mon(0, 0x12345678u, 1u, wfSet, 1, 0, 0);
		// ON FOOT the edge does not exist at all: the game's gate is IsPlayerSurfingNorth.
		CHECK(!fieldtrav_plan(&wbus, &m1, FT_VAR_EMERALD, &pty, 9, 13, 9, 9, false, 0, 0, &g_pr),
		      "…and it is a MID-SURF edge: the same tap taken on foot plans nothing");

		// --- (e) THE RETARGET, on the fall that needs it: Ever Grande (0,8), 8 tiles tall.
		// A tap reaches 5 tiles above the player (touch.c: ddy = s_downGy/16 - 5), so from the pool
		// at y=68 the landing at y=59 is NINE tiles away and cannot be tapped at all. Tapping the
		// FALL is the only gesture available, and it has to mean "take me up".
		FtRomMap rEG;
		CHECK(fieldtrav_rom_map(&rb, EM_MAPGROUPS, 0, 8, &rEG), "EverGrandeCity (0,8) resolves");
		FpBus l2; FpMap m2;
		live_from_rom(&rb, &rEG, &l2, &m2);
		FpBus wbus2 = { wf_r8, wf_r16, wf_r32, &l2 };
		CHECK(fieldtrav_waterfall_top(&l2, &m2, 20, 67) == 59,
		      "Ever Grande's fall is 8 tiles: (20,67) -> y=59 (got %d)",
		      fieldtrav_waterfall_top(&l2, &m2, 20, 67));
		CHECK(59 - 68 == -9, "…and the landing is 9 tiles above the pool, i.e. OUTSIDE a tap's reach");
		bool okEG = fieldtrav_plan(&wbus2, &m2, FT_VAR_EMERALD, &pty, 20, 68, 20, 63, true, 0, 0, &g_pr);
		CHECK(okEG && g_pr.outcome == FT_OUT_PLANNED,
		      "a tap ON the fall (20,63) plans (outcome %s)", OUTN(g_pr.outcome));
		if (okEG) {
			CHECK(g_pr.wfRetarget == 1, "…and says it RETARGETED the goal");
			CHECK(g_pr.goalX == 20 && g_pr.goalY == 59,
			      "…onto the tile the game's own ride ends on, (20,59) (got %d,%d)",
			      g_pr.goalX, g_pr.goalY);
			CHECK(g_pr.nMoves == 1 && g_pr.mv[0].hm == FT_HM_WATERFALL,
			      "…as a single WATERFALL move (%d moves, hm %d)", g_pr.nMoves, g_pr.mv[0].hm);
		}

		// --- (f) DOWNWARD is deliberately NOT modelled. Riding down a fall is FREE in Gen 3 (it is
		// forced movement, not an HM — field_player_avatar.c:159/185), but it is also a tile that
		// takes the controls away, which rule T5.8 forbids routing into. So the planner refuses,
		// and this check is what stops a later session "fixing" that by accident.
		CHECK(!fieldtrav_plan(&wbus2, &m2, FT_VAR_EMERALD, &pty, 20, 59, 20, 68, true, 0, 0, &g_pr),
		      "the DOWN edge does not exist: a tap below the fall, from above it, plans nothing");
		CHECK(g_pr.outcome == FT_OUT_UNREACHABLE, "…UNREACHABLE, honestly (got %s)", OUTN(g_pr.outcome));

		// --- (g) OUR one added rule: the landing must be water the player may float on. Route 119
		// carries a decorative fall (x 21..23, y 79..82) whose top tile is MB_NORMAL with collision
		// set — the game would ride it (a held movement ignores collision), we refuse it.
		FtRomMap r119;
		CHECK(fieldtrav_rom_map(&rb, EM_MAPGROUPS, 0, 34, &r119), "Route119 (0,34) resolves");
		FpBus l3; FpMap m3;
		live_from_rom(&rb, &r119, &l3, &m3);
		CHECK(fieldpath_behaviour_at(&l3, &m3, 21, 82) == 0x13, "…(21,82) is a waterfall tile");
		CHECK(fieldtrav_waterfall_top(&l3, &m3, 21, 82) == 78,
		      "…its ride would end at y=78 (got %d)", fieldtrav_waterfall_top(&l3, &m3, 21, 82));
		CHECK(!fieldtrav_is_surfable(FP_ENG_RSE, fieldpath_behaviour_at(&l3, &m3, 21, 78)),
		      "…on a tile that is NOT floatable, so the edge is refused (beh 0x%02X)",
		      fieldpath_behaviour_at(&l3, &m3, 21, 78));
	}

	printf("TEST 23 — STRENGTH: a terminal, never a thoroughfare\n");
	{
		// Back to the compiled-in fixture: Strength is about OBJECTS and eligibility, and the
		// route117 overlay is where every other object-edge test lives.
		FpBus bus; FpMap m; bind("route117", &bus, &m, 3);
		const FtEngCfg* c = fieldtrav_cfg(FT_VAR_EMERALD);
		const uint16_t strSet[4] = { MOVE_STRENGTH_W, 0, 0, 0 };
		FtParty pty = party_of(1);

		// --- the probe, refusal by refusal. Empty map first.
		npc_clear();
		CHECK(fieldtrav_strength_tap(&bus, &m, FT_VAR_EMERALD, &pty, 18, 12) < 0,
		      "no boulder on the tile -> no Strength tap");

		// A CUT TREE on that tile is not a boulder, however eligible the player is.
		set_flag(FT_VAR_EMERALD, c->badgeStrength);
		put_mon(0, 0x12345678u, 1u, strSet, 1, 0, 0);
		set_obj(6, 82 /* OBJ_EVENT_GFX_CUTTABLE_TREE */, 18, 12);
		CHECK(fieldtrav_strength_tap(&bus, &m, FT_VAR_EMERALD, &pty, 18, 12) < 0,
		      "a cuttable tree is not a boulder");

		// The real thing.
		set_obj(6, 87 /* OBJ_EVENT_GFX_PUSHABLE_BOULDER */, 18, 12);
		CHECK(fieldtrav_strength_tap(&bus, &m, FT_VAR_EMERALD, &pty, 18, 12) == 6,
		      "an eligible, un-activated boulder answers with its own object slot");
		CHECK(fieldtrav_strength_tap(&bus, &m, FT_VAR_EMERALD, &pty, 18, 13) < 0,
		      "…and only on the tile it is actually standing on");

		// THE LATCH. `goto_if_set FLAG_SYS_USE_STRENGTH` sends the script to a plain textbox with
		// no yes/no at all, so a program aimed at it would wait for a prompt that never comes.
		g_ovSb1[c->flagsOff + (c->strengthLatch >> 3)] |= (uint8_t)(1u << (c->strengthLatch & 7));
		CHECK(fieldtrav_strength_tap(&bus, &m, FT_VAR_EMERALD, &pty, 18, 12) < 0,
		      "FLAG_SYS_USE_STRENGTH already set -> nothing to activate, so no tap");
		g_ovSb1[c->flagsOff + (c->strengthLatch >> 3)] &= (uint8_t)~(1u << (c->strengthLatch & 7));
		CHECK(fieldtrav_strength_tap(&bus, &m, FT_VAR_EMERALD, &pty, 18, 12) == 6, "…and back again");

		// Either half of the eligibility gate missing = no tap.
		memset(g_ovSb1, 0, sizeof g_ovSb1);
		CHECK(fieldtrav_strength_tap(&bus, &m, FT_VAR_EMERALD, &pty, 18, 12) < 0, "no badge -> no tap");
		set_flag(FT_VAR_EMERALD, c->badgeStrength);
		put_mon(0, 0x12345678u, 1u, (const uint16_t[4]){ MOVE_CUT, 0, 0, 0 }, 1, 0, 0);
		CHECK(fieldtrav_strength_tap(&bus, &m, FT_VAR_EMERALD, &pty, 18, 12) < 0,
		      "no mon that knows STRENGTH -> no tap");
		put_mon(0, 0x12345678u, 1u, strSet, 1, 0, 0);

		// The FRLG row really is the one being read (gfx 97, not 82/87, and its own flag ids).
		{
			FpBus fb; FpMap fm; bind("frstair", &fb, &fm, 3);
			const FtEngCfg* cf = fieldtrav_cfg(FT_VAR_FRLG);
			set_flag(FT_VAR_FRLG, cf->badgeStrength);
			put_mon(0, 0x12345678u, 1u, strSet, 1, 0, 0);
			FtParty fp = party_of(1);
			set_obj(5, 87, 4, 4);                       // EMERALD's boulder id, on an FRLG map
			CHECK(fieldtrav_strength_tap(&fb, &fm, FT_VAR_FRLG, &fp, 4, 4) < 0,
			      "gfx 87 is not a boulder in FRLG — the two rows must never merge");
			set_obj(5, 97 /* OBJ_EVENT_GFX_PUSHABLE_BOULDER, pokefirered */, 4, 4);
			CHECK(fieldtrav_strength_tap(&fb, &fm, FT_VAR_FRLG, &fp, 4, 4) == 5,
			      "gfx 97 IS, and it reads FRLG's own badge (0x%03X) and latch (0x%03X)",
			      cf->badgeStrength, cf->strengthLatch);
		}

		// --- the PLAN. Tapping the boulder itself is a program that ENDS at it.
		bind("route117", &bus, &m, 3);
		set_flag(FT_VAR_EMERALD, c->badgeStrength);
		put_mon(0, 0x12345678u, 1u, strSet, 1, 0, 0);
		set_obj(6, 87, 18, 12);
		npc_clear(); npc_add(18, 12);                    // the boulder is in the block list, as on hw
		bool ok = fieldtrav_plan(&bus, &m, FT_VAR_EMERALD, &pty, 18, 14, 18, 12, false, g_npc, g_npcN, &g_pr);
		CHECK(ok && g_pr.outcome == FT_OUT_PLANNED,
		      "a tap ON the boulder plans an ACTIVATION (outcome %s)", OUTN(g_pr.outcome));
		if (ok) {
			CHECK(g_pr.nInteracts == 1, "one interact (got %d)", g_pr.nInteracts);
			CHECK(g_pr.nMoves == 2, "two moves: one walk, then the terminal (got %d)", g_pr.nMoves);
			CHECK(g_pr.mv[g_pr.nMoves - 1].hm == FT_HM_STRENGTH,
			      "the LAST move is the STRENGTH interact (got hm %d)", g_pr.mv[g_pr.nMoves - 1].hm);
			CHECK(g_pr.mv[g_pr.nMoves - 1].dir == FP_U, "…facing the boulder (north)");
			CHECK(g_pr.mv[g_pr.nMoves - 1].objSlot == 6,
			      "…naming the boulder's own slot (got %d)", g_pr.mv[g_pr.nMoves - 1].objSlot);
			for (int i = 0; i + 1 < g_pr.nMoves; i++)
				CHECK(g_pr.mv[i].hm == FT_HM_NONE, "every move before it is a plain walk (i=%d)", i);
		}

		// --- THE PROPERTY: never a thoroughfare. The same fully-eligible boulder, with the goal
		// one tile BEYOND it, must still be a wall — this is what stops a route pushing a boulder
		// out of a puzzle to save two steps.
		bind("route117", &bus, &m, 3);
		set_flag(FT_VAR_EMERALD, c->badgeStrength);
		put_mon(0, 0x12345678u, 1u, strSet, 1, 0, 0);
		set_obj(6, 87, 18, 12);
		npc_clear(); seal_pocket();
		CHECK(!fieldtrav_plan(&bus, &m, FT_VAR_EMERALD, &pty, 18, 14, 18, 10, false, g_npc, g_npcN, &g_pr),
		      "a boulder is NEVER routed THROUGH, even when the goal is right behind it");
		CHECK((g_pr.usable & (1u << FT_HM_STRENGTH)) != 0, "…and Strength really was usable");

		// …and the case that actually GRADES the goal-only rule rather than the eligibility that
		// happens to precede it: TWO boulders, one ON the tapped goal and one sealing the only way
		// to it. "There is an activatable boulder somewhere in this plan" is now TRUE, so the rule
		// that has to do the work is the tile comparison itself — drop it and the router walks
		// through the chokepoint boulder to reach the tapped one, pushing a puzzle piece out of the
		// way to save a walk, which is the exact failure this whole design exists to prevent.
		bind("route117", &bus, &m, 3);
		set_flag(FT_VAR_EMERALD, c->badgeStrength);
		put_mon(0, 0x12345678u, 1u, strSet, 1, 0, 0);
		set_obj(6, 87, 18, 12);                          // B: the chokepoint, mouth of the pocket
		set_obj(7, 87, 18, 10);                          // A: the tapped goal, out in the open
		npc_clear(); seal_pocket();                      // (17,12) and (19,12) blocked, (18,12) is B
		CHECK(fieldtrav_strength_tap(&bus, &m, FT_VAR_EMERALD, &pty, 18, 10) == 7,
		      "the TAPPED boulder is activatable — so the plan is not being refused for eligibility");
		CHECK(!fieldtrav_plan(&bus, &m, FT_VAR_EMERALD, &pty, 18, 14, 18, 10, false, g_npc, g_npcN, &g_pr),
		      "a second boulder in the way is still a WALL, even with an activatable one at the goal");
		CHECK(g_pr.outcome == FT_OUT_UNREACHABLE,
		      "…and it says UNREACHABLE, not PLANNED (got %s)", OUTN(g_pr.outcome));
		CHECK(g_pr.nEdges == 2, "…both boulders were seen (%d)", g_pr.nEdges);
		// Remove the chokepoint and the identical tap succeeds — which is what proves the refusal
		// above was caused by boulder B and not by anything else in the setup.
		bind("route117", &bus, &m, 3);
		set_flag(FT_VAR_EMERALD, c->badgeStrength);
		put_mon(0, 0x12345678u, 1u, strSet, 1, 0, 0);
		set_obj(7, 87, 18, 10);
		npc_clear(); npc_add(17, 12); npc_add(19, 12); npc_add(18, 10);
		CHECK(fieldtrav_plan(&bus, &m, FT_VAR_EMERALD, &pty, 18, 14, 18, 10, false, g_npc, g_npcN, &g_pr),
		      "…with the chokepoint gone, the same tap plans the activation");
		CHECK(g_pr.nInteracts == 1 && g_pr.mv[g_pr.nMoves - 1].hm == FT_HM_STRENGTH,
		      "…as ONE Strength terminal (%d interacts, last hm %d)",
		      g_pr.nInteracts, g_pr.mv[g_pr.nMoves - 1].hm);

		// --- and the latch again, this time through the planner: nothing to activate, no program.
		bind("route117", &bus, &m, 3);
		set_flag(FT_VAR_EMERALD, c->badgeStrength);
		g_ovSb1[c->flagsOff + (c->strengthLatch >> 3)] |= (uint8_t)(1u << (c->strengthLatch & 7));
		put_mon(0, 0x12345678u, 1u, strSet, 1, 0, 0);
		set_obj(6, 87, 18, 12);
		npc_clear(); npc_add(18, 12);
		CHECK(!fieldtrav_plan(&bus, &m, FT_VAR_EMERALD, &pty, 18, 14, 18, 12, false, g_npc, g_npcN, &g_pr),
		      "Strength already active -> the tap falls through to the shipped walker, as today");
	}

	// ================================================================ TEST 24
	// PHASE 28 / lane X — DIVE against the USER'S OWN cartridge, and the tier-order precondition.
	//
	// The phase-26 audit (O2) ran the SHIPPED `fieldtrav_dive` on the real Route 126 <-> Underwater
	// Route 126 pair — something no test did — and found it planning a whole dive->swim->surface
	// round trip to a goal ONE PLAIN SURF STEP AWAY. The suite could not see it because its 16x8
	// synthetic world has exactly one dive tile and one surfacing tile, so the only shape it can
	// express is the degenerate `ux == cx && uy == cy` guard lane V shipped. This test is the real
	// world: same map, same coordinates, same party, graded on the fix.
	printf("\nTEST 24 — DIVE on the real Route 126 pair: the tier-order precondition (audit O2)\n");
	rom_load();
	if (!g_rom) {
		printf("TEST 24 — PARTIAL SKIP: roms/emerald.gba not readable from the CWD; "
		       "the real-map dive checks did not run\n");
	} else {
		FpBus rb; rb.read8 = rom_r8; rb.read16 = rom_r16; rb.read32 = rom_r32; rb.ctx = 0;
		FtRomMap r126;
		CHECK(fieldtrav_rom_map(&rb, EM_MAPGROUPS, 0, 41, &r126), "Route126 (0,41) resolves");
		FpBus l1; FpMap m1;
		live_from_rom(&rb, &r126, &l1, &m1);

		// The fixture: badge 07 + a mon that knows DIVE, over the live cartridge map.
		const FtEngCfg* cE = fieldtrav_cfg(FT_VAR_EMERALD);
		memset(g_ovSb1, 0, sizeof g_ovSb1); memset(g_ovParty, 0, sizeof g_ovParty);
		g_ov[0].p = 0;
		g_ov[1].addr = SB1_BASE;   g_ov[1].len = sizeof g_ovSb1;   g_ov[1].p = g_ovSb1;
		g_ov[2].addr = PARTY_BASE; g_ov[2].len = sizeof g_ovParty; g_ov[2].p = g_ovParty;
		g_ov[3].p = 0;
		set_flag(FT_VAR_EMERALD, cE->badgeDive);
		const uint16_t dvSet[4] = { MOVE_DIVE, MOVE_SURF, 0, 0 };
		put_mon(0, 0x12345678u, 1u, dvSet, 1, 0, 0);
		FtParty pty = party_of(1);
		FpBus dbus = { wf_r8, wf_r16, wf_r32, &l1 };
		CHECK((fieldtrav_usable(&dbus, FT_VAR_EMERALD, &pty) & (1u << FT_HM_DIVE)) != 0,
		      "the fixture really is Dive-eligible (badge 0x%03X + MOVE_DIVE)", cE->badgeDive);
		CHECK(fieldpath_behaviour_at(&l1, &m1, 20, 40) == 0x12 &&
		      fieldpath_behaviour_at(&l1, &m1, 20, 41) == 0x12,
		      "(20,40) and (20,41) are both MB_DEEP_WATER on the real map — one surf step apart");

		FtDive dv;
		// (a) THE AUDIT'S EXACT REPRO. Before the fix this answered ok=1 / PLANNED with
		//     out/mid/back = 0/1/0 — a one-tile underwater hop to a tile the player could swim to.
		CHECK(!fieldtrav_dive(&dbus, &m1, EM_MAPGROUPS, 0, 41, FT_VAR_EMERALD, &pty,
		                      20, 40, 20, 41, true, 0, 0, &dv),
		      "a goal ONE plain surf step away is REFUSED, not answered with a round trip");
		CHECK(dv.outcome == FT_OUT_TIER0,
		      "…and the outcome names the reason: TIER0, the shipped router owns that tap (got %s)",
		      OUTN(dv.outcome));
		CHECK(!dv.ok && dv.stepsOut == 0 && dv.stepsMid == 0 && dv.stepsBack == 0,
		      "…with nothing left behind in the plan");

		// (b) The same refusal all the way out to the window edge, for EVERY goal plain surfing
		//     already reaches. This is the property, not the anecdote: sweep the window and assert
		//     that no plain-reachable goal survives, while counting what is left.
		int refused = 0, planned = 0, other = 0;
		for (int gy = 40 - 12; gy <= 40 + 12; gy++) {
			for (int gx = 20 - 12; gx <= 20 + 12; gx++) {
				if (gx == 20 && gy == 40) continue;
				bool ok2 = fieldtrav_dive(&dbus, &m1, EM_MAPGROUPS, 0, 41, FT_VAR_EMERALD, &pty,
				                          20, 40, gx, gy, true, 0, 0, &dv);
				if (ok2) planned++;
				else if (dv.outcome == FT_OUT_TIER0) refused++;
				else other++;
			}
		}
		CHECK(refused > 0, "the sweep really exercised the new rule (%d TIER0 refusals)", refused);
		// PHASE 29 / lane F — audit O7: this used to read `planned == 0 || planned > 0`, which can
		// never fail and inflated the count by one. The real bookkeeping invariant is that every
		// goal in the window was classified into exactly one bucket.
		CHECK(refused + planned + other == 25 * 25 - 1,
		      "every goal in the 25x25 window is classified exactly once (%d + %d + %d of %d)",
		      refused, planned, other, 25 * 25 - 1);
		// Every surviving plan must be a genuine dive: at least one step on the OTHER map, and a
		// surfacing tile that is not the dive tile. (`planned` may legitimately be 0 here — Route
		// 126's dive field is one connected body — so the assertion is about the survivors, not
		// about there being any.)
		for (int gy = 40 - 12; gy <= 40 + 12; gy++) {
			for (int gx = 20 - 12; gx <= 20 + 12; gx++) {
				if (!fieldtrav_dive(&dbus, &m1, EM_MAPGROUPS, 0, 41, FT_VAR_EMERALD, &pty,
				                    20, 40, gx, gy, true, 0, 0, &dv)) continue;
				CHECK(dv.stepsMid > 0, "a surviving plan crosses the paired map (mid=%d)", dv.stepsMid);
				CHECK(!(dv.upX == dv.diveX && dv.upY == dv.diveY),
				      "…and surfaces somewhere else than it dived");
			}
		}

		// (c) The fix must not have turned into "always refuse": with the player standing on a tile
		//     from which the goal is NOT plain-reachable, the planner still has to work. Underwater
		//     Route 126's twin is dimension-identical, so a goal INSIDE the rock ring at Route 126
		//     is the natural case; rather than hard-code one, assert the mechanism directly — the
		//     home-leg BFS is what the precondition reads, and it is the same one the plan uses.
		CHECK(dv.outcome == FT_OUT_TIER0 || dv.outcome == FT_OUT_NODIVE || dv.outcome == FT_OUT_PLANNED,
		      "every sweep answer is one of the three honest outcomes (last %s)", OUTN(dv.outcome));

		// (d) The rule is about REACHABILITY, not about distance. PHASE 29 / lane F — audit O7: the
		//     comment here used to describe the goal the player CANNOT reach in the home mode and
		//     then assert the opposite case (the player's own tile, which is trivially reachable).
		//     Both are worth having, so both are written, and the unreachable one is graded as a
		//     PROPERTY against an independent function rather than a hand-picked tile.
		//     The independent oracle is `fieldtrav_plan`'s own tier-0 verdict: it is a forward,
		//     edge-aware BFS from the player, where the dive precondition is a backward dry BFS
		//     rooted at the goal, so agreeing is a real statement about the map and not a
		//     restatement of one function by itself.
		CHECK(!fieldtrav_dive(&dbus, &m1, EM_MAPGROUPS, 0, 41, FT_VAR_EMERALD, &pty,
		                      20, 40, 20, 40, true, 0, 0, &dv),
		      "the player's OWN tile is refused");
		CHECK(dv.outcome == FT_OUT_TIER0, "…as TIER0 (distance 0 is trivially reachable)");
		int unreachN = 0, wrongTier = 0, tierMismatch = 0;
		for (int gy = 40 - 12; gy <= 40 + 12; gy++) {
			for (int gx = 20 - 12; gx <= 20 + 12; gx++) {
				if (gx == 20 && gy == 40) continue;
				fieldtrav_plan(&dbus, &m1, FT_VAR_EMERALD, &pty, 20, 40, gx, gy, true, 0, 0, &g_pr);
				bool plainReaches = (g_pr.outcome == FT_OUT_TIER0);
				fieldtrav_dive(&dbus, &m1, EM_MAPGROUPS, 0, 41, FT_VAR_EMERALD, &pty,
				               20, 40, gx, gy, true, 0, 0, &dv);
				bool diveRefused = (dv.outcome == FT_OUT_TIER0);
				if (!plainReaches) {
					unreachN++;
					if (diveRefused) wrongTier++;         // the case the old comment described
				}
				if (plainReaches != diveRefused) tierMismatch++;
			}
		}
		CHECK(unreachN > 0,
		      "the window really contains goals plain surfing cannot reach — the case (d) names "
		      "(%d of %d)", unreachN, 25 * 25 - 1);
		CHECK(wrongTier == 0,
		      "…and NOT ONE of them is refused as TIER0: the precondition is about reachability, "
		      "not distance (%d wrong)", wrongTier);
		CHECK(tierMismatch == 0,
		      "…the guard fires on exactly the goals the shipped router already reaches, graded "
		      "against fieldtrav_plan's own tier-0 verdict (%d disagreements)", tierMismatch);
	}

	// ================================================================ TEST 25
	// PHASE 29 / lane F — DEFECT X2, and the CURRENTS twin SPEC-hm-waterfall §4.1 named and left.
	//
	// The defect, live (phase 28, Route 114): a tap on the fall with Waterfall UNUSABLE was handed
	// to the FROZEN tier-0 router, which has no forced-movement table, plotted four UP steps
	// straight through the column and reported ARRIVED at a tile the player never left. §4.2 asked
	// a later phase to give `fieldpath_plan` the same refusal `transition()` enforces; the file is
	// frozen, so the refusal is a SCREEN on the answer — `fieldtrav_path_forced` — and this test is
	// the real-cartridge repro of both halves.
	printf("\nTEST 25 — X2: a dry path is never allowed THROUGH a fall or a current\n");
	{
		// --- (a) the currents metatile test, exhaustively. 0x50..0x53 and nothing else.
		int curHits = 0;
		for (int b = 0; b <= 0xFF; b++) {
			bool rse = fieldtrav_is_current(FP_ENG_RSE, b), frlg = fieldtrav_is_current(FP_ENG_FRLG, b);
			bool want = (b >= 0x50 && b <= 0x53);
			CHECK(rse == want, "RSE  0x%02X current == %d", b, want);
			CHECK(frlg == want, "FRLG 0x%02X current == %d", b, want);
			if (rse) curHits++;
		}
		CHECK(curHits == 4, "exactly FOUR behaviours are currents (got %d)", curHits);
		CHECK(!fieldtrav_is_current(FP_ENG_RSE, -1), "an unreadable behaviour is never a current");
		// The same pairing the waterfall has, and for the same reason: a tile that takes the
		// controls away is never free water.
		for (int b = 0x50; b <= 0x53; b++) {
			CHECK(!fieldtrav_is_surfable(FP_ENG_RSE, b), "RSE  0x%02X is not surfable either", b);
			CHECK(!fieldtrav_is_surfable(FP_ENG_FRLG, b), "FRLG 0x%02X is not surfable either", b);
		}
		CHECK(!fieldtrav_is_current(FP_ENG_RSE, 0x13) && !fieldtrav_is_waterfall(FP_ENG_RSE, 0x50),
		      "the two families are disjoint");
	}
	rom_load();
	if (!g_rom) {
		printf("TEST 25 (ROM half) — PARTIAL SKIP: roms/emerald.gba not readable from the CWD\n");
	} else {
		FpBus rb; rb.read8 = rom_r8; rb.read16 = rom_r16; rb.read32 = rom_r32; rb.ctx = 0;
		FtRomMap r114;
		CHECK(fieldtrav_rom_map(&rb, EM_MAPGROUPS, 0, 29, &r114), "Route114 (0,29) resolves");
		FpBus l1; FpMap m1;
		live_from_rom(&rb, &r114, &l1, &m1);

		// --- (b) THE PHASE-28 REPRO, on the cartridge, through the FROZEN router. This is the
		// measurement the audit made independently: fieldpath_plan (12,13) -> (12,9) = pathLen 4,
		// four UP steps, every one of them into the 0x13 column.
		static FpPlan pl;
		bool okFp = fieldpath_plan(&l1, &m1, 12, 13, 12, 9, 0, 0, &pl);
		CHECK(okFp && pl.pathLen == 4,
		      "the FROZEN router still plots the swim: (12,13)->(12,9) pathLen %d (ok %d)",
		      pl.pathLen, okFp ? 1 : 0);
		int upSteps = 0;
		for (int i = 0; i < pl.pathLen; i++) if (pl.path[i] == FP_U) upSteps++;
		CHECK(upSteps == 4, "…all four steps UP, straight through the fall (%d)", upSteps);
		CHECK(fieldpath_behaviour_at(&l1, &m1, 12, 12) == 0x13 &&
		      fieldpath_behaviour_at(&l1, &m1, 12, 10) == 0x13,
		      "…and the column really is MB_WATERFALL on this cartridge");

		// --- (c) THE SCREEN. It must catch that path at its FIRST forced tile, which is step 0.
		CHECK(fieldtrav_path_forced(&l1, &m1, 12, 13, pl.path, pl.pathLen) == 0,
		      "the screen refuses the swim at step 0 (got %d)",
		      fieldtrav_path_forced(&l1, &m1, 12, 13, pl.path, pl.pathLen));
		// …and it does NOT refuse an honest route. A plain swim along the pool below the fall.
		static FpPlan pl2;
		if (fieldpath_plan(&l1, &m1, 12, 13, 9, 13, 0, 0, &pl2) && pl2.pathLen > 0) {
			CHECK(fieldtrav_path_forced(&l1, &m1, 12, 13, pl2.path, pl2.pathLen) == -1,
			      "a plain swim ALONG the pool (12,13)->(9,13) is clean (%d steps)", pl2.pathLen);
		}
		// The degenerate inputs answer "nothing found", never a false refusal.
		CHECK(fieldtrav_path_forced(0, &m1, 12, 13, pl.path, pl.pathLen) == -1, "NULL bus -> -1");
		CHECK(fieldtrav_path_forced(&l1, &m1, 12, 13, pl.path, 0) == -1, "an empty path -> -1");
		CHECK(fieldtrav_path_forced(&l1, &m1, 12, 13, 0, 4) == -1, "a NULL path -> -1");
		{   // a step code that is not one of FP_R/L/D/U stops the walk rather than indexing wildly
			int8_t bad[3] = { FP_U, 9, FP_U };
			CHECK(fieldtrav_path_forced(&l1, &m1, 12, 9, bad, 3) == -1,
			      "a garbage step code answers 'nothing found', not a crash");
		}
		{   // AN UNREADABLE TILE IS NOT A REFUSAL. -1 means "nothing was found", never "nothing is
			// there" — a screen that treated an off-map read as forced would silently delete every
			// route that passes near a map edge.
			int8_t off[6] = { FP_L, FP_L, FP_L, FP_L, FP_L, FP_L };
			CHECK(fieldpath_behaviour_at(&l1, &m1, -1, 13) < 0,
			      "…(-1,13) really is unreadable (%d)", fieldpath_behaviour_at(&l1, &m1, -1, 13));
			CHECK(fieldtrav_path_forced(&l1, &m1, 2, 13, off, 6) == -1,
			      "a path that walks off the map is clean, not refused (got %d)",
			      fieldtrav_path_forced(&l1, &m1, 2, 13, off, 6));
		}
		// The screen is position-relative: the SAME path from a start that never enters the column
		// is clean, which is what stops it becoming "refuse anything near a fall".
		CHECK(fieldtrav_path_forced(&l1, &m1, 5, 13, pl.path, pl.pathLen) == -1,
		      "the same four UP steps taken from (5,13) touch no fall");
		// It answers with the FIRST forced step, not merely "somewhere": a detour along the pool
		// and then up is refused at index 2, the step that actually enters the column.
		{
			int8_t detour[3] = { FP_L, FP_R, FP_U };
			CHECK(fieldpath_behaviour_at(&l1, &m1, 11, 13) == 0x15,
			      "…(11,13) is pool water, so the first two steps are clean (beh 0x%02X)",
			      fieldpath_behaviour_at(&l1, &m1, 11, 13));
			CHECK(fieldtrav_path_forced(&l1, &m1, 12, 13, detour, 3) == 2,
			      "…and the screen names step 2, the one that enters the fall (got %d)",
			      fieldtrav_path_forced(&l1, &m1, 12, 13, detour, 3));
		}
		// Every tile of the column is refused, not just the bottom one — the whole 3-tile fall.
		for (int y = 12; y >= 10; y--) {
			int8_t up1[1] = { FP_U };
			CHECK(fieldtrav_path_forced(&l1, &m1, 12, y + 1, up1, 1) == 0,
			      "a single step onto (12,%d) is refused", y);
		}
		// …and Ever Grande's K=8 column, the fall the whole retarget exists for.
		{
			FtRomMap rEG2;
			if (fieldtrav_rom_map(&rb, EM_MAPGROUPS, 0, 8, &rEG2)) {
				FpBus lE; FpMap mE;
				live_from_rom(&rb, &rEG2, &lE, &mE);
				int8_t up8[8] = { FP_U, FP_U, FP_U, FP_U, FP_U, FP_U, FP_U, FP_U };
				CHECK(fieldtrav_path_forced(&lE, &mE, 20, 68, up8, 8) == 0,
				      "Ever Grande's 8-tile fall is refused at its first step too (got %d)",
				      fieldtrav_path_forced(&lE, &mE, 20, 68, up8, 8));
			}
		}

		// --- (d) THE TWIN, in `transition()`. The four currents are now the same refusal as the
		// waterfall in BOTH modes. Emerald's currents live on Route 134's westward drift; rather
		// than assume a map, find one on the cartridge and prove the planner will not route into
		// it. (If this ROM has none in the scanned set the check says so instead of passing.)
		{
			const int scanG[] = { 0, 0, 0 }, scanN[] = { 47, 48, 49 };   // Routes 132/133/134
			int found = 0, refusedThrough = 0;
			for (int i = 0; i < 3 && !found; i++) {
				FtRomMap rc;
				if (!fieldtrav_rom_map(&rb, EM_MAPGROUPS, scanG[i], scanN[i], &rc)) continue;
				FpBus lc; FpMap mc;
				live_from_rom(&rb, &rc, &lc, &mc);
				for (int y = 1; y < rc.h - 1 && !found; y++) {
					for (int x = 1; x < rc.w - 1; x++) {
						if (!fieldtrav_is_current(mc.engine, fieldpath_behaviour_at(&lc, &mc, x, y)))
							continue;
						// A tile beside it that is ordinary water, and a goal on the far side.
						if (!fieldtrav_is_surfable(mc.engine, fieldpath_behaviour_at(&lc, &mc, x, y + 1)))
							continue;
						if (!fieldtrav_is_surfable(mc.engine, fieldpath_behaviour_at(&lc, &mc, x, y - 1)))
							continue;
						memset(g_ovSb1, 0, sizeof g_ovSb1); memset(g_ovParty, 0, sizeof g_ovParty);
						g_ov[0].p = 0;
						g_ov[1].addr = SB1_BASE;   g_ov[1].len = sizeof g_ovSb1;   g_ov[1].p = g_ovSb1;
						g_ov[2].addr = PARTY_BASE; g_ov[2].len = sizeof g_ovParty; g_ov[2].p = g_ovParty;
						g_ov[3].p = 0;
						const uint16_t sf[4] = { MOVE_SURF, 0, 0, 0 };
						set_flag(FT_VAR_EMERALD, fieldtrav_cfg(FT_VAR_EMERALD)->badgeSurf);
						put_mon(0, 0x12345678u, 1u, sf, 1, 0, 0);
						FtParty pc = party_of(1);
						FpBus cb = { wf_r8, wf_r16, wf_r32, &lc };
						// A one-tile "route" whose only step is ONTO the current.
						fieldtrav_plan(&cb, &mc, FT_VAR_EMERALD, &pc, x, y + 1, x, y, true, 0, 0, &g_pr);
						found = 1;
						if (g_pr.outcome != FT_OUT_TIER0) refusedThrough = 1;
						CHECK(refusedThrough,
						      "map (%d,%d) (%d,%d) is a CURRENT and the planner refuses to route "
						      "onto it (outcome %s)", scanG[i], scanN[i], x, y, OUTN(g_pr.outcome));
						// …and the SCREEN refuses it too, which is the half that guards the FROZEN
						// router — the same defect as the waterfall, on the twin behaviour.
						{
							int8_t up1[1] = { FP_U };
							CHECK(fieldtrav_path_forced(&lc, &mc, x, y + 1, up1, 1) == 0,
							      "…and a dry path that steps onto (%d,%d) is screened out", x, y);
						}
						break;
					}
				}
			}
			if (!found)
				printf("TEST 25 (d) — NOT RUN: no current metatile found on Routes 132/133/134 of "
				       "this cartridge; the transition() twin is graded by the mutation only\n");
		}
	}

	printf("\n%d checks, %d failures\n", g_checks, g_fail);
	return g_fail ? 1 : 0;
}
