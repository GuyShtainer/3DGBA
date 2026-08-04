// 3DGBA — two Game Boy Advance games at once on a New 3DS, with an emulated link cable.
// Copyright (C) 2026 Guy Shtainer.  Free software under the GNU GPL v3 — see the LICENSE file.
// Distributed WITHOUT ANY WARRANTY. Bundles mGBA (MPL-2.0); ships no games or Nintendo content.
// -----------------------------------------------------------------------------
// 3DGBA (3DS) — two real mGBA cores, one per screen, with a ROM picker and an
// in-game pause menu.
// -----------------------------------------------------------------------------
// Worker A hosts game A (top screen), worker B hosts game B (bottom), each a
// self-contained mGBA core pinned to its own CPU core. Boot -> ROM picker -> dual
// emulation. Tap the touchscreen (or hold START+SELECT) for the pause menu.
// Two cores at full speed is confirmed on real New 3DS hardware (v0.4 GREEN).
// -----------------------------------------------------------------------------

#include <3ds.h>
#include <citro2d.h>
#include <citro3d.h>
#include <malloc.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <math.h>
#include <stdlib.h>
#include <stddef.h>   // offsetof — settings length-tolerance
#include <sys/stat.h>

#include "gbacore.h"
#include "rompicker.h"
#include "theme.h"
#include "ui.h"
#include "assets.h"

static u32 dim_color(u32 c, float f);   // fwd (defined near run_splash)
#include "gamestate.h"
#include "touch.h"
#include "audio.h"
#include "netlink.h"
#include "wireless.h"
#include "diag.h"     // D1 breadcrumbs + surviving-thread watchdog (docs/phase13-diagnostics/SPEC-firmware-diag.md)
#include "control.h"  // D4 file-driven tile-exact movement (docs/phase13-diagnostics/SPEC-control-replay.md)
#include "tilt.h"     // phase 14 HD-2D diorama tilt: pure-C projection + tween (docs/phase14-tilt/)
#include "presence.h"      // phase 15 co-op presence: the pure-C record/gate/anchor (docs/phase15-presence/)
#include "presence_read.h" // ...and the one file that reads game RAM for it (parked window only)
#include "presence_art.h"  // ...and slice M2's pure-C sheet layout / cull-clip / walk cycle
#include "presence_ui.h"   // ...and slice M3's meeting predicate / card / surface policy
#include "gbatext.h"       // ...and the Gen-3 charmap -> UTF-8 decoder the card + nameplate share
#include "warp_shbin.h"   // M2 grid-warp vertex shader (generated from source/warp.v.pica)
#include "tilt_shbin.h"   // phase 14 tilt vertex shader (generated from source/tilt.v.pica)

#define WORKER_STACKSIZE (512 * 1024)   // mGBA runFrame has deep call chains; 32KB overflows
#define FRAME_TICKS      4481520ULL    // SYSCLOCK_ARM11 / (16756991/280095) -> 59.826 fps real-time cap

typedef struct {
	int        id;
	Thread     thread;
	LightEvent go;
	LightEvent done;
	volatile u32  frame;
	volatile u32  keys;     // GBA keypad bits (neutral if unfocused)
	volatile bool skip;
	volatile s32  core_id;  // actual CPU core (svcGetProcessorID); -1 until set

	GbaCore*   core;        // real mGBA core, or NULL if its ROM failed to load
	u16*       fb;          // linear RGB565 framebuffer (stride GBA_FB_STRIDE)
	C3D_Tex    tex;         // 256x256 RGB565 texture the framebuffer is uploaded into
	bool       has_tex;

	bool          linked;     // free-run under lockstep when true
	volatile bool netLinked;  // free-run under the M2.5 net SIO driver (mutually exclusive with linked)
	volatile bool wantWait;   // lockstep asked this core to park (set in onSleep)
	LightEvent    waitEv;     // peer signals this to un-park us (onWake)
	volatile bool paused;     // X freezes the focused game (unlinked play only)
} EmuInstance;

static volatile bool g_quit = false;
static bool s_hasPtm = false;   // ptm:u for battery level (HUD)
// Phase 14 / SPEC-integration I4.13: run_settings() has no model parameter and must NOT grow one
// (its signature is shared with the boot-menu call site), so the model lands in a file-scope
// static the same way s_hasPtm does. Set once in main() next to APT_CheckNew3DS. s_speedupActive
// is the self-calibrating 804 MHz probe; I5.3 forbids CLAMPING tilt on it (a .3dsx from the
// Homebrew Launcher can never claim 804 MHz, so clamping would make tilt un-iterable on the whole
// make-and-3dslink dev loop) — it is recorded so a slow HARDWARE photo is never mistaken for a
// tilt cost.
static bool s_isN3DS = false;
static bool s_speedupActive = false;
static volatile bool g_appActive = true;   // false while suspended (HOME/sleep) -> idle, free the cores
static aptHookCookie s_aptCookie;
static EmuInstance* volatile g_netWorker = NULL;   // the lone wireless worker (emuA); set at link start, cleared on teardown
// D1 render loop-seq (SPEC D1.3): bumped once per run_session frame-loop iteration. The render
// thread is the watchdog's own thread, so this is pass-through context in the STUCK line, not a
// watched seq (renderSeq frozen == no STUCK lines at all — the accepted D1.8 blind spot).
static volatile uint32_t g_renderSeq = 0;
static void apt_hook(APT_HookType t, void* p) {
	(void)p;
	if (t == APTHOOK_ONSUSPEND || t == APTHOOK_ONSLEEP) {
		g_appActive = false;
		// Pull the wireless worker OUT of its net free-run FIRST: clearing netLinked makes the loop exit
		// once its in-flight collect returns. netlink_exit() then joins the RX thread + aborts every round
		// (so a worker parked in collect returns at once) BEFORE udsUnbind/udsExit — no thread is mid-UDS
		// when the radio is reclaimed (the documented nwm wedge / known close-hang). ONSUSPEND fires inside
		// aptMainLoop while we are still foreground enough for nwm to service every IPC.
		EmuInstance* w = g_netWorker;
		if (w) w->netLinked = false;
		netlink_exit();   // net_link_stop() (joins RX, aborts rounds) + net_session_close() + udsExit()
	} else if (t == APTHOOK_ONEXIT) {
		EmuInstance* w = g_netWorker;
		if (w) w->netLinked = false;
		netlink_exit();   // idempotent (s_inited guard); covers any close path that skipped ONSUSPEND
	} else if (t == APTHOOK_ONRESTORE || t == APTHOOK_ONWAKEUP) {
		g_appActive = true;
		netlink_init();   // bring UDS back up after a HOME/sleep resume so wireless still works this session
	}
}

// Dump the M3 per-round link log to a TIMESTAMPED SD file so successive runs don't overwrite each
// other (compare before/after). Role-named (HOST=seat 0 / JOIN=seat 1) + MMDD_HHMMSS.
static void wl_dump(int seat) {
	time_t tt = time(NULL); struct tm* lt = localtime(&tt);
	char lp[96];
	// All logs live in sdmc:/cias/netlogs/ (matches the local netlogs/ folder) so the whole folder
	// drags-and-drops in one go. Timestamped + role-named (HOST=seat 0 / JOIN=seat 1) so successive runs
	// never overwrite. (gbacore_net_log_dump mkdir's the folder.)
	snprintf(lp, sizeof lp, "sdmc:/cias/netlogs/3DGBA_net_%s_%02d%02d_%02d%02d%02d.txt",
	         seat == 0 ? "HOST" : "JOIN",
	         lt ? lt->tm_mon + 1 : 0, lt ? lt->tm_mday : 0,
	         lt ? lt->tm_hour : 0, lt ? lt->tm_min : 0, lt ? lt->tm_sec : 0);
	gbacore_net_log_dump(lp, seat);
}

// Dump the game-state instrumentation log to a TIMESTAMPED file in the same netlogs folder. Called on
// the session teardown path so a play session's screen/geo/3D timeline lands on SD alongside the netlog.
// seat: 0=HOST, 1=JOIN -> tag the filename with the wireless role so a gs log is identifiable WITHOUT
// cross-referencing the netlog timestamp; seat<0 (Quit/Change, no link) -> plain timestamped name.
static void gs_dump(int seat) {
	time_t tt = time(NULL); struct tm* lt = localtime(&tt);
	char lp[96];
	if (seat >= 0)
		snprintf(lp, sizeof lp, "sdmc:/cias/netlogs/3DGBA_gs_%s_%02d%02d_%02d%02d%02d.txt",
		         seat == 0 ? "HOST" : "JOIN",
		         lt ? lt->tm_mon + 1 : 0, lt ? lt->tm_mday : 0,
		         lt ? lt->tm_hour : 0, lt ? lt->tm_min : 0, lt ? lt->tm_sec : 0);
	else
		snprintf(lp, sizeof lp, "sdmc:/cias/netlogs/3DGBA_gs_%02d%02d_%02d%02d%02d.txt",
		         lt ? lt->tm_mon + 1 : 0, lt ? lt->tm_mday : 0,
		         lt ? lt->tm_hour : 0, lt ? lt->tm_min : 0, lt ? lt->tm_sec : 0);
	gamestate_log_dump(lp);
	// + the touch-event log (same netlogs folder, role-/timestamp-named so it pairs with the gs log).
	char tp[96];
	if (seat >= 0)
		snprintf(tp, sizeof tp, "sdmc:/cias/netlogs/3DGBA_touch_%s_%02d%02d_%02d%02d%02d.txt",
		         seat == 0 ? "HOST" : "JOIN",
		         lt ? lt->tm_mon + 1 : 0, lt ? lt->tm_mday : 0,
		         lt ? lt->tm_hour : 0, lt ? lt->tm_min : 0, lt ? lt->tm_sec : 0);
	else
		snprintf(tp, sizeof tp, "sdmc:/cias/netlogs/3DGBA_touch_%02d%02d_%02d%02d%02d.txt",
		         lt ? lt->tm_mon + 1 : 0, lt ? lt->tm_mday : 0,
		         lt ? lt->tm_hour : 0, lt ? lt->tm_min : 0, lt ? lt->tm_sec : 0);
	touch_log_dump(tp);
}

// ---- D1 watchdog: STUCK-line writer state (SPEC-firmware-diag D1.5-D1.7, S.2-S.3) ------------
static DiagWd s_wd;                  // WORKER episode (reset at session / wireless-link start)
static DiagWd s_wdRx;                // RADIO episode — its OWN escalation clock (review fix 2026-08-03).
                                     // The RX thread bumps g_diagRxSeq ~2000x/s on another core, and
                                     // diag_wd_step treats ANY watched seq advancing as progress, so
                                     // the single spec'd wlOn mask (worker | RX) could NEVER fire: the
                                     // radio heartbeat reset the episode ~400 times between two 200 ms
                                     // samples and D1 was blind in exactly the session it is armed for
                                     // (and symmetrically, a live worker masked an RX wedge). Two
                                     // independent episodes detect BOTH classes; the STUCK line's
                                     // `who=` says which one spoke.
static FILE*  s_wdFile   = NULL;     // LAZILY created at the first STUCK event — a healthy run writes no
                                     // file and never fopens on the hot path (SPEC D1.5)
static u64    s_wdLastMs = 0;        // ~200ms sampler cadence anchor (osGetTime ms)
static bool   s_diagOff  = false;    // runtime kill switch: sdmc:/cias/control/diag_off.txt exists
                                     // (checked once per session/link start, NEVER per frame — SPEC S.3)
// ---- D2 hang catcher: episode state + dump writer (SPEC-firmware-diag D2.2-D2.5) -------------
// Shares the wd lifecycle: reset in diag_wd_session_reset, file closed in diag_wd_close (same
// teardown sites), kill-switched by the same diag_off.txt, sampled on the same ~200ms tick.
static DiagHang s_hang;              // >180-frozen-render-frames detector (participant core, wlOn only)
static FILE*    s_hangFile = NULL;   // LAZILY created at the first dump — a healthy run writes no file
// ---- D3 per-frame CSV telemetry: writer state (SPEC-firmware-diag D3.4-D3.5) -----------------
// Opened ONCE at wireless link start (the `wlOn = true` site — NEVER on the frame path), fully
// buffered (a row = one buffered memcpy), fflushed every 256 rows (DeSmuME-PM apCsv discipline,
// mp_bridge.cpp:1558 — a hard crash loses <=256 rows ~= 4 s; the wd/hang files carry the crash
// instant). Closed at every wl teardown site via diag_wd_close (shared lifecycle, zero new call
// sites on the frozen teardown paths). Kill-switched by the same diag_off.txt (file never opens).
// ROW CAP (review fix 2026-08-03): D3 is default-on for every wireless link and a row is ~223 B at
// ~60 rows/s = ~13 KB/s = ~48 MB/hour of unbounded sdmc growth. At DIAG_CSV_MAX_ROWS (~30 min) the
// writer appends one "# csv capped" line and closes the file — the per-frame branch then sees the
// NULL and goes dormant, exactly like a kill-switched run.
static FILE*    s_csvFile  = NULL;
static uint32_t s_csvRows  = 0;      // rows since the last fflush (256-row cadence)
static uint32_t s_csvTotal = 0;      // rows written this session (vs DIAG_CSV_MAX_ROWS)

// S.2 netlog path helper: "sdmc:/cias/netlogs/3DGBA_<kind>_<ROLE>_<MMDD>_<HHMMSS>.<ext>".
// The wl_dump/gs_dump snprintf pattern factored ONCE for the new diagnostic writers (wd now;
// hang/csv in slices D2/D3). seat 0 -> HOST, 1 -> JOIN, <0 -> no role tag (gs_dump's seat<0 branch).
// Refactoring wl_dump/gs_dump onto this is optional per the spec and deliberately NOT done (no
// behavior-change risk on the frozen trade path's teardown).
static void diag_log_path(char* out, size_t cap, const char* kind, const char* ext, int seat) {
	time_t tt = time(NULL); struct tm* lt = localtime(&tt);
	if (seat >= 0)
		snprintf(out, cap, "sdmc:/cias/netlogs/3DGBA_%s_%s_%02d%02d_%02d%02d%02d.%s",
		         kind, seat == 0 ? "HOST" : "JOIN",
		         lt ? lt->tm_mon + 1 : 0, lt ? lt->tm_mday : 0,
		         lt ? lt->tm_hour : 0, lt ? lt->tm_min : 0, lt ? lt->tm_sec : 0, ext);
	else
		snprintf(out, cap, "sdmc:/cias/netlogs/3DGBA_%s_%02d%02d_%02d%02d%02d.%s",
		         kind,
		         lt ? lt->tm_mon + 1 : 0, lt ? lt->tm_mday : 0,
		         lt ? lt->tm_hour : 0, lt ? lt->tm_min : 0, lt ? lt->tm_sec : 0, ext);
}

static void diag_wd_close(void) {
	if (s_wdFile)   { fclose(s_wdFile);   s_wdFile   = NULL; }
	if (s_hangFile) { fclose(s_hangFile); s_hangFile = NULL; }   // D2: the hang dumps share the wd file lifecycle
	if (s_csvFile)  { fclose(s_csvFile);  s_csvFile  = NULL; s_csvRows = 0; }   // D3: fclose flushes the
	                                                             // buffered tail rows (SPEC D3.5)
	s_csvTotal = 0;                                              // fresh row budget for the next link
}

// (Re)arm the watchdog for a fresh session: fresh episode state, fresh (lazy) file — so a later
// link gets its own timestamped wd file — and re-read the kill switch. Called at run_session
// entry AND at the wireless link-start site (the SPEC S.3/D1.5 `wlOn = true` site).
static void diag_wd_session_reset(void) {
	diag_wd_close();
	memset(&s_wd, 0, sizeof s_wd);
	memset(&s_wdRx, 0, sizeof s_wdRx);   // D1: the radio episode is reset with the worker one
	memset(&s_hang, 0, sizeof s_hang);   // D2: fresh hang episode (init=0 -> next armed sample re-baselines)
	s_wdLastMs = 0;
	struct stat st;
	s_diagOff = (stat("sdmc:/cias/control/diag_off.txt", &st) == 0);
}

// ---- D4 file-driven tile-exact movement: control-file polling + the status-log writer --------
// (SPEC-control-replay.md §D4 + §C.3-C.5; design docs/kb/external/pm-bridge-forensics.md PART 2
// #10 melonds_move.txt / #9 go-files, PART 3 ranked port item 2.) source/control.c is PURE C and
// does NO I/O — every sdmc stat/fopen/remove/fprintf for the feature lives right here, and the
// only thing that reaches the emulator is a GBA key mask ORed into the EXISTING per-seat key
// assembly (the emulated keypad — the seam touch_update already uses). Nothing on the
// SIO/celiolink/netlink path is touched: PHASE.md invariant 1.
//
// DEFAULT-OFF (invariant 2): the whole feature arms only when the directory sdmc:/cias/control
// exists, checked ONCE per run_session. No directory => zero polls, zero injection, no log file.
static CtlSched s_ctl[2];            // seat 0 = p1/game A, 1 = p2/game B (FIXED, independent of
                                     // swapped/focused/screen — each console stages its own file)
static bool     s_ctlOn   = false;   // sdmc:/cias/control exists (one stat per session)
static FILE*    s_ctlFile = NULL;    // LAZILY opened at the FIRST status line — a session that
                                     // never runs a script writes no file and never fopens
static char     s_ctlHdr[192];       // the '# control p1=.. p2=.. dir=..' first line (D4.14)
#if CTL_D5_ENABLE
// D5 record/replay state (SPEC §D5). Static, like the schedulers: CtlRep carries the 8192-entry
// table (~48 KB/seat as parallel arrays) and has no business on run_session's stack.
static CtlRec s_ctlRec[2];
static CtlRep s_ctlRep[2];
static FILE*  s_recFile[2] = { NULL, NULL };   // per-seat recording, created AT the anchor
static CtlIn  s_ctlIn[2];                      // this frame's snapshot, kept for the recorder —
static bool   s_ctlInOk[2];                    // it needs the FINAL mask, assembled further down
#endif

// control.h mirrors the GBA KEYINPUT bit order by hand (it must stay libctru-free — pure-C rule).
// A drift between the two tables would silently press the WRONG button on hardware, so make it a
// BUILD error instead of a hardware surprise.
_Static_assert(CTL_KEY_A      == (1u << GBAKEY_A)      && CTL_KEY_B     == (1u << GBAKEY_B) &&
               CTL_KEY_SELECT == (1u << GBAKEY_SELECT) && CTL_KEY_START == (1u << GBAKEY_START) &&
               CTL_KEY_RIGHT  == (1u << GBAKEY_RIGHT)  && CTL_KEY_LEFT  == (1u << GBAKEY_LEFT) &&
               CTL_KEY_UP     == (1u << GBAKEY_UP)     && CTL_KEY_DOWN  == (1u << GBAKEY_DOWN) &&
               CTL_KEY_R      == (1u << GBAKEY_R)      && CTL_KEY_L     == (1u << GBAKEY_L),
               "control.h CTL_KEY_* drifted from gbacore.h GBAKEY_*");

// Append one status line to the control log, creating it on the FIRST line. fflush per line is
// the PM crash-safety rule (mp_bridge.cpp §1): these scripts run precisely in the hang-prone
// hardware sessions where a buffered tail dies with the power switch (the run-#11 lost-log
// lesson — HANDOFF says "POWER OFF, don't press Quit"). Status events are rare (pickup / GO /
// token transitions / abort), so this is never a hot path.
static void ctl_log_line(const char* line) {
	if (!s_ctlFile) {
		mkdir("sdmc:/cias", 0777);           // same preamble as the existing netlog writers
		mkdir("sdmc:/cias/netlogs", 0777);
		char p[96]; diag_log_path(p, sizeof p, "control", "txt", -1);
		s_ctlFile = fopen(p, "w");
		if (s_ctlFile && s_ctlHdr[0]) fputs(s_ctlHdr, s_ctlFile);
	}
	if (s_ctlFile) { fputs(line, s_ctlFile); fflush(s_ctlFile); }
}

static void ctl_drain_seat(int seat) {
	char line[CTL_STATUS_LEN];
	while (ctl_status(&s_ctl[seat], line, sizeof line) > 0) ctl_log_line(line);
}

static void ctl_close(void) {
	if (s_ctlFile) { fclose(s_ctlFile); s_ctlFile = NULL; }
}

// Control-file path builder — shared by BOTH slices, so it lives outside either gate (review fix
// 2026-08-03: control.h documents CTL_D4_ENABLE / CTL_D5_ENABLE as INDEPENDENT bisect gates, but
// the D5 glue used to be nested inside the D4 region while being CALLED from D5-only blocks, so
// `CTL_D4_ENABLE 0` failed to compile — the one gate a reviewer would actually want to flip).
#if CTL_D4_ENABLE || CTL_D5_ENABLE
static void ctl_path(char* out, size_t cap, const char* kind, int seat) {
	snprintf(out, cap, "sdmc:/cias/control/%s_p%d.txt", kind, seat + 1);
}
#endif

#if CTL_D4_ENABLE
// Read a control file whole. Returns the byte count, -1 = missing/unreadable, -2 = larger than
// the cap (the first CTL_FILE_MAX bytes are still in buf, so a leading '!' abort still works).
static int ctl_read_file(const char* path, char* buf, int cap) {
	FILE* f = fopen(path, "rb");
	if (!f) return -1;
	size_t n = fread(buf, 1, (size_t)cap - 1, f);
	int over = (fgetc(f) != EOF);
	fclose(f);
	buf[n] = '\0';
	return over ? -2 : (int)n;
}
#endif

#if CTL_D5_ENABLE
// ---- D5 input record/replay glue (SPEC-control-replay.md §D5 + C.3-C.5) ---------------------
// Same opt-in directory, same status log, same injection seam as D4 — the module halves are pure
// C (source/control.c) and every stat/fopen/fread/fwrite/remove for them lives here.
//   record_p<N>.txt     marker  -> arm the recorder (content ignored, consumed on pickup, D5.1)
//   3DGBA_rec_p<N>_*.txt output <- the recording, created AT THE ANCHOR (netlogs, so the run
//                                  workflow archives it with everything else)
//   replay_p<N>.txt     table   -> loaded when...
//   replay_go_p<N>.txt  trigger -> ...this one-shot appears (consumed; D5.6)
// The table is streamed through a 512-byte buffer into the chunked parser: an 8192-entry
// recording is ~80 KB of text and materialising it whole next to two mGBA cores would be silly.
static void ctl_rec_close(int seat) {
	if (s_recFile[seat]) { fclose(s_recFile[seat]); s_recFile[seat] = NULL; }
}

// Drain the recorder's + replayer's status rings into the SAME control log the scheduler uses
// (one operator-facing timeline per seat; open-once + fflush per line, D4.13).
static void ctl_rr_drain_seat(int seat) {
	char line[CTL_STATUS_LEN];
	while (ctl_rec_status(&s_ctlRec[seat], line, sizeof line) > 0) ctl_log_line(line);
	while (ctl_rep_status(&s_ctlRep[seat], line, sizeof line) > 0) ctl_log_line(line);
}

// The record/replay poll, staggered HALF a period away from that seat's move/go poll so no single
// render frame ever does more than one file's worth of sdmc stat work (D4.2's rule, extended).
static void ctl_rr_poll_seat(int seat) {
	if ((g_renderSeq % (2u * CTL_POLL_FRAMES)) !=
	    (uint32_t)(seat * CTL_POLL_FRAMES + CTL_POLL_FRAMES / 2)) return;
	CtlSched* cs = &s_ctl[seat];        // the scheduler's ring carries the glue's own notes
	char path[96], msg[CTL_STATUS_LEN];
	struct stat st;

	// --- record_p<N>.txt (D5.1): a marker file. Consumed on pickup like every control file, so a
	// stale marker can never re-arm a later session.
	if (!ctl_rec_armed(&s_ctlRec[seat])) {
		ctl_path(path, sizeof path, "record", seat);
		if (stat(path, &st) == 0) {
			remove(path);
			ctl_rec_close(seat);        // a previous recording's file is closed before the new one
			ctl_rec_arm(&s_ctlRec[seat]);
		}
	}

	// --- replay_go_p<N>.txt (D5.6): the one-shot trigger LOADS replay_p<N>.txt. A load failure is
	// loud and arms nothing, and the bad table is removed so it cannot re-fire on the next touch.
	if (!ctl_rep_active(&s_ctlRep[seat])) {
		ctl_path(path, sizeof path, "replay_go", seat);
		if (stat(path, &st) == 0) {
			remove(path);
			char tpath[96]; ctl_path(tpath, sizeof tpath, "replay", seat);
			FILE* tf = fopen(tpath, "rb");
			if (!tf) {
				snprintf(msg, sizeof msg, "replay load error: no replay_p%d.txt", seat + 1);
				ctl_note(cs, msg);
			} else {
				static char chunk[512];   // static: no 512-byte stack burst on the render thread
				char err[96]; err[0] = '\0';
				ctl_rep_load_begin(&s_ctlRep[seat]);
				size_t got;
				while ((got = fread(chunk, 1, sizeof chunk, tf)) > 0)
					if (ctl_rep_load_feed(&s_ctlRep[seat], chunk, (int)got, err, sizeof err) < 0) break;
				fclose(tf);
				if (ctl_rep_load_end(&s_ctlRep[seat], err, sizeof err) < 0) {
					snprintf(msg, sizeof msg, "replay load error: %s", err);
					ctl_note(cs, msg);
					remove(tpath);
				}
				// success: ctl_rep_load_end already queued the "replay armed: N entries" line.
			}
		}
	}
}

// D5.4: feed the recorder the seat's FINAL assembled mask (what the core actually received) and
// append any produced line. The file is created lazily AT THE ANCHOR — a session that never
// anchors writes nothing at all — and fflushed per line, the PM crash-safety rule
// (mp_bridge.cpp §1): these recordings are made in exactly the sessions that end with the power
// switch (the run-#11 lost-log lesson).
static void ctl_rec_feed(int seat, u16 finalMask, GbaCore* core) {
	CtlRec* r = &s_ctlRec[seat];
	if (!ctl_rec_armed(r)) return;
	char line[CTL_REC_LINE];
	int n = ctl_rec_tick(r, &s_ctlIn[seat], finalMask, line, sizeof line);
	if (n <= 0) return;                 // 0 = nothing due; -1 = the cap stopped it (ring says so)
	if (!s_recFile[seat]) {
		mkdir("sdmc:/cias", 0777);      // same preamble as every other netlog writer
		mkdir("sdmc:/cias/netlogs", 0777);
		char kind[12], p[96];
		snprintf(kind, sizeof kind, "rec_p%d", seat + 1);
		diag_log_path(p, sizeof p, kind, "txt", -1);
		s_recFile[seat] = fopen(p, "w");
		if (s_recFile[seat]) {
			char code[5] = "----", date[24], hdr[768];
			if (core) gbacore_game_code(core, code);
			time_t tt = time(NULL); struct tm* lt = localtime(&tt);
			snprintf(date, sizeof date, "%02d%02d_%02d%02d%02d",
			         lt ? lt->tm_mon + 1 : 0, lt ? lt->tm_mday : 0,
			         lt ? lt->tm_hour : 0, lt ? lt->tm_min : 0, lt ? lt->tm_sec : 0);
			int hl = ctl_rec_header(r, hdr, sizeof hdr, code, date);
			if (hl > 0) fwrite(hdr, 1, (size_t)hl, s_recFile[seat]);
		}
	}
	if (s_recFile[seat]) { fwrite(line, 1, (size_t)n, s_recFile[seat]); fflush(s_recFile[seat]); }
}
#endif   // CTL_D5_ENABLE (the record/replay glue)

#if CTL_D4_ENABLE
// One seat's idle poll (D4.2): every CTL_POLL_FRAMES render frames, seats STAGGERED so at most
// one sdmc stat happens on any single frame. The stat is the cheap common case — a fopen only
// happens on a tick where the operator actually dropped a file. Returns true when go_p<N>.txt was
// consumed this tick (the CtlIn.goSeen one-tick pulse).
static bool ctl_poll_seat(int seat, bool paused) {
	if ((g_renderSeq % (2u * CTL_POLL_FRAMES)) != (uint32_t)(seat * CTL_POLL_FRAMES)) return false;
	CtlSched* cs = &s_ctl[seat];
	static char body[CTL_FILE_MAX + 2];   // static: no per-poll stack pressure on the render thread
	char path[96], err[80], msg[CTL_STATUS_LEN];
	struct stat st;

	// --- move_p<N>.txt -----------------------------------------------------------------------
	ctl_path(path, sizeof path, "move", seat);
	if (stat(path, &st) == 0) {
		int n = ctl_read_file(path, body, (int)sizeof body);
		if (n != -1) {
			const char* p = body;
			while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
			if (*p == '!') {                    // D4.4: abort files are honoured MID-script
				remove(path);
				ctl_note(cs, "ABORT by file");
				ctl_abort(cs, NULL);            // silent clear — the note above IS the message
#if CTL_D5_ENABLE
				// D5.5/D5.8: one '!' stops EVERYTHING this seat's harness is doing — the
				// script, an armed-or-running replay, AND the recorder.
				ctl_rep_abort(&s_ctlRep[seat], "by file");
				ctl_rec_stop(&s_ctlRec[seat], "abort file");
				ctl_rec_close(seat);
#endif
			} else if (ctl_active(cs)) {
				// A non-abort file dropped mid-script is LEFT IN PLACE untouched and picked up
				// when the script ends (D4.4) — no consume, no parse, no log spam.
			} else {
				remove(path);                   // CONSUMED ON PICKUP: a stale script can never re-fire
				if (n == -2) {
					snprintf(msg, sizeof msg, "parse error: file larger than %d bytes", CTL_FILE_MAX);
					ctl_note(cs, msg);
				} else {
					int t = ctl_load(cs, body, err, sizeof err);
					if (t < 0) snprintf(msg, sizeof msg, "parse error: %s", err);
					else       snprintf(msg, sizeof msg, "picked up %d tokens%s", t,
					                    ctl_waiting_go(cs) ? " (holding for the go file)" : "");
					ctl_note(cs, msg);
					// SPEC Open Question 2 (scripts on a paused seat): ACCEPT the pickup, but say
					// out loud that this seat's emulated clock is stopped — otherwise the log
					// shows a queued script that never moves and nothing explains why.
					if (t > 0 && paused)
						ctl_note(cs, "note: seat PAUSED (emulated clock stopped) - the script waits");
				}
			}
		}
	}

	// --- go_p<N>.txt (D4.10): polled only while a G-script holds. One-shot + re-armable by
	// nature (consumed each time); a go file with no waiting script is left in place.
	if (ctl_waiting_go(cs)) {
		ctl_path(path, sizeof path, "go", seat);
		if (stat(path, &st) == 0) { remove(path); return true; }
	}
	return false;
}
#endif   // CTL_D4_ENABLE (the move/go script poll)

// Link callbacks (invoked by mGBA's lockstep). onSleep runs on this core's worker thread
// during runFrame and must NOT block — it only requests a park; the worker parks (blocks on
// waitEv) after runFrame returns. onWake runs on the peer's worker thread and just signals.
static void link_cb_sleep(void* ctx) {
	EmuInstance* e = (EmuInstance*)ctx;
	// Clear first so a STALE wake (one the coordinator fired while we weren't parked) can't make
	// the upcoming park return instantly. Clear (here) and signal (onWake) both run under the
	// coordinator mutex, so they stay ordered; a fresh wake after this clear still releases us.
	LightEvent_Clear(&e->waitEv);
	e->wantWait = true;
}
static void link_cb_wake (void* ctx) { LightEvent_Signal(&((EmuInstance*)ctx)->waitEv); }

static void emu_step(EmuInstance* e) {   // one video frame (unlinked path)
	if (e->core) {
		gbacore_set_keys(e->core, (u16)e->keys);
		gbacore_run_frame(e->core);
	} else {
		e->frame++;
	}
}

static void worker_main(void* arg) {
	EmuInstance* e = (EmuInstance*)arg;
	e->core_id = svcGetProcessorID();
	while (!g_quit) {
		LightEvent_Wait(&e->go);
		if (g_quit) break;
		if (e->linked && e->core) {
			// LINKED: run in fine-grained CPU slices (runLoop) so the lockstep's earlyExit
			// returns us at the EXACT transfer/park point — the core can't cross a transfer
			// START->FINISH while the peer is behind (the cause of the stale/0xFFFF link error).
			// Cooperative like mGBA's mCoreThread, but on our core-2-pinned worker. Decoupled
			// from the render loop, so main stays responsive regardless.
			u64 paceDl = svcGetSystemTick();
			u32 lastVf = gbacore_frame_counter(e->core);
			while (e->linked && !g_quit) {
				gbacore_set_keys(e->core, (u16)e->keys);
				gbacore_run_loop(e->core);
				e->frame++;
				audio_pump_core(e->id, e->core);   // drain this core's audio into its ring (link audio)
				if (e->wantWait) { e->wantWait = false; LightEvent_Wait(&e->waitEv); }
				// Real-time pace: cap each PRODUCED video frame to the 3DS refresh so the lockstepped
				// pair runs at ~60fps (it free-runs -> too fast otherwise). Only the wall-clock rate is
				// touched; the lockstep transfer ordering (trade correctness) is unchanged. Paces between
				// transactions; the short sleep rechecks linked/quit so a link-off exits promptly.
				u32 vf = gbacore_frame_counter(e->core);
				if (vf != lastVf) {
					lastVf = vf;
					paceDl += FRAME_TICKS;
					u64 now = svcGetSystemTick();
					if (now > paceDl) paceDl = now;   // behind schedule: don't accumulate debt
					else while (e->linked && !g_quit && svcGetSystemTick() < paceDl) svcSleepThread(400000);
				}
			}
		} else if (e->netLinked && e->core) {
			// M2.5 net link: the per-transfer STALL is net_transfer_collect() inside the driver's
			// finishMultiplayer (runs in gbacore_run_loop on THIS core's thread); gbacore_net_poll lets a
			// passive child notice a parent-initiated round (no-op on the parent, seat 0).
			// NO real-time frame cap here: a Gen-3 trade demands >=9 completed MULTI transfers per VBlank or
			// the master raises "Communication error". The per-transfer cross-core rendezvous already paces
			// the pair; capping to ~60fps (FRAME_TICKS) collapsed throughput to ~1 transfer/frame and starved
			// the trade's block burst. Let the link burst — the collect blocking is the only pacing it needs.
			while (e->netLinked && !g_quit) {
				gbacore_set_keys(e->core, (u16)e->keys);
				gbacore_net_poll(e->core);
				gbacore_run_loop(e->core);
				e->frame++;
				audio_pump_core(e->id, e->core);
			}
		} else if (!e->skip && !e->paused) {
			emu_step(e);
			audio_pump_core(e->id, e->core);   // drain this core's audio into its ring
		}
		LightEvent_Signal(&e->done);
	}
}

static void emu_start(EmuInstance* e, int id, int core, int prio) {
	memset(e, 0, sizeof(*e));
	e->id = id;
	e->core_id = -1;
	LightEvent_Init(&e->go,   RESET_ONESHOT);
	LightEvent_Init(&e->done, RESET_ONESHOT);
	LightEvent_Init(&e->waitEv, RESET_ONESHOT);   // link park/resume (latching)
	e->thread = threadCreate(worker_main, e, WORKER_STACKSIZE, prio, core, false);
	if (!e->thread) {
		e->thread = threadCreate(worker_main, e, WORKER_STACKSIZE, prio, -2, false);
	}
}

// Allocate this instance's framebuffer + texture and load its ROM into a real core.
static bool setup_core(EmuInstance* e, const char* romPath) {
	e->fb   = (u16*)linearAlloc(GBA_FB_STRIDE * 256 * sizeof(u16));
	e->core = gbacore_create();
	if (!e->core || !e->fb) { if (e->core) { gbacore_destroy(e->core); e->core = NULL; } return false; }
	memset(e->fb, 0, GBA_FB_STRIDE * 256 * sizeof(u16));
	gbacore_set_video_buffer(e->core, e->fb, GBA_FB_STRIDE);
	if (!gbacore_load_rom(e->core, romPath)) {
		gbacore_destroy(e->core); e->core = NULL;
		return false;
	}
	C3D_TexInit(&e->tex, 256, 256, GPU_RGB565);
	C3D_TexSetFilter(&e->tex, GPU_NEAREST, GPU_NEAREST);
	e->has_tex = true;
	return true;
}

static void teardown_core(EmuInstance* e) {
	if (e->core)    gbacore_destroy(e->core);
	if (e->has_tex) C3D_TexDelete(&e->tex);
	if (e->fb)      linearFree(e->fb);
}

static u16 to_gba_keys(u32 held) {
	u16 k = 0;
	if (held & KEY_A)      k |= 1 << GBAKEY_A;
	if (held & KEY_B)      k |= 1 << GBAKEY_B;
	if (held & KEY_SELECT) k |= 1 << GBAKEY_SELECT;
	if (held & KEY_START)  k |= 1 << GBAKEY_START;
	if (held & (KEY_DRIGHT | KEY_CPAD_RIGHT)) k |= 1 << GBAKEY_RIGHT;
	if (held & (KEY_DLEFT  | KEY_CPAD_LEFT))  k |= 1 << GBAKEY_LEFT;
	if (held & (KEY_DUP    | KEY_CPAD_UP))    k |= 1 << GBAKEY_UP;
	if (held & (KEY_DDOWN  | KEY_CPAD_DOWN))  k |= 1 << GBAKEY_DOWN;
	if (held & KEY_R)      k |= 1 << GBAKEY_R;
	if (held & KEY_L)      k |= 1 << GBAKEY_L;
	return k;
}

static void upload_frame(EmuInstance* e) {
	GSPGPU_FlushDataCache(e->fb, GBA_FB_STRIDE * GBA_H * sizeof(u16));
	C3D_SyncDisplayTransfer(
		(u32*)e->fb,       GX_BUFFER_DIM(GBA_FB_STRIDE, GBA_H),
		(u32*)e->tex.data, GX_BUFFER_DIM(256, 256),
		GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGB565) |
		GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB565) |
		GX_TRANSFER_OUT_TILED(1) | GX_TRANSFER_FLIP_VERT(0));
}

// ---- Scaling modes (v0.6): live-cycled with ZR ----
enum { SCALE_1X, SCALE_FIT, SCALE_STRETCH };
static const char* SCALE_NAMES[] = { "1:1", "Aspect-fit", "Stretch" };

// Invert the bottom-screen scale/letterbox: map a 320x240 touch to a GBA pixel (0..239,0..159).
// Mirrors render_game's transform for the bottom screen (mode = scaleMode[1]). false if off-frame.
static bool touch_to_gba(int px, int py, int mode, int* gx, int* gy) {
	const float sw = 320.0f, sh = 240.0f;
	float scx, scy;
	if (mode == SCALE_1X)           { scx = scy = 1.0f; }
	else if (mode == SCALE_STRETCH) { scx = sw / GBA_W; scy = sh / GBA_H; }
	else { float f = (sw / GBA_W < sh / GBA_H) ? sw / GBA_W : sh / GBA_H; scx = scy = f; }
	float ox = (sw - GBA_W * scx) / 2.0f, oy = (sh - GBA_H * scy) / 2.0f;
	int X = (int)((px - ox) / scx), Y = (int)((py - oy) / scy);
	*gx = X; *gy = Y;
	return X >= 0 && X < GBA_W && Y >= 0 && Y < GBA_H;
}

// Sharp-bilinear (the "Sharp" filter at fractional scales): integer-NEAREST prescale the
// 240x160 frame into an offscreen target, then LINEAR-downscale that to fit the screen.
// Linear then only blends between already-huge clean pixels -> crisp edges, no shimmer, no
// blur. This is exactly what mGBA's own 3DS port does. 2x is enough to kill the wobble at
// the 1.5x/1.33x screen-fit factors; PRE_TEX is the POT texture that backs the 480x320 buffer.
#define PRESCALE 2
#define PRE_W    (GBA_W * PRESCALE)   // 480
#define PRE_H    (GBA_H * PRESCALE)   // 320
#define PRE_TEX  512

// HD-2D M1 (tilt-shift DoF): no fragment shader on the PICA200, so "blur" = one GPU_LINEAR
// half-res bounce (240x160 -> 120x80) bilinear-upscaled back -> a gentle ~2x2 box soften.
// Composited as subtle top/bottom bands over the TOP screen only (overworld; both eyes share
// the one blurred copy) -> sharp focal band = "diorama" read. TEXT-AWARE: any BG0 text-layer
// content under a band kills that band's blur (band_text_scan), so text stays readable.
#define DOF_TEXA      128   // POT texture backing the 120x80 half-res bounce
#define DOF_SHARP_Y0  48    // sharp focal band (GBA rows Y0..Y1; player sits ~64..96)
#define DOF_SHARP_Y1  112
#define DOF_FADE      24    // blur alpha-ramps in over this many rows outside the band
#define DOF_ALPHA     0x78  // max band alpha (~47%): a subtle soften, never a wall of mush

// HD-2D M3 (LDR bloom): bright-pass = clamp(half-res frame - BLOOM_THRESH) x2 (TEV SUBTRACT +
// x2 scale) into a quarter-res glow map; composited ADDITIVELY (x BLOOM_GAIN) over each eye.
// Honest LDR glow on the RGB565 frame, not HDR bloom. Focused top screen + overworld only,
// and any on-screen text eases the glow off (a white textbox must never halo its own text).
#define BLOOM_TEX     64    // POT texture backing the 60x40 glow map
#define BLOOM_THRESH  0xC8  // a channel must exceed ~78% to glow (water glints, lamps, white)
#define BLOOM_GAIN    0x90  // additive gain (~56%) on the x2'd glow -> subtle, not blinding

// Stereoscopic single-game depth: pop elements forward per eye on the ALREADY-composited frame
// (no extra emulation pass). depth3d.overworld is snapshotted in the race-safe window. M2 = player.
#define DEPTH_MAX_SPR 32
typedef struct {
	bool overworld;
	int  nspr;
	struct { short x, y; unsigned char w, h, elev; } spr[DEPTH_MAX_SPR];   // on-screen OAM rects (+ matched object elevation tier)
	float tdepth[10][15];           // smoothed scenery EXTRA depth px (layer-type + elevation)
	bool textTop, textBot;          // text/UI under the top/bottom blur band -> suppress that band
	struct { unsigned char x0, y0, x1, y1; } uiRect[6];   // BG0 window panels (tile coords, incl.)
	int  nui;                       // panel count (panels pop in ANY context, not just overworld)
	int  nfg;                       // foreground/solid tiles in view (HUD diagnostic)
	float maxd;                     // strongest tdepth in view (HUD diagnostic)
	int  camX, camY;                // gFieldCamera sub-tile scroll (px, -15..15) -> depth scroll-align
	// --- per-sprite stereoscopic-disparity detail (LOGGING ONLY; filled by depth_disparity_stats, which
	// mirrors pop_eye's feet base=RAMP_AT(fy)+floorD and head=base+POP3D_STANDUP). px @ FULL slider. ---
	float feetMin, feetMax, headMin, headMax;   // grounded-feet / head disparity range across on-screen sprites
	short tallOk, tallFail;                     // #sprites whose head exceeds feet by ~standup (tall renders taller) vs not
	bool  orderOk;                              // on-screen set monotonic in screen-y vs feet disparity (front-is-front)
} DepthSnap;
#define POP3D_PLAYER_GX 112   // player tile (7,5) -> sprite rect (16x32, head 16px above the tile)
#define POP3D_PLAYER_GY 64
// Depth model v2 (hardware round 4): a continuous ground-plane ramp (the bottom of the frame
// is closer -> pops more), scenery extra from the metatile layer-type PLUS the map-grid
// elevation nibble (a hilltop pops above the grass at its feet), characters always floating
// CHAR_PX above the floor at their own feet (the floor can never pop over them), and BG0
// window panels (dialogs/menus) popping hardest of all as clean shifted copies.
#define POP3D_RAMP_PX 3.6f    // ground ramp: bottom-edge pop (px @ full slider); top edge = 0 (moderate for comfort)
#define POP3D_STANDUP 3.0f    // a sprite's head pops this far out beyond its grounded feet (standee lean)
#define ENV3D_NORMAL  4.0f    // solid/foreground tile pop (poles/trees/walls) -- strong stand-up
#define ENV3D_SPLIT   1.2f    // ledge / low fence mid extra
#define ENV3D_RAISED  2.6f    // a "raised" elevation tier (engine priority 1) pops this far above ground
#define ENV3D_FRONT   3.6f    // a "frontmost" tier (engine priority 0) pops this far
#define TDEPTH_MAX    4.2f    // clamp the per-tile scenery depth (elevation plane + feature)
#define POP_DISP_MAX  6.5f    // hard comfort ceiling on any element's forward disparity (px @ full slider)
#define UIPOP3D_PX    5.0f    // BG0 window panels (dialogs/menus): strong clean-copy pop
#define RAMP_AT(gy)   (POP3D_RAMP_PX * (float)(gy) / (float)GBA_H)

// Map a Gen-3 map-grid / object elevation tier (0..15) to a forward depth PLANE. Baked from the
// engine's own sElevationToPriority {2,2,2,2,1,2,1,2,1,2,1,2,1,0,0,2}: priority 2 = ground (0),
// priority 1 = a raised tier, priority 0 = frontmost -> our depth order matches what the game draws
// in front ("C"). Tiers 0 (TRANSITION) and 15 (MULTI_LEVEL) are -1 = "no own height" -> interpolated
// from neighbours so stairs/ledges/bridges ramp between the tiers they join.
static const float ELEV_PLANE[16] = {
	-1.f, 0.f, 0.f, 0.f, ENV3D_RAISED, 0.f, ENV3D_RAISED, 0.f,
	ENV3D_RAISED, 0.f, ENV3D_RAISED, 0.f, ENV3D_RAISED, ENV3D_FRONT, ENV3D_FRONT, -1.f
};
static inline float elev_plane(int e) { return (e >= 0 && e < 16 && ELEV_PLANE[e] >= 0.f) ? ELEV_PLANE[e] : 0.f; }
static inline float clamp_disp(float v) { return v > POP_DISP_MAX ? POP_DISP_MAX : v; }
static void calc_xform(int mode, float sW, float sH, float* ox, float* oy, float* sx, float* sy) {
	if (mode == SCALE_1X)           { *sx = *sy = 1.0f; }
	else if (mode == SCALE_STRETCH) { *sx = sW / GBA_W; *sy = sH / GBA_H; }
	else { float f = (sW / GBA_W < sH / GBA_H) ? sW / GBA_W : sH / GBA_H; *sx = *sy = f; }
	*ox = (sW - GBA_W * *sx) / 2.0f;
	*oy = (sH - GBA_H * *sy) / 2.0f;
}
// Re-draw a sub-rect of a frame texture shifted horizontally (the per-eye pop overdraw).
// pscale/texDim pick the source: the raw 256px GBA tex (1/256) or the sharp-bilinear prescale
// (PRESCALE/PRE_TEX) so popped text stays crisp. The destination is clipped to the on-screen
// frame box, so a shifted pop never bleeds into the letterbox (per-eye rivalry on the border).
static void draw_pop_tex(C3D_Tex* tex, int pscale, int texDim, GPU_TEXTURE_FILTER_PARAM filt,
                         int gx, int gy, int gw, int gh, float ox, float oy, float sx, float sy, float xoff) {
	if (gx < 0) { gw += gx; gx = 0; }
	if (gy < 0) { gh += gy; gy = 0; }
	if (gx + gw > GBA_W) gw = GBA_W - gx;
	if (gy + gh > GBA_H) gh = GBA_H - gy;
	float xs = xoff / sx;                                   // shift expressed in GBA pixels
	int loCol = (int)ceilf(-xs);              if (gx < loCol) { gw -= (loCol - gx); gx = loCol; }
	int hiCol = (int)floorf((float)GBA_W - xs); if (gx + gw > hiCol) gw = hiCol - gx;
	if (gw <= 0 || gh <= 0) return;
	float t = (float)pscale, D = (float)texDim;
	Tex3DS_SubTexture st = { (u16)(gw * pscale), (u16)(gh * pscale),
	                        gx * t / D, 1.0f - gy * t / D, (gx + gw) * t / D, 1.0f - (gy + gh) * t / D };
	C2D_Image img = { tex, &st };
	C3D_TexSetFilter(tex, filt, filt);
	C2D_DrawImageAt(img, ox + gx * sx + xoff, oy + gy * sy, 0.0f, NULL, sx / t, sy / t);
}
static void draw_pop(C3D_Tex* tex, int gx, int gy, int gw, int gh,
                     float ox, float oy, float sx, float sy, float xoff) {
	draw_pop_tex(tex, 1, 256, GPU_NEAREST, gx, gy, gw, gh, ox, oy, sx, sy, xoff);
}

// Smoothed floor depth (px) under a screen pixel, so a character can pop ABOVE the very floor
// (ground ramp already separate; this adds the scenery/elevation extra) it stands on.
static float floor_at(const DepthSnap* d, int gx_px, int gy_px) {
	int c = gx_px / 16, r = gy_px / 16;   // sprite floor = its own grid cell (do NOT scroll-shift: the player sits at a fixed cell)
	if (c < 0) c = 0; else if (c > 14) c = 14;
	if (r < 0) r = 0; else if (r > 9) r = 9;
	return d->tdepth[r][c];
}

// Pop the captured characters forward on one eye (shifted sub-rect overdraws on the flat frame).
// eyeSl = +slider (left eye) / -slider (right). Each sprite pops CHAR_PX above the floor ramp
// at its FEET, so the ground plane can never pop over a character standing on it.
static void pop_eye(C3D_RenderTarget* tgt, EmuInstance* g, const DepthSnap* d, int mode, float eyeSl) {
	float ox, oy, sx, sy; calc_xform(mode, 400.0f, 240.0f, &ox, &oy, &sx, &sy);
	C2D_SceneBegin(tgt);
	// Every sprite (NPC, the player, sprite-objects) stands UP out of the ground: its feet sit at
	// the ground depth and the disparity ramps to +STANDUP at its head -> a leaning standee. Applied
	// precisely PER SPRITE (no special rectangle for the centre/player); drawn as a few horizontal
	// strips so the head pops while the base stays anchored to the floor.
	for (int i = 0; i < d->nspr; i++) {
		int x0 = d->spr[i].x, y0 = d->spr[i].y, w = d->spr[i].w, h = d->spr[i].h;
		int cx = x0 + w / 2, fy = y0 + h;
		// Feet floor = smoothed grid depth there, RAISED to the sprite's own object-elevation plane
		// when matched (authoritative on stairs); max keeps the standee never behind its feet tile.
		float floorD = floor_at(d, cx, fy);
		if (d->spr[i].elev != 0xFF) { float pl = elev_plane(d->spr[i].elev); if (pl > floorD) floorD = pl; }
		float base = RAMP_AT(fy) + floorD;                          // disparity at the grounded feet
		int strips = (h + 7) / 8; if (strips < 1) strips = 1;
		for (int s = 0; s < strips; s++) {
			int yy = y0 + s * h / strips, hh = y0 + (s + 1) * h / strips - yy;
			float tmid = ((float)yy + 0.5f * (float)hh - (float)y0) / (float)h;   // 0 head .. 1 feet
			float disp = eyeSl * clamp_disp(base + POP3D_STANDUP * (1.0f - tmid));
			draw_pop(&g->tex, x0, yy, w, hh, ox, oy, sx, sy, disp);
		}
	}
}

// LOGGING ONLY: compute the per-sprite feet/head disparity STATS that pop_eye would render, so the gs log
// proves the 3D effect (not just sprite counts). Mirrors pop_eye EXACTLY: feet base = RAMP_AT(fy)+floorD
// (raised to the sprite's own elevation plane when matched), head = clamp(base+POP3D_STANDUP). All values
// are px @ FULL slider (the unit BEFORE pop_eye multiplies by eyeSl), so they're slider-independent.
// 'tall-is-taller': head must exceed feet by ~POP3D_STANDUP per sprite (after clamping; a sprite already at
// the comfort ceiling can't stand taller, counted as fail). 'front-is-front': sorting sprites by screen-y
// (lower = closer) the feet disparity must be non-decreasing (closer pops >=). Never gates rendering.
static void depth_disparity_stats(DepthSnap* d) {
	d->feetMin = d->feetMax = d->headMin = d->headMax = 0.0f;
	d->tallOk = d->tallFail = 0; d->orderOk = true;
	if (d->nspr <= 0) return;
	float feet[DEPTH_MAX_SPR]; int fy[DEPTH_MAX_SPR];
	for (int i = 0; i < d->nspr; i++) {
		int x0 = d->spr[i].x, y0 = d->spr[i].y, w = d->spr[i].w, h = d->spr[i].h;
		int cx = x0 + w / 2, feetY = y0 + h;
		float floorD = floor_at(d, cx, feetY);
		if (d->spr[i].elev != 0xFF) { float pl = elev_plane(d->spr[i].elev); if (pl > floorD) floorD = pl; }
		float base = RAMP_AT(feetY) + floorD;                 // disparity at the grounded feet (px @ full slider)
		float footDisp = clamp_disp(base);                    // pop_eye clamps each strip; feet strip = base
		float headDisp = clamp_disp(base + POP3D_STANDUP);    // head strip (tmid->0)
		feet[i] = footDisp; fy[i] = feetY;
		if (i == 0 || footDisp < d->feetMin) d->feetMin = footDisp;
		if (i == 0 || footDisp > d->feetMax) d->feetMax = footDisp;
		if (i == 0 || headDisp < d->headMin) d->headMin = headDisp;
		if (i == 0 || headDisp > d->headMax) d->headMax = headDisp;
		if (headDisp - footDisp >= POP3D_STANDUP * 0.5f) d->tallOk++; else d->tallFail++;   // tall renders taller
	}
	// front-is-front: a sprite lower on screen (larger feet screen-y => closer) must pop >= one above it.
	// Insertion-sort indices by feetY ascending (cheap, n<=32), then check feet disparity is non-decreasing.
	int idx[DEPTH_MAX_SPR]; for (int i = 0; i < d->nspr; i++) idx[i] = i;
	for (int i = 1; i < d->nspr; i++) { int v = idx[i], j = i - 1;
		while (j >= 0 && fy[idx[j]] > fy[v]) { idx[j + 1] = idx[j]; j--; } idx[j + 1] = v; }
	for (int i = 1; i < d->nspr; i++)
		if (feet[idx[i]] + 0.01f < feet[idx[i - 1]]) { d->orderOk = false; break; }   // closer popped LESS -> ordering broke
}

// M4: metatile id -> layer type (0 NORMAL=foreground / 1 COVERED=ground / 2 SPLIT=mid) via the
// gMapHeader -> MapLayout -> Tileset -> metatileAttributes chain. Cached per map + memoized by id.
static uint8_t metatile_layer(GbaCore* c, const GameProfile* p, uint16_t id) {
	static uint32_t cLayout = 0, cPri = 0, cSec = 0;
	static uint8_t  cLayer[1024], cValid[1024];
	if (!p->mapHeader || id >= 0x03FF) return 1;                 // no M4 / sentinel border -> ground
	uint32_t layoutP = gbacore_read32(c, p->mapHeader + 0x00);   // MapHeader.mapLayout
	if (layoutP != cLayout) {                                    // map changed -> rebuild bases + memo
		cLayout = layoutP;
		uint32_t tsP = gbacore_read32(c, layoutP + 0x10), tsS = gbacore_read32(c, layoutP + 0x14);
		cPri = tsP ? gbacore_read32(c, tsP + 0x10) : 0;            // Tileset.metatileAttributes
		cSec = tsS ? gbacore_read32(c, tsS + 0x10) : 0;
		memset(cValid, 0, sizeof cValid);
	}
	if (cValid[id]) return cLayer[id];
	uint32_t attrP = (id < 512) ? cPri : cSec;
	uint8_t  layer = 1;
	if (attrP) { uint16_t a = gbacore_read16(c, attrP + 2u * (uint32_t)((id < 512) ? id : id - 512)); layer = (a & 0xF000) >> 12; }
	cLayer[id] = layer; cValid[id] = 1;
	return layer;
}

// M4: build the smoothed scenery-depth grid for the visible 15x10 tiles (cores parked; safe reads).
static void build_depth_grid(GbaCore* core, const GameProfile* p, int px, int py, DepthSnap* d) {
	memset(d->tdepth, 0, sizeof d->tdepth);
	if (!core || !p || px < 0 || py < 0) return;   // metatile_layer handles mapHeader==0 (FR/LG collision depth)
	d->camX = p->fieldCamera ? (int)(int32_t)gbacore_read32(core, p->fieldCamera + 0x10) : 0;
	d->camY = p->fieldCamera ? (int)(int32_t)gbacore_read32(core, p->fieldCamera + 0x14) : 0;
	if (d->camX < -15 || d->camX > 15) d->camX = 0;   // guard garbage
	if (d->camY < -15 || d->camY > 15) d->camY = 0;
	int w = (int32_t)gbacore_read32(core, p->mapLayout + 0);
	int h = (int32_t)gbacore_read32(core, p->mapLayout + 4);
	uint32_t ptr = gbacore_read32(core, p->mapLayout + 8);
	if ((ptr >> 24) != 0x02 || w <= 0 || w > 512 || h <= 0 || h > 512) return;
	// Per visible tile: feature depth (layer-type) + elevation-plane depth. Elevation 0/15 (stairs/
	// ledges/bridges) carry no own height -> left "unknown" and filled below. (Border +7 and the -7
	// player-centring cancel, so screen col c == map gx px+c -- verified against the touch BFS.)
	float feat[10][15], ed[10][15]; bool has[10][15];
	for (int r = 0; r < 10; r++) for (int c = 0; c < 15; c++) {
		int gx = px + c, gy = py + r + 2;
		feat[r][c] = 0.0f; ed[r][c] = 0.0f; has[r][c] = false;
		if (gx >= 0 && gx < w && gy >= 0 && gy < h) {
			uint16_t e = gbacore_read16(core, ptr + 2u * (uint32_t)(gx + w * gy));
			uint8_t layer = metatile_layer(core, p, e & 0x03FF);
			// The metatile NORMAL *layer type* is the default compositing mode -> ~EVERY tile, so using
			// it as "foreground" floods the grid uniform and the whole map reads FLAT (the f~148/150
			// diagnostic caught exactly this). The real stand-up signal is COLLISION: a solid/impassable
			// tile is an object (pole/tree/wall/sign/fence); walkable ground is passable and stays flat.
			// Same 0x0C00 bits the touch BFS walks on -> proven + game-agnostic. Keep SPLIT (ledges /
			// grass edges) as a small extra; skip elevation 1 = surf water (impassable but flat).
			float f = (layer == 2) ? ENV3D_SPLIT : 0.0f;
			if ((e & 0x0C00) && (e >> 12) != 1) f = ENV3D_NORMAL;
			feat[r][c] = f;
			float pl = ELEV_PLANE[e >> 12];
			if (pl >= 0.0f) { ed[r][c] = pl; has[r][c] = true; }
		}
	}
	int nfg = 0; for (int r = 0; r < 10; r++) for (int c = 0; c < 15; c++) if (feat[r][c] > 0.0f) nfg++;
	d->nfg = nfg;   // HUD diagnostic
	// Fill stairs/ledges/bridges by relaxing from known neighbours, so depth RAMPS across them
	// instead of dropping to ground (Gauss-Seidel; cap spans the 10x15 grid, early-exits when stable).
	for (int pass = 0; pass < 16; pass++) {
		bool changed = false;
		for (int r = 0; r < 10; r++) for (int c = 0; c < 15; c++) if (!has[r][c]) {
			float sum = 0.0f; int n = 0;
			if (r > 0  && has[r-1][c]) { sum += ed[r-1][c]; n++; }
			if (r < 9  && has[r+1][c]) { sum += ed[r+1][c]; n++; }
			if (c > 0  && has[r][c-1]) { sum += ed[r][c-1]; n++; }
			if (c < 14 && has[r][c+1]) { sum += ed[r][c+1]; n++; }
			if (n > 0) { ed[r][c] = sum / (float)n; has[r][c] = true; changed = true; }
		}
		if (!changed) break;
	}
	// Combine elevation plane + feature depth per tile. NO blur: a 3x3 average diluted an isolated
	// object (a lone pole 4.0 -> ~0.44 -> invisible), which is why scenery read flat while the
	// un-blurred sprite standee popped. The vertex grid already interpolates between tiles for
	// smoothness, so crisp per-tile depth is fine. maxd = the strongest pop in view (HUD diagnostic).
	float maxd = 0.0f;
	for (int r = 0; r < 10; r++) for (int c = 0; c < 15; c++) {
		float t = ed[r][c] + feat[r][c];
		if (t > TDEPTH_MAX) t = TDEPTH_MAX;
		d->tdepth[r][c] = t;
		if (t > maxd) maxd = t;
	}
	d->maxd = maxd;
}

// M4: pop the foreground scenery tiles forward on one eye (shifted 16x16 sub-rect overdraws).
static void warp_scenery_eye(C3D_RenderTarget* tgt, EmuInstance* g, const DepthSnap* d, int mode, float dispUnit) {
	float ox, oy, sx, sy; calc_xform(mode, 400.0f, 240.0f, &ox, &oy, &sx, &sy);
	C2D_SceneBegin(tgt);
	for (int r = 0; r < 10; r++) for (int c = 0; c < 15; c++) {
		float dep = d->tdepth[r][c];
		if (dep > 0.01f) draw_pop(&g->tex, c * 16, r * 16, 16, 16, ox, oy, sx, sy, dispUnit * dep);
	}
}

// ---- HD-2D M4: time-of-day directional lighting + day/night color grade --------------------
// A per-tile light field modulates the overworld frame: a key light whose colour & direction
// track the time of day (warm low-angle dawn/dusk, bright neutral noon, dim blue night) shades
// the terrain by its surface normal (from the tdepth elevation field) and is drawn as a gouraud
// MULTIPLY mesh over the frame. Pure citro2d (no shader); analytic normals -- the study's
// AI-baked-normal atlas is the deferred optional upgrade. Cheap: ~150 gouraud quads per eye.
typedef struct { float r, g, b, lx, ly, lz, amb, dif; } LightEnv;

// Interpolate the key-light for hour-of-day hf in [0,24). L is a screen-space direction
// (x: +east/-west, y: +down, z: out toward the viewer); ground faces +z.
static LightEnv light_for_hour(float hf) {
	// Kept BRIGHT on purpose: high ambient so the focused game never reads as "darkened" -- the time
	// of day is a SUBTLE colour/lean, not a dimming (a MULTIPLY mesh can only darken, so we stay near
	// white). Low diffuse = gentle slope shading only.
	static const float K[][9] = {   // hour, r,g,b(0..1), lx,ly,lz, ambient, diffuse
		{  0.f, 0.86f,0.90f,1.00f,  0.00f,-0.25f,0.97f, 0.88f,0.12f },   // night (subtle cool, still bright)
		{  5.f, 0.90f,0.92f,1.00f,  0.50f,-0.20f,0.84f, 0.90f,0.12f },   // pre-dawn
		{  7.f, 1.00f,0.95f,0.86f,  0.72f,-0.18f,0.67f, 0.94f,0.14f },   // dawn (subtle warm)
		{ 12.f, 1.00f,1.00f,1.00f,  0.05f,-0.10f,0.99f, 1.00f,0.10f },   // noon (full bright neutral)
		{ 17.f, 1.00f,0.95f,0.86f, -0.62f,-0.18f,0.76f, 0.95f,0.14f },   // golden hour (subtle warm)
		{ 19.f, 1.00f,0.90f,0.82f, -0.74f,-0.16f,0.65f, 0.90f,0.14f },   // dusk (subtle warm)
		{ 21.f, 0.88f,0.91f,1.00f, -0.20f,-0.22f,0.95f, 0.88f,0.12f },   // night falls
		{ 24.f, 0.86f,0.90f,1.00f,  0.00f,-0.25f,0.97f, 0.88f,0.12f },   // wrap == 0h
	};
	int n = sizeof K / sizeof K[0], i = 0;
	while (i < n - 1 && hf >= K[i + 1][0]) i++;
	const float* a = K[i]; const float* b = K[i + 1];
	float u = (b[0] > a[0]) ? (hf - a[0]) / (b[0] - a[0]) : 0.0f;
	LightEnv e;
	e.r = a[1]+(b[1]-a[1])*u; e.g = a[2]+(b[2]-a[2])*u; e.b = a[3]+(b[3]-a[3])*u;
	e.lx= a[4]+(b[4]-a[4])*u; e.ly= a[5]+(b[5]-a[5])*u; e.lz= a[6]+(b[6]-a[6])*u;
	e.amb=a[7]+(b[7]-a[7])*u; e.dif=a[8]+(b[8]-a[8])*u;
	float il = 1.0f / sqrtf(e.lx*e.lx + e.ly*e.ly + e.lz*e.lz + 1e-6f);
	e.lx*=il; e.ly*=il; e.lz*=il;
	return e;
}

// Per-vertex tint = lightColor * (ambient + diffuse*max(0,N.L)); N from the tdepth gradient at
// the grid vertex (raised terrain catches side light). Returned as a citro2d colour for MULTIPLY.
static u32 light_vert(const DepthSnap* d, const LightEnv* e, int vr, int vc) {
	#define LCELL(R,C) d->tdepth[(R)<0?0:((R)>9?9:(R))][(C)<0?0:((C)>14?14:(C))]
	float lf = (LCELL(vr-1, vc-1) + LCELL(vr, vc-1)) * 0.5f;   // left  column avg
	float rt = (LCELL(vr-1, vc)   + LCELL(vr, vc))   * 0.5f;   // right column avg
	float up = (LCELL(vr-1, vc-1) + LCELL(vr-1, vc)) * 0.5f;   // upper row avg
	float dn = (LCELL(vr, vc-1)   + LCELL(vr, vc))   * 0.5f;   // lower row avg
	#undef LCELL
	const float kSlope = 0.18f;                       // depth-px -> normal tilt
	float nx = -(rt - lf) * kSlope, ny = -(dn - up) * kSlope, nz = 1.0f;
	float ndl = (nx*e->lx + ny*e->ly + nz*e->lz) / sqrtf(nx*nx + ny*ny + nz*nz);
	if (ndl < 0.0f) ndl = 0.0f;
	float s = e->amb + e->dif * ndl;
	int R = (int)(e->r*s*255.0f + 0.5f), G = (int)(e->g*s*255.0f + 0.5f), B = (int)(e->b*s*255.0f + 0.5f);
	if (R > 255) R = 255; if (G > 255) G = 255; if (B > 255) B = 255;
	return C2D_Color32((u8)R, (u8)G, (u8)B, 0xFF);
}

// Draw the light field over one eye as a MULTIPLY gouraud mesh (15x10 quads over the frame box).
static void light_pass(C3D_RenderTarget* tgt, const DepthSnap* d, int mode, const LightEnv* e) {
	float ox, oy, sx, sy; calc_xform(mode, 400.0f, 240.0f, &ox, &oy, &sx, &sy);
	u32 col[11][16];
	for (int vr = 0; vr <= 10; vr++) for (int vc = 0; vc <= 15; vc++) col[vr][vc] = light_vert(d, e, vr, vc);
	C2D_SceneBegin(tgt);
	C2D_Flush();   // commit the pending image batch before swapping the blend equation
	C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_DST_COLOR, GPU_ZERO, GPU_DST_COLOR, GPU_ZERO);  // dst*src = MULTIPLY
	for (int r = 0; r < 10; r++) for (int c = 0; c < 15; c++)
		C2D_DrawRectangle(ox + c*16*sx, oy + r*16*sy, 0.0f, 16*sx, 16*sy,
		                  col[r][c], col[r][c+1], col[r+1][c], col[r+1][c+1]);
	C2D_Flush();   // commit the mesh while MULTIPLY is still active, then restore citro2d's blend
	C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA);
}

// M2: continuous vertex-grid scenery warp (one mesh per eye). Verts sit at 16px tile corners
// and shift by the smoothed tile depth, so depth steps STRETCH the texture between cells
// instead of tearing at tile edges (the per-tile quad shift's artifact). citro2d has no mesh
// path, so this is a raw C3D draw with a passthrough shader (warp.v.pica); the warped X
// positions are CPU-computed (176 verts/eye) and the GPU is handed back via C2D_Prepare.
#define WARP_COLS  15
#define WARP_ROWS  10
#define WARP_VERTS ((WARP_COLS + 1) * (WARP_ROWS + 1))   // 16x11 = 176
#define WARP_IDX   (WARP_COLS * WARP_ROWS * 6)           // 900
typedef struct { float x, y, u, v; } WarpVert;
static DVLB_s*         warpDvlb;
static shaderProgram_s warpProg;
static int             warpProjLoc = -1;
static WarpVert*       warpVbo;   // linearAlloc; one slab per eye, rewritten per frame (SYNCDRAW-safe)
static u16*            warpIbo;   // static triangle indices
static WarpVert*       bloomVbo;  // M3 bloom: 3 quads (bright pass + additive L/R)
static bool            warpOk;

static void warp_grid_init(void) {
	warpDvlb = DVLB_ParseFile((u32*)warp_shbin, warp_shbin_size);
	if (!warpDvlb) return;
	shaderProgramInit(&warpProg);
	shaderProgramSetVsh(&warpProg, &warpDvlb->DVLE[0]);
	warpProjLoc = shaderInstanceGetUniformLocation(warpProg.vertexShader, "projection");
	warpVbo = (WarpVert*)linearAlloc(sizeof(WarpVert) * WARP_VERTS * 2);
	warpIbo = (u16*)linearAlloc(sizeof(u16) * WARP_IDX);
	bloomVbo = (WarpVert*)linearAlloc(sizeof(WarpVert) * 12);   // M3 bloom quads
	if (!warpVbo || !warpIbo || !bloomVbo || warpProjLoc < 0) return;   // warpOk stays false -> fallbacks
	int n = 0;
	for (int r = 0; r < WARP_ROWS; r++) for (int c = 0; c < WARP_COLS; c++) {
		u16 tl = (u16)(r * (WARP_COLS + 1) + c), tr = tl + 1;
		u16 bl = tl + (WARP_COLS + 1), br = bl + 1;
		warpIbo[n++] = tl; warpIbo[n++] = bl; warpIbo[n++] = tr;
		warpIbo[n++] = tr; warpIbo[n++] = bl; warpIbo[n++] = br;
	}
	GSPGPU_FlushDataCache(warpIbo, sizeof(u16) * WARP_IDX);
	warpOk = true;
}

static void warp_grid_fini(void) {
	if (warpVbo) linearFree(warpVbo);
	if (warpIbo) linearFree(warpIbo);
	if (bloomVbo) linearFree(bloomVbo);
	if (warpDvlb) { shaderProgramFree(&warpProg); DVLB_Free(warpDvlb); }
}

// ---- Phase 14 (HD-2D "diorama" tilt): the perspective mesh draw -----------------------------
// Spec: docs/phase14-tilt/SPEC-render.md R3 (the pass + budget), R4 (composition), R5 (the
// shader). ALL the math lives in source/tilt.{c,h} — pure C, host-tested by test/host/test_tilt.c
// (CLAUDE.md rule #4 / PHASE.md invariant 6). Nothing GPU-shaped is in that module and no
// projection math is in here: its entire contract with the GPU is (x, y, iq, u, v, rgba) floats
// (SPEC-render R6.7).
//
// WHY ONE ORDINARY TEXTURED DRAW IS THE WHOLE EFFECT. The tilted world is a plane under a pinhole
// camera, i.e. a planar homography (gen1recomp src/render/Tilt.lua:120-130, read out in
// docs/kb/external/gen1-render.md finding 1). gen1recomp has to reconstruct the projective divide
// in a FRAGMENT shader (Renderer.lua:456-471) only because LOVE is a 2D API with no clip-space w.
// The PICA200 has NO fragment shader at all (docs/kb/hd2d-octopath-3d.md §0.1) and does not need
// one: emit a real per-vertex clip w = 1/q and the fixed-function rasterizer interpolates attr/w
// and 1/w linearly in screen space and divides per pixel, which IS their trick (SPEC-render R1.3;
// agreement with their closed form proven to 6e-14 px in R1.9). verify-on-hw: that argument is
// asserted by construction on the CPU side (test_tilt TEST 8/10) and unproven until a real frame
// is rasterized.
// (Slice T2's temporary `TILT_DEV_LEVEL` compile-time drive is GONE as of slice T4: the real
// control is now g_prefs.tiltLevel — the ENHANCE-tab PK_SEG row, persisted through Settings.tilt
// — exactly as T2's decision D8 said it would be. It ships at level 0 (SPEC-integration I5.8), so
// the default binary is still inert and PHASE.md invariant 1 holds for every existing user.)

// R3.6 / open question O4 — SLICE-T2 DECISION: **SPILL**, i.e. no scissor (the sanctioned
// fallback, R3.6.2). Two reasons, in order of weight:
//  1. R2.5.3's argument for the scissor is *stereo-specific*: content present in one eye and
//     absent in the other at the frame border causes binocular rivalry (main.c:718-719). Under
//     rule G10 (SPEC-integration I5.6) tilt and stereo are MUTUALLY EXCLUSIVE — when the tilt is
//     up, both eyes get the identical tilted image — so the rivalry the scissor buys off cannot
//     occur in the shipped configuration. The argument is vacuous here, not merely outweighed.
//  2. The logical->physical quarter-turn for C3D_SetScissor is INFERRED, not documented in any
//     header on this machine, and R3.6.1 says in terms: prove it against a known quadrant first,
//     "Do not ship it unproven". Slice T2 is PC-green only, so it cannot.
// Spill also retains more of the frame (99.56 % vs 95.88 % at 15 deg on the top screen, R2.5.3)
// and costs zero GPU state. Its price: the widened NEAR edge covers the letterbox pillars at the
// bottom of the frame, so the image is a trapezoid rather than a rectangle. **verify-on-hw** — if
// that reads badly, prove the mapping in Azahar per R3.6.1 and set TILT_SCISSOR to 1.
#ifndef TILT_SCISSOR
#define TILT_SCISSOR 0
#endif

// R5.3: 36 B. The colour is 4 FLOATS, not 4 GPU_UNSIGNED_BYTEs — whether the PICA normalises
// integer attributes to 0..1 or hands over the raw 0..255 is not documented in any header here,
// and guessing wrong yields a silently 255x over-bright frame. 908 verts x 36 B x 3 images is
// ~100 KB of linearAlloc, which R3.5 budgets.
typedef struct { float x, y, iq; float u, v; float r, g, b, a; } TiltVert;

// R3.5.1: one slab per GAME IMAGE (0 = top-left eye, 1 = top-right eye, 2 = bottom), never
// shared — the GPU may read any queued buffer at any point up to C3D_FrameEnd, so rewriting one
// slab would corrupt an already-queued draw. This is the same "SYNCDRAW-safe" rule warpVbo
// follows (main.c:996). The per-image capacity is R3.5's worst case: 176 base grid + 512 pop
// strips + 24 UI pops + 16 DoF band corners + 4 bloom + 176 light mesh = ~908. Slice T2 writes
// only the first WARP_VERTS of each slab; the rest is headroom for R4.1/R4.3/R4.4/R4.5/R4.6.
#define TILT_IMAGES     3
#define TILT_SLAB_VERTS 908

// What render_game needs in order to take the tilt path instead of its flat blit. NULL = flat,
// and the flat branch is then today's code character for character (PHASE.md invariant 1).
typedef struct {
	const TiltView* v;    // this image's projection (tilt_view_init, rebuilt once per frame)
	u32             mod;  // dim tint, carried as the VERTEX COLOUR (R5.4 MODULATE stage)
	int             slab; // which per-image vertex slab (0 = top-L, 1 = top-R, 2 = bottom)
	// FIX PASS (2026-08-04, review finding 2) — geometry reuse. -1 = build this slab from the
	// projection. >= 0 = "the 176 projected vertices are ALREADY in that slab, built EARLIER THIS
	// FRAME": copy them and rewrite only the colour, or — when `slab == clone` — draw straight out
	// of it with no CPU write and no cache flush at all. The right eye is the case this exists for:
	// it shares the left eye's TiltView, mode and 400x240 rect, so calc_xform and tilt_project can
	// only produce the identical 176 vertices; rebuilding them cost a second full projection loop
	// (176 float divides), a second 6.3 KB write and a third GSPGPU_FlushDataCache IPC per tilted
	// frame, all to reproduce bytes we already had.
	// CONTRACT (the caller owns it): set `clone >= 0` only when the source slab was written this
	// frame by a draw with the SAME TiltView pointer, the same scale mode and the same screen rect.
	// Both current call sites satisfy it by construction (one `&tiltVw`, one `scaleMode[0]`, one
	// 400x240) and the left eye is always drawn first — and if the left eye bailed out (no core),
	// render_game's identical `!e->core` early-return means the right eye never reaches the draw.
	int             clone;
} TiltDraw;

// ---- slice T3: the GATE's snapshot (SPEC-integration I1.2) ----------------------------------
// The per-screen gate inputs that come from game RAM, snapshotted in the PARKED WINDOW and read
// in the render block — exactly the way DepthSnap depth3d already crosses that boundary
// (declared main.c:1891, filled main.c:2193+, consumed main.c:2810+). Index = SCREEN (0 = top,
// 1 = bottom), never game slot, because `swapped` maps games to screens and the gate is per
// screen (I1.8). No new game_read, no new profile_for, no new gbacore_read*: the two GameStates
// the gs-logger block already reads are reused (I1.1), so the whole gate costs two struct copies
// and adds no race class. Zero/-1 init means a frame that never fills it gates tilt OFF.
typedef struct { uint8_t ok, ctx, sb1Valid, textDlg; int16_t px; } TiltSnap;

// I1.4: the ONE place the GameCtx enum crosses into the header-free tilt module. If someone ever
// inserts a value ahead of GCTX_OVERWORLD in gamestate.h, this is a COMPILE ERROR rather than a
// tilt that silently engages in the wrong screen.
_Static_assert(GCTX_OVERWORLD == TILT_CTX_FIELD, "tilt.h TILT_CTX_FIELD drifted from GameCtx");
// Phase 15 / SPEC-data §0.3 + D4.6: presence shares that ONE crossing (fieldgate.h's
// FIELD_CTX_OVERWORLD is what TILT_CTX_FIELD is now an alias of) and computes in the SAME frame
// space this file blits. Both are pinned here so a drift is a compile error, not a mis-placed
// avatar.
_Static_assert(GCTX_OVERWORLD == FIELD_CTX_OVERWORLD, "fieldgate.h drifted from GameCtx");
_Static_assert(GBA_W == PRES_FRAME_W && GBA_H == PRES_FRAME_H,
               "presence frame space must BE the GBA frame (SPEC-data §0.3)");

static DVLB_s*         tiltDvlb;
static shaderProgram_s tiltProg;
static int             tiltProjLoc = -1;
static TiltVert*       tiltVbo;
static bool            tiltOk;   // false => the flat path, permanently (R3.5.3, invariant 1)

// R5.5.1: mirrors warp_grid_init exactly; any failure leaves tiltOk false and the renderer never
// looks at the tilt again. Runs AFTER warp_grid_init because the tilt reuses warpIbo (R1.4) —
// which warp_grid_init is what builds and cache-flushes — so !warpOk also means no tilt, which is
// the same conclusion R4.2 reaches from the other end ("no shader => no tilt => flat path").
static void tilt_init(void) {
	if (!warpOk) return;
	tiltDvlb = DVLB_ParseFile((u32*)tilt_shbin, tilt_shbin_size);
	if (!tiltDvlb) return;
	shaderProgramInit(&tiltProg);
	shaderProgramSetVsh(&tiltProg, &tiltDvlb->DVLE[0]);
	tiltProjLoc = shaderInstanceGetUniformLocation(tiltProg.vertexShader, "projection");
	tiltVbo = (TiltVert*)linearAlloc(sizeof(TiltVert) * TILT_SLAB_VERTS * TILT_IMAGES);
	if (!tiltVbo || tiltProjLoc < 0) return;   // tiltOk stays false -> flat forever
	tiltOk = true;
}

static void tilt_fini(void) {
	if (tiltVbo) linearFree(tiltVbo);
	if (tiltDvlb) { shaderProgramFree(&tiltProg); DVLB_Free(tiltDvlb); }
}

// The base mesh for one game image: the SAME 16x11 grid warp_grid_eye builds (main.c:1059-1067),
// each vertex pushed through the homography and then through the UNMODIFIED shared screen fit.
// R1.4: because u*q, w*q and q are affine over the whole plane, ANY tessellation reproduces the
// identical map — subdividing introduces no seam and no error — so keeping the grid instead of a
// 4-vertex quad costs nothing and is what will carry the per-vertex stereo displacement when R4.2
// lands. R1.7: tilt is computed in FRAME space and composed with calc_xform afterwards (affine o
// homography is still a homography), never baked per screen.
static void tilt_mesh_base(TiltVert* v, const TiltView* tv, u32 mod,
                           float ox, float oy, float sx, float sy) {
	const float mr = (float)( mod        & 0xFF) / 255.0f,   // C2D_Color32 packs r,g,b,a low->high
	            mg = (float)((mod >>  8) & 0xFF) / 255.0f,   // (c2d/base.h:102-105)
	            mb = (float)((mod >> 16) & 0xFF) / 255.0f,
	            ma = (float)((mod >> 24) & 0xFF) / 255.0f;
	for (int r = 0; r <= WARP_ROWS; r++) for (int c = 0; c <= WARP_COLS; c++) {
		TiltVert* w = &v[r * (WARP_COLS + 1) + c];
		float gx = (float)(c * 16), gy = (float)(r * 16);
		float fx, fy, q;
		tilt_project(tv, gx, gy, &fx, &fy, &q);   // frame -> frame (R1.6, bottom-anchored cover)
		w->x  = ox + fx * sx;                     // then calc_xform, untouched (R1.7)
		w->y  = oy + fy * sy;
		w->iq = 1.0f / q;                         // R1.3: emit the DEPTH 1/q, NEVER q
		w->u  = gx / 256.0f;                      // preTex UVs coincide: PRESCALE/PRE_TEX == 1/256
		w->v  = 1.0f - gy / 256.0f;
		w->r = mr; w->g = mg; w->b = mb; w->a = ma;
	}
}

// FIX PASS (review finding 2): the same mesh under a different dim tint. Geometry is copied
// verbatim — NOT re-projected — so the two eyes cannot drift by a rounding bit, which is what the
// stereo pair needs anyway (a right eye whose vertices disagreed with the left by even a float ULP
// would be a real per-eye difference on the parallax barrier). One pass, no memcpy-then-overwrite.
static void tilt_mesh_recolor(TiltVert* dst, const TiltVert* src, u32 mod) {
	const float mr = (float)( mod        & 0xFF) / 255.0f,
	            mg = (float)((mod >>  8) & 0xFF) / 255.0f,
	            mb = (float)((mod >> 16) & 0xFF) / 255.0f,
	            ma = (float)((mod >> 24) & 0xFF) / 255.0f;
	for (int i = 0; i < WARP_VERTS; i++) {
		dst[i].x = src[i].x; dst[i].y = src[i].y; dst[i].iq = src[i].iq;
		dst[i].u = src[i].u; dst[i].v = src[i].v;
		dst[i].r = mr; dst[i].g = mg; dst[i].b = mb; dst[i].a = ma;
	}
}

// ONE raw-C3D escape/return per GAME IMAGE (R3.3) — the sequence proven at main.c:1069-1092, with
// the tilt program bound and a third (colour) attribute. Slice T2 emits ONE draw inside it: the
// base mesh, which REPLACES render_game's final blit rather than compositing over it (R3.1). The
// pop / UI-pop / DoF / bloom / light draws join THIS SAME block in later slices — that merge is
// where R3.4's "-2 escapes vs today under stereo" comes from, and it is why the escape lives here
// and not inside each effect. Leaves `tgt` bound for the caller's HUD (R3.1.4).
static void tilt_draw_image(C3D_RenderTarget* tgt, C3D_Tex* src, const TiltDraw* td,
                            float screenW, float screenH, int mode) {
	float ox, oy, sx, sy; calc_xform(mode, screenW, screenH, &ox, &oy, &sx, &sy);
	TiltVert* v = tiltVbo + (size_t)td->slab * TILT_SLAB_VERTS;
	// FIX PASS (review finding 2): build, clone-and-recolour, or reuse outright — see TiltDraw.
	if (td->clone < 0) {
		tilt_mesh_base(v, td->v, td->mod, ox, oy, sx, sy);
		GSPGPU_FlushDataCache(v, sizeof(TiltVert) * WARP_VERTS);   // R3.5.2: ONLY the bytes written
	} else {
		const TiltVert* s = tiltVbo + (size_t)td->clone * TILT_SLAB_VERTS;
		if (s != v) {
			tilt_mesh_recolor(v, s, td->mod);
			GSPGPU_FlushDataCache(v, sizeof(TiltVert) * WARP_VERTS);
		}
		// s == v: the requested slab IS the source — same geometry AND same tint, already written
		// and already flushed by the earlier draw. Nothing to do; the GPU reads it a second time.
		// SYNCDRAW-safe (R3.5.1): a slab is written at most once per frame and never rewritten
		// while a draw referencing it is still queued, which is the only thing that rule forbids.
	}
	C2D_Flush();                        // submit citro2d's pending batch (the prescale pass 0)
	C3D_FrameDrawOn(tgt);
	C3D_BindProgram(&tiltProg);
	// R1.5: the ORTHO screen-pixel matrix every other pass already uses — NOT Mtx_PerspTilt. An
	// ortho P is linear with last row (0,0,0,1), so P*(x*iq, y*iq, 0, iq) = iq*[P*(x,y,0,1)]: the
	// NDC is exactly the flat screen point and the entire projective content lands in w_clip.
	C3D_Mtx proj;
	Mtx_OrthoTilt(&proj, 0.0f, screenW, screenH, 0.0f, 1.0f, -1.0f, true);
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, tiltProjLoc, &proj);
	C3D_AttrInfo* ai = C3D_GetAttrInfo();
	AttrInfo_Init(ai);
	AttrInfo_AddLoader(ai, 0, GPU_FLOAT, 3);   // v0 = (x, y, iq)   screen px + homogeneous scale
	AttrInfo_AddLoader(ai, 1, GPU_FLOAT, 2);   // v1 = (u, v)
	AttrInfo_AddLoader(ai, 2, GPU_FLOAT, 4);   // v2 = (r, g, b, a)
	C3D_BufInfo* bi = C3D_GetBufInfo();
	BufInfo_Init(bi);
	BufInfo_Add(bi, v, sizeof(TiltVert), 3, 0x210);   // 3 attrs, permutation nibbles 0/1/2 (R5.3)
	// R3.2.1: the tilted draw is ALWAYS GPU_LINEAR — gen1's exact split (Renderer.lua:514-521:
	// warped canvas LINEAR, flat path NEAREST). Set AFTER C2D_Flush so it can never retro-apply to
	// a still-pending citro2d batch (the prescale pass wants NEAREST). The flat path re-sets its
	// own filter every frame (main.c:1320/1338), so this never leaks back.
	C3D_TexSetFilter(src, GPU_LINEAR, GPU_LINEAR);
	C3D_TexBind(0, src);
	C3D_TexEnv* env = C3D_GetTexEnv(0);       // R5.4: MODULATE(TEXTURE0, PRIMARY_COLOR)
	C3D_TexEnvInit(env);
	C3D_TexEnvSrc(env, C3D_RGB, GPU_TEXTURE0, GPU_PRIMARY_COLOR, 0);
	C3D_TexEnvFunc(env, C3D_RGB, GPU_MODULATE);   // x vertex colour = the unfocused dim-tint analog
	C3D_TexEnvSrc(env, C3D_Alpha, GPU_PRIMARY_COLOR, 0, 0);
	C3D_TexEnvFunc(env, C3D_Alpha, GPU_REPLACE);  // opaque (vertex a = 1) -> src replaces dst
	C3D_TexEnvInit(C3D_GetTexEnv(1));   // R5.4.1: a stale stage 1 silently poisons the result
	C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
	C3D_CullFace(GPU_CULL_NONE);        // the trapezoid's winding varies with the angle
#if TILT_SCISSOR
	{   // R3.6.1 expected mapping — the framebuffer is the screen rotated a quarter turn (which is
		// exactly why Mtx_OrthoTilt exists, c3d/maths.h:530-542). UNPROVEN: see TILT_SCISSOR above.
		float x0 = ox, y0 = oy, x1 = ox + GBA_W * sx, y1 = oy + GBA_H * sy;
		if (x0 < 0.0f) x0 = 0.0f;            if (y0 < 0.0f) y0 = 0.0f;
		if (x1 > screenW) x1 = screenW;      if (y1 > screenH) y1 = screenH;
		u32 W = (u32)screenW;
		C3D_SetScissor(GPU_SCISSOR_NORMAL, (u32)y0, W - (u32)x1, (u32)y1, W - (u32)x0);
	}
#endif
	// NOTE on blend state: slice T2 deliberately sets NONE. citro2d's standard alpha blend is live
	// here (every pass that changes it restores it by hand — bloom_add at main.c:1292-1293,
	// light_pass at main.c:980 — because C2D_Prepare does NOT, R3.3.1), and with vertex alpha 1.0
	// that blend is an opaque overwrite, which is what the base mesh wants. The hand-restore
	// R3.3.1 mandates becomes mandatory the moment the additive bloom / MULTIPLY light draws join
	// this block (slice 3).
	C3D_DrawElements(GPU_TRIANGLES, WARP_IDX, C3D_UNSIGNED_SHORT, warpIbo);
#if TILT_SCISSOR
	C3D_SetScissor(GPU_SCISSOR_DISABLE, 0, 0, 0, 0);   // citro2d never resets it (R3.3.1)
#endif
	C2D_Prepare();                      // hand the GPU back (rebinds citro2d's shader/attrs/texenv)
	C2D_SceneBegin(tgt);                // R3.1.4: render_game's contract — leave the screen bound
}

// ---- Phase 15 slice M2: the co-op peer AVATAR ------------------------------------------------
// Spec: docs/phase15-presence/SPEC-avatar.md A1 (asset), A2 (the flat draw), A3 (the tilted draw).
// All the math is in source/presence_art.{c,h} + source/presence.{c,h} — pure C, host-tested by
// test/host/test_presence.c (CLAUDE.md rule #4 / PHASE.md invariant 8). This file owns exactly
// three things the PC cannot have: the texture, the C2D_DrawImageAt, and the tilt_project call.
//
// WHY THIS IS ONE ORDINARY citro2d SPRITE AND NOT PART OF THE TILT BLOCK (A3.3). A billboard needs
// no perspective divide: it is a PURE TRANSLATION of an upright quad (gen1-render.md finding 2), so
// all four corners share one anchor, q is constant across it, and the divide the tilt shader exists
// to perform is the identity. Joining the block would also force render_game to grow a presence
// parameter or the block to be re-opened later — one EXTRA escape, which is the thing SPEC-render
// R3.3 optimises away. R4.0.1 ("never draw a not-tilt-aware pass flat over a tilted base") is
// satisfied, not bypassed: the avatar IS tilt-aware, because its anchor rides the ground plane
// through tilt_project.
//
// PHASE.md invariant 1 lives here too: this pass READS. It never touches a GbaCore, never calls
// game_read, and physically cannot write emulated RAM.
static C3D_Tex   s_peerTex;              // the generated 128x128 RGBA8 placeholder sheet
static bool      s_peerTexOk;            // ...and whether it actually built
static bool      s_presenceOk;           // false => presence draws NOTHING, permanently, this run
static C2D_Image s_peerArt[2];           // real baked art per variant, when the PNGs are present
static bool      s_peerBaked[2];         // A1.5.4's drop-in switch, resolved once at init

// A1.5 / O-A5 (resolved): the placeholder is THEME-NEUTRAL. It could have used g_ui.acc, but the
// theme is switchable at runtime while this texture is baked once, so a themed placeholder would
// either go stale on a theme change or need a re-bake on the render thread. It is also the answer
// O-A5 asks for in the other direction: the avatar is WORLD content, and world content should not
// re-skin with the chrome (A1.4.2), so shipping the placeholder theme-neutral is what makes it
// look the same as the real art will. Words are 0xRRGGBBAA (presence_art.h).
#define PRES_PH_ACCENT 0x3C78C8FFu   // a flat mid blue: nothing in theme.c and no Gen-3 skin tone
#define PRES_PH_INK    0x201828FFu   // near-black ink for legs + the facing pip

// A1.5.4's drop-in art must also be the RIGHT SIZE. FIX PASS (review finding 2): the switch used to
// test only "does the widget exist", so a peer-walk-*.png exported at, say, 64x64 instead of 128x128
// silently bypassed the placeholder and then asked assets_img_cell for cells up to x=47, y=95 out of
// a 64 px subtexture. img_subrect (assets.c:115-125) computes UVs as `left + uw * (px / width)` with
// NO clamp, so every cell would sample past its own subtexture rect — garbage pixels from whatever
// sits next to it in the .t3x, with the wrap mode never set on a baked texture (only s_peerTex gets
// GPU_CLAMP_TO_EDGE). The sheet layout contract (A1.2/A1.3) puts the used extent of ONE variant at
// PRES_VAR_W x PRES_VAR_H = 48x96, so that is the real minimum; anything smaller falls back to the
// placeholder, which is exactly what A1.5.3's "obvious placeholder beats plausible-looking wrong"
// asks for.
static bool presence_art_fits(C2D_Image img) {
	return img.tex != NULL && img.subtex != NULL &&
	       (int)img.subtex->width >= PRES_VAR_W && (int)img.subtex->height >= PRES_VAR_H;
}

// A1.6: built ONCE, off the per-frame path. Any failure leaves s_presenceOk false and presence is
// off for the whole run — exactly the warpOk/tiltOk discipline (main.c:1032/1160, SPEC-render
// R3.5.3). Never a partial draw.
//
// FIX PASS (review finding 10): LAZY, not unconditional. This allocates 64 KB of linear heap for the
// staging buffer (freed) plus a permanent 64 KB RGBA8 texture held for the whole run, and the co-op
// pref ships DEFAULT-OFF — so a user who never turns it on used to pay 64 KB of linear heap next to
// the GBA framebuffers, the ~100 KB tilt VBO and the prescale textures, for nothing. PHASE.md
// invariant 6 ("costs nothing measurable when off") is about the frame budget, but the linear heap
// is the scarcer resource on this device. Idempotent: called at session start when the stored pref
// is on, and at the moment the pause-menu row turns it on. Both call sites are OUTSIDE
// C3D_FrameBegin/End (the menu action runs before the frame opens), which the blocking
// C3D_SyncDisplayTransfer below requires.
static void presence_art_ensure(void) {
	static bool built = false;
	if (built) return;
	built = true;

	// A1.5.4 — the drop-in switch, one line, one place. Dropping peer-walk-m.png / peer-walk-f.png
	// (128x128 RGBA) into design_handoff_3dgba_ui/assets_3ds/widgets/<theme>/ and re-running
	// tools/build_assets.sh makes them reachable here with ZERO changes to build_assets.sh,
	// assets_gen.h (regenerated), assets.c or the Makefile (A1.4/A1.4.1).
	s_peerArt[0]   = assets_ready() ? assets_wgt("peer-walk-m") : (C2D_Image){ 0 };
	s_peerArt[1]   = assets_ready() ? assets_wgt("peer-walk-f") : (C2D_Image){ 0 };
	s_peerBaked[0] = presence_art_fits(s_peerArt[0]);
	s_peerBaked[1] = presence_art_fits(s_peerArt[1]);

	u8* stage = (u8*)linearAlloc(PRES_SHEET_BYTES);
	if (stage) {
		if (C3D_TexInit(&s_peerTex, PRES_SHEET_DIM, PRES_SHEET_DIM, GPU_RGBA8)) {
			presence_art_build(stage, PRES_PH_ACCENT, PRES_PH_INK);
			GSPGPU_FlushDataCache(stage, PRES_SHEET_BYTES);
			// upload_frame's proven recipe (main.c:617-624), same flags with the format swapped to
			// RGBA8. IN == OUT format means the transfer is a pure linear->tiled reshuffle of 4-byte
			// units and converts no channels, so presence_art_build's byte order is what the sampler
			// reads. FLIP_VERT(0) + the "v = 1 - y/H" subtexture convention in the draw is inherited
			// verbatim from upload_frame / render_game's own subtexture (main.c:1589-1590).
			C3D_SyncDisplayTransfer((u32*)stage,          GX_BUFFER_DIM(PRES_SHEET_DIM, PRES_SHEET_DIM),
			                        (u32*)s_peerTex.data, GX_BUFFER_DIM(PRES_SHEET_DIM, PRES_SHEET_DIM),
			                        GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) |
			                        GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGBA8) |
			                        GX_TRANSFER_OUT_TILED(1) | GX_TRANSFER_FLIP_VERT(0));
			C3D_TexSetWrap(&s_peerTex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);   // a clipped cell must
			                                                       // never sample the opposite edge
			C3D_TexSetFilter(&s_peerTex, GPU_NEAREST, GPU_NEAREST);
			s_peerTexOk = true;
		}
		linearFree(stage);
	}
	// A1.6.2: presence may draw only if EVERY variant it could be asked for has a source — the
	// placeholder covers both, so real art for both also suffices. Anything less and presence is
	// off for the whole run rather than drawing a variant that is not there: the warpOk/tiltOk
	// discipline (main.c:1032/1160, SPEC-render R3.5.3), never a partial draw.
	s_presenceOk = s_peerTexOk || (s_peerBaked[0] && s_peerBaked[1]);
}

static void presence_art_fini(void) {
	if (s_peerTexOk) C3D_TexDelete(&s_peerTex);
	s_peerTexOk  = false;
	s_presenceOk = false;
}

// The peer avatars on ONE screen. Called from the three per-screen sequences (top-left eye,
// top-right eye, bottom) at the position A2.1 fixes: after ui_pop_eye and immediately BEFORE
// light_pass, so the avatar is (a) not overpainted by the pop passes' background re-draws,
// (b) not composited away by the DoF bands, and (c) graded by the time-of-day MULTIPLY like the
// world it stands in — the ordering-instead-of-a-second-colour-path analogue of gen1recomp's
// per-billboard zone-palette lookup at the foot anchor (gen1-render.md finding 2).
//
// `po`, `peer` and `pose` are ARRAYS of `nPeers` entries, one per peer slot; when the flagged 3-4
// player work lands, main.c calls presence_solve per slot and passes longer arrays — A3.4's whole
// point is that the ORDER exists in the shipped draw before it is needed. `nPeers` is EXPLICIT
// rather than assumed to be PRES_MAX_PEERS because main.c's presOut/presPose are indexed by GAME
// today, so it hands over single-element views; reading PRES_MAX_PEERS entries out of those the day
// the bound grows would be a silent out-of-bounds read, which is precisely the class of trap
// A6.1.1 names ("forgetting this is SILENT").
// `tv` NULL == tilt_active() false for this screen == the flat path, byte-identical (A3.2.4).
// `tint` is the SAME C2D_ImageTint pointer the matching render_game call received, so the avatar
// shares the unfocused screen's dim and the right eye's NULL exactly (A2.6.1/A2.6.2) — two eyes
// that disagree in brightness show up in the parallax barrier.
//
// Slice M3 adds `ch` — the NAMEPLATE and the "A - CARD" prompt (A4.4.1 / A5.4.1). They live in
// this call, not in the chrome block, for two reasons A2.5.2 gives: they are chrome ATTACHED TO
// WORLD CONTENT (their anchor is the projected head point, so they ride the tilt and the scroll
// with the avatar), and they must appear in BOTH EYES exactly as the avatar does — a pill visible
// in one eye only is binocular rivalry, the same defect draw_pop_tex's clip rule exists to avoid.
// `ch` NULL => avatar only, and the surfaces cost nothing (presence_surfaces is the ONE policy
// function; main.c never re-decides which pill is up).
typedef struct {
	C2D_TextBuf buf;       // the SHARED frame text buffer (A4.5.1: never allocate one here)
	const char* name;      // the CACHED decoded name (A4.5.2) — never decoded in the draw
	unsigned    surf;      // PRES_SURF_* from presence_surfaces()
	float       plateW;    // FIX PASS (review finding 9): the two chip WIDTHS, measured ONCE per
	float       promptW;   //   frame per screen where PresChrome is built, not three times per pill
	                       //   per draw pass. A centred pill needs its width before it can be
	                       //   placed, and ui_chip/ui_chip_fill measure internally, so the old
	                       //   "measure, then chip (which measures again, then draws)" shape did
	                       //   THREE C2D_TextParse+C2D_TextOptimize passes over the same string —
	                       //   x3 draw passes (top-left eye, top-right eye, bottom) x2 pills = 18
	                       //   parses per frame, on the render thread that feeds two saturated
	                       //   804 MHz workers. A4.5.2's name cache stopped the charmap decode but
	                       //   not the parse, which is the actual per-frame cost A7.2 H9 says to
	                       //   suspect first. Measured here + ui_chip*_w in the draw = 8.
} PresChrome;

static void presence_draw_screen(C3D_RenderTarget* tgt,
                                 const PresenceOut* po, const PeerPresence* peer, const int* pose,
                                 int nPeers, int mode, float screenW, float screenH,
                                 const TiltView* tv, const C2D_ImageTint* tint,
                                 const PresChrome* ch) {
	// A0.1.2: a closed gate costs ZERO work — no projection, no texture bind, no subtexture build,
	// no calc_xform. The frame must be bit-identical to a presence-off frame. `po->draw` IS the data
	// half's whole gate ladder (P-G1..P-G9 plus the D5.7 cull), menuOpen included via P-G2, so there
	// is deliberately no second gate check here that could disagree with it (A3.5's last row).
	if (!s_presenceOk || !po || !peer || !pose) return;
	if (nPeers > PRES_MAX_PEERS) nPeers = PRES_MAX_PEERS;   // the local arrays below are that size

	// A3.4 / gen1-render.md finding 2: ONE list of billboards, sorted ascending by the foot y
	// actually drawn at (the PROJECTED y under tilt, the flat anchor otherwise), tid breaking ties
	// deterministically so the order cannot flicker frame to frame. Everything the draw needs is
	// resolved here, so tilt_project runs exactly once per avatar per screen.
	struct { float sprX, sprY, dx, dy; PresArtCell cell; int gender; } av[PRES_MAX_PEERS];
	PresBillboard bb[PRES_MAX_PEERS];
	int nbb = 0;

	for (int slot = 0; slot < nPeers; slot++) {
		if (!po[slot].draw) continue;
		// The foot anchor arrives in GBA FRAME space with the tile delta AND the engine's own
		// sub-tile term already folded in for BOTH cameras (SPEC-data D5.3), which is what makes the
		// avatar track the BG scroll and glide tile-to-tile. The render half must not re-apply
		// either — doing so would double it (BUILDLOG M0 deviation 10). presence_art_rect is the
		// whole conversion, and it is A1.1's 16x32-with-the-head-above-the-tile convention.
		float ax = po[slot].footX, ay = po[slot].footY;
		float dx = 0.0f, dy = 0.0f;
		if (tv) {
			// A3.1: project the FOOT ANCHOR through the SHIPPED tilt view and translate the whole
			// upright sprite by the difference. `q` is read and THROWN AWAY: the art is deliberately
			// UNSCALED ("pixel-identical to flat mode", gen1-render.md finding 2) because any
			// non-integer scale on 16x32 pixel art turns it to mush — the most visible possible way
			// to make this look broken. `tv` is main.c's per-frame TiltView, built from the LIVE
			// TWEEN angle, so the feet track the moving ground for free; a CACHED view would slide
			// against it during every tween, which is the exact defect invariant 4 is about
			// (A3.2.2/A3.2.3). This is the only tilt entry point presence calls — no sinf/cosf, no
			// TILT_FOCAL, no re-derived projection anywhere in the presence path (A3.1.3).
			float fax, fay, q;
			tilt_project(tv, ax, ay, &fax, &fay, &q);
			(void)q;
			dx = fax - ax;
			dy = fay - ay;
		}
		presence_art_rect(ax, ay, &av[nbb].sprX, &av[nbb].sprY);
		presence_art_cell(po[slot].gender, po[slot].dir, pose[slot], &av[nbb].cell);
		av[nbb].dx = dx; av[nbb].dy = dy; av[nbb].gender = po[slot].gender ? 1 : 0;
		bb[nbb].fy   = ay + dy;
		bb[nbb].tid  = peer[slot].tid;
		bb[nbb].slot = (uint8_t)nbb;      // index INTO av[], so the sort carries the resolved draw
		nbb++;
	}
	if (nbb == 0) return;
	presence_art_ysort(bb, nbb);

	float ox, oy, sx, sy;
	calc_xform(mode, screenW, screenH, &ox, &oy, &sx, &sy);   // A2.2: the SAME screen fit every
	                                                          // other world pass uses, applied LAST

	// FIX PASS (review finding 4): how far the TILTED image reaches outside the flat 240x160 frame.
	// Zero when flat, so the flat path is byte-identical. Four tilt_project calls per screen per
	// frame, only while the tilt is engaged, and they answer the one question the clip needs: the
	// projection maps the flat frame ONTO the trapezoid, so the trapezoid's outermost excursion is
	// attained at the frame's own corners. Widening the clip box by that much is what stops the trim
	// being taken at a boundary the sprite has already been translated away from — without it a peer
	// near a frame edge lost a quarter of its body to a cut while standing on plainly visible
	// spilled ground (presence_art.h has the worked case). Widening rather than shifting is
	// deliberate: under tilt the BASE IMAGE spills past the frame rect too (TILT_SCISSOR 0,
	// main.c:1067-1074), and stereo is mutually exclusive with tilt (rule G10), so the
	// letterbox-rivalry reason for a hard boundary is vacuous exactly when this is nonzero.
	float spill = 0.0f;
	if (tv) {
		static const float CX[4] = { 0.0f, (float)GBA_W, 0.0f,          (float)GBA_W };
		static const float CY[4] = { 0.0f, 0.0f,         (float)GBA_H,  (float)GBA_H };
		for (int c = 0; c < 4; c++) {
			float px, py, q;
			tilt_project(tv, CX[c], CY[c], &px, &py, &q);
			float ex = (px < 0.0f) ? -px : (px - (float)GBA_W);
			float ey = (py < 0.0f) ? -py : (py - (float)GBA_H);
			if (ex > spill) spill = ex;
			if (ey > spill) spill = ey;
		}
	}

	for (int i = 0; i < nbb; i++) {
		int a = bb[i].slot;
		PresArtDraw d;
		// A3.2.1: the clip runs on the UNPROJECTED source rect and the TRIMMED rect is then
		// translated. Because the transform is a pure translation a trimmed rectangle stays a
		// rectangle, which is the whole reason this composition needs no scissor and no raw C3D.
		if (!presence_art_clip(av[a].sprX, av[a].sprY, av[a].cell.mirror, spill, &d)) continue;

		C2D_Image src;
		Tex3DS_SubTexture sub;
		if (s_peerBaked[av[a].gender]) {
			// Real art: cut the cell out of the baked sheet's own subtexture rect (A1.4.3). Each
			// widget PNG bakes into its OWN single-image .t3x (build_assets.sh:52-57), so the filter
			// set below touches no other widget's texture. The baked sheet holds ONE variant, so the
			// variant stride is subtracted back out — the sheet layout contract (A1.2/A1.3) is the
			// same for both paths and only the x origin differs.
			C2D_Image sheet = s_peerArt[av[a].gender];
			float cx = (float)(av[a].cell.x - av[a].gender * PRES_VAR_STRIDE + d.cx);
			if (!assets_img_cell(sheet, cx, (float)(av[a].cell.y + d.cy),
			                     (float)d.w, (float)d.h, &src, &sub)) continue;
			C3D_TexSetFilter(sheet.tex, tv ? GPU_LINEAR : GPU_NEAREST, tv ? GPU_LINEAR : GPU_NEAREST);
		} else if (!s_peerTexOk) {
			continue;              // this variant has neither baked art nor a placeholder to fall
			                       // back on (A1.6.2 makes that unreachable, but never guess pixels)
		} else {
			// The generated placeholder: a plain 128x128 texture, so the subtexture is built here in
			// render_game's own UV convention (main.c:1589-1590 — v = 1 - y/H, matching the
			// GX_TRANSFER_FLIP_VERT(0) the upload used).
			const float D = (float)PRES_SHEET_DIM;
			float u0 = (float)(av[a].cell.x + d.cx) / D,        v0 = 1.0f - (float)(av[a].cell.y + d.cy) / D;
			float u1 = (float)(av[a].cell.x + d.cx + d.w) / D,  v1 = 1.0f - (float)(av[a].cell.y + d.cy + d.h) / D;
			sub.width = (u16)d.w; sub.height = (u16)d.h;
			sub.left = u0; sub.top = v0; sub.right = u1; sub.bottom = v1;
			src.tex = &s_peerTex; src.subtex = &sub;
			// A1.6.3: NEAREST flat / LINEAR under tilt — the project's existing split (main.c:1257,
			// gen1-render.md finding 1). Re-set at every draw because the tilt block re-sets filters
			// on the textures it shares.
			C3D_TexSetFilter(&s_peerTex, tv ? GPU_LINEAR : GPU_NEAREST, tv ? GPU_LINEAR : GPU_NEAREST);
		}

		C2D_SceneBegin(tgt);   // defensive, matching pop_eye / light_pass (main.c:767 / :985)
		float X = ox + (d.x + av[a].dx) * sx, Y = oy + (d.y + av[a].dy) * sy;
		// A2.5.3: order IS the z here — citro2d does no depth sorting and C3D_DepthTest is off
		// (main.c:1266), so the depth argument stays 0.0f like every other call in this file.
		// A2.8: ZERO stereo disparity in this slice — the avatar sits exactly on the screen plane.
		// Deliberate: POP_DISP_MAX is a hardware-validated comfort ceiling SPEC-render R4.1.1
		// forbids relaxing by a rendering change, a flat overlay honestly reads as a flat overlay,
		// and adding an unproven disparity to an unproven overlay makes a bad photo un-diagnosable.
		// The upgrade is fully specified in A2.8 and is one line.
		if (av[a].cell.mirror) C2D_DrawImageAt(src, X + (float)d.w * sx, Y, 0.0f, tint, -sx, sy);  // A1.2.2
		else                   C2D_DrawImageAt(src, X,                   Y, 0.0f, tint,  sx, sy);

		// ---- slice M3: the nameplate + the prompt (A4.4.1 / A5.4.1) ----------------------------
		// Anchored to the sprite's HEAD point — the top-centre of the UNCLIPPED cell, translated by
		// the same tilt offset the art got — so both pills ride the world. Drawn in SCREEN space at
		// a FIXED pixel size (never multiplied by sx), because they must stay legible at SCALE_1X
		// where a 320x240 screen shows a 240x160 frame (A2.5.2), and clamped to the SCREEN rather
		// than to the frame rect: the sprite is world content and is trimmed at the game box, the
		// pills are chrome and simply stay on the panel.
		if (ch && ch->buf && ch->surf) {
			float headFx = av[a].sprX + (float)PRES_FOOT_DX + av[a].dx;   // cell top-centre, frame px
			float headFy = av[a].sprY + av[a].dy;
			float hx = ox + headFx * sx, hy = oy + headFy * sy;
			// FIX PASS (review finding 8): the stack is laid out and clamped AS A UNIT, in pure C
			// (presence_ui.c, host-tested by TEST 38). Two independent y-clamps collapsed the two
			// pills onto each other for any peer ~3+ tiles above the player, and put them inside the
			// translucent HUD bar that is drawn AFTER them. presence_pill_y keeps the 15 px
			// separation at every position and never enters the bar; presence_pill_x is the same
			// right-then-left clamp the old macro did, moved somewhere a PC test can reach it.
			int hasPlate  = (ch->surf & PRES_SURF_PLATE)  ? 1 : 0;
			int hasPrompt = (ch->surf & PRES_SURF_PROMPT) ? 1 : 0;
			float plateY, promptY;
			presence_pill_y(hy, hasPlate, hasPrompt, screenH, &plateY, &promptY);
			if (hasPlate)
				ui_chip_fill_w(ch->buf, ch->name,
				               presence_pill_x(hx, ch->plateW, screenW), plateY, ch->plateW,
				               THEME_HUD_BAR, g_ui.acc);
			if (hasPrompt)
				// A5.4.1: the passive prompt — no keypress needed to SEE it, so the player learns
				// the interaction by walking into it. "A - CARD", not "(A) CARD" with a circled A:
				// U+24B6 is not in the 3DS shared font (ui.h's own note about the missing arrow
				// glyphs, which is why ui_tri exists), and an un-renderable glyph in the one chip
				// that teaches the feature is worse than plain ASCII.
				ui_chip_w(ch->buf, PRES_PROMPT_TEXT,
				          presence_pill_x(hx, ch->promptW, screenW), promptY, ch->promptW, g_ui.acc);
		}
	}
}

// ---- slice M3: the CARD panel (SPEC-avatar A4.4.4) -------------------------------------------
// A PURE READ (A4.4.5 / PHASE.md invariant 1): every value on it comes out of the SAME
// PeerPresence record the avatar is already drawn from — no extra RAM read, no second
// parked-window pass, no link, no state machine. That is exactly why identity is in scope for
// this phase while trade and battle are not, and A5.5.3's one dim line says so on the card itself
// rather than in a README nobody opens.
//
// Drawn from the chrome block (after light_pass, inside `!menuOpen`), so unlike the nameplate it
// is NOT graded by the time-of-day multiply — it is a UI panel, not world content. Left eye only,
// exactly like the HUD bar and the toast: every other piece of chrome in this file already draws
// to `top` alone.
static void presence_draw_card(C2D_TextBuf buf, const PresCardText* t, float screenW) {
	if (!buf || !t) return;
	const float W = 214.0f, H = 80.0f;
	float x = (screenW - W) * 0.5f, y = 32.0f;    // under the HUD bar, over the game image

	// The language of draw_paused_summary (main.c's own panel idiom): a 9-sliced card fill when the
	// device-native art pack is present, the code-drawn panel when it is not — assets_ready() is
	// false on any build where data/ was not baked, and a card that only exists with the art pack
	// would be a diagnostic surface that disappears exactly when something is wrong.
	if (assets_ready()) assets_fill9("fill-card-r8", x, y, W, H, 8.0f);
	else                ui_panel(x, y, W, H, g_ui.panel, g_ui.line, 6.0f);
	ui_border(x, y, W, H, g_ui.acc, 1.0f);

	// A4.4.4's four rows, plus A5.5.3's disclosure:
	//    NILS                    M      <- FNT_SG_BOLD 14 px + the gender field
	//    ID  01234                      <- FNT_JBM_BOLD 10 px  (A4.3.2's %05u; NEVER the secret id)
	//    MAP 3-12   TILE 14,9           <- FNT_JBM_MED 8 px
	//    same map - read-only           <- FNT_JBM_MED 8 px, dim
	//    trade & battle use ...         <- FNT_JBM_MED 8 px, dim
	const float px = x + 10.0f;
	if (assets_ready()) {
		assets_text  (buf, FNT_SG_BOLD,  t->name,           px, y +  8.0f, 14.0f, g_ui.text);
		assets_text_r(buf, FNT_JBM_BOLD, presence_gender_label(t->gender),
		                                                    x + W - 10.0f, y + 10.0f, 10.0f, g_ui.acc);
		assets_text  (buf, FNT_JBM_MED,  "ID",              px, y + 29.0f,  8.0f, g_ui.dim);
		assets_text  (buf, FNT_JBM_BOLD, t->id,             px + 22.0f, y + 27.0f, 10.0f, g_ui.text);
		assets_text  (buf, FNT_JBM_MED,  t->loc,            px, y + 44.0f,  8.0f, g_ui.text);
		assets_text  (buf, FNT_JBM_MED,  PRES_CARD_READONLY_NOTE, px, y + 56.0f, 8.0f, g_ui.dim);
		// A5.5.3 — the sanctioned disclosure. NOT a greyed "Trade"/"Battle" button: a greyed button
		// reads as "coming in the next build", and this phase is not that (presence_ui.h carries the
		// four blockers and the recorded later design).
		assets_text  (buf, FNT_JBM_MED,  PRES_CARD_UNION_NOTE,    px, y + 67.0f, 8.0f, g_ui.dim);
	} else {
		ui_text  (buf, t->name,                              px, y +  7.0f, 0.42f, g_ui.text);
		ui_text_r(buf, presence_gender_label(t->gender),     x + W - 10.0f, y + 9.0f, 0.34f, g_ui.acc);
		ui_text  (buf, "ID",                                 px, y + 29.0f, 0.30f, g_ui.dim);
		ui_text  (buf, t->id,                                px + 22.0f, y + 28.0f, 0.34f, g_ui.text);
		ui_text  (buf, t->loc,                               px, y + 44.0f, 0.30f, g_ui.text);
		ui_text  (buf, PRES_CARD_READONLY_NOTE,              px, y + 56.0f, 0.28f, g_ui.dim);
		ui_text  (buf, PRES_CARD_UNION_NOTE,                 px, y + 67.0f, 0.28f, g_ui.dim);
	}
}

// Standee depth field: a vertex carries the depth of the tile(s) just BELOW it (grid row vr), so a
// foreground tile's TOP edge pops out while its BOTTOM edge (the next row down) sits at the ground.
// Every foreground object (pole / thin tree / rock tile) thus STANDS UP all over the map -- not only
// where it overlaps the player's sprite rect (the old 4-tile AVERAGE diluted an isolated tile down to
// near-ground, so lone poles went flat). max() over the two tiles below keeps thin verticals at full
// pop; a flat plateau (tiles below also raised) stays uniformly forward, leaning only at its front edge.
static float warp_vert_depth(const DepthSnap* d, int vr, int vc) {
	if (vr < 0 || vr >= WARP_ROWS) return 0.0f;                       // bottom screen edge -> grounded
	float dep = 0.0f;
	if (vc - 1 >= 0 && vc - 1 < WARP_COLS && d->tdepth[vr][vc - 1] > dep) dep = d->tdepth[vr][vc - 1];
	if (vc >= 0     && vc     < WARP_COLS && d->tdepth[vr][vc]     > dep) dep = d->tdepth[vr][vc];
	return dep;
}

// Sample the standee field at a FRACTIONAL grid position (bilinear), so a vertex's depth can be
// shifted by the camera's sub-tile scroll -> object pops track the smoothly scrolling map instead
// of snapping per whole tile (the walk "wobble").
static float warp_depth_at(const DepthSnap* d, float fr, float fc) {
	int r0 = (int)floorf(fr), c0 = (int)floorf(fc);
	float fy = fr - (float)r0, fx = fc - (float)c0;
	float d00 = warp_vert_depth(d, r0,     c0), d01 = warp_vert_depth(d, r0,     c0 + 1);
	float d10 = warp_vert_depth(d, r0 + 1, c0), d11 = warp_vert_depth(d, r0 + 1, c0 + 1);
	return (d00 * (1.0f - fx) + d01 * fx) * (1.0f - fy) + (d10 * (1.0f - fx) + d11 * fx) * fy;
}

static void warp_grid_eye(C3D_RenderTarget* tgt, EmuInstance* g, const DepthSnap* d, int mode,
                          float dispUnit, bool sharpPre, C3D_Tex* pre, u32 mod, int eye) {
	float ox, oy, sx, sy; calc_xform(mode, 400.0f, 240.0f, &ox, &oy, &sx, &sy);
	WarpVert* v = warpVbo + (eye ? WARP_VERTS : 0);
	float cxf = (float)d->camX / 16.0f, cyf = (float)d->camY / 16.0f;   // sub-tile scroll
	for (int r = 0; r <= WARP_ROWS; r++) for (int c = 0; c <= WARP_COLS; c++) {
		WarpVert* w = &v[r * (WARP_COLS + 1) + c];
		float gx = (float)(c * 16), gy = (float)(r * 16);
		float dep = warp_depth_at(d, (float)r + cyf, (float)c + cxf);   // depth tracks the sub-tile scroll
		w->x = ox + gx * sx + dispUnit * clamp_disp(RAMP_AT(gy) + dep);   // ramp (screen-anchored) + scrolled depth
		w->y = oy + gy * sy;
		w->u = gx / 256.0f;             // preTex UVs coincide: PRESCALE/PRE_TEX == 1/256
		w->v = 1.0f - gy / 256.0f;
	}
	GSPGPU_FlushDataCache(v, sizeof(WarpVert) * WARP_VERTS);
	C2D_Flush();                        // submit citro2d's pending work before going raw C3D
	C3D_FrameDrawOn(tgt);
	C3D_BindProgram(&warpProg);
	C3D_Mtx proj;
	Mtx_OrthoTilt(&proj, 0.0f, 400.0f, 240.0f, 0.0f, 1.0f, -1.0f, true);
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, warpProjLoc, &proj);
	C3D_AttrInfo* ai = C3D_GetAttrInfo();
	AttrInfo_Init(ai);
	AttrInfo_AddLoader(ai, 0, GPU_FLOAT, 2);   // v0 = position
	AttrInfo_AddLoader(ai, 1, GPU_FLOAT, 2);   // v1 = texcoord
	C3D_BufInfo* bi = C3D_GetBufInfo();
	BufInfo_Init(bi);
	BufInfo_Add(bi, v, sizeof(WarpVert), 2, 0x10);
	C3D_TexBind(0, sharpPre ? pre : &g->tex);  // sharp-bilinear keeps its crisp prescale as the source
	C3D_TexEnv* env = C3D_GetTexEnv(0);
	C3D_TexEnvInit(env);
	C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, GPU_CONSTANT, 0);
	C3D_TexEnvFunc(env, C3D_Both, GPU_MODULATE);   // x mod = the unfocused dim-tint analog
	C3D_TexEnvColor(env, mod);
	C3D_TexEnvInit(C3D_GetTexEnv(1));
	C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
	C3D_CullFace(GPU_CULL_NONE);
	C3D_DrawElements(GPU_TRIANGLES, WARP_IDX, C3D_UNSIGNED_SHORT, warpIbo);
	C2D_Prepare();                      // hand the GPU back to citro2d (rebinds its shader/state)
}

// HD-2D M1: build the blurred copy of the top game's frame (one LINEAR half-res bounce). Runs
// once per frame before the per-eye composites; both eyes sample texA. Half-texel insets on
// the outer sample edges keep the cleared/stride texels from bleeding into the blur.
static void dof_prepare(C3D_Tex* src, C3D_RenderTarget* tgtA) {
	Tex3DS_SubTexture s0 = { GBA_W, GBA_H, 0.0f, 1.0f,
	                         ((float)GBA_W - 0.5f) / 256.0f, 1.0f - ((float)GBA_H - 0.5f) / 256.0f };
	C2D_Image i0 = { src, &s0 };
	C3D_TexSetFilter(src, GPU_LINEAR, GPU_LINEAR);   // averaging downsample (render_game resets it)
	C2D_TargetClear(tgtA, C2D_Color32(0, 0, 0, 0xFF));
	C2D_SceneBegin(tgtA);
	C2D_DrawImageAt(i0, 0.0f, 0.0f, 0.0f, NULL, 0.5f, 0.5f);
}

// One blurred horizontal band: GBA rows gy0..gy1 (multiples of 2) of the half-res copy,
// bilinear-upscaled through the same screen transform, vertical alpha ramp a0(top)->a1(bottom).
// Tint blend 0 leaves RGB untouched; the tint color's alpha is per-corner transparency.
static void dof_band(C3D_Tex* texA, int gy0, int gy1, float ox, float oy, float sx, float sy, float xoff, u8 a0, u8 a1) {
	if (gy1 <= gy0) return;
	float u1 = ((float)(GBA_W / 2) - 0.5f) / DOF_TEXA;
	float v1 = 1.0f - ((float)(gy1 / 2) - (gy1 == GBA_H ? 0.5f : 0.0f)) / DOF_TEXA;
	Tex3DS_SubTexture st = { (u16)(GBA_W / 2), (u16)((gy1 - gy0) / 2),
	                        0.0f, 1.0f - (float)(gy0 / 2) / DOF_TEXA, u1, v1 };
	C2D_Image img = { texA, &st };
	C2D_ImageTint t;
	C2D_SetImageTint(&t, C2D_TopLeft,  C2D_Color32(0, 0, 0, a0), 0.0f);
	C2D_SetImageTint(&t, C2D_TopRight, C2D_Color32(0, 0, 0, a0), 0.0f);
	C2D_SetImageTint(&t, C2D_BotLeft,  C2D_Color32(0, 0, 0, a1), 0.0f);
	C2D_SetImageTint(&t, C2D_BotRight, C2D_Color32(0, 0, 0, a1), 0.0f);
	C2D_DrawImageAt(img, ox + xoff, oy + gy0 * sy, 0.0f, &t, 2.0f * sx, 2.0f * sy);
}

// Tilt-shift composite on one eye target: solid blur at the frame edges, alpha ramp into the
// sharp focal band. Drawn AFTER the pops, so out-of-focus pop edges blur away with the band.
// Each band has its own engagement level: text under a band kills just that band's blur.
static void dof_bands(C3D_RenderTarget* tgt, C3D_Tex* texA, int mode, float lvlTop, float lvlBot, float eyeSl) {
	u8 aT = (u8)(DOF_ALPHA * lvlTop + 0.5f), aB = (u8)(DOF_ALPHA * lvlBot + 0.5f);
	if (!aT && !aB) return;
	float ox, oy, sx, sy; calc_xform(mode, 400.0f, 240.0f, &ox, &oy, &sx, &sy);
	float dT = eyeSl * RAMP_AT(DOF_SHARP_Y0 / 2);            // bands ride the floor ramp at their
	float dB = eyeSl * RAMP_AT((DOF_SHARP_Y1 + GBA_H) / 2);  // centers -> no depth rivalry with it
	C2D_SceneBegin(tgt);
	if (aT) {
		dof_band(texA, 0,                       DOF_SHARP_Y0 - DOF_FADE, ox, oy, sx, sy, dT, aT, aT);
		dof_band(texA, DOF_SHARP_Y0 - DOF_FADE, DOF_SHARP_Y0,            ox, oy, sx, sy, dT, aT, 0x00);
	}
	if (aB) {
		dof_band(texA, DOF_SHARP_Y1,            DOF_SHARP_Y1 + DOF_FADE, ox, oy, sx, sy, dB, 0x00, aB);
		dof_band(texA, DOF_SHARP_Y1 + DOF_FADE, GBA_H,                   ox, oy, sx, sy, dB, aB, aB);
	}
}

// BG0 scan v2 (game-agnostic): gen-3 draws every textbox/banner/menu on BG0, the text/window
// layer (verified: both decomps template bg0; the standard textbox sits at tile rows 15-18).
// One pass yields (a) per-band text flags -> that band's blur is suppressed so ALL text stays
// readable, and (b) the window-panel RECTS -> popped out per eye in ANY context (dialog, START
// menu, bag, party, battle text). Filler = the dominant entry of the visible grid (not assumed
// 0); unscannable modes fail toward readable. Runs at the parked per-frame handshake.
static void bg0_scan(GbaCore* c, const GameProfile* p, bool overworld, DepthSnap* d) {
	uint16_t disp = gbacore_read16(c, 0x04000000);
	if (!(disp & 0x0100)) return;                       // BG0 disabled -> no text layer
	uint16_t cnt = gbacore_read16(c, 0x04000008);
	if ((disp & 0x0007) != 0 || (cnt >> 14) != 0) {     // not mode 0 / text BG not 32x32:
		d->textTop = d->textBot = true;                  // can't reason -> fail toward readable
		return;
	}
	uint32_t map = 0x06000000u + (uint32_t)((cnt >> 8) & 0x1F) * 0x800u;
	uint16_t smp[40];                                   // dominant entry of the visible 32x20 grid
	for (int i = 0; i < 40; i++) smp[i] = gbacore_read16(c, map + 2u * (uint32_t)(i * 16));
	uint16_t filler = smp[0]; int best = 0;
	for (int i = 0; i < 40; i++) {
		int n = 0;
		for (int j = 0; j < 40; j++) n += (smp[j] == smp[i]);
		if (n > best) { best = n; filler = smp[i]; }
	}
	signed char rlo[20], rhi[20]; int rowN[20], topBusy = 0, botBusy = 0;
	for (int r = 0; r < 20; r++) {                      // per-row occupancy of the visible 30 cols
		int lo = -1, hi = -1, n = 0;
		for (int col = 0; col < 30; col++)
			if (gbacore_read16(c, map + 2u * (uint32_t)(r * 32 + col)) != filler) {
				if (lo < 0) lo = col;
				hi = col; n++;
			}
		rlo[r] = (signed char)lo; rhi[r] = (signed char)hi; rowN[r] = n;
		if (r <= 6)  topBusy += n;
		if (r >= 13) botBusy += n;
	}
	if (topBusy >= 8) d->textTop = true;                // tile rows 0..6  ~ GBA y 0..55
	if (botBusy >= 8) d->textBot = true;                // tile rows 13..19 ~ GBA y 104..159
	// UI-panel RECTS are gen-3 only: BG0 is the text/window layer there, but an arbitrary GBA
	// game's BG0 is usually the main playfield -> a full-screen 'panel' popped at max disparity.
	// (The blur text-flags above stay general; they only gate DoF, which is itself gen-3-gated.)
	if (!p) return;
	for (int r = 0; r < 20 && d->nui < 6; ) {           // merge busy rows (>=2 tiles) into panels
		if (rowN[r] < 2) { r++; continue; }
		int q = r, lo = rlo[r], hi = rhi[r];
		while (q + 1 < 20 && rowN[q + 1] >= 2) {
			q++;
			if (rlo[q] < lo) lo = rlo[q];
			if (rhi[q] > hi) hi = rhi[q];
		}
		int wc = hi - lo + 1, hr = q - r + 1;
		// Drop a near-full-screen rect in the OVERWORLD (a transient full BG0 = playfield false
		// positive); menus/bag/party run with overworld=false and legitimately fill the screen.
		if (!(overworld && wc >= 24 && hr >= 14)) {
			d->uiRect[d->nui].x0 = (unsigned char)lo;  d->uiRect[d->nui].y0 = (unsigned char)r;
			d->uiRect[d->nui].x1 = (unsigned char)hi;  d->uiRect[d->nui].y1 = (unsigned char)q;
			d->nui++;
		}
		r = q + 1;
	}
}

// Pop the BG0 window panels out of the screen on one eye: clean shifted overdraws of the SAME
// pixels -> strong depth while the text stays pixel-sharp (no blending, no blur).
static void ui_pop_eye(C3D_RenderTarget* tgt, EmuInstance* g, const DepthSnap* d, int mode,
                       float disp, bool sharpPre, C3D_Tex* pre) {
	float ox, oy, sx, sy; calc_xform(mode, 400.0f, 240.0f, &ox, &oy, &sx, &sy);
	C2D_SceneBegin(tgt);
	for (int i = 0; i < d->nui; i++) {
		int x = d->uiRect[i].x0 * 8, y = d->uiRect[i].y0 * 8;
		int w = (d->uiRect[i].x1 - d->uiRect[i].x0 + 1) * 8;
		int h = (d->uiRect[i].y1 - d->uiRect[i].y0 + 1) * 8;
		if (sharpPre) draw_pop_tex(pre, PRESCALE, PRE_TEX, GPU_LINEAR, x, y, w, h, ox, oy, sx, sy, disp);
		else          draw_pop(&g->tex, x, y, w, h, ox, oy, sx, sy, disp);
	}
}

// ---- HD-2D M3: LDR bloom (raw C3D draws reusing the warp passthrough shader) ----------------
static void bloom_raw_state(C3D_Tex* tex) {   // shared state for the two bloom draws
	C3D_BindProgram(&warpProg);
	C3D_AttrInfo* ai = C3D_GetAttrInfo();
	AttrInfo_Init(ai);
	AttrInfo_AddLoader(ai, 0, GPU_FLOAT, 2);   // v0 = position
	AttrInfo_AddLoader(ai, 1, GPU_FLOAT, 2);   // v1 = texcoord
	C3D_TexBind(0, tex);
	C3D_TexEnvInit(C3D_GetTexEnv(1));
	C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
	C3D_CullFace(GPU_CULL_NONE);
}

static void bloom_quad(WarpVert* v, float x0, float y0, float x1, float y1,
                       float u0, float v0, float u1, float v1) {
	v[0] = (WarpVert){ x0, y0, u0, v0 };
	v[1] = (WarpVert){ x1, y0, u1, v0 };
	v[2] = (WarpVert){ x1, y1, u1, v1 };
	v[3] = (WarpVert){ x0, y1, u0, v1 };
	GSPGPU_FlushDataCache(v, sizeof(WarpVert) * 4);
	C3D_BufInfo* bi = C3D_GetBufInfo();
	BufInfo_Init(bi);
	BufInfo_Add(bi, v, sizeof(WarpVert), 2, 0x10);
	C3D_DrawArrays(GPU_TRIANGLE_FAN, 0, 4);
}

// Bright-pass: half-res copy -> quarter-res glow map. TEV: clamp(tex - threshold) * 2.
static void bloom_bright(C3D_Tex* srcHalf, C3D_RenderTarget* tgt) {
	C2D_TargetClear(tgt, C2D_Color32(0, 0, 0, 0xFF));
	C2D_Flush();
	C3D_FrameDrawOn(tgt);
	bloom_raw_state(srcHalf);
	C3D_Mtx proj;
	Mtx_Ortho(&proj, 0.0f, (float)BLOOM_TEX, (float)BLOOM_TEX, 0.0f, 1.0f, -1.0f, true);
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, warpProjLoc, &proj);
	C3D_TexEnv* env = C3D_GetTexEnv(0);
	C3D_TexEnvInit(env);
	C3D_TexEnvSrc(env, C3D_RGB, GPU_TEXTURE0, GPU_CONSTANT, 0);
	C3D_TexEnvFunc(env, C3D_RGB, GPU_SUBTRACT);      // clamp(frame - threshold)
	C3D_TexEnvScale(env, C3D_RGB, GPU_TEVSCALE_2);   // x2: punch the survivors up
	C3D_TexEnvSrc(env, C3D_Alpha, GPU_TEXTURE0, 0, 0);
	C3D_TexEnvFunc(env, C3D_Alpha, GPU_REPLACE);
	C3D_TexEnvColor(env, C2D_Color32(BLOOM_THRESH, BLOOM_THRESH, BLOOM_THRESH, 0x00));
	bloom_quad(bloomVbo, 0.0f, 0.0f, (float)(GBA_W / 4), (float)(GBA_H / 4),
	           0.0f, 1.0f,
	           ((float)(GBA_W / 2) - 0.5f) / DOF_TEXA, 1.0f - ((float)(GBA_H / 2) - 0.5f) / DOF_TEXA);
	C2D_Prepare();
}

// Additive composite of the glow map over one finished eye (drawn after the DoF bands).
static void bloom_add(C3D_RenderTarget* tgt, C3D_Tex* glow, int mode, float lvl, int eye) {
	float ox, oy, sx, sy; calc_xform(mode, 400.0f, 240.0f, &ox, &oy, &sx, &sy);
	C2D_Flush();
	C3D_FrameDrawOn(tgt);
	bloom_raw_state(glow);
	C3D_Mtx proj;
	Mtx_OrthoTilt(&proj, 0.0f, 400.0f, 240.0f, 0.0f, 1.0f, -1.0f, true);
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, warpProjLoc, &proj);
	u8 g = (u8)(BLOOM_GAIN * lvl + 0.5f);
	C3D_TexEnv* env = C3D_GetTexEnv(0);
	C3D_TexEnvInit(env);
	C3D_TexEnvSrc(env, C3D_RGB, GPU_TEXTURE0, GPU_CONSTANT, 0);
	C3D_TexEnvFunc(env, C3D_RGB, GPU_MODULATE);      // glow x gain (gain carries the text fade)
	C3D_TexEnvSrc(env, C3D_Alpha, GPU_CONSTANT, 0, 0);
	C3D_TexEnvFunc(env, C3D_Alpha, GPU_REPLACE);
	C3D_TexEnvColor(env, C2D_Color32(g, g, g, 0xFF));
	C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ONE, GPU_ZERO, GPU_ONE);   // additive
	bloom_quad(bloomVbo + 4 + eye * 4, ox, oy, ox + GBA_W * sx, oy + GBA_H * sy,
	           0.0f, 1.0f,
	           ((float)(GBA_W / 4) - 0.5f) / BLOOM_TEX, 1.0f - ((float)(GBA_H / 4) - 0.5f) / BLOOM_TEX);
	C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA,
	               GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA);   // restore citro2d's standard blend
	C2D_Prepare();
}

// Draw one game to `screen` at the current scale + filter, leaving `screen` bound so the
// caller can draw overlays (focus bar, toast, menu) on top. `preTgt`/`preTex` are the shared
// offscreen prescale buffer, reused per screen (sequential on the render thread -> no race).
// Phase 14: `td` NULL = the flat path, which is then byte-for-byte today's code (PHASE.md
// invariant 1). Non-NULL = the tilted mesh REPLACES the final blit (SPEC-render R3.1) — the
// prescale pass 0 survives and becomes its source (R4.7), and the C2D_TargetClear stays in both
// branches because it paints the letterbox AND the R2.5.2 top-corner wedges (R3.1.1).
static void render_game(EmuInstance* e, C3D_RenderTarget* screen, C3D_RenderTarget* preTgt,
                        C3D_Tex* preTex, float screenW, float screenH,
                        int mode, bool smooth, const C2D_ImageTint* tint, u32 clrBg,
                        const TiltDraw* td) {
	if (!e->core) { C2D_TargetClear(screen, clrBg); C2D_SceneBegin(screen); return; }

	float sx, sy;
	if (mode == SCALE_1X)           { sx = sy = 1.0f; }
	else if (mode == SCALE_STRETCH) { sx = screenW / GBA_W; sy = screenH / GBA_H; }
	else { float f = (screenW / GBA_W < screenH / GBA_H) ? screenW / GBA_W : screenH / GBA_H; sx = sy = f; }
	float x = (screenW - GBA_W * sx) / 2.0f;
	float y = (screenH - GBA_H * sy) / 2.0f;

	// 1:1 is already pixel-perfect; sharp-bilinear only helps the fractional fits. R3.1.2: a tilt
	// resamples at non-integer rates EVERYWHERE, so GPU_NEAREST on the raw texture shimmers ->
	// force the 2x NEAREST prescale on even at 1:1 while tilting. (`smooth` still wins: the user
	// asked for LINEAR on e->tex and gets it.) With td == NULL this is the original expression.
	bool sharpBilinear = !smooth && (mode != SCALE_1X || td) && preTgt;

	if (sharpBilinear) {
		// Pass 0: NEAREST integer prescale 240x160 -> 480x320 into the offscreen target.
		Tex3DS_SubTexture s0 = { GBA_W, GBA_H, 0.0f, 1.0f,
		                         (float)GBA_W / 256.0f, 1.0f - (float)GBA_H / 256.0f };
		C2D_Image i0 = { &e->tex, &s0 };   // source = this core's 256x256 GBA texture
		C3D_TexSetFilter(&e->tex, GPU_NEAREST, GPU_NEAREST);
		C2D_TargetClear(preTgt, C2D_Color32(0, 0, 0, 0));
		C2D_SceneBegin(preTgt);
		C2D_DrawImageAt(i0, 0.0f, 0.0f, 0.0f, NULL, (float)PRESCALE, (float)PRESCALE);

		// Phase 14 (R3.1): the tilted mesh replaces pass 1, sourcing the crisp prescale exactly as
		// warp_grid_eye does (main.c:1082) — the UVs need no change because PRESCALE/PRE_TEX == 1/256.
		if (td) { C2D_TargetClear(screen, clrBg); tilt_draw_image(screen, preTex, td, screenW, screenH, mode); return; }

		// Pass 1: LINEAR draw the prescaled image, fit to the final on-screen rect.
		Tex3DS_SubTexture s1 = { PRE_W, PRE_H, 0.0f, 1.0f,
		                         (float)PRE_W / PRE_TEX, 1.0f - (float)PRE_H / PRE_TEX };
		C2D_Image i1 = { preTex, &s1 };
		C3D_TexSetFilter(preTex, GPU_LINEAR, GPU_LINEAR);
		C2D_TargetClear(screen, clrBg);
		C2D_SceneBegin(screen);
		C2D_DrawImageAt(i1, x, y, 0.0f, tint, (GBA_W * sx) / PRE_W, (GBA_H * sy) / PRE_H);
	} else {
		// Direct draw: NEAREST for 1:1/Sharp, LINEAR for Smooth.
		Tex3DS_SubTexture s = { GBA_W, GBA_H, 0.0f, 1.0f,
		                        (float)GBA_W / 256.0f, 1.0f - (float)GBA_H / 256.0f };
		C2D_Image img = { &e->tex, &s };
		GPU_TEXTURE_FILTER_PARAM f = smooth ? GPU_LINEAR : GPU_NEAREST;
		C3D_TexSetFilter(&e->tex, f, f);
		C2D_TargetClear(screen, clrBg);
		// Phase 14 (R3.1/R3.1.2): reached with tilt only when the user chose Smooth (or no preTgt),
		// i.e. e->tex sampled LINEAR — which is what the tilt wants anyway (R3.2.1).
		if (td) { tilt_draw_image(screen, &e->tex, td, screenW, screenH, mode); return; }
		C2D_SceneBegin(screen);
		C2D_DrawImageAt(img, x, y, 0.0f, tint, sx, sy);
	}
}

// ---- Settings persistence (sdmc:/3DGBA/settings.bin) --------------------
#define SETTINGS_PATH  "sdmc:/3DGBA/settings.bin"
#define SETTINGS_MAGIC 0x33424744u   // 'DGB3'
typedef struct {
	u32 magic;
	s32 scaleMode[2];
	s32 smooth[2];
	s32 swapped;
	s32 hudMode;
	s32 audioMode;
	s32 volA, volB;
	s32 touchMode;
	s32 frameskip;
	s32 dof;
	s32 bloom;
	s32 light;
	s32 vivid;
	// --- UI redesign additions (appended; older files that end at `vivid` still load) ---
	s32 theme;            // ThemeId
	s32 customBaseHue;    // custom-theme builder params
	s32 customAccentHue;
	s32 customContrast;
	s32 gameMode;         // 0 = dual, 1 = single
	s32 padColor;         // gamepad tint index
	s32 padEdge;          // 0 round / 1 soft / 2 sharp
	// --- phase 14 (appended; files that end at `padEdge` still load, tiltLevel stays default) ---
	s32 tilt;             // g_prefs.tiltLevel, 0..TILT_LEVELS-1 (SPEC-integration I4.9)
	// --- phase 15 (appended; files that end at `tilt` still load, presence stays 0 = OFF) ---
	s32 presence;         // co-op presence pref (SPEC-avatar A6.3). NO magic bump, same as phase
	                      // 14: SETTINGS_MAGIC identifies the FAMILY and the length ladder does the
	                      // versioning, so every file a pre-phase-15 build wrote still loads.
} Settings;
// SPEC-integration I7.6: test/host/test_tilt.c TEST 6 replicates this layout (main.c cannot be
// host-compiled), so pin the two together. If a field is inserted anywhere above, these fire and
// the test's copy must be updated in the same edit — the alternative is a silently-shifted
// offsetof ladder that mis-loads every older settings file. offsetof(tilt) staying 23 across the
// phase-15 append IS the proof that the change is backward-compatible (SPEC-avatar A6.3.2).
_Static_assert(sizeof(Settings)             == 25 * sizeof(s32), "Settings grew/shrank — sync test_tilt TEST 6");
_Static_assert(offsetof(Settings, presence) == 24 * sizeof(s32), "Settings.presence moved — sync test_tilt TEST 6");
_Static_assert(offsetof(Settings, tilt)     == 23 * sizeof(s32), "Settings.tilt moved — sync test_tilt TEST 6");
_Static_assert(offsetof(Settings, padEdge)  == 22 * sizeof(s32), "Settings.padEdge moved — sync test_tilt TEST 6");

static void settings_load(int scaleMode[2], bool smooth[2], bool* swapped, int* hudMode,
                          int* audioMode, int* volA, int* volB, int* touchMode, bool* fsOn, bool* dofOn, bool* bloomOn, bool* lightOn, bool* vividOn,
                          bool* presenceOn) {
	FILE* f = fopen(SETTINGS_PATH, "rb");
	if (!f) return;
	Settings s;
	size_t n = fread(&s, 1, sizeof s, f);
	fclose(f);
	// Accepted file lengths, oldest → newest (offsetof keeps this robust as the struct grows).
	size_t lenDof   = offsetof(Settings, dof);    // pre-dof (ends at frameskip)
	size_t lenBloom = offsetof(Settings, bloom);  // includes dof
	size_t lenLight = offsetof(Settings, light);  // includes bloom
	size_t lenVivid = offsetof(Settings, vivid);  // includes light
	size_t lenOld   = offsetof(Settings, theme);  // includes vivid = pre-redesign full struct
	// phase 14 (SPEC-integration I4.10): NO magic bump — SETTINGS_MAGIC identifies the FAMILY and
	// the length ladder does the versioning. lenPad is the pre-tilt full struct, i.e. exactly what
	// every file written before this build is, so those files still load everything they had and
	// simply leave g_prefs.tiltLevel at its default 0. Backward-compatible by construction.
	size_t lenPad   = offsetof(Settings, tilt);   // includes the UI-redesign prefs (pre-tilt)
	// phase 15 (SPEC-avatar A6.3.1): today's rungs shift by one name. lenTilt is the PRE-PRESENCE
	// full struct — i.e. exactly what every file written by the phase-14 build is — and lenNew is
	// the new sizeof. Getting this rename wrong rejects every existing settings file, which is the
	// whole reason the ladder exists.
	size_t lenTilt  = offsetof(Settings, presence);   // includes tiltLevel (pre-presence)
	size_t lenNew   = sizeof s;                       // + presence
	if ((n != lenNew && n != lenTilt && n != lenPad && n != lenOld && n != lenVivid && n != lenLight && n != lenBloom && n != lenDof)
	    || s.magic != SETTINGS_MAGIC) return;      // tolerate older files
	scaleMode[0] = ((unsigned)s.scaleMode[0]) % 3;
	scaleMode[1] = ((unsigned)s.scaleMode[1]) % 3;
	smooth[0] = s.smooth[0] != 0;
	smooth[1] = s.smooth[1] != 0;
	*swapped  = s.swapped != 0;
	*hudMode    = ((unsigned)s.hudMode) & 3;
	*audioMode = ((unsigned)s.audioMode) % 3;
	*volA = s.volA < 0 ? 0 : (s.volA > 256 ? 256 : s.volA);
	*volB = s.volB < 0 ? 0 : (s.volB > 256 ? 256 : s.volB);
	*touchMode = ((unsigned)s.touchMode) % 3;
	*fsOn = s.frameskip != 0;
	if (n >= lenBloom) *dofOn   = s.dof != 0;      // older files keep the defaults
	if (n >= lenLight) *bloomOn = s.bloom != 0;
	if (n >= lenVivid) *lightOn = s.light != 0;
	if (n >= lenOld)   *vividOn = s.vivid != 0;
	if (n >= lenPad) {                              // the UI-redesign chrome prefs (into the global)
		g_prefs.theme           = ((unsigned)s.theme) % THEME_PRESET_COUNT;
		g_prefs.customBaseHue   = ((s.customBaseHue % 360) + 360) % 360;
		g_prefs.customAccentHue = ((s.customAccentHue % 360) + 360) % 360;
		g_prefs.customContrast  = s.customContrast < 6 ? 6 : (s.customContrast > 24 ? 24 : s.customContrast);
		g_prefs.gameMode        = s.gameMode ? 1 : 0;
		g_prefs.padColor        = ((unsigned)s.padColor) % 5;
		g_prefs.padEdge         = ((unsigned)s.padEdge) % 3;
	}
	// Modulo, exactly like padEdge: a corrupt/negative word can never index the angle ladder out
	// of range (the unsigned cast makes even INT32_MIN land in 0..TILT_LEVELS-1 — TEST 6).
	if (n >= lenTilt) g_prefs.tiltLevel = ((unsigned)s.tilt) % TILT_LEVELS;
	// A6.3: a 2-state pref, so != 0 is the whole clamp — a corrupt word can only ever produce
	// on/off. Older (pre-phase-15) files leave it at the shipped default, which is OFF (A6.3.4:
	// a new feature ships inert so no existing user's frame changes).
	if (n >= lenNew && presenceOn) *presenceOn = s.presence != 0;
	theme_apply(g_prefs.theme, g_prefs.customBaseHue, g_prefs.customAccentHue, g_prefs.customContrast);
}

static void settings_save(const int scaleMode[2], const bool smooth[2], bool swapped, int hudMode,
                          int audioMode, int volA, int volB, int touchMode, bool fsOn, bool dofOn, bool bloomOn, bool lightOn, bool vividOn,
                          bool presenceOn) {
	Settings s = { SETTINGS_MAGIC, { scaleMode[0], scaleMode[1] },
	               { smooth[0], smooth[1] }, swapped, hudMode, audioMode, volA, volB, touchMode, fsOn, dofOn, bloomOn, lightOn, vividOn,
	               g_prefs.theme, g_prefs.customBaseHue, g_prefs.customAccentHue, g_prefs.customContrast,
	               g_prefs.gameMode, g_prefs.padColor, g_prefs.padEdge,
	               g_prefs.tiltLevel,     // phase 14, I4.9: appended, so the file grew by 4 B
	               presenceOn };          // phase 15, A6.3.3: appended last, another 4 B
	FILE* f = fopen(SETTINGS_PATH, "wb");
	if (!f) return;
	fwrite(&s, 1, sizeof s, f);
	fclose(f);
}

// ---- Pause menu ----
enum { SESSION_CHANGE, SESSION_QUIT };
#define MENU_N 20
#define MENU_LINK_IDX  1   // dynamic label ("Link: off/on")
#define MENU_AUDIO_IDX 2   // dynamic label ("Audio: <mode>")
#define MENU_TOUCH_IDX 3   // dynamic label ("Touch: on/off")
#define MENU_FS_IDX    4   // dynamic label ("Frameskip: on/off")
#define MENU_MUTE_IDX  10  // dynamic label ("Mute: on/off")
#define MENU_PAUSE_IDX 11  // dynamic label ("Pause A/B: on/off")
#define MENU_HUD_IDX   5   // dynamic label ("HUD: off/top/bottom/both")
#define MENU_DOF_IDX   12  // dynamic label ("DoF: on/off")
#define MENU_BLOOM_IDX 13  // dynamic label ("Bloom: on/off")
#define MENU_LIGHT_IDX 14  // dynamic label ("Light: on/off")
#define MENU_VIVID_IDX 15  // dynamic label ("Vivid: on/off")
#define MENU_WIRELESS_IDX 16  // opens the wireless lobby
#define MENU_NETLINK_IDX  19  // M2.5 net link (loopback) toggle — dynamic label
static const char* const HUD_NAMES[4] = { "off", "top", "bottom", "both" };

// ---- Tabbed pause menu (UI redesign) ----
// The 6 tabs re-group the SAME actions the old 2x10 grid had; legacy ids (0..19) feed the original
// menuSel dispatch untouched (incl. the wireless block). Ids >= 100 are redesign-only controls
// handled in a small pre-dispatch block.
enum {
	ACT_RESUME = 0, ACT_LINK = 1, ACT_AUDIOMODE = 2, ACT_TOUCHMODE = 3, ACT_FS = 4,
	ACT_HUD = 5, ACT_SWAP = 6, ACT_SAVEST = 7, ACT_LOADST = 8, ACT_LOADSAV = 9,
	ACT_MUTE = 10, ACT_PAUSEG = 11, ACT_DOF = 12, ACT_BLOOM = 13, ACT_LIGHT = 14,
	ACT_VIVID = 15, ACT_WIRELESS = 16, ACT_CHANGE = 17, ACT_QUIT = 18, ACT_NETLINK = 19,
	ACT_SCALE_TOP = 100, ACT_SCALE_BOT, ACT_FILTER, ACT_THEME, ACT_VOLA, ACT_VOLB,
	ACT_3D, ACT_PADCOL, ACT_PADEDGE, ACT_CHUE, ACT_CAHUE, ACT_CCON,
	ACT_PREVIEW_PAD, ACT_PREVIEW_SMART,   // Touch tab: set the mode + resume to see it live
	ACT_TILT,                             // phase 14 HD-2D diorama tilt (PK_SEG, 4 rungs) — I4.7
	ACT_PRESENCE,                         // phase 15 co-op presence (PK_TOG, 2 states) — A6.1.4
};
static const char* const MENU_TAB_NAMES[6] = { "SESSION", "DISPLAY", "AUDIO", "ENHANCE", "LINK", "TOUCH" };
static const char* const PAD_EDGE_NAMES[3] = { "Round", "Soft", "Sharp" };
// SPEC-integration Q1, RESOLVED: NOT "Off/Soft/Med/Deep". The shipped ladder is a deliberately
// mild OFF/10/15/20 deg (SPEC-render R2.5.5 — gen1recomp's own OFF/15/35/50 is undefensible for
// us above its first rung because they render up to 2.56x more world and we have a fixed 240x160
// composited frame), so "Deep" would over-promise a 20 deg tilt. Low/Mid/Max describes a position
// on OUR ladder without claiming an absolute. One table, three consumers: the pause seg, the
// standalone-settings seg, and the status line — so the three can never drift apart.
static const char* const TILT_NAMES[TILT_LEVELS] = { "Off", "Low", "Mid", "Max" };

enum { PK_TOG, PK_SEG, PK_STEP, PK_BTN, PK_SWATCH };
typedef struct { unsigned char kind, act, nseg; short x, y, w, h; const char* ov; } PCtl;
static const PCtl PT_SESSION[] = {
  {PK_BTN,ACT_RESUME,0, 93,10,216,40,0},{PK_BTN,ACT_CHANGE,0, 93,59,216,40,0},{PK_BTN,ACT_QUIT,0, 93,108,216,43,0} };
static const PCtl PT_DISPLAY[] = {
  {PK_SEG,ACT_SCALE_TOP,3, 93,26,208,30,0},{PK_SEG,ACT_SCALE_BOT,3, 93,81,208,30,0},
  {PK_SEG,ACT_FILTER,2, 93,136,208,30,0},{PK_SEG,ACT_HUD,4, 93,191,208,30,0},
  {PK_TOG,ACT_SWAP,0, 150,224,34,14,"Swap"},{PK_TOG,ACT_FS,0, 284,224,34,14,"Skip"} };
static const PCtl PT_AUDIO[] = {
  {PK_SEG,ACT_AUDIOMODE,3, 93,26,216,30,0},{PK_STEP,ACT_VOLA,0, 93,82,216,24,0},
  {PK_STEP,ACT_VOLB,0, 93,132,216,24,0},{PK_TOG,ACT_MUTE,0, 276,171,34,18,0} };
// Phase 14 adds the TILT row (SPEC-integration I4.1/I4.2). PK_SEG, not PK_TOG, because the
// feature is a LADDER (PHASE.md invariant 5 tweens between levels; gen1recomp's Tilt.level) and
// this codebase already models 3-4-way ladders as PK_SEG with nseg (ACT_TOUCHMODE 3, ACT_HUD 4).
// Coordinates: the five baked toggles end at y=170+18=188 and the pause-bot-enhance plate bakes
// NOTHING below y~190; the tab rail owns x<86 and the status hint sits at y=231. So 140,198,
// 170x26 lands in that empty band — right edge 310 (flush with the toggle column's 276+34),
// bottom edge 224 (7 px above the hint). NO existing row moves and the plate art is unchanged;
// the label rides the PCtl.ov overlay mechanism that exists for exactly this (precedent: the
// DISPLAY tab's "Swap"/"Skip" and the TOUCH tab's "EDGES" at the same x=140). Regenerating the
// plate with a baked "Diorama tilt" label is open question O6 — deferred, not forgotten.
static const PCtl PT_ENHANCE[] = {
  {PK_TOG,ACT_3D,0, 276,51,34,18,0},{PK_TOG,ACT_DOF,0, 276,83,34,18,0},{PK_TOG,ACT_BLOOM,0, 276,112,34,18,0},
  {PK_TOG,ACT_LIGHT,0, 276,141,34,18,0},{PK_TOG,ACT_VIVID,0, 276,170,34,18,0},
  {PK_SEG,ACT_TILT,4, 140,198,170,26,"TILT"} };
// Phase 15 adds the CO-OP row (SPEC-avatar A6.1). It goes on LINK, not ENHANCE, because ENHANCE is
// measurably full: its five baked toggles end at y=188, the tilt seg takes y198..224 and the status
// hint sits at y=231 — SEVEN pixels left, and phase 14's open question O6 (new plate art for a 6th
// and 7th row) is still outstanding. LINK's last widget is ACT_LOADSAV at y156 h31 -> bottom edge
// 187, so y196 h18 lands in a genuinely empty 44 px band and clears the hint by 17 px. LINK is also
// semantically right: the tab is "how this console talks to another game", and it is where M4 (the
// wireless version of exactly this feature) will need to live — so the control does not move when
// the transport changes, which is PHASE.md invariant 3's whole point. x276/w34/h18 is the same
// toggle geometry the other two rows on this tab use, so the column stays aligned; the "CO-OP"
// label rides the PCtl.ov overlay (precedent on three tabs). No plate art changes, no row moves.
static const PCtl PT_LINK[] = {
  {PK_TOG,ACT_LINK,0, 276,16,34,18,0},{PK_TOG,ACT_NETLINK,0, 276,44,34,18,0},
  {PK_BTN,ACT_WIRELESS,0, 93,74,216,35,0},{PK_BTN,ACT_SAVEST,0, 93,118,104,31,0},
  {PK_BTN,ACT_LOADST,0, 204,118,104,31,0},{PK_BTN,ACT_LOADSAV,0, 93,156,216,31,0},
  {PK_TOG,ACT_PRESENCE,0, 276,196,34,18,"CO-OP"} };
static const PCtl PT_TOUCH[] = {
  {PK_SEG,ACT_TOUCHMODE,3, 93,26,208,30,0},{PK_BTN,ACT_PREVIEW_PAD,0, 93,109,101,44,0},
  {PK_BTN,ACT_PREVIEW_SMART,0, 201,109,101,44,0},{PK_SWATCH,ACT_PADCOL,0, 93,192,208,26,0},
  {PK_SEG,ACT_PADEDGE,3, 140,224,172,14,"EDGES"} };
static const PCtl* const PTABS[6] = { PT_SESSION, PT_DISPLAY, PT_AUDIO, PT_ENHANCE, PT_LINK, PT_TOUCH };
// I4.4: ENHANCE goes 5 -> 6 for the tilt row. Forgetting this is SILENT — the row would never
// draw (the draw loop is `for (i < nPd)`) and the touch hit-test loop would never reach it.
// A6.1.1: LINK goes 6 -> 7 for the phase-15 CO-OP row, and it is the SAME silent trap.
static const int PTABN[6] = { 3, 6, 4, 6, 7, 5 };
static const char* const PT_PLATE[6] = { "pause-bot-session","pause-bot-display","pause-bot-audio",
                                         "pause-bot-enhance","pause-bot-link","pause-bot-touch" };

// ---- Pause-menu widget layout (v2, 1:1): each tab is a list of typed widgets. ----
// Kinds: SECTION label / SEG segmented / TOGGLE (opt. sublabel) / BUTTON (full or half row) /
// STEPPER (bar = volume, val = theme+custom params) / HEADER pill / TEXT explainer / SWATCH row.
enum { W_SECTION, W_SEG, W_TOGGLE, W_BUTTON, W_STEPPER_BAR, W_STEPPER_VAL,
       W_HEADER, W_TEXT, W_SWATCH, W_GAP };
typedef struct { u8 kind, act, half, aux; } MenuW;   // half: 1 = left half-button, 2 = right

static int menu_w_h(const MenuW* w) {
	switch (w->kind) {
	case W_SECTION: return 13;
	case W_SEG:     return 26;
	case W_TOGGLE:  return w->aux ? 28 : 24;
	case W_BUTTON:  return (w->half == 1) ? 0 : 30;   // half-pairs share one 30px row
	case W_STEPPER_BAR:
	case W_STEPPER_VAL: return 36;
	case W_HEADER:  return 20;
	case W_TEXT:    return (int)w->aux;                // aux = explainer height
	case W_SWATCH:  return 26;
	default:        return 6;                          // W_GAP
	}
}
static int menu_w_sel(u8 kind) {
	return kind == W_SEG || kind == W_TOGGLE || kind == W_BUTTON ||
	       kind == W_STEPPER_BAR || kind == W_STEPPER_VAL || kind == W_SWATCH;
}
#define PUSH(k,a,hf,ax) do { out[n].kind=(k); out[n].act=(a); out[n].half=(hf); out[n].aux=(ax); n++; } while (0)
static int menu_layout(int tab, MenuW* out) {
	int n = 0;
	switch (tab) {
	case 0:   // SESSION
		PUSH(W_GAP, 0, 0, 0);
		PUSH(W_BUTTON, ACT_RESUME, 0, 1);              // aux 1 = gold primary
		PUSH(W_BUTTON, ACT_CHANGE, 0, 0);
		PUSH(W_BUTTON, ACT_PAUSEG, 0, 0);
		PUSH(W_BUTTON, ACT_QUIT,   0, 2);              // aux 2 = destructive
		break;
	case 1:   // DISPLAY
		PUSH(W_SECTION, ACT_SCALE_TOP, 0, 0); PUSH(W_SEG, ACT_SCALE_TOP, 0, 0);
		PUSH(W_SECTION, ACT_SCALE_BOT, 0, 0); PUSH(W_SEG, ACT_SCALE_BOT, 0, 0);
		PUSH(W_SECTION, ACT_FILTER, 0, 0);    PUSH(W_SEG, ACT_FILTER, 0, 0);
		PUSH(W_SECTION, ACT_HUD, 0, 0);       PUSH(W_SEG, ACT_HUD, 0, 0);
		PUSH(W_TOGGLE, ACT_SWAP, 0, 0);
		PUSH(W_TOGGLE, ACT_FS, 0, 0);
		PUSH(W_SECTION, ACT_THEME, 0, 0);     PUSH(W_STEPPER_VAL, ACT_THEME, 0, 0);
		if (g_prefs.theme == THEME_CUSTOM) {
			PUSH(W_STEPPER_VAL, ACT_CHUE, 0, 0);
			PUSH(W_STEPPER_VAL, ACT_CAHUE, 0, 0);
			PUSH(W_STEPPER_VAL, ACT_CCON, 0, 0);
		}
		break;
	case 2:   // AUDIO
		PUSH(W_SECTION, ACT_AUDIOMODE, 0, 0); PUSH(W_SEG, ACT_AUDIOMODE, 0, 0);
		PUSH(W_STEPPER_BAR, ACT_VOLA, 0, 0);
		PUSH(W_STEPPER_BAR, ACT_VOLB, 0, 0);
		PUSH(W_GAP, 0, 0, 0);
		PUSH(W_TOGGLE, ACT_MUTE, 0, 0);
		break;
	case 3:   // ENHANCE
		PUSH(W_HEADER, 0, 0, 0);
		PUSH(W_TOGGLE, ACT_3D, 0, 1);                  // aux 1 = "top screen" sublabel
		PUSH(W_TOGGLE, ACT_DOF, 0, 0);
		PUSH(W_TOGGLE, ACT_BLOOM, 0, 0);
		PUSH(W_TOGGLE, ACT_LIGHT, 0, 0);
		PUSH(W_TOGGLE, ACT_VIVID, 0, 0);
		PUSH(W_SEG,    ACT_TILT,  0, 0);   // phase 14 (I4.5 — bookkeeping; see the note at the top)
		break;
	case 4:   // LINK
		PUSH(W_TOGGLE, ACT_LINK, 0, 0);
		PUSH(W_TOGGLE, ACT_NETLINK, 0, 0);
		PUSH(W_GAP, 0, 0, 0);
		PUSH(W_BUTTON, ACT_WIRELESS, 0, 1);
		PUSH(W_BUTTON, ACT_SAVEST, 1, 0);
		PUSH(W_BUTTON, ACT_LOADST, 2, 0);
		PUSH(W_BUTTON, ACT_LOADSAV, 0, 0);
		PUSH(W_TOGGLE, ACT_PRESENCE, 0, 0);   // phase 15 (A6.1.2 — the v2 list layout's copy)
		break;
	default:  // TOUCH
		PUSH(W_SECTION, ACT_TOUCHMODE, 0, 0); PUSH(W_SEG, ACT_TOUCHMODE, 0, 0);
		PUSH(W_TEXT, ACT_TOUCHMODE, 0, 46);
		PUSH(W_BUTTON, ACT_PREVIEW_PAD, 1, 0);
		PUSH(W_BUTTON, ACT_PREVIEW_SMART, 2, 0);
		PUSH(W_SECTION, ACT_PADCOL, 0, 0);    PUSH(W_SWATCH, ACT_PADCOL, 0, 0);
		PUSH(W_SECTION, ACT_PADEDGE, 0, 0);   PUSH(W_SEG, ACT_PADEDGE, 0, 0);
		break;
	}
	return n;
}
#undef PUSH


// Top-screen summary while the pause menu is open (draws to whichever top target is bound):
// "|| PAUSED", the two game names, a row of active-feature pills, and a pointer to the bottom screen.
static void draw_paused_summary(C2D_TextBuf buf, const char* nameTop, const char* nameBot,
                                bool s3d, bool dof, bool bloom, bool light, bool vivid,
                                int touchMode, bool linkOn, bool netOn, bool wlOn, bool coop) {
	// Screen 05 top: the pause-top PLATE (PAUSED + swap arrow + hint, over a light dim of the game)
	// is the chrome; we composite the two game names (manifest x56/x216 y95) + the feature pills.
	if (!assets_ready()) { C2D_DrawRectSolid(0, 0, 0, 400, 240, C2D_Color32(0, 0, 0, 0x78));
	                       ui_text_c(buf, "PAUSED", 200.0f, 92.0f, 0.42f, g_ui.acc); return; }
	assets_draw_plate("pause-top");
	assets_text_c(buf, FNT_SG_MED, nameTop, nameBot ? 122.0f : 200.0f, 96.0f, 14.0f, THEME_ON_DARK);
	if (nameBot) assets_text_c(buf, FNT_SG_MED, nameBot, 280.0f, 96.0f, 14.0f, THEME_ON_DARK);
	{	// active-feature pills (manifest x56 y129 w288 h17): fill + baked JBM label, tinted per state
		// I4.15: the arrays were [7] and this pushed up to exactly 7 pills; the Tilt pill makes 8,
		// so ALL FOUR must widen in the same edit or the 8th push is a stack buffer overrun.
		// A6.4.3: the phase-15 Co-op pill makes NINE — same trap, same rule, all four widened here.
		const char* labs[9]; u32 fg[9], bg[9]; int n = 0;
		#define PILL(L, ON, C) do { labs[n] = (L); fg[n] = (ON) ? g_ui.ink : g_ui.dim; \
		                            bg[n] = (ON) ? (C) : g_ui.panel2; n++; } while (0)
		PILL("3D", s3d, THEME_GAME_B); PILL("DoF", dof, g_ui.acc); PILL("Bloom", bloom, g_ui.acc);
		PILL("Light", light, g_ui.acc);
		PILL("Tilt", g_prefs.tiltLevel > 0, g_ui.acc);   // right after Light: the enhance pills stay grouped
		if (vivid) PILL("Vivid", 1, g_ui.acc);
		PILL(touchMode == 2 ? "Smart" : (touchMode == 1 ? "Pad" : "Touch Off"), touchMode != 0, THEME_GAME_A);
		// A6.4.3: so the pause screen answers "is co-op on" without opening the LINK tab. It sits
		// beside the link pill because that is where the control lives (A6.1.3).
		PILL("Co-op", coop, g_ui.acc);
		PILL(wlOn ? "Wireless" : (netOn ? "Net" : "Link"), (linkOn || netOn || wlOn), g_ui.acc);
		#undef PILL
		float pw[9], tw = 0.0f;
		for (int i = 0; i < n; i++) { pw[i] = assets_text_w(buf, FNT_JBM_MED, labs[i], 8.0f) + 12.0f; tw += pw[i] + 5.0f; }
		float x = (400.0f - tw) / 2.0f;
		for (int i = 0; i < n; i++) {
			ui_fill(x, 129.0f, pw[i], 15.0f, bg[i], 5.0f);
			assets_text_c(buf, FNT_JBM_MED, labs[i], x + pw[i] / 2.0f, 132.0f, 8.0f, fg[i]);
			x += pw[i] + 5.0f;
		}
	}
	(void)buf;
}



// Run one play session with the two chosen ROMs. Returns SESSION_CHANGE (re-pick) or
// SESSION_QUIT. Creates/destroys the cores + worker threads itself.
static int run_session(C3D_RenderTarget* top, C3D_RenderTarget* bot, C3D_RenderTarget* topR,
                       C2D_TextBuf txtBuf, bool isN3DS, s32 mainPrio, const char* pathA, const char* pathB, bool startLinked) {
	const u32 clrBg     = THEME_LETTERBOX;
	const u32 clrHi     = THEME_GOLD;
	const u32 clrTxt    = THEME_TEXT;
	const u32 clrDim    = THEME_MENU_DIM;
	const u32 clrPanel  = THEME_PANEL;
	const u32 clrSelTxt = THEME_SELTXT;

	EmuInstance emuA, emuB;
	emu_start(&emuA, 0, 0,              mainPrio + 1);
	emu_start(&emuB, 1, isN3DS ? 2 : 1, mainPrio + 1);
	setup_core(&emuA, pathA);
	// 1-game mode (UI redesign): no game B — the bottom screen becomes the touch controller.
	// The PATHS are the source of truth (the picker returns pathB=="" exactly for a 1-game start);
	// a stale gameMode pref must NOT override an explicitly resumed/chosen dual pairing.
	bool single = !pathB[0];
	if (!single) setup_core(&emuB, pathB);

	// Shared offscreen target for sharp-bilinear's NEAREST prescale pass (reused per screen).
	C3D_Tex preTex;
	C3D_RenderTarget* preTgt = NULL;
	if (C3D_TexInitVRAM(&preTex, PRE_TEX, PRE_TEX, GPU_RGBA8)) {
		C3D_TexSetWrap(&preTex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
		preTgt = C3D_RenderTargetCreateFromTex(&preTex, GPU_TEXFACE_2D, 0, -1);
	}

	// HD-2D M1: the DoF bounce target (RGB565, VRAM). DoF silently disables if it fails.
	C3D_Tex dofTexA;
	C3D_RenderTarget *dofTgtA = NULL;
	if (C3D_TexInitVRAM(&dofTexA, DOF_TEXA, DOF_TEXA, GPU_RGB565)) {
		C3D_TexSetFilter(&dofTexA, GPU_LINEAR, GPU_LINEAR);
		C3D_TexSetWrap(&dofTexA, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
		dofTgtA = C3D_RenderTargetCreateFromTex(&dofTexA, GPU_TEXFACE_2D, 0, -1);
	}

	// HD-2D M3: the bloom glow map (RGB565, VRAM). Bloom silently disables if it fails.
	C3D_Tex bloomTex;
	C3D_RenderTarget* bloomTgt = NULL;
	if (warpOk && C3D_TexInitVRAM(&bloomTex, BLOOM_TEX, BLOOM_TEX, GPU_RGB565)) {
		C3D_TexSetFilter(&bloomTex, GPU_LINEAR, GPU_LINEAR);
		C3D_TexSetWrap(&bloomTex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
		bloomTgt = C3D_RenderTargetCreateFromTex(&bloomTex, GPU_TEXFACE_2D, 0, -1);
	}

	// Audio: both cores share one rate (pitch-matched to the 3DS refresh); start clean.
	GbaCore* anyCore = emuA.core ? emuA.core : emuB.core;
	audio_thread_start(mainPrio, isN3DS);   // dedicated audio thread on the core-1 slice
	if (anyCore) audio_set_rate(anyCore);   // shared rate, clean start

	GbaLink* link = gbalink_create();   // shared lockstep coordinator; cores attach on demand
	bool linkOn = false;
	bool netOn  = false;   // M2.5 net link (loopback) active — mutually exclusive with linkOn
	bool wlOn   = false;   // M3 WIRELESS link active (one core + real radio); mutually exclusive with linkOn/netOn
	int  wlSeat = -1;      // this console's seat while wlOn (0 = host, 1 = joiner) — for the SD log filename
	int  wlRtt  = -1, wlDrops = 0;   // RX-thread-measured link RTT/drops for the HUD
	int  touchMode = TOUCH_OFF;   // 0 off / 1 gamepad / 2 smart (touch drives the bottom game)
	bool fsOn = false;      // frameskip the unfocused game to free heavy-scene budget
	bool dofOn = true;      // HD-2D M1: tilt-shift depth-of-field on the top screen (overworld only)
	float dofLvlTop = 1.0f, dofLvlBot = 1.0f;   // per-band engagement 0..1 (text kills its band's blur)
	bool bloomOn = true;    // HD-2D M3: LDR bloom on the focused top screen (overworld only)
	bool lightOn = true;    // HD-2D M4: time-of-day lighting on the overworld
	bool vividOn = false;   // round 7: bright+sharp "sign look" everywhere (no lighting/DoF/bloom haze)
	float bloomLvl = 1.0f;  // eased like the DoF bands; any on-screen text kills the glow
	bool muted = false;     // HARD mute (stops audio rendering, saves CPU)

	int focused = 0;
	bool menuOpen = false;
	bool workersRunning = false;   // pipeline: a non-link frame is computing while we render the last
	DepthSnap depth3d = { false };  // top game's overworld state for stereoscopic depth (M2)
	// ---- phase 14 HD-2D tilt: per-session state (SPEC-integration I2.1/I2.8) ----
	// One tween per SCREEN, not per game: the gates are per screen, and [1] (the bottom) stays
	// pinned flat in this slice (R4.8.1 — it is the touch controller). Driven by the WALL CLOCK,
	// not a frame counter: the render loop is capped to 60 fps but not floored (main.c:2805 only
	// waits when UNDER budget), so a frame-counted tween would visibly run slow under exactly the
	// load tilt adds (I2.2 — do NOT "fix" this to match dofLvl's fixed per-frame increments).
	TiltTween tiltTw[2];
	tilt_tween_reset(&tiltTw[0]);
	tilt_tween_reset(&tiltTw[1]);
	u64 tiltLastMs = osGetTime();
	// Slice T3: the gate's game-RAM inputs, per SCREEN. Filled in the parked window from the two
	// GameStates the gs-logger block already reads (I1.1/I1.2); px = -1 and every flag 0 means a
	// frame that never fills it (or a game with no profile) gates tilt OFF — fail-safe by init.
	TiltSnap tiltSnap[2] = { { 0, 0, 0, 0, -1 }, { 0, 0, 0, 0, -1 } };
	// ---- phase 15 co-op presence: per-session state (SPEC-data D3.4 / D6) ----
	// Indexed by GAME (0 = emuA, 1 = emuB), NOT by screen. tiltSnap above is per SCREEN because a
	// tilt is a property of a display; presence state is a property of a WORLD — the identity latch,
	// the smoothing filter, the D4.7 hold and the teleport detector all belong to a game and must
	// survive an X screen swap untouched. presSt[g] is that game's CONSUMER state and its one peer
	// slot (PRES_MAX_PEERS == 1) holds the OTHER game's record, so this is already M4's shape: one
	// PresenceState per console, one remote peer in it, and the only thing M4 changes is who calls
	// presence_publish. The renderer resolves a screen to its game with `swapped`, exactly as it
	// already does for topG/botG.
	//   presenceOn is a run_session local like dofOn/bloomOn (A6.3.5) — g_prefs is the chrome/theme
	//   block, and this is a session feature toggle. Default OFF (A6.3.4): a new feature ships inert.
	bool          presenceOn = false;
	PresenceState presSt  [2];
	PresenceIdent presId  [2];
	PeerPresence  presSelf[2];   // this frame's OWN record per game = presence_solve's `self`
	PresenceOut   presOut [2];
	int           presDraw[2] = { 0, 0 };
	// Slice M2: the walk-cycle accumulator, one per GAME for the same reason presSt is
	// (SPEC-avatar A2.6.4 — it is a property of a walking WORLD, not of a display), stepped exactly
	// ONCE per frame beside presence_solve. It deliberately does NOT live in the draw: the top
	// screen draws twice (left + right eye), so a per-draw accumulator would animate at double rate
	// AND could hand the two eyes different poses, which on a parallax-barrier panel reads as
	// flicker rather than as a fast walk.
	PresWalk      presWalk[2];
	int           presPose[2] = { PRES_POSE_STAND, PRES_POSE_STAND };
	int8_t        presFaceRaw[2] = { -1, -1 };   // the raw facing nibble game_read reported, kept
	                                             // beside the folded one so the M1 readout can
	                                             // satisfy D2.4.1's promotion criterion ("one run
	                                             // where it reads 1/2/3/4 as the player walks
	                                             // D/U/L/R") without a garbage nibble hiding inside
	                                             // presence_dir's fold to SOUTH. Slice M3 gave it a
	                                             // SECOND, non-logging job: it is the only thing
	                                             // that can tell "facing south" from "we have no
	                                             // idea", which A5.2.1's degraded meeting predicate
	                                             // needs (PeerPresence.facing is always 1..4).
	// ---- slice M3: identity + interaction (SPEC-avatar A4/A5) ----
	// Per GAME, like everything else above. The card is ONE BOOL (A5.4.2 — "no timer and no state
	// machine beyond one bool"): every condition that opens or closes it is already computed
	// elsewhere in the frame, so presence_card_step is a fold of flags rather than a controller.
	PresMeet      presMeet[2];
	PresCard      presCard[2];
	PresCardText  presCardTx[2];
	// A4.5.2 — the decoded name is CACHED and re-decoded only when the raw 8 bytes change. Decoding
	// is cheap; a C2D text parse per surface per frame is not, and this runs on the thread that
	// drives the LightEvent handshake with two saturated 804 MHz workers (PHASE.md invariant 6).
	uint8_t       presNameRaw[2][8];
	char          presNameTxt[2][PRES_CARD_NAME_CAP];
	bool          presNameOk[2] = { false, false };
	for (int i = 0; i < 2; i++) {
		presence_reset(&presSt[i]);
		presence_ident_reset(&presId[i]);
		presence_walk_reset(&presWalk[i]);
		presence_card_reset(&presCard[i]);
		memset(&presSelf[i], 0, sizeof presSelf[i]);   // gameId 0 => P-G4 closes until a real fill
		memset(&presOut[i],  0, sizeof presOut[i]);
		memset(&presMeet[i], 0, sizeof presMeet[i]);
		presence_card_fill(NULL, &presCardTx[i]);      // well-formed placeholders from frame 0
		memset(presNameRaw[i], 0, sizeof presNameRaw[i]);
		presNameTxt[i][0] = '\0';
	}
	gs_log_reset();                 // fresh game-state instrumentation log for this play session
	touch_log_reset();              // fresh touch-event instrumentation log for this play session
	diag_wd_session_reset();        // D1: fresh watchdog episode + kill-switch check for this session
	ctl_init(&s_ctl[0], 0); ctl_init(&s_ctl[1], 1);   // D4: fresh script schedulers (SPEC C.3)
	ctl_publish(&s_ctl[0]); ctl_publish(&s_ctl[1]);
	ctl_close();                    // D4: a previous session's control log never leaks into this one
	s_ctlOn = false; s_ctlHdr[0] = '\0';
#if CTL_D5_ENABLE
	for (int i = 0; i < 2; i++) {   // D5: fresh recorder + replayer; no rec file leaks across
		ctl_rec_init(&s_ctlRec[i], i);
		ctl_rep_init(&s_ctlRep[i], i);
		ctl_rec_close(i);
		s_ctlInOk[i] = false;
		ctl_publish_rr(i, &s_ctlRec[i], &s_ctlRep[i]);
	}
#endif
#if CTL_D4_ENABLE || CTL_D5_ENABLE
	{   // D4.1: the whole feature arms iff sdmc:/cias/control exists — the operator opts in with
		// one mkdir, ONCE per session. No directory => zero polls, zero injection, no log file.
		// BOTH slices ride this one stat (D5 shares the opt-in directory), so it is gated on the
		// OR of the two bisect gates — otherwise CTL_D4_ENABLE 0 would silently disable D5 too.
		struct stat cst;
		s_ctlOn = (stat("sdmc:/cias/control", &cst) == 0);
		if (s_ctlOn) {   // D4.14 session-start mapping echo (the control log's first line)
			char ca[5] = "----", cb[5] = "----";
			if (emuA.core) gbacore_game_code(emuA.core, ca);
			if (emuB.core) gbacore_game_code(emuB.core, cb);
			snprintf(s_ctlHdr, sizeof s_ctlHdr,
			         "# control p1=%s p2=%s dir=sdmc:/cias/control  (p1=game A, p2=game B, fixed; "
			         "clock=emulated frames of that game)\n", ca, cb);
		}
	}
#endif
	int  menuSel = 0;
	int  menuTab = 0, menuRow = 0;   // tabbed pause menu (UI redesign)
	int  menuScroll = 0;             // content scroll offset (tall tabs scroll; see menu_layout)
	bool s3dEnabled = true;          // Enhance-tab master 3D toggle (the slider still gates depth)
	int  result = SESSION_QUIT;
	gfxSet3D(true);   // enable stereoscopic top screen; the right eye is driven below (slider-gated)
	char status[48] = "";   // last save/load result, shown in the menu (fits "Wireless: ON (join, <game>)")
	// Scale + filter are PER SCREEN ([0]=top, [1]=bottom): the 400x240 top and 320x240 bottom
	// have different best fits. ZR/ZL adjust the focused screen (X/Y switches focus).
	int  scaleMode[2] = { SCALE_FIT, SCALE_FIT };
	bool smooth[2]    = { false, false };
	bool swapped      = false;   // false: A=top / B=bottom. true: B=top / A=bottom.
	char toast[48] = "";
	int  toastTimer = 0;

	// HUD (menu-toggleable): per-screen game label + FPS + clock + battery.
	int  hudMode = 3;   // per-screen HUD/fps bitmask: 1=top 2=bottom
	char nameA[64], nameB[64];
	rom_display_name(pathA, nameA, sizeof nameA);
	rom_display_name(pathB, nameB, sizeof nameB);
	u64 fpsT0 = osGetTime();
	int fpsFrames = 0, fps = 0;
	float worstMs = 0.0f; int showMs = 0;   // worst frame work-time over the fps window
	u8  batLvl = 0;
	int batTimer = 0;

	// Audio: mode (solo/mixed/split) + per-game volume (0..256).
	int audioMode = AUD_SOLO;
	int volA = 256, volB = 256;

	settings_load(scaleMode, smooth, &swapped, &hudMode, &audioMode, &volA, &volB, &touchMode, &fsOn, &dofOn, &bloomOn, &lightOn, &vividOn, &presenceOn);   // restore prefs
	if (presenceOn) presence_art_ensure();   // finding 10: build the co-op sheet only for a session
	                                         // that actually starts with the pref on (idempotent;
	                                         // the other call site is the pause-menu row)
	if (single) swapped = false;   // 1-game: the game is ALWAYS on top (a stale swapped would blank it)
	settings_save(scaleMode, smooth, swapped, hudMode, audioMode, volA, volB, touchMode, fsOn, dofOn, bloomOn, lightOn, vividOn, presenceOn);   // persist picker-side g_prefs changes (mode/theme)
	if (startLinked && emuA.core && emuB.core && link) {   // picker's "START - LINKED": attach the cable now
		gbacore_link_attach(emuA.core, link, 0, link_cb_sleep, link_cb_wake, &emuA);
		gbacore_link_attach(emuB.core, link, 1, link_cb_sleep, link_cb_wake, &emuB);
		emuA.linked = emuB.linked = true;
		linkOn = true;
		LightEvent_Signal(&emuA.go);   // kick both workers into the linked free-run
		LightEvent_Signal(&emuB.go);
		snprintf(toast, sizeof toast, "Link cable: ON");
		toastTimer = 120;
	}
	gbacore_set_frameskip(emuA.core, (fsOn && focused != 0) ? 2 : 0);   // unfocused-frameskip
	gbacore_set_frameskip(emuB.core, (fsOn && focused != 1) ? 2 : 0);

	while (aptMainLoop()) {
		if (!g_appActive) { svcSleepThread(16 * 1000 * 1000); continue; }   // backgrounded: don't hog the cores
		u64 wfStart = svcGetSystemTick();
		g_renderSeq++;   // D1 render loop-seq (one store; SPEC D1.3)
		hidScanInput();
		u32 kDown = hidKeysDown();
		u32 kHeld = hidKeysHeld();
		u16 tk = 0;             // touch-injected keys for the bottom game this frame
		u16 ckA = 0, ckB = 0;   // D4 script-injected keys, per GAME SLOT (A = p1, B = p2)
		TouchSmart sm = { 0 };   // bottom game live state for SMART touch
		int tmEff = (single && touchMode == TOUCH_SMART) ? TOUCH_PAD : touchMode;   // SMART needs a bottom-screen game

		// HUD stats: FPS (0.5s window) + battery (throttled).
		fpsFrames++;
		u64 nowMs = osGetTime();
		if (nowMs - fpsT0 >= 500) { fps = (int)(fpsFrames * 1000 / (nowMs - fpsT0)); fpsFrames = 0; fpsT0 = nowMs;
		                            showMs = (int)(worstMs + 0.5f); worstMs = 0.0f; }
		if (s_hasPtm && --batTimer <= 0) { PTMU_GetBatteryLevel(&batLvl); batTimer = 60; }

#if DIAG_D1_ENABLE || DIAG_D2_ENABLE
		// ---- D1 watchdog + D2 hang-catcher sampler (~200ms; SPEC D1.6/D2.3) — BEFORE the menuOpen
		// split so it keeps sampling with the pause menu open (under wlOn the workers free-run
		// through the menu; the gs block below is non-menu-branch only and would go blind exactly
		// mid-wedge). All reads are plain loads / struct reads (gbacore_frame_counter,
		// gbacore_net_wd_counters) plus D2's ONE gbacore_read32 heartbeat read — the same
		// benign-race class as the HUD's gbacore_net_diag reads and the gs block's game_read.
		// D1 armed in the free-run modes (linkOn/netOn/wlOn — D1.7), where e->frame is a true
		// loop-seq; in plain unlinked play workers park between frames by design, so a frozen seq
		// means nothing there. D2 armed under wlOn only (D2.2 — the participant core).
		if (!s_diagOff && nowMs - s_wdLastMs >= 200) {
			s_wdLastMs = nowMs;
#if DIAG_D1_ENABLE
			// TWO INDEPENDENT EPISODES (review fix 2026-08-03; deviation from SPEC D1.7's single
			// wlOn mask, reasoned at the s_wdRx declaration and in diag.h). The worker mask must
			// NOT contain DIAG_WD_RX: diag_wd_step resets the episode when ANY watched seq moves,
			// and the RX thread's heartbeat moves ~2000x/s no matter how dead the worker is.
			uint32_t wdMask = 0, wdMaskRx = 0;
			if (wlOn) {   // watch only the PARTICIPANT worker (the other core is paused)...
				wdMask   = (g_netWorker == &emuB) ? (DIAG_WD_BF | DIAG_WD_BVF)
				                                  : (DIAG_WD_AF | DIAG_WD_AVF);
				wdMaskRx = DIAG_WD_RX;   // ...and the radio thread on its OWN clock (wlOn only: the
			} else if (linkOn || netOn) {   // other modes have no RX thread)
				wdMask = DIAG_WD_AF | DIAG_WD_BF | DIAG_WD_AVF | DIAG_WD_BVF;
			}
			DiagWdSample wds;
			wds.renderSeq = g_renderSeq;
			wds.aFrame    = emuA.frame;
			wds.bFrame    = emuB.frame;
			wds.aVf       = emuA.core ? gbacore_frame_counter(emuA.core) : 0;   // emulated-video seq: "worker
			wds.bVf       = emuB.core ? gbacore_frame_counter(emuB.core) : 0;   // spinning but game frozen" X-ray
			wds.rxSeq     = g_diagRxSeq;
			wds.netCrumb  = g_diagNetCrumb;
			wds.sioCrumb  = g_diagSioCrumb;
			// celio gate-health quick-reads for the STUCK line, out of D3's single read-only
			// counters seam (D1 deviation #3 folded into gbacore_net_counters — one struct fill
			// per 200 ms tick, all pure copies of the worker-captured statics).
			{ GbaNetCounters wdc; gbacore_net_counters(&wdc);
			  wds.gateN = wdc.celioGateN; wds.forceN = wdc.celioForceN; }
			wds.wl   = wlOn ? 1 : 0;
			wds.seat = wlOn ? wlSeat : -1;
			// Episode 1 = the emulator worker(s), episode 2 = the radio. BOTH are stepped every
			// tick (each keeps its own escalation clock) and both write to the same wd file; the
			// line's `who=` names the speaker. A tick where both fire writes two lines — that is
			// the "everything is dead" case and both facts are worth having.
			char wdLine[192];
			for (int wdEp = 0; wdEp < 2; wdEp++) {
				wds.who = wdEp ? DIAG_WD_WHO_RX : DIAG_WD_WHO_WORKER;
				if (!diag_wd_step(wdEp ? &s_wdRx : &s_wd, (uint32_t)nowMs, &wds,
				                  wdEp ? wdMaskRx : wdMask, wdLine, sizeof wdLine)) continue;
				if (!s_wdFile) {   // lazy-create at the FIRST STUCK event (by definition not the healthy hot path)
					mkdir("sdmc:/cias", 0777);           // ensure the netlog dirs exist (matches gbacore_net_log_dump)
					mkdir("sdmc:/cias/netlogs", 0777);
					char wp[96]; diag_log_path(wp, sizeof wp, "wd", "txt", wlOn ? wlSeat : -1);
					s_wdFile = fopen(wp, "w");
				}
				if (s_wdFile) { fputs(wdLine, s_wdFile); fflush(s_wdFile); }   // append + fflush per line (crash-safe)
			}
#endif
#if DIAG_D2_ENABLE
			// ---- D2 game-heartbeat hang catcher (SPEC D2.2-D2.5; pm-bridge-forensics §6/§12).
			// Armed ONLY while the wireless session wants the participant core (wlOn + g_netWorker;
			// under wlOn the OTHER core is deliberately paused and would false-trigger instantly).
			// Tier A = the core's produced-video-frame counter (struct read); Tier B = the game's
			// gMain.vblankCounter1 (ONE gbacore_read32 per tick; armed only when the profile maps
			// it — GameProfile.vblankCtr, verify-on-hw-pending). Frozen >180 render frames =>
			// diag_hang_step returns a dump action per tick (cap 24/episode, reset on advance) and
			// gbacore_dump_cpu's read-only ARM snapshot is appended to the lazy hang netlog.
			if (wlOn && g_netWorker && g_netWorker->core) {
				GbaCore* hangCore = g_netWorker->core;
				const GameProfile* hangProf = profile_for(hangCore);   // 4 ROM-header reads/tick — negligible
				uint32_t hvf  = gbacore_frame_counter(hangCore);
				uint32_t hvbl = (hangProf && hangProf->vblankCtr) ? gbacore_read32(hangCore, hangProf->vblankCtr) : 0;
				int harm = DIAG_HANG_ARM_CORE | ((hangProf && hangProf->vblankCtr) ? DIAG_HANG_ARM_GAME : 0);
				DiagHangAction hact = diag_hang_step(&s_hang, harm, g_renderSeq, hvf, hvbl);
				if (hact != DIAG_HANG_NONE) {
					GbaCpuDump cd;
					if (gbacore_dump_cpu(hangCore, &cd)) {
						static char hangBuf[2048];   // one whole dump (~16 lines); static = no per-dump stack/heap
						diag_hang_format(hangBuf, sizeof hangBuf, &cd, hvf, hvbl, (int)hact,
						                 (uint32_t)nowMs, g_diagNetCrumb, g_diagSioCrumb);
						if (!s_hangFile) {   // lazy-create at the FIRST dump (a healthy run writes no file)
							mkdir("sdmc:/cias", 0777);           // ensure the netlog dirs exist (matches gbacore_net_log_dump)
							mkdir("sdmc:/cias/netlogs", 0777);
							char hp[96]; diag_log_path(hp, sizeof hp, "hang", "txt", wlSeat);
							s_hangFile = fopen(hp, "w");
						}
						if (s_hangFile) { fputs(hangBuf, s_hangFile); fflush(s_hangFile); }   // append + fflush per dump
					}
				}
			} else if (s_hang.init) {
				// Session gone mid-episode (D2.2 disarm): clear the state so a stale freeze can't
				// leak into the next link even if diag_wd_session_reset were ever skipped.
				diag_hang_step(&s_hang, 0, g_renderSeq, 0, 0);
			}
#endif
		}
#endif

		if (!menuOpen) {
			bool combo = (kHeld & KEY_START) && (kHeld & KEY_SELECT);
			// With the virtual gamepad on, the touchscreen drives the game, so the menu opens
			// only via the combo; otherwise a tap opens the menu (the original behaviour).
			if ((touchMode == TOUCH_OFF && (kDown & KEY_TOUCH)) || combo) {
				menuOpen = true; menuSel = 0; menuTab = 0; menuRow = 0; menuScroll = 0; status[0] = '\0';
				if (!linkOn && !netOn && !wlOn && workersRunning) {   // finish the in-flight frame before pausing into the menu
					DIAG_CRUMB(g_diagMainCrumb, DIAG_SITE_MAIN_PIPE_WAIT, 0);   // D1 site 4000 bracket (post-mortem only)
					LightEvent_Wait(&emuA.done); LightEvent_Wait(&emuB.done);
					g_diagMainCrumb = 0;
					if (emuA.core) upload_frame(&emuA); if (emuB.core) upload_frame(&emuB);
					workersRunning = false;
				}
			} else {
				// PIPELINE: finish the PREVIOUS frame (started last iteration, ran during the render) and
				// snapshot it. We render N-1 while N computes -> render isn't chained to the slower core,
				// so non-link is as smooth as the link path. Workers are parked here -> touch RAM access safe.
				if (!linkOn && !netOn && !wlOn && workersRunning) {
					DIAG_CRUMB(g_diagMainCrumb, DIAG_SITE_MAIN_PIPE_WAIT, 0);   // D1 site 4000 bracket (post-mortem only)
					LightEvent_Wait(&emuA.done); LightEvent_Wait(&emuB.done);
					g_diagMainCrumb = 0;
					if (emuA.core) upload_frame(&emuA); if (emuB.core) upload_frame(&emuB);
					workersRunning = false;
				}
				if (kDown & KEY_Y) {
					if (wlOn) {   // WIRELESS LINK: Y cycles the A/B/C pacing EXPERIMENT (focus-switch is a no-op here
						// — the peer game is paused). The active state is stamped per-round into the netlog (exp col),
						// so one hardware run sweeps strategies. A=baseline(paced) B=free-run C=capped free-run.
						static const char* const EXP_NAMES[5] = { "A baseline (paced)", "B free-run", "C capped free-run", "D host-rate (walk+sync)", "E BRIDGE (cmd-framed)" };
						int ex = (gbacore_net_get_exp() + 1) % 6;   // A..F (F = Celio local termination)
						gbacore_net_set_exp(ex);
						snprintf(toast, sizeof toast, "Link exp: %s", EXP_NAMES[ex]);
						toastTimer = 120;
					} else if (!single) {                                    // switch focus (non-link)
						focused ^= 1; audio_reset_stream();
						gbacore_set_frameskip(emuA.core, (fsOn && focused != 0) ? 2 : 0);
						gbacore_set_frameskip(emuB.core, (fsOn && focused != 1) ? 2 : 0);
					}
				}
				if (!single && (kDown & KEY_X)) {                                         // swap which game is on which screen
					swapped = !swapped;
					snprintf(toast, sizeof toast, "Layout: %s", swapped ? "B top / A bottom" : "A top / B bottom");
					toastTimer = 90;
					settings_save(scaleMode, smooth, swapped, hudMode, audioMode, volA, volB, touchMode, fsOn, dofOn, bloomOn, lightOn, vividOn, presenceOn);
				}
				int fs = swapped ? (focused ^ 1) : focused;   // screen the focused game sits on
				if (kDown & KEY_ZR) {
					scaleMode[fs] = (scaleMode[fs] + 1) % 3;
					snprintf(toast, sizeof toast, "%s scale: %s",
					         fs == 0 ? "Top" : "Bottom", SCALE_NAMES[scaleMode[fs]]);
					toastTimer = 90;
					settings_save(scaleMode, smooth, swapped, hudMode, audioMode, volA, volB, touchMode, fsOn, dofOn, bloomOn, lightOn, vividOn, presenceOn);
				}
				if (kDown & KEY_ZL) {
					smooth[fs] = !smooth[fs];   // render_game sets the per-pass filters
					snprintf(toast, sizeof toast, "%s filter: %s", fs == 0 ? "Top" : "Bottom",
					         smooth[fs] ? "Smooth" : "Sharp-bilinear");
					toastTimer = 90;
					settings_save(scaleMode, smooth, swapped, hudMode, audioMode, volA, volB, touchMode, fsOn, dofOn, bloomOn, lightOn, vividOn, presenceOn);
				}
				u16 g = to_gba_keys(kHeld);
				// Touchscreen drives the BOTTOM game (A is on bottom iff swapped) as a POINTER on the
				// real game UI. touch_update is stateful (menu-cursor driver + tap-to-walk) -> run it
				// every gameplay frame when enabled.
				if (tmEff != TOUCH_OFF) {
					bool touching = (kHeld & KEY_TOUCH) != 0;
					touchPosition tp = { 0, 0 };
					if (touching) hidTouchRead(&tp);
					int gx = -1, gy = -1; bool gvalid = false;
					if (tmEff == TOUCH_SMART) {   // game-aware touch works even during a link (benign EWRAM race)
						gvalid = touch_to_gba(tp.px, tp.py, scaleMode[1], &gx, &gy);
						GbaCore* botCore = swapped ? emuA.core : emuB.core;
						const GameProfile* gp = profile_for(botCore);
						GameState gsr;
						if (game_read(botCore, gp, &gsr)) {
							sm.valid = gsr.valid; sm.ctx = gsr.ctx;
							sm.actionCursor = gsr.actionCursor; sm.moveCursor = gsr.moveCursor;
							sm.px = gsr.px; sm.py = gsr.py;
							for (int i = 0; i < 4; i++) sm.moveValid[i] = gsr.moveValid[i];
							sm.core = botCore; sm.actionAddr = gp->actionCursor; sm.moveAddr = gp->moveCursor;
							sm.prof = gp;
							sm.partyCount = gsr.partyCount; sm.partyLayout = gsr.partyLayout;
							sm.battlersCount = gsr.battlersCount; sm.absentMask = gsr.absentMask;
							for (int i = 0; i < 4; i++) sm.battlerPos[i] = gsr.battlerPos[i];
							sm.bagListTaskBase = gsr.bagListTaskBase;
							sm.cb2 = gsr.cb2; sm.ctxResolved = gsr.ctxResolved; sm.nTask = gsr.nTask;   // touch-log fingerprint (LOGGING ONLY)
							for (int i = 0; i < 8; i++) sm.taskFp[i] = gsr.taskFp[i];
						}
					}
					tk = touch_update(tmEff, touching, tp.px, tp.py, gx, gy, gvalid, &sm);
				}
				{   // stereoscopic depth: TOP game overworld state + on-screen OAM rects (cores parked)
					GbaCore* topCore = swapped ? emuB.core : emuA.core;
					const GameProfile* tprof = profile_for(topCore);
					GameState ts; depth3d.overworld = game_read(topCore, tprof, &ts) && ts.ctx == GCTX_OVERWORLD;
					depth3d.textTop = ts.textBanner;   // map-name banner lives in the top band
					depth3d.textBot = ts.textDlg;      // dialog textbox lives in the bottom band
					depth3d.nspr = 0; depth3d.nui = 0; depth3d.nfg = 0; depth3d.maxd = 0.0f; depth3d.camX = depth3d.camY = 0; memset(depth3d.tdepth, 0, sizeof depth3d.tdepth);
					depth3d.feetMin = depth3d.feetMax = depth3d.headMin = depth3d.headMax = 0.0f; depth3d.tallOk = depth3d.tallFail = 0; depth3d.orderOk = true;   // 3D disparity-detail (LOGGING ONLY)
					if (topCore) bg0_scan(topCore, tprof, depth3d.overworld, &depth3d);   // text flags + gen-3 UI panel rects
					if (depth3d.overworld && topCore) {
						static const unsigned char SW[3][4] = {{8,16,32,64},{16,32,32,64},{8,8,16,32}};
						static const unsigned char SH[3][4] = {{8,16,32,64},{8,8,16,32},{16,32,32,64}};
						for (int i = 0; i < 128 && depth3d.nspr < DEPTH_MAX_SPR; i++) {
							u16 a0 = gbacore_read16(topCore, 0x07000000 + i*8 + 0);
							u16 a1 = gbacore_read16(topCore, 0x07000000 + i*8 + 2);
							int aff = (a0 >> 8) & 1;
							if (!aff && ((a0 >> 9) & 1)) continue;            // OBJ disabled
							int shape = (a0 >> 14) & 3; if (shape == 3) continue;
							int w = SW[shape][(a1 >> 14) & 3], h = SH[shape][(a1 >> 14) & 3];
							if (aff && ((a0 >> 9) & 1)) { w *= 2; h *= 2; }   // double-size
							if (w > 32 || h > 32) continue;                   // characters only (skip big effects/UI)
							int y = a0 & 0xFF; if (y >= 160) y -= 256;
							int x = a1 & 0x1FF; if (x >= 256) x -= 512;
							if (x + w <= 0 || x >= GBA_W || y + h <= 0 || y >= GBA_H) continue;
							depth3d.spr[depth3d.nspr].x = (short)x; depth3d.spr[depth3d.nspr].y = (short)y;
							depth3d.spr[depth3d.nspr].w = (unsigned char)w; depth3d.spr[depth3d.nspr].h = (unsigned char)h;
							depth3d.spr[depth3d.nspr].elev = 0xFF;   // 0xFF = unmatched -> floor_at fallback
							depth3d.nspr++;
						}
						build_depth_grid(topCore, tprof, ts.px, ts.py, &depth3d);   // scenery depth (elevation priority planes)
						// B/C: tag each on-screen sprite with its object-event elevation tier (previousElevation =
						// what the engine uses for draw priority) so NPCs/the player pop with the tier they stand on.
						if (tprof->mapObjects) {
							short ogx[16], ogy[16]; unsigned char oel[16]; int nobj = 0;
							for (int o = 0; o < 16; o++) {
								uint32_t oe = tprof->mapObjects + 0x24u * (uint32_t)o;
								if (!(gbacore_read32(topCore, oe) & 1u)) continue;                       // active:1
								ogx[nobj] = (short)(int16_t)gbacore_read16(topCore, oe + 0x10);          // currentCoords.x (grid, +7)
								ogy[nobj] = (short)(int16_t)gbacore_read16(topCore, oe + 0x12);          // currentCoords.y
								oel[nobj] = (gbacore_read8(topCore, oe + 0x0B) >> 4) & 0x0F;             // previousElevation (high nibble)
								nobj++;
							}
							for (int s = 0; s < depth3d.nspr; s++) {                                     // match each sprite by feet grid-tile
								int col = (depth3d.spr[s].x + depth3d.spr[s].w / 2) / 16;
								int row = (depth3d.spr[s].y + depth3d.spr[s].h - 1) / 16;
								if (col < 0) col = 0; else if (col > 14) col = 14;
								if (row < 0) row = 0; else if (row > 9) row = 9;
								int ggx = ts.px + col, ggy = ts.py + row + 2;
								int best = -1, bestd = 3;
								for (int o = 0; o < nobj; o++) {
									int dd = abs(ogx[o] - ggx) + abs(ogy[o] - ggy);
									if (dd < bestd) { bestd = dd; best = o; }
								}
								if (best >= 0) depth3d.spr[s].elev = oel[best];
							}
						}
						depth_disparity_stats(&depth3d);   // LOGGING ONLY: per-sprite feet/head disparity stats for the gs log
					}
				}
				{   // ---- game-state instrumentation log (READ-ONLY; both games; edge-triggered) ----
					// Records each game's screen/geo/3D timeline to SD. Same parked-window reads the touch+3D
					// paths already do; captures during links too (the same benign EWRAM race they accept).
					GbaCore* gsTop = swapped ? emuB.core : emuA.core;
					GbaCore* gsBot = swapped ? emuA.core : emuB.core;
					const GameProfile* gpTop = profile_for(gsTop);
					const GameProfile* gpBot = profile_for(gsBot);
					GameState gst, gsb;
					// D3: the read results are reused for the CSV row below (SPEC D3.4 "do NOT
					// re-read"); game_read fills *out (memset + sentinels) even when it returns false.
					bool gsTopOk = game_read(gsTop, gpTop, &gst);
					bool gsBotOk = game_read(gsBot, gpBot, &gsb);
					{   // ---- phase 14 slice T3: the tilt gate's snapshot (SPEC-integration I1.1/I1.2)
						// Two struct copies off reads that ALREADY happened, in the window where the
						// workers are parked (main.c:2134 "touch RAM access safe"). gst/gsb are already
						// screen-mapped by the gsTop/gsBot swap above, so index = SCREEN. game_read fills
						// *out with memset + sentinels even when it returns false (gamestate.c:87-92), so
						// the !ok case lands as ctx = GCTX_NONE / px = -1 — belt and braces for G5/G7.
						const GameState* gsFor2[2] = { &gst, &gsb };
						const bool       okFor2[2] = { gsTopOk, gsBotOk };
						for (int sc = 0; sc < 2; sc++) {
							tiltSnap[sc].ok       = okFor2[sc] ? 1 : 0;
							tiltSnap[sc].ctx      = (uint8_t)gsFor2[sc]->ctx;
							tiltSnap[sc].sb1Valid = gsFor2[sc]->sb1Valid ? 1 : 0;
							tiltSnap[sc].textDlg  = gsFor2[sc]->textDlg ? 1 : 0;
							tiltSnap[sc].px       = (int16_t)gsFor2[sc]->px;
						}
					}
					{   // ---- phase 15 slice M1: co-op presence, fill + publish (SPEC-data D6.4) ----
						// Two POD fills and two publishes, in the window where both workers are parked
						// and the two GameStates are already in hand. NO writes to either bus
						// (invariant 1), NO new synchronisation (invariant 2), and — when the pref is
						// off — no new game-RAM reads at all.
						//
						// The condition below is a COST GUARD, not the gate. It decides whether the
						// three reads per game happen; the AUTHORITATIVE decision is still
						// presence_solve's P-G1..P-G9 ladder, which is fed the same flags and closes
						// P-G1/P-G2/P-G3 by itself. The two must stay a strict subset relationship:
						// anything the guard skips, the ladder must also refuse. It does — the guard
						// is exactly (P-G1 && !P-G2 && !P-G3).
						//
						// P-G3's link term (linkOn || netOn || wlOn) is load-bearing here rather than
						// belt-and-braces: main.c only parks the workers when !linkOn && !netOn &&
						// !wlOn (see the pipeline wait above), so DURING A LINK THE PARKED-WINDOW
						// GUARANTEE DOES NOT HOLD. The gs log and smart touch accept that benign EWRAM
						// race because a torn row is a bad log line — but a torn (px, py) is a visibly
						// teleporting avatar, and presence DRAWS its reads (D4.8).
						//
						// Index = GAME (0 = emuA, 1 = emuB), mapped off the same `swapped` the gsTop/
						// gsBot split above already applied — the identical shape the D4/D5 control
						// block uses two blocks down for its per-seat CtlIn.
						presence_begin_round(&presSt[0], g_renderSeq);
						presence_begin_round(&presSt[1], g_renderSeq);
						if (presenceOn && !menuOpen && !linkOn && !netOn && !wlOn) {
							GbaCore*           prCore[2] = { emuA.core, emuB.core };
							const GameProfile* prProf[2] = { swapped ? gpBot : gpTop, swapped ? gpTop : gpBot };
							const GameState*   prGs  [2] = { swapped ? &gsb  : &gst,  swapped ? &gst  : &gsb  };
							for (int gi = 0; gi < 2; gi++) {
								presence_read_fill(&presSelf[gi], gi, prCore[gi], prProf[gi], prGs[gi],
								                   &presId[gi], g_renderSeq);
								presFaceRaw[gi] = (int8_t)prGs[gi]->facing;   // LOGGING ONLY (D2.4.1)
								// Each game publishes ITS record as the OTHER game's peer. That single
								// line is the whole same-console transport, and it is the ONE call M4
								// replaces with a UDS beacon RX (D3.4).
								presence_publish(&presSt[gi ^ 1], 0, &presSelf[gi]);
							}
						}
					}
#if CTL_D4_ENABLE || CTL_D5_ENABLE
					// ---- D4 file-driven tile-exact movement (SPEC-control-replay.md §D4 / C.4)
					// + D5 record/replay, which share this one CtlIn snapshot and injection seam.
					// Runs HERE, at the existing parked-window read site, because the two
					// GameState snapshots above are exactly what the closed loop needs — the
					// player's live tile + map (SaveBlock1 pos/location) — so D4 adds NO game-RAM
					// reads and no new race class (SPEC §0). The scheduler clock is the scripted
					// core's EMULATED frame counter, so a paused seat / open pause menu /
					// backgrounded app freezes the script in place instead of blind-firing.
					// Output = a key mask ORed into the assembly below (the emulated keypad).
					// The outer gate is the OR of the two bisect gates so either slice can be
					// compiled out alone (review fix 2026-08-03).
					if (s_ctlOn) {
						const GameState* gsFor[2] = { swapped ? &gsb : &gst, swapped ? &gst : &gsb };
						GbaCore*         coFor[2] = { emuA.core, emuB.core };
						bool             pzFor[2] = { emuA.paused, emuB.paused };
#if !CTL_D4_ENABLE
						(void)pzFor;    // only the D4 move/go poll reports the paused-seat note
#endif
						// The seat's NON-script routed mask = the D4.11 abort trigger. It MIRRORS
						// the assembly at the end of this block, so a key the seat never receives
						// (3DS-level HUD keys, touch while TOUCH_OFF, the other game's pad) can
						// never abort a script — by construction, not by a list.
						u16 rkFor[2];
						rkFor[0] = single ? (u16)(g | tk) : (u16)(((focused == 0) ? g : 0) | (swapped ? tk : 0));
						rkFor[1] = single ? (u16)0        : (u16)(((focused == 1) ? g : 0) | (swapped ? 0 : tk));
#if CTL_D5_ENABLE
						s_ctlInOk[0] = s_ctlInOk[1] = false;   // a stale snapshot never records
#endif
						for (int sq = 0; sq < 2; sq++) {
							if (!coFor[sq]) continue;
							CtlIn ci;
							ci.emuFrame   = gbacore_frame_counter(coFor[sq]);
							ci.fieldValid = gsFor[sq]->valid && gsFor[sq]->ctx == GCTX_OVERWORLD &&
							                gsFor[sq]->sb1Valid && gsFor[sq]->px >= 0;   // D4.9
							ci.px         = (int16_t)gsFor[sq]->px;
							ci.py         = (int16_t)gsFor[sq]->py;
							ci.mapGroup   = (int16_t)gsFor[sq]->mapGroup;
							ci.mapNum     = (int16_t)gsFor[sq]->mapNum;
							ci.realKeys   = rkFor[sq];
#if CTL_D4_ENABLE
							ci.goSeen     = ctl_poll_seat(sq, pzFor[sq]);
							u16 m = ctl_tick(&s_ctl[sq], &ci);
#else
							ci.goSeen     = false;   // D4 bisected out: no move/go polling at all
							u16 m = 0;
#endif
#if CTL_D5_ENABLE
							// D5: a replay mask is a harness mask exactly like a script mask —
							// same emulated-keypad seam, ORed in the same additive way
							// (D4.12/D5.7). The snapshot is KEPT because the recorder needs the
							// FINAL assembled mask, which only exists after the key assembly
							// further down (D5.4).
							ctl_rr_poll_seat(sq);
							m = (u16)(m | ctl_rep_tick(&s_ctlRep[sq], &ci));
							s_ctlIn[sq] = ci; s_ctlInOk[sq] = true;
							ctl_publish_rr(sq, &s_ctlRec[sq], &s_ctlRep[sq]);
#endif
							if (sq == 0) ckA = m; else ckB = m;
							ctl_publish(&s_ctl[sq]);     // D4.14 '# control' netlog mirror
							ctl_drain_seat(sq);          // status lines -> the control log
#if CTL_D5_ENABLE
							ctl_rr_drain_seat(sq);       // ...and the record/replay lines
#endif
						}
					}
#endif   // CTL_D4_ENABLE || CTL_D5_ENABLE (the shared per-frame control tick)
					if (gsTopOk) {
						GsDepth gd = { (uint8_t)depth3d.overworld, (uint8_t)depth3d.textTop, (uint8_t)depth3d.textBot,
						               (short)depth3d.nspr, (short)depth3d.nui, (short)depth3d.nfg,
						               depth3d.maxd, (short)depth3d.camX, (short)depth3d.camY };
						gd.feetMin = depth3d.feetMin; gd.feetMax = depth3d.feetMax;            // per-sprite disparity detail (3D-effect proof)
						gd.headMin = depth3d.headMin; gd.headMax = depth3d.headMax;
						gd.tallOk = depth3d.tallOk; gd.tallFail = depth3d.tallFail; gd.orderOk = depth3d.orderOk ? 1 : 0;
						gd.s3d = (osGet3DSliderState() > 0.03f && !menuOpen) ? 1 : 0;          // stereoscopic engaged this frame (read-only)
						// phase 14 (I6.4): the tilt actually in force, so a hardware photo can be read
						// against the gate. Both screens ride this TOP row (gs_log_sample only gets a
						// depth block for screen 0). These are the PREVIOUS frame's settled values —
						// this block runs in the parked window and tilt_tween_step runs later, in the
						// render phase — i.e. the state of the frame the player just saw, which is
						// exactly what a photo shows. LOGGING ONLY; nothing reads it back.
						gd.tiltLvl    = (uint8_t)tiltTw[0].level;
						gd.tiltAngTop = tiltTw[0].ang;
						gd.tiltAngBot = tiltTw[1].ang;
						// phase 15 (A6.5.3): the TOP game's peer — the surface that actually fires
						// in a same-console run, because the D3 CSV's peer columns only write during
						// a wireless session and P-G3 turns presence off for the whole of one. Same
						// PREVIOUS-frame relationship as the tilt block above, for the same reason.
						// A pure read of state already in hand: no game RAM, no solve, no new sync.
						{
							int pg = swapped ? 1 : 0;                  // the game on the TOP screen
							const PresenceState* gps = &presSt[pg];
							const PeerPresence*  gpr = &gps->rec[0];
							int have = gps->have[0] ? 1 : 0;
							gd.prLive   = (uint8_t)presence_liveness(gps, 0);
							gd.prDrawn  = (uint8_t)(presDraw[pg] ? 1 : 0);
							gd.prReason = (uint8_t)presOut[pg].reason;
							gd.prFace   = (int8_t)(have ? (int)gpr->facing : -1);
							gd.prMapG   = (int16_t)(have ? (int)gpr->mapGroup : -1);
							gd.prMapN   = (int16_t)(have ? (int)gpr->mapNum   : -1);
							gd.prPx     = (int16_t)((have && (gpr->flags & PRES_F_SB1VALID)) ? (int)gpr->px : -1);
							gd.prPy     = (int16_t)((have && (gpr->flags & PRES_F_SB1VALID)) ? (int)gpr->py : -1);
						}
						// injKeys carries the TOP game's script mask so D4 injection shows up in the same
						// timeline as ctx/geo (D4.14 — free correlation, no new log).
						gs_log_sample(gsTop, gpTop, &gst, 0, (u16)(swapped ? ckB : ckA), &gd, (uint32_t)nowMs);   // screen 0 = top/3D
					}
					if (gsBotOk)
						gs_log_sample(gsBot, gpBot, &gsb, 1, (u16)(tk | (swapped ? ckA : ckB)), NULL, (uint32_t)nowMs);   // screen 1 = bottom/touch (touch + script keys)
#if DIAG_D3_ENABLE
					// ---- D3 per-frame CSV telemetry row (SPEC-firmware-diag D3.3-D3.6; LOGGING
					// ONLY — reads + one buffered fwrite, zero heap, zero writes to game RAM).
					// Armed like D2.2: wlOn + the participant core (s_csvFile is non-NULL only when
					// the wireless link opened it and the diag_off.txt kill switch was absent).
					// Menu-open frames emit no row (this block is non-menu-branch) — the resulting
					// GAP in the rf column IS the menu marker, disclosed in the file header.
					if (s_csvFile && wlOn && g_netWorker && g_netWorker->core) {
						GbaCore* csvCore = g_netWorker->core;
						// The participant's GameState is ALREADY in hand: top/bottom mapping per the
						// gsTop/gsBot swap above (SPEC D3.4). game_read filled the struct either way.
						const GameState*   pg = (csvCore == gsTop) ? &gst  : &gsb;
						const GameProfile* pp = (csvCore == gsTop) ? gpTop : gpBot;
						GbaNetCounters nc; gbacore_net_counters(&nc);
						DiagCsvRow r;
						memset(&r, 0, sizeof r);
						r.tms  = (uint32_t)nowMs; r.rf = g_renderSeq; r.exp = gbacore_net_get_exp();
						r.ctx  = (int)pg->ctx;  r.cb2 = pg->cb2;
						r.px   = pg->px;        r.py  = pg->py;
						r.mapg = pg->mapGroup;  r.mapn = pg->mapNum;
						r.objx = pg->objX;      r.objy = pg->objY;  r.face = pg->facing;
						r.sb1  = pg->sb1Valid ? 1 : 0;
						r.lstat = pg->linkStatus; r.lerr = pg->linkErr; r.lnrecv = pg->linkNotRecv;
						r.lbuf0 = pg->linkErrBuf0; r.lbuf1 = pg->linkErrBuf1;
						// game heartbeat: ONE gbacore_read32/frame (gs-block benign-race class). The
						// D2.1 vblankCtr address is verify-on-hw-PENDING — this column ticking ~60/s
						// in the run-#13 CSV is exactly its promotion to verified.
						r.vbl = (pp && pp->vblankCtr) ? gbacore_read32(csvCore, pp->vblankCtr) : 0;
						r.clSec = nc.clSection; r.clSt = nc.clState; r.clBlk = nc.clBlk;
						r.clFrm = nc.clFrames;  r.clPB = nc.clPartyBytes; r.clTC = nc.clTradeC;
						r.clHP  = nc.clHeldParty; r.clHS = nc.clHeldSel;  r.clHC = nc.clHeldConf;
						r.clSelL = nc.clSelLocal; r.clSelP = nc.clSelPeer;
						r.clExitP = nc.clExitP; r.clSessEnd = nc.clSessEnd;
						r.clPCard = nc.clPCard; r.clIdReal = nc.clIdReal; r.clOutQ = nc.clOutQ;
						r.gateN = nc.celioGateN; r.cForceN = nc.celioForceN; r.resetN = nc.celioResetN;
						r.sioMode = nc.celioSioMode; r.siocnt = nc.celioSiocnt;
						r.startN = nc.startN; r.injN = nc.injN; r.finN = nc.finN;
						r.okN = nc.okN; r.toN = nc.toN; r.edgeN = nc.edgeN; r.forceN = nc.forceN;
						r.round = nc.round; r.lastW0 = nc.lastW0; r.lastW1 = nc.lastW1; r.lastOk = nc.lastOk;
						{ int rttMs = -1, drops = 0; net_link_get_rtt(&rttMs, &drops); r.rtt = rttMs; }
						// ONE LOCK-FREE call for all six EVENT-channel columns (review fix
						// 2026-08-03). This used to be net_event_get_stats + net_event_get_queue,
						// i.e. TWO s_evLock acquisitions per render frame — and the RX thread holds
						// that lock across udsSendTo bursts (the ~16 ms un-ACKed-tail re-send), so
						// per-frame telemetry could block the render loop behind radio I/O and add a
						// third contender next to the frozen event plane. Benign-race reads now, the
						// same contract as every other counter column.
						{ int ts = 0, ta = 0, rd = 0, ov = 0, rt = 0, q = 0;
						  net_event_get_stats_fast(&ts, &ta, &rd, &ov, &rt, &q);
						  r.txSeq = ts; r.txAcked = ta; r.rxDel = rd; r.evOvf = ov; r.evRetx = rt;
						  r.evTxQ = q; }
						{ int rxW = 0, txF = 0, busy = 0, pUp = 0;
						  net_link_get_stats(&rxW, &txF, &busy, &pUp, NULL);
						  r.rxWordN = rxW; r.txFails = txF; r.busyN = busy; r.peerUp = pUp; }
						// ---- phase 15 slice M3: the peer columns (SPEC-avatar A6.5) ----
						// A pure read of state already computed — no game-RAM access, no solve, no
						// second parked-window pass. DORMANT in this phase by construction: gate
						// P-G3 blocks the whole feature while wlOn is true (D4.8) and this block
						// only runs while wlOn is true, so every column below reports the "off"
						// case. That is the point (A6.5.2): M4's first run is instrumented on day
						// one rather than retrofitted, and the header/row parity is settled now.
						// The values are the PREVIOUS frame's solve, exactly like gd.tiltLvl above.
						{
							int csvGi = (csvCore == emuA.core) ? 0 : 1;   // presence is keyed by GAME
							const PresenceState* cps = &presSt[csvGi];
							const PeerPresence*  cpr = &cps->rec[0];
							int haveRec = cps->have[0] ? 1 : 0;
							r.prLive   = presence_liveness(cps, 0);
							r.prMapg   = haveRec ? (int)cpr->mapGroup : -1;
							r.prMapn   = haveRec ? (int)cpr->mapNum   : -1;
							r.prPx     = (haveRec && (cpr->flags & PRES_F_SB1VALID)) ? (int)cpr->px : -1;
							r.prPy     = (haveRec && (cpr->flags & PRES_F_SB1VALID)) ? (int)cpr->py : -1;
							r.prSubX   = haveRec ? (int)cpr->subX : 0;
							r.prSubY   = haveRec ? (int)cpr->subY : 0;
							r.prFace   = haveRec ? (int)cpr->facing : -1;
							r.prRound  = haveRec ? cpr->round : 0u;
							r.prDrawn  = presDraw[csvGi];
							r.prReason = presOut[csvGi].reason;
							// A0.2.2's free consistency check: gObjectEvents[0].currentCoords tracks
							// SaveBlock1.pos + MAP_OFFSET, so this must read 7 whenever both are
							// valid. Anything else means a mis-mapped mapObjects — the exact class of
							// defect that would otherwise look like "the avatar is 7 tiles off".
							r.prObjD = (haveRec && cpr->objX >= 0 && (cpr->flags & PRES_F_SB1VALID))
							         ? (int)cpr->objX - (int)cpr->px : -1;
						}
						static char csvBuf[512];   // static = no per-frame stack/heap (SPEC D3.5)
						int rl = diag_csv_row(csvBuf, sizeof csvBuf, &r);
						if (rl > 0) {
							fwrite(csvBuf, 1, (size_t)rl < sizeof csvBuf ? (size_t)rl : sizeof csvBuf - 1, s_csvFile);
							// flush-256 = the PM apCsv cadence (an fflush at most every ~4 s); the
							// 8 KB setvbuf below it means the underlying FS write actually happens
							// every ~36 rows either way, so this only bounds what a hard crash loses.
							if (++s_csvRows >= 256) { fflush(s_csvFile); s_csvRows = 0; }
							// ROW CAP (review fix): stop at ~30 min of rows instead of growing the
							// file for the whole session. One honest final line, then close.
							if (++s_csvTotal >= DIAG_CSV_MAX_ROWS) {
								fprintf(s_csvFile, "# csv capped at %lu rows (DIAG_CSV_MAX_ROWS) - telemetry stops here\n",
								        (unsigned long)s_csvTotal);
								fclose(s_csvFile); s_csvFile = NULL; s_csvRows = 0;
							}
						}
					}
#endif
				}
				// D4 (SPEC D4.12): script keys are ORed in, strictly additive — no existing bit is
				// ever cleared and nothing else writes these words. ckA/ckB are 0 unless a script is
				// running on that slot, so with no control directory this is the original assembly.
				if (single) { emuA.keys = g | tk | ckA; emuB.keys = 0; }   // one game: pad + touch both drive it
				else {
					emuA.keys = ((focused == 0) ? g : 0) | (swapped ? tk : 0) | ckA;
					emuB.keys = ((focused == 1) ? g : 0) | (swapped ? 0 : tk) | ckB;
				}
#if CTL_D5_ENABLE
				// D5.4 RECORD the seat's FINAL assembled mask — real pad + touch + any script or
				// replay keys, i.e. exactly what the core received; that is the only thing that
				// replays faithfully. PASSIVE: this READS the word, never writes one, so a
				// recording can never fight live input (PM mp_bridge.cpp:1614, by construction).
				if (s_ctlOn) {
					if (s_ctlInOk[0]) ctl_rec_feed(0, (u16)emuA.keys, emuA.core);
					if (s_ctlInOk[1]) ctl_rec_feed(1, (u16)emuB.keys, emuB.core);
				}
#endif
				if (linkOn || netOn || wlOn) {
					// Workers free-run + pump their own audio rings; main just samples the latest frames
					// and stays responsive. Audio keeps playing during a link (rings are worker-private).
					if (wlOn) {
						net_ping_update(NULL, NULL, NULL);       // send-only outbound ping; the RX thread echoes + times it
						net_link_get_rtt(&wlRtt, &wlDrops);      // RX-thread-measured RTT for the HUD
						if (!net_session_active()) {             // peer/session dropped (e.g. resumed after HOME) -> tear down
							EmuInstance* part = g_netWorker ? g_netWorker : &emuA;   // the participant (focused game at link start)
							EmuInstance* other = (part == &emuA) ? &emuB : &emuA;
							part->netLinked = false;
							DIAG_CRUMB(g_diagMainCrumb, DIAG_SITE_MAIN_WL_TEARDOWN, 0);   // D1 site 4100 bracket
							LightEvent_Wait(&part->done);
							g_diagMainCrumb = 0;
							wl_dump(wlSeat);   // dump the per-round link log to a timestamped SD file
							gs_dump(wlSeat);   // + the game-state log (HOST/JOIN-tagged; captures the link-error reason)
							diag_wd_close();   // D1/D3: close the wd/hang logs + the CSV (flushes its buffered tail)
							gbacore_net_detach(part->core); net_link_stop();
							g_netWorker = NULL; other->paused = false; wlOn = false;
							snprintf(status, sizeof status, "Wireless link closed");
						}
					}
					if (emuA.core) upload_frame(&emuA);
					if (emuB.core) upload_frame(&emuB);          // emuB shows its last (paused) frame under wlOn
				} else {
					LightEvent_Signal(&emuA.go);   // start THIS frame; it is waited at the top of next iter
					LightEvent_Signal(&emuB.go);   // (render below overlaps this emulation)
					workersRunning = true;
				}
				// The audio thread (core 1) mixes the worker-filled rings + feeds ndsp off the frame path.
				audio_set_params(focused, audioMode, volA, volB);
			}
		} else {
				// ---- Pause menu (v3, plate-composited): PCtl controls at manifest coords. L/R tab,
				// up/down focus, left/right adjust, A/tap activate. Buttons + link toggles route to the
				// legacy menuSel dispatch; segs/steppers/swatch/3D/preview handled inline.
				if (kDown & KEY_L) { menuTab = (menuTab + 5) % 6; menuRow = 0; }
				if (kDown & KEY_R) { menuTab = (menuTab + 1) % 6; menuRow = 0; }
				const PCtl* PT = PTABS[menuTab]; int nP = PTABN[menuTab];
				if (menuRow >= nP) menuRow = 0;
				if (kDown & (KEY_DDOWN | KEY_CPAD_DOWN)) menuRow = (menuRow + 1) % nP;
				if (kDown & (KEY_DUP   | KEY_CPAD_UP))   menuRow = (menuRow - 1 + nP) % nP;
				if (kDown & KEY_B) menuOpen = false;
				bool activate = (kDown & KEY_A) != 0;
				int adj = (kDown & (KEY_DRIGHT | KEY_CPAD_RIGHT)) ? 1 : ((kDown & (KEY_DLEFT | KEY_CPAD_LEFT)) ? -1 : 0);
				int segSet = -1;
				if (kDown & KEY_TOUCH) {
					touchPosition mtp; hidTouchRead(&mtp);
					if (mtp.px < 86) { int t2 = ((int)mtp.py - 8) / 30; if (t2 < 0) t2 = 0; if (t2 > 5) t2 = 5;
					                   if (t2 != menuTab) { menuTab = t2; menuRow = 0; PT = PTABS[menuTab]; nP = PTABN[menuTab]; } }
					else for (int i2 = 0; i2 < nP; i2++) {
						const PCtl* c2 = &PT[i2];
						if (mtp.px < c2->x || mtp.px >= c2->x + c2->w || mtp.py < c2->y || mtp.py >= c2->y + c2->h) continue;
						menuRow = i2;
						if (c2->kind == PK_SEG)   segSet = ui_seg_hit(c2->x, c2->w, c2->nseg, (float)mtp.px);
						else if (c2->kind == PK_STEP) adj = (mtp.px < c2->x + 24) ? -1 : (mtp.px > c2->x + c2->w - 24 ? 1 : 0);
						else if (c2->kind == PK_SWATCH) { int cc = ((int)mtp.px - c2->x) / 30; if (cc<0)cc=0; if (cc>4)cc=4;
							g_prefs.padColor = cc; settings_save(scaleMode, smooth, swapped, hudMode, audioMode, volA, volB, touchMode, fsOn, dofOn, bloomOn, lightOn, vividOn, presenceOn); }
						else activate = true;
						break;
					}
				}
				int act = PT[menuRow].act;
				int pkind = PT[menuRow].kind;
				if (pkind == PK_SEG && (segSet >= 0 || adj)) {
					int cur, n = PT[menuRow].nseg;
					// I4.6 — THE TRAP: both of these switches end in `default:` reading/WRITING
					// g_prefs.padEdge, so a new PK_SEG without its own case does not fail loudly, it
					// silently re-skins the virtual gamepad. ACT_TILT gets an explicit case in all
					// four (two here, two in run_settings).
					switch (act) { case ACT_SCALE_TOP: cur=scaleMode[0]; break; case ACT_SCALE_BOT: cur=scaleMode[1]; break;
						case ACT_FILTER: cur = smooth[swapped?(focused^1):focused]; break; case ACT_HUD: cur=hudMode; break;
						case ACT_AUDIOMODE: cur=audioMode; break; case ACT_TOUCHMODE: cur=touchMode; break;
						case ACT_TILT: cur=g_prefs.tiltLevel; break;
						default: cur=g_prefs.padEdge; break; }
					cur = (segSet >= 0) ? segSet : ((cur + adj + n) % n);
					switch (act) { case ACT_SCALE_TOP: scaleMode[0]=cur; break; case ACT_SCALE_BOT: scaleMode[1]=cur; break;
						case ACT_FILTER: smooth[swapped?(focused^1):focused]=cur; break; case ACT_HUD: hudMode=cur; break;
						case ACT_AUDIOMODE: audioMode=cur; audio_reset_stream(); break; case ACT_TOUCHMODE: touchMode=cur; break;
						case ACT_TILT: g_prefs.tiltLevel=cur; break;
						default: g_prefs.padEdge=cur; break; }
					// I4.14: name the two things a player cannot see from the row itself — that the
					// effect is overworld-only (PHASE.md invariant 5), and that an Old 3DS keeps the
					// SETTING but not the effect (G2 clamps the LIVE level only, I5.1/I5.2).
					if (act == ACT_TILT)
						snprintf(status, sizeof status, isN3DS ? "Tilt: %s (overworld only)" : "Tilt: %s — New 3DS only",
						         TILT_NAMES[cur]);
					settings_save(scaleMode, smooth, swapped, hudMode, audioMode, volA, volB, touchMode, fsOn, dofOn, bloomOn, lightOn, vividOn, presenceOn);
					activate = false;
				}
				else if (pkind == PK_STEP && adj) {
					int* v = (act == ACT_VOLA) ? &volA : &volB;
					*v += adj * 32; if (*v < 0) *v = 0; else if (*v > 256) *v = 256;
					settings_save(scaleMode, smooth, swapped, hudMode, audioMode, volA, volB, touchMode, fsOn, dofOn, bloomOn, lightOn, vividOn, presenceOn);
					activate = false;
				}
				else if (activate && act == ACT_3D) {
					s3dEnabled = !s3dEnabled;
					snprintf(status, sizeof status, "3D %s (slider gates depth)", s3dEnabled ? "on" : "off");
					activate = false;
				}
				// Phase 15 / A6.2 site 1. ACT_PRESENCE is a redesign-only id (>= 100), so like
				// ACT_3D it needs its own branch here — the legacy `menuSel` chain below only
				// covers ids 0..19. A6.2.1's status line names the two things the row itself
				// cannot show: that the feature is same-map + overworld only.
				else if (activate && act == ACT_PRESENCE) {
					presenceOn = !presenceOn;
					if (presenceOn) presence_art_ensure();   // finding 10: the 64 KB sheet is built on
					                                         // FIRST ENABLE, not at boot. Safe here:
					                                         // the menu runs before C3D_FrameBegin,
					                                         // which the blocking display transfer
					                                         // inside it requires.
					// FIX PASS (review finding 3). The map-universe gate (SPEC-data D4.3) is CORRECT
					// and stays: (mapGroup, mapNum) is only meaningful inside ONE game's map table, so
					// an Emerald + FireRed pair that both report "(3, 12)" would otherwise draw a peer
					// walking around an unrelated map — a category error that looks like a
					// mysteriously wrong position. What was missing is that NOTHING SAID SO: the row
					// turned on, the CO-OP chip stayed dim, and the only clue was the word `universe`
					// in a debug readout. The user's own carts are exactly that pair, so it is the
					// FIRST thing they would hit. profile_for is 4 ROM-header reads — main.c:2619
					// already calls it outside the parked window for the same reason (ROM is
					// immutable, so it is safe with the workers running).
					if (!presenceOn) snprintf(status, sizeof status, "Co-op: off");
					else {
						const GameProfile* pa = emuA.core ? profile_for(emuA.core) : NULL;
						const GameProfile* pb = emuB.core ? profile_for(emuB.core) : NULL;
						int ga = pa ? presence_game_id(pa->code) : PRES_GAME_NONE;
						int gb = pb ? presence_game_id(pb->code) : PRES_GAME_NONE;
						// (the strings are kept short on purpose: `status` is 48 bytes and a truncated
						//  explanation is worse than the silence it replaces)
						if (ga == PRES_GAME_NONE || gb == PRES_GAME_NONE)
							snprintf(status, sizeof status, "Co-op: on — no profile: nothing draws");
						else if (ga != gb)
							snprintf(status, sizeof status, "Co-op: on — %s vs %s: no peer",
							         ga == PRES_GAME_HOENN ? "Hoenn" : "Kanto",
							         gb == PRES_GAME_HOENN ? "Hoenn" : "Kanto");
						else
							snprintf(status, sizeof status, "Co-op: on — same map, overworld only");
					}
					settings_save(scaleMode, smooth, swapped, hudMode, audioMode, volA, volB, touchMode, fsOn, dofOn, bloomOn, lightOn, vividOn, presenceOn);
					activate = false;
				}
				else if (activate && (act == ACT_PREVIEW_PAD || act == ACT_PREVIEW_SMART)) {
					touchMode = (act == ACT_PREVIEW_PAD) ? TOUCH_PAD : TOUCH_SMART;
					settings_save(scaleMode, smooth, swapped, hudMode, audioMode, volA, volB, touchMode, fsOn, dofOn, bloomOn, lightOn, vividOn, presenceOn);
					menuOpen = false; activate = false;
				}
				
			if (activate) menuSel = act;   // route the legacy action into the original dispatch below
			if (activate) {
				if      (menuSel == 0) menuOpen = false;         // Resume
				else if (menuSel == 1) {                         // Link cable (experimental)
					if (!emuA.core || !emuB.core || !link) {
						snprintf(status, sizeof status, "Link needs 2 games");
					} else if (netOn || wlOn) {
						snprintf(status, sizeof status, "Stop net/WL link first");
					} else if (!linkOn) {
						gbacore_link_attach(emuA.core, link, 0, link_cb_sleep, link_cb_wake, &emuA);
						gbacore_link_attach(emuB.core, link, 1, link_cb_sleep, link_cb_wake, &emuB);
						emuA.linked = emuB.linked = true;
						linkOn = true;
						LightEvent_Signal(&emuA.go);   // kick both workers into free-run
						LightEvent_Signal(&emuB.go);
						snprintf(status, sizeof status, "Link: ON (beta)");
					} else {
						emuA.linked = emuB.linked = false;        // free-run loops will exit
						LightEvent_Signal(&emuA.waitEv);          // release any parked worker
						LightEvent_Signal(&emuB.waitEv);
						LightEvent_Wait(&emuA.done);              // wait for free-run to finish
						LightEvent_Wait(&emuB.done);
						gbacore_link_detach(emuA.core);
						gbacore_link_detach(emuB.core);
						linkOn = false;
						snprintf(status, sizeof status, "Link: off");
					}
				}
				else if (menuSel == 2) {                         // Audio mode (Solo/Mixed/Split)
					audioMode = (audioMode + 1) % 3;
					snprintf(status, sizeof status, "Audio: %s", AUDIO_NAMES[audioMode]);
					audio_reset_stream();                        // clean cut between modes
					settings_save(scaleMode, smooth, swapped, hudMode, audioMode, volA, volB, touchMode, fsOn, dofOn, bloomOn, lightOn, vividOn, presenceOn);
				}
				else if (menuSel == 3) {                         // Touch mode (off / gamepad / smart)
					touchMode = (touchMode + 1) % 3;
					snprintf(status, sizeof status, "Touch: %s", TOUCH_NAMES[touchMode]);
					settings_save(scaleMode, smooth, swapped, hudMode, audioMode, volA, volB, touchMode, fsOn, dofOn, bloomOn, lightOn, vividOn, presenceOn);
				}
				else if (menuSel == 4) {                         // Frameskip (unfocused game)
					fsOn = !fsOn;
					gbacore_set_frameskip(emuA.core, (fsOn && focused != 0) ? 2 : 0);
					gbacore_set_frameskip(emuB.core, (fsOn && focused != 1) ? 2 : 0);
					snprintf(status, sizeof status, "Frameskip %s", fsOn ? "on" : "off");
					settings_save(scaleMode, smooth, swapped, hudMode, audioMode, volA, volB, touchMode, fsOn, dofOn, bloomOn, lightOn, vividOn, presenceOn);
				}
				else if (menuSel == 5) {                         // Toggle HUD
					hudMode = (hudMode + 1) & 3;
					snprintf(status, sizeof status, "HUD: %s", HUD_NAMES[hudMode]);
					settings_save(scaleMode, smooth, swapped, hudMode, audioMode, volA, volB, touchMode, fsOn, dofOn, bloomOn, lightOn, vividOn, presenceOn);
				}
				else if (menuSel == 6 && single) {               // Swap: meaningless with one game
					snprintf(status, sizeof status, "No swap in 1-game mode");
				}
				else if (menuSel == 6) {                         // Swap screens
					swapped = !swapped; menuOpen = false;
					snprintf(toast, sizeof toast, "Layout: %s", swapped ? "B top / A bottom" : "A top / B bottom");
					toastTimer = 90;
					settings_save(scaleMode, smooth, swapped, hudMode, audioMode, volA, volB, touchMode, fsOn, dofOn, bloomOn, lightOn, vividOn, presenceOn);
				}
				else if ((menuSel == 7 || menuSel == 8 || menuSel == 9) && (linkOn || netOn || wlOn)) {
					snprintf(status, sizeof status, "Stop the link first");   // save/load/.sav would race a live core
				}
				else if (menuSel == 7) {                         // Save state (focused game)
					EmuInstance* fg = (focused == 0) ? &emuA : &emuB;
					snprintf(status, sizeof status, "%s", gbacore_save_state(fg->core, 1) ? "State saved" : "Save failed");
				}
				else if (menuSel == 8) {                         // Load state (focused game)
					EmuInstance* fg = (focused == 0) ? &emuA : &emuB;
					snprintf(status, sizeof status, "%s", gbacore_load_state(fg->core, 1) ? "State loaded" : "No state");
				}
				else if (menuSel == 9) {                         // Load a .sav into the focused game
					EmuInstance* fg = (focused == 0) ? &emuA : &emuB;
					char savp[256];
					if (fg->core && savpicker_run(top, bot, txtBuf, savp, sizeof savp))
						snprintf(status, sizeof status, "%s", gbacore_load_save(fg->core, savp) ? "Loaded .sav" : ".sav failed");
					else
						snprintf(status, sizeof status, "no .sav files");
				}
				else if (menuSel == 10) {                        // Mute (hard: stops sound rendering)
					muted = !muted; audio_set_muted(muted);
					snprintf(status, sizeof status, "Mute %s", muted ? "on" : "off");
				}
				else if (menuSel == 11) {                        // Pause / resume the focused game
					EmuInstance* fg = (focused == 0) ? &emuA : &emuB;
					fg->paused = !fg->paused;
					snprintf(status, sizeof status, "Game %c %s", focused == 0 ? 'A' : 'B', fg->paused ? "paused" : "resumed");
				}
				else if (menuSel == MENU_DOF_IDX) {              // HD-2D tilt-shift DoF (top screen)
					dofOn = !dofOn;
					snprintf(status, sizeof status, "DoF %s", dofOn ? "on" : "off");
					settings_save(scaleMode, smooth, swapped, hudMode, audioMode, volA, volB, touchMode, fsOn, dofOn, bloomOn, lightOn, vividOn, presenceOn);
				}
				else if (menuSel == MENU_BLOOM_IDX) {            // HD-2D LDR bloom (focused top)
					bloomOn = !bloomOn;
					snprintf(status, sizeof status, "Bloom %s", bloomOn ? "on" : "off");
					settings_save(scaleMode, smooth, swapped, hudMode, audioMode, volA, volB, touchMode, fsOn, dofOn, bloomOn, lightOn, vividOn, presenceOn);
				}
				else if (menuSel == MENU_LIGHT_IDX) {            // HD-2D time-of-day lighting
					lightOn = !lightOn;
					snprintf(status, sizeof status, "Light %s", lightOn ? "on" : "off");
					settings_save(scaleMode, smooth, swapped, hudMode, audioMode, volA, volB, touchMode, fsOn, dofOn, bloomOn, lightOn, vividOn, presenceOn);
				}
				else if (menuSel == MENU_VIVID_IDX) {            // bright+sharp "sign look" everywhere
					vividOn = !vividOn;
					snprintf(status, sizeof status, "Vivid %s", vividOn ? "on" : "off");
					settings_save(scaleMode, smooth, swapped, hudMode, audioMode, volA, volB, touchMode, fsOn, dofOn, bloomOn, lightOn, vividOn, presenceOn);
				}
				else if (menuSel == MENU_WIRELESS_IDX) {        // wireless multi-console lobby (M1) -> M3 link
					if (wlOn) {                                  // already linked -> stop the wireless link
						EmuInstance* part = g_netWorker ? g_netWorker : &emuA;   // the participant (focused game at link start)
						EmuInstance* other = (part == &emuA) ? &emuB : &emuA;
						part->netLinked = false;
						DIAG_CRUMB(g_diagMainCrumb, DIAG_SITE_MAIN_WL_TEARDOWN, 0);   // D1 site 4100 bracket
						LightEvent_Wait(&part->done);
						g_diagMainCrumb = 0;
						wl_dump(wlSeat);   // dump the per-round link log to a timestamped SD file
						gs_dump(wlSeat);   // + the game-state log (HOST/JOIN-tagged; captures the link-error reason)
						diag_wd_close();   // D1/D3: close the wd/hang logs + the CSV (flushes its buffered tail)
						gbacore_net_detach(part->core); net_link_stop(); net_session_close();
						g_netWorker = NULL; other->paused = false; wlOn = false;
						snprintf(status, sizeof status, "Wireless: off");
					} else {
						EmuInstance* fg = (focused == 0) ? &emuA : &emuB;
						char gcode[5] = { 0 };
						uint8_t grev = 0;                    // D6: ROM header 0xBC (revision) of the participating game
						if (fg->core) { gbacore_game_code(fg->core, gcode); grev = gbacore_game_rev(fg->core); }
						int lr = wireless_lobby_run(top, bot, txtBuf, gcode, grev);   // 0 closed, 1 host, 2 joiner
						if ((lr == 1 || lr == 2) && fg->core && !linkOn && !netOn) {
							int seat = (lr == 1) ? 0 : 1;          // host = seat 0 (parent/master), joiner = seat 1 (child)
							// The FOCUSED game (fg) is the trade participant — matches the code the lobby advertised
							// (gcode above) and lets the user trade with EITHER loaded game (incl. FireRed) without
							// reordering ROMs. The OTHER game pauses, freeing its core for the radio.
							EmuInstance* part  = fg;
							EmuInstance* other = (fg == &emuA) ? &emuB : &emuA;
							int rxCore = (part == &emuA) ? 2 : 0;  // RX on the freed (non-participant) core
							if (workersRunning) { LightEvent_Wait(&emuA.done); LightEvent_Wait(&emuB.done); workersRunning = false; }
							other->paused = true;                  // FREE the other core for the participant + the radio (NOT netLinked)
							if (net_link_start(seat, rxCore)) {    // loopback=false; resolves the peer; spins the RX thread
								gbacore_net_attach(part->core, seat, 1);   // ONE participating core; peers=1; needMask=0x3
								part->netLinked = true;            // the other stays FALSE (parked, core freed)
								g_netWorker = part;                // the apt hook can now stop this worker on HOME/suspend
								wlOn = true; wlSeat = seat;        // remember our seat for the SD log filename
								diag_wd_session_reset();           // D1 (SPEC S.3/D1.5): kill-switch check + fresh
								                                   // episode + fresh (lazy) wd-file timestamp
#if DIAG_D3_ENABLE
								// D3 (SPEC D3.5): open the per-run CSV ONCE, here — never on the frame
								// path. diag_wd_session_reset above closed any stale file + re-read the
								// kill switch; s_csvFile == NULL is the "D3 dormant" state everywhere else.
								if (!s_diagOff) {
									mkdir("sdmc:/cias", 0777);           // ensure the netlog dirs exist
									mkdir("sdmc:/cias/netlogs", 0777);   // (matches gbacore_net_log_dump)
									char cp[96]; diag_log_path(cp, sizeof cp, "csv", "csv", seat);
									s_csvFile = fopen(cp, "w");
									if (s_csvFile) {
										setvbuf(s_csvFile, NULL, _IOFBF, 8192);   // one-time buffer; a row = a memcpy
										char hdr[768];
										int hl = diag_csv_header(hdr, sizeof hdr, seat);
										if (hl > 0) fwrite(hdr, 1, (size_t)hl < sizeof hdr ? (size_t)hl : sizeof hdr - 1, s_csvFile);
										fflush(s_csvFile);   // the header lands even if the run dies instantly
										s_csvRows = 0; s_csvTotal = 0;   // fresh flush cadence + row budget
									}
								}
#endif
								LightEvent_Signal(&part->go);      // kick ONLY the participant into the net free-run
								menuOpen = false;
								char pn[24]; rom_display_name(part == &emuA ? pathA : pathB, pn, sizeof pn);
								snprintf(status, sizeof status, "Wireless: ON (%s, %s)", seat == 0 ? "host" : "join", pn);
							} else {
								other->paused = false;             // couldn't arm (no unicast peer / RX thread) -> undo
								net_session_close();
								snprintf(status, sizeof status, "WL link failed: no peer");
							}
						} else if (lr == 1 || lr == 2) {
							net_session_close();                   // lobby left it up but we can't link here -> drop it
							snprintf(status, sizeof status, "Stop cable/net first");
						}
					}
				}
				else if (menuSel == MENU_NETLINK_IDX) {         // M2.5 net link (loopback) — beta
					if (!emuA.core || !emuB.core) {
						snprintf(status, sizeof status, "Net link needs 2 games");
					} else if (linkOn || wlOn) {
						snprintf(status, sizeof status, "Stop cable/WL link 1st");
					} else if (!netOn) {
						if (workersRunning) { LightEvent_Wait(&emuA.done); LightEvent_Wait(&emuB.done); workersRunning = false; }
						net_link_set_loopback(true);
						net_transfer_reset();
						gbacore_net_attach(emuA.core, 0, 1);    // seat 0 = parent
						gbacore_net_attach(emuB.core, 1, 1);    // seat 1 = child
						emuA.netLinked = emuB.netLinked = true;
						netOn = true;
						LightEvent_Signal(&emuA.go);            // kick both workers into the net free-run
						LightEvent_Signal(&emuB.go);
						snprintf(status, sizeof status, "Net link: ON (loopback)");
					} else {
						emuA.netLinked = emuB.netLinked = false;  // net loops exit (collect times out <=50ms)
						LightEvent_Wait(&emuA.done);
						LightEvent_Wait(&emuB.done);
						gbacore_net_detach(emuA.core);
						gbacore_net_detach(emuB.core);
						netOn = false;
						snprintf(status, sizeof status, "Net link: off");
					}
				}
				else if (menuSel == 17) { result = SESSION_CHANGE; break; }
				else                    { result = SESSION_QUIT;   break; }
			}
		}

		// ---- text for this frame (single buffer; cleared once) ----
		C2D_TextBufClear(txtBuf);
		C2D_Text tHint, tStatus, tToast;

		// HUD text: per-screen game label + a top-screen stat line (FPS / clock / battery).
		C2D_Text tHudTop, tHudBot, tHudStat;
		const char* topName = swapped ? nameB : nameA;
		const char* botName = swapped ? nameA : nameB;
		if (single) botName = "CONTROLLER";
		char hudStat[72];
		if (hudMode) {
			time_t tt = time(NULL);
			struct tm* lt = localtime(&tt);
			if (netOn || wlOn) {   // net-link diag: PEAK sent/received words + start/ok/timeout/stall
				int ns, ni, no, nt, ne, nf; unsigned nr, pw, cw, rxp, rxc;
				unsigned psp, psc, prp, prc; int stallO;
				unsigned vblMax, capK; int paceBlk;
				gbacore_net_diag(&ns, &ni, &no, &nt, &nr, &pw, &cw, &ne, &nf, &rxp, &rxc);
				gbacore_net_peak(&psp, &psc, &prp, &prc, &stallO);
				gbacore_net_pace(&vblMax, &paceBlk, &capK);
				(void)ni; (void)ne; (void)nr; (void)pw; (void)cw; (void)rxp; (void)rxc; (void)psc; (void)prc; (void)wlDrops; (void)capK;
				// P = peak word WE SENT for seat 0 (watch B9A0->8FFF then BBBB/8888 on the HOST);
				// R = peak word WE RECEIVED for seat 0 (watch the same on the JOINER); s/o = starts/oks
				// (s~=o now means no round churn); to = timeouts; ST = o-value where a collect first MISSED
				// (-1 = never). V = peak emulated VBlanks between serial IRQs (the JOINER pacing/LAG measure;
				// must stay < ~10 or the SLAVE watchdog trips); b = times the joiner blocked at the barrier.
				if (wlOn)
					snprintf(hudStat, sizeof hudStat, "N P%04X R%04X o%d to%d V%u b%d rtt%d X%c",
					         psp, prp, no, nt, vblMax, paceBlk, wlRtt, 'A' + gbacore_net_get_exp());
				else
					snprintf(hudStat, sizeof hudStat, "N P%04X R%04X s%d o%d to%d ST%d V%u b%d",
					         psp, prp, ns, no, nt, stallO, vblMax, paceBlk);
			} else
			snprintf(hudStat, sizeof hudStat, "%s %dfps %dms %02d:%02d %d/5 f%d d%.1f c%d,%d",
			         linkOn ? "LINK" : AUDIO_NAMES[audioMode], fps, showMs, lt ? lt->tm_hour : 0, lt ? lt->tm_min : 0, batLvl, depth3d.nfg, depth3d.maxd, depth3d.camX, depth3d.camY);
			C2D_TextParse(&tHudTop,  txtBuf, topName);  C2D_TextOptimize(&tHudTop);
			C2D_TextParse(&tHudBot,  txtBuf, botName);  C2D_TextOptimize(&tHudBot);
			C2D_TextParse(&tHudStat, txtBuf, hudStat);  C2D_TextOptimize(&tHudStat);
		}
		if (menuOpen) {
			// Footer: last action result, or a controls cheat-sheet when idle.
			C2D_TextParse(&tStatus, txtBuf,
			              status[0] ? status : "L/R tab  A select  B resume  (or tap)");
			C2D_TextOptimize(&tStatus);
		} else {
			char hintBuf[96];
			if (single)
				snprintf(hintBuf, sizeof hintBuf, "3D on top · %s · START+SELECT = menu", TOUCH_NAMES[tmEff]);
			else
				snprintf(hintBuf, sizeof hintBuf, "%s", touchMode != TOUCH_OFF ? "START+SELECT → pause menu"
				                                      : "tap screen → pause menu");
			C2D_TextParse(&tHint, txtBuf, hintBuf);

			C2D_TextOptimize(&tHint);
			if (toastTimer > 0) { C2D_TextParse(&tToast, txtBuf, toast); C2D_TextOptimize(&tToast); }
		}

		// Map games to screens. Scale/filter stay tied to the SCREEN; focus/input to the GAME.
		EmuInstance* topG = swapped ? &emuB : &emuA;
		EmuInstance* botG = swapped ? &emuA : &emuB;
		int focScreen = swapped ? (focused ^ 1) : focused;   // screen showing the focused game

		// Active-game cue: dim the UNFOCUSED game toward black; focused stays full.
		C2D_ImageTint dimTint;
		C2D_PlainImageTint(&dimTint, C2D_Color32(0, 0, 0, 0xFF), 0.5f);
		const C2D_ImageTint* topTint = (focScreen == 0) ? NULL : &dimTint;
		const C2D_ImageTint* botTint = (focScreen == 1) ? NULL : &dimTint;

		C3D_FrameBegin(C3D_FRAME_SYNCDRAW);

		// top screen (sharp-bilinear two-pass when applicable). render_game leaves `top` bound.
		float slider3d = osGet3DSliderState();
		bool s3dOn = slider3d > 0.03f && !menuOpen && s3dEnabled;   // 3D engaged AND not in the menu; else plain 2D (gates every 3D effect)
		bool pop3d = s3dOn && depth3d.overworld && topG->core;

		// ---- phase 14 slice T3: the HD-2D tilt GATE + TWEEN (SPEC-integration §1, §2, §3, §5) ----
		// The gate is now the real G1-G11 predicate, evaluated PER SCREEN by tilt_target_level()
		// in tilt.c — the ONE place any of those rules lives (I1.11), enumerated exhaustively by
		// test_tilt TEST 1-3. There is deliberately NO gating logic in this block: it fills a flat
		// struct of observable state and asks. Per screen and not "the focused game" (I1.8): the
		// data for both is already in hand from the same parked window, every other display
		// property here is already per screen (scaleMode[2]/smooth[2]/hudMode), and a shared gate's
		// failure mode is visible and wrong — game A opens a battle and game B's quietly-overworld
		// screen snaps flat for no reason the player can see.
		// Slice T4 lands the LEVEL: g_prefs.tiltLevel, the ENHANCE-tab PK_SEG row, persisted in
		// Settings.tilt (§4). It ships at 0, so G1 fires and the whole path stays inert until the
		// user raises it — nothing about the shipped default frame changed with this slice.
		int tiltLvl[2];
		{
			TiltGateIn gi;
			// tiltOk folds in the no-shader / no-VRAM fallback (R3.5.3): with the shader or the
			// vertex arena missing, the SAVED level is presented to the gate as 0, so the tween
			// parks at exactly 0.0f, tilt_active() is false and the frame is the flat path —
			// PHASE.md invariant 1's "a no-shader / no-VRAM fallback returns to the flat path",
			// with no second check bolted on further down.
			gi.userLevel     = tiltOk ? g_prefs.tiltLevel : 0;
			gi.menuOpen      = menuOpen ? 1 : 0;                 // G3
			gi.wlOn          = wlOn ? 1 : 0;                     // G4 (linkOn deliberately absent)
			gi.netOn         = netOn ? 1 : 0;
			gi.isN3DS        = isN3DS ? 1 : 0;                   // G2 tier clamp (live only, I5.1)
			gi.fsOn          = fsOn ? 1 : 0;                     // G11
			gi.focScreen     = focScreen;
			// G9 — bottom only; invariant 4. FIX PASS (2026-08-04, review finding 5): read the LIVE
			// touchMode, not the frame-top snapshot tmEff (main.c:2057). The pause menu's preview
			// buttons (ACT_PREVIEW_PAD / ACT_PREVIEW_SMART) set touchMode AND clear menuOpen later
			// in the SAME frame, so on that one frame the stale tmEff still said OFF while menuOpen
			// already said false: G3 had let go and G9 was not yet armed, the target jumped back up
			// to the user level, and tilt_tween_step RETARGETED the bottom screen upward — turning
			// the ~100 ms of residual tween into a fresh 250 ms segment of up-to-5-degree tilt with
			// smart touch already live over it, mapped by the FLAT touch_to_gba inverse. The two
			// expressions are otherwise identical: tmEff only substitutes TOUCH_PAD for TOUCH_SMART
			// in single-game mode (main.c:2057), which can neither produce nor consume TOUCH_OFF.
			// TEST 19 pins the sequence.
			gi.touchActive   = (touchMode != TOUCH_OFF) ? 1 : 0;
			gi.stereoEngaged = pop3d ? 1 : 0;                    // G10 — top only; stereo wins
			for (int sc = 0; sc < 2; sc++) {                     // 0 = top, 1 = bottom (SCREEN)
				gi.screen   = sc;
				gi.ok       = tiltSnap[sc].ok;                   // G5 — unmapped game => never tilt
				gi.ctx      = tiltSnap[sc].ctx;                  // G6 — GCTX_OVERWORLD only
				gi.sb1Valid = tiltSnap[sc].sb1Valid;             // G7 — CtlIn.fieldValid's predicate
				gi.px       = tiltSnap[sc].px;
				gi.textDlg  = tiltSnap[sc].textDlg;              // G8 — a script is talking
				tiltLvl[sc] = tilt_target_level(&gi);
			}
			// Stepped EVERY frame for BOTH screens, gate open or shut — which is what makes every
			// gate change a 250 ms tween instead of a snap (I2.5, PHASE.md invariant 5). This sits
			// below the menu split but nowMs (main.c:2014) is read ABOVE it, so the tween keeps
			// running with the pause menu open and a menu-open tilt tweens DOWN rather than
			// vanishing. dtMs is clamped to 100 ms inside tilt_tween_step (I2.3) so an apt_hook
			// suspend/resume steps once instead of teleporting the angle.
			float dtTilt = (float)(nowMs - tiltLastMs); tiltLastMs = nowMs;
			for (int sc = 0; sc < 2; sc++)
				tilt_tween_step(&tiltTw[sc], tiltLvl[sc], tilt_angle_deg_for_level(tiltLvl[sc]), dtTilt);
		}
		// ---- phase 15 slice M1: co-op presence SOLVE (SPEC-data D6.2) ----
		// One call per GAME, in the render phase next to the tilt tween, exactly as D6.1 splits the
		// work: main.c does the reads, the publishes and the solves — and contains NO presence
		// gating logic of its own, just as it contains no tilt gating logic. presence_solve owns the
		// P-G1..P-G9 ladder, the anchor math, the D4.7 hold and the D5.5 filter, and it mutates only
		// its PresenceState. Running it every frame (gate open or shut) is what keeps `reason`
		// meaningful on the frames that do NOT draw, which is the entire point of slice M1: the HUD
		// readout below must be able to say WHY there is no peer without a rebuild.
		for (int gi = 0; gi < 2; gi++) {
			PresenceIn pin;
			pin.enabled  = presenceOn ? 1 : 0;                        // P-G1
			pin.menuOpen = menuOpen ? 1 : 0;                          // P-G2
			pin.linkAny  = (linkOn || netOn || wlOn) ? 1 : 0;         // P-G3 (D4.8)
			pin.slot     = 0;                                         // PRES_MAX_PEERS == 1
			pin.self     = presSelf[gi];
			presDraw[gi] = presence_solve(&presSt[gi], &pin, &presOut[gi]);
			// ---- slice M2: the walk phase (SPEC-avatar A2.6.3/A2.6.4) ----
			// Driven by the PEER's own accumulated world travel, never by the foot anchor: the
			// anchor is a DIFFERENCE, so a host-driven accumulator would animate a standing peer
			// every time the player walks (and freeze them when both walk in step). A frame that
			// does not draw RESETS the cycle, so a peer who leaves the screen and comes back does
			// not resume mid-stride from a stale accumulator; on a D4.7 hold frame the anchor is
			// frozen, travel stays 0, and the peer settles into STAND after PRES_IDLE_FRAMES —
			// which is exactly what a frozen peer should look like.
			if (presDraw[gi]) presPose[gi] = presence_walk_step(&presWalk[gi], &presSt[gi].rec[0]);
			else { presence_walk_reset(&presWalk[gi]); presPose[gi] = PRES_POSE_STAND; }

			// ---- slice M3: identity + the meeting predicate (SPEC-avatar A4/A5) ----
			// PHASE.md invariant 6 read literally: with the pref off this whole block is skipped —
			// no decode, no memcmp, no predicate — and the card is forced shut, so a presence-off
			// frame does exactly what it did before phase 15. (presence_solve above deliberately
			// DOES run either way, because slice M1's readout must be able to say WHY nothing drew.)
			if (!presenceOn) {
				presence_card_reset(&presCard[gi]);
				memset(&presMeet[gi], 0, sizeof presMeet[gi]);
			} else {
				// A4.5.2 — the decoded name is CACHED and re-decoded ONLY when the raw 8 bytes
				// change. A C2D text parse per surface per frame is the thing A7.2 H9 says to
				// suspect first if the frame budget moves, so the decode never runs in a draw.
				const PeerPresence* pr = &presSt[gi].rec[0];
				if (!presNameOk[gi] || memcmp(presNameRaw[gi], pr->name, 8) != 0) {
					memcpy(presNameRaw[gi], pr->name, 8);
					presNameOk[gi] = true;
					if (pr->flags & PRES_F_IDENT)
						gbatext_decode(pr->name, 8, presNameTxt[gi], (int)sizeof presNameTxt[gi]);
					else
						presNameTxt[gi][0] = '\0';    // A0.4: no identity => nameplate suppressed,
					                                  //   the avatar still draws
				}
				// A5.1/A5.2 — adjacency + facing, from the two records already in hand. The RAW
				// nibbles come along because PeerPresence.facing is always 1..4 (presence_fill_core
				// folds an unavailable facing to SOUTH), so only the raw value can tell "facing
				// south" from "we have no idea" — which is what A5.2.1's degraded mode turns on.
				// Index: presSelf[gi] is game gi's own record, presSt[gi].rec[0] is the OTHER
				// game's, hence presFaceRaw[gi] and presFaceRaw[gi ^ 1].
				presence_meet(&presSelf[gi], (int)presFaceRaw[gi],
				              pr,            (int)presFaceRaw[gi ^ 1], &presMeet[gi]);
				// A5.3 (ABSOLUTE) — presence does not consume, swallow, remap or synthesise a single
				// key. This OBSERVES kDown, which was already assembled and is already on its way to
				// the game this frame. There is no free button left on the 3DS pad anyway (X = screen
				// swap, Y = focus, ZL = filter, ZR = scale, everything else is forwarded), and
				// intercepting A is the input-side version of the write this whole phase refuses to
				// make. The trigger is A on the FOCUSED game only, which is also what makes the card
				// unambiguous when both games have a peer beside them.
				PresCardIn ci;
				ci.enabled  = 1;              // this branch IS presenceOn; the field stays in the
				                              //   input struct because the pure-C FSM must be able
				                              //   to close on it (TEST 35 drives exactly that case)
				ci.drawn    = presDraw[gi];
				ci.meet     = presMeet[gi].meet;
				ci.menuOpen = menuOpen ? 1 : 0;
				// A5.4.4 — the prompt and the card go away with the rest of that screen's chrome.
				// The game a peer belongs to is on the screen `swapped` puts it on, and hudMode's
				// bit 0 is the top screen, bit 1 the bottom.
				ci.hudOn    = (hudMode & ((gi == (swapped ? 1 : 0)) ? 1 : 2)) ? 1 : 0;
				ci.aEdge    = (gi == focused && (kDown & KEY_A)) ? 1 : 0;
				ci.bEdge    = (gi == focused && (kDown & KEY_B)) ? 1 : 0;
				if (presence_card_step(&presCard[gi], &ci))
					presence_card_fill(pr, &presCardTx[gi]);   // A4.5.3/A4.4.5: the SAME record the
					                                           // avatar uses — no extra RAM read
			}
		}
		const int presTopGame = swapped ? 1 : 0;   // the game whose screen the top HUD describes
		const int presBotGame = swapped ? 0 : 1;   // ...and the bottom's (A2.7.1: the roles invert)
		// Slice M3 — which pills are up, resolved ONCE per SCREEN through the single policy function
		// (A4.4.2 / open question O-A4). main.c never re-decides this: when the always-on vs
		// on-approach taste call is made on hardware it is one line in presence_surfaces and nothing
		// here moves. `nearTiles` is the Chebyshev tile distance, which the shipped policy ignores
		// and the O-A4 alternative would use.
		PresChrome presCh[2];
		{
			const int gm[2]  = { presTopGame, presBotGame };
			const int hud[2] = { (hudMode & 1) ? 1 : 0, (hudMode & 2) ? 1 : 0 };
			for (int sc = 0; sc < 2; sc++) {
				int g  = gm[sc];
				int ax = presOut[g].dTileX < 0 ? -presOut[g].dTileX : presOut[g].dTileX;
				int ay = presOut[g].dTileY < 0 ? -presOut[g].dTileY : presOut[g].dTileY;
				presCh[sc].buf  = txtBuf;
				presCh[sc].name = presNameTxt[g];
				presCh[sc].surf = presence_surfaces(presDraw[g], hud[sc], presNameTxt[g][0] != '\0',
				                                    presMeet[g].meet, presCard[g].open,
				                                    ax > ay ? ax : ay);
				// FIX PASS (review finding 9): measure ONCE per screen per frame, and only for a
				// surface that is actually up — a closed gate still costs zero text work (A0.1.2).
				// txtBuf was cleared above (main.c's C2D_TextBufClear), so these parses land in this
				// frame's buffer exactly like every other chrome measurement.
				presCh[sc].plateW  = (presCh[sc].surf & PRES_SURF_PLATE)
				                   ? ui_text_w(txtBuf, presNameTxt[g], 0.32f) + 12.0f : 0.0f;
				presCh[sc].promptW = (presCh[sc].surf & PRES_SURF_PROMPT)
				                   ? ui_text_w(txtBuf, PRES_PROMPT_TEXT, 0.32f) + 12.0f : 0.0f;
			}
		}

		// THE invariant-1 check, once per screen: level > 0 OR angle > 0 (tween included).
		bool tiltTop = tilt_active(&tiltTw[0]);
		bool tiltBot = tilt_active(&tiltTw[1]);
		TiltView tiltVw, tiltVwB;                 // built only when engaged: a tilt-off frame pays nothing
		if (tiltTop) tilt_view_init(&tiltVw,  tiltTw[0].ang * TILT_DEG2RAD,
		                            (float)GBA_W, (float)GBA_H, TILT_COVER_MIX);
		if (tiltBot) tilt_view_init(&tiltVwB, tiltTw[1].ang * TILT_DEG2RAD,
		                            (float)GBA_W, (float)GBA_H, TILT_COVER_MIX);
		// R4.0.1: a pass that is not yet tilt-aware is EXCLUDED while the tilt is active — never
		// drawn flat over a tilted base (a flat overlay on a tilted ground is visibly mis-registered
		// and reads as a bug). Slice T2 ships step 5 (the base mesh) only, so all five composites
		// are excluded. They cannot co-occur with a SETTLED tilt anyway (G10 above, plus every one
		// of them requires s3dOn) — but they DO overlap during the 250 ms tween-out after the 3D
		// slider comes up, which is exactly the frame R4.0.1 is about.
		bool popPass = pop3d && !tiltTop;
		bool uipop = s3dOn && depth3d.nui > 0 && topG->core && !tiltTop;   // BG0 panels pop in ANY context
		// Text-aware DoF: kill a band's blur the moment text/UI shows under it (BG0 scan + RAM
		// signals), ease back in afterwards (fast-out ~3 frames, slow-in ~12 -> no flicker).
		dofLvlTop += (depth3d.overworld && !depth3d.textTop) ? 0.08f : -0.34f;
		dofLvlBot += (depth3d.overworld && !depth3d.textBot) ? 0.08f : -0.34f;
		if (dofLvlTop > 1.0f) dofLvlTop = 1.0f; else if (dofLvlTop < 0.0f) dofLvlTop = 0.0f;
		if (dofLvlBot > 1.0f) dofLvlBot = 1.0f; else if (dofLvlBot < 0.0f) dofLvlBot = 0.0f;
		bloomLvl += (depth3d.overworld && !depth3d.textTop && !depth3d.textBot) ? 0.08f : -0.34f;
		if (bloomLvl > 1.0f) bloomLvl = 1.0f; else if (bloomLvl < 0.0f) bloomLvl = 0.0f;
		bool dofPass = s3dOn && !vividOn && dofOn && dofTgtA && depth3d.overworld && topG->core && (dofLvlTop > 0.01f || dofLvlBot > 0.01f) && !tiltTop;
		bool bloomPass = s3dOn && !vividOn && bloomOn && bloomTgt && dofTgtA && depth3d.overworld && topG->core
		              && focScreen == 0 && bloomLvl > 0.01f && !tiltTop;   // focused top only (study budget rule)
		bool litPass = s3dOn && !vividOn && lightOn && depth3d.overworld && topG->core && !tiltTop;   // time-of-day grade
		LightEnv lenv; if (litPass) { time_t _tt = time(NULL); struct tm* _lt = localtime(&_tt);
			lenv = light_for_hour(_lt ? _lt->tm_hour + _lt->tm_min / 60.0f : 12.0f); }
		if (dofPass || bloomPass) dof_prepare(&topG->tex, dofTgtA);   // shared half-res copy (DoF + bloom source)
		if (bloomPass) bloom_bright(&dofTexA, bloomTgt);              // bright-pass glow map, shared by both eyes
		bool sharpTop = !smooth[0] && scaleMode[0] != SCALE_1X && preTgt;     // matches render_game's two-pass choice
		u32 topMod = (focScreen == 0) ? 0xFFFFFFFFu : C2D_Color32(0x80, 0x80, 0x80, 0xFF);   // grid analog of dimTint
		// (sharpTop deliberately does NOT take R3.1.2's tilt clause: its only consumers —
		//  warp_grid_eye / ui_pop_eye — are excluded while the tilt is up, and render_game makes
		//  the tilt's own source choice internally.)
		// Phase 14: BOTH eyes take the same projection. Stereo is off whenever the tilt is up
		// (G10), so the right eye must be the identical tilted image or the two halves disagree.
		// The right eye carries no dim tint, mirroring today's flat + warp calls (main.c:2662/2664).
		// FIX PASS (review finding 2): identical TiltView + identical mode + identical 400x240 rect
		// => identical geometry, so the right eye CLONES the left eye's slab instead of re-running
		// the projection. When the tints also match (focused top screen: both 0xFFFFFFFF) the two
		// slabs would be byte-identical, so the right eye simply draws slab 0 again — no vertex
		// write and no GSPGPU_FlushDataCache at all for the third game image.
		int trSlab = (topMod == 0xFFFFFFFFu) ? 0 : 1;
		TiltDraw tiltTL = { &tiltVw, topMod,      0,      -1 };
		TiltDraw tiltTR = { &tiltVw, 0xFFFFFFFFu, trSlab,  0 };
		render_game(topG, top, preTgt, &preTex, 400.0f, 240.0f, scaleMode[0], smooth[0], topTint, clrBg,
		            tiltTop ? &tiltTL : NULL);
		if (popPass) {   // M2: continuous grid warp (stretch, no tile tears); quad-warp fallback if no shader
			if (warpOk) warp_grid_eye(top, topG, &depth3d, scaleMode[0], +slider3d, sharpTop, &preTex, topMod, 0);
			else        warp_scenery_eye(top, topG, &depth3d, scaleMode[0], +slider3d);
			pop_eye(top, topG, &depth3d, scaleMode[0], +slider3d);   // LEFT eye shifts RIGHT -> pops OUT (ramp+char per sprite)
		}
		if (dofPass) dof_bands(top, &dofTexA, scaleMode[0], dofLvlTop, dofLvlBot, warpOk ? +slider3d : 0.0f);   // bands OVER the pops
		if (bloomPass) bloom_add(top, &bloomTex, scaleMode[0], bloomLvl, 0);   // additive glow, over the blur
		if (uipop) ui_pop_eye(top, topG, &depth3d, scaleMode[0], +UIPOP3D_PX * slider3d, sharpTop, &preTex);   // UI panels pop hardest
		// Phase 15 slice M2 (SPEC-avatar A2.1): the co-op avatar goes AFTER the pop/DoF/bloom/UI
		// passes — each of them re-draws sub-rects of the GAME texture over the frame, so anything
		// drawn earlier is overpainted by background pixels — and BEFORE light_pass, which is a
		// MULTIPLY grade over the whole frame box: the avatar is world content and must take the
		// time-of-day grade with the map it stands on, or a bright peer floats over a dusk route.
		// Under tilt all four of those passes are suppressed (&& !tiltTop below), so this one
		// insertion point lands immediately after render_game there — one site, both cases (A2.1.1).
		presence_draw_screen(top, &presOut[presTopGame], &presSt[presTopGame].rec[0], &presPose[presTopGame],
		                     1, scaleMode[0], 400.0f, 240.0f, tiltTop ? &tiltVw : NULL, topTint, &presCh[0]);
		if (litPass) light_pass(top, &depth3d, scaleMode[0], &lenv);   // lit LAST -> tints the UI panels too (sign is not a bright patch)
		if (!menuOpen) {
			// Slice M3 / A5.4.2: the Card, over the game image and UNDER the HUD bar drawn just
			// below, as a !menuOpen overlay. AFTER light_pass on purpose — unlike the nameplate it
			// is a UI panel, not world content, so it must not take the time-of-day grade. The
			// hudMode suppression (A5.4.4) is inside the FSM, so `open` is already false there and
			// this needs no second condition that could disagree with it.
			if (presCard[presTopGame].open) presence_draw_card(txtBuf, &presCardTx[presTopGame], 400.0f);
			if (hudMode & 1) {
				C2D_DrawRectSolid(0.0f, 0.0f, 0.0f, 400.0f, 14.0f, THEME_HUD_BAR);
				if (focScreen == 0) C2D_DrawRectSolid(0.0f, 14.0f, 0.0f, 400.0f, 2.0f, clrHi);
				ui_dot(6.0f, 4.5f, swapped ? THEME_GAME_B : THEME_GAME_A);
				C2D_DrawText(&tHudTop, C2D_WithColor, 15.0f, 1.0f, 0.0f, 0.4f, 0.4f, THEME_ON_DARK);
				if (focScreen == 0) {
					float nw; float nh; C2D_TextGetDimensions(&tHudTop, 0.4f, 0.4f, &nw, &nh);
					ui_chip(txtBuf, "●FOCUS", 21.0f + nw, 0.5f, g_ui.acc);
				}
				if (netOn || wlOn) {   // net-diag stat line (dev): keep the dense readout
					float sw, sh; C2D_TextGetDimensions(&tHudStat, 0.4f, 0.4f, &sw, &sh);
					C2D_DrawText(&tHudStat, C2D_WithColor, 396.0f - sw, 1.0f, 0.0f, 0.4f, 0.4f, THEME_ON_DARK);
				} else {               // 1:1: [3D] 59fps 14:32 [battery]
					float rx = 394.0f;
					ui_border(rx - 13.0f, 3.5f, 12.0f, 7.5f, THEME_ON_DARK_DIM, 1.0f);
					C2D_DrawRectSolid(rx - 0.5f, 5.5f, 0.0f, 1.5f, 3.5f, THEME_ON_DARK_DIM);
					{ float bl = 8.0f * (batLvl > 5 ? 5 : batLvl) / 5.0f;
					  if (bl > 0.5f) C2D_DrawRectSolid(rx - 11.0f, 5.5f, 0.0f, bl, 3.5f, THEME_GAME_A); }
					rx -= 19.0f;
					{ time_t tt2 = time(NULL); struct tm* lt2 = localtime(&tt2);
					  char clk[8]; snprintf(clk, sizeof clk, "%02d:%02d", lt2 ? lt2->tm_hour : 0, lt2 ? lt2->tm_min : 0);
					  ui_text_r(txtBuf, clk, rx, 1.5f, 0.38f, THEME_ON_DARK);
					  rx -= ui_text_w(txtBuf, clk, 0.38f) + 7.0f; }
					{ char fs2[12]; snprintf(fs2, sizeof fs2, "%dfps", fps);
					  ui_text_r(txtBuf, fs2, rx, 1.5f, 0.38f, focScreen == 0 ? g_ui.acc : THEME_ON_DARK_DIM);
					  rx -= ui_text_w(txtBuf, fs2, 0.38f) + 8.0f; }
					if (s3dEnabled) { float cw = ui_text_w(txtBuf, "3D", 0.32f) + 12.0f;
					                  ui_chip(txtBuf, "3D", rx - cw, 0.5f, THEME_GAME_B);
					                  rx -= cw + 6.0f; }   // advance so the tilt chip lands to its LEFT
					// Phase 14 / I6.1: the tilt chip exists so a hardware PHOTO is interpretable.
					// Unlike the 3D chip (which shows only "enabled"), the COLOUR carries the gate:
					// accent = engaged on the top screen this frame, dim = the setting is on but the
					// gate is shut (menu / battle / dialog / touch / link / Old-3DS tier / stereo /
					// frameskip). A photo of a tilted screen with a DIM chip is self-contradictory
					// and localises a gate bug immediately. Top screen only (I6.3: the 320-px bottom
					// bar carries clock+fps and the pause pill already says "is tilt on"), and it
					// lives in the else branch of the netOn||wlOn split so it never competes with the
					// net-diag readout — under a link tilt is forced off anyway (G4/I5.5).
					// Costs nothing while tiltLevel == 0, which is the shipped default (I5.8).
					if (g_prefs.tiltLevel > 0) {
						// A table, not snprintf("TILT%d"): the loader already clamps tiltLevel to
						// 0..3 but the compiler cannot see that, so the format would warn about a
						// possible truncation into an 8-byte buffer — and this runs every HUD frame.
						static const char* const TILT_CHIP[TILT_LEVELS] = { "TILT", "TILT1", "TILT2", "TILT3" };
						int tl = g_prefs.tiltLevel < TILT_LEVELS ? g_prefs.tiltLevel : TILT_LEVELS - 1;
						const char* tc = TILT_CHIP[tl];
						float cw = ui_text_w(txtBuf, tc, 0.32f) + 12.0f;
						rx -= cw;
						ui_chip(txtBuf, tc, rx, 0.5f, tilt_active(&tiltTw[0]) ? g_ui.acc : THEME_ON_DARK_DIM);
					}
					// Phase 15 / A6.4: the CO-OP chip, immediately LEFT of the tilt chip, same
					// right-to-left `rx -= cw` flow. The COLOUR carries the gate exactly as the tilt
					// chip's does: accent = presence resolved to DRAW on this screen's game this
					// frame, dim = the setting is on but the ladder closed (different map, not in
					// the overworld, no profile, a live link, stale). That is what makes the M1
					// milestone photographable and it is the fastest triage for "why do I not see
					// my friend" — if the chip is dim it is a data/gate problem, not a draw problem.
					// Costs nothing while the pref is off, which is the shipped default.
					if (presenceOn) {
						const char* pc = "CO-OP";
						float cw = ui_text_w(txtBuf, pc, 0.32f) + 12.0f;
						rx -= cw + 6.0f;
						ui_chip(txtBuf, pc, rx, 0.5f, presDraw[presTopGame] ? g_ui.acc : THEME_ON_DARK_DIM);
					}
				}
			} else if (focScreen == 0) {
				C2D_DrawRectSolid(0.0f, 0.0f, 0.0f, 400.0f, 4.0f, clrHi);
			}
			// ---- phase 15 slice M1: THE READOUT (SPEC-avatar A4.4.3 — the slice's acceptance test)
			// "A HUD line naming the peer + their tile", which must work BEFORE any sprite exists.
			// It is deliberately a DEBUG line, not final chrome: this slice's whole job is to prove
			// the data, so it prints everything needed to diagnose a missing peer without a rebuild
			// — both maps side by side (a mismatch is the commonest cause and the gate reports it as
			// `map`), both tiles, the FOLDED facing next to the RAW nibble (D2.4.1's promotion
			// criterion: one run reading 1/2/3/4 as the player walks D/U/L/R promotes the
			// facingDirection offset to VERIFIED — printing only the folded value would let a
			// garbage nibble masquerade as a genuine "facing south"), the tile delta, the record's
			// AGE in frames and the heartbeat stall (the two liveness tiers, D3.5/D3.6.1), the
			// liveness tier itself, and the P-G reason code. The foot anchor is appended when the
			// gate is open — that number is what M2's calibration run (D5.4.1 / Open Q2) reads.
			// Bottom-left of the TOP screen, matching where the smart-touch diagnostics sit on the
			// bottom screen (y=224); it costs nothing while the pref is off and it respects the
			// user's per-screen HUD preference like every other chrome surface (A4.4.2).
			if (presenceOn && (hudMode & 1)) {
				const PresenceState* pst = &presSt[presTopGame];
				const PeerPresence*  pr  = &pst->rec[0];
				const PeerPresence*  se  = &presSelf[presTopGame];
				const PresenceOut*   po  = &presOut[presTopGame];
				char nm[9] = "?";
				if (pr->flags & PRES_F_IDENT) presence_name_ascii(pr->name, nm);
				char tidBuf[8] = "-----";
				if (pr->flags & PRES_F_IDENT) snprintf(tidBuf, sizeof tidBuf, "%05u", (unsigned)pr->tid);
				// FIX PASS (review finding 11): both counters are CAPPED for display. They are
				// unbounded and they keep counting — presence_begin_round runs every non-menu frame
				// while publishing is skipped whenever the cost guard closes (any link/net/wl
				// session, the very P-G3 case this line exists to explain), so at 60/s ten minutes
				// of link makes `a` and `h` five digits each. That is +8 characters on a line whose
				// own budget below is ~73 characters / ~340 px of a 400 px screen, and ui_text does
				// no measurement, no ellipsis and no clamp — so the overflow is silently clipped at
				// x = 400, taking the reason code, the HOLD flag and the foot anchor with it. Those
				// are exactly the fields a photograph needs. Both counters are only ever read as
				// "0 / small / saturated", and 999+ says saturated as well as 36000 does.
				#define PRES_RO_CAP(v) ((unsigned)(v) > 999u ? 999u : (unsigned)(v))
				unsigned age = pst->have[0] ? PRES_RO_CAP(pst->round - pst->seenRound[0]) : 0u;
				unsigned hbs = PRES_RO_CAP(presence_hb_stall(pst, 0));
				#undef PRES_RO_CAP
				char anch[20] = "";
				if (presDraw[presTopGame])
					snprintf(anch, sizeof anch, " @%d,%d", (int)(po->footX + 0.5f), (int)(po->footY + 0.5f));
				// Kept terse and drawn at 0.32 on purpose: the typical line
				//   "CO-OP me 3-12@14,9 | Nils#01234 3-12@17,8 f1/1 d+3,-1 a0 h0 L2 ok @120,88"
				// is ~73 characters, which is about 340 px at this size — it must not run off the
				// 400 px screen, because a readout whose right-hand half (the reason code and the
				// anchor) is clipped is exactly the half a photograph needs.
				char pl[128];
				snprintf(pl, sizeof pl,
				         "CO-OP me %d-%d@%d,%d | %s#%s %d-%d@%d,%d f%d/%d d%+d,%+d a%u h%u L%d %s%s%s",
				         (int)se->mapGroup, (int)se->mapNum, (int)se->px, (int)se->py,
				         nm, tidBuf,
				         (int)pr->mapGroup, (int)pr->mapNum, (int)pr->px, (int)pr->py,
				         (int)pr->facing, (int)presFaceRaw[presTopGame ^ 1],
				         po->dTileX, po->dTileY, age, hbs, po->liveness,
				         presence_off_reason(po->reason), po->held ? " HOLD" : "", anch);
				ui_text(txtBuf, pl, 6.0f, 226.0f, 0.32f,
				        presDraw[presTopGame] ? g_ui.acc : THEME_ON_DARK_DIM);
			}
			if (toastTimer > 0) C2D_DrawText(&tToast, C2D_WithColor, 8.0f, (hudMode & 1) ? 20.0f : 8.0f, 0.0f, 0.5f, 0.5f, clrHi);
		} else {
			draw_paused_summary(txtBuf, topName, botName, s3dEnabled, dofOn, bloomOn, lightOn, vividOn, touchMode, linkOn, netOn, wlOn, presenceOn);
		}

		// top RIGHT eye = the SAME (top) game -> single-game stereoscopic depth. Player pops forward
		// (positive disparity). (Per-eye dual-game retired; can return later as a menu toggle.)
		render_game(topG, topR, preTgt, &preTex, 400.0f, 240.0f, scaleMode[0], smooth[0], NULL, clrBg,
		            tiltTop ? &tiltTR : NULL);
		if (popPass) {
			if (warpOk) warp_grid_eye(topR, topG, &depth3d, scaleMode[0], -slider3d, sharpTop, &preTex, 0xFFFFFFFFu, 1);
			else        warp_scenery_eye(topR, topG, &depth3d, scaleMode[0], -slider3d);
			pop_eye(topR, topG, &depth3d, scaleMode[0], -slider3d);   // RIGHT eye shifts LEFT
		}
		if (dofPass) dof_bands(topR, &dofTexA, scaleMode[0], dofLvlTop, dofLvlBot, warpOk ? -slider3d : 0.0f);
		if (bloomPass) bloom_add(topR, &bloomTex, scaleMode[0], bloomLvl, 1);
		if (uipop) ui_pop_eye(topR, topG, &depth3d, scaleMode[0], -UIPOP3D_PX * slider3d, sharpTop, &preTex);
		// A2.8: the right eye draws the avatar at the IDENTICAL frame-space position (zero
		// disparity), and A2.6.2: with the identical NULL tint the left eye's `topTint` becomes at
		// this call site. Both eyes therefore agree pixel for pixel except for the game image's own
		// per-eye pops, which is what "the avatar sits on the screen plane" means.
		presence_draw_screen(topR, &presOut[presTopGame], &presSt[presTopGame].rec[0], &presPose[presTopGame],
		                     1, scaleMode[0], 400.0f, 240.0f, tiltTop ? &tiltVw : NULL, NULL, &presCh[0]);
		if (litPass) light_pass(topR, &depth3d, scaleMode[0], &lenv);
		if (menuOpen) draw_paused_summary(txtBuf, topName, botName, s3dEnabled, dofOn, bloomOn, lightOn, vividOn, touchMode, linkOn, netOn, wlOn, presenceOn);

		// bottom screen (+ menu overlay when open). render_game leaves `bot` bound.
		// Phase 14 slice T3: the bottom screen tilts too — but ONLY while every touch mode is off
		// (rule G9), which is SPEC-integration §3.2's decision (b) SUPPRESSION, chosen over (a)
		// "exclude the bottom screen entirely". (a) throws the effect away on half the device for a
		// conflict that exists in one mode; (b) costs it exactly where the conflict is. touch_to_gba
		// (main.c:621-631) is consulted only when tmEff == TOUCH_SMART, and G9 pins this screen's
		// target to 0 whenever tmEff != TOUCH_OFF, so the hardware-validated touch mapping stays
		// bit-identical to today BY CONSTRUCTION — PHASE.md invariant 4, machine-checked as an
		// implication in test_tilt TEST 3. Switching touch on mid-session is just another gate
		// change: the bottom tweens flat over 250 ms and stays there.
		// The dim tint has to ride the VERTEX COLOUR under tilt (R5.4), exactly as topMod does for
		// the top eyes — botTint is a citro2d tint and the tilt draw is not a citro2d draw.
		u32 botMod = (focScreen == 1) ? 0xFFFFFFFFu : C2D_Color32(0x80, 0x80, 0x80, 0xFF);
		TiltDraw tiltBL = { &tiltVwB, botMod, 2, -1 };   // slab 2 = the bottom image (R3.5.1), built
		render_game(botG, bot, preTgt, &preTex, 320.0f, 240.0f, scaleMode[1], smooth[1], botTint, clrBg,
		            tiltBot ? &tiltBL : NULL);
		// A2.7: the bottom screen runs none of the pop/DoF/bloom/light passes, so the avatar draws
		// immediately after render_game and before the HUD. A2.7.1: the PEER here is whichever game
		// is on the TOP screen — the roles invert — and `swapped` is resolved once, in exactly the
		// place the renderer already resolves it for topG/botG. A2.7.2: the avatar is NOT a touch
		// target; smart touch pathfinds on the real game UI and draws its own diagnostics after
		// this, so tapping "on" the peer does nothing, which is the honest ceiling, not a bug.
		presence_draw_screen(bot, &presOut[presBotGame], &presSt[presBotGame].rec[0],
		                     &presPose[presBotGame],
		                     1, scaleMode[1], 320.0f, 240.0f, tiltBot ? &tiltVwB : NULL, botTint,
		                     &presCh[1]);
		if (!menuOpen) {
			// Slice M3: the Card belongs to the FOCUSED game — the one whose A press opened it —
			// so it draws on whichever screen that game is on. A4.4.4 says "a top-screen overlay",
			// which is the single-peer reading of the same rule; with two games each having their
			// own peer, pinning it to the top would put a Card about the bottom game's neighbour on
			// the other screen. Only one can ever be open at a time (the A edge is fed to the
			// focused game alone), so the two call sites are mutually exclusive in practice.
			if (presCard[presBotGame].open) presence_draw_card(txtBuf, &presCardTx[presBotGame], 320.0f);
			if (hudMode & 2) {
				C2D_DrawRectSolid(0.0f, 0.0f, 0.0f, 320.0f, 14.0f, THEME_HUD_BAR);
				if (focScreen == 1) C2D_DrawRectSolid(0.0f, 14.0f, 0.0f, 320.0f, 2.0f, clrHi);
				ui_dot(6.0f, 4.5f, single ? THEME_GAME_A : (swapped ? THEME_GAME_A : THEME_GAME_B));
				C2D_DrawText(&tHudBot, C2D_WithColor, 15.0f, 1.0f, 0.0f, 0.4f, 0.4f, THEME_ON_DARK);
				{
					float nw, nh; C2D_TextGetDimensions(&tHudBot, 0.4f, 0.4f, &nw, &nh);
					float chx = 21.0f + nw;
					if (focScreen == 1) chx += ui_chip(txtBuf, "●FOCUS", chx, 0.5f, g_ui.acc) + 5.0f;
					if (linkOn || netOn || wlOn) ui_chip(txtBuf, "⚡LINK", chx, 0.5f, g_ui.acc);
				}
				if (netOn || wlOn) {
					float bsw, bsh; C2D_TextGetDimensions(&tHudStat, 0.4f, 0.4f, &bsw, &bsh);
					C2D_DrawText(&tHudStat, C2D_WithColor, 316.0f - bsw, 1.0f, 0.0f, 0.4f, 0.4f, THEME_ON_DARK);
				} else {
					float rx = 314.0f;
					{ time_t tt2 = time(NULL); struct tm* lt2 = localtime(&tt2);
					  char clk[8]; snprintf(clk, sizeof clk, "%02d:%02d", lt2 ? lt2->tm_hour : 0, lt2 ? lt2->tm_min : 0);
					  ui_text_r(txtBuf, clk, rx, 1.5f, 0.38f, THEME_ON_DARK);
					  rx -= ui_text_w(txtBuf, clk, 0.38f) + 7.0f; }
					{ char fs2[12]; snprintf(fs2, sizeof fs2, "%dfps", fps);
					  ui_text_r(txtBuf, fs2, rx, 1.5f, 0.38f, focScreen == 1 ? g_ui.acc : THEME_ON_DARK_DIM); }
				}
			} else if (focScreen == 1) {
				C2D_DrawRectSolid(0.0f, 0.0f, 0.0f, 320.0f, 4.0f, clrHi);
			}
			if (tmEff == TOUCH_PAD) {   // virtual gamepad overlay (SMART draws nothing -> real UI)
				touch_draw(tmEff, tk, &sm, txtBuf);
			}
			if (tmEff == TOUCH_SMART && sm.valid) {   // TEMP debug: confirm RAM reads on device
				static const char* const CTXN[] = { "none", "field", "b.act", "b.move", "b.tgt", "party", "fmenu", "bag", "b.oth" };
				char kb[8]; int ki = 0;   // decode the key touch is injecting this frame (on-device diagnostic)
				if (tk & (1 << GBAKEY_UP))    kb[ki++] = 'U';
				if (tk & (1 << GBAKEY_DOWN))  kb[ki++] = 'D';
				if (tk & (1 << GBAKEY_LEFT))  kb[ki++] = 'L';
				if (tk & (1 << GBAKEY_RIGHT)) kb[ki++] = 'R';
				if (tk & (1 << GBAKEY_A))     kb[ki++] = 'A';
				if (tk & (1 << GBAKEY_B))     kb[ki++] = 'B';
				if (!ki) kb[ki++] = '-';
				kb[ki] = '\0';
				char gs[64]; snprintf(gs, sizeof gs, "%s p=%d,%d key=%s", CTXN[sm.ctx], sm.px, sm.py, kb);
				C2D_Text tg; C2D_TextParse(&tg, txtBuf, gs); C2D_TextOptimize(&tg);
				C2D_DrawText(&tg, C2D_WithColor, 4.0f, 224.0f, 0.0f, 0.40f, 0.40f, C2D_Color32(0x42, 0xF5, 0xD0, 0xFF));
			}
		}
		if (menuOpen) {
			C2D_DrawRectSolid(0.0f, 0.0f, 0.0f, 320.0f, 240.0f, clrDim);
			// ---- Pause menu (v3): the pause-bot-<tab> PLATE (rail + highlight + labels baked) + the
			// interactive widget states at manifest coords, drawn on top. ----
			const PCtl* PTd = PTABS[menuTab]; int nPd = PTABN[menuTab];
			assets_draw_plate(PT_PLATE[menuTab]);
			int fsd = swapped ? (focused ^ 1) : focused;
			for (int i = 0; i < nPd; i++) {
				const PCtl* c = &PTd[i]; float x = c->x, y = c->y, w = c->w, h = c->h;
				bool sel = (i == menuRow);
				if (c->ov) assets_text_r(txtBuf, FNT_JBM_MED, c->ov, x - 6.0f, y + h / 2.0f - 4.0f, 8.0f, g_ui.dim);
				switch (c->kind) {
				case PK_TOG: {
					int on = 0;
					switch (c->act) { case ACT_SWAP: on=swapped; break; case ACT_FS: on=fsOn; break; case ACT_MUTE: on=muted; break;
						case ACT_3D: on=s3dEnabled; break; case ACT_DOF: on=dofOn; break; case ACT_BLOOM: on=bloomOn; break;
						case ACT_LIGHT: on=lightOn; break; case ACT_VIVID: on=vividOn; break; case ACT_LINK: on=linkOn; break;
						case ACT_NETLINK: on=netOn; break; case ACT_PRESENCE: on=presenceOn; break; }   // A6.2 site 2
					assets_toggle(on, x, y);
					if (sel) ui_border(x - 2.0f, y - 2.0f, w + 4.0f, h + 4.0f, g_ui.acc, 1.5f);
					break;
				}
				case PK_SEG: {
					static const char* const S_SCALE[3]={"1:1","Aspect-fit","Stretch"};
					static const char* const S_FILT[2]={"Sharp","Smooth"}; static const char* const S_HUD[4]={"off","top","bottom","both"};
					static const char* const S_AUD[3]={"Solo","Mixed","Split"}; static const char* const S_TCH[3]={"Off","Gamepad","Smart"};
					static const char* const S_EDG[3]={"Round","Soft","Sharp"};
					const char* const* o=S_SCALE; int cur=0;
					switch (c->act){case ACT_SCALE_TOP:cur=scaleMode[0];break;case ACT_SCALE_BOT:cur=scaleMode[1];break;
						case ACT_FILTER:o=S_FILT;cur=smooth[fsd];break;case ACT_HUD:o=S_HUD;cur=hudMode;break;
						case ACT_AUDIOMODE:o=S_AUD;cur=audioMode;break;case ACT_TOUCHMODE:o=S_TCH;cur=touchMode;break;
						case ACT_TILT:o=TILT_NAMES;cur=g_prefs.tiltLevel;break;   // phase 14 (I4.6 label table)
						default:o=S_EDG;cur=g_prefs.padEdge;break;}
					assets_seg(txtBuf, x, y, w, h, o, c->nseg, cur, g_ui.ink, g_ui.dim);
					if (sel) ui_border(x, y, w, h, g_ui.acc, 1.5f);
					break;
				}
				case PK_STEP: {
					int v = (c->act == ACT_VOLA) ? volA : volB;
					assets_fill9("fill-secondary-r8", x, y+2, 20, h-4, 7.0f);
					assets_text_c(txtBuf, FNT_SG_BOLD, "-", x+10, y+2, 12.0f, g_ui.text);
					assets_fill9("fill-secondary-r8", x+w-20, y+2, 20, h-4, 7.0f);
					assets_text_c(txtBuf, FNT_SG_BOLD, "+", x+w-10, y+2, 12.0f, g_ui.text);
					float bx=x+28, bw=w-56; ui_fill(bx, y+h/2-3, bw, 6, g_ui.line, 3.0f);
					if (v>0) ui_fill(bx, y+h/2-3, bw*v/256.0f, 6, g_ui.acc, 3.0f);
					assets_text_r(txtBuf, FNT_JBM_MED, (c->act==ACT_VOLA)?"A":"B", x+26, y-1, 8.0f, g_ui.dim);
					if (sel) ui_border(x, y, w, h, g_ui.acc, 1.5f);
					break;
				}
				case PK_BTN: {
					const char* spr="btn-secondary"; const char* lab=""; u32 col=g_ui.text;
					switch (c->act){
					case ACT_RESUME: spr="btn-primary"; lab="Resume"; col=g_ui.ink; break;
					case ACT_CHANGE: lab="Change games"; break;
					case ACT_QUIT: spr="btn-destructive"; lab="Quit"; col=THEME_QUIT_TEXT; break;
					case ACT_WIRELESS: spr="btn-primary"; lab=wlOn?"Wireless: ON":"Wireless lobby..."; col=g_ui.ink; break;
					case ACT_SAVEST: lab="Save state"; break; case ACT_LOADST: lab="Load state"; break;
					case ACT_LOADSAV: lab="Load .sav"; break;
					case ACT_PREVIEW_PAD: lab="Preview Gamepad"; break; case ACT_PREVIEW_SMART: lab="Preview Smart"; break; }
					assets_button(txtBuf, spr, x, y, w, h, lab, FNT_SG_BOLD, 13.0f, col, 0);
					if (sel) ui_border(x, y, w, h, g_ui.acc, 1.5f);
					break;
				}
				case PK_SWATCH: {
					const u32 pc[5]={PAD_COLOR_0,PAD_COLOR_1,PAD_COLOR_2,PAD_COLOR_3,PAD_COLOR_4};
					for (int cc=0; cc<5; cc++){ float sx=x+cc*30;
						if (cc==g_prefs.padColor) ui_border(sx-2, y, 26, h, g_ui.text, 1.5f);
						ui_fill(sx, y+3, 22, h-6, pc[cc], 4.0f); }
					if (sel) ui_border(x-3, y-2, 156, h+4, g_ui.acc, 1.5f);
					break;
				}
				}
			}
			{ float sw2, sh2; C2D_TextGetDimensions(&tStatus, 0.3f, 0.3f, &sw2, &sh2);
			  C2D_DrawText(&tStatus, C2D_WithColor, 314.0f - sw2, 231.0f, 0.0f, 0.3f, 0.3f, g_ui.dim); }
		} else {
			{ float hw, hh2; C2D_TextGetDimensions(&tHint, 0.34f, 0.34f, &hw, &hh2);
			  C2D_DrawText(&tHint, C2D_WithColor, (320.0f - hw) / 2.0f, 229.0f, 0.0f, 0.34f, 0.34f, dim_color(g_ui.acc, 0.85f)); }
		}

		{ float wms = (svcGetSystemTick() - wfStart) * 1000.0f / SYSCLOCK_ARM11; if (wms > worstMs) worstMs = wms; }
		C3D_FrameEnd(0);
		// Real-time cap to the 3DS LCD refresh (the rate audio is matched to): freed-up CPU must not run
		// the games + audio faster than 60fps. Only waits when UNDER budget, so heavy frames are untouched.
		while (svcGetSystemTick() - wfStart < FRAME_TICKS) svcSleepThread(100000);
		if (toastTimer > 0) toastTimer--;
	}

	// teardown this session's workers + cores; reset g_quit for the next session
	gs_dump(-1);   // flush this play session's game-state instrumentation log to SD (Quit / Change games; no link role)
	if (wlOn) {                               // Quit/Change-games straight out of a live WIRELESS link
		EmuInstance* part = g_netWorker ? g_netWorker : &emuA;   // the participant (focused game at link start)
		EmuInstance* other = (part == &emuA) ? &emuB : &emuA;
		part->netLinked = false;              // the worker leaves the net free-run once its collect returns
		net_link_stop();                      // join the RX thread + abort rounds (any blocked collect returns now)
		DIAG_CRUMB(g_diagMainCrumb, DIAG_SITE_MAIN_WL_TEARDOWN, 0);   // D1 site 4100 bracket
		LightEvent_Wait(&part->done);         // wait for the participant's worker to exit the net loop before detaching
		g_diagMainCrumb = 0;
		wl_dump(wlSeat);   // dump the per-round link log to a timestamped SD file
		gbacore_net_detach(part->core);
		net_session_close();
		g_netWorker = NULL;
		other->paused = false;
		wlOn = false;
	}
	ctl_drain_seat(0); ctl_drain_seat(1);     // D4: flush the tail status lines...
#if CTL_D5_ENABLE
	for (int i = 0; i < 2; i++) {             // D5.5: session end stops recording + any replay
		ctl_rec_stop(&s_ctlRec[i], "session end");
		ctl_rep_abort(&s_ctlRep[i], "session end");
		ctl_rr_drain_seat(i);
		ctl_rec_close(i);
	}
#endif
	ctl_close();                              // ...and close the control log (SPEC C.5)
	diag_wd_close();                          // D1/D3: session over — close the wd/hang logs + the CSV
	emuA.linked = emuB.linked = false;        // stop the free-run loop
	emuA.netLinked = emuB.netLinked = false;  // ...and the net free-run loop (symmetric teardown)
	g_quit = true;
	LightEvent_Signal(&emuA.waitEv);          // release any worker parked on a link wait
	LightEvent_Signal(&emuB.waitEv);
	LightEvent_Signal(&emuA.go);              // un-park emuA + a paused/parked emuB
	LightEvent_Signal(&emuB.go);
	if (emuA.thread) { threadJoin(emuA.thread, U64_MAX); threadFree(emuA.thread); }
	if (emuB.thread) { threadJoin(emuB.thread, U64_MAX); threadFree(emuB.thread); }
	audio_thread_stop();   // workers joined -> nothing pumps the rings; safe to stop audio + free them
	if (linkOn) { gbacore_link_detach(emuA.core); gbacore_link_detach(emuB.core); }
	if (netOn)  { gbacore_net_detach(emuA.core);  gbacore_net_detach(emuB.core);  }
	teardown_core(&emuA);
	teardown_core(&emuB);
	gbalink_destroy(link);
	if (preTgt) { C3D_RenderTargetDelete(preTgt); C3D_TexDelete(&preTex); }
	if (dofTgtA) { C3D_RenderTargetDelete(dofTgtA); C3D_TexDelete(&dofTexA); }
	if (bloomTgt) { C3D_RenderTargetDelete(bloomTgt); C3D_TexDelete(&bloomTex); }
	g_quit = false;
	gfxSet3D(false);   // back to flat for the ROM picker / splash between sessions
	return result;
}

// ---- Animated boot splash (GBA-nostalgic, dual-screen) ----------------------
static u32 dim_color(u32 c, float f) {   // scale RGB toward black, keep alpha
	u32 r = (c & 0xFF) * f, g = ((c >> 8) & 0xFF) * f, b = ((c >> 16) & 0xFF) * f;
	return (c & 0xFF000000) | (b << 16) | (g << 8) | r;
}

// A mini GBA "screen" (bezel + screen + ground band + player dot) for the splash.
static void splash_panel(float x, float y, float w, float h, u32 inner, u32 dot) {
	C2D_DrawRectSolid(x - 3, y - 3, 0.0f, w + 6, h + 6, THEME_BEZEL);
	C2D_DrawRectSolid(x, y, 0.0f, w, h, inner);
	C2D_DrawRectSolid(x, y + h * 0.62f, 0.0f, w, h * 0.38f, dim_color(inner, 0.7f));
	C2D_DrawRectSolid(x + w * 0.5f - 3, y + h * 0.5f - 3, 0.0f, 6.0f, 6.0f, dot);
}

static float ease_out(float p) { float q = 1.0f - p; return 1.0f - q * q * q; }

// Empirical New-3DS clock probe: a fixed busy loop runs ~3x faster at 804MHz while svcGetSystemTick
// stays at 268MHz, so comparing the loop's tick cost BEFORE vs AFTER osSetSpeedupEnable() is a
// self-calibrating test (no per-build constant) of whether the speedup actually engaged.
static u64 busy_ticks(void) {
	volatile u32 x = 0;
	u64 t0 = svcGetSystemTick();
	for (volatile u32 i = 0; i < 2000000u; i++) x += i;
	return svcGetSystemTick() - t0;
}

// Standalone settings, reachable from the boot menu WITHOUT a game (ZR on the game-select).
// Reuses the pause-menu plates + PCtl tables for the pre-game-relevant tabs (Display / Audio /
// Enhance / Touch); persists via settings_load/save. B or the on-screen Done exits.
static void run_settings(C3D_RenderTarget* top, C3D_RenderTarget* bot, C2D_TextBuf txtBuf) {
	int scaleMode[2] = { SCALE_FIT, SCALE_FIT }; bool smooth[2] = { false, false };
	bool swapped = false; int hudMode = 3, audioMode = AUD_SOLO, volA = 256, volB = 256, touchMode = TOUCH_OFF;
	bool fsOn = false, dofOn = true, bloomOn = true, lightOn = true, vividOn = false, muted = false, s3dEnabled = true;
	// Phase 15 / A6.2 sites 3+4: the LINK tab is NOT one of the four tabs this pre-game screen
	// exposes (TABS below is Display/Audio/Enhance/Touch), so the CO-OP row is unreachable here
	// today — but this local is NOT optional. Every SETSAVE() writes the WHOLE Settings struct, so
	// without loading and re-saving the pref, one visit to the pre-game settings screen would
	// silently wipe the user's co-op setting. The two switch cases are added for the day the tab
	// list grows; the load/save round trip is what actually matters right now.
	bool presenceOn = false;
	int focused = 0;
	settings_load(scaleMode, smooth, &swapped, &hudMode, &audioMode, &volA, &volB, &touchMode, &fsOn, &dofOn, &bloomOn, &lightOn, &vividOn, &presenceOn);
	#define SETSAVE() settings_save(scaleMode, smooth, swapped, hudMode, audioMode, volA, volB, touchMode, fsOn, dofOn, bloomOn, lightOn, vividOn, presenceOn)
	static const int TABS[4] = { 1, 2, 3, 5 };   // Display, Audio, Enhance, Touch (indices into PTABS/PT_PLATE)
	int ti = 0, row = 0;

	while (aptMainLoop()) {
		hidScanInput();
		u32 k = hidKeysDown();
		int tab = TABS[ti]; const PCtl* PT = PTABS[tab]; int nP = PTABN[tab];
		if (k & KEY_L) { ti = (ti + 3) % 4; row = 0; }
		if (k & KEY_R) { ti = (ti + 1) % 4; row = 0; }
		tab = TABS[ti]; PT = PTABS[tab]; nP = PTABN[tab];
		if (row >= nP) row = 0;
		if (k & (KEY_DDOWN | KEY_CPAD_DOWN)) row = (row + 1) % nP;
		if (k & (KEY_DUP   | KEY_CPAD_UP))   row = (row - 1 + nP) % nP;
		if (k & (KEY_B | KEY_START)) break;
		bool activate = (k & KEY_A) != 0;
		int adj = (k & (KEY_DRIGHT | KEY_CPAD_RIGHT)) ? 1 : ((k & (KEY_DLEFT | KEY_CPAD_LEFT)) ? -1 : 0);
		int segSet = -1;
		if (k & KEY_TOUCH) {
			touchPosition mtp; hidTouchRead(&mtp);
			if (mtp.py >= 224 && mtp.px >= 240) break;   // "Done" corner
			else if (mtp.px < 86) { int t2 = ((int)mtp.py - 8) / 30;   // rail: map to the 4 exposed tabs
				for (int j = 0; j < 4; j++) if (TABS[j] == (t2 < 0 ? 0 : t2 > 5 ? 5 : t2)) { ti = j; row = 0; } }
			else for (int i2 = 0; i2 < nP; i2++) { const PCtl* c2 = &PT[i2];
				if (mtp.px < c2->x || mtp.px >= c2->x + c2->w || mtp.py < c2->y || mtp.py >= c2->y + c2->h) continue;
				row = i2;
				if (c2->kind == PK_SEG) segSet = ui_seg_hit(c2->x, c2->w, c2->nseg, (float)mtp.px);
				else if (c2->kind == PK_STEP) adj = (mtp.px < c2->x + 24) ? -1 : 1;
				else if (c2->kind == PK_SWATCH) { int cc = ((int)mtp.px - c2->x) / 30; g_prefs.padColor = cc<0?0:cc>4?4:cc; SETSAVE(); }
				else activate = true;
				break; }
		}
		int act = PT[row].act, pk = PT[row].kind;
		if (pk == PK_SEG && (segSet >= 0 || adj)) {
			int cur, ns = PT[row].nseg;
			// I4.6: the same `default: ... = g_prefs.padEdge` trap as the pause menu, twice more.
			switch (act) { case ACT_SCALE_TOP: cur=scaleMode[0]; break; case ACT_SCALE_BOT: cur=scaleMode[1]; break;
				case ACT_FILTER: cur=smooth[0]; break; case ACT_HUD: cur=hudMode; break; case ACT_AUDIOMODE: cur=audioMode; break;
				case ACT_TOUCHMODE: cur=touchMode; break; case ACT_TILT: cur=g_prefs.tiltLevel; break;
				default: cur=g_prefs.padEdge; break; }
			cur = (segSet >= 0) ? segSet : ((cur + adj + ns) % ns);
			switch (act) { case ACT_SCALE_TOP: scaleMode[0]=cur; break; case ACT_SCALE_BOT: scaleMode[1]=cur; break;
				case ACT_FILTER: smooth[0]=smooth[1]=cur; break; case ACT_HUD: hudMode=cur; break; case ACT_AUDIOMODE: audioMode=cur; break;
				case ACT_TOUCHMODE: touchMode=cur; break; case ACT_TILT: g_prefs.tiltLevel=cur; break;
				default: g_prefs.padEdge=cur; break; }
			SETSAVE();
		} else if (pk == PK_STEP && adj) { int* v = (act==ACT_VOLA)?&volA:&volB; *v += adj*32; if(*v<0)*v=0; if(*v>256)*v=256; SETSAVE(); }
		else if (activate) {
			switch (act) {
			case ACT_SWAP: swapped=!swapped; break; case ACT_FS: fsOn=!fsOn; break; case ACT_MUTE: muted=!muted; break;
			case ACT_3D: s3dEnabled=!s3dEnabled; break; case ACT_DOF: dofOn=!dofOn; break; case ACT_BLOOM: bloomOn=!bloomOn; break;
			case ACT_LIGHT: lightOn=!lightOn; break; case ACT_VIVID: vividOn=!vividOn; break;
			case ACT_PRESENCE: presenceOn=!presenceOn; break;   // A6.2 site 3 (see the note below)
			case ACT_PREVIEW_PAD: touchMode=TOUCH_PAD; break; case ACT_PREVIEW_SMART: touchMode=TOUCH_SMART; break;
			default: break; }
			SETSAVE();
		}

		C2D_TextBufClear(txtBuf);
		C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
		C2D_TargetClear(top, g_ui.bg); C2D_SceneBegin(top);
		assets_text(txtBuf, FNT_SG_BOLD, "Settings", 20.0f, 20.0f, 20.0f, g_ui.text);
		assets_text(txtBuf, FNT_JBM_MED, "configure before you pick a game", 22.0f, 48.0f, 9.0f, g_ui.dim);
		// Phase 14 / I5.2: on an Old 3DS the TILT row stays visible AND adjustable — the preference
		// must round-trip so a card moved to a New 3DS gives the user what they picked — but the
		// live level is clamped to 0 by gate rule G2. Say so instead of letting it look broken.
		// This is the one place s_isN3DS earns its keep (I4.13: run_settings takes no model arg).
		if (g_prefs.tiltLevel > 0 && !s_isN3DS)
			assets_text(txtBuf, FNT_JBM_MED, "Tilt is set, but this is an Old 3DS - it stays flat.",
			            22.0f, 62.0f, 9.0f, C2D_Color32(0xFF, 0x80, 0x40, 0xFF));
		assets_text(txtBuf, FNT_JBM_MED, "L / R  switch tab", 22.0f, 210.0f, 9.0f, g_ui.dim);
		assets_text(txtBuf, FNT_JBM_MED, "B  done", 22.0f, 224.0f, 9.0f, g_ui.dim);

		C2D_TargetClear(bot, g_ui.bg); C2D_SceneBegin(bot);
		assets_draw_plate(PT_PLATE[tab]);
		int fsd = focused;
		for (int i = 0; i < nP; i++) {
			const PCtl* c = &PT[i]; float x=c->x, y=c->y, w=c->w, h=c->h; bool sel=(i==row);
			if (c->ov) assets_text_r(txtBuf, FNT_JBM_MED, c->ov, x-6.0f, y+h/2.0f-4.0f, 8.0f, g_ui.dim);
			switch (c->kind) {
			case PK_TOG: { int on=0; switch(c->act){case ACT_SWAP:on=swapped;break;case ACT_FS:on=fsOn;break;case ACT_MUTE:on=muted;break;
				case ACT_3D:on=s3dEnabled;break;case ACT_DOF:on=dofOn;break;case ACT_BLOOM:on=bloomOn;break;case ACT_LIGHT:on=lightOn;break;
				case ACT_VIVID:on=vividOn;break;case ACT_PRESENCE:on=presenceOn;break;}   // A6.2 site 4
				assets_toggle(on,x,y); if(sel) ui_border(x-2,y-2,w+4,h+4,g_ui.acc,1.5f); break; }
			case PK_SEG: { static const char* const A[3]={"1:1","Aspect-fit","Stretch"};static const char* const F[2]={"Sharp","Smooth"};
				static const char* const H[4]={"off","top","bottom","both"};static const char* const M[3]={"Solo","Mixed","Split"};
				static const char* const T[3]={"Off","Gamepad","Smart"};static const char* const E[3]={"Round","Soft","Sharp"};
				const char* const* o=A; int cur=0; switch(c->act){case ACT_SCALE_TOP:cur=scaleMode[0];break;case ACT_SCALE_BOT:cur=scaleMode[1];break;
				case ACT_FILTER:o=F;cur=smooth[fsd];break;case ACT_HUD:o=H;cur=hudMode;break;case ACT_AUDIOMODE:o=M;cur=audioMode;break;
				case ACT_TOUCHMODE:o=T;cur=touchMode;break;case ACT_TILT:o=TILT_NAMES;cur=g_prefs.tiltLevel;break;
				default:o=E;cur=g_prefs.padEdge;break;}
				assets_seg(txtBuf,x,y,w,h,o,c->nseg,cur,g_ui.ink,g_ui.dim); if(sel) ui_border(x,y,w,h,g_ui.acc,1.5f); break; }
			case PK_STEP: { int v=(c->act==ACT_VOLA)?volA:volB; assets_fill9("fill-secondary-r8",x,y+2,20,h-4,7.0f);
				assets_text_c(txtBuf,FNT_SG_BOLD,"-",x+10,y+2,12.0f,g_ui.text); assets_fill9("fill-secondary-r8",x+w-20,y+2,20,h-4,7.0f);
				assets_text_c(txtBuf,FNT_SG_BOLD,"+",x+w-10,y+2,12.0f,g_ui.text); float bx=x+28,bw=w-56; ui_fill(bx,y+h/2-3,bw,6,g_ui.line,3.0f);
				if(v>0)ui_fill(bx,y+h/2-3,bw*v/256.0f,6,g_ui.acc,3.0f);
				assets_text_r(txtBuf,FNT_JBM_MED,(c->act==ACT_VOLA)?"A":"B",x+26,y-1,8.0f,g_ui.dim); if(sel)ui_border(x,y,w,h,g_ui.acc,1.5f); break; }
			case PK_BTN: { const char* lab=(c->act==ACT_PREVIEW_PAD)?"Preview Gamepad":"Preview Smart";
				assets_button(txtBuf,"btn-secondary",x,y,w,h,lab,FNT_SG_BOLD,13.0f,g_ui.text,0); if(sel)ui_border(x,y,w,h,g_ui.acc,1.5f); break; }
			case PK_SWATCH: { const u32 pc[5]={PAD_COLOR_0,PAD_COLOR_1,PAD_COLOR_2,PAD_COLOR_3,PAD_COLOR_4};
				for(int cc=0;cc<5;cc++){float sx=x+cc*30; if(cc==g_prefs.padColor)ui_border(sx-2,y,26,h,g_ui.text,1.5f); ui_fill(sx,y+3,22,h-6,pc[cc],4.0f);}
				if(sel)ui_border(x-3,y-2,156,h+4,g_ui.acc,1.5f); break; }
			}
		}
		ui_fill(244.0f, 224.0f, 72.0f, 14.0f, g_ui.acc, 5.0f);
		assets_text_c(txtBuf, FNT_SG_BOLD, "Done", 280.0f, 225.0f, 11.0f, g_ui.ink);
		C3D_FrameEnd(0);
	}
	#undef SETSAVE
}

static void run_splash(C3D_RenderTarget* top, C3D_RenderTarget* bot, C2D_TextBuf txtBuf, const char* warn) {
	const int DUR = 170;   // ~2.8s at 60fps; A/START/touch skips
	for (int f = 0; f < DUR && aptMainLoop(); f++) {
		hidScanInput();
		if (hidKeysDown() & (KEY_A | KEY_B | KEY_START | KEY_TOUCH)) break;
		float t = (float)f / DUR;
		float fade  = t > 0.94f ? (t - 0.94f) / 0.06f : 0.0f;
		float pulse = 0.6f + 0.4f * ((f / 20) % 2);

		C2D_TextBufClear(txtBuf);
		C3D_FrameBegin(C3D_FRAME_SYNCDRAW);

		// TOP: the full splash chrome plate is the art (logo glyph + wordmark + divider + tagline).
		C2D_TargetClear(top, g_ui.bg); C2D_SceneBegin(top);
		assets_draw_plate("splash-top");
		if (fade > 0.0f) C2D_DrawRectSolid(0, 0, 0, 400, 240, C2D_Color32(0x0A, 0x07, 0x12, (u8)(fade * 255)));

		// BOTTOM: chrome plate + the pulsing primary button; label drawn in the baked font.
		C2D_TargetClear(bot, g_ui.bg); C2D_SceneBegin(bot);
		assets_draw_plate("splash-bot");
		{	// manifest splash-bot: btn-primary at x72 y101 w175 h42
			C2D_Image b = assets_wgt("btn-primary");
			if (b.tex) C2D_DrawImageAt(b, 72.0f, 101.0f, 0.6f, NULL,
			           175.0f / (b.subtex ? b.subtex->width : 175.0f), 42.0f / (b.subtex ? b.subtex->height : 42.0f));
			(void)pulse;
			assets_text_c(txtBuf, FNT_SG_BOLD, "TAP TO START", 159.0f, 113.0f, 15.0f, g_ui.ink);
		}
		if (warn && warn[0]) assets_text(txtBuf, FNT_JBM_MED, warn, 6.0f, 4.0f, 8.0f, C2D_Color32(0xFF, 0x80, 0x40, 0xFF));
		if (fade > 0.0f) C2D_DrawRectSolid(0, 0, 0, 320, 240, C2D_Color32(0x0A, 0x07, 0x12, (u8)(fade * 255)));

		C3D_FrameEnd(0);
	}
}

int main(int argc, char** argv) {
	bool isN3DS = false;
	APT_CheckNew3DS(&isN3DS);
	// Self-calibrating 804MHz probe: time a busy loop at the base clock, enable the speedup, time it
	// again. A real clock jump (the .cia exheader granting 804MHz/L2) makes the 2nd run ~3x faster; a
	// .3dsx from the Homebrew Launcher can't claim it, so the two times match.
	u64 clkOff = busy_ticks();
	osSetSpeedupEnable(true);            // request 804MHz + L2 (no-op on O3DS / without the .cia flags)
	u64 clkOn  = busy_ticks();
	APT_SetAppCpuTimeLimit(80);
	aptHook(&s_aptCookie, apt_hook, NULL);   // pause emulation/render while backgrounded
	bool speedupActive = (clkOn * 100 < clkOff * 65);   // 2nd run >~1.5x faster => speedup engaged
	// Phase 14 (I4.13 / I5.3): publish both to the file-scope statics so run_settings — which has
	// no model parameter and must not grow one — and the gs-log header can read them.
	s_isN3DS = isN3DS; s_speedupActive = speedupActive;
	gs_log_set_env(isN3DS, speedupActive);   // stamped into the gs-log header (I5.3)

	char perfWarn[160] = "";
	if (!isN3DS)
		snprintf(perfWarn, sizeof perfWarn,
		         "Old 3DS detected.\nTwo GBA cores need a New 3DS - expect slowdown.");
	else if (!speedupActive)
		snprintf(perfWarn, sizeof perfWarn,
		         "Running SLOW (no 804MHz / L2).\nInstall + run the .CIA for full speed.");

	gfxInitDefault();
	mkdir("sdmc:/3DGBA", 0777);   // first-run: make the data folder so a fresh console finds ROMs/saves/settings
	C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
	C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
	C2D_Prepare();
	assets_init();   // device-native art pack (plates/widgets/fonts); code-drawn fallback if absent
	warp_grid_init();   // M2 grid-warp shader (falls back to the quad warp if it fails)
	tilt_init();        // phase 14 tilt shader + vertex arena (AFTER warp_grid_init: reuses warpIbo)
	// (phase 15's co-op avatar sheet is deliberately NOT built here — presence_art_ensure() is
	//  called from run_session when the stored pref is on, and from the pause-menu row the moment it
	//  is turned on. FIX PASS finding 10: the pref ships default-OFF and the sheet is a permanent
	//  64 KB of linear heap next to the GBA framebuffers, the ~100 KB tilt VBO and the prescale
	//  textures, so a user who never enables co-op must not pay for it. Still built ONCE and still
	//  off the per-frame path — just at the first moment it can be needed. presence_art_fini below
	//  is safe whether or not it ever ran.)
	audio_init();   // ndsp; silently no-ops if dspfirm.cdc isn't present
	netlink_init(); // wireless link (UDS); no-ops without the .cia's nwm::UDS grant
	s_hasPtm = R_SUCCEEDED(ptmuInit());   // battery level for the HUD
	C3D_RenderTarget* top  = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
	C3D_RenderTarget* topR = C2D_CreateScreenTarget(GFX_TOP, GFX_RIGHT);   // right eye (stereoscopic 3D)
	C3D_RenderTarget* bot  = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
	C2D_TextBuf txtBuf = C2D_TextBufNew(4096);

	s32 mainPrio = 0x30;
	svcGetThreadPriority(&mainPrio, CUR_THREAD_HANDLE);

	{   // early prefs read: apply the persisted THEME (+ gameMode etc) before any UI draws.
		// The full read happens again in run_session; this one only wants the g_prefs side effects.
		int sm[2] = { 0, 0 }; bool sm2[2] = { false, false }; bool sw = false;
		int hm = 3, am = 0, va = 256, vb = 256, tm = 0;
		bool f1 = false, f2 = true, f3 = true, f4 = true, f5 = false, f6 = false;   // f6 = presence
		settings_load(sm, sm2, &sw, &hm, &am, &va, &vb, &tm, &f1, &f2, &f3, &f4, &f5, &f6);
	}
	run_splash(top, bot, txtBuf, perfWarn);   // animated boot splash (skippable) + perf warning

	// Session loop: pick two ROMs, play, and on "Change games" pick again.
	while (aptMainLoop()) {
		char pathA[256], pathB[256];
		bool startLinked = false;
		if (!rompicker_run(top, bot, txtBuf, pathA, pathB, sizeof pathA, &startLinked)) {
			strcpy(pathA, "sdmc:/3DGBA/gameA.gba");
			strcpy(pathB, "sdmc:/3DGBA/gameB.gba");
		} else if (!strcmp(pathA, "__SETTINGS__")) {
			run_settings(top, bot, txtBuf);   // configure without a game, then back to the picker
			continue;
		} else {
			rompicker_save_recent(pathA, pathB);   // remember for next boot's resume prompt
		}
		int r = run_session(top, bot, topR, txtBuf, isN3DS, mainPrio, pathA, pathB, startLinked);
		if (r == SESSION_QUIT) break;
		// SESSION_CHANGE -> loop back to the picker
	}

	netlink_exit();
	audio_exit();
	if (s_hasPtm) ptmuExit();
	C2D_TextBufDelete(txtBuf);
	presence_art_fini();
	tilt_fini();
	warp_grid_fini();
	C2D_Fini();
	C3D_Fini();
	gfxExit();
	return 0;
}
