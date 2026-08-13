// test_fieldpath.c — PC host unit test for the phase-18 warp classifier + router
// (source/fieldpath.{c,h}), SPEC-door T4.12.
//
// The fixtures are NOT hand-written mocks: test/host/fixtures_fieldpath.h is generated from the
// USER'S OWN ROMs (sdmc/dual-gba/gameA.gba = BPEE rev0, gameB.gba = BPRE rev1) and contains real
// gBackupMapLayout grids, the real MapHeader -> MapLayout -> Tileset -> metatileAttributes chain
// and the real MapEvents.warps tables. So this suite walks the SAME pointers, at the SAME
// per-engine offsets/strides/masks, over the SAME map geometry the app sees on hardware — which
// is what makes "the door opens" provable on the PC and not just on the console.
//
// Pure-C dual-compile per CLAUDE.md rule #4 (fieldpath.c is header-free by design).
//
//   clang -std=c11 -Wall -Wextra -O2 -I source test/host/test_fieldpath.c source/fieldpath.c -o /tmp/tfp && /tmp/tfp
//
// TEST NUMBERING (SPEC-door T4.12):
//   TEST 1  every row of BOTH engines' behaviour tables ......... T3.8   (a shared table FAILS)
//   TEST 2  the behaviour read walks the real pointer chain ..... T3.5
//   TEST 3  DOOR retarget + approach on the real Mauville grid .. T2.4 / T4.4
//   TEST 4  the zero-step plan .................................. T4.4
//   TEST 5  DOOR with no walkable approach -> emit NOTHING ...... T4.4
//   TEST 6  the DIR_S exit mat (the 100 %-broken class) ......... T2.6 / T4.4
//   TEST 7  STEP escalator / ladder ............................. T4.4
//   TEST 8  FRLG directional stair warps ........................ T3.8 FRLG-only
//   TEST 9  elevation: the router must not walk into the pond ... T5.1 / T4.7
//   TEST 10 degradation ladder (no mapHeader / bad ptr / bounds)  T4.3
//   TEST 11 warp-event confirmation never PROMOTES .............. T3.4
//   TEST 12 the two-metatile-tall door graphic (head retarget) .. T2.5(b)
//   TEST 13 legacy FP_WK_NONE behaviour is bit-for-bit unchanged  T4.4 last row
//   TEST 14 the stall at a blocked WK_NONE terminal is ARRIVAL   FIX PASS finding 1

#include <stdio.h>
#include <string.h>
#include "../../source/fieldpath.h"
#include "fixtures_fieldpath.h"

static int g_checks = 0, g_fail = 0;
#define CHECK(cond, ...) do { \
	g_checks++; \
	if (!(cond)) { g_fail++; printf("  [FAIL] "); printf(__VA_ARGS__); printf("   (at %s:%d)\n", __FILE__, __LINE__); } \
} while (0)

// ---------------- the fixture bus: a sparse GBA memory image ----------------
static const FxMap* g_fx;
static uint8_t fx_r8(void* ctx, uint32_t a) {
	const FxMap* f = (const FxMap*)ctx;
	for (int i = 0; i < f->nreg; i++)
		if (a >= f->reg[i].addr && a < f->reg[i].addr + f->reg[i].len)
			return f->reg[i].p[a - f->reg[i].addr];
	return 0;   // unmapped reads as 0, exactly like an open bus the classifier must survive
}
static uint16_t fx_r16(void* c, uint32_t a) { return (uint16_t)(fx_r8(c, a) | (fx_r8(c, a + 1) << 8)); }
static uint32_t fx_r32(void* c, uint32_t a) { return (uint32_t)fx_r16(c, a) | ((uint32_t)fx_r16(c, a + 2) << 16); }

static const FxMap* fx(const char* name) {
	for (int i = 0; i < FX_MAP_N; i++) if (!strcmp(FX_MAPS[i].name, name)) return &FX_MAPS[i];
	printf("  [FAIL] no fixture '%s'\n", name); g_fail++; return &FX_MAPS[0];
}
static void bind(const char* name, FpBus* bus, FpMap* m) {
	const FxMap* f = fx(name); g_fx = f;
	bus->read8 = fx_r8; bus->read16 = fx_r16; bus->read32 = fx_r32; bus->ctx = (void*)f;
	m->engine = (FpEngine)f->engine; m->mapHeader = f->mapHeader; m->mapObjects = f->mapObjects;
	m->gridPtr = f->gridPtr; m->backupW = f->bw; m->backupH = f->bh;
}
// Set gObjectEvents[0].currentElevation for the plan (the fixture image has no object events,
// so an unmapped read gives 0 = ELEVATION_TRANSITION = the rule disarmed). This overrides it.
static uint8_t g_forceElev = 0xFF;   // 0xFF = "use the image"
static uint8_t fx_r8_elev(void* ctx, uint32_t a) {
	const FxMap* f = (const FxMap*)ctx;
	if (g_forceElev != 0xFF && a == f->mapObjects + 0x0Bu) return g_forceElev;
	return fx_r8(ctx, a);
}
static uint16_t fx_r16e(void* c, uint32_t a) { return (uint16_t)(fx_r8_elev(c, a) | (fx_r8_elev(c, a + 1) << 8)); }
static uint32_t fx_r32e(void* c, uint32_t a) { return (uint32_t)fx_r16e(c, a) | ((uint32_t)fx_r16e(c, a + 2) << 16); }
static void bind_elev(const char* name, FpBus* bus, FpMap* m, int elev) {
	bind(name, bus, m);
	g_forceElev = (uint8_t)elev;
	bus->read8 = fx_r8_elev; bus->read16 = fx_r16e; bus->read32 = fx_r32e;
}

