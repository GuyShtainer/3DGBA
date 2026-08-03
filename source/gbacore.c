// gbacore.c — the ONLY translation unit that includes mGBA headers.
// Keeps mGBA's u8/u16/etc. out of main.c (libctru). Built with the same defines
// libmgba.a was built with (see Makefile MGBA_DEFS / docs/kb/mgba-integration.md).

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>   // O_RDONLY / O_WRONLY / O_CREAT
#include <sys/stat.h>   // mkdir (ensure the netlog dir exists)

#include <mgba/core/core.h>
#include <mgba/core/config.h>
#include <mgba/core/serialize.h>   // mCoreSaveStateNamed / SAVESTATE_ALL
#include <mgba/core/lockstep.h>            // mLockstepUser
#include <mgba/internal/gba/sio/lockstep.h> // GBASIOLockstepCoordinator / Driver
#include <mgba/internal/gba/sio.h>         // GBASIO, GBASIOTransferCycles, GBASIOMode (net link driver)
#include <mgba/internal/gba/gba.h>         // struct GBA (memory.io, timing)
#include <mgba/internal/arm/arm.h>         // struct ARMCore (gprs/cpsr/banked) — D2 gbacore_dump_cpu
                                           // READS through the public header only (mGBA files never
                                           // modified; MPL note in SPEC-firmware-diag D2 order gates)
#include <mgba/core/timing.h>              // mTimingSchedule / mTimingDeschedule
#include <mgba/gba/interface.h>            // mPERIPH_GBA_LINK_PORT
#include <mgba-util/vfs.h>
#include <mgba-util/audio-buffer.h>

#undef GBA_H   // mGBA's gba.h uses GBA_H as its include guard; gbacore.h reuses the name for the GBA
               // screen height (160). gba.h is already fully included above, so drop the guard macro
               // here to avoid a redefinition warning when gbacore.h defines GBA_H = 160.
#include "gbacore.h"
#include "celiolink.h"   // Celio-style local-termination link partner (state F)
#include "control.h"     // D4 file-driven movement — the g_ctlStat mirror for the '# control' line
#include "diag.h"        // D1 breadcrumbs: pure-C header (no mGBA/libctru types) — one volatile
                         // store per gated return names WHERE the driver's clock parked
                         // (SPEC-firmware-diag D1.1 sites 2000-3000; the run-#4 wedge class)
#include "fingerprint.h"  // D6: DGBA_NET_EXP_DEFAULT (the shared session-entry link strategy)

// FIXED_ROM_BUFFER: libmgba's 3DS build (ctru-heap.c, compiled into libmgba.a) DEFINES these
// and allocates one boot romBuffer (~32 MB). The GBA core points gba->memory.rom at romBuffer,
// captured at load/reset. For dual-core we reuse the boot buffer for the FIRST core and malloc
// a dedicated buffer for the SECOND, pointing the global at each core's buffer before its load.
// Loads run sequentially on the main thread at startup, so there's no race; normal runFrame
// uses the per-core gba->memory.rom, not this global.
extern uint32_t* romBuffer;
extern size_t    romBufferSize;
static bool s_bootRomBufTaken = false;

// mLockstepUser + the C-callback bridge to the frontend's worker park/resume.
struct LinkUser {
	struct mLockstepUser u;        // MUST be first (we cast mLockstepUser* <-> LinkUser*)
	void (*onSleep)(void*);
	void (*onWake)(void*);
	void* ctx;
	int   requestedId;
	int   playerId;
};

struct GbaLink {
	struct GBASIOLockstepCoordinator coord;
};

// M2.5 net-link SIO driver state (one per core, alongside the lockstep linkDriver). The driver
// proper is in the "Net link" section below; see docs/kb/wireless-link-architecture.md.
enum NetChildPhase { NET_IDLE = 0, NET_SENDING, NET_RECEIVING };   // child driver phase (one-round-latency split)
struct NetDriver {
	struct GBASIODriver d;       // MUST be first — mGBA holds &d; we cast it back to NetDriver
	int      seat;               // our GBA playerId (0 = parent / clock owner)
	int      peers;              // number of OTHER GBAs (1 for a 2-seat trade)
	uint32_t needMask;           // bitmask of the seats required for a complete round
	uint32_t pendingRound;       // the round finishMultiplayer() must collect
	bool     roundOpen;          // parent: a round was started but not yet retired by net_finishMulti
	uint32_t lastInjectedRound;  // child: last round it self-scheduled (sentinel 0xFFFFFFFF = none)
	// --- M2.5 child phase split (VBA-M-faithful one-round latency) ---
	enum NetChildPhase phase;    // child only; NET_IDLE on the parent
	uint32_t isrWaitRound;       // round whose SIO ISR must finish before we capture next; 0xFFFFFFFF = none
	uint32_t irqArmTime;         // local cycle stamp when that round's RECEIVING-end armed the IRQ
	uint32_t lastActiveFrame;    // child PACING: emulated frame (VBlank) counter at the last injected round
	// --- State E (bridge) command-framed peer assembly. A Gen-3 command = 8 MULTI words (rounds
	// 8C..8C+7) + checksum; the bridge delivers the peer seat's command ATOMICALLY (all 8 genuine, or
	// all-zero idle), never mid-command — so a deferred word never poisons the checksum. peerCmd caches
	// the command currently being fed to the game word-by-word across its 8 transfers. ---
	uint32_t peerCmdIndex;       // peer command index (round>>3) currently cached; 0xFFFFFFFF = none
	uint16_t peerCmd[8];         // the cached peer command words (genuine, or zeros on idle-substitute)
	uint8_t  peerCmdReal;        // diag: 1 = genuine command assembled, 0 = idle-substituted (peer absent)
	// --- State F (Celio local termination): cl IS the link partner; only semantic ClEvents cross the wire.
	// clMyWord/clPartnerWord cache the slave words (computed post-ISR in net_poll_celio) for net_celio_fill. ---
	CelioLink cl;               // the synthesized partner (role = OPPOSITE the local game)
	uint16_t  clMyWord;         // slave: the game own captured word for this transfer
	uint16_t  clPartnerWord;
	bool      clkReady;         // slave: the game is LISTENING (multi mode + SIO IRQ) -> we may clock
	bool      clkPaceValid;     // slave: nextClockCyc holds a valid emulated-cycle pace deadline
	uint32_t  nextClockCyc;     // slave: do not clock the next transfer before this emulated cycle
	uint64_t  lastXferTick;     // wall tick of the last Celio transfer (long gap => the game restarted)
	uint32_t  gateHeldN;        // consecutive gated polls (hysteresis: only a LONG hold re-arms the handshake)
};

struct GbaCore {
	struct mCore* core;
	void*         rom;            // dedicated buffer if we malloc'd one; NULL if using the boot buffer
	char          rompath[256];   // for deriving save-state paths

	struct GbaLink*             link;        // non-NULL while attached to a coordinator
	struct GBASIOLockstepDriver linkDriver;
	struct LinkUser             linkUser;
	struct NetDriver            netDriver;   // M2.5 net link (alternative to linkDriver; never both)
};

// Replace the ROM's final extension with ".sav" (e.g. ".../gameA.gba" -> ".../gameA.sav").
static void derive_sav_path(const char* rom, char* out, size_t cap) {
	snprintf(out, cap, "%s", rom);
	char* dot = strrchr(out, '.');
	if (dot) snprintf(dot, cap - (dot - out), ".sav");
	else     snprintf(out + strlen(out), cap - strlen(out), ".sav");
}

GbaCore* gbacore_create(void) {
	GbaCore* g = (GbaCore*)calloc(1, sizeof(*g));
	if (!g) return NULL;
	struct mCore* core = mCoreCreate(mPLATFORM_GBA);
	if (!core) { free(g); return NULL; }
	if (!core->init(core)) { core->deinit(core); free(g); return NULL; }
	mCoreInitConfig(core, NULL);
	// Match mGBA's own frontend: detect + skip GBA idle loops (big win for games like
	// Pokemon that busy-wait a lot). Read by the core at reset, so set it before load.
	mCoreConfigSetDefaultValue(&core->config, "idleOptimization", "detect");
	// Audio isn't muted by accident: the GBA core takes masterVolume from core->opts.volume,
	// and a zero-initialised opts == SILENCE. Force full volume + unmuted, then apply.
	core->opts.volume = 0x100;   // GBA_AUDIO_VOLUME_MAX
	core->opts.mute   = false;
	mCoreConfigSetDefaultValue(&core->config, "volume", "256");
	mCoreConfigSetDefaultValue(&core->config, "mute",   "0");
	core->loadConfig(core, &core->config);
	// Headroom so the main thread can poll-read audio once per frame without the core's
	// ring buffer overflowing between reads (~549 stereo frames produced per GBA frame).
	core->setAudioBufferSize(core, 4096);
	g->core = core;
	return g;
}

void gbacore_set_video_buffer(GbaCore* g, uint16_t* buf, unsigned stride) {
	// mColor == uint16_t under COLOR_16_BIT/COLOR_5_6_5 (how libmgba was built).
	g->core->setVideoBuffer(g->core, (mColor*)buf, stride);
}

bool gbacore_load_rom(GbaCore* g, const char* path) {
	struct VFile* vf = VFileOpen(path, O_RDONLY);
	if (!vf) return false;
	// Allocate THIS core's ROM buffer and point the global at it before loading, so the
	// core captures its own ROM. (FIXED_ROM_BUFFER avoids _pristineCow, which NULL-crashes
	// on 3DS.) Loads are sequential at startup -> no race on the global.
	size_t sz = vf->size(vf);
	if (!s_bootRomBufTaken && romBuffer && romBufferSize >= sz) {
		s_bootRomBufTaken = true;         // first core: reuse libmgba's boot romBuffer as-is
		g->rom = NULL;                    // we don't own it -> don't free it
	} else {
		g->rom = malloc(sz);              // second core: its own buffer
		if (!g->rom) { vf->close(vf); return false; }
		romBuffer = (uint32_t*)g->rom;
		romBufferSize = sz;
	}
	if (!mCorePreloadVF(g->core, vf)) {   // copies the ROM into romBuffer
		vf->close(vf);                    // on success the core owns vf
		free(g->rom); g->rom = NULL;
		return false;
	}
	// Battery save: load <rom>.sav (writable, created if missing) so SRAM persists. mGBA's
	// mCoreAutoloadSave needs core->dirs configured (we don't), so load by explicit path.
	strncpy(g->rompath, path, sizeof g->rompath - 1);
	g->rompath[sizeof g->rompath - 1] = '\0';
	char savpath[300];
	derive_sav_path(g->rompath, savpath, sizeof savpath);
	mCoreLoadSaveFile(g->core, savpath, false);
	g->core->reset(g->core);              // captures gba->memory.rom = romBuffer (this core's)
	return true;
}

// Load an explicit .sav into this core and reset so the game boots with it. Used by the
// in-game ".sav" loader (separate from the auto <rom>.sav above and from save STATES).
bool gbacore_load_save(GbaCore* g, const char* path) {
	if (!g || !g->core) return false;
	if (!mCoreLoadSaveFile(g->core, path, false)) return false;
	g->core->reset(g->core);
	return true;
}

void gbacore_set_keys(GbaCore* g, uint16_t keys) {
	g->core->setKeys(g->core, keys);
}

void gbacore_set_frameskip(GbaCore* g, int n) {
	if (!g || !g->core) return;
	g->core->opts.frameskip = n;
	g->core->loadConfig(g->core, &g->core->config);   // applies to gba->video.frameskip (keeps volume)
}

void gbacore_run_frame(GbaCore* g) {
	g->core->runFrame(g->core);
	// Audio stays in the core's buffer; the caller reads it (focused core) or drains it
	// (unfocused core) each frame via the gbacore_*_audio helpers below.
}

void gbacore_run_loop(GbaCore* g) {
	// One ARM run-slice. Unlike runFrame (which loops to a full frame, ignoring earlyExit),
	// this returns the instant the lockstep parks the core -> we can block exactly there.
	g->core->runLoop(g->core);
}

unsigned gbacore_sample_rate(GbaCore* g) {
	unsigned r = g->core->audioSampleRate(g->core);
	return r ? r : 32768;
}

unsigned gbacore_ndsp_rate(GbaCore* g) {
	// Match playback to the 3DS LCD's true refresh (16756991/280095 = 59.826 Hz) so audio
	// neither drifts ahead nor lags the ~59.73 Hz GBA core. Same trick mGBA's 3DS port uses.
	double ratio = mCoreCalculateFramerateRatio(g->core, 16756991.0 / 280095.0);
	return (unsigned)(gbacore_sample_rate(g) * ratio);
}

size_t gbacore_audio_available(GbaCore* g) {
	return mAudioBufferAvailable(g->core->getAudioBuffer(g->core));
}

size_t gbacore_read_audio(GbaCore* g, int16_t* out, size_t frames) {
	return mAudioBufferRead(g->core->getAudioBuffer(g->core), out, frames);
}

void gbacore_drain_audio(GbaCore* g) {
	mAudioBufferClear(g->core->getAudioBuffer(g->core));
}

bool gbacore_save_state(GbaCore* g, int slot) {
	if (!g || !g->core || !g->rompath[0]) return false;
	char p[300];
	snprintf(p, sizeof p, "%s.ss%d", g->rompath, slot);
	struct VFile* vf = VFileOpen(p, O_WRONLY | O_CREAT | O_TRUNC);
	if (!vf) return false;
	bool ok = mCoreSaveStateNamed(g->core, vf, SAVESTATE_ALL);
	vf->close(vf);
	return ok;
}

