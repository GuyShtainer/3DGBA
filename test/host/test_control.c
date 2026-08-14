// test_control.c — PC host unit test for the D4 + D5 control module (source/control.{c,h}):
// D4 — the whole token grammar, the closed-loop walk scheduler driven by a FAKE coordinate feed,
// the wall(-blocked) timeout, warp completion, the no-field guard, tap press/slot frame windows,
// the go-gate, the real-input abort rule, frozen-clock behaviour, the status ring / counters.
// D5 — the record encoder (field-entry EDGE anchor + debounce, on-change lines, offsets across a
// frozen clock, the line cap, the file header), the replay table loader (whole-buffer AND
// chunked, every loud-failure mode), PM's catch-up playback loop, and the RECORD -> REPLAY
// ROUND TRIP that reproduces an exact mask timeline through the pure-C core.
// Pure-C dual-compile per CLAUDE.md rule #4 / PHASE.md invariants 3-4;
// spec: docs/phase13-diagnostics/SPEC-control-replay.md §C.6.
//
//   clang -std=c11 -Wall -Wextra -O0 -g -I source test/host/test_control.c -o /tmp/tcl && /tmp/tcl
//
// (No mock <3ds.h> needed — control.c is header-free pure C by design: no libctru, no file I/O,
// no clock. Every timer is driven by the CtlIn.emuFrame the test supplies.)

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "../../source/control.c"

static int g_checks = 0, g_fail = 0;
#define CHECK(cond, ...) do { \
	g_checks++; \
	if (!(cond)) { g_fail++; printf("  [FAIL] "); printf(__VA_ARGS__); printf("   (at %s:%d)\n", __FILE__, __LINE__); } \
} while (0)

// --------------------------------------------------------------------------------------------
// Fake world — the coordinate feed the closed loop reads. The real glue fills CtlIn from the
// GameState the game-state logger already read at the parked window; here the test owns it.
// --------------------------------------------------------------------------------------------
typedef struct {
	CtlIn in;
} World;

static void w_init(World* w, int px, int py) {
	memset(w, 0, sizeof *w);
	w->in.emuFrame = 1000;
	w->in.fieldValid = true;
	w->in.px = (int16_t)px; w->in.py = (int16_t)py;
	w->in.mapGroup = 3; w->in.mapNum = 7;
	w->in.realKeys = 0;
	w->in.goSeen = false;
}
// One tick: advance the emulated clock by one frame and run the scheduler.
static uint16_t w_step(CtlSched* cs, World* w) {
	uint16_t m = ctl_tick(cs, &w->in);
	w->in.emuFrame++;
	w->in.goSeen = false;   // goSeen is a one-tick pulse from the glue's poll
	return m;
}
// Drain the status ring into one buffer (newline-separated, as the control log would look).
static void w_drain(CtlSched* cs, char* out, size_t cap) {
	out[0] = '\0';
	char line[CTL_STATUS_LEN];
	while (ctl_status(cs, line, sizeof line) > 0) {
		size_t have = strlen(out), add = strlen(line);
		if (have + add + 1 < cap) memcpy(out + have, line, add + 1);
	}
}
static bool has(const char* hay, const char* needle) { return strstr(hay, needle) != NULL; }

// --------------------------------------------------------------------------------------------
// TEST 1 — grammar: every token kind parses to the right kind/mask/count (SPEC D4.5).
// --------------------------------------------------------------------------------------------
static void test_grammar(void) {
	printf("TEST 1: grammar — every token kind parses\n");
	CtlSched cs; char err[80];

	ctl_init(&cs, 0);
	int n = ctl_load(&cs, "L1 R2 U3 D4", err, sizeof err);
	CHECK(n == 4, "walks: got %d (%s)\n", n, err);
	CHECK(cs.tok[0].kind == CTOK_WALK && cs.tok[0].mask == CTL_KEY_LEFT  && cs.tok[0].n == 1, "L1\n");
	CHECK(cs.tok[1].kind == CTOK_WALK && cs.tok[1].mask == CTL_KEY_RIGHT && cs.tok[1].n == 2, "R2\n");
	CHECK(cs.tok[2].kind == CTOK_WALK && cs.tok[2].mask == CTL_KEY_UP    && cs.tok[2].n == 3, "U3\n");
	CHECK(cs.tok[3].kind == CTOK_WALK && cs.tok[3].mask == CTL_KEY_DOWN  && cs.tok[3].n == 4, "D4\n");
	CHECK(cs.state == CTL_RUN && ctl_active(&cs) && !ctl_waiting_go(&cs), "no G -> runs immediately\n");
	CHECK(strcmp(cs.tok[3].text, "D4") == 0, "token text kept for the status lines: '%s'\n", cs.tok[3].text);

	// Lowercase walks SPRINT (mask also holds B — Running Shoes).
	ctl_init(&cs, 1);
	n = ctl_load(&cs, "l5 r6 u7 d8", err, sizeof err);
	CHECK(n == 4, "sprints: got %d (%s)\n", n, err);
	CHECK(cs.tok[0].mask == (CTL_KEY_LEFT  | CTL_KEY_B), "l5 = LEFT|B\n");
	CHECK(cs.tok[1].mask == (CTL_KEY_RIGHT | CTL_KEY_B), "r6 = RIGHT|B\n");
	CHECK(cs.tok[2].mask == (CTL_KEY_UP    | CTL_KEY_B), "u7 = UP|B\n");
	CHECK(cs.tok[3].mask == (CTL_KEY_DOWN  | CTL_KEY_B), "d8 = DOWN|B\n");
	CHECK(cs.tok[0].kind == CTOK_WALK && cs.tok[0].n == 5, "sprint is still a closed-loop walk\n");

	// Bare directions are TAPS — and case does NOT add B on a bare tap (PM: n==0 => tap).
	ctl_init(&cs, 0);
	n = ctl_load(&cs, "L u", err, sizeof err);
	CHECK(n == 2, "bare dirs: got %d (%s)\n", n, err);
	CHECK(cs.tok[0].kind == CTOK_TAPDIR && cs.tok[0].mask == CTL_KEY_LEFT, "bare L = tap LEFT\n");
	CHECK(cs.tok[1].kind == CTOK_TAPDIR && cs.tok[1].mask == CTL_KEY_UP,   "bare u = tap UP, no B\n");

	// Buttons: a b s c x y -> A B START SELECT L R (the GBA has no X/Y).
	ctl_init(&cs, 0);
	n = ctl_load(&cs, "a b s c x y", err, sizeof err);
	CHECK(n == 6, "buttons: got %d (%s)\n", n, err);
	CHECK(cs.tok[0].kind == CTOK_BTN && cs.tok[0].mask == CTL_KEY_A,      "a = A\n");
	CHECK(cs.tok[1].mask == CTL_KEY_B,      "b = B\n");
	CHECK(cs.tok[2].mask == CTL_KEY_START,  "s = START\n");
	CHECK(cs.tok[3].mask == CTL_KEY_SELECT, "c = SELECT\n");
	CHECK(cs.tok[4].mask == CTL_KEY_L,      "x = GBA L (no X on a GBA)\n");
	CHECK(cs.tok[5].mask == CTL_KEY_R,      "y = GBA R (no Y on a GBA)\n");

	// Waits, either case.
	ctl_init(&cs, 0);
	n = ctl_load(&cs, "W120 w60", err, sizeof err);
	CHECK(n == 2, "waits: got %d (%s)\n", n, err);
	CHECK(cs.tok[0].kind == CTOK_WAIT && cs.tok[0].n == 120 && cs.tok[0].mask == 0, "W120\n");
	CHECK(cs.tok[1].kind == CTOK_WAIT && cs.tok[1].n == 60, "w60\n");

	// Mixed whitespace (tabs / CRLF / blank lines) — the run-#13 recipe files are hand-edited.
	ctl_init(&cs, 0);
	n = ctl_load(&cs, "\r\n  G\tW120\r\n D2\n\n W300 \t a \n", err, sizeof err);
	CHECK(n == 4, "mixed whitespace: got %d (%s)\n", n, err);
	CHECK(cs.state == CTL_WAIT_GO && ctl_waiting_go(&cs), "leading G -> WAIT_GO\n");
	CHECK(cs.tok[1].kind == CTOK_WALK && cs.tok[1].n == 2 && cs.tok[1].mask == CTL_KEY_DOWN, "G is not a token slot\n");

	// The Appendix example scripts must parse verbatim (they are the run-#13 recipes).
	ctl_init(&cs, 0);
	CHECK(ctl_load(&cs, "G W120 D2 W300 D1 W600", err, sizeof err) == 5, "example 1 (room exit): %s\n", err);
	ctl_init(&cs, 0);
	CHECK(ctl_load(&cs, "G W60 U3 W60 a W90 a W90 a W90 a W600", err, sizeof err) == 11, "example 2 (re-link): %s\n", err);

	// Max token count exactly at the cap, and one over.
	{
		char big[CTL_FILE_MAX]; big[0] = '\0';
		for (int i = 0; i < CTL_TOK_MAX; i++) strcat(big, "a ");
		ctl_init(&cs, 0);
		CHECK(ctl_load(&cs, big, err, sizeof err) == CTL_TOK_MAX, "exactly %d tokens accepted (%s)\n", CTL_TOK_MAX, err);
		strcat(big, "a");
		ctl_init(&cs, 0);
		CHECK(ctl_load(&cs, big, err, sizeof err) == -1 && has(err, "too many tokens"), "%d+1 tokens rejected: '%s'\n", CTL_TOK_MAX, err);
	}
}