static const char* KN[4] = { "NONE", "DOOR", "DIR", "STEP" };
static const char  DN[5] = { 'R', 'L', 'D', 'U', '?' };
static char dch(int d) { return (d >= 0 && d < 4) ? DN[d] : '-'; }
// Render a plan's path as a direction string, so a failure message is readable.
static void pstr(const FpPlan* p, char* out, int cap) {
	int n = 0;
	for (int i = 0; i < p->pathLen && n < cap - 1; i++) out[n++] = dch(p->path[i]);
	if (n == 0) out[n++] = '.';
	out[n] = 0;
}

// ============================ TEST 1: both behaviour tables ============================
static void t1_tables(void) {
	printf("T1  behaviour -> kind, every row, per engine (a SHARED table fails here)\n");
	struct Row { int beh; FpKind k; int d; };
	// RSE (pokeemerald). Values from the enum in include/constants/metatile_behaviors.h, whose
	// own MB_UNUSED_xx names pin the numbering; membership from IsWarpMetatileBehavior /
	// IsArrowWarpMetatileBehavior / TryDoorWarp in src/field_control_avatar.c.
	static const struct Row RSE[] = {
		{ 0x0E, FP_WK_STEP, FP_NODIR }, { 0x0F, FP_WK_STEP, FP_NODIR },
		{ 0x1B, FP_WK_DIR,  FP_U },     { 0x1C, FP_WK_DIR,  FP_D },
		{ 0x29, FP_WK_STEP, FP_NODIR },
		{ 0x60, FP_WK_STEP, FP_NODIR }, { 0x61, FP_WK_STEP, FP_NODIR },
		{ 0x62, FP_WK_DIR,  FP_R },     { 0x63, FP_WK_DIR,  FP_L },
		{ 0x64, FP_WK_DIR,  FP_U },     { 0x65, FP_WK_DIR,  FP_D },
		{ 0x66, FP_WK_NONE, FP_NODIR },                        // MB_CRACKED_FLOOR_HOLE: a script hole
		{ 0x67, FP_WK_STEP, FP_NODIR }, { 0x68, FP_WK_STEP, FP_NODIR },
		{ 0x69, FP_WK_DOOR, FP_U },                            // MB_ANIMATED_DOOR
		{ 0x6A, FP_WK_STEP, FP_NODIR }, { 0x6B, FP_WK_STEP, FP_NODIR },
		{ 0x6C, FP_WK_STEP, FP_NODIR },                        // MB_WATER_DOOR   <- FRLG says DIR_E
		{ 0x6D, FP_WK_DIR,  FP_D },                            // MB_WATER_SOUTH_ARROW_WARP
		{ 0x6E, FP_WK_STEP, FP_NODIR },                        // MB_DEEP_SOUTH_WARP
		{ 0x6F, FP_WK_NONE, FP_NODIR },                        // MB_UNUSED_6F
		{ 0x70, FP_WK_STEP, FP_NODIR },                        // MB_BRIDGE_OVER_OCEAN = Union Room exit
		{ 0x71, FP_WK_NONE, FP_NODIR }, { 0x8D, FP_WK_NONE, FP_NODIR },   // script doors stay NONE
		{ 0xEA, FP_WK_NONE, FP_NODIR }, { 0x00, FP_WK_NONE, FP_NODIR },
	};
	// FRLG (pokefirered), #defines 0x60-0x71 — a DIFFERENT block.
	static const struct Row FRLG[] = {
		{ 0x0E, FP_WK_NONE, FP_NODIR }, { 0x0F, FP_WK_NONE, FP_NODIR },   // RSE-only warps
		{ 0x1B, FP_WK_NONE, FP_NODIR }, { 0x1C, FP_WK_NONE, FP_NODIR },
		{ 0x29, FP_WK_NONE, FP_NODIR },
		{ 0x60, FP_WK_STEP, FP_NODIR },                        // MB_CAVE_DOOR
		{ 0x61, FP_WK_STEP, FP_NODIR },
		{ 0x62, FP_WK_DIR,  FP_R },     { 0x63, FP_WK_DIR,  FP_L },
		{ 0x64, FP_WK_DIR,  FP_U },     { 0x65, FP_WK_DIR,  FP_D },
		{ 0x66, FP_WK_STEP, FP_NODIR },                        // MB_FALL_WARP    <- RSE says NONE
		{ 0x67, FP_WK_STEP, FP_NODIR }, { 0x68, FP_WK_STEP, FP_NODIR },
		{ 0x69, FP_WK_DOOR, FP_U },
		{ 0x6A, FP_WK_STEP, FP_NODIR }, { 0x6B, FP_WK_STEP, FP_NODIR },
		{ 0x6C, FP_WK_DIR,  FP_R },     { 0x6D, FP_WK_DIR,  FP_L },   // the four divergences
		{ 0x6E, FP_WK_DIR,  FP_R },     { 0x6F, FP_WK_DIR,  FP_L },
		{ 0x70, FP_WK_NONE, FP_NODIR },                        // RSE-only
		{ 0x71, FP_WK_STEP, FP_NODIR },                        // MB_UNION_ROOM_WARP
		{ 0x00, FP_WK_NONE, FP_NODIR },
	};
	for (unsigned i = 0; i < sizeof RSE / sizeof RSE[0]; i++) {
		int d = 99; FpKind k = fieldpath_kind_for_behaviour(FP_ENG_RSE, RSE[i].beh, &d);
		CHECK(k == RSE[i].k && d == RSE[i].d, "RSE 0x%02X -> %s/%c, want %s/%c",
		      RSE[i].beh, KN[k], dch(d), KN[RSE[i].k], dch(RSE[i].d));
	}
	for (unsigned i = 0; i < sizeof FRLG / sizeof FRLG[0]; i++) {
		int d = 99; FpKind k = fieldpath_kind_for_behaviour(FP_ENG_FRLG, FRLG[i].beh, &d);
		CHECK(k == FRLG[i].k && d == FRLG[i].d, "FRLG 0x%02X -> %s/%c, want %s/%c",
		      FRLG[i].beh, KN[k], dch(d), KN[FRLG[i].k], dch(FRLG[i].d));
	}
	// The four tiles that make a shared table a bug, stated as an explicit inequality.
	for (int b = 0x6C; b <= 0x6F; b++) {
		int da = 9, db = 9;
		FpKind ka = fieldpath_kind_for_behaviour(FP_ENG_RSE, b, &da);
		FpKind kb = fieldpath_kind_for_behaviour(FP_ENG_FRLG, b, &db);
		CHECK(ka != kb || da != db, "0x%02X must differ between engines (got %s/%c both)", b, KN[ka], dch(da));
	}
	// A negative behaviour (unreadable) is never a warp.
	int d = 9;
	CHECK(fieldpath_kind_for_behaviour(FP_ENG_RSE, -1, &d) == FP_WK_NONE && d == FP_NODIR, "beh<0 -> NONE");
	CHECK(fieldpath_kind_for_behaviour(FP_ENG_FRLG, -1, &d) == FP_WK_NONE, "beh<0 -> NONE (FRLG)");
}

