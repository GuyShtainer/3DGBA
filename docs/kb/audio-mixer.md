# Audio — mixer policy (solo / mixed / split)

**Verified against mGBA + libctru source (2026-06-03).** This is the dual-gba *policy*
for combining two cores' audio. The ndsp hardware output path (channels, wave buffers,
rates) is the toolkit's concern — the `ctr-audio` agent owns it; this doc owns *which
samples get sent and how they're combined*.

## The baseline: Solo ("hear only what's played") — M1/M4 default

One ndsp channel (channel 0, stereo PCM16) plays one stream at a time, so **feed it only
the focused core**. Per frame, after both cores' `runFrame`:

- **Focused core:** drain `core->getAudioBuffer(core)` with
  `mAudioBufferRead(buf, dspBuffer[slot].data_pcm16, AUDIO_SAMPLES)` into a free
  `ndspWaveBuf` slot → `DSP_FlushDataCache` → `ndspChnWaveBufAdd(0, &dspBuffer[slot])`.
  This is exactly mGBA's `_postAudioBuffer` flow (`AUDIO_SAMPLES = 1280`, `DSP_BUFFERS = 4`).
- **Unfocused core — CRITICAL:** `mAudioBufferRead` is a **consuming** read, so the
  unfocused core's buffer must **still be drained every frame** (`mAudioBufferRead` into a
  throwaway buffer, or cheaper `mAudioBufferClear(buf)`) — otherwise it fills, the core
  backpressures, and **emulation stalls**. Just never `ndspChnWaveBufAdd` those samples.
- **On focus switch:** call `ndspChnWaveBufClear(0)` so the previous game's already-queued
  tail doesn't keep playing.

Do **not** use `enableAudioChannel`/`listAudioChannels` — those gate the emulated PSG/FIFO
channels *inside* one core, not whole-core muting.

Pull audio **manually in the coordinator** after `runFrame` (rather than `setAVStream`
callbacks) — simpler in our thread model, and avoids callbacks firing on a worker thread.

## The richer modes (M4) — one code path

Implement all modes as a single mixer stage:

```
out = gainA · panA · samplesA  +  gainB · panB · samplesB
```

where the **mode** selects the pan matrix and which gains are live:

| Mode | panA / panB | gains | notes |
|---|---|---|---|
| **Solo** (default) | both centered | unfocused gain = 0 | the baseline above; only one stream reaches ndsp |
| **Mixed** | both centered | both live | software-sum both cores' int16 streams into one buffer before `ndspChnWaveBufAdd` |
| **Split** | A→left, B→right | both live | interleave each core's mono-summed output into opposite channels |

Plus an independent **per-game volume** (`gainA`, `gainB`) applied in every mode.
**Clamp on sum** (saturating add) to avoid int16 overflow/clipping when both play.

In Mixed/Split both cores' samples are read and combined every frame, so the "drain the
unfocused core" rule is automatically satisfied; in Solo it must be done explicitly.

## Notes / verify-in-code

- Default `audioSampleRate` (≈32768 Hz) and the `fpsRatio` constant
  (`16756991./280095.`) used for `ndspChnSetRate` were **not** re-verified in source —
  confirm when wiring the rate.
- Keep this mixer **pure C and host-testable** (per the toolkit pure-cores convention) —
  it's just sample math; no libctru/citro headers in the mixing function.

## References

- `include/mgba-util/audio-buffer.h`, `src/util/audio-buffer.c` — `mAudioBufferRead`
  (consuming), `mAudioBufferClear`, `mAudioBufferAvailable`.
- `src/platform/3ds/main.c` — `_postAudioBuffer` (the ndsp `AUDIO_SAMPLES=1280`,
  `DSP_BUFFERS=4` model to mirror).
- libctru `<3ds/ndsp/ndsp.h>` — `ndspChnWaveBufAdd`, `ndspChnWaveBufClear`,
  `ndspChnSetRate`, `ndspChnSetFormat(NDSP_FORMAT_STEREO_PCM16)`; `DSP_FlushDataCache`.

Sample *source* belongs to [mgba-integration.md](mgba-integration.md); the ndsp output
plumbing belongs to the toolkit `ctr-audio` agent.