// --------------------------------------------------------------------------------------------
// TEST 2 — grammar errors are LOUD and name the token (D4.3: a parse error consumes the file,
// logs why, and queues NOTHING — the operator re-drops a fixed file).
// --------------------------------------------------------------------------------------------
static void test_grammar_errors(void) {
	printf("TEST 2: grammar errors are loud and specific\n");
	CtlSched cs; char err[80];
	struct { const char* text; const char* want; } BAD[] = {
		{ "L1 Q2 D3",   "bad token 'Q2'" },
		{ "A",          "buttons are lowercase" },   // uppercase button = loud, not a mis-press
		{ "a3",         "bad token 'a3'" },
		{ "D0",         "bad tile count" },          // n must be >= 1
		{ "D256",       "bad tile count" },          // > CTL_WALK_MAX
		{ "Dx",         "expected D<n>" },
		{ "W0",         "bad wait" },
		{ "W3601",      "bad wait" },
		{ "W",          "expected W<n>" },
		{ "D2 G W1",    "G must be the FIRST" },
		{ "",           "empty script" },
		{ "   \n\t ",   "empty script" },
		{ "G",          "empty script" },            // a go-gate with nothing behind it
		{ "D123456789", "too long" },
	};
	for (size_t i = 0; i < sizeof BAD / sizeof BAD[0]; i++) {
		ctl_init(&cs, 0);
		int n = ctl_load(&cs, BAD[i].text, err, sizeof err);
		CHECK(n == -1, "reject '%s': got %d\n", BAD[i].text, n);
		CHECK(has(err, BAD[i].want), "reject '%s': err '%s' should mention '%s'\n", BAD[i].text, err, BAD[i].want);
		CHECK(!ctl_active(&cs) && cs.nTok == 0, "reject '%s': nothing queued\n", BAD[i].text);
	}
	// Over-cap file body (no NUL inside CTL_FILE_MAX+1 bytes).
	{
		static char huge[CTL_FILE_MAX + 64];
		memset(huge, 'a', sizeof huge - 1); huge[sizeof huge - 1] = '\0';
		ctl_init(&cs, 0);
		CHECK(ctl_load(&cs, huge, err, sizeof err) == -1 && has(err, "too large"), "over-cap file rejected: '%s'\n", err);
	}
	// NULL safety + the "seat busy" guard (a live script is never clobbered).
	ctl_init(&cs, 0);
	CHECK(ctl_load(&cs, NULL, err, sizeof err) == -1, "NULL text rejected\n");
	CHECK(ctl_load(NULL, "a", err, sizeof err) == -1, "NULL sched rejected\n");
	CHECK(ctl_load(&cs, "D2", err, sizeof err) == 1, "queue a script\n");
	CHECK(ctl_load(&cs, "U1", err, sizeof err) == -1 && has(err, "busy"), "second load while running refused: '%s'\n", err);
	CHECK(cs.nTok == 1 && cs.tok[0].mask == CTL_KEY_DOWN, "...and the running script is untouched\n");
	CHECK(ctl_load(&cs, "D2", NULL, 0) == -1, "NULL err buffer is safe\n");
}

// --------------------------------------------------------------------------------------------
// TEST 3 — the ABORT file (D4.4): leading '!' returns 0 WITHOUT touching the scheduler; the glue
// logs `ABORT by file` and clears with ctl_abort(cs, NULL).
// --------------------------------------------------------------------------------------------
static void test_abort_file(void) {
	printf("TEST 3: '!' abort file + ctl_abort semantics\n");
	CtlSched cs; char err[80], buf[1024];
	ctl_init(&cs, 0);
	CHECK(ctl_load(&cs, "D2 D2 D2", err, sizeof err) == 3, "queue: %s\n", err);
	CHECK(ctl_load(&cs, "  \n! stop everything", err, sizeof err) == 0, "'!' file returns 0\n");
	CHECK(cs.state == CTL_RUN && cs.nTok == 3, "'!' load alone does NOT clear (the glue does)\n");
	ctl_note(&cs, "ABORT by file");
	ctl_abort(&cs, NULL);          // silent clear — the note above is the operator-facing line
	CHECK(!ctl_active(&cs) && cs.nTok == 0, "ctl_abort cleared the queue\n");
	CHECK(cs.aborts == 1, "abort counted once: %u\n", cs.aborts);
	w_drain(&cs, buf, sizeof buf);
	CHECK(strcmp(buf, "[ctl p1] ABORT by file\n") == 0, "exact abort line: '%s'\n", buf);
	// Idempotent on an idle seat: no line, no counter bump (the glue may call it unconditionally).
	ctl_abort(&cs, "again");
	CHECK(cs.aborts == 1, "abort on an idle seat is free: %u\n", cs.aborts);
	w_drain(&cs, buf, sizeof buf);
	CHECK(buf[0] == '\0', "...and logs nothing: '%s'\n", buf);
	// A '!' file with the bang buried is NOT an abort file (only the first non-ws byte counts).
	ctl_init(&cs, 0);
	CHECK(ctl_load(&cs, "D1 !", err, sizeof err) == -1, "a bang mid-file is a parse error, not an abort\n");
}

// --------------------------------------------------------------------------------------------
// TEST 4 — CLOSED-LOOP WALK with a fake coordinate feed (D4.6). The whole point of the slice:
// the hold ends when the GAME's coordinate reaches the target, not after a frame count.
// --------------------------------------------------------------------------------------------
static void test_walk_closed_loop(void) {
	printf("TEST 4: closed-loop walk convergence (fake coordinate feed)\n");
	CtlSched cs; World w; char err[80], buf[1024];

	// D2 from (10,20): hold DOWN until py == 22, however many frames that takes.
	ctl_init(&cs, 0); w_init(&w, 10, 20);
	CHECK(ctl_load(&cs, "D2", err, sizeof err) == 1, "%s\n", err);
	CHECK(w_step(&cs, &w) == CTL_KEY_DOWN, "start tick already holds DOWN\n");
	CHECK(cs.tx == 10 && cs.ty == 22, "target latched start+n on the moved axis: (%d,%d)\n", cs.tx, cs.ty);
	for (int i = 0; i < 30; i++) CHECK(w_step(&cs, &w) == CTL_KEY_DOWN, "still holding at frame %d (lag is irrelevant)\n", i);
	w.in.py = 21;
	CHECK(w_step(&cs, &w) == CTL_KEY_DOWN, "one tile in -> keep holding\n");
	w.in.py = 22;
	CHECK(w_step(&cs, &w) == 0, "target reached -> release on the completing tick\n");
	CHECK(!ctl_active(&cs), "single-token script finished\n");
	CHECK(cs.toksDone == 1 && cs.timeouts == 0 && cs.aborts == 0, "counters: done=%u to=%u ab=%u\n",
	      cs.toksDone, cs.timeouts, cs.aborts);
	w_drain(&cs, buf, sizeof buf);
	CHECK(has(buf, "[ctl p1] tok 0 D2 start (10,20)->(10,22)"), "start line: '%s'\n", buf);
	CHECK(has(buf, "[ctl p1] tok 0 D2 done at (10,22)"), "done line: '%s'\n", buf);
	CHECK(has(buf, "[ctl p1] script done (1 tokens)"), "script-done line: '%s'\n", buf);

	// Sprint LEFT: the mask carries B the whole way, and the target is start-n on x.
	ctl_init(&cs, 1); w_init(&w, 10, 20);
	CHECK(ctl_load(&cs, "l3", err, sizeof err) == 1, "%s\n", err);
	CHECK(w_step(&cs, &w) == (CTL_KEY_LEFT | CTL_KEY_B), "sprint holds LEFT|B\n");
	CHECK(cs.tx == 7 && cs.ty == 20, "sprint target (%d,%d)\n", cs.tx, cs.ty);
	w.in.px = 8; CHECK(w_step(&cs, &w) == (CTL_KEY_LEFT | CTL_KEY_B), "mid-sprint\n");
	w.in.px = 7; CHECK(w_step(&cs, &w) == 0, "sprint target reached\n");
	CHECK(!ctl_active(&cs), "sprint script done\n");

	// Multi-token choreography: one completion per tick gives every boundary a key release.
	ctl_init(&cs, 0); w_init(&w, 5, 5);
	CHECK(ctl_load(&cs, "D1 R1", err, sizeof err) == 2, "%s\n", err);
	CHECK(w_step(&cs, &w) == CTL_KEY_DOWN, "tok0 holds DOWN\n");
	w.in.py = 6;
	CHECK(w_step(&cs, &w) == 0, "tok0 completes with a released frame\n");
	CHECK(cs.idx == 1 && ctl_active(&cs), "advanced to tok1 (idx=%d)\n", cs.idx);
	CHECK(w_step(&cs, &w) == CTL_KEY_RIGHT, "tok1 starts on the NEXT tick\n");
	CHECK(cs.tx == 6 && cs.ty == 6, "tok1 target is relative to the NEW live tile: (%d,%d)\n", cs.tx, cs.ty);
	w.in.px = 6;
	CHECK(w_step(&cs, &w) == 0, "tok1 completes\n");
	CHECK(!ctl_active(&cs) && cs.toksDone == 2, "both tokens done (%u)\n", cs.toksDone);

	// Cross-axis drift (currents / forced-slide tiles) COMPLETES but says so — diagnostic, not
	// a failure (D4.6).
	ctl_init(&cs, 0); w_init(&w, 10, 20);
	CHECK(ctl_load(&cs, "U2", err, sizeof err) == 1, "%s\n", err);
	(void)w_step(&cs, &w);
	w.in.py = 18; w.in.px = 12;
	CHECK(w_step(&cs, &w) == 0, "moved-axis target reached -> complete despite the drift\n");
	w_drain(&cs, buf, sizeof buf);
	CHECK(has(buf, "cross-drift (2,-2)"), "cross-drift diagnostic: '%s'\n", buf);
	CHECK(has(buf, "tok 0 U2 done at (12,18)"), "still completes: '%s'\n", buf);
}

// --------------------------------------------------------------------------------------------
// TEST 5 — a WARP completes a walk token (D4.7): the map changed, so the target comparison is
// meaningless and the walk achieved its purpose.
// --------------------------------------------------------------------------------------------
static void test_walk_warp(void) {
	printf("TEST 5: warp (map change) completes the walk token\n");
	CtlSched cs; World w; char err[80], buf[1024];
	ctl_init(&cs, 0); w_init(&w, 10, 20);
	CHECK(ctl_load(&cs, "D4 W5", err, sizeof err) == 2, "%s\n", err);
	CHECK(w_step(&cs, &w) == CTL_KEY_DOWN, "walking toward the door\n");
	w.in.py = 21; CHECK(w_step(&cs, &w) == CTL_KEY_DOWN, "one tile in\n");
	// The step onto the warp tile teleports us into a new coordinate space (new map, new tile).
	w.in.mapNum = 9; w.in.px = 2; w.in.py = 40;
	CHECK(w_step(&cs, &w) == 0, "warp completes the token\n");
	CHECK(cs.idx == 1 && ctl_active(&cs), "the script continues with the wait (idx=%d)\n", cs.idx);
	w_drain(&cs, buf, sizeof buf);
	CHECK(has(buf, "tok 0 D4 warp-complete 3.7->3.9"), "warp line names both maps: '%s'\n", buf);
	// Unknown map (-1, sb1 not ready) must NOT read as a warp.
	ctl_init(&cs, 0); w_init(&w, 10, 20);
	w.in.mapGroup = -1; w.in.mapNum = -1;
	CHECK(ctl_load(&cs, "D2", err, sizeof err) == 1, "%s\n", err);
	CHECK(w_step(&cs, &w) == CTL_KEY_DOWN, "start with an unknown map\n");
	w.in.mapGroup = 3; w.in.mapNum = 7;   // the map became known mid-token
	CHECK(w_step(&cs, &w) == CTL_KEY_DOWN, "map becoming KNOWN is not a warp\n");
	CHECK(ctl_active(&cs) && cs.idx == 0, "token still running\n");
}

