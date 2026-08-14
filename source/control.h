// control.h — Phase 13-prep slices D4 + D5: file-driven TILE-EXACT movement + go-files (D4)
//             and INPUT RECORD / REPLAY (D5).
// ============================================================================================
// PURE C (CLAUDE.md rule #4 / PHASE.md invariant 4): <stdint.h>/<stdbool.h> only — no libctru,
// no mGBA, NO file I/O and NO clock — so control.c dual-compiles on the PC host harness
// (test/host/test_control.c) and the whole grammar + closed-loop scheduler is unit-tested with
// a fake coordinate feed. The glue (source/main.c) owns every sdmc stat/fopen/remove/fprintf
// and feeds per-tick snapshots in; the module hands back a key mask + status lines.
//
// Design ported from the melonDS-PM file-driven movement harness
// (docs/kb/external/pm-bridge-forensics.md PART 2 #10 `melonds_move.txt`, EmuThread.cpp:275-408;
// #9 go-files, EmuThread.cpp:199-235; PART 3 ranked port item 2), per the binding spec
// docs/phase13-diagnostics/SPEC-control-replay.md §D4 + §C.
//
// TRADE PATH FROZEN (PHASE.md invariant 1): the ONLY output that reaches the emulator is a GBA
// key mask ORed into the EXISTING per-seat assembly in main.c (`emuA.keys`/`emuB.keys`), i.e.
// the emulated KEYPAD — the same seam touch_update() has always used (touch.h:54-59,
// main.c "emuA.keys = ... | tk"). celiolink's FSM, netlink's rounds and gbacore's SIO fill all
// sit downstream of the game's own input handling and are not on this path: zero SIO changes.
//
// FLAG-GATED, DEFAULT-OFF (PHASE.md invariant 2): the glue enables the whole feature only when
// the directory `sdmc:/cias/control` exists (checked ONCE per session — SPEC D4.1). No
// directory => zero polls, zero overhead, no injection. CTL_D4_ENABLE below is the additional
// compile-time bisect gate (mirrors diag.h's DIAG_D*_ENABLE).
//
// THE SCHEDULER CLOCK IS THE TARGET CORE'S EMULATED FRAME COUNTER (SPEC §0) —
// `gbacore_frame_counter()`, sampled per render frame at the injection site. Every timer here
// (tap slots, waits, walk deadlines, the no-field cap) counts EMULATED frames of the scripted
// core, so: a wireless session at ~4-5 emu-fps scales waits with the game's real rate; an open
// pause menu / backgrounded app / paused peer seat FREEZES the script in place instead of
// blind-firing or timing out. All comparisons are uint32 deltas, wrap-safe.
//
// NO NEW GAME-RAM ADDRESSES (SPEC C.2): the closed loop reads the coordinates the game-state
// logger ALREADY samples every frame — GameState px/py/mapGroup/mapNum, the first two s16
// behind the dereferenced gSaveBlock1Ptr plus SaveBlock1.location (gamestate.c game_read;
// pret pokeemerald/pokefirered `struct SaveBlock1 { struct Coords16 pos; struct WarpData
// location; ... }`). Those addresses are already VERIFIED vs the pret byte-matched sym maps
// (gamestate.h GameProfile.sb1ptr); D4 adds none.
#pragma once
#include <stdint.h>
#include <stdbool.h>

// Compile-time slice gate (bisect a slice out of a build, like diag.h's DIAG_D*_ENABLE). The
// runtime opt-in is the existence of sdmc:/cias/control; the runtime kill switch that already
// exists (sdmc:/cias/control/diag_off.txt) covers the D1-D3 writers, not this one — D4 injects
// nothing unless the operator drops a script file.
#define CTL_D4_ENABLE 1
// D5 (record/replay) rides the SAME opt-in directory and the same poll tick; its own bisect gate
// so a build can carry the movement scripts without the recorder (they are independent features
// that only share the CtlIn snapshot and the injection seam).
#define CTL_D5_ENABLE 1

