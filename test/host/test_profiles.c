// test_profiles.c — PC host unit test for the per-game RAM map (source/gamestate.c PROFILES[])
// and the phase-18 `sbDirect` save-block indirection it introduced.
//
//   clang -std=c11 -Wall -Wextra -O2 -I source -I test/host test/host/test_profiles.c \
//         source/gamestate.c source/presence_read.c source/presence.c source/peersprite.c \
//         -o /tmp/tpr && /tmp/tpr
//   (peersprite.c joined the link when presence_read.c grew its pspr_capture call — phase 20.)
//
// WHY THIS SUITE EXISTS AND WHY IT IS NOT A TABLE COPY (SPEC-coop P4.6.2). The spec offered two
// shapes: host-compile the real table, or copy it into the test and pin the copy with
// _Static_asserts. A copy is a second source of truth that drifts silently the first time someone
// edits one of them, which is the exact class of bug this file is meant to catch — so this suite
// LINKS THE REAL source/gamestate.c and drives it through a FAKE GBA BUS. Every assertion below is
// therefore about the shipping table, not about a restatement of it.
//
// gamestate.c needs exactly five symbols from the emulator (gbacore_read8/16/32, game_code,
// frame_counter) and presence_read.c the same three reads; all of them are stubbed here over a
// tiny byte-addressed sparse memory, so a "profile" can be exercised the way the app exercises it:
// put bytes at the addresses the row names, call game_read, and check what came back.
//
//   TEST 1  profile_for resolves every shipped code, and refuses everything else
//   TEST 2  sbDirect as a TABLE: pointer vs struct, per code, asserted against PROFILES' own data
//   TEST 3  sbDirect BEHAVIOUR: the deref happens iff sbDirect == 0            SPEC-coop P3.2.4
//   TEST 4  the RS rows carry NO ROM address except the one that is licensed   SPEC-coop P3.3.2
//   TEST 5  AXVE and AXPE are byte-identical apart from the code               SPEC-coop P3.5.1
//   TEST 6  every RS address equals the value read from pret's four sym maps   SPEC-coop P3.3.3
//   TEST 7  the RS identity latch rejects an all-zero name                     SPEC-coop P3.2.5
//   TEST 8  no shipped address is nonsense (address-space sanity, all rows)
//   TEST 9  the phase-20 peer-sprite columns, per game    phase20 SPEC S1.3
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "gamestate.h"
#include "presence_read.h"

static int g_checks = 0, g_fails = 0;
static void check(int ok, const char* fmt, ...) {
	g_checks++;
	if (!ok) {
		g_fails++;
		va_list ap; va_start(ap, fmt);
		printf("  [FAIL] "); vprintf(fmt, ap); printf("\n");
		va_end(ap);
	}
}
#define CHECK(c, ...) check((c) ? 1 : 0, __VA_ARGS__)
#define EQU(a, b, ...) do { uint32_t _a = (uint32_t)(a), _b = (uint32_t)(b); \
	check(_a == _b, __VA_ARGS__); \
	if (_a != _b) printf("         got 0x%08X want 0x%08X\n", _a, _b); } while (0)

// ============================================================================================
// The fake GBA bus. Byte-addressed, sparse, tiny — a test writes only the handful of bytes the
// assertion is about, and every unwritten address reads 0 (which is also the honest model: an
// address the app names but the game has not populated reads as zeroes on a real core too).
// ============================================================================================
#define FAKE_MAX 512
struct GbaCore {
	char     code[5];
	uint32_t addr[FAKE_MAX];
	uint8_t  val [FAKE_MAX];
	int      n;
	uint32_t frame;
};
static void bus_reset(GbaCore* c, const char* code) {
	memset(c, 0, sizeof *c);
	snprintf(c->code, sizeof c->code, "%s", code);
}
static void bus_w8(GbaCore* c, uint32_t a, uint8_t v) {
	for (int i = 0; i < c->n; i++) if (c->addr[i] == a) { c->val[i] = v; return; }
	if (c->n < FAKE_MAX) { c->addr[c->n] = a; c->val[c->n] = v; c->n++; }
}
static void bus_w32(GbaCore* c, uint32_t a, uint32_t v) {
	for (int i = 0; i < 4; i++) bus_w8(c, a + (uint32_t)i, (uint8_t)(v >> (8 * i)));
}
uint8_t gbacore_read8(GbaCore* c, uint32_t a) {
	if (!c) return 0;
	for (int i = 0; i < c->n; i++) if (c->addr[i] == a) return c->val[i];
	return 0;
}
uint16_t gbacore_read16(GbaCore* c, uint32_t a) {
	return (uint16_t)(gbacore_read8(c, a) | ((uint16_t)gbacore_read8(c, a + 1) << 8));
}
uint32_t gbacore_read32(GbaCore* c, uint32_t a) {
	return (uint32_t)gbacore_read16(c, a) | ((uint32_t)gbacore_read16(c, a + 2) << 16);
}
void gbacore_game_code(GbaCore* c, char out[5]) {
	if (!c) { out[0] = '\0'; return; }
	memcpy(out, c->code, 5);
}
uint32_t gbacore_frame_counter(GbaCore* c) { return c ? c->frame : 0; }

// ============================================================================================
// TEST 1 — profile_for resolves every shipped code, and only those
// ============================================================================================
static const char* const CODES[] = { "BPEE", "BPRE", "BPGE", "AXVE", "AXPE" };
#define NCODES ((int)(sizeof CODES / sizeof CODES[0]))

static const GameProfile* prof(const char* code) {
	static GbaCore c;
	bus_reset(&c, code);
	return profile_for(&c);
}

static void test_lookup(void) {
	printf("TEST 1: profile_for resolves every shipped game code, and refuses everything else\n");
	for (int i = 0; i < NCODES; i++) {
		const GameProfile* p = prof(CODES[i]);
		CHECK(p != NULL, "%s has a profile", CODES[i]);
		if (p) CHECK(!strncmp(p->code, CODES[i], 4), "%s resolves to its OWN row (got %s)",
		             CODES[i], p->code);
	}
	// P3.5.1's rule made mechanical: the codes must match on ALL FOUR bytes, never on a prefix.
	// "AXV_" and "AX__" are the shapes a careless prefix match would let through.
	CHECK(prof("AXV") == NULL, "a 3-char code matches nothing");
	CHECK(prof("AXVX") == NULL, "AXVX (one byte off AXVE) matches nothing");
	CHECK(prof("AXPX") == NULL, "AXPX (one byte off AXPE) matches nothing");
	CHECK(prof("BPEX") == NULL, "BPEX (one byte off BPEE) matches nothing");
	CHECK(prof("XXXX") == NULL, "an unknown code matches nothing");
	CHECK(prof("") == NULL, "the empty code matches nothing");
	CHECK(profile_for(NULL) == NULL, "a NULL core matches nothing");
	// ...and no two rows may claim the same code, which is how a duplicated row hides.
	for (int i = 0; i < NCODES; i++)
		for (int j = i + 1; j < NCODES; j++)
			CHECK(strncmp(CODES[i], CODES[j], 4) != 0, "%s and %s are distinct codes",
			      CODES[i], CODES[j]);
}

// ============================================================================================
// TEST 2 — sbDirect as a TABLE (SPEC-coop P4.6.2)
// "Is this game's save block a POINTER to deref, or the struct itself?", asserted for every
// shipped code against PROFILES[]'s own data — so a future row cannot get it wrong silently.
// ============================================================================================
static void test_sbdirect_table(void) {
	printf("TEST 2: sbDirect, per game code, against the shipped table\n");
	static const struct { const char* code; int direct; const char* why; } T[] = {
		{ "BPEE", 0, "Emerald has gSaveBlock1Ptr/gSaveBlock2Ptr" },
		{ "BPRE", 0, "FireRed has them too" },
		{ "BPGE", 0, "LeafGreen has them too" },
		{ "AXVE", 1, "Ruby has NO gSaveBlock1Ptr — the symbol IS the struct" },
		{ "AXPE", 1, "Sapphire is the same build as Ruby" },
	};
	for (int i = 0; i < (int)(sizeof T / sizeof T[0]); i++) {
		const GameProfile* p = prof(T[i].code);
		if (!p) { CHECK(0, "%s has no profile", T[i].code); continue; }
		EQU(p->sbDirect, T[i].direct, "%s sbDirect = %d (%s)", T[i].code, T[i].direct, T[i].why);
		// sbDirect is a BOOL in a uint8_t; anything else means a positional initialiser slipped.
		CHECK(p->sbDirect == 0 || p->sbDirect == 1, "%s sbDirect is 0 or 1", T[i].code);
		// A direct profile's sb1ptr/sb2ptr must be real EWRAM addresses (they are dereferenced as
		// data straight away), and they must not be equal to each other.
		if (p->sbDirect) {
			EQU(p->sb1ptr >> 24, 0x02u, "%s sb1ptr is EWRAM", T[i].code);
			EQU(p->sb2ptr >> 24, 0x02u, "%s sb2ptr is EWRAM", T[i].code);
			CHECK(p->sb1ptr != p->sb2ptr, "%s sb1 and sb2 are different blocks", T[i].code);
		}
	}
}

// ============================================================================================
// TEST 3 — sbDirect BEHAVIOUR through the real game_read (SPEC-coop P3.2.4)
// The point the spec makes in P3.2.2: get this wrong and sb1Valid is false FOREVER on RS, so
// presence reports OFF_FIELD on every frame and nothing ever draws. Both directions are proved.
// ============================================================================================
static void put_player(GbaCore* c, uint32_t sb1, int px, int py, int mg, int mn) {
	bus_w8(c, sb1 + 0, (uint8_t)(px & 0xFF));  bus_w8(c, sb1 + 1, (uint8_t)((px >> 8) & 0xFF));
	bus_w8(c, sb1 + 2, (uint8_t)(py & 0xFF));  bus_w8(c, sb1 + 3, (uint8_t)((py >> 8) & 0xFF));
	bus_w8(c, sb1 + 4, (uint8_t)mg);           bus_w8(c, sb1 + 5, (uint8_t)mn);
}

static void test_sbdirect_behaviour(void) {
	printf("TEST 3: the deref happens iff sbDirect == 0 — proved through the real game_read\n");

	// --- the POINTER case (Emerald). Bytes at sb1ptr are a POINTER; the player lives where it points.
	{
		GbaCore c; bus_reset(&c, "BPEE");
		const GameProfile* p = profile_for(&c);
		GameState gs;
		// Nothing written yet: the pointer reads 0, whose high byte is not 0x02 -> not ready.
		game_read(&c, p, &gs);
		CHECK(!gs.sb1Valid, "BPEE: an unpopulated gSaveBlock1Ptr is NOT valid");
		EQU((uint32_t)gs.px, (uint32_t)-1, "BPEE: no position while the pointer is null");
		// Now arm the pointer and put a player behind it.
		const uint32_t SB1 = 0x02025734u;   // any EWRAM block will do; the pointer is what is tested
		bus_w32(&c, p->sb1ptr, SB1);
		put_player(&c, SB1, 11, 7, 3, 9);
		game_read(&c, p, &gs);
		CHECK(gs.sb1Valid, "BPEE: a pointer into EWRAM IS valid");
		EQU((uint32_t)gs.px, 11u, "BPEE: px comes from *(sb1ptr) + 0");
		EQU((uint32_t)gs.py, 7u,  "BPEE: py comes from *(sb1ptr) + 2");
		EQU((uint32_t)gs.mapGroup, 3u, "BPEE: mapGroup from *(sb1ptr) + 4");
		EQU((uint32_t)gs.mapNum,   9u, "BPEE: mapNum   from *(sb1ptr) + 5");
		// THE DISCRIMINATOR: bytes written AT sb1ptr itself must NOT be read as a position.
		CHECK(!(gs.px == (int)(SB1 & 0xFFFF)), "BPEE: sb1ptr's own bytes are not the position");
	}

	// --- the DIRECT case (Ruby). The struct IS at sb1ptr; there is no pointer anywhere.
	{
		GbaCore c; bus_reset(&c, "AXVE");
		const GameProfile* p = profile_for(&c);
		GameState gs;
		put_player(&c, p->sb1ptr, 14, 9, 5, 1);
		game_read(&c, p, &gs);
		CHECK(gs.sb1Valid, "AXVE: a direct EWRAM block is 'mapped' (P3.2.4 — mapped, not loaded)");
		EQU((uint32_t)gs.px, 14u, "AXVE: px comes from sb1ptr + 0 with NO deref");
		EQU((uint32_t)gs.py, 9u,  "AXVE: py comes from sb1ptr + 2");
		EQU((uint32_t)gs.mapGroup, 5u, "AXVE: mapGroup from sb1ptr + 4");
		EQU((uint32_t)gs.mapNum,   1u, "AXVE: mapNum   from sb1ptr + 5");
		// THE REGRESSION THIS TEST EXISTS FOR: if the deref came back, the app would read
		// (px | py<<16) as a pointer, that would fail the 0x02 test, and sb1Valid would be false
		// forever — the P3.2.2 failure, verbatim.
		uint32_t asPointer = gbacore_read32(&c, p->sb1ptr);
		CHECK((asPointer >> 24) != 0x02u,
		      "AXVE: the bytes at sb1ptr do NOT look like a pointer (so a lost sbDirect would break it)");
	}
	// Sapphire behaves identically — it is the same build, and the row is pinned to Ruby's.
	{
		GbaCore c; bus_reset(&c, "AXPE");
		const GameProfile* p = profile_for(&c);
		GameState gs;
		put_player(&c, p->sb1ptr, 2, 3, 0, 0);
		game_read(&c, p, &gs);
		CHECK(gs.sb1Valid, "AXPE: direct block valid");
		EQU((uint32_t)gs.px, 2u, "AXPE: px direct");
		EQU((uint32_t)gs.py, 3u, "AXPE: py direct");
	}
}

