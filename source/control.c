// control.c — Phase 13-prep slices D4 + D5: the file-driven tile-exact movement grammar + the
// closed-loop scheduler (D4), and input RECORD / REPLAY (D5). PURE C (no libctru, no mGBA, no
// file I/O, no clock) so the whole thing unit-tests on the PC with a fake coordinate feed —
// test/host/test_control.c.
//
// Spec: docs/phase13-diagnostics/SPEC-control-replay.md §D4 + §D5 + §C.
// Source design: docs/kb/external/pm-bridge-forensics.md PART 2 #10 (melonds_move.txt token
// grammar, EmuThread.cpp:275-408) and #9 (one-shot re-armable go files, EmuThread.cpp:199-235);
// PART 1 #2 (DeSmuME record/replay patModes 10/11, mp_bridge.cpp:464-477 + 1584-1647);
// PART 3 ranked port items 2 + 3 ("hands-free 2-console repros — our runs die on manual
// choreography; scripts make run #13+ identical each time" / "a manual repro becomes a
// deterministic regression asset, and it is the Tier-3 determinism prototype").
//
// The one design idea worth restating: MOVEMENT IS COORDINATE-CLOSED-LOOP. A walk token latches
// the player's live tile, computes a target, and HOLDS the d-pad until the game's own coordinate
// reaches it — so a script is tile-exact regardless of lag, bumps, wireless slow-motion or a
// collision, and PM's classic failure ("24 frames over-shot and left the house",
// mp_bridge.cpp:1516-1541) cannot happen. The n*60+240 deadline is the only escape hatch: it
// exists so a wall/NPC block stops the script LOUDLY instead of holding a direction forever.
#include "control.h"

#include <stdarg.h>   // vsnprintf for the status ring (the only variadic use in this file)
#include <stdio.h>    // vsnprintf / snprintf only — NO file I/O in this module (SPEC C.1)
#include <string.h>

CtlStat g_ctlStat[2];   // netlog mirror (D4.14); published by ctl_publish after every tick

// --------------------------------------------------------------------------------------------
// Status ring — control.c never touches SD. Lines are "[ctl pN] <text>\n"; the glue drains the
// ring EVERY frame and appends+fflushes each line to the control log (D4.13: rare events, and
// per-line fflush is the PM crash-safety rule, mp_bridge.cpp §1 — these scripts run precisely in
// hang-prone hardware sessions where a buffered tail dies with the power switch; the run-#11
// lost-log lesson).
// --------------------------------------------------------------------------------------------
// The ring mechanics are shared verbatim by the D4 scheduler and the two D5 halves (each keeps
// its OWN ring so the three feature states stay independent) — hence the explicit
// buffer/head/count parameters instead of a struct: CtlSched's layout is unchanged.
static void ctl_ring_vpush(char ring[][CTL_STATUS_LEN], uint8_t* head, uint8_t* count,
                           int seat, const char* fmt, va_list ap) {
	char* slot;
	if (*count >= CTL_STATUS_N) {               // full: drop the OLDEST (the newest events are
		slot = ring[*head];                     // the ones that explain the current state). When
		                                        // count == N the tail slot IS *head, so the new
		                                        // line lands there and *head steps over it.
		*head = (uint8_t)((*head + 1u) % CTL_STATUS_N);
	} else {
		slot = ring[(*head + *count) % CTL_STATUS_N];
		(*count)++;
	}
	int pre = snprintf(slot, CTL_STATUS_LEN, "[ctl p%d] ", seat + 1);
	if (pre < 0) { slot[0] = '\0'; return; }
	if (pre >= CTL_STATUS_LEN) pre = CTL_STATUS_LEN - 1;
	int body = vsnprintf(slot + pre, (size_t)(CTL_STATUS_LEN - pre), fmt, ap);
	if (body < 0) body = 0;
	int len = pre + body;
	if (len > CTL_STATUS_LEN - 2) len = CTL_STATUS_LEN - 2;   // room for '\n' + NUL
	slot[len] = '\n';
	slot[len + 1] = '\0';
}

// Variadic front door for callers that hold the buffer directly (the D5 anchor detector).
static void ctl_ring_push(char ring[][CTL_STATUS_LEN], uint8_t* head, uint8_t* count,
                          int seat, const char* fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	ctl_ring_vpush(ring, head, count, seat, fmt, ap);
	va_end(ap);
}

static int ctl_ring_drain(char ring[][CTL_STATUS_LEN], uint8_t* head, uint8_t* count,
                          char* line, int cap) {
	if (!line || cap <= 0 || *count == 0) return 0;
	const char* src = ring[*head];
	int n = 0;
	while (src[n] && n < cap - 1) { line[n] = src[n]; n++; }
	line[n] = '\0';
	*head = (uint8_t)((*head + 1u) % CTL_STATUS_N);
	(*count)--;
	return n;
}

static void ctl_push(CtlSched* cs, const char* fmt, ...) {
	if (!cs) return;
	va_list ap;
	va_start(ap, fmt);
	ctl_ring_vpush(cs->ring, &cs->rHead, &cs->rCount, (int)cs->seat, fmt, ap);
	va_end(ap);
}

void ctl_note(CtlSched* cs, const char* msg) {
	ctl_push(cs, "%s", msg ? msg : "");
}

int ctl_status(CtlSched* cs, char* line, int cap) {
	if (!cs) return 0;
	return ctl_ring_drain(cs->ring, &cs->rHead, &cs->rCount, line, cap);
}

// --------------------------------------------------------------------------------------------
// Lifecycle
// --------------------------------------------------------------------------------------------
void ctl_init(CtlSched* cs, int seat) {
	if (!cs) return;
	memset(cs, 0, sizeof *cs);
	cs->seat = (int8_t)(seat ? 1 : 0);
	cs->mg0 = cs->mn0 = -1;
}

bool ctl_active(const CtlSched* cs)     { return cs && cs->state != CTL_IDLE; }
bool ctl_waiting_go(const CtlSched* cs) { return cs && cs->state == CTL_WAIT_GO; }

void ctl_counters(const CtlSched* cs, uint16_t* toksDone, uint16_t* aborts,
                  uint16_t* timeouts, uint16_t* pickups) {
	if (toksDone) *toksDone = cs ? cs->toksDone : 0;
	if (aborts)   *aborts   = cs ? cs->aborts   : 0;
	if (timeouts) *timeouts = cs ? cs->timeouts : 0;
	if (pickups)  *pickups  = cs ? cs->pickups  : 0;
}