// --------------------------------------------------------------------------------------------
// Tunables — ONE place (SPEC D4.5). The four tap constants are **verify-on-hw-pending**: the
// Gen-3 turn-vs-step frame threshold (pret src/field_player_avatar.c — turn first, then walk if
// the d-pad is still held) has never been frame-counted on our hardware, and PM's 12/34-40
// numbers are DS-era. Run #13 tunes them here (SPEC Open Question 1): a bare-dir tap must TURN
// without stepping, and a button tap must register exactly ONE newKeys edge at both 60 emu-fps
// and the wireless ~4-5 emu-fps.
// --------------------------------------------------------------------------------------------
#define CTL_FILE_MAX      512    // move-file byte cap (D4.3); bigger = rejected loudly + removed
#define CTL_TOK_MAX       64     // tokens per script (D4.5)
#define CTL_TOK_TEXT      8      // per-token source text kept for the status lines ("D12", "l3")
#define CTL_TAP_DIR_PRESS 4      // bare-dir tap: pressed frames of the slot (face-turn)
#define CTL_TAP_DIR_SLOT  16     //               total slot frames
#define CTL_TAP_BTN_PRESS 12     // button tap: pressed frames (PM EmuThread.cpp:367-381)
#define CTL_TAP_BTN_SLOT  40     //             total slot frames
#define CTL_WALK_DL(n)    ((uint32_t)(n) * 60u + 240u)  // walk wall-timeout, emu frames (D4.8;
                                 // PM EmuThread.cpp:349-361 — generous vs the ~16 frames/tile
                                 // walk cost, so only a genuine wall/NPC block trips it)
#define CTL_NOFIELD_DL    600u   // walk-token field-validity wait cap, emu frames (D4.9)
#define CTL_WAIT_MAX      3600   // W<n> cap
#define CTL_WALK_MAX      255    // n cap on a walk token
#define CTL_STATUS_N      16     // status ring depth (lines) — drained by the glue EVERY frame
#define CTL_STATUS_LEN    96     // status line cap (incl. the trailing '\n' + NUL)
#define CTL_POLL_FRAMES   10     // idle poll cadence in RENDER frames (D4.2; seats staggered by
                                 // the glue so at most one sdmc stat happens per frame)
// ---- D5 record/replay (SPEC-control-replay.md §D5) ------------------------------------------
#define CTL_ANCHOR_FRAMES 3      // field-entry debounce: consecutive fieldValid TICKS before the
                                 // anchor latches (D5.2). Applied IDENTICALLY by the recorder and
                                 // the replayer, so the constant delay cancels between the two.
#define CTL_REC_MAX_LINES 65536u // record line cap (D5.3) — then recording stops LOUDLY
#define CTL_REC_LINE      32     // "<frameOffset> <mask>\n" fits in this (10+1+4+1+NUL)
#define CTL_REP_MAX       8192   // replay table entry cap (D5.6). Deliberately below PM's 65536
                                 // (desktop): the table is RESIDENT next to two mGBA cores —
                                 // 6 B/entry (parallel arrays) = 48 KB/seat. 8192 ON-CHANGE
                                 // entries is tens of minutes of play; over-cap fails LOUDLY.
#define CTL_REP_LINE_MAX  63     // longest accepted table line (the chunked loader's partial-line
                                 // buffer); a longer line is a loud parse error, never a silent cut

// GBA keypad bits, KEYINPUT order — MIRRORED from gbacore.h:16-20 (`GBAKEY_A = 0, GBAKEY_B = 1,
// GBAKEY_SELECT = 2, GBAKEY_START = 3, GBAKEY_RIGHT = 4, GBAKEY_LEFT = 5, GBAKEY_UP = 6,
// GBAKEY_DOWN = 7, GBAKEY_R = 8, GBAKEY_L = 9`). Mirrored rather than included because this
// header must stay libctru/mGBA-free (pure-C rule); main.c carries _Static_asserts that the two
// tables agree, so a drift is a BUILD error, not a wrong button on hardware.
#define CTL_KEY_A      (1u << 0)
#define CTL_KEY_B      (1u << 1)
#define CTL_KEY_SELECT (1u << 2)
#define CTL_KEY_START  (1u << 3)
#define CTL_KEY_RIGHT  (1u << 4)
#define CTL_KEY_LEFT   (1u << 5)
#define CTL_KEY_UP     (1u << 6)
#define CTL_KEY_DOWN   (1u << 7)
#define CTL_KEY_R      (1u << 8)
#define CTL_KEY_L      (1u << 9)

