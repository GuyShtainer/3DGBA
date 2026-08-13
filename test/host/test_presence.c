// test_presence.c — PC host unit test for the phase-15 co-op presence DATA module
// (source/presence.{c,h} + source/fieldgate.h).
//
//   clang -std=c11 -Wall -Wextra -O0 -g -I source test/host/test_presence.c -o /tmp/tp && /tmp/tp
//
// (No mock <3ds.h> needed — presence.c is header-free pure C by design: CLAUDE.md rule #4,
//  PHASE.md invariant 8, SPEC-data D6.1. It also compiles clean at -O2, which is the line
//  SPEC-avatar A7.1 will use once the render half lands.)
//
// TEST NUMBERING is reserved across the two phase-15 specs (SPEC-data D6.5: "TEST 1-20 =
// SPEC-data, TEST 21+ = SPEC-avatar"), stated here the way test_tilt.c:8-11 states its own:
//
//   TEST 1  SUB() over -15..15 + sgn edges ......... SPEC-data D5.3
//   TEST 2  the D5.8 golden table, all 10 rows ..... SPEC-data D5.1/D5.3/D5.4
//   TEST 3  whole-step continuity at 1/2/4 px/f .... SPEC-data D5.8   <- THE bug this math invites
//   TEST 4  degradation ladder, field by field ..... SPEC-data D1.9
//   TEST 5  the gate ladder P-G1..P-G9 ............. SPEC-data D4.1
//   TEST 6  map universes .......................... SPEC-data D4.3
//   TEST 7  field_state_ok == tilt's G5-G8 ......... SPEC-data D4.6
//   TEST 8  staleness .............................. SPEC-data D3.5
//   TEST 9  two-tier presence ...................... SPEC-data D3.6
//   TEST 10 wedge + presence_hb_stall .............. SPEC-data D3.6.1
//   TEST 11 the hold (peer holds, self does not) ... SPEC-data D4.7
//   TEST 12 map change clears filter + hold ........ SPEC-data D4.7.1
//   TEST 13 presence_publish newest-wins ........... SPEC-data D3.4.2
//   TEST 14 smoothing / snap ....................... SPEC-data D5.5
//   TEST 15 teleport guard ......................... SPEC-data D5.6
//   TEST 16 culling ................................ SPEC-data D5.7
//   TEST 17 presence_name_ascii .................... SPEC-data D6.3.1
//   TEST 18 presence_dir ........................... SPEC-data D2.4
//   TEST 19 the 48-byte record layout .............. SPEC-data D3.2
//   TEST 20 solve purity + reset ................... SPEC-data D6.2
//   TEST 21 identity refresh cadence ............... SPEC-data D2.3      <- slice M1
//   TEST 22 presence_fill_core (the producer) ...... SPEC-data D2.2/D3.2 <- slice M1
//   TEST 23 the gate truth table from RAW STATE .... SPEC-data D4.1      <- slice M1
//   TEST 24 the charmap goldens via the producer ... SPEC-data D6.3.1    <- slice M1
//   TEST 25 sheet cell arithmetic .................. SPEC-avatar A1.2/A1.3 (P3)  <- slice M2
//   TEST 26 draw-position math + the anchor identity SPEC-avatar A0.2/A1.1 (P1/P2)
//   TEST 27 culling and clipping ................... SPEC-avatar A2.4      (P4)
//   TEST 28 the MIRRORED clip stays registered ..... SPEC-avatar A1.2.2/A2.4.2
//   TEST 29 the tilt composition is a PURE TRANSLATION SPEC-avatar A3.1    (P5)
//   TEST 30 placeholder art invariants ............. SPEC-avatar A1.5      (P10)
//   TEST 31 walk-frame selection ................... SPEC-avatar A2.6.3/4  (P9)
//   TEST 32 the y-sort comparator .................. SPEC-avatar A3.4      (P6)
//   TEST 33 the charmap decoder (gbatext) .......... SPEC-avatar A4.1/A4.2 (P7)  <- slice M3
//   TEST 34 adjacency + facing (the meeting rule) .. SPEC-avatar A5.1/A5.2 (P8)
//   TEST 35 the card FSM + the surface policy ...... SPEC-avatar A5.4/A4.4.2
//   TEST 36 the card's text + the formatters ....... SPEC-avatar A4.3.2/A4.4.4/A5.5.3
//   TEST 37 the co-op pref's semantics ............. SPEC-avatar A6.3      (P12, the half
//                                                    test_tilt.c TEST 6's byte mirror cannot see)
//
// NUMBERING NOTE (slice M1): D6.5 reserved 1-20 for the data half and 21+ for SPEC-avatar's render
// suite, but the data half needed four more when the producer landed. They take 21-24; the render
// half starts at 25. Recorded here and in BUILDLOG so the two cannot collide.
//
// SPEC-avatar A7.1's P-series maps onto the numbers above. P11 (the degradation ladder) is covered
// end to end by TEST 4/5/23. P12 (the settings ladder) is SPLIT on purpose: the byte layout and the
// length ladder are mirrored in test_tilt.c TEST 6, where the Settings mirror lives and where
// main.c's _Static_asserts point, and TEST 37 here pins what that mirror cannot see — what the
// stored word MEANS once loaded.
//
// TEST 7 pulls in source/tilt.c as well: proving that the phase-14 gate and the phase-15 gate are
// the SAME predicate is the whole point of factoring it into fieldgate.h, and the proof is worth
// nothing if it is asserted against a copy of the rules instead of against tilt_target_level
// itself. (test_tilt.c's own 1694 checks, passing unmodified, are the other half of that proof —
// SPEC-data D4.6.1.)

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>     // offsetof — TEST 19 pins every field offset of the wire record

#include "../../source/presence.c"
#include "../../source/presence_art.c"   // slice M2: the render half's pure-C core (TEST 25-32)
#include "../../source/gbatext.c"        // slice M3: the charmap decoder      (TEST 33)
#include "../../source/presence_ui.c"    // slice M3: meet / card / surfaces   (TEST 34-36)
#include "../../source/tilt.c"     // TEST 7 only: the gate this one was factored out of
                                   // ...and TEST 29: the REAL tilt_view_init/tilt_project, because
                                   // "the billboard is a pure translation" is worth nothing if it
                                   // is asserted against a re-implementation of the projection.

static int g_checks = 0, g_fail = 0;
#define CHECK(cond, ...) do { \
	g_checks++; \
	if (!(cond)) { g_fail++; printf("  [FAIL] "); printf(__VA_ARGS__); printf("   (at %s:%d)\n", __FILE__, __LINE__); } \
} while (0)
#define EQI(a, b, ...) do { \
	g_checks++; \
	if ((long)(a) != (long)(b)) { g_fail++; printf("  [FAIL] "); printf(__VA_ARGS__); \
		printf("  got %ld want %ld   (at %s:%d)\n", (long)(a), (long)(b), __FILE__, __LINE__); } \
} while (0)
#define EQF(a, b, ...) do { \
	g_checks++; \
	double d_ = (double)(a) - (double)(b); if (d_ < 0) d_ = -d_; \
	if (d_ > 1e-4) { g_fail++; printf("  [FAIL] "); printf(__VA_ARGS__); \
		printf("  got %.6f want %.6f   (at %s:%d)\n", (double)(a), (double)(b), __FILE__, __LINE__); } \
} while (0)

// ============================================================================================
// Fixtures
// ============================================================================================

#define MAPG 3
#define MAPN 12

// A fully healthy record: save loaded, in the free-roam overworld, object event agreeing,
// identity latched, camera + heartbeat mapped. Tests then break exactly one thing at a time.
static PeerPresence rec_ok(int px, int py, int subX, int subY) {
	PeerPresence r;
	presence_rec_init(&r, 0, PRES_GAME_HOENN);
	r.flags    = PRES_F_SB1VALID | PRES_F_FIELD | PRES_F_OBJOK | PRES_F_IDENT | PRES_F_CAM | PRES_F_HB;
	r.ctx      = FIELD_CTX_OVERWORLD;
	r.px       = (int16_t)px;      r.py   = (int16_t)py;
	r.objX     = (int16_t)(px + PRES_MAP_OFFSET);
	r.objY     = (int16_t)(py + PRES_MAP_OFFSET);
	r.subX     = (int8_t)subX;     r.subY = (int8_t)subY;
	r.mapGroup = MAPG;             r.mapNum = MAPN;
	r.facing   = PRES_DIR_SOUTH;
	r.gender   = 0;
	r.tid      = 1234;
	r.hb       = 1;
	return r;
}

typedef struct { PresenceState ps; uint32_t round; } Harness;

static void h_init(Harness* h) { presence_reset(&h->ps); h->round = 1; }

// One rendered frame: begin the round, publish the peer (stamping a monotonic round + an
// ADVANCING heartbeat, which is what a healthy game looks like), then solve for this screen.
static int h_step(Harness* h, PeerPresence self, PeerPresence peer, PresenceOut* out) {
	presence_begin_round(&h->ps, h->round);
	peer.round = h->round;
	peer.hb    = h->round;
	presence_publish(&h->ps, 0, &peer);
	PresenceIn in; memset(&in, 0, sizeof in);
	in.enabled = 1; in.slot = 0; in.self = self;
	int d = presence_solve(&h->ps, &in, out);
	h->round++;
	return d;
}

// Same, but WITHOUT publishing — the low-rate/beacon case (the record ages by one round).
static int h_step_nopub(Harness* h, PeerPresence self, PresenceOut* out) {
	presence_begin_round(&h->ps, h->round);
	PresenceIn in; memset(&in, 0, sizeof in);
	in.enabled = 1; in.slot = 0; in.self = self;
	int d = presence_solve(&h->ps, &in, out);
	h->round++;
	return d;
}

// ============================================================================================
// TEST 1 — SUB() (SPEC-data D5.3)
// ============================================================================================
static void test_sub(void) {
	printf("TEST 1: SUB(c) = -c + 16*sgn(c) over the whole camera-phase range\n");
	EQI(presence_sub(0),    0, "SUB(0)");
	EQI(presence_sub(1),   15, "SUB(1)");
	EQI(presence_sub(8),    8, "SUB(8)");
	EQI(presence_sub(15),   1, "SUB(15)");
	EQI(presence_sub(-1), -15, "SUB(-1)");
	EQI(presence_sub(-8),  -8, "SUB(-8)");
	EQI(presence_sub(-15), -1, "SUB(-15)");
	for (int c = 1; c <= 15; c++)  EQI(presence_sub(c),  16 - c, "SUB(%d)", c);
	for (int c = -15; c <= -1; c++) EQI(presence_sub(c), -16 - c, "SUB(%d)", c);
	// The identity the whole sub-tile story rests on: SUB(c) + c == 16*sgn(c), i.e. a tile index
	// and its phase always sum to a whole tile. Getting this wrong is the 15-px-hitch bug.
	for (int c = -15; c <= 15; c++) {
		int sgn = (c > 0) - (c < 0);
		EQI(presence_sub(c) + c, 16 * sgn, "SUB(%d)+%d", c, c);
	}
	// The +-15 sanity guard lives in the CALLER (cam_phase), not in SUB: out of range -> 0.
	EQI(cam_phase(16),  0, "cam_phase(16) is garbage -> 0, never clamped to 15");
	EQI(cam_phase(-16), 0, "cam_phase(-16) -> 0");
	EQI(cam_phase(127), 0, "cam_phase(127) -> 0");
	EQI(cam_phase(15), 15, "cam_phase(15) passes");
	EQI(cam_phase(-15), -15, "cam_phase(-15) passes");
}

// ============================================================================================
// TEST 2 — the D5.8 golden table (SPEC-data D5.1/D5.3/D5.4)
// ============================================================================================
static void golden(const char* what, int apx, int apy, int acx, int acy,
                   int bpx, int bpy, int bcx, int bcy, int cam, float wantX, float wantY) {
	Harness h; h_init(&h);
	PeerPresence peer = rec_ok(apx, apy, acx, acy);
	PeerPresence self = rec_ok(bpx, bpy, bcx, bcy);
	if (!cam) { peer.flags &= ~(unsigned)PRES_F_CAM; self.flags &= ~(unsigned)PRES_F_CAM; }
	PresenceOut o;
	int d = h_step(&h, self, peer, &o);
	EQI(d, 1, "%s: must draw", what);
	EQF(o.footX, wantX, "%s: footX", what);
	EQF(o.footY, wantY, "%s: footY", what);
	EQI(o.dTileX, apx - bpx, "%s: dTileX", what);
	EQI(o.dTileY, apy - bpy, "%s: dTileY", what);
}

static void test_golden(void) {
	printf("TEST 2: the D5.8 golden anchor table (A = peer, B = host)\n");
	golden("1 same tile, both still",  10,10, 0,0,  10,10, 0,0, 1, 120.0f, 88.0f);
	golden("2 peer +3 right 2 up",     13, 8, 0,0,  10,10, 0,0, 1, 168.0f, 56.0f);
	golden("3 host steps right, f1",   10,10, 0,0,  11,10, 1,0, 1, 119.0f, 88.0f);
	golden("4 host mid-step",          10,10, 0,0,  11,10, 8,0, 1, 112.0f, 88.0f);
	golden("5 host step complete",     10,10, 0,0,  11,10, 0,0, 1, 104.0f, 88.0f);
	golden("6 peer steps right, f1",   11,10, 1,0,  10,10, 0,0, 1, 121.0f, 88.0f);
	golden("7 peer step complete",     11,10, 0,0,  10,10, 0,0, 1, 136.0f, 88.0f);
	golden("8 host steps left, f1",    10,10, 0,0,   9,10,-1,0, 1, 121.0f, 88.0f);
	golden("9 peer walks down, mid",   10,11, 0,8,  10,10, 0,0, 1, 120.0f, 96.0f);
	golden("10 no camera field",       11,10, 0,0,  10,10, 0,0, 0, 136.0f, 88.0f);

	// The anchor identity, restated as a sweep: peer == host at ANY camera phase must land exactly
	// on PRES_ANCHOR_X/Y, because both sides' SUB() terms cancel. This is the cheapest possible
	// regression test for the anchor convention (SPEC-avatar A0.2.1 asserts the same thing).
	for (int c = -15; c <= 15; c++) {
		Harness h; h_init(&h);
		PeerPresence r = rec_ok(20, 30, c, -c);
		PresenceOut o;
		EQI(h_step(&h, r, r, &o), 1, "identity draw at phase %d", c);
		EQF(o.footX, PRES_ANCHOR_X, "identity footX at phase %d", c);
		EQF(o.footY, PRES_ANCHOR_Y, "identity footY at phase %d", c);
	}
}

// ============================================================================================
// TEST 3 — whole-step continuity (SPEC-data D5.8)
// ============================================================================================
// "A discontinuity at the tile boundary is the single most likely bug in this math" — the tile
// index and the phase change on the SAME frame, and getting SUB()'s sign or the sgn term wrong
// produces a plausible-looking 15 px hitch once per tile. So: simulate a whole step at 1 px/frame
// (walk), 2 (run) and 4 (bike), for the host and for the peer, on both axes, in both directions,
// and assert the per-frame delta is EXACTLY the speed on every frame including the boundary.
static void step_walk(int moverIsPeer, int axisY, int dir, int speed) {
	int n = 16 / speed;
	Harness h; h_init(&h);
	float prev = 0.0f;
	float total = 0.0f;
	for (int k = 0; k <= n; k++) {
		int tile = (k == 0) ? 0 : dir;                       // pos advances at the FIRST frame
		int ph   = (k == 0 || k == n) ? 0 : dir * k * speed; // ...and the phase runs it out
		PeerPresence mover = axisY ? rec_ok(10, 10 + tile, 0, ph) : rec_ok(10 + tile, 10, ph, 0);
		PeerPresence still = rec_ok(10, 10, 0, 0);
		PresenceOut o;
		int d = moverIsPeer ? h_step(&h, still, mover, &o) : h_step(&h, mover, still, &o);
		EQI(d, 1, "step: must draw (peer=%d axisY=%d dir=%d s=%d k=%d)", moverIsPeer, axisY, dir, speed, k);
		float f = axisY ? o.footY : o.footX;
		if (k > 0) {
			// host moving one way slides the peer the OTHER way; peer moving slides it the same way
			float want = (float)((moverIsPeer ? dir : -dir) * speed);
			EQF(f - prev, want, "per-frame delta (peer=%d axisY=%d dir=%d s=%d k=%d)",
			    moverIsPeer, axisY, dir, speed, k);
			total += f - prev;
		}
		prev = f;
	}
	EQF(total, (float)((moverIsPeer ? dir : -dir) * 16), "total travel (peer=%d axisY=%d dir=%d s=%d)",
	    moverIsPeer, axisY, dir, speed);
}

static void test_continuity(void) {
	printf("TEST 3: whole-step continuity at 1/2/4 px per frame, both roles, both axes, both directions\n");
	const int speeds[3] = { 1, 2, 4 };
	for (int s = 0; s < 3; s++)
		for (int peer = 0; peer < 2; peer++)
			for (int axisY = 0; axisY < 2; axisY++)
				for (int dir = -1; dir <= 1; dir += 2)
					step_walk(peer, axisY, dir, speeds[s]);
}

// ============================================================================================
// TEST 4 — the degradation ladder (SPEC-data D1.9)
// ============================================================================================
static void test_degrade(void) {
	printf("TEST 4: every zeroed profile field degrades to a SANE frame, never garbage\n");
	PresenceOut o;

	{   // profile_for() == NULL for the peer -> presence OFF for the pair, no read, no draw
		Harness h; h_init(&h);
		PeerPresence peer = rec_ok(12, 10, 0, 0); peer.gameId = PRES_GAME_NONE;
		EQI(h_step(&h, rec_ok(10, 10, 0, 0), peer, &o), 0, "no peer profile -> off");
		EQI(o.reason, PRES_OFF_NOPROF, "no peer profile -> NOPROF");
	}
	{   // ...and for the self side
		Harness h; h_init(&h);
		PeerPresence self = rec_ok(10, 10, 0, 0); self.gameId = PRES_GAME_NONE;
		EQI(h_step(&h, self, rec_ok(12, 10, 0, 0), &o), 0, "no self profile -> off");
		EQI(o.reason, PRES_OFF_NOPROF, "no self profile -> NOPROF");
	}
	{   // sb1ptr deref invalid -> sb1Valid clear -> the field gate closes (never a garbage tile)
		Harness h; h_init(&h);
		PeerPresence peer = rec_ok(12, 10, 0, 0);
		peer.flags &= ~(unsigned)PRES_F_SB1VALID; peer.px = -1; peer.py = -1;
		EQI(h_step(&h, rec_ok(10, 10, 0, 0), peer, &o), 0, "peer sb1 invalid -> off");
		EQI(o.reason, PRES_OFF_FIELD, "peer sb1 invalid -> FIELD");
	}
	{   // fieldCamera == 0 -> PRES_F_CAM clear -> whole-tile snap, <=15 px error, NEVER a wrong tile
		Harness h; h_init(&h);
		PeerPresence peer = rec_ok(12, 10, 9, 0), self = rec_ok(10, 10, 5, 0);
		peer.flags &= ~(unsigned)PRES_F_CAM; self.flags &= ~(unsigned)PRES_F_CAM;
		EQI(h_step(&h, self, peer, &o), 1, "no camera -> still draws");
		EQF(o.footX, PRES_ANCHOR_X + 32.0f, "no camera -> exact whole tiles");
		EQI(o.dTileX, 2, "no camera -> tile delta still right");
	}
	{   // sb2ptr == 0 -> identity unavailable: gender defaults MALE, name empty, avatar STILL draws
		Harness h; h_init(&h);
		PeerPresence peer = rec_ok(12, 10, 0, 0);
		peer.flags &= ~(unsigned)PRES_F_IDENT; peer.gender = 1; peer.tid = 0;
		EQI(h_step(&h, rec_ok(10, 10, 0, 0), peer, &o), 1, "no identity -> still draws");
		EQI(o.gender, 0, "no identity -> gender defaults MALE");
		char nm[9]; EQI(presence_name_ascii(peer.name, nm), 0, "no identity -> empty name");
		EQI(nm[0], 0, "name is NUL-terminated");
	}
	{   // mapObjects == 0 -> objX/objY sentinel -> P-G7 SKIPPED (unavailable), reported not passed
		Harness h; h_init(&h);
		PeerPresence peer = rec_ok(12, 10, 0, 0); peer.objX = -1; peer.objY = -1;
		peer.flags &= ~(unsigned)PRES_F_OBJOK;
		EQI(presence_obj_agree(&peer), -1, "unmapped object slot -> UNAVAILABLE (not a pass)");
		EQI(h_step(&h, rec_ok(10, 10, 0, 0), peer, &o), 1, "unmapped mapObjects -> still draws");
		EQI(o.dir, PRES_DIR_SOUTH, "facing degrades to a plausible default, never out of range");
	}
	{   // hbCtr == 0 -> PRES_F_HB clear -> wedge detection DISABLED, never "wedged" by default
		Harness h; h_init(&h);
		PeerPresence peer = rec_ok(12, 10, 0, 0), self = rec_ok(10, 10, 0, 0);
		peer.flags &= ~(unsigned)PRES_F_HB;
		for (int i = 0; i < 400; i++) {
			presence_begin_round(&h.ps, h.round);
			peer.round = h.round; peer.hb = 0;              // frozen forever, and that is FINE
			presence_publish(&h.ps, 0, &peer);
			PresenceIn in; memset(&in, 0, sizeof in);
			in.enabled = 1; in.slot = 0; in.self = self;
			presence_solve(&h.ps, &in, &o);
			h.round++;
		}
		EQI(o.draw, 1, "no heartbeat mapped -> liveness is freshness only, still ACTIVE");
		EQI(o.liveness, PRES_LIVE_ACTIVE, "no heartbeat mapped -> never 'wedged'");
		EQI((long)presence_hb_stall(&h.ps, 0), 0, "no heartbeat mapped -> stall reads 0");
	}
}

// ============================================================================================
// TEST 5 — the gate ladder (SPEC-data D4.1)
// ============================================================================================
static int solve_once(PresenceState* ps, const PeerPresence* self, const PeerPresence* peer,
                      int enabled, int menuOpen, int linkAny, PresenceOut* out) {
	presence_reset(ps);
	presence_begin_round(ps, 1);
	PeerPresence p = *peer; p.round = 1;
	presence_publish(ps, 0, &p);
	PresenceIn in; memset(&in, 0, sizeof in);
	in.enabled = enabled; in.menuOpen = menuOpen; in.linkAny = linkAny; in.slot = 0; in.self = *self;
	return presence_solve(ps, &in, out);
}

