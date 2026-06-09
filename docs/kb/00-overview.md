# dual-gba knowledge base — overview / map

Project-specific subsystem docs for the dual-screen, link-cabled dual-GBA emulator.
**General 3DS knowledge (hardware, GPU, ndsp output, build/toolchain, licensing) lives in
the toolkit kb** at `../../../../docs/kb/` — these docs cover only what's specific to
embedding mGBA for *this* tool.

| Doc | Covers | Status |
|---|---|---|
| `00-overview.md` | this map | ✅ |
| `mgba-integration.md` | embed `libmgba` (not fork the frontend); two `mCore` instances; build flags; perf | ✅ verified vs source |
| `link-cable-lockstep.md` | in-process lockstep SIO linking the two cores; the threading catch; cadence | ✅ verified vs source |
| `audio-mixer.md` | solo/mixed/split mixer policy; draining the unfocused core | ✅ verified vs source |
| `gen3-ram-touch.md` | Gen-3 live-RAM addresses (player x/y, battle flag, action cursor) for game-aware touch | ✅ verified vs pret sym maps |

All three were verified against mGBA **master** source on 2026-06-03. They cite exact
symbols and upstream files — **pin a known mGBA commit** before implementing, as line
numbers drift.

## How this maps to the agents

- Toolkit-scope agents own the hardware layer: `n3ds-systems` (threads/cores/budget),
  `pica-gpu` (framebuffer→texture), `ctr-audio` (ndsp output), `devkitarm-3ds-build`
  (libmgba link + `.cia`), `n3ds-hardware-testing` (the sign-off gate).
- Project-scope agents own these docs: `mgba-core`, `gba-link-lockstep`, `gba-audio-mixer`.

## Build/run

See [../../README.md](../../README.md) and the toolkit `../../../../CLAUDE.md`. The master
version ladder (v0.1 → v1.0 → v1.x) is in [../ROADMAP.md](../ROADMAP.md); the toolkit-level
verdict is in `../../../../docs/ROADMAP.md`.
