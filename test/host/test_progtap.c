// test_progtap.c — PC host unit test for source/progtap.c, THE TAP GATE.
//
//   clang -std=c11 -Wall -Wextra -O2 -I source test/host/test_progtap.c source/progtap.c \
//         -o /tmp/tpt && /tmp/tpt
//
// WHY THIS SUITE EXISTS. The phase-26 adversarial audit, finding O3:
//
//     "the tap gate is the riskiest new code in the phase and has ZERO regression cover ...
//      Uncovered: the 2/1/0/-1 classification, the `gate == -1` 'plan NOTHING' path (a tap that
//      used to do something now does nothing), the `prog_retargeted_goal()` fallback."
//
// That is phase-24 finding O2 ("no host suite compiles source/touch.c") re-opening two phases after
// phase 25 closed it for the sequencer. This suite closes it the same way phase 25 did: the pure
// decision was lifted into progtap.c line for line, and here it is graded against a model written
// from the RULE (what the game does), not from the implementation.
//
// The gate is a classifier, so the grading is exhaustive rather than anecdotal: TEST 1 sweeps the
// WHOLE input space (2^5 boolean combinations x a range of column heights x both signs of the
// window) against an independently written oracle. A classifier that is right on a hand-picked
// eight cases and wrong on the ninth is exactly what the audit was worried about.
//
// TEST NUMBERING
//   TEST 1  the whole 2/1/0/-1 truth table, swept, against an independent oracle
//   TEST 2  the REFUSE path in detail — the tap that must plan NOTHING
//   TEST 3  precedence: a fall tap outranks surf-upward, which outranks the boulder
//   TEST 4  the window edge, both signs (a fall exactly FP_WHALF away is still climbable)
//   TEST 5  the retarget fallback
//   TEST 6  NULL / defensive inputs
#include <stdio.h>
#include <string.h>
#include "progtap.h"

