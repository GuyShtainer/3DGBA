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
#include <mgba/gba/interface.h>            // mPERIPH_GBA_LINK_PORT
#include <mgba-util/vfs.h>
#include <mgba-util/audio-buffer.h>

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

struct GbaCore {
	struct mCore* core;
	void*         rom;            // dedicated buffer if we malloc'd one; NULL if using the boot buffer
	char          rompath[256];   // for deriving save-state paths

	struct GbaLink*             link;        // non-NULL while attached to a coordinator
	struct GBASIOLockstepDriver linkDriver;
	struct LinkUser             linkUser;
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
void     gbacore_write8(GbaCore* g, uint32_t a, uint8_t v) { g->core->busWrite8(g->core, a, v); }

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
