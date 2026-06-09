# Dual-GBA — Master Roadmap, v0.1 → v1.0 → v1.x

## Context

dual-gba is a New-3DS homebrew app that runs **two GBA games at once, one per screen,
joined by an emulated link cable**. The dual-core scaffold already exists (two worker
threads pinned to separate CPUs, per-frame `LightEvent` handshake, dual-screen citro2d
render, X/Y focus toggle, `emu_step()` stub). The technical approach is verified against
mGBA master source (`docs/kb/*`): **embed `libmgba` (two `mCore` instances) — do not fork
mGBA's single-core 3DS frontend.**

This is the project's master roadmap: a **shippable version ladder** folding in the full
set of GUI/visual decisions, sequenced around the two real unknowns — the **two-core
performance gate** and the **link-cable concurrency spike**. It supersedes the toolkit's
high-level M0–M4 note (`../../docs/ROADMAP.md`), which stays as the toolkit-level verdict.

## Status (2026-06-08)

- **🧰 v1.1 touch/HUD follow-ups (2026-06-09), pending hw test:** per-screen **HUD/fps** toggle (menu cycles Off/Top/Bottom/Both — the fps lives in the HUD); **touch now works during a link** (removed the !linkOn guard — game_read/write is a benign EWRAM race on the cache-coherent MPCore); **battle dialog/animation: tap (or hold) anywhere = A** to advance text. **Queued:** in-battle **party menu** touch (switch-Pokémon screen, single vs double layout) — needs party-menu detection within the battle state + gPartyMenu.slotId write; currently a tap there just presses A.
- **🧰 v1.1 polish batch (2026-06-09), pending hw test:** move-menu select fixed (all 4 moves; removed an over-rejecting RAM gate); **hard Mute** menu item (stops audio pump/mix/feed — real CPU save, not volume 0); **2D button pause-menu** (D-pad *or tap* to select) + a **Pause** entry; and a **render/emulation pipeline decouple** for non-link play — main now renders frame N-1 while the workers compute N, so the picture is no longer chained to the slower core (the reason link felt smoother). `worker_main` + the link path are untouched; drains keep the menu/link transitions clean.
- **🎮 v1.1 TOUCH — direct-touch on the REAL game UI, built (2026-06-09), pending hardware test.**
  Touch is a 3-way mode (Off / Gamepad / **Smart**). In Smart the touchscreen is a *pointer on the
  game's own UI* (no overlay buttons): the touch is mapped back through the render scaling to a GBA
  pixel and routed by live RAM (`gBattle_BG0_Y` = 160 action / 320 move; `gBattleTypeFlags`):
  * **Battle action menu** — tap the real FIGHT/BAG/POKéMON/RUN → a closed-loop controller drives
    `gActionSelectionCursor` there and presses A.
  * **Battle move menu** — tap the real move → drives `gMoveSelectionCursor` + A; empty slots (read
    from `gBattleMons[].moves`) are ignored.
  * **Overworld tap-to-walk** — tap a tile → walk there (axis path + stall-detect off the player
    coords; player sits at screen tile 7,5); tap yourself = A (interact / advance text).
  The earlier overlay-button version was rejected ("a cool workaround, not what I had in mind"). All
  addresses verified vs pret sym maps — see [docs/kb/gen3-ram-touch.md](docs/kb/gen3-ram-touch.md). A
  temp `ctx/xy/cursor` readout stays on to confirm reads on device. Party/bag/field-menu direct-touch
  + true collision pathfinding are deferred (addresses already sourced). *Not done until verified on hw.*