// --------------------------------------------------------------------------------------------
// TEST 6 — the WALL TIMEOUT (D4.8): a blocked walk stops the WHOLE queue at n*60+240 emulated
// frames, loudly. Every later walk target is start-relative, so continuing would run the rest of
// the choreography from the wrong tile.
// --------------------------------------------------------------------------------------------
static void test_walk_timeout(void) {
	printf("TEST 6: wall timeout aborts the whole queue at n*60+240\n");
	CtlSched cs; World w; char err[80], buf[1024];
	ctl_init(&cs, 0); w_init(&w, 10, 20);
	CHECK(ctl_load(&cs, "D2 R1 a", err, sizeof err) == 3, "%s\n", err);
	uint32_t dl = CTL_WALK_DL(2);
	CHECK(dl == 360u, "deadline formula: %lu\n", (unsigned long)dl);
	uint32_t held = 0;
	for (uint32_t i = 0; i < dl; i++) {   // the wall never moves us
		if (w_step(&cs, &w) == CTL_KEY_DOWN) held++;
	}
	CHECK(held == dl, "held the direction for every pre-deadline frame: %lu/%lu\n",
	      (unsigned long)held, (unsigned long)dl);
	CHECK(ctl_active(&cs), "still running one frame before the deadline\n");
	CHECK(w_step(&cs, &w) == 0, "deadline tick releases\n");
	CHECK(!ctl_active(&cs), "the WHOLE queue aborted (not skip-to-next)\n");
	CHECK(cs.timeouts == 1 && cs.aborts == 1, "counters: to=%u ab=%u\n", cs.timeouts, cs.aborts);
	w_drain(&cs, buf, sizeof buf);
	CHECK(has(buf, "[ctl p1] TIMEOUT tok 0 D2 at (10,20)"), "timeout line names token + tile: '%s'\n", buf);
	CHECK(!has(buf, "script done"), "an aborted script never reports 'done': '%s'\n", buf);
	// The deadline is armed at the REAL start (after the no-field wait), not at pickup.
	ctl_init(&cs, 0); w_init(&w, 10, 20);
	w.in.fieldValid = false;
	CHECK(ctl_load(&cs, "D1", err, sizeof err) == 1, "%s\n", err);
	for (int i = 0; i < 100; i++) CHECK(w_step(&cs, &w) == 0, "no injection while off-field (i=%d)\n", i);
	w.in.fieldValid = true;
	CHECK(w_step(&cs, &w) == CTL_KEY_DOWN, "starts when the field snapshot goes valid\n");
	CHECK(cs.deadline == w.in.emuFrame - 1u + CTL_WALK_DL(1), "deadline armed at the real start: %lu\n",
	      (unsigned long)cs.deadline);
}

// --------------------------------------------------------------------------------------------
// TEST 7 — the NO-FIELD guard (D4.9): a walk token waits for a live field snapshot, and gives up
// loudly after CTL_NOFIELD_DL emulated frames. Taps and waits have NO such precondition (they are
// used inside menus deliberately).
// --------------------------------------------------------------------------------------------
static void test_no_field(void) {
	printf("TEST 7: no-field wait then loud abort; taps/waits are menu-legal\n");
	CtlSched cs; World w; char err[80], buf[1024];
	ctl_init(&cs, 0); w_init(&w, 10, 20);
	w.in.fieldValid = false;
	CHECK(ctl_load(&cs, "D3", err, sizeof err) == 1, "%s\n", err);
	for (uint32_t i = 0; i < CTL_NOFIELD_DL; i++) {
		CHECK(w_step(&cs, &w) == 0, "off-field injects nothing (i=%lu)\n", (unsigned long)i);
		if (!ctl_active(&cs)) break;
	}
	CHECK(ctl_active(&cs), "still waiting one frame before the cap\n");
	CHECK(w_step(&cs, &w) == 0, "cap tick\n");
	CHECK(!ctl_active(&cs), "no-field abort fired at the cap\n");
	CHECK(cs.aborts == 1 && cs.timeouts == 0, "counted as an abort, not a timeout: ab=%u to=%u\n", cs.aborts, cs.timeouts);
	w_drain(&cs, buf, sizeof buf);
	CHECK(has(buf, "[ctl p1] NO-FIELD tok 0 D3"), "no-field line: '%s'\n", buf);

	// Taps + waits run happily with fieldValid == false (that IS the menu case).
	ctl_init(&cs, 0); w_init(&w, 10, 20);
	w.in.fieldValid = false; w.in.px = -1; w.in.py = -1;
	CHECK(ctl_load(&cs, "a W2 L", err, sizeof err) == 3, "%s\n", err);
	CHECK(w_step(&cs, &w) == CTL_KEY_A, "a button tap fires inside a menu\n");
	for (int i = 1; i < CTL_TAP_BTN_SLOT; i++) (void)w_step(&cs, &w);
	CHECK(w_step(&cs, &w) == 0 && cs.idx == 1, "tap done, on to the wait (idx=%d)\n", cs.idx);
	CHECK(ctl_active(&cs), "the script survives an off-field menu\n");
}

// --------------------------------------------------------------------------------------------
// TEST 8 — tap / button press+slot windows and W<n> (D4.5). Frame-exact, counted in EMULATED
// frames so a wireless ~4-5 emu-fps session presses for the same number of GAME frames.
// --------------------------------------------------------------------------------------------
static void test_taps_waits(void) {
	printf("TEST 8: tap press/slot windows + W<n>\n");
	CtlSched cs; World w; char err[80];

	// Bare direction: pressed CTL_TAP_DIR_PRESS of a CTL_TAP_DIR_SLOT slot.
	ctl_init(&cs, 0); w_init(&w, 1, 1);
	CHECK(ctl_load(&cs, "R", err, sizeof err) == 1, "%s\n", err);
	for (int i = 0; i < CTL_TAP_DIR_SLOT; i++) {
		uint16_t m = w_step(&cs, &w);
		uint16_t want = (i < CTL_TAP_DIR_PRESS) ? (uint16_t)CTL_KEY_RIGHT : (uint16_t)0;
		CHECK(m == want, "dir tap frame %d: got %04X want %04X\n", i, m, want);
	}
	CHECK(ctl_active(&cs), "slot not yet elapsed at frame %d\n", CTL_TAP_DIR_SLOT - 1);
	CHECK(w_step(&cs, &w) == 0, "slot tick releases + completes\n");
	CHECK(!ctl_active(&cs) && cs.toksDone == 1, "tap token done\n");

	// Button tap: pressed CTL_TAP_BTN_PRESS of a CTL_TAP_BTN_SLOT slot.
	ctl_init(&cs, 0); w_init(&w, 1, 1);
	CHECK(ctl_load(&cs, "a", err, sizeof err) == 1, "%s\n", err);
	for (int i = 0; i < CTL_TAP_BTN_SLOT; i++) {
		uint16_t m = w_step(&cs, &w);
		uint16_t want = (i < CTL_TAP_BTN_PRESS) ? (uint16_t)CTL_KEY_A : (uint16_t)0;
		CHECK(m == want, "btn tap frame %d: got %04X want %04X\n", i, m, want);
	}
	CHECK(w_step(&cs, &w) == 0 && !ctl_active(&cs), "button slot completes\n");

	// W<n>: injects nothing for exactly n emulated frames, then completes.
	ctl_init(&cs, 0); w_init(&w, 1, 1);
	CHECK(ctl_load(&cs, "W3 b", err, sizeof err) == 2, "%s\n", err);
	for (int i = 0; i < 3; i++) {
		CHECK(w_step(&cs, &w) == 0, "wait frame %d injects nothing\n", i);
		CHECK(cs.idx == 0, "wait frame %d still on tok0\n", i);
	}
	CHECK(w_step(&cs, &w) == 0, "wait completes silently\n");
	CHECK(cs.idx == 1, "advanced past the wait (idx=%d)\n", cs.idx);
	CHECK(w_step(&cs, &w) == CTL_KEY_B, "the next token starts after the wait\n");
}

// --------------------------------------------------------------------------------------------
// TEST 9 — the GO-GATE (D4.10): a `G` script parses + queues, injects NOTHING, and starts on the
// tick the glue reports go_p<N>.txt was consumed.
// --------------------------------------------------------------------------------------------
static void test_go_gate(void) {
	printf("TEST 9: go-gate holds until goSeen\n");
	CtlSched cs; World w; char err[80], buf[1024];
	ctl_init(&cs, 0); w_init(&w, 10, 20);
	CHECK(ctl_load(&cs, "G D1 a", err, sizeof err) == 2, "%s\n", err);
	CHECK(ctl_waiting_go(&cs) && ctl_active(&cs), "waiting on the go file\n");
	for (int i = 0; i < 50; i++) CHECK(w_step(&cs, &w) == 0, "no injection before GO (i=%d)\n", i);
	CHECK(cs.idx == 0 && !cs.tokStarted, "token 0 has not even latched\n");
	// The operator stages the scene BY HAND while a G-script waits: real input must NOT abort it
	// (the deliberate SPEC-D4.11 scope call — see the ctl_tick comment / BUILDLOG).
	w.in.realKeys = CTL_KEY_UP;
	for (int i = 0; i < 10; i++) CHECK(w_step(&cs, &w) == 0, "staging input while gated (i=%d)\n", i);
	CHECK(ctl_waiting_go(&cs) && cs.aborts == 0, "hand-staging does NOT abort a gated script\n");
	w.in.realKeys = 0;
	uint32_t goFrame = w.in.emuFrame;
	w.in.goSeen = true;
	CHECK(w_step(&cs, &w) == CTL_KEY_DOWN, "GO starts token 0 on that very tick\n");
	CHECK(cs.state == CTL_RUN && !ctl_waiting_go(&cs), "state moved to RUN\n");
	w_drain(&cs, buf, sizeof buf);
	{
		char want[64]; snprintf(want, sizeof want, "[ctl p1] GO f=%lu\n", (unsigned long)goFrame);
		CHECK(has(buf, want), "exact GO line ('%s' in '%s')\n", want, buf);
	}
	// goSeen with no waiting script is inert (the glue leaves such a go file in place).
	ctl_init(&cs, 0); w_init(&w, 10, 20);
	w.in.goSeen = true;
	CHECK(w_step(&cs, &w) == 0 && !ctl_active(&cs), "goSeen on an idle seat does nothing\n");
}

// --------------------------------------------------------------------------------------------
// TEST 10 — REAL INPUT WINS (D4.11): any key routed to this seat aborts its script immediately
// and injects nothing that frame. The human always outranks the harness.
// --------------------------------------------------------------------------------------------
static void test_real_input_abort(void) {
	printf("TEST 10: real input aborts the running script\n");
	CtlSched cs; World w; char err[80], buf[1024];
	ctl_init(&cs, 1); w_init(&w, 10, 20);
	CHECK(ctl_load(&cs, "D5 R2", err, sizeof err) == 2, "%s\n", err);
	CHECK(w_step(&cs, &w) == CTL_KEY_DOWN, "running\n");
	w.in.realKeys = CTL_KEY_A;
	CHECK(w_step(&cs, &w) == 0, "abort tick injects NOTHING (no key fight)\n");
	CHECK(!ctl_active(&cs) && cs.nTok == 0, "script + queue cleared\n");
	CHECK(cs.aborts == 1, "abort counted: %u\n", cs.aborts);
	w_drain(&cs, buf, sizeof buf);
	CHECK(has(buf, "[ctl p2] ABORT real-input at tok 0"), "seat-2 abort line: '%s'\n", buf);
	// ...and it keeps injecting nothing afterwards.
	w.in.realKeys = 0;
	CHECK(w_step(&cs, &w) == 0, "idle after abort\n");
	// Mid-script (token 1) the line names the RIGHT token.
	ctl_init(&cs, 0); w_init(&w, 0, 0);
	CHECK(ctl_load(&cs, "W1 W1 W1", err, sizeof err) == 3, "%s\n", err);
	(void)w_step(&cs, &w); (void)w_step(&cs, &w);   // finish tok0
	CHECK(cs.idx == 1, "on tok1 (idx=%d)\n", cs.idx);
	w.in.realKeys = CTL_KEY_START;
	(void)w_step(&cs, &w);
	w_drain(&cs, buf, sizeof buf);
	CHECK(has(buf, "ABORT real-input at tok 1"), "names the live token: '%s'\n", buf);
}