// --------------------------------------------------------------------------------------------
// Per-tick input snapshot, assembled by the glue at the EXISTING parked-window read site
// (main.c, the game-state-logger block) from the ALREADY-read GameState — no new RAM reads.
// --------------------------------------------------------------------------------------------
typedef struct {
	uint32_t emuFrame;    // gbacore_frame_counter(core) — THE scheduler clock (SPEC §0)
	bool     fieldValid;  // gs.valid && ctx==GCTX_OVERWORLD && sb1Valid && px>=0 (D4.9).
	                      // NOTE ctxResolved is deliberately NOT required: GCTX_OVERWORLD is the
	                      // fall-through context (gamestate.h) and ctxResolved stays false there.
	int16_t  px, py;      // SaveBlock1 pos.x/pos.y (GameState px/py); meaningless unless fieldValid
	int16_t  mapGroup, mapNum;  // SaveBlock1.location (GameState mapGroup/mapNum); -1 = unknown
	uint16_t realKeys;    // the seat's NON-script routed mask this frame — the D4.11 abort trigger
	bool     goSeen;      // the glue consumed go_p<N>.txt this tick (D4.10)
} CtlIn;

// Token kinds (internal, exposed so the host test can assert what parsed).
enum {
	CTOK_WALK = 0,   // L<n>/R<n>/U<n>/D<n> (+ lowercase = sprint, adds B) — CLOSED LOOP
	CTOK_TAPDIR,     // bare L/R/U/D (either case) — one press slot, no B
	CTOK_BTN,        // a b s c x y -> A B START SELECT L R
	CTOK_WAIT        // W<n>/w<n>
};
// Direction indices (walk/tapdir). Gen-3 map coordinates: +x = RIGHT, +y = DOWN.
enum { CDIR_L = 0, CDIR_R, CDIR_U, CDIR_D };

typedef struct {
	uint8_t  kind;                 // CTOK_*
	uint8_t  dir;                  // CDIR_* (walk/tapdir only)
	uint16_t mask;                 // the GBA key mask this token holds (0 for CTOK_WAIT)
	uint16_t n;                    // walk tiles / wait frames (0 for taps)
	char     text[CTL_TOK_TEXT];   // the source token, for the status lines ("D12", "l3", "a")
} CtlTok;

// Scheduler states.
enum { CTL_IDLE = 0, CTL_WAIT_GO = 1, CTL_RUN = 2 };

// One per seat. Fixed-size, no allocation — the glue keeps two in static storage (~2.6 KB each,
// deliberately OFF run_session's stack).
typedef struct CtlSched {
	int8_t   seat;        // 0 = p1 / game A, 1 = p2 / game B
	uint8_t  state;       // CTL_IDLE / CTL_WAIT_GO / CTL_RUN
	uint8_t  tokStarted;  // the current token latched its start frame (and, for walks, its target)
	uint8_t  waitArmed;   // the no-field wait window is armed (walk token, D4.9)
	int16_t  nTok, idx;   // parsed token count / current token
	CtlTok   tok[CTL_TOK_MAX];
	uint32_t t0;          // emuFrame the current token started at
	uint32_t deadline;    // emuFrame walk deadline (D4.8); armed only once the token really starts
	uint32_t waitT0;      // emuFrame the no-field wait started at (D4.9)
	int16_t  sx, sy;      // coords latched at walk-token start
	int16_t  tx, ty;      // walk target (start +- n on the moved axis)
	int16_t  mg0, mn0;    // map latched at walk-token start (warp detection, D4.7); -1 = unknown
	uint16_t toksDone, aborts, timeouts, pickups;   // counters for the '# control' netlog line
	// Status ring (D4.13): control.c NEVER does file I/O — it queues lines the glue drains every
	// frame and appends (+fflush per line, PM crash-safety) to the control log.
	char     ring[CTL_STATUS_N][CTL_STATUS_LEN];
	uint8_t  rHead, rCount;
} CtlSched;