bool gbacore_load_state(GbaCore* g, int slot) {
	if (!g || !g->core || !g->rompath[0]) return false;
	char p[300];
	snprintf(p, sizeof p, "%s.ss%d", g->rompath, slot);
	struct VFile* vf = VFileOpen(p, O_RDONLY);
	if (!vf) return false;   // no state saved yet
	bool ok = mCoreLoadStateNamed(g->core, vf, SAVESTATE_ALL);
	vf->close(vf);
	return ok;
}

// ---- Link cable (mGBA SIO lockstep) ----------------------------------------
static void link_user_sleep(struct mLockstepUser* u) {
	struct LinkUser* l = (struct LinkUser*)u;
	if (l->onSleep) l->onSleep(l->ctx);   // request a wait; must not block here
}
static void link_user_wake(struct mLockstepUser* u) {
	struct LinkUser* l = (struct LinkUser*)u;
	if (l->onWake) l->onWake(l->ctx);     // signal the parked worker
}
static int link_user_requestedId(struct mLockstepUser* u) {
	return ((struct LinkUser*)u)->requestedId;
}
static void link_user_playerIdChanged(struct mLockstepUser* u, int id) {
	((struct LinkUser*)u)->playerId = id;
}

GbaLink* gbalink_create(void) {
	GbaLink* l = (GbaLink*)calloc(1, sizeof(*l));
	if (!l) return NULL;
	GBASIOLockstepCoordinatorInit(&l->coord);
	return l;
}

void gbalink_destroy(GbaLink* link) {
	if (!link) return;
	GBASIOLockstepCoordinatorDeinit(&link->coord);
	free(link);
}

void gbacore_link_attach(GbaCore* g, GbaLink* link, int requestedId,
                         void (*onSleep)(void*), void (*onWake)(void*), void* ctx) {
	if (!g || !g->core || !link || g->link) return;
	g->linkUser.u.sleep          = link_user_sleep;
	g->linkUser.u.wake           = link_user_wake;
	g->linkUser.u.requestedId    = link_user_requestedId;
	g->linkUser.u.playerIdChanged = link_user_playerIdChanged;
	g->linkUser.onSleep     = onSleep;
	g->linkUser.onWake      = onWake;
	g->linkUser.ctx         = ctx;
	g->linkUser.requestedId = requestedId;
	g->linkUser.playerId    = -1;
	GBASIOLockstepDriverCreate(&g->linkDriver, &g->linkUser.u);
	GBASIOLockstepCoordinatorAttach(&link->coord, &g->linkDriver);
	g->core->setPeripheral(g->core, mPERIPH_GBA_LINK_PORT, &g->linkDriver.d);
	g->link = link;
}

void gbacore_link_detach(GbaCore* g) {
	if (!g || !g->link) return;
	g->core->setPeripheral(g->core, mPERIPH_GBA_LINK_PORT, NULL);
	GBASIOLockstepCoordinatorDetach(&g->link->coord, &g->linkDriver);
	g->link = NULL;
}

// ---- Net link (M2.5: GBASIONetDriver) ---------------------------------------
// A from-scratch SIO driver that routes a GBA-MULTI transfer over netlink's transfer plane instead
// of mGBA's in-process lockstep coordinator — loopback-testable on one console (two local cores, no
// radio). mGBA's stock _sioFinish still does the SIOMULTI write + IRQ; we only feed it the words
// (Option A — never call GBASIOMultiplayerFinishTransfer directly off the core's timing wheel).
//
// netlink transfer plane (defined in netlink.c). Declared here with stdint types — ABI-identical to
// netlink.h's u16/u32/u64 — so this mGBA translation unit needn't pull in libctru's <3ds.h>.
void net_transfer_send_word(int seat, int mode, uint32_t round, uint16_t send);
bool net_transfer_collect(uint32_t round, int mode, uint16_t out[4], uint32_t needMask, uint64_t deadline_ms);
bool net_round_ready(uint32_t round, uint32_t needMask);
bool net_round_peek_word(uint32_t round, int seat, uint16_t* out);   // STATE E bridge: non-blocking 1-seat read
bool net_cmd_collect(int seat, uint32_t base, uint16_t out[8], uint64_t deadline_ms);  // STATE E: atomic 8-word command wait
bool net_seat_max_round(int seat, uint32_t* out);                    // STATE E: highest round with this seat's word (lead bound)
bool net_round_wait(uint32_t round, uint32_t needMask, uint64_t deadline_ms);  // joiner pacing barrier (blocks)
uint64_t net_mono_ticks(void);                 // libctru wall-clock tick (for the netlog dt_us column)
uint32_t net_ticks_to_us(uint64_t dticks);     // ticks -> microseconds
bool net_older_than_ms(uint64_t sinceTick, uint32_t ms);   // u64-safe wall-clock age test (pacing gate)
void net_link_get_stats(int* rxWordN, int* wordSendFails, int* busyN, int* peerUp, int* maxSeat0Round);  // establishment diag
void net_link_get_rtt(int* rttMs, int* drops);   // PURE-NETWORK ping round-trip (no game) — splits radio vs our per-round overhead
bool net_round_next_parent(uint32_t afterRound, uint32_t* outRound);   // M3: child adopts the parent's wire round
int  net_fprint_log(char* buf, int max);       // D6: the '# fprint' link-surface line (lobby-stage exchange)
// Celio EVENT channel (netlink.c): reliable, in-order semantic ClEvents (party/select/confirm) for state F.
int  net_event_send(int seat, const void* clEvent);
int  net_event_recv(int seat, void* clEventOut);
void net_event_reset(void);
void net_event_get_stats(int* txSeq, int* txAcked, int* rxDelivered, int* overflow, int* retransmits);

#define IO_SIOMLT_SEND  0x95          // gba->memory.io[] halfword index for SIOMLT_SEND (0x0400012A)
#define IO_IF           0x101         // GBA_REG_IF (0x0400_0202) >> 1 — interrupt-flag latch
#define IO_IE           0x100         // GBA_REG_IE (0x0400_0200) >> 1 — interrupt-enable mask
#define NET_CELIO_GATE_CEIL 60000u    // wedge-escape ceiling for the state-F edge gate (~3.6ms emulated)
#define SIO_IRQ_BIT     (1u << 7)     // GBA_IRQ_SIO == 7 (gba.h:32)
#define NET_DEADLINE_MS 2000          // ESTABLISHED link-lost timeout: re-send recovers routine loss within ms,
                                      // so this only fires if the peer is truly gone mid-trade (game errors cleanly).
#define NET_ESTABLISH_MS 100          // PRE-establishment poll period: before the first successful exchange the host
                                      // re-clocks the same round to poll for the slave; a SHORT deadline here means
                                      // each no-reply poll frees the host in ~100ms (polls ~10x/s) instead of a 2s
                                      // wall-clock freeze ("host stuck") while the joiner navigates to the cable club.
#define NET_ISR_GUARD_CYCLES 3000u    // local-clock floor before capture: > IRQ_DELAY(7)+ISR+DoSend; ~1 MULTI xfer
#define NET_ISR_GUARD_CEIL   (NET_ISR_GUARD_CYCLES * 4u)  // hard ceiling: capture on time alone (pure-poll game can't wedge)
// JOINER PACING (v2, active-gated on WALL-CLOCK). The UDS round-trip rate-limits the link far below the GBA's
// ~9 transfers/VBlank, so a FREE-running joiner outruns it and starves the Gen-3 SLAVE VBlank watchdog (link
// error mid-trade; 2x Emerald okN=261, dvbl 2-11). Fix: while ACTIVELY transferring, briefly BLOCK for the
// parent's next word — blocking freezes our emulated clock so few VBlanks pass without a serial IRQ (no trip),
// both sides run slow-but-synced. "Actively transferring" = a round was injected within the last NET_ACTIVE_MS
// of WALL-CLOCK time. CRITICAL: the gate MUST be wall-clock, NOT emulated frames — blocking freezes the
// emulated clock, so an emulated-frame gate would never age out and would re-freeze the joiner (the cable-club
// freeze bug). With no recent inject (navigation / pre-establishment / mid-trade pause) we FREE-RUN, so we
// never block waiting for a host that isn't clocking. NET_PACE_WAIT_MS > NET_ACTIVE_MS so a stalled link does
// at most one bounded wait then ages out to free-run (no hang).
#define NET_ACTIVE_MS     250u   // pace only if a round injected within this many ms of wall-clock
#define NET_PACE_WAIT_MS  500    // max paced wait for the parent's word during an active burst (else free-run)
// (NET_PACE_CAP_VBL free-run removed: it desynced the joiner ahead of the host -> earlier comm error.)
// EXPERIMENT HARNESS (live-toggled from the wireless HUD, KEY_Y). The ONLY thing each state changes is WHEN the
// joiner BLOCKS for the host's word — round-lock, capture-LAST, and adopt-on-host-word are identical in all
// states, so toggling mid-trade is always safe and never desyncs the word stream. The goal is to sweep the
// pacing dimension in ONE hardware run and read, per round (the `exp` log column), which strategy is faster /
// completes. A=baseline (proven: block per round, slow-but-synced); B=free-run (never block -> measures the host
// round-trip WITHOUT our barrier, the decisive radio-vs-pacing test; expected to diverge+error, but the rtt it
// yields is the datum); C=capped free-run (run up to NET_EXP_CAP_VBL emulated VBlanks, then block — a middle
// ground). Per-round `paceus` (joiner only) = wall-clock spent blocked at the barrier this round.
#define NET_EXP_CAP_VBL   3u     // state C: free-run up to this many emulated VBlanks past the last round, then block
// State D = HOST-RATE FOLLOW. The comm error decoded to a HOST CHECKSUM error (LINK_STAT_ERROR_CHECKSUM): the
// frozen-XA joiner (~0fps) can't compute the correct word per round, so it sends STALE words and the host's
// running checksum fails. Fix: let the joiner free-run but bound its emulated-frame advance to track the host's
// rate — the host clocks ~NET_HOSTRATE_DIV MULTI transfers per emulated frame (baudGame=0 ⇒ ~9), and the link is
// round-locked, so the joiner's adopted-round count IS the host's transfer count. Target joiner frame-delta =
// (round - base) / DIV; free-run while at/under target (+slack) so it advances at ~the host's fps (walkable AND
// in-sync words), block when it would pull ahead (no divergence). Round-count proxy — NO transport change.
#define NET_HOSTRATE_DIV   9u    // host MULTI transfers per emulated frame (baudGame=0) -> rounds-per-joiner-frame
#define NET_HOSTRATE_SLACK 2u    // emulated-frame slack the joiner may lead the host before it blocks
// State E = BRIDGE (Celio-inspired; see docs/kb/wireless-bridge-design.md). The decoupling principle: never
// park the emulated clock for a full radio RTT. An IDLE round (the local game wrote SIOMLT_SEND==0, which
// dominates the handshake) completes INSTANTLY with the Gen-3 idle word 0x0000 for the absent peer seat —
// safe because both-idle is what a silent partner sends (link.c: an all-zero received command sets
// receivedNothing + is skipped). A REAL round (local word != 0) is a live, checksummed exchange where
// substituting idle/stale is desync-fatal (HANDOFF.md:58), so it keeps the PROVEN reliable rendezvous but
// with a SHORT bounded wait (NET_BRIDGE_WAIT_MS, ~one RTT) instead of the multi-second park: the round
// re-clocks if the genuine peer word hasn't landed, never fabricates one. v1 is correct-by-construction (no
// real word is ever substituted); the netlog `exp`/idle-vs-real split measures how much of the active burst
// is idle (the free speed-up) vs real (still RTT-bound) — which tells us whether ahead-shipping (v2) is needed.
#define NET_BRIDGE_WAIT_MS 30    // state E: bounded wait for a genuine peer COMMAND to assemble (else idle-substitute)
#define NET_BRIDGE_LEAD    16u   // state E: max rounds (2 commands) we free-run AHEAD of the peer's received
                                 // progress before stalling — bounds the lead inside the 32-deep ring + Gen-3's
                                 // ~50-deep link FIFO so the two emulated clocks never diverge (the 26df69a wall).
#define NET_BRIDGE_SAFE_VBL 6u   // state E SLAVE: free-run the emulated clock at most this many VBlanks between
                                 // serial transfers, THEN freeze (block for the next round) — keeps us safely
                                 // under the Gen-3 SLAVE watchdog (LAG_SLAVE trips at >10 VBlanks-without-IRQ;
                                 // hardware test 0628 hit vblMax=18-19 with no cap and the joiner errored out).