// ============================================================================================
// TEST 4 — the RS rows carry no ROM address except the licensed one (SPEC-coop P3.3.2)
// Ruby vs Sapphire vs rev0 vs rev1 agree on every RAM symbol and disagree on ~90 000 ROM ones,
// so an 0x08... value in an RS row is wrong for three of the four cartridges it will meet. The
// ONE exception in the walked span is battleMainCb, which is identical in all four maps and is
// load-bearing (and since lane B, live-verified in a real Ruby battle — LANE-B-RS.md §1).
// The phase-22.0 cb2Title/cb2FullUi lists sit OUTSIDE this span and carry the lane-B exception:
// PER-TITLE ROM values live-read on the rev-2 fixtures (= the user's carts) — TEST 11 pins them.
// ============================================================================================
static void test_rs_no_rom(void) {
	printf("TEST 4: no ROM address in the RS rows except the licensed battleMainCb\n");
	const char* names[] = {
		"sb1ptr","battleFlags","actionCursor","moveCursor","battleMons","bg0y","partyMenu",
		"partyCount","mainCb2","cb2UpdParty","cb2InitParty","newKeys","ctrlFuncs","chooseTarget",
		"multiCursor","battlerPos","battlersCount","absentFlags","activeBattler","mapLayout",
		"startCb","startCbInput","sMenuBase","gWindowsBase","startCursor","gTasksBase","cb2BagRun",
		"bagHandler","bagOpen","partyTask","yesNoTask","multiTask","selMenuTask","startMenuTask",
		"mapHeader","battleMainCb","mapObjects","fieldMsgMode","mapNameTask","fieldCamera",
		"linkStatus","linkErr","linkErrBuf","linkNotRecv","vblankCtr","sb2ptr","spriteCoordOff",
		"hbCtr","mapHeaderPath",
	};
	for (int r = 3; r < NCODES; r++) {               // AXVE, AXPE
		const GameProfile* p = prof(CODES[r]);
		if (!p) continue;
		// Walk the row as a flat array of uint32_t — the struct is uint32_t from sb1ptr to
		// mapHeaderPath, which is exactly the span the names[] table describes.
		const uint32_t* w = &p->sb1ptr;
		int n = (int)(sizeof names / sizeof names[0]);
		for (int i = 0; i < n; i++) {
			int isRom = (w[i] >> 24) == 0x08u || (w[i] >> 24) == 0x09u;
			int licensed = !strcmp(names[i], "battleMainCb");
			CHECK(!isRom || licensed, "%s.%s = 0x%08X is a ROM address and is not licensed",
			      CODES[r], names[i], w[i]);
		}
		EQU(p->battleMainCb, 0x0800F808u,
		    "%s battleMainCb = BattleMainCB2 (identical in all four RS sym maps)", CODES[r]);
		// The 0 columns whose degradation is NAMED in gamestate.c — asserted so a future edit that
		// "helpfully" fills one has to change this test and read the reason first.
		EQU(p->partyMenu,     0u, "%s partyMenu = 0 (no gPartyMenu symbol in RS)", CODES[r]);
		EQU(p->chooseTarget,  0u, "%s chooseTarget = 0 (absent from the RS decomp)", CODES[r]);
		EQU(p->multiCursor,   0u, "%s multiCursor = 0 (no gMultiUsePlayerCursor)", CODES[r]);
		EQU(p->gWindowsBase,  0u, "%s gWindowsBase = 0 (no gWindows)", CODES[r]);
		EQU(p->sMenuBase,     0u, "%s sMenuBase = 0 (RS sMenu is 4 bytes; layout unproven)", CODES[r]);
		EQU(p->partyTask,     0u, "%s partyTask = 0 -> GCTX_PARTY unreachable", CODES[r]);
		EQU(p->yesNoTask,     0u, "%s yesNoTask = 0", CODES[r]);
		EQU(p->multiTask,     0u, "%s multiTask = 0", CODES[r]);
		EQU(p->selMenuTask,   0u, "%s selMenuTask = 0", CODES[r]);
		EQU(p->startMenuTask, 0u, "%s startMenuTask = 0 -> GCTX_FIELDMENU unreachable", CODES[r]);
		EQU(p->mapNameTask,   0u, "%s mapNameTask = 0", CODES[r]);
		EQU(p->cb2BagRun,     0u, "%s cb2BagRun = 0 -> GCTX_BAG unreachable", CODES[r]);
		EQU(p->bagHandler,    0u, "%s bagHandler = 0", CODES[r]);
		EQU(p->linkErrBuf,    0u, "%s linkErrBuf = 0 (no sLinkErrorBuffer)", CODES[r]);
		EQU(p->linkNotRecv,   0u, "%s linkNotRecv = 0 (no gRemoteLinkPlayersNotReceived)", CODES[r]);
		EQU(p->spriteCoordOff,0u, "%s spriteCoordOff = 0 (X and Y are 0x310 apart, not +2)", CODES[r]);
		// mapHeader stays 0 ON PURPOSE (it gates the phase-14 HD-2D depth path), while
		// mapHeaderPath — the warp-classification copy — is filled. Confusing the two would switch
		// an untested render path on for a new game.
		EQU(p->mapHeader,     0u, "%s mapHeader = 0 (the HD-2D depth gate stays shut)", CODES[r]);
		CHECK(p->mapHeaderPath != 0u, "%s mapHeaderPath IS filled (warp routing works)", CODES[r]);
	}
}

// ============================================================================================
// TEST 5 — AXVE and AXPE share ONE RAM body (SPEC-coop P3.5.1) — and differ in exactly the
// places where Ruby and Sapphire ROM addresses genuinely DRIFT (LANE-B-RS.md §2 headline; every
// such value is live-read or sym-read PER TITLE and must never be copied across):
//   * the cb2Title/cb2FullUi screen-class lists (lane-B fold-in), and
//   * phase 22.2's mapGroupsRom / mapGroupsRomAlt — Ruby's gMapGroups is 0x083085A0 and
//     Sapphire's is 0x08308530, 0x70 apart, and this one is DEREFERENCED rather than compared, so
//     a cross-title copy would walk a wrong ROM table instead of merely never matching.
// Everything else stays byte-identical, pinned here.
// ============================================================================================
static void test_rs_rows_pinned(void) {
	printf("TEST 5: the RS rows share one RAM body; only the per-title cb2 lists differ\n");
	const GameProfile* ru = prof("AXVE");
	const GameProfile* sa = prof("AXPE");
	CHECK(ru && sa, "both RS rows exist");
	if (!ru || !sa) return;
	GameProfile a = *ru, b = *sa;
	memset(a.code, 0, sizeof a.code);
	memset(b.code, 0, sizeof b.code);
	memset(a.cb2Title,  0, sizeof a.cb2Title);  memset(b.cb2Title,  0, sizeof b.cb2Title);
	memset(a.cb2FullUi, 0, sizeof a.cb2FullUi); memset(b.cb2FullUi, 0, sizeof b.cb2FullUi);
	a.mapGroupsRom = b.mapGroupsRom = 0; a.mapGroupsRomAlt = b.mapGroupsRomAlt = 0;
	CHECK(memcmp(&a, &b, sizeof a) == 0,
	      "AXVE and AXPE differ ONLY in code + the per-title cb2 lists + gMapGroups (one RAM map)");
	// The phase-22.2 traversal columns, both halves of the split: gPlayerParty is SHARED (all six
	// RS maps carry 0x03004360 with size 0x258 — an IWRAM party, unlike EM/FRLG's EWRAM one),
	// while gMapGroups is PER TITLE and the two must never be equal.
	EQU(ru->partyBase, 0x03004360u, "Ruby gPlayerParty (IWRAM, pokeruby[_rev1|_rev2].sym)");
	EQU(sa->partyBase, 0x03004360u, "Sapphire gPlayerParty — the same in all six RS maps");
	EQU(ru->mapGroupsRom,    0x083085A0u, "Ruby gMapGroups rev1/rev2");
	EQU(ru->mapGroupsRomAlt, 0x08308588u, "Ruby gMapGroups rev0 (the alternate)");
	EQU(sa->mapGroupsRom,    0x08308530u, "Sapphire gMapGroups rev1/rev2 — measured, not copied");
	EQU(sa->mapGroupsRomAlt, 0x08308518u, "Sapphire gMapGroups rev0");
	CHECK(ru->mapGroupsRom != sa->mapGroupsRom,
	      "Ruby's and Sapphire's gMapGroups are DIFFERENT — this column is dereferenced, so a "
	      "cross-title copy would walk the wrong ROM table");
	// The drift itself, as lane B measured it LIVE on both titles (never copy across):
	EQU(ru->cb2Title[0], sa->cb2Title[0], "MainCB2_Intro is identical (measured on BOTH, not copied)");
	EQU(ru->cb2Title[1] + 4u, sa->cb2Title[1], "title MainCB2 drifts +4 Ruby->Sapphire (live both)");
	EQU(ru->cb2Title[2], sa->cb2Title[2], "CB2_MainMenu is identical (measured on both)");
	// Ruby's party-menu cb2 must NOT appear anywhere in Sapphire's lists (it resolves inside
	// Task_ResetRtcScreen on the sapphire map — the exact cross-title copy this test bans).
	for (int k = 0; k < GS_N_TITLE; k++)
		CHECK(sa->cb2Title[k] != 0x0806AEFCu, "Sapphire title[%d] is not Ruby's party cb2", k);
	for (int k = 0; k < GS_N_FULLUI; k++)
		CHECK(sa->cb2FullUi[k] != 0x0806AEFCu, "Sapphire fullui[%d] is not Ruby's party cb2", k);
}

// ============================================================================================
// TEST 6 — every RS address is the value read from pret's four byte-matched sym maps
// (SPEC-coop P3.3.3, re-verified 2026-08-13: pokeruby / pokesapphire / pokeruby_rev1 /
//  pokesapphire_rev1 agree on all 727 RAM symbols, zero differences.)
// This is the one place the numbers themselves are asserted, so a typo in a 50-column positional
// initialiser is a test failure rather than a wrong pointer deref on someone's console.
// ============================================================================================
static void test_rs_addresses(void) {
	printf("TEST 6: every RS address matches pret's byte-matched symbol maps\n");
	const GameProfile* p = prof("AXVE");
	if (!p) { CHECK(0, "AXVE row missing"); return; }
	EQU(p->sb1ptr,        0x02025734u, "gSaveBlock1        (pokeruby.sym:106)");
	EQU(p->sb2ptr,        0x02024EA4u, "gSaveBlock2        (:105)");
	EQU(p->mainCb2,       0x03001774u, "gMain(0x03001770,:526) + 4  = callback2");
	EQU(p->vblankCtr,     0x03001790u, "gMain + 0x20 = vblankCounter1 (a REAL u32 on RS)");
	EQU(p->hbCtr,         0x03001794u, "gMain + 0x24 = vblankCounter2");
	EQU(p->newKeys,       0x0300179Eu, "gMain + 0x2E = newKeys");
	EQU(p->gTasksBase,    0x03004B20u, "gTasks             (:642)");
	EQU(p->mapObjects,    0x030048A0u, "gObjectEvents      (:634, size 0x240 = 16 x 0x24)");
	EQU(p->fieldCamera,   0x03004880u, "gFieldCamera       (:631, size 0x18)");
	EQU(p->fieldMsgMode,  0x030005A8u, "sMessageBoxMode    (:464)");
	EQU(p->mapHeaderPath, 0x0202E828u, "gMapHeader         (:126)");
	EQU(p->mapLayout,     0x03004870u, "gBackupMapLayout   (:630)");
	EQU(p->battleFlags,   0x020239F8u, "gBattleTypeFlags   (:23)");
	EQU(p->actionCursor,  0x02024E60u, "gActionSelectionCursor (:94)");
	EQU(p->moveCursor,    0x02024E64u, "gMoveSelectionCursor   (:95)");
	EQU(p->battleMons,    0x02024A80u, "gBattleMons        (:37)");
	EQU(p->bg0y,          0x030042A0u, "gBattle_BG0_Y      (:599)");
	EQU(p->battlersCount, 0x02024A68u, "gBattlersCount     (:30)");
	EQU(p->battlerPos,    0x02024A72u, "gBattlerPositions  (:32)");
	EQU(p->absentFlags,   0x02024C0Cu, "gAbsentBattlerFlags(:54)");
	EQU(p->activeBattler, 0x02024A60u, "gActiveBattler     (:28)");
	EQU(p->ctrlFuncs,     0x03004330u, "gBattlerControllerFuncs (:608)");
	EQU(p->partyCount,    0x03004350u, "gPlayerPartyCount  (:613) — verify-on-hw-pending, inert");
	EQU(p->startCb,       0x03004AE8u, "gMenuCallback      (:637)");
	EQU(p->startCursor,   0x0202E8FCu, "sStartMenuCursorPos(:162)");
	EQU(p->linkStatus,    0x03002A60u, "gLinkStatus        (:558)");
	EQU(p->linkErr,       0x0300295Cu, "gLinkErrorOccurred (:549)");
}