void ctl_publish(const CtlSched* cs) {
	if (!cs) return;
	int s = (cs->seat == 1) ? 1 : 0;
	g_ctlStat[s].toksDone = cs->toksDone;
	g_ctlStat[s].aborts   = cs->aborts;
	g_ctlStat[s].timeouts = cs->timeouts;
	g_ctlStat[s].pickups  = cs->pickups;
	g_ctlStat[s].state    = cs->state;
	g_ctlStat[s].nTok     = (uint8_t)(cs->nTok  > 255 ? 255 : cs->nTok);
	g_ctlStat[s].idx      = (uint8_t)(cs->idx   > 255 ? 255 : cs->idx);
}

// Clear the queue + per-token latches WITHOUT logging (the callers own the message).
static void ctl_clear(CtlSched* cs) {
	cs->state = CTL_IDLE;
	cs->nTok = 0; cs->idx = 0;
	cs->tokStarted = 0; cs->waitArmed = 0;
	cs->mg0 = cs->mn0 = -1;
}

void ctl_abort(CtlSched* cs, const char* why) {
	if (!cs || cs->state == CTL_IDLE) return;   // idempotent: nothing to abort, nothing logged
	if (why) ctl_push(cs, "ABORT %s at tok %d", why, (int)cs->idx);
	cs->aborts++;
	ctl_clear(cs);
}

// --------------------------------------------------------------------------------------------
// Grammar (SPEC D4.5, adopted from PM PART 2 #10 with GBA-specific mappings)
//
//   L<n> R<n> U<n> D<n>   walk n tiles, CLOSED LOOP on the live coordinate
//   l<n> r<n> u<n> d<n>   sprint n tiles — same closed loop, mask also holds B (Running Shoes;
//                         indoors the game ignores B and walks, and the closed loop doesn't care)
//   bare L R U D / l r u d   one TAP (face-turn / menu navigation). Case does NOT add B on a
//                         bare tap (PM treats n==0 as a tap, EmuThread.cpp:334-346).
//   a b s c x y           button tap = A, B, START, SELECT, L, R. **The GBA has no X/Y**, so
//                         PM's x/y letters are remapped to the GBA shoulder buttons; the button
//                         letters stay disjoint from the direction letters, so the grammar is
//                         unambiguous. Buttons are LOWERCASE ONLY (a stray 'A' is a loud parse
//                         error naming the token, not a silent mis-press).
//   W<n> / w<n>           wait n emulated frames (inject nothing)
//   G                     go-gate, FIRST token only — the script parses and holds until
//                         go_p<N>.txt appears (D4.10)
//   leading '!'           ABORT file (D4.4)
// --------------------------------------------------------------------------------------------
static bool ctl_is_space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f'; }
static bool ctl_is_digit(char c) { return c >= '0' && c <= '9'; }

static void ctl_seterr(char* err, int errCap, const char* fmt, ...) {
	if (!err || errCap <= 0) return;
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(err, (size_t)errCap, fmt, ap);
	va_end(ap);
}

// Direction letter -> CDIR_*, or -1. `sprint` is set when the letter was lowercase.
static int ctl_dir_of(char c, int* sprint) {
	switch (c) {
		case 'L': *sprint = 0; return CDIR_L;
		case 'R': *sprint = 0; return CDIR_R;
		case 'U': *sprint = 0; return CDIR_U;
		case 'D': *sprint = 0; return CDIR_D;
		case 'l': *sprint = 1; return CDIR_L;
		case 'r': *sprint = 1; return CDIR_R;
		case 'u': *sprint = 1; return CDIR_U;
		case 'd': *sprint = 1; return CDIR_D;
		default:  return -1;
	}
}

static uint16_t ctl_dir_mask(int dir) {
	switch (dir) {
		case CDIR_L: return CTL_KEY_LEFT;
		case CDIR_R: return CTL_KEY_RIGHT;
		case CDIR_U: return CTL_KEY_UP;
		default:     return CTL_KEY_DOWN;
	}
}

// Button letter -> GBA mask, or 0. (x/y = the GBA shoulders — SPEC D4.5.)
static uint16_t ctl_btn_mask(char c) {
	switch (c) {
		case 'a': return CTL_KEY_A;
		case 'b': return CTL_KEY_B;
		case 's': return CTL_KEY_START;
		case 'c': return CTL_KEY_SELECT;
		case 'x': return CTL_KEY_L;
		case 'y': return CTL_KEY_R;
		default:  return 0;
	}
}