// --------------------------------------------------------------------------------------------
// TEST 11 — FROZEN CLOCK (SPEC §0): pause menu open / app backgrounded / a paused wireless peer
// seat all stop the core's emulated frame counter. The script must FREEZE in place — no token
// progress, no deadline expiry, no blind firing.
// --------------------------------------------------------------------------------------------
static void test_frozen_clock(void) {
	printf("TEST 11: a frozen emulated clock freezes the script (no progress, no timeout)\n");
	CtlSched cs; World w; char err[80];

	// A walk: the hold persists, the deadline never expires while the clock is stopped.
	ctl_init(&cs, 0); w_init(&w, 10, 20);
	CHECK(ctl_load(&cs, "D1", err, sizeof err) == 1, "%s\n", err);
	CHECK(ctl_tick(&cs, &w.in) == CTL_KEY_DOWN, "started\n");
	uint32_t frozen = w.in.emuFrame;
	for (int i = 0; i < 5000; i++)   // 5000 render frames >> CTL_WALK_DL(1) = 300 emulated frames
		CHECK(ctl_tick(&cs, &w.in) == CTL_KEY_DOWN, "frozen render frame %d still holds\n", i);
	CHECK(ctl_active(&cs) && cs.timeouts == 0, "no timeout while the emulated clock is stopped\n");
	CHECK(w.in.emuFrame == frozen, "the test really did freeze the clock\n");
	w.in.emuFrame += CTL_WALK_DL(1);   // clock resumes, straight past the deadline
	CHECK(ctl_tick(&cs, &w.in) == 0, "the deadline fires once the clock moves\n");
	CHECK(cs.timeouts == 1, "timeout counted after the resume: %u\n", cs.timeouts);

	// A tap: the press window is emulated frames, so a frozen clock holds the press.
	ctl_init(&cs, 0); w_init(&w, 1, 1);
	CHECK(ctl_load(&cs, "a", err, sizeof err) == 1, "%s\n", err);
	for (int i = 0; i < 200; i++) CHECK(ctl_tick(&cs, &w.in) == CTL_KEY_A, "frozen tap frame %d\n", i);
	CHECK(cs.idx == 0 && ctl_active(&cs), "no tap progress on a frozen clock\n");

	// A no-field wait: same rule, the 600-frame cap is emulated frames.
	ctl_init(&cs, 0); w_init(&w, 1, 1);
	w.in.fieldValid = false;
	CHECK(ctl_load(&cs, "U1", err, sizeof err) == 1, "%s\n", err);
	for (int i = 0; i < 2000; i++) (void)ctl_tick(&cs, &w.in);
	CHECK(ctl_active(&cs) && cs.aborts == 0, "no no-field abort on a frozen clock\n");
}

// --------------------------------------------------------------------------------------------
// TEST 12 — status ring + counters + the netlog mirror (D4.13/D4.14).
// --------------------------------------------------------------------------------------------
static void test_status_and_counters(void) {
	printf("TEST 12: status ring FIFO/overflow, counters, netlog mirror\n");
	CtlSched cs; char line[CTL_STATUS_LEN];
	ctl_init(&cs, 0);
	CHECK(ctl_status(&cs, line, sizeof line) == 0, "empty ring drains 0\n");

	for (int i = 0; i < CTL_STATUS_N; i++) { char m[32]; snprintf(m, sizeof m, "line %d", i); ctl_note(&cs, m); }
	CHECK(cs.rCount == CTL_STATUS_N, "ring full: %u\n", cs.rCount);
	for (int i = 0; i < CTL_STATUS_N; i++) {
		char want[64]; snprintf(want, sizeof want, "[ctl p1] line %d\n", i);
		int n = ctl_status(&cs, line, sizeof line);
		CHECK(n == (int)strlen(want) && strcmp(line, want) == 0, "FIFO order %d: '%s' vs '%s'\n", i, line, want);
	}
	CHECK(cs.rCount == 0, "fully drained\n");

	// Overflow drops the OLDEST (the newest lines explain the current state).
	for (int i = 0; i < CTL_STATUS_N + 5; i++) { char m[32]; snprintf(m, sizeof m, "L%d", i); ctl_note(&cs, m); }
	CHECK(cs.rCount == CTL_STATUS_N, "still capped at %d: %u\n", CTL_STATUS_N, cs.rCount);
	(void)ctl_status(&cs, line, sizeof line);
	CHECK(strcmp(line, "[ctl p1] L5\n") == 0, "oldest 5 dropped, first surviving is L5: '%s'\n", line);
	while (ctl_status(&cs, line, sizeof line) > 0) { }
	CHECK(strcmp(line, "[ctl p1] L20\n") == 0, "newest kept: '%s'\n", line);

	// Over-long notes truncate, never overflow, and always end in exactly one '\n'.
	{
		char big[512]; memset(big, 'Z', sizeof big - 1); big[sizeof big - 1] = '\0';
		ctl_note(&cs, big);
		int n = ctl_status(&cs, line, sizeof line);
		CHECK(n > 0 && n <= CTL_STATUS_LEN - 1, "truncated length %d\n", n);
		CHECK(line[n - 1] == '\n' && line[n] == '\0', "terminated with one newline\n");
		CHECK(strncmp(line, "[ctl p1] ZZZ", 12) == 0, "prefix survives truncation: '%.16s'\n", line);
	}
	// A short output buffer truncates safely too.
	{
		char tiny[6];
		ctl_note(&cs, "hello");
		int n = ctl_status(&cs, tiny, sizeof tiny);
		CHECK(n == 5 && tiny[5] == '\0', "tiny drain: n=%d '%s'\n", n, tiny);
		CHECK(cs.rCount == 0, "the line is consumed even when truncated\n");
	}
	// NULL safety.
	CHECK(ctl_status(NULL, line, sizeof line) == 0, "NULL sched drains 0\n");
	CHECK(ctl_status(&cs, NULL, 8) == 0, "NULL buffer drains 0\n");
	ctl_note(NULL, "x");
	CHECK(!ctl_active(NULL) && !ctl_waiting_go(NULL), "NULL sched is inactive\n");
	CHECK(ctl_tick(NULL, NULL) == 0, "NULL tick is 0\n");

	// Counters + the '# control' netlog mirror.
	CtlSched a; World w; char err[80];
	ctl_init(&a, 1); w_init(&w, 3, 3);
	CHECK(ctl_load(&a, "W1 D1", err, sizeof err) == 2, "%s\n", err);
	(void)w_step(&a, &w); (void)w_step(&a, &w);          // wait runs + completes
	(void)w_step(&a, &w); w.in.py = 4; (void)w_step(&a, &w);   // walk runs + completes
	uint16_t td = 0, ab = 0, to = 0, pk = 0;
	ctl_counters(&a, &td, &ab, &to, &pk);
	CHECK(td == 2 && ab == 0 && to == 0 && pk == 1, "counters: done=%u ab=%u to=%u pick=%u\n", td, ab, to, pk);
	ctl_counters(&a, NULL, NULL, NULL, NULL);            // NULL out-params are legal
	ctl_publish(&a);
	CHECK(g_ctlStat[1].toksDone == 2 && g_ctlStat[1].pickups == 1 && g_ctlStat[1].state == CTL_IDLE,
	      "mirror: done=%u pick=%u st=%u\n", g_ctlStat[1].toksDone, g_ctlStat[1].pickups, g_ctlStat[1].state);
	ctl_counters(NULL, &td, &ab, &to, &pk);
	CHECK(td == 0 && ab == 0 && to == 0 && pk == 0, "NULL sched zeroes the counters\n");
	ctl_publish(NULL);   // must not crash

	// A second pickup on the same seat accumulates (counters are per-session, not per-script).
	CHECK(ctl_load(&a, "a", err, sizeof err) == 1, "%s\n", err);
	ctl_counters(&a, &td, NULL, NULL, &pk);
	CHECK(pk == 2 && td == 2, "second pickup: pick=%u done=%u\n", pk, td);
}

// --------------------------------------------------------------------------------------------
// TEST 13 — the two SEATS are fully independent (each console stages its own move_p<N>.txt; the
// glue runs one scheduler per game slot, p1 = game A, p2 = game B, fixed).
// --------------------------------------------------------------------------------------------
static void test_two_seats(void) {
	printf("TEST 13: the two seats are independent\n");
	CtlSched p1, p2; World w1, w2; char err[80], buf[1024];
	ctl_init(&p1, 0); ctl_init(&p2, 1);
	w_init(&w1, 10, 20); w_init(&w2, 4, 4);
	CHECK(ctl_load(&p1, "D1", err, sizeof err) == 1, "%s\n", err);
	CHECK(ctl_load(&p2, "R2", err, sizeof err) == 1, "%s\n", err);
	CHECK(w_step(&p1, &w1) == CTL_KEY_DOWN,  "p1 walks down\n");
	CHECK(w_step(&p2, &w2) == CTL_KEY_RIGHT, "p2 walks right\n");
	// Aborting p1 leaves p2 alone.
	w1.in.realKeys = CTL_KEY_B;
	(void)w_step(&p1, &w1);
	CHECK(!ctl_active(&p1) && ctl_active(&p2), "p1 aborted, p2 still running\n");
	CHECK(w_step(&p2, &w2) == CTL_KEY_RIGHT, "p2 keeps holding\n");
	w_drain(&p1, buf, sizeof buf);
	CHECK(has(buf, "[ctl p1]"), "p1 lines are tagged p1: '%s'\n", buf);
	w_drain(&p2, buf, sizeof buf);
	CHECK(has(buf, "[ctl p2]") && !has(buf, "[ctl p1]"), "p2 lines are tagged p2: '%s'\n", buf);
	w2.in.px = 6;
	CHECK(w_step(&p2, &w2) == 0 && !ctl_active(&p2), "p2 completes on its own coordinate feed\n");
}

// ============================================================================================
// D5 — INPUT RECORD / REPLAY (SPEC-control-replay.md §D5)
// ============================================================================================
// The two halves are driven through the same shape of synthetic tick stream the glue feeds on
// hardware: `pre` ticks OFF-field (so the anchor has a real edge to find), then `n` field ticks
// with a scripted mask timeline, the emulated clock advancing `emuStep` frames per tick (1 =
// unlinked 60 fps, 12+ = the wireless ~4-5 emu-fps case, 0 = a frozen clock/pause).
// CtlRep is ~50 KB (the 8192-entry table) -> every instance here is static, never a stack frame.

