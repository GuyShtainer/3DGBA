# Phase 31 — DIORAMA: the 3D composition of the Emerald overworld

Decided 2026-09-10. Guy's ask: *"Start building the 3D composition of Pokémon Emerald using the
new source"* — the source being `tripplyons/pokeemerald-3d` (archived 2026-08-31 under
`projects/_reference/`, study-only), read together with the two earlier finds
(`pokeemerald-multiplatform`'s C voxel renderer and the DramaticShape mod). The goal is the
look of the two Facebook posts Guy shared on 2026-08-31 (`docs/kb/voxel-diorama-reference.md`):
the real GBA tile art extruded into a beveled 3D diorama, sprites standing up as billboards,
a pitched camera following the player.

**Inputs the implementer may read:** this file, the three clean-room distillations in this
folder (`RESEARCH-classification.md`, `RESEARCH-presenter.md`, `RESEARCH-polish.md`), the specs
this phase produces (`SPEC-*.md`), pret's public symbol maps / headers (functional facts about
the game), and this repository. **Never open `projects/_reference/`** (invariant 6).

## What ships (v1 = this phase)

On the **top screen**, while an **Emerald (BPEE)** game is in the free-roam overworld, replace
the flat frame with a real 3D scene built from the game's own map semantics:

1. **Terrain** — every map tile as a textured ground quad, textured with the real metatile art
   (composed from the tileset tiles + palettes the game has loaded).
2. **Massing** — impassable tiles classified by the collision-neighbourhood rule
   (solid-above → WALL, solid-below → ROOF, isolated → LOW; ledges/water/grass by behaviour),
   flood-filled into structures so a house extrudes as ONE volume with a coherent roof, exterior
   faces only, textured from the rows of art the map already stacks (`RESEARCH-classification.md`
   §3–§5). Indoor maps take the Emerald special-case table (§3.3), re-verified against pret.
3. **Sprites** — the player and every active object event as an upright billboard decoded from
   the game's live OAM + OBJ VRAM + palette (the phase-20 `peersprite` chain, generalised), at
   its exact sub-tile world position, depth-tested against the massing.
4. **Camera** — orbit follow camera: pitch ≈ 40°, yaw 0 (looking north), fov ≈ 35°, distance
   adaptive to map size, exponential follow; rebased across walk-through map connections, snapped
   on warps (§7).
5. **Fallback = exactness** — whenever the gate is shut (menu, textbox, battle, transition,
   non-overworld screen) the top screen shows today's flat frame. This is pokeemerald-3d's
   shipped stance (`RESEARCH-presenter.md` §3.6): fall back rather than approximate.
6. **Stereoscopic 3D, honest** (added 2026-09-10 on Guy's question "so it could be used with 3D on
   the top screen" — yes, and it is the point of real geometry): when the 3D slider is up, the
   right-eye target draws the SAME static vertex buffer with the camera shifted sideways by an
   interaxial offset scaled by the slider, converged on the player's ground tile (player at screen
   depth, roofs/trees toward the viewer, the far route receding). One extra draw call, no extra
   CPU or emulation work. Slider at zero = one camera, one pass, as today. The comfortable
   interaxial value is tuned on the real panel (invariant 8).

Polish (tilt-shift DoF, shadows, water, day/night) is **out of scope for v1** and listed in
§"After v1" so nobody builds it early.

## What bounds it (decided — do not re-litigate)

1. **Emerald only, this phase.** The reader is profile-driven so FR/LG/RS can follow, but the
   indoor id table and every verified address are BPEE's. Other games keep today's flat path.
2. **Top screen only.** The bottom screen stays the flat game — it is the touch surface.
3. **Ships OFF.** The control is the existing `DIORAMA · TILT` ladder on the ENHANCE tab gaining
   a fifth rung, `3D` (level 4): the flat path is byte-identical for every existing user and old
   settings files load unchanged. (If SPEC-render proves the 170-px segment cannot take a fifth
   rung legibly, it proposes the alternative; the default choice is the fifth rung.) On the
   bottom screen level 4 behaves as level 3.
4. **Per-TILE massing with grouped structures — never per-pixel heights.** The shipped negative
   result in pokeemerald-3d (`RESEARCH-presenter.md` §6: heights derived from gameplay masks
   "tear buildings apart") is binding. Heights come from the classifier + structure grouping.
5. **Neighbour maps are instanced for classification and geometry, textured only when they share
   both tilesets** with the current map (identical atlas). A neighbour with a different tileset is
   drawn as un-textured dark ground in v1 — never with the wrong art (§8.3.8 of the research).
   ROM tileset decompression for full neighbour art is an "After v1" item.