// ================= TEST 2: the behaviour read walks the real chain ====================
static void t2_chain(void) {
	printf("T2  metatile behaviour via the real MapHeader->MapLayout->Tileset->attrs chain\n");
	FpBus bus; FpMap m;
	bind("mauville", &bus, &m);
	// Mauville City's six Pokemon-Center-style doors, straight out of the ROM's warp table.
	static const int DOORS[6][2] = { {8,5}, {22,5}, {35,5}, {23,14}, {32,14}, {19,14} };
	for (int i = 0; i < 6; i++)
		CHECK(fieldpath_behaviour_at(&bus, &m, DOORS[i][0], DOORS[i][1]) == 0x69,
		      "mauville (%d,%d) should read MB_ANIMATED_DOOR 0x69, got 0x%02X",
		      DOORS[i][0], DOORS[i][1], fieldpath_behaviour_at(&bus, &m, DOORS[i][0], DOORS[i][1]));
	CHECK(fieldpath_behaviour_at(&bus, &m, 8, 13) == 0x60, "the Game Corner door is MB_NON_ANIMATED_DOOR");
	CHECK(fieldpath_behaviour_at(&bus, &m, 22, 6) == 0x00, "the tile below the PC door is plain ground");

	bind("pc1f", &bus, &m);
	CHECK(fieldpath_behaviour_at(&bus, &m, 6, 8) == 0x65, "PC 1F exit mat = MB_SOUTH_ARROW_WARP");
	CHECK(fieldpath_behaviour_at(&bus, &m, 7, 8) == 0x65, "PC 1F exit mat (2nd tile)");
	CHECK(fieldpath_behaviour_at(&bus, &m, 1, 6) == 0x6A, "PC 1F up escalator = MB_UP_ESCALATOR");

	bind("frstair", &bus, &m);   // FRLG: 640 primary metatiles, u32 attrs at Tileset+0x14, mask 0x1FF
	CHECK(fieldpath_behaviour_at(&bus, &m, 3, 7) == 0x6F, "FR stair = MB_DOWN_LEFT_STAIR_WARP 0x6F, got 0x%02X",
	      fieldpath_behaviour_at(&bus, &m, 3, 7));
}

