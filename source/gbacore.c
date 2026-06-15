// gbacore.c — the ONLY translation unit that includes mGBA headers.
// Keeps mGBA's u8/u16/etc. out of main.c (libctru). Built with the same defines
// libmgba.a was built with (see Makefile MGBA_DEFS / docs/kb/mgba-integration.md).

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>   // O_RDONLY / O_WRONLY / O_CREAT

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
struct NetDriver {
	struct GBASIODriver d;       // MUST be first — mGBA holds &d; we cast it back to NetDriver
	int      seat;               // our GBA playerId (0 = parent / clock owner)
	int      peers;              // number of OTHER GBAs (1 for a 2-seat trade)
	uint32_t needMask;           // bitmask of the seats required for a complete round
	uint32_t pendingRound;       // the round finishMultiplayer() must collect
	uint32_t lastInjectedRound;  // child: last round it self-scheduled (sentinel 0xFFFFFFFF = none)
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

#define IO_SIOMLT_SEND  0x95          // gba->memory.io[] halfword index for SIOMLT_SEND (0x0400012A)
#define NET_DEADLINE_MS 50            // per-transfer link-lost timeout (loopback returns far sooner)

static volatile uint32_t s_netRound = 0;   // shared per-link round; single writer = the parent seat
static int s_netStartN = 0, s_netInjectN = 0, s_netOkN = 0, s_netToN = 0;   // M2.5 on-device diagnostics
static uint16_t s_netPWord = 0, s_netCWord = 0;   // last word the parent / child actually sent (word-dump diag)

static bool     net_init   (struct GBASIODriver* d) { (void)d; return true; }
static void     net_deinit (struct GBASIODriver* d) { (void)d; }
static void     net_reset  (struct GBASIODriver* d) { ((struct NetDriver*)d)->pendingRound = 0; }
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
	return (d->p->mode == GBA_SIO_MULTI) ? GBASIOMultiplayerSetReady(v, 1) : v;
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
	uint32_t round = s_netRound;
	net_transfer_send_word(0, GBA_SIO_MULTI, round, w);
	nd->pendingRound = round;
	// Re-sync the cores at the transfer START — the analog of lockstep's WaitOnPlayers (lockstep.c:571).
	// BLOCK here until the child has injected its word for THIS round, so the parent never runs its
	// transfer timeline ahead of the peer. Without this the parent retried on stale ring data (the s>i
	// gap) and the trade failed its confirm-step validation. (Loopback: the child's worker is independent,
	// so it polls + injects while we wait; NET_DEADLINE_MS bounds a genuinely-absent peer.)
	uint16_t sync[4];
	if (net_transfer_collect(round, GBA_SIO_MULTI, sync, nd->needMask, NET_DEADLINE_MS))
		s_netRound = round + 1;                      // advance the round HERE (per successful start), NOT in
		                                             // finishMultiplayer: while blocked above the parent's timing
		                                             // wheel is frozen, so a finish-time advance can't fire ->
		                                             // s_netRound sticks and re-Busies re-read the child's STALE
		                                             // word (the 240-burst gap + corrupt trade). Fresh round/start.
	s_netStartN++;
	return true;
}

