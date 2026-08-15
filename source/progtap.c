// progtap.c — PHASE 28 / lane X. The tap gate's decision, lifted OUT of touch.c line for line
// (phase-26 audit O3; the same move phase 25 made for progseq/excseq). Pure C: no libctru, no
// citro, no reads — see progtap.h for why it exists and what each observation is read from.
#include "progtap.h"

int progtap_gate(const PtObs* o) {
	if (!o) return PT_GATE_SHIPPED;

	// (1) The tap landed ON a waterfall — "take me up this". A tall fall's top is unreachable by a
	//     tap at all (the touch window is 5 tiles up; Ever Grande's fall is 8), so this gesture is
	//     the only one available. Refuse outright if the column will not resolve: the raw tile is
	//     the one thing that must never reach a router.
	if (o->goalIsWaterfall) {
		int d = o->topY - o->py;
		if (d < 0) d = -d;
		return (o->topY < 0 || d > o->whalf) ? PT_GATE_REFUSE : PT_GATE_FALL;
	}

	// (2) Mid-surf, aimed UPWARD, with Waterfall actually usable. The direction test is what keeps
	//     this off every ordinary water tap: the edge only ever climbs.
	//
	//     SCOPE, and why the broad form is correct (phase-26 audit O3 asked for this in writing).
	//     The justification given in phase 26 — "tier 0 wins with a WRONG answer, a swim up the
	//     fall" — only covers taps that involve a fall, while the rule fires for EVERY upward
	//     mid-surf tap once Waterfall is usable. Narrowing it would need to know whether any route
	//     to the goal crosses a fall, which is the search itself. What makes the broad form safe is
	//     that consulting the conditional planner first CANNOT change the answer when no fall is
	//     involved, because both of its refusals fall through to the shipped router at the call
	//     site (touch.c):
	//         FT_OUT_TIER0    -> prog_plan returns -1 -> the tap runs walk_plan, unchanged;
	//         declined/failed -> prog_plan returns  0 -> the tap runs walk_plan, unchanged.
	//     So the only differences are one extra BFS on that tap and the case where fieldtrav really
	//     does find a conditional edge — which is the feature. The one place the two routers
	//     genuinely disagree, a BLOCKED goal (fieldpath accepts it as a terminal, fieldtrav does
	//     not), also lands on `walk_plan`, so the plain walk-and-hold still wins.
	if (o->upward && o->surfing && o->waterfallUsable) return PT_GATE_FIRST;

	// (3) The boulder terminal. `fieldtrav_strength_tap` is the SAME function fieldtrav_plan uses to
	//     decide whether the edge exists, so the gate and the planner cannot disagree.
	if (o->strengthTap) return PT_GATE_FIRST;

	return PT_GATE_SHIPPED;
}

int progtap_retargeted_goal(int wfRetarget, int goalX, int goalY, int* gx, int* gy) {
	if (!wfRetarget || !gx || !gy) return 0;
	*gx = goalX; *gy = goalY;
	return 1;
}
