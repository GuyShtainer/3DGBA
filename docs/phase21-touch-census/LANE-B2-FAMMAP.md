# LANE B2 (phase 24) — the FULL-class screens: FAM-MAP (tap-to-fly) first

_Session 2026-08-14, continuing directly from lane B1. Worktree
`.claude/worktrees/lane-b2-ph24` (branch `lane-b2-fammap`), Azahar **instance b** only
(`state-b/`, `runs-b/`, gdb port 24690, private `az-b/` bundle). Instance `a` belongs to another
lane and is never touched — its pid was observed live at start (93543, the user's own
`~/Applications/Azahar.app`) and left alone._

**The brief.** Lane B1 judged the 48 `TAP` rows. This lane takes the `FULL` rows with the best
value per hour, in the order the parent gave them:

1. **REGION MAP tap-to-fly, both families** (TOUCH-PLAN B7 + B8) — "the single most obviously
   touch-shaped screen in the game", and finally testable because lane A put a FLY user
   (LUGIA, box 8 → party slot 5) into `roms/emerald-fix.sav` (commit `af3bb50`).
2. **The FireRed exclusives** BERRY POUCH (E4) + TM CASE (E5) — list-family screens with their
   own cb2s; reuse the proven list driver.
3. **The FR Pokédex GRID** (F6) if the grid family covers it.

---

## Entry 0 — the desk pass: what was already known, and the one thing that had to be decided

Phase 23's lane B banked the FAM-MAP research rather than half-building it
(OVERNIGHT2-BUILDLOG "Banked for the next lane: TAP-TO-FLY is ready to implement"). Everything
there was re-read against the local pret clone this session before a line was written, and the
one open question it flagged — *"whether pokeemerald's `sprite->x` is the CENTRE or the corner is
the ONE thing to calibrate live rather than assume"* — is answered below by the round trip, not
by an assumption.

**What the screen actually is** (pret pokeemerald, local clone
`gba-toolkit/projects/PokeDNA/daycare map/pokeemerald`, cross-checked against the local
`pokeemerald.sym`):

| fact | where | value |
|---|---|---|
| the live struct | `src/region_map.c` `InitRegionMapData` | `sRegionMap = regionMap` — set for the wall map, the fly map's embedded `sFlyMap->regionMap` **and** PokéNav, so ONE pointer covers every instance |
| its address | `pokeemerald.sym` | `0203a144 l 00000004 sRegionMap` (a POINTER — deref) |
| cursor | `include/region_map.h:28-83` | `mapSecId` +0x000 u16 · `mapSecType` +0x002 u8 · `cursorPosX` +0x054 u16 · `cursorPosY` +0x056 u16 · `zoomed` +0x078 |
| bounds | `src/region_map.c:41-46` | cursorPosX **1..28**, cursorPosY **2..16** |
| cell → px | `CreateRegionMapCursor` :1418-1419 | `x = 8*cursorPosX + 4`, `y = 8*cursorPosY + 4` |
| movement | `ProcessRegionMapInput_Full` :653-690 | **JOY_HELD**; X and Y read **independently** (a diagonal is ONE frame); a move sets `cursorMovementFrameCounter = 4` and swaps the callback to `MoveRegionMapCursor_Full`, which does **not** poll input during the slide and writes `cursorPosX/Y` only at its END |
| fly A | `CB_HandleFlyMapInput` | A is accepted **only** on `MAPSECTYPE_CITY_CANFLY` (2) or `MAPSECTYPE_BATTLE_FRONTIER` (4) |
| wall A | `field_region_map.c` `CB_HandleInput` | A **and** B both EXIT |

**The design decision that follows from the movement model, and it is the whole slice.** The
obvious driver holds the D-pad until the live cursor arrives. That is wrong here: our read of
`cursorPosX/Y` lags the emulated frame, and a held key keeps moving — so the loop overshoots and
then oscillates. But because the engine *ignores input for the whole 4-frame slide*, a **single-
frame press moves exactly one cell no matter what happens next**. So the driver presses one frame
per cell and waits (`MAPNAV_GAP 5`), closed-loop on the live cursor. It cannot overshoot by
construction — and the host suite grades exactly that (TEST 15 counts overshoots against an
engine model: 0 in 176 400 routes).

