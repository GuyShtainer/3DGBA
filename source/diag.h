// diag.h — Phase 13-prep slice D1: numbered breadcrumb globals + the surviving-thread watchdog.
// ============================================================================================
// PURE C (CLAUDE.md rule #4 / PHASE.md invariant 4): <stdint.h>/<stddef.h> only — no libctru,
// no mGBA — so diag.c dual-compiles on the PC host harness (test/host/test_diag.c) and this
// header is safely includable from netlink.c, gbacore.c AND the host tests.
//
// Design ported from the melonDS-PM freeze watchdog + numbered wifi breadcrumbs
// (docs/kb/external/pm-bridge-forensics.md §13: EmuThread.cpp:568-602, Wifi.cpp:29-31/1236-1238/
// 1846-1847, LAN.cpp:1208-1216; PART 3 port item 6), per the binding spec
// docs/phase13-diagnostics/SPEC-firmware-diag.md §D1. On the 3DS the RENDER thread (which
// survives worker wedges) is the sampler — no extra thread (SPEC D1.6/D1.8; the teardown-wait
// blind spot is accepted and documented there).
//
// TRADE PATH FROZEN (PHASE.md invariant 1): everything here is additive — each crumb stamp is a
// single volatile store inside an already-off-the-fast-path wait/gate branch (SPEC S.4); the
// watchdog only LOADS the stamped words from the render thread.
#pragma once
#include <stdint.h>
#include <stddef.h>   // size_t

// Compile-time slice gates (SPEC S.3): let a reviewer bisect a slice out of a build. The runtime
// kill switch (sdmc:/cias/control/diag_off.txt, checked once per session in main.c) is separate.
#define DIAG_D1_ENABLE 1
#define DIAG_D2_ENABLE 1   // D2: game-heartbeat hang catcher + auto ARM register dump
#define DIAG_D3_ENABLE 1   // D3: per-frame CSV telemetry (own+peer game state + link counters as columns)

// --------------------------------------------------------------------------------------------
// Crumb site IDs — the SPEC D1.1 inventory of every loop/gate that can block or spin.
// One crumb word per subsystem; the LAST stamped site names where that subsystem's thread is
// (or was last) waiting. celiolink.c has NO blocking loops (verified in SPEC D1.1: cl_transfer
// is O(1), every internal loop bounded) — it gets NO crumbs and NO code change; its logical
// holds are already exported via ClStatus (celiolink.h:208-229) and become D3 CSV columns.
// --------------------------------------------------------------------------------------------
enum {
	// g_diagNetCrumb — netlink.c wait loops (workers / RX thread / lobby main thread)
	DIAG_SITE_NET_COLLECT      = 1000,   // net_transfer_collect for(;;) 1ms poll (netlink.c ~410-423)
	DIAG_SITE_NET_CMD_COLLECT  = 1100,   // net_cmd_collect for(;;) — state E only (netlink.c ~465-473)
	DIAG_SITE_NET_ROUND_WAIT   = 1200,   // net_round_wait joiner pacing barrier (netlink.c ~505-511)
	DIAG_SITE_NET_RX_PASS      = 1300,   // net_rx_thread outer pass; iter = g_diagRxSeq (netlink.c ~795)
	DIAG_SITE_NET_LOBBY_DRAIN  = 1400,   // net_ping_update lobby udsPullPacket drain (netlink.c ~270-285)
	// g_diagSioCrumb — gbacore.c driver cross-call spin gates (the run-#4 wedge class)
	DIAG_SITE_SIO_CELIO_GATE   = 2000,   // net_poll_celio link-ready gate; iter = gateHeldN (gbacore.c ~942)
	DIAG_SITE_SIO_CELIO_ISR    = 2100,   // net_poll_celio ISR-edge wait (the run-#4 silent-freeze site)
	DIAG_SITE_SIO_CELIO_PACE   = 2200,   // net_poll_celio master-clock pacing gate (benign/high-frequency)
	DIAG_SITE_SIO_ISR_GATE     = 2300,   // gbacore_net_poll states A-E ISR-proof gate (gbacore.c ~1106)
	DIAG_SITE_SIO_ROUND_OPEN   = 2400,   // gbacore_net_poll one-round-in-flight gate (gbacore.c ~1032)
	DIAG_SITE_SIO_FINISH       = 3000,   // net_finishMulti entry bracket; iter = pendingRound; 0 on exit
	// g_diagMainCrumb — render-thread blockers (stamped for post-mortem only, NOT sampled: the
	// sampler runs on this very thread — SPEC D1.8 accepted blind spot). Bracket style: stamp on
	// entry, store 0 on exit, so nonzero == currently parked there.
	DIAG_SITE_MAIN_PIPE_WAIT   = 4000,   // run_session pause/pipeline LightEvent_Wait(done) (main.c ~1420/1428)
	DIAG_SITE_MAIN_WL_TEARDOWN = 4100,   // wl teardown part->done waits (main.c ~1594/1787/2156)
	DIAG_SITE_MAIN_RX_JOIN     = 4200,   // net_link_stop threadJoin of the RX thread (netlink.c ~898)
};

