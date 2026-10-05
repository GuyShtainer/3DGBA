# Phase 31 — DIORAMA — build log

One dated entry per slice, newest last. Numbers are measured unless marked estimate.

## 2026-09-10 — S0 kick-off + S2 (fixture tooling) done early

- `PHASE.md` written (contract, bounds, invariants, slices). Three read-only spec agents launched in
  parallel (all opus): SPEC-data (addresses/structs vs pret), SPEC-render (citro3d pass, budget,
  control), SPEC-world (pure-C module API + test plan).
- **S2 landed ahead of S1**, because it needs no spec: `tools/diorama/diodump.py` reads a BPEE ROM
  file directly with pret's symbol map (`/tmp/pret/pokeemerald.sym`, re-fetched today) — map header
  → layout → both tilesets (LZ77-decompressed tiles, palettes, metatiles, u16 attributes) →
  connections → first-ring neighbour layouts (with a "tilesets match" flag) — and writes a TLV
  fixture `test/fixtures/dio_<map>.bin` (format documented in the tool header) plus a composed PNG
  of the map's real metatile art and a JSON provenance sidecar. Fixtures are ROM-derived and
  git-ignored (`.gitignore` gained `test/fixtures/dio_*`).
- **Proof the metatile decode is right:** `dio_littleroot.png` is Littleroot Town pixel-for-pixel
  (both layers, flips, primary/secondary palette split, backdrop colour) — composed from ROM only.
  The same composition rule is what the on-device atlas builder will run over VRAM.
- **First classification dry run (python, RESEARCH §3.4 rule only) on Littleroot:** 20×20, 132 solid
  cells → 88 WALL / 41 ROOF / 3 LOW; every house = 1 ROOF row over 3 WALL rows, trees = ROOF over
  WALL, mailboxes/signs = LOW. The rule produces the expected massing on real data before any C
  exists. Behaviours present in Littleroot: only 0x00 and 0x69 (3 cells) — SPEC-data names them.
- Fixtures dumped: LittlerootTown (20×20, 1 north connection, tilesets match), OldaleTown (20×20,
  3 connections, all match), BattleFrontier_BattleArenaLobby (16×13 indoor — the user's Emerald
  save sits here), BattleFrontier_OutsideWest (56×72, one east neighbour with DIFFERENT tilesets —
  the PHASE bound-5 case), LittlerootTown_BrendansHouse_1F (11×9 indoor).
- **Sizing facts from the ROM (all 518 map headers walked):** largest layout 80×80 = 6400 cells
  (Route 124/126/127 + their Underwater twins); tallest 40×140 (Routes 111/119); median 165 cells,
  p90 1681; at most 5 connections on one map; **largest current-map + first-ring-neighbours total =
  22,400 cells (Route 124)** — the number the world module's fixed storage is sized from.
  Distinct metatile ids per map: Littleroot 63, Oldale 71, BattleFrontier_OutsideWest **524** —
  so a "used ids only" atlas saves little on big maps; the full 1024-slot 512×512 atlas is the
  simple choice (SPEC-render decides the format).
- **Python reference model `tools/diorama/dioref.py`** (differential-testing twin of the future C
  module; RESEARCH §3–§4 rules verbatim + our own rules below) over the fixtures → golden JSON
  (`test/fixtures/dio_<map>.golden.json`: class grid + structure table), class-colour overlays, and
  an oblique "cardboard" 3D preview (`dio_<map>_preview.png`) — the intended look, seen today.
- **Two rules of OUR OWN, decided from the previews (record them in SPEC-world; they are not in any
  reference):**
  - **D1 VEG:** outdoors, a solid cell whose metatile id repeats vertically within ±1..4 rows in
    another solid cell is a PERIODIC BAND (tree lines, hedges, rock/cliff bands) → class `VEG`:
    excluded from structure grouping, extruded per cell to height 2 with same-class side culling,
    top face = own art, south face = own art (level 0) + the north row's art (level 1). Without it,
    Oldale's tree line became one 32-tall wall and Rustboro's east band 36-tall (the reference's
    known "trees are buildings" weakness, RESEARCH §3.1); with it, in-map maxima are 4 (Littleroot,
    Route 101), 4 (Oldale), 6 (Rustboro — its real big buildings). Buildings survive the rule:
    their rows are distinct metatiles (checked on the Rustboro + Petalburg overlays).
  - **D2 HMAX = 6:** structure height is capped at 6 tiles. Adjacent solid scenery (Petalburg's gym
    hedge) fuses into the building blob and drove one structure to 9; nothing real is taller than 6.
    Known limitation, disclosed: fused scenery shares the building's height.
- Fixture coverage now: Littleroot, Oldale, Petalburg, Rustboro, Route 101 (outdoor),
  Battle Arena lobby + Brendan's house 1F (indoor), BattleFrontier_OutsideWest (mismatched-tileset
  neighbour). `tools/emutest/roms/emerald.{gba,sav}` staged (git-ignored) for the S5 boot.
