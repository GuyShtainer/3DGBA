// test_excseq.c — PC host unit test for source/excseq.c, the EXCURSION LEG MACHINE.
//
//   clang -std=c11 -Wall -Wextra -O2 -I source test/host/test_excseq.c source/excseq.c \
//         -o /tmp/tes && /tmp/tes
//
// WHY THIS SUITE EXISTS. The other half of phase-24 audit finding O2: `e1c6041` (plan the next leg
// when the world has loaded, not when the warp fires), `a1cbdca` (watch the leg boundary OUTSIDE
// the follow loop) and `2fa4976` (a leg must be planned on the FINISHED map) were all emulator-only
// fixes in touch.c with no regression barrier at all. Each cost one full P4 excursion run to find.
//
// The three defects are three different ways of believing a warp too early, and the fixtures below
// are the OBSERVED sequences, not invented ones — every number in TEST 3 and TEST 6 comes from the
// live runs quoted in the commit messages:
//
//   * leg 0 warped into Lavaridge's Pokemon Center; the interior leg was planned on the boundary
//     frame and reported FP_OUT_UNREACHABLE for a back door four tiles away (e1c6041);
//   * the last leg's terminal is a STEP warp: the walker arrives, declares the route finished, and
//     only THEN does the warp fire — so a watcher inside the follow loop never sees it (a1cbdca);
//   * leg 2 was planned on the Pokemon Center's 14x9 grid while the town's 20x20 was still
//     loading, and the follow loop killed it as a MAPCHANGE one frame later (2fa4976).
//
// TEST 2 is written as an ANTI-TEST: it runs the OLD rule (a boundary is only noticed while a
// route is being followed) against the same STEP-warp trace and shows it misses the boundary. If
// that ever stops failing, this suite has stopped testing anything.
//
// PHASE 27 (lane R). This suite found a FOURTH defect while covering the first three — the dims
// history survived a leg boundary, so leg 2's first frame was settled by the previous map's
// dimensions and planned on the wrong grid. Lane C2 pinned it (behaviour-preserving extraction);
// it is now FIXED in `excseq_boundary` AND in `excseq_layout_kill` (the same-size-swap path, TEST
// 7), and TESTs 6 and 7 assert the fixed behaviour instead of the residue.
//
// TEST NUMBERING
//   TEST 1  arm / boundary / the map the plan predicted (and what a wrong map does)
//   TEST 2  the STEP-warp watcher, vs the old in-the-follow-loop rule            fix a1cbdca
//   TEST 3  ARM, don't plan: the boundary frame never plans                      fix e1c6041
//   TEST 4  the layout-STABLE rule, on the observed 14x9 -> 20x20 sequence       fix 2fa4976
//   TEST 5  the retry cadence and the give-up budget
//   TEST 6  the whole P4 excursion, leg by leg
//   TEST 7  layout_kill re-arms; home_done ends it; and neither fires early
//   TEST 7b what the phase-27 dims fix does NOT cure — measured live, pinned as a defect
//   TEST 8  invariants over an exhaustive sweep of map/dim/px inputs
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "excseq.h"

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

// The real maps of the P4 run, so the traces below read like the mirror did.
// Lavaridge Town = map group 0, num 12 (the fixture save's own location); its Pokemon Center is
// group 4 (INDOOR_LAVARIDGE), num 5 — as the live run's `map (4,5) pos (7,8)` line reported.
#define TOWN_G 0
#define TOWN_N 12
#define PC_G   4
#define PC_N   5

// The follower's half of the protocol, exactly as touch.c wires it: try, then plan, then either
// commit or wait. `planOk` is what the BFS would have answered.
static int follow(ExcSeq* s, int mapG, int mapN, int px, int haveMap, int mw, int mh, int planOk) {
	int r = excseq_settle_try(s, mapG, mapN, px, haveMap, mw, mh);
	if (r == EXC_S_PLAN && planOk) { excseq_settle_planned(s, mapG, mapN); return 1; }
	excseq_settle_wait(s);
	return 0;
}
// ...and the same thing when we want to know whether the BFS was even ATTEMPTED.
static int would_plan(ExcSeq* s, int mapG, int mapN, int px, int haveMap, int mw, int mh) {
	return excseq_settle_try(s, mapG, mapN, px, haveMap, mw, mh) == EXC_S_PLAN;
}