int ctl_load(CtlSched* cs, const char* text, char* err, int errCap) {
	if (err && errCap > 0) err[0] = '\0';
	if (!cs || !text) { ctl_seterr(err, errCap, "null args"); return -1; }

	// Bounded length scan (the buffer may be untrusted SD content).
	size_t len = 0;
	while (len <= (size_t)CTL_FILE_MAX && text[len]) len++;
	if (len > (size_t)CTL_FILE_MAX) {
		ctl_seterr(err, errCap, "file too large (>%d bytes)", CTL_FILE_MAX);
		return -1;
	}

	// The ABORT file is checked FIRST, before the busy guard: '!' exists precisely to stop a
	// RUNNING script (D4.4 — it is the one file honoured mid-script). State is untouched here;
	// the glue logs `ABORT by file` and clears via ctl_abort(cs, NULL).
	const char* p = text;
	while (*p && ctl_is_space(*p)) p++;
	if (*p == '!') return 0;

	// Never clobber a live script: the glue leaves a non-abort file in place while one runs
	// (D4.4), so reaching here with an active seat is a glue bug — fail loudly, change nothing.
	if (cs->state != CTL_IDLE) { ctl_seterr(err, errCap, "seat busy (script running)"); return -1; }

	int nT = 0;
	bool waitGo = false;
	int tokIdx = 0;   // index of the token in the FILE (G counts, so "G must be first" is exact)
	while (*p) {
		while (*p && ctl_is_space(*p)) p++;
		if (!*p) break;
		const char* start = p;
		while (*p && !ctl_is_space(*p)) p++;
		size_t tl = (size_t)(p - start);
		if (tl >= (size_t)CTL_TOK_TEXT) {
			ctl_seterr(err, errCap, "token %d too long (max %d chars)", tokIdx, CTL_TOK_TEXT - 1);
			return -1;
		}
		char tb[CTL_TOK_TEXT];
		memcpy(tb, start, tl); tb[tl] = '\0';

		// 'G' — the go-gate, FIRST token only (D4.10).
		if (tb[0] == 'G' && tl == 1) {
			if (tokIdx != 0) { ctl_seterr(err, errCap, "G must be the FIRST token"); return -1; }
			waitGo = true;
			tokIdx++;
			continue;
		}
		if (nT >= CTL_TOK_MAX) { ctl_seterr(err, errCap, "too many tokens (max %d)", CTL_TOK_MAX); return -1; }

		// Trailing digits (shared by walks and waits).
		uint32_t num = 0;
		bool haveNum = false, badNum = false;
		for (size_t i = 1; i < tl; i++) {
			if (!ctl_is_digit(tb[i])) { badNum = true; break; }
			haveNum = true;
			num = num * 10u + (uint32_t)(tb[i] - '0');
			if (num > 100000u) { badNum = true; break; }
		}

		CtlTok* t = &cs->tok[nT];
		memset(t, 0, sizeof *t);
		memcpy(t->text, tb, tl + 1);

		int sprint = 0;
		int dir = ctl_dir_of(tb[0], &sprint);
		if (dir >= 0) {
			if (tl == 1) {                       // bare direction = one TAP (no B, either case)
				t->kind = CTOK_TAPDIR; t->dir = (uint8_t)dir; t->mask = ctl_dir_mask(dir); t->n = 0;
			} else {
				if (badNum || !haveNum) { ctl_seterr(err, errCap, "bad token '%s' (expected %c<n>)", tb, tb[0]); return -1; }
				if (num < 1 || num > (uint32_t)CTL_WALK_MAX) {
					ctl_seterr(err, errCap, "bad tile count in '%s' (1..%d)", tb, CTL_WALK_MAX); return -1;
				}
				t->kind = CTOK_WALK; t->dir = (uint8_t)dir; t->n = (uint16_t)num;
				t->mask = (uint16_t)(ctl_dir_mask(dir) | (sprint ? CTL_KEY_B : 0u));
			}
		} else if (tb[0] == 'W' || tb[0] == 'w') {
			if (badNum || !haveNum) { ctl_seterr(err, errCap, "bad token '%s' (expected W<n>)", tb); return -1; }
			if (num < 1 || num > (uint32_t)CTL_WAIT_MAX) {
				ctl_seterr(err, errCap, "bad wait in '%s' (1..%d)", tb, CTL_WAIT_MAX); return -1;
			}
			t->kind = CTOK_WAIT; t->mask = 0; t->n = (uint16_t)num;
		} else if (tl == 1 && ctl_btn_mask(tb[0])) {
			t->kind = CTOK_BTN; t->mask = ctl_btn_mask(tb[0]); t->n = 0;
		} else {
			ctl_seterr(err, errCap, "bad token '%s' (buttons are lowercase a b s c x y)", tb);
			return -1;
		}
		nT++;
		tokIdx++;
	}

	if (nT == 0) { ctl_seterr(err, errCap, "empty script (no tokens)"); return -1; }

	cs->nTok = (int16_t)nT;
	cs->idx = 0;
	cs->tokStarted = 0; cs->waitArmed = 0;
	cs->mg0 = cs->mn0 = -1;
	cs->state = waitGo ? CTL_WAIT_GO : CTL_RUN;
	cs->pickups++;
	return nT;
}

// --------------------------------------------------------------------------------------------
// Scheduler
// --------------------------------------------------------------------------------------------
// Finish the current token and step to the next. ONE completion per tick: the completing tick
// injects nothing, which gives every token boundary a natural one-frame key RELEASE (a Gen-3
// menu needs the newKeys edge; a walk needs the d-pad let go before the next direction).
static void ctl_next(CtlSched* cs, const CtlIn* in) {
	cs->toksDone++;
	cs->idx++;
	cs->tokStarted = 0; cs->waitArmed = 0;
	cs->mg0 = cs->mn0 = -1;
	if (cs->idx >= cs->nTok) {
		ctl_push(cs, "script done (%d tokens) f=%lu", (int)cs->nTok, (unsigned long)in->emuFrame);
		ctl_clear(cs);
	}
}

