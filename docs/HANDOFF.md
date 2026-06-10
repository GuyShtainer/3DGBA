# dual-gba — Handoff

> Living resume doc maintained by the `handoff` skill. The **Current status** and **Next steps**
> sections are always kept current — start there to resume. The **Session log** grows downward,
> newest first, and is never pruned.
> Last updated: 2026-06-10

## Current status

- **Repo / branch:** `/Users/guyshtainer/VSCodeProjects/3ds-toolkit/projects/dual-gba` / `main` — pushed: no (local only; this sub-tool is its own git repo; the toolkit git-ignores `projects/`).
- **Goal:** A New-3DS homebrew that runs **two Gen-3 Pokémon GBA games at once** (one per screen) on embedded mGBA (`libmgba`, two `mCore`s), joined by an **emulated GBA link cable** (trades/battles — already works on real New 3DS). On top of that: a **touchscreen "smart pointer"** that drives the real in-game UI, **stereoscopic 3D depth**, and (researched, not built) **wireless multi-3DS link**, **PokéMMO-style shared overworld**, and an **Octopath HD-2D** look.
- **State right now:** All committed at `1545cd2`, working tree clean. The current `.cia` (build it with the commands below) contains: the full touch suite, **stereoscopic 3D depth M2–M4** (characters + scenery pop OUT, slider-gated), and **5 touch fixes**. Four big features are **researched with saved, source-verified milestone plans** but not yet built (HD-2D, co-op overworld, wireless link). Awaiting the user's **hardware test feedback** on the current build.
- **Done (this arc, newest first):**
  - `1545cd2` docs: **co-op shared-overworld** feasibility + plan (`docs/kb/coop-shared-overworld.md`).
  - `949a6b7` docs: **Octopath HD-2D** feasibility + plan (`docs/kb/hd2d-octopath-3d.md`).
  - `c373d80` touch: **BFS routes around NPCs** (gObjectEvents) + **double-tap=START** + **smart title taps** (px<0 → tap=A/double=START).
  - `fc0777b` touch fix: **post-battle A-spam** — battle now gated on `callback2==BattleMainCB2` (gBattleTypeFlags lingers post-battle).
  - `76b781c` 3D **M4 scenery depth** (Emerald metatile layer-type, 3×3 smoothed for arches).
  - `06d0ce9` 3D **pop-OUT sign fix** (was popping in/behind floor).
  - `25e599a`/`acc4925` 3D **M3 NPC pop** (OAM) / **M2 player pop**.
  - `a00737e` 3D depth feasibility study (`docs/kb/emerald-3d-depth.md`).
  - `66d910b` touch the **launcher** (tap a game, drag to scroll).
  - `9b4c016` **wireless link** architecture + plan (`docs/kb/wireless-link-architecture.md`).
  - `7fb6f53` **hybrid walk** (quick-tap=BFS route, hold=directional steer).
  - `10d768f`/`6381e7c`/`a661b80`/`0be70b7`/`21d26cf` touch detection made **task-based & fail-safe** (fixed recurring Emerald walk breaks + party-in-battle); directional walk; field-menu/bag/party/popup touch.
  - `d8bf725` initial slider-gated per-eye 3D (the dual-game variant — now superseded by single-game depth).
- **In progress:** **HD-2D M1 (tilt-shift depth-of-field)** was the agreed next build (user picked it) but **NOT started** — the implementation hand-off to the `pica-gpu` agent was halted by the user before any change. Nothing uncommitted.
- **Blocked / needs the user:** (1) **Hardware test feedback** on the current build — 3D depth quality (ghosting at foreground edges, scenery tuning), and the 5 touch fixes (post-battle walk, NPC routing, double-tap START, title taps, FireRed menu). (2) Co-op M0 needs **Ruby/Sapphire (`AXVE`/`AXPE`) byte-matched symbol maps** to add profiles. Azahar can't validate 3D/link/timing — those are **hardware-final**.

## Next steps (resume here)

