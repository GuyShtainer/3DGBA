// dual-gba (3DS) — two real mGBA cores, one per screen, with a ROM picker and an
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

#include "gbacore.h"
#include "rompicker.h"

#define WORKER_STACKSIZE (512 * 1024)   // mGBA runFrame has deep call chains; 32KB overflows

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
} EmuInstance;

static volatile bool g_quit = false;

static void emu_step(EmuInstance* e) {
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
		if (!e->skip) emu_step(e);
		LightEvent_Signal(&e->done);
	}
}

static void emu_start(EmuInstance* e, int id, int core, int prio) {
	memset(e, 0, sizeof(*e));
	e->id = id;
	e->core_id = -1;
	LightEvent_Init(&e->go,   RESET_ONESHOT);
	LightEvent_Init(&e->done, RESET_ONESHOT);
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

// Sharp-bilinear (the "Sharp" filter at fractional scales): integer-NEAREST prescale the
// 240x160 frame into an offscreen target, then LINEAR-downscale that to fit the screen.
// Linear then only blends between already-huge clean pixels -> crisp edges, no shimmer, no
// blur. This is exactly what mGBA's own 3DS port does. 2x is enough to kill the wobble at
// the 1.5x/1.33x screen-fit factors; PRE_TEX is the POT texture that backs the 480x320 buffer.
#define PRESCALE 2
#define PRE_W    (GBA_W * PRESCALE)   // 480
#define PRE_H    (GBA_H * PRESCALE)   // 320
#define PRE_TEX  512

// Draw one game to `screen` at the current scale + filter, leaving `screen` bound so the
// caller can draw overlays (focus bar, toast, menu) on top. `preTgt`/`preTex` are the shared
// offscreen prescale buffer, reused per screen (sequential on the render thread -> no race).
static void render_game(EmuInstance* e, C3D_RenderTarget* screen, C3D_RenderTarget* preTgt,
                        C3D_Tex* preTex, float screenW, float screenH,
                        int mode, bool smooth, const C2D_ImageTint* tint, u32 clrBg) {
	if (!e->core) { C2D_TargetClear(screen, clrBg); C2D_SceneBegin(screen); return; }

	float sx, sy;
	if (mode == SCALE_1X)           { sx = sy = 1.0f; }
	else if (mode == SCALE_STRETCH) { sx = screenW / GBA_W; sy = screenH / GBA_H; }
	else { float f = (screenW / GBA_W < screenH / GBA_H) ? screenW / GBA_W : screenH / GBA_H; sx = sy = f; }
	float x = (screenW - GBA_W * sx) / 2.0f;
	float y = (screenH - GBA_H * sy) / 2.0f;

	// 1:1 is already pixel-perfect; sharp-bilinear only helps the fractional fits.
	bool sharpBilinear = !smooth && mode != SCALE_1X && preTgt;

	if (sharpBilinear) {
		// Pass 0: NEAREST integer prescale 240x160 -> 480x320 into the offscreen target.
		Tex3DS_SubTexture s0 = { GBA_W, GBA_H, 0.0f, 1.0f,
		                         (float)GBA_W / 256.0f, 1.0f - (float)GBA_H / 256.0f };
		C2D_Image i0 = { &e->tex, &s0 };   // source = this core's 256x256 GBA texture
		C3D_TexSetFilter(&e->tex, GPU_NEAREST, GPU_NEAREST);
		C2D_TargetClear(preTgt, C2D_Color32(0, 0, 0, 0));
		C2D_SceneBegin(preTgt);
		C2D_DrawImageAt(i0, 0.0f, 0.0f, 0.0f, NULL, (float)PRESCALE, (float)PRESCALE);

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
		C2D_SceneBegin(screen);
		C2D_DrawImageAt(img, x, y, 0.0f, tint, sx, sy);
	}
}

// ---- Audio output (v0.7 solo): one ndsp stereo channel; only the FOCUSED game plays ----
#define AUDIO_FRAMES   1280   // stereo frames per wave buffer (matches mGBA's 3DS port)
#define AUDIO_DSP_BUFS 4
static ndspWaveBuf s_wbuf[AUDIO_DSP_BUFS];
static int16_t*    s_audioMem = NULL;
static int         s_bufId = 0;
static bool        s_hasSound = false;

// Audio mix modes (volume is per-game, 0..256 where 256 = 100%).
enum { AUD_SOLO, AUD_MIXED, AUD_SPLIT };
static const char* AUDIO_NAMES[] = { "Solo", "Mixed", "Split" };
static int16_t s_mixA[AUDIO_FRAMES * 2];   // scratch for reading each core when mixing
static int16_t s_mixB[AUDIO_FRAMES * 2];
static inline int16_t clamp16(int v) { return v < -32768 ? -32768 : (v > 32767 ? 32767 : v); }

static bool s_hasPtm = false;   // ptm:u for battery level (HUD)

static void audio_wbuf_reset(void) {   // (re)point the 4 wave buffers at their slices
	memset(s_wbuf, 0, sizeof(s_wbuf));
	for (int i = 0; i < AUDIO_DSP_BUFS; i++)
		s_wbuf[i].data_pcm16 = &s_audioMem[AUDIO_FRAMES * 2 * i];
	s_bufId = 0;
}

static void audio_init(void) {
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

static void audio_exit(void) {
	if (!s_hasSound) return;
	ndspChnWaveBufClear(0);
	ndspExit();
	if (s_audioMem) linearFree(s_audioMem);
	s_audioMem = NULL;
	s_hasSound = false;
}

// Drop everything queued/playing and reset the ring — instant clean cut on a focus switch.
static void audio_reset_stream(void) {
	if (!s_hasSound) return;
	ndspChnWaveBufClear(0);
	audio_wbuf_reset();
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

// Mix the two cores into ndsp per the chosen mode. SOLO = focused core only (other drained);
// MIXED = both summed (per-game volume); SPLIT = game A -> left speaker, game B -> right.
// volA/volB are 0..256 (256 = 100%).
static void audio_feed(EmuInstance* ea, EmuInstance* eb, int focused, int mode, int volA, int volB) {
	if (!s_hasSound) return;
	GbaCore* ca = ea->core;
	GbaCore* cb = eb->core;

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

// ---- Settings persistence (sdmc:/dual-gba/settings.bin) --------------------
#define SETTINGS_PATH  "sdmc:/dual-gba/settings.bin"
#define SETTINGS_MAGIC 0x32424744u   // 'DGB2'
typedef struct {
	u32 magic;
	s32 scaleMode[2];
	s32 smooth[2];
	s32 swapped;
	s32 hudOn;
	s32 audioMode;
	s32 volA, volB;
} Settings;

static void settings_load(int scaleMode[2], bool smooth[2], bool* swapped, bool* hudOn,
                          int* audioMode, int* volA, int* volB) {
	FILE* f = fopen(SETTINGS_PATH, "rb");
	if (!f) return;
	Settings s;
	size_t n = fread(&s, 1, sizeof s, f);
	fclose(f);
	if (n != sizeof s || s.magic != SETTINGS_MAGIC) return;
	scaleMode[0] = ((unsigned)s.scaleMode[0]) % 3;
	scaleMode[1] = ((unsigned)s.scaleMode[1]) % 3;
	smooth[0] = s.smooth[0] != 0;
	smooth[1] = s.smooth[1] != 0;
	*swapped  = s.swapped != 0;
	*hudOn    = s.hudOn   != 0;
	*audioMode = ((unsigned)s.audioMode) % 3;
	*volA = s.volA < 0 ? 0 : (s.volA > 256 ? 256 : s.volA);
	*volB = s.volB < 0 ? 0 : (s.volB > 256 ? 256 : s.volB);
}

static void settings_save(const int scaleMode[2], const bool smooth[2], bool swapped, bool hudOn,
                          int audioMode, int volA, int volB) {
	Settings s = { SETTINGS_MAGIC, { scaleMode[0], scaleMode[1] },
	               { smooth[0], smooth[1] }, swapped, hudOn, audioMode, volA, volB };
	FILE* f = fopen(SETTINGS_PATH, "wb");
	if (!f) return;
	fwrite(&s, 1, sizeof s, f);
	fclose(f);
}

// ---- Pause menu ----
enum { SESSION_CHANGE, SESSION_QUIT };
static const char* MENU_ITEMS[] = {
	"Resume", "Audio", "Toggle HUD", "Swap screens",
	"Save A", "Load A", "Save B", "Load B", "Load .sav (focused)", "Change games", "Quit"
};
#define MENU_N 11
#define MENU_AUDIO_IDX 1   // this row's label is built dynamically ("Audio: <mode>")

// Run one play session with the two chosen ROMs. Returns SESSION_CHANGE (re-pick) or
// SESSION_QUIT. Creates/destroys the cores + worker threads itself.
static int run_session(C3D_RenderTarget* top, C3D_RenderTarget* bot, C2D_TextBuf txtBuf,
                       bool isN3DS, s32 mainPrio, const char* pathA, const char* pathB) {
	const u32 clrBg     = C2D_Color32(0x10, 0x10, 0x10, 0xFF);
	const u32 clrHi     = C2D_Color32(0xF5, 0xD0, 0x42, 0xFF);
	const u32 clrTxt    = C2D_Color32(0xFF, 0xFF, 0xFF, 0xFF);
	const u32 clrDim    = C2D_Color32(0x00, 0x00, 0x00, 0xC0);  // menu dim overlay
	const u32 clrPanel  = C2D_Color32(0x2a, 0x20, 0x42, 0xFF);  // GBA-nostalgic indigo
	const u32 clrSelTxt = C2D_Color32(0x20, 0x18, 0x30, 0xFF);

	EmuInstance emuA, emuB;
	emu_start(&emuA, 0, 0,              mainPrio + 1);
	emu_start(&emuB, 1, isN3DS ? 2 : 1, mainPrio + 1);
	setup_core(&emuA, pathA);
	setup_core(&emuB, pathB);

	// Shared offscreen target for sharp-bilinear's NEAREST prescale pass (reused per screen).
	C3D_Tex preTex;
	C3D_RenderTarget* preTgt = NULL;
	if (C3D_TexInitVRAM(&preTex, PRE_TEX, PRE_TEX, GPU_RGBA8)) {
		C3D_TexSetWrap(&preTex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
		preTgt = C3D_RenderTargetCreateFromTex(&preTex, GPU_TEXFACE_2D, 0, -1);
	}

	// Audio: both cores share one rate (pitch-matched to the 3DS refresh); start clean.
	GbaCore* anyCore = emuA.core ? emuA.core : emuB.core;
	if (s_hasSound && anyCore) {
		ndspChnSetRate(0, (float)gbacore_ndsp_rate(anyCore));
		audio_reset_stream();
	}

	int focused = 0;
	bool menuOpen = false;
	int  menuSel = 0;
	int  result = SESSION_QUIT;
	char status[24] = "";   // last save/load result, shown in the menu
	// Scale + filter are PER SCREEN ([0]=top, [1]=bottom): the 400x240 top and 320x240 bottom
	// have different best fits. ZR/ZL adjust the focused screen (X/Y switches focus).
	int  scaleMode[2] = { SCALE_FIT, SCALE_FIT };
	bool smooth[2]    = { false, false };
	bool swapped      = false;   // false: A=top / B=bottom. true: B=top / A=bottom.
	char toast[48] = "";
	int  toastTimer = 0;

	// HUD (menu-toggleable): per-screen game label + FPS + clock + battery.
	bool hudOn = true;
	char nameA[64], nameB[64];
	rom_display_name(pathA, nameA, sizeof nameA);
	rom_display_name(pathB, nameB, sizeof nameB);
	u64 fpsT0 = osGetTime();
	int fpsFrames = 0, fps = 0;
	u8  batLvl = 0;
	int batTimer = 0;

	// Audio: mode (solo/mixed/split) + per-game volume (0..256).
	int audioMode = AUD_SOLO;
	int volA = 256, volB = 256;

	settings_load(scaleMode, smooth, &swapped, &hudOn, &audioMode, &volA, &volB);   // restore prefs

	while (aptMainLoop()) {
		hidScanInput();
		u32 kDown = hidKeysDown();
		u32 kHeld = hidKeysHeld();

		// HUD stats: FPS (0.5s window) + battery (throttled).
		fpsFrames++;
		u64 nowMs = osGetTime();
		if (nowMs - fpsT0 >= 500) { fps = (int)(fpsFrames * 1000 / (nowMs - fpsT0)); fpsFrames = 0; fpsT0 = nowMs; }
		if (s_hasPtm && --batTimer <= 0) { PTMU_GetBatteryLevel(&batLvl); batTimer = 60; }

		if (!menuOpen) {
			bool combo = (kHeld & KEY_START) && (kHeld & KEY_SELECT);
			if ((kDown & KEY_TOUCH) || combo) {
				menuOpen = true; menuSel = 0; status[0] = '\0';
			} else {
				if (kDown & (KEY_X | KEY_Y)) { focused ^= 1; audio_reset_stream(); }
				int fs = swapped ? (focused ^ 1) : focused;   // screen the focused game sits on
				if (kDown & KEY_ZR) {
					scaleMode[fs] = (scaleMode[fs] + 1) % 3;
					snprintf(toast, sizeof toast, "%s scale: %s",
					         fs == 0 ? "Top" : "Bottom", SCALE_NAMES[scaleMode[fs]]);
					toastTimer = 90;
					settings_save(scaleMode, smooth, swapped, hudOn, audioMode, volA, volB);
				}
				if (kDown & KEY_ZL) {
					smooth[fs] = !smooth[fs];   // render_game sets the per-pass filters
					snprintf(toast, sizeof toast, "%s filter: %s", fs == 0 ? "Top" : "Bottom",
					         smooth[fs] ? "Smooth" : "Sharp-bilinear");
					toastTimer = 90;
					settings_save(scaleMode, smooth, swapped, hudOn, audioMode, volA, volB);
				}
				u16 g = to_gba_keys(kHeld);
				emuA.keys = (focused == 0) ? g : 0;
				emuB.keys = (focused == 1) ? g : 0;
				LightEvent_Signal(&emuA.go);
				LightEvent_Signal(&emuB.go);
				LightEvent_Wait(&emuA.done);
				LightEvent_Wait(&emuB.done);
				if (emuA.core) upload_frame(&emuA);
				if (emuB.core) upload_frame(&emuB);
				audio_feed(&emuA, &emuB, focused, audioMode, volA, volB);
			}
		} else {
			if (kDown & (KEY_DDOWN | KEY_CPAD_DOWN)) menuSel = (menuSel + 1) % MENU_N;
			if (kDown & (KEY_DUP   | KEY_CPAD_UP))   menuSel = (menuSel - 1 + MENU_N) % MENU_N;
			if (menuSel == MENU_AUDIO_IDX && (kDown & (KEY_DLEFT | KEY_CPAD_LEFT | KEY_DRIGHT | KEY_CPAD_RIGHT))) {
					int* v = (focused == 0) ? &volA : &volB;   // adjust the focused game's volume
					*v += (kDown & (KEY_DRIGHT | KEY_CPAD_RIGHT)) ? 32 : -32;
					if (*v < 0) *v = 0; else if (*v > 256) *v = 256;
					snprintf(status, sizeof status, "Vol %c: %d%%", focused == 0 ? 'A' : 'B', *v * 100 / 256);
					settings_save(scaleMode, smooth, swapped, hudOn, audioMode, volA, volB);
				}
				if (kDown & KEY_B) menuOpen = false;                 // resume
			if (kDown & KEY_A) {
				if      (menuSel == 0) menuOpen = false;         // Resume
				else if (menuSel == 1) {                         // Audio mode (Solo/Mixed/Split)
					audioMode = (audioMode + 1) % 3;
					snprintf(status, sizeof status, "Audio: %s", AUDIO_NAMES[audioMode]);
					audio_reset_stream();                        // clean cut between modes
					settings_save(scaleMode, smooth, swapped, hudOn, audioMode, volA, volB);
				}
				else if (menuSel == 2) {                         // Toggle HUD
					hudOn = !hudOn;
					snprintf(status, sizeof status, "HUD %s", hudOn ? "on" : "off");
					settings_save(scaleMode, smooth, swapped, hudOn, audioMode, volA, volB);
				}
				else if (menuSel == 3) {                         // Swap screens
					swapped = !swapped; menuOpen = false;
					snprintf(toast, sizeof toast, "Layout: %s", swapped ? "B top / A bottom" : "A top / B bottom");
					toastTimer = 90;
					settings_save(scaleMode, smooth, swapped, hudOn, audioMode, volA, volB);
				}
				else if (menuSel == 4) snprintf(status, sizeof status, "%s", gbacore_save_state(emuA.core, 1) ? "Saved A"  : "Save A failed");
				else if (menuSel == 5) snprintf(status, sizeof status, "%s", gbacore_load_state(emuA.core, 1) ? "Loaded A" : "No state A");
				else if (menuSel == 6) snprintf(status, sizeof status, "%s", gbacore_save_state(emuB.core, 1) ? "Saved B"  : "Save B failed");
				else if (menuSel == 7) snprintf(status, sizeof status, "%s", gbacore_load_state(emuB.core, 1) ? "Loaded B" : "No state B");
				else if (menuSel == 8) {                         // Load a .sav into the focused game
					EmuInstance* fg = (focused == 0) ? &emuA : &emuB;
					char savp[256];
					if (fg->core && savpicker_run(top, bot, txtBuf, savp, sizeof savp))
						snprintf(status, sizeof status, "%s %c", gbacore_load_save(fg->core, savp) ? "Loaded sav" : "sav failed", focused == 0 ? 'A' : 'B');
					else
						snprintf(status, sizeof status, "no .sav files");
				}
				else if (menuSel == 9) { result = SESSION_CHANGE; break; }
				else                   { result = SESSION_QUIT;   break; }
			}
		}

		// ---- text for this frame (single buffer; cleared once) ----
		C2D_TextBufClear(txtBuf);
		C2D_Text items[MENU_N], tHint, tStatus, tToast;

		// HUD text: per-screen game label + a top-screen stat line (FPS / clock / battery).
		C2D_Text tHudTop, tHudBot, tHudStat;
		const char* topName = swapped ? nameB : nameA;
		const char* botName = swapped ? nameA : nameB;
		char hudStat[40];
		if (hudOn) {
			time_t tt = time(NULL);
			struct tm* lt = localtime(&tt);
			snprintf(hudStat, sizeof hudStat, "%s  %dfps  %02d:%02d  %d/5",
			         AUDIO_NAMES[audioMode], fps, lt ? lt->tm_hour : 0, lt ? lt->tm_min : 0, batLvl);
			C2D_TextParse(&tHudTop,  txtBuf, topName);  C2D_TextOptimize(&tHudTop);
			C2D_TextParse(&tHudBot,  txtBuf, botName);  C2D_TextOptimize(&tHudBot);
			C2D_TextParse(&tHudStat, txtBuf, hudStat);  C2D_TextOptimize(&tHudStat);
		}
		if (menuOpen) {
			char alabel[40];
			for (int i = 0; i < MENU_N; i++) {
				const char* label = MENU_ITEMS[i];
				if (i == MENU_AUDIO_IDX) {   // dynamic: "Audio: Mixed  A100 B80"
					snprintf(alabel, sizeof alabel, "Audio: %s  A%d B%d",
					         AUDIO_NAMES[audioMode], volA * 100 / 256, volB * 100 / 256);
					label = alabel;
				}
				C2D_TextParse(&items[i], txtBuf, label); C2D_TextOptimize(&items[i]);
			}
			if (status[0]) { C2D_TextParse(&tStatus, txtBuf, status); C2D_TextOptimize(&tStatus); }
		} else {
			C2D_TextParse(&tHint, txtBuf, "tap or START+SELECT for menu");
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
		render_game(topG, top, preTgt, &preTex, 400.0f, 240.0f, scaleMode[0], smooth[0], topTint, clrBg);
		if (!menuOpen) {
			if (hudOn) {
				C2D_DrawRectSolid(0.0f, 0.0f, 0.0f, 400.0f, 14.0f, C2D_Color32(0, 0, 0, 0x90));
				if (focScreen == 0) C2D_DrawRectSolid(0.0f, 14.0f, 0.0f, 400.0f, 2.0f, clrHi);
				C2D_DrawText(&tHudTop, C2D_WithColor, 4.0f, 1.0f, 0.0f, 0.4f, 0.4f, clrTxt);
				float sw, sh; C2D_TextGetDimensions(&tHudStat, 0.4f, 0.4f, &sw, &sh);
				C2D_DrawText(&tHudStat, C2D_WithColor, 396.0f - sw, 1.0f, 0.0f, 0.4f, 0.4f, clrTxt);
			} else if (focScreen == 0) {
				C2D_DrawRectSolid(0.0f, 0.0f, 0.0f, 400.0f, 4.0f, clrHi);
			}
			if (toastTimer > 0) C2D_DrawText(&tToast, C2D_WithColor, 8.0f, hudOn ? 20.0f : 8.0f, 0.0f, 0.5f, 0.5f, clrHi);
		}

		// bottom screen (+ menu overlay when open). render_game leaves `bot` bound.
		render_game(botG, bot, preTgt, &preTex, 320.0f, 240.0f, scaleMode[1], smooth[1], botTint, clrBg);
		if (!menuOpen) {
			if (hudOn) {
				C2D_DrawRectSolid(0.0f, 0.0f, 0.0f, 320.0f, 14.0f, C2D_Color32(0, 0, 0, 0x90));
				if (focScreen == 1) C2D_DrawRectSolid(0.0f, 14.0f, 0.0f, 320.0f, 2.0f, clrHi);
				C2D_DrawText(&tHudBot, C2D_WithColor, 4.0f, 1.0f, 0.0f, 0.4f, 0.4f, clrTxt);
			} else if (focScreen == 1) {
				C2D_DrawRectSolid(0.0f, 0.0f, 0.0f, 320.0f, 4.0f, clrHi);
			}
		}
		if (menuOpen) {
			C2D_DrawRectSolid(0.0f, 0.0f, 0.0f, 320.0f, 240.0f, clrDim);
			C2D_DrawRectSolid(34.0f, 2.0f, 0.0f, 252.0f, 236.0f, clrPanel);
			for (int i = 0; i < MENU_N; i++) {
				float y = 5.0f + i * 20.0f;
				bool s = (i == menuSel);
				if (s) C2D_DrawRectSolid(44.0f, y - 1.0f, 0.0f, 232.0f, 18.0f, clrHi);
				C2D_DrawText(&items[i], C2D_WithColor, 54.0f, y, 0.0f, 0.46f, 0.46f, s ? clrSelTxt : clrTxt);
			}
			if (status[0]) C2D_DrawText(&tStatus, C2D_WithColor, 44.0f, 226.0f, 0.0f, 0.42f, 0.42f, clrTxt);
		} else {
			C2D_DrawText(&tHint, C2D_WithColor, 6.0f, 224.0f, 0.0f, 0.4f, 0.4f, clrTxt);
		}

		C3D_FrameEnd(0);
		if (toastTimer > 0) toastTimer--;
	}

	// teardown this session's workers + cores; reset g_quit for the next session
	if (s_hasSound) ndspChnWaveBufClear(0);   // stop sound between sessions
	g_quit = true;
	LightEvent_Signal(&emuA.go);
	LightEvent_Signal(&emuB.go);
	if (emuA.thread) { threadJoin(emuA.thread, U64_MAX); threadFree(emuA.thread); }
	if (emuB.thread) { threadJoin(emuB.thread, U64_MAX); threadFree(emuB.thread); }
	teardown_core(&emuA);
	teardown_core(&emuB);
	if (preTgt) { C3D_RenderTargetDelete(preTgt); C3D_TexDelete(&preTex); }
	g_quit = false;
	return result;
}

// ---- Animated boot splash (GBA-nostalgic, dual-screen) ----------------------
static u32 dim_color(u32 c, float f) {   // scale RGB toward black, keep alpha
	u32 r = (c & 0xFF) * f, g = ((c >> 8) & 0xFF) * f, b = ((c >> 16) & 0xFF) * f;
	return (c & 0xFF000000) | (b << 16) | (g << 8) | r;
}

// A mini GBA "screen" (bezel + screen + ground band + player dot) for the splash.
static void splash_panel(float x, float y, float w, float h, u32 inner, u32 dot) {
	C2D_DrawRectSolid(x - 3, y - 3, 0.0f, w + 6, h + 6, C2D_Color32(0x12, 0x0e, 0x1c, 0xFF));
	C2D_DrawRectSolid(x, y, 0.0f, w, h, inner);
	C2D_DrawRectSolid(x, y + h * 0.62f, 0.0f, w, h * 0.38f, dim_color(inner, 0.7f));
	C2D_DrawRectSolid(x + w * 0.5f - 3, y + h * 0.5f - 3, 0.0f, 6.0f, 6.0f, dot);
}

static float ease_out(float p) { float q = 1.0f - p; return 1.0f - q * q * q; }

static void run_splash(C3D_RenderTarget* top, C3D_RenderTarget* bot, C2D_TextBuf txtBuf) {
	const u32 bg   = C2D_Color32(0x18, 0x11, 0x28, 0xFF);
	const u32 gold = C2D_Color32(0xF5, 0xD0, 0x42, 0xFF);
	const u32 grn  = C2D_Color32(0x4a, 0x7a, 0x2a, 0xFF);
	const u32 blu  = C2D_Color32(0x2a, 0x60, 0x96, 0xFF);
	const int DUR  = 150;   // ~2.5s at 60fps; A/START/touch skips

	for (int f = 0; f < DUR && aptMainLoop(); f++) {
		hidScanInput();
		if (hidKeysDown() & (KEY_A | KEY_B | KEY_START | KEY_TOUCH)) break;
		float t = (float)f / DUR;

		float slide = ease_out(t < 0.5f ? t / 0.5f : 1.0f);          // panels glide in
		float titleA = t < 0.4f ? 0.0f : (t < 0.7f ? (t - 0.4f) / 0.3f : 1.0f);
		float fade   = t > 0.92f ? (t - 0.92f) / 0.08f : 0.0f;       // fade out at the end
		float pulse  = 0.6f + 0.4f * ((f / 8) % 2);                  // link "blink"

		const float pw = 150.0f, ph = 100.0f;
		C2D_Text tTitle, tSub;
		C2D_TextBufClear(txtBuf);
		C2D_TextParse(&tTitle, txtBuf, "DUAL GBA");                   C2D_TextOptimize(&tTitle);
		C2D_TextParse(&tSub,   txtBuf, "two games  -  one link cable"); C2D_TextOptimize(&tSub);
		float twT, thT, twS, thS;
		C2D_TextGetDimensions(&tTitle, 1.0f, 1.0f, &twT, &thT);
		C2D_TextGetDimensions(&tSub,   0.5f, 0.5f, &twS, &thS);

		C3D_FrameBegin(C3D_FRAME_SYNCDRAW);

		// top screen: Game-A panel drops in from above + title
		C2D_TargetClear(top, bg);
		C2D_SceneBegin(top);
		float ax = (400.0f - pw) / 2.0f, ay = -ph + (28.0f + ph) * slide;
		splash_panel(ax, ay, pw, ph, grn, C2D_Color32(0xF0, 0xDC, 0x40, 0xFF));
		C2D_DrawText(&tTitle, C2D_WithColor, (400.0f - twT) / 2.0f, 196.0f, 0.0f, 1.0f, 1.0f,
		             C2D_Color32(0xF5, 0xD0, 0x42, (u8)(titleA * 255)));
		// link plug reaching down toward the hinge
		C2D_DrawRectSolid(196.0f, ay + ph, 0.0f, 8.0f, 12.0f * slide, dim_color(gold, pulse));
		if (fade > 0.0f) C2D_DrawRectSolid(0, 0, 0, 400, 240, C2D_Color32(0x18, 0x11, 0x28, (u8)(fade * 255)));

		// bottom screen: Game-B panel rises in from below + subtitle
		C2D_TargetClear(bot, bg);
		C2D_SceneBegin(bot);
		float bx = (320.0f - pw) / 2.0f, by = 240.0f - (28.0f + ph) * slide;
		C2D_DrawRectSolid(156.0f, by - 12.0f * slide, 0.0f, 8.0f, 12.0f * slide, dim_color(gold, pulse));
		splash_panel(bx, by, pw, ph, blu, C2D_Color32(0xE6, 0x4B, 0x41, 0xFF));
		C2D_DrawText(&tSub, C2D_WithColor, (320.0f - twS) / 2.0f, 220.0f, 0.0f, 0.5f, 0.5f,
		             C2D_Color32(0xBE, 0xB4, 0xD7, (u8)(titleA * 255)));
		if (fade > 0.0f) C2D_DrawRectSolid(0, 0, 0, 320, 240, C2D_Color32(0x18, 0x11, 0x28, (u8)(fade * 255)));

		C3D_FrameEnd(0);
	}
}

int main(int argc, char** argv) {
	bool isN3DS = false;
	APT_CheckNew3DS(&isN3DS);
	if (isN3DS) osSetSpeedupEnable(true);
	APT_SetAppCpuTimeLimit(80);

	gfxInitDefault();
	C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
	C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
	C2D_Prepare();
	audio_init();   // ndsp; silently no-ops if dspfirm.cdc isn't present
	s_hasPtm = R_SUCCEEDED(ptmuInit());   // battery level for the HUD
	C3D_RenderTarget* top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
	C3D_RenderTarget* bot = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
	C2D_TextBuf txtBuf = C2D_TextBufNew(4096);

	s32 mainPrio = 0x30;
	svcGetThreadPriority(&mainPrio, CUR_THREAD_HANDLE);

	run_splash(top, bot, txtBuf);   // animated GBA-nostalgic boot splash (skippable)

	// Session loop: pick two ROMs, play, and on "Change games" pick again.
	while (aptMainLoop()) {
		char pathA[256], pathB[256];
		if (!rompicker_run(top, bot, txtBuf, pathA, pathB, sizeof pathA)) {
			strcpy(pathA, "sdmc:/dual-gba/gameA.gba");
			strcpy(pathB, "sdmc:/dual-gba/gameB.gba");
		} else {
			rompicker_save_recent(pathA, pathB);   // remember for next boot's resume prompt
		}
		int r = run_session(top, bot, txtBuf, isN3DS, mainPrio, pathA, pathB);
		if (r == SESSION_QUIT) break;
		// SESSION_CHANGE -> loop back to the picker
	}

	audio_exit();
	if (s_hasPtm) ptmuExit();
	C2D_TextBufDelete(txtBuf);
	C2D_Fini();
	C3D_Fini();
	gfxExit();
	return 0;
}
