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
//   TEST 2  the per-engine constant block: badges / flags offset / gfx ids ..... T1.3 / T1.4
//   TEST 3  the badge read is FlagGet's own bit math, with its degradations .... H1.4
//   TEST 4  the substruct permutation table is a permutation, and pret's ....... H1.5
//   TEST 5  party decryption on golden bytes: moves, egg, checksum, empty ...... H1.5
//   TEST 6  eligibility = badge AND mon, never one of the two .................. §1.4
//   TEST 7  edge scan: gfx ids per engine, LIVE coords, inactive slots skipped . H1.2 / H1.3
//   TEST 8  DRY PATHS WIN — a walkable route is never displaced ................ H1.7
//   TEST 9  the Route 117 surf crossing, end to end ........................... §1.5 / T6-P1
//   TEST 10 never prompt what the game refuses: no badge / no mon = no plan .... §0 property 2
//   TEST 11 currents and waterfalls are never free surf tiles ................. T5.8
//   TEST 12 the Cut object edge, and the same tap with no badge ............... §2.3.1
//   TEST 13 the interact cap fails honestly ................................... H1.8
//   TEST 14 a tap taken while already surfing never plans a second mount ...... H1.5

#include <stdio.h>
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
#define OV_N 3
static struct { uint32_t addr; uint32_t len; uint8_t* p; } g_ov[OV_N];
static uint8_t g_ovObj[0x24 * 16];      // gObjectEvents[16]
static uint8_t g_ovSb1[0x1500];         // SaveBlock1 head THROUGH the flags array (EM's
                                        // flags[] runs 0x1270..0x139B, so 0x1300 would truncate it)
static uint8_t g_ovParty[100 * 6];      // gPlayerParty[6]

#define SB1_BASE   0x02025734u          // an EWRAM address; the module takes sb1 already resolved
#define PARTY_BASE 0x02024000u

static const FxMap* g_fx;
static uint8_t ov_r8(void* ctx, uint32_t a) {
	for (int i = 0; i < OV_N; i++)
		if (g_ov[i].p && a >= g_ov[i].addr && a < g_ov[i].addr + g_ov[i].len)
			return g_ov[i].p[a - g_ov[i].addr];
	const FxMap* f = (const FxMap*)ctx;
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
	g_ov[0].addr = f->mapObjects; g_ov[0].len = sizeof g_ovObj;   g_ov[0].p = g_ovObj;
	g_ov[1].addr = SB1_BASE;      g_ov[1].len = sizeof g_ovSb1;   g_ov[1].p = g_ovSb1;
	g_ov[2].addr = PARTY_BASE;    g_ov[2].len = sizeof g_ovParty; g_ov[2].p = g_ovParty;
	ov_w32(g_ovObj, 0x00, 1u);                                    // slot 0 (the player) active
	ov_w8 (g_ovObj, 0x0B, (uint8_t)(pElev & 0x0F));               // currentElevation
	bus->read8 = ov_r8; bus->read16 = ov_r16; bus->read32 = ov_r32; bus->ctx = (void*)f;
	m->engine = (FpEngine)f->engine; m->mapHeader = f->mapHeader; m->mapObjects = f->mapObjects;
	m->gridPtr = f->gridPtr; m->backupW = f->bw; m->backupH = f->bh;
}