// ---------------------------------------------------------------------------------------------
// TEST 1 — arm, and the boundary's map contract
// ---------------------------------------------------------------------------------------------
static void test_arm_boundary(void) {
	ExcSeq s; excseq_reset(&s);
	CHECK(!s.on && s.leg == 0 && !s.pend, "a reset machine is off");
	CHECK(!excseq_boundary_due(&s, 99, 99), "an off machine never reports a boundary");

	excseq_arm(&s, TOWN_G, TOWN_N, 5, 5);
	CHECK(s.on && s.leg == 0, "armed on leg 0");
	CHECK(s.homeG == TOWN_G && s.homeN == TOWN_N, "home is where the tap happened");
	CHECK(s.curG == TOWN_G && s.curN == TOWN_N, "and leg 0 is walked on that same map");
	CHECK(s.goalX == 5 && s.goalY == 5, "the tapped goal is kept for leg 2");
	CHECK(!excseq_boundary_due(&s, TOWN_G, TOWN_N), "standing still is not a boundary");
	CHECK(excseq_boundary_due(&s, PC_G, PC_N), "the map changing IS one");

	// Leg 0 must land on the excursion plan's D map. Anything else — a wrong door, a script warp,
	// a Fly — ends the excursion where it stands (H3.5: never a wander).
	ExcSeq w = s;
	int r = excseq_boundary(&w, 3, 7, PC_G, PC_N, 2, 1);
	CHECK(r == EXC_B_RESET && !w.on, "a map we did not predict ends the excursion");

	r = excseq_boundary(&s, PC_G, PC_N, PC_G, PC_N, 2, 1);
	CHECK(r == EXC_B_ARMED && s.leg == 1, "the predicted D map advances to leg 1");
	CHECK(s.pend == 1 && s.pendG == PC_G && s.pendN == PC_N, "...armed, on the map it must be walked on");
	CHECK(s.pendX == 2 && s.pendY == 1, "...toward the RETURN warp Wj");
	CHECK(s.pendFrames == 0, "with a fresh settle clock");

	// Leg 1 comes home: the target is the tapped goal, on the home map.
	excseq_settle_planned(&s, PC_G, PC_N);
	r = excseq_boundary(&s, TOWN_G, TOWN_N, PC_G, PC_N, 2, 1);
	CHECK(r == EXC_B_ARMED && s.leg == 2, "back on the home map -> leg 2");
	CHECK(s.pendX == 5 && s.pendY == 5, "...toward the tile the player actually tapped");
	CHECK(s.pendG == TOWN_G && s.pendN == TOWN_N, "...on the home map");
	// Leg 2 ends at the goal, not at a warp: a further map change is the end.
	excseq_settle_planned(&s, TOWN_G, TOWN_N);
	r = excseq_boundary(&s, 9, 9, PC_G, PC_N, 2, 1);
	CHECK(r == EXC_B_RESET && !s.on, "a map change during the LAST leg ends the excursion");
	// A boundary on an off machine is a no-op, not a leg.
	r = excseq_boundary(&s, TOWN_G, TOWN_N, PC_G, PC_N, 2, 1);
	CHECK(r == EXC_B_NONE && !s.on, "and it stays off");
}

// ---------------------------------------------------------------------------------------------
// TEST 2 — the STEP-warp watcher (fix a1cbdca), with the old rule as the anti-test
// ---------------------------------------------------------------------------------------------
// "That works for a DOOR terminal (the walker is still holding the direction when the warp fires)
// but not for the back door, which fieldpath classifies as a STEP warp: the walker arrives on the
// tile, declares the route finished — s_walking = false — and the warp fires a frame later, with
// nobody watching. So the excursion never learned it had come home."
static void test_step_warp_watcher(void) {
	// The trace: 3 frames walking inside the PC, the route ends (walking 0), and the warp fires on
	// the NEXT frame. `walking` is the follow loop's own flag.
	const int walkTrace[6] = { 1, 1, 1, 0, 0, 0 };
	const int mapTrace [6] = { 0, 0, 0, 0, 1, 1 };   // 1 = the town (the warp fired at frame 4)

	// THE SHIPPED RULE: the watcher sits above the follow loop and does not consult `walking`.
	ExcSeq s; excseq_reset(&s);
	excseq_arm(&s, TOWN_G, TOWN_N, 5, 5);
	excseq_boundary(&s, PC_G, PC_N, PC_G, PC_N, 2, 1);      // leg 0 -> inside the PC
	excseq_settle_planned(&s, PC_G, PC_N);
	int seen = -1;
	for (int f = 0; f < 6; f++) {
		int g = mapTrace[f] ? TOWN_G : PC_G, n = mapTrace[f] ? TOWN_N : PC_N;
		if (excseq_boundary_due(&s, g, n)) { seen = f; break; }
	}
	CHECK(seen == 4, "the STEP warp is noticed on the frame it fires (frame %d)", seen);

	// THE OLD RULE, restated: `if (walking) { ...if (map changed) exc_leg_boundary(); }`.
	int oldSeen = -1;
	for (int f = 0; f < 6; f++) {
		if (!walkTrace[f]) continue;                        // behind `if (!s_walking) return`
		if (mapTrace[f]) { oldSeen = f; break; }
	}
	CHECK(oldSeen == -1, "THE ANTI-TEST: the pre-a1cbdca watcher never sees it — the route ended first");

	// A DOOR terminal (the walker is still holding the direction) is seen by BOTH rules — which is
	// why the defect survived two runs.
	const int doorWalk[4] = { 1, 1, 1, 1 };
	const int doorMap [4] = { 0, 0, 1, 1 };
	int newSeen = -1, oldSeen2 = -1;
	ExcSeq d; excseq_reset(&d); excseq_arm(&d, TOWN_G, TOWN_N, 5, 5);
	for (int f = 0; f < 4; f++) {
		int g = doorMap[f] ? PC_G : TOWN_G, n = doorMap[f] ? PC_N : TOWN_N;
		if (newSeen < 0 && excseq_boundary_due(&d, g, n)) newSeen = f;
		if (oldSeen2 < 0 && doorWalk[f] && doorMap[f]) oldSeen2 = f;
	}
	CHECK(newSeen == 2 && oldSeen2 == 2, "a DOOR warp is seen by both rules (that is why this hid)");

	// ...and the watcher is deliberately silent while a leg is PENDING: the map "changing" during a
	// load must not be read as a second boundary.
	ExcSeq p; excseq_reset(&p); excseq_arm(&p, TOWN_G, TOWN_N, 5, 5);
	excseq_boundary(&p, PC_G, PC_N, PC_G, PC_N, 2, 1);
	CHECK(p.pend == 1, "a leg is armed");
	CHECK(!excseq_boundary_due(&p, 77, 77), "no boundary is reported while one is still pending");
}

