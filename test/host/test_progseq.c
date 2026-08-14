// test_progseq.c — PC host unit test for source/progseq.c, the INTERACT SEQUENCER.
//
//   clang -std=c11 -Wall -Wextra -O2 -I source test/host/test_progseq.c source/progseq.c \
//         -o /tmp/tpq && /tmp/tpq
//
// WHY THIS SUITE EXISTS. The phase-24 adversarial audit, finding O2:
//
//     "No host suite compiles source/touch.c ... Of the seven lane-A fixes, exactly one has a
//      regression barrier. 2dde03e (the level-triggered YES), b0ae8d9 (the DLG cadence),
//      70f963d (closed-loop FACE) ... are emulator-only, with nothing that would catch a
//      regression."
//
// Three of those six are in this file's subject. Each was a LIVE defect that cost a hardware-class
// emulator run to find, and each has the same shape: an EDGE where the game needs a LEVEL, or a
// FRAME COUNT where the game will answer for itself. So the tests below are written the way the
// defects were found — drive the machine frame by frame, and judge it against a model of what the
// GAME does, not against a restatement of what our code does:
//
//   * TEST 4 runs the real sequencer against a transcription of pokeemerald's own
//     Task_HandleYesNoInput (the five-frame arming window). The oracle presses YES when the GAME
//     would have; the assertion is that YES gets pressed at all. A single-edge implementation —
//     the one that shipped before 2dde03e — fails it, and the suite proves that by running the
//     same oracle against the old rule (the ANTI-TEST).
//   * TEST 3 runs it against a model of the avatar's facing that does what the engine does: input
//     is ignored while the previous step is still animating, so a fixed-length hold turns nothing.
//   * TEST 5 counts the A presses a never-opening dialog gets, which is the difference between
//     "we pressed once and gave up" and "we kept tapping, like a player".
//
// The machine under test is the SHIPPED one: touch.c calls progseq_step for every program frame
// and does nothing with the sequence itself. There is no second copy to drift from.
//
// TEST NUMBERING
//   TEST 1  the abort ladder — six tripwires, in order, each injecting nothing        SPEC H0.2
//   TEST 2  the plain WALK leg: step advance, the run SPAN, the budget -> re-plan     D2 / H0.1
//   TEST 3  CLOSED-LOOP FACE + the A settle                                           fix 70f963d
//   TEST 4  the LEVEL-triggered ANSWER, judged by the game's own yes/no task          fix 2dde03e
//   TEST 5  the DLG cadence: keep tapping at an object we chose and vetted            fix b0ae8d9
//   TEST 6  YESNO — advance NOT gated on textDlg, and the unpredicted-prompt safety   fix 2dde03e
//   TEST 7  DONE — the surf mount consumes the step, the Cut does not                 T2
//   TEST 8  the eligibility request (H1.6), and what a refusal does
//   TEST 9  P1 SURF end to end: the phase trace the live run produced
//   TEST 10 invariants, exhaustively: never a key after an end, never A while steering
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "progseq.h"

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

// ---------------------------------------------------------------------------------------------
// Harness: a program, a world, and one frame at a time.
// ---------------------------------------------------------------------------------------------
// touch.c's s_keyDir order, restated once so the direction assertions read like the shipped code.
enum { DIR_R = 0, DIR_L, DIR_D, DIR_U };

static void prog_walk(FtProgram* pr, int n, int dir) {   // n plain walk steps, one direction
	memset(pr, 0, sizeof *pr);
	pr->ok = 1; pr->nMoves = n;
	for (int i = 0; i < n; i++) { pr->mv[i].dir = (int8_t)dir; pr->mv[i].hm = FT_HM_NONE; pr->mv[i].objSlot = -1; }
}
// n plain steps then ONE interact of `hm` facing `dir` (objSlot -1 = Surf's self-proof).
static void prog_walk_then(FtProgram* pr, int n, int dir, int hm, int objSlot) {
	prog_walk(pr, n + 1, dir);
	pr->nInteracts = 1;
	pr->mv[n].hm = (uint8_t)hm;
	pr->mv[n].objSlot = (int16_t)objSlot;
}

typedef struct {          // a tiny stand-in for the world touch.c reads
	ProgSeq  s;
	FtProgram pr;
	ProgObs  o;
	ProgAct  a;
	int frames;           // frames driven
	int aFrames;          // frames on which the machine emitted A
	int aEdges;           // 0 -> 1 transitions of the A key (what the GAME counts)
	int lastA;
	int keyFrames;        // frames on which it held a direction
} World;

