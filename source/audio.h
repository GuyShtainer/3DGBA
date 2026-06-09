// audio.h — ndsp output for the two cores (v0.7): one stereo channel, mode-mixed.
// SOLO = focused core only; MIXED = both summed; SPLIT = A->left / B->right. Per-game volume.
#pragma once
#include <stdbool.h>
#include "gbacore.h"

enum { AUD_SOLO, AUD_MIXED, AUD_SPLIT };
extern const char* const AUDIO_NAMES[3];

void audio_init(void);                  // bring up ndsp (no-op/silent if dspfirm.cdc is missing)
void audio_exit(void);
bool audio_ready(void);                 // true if ndsp came up
void audio_set_rate(GbaCore* anyCore);  // set playback rate from a core's clock + reset the stream
void audio_reset_stream(void);          // drop queued audio (clean cut on focus switch / pause)

// Mix both cores into ndsp per `mode`; volA/volB are 0..256 (256 = 100%). Pass either core NULL if absent.
void audio_feed(GbaCore* ca, GbaCore* cb, int focused, int mode, int volA, int volB);
