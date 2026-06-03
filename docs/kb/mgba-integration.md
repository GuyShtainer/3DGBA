# Embedding mGBA — the core, not the frontend

**Verdict (verified against mGBA master source, 2026-06-03):** use mGBA's
**platform-independent core as a static `libmgba.a`** and embed **two `mCore`
instances** into our existing skeleton. **Do *not* fork mGBA's 3DS frontend.**

## Why embed, don't fork

- mGBA's 3DS frontend (`src/platform/3ds/main.c`, ~1117 lines) is **single-core,
  single-active-screen**: one `struct mGUIRunner` with one `struct mCore* core`, one
  `static mColor* outputBuffer`, and a `ScreenMode` enum that routes that *one* image to
  top **or** bottom. Its "core2" flag is just New-3DS second-CPU detection that turns on
  `threadedVideo` for the *same* core — **not** a second emulator. Forking it means
  gutting its control flow only to re-add the dual-core coordinator we already have.
- **We already own the hard part.** [source/main.c](../../source/main.c) is the dual-core
  3DS scaffold mGBA lacks: two worker threads pinned to separate CPUs, a per-frame
  `LightEvent` go/done handshake, dual-screen citro2d on the main thread, X/Y focus, and a
  per-instance keys/frame/skip contract. `emu_step()` is a one-line stub.
- **Everything we need is in the portable core.** Two `GBACoreCreate` instances are
  self-contained (malloc'd, no globals → they coexist); the link cable
  (`src/gba/sio/lockstep.c`) and per-core audio (`core->getAudioBuffer`) are
  platform-independent and proven by mGBA's own Qt `MultiplayerController`.
- **Build fit:** embedding adds `libmgba.a` + headers to our existing
  `-lcitro2d -lcitro3d -lctru -lm` line; forking would import mGBA's whole CMake/Picasso/
  makerom apparatus.

## The mCore vtable maps 1:1 onto our skeleton

| Skeleton (`EmuInstance`) | mGBA call |
|---|---|
| `emu_step(e)` | `core->runFrame(core)` |
| `e->keys` | `core->setKeys(core, keys)` (GBA keypad bits; `GBA_KEY_A=0…`) |
| `e->frame` (video out) | the `mColor*` buffer set via `core->setVideoBuffer(core, fb, 256)` |
| (audio) | `core->getAudioBuffer(core)` → `mAudioBufferRead` (see [audio-mixer.md](audio-mixer.md)) |
| (link) | `core->setPeripheral(core, mPERIPH_GBA_LINK_PORT, &driver->d)` (see [link-cable-lockstep.md](link-cable-lockstep.md)) |

Canonical embed sequence (mirror `src/feature/gui/gui-runner.c`):
`core = mCoreCreate(mPLATFORM_GBA)` (or `mCoreFind(path)`) → `core->init(core)` →
`mCoreInitConfig(core, NULL)` → allocate a **256-stride** `mColor` framebuffer +
`core->setVideoBuffer(core, fb, 256)` → open ROM as a `VFile` (`VFileOpen`) +
`core->loadROM` → `mCoreAutoloadSave(core)` → `core->reset(core)`. Per frame:
`core->setKeys` then `core->runFrame`; upload the 240×160 region to a tiled PICA200
texture (toolkit `pica-gpu`) and run frames under the toolkit `n3ds-systems` thread model.

## Building libmgba for 3DS (M1a)

Cross-compile **only** the core as a static archive with the devkitARM toolchain
(`-march=armv6k -mtune=mpcore -mfloat-abi=hard`, matching our Makefile). Turn the
frontends off (`BUILD_QT=OFF`, `BUILD_SDL=OFF`, 3DS frontend off). **Locked build
flags — identical for the lib and everything that links it, or you get silent ABI drift:**

- `COLOR_16_BIT=ON` → `mColor == uint16_t` RGB565, matching citro2d.
- `ENABLE_VFS=ON` (ROM loading), `M_CORE_GBA=ON`.
- **Threading ON** (`DISABLE_THREADING=OFF`) — the shipped lockstep user
  (`mLockstepThreadUser`) is guarded by `!DISABLE_THREADING`; turning it off loses the
  link feature's shipped path.

Output: `libmgba.a` + the `include/mgba` and `include/mgba-util` header trees. Add to
[../../Makefile](../../Makefile): `-lmgba` after a `-L` to its lib dir; its include path on `INCLUDE`.

## Milestones (detail; see toolkit ../../../docs/ROADMAP.md for M0–M4)

- **M1a** build `libmgba.a` · **M1b** one core → top screen (proves the embed) ·
  **M2** second core on CPU core 2 → bottom screen, **measure the two-core budget on
  hardware** · **M3** link cable · **M4** audio mixer modes + focus UX + saves.

## Performance — the real risk (not architecture)

mGBA is a **pure interpreter (no dynarec)**. A *single* core already drops to ~48–50 fps
in heavy scenes on New 3DS (upstream issue #2460). Two cores in one 60 Hz frame may not
hold full speed in demanding titles. Mitigations, in order: (1) **frameskip the
unfocused game** (still *step* it for link/audio correctness); (2) `APT_SetAppCpuTimeLimit`
+ CIA core-2/804 MHz/L2 (already in [app.rsf](../../app.rsf)); (3) fall back to **gpSP**
(dynarec, faster, lower accuracy) — **but gpSP has no in-process lockstep, so the
link-cable feature likely does not survive that swap.** Decide at **M2, on hardware**.

## Risks / verify-in-code

- **Build:** confirm mGBA's CMake yields a clean standalone `libmgba.a` for the 3DS target
  without dragging in SDL/Qt/3DS frontends.
- **ABI drift:** `COLOR_16_BIT`, `ENABLE_VFS`, `MINIMAL_CORE`, `DISABLE_THREADING` all
  change struct layouts/symbols — same flags everywhere.
- **API version:** wiring assumes current **master** (Coordinator/Driver API). Pin a known
  commit and record it; the old `GBASIOLockstepNode` API was removed for GBA pre-0.11.
- **Memory:** two cores + two framebuffers + two audio rings vs the 124 MB app budget
  (`SystemModeExt:124MB`) — measure against the ctru linear heap.
- **No prior art:** nobody has run two mGBA cores on a 3DS with dual-screen/on-device link.
- **License:** MPL-2.0, file-level copyleft — embedding unmodified `libmgba` keeps
  obligations to the bundled mGBA source we redistribute. See ../../../docs/kb/licensing.md.

## References (mGBA master, fetched 2026-06-03 — pin a commit)

- `include/mgba/core/core.h` — `struct mCore` vtable; `mCoreCreate(mPLATFORM_GBA)`/`mCoreFind`.
- `src/feature/gui/gui-runner.c` — the embed sequence to mirror.
- `src/platform/3ds/main.c` — the single-core frontend we are **not** forking; its
  `_postAudioBuffer` ndsp flow (`AUDIO_SAMPLES=1280`, `DSP_BUFFERS=4`) is the audio model to copy.
- `src/gba/core.c` — `GBACoreCreate` (self-contained instance); `_GBACoreSetPeripheral`.
- mGBA issue #2460 (single-core ~48–50 fps on 3DS); #3022 (on-device link historically "not feasible" *networked* — ours is in-process, different).