// ============================================================================================
// TEST 7 — the RS identity latch rejects an all-zero name (SPEC-coop P3.2.5)
// gSaveBlock2 exists (zeroed) from boot on RS, so the "is it a valid pointer" test can no longer
// say "a save is loaded". A zeroed name decodes to eight spaces, which is well-formed by the
// 0xFF rule and would therefore be latched FOREVER as a blank nameplate.
// ============================================================================================
static void test_rs_ident(void) {
	printf("TEST 7: an all-zero RS name is refused, a real one is latched\n");

	// --- RS, zeroed save block: nothing may latch, however many frames go by.
	{
		GbaCore c; bus_reset(&c, "AXVE");
		const GameProfile* p = profile_for(&c);
		GameState gs; game_read(&c, p, &gs);
		PresenceIdent id; presence_ident_reset(&id);
		PeerPresence r;
		for (uint32_t f = 0; f < 240; f++) presence_read_fill(&r, 0, &c, p, &gs, &id, f);
		CHECK(!id.have, "AXVE: a zeroed gSaveBlock2 never latches an identity");
	}
	// --- RS with a real name: latches, and the name survives.
	{
		GbaCore c; bus_reset(&c, "AXVE");
		const GameProfile* p = profile_for(&c);
		const uint8_t NM[8] = { 0xBF, 0xD9, 0xE1, 0xFF, 0x00, 0x00, 0x00, 0x00 };   // "Guy" + terminator
		for (int i = 0; i < 8; i++) bus_w8(&c, p->sb2ptr + (uint32_t)i, NM[i]);
		bus_w8(&c, p->sb2ptr + 0x08u, 0);                                            // gender = male
		bus_w8(&c, p->sb2ptr + 0x0Au, 0x22); bus_w8(&c, p->sb2ptr + 0x0Bu, 0x11);     // TID
		GameState gs; game_read(&c, p, &gs);
		PresenceIdent id; presence_ident_reset(&id);
		PeerPresence r;
		for (uint32_t f = 0; f < 240; f++) presence_read_fill(&r, 0, &c, p, &gs, &id, f);
		CHECK(id.have, "AXVE: a real name DOES latch");
		CHECK(memcmp(id.name, NM, 8) == 0, "AXVE: the latched bytes are the name that was there");
		EQU(id.tid, 0x1122u, "AXVE: the visible TID is the LE u16 at +0x0A");
	}
	// --- the pointer games are untouched by the new rule: an unarmed pointer still latches nothing,
	//     and an all-zero name behind a VALID pointer must ALSO be refused only where RS needs it —
	//     for a pointer profile the deref guard already did the work, so nothing changes.
	{
		GbaCore c; bus_reset(&c, "BPEE");
		const GameProfile* p = profile_for(&c);
		GameState gs; game_read(&c, p, &gs);
		PresenceIdent id; presence_ident_reset(&id);
		PeerPresence r;
		for (uint32_t f = 0; f < 240; f++) presence_read_fill(&r, 0, &c, p, &gs, &id, f);
		CHECK(!id.have, "BPEE: an unarmed gSaveBlock2Ptr still latches nothing");
	}
}

// ============================================================================================
// TEST 8 — address-space sanity across EVERY row
// A shipped address is either 0 (a named degradation) or lands in a real GBA region. This is the
// cheapest possible catch for a positional initialiser that lost or gained a column.
// ============================================================================================
static void test_address_sanity(void) {
	printf("TEST 8: every shipped address is 0 or in EWRAM / IWRAM / ROM\n");
	for (int r = 0; r < NCODES; r++) {
		const GameProfile* p = prof(CODES[r]);
		if (!p) continue;
		const uint32_t* w = &p->sb1ptr;
		int n = (int)((const uint32_t*)&p->mapHeaderPath - w) + 1;
		CHECK(n == 49, "%s: the row spans 49 uint32_t columns (got %d)", CODES[r], n);
		for (int i = 0; i < n; i++) {
			uint32_t region = w[i] >> 24;
			CHECK(w[i] == 0 || region == 0x02u || region == 0x03u || region == 0x08u ||
			      region == 0x09u,
			      "%s column %d = 0x%08X is not a GBA address", CODES[r], i, w[i]);
		}
		// The three columns presence CANNOT work without, for every game that claims support.
		CHECK(p->sb1ptr != 0,     "%s has sb1ptr",     CODES[r]);
		CHECK(p->sb2ptr != 0,     "%s has sb2ptr",     CODES[r]);
		CHECK(p->mapObjects != 0, "%s has mapObjects", CODES[r]);
		CHECK(p->mapLayout != 0,  "%s has mapLayout",  CODES[r]);
		CHECK(p->mainCb2 != 0,    "%s has mainCb2",    CODES[r]);
	}
}

// ============================================================================================
// TEST 9 — PHASE 20: the three peer-sprite columns (docs/phase20-peersprite/SPEC.md S1.3)
// The numbers themselves, per game, against pret's byte-matched `symbols` branch — the same
// discipline TEST 6 applies to the RS row, and for the same reason: a typo in a 50-column
// positional initialiser must be a test failure here, not a wrong read on someone's console.
// Every value below was re-derived from the nine .sym maps on this machine on 2026-08-13.
// ============================================================================================
static void test_peersprite_columns(void) {
	printf("TEST 9: the phase-20 gSprites / gPlttBufferUnfaded / gPlayerAvatar columns\n");
	struct { const char* code; uint32_t spr, pltt, pav; } W[] = {
		// pokeemerald.sym: 02020630 gSprites / 02037714 gPlttBufferUnfaded / 02037590 gPlayerAvatar
		{ "BPEE", 0x02020630u, 0x02037714u, 0x02037590u },
		// pokefirered.sym AND pokefirered_rev1.sym (identical; the user's FR is rev1)
		{ "BPRE", 0x0202063Cu, 0x020371F8u, 0x02037078u },
		// pokeleafgreen.sym AND pokeleafgreen_rev1.sym — LeafGreen's OWN maps, not FR-derived
		{ "BPGE", 0x0202063Cu, 0x020371F8u, 0x02037078u },
		// pokeruby / pokesapphire / both rev1 — 4/4 agree. VERIFIED-SYM / VERIFY-ON-HW (no RS ROM
		// exists on this machine, so these have never been EXECUTED).
		{ "AXVE", 0x02020004u, 0x0202EAC8u, 0x0202E858u },
		{ "AXPE", 0x02020004u, 0x0202EAC8u, 0x0202E858u },
	};
	for (unsigned i = 0; i < sizeof W / sizeof W[0]; i++) {
		const GameProfile* p = prof(W[i].code);
		CHECK(p != NULL, "%s row exists", W[i].code);
		if (!p) continue;
		EQU(p->sprites,      W[i].spr,  "%s gSprites",           W[i].code);
		EQU(p->plttUnfaded,  W[i].pltt, "%s gPlttBufferUnfaded", W[i].code);
		EQU(p->playerAvatar, W[i].pav,  "%s gPlayerAvatar",      W[i].code);
		// EWRAM only — every one of these is a RAM symbol, which is what licenses ONE value for
		// Ruby AND Sapphire AND both revisions (the ROM-address ban TEST 4 enforces).
		CHECK((p->sprites      >> 24) == 0x02u, "%s gSprites is EWRAM",      W[i].code);
		CHECK((p->plttUnfaded  >> 24) == 0x02u, "%s gPlttBufferUnfaded EWRAM", W[i].code);
		CHECK((p->playerAvatar >> 24) == 0x02u, "%s gPlayerAvatar is EWRAM",  W[i].code);
		// The whole chain must be present, or the live sprite silently falls back to the
		// placeholder for that game — which is safe, but it must not happen by accident.
		CHECK(p->sprites != 0 && p->mapObjects != 0,
		      "%s can resolve a peer sprite at all (sprites + mapObjects)", W[i].code);
	}
	// gSprites' 65-entry extent is the bound pspr_resolve clause 7 relies on: spriteId < 64.
	// gPlttBufferUnfaded is 0x400 bytes = 512 u16, so OBJ bank 15 colour 15 sits at +512+480+30 =
	// +1022, the last two bytes — asserted here so a future column edit cannot quietly overrun it.
	CHECK(512u + 32u * 15u + 2u * 15u + 1u < 0x400u,
	      "OBJ bank 15 colour 15 lies inside gPlttBufferUnfaded's 0x400 bytes");
}

// ============================================================================================
// TEST 10 — PHASE 22.0: the rev-alternate ROM anchors + the newKeys transposition fix
// (docs/phase21-touch-census/: VISITED-firered.md headline, CB2-HARVEST.md:97-100,
//  RS-REV2-VERIFICATION.md §6/§7.)
// The census proved FRLG rev1 MOVED every ROM function while RAM stayed identical, and the
// profile cannot see the cart's revision byte — so each rev-sensitive anchor now ships a
// PRIMARY and an ALTERNATE and gamestate.c tests both. The numbers themselves are pinned here,
// per game, exactly like TEST 6 pins RS: a typo in the positional initialiser must fail here,
// not silently dead-en detection on someone's cart (which is precisely the bug this fixes).
// ============================================================================================
static void test_rev_alternates(void) {
	printf("TEST 10: phase-22.0 rev-alternate ROM anchors + the newKeys fix, per game\n");
	// Per-row expectation: 13 primaries then 13 alternates, in the struct's field order.
	// BPRE: primary = FR rev0 (unchanged); alt = FR rev1 — the USER'S cart. Six of the alts are
	// live-verified [exact] census 2026-08-14 (BattleMainCB2, CB2_UpdatePartyMenu, CB2_BagMenuRun,
	// Task_HandleChooseMonInput, Task_StartMenuHandleInput, Task_HandleSelectionMenuInput); the
	// other seven are rev1 sym-derived (pokefirered_rev1.sym), verify-in-emulator.
	// BPGE: primary = LG rev1 (user's cart is rev 1.1); alt = LG rev0. Lane B live-verified 10 of
	// the 13 primaries [exact] on the running LG fixture (LANE-B-LG.md; chooseTarget needs a
	// double battle, startCbInput is unused-by-code, yesNoTask is live-unreached). The old
	// primaries were FR-rev0 values — wrong for EVERY LeafGreen revision. yesNoTaskAlt DEVIATES
	// from the rev0 convention: it carries Task_CallYesOrNoCallback 0x080BF548 (LG rev1, live
	// [exact] twice — bag-toss + mart-buy confirms), the handler FRLG actually routes its common
	// yes/no prompts through (the LANE-B-LG.md #8 bypass finding).
	// BPEE: one revision in play, primaries live-verified by the census -> all alternates 0.
	// AXVE/AXPE: the RS ROM-address ban covers alternates too -> all 0.
	struct { const char* code;
	         uint32_t battleMain, cb2Upd, cb2Init, chooseTgt, startCbIn, bagRun, bagH,
	                  party, yesNo, multi, selMenu, startMenu, mapName;              // primaries
	         uint32_t aBattleMain, aCb2Upd, aCb2Init, aChooseTgt, aStartCbIn, aBagRun, aBagH,
	                  aParty, aYesNo, aMulti, aSelMenu, aStartMenu, aMapName;        // alternates
	         uint32_t newKeys; } W[] = {
		{ "BPEE",
		  0x08038420u, 0x081B01B0u, 0x081B01E0u, 0x08057824u, 0x0809FAC4u, 0x081AAD5Cu, 0x081ABD28u,
		  0x081B1370u, 0x080E215Cu, 0x080E2058u, 0x081B3730u, 0x0809FA34u, 0x080D487Cu,
		  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		  0x030022EEu },                                    // gMain 0x030022C0 + 0x2E (unchanged)
		{ "BPRE",
		  0x08011100u, 0x0811EBA0u, 0x0811EBD0u, 0x0802E674u, 0x0806F280u, 0x08107EE0u, 0x08108F0Cu,
		  0x0811FB28u, 0x0809CE54u, 0x0809CC98u, 0x08122C5Cu, 0x0806F1F0u, 0x080981ACu,
		  0x08011114u, 0x0811EC18u, 0x0811EC48u, 0x0802E688u, 0x0806F294u, 0x08107F58u, 0x08108F84u,
		  0x0811FBA0u, 0x0809CE68u, 0x0809CCACu, 0x08122CD4u, 0x0806F204u, 0x080981C0u,
		  0x0300311Eu },                                    // §7 fix: gMain 0x030030F0 + 0x2E
		{ "BPGE",
		  0x08011114u, 0x0811EBF0u, 0x0811EC20u, 0x0802E688u, 0x0806F294u, 0x08107F30u, 0x08108F5Cu,
		  0x0811FB78u, 0x0809CE3Cu, 0x0809CC80u, 0x08122CACu, 0x0806F204u, 0x08098194u,
		  0x08011100u, 0x0811EB78u, 0x0811EBA8u, 0x0802E674u, 0x0806F280u, 0x08107EB8u, 0x08108EE4u,
		  0x0811FB00u, 0x080BF548u, 0x0809CC6Cu, 0x08122C34u, 0x0806F1F0u, 0x08098180u,
		  0x0300311Eu },   // §7 fix (same gMain as FR); yesNo alt = Task_CallYesOrNoCallback (lane B)
		{ "AXVE",
		  0x0800F808u, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		  0x0300179Eu },                                    // gMain 0x03001770 + 0x2E (unchanged)
		{ "AXPE",
		  0x0800F808u, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		  0x0300179Eu },
	};
	for (unsigned i = 0; i < sizeof W / sizeof W[0]; i++) {
		const GameProfile* p = prof(W[i].code);
		if (!p) { CHECK(0, "%s row missing", W[i].code); continue; }
		EQU(p->battleMainCb,  W[i].battleMain, "%s battleMainCb",  W[i].code);
		EQU(p->cb2UpdParty,   W[i].cb2Upd,     "%s cb2UpdParty",   W[i].code);
		EQU(p->cb2InitParty,  W[i].cb2Init,    "%s cb2InitParty",  W[i].code);
		EQU(p->chooseTarget,  W[i].chooseTgt,  "%s chooseTarget",  W[i].code);
		EQU(p->startCbInput,  W[i].startCbIn,  "%s startCbInput",  W[i].code);
		EQU(p->cb2BagRun,     W[i].bagRun,     "%s cb2BagRun",     W[i].code);
		EQU(p->bagHandler,    W[i].bagH,       "%s bagHandler",    W[i].code);
		EQU(p->partyTask,     W[i].party,      "%s partyTask",     W[i].code);
		EQU(p->yesNoTask,     W[i].yesNo,      "%s yesNoTask",     W[i].code);
		EQU(p->multiTask,     W[i].multi,      "%s multiTask",     W[i].code);
		EQU(p->selMenuTask,   W[i].selMenu,    "%s selMenuTask",   W[i].code);
		EQU(p->startMenuTask, W[i].startMenu,  "%s startMenuTask", W[i].code);
		EQU(p->mapNameTask,   W[i].mapName,    "%s mapNameTask",   W[i].code);
		EQU(p->battleMainCbAlt,  W[i].aBattleMain, "%s battleMainCbAlt",  W[i].code);
		EQU(p->cb2UpdPartyAlt,   W[i].aCb2Upd,     "%s cb2UpdPartyAlt",   W[i].code);
		EQU(p->cb2InitPartyAlt,  W[i].aCb2Init,    "%s cb2InitPartyAlt",  W[i].code);
		EQU(p->chooseTargetAlt,  W[i].aChooseTgt,  "%s chooseTargetAlt",  W[i].code);
		EQU(p->startCbInputAlt,  W[i].aStartCbIn,  "%s startCbInputAlt",  W[i].code);
		EQU(p->cb2BagRunAlt,     W[i].aBagRun,     "%s cb2BagRunAlt",     W[i].code);
		EQU(p->bagHandlerAlt,    W[i].aBagH,       "%s bagHandlerAlt",    W[i].code);
		EQU(p->partyTaskAlt,     W[i].aParty,      "%s partyTaskAlt",     W[i].code);
		EQU(p->yesNoTaskAlt,     W[i].aYesNo,      "%s yesNoTaskAlt",     W[i].code);
		EQU(p->multiTaskAlt,     W[i].aMulti,      "%s multiTaskAlt",     W[i].code);
		EQU(p->selMenuTaskAlt,   W[i].aSelMenu,    "%s selMenuTaskAlt",   W[i].code);
		EQU(p->startMenuTaskAlt, W[i].aStartMenu,  "%s startMenuTaskAlt", W[i].code);
		EQU(p->mapNameTaskAlt,   W[i].aMapName,    "%s mapNameTaskAlt",   W[i].code);
		// The newKeys pin — the field is UNUSED at runtime (gamestate.h documents it; keys are
		// injected via the returned mask), but a transposed gMain-relative address is a loaded
		// gun for any future edit, which is why the fix ships WITH this pin (never silently).
		EQU(p->newKeys, W[i].newKeys, "%s newKeys = gMain + 0x2E (RS-REV2-VERIFICATION.md §7)", W[i].code);
		// Structural sanity: an alternate is ROM-space when set, and never EQUAL to its primary
		// (a same-value pair is the copy-paste bug this table would otherwise hide).
		const uint32_t pri[13] = { p->battleMainCb, p->cb2UpdParty, p->cb2InitParty, p->chooseTarget,
			p->startCbInput, p->cb2BagRun, p->bagHandler, p->partyTask, p->yesNoTask, p->multiTask,
			p->selMenuTask, p->startMenuTask, p->mapNameTask };
		const uint32_t alt[13] = { p->battleMainCbAlt, p->cb2UpdPartyAlt, p->cb2InitPartyAlt,
			p->chooseTargetAlt, p->startCbInputAlt, p->cb2BagRunAlt, p->bagHandlerAlt, p->partyTaskAlt,
			p->yesNoTaskAlt, p->multiTaskAlt, p->selMenuTaskAlt, p->startMenuTaskAlt, p->mapNameTaskAlt };
		for (int k = 0; k < 13; k++) {
			if (alt[k]) {
				CHECK((alt[k] >> 24) == 0x08u, "%s alt[%d] is ROM-space", W[i].code, k);
				CHECK(alt[k] != pri[k], "%s alt[%d] differs from its primary", W[i].code, k);
			}
		}
	}
}