static volatile int s_netExp     = 0;   // 0=A 1=B 2=C 3=D 4=E(bridge) — live experiment state (set from the HUD)
static int          s_netExpSeen = 1;   // bitmask of states active during this run (header diag; A seen by default)
static uint32_t     s_netRoundPaceUs = 0;   // joiner: wall-clock us blocked at the barrier for the in-flight round
// TURNAROUND split (the 19ms-over-radio X-ray): per round, the role-specific PRODUCTION time. HOST = collect-done
// -> next net_start (the host emulating to its next transfer). JOINER = host-word-ready -> reply-sent (the joiner
// emulating to produce + ship its reply). Radio is ~5ms (the ping); these say how much of the ~24ms rtt is each
// console's per-round software/emulation turnaround (the speed lever if it dominates) vs the wire.
static uint32_t     s_netTurnUs = 0;          // role-specific turnaround for the in-flight round (us)
static uint64_t     s_netCollectDoneTick = 0; // HOST: tick the previous round's collect finished
static uint64_t     s_netReadyTick = 0;       // JOINER: tick the host's word for this round first became ready
static uint32_t     s_netHostRateBaseFrame = 0, s_netHostRateBaseRound = 0;  // state D: frame/round baseline
static bool         s_netHostRateValid = false;                             // ...armed at the first D-paced round
// (LINK-RATE THROTTLE removed: forcing the MULTI baud cut round-trips ~3x but the Gen-3 game reads SIOCNT back
// during connection-verify and rejects a baud mismatch — it broke the link right after save. See net_wSIOCNT.)

static volatile uint32_t s_netRound = 0;   // shared per-link round; single writer = the parent seat
static int s_netStartN = 0, s_netInjectN = 0, s_netOkN = 0, s_netToN = 0;   // M2.5 on-device diagnostics
static int s_netEdgeN = 0, s_netForceN = 0;   // M2.5: captures via the ISR-ran edge vs via the time-ceiling
static uint16_t s_netPWord = 0, s_netCWord = 0;   // last word the parent / child actually sent (word-dump diag)
// M3: RECEIVED peer words from the most recent collect. rxP = slot 0 (the seat-0/parent word THIS console
// received), rxC = slot 1. (Data phase is TRUE zero-loss now — no last-good cache, no substitution.)
static uint16_t s_netRxP = 0, s_netRxC = 0;
// M3 PEAK-WORD latch: the last NON-0000/non-FFFF word THIS console SENT / RECEIVED per seat. The live
// p/r words go 0000 idle after a stall, so a single photo can't show whether 8FFF (master handshake) and
// the block cmds (0xBBBB/0x8888) ever appeared. These high-water words prove how far the protocol got.
static uint16_t s_peakSentP = 0, s_peakSentC = 0;   // peak word the parent / child SENT
static uint16_t s_peakRxP  = 0, s_peakRxC  = 0;     // peak word THIS console RECEIVED for seat 0 / seat 1
static int      s_netStallO = -1;                   // o (okN) latched when a collect first MISSED; -1 = never
// JOINER PACING diag: peak emulated VBlanks the child free-ran past its last completed round (i.e. between
// serial IRQs). This is the DIRECT measure of the LAG hypothesis — if it stays < the SLAVE watchdog limit
// (>10) the divergence that tripped "communication error" is gone. s_netPaceBlkN = times the child blocked.
static uint32_t s_netVblMax = 0;
static int      s_netPaceBlkN = 0;
// STATE E (bridge) diag, at COMMAND (8-word) granularity: realCmds = peer commands delivered genuine
// (pipelined or assembled within the wait); idleCmds = peer commands the peer hadn't produced -> whole
// all-zero IDLE command substituted (receivedNothing/skip; recoverable); leadStalls = times we re-clocked
// because we'd run >NET_BRIDGE_LEAD ahead of the peer (bounded-lead, divergence guard). realCmds dominating
// with few idle/stall = a healthy fast bridge; many idleCmds mid-trade or rising leadStalls localizes trouble.
static int      s_netBridgeRealN = 0, s_netBridgeIdleN = 0, s_netBridgeStallN = 0;
static uint32_t s_netBridgeLeadMax = 0;   // peak observed lead (our round - peer max) — watch vs NET_BRIDGE_LEAD/FIFO
// M3 on-device LINK LOG: a ring of the last NETLOG_N completed transfers (round + both seats' exchanged
// words + ok/timeout), dumped to SD on link stop. Diff the HOST file against the JOIN file by round to
// pinpoint the exact round where the two consoles' word streams diverge (the block-transfer checksum
// break). Precise, offline, role-labelled — replaces squinting at HUD photos/video.
#define NETLOG_N 1024
// Per completed transfer: the round id, both seats' exchanged words, ok/timeout, AND the emulated VBlank
// (frame) counter at retire. `frame` is the rich datum the pacing work needs — the gap between consecutive
// rounds' `frame` IS the emulated VBlanks-between-serial-IRQs (the SLAVE-watchdog measure), per round, and
// host-vs-joiner frame deltas show whether the two emulated clocks stay phase-locked. Generally useful for
// any future link-timing debugging, not just this fix.
typedef struct { uint32_t round; uint32_t frame; uint64_t tick; uint32_t rtt; uint32_t paceus; uint32_t turnus; uint16_t w0, w1; uint8_t ok; uint8_t exp; uint8_t sub; } NetLogEntry;
static NetLogEntry s_netLog[NETLOG_N];
static uint32_t    s_netLogN = 0;   // total appended (ring index = % NETLOG_N)
static uint32_t    s_netLastLogRound = 0; static bool s_netHaveLastLog = false;  // dedup repeated same-round timeout rows
static bool        s_netEstablished = false;   // a real exchange has happened -> use the long loss-recovery deadline
static uint64_t    s_netLastInjectTick = 0;    // wall-clock tick of the joiner's last inject (0 = none) -> pacing gate
static int         s_netBaudSeen = -1;         // the game's intended MULTI baud (before we throttle it); diag
static uint64_t    s_roundSendTick = 0;        // host: wall-clock tick the current round's word was first sent (rtt X-ray)
static int         s_netFinishN = 0;           // times net_finishMulti was ENTERED (completeEvent fired) — joiner-stall X-ray

static bool     net_init   (struct GBASIODriver* d) { (void)d; return true; }
static void     net_deinit (struct GBASIODriver* d) { (void)d; }
static void     net_reset  (struct GBASIODriver* d) {
	struct NetDriver* nd = (struct NetDriver*)d;
	nd->pendingRound = 0;
	nd->roundOpen = false;
	nd->phase = NET_IDLE;
	nd->isrWaitRound = 0xFFFFFFFFu;
	nd->irqArmTime = 0;
	nd->peerCmdIndex = 0xFFFFFFFFu;   // STATE E: no peer command cached yet (0 is a valid index, so use the sentinel)
}
static uint32_t net_id     (const struct GBASIODriver* d) { (void)d; return 0x54454E47u; /* 'GNET' */ }
static bool     net_load   (struct GBASIODriver* d, const void* s, size_t n) { (void)d;(void)s;(void)n; return true; }
static void     net_save   (struct GBASIODriver* d, void** s, size_t* n) { (void)d; if (s) *s = NULL; if (n) *n = 0; }
static void     net_setMode(struct GBASIODriver* d, enum GBASIOMode m) {
	// Seed the MULTI "ready" handshake the instant the game enters MULTI (before its next SIOCNT read),
	// mirroring lockstep's _setReady at the mode transition — else the game polls SIOCNT in MULTI, sees
	// Ready==0, and never starts a transfer.
	if (m == GBA_SIO_MULTI) {
		struct GBASIO* sio = d->p;
		sio->siocnt = GBASIOMultiplayerSetReady(sio->siocnt, 1);   // (baud-throttle reverted — see net_wSIOCNT)
		sio->rcnt   = GBASIORegisterRCNTSetSd(sio->rcnt, 1);
		// PACING: baseline the child's VBlank-cap clock at MULTI ENTRY (not at attach). The walk to the
		// trade room advances frameCounter by hundreds of VBlanks while lastActiveFrame still holds the
		// attach value; without this rebaseline the first inter-IRQ gap measured in gbacore_net_poll would
		// latch s_netVblMax (the headline 'V'/vblMax pacing metric) to that huge stale delta. The block
		// DECISION is unaffected (a huge gap blocks either way) — this only keeps the diagnostic honest.
		struct NetDriver* nd = (struct NetDriver*)d;
		if (nd->seat != 0) nd->lastActiveFrame = (uint32_t)((struct GBA*)d->p->p)->video.frameCounter;
	}
}
static bool     net_handles(struct GBASIODriver* d, enum GBASIOMode m) { (void)d; return m == GBA_SIO_MULTI; }
static int      net_devices(struct GBASIODriver* d) { return ((struct NetDriver*)d)->peers; }
static int      net_devId  (struct GBASIODriver* d) { return ((struct NetDriver*)d)->seat; }
// Assert the MULTI "all players ready" bit — the game polls it before writing Busy to start a transfer.
// In a 2-seat loopback both seats are always present + agree on MULTI, so Ready is unconditional. sio.c
// routes every SIOCNT write through here when we handle the mode, SKIPPING mGBA's own FillReady fallback,
// so WE must supply Ready (else the game waits forever — the cause of "no response").
static uint16_t net_wSIOCNT(struct GBASIODriver* d, uint16_t v) {
	if (d->p->mode != GBA_SIO_MULTI) return v;
	// Do NOT modify the baud bits. Forcing the baud to throttle the link rate (commits 0407491/431a63e) BROKE
	// the Gen-3 connection-verify-after-save step: the game writes its baud then reads SIOCNT back, and a
	// mismatch makes it reject the link (2x Emerald stuck right after save, very laggy; 0616 23:47, okN=1 then
	// timeouts). REVERTED. We only CAPTURE the game's baud for diagnostics and assert Ready (as before).
	s_netBaudSeen = (int)GBASIOMultiplayerGetBaud(v);   // diag only (no longer forced)
	return GBASIOMultiplayerSetReady(v, 1);
}
static uint16_t net_wRCNT(struct GBASIODriver* d, uint16_t v) {
	return (d->p->mode == GBA_SIO_MULTI) ? GBASIORegisterRCNTSetSd(v, 1) : v;   // assert the SD connected line
}

// PARENT (seat 0) only: push our word and let sio.c schedule our completeEvent (return true).
// A secondary must never self-start a MULTI it isn't the clock-owner of (return false).
static bool net_start(struct GBASIODriver* d) {
	struct NetDriver* nd = (struct NetDriver*)d;
	if (nd->seat != 0) return false;
	struct GBA* gba = d->p->p;
	uint16_t w = gba->memory.io[IO_SIOMLT_SEND];
	s_netPWord = w;                                  // diag: last word the parent sent
	if (w && w != 0xFFFF) s_peakSentP = w;           // PEAK: latch last non-idle word the parent SENT
	// ONE wire round per COMPLETED transfer. A Busy edge that arrives before net_finishMulti retired the
	// current round must NOT mint a fresh round — that abandoned rounds R/R+1 and made the host collect
	// R+2 while the child replied for R (the manufactured to≈9 + checksum-poisoning misalignment). On a
	// re-Busy we re-send OUR word for the SAME open round (net_round_merge overwrites our own seat-0 slot
	// idempotently); the round is retired exactly once, in net_finishMulti, after the collect succeeds. So
	// pendingRound is the single wire identity both seats agree on (the child adopts the lowest unstamped
	// parent round). The word-exchange RENDEZVOUS still lives in net_finishMulti's collect (both seats).
	uint32_t round;
	if (nd->roundOpen) {
		round = nd->pendingRound;                    // refresh OUR word for the in-flight round
	} else {
		round = __atomic_load_n(&s_netRound, __ATOMIC_ACQUIRE);
		nd->pendingRound = round;
		nd->roundOpen    = true;                     // closed by net_finishMulti on a successful collect
		s_roundSendTick  = net_mono_ticks();         // LATENCY X-RAY: stamp when the host first sent this round's word
		s_netTurnUs = s_netCollectDoneTick ? net_ticks_to_us(s_roundSendTick - s_netCollectDoneTick) : 0;  // HOST emulate-to-next-transfer
	}
	if (s_netExp != 5) net_transfer_send_word(0, GBA_SIO_MULTI, round, w);   // F (Celio): partner is local, no wire word
	s_netStartN++;
	return true;
}

// STATE E (bridge) fill — PER-WORD, in order, free-running. Resolve nd->pendingRound's words without the
// multi-second park. The Gen-3 link has a SINGLE-WORD handshake (e.g. B9A0 repeated) before any 8-word
// command, so we must NOT pre-frame into fixed 8-word units (that was the bug: it assembled an idle
// "command" before the real words arrived and cached it forever -> realCmds=0, the handshake never
// crossed). Instead we deliver the peer's GENUINE word for THIS round, in order, and PACE to the peer with
// a real blocking wait — never a fabricated 0xFFFF. Fast path: the peer's word is already here (pipelined)
// -> zero wait. Else we wait via the proven reliable rendezvous (the RX thread re-sends so a dropped word
// still lands): a LONG wait when we've run ahead of the peer's progress (so the host can't outrun a slow
// joiner and evict its un-consumed words from the ring), a SHORT wait otherwise; on timeout we hand the
// game the Gen-3 idle word 0x0000 (receivedNothing -> the game retries) and advance. Always advances
// (free-run); pacing is the wait, not a fake word.
static bool net_bridge_fill(struct NetDriver* nd, uint16_t data[4]) {
	int me = nd->seat, peer = me ^ 1;                       // 2-player trade: the other seat
	uint32_t r = nd->pendingRound;

	// How far have WE run ahead of the peer's received progress? Used only to size the wait (pace to a
	// slow peer), never to fabricate a word. If the peer's progress isn't visible at all (no peer word in
	// the ring — peer not started yet, or fell so far behind its words were evicted), treat as far-ahead and
	// PACE to it: this is the 0628 divergence fix (once the joiner died the host raced alone to round 375).
	uint32_t peerMax = 0; bool farAhead = true;
	if (net_seat_max_round(peer, &peerMax)) {
		int32_t lead = (int32_t)(r - peerMax);
		if (lead > 0 && (uint32_t)lead > s_netBridgeLeadMax) s_netBridgeLeadMax = (uint32_t)lead;
		farAhead = lead > (int32_t)NET_BRIDGE_LEAD;
	}

	data[me] = 0; net_round_peek_word(r, me, &data[me]);    // our own word for this round (already merged locally)
	uint16_t peerW = 0;
	if (net_round_peek_word(r, peer, &peerW)) {             // FAST PATH: peer's genuine word already pipelined in
		nd->peerCmdReal = 1; s_netBridgeRealN++;
	} else {
		// Wait for the GENUINE word (the RX thread re-sends, so a dropped word still arrives). Long deadline
		// when we've outrun the peer (pace to it); short otherwise (pipelined => usually instant).
		uint64_t dl = farAhead ? (uint64_t)NET_DEADLINE_MS : (uint64_t)NET_BRIDGE_WAIT_MS;
		if (farAhead) s_netBridgeStallN++;
		if (net_transfer_collect(r, GBA_SIO_MULTI, data, nd->needMask, dl)) {
			nd->peerCmdReal = 1; s_netBridgeRealN++;
			return true;                                   // collect filled ALL of data[] with the genuine words
		}
		peerW = 0x0000;                                    // genuinely not ready -> Gen-3 idle (game retries); NEVER 0xFFFF
		nd->peerCmdReal = 0; s_netBridgeIdleN++;
	}
	data[peer] = peerW;
	data[2] = data[3] = 0xFFFF;                             // seats 2/3 absent in a 2-player link
	return true;                                            // FREE-RUN: advance; the atomic cache keeps it consistent
}

