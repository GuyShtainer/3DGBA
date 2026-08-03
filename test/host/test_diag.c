// test_diag.c — PC host unit test for the D1+D2 diagnostics module (source/diag.{c,h}): crumb
// encode/decode for every SPEC D1.1 site, the watchdog escalation state machine, the exact
// STUCK line format, the D2 hang-catcher trigger edge logic (>180-frozen-render-frames, 24-cap,
// re-arm), and the D2 register-dump formatter golden bytes. Pure-C dual-compile per CLAUDE.md
// rule #4 / PHASE.md invariant 3-4; spec: docs/phase13-diagnostics/SPEC-firmware-diag.md
// §D1.9 + §D2.7.
//
//   clang -std=c11 -Wall -Wextra -O2 -I source test/host/test_diag.c -o /tmp/td && /tmp/td
//
// (No mock <3ds.h> needed — diag.c is header-free pure C by design.)

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "../../source/diag.c"

static int g_checks = 0, g_fail = 0;
#define CHECK(cond, ...) do { \
	g_checks++; \
	if (!(cond)) { g_fail++; printf("  [FAIL] "); printf(__VA_ARGS__); printf("   (at %s:%d)\n", __FILE__, __LINE__); } \
} while (0)

// --------------------------------------------------------------------------------------------
// TEST 1 — crumb encode/decode round-trip for EVERY site ID in the SPEC D1.1 tables.
// Decode rule (diag.h): site = crumb - crumb % 100, iter residue = crumb % 100 (mask 0x3F keeps
// every residue < 64 < the 100 spacing, so decode is exact for all sites — the BUILDLOG-recorded
// deviation from the spec's aliasing &0xFF).
// --------------------------------------------------------------------------------------------
static void test_crumbs(void) {
	printf("TEST 1: crumb encode/decode round-trip (all D1.1 sites)\n");
	static const uint32_t SITES[] = {
		DIAG_SITE_NET_COLLECT, DIAG_SITE_NET_CMD_COLLECT, DIAG_SITE_NET_ROUND_WAIT,
		DIAG_SITE_NET_RX_PASS, DIAG_SITE_NET_LOBBY_DRAIN,
		DIAG_SITE_SIO_CELIO_GATE, DIAG_SITE_SIO_CELIO_ISR, DIAG_SITE_SIO_CELIO_PACE,
		DIAG_SITE_SIO_ISR_GATE, DIAG_SITE_SIO_ROUND_OPEN, DIAG_SITE_SIO_FINISH,
		DIAG_SITE_MAIN_PIPE_WAIT, DIAG_SITE_MAIN_WL_TEARDOWN, DIAG_SITE_MAIN_RX_JOIN,
	};
	static const uint32_t ITERS[] = { 0, 1, 63, 64, 201, 0xFFFFFFFFu };   // incl. mask wraparounds
	volatile uint32_t crumb = 0;
	for (size_t s = 0; s < sizeof SITES / sizeof SITES[0]; s++) {
		for (size_t i = 0; i < sizeof ITERS / sizeof ITERS[0]; i++) {
			DIAG_CRUMB(crumb, SITES[s], ITERS[i]);
			uint32_t c = crumb;
			CHECK(c == SITES[s] + (ITERS[i] & 0x3Fu), "encode site %lu iter %lu -> %lu\n",
			      (unsigned long)SITES[s], (unsigned long)ITERS[i], (unsigned long)c);
			CHECK(DIAG_CRUMB_SITE(c) == SITES[s], "decode site of %lu: got %lu want %lu\n",
			      (unsigned long)c, (unsigned long)DIAG_CRUMB_SITE(c), (unsigned long)SITES[s]);
			CHECK(DIAG_CRUMB_ITER(c) == (ITERS[i] & 0x3Fu), "decode iter of %lu: got %lu want %lu\n",
			      (unsigned long)c, (unsigned long)DIAG_CRUMB_ITER(c), (unsigned long)(ITERS[i] & 0x3Fu));
		}
	}
	// The real globals exist and take a stamp (link-level sanity for the stamped TUs).
	DIAG_CRUMB(g_diagNetCrumb, DIAG_SITE_NET_COLLECT, 7);
	DIAG_CRUMB(g_diagSioCrumb, DIAG_SITE_SIO_FINISH, 12);
	DIAG_CRUMB(g_diagMainCrumb, DIAG_SITE_MAIN_PIPE_WAIT, 0);
	CHECK(g_diagNetCrumb == 1007 && g_diagSioCrumb == 3012 && g_diagMainCrumb == 4000,
	      "global crumb stores: net=%lu sio=%lu main=%lu\n",
	      (unsigned long)g_diagNetCrumb, (unsigned long)g_diagSioCrumb, (unsigned long)g_diagMainCrumb);
}

