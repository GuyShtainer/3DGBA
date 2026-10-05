# Phase 32 — VOXEL: Zallax's voxel overworld, widescreen, stereo, and a real touch panel

Decided 2026-10-05. Guy's ask, verbatim: *"Alright implement it, including the wide screen and make
sure the 3d works on screen. And also fix the touch controls. Its not so good now but the demo of
this repo shows it working nicely"* — "it" being ZallaxDev/pokeemerald-3Ds-dualscreen v0.2.0
(the 3D effect Guy said is "the 3D effect I wanted").

**Supersedes phase 31's build plan.** Phase 31 was writing a clean-room renderer from scratch
because every source then was unlicensed. Zallax's port code is MIT (legal gate:
`LEGAL-zallax-port.md`, OK-with-conditions), so we port a working renderer instead. Phase 31's
SPEC-data (addresses), gate design and stereo math (SPEC-render §3S) stay valid references.

## Upstream pin

`projects/_reference/pokeemerald-3Ds-dualscreen` at **v0.2.0 = c330c0a (2026-10-04)**. The
runtime data formats (`voxel/{regions,signposts,buildings,relief}.bin`, the `.pak`) are tied to
this commit. Never mix files from another upstream commit.

## What ships

Three tracks. All ship as settings; V and W ship **Off**, T becomes the single-game default only
after its emulator proof.

### V — the voxel overworld (Emerald BPEE only)
Zallax's `voxel/` world (terrain, buildings, trees, signposts, relief, lighting, fog, entities as
billboards, chunked mesh) drawn on the **top screen** whenever the top game is Emerald, the field
is free-roam, and the setting is on. Reads come from the emulated GBA (mGBA bus / RAM buffers)
through one adapter; nothing writes game RAM.

### W — widescreen
In voxel mode the field fills the **full 400×240** top screen (the world is drawn from map data,
not from the 240-px GBA frame). Camera **3D ANGLE** and **3D ZOOM** settings as in Zallax
(a steep angle reads as a widescreen 2D view). The game's own text boxes / menus that appear over
the field are composited on top, centred, from the GBA frame. Non-field screens (battle, menus,
title) show the GBA frame as today.
*Known limit, disclosed:* the GBA only spawns NPCs a couple of tiles outside its 240-px view, so
NPCs at the far left/right edges of the wide view pop in. Zallax avoids this by changing game
code; we do not patch the game.

### S — stereoscopic 3D that works on the voxel scene
Zallax renders one eye only (`ctr_voxel.c:5703`, "Stereoscopy is V8"). We add the per-eye pass:
parallel cameras with an asymmetric frustum, interaxial × 3D slider, converged on the player's
tile (phase 31 SPEC-render §3S). Overlays (text box) sit at screen depth. Slider at 0 = one pass.

### T — the touch panel (single-game mode)
Replaces the virtual gamepad on the bottom screen when one Gen-3 Pokémon game runs, modelled on
Zallax's bottom screen (ideas only, own code — legal condition 5):
1. **Right column**, always present: MAP · POKéMON · BAG · TRAINER · POKéDEX · POKéNAV · SAVE ·
   OPTION — each opens that START-menu entry by closed-loop key injection (open START, read the
   live menu list + cursor from RAM, move, press A). Entries the game hasn't unlocked are dimmed.
2. **Left 240×240 area**: while a menu screen is open the game's own frame is shown there 1:1 and
   taps go through the existing smart-touch families (they were built for exactly this); the top
   screen holds the last field frame. In the free-roam field the area is the tap-to-walk surface.
3. **Battle**: FIGHT · BAG · POKéMON · RUN as big buttons; FIGHT opens four move buttons with name,
   type and PP read from RAM. Tapping drives the game's own battle cursors (as smart touch does).
4. Dialogue: a tap anywhere on the left area = A.

## Invariants

1. **Presentation-only for V/W/S.** No game-RAM writes; emulation, saves, audio, link untouched.
   Off = today's frame, byte-identical.
2. **The trade path is untouched**: no edits to `celiolink.c`, `netlink.c`, `wireless.c`.
3. **Legal conditions are binding** (`LEGAL-zallax-port.md`): both upstream MIT notices kept
   (Zallax at c330c0a, pokeemerald-multiplatform at db1cab3d) in `source/voxel/NOTICE.md`, the
   repo NOTICE and the app's About/credits; every pret include deleted and replaced by our own
   `source/voxel/gba_game.h` written from SPEC-data; our own LZ77 decoder; "Modified for 3DGBA
   (GPLv3), 2026" in every vendored file header. Stereo and touch code are our own.
4. **Never ship generated data.** The voxel `.bin`s are made from the user's own ROM on the user's
   side. v1 reads them from the pack Zallax's builder writes (`sdmc:/3ds/emerald3ds/emerald3ds.pak`)
   or loose files in `sdmc:/3ds/3DGBA/voxel/`. We do not host, mirror or bundle Zallax's builder,
   recipe or pak; README links to their release. Missing data = the setting says why and stays flat.
5. **One render thread owns the GPU** (toolkit rule 2). Game state the voxel modules read is
   snapshotted in the parked window; chunk building after `C3D_FrameEnd` reads only the snapshot.
6. **Suites green and growing**; `make` and `make cia` clean after each slice; BUILDLOG entry per
   slice; `tools/closeout.sh` at the end.
7. **Exit gate = emulator-visible; hardware-final.** Azahar screenshot of the voxel field
   (Littleroot/Oldale) and of the touch panel. Stereo and frame budget are hardware-only checks and
   go on the HANDOFF checklist.

## Slices

| # | Slice | Who |
|---|---|---|
| P0 | Legal gate | ip-legal (done) |
| P1 | SPEC-port: every pret/compositor touchpoint → adapter function; snapshot set; render insertion; overlay composite; per-eye pass; data loading | design agent (opus) |
| P2 | Vendor + adapter: `source/voxel/` compiles for 3DS and host; `gba_game.h`; data loader (pak + loose) | implementer |
| P3 | Render integration in `main.c`: gate, draw, overlay, settings (VOXEL 3D / 3D ANGLE / 3D ZOOM) | implementer |
| P4 | Stereo per-eye pass | implementer |
| P5 | Touch panel | spec, then implementer |
| P6 | Emulator proof, adversarial review, closeout, HANDOFF checklist | me + reviewer |