static void test_gate(void) {
	printf("TEST 5: the gate ladder P-G1..P-G9 — one distinct reason each, first hit wins\n");
	PresenceState ps; PresenceOut o;
	PeerPresence self = rec_ok(10, 10, 0, 0), peer = rec_ok(12, 10, 0, 0);

	EQI(solve_once(&ps, &self, &peer, 1, 0, 0, &o), 1, "all clear -> draws");
	EQI(o.reason, PRES_OFF_NONE, "all clear -> reason ok");

	EQI(solve_once(&ps, &self, &peer, 0, 0, 0, &o), 0, "G1 pref off");
	EQI(o.reason, PRES_OFF_DISABLED, "G1 reason");
	EQI(solve_once(&ps, &self, &peer, 1, 1, 0, &o), 0, "G2 pause menu");
	EQI(o.reason, PRES_OFF_MENU, "G2 reason");
	EQI(solve_once(&ps, &self, &peer, 1, 0, 1, &o), 0, "G3 link/net/wireless live");
	EQI(o.reason, PRES_OFF_LINK, "G3 reason");
	{ PeerPresence b = peer; b.gameId = PRES_GAME_NONE;
	  EQI(solve_once(&ps, &self, &b, 1, 0, 0, &o), 0, "G4 unmapped game");
	  EQI(o.reason, PRES_OFF_NOPROF, "G4 reason"); }
	{ PeerPresence b = peer; b.gameId = PRES_GAME_KANTO;
	  EQI(solve_once(&ps, &self, &b, 1, 0, 0, &o), 0, "G5 map universe");
	  EQI(o.reason, PRES_OFF_UNIVERSE, "G5 reason"); }
	{ PeerPresence a = self; a.flags &= ~(unsigned)PRES_F_FIELD;
	  EQI(solve_once(&ps, &a, &peer, 1, 0, 0, &o), 0, "G6 self not in the field");
	  EQI(o.reason, PRES_OFF_SELF, "G6-self reason"); }
	{ PeerPresence a = self; a.objX = 99;
	  EQI(solve_once(&ps, &a, &peer, 1, 0, 0, &o), 0, "G7 self object disagrees");
	  // FIX PASS (review finding 7): its OWN code, so the readout can distinguish "the host is not
	  // in the field" from "the host's gObjectEvents[0] disagrees with its SaveBlock1" — the second
	  // is what a wrong mapObjects address would look like.
	  EQI(o.reason, PRES_OFF_SELFOBJ, "G7-self reason"); }
	// FIX PASS (review finding 6): a textbox closes the gate on the SELF side ONLY. The host's own
	// dialog box is drawn in the frame this avatar composites over; the peer's is on the peer's
	// screen, and their position stays perfectly valid while they read a sign.
	{ PeerPresence a = self; a.flags |= (unsigned)PRES_F_TEXT;
	  EQI(solve_once(&ps, &a, &peer, 1, 0, 0, &o), 0, "a textbox on OUR screen closes the gate");
	  EQI(o.reason, PRES_OFF_SELF, "...as a SELF-side field failure"); }
	{ PeerPresence b = peer; b.flags |= (unsigned)PRES_F_TEXT;
	  EQI(solve_once(&ps, &self, &b, 1, 0, 0, &o), 1, "a textbox on the PEER's screen keeps drawing");
	  EQI(o.reason, PRES_OFF_NONE, "...with no reason code at all"); }
	{ PeerPresence b = peer; b.flags &= ~(unsigned)PRES_F_FIELD;
	  EQI(solve_once(&ps, &self, &b, 1, 0, 0, &o), 0, "G6 peer not in the field");
	  EQI(o.reason, PRES_OFF_FIELD, "G6-peer reason"); }
	{ PeerPresence b = peer; b.objX = 99;
	  EQI(solve_once(&ps, &self, &b, 1, 0, 0, &o), 0, "G7 peer object disagrees");
	  EQI(o.reason, PRES_OFF_OBJ, "G7-peer reason"); }
	{ PeerPresence b = peer; b.mapNum = MAPN + 1;
	  EQI(solve_once(&ps, &self, &b, 1, 0, 0, &o), 0, "G8 different map");
	  EQI(o.reason, PRES_OFF_MAP, "G8 reason"); }
	{ // G9: fresh + in the field but the heartbeat is wedged -> CONNECTED, never ACTIVE, never drawn
	  presence_reset(&ps);
	  PeerPresence b = peer;
	  for (uint32_t r = 1; r <= 200; r++) {
	  	presence_begin_round(&ps, r); b.round = r; b.hb = 7;   // frozen
	  	presence_publish(&ps, 0, &b);
	  }
	  PresenceIn in; memset(&in, 0, sizeof in);
	  in.enabled = 1; in.slot = 0; in.self = self;
	  EQI(presence_solve(&ps, &in, &o), 0, "G9 wedged peer");
	  EQI(o.reason, PRES_OFF_LIVE, "G9 reason");
	  EQI(o.liveness, PRES_LIVE_CONNECTED, "G9 liveness is CONNECTED, not ACTIVE"); }

	// P-G4's second job (D1.9): NO record published at all must be OFF, not a draw at (0,0).
	{ presence_reset(&ps); presence_begin_round(&ps, 1);
	  PresenceIn in; memset(&in, 0, sizeof in);
	  in.enabled = 1; in.slot = 0; in.self = self;
	  EQI(presence_solve(&ps, &in, &o), 0, "no record ever published -> off");
	  EQI(o.reason, PRES_OFF_NOPROF, "no record -> NOPROF");
	  EQI(o.liveness, PRES_LIVE_NONE, "no record -> liveness NONE"); }

	// ORDER INDEPENDENCE: with two rules broken at once the EARLIER one wins and the answer is
	// still "off" — no later rule can ever re-open a gate an earlier one closed.
	{ PeerPresence b = peer; b.mapNum = MAPN + 1; b.gameId = PRES_GAME_KANTO;
	  EQI(solve_once(&ps, &self, &b, 0, 1, 1, &o), 0, "four rules at once");
	  EQI(o.reason, PRES_OFF_DISABLED, "four rules at once -> the FIRST wins"); }
	{ PeerPresence b = peer; b.mapNum = MAPN + 1;
	  EQI(solve_once(&ps, &self, &b, 1, 0, 1, &o), 0, "link + wrong map");
	  EQI(o.reason, PRES_OFF_LINK, "link beats map"); }

	// Every reason code has a distinct, non-empty HUD string ("it did not draw" must answer "why").
	for (int i = 0; i < PRES_OFF__COUNT; i++) {
		const char* a = presence_off_reason(i);
		CHECK(a && a[0], "reason %d has a name", i);
		for (int j = i + 1; j < PRES_OFF__COUNT; j++)
			CHECK(strcmp(a, presence_off_reason(j)) != 0, "reason %d and %d differ", i, j);
	}
	CHECK(strcmp(presence_off_reason(-1), "?") == 0, "out-of-range reason is '?'");
	CHECK(strcmp(presence_off_reason(PRES_OFF__COUNT), "?") == 0, "out-of-range reason is '?'");
}

// ============================================================================================
// TEST 6 — map universes (SPEC-data D4.3)
// ============================================================================================
static void test_universes(void) {
	printf("TEST 6: (mapGroup,mapNum) is only meaningful within ONE game's map table\n");
	EQI(presence_game_id("BPEE"), PRES_GAME_HOENN, "BPEE -> Hoenn");
	EQI(presence_game_id("BPRE"), PRES_GAME_KANTO, "BPRE -> Kanto");
	EQI(presence_game_id("BPGE"), PRES_GAME_KANTO, "BPGE -> Kanto (FR/LG share one map table)");
	// PHASE 18 / SPEC-coop P3.1.4. Ruby and Sapphire now HAVE a universe — their own, never
	// Emerald's. This assertion used to read "AXVE -> NONE (Ruby is not assumed Hoenn-compatible)";
	// the assumption it guarded against is still banned, and now it is banned by a DIFFERENT id
	// rather than by having no id at all. The evidence (re-derived independently 2026-08-13):
	// pret/pokeruby and pret/pokeemerald data/maps/map_groups.json share 393 (group, num) slots and
	// 76 of them name a DIFFERENT map, with the divergence starting at group 0 index 50
	// (Underwater1 vs Underwater_Route124). See presence.h's PRES_GAME_HOENN_RS note.
	EQI(presence_game_id("AXVE"), PRES_GAME_HOENN_RS, "AXVE -> Hoenn RS, its OWN universe");
	EQI(presence_game_id("AXPE"), PRES_GAME_HOENN_RS, "AXPE -> Hoenn RS (Ruby and Sapphire are one build)");
	CHECK(PRES_GAME_HOENN_RS != PRES_GAME_HOENN, "Ruby is NOT assumed to be Emerald-compatible");
	CHECK(PRES_GAME_HOENN_RS != PRES_GAME_KANTO, "...nor Kanto");
	CHECK(PRES_GAME_HOENN_RS != PRES_GAME_NONE,  "...and it IS a real universe, not 'no profile'");
	EQI(presence_game_id("XXXX"), PRES_GAME_NONE,  "unknown code -> none");
	EQI(presence_game_id(NULL),   PRES_GAME_NONE,  "NULL -> none");
	// The four pair cases P3.1.4 names, at the level the gate actually uses (id equality).
	CHECK(presence_game_id("AXVE") == presence_game_id("AXPE"), "AXVE + AXPE = same universe");
	CHECK(presence_game_id("AXVE") != presence_game_id("BPEE"), "AXVE + BPEE = different");
	CHECK(presence_game_id("AXPE") != presence_game_id("BPEE"), "AXPE + BPEE = different");
	CHECK(presence_game_id("AXVE") != presence_game_id("BPRE"), "AXVE + BPRE = different");

	PresenceState ps; PresenceOut o;
	PeerPresence em = rec_ok(10, 10, 0, 0);                          // Emerald
	PeerPresence fr = rec_ok(12, 10, 0, 0); fr.gameId = PRES_GAME_KANTO;
	PeerPresence lg = rec_ok(12, 10, 0, 0); lg.gameId = PRES_GAME_KANTO;

	// The user's ACTUAL carts, on identical (mapGroup, mapNum): must be OFF, not a peer walking
	// around an unrelated map.
	EQI(solve_once(&ps, &em, &fr, 1, 0, 0, &o), 0, "Emerald + FireRed on 'the same' map -> OFF");
	EQI(o.reason, PRES_OFF_UNIVERSE, "-> UNIVERSE, not a mysterious position bug");
	EQI(presence_same_map(&em, &fr), 0, "same_map is false across universes");

	PeerPresence frSelf = fr; frSelf.px = 10; frSelf.objX = 17;
	EQI(solve_once(&ps, &frSelf, &lg, 1, 0, 0, &o), 1, "FireRed + LeafGreen -> allowed");
	EQI(presence_same_map(&frSelf, &lg), 1, "same_map is true within Kanto");
	{ PeerPresence b = lg; b.mapGroup = MAPG + 1;
	  EQI(presence_same_map(&frSelf, &b), 0, "different mapGroup -> not same map"); }
	{ PeerPresence b = lg; b.mapNum = (int8_t)-100;   // FR/LG map numbers >= 128 read back signed
	  EQI(presence_same_map(&frSelf, &b), 0, "different mapNum -> not same map"); }
	// s8 round trip: a >=128 map number must compare equal to itself, not wrap into a false match.
	{ PeerPresence a = frSelf, b = lg; a.mapNum = (int8_t)0x80; b.mapNum = (int8_t)0x80;
	  EQI(presence_same_map(&a, &b), 1, "mapNum 0x80 compares equal to itself"); }

	// PHASE 18 / SPEC-coop P3.1 — the Ruby/Sapphire universe, driven through the REAL ladder, not
	// just through presence_game_id. The three pairs that matter, all with IDENTICAL coordinates so
	// the only thing under test is the universe rule:
	//   Ruby + Sapphire  -> DRAWS      (one build, one map table)
	//   Ruby + Emerald   -> OFF_UNIVERSE  (76 of 393 shared slots name a different map)
	//   Ruby + FireRed   -> OFF_UNIVERSE
	PeerPresence ru = rec_ok(12, 10, 0, 0); ru.gameId = PRES_GAME_HOENN_RS;
	PeerPresence sa = rec_ok(12, 10, 0, 0); sa.gameId = PRES_GAME_HOENN_RS;
	PeerPresence ruSelf = ru; ruSelf.px = 10; ruSelf.objX = 17;
	EQI(solve_once(&ps, &ruSelf, &sa, 1, 0, 0, &o), 1, "Ruby + Sapphire -> allowed");
	EQI(presence_same_map(&ruSelf, &sa), 1, "same_map is true within Hoenn RS");
	EQI(solve_once(&ps, &ruSelf, &em, 1, 0, 0, &o), 0, "Ruby + Emerald -> OFF");
	EQI(o.reason, PRES_OFF_UNIVERSE, "-> UNIVERSE: Ruby's (3,12) is not Emerald's (3,12)");
	EQI(presence_same_map(&ruSelf, &em), 0, "same_map is false Ruby vs Emerald");
	EQI(solve_once(&ps, &ruSelf, &fr, 1, 0, 0, &o), 0, "Ruby + FireRed -> OFF");
	EQI(o.reason, PRES_OFF_UNIVERSE, "-> UNIVERSE");
	// ...and the symmetric direction, because a one-sided gate would draw a peer on exactly one of
	// the two screens — the most confusing failure this module could produce.
	{ PeerPresence emSelf = em; emSelf.px = 10; emSelf.objX = 17;
	  EQI(solve_once(&ps, &emSelf, &ru, 1, 0, 0, &o), 0, "Emerald + Ruby -> OFF (the other way round)");
	  EQI(o.reason, PRES_OFF_UNIVERSE, "-> UNIVERSE, symmetrically"); }
}

// ============================================================================================
// TEST 7 — field_state_ok IS tilt's G5-G8 (SPEC-data D4.6)
// ============================================================================================
static void test_fieldgate(void) {
	printf("TEST 7: fieldgate.h agrees with tilt_target_level's G5-G8 over the enumerated space\n");
	CHECK(FIELD_CTX_OVERWORLD == TILT_CTX_FIELD, "TILT_CTX_FIELD is an alias of FIELD_CTX_OVERWORLD");
	CHECK(FIELD_CTX_OVERWORLD == 1, "== GCTX_OVERWORLD (gamestate.h:12)");

	// An all-clear tilt gate (every rule G1-G11 satisfied) with only the four field inputs swept.
	for (int ok = 0; ok <= 1; ok++)
	for (int ctx = 0; ctx <= 8; ctx++)          // the nine GameCtx values
	for (int sb1 = 0; sb1 <= 1; sb1++)
	for (int pxi = 0; pxi < 3; pxi++)
	for (int txt = 0; txt <= 1; txt++) {
		int px = (pxi == 0) ? -1 : (pxi == 1) ? 0 : 12;
		TiltGateIn in;
		in.userLevel = 2; in.screen = 0; in.ok = ok; in.ctx = ctx; in.sb1Valid = sb1;
		in.px = px; in.textDlg = txt; in.menuOpen = 0; in.wlOn = 0; in.netOn = 0;
		in.touchActive = 0; in.stereoEngaged = 0; in.isN3DS = 1; in.fsOn = 0; in.focScreen = 0;
		int want = field_state_ok(ok, ctx, sb1, px, txt);
		EQI(tilt_target_level(&in) != 0, want,
		    "tilt gate vs field_state_ok (ok=%d ctx=%d sb1=%d px=%d txt=%d)", ok, ctx, sb1, px, txt);

		// ...and presence's own P-G6 must agree on a RECORD built from the same inputs. The `ok`
		// half is not part of this comparison by design: for a record, "no profile" is P-G4
		// (gameId == PRES_GAME_NONE), not the field predicate. The producer sets PRES_F_FIELD from
		// field_state_ok itself, because the textDlg term has no separate slot in the 48 bytes —
		// so a wrong flag and a wrong predicate would have to disagree here to slip through.
		int wantRec = field_state_ok(1, ctx, sb1, px, txt);
		PeerPresence r = rec_ok(px, 10, 0, 0);
		r.ctx = (uint8_t)ctx;
		if (!sb1)     r.flags &= ~(unsigned)PRES_F_SB1VALID;
		if (!wantRec) r.flags &= ~(unsigned)PRES_F_FIELD;
		EQI(rec_field_ok(&r) != 0, wantRec,
		    "presence record field predicate (ctx=%d sb1=%d px=%d txt=%d)", ctx, sb1, px, txt);
	}
}

// ============================================================================================
// TEST 8/9/10 — staleness, two-tier presence, the wedge (SPEC-data D3.5, D3.6, D3.6.1)
// ============================================================================================
static void test_liveness(void) {
	printf("TEST 8: staleness ages a peer out after exactly PRES_STALE_FRAMES\n");
	{
		PresenceState ps; presence_reset(&ps);
		presence_begin_round(&ps, 100);
		PeerPresence p = rec_ok(12, 10, 0, 0); p.round = 100; p.hb = 100;
		presence_publish(&ps, 0, &p);
		EQI(presence_liveness(&ps, 0), PRES_LIVE_ACTIVE, "fresh -> ACTIVE");
		presence_begin_round(&ps, 100 + 179);
		EQI(presence_liveness(&ps, 0), PRES_LIVE_ACTIVE, "+179 frames -> still fresh");
		presence_begin_round(&ps, 100 + 180);
		EQI(presence_liveness(&ps, 0), PRES_LIVE_ACTIVE, "+180 frames -> still fresh (boundary)");
		presence_begin_round(&ps, 100 + 181);
		EQI(presence_liveness(&ps, 0), PRES_LIVE_NONE, "+181 frames -> NONE");
		EQI(presence_liveness(&ps, 1), PRES_LIVE_NONE, "out-of-range slot -> NONE, never a read");
		EQI(presence_liveness(&ps, -1), PRES_LIVE_NONE, "negative slot -> NONE");
	}

	printf("TEST 9: two-tier presence — connected is NOT active (pm-rom-abi §7.4)\n");
	{
		PresenceState ps; PresenceOut o;
		PeerPresence self = rec_ok(10, 10, 0, 0);
		PeerPresence peer = rec_ok(12, 10, 0, 0);
		peer.flags &= ~(unsigned)PRES_F_FIELD;      // the peer is in a menu / not in the field
		EQI(solve_once(&ps, &self, &peer, 1, 0, 0, &o), 0, "not game-active -> never draws");
		EQI(presence_liveness(&ps, 0), PRES_LIVE_CONNECTED, "fresh but not in the field -> CONNECTED");
		EQI(o.dTileX, 2, "...and the HUD readout still works while it does not draw");

		PeerPresence noSave = rec_ok(12, 10, 0, 0);
		noSave.flags &= ~(unsigned)PRES_F_SB1VALID;
		EQI(solve_once(&ps, &self, &noSave, 1, 0, 0, &o), 0, "no save loaded -> never draws");
		EQI(presence_liveness(&ps, 0), PRES_LIVE_CONNECTED, "no save -> CONNECTED at most");
	}

	printf("TEST 10: the wedge — a frozen heartbeat on a FRESH record (pm-rom-abi §3, §8)\n");
	{
		PresenceState ps; presence_reset(&ps);
		PeerPresence p = rec_ok(12, 10, 0, 0);
		for (uint32_t r = 1; r <= 5; r++) {         // healthy: heartbeat advancing
			presence_begin_round(&ps, r); p.round = r; p.hb = r; presence_publish(&ps, 0, &p);
		}
		EQI(presence_liveness(&ps, 0), PRES_LIVE_ACTIVE, "advancing heartbeat -> ACTIVE");
		EQI((long)presence_hb_stall(&ps, 0), 0, "no stall while it advances");
		uint32_t frozen = p.hb;                      // hbRound is now 5
		for (uint32_t r = 6; r <= 5 + PRES_WEDGE_FRAMES; r++) {   // record stays FRESH, hb frozen
			presence_begin_round(&ps, r); p.round = r; p.hb = frozen; presence_publish(&ps, 0, &p);
		}
		EQI((long)presence_hb_stall(&ps, 0), (long)PRES_WEDGE_FRAMES, "stall counts the frozen rounds exactly");
		EQI(presence_liveness(&ps, 0), PRES_LIVE_ACTIVE, "a stall of EXACTLY 180 is not yet a wedge");
		{ uint32_t r = 6 + PRES_WEDGE_FRAMES;                     // one more frozen round
		  presence_begin_round(&ps, r); p.round = r; p.hb = frozen; presence_publish(&ps, 0, &p); }
		EQI((long)presence_hb_stall(&ps, 0), (long)PRES_WEDGE_FRAMES + 1, "181 frozen rounds");
		EQI(presence_liveness(&ps, 0), PRES_LIVE_CONNECTED, "wedged -> CONNECTED, never ACTIVE");
	}
}

// ============================================================================================
// TEST 11/12 — the hold and the map change (SPEC-data D4.7)
// ============================================================================================
static void test_hold(void) {
	printf("TEST 11: the D4.7 hold — the PEER's blink is held 30 frames, the HOST's stops NOW\n");
	{
		Harness h; h_init(&h);
		PeerPresence self = rec_ok(10, 10, 0, 0), peer = rec_ok(12, 10, 0, 0);
		PresenceOut o;
		for (int i = 0; i < 3; i++) EQI(h_step(&h, self, peer, &o), 1, "warm-up draws");
		float lastX = o.footX, lastY = o.footY;

		PeerPresence blink = peer; blink.flags &= ~(unsigned)PRES_F_FIELD;
		for (int i = 1; i <= (int)PRES_HOLD_FRAMES; i++) {
			EQI(h_step(&h, self, blink, &o), 1, "held frame %d still draws", i);
			EQI(o.held, 1, "held frame %d is flagged as a hold", i);
			EQF(o.footX, lastX, "held anchor does not move (x), frame %d", i);
			EQF(o.footY, lastY, "held anchor does not move (y), frame %d", i);
			EQI(o.reason, PRES_OFF_FIELD, "held frame reports WHY it is held");
		}
		EQI(h_step(&h, self, blink, &o), 0, "frame 31 gives up");
		EQI(o.held, 0, "expired hold is not a hold");
		EQI(o.reason, PRES_OFF_FIELD, "expired hold still reports the rule that closed the gate");
	}
	{   // the object-agreement blink is holdable too (P-G7 peer side)
		Harness h; h_init(&h);
		PeerPresence self = rec_ok(10, 10, 0, 0), peer = rec_ok(12, 10, 0, 0);
		PresenceOut o;
		EQI(h_step(&h, self, peer, &o), 1, "warm-up draws");
		PeerPresence bad = peer; bad.objX = 99;
		EQI(h_step(&h, self, bad, &o), 1, "peer object disagreement is held");
		EQI(o.held, 1, "...and flagged");
		EQI(o.reason, PRES_OFF_OBJ, "...with the OBJ reason");
	}
	{   // the HOST's own gate failing stops immediately — no hold, ever (D4.7.3)
		Harness h; h_init(&h);
		PeerPresence self = rec_ok(10, 10, 0, 0), peer = rec_ok(12, 10, 0, 0);
		PresenceOut o;
		for (int i = 0; i < 3; i++) EQI(h_step(&h, self, peer, &o), 1, "warm-up draws");
		PeerPresence menu = self; menu.flags &= ~(unsigned)PRES_F_FIELD;
		EQI(h_step(&h, menu, peer, &o), 0, "self leaves the field -> stop IMMEDIATELY");
		EQI(o.held, 0, "self failure never holds");
		EQI(o.reason, PRES_OFF_SELF, "self failure reason");
		// ...and the hold state is gone, so recovery snaps to truth rather than to a stale anchor
		EQI(h.ps.holdHave[0], 0, "self failure clears the stored hold");
		EQI(h.ps.haveSm[0], 0, "self failure clears the smoothing filter");
	}
	{   // a different map is NOT holdable (nothing to hold: the tile delta is meaningless)
		Harness h; h_init(&h);
		PeerPresence self = rec_ok(10, 10, 0, 0), peer = rec_ok(12, 10, 0, 0);
		PresenceOut o;
		EQI(h_step(&h, self, peer, &o), 1, "warm-up draws");
		PeerPresence away = peer; away.mapNum = MAPN + 1;
		EQI(h_step(&h, self, away, &o), 0, "peer left the map -> stop immediately");
		EQI(o.reason, PRES_OFF_MAP, "map reason");
		EQI(h.ps.holdHave[0], 0, "map change cleared the hold");
	}

	printf("TEST 12: a map change clears the filter and the hold — no interpolation across maps\n");
	{
		Harness h; h_init(&h);
		PeerPresence self = rec_ok(10, 10, 0, 0), peer = rec_ok(12, 10, 0, 0);
		PresenceOut o;
		EQI(h_step(&h, self, peer, &o), 1, "warm-up draws");
		CHECK(h.ps.haveSm[0] == 1 && h.ps.holdHave[0] == 1, "filter + hold are primed");
		PeerPresence moved = peer; moved.mapGroup = MAPG + 1; moved.px = 40; moved.py = 40;
		moved.objX = 47; moved.objY = 47;
		presence_begin_round(&h.ps, h.round); moved.round = h.round; moved.hb = h.round;
		presence_publish(&h.ps, 0, &moved);
		EQI(h.ps.haveSm[0], 0, "map change cleared the smoothing filter");
		EQI(h.ps.holdHave[0], 0, "map change cleared the hold");
		EQI(h.ps.snapNext[0], 1, "map change armed the snap");
		EQI((long)h.ps.mapChgN[0], 1, "map change counted (diagnostics)");
		EQI((long)h.ps.teleN[0], 0, "a map change is NOT counted as a teleport");
	}
}

// ============================================================================================
// TEST 13 — newest-wins (SPEC-data D3.4.2)
// ============================================================================================
static void test_newest_wins(void) {
	printf("TEST 13: presence_publish drops anything not strictly newer\n");
	PresenceState ps; presence_reset(&ps);
	presence_begin_round(&ps, 1);
	PeerPresence a = rec_ok(12, 10, 0, 0); a.round = 5; a.hb = 5;
	presence_publish(&ps, 0, &a);
	EQI(ps.rec[0].px, 12, "first record lands");
	EQI((long)ps.dropN[0], 0, "nothing dropped yet");

	PeerPresence older = rec_ok(99, 99, 0, 0); older.round = 4;
	presence_publish(&ps, 0, &older);
	EQI(ps.rec[0].px, 12, "an OLDER round is dropped");
	EQI((long)ps.dropN[0], 1, "...and counted");

	PeerPresence same = rec_ok(88, 88, 0, 0); same.round = 5;
	presence_publish(&ps, 0, &same);
	EQI(ps.rec[0].px, 12, "an EQUAL round is dropped");
	EQI((long)ps.dropN[0], 2, "...and counted");

	PeerPresence newer = rec_ok(13, 10, 0, 0); newer.round = 6; newer.hb = 6;
	presence_publish(&ps, 0, &newer);
	EQI(ps.rec[0].px, 13, "a NEWER round is accepted");
	EQI((long)ps.dropN[0], 2, "...and not counted as a drop");

	// Out-of-range slots and NULLs are inert, never a write past the array.
	presence_publish(&ps, PRES_MAX_PEERS, &newer);
	presence_publish(&ps, -1, &newer);
	presence_publish(&ps, 0, NULL);
	presence_publish(NULL, 0, &newer);
	EQI(ps.rec[0].px, 13, "bad publishes changed nothing");
}