// --------------------------------------------------------------------------------------------
// TEST 2 — watchdog state machine (SPEC D1.9 item 2).
// --------------------------------------------------------------------------------------------
static DiagWdSample mk_sample(uint32_t aF, uint32_t rx) {
	DiagWdSample s;
	memset(&s, 0, sizeof s);
	s.renderSeq = 42; s.aFrame = aF; s.bFrame = 7; s.aVf = 3; s.bVf = 4; s.rxSeq = rx;
	s.netCrumb = 1005; s.sioCrumb = 3010; s.gateN = 3; s.forceN = 1; s.wl = 1; s.seat = 0;
	return s;
}

static void test_watchdog(void) {
	printf("TEST 2: watchdog escalation (healthy / freeze x3 / reset / re-freeze / mask)\n");
	DiagWd wd; memset(&wd, 0, sizeof wd);
	char line[192];
	uint32_t mask = DIAG_WD_AF | DIAG_WD_RX;

	// Healthy: aFrame advances every 200ms tick -> zero lines over 15s.
	int lines = 0;
	for (uint32_t t = 200; t <= 15000; t += 200) {
		DiagWdSample s = mk_sample(t / 200, 5);   // aFrame changes each tick
		lines += diag_wd_step(&wd, t, &s, mask, line, sizeof line);
	}
	CHECK(lines == 0, "healthy run emitted %d lines (want 0)\n", lines);

	// Full freeze: constant sample -> EXACTLY three lines, ms=1000/4000/12000, in order.
	memset(&wd, 0, sizeof wd);
	DiagWdSample fz = mk_sample(100, 5);
	uint32_t msSeen[8]; int nSeen = 0;
	(void)diag_wd_step(&wd, 200, &fz, mask, line, sizeof line);   // baseline (progress vs zeroed last)
	for (uint32_t t = 400; t <= 20000; t += 200) {
		if (diag_wd_step(&wd, t, &fz, mask, line, sizeof line)) {
			unsigned long msv = 0;
			CHECK(sscanf(line, "STUCK ms=%lu ", &msv) == 1, "line parse: '%s'\n", line);
			if (nSeen < 8) msSeen[nSeen] = (uint32_t)msv;
			nSeen++;
			// escalation timing: the ms=X line appears >= X ms after the first frozen sample
			CHECK(t - 400 + 200 >= (uint32_t)msv, "ms=%lu line at t=%lu (too early)\n", msv, (unsigned long)t);
		}
	}
	CHECK(nSeen == 3, "full freeze emitted %d lines (want 3)\n", nSeen);
	CHECK(nSeen >= 3 && msSeen[0] == 1000 && msSeen[1] == 4000 && msSeen[2] == 12000,
	      "ms sequence %lu/%lu/%lu (want 1000/4000/12000)\n",
	      (unsigned long)msSeen[0], (unsigned long)msSeen[1], (unsigned long)msSeen[2]);

	// Progress mid-episode resets; a SECOND freeze emits three FRESH lines.
	{
		DiagWdSample s2 = mk_sample(101, 5);                       // aFrame advanced -> progress
		CHECK(diag_wd_step(&wd, 20200, &s2, mask, line, sizeof line) == 0, "progress emitted a line\n");
		CHECK(wd.stuckSinceMs == 0 && wd.fired == 0, "progress did not reset the episode\n");
		int n2 = 0;
		for (uint32_t t = 20400; t <= 40000; t += 200)
			n2 += diag_wd_step(&wd, t, &s2, mask, line, sizeof line);
		CHECK(n2 == 3, "second freeze emitted %d lines (want 3)\n", n2);
	}

	// Sampler gap: a freeze whose next step jumps past ALL thresholds still emits 1s,4s,12s in
	// order (ascending one-per-call), never just the highest.
	{
		memset(&wd, 0, sizeof wd);
		DiagWdSample s3 = mk_sample(500, 9);
		(void)diag_wd_step(&wd, 1000, &s3, mask, line, sizeof line);           // baseline/progress
		CHECK(diag_wd_step(&wd, 1200, &s3, mask, line, sizeof line) == 0, "episode-start emitted\n");
		unsigned long msv = 0;
		CHECK(diag_wd_step(&wd, 15000, &s3, mask, line, sizeof line) == 1
		      && sscanf(line, "STUCK ms=%lu ", &msv) == 1 && msv == 1000, "gap: 1st line ms=%lu\n", msv);
		CHECK(diag_wd_step(&wd, 15200, &s3, mask, line, sizeof line) == 1
		      && sscanf(line, "STUCK ms=%lu ", &msv) == 1 && msv == 4000, "gap: 2nd line ms=%lu\n", msv);
		CHECK(diag_wd_step(&wd, 15400, &s3, mask, line, sizeof line) == 1
		      && sscanf(line, "STUCK ms=%lu ", &msv) == 1 && msv == 12000, "gap: 3rd line ms=%lu\n", msv);
		CHECK(diag_wd_step(&wd, 15600, &s3, mask, line, sizeof line) == 0, "gap: 4th line emitted\n");
	}

	// watchMask honored: an UNWATCHED seq frozen alone (rx frozen, watched aFrame advancing)
	// emits nothing; and a watched-frozen freeze is invisible when only OTHER seqs are watched.
	{
		memset(&wd, 0, sizeof wd);
		int n3 = 0;
		for (uint32_t t = 200; t <= 15000; t += 200) {
			DiagWdSample s4 = mk_sample(t / 200, 5);               // rx pinned at 5, aFrame advancing
			n3 += diag_wd_step(&wd, t, &s4, DIAG_WD_AF | DIAG_WD_RX, line, sizeof line);
		}
		CHECK(n3 == 0, "unwatched-alone freeze emitted %d lines (want 0)\n", n3);
		memset(&wd, 0, sizeof wd);
		n3 = 0;
		for (uint32_t t = 200; t <= 15000; t += 200) {
			DiagWdSample s5 = mk_sample(100, t / 200);             // aFrame frozen but NOT watched
			n3 += diag_wd_step(&wd, t, &s5, DIAG_WD_RX, line, sizeof line);
		}
		CHECK(n3 == 0, "frozen-but-unwatched aFrame emitted %d lines (want 0)\n", n3);
	}

	// mask==0 (not armed, D1.7): even a total freeze emits nothing and clears the episode.
	{
		memset(&wd, 0, sizeof wd);
		DiagWdSample s6 = mk_sample(100, 5);
		int n4 = 0;
		for (uint32_t t = 200; t <= 15000; t += 200)
			n4 += diag_wd_step(&wd, t, &s6, 0, line, sizeof line);
		CHECK(n4 == 0, "mask==0 emitted %d lines (want 0)\n", n4);
		CHECK(wd.stuckSinceMs == 0 && wd.fired == 0, "mask==0 left an episode armed\n");
	}
}

