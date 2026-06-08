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

### ✅ Verified recipe (2026-06-03, mGBA master @ `92621ea`; macOS arm64, devkitARM gcc 15.2.0, CMake 4.3.3)

Cloned to `external/` (gitignored). Three non-obvious gotchas, all confirmed:

1. **Use mGBA's bundled 3DS toolchain**, `src/platform/3ds/CMakeToolchain.txt` — it sets
   `3DS ON`, `-D__3DS__`, and arch flags (`-march=armv6k -mtune=mpcore -mfloat-abi=hard
   -mtp=soft`) that match our app's ABI. **Not** devkitPro's generic `cmake/3DS.cmake` (that
   sets `CMAKE_SYSTEM_NAME` but not the `3DS` var mGBA's CMake keys on).
2. **Make generator, not Ninja.** The 3DS *frontend* subdir emits duplicate `mgba.3dsx`
   rules → Ninja's generate step hard-fails ("multiple rules generate"); Make only warns.
3. **`-DCMAKE_POLICY_VERSION_MINIMUM=3.5`** is required — bundled zlib's
   `cmake_minimum_required` predates CMake 4. And **build only the `mgba` target** (the
   static lib) to skip the frontend `.3dsx`.

```bash
export DEVKITPRO=/opt/devkitpro
export DEVKITARM=/opt/devkitpro/devkitARM        # on its OWN line — one-line export mis-expands $DEVKITPRO
cd external/mgba && mkdir build-3ds && cd build-3ds   # keep FIXED_ROM_BUFFER (clone default) — required on 3DS
cmake -G "Unix Makefiles" \
  -DCMAKE_TOOLCHAIN_FILE=../src/platform/3ds/CMakeToolchain.txt \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
  -DBUILD_QT=OFF -DBUILD_SDL=OFF -DCOLOR_16_BIT=ON -DCOLOR_5_6_5=ON ..
make mgba -j$(sysctl -n hw.ncpu)        # -> build-3ds/libmgba.a (~7.7 MB)
```

> **Link cable (v0.8) needs one CMake edit before this step.** mGBA's bare `mgba` library
> target compiles `sio.c` + `sio/gbp.c` but **not** `sio/lockstep.c` (it lives in a separate
> `SIO_FILES` group only the SDL/Qt frontends pull in). So `GBASIOLockstepCoordinator*` etc.
> are undefined when you link the dual-GBA link code. Fix: in **`src/gba/CMakeLists.txt`**
> move `sio/lockstep.c` from `SIO_FILES` into `SOURCE_FILES` (the `GBA_SRC` list), then
> rebuild (`make mgba`). `external/` is git-ignored, so this edit isn't committed — it must
> be re-applied on a fresh clone. (Leave `sio/dolphin.c` out — it's Dolphin netplay, unneeded.)

### Using it without ABI drift (CRITICAL)

The public headers' struct layouts depend on compile-time macros, and **`mgba/flags.h` is
NOT the source of truth** (it disagreed with the real build — e.g. it marks
`ENABLE_DIRECTORIES`/`FIXED_ROM_BUFFER` undef while the lib was built *with* them, and lists
`MINIMAL_CORE` which the lib was built *without*). The authoritative set is the **C_DEFINES
in `build-3ds/CMakeFiles/mgba.dir/flags.make`**. Compile our mgba-touching code with that
exact set. As built here it is:

```
-DBUILD_STATIC -DCOLOR_16_BIT -DCOLOR_5_6_5 -DENABLE_DIRECTORIES -DENABLE_SCRIPTING
-DENABLE_VFS -DENABLE_VFS_FD -DFIXED_ROM_BUFFER -DM_CORE_GB -DM_CORE_GBA -DUSE_LZMA
-DUSE_MINIZIP -DUSE_PNG -DUSE_ZLIB -D__3DS__   (+ the HAVE_*/_GNU_SOURCE/IOAPI_NO_64 set)
```

- **Includes:** `external/mgba/include` **and** `external/mgba/build-3ds/include` (generated
  `mgba/flags.h` lives in the latter). **Link:** `-L external/mgba/build-3ds -lmgba`.
- **`mColor` is `uint16_t` RGB565** under these flags → pair with `GPU_RGB565` /
  `GX_TRANSFER_FMT_RGB565` (watch channel order at upload).
- **Isolate mGBA in a wrapper TU** (e.g. `source/gbacore.c`) that does **not** include
  `<3ds.h>` — both mGBA and libctru define `u8`/`u16`, so keep them in separate files; expose
  a plain-C API (`uint16_t*`, `uint32_t keys`) to `main.c`.

### FIXED_ROM_BUFFER must STAY — dual-core via per-core buffers (corrected v0.3)

**An earlier attempt removed `FIXED_ROM_BUFFER` to give each core its own ROM. That was
wrong and crashed on real 3DS.** Without it, GBA `reset` runs `_pristineCow` (`memory.c`),
which allocates a *second* "pristine" copy of the ROM and `memcpy`s into it — that alloc
returns NULL on the 3DS → data abort in `memcpy(NULL,…)`. Confirmed by the Luma crash dump:
`_pristineCow ← GBAReset ← reset ← gbacore_load_rom`. It only "worked" in Azahar because its
looser heap let the copy succeed for small ROMs (hence the earlier false "it's just memory"
conclusion — the real cause is `_pristineCow`, and it fails on hardware regardless).

**Correct approach (applied; verified stable with two 16 MB ROMs in Azahar):** keep
`FIXED_ROM_BUFFER` — the 3DS-native path, **no pristine copy**. libmgba's `ctru-heap.c`
(compiled *into* `libmgba.a`) already **defines** `romBuffer`/`romBufferSize` and allocates
one boot buffer (~32 MB). For two cores, `source/gbacore.c`:

1. **`extern`s** those globals — do **not** redefine them (`ctru-heap.c` provides them;
   redefining → "multiple definition" link error).
2. **Reuses the boot `romBuffer` for the first core**; `malloc`s a dedicated buffer for the
   **second**, pointing `romBuffer`/`romBufferSize` at it **before** that core's load.
3. Loads via `mCorePreloadVF` (copies the ROM into `romBuffer`) → `mCoreAutoloadSave` →
   `reset` (captures `gba->memory.rom = romBuffer`, per-core). Loads are sequential on the
   main thread → no race; normal `runFrame` uses the per-core `gba->memory.rom`, not the global.

So `-DFIXED_ROM_BUFFER` **belongs in MGBA_DEFS** (it's in the list above) and in the lib build
(the clone default — don't `sed` it out).

**Memory:** ~ROM-size per core (no pristine copy) → two 16 MB ROMs ≈ 32 MB boot (core A) +
16 MB (core B). Fits the `.cia` (124 MB) easily, and even the Azahar `.3dsx` heap.

**Other gotchas:** worker stacks — mGBA `runFrame` has deep call chains, give each worker
**≥512 KB** (32 KB overflows/corrupts). `svcGetProcessorID` is **unimplemented in Azahar**
(garbage core readout there; correct on hardware).

**Edge case / TODO:** a GBA *soft-reset* at runtime re-reads the global `romBuffer` (last set
= core B's), so core A would reset to B's ROM. Rare (user combo); fix later by re-pointing the
global per core around reset.

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