// ================== TEST 3: the DOOR retarget on real geometry =======================
static void t3_door(void) {
	printf("T3  DOOR: goal retargets to the SOUTH approach, path ends there, terminal = UP\n");
	FpBus bus; FpMap m; FpPlan p; char s[128];
	bind("mauville", &bus, &m);

	// The user's exact scenario: standing beside the door, tapping the door.
	static const struct { int sx, sy; const char* want; } CASES[] = {
		{ 23, 6, "L"     },   // one tile east of the approach
		{ 21, 6, "R"     },   // one tile west
		{ 22, 6, "."     },   // already on it: the ZERO-STEP plan
		{ 24, 8, "LLUU"  },   // two rows down, two columns east
		{ 19, 10,NULL    },   // far away: only the endpoint is asserted
	};
	for (unsigned i = 0; i < sizeof CASES / sizeof CASES[0]; i++) {
		bool ok = fieldpath_plan(&bus, &m, CASES[i].sx, CASES[i].sy, 22, 5, NULL, 0, &p);
		pstr(&p, s, sizeof s);
		CHECK(ok, "(%d,%d)->door(22,5) should plan (outcome %d)", CASES[i].sx, CASES[i].sy, p.outcome);
		CHECK(p.kind == FP_WK_DOOR, "kind should be DOOR, got %s", KN[p.kind]);
		CHECK(p.termDir == FP_U, "a door is bumped from the SOUTH -> hold UP, got %c", dch(p.termDir));
		CHECK(p.approachX == 22 && p.approachY == 6, "approach should be (22,6), got (%d,%d)", p.approachX, p.approachY);
		CHECK(p.goalX == 22 && p.goalY == 5, "goal stays the door tile");
		CHECK(p.warpGroup == 10 && p.warpNum == 5, "the warp event confirms MauvilleCity_PokemonCenter_1F, got %d/%d",
		      p.warpGroup, p.warpNum);
		if (CASES[i].want) CHECK(!strcmp(s, CASES[i].want), "(%d,%d): path '%s', want '%s'",
		                         CASES[i].sx, CASES[i].sy, s, CASES[i].want);
	}
	// EVERY door in the city retargets, and none of them ends on the door tile itself.
	static const int DOORS[6][2] = { {8,5}, {22,5}, {35,5}, {23,14}, {32,14}, {19,14} };
	for (int i = 0; i < 6; i++) {
		bool ok = fieldpath_plan(&bus, &m, DOORS[i][0], DOORS[i][1] + 3, DOORS[i][0], DOORS[i][1], NULL, 0, &p);
		CHECK(ok && p.kind == FP_WK_DOOR && p.termDir == FP_U, "door (%d,%d) plans", DOORS[i][0], DOORS[i][1]);
		CHECK(p.approachY == DOORS[i][1] + 1 && p.approachX == DOORS[i][0],
		      "door (%d,%d): approach (%d,%d)", DOORS[i][0], DOORS[i][1], p.approachX, p.approachY);
	}
	// The BEFORE behaviour, asserted so the regression is visible: with the goal left on the door
	// tile the last step is into it and the router would just hold whatever direction that was.
	bool ok = fieldpath_plan(&bus, &m, 22, 6, 22, 5, NULL, 0, &p);
	CHECK(ok && p.pathLen == 0, "the zero-step case must still be a legal plan (pathLen %d)", p.pathLen);
}

// ========================= TEST 4: the zero-step plan =================================
static void t4_zero(void) {
	printf("T4  zero-step plans: standing on the approach / on the warp tile\n");
	FpBus bus; FpMap m; FpPlan p;
	bind("mauville", &bus, &m);
	CHECK(fieldpath_plan(&bus, &m, 22, 6, 22, 5, NULL, 0, &p), "door from its own approach tile");
	CHECK(p.pathLen == 0 && p.termDir == FP_U && p.ok, "zero steps + hold UP");
	bind("pc1f", &bus, &m);
	CHECK(fieldpath_plan(&bus, &m, 6, 8, 6, 8, NULL, 0, &p), "standing ON the exit mat");
	CHECK(p.pathLen == 0 && p.kind == FP_WK_DIR && p.termDir == FP_D, "zero steps + hold DOWN (%s/%c)",
	      KN[p.kind], dch(p.termDir));
}

// ================== TEST 5: a DOOR with no walkable approach ==========================
static void t5_noapproach(void) {
	printf("T5  DOOR whose south tile is not enterable -> NO plan, no keys, no wall tackle\n");
	FpBus bus; FpMap m; FpPlan p;
	bind("mauville", &bus, &m);
	// An NPC parked on the approach mat is the live version of this (T1.5 treats NPCs as walls).
	short npc[1][2] = { { 22 + 7, 6 + 7 } };
	bool ok = fieldpath_plan(&bus, &m, 24, 6, 22, 5, npc, 1, &p);
	CHECK(!ok, "an occupied approach must NOT produce a route");
	CHECK(p.outcome == FP_OUT_NO_APPROACH, "outcome should be NO_APPROACH, got %d", p.outcome);
	CHECK(p.pathLen == 0 && p.termDir == FP_U, "nothing to emit");
	// The same door with the mat free still plans — i.e. the refusal is about the NPC, not the door.
	CHECK(fieldpath_plan(&bus, &m, 24, 6, 22, 5, NULL, 0, &p) && p.outcome == FP_OUT_PLANNED,
	      "with the mat free the same tap plans");
	// A door boxed in by its own map (2F's Cable Club doors sit under a wall row) still has a
	// legal southern approach — prove the refusal is not just "every door in a corridor".
	bind("pc2f", &bus, &m);
	CHECK(fieldpath_plan(&bus, &m, 7, 2, 5, 1, NULL, 0, &p) && p.approachY == 2,
	      "PC 2F Cable Club door approach is (5,2)");
	// An unreachable goal: tap a door on the far side of a wall the player cannot get around.
	bind("pc1f", &bus, &m);
	short wall[3][2] = { { 1 + 7, 5 + 7 }, { 2 + 7, 6 + 7 }, { 1 + 7, 7 + 7 } };
	ok = fieldpath_plan(&bus, &m, 6, 6, 1, 6, wall, 3, &p);
	CHECK(!ok && p.outcome == FP_OUT_UNREACHABLE, "walled-off escalator -> UNREACHABLE (ok=%d out=%d)", ok, p.outcome);
	// Outside the search window -> WINDOW, never a half-built route.
	CHECK(!fieldpath_plan(&bus, &m, 6, 6, 6, 6 + FP_WHALF + 1, NULL, 0, &p) && p.outcome == FP_OUT_WINDOW,
	      "beyond +-WHALF -> WINDOW");
}