**The second decision: A is NOT class-wide.** The two screens share a cursor model and disagree
completely about A — on the fly map it commits a destination, on the wall map it closes the
screen. So `GameState.mapFly` carries which one is up (they have different cb2s in Emerald), and
the wall map is cursor-only. A family that "just presses A on arrival" would make the wall map
un-usable, and that is the kind of thing that only shows up on the fourth screen.

**What this slice cannot break.** Both map cb2s were ALREADY in Emerald's `cb2FullUi` list
(census [exact]), i.e. both screens already detected and already had FAM-DLG's tap=A/hold=B/drag.
`GCTX_MAP` is tested *before* the `cb2FullUi` loop, exactly like `GCTX_NAMING`/`GCTX_STORAGE`, so
the change is a re-CLASSIFICATION of two already-detected screens. A game with no map anchors
(FR/LG/RS) never matches and keeps today's behaviour — pinned by test_profiles TEST 18.

**FR/LG are 0 for a measured reason, not an unfinished one.** The census harvested **one** cb2
for both FireRed map screens (`CB2_RegionMap` 0x080C08C8 — "town map AND fly map, one loop, mode
internal", CB2-HARVEST.md), so *fly-vs-wall is not decidable from the callback*, and firing an
arrival A on the wall variant would close the map. No pokefirered symbol map was available this
session to resolve FR's region-map struct pointer either. Named degradation, written into the
profile row.

---

## Entry 1 — the code, and the host proofs that grade it

Commit `5b28689` on `lane-b2-fammap`.

| file | what |
|---|---|
| `source/touchgeom.{c,h}` | `mapgeom_hit` (px → cell, the exact inverse of the game's cursor formula) + `mapnav_step` (one closed-loop frame, a bitmask so diagonals cost one frame) — pure C, no bus, no state |
| `source/gamestate.{c,h}` | `GCTX_MAP` + `GameState.mapFly` + the three profile columns `rmPtr` / `rmFlyCb` / `rmWallCb` (EM `0x0203A144` / `0x081248D4` / `0x08170274`; every other game explicitly 0) |
| `source/touch.c` | `rmap_read` (the guarded struct read) + `map_update` (the driver) + the `GCTX_MAP` dispatch arm + the `g_touchDbg` mirror |
| `source/touch.h` | `TouchSmart.mapFly` + eleven `TouchDbg` fields at +0xD4..+0x100 (5 counters + 4 live game reads + the armed target) |
| `source/main.c` | one line: `sm.mapFly = gsr.mapFly` |

**Host suites (the desk half of the proof):**

- `test_touchgeom` **158 971 → 362 691 checks, 0 failures**
  - **TEST 14** round-trips *every* legal cell through the pixel the GAME draws its cursor at,
    then sweeps all 240×160 px: the map body is exactly x[8,232) y[16,136) and everything else
    is DEAD — the destination-name window is not clamped to the nearest square (rule M1: never
    fly somewhere the finger did not point).
  - **TEST 15** drives a transcription of the engine's own input pair over **176 400 routes**
    (420 starts × 420 targets) and asserts every one converges in **exactly** `max(|dx|,|dy|)`
    presses with **zero** overshoots — i.e. the diagonal is really used and the one-frame-press
    rule holds everywhere.
- `test_profiles` **1660 → 1678 checks, 0 failures** (**TEST 18**: the three columns, the
  EWRAM/ROM split, both cb2s still inside `cb2FullUi`, fly/wall behaviour through the real
  `game_read`, and FR's own region-map cb2 proven *not* to reach `GCTX_MAP`).
- `fieldpath` UNMODIFIED at **1808**.

`make -j8` clean in the worktree (`3DGBA.3dsx` 4 399 100 B).

_Emulator proof: Entry 2._
