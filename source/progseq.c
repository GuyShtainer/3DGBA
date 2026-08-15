// progseq.c — the interact sequencer, lifted out of touch.c unchanged (phase 25, lane C2).
//
// Every branch below was `prog_update`'s. The transcription rule was strict: same order, same
// budgets, same edge cases — including the ones that look like accidents (the abort ladder runs
// BEFORE the frame counter ticks; the A-pulse drains before the phase switch, so a phase entered
// with a pulse owed does not see its first frame until the pulse is spent). Anything that reads
// or writes the game stayed behind in touch.c and arrives here as an observation or leaves as an
// action. The citations are the ones the fixes were made against and are kept verbatim: they are
// the reason each rule is shaped the way it is.
#include <string.h>
#include "progseq.h"

// dir (0 R / 1 L / 2 D / 3 U — touch.c's s_keyDir order) -> the game's facing code.
static const uint8_t s_faceOfDir[4] = { 4, 3, 1, 2 };
int progseq_face_of_dir(int dir) { return (dir >= 0 && dir < 4) ? (int)s_faceOfDir[dir] : -1; }

void progseq_reset(ProgSeq* s) { if (s) memset(s, 0, sizeof *s); }

void progseq_arm(ProgSeq* s, int px, int py) {
	if (!s) return;
	s->on = 1;
	s->phase = TPH_WALK;
	s->step = 0; s->frames = 0; s->aPulse = 0; s->answers = 0;
	s->lpx = px; s->lpy = py;
}

void progseq_elig_fail(ProgSeq* s) { if (s) s->on = 0; }

static void psq_end(ProgSeq* s, ProgAct* a, int why) { s->on = 0; a->end = why; }