static void ci_init(CtlIn* in, uint32_t frame) {
	memset(in, 0, sizeof *in);
	in->emuFrame = frame;
	in->px = 5; in->py = 5; in->mapGroup = 3; in->mapNum = 7;
	in->fieldValid = false;
}

// Drive the recorder: `pre` off-field ticks, then the mask timeline. Emitted lines are appended
// to `text` exactly as the glue would append them to the file. Returns the byte length.
static int rec_drive(CtlRec* r, uint32_t start, int pre, const uint16_t* masks, int n,
                     int emuStep, char* text, size_t cap) {
	CtlIn in; ci_init(&in, start);
	size_t len = 0; if (cap) text[0] = '\0';
	char line[CTL_REC_LINE];
	for (int i = 0; i < pre; i++) {
		(void)ctl_rec_tick(r, &in, 0, line, sizeof line);
		in.emuFrame += (uint32_t)emuStep;
	}
	in.fieldValid = true;
	for (int i = 0; i < n; i++) {
		int L = ctl_rec_tick(r, &in, masks[i], line, sizeof line);
		if (L > 0 && len + (size_t)L + 1 < cap) { memcpy(text + len, line, (size_t)L); len += (size_t)L; text[len] = '\0'; }
		in.emuFrame += (uint32_t)emuStep;
	}
	return (int)len;
}

// Drive the replayer over the same tick shape; out[i] = the mask injected on field tick i.
static void rep_drive(CtlRep* r, uint32_t start, int pre, uint16_t* out, int n, int emuStep) {
	CtlIn in; ci_init(&in, start);
	for (int i = 0; i < pre; i++) { (void)ctl_rep_tick(r, &in); in.emuFrame += (uint32_t)emuStep; }
	in.fieldValid = true;
	for (int i = 0; i < n; i++) { out[i] = ctl_rep_tick(r, &in); in.emuFrame += (uint32_t)emuStep; }
}

static void rec_drain(CtlRec* r, char* out, size_t cap) {
	out[0] = '\0';
	char line[CTL_STATUS_LEN];
	while (ctl_rec_status(r, line, sizeof line) > 0) {
		size_t have = strlen(out), add = strlen(line);
		if (have + add + 1 < cap) memcpy(out + have, line, add + 1);
	}
}
static void rep_drain(CtlRep* r, char* out, size_t cap) {
	out[0] = '\0';
	char line[CTL_STATUS_LEN];
	while (ctl_rep_status(r, line, sizeof line) > 0) {
		size_t have = strlen(out), add = strlen(line);
		if (have + add + 1 < cap) memcpy(out + have, line, add + 1);
	}
}

// --------------------------------------------------------------------------------------------
// TEST 14 — RECORD: the field-entry EDGE anchor (D5.2), first line AT the anchor, ON-CHANGE only
// (D5.3), offsets in EMULATED frames, and a frozen clock adding nothing (D5.5).
// --------------------------------------------------------------------------------------------
static void test_record_basic(void) {
	printf("TEST 14: record — edge anchor, on-change lines, emulated-frame offsets\n");
	static CtlRec r; char text[512], buf[1024];
	ctl_rec_init(&r, 0);
	CHECK(!ctl_rec_armed(&r) && !ctl_rec_anchored(&r), "a fresh recorder is idle\n");

	// Unarmed: never emits, whatever happens on screen.
	{
		CtlIn in; ci_init(&in, 100); in.fieldValid = true;
		char line[CTL_REC_LINE];
		CHECK(ctl_rec_tick(&r, &in, CTL_KEY_A, line, sizeof line) == 0, "unarmed records nothing\n");
	}

	ctl_rec_arm(&r);
	CHECK(ctl_rec_armed(&r) && !ctl_rec_anchored(&r), "armed, hunting the anchor\n");

	// Two off-field ticks (the edge), then the timeline. The anchor needs CTL_ANCHOR_FRAMES
	// consecutive field ticks, so the first line lands on field tick index 2.
	static const uint16_t TL[] = {
		0, 0,                                  // ticks 0-1: debounce, nothing recorded yet
		0,                                     // tick 2: ANCHOR -> first line "0 0000"
		CTL_KEY_DOWN, CTL_KEY_DOWN, CTL_KEY_DOWN,   // held: ONE line at the change
		0,                                     // release: one more line
		CTL_KEY_A, 0                           // a tap: two more lines
	};
	int n = (int)(sizeof TL / sizeof TL[0]);
	int len = rec_drive(&r, 1000, 2, TL, n, 1, text, sizeof text);
	CHECK(len > 0 && ctl_rec_anchored(&r), "anchored + wrote lines (%d bytes)\n", len);
	CHECK(ctl_rec_anchor_frame(&r) == 1000u + 2u + 2u, "anchor frame = the 3rd field tick: %lu\n",
	      (unsigned long)ctl_rec_anchor_frame(&r));
	CHECK(ctl_rec_lines(&r) == 5, "exactly 5 ON-CHANGE lines: %lu\n", (unsigned long)ctl_rec_lines(&r));
	CHECK(strcmp(text,
	             "0 0000\n"      // at the anchor, current mask
	             "1 0080\n"      // DOWN pressed one frame later
	             "4 0000\n"      // released after 3 frames of hold
	             "5 0001\n"      // A
	             "6 0000\n") == 0, "exact recorded timeline:\n%s", text);
	rec_drain(&r, buf, sizeof buf);
	CHECK(has(buf, "[ctl p1] record armed"), "arm line: '%s'\n", buf);
	CHECK(has(buf, "[ctl p1] record anchored f=1004"), "anchor line: '%s'\n", buf);

	// A FROZEN clock (emuStep 0 = pause menu / paused peer seat) adds no lines while the mask is
	// unchanged, and never inflates the offsets: the next change after the resume keeps counting
	// emulated frames.
	{
		static CtlRec r2; ctl_rec_init(&r2, 1); ctl_rec_arm(&r2);
		CtlIn in; ci_init(&in, 500);
		char line[CTL_REC_LINE];
		for (int i = 0; i < 2; i++) { (void)ctl_rec_tick(&r2, &in, 0, line, sizeof line); in.emuFrame++; }
		in.fieldValid = true;
		for (int i = 0; i < 3; i++) { (void)ctl_rec_tick(&r2, &in, 0, line, sizeof line); in.emuFrame++; }
		CHECK(ctl_rec_lines(&r2) == 1 && ctl_rec_anchored(&r2), "anchored, one line so far\n");
		for (int i = 0; i < 300; i++) CHECK(ctl_rec_tick(&r2, &in, 0, line, sizeof line) == 0,
		                                    "frozen clock, unchanged mask: no line (i=%d)\n", i);
		CHECK(ctl_rec_lines(&r2) == 1, "still one line after 300 frozen ticks: %lu\n",
		      (unsigned long)ctl_rec_lines(&r2));
		in.emuFrame += 10;   // the clock resumes 10 emulated frames on
		int L = ctl_rec_tick(&r2, &in, CTL_KEY_B, line, sizeof line);
		// anchor was at frame 504 (3rd field tick); 300 FROZEN ticks add nothing; the clock
		// resumes 10 frames on at 515 -> offset 11, NOT 311.
		CHECK(L > 0 && strcmp(line, "11 0002\n") == 0, "offset counts EMULATED frames only: '%s'\n", line);
	}
}

// --------------------------------------------------------------------------------------------
// TEST 15 — RECORD: armed ON-FIELD waits (loudly) for a real edge; stop/re-arm; the line cap
// returns -1 exactly once; the file header is well formed and is skipped by the loader.
// --------------------------------------------------------------------------------------------
static void test_record_edge_stop_cap(void) {
	printf("TEST 15: record — on-field arming, stop/re-arm, cap, header\n");
	static CtlRec r; char buf[1024], line[CTL_REC_LINE];

	// Armed while ALREADY on the field: no anchor until the state leaves and returns.
	ctl_rec_init(&r, 0); ctl_rec_arm(&r);
	CtlIn in; ci_init(&in, 10); in.fieldValid = true;
	for (int i = 0; i < 100; i++) { CHECK(ctl_rec_tick(&r, &in, CTL_KEY_A, line, sizeof line) == 0,
	                                     "no anchor without an edge (i=%d)\n", i); in.emuFrame++; }
	CHECK(!ctl_rec_anchored(&r) && ctl_rec_lines(&r) == 0, "still hunting the edge\n");
	rec_drain(&r, buf, sizeof buf);
	CHECK(has(buf, "record armed ON-FIELD"), "the wait is LOUD, once: '%s'\n", buf);
	in.fieldValid = false; (void)ctl_rec_tick(&r, &in, 0, line, sizeof line); in.emuFrame++;
	in.fieldValid = true;
	for (int i = 0; i < CTL_ANCHOR_FRAMES - 1; i++) {
		CHECK(ctl_rec_tick(&r, &in, 0, line, sizeof line) == 0, "debounce tick %d\n", i);
		in.emuFrame++;
	}
	CHECK(ctl_rec_tick(&r, &in, 0, line, sizeof line) > 0, "the edge anchors after the debounce\n");
	CHECK(ctl_rec_anchored(&r), "anchored\n");

	// Stop (the '!' abort file / session end, D5.5) then re-arm: fresh offsets, fresh line count.
	ctl_rec_stop(&r, "abort file");
	CHECK(!ctl_rec_armed(&r) && !ctl_rec_anchored(&r), "stopped\n");
	in.emuFrame += 5;
	CHECK(ctl_rec_tick(&r, &in, CTL_KEY_A, line, sizeof line) == 0, "a stopped recorder is silent\n");
	rec_drain(&r, buf, sizeof buf);
	CHECK(has(buf, "record stopped: abort file"), "stop line: '%s'\n", buf);
	ctl_rec_stop(&r, "again");   // idempotent
	rec_drain(&r, buf, sizeof buf);
	CHECK(buf[0] == '\0', "stopping an idle recorder logs nothing: '%s'\n", buf);
	ctl_rec_arm(&r);
	CHECK(ctl_rec_lines(&r) == 0, "re-arm resets the line count\n");

	// The cap (D5.3): -1 EXACTLY once, then silence.
	{
		static CtlRec c; ctl_rec_init(&c, 1); ctl_rec_arm(&c);
		CtlIn ci; ci_init(&ci, 0);
		for (int i = 0; i < 2; i++) { (void)ctl_rec_tick(&c, &ci, 0, line, sizeof line); ci.emuFrame++; }
		ci.fieldValid = true;
		for (int i = 0; i < CTL_ANCHOR_FRAMES; i++) { (void)ctl_rec_tick(&c, &ci, 0, line, sizeof line); ci.emuFrame++; }
		c.lines = CTL_REC_MAX_LINES;                     // white-box: jump to the cap
		int L = ctl_rec_tick(&c, &ci, CTL_KEY_A, line, sizeof line);
		CHECK(L == -1, "cap returns -1: %d\n", L);
		CHECK(!ctl_rec_armed(&c), "...and recording stopped\n");
		ci.emuFrame++;
		CHECK(ctl_rec_tick(&c, &ci, CTL_KEY_B, line, sizeof line) == 0, "-1 happens exactly once\n");
		rec_drain(&c, buf, sizeof buf);
		CHECK(has(buf, "record cap"), "cap line: '%s'\n", buf);
	}

	// The header (D5.3): every line starts with '#', names the seat/game/anchor, and the whole
	// file (header + body) loads as a replay table — that is what makes a recording directly
	// replayable (copy it to replay_p<N>.txt).
	{
		char hdr[768];
		int hl = ctl_rec_header(&r, hdr, sizeof hdr, "BPEE", "0803_191500");
		CHECK(hl > 0 && (size_t)hl == strlen(hdr), "header length %d\n", hl);
		CHECK(has(hdr, "# 3DGBA rec v1 seat=p1 game=BPEE anchored="), "header identity: '%.64s'\n", hdr);
		CHECK(has(hdr, "A0 B1 SELECT2 START3 RIGHT4 LEFT5 UP6 DOWN7 R8 L9"), "mask layout documented\n");
		CHECK(has(hdr, "# save:"), "operator free-text line present\n");
		for (const char* p = hdr; *p; ) {                 // EVERY line must be a '#' comment
			CHECK(*p == '#', "header line starts with '#': '%.16s'\n", p);
			while (*p && *p != '\n') p++;
			if (*p == '\n') p++;
		}
		char whole[1280]; snprintf(whole, sizeof whole, "%s0 0000\n5 0080\n", hdr);
		static CtlRep rp; ctl_rep_init(&rp, 0); char err[96];
		CHECK(ctl_rep_load(&rp, whole, err, sizeof err) == 2, "header+body loads: %s\n", err);
		// NULL-safe formatting (the glue may not know the game code yet).
		CHECK(ctl_rec_header(NULL, hdr, sizeof hdr, NULL, NULL) > 0 && has(hdr, "game=----"),
		      "NULL args are safe: '%.48s'\n", hdr);
		CHECK(ctl_rec_header(&r, hdr, 0, "BPEE", "x") == 0, "zero cap writes nothing\n");
	}
	CHECK(ctl_rec_tick(NULL, NULL, 0, line, sizeof line) == 0, "NULL recorder is silent\n");
	ctl_rec_arm(NULL); ctl_rec_stop(NULL, "x"); ctl_rec_init(NULL, 0);   // must not crash
	CHECK(!ctl_rec_armed(NULL) && !ctl_rec_anchored(NULL) && ctl_rec_lines(NULL) == 0 &&
	      ctl_rec_anchor_frame(NULL) == 0, "NULL accessors are 0\n");
}