// Netlog mirror (D4.14): the glue publishes each seat's counters here after every tick so
// gbacore.c's net-log dump can print one '# control' summary line next to '# celio' without
// knowing anything about the schedulers (same "mirror a worker's state into a static" pattern
// gbacore.c already uses for s_celio*). The rec*/rep* fields are D5's half of the same line.
typedef struct {
	uint16_t toksDone, aborts, timeouts, pickups; uint8_t state, nTok, idx;   // D4 scheduler
	uint32_t recLines;                    // D5 record: lines written so far
	uint8_t  recState;                    // 0 = off, 1 = armed (hunting the anchor), 2 = recording
	uint8_t  repState;                    // CTL_REP_* below
	uint16_t repIdx, repN;                // D5 replay: entries played / entries loaded
} CtlStat;
extern CtlStat g_ctlStat[2];

// --------------------------------------------------------------------------------------------
// API
// --------------------------------------------------------------------------------------------
void     ctl_init(CtlSched* cs, int seat);   // seat 0 = p1/game A, 1 = p2/game B

// Parse a move-file body (NUL-terminated, <= CTL_FILE_MAX bytes). Returns:
//   >0  token count — the script is queued and RUNNING (or CTL_WAIT_GO if token 0 was 'G')
//    0  the file was an ABORT file (first non-whitespace byte '!', D4.4). The scheduler is NOT
//       touched here: the glue logs `ABORT by file` (ctl_note) and clears via ctl_abort(cs,NULL)
//       — plus, in a later slice, the armed/running replay (D5).
//   -1  parse error; `err` (if non-NULL) names why. Nothing is queued; any previously running
//       script is left alone (the glue only offers a file when the seat is idle).
int      ctl_load(CtlSched* cs, const char* text, char* err, int errCap);

bool     ctl_active(const CtlSched* cs);       // glue accepts a NEW move file only when false
bool     ctl_waiting_go(const CtlSched* cs);   // glue polls go_p<N>.txt only when true

// Abort + clear. `why` NULL = clear silently (the caller already logged); non-NULL pushes
// `ABORT <why> at tok <i>`. Idempotent and free when the seat is already idle (so the glue can
// call it unconditionally). Counts an abort ONLY when something was actually active.
void     ctl_abort(CtlSched* cs, const char* why);

// Advance one render frame; returns the GBA key mask to OR into this seat's assembly.
uint16_t ctl_tick(CtlSched* cs, const CtlIn* in);

// Push one operator-facing line (the glue's own events: pickups, parse errors, GO, aborts).
// Formatted as "[ctl pN] <msg>\n"; over-long messages are truncated, never overflow.
void     ctl_note(CtlSched* cs, const char* msg);

// Drain ONE queued status line into line[cap] (FIFO). Returns its length, or 0 when empty.
int      ctl_status(CtlSched* cs, char* line, int cap);

// Counters for the '# control' netlog summary (D4.14); any out-param may be NULL.
void     ctl_counters(const CtlSched* cs, uint16_t* toksDone, uint16_t* aborts,
                      uint16_t* timeouts, uint16_t* pickups);
// Copy this seat's counters into g_ctlStat[seat] (the netlog mirror).
void     ctl_publish(const CtlSched* cs);