uint16_t ctl_tick(CtlSched* cs, const CtlIn* in) {
	if (!cs || !in || cs->state == CTL_IDLE) return 0;

	// ---- D4.11 REAL INPUT WINS. The trigger is the seat's routed non-script mask, so keys that
	// never reach this seat (3DS-level HUD keys, touch while TOUCH_OFF, the pad of the OTHER
	// game) can't abort by construction. Abort, not pause: walk targets are latched
	// start-relative, so resuming from an operator-moved tile silently walks the wrong path —
	// exactly the quiet-corruption class the touch arc taught us to refuse (auto-memory
	// `touch-reliability-over-piling`). Recovery is cheap: drop the file again.
	// DELIBERATE SCOPE (SPEC deviation, see BUILDLOG): this applies ONLY while RUNNING. A
	// G-gated script is *designed* to be staged by hand (D4.10 / Appendix R2: "the operator
	// stages the scene by hand, then touches the file"), so aborting on the staging input would
	// make the go-file workflow unusable.
	if (cs->state == CTL_RUN && in->realKeys != 0) {
		ctl_push(cs, "ABORT real-input at tok %d", (int)cs->idx);
		cs->aborts++;
		ctl_clear(cs);
		return 0;
	}

	// ---- D4.10 go-gate.
	if (cs->state == CTL_WAIT_GO) {
		if (!in->goSeen) return 0;
		ctl_push(cs, "GO f=%lu", (unsigned long)in->emuFrame);
		cs->state = CTL_RUN;
		// fall through: token 0 starts on this very tick
	}

	if (cs->idx < 0 || cs->idx >= cs->nTok) { ctl_clear(cs); return 0; }   // defensive
	CtlTok* t = &cs->tok[cs->idx];

	switch (t->kind) {

	case CTOK_WAIT: {
		if (!cs->tokStarted) { cs->t0 = in->emuFrame; cs->tokStarted = 1; }
		if ((uint32_t)(in->emuFrame - cs->t0) >= (uint32_t)t->n) {
			ctl_push(cs, "tok %d %s done f=%lu", (int)cs->idx, t->text, (unsigned long)in->emuFrame);
			ctl_next(cs, in);
		}
		return 0;   // waits inject nothing, running or completing
	}

	case CTOK_TAPDIR:
	case CTOK_BTN: {
		// A fixed press/slot window, counted in EMULATED frames — so a tap survives the
		// wireless ~4-5 emu-fps unchanged (it is 12 GAME frames of A either way) and a frozen
		// clock simply suspends it (SPEC §0). Taps/waits have NO coordinate precondition: they
		// are used inside menus deliberately (D4.9).
		if (!cs->tokStarted) { cs->t0 = in->emuFrame; cs->tokStarted = 1; }
		uint32_t el    = (uint32_t)(in->emuFrame - cs->t0);
		uint32_t press = (t->kind == CTOK_BTN) ? (uint32_t)CTL_TAP_BTN_PRESS : (uint32_t)CTL_TAP_DIR_PRESS;
		uint32_t slot  = (t->kind == CTOK_BTN) ? (uint32_t)CTL_TAP_BTN_SLOT  : (uint32_t)CTL_TAP_DIR_SLOT;
		if (el >= slot) {
			ctl_push(cs, "tok %d %s done f=%lu", (int)cs->idx, t->text, (unsigned long)in->emuFrame);
			ctl_next(cs, in);
			return 0;
		}
		return (el < press) ? t->mask : (uint16_t)0;
	}

	default: {   // CTOK_WALK — the closed loop
		if (!cs->tokStarted) {
			// ---- D4.9 coordinate validity. A walk only STARTS on a live field snapshot; until
			// then it WAITS with its deadline unarmed (so a script dropped during a fade doesn't
			// burn its timeout on the loading screen). CTL_NOFIELD_DL emulated frames of waiting
			// = the game is not where the script thinks it is -> stop loudly.
			if (!in->fieldValid) {
				if (!cs->waitArmed) { cs->waitArmed = 1; cs->waitT0 = in->emuFrame; }
				else if ((uint32_t)(in->emuFrame - cs->waitT0) >= CTL_NOFIELD_DL) {
					ctl_push(cs, "NO-FIELD tok %d %s", (int)cs->idx, t->text);
					cs->aborts++;
					ctl_clear(cs);
				}
				return 0;
			}
			cs->waitArmed = 0;
			cs->sx = in->px; cs->sy = in->py;
			cs->tx = in->px; cs->ty = in->py;
			switch (t->dir) {
				case CDIR_L: cs->tx = (int16_t)(cs->sx - (int)t->n); break;
				case CDIR_R: cs->tx = (int16_t)(cs->sx + (int)t->n); break;
				case CDIR_U: cs->ty = (int16_t)(cs->sy - (int)t->n); break;
				default:     cs->ty = (int16_t)(cs->sy + (int)t->n); break;
			}
			cs->mg0 = in->mapGroup; cs->mn0 = in->mapNum;
			cs->t0 = in->emuFrame;
			cs->deadline = in->emuFrame + CTL_WALK_DL(t->n);   // D4.8, armed at the REAL start
			cs->tokStarted = 1;
			ctl_push(cs, "tok %d %s start (%d,%d)->(%d,%d) f=%lu", (int)cs->idx, t->text,
			         (int)cs->sx, (int)cs->sy, (int)cs->tx, (int)cs->ty, (unsigned long)in->emuFrame);
		}

		// ---- D4.7 a WARP completes the token. Stepping onto a warp tile teleports the player
		// into a NEW coordinate space, so the target comparison is meaningless and the walk
		// achieved its purpose (it reached the warp). Scripts must END a walk at the warp and
		// ride the fade with a W<n> (Appendix R4).
		if (cs->mg0 >= 0 && in->mapGroup >= 0 && (in->mapGroup != cs->mg0 || in->mapNum != cs->mn0)) {
			ctl_push(cs, "tok %d %s warp-complete %d.%d->%d.%d", (int)cs->idx, t->text,
			         (int)cs->mg0, (int)cs->mn0, (int)in->mapGroup, (int)in->mapNum);
			ctl_next(cs, in);
			return 0;
		}

		// ---- D4.6 the closed loop: compare the MOVED axis only. Mid-token invalidity (a fade,
		// a warp in flight) just suspends the comparison — the hold continues and the deadline
		// or the map change decides.
		if (in->fieldValid) {
			bool xaxis = (t->dir == CDIR_L || t->dir == CDIR_R);
			int live = xaxis ? (int)in->px : (int)in->py;
			int targ = xaxis ? (int)cs->tx : (int)cs->ty;
			if (live == targ) {
				int dx = (int)in->px - (int)cs->sx, dy = (int)in->py - (int)cs->sy;
				int cross = xaxis ? dy : dx;
				// Cross-axis drift is a DIAGNOSTIC, not a failure: currents / forced-slide tiles
				// legitimately move the other axis. Complete anyway, but say so.
				if (cross != 0)
					ctl_push(cs, "tok %d %s cross-drift (%d,%d)", (int)cs->idx, t->text, dx, dy);
				ctl_push(cs, "tok %d %s done at (%d,%d) f=%lu", (int)cs->idx, t->text,
				         (int)in->px, (int)in->py, (unsigned long)in->emuFrame);
				ctl_next(cs, in);
				return 0;
			}
		}

		// ---- D4.8 wall(-blocked) timeout: ABORT THE WHOLE QUEUE, don't skip to the next token.
		// Every later walk target is start-relative, so continuing after a missed walk executes
		// the rest of the choreography from the wrong tile — silently wrong beats loudly stopped.
		if ((int32_t)(in->emuFrame - cs->deadline) >= 0) {
			ctl_push(cs, "TIMEOUT tok %d %s at (%d,%d)", (int)cs->idx, t->text, (int)in->px, (int)in->py);
			cs->timeouts++;
			cs->aborts++;   // a timeout IS an abort of the queue; it counts in both columns
			ctl_clear(cs);
			return 0;
		}
		return t->mask;
	}
	}
}

// ============================================================================================
// D5 — INPUT RECORD / REPLAY (SPEC-control-replay.md §D5)
// ============================================================================================
// Ported from the DeSmuME-PM bridge's patModes 10/11 (docs/kb/external/pm-bridge-forensics.md
// PART 1 #2): record = mp_bridge.cpp:1591-1615, replay = 1616-1647, determinism comment 464-477.
// Both halves are pure formatting + arithmetic here; every byte of file I/O is the glue's.
//
// The two halves share ONE anchor detector so the offsets they produce and consume mean the same
// thing (SPEC D5.2/D5.7). Recording is PASSIVE by construction — ctl_rec_tick returns text, never
// a key mask — which is how PM's "record never overrides live input" rule (early return at
// mp_bridge.cpp:1614) is satisfied structurally rather than by a runtime check.
// --------------------------------------------------------------------------------------------