1. **HD-2D M1 — tilt-shift depth-of-field** (highest-impact visual; user-chosen). Follow `docs/kb/hd2d-octopath-3d.md` §3 recipe + §4 "M1": sharp focal band, blurred top/bottom bands, on the **focused top screen only**, 60fps with both games. PICA200 has **no fragment shader** — blur via an offscreen target + LINEAR down/up-sample + composite (scissor-bands or a per-vertex-alpha gradient; mind the rotated 240×400 top framebuffer for scissor). Add a "DoF" menu toggle (or a session-only `bool`). This is GPU-render work — the `pica-gpu` agent is the right specialist.
2. **Fold in hardware feedback** once it arrives. Likely follow-ups: 3D **M2 vertex-grid depth warp** (replaces the per-tile quad shift → kills the scenery edge-ghosting); tune `POP3D_PX`/`ENV3D_*` in `main.c`.
3. **Then pick among the other ready tracks** (each has a saved plan): co-op overworld **M0/M1** (`coop-shared-overworld.md`), **HD-2D M3 bloom**, **wireless link M1** lobby+seats (`wireless-link-architecture.md`).
4. **Still-pending touch RE** (`docs/kb/touch-issues-todo.md`): bag **pocket-tap** (needs per-game pocket-indicator rects), Pokémon **summary page** switch, naming **keyboard**.

## How to build / test / run

