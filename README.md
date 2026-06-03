# Dual GBA

Play **two Game Boy Advance games at once on a single 3DS** — one game on the top
screen, one on the bottom. Each game runs in its own emulator core, pinned to its
own CPU core, so they advance in genuine parallel rather than time-slicing.

This repo starts from the `3ds-tool-template` skeleton, which already implements
the hard part of the architecture (dual-core threading, per-frame sync, dual-screen
rendering, input focus). What remains is dropping a real GBA core into `emu_step()`.
See **Roadmap** below.

## Status

🟡 **Skeleton.** Boots both screens, spawns two worker threads (one per core), and
toggles input focus with **X / Y** — but `emu_step()` currently just animates a
placeholder box. No GBA core is wired in yet.

## 1. Install the toolchain

Install devkitPro: https://devkitpro.org/wiki/Getting_Started

Then pull the 3DS dev group (gives you devkitARM, libctru, citro2d/citro3d, and the
`3dsxtool` / `3dslink` / `makerom` tools):

```
sudo dkp-pacman -S 3ds-dev      # macOS/Linux; on some setups it's just 'pacman'
```

Make sure these are exported (the installer normally adds them to your shell
profile — on macOS the default is `/opt/devkitpro`):

```
export DEVKITPRO=/opt/devkitpro
export DEVKITARM=$DEVKITPRO/devkitARM
```

> If VS Code's IntelliSense can't find headers, it's almost always because VS Code
> was launched from the GUI and didn't inherit these vars. Launch it from a terminal
> with `code .`, or hardcode the paths in `.vscode/c_cpp_properties.json`.

Install the **C/C++** extension (`ms-vscode.cpptools`) in VS Code.

## 2. Build

`Cmd/Ctrl + Shift + B`, or run `make`. Output is `dual-gba.3dsx` plus a `.smdh`.
`make clean` wipes the build.

## 3. Run

- **On hardware (Luma3DS):** open the Homebrew Launcher, press **Y** to enter
  netloader/receive mode, then run the *"run on 3DS (3dslink)"* task. The 3DS must be
  on the same network. (Or copy the `.3dsx` to `/3ds/` on the SD and launch from HBL.)
- **In an emulator:** open the `.3dsx` in Azahar (the maintained successor to Citra).

## 4. What the skeleton does

- Enables the New 3DS 804 MHz + L2 boost (`osSetSpeedupEnable`) and claims syscore
  time (`APT_SetAppCpuTimeLimit`).
- Creates two worker threads via `threadCreate(..., core_id, ...)`: worker A shares
  core 0 with the light coordinator; worker B takes core 2 on a New 3DS (falls back
  gracefully otherwise).
- Runs a per-frame `LightEvent` handshake: the coordinator signals both workers,
  waits for both, then does all GPU work on the main thread.
- Renders worker A to the top screen, worker B to the bottom, with a highlight bar on
  whichever has input focus. **X/Y** flips focus; **Start** quits.

## 5. Build a CIA (installable; guaranteed core 2 on New 3DS)

A `.3dsx` launched from the Homebrew Launcher can't reliably claim core 2. For
guaranteed full dual-core access on a New 3DS, build a `.cia` instead — the included
`app.rsf` already sets `CanAccessCore2: true`, `CpuSpeed: 804MHz`, and
`EnableL2Cache: true`.

Two external tools are needed (not part of devkitPro — install separately, put on PATH):

- **makerom** — https://github.com/3DSGuy/Project_CTR
- **bannertool** — https://github.com/Steveice10/bannertool (or a current fork)

```
make            # builds the .elf + .smdh first
make cia        # wraps them into dual-gba.cia
```

Install the resulting `.cia` with a title manager (e.g. FBI) on a CFW console.

If makerom/bannertool live off-PATH, override per build:
`make cia MAKEROM=/path/to/makerom BANNERTOOL=/path/to/bannertool`.

## Roadmap — wiring in a real GBA core

The skeleton's `emu_step()` is where a GBA core plugs in. The plan:

1. **Pick a core.** [mGBA](https://github.com/mgba-emu/mgba) (accurate, has a
   maintained 3DS port; build `libmgba` and call the `mCore` API:
   `core->setKeys`, `mCoreRunFrame`, `core->getVideoBuffer`) vs.
   **gpSP** (dynarec, much faster but lower compatibility). Start with mGBA for
   correctness; keep gpSP as the fallback if two cores can't hit full speed.
2. **One `mCore` per `EmuInstance`.** Load a ROM per instance; run one frame per
   worker tick. Two cores ≈ 2× the GBA CPU cost — realistic only on a **New 3DS /
   New 2DS XL** (804 MHz, core 2). Old 3DS almost certainly can't do two at full speed.
3. **Video.** GBA is 240×160; PICA200 wants tiled, power-of-two textures — back each
   frame with a 256×256 texture and run a linear→tiled `GX_DisplayTransfer` each frame,
   then draw it to that instance's screen (top is 400×240, bottom 320×240).
4. **Audio.** There is one stereo output for two games. Options: mix both, **pan game
   A left / game B right**, or follow input focus. Needs a design decision.
5. **Input.** One physical control set, two games — the core UX problem:
   - *Focus toggle* (what the skeleton does): only the focused game gets input.
   - *Link-cable play*: emulate a GBA link cable **between the two in-process cores**
     so you can run a 2-player link game solo, or trade between two carts of the same
     game. This is the most compelling use of "two games at once."
   - *Split controls*: map one set of buttons to each game.
6. **ROM/BIOS/saves.** User-supplied GBA ROMs (and optionally the GBA BIOS for
   accuracy); per-game `.sav` files. A small file picker on boot.

> **Feasibility note:** the hard *systems* work (dual-core, sync, dual-screen) is
> done. The risk is purely performance — fitting two software GBA cores in one 60 fps
> budget on a New 3DS. Frameskip and gpSP are the escape hatches if mGBA×2 is too heavy.

## Layout

```
.
├── Makefile                  devkitPro 3DS build (.3dsx + .cia targets)
├── app.rsf                   exheader spec; New 3DS / core-2 flags live here
├── icon.png                  48x48 home-menu icon
├── banner.png / banner.wav   home-menu banner (image + audio)
├── source/main.c             coordinator + two emulator workers
├── include/                  your headers
├── .vscode/                  IntelliSense + build/run tasks
└── README.md
```