// ============================================================================================
// D5 — INPUT RECORD / REPLAY (SPEC-control-replay.md §D5)
// ============================================================================================
// Ported from the DeSmuME-PM bridge's patModes 10/11 (docs/kb/external/pm-bridge-forensics.md
// PART 1 #2 — mp_bridge.cpp:464-477 determinism comment, 1584-1615 record, 1616-1647 replay;
// PART 3 ranked port item 3). Two halves, both pure C:
//
//   RECORD  — passive. Reads the seat's FINAL assembled key mask (real pad + touch + any D4
//             script keys — what the core actually received is the only thing that replays
//             faithfully, D5.4) and hands back "<frameOffset> <mask>" lines ON CHANGE for the
//             glue to append+fflush. It NEVER injects, so it can never fight live input — PM's
//             own rule (mp_bridge.cpp:1614 early-return) satisfied by construction, not by a
//             check.
//   REPLAY  — armed by a go file, waits for the SAME anchor, then per tick advances
//             `while (tab[idx].f <= now) latest = tab[idx++].mask;` and ORs `latest` into the
//             seat exactly like a script mask (PM's loop, mp_bridge.cpp:1616-1647).
//
// THE ANCHOR (D5.2) is the FIELD-ENTRY EDGE — `fieldValid` (the D4.9 predicate the glue
// computes: gs.valid && ctx==GCTX_OVERWORLD && sb1Valid && px>=0) becoming true for
// CTL_ANCHOR_FRAMES consecutive ticks AFTER a non-field state. Both halves use the identical
// detector, so boot/menu/connect timing washes out of the offsets — PM's reason for anchoring at
// field entry rather than power-on (mp_bridge.cpp:464-470). Requiring a real EDGE (a non-field
// state first) is deliberate: an anchor that can fire "wherever we happen to be standing" is not
// reproducible on the replaying console. Cheap universal edge on Gen-3: open the START menu
// (GCTX_FIELDMENU = non-field) and close it. Arming while already on the field is legal but
// LOUD — the status ring says the anchor is waiting for that edge (never a silent no-op).
//
// KNOWN WEAKNESS (documented + accepted, SPEC D5.2): GCTX_OVERWORLD is the fall-through context
// (gamestate.h ctxResolved), so an UNDETECTED save-loaded screen (Pokénav, the PC, …) also reads
// as "field". Discipline (Appendix R2): park the save AT the scene and arm from a menu, so the
// first matching edge IS the field entry. PM's "overworld frameCounter advancing" anchor
// (mp_bridge.cpp:1379-1385) carries exactly the same class of caveat.
//
// DETERMINISM ASSUMPTIONS — operator-facing, VERBATIM per SPEC D5.9 (from mp_bridge.cpp:464-470
// plus our platform facts). A replay only reproduces a session when:
//   1. The replaying console starts from the SAME battery .sav (and the same ROM revision) as
//      the recording — "the replay instance must start from the SAME save/state as the
//      recording".
//   2. The field-entry anchor washes out boot/menu timing — offsets are relative to the
//      aligning event, not to power-on.
//   3. Same starting tile + facing (park the save at the scene, Appendix R2).
//   4. RTC caveat: Emerald's RTC seeds berry/tide/Feebas state — irrelevant over minutes-long
//      scripts, noted for honesty; mGBA's RTC is host-clock-derived, so two runs are never
//      RTC-identical.
//   5. WIRELESS SESSIONS ARE NOT DETERMINISTIC (radio timing, the peer's game). Replay is a
//      solo/local repro + regression tool; the two-console Tier-3 experiment (record seat A on
//      console 1, replay from the same save on console 2, compare state checksums —
//      pm-bridge-forensics.md checklist #3) is exactly the experiment this enables, and a
//      divergence there is a RESULT, not a bug in D5.
// ============================================================================================

// The shared field-entry edge detector state (identical in both halves — see THE ANCHOR above).
typedef struct {
	uint8_t db;           // consecutive fieldValid ticks so far (0..CTL_ANCHOR_FRAMES)
	uint8_t sawNonField;  // a non-field tick has been seen since arming => an EDGE is possible
	uint8_t noted;        // the "armed while ON-FIELD" status line has been pushed once
} CtlAnchor;

