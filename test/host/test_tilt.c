// test_tilt.c — PC host unit test for the phase-14 HD-2D tilt module (source/tilt.{c,h}).
//
//   clang -std=c11 -Wall -Wextra -O2 -I source test/host/test_tilt.c -o /tmp/tt && /tmp/tt
//
// (No mock <3ds.h> needed — tilt.c is header-free pure C by design: CLAUDE.md rule #4,
//  PHASE.md invariant 6, SPEC-render R6.1.)
//
// TEST NUMBERING is reserved across the two specs (SPEC-integration §0.2: "TEST 1-6 = this spec,
// TEST 7+ = SPEC-render"). Slice T1 ships the render half plus the tween the renderer consumes,
// so the numbers land as:
//
//   TEST 1  gate truth table .............. SPEC-integration I7.1  <- slice T3 (the gate)
//   TEST 2  per-screen independence ....... SPEC-integration I7.2  <- slice T3
//   TEST 3  touch invariant ............... SPEC-integration I7.3  <- slice T3
//   TEST 4  tween shape ................... SPEC-integration I7.4  <- here
//   TEST 5  tilt_active contract .......... SPEC-integration I7.5  <- here
//   TEST 6  settings + tier / defaults .... SPEC-integration I7.6 + §5  <- slice T4
//   TEST 7  identity at zero (bitwise) .... SPEC-render R6.9 T1    <- here
//   TEST 8  the R1.8 corner table ......... SPEC-render R6.9 T2    <- here
//   TEST 9  vertical fit is exact ......... SPEC-render R6.9 T3    <- here
//   TEST 10 gen1recomp reference table .... SPEC-render R6.9 T4    <- here
//   TEST 11 growth factors ................ SPEC-render R6.9 T5    <- here
//   TEST 12 projection round trip ......... SPEC-render R6.9 T6    <- here
//   TEST 13 coverage diagnostics .......... SPEC-render R6.9 T7    <- here
//   TEST 14 monotonicity / sanity ......... SPEC-render R6.9 T8    <- here
//   TEST 15 disparity foreshortening ...... SPEC-render R6.9 T10   <- here
//   TEST 16 the angle ladder .............. SPEC-render R2.5.5     <- here (extra)
//   TEST 17 the emitted screen-space mesh . SPEC-render R1.7/R6.7  <- slice T2 (the render pass)
//   TEST 18 second-eye slab contract ...... fix pass, review finding 2 (2026-08-04)
//   TEST 19 pause-menu -> touch-on .. ..... fix pass, review finding 5 (2026-08-04)
//
// The goldens in TEST 8/10/11/13 are the spec's own published tables, which were computed in
// double precision; this module is float32, so the tolerances below are the spec's (1e-4 px on
// positions, 1e-5 on the 5-decimal growth factors) unless a tighter/looser one is noted inline.

#include <stdio.h>
#include <math.h>
#include <stdint.h>
#include <stddef.h>     // offsetof — TEST 6 mirrors main.c's settings length ladder
#include <string.h>     // memcmp/memset — TEST 6 proves the gate never mutates its input

#include "../../source/tilt.c"

static int g_checks = 0, g_fail = 0;
#define CHECK(cond, ...) do { \
	g_checks++; \
	if (!(cond)) { g_fail++; printf("  [FAIL] "); printf(__VA_ARGS__); printf("   (at %s:%d)\n", __FILE__, __LINE__); } \
} while (0)
#define NEAR(a, b, tol, ...) do { \
	g_checks++; \
	if (!(fabs((double)(a) - (double)(b)) <= (double)(tol))) { g_fail++; printf("  [FAIL] "); \
		printf(__VA_ARGS__); printf("  got %.6f want %.6f (d=%.3g > %.3g)   (at %s:%d)\n", \
		(double)(a), (double)(b), fabs((double)(a) - (double)(b)), (double)(tol), __FILE__, __LINE__); } \
} while (0)

#define VW  240.0f      // GBA_W (main.c)
#define VH  160.0f      // GBA_H
#define D2R TILT_DEG2RAD

// main.c:694 — the hardware-validated stereo comfort ceiling, replicated here because main.c is
// not host-compilable. TEST 15 proves tilt cannot quietly raise it.
#define POP_DISP_MAX 6.5f
static float clamp_disp(float v) { return v > POP_DISP_MAX ? POP_DISP_MAX : v; }

// ============================================================================================
// SLICE T3 — the GATE (SPEC-integration §1, §3, §5): TEST 1, 2, 3.
// ============================================================================================

// The nine GameCtx values (gamestate.h:10-20) as plain ints. TILT_CTX_FIELD == GCTX_OVERWORLD
// == 1 is pinned in main.c by a _Static_assert; here the whole enum is swept so a gate that
// accidentally allowed, say, GCTX_FIELDMENU would fail loudly.
static const int CTX_ALL[9] = { 0 /*NONE*/, 1 /*OVERWORLD*/, 2 /*BATTLE_ACTION*/, 3 /*BATTLE_MOVE*/,
                                4 /*BATTLE_TARGET*/, 5 /*PARTY*/, 6 /*FIELDMENU*/, 7 /*BAG*/,
                                8 /*BATTLE_OTHER*/ };

// An ALL-CLEAR gate input: every rule G1-G11 satisfied, so tilt_target_level must return
// userLevel. Tests then break exactly one field at a time.
static TiltGateIn gate_clear(int screen, int userLevel) {
	TiltGateIn in;
	in.userLevel     = userLevel;
	in.screen        = screen;
	in.ok            = 1;
	in.ctx           = TILT_CTX_FIELD;
	in.sb1Valid      = 1;
	in.px            = 12;
	in.textDlg       = 0;
	in.menuOpen      = 0;
	in.wlOn          = 0;
	in.netOn         = 0;
	in.touchActive   = 0;
	in.stereoEngaged = 0;
	in.isN3DS        = 1;
	in.fsOn          = 0;
	in.focScreen     = screen;
	return in;
}

// INDEPENDENT reference formulation of the same policy: one positive conjunction of "allowed"
// conditions, versus tilt_target_level's ladder of eleven negative early-returns. Written from
// SPEC-integration §1.4's table, not from tilt.c, so a transcription slip in either shows up as
// a mismatch in the exhaustive sweep below.
static int gate_ref(const TiltGateIn* in) {
	int allow = in->userLevel > 0
	         && in->isN3DS
	         && !in->menuOpen
	         && !in->wlOn && !in->netOn
	         && in->ok
	         && in->ctx == TILT_CTX_FIELD
	         && in->sb1Valid && in->px >= 0
	         && !in->textDlg
	         && !(in->screen == 1 && in->touchActive)
	         && !(in->screen == 0 && in->stereoEngaged)
	         && !(in->fsOn && in->screen != in->focScreen);
	if (!allow) return 0;
	return in->userLevel < TILT_LEVELS ? in->userLevel : TILT_LEVELS - 1;
}

// --------------------------------------------------------------------------------------------
// TEST 1 — the gate truth table (SPEC-integration I7.1). Enumerates tilt_target_level over the
// full cross product of the boolean inputs x all 9 GameCtx values x screen x userLevel, then
// pins each individual rule, the precedence property, and the two "never garbage" cases.
// --------------------------------------------------------------------------------------------
static void test_gate(void) {
	printf("TEST 1: gate truth table G1-G11 (SPEC-integration §1.4 / I7.1)\n");

	// -- (a) the all-clear row: exactly userLevel, on both screens, for every level --------
	for (int sc = 0; sc < 2; sc++)
		for (int lv = 0; lv < TILT_LEVELS; lv++) {
			TiltGateIn in = gate_clear(sc, lv);
			CHECK(tilt_target_level(&in) == lv,
			      "all-clear screen %d level %d -> %d (want %d)\n", sc, lv, tilt_target_level(&in), lv);
		}
	{   // out-of-range preference is clamped, never used as an index by the caller
		TiltGateIn in = gate_clear(0, 99);
		CHECK(tilt_target_level(&in) == TILT_LEVELS - 1, "userLevel 99 clamps to the top rung\n");
	}

	// -- (b) every rule in isolation, all other inputs clear (I7.1 bullet 2) ---------------
	// Each row: break ONE thing on an otherwise-perfect level-3 input and demand 0.
	{
		TiltGateIn in;
		in = gate_clear(0, 0);                      CHECK(tilt_target_level(&in) == 0, "G1: userLevel 0\n");
		in = gate_clear(0, -1);                     CHECK(tilt_target_level(&in) == 0, "G1: negative userLevel\n");
		in = gate_clear(0, 3); in.isN3DS = 0;       CHECK(tilt_target_level(&in) == 0, "G2: Old 3DS clamps live level to 0\n");
		in = gate_clear(1, 3); in.isN3DS = 0;       CHECK(tilt_target_level(&in) == 0, "G2: ...on the bottom screen too\n");
		in = gate_clear(0, 3); in.menuOpen = 1;     CHECK(tilt_target_level(&in) == 0, "G3: pause menu open\n");
		in = gate_clear(0, 3); in.wlOn = 1;         CHECK(tilt_target_level(&in) == 0, "G4: wireless link live\n");
		in = gate_clear(0, 3); in.netOn = 1;        CHECK(tilt_target_level(&in) == 0, "G4: loopback net link live\n");
		in = gate_clear(0, 3); in.ok = 0;           CHECK(tilt_target_level(&in) == 0, "G5: no profile for this game\n");
		in = gate_clear(0, 3); in.sb1Valid = 0;     CHECK(tilt_target_level(&in) == 0, "G7: save not loaded\n");
		in = gate_clear(0, 3); in.px = -1;          CHECK(tilt_target_level(&in) == 0, "G7: SaveBlock1 pointer not ready\n");
		in = gate_clear(0, 3); in.textDlg = 1;      CHECK(tilt_target_level(&in) == 0, "G8: field textbox up (script talking)\n");
		in = gate_clear(1, 3); in.touchActive = 1;  CHECK(tilt_target_level(&in) == 0, "G9: touch active on the bottom screen\n");
		in = gate_clear(0, 3); in.stereoEngaged = 1;CHECK(tilt_target_level(&in) == 0, "G10: stereo wins on the top screen\n");
		in = gate_clear(0, 3); in.fsOn = 1; in.focScreen = 1;
		CHECK(tilt_target_level(&in) == 0, "G11: frameskip + unfocused screen\n");
		in = gate_clear(1, 3); in.fsOn = 1; in.focScreen = 0;
		CHECK(tilt_target_level(&in) == 0, "G11: ...symmetric on the other screen\n");
		in = gate_clear(0, 3); in.fsOn = 1; in.focScreen = 0;
		CHECK(tilt_target_level(&in) == 3, "G11 does NOT fire on the FOCUSED screen under frameskip\n");
	}

	// -- (c) G6: only GCTX_OVERWORLD passes; every other context gates off -----------------
	for (unsigned i = 0; i < sizeof CTX_ALL / sizeof CTX_ALL[0]; i++) {
		for (int sc = 0; sc < 2; sc++) {
			TiltGateIn in = gate_clear(sc, 2); in.ctx = CTX_ALL[i];
			int want = (CTX_ALL[i] == TILT_CTX_FIELD) ? 2 : 0;
			CHECK(tilt_target_level(&in) == want,
			      "G6: ctx %d on screen %d -> %d (want %d)\n", CTX_ALL[i], sc, tilt_target_level(&in), want);
		}
	}

	// -- (d) I1.6, the unmapped-game case: ok == 0 gates off for EVERY ctx, including field --
	// (belt and braces: game_read already returns ctx = GCTX_NONE there, but a future refactor
	//  of those sentinels must not be able to open the gate on a frame we know nothing about.)
	for (unsigned i = 0; i < sizeof CTX_ALL / sizeof CTX_ALL[0]; i++) {
		TiltGateIn in = gate_clear(0, 3); in.ok = 0; in.ctx = CTX_ALL[i];
		CHECK(tilt_target_level(&in) == 0, "I1.6: ok=0 with ctx %d must gate off\n", CTX_ALL[i]);
	}
	// -- and I1.5: the field-validity pair is required even in the field --------------------
	{
		TiltGateIn in = gate_clear(0, 3); in.ctx = TILT_CTX_FIELD; in.sb1Valid = 0;
		CHECK(tilt_target_level(&in) == 0, "I1.5: sb1Valid=0 in the field still gates off\n");
		in = gate_clear(0, 3); in.ctx = TILT_CTX_FIELD; in.px = -1;
		CHECK(tilt_target_level(&in) == 0, "I1.5: px<0 in the field still gates off\n");
		in = gate_clear(0, 3); in.px = 0;
		CHECK(tilt_target_level(&in) == 3, "px == 0 is a legal tile (only -1 is the sentinel)\n");
	}

	// -- (e) PRECEDENCE: two rules firing is still 0 (a 0-returning ladder is order-free) ---
	{
		int bad = 0, pairs = 0;
		// break rule i and rule j together, over a compact list of single-field breakers
		for (int i = 0; i < 11; i++) for (int j = i + 1; j < 11; j++) {
			for (int sc = 0; sc < 2; sc++) {
				TiltGateIn in = gate_clear(sc, 3);
				int idx[2] = { i, j };
				for (int n = 0; n < 2; n++) switch (idx[n]) {
					case 0: in.userLevel = 0; break;
					case 1: in.isN3DS = 0; break;
					case 2: in.menuOpen = 1; break;
					case 3: in.wlOn = 1; break;
					case 4: in.netOn = 1; break;
					case 5: in.ok = 0; break;
					case 6: in.ctx = 2; break;
					case 7: in.sb1Valid = 0; break;
					case 8: in.textDlg = 1; break;
					case 9: in.touchActive = 1; in.stereoEngaged = 1; break;   // the screen-specific pair
					case 10: in.fsOn = 1; in.focScreen = sc ^ 1; break;
					default: break;
				}
				pairs++;
				if (tilt_target_level(&in) != 0) bad++;
			}
		}
		CHECK(pairs == 110, "the precedence sweep ran the expected number of pairs (%d)\n", pairs);
		CHECK(bad == 0, "%d of %d two-rule combinations returned non-zero — a rule unblocked another\n",
		      bad, pairs);
	}

	// -- (f) EXHAUSTIVE cross product vs the independent reference (I7.1) -------------------
	// 11 boolean-ish inputs x 9 ctx x 2 screens x 4 user levels = 147456 rows; asserted as one
	// aggregate so the suite's check budget stays honest while the coverage stays total.
	{
		long rows = 0, mismatch = 0, nonzero = 0;
		for (int lv = 0; lv < TILT_LEVELS; lv++)
		for (int sc = 0; sc < 2; sc++)
		for (unsigned ci = 0; ci < sizeof CTX_ALL / sizeof CTX_ALL[0]; ci++)
		for (int bits = 0; bits < (1 << 11); bits++) {
			TiltGateIn in = gate_clear(sc, lv);
			in.ctx           = CTX_ALL[ci];
			in.ok            =  (bits >> 0) & 1;
			in.sb1Valid      =  (bits >> 1) & 1;
			in.px            = ((bits >> 2) & 1) ? 7 : -1;
			in.textDlg       =  (bits >> 3) & 1;
			in.menuOpen      =  (bits >> 4) & 1;
			in.wlOn          =  (bits >> 5) & 1;
			in.netOn         =  (bits >> 6) & 1;
			in.touchActive   =  (bits >> 7) & 1;
			in.stereoEngaged =  (bits >> 8) & 1;
			in.isN3DS        =  (bits >> 9) & 1;
			in.fsOn          =  (bits >> 10) & 1;
			in.focScreen     = ((bits >> 10) & 1) ? (sc ^ 1) : sc;   // fsOn rows test the UNFOCUSED case
			int got = tilt_target_level(&in), want = gate_ref(&in);
			rows++;
			if (got != want) mismatch++;
			if (got != 0)    nonzero++;
			if (got != 0 && got != lv) mismatch++;   // an open gate returns the SAVED level, nothing else
		}
		CHECK(rows == 147456, "the exhaustive sweep covered every row (%ld)\n", rows);
		CHECK(mismatch == 0, "%ld of %ld gate rows disagree with the independent reference\n", mismatch, rows);
		CHECK(nonzero > 0, "the sweep is not vacuous: %ld rows opened the gate\n", nonzero);
	}

	// -- (g) the composition main.c actually performs: gate -> tween -> tilt_active ----------
	// Proves the three pieces work together the way the render block wires them (I2.5): an open
	// gate rises over ~250 ms, a closing gate DESCENDS (never snaps) and lands on exactly 0.0f,
	// which is the only state in which tilt_active() reports the flat path.
	{
		TiltTween tw; tilt_tween_reset(&tw);
		TiltGateIn in = gate_clear(0, 2);
		CHECK(tilt_active(&tw) == 0, "a fresh session starts on the flat path\n");
		float prev = tw.ang;
		for (int f = 0; f < 20; f++) {                       // ~333 ms of 60 fps frames, gate OPEN
			int lv = tilt_target_level(&in);
			tilt_tween_step(&tw, lv, tilt_angle_deg_for_level(lv), 16.6666f);
			CHECK(tw.ang >= prev, "frame %d: the rise went backwards\n", f);
			prev = tw.ang;
		}
		CHECK(tw.ang == TILT_ANGLE_DEG[2], "the open gate settles on the ladder angle (%.4f)\n", (double)tw.ang);
		in.ctx = 2;                                          // a battle starts -> G6 closes the gate
		int lv1 = tilt_target_level(&in);
		CHECK(lv1 == 0, "the battle closed the gate\n");
		tilt_tween_step(&tw, lv1, tilt_angle_deg_for_level(lv1), 16.6666f);
		CHECK(tw.ang < TILT_ANGLE_DEG[2] && tw.ang > 0.0f, "a closed gate DESCENDS, it does not snap\n");
		CHECK(tilt_active(&tw) == 1, "still active mid-descent (the tilt is still on screen)\n");
		for (int f = 0; f < 20; f++) {
			int lv = tilt_target_level(&in);
			tilt_tween_step(&tw, lv, tilt_angle_deg_for_level(lv), 16.6666f);
		}
		CHECK(tw.ang == 0.0f, "the descent lands on EXACTLY 0.0f (%.9f)\n", (double)tw.ang);
		CHECK(tilt_active(&tw) == 0, "...and only then is the flat path byte-identical again\n");
	}
}

