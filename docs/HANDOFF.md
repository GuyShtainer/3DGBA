# dual-gba — Handoff

> Living resume doc maintained by the `handoff` skill. The **Current status** and **Next steps**
> sections are always kept current — start there to resume. The **Session log** grows downward,
> newest first, and is never pruned.
> Last updated: 2026-06-10

## Current status

- **Repo / branch:** `/Users/guyshtainer/VSCodeProjects/3ds-toolkit/projects/dual-gba` / `main` — pushed: no (local only; this sub-tool is its own git repo; the toolkit git-ignores `projects/`).
- **Goal:** A New-3DS homebrew that runs **two Gen-3 Pokémon GBA games at once** (one per screen) on embedded mGBA (`libmgba`, two `mCore`s), joined by an **emulated GBA link cable** (trades/battles — already works on real New 3DS). On top of that: a **touchscreen "smart pointer"** that drives the real in-game UI, **stereoscopic 3D depth**, and (researched, not built) **wireless multi-3DS link**, **PokéMMO-style shared overworld**, and an **Octopath HD-2D** look.
- **State right now:** HD-2D **M1–M4 committed** (`657b6e1`→`f069854`). Two **UNCOMMITTED, building** workstreams on top (`.3dsx`+`.cia` clean; last commit `f069854`):
  1. **Depth round 5 — bigger gradient + elevation A+B+C** (`main.c`): ground ramp bumped `POP3D_RAMP_PX` 2.4→3.6; elevation is now a quantized depth **plane** baked from the engine's own `sElevationToPriority` (the `ELEV_PLANE[16]` table — so depth order matches what the game draws in front = the "C" guardrail), transition/stairs/ledge tiles flood-filled from neighbours so they **ramp**; on-screen NPCs/the player pop by their **own** object-event `previousElevation` (matched OAM→`gObjectEvents` by feet grid-tile) = "B"; a `POP_DISP_MAX` comfort clamp caps stacking. **An adversarial review of this diff is still running** (task `w1so7axgq`) — fold findings, then commit. Verified the screen→map mapping is correct (+7 border and −7 centring cancel, matches the touch BFS).
  2. **Wireless M1 foundation** — NEW `source/netlink.{c,h}`: pure-libctru **UDS transport** (`netlink_init`/`net_session_host`/`net_lobby_scan`/`net_session_join`/`net_lobby_status`/`net_session_close`), wlancommID-filtered scan, appData advertise (game-code/CRC/seats), per-node usernames. Wired `netlink_init/exit` into `main()` (UDS comes up on the `.cia`; graceful no-op on `.3dsx`/no grant). NO lobby UI yet — that's the rest of M1. Per `docs/kb/wireless-link-architecture.md`. Hardware feedback round 2 ("blur STILL too much, overrides so much text") drove a rework: blur is now ONE half-res bounce (gentle ~2×2 soften, was quarter-res mush), alpha ceiling 47% (was 67%), sharp band widened to rows 48–112, and text-awareness became a **game-agnostic BG0 text-layer tilemap scan** (`band_text_scan` in `main.c`: gen-3 draws ALL textboxes/banners/menus on BG0 — verified, the standard textbox window sits at tile rows 15–18 on bg 0; any non-filler entries under a band kill that band's blur per-band, RAM signals kept as precise backups). Awaiting **hardware test round 3**. Remaining researched-not-built: HD-2D M3 bloom, co-op overworld, wireless link.
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
- **In progress:** **HD-2D M1 (text-aware DoF) + M2 (grid warp)** — code complete, builds clean, **uncommitted and not yet hardware-verified**. M1 gate: text always readable (dialog + map-name banner kill the blur instantly), bands blur softly, `worstMs` ≤16.7 ms, toggle persists. M2 gate: foreground edges **stretch** instead of tearing/ghosting with the slider up, player still pops, `worstMs` unchanged vs the quad warp.
- **Blocked / needs the user:** (1) **Hardware test round 2** — the text-aware/softened DoF, the M2 grid warp (watch `worstMs`: the C2D↔C3D state churn is M2's known risk), plus the prior 5 touch fixes. (2) Co-op M0 needs **Ruby/Sapphire (`AXVE`/`AXPE`) byte-matched symbol maps** to add profiles. Azahar can't validate 3D/link/timing — those are **hardware-final**.

## Next steps (resume here)

1. **Commit M4** (after the user OKs; one `feat` commit, `main.c` only), then **hardware-test the whole HD-2D stack** (install the fresh `dual-gba.cia`): **M4 lighting** — walk the overworld at different in-game/real clock times: dawn warm + low-angle, noon bright/neutral, dusk orange, night dim-blue; hillsides should shade brighter on the lit side (directional); toggle "Light" in the menu (persists). Plus the round-4 depth checks (3D slider up): (a) hill/stairs pop ABOVE the grass; (b) floor never pops over the character; (c) bottom-of-screen pops a touch more than top; (d) dialog/menu/bag/party panels pop toward you (`UIPOP3D_PX` 5px) with crisp text; (e) no side-strip artifact / full-frame flat pop. `worstMs` HUD ≤16 ms with everything on — **if the budget breaks, drop bloom first, then M4 lighting** (both are cheap but expendable).
2. **Known limitations (mention if asked):** (a) battle text panels don't pop — gen-3 battle BG0 is 32×64, so `bg0_scan` bails at its size guard (which also protects the full-screen-pop case); (b) M4 is the **analytic** lighting, not the study's AI-baked-normal atlas (deferred — needs offline bake + per-game tile RE).
3. Tuning knobs: lighting — edit the `light_for_hour` keyframe table + `kSlope` (0.18) in `main.c`; depth — `POP3D_RAMP_PX` (2.4), `POP3D_CHAR_PX` (2.0), `ENV3D_ELEV` (0.8/level), `UIPOP3D_PX` (5.0); DoF/bloom — `DOF_ALPHA` (0x78), `DOF_SHARP_Y0/Y1` (48/112), `BLOOM_THRESH` (0xC8), `BLOOM_GAIN` (0x90).
2. **Commit** the round (suggested split: text-aware DoF fix; M2 grid warp; or one combined HD-2D commit) once the user okays it.
3. **Fold in feedback**, then pick the next ready track: **HD-2D M3 bloom** (`hd2d-octopath-3d.md`), co-op overworld **M0/M1** (`coop-shared-overworld.md`), **wireless link M1** (`wireless-link-architecture.md`). Tuning knobs if asked: `DOF_ALPHA`/`DOF_SHARP_Y0/Y1`/`DOF_FADE`, `POP3D_PX`/`ENV3D_*`, the dofLevel ramp rates in the frame loop.
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

### Session — 2026-06-11 (M4) — time-of-day lighting

- **Intent:** User: "proceed to M4 and build it." The study's literal M4 is AI-baked normal atlases (flagged "the one to cut"); built the honest, fully-buildable realization of its intent instead — analytic time-of-day directional lighting — and documented the AI-atlas as a deferred upgrade (can't run an AI bake or the per-game tile-matching RE in this env).
- **Did (uncommitted, `main.c` only):**
  - `light_for_hour(hf)` — lerps an 8-keyframe day cycle (deep night→pre-dawn→dawn→noon→golden→dusk→night→wrap) into a `LightEnv` {rgb, light dir L, ambient, diffuse}; normalizes L.
  - `light_vert(d,e,vr,vc)` — per grid-vertex tint = lightColor·(amb + dif·max(0,N·L)); N from the `tdepth` gradient at the vertex (raised terrain catches side light); clamps to a citro2d colour.
  - `light_pass(tgt,d,mode,e)` — 15×10 gouraud **MULTIPLY** mesh (`C2D_DrawRectangle`) over the frame box, bracketed `C2D_Flush`→`C3D_AlphaBlend(GPU_DST_COLOR,GPU_ZERO)`→draw→`C2D_Flush`→restore standard blend; pure citro2d so no `C2D_Prepare` needed.
  - Integration: `lenv` computed once/frame from `localtime` (same clock as the HUD), `litPass = lightOn && overworld && topG->core` (slider-independent), drawn on BOTH top eyes after the scene pops and before dof_bands/bloom/ui. "Light" menu toggle (`MENU_LIGHT_IDX` 14; grid reflowed 2×8→2×9 at pitch 25/h23; Change games→15, Quit→16); `Settings.light` appended, size-tolerant load now accepts all 4 historical sizes.
  - **Review:** Workflow (2 lenses render/logic × 2 skeptics, ran to completion) → **0 confirmed defects** (missing-flush claim false — flush present; localtime cosmetic + null-checked + matches the working HUD clock; per-eye order consistent; 300 quads/frame well under the 4096 C2D budget). No fixes needed.
  - `.3dsx` + `.cia` build clean.