// --------------------------------------------------------------------------------------------
// TEST 3 — STUCK line golden bytes + cap-truncation safety (SPEC D1.9 item 3, D1.5 format).
// --------------------------------------------------------------------------------------------
static void test_format(void) {
	printf("TEST 3: STUCK line golden string + truncation\n");
	DiagWdSample s = mk_sample(1000, 77);
	s.bFrame = 2000; s.aVf = 500; s.bVf = 600;
	char buf[192];
	int n = diag_stuck_format(buf, sizeof buf, 1000, &s);
	const char* want =
	    "STUCK ms=1000 rseq=42 aF=1000 bF=2000 aVf=500 bVf=600 rx=77 netC=1005 sioC=3010 gateN=3 forceN=1 wl=1 seat=0\n";
	CHECK(n == (int)strlen(want), "format length %d want %d\n", n, (int)strlen(want));
	CHECK(strcmp(buf, want) == 0, "golden mismatch:\n  got:  %s  want: %s", buf, want);

	// seat=-1 (no wireless role) renders as a signed -1, wl=0.
	s.wl = 0; s.seat = -1;
	diag_stuck_format(buf, sizeof buf, 4000, &s);
	CHECK(strstr(buf, "wl=0 seat=-1\n") != NULL, "wl/seat rendering: %s", buf);

	// Truncation: a tiny cap never overflows and stays NUL-terminated; return = would-be length.
	char tiny[16];
	memset(tiny, 0x7F, sizeof tiny);
	int n2 = diag_stuck_format(tiny, sizeof tiny, 12000, &s);
	CHECK(n2 > (int)sizeof tiny, "truncated return %d (want would-be full length)\n", n2);
	CHECK(tiny[sizeof tiny - 1] == '\0' || strlen(tiny) < sizeof tiny, "tiny not NUL-terminated\n");
	CHECK(strlen(tiny) == sizeof tiny - 1, "tiny wrote %zu chars (want cap-1)\n", strlen(tiny));
	CHECK(strncmp(tiny, "STUCK ms=12000 ", sizeof tiny - 1) == 0, "tiny prefix: %s\n", tiny);

	// Degenerate args are refused, never written through.
	CHECK(diag_stuck_format(NULL, 32, 1000, &s) < 0, "NULL out accepted\n");
	CHECK(diag_stuck_format(buf, 0, 1000, &s) < 0, "cap 0 accepted\n");
	CHECK(diag_stuck_format(buf, sizeof buf, 1000, NULL) < 0, "NULL sample accepted\n");
}

