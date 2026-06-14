# 3DGBA

**Play two Game Boy Advance games at once on a single New 3DS — one per screen — and
link them with an emulated GBA link cable.** Trade and battle between the two games on
one handheld. Each game runs in its own [mGBA](https://mgba.io/) core pinned to its own
ARM11 CPU core, so they advance in *genuine parallel* rather than time-slicing.

This was **hardware-impossible on the Nintendo DS** (a documented wall, not an effort
wall) — the New 3DS's quad-core ARM11 at 804 MHz is what finally makes hosting two
software GBA cores plus an emulated link cable feasible on one device.

**3DGBA is a general Game Boy Advance emulator — it runs any GBA game you supply, and the
emulated link cable works with any game's link features.** But the project's *heart* is
making the **Pokémon** experience better: the touch "smart pointer" and the stereoscopic-3D
depth are **Gen-3-Pokémon-specific** enhancements — they do nothing in, say, Sonic or
Kirby. The emulator and the link are general; the special sauce is for Pokémon. It ships no
games or other content.

> ⚠️ A hobby project, and a work in progress. The core (two games + the link cable)
> runs on real New 3DS hardware; the stereoscopic-3D and wireless features are
> experimental and being tuned on-device.

## What it does

- **Two GBA games simultaneously** — game A on the top screen, game B on the bottom,
  each a full mGBA core on its own CPU core, running in genuine parallel. Two interpreted
  cores plus audio is heavy, so it **doesn't always hold full speed** — framerate dips in
  busy scenes, and performance work is ongoing.
- **Emulated link cable between the two games** — do a Pokémon trade or link battle
  between the game on top and the game on the bottom, no second console required.
- **Touchscreen "smart pointer"** — instead of an on-screen gamepad, the touch screen
  drives the *real in-game UI*: tap a tile to pathfind-walk there, tap menu entries,
  bag/party/battle targets, double-tap for START. (Gen-3 Pokémon.)
- **Experimental stereoscopic 3D depth** — uses the 3DS 3D slider to pop the overworld
  into a 2.5D diorama: characters and scenery stand up off a ground plane, with
  elevation/tile-collision-aware depth. (Actively being tuned.)
- **HD-2D-style post effects** — optional tilt-shift depth-of-field, LDR bloom, and a
  time-of-day color grade, all toggleable in the pause menu.
- **Wireless multi-console lobby** *(in progress)* — host/scan/join over local wireless
  (UDS) with a live seat map; the emulation-over-the-air link is a later milestone.
- Save states, per-game `.sav` loading, scaling/filter options, audio mix modes, and a
  ROM picker.

## Status & roadmap

A hobby project under active development — see **[STATUS.md](STATUS.md)** for the honest,
up-to-date breakdown. Short version:

- ✅ **Working on real hardware:** two GBA games at once, general GBA emulation, the local
  emulated link cable (Pokémon trades), saves/states, and the wireless lobby + link probe.
- 🟡 **Experimental, being tuned on-device:** the touchscreen smart-pointer, stereoscopic
  3D depth, and the HD-2D post effects.
- 🔜 **Planned:** wireless trades/battles between two consoles (the net link, M2.5 → M3),
  and a pokeMMO-style co-op **shared overworld** where you see other players in the field.

## Requirements

- A **New 3DS / New 2DS XL** (the two cores need core 2 at 804 MHz + L2 cache). Old 3DS
  is best-effort only and will run slow.
- Installed as a **`.cia`** — the exheader flags that grant core 2 / 804 MHz / L2 /
  `nwm::UDS` can't be claimed reliably by a `.3dsx` from the Homebrew Launcher.
- Your own legally-obtained GBA ROMs, placed in `sdmc:/3DGBA/`.

## Building

Builds with **[devkitPro](https://devkitpro.org/wiki/Getting_Started)** (devkitARM +
libctru + citro2d/citro3d) and a vendored **mGBA** core.

```bash
# 1. devkitPro toolchain (one-time)
sudo dkp-pacman -S 3ds-dev
export DEVKITPRO=/opt/devkitpro
export DEVKITARM=$DEVKITPRO/devkitARM

# 2. mGBA core — built into external/mgba/ (see docs/kb/mgba-integration.md).
#    Not vendored in git (MPL-2.0, large); the Makefile links libmgba.a from there.

# 3. build
make            # -> 3DGBA.3dsx  (quick netload testing via 3dslink)
make cia        # -> 3DGBA.cia   (the real install target; grants core 2 / 804 MHz)
make clean
```

`makerom` + `bannertool` (for the `.cia`) aren't part of devkitPro; the Makefile
auto-discovers them and falls back to `PATH`. Iterate in
**[Azahar](https://github.com/azahar-emu/azahar)** (the maintained Citra successor), but
**sign off on real hardware** — emulators don't model core-2 contention, the 804 MHz
budget, or UDS latency.

## How it works

- **One render thread owns the GPU.** Two emulator worker threads (core 0 + core 2)
  produce 240×160 framebuffers on their own cores; *all* citro3d/GPU calls happen on the
  main thread between `C3D_FrameBegin/End`. A per-frame `LightEvent` handshake parks the
  workers at a safe point each frame for state reads.
- **The link cable** is mGBA's own `GBASIOLockstep` driving the two in-process cores,
  with fine-grained `runLoop` slicing so trades/battles stay in sync.
- **Pure-C cores stay header-free** so parse/algorithm logic can dual-compile on a PC
  test harness.
- The **3D depth** is sprite/scenery "pop" overdrawn per-eye on the single composited
  frame (no second emulation pass — the cores are saturated), aligned to the field
  camera's sub-tile scroll.

See [`docs/`](docs/) for the architecture deep-dives, the design studies (stereoscopic
3D, HD-2D, wireless link, co-op overworld), and the milestone roadmap.

## License

**3DGBA is free software under the [GNU GPLv3](LICENSE)** — always free, always open. Any
distributed version or fork must also remain open-source under the GPL.

## Credits & third-party

- **[mGBA](https://mgba.io/)** by endrift powers the emulation — licensed **MPL-2.0**. 3DGBA
  links mGBA built from upstream commit `92621ea` with one local change to
  `src/gba/CMakeLists.txt` (so the link-cable code links); per MPL-2.0 that file stays MPL-2.0
  and its source is available (the change is documented in `docs/kb/mgba-integration.md`).
  MPL-2.0 is GPL-compatible.
- Built with **devkitPro** / **libctru** / **citro2d / citro3d**.
- Gen-3 RAM addresses + struct offsets were found by referencing the
  **[pret](https://github.com/pret)** decompilations (addresses/facts only — no game code is
  included or distributed).

## Legal

3DGBA is a **general Game Boy Advance emulator**. It ships **no games, BIOS, or other Nintendo
content** — you supply your own legally-obtained ROMs. Not affiliated with, authorized by, or
endorsed by Nintendo. "Game Boy Advance" and "Pokémon" are trademarks of their respective
owners, used here only descriptively.