// Both seats: _sioFinish calls this to GET the agreed words; mGBA then writes SIOMULTI + raises IRQ.
static void net_finishMulti(struct GBASIODriver* d, uint16_t data[4]) {
	struct NetDriver* nd = (struct NetDriver*)d;
	// CHILD only: re-latch our outgoing word HERE, at the transfer-finish event, and re-merge it. mGBA
	// latches the secondary's SIOMLT_SEND AT the transfer event (lockstep.c:831/965) — after its CPU has
	// run forward to that point — whereas our inject-time latch (gbacore_net_poll) fires a beat too early,
	// before the game wrote the value it intends as this transfer's RESPONSE, so it can be STALE. The
	// inject-time send stays (it unblocks the parent + advances the round); this overwrites the slot with
	// the fresh word the game actually produced. (Parent's word is correct from its busy-write latch.)
	if (nd->seat != 0) {
		struct GBASIO* sio = nd->d.p;
		uint16_t w = sio->p->memory.io[IO_SIOMLT_SEND];
		s_netCWord = w;                             // diag: the FINAL word the child sent (what 'c' shows)
		net_transfer_send_word(nd->seat, GBA_SIO_MULTI, nd->pendingRound, w);
	}
	if (net_transfer_collect(nd->pendingRound, GBA_SIO_MULTI, data, nd->needMask, NET_DEADLINE_MS)) {
		s_netOkN++;                                 // diag: words converged (the round advance is in net_start now)
	} else {
		s_netToN++;                                 // diag: collect timed out (words never converged)
		memset(data, 0xFF, sizeof(uint16_t) * 4);   // timeout/link-lost: fail the transfer, keep the round
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
	nd->d.init = net_init;       nd->d.deinit = net_deinit;     nd->d.reset = net_reset;
	nd->d.driverId = net_id;     nd->d.loadState = net_load;    nd->d.saveState = net_save;
	nd->d.setMode = net_setMode; nd->d.handlesMode = net_handles;
	nd->d.connectedDevices = net_devices; nd->d.deviceId = net_devId;
	nd->d.writeSIOCNT = net_wSIOCNT;      nd->d.writeRCNT = net_wRCNT;
	nd->d.start = net_start;     nd->d.finishMultiplayer = net_finishMulti;
	nd->d.finishNormal8 = net_finishN8;   nd->d.finishNormal32 = net_finishN32;
	if (seat == 0) { s_netRound = 0; s_netStartN = s_netInjectN = s_netOkN = s_netToN = 0; }   // parent resets shared state
	g->core->setPeripheral(g->core, mPERIPH_GBA_LINK_PORT, &nd->d);
}

void gbacore_net_detach(GbaCore* g) {
	if (!g || !g->core) return;
	g->core->setPeripheral(g->core, mPERIPH_GBA_LINK_PORT, NULL);
}

// M2.5 on-device diagnostics: parent transfers started, child injects, collect ok/timeout, round.
void gbacore_net_diag(int* startN, int* injectN, int* okN, int* toN, unsigned* round, unsigned* pWord, unsigned* cWord) {
	if (startN)  *startN  = s_netStartN;
	if (injectN) *injectN = s_netInjectN;
	if (okN)     *okN     = s_netOkN;
	if (toN)     *toN     = s_netToN;
	if (round)   *round   = (unsigned)s_netRound;
	if (pWord)   *pWord   = s_netPWord;
	if (cWord)   *cWord   = s_netCWord;
}

// CHILD-side per-slice hook (no-op for the parent). MUST run on this core's OWN worker thread: it
// reads the core's io and schedules an event on the core's timing. Mirrors lockstep.c:964-970 —
// notice a parent-initiated round, latch our word, set Busy, self-schedule our completeEvent.
void gbacore_net_poll(GbaCore* g) {
	if (!g || !g->core) return;
	struct NetDriver* nd = &g->netDriver;
	if (nd->seat == 0) return;                       // the parent self-schedules in net_start
	uint32_t round = nd->lastInjectedRound + 1;      // inject rounds IN ORDER, no skips (sentinel+1 = round 0)
	if ((int32_t)(round - s_netRound) > 0) return;   // caught up — nothing new to inject yet
	if (!net_round_ready(round, 1u << 0)) return;    // parent's word for this round not in yet
	struct GBASIO* sio = nd->d.p;
	struct GBA*    gba = sio->p;
	uint16_t w = gba->memory.io[IO_SIOMLT_SEND];     // latch our outgoing word
	s_netCWord = w;                                  // diag: last word the child sent
	net_transfer_send_word(nd->seat, GBA_SIO_MULTI, round, w);
	sio->siocnt |= 0x80;                             // Busy: transfer in progress (lockstep.c:967)
	nd->pendingRound = round;
	nd->lastInjectedRound = round;
	s_netInjectN++;                                  // diag: the child injected a parent-initiated round
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