- **⚙️ Multi-core audio offload + full-clock use, built (2026-06-09), pending hardware test.** Audio
  moved off the render frame onto a **dedicated audio thread pinned to the core-1 syscore slice** (the
  previously-unused lane): each worker drains its OWN core's audio into a lock-free SPSC ring, and the
  audio thread mixes both rings + feeds ndsp. This (a) frees core 0 so heavy scenes can hold 60fps with
  audio on, and (b) **un-mutes audio during a link** (rings are worker-private → no buffer race).
  `osSetSpeedupEnable(true)` is now unconditional, and a **self-calibrating 804MHz/L2 detector** warns
  on the splash if a New 3DS isn't accelerated (run the `.cia`, not the HBL `.3dsx`). **Core budget:
  all 3 usable lanes now used** — core 0 (main + worker A), core 2 (worker B), core 1 (audio); core 3
  is OS-reserved (never available to titles). Full plan + rollout in
  [docs/kb/n3ds-audio-offload-plan.md](docs/kb/n3ds-audio-offload-plan.md). *Hardware-final: watch for
  core-1 underrun crackle under the 80% syscore slice (mitigation: deepen the wave ring 4→6).*
- **🏆 v0.8 LINK CABLE = ✅ DONE on real New 3DS (2026-06-08).** Two Gen-3 Pokémon games completed a
  **full trade across the two screens, saved, and persisted across a restart** — the project's
  flagship goal, achieved. The fix that made it work: run the *linked* cores via `core->runLoop`
  (mGBA's fine-grained CPU slice that returns on `earlyExit`) instead of `runFrame`, so each worker
  parks at the **exact** lockstep transfer point — mGBA's cooperative correctness, but on our
  **core-2-pinned** workers (no `mCoreThread` rewrite, parallel perf kept). See
  [docs/kb/link-cable-lockstep.md](docs/kb/link-cable-lockstep.md). Free-running B2 (frame-granular
  park) was the dead end; `runLoop` is the answer.
- **✅ POLISH PASS #1 done + merged to `main` (2026-06-09).** Unfocused-frameskip (perf lever),
  menu declutter + focused-game save/load state, settings persistence (`DGB3`), HUD link-status +
  **worst-frame-ms** readout, controls cheat-sheet, `theme.h` palette unification, and a module split
  (`audio.{c,h}` / `touch.{c,h}` / `gamestate.{c,h}` out of `main.c`). Confirmed working on hardware.
- **🟢 POLISH PASS #2 (the three deferred perf/audio items) — ADDRESSED in code (2026-06-09), pending
  hardware sign-off** via the multi-core audio offload above:
  1. **fps locking higher** — audio work left core 0, returning the heavy-scene budget it was eating;
     expect 60fps with audio on (confirm on hw; unfocused-frameskip remains as a fallback).
  2. **audio "smoosh"/crackle** — decoupled from the frame via SPSC rings consumed by the core-1 audio
     thread; if crackle persists under the syscore slice, deepen the wave ring (4→6) / tune the limit.
  3. **audio during link** — now plays (worker-private rings); the old main-thread buffer race is gone.
- **v0.1–v0.3 + v0.5: done.** Two real GBA games emulate at once, one per screen (mGBA via
  `libmgba`), X/Y focus, per-core ROMs, and a boot-time ROM picker.
- **v0.4 PERF GATE = ✅ GREEN** — two cores run at **full speed on a real New 3DS** (`.cia`).
  Main line confirmed; the **link cable (v0.8) is viable** (no gpSP fork needed). Azahar's
  slowness was purely nested-emulation, as predicted.
- **Hard-won lesson:** keep `FIXED_ROM_BUFFER` on 3DS — removing it NULL-crashes via
  `_pristineCow`; dual-core uses a per-core `romBuffer`. See `docs/kb/mgba-integration.md`.
- **v0.6 presentation = done.** v0.6.1 per-screen scaling (1:1 / aspect-fit / stretch, ZR) +
  **sharp-bilinear** filter (ZL) — the blur fix: NEAREST integer prescale → LINEAR fit, the same
  two-pass mGBA's own 3DS port uses (a PICA200 has no programmable fragment shader, so the
  single-pass sharp-bilinear shader is impossible; two-pass via an offscreen target is the way).
  v0.6.2 runtime A↔B screen swap (pause menu; scale/filter bound to the *screen*, focus/input to
  the *game*). v0.6.3 active-game dim cue (unfocused game dimmed 50%), custom GBA-nostalgic icon +
  CIA banner, and an animated dual-screen boot splash.