static void w_init(World* w) {
	memset(w, 0, sizeof *w);
	w->o.ctx = PSQ_CTX_OVERWORLD;
	w->o.facing = -1;          // "unreadable" unless a test says otherwise
	w->o.px = 10; w->o.py = 10;
	progseq_arm(&w->s, w->o.px, w->o.py);
}
static void w_step(World* w) {
	progseq_step(&w->s, &w->pr, &w->o, &w->a);
	w->frames++;
	if (w->a.pressA) { w->aFrames++; if (!w->lastA) w->aEdges++; }
	if (w->a.keyDir >= 0) w->keyFrames++;
	w->lastA = w->a.pressA;
}
static void w_run(World* w, int n) { for (int i = 0; i < n; i++) if (w->s.on) w_step(w); }

// ---------------------------------------------------------------------------------------------
// TEST 1 — the abort ladder (SPEC H0.2 / §2.4)
// ---------------------------------------------------------------------------------------------
static void test_abort_ladder(void) {
	struct { const char* name; int want; } row[] = {
		{ "a physical key", TPE_KEY }, { "a new touch", TPE_CANCEL }, { "the map changed", TPE_MAPCHANGE },
		{ "someone else's screen", TPE_CTX }, { "no overworld", TPE_CTX }, { "step past the plan", TPE_ARRIVED },
	};
	for (int i = 0; i < 6; i++) {
		World w; w_init(&w); prog_walk(&w.pr, 4, DIR_R);
		switch (i) {
		case 0: w.o.padKeys = 1; break;
		case 1: w.o.newPress = 1; break;
		case 2: w.o.mapChanged = 1; break;
		case 3: w.o.ctx = PSQ_CTX_OTHER; break;
		case 4: w.o.px = -1; break;
		case 5: w.s.step = 4; break;
		}
		w_step(&w);
		CHECK(w.a.end == row[i].want, "%s ends the program (%d, want %d)", row[i].name, w.a.end, row[i].want);
		CHECK(w.s.on == 0, "%s: the machine is off afterwards", row[i].name);
		CHECK(w.a.keyDir < 0 && !w.a.pressA && !w.a.replan && !w.a.writeYes,
		      "%s: nothing is injected on the frame that aborts", row[i].name);
		CHECK(w.a.stamp == 0, "%s: an abort does not tick the phase clock", row[i].name);
		// ...and it stays off: a second frame injects nothing either.
		w.o.padKeys = w.o.newPress = w.o.mapChanged = 0; w.o.ctx = PSQ_CTX_OVERWORLD; w.o.px = 10;
		progseq_step(&w.s, &w.pr, &w.o, &w.a);
		CHECK(w.a.keyDir < 0 && !w.a.pressA && w.a.end == TPE_NONE, "%s: and the frame after", row[i].name);
	}
	// The two contexts a program legitimately lives across, and the third that kills it.
	World w; w_init(&w); prog_walk(&w.pr, 4, DIR_R);
	w.o.ctx = PSQ_CTX_FIELDMENU; w_step(&w);
	CHECK(w.s.on && w.a.end == TPE_NONE, "a FIELDMENU is the yes/no we are about to answer, not an abort");
	// CANCEL is the only abort that swallows the touch that caused it (H4.3).
	for (int i = 0; i < 6; i++) {
		World v; w_init(&v); prog_walk(&v.pr, 4, DIR_R);
		if (i == 0) v.o.padKeys = 1; else if (i == 1) v.o.newPress = 1; else if (i == 2) v.o.mapChanged = 1;
		else if (i == 3) v.o.ctx = PSQ_CTX_OTHER; else if (i == 4) v.o.px = -1; else v.s.step = 4;
		w_step(&v);
		CHECK(v.a.swallow == (i == 1), "swallow is set for the touch cancel and nothing else (%d)", i);
		CHECK(v.a.farewell == 0, "no abort queues the farewell A (only an UNPREDICTED prompt does)");
	}
}

