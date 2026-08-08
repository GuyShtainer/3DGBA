# How the gba-toolkit (PokeDNA) session self-tests — read directly from its scratchpad, 2026-08-08

The model for phase 16. Key mechanics only; quoted paths are on THIS machine.

## The headline

No emulator GUI, no OS automation, no Lua: a Python harness driving the **mGBA core in-process**
via `libmgba-py` bindings, rebuilt per session in the scratchpad by a `setup.sh` (vendored zip at
`gba-toolkit/projects/rec2mp4/vendor/libmgba-py.zip`; technique origin: `projects/rec2mp4`).
**It was never committed** — parts already garbage-collected from older scratchpads. Phase 16
commits ours to the repo for exactly that reason.

## Mechanism per capability

| Capability | Mechanism |
|---|---|
| RUN | `GbaDriver.load(rom, save_path, out_dir)`; `core.run_frame()` loop; headless, frame-exact (`FRAME_RATE = 16777216/280896`, not 60) |
| SEE | `core.set_video_buffer()` → numpy → PIL PNG → the model Reads the PNG; 4-8x nearest-neighbour zoom crops (`zoom.py`) for 5x7 glyphs; contact sheets with markers (`sheet.py`) |
| PRESS | `core.set_keys(raw=<KEYINPUT bitmask>)` + hold 8 / release 8 frames ("games latch new-key on the 0→1 edge; menus debounce") |
| DECIDE | (1) the model looks at the PNG — "a defect you cannot see with your own eyes is REJECTED"; (2) pixel metrics (dark-fraction, mean luma, `compare.py` ref-vs-new heat-map diff with `--tolerance/--max-diff`, exit code = verdict); (3) `read_mem()` on emulated RAM/IO/OAM + `dump_save()` byte assertions |

## Rules that transfer to phase 16

- **The reference is the retail game captured the same way** (they read retail Emerald's BLDCNT/
  BLDALPHA live out of the running ROM rather than inferring).
- **RTC pinning is a reproducibility requirement** — wall-clock-dependent screens silently break
  screenshot regression (Azahar analog: `init_clock=1` + fixed `init_time`).
- **Fresh boot per run, state via patched save copies** — savestates exist but are unused; saves
  are ALWAYS copied, never edited in place.
- **Tests as data**: a JSON script of `[["tap","START"],["step",300],["screenshot","x.png"],
  ["assert_mem",addr,"deadbeef"]]` with `wait_stable(quiet_frames)` instead of sleeps.
- **Scale + adversarial verification**: a 2,600-screenshot sweep, every claimed defect
  independently re-reproduced by a second agent from the repro steps before being believed.
- **One captioned image per claim** with a marker pointing at the thing being claimed; galleries
  as HTML with inlined images because raw attachments don't render for the user.
- `mgba.log.silence()` before core creation; video buffer and save loaded BEFORE `reset()`;
  keep a Python ref to the save VFile or GC frees it under the core (libmgba-py footguns).

## What has no 3DS equivalent (why phase 16 differs)

`libmgba-py` itself. Azahar has no in-process bindings — phase 16 substitutes:
CLI launch + managed config profile (RUN), `--gdbport` GDB RSP + `.map` symbols + the app's own
sdmc diagnostics files (READ-STATE), synthesized CTM TAS movies + phase-13 D4 control files
(PRESS), window-targeted screencapture behind the Screen Recording permission (SEE).
