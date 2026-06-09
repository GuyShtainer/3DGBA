// audio.c — see audio.h.
#include <3ds.h>
#include <malloc.h>
#include <string.h>
#include "audio.h"

#define AUDIO_FRAMES   1280   // stereo frames per ndsp wave buffer (matches mGBA's 3DS port)
#define AUDIO_DSP_BUFS 4
#define PUMP_CHUNK     AUDIO_FRAMES
#define RING_CAP       8192   // SPSC capacity in stereo frames (power of two); ~0.25s at 32kHz
#define RING_MASK      (RING_CAP - 1)
#define LATENCY_CAP    (3 * AUDIO_FRAMES)   // consumer trims a ring above this (caps audio latency)
#define AUDIO_STACK    (32 * 1024)

const char* const AUDIO_NAMES[3] = { "Solo", "Mixed", "Split" };

// ---- ndsp wave-buffer ring (touched only by the audio thread) --------------
static ndspWaveBuf s_wbuf[AUDIO_DSP_BUFS];
static int16_t*    s_audioMem = NULL;
static int         s_bufId = 0;
static bool        s_hasSound = false;
static int16_t     s_mixA[AUDIO_FRAMES * 2];   // scratch (audio thread only)
static int16_t     s_mixB[AUDIO_FRAMES * 2];

// ---- per-core lock-free SPSC ring: producer = that core's worker, consumer = audio thread ----
typedef struct {
	int16_t* buf;                 // RING_CAP * 2 int16 (interleaved stereo)
	volatile u32 head, tail;      // running frame indices; count = head - tail (u32 wrap-safe)
} Spsc;
static Spsc      s_ring[2];
static int16_t   s_pump[2][PUMP_CHUNK * 2];   // per-slot worker scratch (separate threads)
static bool      s_present[2];

// ---- audio-thread control (main writes, audio thread reads) ----------------
static Thread    s_thread = NULL;
static volatile bool s_quit = false;
static volatile bool s_resetReq = false;
static volatile bool s_rateReq = false;
static volatile bool s_muted = false;   // HARD mute: workers stop pumping, audio thread idles
static volatile float s_pendRate = 32768.0f;
static LightLock s_paramLock;
static int s_focused = 0, s_mode = AUD_SOLO, s_volA = 256, s_volB = 256;

static inline int16_t clamp16(int v) { return v < -32768 ? -32768 : (v > 32767 ? 32767 : v); }

// ---- SPSC ops --------------------------------------------------------------
static u32 spsc_avail(Spsc* r) {
	return __atomic_load_n(&r->head, __ATOMIC_ACQUIRE) - __atomic_load_n(&r->tail, __ATOMIC_ACQUIRE);
}
// producer: append `frames`, dropping any overflow (never blocks). Single writer of head.
static void spsc_write(Spsc* r, const int16_t* src, u32 frames) {
	u32 head = __atomic_load_n(&r->head, __ATOMIC_RELAXED);
	u32 tail = __atomic_load_n(&r->tail, __ATOMIC_ACQUIRE);
	u32 space = RING_CAP - (head - tail);
	if (frames > space) frames = space;
	for (u32 i = 0; i < frames; i++) {
		u32 idx = (head + i) & RING_MASK;
		r->buf[idx * 2] = src[i * 2]; r->buf[idx * 2 + 1] = src[i * 2 + 1];
	}
	__atomic_store_n(&r->head, head + frames, __ATOMIC_RELEASE);
}
// consumer: read up to `frames`. Single writer of tail.
static u32 spsc_read(Spsc* r, int16_t* dst, u32 frames) {
	u32 tail = __atomic_load_n(&r->tail, __ATOMIC_RELAXED);
	u32 head = __atomic_load_n(&r->head, __ATOMIC_ACQUIRE);
	u32 avail = head - tail;
	if (frames > avail) frames = avail;
	for (u32 i = 0; i < frames; i++) {
		u32 idx = (tail + i) & RING_MASK;
		dst[i * 2] = r->buf[idx * 2]; dst[i * 2 + 1] = r->buf[idx * 2 + 1];
	}
	__atomic_store_n(&r->tail, tail + frames, __ATOMIC_RELEASE);
	return frames;
}
// consumer: drop oldest down to `keep` frames (drift control). Single writer of tail.
static void spsc_trim(Spsc* r, u32 keep) {
	u32 tail = __atomic_load_n(&r->tail, __ATOMIC_RELAXED);
	u32 head = __atomic_load_n(&r->head, __ATOMIC_ACQUIRE);
	if (head - tail > keep) __atomic_store_n(&r->tail, head - keep, __ATOMIC_RELEASE);
}
static void spsc_clear(Spsc* r) {   // consumer-side reset
	__atomic_store_n(&r->tail, __atomic_load_n(&r->head, __ATOMIC_ACQUIRE), __ATOMIC_RELEASE);
}