- **Backlog / nice-to-have (later):** an **authentic GBA-BIOS-style boot animation** — the classic
  "Game Boy Advance" logo slide-down + chime — as an optional extra startup screen alongside the
  current custom splash. Purely cosmetic; deferred.
- **v0.7 in progress.** Built (compile-clean, runtime-unverified): **solo audio** via ndsp (focused
  core → one STEREO_PCM16 channel, unfocused drained, channel cleared on X/Y; rate pitch-matched to
  the 3DS refresh) — *plus* the exheader fix (`app.rsf` was missing `dsp::DSP`, so `ndspInit` failed →
  silence); **toggleable HUD** (per-screen game label + FPS/clock/battery); **auto-named ROM picker**
  (GBA header → Gen-3 names/title); **recent pairings** (one-button resume of the last A+B at boot);
  **settings persistence** (`sdmc:/dual-gba/settings.bin`). A temporary on-screen audio readout
  (snd/rate/av/add/s0) is in to diagnose the reported silence; remove once audio is confirmed.
- **Open: audio still silent in Azahar** as of last test — diag build pending the user's read of the
  on-screen numbers to localize (ndspInit vs core-produces-samples vs ndsp-output).
- **Next:** confirm audio (diag), then audio mixer modes (mixed/split + per-game volume), the
  cover-art *grid* picker, and the v0.6/v0.7 hardware sign-off; then v0.8 link cable.

## Locked GUI / visual decisions (the design contract)

| Area | Decision |
|---|---|
| Layout | Game A top / Game B bottom, **runtime-swappable** (button) |
| Scaling | **All modes** (pixel-perfect 1:1 / aspect-fit / stretch), **live-switch per screen via ZL/ZR** |
| Pixel look | **Sharp↔smooth (nearest/linear) live toggle**, default sharp |
| Active-game cue | **Subtle**: small enhance on the played game + slight desaturate/dim on the other |
| Theme | **GBA-nostalgic** (indigo/purple, rounded, pixel font); **animated dual-screen splash** + custom icon/banner |
| HUD (all menu-toggleable) | performance (FPS/frameskip), audio mode+volume, link status, labels+system info (auto game name/screen, battery, clock, mode toasts) |
| ROM picker | **Cover-art grid + recent-pairings + browse**, auto-detected game names (parse ROM header); plain/auto-named list as fallback. Pick A then B |
| Menu open | **Corner MENU button + hardware combo (START+SELECT)**; a button **cycles touch-mode** (gamepad / tap-to-walk / off) |
| Touch (full ladder) | (A) translucent virtual gamepad over the bottom game → (B) game-aware tap regions (battle move/bag/run) → (C) **tap-to-walk** (read RAM + pathfind). Per-game; **auto-detect ROM** → profile. Gen-3 Pokémon: Emerald, FR/LG, R/S |
| Save states | per-game **thumbnail slots + quick save/load hotkey** (separate from in-game `.sav`) |
| Audio | **solo (default) / mixed / split + per-game volume** |
| Stereoscopic 3D | **all modes**: off / subtle pop-out / slider-peek / **per-eye dual-game** (A→left eye, B→right eye) as flagship **experimental**, late |
| Input | X/Y toggles which game gets controls; focus also drives solo audio |

## Two gates that shape everything

