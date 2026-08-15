// progtap.h — PHASE 28 / lane X: the TAP GATE, extracted from touch.c so a host suite can grade it.
//
// WHY THIS FILE EXISTS. The phase-24 adversarial audit, finding O2: "no host suite compiles
// source/touch.c". Phase 25 closed that for the interact sequencer by lifting the pure decision out
// of touch.c into `progseq.c` line for line; the phase-26 audit (O3) re-opened it for the phase-26
// tap gate, which is the riskiest new code in that phase and had ZERO regression cover — including
// its `gate == -1` path, where a tap that used to do something now deliberately does NOTHING.
// This is the same extraction, done the same way: touch.c keeps every READ (the map, the party, the
// avatar), this file keeps the DECISION, and there is exactly one copy of it.
//
// WHAT THE GATE IS FOR. The shipped tier order (dry walk first; conditional edges only once it has
// failed — SPEC-family-traversal H1.7) assumes something that is true for Cut, Rock Smash and Surf
// and FALSE for the two HMs phase 26 added: that when the dry router answers, its answer is right.
//
//   * WATERFALL. A waterfall metatile is collision 0 / elevation 1 — measured, 62 columns across
//     7 maps on the user's own Emerald ROM — i.e. indistinguishable from the ocean above and below
//     it to `fieldpath_enterable`. The frozen tier-0 router therefore plots a straight swim UP the
//     fall and calls it a route, and the game's forced movement (pokeemerald
//     src/field_player_avatar.c:160 sForcedMovementTestFuncs[14] -> :184 sForcedMovementFuncs[15]
//     ForcedMovement_PushedSouthByCurrent) flushes the player straight back down. Tier 0 does not
//     merely MISS the waterfall edge — it wins with a wrong answer.
//   * STRENGTH. Tapping a boulder already succeeds at tier 0, because fieldpath deliberately allows
//     a blocked GOAL as the terminal (fieldpath.h:146-152), so a Strength activation could never be
//     reached either. Here the route is IDENTICAL with or without the gate (walk to the tile beside
//     the boulder); only the TERMINAL differs — a directional hold versus the game's own prompt.
//
// LIVE NOTE (phase 28, lane X, emulator instance b, Route 114 (0,29)): the classification below is
// the half of phase 26 that WORKS. A tap on the fall at (12,12) from (12,13) was classified 2, the
// planner retargeted to (12,9) and reported PLANNED/FALLS. What is broken is downstream, in the
// EXECUTOR (see LANE-X-EXECUTE.md W1): its FACE step holds the direction key until the avatar faces
// the fall, and because a fall tile is enterable while surfing the hold SWIMS ONTO IT — the player
// oscillates (12,13)<->(12,12) and the A that follows never lands on an idle frame.
#ifndef PROGTAP_H
#define PROGTAP_H

// The gate's verdict. Deliberately the same integers touch.c used before the extraction, so the
// call site reads identically and a diff of that file is a one-line change.
enum {
	PT_GATE_REFUSE = -1,   // plan NOTHING (a waterfall tap whose column will not resolve)
	PT_GATE_SHIPPED = 0,   // shipped order, unchanged
	PT_GATE_FIRST  = 1,    // ask the conditional planner FIRST, ordinary fallback afterwards
	PT_GATE_FALL   = 2     // the tap landed ON a waterfall: ask first, and NEVER hand the raw tile
	                       // to a router afterwards (that tile is the flush)
};

// Everything the decision needs, and nothing it could read for itself. Each field names the read
// touch.c makes for it, so the extraction can be checked against the call site by eye.
typedef struct {
	int goalIsWaterfall;   // fieldtrav_is_waterfall(engine, fieldpath_behaviour_at(bus, m, gx, gy))
	int topY;              // fieldtrav_waterfall_top(bus, m, gx, gy)   (-1 = will not resolve)
	int py;                // the player's tile y
	int whalf;             // FP_WHALF — the BFS window half-edge the top must fall inside
	int upward;            // gy < py   (the tap aims UP the screen)
	int surfing;           // gPlayerAvatar bit 3
	int waterfallUsable;   // fieldtrav_usable(...) & (1 << FT_HM_WATERFALL)
	int strengthTap;       // fieldtrav_strength_tap(...) >= 0
} PtObs;

// CLASSIFY ONLY. It does not move the goal: the retarget has exactly one implementation, in
// `fieldtrav_plan`, which is what makes `FtProgram.wfRetarget` (and its `progRetarget` mirror) mean
// something on a live run — a gate that quietly pre-retargeted would leave the planner with nothing
// to report. Cost discipline is part of the contract: the caller fills the cheap fields first and
// only walks the party for `waterfallUsable`/`strengthTap` when the earlier tests leave it open.
int progtap_gate(const PtObs* o);

// The goal the LAST fieldtrav_plan actually resolved, and 1 only when it really moved it. The tap
// handler needs it for one case — a fall the conditional planner DECLINED — where the raw tile must
// not be handed to the dry router but the top of the column safely can be.
int progtap_retargeted_goal(int wfRetarget, int goalX, int goalY, int* gx, int* gy);

#endif