// ============================================================================================
// TEST 14 — the low-rate filter (SPEC-data D5.5) — the M4 hinge
// ============================================================================================
static void test_smoothing(void) {
	printf("TEST 14: age==0 is bit-exact; age>0 smooths; a big jump snaps\n");
	{   // same-console: the filter is a NO-OP and the anchor is bit-exact the D5.3 target
		Harness h; h_init(&h);
		PeerPresence self = rec_ok(10, 10, 3, 0), peer = rec_ok(14, 12, 9, 5);
		PresenceOut o;
		for (int i = 0; i < 8; i++) EQI(h_step(&h, self, peer, &o), 1, "same-console draws");
		float wantX = PRES_ANCHOR_X + 16.0f * 4.0f + (float)(presence_sub(3) - presence_sub(9));
		float wantY = PRES_ANCHOR_Y + 16.0f * 2.0f + (float)(presence_sub(0) - presence_sub(5));
		CHECK(o.footX == wantX, "age==0 footX is BIT-exact (%f vs %f)", (double)o.footX, (double)wantX);
		CHECK(o.footY == wantY, "age==0 footY is BIT-exact (%f vs %f)", (double)o.footY, (double)wantY);
	}
	{   // low-rate: publish once, then let the record age while the HOST keeps moving
		Harness h; h_init(&h);
		PeerPresence self = rec_ok(10, 10, 0, 0), peer = rec_ok(11, 10, 0, 0);
		PresenceOut o;
		EQI(h_step(&h, self, peer, &o), 1, "first frame draws");
		EQF(o.footX, PRES_ANCHOR_X + 16.0f, "first frame is exact");
		float sm = o.footX;
		PeerPresence self2 = rec_ok(10, 11, 0, 0);       // host stepped down: target moves -16 in y
		float target = PRES_ANCHOR_X + 16.0f;            // x target unchanged
		(void)target;
		float smY = o.footY, targetY = PRES_ANCHOR_Y - 16.0f;
		for (int i = 0; i < 6; i++) {
			EQI(h_step_nopub(&h, self2, &o), 1, "aged record still draws");
			smY += (targetY - smY) * PRES_SMOOTH_A;
			EQF(o.footY, smY, "single-pole convergence, step %d", i);
			EQF(o.footX, sm, "the unmoved axis does not drift");
		}
		CHECK(o.footY - targetY < 2.0f && o.footY - targetY > -2.0f,
		      "~90%% closed within 6 frames (got %.3f, want %.3f)", (double)o.footY, (double)targetY);
	}
	{   // a jump beyond PRES_SNAP_PX teleports instead of gliding
		Harness h; h_init(&h);
		PeerPresence self = rec_ok(10, 10, 0, 0), peer = rec_ok(11, 10, 0, 0);
		PresenceOut o;
		EQI(h_step(&h, self, peer, &o), 1, "first frame draws");
		PeerPresence self2 = rec_ok(8, 10, 0, 0);        // host warped 2 tiles = 32 px > 24
		EQI(h_step_nopub(&h, self2, &o), 1, "aged record still draws");
		EQF(o.footX, PRES_ANCHOR_X + 48.0f, "a >24 px jump SNAPS, it does not glide");
	}
}

// ============================================================================================
// TEST 15 — the teleport guard (SPEC-data D5.6)
// ============================================================================================
static void test_teleport(void) {
	printf("TEST 15: a >8-tile jump on ONE map snaps the filter and is counted\n");
	Harness h; h_init(&h);
	PeerPresence self = rec_ok(10, 10, 0, 0), peer = rec_ok(12, 10, 0, 0);
	PresenceOut o;
	EQI(h_step(&h, self, peer, &o), 1, "warm-up draws");
	EQI((long)h.ps.teleN[0], 0, "no teleport yet");

	PeerPresence near = rec_ok(12 + 8, 10, 0, 0);        // exactly 8 tiles: NOT a teleport
	presence_begin_round(&h.ps, h.round); near.round = h.round; near.hb = h.round;
	presence_publish(&h.ps, 0, &near); h.round++;
	EQI((long)h.ps.teleN[0], 0, "|dpx|+|dpy| == 8 is a legitimately fast peer");
	EQI(h.ps.snapNext[0], 0, "...and does not arm the snap");

	PeerPresence far = rec_ok(12 + 8 + 9, 10, 0, 0);     // 9 tiles further: a teleport
	presence_begin_round(&h.ps, h.round); far.round = h.round; far.hb = h.round;
	presence_publish(&h.ps, 0, &far); h.round++;
	EQI((long)h.ps.teleN[0], 1, "|dpx|+|dpy| > 8 is a teleport");
	EQI(h.ps.snapNext[0], 1, "...and arms the snap");
	// It NEVER suppresses the draw — a legitimately fast peer must not vanish.
	presence_begin_round(&h.ps, h.round);
	PeerPresence back = rec_ok(13, 10, 0, 0);
	back.round = h.round; back.hb = h.round;
	presence_publish(&h.ps, 0, &back);
	PresenceIn in; memset(&in, 0, sizeof in); in.enabled = 1; in.slot = 0; in.self = self;
	EQI(presence_solve(&h.ps, &in, &o), 1, "a teleported peer still draws");
	EQI((long)h.ps.teleN[0], 2, "the way back counts too");
}

// ============================================================================================
// TEST 16 — culling (SPEC-data D5.7)
// ============================================================================================
static void test_cull(void) {
	printf("TEST 16: the tile early-out never culls something the rect test would keep\n");
	// Stage 1 (tile early-out) must be a strict SUBSET of stage 2 (the rect test), for every
	// reachable sub-tile phase — otherwise the cheap test would hide a visible avatar.
	for (int dx = -14; dx <= 14; dx++)
	for (int dy = -14; dy <= 14; dy++)
	for (int sub = -15; sub <= 15; sub += 15) {
		float fx = PRES_ANCHOR_X + 16.0f * (float)dx + (float)sub;
		float fy = PRES_ANCHOR_Y + 16.0f * (float)dy + (float)sub;
		int tileOut = (dx > 9 || dx < -9 || dy > 8 || dy < -8);
		float x0 = fx - 8.0f, y0 = fy - 32.0f;
		// CLOSED edge tests (fix pass, review finding 5) — the shipped rule, mirrored here.
		int rectHit = !(x0 + 16.0f <= -PRES_CULL_M || x0 >= (float)PRES_FRAME_W + PRES_CULL_M ||
		                y0 + 32.0f <= -PRES_CULL_M || y0 >= (float)PRES_FRAME_H + PRES_CULL_M);
		if (tileOut) CHECK(!rectHit, "tile early-out at (%d,%d,sub %d) agrees with the rect", dx, dy, sub);
		EQI(on_screen(dx, dy, fx, fy), tileOut ? 0 : rectHit,
		    "on_screen is exactly (tile early-out AND rect) at (%d,%d,sub %d)", dx, dy, sub);
	}
	// End to end: a peer far off the map edge does not draw, and says CULL rather than pretending
	// a gate closed.
	PresenceState ps; PresenceOut o;
	PeerPresence self = rec_ok(10, 10, 0, 0);
	{ PeerPresence peer = rec_ok(10 + 20, 10, 0, 0);
	  EQI(solve_once(&ps, &self, &peer, 1, 0, 0, &o), 0, "peer 20 tiles right -> not drawn");
	  EQI(o.reason, PRES_OFF_CULL, "-> CULL, and the tile delta is still reported");
	  EQI(o.dTileX, 20, "dTileX survives the cull for the HUD"); }
	{ PeerPresence peer = rec_ok(10 + 7, 10 + 4, 0, 0);
	  EQI(solve_once(&ps, &self, &peer, 1, 0, 0, &o), 1, "the far corner of the visible grid draws"); }
	{ PeerPresence peer = rec_ok(10 - 7, 10 - 5, 0, 0);
	  EQI(solve_once(&ps, &self, &peer, 1, 0, 0, &o), 1, "the near corner of the visible grid draws"); }
	{ PeerPresence peer = rec_ok(10 + 7, 10, 0, 0);      // right edge, still partially visible
	  EQI(solve_once(&ps, &self, &peer, 1, 0, 0, &o), 1, "a partially visible avatar is kept (clipped by the renderer)"); }
	// FIX PASS (review finding 5): the two whole-tile positions the 8 px margin used to LIE about.
	// Both put the art rect exactly on the frame boundary, where presence_art_clip renders nothing.
	{ PeerPresence peer = rec_ok(10 + 8, 10, 0, 0);      // art rect x = 240 exactly
	  EQI(solve_once(&ps, &self, &peer, 1, 0, 0, &o), 0, "dTileX = +8 is CULLED, not reported as drawn");
	  EQI(o.reason, PRES_OFF_CULL, "...and says so"); }
	{ PeerPresence peer = rec_ok(10, 10 - 6, 0, 0);      // art rect y1 = -8 exactly
	  EQI(solve_once(&ps, &self, &peer, 1, 0, 0, &o), 0, "dTileY = -6 is CULLED, not reported as drawn");
	  EQI(o.reason, PRES_OFF_CULL, "...and says so"); }
}

// ============================================================================================
// TEST 16b — the data half's cull and the render half's clip must agree (fix pass, finding 5)
// ============================================================================================
// `draw` is what the CO-OP chip colours, what the M1 readout prints as `ok`, and what gates the
// walk accumulator. It therefore has to MEAN "an avatar will be on the screen". It did not: the
// D5.7 rect test carried an 8 px margin that presence_art_clip does not, so there was a band where
// presence_solve said draw = 1 and the renderer drew nothing — and the band landed on whole-tile
// positions (dTileX = +-8, dTileY = -6), i.e. on ordinary frames, inverting A7.2 H1's triage rule.
//
// This test is the standing proof that the two functions agree. It sweeps every INTEGER foot anchor
// across the frame and well past every edge and asserts on_screen == (presence_art_clip != 0). The
// residual it deliberately does not chase: at FRACTIONAL anchors the clip rounds source columns
// OUTWARD (a 0.5 px overhang is dropped), which on_screen cannot mirror without learning about
// source columns — a <= 1 px disagreement, checked separately below as a bound rather than as an
// equality.
static void test_cull_matches_clip(void) {
	printf("TEST 16b: the D5.7 cull and the A2.4 clip agree on every integer anchor\n");
	int mismatch = 0, subPixel = 0;
	for (int fy = -60; fy <= 220; fy++) {
		for (int fx = -40; fx <= 280; fx++) {
			int dTileX = (fx - (int)PRES_ANCHOR_X) / 16;    // inside the stage-1 early-out for the
			int dTileY = (fy - (int)PRES_ANCHOR_Y) / 16;    //   whole sweep, so stage 2 is under test
			if (dTileX > 9 || dTileX < -9 || dTileY > 8 || dTileY < -8) continue;
			float sprX, sprY; presence_art_rect((float)fx, (float)fy, &sprX, &sprY);
			PresArtDraw d;
			int drawn = presence_art_clip(sprX, sprY, 0, 0.0f, &d) ? 1 : 0;
			int kept  = on_screen(dTileX, dTileY, (float)fx, (float)fy) ? 1 : 0;
			if (drawn != kept) mismatch++;
		}
	}
	EQI(mismatch, 0, "every integer anchor: on_screen == (the clip renders something)");
	for (int i = 0; i < 64; i++) {
		float fx = -40.0f + (float)i * 5.0f + 0.5f;
		float sprX, sprY; presence_art_rect(fx, 88.0f, &sprX, &sprY);
		PresArtDraw d;
		int drawn = presence_art_clip(sprX, sprY, 0, 0.0f, &d) ? 1 : 0;
		int kept  = on_screen(0, 0, fx, 88.0f) ? 1 : 0;
		if (drawn != kept) subPixel++;
	}
	CHECK(subPixel <= 2, "the fractional-anchor residual is the round-outward band only (got %d)\n",
	      subPixel);
}

// ============================================================================================
// TEST 17 — the charmap decoder (SPEC-data D6.3.1)
// ============================================================================================
static void test_name(void) {
	printf("TEST 17: presence_name_ascii — every documented range, and no overrun\n");
	char out[9];
	{   // The in-repo witness: Celio-Link's canned demo trainer name (celiolink_payloads.h:98-100)
		const uint8_t nils[8] = { 0xC8, 0xDD, 0xE0, 0xE7, 0xFF, 0, 0, 0 };
		EQI(presence_name_ascii(nils, out), 4, "'Nils' is 4 characters");
		CHECK(strcmp(out, "Nils") == 0, "the in-repo Celio anchor round-trips (got '%s')", out);
	}
	{   // A-Z and a-z, both full ranges
		for (int i = 0; i < 26; i++) {
			uint8_t up[8], lo[8];
			memset(up, 0xFF, 8); memset(lo, 0xFF, 8);
			up[0] = (uint8_t)(0xBB + i); lo[0] = (uint8_t)(0xD5 + i);
			presence_name_ascii(up, out); EQI(out[0], 'A' + i, "upper %d", i);
			presence_name_ascii(lo, out); EQI(out[0], 'a' + i, "lower %d", i);
		}
	}
	{   // digits and punctuation
		const uint8_t digits[8] = { 0xA1, 0xA2, 0xA5, 0xAA, 0xFF, 0, 0, 0 };
		EQI(presence_name_ascii(digits, out), 4, "4 digits");
		CHECK(strcmp(out, "0149") == 0, "digits decode (got '%s')", out);
		const uint8_t punct[8] = { 0xAB, 0xAC, 0xAD, 0xAE, 0xB8, 0xBA, 0x00, 0xFF };
		EQI(presence_name_ascii(punct, out), 7, "7 punctuation glyphs");
		CHECK(strcmp(out, "!?.-,/ ") == 0, "punctuation decodes (got '%s')", out);
	}
	{   // an UNTERMINATED 8-byte name still terminates the output within 9 bytes
		uint8_t full[8]; for (int i = 0; i < 8; i++) full[i] = (uint8_t)(0xBB + i);
		char buf[16]; memset(buf, 0x7E, sizeof buf);
		EQI(presence_name_ascii(full, buf), 8, "an 8-char name writes 8 characters");
		EQI(buf[8], 0, "...and the NUL lands at [8]");
		EQI((unsigned char)buf[9], 0x7E, "...and NOTHING is written past it");
		CHECK(strcmp(buf, "ABCDEFGH") == 0, "full-length name decodes (got '%s')", buf);
	}
	{   // an EMPTY name, and a NULL guard
		uint8_t empty[8]; memset(empty, 0xFF, 8);
		EQI(presence_name_ascii(empty, out), 0, "empty name");
		EQI(out[0], 0, "empty name is still NUL-terminated");
		EQI(presence_name_ascii(NULL, out), 0, "NULL source is inert");
		EQI(out[0], 0, "NULL source still terminates the output");
	}
	{   // EVERY one of the 256 byte values: decodes to something, never reads/writes out of range
		for (int b = 0; b < 256; b++) {
			uint8_t one[8]; memset(one, 0xFF, 8); one[0] = (uint8_t)b;
			char buf[16]; memset(buf, 0x7E, sizeof buf);
			int n = presence_name_ascii(one, buf);
			CHECK(n == (b == 0xFF ? 0 : 1), "byte 0x%02X writes %d char(s)", b, n);
			EQI(buf[n], 0, "byte 0x%02X terminates", b);
			EQI((unsigned char)buf[n + 1], 0x7E, "byte 0x%02X writes nothing past the NUL", b);
			if (n) CHECK(buf[0] >= ' ' && buf[0] <= '~', "byte 0x%02X decodes to printable ASCII", b);
		}
		// The unmapped blocks are '?' — cosmetic, and explicitly NOT an out-of-range read.
		uint8_t odd[8]; memset(odd, 0xFF, 8);
		odd[0] = 0xB5; presence_name_ascii(odd, out); EQI(out[0], '?', "gender sign -> '?'");
		odd[0] = 0x01; presence_name_ascii(odd, out); EQI(out[0], '?', "accented block -> '?'");
		odd[0] = 0xEF; presence_name_ascii(odd, out); EQI(out[0], '?', "just past 'z' -> '?'");
		odd[0] = 0xBA; presence_name_ascii(odd, out); EQI(out[0], '/', "0xBA is '/'");
		odd[0] = 0xBB; presence_name_ascii(odd, out); EQI(out[0], 'A', "0xBB is 'A'");
		odd[0] = 0xD4; presence_name_ascii(odd, out); EQI(out[0], 'Z', "0xD4 is 'Z'");
		odd[0] = 0xD5; presence_name_ascii(odd, out); EQI(out[0], 'a', "0xD5 is 'a'");
		odd[0] = 0xEE; presence_name_ascii(odd, out); EQI(out[0], 'z', "0xEE is 'z'");
	}
}

// ============================================================================================
// TEST 18 — presence_dir (SPEC-data D2.4)
// ============================================================================================
static void test_dir(void) {
	printf("TEST 18: presence_dir folds the diagonals and clamps everything else\n");
	EQI(presence_dir(PRES_DIR_SOUTH), PRES_DIR_SOUTH, "1 = DOWN");
	EQI(presence_dir(PRES_DIR_NORTH), PRES_DIR_NORTH, "2 = UP");
	EQI(presence_dir(PRES_DIR_WEST),  PRES_DIR_WEST,  "3 = LEFT");
	EQI(presence_dir(PRES_DIR_EAST),  PRES_DIR_EAST,  "4 = RIGHT");
	EQI(presence_dir(0), PRES_DIR_SOUTH, "DIR_NONE degrades to a plausible default");
	EQI(presence_dir(5), PRES_DIR_SOUTH, "SOUTHWEST folds to SOUTH");
	EQI(presence_dir(6), PRES_DIR_SOUTH, "SOUTHEAST folds to SOUTH");
	EQI(presence_dir(7), PRES_DIR_NORTH, "NORTHWEST folds to NORTH");
	EQI(presence_dir(8), PRES_DIR_NORTH, "NORTHEAST folds to NORTH");
	// Every possible input — including a whole byte, which is what a torn/garbage read looks like —
	// must land in 1..4 so the art index can NEVER be out of range.
	for (int f = -300; f <= 300; f++) {
		int d = presence_dir(f);
		CHECK(d >= PRES_DIR_SOUTH && d <= PRES_DIR_EAST, "presence_dir(%d) = %d is in range", f, d);
	}
	for (int f = 9; f <= 15; f++) EQI(presence_dir(f), PRES_DIR_SOUTH, "nibble %d clamps to SOUTH", f);
}

// ============================================================================================
// TEST 19 — the 48-byte wire record (SPEC-data D3.2)
// ============================================================================================
static void test_record(void) {
	printf("TEST 19: PeerPresence is the 48-byte, fixed-offset, PM-shaped wire record\n");
	EQI(sizeof(PeerPresence), 48, "sizeof");
	EQI(PRES_REC_BYTES, 48, "PRES_REC_BYTES");
	EQI(offsetof(PeerPresence, ver),         0x00, "+0x00 ver");
	EQI(offsetof(PeerPresence, seat),        0x01, "+0x01 seat");
	EQI(offsetof(PeerPresence, flags),       0x02, "+0x02 flags");
	EQI(offsetof(PeerPresence, ctx),         0x03, "+0x03 ctx");
	EQI(offsetof(PeerPresence, px),          0x04, "+0x04 px   (PM ow +0x04 x)");
	EQI(offsetof(PeerPresence, py),          0x06, "+0x06 py   (PM ow +0x06 z)");
	EQI(offsetof(PeerPresence, objX),        0x08, "+0x08 objX");
	EQI(offsetof(PeerPresence, objY),        0x0A, "+0x0A objY");
	EQI(offsetof(PeerPresence, hb),          0x0C, "+0x0C hb   (PM ow +0x0C frameCounter)");
	EQI(offsetof(PeerPresence, subX),        0x10, "+0x10 subX");
	EQI(offsetof(PeerPresence, subY),        0x11, "+0x11 subY");
	EQI(offsetof(PeerPresence, facing),      0x12, "+0x12 facing");
	EQI(offsetof(PeerPresence, gender),      0x13, "+0x13 gender");
	EQI(offsetof(PeerPresence, avatarFlags), 0x14, "+0x14 avatarFlags (reserved, ships 0)");
	EQI(offsetof(PeerPresence, mapGroup),    0x15, "+0x15 mapGroup");
	EQI(offsetof(PeerPresence, mapNum),      0x16, "+0x16 mapNum");
	EQI(offsetof(PeerPresence, gameId),      0x17, "+0x17 gameId");
	EQI(offsetof(PeerPresence, tid),         0x18, "+0x18 tid");
	EQI(offsetof(PeerPresence, mapLayoutId), 0x1A, "+0x1A mapLayoutId");
	EQI(offsetof(PeerPresence, round),       0x1C, "+0x1C round");
	EQI(offsetof(PeerPresence, name),        0x20, "+0x20 name[8]");
	EQI(offsetof(PeerPresence, rsv),         0x28, "+0x28 rsv[8]");
	// Every multi-byte field is naturally aligned, so M4 can memcpy the struct into a beacon with
	// no repacking and no byte-swap layer (both ends are ARM LE).
	CHECK(offsetof(PeerPresence, px)    % 2 == 0, "px is 2-aligned");
	CHECK(offsetof(PeerPresence, hb)    % 4 == 0, "hb is 4-aligned");
	CHECK(offsetof(PeerPresence, tid)   % 2 == 0, "tid is 2-aligned");
	CHECK(offsetof(PeerPresence, round) % 4 == 0, "round is 4-aligned");

	PeerPresence r; presence_rec_init(&r, 1, PRES_GAME_KANTO);
	EQI(r.ver, PRES_REC_VER, "rec_init stamps the version");
	EQI(r.seat, 1, "rec_init stamps the seat");
	EQI(r.gameId, PRES_GAME_KANTO, "rec_init stamps the map universe");
	EQI(r.objX, -1, "rec_init leaves the object gate UNAVAILABLE, not passing");
	EQI(r.objY, -1, "rec_init objY sentinel");
	EQI(r.flags, 0, "rec_init claims nothing");
	EQI(r.avatarFlags, 0, "avatarFlags ships 0 (D1.6 — gPlayerAvatar is not in the profile)");
	for (int i = 0; i < 8; i++) EQI(r.rsv[i], 0, "rsv[%d] is zeroed for M4 growth", i);
	char nm[9]; EQI(presence_name_ascii(r.name, nm), 0, "rec_init's name is empty, not garbage");
}