// ---------------------------------------------------------------------------------------------
// TEST 3 — ARM, don't plan (fix e1c6041)
// ---------------------------------------------------------------------------------------------
// "A Gen-3 warp writes the location when it STARTS — the fade, the map load and the avatar
// placement all come after — so that plan reads a half-built gBackupMapLayout, fails, and
// exc_reset() throws the whole excursion away one door short."
static void test_arm_not_plan(void) {
	ExcSeq s; excseq_reset(&s);
	excseq_arm(&s, TOWN_G, TOWN_N, 5, 5);
	int r = excseq_boundary(&s, PC_G, PC_N, PC_G, PC_N, 2, 1);
	CHECK(r == EXC_B_ARMED, "the boundary ARMS the next leg");
	CHECK(s.pend == 1, "...and only arms it: nothing is planned on the boundary frame");
	// The very first settle attempt cannot plan either: the dims history is empty, so nothing is
	// "stable" yet. That is what stops a plan being drawn on the map we just left.
	CHECK(!would_plan(&s, PC_G, PC_N, 7, 1, 14, 9),
	      "the first frame after the warp never plans — no two readings agree yet");
	// A leg whose map never becomes the predicted one is never planned, however long we wait.
	int planned = 0;
	for (int f = 0; f < 100; f++) planned += follow(&s, TOWN_G, TOWN_N, 7, 1, 20, 20, 1);
	CHECK(planned == 0, "and a leg is NEVER planned on a map other than the one it was armed for");
	CHECK(s.on, "...while the budget lasts, the excursion is still alive");
}