```
cd /Users/guyshtainer/VSCodeProjects/3ds-toolkit/projects/dual-gba
export DEVKITPRO=/opt/devkitpro DEVKITARM=$DEVKITPRO/devkitARM
export PATH="$HOME/3ds-tools/bin:$PATH"      # makerom/bannertool also auto-discovered from toolkit tools/bin
make        # -> dual-gba.3dsx
make cia    # -> dual-gba.cia  (installable; app.rsf grants CanAccessCore2 / 804MHz / L2 / nwm::UDS)
make clean
```
Iterate in **Azahar** (Citra successor). **Sign off only on real New 3DS** — 3D fusion, core-2 contention, the 804MHz/L2 budget, ndsp latency and any uds latency are NOT modeled by the emulator (CLAUDE.md rule #6). On-device touch debug: SMART mode draws a bottom-left line `ctx p=x,y key=U/D/L/R/A` — ask the user to read it when a touch bug repro's.

## Key decisions (and why)

- **Menu/context detection is TASK-BASED + fail-safe.** `game_read` (gamestate.c) decides the on-screen context by scanning `gTasks` for each menu's active input-handler task (`task_active`/`find_bag_list_task`), NOT by windowId/callback heuristics. **Why:** heuristics (stale `sMenu.windowId`, stale `gMenuCallback`) repeatedly false-positived and **blocked walking**; a wrong/absent task address now just means "not detected" → the overworld still walks. This is the structural cure for the recurring walk bug.
- **"In battle" = `callback2==BattleMainCB2`, not `gBattleTypeFlags`.** `gBattleTypeFlags` is zeroed at battle *setup*, not end, so it lingered into the field → whole post-battle screen read as a battle dialog (tap=A everywhere).
- **Walk is HYBRID:** quick-tap = BFS route to that tile (doors/ladders/NPCs as terminals; NPCs block transit so it routes *around* them); hold/slide = directional steer (robust at any speed incl. bike). **Why:** the old BFS continuous-retarget desynced on bikes.
- **3D depth = sprite-pop 2.5D on the single composited frame; NO extra emulation pass.** True volumetric/occlusion-correct 3D is a **hardware budget wall** (each separated layer needs a full extra mGBA pass; both cores already saturated). So we overdraw shifted sub-rects (characters via OAM, scenery via Emerald metatile layer-type) per eye. Honest ceiling: convincing 2.5D, faint edge ghosting, Emerald-only scenery.
- **HD-2D:** DoF + LDR bloom are the cheap high-impact wins (fixed-function screen-space). **AI *depth* maps = not worth it** (garbage on 16px pixel art, impossible at runtime, and our analytic `tdepth` is a better *dynamic* signal). **AI *normal* maps = worth it only for sprite/tile lighting** (PICA200 has HW normal-mapping). PICA200 = no fragment shader (only vertex/geometry shader + 6 TEV combiner stages + fixed fragment-lighting + fog LUT).
- **Co-op overworld:** SEE the peer via a **citro2d overlay avatar** (not RAM object-injection — that needs sprite+palette+callback and we only have `write8/16`, no `write32`/ROM-call). INTERACT via the games' **own Union Room/link** (a trade/battle is a link state-machine; it **cannot** be force-started by RAM pokes — that crashes).
- **Reliability-first for touch:** one feature at a time, hardware-verified, with an on-device debug readout. The proven-robust pattern is **write the game's cursor variable + pulse A** (battle action/move/party/target/field-menu all use it).

## Where things live

- `source/gamestate.c` : `PROFILES[]` (per-game RAM map — `BPEE` Emerald / `BPRE` FireRed / `BPGE` LeafGreen; **no `AXVE`/`AXPE` Ruby/Sapphire yet**), `game_read` (the context state machine), `task_active`, `find_bag_list_task`, `metatile_layer` is in main.c. `gamestate.h` : `GameProfile` (every RAM address, verified vs pret sym maps), `GameCtx`, `GameState`.
- `source/touch.c` : `walk_update` (hybrid walk + double-tap-START + title), `plan_bfs`/`walkable`/`read_npcs`/`map_read` (BFS + NPC collision), the menu handlers `hit_fieldmenu`/`fmenu_select` (sMenu START/yes-no/multichoice/party-popup), `hit_party`, `hit_battler`/`battler_index_for_pos` (double-battle target), `bag_update` (phone-style tap/drag/swipe), `menu_select`/`select_pulse` (battle action/move), the `touch_update` dispatch `switch`.
- `source/main.c` : `run_session` (the per-frame loop ~820-908; worker/render pipeline), `render_game`, the 3D-depth code (`calc_xform`, `draw_pop`, `pop_eye`, `warp_scenery_eye`, `build_depth_grid`, `metatile_layer`, `DepthSnap depth3d`, the parked-window snapshot, the per-eye render, `POP3D_PX`/`ENV3D_NORMAL`/`ENV3D_SPLIT`), the 2×7 button menu (`MENU_ITEMS`/`MENU_*_IDX`), the SMART debug line, settings persistence, `osSetSpeedupEnable`/804MHz detector.
- `source/rompicker.c` : `rompicker_run` (touch launcher — tap a game, drag to scroll).
- `source/gbacore.{c,h}` : mCore wrapper, `gbacore_read/write8/16/32`, the in-process link (`gbalink_*`, `GBASIOLockstep`). `source/audio.{c,h}` : ndsp offload (core-1 thread + per-core SPSC rings).
- `docs/kb/` : **the saved design studies** — `wireless-link-architecture.md`, `emerald-3d-depth.md`, `hd2d-octopath-3d.md`, `coop-shared-overworld.md`; plus `gen3-ram-touch.md`, `gen3-menu-bag-touch-spec.md`, `gen3-touch-features-spec.md`, `n3ds-audio-offload-plan.md`, `touch-issues-todo.md`. `docs/ROADMAP.md`.
- **Toolkit (separate repo)** `/Users/guyshtainer/VSCodeProjects/3ds-toolkit` : `CLAUDE.md` (conventions + the specialist subagents `n3ds-systems`/`pica-gpu`/`devkitarm-3ds-build`/`ctr-audio`/`n3ds-hardware-testing`), `tools/bin/` (makerom+bannertool). Embedded mGBA at `projects/dual-gba/external/mgba`.
- **Decomps for RE** (read-only, on this machine): `/tmp/pret/pokeemerald`, `/tmp/pret/pokefirered`; symbol maps `/tmp/EMERALD.sym`, `/tmp/FIRERED.sym` (col1 = absolute GBA bus address).

## Gotchas / constraints

- **Edit `main.c`/`touch.c` via Python scripts** with exact `\t`/`\n` strings + `.count()` asserts — the files are tab-indented and the Edit tool fails on whitespace mismatches. (A linter sometimes reformats between reads.)
- **3 usable ARM11 lanes only:** core 0 = main thread + worker A; core 2 = worker B (needs `CanAccessCore2`); core 1 = audio thread (syscore slice via `APT_SetAppCpuTimeLimit`). **Core 3 is OS-reserved.** No budget for a second emulation pass.
- **mGBA is MPL-2.0** (file-level copyleft) — don't modify mGBA files; the wireless plan adds a **network `GBASIODriver` as a peer driver**, not a fork.
- **All RAM addresses verified vs the pret byte-matched sym maps.** **LeafGreen (`BPGE`) ROM addresses are FireRed-derived (unverified)** → those features may be inert on LG (fail-safe). 3D scenery (M4) is **Emerald-only** (`mapHeader` is 0 elsewhere → falls back to sprite-pop).
- `gbacore` exposes only `read8/16/32` + `write8/16` — **no `write32`, no ROM-call/trampoline.** This is why co-op uses an overlay (not object injection) and interactions ride the real link.
- Reads of a worker's core must happen at the **parked per-frame handshake** (where `game_read`/the depth+OAM snapshot already run), never during a link free-run.

## Related docs

- `docs/ROADMAP.md` — the milestone narrative.
- `docs/kb/*.md` — the four design studies (wireless / 3D-depth / HD-2D / co-op) are the source of truth for those features; each ends with a hardware-verifiable milestone plan. Don't duplicate them here.
- Auto-memory at `~/.claude/projects/-Users-guyshtainer-VSCodeProjects-3ds-toolkit/memory/` (e.g. `touch-reliability-over-piling.md`).

---

## Session log

### Session — 2026-06-10

- **Intent:** Continue the dual-GBA build: finish the **3D depth** effect to the "full feasible version", fix a batch of **touch bugs**, and research two new directions the user asked about — a **PokéMMO-style co-op shared overworld** and an **Octopath HD-2D** look ("its possible even if we have to use an AI tool to render a somewhat 3D directions for 2D objects"). User is testing on a real New 3DS throughout.
- **Did:**
  - **3D depth M2–M4** (`acc4925`,`25e599a`,`06d0ce9`,`76b781c`): player + on-screen OAM NPCs pop OUT (fixed an inverted-disparity "pop-in" bug), foreground scenery pops via Emerald metatile layer-type with a 3×3 smoothing pass (the user's "average between tiles for arches" idea). Single composited frame, no extra emulation pass. User confirmed: clean (no garbage), flat when slider down, both games full-speed; "not perfect yet" + wanted pop-OUT (fixed) and M4 (added).
  - **Touch fixes** (`fc0777b`,`c373d80`): post-battle tap-anywhere-A fixed (battle gated on `callback2==BattleMainCB2`); BFS now routes **around** NPCs (gObjectEvents collision, goal exempt so tapping an NPC still talks); **double-tap your character = START**; title/intro (no map, px<0) → tap=A / double=START. Earlier in the session also: task-based fail-safe detection (recurring Emerald walk fix), hybrid walk, launcher touch.
  - **Four design studies run + saved** (`9b4c016`,`a00737e`,`949a6b7`,`1545cd2`): wireless multi-3DS link (FEASIBLE via a network SIO driver), 3D-depth (sprite-pop feasible, volumetric impractical), HD-2D (DoF+bloom cheap; AI normals yes / AI depth no), co-op overworld (overlay presence FEASIBLE; interaction via Union Room/link; can't force-start a trade).
  - Copied the `handoff` skill from gba-toolkit into `3ds-toolkit/.claude/skills/` and wrote this handoff.
- **Left off:** About to build **HD-2D M1 (tilt-shift DoF)** — user picked it from the menu — but **halted before any code change** so the user can resume in a **new chat (a different model)**. Working tree clean at `1545cd2`.
- **Open threads:** Hardware feedback pending on the current build (3D ghosting/tuning + the 5 touch fixes + whether FireRed's menu works now). Co-op needs Ruby/Sapphire symbol maps (M0). Still-deferred touch RE: bag pocket-tap, summary pages, naming keyboard. The four study plans are queued and independent.