// ============================================================================================
// TEST 11 — PHASE 22.0: the census cb2 screen-class fingerprint lists, per game
// (docs/phase21-touch-census/CB2-HARVEST.md — every EM/FR value below was read [exact] from the
//  LIVE game; the lists' whole job is to stop those 40 screens hiding in GCTX_OVERWORLD.)
// ============================================================================================
static void test_screen_classes(void) {
	printf("TEST 11: phase-22.0 cb2Title / cb2FullUi lists, per game\n");
	static const uint32_t EM_TITLE[GS_N_TITLE] = { 0x0816CC00u, 0x080AAB2Cu, 0x0802F6B0u };
	// PHASE 25 (lane C1): entries 16.. are the harvest that unblocks lane B1's nine BROKEN TAP
	// rows. 0x08173560 (CB2_HallOfFame, rows E15+L1) and 0x08121C64 (CB2_MailRead, row E8) were
	// read LIVE [exact] this session; the rest are VERIFIED-SYM per LANE-C-HARVEST.md.
	static const uint32_t EM_FULL[GS_N_FULLUI] = {
		0x080BA4B0u, 0x080C2710u, 0x081248D4u, 0x080C5438u, 0x081BFAB4u, 0x08177C54u,
		0x080BB774u, 0x081C7400u, 0x0813591Cu, 0x0816631Cu, 0x08179B68u, 0x08170274u,
		0x08134C9Cu, 0x080E4F58u, 0x080C7D54u, 0x0812A670u,
		0x08173560u, 0x08121C64u, 0x081219F0u, 0x0813E3A4u, 0x0813E3C0u, 0x0813DD7Cu,
		0x0813DF70u, 0x080F5C00u, 0x0812FDF8u, 0x08081898u, 0x08081FC8u, 0x0800B1A0u,
		0x0800AF30u };
	static const uint32_t FR_TITLE[GS_N_TITLE] = {
		0x080EC9E8u, 0x080EC878u, 0x08078BB0u, 0x0800C2E8u, 0x0812EB88u };
	// PHASE 25: entries 16.. are the FireRed halves of the same rows, VERIFIED-SYM on
	// pokefirered_rev1.sym and live-unverified (this lane's boot parked FR at its title).
	static const uint32_t FR_FULL[GS_N_FULLUI] = {
		0x08088370u, 0x08089084u, 0x080C08C8u, 0x08137F60u, 0x0813CE78u, 0x081318DCu,
		0x0811C774u, 0x0810254Cu, 0x0815AC0Cu, 0x0812C40Cu, 0x0808CDD8u, 0x0809FB84u,
		0x080F1E38u, 0x0809ADF8u, 0x0813F9C4u, 0x08056760u,
		0x080CE724u, 0x080CE740u, 0x080CE0FCu, 0x080CE2F0u, 0x080BF37Cu, 0x080BF124u,
		0x080EC274u, 0x080F29F0u, 0x0800AF40u, 0x0800ACE8u };
	static const uint32_t NONE_T[GS_N_TITLE]  = { 0 };
	static const uint32_t NONE_F[GS_N_FULLUI] = { 0 };
	// Lane-B RS promotion (LANE-B-RS.md): PER-TITLE lists — Ruby and Sapphire ROM addresses
	// drift (title MainCB2 +4; party drifts), so AXVE and AXPE pin DIFFERENT values where lane B
	// measured both, and Sapphire's party slot stays EMPTY (only Ruby was measured — never copy
	// across the drift). All values live-read [exact] on pokeruby_rev2.sym / pokesapphire_rev2.sym.
	static const uint32_t RU_TITLE[GS_N_TITLE] = { 0x0813B7B8u, 0x0807C474u, 0x080096C4u };
	static const uint32_t RU_FULL[GS_N_FULLUI] = { 0x0806AEFCu, 0x080A3138u };
	static const uint32_t SA_TITLE[GS_N_TITLE] = { 0x0813B7B8u, 0x0807C478u, 0x080096C4u };
	static const uint32_t SA_FULL[GS_N_FULLUI] = { 0x080A3138u };
	struct { const char* code; const uint32_t* title; const uint32_t* full; int visited; } W[] = {
		{ "BPEE", EM_TITLE, EM_FULL, 1 },   // census boots #1/#2, live-harvested
		{ "BPRE", FR_TITLE, FR_FULL, 1 },   // FR visit pass on the user's rev1 cart
		{ "BPGE", NONE_T,   NONE_F,  0 },   // LG harvest banked (LANE-B-LG.md) but not yet promoted
		{ "AXVE", RU_TITLE, RU_FULL, 1 },   // lane-B Ruby boot (user's 600h save fixture)
		{ "AXPE", SA_TITLE, SA_FULL, 1 },   // lane-B Sapphire co-op + solo smoke
	};
	for (unsigned i = 0; i < sizeof W / sizeof W[0]; i++) {
		const GameProfile* p = prof(W[i].code);
		if (!p) { CHECK(0, "%s row missing", W[i].code); continue; }
		for (int k = 0; k < GS_N_TITLE; k++)
			EQU(p->cb2Title[k],  W[i].title[k], "%s cb2Title[%d]",  W[i].code, k);
		for (int k = 0; k < GS_N_FULLUI; k++)
			EQU(p->cb2FullUi[k], W[i].full[k],  "%s cb2FullUi[%d]", W[i].code, k);
		// Structure: every nonzero entry is ROM-space, and no cb2 appears twice across BOTH lists
		// (one screen must classify one way).
		uint32_t all[GS_N_TITLE + GS_N_FULLUI]; int n = 0;
		for (int k = 0; k < GS_N_TITLE;  k++) if (p->cb2Title[k])  all[n++] = p->cb2Title[k];
		for (int k = 0; k < GS_N_FULLUI; k++) if (p->cb2FullUi[k]) all[n++] = p->cb2FullUi[k];
		for (int a = 0; a < n; a++) {
			CHECK((all[a] >> 24) == 0x08u, "%s class cb2 0x%08X is ROM-space", W[i].code, all[a]);
			for (int b = a + 1; b < n; b++)
				CHECK(all[a] != all[b], "%s cb2 0x%08X listed once only", W[i].code, all[a]);
		}
		if (!W[i].visited)
			CHECK(n == 0, "%s (never census-visited) ships EMPTY class lists — no guessed cb2s", W[i].code);
	}
}

// ============================================================================================
// TEST 12 — PHASE 22.0 BEHAVIOUR through the real game_read: the alternates actually detect,
// the class lists actually classify, precedence holds, and the fall-through is undisturbed.
// This is the "FR rev1 un-deaded" claim executed rather than asserted.
// ============================================================================================
static void put_task(GbaCore* c, const GameProfile* p, int slot, uint32_t fn, uint8_t active) {
	uint32_t task = p->gTasksBase + 40u * (uint32_t)slot;
	bus_w32(c, task + 0, fn);
	bus_w8 (c, task + 4, active);
}