// Crumb encode/decode. PM model: "1600 + (gatherIter & 0xFF)" (LAN.cpp:1214). DEVIATION from the
// SPEC's &0xFF literal (recorded in BUILDLOG.md 2026-08-03): our site bases are spaced 100 apart
// (they're load-bearing in the SPEC/D3 docs, so they stay), and 0xFF residues would alias
// neighbouring sites (1000+150 == 1100+50 — the spec's "spaced >= 256" premise doesn't hold for
// its own table). Masking the iteration to 0x3F (0..63) keeps every crumb uniquely decodable:
//   site = crumb - crumb % 100,  iter residue = crumb % 100  (always < 64).
#if DIAG_D1_ENABLE
#define DIAG_CRUMB(var, site, iter) ((var) = (uint32_t)(site) + ((uint32_t)(iter) & 0x3Fu))
#else   // bisected out: still EVALUATE iter (stamp sites pass e.g. wdIt++ / counter reads) but store nothing
#define DIAG_CRUMB(var, site, iter) ((void)(uint32_t)(iter))
#endif
#define DIAG_CRUMB_SITE(crumb)      ((uint32_t)(crumb) - ((uint32_t)(crumb) % 100u))
#define DIAG_CRUMB_ITER(crumb)      ((uint32_t)(crumb) % 100u)

// The crumb words (defined in diag.c). volatile u32: single-word aligned stores/loads; writers
// are the threads named in the site table above; cross-thread last-writer-wins races are benign
// telemetry (SPEC D1.2). 0 = never stamped / bracket exited.
extern volatile uint32_t g_diagNetCrumb;    // netlink.c wait loops   (sites 1000-1499)
extern volatile uint32_t g_diagSioCrumb;    // gbacore.c driver gates (sites 2000-3299)
extern volatile uint32_t g_diagMainCrumb;   // render-thread blockers (sites 4000-4299)

// RX-thread loop-seq (SPEC D1.3): incremented once per net_rx_thread outer pass. The RX thread
// is the radio's heartbeat — g_diagRxSeq frozen while workers live = the radio thread wedged
// (a distinct failure class from the resolved close-hang). Worker loop-seq = EmuInstance.frame
// (main.c, pre-existing); render loop-seq = g_renderSeq (static in main.c).
extern volatile uint32_t g_diagRxSeq;