// --------------------------------------------------------------------------------------------
// TEST 2 — per-screen independence (SPEC-integration I7.2 / I1.8). The gate is per SCREEN, by
// the game on that screen: one screen's battle/menu/touch state must not touch the other's.
// --------------------------------------------------------------------------------------------
static void test_gate_screens(void) {
	printf("TEST 2: per-screen independence (SPEC-integration I1.8 / I7.2)\n");

	// -- the two screen-specific rules apply to their own screen only ----------------------
	{
		TiltGateIn top = gate_clear(0, 3); top.touchActive = 1;
		CHECK(tilt_target_level(&top) == 3, "G9 (touch) must NOT affect the top screen\n");
		TiltGateIn bot = gate_clear(1, 3); bot.stereoEngaged = 1;
		CHECK(tilt_target_level(&bot) == 3, "G10 (stereo) must NOT affect the bottom screen\n");
	}

	// -- a battle on ONE screen leaves the other alone (two calls, two snapshots) -----------
	{
		TiltGateIn top = gate_clear(0, 2), bot = gate_clear(1, 2);
		top.ctx = 2;                                  // top game entered a battle
		CHECK(tilt_target_level(&top) == 0, "the battling screen gates off\n");
		CHECK(tilt_target_level(&bot) == 2, "the other screen keeps tilting (I1.8 reason 3)\n");
		top = gate_clear(0, 2); bot = gate_clear(1, 2);
		bot.textDlg = 1;                              // bottom game is in a dialog
		CHECK(tilt_target_level(&bot) == 0, "the talking screen gates off\n");
		CHECK(tilt_target_level(&top) == 2, "...and the top screen is unaffected\n");
		top = gate_clear(0, 2); bot = gate_clear(1, 2);
		bot.ok = 0;                                   // single-game session: no bottom game at all
		CHECK(tilt_target_level(&bot) == 0, "no game on the bottom screen -> off (answers Q4)\n");
		CHECK(tilt_target_level(&top) == 2, "...and the single game on top still tilts\n");
	}

	// -- the SHARED rules hit both screens together ----------------------------------------
	{
		static const int SHARED = 5;   // menuOpen, wlOn, netOn, isN3DS, userLevel
		for (int r = 0; r < SHARED; r++) {
			TiltGateIn a = gate_clear(0, 3), b = gate_clear(1, 3);
			switch (r) {
				case 0: a.menuOpen = b.menuOpen = 1; break;
				case 1: a.wlOn = b.wlOn = 1; break;
				case 2: a.netOn = b.netOn = 1; break;
				case 3: a.isN3DS = b.isN3DS = 0; break;
				default: a.userLevel = b.userLevel = 0; break;
			}
			CHECK(tilt_target_level(&a) == 0 && tilt_target_level(&b) == 0,
			      "shared rule %d must close BOTH screens\n", r);
		}
	}

	// -- G11 under frameskip: exactly the focused screen survives ---------------------------
	for (int foc = 0; foc < 2; foc++)
		for (int sc = 0; sc < 2; sc++) {
			TiltGateIn in = gate_clear(sc, 1); in.fsOn = 1; in.focScreen = foc;
			int want = (sc == foc) ? 1 : 0;
			CHECK(tilt_target_level(&in) == want,
			      "frameskip: screen %d with focus on %d -> %d (want %d)\n",
			      sc, foc, tilt_target_level(&in), want);
		}
}

// --------------------------------------------------------------------------------------------
// TEST 3 — THE TOUCH INVARIANT (PHASE.md invariant 4, SPEC-integration I7.3 / I3.4).
// touch_to_gba is consulted only when tmEff == TOUCH_SMART; SPEC-integration §3.2 chose (b)
// SUPPRESSION, so the guarantee we need is the implication
//        touchActive != 0  =>  tilt_target_level(screen = 1, ...) == 0
// over the ENTIRE remaining cross product — i.e. the bottom screen cannot be tilted while any
// touch mode is live, no matter what else is true. Proven here as a machine-checked statement
// rather than asserted in a comment. There are no touch-inverse goldens to check because the
// inverse is deliberately NOT on the shipped path (I3.3); tilt_unproject is host-tested anyway
// by TEST 12 so enabling it later is a one-line change, not a re-derivation.
// --------------------------------------------------------------------------------------------
static void test_touch_invariant(void) {
	printf("TEST 3: touch invariant — touchActive => bottom screen flat (PHASE invariant 4)\n");

	long rows = 0, violations = 0, topUnaffected = 0, topRows = 0;
	for (int lv = 1; lv < TILT_LEVELS; lv++)
	for (unsigned ci = 0; ci < sizeof CTX_ALL / sizeof CTX_ALL[0]; ci++)
	for (int bits = 0; bits < (1 << 9); bits++) {
		TiltGateIn in = gate_clear(1, lv);            // BOTTOM screen, touch live
		in.touchActive   = 1;
		in.ctx           = CTX_ALL[ci];
		in.ok            =  (bits >> 0) & 1;
		in.sb1Valid      =  (bits >> 1) & 1;
		in.px            = ((bits >> 2) & 1) ? 3 : -1;
		in.textDlg       =  (bits >> 3) & 1;
		in.menuOpen      =  (bits >> 4) & 1;
		in.wlOn          =  (bits >> 5) & 1;
		in.netOn         =  (bits >> 6) & 1;
		in.stereoEngaged =  (bits >> 7) & 1;
		in.isN3DS        =  (bits >> 8) & 1;
		rows++;
		if (tilt_target_level(&in) != 0) violations++;

		// ...and the mirror property: the same input on the TOP screen must be INSENSITIVE to
		// touchActive (the top screen is not the touch controller — I3.1/I7.3).
		TiltGateIn t0 = in;
		t0.screen = 0; t0.focScreen = 0; t0.touchActive = 0;
		TiltGateIn t1 = t0; t1.touchActive = 1;
		topRows++;
		if (tilt_target_level(&t0) == tilt_target_level(&t1)) topUnaffected++;
	}
	CHECK(rows == 13824, "the touch sweep covered every row (%ld)\n", rows);
	CHECK(violations == 0,
	      "PHASE invariant 4 VIOLATED: %ld of %ld bottom-screen rows tilted while touch was live\n",
	      violations, rows);
	CHECK(topUnaffected == topRows,
	      "touchActive changed the TOP screen's answer in %ld of %ld rows\n",
	      topRows - topUnaffected, topRows);

	// The three touch modes, spelled out: OFF is the only one that lets the bottom screen tilt.
	// (main.c passes tmEff != TOUCH_OFF, so TOUCH_PAD is suppressed too — I3.6: its overlay is
	//  drawn in flat screen space and a tilted game under a flat D-pad reads as a bug.)
	{
		static const char* const MODE[3] = { "TOUCH_OFF", "TOUCH_PAD", "TOUCH_SMART" };
		for (int m = 0; m < 3; m++) {
			TiltGateIn in = gate_clear(1, 2); in.touchActive = (m != 0);
			int want = (m == 0) ? 2 : 0;
			CHECK(tilt_target_level(&in) == want, "%s -> bottom level %d (want %d)\n",
			      MODE[m], tilt_target_level(&in), want);
		}
	}
}