// --------------------------------------------------------------------------------------------
// TEST 16 — REPLAY table loader (D5.6): '#'/blank lines skipped, hex forms, and EVERY failure
// mode LOUD with nothing armed (an over-cap or reordered table would replay the wrong inputs).
// --------------------------------------------------------------------------------------------
static void test_replay_load(void) {
	printf("TEST 16: replay table loader — skips comments, fails loudly\n");
	static CtlRep r; char err[96];

	ctl_rep_init(&r, 0);
	int n = ctl_rep_load(&r,
		"# 3DGBA rec v1 seat=p1 game=BPEE anchored=1234\n"
		"# save: parked at the cable club\n"
		"\n"
		"0 0000\n"
		"  12   0080  \n"      // leading/trailing/extra whitespace is fine
		"30 0x1\n"             // 0x prefix tolerated
		"30 000a\n"            // same frame twice: legal (the loop takes the latest)
		"9999 03FF",           // no trailing newline
		err, sizeof err);
	CHECK(n == 5, "loaded 5 entries: got %d (%s)\n", n, err);
	CHECK(ctl_rep_active(&r), "a loaded table is armed (hunting the anchor)\n");
	CHECK(r.f[0] == 0 && r.mask[0] == 0x0000, "entry 0\n");
	CHECK(r.f[1] == 12 && r.mask[1] == CTL_KEY_DOWN, "entry 1 (%lu,%04X)\n", (unsigned long)r.f[1], r.mask[1]);
	CHECK(r.f[2] == 30 && r.mask[2] == 0x1, "0x-prefixed mask\n");
	CHECK(r.f[3] == 30 && r.mask[3] == 0xA, "lowercase hex, duplicate offset\n");
	CHECK(r.f[4] == 9999 && r.mask[4] == 0x3FF, "last entry with no trailing newline\n");

	struct { const char* text; const char* want; } BAD[] = {
		{ "0 0000\nx 12\n",       "line 2" },                  // not a number
		{ "0 0000\n5\n",          "missing space" },           // no mask
		{ "0 0000\n5 zz\n",       "bad hex mask" },            // not hex
		{ "0 0000\n5 12345\n",    "wider than 16 bits" },      // mask too wide
		{ "0 0000\n5 00A junk\n", "trailing garbage" },        // extra field
		{ "0 0000\n1234567890 1\n", "offset too large" },      // > 9 digits
		{ "10 0001\n5 0002\n",    "goes backwards" },          // unplayable ordering
		{ "# only comments\n",    "empty table" },
		{ "",                     "empty table" },
	};
	for (size_t i = 0; i < sizeof BAD / sizeof BAD[0]; i++) {
		ctl_rep_init(&r, 0);
		CHECK(ctl_rep_load(&r, BAD[i].text, err, sizeof err) == -1, "reject '%s'\n", BAD[i].text);
		CHECK(has(err, BAD[i].want), "err for '%s' = '%s' should mention '%s'\n", BAD[i].text, err, BAD[i].want);
		CHECK(!ctl_rep_active(&r) && r.n == 0, "nothing armed after '%s'\n", BAD[i].text);
	}

	// The cap fails LOUDLY, never silently truncates (D5.6).
	{
		static char big[CTL_REP_MAX * 8 + 64];
		size_t at = 0;
		for (int i = 0; i < CTL_REP_MAX; i++) at += (size_t)snprintf(big + at, sizeof big - at, "%d 1\n", i);
		ctl_rep_init(&r, 0);
		CHECK(ctl_rep_load(&r, big, err, sizeof err) == CTL_REP_MAX, "exactly %d entries load (%s)\n", CTL_REP_MAX, err);
		snprintf(big + at, sizeof big - at, "%d 1\n", CTL_REP_MAX);
		ctl_rep_init(&r, 0);
		CHECK(ctl_rep_load(&r, big, err, sizeof err) == -1 && has(err, "table too large"),
		      "one over the cap is rejected: '%s'\n", err);
		CHECK(!ctl_rep_active(&r), "...and nothing is armed\n");
	}
	ctl_rep_init(&r, 0);
	CHECK(ctl_rep_load(&r, NULL, err, sizeof err) == -1, "NULL text rejected\n");
	CHECK(ctl_rep_load(NULL, "0 1\n", err, sizeof err) == -1, "NULL replay rejected\n");
	CHECK(ctl_rep_load(&r, "0 1\n", NULL, 0) == 1, "NULL err buffer is safe\n");
}

// --------------------------------------------------------------------------------------------
// TEST 17 — the CHUNKED loader: the glue streams the file through a small buffer instead of
// materialising the whole text in RAM next to two mGBA cores. Any chunking must parse identically.
// --------------------------------------------------------------------------------------------
static void test_replay_chunked_load(void) {
	printf("TEST 17: chunked table load == whole-buffer load\n");
	static CtlRep a, b; char err[96];
	const char* TXT = "# hdr\n0 0000\n7 0040\n19 0000\n40 0201\n";
	ctl_rep_init(&a, 0);
	CHECK(ctl_rep_load(&a, TXT, err, sizeof err) == 4, "whole-buffer: %s\n", err);

	for (int chunk = 1; chunk <= 7; chunk++) {          // every chunk size splits lines differently
		ctl_rep_init(&b, 0);
		ctl_rep_load_begin(&b);
		int len = (int)strlen(TXT), bad = 0;
		for (int i = 0; i < len; i += chunk) {
			int take = (len - i < chunk) ? len - i : chunk;
			if (ctl_rep_load_feed(&b, TXT + i, take, err, sizeof err) < 0) { bad = 1; break; }
		}
		int n = bad ? -1 : ctl_rep_load_end(&b, err, sizeof err);
		CHECK(n == 4, "chunk=%d loaded %d (%s)\n", chunk, n, err);
		int same = (b.n == a.n);
		for (int i = 0; same && i < a.n; i++) same = (a.f[i] == b.f[i] && a.mask[i] == b.mask[i]);
		CHECK(same, "chunk=%d produced an identical table\n", chunk);
	}
	// A line longer than the partial-line buffer is a loud error, not a silent cut.
	{
		static char longline[CTL_REP_LINE_MAX + 32];
		memset(longline, '0', sizeof longline - 2);
		longline[0] = '5'; longline[sizeof longline - 2] = '\n'; longline[sizeof longline - 1] = '\0';
		ctl_rep_init(&b, 0);
		CHECK(ctl_rep_load(&b, longline, err, sizeof err) == -1 && has(err, "longer than"),
		      "over-long line rejected: '%s'\n", err);
	}
	// Feeding after an error is inert; end() still reports the failure.
	{
		ctl_rep_init(&b, 0);
		ctl_rep_load_begin(&b);
		CHECK(ctl_rep_load_feed(&b, "zz\n", 3, err, sizeof err) == -1, "first error reported\n");
		CHECK(ctl_rep_load_feed(&b, "0 1\n", 4, err, sizeof err) == -1, "later feeds stay -1\n");
		CHECK(ctl_rep_load_end(&b, err, sizeof err) == -1 && !ctl_rep_active(&b), "end() fails, nothing armed\n");
	}
	// Feed with no begin(), NULL chunk, zero length: all safe no-ops.
	ctl_rep_init(&b, 0);
	CHECK(ctl_rep_load_feed(&b, "0 1\n", 4, err, sizeof err) == 0, "feed without begin is inert\n");
	ctl_rep_load_begin(&b);
	CHECK(ctl_rep_load_feed(&b, NULL, 4, err, sizeof err) == 0, "NULL chunk is safe\n");
	CHECK(ctl_rep_load_feed(&b, "x", 0, err, sizeof err) == 0, "zero length is safe\n");
	ctl_rep_load_begin(NULL); ctl_rep_init(NULL, 0);
	CHECK(ctl_rep_load_feed(NULL, "x", 1, err, sizeof err) == 0 && ctl_rep_load_end(NULL, err, sizeof err) == -1,
	      "NULL replay is safe\n");
}