// STATE F (Celio local termination): the synthesized partner (nd->cl) answers EVERY SIO transfer locally at full
// speed; the radio carries only semantic ClEvents (party/select/confirm), never per-transfer words. The game thus
// never waits on the wire and is never fed a mid-stream idle (the state-E RECEIVED_NOTHING cause). Credit:
// github.com/Celio-Link (GPL-3.0). Design: docs/kb/celio/celio-local-termination-port.md.
// LOGGING ONLY: snapshot the Celio (state F) FSM status so the netlog header shows section/state progress.
static int s_celioSection = -1, s_celioState = -1, s_celioBlk = -1;
static int s_celioFrames = 0, s_celioPartyB = 0, s_celioTradeC = 0;
static int s_celioGateN = 0, s_celioSioMode = -1, s_celioSiocnt = 0;   // link-ready gate holds + SIO view
static int s_celioResetN = 0;            // full session restarts (long no-transfer gap = user retried)
static int s_celioForceN = 0;            // edge-gate wedge escapes (IE.SIO masked or ceiling) — should stay ~0
static int s_celioExitP = 0, s_celioSessEnd = 0;   // run-#12 room-exit progress (exit seen / close answered)
static int s_celioPCard = 0, s_celioIdReal = 0;    // peer's real trainer card cached / real identity served
// D3 per-frame CSV (SPEC-firmware-diag D3.2): the FULL ClStatus snapshot + the outgoing ClEvent
// queue depth, captured WORKER-side in net_celio_capture (LOGGING ONLY, same seam as the s_celio*
// ints above, which stay — the netlog header keeps using them). The render thread copies these out
// via gbacore_net_counters; single-word reads, cross-field tearing accepted (telemetry).
static ClStatus s_celioStatus;
static int      s_celioOutQ = 0;
static struct NetDriver* s_celioNdForTrace = NULL;   // the attached participant (for the trace dump)
#define NET_CELIO_RESTART_MS 3000        // no transfers for this long => the game left + restarted the club
                                         // (the legit 0x1144 trade-anim reopen gap can reach ~1.7s: fade 15f
                                         // + close + 60f settle + 30f master wait — keep well above it)
static void net_celio_capture(struct NetDriver* nd) {
	ClStatus cs; cl_get_status(&nd->cl, &cs);
	s_celioStatus = cs;   // D3: full FSM snapshot for the CSV columns (holds/select slots included)
	// D3: outgoing-event queue depth (outTail = producer, outHead = consumer; celiolink.c
	// cl_emit/cl_take_outgoing) — how many semantic events are queued but not yet shipped over UDS.
	s_celioOutQ = (nd->cl.outTail - nd->cl.outHead + CL_EVENT_QUEUE_DEPTH) % CL_EVENT_QUEUE_DEPTH;
	s_celioSection = cs.section; s_celioState = cs.state; s_celioBlk = cs.blockSeq;
	s_celioFrames = (int)cs.frameCount; s_celioPartyB = (int)cs.partnerPartyBytes; s_celioTradeC = cs.tradeComplete;
	s_celioExitP = cs.exitPending; s_celioSessEnd = cs.sessionEnded;
	s_celioPCard = cs.partnerHasCard; s_celioIdReal = cs.identityWasReal;
	struct GBASIO* csio = nd->d.p;
	if (csio) { s_celioSioMode = (int)csio->mode; s_celioSiocnt = (int)csio->siocnt; }
}
// A LONG gap without transfers means the game tore the link down and the user restarted the club
// conversation from the attendant (HW run #3: host exited the room to retry -> the partner FSM was
// stale in its old section -> the game stuck right before the door). Phase transitions (room ->
// trade machine) re-clock within ms, so a >=2s gap is unambiguous. Full restart, party preserved.
static void net_celio_gap_check(struct NetDriver* nd) {
	uint64_t now = net_mono_ticks();
	if (nd->lastXferTick && net_older_than_ms(nd->lastXferTick, NET_CELIO_RESTART_MS)) {
		cl_reset_session(&nd->cl);
		s_celioResetN++;
	}
	nd->lastXferTick = now;
}
static void net_celio_pump(struct NetDriver* nd) {
	ClEvent ev;
	while (cl_take_outgoing(&nd->cl, &ev)) net_event_send(nd->seat, &ev);   // ship our events to the peer over UDS
	while (net_event_recv(nd->seat ^ 1, &ev)) cl_put_incoming(&nd->cl, &ev); // poll the PEER's stream — events are
	                                                                         // keyed by the SENDER's seat (run #7:
	                                                                         // txAcked=4 / rxDelivered=0 = we polled
	                                                                         // our own empty stream)
}
static bool net_celio_fill(struct NetDriver* nd, uint16_t data[4]) {
	net_celio_gap_check(nd);
	if (nd->seat == 0) {                                 // MASTER: our game word was captured in net_start
		data[0] = s_netPWord;
		data[1] = cl_transfer(&nd->cl, s_netPWord);      // the synthesized partner reply (local, full-speed)
	} else {                                             // SLAVE: words computed post-ISR in net_poll_celio
		data[0] = nd->clPartnerWord;
		data[1] = nd->clMyWord;
	}
	data[2] = data[3] = 0xFFFF;                          // seats 2/3 absent in a 2-player trade
	net_celio_pump(nd);
	net_celio_capture(nd);                               // LOGGING ONLY
	return true;                                         // the local partner is never late
}

// Both seats: _sioFinish calls this to GET the agreed words; mGBA then writes SIOMULTI + raises IRQ.
// The CHILD's word for this round was ALREADY captured+sent at the START of the round in gbacore_net_poll,
// AFTER its prior-round SIO ISR was PROVEN to have run (post-ISR-armed; the VBA-M one-transfer latency).
// We must NOT re-read io[SIOMLT_SEND] here: _sioFinish calls us (sio.c:419) BEFORE this round's
// GBASIOMultiplayerFinishTransfer (sio.c:421) writes SIOMULTI / GBARaiseIRQ, so a read here is the
// one-transfer-stale word — exactly the bug. Both seats only RENDEZVOUS on collect (full needMask).
static void net_finishMulti(struct GBASIODriver* d, uint16_t data[4]) {
	struct NetDriver* nd = (struct NetDriver*)d;
	// D1 site 3000 ENTRY bracket: the worker parks INSIDE this call for the whole collect. A wedged
	// collect shows site 1000 in g_diagNetCrumb; this bracket says WHICH round (iter = pendingRound).
	// Cleared to 0 at function exit — sioCrumb pinned at 3000+r == currently parked in round r's collect.
	DIAG_CRUMB(g_diagSioCrumb, DIAG_SITE_SIO_FINISH, nd->pendingRound);
	s_netFinishN++;   // DIAG: net_finishMulti ENTERED (completeEvent fired). finishN==0 while edge>0 => the
	                  // completeEvent never fired (worker not running run_loop after inject); finishN>okN+toN
	                  // => entered but collect is BLOCKED (stuck waiting for a word). Pins the joiner round-0 stall.
	// PRE-establishment use a short poll deadline so a no-reply transfer frees the host in ~100ms (master
	// polling for a late slave) instead of a 2s wall-clock freeze; once a real exchange has happened, use the
	// long loss-recovery deadline. (collect freezes EMULATED time either way, so this only affects real-time feel.)
	uint64_t deadline = s_netEstablished ? NET_DEADLINE_MS : NET_ESTABLISH_MS;
	bool ok;
	if (s_netExp == 5) { ok = net_celio_fill(nd, data); }   // STATE F (Celio): answer locally, never touch the radio
	else ok = (s_netExp == 4)
	          ? net_bridge_fill(nd, data)   // STATE E (bridge): non-blocking idle-fast / bounded real-round wait
	          : net_transfer_collect(nd->pendingRound, GBA_SIO_MULTI, data, nd->needMask, deadline);   // (states A-E)
	if (ok) {
		s_netOkN++;                                 // diag: words converged (the reliable rendezvous resolved)
		s_netEstablished = true;                    // first real exchange -> switch to the long deadline
	} else {
		s_netToN++;                                 // diag: collect gave up -> GENUINE link loss (peer truly gone)
		if (s_netStallO < 0) s_netStallO = s_netOkN;   // PEAK diag: latch the o-value where progress first stalled
		// The collect re-sends our word + blocks to a long link-lost deadline, so a miss is NOT routine word
		// loss anymore — it means the peer is genuinely gone. Fill 0xFFFF so the game errors cleanly. We do
		// NOT retry the round (that delivered a SECOND, phantom transfer for the same round and poisoned the
		// Gen-3 checksum); the round is retired below exactly once, just like a success.
		memset(data, 0xFF, sizeof(uint16_t) * 4);
	}
	if (nd->seat == 0 && ok) {                      // ADVANCE THE WIRE ROUND ONLY ON A REAL EXCHANGE (both words in).
		__atomic_store_n(&s_netRound, nd->pendingRound + 1, __ATOMIC_RELEASE);
		nd->roundOpen = false;                      // success -> next Busy mints the next round
	}
	// On a TIMEOUT (ok==false) we deliberately do NOT advance s_netRound and KEEP roundOpen: the host re-clocks
	// the SAME round (a real GBA master polling an absent/late slave), and the RX re-send delivers a lost reply,
	// so the host and joiner stay LOCKED to one round number. The old "advance on every transfer" raced the host
	// ahead in round-number while the joiner was still navigating to the cable club -> the joiner (waiting on
	// round 0) and the host (already at round N) never met = the intermittent "round 0 never crosses" / host-
	// stuck establishment failure (predates the pacing barrier; see netlogs 0616). No-op on a healthy trade
	// (toN==0 there). A genuinely-gone peer keeps reading 0xFFFF -> the game's own link watchdog errors cleanly.
	s_netRxP = data[0];                            // diag: the seat-0 word THIS console received this round
	s_netRxC = data[1];                            // diag: the seat-1 word received
	if (data[0] && data[0] != 0xFFFF) s_peakRxP = data[0];   // PEAK: latch last non-idle received seat-0 word
	if (data[1] && data[1] != 0xFFFF) s_peakRxC = data[1];   // PEAK: ...and seat-1
	// LINK LOG: append this completed round (both seats' agreed words + ok/timeout + emulated VBlank + wall-clock
	// stamps). SKIP a repeated TIMEOUT row for the SAME round — the host now re-clocks round N while polling for a
	// late slave, so without this the ring floods with identical round-N/ok=0 rows and wraps, evicting the very
	// establishment moment the log exists to capture. A success or a NEW round always logs.
	bool dupTimeout = (!ok && s_netHaveLastLog && s_netLastLogRound == nd->pendingRound);
	if (!dupTimeout) {
		uint32_t fr = (uint32_t)((struct GBA*)nd->d.p->p)->video.frameCounter;   // == core->frameCounter
		// rtt = the ACTUAL host round-trip this round: from first-send (net_start) to this collect succeeding.
		// Compare to dt_us (wall-clock between rounds): rtt~=dt_us => the time IS the wire round-trip; rtt<<dt_us
		// => the latency is emulated processing/gap between rounds, NOT the wire. Host-only (joiner never sends in
		// net_start, so s_roundSendTick stays 0 -> rtt 0 there).
		uint32_t rtt = (nd->seat == 0 && s_roundSendTick) ? net_ticks_to_us(net_mono_ticks() - s_roundSendTick) : 0;
		s_netLog[s_netLogN % NETLOG_N].round  = nd->pendingRound;
		s_netLog[s_netLogN % NETLOG_N].frame  = fr;
		s_netLog[s_netLogN % NETLOG_N].rtt    = rtt;
		s_netLog[s_netLogN % NETLOG_N].paceus = s_netRoundPaceUs;   // joiner: wall-clock blocked at the barrier this round (0 on host)
		s_netLog[s_netLogN % NETLOG_N].turnus = s_netTurnUs;        // HOST emulate-to-next-send / JOINER reply-production (us)
		s_netLog[s_netLogN % NETLOG_N].tick   = net_mono_ticks();   // wall-clock -> dt_us (emulated-divergence vs UDS latency)
		s_netLog[s_netLogN % NETLOG_N].w0     = data[0];
		s_netLog[s_netLogN % NETLOG_N].w1     = data[1];
		s_netLog[s_netLogN % NETLOG_N].ok     = (uint8_t)ok;
		s_netLog[s_netLogN % NETLOG_N].exp    = (uint8_t)s_netExp;   // experiment state active when this round retired
		s_netLog[s_netLogN % NETLOG_N].sub    = (s_netExp == 4 && !nd->peerCmdReal) ? 1 : 0;   // STATE E: peer word was IDLE-substituted (suspect-for-desync)
		s_netLogN++;
		s_netLastLogRound = nd->pendingRound; s_netHaveLastLog = true;
	}
	s_netRoundPaceUs = 0; s_netTurnUs = 0;          // reset per-round accumulators
	s_netCollectDoneTick = net_mono_ticks();        // HOST: collect just finished -> base for the next emulate-to-send
	// RECEIVING-end bookkeeping (child only). GBASIOMultiplayerFinishTransfer (the very next _sioFinish call)
	// writes SIOMULTI, clears Busy, and GBARaiseIRQ schedules irqEvent at +7. Stamp the LOCAL clock now and
	// record that THIS round's ISR must complete before the next capture. Flip toward IDLE.
	if (nd->seat != 0) {
		struct GBA* gba = nd->d.p->p;
		nd->isrWaitRound = nd->pendingRound;        // the ISR about to be armed
		nd->irqArmTime   = (uint32_t)mTimingCurrentTime(&gba->timing);
		nd->phase        = NET_IDLE;                // ready to notice the next parent round
	}
	g_diagSioCrumb = 0;   // D1 site 3000 EXIT bracket (both seats): nonzero == currently inside
}