6. **Read-only, presentation-only.** No write to game RAM, ever. Zero effect on emulation, input,
   touch mapping, save data, audio, or the link.
7. **Frame budget over pixel fill.** Static geometry is built once per map (and when the live grid
   changes — doors, cut trees), never per frame. Per frame: the object-event/OAM reads, one small
   billboard VBO, one bound atlas, one or two draw calls. SPEC-render budgets the numbers.

## Invariants (binding on every implementer)

1. **Flat path byte-identical when the diorama is off.** One `dio_active()`-style check guards
   every new draw and every new read; level < 4 (or any gate rule shut) is today's code.
2. **The trade path is untouched.** No edits to `celiolink.c`, `netlink.c`, `gbacore.c`,
   `touch.c`, `wireless.c`, `fieldtrav.c`, `fieldpath.c`. Reads go through the existing
   `gbacore_read8/16/32` only. Verify with `git diff --stat` on those files: empty.
3. **Pure-C world module** (`source/diorama.{c,h}`, CLAUDE.md rule #4): `<stdint.h>`/`<string.h>`
   only; the game is reached through a read-only bus-callback struct (the `PsprBus` shape);
   ALL classification, grouping, mesh emission, atlas composition, billboard decode and camera
   math live there and dual-compile on the PC. `main.c` owns textures, VBO upload and the draw
   calls — nothing else. Host suite `test/host/test_diorama.c` with synthetic fixtures AND a real
   dumped Emerald map; its check count is part of the gate.
4. **Reads in the parked window, texture writes inside the frame.** Every bus read happens where
   `build_depth_grid`/`bg0_scan`/`presence_read` already read (main.c's parked window); every
   texture/VBO write happens inside `C3D_FrameBegin/End` (the phase-20 rule at main.c:1550).
5. **Gate = the existing tilt gate, with ONE rule re-decided for level 4.** `tilt_target_level()`
   (G1–G11) is the ONLY gate; level 4 is the diorama. No second gate ladder. When it shuts, the
   flat frame returns that same frame. **G10 ("stereo engaged shuts the tilt") does NOT apply to
   level 4:** the slider feeds the per-eye diorama camera instead (What ships #6), and the per-eye
   pop / warp / DoF / bloom / UI-pop passes are excluded under level 4 because the geometry
   replaces them. Levels 1–3 keep G10 exactly as shipped (test_tilt's truth table is unchanged for
   them).
6. **Clean room.** Nothing from `projects/_reference/` is opened, copied, or paraphrased at the
   code level. Ideas come from the RESEARCH docs; game facts from pret's public symbols/headers.
   The repo is public GPLv3 and every line here is ours.
7. **Suites stay green and grow;** `make` and `make cia` succeed after every slice with zero
   warnings in new files; a dated `BUILDLOG.md` entry per slice; `tools/closeout.sh` clean at
   the end.
8. **Emulator-visible is this phase's exit; hardware-final is the truth.** Unlike timing, Azahar
   renders PICA geometry faithfully enough to SEE the diorama, so the exit gate is: PC suites
   green, build green, and a Littleroot/Oldale screenshot from the `tools/emutest` harness showing
   extruded buildings and standing sprites. The HANDOFF gains the hardware checklist
   (frame budget with two cores, stereo barrier, the real panel).

## Existing machinery to build on (verified in-tree 2026-09-10)

- **Depth buffer exists:** `C2D_CreateScreenTarget` creates the top target with `GPU_RB_DEPTH16`
  (citro2d `source/base.c`), so `C3D_DepthTest(true, GPU_GREATER…)` works on `top` as-is.
- **Raw-C3D escape/return sequence** — the phase-14 block `tilt_draw_image` (main.c ≈1300-1360):
  `C2D_Flush → C3D_FrameDrawOn → C3D_BindProgram → AttrInfo/BufInfo → TexEnv → draw → restore`.
  `tilt.v.pica` is a screen-space shader; the diorama needs its own `diorama.v.pica` taking a full
  4×4 model-view-projection (`Mtx_PerspTilt` + look-at), positions in world units.
- **Bus reads:** `gbacore_read8/16/32` = mGBA `busRead*` — VRAM, OAM, palette RAM, EWRAM, IWRAM,
  ROM all readable (`gbacore.c:1339-1341`).
- **Verified BPEE symbols** (pokeemerald.sym, re-fetched 2026-09-10 to `/tmp/pret/`):
  `gMapHeader 0x02037318`, `gBackupMapLayout 0x03005DC0`, `gObjectEvents 0x02037350` (16×0x24),
  `gPlayerAvatar 0x02037590`, `gSprites 0x02020630` (stride 0x44), `gFieldCamera 0x03005DD0`,
  `gSaveBlock1Ptr 0x03005D8C`, `gPlttBufferUnfaded 0x02037714`, `gSpriteCoordOffsetX 0x02021BBC`,
  `gTotalCameraPixelOffsetX 0x03005DEC`, `gMain 0x030022C0`. Already in `GameProfile` (BPEE row,
  gamestate.c:23): `mapLayout`, `mapHeader`, `mapObjects`, `fieldCamera`, `sprites`, `pltt`,
  `playerAvatar`.
- **Metatile chain already walked:** `metatile_layer()` main.c:918 (`MapHeader+0 → MapLayout
  +0x10/+0x14 → Tileset+0x10 metatileAttributes`), `build_depth_grid()` main.c:939 (the live grid
  read with the +7 border cancel), `bg0_scan()` main.c:2160 (textbox detection).
- **Sprite decode:** `peersprite.{c,h}` — pure-C OAM → 4bpp → RGBA → tiled-texture chain with a
  read-only `PsprBus`; the diorama's billboard decoder generalises it (≤32×32, 16-colour,
  non-affine — the overworld object-event case).
- **Gate + control + persistence:** `tilt_target_level()` (tilt.c:143), the `PT_ENHANCE` table
  (main.c ≈2640, `ACT_TILT` nseg 4), `Settings.tilt`, the `TILT1/2/3` HUD chip, the phase-14
  `TiltSnap` parked-window snapshot (main.c:1215).
- **Emulator harness:** `tools/emutest` — Azahar launch (`azctl`), gdb reads (`gdbio`),
  screenshots (`see shot top`), movies (`ctm`). This is how the exit-gate screenshot is taken and
  how the real-map fixture is dumped.

## Slices

| # | Slice | Output | Who |
|---|---|---|---|
| S0 | Specs, in parallel, read-only | `SPEC-data.md` (every address/struct/cadence, verified vs pret), `SPEC-render.md` (shader, VBO/atlas formats, budget, frame-loop insertion, control), `SPEC-world.md` (module API, data model, fixtures, test plan) | 3 spec agents |
| S1 | World module, pure C | `source/diorama.{c,h}` classifier + structures + mesh + camera; `test/host/test_diorama.c` synthetic fixtures | implementer |
| S2 | Real-map fixture | a BPEE map dumped from Azahar → `test/fixtures/` + expected counts | harness |
| S3 | Atlas + billboards, pure C | metatile atlas composer (VRAM tiles + palettes → tiled texture bytes), object-event billboard decoder + world placement | implementer |
| S4 | Render integration | `diorama.v.pica`, VBO/atlas upload, the draw block, gate level 4 (G10 carve-out), **per-eye stereo pass**, 5th rung, HUD chip, BUILDLOG | implementer |
| S5 | Emulator proof | Littleroot / Oldale / an interior screenshot via the harness; the debug class-overlay | me + harness |
| S6 | Adversarial review + fix pass + closeout | 2–3 lenses, every finding re-verified, `tools/closeout.sh` | reviewers |

Each implementation slice self-reverts if it leaves a suite red or the build broken.

## Open questions (the specs settle these; record the answer in the spec, not here)

- O1 Sub-tile sprite placement: from `gSprites` screen coords relative to the player's sprite, or
  from object-event step timers? SPEC-data picks the one that is exact under camera pans.
- O2 The fifth rung's fit in the 170-px segment; alternative placement if it cannot fit.
- O3 Live-grid change detection (door animations, cut trees): hash cadence vs. per-frame diff.
- O4 Palette source for the atlas: hardware PALRAM (faded) vs `gPlttBufferUnfaded`; how a screen
  fade should read in the diorama (probably: gate shuts on the fade's context anyway).
- O5 v1.5 UI overlay: blit the field textbox / START-menu rectangle of the flat frame over the
  scene instead of falling back whole — only if the rect is provably opaque and RAM-detectable.

## After v1 (deliberately not now)

Tilt-shift DoF, blob then mapped shadows, water tiers, day/night grade, neighbour-tileset ROM
decompression, FR/LG profile, tall-grass tufts, the UI overlay (O5) — each hardware-gated, each
its own slice. (Stereo eyes moved INTO v1 — What ships #6.)