// --------------------------------------------------------------------------------------------
// TEST 18 — REPLAY playback (D5.7/D5.8): the edge wait, PM's catch-up loop (several entries due
// in one tick -> inject the LATEST), done detection, the non-zero-tail release, and aborts.
// --------------------------------------------------------------------------------------------
static void test_replay_playback(void) {
	printf("TEST 18: replay playback — catch-up loop, done, aborts\n");
	static CtlRep r; char err[96], buf[1024];

	// Edge wait: an armed table injects NOTHING until the field-entry anchor.
	ctl_rep_init(&r, 0);
	CHECK(ctl_rep_load(&r, "0 0001\n3 0000\n", err, sizeof err) == 2, "%s\n", err);
	{
		CtlIn in; ci_init(&in, 700); in.fieldValid = true;
		for (int i = 0; i < 50; i++) { CHECK(ctl_rep_tick(&r, &in) == 0, "no injection without an edge (i=%d)\n", i); in.emuFrame++; }
		rep_drain(&r, buf, sizeof buf);
		CHECK(has(buf, "replay armed: 2 entries"), "arm line: '%s'\n", buf);
		CHECK(has(buf, "replay armed ON-FIELD"), "the edge wait is LOUD: '%s'\n", buf);
		in.fieldValid = false; (void)ctl_rep_tick(&r, &in); in.emuFrame++;
		in.fieldValid = true;
		for (int i = 0; i < CTL_ANCHOR_FRAMES - 1; i++) { CHECK(ctl_rep_tick(&r, &in) == 0, "debounce %d\n", i); in.emuFrame++; }
		CHECK(ctl_rep_tick(&r, &in) == CTL_KEY_A, "the anchor tick plays entry 0 immediately\n");
		rep_drain(&r, buf, sizeof buf);
		CHECK(has(buf, "replay anchored f="), "anchor line: '%s'\n", buf);
		in.emuFrame += 1; CHECK(ctl_rep_tick(&r, &in) == CTL_KEY_A, "still held at offset 1\n");
		in.emuFrame += 2; CHECK(ctl_rep_tick(&r, &in) == 0, "offset 3 releases\n");
		CHECK(!ctl_rep_active(&r), "table exhausted with the keys released -> done\n");
		rep_drain(&r, buf, sizeof buf);
		CHECK(has(buf, "replay done (2 entries"), "done line: '%s'\n", buf);
	}

	// CATCH-UP: a coarse clock (wireless ~4-5 emu-fps: many emulated frames per render tick)
	// consumes several entries in ONE tick and injects only the LATEST (PM 1616-1647).
	{
		ctl_rep_init(&r, 1);
		CHECK(ctl_rep_load(&r, "0 0001\n1 0002\n2 0004\n3 0008\n40 0000\n", err, sizeof err) == 5, "%s\n", err);
		CtlIn in; ci_init(&in, 0);
		(void)ctl_rep_tick(&r, &in); in.emuFrame += 12;         // off-field: the edge
		in.fieldValid = true;
		for (int i = 0; i < CTL_ANCHOR_FRAMES - 1; i++) { (void)ctl_rep_tick(&r, &in); in.emuFrame += 12; }
		uint16_t m = ctl_rep_tick(&r, &in);            // the anchor tick: only offset 0 is due
		CHECK(m == 0x0001 && r.idx == 1, "anchor tick plays entry 0: %04X idx=%ld\n", m, (long)r.idx);
		in.emuFrame += 12;
		m = ctl_rep_tick(&r, &in);                     // now = 12: offsets 1,2,3 all became due
		CHECK(m == 0x0008, "one tick consumed offsets 1..3 and injected the LATEST: %04X\n", m);
		CHECK(r.idx == 4, "four entries consumed (idx=%ld)\n", (long)r.idx);
		in.emuFrame += 12; CHECK(ctl_rep_tick(&r, &in) == 0x0008, "held until the next entry is due\n");
		in.emuFrame += 12; CHECK(ctl_rep_tick(&r, &in) == 0x0008, "still held at now=36 (<40)\n");
		in.emuFrame += 12;
		CHECK(ctl_rep_tick(&r, &in) == 0 && !ctl_rep_active(&r), "the last entry (0000) finishes it\n");
	}

	// A table whose LAST entry is non-zero holds it one tick, then releases + finishes (never a
	// key held forever).
	{
		ctl_rep_init(&r, 0);
		CHECK(ctl_rep_load(&r, "0 0010\n", err, sizeof err) == 1, "%s\n", err);
		CtlIn in; ci_init(&in, 0);
		(void)ctl_rep_tick(&r, &in); in.emuFrame++;
		in.fieldValid = true;
		for (int i = 0; i < CTL_ANCHOR_FRAMES - 1; i++) { (void)ctl_rep_tick(&r, &in); in.emuFrame++; }
		CHECK(ctl_rep_tick(&r, &in) == CTL_KEY_RIGHT, "final mask injected on its tick\n");
		in.emuFrame++;
		CHECK(ctl_rep_tick(&r, &in) == 0, "released on the next tick\n");
		CHECK(!ctl_rep_active(&r), "...and done\n");
	}

	// D5.8 real input aborts a RUNNING replay (same rule as D4.11) — but NOT while it waits for
	// the anchor, because reaching the field is the operator's job (the D4 go-gate scope call).
	{
		ctl_rep_init(&r, 1);
		CHECK(ctl_rep_load(&r, "0 0001\n100 0000\n", err, sizeof err) == 2, "%s\n", err);
		CtlIn in; ci_init(&in, 0);
		in.realKeys = CTL_KEY_UP;                      // walking into the field by hand
		for (int i = 0; i < 5; i++) { CHECK(ctl_rep_tick(&r, &in) == 0, "staging (i=%d)\n", i); in.emuFrame++; }
		CHECK(ctl_rep_active(&r), "hand-staging does NOT abort an armed replay\n");
		in.fieldValid = true;
		for (int i = 0; i < CTL_ANCHOR_FRAMES - 1; i++) { (void)ctl_rep_tick(&r, &in); in.emuFrame++; }
		in.realKeys = 0;
		CHECK(ctl_rep_tick(&r, &in) == CTL_KEY_A, "playing\n");
		in.emuFrame++; in.realKeys = CTL_KEY_B;
		CHECK(ctl_rep_tick(&r, &in) == 0, "the abort tick injects NOTHING (no key fight)\n");
		CHECK(!ctl_rep_active(&r), "replay aborted\n");
		rep_drain(&r, buf, sizeof buf);
		CHECK(has(buf, "[ctl p2] replay ABORT real-input at 1/2"), "abort line names the progress: '%s'\n", buf);
		in.emuFrame++; in.realKeys = 0;
		CHECK(ctl_rep_tick(&r, &in) == 0, "idle after the abort\n");
	}

	// The '!' abort file path: ctl_rep_abort clears an armed OR running replay, idempotently.
	{
		ctl_rep_init(&r, 0);
		CHECK(ctl_rep_load(&r, "0 0001\n", err, sizeof err) == 1, "%s\n", err);
		ctl_rep_abort(&r, "by file");
		CHECK(!ctl_rep_active(&r), "armed replay cleared by the abort file\n");
		rep_drain(&r, buf, sizeof buf);
		CHECK(has(buf, "replay ABORT by file at 0/1"), "abort-file line: '%s'\n", buf);
		ctl_rep_abort(&r, "again");
		rep_drain(&r, buf, sizeof buf);
		CHECK(buf[0] == '\0', "aborting an idle replay logs nothing: '%s'\n", buf);
	}
	CHECK(ctl_rep_tick(NULL, NULL) == 0 && !ctl_rep_active(NULL), "NULL replay is inert\n");
	ctl_rep_abort(NULL, "x");
	CHECK(ctl_rep_status(NULL, buf, sizeof buf) == 0, "NULL status drains 0\n");
}

// --------------------------------------------------------------------------------------------
// TEST 19 — THE ROUND TRIP (the slice's headline claim): a mask timeline recorded through
// ctl_rec_tick, reloaded through ctl_rep_load, and replayed through ctl_rep_tick reproduces the
// EXACT same timeline, frame for frame — including a header-carrying file and a coarse
// (wireless-speed) replay clock.
// --------------------------------------------------------------------------------------------
static void test_round_trip(void) {
	printf("TEST 19: record -> replay ROUND TRIP reproduces the exact mask timeline\n");
	// A realistic little choreography: walk down, stop, press A twice, hold B+RIGHT, release.
	static const uint16_t TL[] = {
		0, 0,                                                   // the anchor debounce ticks
		0,
		CTL_KEY_DOWN, CTL_KEY_DOWN, CTL_KEY_DOWN, CTL_KEY_DOWN,
		0, 0,
		CTL_KEY_A, CTL_KEY_A, 0, 0,
		CTL_KEY_A, CTL_KEY_A, 0,
		(CTL_KEY_B | CTL_KEY_RIGHT), (CTL_KEY_B | CTL_KEY_RIGHT), (CTL_KEY_B | CTL_KEY_RIGHT),
		CTL_KEY_B, 0, 0
	};
	const int N = (int)(sizeof TL / sizeof TL[0]);
	static CtlRec rec; static CtlRep rep;
	static char text[1024], file[2560], err[96];
	uint16_t out[64];

	ctl_rec_init(&rec, 0); ctl_rec_arm(&rec);
	int len = rec_drive(&rec, 4000, 3, TL, N, 1, text, sizeof text);
	CHECK(len > 0, "recorded %d bytes / %lu lines\n", len, (unsigned long)ctl_rec_lines(&rec));

	// The file the operator copies: header + body, exactly as the glue writes it.
	char hdr[768]; ctl_rec_header(&rec, hdr, sizeof hdr, "BPEE", "0803_200000");
	snprintf(file, sizeof file, "%s%s", hdr, text);
	ctl_rep_init(&rep, 0);
	int n = ctl_rep_load(&rep, file, err, sizeof err);
	CHECK(n == (int)ctl_rec_lines(&rec), "every recorded line reloads: %d vs %lu (%s)\n",
	      n, (unsigned long)ctl_rec_lines(&rec), err);

	// Same anchor shape, same clock: the replayed masks must equal the recorded ones tick for
	// tick (the pre-anchor debounce ticks inject 0, which is what the timeline starts with).
	rep_drive(&rep, 9999, 3, out, N, 1);   // a DIFFERENT absolute start frame: offsets are relative
	int mism = -1;
	for (int i = 0; i < N; i++) if (out[i] != TL[i]) { mism = i; break; }
	CHECK(mism < 0, "exact reproduction; first mismatch at tick %d (got %04X want %04X)\n",
	      mism, mism >= 0 ? out[mism] : 0, mism >= 0 ? TL[mism] : 0);
	CHECK(!ctl_rep_active(&rep), "replay finished (the timeline ends with the keys released)\n");

	// Replaying the SAME table on a coarse clock (3 emulated frames per tick — the wireless
	// case): every tick must inject the mask the recording held at that emulated frame.
	ctl_rep_init(&rep, 0);
	CHECK(ctl_rep_load(&rep, file, err, sizeof err) == n, "reload for the coarse pass: %s\n", err);
	{
		uint16_t co[64];
		rep_drive(&rep, 77, 3, co, N, 3);
		int bad = -1;
		for (int i = CTL_ANCHOR_FRAMES - 1; i < N; i++) {
			uint32_t now = (uint32_t)(i - (CTL_ANCHOR_FRAMES - 1)) * 3u;   // emulated frames since the anchor
			uint16_t want = 0;
			for (int e = 0; e < n; e++) if (rep.f[e] <= now) want = rep.mask[e];
			if (co[i] != want && ctl_rep_active(&rep)) { bad = i; break; }
			if (!ctl_rep_active(&rep)) break;
		}
		CHECK(bad < 0, "coarse-clock replay tracks the table; first mismatch at tick %d\n", bad);
	}
}