// ---------------------------------------------------------------------------------------------
// TEST 2 — a plain walk leg
// ---------------------------------------------------------------------------------------------
static void test_walk(void) {
	World w; w_init(&w); prog_walk(&w.pr, 5, DIR_R);
	// Frame 1: the machine holds the direction and reports the LEG span for the run decision.
	w_step(&w);
	CHECK(w.a.keyDir == DIR_R, "a walk step holds its own direction");
	CHECK(w.a.runSpan == 5, "the run span is the whole leg (5 plain moves)");
	CHECK(w.a.stampStep == 0 && w.a.stampPhase == TPH_WALK, "the mirror reports the frame's OWN step/phase");
	// Each completed step (the player tile changing) advances the plan and shrinks the span.
	for (int i = 1; i <= 4; i++) {
		w.o.px += 1;                      // the game moved us one tile
		w_step(&w);
		CHECK(w.s.step == i, "step %d completed", i);
		CHECK(w.a.chipRefresh == 1, "a completed step refreshes the HUD verb");
		CHECK(w.a.runSpan == 5 - i, "the span shrinks as the plan is consumed (%d)", w.a.runSpan);
		CHECK(w.s.frames == 0, "the phase clock restarts on every step");
	}
	w.o.px += 1; w_step(&w);
	CHECK(w.a.end == TPE_ARRIVED && !w.s.on, "the last step arrives");
	CHECK(w.a.chipRefresh == 1, "...and still refreshes the chip before it goes");

	// A blocked step: no movement for the whole budget -> ONE re-plan request, never a barge.
	World b; w_init(&b); prog_walk(&b.pr, 5, DIR_R);
	int replans = 0;
	for (int i = 0; i < TP_WALK_BUDGET + 1; i++) { w_step(&b); if (b.a.replan) replans++; }
	CHECK(b.s.frames == TP_WALK_BUDGET + 1, "the walk budget is counted in frames, not steps");
	CHECK(replans == 1, "a stalled step asks for exactly one re-plan at the budget (got %d)", replans);
	CHECK(b.a.keyDir < 0, "and injects nothing on the frame it asks");
	CHECK(b.keyFrames == TP_WALK_BUDGET, "every frame up to the budget kept walking");

	// A move with a bad direction is a stall, not a wild key.
	World s; w_init(&s); prog_walk(&s.pr, 2, DIR_R); s.pr.mv[0].dir = 9;
	w_step(&s);
	CHECK(s.a.end == TPE_STALL && s.a.keyDir < 0, "an out-of-range direction stalls rather than guessing");
}

// ---------------------------------------------------------------------------------------------
// TEST 3 — CLOSED-LOOP FACE (fix 70f963d) and the A settle
// ---------------------------------------------------------------------------------------------
// THE DEFECT, in the words of the run that found it: "five A presses over 120 frames and still no
// dialog at the Route 117 tree ... a bare `a` with no direction ALSO did nothing, while a
// hand-driven U then a opened the tree instantly. So the A was landing; it was aimed one tile off,
// because the avatar was not facing the tree." Gen 3 only reads field input when the avatar is
// idle, so a hold that begins while the last walk step is still animating is swallowed whole.
static void test_face(void) {
	// The mapping is the whole correctness of the loop: our dir order (R/L/D/U) vs the game's
	// facingDirection codes (1 D / 2 U / 3 L / 4 R — gObjectEvents[0]+0x18 low nibble).
	CHECK(progseq_face_of_dir(DIR_R) == 4, "R -> the game's 4");
	CHECK(progseq_face_of_dir(DIR_L) == 3, "L -> 3");
	CHECK(progseq_face_of_dir(DIR_D) == 1, "D -> 1");
	CHECK(progseq_face_of_dir(DIR_U) == 2, "U -> 2");
	CHECK(progseq_face_of_dir(-1) == -1 && progseq_face_of_dir(4) == -1, "and nothing else maps");

	for (int dir = 0; dir < 4; dir++) {
		// THE ANIMATION CASE. The avatar is still facing the way it WALKED (the perpendicular
		// direction), and the engine ignores the hold until the step animation ends.
		World w; w_init(&w); prog_walk_then(&w.pr, 0, dir, FT_HM_CUT, 3);
		w.o.facing = progseq_face_of_dir(dir ^ 2);      // some other direction
		w_step(&w);                                     // the WALK frame that arms the interact
		CHECK(w.s.phase == TPH_FACE && w.a.keyDir < 0, "dir %d: the interact opens in FACE", dir);
		for (int i = 0; i < TP_FACE_BUDGET - 1; i++) {
			w_step(&w);
			CHECK(w.s.phase == TPH_FACE, "dir %d frame %d: still turning (the game has not agreed yet)", dir, i);
			CHECK(w.a.keyDir == dir, "dir %d frame %d: and still holding the direction", dir, i);
		}
		CHECK(w.frames >= TP_FACE_FRAMES,
		      "dir %d: the OLD fixed %d-frame hold would have fired the A here, aimed one tile off",
		      dir, TP_FACE_FRAMES);
		// The game finally turns the avatar: the loop closes on the game's own answer.
		w.o.facing = progseq_face_of_dir(dir);
		w_step(&w);
		CHECK(w.s.phase == TPH_A, "dir %d: the game says we face the obstacle -> aim the A", dir);
		CHECK(w.a.keyDir < 0, "dir %d: and the hold is released on that frame", dir);
	}
	// THE BUDGET: a facing that never agrees gives up loudly instead of hanging.
	World h; w_init(&h); prog_walk_then(&h.pr, 0, DIR_U, FT_HM_CUT, 3);
	h.o.facing = progseq_face_of_dir(DIR_D);
	w_run(&h, TP_FACE_BUDGET + 10);
	CHECK(h.a.end == TPE_STALL, "a facing that never agrees ends TPE_STALL");
	CHECK(h.frames == TP_FACE_BUDGET + 1, "...at exactly the budget, plus the frame that entered "
	      "the phase (%d)", h.frames);

	// THE DEGRADE: an unreadable facing (no mapObjects address, a game we have no column for) must
	// fall back to the old fixed hold rather than hang — a wrong A beats a dead feature here.
	World d; w_init(&d); prog_walk_then(&d.pr, 0, DIR_U, FT_HM_CUT, 3);
	d.o.facing = -1;
	w_step(&d);                                     // the WALK frame that arms the interact
	for (int i = 0; i < TP_FACE_FRAMES - 1; i++) { w_step(&d); CHECK(d.s.phase == TPH_FACE, "unreadable: hold %d", i); }
	w_step(&d);
	CHECK(d.s.phase == TPH_A, "an unreadable facing degrades to the fixed %d-frame hold", TP_FACE_FRAMES);

	// The two-frame floor: even a facing that ALREADY agrees is given a frame, because the read can
	// be from before the hold started.
	World f; w_init(&f); prog_walk_then(&f.pr, 0, DIR_U, FT_HM_CUT, 3);
	f.o.facing = progseq_face_of_dir(DIR_U);
	w_step(&f);                                     // WALK -> FACE
	w_step(&f);
	CHECK(f.s.phase == TPH_FACE, "frame 1 never advances, however agreeable the facing");
	w_step(&f);
	CHECK(f.s.phase == TPH_A, "frame 2 does");

	// THE SETTLE (same fix): the turn/bump that just finished is an ANIMATION and the field
	// controller does not read A while it runs, so TPH_A releases every key first.
	int aFirst = -1;
	for (int i = 0; i < TP_SETTLE_FRAMES + 6; i++) {
		w_step(&f);
		if (f.a.pressA && aFirst < 0) aFirst = i;
	}
	CHECK(aFirst == TP_SETTLE_FRAMES, "the first A lands after the settle, not inside the animation (%d)", aFirst);
	CHECK(f.aFrames == 3, "and it is the shipped 3-frame pulse (%d)", f.aFrames);
	CHECK(f.aEdges == 1, "one edge");

	// The world moving under a FACE means the tile was NOT impassable: re-plan, never press.
	World m; w_init(&m); prog_walk_then(&m.pr, 0, DIR_R, FT_HM_SMASH, 2);
	m.o.facing = progseq_face_of_dir(DIR_R);
	w_step(&m);
	m.o.px += 1;
	w_step(&m);
	CHECK(m.a.replan == 1, "an avatar that MOVED during a FACE re-plans (the world is not the plan)");
	CHECK(!m.a.pressA && m.a.keyDir < 0, "...and injects nothing while it does");
}