// --------------------------------------------------------------------------------------------
// Watchdog state machine (SPEC D1.4) — pure functions over plain structs, host-tested. The
// render thread gathers a DiagWdSample every ~200 ms and calls diag_wd_step; when a STUCK line
// must be appended to the wd netlog it returns 1 with the line in lineOut.
// --------------------------------------------------------------------------------------------
typedef struct {
	uint32_t renderSeq, aFrame, bFrame, aVf, bVf, rxSeq;   // sampled seqs (aVf/bVf = emulated video frames)
	uint32_t netCrumb, sioCrumb;                            // crumbs (pass-through to the line)
	int      gateN, forceN;                                 // celio quick-reads (pass-through)
	int      wl, seat;                                      // session context for the line (wl 0/1; seat -1/0/1)
} DiagWdSample;

typedef struct {
	DiagWdSample last;       // seqs at the last progress edge
	uint32_t stuckSinceMs;   // 0 = progressing; else nowMs when the current freeze episode began
	uint8_t  fired;          // bitmask: bit0=1s bit1=4s bit2=12s emitted for this episode
} DiagWd;

// watchMask bits — which seqs count as "progress" (SPEC D1.7 arming): under wlOn only the
// participant worker (+RX); under linkOn/netOn both workers (no RX thread there). mask==0 =
// not armed: the step resets any episode and emits nothing.
#define DIAG_WD_AF   (1u << 0)   // emuA.frame (worker-A loop-seq)
#define DIAG_WD_BF   (1u << 1)   // emuB.frame
#define DIAG_WD_AVF  (1u << 2)   // core A produced-video-frame counter (gbacore_frame_counter)
#define DIAG_WD_BVF  (1u << 3)   // core B
#define DIAG_WD_RX   (1u << 4)   // g_diagRxSeq (wireless RX thread)

// Step every ~200 ms. ANY watched seq advancing = progress (episode reset). All watched frozen
// -> escalate ONCE each at >=1000/4000/12000 ms (ascending, one line per call, so a sampler gap
// still emits all three in order). Returns 1 and fills lineOut (one '\n'-terminated STUCK line,
// snprintf-truncated to cap, always NUL-terminated) when a line must be appended; else 0.
int diag_wd_step(DiagWd* wd, uint32_t nowMs, const DiagWdSample* s, uint32_t watchMask,
                 char* lineOut, size_t cap);

// The exact STUCK line (SPEC D1.5, decimal everywhere; crumbs decode via DIAG_CRUMB_SITE/ITER):
//   STUCK ms=<1000|4000|12000> rseq= aF= bF= aVf= bVf= rx= netC= sioC= gateN= forceN= wl= seat=\n
// Exposed separately so the host test can golden-compare the bytes. Returns snprintf's result
// (would-be length; output truncated to cap-1 + NUL when cap is too small — never overflows).
int diag_stuck_format(char* out, size_t cap, uint32_t ms, const DiagWdSample* s);

// --------------------------------------------------------------------------------------------
// D2 — game-heartbeat hang catcher + auto ARM register dump (SPEC D2). Model: the DeSmuME-PM
// always-on crash catcher ("exported frameCounter frozen >180 frames while a session is wanted
// => dump ARM regs + 32-word stack", mp_bridge.cpp:1133-1174, cap 24 dumps/episode, reset when
// the counter moves again) + the melonDS flavour that adds IE/IF/IME so "IRQ masked forever"
// wedges are readable (EmuThread.cpp:510-533) — docs/kb/external/pm-bridge-forensics.md §6/§12,
// pm-rom-abi.md §8, PART 3 port item 1.
//
// Two watched tiers (SPEC D2.1), both fed by the render thread's ~200 ms sampler in main.c:
//   Tier A (CORE): gbacore_frame_counter(participant) — mGBA's produced-video-frame counter.
//     Frozen => the emulated core is producing nothing at all (worker park / the run-#4 driver-
//     gate wedge class) -> DIAG_HANG_DUMP_CORE.
//   Tier B (GAME): gMain.vblankCounter1 (GameProfile.vblankCtr, gamestate.h) — the game's own
//     VBlank-ISR heartbeat. Frozen while Tier A advances => the game's IRQ delivery is dead
//     (IME/IE masked forever, or the CPU halted wrong) -> DIAG_HANG_DUMP_GAME; the dump's
//     IE/IF/IME line makes the difference readable.
// Known limitation (SPEC D2.1, accepted): a main-loop-only wedge with the VBlank IRQ alive
// advances BOTH counters and is caught by D3 (static cb2 + pinned celio section), not D2.
// --------------------------------------------------------------------------------------------
typedef enum { DIAG_HANG_NONE = 0, DIAG_HANG_DUMP_CORE, DIAG_HANG_DUMP_GAME } DiagHangAction;