// --------------------------------------------------------------------------------------------
// TEST 20 — the '# control' netlog mirror for D5 + two independent seats recording/replaying.
// --------------------------------------------------------------------------------------------
static void test_rr_mirror_and_seats(void) {
	printf("TEST 20: D5 netlog mirror + independent seats\n");
	static CtlRec rec; static CtlRep rep; char err[96], line[CTL_REC_LINE];

	ctl_rec_init(&rec, 0); ctl_rep_init(&rep, 0);
	ctl_publish_rr(0, &rec, &rep);
	CHECK(g_ctlStat[0].recState == 0 && g_ctlStat[0].repState == CTL_REP_IDLE &&
	      g_ctlStat[0].recLines == 0 && g_ctlStat[0].repN == 0, "idle mirror is all zero\n");

	ctl_rec_arm(&rec);
	ctl_publish_rr(0, &rec, &rep);
	CHECK(g_ctlStat[0].recState == 1, "armed = state 1 (hunting the anchor)\n");
	{
		CtlIn in; ci_init(&in, 0);
		(void)ctl_rec_tick(&rec, &in, 0, line, sizeof line); in.emuFrame++;
		in.fieldValid = true;
		for (int i = 0; i < CTL_ANCHOR_FRAMES; i++) { (void)ctl_rec_tick(&rec, &in, 0, line, sizeof line); in.emuFrame++; }
	}
	CHECK(ctl_rep_load(&rep, "0 0001\n5 0000\n", err, sizeof err) == 2, "%s\n", err);
	ctl_publish_rr(0, &rec, &rep);
	CHECK(g_ctlStat[0].recState == 2 && g_ctlStat[0].recLines == 1, "recording = state 2, %lu line(s)\n",
	      (unsigned long)g_ctlStat[0].recLines);
	CHECK(g_ctlStat[0].repState == CTL_REP_WAIT && g_ctlStat[0].repN == 2 && g_ctlStat[0].repIdx == 0,
	      "replay armed in the mirror: st=%u %u/%u\n", g_ctlStat[0].repState, g_ctlStat[0].repIdx, g_ctlStat[0].repN);
	ctl_publish_rr(1, NULL, NULL);
	CHECK(g_ctlStat[1].recState == 0 && g_ctlStat[1].repState == 0 && g_ctlStat[1].repN == 0,
	      "NULL publish zeroes the other seat\n");

	// Two seats: p2's recorder is untouched by p1's, and the status lines are seat-tagged.
	{
		static CtlRec r1, r2; char b1[512], b2[512], t1[256], t2[256];
		static const uint16_t A[] = { 0, 0, 0, CTL_KEY_LEFT, 0 };
		static const uint16_t B[] = { 0, 0, 0, CTL_KEY_UP, CTL_KEY_UP, 0 };
		ctl_rec_init(&r1, 0); ctl_rec_arm(&r1);
		ctl_rec_init(&r2, 1); ctl_rec_arm(&r2);
		(void)rec_drive(&r1, 0, 2, A, (int)(sizeof A / sizeof A[0]), 1, t1, sizeof t1);
		(void)rec_drive(&r2, 0, 2, B, (int)(sizeof B / sizeof B[0]), 1, t2, sizeof t2);
		CHECK(strcmp(t1, "0 0000\n1 0020\n2 0000\n") == 0, "p1 timeline: '%s'\n", t1);
		CHECK(strcmp(t2, "0 0000\n1 0040\n3 0000\n") == 0, "p2 timeline: '%s'\n", t2);
		rec_drain(&r1, b1, sizeof b1); rec_drain(&r2, b2, sizeof b2);
		CHECK(has(b1, "[ctl p1]") && !has(b1, "[ctl p2]"), "p1 lines tagged p1: '%s'\n", b1);
		CHECK(has(b2, "[ctl p2]") && !has(b2, "[ctl p1]"), "p2 lines tagged p2: '%s'\n", b2);
	}
}

// ============================================================================================
// D4-T (phase 22.1) — the synthetic-touch script: grammar, tick timeline, drag interpolation,
// aborts. The glue's file plumbing is main.c's; everything below is the pure-C core.
// ============================================================================================
static void test_touch_grammar(void) {
	printf("\n-- TOUCH-SCRIPT: grammar + loud errors\n");
	CtlTouch ts; char err[80];
	// happy path: tap + drag + wait, defaults applied
	int n = ctl_touch_load(&ts, "t 160 120 8\nd 100 200 100 100 20 4\nw 30\n", err, sizeof err);
	CHECK(n == 3, "3 ops parsed (got %d: %s)", n, err);
	CHECK(ts.ev[0].x0 == 160 && ts.ev[0].y0 == 120 && ts.ev[0].hold == 8 && ts.ev[0].gap == 8,
	      "tap fields + default gap 8");
	CHECK(ts.ev[1].x0 == 100 && ts.ev[1].y1 == 100 && ts.ev[1].hold == 20 && ts.ev[1].gap == 4,
	      "drag fields + explicit gap");
	CHECK(ts.ev[2].isWait == 1 && ts.ev[2].gap == 30, "wait op");
	CHECK(ctl_touch_active(&ts), "script active after load");
	// comments + blank lines + CRLF
	n = ctl_touch_load(&ts, "# route\r\n\r\n t 0 0 1 0\r\n", err, sizeof err);
	CHECK(n == 1 && ts.ev[0].hold == 1 && ts.ev[0].gap == 0, "comments/CRLF tolerated");
	// abort file
	CHECK(ctl_touch_load(&ts, "  ! stop", err, sizeof err) == 0, "'!' returns 0 (abort)");
	// loud errors: never a silent partial queue
	CHECK(ctl_touch_load(&ts, "t 320 0 8\n", err, sizeof err) == -1, "X 320 out of range");
	CHECK(ctl_touch_load(&ts, "t 0 240 8\n", err, sizeof err) == -1, "Y 240 out of range");
	CHECK(ctl_touch_load(&ts, "t 10 10 0\n", err, sizeof err) == -1, "HOLD 0 rejected");
	CHECK(ctl_touch_load(&ts, "t 10 10 601\n", err, sizeof err) == -1, "HOLD 601 rejected");
	CHECK(ctl_touch_load(&ts, "d 0 0 10 10 1\n", err, sizeof err) == -1, "drag FRAMES 1 rejected");
	CHECK(ctl_touch_load(&ts, "q 1 2 3\n", err, sizeof err) == -1, "unknown op rejected");
	CHECK(ctl_touch_load(&ts, "t 10 10 8 9 77\n", err, sizeof err) == -1, "trailing junk rejected");
	CHECK(ctl_touch_load(&ts, "\n\n#only comments\n", err, sizeof err) == -1, "empty script rejected");
	CHECK(!ctl_touch_active(&ts), "a failed load queues nothing");
	// the op cap fails loudly
	char big[4096]; big[0] = '\0';
	for (int i = 0; i < CTL_TOUCH_MAX + 1; i++) strcat(big, "t 1 1 1\n");
	CHECK(ctl_touch_load(&ts, big, err, sizeof err) == -1, "op cap is loud");
}

static void test_touch_tick(void) {
	printf("\n-- TOUCH-SCRIPT: tick timeline (tap, drag interpolation, wait, abort)\n");
	CtlTouch ts; char err[80];
	int x = -1, y = -1;
	// tap: 4 down frames at the point, then 2 released, then idle
	CHECK(ctl_touch_load(&ts, "t 50 60 4 2\n", err, sizeof err) == 1, "load tap");
	for (int f = 0; f < 4; f++) {
		CHECK(ctl_touch_tick(&ts, &x, &y) == 1 && x == 50 && y == 60, "tap down frame %d", f);
	}
	CHECK(ctl_touch_tick(&ts, &x, &y) == 0, "gap frame 1 released");
	CHECK(ctl_touch_tick(&ts, &x, &y) == 0, "gap frame 2 released");
	CHECK(!ctl_touch_active(&ts), "tap script done");
	CHECK(ctl_touch_tick(&ts, &x, &y) == 0, "idle after done");
	// drag: 5 frames 0,0 -> 40,80 — endpoints exact, x monotonic
	CHECK(ctl_touch_load(&ts, "d 0 0 40 80 5 1\n", err, sizeof err) == 1, "load drag");
	int lastX = -1, mono = 1;
	for (int f = 0; f < 5; f++) {
		CHECK(ctl_touch_tick(&ts, &x, &y) == 1, "drag down frame %d", f);
		if (f == 0) CHECK(x == 0 && y == 0, "drag starts at (0,0)");
		if (f == 4) CHECK(x == 40 && y == 80, "drag ends at (40,80)");
		if (x < lastX) mono = 0;
		lastX = x;
	}
	CHECK(mono, "drag x monotonic");
	CHECK(ctl_touch_tick(&ts, &x, &y) == 0, "drag gap released");
	CHECK(!ctl_touch_active(&ts), "drag script done");
	// wait-first script: released for the whole wait, then the tap fires
	CHECK(ctl_touch_load(&ts, "w 3\nt 5 5 2 1\n", err, sizeof err) == 2, "load wait+tap");
	for (int f = 0; f < 3; f++) CHECK(ctl_touch_tick(&ts, &x, &y) == 0, "wait frame %d", f);
	CHECK(ctl_touch_tick(&ts, &x, &y) == 1 && x == 5, "tap after wait");
	// abort mid-script
	ctl_touch_abort(&ts);
	CHECK(!ctl_touch_active(&ts) && ctl_touch_tick(&ts, &x, &y) == 0, "abort clears");
}

int main(void) {
	printf("=== control.c (D4 file-driven movement + D5 record/replay) host test ===\n");
	test_grammar();
	test_grammar_errors();
	test_abort_file();
	test_walk_closed_loop();
	test_walk_warp();
	test_walk_timeout();
	test_no_field();
	test_taps_waits();
	test_go_gate();
	test_real_input_abort();
	test_frozen_clock();
	test_status_and_counters();
	test_two_seats();
	test_record_basic();
	test_record_edge_stop_cap();
	test_replay_load();
	test_replay_chunked_load();
	test_replay_playback();
	test_round_trip();
	test_rr_mirror_and_seats();
	test_touch_grammar();        // phase 22.1 D4-T
	test_touch_tick();           // phase 22.1 D4-T
	printf("\n%d checks, %d failures -> %s\n", g_checks, g_fail, g_fail ? "FAIL" : "PASS");
	return g_fail ? 1 : 0;
}