- **⛔ Performance gate (v0.4):** do two full-speed mGBA cores hold 60 fps on real New 3DS?
  mGBA is a pure interpreter; one core already dips to ~48–50 fps in heavy scenes.
  - **GREEN** (fit, with unfocused-frameskip) → main line; link cable survives.
  - **RED** (can't hold even with frameskip/804 MHz/L2) → **gpSP fork**: re-do license (GPL),
    rewire core/audio path, and **the link cable (v0.8) is forfeited** (gpSP has no in-process
    lockstep). Product pivots to "two independent games + presentation/touch" — still shippable.
- **🔬 Link concurrency spike (v0.8):** **B2 spike built + tested (2026-06-08)** — handshake works
  (two Gen-3 games detect each other + start exchanging across the screens), full speed + responsive,
  but a real trade **errors mid-exchange**. A 3-agent source audit proved the cause is **structural**:
  the free-running model races mGBA's *cooperative* sleep/wake (frame-granular park vs many transfers
  per frame; `Wake` dropped by the `player->asleep` guard) → stale/`0xFFFF` partner words → link error.
  **Azahar serializes the two cores so it can't fairly test the link anyway.** Verdict: free-running B2
  can't reliably complete a transfer. Path to a working link = **B1** (`mCoreThread` +
  `mLockstepThreadUser`, mGBA's proven cooperative model — a real worker/render/audio rewrite) and/or
  **real New-3DS** validation (cores genuinely parallel). Full analysis + options in
  [docs/kb/link-cable-lockstep.md](docs/kb/link-cable-lockstep.md). The committed spike stays as the
  responsive baseline (main is decoupled → never hangs; toggle Link off any time).

## Version ladder (GREEN path; RED variances noted)

Agents: toolkit `n3ds-systems`, `pica-gpu`, `devkitarm-3ds-build`, `ctr-audio`,
`n3ds-hardware-testing`; project `mgba-core`, `gba-link-lockstep`, `gba-audio-mixer`.

- **v0.1 — Build/boot loop locked (M0).** Install the skeleton `.cia` on real New 3DS; confirm
  worker B on core 2 (`svcGetProcessorID()==2`). Owners: `devkitarm-3ds-build`,
  `n3ds-hardware-testing`. Gate: `.cia` boots + core 2 confirmed.
- **v0.2 — One mGBA core, one screen (M1).** Build `libmgba.a` (flags: `COLOR_16_BIT=ON`,
  `ENABLE_VFS=ON`, `M_CORE_GBA=ON`, `DISABLE_THREADING=OFF`, `BUILD_QT/SDL=OFF`; **pin the master
  commit**). Fill `emu_step()` with the mCore lifecycle; map keys; upload 240×160 RGB565 → 256×256
  tiled `C3D_Tex` via `GX_DisplayTransfer`. Owners: `mgba-core`, `pica-gpu`, `devkitarm-3ds-build`.
  Gate: one ROM full-speed on top screen, measured with `svcGetSystemTick`. Risk: standalone-lib
  build + ABI-flag drift.
- **v0.3 — Two cores, two screens.** Second `mCore` in worker B → bottom screen; X/Y routes input;
  memory check vs 124 MB budget. Owners: `mgba-core`, `pica-gpu`, `n3ds-systems`. Gate: both visible,
  no OOM. (Do not tune perf yet.)
- **v0.4 — ⛔ PERFORMANCE GATE.** Instrument per-worker + present cost (worst-case); implement
  unfocused-frameskip + deadline/`skip` policy (still step the unfocused core). Owners:
  `n3ds-hardware-testing` (authority), `n3ds-systems`, `mgba-core`. **GREEN/RED fork as above.**
- **v0.5 — citro2d UI layer + plain ROM picker + logging.** Reimplement the tonc UX patterns on
  citro2d (see module table). Plain/auto-named list picker (parse ROM header title/code). Owners:
  `pica-gpu`, `mgba-core` (header parse), `n3ds-systems`. Gate: host-test widget logic; picker in Azahar.
- **v0.6 — Presentation pass.** Layout swap; full scaling matrix + ZL/ZR live switch; sharp/smooth
  filter; subtle active/desaturate cue; GBA-nostalgic theme; animated splash + icon/banner. Owners:
  `pica-gpu`, `devkitarm-3ds-build`. Gate: modes switch live with no frame hitch (measure the tint pass).
- **v0.7 — HUD + cover-art picker + solo audio.** All toggleable HUD elements; cover grid + recent
  pairings; **solo audio** (feed focused core, drain the unfocused buffer every frame,
  `ndspChnWaveBufClear` on switch). Owners: `gba-audio-mixer`, `ctr-audio`, `pica-gpu`, `mgba-core`.
  Gate: glitch-free focused audio, unfocused never stalls (needs `dspfirm.cdc` on hw).