// ---- record (one per seat; PASSIVE — never returns a key mask) ------------------------------
typedef struct CtlRec {
	int8_t    seat;
	uint8_t   armed;       // record_p<N>.txt was consumed (D5.1)
	uint8_t   anchored;    // the field-entry edge latched anchorFrame
	CtlAnchor anch;
	uint32_t  anchorFrame; // emulated frame the anchor latched at (offset 0)
	uint32_t  lines;       // lines emitted (cap CTL_REC_MAX_LINES)
	uint16_t  lastMask;    // ON-CHANGE filter (PM's apRecLastMask, mp_bridge.cpp:1591-1615)
	char      ring[CTL_STATUS_N][CTL_STATUS_LEN];
	uint8_t   rHead, rCount;
} CtlRec;

void     ctl_rec_init(CtlRec* r, int seat);
void     ctl_rec_arm(CtlRec* r);                 // record_p<N>.txt consumed (D5.1)
bool     ctl_rec_armed(const CtlRec* r);         // armed (hunting the anchor OR recording)
bool     ctl_rec_anchored(const CtlRec* r);      // anchor latched => the glue's file exists
uint32_t ctl_rec_anchor_frame(const CtlRec* r);  // for the file header's `anchored=` field
uint32_t ctl_rec_lines(const CtlRec* r);
// Stop recording (session end / '!' abort file — D5.5). `why` NULL = silent.
void     ctl_rec_stop(CtlRec* r, const char* why);
// Feed one tick + the seat's FINAL assembled mask (D5.4). Returns the length of a
// "<frameOffset> <mask>\n" line written into line[cap] when one is due (anchor latch + the
// on-change filter live inside), 0 when silent, and -1 EXACTLY ONCE when the cap stops
// recording. Offsets are EMULATED frames since the anchor, so a pause/menu (frozen clock) adds
// nothing and never inflates them.
int      ctl_rec_tick(CtlRec* r, const CtlIn* in, uint16_t finalMask, char* line, int cap);
// Format the record file's `#` header (D5.3) — pure formatting so it is host-testable and
// round-trips through ctl_rep_load (which skips '#' lines). `game` = 4-char game code,
// `date` = the glue's timestamp text; either may be NULL.
int      ctl_rec_header(const CtlRec* r, char* buf, int cap, const char* game, const char* date);
int      ctl_rec_status(CtlRec* r, char* line, int cap);   // drain ONE status line (FIFO)

// ---- replay (one per seat) ------------------------------------------------------------------
enum { CTL_REP_IDLE = 0,   // nothing loaded
       CTL_REP_WAIT = 1,   // table loaded, hunting the field-entry anchor
       CTL_REP_RUN  = 2 }; // anchored, playing back

typedef struct CtlRep {
	int8_t    seat;
	uint8_t   state;        // CTL_REP_*
	CtlAnchor anch;
	uint32_t  anchorFrame;
	int32_t   n, idx;       // entries loaded / next entry to consume
	uint16_t  latest;       // the mask currently held (PM: the last entry whose f <= now)
	uint8_t   drain;        // the table ran out; release `latest` next tick, then done
	// Chunked loader state (the glue streams the file through a small buffer instead of
	// materialising an 80 KB text blob in RAM next to two mGBA cores).
	uint8_t   loading, loadErr, pendLen, skipping;   // skipping = swallowing an over-long '#' line
	char      pend[CTL_REP_LINE_MAX + 2];
	int32_t   lineNo;       // 1-based source line, for the error message
	uint32_t  lastOff;      // monotonicity check (a table that goes backwards can never replay)
	// The table: PARALLEL arrays = 6 B/entry (a {u32,u16} struct would pad to 8).
	uint32_t  f[CTL_REP_MAX];
	uint16_t  mask[CTL_REP_MAX];
	char      ring[CTL_STATUS_N][CTL_STATUS_LEN];
	uint8_t   rHead, rCount;
} CtlRep;

