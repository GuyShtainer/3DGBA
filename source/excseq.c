// excseq.c — the excursion leg machine, lifted out of touch.c unchanged (phase 25, lane C2).
// Same transcription rule as progseq.c: same order, same budgets, same edge cases. The one thing
// worth staring at is the settle protocol, because its ORDER is load-bearing —
//
//   1. the dims history is updated on EVERY waiting frame, before anything is decided (that is
//      what makes "stable" mean "stable across two consecutive checks");
//   2. the cadence is checked against pendFrames BEFORE it is incremented;
//   3. pendFrames only ticks on a frame that did NOT commit a leg.
//
// touch.c's original expressed all three as one `if (settled && cadence && walk_plan(...)) {...}
// else if (++pendFrames > BUDGET) reset();` — the short-circuit is why the BFS is not attempted on
// a frame that is not due, and excseq_settle_try / _planned / _wait keep exactly that shape.
#include <string.h>
#include "excseq.h"

void excseq_reset(ExcSeq* s) {
	if (!s) return;
	memset(s, 0, sizeof *s);
	s->homeG = s->homeN = -1;
	s->curG  = s->curN  = -1;
	s->pendG = s->pendN = -1;
	s->lastW = s->lastH = -1;
}

// PHASE 27 (lane R). THE ONE INVARIANT THAT WAS MISSING: **a re-arm carries no dims history.**
//
// `lastW/lastH` only mean something as the FIRST of the two consecutive reads the stability rule
// compares. They are written on every waiting frame and then left alone while the leg is walked —
// so at the moment a leg is re-armed they hold dims that were true on a map the machine has since
// left. `excseq_settle_try` then compares the ARRIVAL map's dims against that fossil, and if the
// two happen to agree it calls the world "stable" on the first frame it looks at, which is the very
// thing the stability rule exists to forbid.
//
// It was not hypothetical on either re-arm site:
//   * `excseq_boundary` — pinned by test_excseq TEST 6 (lane C2) and then seen LIVE in the phase-25
//     P4 excursion as `planSeq 4 -> routeEnd 6 (MAPCHANGE) -> planSeq 5`: leg 2 planned on the
//     Pokemon Center's 14x9 the moment the town's 20x20 was still loading, and the follow loop's
//     layout check had to throw that BFS away.
//   * `excseq_layout_kill` — the same fossil, reached the other way. touch.c kills a route when
//     `ptr != s_mapPtr || w != s_mapW || h != s_mapH`, and the pointer half exists precisely because
//     gBackupMapLayout is a FIXED EWRAM buffer, so a SAME-SIZE map swap changes only the pointer.
//     On that path the stale dims match the new map's exactly, "stable" is true on frame 0 of the
//     re-arm, and the retry plans on a half-built world — the identical defect with no test on it.
// Both sites now forget, so a re-armed leg must OBSERVE the world twice before it believes it.
static void exc_forget_dims(ExcSeq* s) { s->lastW = s->lastH = -1; }

void excseq_arm(ExcSeq* s, int mapG, int mapN, int goalX, int goalY) {
	if (!s) return;
	s->on = 1; s->leg = 0;
	s->homeG = mapG; s->homeN = mapN;
	s->curG  = mapG; s->curN  = mapN;
	s->goalX = goalX; s->goalY = goalY;
}

int excseq_boundary_due(const ExcSeq* s, int mapG, int mapN) {
	if (!s) return 0;
	return s->on && !s->pend && (mapG != s->curG || mapN != s->curN);
}

int excseq_boundary(ExcSeq* s, int mapG, int mapN, int dG, int dN, int wjX, int wjY) {
	if (!s || !s->on) return EXC_B_NONE;
	int wantG, wantN, gx, gy;
	if (s->leg == 0)      { wantG = dG;       wantN = dN;       gx = wjX;      gy = wjY; }
	else if (s->leg == 1) { wantG = s->homeG; wantN = s->homeN; gx = s->goalX; gy = s->goalY; }
	else                  { excseq_reset(s); return EXC_B_RESET; }   // leg 2 ends at the goal
	if (mapG != wantG || mapN != wantN) { excseq_reset(s); return EXC_B_RESET; }  // not the map we predicted
	s->leg++;
	// ARM the leg; the follower plans it once the new map is really loaded. The door drops the
	// avatar on (or one step off) the arrival tile; the walk plan starts from where the player
	// REALLY is, so a +-1 arrival needs no tolerance rule here.
	s->pendG = wantG; s->pendN = wantN; s->pendX = gx; s->pendY = gy;
	s->pend = 1; s->pendFrames = 0;
	exc_forget_dims(s);                  // phase 27: the previous leg's dims are not evidence here
	return EXC_B_ARMED;
}

int excseq_settle_try(ExcSeq* s, int mapG, int mapN, int px, int haveMap, int mw, int mh) {
	if (!s) return EXC_S_WAIT;
	// "Settled" is NOT just the location matching. SaveBlock1.location flips when the warp starts;
	// gBackupMapLayout is rebuilt later, and a plan made in between is drawn on the PREVIOUS map's
	// grid — it can even succeed, and then the follow loop's own layout check kills it as a
	// MAPCHANGE one frame later (observed: leg 2 planned on the Pokemon Center's 14x9 grid the
	// moment the town's 20x20 was still loading). So require the DIMENSIONS to be stable across two
	// checks as well; the layout stops moving exactly when the load ends.
	int stable = haveMap && mw == s->lastW && mh == s->lastH;
	s->lastW = haveMap ? mw : -1; s->lastH = haveMap ? mh : -1;
	int settled = (mapG == s->pendG && mapN == s->pendN && px >= 0 && stable);
	return (settled && (s->pendFrames % EXC_SETTLE_EVERY) == 0) ? EXC_S_PLAN : EXC_S_WAIT;
}

void excseq_settle_planned(ExcSeq* s, int mapG, int mapN) {
	if (!s) return;
	s->pend = 0; s->pendFrames = 0;
	s->curG = mapG; s->curN = mapN;      // the leg is walked on THIS map now
}

int excseq_settle_wait(ExcSeq* s) {
	if (!s) return 0;
	if (++s->pendFrames > EXC_SETTLE_BUDGET) { excseq_reset(s); return 1; }
	return 0;
}

int excseq_home_done(ExcSeq* s, int walking) {
	if (!s) return 0;
	if (s->on && !s->pend && !walking && s->leg >= 2) { excseq_reset(s); return 1; }
	return 0;
}

void excseq_layout_kill(ExcSeq* s) {
	if (!s || !s->on) return;
	s->pend = 1; s->pendFrames = 0;
	exc_forget_dims(s);                  // phase 27: same rule — a re-arm re-observes (same-size swap)
}