// ================= TEST 6: the DIR_S exit mat (100 % broken before) ===================
static void t6_mat(void) {
	printf("T6  DIR: the building exit mat routes ONTO the tile and holds the arrow direction\n");
	FpBus bus; FpMap m; FpPlan p; char s[128];
	bind("pc1f", &bus, &m);
	bool ok = fieldpath_plan(&bus, &m, 6, 6, 6, 8, NULL, 0, &p);
	pstr(&p, s, sizeof s);
	CHECK(ok && p.kind == FP_WK_DIR, "the exit mat classifies DIR, got %s", KN[p.kind]);
	CHECK(p.termDir == FP_D, "MB_SOUTH_ARROW_WARP -> hold DOWN, got %c", dch(p.termDir));
	CHECK(p.approachX == 6 && p.approachY == 8, "a DIR warp is stood ON, not approached from the south");
	CHECK(!strcmp(s, "DD"), "path '%s', want 'DD'", s);
	CHECK(p.warpGroup == 0 && p.warpNum == 2, "the mat warps to MauvilleCity (0/2), got %d/%d", p.warpGroup, p.warpNum);
	ok = fieldpath_plan(&bus, &m, 6, 6, 7, 8, NULL, 0, &p);
	CHECK(ok && p.termDir == FP_D, "the second mat tile behaves identically");
}

// ========================== TEST 7: STEP warps ========================================
static void t7_step(void) {
	printf("T7  STEP: escalators / ladders need NO terminal hold\n");
	FpBus bus; FpMap m; FpPlan p; char s[128];
	bind("pc2f", &bus, &m);
	bool ok = fieldpath_plan(&bus, &m, 3, 6, 1, 6, NULL, 0, &p);
	pstr(&p, s, sizeof s);
	CHECK(ok && p.kind == FP_WK_STEP, "the down escalator classifies STEP, got %s", KN[p.kind]);
	CHECK(p.termDir == FP_NODIR, "a step warp fires on arrival: termDir must be none, got %c", dch(p.termDir));
	CHECK(p.approachX == 1 && p.approachY == 6, "the escalator tile IS the terminal");
	CHECK(!strcmp(s, "LL"), "path '%s', want 'LL'", s);
	CHECK(p.warpGroup == 10 && p.warpNum == 5, "the escalator goes to 1F (10/5), got %d/%d", p.warpGroup, p.warpNum);
	bind("mauville", &bus, &m);
	ok = fieldpath_plan(&bus, &m, 8, 15, 8, 13, NULL, 0, &p);
	CHECK(ok && p.kind == FP_WK_STEP && p.termDir == FP_NODIR,
	      "MB_NON_ANIMATED_DOOR (the Game Corner) is a STEP warp, not a north bump (%s/%c)",
	      KN[p.kind], dch(p.termDir));
}

// ==================== TEST 8: FRLG directional stair warps ============================
static void t8_stairs(void) {
	printf("T8  FRLG-only: a directional stair warp holds EAST/WEST, and RSE must not\n");
	FpBus bus; FpMap m; FpPlan p;
	bind("frstair", &bus, &m);
	bool ok = fieldpath_plan(&bus, &m, 5, 7, 3, 7, NULL, 0, &p);
	CHECK(ok && p.kind == FP_WK_DIR, "0x6F classifies DIR on FRLG, got %s", KN[p.kind]);
	CHECK(p.termDir == FP_L, "MB_DOWN_LEFT_STAIR_WARP -> hold WEST, got %c", dch(p.termDir));
	CHECK(p.approachX == 3 && p.approachY == 7, "stood on, not approached");
	CHECK(p.warpGroup == 1 && p.warpNum == 6, "the stair's own warp event (%d/%d)", p.warpGroup, p.warpNum);
	// The SAME behaviour byte under the RSE table is MB_UNUSED_6F = not a warp at all.
	int d = 9;
	CHECK(fieldpath_kind_for_behaviour(FP_ENG_RSE, 0x6F, &d) == FP_WK_NONE,
	      "0x6F must be NOTHING on RSE (this is why the tables are separate)");
}