// ---- ndsp wave-buffer plumbing (audio thread only) -------------------------
static void audio_wbuf_reset(void) {
	memset(s_wbuf, 0, sizeof(s_wbuf));
	for (int i = 0; i < AUDIO_DSP_BUFS; i++) s_wbuf[i].data_pcm16 = &s_audioMem[AUDIO_FRAMES * 2 * i];
	s_bufId = 0;
}
static bool audio_wbuf_free(void) {
	return s_wbuf[s_bufId].status != NDSP_WBUF_QUEUED && s_wbuf[s_bufId].status != NDSP_WBUF_PLAYING;
}
static void audio_queue(size_t frames) {
	s_wbuf[s_bufId].nsamples = frames;
	DSP_FlushDataCache(s_wbuf[s_bufId].data_pcm16, frames * 2 * sizeof(int16_t));
	ndspChnWaveBufAdd(0, &s_wbuf[s_bufId]);
	s_bufId = (s_bufId + 1) & (AUDIO_DSP_BUFS - 1);
}

// ---- the mix (audio thread): pull both rings per the live params, feed ndsp ----
static void audio_mix_fill(void) {
	LightLock_Lock(&s_paramLock);
	int focused = s_focused, mode = s_mode, volA = s_volA, volB = s_volB;
	LightLock_Unlock(&s_paramLock);

	Spsc* rA = &s_ring[0]; Spsc* rB = &s_ring[1];
	bool haveA = s_present[0], haveB = s_present[1];

	if (mode == AUD_SOLO || !haveA || !haveB) {
		Spsc* play = (mode == AUD_SOLO) ? (focused == 0 ? rA : rB) : (haveA ? rA : rB);
		Spsc* mute = (mode == AUD_SOLO) ? (focused == 0 ? rB : rA) : NULL;
		int vol = (play == rA) ? volA : volB;
		if (mute) spsc_trim(mute, 0);            // discard the silenced core's audio
		while (audio_wbuf_free()) {
			u32 avail = spsc_avail(play);
			if (!avail) break;
			u32 m = avail < AUDIO_FRAMES ? avail : AUDIO_FRAMES;
			m = spsc_read(play, s_wbuf[s_bufId].data_pcm16, m);
			if (!m) break;
			if (vol != 256)
				for (u32 i = 0; i < m * 2; i++)
					s_wbuf[s_bufId].data_pcm16[i] = clamp16(s_wbuf[s_bufId].data_pcm16[i] * vol >> 8);
			audio_queue(m);
		}
		spsc_trim(play, LATENCY_CAP);
		return;
	}

	// MIXED / SPLIT: consume both rings in lockstep.
	while (audio_wbuf_free()) {
		u32 a = spsc_avail(rA), b = spsc_avail(rB);
		u32 m = a < b ? a : b;
		if (!m) break;
		if (m > AUDIO_FRAMES) m = AUDIO_FRAMES;
		spsc_read(rA, s_mixA, m);
		spsc_read(rB, s_mixB, m);
		int16_t* out = s_wbuf[s_bufId].data_pcm16;
		for (u32 i = 0; i < m; i++) {
			int aL = s_mixA[2*i], aR = s_mixA[2*i+1];
			int bL = s_mixB[2*i], bR = s_mixB[2*i+1];
			if (mode == AUD_MIXED) {
				out[2*i]   = clamp16((aL * volA + bL * volB) >> 8);
				out[2*i+1] = clamp16((aR * volA + bR * volB) >> 8);
			} else {   // SPLIT: A -> left, B -> right (each downmixed to mono)
				out[2*i]   = clamp16(((aL + aR) >> 1) * volA >> 8);
				out[2*i+1] = clamp16(((bL + bR) >> 1) * volB >> 8);
			}
		}
		audio_queue(m);
	}
	spsc_trim(rA, LATENCY_CAP);   // keep the faster core from building latency
	spsc_trim(rB, LATENCY_CAP);
}