static void ctl_rec_push(CtlRec* r, const char* fmt, ...) {
	if (!r) return;
	va_list ap; va_start(ap, fmt);
	ctl_ring_vpush(r->ring, &r->rHead, &r->rCount, (int)r->seat, fmt, ap);
	va_end(ap);
}
static void ctl_rep_push(CtlRep* r, const char* fmt, ...) {
	if (!r) return;
	va_list ap; va_start(ap, fmt);
	ctl_ring_vpush(r->ring, &r->rHead, &r->rCount, (int)r->seat, fmt, ap);
	va_end(ap);
}

// ---- the shared field-entry EDGE detector (D5.2) --------------------------------------------
// True exactly on the tick the anchor latches: `fieldValid` held for CTL_ANCHOR_FRAMES
// consecutive ticks AFTER a non-field tick. The "after a non-field tick" half is what makes it an
// EDGE — an anchor that can fire wherever the player happens to be standing is not reproducible
// on the replaying console, and both halves must agree on the same instant or every offset is
// wrong. Arming while already on the field is legal but says so ONCE (loud, never a silent
// no-op): the cheapest universal edge on Gen-3 is to open the START menu (GCTX_FIELDMENU is a
// non-field context, gamestate.h) and close it again.
static bool ctl_anchor_step(CtlAnchor* a, const CtlIn* in, char ring[][CTL_STATUS_LEN],
                            uint8_t* head, uint8_t* count, int seat, const char* who) {
	if (!in->fieldValid) { a->db = 0; a->sawNonField = 1; return false; }
	if (!a->sawNonField) {                    // armed while ON-FIELD: wait for a real edge
		if (!a->noted) {
			a->noted = 1;
			ctl_ring_push(ring, head, count, seat,
			              "%s armed ON-FIELD: waiting for a field-entry EDGE "
			              "(open+close the START menu, or a map/menu transition)", who);
		}
		return false;
	}
	if (a->db < 255) a->db++;
	return a->db == (uint8_t)CTL_ANCHOR_FRAMES;
}

// --------------------------------------------------------------------------------------------
// RECORD (D5.1-D5.5) — PASSIVE: text out, never a key mask.
// --------------------------------------------------------------------------------------------
void ctl_rec_init(CtlRec* r, int seat) {
	if (!r) return;
	memset(r, 0, sizeof *r);
	r->seat = (int8_t)(seat ? 1 : 0);
}

void ctl_rec_arm(CtlRec* r) {
	if (!r) return;
	// Re-arming mid-session is legal (drop record_p<N>.txt again): everything but the status ring
	// resets, so the NEXT anchor starts a fresh file with fresh offsets.
	r->armed = 1; r->anchored = 0; r->lines = 0; r->lastMask = 0; r->anchorFrame = 0;
	r->anch.db = 0; r->anch.sawNonField = 0; r->anch.noted = 0;
	ctl_rec_push(r, "record armed (waiting for the field-entry edge)");
}

bool     ctl_rec_armed(const CtlRec* r)        { return r && r->armed; }
bool     ctl_rec_anchored(const CtlRec* r)     { return r && r->anchored; }
uint32_t ctl_rec_anchor_frame(const CtlRec* r) { return r ? r->anchorFrame : 0u; }
uint32_t ctl_rec_lines(const CtlRec* r)        { return r ? r->lines : 0u; }

void ctl_rec_stop(CtlRec* r, const char* why) {
	if (!r || !r->armed) return;             // idempotent (the glue may call it unconditionally)
	if (why) ctl_rec_push(r, "record stopped: %s (%lu lines)", why, (unsigned long)r->lines);
	r->armed = 0; r->anchored = 0;
	r->anch.db = 0; r->anch.sawNonField = 0; r->anch.noted = 0;
}

// Emit one body line, enforcing the cap. The cap check runs BEFORE the write, so the -1
// ("recording stopped") happens exactly once: `armed` is cleared with it and every later tick
// returns 0 at the top of ctl_rec_tick.
static int ctl_rec_emit(CtlRec* r, char* line, int cap, uint32_t off, uint16_t mask) {
	if (r->lines >= CTL_REC_MAX_LINES) {
		ctl_rec_push(r, "record cap (%lu lines) - recording stopped", (unsigned long)CTL_REC_MAX_LINES);
		r->armed = 0;
		return -1;
	}
	int n = snprintf(line, (size_t)cap, "%lu %04X\n", (unsigned long)off, (unsigned)mask);
	if (n < 0 || n >= cap) return 0;         // truncated => not a line; never count a half-line
	r->lines++;
	return n;
}

int ctl_rec_tick(CtlRec* r, const CtlIn* in, uint16_t finalMask, char* line, int cap) {
	if (!r || !in || !r->armed || !line || cap <= 0) return 0;

	if (!r->anchored) {
		if (!ctl_anchor_step(&r->anch, in, r->ring, &r->rHead, &r->rCount, (int)r->seat, "record"))
			return 0;
		r->anchored = 1;
		r->anchorFrame = in->emuFrame;       // offset 0 (the glue writes the header from here)
		ctl_rec_push(r, "record anchored f=%lu", (unsigned long)in->emuFrame);
		r->lastMask = finalMask;
		return ctl_rec_emit(r, line, cap, 0u, finalMask);   // FIRST line sits AT the anchor
	}

	// ON-CHANGE ONLY (PM's apRecLastMask, mp_bridge.cpp:1591-1615). A frozen emulated clock
	// (pause menu / paused peer seat) simply produces no changes and therefore no lines, and it
	// never inflates the offsets — they are emulated frames, not wall time (SPEC D5.5/§0).
	if (finalMask == r->lastMask) return 0;
	r->lastMask = finalMask;
	return ctl_rec_emit(r, line, cap, (uint32_t)(in->emuFrame - r->anchorFrame), finalMask);
}