// --------------------------------------------------------------------------------------------
// TEST 4 — tween shape (SPEC-integration I7.4): monotone, exact goldens, termination, the dt
// clamp, retarget continuity, and the exact-zero landing PHASE.md invariant 1 depends on.
// --------------------------------------------------------------------------------------------
static void test_tween(void) {
	printf("TEST 4: tween shape (wall-clock smoothstep, SPEC-integration I2.4/I7.4)\n");

	// -- monotonicity: 0 -> 15 deg in 5 ms slices --------------------------------------------
	TiltTween t; tilt_tween_reset(&t);
	CHECK(t.ang == 0.0f && t.angTo == 0.0f && t.level == 0, "reset leaves a flat, arrived tween\n");
	CHECK(t.tMs == TILT_TWEEN_MS, "reset parks tMs at the end of the segment (no phantom ease-in)\n");
	float prev = t.ang;
	for (int i = 0; i < 80; i++) {
		tilt_tween_step(&t, 2, 15.0f, 5.0f);
		CHECK(t.ang >= prev, "step %d: ang went backwards (%.6f -> %.6f)\n", i, (double)prev, (double)t.ang);
		CHECK(t.ang >= 0.0f && t.ang <= 15.0f, "step %d: ang %.6f left [0,15]\n", i, (double)t.ang);
		prev = t.ang;
	}
	CHECK(t.ang == 15.0f, "a 400 ms walk lands exactly on the target (%.9f)\n", (double)t.ang);
	CHECK(t.tMs == TILT_TWEEN_MS, "tMs saturates at TILT_TWEEN_MS\n");

	// -- golden values: both are exact binary fractions, so assert with == ---------------------
	// NOTE (resolved spec conflict): SPEC-integration I7.4 words the half-way golden as "one step
	// of 125 ms", which I2.3's 100 ms dt clamp makes unreachable in a single step. The CLAMP is
	// the safety property (it is what stops a HOME-menu resume from teleporting the angle), so it
	// wins and the golden is reached in two legal steps. Same numbers, reachable schedule.
	// u = 62.5/250 = 0.25 -> s = 0.0625*2.5 = 0.15625 -> 15 * 0.15625 = 2.34375
	tilt_tween_reset(&t);
	tilt_tween_step(&t, 2, 15.0f, 62.5f);
	CHECK(t.ang == 2.34375f, "quarter-way smoothstep golden: got %.9f want 2.34375\n", (double)t.ang);
	// + another 62.5 ms -> u = 125/250 = 0.5 -> s = u*u*(3-2u) = 0.5 -> 15 * 0.5 = 7.5
	tilt_tween_step(&t, 2, 15.0f, 62.5f);
	CHECK(t.ang == 7.5f, "half-way smoothstep golden: got %.9f want 7.5\n", (double)t.ang);
	CHECK(t.tMs == 125.0f, "the segment clock accumulated both slices (%.4f)\n", (double)t.tMs);

	// -- termination is slicing-independent (slices <= the dt clamp) ---------------------------
	static const float SLICES[] = { 100.0f, 50.0f, 16.6666f, 5.0f, 1.0f };
	for (unsigned si = 0; si < sizeof SLICES / sizeof SLICES[0]; si++) {
		tilt_tween_reset(&t);
		float acc = 0.0f;
		while (acc < 250.0f) { tilt_tween_step(&t, 3, 20.0f, SLICES[si]); acc += SLICES[si]; }
		CHECK(t.tMs == TILT_TWEEN_MS, "slice %.4f ms: tMs %.4f, want %.1f\n",
		      (double)SLICES[si], (double)t.tMs, (double)TILT_TWEEN_MS);
		CHECK(t.ang == 20.0f, "slice %.4f ms: ang %.9f, want exactly 20\n", (double)SLICES[si], (double)t.ang);
	}
	// an over-clamp slice still terminates, in exactly ceil(250/100) = 3 steps
	tilt_tween_reset(&t);
	tilt_tween_step(&t, 3, 20.0f, 5000.0f);  CHECK(t.ang != 20.0f, "over-clamp step 1 must not arrive\n");
	tilt_tween_step(&t, 3, 20.0f, 5000.0f);  CHECK(t.ang != 20.0f, "over-clamp step 2 must not arrive\n");
	tilt_tween_step(&t, 3, 20.0f, 5000.0f);
	CHECK(t.ang == 20.0f, "over-clamp step 3 arrives exactly (%.9f)\n", (double)t.ang);

	// -- exact zero on the way down (I2.7): the flat-path guarantee ---------------------------
	tilt_tween_reset(&t);
	tilt_tween_step(&t, 2, 15.0f, 1000.0f);   // clamped to 100 ms...
	tilt_tween_step(&t, 2, 15.0f, 1000.0f);   // ...so two more steps are needed to arrive
	tilt_tween_step(&t, 2, 15.0f, 1000.0f);
	CHECK(t.ang == 15.0f, "arrived at 15 before the descent (%.9f)\n", (double)t.ang);
	int sawActive = 0;
	for (int i = 0; i < 20; i++) {
		tilt_tween_step(&t, 0, 0.0f, 20.0f);
		if (t.ang > 0.0f) { sawActive += tilt_active(&t); }
	}
	CHECK(t.ang == 0.0f, "a completed down-tween lands EXACTLY on 0.0f (bit compare), got %.9g\n", (double)t.ang);
	CHECK(tilt_active(&t) == 0, "tilt_active is 0 once the down-tween completes (flat path)\n");
	CHECK(sawActive > 0, "tilt_active stayed true for the descent\n");

	// -- dt clamp (I2.3): one enormous delta advances at most TILT_DT_MAX_MS -------------------
	tilt_tween_reset(&t);
	tilt_tween_step(&t, 2, 15.0f, 100000.0f);
	CHECK(t.tMs == TILT_DT_MAX_MS, "a 100 s resume delta advanced %.3f ms, want %.1f\n",
	      (double)t.tMs, (double)TILT_DT_MAX_MS);
	CHECK(t.ang < 15.0f, "the clamped step did not teleport to the target (%.6f)\n", (double)t.ang);
	{   // u = 100/250 = 0.4 -> s = 0.16*(3-0.8) = 0.352 -> 15*0.352 = 5.28
		NEAR(t.ang, 5.28f, 1e-5, "clamped-step angle\n");
	}
	tilt_tween_reset(&t);
	tilt_tween_step(&t, 2, 15.0f, -5.0f);      // negative dt is swallowed, never rewinds
	CHECK(t.tMs == 0.0f && t.ang == 0.0f, "a negative dt is clamped to 0 (tMs %.3f ang %.3f)\n",
	      (double)t.tMs, (double)t.ang);
	tilt_tween_reset(&t);
	tilt_tween_step(&t, 2, 15.0f, (float)NAN);
	CHECK(t.tMs == 0.0f && t.ang == 0.0f, "a NaN dt is swallowed (tMs %.3f ang %.3f)\n",
	      (double)t.tMs, (double)t.ang);

	// -- retarget mid-tween is C0-continuous and still terminates ------------------------------
	tilt_tween_reset(&t);
	for (int i = 0; i < 5; i++) tilt_tween_step(&t, 3, 20.0f, 16.0f);   // 80 ms in, ~partway up
	float before = t.ang, step0 = t.ang;
	tilt_tween_step(&t, 1, 10.0f, 16.0f);                               // retarget down mid-flight
	CHECK(fabsf(t.ang - before) <= 1.0f, "retarget jumped (%.6f -> %.6f)\n", (double)before, (double)t.ang);
	CHECK(t.angFrom == step0, "retarget restarts from the CURRENT angle, not from angTo\n");
	CHECK(t.tMs == 16.0f, "retarget restarts the segment clock (tMs %.3f)\n", (double)t.tMs);
	for (int i = 0; i < 40; i++) tilt_tween_step(&t, 1, 10.0f, 16.0f);
	CHECK(t.ang == 10.0f, "the retargeted segment still terminates exactly (%.9f)\n", (double)t.ang);

	// -- the tween keeps running with the gate closed (I2.5: a menu tweens down, never snaps) --
	tilt_tween_reset(&t);
	for (int i = 0; i < 40; i++) tilt_tween_step(&t, 2, 15.0f, 16.0f);
	tilt_tween_step(&t, 0, 0.0f, 16.0f);
	CHECK(t.ang < 15.0f && t.ang > 0.0f, "gate close starts a descent, not a snap (%.6f)\n", (double)t.ang);
	CHECK(tilt_active(&t) == 1, "still active on the first gated-off frame (the tween is visible)\n");
}

// --------------------------------------------------------------------------------------------
// TEST 5 — tilt_active contract (SPEC-integration I7.5 / I2.6). gen1recomp Tilt.active()
// (Tilt.lua:110-114) verbatim: level > 0 OR angle > 0.
// --------------------------------------------------------------------------------------------
static void test_active(void) {
	printf("TEST 5: tilt_active contract (level > 0 || ang > 0)\n");
	TiltTween t; tilt_tween_reset(&t);

	t.level = 2; t.ang = 0.0f;
	CHECK(tilt_active(&t) == 1, "level>0 with ang==0 (the first frame after enabling) is ACTIVE\n");
	t.level = 0; t.ang = 0.5f;
	CHECK(tilt_active(&t) == 1, "level==0 with ang>eps (the down-tween) is ACTIVE\n");
	t.level = 0; t.ang = 0.0f;
	CHECK(tilt_active(&t) == 0, "level==0 with ang==0 is NOT active -> flat path byte-identical\n");
	t.level = 0; t.ang = TILT_EPS_DEG;
	CHECK(tilt_active(&t) == 0, "the epsilon is exclusive (ang == EPS is not active)\n");
	t.level = 0; t.ang = TILT_EPS_DEG * 2.0f;
	CHECK(tilt_active(&t) == 1, "just above the epsilon is active\n");
	t.level = 3; t.ang = 20.0f;
	CHECK(tilt_active(&t) == 1, "fully engaged is active\n");
}

// --------------------------------------------------------------------------------------------
// SLICE T4 — SETTINGS PERSISTENCE + THE PERFORMANCE TIER (SPEC-integration §4, §5): TEST 6.
//
// main.c is not host-compilable (libctru/citro2d), so I7.6's instruction is followed literally:
// the Settings layout and the settings_load length ladder are REPLICATED here, and main.c carries
// _Static_asserts pinning its own struct to the same numbers. If anyone inserts a field above
// `tilt` in main.c, main.c fails to COMPILE rather than this test silently testing a fiction.
// --------------------------------------------------------------------------------------------

// Mirror of main.c's `Settings` (the struct immediately below SETTINGS_MAGIC). s32 -> int32_t;
// the layout is 25 consecutive 4-byte words with no padding on any target we build for, which is
// what the _Static_asserts in main.c assert (sizeof == 25*4, offsetof(presence) == 24*4,
// offsetof(tilt) == 23*4, offsetof(padEdge) == 22*4).
//
// PHASE 15 (SPEC-avatar A6.3.2, updated in the same commit as main.c): `presence` is APPENDED, so
// offsetof(tilt) stays 23 — and that invariance IS the proof the change is backward-compatible.
// The length ladder grows one rung: what was "the full struct" for a phase-14 build is now the
// `lenTilt` rung, i.e. exactly what every settings file on an existing card is.
//
// PHASE 22.2 (SPEC-family-traversal T4.1, again in the same commit as main.c): `traverse` is
// appended by the identical rule, so offsetof(tilt) is STILL 23 and offsetof(presence) is STILL
// 24 — the two invariances below are what prove that every settings file written by any build
// since phase 14 still loads, and that no existing user's tilt or co-op pref moves.
typedef struct {
	uint32_t magic;
	int32_t scaleMode[2], smooth[2], swapped, hudMode, audioMode, volA, volB, touchMode, frameskip;
	int32_t dof, bloom, light, vivid;                                     // HD-2D toggles
	int32_t theme, customBaseHue, customAccentHue, customContrast;        // UI-redesign chrome...
	int32_t gameMode, padColor, padEdge;                                  // ...prefs
	int32_t tilt;                                                         // phase 14 (I4.9)
	int32_t presence;                                                     // phase 15 (A6.3)
	int32_t traverse;                                                     // phase 22.2 (T4.1)
	int32_t voxel, voxPitch, voxZoom;                                     // phase 32 (SPEC-port 8.2)
} RefSettings;
// The ladder size, mirrored from theme.h. theme.h cannot be included here (it opens with
// <citro2d.h> unless the THEME_HOST_SHIM is in play, and tilt.c is header-free by design), so the
// constant is restated — and then PINNED to the shipped header by the grep-able assertion below,
// which is the same contract the RefSettings mirror itself carries: if theme.h changes and this
// does not, the tests are testing a fiction, so the number lives here in exactly one place.
#define SMART_TRAVERSE_LEVELS 3   // theme.h: 0 Off / 1 HM / 2 HM+Via
_Static_assert(sizeof(RefSettings)             == 29 * sizeof(int32_t), "mirror drifted from main.c Settings");
_Static_assert(offsetof(RefSettings, voxZoom)  == 28 * sizeof(int32_t), "mirror drifted from main.c Settings");
_Static_assert(offsetof(RefSettings, voxPitch) == 27 * sizeof(int32_t), "mirror drifted from main.c Settings");
_Static_assert(offsetof(RefSettings, voxel)    == 26 * sizeof(int32_t), "mirror drifted from main.c Settings");
_Static_assert(offsetof(RefSettings, traverse) == 25 * sizeof(int32_t), "mirror drifted from main.c Settings");
_Static_assert(offsetof(RefSettings, presence) == 24 * sizeof(int32_t), "mirror drifted from main.c Settings");
_Static_assert(offsetof(RefSettings, tilt)     == 23 * sizeof(int32_t), "mirror drifted from main.c Settings");
_Static_assert(offsetof(RefSettings, padEdge)  == 22 * sizeof(int32_t), "mirror drifted from main.c Settings");

// A pure-C mirror of settings_load's acceptance + field-gating decision (main.c). Returns 1 if the
// file is accepted. `outTilt` / `outPresence` / `outTrav` are only written when the ladder says
// that field is present, so the caller can prove an older file leaves each pref at its DEFAULT.
static int ref_settings_load4(size_t n, uint32_t magic, int32_t tiltWord, int32_t presenceWord,
                              int32_t travWord, int* outAcceptedRedesign, int* outTilt,
                              int* outPresence, int* outTrav);
static int ref_settings_load(size_t n, uint32_t magic, int32_t tiltWord, int32_t presenceWord,
                             int* outAcceptedRedesign, int* outTilt, int* outPresence) {
	return ref_settings_load4(n, magic, tiltWord, presenceWord, 0, outAcceptedRedesign, outTilt,
	                          outPresence, 0);
}
static int ref_settings_load4(size_t n, uint32_t magic, int32_t tiltWord, int32_t presenceWord,
                              int32_t travWord, int* outAcceptedRedesign, int* outTilt,
                              int* outPresence, int* outTrav) {
	const size_t lenDof   = offsetof(RefSettings, dof);
	const size_t lenBloom = offsetof(RefSettings, bloom);
	const size_t lenLight = offsetof(RefSettings, light);
	const size_t lenVivid = offsetof(RefSettings, vivid);
	const size_t lenOld   = offsetof(RefSettings, theme);
	const size_t lenPad   = offsetof(RefSettings, tilt);       // pre-tilt full struct
	const size_t lenTilt  = offsetof(RefSettings, presence);   // pre-presence full struct (phase 14)
	const size_t lenPres  = offsetof(RefSettings, traverse);   // pre-traverse full struct (phase 15..22.1)
	const size_t lenTrav  = offsetof(RefSettings, voxel);      // pre-voxel full struct (phase 22.2..31)
	const size_t lenNew   = sizeof(RefSettings);
	if ((n != lenNew && n != lenTrav && n != lenPres && n != lenTilt && n != lenPad && n != lenOld && n != lenVivid
	     && n != lenLight && n != lenBloom && n != lenDof) || magic != 0x33424744u) return 0;
	if (outAcceptedRedesign) *outAcceptedRedesign = (n >= lenPad);
	if (n >= lenTilt && outTilt)     *outTilt     = (int)(((unsigned)tiltWord) % TILT_LEVELS);
	if (n >= lenPres && outPresence) *outPresence = (presenceWord != 0) ? 1 : 0;
	if (n >= lenTrav && outTrav)     *outTrav     = (int)(((unsigned)travWord) % SMART_TRAVERSE_LEVELS);
	return 1;
}
// Phase 32: the voxel triple rides the newest rung only; every modulo mirrors main.c (% 2 as != 0, % 5, % 4).
static int ref_settings_load_vox(size_t n, uint32_t magic, int32_t vox, int32_t pitch, int32_t zoom,
                                 int* oVox, int* oPitch, int* oZoom) {
	if (!ref_settings_load4(n, magic, 0, 0, 0, NULL, NULL, NULL, NULL)) return 0;
	if (n >= sizeof(RefSettings)) {
		*oVox = vox != 0; *oPitch = (int)(((unsigned)pitch) % 5); *oZoom = (int)(((unsigned)zoom) % 4);
	}
	return 1;
}

