// audio.h — offloaded ndsp output (v1.2). A dedicated audio thread, pinned to the New-3DS core-1
// syscore slice, owns the ndsp channel. Each emulator worker drains its OWN core's audio into a
// lock-free SPSC ring (audio_pump_core, on that worker thread); the audio thread mixes the two
// rings and feeds ndsp — off the render frame's critical path. This recovers 60fps-with-audio in
// heavy scenes AND lets audio keep playing during a link (each ring has a single writer, no race).
//   SOLO = focused core only; MIXED = both summed; SPLIT = A->left / B->right. Per-game volume.
#pragma once
#include <3ds.h>
#include <stdbool.h>
#include "gbacore.h"

enum { AUD_SOLO, AUD_MIXED, AUD_SPLIT };
extern const char* const AUDIO_NAMES[3];

void audio_init(void);     // bring up ndsp once (no-op/silent without dspfirm.cdc)
void audio_exit(void);
bool audio_ready(void);

// Per-session audio thread (owns the ndsp channel). Start after the cores are set up; stop AFTER
// the worker threads have been joined, so nothing pumps the rings during teardown.
void audio_thread_start(s32 mainPrio, bool isN3DS);
void audio_thread_stop(void);

// Worker-side handoff: drain this core's produced audio into its SPSC ring. Call ONLY from that
// core's worker thread (slot 0 = game A, 1 = game B), once per produced frame/slice.
void audio_pump_core(int slot, GbaCore* core);

// Main-side controls (the audio thread honors these). set_params is cheap — call it every frame.
void audio_set_params(int focused, int mode, int volA, int volB);
void audio_set_rate(GbaCore* anyCore);   // match ndsp playback rate to a core's clock (+ clean cut)
void audio_reset_stream(void);           // request a clean cut (focus / mode / pause change)
void audio_set_muted(bool m);            // HARD mute: stop pumping/mixing/feeding entirely (saves CPU)