static uint8_t  net_finishN8 (struct GBASIODriver* d) { (void)d; return 0xFF; }
static uint32_t net_finishN32(struct GBASIODriver* d) { (void)d; return 0xFFFFFFFFu; }

void gbacore_net_attach(GbaCore* g, int seat, int peers) {
	if (!g || !g->core || g->link) return;          // never co-attach with the lockstep driver
	struct NetDriver* nd = &g->netDriver;
	memset(nd, 0, sizeof *nd);
	nd->seat  = seat;
	nd->peers = peers;
	nd->needMask = (1u << (peers + 1)) - 1u;         // seats 0..peers all required (0x3 for a 2-seat trade)
	nd->lastInjectedRound = 0xFFFFFFFFu;             // sentinel: nothing injected yet
	nd->roundOpen = false;                           // no round started yet (parent)
	nd->phase = NET_IDLE;
	nd->isrWaitRound = 0xFFFFFFFFu;
	nd->irqArmTime = 0;
	nd->lastActiveFrame = g->core->frameCounter(g->core);   // PACING baseline = current emulated VBlank count
	// State F (Celio): the synthesized partner takes the OPPOSITE role of the local game. HOST/seat-0 game is
	// the Gen-3 MASTER -> its partner is the SLAVE (CL_SLAVE); JOIN/seat-1 game is the slave -> partner = MASTER
	// (CL_MASTER). Verified vs celiolink.c handshake polarity: CL_MASTER drives 0x8FFF -> local game = slave.
	cl_init(&nd->cl, (seat == 0) ? CL_SLAVE : CL_MASTER, LINKTYPE_TRADE);
	s_celioNdForTrace = nd;
	nd->clMyWord = nd->clPartnerWord = 0;
	s_celioSection = s_celioState = s_celioBlk = -1;
	s_celioFrames = s_celioPartyB = s_celioTradeC = 0; s_celioGateN = 0; s_celioResetN = 0; s_celioForceN = 0; s_celioSioMode = -1; s_celioSiocnt = 0;
	cl_get_status(&nd->cl, &s_celioStatus);   // D3 (LOGGING ONLY): truthful baseline from the freshly
	s_celioOutQ = 0;                          // cl_init'd FSM (select slots -1, holds 0) for row 0
	net_event_reset();
	// DGBA_NET_EXP_DEFAULT (fingerprint.h) == 5: the SINGLE SOURCE OF TRUTH for the session-entry
	// link strategy, shared with the D6 fingerprint's modeFlags nibble so the value the peer is told
	// can never drift from the value we actually run (SPEC-suite-hardening.md §D6.1 field 5).
	s_netExp = DGBA_NET_EXP_DEFAULT; s_netExpSeen = (1 << DGBA_NET_EXP_DEFAULT); s_netRoundPaceUs = 0;   // DEFAULT = state F (Celio local termination); D/A/B/C/E via KEY_Y.    // DEFAULT = state D (host-rate-follow + edge-strict) — the
	                                                        // PROVEN trade recipe: every link uses it from round 0 (no
	                                                        // toggle, no pre-D force-captures, no mid-switch desync). A/B/C
	                                                        // stay reachable via the HUD's Y toggle as diagnostic fallbacks.
	s_netHostRateValid = false;                              // re-arm state D's host-rate baseline for this session
	nd->d.init = net_init;       nd->d.deinit = net_deinit;     nd->d.reset = net_reset;
	nd->d.driverId = net_id;     nd->d.loadState = net_load;    nd->d.saveState = net_save;
	nd->d.setMode = net_setMode; nd->d.handlesMode = net_handles;
	nd->d.connectedDevices = net_devices; nd->d.deviceId = net_devId;
	nd->d.writeSIOCNT = net_wSIOCNT;      nd->d.writeRCNT = net_wRCNT;
	nd->d.start = net_start;     nd->d.finishMultiplayer = net_finishMulti;
	nd->d.finishNormal8 = net_finishN8;   nd->d.finishNormal32 = net_finishN32;
	// Reset the on-device diag counters + link log on EVERY fresh attach (both consoles). A seat-0-only
	// reset left the JOINER (seat 1) showing stale counters/log from a prior session (e.g. startN=720).
	s_netStartN = s_netInjectN = s_netOkN = s_netToN = 0; s_netEdgeN = s_netForceN = 0;
	s_netRxP = s_netRxC = 0; s_peakSentP = s_peakSentC = s_peakRxP = s_peakRxC = 0; s_netStallO = -1; s_netLogN = 0;
	s_netVblMax = 0; s_netPaceBlkN = 0;   // PACING diag: peak emulated-VBlanks-between-IRQs + paced-wait count (active-gated)
	s_netBridgeRealN = s_netBridgeIdleN = s_netBridgeStallN = 0; s_netBridgeLeadMax = 0;   // STATE E bridge: cmd split + peak lead
	nd->peerCmdIndex = 0xFFFFFFFFu;       // STATE E: clear the per-driver peer-command cache for the fresh link
	s_netHaveLastLog = false; s_netLastLogRound = 0; s_netEstablished = false; s_netLastInjectTick = 0; s_netBaudSeen = -1; s_roundSendTick = 0; s_netFinishN = 0;   // fresh link
	if (seat == 0) s_netRound = 0;   // only the parent owns the shared per-link round counter
	g->core->setPeripheral(g->core, mPERIPH_GBA_LINK_PORT, &nd->d);
}

void gbacore_net_detach(GbaCore* g) {
	if (!g || !g->core) return;
	g->core->setPeripheral(g->core, mPERIPH_GBA_LINK_PORT, NULL);
}

// M2.5 on-device diagnostics: parent transfers started, child injects, collect ok/timeout, round,
// last words, and ISR-edge vs forced-by-ceiling captures (the mechanism-health surface).
void gbacore_net_diag(int* startN, int* injectN, int* okN, int* toN, unsigned* round,
                      unsigned* pWord, unsigned* cWord, int* edgeN, int* forceN,
                      unsigned* rxP, unsigned* rxC) {
	if (startN)  *startN  = s_netStartN;
	if (injectN) *injectN = s_netInjectN;
	if (okN)     *okN     = s_netOkN;
	if (toN)     *toN     = s_netToN;
	if (round)   *round   = (unsigned)s_netRound;
	if (pWord)   *pWord   = s_netPWord;
	if (cWord)   *cWord   = s_netCWord;
	if (edgeN)   *edgeN   = s_netEdgeN;
	if (forceN)  *forceN  = s_netForceN;
	if (rxP)     *rxP     = s_netRxP;   // seat-0 word THIS console RECEIVED (the master-handshake watch)
	if (rxC)     *rxC     = s_netRxC;   // seat-1 word received
}

// PEAK-WORD diagnostic: the high-water (last non-0000/non-FFFF) word each seat SENT and this console
// RECEIVED, plus the o (okN) value latched when a collect first MISSED (-1 = never). The live p/r words
// idle to 0000 after a stall; these prove how far the protocol got (8FFF master, then block cmds
// 0xBBBB/0x8888) from a SINGLE photo. On the HOST watch peakSentP; on the JOINER watch peakRxP.
void gbacore_net_peak(unsigned* peakSentP, unsigned* peakSentC,
                      unsigned* peakRxP, unsigned* peakRxC, int* stallO) {
	if (peakSentP) *peakSentP = s_peakSentP;
	if (peakSentC) *peakSentC = s_peakSentC;
	if (peakRxP)   *peakRxP   = s_peakRxP;
	if (peakRxC)   *peakRxC   = s_peakRxC;
	if (stallO)    *stallO    = s_netStallO;
}

// JOINER PACING diagnostic (HUD + log). vblMax = peak emulated VBlanks the child free-ran between serial
// IRQs (the LAG measure; must stay < the SLAVE watchdog's >10 limit). blkN = times the child blocked at the
// pacing barrier. capK = the configured VBlank cap. On the HOST these stay 0 (only the joiner paces).
void gbacore_net_pace(unsigned* vblMax, int* blkN, unsigned* capK) {
	if (vblMax) *vblMax = s_netVblMax;
	if (blkN)   *blkN   = s_netPaceBlkN;
	if (capK)   *capK   = NET_ACTIVE_MS;
}

// D3 per-frame CSV telemetry (SPEC-firmware-diag D3.2): the full read-only counters snapshot.
// NOTE (D3 fold, BUILDLOG 2026-08-03): slice D1 shipped a minimal `gbacore_net_wd_counters(gateN,
// forceN)` forward for the STUCK line and recorded "D3 may fold it into the full export" (D1
// deviation #3). Folded here — the D1 watchdog sampler now reads celioGateN/celioForceN out of
// this one snapshot (~200 ms cadence, a single struct fill), so there is exactly ONE read-only
// counters seam in gbacore.c instead of two.
// Pure copies of the statics above — NO logic change, NO lock. Called at ~60 Hz from the render
// thread; every field is a single aligned-word read (the gbacore_net_diag benign-race class);
// cross-field tearing between a worker write and this copy is accepted, disclosed telemetry
// (SPEC Open Q5 — a seqlock would add worker-side cost to the FROZEN path's logging call).
void gbacore_net_counters(GbaNetCounters* out) {
	if (!out) return;
	// celio FSM: the -1-sentinel ints (pre-capture truthful) + the full worker-captured ClStatus
	out->clSection    = s_celioSection;
	out->clState      = s_celioState;
	out->clBlk        = s_celioBlk;
	out->clFrames     = (unsigned)s_celioStatus.frameCount;
	out->clPartyBytes = (unsigned)s_celioStatus.partnerPartyBytes;
	out->clTradeC     = s_celioTradeC;
	out->clHeldParty  = s_celioStatus.partnerPartyHeld;   // the logical parks CSV exists to expose
	out->clHeldSel    = s_celioStatus.selectHeld;         // (SPEC D1.1: celiolink wedges are diagnosed
	out->clHeldConf   = s_celioStatus.confirmHeld;        //  by CSV hold columns, not crumbs)
	out->clSelLocal   = s_celioStatus.localSelectSlot;    // int8 -1 = not yet
	out->clSelPeer    = s_celioStatus.partnerSelectSlot;
	out->clExitP      = s_celioExitP;
	out->clSessEnd    = s_celioSessEnd;
	out->clPCard      = s_celioPCard;
	out->clIdReal     = s_celioIdReal;
	out->clOutQ       = s_celioOutQ;
	out->celioGateN   = s_celioGateN;
	out->celioForceN  = s_celioForceN;
	out->celioResetN  = s_celioResetN;
	out->celioSioMode = s_celioSioMode;
	out->celioSiocnt  = (unsigned)(uint16_t)s_celioSiocnt;
	// SIO driver
	out->startN   = s_netStartN;
	out->injN     = s_netInjectN;
	out->finN     = s_netFinishN;
	out->okN      = s_netOkN;
	out->toN      = s_netToN;
	out->edgeN    = s_netEdgeN;
	out->forceN   = s_netForceN;
	out->paceBlkN = s_netPaceBlkN;
	out->round    = (unsigned)s_netRound;
	out->vblMax   = (unsigned)s_netVblMax;
	out->pWord    = s_netPWord;
	out->cWord    = s_netCWord;
	out->rxP      = s_netRxP;
	out->rxC      = s_netRxC;
	out->peakSentP = s_peakSentP;
	out->peakSentC = s_peakSentC;
	out->peakRxP   = s_peakRxP;
	out->peakRxC   = s_peakRxC;
	// netlog ring tail: the last retired round's facts WITHOUT waiting for the dump. The worker
	// appends while we read — a torn in-progress entry is possible and accepted (telemetry).
	uint32_t n = s_netLogN;
	out->logN = (unsigned)n;
	if (n > 0) {
		const NetLogEntry* e = &s_netLog[(n - 1u) % NETLOG_N];
		out->lastRound = (unsigned)e->round;
		out->lastW0    = e->w0;
		out->lastW1    = e->w1;
		out->lastOk    = e->ok;
	} else {
		out->lastRound = 0; out->lastW0 = 0; out->lastW1 = 0;
		out->lastOk    = -1;   // "none" sentinel (SPEC D3.3 format rule)
	}
}

