// 3DGBA — two Game Boy Advance games at once on a New 3DS, with an emulated link cable.
// Copyright (C) 2026 Guy Shtainer.  Free software under the GNU GPL v3 — see the LICENSE file.
// Distributed WITHOUT ANY WARRANTY. Bundles mGBA (MPL-2.0); ships no games or Nintendo content.
// -----------------------------------------------------------------------------
// 3DGBA (3DS) — two real mGBA cores, one per screen, with a ROM picker and an
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
#include <math.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "gbacore.h"
#include "rompicker.h"
#include "theme.h"
#include "gamestate.h"
#include "touch.h"
#include "audio.h"
#include "netlink.h"
#include "wireless.h"
#include "warp_shbin.h"   // M2 grid-warp vertex shader (generated from source/warp.v.pica)

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
	volatile bool netLinked;  // free-run under the M2.5 net SIO driver (mutually exclusive with linked)
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
	if (t == APTHOOK_ONSUSPEND || t == APTHOOK_ONSLEEP) {
		g_appActive = false;
		// FULLY shut UDS down NOW — the session AND udsExit. ONSUSPEND fires inside aptMainLoop while we
		// are still foreground enough for nwm to service every IPC. ANY UDS call left for after the system
		// reclaims the radio (HOME->Close, or launching another app and confirming "Close 3DGBA?") blocks
		// on nwm and hangs the close forever — and udsExit, not just net_session_close, is on that path.
		netlink_exit();   // net_session_close() + udsExit(); re-init on resume below
	} else if (t == APTHOOK_ONEXIT) {
		netlink_exit();   // idempotent (s_inited guard); covers any close path that skipped ONSUSPEND
	} else if (t == APTHOOK_ONRESTORE || t == APTHOOK_ONWAKEUP) {
		g_appActive = true;
		netlink_init();   // bring UDS back up after a HOME/sleep resume so wireless still works this session
	}
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
			u64 paceDl = svcGetSystemTick();
			u32 lastVf = gbacore_frame_counter(e->core);
			while (e->linked && !g_quit) {
				gbacore_set_keys(e->core, (u16)e->keys);
				gbacore_run_loop(e->core);
				e->frame++;
				audio_pump_core(e->id, e->core);   // drain this core's audio into its ring (link audio)
				if (e->wantWait) { e->wantWait = false; LightEvent_Wait(&e->waitEv); }
				// Real-time pace: cap each PRODUCED video frame to the 3DS refresh so the lockstepped
				// pair runs at ~60fps (it free-runs -> too fast otherwise). Only the wall-clock rate is
				// touched; the lockstep transfer ordering (trade correctness) is unchanged. Paces between
				// transactions; the short sleep rechecks linked/quit so a link-off exits promptly.
				u32 vf = gbacore_frame_counter(e->core);
				if (vf != lastVf) {
					lastVf = vf;
					paceDl += FRAME_TICKS;
					u64 now = svcGetSystemTick();
					if (now > paceDl) paceDl = now;   // behind schedule: don't accumulate debt
					else while (e->linked && !g_quit && svcGetSystemTick() < paceDl) svcSleepThread(400000);
				}
			}
		} else if (e->netLinked && e->core) {
			// M2.5 net link: like the lockstep free-run, but the per-transfer STALL is
			// net_transfer_collect() inside the driver's finishMultiplayer (which runs in
			// gbacore_run_loop on THIS core's thread) — so there's no waitEv park here.
			// gbacore_net_poll lets a passive child notice a parent-initiated round and
			// self-schedule its completeEvent; it's a no-op on the parent (seat 0).
			u64 paceDl = svcGetSystemTick();
			u32 lastVf = gbacore_frame_counter(e->core);
			while (e->netLinked && !g_quit) {
				gbacore_set_keys(e->core, (u16)e->keys);
				gbacore_net_poll(e->core);
				gbacore_run_loop(e->core);
				e->frame++;
				audio_pump_core(e->id, e->core);
				u32 vf = gbacore_frame_counter(e->core);
				if (vf != lastVf) {
					lastVf = vf;
					paceDl += FRAME_TICKS;
					u64 now = svcGetSystemTick();
					if (now > paceDl) paceDl = now;
					else while (e->netLinked && !g_quit && svcGetSystemTick() < paceDl) svcSleepThread(400000);
				}
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

// HD-2D M1 (tilt-shift DoF): no fragment shader on the PICA200, so "blur" = one GPU_LINEAR
// half-res bounce (240x160 -> 120x80) bilinear-upscaled back -> a gentle ~2x2 box soften.
// Composited as subtle top/bottom bands over the TOP screen only (overworld; both eyes share
// the one blurred copy) -> sharp focal band = "diorama" read. TEXT-AWARE: any BG0 text-layer
// content under a band kills that band's blur (band_text_scan), so text stays readable.
#define DOF_TEXA      128   // POT texture backing the 120x80 half-res bounce
#define DOF_SHARP_Y0  48    // sharp focal band (GBA rows Y0..Y1; player sits ~64..96)
#define DOF_SHARP_Y1  112
#define DOF_FADE      24    // blur alpha-ramps in over this many rows outside the band
#define DOF_ALPHA     0x78  // max band alpha (~47%): a subtle soften, never a wall of mush

// HD-2D M3 (LDR bloom): bright-pass = clamp(half-res frame - BLOOM_THRESH) x2 (TEV SUBTRACT +
// x2 scale) into a quarter-res glow map; composited ADDITIVELY (x BLOOM_GAIN) over each eye.
// Honest LDR glow on the RGB565 frame, not HDR bloom. Focused top screen + overworld only,
// and any on-screen text eases the glow off (a white textbox must never halo its own text).
#define BLOOM_TEX     64    // POT texture backing the 60x40 glow map
#define BLOOM_THRESH  0xC8  // a channel must exceed ~78% to glow (water glints, lamps, white)
#define BLOOM_GAIN    0x90  // additive gain (~56%) on the x2'd glow -> subtle, not blinding

// Stereoscopic single-game depth: pop elements forward per eye on the ALREADY-composited frame
// (no extra emulation pass). depth3d.overworld is snapshotted in the race-safe window. M2 = player.
#define DEPTH_MAX_SPR 32
typedef struct {
	bool overworld;
	int  nspr;
	struct { short x, y; unsigned char w, h, elev; } spr[DEPTH_MAX_SPR];   // on-screen OAM rects (+ matched object elevation tier)
	float tdepth[10][15];           // smoothed scenery EXTRA depth px (layer-type + elevation)
	bool textTop, textBot;          // text/UI under the top/bottom blur band -> suppress that band
	struct { unsigned char x0, y0, x1, y1; } uiRect[6];   // BG0 window panels (tile coords, incl.)
	int  nui;                       // panel count (panels pop in ANY context, not just overworld)
	int  nfg;                       // foreground/solid tiles in view (HUD diagnostic)
	float maxd;                     // strongest tdepth in view (HUD diagnostic)
	int  camX, camY;                // gFieldCamera sub-tile scroll (px, -15..15) -> depth scroll-align
} DepthSnap;
#define POP3D_PLAYER_GX 112   // player tile (7,5) -> sprite rect (16x32, head 16px above the tile)
#define POP3D_PLAYER_GY 64
// Depth model v2 (hardware round 4): a continuous ground-plane ramp (the bottom of the frame
// is closer -> pops more), scenery extra from the metatile layer-type PLUS the map-grid
// elevation nibble (a hilltop pops above the grass at its feet), characters always floating
// CHAR_PX above the floor at their own feet (the floor can never pop over them), and BG0
// window panels (dialogs/menus) popping hardest of all as clean shifted copies.
#define POP3D_RAMP_PX 3.6f    // ground ramp: bottom-edge pop (px @ full slider); top edge = 0 (moderate for comfort)
#define POP3D_STANDUP 3.0f    // a sprite's head pops this far out beyond its grounded feet (standee lean)
#define ENV3D_NORMAL  4.0f    // solid/foreground tile pop (poles/trees/walls) -- strong stand-up
#define ENV3D_SPLIT   1.2f    // ledge / low fence mid extra
#define ENV3D_RAISED  2.6f    // a "raised" elevation tier (engine priority 1) pops this far above ground
#define ENV3D_FRONT   3.6f    // a "frontmost" tier (engine priority 0) pops this far
#define TDEPTH_MAX    4.2f    // clamp the per-tile scenery depth (elevation plane + feature)
#define POP_DISP_MAX  6.5f    // hard comfort ceiling on any element's forward disparity (px @ full slider)
#define UIPOP3D_PX    5.0f    // BG0 window panels (dialogs/menus): strong clean-copy pop
#define RAMP_AT(gy)   (POP3D_RAMP_PX * (float)(gy) / (float)GBA_H)

// Map a Gen-3 map-grid / object elevation tier (0..15) to a forward depth PLANE. Baked from the
// engine's own sElevationToPriority {2,2,2,2,1,2,1,2,1,2,1,2,1,0,0,2}: priority 2 = ground (0),
// priority 1 = a raised tier, priority 0 = frontmost -> our depth order matches what the game draws
// in front ("C"). Tiers 0 (TRANSITION) and 15 (MULTI_LEVEL) are -1 = "no own height" -> interpolated
// from neighbours so stairs/ledges/bridges ramp between the tiers they join.
static const float ELEV_PLANE[16] = {
	-1.f, 0.f, 0.f, 0.f, ENV3D_RAISED, 0.f, ENV3D_RAISED, 0.f,
	ENV3D_RAISED, 0.f, ENV3D_RAISED, 0.f, ENV3D_RAISED, ENV3D_FRONT, ENV3D_FRONT, -1.f
};
static inline float elev_plane(int e) { return (e >= 0 && e < 16 && ELEV_PLANE[e] >= 0.f) ? ELEV_PLANE[e] : 0.f; }
static inline float clamp_disp(float v) { return v > POP_DISP_MAX ? POP_DISP_MAX : v; }
static void calc_xform(int mode, float sW, float sH, float* ox, float* oy, float* sx, float* sy) {
	if (mode == SCALE_1X)           { *sx = *sy = 1.0f; }
	else if (mode == SCALE_STRETCH) { *sx = sW / GBA_W; *sy = sH / GBA_H; }
	else { float f = (sW / GBA_W < sH / GBA_H) ? sW / GBA_W : sH / GBA_H; *sx = *sy = f; }
	*ox = (sW - GBA_W * *sx) / 2.0f;
	*oy = (sH - GBA_H * *sy) / 2.0f;
}
// Re-draw a sub-rect of a frame texture shifted horizontally (the per-eye pop overdraw).
// pscale/texDim pick the source: the raw 256px GBA tex (1/256) or the sharp-bilinear prescale
// (PRESCALE/PRE_TEX) so popped text stays crisp. The destination is clipped to the on-screen
// frame box, so a shifted pop never bleeds into the letterbox (per-eye rivalry on the border).
static void draw_pop_tex(C3D_Tex* tex, int pscale, int texDim, GPU_TEXTURE_FILTER_PARAM filt,
                         int gx, int gy, int gw, int gh, float ox, float oy, float sx, float sy, float xoff) {
	if (gx < 0) { gw += gx; gx = 0; }
	if (gy < 0) { gh += gy; gy = 0; }
	if (gx + gw > GBA_W) gw = GBA_W - gx;
	if (gy + gh > GBA_H) gh = GBA_H - gy;
	float xs = xoff / sx;                                   // shift expressed in GBA pixels
	int loCol = (int)ceilf(-xs);              if (gx < loCol) { gw -= (loCol - gx); gx = loCol; }
	int hiCol = (int)floorf((float)GBA_W - xs); if (gx + gw > hiCol) gw = hiCol - gx;
	if (gw <= 0 || gh <= 0) return;
	float t = (float)pscale, D = (float)texDim;
	Tex3DS_SubTexture st = { (u16)(gw * pscale), (u16)(gh * pscale),
	                        gx * t / D, 1.0f - gy * t / D, (gx + gw) * t / D, 1.0f - (gy + gh) * t / D };
	C2D_Image img = { tex, &st };
	C3D_TexSetFilter(tex, filt, filt);
	C2D_DrawImageAt(img, ox + gx * sx + xoff, oy + gy * sy, 0.0f, NULL, sx / t, sy / t);
}
static void draw_pop(C3D_Tex* tex, int gx, int gy, int gw, int gh,
                     float ox, float oy, float sx, float sy, float xoff) {
	draw_pop_tex(tex, 1, 256, GPU_NEAREST, gx, gy, gw, gh, ox, oy, sx, sy, xoff);
}

// Smoothed floor depth (px) under a screen pixel, so a character can pop ABOVE the very floor
// (ground ramp already separate; this adds the scenery/elevation extra) it stands on.
static float floor_at(const DepthSnap* d, int gx_px, int gy_px) {
	int c = gx_px / 16, r = gy_px / 16;   // sprite floor = its own grid cell (do NOT scroll-shift: the player sits at a fixed cell)
	if (c < 0) c = 0; else if (c > 14) c = 14;
	if (r < 0) r = 0; else if (r > 9) r = 9;
	return d->tdepth[r][c];
}

// Pop the captured characters forward on one eye (shifted sub-rect overdraws on the flat frame).
// eyeSl = +slider (left eye) / -slider (right). Each sprite pops CHAR_PX above the floor ramp
// at its FEET, so the ground plane can never pop over a character standing on it.
static void pop_eye(C3D_RenderTarget* tgt, EmuInstance* g, const DepthSnap* d, int mode, float eyeSl) {
	float ox, oy, sx, sy; calc_xform(mode, 400.0f, 240.0f, &ox, &oy, &sx, &sy);
	C2D_SceneBegin(tgt);
	// Every sprite (NPC, the player, sprite-objects) stands UP out of the ground: its feet sit at
	// the ground depth and the disparity ramps to +STANDUP at its head -> a leaning standee. Applied
	// precisely PER SPRITE (no special rectangle for the centre/player); drawn as a few horizontal
	// strips so the head pops while the base stays anchored to the floor.
	for (int i = 0; i < d->nspr; i++) {
		int x0 = d->spr[i].x, y0 = d->spr[i].y, w = d->spr[i].w, h = d->spr[i].h;
		int cx = x0 + w / 2, fy = y0 + h;
		// Feet floor = smoothed grid depth there, RAISED to the sprite's own object-elevation plane
		// when matched (authoritative on stairs); max keeps the standee never behind its feet tile.
		float floorD = floor_at(d, cx, fy);
		if (d->spr[i].elev != 0xFF) { float pl = elev_plane(d->spr[i].elev); if (pl > floorD) floorD = pl; }
		float base = RAMP_AT(fy) + floorD;                          // disparity at the grounded feet
		int strips = (h + 7) / 8; if (strips < 1) strips = 1;
		for (int s = 0; s < strips; s++) {
			int yy = y0 + s * h / strips, hh = y0 + (s + 1) * h / strips - yy;
			float tmid = ((float)yy + 0.5f * (float)hh - (float)y0) / (float)h;   // 0 head .. 1 feet
			float disp = eyeSl * clamp_disp(base + POP3D_STANDUP * (1.0f - tmid));
			draw_pop(&g->tex, x0, yy, w, hh, ox, oy, sx, sy, disp);
		}
	}
}

// M4: metatile id -> layer type (0 NORMAL=foreground / 1 COVERED=ground / 2 SPLIT=mid) via the
// gMapHeader -> MapLayout -> Tileset -> metatileAttributes chain. Cached per map + memoized by id.
static uint8_t metatile_layer(GbaCore* c, const GameProfile* p, uint16_t id) {
	static uint32_t cLayout = 0, cPri = 0, cSec = 0;
	static uint8_t  cLayer[1024], cValid[1024];
	if (!p->mapHeader || id >= 0x03FF) return 1;                 // no M4 / sentinel border -> ground
	uint32_t layoutP = gbacore_read32(c, p->mapHeader + 0x00);   // MapHeader.mapLayout
	if (layoutP != cLayout) {                                    // map changed -> rebuild bases + memo
		cLayout = layoutP;
		uint32_t tsP = gbacore_read32(c, layoutP + 0x10), tsS = gbacore_read32(c, layoutP + 0x14);
		cPri = tsP ? gbacore_read32(c, tsP + 0x10) : 0;            // Tileset.metatileAttributes
		cSec = tsS ? gbacore_read32(c, tsS + 0x10) : 0;
		memset(cValid, 0, sizeof cValid);
	}
	if (cValid[id]) return cLayer[id];
	uint32_t attrP = (id < 512) ? cPri : cSec;
	uint8_t  layer = 1;
	if (attrP) { uint16_t a = gbacore_read16(c, attrP + 2u * (uint32_t)((id < 512) ? id : id - 512)); layer = (a & 0xF000) >> 12; }
	cLayer[id] = layer; cValid[id] = 1;
	return layer;
}

// M4: build the smoothed scenery-depth grid for the visible 15x10 tiles (cores parked; safe reads).
static void build_depth_grid(GbaCore* core, const GameProfile* p, int px, int py, DepthSnap* d) {
	memset(d->tdepth, 0, sizeof d->tdepth);
	if (!core || !p || px < 0 || py < 0) return;   // metatile_layer handles mapHeader==0 (FR/LG collision depth)
	d->camX = p->fieldCamera ? (int)(int32_t)gbacore_read32(core, p->fieldCamera + 0x10) : 0;
	d->camY = p->fieldCamera ? (int)(int32_t)gbacore_read32(core, p->fieldCamera + 0x14) : 0;
	if (d->camX < -15 || d->camX > 15) d->camX = 0;   // guard garbage
	if (d->camY < -15 || d->camY > 15) d->camY = 0;
	int w = (int32_t)gbacore_read32(core, p->mapLayout + 0);
	int h = (int32_t)gbacore_read32(core, p->mapLayout + 4);
	uint32_t ptr = gbacore_read32(core, p->mapLayout + 8);
	if ((ptr >> 24) != 0x02 || w <= 0 || w > 512 || h <= 0 || h > 512) return;
	// Per visible tile: feature depth (layer-type) + elevation-plane depth. Elevation 0/15 (stairs/
	// ledges/bridges) carry no own height -> left "unknown" and filled below. (Border +7 and the -7
	// player-centring cancel, so screen col c == map gx px+c -- verified against the touch BFS.)
	float feat[10][15], ed[10][15]; bool has[10][15];
	for (int r = 0; r < 10; r++) for (int c = 0; c < 15; c++) {
		int gx = px + c, gy = py + r + 2;
		feat[r][c] = 0.0f; ed[r][c] = 0.0f; has[r][c] = false;
		if (gx >= 0 && gx < w && gy >= 0 && gy < h) {
			uint16_t e = gbacore_read16(core, ptr + 2u * (uint32_t)(gx + w * gy));
			uint8_t layer = metatile_layer(core, p, e & 0x03FF);
			// The metatile NORMAL *layer type* is the default compositing mode -> ~EVERY tile, so using
			// it as "foreground" floods the grid uniform and the whole map reads FLAT (the f~148/150
			// diagnostic caught exactly this). The real stand-up signal is COLLISION: a solid/impassable
			// tile is an object (pole/tree/wall/sign/fence); walkable ground is passable and stays flat.
			// Same 0x0C00 bits the touch BFS walks on -> proven + game-agnostic. Keep SPLIT (ledges /
			// grass edges) as a small extra; skip elevation 1 = surf water (impassable but flat).
			float f = (layer == 2) ? ENV3D_SPLIT : 0.0f;
			if ((e & 0x0C00) && (e >> 12) != 1) f = ENV3D_NORMAL;
			feat[r][c] = f;
			float pl = ELEV_PLANE[e >> 12];
			if (pl >= 0.0f) { ed[r][c] = pl; has[r][c] = true; }
		}
	}
	int nfg = 0; for (int r = 0; r < 10; r++) for (int c = 0; c < 15; c++) if (feat[r][c] > 0.0f) nfg++;
	d->nfg = nfg;   // HUD diagnostic
	// Fill stairs/ledges/bridges by relaxing from known neighbours, so depth RAMPS across them
	// instead of dropping to ground (Gauss-Seidel; cap spans the 10x15 grid, early-exits when stable).
	for (int pass = 0; pass < 16; pass++) {
		bool changed = false;
		for (int r = 0; r < 10; r++) for (int c = 0; c < 15; c++) if (!has[r][c]) {
			float sum = 0.0f; int n = 0;
			if (r > 0  && has[r-1][c]) { sum += ed[r-1][c]; n++; }
			if (r < 9  && has[r+1][c]) { sum += ed[r+1][c]; n++; }
			if (c > 0  && has[r][c-1]) { sum += ed[r][c-1]; n++; }
			if (c < 14 && has[r][c+1]) { sum += ed[r][c+1]; n++; }
			if (n > 0) { ed[r][c] = sum / (float)n; has[r][c] = true; changed = true; }
		}
		if (!changed) break;
	}
	// Combine elevation plane + feature depth per tile. NO blur: a 3x3 average diluted an isolated
	// object (a lone pole 4.0 -> ~0.44 -> invisible), which is why scenery read flat while the
	// un-blurred sprite standee popped. The vertex grid already interpolates between tiles for
	// smoothness, so crisp per-tile depth is fine. maxd = the strongest pop in view (HUD diagnostic).
	float maxd = 0.0f;
	for (int r = 0; r < 10; r++) for (int c = 0; c < 15; c++) {
		float t = ed[r][c] + feat[r][c];
		if (t > TDEPTH_MAX) t = TDEPTH_MAX;
		d->tdepth[r][c] = t;
		if (t > maxd) maxd = t;
	}
	d->maxd = maxd;
}

// M4: pop the foreground scenery tiles forward on one eye (shifted 16x16 sub-rect overdraws).
static void warp_scenery_eye(C3D_RenderTarget* tgt, EmuInstance* g, const DepthSnap* d, int mode, float dispUnit) {
	float ox, oy, sx, sy; calc_xform(mode, 400.0f, 240.0f, &ox, &oy, &sx, &sy);
	C2D_SceneBegin(tgt);
	for (int r = 0; r < 10; r++) for (int c = 0; c < 15; c++) {
		float dep = d->tdepth[r][c];
		if (dep > 0.01f) draw_pop(&g->tex, c * 16, r * 16, 16, 16, ox, oy, sx, sy, dispUnit * dep);
	}
}

// ---- HD-2D M4: time-of-day directional lighting + day/night color grade --------------------
// A per-tile light field modulates the overworld frame: a key light whose colour & direction
// track the time of day (warm low-angle dawn/dusk, bright neutral noon, dim blue night) shades
// the terrain by its surface normal (from the tdepth elevation field) and is drawn as a gouraud
// MULTIPLY mesh over the frame. Pure citro2d (no shader); analytic normals -- the study's
// AI-baked-normal atlas is the deferred optional upgrade. Cheap: ~150 gouraud quads per eye.
typedef struct { float r, g, b, lx, ly, lz, amb, dif; } LightEnv;

// Interpolate the key-light for hour-of-day hf in [0,24). L is a screen-space direction
// (x: +east/-west, y: +down, z: out toward the viewer); ground faces +z.
static LightEnv light_for_hour(float hf) {
	// Kept BRIGHT on purpose: high ambient so the focused game never reads as "darkened" -- the time
	// of day is a SUBTLE colour/lean, not a dimming (a MULTIPLY mesh can only darken, so we stay near
	// white). Low diffuse = gentle slope shading only.
	static const float K[][9] = {   // hour, r,g,b(0..1), lx,ly,lz, ambient, diffuse
		{  0.f, 0.86f,0.90f,1.00f,  0.00f,-0.25f,0.97f, 0.88f,0.12f },   // night (subtle cool, still bright)
		{  5.f, 0.90f,0.92f,1.00f,  0.50f,-0.20f,0.84f, 0.90f,0.12f },   // pre-dawn
		{  7.f, 1.00f,0.95f,0.86f,  0.72f,-0.18f,0.67f, 0.94f,0.14f },   // dawn (subtle warm)
		{ 12.f, 1.00f,1.00f,1.00f,  0.05f,-0.10f,0.99f, 1.00f,0.10f },   // noon (full bright neutral)
		{ 17.f, 1.00f,0.95f,0.86f, -0.62f,-0.18f,0.76f, 0.95f,0.14f },   // golden hour (subtle warm)
		{ 19.f, 1.00f,0.90f,0.82f, -0.74f,-0.16f,0.65f, 0.90f,0.14f },   // dusk (subtle warm)
		{ 21.f, 0.88f,0.91f,1.00f, -0.20f,-0.22f,0.95f, 0.88f,0.12f },   // night falls
		{ 24.f, 0.86f,0.90f,1.00f,  0.00f,-0.25f,0.97f, 0.88f,0.12f },   // wrap == 0h
	};
	int n = sizeof K / sizeof K[0], i = 0;
	while (i < n - 1 && hf >= K[i + 1][0]) i++;
	const float* a = K[i]; const float* b = K[i + 1];
	float u = (b[0] > a[0]) ? (hf - a[0]) / (b[0] - a[0]) : 0.0f;
	LightEnv e;
	e.r = a[1]+(b[1]-a[1])*u; e.g = a[2]+(b[2]-a[2])*u; e.b = a[3]+(b[3]-a[3])*u;
	e.lx= a[4]+(b[4]-a[4])*u; e.ly= a[5]+(b[5]-a[5])*u; e.lz= a[6]+(b[6]-a[6])*u;
	e.amb=a[7]+(b[7]-a[7])*u; e.dif=a[8]+(b[8]-a[8])*u;
	float il = 1.0f / sqrtf(e.lx*e.lx + e.ly*e.ly + e.lz*e.lz + 1e-6f);
	e.lx*=il; e.ly*=il; e.lz*=il;
	return e;
}

// Per-vertex tint = lightColor * (ambient + diffuse*max(0,N.L)); N from the tdepth gradient at
// the grid vertex (raised terrain catches side light). Returned as a citro2d colour for MULTIPLY.
static u32 light_vert(const DepthSnap* d, const LightEnv* e, int vr, int vc) {
	#define LCELL(R,C) d->tdepth[(R)<0?0:((R)>9?9:(R))][(C)<0?0:((C)>14?14:(C))]
	float lf = (LCELL(vr-1, vc-1) + LCELL(vr, vc-1)) * 0.5f;   // left  column avg
	float rt = (LCELL(vr-1, vc)   + LCELL(vr, vc))   * 0.5f;   // right column avg
	float up = (LCELL(vr-1, vc-1) + LCELL(vr-1, vc)) * 0.5f;   // upper row avg
	float dn = (LCELL(vr, vc-1)   + LCELL(vr, vc))   * 0.5f;   // lower row avg
	#undef LCELL
	const float kSlope = 0.18f;                       // depth-px -> normal tilt
	float nx = -(rt - lf) * kSlope, ny = -(dn - up) * kSlope, nz = 1.0f;
	float ndl = (nx*e->lx + ny*e->ly + nz*e->lz) / sqrtf(nx*nx + ny*ny + nz*nz);
	if (ndl < 0.0f) ndl = 0.0f;
	float s = e->amb + e->dif * ndl;
	int R = (int)(e->r*s*255.0f + 0.5f), G = (int)(e->g*s*255.0f + 0.5f), B = (int)(e->b*s*255.0f + 0.5f);
	if (R > 255) R = 255; if (G > 255) G = 255; if (B > 255) B = 255;
	return C2D_Color32((u8)R, (u8)G, (u8)B, 0xFF);
}

// Draw the light field over one eye as a MULTIPLY gouraud mesh (15x10 quads over the frame box).
static void light_pass(C3D_RenderTarget* tgt, const DepthSnap* d, int mode, const LightEnv* e) {
	float ox, oy, sx, sy; calc_xform(mode, 400.0f, 240.0f, &ox, &oy, &sx, &sy);
	u32 col[11][16];
	for (int vr = 0; vr <= 10; vr++) for (int vc = 0; vc <= 15; vc++) col[vr][vc] = light_vert(d, e, vr, vc);
	C2D_SceneBegin(tgt);
	C2D_Flush();   // commit the pending image batch before swapping the blend equation
	C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_DST_COLOR, GPU_ZERO, GPU_DST_COLOR, GPU_ZERO);  // dst*src = MULTIPLY
	for (int r = 0; r < 10; r++) for (int c = 0; c < 15; c++)
		C2D_DrawRectangle(ox + c*16*sx, oy + r*16*sy, 0.0f, 16*sx, 16*sy,
		                  col[r][c], col[r][c+1], col[r+1][c], col[r+1][c+1]);
	C2D_Flush();   // commit the mesh while MULTIPLY is still active, then restore citro2d's blend
	C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA);
}