static void audio_thread_fn(void* arg) {
	(void)arg;
	while (!s_quit) {
		if (s_rateReq)  { ndspChnSetRate(0, s_pendRate); s_rateReq = false; s_resetReq = true; }
		if (s_resetReq) { ndspChnWaveBufClear(0); audio_wbuf_reset();
		                  spsc_clear(&s_ring[0]); spsc_clear(&s_ring[1]); s_resetReq = false; }
		if (s_muted) { svcSleepThread(16 * 1000 * 1000); continue; }   // hard mute: no mixing/feeding
		audio_mix_fill();
		svcSleepThread(4 * 1000 * 1000);   // 4ms; wave-buffer ring holds ~150ms, so this never starves
	}
	ndspChnWaveBufClear(0);
}

// ---- lifecycle -------------------------------------------------------------
void audio_init(void) {
	if (R_FAILED(ndspInit())) return;   // no dspfirm.cdc -> stays silent
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
	LightLock_Init(&s_paramLock);
	s_ring[0].buf = (int16_t*)malloc(RING_CAP * 2 * sizeof(int16_t));
	s_ring[1].buf = (int16_t*)malloc(RING_CAP * 2 * sizeof(int16_t));
}

void audio_exit(void) {
	if (!s_hasSound) return;
	audio_thread_stop();
	ndspChnWaveBufClear(0);
	ndspExit();
	if (s_audioMem) { linearFree(s_audioMem); s_audioMem = NULL; }
	free(s_ring[0].buf); free(s_ring[1].buf);
	s_ring[0].buf = s_ring[1].buf = NULL;
	s_hasSound = false;
}

bool audio_ready(void) { return s_hasSound; }

void audio_thread_start(s32 mainPrio, bool isN3DS) {
	if (!s_hasSound || s_thread) return;
	s_ring[0].head = s_ring[0].tail = s_ring[1].head = s_ring[1].tail = 0;
	s_present[0] = s_present[1] = false;
	s_quit = false; s_resetReq = true;
	// Core-1 syscore slice on New 3DS (light audio work fits; needs APT_SetAppCpuTimeLimit, set in
	// main). On Old 3DS core 1 hosts worker B, so let the scheduler place audio (coreId -1).
	s_thread = threadCreate(audio_thread_fn, NULL, AUDIO_STACK, mainPrio - 1, isN3DS ? 1 : -1, false);
	if (!s_thread) s_thread = threadCreate(audio_thread_fn, NULL, AUDIO_STACK, mainPrio - 1, -1, false);
}

void audio_thread_stop(void) {
	if (!s_thread) return;
	s_quit = true;
	threadJoin(s_thread, U64_MAX);
	threadFree(s_thread);
	s_thread = NULL;
}

void audio_pump_core(int slot, GbaCore* core) {   // CALLED ON THE WORKER THREAD for `slot`
	if (!s_hasSound || s_muted || !core) return;   // hard mute: skip the drain/mix work entirely
	s_present[slot] = true;
	for (;;) {
		size_t avail = gbacore_audio_available(core);
		if (!avail) break;
		size_t m = avail < PUMP_CHUNK ? avail : PUMP_CHUNK;
		m = gbacore_read_audio(core, s_pump[slot], m);
		if (!m) break;
		spsc_write(&s_ring[slot], s_pump[slot], (u32)m);
	}
}

void audio_set_params(int focused, int mode, int volA, int volB) {
	if (!s_hasSound) return;   // s_paramLock is only inited once ndsp came up
	LightLock_Lock(&s_paramLock);
	s_focused = focused; s_mode = mode; s_volA = volA; s_volB = volB;
	LightLock_Unlock(&s_paramLock);
}

void audio_set_rate(GbaCore* core) {
	if (!s_hasSound || !core) return;
	s_pendRate = (float)gbacore_ndsp_rate(core);
	s_rateReq = true;
}

void audio_reset_stream(void) { if (s_hasSound) s_resetReq = true; }

void audio_set_muted(bool m) { s_muted = m; s_resetReq = true; }   // reset clears the channel (instant silence)