// EXPERIMENT STATE (live A/B/C pacing sweep). set_exp is called from the wireless HUD (KEY_Y); it records the
// state in s_netExpSeen so the netlog header shows which strategies a run exercised. Global (one net link), not
// per-core. Only the joiner's net_poll acts on it; on the host it is inert (the host never paces).
void gbacore_net_set_exp(int exp) {
	if (exp < 0 || exp > 4) return;             // 0=A 1=B 2=C 3=D 4=E(bridge)
	if (exp == 3 || exp == 4) s_netHostRateValid = false;   // re-arm state D/E's baseline on (re-)entry so a stale
	                                            // frame/round pair from a prior stint can't mis-pace (toggling A<->D/E)
	s_netExp = exp;
	s_netExpSeen |= (1 << exp);
}
int gbacore_net_get_exp(void) { return s_netExp; }

// Dump the M3 link log (the ring of completed transfers) to an SD text file. Called on wireless link
// stop. seat: 0 = HOST, 1 = JOIN (encoded in the header + the caller's filename). Both consoles' files
// are then diffed by round to find where the two word streams diverge (the checksum break). w0 = the
// seat-0/parent word, w1 = the seat-1/child word — identical on both consoles for a correct round.
void gbacore_net_log_dump(const char* path, int seat) {
	mkdir("sdmc:/cias", 0777);           // ensure the parent dir exists (ignored if already present)
	mkdir("sdmc:/cias/netlogs", 0777);   // ...and the dedicated netlogs folder (matches the local netlogs/ dir; drag-and-drop)
	FILE* f = fopen(path, "w");
	if (!f) return;
	fprintf(f, "# 3DGBA netlog role=%s seat=%d startN=%d injectN=%d finishN=%d okN=%d toN=%d edge=%d force=%d stallO=%d\n",
	        seat == 0 ? "HOST" : "JOIN", seat, s_netStartN, s_netInjectN, s_netFinishN, s_netOkN, s_netToN, s_netEdgeN, s_netForceN, s_netStallO);
	fprintf(f, "# peakSentP=%04X peakSentC=%04X peakRxP=%04X peakRxC=%04X\n",
	        s_peakSentP, s_peakSentC, s_peakRxP, s_peakRxC);
	// established=1 = round 0 crossed (past the handshake wall); established=0 + okN=0 = round-0-never-crosses.
	// PACING (active-gated, JOIN only): vblMax = peak emulated VBlanks the joiner ran between serial IRQs (must
	// stay < ~10 or the Gen-3 SLAVE watchdog trips mid-trade); paceN = paced waits during active transfers.
	// dvbl col below = emulated VBlanks since prev round (per-round watchdog measure); dt_us = wall-clock us.
	char expSeen[6] = {0}; int es = 0;   // which experiment states (A/B/C/D/E) were active during this run
	if (s_netExpSeen & 1)  expSeen[es++] = 'A';
	if (s_netExpSeen & 2)  expSeen[es++] = 'B';
	if (s_netExpSeen & 4)  expSeen[es++] = 'C';
	if (s_netExpSeen & 8)  expSeen[es++] = 'D';
	if (s_netExpSeen & 16) expSeen[es++] = 'E';
	fprintf(f, "# established=%d vblMax=%lu paceN=%d activeMs=%u baudGame=%d expSeen=%s capVbl=%u\n",
	        s_netEstablished ? 1 : 0, (unsigned long)s_netVblMax, s_netPaceBlkN, (unsigned)NET_ACTIVE_MS,
	        s_netBaudSeen, expSeen, (unsigned)NET_EXP_CAP_VBL);
	// STATE E (bridge) split, at COMMAND (8-word) granularity: realCmds = genuine peer commands delivered
	// (pipelined or assembled within the wait); idleCmds = peer hadn't produced the command -> a whole all-zero
	// IDLE command substituted (receivedNothing/skip; recoverable); leadStalls = re-clocks because we'd run
	// >lead ahead of the peer; leadMax = peak (our round - peer max). realCmds dominating with leadMax well under
	// the lead cap = a healthy fast bridge; idleCmds climbing mid-trade, or leadMax pinned at the cap, localizes
	// the trouble (correlate with the per-round w0/w1 + the link-error decode below to find the exact command).
	fprintf(f, "# bridge realCmds=%d idleCmds=%d leadStalls=%d leadMax=%lu leadCap=%u waitMs=%d\n",
	        s_netBridgeRealN, s_netBridgeIdleN, s_netBridgeStallN, (unsigned long)s_netBridgeLeadMax,
	        (unsigned)NET_BRIDGE_LEAD, (int)NET_BRIDGE_WAIT_MS);
	// TRANSPORT/ESTABLISHMENT diag: when round 0 never completes (okN=0, no rows below), THIS line says why.
	// JOIN: rxWords=0 => peer WORDs never arrived (host not TXing / peer unresolved); maxSeat0Round vs the HOST's
	// hostRound => round-number DESYNC (host raced past round 0 on timeout while we still wait on it). HOST:
	// txFails/busy => our sends failed. peerUp=0 => no unicast peer (link never really came up).
	{ int rxW=0, txF=0, busy=0, peerUp=0, maxS0=-1; net_link_get_stats(&rxW, &txF, &busy, &peerUp, &maxS0);
	  fprintf(f, "# transport rxWords=%d txFails=%d busy=%d peerUp=%d maxSeat0Round=%d hostRound=%lu\n",
	          rxW, txF, busy, peerUp, maxS0, (unsigned long)s_netRound); }
	// D6 LINK SURFACE (SPEC-suite-hardening.md §D6.6): what the two consoles told each other in the
	// LOBBY about whether they can link at all — game code + revision, our FSM's protocol rev, the
	// transport rev, and the session-entry link strategy. verdict=MATCH / DIFF:<named fields> /
	// unknown (the peer's surface never arrived — NEVER read that as a match). A `DIFF:` here is the
	// one-line diagnosis for a run that would otherwise be a mystery; none of it is refuse-grade
	// (EM<->FR and FR rev0<->rev1 trade legitimately — over-hashing would reject compatible peers,
	// gen1recomp #511). Absent entirely = no lobby exchange ran (e.g. a loopback/one-console run).
	{ char fpline[224];
	  if (net_fprint_log(fpline, (int)sizeof fpline) > 0) fprintf(f, "%s\n", fpline); }
	// PURE-NETWORK latency (the decisive split): the PING round-trip runs alongside the trade with NO game in the
	// loop. If pingRtt << the per-round rtt_us below, the ~21ms-over-floor is OUR per-round turnaround (worker
	// poll/emulate/reply), NOT the radio -> a router/socket won't help, but tightening the loop would. If
	// pingRtt ~= rtt_us, it's the radio/UDS scheduling -> a router/socket path could approach the ~3ms floor.
	{ int pr=-1, pd=0; net_link_get_rtt(&pr, &pd);
	  fprintf(f, "# ping pureNetRtt_ms=%d drops=%d  (vs the per-round rtt_us below: << => our overhead; ~= => the radio)\n", pr, pd); }
	// DATA-RATE summary (DEMAND vs SUPPLY — the system-limit question). Over the logged rounds: the emulated-frame
	// span is the GAME's demand (transfers/emu-frame ~= what a full-speed/in-console trade needs per frame, ~9 for
	// Gen-3 => ~9*60=540 transfers/s at 60fps); the wall-clock span is what the WIRELESS supplied. supply
	// transfers/s + payload B/s show the trade is LATENCY-bound (tiny bytes, but one synchronous round-trip per
	// 16-bit transfer); sustained emu-fps = the speed the link holds. demand/supply gap = the round-trip wall.
	{ uint32_t nn = (s_netLogN < NETLOG_N) ? s_netLogN : NETLOG_N;
	  uint32_t b0 = (s_netLogN < NETLOG_N) ? 0u : (s_netLogN % NETLOG_N);
	  if (nn >= 2) {
	    const NetLogEntry* e0 = &s_netLog[b0 % NETLOG_N];
	    const NetLogEntry* eN = &s_netLog[(b0 + nn - 1) % NETLOG_N];
	    uint32_t frameSpan = eN->frame - e0->frame;
	    uint32_t wallUs    = net_ticks_to_us(eN->tick - e0->tick);
	    float    wallSec   = wallUs / 1.0e6f;
	    float    rps       = wallSec > 0.0f ? nn / wallSec : 0.0f;
	    float    tpf       = frameSpan > 0 ? (float)nn / frameSpan : 0.0f;
	    float    fps       = wallSec > 0.0f ? frameSpan / wallSec : 0.0f;
	    fprintf(f, "# rate rounds=%lu wall_ms=%lu emuFrames=%lu | SUPPLY %.1f transfers/s (~%.0f payload B/s) | DEMAND %.1f transfers/emu-frame (~%.0f/s @60fps) | sustained %.2f emu-fps  [LATENCY-bound: 1 round-trip per 2-byte transfer]\n",
	            (unsigned long)nn, (unsigned long)(wallUs/1000u), (unsigned long)frameSpan, rps, rps*2.0f, tpf, tpf*60.0f, fps);
	  } }
	fprintf(f, "# celio section=%d state=%d blk=%d frames=%d partnerPartyB=%d tradeComplete=%d gateN=%d resetN=%d forceN=%d sioMode=%d siocnt=%04X exitP=%d sessEnd=%d pCard=%d idReal=%d  (state 0=handshake/1=crc/2=command; section 0=SETUP/1=CONNECTION/2=DISCONNECT/3=LOUNGE; partnerPartyB>0 => peer party arrived over UDS; exitP/sessEnd = the run-#12 room-exit close ran; pCard/idReal = the peer's real card/identity were cached/served)\n",
	        s_celioSection, s_celioState, s_celioBlk, s_celioFrames, s_celioPartyB, s_celioTradeC,
	        s_celioGateN, s_celioResetN, s_celioForceN, s_celioSioMode, s_celioSiocnt,
	        s_celioExitP, s_celioSessEnd, s_celioPCard, s_celioIdReal);
	// D4 file-driven movement summary (SPEC-control-replay.md D4.14). Pure copies of the
	// g_ctlStat mirror main.c publishes after every scheduler tick — this dump function never
	// touches the schedulers themselves. All-zero = no script ran this session (the normal case).
	fprintf(f, "# control p1 tok=%u abort=%u timeout=%u pick=%u st=%u(%d/%d) | p2 tok=%u abort=%u timeout=%u pick=%u st=%u(%d/%d)  (D4 sdmc:/cias/control move_p<N>.txt scripts; st 0=idle 1=waiting-for-go 2=running, (idx/nTok); the per-event detail is in 3DGBA_control_*.txt)\n",
	        g_ctlStat[0].toksDone, g_ctlStat[0].aborts, g_ctlStat[0].timeouts, g_ctlStat[0].pickups,
	        g_ctlStat[0].state, (int)g_ctlStat[0].idx, (int)g_ctlStat[0].nTok,
	        g_ctlStat[1].toksDone, g_ctlStat[1].aborts, g_ctlStat[1].timeouts, g_ctlStat[1].pickups,
	        g_ctlStat[1].state, (int)g_ctlStat[1].idx, (int)g_ctlStat[1].nTok);
	// D5 record/replay half of the same mirror (SPEC-control-replay.md D5). All-zero = neither
	// was armed this session. rec: 0=off 1=armed(hunting the field-entry anchor) 2=recording;
	// rep: 0=idle 1=armed(hunting the anchor) 2=playing, (idx/entries).
	fprintf(f, "# control-rr p1 rec=%u(%lu lines) rep=%u(%u/%u) | p2 rec=%u(%lu lines) rep=%u(%u/%u)  (D5 record_p<N>.txt arms recording -> 3DGBA_rec_p<N>_*.txt; replay_go_p<N>.txt loads replay_p<N>.txt)\n",
	        g_ctlStat[0].recState, (unsigned long)g_ctlStat[0].recLines,
	        g_ctlStat[0].repState, g_ctlStat[0].repIdx, g_ctlStat[0].repN,
	        g_ctlStat[1].recState, (unsigned long)g_ctlStat[1].recLines,
	        g_ctlStat[1].repState, g_ctlStat[1].repIdx, g_ctlStat[1].repN);
	if (s_celioNdForTrace) {
		uint32_t tf[32]; uint16_t tc[32]; uint8_t tsec[32], tst[32], tbl[32];
		int tn = cl_get_trace(&s_celioNdForTrace->cl, tf, tc, tsec, tst, tbl, 32);
		for (int ti = 0; ti < tn; ti++)
			fprintf(f, "# celio-trace f=%lu cmd=%04X sec=%u st=%u blk=%u\n",
			        (unsigned long)tf[ti], tc[ti], tsec[ti], tst[ti], tbl[ti]);
	}
	{ int ts=0, ta=0, rd=0, ov=0, rt=0; net_event_get_stats(&ts,&ta,&rd,&ov,&rt);
	  fprintf(f, "# event txSeq=%d txAcked=%d rxDelivered=%d overflow=%d retransmits=%d  (rxDelivered>0 => the peer's semantic events [party/select/confirm] crossed UDS)\n", ts,ta,rd,ov,rt); }
	fprintf(f, "# columns: idx,round,frame,dvbl,dt_us,rtt_us,paceus,turnus,exp,w0,w1,ok,sub   (dt_us=wall-clock between rounds; rtt_us=HOST round-trip [send->collect]; paceus=JOINER barrier-block this round; turnus=role TURNAROUND [HOST: collect-done->next-send = emulate-to-next-transfer; JOINER: host-word-ready->reply-sent = reply-production] — the 19ms-over-radio X-ray: big turnus => that console's per-round software/emulation is the wall [the speed lever]; small turnus on both => it's the wire/scheduling; w0=seat0 w1=seat1; ok=1/timeout=0; sub=1 => STATE-E peer word was IDLE-substituted [the prime desync suspect — diff HOST vs JOIN where sub differs])\n");
	fprintf(f, "idx,round,frame,dvbl,dt_us,rtt_us,paceus,turnus,exp,w0,w1,ok,sub\n");
	uint32_t n    = (s_netLogN < NETLOG_N) ? s_netLogN : NETLOG_N;
	uint32_t base = (s_netLogN < NETLOG_N) ? 0u : (s_netLogN % NETLOG_N);   // oldest retained entry
	uint32_t prevFrame = 0; uint64_t prevTick = 0; bool havePrev = false;
	for (uint32_t i = 0; i < n; i++) {
		const NetLogEntry* e = &s_netLog[(base + i) % NETLOG_N];
		uint32_t dvbl  = havePrev ? (e->frame - prevFrame) : 0;            // emulated VBlanks since the previous logged round
		uint32_t dt_us = havePrev ? net_ticks_to_us(e->tick - prevTick) : 0;  // wall-clock us since the previous logged round
		prevFrame = e->frame; prevTick = e->tick; havePrev = true;
		fprintf(f, "%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%c,%04X,%04X,%u,%u\n",
		        (unsigned long)i, (unsigned long)e->round, (unsigned long)e->frame,
		        (unsigned long)dvbl, (unsigned long)dt_us, (unsigned long)e->rtt,
		        (unsigned long)e->paceus, (unsigned long)e->turnus, (char)('A' + (e->exp <= 5 ? e->exp : 0)), e->w0, e->w1, e->ok, e->sub);
	}
	fclose(f);
}