// M2: continuous vertex-grid scenery warp (one mesh per eye). Verts sit at 16px tile corners
// and shift by the smoothed tile depth, so depth steps STRETCH the texture between cells
// instead of tearing at tile edges (the per-tile quad shift's artifact). citro2d has no mesh
// path, so this is a raw C3D draw with a passthrough shader (warp.v.pica); the warped X
// positions are CPU-computed (176 verts/eye) and the GPU is handed back via C2D_Prepare.
#define WARP_COLS  15
#define WARP_ROWS  10
#define WARP_VERTS ((WARP_COLS + 1) * (WARP_ROWS + 1))   // 16x11 = 176
#define WARP_IDX   (WARP_COLS * WARP_ROWS * 6)           // 900
typedef struct { float x, y, u, v; } WarpVert;
static DVLB_s*         warpDvlb;
static shaderProgram_s warpProg;
static int             warpProjLoc = -1;
static WarpVert*       warpVbo;   // linearAlloc; one slab per eye, rewritten per frame (SYNCDRAW-safe)
static u16*            warpIbo;   // static triangle indices
static WarpVert*       bloomVbo;  // M3 bloom: 3 quads (bright pass + additive L/R)
static bool            warpOk;

static void warp_grid_init(void) {
	warpDvlb = DVLB_ParseFile((u32*)warp_shbin, warp_shbin_size);
	if (!warpDvlb) return;
	shaderProgramInit(&warpProg);
	shaderProgramSetVsh(&warpProg, &warpDvlb->DVLE[0]);
	warpProjLoc = shaderInstanceGetUniformLocation(warpProg.vertexShader, "projection");
	warpVbo = (WarpVert*)linearAlloc(sizeof(WarpVert) * WARP_VERTS * 2);
	warpIbo = (u16*)linearAlloc(sizeof(u16) * WARP_IDX);
	bloomVbo = (WarpVert*)linearAlloc(sizeof(WarpVert) * 12);   // M3 bloom quads
	if (!warpVbo || !warpIbo || !bloomVbo || warpProjLoc < 0) return;   // warpOk stays false -> fallbacks
	int n = 0;
	for (int r = 0; r < WARP_ROWS; r++) for (int c = 0; c < WARP_COLS; c++) {
		u16 tl = (u16)(r * (WARP_COLS + 1) + c), tr = tl + 1;
		u16 bl = tl + (WARP_COLS + 1), br = bl + 1;
		warpIbo[n++] = tl; warpIbo[n++] = bl; warpIbo[n++] = tr;
		warpIbo[n++] = tr; warpIbo[n++] = bl; warpIbo[n++] = br;
	}
	GSPGPU_FlushDataCache(warpIbo, sizeof(u16) * WARP_IDX);
	warpOk = true;
}