// ============= TEST 9: elevation — the router must not walk into the pond =============
static void t9_water(void) {
	printf("T9  elevation: Route 117's pond is collision-0 and must still be impassable\n");
	FpBus bus; FpMap m; FpPlan p; char s[128];
	// Ground truth first: the water tiles really are collision 0 (which is why the old
	// collision-only walkable() sailed straight over them).
	bind("route117", &bus, &m);
	for (int x = 26; x <= 30; x++) {
		uint16_t w = fx_r16((void*)g_fx, m.gridPtr + 2u * (uint32_t)((x + 7) + m.backupW * (6 + 7)));
		CHECK(((w & 0x0C00) >> 10) == 0, "(%d,6) collision must be 0 (it is water)", x);
		CHECK(((w & 0xF000) >> 12) == 1, "(%d,6) elevation must be 1 (water)", x);
	}
	// BEFORE: elevation disarmed (pElev 0) -> the straight line across the pond, reproduced.
	bind_elev("route117", &bus, &m, 0);
	CHECK(fieldpath_plan(&bus, &m, 25, 6, 31, 6, NULL, 0, &p), "the legacy plan exists");
	pstr(&p, s, sizeof s);
	CHECK(!strcmp(s, "RRRRRR"), "BEFORE (pElev=0) should be the straight swim 'RRRRRR', got '%s'", s);
	// AFTER: the walking player is elevation 3 -> a land detour, and NOT one water tile on it.
	bind_elev("route117", &bus, &m, 3);
	CHECK(fieldpath_plan(&bus, &m, 25, 6, 31, 6, NULL, 0, &p), "the land route exists");
	pstr(&p, s, sizeof s);
	CHECK(p.pElev == 3, "the plan should use pElev 3, got %d", p.pElev);
	CHECK(strcmp(s, "RRRRRR") != 0, "AFTER must not be the straight swim");
	{   // walk the path and assert every tile it crosses is land
		int x = 25, y = 6; static const int DX[4] = { 1, -1, 0, 0 }, DY[4] = { 0, 0, 1, -1 };
		for (int i = 0; i < p.pathLen; i++) {
			x += DX[(int)p.path[i]]; y += DY[(int)p.path[i]];
			uint16_t w = fx_r16((void*)g_fx, m.gridPtr + 2u * (uint32_t)((x + 7) + m.backupW * (y + 7)));
			CHECK(((w & 0xF000) >> 12) != 1, "step %d lands on water at (%d,%d)", i, x, y);
		}
		CHECK(x == 31 && y == 6, "the detour still arrives at (31,6), got (%d,%d)", x, y);
	}
	// A SURFING player (elevation 1) gets the opposite answer for free: the water IS its road.
	bind_elev("route117", &bus, &m, 1);
	CHECK(fieldpath_plan(&bus, &m, 26, 6, 30, 6, NULL, 0, &p), "surfing across the pond plans");
	pstr(&p, s, sizeof s);
	CHECK(!strcmp(s, "RRRR"), "a surfer crosses the pond directly, got '%s'", s);
	// The self-consistency rail: an elevation the player's OWN tile fails disarms the filter
	// rather than making the whole map unreachable.
	bind_elev("route117", &bus, &m, 9);
	CHECK(fieldpath_plan(&bus, &m, 25, 6, 31, 6, NULL, 0, &p) && p.pElev == 0,
	      "an impossible pElev must disarm to 0, got %d", p.pElev);
	g_forceElev = 0xFF;
}

// ===================== TEST 10: the degradation ladder ================================
static void t10_degrade(void) {
	printf("T10 degradation: no gMapHeader / bad pointers / out of the map -> NONE, never a guess\n");
	FpBus bus; FpMap m; FpPlan p;
	bind("mauville", &bus, &m);
	uint32_t realMH = m.mapHeader;

	m.mapHeader = 0;             // a profile with no verified address (the house rule's degradation)
	CHECK(fieldpath_behaviour_at(&bus, &m, 22, 5) == -1, "no mapHeader -> behaviour unavailable");
	FpClass c = fieldpath_classify(&bus, &m, 22, 5);
	CHECK(c.kind == FP_WK_NONE && c.termDir == FP_NODIR, "no mapHeader -> legacy behaviour, no warp claim");
	CHECK(fieldpath_plan(&bus, &m, 22, 6, 22, 5, NULL, 0, &p) && p.kind == FP_WK_NONE,
	      "and the plan still works, exactly as it does today");

	m.mapHeader = 0x02FFFF00u;   // maps to nothing -> layout pointer reads 0 -> not ROM
	CHECK(fieldpath_behaviour_at(&bus, &m, 22, 5) == -1, "a non-ROM MapLayout pointer -> unavailable");

	m.mapHeader = realMH;
	// Outside the CURRENT map's bounds the tile belongs to a connected map and its metatile id
	// indexes the wrong tilesets — refuse instead of answering confidently wrong.
	int mw = m.backupW - 15, mh = m.backupH - 14;
	CHECK(fieldpath_behaviour_at(&bus, &m, -1, 5) == -1, "x<0 -> unavailable");
	CHECK(fieldpath_behaviour_at(&bus, &m, mw, 5) == -1, "x>=width -> unavailable");
	CHECK(fieldpath_behaviour_at(&bus, &m, 5, mh) == -1, "y>=height -> unavailable");
	CHECK(fieldpath_behaviour_at(&bus, &m, 5, -1) == -1, "y<0 -> unavailable");

	// A bad map descriptor never produces a route.
	FpMap bad = m; bad.gridPtr = 0x08000000u;
	CHECK(!fieldpath_plan(&bus, &bad, 22, 6, 22, 5, NULL, 0, &p) && p.outcome == FP_OUT_BADMAP, "grid not in EWRAM");
	bad = m; bad.backupW = 0;
	CHECK(!fieldpath_plan(&bus, &bad, 22, 6, 22, 5, NULL, 0, &p) && p.outcome == FP_OUT_BADMAP, "width 0");
	bad = m; bad.backupH = 9999;
	CHECK(!fieldpath_plan(&bus, &bad, 22, 6, 22, 5, NULL, 0, &p) && p.outcome == FP_OUT_BADMAP, "absurd height");
	// mapObjects unset -> pElev 0 -> the elevation clause is a no-op (today's behaviour).
	FpMap noobj = m; noobj.mapObjects = 0;
	CHECK(fieldpath_player_elev(&bus, &noobj) == 0, "no gObjectEvents -> elevation rule disarmed");
}