// STATE F (Celio) child poll: NO wire rendezvous, NO pacing (the partner is local). Keep ONLY the edge-strict
// ISR gate (capture the post-ISR-armed word; never force a stale one) + self-clock. cl_transfer makes the partner
// reply locally; both words are cached for net_celio_fill. The slave free-runs at its game natural rate -- the
// partner answers every transfer so the SLAVE-watchdog never starves, and the trade synchronises at the
// semantic-event level, not per transfer (so there is no emulated-clock divergence to pace against).
static void net_poll_celio(struct NetDriver* nd, GbaCore* g) {
	struct GBASIO* sio = nd->d.p;
	struct GBA*    gba = sio->p;
	// LINK-READY GATE (HW run #2 fix): do NOT clock the slave until its game is LISTENING — SIO in
	// MULTI mode with the SIO IRQ enabled (SIOCNT bit 14). Run #2 self-clocked from link-start, so
	// thousands of transfers hit the game in the OVERWORLD and the partner burned its handshake against
	// a game that wasn't listening (JOIN logs: 8xB9A0 then 8FFF from round 0; the game never wrote a
	// word, peakSentC=0000). States A-D never had this: the first wire round only ever arrived once the
	// REAL master game was clocking (both consoles at the club). Restore that precondition — and on the
	// not-ready -> ready EDGE re-arm the partner handshake so it starts FRESH when the game listens.
	if (sio->mode != GBA_SIO_MULTI || !(sio->siocnt & 0x4000)) {
		s_celioGateN++;
		nd->gateHeldN++;
		DIAG_CRUMB(g_diagSioCrumb, DIAG_SITE_SIO_CELIO_GATE, nd->gateHeldN);   // D1 site 2000: link-ready gate held
		nd->isrWaitRound = 0xFFFFFFFFu;                    // no stale ISR wait across the gap
		return;
	}
	if (nd->gateHeldN) {
		// Ready again. Re-arm the handshake ONLY if the gate held long enough to be a real link
		// teardown (a fresh OpenLink always re-handshakes). A transient SIOCNT write mid-room
		// (a few gated polls) must NOT restart the handshake against a game mid-conversation
		// (audit driver-audit.md 5.4: the re-arm ping-pong hazard). Polls are per-slice (~73us
		// emulated); 64 polls ~= 5ms — far above any single-write transient, far below a teardown.
		if (nd->gateHeldN >= 64) {
			nd->clkPaceValid = false;
			cl_rearm_handshake(&nd->cl);                   // fresh handshake at listen-start (partner
			                                               // party/select/confirm state preserved)
		}
		nd->gateHeldN = 0;
	}
	// CELIO MASTER-CLOCK PACING (packetLayer.hpp:51-53): ~30ms between handshake words, ~1.4ms between
	// a frame's words, ~13ms between frames — in EMULATED time, so the game's ISR + main-loop link task
	// run at the cadence the protocol was designed for (run #2 clocked at raw poll speed).
	uint32_t nowCyc = (uint32_t)mTimingCurrentTime(&gba->timing);
	if (nd->clkPaceValid && (int32_t)(nowCyc - nd->nextClockCyc) < 0) {
		DIAG_CRUMB(g_diagSioCrumb, DIAG_SITE_SIO_CELIO_PACE, 0);   // D1 site 2200: master-clock pacing gate
		return;                                                    // (benign/high-frequency; a stuck
	}                                                              // nextClockCyc would pin this value)
	uint32_t round = nd->lastInjectedRound + 1;
	if (nd->isrWaitRound != 0xFFFFFFFFu) {
		uint32_t now     = (uint32_t)mTimingCurrentTime(&gba->timing);
		uint32_t elapsed = now - nd->irqArmTime;
		bool acked   = (gba->memory.io[IO_IF] & SIO_IRQ_BIT) == 0;
		bool fired   = !mTimingIsScheduled(&gba->timing, &gba->irqEvent);
		bool guarded = elapsed >= NET_ISR_GUARD_CYCLES;
		// WEDGE ESCAPE (audit driver-audit.md 5.2, the run-#4 freeze shape): GBARaiseIRQ sets IF even
		// when IE.SIO is masked (gba.c:585), and a masked game never acks -> without an escape the
		// clock parks FOREVER: no transfers -> RECEIVED_NOTHING every frame -> callbacks skipped ->
		// a silent permanent freeze (even CB2_LinkError can't render). A REAL master clocks on its
		// schedule regardless of the slave's IRQ state — the edge gate is only a freshness courtesy.
		// Escape when the ISR CANNOT run (IE.SIO masked) or after a bounded ceiling; the captured
		// word is then whatever sits in SIOMLT_SEND — exactly what real hardware would clock out.
		bool cantRun = (gba->memory.io[IO_IE] & SIO_IRQ_BIT) == 0;
		bool ceiling = elapsed >= NET_CELIO_GATE_CEIL;
		if (!((acked && fired) && guarded)) {
			if (!((cantRun && guarded) || ceiling)) {         // no escape yet: re-poll (CPU advances)
				DIAG_CRUMB(g_diagSioCrumb, DIAG_SITE_SIO_CELIO_ISR, elapsed >> 10);   // D1 site 2100:
				return;                                       // the run-#4 silent-freeze site (iter ~= wait depth)
			}
			s_celioForceN++;                                  // escaped the wedge (diag; should stay ~0)
		} else {
			s_netEdgeN++;
		}
		nd->isrWaitRound = 0xFFFFFFFFu;
	} else {
		s_netEdgeN++;                                      // round 0: the pre-seeded io[SIOMLT_SEND] is correct
	}
	net_celio_gap_check(nd);
	uint16_t w = gba->memory.io[IO_SIOMLT_SEND];
	s_netCWord = w;
	if (w && w != 0xFFFF) s_peakSentC = w;
	nd->clMyWord      = w;
	nd->clPartnerWord = cl_transfer(&nd->cl, w);
	net_celio_capture(nd);                                 // LOGGING ONLY
	if (nd->clPartnerWord && nd->clPartnerWord != 0xFFFF) s_peakRxC = nd->clPartnerWord;
	sio->siocnt |= 0x80;                                   // Busy
	nd->pendingRound      = round;
	nd->lastInjectedRound = round;
	nd->phase             = NET_RECEIVING;
	nd->lastActiveFrame   = g->core->frameCounter(g->core);
	s_netInjectN++;
	int32_t cyc = GBASIOTransferCycles(GBA_SIO_MULTI, sio->siocnt, nd->peers);
	mTimingDeschedule(&gba->timing, &sio->completeEvent);
	mTimingSchedule(&gba->timing, &sio->completeEvent, cyc);
	nd->nextClockCyc = nowCyc + (uint32_t)cyc + cl_next_delay_us(&nd->cl) * 17u;   // ~16.78 cycles/us
	nd->clkPaceValid = true;
}