// TEST 6 — settings round-trip (I7.6) + the tier / default rules (§5) that are pure functions.
static void test_settings_and_tier(void) {
	printf("TEST 6: settings round-trip (I7.6) + performance tier / defaults (§5)\n");

	// -- (a) the offsetof ladder is strictly increasing, and each rung IS a shipped sizeof --------
	{
		const size_t rung[10] = {
			offsetof(RefSettings, dof), offsetof(RefSettings, bloom), offsetof(RefSettings, light),
			offsetof(RefSettings, vivid), offsetof(RefSettings, theme), offsetof(RefSettings, tilt),
			offsetof(RefSettings, presence), offsetof(RefSettings, traverse), offsetof(RefSettings, voxel),
			sizeof(RefSettings)
		};
		for (int i = 1; i < 10; i++)
			CHECK(rung[i] > rung[i - 1], "length rung %d (%u) must exceed rung %d (%u)\n",
			      i, (unsigned)rung[i], i - 1, (unsigned)rung[i - 1]);
		// I4.10's / A6.3's whole backward-compatibility claim in two lines: the length a PRE-PHASE-15
		// build wrote is exactly the new struct's offsetof(presence), and the length a PRE-TILT build
		// wrote is offsetof(tilt) — so BOTH still match a rung.
		CHECK(offsetof(RefSettings, voxel) == sizeof(RefSettings) - 3 * sizeof(int32_t),
		      "the pre-voxel full struct must be exactly three s32 shorter than the new one (phase 32)\n");
		CHECK(offsetof(RefSettings, traverse) + sizeof(int32_t) == offsetof(RefSettings, voxel),
		      "the phase-22.2 word is still the LAST word of the pre-voxel struct (append-only)\n");
		CHECK(offsetof(RefSettings, voxZoom) == sizeof(RefSettings) - sizeof(int32_t),
		      "the voxel triple is the tail of the struct\n");
		CHECK(offsetof(RefSettings, traverse) - offsetof(RefSettings, presence) == 4,
		      "...and phase 15's word is still exactly where it was (append-only)\n");
		CHECK(offsetof(RefSettings, presence) - offsetof(RefSettings, tilt) == 4,
		      "...as is phase 14's (two appends later, tilt has not moved)\n");
	}

	// -- (b) the loader accepts every historical length, and each field only rides its own rung --
	{
		int redesign = -1, tilt = 0 /* the g_prefs default */, pres = 0 /* A6.3.4: default OFF */;
		CHECK(ref_settings_load(sizeof(RefSettings), 0x33424744u, 2, 1, &redesign, &tilt, &pres) == 1,
		      "a file written by THIS build is accepted\n");
		CHECK(redesign == 1 && tilt == 2 && pres == 1,
		      "...and carries the redesign prefs, the tilt level AND the co-op pref\n");

		redesign = -1; tilt = 0; pres = 0;
		CHECK(ref_settings_load(offsetof(RefSettings, presence), 0x33424744u, 3, 1, &redesign, &tilt, &pres) == 1,
		      "a PRE-PRESENCE (phase-14) file is still accepted (A6.3 — no magic bump)\n");
		CHECK(redesign == 1 && tilt == 3, "...and still loads everything it had, tilt included\n");
		CHECK(pres == 0, "...and leaves co-op at the DEFAULT rather than reading past the file\n");

		redesign = -1; tilt = 0; pres = 0;
		CHECK(ref_settings_load(offsetof(RefSettings, tilt), 0x33424744u, 3, 1, &redesign, &tilt, &pres) == 1,
		      "a PRE-TILT file is still accepted (I4.10 — no magic bump)\n");
		CHECK(redesign == 1, "...and still loads every UI-redesign chrome pref it had\n");
		CHECK(tilt == 0 && pres == 0, "...and leaves BOTH later prefs at their defaults\n");

		redesign = -1; tilt = 0; pres = 0;
		CHECK(ref_settings_load(offsetof(RefSettings, theme), 0x33424744u, 3, 1, &redesign, &tilt, &pres) == 1,
		      "a pre-redesign file is still accepted\n");
		CHECK(redesign == 0 && tilt == 0 && pres == 0,
		      "...and gets defaults for the chrome prefs, tilt and co-op alike\n");

		CHECK(ref_settings_load(offsetof(RefSettings, dof), 0x33424744u, 1, 1, NULL, NULL, NULL) == 1,
		      "the oldest accepted rung still loads\n");
		CHECK(ref_settings_load(sizeof(RefSettings) - 1, 0x33424744u, 1, 1, NULL, NULL, NULL) == 0,
		      "an off-ladder length is rejected wholesale\n");
		CHECK(ref_settings_load(sizeof(RefSettings) + 4, 0x33424744u, 1, 1, NULL, NULL, NULL) == 0,
		      "a LONGER file (a future build) is rejected — I4.11's disclosed one-way property\n");
		CHECK(ref_settings_load(sizeof(RefSettings), 0xDEADBEEFu, 1, 1, NULL, NULL, NULL) == 0,
		      "a foreign magic is rejected at every length\n");
	}

	// -- (b2) phase 15: the co-op word is 2-state, so a CORRUPT s32 can only ever produce 0/1 -----
	{
		const int32_t nasty[8] = { 0, 1, -1, 2, 12345, -99999, INT32_MAX, INT32_MIN };
		for (int i = 0; i < 8; i++) {
			int pres = -7;
			CHECK(ref_settings_load(sizeof(RefSettings), 0x33424744u, 0, nasty[i], NULL, NULL, &pres) == 1,
			      "presence word %ld: the file still loads\n", (long)nasty[i]);
			CHECK(pres == 0 || pres == 1, "presence word %ld maps to 0/1 (got %d)\n", (long)nasty[i], pres);
			CHECK(pres == (nasty[i] != 0 ? 1 : 0), "presence word %ld round-trips as != 0\n", (long)nasty[i]);
		}
	}

	// -- (b3) phase 22.2: the traverse word is a 3-state ladder, so the SAME modulo rule applies —
	//         a corrupt s32 must land inside 0..2 and never index a label table out of range. And
	//         every file written before this build must leave it at the shipped default, Off.
	{
		const int32_t nasty[10] = { 0, 1, 2, 3, -1, -3, 12345, -99999, INT32_MAX, INT32_MIN };
		for (int i = 0; i < 10; i++) {
			int trav = -7;
			CHECK(ref_settings_load4(sizeof(RefSettings), 0x33424744u, 0, 0, nasty[i],
			                         NULL, NULL, NULL, &trav) == 1,
			      "traverse word %ld: the file still loads\n", (long)nasty[i]);
			CHECK(trav >= 0 && trav < SMART_TRAVERSE_LEVELS,
			      "traverse word %ld maps into 0..%d (got %d)\n",
			      (long)nasty[i], SMART_TRAVERSE_LEVELS - 1, trav);
		}
		CHECK((int)(((unsigned)INT32_MIN) % SMART_TRAVERSE_LEVELS) == 2,
		      "INT32_MIN specifically (2147483648 %% 3 == 2) lands inside the ladder — the unsigned "
		      "cast is what stops C's signed modulo returning a NEGATIVE index here\n");
		// Every pre-22.2 file: traverse must stay at its default, i.e. NOT be read past the file.
		int trav = 0;
		CHECK(ref_settings_load4(offsetof(RefSettings, traverse), 0x33424744u, 0, 1, 2,
		                         NULL, NULL, NULL, &trav) == 1,
		      "a PRE-TRAVERSE (phase-15..22.1) file is still accepted — no magic bump\n");
		CHECK(trav == 0, "...and leaves HM routing at the shipped default, Off (T4.1)\n");
		trav = 0;
		CHECK(ref_settings_load4(offsetof(RefSettings, presence), 0x33424744u, 0, 1, 2,
		                         NULL, NULL, NULL, &trav) == 1, "a phase-14 file still loads\n");
		CHECK(trav == 0, "...and also leaves HM routing Off\n");
		// Round trip, every level.
		for (int lv = 0; lv < SMART_TRAVERSE_LEVELS; lv++) {
			int got = -1;
			CHECK(ref_settings_load4(sizeof(RefSettings), 0x33424744u, 0, 0, lv,
			                         NULL, NULL, NULL, &got) == 1, "traverse %d file loads\n", lv);
			CHECK(got == lv, "traverse %d round-trips through the file (got %d)\n", lv, got);
		}
	}

	// -- (b4) phase 32: the voxel triple (SPEC-port 8.2). Defaults OFF / 40 deg (idx 2) / 100 % (idx 1);
	//         a corrupt word must land inside 0..1 / 0..4 / 0..3; and every file written before this
	//         build (including the phase-22.2..31 full struct) must leave all three at their defaults.
	{
		const int32_t nasty[10] = { 0, 1, 2, 3, 4, 5, -1, 12345, INT32_MAX, INT32_MIN };
		for (int i = 0; i < 10; i++) {
			int v = -7, p = -7, z = -7;
			CHECK(ref_settings_load_vox(sizeof(RefSettings), 0x33424744u, nasty[i], nasty[i], nasty[i], &v, &p, &z) == 1,
			      "voxel word %ld: the file still loads\n", (long)nasty[i]);
			CHECK(v == 0 || v == 1, "voxel word %ld maps to 0/1 (got %d)\n", (long)nasty[i], v);
			CHECK(p >= 0 && p < 5, "voxPitch word %ld maps into 0..4 (got %d)\n", (long)nasty[i], p);
			CHECK(z >= 0 && z < 4, "voxZoom word %ld maps into 0..3 (got %d)\n", (long)nasty[i], z);
		}
		int v = 0, p = 2, z = 1;   // the shipped defaults
		CHECK(ref_settings_load_vox(offsetof(RefSettings, voxel), 0x33424744u, 1, 4, 3, &v, &p, &z) == 1,
		      "a PRE-VOXEL (phase 22.2..31) file is still accepted - no magic bump\n");
		CHECK(v == 0 && p == 2 && z == 1, "...and leaves VOXEL OFF / 40 deg / 100 %% at their defaults\n");
		CHECK(ref_settings_load_vox(offsetof(RefSettings, traverse), 0x33424744u, 1, 4, 3, &v, &p, &z) == 1 &&
		      v == 0 && p == 2 && z == 1, "a pre-traverse file likewise leaves the voxel triple at its defaults\n");
		CHECK(ref_settings_load_vox(sizeof(RefSettings), 0x33424744u, 1, 4, 3, &v, &p, &z) == 1 &&
		      v == 1 && p == 4 && z == 3, "a file written by THIS build round-trips VOXEL on / idx 4 / idx 3\n");
		int trav = -1;
		CHECK(ref_settings_load4(offsetof(RefSettings, voxel), 0x33424744u, 0, 0, 2, NULL, NULL, NULL, &trav) == 1 && trav == 2,
		      "...and the pre-voxel file STILL carries its traverse word (the rung moved, the pref did not)\n");
	}

	// -- (c) the modulo maps EVERY s32 into the ladder, so a corrupt word can never index out ----
	{
		const int32_t nasty[10] = { 0, 1, 2, 3, 4, -1, -4, 12345, INT32_MAX, INT32_MIN };
		for (int i = 0; i < 10; i++) {
			int v = (int)(((unsigned)nasty[i]) % TILT_LEVELS);
			CHECK(v >= 0 && v < TILT_LEVELS, "s32 %ld maps into 0..%d (got %d)\n",
			      (long)nasty[i], TILT_LEVELS - 1, v);
			// ...and the clamped value is a legal index into the shipped angle ladder.
			CHECK(tilt_angle_deg_for_level(v) == TILT_ANGLE_DEG[v], "...and indexes the ladder\n");
		}
		CHECK((int)(((unsigned)INT32_MIN) % TILT_LEVELS) == 0,
		      "INT32_MIN specifically (the classic signed-modulo trap) lands on Off\n");
	}

	// -- (d) round trip: every level survives save -> load -> gate -> tween -> angle -------------
	for (int lv = 0; lv < TILT_LEVELS; lv++) {
		RefSettings s; memset(&s, 0, sizeof s);
		s.magic = 0x33424744u; s.tilt = lv;
		int got = -1;
		CHECK(ref_settings_load(sizeof s, s.magic, s.tilt, s.presence, NULL, &got, NULL) == 1,
		      "level %d file loads\n", lv);
		CHECK(got == lv, "level %d round-trips through the file (got %d)\n", lv, got);
		TiltGateIn in = gate_clear(0, got);
		CHECK(tilt_target_level(&in) == lv, "level %d survives an all-clear gate\n", lv);
		CHECK(tilt_angle_deg_for_level(lv) == TILT_ANGLE_DEG[lv], "level %d maps to its ladder angle\n", lv);
	}

	// -- (e) THE DEFAULT (I5.8): a fresh install is inert -----------------------------------------
	{
		CHECK(TILT_ANGLE_DEG[0] == 0.0f, "level 0 is EXACTLY 0 deg — the identity map (invariant 1)\n");
		TiltGateIn in = gate_clear(0, 0);              // g_prefs.tiltLevel default
		CHECK(tilt_target_level(&in) == 0, "the shipped default gates off on an all-clear frame\n");
		TiltTween t; tilt_tween_reset(&t);
		for (int f = 0; f < 60; f++) tilt_tween_step(&t, 0, 0.0f, 16.7f);
		CHECK(t.ang == 0.0f, "...and a whole second of stepping leaves ang bitwise 0.0f\n");
		CHECK(tilt_active(&t) == 0, "...so the default binary NEVER leaves the flat path\n");
	}

	// -- (f) I5.2 tier clamp, exhaustively: an Old 3DS is flat for every reachable input ---------
	{
		long rows = 0, bad = 0;
		for (int lv = 0; lv < TILT_LEVELS; lv++)
			for (int sc = 0; sc < 2; sc++)
				for (int ci = 0; ci < 9; ci++)
					for (int b = 0; b < 8; b++) {          // textDlg / touchActive / stereoEngaged
						TiltGateIn in = gate_clear(sc, lv);
						in.isN3DS = 0;
						in.ctx = CTX_ALL[ci];
						in.textDlg       = (b & 1) ? 1 : 0;
						in.touchActive   = (b & 2) ? 1 : 0;
						in.stereoEngaged = (b & 4) ? 1 : 0;
						rows++;
						if (tilt_target_level(&in) != 0) bad++;
					}
		CHECK(rows == 4 * 2 * 9 * 8, "the Old-3DS sweep covered every row (%ld)\n", rows);
		CHECK(bad == 0, "%ld Old-3DS rows tilted — G2 must clamp the LIVE level to 0 everywhere\n", bad);
	}

	// -- (g) I5.1 "clamp live, NEVER rewrite": the gate is a pure read ---------------------------
	// The saved preference is an INPUT; a tier clamp may only lower the answer, never touch the
	// struct. Proven two ways over the tier cross product: the result is <= userLevel, and the
	// input bytes are unchanged by the call.
	{
		long rows = 0, over = 0, mutated = 0;
		for (int lv = 0; lv < TILT_LEVELS; lv++)
			for (int sc = 0; sc < 2; sc++)
				for (int n3 = 0; n3 < 2; n3++)
					for (int fs = 0; fs < 2; fs++)
						for (int fo = 0; fo < 2; fo++)
							for (int wl = 0; wl < 2; wl++) {
								TiltGateIn in = gate_clear(sc, lv);
								in.isN3DS = n3; in.fsOn = fs; in.focScreen = fo; in.wlOn = wl;
								TiltGateIn before = in;
								int r = tilt_target_level(&in);
								rows++;
								if (r > lv) over++;
								if (memcmp(&before, &in, sizeof in) != 0) mutated++;
							}
		CHECK(rows == 4 * 2 * 2 * 2 * 2 * 2, "the clamp sweep covered every row (%ld)\n", rows);
		CHECK(over == 0, "%ld rows returned MORE than the saved level — a clamp may only lower\n", over);
		CHECK(mutated == 0, "%ld rows mutated the input — the saved preference is read-only (I5.1)\n", mutated);
	}

	// -- (h) I5.4 / I5.5 as tier rules, stated as the behaviour a tester will look for -----------
	{
		TiltGateIn in = gate_clear(0, 3); in.wlOn = 1;
		CHECK(tilt_target_level(&in) == 0, "a wireless link forces tilt off (I5.5)\n");
		in = gate_clear(0, 3); in.netOn = 1;
		CHECK(tilt_target_level(&in) == 0, "...and so does the loopback net link\n");
		// linkOn (the in-process cable, both cores free-running) is deliberately NOT a rule and has
		// no field in TiltGateIn at all — so it CANNOT accidentally become one. That absence is the
		// assertion; record it here so the asymmetry is not "fixed" later.
		in = gate_clear(0, 3); in.fsOn = 1; in.focScreen = 0;
		CHECK(tilt_target_level(&in) == 3, "frameskip keeps the FOCUSED screen tilting (I5.4)\n");
		in.focScreen = 1;
		CHECK(tilt_target_level(&in) == 0, "...and starves the unfocused one\n");
	}
}