// ---------------------------------------------------------------------------------------------
// TEST 4 — the layout-STABLE rule (fix 2fa4976), on the observed sequence
// ---------------------------------------------------------------------------------------------
// "the mirror said it exactly: plan seq=5 goal=(5,5) PLANNED, routeEnd=6 (MAPCHANGE), behaviour
// 0x0C, which is not what (5,5) reads on the town map. The leg had been drawn on the Pokemon
// Center's 14x9 grid."
static void test_layout_stable(void) {
	ExcSeq s; excseq_reset(&s);
	excseq_arm(&s, TOWN_G, TOWN_N, 5, 5);
	excseq_boundary(&s, PC_G, PC_N, PC_G, PC_N, 2, 1);     // leg 1, inside the PC
	excseq_settle_planned(&s, PC_G, PC_N);
	excseq_boundary(&s, TOWN_G, TOWN_N, PC_G, PC_N, 2, 1); // leg 2: out the back door, home

	// The observed frames: SaveBlock1.location is ALREADY the town while gBackupMapLayout still
	// reads the Pokemon Center's 14x9, then flips to the town's 20x20. Driven as touch.c drives it
	// — one frame at a time, try/plan/wait — so the CADENCE is in the picture too: the first
	// attempt is frame 0, and after that only every EXC_SETTLE_EVERY-th frame may run a BFS.
	int planned = -1, unstableTries = 0;
	for (int f = 0; f < 24 && planned < 0; f++) {
		int mw = (f < 4) ? 14 : 20, mh = (f < 4) ? 9 : 20;      // the layout arrives at frame 4
		int stableNow = (f > 0) && ((f < 4) || (f > 4));         // what "two agreeing reads" means here
		int p = would_plan(&s, TOWN_G, TOWN_N, 9, 1, mw, mh);
		if (p && !stableNow) unstableTries++;
		if (p) { planned = f; excseq_settle_planned(&s, TOWN_G, TOWN_N); }
		else excseq_settle_wait(&s);
	}
	CHECK(unstableTries == 0, "no plan is ever attempted on a layout that is still moving");
	CHECK(planned == EXC_SETTLE_EVERY,
	      "the leg is planned on the first cadence frame after the town's grid settles (frame %d)", planned);
	CHECK(planned > 4, "...which is AFTER the 14x9 window that 2fa4976's run planned inside of");

	// An unreadable map (map_read failed mid-load) resets the history rather than pretending.
	ExcSeq u; excseq_reset(&u);
	excseq_arm(&u, TOWN_G, TOWN_N, 5, 5);
	excseq_boundary(&u, PC_G, PC_N, PC_G, PC_N, 2, 1);
	would_plan(&u, PC_G, PC_N, 7, 1, 14, 9);
	CHECK(!would_plan(&u, PC_G, PC_N, 7, 0, 14, 9), "a failed map read is never 'stable'");
	CHECK(u.lastW == -1 && u.lastH == -1, "...and it clears the history it would have agreed with");
	CHECK(!would_plan(&u, PC_G, PC_N, 7, 1, 14, 9), "so the next good read has nothing to agree with yet");

	// px < 0 (no loaded overworld: the fade is still up) is never settled either.
	ExcSeq f; excseq_reset(&f);
	excseq_arm(&f, TOWN_G, TOWN_N, 5, 5);
	excseq_boundary(&f, PC_G, PC_N, PC_G, PC_N, 2, 1);
	would_plan(&f, PC_G, PC_N, -1, 1, 14, 9);
	CHECK(!would_plan(&f, PC_G, PC_N, -1, 1, 14, 9), "a mid-fade frame (px < 0) never plans");
}

// ---------------------------------------------------------------------------------------------
// TEST 5 — the retry cadence and the give-up budget
// ---------------------------------------------------------------------------------------------
static void test_cadence_budget(void) {
	ExcSeq s; excseq_reset(&s);
	excseq_arm(&s, TOWN_G, TOWN_N, 5, 5);
	excseq_boundary(&s, PC_G, PC_N, PC_G, PC_N, 2, 1);
	// A settled world where the BFS keeps failing (an NPC camped in the doorway): the attempt is
	// made on a CADENCE, because each one is a whole-window flood on the render thread.
	int attempts = 0, frames = 0;
	while (s.on && frames < EXC_SETTLE_BUDGET + 10) {
		attempts += follow(&s, PC_G, PC_N, 7, 1, 14, 9, 0);
		frames++;
	}
	CHECK(!s.on, "a leg that can never be planned gives up");
	CHECK(frames == EXC_SETTLE_BUDGET + 1, "at the budget (%d frames)", frames);
	CHECK(attempts == 0, "and it committed nothing");

	// The cadence itself: with the world settled from frame 0, a plan is attempted every 8th frame.
	ExcSeq c; excseq_reset(&c);
	excseq_arm(&c, TOWN_G, TOWN_N, 5, 5);
	excseq_boundary(&c, PC_G, PC_N, PC_G, PC_N, 2, 1);
	would_plan(&c, PC_G, PC_N, 7, 1, 14, 9);      // prime the dims history
	int tries = 0, seen[40], nSeen = 0;
	for (int f = 0; f < 40; f++) {
		if (would_plan(&c, PC_G, PC_N, 7, 1, 14, 9)) { tries++; if (nSeen < 40) seen[nSeen++] = c.pendFrames; }
		excseq_settle_wait(&c);
	}
	CHECK(tries == 5, "5 attempts in 40 frames — one every %d (%d)", EXC_SETTLE_EVERY, tries);
	for (int i = 0; i < nSeen; i++)
		CHECK(seen[i] % EXC_SETTLE_EVERY == 0, "attempt %d landed on the cadence (pendFrames %d)", i, seen[i]);

	// A success stops the clock and re-bases the leg's map.
	ExcSeq k; excseq_reset(&k);
	excseq_arm(&k, TOWN_G, TOWN_N, 5, 5);
	excseq_boundary(&k, PC_G, PC_N, PC_G, PC_N, 2, 1);
	would_plan(&k, PC_G, PC_N, 7, 1, 14, 9);
	CHECK(follow(&k, PC_G, PC_N, 7, 1, 14, 9, 1) == 1, "a settled world with a working BFS plans the leg");
	CHECK(!k.pend && k.pendFrames == 0, "the leg is no longer pending");
	CHECK(k.curG == PC_G && k.curN == PC_N, "and the boundary reference is now the map it walks on");
	CHECK(k.on && k.leg == 1, "the excursion is still live, on leg 1");
}

