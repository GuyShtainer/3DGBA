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

---

## Entry 2 — ✅ **TAP-TO-FLY, LIVE**: one tap on the map and the game flew to Littleroot

Boot `runs-b/20260814-152612`, instance **b**, `--stage-roms firered,emerald-fix` — the pair, with
**Emerald as gameB so it renders on the BOTTOM screen and owns touch** without touching
`settings.bin` at all (the seat order IS the swap; B1 flipped the pref instead, this is the
cheaper half of the same technique). FireRed sat on the top screen at its title the whole time,
deliberately: leaving it there skips FR's ~3.5-minute quest-log replay, and the lane needed
Emerald.

### Getting to the screen (the setup, so the proof is reproducible)

| # | act | evidence |
|---|---|---|
| 1 | D4 seat 2: boot prefix `W600 s W300 s W300 s W300 s W300 a` then one more `a` (the first landed in a fade) | `ctx = 1`, save = **26.28 (13,8)** = the Battle Frontier building lane A saved in |
| 2 | D4 `D1 L6 D3` | live p → **(7,12)** — the map's one warp tile |
| 3 | D4 `D1` (landmine 6: an arrow warp costs a SECOND token) | map **26.28 → 26.14**, p **(39,30)** — outdoors, where FLY is legal |
| 4 | D4 `s`, `D`, `a` | `ctx = 5` GCTX_PARTY |
| 5 | D4 `D D D D`, `a` | the popup: **SUMMARY / FLY / SURF / SWITCH / ITEM / CANCEL** on LUGIA (party slot 5 — the mon lane A withdrew from box 8 for exactly this) |
| 6 | **TOUCH** `t 224 122` on the FLY row | `ctx` **6 → 14 = GCTX_MAP** — the fmenu family opened the fly map, and the new context claimed it |

### The screen classifies, and the struct read is right — proven before any tap

```
ctx      = 14 (GCTX_MAP)      mapIsFly = 1        (CB2_FlyMap matched rmFlyCb)
mapCurX  = 23   mapCurY = 14  mapSecId = 58       mapSecType = 4 (BATTLE_FRONTIER)
```

That is the whole address chain validated in one read, and it validates itself: the game puts
the fly cursor on **where the player is standing**, and the player was standing at the Battle
Frontier — `mapSecId 58 = MAPSEC_BATTLE_FRONTIER`, `mapSecType 4 = MAPSECTYPE_BATTLE_FRONTIER`.
A wrong `rmPtr` or a wrong offset could not produce that agreement.
The cursor's drawn position agrees too: cell (23,14) → the engine's `8*c+4` formula → GBA px
(188,116) → screen (251,168) → the capture shows the cursor box at exactly that spot
(`EM-B8-flymap-open.png`). **That is the calibration phase 23 flagged as "the ONE thing to
verify live rather than assume" — `sprite->x` is the CELL CENTRE, and `mapgeom_hit`'s
`gx >> 3` inverse is right.**

### Tap 1 — an OCEAN cell: the cursor walks there and **no A is emitted**

`t 133 179` = GBA (100,124) = cell **(12,15)**, open water.

```
mapTaps 0 -> 1   mapSteps 0 -> 11   mapArrive 0 -> 1   mapFlies 0 -> 0
mapCurX -> 12    mapCurY -> 15      mapSecId -> 213 (0xD5 = MAPSEC_NONE)   mapSecType -> 0
mapTgtX/Y -> -1  (disarmed on arrival)
```

Three separate things are proven by that one line:

1. **the hit geometry is exact** — the cursor landed on the cell the finger pointed at, and the
   capture (`EM-B8-tap-ocean-no-A.png`) shows the dashed cursor box out in the ocean with the
   destination-name window **empty**;
2. **the navigator costs exactly what the host suite predicted** — `mapSteps = 11 =
   max(|12-23|, |15-14|)`, the Chebyshev distance, i.e. the diagonal really is one frame and not
   one press was wasted. TEST 15's oracle and the live game agree on the number;
3. **the game's own acceptance test is honoured** — `mapSecType = 0`, so `mapFlies` stayed **0**:
   no A was emitted, the map stayed open, the player did not move. A driver that pressed A on
   every arrival would have looked identical on screen here and been wrong on the wall map.

### Tap 2 — **LITTLEROOT TOWN**, and the game flew

`t 59 157` = GBA (44,108) = cell **(5,13)** = `gRegionMapEntries[MAPSEC_LITTLEROOT_TOWN]`
(x 4, y 11 → +MAPCURSOR_X_MIN/Y_MIN), from the pret data file — i.e. the tap coordinate was
computed from the GAME'S OWN table, not read off a screenshot.

```
mapTaps  1 -> 2      mapSteps 11 -> 18    (+7 = max(|5-12|,|13-15|), again exact)
mapArrive 1 -> 2     mapFlies 0 -> 1      <- the confirm fired
then: ctx 14 -> 1, and every live map read goes to -1 (the struct is freed — the screen is gone)

g_fieldDbg  BEFORE: mapGroup.mapNum = 26.14  p = (39,30)     [Battle Frontier, outdoors]
            AFTER : mapGroup.mapNum =  0.9   p = (14, 9)     [LITTLEROOT TOWN]
```

and the game drew its own map-name banner: **`LITTLEROOT TOWN`**
(`EM-B8-flew-to-littleroot.png`).

That is TOUCH-PLAN slice 22.4's acceptance test executed verbatim — *"tap Littleroot on the fly
map, read the cursor mapsec before/after, confirm the warp via SaveBlock1.location"* — with the
before/after on both channels. **Row B8 = VERIFIED.**