// --------------------------------------------------------------------------------------------
// TEST 7 — R6.9 T1: at angle 0 the projection is the IDENTITY, bitwise. This is the algebraic
// form of PHASE.md invariant 1. Sample points are exact binary fractions (the real mesh's 16 px
// grid), so 120 + (cx - 120) is exact in float32 and == is the right comparison.
// --------------------------------------------------------------------------------------------
static void test_identity(void) {
	printf("TEST 7: identity at angle 0, bitwise (R6.9 T1 / PHASE invariant 1)\n");
	TiltView v; tilt_view_init(&v, 0.0f, VW, VH, TILT_COVER_MIX);

	CHECK(v.sinA == 0.0f && v.cosA == 1.0f, "sin/cos exact at 0 (%.9g / %.9g)\n", (double)v.sinA, (double)v.cosA);
	CHECK(v.k  == 1.0f,  "cover scale is exactly 1 at angle 0 (%.9g)\n", (double)v.k);
	CHECK(v.yc == 80.0f, "the anchor is exactly vh/2 at angle 0 (%.9g)\n", (double)v.yc);
	CHECK(v.d  == 160.0f, "d = TILT_FOCAL * vh = 160 (%.9g)\n", (double)v.d);

	for (int gy = 0; gy <= 160; gy += 40) for (int gx = 0; gx <= 240; gx += 60) {
		float fx, fy, q;
		tilt_project(&v, (float)gx, (float)gy, &fx, &fy, &q);
		CHECK(fx == (float)gx, "fx identity at (%d,%d): %.9g\n", gx, gy, (double)fx);
		CHECK(fy == (float)gy, "fy identity at (%d,%d): %.9g\n", gx, gy, (double)fy);
		CHECK(q  == 1.0f,      "q == 1 exactly at (%d,%d): %.9g\n", gx, gy, (double)q);
		CHECK(tilt_disp_scale(&v, (float)gy) == 1.0f,
		      "disp scale == 1 exactly at cy=%d (stereo clamp bit-identical to today)\n", gy);
	}
	// the whole 16 px vertex grid the tilt mesh actually emits (16x11 = 176 verts, main.c:988-991)
	int gridBad = 0;
	for (int r = 0; r <= 10; r++) for (int c = 0; c <= 15; c++) {
		float fx, fy, q;
		tilt_project(&v, (float)(c * 16), (float)(r * 16), &fx, &fy, &q);
		if (fx != (float)(c * 16) || fy != (float)(r * 16) || q != 1.0f) gridBad++;
	}
	CHECK(gridBad == 0, "%d of 176 mesh vertices moved at angle 0\n", gridBad);

	float voidFar = -1.0f, cropMid = -1.0f, cropNear = -1.0f;
	tilt_coverage(&v, &voidFar, &cropMid, &cropNear);
	CHECK(voidFar == 0.0f && cropMid == 0.0f && cropNear == 0.0f,
	      "zero void and zero crop at angle 0 (%.9g / %.9g / %.9g)\n",
	      (double)voidFar, (double)cropMid, (double)cropNear);
}

// --------------------------------------------------------------------------------------------
// TEST 8 — R6.9 T2 / R1.8: the shipped corner table (bottom-anchored, k = tilt_cover_fit).
// Tolerance 1e-4 px, the spec's own.
// --------------------------------------------------------------------------------------------
static void test_corners(void) {
	printf("TEST 8: the R1.8/T2 corner table (shipped bottom-anchored model)\n");
	struct Row { float deg, cx, cy, fx, fy, q; };
	static const struct Row R[] = {
		// R1.8, angle 0 (identity)
		{  0.0f,   0.0f,   0.0f,   0.000000f,   0.000000f, 1.00000000f },
		{  0.0f, 240.0f,   0.0f, 240.000000f,   0.000000f, 1.00000000f },
		{  0.0f,   0.0f, 160.0f,   0.000000f, 160.000000f, 1.00000000f },
		{  0.0f, 240.0f, 160.0f, 240.000000f, 160.000000f, 1.00000000f },
		// R1.8, 10 deg
		{ 10.0f,   0.0f,   0.0f,   8.728425f,   0.000000f, 0.92011210f },
		{ 10.0f, 240.0f,   0.0f, 231.271575f,   0.000000f, 0.92011210f },
		{ 10.0f,   0.0f, 160.0f, -12.430812f, 160.000000f, 1.09507926f },
		{ 10.0f, 240.0f, 160.0f, 252.430812f, 160.000000f, 1.09507926f },
		{ 10.0f, 120.0f,  80.0f, 120.000000f,  73.054073f, 1.00000000f },
		// R1.8, 15 deg
		{ 15.0f,   0.0f,   0.0f,  11.843810f,   0.000000f, 0.88541842f },
		{ 15.0f, 240.0f,   0.0f, 228.156190f,   0.000000f, 0.88541842f },
		{ 15.0f,   0.0f, 160.0f, -20.310093f, 160.000000f, 1.14864569f },
		{ 15.0f, 240.0f, 160.0f, 260.310093f, 160.000000f, 1.14864569f },
		{ 15.0f, 120.0f,  80.0f, 120.000000f,  69.647238f, 1.00000000f },
		// R1.8, 20 deg
		{ 20.0f,   0.0f,   0.0f,  14.136881f,   0.000000f, 0.85396362f },
		{ 20.0f, 240.0f,   0.0f, 225.863119f,   0.000000f, 0.85396362f },
		{ 20.0f,   0.0f, 160.0f, -29.539547f, 160.000000f, 1.20628727f },
		{ 20.0f, 240.0f, 160.0f, 269.539547f, 160.000000f, 1.20628727f },
		{ 20.0f, 120.0f,  80.0f, 120.000000f,  66.319194f, 1.00000000f },
		// T2 extras: the centre row at 25 deg and the near corner at 35 deg (off-ladder, math only)
		{ 25.0f, 120.0f,  80.0f, 120.000000f,  63.095270f, 1.00000000f },
		{ 35.0f,   0.0f, 160.0f, -68.505403f, 160.000000f, 1.40210808f },
	};
	for (unsigned i = 0; i < sizeof R / sizeof R[0]; i++) {
		TiltView v; tilt_view_init(&v, R[i].deg * D2R, VW, VH, TILT_COVER_MIX);
		float fx, fy, q;
		tilt_project(&v, R[i].cx, R[i].cy, &fx, &fy, &q);
		NEAR(fx, R[i].fx, 1e-4, "a=%.0f (%.0f,%.0f) fx\n", (double)R[i].deg, (double)R[i].cx, (double)R[i].cy);
		NEAR(fy, R[i].fy, 1e-4, "a=%.0f (%.0f,%.0f) fy\n", (double)R[i].deg, (double)R[i].cx, (double)R[i].cy);
		NEAR(q,  R[i].q,  1e-6, "a=%.0f (%.0f,%.0f) q\n",  (double)R[i].deg, (double)R[i].cx, (double)R[i].cy);
		// R1.3: main.c emits iq = 1/q as clip-space w. Emitting q instead is the single easiest
		// way to ship a subtly-wrong tilt (up to 31 px off at 15 deg), so pin the reciprocal too.
		NEAR(1.0f / q, 1.0 / (double)R[i].q, 1e-6, "a=%.0f iq = 1/q\n", (double)R[i].deg);
	}
	// the frame is left/right symmetric about cx = vw/2 at every angle
	for (int deg = 0; deg <= 35; deg += 5) {
		TiltView v; tilt_view_init(&v, (float)deg * D2R, VW, VH, TILT_COVER_MIX);
		for (int cy = 0; cy <= 160; cy += 40) {
			float lx, ly, lq, rx, ry, rq;
			tilt_project(&v,   0.0f, (float)cy, &lx, &ly, &lq);
			tilt_project(&v, 240.0f, (float)cy, &rx, &ry, &rq);
			NEAR(lx + rx, 240.0f, 2e-4, "a=%d cy=%d: left/right symmetry about vw/2\n", deg, cy);
			NEAR(ly, ry, 1e-4, "a=%d cy=%d: a row projects to one fy\n", deg, cy);
		}
	}
}

// --------------------------------------------------------------------------------------------
// TEST 9 — R6.9 T3: the vertical fit is EXACT at every angle. fy(0) == 0 and fy(160) == 160 is
// the defining property of tilt_cover_fit + the bottom anchor (R2.3), and the cheapest possible
// guard on k and yc: zero rows are lost off the top or the bottom, at any angle.
// --------------------------------------------------------------------------------------------
static void test_vfit(void) {
	printf("TEST 9: vertical fit is exact at every angle (R6.9 T3)\n");
	static const float DEGS[] = { 0.0f, 5.0f, 8.0f, 10.0f, 12.0f, 15.0f, 18.0f, 20.0f, 25.0f, 35.0f };
	for (unsigned i = 0; i < sizeof DEGS / sizeof DEGS[0]; i++) {
		TiltView v; tilt_view_init(&v, DEGS[i] * D2R, VW, VH, TILT_COVER_MIX);
		float fx, fyTop, fyBot, q;
		tilt_project(&v, 120.0f,   0.0f, &fx, &fyTop, &q);
		tilt_project(&v, 120.0f, 160.0f, &fx, &fyBot, &q);
		NEAR(fyTop,   0.0f, 1e-4, "a=%.0f: projected top row\n", (double)DEGS[i]);
		NEAR(fyBot, 160.0f, 1e-4, "a=%.0f: projected bottom row\n", (double)DEGS[i]);
		// R2.5.4: the re-anchoring rides the image UP by (vh/2 - yc) frame px — monotone in a.
		CHECK(v.yc <= 80.0f + 1e-4f, "a=%.0f: yc %.4f must never sit below vh/2\n",
		      (double)DEGS[i], (double)v.yc);
	}
	// the published shift: +6.95 / +10.35 / +13.68 frame px at 10 / 15 / 20 deg (R1.8 centre check)
	static const float SDEG[] = { 10.0f, 15.0f, 20.0f };
	static const float SHIFT[] = { 6.945927f, 10.352762f, 13.680806f };
	for (unsigned i = 0; i < 3; i++) {
		TiltView v; tilt_view_init(&v, SDEG[i] * D2R, VW, VH, TILT_COVER_MIX);
		NEAR(80.0f - v.yc, SHIFT[i], 1e-3, "a=%.0f: upward re-anchoring\n", (double)SDEG[i]);
	}
}

// --------------------------------------------------------------------------------------------
// TEST 10 — R6.9 T4: pin the projection to the EXTERNAL source of truth, gen1recomp's own
// Tilt.groundPoint (Tilt.lua:120-130) in its centre-pinned form (k = 1, yc = vh/2), independent
// of our cover/anchor choices. Constructed by overriding the two fields tilt_view_init derives.
// --------------------------------------------------------------------------------------------
static void test_gen1_reference(void) {
	printf("TEST 10: gen1recomp Tilt.groundPoint reference values (R6.9 T4)\n");
	struct Row { float deg, cy, sy; };
	static const struct Row R[] = {
		{  0.0f,   0.0f,   0.000000f }, {  0.0f,  80.0f,  80.000000f }, {  0.0f, 160.0f, 160.000000f },
		{ 15.0f,   0.0f,  11.580118f }, { 15.0f,  80.0f,  80.000000f }, { 15.0f, 160.0f, 168.760523f },
		{ 25.0f,   0.0f,  20.143584f }, { 25.0f,  80.0f,  80.000000f }, { 25.0f, 160.0f, 171.930344f },
		{ 35.0f,   0.0f,  29.073078f }, { 35.0f,  80.0f,  80.000000f }, { 35.0f, 160.0f, 171.883176f },
	};
	for (unsigned i = 0; i < sizeof R / sizeof R[0]; i++) {
		TiltView v; tilt_view_init(&v, R[i].deg * D2R, VW, VH, TILT_COVER_MIX);
		v.k = 1.0f; v.yc = VH * 0.5f;                 // <- their centre-pinned form, verbatim
		float fx, fy, q;
		tilt_project(&v, 120.0f, R[i].cy, &fx, &fy, &q);
		NEAR(fy, R[i].sy, 1e-4, "gen1 groundPoint sy at a=%.0f cy=%.0f\n", (double)R[i].deg, (double)R[i].cy);
	}
	// ...and the X half, which exercises the u*q term (R1.9's second table, left edge cx = 0)
	struct RowX { float deg, cy, sx; };
	static const struct RowX RX[] = {
		{  0.0f,   0.0f,   0.000000f },
		{ 15.0f,   0.0f,  13.749789f }, { 15.0f, 160.0f, -17.837483f },
		{ 25.0f,   0.0f,  20.933629f }, { 25.0f, 160.0f, -32.150868f },
		{ 35.0f,   0.0f,  26.744561f }, { 35.0f, 160.0f, -48.252969f },
	};
	for (unsigned i = 0; i < sizeof RX / sizeof RX[0]; i++) {
		TiltView v; tilt_view_init(&v, RX[i].deg * D2R, VW, VH, TILT_COVER_MIX);
		v.k = 1.0f; v.yc = VH * 0.5f;
		float fx, fy, q;
		tilt_project(&v, 0.0f, RX[i].cy, &fx, &fy, &q);
		NEAR(fx, RX[i].sx, 1e-4, "gen1 groundPoint sx at a=%.0f cy=%.0f\n", (double)RX[i].deg, (double)RX[i].cy);
	}
}