// --------------------------------------------------------------------------------------------
// TEST 4 — D2 hang-catcher trigger edge logic (SPEC D2.3 / D2.7 item 1). The freeze clock is
// the renderSeq delta, so "tick" below = one step call with renderSeq+1 (per-frame cadence);
// the tick-style 200ms cadence (renderSeq+12/call) is exercised at the end.
// --------------------------------------------------------------------------------------------
static void test_hang_step(void) {
	printf("TEST 4: hang-catcher edges (disarmed / CORE / GAME / cap 24 / re-arm / cadence)\n");
	DiagHang h; memset(&h, 0, sizeof h);
	uint32_t rs = 0;
	const int ARM_BOTH = DIAG_HANG_ARM_CORE | DIAG_HANG_ARM_GAME;

	// Disarmed: static everything, never fires; state stays cleared (D2.7 "disarmed -> never").
	int bad = 0;
	for (int i = 0; i < 400; i++)
		if (diag_hang_step(&h, 0, ++rs, 100, 200) != DIAG_HANG_NONE) bad++;
	CHECK(bad == 0, "disarmed fired %d times\n", bad);
	CHECK(h.dumps == 0 && h.frozenVf == 0 && h.frozenVbl == 0 && h.init == 0,
	      "disarmed left state armed (dumps=%d fVf=%u fVbl=%u init=%d)\n",
	      h.dumps, h.frozenVf, h.frozenVbl, h.init);

	// Tier A: baseline, then 180 static ticks -> NONE; tick 181 -> DUMP_CORE (spec: "static vf
	// for 180 ticks -> DUMP_CORE on tick 181"). vbl keeps advancing so only Tier A is frozen.
	memset(&h, 0, sizeof h); rs = 1000;
	uint32_t vbl = 200;
	CHECK(diag_hang_step(&h, ARM_BOTH, ++rs, 100, ++vbl) == DIAG_HANG_NONE, "baseline fired\n");
	bad = 0;
	for (int i = 1; i <= 180; i++)
		if (diag_hang_step(&h, ARM_BOTH, ++rs, 100, ++vbl) != DIAG_HANG_NONE) bad++;
	CHECK(bad == 0, "Tier A fired %d times before the 180-frame threshold\n", bad);
	CHECK(h.frozenVf == 180, "frozenVf=%u after 180 static ticks (want 180)\n", h.frozenVf);
	CHECK(diag_hang_step(&h, ARM_BOTH, ++rs, 100, ++vbl) == DIAG_HANG_DUMP_CORE,
	      "tick 181 did not DUMP_CORE\n");

	// Cap: one dump per call until 24 total, then silent while still frozen (D2.7 "24-cap honored").
	int dumps = 1;
	for (int i = 0; i < 100; i++)
		if (diag_hang_step(&h, ARM_BOTH, ++rs, 100, ++vbl) != DIAG_HANG_NONE) dumps++;
	CHECK(dumps == 24, "episode emitted %d dumps (want 24)\n", dumps);
	CHECK(h.dumps == 24, "dump counter %d (want 24)\n", h.dumps);

	// Re-arm: the counter advancing resets frozenN AND the cap; a later freeze is a fresh episode
	// with its own full edge + its own 24 (D2.7 "counter advance resets cap + frozenN").
	CHECK(diag_hang_step(&h, ARM_BOTH, ++rs, 101, ++vbl) == DIAG_HANG_NONE, "advance fired\n");
	CHECK(h.dumps == 0 && h.frozenVf == 0, "advance did not reset (dumps=%d fVf=%u)\n", h.dumps, h.frozenVf);
	bad = 0;
	for (int i = 1; i <= 180; i++)
		if (diag_hang_step(&h, ARM_BOTH, ++rs, 101, ++vbl) != DIAG_HANG_NONE) bad++;
	CHECK(bad == 0, "re-freeze fired %d times before threshold\n", bad);
	CHECK(diag_hang_step(&h, ARM_BOTH, ++rs, 101, ++vbl) == DIAG_HANG_DUMP_CORE, "re-freeze tick 181\n");
	dumps = 1;
	for (int i = 0; i < 100; i++)
		if (diag_hang_step(&h, ARM_BOTH, ++rs, 101, ++vbl) != DIAG_HANG_NONE) dumps++;
	CHECK(dumps == 24, "second episode emitted %d dumps (want fresh 24)\n", dumps);

	// Tier B: vf advancing + vbl static -> DUMP_GAME at the same edge (the IRQ-dead class).
	memset(&h, 0, sizeof h); rs = 0;
	uint32_t vf = 5;
	CHECK(diag_hang_step(&h, ARM_BOTH, ++rs, ++vf, 50) == DIAG_HANG_NONE, "B baseline fired\n");
	bad = 0;
	for (int i = 1; i <= 180; i++)
		if (diag_hang_step(&h, ARM_BOTH, ++rs, ++vf, 50) != DIAG_HANG_NONE) bad++;
	CHECK(bad == 0, "Tier B fired %d times before threshold\n", bad);
	CHECK(diag_hang_step(&h, ARM_BOTH, ++rs, ++vf, 50) == DIAG_HANG_DUMP_GAME,
	      "tick 181 did not DUMP_GAME\n");

	// Tier B unarmed (profile has no vblankCtr): a constant vbl placeholder must NEVER fire.
	memset(&h, 0, sizeof h); rs = 0; vf = 5;
	bad = 0;
	for (int i = 0; i < 400; i++)
		if (diag_hang_step(&h, DIAG_HANG_ARM_CORE, ++rs, ++vf, 0) != DIAG_HANG_NONE) bad++;
	CHECK(bad == 0, "unarmed Tier B fired %d times on the constant placeholder\n", bad);

	// Alternating advance/freeze below threshold: never fires (D2.7 "alternating ... -> never").
	// vbl advances every call so only vf alternates; each 100-static-frame run stays < 180.
	memset(&h, 0, sizeof h); rs = 0; vf = 100; vbl = 0;
	bad = 0;
	for (int blk = 0; blk < 10; blk++) {
		for (int i = 0; i < 100; i++)                       // 100 static frames (< 180)
			if (diag_hang_step(&h, ARM_BOTH, ++rs, vf, ++vbl) != DIAG_HANG_NONE) bad++;
		if (diag_hang_step(&h, ARM_BOTH, ++rs, ++vf, ++vbl) != DIAG_HANG_NONE) bad++;   // then progress
	}
	CHECK(bad == 0, "sub-threshold alternation fired %d times\n", bad);
	CHECK(h.frozenVf == 0 && h.dumps == 0, "alternation left state (fVf=%u dumps=%d)\n",
	      h.frozenVf, h.dumps);
	// The independent-tier corollary: BOTH tiers armed, vf alternating sub-threshold but vbl
	// TRULY frozen -> Tier B fires after ITS OWN 181-frame edge even though vf keeps resetting.
	memset(&h, 0, sizeof h); rs = 0; vf = 100;
	int firstGame = 0, calls = 0;
	CHECK(diag_hang_step(&h, ARM_BOTH, ++rs, vf, 7) == DIAG_HANG_NONE, "corollary baseline\n");
	for (int blk = 0; blk < 4 && !firstGame; blk++) {
		for (int i = 0; i < 100 && !firstGame; i++) {
			calls++;
			if (diag_hang_step(&h, ARM_BOTH, ++rs, vf, 7) == DIAG_HANG_DUMP_GAME) firstGame = calls;
		}
		if (!firstGame) {
			calls++;
			if (diag_hang_step(&h, ARM_BOTH, ++rs, ++vf, 7) == DIAG_HANG_DUMP_GAME) firstGame = calls;
		}
	}
	CHECK(firstGame == 181, "frozen-vbl-under-alternating-vf fired at call %d (want 181)\n", firstGame);

	// Tick-style cadence (the real main.c hook: renderSeq += ~12 per 200ms tick): threshold still
	// measured in render FRAMES -> 15 static ticks = 180 frames = not yet; the 16th (192) fires.
	memset(&h, 0, sizeof h); rs = 0;
	rs += 12;
	CHECK(diag_hang_step(&h, ARM_BOTH, rs, 100, 200) == DIAG_HANG_NONE, "tick-cadence baseline\n");
	bad = 0;
	for (int i = 1; i <= 15; i++) {
		rs += 12;
		if (diag_hang_step(&h, ARM_BOTH, rs, 100, 200 + (uint32_t)i) != DIAG_HANG_NONE) bad++;
	}
	CHECK(bad == 0 && h.frozenVf == 180, "tick cadence: fired early (bad=%d fVf=%u)\n", bad, h.frozenVf);
	rs += 12;
	CHECK(diag_hang_step(&h, ARM_BOTH, rs, 100, 999) == DIAG_HANG_DUMP_CORE,
	      "tick cadence: 16th static tick (192 frames) did not DUMP_CORE\n");

	// NULL-safety.
	CHECK(diag_hang_step(NULL, ARM_BOTH, 1, 2, 3) == DIAG_HANG_NONE, "NULL DiagHang fired\n");
}