// CHILD-side per-slice hook (no-op for the parent). MUST run on this core's OWN worker thread (it
// schedules an event on the core's timing and reads ONLY this core's io[]/timing — never the peer, so
// it transplants to two consoles over UDS). Mirrors VBA-M gbaLink.cpp UpdateCableSocket's SENDING phase,
// reached only AFTER the prior round's RECEIVING-end IRQ fired AND the local clock advanced: we capture
// the FRESH (post-ISR-armed) reply for round R and ship it, then arm completion. This is the proven
// one-transfer latency (round R's reply is the value the ISR armed in response to round R-1).
void gbacore_net_poll(GbaCore* g) {
	if (!g || !g->core) return;
	struct NetDriver* nd = &g->netDriver;
	if (nd->seat == 0) return;                       // the parent captures+sends in net_start
	struct GBASIO* sio = nd->d.p;
	struct GBA*    gba = sio->p;

	// STRICTLY ONE round in flight. If a round was injected but net_finishMulti hasn't processed its completion
	// yet (phase != IDLE), RETURN so this worker's run_loop can fire that round's completeEvent + SIO IRQ before
	// we touch the next round. CRITICAL with pacing: the pace-block below sleeps ON THIS WORKER THREAD, so if we
	// reached it while a round were still in flight it would starve run_loop and the in-flight round's completion
	// would never fire — okN stalls and the joiner sticks at the handshake (0616 18:40: edge=529 injects but
	// okN=3 completed). Also prevents injecting round N+1 before round N completes. net_finishMulti -> phase=IDLE.
	if (nd->phase != NET_IDLE) {
		DIAG_CRUMB(g_diagSioCrumb, DIAG_SITE_SIO_ROUND_OPEN, 0);   // D1 site 2400: pinned here = the
		return;                                                    // round's completeEvent never fired
	}
	if (s_netExp == 5) { net_poll_celio(nd, g); return; }   // STATE F: local termination -- no wire rendezvous/pacing

	// M3 RELIABLE: process rounds STRICTLY in order (next == lastInjected+1) — NEVER skip a gap. UDS reorders
	// and drops, so a not-yet-arrived round must be WAITED for (the parent re-sends it via the collect loop),
	// not skipped: the Gen-3 trade is a checksummed lockstep where a skipped/duplicated/reordered round
	// corrupts the stream. The parent (FIX B) stamps exactly one sequential round per COMPLETED transfer, so
	// lastInjected+1 is always the next wire round. (sentinel 0xFFFFFFFF + 1 = round 0.)
	uint32_t round = nd->lastInjectedRound + 1;
	if (!net_round_ready(round, 1u << 0)) {
		// ACTIVE-GATED PACING (see NET_ACTIVE_MS note). Pace (briefly block for the parent's word) ONLY while
		// actively transferring — a round was injected within the last NET_ACTIVE_MS of WALL-CLOCK time. Blocking
		// freezes our emulated clock between rounds so the SLAVE watchdog's VBlanks-without-serial-IRQ stays low
		// (fixes the 2x-Emerald mid-trade "link error", okN=261, dvbl 2-11). When NOT actively transferring
		// (navigation / pre-establishment / a mid-trade pause) FREE-RUN — never freeze waiting for a host that
		// isn't clocking (that was the cable-club freeze). Wall-clock gate (not emulated frames): a block freezes
		// emulated time, so a frame gate would never age out and would re-freeze us.
		if (s_netLastInjectTick == 0 || net_older_than_ms(s_netLastInjectTick, NET_ACTIVE_MS)) {
			// NOT actively transferring (overworld navigation / pre-establishment / a mid-trade pause): FREE-RUN.
			// Also INVALIDATE state D's host-rate baseline here — while navigating, the joiner advances emulated
			// FRAMES with no rounds, so a baseline taken before/through navigation makes actDelta(frames) >>
			// expDelta(round/DIV) once the trade starts, and D would then block EVERY round (frozen joiner — the
			// default-D regression). Re-arming on idle makes D re-base at the START of each active burst (= what
			// toggling to D in the room did manually), so it tracks the host's rate from there.
			s_netHostRateValid = false;
			return;
		}
		// VBlank-CAP free-run was REVERTED (it felt "way faster" but desynced): letting the joiner run extra
		// VBlanks between rounds advanced its emulated clock ~1.9 frames/round vs the host's ~0.11, so the joiner
		// raced ~90s AHEAD -> the two game clocks diverged -> comm error SOONER (0617: 3068 rounds with the cap vs
		// 9785 without). Block per round so the joiner advances as little as possible between rounds = closest to
		// the host's rate = least divergence. Real speed must come from MORE ROUNDS/SEC (concurrent exchange),
		// not from free-running one side ahead of the other.
		uint32_t vbl = (uint32_t)(g->core->frameCounter(g->core) - nd->lastActiveFrame);
		if (vbl > s_netVblMax) s_netVblMax = vbl;                         // diag: peak emulated VBlanks between serial IRQs
		// EXPERIMENT (live-toggled, KEY_Y): the ONLY behavioural fork. A=block per round (proven baseline); B=never
		// block (free-run -> the host rtt this round excludes our barrier = the decisive radio-vs-pacing datum);
		// C=free-run until NET_EXP_CAP_VBL VBlanks, then block. Adoption stays gated on the host's word either way.
		int exp = s_netExp;
		if (exp == 4) {         // E (bridge): free-run the emulated clock for speed, but CAP it so the slave's
		                        // serial IRQ stays frequent enough to never trip the LAG_SLAVE watchdog (the
		                        // proven 0628 failure: free-running uncapped hit vblMax=19 and the joiner errored).
			if (vbl < NET_BRIDGE_SAFE_VBL) return;            // under the cap -> free-run (fast, no park)
			// at/over the cap -> fall through to the net_round_wait BLOCK below, which FREEZES the emulated clock
			// until the next host round (vbl resets), so we never accumulate >10 VBlanks without a serial IRQ.
		}
		if (exp == 1) return;                                  // B: free-run — never block
		if (exp == 2 && vbl < NET_EXP_CAP_VBL) return;         // C: free-run until the cap, then block
		if (exp == 3) {                                        // D: follow the host's frame rate (round/DIV)
			uint32_t fr = g->core->frameCounter(g->core);
			if (!s_netHostRateValid) { s_netHostRateBaseFrame = fr; s_netHostRateBaseRound = round; s_netHostRateValid = true; }
			uint32_t expDelta = (round - s_netHostRateBaseRound) / NET_HOSTRATE_DIV;   // host frames elapsed (proxy)
			uint32_t actDelta = fr - s_netHostRateBaseFrame;                            // joiner frames elapsed
			if (actDelta <= expDelta + NET_HOSTRATE_SLACK) return;   // at/under the host's pace -> free-run (walkable)
			// else: pulling ahead of the host -> fall through and BLOCK (stay in sync, no divergence/checksum break)
		}
		s_netPaceBlkN++;
		uint64_t pt0 = net_mono_ticks();
		bool got = net_round_wait(round, 1u << 0, NET_PACE_WAIT_MS);     // bounded wait; stalled link -> free-run, no hang
		s_netRoundPaceUs += net_ticks_to_us(net_mono_ticks() - pt0);     // diag: wall-clock blocked at the barrier this round
		if (!got) return;
		// word arrived -> fall through and inject
	}
	if (!s_netReadyTick) s_netReadyTick = net_mono_ticks();   // JOINER: host word is now available -> start timing reply-production

	// --- ISR-PROOF GATE: do not capture round R's reply until round (R-1)'s SIO ISR has run+acked. ---
	// All three signals are LOCAL (no peer access, no shared clock):
	//   acked  = the handler write-1-to-cleared IF.SIO (io.c:518-520) after GBARaiseIRQ set it (gba.c:585)
	//   fired  = irqEvent is no longer scheduled => the +7 IRQ was taken (gba.c:596-597)
	//   guarded= a local-clock floor so DoSend (which arms SIOMLT_SEND a few hundred cycles into the handler)
	//            has completed; this is VBA-M's "give the CPU a time window then read" on the LOCAL clock.
	// The (acked && fired) edge is value-agnostic — it fires even when the reply equals the prior word
	// (the identical-idle/handshake rounds that dominate a trade), which is why a value-change detector fails.
	// A hard ceiling (NET_ISR_GUARD_CEIL) captures on time alone so a pure-poll SIO game cannot wedge us.
	if (nd->isrWaitRound != 0xFFFFFFFFu) {
		uint32_t now     = (uint32_t)mTimingCurrentTime(&gba->timing);
		uint32_t elapsed = now - nd->irqArmTime;
		bool acked   = (gba->memory.io[IO_IF] & SIO_IRQ_BIT) == 0;
		bool fired   = !mTimingIsScheduled(&gba->timing, &gba->irqEvent);
		bool guarded = elapsed >= NET_ISR_GUARD_CYCLES;
		// State D EDGE-STRICT: disable the time-ceiling so we ALWAYS wait for the proven (acked && fired) ISR edge
		// and never force-capture a not-yet-armed (stale) word. Force-captures correlate with the host CHECKSUM
		// error (D run: force=185 -> errored at round 3824; force=0 runs survived 10000+); D free-runs more
		// emulated cycles between rounds, so it blew past the ceiling and grabbed stale words. Gen-3 is
		// interrupt-driven (the edge always comes; run_loop advances on each re-poll), so waiting can't wedge.
		bool ceiling = (s_netExp != 3) && (elapsed >= NET_ISR_GUARD_CEIL);
		if (!ceiling && !((acked && fired) && guarded)) {         // not yet — re-poll next slice (CPU advances)
			DIAG_CRUMB(g_diagSioCrumb, DIAG_SITE_SIO_ISR_GATE, elapsed >> 10);   // D1 site 2300: stale-word /
			return;                                               // edge-never-comes class (A-E states)
		}
		if (ceiling && !(acked && fired)) s_netForceN++;          // captured on the time floor, not a proven edge
		else                              s_netEdgeN++;           // captured behind the proven ISR-ran edge (good)
		nd->isrWaitRound = 0xFFFFFFFFu;                           // satisfied for this round
	} else {
		s_netEdgeN++;   // round 0 (no prior ISR): the pre-seeded io[SIOMLT_SEND] is the correct first word
	}

	// --- SENDING: capture the POST-ISR-armed reply and ship it for THIS round, then arm completion. ---
	uint16_t w = gba->memory.io[IO_SIOMLT_SEND];     // armed by the ISR we just proved ran (or round-0 pre-seed)
	s_netCWord = w;                                  // diag: the FRESH word the child sends (what 'c' shows)
	if (w && w != 0xFFFF) s_peakSentC = w;           // PEAK: latch last non-idle word the child SENT
	net_transfer_send_word(nd->seat, GBA_SIO_MULTI, round, w);
	s_netTurnUs = s_netReadyTick ? net_ticks_to_us(net_mono_ticks() - s_netReadyTick) : 0;  // JOINER reply-production us
	s_netReadyTick = 0;                              // re-arm for the next round

	sio->siocnt |= 0x80;                             // Busy: transfer in progress (lockstep.c:967)
	nd->pendingRound = round;
	nd->lastInjectedRound = round;
	nd->phase = NET_RECEIVING;                       // completion delivers+IRQs, then net_finishMulti flips to IDLE
	nd->lastActiveFrame = g->core->frameCounter(g->core);  // emulated-VBlank baseline for the dvbl/vblMax diag
	s_netLastInjectTick = net_mono_ticks();          // wall-clock of this inject -> the active-transfer pacing gate
	s_netInjectN++;                                  // diag: the child captured+armed a parent-initiated round
	int32_t cyc = GBASIOTransferCycles(GBA_SIO_MULTI, sio->siocnt, nd->peers);
	mTimingDeschedule(&gba->timing, &sio->completeEvent);
	mTimingSchedule(&gba->timing, &sio->completeEvent, cyc);
}

uint32_t gbacore_frame_counter(GbaCore* g) {
	return g->core->frameCounter(g->core);
}

// D2 hang catcher (SPEC-firmware-diag D2.4): read-only ARM CPU snapshot for the auto register
// dump. Struct paths verified against external/mgba/include (today's tree):
//   - struct mCore's FIRST member is `void* cpu` (core.h) -> `(struct ARMCore*)g->core->cpu`
//     (the documented seam; equivalently ((struct GBA*)g->core->board)->cpu, gba.h).
//   - struct ARMCore embeds ARM_REGISTER_FILE (arm.h): gprs[16] (13=SP 14=LR 15=PC) +
//     cpsr/spsr as union PSR (.packed int32).
//   - bankedRegisters[6][7] / bankedSPSRs[6] (arm.h); bank order = enum RegisterBank
//     (NONE,FIQ,IRQ,SVC,ABT,UND); slot 0 = that mode's r13, slot 1 = r14 (arm.c
//     ARMSetPrivilegeMode's swap; FIQ additionally banks r8-r12 in slots 2-6). The CURRENT
//     mode's live SP/LR are gprs[13..14] and its bank slots are STALE — dumped raw, offline
//     analysis interprets via cpsr's low 5 mode bits (diag.h CAVEAT).
//   - IE/IF/IME via bus reads of 0x4000200/0x4000202/0x4000208 (the melonDS hang-dump columns,
//     pm-bridge-forensics.md §12 EmuThread.cpp:525-528).
//   - stack: 32 words at (sp & ~3) - 32 .. +92 (8 below SP, 24 above — callee frames sit above),
//     only when SP points into EWRAM/IWRAM (0x02/0x03 — PM's "must point into main RAM" guard,
//     mp_bridge.cpp:1157); else stackValid=0 and the words stay zero.
// RACE DISCLOSURE: this runs on the RENDER thread while the wedged worker may still be
// executing — the registers are a sampled instant, not a stopped core (PM's samplers accept the
// same). For a truly parked worker (collect/gate wedge) the values are stable. Forensics, not a
// debugger. LOGGING ONLY: zero writes to the core or the game bus.
bool gbacore_dump_cpu(GbaCore* g, GbaCpuDump* out) {
	if (!out) return false;
	memset(out, 0, sizeof *out);
	if (!g || !g->core) return false;
	struct ARMCore* cpu = (struct ARMCore*)g->core->cpu;
	if (!cpu) return false;
	for (int i = 0; i < 16; i++) out->gprs[i] = (uint32_t)cpu->gprs[i];
	out->cpsr = (uint32_t)cpu->cpsr.packed;
	out->spsr = (uint32_t)cpu->spsr.packed;
	for (int b = 0; b < 6; b++) {
		out->bankedR13[b]  = (uint32_t)cpu->bankedRegisters[b][0];   // that mode's r13/SP
		out->bankedR14[b]  = (uint32_t)cpu->bankedRegisters[b][1];   // that mode's r14/LR
		out->bankedSPSR[b] = (uint32_t)cpu->bankedSPSRs[b];
	}
	out->ie  = gbacore_read16(g, 0x04000200u);
	out->if_ = gbacore_read16(g, 0x04000202u);
	out->ime = gbacore_read16(g, 0x04000208u);
	out->sp  = out->gprs[13];
	uint8_t region = (uint8_t)(out->sp >> 24);
	if (region == 0x02 || region == 0x03) {              // EWRAM/IWRAM only (PM main-RAM guard)
		out->stackValid = 1;
		uint32_t base = out->sp & ~3u;
		for (int i = 0; i < 32; i++)                     // stack[0]=sp-32 ... stack[8]=sp ... [31]=sp+92
			out->stack[i] = gbacore_read32(g, base + 4u * (uint32_t)i - 32u);
	}
	return true;
}

// ---- Live RAM access + game id (game-aware touch) --------------------------
uint8_t  gbacore_read8 (GbaCore* g, uint32_t a) { return (uint8_t)  g->core->busRead8 (g->core, a); }
uint16_t gbacore_read16(GbaCore* g, uint32_t a) { return (uint16_t) g->core->busRead16(g->core, a); }
uint32_t gbacore_read32(GbaCore* g, uint32_t a) { return           g->core->busRead32(g->core, a); }
// Write a byte to the running game's bus (used to set a menu cursor before injecting A). Safe only
// from the same thread that runs the core, or while that core's worker is parked (the main loop's
// per-frame handshake) — never during a link free-run.
void     gbacore_write8 (GbaCore* g, uint32_t a, uint8_t v)  { g->core->busWrite8 (g->core, a, v); }
void     gbacore_write16(GbaCore* g, uint32_t a, uint16_t v) { g->core->busWrite16(g->core, a, v); }

void gbacore_game_code(GbaCore* g, char out[5]) {
	for (int i = 0; i < 4; i++) out[i] = (char)g->core->busRead8(g->core, 0x080000ACu + i);
	out[4] = '\0';
}

// GBA cartridge header (GBATEK "GBA Cartridge Header"): 0xAC..0xAF = game code, 0xB0..0xB1 = maker
// code, 0xBC = SOFTWARE VERSION (the revision byte). Read-only, one bus byte — D6 fingerprint.
uint8_t gbacore_game_rev(GbaCore* g) {
	if (!g || !g->core) return 0;
	return (uint8_t)g->core->busRead8(g->core, 0x080000BCu);
}

void gbacore_destroy(GbaCore* g) {
	if (!g) return;
	if (g->core) {
		mCoreConfigDeinit(&g->core->config);
		g->core->deinit(g->core);
	}
	free(g->rom);
	free(g);
}
