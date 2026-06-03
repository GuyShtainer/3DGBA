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

// ---- Pause menu ----
enum { SESSION_CHANGE, SESSION_QUIT };
static const char* MENU_ITEMS[] = {
	"Resume", "Save A", "Load A", "Save B", "Load B", "Change games", "Quit"
};
#define MENU_N 7

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

	int focused = 0;
	bool menuOpen = false;
	int  menuSel = 0;
	int  result = SESSION_QUIT;
	char status[24] = "";   // last save/load result, shown in the menu

	while (aptMainLoop()) {
		hidScanInput();
		u32 kDown = hidKeysDown();
		u32 kHeld = hidKeysHeld();

		if (!menuOpen) {
			bool combo = (kHeld & KEY_START) && (kHeld & KEY_SELECT);
			if ((kDown & KEY_TOUCH) || combo) {
				menuOpen = true; menuSel = 0; status[0] = '\0';
			} else {
				if (kDown & (KEY_X | KEY_Y)) focused ^= 1;
				u16 g = to_gba_keys(kHeld);
				emuA.keys = (focused == 0) ? g : 0;
				emuB.keys = (focused == 1) ? g : 0;
				LightEvent_Signal(&emuA.go);
				LightEvent_Signal(&emuB.go);
				LightEvent_Wait(&emuA.done);
				LightEvent_Wait(&emuB.done);
				if (emuA.core) upload_frame(&emuA);
				if (emuB.core) upload_frame(&emuB);
			}
		} else {
			if (kDown & (KEY_DDOWN | KEY_CPAD_DOWN)) menuSel = (menuSel + 1) % MENU_N;
			if (kDown & (KEY_DUP   | KEY_CPAD_UP))   menuSel = (menuSel - 1 + MENU_N) % MENU_N;
			if (kDown & KEY_B) menuOpen = false;                 // resume
			if (kDown & KEY_A) {
				if      (menuSel == 0) menuOpen = false;         // Resume
				else if (menuSel == 1) snprintf(status, sizeof status, "%s", gbacore_save_state(emuA.core, 1) ? "Saved A"  : "Save A failed");
				else if (menuSel == 2) snprintf(status, sizeof status, "%s", gbacore_load_state(emuA.core, 1) ? "Loaded A" : "No state A");
				else if (menuSel == 3) snprintf(status, sizeof status, "%s", gbacore_save_state(emuB.core, 1) ? "Saved B"  : "Save B failed");
				else if (menuSel == 4) snprintf(status, sizeof status, "%s", gbacore_load_state(emuB.core, 1) ? "Loaded B" : "No state B");
				else if (menuSel == 5) { result = SESSION_CHANGE; break; }
				else                   { result = SESSION_QUIT;   break; }
			}
		}

		// ---- text for this frame (single buffer; cleared once) ----
		C2D_TextBufClear(txtBuf);
		C2D_Text items[MENU_N], tHint, tStatus;
		if (menuOpen) {
			for (int i = 0; i < MENU_N; i++) { C2D_TextParse(&items[i], txtBuf, MENU_ITEMS[i]); C2D_TextOptimize(&items[i]); }
			if (status[0]) { C2D_TextParse(&tStatus, txtBuf, status); C2D_TextOptimize(&tStatus); }
		} else {
			C2D_TextParse(&tHint, txtBuf, "tap or START+SELECT for menu");
			C2D_TextOptimize(&tHint);
		}

		C3D_FrameBegin(C3D_FRAME_SYNCDRAW);

		// top: game A
		C2D_TargetClear(top, clrBg);
		C2D_SceneBegin(top);
		if (emuA.core) draw_game(&emuA, 400.0f, 240.0f);
		if (!menuOpen && focused == 0) C2D_DrawRectSolid(0.0f, 0.0f, 0.0f, 400.0f, 6.0f, clrHi);

		// bottom: game B (+ menu overlay when open)
		C2D_TargetClear(bot, clrBg);
		C2D_SceneBegin(bot);
		if (emuB.core) draw_game(&emuB, 320.0f, 240.0f);
		if (!menuOpen && focused == 1) C2D_DrawRectSolid(0.0f, 0.0f, 0.0f, 320.0f, 6.0f, clrHi);
		if (menuOpen) {
			C2D_DrawRectSolid(0.0f, 0.0f, 0.0f, 320.0f, 240.0f, clrDim);
			C2D_DrawRectSolid(34.0f, 8.0f, 0.0f, 252.0f, 224.0f, clrPanel);
			for (int i = 0; i < MENU_N; i++) {
				float y = 16.0f + i * 26.0f;
				bool s = (i == menuSel);
				if (s) C2D_DrawRectSolid(44.0f, y - 2.0f, 0.0f, 232.0f, 24.0f, clrHi);
				C2D_DrawText(&items[i], C2D_WithColor, 56.0f, y, 0.0f, 0.6f, 0.6f, s ? clrSelTxt : clrTxt);
			}
			if (status[0]) C2D_DrawText(&tStatus, C2D_WithColor, 44.0f, 204.0f, 0.0f, 0.5f, 0.5f, clrTxt);
		} else {
			C2D_DrawText(&tHint, C2D_WithColor, 6.0f, 224.0f, 0.0f, 0.4f, 0.4f, clrTxt);
		}

		C3D_FrameEnd(0);
	}

	// teardown this session's workers + cores; reset g_quit for the next session
	g_quit = true;
	LightEvent_Signal(&emuA.go);
	LightEvent_Signal(&emuB.go);
	if (emuA.thread) { threadJoin(emuA.thread, U64_MAX); threadFree(emuA.thread); }
	if (emuB.thread) { threadJoin(emuB.thread, U64_MAX); threadFree(emuB.thread); }
	teardown_core(&emuA);
	teardown_core(&emuB);
	g_quit = false;
	return result;
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
	C2D_TextBuf txtBuf = C2D_TextBufNew(4096);

	s32 mainPrio = 0x30;
	svcGetThreadPriority(&mainPrio, CUR_THREAD_HANDLE);

	// Session loop: pick two ROMs, play, and on "Change games" pick again.
	while (aptMainLoop()) {
		char pathA[256], pathB[256];
		if (!rompicker_run(top, bot, txtBuf, pathA, pathB, sizeof pathA)) {
			strcpy(pathA, "sdmc:/dual-gba/gameA.gba");
			strcpy(pathB, "sdmc:/dual-gba/gameB.gba");
		}
		int r = run_session(top, bot, txtBuf, isN3DS, mainPrio, pathA, pathB);
		if (r == SESSION_QUIT) break;
		// SESSION_CHANGE -> loop back to the picker
	}

	C2D_TextBufDelete(txtBuf);
	C2D_Fini();
	C3D_Fini();
	gfxExit();
	return 0;
}
