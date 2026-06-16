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
#include <mgba/core/timing.h>              // mTimingSchedule / mTimingDeschedule
#include <mgba/gba/interface.h>            // mPERIPH_GBA_LINK_PORT
#include <mgba-util/vfs.h>
#include <mgba-util/audio-buffer.h>

#undef GBA_H   // mGBA's gba.h uses GBA_H as its include guard; gbacore.h reuses the name for the GBA
               // screen height (160). gba.h is already fully included above, so drop the guard macro
               // here to avoid a redefinition warning when gbacore.h defines GBA_H = 160.
#include "gbacore.h"

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
bool net_round_wait(uint32_t round, uint32_t needMask, uint64_t deadline_ms);  // joiner pacing barrier (blocks)
uint64_t net_mono_ticks(void);                 // libctru wall-clock tick (for the netlog dt_us column)
uint32_t net_ticks_to_us(uint64_t dticks);     // ticks -> microseconds
bool net_older_than_ms(uint64_t sinceTick, uint32_t ms);   // u64-safe wall-clock age test (pacing gate)
void net_link_get_stats(int* rxWordN, int* wordSendFails, int* busyN, int* peerUp, int* maxSeat0Round);  // establishment diag
bool net_round_next_parent(uint32_t afterRound, uint32_t* outRound);   // M3: child adopts the parent's wire round

#define IO_SIOMLT_SEND  0x95          // gba->memory.io[] halfword index for SIOMLT_SEND (0x0400012A)
#define IO_IF           0x101         // GBA_REG_IF (0x0400_0202) >> 1 — interrupt-flag latch
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
// LINK-RATE THROTTLE: force the MULTI baud so the game clocks fewer transfers/frame -> fewer UDS round-trips
// (each ~25ms) -> usable speed. mGBA GBASIOCyclesPerTransfer[baud][connected=1]: baud0=63427(~4/frame, BELOW
// the master watchdog's ~9 floor -> would trip LAG_MASTER), baud1=16241(~17/frame, SAFE), baud3=5755(~49/frame,
// the game's typical). baud 1 is the slowest safe rate = ~3x fewer round-trips than baud 3. Both consoles route
// SIOCNT writes through net_wSIOCNT so they share this baud and stay timing-consistent.
#define NET_LINK_BAUD     1u     // 38400 -> 16241 cyc -> ~17 MULTI transfers per emulated VBlank (> ~9 floor)

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
typedef struct { uint32_t round; uint32_t frame; uint64_t tick; uint16_t w0, w1; uint8_t ok; } NetLogEntry;
static NetLogEntry s_netLog[NETLOG_N];
static uint32_t    s_netLogN = 0;   // total appended (ring index = % NETLOG_N)
static uint32_t    s_netLastLogRound = 0; static bool s_netHaveLastLog = false;  // dedup repeated same-round timeout rows
static bool        s_netEstablished = false;   // a real exchange has happened -> use the long loss-recovery deadline
static uint64_t    s_netLastInjectTick = 0;    // wall-clock tick of the joiner's last inject (0 = none) -> pacing gate
static int         s_netBaudSeen = -1;         // the game's intended MULTI baud (before we throttle it); diag

static bool     net_init   (struct GBASIODriver* d) { (void)d; return true; }
static void     net_deinit (struct GBASIODriver* d) { (void)d; }
static void     net_reset  (struct GBASIODriver* d) {
	struct NetDriver* nd = (struct NetDriver*)d;
	nd->pendingRound = 0;
	nd->roundOpen = false;
	nd->phase = NET_IDLE;
	nd->isrWaitRound = 0xFFFFFFFFu;
	nd->irqArmTime = 0;
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
		sio->siocnt = GBASIOMultiplayerSetReady(sio->siocnt, 1);
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
	// THROTTLE the link rate (see NET_LINK_BAUD): force the MULTI baud so the game clocks ~17 transfers/VBlank
	// instead of ~49, cutting UDS round-trips ~3x for usable speed while staying above the master watchdog floor.
	// Both consoles route SIOCNT writes here, so they share the baud and stay timing-consistent (lockstep).
	s_netBaudSeen = (int)GBASIOMultiplayerGetBaud(v);   // diag: the game's intended baud (pre-throttle)
	v = GBASIOMultiplayerSetBaud(v, NET_LINK_BAUD);
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
	}
	net_transfer_send_word(0, GBA_SIO_MULTI, round, w);
	s_netStartN++;
	return true;
}