// ============ TEST 11: warp-event confirmation may DOWNGRADE, never PROMOTE ===========
static void t11_confirm(void) {
	printf("T11 the warp EVENT confirms; it never promotes a non-warp tile\n");
	FpBus bus; FpMap m;
	bind("mauville", &bus, &m);
	// Mauville's warp list also carries plain-ground arrival destinations. Whatever the list
	// says, a tile whose BEHAVIOUR is not a warp must stay FP_WK_NONE.
	for (int y = 0; y < m.backupH - 14; y++) {
		for (int x = 0; x < m.backupW - 15; x++) {
			int beh = fieldpath_behaviour_at(&bus, &m, x, y);
			int d = 9;
			FpKind tbl = fieldpath_kind_for_behaviour(FP_ENG_RSE, beh, &d);
			FpClass c = fieldpath_classify(&bus, &m, x, y);
			if (tbl == FP_WK_NONE && !c.headRetarget)
				CHECK(c.kind == FP_WK_NONE, "(%d,%d) beh 0x%02X is not a warp behaviour but classified %s",
				      x, y, beh, KN[c.kind]);
			if (c.kind != FP_WK_NONE)
				CHECK(c.warpGroup >= 0 && c.warpNum >= 0, "(%d,%d) classified %s without a confirming warp event",
				      x, y, KN[c.kind]);
		}
	}
	// PC 2F has doors AND ordinary walls; scan it too (a different tileset pair).
	bind("pc2f", &bus, &m);
	int doors = 0;
	for (int y = 0; y < m.backupH - 14; y++)
		for (int x = 0; x < m.backupW - 15; x++) {
			FpClass c = fieldpath_classify(&bus, &m, x, y);
			if (c.kind == FP_WK_DOOR && !c.headRetarget) doors++;
		}
	CHECK(doors == 2, "PC 2F has exactly the two Cable Club doors, found %d", doors);
}

// =============== TEST 12: the two-metatile-tall door graphic =========================
static void t12_head(void) {
	printf("T12 tapping the WALL directly above a door takes the door (the graphic is 2 tall)\n");
	FpBus bus; FpMap m; FpPlan p;
	bind("mauville", &bus, &m);
	// (22,4) is ordinary impassable wall — the top half of the Pokemon Center's doorway.
	CHECK(fieldpath_behaviour_at(&bus, &m, 22, 4) == 0x00, "the tile above the door is plain wall");
	FpClass c = fieldpath_classify(&bus, &m, 22, 4);
	CHECK(c.kind == FP_WK_DOOR && c.headRetarget, "it should retarget onto the door below (%s head=%d)",
	      KN[c.kind], (int)c.headRetarget);
	CHECK(c.goalX == 22 && c.goalY == 5 && c.approachY == 6, "goal (%d,%d) approach (%d,%d)",
	      c.goalX, c.goalY, c.approachX, c.approachY);
	CHECK(fieldpath_plan(&bus, &m, 24, 6, 22, 4, NULL, 0, &p) && p.termDir == FP_U && p.approachY == 6,
	      "and the plan is the same route as tapping the door itself");
	// The retarget only fires on an IMPASSABLE non-warp tile: ordinary walkable ground one tile
	// above a door must NOT be hijacked.
	bind("pc2f", &bus, &m);
	// (5,2) is the walkable approach of the door at (5,1); the tile above the door is (5,0).
	FpClass w = fieldpath_classify(&bus, &m, 5, 2);
	CHECK(w.kind == FP_WK_NONE, "a walkable tile above nothing stays NONE");
	FpClass h = fieldpath_classify(&bus, &m, 5, 0);
	CHECK(h.kind == FP_WK_DOOR && h.headRetarget && h.goalY == 1, "the wall above the Cable Club door retargets");
	// A speculative look at the neighbour must never leak into the diagnostics: if the retarget
	// does not survive the warp-event confirmation, the row must report the TAPPED tile's own
	// behaviour. (A log line reading "beh=0x69, kind=none" would send the next hardware
	// investigation down the wrong path.) Every impassable non-warp tile in the city is a case.
	bind("mauville", &bus, &m);
	int checked = 0;
	for (int y = 0; y < m.backupH - 14; y++)
		for (int x = 0; x < m.backupW - 15; x++) {
			FpClass q = fieldpath_classify(&bus, &m, x, y);
			if (q.kind != FP_WK_NONE) continue;
			int own = fieldpath_behaviour_at(&bus, &m, x, y);
			CHECK(q.behaviour == own, "(%d,%d) NONE row reports beh 0x%02X but the tile reads 0x%02X",
			      x, y, q.behaviour, own);
			checked++;
		}
	CHECK(checked > 500, "the scan should cover the whole map, covered %d", checked);
}