static void warp_grid_fini(void) {
	if (warpVbo) linearFree(warpVbo);
	if (warpIbo) linearFree(warpIbo);
	if (bloomVbo) linearFree(bloomVbo);
	if (warpDvlb) { shaderProgramFree(&warpProg); DVLB_Free(warpDvlb); }
}

// Standee depth field: a vertex carries the depth of the tile(s) just BELOW it (grid row vr), so a
// foreground tile's TOP edge pops out while its BOTTOM edge (the next row down) sits at the ground.
// Every foreground object (pole / thin tree / rock tile) thus STANDS UP all over the map -- not only
// where it overlaps the player's sprite rect (the old 4-tile AVERAGE diluted an isolated tile down to
// near-ground, so lone poles went flat). max() over the two tiles below keeps thin verticals at full
// pop; a flat plateau (tiles below also raised) stays uniformly forward, leaning only at its front edge.
static float warp_vert_depth(const DepthSnap* d, int vr, int vc) {
	if (vr < 0 || vr >= WARP_ROWS) return 0.0f;                       // bottom screen edge -> grounded
	float dep = 0.0f;
	if (vc - 1 >= 0 && vc - 1 < WARP_COLS && d->tdepth[vr][vc - 1] > dep) dep = d->tdepth[vr][vc - 1];
	if (vc >= 0     && vc     < WARP_COLS && d->tdepth[vr][vc]     > dep) dep = d->tdepth[vr][vc];
	return dep;
}

// Sample the standee field at a FRACTIONAL grid position (bilinear), so a vertex's depth can be
// shifted by the camera's sub-tile scroll -> object pops track the smoothly scrolling map instead
// of snapping per whole tile (the walk "wobble").
static float warp_depth_at(const DepthSnap* d, float fr, float fc) {
	int r0 = (int)floorf(fr), c0 = (int)floorf(fc);
	float fy = fr - (float)r0, fx = fc - (float)c0;
	float d00 = warp_vert_depth(d, r0,     c0), d01 = warp_vert_depth(d, r0,     c0 + 1);
	float d10 = warp_vert_depth(d, r0 + 1, c0), d11 = warp_vert_depth(d, r0 + 1, c0 + 1);
	return (d00 * (1.0f - fx) + d01 * fx) * (1.0f - fy) + (d10 * (1.0f - fx) + d11 * fx) * fy;
}