// ============================================================================================
// TEST 20 — purity + reset (SPEC-data D6.2)
// ============================================================================================
static void test_purity(void) {
	printf("TEST 20: presence_solve never mutates its input; presence_reset fully clears state\n");
	{
		PresenceState ps; presence_reset(&ps);
		presence_begin_round(&ps, 1);
		PeerPresence peer = rec_ok(12, 10, 0, 0); peer.round = 1;
		presence_publish(&ps, 0, &peer);
		PresenceIn in; memset(&in, 0, sizeof in);
		in.enabled = 1; in.slot = 0; in.self = rec_ok(10, 10, 0, 0);
		PresenceIn before = in;
		PresenceOut o;
		presence_solve(&ps, &in, &o);
		CHECK(memcmp(&before, &in, sizeof in) == 0, "presence_solve did not touch PresenceIn");
		// ...on the off path too
		in.enabled = 0; before = in;
		presence_solve(&ps, &in, &o);
		CHECK(memcmp(&before, &in, sizeof in) == 0, "presence_solve did not touch PresenceIn (gate closed)");
	}
	{
		PresenceState dirty, zero;
		memset(&dirty, 0xAA, sizeof dirty);
		memset(&zero,  0x00, sizeof zero);
		presence_reset(&dirty);
		CHECK(memcmp(&dirty, &zero, sizeof zero) == 0, "presence_reset zeroes every byte of the state");
	}
	{   // NULL / out-of-range guards: inert, and never a draw
		PresenceOut o;
		PresenceState ps; presence_reset(&ps);
		PresenceIn in; memset(&in, 0, sizeof in); in.enabled = 1; in.slot = 0;
		EQI(presence_solve(NULL, &in, &o), 0, "NULL state -> no draw");
		EQI(o.draw, 0, "...and out says so");
		EQI(presence_solve(&ps, NULL, &o), 0, "NULL input -> no draw");
		in.slot = PRES_MAX_PEERS;
		EQI(presence_solve(&ps, &in, &o), 0, "out-of-range slot -> no draw");
		in.slot = -1;
		EQI(presence_solve(&ps, &in, &o), 0, "negative slot -> no draw");
		presence_reset(NULL); presence_begin_round(NULL, 1);   // must not crash
		EQI((long)presence_hb_stall(NULL, 0), 0, "NULL hb_stall");
		EQI(presence_liveness(NULL, 0), PRES_LIVE_NONE, "NULL liveness");
		EQI(presence_same_map(NULL, NULL), 0, "NULL same_map");
		EQI(presence_obj_agree(NULL), -1, "NULL obj_agree is UNAVAILABLE, not a pass");
		presence_rec_init(NULL, 0, 0);
	}
	{   // solve must be idempotent within a round when nothing changes (no hidden per-call drift)
		Harness h; h_init(&h);
		PeerPresence self = rec_ok(10, 10, 0, 0), peer = rec_ok(12, 10, 0, 0);
		PresenceOut a, b;
		EQI(h_step(&h, self, peer, &a), 1, "draws");
		PresenceIn in; memset(&in, 0, sizeof in);
		in.enabled = 1; in.slot = 0; in.self = self;
		presence_solve(&h.ps, &in, &b);
		EQF(a.footX, b.footX, "re-solving the same round gives the same anchor (x)");
		EQF(a.footY, b.footY, "re-solving the same round gives the same anchor (y)");
	}
}

// ============================================================================================
// SLICE M1 — THE PRODUCER (SPEC-data D2.2/D2.3/D3.2): TEST 21, 22, 23, 24.
//
// M0 built the record, the gate and the anchor and tested them against HAND-BUILT records. M1 adds
// the other end: presence_fill_core, which is what actually turns a game's live state into one of
// those records, and presence_ident_due, the D2.3 caching policy. Splitting the producer into a
// pure core plus a thin RAM reader (presence_read.c) is what makes this testable at all — the
// alternative SPEC-data D6.4 allows (all of it inline in main.c) would have put every one of these
// decisions in the one file the PC harness can never compile.
//
// NUMBERING: SPEC-data D6.5 reserves TEST 1-20 for the data half and TEST 21+ for SPEC-avatar's
// render suite. These four are data-half tests that arrived after 1-20 were spent, so they take
// 21-24 and SPEC-avatar's render tests start at 25 — recorded here and in BUILDLOG so the render
// slice does not collide.
//
//   TEST 21 presence_ident_due — the D2.3 refresh cadence, exhaustively
//   TEST 22 presence_fill_core — the flag/field assembly from raw game state
//   TEST 23 THE GATE TRUTH TABLE, driven end-to-end from RAW SOURCE VALUES
//   TEST 24 the charmap goldens through the producer (the in-repo "Nils" anchor)
// ============================================================================================

// A healthy PresenceSrc: the exact shape presence_read.c hands over for a game that is loaded, in
// the free-roam overworld, with every profile field mapped. Tests break one field at a time.
static const uint8_t NAME_NILS[8] = { 0xC8, 0xDD, 0xE0, 0xE7, 0xFF, 0xFF, 0xFF, 0xFF };

static PresenceSrc src_ok(int px, int py) {
	PresenceSrc s;
	memset(&s, 0, sizeof s);
	s.ok = 1;
	s.ctx = FIELD_CTX_OVERWORLD;
	s.sb1Valid = 1;
	s.textDlg = 0;
	s.px = px; s.py = py;
	s.mapGroup = MAPG; s.mapNum = MAPN;
	s.objX = px + PRES_MAP_OFFSET; s.objY = py + PRES_MAP_OFFSET;
	s.facing = PRES_DIR_NORTH;
	s.camOk = 1; s.camX = 0; s.camY = 0;
	s.hbOk = 1; s.hb = 7;
	s.identOk = 1; s.name = NAME_NILS; s.gender = 1; s.tid = 1234;
	s.round = 1;
	return s;
}

// TEST 21 — the identity cadence (SPEC-data D2.3)
static void test_ident_due(void) {
	printf("TEST 21: identity is CACHED, not polled — the D2.3 refresh cadence\n");

	// A map change ALWAYS re-latches, at any age, latched or not (D4.7.1 requires it: a warp is the
	// one same-console event that can mean a different save is loaded).
	for (int have = 0; have <= 1; have++)
		for (unsigned age = 0; age <= 601; age += 100)
			EQI(presence_ident_due(have, 1, age), 1, "map change is always due (have=%d age=%u)", have, age);

	// Nothing latched yet: retry on the SHORT cadence, so a save that is not loaded at boot does not
	// cost a 10-second blank nameplate.
	for (unsigned age = 0; age < PRES_IDENT_RETRY_FRAMES; age++)
		EQI(presence_ident_due(0, 0, age), 0, "no identity, age %u < retry: not yet", age);
	EQI(presence_ident_due(0, 0, PRES_IDENT_RETRY_FRAMES), 1, "no identity, exactly at the retry cadence: due");
	EQI(presence_ident_due(0, 0, PRES_IDENT_RETRY_FRAMES + 1), 1, "...and after it");
	EQI(presence_ident_due(0, 0, 0xFFFFFFFFu), 1, "\"never tried\" (the reader's sentinel age) is due");

	// Latched: the LONG cadence, which is gamestate.c's own GS_HEARTBEAT_FRAMES.
	EQI((long)PRES_IDENT_REFRESH_FRAMES, 600, "the refresh cadence is the gs-log heartbeat cadence");
	for (unsigned age = 0; age < PRES_IDENT_REFRESH_FRAMES; age += 37)
		EQI(presence_ident_due(1, 0, age), 0, "latched, age %u < refresh: cached", age);
	EQI(presence_ident_due(1, 0, PRES_IDENT_REFRESH_FRAMES - 1), 0, "latched, one frame short: still cached");
	EQI(presence_ident_due(1, 0, PRES_IDENT_REFRESH_FRAMES), 1, "latched, exactly at the refresh cadence: due");
	// The whole point: over a 10-second window a latched identity is read ONCE, not 600 times.
	{
		int reads = 0;
		unsigned last = 0;
		for (unsigned f = 1; f <= 600; f++)
			if (presence_ident_due(1, 0, f - last)) { reads++; last = f; }
		EQI(reads, 1, "600 frames with a latched identity cost exactly one re-read");
	}
}

// TEST 22 — presence_fill_core (SPEC-data D2.2/D3.2/D1.9)
static void test_fill_core(void) {
	printf("TEST 22: presence_fill_core turns raw game state into a well-formed record\n");

	{   // the healthy case: every flag set, every field carried across
		PresenceSrc s = src_ok(10, 20);
		s.round = 99;
		PeerPresence r;
		presence_fill_core(&r, 1, PRES_GAME_KANTO, &s);
		EQI(r.ver, PRES_REC_VER, "ver is stamped");
		EQI(r.seat, 1, "seat is the producer id");
		EQI(r.gameId, PRES_GAME_KANTO, "gameId is the MAP UNIVERSE, not the raw code");
		EQI(r.round, 99, "the producer's round is carried");
		EQI(r.px, 10, "px"); EQI(r.py, 20, "py");
		EQI(r.mapGroup, MAPG, "mapGroup"); EQI(r.mapNum, MAPN, "mapNum");
		EQI(r.objX, 10 + PRES_MAP_OFFSET, "objX carries the MAP_OFFSET bias as read");
		EQI(r.objY, 20 + PRES_MAP_OFFSET, "objY");
		EQI(r.facing, PRES_DIR_NORTH, "facing");
		EQI(r.gender, 1, "gender");
		EQI(r.tid, 1234, "tid");
		EQI(r.hb, 7, "heartbeat");
		EQI(r.avatarFlags, 0, "avatarFlags is RESERVED and ships 0 (D1.6)");
		EQI(r.flags, PRES_F_SB1VALID | PRES_F_FIELD | PRES_F_OBJOK | PRES_F_IDENT | PRES_F_CAM | PRES_F_HB,
		    "every flag is set on a healthy game");
		EQI(memcmp(r.name, NAME_NILS, 8), 0, "the name crosses RAW (D3.2.1), byte for byte");
		for (int i = 0; i < 8; i++) EQI(r.rsv[i], 0, "rsv[%d] is zeroed for M4", i);
	}
	{   // FIX PASS (review finding 6): a textbox is recorded as its OWN bit and does NOT close
		// PRES_F_FIELD. The two sides of the gate want opposite answers, and folding them into one
		// flag made the consumer unable to tell them apart — which is how a peer reading a sign
		// ended up vanishing off the host's map.
		PresenceSrc s = src_ok(10, 20);
		s.textDlg = 1;
		PeerPresence r;
		presence_fill_core(&r, 0, PRES_GAME_HOENN, &s);
		CHECK(r.flags & PRES_F_TEXT,  "a field textbox sets PRES_F_TEXT\n");
		CHECK(r.flags & PRES_F_FIELD, "...and does NOT close PRES_F_FIELD: the position is still real\n");
		// ...which is also what makes a peer in a dialog still count as GAME-ACTIVE (D3.6 reads
		// SB1VALID|FIELD), so their record keeps stamping activeRound instead of decaying to
		// CONNECTED every time they talk to somebody.
		EQI(r.flags & PRES_F_SB1VALID, PRES_F_SB1VALID, "...with the position flag intact");
	}
	{   // D1.9: sb1 not ready -> no position, no field, and mapGroup/mapNum keep the -1 sentinel
		PresenceSrc s = src_ok(10, 20);
		s.sb1Valid = 0; s.px = -1; s.py = -1; s.mapGroup = -1; s.mapNum = -1;
		PeerPresence r;
		presence_fill_core(&r, 0, PRES_GAME_HOENN, &s);
		EQI(r.flags & PRES_F_SB1VALID, 0, "no save => no SB1VALID");
		EQI(r.flags & PRES_F_FIELD, 0, "...and the field predicate closes with it");
		EQI(r.mapGroup, -1, "mapGroup stays at the sentinel, never a stale 0");
		EQI(r.px, 0, "px is left at the zeroed default (never a garbage read)");
	}
	{   // D4.5.1: an inactive/unmapped gObjectEvents must SURVIVE as -1 = gate UNAVAILABLE
		PresenceSrc s = src_ok(10, 20);
		s.objX = -1; s.objY = -1;
		PeerPresence r;
		presence_fill_core(&r, 0, PRES_GAME_HOENN, &s);
		EQI(r.objX, -1, "objX keeps the -1 sentinel");
		EQI(r.objY, -1, "objY keeps the -1 sentinel");
		EQI(r.flags & PRES_F_OBJOK, 0, "OBJOK is NOT set");
		EQI(presence_obj_agree(&r), -1, "...and the gate reports UNAVAILABLE, never a pass");
	}
	{   // a DISAGREEING object event (beyond the tolerance) is a real gate closure, not unavailable
		PresenceSrc s = src_ok(10, 20);
		s.objX = 10 + PRES_MAP_OFFSET + 3;
		PeerPresence r;
		presence_fill_core(&r, 0, PRES_GAME_HOENN, &s);
		EQI(r.flags & PRES_F_OBJOK, 0, "OBJOK clear");
		EQI(presence_obj_agree(&r), 0, "the gate DISAGREES (0), which is distinct from unavailable");
		// ...and one tile of disagreement is normal and must still pass (D4.5's tolerance 1)
		s.objX = 10 + PRES_MAP_OFFSET + 1;
		presence_fill_core(&r, 0, PRES_GAME_HOENN, &s);
		EQI(presence_obj_agree(&r), 1, "one tile of lag is NORMAL and still agrees");
		EQI(r.flags & PRES_F_OBJOK, PRES_F_OBJOK, "...and the producer records it as OK");
	}
	{   // D1.3/D1.9: no fieldCamera => no CAM flag, sub-tile 0, and the anchor degrades to tiles
		PresenceSrc s = src_ok(10, 20);
		s.camOk = 0; s.camX = 9; s.camY = -9;
		PeerPresence r;
		presence_fill_core(&r, 0, PRES_GAME_HOENN, &s);
		EQI(r.flags & PRES_F_CAM, 0, "no camera => no CAM flag");
		EQI(r.subX, 0, "...and the phase is 0, not the unread value");
		EQI(r.subY, 0, "...both axes");
	}
	{   // the +-15 guard is applied HERE, exactly as main.c:854-855 applies it (out of range => 0)
		const int bad[6] = { 16, -16, 100, -100, 32767, -32768 };
		for (int i = 0; i < 6; i++) {
			PresenceSrc s = src_ok(10, 20);
			s.camX = bad[i]; s.camY = bad[i];
			PeerPresence r;
			presence_fill_core(&r, 0, PRES_GAME_HOENN, &s);
			EQI(r.subX, 0, "out-of-range camera phase %d degrades to 0, never clamps to an edge", bad[i]);
			EQI(r.subY, 0, "...both axes");
		}
		for (int c = -15; c <= 15; c++) {
			PresenceSrc s = src_ok(10, 20);
			s.camX = c; s.camY = -c;
			PeerPresence r;
			presence_fill_core(&r, 0, PRES_GAME_HOENN, &s);
			EQI(r.subX, c, "in-range phase %d is carried verbatim", c);
			EQI(r.subY, -c, "...both axes");
		}
	}
	{   // D1.9: no hbCtr => wedge detection DISABLED, never "wedged" by default
		PresenceSrc s = src_ok(10, 20);
		s.hbOk = 0; s.hb = 12345;
		PeerPresence r;
		presence_fill_core(&r, 0, PRES_GAME_HOENN, &s);
		EQI(r.flags & PRES_F_HB, 0, "no heartbeat address => no HB flag");
		EQI(r.hb, 0, "...and no value is invented");
	}
	{   // D1.9: no identity => name stays the empty (0xFF) one, TID hidden, gender MALE
		PresenceSrc s = src_ok(10, 20);
		s.identOk = 0;
		PeerPresence r;
		presence_fill_core(&r, 0, PRES_GAME_HOENN, &s);
		EQI(r.flags & PRES_F_IDENT, 0, "no IDENT flag");
		EQI(r.name[0], 0xFF, "the name is the immediately-terminated default");
		EQI(r.tid, 0, "no TID"); EQI(r.gender, 0, "gender defaults MALE");
		char out[9]; EQI(presence_name_ascii(r.name, out), 0, "...and it decodes to the empty string");
	}
	{   // D2.4: EVERY raw facing value, including the -1 game_read reports, is bounded art-side
		for (int f = -1; f < 32; f++) {
			PresenceSrc s = src_ok(10, 20);
			s.facing = f;
			PeerPresence r;
			presence_fill_core(&r, 0, PRES_GAME_HOENN, &s);
			CHECK(r.facing >= PRES_DIR_SOUTH && r.facing <= PRES_DIR_EAST,
			      "raw facing %d folds into 1..4 (got %d)\n", f, (int)r.facing);
		}
		// The four cardinals survive untouched — a wrong offset must be VISIBLE, not folded away.
		for (int f = PRES_DIR_SOUTH; f <= PRES_DIR_EAST; f++) {
			PresenceSrc s = src_ok(10, 20); s.facing = f;
			PeerPresence r; presence_fill_core(&r, 0, PRES_GAME_HOENN, &s);
			EQI(r.facing, f, "cardinal %d is carried verbatim", f);
		}
	}
	{   // gender is only ever 0 or 1, whatever byte EWRAM produced
		const int g[6] = { 0, 1, 2, 7, 255, -3 };
		for (int i = 0; i < 6; i++) {
			PresenceSrc s = src_ok(10, 20); s.gender = g[i];
			PeerPresence r; presence_fill_core(&r, 0, PRES_GAME_HOENN, &s);
			EQI(r.gender, g[i] == 1 ? 1 : 0, "gender byte %d folds to MALE unless it is exactly 1", g[i]);
		}
	}
	{   // a NULL name pointer must not be dereferenced (the reader passes NULL when nothing latched)
		PresenceSrc s = src_ok(10, 20);
		s.name = NULL;
		PeerPresence r;
		presence_fill_core(&r, 0, PRES_GAME_HOENN, &s);
		EQI(r.name[0], 0xFF, "a NULL name leaves the empty default in place");
		presence_fill_core(&r, 0, PRES_GAME_HOENN, NULL);   // must not crash
		EQI(r.gameId, PRES_GAME_HOENN, "a NULL source still yields an initialised record");
		presence_fill_core(NULL, 0, 0, &s);                 // must not crash
	}
	{   // no profile at all (the reader's gameId 0 path): P-G4 must close on it
		PresenceSrc s; memset(&s, 0, sizeof s);
		s.px = s.py = -1; s.mapGroup = s.mapNum = -1; s.objX = s.objY = -1; s.facing = -1;
		PeerPresence r;
		presence_fill_core(&r, 0, PRES_GAME_NONE, &s);
		EQI(r.gameId, PRES_GAME_NONE, "gameId 0 = presence silently OFF for the pair");
		EQI(r.flags, 0, "and not one flag is set");
	}
}

// TEST 23 — the gate truth table, end to end from RAW SOURCE VALUES (SPEC-data D4.1)
//
// TEST 5 enumerates the ladder against hand-built records. This one asks the harder question the
// slice actually shipped: does a game in state X, read the way presence_read.c reads it, produce a
// record that the ladder judges correctly? Each row breaks ONE thing in the source and names the
// reason code it must produce — so a producer bug (a flag never set, a sentinel overwritten) shows
// up as a gate verdict, which is what the user sees on the HUD.
static void test_gate_from_source(void) {
	printf("TEST 23: the gate truth table, driven end-to-end from raw source values\n");

	enum { SELF, PEER };
	// One row = "bend exactly this in the raw state of exactly this game, and the ladder must
	// answer exactly this". C has no lambdas, so the table is a macro rather than a function-
	// pointer array — which also keeps each bend readable at the point it is named.
	#define ROW(name_, side_, want_, body_) do {                                                  \
		PresenceSrc ss = src_ok(10, 10), sp = src_ok(12, 10);                                     \
		PresenceSrc* b = ((side_) == SELF) ? &ss : &sp;                                           \
		{ body_ }                                                                                 \
		PeerPresence rs, rp;                                                                      \
		presence_fill_core(&rs, 0, PRES_GAME_HOENN, &ss);                                         \
		presence_fill_core(&rp, 1, PRES_GAME_HOENN, &sp);                                         \
		Harness h; h_init(&h);                                                                    \
		PresenceOut o;                                                                            \
		h_step(&h, rs, rp, &o);                                                                   \
		EQI(o.reason, (want_), "%s => %s (got %s)", name_,                                        \
		    presence_off_reason(want_), presence_off_reason(o.reason));                           \
	} while (0)

	ROW("a healthy pair on one map",         SELF, PRES_OFF_NONE,  (void)b;);
	ROW("the PEER left the overworld (ctx)", PEER, PRES_OFF_FIELD, b->ctx = 6 /*GCTX_FIELDMENU*/;);
	// FIX PASS (review finding 6): the PEER reading a sign KEEPS DRAWING. Their textbox is on their
	// screen; their position is a valid tile they are standing still on. Gating on it made a friend
	// vanish for the length of every NPC line (the D4.7 hold covers only 0.5 s of it).
	ROW("the PEER has a textbox up",         PEER, PRES_OFF_NONE,  b->textDlg = 1;);
	ROW("the PEER's save is not loaded",     PEER, PRES_OFF_FIELD, b->sb1Valid = 0; b->px = -1; b->py = -1;);
	ROW("game_read said !valid for the PEER",PEER, PRES_OFF_FIELD, b->ok = 0;);
	ROW("the PEER's field is torn down",     PEER, PRES_OFF_OBJ,   b->objX = 12 + PRES_MAP_OFFSET + 4;);
	ROW("the SELF game left the overworld",  SELF, PRES_OFF_SELF,  b->ctx = 2 /*GCTX_BATTLE_ACTION*/;);
	ROW("the SELF game has a textbox up",    SELF, PRES_OFF_SELF,  b->textDlg = 1;);
	ROW("the SELF game disagrees w/ its obj",SELF, PRES_OFF_SELFOBJ,b->objX = 10 + PRES_MAP_OFFSET + 4;);
	ROW("a different map",                   PEER, PRES_OFF_MAP,   b->mapNum = MAPN + 1;);
	ROW("a different map GROUP",             PEER, PRES_OFF_MAP,   b->mapGroup = MAPG + 1;);
	ROW("the peer walked off screen",        PEER, PRES_OFF_CULL,  b->px = 50; b->objX = 50 + PRES_MAP_OFFSET;);

	// An UNAVAILABLE object gate (mapObjects == 0, or the slot inactive) must be SKIPPED, not
	// treated as a failure — D4.5.1's whole point, and the one case a naive `objOk` bit gets wrong.
	ROW("the peer's mapObjects is unmapped", PEER, PRES_OFF_NONE, b->objX = -1; b->objY = -1;);
	ROW("the self game's mapObjects is unmapped", SELF, PRES_OFF_NONE, b->objX = -1; b->objY = -1;);

	// P-G4: a game with NO PROFILE never gets a gameId, so presence is silently OFF for the pair
	// (invariant 5) — the ladder must say NOPROF, not walk on into the field rules.
	{
		PresenceSrc ss = src_ok(10, 10), sp = src_ok(12, 10);
		PeerPresence rs, rp;
		PresenceOut o;
		presence_fill_core(&rs, 0, PRES_GAME_NONE,  &ss);
		presence_fill_core(&rp, 1, PRES_GAME_HOENN, &sp);
		Harness h1; h_init(&h1); h_step(&h1, rs, rp, &o);
		EQI(o.reason, PRES_OFF_NOPROF, "the SELF game has no profile => NOPROF");
		presence_fill_core(&rs, 0, PRES_GAME_HOENN, &ss);
		presence_fill_core(&rp, 1, PRES_GAME_NONE,  &sp);
		Harness h2b; h_init(&h2b); h_step(&h2b, rs, rp, &o);
		EQI(o.reason, PRES_OFF_NOPROF, "the PEER game has no profile => NOPROF");
	}

	// The map-universe gate: the user's ACTUAL pair (Emerald + FireRed) on identical map ids.
	{
		PresenceSrc ss = src_ok(10, 10), sp = src_ok(12, 10);
		PeerPresence rs, rp;
		presence_fill_core(&rs, 0, PRES_GAME_HOENN, &ss);
		presence_fill_core(&rp, 1, PRES_GAME_KANTO, &sp);
		Harness h; h_init(&h);
		PresenceOut o;
		h_step(&h, rs, rp, &o);
		EQI(o.reason, PRES_OFF_UNIVERSE, "Emerald + FireRed on the same (mapGroup,mapNum) => OFF");
		// ...and the FR/LG pair, which is the one cross-title pair that IS allowed.
		presence_fill_core(&rs, 0, PRES_GAME_KANTO, &ss);
		Harness h2; h_init(&h2);
		h_step(&h2, rs, rp, &o);
		EQI(o.reason, PRES_OFF_NONE, "FireRed + LeafGreen share a map table => allowed");
	}
	// And the three flags main.c feeds the ladder, from a healthy pair (nothing in the SOURCE is
	// wrong — these are OUR state, not the game's).
	{
		PresenceSrc ss = src_ok(10, 10), sp = src_ok(12, 10);
		PeerPresence rs, rp;
		presence_fill_core(&rs, 0, PRES_GAME_HOENN, &ss);
		presence_fill_core(&rp, 1, PRES_GAME_HOENN, &sp);
		const struct { int en, menu, link, want; const char* what; } F[4] = {
			{ 1, 0, 0, PRES_OFF_NONE,     "all clear" },
			{ 0, 0, 0, PRES_OFF_DISABLED, "the pref is off" },
			{ 1, 1, 0, PRES_OFF_MENU,     "our pause menu is up" },
			{ 1, 0, 1, PRES_OFF_LINK,     "a link/trade session is live (D4.8)" },
		};
		for (int i = 0; i < 4; i++) {
			Harness h; h_init(&h);
			presence_begin_round(&h.ps, h.round);
			PeerPresence peer = rp; peer.round = h.round; peer.hb = h.round;
			presence_publish(&h.ps, 0, &peer);
			PresenceIn in; memset(&in, 0, sizeof in);
			in.enabled = F[i].en; in.menuOpen = F[i].menu; in.linkAny = F[i].link;
			in.slot = 0; in.self = rs;
			PresenceOut o;
			presence_solve(&h.ps, &in, &o);
			EQI(o.reason, F[i].want, "%s => %s", F[i].what, presence_off_reason(F[i].want));
		}
	}
	#undef ROW
}