// Both seats: _sioFinish calls this to GET the agreed words; mGBA then writes SIOMULTI + raises IRQ.
// The CHILD's word for this round was ALREADY captured+sent at the START of the round in gbacore_net_poll,
// AFTER its prior-round SIO ISR was PROVEN to have run (post-ISR-armed; the VBA-M one-transfer latency).
// We must NOT re-read io[SIOMLT_SEND] here: _sioFinish calls us (sio.c:419) BEFORE this round's
// GBASIOMultiplayerFinishTransfer (sio.c:421) writes SIOMULTI / GBARaiseIRQ, so a read here is the
// one-transfer-stale word — exactly the bug. Both seats only RENDEZVOUS on collect (full needMask).
static void net_finishMulti(struct GBASIODriver* d, uint16_t data[4]) {
	struct NetDriver* nd = (struct NetDriver*)d;
	// PRE-establishment use a short poll deadline so a no-reply transfer frees the host in ~100ms (master
	// polling for a late slave) instead of a 2s wall-clock freeze; once a real exchange has happened, use the
	// long loss-recovery deadline. (collect freezes EMULATED time either way, so this only affects real-time feel.)
	uint64_t deadline = s_netEstablished ? NET_DEADLINE_MS : NET_ESTABLISH_MS;
	bool ok = net_transfer_collect(nd->pendingRound, GBA_SIO_MULTI, data, nd->needMask, deadline);
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
		s_netLog[s_netLogN % NETLOG_N].round = nd->pendingRound;
		s_netLog[s_netLogN % NETLOG_N].frame = fr;
		s_netLog[s_netLogN % NETLOG_N].tick  = net_mono_ticks();   // wall-clock -> dt_us (emulated-divergence vs UDS latency)
		s_netLog[s_netLogN % NETLOG_N].w0    = data[0];
		s_netLog[s_netLogN % NETLOG_N].w1    = data[1];
		s_netLog[s_netLogN % NETLOG_N].ok    = (uint8_t)ok;
		s_netLogN++;
		s_netLastLogRound = nd->pendingRound; s_netHaveLastLog = true;
	}
	// RECEIVING-end bookkeeping (child only). GBASIOMultiplayerFinishTransfer (the very next _sioFinish call)
	// writes SIOMULTI, clears Busy, and GBARaiseIRQ schedules irqEvent at +7. Stamp the LOCAL clock now and
	// record that THIS round's ISR must complete before the next capture. Flip toward IDLE.
	if (nd->seat != 0) {
		struct GBA* gba = nd->d.p->p;
		nd->isrWaitRound = nd->pendingRound;        // the ISR about to be armed
		nd->irqArmTime   = (uint32_t)mTimingCurrentTime(&gba->timing);
		nd->phase        = NET_IDLE;                // ready to notice the next parent round
	}
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
	s_netHaveLastLog = false; s_netLastLogRound = 0; s_netEstablished = false; s_netLastInjectTick = 0; s_netBaudSeen = -1;   // fresh link
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

