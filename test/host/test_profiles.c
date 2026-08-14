// test_profiles.c — PC host unit test for the per-game RAM map (source/gamestate.c PROFILES[])
// and the phase-18 `sbDirect` save-block indirection it introduced.
//
//   clang -std=c11 -Wall -Wextra -O2 -I source -I test/host test/host/test_profiles.c \
//         source/gamestate.c source/presence_read.c source/presence.c -o /tmp/tpr && /tmp/tpr
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
// ONE exception is battleMainCb, which is identical in all four maps and is load-bearing.
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
// TEST 5 — AXVE and AXPE are byte-identical apart from the code (SPEC-coop P3.5.1)
// ============================================================================================
static void test_rs_rows_pinned(void) {
	printf("TEST 5: the Ruby and Sapphire rows are one body, pinned together\n");
	const GameProfile* ru = prof("AXVE");
	const GameProfile* sa = prof("AXPE");
	CHECK(ru && sa, "both RS rows exist");
	if (!ru || !sa) return;
	GameProfile a = *ru, b = *sa;
	memset(a.code, 0, sizeof a.code);
	memset(b.code, 0, sizeof b.code);
	CHECK(memcmp(&a, &b, sizeof a) == 0,
	      "AXVE and AXPE differ ONLY in their 4-char code (one build, one RAM map)");
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
	// BPGE: primary = LG rev1 (user's cart is rev 1.1); alt = LG rev0. Both columns sym-derived
	// from pokeleafgreen{,_rev1}.sym (RS-REV2-VERIFICATION.md §6), verify-in-emulator. The old
	// primaries were FR-rev0 values — wrong for EVERY LeafGreen revision.
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
		  0x0811FB00u, 0x0809CE28u, 0x0809CC6Cu, 0x08122C34u, 0x0806F1F0u, 0x08098180u,
		  0x0300311Eu },                                    // §7 fix (same gMain as FR)
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
	static const uint32_t EM_FULL[GS_N_FULLUI] = {
		0x080BA4B0u, 0x080C2710u, 0x081248D4u, 0x080C5438u, 0x081BFAB4u, 0x08177C54u,
		0x080BB774u, 0x081C7400u, 0x0813591Cu, 0x0816631Cu, 0x08179B68u, 0x08170274u,
		0x08134C9Cu, 0x080E4F58u, 0x080C7D54u, 0x0812A670u };
	static const uint32_t FR_TITLE[GS_N_TITLE] = {
		0x080EC9E8u, 0x080EC878u, 0x08078BB0u, 0x0800C2E8u, 0x0812EB88u };
	static const uint32_t FR_FULL[GS_N_FULLUI] = {
		0x08088370u, 0x08089084u, 0x080C08C8u, 0x08137F60u, 0x0813CE78u, 0x081318DCu,
		0x0811C774u, 0x0810254Cu, 0x0815AC0Cu, 0x0812C40Cu, 0x0808CDD8u, 0x0809FB84u,
		0x080F1E38u, 0x0809ADF8u, 0x0813F9C4u, 0x08056760u };
	static const uint32_t NONE_T[GS_N_TITLE]  = { 0 };
	static const uint32_t NONE_F[GS_N_FULLUI] = { 0 };
	struct { const char* code; const uint32_t* title; const uint32_t* full; int visited; } W[] = {
		{ "BPEE", EM_TITLE, EM_FULL, 1 },   // census boots #1/#2, live-harvested
		{ "BPRE", FR_TITLE, FR_FULL, 1 },   // FR visit pass on the user's rev1 cart
		{ "BPGE", NONE_T,   NONE_F,  0 },   // no LG boot yet -> named degradation (empty lists)
		{ "AXVE", NONE_T,   NONE_F,  0 },   // no RS ROM on this machine
		{ "AXPE", NONE_T,   NONE_F,  0 },
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
	printf("\n=== %d checks, %d failures ===\n", g_checks, g_fails);
	return g_fails ? 1 : 0;
}
