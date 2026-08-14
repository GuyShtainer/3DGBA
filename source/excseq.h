#pragma once
// excseq.h — THE EXCURSION LEG MACHINE, extracted pure (phase 25, lane C2).
//
// The other half of audit finding O2. A cross-map route is not one plan, it is a PROGRAM of legs
// (SPEC-family-traversal §3):
//
//   leg 0  current map  -> the OUT warp (Wi)
//   leg 1  the interior -> the RETURN warp (Wj)          [re-planned live on arrival]
//   leg 2  current map  -> the tapped goal               [re-planned live on arrival]
//
// and three of phase 24's fixes are in the SUPERVISOR that notices a leg ended and gets the next
// one planned — none of which needs to read the game:
//
//   * `a1cbdca` the boundary WATCHER. A leg boundary is a map change, and it must be seen whether
//     or not a route is still being followed: the last leg's terminal is a STEP warp, so the
//     walker finishes the route (walking = false) and only THEN does the warp fire.
//   * `e1c6041` ARM, don't plan. A Gen-3 warp writes SaveBlock1.location when it STARTS; the
//     fade, the map load and the avatar placement all come after. Planning on the boundary frame
//     reads a half-built world and threw the whole excursion away one door short.
//   * `2fa4976` the layout-STABLE rule. Matching the location is not the same as the world being
//     there: gBackupMapLayout is rebuilt later still, so a leg is only planned once the map's
//     DIMENSIONS have stopped moving under it — and a route the layout check kills while an
//     excursion is live re-arms that leg instead of discarding it.
//
// touch.c keeps every read (SaveBlock1.location, gBackupMapLayout) and the BFS; this file decides.
// PURE-C per CLAUDE.md rule #4: nothing but <stdint.h>/<string.h>.
#include <stdint.h>

#define EXC_SETTLE_EVERY  8      // re-try cadence (a BFS per frame on the render thread is not free)
#define EXC_SETTLE_BUDGET 240    // ~4 s at 60 fps: a fade + map load, generously

typedef struct {
	int on;                  // an excursion is live
	int leg;                 // 0 = out, 1 = interior, 2 = home
	int homeG, homeN;        // where the tap happened, and where leg 2 walks to the goal
	int curG, curN;          // the map the CURRENT leg is being walked on — the reference a leg
	                         //   boundary is detected against
	int goalX, goalY;        // the tapped goal, kept for leg 2
	int pend;                // a leg is ARMED and waiting for its map to finish loading
	int pendG, pendN;        // ...the map it must be walked on
	int pendX, pendY;        // ...and the tile it walks to
	int pendFrames;
	int lastW, lastH;        // last backup-layout dims seen while waiting (the stability rule)
} ExcSeq;

// excseq_boundary()
enum { EXC_B_NONE = 0, EXC_B_ARMED, EXC_B_RESET };
// excseq_settle_try()
enum { EXC_S_WAIT = 0, EXC_S_PLAN };

void excseq_reset(ExcSeq* s);

// A fresh excursion, armed by exc_plan once leg 0's ordinary walk route is planned.
void excseq_arm(ExcSeq* s, int mapG, int mapN, int goalX, int goalY);

// THE WATCHER. True when the live map is not the one the current leg is being walked on — i.e. a
// warp fired, whether or not a route was still in flight. Deliberately NOT gated on `walking`.
int excseq_boundary_due(const ExcSeq* s, int mapG, int mapN);

// A leg ended because SaveBlock1.location changed. Verify we are where the plan SAID we would be,
// then ARM the next leg (the follower plans it once the world answers). `dG/dN/wjX/wjY` are the
// excursion plan's D-map and its return warp — leg 1's target; leg 2's is the home map and the
// tapped goal, both of which this struct already holds.
// Returns EXC_B_ARMED (leg advanced, `leg` is the new one) or EXC_B_RESET (not the map we
// predicted, or the excursion is over — the machine is now off).
int excseq_boundary(ExcSeq* s, int mapG, int mapN, int dG, int dN, int wjX, int wjY);

// One frame of "is the world ready for the armed leg yet". ALWAYS updates the dims history.
// Returns EXC_S_PLAN when the caller should attempt the walk_plan for (pendX,pendY) right now.
int excseq_settle_try(ExcSeq* s, int mapG, int mapN, int px, int haveMap, int mw, int mh);

// ...and the two answers to it.
void excseq_settle_planned(ExcSeq* s, int mapG, int mapN);   // the BFS succeeded: the leg is live
int  excseq_settle_wait(ExcSeq* s);                          // it did not: tick, 1 = gave up (reset)

// The last leg's route ending IS the end of the excursion — otherwise the machine (and its
// "VIA DOOR - LEG 3/3" chip) would linger until some later map change happened to clear it.
// Returns 1 if it reset.
int excseq_home_done(ExcSeq* s, int walking);

// The follow loop's layout check killed the route while an excursion is live: recoverable, because
// the leg's target is still held. Re-arm it rather than throw away a route two doors along.
void excseq_layout_kill(ExcSeq* s);