// TEST 24 — the charmap goldens, through the producer (SPEC-data D6.3.1 / SPEC-avatar A4.2.1)
static void test_name_through_producer(void) {
	printf("TEST 24: the peer's name/TID/gender survive the producer and decode correctly\n");

	{   // THE in-repo anchor: Celio-Link's canned demo identity, celiolink_payloads.h:98-100.
		// 0xC8-0xBB=13 -> 'N', 0xDD-0xD5=8 -> 'i', 0xE0-0xD5=11 -> 'l', 0xE7-0xD5=18 -> 's'.
		// It is the only part of the table with a real in-repo witness, and it pins BOTH letter
		// ranges and the terminator at once.
		PresenceSrc s = src_ok(10, 20);
		s.name = NAME_NILS; s.tid = 1234; s.gender = 0;
		PeerPresence r;
		presence_fill_core(&r, 0, PRES_GAME_HOENN, &s);
		char out[9];
		EQI(presence_name_ascii(r.name, out), 4, "\"Nils\" is four characters");
		CHECK(!strcmp(out, "Nils"), "the in-repo Celio anchor decodes to \"Nils\" (got \"%s\")\n", out);
		EQI(r.tid, 1234, "the visible TID rides the record");
		// A4.3.2: the trainer card shows five digits — the HUD's %05u of this value.
		{ char tb[8]; snprintf(tb, sizeof tb, "%05u", (unsigned)r.tid);
		  CHECK(!strcmp(tb, "01234"), "TID renders as the five-digit card form (got %s)\n", tb); }
	}
	{   // a full 8-character name (no terminator at all) must still NUL-terminate inside out[9]
		const uint8_t full[8] = { 0xBB, 0xBC, 0xBD, 0xD5, 0xD6, 0xA1, 0xAD, 0xAE };   // ABCabc0.-
		PresenceSrc s = src_ok(1, 1); s.name = full;
		PeerPresence r; presence_fill_core(&r, 0, PRES_GAME_HOENN, &s);
		char out[9];
		EQI(presence_name_ascii(r.name, out), 8, "all eight bytes decode");
		CHECK(!strcmp(out, "ABCab0.-"), "the un-terminated 8-byte name decodes fully (got \"%s\")\n", out);
		EQI(out[8], '\0', "...and out[8] is the NUL");
	}
	{   // gendered / accented bytes are cosmetic '?' — never an out-of-range read (D6.3.1)
		const uint8_t odd[8] = { 0xB5, 0xB6, 0x01, 0x14, 0xBB, 0xFF, 0xFF, 0xFF };
		PresenceSrc s = src_ok(1, 1); s.name = odd;
		PeerPresence r; presence_fill_core(&r, 0, PRES_GAME_HOENN, &s);
		char out[9];
		EQI(presence_name_ascii(r.name, out), 5, "five bytes before the terminator");
		CHECK(!strcmp(out, "????A"), "unmapped bytes become '?', the letter survives (got \"%s\")\n", out);
	}
	{   // identity NOT latched: the record's name must be the empty one, whatever was passed
		PresenceSrc s = src_ok(1, 1); s.identOk = 0; s.name = NAME_NILS; s.tid = 4242; s.gender = 1;
		PeerPresence r; presence_fill_core(&r, 0, PRES_GAME_HOENN, &s);
		char out[9];
		EQI(presence_name_ascii(r.name, out), 0, "no identity => an empty decoded name");
		EQI(r.tid, 0, "...and no TID leaks through");
		EQI(r.gender, 0, "...and gender falls back to MALE");
	}
}

// ============================================================================================
// SLICE M2 — the render half (SPEC-avatar.md). TEST 25-32.
// ============================================================================================
// main.c's own constants, reproduced here because main.c cannot be included on the host. Cited so
// a future edit to either side is a visible divergence rather than a silent one.
#define POP3D_PLAYER_GX 112   // main.c:693 "player tile (7,5) -> sprite rect (16x32, head 16px
#define POP3D_PLAYER_GY 64    //            above the tile)"

// ============================================================================================
// TEST 25 — sheet cell arithmetic (SPEC-avatar A1.2/A1.3, A7.1 P3)
// ============================================================================================
static void test_art_cells(void) {
	printf("TEST 25: sheet cells — every (variant, facing, pose) lands inside the 128x128 sheet\n");
	// Exhaustive over the FULL byte range of `facing` and past both ends of `pose`/`gender`: the
	// degradation ladder (A0.4) says an unknown facing draws the DOWN STANDING frame, and the thing
	// that must be impossible is an out-of-range sheet cell, so the sweep is over inputs no caller
	// should ever produce.
	for (int var = -1; var <= 2; var++) {
		for (int f = 0; f < 256; f++) {
			for (int p = -1; p <= PRES_POSE_COUNT; p++) {
				PresArtCell c;
				c.x = c.y = c.mirror = -999;
				presence_art_cell(var, f, p, &c);
				CHECK(c.x >= 0 && c.x + PRES_CELL_W  <= PRES_SHEET_DIM &&
				      c.y >= 0 && c.y + PRES_CELL_H <= PRES_SHEET_DIM,
				      "cell (var=%d facing=%d pose=%d) escapes the sheet: (%d,%d)\n", var, f, p, c.x, c.y);
				CHECK(c.mirror == 0 || c.mirror == 1,
				      "mirror must be a flag (var=%d facing=%d pose=%d -> %d)\n", var, f, p, c.mirror);
			}
		}
	}
	// The four real facings, spelled out (A1.2's sheet layout is the contract both the placeholder
	// and the eventual real art obey, so it is asserted once for both).
	{
		PresArtCell c;
		presence_art_cell(0, PRES_DIR_SOUTH, PRES_POSE_STAND, &c);
		EQI(c.x, 0,  "S/STAND col 0"); EQI(c.y, 0,  "S row 0"); EQI(c.mirror, 0, "S not mirrored");
		presence_art_cell(0, PRES_DIR_NORTH, PRES_POSE_STAND, &c);
		EQI(c.y, PRES_CELL_H, "N row 1");                       EQI(c.mirror, 0, "N not mirrored");
		presence_art_cell(0, PRES_DIR_WEST,  PRES_POSE_STAND, &c);
		EQI(c.y, 2 * PRES_CELL_H, "W row 2");                   EQI(c.mirror, 0, "W not mirrored");
		presence_art_cell(0, PRES_DIR_EAST,  PRES_POSE_STAND, &c);
		EQI(c.y, 2 * PRES_CELL_H, "E is the WEST row...");      EQI(c.mirror, 1, "...drawn MIRRORED");
		// A1.2's whole justification: E and W can never drift apart because they are ONE cell.
		PresArtCell w, e;
		for (int p = 0; p < PRES_POSE_COUNT; p++) {
			presence_art_cell(0, PRES_DIR_WEST, p, &w);
			presence_art_cell(0, PRES_DIR_EAST, p, &e);
			EQI(w.x, e.x, "E/W share a cell x (pose %d)", p);
			EQI(w.y, e.y, "E/W share a cell y (pose %d)", p);
		}
	}
	// A0.4 / A1.2.1: facing outside 1..4 selects row 0 COL 0 — the pose is forced to STAND too, so
	// a garbage nibble can never present as a walking peer.
	for (int f = 0; f < 256; f++) {
		if (f >= PRES_DIR_SOUTH && f <= PRES_DIR_EAST) continue;
		PresArtCell c;
		presence_art_cell(0, f, PRES_POSE_STEP_B, &c);
		EQI(c.x, 0, "unknown facing %d -> col 0", f);
		EQI(c.y, 0, "unknown facing %d -> row 0", f);
		EQI(c.mirror, 0, "unknown facing %d -> not mirrored", f);
	}
	// Variant stride, and "the 9 authored cells of a variant tile 48x96 exactly" — proven by
	// painting the sheet with cell ids and checking the cover is exact and disjoint.
	{
		static int8_t cover[PRES_SHEET_DIM][PRES_SHEET_DIM];
		memset(cover, 0, sizeof cover);
		int overlaps = 0;
		for (int var = 0; var < 2; var++) {
			for (int d = PRES_DIR_SOUTH; d <= PRES_DIR_WEST; d++) {     // the 3 AUTHORED rows
				for (int p = 0; p < PRES_POSE_COUNT; p++) {
					PresArtCell c; presence_art_cell(var, d, p, &c);
					for (int y = 0; y < PRES_CELL_H; y++)
						for (int x = 0; x < PRES_CELL_W; x++) {
							if (cover[c.y + y][c.x + x]) overlaps++;
							cover[c.y + y][c.x + x] = 1;
						}
				}
			}
		}
		EQI(overlaps, 0, "the 18 authored cells never overlap");
		int inside = 0, outside = 0;
		for (int y = 0; y < PRES_SHEET_DIM; y++)
			for (int x = 0; x < PRES_SHEET_DIM; x++) {
				int v0 = (x < PRES_VAR_W && y < PRES_VAR_H);
				int v1 = (x >= PRES_VAR_STRIDE && x < PRES_VAR_STRIDE + PRES_VAR_W && y < PRES_VAR_H);
				if (v0 || v1) { if (!cover[y][x]) inside++; }
				else          { if ( cover[y][x]) outside++; }
			}
		EQI(inside, 0,  "the two 48x96 variant blocks are covered EXACTLY");
		EQI(outside, 0, "...and nothing is drawn outside them");
		EQI(PRES_VAR_W, 48, "one variant is 48 wide");
		EQI(PRES_VAR_H, 96, "one variant is 96 tall");
	}
}

// ============================================================================================
// TEST 26 — draw-position math + the anchor identity (SPEC-avatar A0.2.1/A1.1, A7.1 P1/P2)
// ============================================================================================
static void test_art_rect(void) {
	printf("TEST 26: foot anchor -> sprite rect, and the same-tile anchor identity (Open Q2 pinned)\n");
	// THE cheapest possible regression test (A0.2.1): a peer standing on the host's OWN tile must
	// place the sprite top-left at the player's own sprite rect. If a future edit breaks the anchor
	// convention this fires before anything is drawn.
	//
	// *** THE 8 px THAT THE TWO SPECS DISAGREE ABOUT — DO NOT "FIX" EITHER SIDE HERE. ***
	// SPEC-avatar A0.2.1 states the degenerate case as (112, 64) == (POP3D_PLAYER_GX,
	// POP3D_PLAYER_GY). SPEC-data D5.4 derives the FOOT anchor from pret's own sprite chain
	// (event_object_movement.c:1850-1854 + field_camera.c:445/453/462) and gets 88, which puts the
	// sprite top at 88 - 32 = 56; main.c:694's POP3D_PLAYER_GY 64 implies 96 instead, and
	// presence.h:50-57 records that that constant is UNUSED (grep finds only its definition), i.e.
	// a comment rather than a validated value. So slice M0 shipped 88 marked VERIFY-ON-HW and named
	// it "the ONE number in this module that could be 8 px wrong" (BUILDLOG Open Q2). The house
	// rule is flag, never silently correct — so this test pins BOTH facts:
	//   (a) the conversion itself is exact, in terms of the SHIPPED constant, and
	//   (b) the divergence from POP3D_PLAYER_GY is exactly -8 px, right now.
	// The M2 calibration run (D5.4.1 / hardware item H1) stands both players on the SAME TILE and
	// photographs the overlay against the host's own character. Whichever way it lands, ONE number
	// changes (PRES_ANCHOR_Y) and this assertion fires, forcing the edit to be deliberate.
	EQI((int)(PRES_ANCHOR_Y - (float)PRES_FOOT_DY) - POP3D_PLAYER_GY, -8,
	    "Open Q2 UNRESOLVED: pret's chain (anchor 88 -> sprite top 56) sits 8 px above "
	    "main.c:694's implied 96 -> 64. Settle it on hardware, then change PRES_ANCHOR_Y and this line");
	for (int c = -15; c <= 15; c++) {
		Harness h; h_init(&h);
		PeerPresence r = rec_ok(20, 30, c, -c);
		PresenceOut o;
		EQI(h_step(&h, r, r, &o), 1, "identity draws at phase %d", c);
		float sx, sy; presence_art_rect(o.footX, o.footY, &sx, &sy);
		// X is corroborated twice (POP3D_PLAYER_GX 112 + 8 for the cell centre, and pret's chain)
		// and is NOT at risk — so it is asserted against main.c's own constant.
		EQF(sx, (float)POP3D_PLAYER_GX, "identity sprite x == POP3D_PLAYER_GX at phase %d", c);
		EQF(sy, PRES_ANCHOR_Y - (float)PRES_FOOT_DY, "identity sprite y == the shipped anchor at phase %d", c);
	}
	// The conversion itself, over a spread of anchors: sprite top-left = (foot - 8, foot - 32), i.e.
	// cell top-left (foot-8, foot-16) with the 16 px head margin above it (A1.1).
	for (float fx = -40.0f; fx <= 280.0f; fx += 7.5f) {
		for (float fy = -40.0f; fy <= 200.0f; fy += 11.0f) {
			float sx, sy; presence_art_rect(fx, fy, &sx, &sy);
			EQF(sx, fx - 8.0f,  "sprX = footX - 8");
			EQF(sy, fy - 32.0f, "sprY = footY - 32");
		}
	}
	// A2's sub-tile continuity, seen THROUGH the rect conversion: the whole-step sweep TEST 3 runs
	// on footX is re-run here on the drawn rect, so a future edit that "helpfully" re-applies a
	// sub-tile term in the render half (the exact thing BUILDLOG M0 deviation 10 forbids) shows up
	// as a doubled delta rather than as a subtly fast avatar.
	const int speeds[3] = { 1, 2, 4 };
	for (int s = 0; s < 3; s++) {
		int speed = speeds[s], n = 16 / speed;
		for (int moverIsPeer = 0; moverIsPeer < 2; moverIsPeer++) {
			for (int dir = -1; dir <= 1; dir += 2) {
				Harness h; h_init(&h);
				float prev = 0.0f, total = 0.0f;
				for (int k = 0; k <= n; k++) {
					int tile = (k == 0) ? 0 : dir;
					int ph   = (k == 0 || k == n) ? 0 : dir * k * speed;
					PeerPresence mover = rec_ok(10 + tile, 10, ph, 0);
					PeerPresence still = rec_ok(10, 10, 0, 0);
					PresenceOut o;
					int d = moverIsPeer ? h_step(&h, still, mover, &o) : h_step(&h, mover, still, &o);
					EQI(d, 1, "rect sweep draws (peer=%d dir=%d s=%d k=%d)", moverIsPeer, dir, speed, k);
					float sx, sy; presence_art_rect(o.footX, o.footY, &sx, &sy);
					EQF(sy, PRES_ANCHOR_Y - 32.0f, "y is untouched by an x walk");
					if (k > 0) {
						float want = (float)((moverIsPeer ? dir : -dir) * speed);
						EQF(sx - prev, want, "rect delta (peer=%d dir=%d s=%d k=%d)",
						    moverIsPeer, dir, speed, k);
						total += sx - prev;
					}
					prev = sx;
				}
				EQF(total, (float)((moverIsPeer ? dir : -dir) * 16), "one whole tile, exactly");
			}
		}
	}
}

// ============================================================================================
// TEST 27 — culling and clipping (SPEC-avatar A2.4, A7.1 P4)
// ============================================================================================
static void test_art_clip(void) {
	printf("TEST 27: cull/clip — exhaustive 1 px sweep over the frame and well past every edge\n");
	// The sweep is aggregated (one CHECK per PROPERTY, with the first offending case printed)
	// because 300k individual CHECKs would drown the run without proving anything more. Every
	// property below is evaluated at every one of the ~78k positions, both mirrored and not.
	int badCull = 0, badBounds = 0, badSize = 0, badReg = 0, badCell = 0;
	int firstCullX = 0, firstCullY = 0;
	for (int mirror = 0; mirror < 2; mirror++) {
		for (int ix = -32; ix <= 272; ix++) {
			for (int iy = -48; iy <= 208; iy++) {
				float sx = (float)ix, sy = (float)iy;
				PresArtDraw d;
				int vis = presence_art_clip(sx, sy, mirror, 0.0f, &d);
				// Truth: the sprite rect [sx, sx+16) x [sy, sy+32) intersects [0,240) x [0,160).
				int want = (sx + PRES_CELL_W > 0.0f) && (sx < (float)PRES_FRAME_W) &&
				           (sy + PRES_CELL_H > 0.0f) && (sy < (float)PRES_FRAME_H);
				if (vis != want) {
					if (!badCull) { firstCullX = ix; firstCullY = iy; }
					badCull++;
					continue;
				}
				if (!vis) continue;
				// The visible rect NEVER leaves the frame box — the property main.c:731-732 calls
				// out as mandatory ("a shifted pop never bleeds into the letterbox").
				if (!(d.x >= -0.0001f && d.y >= -0.0001f &&
				      d.x + (float)d.w <= (float)PRES_FRAME_W + 0.0001f &&
				      d.y + (float)d.h <= (float)PRES_FRAME_H + 0.0001f)) badBounds++;
				if (d.w <= 0 || d.w > PRES_CELL_W || d.h <= 0 || d.h > PRES_CELL_H) badSize++;
				// The trimmed SOURCE window stays inside the cell...
				if (d.cx < 0 || d.cy < 0 || d.cx + d.w > PRES_CELL_W || d.cy + d.h > PRES_CELL_H) badCell++;
				// ...and the visible part stays REGISTERED: for an unmirrored draw the surviving
				// source column d.cx must land exactly at d.x, i.e. d.x - sx == d.cx. (The mirrored
				// registration is TEST 28's whole subject.)
				if (!mirror) {
					float regX = d.x - sx, regY = d.y - sy;
					if (regX < (float)d.cx - 0.0001f || regX > (float)d.cx + 1.0001f) badReg++;
					if (regY < (float)d.cy - 0.0001f || regY > (float)d.cy + 1.0001f) badReg++;
				} else {
					float regY = d.y - sy;
					if (regY < (float)d.cy - 0.0001f || regY > (float)d.cy + 1.0001f) badReg++;
				}
			}
		}
	}
	EQI(badCull, 0, "cull <=> no intersection with the frame rect (first bad at %d,%d)", firstCullX, firstCullY);
	EQI(badBounds, 0, "the clipped rect never leaves [0,240]x[0,160]");
	EQI(badSize, 0, "the clipped size is always 1..16 x 1..32");
	EQI(badCell, 0, "the source window never leaves the 16x32 cell");
	EQI(badReg, 0, "the visible part stays registered where it was");

	// Hand-computed spot checks, one per edge and one per corner (A7.1 P4's second half).
	PresArtDraw d;
	CHECK(presence_art_clip(-5.0f, 40.0f, 0, 0.0f, &d), "left edge is partially visible\n");
	EQF(d.x, 0.0f, "left: dst clamps to 0"); EQI(d.w, 11, "left: 5 columns cut");
	EQI(d.cx, 5, "left: the source starts 5 in"); EQI(d.h, 32, "left: height untouched");
	CHECK(presence_art_clip(232.0f, 40.0f, 0, 0.0f, &d), "right edge is partially visible\n");
	EQF(d.x, 232.0f, "right: dst unchanged"); EQI(d.w, 8, "right: 8 columns survive");
	EQI(d.cx, 0, "right: the source starts at the cell left");
	CHECK(presence_art_clip(100.0f, -7.0f, 0, 0.0f, &d), "top edge is partially visible\n");
	EQF(d.y, 0.0f, "top: dst clamps to 0"); EQI(d.h, 25, "top: 7 rows cut"); EQI(d.cy, 7, "top: source offset");
	CHECK(presence_art_clip(100.0f, 150.0f, 0, 0.0f, &d), "bottom edge is partially visible\n");
	EQI(d.h, 10, "bottom: 10 rows survive"); EQI(d.cy, 0, "bottom: source starts at the cell top");
	CHECK(presence_art_clip(-6.0f, -9.0f, 0, 0.0f, &d), "top-left corner is partially visible\n");
	EQI(d.w, 10, "corner w"); EQI(d.h, 23, "corner h"); EQI(d.cx, 6, "corner cx"); EQI(d.cy, 9, "corner cy");
	EQI(presence_art_clip(-16.0f, 40.0f, 0, 0.0f, &d), 0, "exactly one cell off the left edge is CULLED");
	EQI(presence_art_clip(240.0f, 40.0f, 0, 0.0f, &d), 0, "exactly at the right edge is CULLED");
	EQI(presence_art_clip(100.0f, -32.0f, 0, 0.0f, &d), 0, "exactly one cell above is CULLED");
	EQI(presence_art_clip(100.0f, 160.0f, 0, 0.0f, &d), 0, "exactly at the bottom edge is CULLED");
	// Fractional positions round OUTWARD, so a sub-pixel sliver is dropped rather than allowed to
	// bleed past the frame box (the direction main.c:731-732's rule picks). Same-console every
	// anchor is an integer (SPEC-data D5.3 is exact and D5.5's filter is a bit-exact no-op at
	// age 0), so this only bites over a radio (M4) — where dropping half a pixel at the very frame
	// edge is unarguably the right trade against a sliver of avatar in the letterbox.
	CHECK(presence_art_clip(-0.5f, 40.0f, 0, 0.0f, &d), "a half-pixel overhang on the LEFT still draws\n");
	CHECK(d.x >= 0.0f, "...with the destination inside the frame (got %.3f)\n", (double)d.x);
	EQI(d.w, 15, "...having dropped the one straddling source column");
	CHECK(presence_art_clip(238.5f, 40.0f, 0, 0.0f, &d), "one whole column at the right edge still draws\n");
	EQI(d.w, 1, "...exactly one column survives");
	CHECK(d.x + (float)d.w <= (float)PRES_FRAME_W, "...and stays inside (got %.3f)\n",
	      (double)(d.x + (float)d.w));
	EQI(presence_art_clip(239.5f, 40.0f, 0, 0.0f, &d), 0,
	    "a sub-pixel sliver at the right edge is CULLED, never drawn half in the letterbox");
	// Garbage/NaN can never reach a float->int cast (the anchor is bounded, but a torn record is not).
	EQI(presence_art_clip(1.0e9f, 40.0f, 0, 0.0f, &d), 0, "an absurd x is culled, not cast");
	EQI(presence_art_clip(40.0f, -1.0e9f, 0, 0.0f, &d), 0, "an absurd y is culled, not cast");
	EQI(presence_art_clip(0.0f, 0.0f, 0, 0.0f, NULL), 0, "a NULL out is refused");
}

// ============================================================================================
// TEST 28 — the MIRRORED clip stays registered (SPEC-avatar A1.2.2 + A2.4.2)
// ============================================================================================
static void test_art_clip_mirror(void) {
	printf("TEST 28: a mirrored (EAST) cell takes its cuts off the OPPOSITE source edge\n");
	// A mirrored draw maps the destination's LEFT edge to the source's RIGHT edge. Cutting the
	// destination left must therefore drop source columns from the RIGHT. Getting this backwards is
	// invisible mid-screen and makes the sprite slide inside its own box at the frame edges.
	PresArtDraw f, m;
	// Cut 5 off the LEFT: unmirrored keeps source columns 5..15, mirrored keeps 0..10.
	CHECK(presence_art_clip(-5.0f, 40.0f, 0, 0.0f, &f), "unmirrored left clip\n");
	CHECK(presence_art_clip(-5.0f, 40.0f, 1, 0.0f, &m), "mirrored left clip\n");
	EQI(f.w, m.w, "same visible width either way");
	EQI(f.x == m.x, 1, "same destination either way");
	EQI(f.cx, 5, "unmirrored: source starts 5 in");
	EQI(m.cx, 0, "mirrored: the cut came off the source RIGHT, so the source starts at 0");
	// Cut 8 off the RIGHT: unmirrored keeps 0..7, mirrored keeps 8..15.
	CHECK(presence_art_clip(232.0f, 40.0f, 0, 0.0f, &f), "unmirrored right clip\n");
	CHECK(presence_art_clip(232.0f, 40.0f, 1, 0.0f, &m), "mirrored right clip\n");
	EQI(f.w, 8, "8 columns survive");
	EQI(m.w, 8, "...either way");
	EQI(f.cx, 0, "unmirrored: source from the left");
	EQI(m.cx, 8, "mirrored: source from the right half");
	// Vertical cuts are orientation-independent (there is no vertical mirror).
	for (int iy = -40; iy <= 200; iy++) {
		PresArtDraw a, b;
		int va = presence_art_clip(100.0f, (float)iy, 0, 0.0f, &a);
		int vb = presence_art_clip(100.0f, (float)iy, 1, 0.0f, &b);
		EQI(va, vb, "mirroring never changes visibility (y=%d)", iy);
		if (!va) continue;
		EQI(a.cy, b.cy, "mirroring never changes the vertical source offset (y=%d)", iy);
		EQI(a.h,  b.h,  "mirroring never changes the visible height (y=%d)", iy);
	}
	// The sum rule that makes both orientations one function: cx is a cut off one side or the
	// other, so cx + w never exceeds the cell, in either orientation, anywhere on the sweep.
	int bad = 0;
	for (int mirror = 0; mirror < 2; mirror++)
		for (int ix = -20; ix <= 260; ix++) {
			PresArtDraw d;
			if (!presence_art_clip((float)ix, 40.0f, mirror, 0.0f, &d)) continue;
			if (d.cx + d.w > PRES_CELL_W || d.cx < 0) bad++;
		}
	EQI(bad, 0, "cx + w stays inside the cell for both orientations");
}