// armed bitmask for diag_hang_step. 0 = disarmed (session gone): the step clears the episode and
// never fires. Tier B is armed only when the participant's GameProfile maps vblankCtr — feeding a
// constant placeholder with the tier armed would false-DUMP_GAME after the threshold.
#define DIAG_HANG_ARM_CORE (1 << 0)   // watch vf (Tier A)
#define DIAG_HANG_ARM_GAME (1 << 1)   // watch vbl (Tier B)

#define DIAG_HANG_FROZEN_FRAMES 180   // trigger: frozen > this many RENDER frames (~3 s at the
                                      // ~60 fps render loop) — PM "frozen >180 frames", mp_bridge.cpp:1135
#define DIAG_HANG_DUMP_CAP      24    // PM "Cap 24 dumps per freeze episode", mp_bridge.cpp:1133-1174

typedef struct {
	uint32_t lastVf, lastVbl;     // last observed counters
	uint32_t lastRseq;            // render loop-seq (g_renderSeq) at the last step: freeze time is
	                              // COUNTED IN RENDER FRAMES via the seq delta, so the ">180 frames"
	                              // threshold holds at any caller cadence (per-frame or 200 ms tick)
	uint16_t frozenVf, frozenVbl; // consecutive armed render frames each counter has been static (saturating)
	uint8_t  dumps;               // dumps emitted THIS episode (cap DIAG_HANG_DUMP_CAP)
	uint8_t  init;                // first-armed-call baseline taken (0 after memset/disarm)
} DiagHang;

// Step once per sampler call (main.c: the ~200 ms D1.6 tick). renderSeq = g_renderSeq (the freeze
// clock); vf/vbl = the two tier counters. Returns the dump the CALLER must write now, at most one
// per call (=> one dump per ~200 ms tick after the trigger — the PM cadence) until the 24-cap.
// Tier A takes precedence (a parked core freezes both counters). Re-arm rule: the moment no armed
// tier is over threshold (the frozen counter advanced), frozenN and dumps reset — a later freeze
// is a fresh episode with its own 24 (PM "counter resets when FC moves again").
DiagHangAction diag_hang_step(DiagHang* h, int armed, uint32_t renderSeq, uint32_t vf, uint32_t vbl);