// ---------------------------------------------------------------------------------------------
// TEST 4 — the LEVEL-triggered ANSWER (fix 2dde03e), judged by the GAME'S OWN yes/no task
// ---------------------------------------------------------------------------------------------
// pokeemerald src/script_menu.c Task_HandleYesNoInput opens with
//
//     if (gTasks[taskId].tRight < 5) { gTasks[taskId].tRight++; return; }
//
// so the field yes/no ignores input for its first FIVE frames — and it reads NEW presses (edges),
// not levels. This oracle is that rule and nothing else; it is what decides the test.
typedef struct { int frames; int done; int pressedYes; } YesNoTask;
static void yesno_task(YesNoTask* t, int aHeld, int aWasHeld) {
	if (t->done) return;
	if (t->frames < 5) { t->frames++; return; }     // the arming window: input is DISCARDED
	t->frames++;
	if (aHeld && !aWasHeld) { t->done = 1; t->pressedYes = 1; }   // JOY_NEW(A_BUTTON)
}

static void test_answer(void) {
	// Drive the REAL sequencer at a REAL yes/no task. The prompt is up (ctx = FIELDMENU) from the
	// frame the program reaches ANSWER, exactly as gamestate.c's task-based detection reports it.
	World w; w_init(&w); prog_walk_then(&w.pr, 0, DIR_R, FT_HM_SURF, -1);
	w.o.facing = progseq_face_of_dir(DIR_R);
	w.s.phase = TPH_ANSWER; w.s.frames = 0;
	w.o.ctx = PSQ_CTX_FIELDMENU;
	YesNoTask t = { 0, 0, 0 };
	int prevA = 0, cursorWrites = 0;
	for (int i = 0; i < 60 && w.s.on && !t.done; i++) {
		w_step(&w);
		if (w.a.writeYes) cursorWrites++;
		yesno_task(&t, w.a.pressA, prevA);
		prevA = w.a.pressA;
	}
	CHECK(t.pressedYes == 1, "THE FIX: the game's own yes/no task really pressed YES");
	CHECK(t.frames > 5, "...on a frame past the five-frame arming window (frame %d)", t.frames);
	CHECK(cursorWrites >= 1, "the cursor is written to row 0 (YES) while the menu is up");
	CHECK(w.aEdges >= 2, "which took more than one edge (%d) — that is what LEVEL-triggered means", w.aEdges);

	// THE ANTI-TEST. The shipped-before-2dde03e rule was: write the cursor, fire ONE 3-frame pulse,
	// move to DONE. Run the SAME oracle against it. If this ever passes, the test above is not
	// testing anything.
	{
		YesNoTask t2 = { 0, 0, 0 };
		int prev = 0;
		for (int i = 0; i < 60; i++) {
			int a = (i < 3);                      // the single 3-frame pulse, fired on entry
			yesno_task(&t2, a, prev);
			prev = a;
		}
		CHECK(t2.pressedYes == 0,
		      "the OLD single-edge answer never pressed YES — its one edge died inside the window");
	}

	// The cap: a menu that never closes ends the program instead of drumming A forever.
	World c; w_init(&c); prog_walk_then(&c.pr, 0, DIR_R, FT_HM_SURF, -1);
	c.s.phase = TPH_ANSWER; c.s.frames = 0; c.o.ctx = PSQ_CTX_FIELDMENU;
	w_run(&c, TP_YESNO_BUDGET + 40);
	CHECK(c.a.end == TPE_TIMEOUT, "an unanswerable menu ends TPE_TIMEOUT");
	CHECK(c.s.answers == TP_ANSWER_MAX, "after exactly %d aimed presses (%d)", TP_ANSWER_MAX, c.s.answers);
	CHECK(c.frames == (TP_ANSWER_MAX * TP_ANSWER_EVERY) + 1,
	      "the cap, not the budget, is what stops it (%d frames)", c.frames);

	// The cadence itself, and that every ANSWER frame keeps the cursor on YES.
	World k; w_init(&k); prog_walk_then(&k.pr, 0, DIR_R, FT_HM_SURF, -1);
	k.s.phase = TPH_ANSWER; k.s.frames = 0; k.o.ctx = PSQ_CTX_FIELDMENU;
	int writes = 0, answered = 0;
	for (int i = 0; i < TP_ANSWER_EVERY * 3 && k.s.on; i++) {
		w_step(&k);
		if (k.a.writeYes) writes++;
		if (k.a.answered) answered++;
		if (k.a.answered) CHECK((k.s.frames % TP_ANSWER_EVERY) == 1, "a press is made on the cadence frame");
	}
	CHECK(answered == 3, "three presses in three cadence windows (%d)", answered);
	CHECK(writes >= TP_ANSWER_EVERY * 3 - 6, "the cursor is held on YES on (nearly) every frame: %d", writes);
	CHECK(k.aEdges == 3, "three FRESH edges (%d) — 2 held frames then >=6 released", k.aEdges);

	// The menu closing is how the phase ends normally.
	World e; w_init(&e); prog_walk_then(&e.pr, 0, DIR_R, FT_HM_SURF, -1);
	e.s.phase = TPH_ANSWER; e.s.frames = 0; e.o.ctx = PSQ_CTX_FIELDMENU;
	w_step(&e);
	e.o.ctx = PSQ_CTX_OVERWORLD;
	w_run(&e, 4);                       // the pulse in flight drains first — it always finishes
	CHECK(e.s.phase == TPH_DONE && e.s.on, "the menu closing moves the program to DONE, not to an end");
}