// ---------------------------------------------------------------------------------------------
// TEST 6 — the whole P4 excursion, leg by leg
// ---------------------------------------------------------------------------------------------
// The live run (phase 24, take 3): "legs 0 and 1 ran (front door -> across the Pokemon Center ->
// out the back door onto the terrace at (9,2)), the new boundary watcher armed leg 2 ... and it
// went all the way home."
static void test_p4_excursion(void) {
	ExcSeq s; excseq_reset(&s);
	excseq_arm(&s, TOWN_G, TOWN_N, 5, 5);              // the tap: a goal across the building
	CHECK(s.leg == 0, "leg 0: walk to the Pokemon Center's front door");

	// --- the front door fires (a DOOR terminal: the walker was still holding the direction) ---
	CHECK(excseq_boundary_due(&s, PC_G, PC_N), "the door warp is a boundary");
	CHECK(excseq_boundary(&s, PC_G, PC_N, PC_G, PC_N, 2, 1) == EXC_B_ARMED, "leg 1 is armed");
	// The world arrives over several frames: location first (frame 0), then the layout, then the
	// avatar. Frames 0-1 are mid-fade — nothing is readable at all.
	int planned = 0, first = -1;
	for (int f = 0; f < 40 && !planned; f++) {
		int fading = (f < 2);
		int got = follow(&s, PC_G, PC_N, fading ? -1 : 7, fading ? 0 : 1, 14, 9, 1);
		if (got) { planned = 1; first = f; }
		else CHECK(s.pend == 1, "frame %d: still armed, nothing planned yet", f);
	}
	CHECK(planned == 1, "leg 1 is planned once the interior is really there");
	CHECK(first == EXC_SETTLE_EVERY, "on the first cadence frame after the fade (frame %d)", first);
	CHECK(s.leg == 1 && s.curG == PC_G && s.curN == PC_N, "and it is walked on the PC's map");

	// --- the back door: a STEP warp, so the route ENDS before the warp fires ---
	CHECK(!excseq_home_done(&s, 1), "a live route is not the end of the excursion");
	CHECK(!excseq_home_done(&s, 0), "...and neither is a finished one on leg 1");
	CHECK(s.on, "the machine survives the end of leg 1's route");
	CHECK(excseq_boundary_due(&s, TOWN_G, TOWN_N), "the STEP warp home is a boundary (a1cbdca)");
	CHECK(excseq_boundary(&s, TOWN_G, TOWN_N, PC_G, PC_N, 2, 1) == EXC_B_ARMED, "leg 2 is armed");
	CHECK(s.pendX == 5 && s.pendY == 5, "toward the tile the player tapped");

	// --- the town loads under the leg: the 2fa4976 sequence ---
	//
	// THE RESIDUE THIS SUITE FOUND, NOW FIXED (phase 27 lane R). Lane C2 pinned it as a defect
	// rather than hiding it: `lastW/lastH` were not cleared by a leg boundary, and nothing updates
	// them while a leg is being WALKED — so the first frame of leg 2 compared the town's dims
	// against the 14x9 the PREVIOUS leg's settle had recorded, and when those agreed it called the
	// world "stable" on the first frame it looked at and planned on the old grid. It cost one
	// wasted BFS on every excursion (`planSeq 4 -> routeEnd 6 -> planSeq 5` in the live phase-25 P4).
	// `excseq_boundary` now forgets the dims, so an armed leg must OBSERVE the world twice.
	// This loop is the same trace as before; only the expected answer moved.
	CHECK(s.lastW == -1 && s.lastH == -1, "THE FIX: arming a leg forgets the previous leg's dims");
	planned = 0; first = -1;
	int wastedOnOldGrid = 0;
	for (int f = 0; f < 40 && !planned; f++) {
		int mw = (f < 6) ? 14 : 20, mh = (f < 6) ? 9 : 20;     // the PC's grid is still up until 6
		int got = follow(&s, TOWN_G, TOWN_N, 9, 1, mw, mh, 1);
		if (got && f < 6) wastedOnOldGrid = 1;
		if (got) { planned = 1; first = f; }
	}
	CHECK(planned == 1, "leg 2 is planned");
	CHECK(wastedOnOldGrid == 0, "and NOT on the Pokemon Center's grid — that plan no longer happens");
	CHECK(first == EXC_SETTLE_EVERY,
	      "...it waits for the town's own dims to hold still, then plans on the first cadence frame (frame %d)", first);

	// The recovery path is still exercised, because the follow loop's layout check can still fire
	// for a reason the boundary never saw (a script warp, a same-size swap): the leg re-arms and the
	// retry plans on the FINISHED map.
	excseq_layout_kill(&s);
	CHECK(s.lastW == -1 && s.lastH == -1, "THE FIX, other site: a layout kill forgets the dims too");
	CHECK(s.pend == 1 && s.leg == 2, "the layout kill re-armed leg 2 instead of dropping it");
	planned = 0; first = -1;
	for (int f = 0; f < 40 && !planned; f++) {
		int got = follow(&s, TOWN_G, TOWN_N, 9, 1, 20, 20, 1);
		if (got) { planned = 1; first = f; }
		else CHECK(s.pend == 1, "leg 2 retry frame %d: still armed", f);
	}
	CHECK(planned == 1, "leg 2 is re-planned on the town's own 20x20 grid");
	CHECK(first == EXC_SETTLE_EVERY, "...on the first cadence frame once the dims agree (frame %d)", first);
	CHECK(s.leg == 2 && s.curG == TOWN_G, "on leg 2, walking the home map");

	// --- the last leg's route ends AT THE GOAL: that is the end of the excursion ---
	CHECK(excseq_home_done(&s, 0) == 1, "the last leg's route ending ends the excursion");
	CHECK(!s.on && s.leg == 0, "the machine is off and clean");
	CHECK(!excseq_boundary_due(&s, PC_G, PC_N), "and a later map change is nobody's business");
}