// FlagSet, the writer half of fieldtrav_flag_get (pokeemerald src/event_data.c FlagSet).
static void set_flag(FpEngine eng, int flagId) {
	const FtEngCfg* c = fieldtrav_cfg(eng);
	g_ovSb1[c->flagsOff + (flagId >> 3)] |= (uint8_t)(1u << (flagId & 7));
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
static void put_mon(int idx, uint32_t pers, uint32_t otId, const uint16_t moves[4],
                    int hasSpecies, int isEgg, int corruptChecksum) {
	uint8_t* mon = g_ovParty + 100 * idx;
	memset(mon, 0, 100);
	ov_w32(mon, 0x00, pers);
	ov_w32(mon, 0x04, otId);
	ov_w8 (mon, 0x13, (uint8_t)((hasSpecies ? 2 : 0) | (isEgg ? 4 : 0)));

	uint16_t plain[24];
	memset(plain, 0, sizeof plain);
	int slot = fieldtrav_substruct_slot(pers, 1);        // the Attacks substruct
	for (int i = 0; i < 4; i++) plain[slot * 6 + i] = moves[i];
	// Growth substruct (type 0) slot gets a plausible species so the block is not all zeros.
	plain[fieldtrav_substruct_slot(pers, 0) * 6 + 0] = 260;   // arbitrary species id

	uint16_t sum = 0;
	for (int i = 0; i < 24; i++) sum = (uint16_t)(sum + plain[i]);
	ov_w16(mon, 0x1C, (uint16_t)(corruptChecksum ? sum ^ 0xBEEF : sum));

	uint32_t key = pers ^ otId;
	for (int w = 0; w < 12; w++) {
		uint32_t v = (uint32_t)plain[2 * w] | ((uint32_t)plain[2 * w + 1] << 16);
		ov_w32(mon, 0x20 + 4 * w, v ^ key);
	}
}
static FtParty party_of(int count) {
	FtParty p; p.sb1 = SB1_BASE; p.partyBase = PARTY_BASE; p.partyCount = count; return p;
}

#define MOVE_CUT   15
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
	printf("TEST 2 — the per-engine constant block\n");
	{
		const FtEngCfg* r = fieldtrav_cfg(FP_ENG_RSE);
		const FtEngCfg* f = fieldtrav_cfg(FP_ENG_FRLG);
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
		CHECK(fieldtrav_cfg((FpEngine)99) == r, "an unknown engine falls back to RSE, never NULL");
	}

	// ---------------------------------------------------------------- TEST 3
	printf("TEST 3 — the badge read is FlagGet's own bit math\n");
	{
		FpBus bus; FpMap m; bind("route117", &bus, &m, 3);
		const FtEngCfg* c = fieldtrav_cfg(FP_ENG_RSE);
		CHECK(!fieldtrav_flag_get(&bus, FP_ENG_RSE, SB1_BASE, c->badgeSurf), "no badge before it is set");
		set_flag(FP_ENG_RSE, c->badgeSurf);
		CHECK(fieldtrav_flag_get(&bus, FP_ENG_RSE, SB1_BASE, c->badgeSurf), "badge reads back");
		// Neighbouring bits in the SAME byte must be unaffected — the classic off-by-one this
		// rail exists to catch (0x86B = byte 0x10D bit 3, so 0x86A and 0x86C share the byte).
		CHECK(!fieldtrav_flag_get(&bus, FP_ENG_RSE, SB1_BASE, c->badgeSurf - 1), "the bit below stays clear");
		CHECK(!fieldtrav_flag_get(&bus, FP_ENG_RSE, SB1_BASE, c->badgeSurf + 1), "the bit above stays clear");
		// Every one of the 8 bits of one byte, independently.
		for (int b = 0; b < 8; b++) {
			memset(g_ovSb1, 0, sizeof g_ovSb1);
			set_flag(FP_ENG_RSE, 0x860 + b);
			for (int q = 0; q < 8; q++)
				CHECK(fieldtrav_flag_get(&bus, FP_ENG_RSE, SB1_BASE, 0x860 + q) == (q == b),
				      "bit %d set -> only %d reads true (q=%d)", b, b, q);
		}
		// Degradations: no save loaded and a nonsense flag id both answer FALSE, never a guess.
		CHECK(!fieldtrav_flag_get(&bus, FP_ENG_RSE, 0, c->badgeSurf), "sb1 == 0 -> no badge");
		CHECK(!fieldtrav_flag_get(&bus, FP_ENG_RSE, 0x08000000u, c->badgeSurf), "a ROM 'sb1' -> no badge");
		CHECK(!fieldtrav_flag_get(&bus, FP_ENG_RSE, SB1_BASE, -1), "a negative flag id -> false");
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
		const FtEngCfg* c = fieldtrav_cfg(FP_ENG_RSE);

		CHECK(fieldtrav_usable(&bus, FP_ENG_RSE, &pty) == 0, "nothing set -> nothing usable");

		put_mon(0, 0x12345678u, 1u, surfSet, 1, 0, 0);
		CHECK((fieldtrav_usable(&bus, FP_ENG_RSE, &pty) & (1u << FT_HM_SURF)) == 0,
		      "a mon that knows Surf without the badge is NOT usable");

		memset(g_ovParty, 0, sizeof g_ovParty);
		set_flag(FP_ENG_RSE, c->badgeSurf);
		CHECK((fieldtrav_usable(&bus, FP_ENG_RSE, &pty) & (1u << FT_HM_SURF)) == 0,
		      "the badge without a mon is NOT usable");

		put_mon(0, 0x12345678u, 1u, surfSet, 1, 0, 0);
		uint32_t u = fieldtrav_usable(&bus, FP_ENG_RSE, &pty);
		CHECK((u & (1u << FT_HM_SURF)) != 0, "badge AND mon -> Surf usable");
		CHECK((u & (1u << FT_HM_CUT)) == 0, "Cut is still not usable");
		CHECK(fieldtrav_usable(&bus, FP_ENG_RSE, NULL) == 0, "a NULL party -> nothing usable");
		CHECK(fieldtrav_usable(NULL, FP_ENG_RSE, &pty) == 0, "a NULL bus -> nothing usable");
	}

	// ---------------------------------------------------------------- TEST 7
	printf("TEST 7 — the edge scan\n");
	{
		FpBus bus; FpMap m; bind("route117", &bus, &m, 3);
		FtEdge e[FT_MAX_EDGES];
		CHECK(fieldtrav_scan_edges(&bus, &m, e) == 0, "an empty object table has no edges");

		set_obj(1, 82, 18, 12);    // OBJ_EVENT_GFX_CUTTABLE_TREE (EM)
		set_obj(2, 86, 19, 12);    // OBJ_EVENT_GFX_BREAKABLE_ROCK (EM)
		set_obj(3, 87, 17, 12);    // OBJ_EVENT_GFX_PUSHABLE_BOULDER (EM)
		set_obj(4, 3,  16, 12);    // a plain NPC — never an edge
		set_obj(5, 95, 15, 12);    // FR's cut-tree id on an RSE map — must NOT match
		int n = fieldtrav_scan_edges(&bus, &m, e);
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
		n = fieldtrav_scan_edges(&bus, &m, e);
		CHECK(e[0].x == 0 && e[0].y == 0, "map-local (0,0) survives the offset removal");

		// An INACTIVE slot is not an edge, and slot 0 (the player) is never scanned.
		memset(g_ovObj, 0, sizeof g_ovObj);
		ov_w32(g_ovObj, 0x00, 1u); ov_w8(g_ovObj, 0x05, 82);         // the player wearing a tree's id
		CHECK(fieldtrav_scan_edges(&bus, &m, e) == 0, "slot 0 is never an edge");
		set_obj(2, 82, 5, 5); ov_w32(g_ovObj + 0x24 * 2, 0x00, 0u);  // present but inactive
		CHECK(fieldtrav_scan_edges(&bus, &m, e) == 0, "an inactive slot is not an edge");

		// FRLG ids on an FRLG map.
		FpBus fb; FpMap fm; bind("frstair", &fb, &fm, 3);
		set_obj(1, 95, 3, 3); set_obj(2, 82, 4, 3);
		n = fieldtrav_scan_edges(&fb, &fm, e);
		CHECK(n == 1 && e[0].hm == FT_HM_CUT, "FRLG matches 95, not 82 (got n=%d)", n);

		CHECK(fieldtrav_scan_edges(&bus, &m, NULL) == 0, "a NULL out array is refused");
		FpMap noObj = m; noObj.mapObjects = 0;
		CHECK(fieldtrav_scan_edges(&bus, &noObj, e) == 0, "mapObjects 0 -> no edges (named degradation)");
	}

	// ---------------------------------------------------------------- TEST 8
	printf("TEST 8 — DRY PATHS WIN\n");
	{
		// Route 117: (19,14) and (28,14) are the two banks of the pond, but they are ALSO joined by
		// a long dry walk along row 8. With Surf fully available, the planner must still decline —
		// tier order, not step count, is the primary key (SPEC H1.7).
		FpBus bus; FpMap m; bind("route117", &bus, &m, 3);
		set_flag(FP_ENG_RSE, fieldtrav_cfg(FP_ENG_RSE)->badgeSurf);
		const uint16_t surfSet[4] = { MOVE_SURF, 0, 0, 0 };
		put_mon(0, 0x12345678u, 1u, surfSet, 1, 0, 0);
		FtParty pty = party_of(1);
		npc_clear();
		bool ok = fieldtrav_plan(&bus, &m, &pty, 19, 14, 28, 14, false, g_npc, g_npcN, &g_pr);
		CHECK(!ok, "a walkable route is DECLINED by the traversal planner");
		CHECK(g_pr.outcome == FT_OUT_TIER0, "outcome is TIER0, not a wet program (got %s)", OUTN(g_pr.outcome));
		CHECK(g_pr.nMoves == 0 && g_pr.nInteracts == 0, "nothing is emitted when tier 0 wins");
		CHECK((g_pr.usable & (1u << FT_HM_SURF)) != 0, "...and it declined WITH Surf available, which is the point");
	}

	// ---------------------------------------------------------------- TEST 9
	printf("TEST 9 — the Route 117 surf crossing, end to end\n");
	{
		FpBus bus; FpMap m; bind("route117", &bus, &m, 3);
		set_flag(FP_ENG_RSE, fieldtrav_cfg(FP_ENG_RSE)->badgeSurf);
		const uint16_t surfSet[4] = { MOVE_SURF, 0, 0, 0 };
		put_mon(0, 0x12345678u, 1u, surfSet, 1, 0, 0);
		FtParty pty = party_of(1);
		npc_clear(); seal_pocket();
		bool ok = fieldtrav_plan(&bus, &m, &pty, 19, 14, 28, 14, false, g_npc, g_npcN, &g_pr);
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
		ok = fieldtrav_plan(&bus, &m, &pty, 19, 14, 24, 14, false, g_npc, g_npcN, &g_pr);
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
		set_flag(FP_ENG_RSE, fieldtrav_cfg(FP_ENG_RSE)->badgeSurf);
		npc_clear(); seal_pocket();
		CHECK(!fieldtrav_plan(&bus, &m, &pty, 19, 14, 28, 14, false, g_npc, g_npcN, &g_pr),
		      "badge without a Surf mon plans NOTHING");
		CHECK(g_pr.nMoves == 0, "and emits no moves");

		// (b) mon, no badge.
		bind("route117", &bus, &m, 3);
		put_mon(0, 0x12345678u, 1u, surfSet, 1, 0, 0);
		npc_clear(); seal_pocket();
		CHECK(!fieldtrav_plan(&bus, &m, &pty, 19, 14, 28, 14, false, g_npc, g_npcN, &g_pr),
		      "a Surf mon without the badge plans NOTHING");

		// (c) neither.
		bind("route117", &bus, &m, 3);
		npc_clear(); seal_pocket();
		CHECK(!fieldtrav_plan(&bus, &m, &pty, 19, 14, 28, 14, false, g_npc, g_npcN, &g_pr),
		      "neither -> nothing");
		CHECK(g_pr.usable == 0, "the eligibility mask is empty and is reported honestly");

		// (d) a genuinely unreachable tile with FULL eligibility still fails honestly.
		bind("route117", &bus, &m, 3);
		set_flag(FP_ENG_RSE, fieldtrav_cfg(FP_ENG_RSE)->badgeSurf);
		put_mon(0, 0x12345678u, 1u, surfSet, 1, 0, 0);
		npc_clear();
		CHECK(!fieldtrav_plan(&bus, &m, &pty, 19, 14, 16, 14, false, g_npc, g_npcN, &g_pr),
		      "a tile inside a wall is never reachable");
		CHECK(g_pr.outcome == FT_OUT_UNREACHABLE, "and says UNREACHABLE (got %s)", OUTN(g_pr.outcome));

		// (e) degradations: a bad map, and a goal outside the search window.
		FpMap bad = m; bad.gridPtr = 0x08000000u;
		CHECK(!fieldtrav_plan(&bus, &bad, &pty, 19, 14, 28, 14, false, g_npc, g_npcN, &g_pr) &&
		      g_pr.outcome == FT_OUT_BADMAP, "a ROM grid pointer -> BADMAP");
		CHECK(!fieldtrav_plan(&bus, &m, &pty, 19, 14, 19 + FP_WHALF + 1, 14, false, g_npc, g_npcN, &g_pr) &&
		      g_pr.outcome == FT_OUT_WINDOW, "a goal past +-FP_WHALF -> WINDOW");
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
		const FtEngCfg* c = fieldtrav_cfg(FP_ENG_RSE);
		set_flag(FP_ENG_RSE, c->badgeCut);
		const uint16_t cutSet[4] = { MOVE_CUT, 0, 0, 0 };
		put_mon(0, 0x12345678u, 1u, cutSet, 1, 0, 0);
		FtParty pty = party_of(1);

		// Seal the pocket's three-tile mouth with a CUT TREE in the middle and NPCs either side.
		// The only way out is now through the tree — the user's "a place beyond some tree".
		set_obj(6, 82, 18, 12);
		npc_clear(); npc_add(17, 12); npc_add(18, 12); npc_add(19, 12);
		bool ok = fieldtrav_plan(&bus, &m, &pty, 18, 14, 18, 10, false, g_npc, g_npcN, &g_pr);
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
		CHECK(!fieldtrav_plan(&bus, &m, &pty, 18, 14, 18, 10, false, g_npc, g_npcN, &g_pr),
		      "no badge -> the tree stays a wall and NOTHING is planned");
		CHECK(g_pr.nEdges == 1, "the edge was still SEEN (honest reporting), it was just not usable");

		// A BOULDER is never a planned edge, badge or not (Sokoban risk, SPEC §5).
		bind("route117", &bus, &m, 3);
		set_flag(FP_ENG_RSE, c->badgeStrength);
		const uint16_t strSet[4] = { 70 /* MOVE_STRENGTH */, 0, 0, 0 };
		put_mon(0, 0x12345678u, 1u, strSet, 1, 0, 0);
		set_obj(6, 87, 18, 12);
		npc_clear(); npc_add(17, 12); npc_add(18, 12); npc_add(19, 12);
		CHECK(!fieldtrav_plan(&bus, &m, &pty, 18, 14, 18, 10, false, g_npc, g_npcN, &g_pr),
		      "a pushable boulder is NEVER routed through, even fully eligible");
		CHECK((g_pr.usable & (1u << FT_HM_STRENGTH)) != 0, "...and Strength really was usable, which is the point");
	}

	// ---------------------------------------------------------------- TEST 13
	printf("TEST 13 — the interact cap fails honestly\n");
	{
		// Three trees in a row across the pocket mouth: 3 interacts would be needed, the cap is 2.
		FpBus bus; FpMap m; bind("route117", &bus, &m, 3);
		const FtEngCfg* c = fieldtrav_cfg(FP_ENG_RSE);
		set_flag(FP_ENG_RSE, c->badgeCut);
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
		bool ok = fieldtrav_plan(&bus, &m, &pty, 18, 14, 18, 9, false, g_npc, g_npcN, &g_pr);
		CHECK(!ok, "three activations exceed FT_MAX_INTERACTS and the plan fails");
		CHECK(g_pr.outcome == FT_OUT_UNREACHABLE, "reported honestly (got %s)", OUTN(g_pr.outcome));
		CHECK(g_pr.nMoves == 0, "and NOTHING partial is emitted — never 'walk toward it and hope'");
		CHECK(g_pr.nEdges == 3, "all three trees were seen (honest reporting)");

		// The SAME chain two trees long IS inside the cap and plans — which proves the cap is a
		// limit and not a blanket refusal of multi-HM routes.
		bind("route117", &bus, &m, 3);
		set_flag(FP_ENG_RSE, c->badgeCut);
		put_mon(0, 0x12345678u, 1u, cutSet, 1, 0, 0);
		set_obj(6, 82, 18, 12); set_obj(7, 82, 18, 11);
		npc_clear();
		npc_add(17, 12); npc_add(19, 12); npc_add(17, 11); npc_add(19, 11);
		npc_add(18, 12); npc_add(18, 11);
		ok = fieldtrav_plan(&bus, &m, &pty, 18, 14, 18, 10, false, g_npc, g_npcN, &g_pr);
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
		set_flag(FP_ENG_RSE, fieldtrav_cfg(FP_ENG_RSE)->badgeSurf);
		const uint16_t surfSet[4] = { MOVE_SURF, 0, 0, 0 };
		put_mon(0, 0x12345678u, 1u, surfSet, 1, 0, 0);
		FtParty pty = party_of(1);
		npc_clear();
		bool ok = fieldtrav_plan(&bus, &m, &pty, 24, 14, 28, 14, true /* startSurfing */, g_npc, g_npcN, &g_pr);
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

	printf("\n%d checks, %d failures\n", g_checks, g_fail);
	return g_fail ? 1 : 0;
}