// --------------------------------------------------------------------------------------------
// TEST 5 — D2 register-dump formatter golden bytes + truncation (SPEC D2.5 / D2.7 items 2-3).
// --------------------------------------------------------------------------------------------
// D2.7 item 3: the diag.h mirror of mGBA's register file can't silently drift. Layout (4-align):
// gprs 64 + cpsr/spsr 8 + banked 3*24 + ie/if_/ime 6 (+2 pad) + sp 4 + stack 128 + valid 1 (+3) = 288.
_Static_assert(sizeof(GbaCpuDump) == 288, "GbaCpuDump layout drifted from the documented mirror");

static void test_hang_format(void) {
	printf("TEST 5: hang-dump golden string + truncation\n");
	GbaCpuDump d; memset(&d, 0, sizeof d);
	for (int i = 0; i < 16; i++) d.gprs[i] = 0x10000000u + (uint32_t)i;
	d.cpsr = 0x0000001Fu; d.spsr = 0x600000D3u;
	for (int b = 0; b < 6; b++) {
		d.bankedR13[b]  = 0x000000A0u + (uint32_t)b;
		d.bankedR14[b]  = 0x000000B0u + (uint32_t)b;
		d.bankedSPSR[b] = 0x000000C0u + (uint32_t)b;
	}
	d.ie = 0x000D; d.if_ = 0x0001; d.ime = 0x0001;
	d.sp = 0x03007F00u; d.stackValid = 1;
	for (int i = 0; i < 32; i++) d.stack[i] = 0xDEAD0000u + (uint32_t)i;

	char buf[2048];
	int n = diag_hang_format(buf, sizeof buf, &d, 1234, 5678, DIAG_HANG_DUMP_CORE, 99999, 1005, 2100);
	const char* want =
	    "HANG kind=CORE ms=99999 vf=1234 vbl=5678 netC=1005 sioC=2100\n"
	    "R0  10000000 10000001 10000002 10000003\n"
	    "R4  10000004 10000005 10000006 10000007\n"
	    "R8  10000008 10000009 1000000A 1000000B\n"
	    "R12 1000000C SP 1000000D LR 1000000E PC 1000000F\n"
	    "CPSR 0000001F SPSR 600000D3\n"
	    "BANK r13: none=000000A0 fiq=000000A1 irq=000000A2 svc=000000A3 abt=000000A4 und=000000A5\n"
	    "BANK r14: none=000000B0 fiq=000000B1 irq=000000B2 svc=000000B3 abt=000000B4 und=000000B5\n"
	    "BANK spsr: none=000000C0 fiq=000000C1 irq=000000C2 svc=000000C3 abt=000000C4 und=000000C5\n"
	    "IE 000D IF 0001 IME 0001\n"
	    "STACK sp=03007F00 valid=1\n"
	    "  DEAD0000 DEAD0001 DEAD0002 DEAD0003 DEAD0004 DEAD0005 DEAD0006 DEAD0007\n"
	    "  DEAD0008 DEAD0009 DEAD000A DEAD000B DEAD000C DEAD000D DEAD000E DEAD000F\n"
	    "  DEAD0010 DEAD0011 DEAD0012 DEAD0013 DEAD0014 DEAD0015 DEAD0016 DEAD0017\n"
	    "  DEAD0018 DEAD0019 DEAD001A DEAD001B DEAD001C DEAD001D DEAD001E DEAD001F\n";
	CHECK(n == (int)strlen(want), "format length %d want %d\n", n, (int)strlen(want));
	CHECK(strcmp(buf, want) == 0, "golden mismatch:\n--- got ---\n%s--- want ---\n%s", buf, want);

	// GAME kind + stackValid=0: header says GAME, the STACK line says valid=0, NO word lines.
	d.stackValid = 0; d.sp = 0x0000FFF0u;   // SP outside 0x02/0x03 (e.g. BIOS region)
	int n2 = diag_hang_format(buf, sizeof buf, &d, 1, 2, DIAG_HANG_DUMP_GAME, 3, 0, 0);
	CHECK(strncmp(buf, "HANG kind=GAME ms=3 vf=1 vbl=2 netC=0 sioC=0\n", 44) == 0,
	      "GAME header: %.60s\n", buf);
	const char* stackLn = strstr(buf, "STACK sp=0000FFF0 valid=0\n");
	CHECK(stackLn != NULL, "valid=0 STACK line missing\n");
	CHECK(stackLn && stackLn[strlen("STACK sp=0000FFF0 valid=0\n")] == '\0',
	      "valid=0 dump did not end at the STACK line\n");
	CHECK(n2 == (int)strlen(buf), "valid=0 length %d vs %d\n", n2, (int)strlen(buf));

	// Truncation: a small cap never overflows, stays NUL-terminated, return = would-be length.
	d.stackValid = 1; d.sp = 0x03007F00u;
	char tiny[100];
	memset(tiny, 0x7F, sizeof tiny);
	int n3 = diag_hang_format(tiny, sizeof tiny, &d, 1234, 5678, DIAG_HANG_DUMP_CORE, 99999, 1005, 2100);
	CHECK(n3 == n, "truncated return %d (want would-be full length %d)\n", n3, n);
	CHECK(strlen(tiny) == sizeof tiny - 1, "tiny wrote %zu chars (want cap-1)\n", strlen(tiny));
	CHECK(strncmp(tiny, want, sizeof tiny - 1) == 0, "tiny prefix mismatch: %.40s\n", tiny);

	// Degenerate args refused.
	CHECK(diag_hang_format(NULL, 32, &d, 0, 0, 1, 0, 0, 0) < 0, "NULL buf accepted\n");
	CHECK(diag_hang_format(buf, 0, &d, 0, 0, 1, 0, 0, 0) < 0, "cap 0 accepted\n");
	CHECK(diag_hang_format(buf, sizeof buf, NULL, 0, 0, 1, 0, 0, 0) < 0, "NULL dump accepted\n");
}

int main(void) {
	printf("=== test_diag: D1 breadcrumbs + watchdog, D2 hang catcher (SPEC-firmware-diag D1.9/D2.7) ===\n");
	test_crumbs();
	test_watchdog();
	test_format();
	test_hang_step();
	test_hang_format();
	printf("=== %d checks, %d failures ===\n", g_checks, g_fail);
	return g_fail ? 1 : 0;
}