// ---------------------------------------------------------------------------------------------
// TEST 7 — layout_kill re-arms, home_done does not fire early
// ---------------------------------------------------------------------------------------------
static void test_kill_and_home(void) {
	ExcSeq s; excseq_reset(&s);
	excseq_arm(&s, TOWN_G, TOWN_N, 5, 5);
	excseq_boundary(&s, PC_G, PC_N, PC_G, PC_N, 2, 1);
	would_plan(&s, PC_G, PC_N, 7, 1, 14, 9);
	follow(&s, PC_G, PC_N, 7, 1, 14, 9, 1);
	CHECK(!s.pend, "the leg is live");
	int keepX = s.pendX, keepY = s.pendY;
	// The follow loop's layout check kills the route a frame later — recoverable, because the leg's
	// target is still held.
	excseq_layout_kill(&s);
	CHECK(s.pend == 1 && s.pendFrames == 0, "a layout kill RE-ARMS the same leg");
	CHECK(s.pendX == keepX && s.pendY == keepY, "...with the same target it was aiming at");
	CHECK(s.on && s.leg == 1, "...and does not throw away an excursion that is two doors along");

	// PHASE 27 — THE SAME-SIZE SWAP, which is why the layout kill forgets the dims as well.
	// touch.c kills a route on `ptr != s_mapPtr || w != s_mapW || h != s_mapH`; the POINTER half is
	// there because gBackupMapLayout is a fixed EWRAM buffer, so a map swap between two 14x9 rooms
	// changes nothing but the pointer. With the dims history carried across the re-arm, the retry
	// compared 14x9 against the 14x9 it had recorded on the map it just LEFT, called that stable,
	// and planned on frame 0 of a world that was still loading — the TEST 6 defect by another door.
	CHECK(s.lastW == -1 && s.lastH == -1, "a layout kill forgets the dims history");
	CHECK(!would_plan(&s, PC_G, PC_N, 7, 1, 14, 9),
	      "so the first frame after a SAME-SIZE swap is not settled...");
	CHECK(would_plan(&s, PC_G, PC_N, 7, 1, 14, 9),
	      "...and only the second read of the same dims is");
	// On a dead machine it is a no-op (touch.c calls it unconditionally from the follow loop).
	excseq_reset(&s);
	excseq_layout_kill(&s);
	CHECK(!s.on && !s.pend, "a layout kill on an off machine arms nothing");

	// home_done: only on the LAST leg, only with no route in flight, only with nothing pending.
	struct { int leg, walking, pend, want; } row[] = {
		{ 0, 0, 0, 0 }, { 1, 0, 0, 0 }, { 2, 1, 0, 0 }, { 2, 0, 1, 0 }, { 2, 0, 0, 1 },
	};
	for (int i = 0; i < 5; i++) {
		ExcSeq h; excseq_reset(&h);
		excseq_arm(&h, TOWN_G, TOWN_N, 5, 5);
		h.leg = row[i].leg; h.pend = row[i].pend;
		CHECK(excseq_home_done(&h, row[i].walking) == row[i].want,
		      "home_done(leg %d, walking %d, pend %d) == %d", row[i].leg, row[i].walking, row[i].pend, row[i].want);
	}
}