- **v0.8 — 🔬 LINK CABLE (GREEN-only).** Run the B1/B2 concurrency spike first; wire
  `GBASIOLockstepCoordinator`/`Driver` + `setPeripheral(mPERIPH_GBA_LINK_PORT)`; resolve cadence vs
  frameskip; link-status HUD. Owners: `gba-link-lockstep`, `n3ds-systems`, `mgba-core`,
  `n3ds-hardware-testing`. Gate: a real Gen-3 trade/battle completes across screens on hw. **HIGH risk,
  no prior art** — if the spike fails a timebox, defer link to v1.x and ship v1.0 without it.
- **v0.9 — Audio mixer modes + save states + menu/touch entry.** mixed/split + per-game volume (one
  clamped mixer equation); thumbnail save-state slots + quick save/load; corner MENU button + START+SELECT;
  touch-mode cycle state. Owners: `gba-audio-mixer`, `ctr-audio`, `mgba-core`, `pica-gpu`, `n3ds-systems`.
  Gate: host-test mixer math; save states round-trip on hw.
- **v0.10 → v1.0 — Touch Tier A + persistence + sign-off.** Translucent virtual gamepad over the bottom
  game (input remap only, no game knowledge); settings persistence (SD config); first-run flow; full
  New-3DS hardware sign-off. **v1.0 = Touch Tier A only.** Owners: `n3ds-systems`, `pica-gpu`,
  `n3ds-hardware-testing`, `devkitarm-3ds-build`.

### Experimental v1.x (post-1.0, each independently shippable/abandonable)
- **v1.1 — Per-game profiles + ROM-detection + Touch Tier B.** ROM game-code (0xAC) → profile
  (Emerald BPEE, FR/LG BPRE/BPGE, R/S AXVE/AXPE), reusing pokeviewer's `Gen3Version` mapping. Profile =
  header-free table of tap-region→button-sequence + RAM addresses (grounded in vendored pret decomps).
  **New capability:** read the *running core's* RAM (mGBA memory accessors), not a `.sav`. Confirm expected
  menu via RAM before issuing a sequence. Risk: brittle across revisions.
- **v1.2 — Touch Tier C (tap-to-walk).** Read player coord + map/collision from RAM, pathfind, inject
  D-pad. **Highest behavioral risk — bot-like, per-game, brittle; off by default, supported-games-only,
  "experimental".** Conflicts with link timing.
- **v1.3 — Stereoscopic ladder → per-eye dual-game.** off / pop-out / slider-peek, then **per-eye**
  (A→`GFX_LEFT`, B→`GFX_RIGHT` of the top screen — adds the right-eye target the scaffold lacks). Risk:
  brightness halving, crosstalk between two different images, doubles top-screen GPU cost — labeled curiosity,
  must not regress the v1.0 budget.
- **v1.4 — 📡 WIRELESS multi-3DS link (4-player GBA over 3DS local wireless).** *(user-requested, build
  later)* Extend the in-process SIO lockstep (v0.8) **across consoles** so up to **4 emulated GBA
  "devices" link over 3DS local wireless** — bridging mGBA's GBA-SIO bytes over the 3DS `uds` service
  (NWMUDS ad-hoc). **Flexible topology:** 4 separate 3DS each running 1 game; or a console running 2
  games (both screens) contributing 2 of the 4 link seats; any mix that sums to ≤4 GBAs (e.g. 3 consoles
  with 1+1+2, or 2 consoles with 2+2). Players **negotiate P1–P4 seat assignment** at join (random/agreed)
  so any console can host any seat(s). Targets Gen-3 4-player (Union Room / trades / multi battles) and
  other GBA 4-player titles. **Architecture:** a `uds` transport layer feeding the existing
  `GBASIOLockstepCoordinator` — local in-process cores + remote cores both appear as lockstep drivers;
  the lockstep cadence already proven on one console must now tolerate wireless latency/jitter (buffer +
  rollback or strict lockstep-with-wait). **High risk:** wireless latency vs the GBA link's tight timing,
  `uds` throughput/MTU, and clean seat negotiation. Independent of v1.1–v1.3; the single-console link
  (v0.8) is the foundation it builds on. Detail will land in `docs/kb/wireless-link-uds.md` when started.

