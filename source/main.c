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
#include "theme.h"
#include "gamestate.h"
#include "touch.h"
#include "audio.h"

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
	volatile bool wantWait;   // lockstep asked this core to park (set in onSleep)
	LightEvent    waitEv;     // peer signals this to un-park us (onWake)
	volatile bool paused;     // X freezes the focused game (unlinked play only)
} EmuInstance;

static volatile bool g_quit = false;
static bool s_hasPtm = false;   // ptm:u for battery level (HUD)
static volatile bool g_appActive = true;   // false while suspended (HOME/sleep) -> idle, free the cores
static aptHookCookie s_aptCookie;
static void apt_hook(APT_HookType t, void* p) {
	(void)p;
	if (t == APTHOOK_ONSUSPEND || t == APTHOOK_ONSLEEP) g_appActive = false;
	else if (t == APTHOOK_ONRESTORE || t == APTHOOK_ONWAKEUP) g_appActive = true;
}

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
			while (e->linked && !g_quit) {
				gbacore_set_keys(e->core, (u16)e->keys);
				gbacore_run_loop(e->core);
				e->frame++;
				audio_pump_core(e->id, e->core);   // drain this core's audio into its ring (link audio)
				if (e->wantWait) { e->wantWait = false; LightEvent_Wait(&e->waitEv); }
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

// ---- Settings persistence (sdmc:/dual-gba/settings.bin) --------------------
#define SETTINGS_PATH  "sdmc:/dual-gba/settings.bin"
#define SETTINGS_MAGIC 0x33424744u   // 'DGB3'
typedef struct {
	u32 magic;
	s32 scaleMode[2];
	s32 smooth[2];
	s32 swapped;
	s32 hudOn;
	s32 audioMode;
	s32 volA, volB;
	s32 touchMode;
	s32 frameskip;
} Settings;

static void settings_load(int scaleMode[2], bool smooth[2], bool* swapped, bool* hudOn,
                          int* audioMode, int* volA, int* volB, int* touchMode, bool* fsOn) {
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
	*touchMode = ((unsigned)s.touchMode) % 3;
	*fsOn = s.frameskip != 0;
}

static void settings_save(const int scaleMode[2], const bool smooth[2], bool swapped, bool hudOn,
                          int audioMode, int volA, int volB, int touchMode, bool fsOn) {
	Settings s = { SETTINGS_MAGIC, { scaleMode[0], scaleMode[1] },
	               { smooth[0], smooth[1] }, swapped, hudOn, audioMode, volA, volB, touchMode, fsOn };
	FILE* f = fopen(SETTINGS_PATH, "wb");
	if (!f) return;
	fwrite(&s, 1, sizeof s, f);
	fclose(f);
}

// ---- Pause menu ----
enum { SESSION_CHANGE, SESSION_QUIT };
static const char* MENU_ITEMS[] = {
	"Resume", "Link", "Audio", "Touch", "Frameskip", "Toggle HUD", "Swap screens",
	"Save state", "Load state", "Load .sav (focused)", "Mute", "Pause", "Change games", "Quit"
};
#define MENU_N 14
#define MENU_LINK_IDX  1   // dynamic label ("Link: off/on")
#define MENU_AUDIO_IDX 2   // dynamic label ("Audio: <mode>")
#define MENU_TOUCH_IDX 3   // dynamic label ("Touch: on/off")
#define MENU_FS_IDX    4   // dynamic label ("Frameskip: on/off")
#define MENU_MUTE_IDX  10  // dynamic label ("Mute: on/off")
#define MENU_PAUSE_IDX 11  // dynamic label ("Pause A/B: on/off")

// Run one play session with the two chosen ROMs. Returns SESSION_CHANGE (re-pick) or
// SESSION_QUIT. Creates/destroys the cores + worker threads itself.
static int run_session(C3D_RenderTarget* top, C3D_RenderTarget* bot, C2D_TextBuf txtBuf,
                       bool isN3DS, s32 mainPrio, const char* pathA, const char* pathB) {
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
	audio_thread_start(mainPrio, isN3DS);   // dedicated audio thread on the core-1 slice
	if (anyCore) audio_set_rate(anyCore);   // shared rate, clean start

	GbaLink* link = gbalink_create();   // shared lockstep coordinator; cores attach on demand
	bool linkOn = false;
	int  touchMode = TOUCH_OFF;   // 0 off / 1 gamepad / 2 smart (touch drives the bottom game)
	bool fsOn = false;      // frameskip the unfocused game to free heavy-scene budget
	bool muted = false;     // HARD mute (stops audio rendering, saves CPU)

	int focused = 0;
	bool menuOpen = false;
	bool workersRunning = false;   // pipeline: a non-link frame is computing while we render the last
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
	float worstMs = 0.0f; int showMs = 0;   // worst frame work-time over the fps window
	u8  batLvl = 0;
	int batTimer = 0;

	// Audio: mode (solo/mixed/split) + per-game volume (0..256).
	int audioMode = AUD_SOLO;
	int volA = 256, volB = 256;

	settings_load(scaleMode, smooth, &swapped, &hudOn, &audioMode, &volA, &volB, &touchMode, &fsOn);   // restore prefs
	gbacore_set_frameskip(emuA.core, (fsOn && focused != 0) ? 2 : 0);   // unfocused-frameskip
	gbacore_set_frameskip(emuB.core, (fsOn && focused != 1) ? 2 : 0);

	while (aptMainLoop()) {
		if (!g_appActive) { svcSleepThread(16 * 1000 * 1000); continue; }   // backgrounded: don't hog the cores
		u64 wfStart = svcGetSystemTick();
		hidScanInput();
		u32 kDown = hidKeysDown();
		u32 kHeld = hidKeysHeld();
		u16 tk = 0;             // touch-injected keys for the bottom game this frame
		TouchSmart sm = { 0 };   // bottom game live state for SMART touch

		// HUD stats: FPS (0.5s window) + battery (throttled).
		fpsFrames++;
		u64 nowMs = osGetTime();
		if (nowMs - fpsT0 >= 500) { fps = (int)(fpsFrames * 1000 / (nowMs - fpsT0)); fpsFrames = 0; fpsT0 = nowMs;
		                            showMs = (int)(worstMs + 0.5f); worstMs = 0.0f; }
		if (s_hasPtm && --batTimer <= 0) { PTMU_GetBatteryLevel(&batLvl); batTimer = 60; }

		if (!menuOpen) {
			bool combo = (kHeld & KEY_START) && (kHeld & KEY_SELECT);
			// With the virtual gamepad on, the touchscreen drives the game, so the menu opens
			// only via the combo; otherwise a tap opens the menu (the original behaviour).
			if ((touchMode == TOUCH_OFF && (kDown & KEY_TOUCH)) || combo) {
				menuOpen = true; menuSel = 0; status[0] = '\0';
				if (!linkOn && workersRunning) {   // finish the in-flight frame before pausing into the menu
					LightEvent_Wait(&emuA.done); LightEvent_Wait(&emuB.done);
					if (emuA.core) upload_frame(&emuA); if (emuB.core) upload_frame(&emuB);
					workersRunning = false;
				}
			} else {
				// PIPELINE: finish the PREVIOUS frame (started last iteration, ran during the render) and
				// snapshot it. We render N-1 while N computes -> render isn't chained to the slower core,
				// so non-link is as smooth as the link path. Workers are parked here -> touch RAM access safe.
				if (!linkOn && workersRunning) {
					LightEvent_Wait(&emuA.done); LightEvent_Wait(&emuB.done);
					if (emuA.core) upload_frame(&emuA); if (emuB.core) upload_frame(&emuB);
					workersRunning = false;
				}
				if (kDown & KEY_Y) {                                         // switch focus
					focused ^= 1; audio_reset_stream();
					gbacore_set_frameskip(emuA.core, (fsOn && focused != 0) ? 2 : 0);
					gbacore_set_frameskip(emuB.core, (fsOn && focused != 1) ? 2 : 0);
				}
				if (kDown & KEY_X) {                                         // pause/resume focused game
					EmuInstance* fg = (focused == 0) ? &emuA : &emuB;
					fg->paused = !fg->paused;
					snprintf(toast, sizeof toast, "Game %c %s", focused == 0 ? 'A' : 'B',
					         fg->paused ? "paused" : "resumed");
					toastTimer = 90;
				}
				int fs = swapped ? (focused ^ 1) : focused;   // screen the focused game sits on
				if (kDown & KEY_ZR) {
					scaleMode[fs] = (scaleMode[fs] + 1) % 3;
					snprintf(toast, sizeof toast, "%s scale: %s",
					         fs == 0 ? "Top" : "Bottom", SCALE_NAMES[scaleMode[fs]]);
					toastTimer = 90;
					settings_save(scaleMode, smooth, swapped, hudOn, audioMode, volA, volB, touchMode, fsOn);
				}
				if (kDown & KEY_ZL) {
					smooth[fs] = !smooth[fs];   // render_game sets the per-pass filters
					snprintf(toast, sizeof toast, "%s filter: %s", fs == 0 ? "Top" : "Bottom",
					         smooth[fs] ? "Smooth" : "Sharp-bilinear");
					toastTimer = 90;
					settings_save(scaleMode, smooth, swapped, hudOn, audioMode, volA, volB, touchMode, fsOn);
				}
				u16 g = to_gba_keys(kHeld);
				// Touchscreen drives the BOTTOM game (A is on bottom iff swapped) as a POINTER on the
				// real game UI. touch_update is stateful (menu-cursor driver + tap-to-walk) -> run it
				// every gameplay frame when enabled.
				if (touchMode != TOUCH_OFF) {
					bool touching = (kHeld & KEY_TOUCH) != 0;
					touchPosition tp = { 0, 0 };
					if (touching) hidTouchRead(&tp);
					int gx = -1, gy = -1; bool gvalid = false;
					if (touchMode == TOUCH_SMART && !linkOn) {   // skip cross-thread RAM read during a link
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
						}
					}
					tk = touch_update(touchMode, touching, tp.px, tp.py, gx, gy, gvalid, &sm);
				}
				emuA.keys = ((focused == 0) ? g : 0) | (swapped ? tk : 0);
				emuB.keys = ((focused == 1) ? g : 0) | (swapped ? 0 : tk);
				if (linkOn) {
					// Workers free-run + pump their own audio rings; main just samples the latest frames
					// and stays responsive. Audio keeps playing during a link (rings are worker-private).
					if (emuA.core) upload_frame(&emuA);
					if (emuB.core) upload_frame(&emuB);
				} else {
					LightEvent_Signal(&emuA.go);   // start THIS frame; it is waited at the top of next iter
					LightEvent_Signal(&emuB.go);   // (render below overlaps this emulation)
					workersRunning = true;
				}
				// The audio thread (core 1) mixes the worker-filled rings + feeds ndsp off the frame path.
				audio_set_params(focused, audioMode, volA, volB);
			}
		} else {
			bool onAudio = (menuSel == MENU_AUDIO_IDX);
			if (kDown & (KEY_DDOWN | KEY_CPAD_DOWN)) { if (menuSel + 2 < MENU_N) menuSel += 2; }
			if (kDown & (KEY_DUP   | KEY_CPAD_UP))   { if (menuSel - 2 >= 0)      menuSel -= 2; }
			if (!onAudio && (kDown & (KEY_DRIGHT | KEY_CPAD_RIGHT))) { if (menuSel + 1 < MENU_N) menuSel++; }
			if (!onAudio && (kDown & (KEY_DLEFT  | KEY_CPAD_LEFT)))  { if (menuSel - 1 >= 0)      menuSel--; }
			if (menuSel == MENU_AUDIO_IDX && (kDown & (KEY_DLEFT | KEY_CPAD_LEFT | KEY_DRIGHT | KEY_CPAD_RIGHT))) {
					int* v = (focused == 0) ? &volA : &volB;   // adjust the focused game's volume
					*v += (kDown & (KEY_DRIGHT | KEY_CPAD_RIGHT)) ? 32 : -32;
					if (*v < 0) *v = 0; else if (*v > 256) *v = 256;
					snprintf(status, sizeof status, "Vol %c: %d%%", focused == 0 ? 'A' : 'B', *v * 100 / 256);
					settings_save(scaleMode, smooth, swapped, hudOn, audioMode, volA, volB, touchMode, fsOn);
				}
				if (kDown & KEY_B) menuOpen = false;                 // resume
			bool activate = (kDown & KEY_A) != 0;
			if (kDown & KEY_TOUCH) {   // tap a button to select it
				touchPosition mtp; hidTouchRead(&mtp);
				for (int i = 0; i < MENU_N; i++) {
					float bx = 4.0f + (i & 1) * 160.0f, by = 4.0f + (i >> 1) * 32.0f;
					if (mtp.px >= bx && mtp.px < bx + 152 && mtp.py >= by && mtp.py < by + 29) { menuSel = i; activate = true; break; }
				}
			}
			if (activate) {
				if      (menuSel == 0) menuOpen = false;         // Resume
				else if (menuSel == 1) {                         // Link cable (experimental)
					if (!emuA.core || !emuB.core || !link) {
						snprintf(status, sizeof status, "Link needs 2 games");
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
					settings_save(scaleMode, smooth, swapped, hudOn, audioMode, volA, volB, touchMode, fsOn);
				}
				else if (menuSel == 3) {                         // Touch mode (off / gamepad / smart)
					touchMode = (touchMode + 1) % 3;
					snprintf(status, sizeof status, "Touch: %s", TOUCH_NAMES[touchMode]);
					settings_save(scaleMode, smooth, swapped, hudOn, audioMode, volA, volB, touchMode, fsOn);
				}
				else if (menuSel == 4) {                         // Frameskip (unfocused game)
					fsOn = !fsOn;
					gbacore_set_frameskip(emuA.core, (fsOn && focused != 0) ? 2 : 0);
					gbacore_set_frameskip(emuB.core, (fsOn && focused != 1) ? 2 : 0);
					snprintf(status, sizeof status, "Frameskip %s", fsOn ? "on" : "off");
					settings_save(scaleMode, smooth, swapped, hudOn, audioMode, volA, volB, touchMode, fsOn);
				}
				else if (menuSel == 5) {                         // Toggle HUD
					hudOn = !hudOn;
					snprintf(status, sizeof status, "HUD %s", hudOn ? "on" : "off");
					settings_save(scaleMode, smooth, swapped, hudOn, audioMode, volA, volB, touchMode, fsOn);
				}
				else if (menuSel == 6) {                         // Swap screens
					swapped = !swapped; menuOpen = false;
					snprintf(toast, sizeof toast, "Layout: %s", swapped ? "B top / A bottom" : "A top / B bottom");
					toastTimer = 90;
					settings_save(scaleMode, smooth, swapped, hudOn, audioMode, volA, volB, touchMode, fsOn);
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
				else if (menuSel == 12) { result = SESSION_CHANGE; break; }
				else                    { result = SESSION_QUIT;   break; }
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
			snprintf(hudStat, sizeof hudStat, "%s  %dfps %dms  %02d:%02d  %d/5",
			         linkOn ? "LINK" : AUDIO_NAMES[audioMode], fps, showMs, lt ? lt->tm_hour : 0, lt ? lt->tm_min : 0, batLvl);
			C2D_TextParse(&tHudTop,  txtBuf, topName);  C2D_TextOptimize(&tHudTop);
			C2D_TextParse(&tHudBot,  txtBuf, botName);  C2D_TextOptimize(&tHudBot);
			C2D_TextParse(&tHudStat, txtBuf, hudStat);  C2D_TextOptimize(&tHudStat);
		}
		if (menuOpen) {
			char alabel[40], llabel[24];
			for (int i = 0; i < MENU_N; i++) {
				const char* label = MENU_ITEMS[i];
				if (i == MENU_AUDIO_IDX) {   // dynamic: "Audio: Mixed  A100 B80"
					snprintf(alabel, sizeof alabel, "Audio: %s  A%d B%d",
					         AUDIO_NAMES[audioMode], volA * 100 / 256, volB * 100 / 256);
					label = alabel;
				} else if (i == MENU_LINK_IDX) {   // dynamic: "Link: off/ON (beta)"
					snprintf(llabel, sizeof llabel, "Link: %s", linkOn ? "ON (beta)" : "off");
					label = llabel;
				
				} else if (i == MENU_TOUCH_IDX) {
					snprintf(llabel, sizeof llabel, "Touch: %s", TOUCH_NAMES[touchMode]);
					label = llabel;
				} else if (i == MENU_FS_IDX) {
					snprintf(llabel, sizeof llabel, "Frameskip: %s", fsOn ? "on" : "off");
					label = llabel;
				} else if (i == MENU_MUTE_IDX) {
					snprintf(llabel, sizeof llabel, "Mute: %s", muted ? "on" : "off");
					label = llabel;
				} else if (i == MENU_PAUSE_IDX) {
					bool fp = (focused == 0) ? emuA.paused : emuB.paused;
					snprintf(llabel, sizeof llabel, "Pause %c: %s", focused == 0 ? 'A' : 'B', fp ? "on" : "off");
					label = llabel;
				}
				C2D_TextParse(&items[i], txtBuf, label); C2D_TextOptimize(&items[i]);
			}
			// Footer: last action result, or a controls cheat-sheet when idle.
			C2D_TextParse(&tStatus, txtBuf,
			              status[0] ? status : "Y focus  X pause  ZL/ZR filter/scale  L/R = GBA");
			C2D_TextOptimize(&tStatus);
		} else {
			C2D_TextParse(&tHint, txtBuf, touchMode != TOUCH_OFF ? "START+SELECT: menu  (touch drives bottom game)"
			                                      : "tap / START+SELECT: menu");
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
				C2D_DrawRectSolid(0.0f, 0.0f, 0.0f, 400.0f, 14.0f, THEME_HUD_BAR);
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
				C2D_DrawRectSolid(0.0f, 0.0f, 0.0f, 320.0f, 14.0f, THEME_HUD_BAR);
				if (focScreen == 1) C2D_DrawRectSolid(0.0f, 14.0f, 0.0f, 320.0f, 2.0f, clrHi);
				C2D_DrawText(&tHudBot, C2D_WithColor, 4.0f, 1.0f, 0.0f, 0.4f, 0.4f, clrTxt);
				float bsw, bsh; C2D_TextGetDimensions(&tHudStat, 0.4f, 0.4f, &bsw, &bsh);
				C2D_DrawText(&tHudStat, C2D_WithColor, 316.0f - bsw, 1.0f, 0.0f, 0.4f, 0.4f, clrTxt);
			} else if (focScreen == 1) {
				C2D_DrawRectSolid(0.0f, 0.0f, 0.0f, 320.0f, 4.0f, clrHi);
			}
			if (touchMode == TOUCH_PAD) {   // virtual gamepad overlay (SMART draws nothing -> real UI)
				touch_draw(touchMode, tk, &sm);
			}
			if (touchMode == TOUCH_SMART && !linkOn && sm.valid) {   // TEMP debug: confirm RAM reads on device
				static const char* const CTXN[] = { "none", "field", "b.act", "b.move", "b.other" };
				char gs[64]; snprintf(gs, sizeof gs, "%s xy=%d,%d a=%d m=%d",
				         CTXN[sm.ctx], sm.px, sm.py, sm.actionCursor, sm.moveCursor);
				C2D_Text tg; C2D_TextParse(&tg, txtBuf, gs); C2D_TextOptimize(&tg);
				C2D_DrawText(&tg, C2D_WithColor, 4.0f, 224.0f, 0.0f, 0.40f, 0.40f, C2D_Color32(0x42, 0xF5, 0xD0, 0xFF));
			}
		}
		if (menuOpen) {
			C2D_DrawRectSolid(0.0f, 0.0f, 0.0f, 320.0f, 240.0f, clrDim);
			for (int i = 0; i < MENU_N; i++) {   // 2x7 grid of buttons (D-pad or tap to select)
				float bx = 4.0f + (i & 1) * 160.0f, by = 4.0f + (i >> 1) * 32.0f;
				bool s = (i == menuSel);
				C2D_DrawRectSolid(bx, by, 0.0f, 152.0f, 29.0f, s ? clrHi : clrPanel);
				C2D_DrawText(&items[i], C2D_WithColor, bx + 6.0f, by + 7.0f, 0.0f, 0.4f, 0.4f, s ? clrSelTxt : clrTxt);
			}
			C2D_DrawText(&tStatus, C2D_WithColor, 4.0f, 228.0f, 0.0f, 0.35f, 0.35f, clrTxt);
		} else {
			C2D_DrawText(&tHint, C2D_WithColor, 6.0f, 224.0f, 0.0f, 0.4f, 0.4f, clrTxt);
		}

		{ float wms = (svcGetSystemTick() - wfStart) * 1000.0f / SYSCLOCK_ARM11; if (wms > worstMs) worstMs = wms; }
		C3D_FrameEnd(0);
		// Real-time cap to the 3DS LCD refresh (the rate audio is matched to): freed-up CPU must not run
		// the games + audio faster than 60fps. Only waits when UNDER budget, so heavy frames are untouched.
		while (svcGetSystemTick() - wfStart < FRAME_TICKS) svcSleepThread(100000);
		if (toastTimer > 0) toastTimer--;
	}

	// teardown this session's workers + cores; reset g_quit for the next session
	emuA.linked = emuB.linked = false;        // stop the free-run loop
	g_quit = true;
	LightEvent_Signal(&emuA.waitEv);          // release any worker parked on a link wait
	LightEvent_Signal(&emuB.waitEv);
	LightEvent_Signal(&emuA.go);
	LightEvent_Signal(&emuB.go);
	if (emuA.thread) { threadJoin(emuA.thread, U64_MAX); threadFree(emuA.thread); }
	if (emuB.thread) { threadJoin(emuB.thread, U64_MAX); threadFree(emuB.thread); }
	audio_thread_stop();   // workers joined -> nothing pumps the rings; safe to stop audio + free them
	if (linkOn) { gbacore_link_detach(emuA.core); gbacore_link_detach(emuB.core); }
	teardown_core(&emuA);
	teardown_core(&emuB);
	gbalink_destroy(link);
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

static void run_splash(C3D_RenderTarget* top, C3D_RenderTarget* bot, C2D_TextBuf txtBuf, const char* warn) {
	const u32 bg   = THEME_BG;
	const u32 gold = THEME_GOLD;
	const u32 grn  = THEME_GAME_A;
	const u32 blu  = THEME_GAME_B;
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
		if (warn && warn[0]) {   // perf warning (Old 3DS, or New 3DS not at full speed)
			C2D_Text tw; C2D_TextParse(&tw, txtBuf, warn); C2D_TextOptimize(&tw);
			C2D_DrawText(&tw, C2D_WithColor, 6.0f, 4.0f, 0.0f, 0.42f, 0.42f, C2D_Color32(0xFF, 0x80, 0x40, 0xFF));
		}
		if (fade > 0.0f) C2D_DrawRectSolid(0, 0, 0, 320, 240, C2D_Color32(0x18, 0x11, 0x28, (u8)(fade * 255)));

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

	char perfWarn[160] = "";
	if (!isN3DS)
		snprintf(perfWarn, sizeof perfWarn,
		         "Old 3DS detected.\nTwo GBA cores need a New 3DS - expect slowdown.");
	else if (!speedupActive)
		snprintf(perfWarn, sizeof perfWarn,
		         "Running SLOW (no 804MHz / L2).\nInstall + run the .CIA for full speed.");

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

	run_splash(top, bot, txtBuf, perfWarn);   // animated boot splash (skippable) + perf warning

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