// ---------------------------------------------------------------------------------------------
// TEST 7b — WHAT THE PHASE-27 FIX DOES *NOT* CURE, measured live and pinned here (lane R)
// ---------------------------------------------------------------------------------------------
// Forgetting the dims at a re-arm removes one failure mode: "settled on frame 0 by a fossil".
// It does NOT remove the OTHER one, and the live P4 on the fixed build says so out loud —
// `planSeq 4 -> routeEnd 6 (MAPCHANGE) -> planSeq 5` still happened, byte for byte the sequence
// lane C2 recorded before the fix (final planSeq 5, endSeq 3, arrived (5,5) beh 0x28).
//
// The reason, and it is a property of the RULE, not a bug in it: "stable" means the dimensions
// did not move between two reads, and a map the game has NOT started rebuilding yet does not move
// either. After the STEP warp home, gBackupMapLayout keeps the Pokemon Center's 14x9 through the
// whole fade — far longer than EXC_SETTLE_EVERY — so the two reads agree ON THE OLD MAP, the leg
// plans on it, and the follow loop's layout check kills the BFS when the town's 20x20 finally
// lands. TEST 6's fixture (old dims for 6 frames) is the case the fix covers; this is the case
// the live warp actually produces.
//
// So this test asserts the DEFECT, deliberately, the way lane C2 asserted the one it could not
// take: if someone fixes it, this goes red and gets rewritten. The candidate fix is stated in
// LANE-R-REACH.md — refuse to settle until the layout has been observed to CHANGE since the
// boundary, with a frame-grace escape so two same-size rooms (a change that is invisible in the
// dims) do not hang the leg until EXC_SETTLE_BUDGET. It needs its own live P4 to grade, which is
// why this lane measured it and did not guess at it.
static void test_slow_layout_residue(void) {
	ExcSeq s; excseq_reset(&s);
	excseq_arm(&s, TOWN_G, TOWN_N, 5, 5);
	excseq_boundary(&s, PC_G, PC_N, PC_G, PC_N, 2, 1);
	follow(&s, PC_G, PC_N, 7, 1, 14, 9, 0);              // leg 1 lives on the PC's grid
	excseq_settle_planned(&s, PC_G, PC_N);
	CHECK(excseq_boundary(&s, TOWN_G, TOWN_N, PC_G, PC_N, 2, 1) == EXC_B_ARMED, "leg 2 armed by the STEP warp home");
	CHECK(s.lastW == -1, "with no dims history, per the phase-27 fix");

	// THE REAL TRACE: the PC's 14x9 outlives the settle cadence, then the town arrives.
	int planned = 0, first = -1, dimsAtPlan = 0;
	for (int f = 0; f < 60 && !planned; f++) {
		int mw = (f < 30) ? 14 : 20, mh = (f < 30) ? 9 : 20;
		if (follow(&s, TOWN_G, TOWN_N, 9, 1, mw, mh, 1)) { planned = 1; first = f; dimsAtPlan = mw; }
	}
	CHECK(planned == 1, "leg 2 is planned");
	CHECK(first == EXC_SETTLE_EVERY, "on the first cadence frame (frame %d) — the fix cannot see further", first);
	CHECK(dimsAtPlan == 14, "AND IT IS STILL THE OLD MAP'S GRID: a map that has not started loading "
	                        "does not move either, so 'stable' is satisfied by the PC sitting still");
	// ...and the recovery is the one the live run takes: the layout check kills it, the leg
	// re-arms, and the retry lands on the town.
	excseq_layout_kill(&s);
	planned = 0; first = -1; dimsAtPlan = 0;
	for (int f = 0; f < 60 && !planned; f++)
		if (follow(&s, TOWN_G, TOWN_N, 9, 1, 20, 20, 1)) { planned = 1; first = f; dimsAtPlan = 20; }
	CHECK(planned == 1 && dimsAtPlan == 20, "the retry plans on the town's own grid");
	CHECK(first == EXC_SETTLE_EVERY, "one wasted BFS later (frame %d) — the live cost, still there", first);
}

