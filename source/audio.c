// audio.c — see audio.h.
#include <3ds.h>
#include <malloc.h>
#include <string.h>
#include "audio.h"

#define AUDIO_FRAMES   1280   // stereo frames per wave buffer (matches mGBA's 3DS port)
#define AUDIO_DSP_BUFS 4

const char* const AUDIO_NAMES[3] = { "Solo", "Mixed", "Split" };

static ndspWaveBuf s_wbuf[AUDIO_DSP_BUFS];
static int16_t*    s_audioMem = NULL;
static int         s_bufId = 0;
static bool        s_hasSound = false;
static int16_t     s_mixA[AUDIO_FRAMES * 2];   // scratch for reading each core when mixing
static int16_t     s_mixB[AUDIO_FRAMES * 2];

static inline int16_t clamp16(int v) { return v < -32768 ? -32768 : (v > 32767 ? 32767 : v); }

static void audio_wbuf_reset(void) {   // (re)point the 4 wave buffers at their slices
	memset(s_wbuf, 0, sizeof(s_wbuf));
	for (int i = 0; i < AUDIO_DSP_BUFS; i++)
		s_wbuf[i].data_pcm16 = &s_audioMem[AUDIO_FRAMES * 2 * i];
	s_bufId = 0;
}

void audio_init(void) {
	if (R_FAILED(ndspInit())) return;   // no dspfirm.cdc (homebrew w/o it) -> stays silent
	s_hasSound = true;
	ndspSetOutputMode(NDSP_OUTPUT_STEREO);
	ndspSetOutputCount(1);
	ndspChnReset(0);
	ndspChnSetFormat(0, NDSP_FORMAT_STEREO_PCM16);
	ndspChnSetInterp(0, NDSP_INTERP_NONE);
	ndspChnSetPaused(0, false);
	ndspChnWaveBufClear(0);
	s_audioMem = (int16_t*)linearAlloc(AUDIO_FRAMES * AUDIO_DSP_BUFS * 2 * sizeof(int16_t));
	audio_wbuf_reset();
}

void audio_exit(void) {
	if (!s_hasSound) return;
	ndspChnWaveBufClear(0);
	ndspExit();
	if (s_audioMem) linearFree(s_audioMem);
	s_audioMem = NULL;
	s_hasSound = false;
}

bool audio_ready(void) { return s_hasSound; }

void audio_reset_stream(void) {
	if (!s_hasSound) return;
	ndspChnWaveBufClear(0);
	audio_wbuf_reset();
}

void audio_set_rate(GbaCore* core) {
	if (!s_hasSound || !core) return;
	ndspChnSetRate(0, (float)gbacore_ndsp_rate(core));
	audio_reset_stream();
}

static bool audio_wbuf_free(void) {
	return s_wbuf[s_bufId].status != NDSP_WBUF_QUEUED && s_wbuf[s_bufId].status != NDSP_WBUF_PLAYING;
}

static void audio_queue(size_t frames) {   // flush + submit the current wave buffer, advance ring
	s_wbuf[s_bufId].nsamples = frames;
	DSP_FlushDataCache(s_wbuf[s_bufId].data_pcm16, frames * 2 * sizeof(int16_t));
	ndspChnWaveBufAdd(0, &s_wbuf[s_bufId]);
	s_bufId = (s_bufId + 1) & (AUDIO_DSP_BUFS - 1);
}

void audio_feed(GbaCore* ca, GbaCore* cb, int focused, int mode, int volA, int volB) {
	if (!s_hasSound) return;

	// SOLO, or a fallback when only one core is present: play one, drain the other.
	if (mode == AUD_SOLO || !ca || !cb) {
		GbaCore* play = mode == AUD_SOLO ? (focused == 0 ? ca : cb) : (ca ? ca : cb);
		GbaCore* mute = mode == AUD_SOLO ? (focused == 0 ? cb : ca) : NULL;
		int vol = (play == ca) ? volA : volB;
		if (mute) gbacore_drain_audio(mute);
		if (!play) return;
		while (audio_wbuf_free()) {
			size_t avail = gbacore_audio_available(play);
			if (!avail) break;
			size_t m = avail < AUDIO_FRAMES ? avail : AUDIO_FRAMES;
			m = gbacore_read_audio(play, s_wbuf[s_bufId].data_pcm16, m);
			if (!m) break;
			if (vol != 256)
				for (size_t i = 0; i < m * 2; i++)
					s_wbuf[s_bufId].data_pcm16[i] = clamp16(s_wbuf[s_bufId].data_pcm16[i] * vol >> 8);
			audio_queue(m);
		}
		return;
	}

	// MIXED / SPLIT: read both cores in lockstep and combine.
	while (audio_wbuf_free()) {
		size_t availA = gbacore_audio_available(ca);
		size_t availB = gbacore_audio_available(cb);
		size_t m = availA < availB ? availA : availB;
		if (!m) break;
		if (m > AUDIO_FRAMES) m = AUDIO_FRAMES;
		gbacore_read_audio(ca, s_mixA, m);
		gbacore_read_audio(cb, s_mixB, m);
		int16_t* out = s_wbuf[s_bufId].data_pcm16;
		for (size_t i = 0; i < m; i++) {
			int aL = s_mixA[2*i], aR = s_mixA[2*i+1];
			int bL = s_mixB[2*i], bR = s_mixB[2*i+1];
			if (mode == AUD_MIXED) {
				out[2*i]   = clamp16((aL * volA + bL * volB) >> 8);
				out[2*i+1] = clamp16((aR * volA + bR * volB) >> 8);
			} else {   // AUD_SPLIT: game A -> left, game B -> right (each downmixed to mono)
				out[2*i]   = clamp16(((aL + aR) >> 1) * volA >> 8);
				out[2*i+1] = clamp16(((bL + bR) >> 1) * volB >> 8);
			}
		}
		audio_queue(m);
	}
	// Don't let the two cores drift apart if one produced more than the other.
	if (gbacore_audio_available(ca) > 2 * AUDIO_FRAMES) gbacore_drain_audio(ca);
	if (gbacore_audio_available(cb) > 2 * AUDIO_FRAMES) gbacore_drain_audio(cb);
}