// =============== TEST 13: legacy FP_WK_NONE behaviour is unchanged ====================
static void t13_legacy(void) {
	printf("T13 a non-warp goal keeps the OLD contract, bit for bit\n");
	FpBus bus; FpMap m; FpPlan p; char s[128];
	bind("mauville", &bus, &m);
	// A blocked, non-warp goal is still a legal terminal (signs / NPCs / cuttable trees).
	CHECK(fieldpath_plan(&bus, &m, 19, 6, 19, 5, NULL, 0, &p), "a blocked non-warp goal still plans");
	CHECK(p.kind == FP_WK_NONE && p.termDir == FP_NODIR, "no warp claim, no terminal hold");
	CHECK(p.approachX == 19 && p.approachY == 5, "the terminal IS the blocked tile, as before");
	// Ordinary open-ground routing is untouched, tie-break included: on an open row the
	// R,L,D,U child order still yields the horizontal-first shortest path.
	CHECK(fieldpath_plan(&bus, &m, 20, 8, 24, 8, NULL, 0, &p), "plain walk east");
	pstr(&p, s, sizeof s);
	CHECK(!strcmp(s, "RRRR"), "path '%s', want 'RRRR'", s);
	CHECK(fieldpath_plan(&bus, &m, 24, 8, 20, 8, NULL, 0, &p), "plain walk west");
	pstr(&p, s, sizeof s);
	CHECK(!strcmp(s, "LLLL"), "path '%s', want 'LLLL'", s);
	CHECK(fieldpath_plan(&bus, &m, 20, 8, 22, 6, NULL, 0, &p), "up-left/up-right diagonal");
	pstr(&p, s, sizeof s);
	CHECK(!strcmp(s, "RRUU"), "horizontal-first tie-break preserved: '%s', want 'RRUU'", s);
}

// =============== TEST 14: the stall rule (FIX PASS, review finding 1) =================
// The follow loop in touch.c may re-plan when the avatar stops making progress, because NPCs are
// sampled once at plan time. But an FP_WK_NONE route's terminal is a BLOCKED tile on purpose
// (T13 above: "the terminal IS the blocked tile"), so its last step can never complete and the
// stall there is the route ARRIVING. Re-planning there always succeeds — the same
// blocked-goal-allowed BFS re-finds a one-step path from the tile we already stand on — so the
// bounded replan spent REPLAN_MAX x 25 frames pushing the avatar into the sign before the A press
// the route exists for. This asserts the rule, and pairs it with a PLAN so the premise ("the
// player can never reach a WK_NONE approach") is measured rather than assumed.
static void t14_stall_rule(void) {
	printf("T14 the stall at a blocked WK_NONE terminal is arrival, not an obstruction\n");
	FpBus bus; FpMap m; FpPlan p;
	bind("mauville", &bus, &m);

	// the premise: a WK_NONE plan ends ON the blocked tile, which is NOT enterable
	CHECK(fieldpath_plan(&bus, &m, 20, 8, 20, 4, NULL, 0, &p), "plan to a blocked sign-like tile");
	CHECK(p.kind == FP_WK_NONE, "kind is NONE");
	CHECK(p.approachX == 20 && p.approachY == 4, "approach IS the blocked tile");
	CHECK(p.pathLen == 4, "and there are steps to walk first (%d)", p.pathLen);
	CHECK(!fieldpath_enterable(&bus, &m, p.approachX, p.approachY, p.pElev),
	      "and the follow loop can therefore NEVER stand on it");
	// so at the last step (pathPos == pathLen-1) the router must stop replanning...
	CHECK(!fieldpath_should_replan(FP_WK_NONE, p.pathLen - 1, p.pathLen),
	      "no replan at the WK_NONE terminal (pathLen %d)", p.pathLen);
	CHECK(!fieldpath_should_replan(FP_WK_NONE, p.pathLen, p.pathLen), "nor past it");
	// ...but a stall EARLIER in the same route is a real obstruction and must still replan
	for (int i = 0; i < p.pathLen - 1; i++)
		CHECK(fieldpath_should_replan(FP_WK_NONE, i, p.pathLen),
		      "mid-route stall at step %d/%d must still replan", i, p.pathLen);
	// the zero-step WK_NONE plan (tapped the tile we already stand next to with nothing to walk)
	CHECK(!fieldpath_should_replan(FP_WK_NONE, 0, 0), "a zero-length WK_NONE plan never replans");
	// every OTHER kind is untouched — their approach is a tile the player CAN stand on, so a
	// stall there is a genuine obstruction and the replan is exactly right, terminal included.
	CHECK(fieldpath_plan(&bus, &m, 23, 6, 22, 5, NULL, 0, &p), "the Pokemon Center door");
	CHECK(p.kind == FP_WK_DOOR, "kind is DOOR");
	CHECK(fieldpath_enterable(&bus, &m, p.approachX, p.approachY, p.pElev), "its approach IS standable");
	for (int i = 0; i <= p.pathLen; i++) {
		CHECK(fieldpath_should_replan(FP_WK_DOOR, i, p.pathLen), "DOOR replans at %d", i);
		CHECK(fieldpath_should_replan(FP_WK_DIR,  i, p.pathLen), "DIR replans at %d", i);
		CHECK(fieldpath_should_replan(FP_WK_STEP, i, p.pathLen), "STEP replans at %d", i);
	}
}

int main(void) {
	printf("== test_fieldpath (phase-18 SPEC-door T4.12) ==\n");
	printf("   fixtures: %d real maps out of the user's own ROMs\n", FX_MAP_N);
	t1_tables();
	t2_chain();
	t3_door();
	t4_zero();
	t5_noapproach();
	t6_mat();
	t7_step();
	t8_stairs();
	t9_water();
	t10_degrade();
	t11_confirm();
	t12_head();
	t13_legacy();
	t14_stall_rule();
	printf("\n%d checks, %d failures\n", g_checks, g_fail);
	return g_fail ? 1 : 0;
}