static void warp_grid_eye(C3D_RenderTarget* tgt, EmuInstance* g, const DepthSnap* d, int mode,
                          float dispUnit, bool sharpPre, C3D_Tex* pre, u32 mod, int eye) {
	float ox, oy, sx, sy; calc_xform(mode, 400.0f, 240.0f, &ox, &oy, &sx, &sy);
	WarpVert* v = warpVbo + (eye ? WARP_VERTS : 0);
	float cxf = (float)d->camX / 16.0f, cyf = (float)d->camY / 16.0f;   // sub-tile scroll
	for (int r = 0; r <= WARP_ROWS; r++) for (int c = 0; c <= WARP_COLS; c++) {
		WarpVert* w = &v[r * (WARP_COLS + 1) + c];
		float gx = (float)(c * 16), gy = (float)(r * 16);
		float dep = warp_depth_at(d, (float)r + cyf, (float)c + cxf);   // depth tracks the sub-tile scroll
		w->x = ox + gx * sx + dispUnit * clamp_disp(RAMP_AT(gy) + dep);   // ramp (screen-anchored) + scrolled depth
		w->y = oy + gy * sy;
		w->u = gx / 256.0f;             // preTex UVs coincide: PRESCALE/PRE_TEX == 1/256
		w->v = 1.0f - gy / 256.0f;
	}
	GSPGPU_FlushDataCache(v, sizeof(WarpVert) * WARP_VERTS);
	C2D_Flush();                        // submit citro2d's pending work before going raw C3D
	C3D_FrameDrawOn(tgt);
	C3D_BindProgram(&warpProg);
	C3D_Mtx proj;
	Mtx_OrthoTilt(&proj, 0.0f, 400.0f, 240.0f, 0.0f, 1.0f, -1.0f, true);
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, warpProjLoc, &proj);
	C3D_AttrInfo* ai = C3D_GetAttrInfo();
	AttrInfo_Init(ai);
	AttrInfo_AddLoader(ai, 0, GPU_FLOAT, 2);   // v0 = position
	AttrInfo_AddLoader(ai, 1, GPU_FLOAT, 2);   // v1 = texcoord
	C3D_BufInfo* bi = C3D_GetBufInfo();
	BufInfo_Init(bi);
	BufInfo_Add(bi, v, sizeof(WarpVert), 2, 0x10);
	C3D_TexBind(0, sharpPre ? pre : &g->tex);  // sharp-bilinear keeps its crisp prescale as the source
	C3D_TexEnv* env = C3D_GetTexEnv(0);
	C3D_TexEnvInit(env);
	C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, GPU_CONSTANT, 0);
	C3D_TexEnvFunc(env, C3D_Both, GPU_MODULATE);   // x mod = the unfocused dim-tint analog
	C3D_TexEnvColor(env, mod);
	C3D_TexEnvInit(C3D_GetTexEnv(1));
	C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
	C3D_CullFace(GPU_CULL_NONE);
	C3D_DrawElements(GPU_TRIANGLES, WARP_IDX, C3D_UNSIGNED_SHORT, warpIbo);
	C2D_Prepare();                      // hand the GPU back to citro2d (rebinds its shader/state)
}

// HD-2D M1: build the blurred copy of the top game's frame (one LINEAR half-res bounce). Runs
// once per frame before the per-eye composites; both eyes sample texA. Half-texel insets on
// the outer sample edges keep the cleared/stride texels from bleeding into the blur.
static void dof_prepare(C3D_Tex* src, C3D_RenderTarget* tgtA) {
	Tex3DS_SubTexture s0 = { GBA_W, GBA_H, 0.0f, 1.0f,
	                         ((float)GBA_W - 0.5f) / 256.0f, 1.0f - ((float)GBA_H - 0.5f) / 256.0f };
	C2D_Image i0 = { src, &s0 };
	C3D_TexSetFilter(src, GPU_LINEAR, GPU_LINEAR);   // averaging downsample (render_game resets it)
	C2D_TargetClear(tgtA, C2D_Color32(0, 0, 0, 0xFF));
	C2D_SceneBegin(tgtA);
	C2D_DrawImageAt(i0, 0.0f, 0.0f, 0.0f, NULL, 0.5f, 0.5f);
}

// One blurred horizontal band: GBA rows gy0..gy1 (multiples of 2) of the half-res copy,
// bilinear-upscaled through the same screen transform, vertical alpha ramp a0(top)->a1(bottom).
// Tint blend 0 leaves RGB untouched; the tint color's alpha is per-corner transparency.
static void dof_band(C3D_Tex* texA, int gy0, int gy1, float ox, float oy, float sx, float sy, float xoff, u8 a0, u8 a1) {
	if (gy1 <= gy0) return;
	float u1 = ((float)(GBA_W / 2) - 0.5f) / DOF_TEXA;
	float v1 = 1.0f - ((float)(gy1 / 2) - (gy1 == GBA_H ? 0.5f : 0.0f)) / DOF_TEXA;
	Tex3DS_SubTexture st = { (u16)(GBA_W / 2), (u16)((gy1 - gy0) / 2),
	                        0.0f, 1.0f - (float)(gy0 / 2) / DOF_TEXA, u1, v1 };
	C2D_Image img = { texA, &st };
	C2D_ImageTint t;
	C2D_SetImageTint(&t, C2D_TopLeft,  C2D_Color32(0, 0, 0, a0), 0.0f);
	C2D_SetImageTint(&t, C2D_TopRight, C2D_Color32(0, 0, 0, a0), 0.0f);
	C2D_SetImageTint(&t, C2D_BotLeft,  C2D_Color32(0, 0, 0, a1), 0.0f);
	C2D_SetImageTint(&t, C2D_BotRight, C2D_Color32(0, 0, 0, a1), 0.0f);
	C2D_DrawImageAt(img, ox + xoff, oy + gy0 * sy, 0.0f, &t, 2.0f * sx, 2.0f * sy);
}

// Tilt-shift composite on one eye target: solid blur at the frame edges, alpha ramp into the
// sharp focal band. Drawn AFTER the pops, so out-of-focus pop edges blur away with the band.
// Each band has its own engagement level: text under a band kills just that band's blur.
static void dof_bands(C3D_RenderTarget* tgt, C3D_Tex* texA, int mode, float lvlTop, float lvlBot, float eyeSl) {
	u8 aT = (u8)(DOF_ALPHA * lvlTop + 0.5f), aB = (u8)(DOF_ALPHA * lvlBot + 0.5f);
	if (!aT && !aB) return;
	float ox, oy, sx, sy; calc_xform(mode, 400.0f, 240.0f, &ox, &oy, &sx, &sy);
	float dT = eyeSl * RAMP_AT(DOF_SHARP_Y0 / 2);            // bands ride the floor ramp at their
	float dB = eyeSl * RAMP_AT((DOF_SHARP_Y1 + GBA_H) / 2);  // centers -> no depth rivalry with it
	C2D_SceneBegin(tgt);
	if (aT) {
		dof_band(texA, 0,                       DOF_SHARP_Y0 - DOF_FADE, ox, oy, sx, sy, dT, aT, aT);
		dof_band(texA, DOF_SHARP_Y0 - DOF_FADE, DOF_SHARP_Y0,            ox, oy, sx, sy, dT, aT, 0x00);
	}
	if (aB) {
		dof_band(texA, DOF_SHARP_Y1,            DOF_SHARP_Y1 + DOF_FADE, ox, oy, sx, sy, dB, 0x00, aB);
		dof_band(texA, DOF_SHARP_Y1 + DOF_FADE, GBA_H,                   ox, oy, sx, sy, dB, aB, aB);
	}
}

// BG0 scan v2 (game-agnostic): gen-3 draws every textbox/banner/menu on BG0, the text/window
// layer (verified: both decomps template bg0; the standard textbox sits at tile rows 15-18).
// One pass yields (a) per-band text flags -> that band's blur is suppressed so ALL text stays
// readable, and (b) the window-panel RECTS -> popped out per eye in ANY context (dialog, START
// menu, bag, party, battle text). Filler = the dominant entry of the visible grid (not assumed
// 0); unscannable modes fail toward readable. Runs at the parked per-frame handshake.
static void bg0_scan(GbaCore* c, const GameProfile* p, bool overworld, DepthSnap* d) {
	uint16_t disp = gbacore_read16(c, 0x04000000);
	if (!(disp & 0x0100)) return;                       // BG0 disabled -> no text layer
	uint16_t cnt = gbacore_read16(c, 0x04000008);
	if ((disp & 0x0007) != 0 || (cnt >> 14) != 0) {     // not mode 0 / text BG not 32x32:
		d->textTop = d->textBot = true;                  // can't reason -> fail toward readable
		return;
	}
	uint32_t map = 0x06000000u + (uint32_t)((cnt >> 8) & 0x1F) * 0x800u;
	uint16_t smp[40];                                   // dominant entry of the visible 32x20 grid
	for (int i = 0; i < 40; i++) smp[i] = gbacore_read16(c, map + 2u * (uint32_t)(i * 16));
	uint16_t filler = smp[0]; int best = 0;
	for (int i = 0; i < 40; i++) {
		int n = 0;
		for (int j = 0; j < 40; j++) n += (smp[j] == smp[i]);
		if (n > best) { best = n; filler = smp[i]; }
	}
	signed char rlo[20], rhi[20]; int rowN[20], topBusy = 0, botBusy = 0;
	for (int r = 0; r < 20; r++) {                      // per-row occupancy of the visible 30 cols
		int lo = -1, hi = -1, n = 0;
		for (int col = 0; col < 30; col++)
			if (gbacore_read16(c, map + 2u * (uint32_t)(r * 32 + col)) != filler) {
				if (lo < 0) lo = col;
				hi = col; n++;
			}
		rlo[r] = (signed char)lo; rhi[r] = (signed char)hi; rowN[r] = n;
		if (r <= 6)  topBusy += n;
		if (r >= 13) botBusy += n;
	}
	if (topBusy >= 8) d->textTop = true;                // tile rows 0..6  ~ GBA y 0..55
	if (botBusy >= 8) d->textBot = true;                // tile rows 13..19 ~ GBA y 104..159
	// UI-panel RECTS are gen-3 only: BG0 is the text/window layer there, but an arbitrary GBA
	// game's BG0 is usually the main playfield -> a full-screen 'panel' popped at max disparity.
	// (The blur text-flags above stay general; they only gate DoF, which is itself gen-3-gated.)
	if (!p) return;
	for (int r = 0; r < 20 && d->nui < 6; ) {           // merge busy rows (>=2 tiles) into panels
		if (rowN[r] < 2) { r++; continue; }
		int q = r, lo = rlo[r], hi = rhi[r];
		while (q + 1 < 20 && rowN[q + 1] >= 2) {
			q++;
			if (rlo[q] < lo) lo = rlo[q];
			if (rhi[q] > hi) hi = rhi[q];
		}
		int wc = hi - lo + 1, hr = q - r + 1;
		// Drop a near-full-screen rect in the OVERWORLD (a transient full BG0 = playfield false
		// positive); menus/bag/party run with overworld=false and legitimately fill the screen.
		if (!(overworld && wc >= 24 && hr >= 14)) {
			d->uiRect[d->nui].x0 = (unsigned char)lo;  d->uiRect[d->nui].y0 = (unsigned char)r;
			d->uiRect[d->nui].x1 = (unsigned char)hi;  d->uiRect[d->nui].y1 = (unsigned char)q;
			d->nui++;
		}
		r = q + 1;
	}
}

// Pop the BG0 window panels out of the screen on one eye: clean shifted overdraws of the SAME
// pixels -> strong depth while the text stays pixel-sharp (no blending, no blur).
static void ui_pop_eye(C3D_RenderTarget* tgt, EmuInstance* g, const DepthSnap* d, int mode,
                       float disp, bool sharpPre, C3D_Tex* pre) {
	float ox, oy, sx, sy; calc_xform(mode, 400.0f, 240.0f, &ox, &oy, &sx, &sy);
	C2D_SceneBegin(tgt);
	for (int i = 0; i < d->nui; i++) {
		int x = d->uiRect[i].x0 * 8, y = d->uiRect[i].y0 * 8;
		int w = (d->uiRect[i].x1 - d->uiRect[i].x0 + 1) * 8;
		int h = (d->uiRect[i].y1 - d->uiRect[i].y0 + 1) * 8;
		if (sharpPre) draw_pop_tex(pre, PRESCALE, PRE_TEX, GPU_LINEAR, x, y, w, h, ox, oy, sx, sy, disp);
		else          draw_pop(&g->tex, x, y, w, h, ox, oy, sx, sy, disp);
	}
}

