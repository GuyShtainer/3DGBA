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

// Create + init a GBA core. NULL on failure.
GbaCore* gbacore_create(void);

// Hand the core a caller-owned RGB565 framebuffer (>= stride*GBA_H pixels).
// Call before gbacore_load_rom().
void     gbacore_set_video_buffer(GbaCore* c, uint16_t* buf, unsigned stride);

// Preload the ROM into a per-core buffer, autoload its .sav, and reset. false on failure.
bool     gbacore_load_rom(GbaCore* c, const char* path);

void     gbacore_set_keys(GbaCore* c, uint16_t gba_keys);  // mask of (1 << GBAKEY_x)
void     gbacore_run_frame(GbaCore* c);                    // one frame; drains audio (no sound yet)

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

void     gbacore_destroy(GbaCore* c);