// ============================================================================================
// TEST 29 — the tilt composition is a PURE TRANSLATION (SPEC-avatar A3.1, A7.1 P5)
// ============================================================================================
static void test_art_tilt(void) {
	printf("TEST 29: tilted avatar = flat rect + ONE constant offset; unscaled; identity at 0 deg\n");
	// gen1-render.md finding 2: "translate(sx - fx, sy - fy); drawFn() -- PURE TRANSLATION ... the
	// sprite is UPRIGHT and UNSCALED ('depthScale is deliberately ignored for sizing ...
	// pixel-identical to flat mode'). Only the anchor moves." This test drives the REAL shipped
	// tilt_view_init/tilt_project (source/tilt.c) so it cannot pass against a re-derivation.
	int sawNonZero = 0, sawDiffersFromPerCorner = 0;
	// main.c's SPILL, recomputed here from the same four frame corners (fix pass, finding 4): how
	// far the projected image reaches outside the flat 240x160 frame. The projection maps the frame
	// ONTO the trapezoid, so the outermost excursion is attained at the frame's own corners.
	float spill[TILT_LEVELS];
	for (int lvl = 0; lvl < TILT_LEVELS; lvl++) {
		TiltView v;
		tilt_view_init(&v, TILT_ANGLE_DEG[lvl] * TILT_DEG2RAD,
		               (float)PRES_FRAME_W, (float)PRES_FRAME_H, TILT_COVER_MIX);
		static const float CX[4] = { 0.0f, (float)PRES_FRAME_W, 0.0f, (float)PRES_FRAME_W };
		static const float CY[4] = { 0.0f, 0.0f, (float)PRES_FRAME_H, (float)PRES_FRAME_H };
		spill[lvl] = 0.0f;
		for (int c = 0; c < 4; c++) {
			float px, py, q;
			tilt_project(&v, CX[c], CY[c], &px, &py, &q);
			float ex = (px < 0.0f) ? -px : (px - (float)PRES_FRAME_W);
			float ey = (py < 0.0f) ? -py : (py - (float)PRES_FRAME_H);
			if (ex > spill[lvl]) spill[lvl] = ex;
			if (ey > spill[lvl]) spill[lvl] = ey;
		}
		if (lvl == 0) EQF(spill[0], 0.0f, "a flat view spills nothing, so the clip box is unchanged");
		else          CHECK(spill[lvl] > 0.0f, "a tilted view spills (lvl %d, got %.3f)\n",
		                    lvl, (double)spill[lvl]);
	}
	for (int lvl = 0; lvl < TILT_LEVELS; lvl++) {
		TiltView v;
		tilt_view_init(&v, TILT_ANGLE_DEG[lvl] * TILT_DEG2RAD,
		               (float)PRES_FRAME_W, (float)PRES_FRAME_H, TILT_COVER_MIX);
		for (int fx = 16; fx <= 224; fx += 16) {
			for (int fy = 40; fy <= 152; fy += 8) {
				float ax = (float)fx, ay = (float)fy;
				float sprX, sprY; presence_art_rect(ax, ay, &sprX, &sprY);
				float fax, fay, q;
				tilt_project(&v, ax, ay, &fax, &fay, &q);
				CHECK(q > 0.0f, "q must be strictly positive (lvl %d, %d,%d)\n", lvl, fx, fy);
				float dx = fax - ax, dy = fay - ay;

				// The shipped rule: clip the UNPROJECTED rect, then translate it (A3.2.1) — with the
				// box widened by the view's SPILL under tilt (fix pass, finding 4).
				// NOTE: this pair used to be called with IDENTICAL arguments and then asserted equal,
				// i.e. the function compared to itself — a check that could never fail. It now
				// compares the FLAT box against the TILTED (spilled) box, which is a real statement:
				// a wider box can only ever keep MORE of the sprite, never less.
				PresArtDraw flat, tilted;
				int vf = presence_art_clip(sprX, sprY, 0, 0.0f,      &flat);
				int vt = presence_art_clip(sprX, sprY, 0, spill[lvl], &tilted);
				CHECK(!vf || vt, "the spilled box never culls what the flat box kept (lvl %d)\n", lvl);
				if (vf && vt) {
					CHECK(tilted.w >= flat.w && tilted.h >= flat.h,
					      "the spilled box never trims MORE than the flat box (lvl %d, %d,%d)\n",
					      lvl, fx, fy);
				}
				if (!vf) continue;
				// THE assertion: the size is BITWISE unchanged by the tilt. No q, no k*q, no
				// tilt_disp_scale anywhere in the sizing path — any non-integer scale on 16x32
				// pixel art turns it to mush (A3.1.1).
				EQI(tilted.w, flat.w, "tilt never changes the sprite WIDTH (lvl %d)", lvl);
				EQI(tilted.h, flat.h, "tilt never changes the sprite HEIGHT (lvl %d)", lvl);

				if (lvl == 0) {
					// At angle 0 the projection is the identity, bitwise (tilt.h:28-30), so the
					// tilted path IS the flat path — phase 14's invariant 1, inherited.
					CHECK(dx == 0.0f && dy == 0.0f,
					      "angle 0 must translate by exactly (0,0), got (%.9f,%.9f)\n",
					      (double)dx, (double)dy);
				} else {
					if (dx != 0.0f || dy != 0.0f) sawNonZero++;
					// Non-vacuity: a PER-CORNER projection would NOT be a translation. Project the
					// sprite's top-left corner directly and show it differs from corner+delta —
					// that difference is exactly the "upright, unscaled billboard" decision, so if
					// it ever vanished the test above would be asserting nothing.
					float cfx, cfy, cq;
					tilt_project(&v, sprX, sprY, &cfx, &cfy, &cq);
					float pcx = sprX + dx, pcy = sprY + dy;
					float ddx = cfx - pcx, ddy = cfy - pcy;
					if (ddx < 0) ddx = -ddx;
					if (ddy < 0) ddy = -ddy;
					if (ddx > 0.01f || ddy > 0.01f) sawDiffersFromPerCorner++;
				}
			}
		}
	}
	CHECK(sawNonZero > 0, "a nonzero angle must actually move the anchor (else the test is vacuous)\n");
	CHECK(sawDiffersFromPerCorner > 0,
	      "the billboard rule must differ from a per-corner projection (else it is not a billboard)\n");
	// The foot anchor is what rides the ground: at any angle, projecting the FOOT and translating
	// puts the foot exactly on the projected ground point. That is the one-line statement of A3.1.
	for (int lvl = 1; lvl < TILT_LEVELS; lvl++) {
		TiltView v;
		tilt_view_init(&v, TILT_ANGLE_DEG[lvl] * TILT_DEG2RAD,
		               (float)PRES_FRAME_W, (float)PRES_FRAME_H, TILT_COVER_MIX);
		float ax = 120.0f, ay = 88.0f, fax, fay, q;
		tilt_project(&v, ax, ay, &fax, &fay, &q);
		float sprX, sprY; presence_art_rect(ax, ay, &sprX, &sprY);
		float dx = fax - ax, dy = fay - ay;
		EQF(sprX + dx + (float)PRES_FOOT_DX, fax, "translated foot x == projected ground x (lvl %d)", lvl);
		EQF(sprY + dy + (float)PRES_FOOT_DY, fay, "translated foot y == projected ground y (lvl %d)", lvl);
	}

	// ---- the two defects the spill box exists to fix (fix pass, review finding 4) ----
	// Both are stated as the reviewer stated them, at the steepest shipped tilt, so a regression
	// reintroduces a named failure rather than a number moving.
	{
		int lvl = TILT_LEVELS - 1;
		TiltView v;
		tilt_view_init(&v, TILT_ANGLE_DEG[lvl] * TILT_DEG2RAD,
		               (float)PRES_FRAME_W, (float)PRES_FRAME_H, TILT_COVER_MIX);
		PresArtDraw d;
		// (a) AMPUTATION. A peer near the left edge, drawn on ground the tilted image spills onto,
		//     had its left columns cut by a boundary the sprite is no longer anywhere near.
		{
			float ax = 4.0f, ay = 152.0f;
			float sprX, sprY; presence_art_rect(ax, ay, &sprX, &sprY);
			CHECK(presence_art_clip(sprX, sprY, 0, 0.0f, &d), "the flat box keeps the left-edge peer\n");
			EQI(d.w < PRES_CELL_W, 1, "...but the flat box AMPUTATES it (that was the bug)");
			CHECK(presence_art_clip(sprX, sprY, 0, spill[lvl], &d), "the spilled box keeps it too\n");
			EQI(d.w, PRES_CELL_W, "...whole: no slice missing from a sprite standing on visible ground");
		}
		// (b) ...and its mirror at the right edge, so the fix is not one-sided.
		{
			float ax = 236.0f, ay = 152.0f;
			float sprX, sprY; presence_art_rect(ax, ay, &sprX, &sprY);
			CHECK(presence_art_clip(sprX, sprY, 0, 0.0f, &d), "the flat box keeps the right-edge peer\n");
			EQI(d.w < PRES_CELL_W, 1, "...but the flat box AMPUTATES it too");
			CHECK(presence_art_clip(sprX, sprY, 0, spill[lvl], &d), "the spilled box keeps it whole\n");
			EQI(d.w, PRES_CELL_W, "...whole");
		}
		// (c) What the spill box must NOT do: rescue a peer the DATA half already culled. The
		//     horizontal map at any row is x -> 120 + s*(x-120) with s > 0, so a flat x outside
		//     [0,240] lands outside that row's projected extent exactly — a peer whose flat rect
		//     misses the frame is not standing on the tilted image either, and presence_solve
		//     (which knows nothing about tilt, D5.7.1) is the right authority for that call.
		{
			PeerPresence self = rec_ok(10, 10, 0, 0), peer = rec_ok(10 + 9, 10, 0, 0);
			PresenceState ps; PresenceOut o;
			EQI(solve_once(&ps, &self, &peer, 1, 0, 0, &o), 0, "a peer past the frame edge is CULLED");
			EQI(o.reason, PRES_OFF_CULL, "...by the data half, before any tilt geometry exists");
		}
		// The box is a SPILL, never a shrink, and garbage never becomes a box: a negative or absurd
		// margin degrades to the flat box rather than to a computed-from-garbage one.
		{
			float sprX, sprY; presence_art_rect(120.0f, 88.0f, &sprX, &sprY);
			PresArtDraw a, b, c;
			CHECK(presence_art_clip(sprX, sprY, 0,  0.0f, &a), "centre, flat box\n");
			CHECK(presence_art_clip(sprX, sprY, 0, -5.0f, &b), "centre, negative margin\n");
			CHECK(presence_art_clip(sprX, sprY, 0, 1.0e9f, &c), "centre, absurd margin\n");
			EQI(b.w, a.w, "a negative margin degrades to the flat box");
			EQI(c.w, a.w, "an absurd margin degrades to the flat box");
		}
	}
}

// ============================================================================================
// TEST 30 — placeholder art invariants (SPEC-avatar A1.5, A7.1 P10)
// ============================================================================================
static uint32_t art_px(const uint8_t* sheet, int x, int y) {
	const uint8_t* p = sheet + ((size_t)y * PRES_SHEET_DIM + (size_t)x) * 4u;
	// presence_art_build writes GPU_RGBA8 byte order (A,B,G,R ascending); read it back as
	// 0xRRGGBBAA, which is the form the colour constants are written in.
	return ((uint32_t)p[3] << 24) | ((uint32_t)p[2] << 16) | ((uint32_t)p[1] << 8) | (uint32_t)p[0];
}

static void test_art_build(void) {
	printf("TEST 30: the placeholder sheet — bounds, coverage, and the two magenta TELLS\n");
	#define GUARD 64
	static uint8_t buf[PRES_SHEET_BYTES + GUARD];
	memset(buf, 0xA5, sizeof buf);
	presence_art_build(buf, 0x3C78C8FFu, 0x201828FFu);
	int guardBad = 0;
	for (int i = 0; i < GUARD; i++) if (buf[PRES_SHEET_BYTES + i] != 0xA5) guardBad++;
	EQI(guardBad, 0, "presence_art_build writes EXACTLY 128*128*4 bytes and not one more");
	presence_art_build(NULL, 0, 0);   // must not crash

	// Everything outside the two 48x96 variant blocks is fully transparent (A1.3).
	int strayOutside = 0;
	for (int y = 0; y < PRES_SHEET_DIM; y++)
		for (int x = 0; x < PRES_SHEET_DIM; x++) {
			int v0 = (x < PRES_VAR_W && y < PRES_VAR_H);
			int v1 = (x >= PRES_VAR_STRIDE && x < PRES_VAR_STRIDE + PRES_VAR_W && y < PRES_VAR_H);
			if (!v0 && !v1 && art_px(buf, x, y) != 0u) strayOutside++;
		}
	EQI(strayOutside, 0, "nothing is drawn outside the two variant blocks");

	// Every one of the 18 cells is non-empty, carries BOTH tells, and differs from its neighbours.
	for (int var = 0; var < 2; var++) {
		for (int d = PRES_DIR_SOUTH; d <= PRES_DIR_WEST; d++) {
			for (int p = 0; p < PRES_POSE_COUNT; p++) {
				PresArtCell c; presence_art_cell(var, d, p, &c);
				int opaque = 0, magenta = 0;
				for (int y = 0; y < PRES_CELL_H; y++)
					for (int x = 0; x < PRES_CELL_W; x++) {
						uint32_t px = art_px(buf, c.x + x, c.y + y);
						if (px) opaque++;
						if (px == PRES_ART_MAGENTA) magenta++;
					}
				CHECK(opaque > 40, "cell (var%d dir%d pose%d) is drawn (%d opaque px)\n", var, d, p, opaque);
				// TELL 1 + TELL 2 both produce magenta; the checker alone contributes 8 px, and the
				// silhouette outline contributes many more. A cell with only the checker's 8 would
				// mean the outline was quietly dropped.
				CHECK(magenta > 20, "cell (var%d dir%d pose%d) carries the magenta TELLS (%d px)\n",
				      var, d, p, magenta);
				// TELL 2 exactly: a 2x2 magenta/black checker in the cell's top-left 4x4 (A1.5.2).
				for (int y = 0; y < 4; y++)
					for (int x = 0; x < 4; x++) {
						uint32_t want = (((x >> 1) + (y >> 1)) & 1) ? PRES_ART_BLACK : PRES_ART_MAGENTA;
						EQI(art_px(buf, c.x + x, c.y + y), want,
						    "checker px (%d,%d) of cell (var%d dir%d pose%d)", x, y, var, d, p);
					}
			}
		}
	}
	// The three poses of one facing must actually DIFFER, or the walk cycle is invisible.
	for (int var = 0; var < 2; var++) {
		for (int d = PRES_DIR_SOUTH; d <= PRES_DIR_WEST; d++) {
			for (int p = 1; p < PRES_POSE_COUNT; p++) {
				PresArtCell a, b;
				presence_art_cell(var, d, 0, &a);
				presence_art_cell(var, d, p, &b);
				int diff = 0;
				for (int y = 0; y < PRES_CELL_H; y++)
					for (int x = 0; x < PRES_CELL_W; x++)
						if (art_px(buf, a.x + x, a.y + y) != art_px(buf, b.x + x, b.y + y)) diff++;
				CHECK(diff > 0, "pose %d differs from STAND (var%d dir%d)\n", p, var, d);
			}
		}
	}
	// The three facings must differ (the facing pip moves), or H3 cannot be judged from a photo.
	for (int var = 0; var < 2; var++) {
		PresArtCell s, n, w;
		presence_art_cell(var, PRES_DIR_SOUTH, 0, &s);
		presence_art_cell(var, PRES_DIR_NORTH, 0, &n);
		presence_art_cell(var, PRES_DIR_WEST,  0, &w);
		int dsn = 0, dsw = 0;
		for (int y = 0; y < PRES_CELL_H; y++)
			for (int x = 0; x < PRES_CELL_W; x++) {
				if (art_px(buf, s.x + x, s.y + y) != art_px(buf, n.x + x, n.y + y)) dsn++;
				if (art_px(buf, s.x + x, s.y + y) != art_px(buf, w.x + x, w.y + y)) dsw++;
			}
		CHECK(dsn > 0, "SOUTH and NORTH are distinguishable (var %d)\n", var);
		CHECK(dsw > 0, "SOUTH and WEST are distinguishable (var %d)\n", var);
	}
	// The two gender variants must differ — the placeholder carries a COLOUR tell and a SHAPE tell,
	// and the shape tell is what survives a monochrome photo and colour-blind eyes.
	{
		PresArtCell m, f;
		presence_art_cell(0, PRES_DIR_SOUTH, 0, &m);
		presence_art_cell(1, PRES_DIR_SOUTH, 0, &f);
		int diffAny = 0, diffAlpha = 0;
		for (int y = 0; y < PRES_CELL_H; y++)
			for (int x = 0; x < PRES_CELL_W; x++) {
				uint32_t a = art_px(buf, m.x + x, m.y + y), b = art_px(buf, f.x + x, f.y + y);
				if (a != b) diffAny++;
				if ((a != 0u) != (b != 0u)) diffAlpha++;
			}
		CHECK(diffAny > 0,   "the two variants are not the same pixels\n");
		CHECK(diffAlpha > 0, "the variants differ in SHAPE, not only in colour\n");
	}
	// Byte order: the magenta tell must be the literal A1.5.2 word, and the bytes must be the
	// GPU_RGBA8 order (A,B,G,R) the display transfer hands to the sampler untouched.
	{
		PresArtCell c; presence_art_cell(0, PRES_DIR_SOUTH, 0, &c);
		const uint8_t* p = buf + ((size_t)c.y * PRES_SHEET_DIM + (size_t)c.x) * 4u;
		EQI(p[0], 0xFF, "magenta byte 0 = A");
		EQI(p[1], 0xFF, "magenta byte 1 = B");
		EQI(p[2], 0x00, "magenta byte 2 = G");
		EQI(p[3], 0xFF, "magenta byte 3 = R");
	}
	#undef GUARD
}

// ============================================================================================
// TEST 31 — walk-frame selection (SPEC-avatar A2.6.3/A2.6.4, A7.1 P9)
// ============================================================================================
static void test_art_walk(void) {
	printf("TEST 31: the walk cycle — travel-driven, idle-resetting, discontinuity-proof\n");
	EQI(PRES_WALK_CYCLE[0], PRES_POSE_STEP_A, "cycle beat 0");
	EQI(PRES_WALK_CYCLE[1], PRES_POSE_STAND,  "cycle beat 1");
	EQI(PRES_WALK_CYCLE[2], PRES_POSE_STEP_B, "cycle beat 2");
	EQI(PRES_WALK_CYCLE[3], PRES_POSE_STAND,  "cycle beat 3");

	// The peer's world x, read back out of a record, is monotone at exactly the movement speed and
	// does NOT hitch at the tile boundary — the same property TEST 3 proves for the anchor, checked
	// here on the channel the walk cycle is actually driven by.
	for (int speed = 1; speed <= 4; speed *= 2) {
		int n = 16 / speed, prev = 0;
		for (int k = 0; k <= n; k++) {
			int tile = (k == 0) ? 0 : 1;
			int ph   = (k == 0 || k == n) ? 0 : k * speed;
			PeerPresence r = rec_ok(10 + tile, 20, ph, 0);
			int wx, wy; presence_art_world_px(&r, &wx, &wy);
			EQI(wy, 16 * 20, "world y is untouched by an x walk (s=%d k=%d)", speed, k);
			if (k == 0) EQI(wx, 160, "world x starts at 16*10 (s=%d)", speed);
			else        EQI(wx - prev, speed, "world x advances by exactly the speed (s=%d k=%d)", speed, k);
			prev = wx;
		}
		EQI(prev, 176, "one whole tile of world travel (s=%d)", speed);
	}
	// No camera field (PRES_F_CAM clear) => the sub-tile term degrades to 0 and travel jumps a whole
	// tile at once. A2.6.3 requires that to read as a stiff step rather than a freeze, i.e. the pose
	// must still advance.
	{
		PresWalk w; presence_walk_reset(&w);
		PeerPresence a = rec_ok(10, 20, 0, 0); a.flags &= ~(unsigned)PRES_F_CAM; a.round = 1;
		PeerPresence b = rec_ok(11, 20, 0, 0); b.flags &= ~(unsigned)PRES_F_CAM; b.round = 2;
		int wx, wy; presence_art_world_px(&a, &wx, &wy);
		EQI(wx, 160, "no camera: world x is whole tiles");
		presence_walk_step(&w, &a);
		int p = presence_walk_step(&w, &b);
		EQI(w.travel, 16, "a whole tile lands in the accumulator at once");
		EQI(p, PRES_WALK_CYCLE[(16 / PRES_WALK_BEAT_PX) & 3], "...and the pose advances a full cycle");
	}

	// A 64-frame walk at 1 px/frame: the pose must follow the 4-beat cycle in order.
	{
		PresWalk w; presence_walk_reset(&w);
		PeerPresence r = rec_ok(10, 20, 0, 0);
		r.round = 1;
		EQI(presence_walk_step(&w, &r), PRES_POSE_STAND, "the first sample is always STAND");
		int travel = 0;
		for (int k = 1; k <= 64; k++) {
			// walk right one px per frame: tile += (k+15)/16 whole tiles, phase = k % 16
			int tile = (k + 15) / 16;
			int ph   = k % 16;
			PeerPresence s = rec_ok(10 + tile, 20, ph, 0);
			s.round = (uint32_t)(k + 1);
			int pose = presence_walk_step(&w, &s);
			travel++;
			EQI(w.travel, travel, "accumulator == travelled px at frame %d", k);
			EQI(pose, PRES_WALK_CYCLE[(travel / PRES_WALK_BEAT_PX) & 3], "pose at frame %d", k);
		}
		// One 16 px tile step plays exactly one full 4-beat cycle (the cadence claim in A2.6.3).
		int seen[PRES_POSE_COUNT] = { 0, 0, 0 };
		for (int t = 1; t <= 16; t++) seen[PRES_WALK_CYCLE[(t / PRES_WALK_BEAT_PX) & 3]]++;
		CHECK(seen[PRES_POSE_STAND] > 0 && seen[PRES_POSE_STEP_A] > 0 && seen[PRES_POSE_STEP_B] > 0,
		      "one tile step shows all three poses (%d/%d/%d)\n",
		      seen[PRES_POSE_STAND], seen[PRES_POSE_STEP_A], seen[PRES_POSE_STEP_B]);
	}
	// Standing still: STAND after exactly PRES_IDLE_FRAMES, and the accumulator is cleared so the
	// next step restarts the cycle from the top instead of resuming mid-stride.
	{
		PresWalk w; presence_walk_reset(&w);
		PeerPresence r = rec_ok(10, 20, 0, 0); r.round = 1;
		presence_walk_step(&w, &r);
		for (int k = 1; k <= 6; k++) {   // move 6 px so the pose is NOT already STAND
			PeerPresence s = rec_ok(10, 20, k, 0); s.round = (uint32_t)(k + 1); s.px = 11;
			presence_walk_step(&w, &s);
		}
		PeerPresence hold = rec_ok(11, 20, 6, 0);
		int pose = 0;
		for (uint32_t k = 0; k < PRES_IDLE_FRAMES; k++) {
			hold.round = 8u + k;
			pose = presence_walk_step(&w, &hold);
			if (k + 1 < PRES_IDLE_FRAMES)
				EQI(w.idle, (int)(k + 1), "idle counter at %u", (unsigned)k);
		}
		EQI(pose, PRES_POSE_STAND, "STAND after PRES_IDLE_FRAMES of no travel");
		EQI(w.travel, 0, "...and the accumulator is cleared");
	}
	// Discontinuities: map change, a rewound producer round, a stale gap, and a teleport all restart
	// the cycle at STAND (A2.6.4).
	{
		PresWalk w; presence_walk_reset(&w);
		PeerPresence a = rec_ok(10, 20, 0, 0); a.round = 100;
		presence_walk_step(&w, &a);
		PeerPresence b = rec_ok(10, 20, 4, 0); b.round = 101; b.px = 11;
		presence_walk_step(&w, &b);
		CHECK(w.travel > 0, "the accumulator is primed\n");

		PeerPresence mc = b; mc.mapNum = MAPN + 1; mc.round = 102;
		EQI(presence_walk_step(&w, &mc), PRES_POSE_STAND, "a map change restarts at STAND");
		EQI(w.travel, 0, "...with a cleared accumulator");

		presence_walk_reset(&w);
		presence_walk_step(&w, &a);
		presence_walk_step(&w, &b);
		PeerPresence rw = b; rw.round = 1;
		EQI(presence_walk_step(&w, &rw), PRES_POSE_STAND, "a rewound producer round restarts");

		presence_walk_reset(&w);
		presence_walk_step(&w, &a);
		presence_walk_step(&w, &b);
		PeerPresence st = b; st.round = 101u + PRES_STALE_FRAMES + 1u;
		EQI(presence_walk_step(&w, &st), PRES_POSE_STAND, "a stale gap restarts");

		presence_walk_reset(&w);
		presence_walk_step(&w, &a);
		presence_walk_step(&w, &b);
		PeerPresence tp = b; tp.round = 102; tp.px = (int16_t)(b.px + 5);   // 80 world px in a frame
		EQI(presence_walk_step(&w, &tp), PRES_POSE_STAND, "a teleport restarts, it does not spin the cycle");
		EQI(w.travel, 0, "...with a cleared accumulator");
		// A legitimate bike frame (4 px) is NOT a discontinuity.
		presence_walk_reset(&w);
		presence_walk_step(&w, &a);
		PeerPresence bike = rec_ok(11, 20, 12, 0); bike.round = 101;   // world 176 - 4 = 172, +12 px
		presence_walk_step(&w, &bike);
		EQI(w.travel, 12, "a 12 px frame is accumulated, not rejected");
	}
	// Defensive: NULL inputs never crash and always read as STAND.
	{
		PresWalk w; presence_walk_reset(&w);
		EQI(presence_walk_step(NULL, NULL), PRES_POSE_STAND, "NULL state -> STAND");
		EQI(presence_walk_step(&w, NULL),   PRES_POSE_STAND, "NULL record -> STAND + reset");
		EQI(w.have, 0, "...and the state is reset");
		presence_walk_reset(NULL);
		int wx = 7, wy = 7;
		presence_art_world_px(NULL, &wx, &wy);
		EQI(wx, 0, "world px of a NULL record is 0, not garbage");
		EQI(wy, 0, "world py of a NULL record is 0, not garbage");
	}
	// Phase preservation across the 0xFFFF accumulator wrap: the wrap point is a multiple of the
	// 16 px cycle, so a beat is never skipped (a plain clamp would skip one every few hours).
	{
		PresWalk w; presence_walk_reset(&w);
		for (int t = 0; t < 16; t++)
			EQI(PRES_WALK_CYCLE[((0x10000 + t) / PRES_WALK_BEAT_PX) & 3],
			    PRES_WALK_CYCLE[(t / PRES_WALK_BEAT_PX) & 3], "wrap preserves the phase at +%d", t);
		(void)w;
	}
}