void progseq_step(ProgSeq* s, const FtProgram* pr, const ProgObs* o, ProgAct* a) {
	if (!a) return;
	memset(a, 0, sizeof *a);
	a->keyDir = -1; a->eligHm = FT_HM_NONE; a->end = TPE_NONE;
	if (!s || !pr || !o || !s->on) return;

	// --- the abort ladder (SPEC H0.2 / §2.4). Order matters only in that the CHEAPEST and most
	// certain tripwires come first; every one of them injects nothing further. ---
	if (o->padKeys)    { psq_end(s, a, TPE_KEY); return; }
	if (o->newPress)   { psq_end(s, a, TPE_CANCEL); a->swallow = 1; return; }
	if (o->mapChanged) { psq_end(s, a, TPE_MAPCHANGE); return; }
	// The program legitimately lives across OVERWORLD <-> FIELDMENU (the yes/no it is about to
	// answer IS a FIELDMENU), and nothing else. A battle, a bag, a party screen = someone else's
	// world now.
	if (o->ctx != PSQ_CTX_OVERWORLD && o->ctx != PSQ_CTX_FIELDMENU) { psq_end(s, a, TPE_CTX); return; }
	if (o->px < 0 || s->step < 0)    { psq_end(s, a, TPE_CTX); return; }
	if (s->step >= pr->nMoves)       { psq_end(s, a, TPE_ARRIVED); return; }

	FtMove mv = pr->mv[s->step];
	int moved = (o->px != s->lpx || o->py != s->lpy);
	s->frames++;
	a->stamp = 1; a->stampStep = s->step; a->stampPhase = s->phase;

	if (s->aPulse > 0) { s->aPulse--; a->pressA = 1; return; }

	// ---------- a plain walk step ----------
	if (mv.hm == FT_HM_NONE || s->phase == TPH_WALK) {
		if (moved) {
			s->lpx = o->px; s->lpy = o->py; s->frames = 0;
			s->step++;
			s->phase = TPH_WALK;
			a->chipRefresh = 1;
			if (s->step >= pr->nMoves) { psq_end(s, a, TPE_ARRIVED); return; }
			mv = pr->mv[s->step];
			if (mv.hm != FT_HM_NONE) { s->phase = TPH_FACE; s->frames = 0; return; }
		}
		if (s->frames > TP_WALK_BUDGET) {
			// Blocked: an NPC has walked into the path, or the world is not what we planned.
			// Re-plan from reality once (bounded), never barge.
			a->replan = 1; return;
		}
		if (mv.hm != FT_HM_NONE) { s->phase = TPH_FACE; s->frames = 0; return; }
		if (mv.dir < 0 || mv.dir > 3) { psq_end(s, a, TPE_STALL); return; }
		// PHASE 24 / lane A2 (decision D2): "decide per LEG, not once for the whole program". A
		// traversal program's leg is the contiguous run of plain walk moves before the next HM
		// interact, so the span is counted from HERE and shrinks as the obstacle approaches —
		// which means B is always released at least RUNGEOM_MIN_TILES-1 tiles before any A, and
		// can never be held into a FACE / A / DLG / YESNO / ANSWER phase. That matters more here
		// than the speed does: in a yes/no, B is NO.
		{
			int span = 0;
			for (int i = s->step; i < pr->nMoves && pr->mv[i].hm == FT_HM_NONE; i++) span++;
			a->runSpan = span;
			a->keyDir  = mv.dir;
		}
		return;
	}

	// ---------- the INTERACT sequence ----------
	switch (s->phase) {
	case TPH_FACE:
		// Re-check eligibility at the START of every interact (H1.6). It cannot realistically
		// change mid-route, but a cheap honest re-read beats an assumption, and a FALSE here means
		// we would have prompted something the game is about to refuse.
		if (s->frames == 1) { a->needElig = 1; a->eligHm = mv.hm; }
		// Hold the direction so the game turns the avatar to face the obstacle. The tile is
		// impassable (that is the whole point), so this bumps in place — the game's own
		// turn-to-face, not a step.
		if (moved) {   // it was NOT impassable: the world moved under the plan -> re-plan
			s->lpx = o->px; s->lpy = o->py;
			a->replan = 1; return;
		}
		if (mv.dir < 0 || mv.dir > 3) { psq_end(s, a, TPE_STALL); return; }
		// PHASE 24: CLOSED-LOOP on the game's own facing, not on a frame count. A fixed 8-frame
		// hold is a guess, and it loses exactly when the interact matters: Gen 3 ignores field
		// input while the avatar is still animating its previous step (field_player_avatar.c —
		// MovePlayerNotOnBike only runs from the field controller when the avatar is idle), so the
		// whole hold can be swallowed by the walk step that just ended and the avatar keeps facing
		// the way it was WALKING. Every A after that is aimed one tile off, which is precisely what
		// the Route 117 cut tree did: plan correct, walk correct, five A presses, no dialog, and a
		// hand-driven U+A at the same tile opened it instantly. So hold until the game says we face
		// the obstacle. -1 (unreadable facing) degrades to the old fixed hold rather than hanging.
		{
			int face = o->facing;
			int ok = (face < 0) ? (s->frames >= TP_FACE_FRAMES)
			                    : (face == progseq_face_of_dir(mv.dir) && s->frames >= 2);
			if (ok) { s->phase = TPH_A; s->frames = 0; return; }
			if (s->frames >= TP_FACE_BUDGET) { psq_end(s, a, TPE_STALL); return; }
		}
		a->keyDir = mv.dir;
		return;
	case TPH_A:
		// Settle first: the turn/bump that just finished is an ANIMATION, and the field controller
		// does not read A while it runs. Releasing every key for a few frames costs nothing and
		// makes the first press the one that lands (the DLG retry below covers the rest).
		if (s->frames < TP_SETTLE_FRAMES) return;
		s->aPulse = 3;                       // the shipped 3-frame A-pulse shape
		s->phase = TPH_DLG; s->frames = 0;
		return;
	case TPH_DLG:
		// "The script is talking" = our A landed on the object we aimed at.
		if (o->textDlg || o->ctx == PSQ_CTX_FIELDMENU) { s->phase = TPH_YESNO; s->frames = 0; return; }
		// PHASE 24, second edge-vs-level defect, found on the Route 117 cut tree: ONE A here is
		// not enough either. TPH_FACE holds the direction into the obstacle, and when the avatar
		// ALREADY faces it that hold is not a turn but a blocked step — pokeemerald
		// src/field_player_avatar.c PlayerNotOnBikeCollide plays a walk-in-place ("bump") animation,
		// and the field controller does not read A while the avatar is animating. So the pulse fired
		// straight into the bump and vanished (aKeys said we pressed; nothing opened). Water hid
		// this: there the player arrives facing along the shore, so FACE is a real turn and the A
		// lands after it. Keep tapping, like a player would, until the script answers or the budget
		// runs out — A at a tree/rock/water we chose and vetted is the whole point of the phase.
		if ((s->frames % TP_ADVANCE_EVERY) == 0) s->aPulse = 3;
		if (s->frames > TP_DLG_BUDGET) { psq_end(s, a, TPE_TIMEOUT); return; }
		return;
	case TPH_YESNO:
		if (o->ctx == PSQ_CTX_FIELDMENU) { s->phase = TPH_ANSWER; s->frames = 0; return; }
		if (s->frames > TP_YESNO_BUDGET) {
			// THE CORE SAFETY CASE: a dialog appeared but it was not the yes/no we predicted.
			// Close it with a single A and kill the program. NEVER guess at an unpredicted prompt.
			psq_end(s, a, TPE_UNEXPECTED);
			a->farewell = 1;    // it must survive the program, not die with it
			return;
		}
		// Advance the "want to use" message boxes, gently — and NOT gated on textDlg. PHASE 24,
		// read off pokeemerald src/field_message_box.c Task_DrawFieldMessage case 2: the game sets
		// sFieldMessageBoxMode back to HIDDEN the moment the text FINISHES PRINTING, so `textDlg`
		// is false for the whole "box is up, waiting for A" window — the exact window an advance
		// has to press in. (Cut's Text_WantToCut is a two-page \p message, so this is not a corner
		// case: without a press the second page never comes and the yes/no never appears.) A on a
		// field text box is benign, and the phase budget still bounds it.
		if ((s->frames % TP_ADVANCE_EVERY) == 0) s->aPulse = 3;
		return;
	case TPH_ANSWER:
		// THE 5-FRAME ARMING WINDOW — the defect that hung this feature twice (phase 23's open
		// prompt, and phase 24's first run), root-caused off the game's OWN source rather than
		// guessed. pokeemerald src/script_menu.c Task_HandleYesNoInput opens with
		//
		//     if (gTasks[taskId].tRight < 5) { gTasks[taskId].tRight++; return; }
		//
		// i.e. the field yes/no DELIBERATELY ignores input for its first five frames. Our ctx flips
		// to GCTX_FIELDMENU the instant that task exists (gamestate.c task-based detection), so a
		// single A pulse fired here lands entirely inside the dead window: its one 0->1 newKeys
		// edge is discarded, the held frames after it are not edges, and the prompt then sits open
		// with YES highlighted forever (progAKeys said we pressed; the game never saw it).
		//
		// So the answer is LEVEL-triggered: while the menu is up, keep the cursor on YES and keep
		// making FRESH edges on a cadence — capped, so a menu that never closes ends the program.
		if (o->ctx != PSQ_CTX_FIELDMENU) { s->phase = TPH_DONE; s->frames = 0; return; }
		a->writeYes = 1;                     // YES is row 0 in both engines' yes/no menus
		if ((s->frames % TP_ANSWER_EVERY) == 1) {
			if (s->answers >= TP_ANSWER_MAX) { psq_end(s, a, TPE_TIMEOUT); return; }
			s->answers++; a->answered = 1;
			s->aPulse = 2;                   // 2 held frames then >=6 released = a clean new edge
		}
		if (s->frames > TP_YESNO_BUDGET) { psq_end(s, a, TPE_TIMEOUT); return; }
		return;
	case TPH_DONE: {
		int done = 0;
		if (mv.hm == FT_HM_SURF) {
			// The mount CONSUMES the step: FLDEFF_USE_SURF puts the player ON the water tile.
			if (o->surfing && moved) {
				s->lpx = o->px; s->lpy = o->py;
				s->step++;
				done = 1;
			}
		} else if (mv.hm == FT_HM_WATERFALL) {
			// PHASE 26 / lane W. The ride is MULTI-TILE and self-terminating, and the game will say
			// when: pokeemerald src/field_effect.c:1880-1893 WaterfallFieldEffect_ContinueRideOrEnd
			// re-issues the slow walk north for as long as `MetatileBehavior_IsWaterfall(objectEvent
			// ->currentMetatileBehavior)` holds, and unlocks the player the first time it does not.
			// So the completion test is that same condition, inverted — never a tile count.
			//
			// Why `moved` is still required: on the FIRST frames of this phase the player is below
			// the fall, has not moved and is not on a waterfall, which would satisfy `!onWaterfall`
			// on its own and declare a ride that never started finished. Why `surfing` is still
			// required: the ride only exists mid-surf, so losing the surf bit means something else
			// happened (a battle, a warp) and the honest answer is to keep waiting for the budget.
			if (o->surfing && moved && !o->onWaterfall) {
				s->lpx = o->px; s->lpy = o->py;
				s->step++;                   // ONE move consumed however many tiles it carried us
				done = 1;
			}
		} else if (mv.hm == FT_HM_STRENGTH) {
			// PHASE 26 / lane W. Nothing on the map changes when Strength is activated — the
			// boulder is still there, the player has not moved, no object slot deactivates. The
			// only observable is the game's own latch (`setflag FLAG_SYS_USE_STRENGTH`,
			// data/scripts/field_move_scripts.inc:145), which is exactly what the executor watches.
			//
			// A Strength edge is a TERMINAL by construction (fieldtrav.c edge_at), so this step is
			// the LAST one and `s->step++` runs it off the end into TPE_ARRIVED below. That is the
			// safety property, not an accident: the walk key for this move is never emitted, so the
			// program cannot bump the boulder and push it somewhere the player did not ask for.
			if (o->strengthOn) {
				s->step++;
				done = 1;
			}
		} else {
			// Cut / Rock Smash: the object slot deactivates and the player stays put, so the walk
			// step still has to happen — the same step index, now in WALK phase.
			if (mv.objSlot >= 0 && !o->objActive) done = 1;
		}
		a->stampSurf = 1;
		if (done) {
			if (s->step >= pr->nMoves) { psq_end(s, a, TPE_ARRIVED); return; }
			a->replan = 1;               // H0.1: re-plan from reality
			return;
		}
		if (s->frames > TP_DONE_BUDGET) { psq_end(s, a, TPE_TIMEOUT); return; }
		// A-advance the "MON used SURF!" / "used CUT!" box while the cutscene plays. Same phase-24
		// correction as TPH_YESNO: `msgbox MSGBOX_DEFAULT` ends in `waitbuttonpress`, and textDlg
		// is already false by then, so gating the advance on it left the field move announced and
		// never performed (the mount is the NEXT script line).
		if ((s->frames % TP_ADVANCE_EVERY) == 0) s->aPulse = 3;
		return;
	}
	default:
		psq_end(s, a, TPE_STALL);
		return;
	}
}