- **D3 indoor rules are BEHAVIOUR-only in v1.** The research's indoor metatile-ID table (622 filler,
  570 PC, 533/534 shelves, 576-586 tables, …) was checked against the real Emerald sheets
  (`gTileset_GenericBuilding` — 73 of the indoor maps — and `gTileset_BrendansMaysHouse`, rendered
  from ROM): the ids do not correspond to those objects in either. So v1 indoors = a tileset-agnostic
  VOID rule (all 8 tile entries blank ⇒ pure backdrop ⇒ not meshed) + behaviour predicates
  (PC/TV → FURNITURE, shelves → WALL, counter → COUNTER, doors → WALL) + the collision rule + alcove.
  Chairs/tables/beds fall to LOW boxes via collision — acceptable v1. A per-tileset id table can
  return later, keyed by the secondary tileset's ROM address, once each id is verified on a sheet.
- **Guy (13:5x): "So it could be used with 3D on the top screen" → YES, and it moves INTO v1:** PHASE
  "What ships" #6 (per-eye pass from the same static VBO, interaxial × slider, converged on the
  player's tile) and invariant 5 now carves level 4 out of G10 (the slider feeds the camera instead
  of shutting the effect). SPEC-render must specify the per-eye pass; test_tilt's G10 truth table
  stays unchanged for levels 1–3.
- `test/host/dio_fixture.h` — pure-C11 loader for DIOF / DIOG / DIOA (loud-SKIP helper included),
  verified against the Littleroot fixture: 132 solid cells (== the Python count), golden 23
  structures, atlas readable. The S1/S3 host suites build on it.
- **Emulator harness dry run (S5 channel check):** `azctl boot --stage-roms emerald --movie` boots
  the app into the game (READY in 1.7 s, gdb parked → `gdbio resume` works), but (a) `see shot`
  SKIPs — **Screen Recording permission for the host app (VS Code) is missing again** (was granted
  2026-08-08; needs re-grant + VS Code restart, a user action), and (b) `gdbio read-u32` timed out
  75 s after resume — being probed. `fbdump top` (pixel-exact framebuffer over gdb, zero OS
  permissions) is the fallback proof channel if gdb reads work.
- **SPEC-data.md DONE (opus agent, 1458 lines, 10/10 sections, 55-row address table).** Facts that
  change the build: (1) it independently DISPROVED the research's indoor id table from pret's own
  data (622 = a bookshelf top, 514-517 = upper back wall, 589 = the bed…) — D3 stands; (2) placement
  (O1): `sprite->x/y` DIFFERENCES vs the player's sprite are exact under pans/shakes (proof + worked
  example §7); absolute anchor = player tile + `gFieldCamera` sub-tile (forced 0 when standing);
  `y2` = jump height; (3) `gCamera` 0x02037334 (NEW profile column) tells a walked connection from a
  warp → rebase by −gCamera.(x,y); (4) every tile is in VRAM at 0x06000000+32·T, no LZ77 on device;
  atlas palettes from `gPlttBufferUnfaded` (BG slot 0 index 0 forced black = the backdrop);
  (5) the live grid's 7-tile skirt already holds real neighbour rows; (6) reads go in main.c
  3679-3712 (parked window), GPU writes inside 4507-5314; ~200 bus reads/frame steady state;
  (7) O3 = hash the visible 15×14 window every frame + 1/8 of the grid round-robin. Open: the
  `gPaletteFade` layout (workaround: diff faded vs unfaded palettes), 64×64 subsprite NPCs refused.
- **Emulator harness, three probes:** the Azahar PROCESS goes idle ~60 s after resume every time
  (CPU 33% → 0.3%, gdb reads time out, the D4 control file is never picked up); the Azahar log is
  silent after 16 s. `see` also SKIPs (Screen Recording grant for the host app is gone). Probe 3
  tests the App Nap hypothesis (`defaults write org.azahar-emu.azahar NSAppSleepDisabled YES` +
  `caffeinate`, activation at t=70 s).

## 2026-09-12 03:0x — session limit hit at ~14:03 on 09-10; resumed

- Both remaining spec agents (opus) died with HTTP 429 mid-write: SPEC-render at §5 (later found
  §1–7 + §3S "THE PER-EYE PASS" complete on disk, §8–11 pending); SPEC-world §1–11 complete
  (§7 mid-rewrite to RULES-v1), §12–13 pending. SPEC-data was complete. Incremental banking paid
  off — nothing was lost.
- Guy's rules recorded (memory): choose the agent MODEL BY ROLE COMPLEXITY (opus only for hard
  judgment; sonnet/haiku otherwise — the limit is shared) and SCHEDULE WAKE-UPS to resume
  autonomously after a limit. Applied now: SPEC-render §8–11 → sonnet; SPEC-world §12–13 →
  sonnet; **S1 implementer → sonnet** (its correctness is mechanically verified by the goldens);
  adversarial review later → one opus lens only; S4 render integration → opus.
- Probe 3 result: App Nap off + caffeinate + activation did NOT revive the stalled Azahar process
  (CPU 1% from ~55 s after resume; deterministic around app frame ~1750, i.e. the same point of
  the movie timeline each run; the app's own control channel is never serviced afterwards). Cause
  still unknown — parked until S4 needs the screenshot; diagnosis plan in SPEC-render §10.
- **2026-09-12 03:03 — HALTED by Guy ("don't continue working").** Wake-up cancelled; the three sonnet agents (SPEC-render §8-11, SPEC-world §12-13, S1 implementer) were stopped mid-flight — whatever they banked is on disk; check `git status` and the "(pending)" markers before resuming. Resume point: BRIEF-S1.md.