// ============================================================================================
// TEST 32 — the y-sort comparator (SPEC-avatar A3.4, A7.1 P6)
// ============================================================================================
static void test_art_ysort(void) {
	printf("TEST 32: billboards sort back-to-front by foot y, ties broken deterministically\n");
	// A one-element sort is a proven no-op, which is what PRES_MAX_PEERS == 1 makes it today.
	PresBillboard one = { 42.0f, 7, 0 };
	presence_art_ysort(&one, 1);
	EQF(one.fy, 42.0f, "a 1-element sort is a no-op");
	presence_art_ysort(NULL, 3);   // must not crash
	presence_art_ysort(&one, 0);

	// Strict weak ordering + antisymmetry over a spread of values.
	{
		const float ys[4] = { 10.0f, 10.0f, 20.0f, -5.0f };
		const uint16_t ts[3] = { 1, 2, 2 };
		for (int i = 0; i < 4; i++) for (int ti = 0; ti < 3; ti++)
		for (int j = 0; j < 4; j++) for (int tj = 0; tj < 3; tj++) {
			PresBillboard a = { ys[i], ts[ti], (uint8_t)ti };
			PresBillboard b = { ys[j], ts[tj], (uint8_t)tj };
			int ab = presence_art_ycmp(&a, &b), ba = presence_art_ycmp(&b, &a);
			CHECK((ab < 0 && ba > 0) || (ab > 0 && ba < 0) || (ab == 0 && ba == 0),
			      "ycmp must be antisymmetric (%g/%u/%u vs %g/%u/%u -> %d,%d)\n",
			      (double)a.fy, a.tid, a.slot, (double)b.fy, b.tid, b.slot, ab, ba);
			if (i == j && ti == tj) EQI(ab, 0, "a comparator is irreflexive-equal with itself");
		}
	}
	EQI(presence_art_ycmp(NULL, NULL), 0, "NULLs compare equal rather than crashing");

	// Every permutation of three distinct billboards sorts to the same ascending-foot-y order, and
	// an equal-y pair is ordered by tid — a flickering tie-break reads as z-fighting.
	{
		const PresBillboard src[3] = { { 100.0f, 9, 0 }, { 40.0f, 3, 1 }, { 100.0f, 4, 2 } };
		const int perm[6][3] = { {0,1,2},{0,2,1},{1,0,2},{1,2,0},{2,0,1},{2,1,0} };
		for (int p = 0; p < 6; p++) {
			PresBillboard b[3];
			for (int i = 0; i < 3; i++) b[i] = src[perm[p][i]];
			presence_art_ysort(b, 3);
			EQF(b[0].fy, 40.0f,  "permutation %d: the far row is first", p);
			EQF(b[1].fy, 100.0f, "permutation %d: then the near row", p);
			EQF(b[2].fy, 100.0f, "permutation %d: ...both of it", p);
			EQI(b[1].tid, 4, "permutation %d: the equal-y pair is ordered by tid", p);
			EQI(b[2].tid, 9, "permutation %d: ...deterministically", p);
			for (int i = 1; i < 3; i++)
				CHECK(presence_art_ycmp(&b[i - 1], &b[i]) <= 0,
				      "permutation %d: the result is sorted at %d\n", p, i);
		}
	}
}

// ============================================================================================
// SLICE M3 — identity + interaction (SPEC-avatar A4/A5/A6): TEST 33-37.
// ============================================================================================

// --------------------------------------------------------------------------------------------
// TEST 33 — the charmap decoder (SPEC-avatar A4.1/A4.2, A7.1 P7).
//
// The point of this test is that nothing about gbatext.c is taken on trust: the ONE row with an
// in-repo witness is asserted against that witness, every other row is asserted against the
// INDEPENDENTLY transcribed decoder presence.c already shipped (SPEC-data D6.3.1), the two
// decoders' disagreements are enumerated EXACTLY rather than tolerated, and the memory-safety
// properties are swept over all 256 byte values and every cap.
// --------------------------------------------------------------------------------------------
static void test_gbatext(void) {
	printf("TEST 33: charmap decode — the in-repo \"Nils\" anchor, the cross-check with\n"
	       "         presence_name_ascii over all 256 bytes, and cap/index safety\n");

	// -- (a) THE ANCHOR (A4.2.1). celiolink_payloads.h:98-100 / :130-133 ship Celio's canned demo
	// identity as these exact bytes, and its demo trainer name is "Nils". One four-character round
	// trip pins BOTH letter ranges and the terminator simultaneously — it is the only part of the
	// table with an in-repo witness, so it is asserted first and by itself.
	{
		const uint8_t nils[8] = { 0xC8, 0xDD, 0xE0, 0xE7, 0xFF, 0xFF, 0xFF, 0xFF };
		char out[32];
		int  n = gbatext_decode(nils, 8, out, (int)sizeof out);
		CHECK(strcmp(out, "Nils") == 0, "the in-repo Celio anchor decodes to \"Nils\", got \"%s\"\n", out);
		EQI(n, 4, "...and returns its byte length");
		EQI(n, (int)strlen(out), "...which is strlen (the documented return-value deviation)");
	}

	// -- (b) the letter/digit ranges, end to end.
	{
		char out[32];
		uint8_t up[8], lo[8], dg[8];
		for (int i = 0; i < 8; i++) { up[i] = (uint8_t)(0xBB + i); lo[i] = (uint8_t)(0xD5 + i); }
		for (int i = 0; i < 8; i++) dg[i] = (uint8_t)(0xA1 + i);
		gbatext_decode(up, 8, out, (int)sizeof out); CHECK(strcmp(out, "ABCDEFGH") == 0, "A..H: %s\n", out);
		gbatext_decode(lo, 8, out, (int)sizeof out); CHECK(strcmp(out, "abcdefgh") == 0, "a..h: %s\n", out);
		gbatext_decode(dg, 8, out, (int)sizeof out); CHECK(strcmp(out, "01234567") == 0, "0..7: %s\n", out);
		// The far ends of both ranges, so an off-by-one at either boundary shows.
		EQI(gbatext_glyph(0xBB)[0], 'A', "0xBB is 'A'");
		EQI(gbatext_glyph(0xD4)[0], 'Z', "0xD4 is 'Z'");
		EQI(gbatext_glyph(0xD5)[0], 'a', "0xD5 is 'a'");
		EQI(gbatext_glyph(0xEE)[0], 'z', "0xEE is 'z'");
		EQI(gbatext_glyph(0xA1)[0], '0', "0xA1 is '0'");
		EQI(gbatext_glyph(0xAA)[0], '9', "0xAA is '9'");
		CHECK(gbatext_glyph(0xEF)[0] == '?', "0xEF is one past 'z' and must NOT decode\n");
		CHECK(gbatext_glyph(0xBA)[0] == '/', "0xBA is one before 'A' and is the slash row\n");
	}

	// -- (c) THE TABLE IS COMPLETE. A [256] array with too few initialisers zero-fills the tail,
	// and a NULL entry means "terminator" — i.e. an under-supplied table would silently truncate
	// every name at the first high byte. Exactly one entry may be NULL, and it must be 0xFF.
	{
		int nulls = 0, nullIdx = -1;
		for (int b = 0; b < 256; b++)
			if (!gbatext_glyph((uint8_t)b)) { nulls++; nullIdx = b; }
		EQI(nulls, 1, "exactly one table entry is the terminator");
		EQI(nullIdx, 0xFF, "...and it is 0xFF");
		for (int b = 0; b < 256; b++) {
			if (b == 0xFF) continue;
			const char* g = gbatext_glyph((uint8_t)b);
			size_t gl = strlen(g);
			CHECK(gl >= 1 && gl <= 3, "glyph 0x%02X has length %u (want 1..3)\n", b, (unsigned)gl);
		}
	}

	// -- (d) THE CROSS-CHECK (the whole reason two decoders are allowed to coexist). For every one
	// of the 256 byte values, gbatext and presence_name_ascii must agree — except on the three rows
	// gbatext deliberately adds, which are enumerated here so a fourth cannot appear unnoticed.
	{
		for (int b = 0; b < 256; b++) {
			if (b == 0xFF) continue;
			uint8_t src[8]; memset(src, 0xFF, sizeof src); src[0] = (uint8_t)b;
			char a[9], u[32];
			presence_name_ascii(src, a);
			gbatext_decode(src, 8, u, (int)sizeof u);
			if (b == 0xB4 || b == 0xB5 || b == 0xB6) {
				// The three A4.2 rows SPEC-data D6.3.1 does not carry: the apostrophe and the two
				// gendered signs. presence_name_ascii renders them '?', which is its documented
				// "cosmetic, never unsafe" behaviour; gbatext renders them for real.
				CHECK(strcmp(a, "?") == 0, "0x%02X is '?' through the ASCII decoder, got \"%s\"\n", b, a);
				CHECK(strcmp(u, "?") != 0, "0x%02X must decode for real through gbatext\n", b);
			} else {
				CHECK(strcmp(a, u) == 0,
				      "byte 0x%02X: ASCII decoder says \"%s\", gbatext says \"%s\"\n", b, a, u);
			}
		}
		CHECK(strcmp(gbatext_glyph(0xB5), "\xE2\x99\x82") == 0, "0xB5 is U+2642 MALE SIGN\n");
		CHECK(strcmp(gbatext_glyph(0xB6), "\xE2\x99\x80") == 0, "0xB6 is U+2640 FEMALE SIGN\n");
		CHECK(strcmp(gbatext_glyph(0xB4), "'") == 0, "0xB4 is the apostrophe\n");
		CHECK(strcmp(gbatext_glyph(0x00), " ") == 0, "0x00 is a space\n");
	}

	// -- (e) the terminator ends the string EARLY, and an unterminated 8-byte name still ends.
	{
		char out[32];
		const uint8_t mid[8] = { 0xBB, 0xBC, 0xFF, 0xBD, 0xBE, 0xBF, 0xC0, 0xC1 };
		EQI(gbatext_decode(mid, 8, out, (int)sizeof out), 2, "0xFF stops the decode");
		CHECK(strcmp(out, "AB") == 0, "...at exactly the terminator, got \"%s\"\n", out);
		uint8_t full[8]; for (int i = 0; i < 8; i++) full[i] = (uint8_t)(0xBB + i);
		EQI(gbatext_decode(full, 8, out, (int)sizeof out), 8, "an UNTERMINATED 8-byte name decodes whole");
		CHECK(out[8] == '\0', "...and is still NUL-terminated\n");
		const uint8_t empty[8] = { 0xFF, 0, 0, 0, 0, 0, 0, 0 };
		EQI(gbatext_decode(empty, 8, out, (int)sizeof out), 0, "an EMPTY name decodes to nothing");
		CHECK(out[0] == '\0', "...and is an empty C string, not garbage\n");
	}

	// -- (f) MEMORY SAFETY, swept. No write past cap for ANY cap; always NUL-terminated when cap
	// >= 1; a multi-byte glyph is never truncated mid-sequence (the byte after the NUL-terminated
	// text must be untouched, and the text must never end inside a UTF-8 continuation).
	{
		uint8_t worst[8]; for (int i = 0; i < 8; i++) worst[i] = 0xB5;   // eight 3-byte glyphs
		for (int cap = 0; cap <= 32; cap++) {
			char buf[64];
			memset(buf, 0x7F, sizeof buf);
			int n = gbatext_decode(worst, 8, buf, cap);
			if (cap == 0) { EQI(n, 0, "cap 0 writes nothing"); CHECK((unsigned char)buf[0] == 0x7F,
			                    "cap 0 must not touch the buffer\n"); continue; }
			CHECK(n >= 0 && n < cap, "cap %d: wrote %d bytes (must be < cap)\n", cap, n);
			EQI(n, (int)strlen(buf), "cap %d: the return value is strlen", cap);
			CHECK(buf[n] == '\0', "cap %d: NUL-terminated\n", cap);
			for (int k = n + 1; k < (int)sizeof buf; k++)
				CHECK((unsigned char)buf[k] == 0x7F, "cap %d: byte %d past the NUL was written\n", cap, k);
			EQI(n % 3, 0, "cap %d: never half a 3-byte glyph", cap);
		}
		// Degenerate arguments are refused rather than dereferenced.
		char b2[8]; memset(b2, 0x7F, sizeof b2);
		EQI(gbatext_decode(NULL, 8, b2, (int)sizeof b2), 0, "NULL src writes nothing");
		CHECK(b2[0] == '\0', "...but still terminates the output\n");
		EQI(gbatext_decode(worst, 0, b2, (int)sizeof b2), 0, "n == 0 writes nothing");
		EQI(gbatext_decode(worst, -1, b2, (int)sizeof b2), 0, "n < 0 writes nothing");
		EQI(gbatext_decode(worst, 8, NULL, 8), 0, "NULL out is refused");
		EQI(gbatext_decode(worst, 8, b2, -1), 0, "cap < 0 is refused");
	}
}

// --------------------------------------------------------------------------------------------
// TEST 34 — adjacency + facing (SPEC-avatar A5.1/A5.2/A5.2.1, A7.1 P8).
//
// Exhaustive over the tile delta x both raw facing nibbles, against an INDEPENDENT reference
// formulation of the rule (written from the spec's prose, not from presence_ui.c), so a
// transcription slip in either shows up as a mismatch rather than as agreement.
// --------------------------------------------------------------------------------------------

// The reference: a positive statement of A5.1 + A5.2 + A5.2.1, deliberately shaped differently
// from the implementation (a table of the four legal (dx,dy,hostDir,peerDir) tuples).
static int meet_ref(int dx, int dy, int hostRaw, int peerRaw, int hostFold, int peerFold) {
	static const int T[4][4] = {   // dx, dy, host must face, peer must face
		{  1,  0, PRES_DIR_EAST,  PRES_DIR_WEST  },
		{ -1,  0, PRES_DIR_WEST,  PRES_DIR_EAST  },
		{  0,  1, PRES_DIR_SOUTH, PRES_DIR_NORTH },
		{  0, -1, PRES_DIR_NORTH, PRES_DIR_SOUTH },
	};
	int row = -1;
	for (int i = 0; i < 4; i++) if (T[i][0] == dx && T[i][1] == dy) row = i;
	if (row < 0) return 0;                                        // not orthogonally adjacent
	int usable = (hostRaw >= 1 && hostRaw <= 8) && (peerRaw >= 1 && peerRaw <= 8);
	if (!usable) return 1;                                        // A5.2.1: adjacency-only
	return hostFold == T[row][2] && peerFold == T[row][3];
}

static void test_meet(void) {
	printf("TEST 34: adjacency + facing — exhaustive over the tile delta x both facing nibbles\n");

	// -- (a) the exhaustive sweep. dx,dy in [-2,2] (so "two apart" and "same tile" are both covered)
	// x host raw nibble x peer raw nibble over -1..9, which spans unavailable, DIR_NONE, the four
	// cardinals, the four diagonals and one garbage value.
	{
		int sawMeet = 0, sawDegraded = 0, sawAdjacentNoFace = 0;
		for (int dx = -2; dx <= 2; dx++)
		for (int dy = -2; dy <= 2; dy++)
		for (int hr = -1; hr <= 9; hr++)
		for (int pr = -1; pr <= 9; pr++) {
			PeerPresence h = rec_ok(20, 20, 0, 0);
			PeerPresence p = rec_ok(20 + dx, 20 + dy, 0, 0);
			h.facing = (uint8_t)presence_dir(hr < 0 ? PRES_DIR_SOUTH : hr);   // what fill_core does
			p.facing = (uint8_t)presence_dir(pr < 0 ? PRES_DIR_SOUTH : pr);
			PresMeet m;
			presence_meet(&h, hr, &p, pr, &m);

			EQI(m.sameMap, 1, "same map (%d,%d)", dx, dy);
			EQI(m.dx, dx, "dx");
			EQI(m.dy, dy, "dy");
			int adjRef = (dx * dx + dy * dy) == 1;
			EQI(m.adjacent, adjRef, "adjacency at (%d,%d)", dx, dy);
			EQI(m.meet, meet_ref(dx, dy, hr, pr, (int)h.facing, (int)p.facing),
			    "meet at d(%d,%d) raw(%d,%d)", dx, dy, hr, pr);
			// meet can never be true without adjacency — the property the prompt rides on.
			CHECK(!m.meet || m.adjacent, "meet without adjacency at (%d,%d)\n", dx, dy);
			// ...and diagonals are NEVER adjacent (A5.1: Gen-3 characters cannot face diagonally).
			if (dx != 0 && dy != 0) EQI(m.adjacent, 0, "diagonal (%d,%d) is not adjacent", dx, dy);
			if (m.meet) sawMeet = 1;
			if (m.degraded) { sawDegraded = 1; CHECK(m.adjacent, "degraded implies adjacent\n"); }
			if (adjRef && !m.facingOk && !m.degraded) sawAdjacentNoFace = 1;
		}
		// Non-vacuity: the sweep must actually have produced all three interesting outcomes.
		CHECK(sawMeet, "the sweep never produced a meeting\n");
		CHECK(sawDegraded, "the sweep never produced the A5.2.1 degraded case\n");
		CHECK(sawAdjacentNoFace, "the sweep never produced adjacent-but-looking-away\n");
	}

	// -- (b) the four legal meetings, spelled out, so a sign flip in dir_from_delta is named.
	{
		const struct { int dx, dy, hf, pf; const char* what; } C[4] = {
			{  1,  0, PRES_DIR_EAST,  PRES_DIR_WEST,  "peer to the EAST"  },
			{ -1,  0, PRES_DIR_WEST,  PRES_DIR_EAST,  "peer to the WEST"  },
			{  0,  1, PRES_DIR_SOUTH, PRES_DIR_NORTH, "peer to the SOUTH" },
			{  0, -1, PRES_DIR_NORTH, PRES_DIR_SOUTH, "peer to the NORTH" },
		};
		for (int i = 0; i < 4; i++) {
			PeerPresence h = rec_ok(10, 10, 0, 0);
			PeerPresence p = rec_ok(10 + C[i].dx, 10 + C[i].dy, 0, 0);
			h.facing = (uint8_t)C[i].hf; p.facing = (uint8_t)C[i].pf;
			PresMeet m; presence_meet(&h, C[i].hf, &p, C[i].pf, &m);
			EQI(m.dirToPeer, C[i].hf, "%s: dirToPeer", C[i].what);
			EQI(m.facingOk, 1, "%s: facing each other", C[i].what);
			EQI(m.degraded, 0, "%s: not degraded (both nibbles are readings)", C[i].what);
			EQI(m.meet, 1, "%s: meets", C[i].what);
			// ...and turning EITHER one away breaks it.
			PeerPresence p2 = p; p2.facing = (uint8_t)C[i].hf;
			presence_meet(&h, C[i].hf, &p2, C[i].hf, &m);
			EQI(m.meet, 0, "%s: peer looking away does NOT meet", C[i].what);
		}
	}

	// -- (c) A5.2.1's degraded mode, stated as its own property: an UNAVAILABLE facing must make
	// the predicate EAGER (adjacency-only), never dead. This is the difference between a feature
	// that works on FireRed and one that silently never fires there.
	{
		PeerPresence h = rec_ok(10, 10, 0, 0), p = rec_ok(11, 10, 0, 0);
		h.facing = PRES_DIR_SOUTH; p.facing = PRES_DIR_SOUTH;      // both looking the WRONG way
		PresMeet m;
		presence_meet(&h, PRES_DIR_SOUTH, &p, PRES_DIR_SOUTH, &m);
		EQI(m.meet, 0, "readable facings that do not match => no meeting");
		presence_meet(&h, -1, &p, PRES_DIR_SOUTH, &m);
		EQI(m.degraded, 1, "an unavailable HOST nibble degrades");
		EQI(m.meet, 1, "...to adjacency-only (eager, not dead)");
		presence_meet(&h, PRES_DIR_SOUTH, &p, -1, &m);
		EQI(m.degraded, 1, "an unavailable PEER nibble degrades");
		EQI(m.meet, 1, "...to adjacency-only");
		presence_meet(&h, 0, &p, 0, &m);
		EQI(m.degraded, 1, "DIR_NONE is not a reading either");
		presence_meet(&h, 9, &p, 9, &m);
		EQI(m.degraded, 1, "a garbage nibble (9) is not a reading either");
		// But an unusable facing NEVER manufactures adjacency.
		PeerPresence far = rec_ok(14, 10, 0, 0);
		presence_meet(&h, -1, &far, -1, &m);
		EQI(m.meet, 0, "degraded mode does not invent adjacency four tiles away");
	}

	// -- (d) the preconditions: a different map, a different MAP UNIVERSE, or an invalid position
	// must all yield nothing at all — a tile delta across two map tables is a category error, and
	// this is where Emerald's (3,12) meeting FireRed's (3,12) is stopped.
	{
		PeerPresence h = rec_ok(10, 10, 0, 0), p = rec_ok(11, 10, 0, 0);
		h.facing = PRES_DIR_EAST; p.facing = PRES_DIR_WEST;
		PresMeet m;
		presence_meet(&h, PRES_DIR_EAST, &p, PRES_DIR_WEST, &m);
		EQI(m.meet, 1, "the control case meets");

		PeerPresence p2 = p; p2.mapNum = MAPN + 1;
		presence_meet(&h, PRES_DIR_EAST, &p2, PRES_DIR_WEST, &m);
		EQI(m.sameMap, 0, "a different map is not the same map");
		EQI(m.meet, 0, "...and cannot meet");
		EQI(m.dx, 0, "...and does not even report a tile delta (it would be meaningless)");

		PeerPresence p3 = p; p3.gameId = PRES_GAME_KANTO;
		presence_meet(&h, PRES_DIR_EAST, &p3, PRES_DIR_WEST, &m);
		EQI(m.meet, 0, "Emerald's (3,12) is not FireRed's (3,12) — the map-universe gate");

		PeerPresence p4 = p; p4.flags &= (uint8_t)~PRES_F_SB1VALID;
		presence_meet(&h, PRES_DIR_EAST, &p4, PRES_DIR_WEST, &m);
		EQI(m.meet, 0, "a peer with no valid SaveBlock1 has no position to be adjacent to");
		PeerPresence h4 = h; h4.flags &= (uint8_t)~PRES_F_SB1VALID;
		presence_meet(&h4, PRES_DIR_EAST, &p, PRES_DIR_WEST, &m);
		EQI(m.meet, 0, "...and neither does a host without one");

		// NULLs are safe and produce the all-zero (no meeting) verdict.
		memset(&m, 0x5A, sizeof m);
		presence_meet(NULL, 1, &p, 1, &m);
		EQI(m.meet, 0, "NULL host is safe");
		EQI(m.sameMap, 0, "...and clears the struct");
		presence_meet(&h, 1, NULL, 1, &m);
		EQI(m.meet, 0, "NULL peer is safe");
		presence_meet(&h, 1, &p, 1, NULL);      // must not crash
		CHECK(1, "NULL out is safe\n");
	}
}