static int g_checks = 0, g_fails = 0;
#define CHECK(cond, ...) do { g_checks++; if (!(cond)) { g_fails++; \
	printf("  FAIL %s:%d — ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

#define WHALF 32          // fieldpath.h FP_WHALF — the value touch.c passes

// The ORACLE, written from the rule as stated in progtap.h, not from progtap.c:
//   * a tap ON a fall means "take me up it" — 2 if the ride's landing is inside the search window,
//     otherwise -1, because the raw fall tile must never reach a router;
//   * otherwise, a mid-surf tap aimed UP with Waterfall usable asks the conditional planner first;
//   * otherwise, a boulder the game would really prompt at asks it first;
//   * otherwise nothing changes.
static int oracle(const PtObs* o) {
	if (o->goalIsWaterfall) {
		if (o->topY < 0) return -1;
		int d = o->topY - o->py; if (d < 0) d = -d;
		return (d > o->whalf) ? -1 : 2;
	}
	if (o->upward && o->surfing && o->waterfallUsable) return 1;
	if (o->strengthTap) return 1;
	return 0;
}

int main(void) {
	printf("test_progtap — the phase-26 tap gate (audit O3)\n");

	// ================================================================ TEST 1
	printf("\nTEST 1 — the whole truth table, swept against an independent oracle\n");
	{
		int seen[4] = { 0, 0, 0, 0 };     // how many of each verdict the sweep produced
		static const int tops[] = { -1, 0, 5, 12, 40, 60, 100 };
		static const int pys[]  = { 0, 9, 13, 40, 68, 99 };
		for (unsigned mask = 0; mask < 32u; mask++) {
			for (unsigned ti = 0; ti < sizeof tops / sizeof tops[0]; ti++) {
				for (unsigned pi = 0; pi < sizeof pys / sizeof pys[0]; pi++) {
					PtObs o;
					o.goalIsWaterfall = (mask >> 0) & 1;
					o.upward          = (mask >> 1) & 1;
					o.surfing         = (mask >> 2) & 1;
					o.waterfallUsable = (mask >> 3) & 1;
					o.strengthTap     = (mask >> 4) & 1;
					o.topY = tops[ti];
					o.py   = pys[pi];
					o.whalf = WHALF;
					int got = progtap_gate(&o), want = oracle(&o);
					CHECK(got == want,
					      "mask=%02x top=%d py=%d -> %d, oracle %d", mask, o.topY, o.py, got, want);
					if (want >= 0 && want <= 2) seen[want]++; else seen[3]++;
				}
			}
		}
		// The sweep is worthless if it never reached a verdict — pin that all four appeared.
		CHECK(seen[0] > 0, "the sweep produced SHIPPED verdicts (%d)", seen[0]);
		CHECK(seen[1] > 0, "the sweep produced FIRST verdicts (%d)", seen[1]);
		CHECK(seen[2] > 0, "the sweep produced FALL verdicts (%d)", seen[2]);
		CHECK(seen[3] > 0, "the sweep produced REFUSE verdicts (%d)", seen[3]);
	}

	// ================================================================ TEST 2
	// THE PATH THE AUDIT NAMED: "a tap that used to do something now does nothing". Two ways a
	// column fails to resolve, and both must answer -1 rather than fall through to a router.
	printf("\nTEST 2 — REFUSE: the fall whose column will not resolve\n");
	{
		PtObs o; memset(&o, 0, sizeof o);
		o.goalIsWaterfall = 1; o.whalf = WHALF; o.py = 68;
		o.topY = -1;                                   // fieldtrav_waterfall_top gave up
		CHECK(progtap_gate(&o) == PT_GATE_REFUSE, "an unresolvable column refuses");
		o.topY = 68 - (WHALF + 1);                     // resolves, but outside the search window
		CHECK(progtap_gate(&o) == PT_GATE_REFUSE, "a landing outside the window refuses");
		o.topY = 68 + (WHALF + 1);                     // ...and the same distance the other way
		CHECK(progtap_gate(&o) == PT_GATE_REFUSE, "…in either direction");
		// A refusal must not be reachable by accident from the OTHER two branches: whatever else is
		// true, a tap that is not on a fall can never answer -1. (That is what makes -1 a statement
		// about waterfalls specifically, which is what the executor's caller assumes.)
		for (unsigned mask = 0; mask < 32u; mask++) {
			PtObs q; memset(&q, 0, sizeof q);
			q.goalIsWaterfall = 0; q.whalf = WHALF; q.py = 40; q.topY = -1;
			q.upward          = (mask >> 1) & 1;
			q.surfing         = (mask >> 2) & 1;
			q.waterfallUsable = (mask >> 3) & 1;
			q.strengthTap     = (mask >> 4) & 1;
			CHECK(progtap_gate(&q) != PT_GATE_REFUSE, "mask=%02x off a fall never refuses", mask);
		}
	}

	// ================================================================ TEST 3
	printf("\nTEST 3 — precedence: fall > surf-upward > boulder\n");
	{
		PtObs o; memset(&o, 0, sizeof o);
		o.whalf = WHALF; o.py = 13; o.topY = 9;
		o.goalIsWaterfall = 1; o.upward = 1; o.surfing = 1; o.waterfallUsable = 1; o.strengthTap = 1;
		CHECK(progtap_gate(&o) == PT_GATE_FALL, "a fall tap wins over everything else");
		o.goalIsWaterfall = 0;
		CHECK(progtap_gate(&o) == PT_GATE_FIRST, "…then surf-upward");
		o.upward = 0;
		CHECK(progtap_gate(&o) == PT_GATE_FIRST, "…then the boulder (strengthTap alone)");
		o.strengthTap = 0;
		CHECK(progtap_gate(&o) == PT_GATE_SHIPPED, "…and with none of them, the shipped order");
		// Each half of the surf-upward rule is load-bearing on its own — this is the anti-merge
		// assertion: a DOWNWARD mid-surf tap, and an upward tap on FOOT, both stay shipped.
		PtObs d = o; d.upward = 0; d.surfing = 1; d.waterfallUsable = 1;
		CHECK(progtap_gate(&d) == PT_GATE_SHIPPED, "downward mid-surf stays shipped");
		PtObs f = o; f.upward = 1; f.surfing = 0; f.waterfallUsable = 1;
		CHECK(progtap_gate(&f) == PT_GATE_SHIPPED, "upward on FOOT stays shipped");
		PtObs u = o; u.upward = 1; u.surfing = 1; u.waterfallUsable = 0;
		CHECK(progtap_gate(&u) == PT_GATE_SHIPPED, "upward mid-surf with the HM unusable stays shipped");
	}

	// ================================================================ TEST 4
	// The window edge, both signs. FP_WHALF is the BFS window half-edge, so a landing EXACTLY that
	// far away is still inside it — an off-by-one here would silently refuse the tallest legal fall.
	printf("\nTEST 4 — the window edge is inclusive, in both directions\n");
	{
		PtObs o; memset(&o, 0, sizeof o);
		o.goalIsWaterfall = 1; o.whalf = WHALF; o.py = 60;
		o.topY = 60 - WHALF;
		CHECK(progtap_gate(&o) == PT_GATE_FALL, "exactly FP_WHALF above is inside the window");
		o.topY = 60 + WHALF;
		CHECK(progtap_gate(&o) == PT_GATE_FALL, "exactly FP_WHALF below is too");
		o.topY = 60;
		CHECK(progtap_gate(&o) == PT_GATE_FALL, "a zero-distance landing is trivially inside");
		// The real Emerald numbers, measured off the user's cart: the two falls this project has
		// actually stood at. Ever Grande (0,8) x15..26 fall y60..67, landing y=59, tapped from the
		// pool at y=68; Route 114 (0,29) fall y10..12, landing y=9, tapped from (12,13).
		o.py = 68; o.topY = 59; CHECK(progtap_gate(&o) == PT_GATE_FALL, "EverGrande K=8 classifies FALL");
		o.py = 13; o.topY =  9; CHECK(progtap_gate(&o) == PT_GATE_FALL, "Route114 K=3 classifies FALL");
	}

	// ================================================================ TEST 5
	printf("\nTEST 5 — the retarget fallback\n");
	{
		int gx = -7, gy = -7;
		CHECK(progtap_retargeted_goal(0, 12, 9, &gx, &gy) == 0, "no retarget -> 0");
		CHECK(gx == -7 && gy == -7, "…and the caller's goal is left ALONE (it is still the raw tap)");
		CHECK(progtap_retargeted_goal(1, 12, 9, &gx, &gy) == 1, "a real retarget -> 1");
		CHECK(gx == 12 && gy == 9, "…and it hands back the goal fieldtrav resolved");
		CHECK(progtap_retargeted_goal(1, 12, 9, 0, &gy) == 0, "a NULL out pointer refuses");
		CHECK(progtap_retargeted_goal(1, 12, 9, &gx, 0) == 0, "…either one");
	}

	// ================================================================ TEST 6
	printf("\nTEST 6 — defensive inputs\n");
	{
		CHECK(progtap_gate(0) == PT_GATE_SHIPPED, "a NULL observation leaves the shipped order alone");
		PtObs o; memset(&o, 0, sizeof o);
		CHECK(progtap_gate(&o) == PT_GATE_SHIPPED, "an all-zero observation is the shipped order");
		// whalf 0 is not a configuration touch.c can produce, but the gate must not divide, index or
		// wrap on it — only a fall landing on the player's own row would then be climbable.
		o.goalIsWaterfall = 1; o.py = 5; o.topY = 5; o.whalf = 0;
		CHECK(progtap_gate(&o) == PT_GATE_FALL, "whalf=0 still admits a zero-distance landing");
		o.topY = 6;
		CHECK(progtap_gate(&o) == PT_GATE_REFUSE, "…and refuses anything further");
	}

	printf("\n%d checks, %d failures\n", g_checks, g_fails);
	return g_fails ? 1 : 0;
}