// The record file's '#' header (D5.3). Pure formatting so it is host-testable AND so the whole
// file round-trips: ctl_rep_load skips '#' lines, which is what makes a recording directly
// usable as replay_p<N>.txt (copy it, touch the go file).
int ctl_rec_header(const CtlRec* r, char* buf, int cap, const char* game, const char* date) {
	if (!buf || cap <= 0) return 0;
	int seat1 = r ? (int)r->seat + 1 : 0;
	int n = snprintf(buf, (size_t)cap,
	    "# 3DGBA rec v1 seat=p%d game=%s anchored=%lu date=%s\n"
	    "# '<frameOffset> <mask>' ON CHANGE; offset = EMULATED frames since the field-entry"
	    " anchor (%d-tick debounce)\n"
	    "# mask hex, GBA KEYINPUT order: A0 B1 SELECT2 START3 RIGHT4 LEFT5 UP6 DOWN7 R8 L9"
	    " (gbacore.h GBAKEY_*, NOT the DS layout)\n"
	    "# replay: copy to sdmc:/cias/control/replay_p%d.txt, touch replay_go_p%d.txt;"
	    " same .sav + ROM rev + start tile or it diverges\n"
	    "# save: \n",
	    seat1, (game && *game) ? game : "----",
	    (unsigned long)(r ? r->anchorFrame : 0u), (date && *date) ? date : "",
	    (int)CTL_ANCHOR_FRAMES, seat1, seat1);
	if (n < 0) return 0;
	return (n >= cap) ? cap - 1 : n;
}

int ctl_rec_status(CtlRec* r, char* line, int cap) {
	if (!r) return 0;
	return ctl_ring_drain(r->ring, &r->rHead, &r->rCount, line, cap);
}