// --------------------------------------------------------------------------------------------
// TEST 35 — the card state machine + the surface policy (SPEC-avatar A5.4, A4.4.2, O-A4).
// --------------------------------------------------------------------------------------------
static PresCardIn card_in(int enabled, int drawn, int meet, int menuOpen, int a, int b) {
	PresCardIn in; in.enabled = enabled; in.drawn = drawn; in.meet = meet;
	in.menuOpen = menuOpen; in.hudOn = 1; in.aEdge = a; in.bEdge = b;
	return in;
}

static void test_card_fsm(void) {
	printf("TEST 35: the card is one bool — every open, every close, and no other state\n");

	// -- (a) A opens it, and ONLY when the meeting predicate and the draw gate both hold.
	{
		PresCard c; presence_card_reset(&c);
		EQI(c.open, 0, "a fresh card is closed");
		PresCardIn in = card_in(1, 1, 0, 0, 1, 0);          // A pressed, but not meeting
		EQI(presence_card_step(&c, &in), 0, "A alone does not open the card");
		in = card_in(1, 0, 1, 0, 1, 0);                     // meeting, but nothing drawn
		EQI(presence_card_step(&c, &in), 0, "A with no avatar on screen does not open it");
		in = card_in(1, 1, 1, 0, 1, 0);
		EQI(presence_card_step(&c, &in), 1, "A while meeting AND drawing opens it");
		// It STAYS open across frames with no edge at all — the one bool is the whole state.
		for (int f = 0; f < 10; f++) {
			in = card_in(1, 1, 1, 0, 0, 0);
			EQI(presence_card_step(&c, &in), 1, "frame %d: it stays open", f);
		}
	}

	// -- (b) every close condition A5.4.2/A5.4.4 lists, each from a fresh open.
	//                              enabled drawn meet menuOpen hudOn aEdge bEdge
	{
		const struct { const char* what; PresCardIn in; } CL[7] = {
			{ "another A edge",        { 1, 1, 1, 0, 1, 1, 0 } },
			{ "a B edge",              { 1, 1, 1, 0, 1, 0, 1 } },
			{ "the meeting ending",    { 1, 1, 0, 0, 1, 0, 0 } },
			{ "the avatar leaving",    { 1, 0, 1, 0, 1, 0, 0 } },
			{ "the pause menu",        { 1, 1, 1, 1, 1, 0, 0 } },
			{ "the pref going off",    { 0, 1, 1, 0, 1, 0, 0 } },
			{ "the HUD being hidden",  { 1, 1, 1, 0, 0, 0, 0 } },
		};
		for (int i = 0; i < 7; i++) {
			PresCard c; presence_card_reset(&c);
			PresCardIn open = card_in(1, 1, 1, 0, 1, 0);
			EQI(presence_card_step(&c, &open), 1, "%s: opened first", CL[i].what);
			PresCardIn cl = CL[i].in;
			EQI(presence_card_step(&c, &cl), 0, "%s closes the card", CL[i].what);
		}
	}

	// -- (c) an involuntary close WINS over the A that would otherwise open it, so a card can never
	// exist for a frame in which its own precondition is false.
	{
		PresCard c; presence_card_reset(&c);
		PresCardIn in = card_in(1, 1, 1, 1, 1, 0);          // A pressed WHILE the menu is up
		EQI(presence_card_step(&c, &in), 0, "the menu wins over the A edge");
		in = card_in(0, 1, 1, 0, 1, 0);                     // ...and so does the pref
		EQI(presence_card_step(&c, &in), 0, "the pref wins over the A edge");
	}

	// -- (d) NULLs, and the invariant that `open` is never anything but 0/1.
	{
		PresCard c; presence_card_reset(&c);
		EQI(presence_card_step(&c, NULL), 0, "a NULL input closes rather than crashes");
		EQI(presence_card_step(NULL, NULL), 0, "a NULL card is safe");
		PresCardIn in = card_in(1, 1, 1, 0, 1, 0);
		presence_card_step(&c, &in);
		CHECK(c.open == 0 || c.open == 1, "open is a bool, got %u\n", c.open);
		presence_card_reset(&c);
		EQI(c.open, 0, "reset closes it");
		presence_card_reset(NULL);
		CHECK(1, "reset(NULL) is safe\n");
	}

	// -- (e) the surface policy (A4.4.2 / O-A4): ONE function decides which pills are up, so the
	// always-on vs on-approach taste call is a one-line change with this test already pointed at it.
	{
		EQI(presence_surfaces(0, 1, 1, 1, 0, 1), 0u, "nothing drawn => no pills at all");
		EQI(presence_surfaces(1, 0, 1, 1, 0, 1), 0u, "the HUD is off => no pills (one user control)");
		//                              drawn hud name meet card near
		EQI(presence_surfaces(1, 1, 1, 0, 0, 9), PRES_SURF_PLATE, "a named peer gets a nameplate");
		EQI(presence_surfaces(1, 1, 1, 1, 0, 9) & PRES_SURF_PLATE, PRES_SURF_PLATE,
		    "...whether or not we are meeting");
		EQI(presence_surfaces(1, 1, 1, 1, 0, 1), PRES_SURF_PLATE | PRES_SURF_PROMPT,
		    "meeting raises the prompt");
		EQI(presence_surfaces(1, 1, 1, 1, 1, 1), PRES_SURF_PLATE,
		    "...which goes away once the card it opens is up");
		EQI(presence_surfaces(1, 1, 0, 1, 0, 1) & PRES_SURF_PLATE, 0u,
		    "an unnamed peer gets no nameplate (A0.4) ...");
		EQI(presence_surfaces(1, 1, 0, 1, 0, 1) & PRES_SURF_PROMPT, PRES_SURF_PROMPT,
		    "...but is still interactable");
		// The shipped policy ignores distance; the O-A4 alternative is the only thing that would
		// use it. Pinned so switching is a deliberate edit rather than an accident.
		for (int t = 0; t < 40; t++)
			EQI(presence_surfaces(1, 1, 1, 0, 0, t), PRES_SURF_PLATE,
			    "distance %d does not change the shipped policy", t);
	}
}

// --------------------------------------------------------------------------------------------
// TEST 36 — the Card's text + the formatters (SPEC-avatar A4.3.2, A4.4.4, A5.5.3).
// --------------------------------------------------------------------------------------------
static void test_card_text(void) {
	printf("TEST 36: the card's text — %%05u trainer ID, no secret ID, the degraded card,\n"
	       "         and A5.5.3's disclosure line\n");

	// -- (a) the formatters, including the boundaries a hand-rolled one gets wrong.
	{
		char b[16];
		EQI(presence_fmt_u5(0, b, (int)sizeof b), 5, "u5 writes five digits");
		CHECK(strcmp(b, "00000") == 0, "u5(0) = %s\n", b);
		presence_fmt_u5(1234, b, (int)sizeof b);  CHECK(strcmp(b, "01234") == 0, "u5(1234) = %s\n", b);
		presence_fmt_u5(65535, b, (int)sizeof b); CHECK(strcmp(b, "65535") == 0, "u5(65535) = %s\n", b);
		memset(b, 0x7F, sizeof b);
		EQI(presence_fmt_u5(1234, b, 5), 0, "u5 refuses a buffer that cannot hold 5 digits + NUL");
		CHECK(b[0] == '\0', "...and still terminates it\n");
		EQI(presence_fmt_u5(1234, NULL, 8), 0, "u5(NULL) is safe");

		EQI(presence_fmt_int(0, b, (int)sizeof b), 1, "int(0)");
		CHECK(strcmp(b, "0") == 0, "int(0) = %s\n", b);
		presence_fmt_int(-1, b, (int)sizeof b);   CHECK(strcmp(b, "-1") == 0, "int(-1) = %s\n", b);
		presence_fmt_int(255, b, (int)sizeof b);  CHECK(strcmp(b, "255") == 0, "int(255) = %s\n", b);
		presence_fmt_int(-128, b, (int)sizeof b); CHECK(strcmp(b, "-128") == 0, "int(-128) = %s\n", b);
		presence_fmt_int(2147483647, b, (int)sizeof b);
		CHECK(strcmp(b, "2147483647") == 0, "int(INT_MAX) = %s\n", b);
		// The negation trap. FIX PASS (review finding 1): the VALUE was always right here, because
		// the host's `long` is 64-bit — on devkitARM (ILP32) it is 32-bit, `-(long)INT32_MIN` is not
		// representable, and that is signed-overflow UB the optimiser may act on. This assertion
		// cannot detect that; it pins the intended answer, and the implementation now reaches it
		// through `0u - (unsigned)v`, which is well-defined modular arithmetic on every ABI.
		presence_fmt_int(-2147483647 - 1, b, (int)sizeof b);
		CHECK(strcmp(b, "-2147483648") == 0, "int(INT_MIN) = %s (the negation trap)\n", b);
		memset(b, 0x7F, sizeof b);
		EQI(presence_fmt_int(12345, b, 4), 0, "a number that does not FIT is dropped, not truncated");
		CHECK(b[0] == '\0', "...and the field is left empty rather than half-printed\n");
	}

	// -- (b) a fully-identified peer.
	{
		PeerPresence p = rec_ok(14, 9, 0, 0);
		const uint8_t nils[8] = { 0xC8, 0xDD, 0xE0, 0xE7, 0xFF, 0xFF, 0xFF, 0xFF };
		memcpy(p.name, nils, 8);
		p.tid = 1234; p.gender = 1;
		PresCardText t; presence_card_fill(&p, &t);
		EQI(t.haveIdent, 1, "identity present");
		CHECK(strcmp(t.name, "Nils") == 0, "name = %s\n", t.name);
		CHECK(strcmp(t.id, "01234") == 0, "A4.3.2's five-digit trainer ID, got %s\n", t.id);
		EQI(t.gender, 1, "gender");
		CHECK(strcmp(presence_gender_label(t.gender), "F") == 0, "gender label\n");
		CHECK(strcmp(t.loc, "MAP 3-12   TILE 14,9") == 0, "loc = \"%s\"\n", t.loc);
	}

	// -- (c) the degraded card (SPEC-data D1.9: sb2ptr == 0 / a failed deref / a save not yet
	// loaded). The AVATAR still draws in that state, so the card must still be well formed.
	{
		PeerPresence p = rec_ok(1, 2, 0, 0);
		p.flags &= (uint8_t)~PRES_F_IDENT;
		p.tid = 4242;
		PresCardText t; presence_card_fill(&p, &t);
		EQI(t.haveIdent, 0, "no identity latched");
		CHECK(strcmp(t.name, "?") == 0, "name falls back to '?', got %s\n", t.name);
		CHECK(strcmp(t.id, "-----") == 0, "the TID is HIDDEN, not printed from a stale record\n");
		EQI(t.gender, -1, "gender is unknown, not silently male");
		CHECK(strcmp(presence_gender_label(t.gender), "?") == 0, "unknown gender label\n");
		CHECK(strcmp(t.loc, "MAP 3-12   TILE 1,2") == 0, "the POSITION is still shown: %s\n", t.loc);

		// ...and a record with no valid position shows no position at all rather than a plausible
		// wrong one.
		PeerPresence q = rec_ok(5, 6, 0, 0);
		q.flags &= (uint8_t)~PRES_F_SB1VALID;
		presence_card_fill(&q, &t);
		CHECK(t.loc[0] == '\0', "an invalid SaveBlock1 prints NO tile, got \"%s\"\n", t.loc);

		// A NULL record still yields the placeholder card (main.c fills one at session start).
		presence_card_fill(NULL, &t);
		CHECK(strcmp(t.name, "?") == 0 && strcmp(t.id, "-----") == 0 && t.loc[0] == '\0',
		      "a NULL record is a well-formed empty card\n");
		presence_card_fill(&p, NULL);   // must not crash
		CHECK(1, "NULL out is safe\n");
	}

	// -- (d) an 8-character name with a gendered sign — the case the whole UTF-8 decoder exists for
	// — must fit the card's field with room for the NUL.
	{
		PeerPresence p = rec_ok(0, 0, 0, 0);
		uint8_t nm[8]; for (int i = 0; i < 7; i++) nm[i] = (uint8_t)(0xBB + i);
		nm[7] = 0xB5;                                   // ...ending in the male sign
		memcpy(p.name, nm, 8);
		PresCardText t; presence_card_fill(&p, &t);
		CHECK(strcmp(t.name, "ABCDEFG\xE2\x99\x82") == 0, "8-glyph name with a sign: \"%s\"\n", t.name);
		CHECK(strlen(t.name) + 1 <= PRES_CARD_NAME_CAP, "the worst case fits the field\n");
		// The absolute worst case: eight 3-byte glyphs = 24 bytes + NUL == PRES_CARD_NAME_CAP.
		memset(p.name, 0xB5, 8);
		presence_card_fill(&p, &t);
		EQI((int)strlen(t.name), 24, "eight 3-byte glyphs decode whole");
		EQI(PRES_CARD_NAME_CAP, 25, "...and the field is sized for exactly that plus the NUL");
	}

	// -- (e) A5.5.3, BINDING: the card carries the Union-Room disclosure, and it does NOT carry a
	// trade or battle affordance. A greyed button would read as "coming in the next build".
	{
		CHECK(strstr(PRES_CARD_UNION_NOTE, "Union Room") != NULL,
		      "the disclosure names the game's own Union Room\n");
		CHECK(strstr(PRES_CARD_UNION_NOTE, "not from here") != NULL,
		      "...and says it does not happen from the card\n");
		CHECK(strstr(PRES_CARD_READONLY_NOTE, "read-only") != NULL,
		      "the card says it is read-only (PHASE.md invariant 1, on the surface itself)\n");
		// PresCardText has no button, no action and no selection index — there is nothing on the
		// card that could be pressed. That is the structural form of A5.5.3: three text fields and
		// two ints, and nothing else can be added without this size assertion firing.
		{
			size_t text = PRES_CARD_NAME_CAP + 8 + 32;              // name + id + loc
			size_t want = ((text + sizeof(int) - 1) / sizeof(int)) * sizeof(int)   // int alignment
			            + 2 * sizeof(int);                          // gender + haveIdent
			EQI((int)sizeof(PresCardText), (int)want,
			    "the card is text and two ints — no action, no selection, nothing pressable");
		}
	}
}

// --------------------------------------------------------------------------------------------
// TEST 37 — the co-op pref's semantics (SPEC-avatar A6.3, A7.1 P12).
//
// The Settings BYTE LAYOUT and the length ladder are mirrored in test_tilt.c TEST 6, where the
// struct mirror lives and where main.c's _Static_asserts point (BUILDLOG slice M1, deviation 8).
// What that test cannot see is what the pref MEANS once loaded, which is this one:
// P12's "a corrupt/negative presence word can only produce 0/1", plus the default and the gate.
// --------------------------------------------------------------------------------------------
static void test_pref_semantics(void) {
	printf("TEST 37: the co-op pref — any stored word normalises to a bool, and P-G1 is first\n");

	// -- (a) normalisation. main.c stores `s.presence != 0`; every one of these words must land on
	// exactly one of two values, including the negative and the all-ones cases a corrupt or
	// truncated file produces.
	{
		const int32_t W[8] = { 0, 1, -1, 2, 0x7FFFFFFF, (int32_t)0x80000000, 42, -32768 };
		for (int i = 0; i < 8; i++) {
			int on = (W[i] != 0) ? 1 : 0;
			CHECK(on == 0 || on == 1, "word %ld normalises to a bool\n", (long)W[i]);
			EQI(on, W[i] != 0, "word %ld -> %d", (long)W[i], on);
		}
		EQI((int)(0 != 0), 0, "A6.3.4: the shipped default is OFF, so a new feature ships inert");
	}

	// -- (b) the pref IS gate rule P-G1, and it is FIRST: nothing downstream can re-open it, and
	// the reason code says so by name. This is also the contract main.c's cost guard depends on —
	// the guard skips the three RAM reads per game when the pref is off, and the ladder must refuse
	// the same case, or the two would disagree.
	{
		Harness h; h_init(&h);
		PeerPresence self = rec_ok(10, 10, 0, 0), peer = rec_ok(12, 10, 0, 0);
		PresenceOut out;
		EQI(h_step(&h, self, peer, &out), 1, "with the pref ON and everything healthy, it draws");

		presence_begin_round(&h.ps, h.round);
		peer.round = h.round; peer.hb = h.round;
		presence_publish(&h.ps, 0, &peer);
		PresenceIn in; memset(&in, 0, sizeof in);
		in.enabled = 0; in.slot = 0; in.self = self;
		EQI(presence_solve(&h.ps, &in, &out), 0, "the pref off closes the gate");
		EQI(out.reason, PRES_OFF_DISABLED, "...with P-G1's own reason code");
		CHECK(strcmp(presence_off_reason(out.reason), "off") == 0,
		      "...which prints as \"off\" in the HUD readout\n");
		// P-G1 wins even when EVERY other rule would also have closed the gate, so the readout can
		// never blame a later rule for a switched-off feature.
		in.menuOpen = 1; in.linkAny = 1;
		memset(&in.self, 0, sizeof in.self);
		EQI(presence_solve(&h.ps, &in, &out), 0, "still closed");
		EQI(out.reason, PRES_OFF_DISABLED, "...and still blamed on the pref, not on a later rule");
	}
}

// ============================================================================================

// ============================================================================================
// TEST 38 — the head-pill stack (SPEC-avatar A4.4.1 / A5.4.1; fix pass, review finding 8)
// ============================================================================================
static void test_pill_stack(void) {
	printf("TEST 38: the nameplate + prompt are ONE stack — never overlapping, never in the HUD bar\n");
	const float SH = 240.0f, SW = 400.0f;

	// The defect, stated: with two INDEPENDENT clamps, any peer roughly 3+ tiles above the player
	// collapsed both pills onto y = 2 — and y = 2..17 is inside the 0..16 px HUD bar, which is drawn
	// AFTER them and is translucent. Sweep every head y a peer can have on either screen.
	int overlap = 0, inBar = 0, wrongGap = 0, offBottom = 0;
	for (int hy = -200; hy <= 400; hy++) {
		float plateY, promptY;
		presence_pill_y((float)hy, 1, 1, SH, &plateY, &promptY);
		if (promptY < plateY + PRES_PILL_H - 0.001f) overlap++;                 // pills on top of each other
		if (promptY - plateY < PRES_PILL_H - 0.001f ||
		    promptY - plateY > PRES_PILL_H + 0.001f) wrongGap++;                // the gap must be RIGID
		if (plateY < PRES_PILL_TOP - 0.001f) inBar++;                           // into the HUD bar
		if (promptY + PRES_PILL_H > SH + 0.001f && plateY > PRES_PILL_TOP + 0.001f) offBottom++;
	}
	EQI(overlap,   0, "the two pills NEVER draw on top of each other, at any head y");
	EQI(wrongGap,  0, "...and keep their exact 15 px separation, at any head y");
	EQI(inBar,     0, "...and never enter the HUD bar the chrome block draws over them");
	EQI(offBottom, 0, "...and are pushed up rather than off the bottom of the panel");

	// The specific case the review worked: top screen SCALE_FIT (sy = 1.5, oy = 0), peer 5 tiles
	// above the host => footY = 88 - 80 = 8 => sprY = -24 => headFy = -24 => hy = -36. BOTH pills
	// used to clamp to 2.0 and draw one over the other.
	{
		float plateY, promptY;
		presence_pill_y(-36.0f, 1, 1, SH, &plateY, &promptY);
		EQF(plateY,  PRES_PILL_TOP,               "the review's own case: the plate sits below the bar");
		EQF(promptY, PRES_PILL_TOP + PRES_PILL_H, "...and the prompt sits below the plate, not on it");
	}
	// A lone surface takes the prompt's slot (A4.4.1's "y = headY - 13 when it is alone").
	{
		float plateY, promptY;
		presence_pill_y(100.0f, 1, 0, SH, &plateY, &promptY);
		EQF(plateY, 100.0f - PRES_PILL_H, "a lone nameplate sits immediately above the head");
		presence_pill_y(100.0f, 0, 1, SH, &plateY, &promptY);
		EQF(promptY, 100.0f - PRES_PILL_H, "a lone prompt does too");
	}
	// Unclamped in the middle of the screen: the stack must not move when it does not have to.
	{
		float plateY, promptY;
		presence_pill_y(120.0f, 1, 1, SH, &plateY, &promptY);
		EQF(promptY, 120.0f - PRES_PILL_H,        "mid-screen: the prompt is exactly one pill up");
		EQF(plateY,  120.0f - 2.0f * PRES_PILL_H, "mid-screen: the plate is exactly two");
	}
	// NULL outputs are accepted (either pill may be absent at the call site).
	presence_pill_y(50.0f, 1, 1, SH, NULL, NULL);
	CHECK(1, "NULL outputs are refused without crashing\n");

	// The x clamp: centred, right edge first, left edge second — so a pill WIDER than the screen is
	// pinned at the left rather than pushed off it (the opposite order yields a negative x).
	EQF(presence_pill_x(200.0f, 100.0f, SW), 150.0f, "a pill centres on the head point");
	EQF(presence_pill_x(395.0f, 100.0f, SW), SW - 100.0f - PRES_PILL_EDGE, "...clamped at the right");
	EQF(presence_pill_x(2.0f,   100.0f, SW), PRES_PILL_EDGE,               "...clamped at the left");
	EQF(presence_pill_x(200.0f, 500.0f, SW), PRES_PILL_EDGE,
	    "a pill wider than the screen is pinned at the LEFT, never at a negative x");
}

// ============================================================================================

int main(void) {
	printf("=== test_presence — phase 15 co-op presence, the data half (SPEC-data.md) ===\n");
	test_sub();
	test_golden();
	test_continuity();
	test_degrade();
	test_gate();
	test_universes();
	test_fieldgate();
	test_liveness();
	test_hold();
	test_newest_wins();
	test_smoothing();
	test_teleport();
	test_cull();
	test_cull_matches_clip();    // fix pass (finding 5)
	test_name();
	test_dir();
	test_record();
	test_purity();
	test_ident_due();            // slice M1
	test_fill_core();            // slice M1
	test_gate_from_source();     // slice M1
	test_name_through_producer();// slice M1
	test_art_cells();            // slice M2 — the render half (SPEC-avatar.md)
	test_art_rect();             // slice M2
	test_art_clip();             // slice M2
	test_art_clip_mirror();      // slice M2
	test_art_tilt();             // slice M2
	test_art_build();            // slice M2
	test_art_walk();             // slice M2
	test_art_ysort();            // slice M2
	test_gbatext();              // slice M3 — identity + interaction (SPEC-avatar A4/A5/A6)
	test_meet();                 // slice M3
	test_card_fsm();             // slice M3
	test_card_text();            // slice M3
	test_pref_semantics();       // slice M3
	test_pill_stack();           // fix pass (finding 8)
	printf("=== %d checks, %d failures ===\n", g_checks, g_fail);
	return g_fail ? 1 : 0;
}
