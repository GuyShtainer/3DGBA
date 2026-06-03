// gbacore.c — the ONLY translation unit that includes mGBA headers.
// Keeps mGBA's u8/u16/etc. out of main.c (libctru). Built with the same defines
// libmgba.a was built with (see Makefile MGBA_DEFS / docs/kb/mgba-integration.md).

#include <stdlib.h>
#include <fcntl.h>   // O_RDONLY

#include <mgba/core/core.h>
#include <mgba/core/config.h>
#include <mgba-util/vfs.h>
#include <mgba-util/audio-buffer.h>

#include "gbacore.h"

struct GbaCore {
	struct mCore* core;
};

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
	// Preload reads the whole ROM into a per-core malloc'd buffer. Required on 3DS
	// (no VFile mmap) and dual-core-safe (we built libmgba WITHOUT FIXED_ROM_BUFFER).
	if (!mCorePreloadVF(g->core, vf)) {
		vf->close(vf);            // close only on failure; on success the core owns it
		return false;
	}
	mCoreAutoloadSave(g->core);
	g->core->reset(g->core);
	return true;
}

void gbacore_set_keys(GbaCore* g, uint16_t keys) {
	g->core->setKeys(g->core, keys);
}

void gbacore_run_frame(GbaCore* g) {
	g->core->runFrame(g->core);
	// No audio path yet (v0.7) — drain the core's audio buffer so it can't back up.
	mAudioBufferClear(g->core->getAudioBuffer(g->core));
}

void gbacore_destroy(GbaCore* g) {
	if (!g) return;
	if (g->core) {
		mCoreConfigDeinit(&g->core->config);
		g->core->deinit(g->core);
	}
	free(g);
}
