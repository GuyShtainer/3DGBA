// gbacore.h — plain-C interface to one embedded mGBA GBA core.
// No mGBA or libctru types leak through here, so main.c (libctru/citro2d) and
// gbacore.c (mGBA) stay in separate translation units — they both define u8/u16.
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>   // size_t

#define GBA_W          240   // GBA visible width
#define GBA_H          160   // GBA visible height
#define GBA_FB_STRIDE  256   // pixels per row of the framebuffer we hand the core

// GBA keypad bit indices (KEYINPUT order); build a mask as (1 << GBAKEY_x).
enum {
	GBAKEY_A = 0, GBAKEY_B = 1, GBAKEY_SELECT = 2, GBAKEY_START = 3,
	GBAKEY_RIGHT = 4, GBAKEY_LEFT = 5, GBAKEY_UP = 6, GBAKEY_DOWN = 7,
	GBAKEY_R = 8, GBAKEY_L = 9
};

typedef struct GbaCore GbaCore;
typedef struct GbaLink GbaLink;   // shared lockstep coordinator for two linked cores

// Create + init a GBA core. NULL on failure.
GbaCore* gbacore_create(void);

// Hand the core a caller-owned RGB565 framebuffer (>= stride*GBA_H pixels).
// Call before gbacore_load_rom().
void     gbacore_set_video_buffer(GbaCore* c, uint16_t* buf, unsigned stride);

// Preload the ROM into a per-core buffer, load <rom>.sav (writable), and reset. false on fail.
bool     gbacore_load_rom(GbaCore* c, const char* path);

// Load an explicit battery .sav into a running core and reset so it boots with it. false on fail.
bool     gbacore_load_save(GbaCore* c, const char* path);

void     gbacore_set_keys(GbaCore* c, uint16_t gba_keys);  // mask of (1 << GBAKEY_x)
// Video frameskip: render 1 of every (n+1) GBA frames (CPU still runs full speed, so SIO/link
// and audio timing are unaffected). Used to give the focused game more budget in heavy scenes.
void     gbacore_set_frameskip(GbaCore* c, int n);
void     gbacore_run_frame(GbaCore* c);                    // run one whole video frame
// One CPU run-slice (core->runLoop): returns promptly when the lockstep sets earlyExit, so a
// linked worker can park at the exact transfer point. Same primitive mGBA's mCoreThread uses.
void     gbacore_run_loop(GbaCore* c);

// Save/load an emulator save-state to <rom-path>.ss<slot>. Returns false on failure
// (e.g. no state file to load). Separate from the in-game .sav (battery) save.
bool     gbacore_save_state(GbaCore* c, int slot);
bool     gbacore_load_state(GbaCore* c, int slot);

// --- Audio (v0.7 solo): pull PCM from the focused core; drain the unfocused one. ---
// Samples are interleaved stereo int16 (L,R). "frames" == stereo sample pairs.
unsigned gbacore_sample_rate(GbaCore* c);   // Hz (32768 fallback if the core reports 0)
unsigned gbacore_ndsp_rate(GbaCore* c);     // rate matched to the 3DS LCD refresh (no pitch drift)
size_t   gbacore_audio_available(GbaCore* c);                          // frames ready to read
size_t   gbacore_read_audio(GbaCore* c, int16_t* out, size_t frames);  // returns frames read
void     gbacore_drain_audio(GbaCore* c);                              // discard pending audio

// --- Link cable (v0.8, experimental): in-process mGBA SIO lockstep between two cores. ---
// onSleep/onWake are plain-C callbacks the mGBA lockstep invokes to park/resume a core's
// worker thread (onSleep on the core's own thread, onWake from the peer's thread). They must
// NOT block inside onSleep (it only requests a wait; the worker blocks after runFrame returns).
GbaLink* gbalink_create(void);
void     gbalink_destroy(GbaLink* link);
void     gbacore_link_attach(GbaCore* c, GbaLink* link, int requestedId,
                             void (*onSleep)(void*), void (*onWake)(void*), void* ctx);
void     gbacore_link_detach(GbaCore* c);

// Net link (M2.5): an ALTERNATIVE to gbalink_* — routes a GBA-MULTI transfer over the wireless
// transfer plane (netlink.c) instead of the in-process lockstep coordinator. seat = GBA playerId
// (0 = parent/clock-owner), peers = number of other GBAs. NEVER attach this AND gbalink to one core.
void     gbacore_net_attach(GbaCore* c, int seat, int peers);
void     gbacore_net_detach(GbaCore* c);
void     gbacore_net_poll(GbaCore* c);        // child-side per-slice hook; call ONLY from the core's worker

uint32_t gbacore_frame_counter(GbaCore* c);   // bumps once per produced video frame

// --- Live RAM access + game id (v1.1 game-aware touch) ---
// Read the running game's bus (EWRAM 0x02000000, IWRAM 0x03000000, ROM 0x08000000, ...).
uint8_t  gbacore_read8(GbaCore* c, uint32_t addr);
uint16_t gbacore_read16(GbaCore* c, uint32_t addr);
uint32_t gbacore_read32(GbaCore* c, uint32_t addr);
// Write a byte/halfword to the running game's bus (set a menu cursor before injecting A). Same-thread/parked only.
void     gbacore_write8(GbaCore* c, uint32_t addr, uint8_t val);
void     gbacore_write16(GbaCore* c, uint32_t addr, uint16_t val);
// 4-char ROM game code from the header (e.g. "BPEE"); out must hold >=5 bytes (NUL-terminated).
void     gbacore_game_code(GbaCore* c, char out[5]);

void     gbacore_destroy(GbaCore* c);