// ---------------------------------------------------------------------------------------------
// TEST 8 — invariants over an exhaustive sweep
// ---------------------------------------------------------------------------------------------
static void test_invariants(void) {
	int cases = 0, planWrongMap = 0, planUnstable = 0, planNoPlayer = 0, planWhileWalkingLeg = 0;
	for (int mapSame = 0; mapSame <= 1; mapSame++)
	for (int px = -1; px <= 1; px++)
	for (int haveMap = 0; haveMap <= 1; haveMap++)
	for (int w1 = 8; w1 <= 10; w1++)
	for (int h1 = 8; h1 <= 10; h1++)
	for (int w2 = 8; w2 <= 10; w2++)
	for (int h2 = 8; h2 <= 10; h2++) {
		ExcSeq s; excseq_reset(&s);
		excseq_arm(&s, TOWN_G, TOWN_N, 5, 5);
		excseq_boundary(&s, PC_G, PC_N, PC_G, PC_N, 2, 1);
		int g = mapSame ? PC_G : TOWN_G, n = mapSame ? PC_N : TOWN_N;
		would_plan(&s, g, n, px, haveMap, w1, h1);
		int p = would_plan(&s, g, n, px, haveMap, w2, h2);
		cases++;
		if (p && !mapSame) planWrongMap++;
		if (p && !(haveMap && w1 == w2 && h1 == h2)) planUnstable++;
		if (p && px < 0) planNoPlayer++;
		if (s.pend == 0) planWhileWalkingLeg++;      // nothing here may clear `pend` on its own
	}
	CHECK(cases == 2 * 3 * 2 * 3 * 3 * 3 * 3, "the sweep really ran (%d cases)", cases);
	CHECK(planWrongMap == 0, "a leg is NEVER planned on a map it was not armed for");
	CHECK(planUnstable == 0, "...nor on a layout whose dimensions are still moving");
	CHECK(planNoPlayer == 0, "...nor before the avatar exists");
	CHECK(planWhileWalkingLeg == 0, "and only the caller's success can un-arm a leg");
}

// ---------------------------------------------------------------------------------------------
// TEST 9 — the two safety rules, stated over every map in a small neighbourhood
// ---------------------------------------------------------------------------------------------
// H3.5: "Any other map, or a leg that cannot be re-planned live, ends the excursion where it
// stands: visible, honest, recoverable. Never a wander." Both halves of that are single
// comparisons, and a single comparison is exactly the kind of thing that survives a refactor with
// its operands swapped — so both are asserted over the whole neighbourhood rather than at a point.
static void test_neighbourhood(void) {
	// (a) the WATCHER: due iff the live map differs from the map the current leg walks on, and
	//     never while a leg is pending.
	for (int cg = 0; cg <= 3; cg++)
	for (int cn = 0; cn <= 3; cn++)
	for (int mg = 0; mg <= 3; mg++)
	for (int mn = 0; mn <= 3; mn++)
	for (int pend = 0; pend <= 1; pend++) {
		ExcSeq s; excseq_reset(&s);
		excseq_arm(&s, cg, cn, 5, 5);
		s.pend = pend;
		int want = (!pend && (mg != cg || mn != cn));
		CHECK(excseq_boundary_due(&s, mg, mn) == want,
		      "watcher: cur (%d,%d) live (%d,%d) pend %d -> %d", cg, cn, mg, mn, pend, want);
	}
	// (b) the BOUNDARY: leg 0 advances only on the excursion plan's D map, leg 1 only on home.
	for (int leg = 0; leg <= 1; leg++)
	for (int mg = 0; mg <= 3; mg++)
	for (int mn = 0; mn <= 3; mn++) {
		ExcSeq s; excseq_reset(&s);
		excseq_arm(&s, TOWN_G, TOWN_N, 5, 5);
		if (leg == 1) { excseq_boundary(&s, PC_G, PC_N, PC_G, PC_N, 2, 1); excseq_settle_planned(&s, PC_G, PC_N); }
		int wantG = leg ? TOWN_G : PC_G, wantN = leg ? TOWN_N : PC_N;
		int r = excseq_boundary(&s, mg, mn, PC_G, PC_N, 2, 1);
		int predicted = (mg == wantG && mn == wantN);
		CHECK(r == (predicted ? EXC_B_ARMED : EXC_B_RESET),
		      "leg %d landing on (%d,%d): %s", leg, mg, mn, predicted ? "ARMED" : "RESET");
		CHECK(s.on == predicted, "leg %d landing on (%d,%d): machine %s", leg, mg, mn,
		      predicted ? "lives" : "is off");
		if (predicted) CHECK(s.pendX == (leg ? 5 : 2) && s.pendY == (leg ? 5 : 1),
		                     "leg %d armed toward the right tile", leg);
	}
}

int main(void) {
	test_arm_boundary();
	test_step_warp_watcher();
	test_arm_not_plan();
	test_layout_stable();
	test_cadence_budget();
	test_p4_excursion();
	test_kill_and_home();
	test_slow_layout_residue();
	test_invariants();
	test_neighbourhood();
	printf("\n=== %d checks, %d failures ===\n", g_checks, g_fails);
	return g_fails ? 1 : 0;
}