// --------------------------------------------------------------------------------------------
// REPLAY (D5.6-D5.8) — table loader (whole-buffer AND chunked) + PM's catch-up playback loop.
// --------------------------------------------------------------------------------------------
static int ctl_hex(char c) {
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

// Parse ONE table line. 1 = entry appended, 0 = comment/blank, -1 = malformed (err filled).
// Everything is rejected LOUDLY and nothing is armed (D5.6) — a silently truncated or reordered
// table would replay the wrong inputs, which is worse than no replay at all.
static int ctl_rep_line(CtlRep* r, const char* s, char* err, int errCap) {
	r->lineNo++;
	while (*s && ctl_is_space(*s)) s++;
	if (!*s || *s == '#') return 0;                       // '#' header/comment, or a blank line

	if (!ctl_is_digit(*s)) {
		ctl_seterr(err, errCap, "line %ld: expected '<offset> <hexmask>', got '%.12s'",
		           (long)r->lineNo, s);
		return -1;
	}
	uint32_t off = 0; int nd = 0;
	while (ctl_is_digit(*s)) {
		off = off * 10u + (uint32_t)(*s - '0');
		s++;
		if (++nd > 9) { ctl_seterr(err, errCap, "line %ld: offset too large", (long)r->lineNo); return -1; }
	}
	if (!ctl_is_space(*s)) {
		ctl_seterr(err, errCap, "line %ld: missing space before the mask", (long)r->lineNo);
		return -1;
	}
	while (*s && ctl_is_space(*s)) s++;
	if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;   // 0x prefix tolerated
	if (ctl_hex(*s) < 0) {
		ctl_seterr(err, errCap, "line %ld: bad hex mask '%.8s'", (long)r->lineNo, s);
		return -1;
	}
	uint32_t m = 0; int nh = 0;
	while (ctl_hex(*s) >= 0) {
		m = m * 16u + (uint32_t)ctl_hex(*s);
		s++;
		if (++nh > 4) { ctl_seterr(err, errCap, "line %ld: mask wider than 16 bits", (long)r->lineNo); return -1; }
	}
	while (*s && ctl_is_space(*s)) s++;
	if (*s) {
		ctl_seterr(err, errCap, "line %ld: trailing garbage '%.8s'", (long)r->lineNo, s);
		return -1;
	}
	// Monotonicity: the playback loop only walks FORWARD (PM mp_bridge.cpp:1616-1647), so a
	// backwards offset can never be played — reject it instead of silently skipping the entry.
	if (r->n > 0 && off < r->lastOff) {
		ctl_seterr(err, errCap, "line %ld: offset %lu goes backwards (prev %lu)",
		           (long)r->lineNo, (unsigned long)off, (unsigned long)r->lastOff);
		return -1;
	}
	if (r->n >= (int32_t)CTL_REP_MAX) {
		ctl_seterr(err, errCap, "table too large (>%d entries)", (int)CTL_REP_MAX);
		return -1;
	}
	r->f[r->n] = off;
	r->mask[r->n] = (uint16_t)m;
	r->n++;
	r->lastOff = off;
	return 1;
}

void ctl_rep_init(CtlRep* r, int seat) {
	if (!r) return;
	// NOT a memset: the table arrays are ~48 KB and clearing them buys nothing (n bounds them).
	r->seat = (int8_t)(seat ? 1 : 0);
	r->state = CTL_REP_IDLE;
	r->anch.db = r->anch.sawNonField = r->anch.noted = 0;
	r->anchorFrame = 0;
	r->n = r->idx = 0;
	r->latest = 0; r->drain = 0;
	r->loading = r->loadErr = r->pendLen = r->skipping = 0;
	r->pend[0] = '\0';
	r->lineNo = 0; r->lastOff = 0;
	r->rHead = r->rCount = 0;
}

void ctl_rep_load_begin(CtlRep* r) {
	if (!r) return;
	r->state = CTL_REP_IDLE;                  // a failed/partial load must never leave one armed
	r->n = 0; r->idx = 0; r->latest = 0; r->drain = 0;
	r->loading = 1; r->loadErr = 0; r->pendLen = 0; r->skipping = 0; r->pend[0] = '\0';
	r->lineNo = 0; r->lastOff = 0;
	r->anchorFrame = 0;
	r->anch.db = 0; r->anch.sawNonField = 0; r->anch.noted = 0;
}

int ctl_rep_load_feed(CtlRep* r, const char* chunk, int len, char* err, int errCap) {
	if (!r || !r->loading || r->loadErr) return (r && r->loadErr) ? -1 : 0;
	if (!chunk || len <= 0) return 0;
	for (int i = 0; i < len; i++) {
		char c = chunk[i];
		if (c == '\n' || c == '\0') {
			if (r->skipping) { r->skipping = 0; r->lineNo++; r->pendLen = 0; continue; }
			r->pend[r->pendLen] = '\0';
			int rc = ctl_rep_line(r, r->pend, err, errCap);
			r->pendLen = 0;
			if (rc < 0) { r->loadErr = 1; return -1; }
		} else if (r->skipping) {
			continue;                                  // inside an over-long comment line
		} else if (r->pendLen >= (uint8_t)CTL_REP_LINE_MAX) {
			// An over-long COMMENT is legal and expected — the record header explains the format
			// in prose (and a hand-edited '# save:' note can be any length). Swallow it. An
			// over-long DATA line is a real malformation and fails loudly.
			r->pend[r->pendLen] = '\0';
			const char* s = r->pend;
			while (*s && ctl_is_space(*s)) s++;
			if (*s == '#') { r->skipping = 1; r->pendLen = 0; continue; }
			ctl_seterr(err, errCap, "line %ld longer than %d chars", (long)r->lineNo + 1,
			           (int)CTL_REP_LINE_MAX);
			r->loadErr = 1;
			return -1;
		} else {
			r->pend[r->pendLen++] = c;
		}
	}
	return 0;
}

int ctl_rep_load_end(CtlRep* r, char* err, int errCap) {
	if (!r) return -1;
	if (r->skipping) { r->skipping = 0; r->pendLen = 0; }   // trailing over-long comment, no newline
	if (!r->loadErr && r->pendLen > 0) {      // a last line with no trailing newline
		r->pend[r->pendLen] = '\0';
		if (ctl_rep_line(r, r->pend, err, errCap) < 0) r->loadErr = 1;
	}
	r->pendLen = 0;
	r->loading = 0;
	if (r->loadErr) { r->n = 0; r->state = CTL_REP_IDLE; return -1; }
	if (r->n == 0) {
		ctl_seterr(err, errCap, "empty table (no '<offset> <mask>' rows)");
		r->state = CTL_REP_IDLE;
		return -1;
	}
	r->idx = 0; r->latest = 0; r->drain = 0;
	r->state = CTL_REP_WAIT;
	ctl_rep_push(r, "replay armed: %ld entries, last f=%lu (waiting for the field-entry edge)",
	             (long)r->n, (unsigned long)r->f[r->n - 1]);
	return (int)r->n;
}

int ctl_rep_load(CtlRep* r, const char* text, char* err, int errCap) {
	if (err && errCap > 0) err[0] = '\0';
	if (!r) return -1;
	if (!text) {
		ctl_seterr(err, errCap, "null table text");
		r->loading = 0; r->n = 0; r->state = CTL_REP_IDLE;
		return -1;
	}
	ctl_rep_load_begin(r);
	const char* p = text;
	while (*p) {                              // fed in bounded slices: identical to the glue's path
		int n = 0;
		while (p[n] && n < 256) n++;
		if (ctl_rep_load_feed(r, p, n, err, errCap) < 0) break;
		p += n;
	}
	return ctl_rep_load_end(r, err, errCap);
}

bool ctl_rep_active(const CtlRep* r) { return r && r->state != CTL_REP_IDLE; }

void ctl_rep_abort(CtlRep* r, const char* why) {
	if (!r || r->state == CTL_REP_IDLE) return;   // idempotent
	if (why) ctl_rep_push(r, "replay ABORT %s at %ld/%ld", why, (long)r->idx, (long)r->n);
	r->state = CTL_REP_IDLE;
	r->latest = 0; r->drain = 0;
}

uint16_t ctl_rep_tick(CtlRep* r, const CtlIn* in) {
	if (!r || !in || r->state == CTL_REP_IDLE) return 0;

	// ---- D5.8 REAL INPUT WINS — same trigger and same wording as the D4.11 script rule, and for
	// the same reason (the human always outranks the harness). SCOPE (as for the D4 go-gate, see
	// ctl_tick): only while RUNNING. Reaching the anchor means the operator has to enter the field
	// BY HAND (that is the whole staging discipline, Appendix R2) — aborting on that input would
	// make an armed replay impossible to start.
	if (r->state == CTL_REP_RUN && in->realKeys != 0) {
		ctl_rep_push(r, "replay ABORT real-input at %ld/%ld", (long)r->idx, (long)r->n);
		r->state = CTL_REP_IDLE;
		r->latest = 0; r->drain = 0;
		return 0;
	}

	if (r->state == CTL_REP_WAIT) {
		if (!ctl_anchor_step(&r->anch, in, r->ring, &r->rHead, &r->rCount, (int)r->seat, "replay"))
			return 0;
		r->state = CTL_REP_RUN;
		r->anchorFrame = in->emuFrame;
		ctl_rep_push(r, "replay anchored f=%lu (%ld entries)", (unsigned long)in->emuFrame, (long)r->n);
		// fall through: an entry at offset 0 is due on this very tick
	}

	if (r->drain) {          // the table ran out last tick holding a non-zero mask: release + done
		ctl_rep_push(r, "replay done (%ld entries, %lu frames)", (long)r->n,
		             (unsigned long)(in->emuFrame - r->anchorFrame));
		r->state = CTL_REP_IDLE;
		r->latest = 0; r->drain = 0;
		return 0;
	}

	// PM's catch-up loop verbatim (mp_bridge.cpp:1616-1647): consume EVERY entry that is already
	// due and inject only the LATEST — so a slow render frame (or the wireless ~4-5 emu-fps) never
	// loses inputs, it just applies the newest state.
	uint32_t now = (uint32_t)(in->emuFrame - r->anchorFrame);
	while (r->idx < r->n && r->f[r->idx] <= now) r->latest = r->mask[r->idx++];

	if (r->idx >= r->n) {
		if (r->latest == 0) {   // the recording ended with the keys released — done immediately
			ctl_rep_push(r, "replay done (%ld entries, %lu frames)", (long)r->n, (unsigned long)now);
			r->state = CTL_REP_IDLE;
			r->drain = 0;
			return 0;
		}
		r->drain = 1;           // hold the final mask for THIS tick, release + finish on the next
	}
	return r->latest;
}

int ctl_rep_status(CtlRep* r, char* line, int cap) {
	if (!r) return 0;
	return ctl_ring_drain(r->ring, &r->rHead, &r->rCount, line, cap);
}

// D4.14's mirror, D5 half: the '# control' netlog line gets record/replay progress for free.
void ctl_publish_rr(int seat, const CtlRec* rec, const CtlRep* rep) {
	int s = seat ? 1 : 0;
	g_ctlStat[s].recLines = rec ? rec->lines : 0u;
	g_ctlStat[s].recState = (uint8_t)(!rec || !rec->armed ? 0 : (rec->anchored ? 2 : 1));
	g_ctlStat[s].repState = (uint8_t)(rep ? rep->state : 0);
	g_ctlStat[s].repIdx   = (uint16_t)(rep ? (rep->idx > 65535 ? 65535 : rep->idx) : 0);
	g_ctlStat[s].repN     = (uint16_t)(rep ? (rep->n   > 65535 ? 65535 : rep->n)   : 0);
}

// ============================================================================================
// D4-T (phase 22.1): the synthetic-touch script — see control.h for the grammar + design rules.
// Pure C, no I/O; host-tested in test/host/test_control.c (TOUCH-SCRIPT tests).
// ============================================================================================
void ctl_touch_init(CtlTouch* ts) { ts->n = ts->idx = 0; ts->phase = 0; ts->t = 0; }
bool ctl_touch_active(const CtlTouch* ts) { return ts->idx < ts->n; }
void ctl_touch_abort(CtlTouch* ts) { ts->n = ts->idx = 0; ts->phase = 0; ts->t = 0; }

// Whitespace-separated signed-int scanner (the replay loader's style: strict, loud).
static int ctt_num(const char** pp, int* out) {
	const char* p = *pp;
	while (*p == ' ' || *p == '\t') p++;
	int neg = 0;
	if (*p == '-') { neg = 1; p++; }
	if (*p < '0' || *p > '9') return 0;
	long v = 0;
	while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); if (v > 100000) return 0; p++; }
	*out = neg ? (int)-v : (int)v;
	*pp = p;
	return 1;
}
static int ctt_eol(const char* p) {   // only whitespace to end-of-line?
	while (*p == ' ' || *p == '\t' || *p == '\r') p++;
	return *p == '\0' || *p == '\n';
}