// --------------------------------------------------------------------------------------------
// TEST 11 — R6.9 T5: the R2.2 growth-factor table, including the cross-check that our formulas
// reproduce gen1's published base(a) = 1/(cos a * topScale) numbers (1.17 / 1.57 / 2.15 at
// 15 / 35 / 50 deg, gen1-render.md finding 4) — i.e. our formulas ARE their formulas.
// --------------------------------------------------------------------------------------------
static void test_growth(void) {
	printf("TEST 11: view-growth factors vs the R2.2 table (R6.9 T5)\n");
	struct Row { float deg, base, kfit, kfull, topS, botS; };
	static const struct Row R[] = {   // R2.2, verbatim
		{  0.0f, 1.0000f, 1.00000f, 1.0000f, 1.00000f, 1.00000f },
		{  5.0f, 1.0476f, 1.00191f, 1.0436f, 0.95824f, 1.04556f },
		{  8.0f, 1.0801f, 1.00494f, 1.0696f, 0.93494f, 1.07479f },
		{ 10.0f, 1.1036f, 1.00777f, 1.0868f, 0.92011f, 1.09508f },
		{ 12.0f, 1.1286f, 1.01129f, 1.1040f, 0.90583f, 1.11602f },
		{ 15.0f, 1.1693f, 1.01794f, 1.1294f, 0.88542f, 1.14865f },
		{ 18.0f, 1.2139f, 1.02636f, 1.1545f, 0.86617f, 1.18274f },
		{ 20.0f, 1.2462f, 1.03306f, 1.1710f, 0.85396f, 1.20629f },
		{ 25.0f, 1.3365f, 1.05411f, 1.2113f, 0.82555f, 1.26792f },
		{ 35.0f, 1.5709f, 1.12037f, 1.2868f, 0.77713f, 1.40211f },
		{ 50.0f, 2.1516f, 1.32749f, 1.3830f, 0.72305f, 1.62080f },
	};
	for (unsigned i = 0; i < sizeof R / sizeof R[0]; i++) {
		float a = R[i].deg * D2R;
		NEAR(tilt_cover_fit(a),  R[i].kfit,  1e-5, "kfit at %.0f deg\n",  (double)R[i].deg);
		NEAR(tilt_cover_full(a), R[i].kfull, 1e-4, "kfull at %.0f deg\n", (double)R[i].deg);
		NEAR(tilt_top_scale(a),  R[i].topS,  1e-5, "topScale at %.0f deg\n", (double)R[i].deg);
		NEAR(tilt_bot_scale(a),  R[i].botS,  1e-5, "botScale at %.0f deg\n", (double)R[i].deg);
		// gen1's centre-pinned cover growth, Tilt.lua:135
		NEAR(1.0f / (cosf(a) * tilt_top_scale(a)), R[i].base, 1e-4, "gen1 base at %.0f deg\n", (double)R[i].deg);
		// identity used by the coverage math: kfit * botScale == base (see tilt_coverage)
		NEAR(tilt_cover_fit(a) * tilt_bot_scale(a), R[i].base, 1e-4, "kfit*botScale == base at %.0f deg\n",
		     (double)R[i].deg);
		// R2.5.1: mix = 0 is exactly kfit, mix = 1 is exactly kfull
		CHECK(tilt_cover(a, 0.0f) == tilt_cover_fit(a), "cover(mix=0) == kfit at %.0f deg\n", (double)R[i].deg);
		NEAR(tilt_cover(a, 1.0f), tilt_cover_full(a), 1e-6, "cover(mix=1) == kfull at %.0f deg\n",
		     (double)R[i].deg);
		NEAR(tilt_cover(a, 0.5f), 0.5f * (R[i].kfit + R[i].kfull), 1e-4, "cover(mix=0.5) at %.0f deg\n",
		     (double)R[i].deg);
	}
	CHECK(tilt_cover(0.0f, TILT_COVER_MIX) == 1.0f, "cover is exactly 1 at angle 0 for the shipped mix\n");
}

// --------------------------------------------------------------------------------------------
// TEST 12 — R6.9 T6: tilt_unproject o tilt_project == identity within 0.01 px. Not needed by the
// shipped configuration (the bottom screen stays flat so touch_to_gba is untouched — R4.8.1 /
// SPEC-integration I3.1), but the inverse is the thing a later slice would get silently wrong,
// and silent mis-taps are the regression PHASE.md invariant 4 names.
// --------------------------------------------------------------------------------------------
static void test_roundtrip(void) {
	printf("TEST 12: projection round trip (R6.9 T6)\n");
	static const float DEGS[] = { 0.0f, 10.0f, 15.0f, 20.0f };
	double worst = 0.0;
	for (unsigned i = 0; i < sizeof DEGS / sizeof DEGS[0]; i++) {
		TiltView v; tilt_view_init(&v, DEGS[i] * D2R, VW, VH, TILT_COVER_MIX);
		for (int r = 0; r < 5; r++) for (int c = 0; c < 5; c++) {
			float cx = 240.0f * (float)c / 4.0f, cy = 160.0f * (float)r / 4.0f;
			float fx, fy, q, bx, by;
			tilt_project(&v, cx, cy, &fx, &fy, &q);
			tilt_unproject(&v, fx, fy, &bx, &by);
			NEAR(bx, cx, 0.01, "a=%.0f round-trip cx at (%.0f,%.0f)\n", (double)DEGS[i], (double)cx, (double)cy);
			NEAR(by, cy, 0.01, "a=%.0f round-trip cy at (%.0f,%.0f)\n", (double)DEGS[i], (double)cy, (double)cy);
			double e = fabs((double)bx - cx); if (e > worst) worst = e;
			e = fabs((double)by - cy);        if (e > worst) worst = e;
		}
	}
	printf("        worst round-trip error over the grid: %.3g px\n", worst);
	CHECK(worst < 0.01, "worst round-trip error %.4g px exceeds 0.01\n", worst);
	// the inverse is the identity at angle 0 too
	TiltView v0; tilt_view_init(&v0, 0.0f, VW, VH, TILT_COVER_MIX);
	for (int gy = 0; gy <= 160; gy += 80) for (int gx = 0; gx <= 240; gx += 120) {
		float bx, by; tilt_unproject(&v0, (float)gx, (float)gy, &bx, &by);
		CHECK(bx == (float)gx && by == (float)gy, "unproject identity at (%d,%d): %.6f,%.6f\n",
		      gx, gy, (double)bx, (double)by);
	}
}

// --------------------------------------------------------------------------------------------
// TEST 13 — R6.9 T7: the R2.4 coverage table. DEFINITIONS (the spec's labels are terse and the
// obvious mis-reading is expensive, so they are pinned here):
//   void @ far row  = DESTINATION px per side of the frame rect left uncovered  = (vw/2)(1 - k*q)
//   crop @ row      = SOURCE columns per side that fall outside the rect        = (vw/2)(1 - 1/(k*q))
// e.g. at 15 deg the near row PROJECTS to fx in [-20.31, 260.31] (R1.8), but the source columns
// lost per side are 17.370, not 20.310 — because 240/(k*q) source px survive, not 240 - 2*20.31.
// The identity k*q_near = kfit*botScale = base(a) makes cropNear = (vw/2)(1 - 1/base) exactly.
// --------------------------------------------------------------------------------------------
static void test_coverage(void) {
	printf("TEST 13: coverage diagnostics vs the R2.4 table (R6.9 T7)\n");
	struct Row { float deg, voidFar, cropMid, cropNear; };
	static const struct Row R[] = {   // R2.4, verbatim (-1 = the spec prints no value)
		{  0.0f,  0.000f,  0.000f,  0.000f },
		{  5.0f,  4.791f,  0.229f,  5.449f },
		{  8.0f,  7.253f,  0.590f,  8.899f },
		{ 10.0f,  8.728f,  0.925f, 11.264f },
		{ 12.0f, 10.073f,  1.340f, 13.675f },
		{ 15.0f, 11.844f,  2.115f, 17.370f },
		{ 18.0f, 13.320f,  3.082f, 21.147f },
		{ 20.0f, 14.137f,  3.840f, 23.704f },
		{ 25.0f, 15.573f,  6.160f, 30.215f },
		{ 35.0f, 15.520f, 12.892f, 43.610f },
	};
	for (unsigned i = 0; i < sizeof R / sizeof R[0]; i++) {
		TiltView v; tilt_view_init(&v, R[i].deg * D2R, VW, VH, TILT_COVER_MIX);
		float vf, cm, cn;
		tilt_coverage(&v, &vf, &cm, &cn);
		NEAR(vf, R[i].voidFar,  0.01, "a=%.0f void/side @ far row\n",  (double)R[i].deg);
		NEAR(cm, R[i].cropMid,  0.01, "a=%.0f crop/side @ mid row\n",  (double)R[i].deg);
		NEAR(cn, R[i].cropNear, 0.01, "a=%.0f crop/side @ near row\n", (double)R[i].deg);
		// the void figure IS the projected top-left corner (R1.8's fx at (0,0)) — one number, two
		// tables; a drift between them would mean the wedge is not where R2.6 says it is.
		float fx, fy, q; tilt_project(&v, 0.0f, 0.0f, &fx, &fy, &q);
		NEAR(vf, fx, 1e-4, "a=%.0f: void/side == fx(0,0)\n", (double)R[i].deg);
		// R2.5.5's headline: the void never exceeds ~1.91 % of the frame on the shipped ladder
		if (R[i].deg <= 20.0f) CHECK(vf <= 14.2f, "a=%.0f: void %.3f px/side is off the ladder budget\n",
		                             (double)R[i].deg, (double)vf);
	}
	// TILT_COVER_MIX = 1 is the documented zero-void escape hatch (R2.5.6): void goes to 0 and the
	// near-row crop grows (27.50 px/side at 15 deg per R2.4's option-(a) table).
	TiltView vf1; tilt_view_init(&vf1, 15.0f * D2R, VW, VH, 1.0f);
	float vf, cm, cn; tilt_coverage(&vf1, &vf, &cm, &cn);
	NEAR(vf, 0.0f, 1e-3, "mix=1 leaves zero void at 15 deg\n");
	NEAR(cn, 27.50f, 0.05, "mix=1 near-row crop at 15 deg (R2.4 option (a))\n");
}

// --------------------------------------------------------------------------------------------
// TEST 14 — R6.9 T8: monotonicity and the sanity properties the projection rests on.
// --------------------------------------------------------------------------------------------
static void test_sanity(void) {
	printf("TEST 14: monotonicity + sanity (R6.9 T8 / R1.2)\n");
	for (int deg = 1; deg <= 60; deg++) {
		TiltView v; tilt_view_init(&v, (float)deg * D2R, VW, VH, TILT_COVER_MIX);
		float prevQ = -1.0f, prevY = -1e9f;
		int qBad = 0, yBad = 0, posBad = 0;
		for (int cy = 0; cy <= 160; cy++) {
			float fx, fy, q;
			tilt_project(&v, 120.0f, (float)cy, &fx, &fy, &q);
			if (q <= 0.0f) posBad++;                      // R1.2: q > 0 everywhere, no clip case
			if (q <= prevQ) qBad++;                       // strictly increasing toward the viewer
			if (fy <= prevY) yBad++;                      // rows keep their order (no fold-over)
			prevQ = q; prevY = fy;
		}
		CHECK(posBad == 0, "a=%d: %d rows with q <= 0\n", deg, posBad);
		CHECK(qBad == 0,   "a=%d: %d rows where q did not strictly increase with cy\n", deg, qBad);
		CHECK(yBad == 0,   "a=%d: %d rows where fy did not strictly increase (fold-over)\n", deg, yBad);
	}
	for (int deg = 0; deg <= 45; deg++) {
		float a = (float)deg * D2R;
		CHECK(tilt_cover_fit(a) <= tilt_cover_full(a) + 1e-6f,
		      "a=%d: kfit %.6f must not exceed kfull %.6f\n", deg,
		      (double)tilt_cover_fit(a), (double)tilt_cover_full(a));
		CHECK(tilt_cover_fit(a) >= 1.0f - 1e-6f, "a=%d: kfit %.6f dipped below 1\n", deg,
		      (double)tilt_cover_fit(a));
		CHECK(tilt_top_scale(a) <= 1.0f + 1e-6f && tilt_bot_scale(a) >= 1.0f - 1e-6f,
		      "a=%d: the far edge must shrink and the near edge grow\n", deg);
	}
	// R1.10 sign convention: a > 0 recedes the TOP (rows above centre shrink, q < 1) and brings the
	// BOTTOM toward the viewer (q > 1). Getting this backwards is a plausible-looking wrong tilt.
	TiltView v; tilt_view_init(&v, 15.0f * D2R, VW, VH, TILT_COVER_MIX);
	float fx, fy, qTop, qBot, qMid;
	tilt_project(&v, 120.0f,   0.0f, &fx, &fy, &qTop);
	tilt_project(&v, 120.0f,  80.0f, &fx, &fy, &qMid);
	tilt_project(&v, 120.0f, 160.0f, &fx, &fy, &qBot);
	CHECK(qTop < 1.0f && qBot > 1.0f && qMid == 1.0f,
	      "sign convention: qTop %.5f < 1 < qBot %.5f, qMid %.5f == 1\n",
	      (double)qTop, (double)qMid, (double)qBot);
	// and the far row is narrower on screen than the near row (the trapezoid actually tapers)
	float lxT, lyT, rxT, ryT, lxB, lyB, rxB, ryB, qq;
	tilt_project(&v,   0.0f,   0.0f, &lxT, &lyT, &qq);
	tilt_project(&v, 240.0f,   0.0f, &rxT, &ryT, &qq);
	tilt_project(&v,   0.0f, 160.0f, &lxB, &lyB, &qq);
	tilt_project(&v, 240.0f, 160.0f, &rxB, &ryB, &qq);
	CHECK((rxT - lxT) < (rxB - lxB), "the far edge (%.3f px) must be narrower than the near (%.3f px)\n",
	      (double)(rxT - lxT), (double)(rxB - lxB));
}