- **Verify on hardware:** day/night grade reads right across the clock; hillsides shade directionally; toggle persists; `worstMs` ≤16 ms with the whole stack (drop bloom then M4 if budget breaks).
- **Left off:** awaiting hardware verdict; commit pending user OK (done next).

### Session — 2026-06-11 (round 4) — depth model v2 + UI-panel pop, reviewed

- **Hardware feedback (round 4):** "3D on objects isn't a fine-enough gradient — grass and the hill up the stairs seem the same height"; "the floor pops up even over the character"; wants "a top-to-bottom overall gradient on the floor, bottom pops more (closer)"; "in the menu / party / bag the 3D is completely off — improve that"; "every text box (dialog or menu) should very pop out, but text must still look good."
- **Did (uncommitted, `main.c` only):**
  - **Depth model v2:** continuous ground ramp `RAMP_AT(gy)=POP3D_RAMP_PX*gy/160` added to grid verts + sprite pops + DoF bands (bottom closer); `tdepth` is now float; `build_depth_grid` adds the **map-grid elevation nibble** `(entry>>12)` × `ENV3D_ELEV` (0/15→base 3, clamped +4) so hills pop above grass; `pop_eye` pops each character `RAMP_AT(feet) + floor_at(feet) + POP3D_CHAR_PX` — `floor_at` samples the smoothed floor under the sprite so the ground can never out-pop the character (the round-4 "floor over character" bug).
  - **UI-panel pop:** `bg0_scan` (was `band_text_scan`) now also merges busy BG0 rows into window-panel rects (`DepthSnap.uiRect[6]`/`nui`); `ui_pop_eye` pops them per eye as clean shifted copies so dialogs/START/bag/party/menus gain strong depth (`UIPOP3D_PX` 5px) with sharp text. Runs every frame, any context.
  - **Adversarial review** (Workflow: 3 lenses gpu/gen3/integ × 2 skeptics; ran ~39 agents, verification truncated by a session limit but 3 findings cleared both votes + others had a confirming vote). **Fixed:** (1) panel detector gated on a **gen-3 profile** (`bg0_scan(core,p,overworld,d)`; `if(!p) return` before rect emit) + an overworld near-full-screen guard (`wc>=24 && hr>=14`) → no phantom full-frame max-disparity pop on arbitrary GBA games; (2) `ui_pop_eye` samples the **sharp-bilinear prescale** (`draw_pop_tex(pre,PRESCALE,PRE_TEX,GPU_LINEAR,…)`) so popped text isn't NEAREST-shimmered at 1.5× — directly serves "text must look good"; (3) generalized `draw_pop`→`draw_pop_tex` **clips the destination to the frame box** (kills per-eye letterbox bleed for ALL pops); (4) quad-warp fallback: dropped its per-tile ramp term + gated the DoF-band ramp on `warpOk` (no ramp islands / band-vs-floor rivalry in the no-shader path). Dropped BG0-scroll compensation deliberately (scroll regs are write-only/unreliable; the fixed-offset scan already detects text correctly → gen-3 text layer is screen-aligned).
  - **Known limitation:** battle BG0 is 32×64 → `bg0_scan` size-guard bails → no battle-text panel pop (also what protects the full-screen case). Deferred.
  - `.3dsx` + `.cia` build clean.