int ctl_touch_load(CtlTouch* ts, const char* text, char* err, int errCap) {
	const char* p = text;
	while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
	if (*p == '!') return 0;                                   // abort file — the glue handles it
	ctl_touch_init(ts);
	int line = 0;
	while (*p) {
		line++;
		while (*p == ' ' || *p == '\t') p++;
		if (*p == '\0') break;
		if (*p == '\n' || *p == '\r' || *p == '#') {           // blank / comment line
			while (*p && *p != '\n') p++;
			if (*p == '\n') p++;
			continue;
		}
		char op = *p++;
		CtlTouchEv e; e.x0 = e.y0 = e.x1 = e.y1 = 0; e.hold = 0; e.gap = 8; e.isWait = 0;
		int a, b, c, d, f, g;
		if (op == 't' || op == 'T') {
			if (!ctt_num(&p, &a) || !ctt_num(&p, &b) || !ctt_num(&p, &c))
				{ ctl_seterr(err, errCap, "line %d: t needs X Y HOLD", line); return -1; }
			g = 8; if (!ctt_eol(p) && !ctt_num(&p, &g))
				{ ctl_seterr(err, errCap, "line %d: bad GAP", line); return -1; }
			if (a < 0 || a > 319 || b < 0 || b > 239)
				{ ctl_seterr(err, errCap, "line %d: X/Y out of 320x240", line); return -1; }
			if (c < 1 || c > CTL_TOUCH_FRAMES_MAX || g < 0 || g > CTL_TOUCH_FRAMES_MAX)
				{ ctl_seterr(err, errCap, "line %d: HOLD/GAP out of range", line); return -1; }
			e.x0 = e.x1 = (int16_t)a; e.y0 = e.y1 = (int16_t)b;
			e.hold = (uint16_t)c; e.gap = (uint16_t)g;
		} else if (op == 'd' || op == 'D') {
			if (!ctt_num(&p, &a) || !ctt_num(&p, &b) || !ctt_num(&p, &c) || !ctt_num(&p, &d) || !ctt_num(&p, &f))
				{ ctl_seterr(err, errCap, "line %d: d needs X0 Y0 X1 Y1 FRAMES", line); return -1; }
			g = 8; if (!ctt_eol(p) && !ctt_num(&p, &g))
				{ ctl_seterr(err, errCap, "line %d: bad GAP", line); return -1; }
			if (a < 0 || a > 319 || b < 0 || b > 239 || c < 0 || c > 319 || d < 0 || d > 239)
				{ ctl_seterr(err, errCap, "line %d: X/Y out of 320x240", line); return -1; }
			if (f < 2 || f > CTL_TOUCH_FRAMES_MAX || g < 0 || g > CTL_TOUCH_FRAMES_MAX)
				{ ctl_seterr(err, errCap, "line %d: FRAMES/GAP out of range", line); return -1; }
			e.x0 = (int16_t)a; e.y0 = (int16_t)b; e.x1 = (int16_t)c; e.y1 = (int16_t)d;
			e.hold = (uint16_t)f; e.gap = (uint16_t)g;
		} else if (op == 'w' || op == 'W') {
			if (!ctt_num(&p, &f) || f < 1 || f > CTL_TOUCH_FRAMES_MAX)
				{ ctl_seterr(err, errCap, "line %d: w needs FRAMES 1..%d", line, CTL_TOUCH_FRAMES_MAX); return -1; }
			e.isWait = 1; e.hold = 0; e.gap = (uint16_t)f;
		} else {
			ctl_seterr(err, errCap, "line %d: unknown op '%c' (t/d/w)", line, op);
			return -1;
		}
		if (!ctt_eol(p)) { ctl_seterr(err, errCap, "line %d: trailing junk", line); return -1; }
		if (ts->n >= CTL_TOUCH_MAX)
			{ ctl_seterr(err, errCap, "too many ops (max %d)", CTL_TOUCH_MAX); ctl_touch_init(ts); return -1; }
		ts->ev[ts->n++] = e;
		while (*p && *p != '\n') p++;
		if (*p == '\n') p++;
	}
	if (ts->n == 0) { ctl_seterr(err, errCap, "empty touch script"); return -1; }
	ts->idx = 0; ts->phase = ts->ev[0].isWait ? 1 : 0; ts->t = 0;
	return ts->n;
}

int ctl_touch_tick(CtlTouch* ts, int* x, int* y) {
	if (ts->idx >= ts->n) return 0;
	const CtlTouchEv* e = &ts->ev[ts->idx];
	int down = 0;
	if (ts->phase == 0) {                                       // hold phase (touch down)
		// Linear interpolation for drags; a tap has x0==x1/y0==y1 so it degenerates for free.
		int span = (e->hold > 1) ? (e->hold - 1) : 1;
		int tt = (ts->t < span) ? (int)ts->t : span;
		if (x) *x = e->x0 + (int)((e->x1 - e->x0) * tt) / span;
		if (y) *y = e->y0 + (int)((e->y1 - e->y0) * tt) / span;
		down = 1;
		if (++ts->t >= e->hold) { ts->phase = 1; ts->t = 0; }
	} else {                                                    // gap phase (released)
		if (++ts->t >= e->gap) {
			ts->idx++; ts->t = 0;
			ts->phase = (ts->idx < ts->n && ts->ev[ts->idx].isWait) ? 1 : 0;
		}
	}
	return down;
}