void     ctl_rep_init(CtlRep* r, int seat);
// Parse a whole replay table ('#' and blank lines skipped; "<off> <hexmask>" rows). Returns the
// entry count, or -1 with `err` filled — over-cap, malformed, or backwards offsets fail LOUDLY
// and arm NOTHING (never a silent truncation, D5.6).
int      ctl_rep_load(CtlRep* r, const char* text, char* err, int errCap);
// The same parser, chunked: begin -> feed(chunk, len) xN -> end. `ctl_rep_load` is exactly
// begin+feed+end; the glue uses the chunked form so it can stream the file through a 512-byte
// buffer. feed() returns 0, or -1 once on the first error (later feeds are inert).
void     ctl_rep_load_begin(CtlRep* r);
int      ctl_rep_load_feed(CtlRep* r, const char* chunk, int len, char* err, int errCap);
int      ctl_rep_load_end(CtlRep* r, char* err, int errCap);
bool     ctl_rep_active(const CtlRep* r);      // a table is loaded and not finished
void     ctl_rep_abort(CtlRep* r, const char* why);
uint16_t ctl_rep_tick(CtlRep* r, const CtlIn* in);   // waits for the anchor, then plays back
int      ctl_rep_status(CtlRep* r, char* line, int cap);
// Publish this seat's record/replay state into g_ctlStat[seat] (the '# control' netlog mirror).
void     ctl_publish_rr(int seat, const CtlRec* rec, const CtlRep* rep);

// --------------------------------------------------------------------------------------------
// D4-T (phase 22.1, lane A): SYNTHETIC TOUCH script — the harness channel that lets the
// emutest workflow deliver bottom-screen TOUCHES (taps + drags) to the app, the way move_p<N>
// delivers keys. Same design rules as D4: pure C here (no file I/O, no clock — the glue owns
// sdmc and feeds ticks), consumed-on-pickup, '!' aborts, DEFAULT-OFF behind the same
// sdmc:/cias/control opt-in. The synthetic touch merges at the EXISTING touch-input read in
// main.c (hidTouchRead) and flows through the SAME touch_update path a real stylus uses —
// nothing downstream can tell the difference, which is exactly what makes an emulator proof of
// the touch families honest. Timers count RENDER frames (touch sampling is render-side; in the
// unlinked dual boot the cores step one emulated frame per render frame, so the two clocks
// agree where it matters).
//
// Script grammar (one op per line; '#' comment lines; blank lines skipped; '!' first = abort):
//   t X Y HOLD [GAP]          tap/hold at bottom-screen (X,Y) for HOLD frames, then GAP
//                             released frames (default 8). X 0..319, Y 0..239.
//   d X0 Y0 X1 Y1 FRAMES [GAP]  drag: press at (X0,Y0), move linearly to (X1,Y1) over FRAMES
//                             frames, release, then GAP (default 8). FRAMES >= 2.
//   w FRAMES                  wait released.
#define CTL_TOUCH_MAX 48       // ops per script (a whole name is ~10 taps; 48 is headroom)
#define CTL_TOUCH_FRAMES_MAX 600

typedef struct { int16_t x0, y0, x1, y1; uint16_t hold, gap; uint8_t isWait; } CtlTouchEv;
typedef struct {
	int16_t  n, idx;       // ops loaded / current op (idx >= n = idle)
	uint8_t  phase;        // 0 = hold (touch down / dragging), 1 = gap (released)
	uint16_t t;            // frames spent in the current phase
	CtlTouchEv ev[CTL_TOUCH_MAX];
} CtlTouch;

void ctl_touch_init(CtlTouch* ts);
// Parse a script body. Returns op count (>0, queued and RUNNING), 0 = abort file ('!'),
// -1 = parse error (err filled; nothing queued).
int  ctl_touch_load(CtlTouch* ts, const char* text, char* err, int errCap);
bool ctl_touch_active(const CtlTouch* ts);
void ctl_touch_abort(CtlTouch* ts);
// Advance one render frame. Returns 1 with (*x,*y) = the synthetic touch DOWN this frame, else 0.
int  ctl_touch_tick(CtlTouch* ts, int* x, int* y);
