// 3DS Tool Template
// -----------------------------------------------------------------------------
// A runnable skeleton for a dual-"core" 3DS app: two worker threads (each meant
// to host one emulator instance), one per screen, with an X/Y control toggle.
//
// What's real here: thread creation + core pinning, the LightEvent frame
// handshake, the New 3DS speedup, dual-screen citro2d rendering, and input
// focus toggling. What's a stub: emu_step(), which just animates a box. Replace
// its body with mGBA's setKeys()/runFrame() and swap the box for the emulated
// framebuffer uploaded as a GPU texture.
// -----------------------------------------------------------------------------

#include <3ds.h>
#include <citro2d.h>
#include <citro3d.h>
#include <string.h>

#define WORKER_STACKSIZE (32 * 1024)

// One emulated system. In the real project this wraps an mGBA core + its
// framebuffer/audio ring. The coordinator reads `frame` after `done` fires.
typedef struct {
	int        id;
	Thread     thread;
	LightEvent go;      // coordinator -> worker: "produce the next frame"
	LightEvent done;    // worker -> coordinator: "frame ready"
	volatile u32  frame;  // stand-in for emulated video output
	volatile u32  keys;   // input handed in by the coordinator (neutral if unfocused)
	volatile bool skip;   // per-instance frameskip request (set when a core overruns)
} EmuInstance;

static volatile bool g_quit = false;

// ---- Emulate one frame. Replace this body. --------------------------------
// Real version, roughly:
//     core->setKeys(core, e->keys);
//     mCoreRunFrame(core);                 // ~16.78M GBA cycles of work
//     convert core framebuffer -> tiled GPU texture in this instance's back buffer
static void emu_step(EmuInstance* e) {
	e->frame++;
}

// ---- Worker thread: one per emulated system, pinned to its own CPU core ----
static void worker_main(void* arg) {
	EmuInstance* e = (EmuInstance*)arg;
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
	LightEvent_Init(&e->go,   RESET_ONESHOT);
	LightEvent_Init(&e->done, RESET_ONESHOT);
	e->thread = threadCreate(worker_main, e, WORKER_STACKSIZE, prio, core, false);
	if (!e->thread) {
		// Requested core unavailable (e.g. core 2 without a CIA build / on Old 3DS):
		// fall back to letting the scheduler place it.
		e->thread = threadCreate(worker_main, e, WORKER_STACKSIZE, prio, -2, false);
	}
}

int main(int argc, char** argv) {
	// --- New 3DS: unlock 804 MHz + L2 cache. Claim syscore (core 1) time too. ---
	bool isN3DS = false;
	APT_CheckNew3DS(&isN3DS);
	if (isN3DS) osSetSpeedupEnable(true);
	APT_SetAppCpuTimeLimit(80);

	// --- Both screens via citro2d ---
	gfxInitDefault();
	C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
	C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
	C2D_Prepare();
	C3D_RenderTarget* top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
	C3D_RenderTarget* bot = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);

	const u32 clrBg = C2D_Color32(0x10, 0x10, 0x10, 0xFF);
	const u32 clrA  = C2D_Color32(0x3A, 0x7B, 0xD5, 0xFF);
	const u32 clrB  = C2D_Color32(0xD5, 0x6A, 0x3A, 0xFF);
	const u32 clrHi = C2D_Color32(0xF5, 0xD0, 0x42, 0xFF);

	// --- Spawn the two emulator workers ---
	// Core 0 hosts this (light) coordinator, so worker A shares it: the blocking
	// handshake means A gets ~all of core 0 while the coordinator sleeps. Worker B
	// gets full core 2 on a New 3DS (CIA build needed for guaranteed access),
	// otherwise core 1. Workers run a notch below the coordinator so it can always
	// preempt to present.
	s32 mainPrio = 0x30;
	svcGetThreadPriority(&mainPrio, CUR_THREAD_HANDLE);
	EmuInstance emuA, emuB;
	emu_start(&emuA, 0, 0,                  mainPrio + 1);
	emu_start(&emuB, 1, isN3DS ? 2 : 1,     mainPrio + 1);

	int focused = 0; // which game currently receives input

	// --- Coordinator loop (main thread, core 0) ---
	while (aptMainLoop()) {
		hidScanInput();
		u32 kDown = hidKeysDown();
		u32 kHeld = hidKeysHeld();
		if (kDown & KEY_START) break;
		if (kDown & (KEY_X | KEY_Y)) focused ^= 1;

		// Focused game gets live input; the other keeps running with neutral input.
		// (Real project: remap these 3DS bits to GBA keypad bits.)
		emuA.keys = (focused == 0) ? kHeld : 0;
		emuB.keys = (focused == 1) ? kHeld : 0;

		// Kick both workers in parallel, then gather. The real loop adds a deadline:
		// if a worker is late, set its .skip for next frame instead of stalling.
		LightEvent_Signal(&emuA.go);
		LightEvent_Signal(&emuB.go);
		LightEvent_Wait(&emuA.done);
		LightEvent_Wait(&emuB.done);

		// All GPU work happens here on the one render thread.
		C3D_FrameBegin(C3D_FRAME_SYNCDRAW);

		C2D_TargetClear(top, clrBg);
		C2D_SceneBegin(top);
		C2D_DrawRectSolid(20.0f + (float)(emuA.frame % 300), 90.0f, 0.0f, 60.0f, 60.0f, clrA);
		if (focused == 0) C2D_DrawRectSolid(0.0f, 0.0f, 0.0f, 400.0f, 6.0f, clrHi);

		C2D_TargetClear(bot, clrBg);
		C2D_SceneBegin(bot);
		C2D_DrawRectSolid(20.0f + (float)(emuB.frame % 240), 90.0f, 0.0f, 60.0f, 60.0f, clrB);
		if (focused == 1) C2D_DrawRectSolid(0.0f, 0.0f, 0.0f, 320.0f, 6.0f, clrHi);

		C3D_FrameEnd(0);
	}

	// --- Shutdown: wake workers so they observe g_quit and exit ---
	g_quit = true;
	LightEvent_Signal(&emuA.go);
	LightEvent_Signal(&emuB.go);
	if (emuA.thread) { threadJoin(emuA.thread, U64_MAX); threadFree(emuA.thread); }
	if (emuB.thread) { threadJoin(emuB.thread, U64_MAX); threadFree(emuB.thread); }

	C2D_Fini();
	C3D_Fini();
	gfxExit();
	return 0;
}