// ---------------------------------------------------------------------------------------------
// TEST 5 — the DLG cadence (fix b0ae8d9): keep tapping, like a player
// ---------------------------------------------------------------------------------------------
static void test_dlg(void) {
	World w; w_init(&w); prog_walk_then(&w.pr, 0, DIR_U, FT_HM_CUT, 3);
	w.s.phase = TPH_DLG; w.s.frames = 0;
	int pulses = 0, prevA = 0;
	for (int i = 0; i < TP_DLG_BUDGET + 5 && w.s.on; i++) {
		w_step(&w);
		if (w.a.pressA && !prevA) pulses++;
		prevA = w.a.pressA;
	}
	CHECK(pulses == 5, "a dialog that never opens gets FIVE A presses over the budget (got %d)", pulses);
	CHECK(w.a.end == TPE_TIMEOUT, "...and then the program gives up honestly");
	// +4, not +1: the budget test lives BELOW the pulse drain, so a pulse in flight always finishes
	// (3 frames) before the phase can time out. That is the shipped shape and it is the safe one —
	// a half-emitted A is a keypress the game may read as something else entirely.
	CHECK(w.frames == TP_DLG_BUDGET + 4, "at the budget, after the pulse in flight (%d)", w.frames);
	CHECK(pulses > 1, "the pre-b0ae8d9 rule pressed once; the bump animation ate it");

	// The script talking is the proof our A landed on the object we aimed at.
	World t; w_init(&t); prog_walk_then(&t.pr, 0, DIR_U, FT_HM_CUT, 3);
	t.s.phase = TPH_DLG; t.s.frames = 0;
	w_step(&t);
	t.o.textDlg = 1;
	w_step(&t);
	CHECK(t.s.phase == TPH_YESNO && t.s.frames == 0, "sFieldMessageBoxMode != 0 -> the script is talking");
	// ...and so is a yes/no that appeared without a textbox we ever saw printing.
	World f; w_init(&f); prog_walk_then(&f.pr, 0, DIR_U, FT_HM_CUT, 3);
	f.s.phase = TPH_DLG; f.s.frames = 0;
	f.o.ctx = PSQ_CTX_FIELDMENU;
	w_step(&f);
	CHECK(f.s.phase == TPH_YESNO, "a FIELDMENU is equally good proof the interact landed");
}