// Dump the M3 link log (the ring of completed transfers) to an SD text file. Called on wireless link
// stop. seat: 0 = HOST, 1 = JOIN (encoded in the header + the caller's filename). Both consoles' files
// are then diffed by round to find where the two word streams diverge (the checksum break). w0 = the
// seat-0/parent word, w1 = the seat-1/child word — identical on both consoles for a correct round.
void gbacore_net_log_dump(const char* path, int seat) {
	mkdir("sdmc:/cias", 0777);           // ensure the parent dir exists (ignored if already present)
	mkdir("sdmc:/cias/netlogs", 0777);   // ...and the dedicated netlogs folder (matches the local netlogs/ dir; drag-and-drop)
	FILE* f = fopen(path, "w");
	if (!f) return;
	fprintf(f, "# 3DGBA netlog role=%s seat=%d startN=%d okN=%d toN=%d edge=%d force=%d stallO=%d\n",
	        seat == 0 ? "HOST" : "JOIN", seat, s_netStartN, s_netOkN, s_netToN, s_netEdgeN, s_netForceN, s_netStallO);
	fprintf(f, "# peakSentP=%04X peakSentC=%04X peakRxP=%04X peakRxC=%04X\n",
	        s_peakSentP, s_peakSentC, s_peakRxP, s_peakRxC);
	// established=1 = round 0 crossed (past the handshake wall); established=0 + okN=0 = round-0-never-crosses.
	// PACING (active-gated, JOIN only): vblMax = peak emulated VBlanks the joiner ran between serial IRQs (must
	// stay < ~10 or the Gen-3 SLAVE watchdog trips mid-trade); paceN = paced waits during active transfers.
	// dvbl col below = emulated VBlanks since prev round (per-round watchdog measure); dt_us = wall-clock us.
	fprintf(f, "# established=%d vblMax=%lu paceN=%d activeMs=%u baudGame=%d baudForced=%u\n",
	        s_netEstablished ? 1 : 0, (unsigned long)s_netVblMax, s_netPaceBlkN, (unsigned)NET_ACTIVE_MS,
	        s_netBaudSeen, (unsigned)NET_LINK_BAUD);
	// TRANSPORT/ESTABLISHMENT diag: when round 0 never completes (okN=0, no rows below), THIS line says why.
	// JOIN: rxWords=0 => peer WORDs never arrived (host not TXing / peer unresolved); maxSeat0Round vs the HOST's
	// hostRound => round-number DESYNC (host raced past round 0 on timeout while we still wait on it). HOST:
	// txFails/busy => our sends failed. peerUp=0 => no unicast peer (link never really came up).
	{ int rxW=0, txF=0, busy=0, peerUp=0, maxS0=-1; net_link_get_stats(&rxW, &txF, &busy, &peerUp, &maxS0);
	  fprintf(f, "# transport rxWords=%d txFails=%d busy=%d peerUp=%d maxSeat0Round=%d hostRound=%lu\n",
	          rxW, txF, busy, peerUp, maxS0, (unsigned long)s_netRound); }
	fprintf(f, "# columns: idx,round,frame,dvbl,dt_us,w0,w1,ok   (dvbl=emulated VBlanks since prev round; dt_us=WALL-CLOCK us since prev round -> emulated-divergence vs UDS air latency; w0=seat0/parent w1=seat1/child; ok=1/timeout=0)\n");
	fprintf(f, "idx,round,frame,dvbl,dt_us,w0,w1,ok\n");
	uint32_t n    = (s_netLogN < NETLOG_N) ? s_netLogN : NETLOG_N;
	uint32_t base = (s_netLogN < NETLOG_N) ? 0u : (s_netLogN % NETLOG_N);   // oldest retained entry
	uint32_t prevFrame = 0; uint64_t prevTick = 0; bool havePrev = false;
	for (uint32_t i = 0; i < n; i++) {
		const NetLogEntry* e = &s_netLog[(base + i) % NETLOG_N];
		uint32_t dvbl  = havePrev ? (e->frame - prevFrame) : 0;            // emulated VBlanks since the previous logged round
		uint32_t dt_us = havePrev ? net_ticks_to_us(e->tick - prevTick) : 0;  // wall-clock us since the previous logged round
		prevFrame = e->frame; prevTick = e->tick; havePrev = true;
		fprintf(f, "%lu,%lu,%lu,%lu,%lu,%04X,%04X,%u\n",
		        (unsigned long)i, (unsigned long)e->round, (unsigned long)e->frame,
		        (unsigned long)dvbl, (unsigned long)dt_us, e->w0, e->w1, e->ok);
	}
	fclose(f);
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
	if (nd->phase != NET_IDLE) return;

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
		if (s_netLastInjectTick == 0 || net_older_than_ms(s_netLastInjectTick, NET_ACTIVE_MS)) return;  // not active -> free-run
		uint32_t vbl = (uint32_t)(g->core->frameCounter(g->core) - nd->lastActiveFrame);
		if (vbl > s_netVblMax) s_netVblMax = vbl;                         // diag: peak emulated VBlanks between serial IRQs
		s_netPaceBlkN++;                                                  // diag: paced waits during active transfers
		if (!net_round_wait(round, 1u << 0, NET_PACE_WAIT_MS)) return;    // bounded wait; stalled link -> free-run, no hang
		// word arrived -> fall through and inject, paced to the link rate (slow-but-synced)
	}

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
		bool ceiling = elapsed >= NET_ISR_GUARD_CEIL;
		if (!ceiling && !((acked && fired) && guarded)) return;   // not yet — re-poll next slice (CPU advances)
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

void gbacore_destroy(GbaCore* g) {
	if (!g) return;
	if (g->core) {
		mCoreConfigDeinit(&g->core->config);
		g->core->deinit(g->core);
	}
	free(g->rom);
	free(g);
}