## citro2d UI-layer module structure (v0.5) — `source/ui/`

Reimplement the tonc patterns from `gba-pokeviewer` on citro2d; keep **logic header-free and
host-testable** (`../../CLAUDE.md` rule), isolate citro2d in thin render files; rendering only on
the coordinator/main thread (never a worker).

| Module | Pattern source | Host-testable logic |
|---|---|---|
| `theme.{c,h}` | `ui.h` palette | color/mode tables |
| `widget_text.c` | `ui.c` `ui_truncate`/`ui_text_sel` | UTF-8 truncate |
| `widget_list.c` | `pkview_main.c` browser | scroll/clamp math ✅ |
| `widget_grid.c` | new (cover art) | grid index/scroll ✅ |
| `widget_osk.c` | `osk.c` | buffer/cursor edits ✅ |
| `widget_modal.c` | `pkview_main.c` menu/confirm/halt | selection logic |
| `widget_toast_hud.c` | new | toast queue/timer ✅ |
| `ui_log.{c,h}` | `gba-toolkit/source/log.c` | ring buffer ✅ |

## Critical files

- `source/main.c` — dual-core scaffold; `emu_step()` (≈L39) is the mGBA wire-in; render loop
  ≈L122–134; core-2 spawn ≈L96–97; only `GFX_TOP,GFX_LEFT` today (v1.3 adds right-eye).
- `docs/kb/{mgba-integration,link-cable-lockstep,audio-mixer}.md` — verified spine.
- `Makefile`, `app.rsf` — add `-lmgba`/includes; RSF already has core-2/804/L2.
- `gba-toolkit/projects/gba-pokeviewer/source/{ui.c,osk.c,pkview_main.c}` + `gen3_save.h` — UX patterns
  to reimplement (v0.5) and the Gen3Version mapping reused for v1.1 ROM detection.
- **Pokémon decomps (for v1.1/v1.2 RAM maps):** curated *subsets* are vendored at
  `gba-toolkit/projects/gba-pokeviewer/reference/{pokeemerald,pokeruby,pokefirered}` (save-format
  refs only — ~6–8 files each). For the deeper per-game RAM maps (player coord, map/collision, battle
  menu state), clone the **full** pret decomps from GitHub when that work starts: `pret/pokeemerald`,
  `pret/pokefirered`, `pret/pokeruby`. (Emerald + Ruby live in the gba project; FireRed: vendored subset
  present, pull the full source online for nav work.)
- The 8 specialist agents under the two `.claude/agents/` dirs own the work per version.

## Verification (per phase, hardware-final)

- **Each v0.x:** build `.cia` (`make cia`), iterate in **Azahar**, then **sign off on real New 3DS**
  via `n3ds-hardware-testing` — Azahar does not model core-2 contention, the 804 MHz budget, ndsp
  timing, or the link. Always **measure** the worst-case per-frame cost with `svcGetSystemTick`/
  `osTickCounter` and log it; record any unproven claim as an open item.
- **Pure-C logic** (UI widget math, audio mixer, touch profiles/pathfinder) gets a **host test**
  compiled on PC before any hardware run.
- **v0.4 gate:** two cores' worst-case frame fits 60 fps with unfocused-frameskip on hardware →
  GREEN; else take the documented gpSP-RED branch.
- **v0.8 link:** a real Gen-3 **trade or 2-player battle completes across both screens** with no
  desync/hang on hardware.
- **v1.0:** full sign-off — core 2 confirmed, frame budget in-budget and logged, audio glitch-free,
  link completes a trade (if GREEN), no thread/memory leaks, settings persist.