// ---- HD-2D M3: LDR bloom (raw C3D draws reusing the warp passthrough shader) ----------------
static void bloom_raw_state(C3D_Tex* tex) {   // shared state for the two bloom draws
	C3D_BindProgram(&warpProg);
	C3D_AttrInfo* ai = C3D_GetAttrInfo();
	AttrInfo_Init(ai);
	AttrInfo_AddLoader(ai, 0, GPU_FLOAT, 2);   // v0 = position
	AttrInfo_AddLoader(ai, 1, GPU_FLOAT, 2);   // v1 = texcoord
	C3D_TexBind(0, tex);
	C3D_TexEnvInit(C3D_GetTexEnv(1));
	C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
	C3D_CullFace(GPU_CULL_NONE);
}

static void bloom_quad(WarpVert* v, float x0, float y0, float x1, float y1,
                       float u0, float v0, float u1, float v1) {
	v[0] = (WarpVert){ x0, y0, u0, v0 };
	v[1] = (WarpVert){ x1, y0, u1, v0 };
	v[2] = (WarpVert){ x1, y1, u1, v1 };
	v[3] = (WarpVert){ x0, y1, u0, v1 };
	GSPGPU_FlushDataCache(v, sizeof(WarpVert) * 4);
	C3D_BufInfo* bi = C3D_GetBufInfo();
	BufInfo_Init(bi);
	BufInfo_Add(bi, v, sizeof(WarpVert), 2, 0x10);
	C3D_DrawArrays(GPU_TRIANGLE_FAN, 0, 4);
}

// Bright-pass: half-res copy -> quarter-res glow map. TEV: clamp(tex - threshold) * 2.
static void bloom_bright(C3D_Tex* srcHalf, C3D_RenderTarget* tgt) {
	C2D_TargetClear(tgt, C2D_Color32(0, 0, 0, 0xFF));
	C2D_Flush();
	C3D_FrameDrawOn(tgt);
	bloom_raw_state(srcHalf);
	C3D_Mtx proj;
	Mtx_Ortho(&proj, 0.0f, (float)BLOOM_TEX, (float)BLOOM_TEX, 0.0f, 1.0f, -1.0f, true);
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, warpProjLoc, &proj);
	C3D_TexEnv* env = C3D_GetTexEnv(0);
	C3D_TexEnvInit(env);
	C3D_TexEnvSrc(env, C3D_RGB, GPU_TEXTURE0, GPU_CONSTANT, 0);
	C3D_TexEnvFunc(env, C3D_RGB, GPU_SUBTRACT);      // clamp(frame - threshold)
	C3D_TexEnvScale(env, C3D_RGB, GPU_TEVSCALE_2);   // x2: punch the survivors up
	C3D_TexEnvSrc(env, C3D_Alpha, GPU_TEXTURE0, 0, 0);
	C3D_TexEnvFunc(env, C3D_Alpha, GPU_REPLACE);
	C3D_TexEnvColor(env, C2D_Color32(BLOOM_THRESH, BLOOM_THRESH, BLOOM_THRESH, 0x00));
	bloom_quad(bloomVbo, 0.0f, 0.0f, (float)(GBA_W / 4), (float)(GBA_H / 4),
	           0.0f, 1.0f,
	           ((float)(GBA_W / 2) - 0.5f) / DOF_TEXA, 1.0f - ((float)(GBA_H / 2) - 0.5f) / DOF_TEXA);
	C2D_Prepare();
}

// Additive composite of the glow map over one finished eye (drawn after the DoF bands).
static void bloom_add(C3D_RenderTarget* tgt, C3D_Tex* glow, int mode, float lvl, int eye) {
	float ox, oy, sx, sy; calc_xform(mode, 400.0f, 240.0f, &ox, &oy, &sx, &sy);
	C2D_Flush();
	C3D_FrameDrawOn(tgt);
	bloom_raw_state(glow);
	C3D_Mtx proj;
	Mtx_OrthoTilt(&proj, 0.0f, 400.0f, 240.0f, 0.0f, 1.0f, -1.0f, true);
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, warpProjLoc, &proj);
	u8 g = (u8)(BLOOM_GAIN * lvl + 0.5f);
	C3D_TexEnv* env = C3D_GetTexEnv(0);
	C3D_TexEnvInit(env);
	C3D_TexEnvSrc(env, C3D_RGB, GPU_TEXTURE0, GPU_CONSTANT, 0);
	C3D_TexEnvFunc(env, C3D_RGB, GPU_MODULATE);      // glow x gain (gain carries the text fade)
	C3D_TexEnvSrc(env, C3D_Alpha, GPU_CONSTANT, 0, 0);
	C3D_TexEnvFunc(env, C3D_Alpha, GPU_REPLACE);
	C3D_TexEnvColor(env, C2D_Color32(g, g, g, 0xFF));
	C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ONE, GPU_ZERO, GPU_ONE);   // additive
	bloom_quad(bloomVbo + 4 + eye * 4, ox, oy, ox + GBA_W * sx, oy + GBA_H * sy,
	           0.0f, 1.0f,
	           ((float)(GBA_W / 4) - 0.5f) / BLOOM_TEX, 1.0f - ((float)(GBA_H / 4) - 0.5f) / BLOOM_TEX);
	C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA,
	               GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA);   // restore citro2d's standard blend
	C2D_Prepare();
}

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

// ---- Settings persistence (sdmc:/3DGBA/settings.bin) --------------------
#define SETTINGS_PATH  "sdmc:/3DGBA/settings.bin"
#define SETTINGS_MAGIC 0x33424744u   // 'DGB3'
typedef struct {
	u32 magic;
	s32 scaleMode[2];
	s32 smooth[2];
	s32 swapped;
	s32 hudMode;
	s32 audioMode;
	s32 volA, volB;
	s32 touchMode;
	s32 frameskip;
	s32 dof;
	s32 bloom;
	s32 light;
	s32 vivid;
} Settings;

static void settings_load(int scaleMode[2], bool smooth[2], bool* swapped, int* hudMode,
                          int* audioMode, int* volA, int* volB, int* touchMode, bool* fsOn, bool* dofOn, bool* bloomOn, bool* lightOn, bool* vividOn) {
	FILE* f = fopen(SETTINGS_PATH, "rb");
	if (!f) return;
	Settings s;
	size_t n = fread(&s, 1, sizeof s, f);
	fclose(f);
	size_t noVivid = sizeof s - sizeof s.vivid, noLight = noVivid - sizeof s.light, noBloom = noLight - sizeof s.bloom, noDof = noBloom - sizeof s.dof;
	if ((n != sizeof s && n != noVivid && n != noLight && n != noBloom && n != noDof) || s.magic != SETTINGS_MAGIC) return;   // tolerate older files
	scaleMode[0] = ((unsigned)s.scaleMode[0]) % 3;
	scaleMode[1] = ((unsigned)s.scaleMode[1]) % 3;
	smooth[0] = s.smooth[0] != 0;
	smooth[1] = s.smooth[1] != 0;
	*swapped  = s.swapped != 0;
	*hudMode    = ((unsigned)s.hudMode) & 3;
	*audioMode = ((unsigned)s.audioMode) % 3;
	*volA = s.volA < 0 ? 0 : (s.volA > 256 ? 256 : s.volA);
	*volB = s.volB < 0 ? 0 : (s.volB > 256 ? 256 : s.volB);
	*touchMode = ((unsigned)s.touchMode) % 3;
	*fsOn = s.frameskip != 0;
	if (n >= noBloom)  *dofOn   = s.dof != 0;     // older files keep the defaults
	if (n >= noLight)  *bloomOn = s.bloom != 0;
	if (n >= noVivid)  *lightOn = s.light != 0;
	if (n == sizeof s) *vividOn = s.vivid != 0;
}

static void settings_save(const int scaleMode[2], const bool smooth[2], bool swapped, int hudMode,
                          int audioMode, int volA, int volB, int touchMode, bool fsOn, bool dofOn, bool bloomOn, bool lightOn, bool vividOn) {
	Settings s = { SETTINGS_MAGIC, { scaleMode[0], scaleMode[1] },
	               { smooth[0], smooth[1] }, swapped, hudMode, audioMode, volA, volB, touchMode, fsOn, dofOn, bloomOn, lightOn, vividOn };
	FILE* f = fopen(SETTINGS_PATH, "wb");
	if (!f) return;
	fwrite(&s, 1, sizeof s, f);
	fclose(f);
}

// ---- Pause menu ----
enum { SESSION_CHANGE, SESSION_QUIT };
static const char* MENU_ITEMS[] = {
	"Resume", "Link", "Audio", "Touch", "Frameskip", "Toggle HUD", "Swap screens",
	"Save state", "Load state", "Load .sav (focused)", "Mute", "Pause", "DoF",
	"Bloom", "Light", "Vivid", "Wireless", "Change games", "Quit", "Net link"
};
#define MENU_N 20
#define MENU_LINK_IDX  1   // dynamic label ("Link: off/on")
#define MENU_AUDIO_IDX 2   // dynamic label ("Audio: <mode>")
#define MENU_TOUCH_IDX 3   // dynamic label ("Touch: on/off")
#define MENU_FS_IDX    4   // dynamic label ("Frameskip: on/off")
#define MENU_MUTE_IDX  10  // dynamic label ("Mute: on/off")
#define MENU_PAUSE_IDX 11  // dynamic label ("Pause A/B: on/off")
#define MENU_HUD_IDX   5   // dynamic label ("HUD: off/top/bottom/both")
#define MENU_DOF_IDX   12  // dynamic label ("DoF: on/off")
#define MENU_BLOOM_IDX 13  // dynamic label ("Bloom: on/off")
#define MENU_LIGHT_IDX 14  // dynamic label ("Light: on/off")
#define MENU_VIVID_IDX 15  // dynamic label ("Vivid: on/off")
#define MENU_WIRELESS_IDX 16  // opens the wireless lobby
#define MENU_NETLINK_IDX  19  // M2.5 net link (loopback) toggle — dynamic label
static const char* const HUD_NAMES[4] = { "off", "top", "bottom", "both" };