// --------------------------------------------------------------------------------------------
// TEST 15 — R6.9 T10: stereo disparity foreshortening. Displacing in frame space BEFORE the
// projection is the correct order (R4.2), but it multiplies the on-screen disparity by k*q(row),
// which would silently raise the hardware-validated comfort ceiling POP_DISP_MAX (main.c:694) by
// 17-25 % at the near edge. Re-clamping the PROJECTED disparity keeps the shipped ceiling exact.
// --------------------------------------------------------------------------------------------
static void test_disp(void) {
	printf("TEST 15: stereo disparity foreshortening + re-clamp (R6.9 T10 / R4.1.1)\n");
	struct Row { float deg, nearScale; };
	static const struct Row R[] = { { 15.0f, 1.169f }, { 20.0f, 1.246f } };
	for (unsigned i = 0; i < sizeof R / sizeof R[0]; i++) {
		TiltView v; tilt_view_init(&v, R[i].deg * D2R, VW, VH, TILT_COVER_MIX);
		float sNear = tilt_disp_scale(&v, 160.0f);
		NEAR(sNear, R[i].nearScale, 1e-3, "a=%.0f near-row disparity scale\n", (double)R[i].deg);
		CHECK(sNear > 1.0f, "a=%.0f: the near row must carry MORE disparity (%.4f)\n",
		      (double)R[i].deg, (double)sNear);
		CHECK(tilt_disp_scale(&v, 0.0f) < 1.0f, "a=%.0f: the far row must carry LESS disparity\n",
		      (double)R[i].deg);
		NEAR(tilt_disp_scale(&v, 80.0f), v.k, 1e-6, "a=%.0f: the centre row scales by exactly k\n",
		     (double)R[i].deg);
		// the shipped ceiling survives the projection unchanged
		float projected = clamp_disp(POP_DISP_MAX * sNear);
		CHECK(projected == POP_DISP_MAX, "a=%.0f: re-clamped near-row disparity %.4f != %.4f\n",
		      (double)R[i].deg, (double)projected, (double)POP_DISP_MAX);
		// and a sub-ceiling disparity is still allowed to grow, up to the ceiling
		float small = clamp_disp(2.0f * sNear);
		CHECK(small > 2.0f && small <= POP_DISP_MAX, "a=%.0f: sub-ceiling disparity %.4f\n",
		      (double)R[i].deg, (double)small);
	}
	// at angle 0 the clamp is bit-identical to today's flat path
	TiltView v0; tilt_view_init(&v0, 0.0f, VW, VH, TILT_COVER_MIX);
	for (int cy = 0; cy <= 160; cy += 16)
		CHECK(clamp_disp(3.25f * tilt_disp_scale(&v0, (float)cy)) == clamp_disp(3.25f),
		      "cy=%d: angle-0 disparity clamp is bit-identical to the flat path\n", cy);
}

// --------------------------------------------------------------------------------------------
// TEST 16 — the shipped angle ladder (R2.5.5) and its level clamp.
// --------------------------------------------------------------------------------------------
static void test_ladder(void) {
	printf("TEST 16: the OFF/10/15/20 angle ladder (R2.5.5)\n");
	CHECK(TILT_LEVELS == 4, "four levels: OFF / Soft / Med / Deep\n");
	CHECK(TILT_ANGLE_DEG[0] == 0.0f, "level 0 is EXACTLY 0 deg (the identity map)\n");
	CHECK(TILT_ANGLE_DEG[1] == 10.0f && TILT_ANGLE_DEG[2] == 15.0f && TILT_ANGLE_DEG[3] == 20.0f,
	      "the ladder is 10/15/20 deg (%.1f/%.1f/%.1f)\n",
	      (double)TILT_ANGLE_DEG[1], (double)TILT_ANGLE_DEG[2], (double)TILT_ANGLE_DEG[3]);
	for (int i = 1; i < TILT_LEVELS; i++)
		CHECK(TILT_ANGLE_DEG[i] > TILT_ANGLE_DEG[i - 1], "the ladder is strictly increasing at %d\n", i);
	CHECK(tilt_angle_deg_for_level(-7) == 0.0f, "a negative level clamps to OFF\n");
	CHECK(tilt_angle_deg_for_level(99) == TILT_ANGLE_DEG[TILT_LEVELS - 1], "an over-range level clamps to Deep\n");
	CHECK(tilt_angle_for_level(0) == 0.0f, "level 0 is exactly 0 rad — the identity, bitwise\n");
	for (int i = 0; i < TILT_LEVELS; i++)
		NEAR(tilt_angle_for_level(i), (double)TILT_ANGLE_DEG[i] * 0.017453292519943295, 1e-7,
		     "level %d deg->rad\n", i);
	// the ladder feeds the tween in DEGREES and the view in RADIANS; prove the pair agrees
	for (int i = 0; i < TILT_LEVELS; i++) {
		TiltView v; tilt_view_init(&v, tilt_angle_for_level(i), VW, VH, TILT_COVER_MIX);
		NEAR(v.angle, tilt_angle_deg_for_level(i) * D2R, 1e-7, "level %d view angle\n", i);
	}
}

// --------------------------------------------------------------------------------------------
// TEST 17 — SLICE T2: the VERTEX the renderer actually emits (SPEC-render R1.7 / R6.7 / R5.3).
// tilt_mesh_base (main.c) composes this module with the shared screen fit and writes
// (x, y, iq, u, v, rgba) — and that composition is pure arithmetic, so it is host-checkable even
// though the draw is not. `calc_xform` is replicated verbatim from main.c:709-715 (it is not
// host-compilable); render_game's flat blit computes the SAME rect with the same expressions
// (main.c:1306-1310), which is what makes part (a) below a statement about PHASE.md invariant 1.
// --------------------------------------------------------------------------------------------
#define WARP_COLS 15    // main.c:988-990 — the 16x11 grid at 16 px frame-space spacing
#define WARP_ROWS 10

static void calc_xform_ref(int mode, float sW, float sH, float* ox, float* oy, float* sx, float* sy) {
	if (mode == 0)      { *sx = *sy = 1.0f; }                                  // SCALE_1X
	else if (mode == 2) { *sx = sW / VW; *sy = sH / VH; }                      // SCALE_STRETCH
	else { float f = (sW / VW < sH / VH) ? sW / VW : sH / VH; *sx = *sy = f; } // SCALE_FIT
	*ox = (sW - VW * *sx) / 2.0f;
	*oy = (sH - VH * *sy) / 2.0f;
}

typedef struct { float x, y, iq, u, v; } RefVert;   // the emitted half of TiltVert (R5.3)

static void tilt_mesh_ref(RefVert* out, const TiltView* tv, float ox, float oy, float sx, float sy) {
	for (int r = 0; r <= WARP_ROWS; r++) for (int c = 0; c <= WARP_COLS; c++) {
		RefVert* w = &out[r * (WARP_COLS + 1) + c];
		float gx = (float)(c * 16), gy = (float)(r * 16);
		float fx, fy, q;
		tilt_project(tv, gx, gy, &fx, &fy, &q);
		w->x = ox + fx * sx; w->y = oy + fy * sy; w->iq = 1.0f / q;
		w->u = gx / 256.0f;  w->v = 1.0f - gy / 256.0f;
	}
}

static void test_mesh(void) {
	printf("TEST 17: the emitted screen-space mesh — invariant 1 in screen space + the spill (T2)\n");
	static RefVert m[(WARP_COLS + 1) * (WARP_ROWS + 1)];
	struct Screen { float w, h; const char* n; };
	static const struct Screen SC[] = { { 400.0f, 240.0f, "top" }, { 320.0f, 240.0f, "bottom" } };

	// (a) ANGLE 0 => the mesh lands EXACTLY on the rect render_game's flat blit draws, bitwise,
	//     for every scale mode and both screens. This is the geometric half of PHASE.md invariant 1
	//     (the other half — that the flat path is not even entered — is the `td == NULL` branch).
	for (unsigned s = 0; s < sizeof SC / sizeof SC[0]; s++) for (int mode = 0; mode < 3; mode++) {
		float ox, oy, sx, sy; calc_xform_ref(mode, SC[s].w, SC[s].h, &ox, &oy, &sx, &sy);
		TiltView v; tilt_view_init(&v, 0.0f, VW, VH, TILT_COVER_MIX);
		tilt_mesh_ref(m, &v, ox, oy, sx, sy);
		int bad = 0;
		for (int r = 0; r <= WARP_ROWS; r++) for (int c = 0; c <= WARP_COLS; c++) {
			const RefVert* w = &m[r * (WARP_COLS + 1) + c];
			float gx = (float)(c * 16), gy = (float)(r * 16);
			if (w->x != ox + gx * sx || w->y != oy + gy * sy) bad++;
			if (w->iq != 1.0f) bad++;
			if (w->u != gx / 256.0f || w->v != 1.0f - gy / 256.0f) bad++;
		}
		CHECK(bad == 0, "%s mode %d: %d angle-0 vertices differ from the flat rect (must be bitwise 0)\n",
		      SC[s].n, mode, bad);
	}

	// (b) EVERY shipped angle => the mesh still spans exactly the flat rect VERTICALLY (R2.5.1's
	//     zero vertical crop, carried through the screen fit). Top row -> oy, bottom row -> oy+160*sy.
	static const float DEG[] = { 0.0f, 10.0f, 15.0f, 20.0f };
	for (unsigned s = 0; s < sizeof SC / sizeof SC[0]; s++) for (int mode = 0; mode < 3; mode++)
		for (unsigned a = 0; a < sizeof DEG / sizeof DEG[0]; a++) {
			float ox, oy, sx, sy; calc_xform_ref(mode, SC[s].w, SC[s].h, &ox, &oy, &sx, &sy);
			TiltView v; tilt_view_init(&v, DEG[a] * D2R, VW, VH, TILT_COVER_MIX);
			tilt_mesh_ref(m, &v, ox, oy, sx, sy);
			NEAR(m[0].y, oy, 1e-3, "%s mode %d a=%.0f: top row off the frame rect\n",
			     SC[s].n, mode, (double)DEG[a]);
			NEAR(m[WARP_ROWS * (WARP_COLS + 1)].y, oy + VH * sy, 1e-3,
			     "%s mode %d a=%.0f: bottom row off the frame rect\n", SC[s].n, mode, (double)DEG[a]);
		}

	// (c) THE SPILL, in screen pixels — the numbers behind slice T2's "no scissor" decision
	//     (R3.6.2). Top screen, Aspect-fit (sx = sy = 1.5, ox = 20, oy = 0): the R1.8 corner table
	//     composed with calc_xform. The near row leaves the frame rect (x < ox) — at 10 deg it stops
	//     at screen x = 1.35, i.e. it eats the whole 20 px letterbox pillar; at 15/20 deg it runs off
	//     the screen entirely (-10.5 / -24.3) and the viewport clips it. The far row falls SHORT of
	//     the rect by the R2.5.2 wedge, which the C2D_TargetClear background fills.
	{
		float ox, oy, sx, sy; calc_xform_ref(1, 400.0f, 240.0f, &ox, &oy, &sx, &sy);
		CHECK(ox == 20.0f && oy == 0.0f && sx == 1.5f && sy == 1.5f,
		      "top Aspect-fit is the ox=20 sx=1.5 rect the spec's tables assume\n");
		struct Sp { float deg, farLeft, nearLeft; };
		static const struct Sp SP[] = {   // 20 + fx*1.5 with fx from R1.8
			{ 10.0f, 20.0f + 8.728425f * 1.5f,  20.0f + -12.430812f * 1.5f },
			{ 15.0f, 20.0f + 11.843810f * 1.5f, 20.0f + -20.310093f * 1.5f },
			{ 20.0f, 20.0f + 14.136881f * 1.5f, 20.0f + -29.539547f * 1.5f },
		};
		for (unsigned i = 0; i < sizeof SP / sizeof SP[0]; i++) {
			TiltView v; tilt_view_init(&v, SP[i].deg * D2R, VW, VH, TILT_COVER_MIX);
			tilt_mesh_ref(m, &v, ox, oy, sx, sy);
			NEAR(m[0].x, SP[i].farLeft, 1e-3, "a=%.0f far-row left edge (screen px)\n", (double)SP[i].deg);
			NEAR(m[WARP_ROWS * (WARP_COLS + 1)].x, SP[i].nearLeft, 1e-3,
			     "a=%.0f near-row left edge (screen px)\n", (double)SP[i].deg);
			CHECK(m[0].x > ox, "a=%.0f: the far row must fall INSIDE the rect (the wedge)\n", (double)SP[i].deg);
			CHECK(m[WARP_ROWS * (WARP_COLS + 1)].x < ox,
			      "a=%.0f: the near row must spill OUT of the frame rect (%.3f px vs ox %.1f) — R3.6.2\n",
			      (double)SP[i].deg, (double)m[WARP_ROWS * (WARP_COLS + 1)].x, (double)ox);
		}
	}

	// (d) THE HOMOGENEOUS w. Emitting q instead of 1/q is the one inversion that still looks
	//     plausible (R1.3: up to 31 px of error at 15 deg), so pin its shape: iq strictly DECREASES
	//     down the mesh, iq(far) > 1 > iq(near), and the centre row is exactly 1.
	for (unsigned a = 1; a < sizeof DEG / sizeof DEG[0]; a++) {
		float ox, oy, sx, sy; calc_xform_ref(1, 400.0f, 240.0f, &ox, &oy, &sx, &sy);
		TiltView v; tilt_view_init(&v, DEG[a] * D2R, VW, VH, TILT_COVER_MIX);
		tilt_mesh_ref(m, &v, ox, oy, sx, sy);
		for (int r = 1; r <= WARP_ROWS; r++)
			CHECK(m[r * (WARP_COLS + 1)].iq < m[(r - 1) * (WARP_COLS + 1)].iq,
			      "a=%.0f row %d: iq must decrease toward the viewer\n", (double)DEG[a], r);
		for (int c = 0; c <= WARP_COLS; c++)   // constant along a row: q depends on cy only
			CHECK(m[c].iq == m[0].iq, "a=%.0f: iq varies along the far row at col %d\n", (double)DEG[a], c);
		CHECK(m[0].iq > 1.0f, "a=%.0f: the far row is FARTHER (iq %.6f)\n", (double)DEG[a], (double)m[0].iq);
		CHECK(m[WARP_ROWS * (WARP_COLS + 1)].iq < 1.0f, "a=%.0f: the near row is NEARER\n", (double)DEG[a]);
		NEAR(m[5 * (WARP_COLS + 1)].iq, 1.0f, 1e-6, "a=%.0f: the centre row sits at w=1\n", (double)DEG[a]);
	}

	// (e) the tilt moves POSITIONS, never texcoords: the source rect is the same at every angle.
	{
		float ox, oy, sx, sy; calc_xform_ref(1, 400.0f, 240.0f, &ox, &oy, &sx, &sy);
		static RefVert m0[(WARP_COLS + 1) * (WARP_ROWS + 1)];
		TiltView v0; tilt_view_init(&v0, 0.0f, VW, VH, TILT_COVER_MIX);
		tilt_mesh_ref(m0, &v0, ox, oy, sx, sy);
		for (unsigned a = 1; a < sizeof DEG / sizeof DEG[0]; a++) {
			TiltView v; tilt_view_init(&v, DEG[a] * D2R, VW, VH, TILT_COVER_MIX);
			tilt_mesh_ref(m, &v, ox, oy, sx, sy);
			int bad = 0;
			for (int i = 0; i < (WARP_COLS + 1) * (WARP_ROWS + 1); i++)
				if (m[i].u != m0[i].u || m[i].v != m0[i].v) bad++;
			CHECK(bad == 0, "a=%.0f: %d texcoords moved (the UVs must be angle-independent)\n",
			      (double)DEG[a], bad);
		}
	}
}

