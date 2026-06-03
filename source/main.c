// dual-gba (3DS) — v0.3: two real mGBA cores, one per screen.
// -----------------------------------------------------------------------------
// Worker A hosts game A on the top screen, worker B hosts game B on the bottom
// screen, each a self-contained mGBA core pinned to its own CPU core. Per-frame
// LightEvent handshake, dual-screen citro2d rendering, X/Y focus toggle.
// NOTE: the real two-core performance budget is only meaningful on New 3DS
// hardware (the v0.4 gate) — Azahar's nested emulation is not a valid signal.
// -----------------------------------------------------------------------------

#include <3ds.h>
#include <citro2d.h>
#include <citro3d.h>
#include <malloc.h>
#include <string.h>
#include <stdio.h>

#include "gbacore.h"

#define WORKER_STACKSIZE (32 * 1024)

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
// Call after emu_start (which zeroes the instance) and before the first frame signal.
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

// Upload an instance's linear RGB565 framebuffer into its tiled GPU texture.
static void upload_frame(EmuInstance* e) {
	GSPGPU_FlushDataCache(e->fb, GBA_FB_STRIDE * GBA_H * sizeof(u16));
	C3D_SyncDisplayTransfer(
		(u32*)e->fb,       GX_BUFFER_DIM(GBA_FB_STRIDE, GBA_H),
		(u32*)e->tex.data, GX_BUFFER_DIM(256, 256),
		GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGB565) |
		GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB565) |
		GX_TRANSFER_OUT_TILED(1) | GX_TRANSFER_FLIP_VERT(0));
}

// Draw an instance's game texture aspect-fit + centered on a screen.
static void draw_game(EmuInstance* e, float screenW, float screenH) {
	Tex3DS_SubTexture sub = {
		.width = GBA_W, .height = GBA_H,
		.left = 0.0f, .top = 1.0f,
		.right = (float)GBA_W / 256.0f,
		.bottom = 1.0f - (float)GBA_H / 256.0f,
	};
	C2D_Image img = { .tex = &e->tex, .subtex = &sub };
	float sx = screenW / GBA_W, sy = screenH / GBA_H;
	float scale = sx < sy ? sx : sy;
	float x = (screenW - GBA_W * scale) / 2.0f;
	float y = (screenH - GBA_H * scale) / 2.0f;
	C2D_DrawImageAt(img, x, y, 0.0f, NULL, scale, scale);
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
	C3D_RenderTarget* top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
	C3D_RenderTarget* bot = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);

	const u32 clrBg  = C2D_Color32(0x10, 0x10, 0x10, 0xFF);
	const u32 clrHi  = C2D_Color32(0xF5, 0xD0, 0x42, 0xFF);
	const u32 clrTxt = C2D_Color32(0xFF, 0xFF, 0xFF, 0xFF);

	C2D_TextBuf txtBuf = C2D_TextBufNew(256);

	s32 mainPrio = 0x30;
	svcGetThreadPriority(&mainPrio, CUR_THREAD_HANDLE);
	EmuInstance emuA, emuB;
	emu_start(&emuA, 0, 0,                  mainPrio + 1);
	emu_start(&emuB, 1, isN3DS ? 2 : 1,     mainPrio + 1);

	// v0.3: a real mGBA core per screen (each owns its ROM — no FIXED_ROM_BUFFER).
	bool okA = setup_core(&emuA, "sdmc:/dual-gba/gameA.gba");
	bool okB = setup_core(&emuB, "sdmc:/dual-gba/gameB.gba");

	int focused = 0; // which game receives input

	while (aptMainLoop()) {
		hidScanInput();
		u32 kDown = hidKeysDown();
		u32 kHeld = hidKeysHeld();
		if (kDown & KEY_START) break;
		if (kDown & (KEY_X | KEY_Y)) focused ^= 1;

		u16 gbaKeys = to_gba_keys(kHeld);
		emuA.keys = (focused == 0) ? gbaKeys : 0;
		emuB.keys = (focused == 1) ? gbaKeys : 0;

		LightEvent_Signal(&emuA.go);
		LightEvent_Signal(&emuB.go);
		LightEvent_Wait(&emuA.done);
		LightEvent_Wait(&emuB.done);

		// Build the small core-readout text once per frame (single buffer).
		C2D_TextBufClear(txtBuf);
		char line[64];
		snprintf(line, sizeof line, "A core:%ld %s   B core:%ld %s",
		         (long)emuA.core_id, okA ? "" : "(no rom)",
		         (long)emuB.core_id, okB ? "" : "(no rom)");
		C2D_Text txtReadout;
		C2D_TextParse(&txtReadout, txtBuf, line);
		C2D_TextOptimize(&txtReadout);

		// Texture uploads happen outside the citro3d frame (GPU idle).
		if (emuA.core) upload_frame(&emuA);
		if (emuB.core) upload_frame(&emuB);

		C3D_FrameBegin(C3D_FRAME_SYNCDRAW);

		// --- Top screen: Game A ---
		C2D_TargetClear(top, clrBg);
		C2D_SceneBegin(top);
		if (emuA.core) draw_game(&emuA, 400.0f, 240.0f);
		if (focused == 0) C2D_DrawRectSolid(0.0f, 0.0f, 0.0f, 400.0f, 6.0f, clrHi);

		// --- Bottom screen: Game B (+ small readout in the top letterbox) ---
		C2D_TargetClear(bot, clrBg);
		C2D_SceneBegin(bot);
		if (emuB.core) draw_game(&emuB, 320.0f, 240.0f);
		if (focused == 1) C2D_DrawRectSolid(0.0f, 0.0f, 0.0f, 320.0f, 6.0f, clrHi);
		C2D_DrawText(&txtReadout, C2D_WithColor, 4.0f, 0.0f, 0.0f, 0.4f, 0.4f, clrTxt);

		C3D_FrameEnd(0);
	}

	g_quit = true;
	LightEvent_Signal(&emuA.go);
	LightEvent_Signal(&emuB.go);
	if (emuA.thread) { threadJoin(emuA.thread, U64_MAX); threadFree(emuA.thread); }
	if (emuB.thread) { threadJoin(emuB.thread, U64_MAX); threadFree(emuB.thread); }

	teardown_core(&emuA);
	teardown_core(&emuB);

	C2D_TextBufDelete(txtBuf);
	C2D_Fini();
	C3D_Fini();
	gfxExit();
	return 0;
}