- **Verify on hardware (round 4):** hill>grass gradient; char always above floor; bottom-of-screen pops more; dialog/menu/bag/party panels pop with crisp text; no side-strip artifact; `worstMs` ≤16 ms.
- **Left off:** awaiting round-4 verdict; commit pending user OK (done next).

### Session — 2026-06-11 — HD-2D M3: LDR bloom

- **Intent:** User: "go to phase M3 now" → the study's M3 (LDR bloom), built on the M1/M2 infra.
- **Did (uncommitted, `main.c` only):**
  - `bloom_bright` — bright-pass from the **M1 half-res copy** (`dofTexA`, so `dof_prepare` now runs when `dofPass || bloomPass`) into a new 64×64 RGB565 VRAM target (`bloomTex/bloomTgt`, 60×40 content): TEV `GPU_SUBTRACT` (threshold `BLOOM_THRESH` 0xC8 ≈ only >78% channels glow) + `GPU_TEVSCALE_2`, raw C3D quad with plain `Mtx_Ortho` (offscreen targets are unrotated).
  - `bloom_add` — glow map composited **additively** (`C3D_AlphaBlend` ONE/ONE, then restore citro2d's standard blend + `C2D_Prepare`) over each eye AFTER `dof_bands`, modulated by `BLOOM_GAIN` 0x90 × `bloomLvl`; per-eye vbo slabs in a new 12-vert `bloomVbo` (allocated in `warp_grid_init`, reuses the warp passthrough shader + `warpProjLoc`).
  - **Gates:** overworld + **focused top screen only** (study budget rule) + `bloomLvl` eased to 0 whenever `textTop||textBot` (glowing white textboxes would halo their own text — the round-2/3 lesson applied preemptively).
  - **Menu/settings:** "Bloom" item `MENU_BLOOM_IDX` 13 (grid now a full 2×8; Change games→14, Quit→15), `Settings.bloom` appended; `settings_load` now tolerates all 3 historical file sizes (pre-DoF / DoF-era / current).
  - `.3dsx` + `.cia` build clean.
- **Verify on hardware:** glints/lamps/white-bright spots glow softly (most visible at night/in caves); glow dies with any text and when focus moves to the bottom game; toggle persists; `worstMs` ≤16 ms — **bloom is the first thing to drop** if the budget breaks (study's call).
- **Left off:** rounds 1–3 feedback all folded in; M1+M2+M3 in one uncommitted tree awaiting the combined hardware verdict.

### Session — 2026-06-10 (round 3) — DoF made subtle + universally text-aware; M2 grid warp

- **Hardware feedback:** round 1 "blur way too aggressive, top/bottom text unreadable" → round 2 (RAM-signal text gating + 67% alpha) STILL "way too much and overrides so much text".
- **Did (uncommitted):**
  - **Subtle blur rework** (`main.c`): one half-res `GPU_LINEAR` bounce (was two → quarter-res), `DOF_ALPHA` 0xAA→0x78 (~47%), sharp band widened 56–104→**48–112**; `dofTexB`/second pass deleted (saves a render-target pass/frame).
  - **Universal text-awareness:** `band_text_scan` + `band_rows_busy` (`main.c`) read DISPCNT/BG0CNT/VRAM at the parked handshake — **BG0 is gen-3's text/window layer** (verified in both decomps; standard textbox = bg0 window at tile rows 15–18). Non-filler tilemap entries under a band (top rows 0–6 / bottom rows 13–19, filler = dominant sampled entry, threshold 8 tiles) kill that band's blur. **Per-band**: `DepthSnap.textTop/.textBot`, `dofLvlTop/dofLvlBot` easing (fast-out ~3f, slow-in ~12f). Catches dialogs, banners, match-call, item popups — anything — with zero per-game addresses. RAM signals (`textDlg`/`textBanner` in `gamestate.c`, EM `sFieldMessageBoxMode` 0x020375BC / `Task_MapNamePopUpWindow` 0x080D487C, FR `sMessageBoxType` 0x0203709C / `Task_MapNamePopup` 0x080981AC, LG FR-derived) kept as precise per-band backups.
  - **M2 vertex-grid scenery warp** (earlier this session): `source/warp.v.pica` passthrough shader (+ Makefile picasso/bin2o rules), `warp_grid_init/fini/_eye` — 16×11 vert mesh per eye, corner depth = avg of adjacent `tdepth` cells, raw C3D draw (own attr/buf/texenv, `Mtx_OrthoTilt`, `GPU_CONSTANT` modulate = dim-tint analog, `C2D_Flush`→draw→`C2D_Prepare`), samples the sharp-bilinear prescale when active (UVs coincide: 2/512=1/256); replaces `warp_scenery_eye` tile tearing with smooth stretch; quad-warp fallback if shader init fails. Pops now drawn inside the `if (pop3d)` block AFTER the grid.
- **Verify on hardware (round 3):** all text readable everywhere; blur reads subtle; M2 stretch vs tear; `worstMs` ≤16 ms (M2's C2D↔C3D churn is the suspect if not).
- **Left off:** awaiting round-3 hardware verdict; commit pending user OK.

### Session — 2026-06-10 (later) — HD-2D M1 built

- **Intent:** User: "yes focus on the HD-2D research to really improve the 3D effect" → build **M1 tilt-shift depth-of-field** from `docs/kb/hd2d-octopath-3d.md` §3/§4.
- **Did (all in `source/main.c`, uncommitted):**
  - **DoF pipeline** — `dof_prepare` (two `GPU_LINEAR` down-bounces 240×160→120×80→60×40 into new RGB565 VRAM targets `dofTexA/dofTgtA` + `dofTexB/dofTgtB`; half-texel edge insets stop cleared/stride texels bleeding into the blur), `dof_band` (quarter-res copy upscaled 4× through the screen transform with a per-corner alpha ramp — `C2D_SetImageTint` blend 0 leaves RGB and uses tint alpha as transparency), `dof_bands` (solid blur rows 0–32 / ramp 32–56 / **sharp 56–104** / ramp 104–128 / solid 128–160).
  - **Integration** — one `dof_prepare` per frame after `C3D_FrameBegin`, `dof_bands` on BOTH eye targets after the pops (blur covers out-of-focus pop edges → less ghosting); gated `dofOn && dofTgtB && depth3d.overworld` — battles/menus/unsupported games stay sharp; slider-independent.
  - **Menu + settings** — new "DoF" item (`MENU_DOF_IDX` 12), grid 2×7→**2×8** (pitch 32→28 px, buttons 29→27 px, D-pad down now reaches the last odd item), `Settings.dof` appended with a size-tolerant load (old settings.bin keeps defaults, no reset).
  - **Study-recommended tune** — `POP3D_PX` 4.0→3.0 (the 2–3 px disparity ceiling vs edge ghosting).
  - Verified `.3dsx` + `.cia` both build; only pre-existing mGBA/upload_frame warnings remain.
- **Why these choices:** PICA200 has no fragment shader → bilinear bounce blur is the §3 recipe; both eyes share one blurred copy (+2 render-target passes total, inside the §3 budget); default **ON** so the next hardware boot shows the effect immediately.
- **Left off:** Awaiting hardware verdict (M1 gate) — commit pending user OK.

### Session — 2026-06-10

- **Intent:** Continue the dual-GBA build: finish the **3D depth** effect to the "full feasible version", fix a batch of **touch bugs**, and research two new directions the user asked about — a **PokéMMO-style co-op shared overworld** and an **Octopath HD-2D** look ("its possible even if we have to use an AI tool to render a somewhat 3D directions for 2D objects"). User is testing on a real New 3DS throughout.
- **Did:**
  - **3D depth M2–M4** (`acc4925`,`25e599a`,`06d0ce9`,`76b781c`): player + on-screen OAM NPCs pop OUT (fixed an inverted-disparity "pop-in" bug), foreground scenery pops via Emerald metatile layer-type with a 3×3 smoothing pass (the user's "average between tiles for arches" idea). Single composited frame, no extra emulation pass. User confirmed: clean (no garbage), flat when slider down, both games full-speed; "not perfect yet" + wanted pop-OUT (fixed) and M4 (added).
  - **Touch fixes** (`fc0777b`,`c373d80`): post-battle tap-anywhere-A fixed (battle gated on `callback2==BattleMainCB2`); BFS now routes **around** NPCs (gObjectEvents collision, goal exempt so tapping an NPC still talks); **double-tap your character = START**; title/intro (no map, px<0) → tap=A / double=START. Earlier in the session also: task-based fail-safe detection (recurring Emerald walk fix), hybrid walk, launcher touch.
  - **Four design studies run + saved** (`9b4c016`,`a00737e`,`949a6b7`,`1545cd2`): wireless multi-3DS link (FEASIBLE via a network SIO driver), 3D-depth (sprite-pop feasible, volumetric impractical), HD-2D (DoF+bloom cheap; AI normals yes / AI depth no), co-op overworld (overlay presence FEASIBLE; interaction via Union Room/link; can't force-start a trade).
  - Copied the `handoff` skill from gba-toolkit into `3ds-toolkit/.claude/skills/` and wrote this handoff.
- **Left off:** About to build **HD-2D M1 (tilt-shift DoF)** — user picked it from the menu — but **halted before any code change** so the user can resume in a **new chat (a different model)**. Working tree clean at `1545cd2`.
- **Open threads:** Hardware feedback pending on the current build (3D ghosting/tuning + the 5 touch fixes + whether FireRed's menu works now). Co-op needs Ruby/Sapphire symbol maps (M0). Still-deferred touch RE: bag pocket-tap, summary pages, naming keyboard. The four study plans are queued and independent.