// Read-only ARM CPU snapshot of one core (filled by gbacore_dump_cpu, gbacore.c — the one TU
// allowed to see mGBA's struct ARMCore; SPEC D2.4). Plain stdint mirror so this header and the
// diag_hang_format formatter stay pure C. Field sources (external/mgba/include, verified):
//   gprs[16]              <- ARMCore.gprs (arm.h ARM_REGISTER_FILE; 13=SP 14=LR 15=PC)
//   cpsr/spsr             <- ARMCore.cpsr/.spsr union PSR .packed (arm.h)
//   bankedR13/R14[6]      <- ARMCore.bankedRegisters[bank][0..1] (arm.h; arm.c ARMSetPrivilegeMode:
//                            slot 0 = that mode's r13, slot 1 = r14; FIQ also banks r8-r12 in 2-6)
//   bankedSPSR[6]         <- ARMCore.bankedSPSRs[bank]; banks: 0 NONE(usr/sys) 1 FIQ 2 IRQ 3 SVC
//                            4 ABT 5 UND (enum RegisterBank, arm.h)
//   ie/if_/ime            <- GBA bus 0x4000200/0x4000202/0x4000208 (the melonDS IE/IF/IME columns)
//   sp + stack[32]        <- 32 words at (sp & ~3): 8 below SP, 24 above (callee frames sit above);
//                            stackValid=1 only when SP points into EWRAM/IWRAM (0x02/0x03)
// CAVEAT (offline analysis): the CURRENT mode's live SP/LR are gprs[13..14]; that mode's bank
// slots are STALE (mGBA swaps banks on mode change). Interpret via cpsr's low 5 mode bits.
typedef struct {
	uint32_t gprs[16];            // r0-r15 (r13 SP, r14 LR, r15 PC)
	uint32_t cpsr, spsr;          // union PSR .packed
	uint32_t bankedR13[6], bankedR14[6], bankedSPSR[6];   // banks: NONE,FIQ,IRQ,SVC,ABT,UND
	uint16_t ie, if_, ime;        // GBA 0x4000200 / 0x4000202 / 0x4000208
	uint32_t sp;                  // = gprs[13] at capture
	uint32_t stack[32];           // 32 words read at (sp & ~3) - 32 .. +92 (see gbacore_dump_cpu)
	uint8_t  stackValid;          // sp pointed into 0x02/0x03 RAM
} GbaCpuDump;

// One whole hang dump as text (~12-16 lines, %08X words; SPEC D2.5 format), appended to the
// sdmc netlog hang file by main.c. kind = the DiagHangAction that fired (CORE/GAME); nowMs +
// crumbs stamp the header so the dump correlates with the wd STUCK lines. Pure (snprintf only);
// returns the would-be total length, output truncated to cap-1 + NUL — never overflows.
int diag_hang_format(char* buf, size_t cap, const GbaCpuDump* d, uint32_t vf, uint32_t vbl,
                     int kind, uint32_t nowMs, uint32_t netCrumb, uint32_t sioCrumb);