// ---------------------------------------------------------------------------------------------
// TEST 6 — YESNO: the advance is NOT gated on textDlg, and the safety case
// ---------------------------------------------------------------------------------------------
// pokeemerald src/field_message_box.c sets sFieldMessageBoxMode back to HIDDEN the moment the text
// FINISHES PRINTING — so textDlg is false for the whole "box is up, waiting for A" window, which is
// exactly the window an advance has to press in. Cut's Text_WantToCut is a two-page \p message.
static void test_yesno(void) {
	World w; w_init(&w); prog_walk_then(&w.pr, 0, DIR_U, FT_HM_CUT, 3);
	w.s.phase = TPH_YESNO; w.s.frames = 0;
	w.o.textDlg = 0;                       // the printing flag is ALREADY back to zero
	int pulses = 0, prevA = 0;
	for (int i = 0; i < TP_ADVANCE_EVERY * 3 + 4 && w.s.on; i++) {
		w_step(&w);
		if (w.a.pressA && !prevA) pulses++;
		prevA = w.a.pressA;
	}
	CHECK(pulses == 3, "a waiting box is advanced on the cadence even with textDlg == 0 (%d)", pulses);

	// THE CORE SAFETY CASE: a dialog appeared but it was not the yes/no we predicted. One A closes
	// it, the program dies, and the A must OUTLIVE the program (s_progFarewell in touch.c).
	World u; w_init(&u); prog_walk_then(&u.pr, 0, DIR_U, FT_HM_CUT, 3);
	u.s.phase = TPH_YESNO; u.s.frames = 0;
	w_run(&u, TP_YESNO_BUDGET + 5);
	CHECK(u.a.end == TPE_UNEXPECTED, "an unpredicted prompt ends TPE_UNEXPECTED — never a guessed YES");
	CHECK(u.a.farewell == 1, "...and queues the single farewell A that closes it");
	CHECK(u.frames == TP_YESNO_BUDGET + 4, "at the budget, after the pulse in flight (%d)", u.frames);

	// The predicted yes/no appearing is the normal exit.
	World y; w_init(&y); prog_walk_then(&y.pr, 0, DIR_U, FT_HM_CUT, 3);
	y.s.phase = TPH_YESNO; y.s.frames = 0;
	y.o.ctx = PSQ_CTX_FIELDMENU;
	w_step(&y);
	CHECK(y.s.phase == TPH_ANSWER && y.s.frames == 0, "the yes/no we predicted -> ANSWER");
}