static void test_phase22_behaviour(void) {
	printf("TEST 12: phase-22.0 behaviour — alternates detect, classes classify, precedence holds\n");

	// --- (a) FR rev1 battle via the ALTERNATE cb2 (the user's cart) — was dead pre-phase-22.
	{
		GbaCore c; bus_reset(&c, "BPRE");
		const GameProfile* p = profile_for(&c);
		GameState gs;
		bus_w32(&c, p->mainCb2, p->battleMainCbAlt | 1u);   // live cb2 carries the Thumb bit
		bus_w32(&c, p->bg0y, 160u);                          // (bg0y is u16; low half = 160)
		game_read(&c, p, &gs);
		EQU(gs.ctx, GCTX_BATTLE_ACTION, "BPRE rev1: BattleMainCB2 0x08011114 -> GCTX_BATTLE_ACTION");
		// ...and the rev0 PRIMARY still works (a rev0 cart is not broken by the fix).
		bus_w32(&c, p->mainCb2, p->battleMainCb | 1u);
		game_read(&c, p, &gs);
		EQU(gs.ctx, GCTX_BATTLE_ACTION, "BPRE rev0: BattleMainCB2 0x08011100 still detects");
	}
	// --- (b) FR rev1 party / start-menu / bag via ALTERNATE task handlers.
	{
		GbaCore c; bus_reset(&c, "BPRE");
		const GameProfile* p = profile_for(&c);
		GameState gs;
		put_task(&c, p, 3, p->partyTaskAlt | 1u, 1);
		game_read(&c, p, &gs);
		EQU(gs.ctx, GCTX_PARTY, "BPRE rev1: Task_HandleChooseMonInput alt -> GCTX_PARTY");
		bus_reset(&c, "BPRE"); p = profile_for(&c);
		put_task(&c, p, 0, p->startMenuTaskAlt | 1u, 1);
		game_read(&c, p, &gs);
		EQU(gs.ctx, GCTX_FIELDMENU, "BPRE rev1: Task_StartMenuHandleInput alt -> GCTX_FIELDMENU");
		bus_reset(&c, "BPRE"); p = profile_for(&c);
		put_task(&c, p, 2, p->bagHandlerAlt | 1u, 1);        // bag input task, data[0] = listTaskId
		bus_w8(&c, p->gTasksBase + 40u * 2u + 8u, 5);
		game_read(&c, p, &gs);
		EQU(gs.ctx, GCTX_BAG, "BPRE rev1: Task_BagMenu_HandleInput alt -> GCTX_BAG");
		EQU(gs.bagListTaskBase, p->gTasksBase + 40u * 5u + 8u, "BPRE rev1: bag list task resolved");
	}
	// --- (c) the census screen classes classify POSITIVELY (resolved=true, no key-leaking
	//     overworld fall-through), on both harvested games.
	{
		GbaCore c; bus_reset(&c, "BPRE");
		const GameProfile* p = profile_for(&c);
		GameState gs;
		bus_w32(&c, p->mainCb2, 0x08078BB0u | 1u);           // CB2_TitleScreenRun [exact]
		game_read(&c, p, &gs);
		EQU(gs.ctx, GCTX_TITLE, "BPRE: title screen classifies as GCTX_TITLE");
		CHECK(gs.ctxResolved, "BPRE: ...and it is a POSITIVE match (resolved)");
		bus_w32(&c, p->mainCb2, 0x0810254Cu | 1u);           // CB2_PokedexScreen [exact]
		game_read(&c, p, &gs);
		EQU(gs.ctx, GCTX_FULLUI, "BPRE: Pokedex classifies as GCTX_FULLUI");
		CHECK(gs.ctxResolved, "BPRE: ...positively (resolved)");
	}
	{
		GbaCore c; bus_reset(&c, "BPEE");
		const GameProfile* p = profile_for(&c);
		GameState gs;
		bus_w32(&c, p->mainCb2, 0x0816CC00u | 1u);           // MainCB2_Intro [exact]
		game_read(&c, p, &gs);
		EQU(gs.ctx, GCTX_TITLE, "BPEE: GF intro classifies as GCTX_TITLE");
		bus_w32(&c, p->mainCb2, 0x081C7400u | 1u);           // CB2_Pokenav [exact]
		game_read(&c, p, &gs);
		EQU(gs.ctx, GCTX_FULLUI, "BPEE: PokeNav classifies as GCTX_FULLUI");
	}
	// --- (d) PRECEDENCE: a task-detected menu beats the class lists (more specific wins), and
	//     the class check never runs in battle.
	{
		GbaCore c; bus_reset(&c, "BPEE");
		const GameProfile* p = profile_for(&c);
		GameState gs;
		bus_w32(&c, p->mainCb2, 0x081BFAB4u | 1u);           // summary screen cb2 (FULLUI-listed)
		put_task(&c, p, 1, p->yesNoTask | 1u, 1);            // ...with a yes/no popup task live
		game_read(&c, p, &gs);
		EQU(gs.ctx, GCTX_FIELDMENU, "BPEE: an active menu TASK outranks the FULLUI class");
	}
	// --- (e) the fall-through is undisturbed: an UNKNOWN cb2 still lands in GCTX_OVERWORLD with
	//     resolved=false (the residual the gs-log promotion pipeline reads).
	{
		GbaCore c; bus_reset(&c, "BPEE");
		const GameProfile* p = profile_for(&c);
		GameState gs;
		bus_w32(&c, p->mainCb2, 0x08ABCDE0u | 1u);           // not in any list
		game_read(&c, p, &gs);
		EQU(gs.ctx, GCTX_OVERWORLD, "BPEE: unknown cb2 still falls through to GCTX_OVERWORLD");
		CHECK(!gs.ctxResolved, "BPEE: ...with resolved=false (the promotion pipeline's hook)");
	}
	// --- (f) the ctx name table kept up (the HUD/log surface of the new contexts).
	CHECK(!strcmp(gamestate_ctx_name(GCTX_TITLE),  "title"),  "GCTX_TITLE prints as 'title'");
	CHECK(!strcmp(gamestate_ctx_name(GCTX_FULLUI), "fullui"), "GCTX_FULLUI prints as 'fullui'");
	CHECK(!strcmp(gamestate_ctx_name(GCTX_BATTLE_OTHER), "b.oth"), "existing names undisturbed");
	// --- (g) lane-B RS promotion behaviour (LANE-B-RS.md): the per-title lists classify on
	//     their OWN title and a cross-title (drifted) value NEVER fires — executed, not asserted.
	{
		GbaCore c; bus_reset(&c, "AXVE");
		const GameProfile* p = profile_for(&c);
		GameState gs;
		bus_w32(&c, p->mainCb2, 0x0813B7B8u | 1u);           // MainCB2_Intro (live both titles)
		game_read(&c, p, &gs);
		EQU(gs.ctx, GCTX_TITLE, "AXVE: GF intro classifies as GCTX_TITLE");
		CHECK(gs.ctxResolved, "AXVE: ...positively (resolved)");
		bus_w32(&c, p->mainCb2, 0x0806AEFCu | 1u);           // Ruby CB2_PartyMenuMain [exact]
		game_read(&c, p, &gs);
		EQU(gs.ctx, GCTX_FULLUI, "AXVE: party menu classifies as GCTX_FULLUI (RS has no partyTask)");
		bus_w32(&c, p->mainCb2, 0x0807C478u | 1u);           // SAPPHIRE's drifted title MainCB2
		game_read(&c, p, &gs);
		EQU(gs.ctx, GCTX_OVERWORLD, "AXVE: Sapphire's +4-drifted title cb2 does NOT fire on Ruby");
		CHECK(!gs.ctxResolved, "AXVE: ...it falls through unresolved (the drift rule held)");
	}
	{
		GbaCore c; bus_reset(&c, "AXPE");
		const GameProfile* p = profile_for(&c);
		GameState gs;
		bus_w32(&c, p->mainCb2, 0x0807C478u | 1u);           // Sapphire's own title MainCB2 [exact]
		game_read(&c, p, &gs);
		EQU(gs.ctx, GCTX_TITLE, "AXPE: Sapphire title screen classifies as GCTX_TITLE");
		bus_w32(&c, p->mainCb2, 0x080A3138u | 1u);           // bag run loop ([exact] both RS maps)
		game_read(&c, p, &gs);
		EQU(gs.ctx, GCTX_FULLUI, "AXPE: bag run loop classifies as GCTX_FULLUI");
		bus_w32(&c, p->mainCb2, 0x0807C474u | 1u);           // RUBY's title MainCB2 on Sapphire
		game_read(&c, p, &gs);
		EQU(gs.ctx, GCTX_OVERWORLD, "AXPE: Ruby's title cb2 does NOT fire on Sapphire");
		bus_w32(&c, p->mainCb2, 0x0806AEFCu | 1u);           // Ruby's party cb2 = Task_ResetRtcScreen here
		game_read(&c, p, &gs);
		EQU(gs.ctx, GCTX_OVERWORLD, "AXPE: Ruby's party cb2 does NOT fire (named degradation kept)");
		CHECK(!gs.ctxResolved, "AXPE: ...unresolved fall-through (promotion hook stays open)");
	}
	// --- (h) the lane-B LG yes/no bypass fix (LANE-B-LG.md #8): Task_CallYesOrNoCallback — the
	//     handler FRLG actually routes bag-toss/mart-buy confirms through — detects as FIELDMENU
	//     via the yesNoTaskAlt slot, and the rev1 primary still detects too.
	{
		GbaCore c; bus_reset(&c, "BPGE");
		const GameProfile* p = profile_for(&c);
		GameState gs;
		EQU(p->yesNoTaskAlt, 0x080BF548u, "BPGE: yesNoTaskAlt IS Task_CallYesOrNoCallback (LG rev1)");
		put_task(&c, p, 3, 0x080BF548u | 1u, 1);
		game_read(&c, p, &gs);
		EQU(gs.ctx, GCTX_FIELDMENU, "BPGE: Task_CallYesOrNoCallback -> GCTX_FIELDMENU (confirms detect)");
		bus_reset(&c, "BPGE"); p = profile_for(&c);
		put_task(&c, p, 3, p->yesNoTask | 1u, 1);            // rev1 Task_YesNoMenu_HandleInput
		game_read(&c, p, &gs);
		EQU(gs.ctx, GCTX_FIELDMENU, "BPGE: the rev1 yes/no primary still detects (nothing displaced)");
	}
}