// --------------------------------------------------------------------------------------------
// D3 — per-frame CSV telemetry (SPEC D3). Model: the melonDS-PM per-frame CSV schema
// (pm-bridge-forensics.md §11, EmuThread.cpp:535-553), the DeSmuME-PM apCsv flush-256 discipline
// (§3, mp_bridge.cpp:1544-1559), and the ROM diag-block-as-columns idea (pm-rom-abi.md §6);
// PART 3 port item 4 ("game state side by side, plus the link layer's counters as CSV columns
// every frame — not occasional # celio lines").
//
// One row per render frame while a wireless session is armed (main.c glue: wlOn + participant
// core, emitted from the gs-logger block where the participant's GameState is already in hand).
// The HOST and JOIN files diff mechanically — the peer-view columns (clPB/clSelP/clPCard) are
// the peer's state as locally known; the two-sided truth IS the HOST-vs-JOIN file diff.
// The struct mirrors the SPEC D3.3 column list ONE FIELD PER COLUMN, in column order; main.c
// fills it from the exports (gbacore_net_counters / net_event_get_stats / net_link_get_stats /
// net_event_get_queue / net_link_get_rtt) + the participant GameState + the D2.1 vbl heartbeat.
// --------------------------------------------------------------------------------------------
typedef struct {
	// meta
	uint32_t tms;             // wall-clock ms (osGetTime at the top of the render frame)
	uint32_t rf;              // g_renderSeq — a GAP in rf between adjacent rows == menu-open frames
	int      exp;             // gbacore_net_get_exp() (5 = state F / Celio)
	// own game (the participant core's GameState; gamestate.h)
	int      ctx;             // GameCtx
	uint32_t cb2;             // raw gMain.callback2, Thumb-stripped (HEX column)
	int      px, py;          // SaveBlock1 player tile (-1 = sb1 not ready)
	int      mapg, mapn;      // SaveBlock1.location mapGroup/mapNum (-1 = n/a)
	int      objx, objy;      // gObjectEvents[0] true avatar tile (-1 = n/a)
	int      face;            // facing 1=D 2=U 3=L 4=R (-1 = n/a)
	int      sb1;             // sb1Valid 0/1
	uint32_t lstat;           // gLinkStatus (HEX)
	int      lerr;            // gLinkErrorOccurred 0/1
	uint32_t lnrecv;          // gRemoteLinkPlayersNotReceived (HEX)
	uint32_t lbuf0, lbuf1;    // sLinkErrorBuffer[0..3]/[4..7] (HEX)
	// game heartbeat (D2.1 address — this column IS its verify-on-hw self-verification: ~60/s)
	uint32_t vbl;             // gMain.vblankCounter1 (0 when the profile doesn't map it)
	// celio FSM (gbacore_net_counters <- worker-captured ClStatus + s_celio* statics)
	int      clSec, clSt, clBlk;         // section / packet-layer state / blockSeq (-1 = no capture yet)
	uint32_t clFrm;                      // completed 8-word frames since cl_init
	uint32_t clPB;                       // peer party bytes captured (0..600)
	int      clTC;                       // tradeComplete 0/1
	int      clHP, clHS, clHC;           // level-triggered holds: party / select / confirm
	int      clSelL, clSelP;             // local / peer trade slot (-1 = none yet)
	int      clExitP, clSessEnd;         // run-#12 room-exit progress
	int      clPCard, clIdReal;          // peer real card cached / real identity served
	int      clOutQ;                     // outgoing ClEvent queue depth (0..CL_EVENT_QUEUE_DEPTH-1)
	int      gateN, cForceN, resetN;     // link-ready gate holds / wedge escapes / session restarts
	int      sioMode;                    // GBASIOMode as seen by the driver (-1 = none)
	uint32_t siocnt;                     // last captured SIOCNT (HEX)
	// SIO driver (gbacore_net_counters <- gbacore.c statics + netlog ring tail)
	int      startN, injN, finN, okN, toN, edgeN, forceN;
	uint32_t round;                      // shared per-link round counter
	uint32_t lastW0, lastW1;             // ring tail: last retired round's seat words (HEX)
	int      lastOk;                     // ring tail ok flag (-1 = no rounds logged yet)
	// transport (netlink.c exports)
	int      rtt;                        // pure-network ping RTT ms (-1 = none yet)
	int      txSeq, txAcked, rxDel;      // EVENT channel: highest sent / peer-ACKed / delivered seq
	int      evOvf, evRetx;              // EVENT channel: inbound overflow (must stay 0) / re-sends
	int      evTxQ;                      // EVENT channel outbound un-ACKed backlog (run-#6 overflow X-ray)
	int      rxWordN, txFails, busyN;    // WORD plane: received words / send fails / TX-busy retries
	int      peerUp;                     // unicast peer resolved 0/1
} DiagCsvRow;

// Both header lines (SPEC D3.3): line 1 = "# 3DGBA csv role=<HOST|JOIN> seat=<n> built=..." with
// the D3.4 menu-gap disclosure; line 2 = the column-name header (comma count == DiagCsvRow field
// count — host-test asserted so a future column add can't desync header vs row). Returns the
// would-be total length (snprintf contract, truncated to cap-1 + NUL); <0 on degenerate args.
int diag_csv_header(char* buf, size_t cap, int seat);

// One '\n'-terminated CSV row (SPEC D3.6). Format rules (D3.3, binding): decimal counters; %X hex
// (no 0x) for cb2,lstat,lnrecv,lbuf0,lbuf1,siocnt,lastW0,lastW1; -1 propagated for "none";
// booleans 0/1. Pure snprintf; returns the would-be length; <0 on degenerate args.
int diag_csv_row(char* buf, size_t cap, const DiagCsvRow* r);