// ---------------------------------------------------------------------------------------------
// TEST 7 — DONE: who moves the avatar decides whether the step is consumed
// ---------------------------------------------------------------------------------------------
static void test_done(void) {
	// SURF: FLDEFF_USE_SURF puts the player ON the water tile, so the interaction CONSUMES the move.
	World s; w_init(&s); prog_walk_then(&s.pr, 0, DIR_R, FT_HM_SURF, -1);
	s.s.phase = TPH_DONE; s.s.frames = 0;
	w_step(&s);
	CHECK(s.a.stampSurf == 1, "every DONE frame re-reads the surf bit for the mirror");
	CHECK(s.s.on && s.s.step == 0, "the mount has not landed yet");
	s.o.surfing = 1;                       // the bit is set...
	w_step(&s);
	CHECK(s.s.on && s.s.step == 0, "...but the avatar has not moved: not done (a bit alone is not proof)");
	s.o.px += 1;                           // ...and now the cutscene has stepped us onto the water
	w_step(&s);
	CHECK(s.a.end == TPE_ARRIVED, "surf + a step = the mount landed, and it consumed the last move");

	// CUT / SMASH: the object vanishes and the player stays put, so the walk step still happens.
	World c; w_init(&c); prog_walk_then(&c.pr, 1, DIR_U, FT_HM_CUT, 3);
	c.s.step = 1; c.s.phase = TPH_DONE; c.s.frames = 0;
	c.o.objActive = 1;
	w_step(&c);
	CHECK(c.s.on && !c.a.replan, "the tree is still standing");
	c.o.objActive = 0;                     // the script's removeobject VAR_LAST_TALKED
	w_step(&c);
	CHECK(c.a.replan == 1, "the object going inactive IS the proof the Cut landed -> re-plan from reality");
	CHECK(c.s.step == 1, "...and the move is NOT consumed: the walk through the gap still has to happen");

	// The cutscene advance, ungated on textDlg for the same reason as YESNO (waitbuttonpress).
	World a; w_init(&a); prog_walk_then(&a.pr, 0, DIR_R, FT_HM_SURF, -1);
	a.s.phase = TPH_DONE; a.s.frames = 0;
	int pulses = 0, prevA = 0;
	for (int i = 0; i < TP_ADVANCE_EVERY * 2 + 4 && a.s.on; i++) {
		w_step(&a);
		if (a.a.pressA && !prevA) pulses++;
		prevA = a.a.pressA;
	}
	CHECK(pulses == 2, "\"MON used SURF!\" is advanced on the cadence with textDlg 0 (%d)", pulses);
	// The budget.
	World b; w_init(&b); prog_walk_then(&b.pr, 0, DIR_R, FT_HM_SURF, -1);
	b.s.phase = TPH_DONE; b.s.frames = 0;
	w_run(&b, TP_DONE_BUDGET + 8);
	CHECK(b.a.end == TPE_TIMEOUT && b.frames == TP_DONE_BUDGET + 4, "a cutscene that never ends times out");
	// An interact with no object slot and no surf can never "complete" by accident.
	World n; w_init(&n); prog_walk_then(&n.pr, 0, DIR_U, FT_HM_CUT, -1);
	n.s.phase = TPH_DONE; n.s.frames = 0; n.o.objActive = 0;
	w_run(&n, 30);
	CHECK(n.s.on && n.s.step == 0, "objSlot < 0 on a Cut never self-completes (it has no proof to offer)");
}

// ---------------------------------------------------------------------------------------------
// TEST 8 — the eligibility request (H1.6)
// ---------------------------------------------------------------------------------------------
static void test_elig(void) {
	World w; w_init(&w); prog_walk_then(&w.pr, 2, DIR_R, FT_HM_SURF, -1);
	int asks = 0;
	for (int i = 0; i < 3; i++) { w_step(&w); if (w.a.needElig) asks++; w.o.px += 1; }
	CHECK(asks == 0, "a plain walk step never asks for an eligibility re-read (it is expensive)");
	// step 2 is the interact: the FACE phase's FIRST frame asks, once.
	w.o.facing = -1;
	asks = 0;
	int hm = 0;
	for (int i = 0; i < TP_FACE_FRAMES; i++) { w_step(&w); if (w.a.needElig) { asks++; hm = w.a.eligHm; } }
	CHECK(asks == 1, "every interact re-checks eligibility exactly once, on its first frame (%d)", asks);
	CHECK(hm == FT_HM_SURF, "...for the move it is about to use");
	// A refusal ends the program: we would have prompted something the game is about to refuse.
	progseq_elig_fail(&w.s);
	CHECK(w.s.on == 0, "a refusal turns the machine off");
	progseq_step(&w.s, &w.pr, &w.o, &w.a);
	CHECK(w.a.keyDir < 0 && !w.a.pressA, "...and nothing is injected afterwards");
}