// Run one play session with the two chosen ROMs. Returns SESSION_CHANGE (re-pick) or
// SESSION_QUIT. Creates/destroys the cores + worker threads itself.
static int run_session(C3D_RenderTarget* top, C3D_RenderTarget* bot, C3D_RenderTarget* topR,
                       C2D_TextBuf txtBuf, bool isN3DS, s32 mainPrio, const char* pathA, const char* pathB) {
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

	// HD-2D M1: the DoF bounce target (RGB565, VRAM). DoF silently disables if it fails.
	C3D_Tex dofTexA;
	C3D_RenderTarget *dofTgtA = NULL;
	if (C3D_TexInitVRAM(&dofTexA, DOF_TEXA, DOF_TEXA, GPU_RGB565)) {
		C3D_TexSetFilter(&dofTexA, GPU_LINEAR, GPU_LINEAR);
		C3D_TexSetWrap(&dofTexA, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
		dofTgtA = C3D_RenderTargetCreateFromTex(&dofTexA, GPU_TEXFACE_2D, 0, -1);
	}

	// HD-2D M3: the bloom glow map (RGB565, VRAM). Bloom silently disables if it fails.
	C3D_Tex bloomTex;
	C3D_RenderTarget* bloomTgt = NULL;
	if (warpOk && C3D_TexInitVRAM(&bloomTex, BLOOM_TEX, BLOOM_TEX, GPU_RGB565)) {
		C3D_TexSetFilter(&bloomTex, GPU_LINEAR, GPU_LINEAR);
		C3D_TexSetWrap(&bloomTex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
		bloomTgt = C3D_RenderTargetCreateFromTex(&bloomTex, GPU_TEXFACE_2D, 0, -1);
	}

	// Audio: both cores share one rate (pitch-matched to the 3DS refresh); start clean.
	GbaCore* anyCore = emuA.core ? emuA.core : emuB.core;
	audio_thread_start(mainPrio, isN3DS);   // dedicated audio thread on the core-1 slice
	if (anyCore) audio_set_rate(anyCore);   // shared rate, clean start

	GbaLink* link = gbalink_create();   // shared lockstep coordinator; cores attach on demand
	bool linkOn = false;
	bool netOn  = false;   // M2.5 net link (loopback) active — mutually exclusive with linkOn
	int  touchMode = TOUCH_OFF;   // 0 off / 1 gamepad / 2 smart (touch drives the bottom game)
	bool fsOn = false;      // frameskip the unfocused game to free heavy-scene budget
	bool dofOn = true;      // HD-2D M1: tilt-shift depth-of-field on the top screen (overworld only)
	float dofLvlTop = 1.0f, dofLvlBot = 1.0f;   // per-band engagement 0..1 (text kills its band's blur)
	bool bloomOn = true;    // HD-2D M3: LDR bloom on the focused top screen (overworld only)
	bool lightOn = true;    // HD-2D M4: time-of-day lighting on the overworld
	bool vividOn = false;   // round 7: bright+sharp "sign look" everywhere (no lighting/DoF/bloom haze)
	float bloomLvl = 1.0f;  // eased like the DoF bands; any on-screen text kills the glow
	bool muted = false;     // HARD mute (stops audio rendering, saves CPU)

	int focused = 0;
	bool menuOpen = false;
	bool workersRunning = false;   // pipeline: a non-link frame is computing while we render the last
	DepthSnap depth3d = { false };  // top game's overworld state for stereoscopic depth (M2)
	int  menuSel = 0;
	int  result = SESSION_QUIT;
	gfxSet3D(true);   // enable stereoscopic top screen; the right eye is driven below (slider-gated)
	char status[24] = "";   // last save/load result, shown in the menu
	// Scale + filter are PER SCREEN ([0]=top, [1]=bottom): the 400x240 top and 320x240 bottom
	// have different best fits. ZR/ZL adjust the focused screen (X/Y switches focus).
	int  scaleMode[2] = { SCALE_FIT, SCALE_FIT };
	bool smooth[2]    = { false, false };
	bool swapped      = false;   // false: A=top / B=bottom. true: B=top / A=bottom.
	char toast[48] = "";
	int  toastTimer = 0;

	// HUD (menu-toggleable): per-screen game label + FPS + clock + battery.
	int  hudMode = 3;   // per-screen HUD/fps bitmask: 1=top 2=bottom
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

	settings_load(scaleMode, smooth, &swapped, &hudMode, &audioMode, &volA, &volB, &touchMode, &fsOn, &dofOn, &bloomOn, &lightOn, &vividOn);   // restore prefs
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
				if (!linkOn && !netOn && workersRunning) {   // finish the in-flight frame before pausing into the menu
					LightEvent_Wait(&emuA.done); LightEvent_Wait(&emuB.done);
					if (emuA.core) upload_frame(&emuA); if (emuB.core) upload_frame(&emuB);
					workersRunning = false;
				}
			} else {
				// PIPELINE: finish the PREVIOUS frame (started last iteration, ran during the render) and
				// snapshot it. We render N-1 while N computes -> render isn't chained to the slower core,
				// so non-link is as smooth as the link path. Workers are parked here -> touch RAM access safe.
				if (!linkOn && !netOn && workersRunning) {
					LightEvent_Wait(&emuA.done); LightEvent_Wait(&emuB.done);
					if (emuA.core) upload_frame(&emuA); if (emuB.core) upload_frame(&emuB);
					workersRunning = false;
				}
				if (kDown & KEY_Y) {                                         // switch focus
					focused ^= 1; audio_reset_stream();
					gbacore_set_frameskip(emuA.core, (fsOn && focused != 0) ? 2 : 0);
					gbacore_set_frameskip(emuB.core, (fsOn && focused != 1) ? 2 : 0);
				}
				if (kDown & KEY_X) {                                         // swap which game is on which screen
					swapped = !swapped;
					snprintf(toast, sizeof toast, "Layout: %s", swapped ? "B top / A bottom" : "A top / B bottom");
					toastTimer = 90;
					settings_save(scaleMode, smooth, swapped, hudMode, audioMode, volA, volB, touchMode, fsOn, dofOn, bloomOn, lightOn, vividOn);
				}
				int fs = swapped ? (focused ^ 1) : focused;   // screen the focused game sits on
				if (kDown & KEY_ZR) {
					scaleMode[fs] = (scaleMode[fs] + 1) % 3;
					snprintf(toast, sizeof toast, "%s scale: %s",
					         fs == 0 ? "Top" : "Bottom", SCALE_NAMES[scaleMode[fs]]);
					toastTimer = 90;
					settings_save(scaleMode, smooth, swapped, hudMode, audioMode, volA, volB, touchMode, fsOn, dofOn, bloomOn, lightOn, vividOn);
				}
				if (kDown & KEY_ZL) {
					smooth[fs] = !smooth[fs];   // render_game sets the per-pass filters
					snprintf(toast, sizeof toast, "%s filter: %s", fs == 0 ? "Top" : "Bottom",
					         smooth[fs] ? "Smooth" : "Sharp-bilinear");
					toastTimer = 90;
					settings_save(scaleMode, smooth, swapped, hudMode, audioMode, volA, volB, touchMode, fsOn, dofOn, bloomOn, lightOn, vividOn);
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
					if (touchMode == TOUCH_SMART) {   // game-aware touch works even during a link (benign EWRAM race)
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
							sm.prof = gp;
							sm.partyCount = gsr.partyCount; sm.partyLayout = gsr.partyLayout;
							sm.battlersCount = gsr.battlersCount; sm.absentMask = gsr.absentMask;
							for (int i = 0; i < 4; i++) sm.battlerPos[i] = gsr.battlerPos[i];
							sm.bagListTaskBase = gsr.bagListTaskBase;
						}
					}
					tk = touch_update(touchMode, touching, tp.px, tp.py, gx, gy, gvalid, &sm);
				}
				{   // stereoscopic depth: TOP game overworld state + on-screen OAM rects (cores parked)
					GbaCore* topCore = swapped ? emuB.core : emuA.core;
					const GameProfile* tprof = profile_for(topCore);
					GameState ts; depth3d.overworld = game_read(topCore, tprof, &ts) && ts.ctx == GCTX_OVERWORLD;
					depth3d.textTop = ts.textBanner;   // map-name banner lives in the top band
					depth3d.textBot = ts.textDlg;      // dialog textbox lives in the bottom band
					depth3d.nspr = 0; depth3d.nui = 0; depth3d.nfg = 0; depth3d.maxd = 0.0f; depth3d.camX = depth3d.camY = 0; memset(depth3d.tdepth, 0, sizeof depth3d.tdepth);
					if (topCore) bg0_scan(topCore, tprof, depth3d.overworld, &depth3d);   // text flags + gen-3 UI panel rects
					if (depth3d.overworld && topCore) {
						static const unsigned char SW[3][4] = {{8,16,32,64},{16,32,32,64},{8,8,16,32}};
						static const unsigned char SH[3][4] = {{8,16,32,64},{8,8,16,32},{16,32,32,64}};
						for (int i = 0; i < 128 && depth3d.nspr < DEPTH_MAX_SPR; i++) {
							u16 a0 = gbacore_read16(topCore, 0x07000000 + i*8 + 0);
							u16 a1 = gbacore_read16(topCore, 0x07000000 + i*8 + 2);
							int aff = (a0 >> 8) & 1;
							if (!aff && ((a0 >> 9) & 1)) continue;            // OBJ disabled
							int shape = (a0 >> 14) & 3; if (shape == 3) continue;
							int w = SW[shape][(a1 >> 14) & 3], h = SH[shape][(a1 >> 14) & 3];
							if (aff && ((a0 >> 9) & 1)) { w *= 2; h *= 2; }   // double-size
							if (w > 32 || h > 32) continue;                   // characters only (skip big effects/UI)
							int y = a0 & 0xFF; if (y >= 160) y -= 256;
							int x = a1 & 0x1FF; if (x >= 256) x -= 512;
							if (x + w <= 0 || x >= GBA_W || y + h <= 0 || y >= GBA_H) continue;
							depth3d.spr[depth3d.nspr].x = (short)x; depth3d.spr[depth3d.nspr].y = (short)y;
							depth3d.spr[depth3d.nspr].w = (unsigned char)w; depth3d.spr[depth3d.nspr].h = (unsigned char)h;
							depth3d.spr[depth3d.nspr].elev = 0xFF;   // 0xFF = unmatched -> floor_at fallback
							depth3d.nspr++;
						}
						build_depth_grid(topCore, tprof, ts.px, ts.py, &depth3d);   // scenery depth (elevation priority planes)
						// B/C: tag each on-screen sprite with its object-event elevation tier (previousElevation =
						// what the engine uses for draw priority) so NPCs/the player pop with the tier they stand on.
						if (tprof->mapObjects) {
							short ogx[16], ogy[16]; unsigned char oel[16]; int nobj = 0;
							for (int o = 0; o < 16; o++) {
								uint32_t oe = tprof->mapObjects + 0x24u * (uint32_t)o;
								if (!(gbacore_read32(topCore, oe) & 1u)) continue;                       // active:1
								ogx[nobj] = (short)(int16_t)gbacore_read16(topCore, oe + 0x10);          // currentCoords.x (grid, +7)
								ogy[nobj] = (short)(int16_t)gbacore_read16(topCore, oe + 0x12);          // currentCoords.y
								oel[nobj] = (gbacore_read8(topCore, oe + 0x0B) >> 4) & 0x0F;             // previousElevation (high nibble)
								nobj++;
							}
							for (int s = 0; s < depth3d.nspr; s++) {                                     // match each sprite by feet grid-tile
								int col = (depth3d.spr[s].x + depth3d.spr[s].w / 2) / 16;
								int row = (depth3d.spr[s].y + depth3d.spr[s].h - 1) / 16;
								if (col < 0) col = 0; else if (col > 14) col = 14;
								if (row < 0) row = 0; else if (row > 9) row = 9;
								int ggx = ts.px + col, ggy = ts.py + row + 2;
								int best = -1, bestd = 3;
								for (int o = 0; o < nobj; o++) {
									int dd = abs(ogx[o] - ggx) + abs(ogy[o] - ggy);
									if (dd < bestd) { bestd = dd; best = o; }
								}
								if (best >= 0) depth3d.spr[s].elev = oel[best];
							}
						}
					}
				}
				emuA.keys = ((focused == 0) ? g : 0) | (swapped ? tk : 0);
				emuB.keys = ((focused == 1) ? g : 0) | (swapped ? 0 : tk);
				if (linkOn || netOn) {
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
			if (kDown & (KEY_DDOWN | KEY_CPAD_DOWN)) { if (menuSel + 2 < MENU_N) menuSel += 2; else if ((menuSel & 1) && menuSel + 1 < MENU_N) menuSel = MENU_N - 1; }
			if (kDown & (KEY_DUP   | KEY_CPAD_UP))   { if (menuSel - 2 >= 0)      menuSel -= 2; }
			if (!onAudio && (kDown & (KEY_DRIGHT | KEY_CPAD_RIGHT))) { if (menuSel + 1 < MENU_N) menuSel++; }
			if (!onAudio && (kDown & (KEY_DLEFT  | KEY_CPAD_LEFT)))  { if (menuSel - 1 >= 0)      menuSel--; }
			if (menuSel == MENU_AUDIO_IDX && (kDown & (KEY_DLEFT | KEY_CPAD_LEFT | KEY_DRIGHT | KEY_CPAD_RIGHT))) {
					int* v = (focused == 0) ? &volA : &volB;   // adjust the focused game's volume
					*v += (kDown & (KEY_DRIGHT | KEY_CPAD_RIGHT)) ? 32 : -32;
					if (*v < 0) *v = 0; else if (*v > 256) *v = 256;
					snprintf(status, sizeof status, "Vol %c: %d%%", focused == 0 ? 'A' : 'B', *v * 100 / 256);
					settings_save(scaleMode, smooth, swapped, hudMode, audioMode, volA, volB, touchMode, fsOn, dofOn, bloomOn, lightOn, vividOn);
				}
				if (kDown & KEY_B) menuOpen = false;                 // resume
			bool activate = (kDown & KEY_A) != 0;
			if (kDown & KEY_TOUCH) {   // tap a button to select it
				touchPosition mtp; hidTouchRead(&mtp);
				for (int i = 0; i < MENU_N; i++) {
					float bx = 4.0f + (i & 1) * 160.0f, by = 2.0f + (i >> 1) * 22.0f;
					if (mtp.px >= bx && mtp.px < bx + 152 && mtp.py >= by && mtp.py < by + 20) {
						menuSel = i;
						if (i == MENU_AUDIO_IDX) {   // 3 sub-buttons: A vol | B vol | mode (no full-cell activate)
							int j = (int)((mtp.px - bx) / 50.5f); if (j < 0) j = 0; if (j > 2) j = 2;
							if      (j == 0) { volA += 64; if (volA > 256) volA = 0; }
							else if (j == 1) { volB += 64; if (volB > 256) volB = 0; }
							else            { audioMode = (audioMode + 1) % 3; audio_reset_stream(); }
							settings_save(scaleMode, smooth, swapped, hudMode, audioMode, volA, volB, touchMode, fsOn, dofOn, bloomOn, lightOn, vividOn);
						} else {
							activate = true;
						}
						break;
					}
				}
			}
			if (activate) {
				if      (menuSel == 0) menuOpen = false;         // Resume
				else if (menuSel == 1) {                         // Link cable (experimental)
					if (!emuA.core || !emuB.core || !link) {
						snprintf(status, sizeof status, "Link needs 2 games");
					} else if (netOn) {
						snprintf(status, sizeof status, "Turn Net link off first");
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
					settings_save(scaleMode, smooth, swapped, hudMode, audioMode, volA, volB, touchMode, fsOn, dofOn, bloomOn, lightOn, vividOn);
				}
				else if (menuSel == 3) {                         // Touch mode (off / gamepad / smart)
					touchMode = (touchMode + 1) % 3;
					snprintf(status, sizeof status, "Touch: %s", TOUCH_NAMES[touchMode]);
					settings_save(scaleMode, smooth, swapped, hudMode, audioMode, volA, volB, touchMode, fsOn, dofOn, bloomOn, lightOn, vividOn);
				}
				else if (menuSel == 4) {                         // Frameskip (unfocused game)
					fsOn = !fsOn;
					gbacore_set_frameskip(emuA.core, (fsOn && focused != 0) ? 2 : 0);
					gbacore_set_frameskip(emuB.core, (fsOn && focused != 1) ? 2 : 0);
					snprintf(status, sizeof status, "Frameskip %s", fsOn ? "on" : "off");
					settings_save(scaleMode, smooth, swapped, hudMode, audioMode, volA, volB, touchMode, fsOn, dofOn, bloomOn, lightOn, vividOn);
				}
				else if (menuSel == 5) {                         // Toggle HUD
					hudMode = (hudMode + 1) & 3;
					snprintf(status, sizeof status, "HUD: %s", HUD_NAMES[hudMode]);
					settings_save(scaleMode, smooth, swapped, hudMode, audioMode, volA, volB, touchMode, fsOn, dofOn, bloomOn, lightOn, vividOn);
				}
				else if (menuSel == 6) {                         // Swap screens
					swapped = !swapped; menuOpen = false;
					snprintf(toast, sizeof toast, "Layout: %s", swapped ? "B top / A bottom" : "A top / B bottom");
					toastTimer = 90;
					settings_save(scaleMode, smooth, swapped, hudMode, audioMode, volA, volB, touchMode, fsOn, dofOn, bloomOn, lightOn, vividOn);
				}
				else if ((menuSel == 7 || menuSel == 8 || menuSel == 9) && (linkOn || netOn)) {
					snprintf(status, sizeof status, "Stop the link first");   // save/load/.sav would race a live core
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
				else if (menuSel == MENU_DOF_IDX) {              // HD-2D tilt-shift DoF (top screen)
					dofOn = !dofOn;
					snprintf(status, sizeof status, "DoF %s", dofOn ? "on" : "off");
					settings_save(scaleMode, smooth, swapped, hudMode, audioMode, volA, volB, touchMode, fsOn, dofOn, bloomOn, lightOn, vividOn);
				}
				else if (menuSel == MENU_BLOOM_IDX) {            // HD-2D LDR bloom (focused top)
					bloomOn = !bloomOn;
					snprintf(status, sizeof status, "Bloom %s", bloomOn ? "on" : "off");
					settings_save(scaleMode, smooth, swapped, hudMode, audioMode, volA, volB, touchMode, fsOn, dofOn, bloomOn, lightOn, vividOn);
				}
				else if (menuSel == MENU_LIGHT_IDX) {            // HD-2D time-of-day lighting
					lightOn = !lightOn;
					snprintf(status, sizeof status, "Light %s", lightOn ? "on" : "off");
					settings_save(scaleMode, smooth, swapped, hudMode, audioMode, volA, volB, touchMode, fsOn, dofOn, bloomOn, lightOn, vividOn);
				}
				else if (menuSel == MENU_VIVID_IDX) {            // bright+sharp "sign look" everywhere
					vividOn = !vividOn;
					snprintf(status, sizeof status, "Vivid %s", vividOn ? "on" : "off");
					settings_save(scaleMode, smooth, swapped, hudMode, audioMode, volA, volB, touchMode, fsOn, dofOn, bloomOn, lightOn, vividOn);
				}
				else if (menuSel == MENU_WIRELESS_IDX) {        // wireless multi-console lobby (M1)
					EmuInstance* fg = (focused == 0) ? &emuA : &emuB;
					char gcode[5] = { 0 };
					if (fg->core) gbacore_game_code(fg->core, gcode);
					wireless_lobby_run(top, bot, txtBuf, gcode);
				}
				else if (menuSel == MENU_NETLINK_IDX) {         // M2.5 net link (loopback) — beta
					if (!emuA.core || !emuB.core) {
						snprintf(status, sizeof status, "Net link needs 2 games");
					} else if (linkOn) {
						snprintf(status, sizeof status, "Turn Link off first");
					} else if (!netOn) {
						if (workersRunning) { LightEvent_Wait(&emuA.done); LightEvent_Wait(&emuB.done); workersRunning = false; }
						net_link_set_loopback(true);
						net_transfer_reset();
						gbacore_net_attach(emuA.core, 0, 1);    // seat 0 = parent
						gbacore_net_attach(emuB.core, 1, 1);    // seat 1 = child
						emuA.netLinked = emuB.netLinked = true;
						netOn = true;
						LightEvent_Signal(&emuA.go);            // kick both workers into the net free-run
						LightEvent_Signal(&emuB.go);
						snprintf(status, sizeof status, "Net link: ON (loopback)");
					} else {
						emuA.netLinked = emuB.netLinked = false;  // net loops exit (collect times out <=50ms)
						LightEvent_Wait(&emuA.done);
						LightEvent_Wait(&emuB.done);
						gbacore_net_detach(emuA.core);
						gbacore_net_detach(emuB.core);
						netOn = false;
						snprintf(status, sizeof status, "Net link: off");
					}
				}
				else if (menuSel == 17) { result = SESSION_CHANGE; break; }
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
		char hudStat[56];
		if (hudMode) {
			time_t tt = time(NULL);
			struct tm* lt = localtime(&tt);
			snprintf(hudStat, sizeof hudStat, "%s %dfps %dms %02d:%02d %d/5 f%d d%.1f c%d,%d",
			         linkOn ? "LINK" : AUDIO_NAMES[audioMode], fps, showMs, lt ? lt->tm_hour : 0, lt ? lt->tm_min : 0, batLvl, depth3d.nfg, depth3d.maxd, depth3d.camX, depth3d.camY);
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
				} else if (i == MENU_NETLINK_IDX) {   // dynamic: "Net link: off/ON"
					snprintf(llabel, sizeof llabel, "Net link: %s", netOn ? "ON" : "off");
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
				} else if (i == MENU_HUD_IDX) {
					snprintf(llabel, sizeof llabel, "HUD: %s", HUD_NAMES[hudMode]);
					label = llabel;
				} else if (i == MENU_DOF_IDX) {
					snprintf(llabel, sizeof llabel, "DoF: %s", dofOn ? "on" : "off");
					label = llabel;
				} else if (i == MENU_BLOOM_IDX) {
					snprintf(llabel, sizeof llabel, "Bloom: %s", bloomOn ? "on" : "off");
					label = llabel;
				} else if (i == MENU_LIGHT_IDX) {
					snprintf(llabel, sizeof llabel, "Light: %s", lightOn ? "on" : "off");
					label = llabel;
				} else if (i == MENU_VIVID_IDX) {
					snprintf(llabel, sizeof llabel, "Vivid: %s", vividOn ? "on" : "off");
					label = llabel;
				}
				C2D_TextParse(&items[i], txtBuf, label); C2D_TextOptimize(&items[i]);
			}
			// Footer: last action result, or a controls cheat-sheet when idle.
			C2D_TextParse(&tStatus, txtBuf,
			              status[0] ? status : "Y focus  X swap  ZL/ZR filter/scale  L/R = GBA");
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
		float slider3d = osGet3DSliderState();
		bool s3dOn = slider3d > 0.03f && !menuOpen;   // 3D engaged AND not in the menu; else plain 2D (gates every 3D effect)
		bool pop3d = s3dOn && depth3d.overworld && topG->core;
		bool uipop = s3dOn && depth3d.nui > 0 && topG->core;   // BG0 panels pop in ANY context
		// Text-aware DoF: kill a band's blur the moment text/UI shows under it (BG0 scan + RAM
		// signals), ease back in afterwards (fast-out ~3 frames, slow-in ~12 -> no flicker).
		dofLvlTop += (depth3d.overworld && !depth3d.textTop) ? 0.08f : -0.34f;
		dofLvlBot += (depth3d.overworld && !depth3d.textBot) ? 0.08f : -0.34f;
		if (dofLvlTop > 1.0f) dofLvlTop = 1.0f; else if (dofLvlTop < 0.0f) dofLvlTop = 0.0f;
		if (dofLvlBot > 1.0f) dofLvlBot = 1.0f; else if (dofLvlBot < 0.0f) dofLvlBot = 0.0f;
		bloomLvl += (depth3d.overworld && !depth3d.textTop && !depth3d.textBot) ? 0.08f : -0.34f;
		if (bloomLvl > 1.0f) bloomLvl = 1.0f; else if (bloomLvl < 0.0f) bloomLvl = 0.0f;
		bool dofPass = s3dOn && !vividOn && dofOn && dofTgtA && depth3d.overworld && topG->core && (dofLvlTop > 0.01f || dofLvlBot > 0.01f);
		bool bloomPass = s3dOn && !vividOn && bloomOn && bloomTgt && dofTgtA && depth3d.overworld && topG->core
		              && focScreen == 0 && bloomLvl > 0.01f;   // focused top only (study budget rule)
		bool litPass = s3dOn && !vividOn && lightOn && depth3d.overworld && topG->core;   // time-of-day grade
		LightEnv lenv; if (litPass) { time_t _tt = time(NULL); struct tm* _lt = localtime(&_tt);
			lenv = light_for_hour(_lt ? _lt->tm_hour + _lt->tm_min / 60.0f : 12.0f); }
		if (dofPass || bloomPass) dof_prepare(&topG->tex, dofTgtA);   // shared half-res copy (DoF + bloom source)
		if (bloomPass) bloom_bright(&dofTexA, bloomTgt);              // bright-pass glow map, shared by both eyes
		bool sharpTop = !smooth[0] && scaleMode[0] != SCALE_1X && preTgt;     // matches render_game's two-pass choice
		u32 topMod = (focScreen == 0) ? 0xFFFFFFFFu : C2D_Color32(0x80, 0x80, 0x80, 0xFF);   // grid analog of dimTint
		render_game(topG, top, preTgt, &preTex, 400.0f, 240.0f, scaleMode[0], smooth[0], topTint, clrBg);
		if (pop3d) {   // M2: continuous grid warp (stretch, no tile tears); quad-warp fallback if no shader
			if (warpOk) warp_grid_eye(top, topG, &depth3d, scaleMode[0], +slider3d, sharpTop, &preTex, topMod, 0);
			else        warp_scenery_eye(top, topG, &depth3d, scaleMode[0], +slider3d);
			pop_eye(top, topG, &depth3d, scaleMode[0], +slider3d);   // LEFT eye shifts RIGHT -> pops OUT (ramp+char per sprite)
		}
		if (dofPass) dof_bands(top, &dofTexA, scaleMode[0], dofLvlTop, dofLvlBot, warpOk ? +slider3d : 0.0f);   // bands OVER the pops
		if (bloomPass) bloom_add(top, &bloomTex, scaleMode[0], bloomLvl, 0);   // additive glow, over the blur
		if (uipop) ui_pop_eye(top, topG, &depth3d, scaleMode[0], +UIPOP3D_PX * slider3d, sharpTop, &preTex);   // UI panels pop hardest
		if (litPass) light_pass(top, &depth3d, scaleMode[0], &lenv);   // lit LAST -> tints the UI panels too (sign is not a bright patch)
		if (!menuOpen) {
			if (hudMode & 1) {
				C2D_DrawRectSolid(0.0f, 0.0f, 0.0f, 400.0f, 14.0f, THEME_HUD_BAR);
				if (focScreen == 0) C2D_DrawRectSolid(0.0f, 14.0f, 0.0f, 400.0f, 2.0f, clrHi);
				C2D_DrawText(&tHudTop, C2D_WithColor, 4.0f, 1.0f, 0.0f, 0.4f, 0.4f, clrTxt);
				float sw, sh; C2D_TextGetDimensions(&tHudStat, 0.4f, 0.4f, &sw, &sh);
				C2D_DrawText(&tHudStat, C2D_WithColor, 396.0f - sw, 1.0f, 0.0f, 0.4f, 0.4f, clrTxt);
			} else if (focScreen == 0) {
				C2D_DrawRectSolid(0.0f, 0.0f, 0.0f, 400.0f, 4.0f, clrHi);
			}
			if (toastTimer > 0) C2D_DrawText(&tToast, C2D_WithColor, 8.0f, (hudMode & 1) ? 20.0f : 8.0f, 0.0f, 0.5f, 0.5f, clrHi);
		}

		// top RIGHT eye = the SAME (top) game -> single-game stereoscopic depth. Player pops forward
		// (positive disparity). (Per-eye dual-game retired; can return later as a menu toggle.)
		render_game(topG, topR, preTgt, &preTex, 400.0f, 240.0f, scaleMode[0], smooth[0], NULL, clrBg);
		if (pop3d) {
			if (warpOk) warp_grid_eye(topR, topG, &depth3d, scaleMode[0], -slider3d, sharpTop, &preTex, 0xFFFFFFFFu, 1);
			else        warp_scenery_eye(topR, topG, &depth3d, scaleMode[0], -slider3d);
			pop_eye(topR, topG, &depth3d, scaleMode[0], -slider3d);   // RIGHT eye shifts LEFT
		}
		if (dofPass) dof_bands(topR, &dofTexA, scaleMode[0], dofLvlTop, dofLvlBot, warpOk ? -slider3d : 0.0f);
		if (bloomPass) bloom_add(topR, &bloomTex, scaleMode[0], bloomLvl, 1);
		if (uipop) ui_pop_eye(topR, topG, &depth3d, scaleMode[0], -UIPOP3D_PX * slider3d, sharpTop, &preTex);
		if (litPass) light_pass(topR, &depth3d, scaleMode[0], &lenv);

		// bottom screen (+ menu overlay when open). render_game leaves `bot` bound.
		render_game(botG, bot, preTgt, &preTex, 320.0f, 240.0f, scaleMode[1], smooth[1], botTint, clrBg);
		if (!menuOpen) {
			if (hudMode & 2) {
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
			if (touchMode == TOUCH_SMART && sm.valid) {   // TEMP debug: confirm RAM reads on device
				static const char* const CTXN[] = { "none", "field", "b.act", "b.move", "b.tgt", "party", "fmenu", "bag", "b.oth" };
				char kb[8]; int ki = 0;   // decode the key touch is injecting this frame (on-device diagnostic)
				if (tk & (1 << GBAKEY_UP))    kb[ki++] = 'U';
				if (tk & (1 << GBAKEY_DOWN))  kb[ki++] = 'D';
				if (tk & (1 << GBAKEY_LEFT))  kb[ki++] = 'L';
				if (tk & (1 << GBAKEY_RIGHT)) kb[ki++] = 'R';
				if (tk & (1 << GBAKEY_A))     kb[ki++] = 'A';
				if (tk & (1 << GBAKEY_B))     kb[ki++] = 'B';
				if (!ki) kb[ki++] = '-';
				kb[ki] = '\0';
				char gs[64]; snprintf(gs, sizeof gs, "%s p=%d,%d key=%s", CTXN[sm.ctx], sm.px, sm.py, kb);
				C2D_Text tg; C2D_TextParse(&tg, txtBuf, gs); C2D_TextOptimize(&tg);
				C2D_DrawText(&tg, C2D_WithColor, 4.0f, 224.0f, 0.0f, 0.40f, 0.40f, C2D_Color32(0x42, 0xF5, 0xD0, 0xFF));
			}
		}
		if (menuOpen) {
			C2D_DrawRectSolid(0.0f, 0.0f, 0.0f, 320.0f, 240.0f, clrDim);
			for (int i = 0; i < MENU_N; i++) {   // 2x10 grid of buttons (D-pad or tap to select)
				float bx = 4.0f + (i & 1) * 160.0f, by = 2.0f + (i >> 1) * 22.0f;
				bool s = (i == menuSel);
				if (i == MENU_AUDIO_IDX) {   // split cell: A vol | B vol | mode
					char vlab[3][12];
					snprintf(vlab[0], 12, "A%d%%", volA * 100 / 256);
					snprintf(vlab[1], 12, "B%d%%", volB * 100 / 256);
					snprintf(vlab[2], 12, "%s", AUDIO_NAMES[audioMode]);
					for (int j = 0; j < 3; j++) {
						float sx = bx + j * 50.5f;
						C2D_DrawRectSolid(sx, by, 0.0f, 49.0f, 20.0f, s ? clrHi : clrPanel);
						C2D_Text st; C2D_TextParse(&st, txtBuf, vlab[j]); C2D_TextOptimize(&st);
						C2D_DrawText(&st, C2D_WithColor, sx + 4.0f, by + 5.0f, 0.0f, 0.36f, 0.36f, s ? clrSelTxt : clrTxt);
					}
					continue;
				}
				C2D_DrawRectSolid(bx, by, 0.0f, 152.0f, 20.0f, s ? clrHi : clrPanel);
				C2D_DrawText(&items[i], C2D_WithColor, bx + 6.0f, by + 5.0f, 0.0f, 0.4f, 0.4f, s ? clrSelTxt : clrTxt);
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
	emuA.netLinked = emuB.netLinked = false;  // ...and the net free-run loop (symmetric teardown)
	g_quit = true;
	LightEvent_Signal(&emuA.waitEv);          // release any worker parked on a link wait
	LightEvent_Signal(&emuB.waitEv);
	LightEvent_Signal(&emuA.go);
	LightEvent_Signal(&emuB.go);
	if (emuA.thread) { threadJoin(emuA.thread, U64_MAX); threadFree(emuA.thread); }
	if (emuB.thread) { threadJoin(emuB.thread, U64_MAX); threadFree(emuB.thread); }
	audio_thread_stop();   // workers joined -> nothing pumps the rings; safe to stop audio + free them
	if (linkOn) { gbacore_link_detach(emuA.core); gbacore_link_detach(emuB.core); }
	if (netOn)  { gbacore_net_detach(emuA.core);  gbacore_net_detach(emuB.core);  }
	teardown_core(&emuA);
	teardown_core(&emuB);
	gbalink_destroy(link);
	if (preTgt) { C3D_RenderTargetDelete(preTgt); C3D_TexDelete(&preTex); }
	if (dofTgtA) { C3D_RenderTargetDelete(dofTgtA); C3D_TexDelete(&dofTexA); }
	if (bloomTgt) { C3D_RenderTargetDelete(bloomTgt); C3D_TexDelete(&bloomTex); }
	g_quit = false;
	gfxSet3D(false);   // back to flat for the ROM picker / splash between sessions
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
	mkdir("sdmc:/3DGBA", 0777);   // first-run: make the data folder so a fresh console finds ROMs/saves/settings
	C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
	C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
	C2D_Prepare();
	warp_grid_init();   // M2 grid-warp shader (falls back to the quad warp if it fails)
	audio_init();   // ndsp; silently no-ops if dspfirm.cdc isn't present
	netlink_init(); // wireless link (UDS); no-ops without the .cia's nwm::UDS grant
	s_hasPtm = R_SUCCEEDED(ptmuInit());   // battery level for the HUD
	C3D_RenderTarget* top  = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
	C3D_RenderTarget* topR = C2D_CreateScreenTarget(GFX_TOP, GFX_RIGHT);   // right eye (stereoscopic 3D)
	C3D_RenderTarget* bot  = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
	C2D_TextBuf txtBuf = C2D_TextBufNew(4096);

	s32 mainPrio = 0x30;
	svcGetThreadPriority(&mainPrio, CUR_THREAD_HANDLE);

	run_splash(top, bot, txtBuf, perfWarn);   // animated boot splash (skippable) + perf warning

	// Session loop: pick two ROMs, play, and on "Change games" pick again.
	while (aptMainLoop()) {
		char pathA[256], pathB[256];
		if (!rompicker_run(top, bot, txtBuf, pathA, pathB, sizeof pathA)) {
			strcpy(pathA, "sdmc:/3DGBA/gameA.gba");
			strcpy(pathB, "sdmc:/3DGBA/gameB.gba");
		} else {
			rompicker_save_recent(pathA, pathB);   // remember for next boot's resume prompt
		}
		int r = run_session(top, bot, topR, txtBuf, isN3DS, mainPrio, pathA, pathB);
		if (r == SESSION_QUIT) break;
		// SESSION_CHANGE -> loop back to the picker
	}

	netlink_exit();
	audio_exit();
	if (s_hasPtm) ptmuExit();
	C2D_TextBufDelete(txtBuf);
	warp_grid_fini();
	C2D_Fini();
	C3D_Fini();
	gfxExit();
	return 0;
}