// --------------------------------------------------------------------------------------------
// TEST 18 — FIX PASS (2026-08-04, review finding 2): the SECOND-EYE SLAB CONTRACT.
// main.c's right-eye draw no longer re-projects its 176 vertices: it clones the left eye's slab
// and rewrites only the colour (`TiltDraw.clone`), and when the dim tint matches it draws straight
// out of the left eye's slab. That is only sound because the two eyes share a TiltView, a scale
// mode and a screen rect — this test pins exactly that premise, and pins that it is NOT vacuous
// (change the mode and the geometry really does move, so a clone across modes would be a bug).
// tilt_mesh_base / tilt_mesh_recolor live in main.c and are not host-compilable, so both are
// replicated here verbatim from main.c:1152-1200 the same way calc_xform_ref replicates the fit.
// --------------------------------------------------------------------------------------------
typedef struct { float x, y, iq, u, v, r, g, b, a; } RefVertC;

static void tilt_mesh_base_ref(RefVertC* out, const TiltView* tv, unsigned mod,
                               float ox, float oy, float sx, float sy) {
	const float mr = (float)( mod        & 0xFF) / 255.0f,   // C2D_Color32 packs r,g,b,a low->high
	            mg = (float)((mod >>  8) & 0xFF) / 255.0f,
	            mb = (float)((mod >> 16) & 0xFF) / 255.0f,
	            ma = (float)((mod >> 24) & 0xFF) / 255.0f;
	for (int r = 0; r <= WARP_ROWS; r++) for (int c = 0; c <= WARP_COLS; c++) {
		RefVertC* w = &out[r * (WARP_COLS + 1) + c];
		float gx = (float)(c * 16), gy = (float)(r * 16);
		float fx, fy, q;
		tilt_project(tv, gx, gy, &fx, &fy, &q);
		w->x = ox + fx * sx; w->y = oy + fy * sy; w->iq = 1.0f / q;
		w->u = gx / 256.0f;  w->v = 1.0f - gy / 256.0f;
		w->r = mr; w->g = mg; w->b = mb; w->a = ma;
	}
}

static void tilt_mesh_recolor_ref(RefVertC* dst, const RefVertC* src, unsigned mod) {
	const float mr = (float)( mod        & 0xFF) / 255.0f,
	            mg = (float)((mod >>  8) & 0xFF) / 255.0f,
	            mb = (float)((mod >> 16) & 0xFF) / 255.0f,
	            ma = (float)((mod >> 24) & 0xFF) / 255.0f;
	for (int i = 0; i < (WARP_COLS + 1) * (WARP_ROWS + 1); i++) {
		dst[i].x = src[i].x; dst[i].y = src[i].y; dst[i].iq = src[i].iq;
		dst[i].u = src[i].u; dst[i].v = src[i].v;
		dst[i].r = mr; dst[i].g = mg; dst[i].b = mb; dst[i].a = ma;
	}
}

static void test_second_eye_slab(void) {
	printf("TEST 18: the second-eye slab contract — clone+recolour == rebuild (fix-pass finding 2)\n");
	enum { NV = (WARP_COLS + 1) * (WARP_ROWS + 1) };
	static RefVertC left[NV], cloned[NV], rebuilt[NV];
	static const float DEG18[] = { 0.0f, 10.0f, 15.0f, 20.0f };
	static const unsigned MOD[] = { 0xFFFFFFFFu, 0xFF808080u };   // focused white / unfocused dim
	// (a) the substitution main.c actually performs: clone the left slab, rewrite the colour, and
	//     the result must be BITWISE what a fresh build with that colour would have produced.
	for (unsigned a = 0; a < sizeof DEG18 / sizeof DEG18[0]; a++)
		for (unsigned mi = 0; mi < sizeof MOD / sizeof MOD[0]; mi++)
			for (unsigned mj = 0; mj < sizeof MOD / sizeof MOD[0]; mj++) {
				float ox, oy, sx, sy; calc_xform_ref(1, 400.0f, 240.0f, &ox, &oy, &sx, &sy);
				TiltView v; tilt_view_init(&v, DEG18[a] * D2R, VW, VH, TILT_COVER_MIX);
				tilt_mesh_base_ref(left, &v, MOD[mi], ox, oy, sx, sy);       // left eye (built)
				tilt_mesh_recolor_ref(cloned, left, MOD[mj]);                // right eye (cloned)
				tilt_mesh_base_ref(rebuilt, &v, MOD[mj], ox, oy, sx, sy);    // right eye (as before)
				int bad = 0;
				for (int i = 0; i < NV; i++)
					if (memcmp(&cloned[i], &rebuilt[i], sizeof(RefVertC)) != 0) bad++;
				CHECK(bad == 0, "a=%.0f mod %08X->%08X: %d cloned vertices differ from a rebuild\n",
				      (double)DEG18[a], MOD[mi], MOD[mj], bad);
			}
	// (b) same tint => the slabs are byte-identical, which is what licenses main.c drawing the
	//     LEFT slab a second time for the right eye (no write, no GSPGPU_FlushDataCache).
	for (unsigned a = 0; a < sizeof DEG18 / sizeof DEG18[0]; a++) {
		float ox, oy, sx, sy; calc_xform_ref(1, 400.0f, 240.0f, &ox, &oy, &sx, &sy);
		TiltView v; tilt_view_init(&v, DEG18[a] * D2R, VW, VH, TILT_COVER_MIX);
		tilt_mesh_base_ref(left,    &v, 0xFFFFFFFFu, ox, oy, sx, sy);
		tilt_mesh_base_ref(rebuilt, &v, 0xFFFFFFFFu, ox, oy, sx, sy);
		CHECK(memcmp(left, rebuilt, sizeof left) == 0,
		      "a=%.0f: two builds of the same view+mod must be byte-identical\n", (double)DEG18[a]);
	}
	// (c) the premise is NOT vacuous — geometry depends on the scale mode and the screen rect, so
	//     the clone is only ever legal between draws that share both. (Aspect-fit vs 1:1 on top.)
	{
		TiltView v; tilt_view_init(&v, 15.0f * D2R, VW, VH, TILT_COVER_MIX);
		float ox, oy, sx, sy;   calc_xform_ref(1, 400.0f, 240.0f, &ox, &oy, &sx, &sy);
		float ox2, oy2, sx2, sy2; calc_xform_ref(0, 400.0f, 240.0f, &ox2, &oy2, &sx2, &sy2);
		tilt_mesh_base_ref(left,    &v, 0xFFFFFFFFu, ox,  oy,  sx,  sy);
		tilt_mesh_base_ref(rebuilt, &v, 0xFFFFFFFFu, ox2, oy2, sx2, sy2);
		CHECK(memcmp(left, rebuilt, sizeof left) != 0,
		      "the mode/rect really does move the mesh — cloning across modes would be wrong\n");
	}
}

// --------------------------------------------------------------------------------------------
// TEST 19 — FIX PASS (2026-08-04, review finding 5): the PAUSE-MENU -> TOUCH-ON transition.
// main.c used to feed G9 from `tmEff`, a snapshot taken at the top of the frame (main.c:2057),
// while the TOUCH tab's Smart/Pad preview buttons set touchMode AND clear menuOpen later in that
// SAME frame (main.c:2624-2628). For exactly one frame the gate then saw menuOpen=0 with
// touchActive=0 — G3 released, G9 not yet armed — reopened, and tilt_tween_step RETARGETED the
// bottom screen upward, restarting the descent clock with smart touch already live over it.
// The gate is a pure function, so the whole sequence is host-testable: feed it the two ways and
// demand that the LIVE feed never returns nonzero and never lets the angle rise.
// --------------------------------------------------------------------------------------------
static void test_touch_transition(void) {
	printf("TEST 19: pause-menu -> touch-on must never re-open the bottom gate (fix-pass finding 5)\n");
	const float dt = 1000.0f / 60.0f;            // one 60 fps frame
	const int   openFrames = 9;                  // ~150 ms of menu before the preview tap
	for (int stale = 0; stale <= 1; stale++) {
		TiltTween tw; tilt_tween_reset(&tw);
		TiltGateIn in = gate_clear(1, 3);        // bottom screen, level 3 (Deep), everything clear
		// settle at the full angle first
		for (int f = 0; f < 30; f++) tilt_tween_step(&tw, tilt_target_level(&in), tilt_angle_deg_for_level(tilt_target_level(&in)), dt);
		CHECK(tw.ang == TILT_ANGLE_DEG[3], "%s: the bottom screen settled before the menu opened\n",
		      stale ? "stale" : "live");
		// the pause menu opens (G3) and the descent starts
		in.menuOpen = 1;
		for (int f = 0; f < openFrames; f++) { int lv = tilt_target_level(&in);
			tilt_tween_step(&tw, lv, tilt_angle_deg_for_level(lv), dt); }
		float residual = tw.ang;
		CHECK(residual > 0.0f && residual < TILT_ANGLE_DEG[3],
		      "%s: mid-descent when the user taps the preview button (%.2f deg)\n",
		      stale ? "stale" : "live", (double)residual);
		// THE FRAME: the preview button sets touchMode = SMART and closes the menu. `stale` models
		// the old tmEff snapshot (touchActive still 0 on this frame); `live` models the fix.
		in.menuOpen = 0;
		in.touchActive = stale ? 0 : 1;
		int lvNow = tilt_target_level(&in);
		if (stale) CHECK(lvNow == 3, "stale: the frame-top snapshot DOES re-open the gate (the bug)\n");
		else       CHECK(lvNow == 0, "live: the live touchMode keeps the gate shut on that frame\n");
		tilt_tween_step(&tw, lvNow, tilt_angle_deg_for_level(lvNow), dt);
		if (stale) CHECK(tw.ang > residual, "stale: ...and the angle climbs back up (%.2f -> %.2f)\n",
		                 (double)residual, (double)tw.ang);
		else       CHECK(tw.ang < residual, "live: ...and the angle keeps unwinding (%.2f -> %.2f)\n",
		                 (double)residual, (double)tw.ang);
		// from here on touch is live in both models; only the LIVE feed is monotone, and only it
		// lands flat within the ORIGINAL 250 ms window (the stale retarget restarts the clock).
		in.touchActive = 1;
		float prev = tw.ang; int rose = 0;
		int framesToFlat = -1;
		for (int f = 0; f < 40; f++) {
			int lv = tilt_target_level(&in);
			CHECK(lv == 0, "%s: G9 must hold the target at 0 for every frame after touch is on\n",
			      stale ? "stale" : "live");
			tilt_tween_step(&tw, lv, tilt_angle_deg_for_level(lv), dt);
			if (tw.ang > prev) rose++;
			prev = tw.ang;
			if (framesToFlat < 0 && tw.ang == 0.0f) framesToFlat = f + 1;
		}
		CHECK(rose == 0, "%s: the angle never rises once touch is on (%d frames rose)\n",
		      stale ? "stale" : "live", rose);
		CHECK(tw.ang == 0.0f && tilt_active(&tw) == 0,
		      "%s: the bottom screen ends EXACTLY flat (%.9f)\n", stale ? "stale" : "live", (double)tw.ang);
		// TILT_TWEEN_MS from the MENU OPENING is the honest bound for the live feed: the descent
		// started at G3, so only (250 - menu-open time) is left when touch arrives. 250/16.67 = 15
		// frames total, 9 already spent => <= 6 more. The stale feed needs a fresh 15.
		if (!stale) CHECK(framesToFlat >= 0 && framesToFlat <= 6,
		                  "live: flat within the ORIGINAL 250 ms window (%d frames left)\n", framesToFlat);
		else        CHECK(framesToFlat > 6,
		                  "stale: the retarget really did restart the clock (%d frames)\n", framesToFlat);
	}
}

int main(void) {
	printf("=== test_tilt: phase-14 HD-2D diorama tilt — projection (SPEC-render R1/R2/R6)"
	       " + gate/tween (SPEC-integration §1-§3) ===\n");
	test_gate();
	test_gate_screens();
	test_touch_invariant();
	test_tween();
	test_active();
	test_settings_and_tier();
	test_identity();
	test_corners();
	test_vfit();
	test_gen1_reference();
	test_growth();
	test_roundtrip();
	test_coverage();
	test_sanity();
	test_disp();
	test_ladder();
	test_mesh();
	test_second_eye_slab();
	test_touch_transition();
	printf("=== %d checks, %d failures ===\n", g_checks, g_fail);
	return g_fail ? 1 : 0;
}