// ---------------------------------------------------------------------------------------------
// TEST 9 — P1 SURF, end to end, as the live run produced it
// ---------------------------------------------------------------------------------------------
// The phase-24 P1 proof: a tap on the ocean planned a SURF edge, the route walked the shore, the
// executor faced the water, pressed A, advanced the game's message, answered YES, and the game
// mounted. This drives the WHOLE sequence with a world that answers the way Emerald answered.
static void test_p1_surf_e2e(void) {
	World w; w_init(&w); prog_walk_then(&w.pr, 3, DIR_R, FT_HM_SURF, -1);
	w.o.facing = progseq_face_of_dir(DIR_D);      // arrived walking along the shore, facing DOWN
	int phases[9]; int nph = 0; int last = -1;
	YesNoTask task = { 0, 0, 0 };
	int prevA = 0, guard = 0;
	while (w.s.on && guard++ < 2000) {
		if (w.s.phase != last) { if (nph < 9) phases[nph++] = w.s.phase; last = w.s.phase; }
		w_step(&w);
		// The world's answers, in the order Emerald gave them:
		if (w.a.keyDir == DIR_R && w.s.phase == TPH_WALK) w.o.px += 1;        // a step completes
		if (w.s.phase == TPH_FACE && w.s.frames >= 3) w.o.facing = progseq_face_of_dir(DIR_R);
		if (w.s.phase == TPH_DLG && w.aFrames > 0) w.o.textDlg = 1;           // "The water is dyed..."
		if (w.s.phase == TPH_YESNO && w.s.frames > 2) { w.o.textDlg = 0; w.o.ctx = PSQ_CTX_FIELDMENU; }
		if (w.s.phase == TPH_ANSWER) {
			yesno_task(&task, w.a.pressA, prevA);
			if (task.pressedYes) { w.o.ctx = PSQ_CTX_OVERWORLD; }
		}
		prevA = w.a.pressA;
		if (w.s.phase == TPH_DONE && task.pressedYes && w.s.frames > 30) { w.o.surfing = 1; w.o.px += 1; }
	}
	CHECK(task.pressedYes == 1, "P1: the game's yes/no really answered YES");
	CHECK(w.a.end == TPE_ARRIVED, "P1: the program ended ARRIVED (%d)", w.a.end);
	CHECK(w.o.surfing == 1, "P1: and the avatar is afloat");
	const int want[7] = { TPH_WALK, TPH_FACE, TPH_A, TPH_DLG, TPH_YESNO, TPH_ANSWER, TPH_DONE };
	CHECK(nph == 7, "P1: seven phases, in order (%d)", nph);
	for (int i = 0; i < nph && i < 7; i++) CHECK(phases[i] == want[i], "P1 phase %d == %d", i, want[i]);
	CHECK(guard < 2000, "P1: and it terminated");
}

// ---------------------------------------------------------------------------------------------
// TEST 10 — invariants, over every phase x a sweep of worlds
// ---------------------------------------------------------------------------------------------
static void test_invariants(void) {
	int bad = 0, both = 0, afterEnd = 0, aimed = 0, cases = 0;
	for (int phase = TPH_WALK; phase <= TPH_DONE; phase++)
	for (int hm = FT_HM_NONE; hm <= FT_HM_SURF; hm++)
	for (int ctx = 0; ctx <= 2; ctx++)
	for (int dlg = 0; dlg <= 1; dlg++)
	for (int face = -1; face <= 4; face++)
	for (int surf = 0; surf <= 1; surf++)
	for (int obj = 0; obj <= 1; obj++) {
		World w; w_init(&w);
		prog_walk_then(&w.pr, 1, DIR_R, hm ? hm : FT_HM_CUT, hm == FT_HM_SURF ? -1 : 3);
		if (hm == FT_HM_NONE) prog_walk(&w.pr, 3, DIR_R);
		w.s.step = 1; w.s.phase = phase; w.s.frames = 0;
		w.o.ctx = ctx; w.o.textDlg = dlg; w.o.facing = face; w.o.surfing = surf; w.o.objActive = obj;
		for (int f = 0; f < 40; f++) {
			int wasOn = w.s.on;
			progseq_step(&w.s, &w.pr, &w.o, &w.a);
			cases++;
			if (w.a.pressA && w.a.keyDir >= 0) both++;             // A is aimed by the FACING...
			if (w.a.keyDir >= 4 || w.a.keyDir < -1) bad++;         // ...and a key is always a real one
			if (!wasOn && (w.a.pressA || w.a.keyDir >= 0)) afterEnd++;
			if (w.a.end != TPE_NONE && (w.a.pressA || w.a.keyDir >= 0)) aimed++;
		}
	}
	CHECK(cases > 20000, "the invariant sweep really ran (%d frames)", cases);
	CHECK(both == 0, "A is NEVER pressed on a frame that also steers (it would aim one tile off)");
	CHECK(bad == 0, "a direction is always -1 or 0..3");
	CHECK(afterEnd == 0, "a dead program injects nothing, ever");
	CHECK(aimed == 0, "the frame that ends a program injects nothing either");
}

int main(void) {
	test_abort_ladder();
	test_walk();
	test_face();
	test_answer();
	test_dlg();
	test_yesno();
	test_done();
	test_elig();
	test_p1_surf_e2e();
	test_invariants();
	printf("\n=== %d checks, %d failures ===\n", g_checks, g_fails);
	return g_fails ? 1 : 0;
}