// ============================================================================================
// TEST 13 — PHASE 22.1: the keyboard + lists family anchors, pinned per game against the values
// re-read from the five pret sym maps this session (lane A; every citation in gamestate.c).
// ============================================================================================
typedef struct {
	const char* code;
	uint32_t namingCb, namingCbAlt, namingPtr;
	uint32_t buyTask, buyTaskAlt, buyQtyTask, buyQtyTaskAlt;
	uint32_t pcItemTask, pcItemTaskAlt, lmDummyTask, lmDummyTaskAlt;
	uint32_t bagPocket, dexTask, dexView;
	uint8_t  buyListSlot, pcItemListSlot;
} Fam22;
static const Fam22 FAM22[] = {
	{ "BPEE", 0x080E4F58u, 0, 0x02039F94u,
	          0x080E0AC8u, 0, 0x080E0D88u, 0,
	          0x0816C30Cu, 0, 0x081AE458u, 0,
	          0x0203CE5Du, 0x080BB7D4u, 0x02039B4Cu, 7, 5 },
	{ "BPRE", 0x0809FB70u, 0x0809FB84u, 0x0203998Cu,          // primaries rev0, alts rev1
	          0x0809BBC0u, 0x0809BBD4u, 0x0809BD8Cu, 0x0809BDA0u,
	          0x0810DEA0u, 0x0810DF18u, 0x08106ECCu, 0x08106F44u,
	          0, 0, 0, 7, 0 },
	{ "BPGE", 0x0809FB58u, 0x0809FB44u, 0x0203998Cu,          // primaries rev1, alts rev0
	          0x0809BBA8u, 0x0809BB94u, 0x0809BD74u, 0x0809BD60u,
	          0x0810DEF0u, 0x0810DE78u, 0x08106F1Cu, 0x08106EA4u,
	          0, 0, 0, 7, 0 },
	{ "AXVE", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },   // the ROM ban: all zero
	{ "AXPE", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
};
static void test_family22_columns(void) {
	printf("TEST 13: phase-22.1 keyboard+lists anchors, per game\n");
	for (unsigned i = 0; i < sizeof FAM22 / sizeof FAM22[0]; i++) {
		const Fam22* f = &FAM22[i];
		const GameProfile* p = prof(f->code);
		CHECK(p != NULL, "%s row exists", f->code);
		if (!p) continue;
		EQU(p->namingCb,       f->namingCb,       "%s namingCb", f->code);
		EQU(p->namingCbAlt,    f->namingCbAlt,    "%s namingCbAlt", f->code);
		EQU(p->namingPtr,      f->namingPtr,      "%s namingPtr", f->code);
		EQU(p->buyTask,        f->buyTask,        "%s buyTask", f->code);
		EQU(p->buyTaskAlt,     f->buyTaskAlt,     "%s buyTaskAlt", f->code);
		EQU(p->buyQtyTask,     f->buyQtyTask,     "%s buyQtyTask", f->code);
		EQU(p->buyQtyTaskAlt,  f->buyQtyTaskAlt,  "%s buyQtyTaskAlt", f->code);
		EQU(p->pcItemTask,     f->pcItemTask,     "%s pcItemTask", f->code);
		EQU(p->pcItemTaskAlt,  f->pcItemTaskAlt,  "%s pcItemTaskAlt", f->code);
		EQU(p->lmDummyTask,    f->lmDummyTask,    "%s lmDummyTask", f->code);
		EQU(p->lmDummyTaskAlt, f->lmDummyTaskAlt, "%s lmDummyTaskAlt", f->code);
		EQU(p->bagPocket,      f->bagPocket,      "%s bagPocket", f->code);
		EQU(p->dexTask,        f->dexTask,        "%s dexTask", f->code);
		EQU(p->dexView,        f->dexView,        "%s dexView", f->code);
		EQU(p->buyListSlot,    f->buyListSlot,    "%s buyListSlot", f->code);
		EQU(p->pcItemListSlot, f->pcItemListSlot, "%s pcItemListSlot", f->code);
		// structural rules: ROM anchors in ROM space; RAM ptrs in EWRAM; alt != primary when set
		if (p->namingCb)   CHECK(p->namingCb >> 24 == 0x08, "%s namingCb in ROM", f->code);
		if (p->namingPtr)  CHECK(p->namingPtr >> 24 == 0x02, "%s namingPtr in EWRAM", f->code);
		if (p->dexView)    CHECK(p->dexView  >> 24 == 0x02, "%s dexView in EWRAM", f->code);
		if (p->namingCbAlt) CHECK(p->namingCbAlt != p->namingCb, "%s naming alt distinct", f->code);
		if (p->buyTaskAlt)  CHECK(p->buyTaskAlt  != p->buyTask,  "%s buy alt distinct", f->code);
	}
}

// ============================================================================================
// TEST 14 — PHASE 22.1 BEHAVIOUR through the real game_read: naming + list contexts resolve,
// the slot plumbing finds the right ListMenu, precedence holds, RS never fires.
// ============================================================================================
static void test_family22_behaviour(void) {
	printf("TEST 14: phase-22.1 behaviour — naming/list detection through game_read\n");

	// --- (a) GCTX_NAMING beats the FULLUI class it also appears in (both FR revisions + EM).
	{
		GbaCore c; bus_reset(&c, "BPEE");
		const GameProfile* p = profile_for(&c);
		GameState gs;
		bus_w32(&c, p->mainCb2, p->namingCb | 1u);
		game_read(&c, p, &gs);
		EQU(gs.ctx, GCTX_NAMING, "BPEE: CB2_NamingScreen -> GCTX_NAMING (not FULLUI)");
		CHECK(gs.ctxResolved, "BPEE: naming is a positive match");
	}
	{
		GbaCore c; bus_reset(&c, "BPRE");
		const GameProfile* p = profile_for(&c);
		GameState gs;
		bus_w32(&c, p->mainCb2, p->namingCbAlt | 1u);        // the user's rev1 cart
		game_read(&c, p, &gs);
		EQU(gs.ctx, GCTX_NAMING, "BPRE rev1: naming alt -> GCTX_NAMING");
		bus_w32(&c, p->mainCb2, p->namingCb | 1u);           // rev0 primary
		game_read(&c, p, &gs);
		EQU(gs.ctx, GCTX_NAMING, "BPRE rev0: naming primary -> GCTX_NAMING");
	}
	// --- (b) mart buy: task + data[7] slot -> listBase; both revisions.
	{
		GbaCore c; bus_reset(&c, "BPEE");
		const GameProfile* p = profile_for(&c);
		GameState gs;
		put_task(&c, p, 4, p->buyTask | 1u, 1);
		bus_w8(&c, p->gTasksBase + 40u * 4u + 8u + 2u * 7u, 9);   // data[7] = listTaskId 9
		game_read(&c, p, &gs);
		EQU(gs.ctx, GCTX_LIST, "BPEE: Task_BuyMenu -> GCTX_LIST");
		EQU(gs.listKind, LK_BUY, "BPEE: kind LK_BUY");
		EQU(gs.listBase, p->gTasksBase + 40u * 9u + 8u, "BPEE: buy list resolved via data[7]");
	}
	{
		GbaCore c; bus_reset(&c, "BPRE");
		const GameProfile* p = profile_for(&c);
		GameState gs;
		put_task(&c, p, 2, p->buyTaskAlt | 1u, 1);           // rev1
		bus_w8(&c, p->gTasksBase + 40u * 2u + 8u + 2u * 7u, 3);
		game_read(&c, p, &gs);
		EQU(gs.ctx, GCTX_LIST, "BPRE rev1: buy alt task -> GCTX_LIST");
		EQU(gs.listBase, p->gTasksBase + 40u * 3u + 8u, "BPRE rev1: buy list via data[7]");
	}
	// --- (c) an INVALID slot value degrades to listBase 0 with the ctx still positive (L10:
	//     the driver emits nothing; the screen never falls back to walk keys).
	{
		GbaCore c; bus_reset(&c, "BPEE");
		const GameProfile* p = profile_for(&c);
		GameState gs;
		put_task(&c, p, 4, p->buyTask | 1u, 1);
		bus_w8(&c, p->gTasksBase + 40u * 4u + 8u + 2u * 7u, 200);   // listTaskId out of range
		game_read(&c, p, &gs);
		EQU(gs.ctx, GCTX_LIST, "BPEE: bad slot still GCTX_LIST (positive)");
		EQU(gs.listBase, 0, "BPEE: ...with listBase 0 (driver emits nothing)");
	}
	// --- (d) the qty roller task -> LK_QTY (no listBase needed).
	{
		GbaCore c; bus_reset(&c, "BPEE");
		const GameProfile* p = profile_for(&c);
		GameState gs;
		put_task(&c, p, 4, p->buyQtyTask | 1u, 1);
		game_read(&c, p, &gs);
		EQU(gs.ctx, GCTX_LIST, "BPEE: qty task -> GCTX_LIST");
		EQU(gs.listKind, LK_QTY, "BPEE: kind LK_QTY");
	}
	// --- (e) PC items: EM data[5], FR data[0] — the per-engine slot difference exercised.
	{
		GbaCore c; bus_reset(&c, "BPEE");
		const GameProfile* p = profile_for(&c);
		GameState gs;
		put_task(&c, p, 6, p->pcItemTask | 1u, 1);
		bus_w8(&c, p->gTasksBase + 40u * 6u + 8u + 2u * 5u, 11);   // EM: data[5]
		game_read(&c, p, &gs);
		EQU(gs.ctx, GCTX_LIST, "BPEE: ItemStorage_ProcessInput -> GCTX_LIST");
		EQU(gs.listKind, LK_PCITEM, "BPEE: kind LK_PCITEM");
		EQU(gs.listBase, p->gTasksBase + 40u * 11u + 8u, "BPEE: pc list via data[5]");
	}
	{
		GbaCore c; bus_reset(&c, "BPRE");
		const GameProfile* p = profile_for(&c);
		GameState gs;
		put_task(&c, p, 6, p->pcItemTaskAlt | 1u, 1);        // rev1 Task_ItemPcMain
		bus_w8(&c, p->gTasksBase + 40u * 6u + 8u + 2u * 0u, 12);   // FR: data[0]
		game_read(&c, p, &gs);
		EQU(gs.ctx, GCTX_LIST, "BPRE rev1: Task_ItemPcMain -> GCTX_LIST");
		EQU(gs.listBase, p->gTasksBase + 40u * 12u + 8u, "BPRE rev1: pc list via data[0]");
	}
	// --- (f) EM dex: the input task alone resolves LK_DEX (key-injection-only adapter).
	{
		GbaCore c; bus_reset(&c, "BPEE");
		const GameProfile* p = profile_for(&c);
		GameState gs;
		bus_w32(&c, p->mainCb2, 0x080BB774u | 1u);           // CB2_Pokedex [exact]
		put_task(&c, p, 5, p->dexTask | 1u, 1);
		game_read(&c, p, &gs);
		EQU(gs.ctx, GCTX_LIST, "BPEE: Task_HandlePokedexInput -> GCTX_LIST");
		EQU(gs.listKind, LK_DEX, "BPEE: kind LK_DEX");
		// ...and the dex WITHOUT the list task (entry page etc.) stays FULLUI.
		put_task(&c, p, 5, p->dexTask | 1u, 0);              // task inactive
		game_read(&c, p, &gs);
		EQU(gs.ctx, GCTX_FULLUI, "BPEE: dex entry page (no list task) stays GCTX_FULLUI");
	}
	// --- (g) precedence: the BAG still outranks everything (its check runs first), and a
	//     yes/no popup over the buy menu resolves FIELDMENU (sub-menus are sMenu — L8/L-C).
	{
		GbaCore c; bus_reset(&c, "BPEE");
		const GameProfile* p = profile_for(&c);
		GameState gs;
		put_task(&c, p, 4, p->buyTask | 1u, 1);
		put_task(&c, p, 7, p->yesNoTask | 1u, 1);
		game_read(&c, p, &gs);
		EQU(gs.ctx, GCTX_FIELDMENU, "BPEE: yes/no popup outranks the buy list");
	}
	// --- (h) RS: the whole family is inert by construction (all-zero anchors).
	{
		GbaCore c; bus_reset(&c, "AXVE");
		const GameProfile* p = profile_for(&c);
		GameState gs;
		bus_w32(&c, p->mainCb2, 0x0809FB58u | 1u);           // an FRLG naming cb2 on an RS core
		game_read(&c, p, &gs);
		CHECK(gs.ctx != GCTX_NAMING && gs.ctx != GCTX_LIST,
		      "AXVE: family contexts never fire (got %d)", gs.ctx);
	}
	// --- (i) the ctx name table kept up.
	CHECK(!strcmp(gamestate_ctx_name(GCTX_NAMING), "naming"), "GCTX_NAMING prints as 'naming'");
	CHECK(!strcmp(gamestate_ctx_name(GCTX_LIST),   "list"),   "GCTX_LIST prints as 'list'");
	CHECK(!strcmp(gamestate_ctx_name(GCTX_FULLUI), "fullui"), "existing names undisturbed");
}

// ============================================================================================
// TEST 15 — PHASE 22.2: the storage GRID columns, pinned per game, + behaviour through the
// real game_read (SPEC-family-grid G1). EM values = the local pokeemerald.sym re-read
// (2026-08-14, gamestate.c row comment carries the per-symbol addresses); every other game 0 =
// the named degradation (FRLG different module unmapped; RS ban).
// ============================================================================================
static void test_family22_2_storage(void) {
	printf("TEST 15: phase-22.2 storage grid columns + behaviour\n");
	static const struct {
		const char* code;
		uint32_t storageCb, storageCbAlt, stStorage, stCursor, pcStoragePtr;
	} T[] = {
		{ "BPEE", 0x080C7D54u, 0, 0x02039D08u, 0x02039D78u, 0x03005D94u },
		{ "BPRE", 0, 0, 0, 0, 0 },
		{ "BPGE", 0, 0, 0, 0, 0 },
		{ "AXVE", 0, 0, 0, 0, 0 },
		{ "AXPE", 0, 0, 0, 0, 0 },
	};
	for (unsigned i = 0; i < sizeof T / sizeof T[0]; i++) {
		const GameProfile* p = prof(T[i].code);
		CHECK(p != NULL, "%s row exists", T[i].code);
		if (!p) continue;
		EQU(p->storageCb,    T[i].storageCb,    "%s storageCb", T[i].code);
		EQU(p->storageCbAlt, T[i].storageCbAlt, "%s storageCbAlt", T[i].code);
		EQU(p->stStorage,    T[i].stStorage,    "%s stStorage", T[i].code);
		EQU(p->stCursor,     T[i].stCursor,     "%s stCursor", T[i].code);
		EQU(p->pcStoragePtr, T[i].pcStoragePtr, "%s pcStoragePtr", T[i].code);
	}
	// structure: the contiguity the driver relies on (spec §1.1 — one base, doc'd offsets) is a
	// property of the SHIPPED addresses, so pin the relation, not just the raw values.
	{
		const GameProfile* p = prof("BPEE");
		if (p) {
			EQU(p->stStorage + 0x70u, p->stCursor, "BPEE: sCursorArea = sStorage + 0x70 (map fact)");
			CHECK((p->stCursor >> 24) == 0x02 && (p->stStorage >> 24) == 0x02,
			      "BPEE: storage statics are EWRAM");
			CHECK((p->storageCb >> 24) == 0x08, "BPEE: storageCb is ROM");
			CHECK((p->pcStoragePtr >> 24) == 0x03, "BPEE: gPokemonStoragePtr is IWRAM");
		}
	}
	// behaviour (a): EM CB2_PokeStorage -> GCTX_STORAGE, a POSITIVE match, and it beats the
	// cb2FullUi row that also lists the same value (the G1 precedence).
	{
		GbaCore c; bus_reset(&c, "BPEE");
		const GameProfile* p = profile_for(&c);
		GameState gs;
		bus_w32(&c, p->mainCb2, p->storageCb | 1u);
		game_read(&c, p, &gs);
		EQU(gs.ctx, GCTX_STORAGE, "BPEE: CB2_PokeStorage -> GCTX_STORAGE (not FULLUI)");
		CHECK(gs.ctxResolved, "BPEE: storage is a positive match");
		CHECK(strcmp(gamestate_ctx_name(GCTX_STORAGE), "stor") == 0, "ctx name is 'stor'");
		int found = 0;   // the value must STAY in the fullui list (TEST 11 pins the list exact)
		for (int i = 0; i < GS_N_FULLUI; i++) if (p->cb2FullUi[i] == p->storageCb) found = 1;
		CHECK(found, "BPEE: CB2_PokeStorage remains in cb2FullUi (storage merely tests first)");
	}
	// behaviour (b): FR's OWN CB2_PokeStorage (0x0808CDD8, census-harvested) stays GCTX_FULLUI —
	// the named degradation, not a leak into the walk fall-through.
	{
		GbaCore c; bus_reset(&c, "BPRE");
		const GameProfile* p = profile_for(&c);
		GameState gs;
		bus_w32(&c, p->mainCb2, 0x0808CDD8u | 1u);
		game_read(&c, p, &gs);
		EQU(gs.ctx, GCTX_FULLUI, "BPRE: FR storage cb2 -> GCTX_FULLUI (named degradation)");
	}
}

// ============================================================================================
// TEST 16 — PHASE 23: the FAM-DLG pager whitelist (GameProfile.cb2Pager). Three properties, and
// the SECOND one is the load-bearing safety claim of the whole tap-advance family:
//   (a) the values are exactly what the row comments cite (VERIFIED-SRC page/value verbs);
//   (b) every pager entry is ALSO in that row's cb2FullUi list — a pager cb2 that never reached
//       GCTX_FULLUI would be dead code, and worse, a pager cb2 that is NOT a classified screen
//       would mean someone had added detection through the back door;
//   (c) the screens with a RICHER handler are NOT in it (naming / storage / bag / summary-adjacent
//       list screens keep their own family — the plan's "keep screens that already have a richer
//       handler out of the tap-advance class").
// ============================================================================================
static void test_family23_pager(void) {
	printf("TEST 16: phase-23 FAM-DLG pager whitelist (cb2Pager)\n");
	static const uint32_t EM_PAGER[GS_N_PAGER] = { 0x081BFAB4u, 0x080BA4B0u };   // summary, options
	static const uint32_t FR_PAGER[GS_N_PAGER] = { 0x08137F60u, 0x08088370u };   // summary, options
	static const uint32_t NONE_P[GS_N_PAGER]   = { 0 };
	struct { const char* code; const uint32_t* pager; } W[] = {
		{ "BPEE", EM_PAGER }, { "BPRE", FR_PAGER },
		{ "BPGE", NONE_P   },   // LG class lists are empty -> a pager could never be consulted
		{ "AXVE", NONE_P   }, { "AXPE", NONE_P },   // RS ROM-address ban
	};
	for (unsigned i = 0; i < sizeof W / sizeof W[0]; i++) {
		const GameProfile* p = prof(W[i].code);
		if (!p) { CHECK(0, "%s row missing", W[i].code); continue; }
		for (int k = 0; k < GS_N_PAGER; k++)
			EQU(p->cb2Pager[k], W[i].pager[k], "%s cb2Pager[%d]", W[i].code, k);
		for (int k = 0; k < GS_N_PAGER; k++) {
			if (!p->cb2Pager[k]) continue;
			CHECK((p->cb2Pager[k] >> 24) == 0x08u, "%s pager 0x%08X is ROM-space", W[i].code, p->cb2Pager[k]);
			// (b) the subset property — the whole safety argument in one loop.
			int inFull = 0;
			for (int j = 0; j < GS_N_FULLUI; j++) if (p->cb2FullUi[j] == p->cb2Pager[k]) inFull = 1;
			CHECK(inFull, "%s pager 0x%08X is also in cb2FullUi (never new detection)", W[i].code, p->cb2Pager[k]);
			// and it must NOT be a TITLE-class screen (edge zones on the main menu would be wrong)
			for (int j = 0; j < GS_N_TITLE; j++)
				CHECK(p->cb2Title[j] != p->cb2Pager[k], "%s pager 0x%08X is not a TITLE screen", W[i].code, p->cb2Pager[k]);
			// no duplicates within the list
			for (int j = k + 1; j < GS_N_PAGER; j++)
				CHECK(p->cb2Pager[j] != p->cb2Pager[k], "%s pager 0x%08X listed once only", W[i].code, p->cb2Pager[k]);
		}
		// (c) screens that own a richer family must never be pagers.
		for (int k = 0; k < GS_N_PAGER; k++) {
			if (!p->cb2Pager[k]) continue;
			CHECK(!p->namingCb  || p->cb2Pager[k] != p->namingCb,  "%s pager != namingCb", W[i].code);
			CHECK(!p->namingCbAlt|| p->cb2Pager[k] != p->namingCbAlt,"%s pager != namingCbAlt", W[i].code);
			CHECK(!p->storageCb || p->cb2Pager[k] != p->storageCb, "%s pager != storageCb", W[i].code);
			CHECK(!p->cb2BagRun || p->cb2Pager[k] != p->cb2BagRun, "%s pager != cb2BagRun", W[i].code);
			CHECK(!p->cb2BagRunAlt || p->cb2Pager[k] != p->cb2BagRunAlt, "%s pager != cb2BagRunAlt", W[i].code);
			CHECK(!p->cb2UpdParty || p->cb2Pager[k] != p->cb2UpdParty, "%s pager != cb2UpdParty", W[i].code);
			CHECK(!p->battleMainCb || p->cb2Pager[k] != p->battleMainCb, "%s pager != battleMainCb", W[i].code);
		}
	}
	// BEHAVIOUR: the two EM pager screens really do resolve to GCTX_FULLUI through the real
	// game_read — i.e. the family they opt into is actually the family they reach.
	{
		GbaCore c; bus_reset(&c, "BPEE");
		const GameProfile* p = profile_for(&c);
		GameState gs;
		for (int k = 0; k < GS_N_PAGER; k++) {
			if (!p->cb2Pager[k]) continue;
			bus_w32(&c, p->mainCb2, p->cb2Pager[k] | 1u);
			game_read(&c, p, &gs);
			EQU(gs.ctx, GCTX_FULLUI, "BPEE: pager cb2 0x%08X reaches GCTX_FULLUI", p->cb2Pager[k]);
			CHECK(gs.ctxResolved, "BPEE: pager cb2 0x%08X is a positive match", p->cb2Pager[k]);
		}
	}
	{   // and on FR, on the user's rev1 cart values
		GbaCore c; bus_reset(&c, "BPRE");
		const GameProfile* p = profile_for(&c);
		GameState gs;
		for (int k = 0; k < GS_N_PAGER; k++) {
			if (!p->cb2Pager[k]) continue;
			bus_w32(&c, p->mainCb2, p->cb2Pager[k] | 1u);
			game_read(&c, p, &gs);
			EQU(gs.ctx, GCTX_FULLUI, "BPRE: pager cb2 0x%08X reaches GCTX_FULLUI", p->cb2Pager[k]);
		}
	}
}

// TEST 17 — PHASE 24 (lane B1): sLockFieldControls, the column that ends the field-dialog
// walk-key leak. Every game gets one, because unlike the cb2 class lists this is IWRAM: the
// address is revision-insensitive, both FRLG maps agree with each other, and both RS maps agree
// with each other, so there is no rev-alternate to carry and no ROM-address ban to respect.
// What is graded: the exact values (a silent edit is how the BPGE row was wrong for two phases),
// the IWRAM-space property, the FRLG/RS sharing that the comments claim, and — the behavioural
// half — that game_read turns the byte into GameState.fieldLock without disturbing textDlg.
static void test_field_lock(void) {
	printf("TEST 17: phase-24 fieldLock (sLockFieldControls) — the field-dialog signal\n");
	struct { const char* code; uint32_t addr; } W[] = {
		{ "BPEE", 0x03000F2Cu },   // pokeemerald.sym — LIVE-VERIFIED in the emulator this session
		{ "BPRE", 0x03000F9Cu },   // pokefirered.sym == pokefirered_rev1.sym
		{ "BPGE", 0x03000F9Cu },   // pokeleafgreen.sym == pokeleafgreen_rev1.sym == FR (one IWRAM map)
		{ "AXVE", 0x030006A4u },   // pokeruby_rev2.sym
		{ "AXPE", 0x030006A4u },   // pokesapphire_rev2.sym — identical, hence the shared RAM body
	};
	for (unsigned i = 0; i < sizeof W / sizeof W[0]; i++) {
		const GameProfile* p = prof(W[i].code);
		if (!p) { CHECK(0, "%s row missing", W[i].code); continue; }
		EQU(p->fieldLock, W[i].addr, "%s fieldLock", W[i].code);
		CHECK(p->fieldLock != 0, "%s fieldLock is SET — every game gets the field-dialog fix, "
		      "including the two (RS) that never had a fieldMsgMode worth reading", W[i].code);
		CHECK((p->fieldLock >> 24) == 0x03u, "%s fieldLock 0x%08X is IWRAM (0x03xxxxxx) — which is "
		      "why no rev-alternate is needed", W[i].code, p->fieldLock);
		CHECK(p->fieldLock != p->fieldMsgMode, "%s fieldLock is a DIFFERENT byte from fieldMsgMode "
		      "(they answer different questions: 'a script owns the field' vs 'text is printing')",
		      W[i].code);
	}
	// The sharing the row comments assert, pinned so a future edit cannot silently break it.
	EQU(prof("BPGE")->fieldLock, prof("BPRE")->fieldLock, "FRLG share one IWRAM map");
	EQU(prof("AXPE")->fieldLock, prof("AXVE")->fieldLock, "RS share one IWRAM map");
	CHECK(prof("BPEE")->fieldLock != prof("BPRE")->fieldLock, "EM is its own build, not FR-derived");

	// Behaviour through the REAL game_read: the byte becomes GameState.fieldLock, and it is
	// independent of textDlg (the whole point — textDlg reads 0 while a box waits for A).
	for (unsigned i = 0; i < sizeof W / sizeof W[0]; i++) {
		const GameProfile* p = prof(W[i].code);
		if (!p) continue;
		GbaCore c; bus_reset(&c, W[i].code);
		GameState gs;
		bus_w8(&c, p->fieldLock, 0);
		if (p->fieldMsgMode) bus_w8(&c, p->fieldMsgMode, 0);
		game_read(&c, p, &gs);
		CHECK(!gs.fieldLock && !gs.textDlg, "%s: both bytes 0 -> fieldLock 0, textDlg 0", W[i].code);
		bus_w8(&c, p->fieldLock, 1);
		game_read(&c, p, &gs);
		CHECK(gs.fieldLock && !gs.textDlg,
		      "%s: lock byte set ALONE -> fieldLock 1 while textDlg stays 0 (exactly the case a "
		      "textDlg-only rule missed: the box waiting for A)", W[i].code);
		bus_w8(&c, p->fieldLock, 2);
		game_read(&c, p, &gs);
		CHECK(gs.fieldLock, "%s: a non-1 truthy byte still means locked", W[i].code);
	}
}


// ============================================================================================
// TEST 18 — PHASE 24 (lane B2): the FAM-MAP columns (rmPtr / rmFlyCb / rmWallCb) — the region
// map, and with it tap-to-fly. Three properties, and the third is the one that keeps this slice
// from being able to break anything:
//   (a) the EM values are exactly the three symbols the driver was derived against;
//   (b) rmPtr is the ONLY dereferenced value in the family, so it must be EWRAM, and the two cb2s
//       must be ROM — a swapped pair would read a callback as a struct;
//   (c) both cb2s are ALREADY in this row's cb2FullUi list, i.e. the screens detected BEFORE this
//       slice and only their tap semantics change. That is what makes the family unable to
//       "un-detect" anything, and it is pinned rather than asserted in a comment.
// Plus the behaviour through the REAL game_read: the fly map resolves GCTX_MAP with mapFly = 1,
// the wall map GCTX_MAP with mapFly = 0 (A EXITS there — the driver must never fire one), and a
// game with no anchors still lands in GCTX_FULLUI exactly as it does today.
static void test_family24_map(void) {
	printf("TEST 18: phase-24 FAM-MAP columns (region map / tap-to-fly)\n");
	const GameProfile* em = prof("BPEE");
	if (!em) { CHECK(0, "BPEE row missing"); return; }
	EQU(em->rmPtr,    0x0203A144u, "BPEE rmPtr = sRegionMap (pokeemerald.sym `0203a144 l 00000004`)");
	EQU(em->rmFlyCb,  0x081248D4u, "BPEE rmFlyCb = CB2_FlyMap");
	EQU(em->rmWallCb, 0x08170274u, "BPEE rmWallCb = MCB2_FieldUpdateRegionMap (the RUN loop — the "
	    "census correction; CB2_FieldShowRegionMap 0x08138C84 is only the setup)");
	CHECK((em->rmPtr >> 24) == 0x02u, "rmPtr is EWRAM — it is the one value here that gets "
	      "DEREFERENCED, so a ROM value would read a function as a struct");
	CHECK((em->rmFlyCb >> 24) == 0x08u && (em->rmWallCb >> 24) == 0x08u,
	      "both map cb2s are ROM addresses (compare-only, hence fail-safe)");
	CHECK(em->rmFlyCb != em->rmWallCb, "the two screens are distinguishable — which is exactly "
	      "what FR/LG could NOT do (one shared cb2), and why their columns are 0");
	// (c) the two cb2s must still be in cb2FullUi: GCTX_MAP is tested FIRST, so the fullui entry
	// is the fallback a profile without map anchors uses. TEST 11 pins that list exactly.
	int fly = 0, wall = 0;
	for (int i = 0; i < GS_N_FULLUI; i++) {
		if (em->cb2FullUi[i] == em->rmFlyCb)  fly = 1;
		if (em->cb2FullUi[i] == em->rmWallCb) wall = 1;
	}
	CHECK(fly && wall, "both map cb2s remain in BPEE's cb2FullUi list — this slice re-CLASSIFIES "
	      "two already-detected screens, it never makes one start or stop detecting");
	// The named degradations, pinned so a later edit has to argue with the suite.
	const char* zero[] = { "BPRE", "BPGE", "AXVE", "AXPE" };
	for (unsigned i = 0; i < 4; i++) {
		const GameProfile* q = prof(zero[i]);
		if (!q) { CHECK(0, "%s row missing", zero[i]); continue; }
		CHECK(q->rmPtr == 0 && q->rmFlyCb == 0 && q->rmWallCb == 0,
		      "%s FAM-MAP columns are 0 — FR/LG: ONE cb2 serves both map screens (fly-vs-wall is "
		      "not decidable from the callback) and no FRLG region-map struct pointer was "
		      "resolved; RS: the ROM/statics ban. Named degradation: FAM-DLG keeps those screens",
		      zero[i]);
	}

	// --- behaviour through the real game_read ---------------------------------------------
	{
		GbaCore c; bus_reset(&c, "BPEE");
		const GameProfile* p = profile_for(&c);
		GameState gs;
		bus_w32(&c, p->sb1ptr, 0x02025734u);                  // a loaded save (not a title screen)
		bus_w32(&c, p->mainCb2, p->rmFlyCb | 1u);             // the live cb2 carries the Thumb bit
		game_read(&c, p, &gs);
		EQU(gs.ctx, GCTX_MAP, "CB2_FlyMap -> GCTX_MAP (not the bare GCTX_FULLUI it was)");
		CHECK(gs.mapFly, "…with mapFly = 1: on the fly map an arrival A is the fly confirm");
		bus_w32(&c, p->mainCb2, p->rmWallCb | 1u);
		game_read(&c, p, &gs);
		EQU(gs.ctx, GCTX_MAP, "MCB2_FieldUpdateRegionMap -> GCTX_MAP");
		CHECK(!gs.mapFly, "…with mapFly = 0: on the WALL map A exits, so the driver emits none "
		      "(touchgeom.h rule M2)");
		// a different FULLUI screen is untouched by the new arm
		bus_w32(&c, p->mainCb2, 0x081BFAB4u | 1u);            // summary MainCB2
		game_read(&c, p, &gs);
		EQU(gs.ctx, GCTX_FULLUI, "a non-map FULLUI screen still classifies as GCTX_FULLUI");
		CHECK(!gs.mapFly, "…and mapFly is 0 outside GCTX_MAP");
	}
	{   // FR: the same callback that would be a map elsewhere must NOT become GCTX_MAP here
		GbaCore c; bus_reset(&c, "BPRE");
		const GameProfile* p = profile_for(&c);
		GameState gs;
		bus_w32(&c, p->sb1ptr, 0x02025734u);
		bus_w32(&c, p->mainCb2, 0x080C08C8u | 1u);            // FR CB2_RegionMap (census [exact])
		game_read(&c, p, &gs);
		CHECK(gs.ctx != GCTX_MAP, "BPRE's region map does NOT reach GCTX_MAP — with no anchors the "
		      "zero columns can never match, which is the fail-safe the family is built on");
	}
}


// ============================================================================================
// TEST 19 — PHASE 24 (lane B2): the DISCOVERED-LIST whitelist (GameProfile.cb2List) — the
// mechanism that gives TOUCH-PLAN E4 (FR Berry Pouch) and E5 (FR TM Case) a real list driver
// without a single new address. The properties that make it safe are the ones graded here:
//   (a) the two FR values are the census's [exact] cb2s, and BOTH are still inside cb2FullUi —
//       so a screen in this list can never fail to detect, it can only be READ differently;
//   (b) every other game is 0, including LeafGreen, where copying FR's values is the exact bug
//       that row was rebuilt to end;
//   (c) behaviour through the REAL game_read: with a live ListMenu dummy task present the screen
//       becomes GCTX_LIST / LK_FULLUI with listBase pointing at that task's struct; with NO live
//       list it falls back to GCTX_FULLUI (today's FAM-DLG behaviour) rather than to a driven
//       list with a garbage base; and an UNLISTED cb2 is never claimed even when a list is live.
static void test_family24_listcb2(void) {
	printf("TEST 19: phase-24 discovered-list whitelist (cb2List) — FR Berry Pouch + TM Case\n");
	const GameProfile* fr = prof("BPRE");
	if (!fr) { CHECK(0, "BPRE row missing"); return; }
	EQU(fr->cb2List[0], 0x0813CE78u, "BPRE cb2List[0] = CB2_BerryPouchIdle (E4)");
	EQU(fr->cb2List[1], 0x081318DCu, "BPRE cb2List[1] = CB2_Idle, the TM Case run loop (E5)");
	CHECK(fr->cb2List[2] == 0 && fr->cb2List[3] == 0, "the spare slots are 0 (never match)");
	CHECK(fr->lmDummyTask && fr->lmDummyTaskAlt, "the scan anchor exists on BOTH FR revisions — "
	      "the whitelist has no rev-alternate, but the part that IS rev-sensitive (finding the "
	      "list) is covered on rev0 and rev1 alike");
	for (int i = 0; i < 2; i++) {
		int in = 0;
		for (int j = 0; j < GS_N_FULLUI; j++) if (fr->cb2FullUi[j] == fr->cb2List[i]) in = 1;
		CHECK(in, "cb2List[%d] 0x%08X is still in BPRE's cb2FullUi list — the whitelist changes "
		      "how a tap READS, never whether the screen detects", i, fr->cb2List[i]);
	}
	const char* zero[] = { "BPEE", "BPGE", "AXVE", "AXPE" };
	for (unsigned i = 0; i < 4; i++) {
		const GameProfile* q = prof(zero[i]);
		if (!q) { CHECK(0, "%s row missing", zero[i]); continue; }
		int any = 0;
		for (int j = 0; j < GS_N_LISTCB2; j++) if (q->cb2List[j]) any = 1;
		CHECK(!any, "%s cb2List is all-0 (EM has neither screen and real anchors elsewhere; LG "
		      "never harvested its own values and must NEVER inherit FR's; RS is banned)", zero[i]);
	}

	// --- behaviour through the real game_read ---------------------------------------------
	GbaCore c; bus_reset(&c, "BPRE");
	const GameProfile* p = profile_for(&c);
	GameState gs;
	bus_w32(&c, p->sb1ptr, 0x02025734u);                    // a loaded save
	bus_w32(&c, p->mainCb2, p->cb2List[0] | 1u);            // the Berry Pouch is up

	// (c1) listed screen, NO live ListMenu -> the shipped FULLUI behaviour, not a driven list
	game_read(&c, p, &gs);
	EQU(gs.ctx, GCTX_FULLUI, "listed cb2 with no live ListMenu -> GCTX_FULLUI (no upgrade, and "
	    "certainly not a driven list with a garbage base)");
	CHECK(gs.listBase == 0, "…and listBase stays 0");

	// (c2) listed screen WITH a live ListMenu dummy task -> driven list, base = that task + 8
	put_task(&c, p, 5, p->lmDummyTaskAlt | 1u, 1);          // rev1 anchor, live in gTasks[5]
	game_read(&c, p, &gs);
	EQU(gs.ctx, GCTX_LIST, "Berry Pouch + a live ListMenu -> GCTX_LIST");
	EQU(gs.listKind, LK_FULLUI, "…resolved as LK_FULLUI (the discovered kind)");
	EQU(gs.listBase, p->gTasksBase + 40u * 5u + 8u, "…with listBase = gTasks[5] + 8, i.e. the "
	    "ListMenu struct the engine embedded in that task");
	// the rev0 anchor works identically (a rev0 cart still finds its own list)
	put_task(&c, p, 5, p->lmDummyTask | 1u, 1);
	game_read(&c, p, &gs);
	EQU(gs.ctx, GCTX_LIST, "the rev0 ListMenuDummyTask is found too");

	// (c3) the TM Case, the second entry
	bus_w32(&c, p->mainCb2, p->cb2List[1] | 1u);
	game_read(&c, p, &gs);
	EQU(gs.ctx, GCTX_LIST, "TM Case (CB2_Idle) + a live ListMenu -> GCTX_LIST");
	EQU(gs.listKind, LK_FULLUI, "…LK_FULLUI");

	// (c4) an UNLISTED fullui screen with the SAME live list is NOT claimed — this is the whole
	// safety argument for the whitelist, so it is graded rather than asserted in a comment.
	bus_w32(&c, p->mainCb2, 0x08137F60u | 1u);              // the FR summary screen
	game_read(&c, p, &gs);
	EQU(gs.ctx, GCTX_FULLUI, "an unlisted FULLUI screen is never claimed, even with a live "
	    "ListMenu task sitting in gTasks");
}

// ============================================================================================
// TEST 20 — PHASE 25 (lane C1): the INERT class — "detected, and deliberately SILENT".
// Two rules share one context and both are graded here, as columns AND as behaviour:
//   (a) row L2, the CREDITS, named by cb2 (GameProfile.cb2Inert). It has its own list rather
//       than living in cb2FullUi because the FAM-DLG hold verb would be a held B, which
//       pokeemerald credits.c:349 reads as the credits FAST-FORWARD. So the list must be
//       DISJOINT from the two class lists — a screen classifies exactly one way;
//   (b) row K4, FRLG QUEST-LOG PLAYBACK, named by STATE (GameProfile.questLog). The test is the
//       game's own QL_IS_PLAYBACK_STATE (2 or 3), never "non-zero" — ordinary FRLG play sits at
//       QL_STATE_RECORDING (1), and a "non-zero" guard would kill touch for the whole game.
//   Both run BEFORE every other rule in game_read, which is exactly what "do not touch this
//   screen" has to mean, so the behaviour half proves the guard beats a live menu task AND a
//   battle callback.
// It also pins the two live-harvested EM values actually classifying (rows E15/L1 and E8), and
// the ctx-name table, whose GCTX_MAP entry was MISSING (every region-map row printed "?").
// ============================================================================================
static void test_family25_inert(void) {
	printf("TEST 20: phase-25 INERT class (credits cb2 + FRLG quest-log playback)\n");
	CHECK(!strcmp(gamestate_ctx_name(GCTX_INERT), "inert"), "GCTX_INERT prints as 'inert'");
	CHECK(!strcmp(gamestate_ctx_name(GCTX_MAP),   "map"),   "GCTX_MAP prints as 'map' — the table "
	      "stopped at GCTX_STORAGE, so every phase-24 region-map row logged '?'");
	CHECK(!strcmp(gamestate_ctx_name(GCTX_STORAGE), "stor"), "existing names undisturbed");

	// --- (a) the columns, per game -----------------------------------------------------------
	struct { const char* code; uint32_t in0, in1, ql; } W[] = {
		{ "BPEE", 0x081754DCu, 0x08175620u, 0x00000000u },  // CB2_Credits + CB2_StartCreditsSequence
		{ "BPRE", 0x080F3A60u, 0x00000000u, 0x0203ADFAu },  // CB2_Credits ; gQuestLogState
		{ "BPGE", 0x00000000u, 0x00000000u, 0x0203ADFAu },  // no ROM values; the RAM one is shared
		{ "AXVE", 0x00000000u, 0x00000000u, 0x00000000u },
		{ "AXPE", 0x00000000u, 0x00000000u, 0x00000000u },
	};
	for (unsigned i = 0; i < sizeof W / sizeof W[0]; i++) {
		const GameProfile* p = prof(W[i].code);
		if (!p) { CHECK(0, "%s row missing", W[i].code); continue; }
		EQU(p->cb2Inert[0], W[i].in0, "%s cb2Inert[0]", W[i].code);
		EQU(p->cb2Inert[1], W[i].in1, "%s cb2Inert[1]", W[i].code);
		CHECK(p->cb2Inert[2] == 0 && p->cb2Inert[3] == 0, "%s cb2Inert spare slots are 0", W[i].code);
		EQU(p->questLog, W[i].ql, "%s questLog", W[i].code);
		if (p->questLog)
			CHECK((p->questLog >> 24) == 0x02u, "%s questLog is EWRAM (a STATE byte, not a ROM "
			      "address) — which is why it needs no rev-alternate", W[i].code);
		// DISJOINTNESS: an inert cb2 must appear in NEITHER class list, or the two rules would
		// disagree about the same screen.
		for (int k = 0; k < GS_N_INERT; k++) {
			if (!p->cb2Inert[k]) continue;
			CHECK((p->cb2Inert[k] >> 24) == 0x08u, "%s cb2Inert[%d] is ROM-space", W[i].code, k);
			for (int j = 0; j < GS_N_TITLE; j++)
				CHECK(p->cb2Title[j] != p->cb2Inert[k], "%s: inert cb2 0x%08X is not also TITLE",
				      W[i].code, p->cb2Inert[k]);
			for (int j = 0; j < GS_N_FULLUI; j++)
				CHECK(p->cb2FullUi[j] != p->cb2Inert[k], "%s: inert cb2 0x%08X is not also FULLUI",
				      W[i].code, p->cb2Inert[k]);
		}
	}
	// The two LG/RS rows must not have inherited FireRed's ROM value (the BPGE failure mode).
	{
		const GameProfile* lg = prof("BPGE");
		if (lg) CHECK(lg->cb2Inert[0] == 0, "BPGE never inherits FR's credits cb2");
	}

	// --- (b) behaviour: the CREDITS go inert, the harvest classifies ---------------------------
	{
		GbaCore c; bus_reset(&c, "BPEE");
		const GameProfile* p = profile_for(&c);
		GameState gs;
		bus_w32(&c, p->sb1ptr, 0x02025734u);                 // a loaded save under everything
		bus_w32(&c, p->mainCb2, p->cb2Inert[0] | 1u);        // CB2_Credits (Thumb bit set, as live)
		game_read(&c, p, &gs);
		EQU(gs.ctx, GCTX_INERT, "EM credits cb2 -> GCTX_INERT (not FULLUI: a held B is the "
		    "credits fast-forward, credits.c:349)");
		bus_w32(&c, p->mainCb2, p->cb2Inert[1] | 1u);
		game_read(&c, p, &gs);
		EQU(gs.ctx, GCTX_INERT, "…and so does its multi-frame starter");
		// The two LIVE-harvested values from this lane actually classify.
		bus_w32(&c, p->mainCb2, 0x08173560u | 1u);           // CB2_HallOfFame (E15 + L1)
		game_read(&c, p, &gs);
		EQU(gs.ctx, GCTX_FULLUI, "EM CB2_HallOfFame 0x08173560 -> GCTX_FULLUI (rows E15 + L1)");
		bus_w32(&c, p->mainCb2, 0x08121C64u | 1u);           // CB2_MailRead (E8)
		game_read(&c, p, &gs);
		EQU(gs.ctx, GCTX_FULLUI, "EM CB2_MailRead 0x08121C64 -> GCTX_FULLUI (row E8)");
		bus_w32(&c, p->mainCb2, 0x0813E3A4u | 1u);           // CB2_EvolutionSceneUpdate (C20)
		game_read(&c, p, &gs);
		EQU(gs.ctx, GCTX_FULLUI, "EM evolution scene -> GCTX_FULLUI (row C20)");
		// Emerald has NO quest log, so the guard can never fire there even if the FRLG address
		// happens to hold a playback value.
		bus_w8(&c, 0x0203ADFAu, 2);
		bus_w32(&c, p->mainCb2, p->cb2Title[0] | 1u);
		game_read(&c, p, &gs);
		EQU(gs.ctx, GCTX_TITLE, "EM is untouched by the FRLG quest-log guard (questLog == 0)");
		EQU(gs.questLogState, 0, "…and reports questLogState 0");
	}

	// --- (c) behaviour: the FRLG quest-log guard, and the boundary that makes it safe ----------
	{
		GbaCore c; bus_reset(&c, "BPRE");
		const GameProfile* p = profile_for(&c);
		GameState gs;
		bus_w32(&c, p->sb1ptr, 0x02025734u);
		bus_w32(&c, p->mainCb2, 0x080565C8u | 1u);           // CB2_Overworld — playback's own cb2
		put_task(&c, p, 2, p->startMenuTaskAlt | 1u, 1);     // the replay opens menus, too
		for (int v = 0; v <= 4; v++) {
			bus_w8(&c, p->questLog, (uint8_t)v);
			game_read(&c, p, &gs);
			EQU(gs.questLogState, v, "FR questLogState mirrors the raw byte (%d)", v);
			if (v == 2 || v == 3)
				EQU(gs.ctx, GCTX_INERT, "FR quest-log state %d (PLAYBACK) -> GCTX_INERT, beating "
				    "the live start-menu task", v);
			else
				EQU(gs.ctx, GCTX_FIELDMENU, "FR quest-log state %d is NOT playback — the menu "
				    "still detects (state 1 is RECORDING, i.e. ordinary play)", v);
		}
		// …and it beats the battle test too, which sits above every menu rule.
		bus_w32(&c, p->mainCb2, p->battleMainCbAlt | 1u);
		bus_w32(&c, p->bg0y, 160u);
		bus_w8(&c, p->questLog, 2);
		game_read(&c, p, &gs);
		EQU(gs.ctx, GCTX_INERT, "the playback guard runs before the battle test");
		bus_w8(&c, p->questLog, 1);
		game_read(&c, p, &gs);
		EQU(gs.ctx, GCTX_BATTLE_ACTION, "…and with RECORDING (1) the battle detects normally");
	}
}

int main(void) {
	printf("test_profiles — the per-game RAM map (source/gamestate.c PROFILES[])\n\n");
	test_lookup();
	test_sbdirect_table();
	test_sbdirect_behaviour();
	test_rs_no_rom();
	test_rs_rows_pinned();
	test_rs_addresses();
	test_rs_ident();
	test_address_sanity();
	test_peersprite_columns();   // phase 20
	test_rev_alternates();       // phase 22.0 (census S2 merge)
	test_screen_classes();       // phase 22.0
	test_phase22_behaviour();    // phase 22.0
	test_family22_columns();     // phase 22.1 (keyboard + lists)
	test_family22_behaviour();   // phase 22.1
	test_family22_2_storage();   // phase 22.2 (grid)
	test_family23_pager();       // phase 23 (FAM-DLG pager whitelist)
	test_field_lock();           // phase 24 (lane B1) sLockFieldControls
	test_family24_map();         // phase 24 (lane B2) FAM-MAP region map / tap-to-fly
	test_family24_listcb2();     // phase 24 (lane B2) discovered-list whitelist (FR E4/E5)
	test_family25_inert();       // phase 25 (lane C1) INERT class: credits + FRLG quest log
	printf("\n=== %d checks, %d failures ===\n", g_checks, g_fails);
	return g_fails ? 1 : 0;
}
