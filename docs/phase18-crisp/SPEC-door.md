# SPEC-door — smart touch must enter doors and warps, not tackle walls

Phase 18 ("crisp"), workstream 2 of 3. Binding spec for the smart-touch pathfinder.

**User report (hardware, phase 17):** smart touch _"mostly works finding a path on its own and
walk, it also fail on simple occasions, for example standing right next to a door, touching the
door makes the player tackle the wall and not enter the door (up and then left instead of left and
than up for example)"._

**Scope.** `source/touch.c` (the SMART overworld router) plus one new pure-C module and two
plumbing fields. **Out of scope and untouched:** emulation, threading, the link/net drivers
(`celiolink.c` — empty diff since phase 13, keep it that way — `netlink.c`, `gbacore` SIO), the
HD-2D render passes, and every drawing path (so the six themes cannot regress: this phase draws
nothing new).

**Evidence base.** Everything in T1–T3 was derived without guessing: pret source downloaded and
read line-by-line, pret byte-matched `symbols`-branch maps, and — because the emulator was held by
a sibling workstream for the whole session — a **static extraction from the user's own ROMs and
save** (`/tmp/…/scratchpad/door/gen3map.py`, a faithful Python port of `plan_bfs`). That tool
reads the real Emerald/FireRed map grids, tileset attributes and warp-event tables out of
`sdmc/dual-gba/gameA.gba` / `gameB.gba` (read-only) and re-runs the shipped algorithm against
them, so the "what path does the user actually get" question is answered from real map data
rather than from reasoning. T6 states what still has to be shown live.

---

## T1. What the router does today (read of `source/touch.c`, end to end)

**T1.1 Mode dispatch.** `touch_update` (`touch.c:571`) routes `GCTX_OVERWORLD` to `walk_update`
(`touch.c:615-618`) after resetting every other sub-machine. `sm.px/sm.py` are the player's
`SaveBlock1.pos` tile (`gamestate.c:118-119`); `gx,gy` are the touch mapped into GBA pixel space
by `main.c:684 touch_to_gba` and are `-1/false` off-frame.

**T1.2 Tap vs hold discrimination** (`touch.c:303-304`, `:325-326`). A press latches
`s_downGx/Gy` (GBA px) and `s_downPx/Py` (the player tile **at press time**). Every touching frame
increments `s_touchFrames` and sets `s_moved` if the finger has travelled >8 px on either axis. On
release, `tap = s_touchFrames <= TAP_FRAMES(12) && !s_moved`. While *touching*, the router is in
hold-steer mode (`:317-323`): it cancels any route and returns the dominant-axis direction toward
the finger. Only a clean tap plans a route.

**T1.3 Tap→tile mapping** (`touch.c:327`, `:334`). `ddx = s_downGx/16 - 7`, `ddy = s_downGy/16 - 5`;
goal = `(s_downPx + ddx, s_downPy + ddy)`. I.e. the camera is assumed to pin the player at screen
tile **(col 7, row 5)**. `ddx==0 && ddy==0` is the tap-self gesture: single → A pulse, double
(within 16 frames) → START (`:330-333`). The (7,5) anchor is corroborated in two other places in
our own source (`presence.c:371-383`, `main.c:916-919`) but has **never been independently
verified**; `presence.h:54` already flags the Y half as "could be 8 px wrong" (see Open Q1).

**T1.4 The collision read** (`touch.c:242-256`). `map_read` pulls `width/height/map*` from
`gBackupMapLayout` (EM `0x03005DC0`) / `VMap` (FR/LG `0x03005040`). `walkable()` adds the
`MAP_OFFSET` +7 border bias, rejects `0x03FF` (`MAPGRID_UNDEFINED`), rejects any tile holding an
NPC, and returns `((block & 0x0C00) >> 10) == 0`. **Collision bits only.** Elevation (bits 12-15)
is read by nobody. See T5.1 — this is the single largest defect found.

**T1.5 NPC read** (`touch.c:230-240`). `gObjectEvents[1..15]` (stride 0x24, `+0` active bit,
`+0x10/+0x12` currentCoords) are sampled **once, at plan time**, and treated as permanent walls
for the life of the route. Slot 0 (the player) is skipped.

**T1.6 The search** (`plan_bfs`, `touch.c:258-295`). Breadth-first over a 65×65 window
(`WBOX/WHALF`, ±32 tiles) centred on the player, FIFO queue, `parent[]` set on first discovery.
Neighbour order is fixed at `dxs[4] = {1,-1,0,0}, dys[4] = {0,0,1,-1}` (`touch.c:268`) —
**RIGHT, LEFT, DOWN, UP**. Line `:283` is the only special case: `if (nidx != goal && !walkable(...)) continue;`
— *"A blocked GOAL is allowed as the terminal (door/NPC)"*. The goal test happens on pop
(`:275`), so the parent that first discovered the goal wins.

**T1.7 THE TIE-BREAK, stated exactly.** With a FIFO queue and a fixed child order, the first
shortest path to reach any node is the **lexicographically smallest direction string** under the
order **R < L < D < U**. Consequences, verified by running the ported algorithm on the real
Mauville City grid:

| goal relative to player | path you get | final step |
|---|---|---|
| up-left diagonal | `L…U…` (horizontal first) | UP |
| up-right diagonal | `R…U…` | UP |
| down-left / down-right | `L…D…` / `R…D…` | DOWN |
| directly east/west | `R…` / `L…` | horizontal |

So on an **open** grid the last step toward anything north of you is already UP, which is why the
diagonal door case in the user's example is *not* reproducible on vanilla geometry (T2.4). The
real defect is not the tie-break — it is that **arrival direction is an accident of the search
rather than a constraint**, and that **there is no terminal action at all**.

**T1.8 The follow loop** (`touch.c:342-352`). Per frame: re-read the map; abort if
`ptr/width/height` changed (`:344`); stop when `px==goalX && py==goalY` or the path is exhausted
(`:345`); advance `s_pathPos` when `px/py` changed (`:346-348`); otherwise count `s_stall`, and at
>24 frames **cancel the route and fire a 3-frame A pulse** (`:349`). Emit `s_keyDir[dir]`
(`:350-352`).

**T1.9 What that means for a door.** The path's last element is the step *into* the impassable
door tile, so the loop emits UP and the player cannot move; `s_stall` runs to 25 and then presses
A. The door only opens because the game samples held UP during those ≤24 frames. Nothing in the
router knows a door exists, that north is the only legal approach, or that arrival should be
followed by anything.

**T1.10 Map-change detection is unsound.** `:344` compares `map`, `width`, `height`.
`gBackupMapLayout.map` is a **fixed EWRAM buffer** (`sBackupMapData`, pokeemerald
`src/fieldmap.c:96`; `gBackupMapData`, pokefirered `src/fieldmap.c:107`) so the pointer never
changes across a warp; only the padded dimensions do, and only when the two maps differ in size.
`SaveBlock1.location` (`mapGroup/mapNum`) is already read every frame by `gamestate.c:120-121`
but is **not plumbed into `TouchSmart`** (`touch.h:20-44`), so the router cannot see a warp
happen. A route therefore survives the very warp it triggered and keeps pressing directions on
the arrival map.

---

## T2. Reproduction and measurement

**T2.1 Method (emulator-free, repeatable).** `scratchpad/door/gen3map.py` parses, from the user's
ROMs: `gMapGroups` → `MapHeader` → `MapLayout` → map grid + both tilesets' metatile-attribute
tables + `MapEvents.warps`, then re-runs a line-faithful port of `plan_bfs` (same `dxs/dys`, same
blocked-goal exemption, same window) over the real grid. Struct offsets and masks are cited in
T3. Symbol bases: `gMapGroups` EM `0x08486578` (`pokeemerald.sym:43533`), FR rev1 `0x08352718`
(`pokefirered_rev1.sym:29537`). Cross-check that this is the *user's* build: `gameA.gba` header
`BPEE` rev 0, `gameB.gba` `BPRE` rev 1 — matching the maps used.

**T2.2 Where the saves actually are.** Parsed from `dual-gba/gameA.sav` / `gameB.sav` (newest
save slot, sector id 1 = `SaveBlock1`, `pos` at +0x00, `location` at +0x04):

- gameA (Emerald): **map group 10, num 6 = `MauvilleCity_PokemonCenter_2F`**, player at **(9,4)**.
- gameB (FireRed): **group 13, num 1 = `IndigoPlateau_PokemonCenter_2F`**, player at **(9,4)**.

Both are the wireless-trade club — which is why the phase-17 hardware run started there, and it
is the anchor for the T6 scenarios (the route out of the club exercises three different warp
kinds before you reach a door).

**T2.3 The warp tiles of the save's own building** (extracted, Emerald):

```
MauvilleCity_PokemonCenter_2F (10/6) 14x10
   warp id=2 at (1,6)  -> 10/5   tile beh=0x6B (DOWN_ESCALATOR)  coll=0
   warp id=0 at (5,1)  -> 25/60  tile beh=0x69 (ANIMATED_DOOR)   coll=1
   warp id=0 at (9,1)  -> 25/25  tile beh=0x69 (ANIMATED_DOOR)   coll=1
MauvilleCity_PokemonCenter_1F (10/5) 14x9
   warp id=1 at (7,8)  -> 0/2    tile beh=0x65 (SOUTH_ARROW_WARP) coll=0
   warp id=1 at (6,8)  -> 0/2    tile beh=0x65 (SOUTH_ARROW_WARP) coll=0
   warp id=0 at (1,6)  -> 10/6   tile beh=0x6A (UP_ESCALATOR)     coll=0
MauvilleCity (0/2) 40x20
   warp at (8,5) (22,5) (35,5) (23,14) (32,14) (19,14)  beh=0x69 coll=1   [six building doors]
   warp at (8,13)                                        beh=0x60 coll=0   [Game Corner, non-anim door]
```

**T2.4 The door path, on real geometry.** Mauville City around the Pokémon Center door (22,5);
`#` impassable, `D` the door, `.` walkable:

```
     17 18 19 20 21 22 23 24 25
  4   .  .  .  #  #  #  #  #  #
  5   .  .  .  .  #  D  #  #  #
  6   .  .  .  .  .  .  .  .  .
```

Current `plan_bfs` output, measured:

| player | path today | final step | outcome |
|---|---|---|---|
| (23,6) | `L U` | UP | door opens (by luck of geometry) |
| (21,6) | `R U` | UP | door opens |
| (22,6) | `U` | UP | door opens |
| (24,8) | `L L U U U` | UP | door opens |
| (23,5) *(if it were standable)* | `L` | **LEFT** | **bump — no warp** |

**T2.5 Census: why the user's literal case is rare but the class is real.** Across **all 518
Emerald maps** and **all 425 FireRed maps** (every warp event, classified by its tile's real
metatile behaviour):

- Emerald: 195 `MB_ANIMATED_DOOR` warp tiles; **8 (4.1 %)** have any walkable non-south neighbour
  (all of them north-side: Route 110/111 gatehouses, Battle Palace) — the rest are walled in, so
  the search is *forced* into the correct approach.
- FireRed: 195 `MB_WARP_DOOR` tiles; **0** have a walkable side neighbour, and **0** lack a
  walkable south tile.

So the "one step, wrong direction" bump cannot normally happen on a vanilla door — **unless the
south approach tile is occupied** (an NPC read as a wall, T1.5) or **the goal tile is not the tile
the user meant** (T1.3 anchor / the door graphic is two metatiles tall and only the **bottom** one
is the warp). Both are live candidates for the user's exact sentence and both are discriminated by
the T6 instrumentation. What the census *does* prove is far bigger than the door case:

**T2.6 The class that is 100 % broken today.** Warp tiles that need a **direction press after you
stand on them** — the router routes onto them and then stops (`touch.c:345`), pressing nothing:

| kind | Emerald | FireRed |
|---|---|---|
| `MB_SOUTH_ARROW_WARP` family (building exit mats) | **512** | **263** |
| other arrow warps (E/W/N) | 16 | 82 |
| FRLG directional stair warps `0x6C–0x6F` | — | **123** |
| **total broken-by-construction warp tiles** | **528** | **468** |

`MB_SOUTH_ARROW_WARP` is the single most common warp tile in both games: **it is the mat you stand
on to leave almost every building**. "Tap the exit to leave" does nothing today, in every building,
in both games.

**T2.7 The other measured defect.** See T5.1 — the router will walk you into the sea.

**T2.8 Not yet reproduced live.** The emulator was occupied by a sibling workstream for this
session (`azctl` lock + a live Azahar pid with staged Tier-B fixtures), and the harness rule is one
Azahar at a time. Nothing here depends on the emulator: every number above comes from the user's
own ROM bytes. T6 is the live plan, and it must be executed before this spec is called done.

---

## T3. Ground truth: how Gen-3 warps actually trigger

All citations are from pret master, downloaded and read for this spec.

**T3.1 Doors trigger on a NORTH bump against the tile IN FRONT of you.**
`ProcessPlayerFieldInput` (pokeemerald `src/field_control_avatar.c:170-179`, pokefirered
`:258-281`) does `GetInFrontOfPlayerPosition(&position)` and then

```c
if (input->heldDirection2 && input->dpadDirection == playerDirection)
    if (TryDoorWarp(&position, metatileBehavior, playerDirection) == TRUE) return TRUE;
```

and `TryDoorWarp` (pokeemerald `:584-605`, pokefirered `:987-1006`) is gated by
`if (direction == DIR_NORTH)` before `MetatileBehavior_IsWarpDoor` +
`GetWarpEventAtMapPosition` + `IsWarpMetatileBehavior`. **DIR_NORTH is hard-coded in both
engines.** `heldDirection2` is set by `FieldGetPlayerInput` (`:88-113`) only while the avatar is
at `T_TILE_CENTER`/`T_NOT_MOVING`, i.e. standing still with the d-pad held — so the trigger is a
*sustained* press, not a tap, and the first frame or two are consumed turning to face north.
The player never stands on the door tile.

**T3.2 Step-on warps trigger when a step COMPLETES on the tile.** `ProcessPlayerFieldInput` runs
`TryStartStepBasedScript` under `if (input->tookStep)`, which reaches `TryStartWarpEventScript`
(pokeemerald `:478-515`, pokefirered `:856-899`): warp event present **and**
`IsWarpMetatileBehavior(behaviour)` → `DoWarp` / `DoEscalatorWarp` / `DoTeleportTileWarp` /
`DoSpinExitWarp` / hole scripts. Approach direction is irrelevant.

**T3.3 Arrow warps and FRLG stair warps trigger on YOUR OWN tile plus a held direction.**
`TryArrowWarp` (pokeemerald `:466-476`, pokefirered `:825-853`) is called with the *player's*
position under `if (input->heldDirection && input->dpadDirection == playerDirection)`;
`IsArrowWarpMetatileBehavior` switches on the direction. FireRed adds
`IsDirectionalStairWarpMetatileBehavior` in the same block (`pokefirered:839-849`): DIR_WEST
matches the two `*_LEFT_STAIR_WARP` behaviours, DIR_EAST the two `*_RIGHT_STAIR_WARP` ones.

**T3.4 A warp EVENT does not mean a warp TRIGGER.** `IsWarpMetatileBehavior` is the gate in every
path. Measured: **94 Emerald / 239 FireRed warp events sit on `MB_NORMAL` tiles** — those are
arrival destinations of the reverse warp, not triggers. Therefore *the metatile behaviour is the
classifier and the warp-event list is only a confirmation*. Using the warp list alone would make
the router try to "enter" the middle of a room.

**T3.5 What we can read, and how.** The `u16` we already fetch per tile
(`gBackupMapLayout.map[x + 7 + width*(y + 7)]`) packs, in **both** engines
(`include/global.fieldmap.h:7-12`): metatile id `&0x03FF`, collision `&0x0C00 >>10`, elevation
`&0xF000 >>12`; `0x03FF` = `MAPGRID_UNDEFINED`. The backup grid is the map padded by
`MAP_OFFSET_W=15` / `MAP_OFFSET_H=14` (`fieldmap.c:97-98` / `:108-109`), so map-local
coordinates are valid for `0 <= x < width-15`, `0 <= y < height-14`.

The **behaviour** is *not* in the grid; it comes from the tileset attribute tables, reachable from
`gMapHeader` only, and **the chain differs per engine**:

| | Emerald (RSE) | FireRed / LeafGreen |
|---|---|---|
| `MapHeader` | `+0x00 mapLayout*`, `+0x04 events*` | identical |
| `MapLayout` | `+0x10 primaryTileset*`, `+0x14 secondaryTileset*` | identical |
| `Tileset.metatileAttributes` | **`+0x10`, `const u16*`** | **`+0x14`, `const u32*`** (`+0x10` is the callback) |
| primary tileset size | **512** (`fieldmap.h:6`) | **640** (`fieldmap.h:8`) |
| behaviour mask | `attr & 0x00FF` (`global.fieldmap.h:39`) | `attr & 0x000001FF` (`fieldmap.c:64`) |
| id ≥ 1024 | `MB_INVALID` | `MB_INVALID` |

`GetMetatileAttributesById` pokeemerald `src/fieldmap.c:375-389`; pokefirered
`MapGridGetMetatileAttributeAt` `src/fieldmap.c:385-394` + `sMetatileAttrMasks` `:63-72`.

**T3.6 The warp-event table** (identical layout both engines, `global.fieldmap.h:112-119` /
`:136-143` and `:145-155` / `:165-175`): `MapEvents` `+0x00 objCount u8, +0x01 warpCount u8,
+0x02 coordCount, +0x03 bgCount, +0x04 objs*, +0x08 warps*, +0x0C coords*, +0x10 bgs*`;
`WarpEvent` is 8 bytes `{s16 x, s16 y, u8 elevation, u8 warpId, u8 mapNum, u8 mapGroup}`.
`GetWarpEventAtPosition` (pokeemerald `:860-875`) matches x/y and then
`elevation == playerElevation || elevation == ELEVATION_TRANSITION(0)`.

**T3.7 `gMapHeader` addresses (VERIFIED-SYM, no derivation).**

| game | address | source | corroboration |
|---|---|---|---|
| BPEE | `0x02037318` | `pokeemerald.sym:241` | already in `GameProfile.mapHeader` and hardware-exercised by the phase-14 depth path |
| BPRE | `0x02036DFC` | `pokefirered.sym:198` **and** `pokefirered_rev1.sym:198` (identical) | the same file's `gObjectEvents 0x02036E38` is byte-identical to our already-hardware-exercised `mapObjects` |
| BPGE | `0x02036DFC` | `pokeleafgreen.sym:198` **and** `pokeleafgreen_rev1.sym:198` (identical) | same |

The user's FireRed is **rev1**, and both revisions agree, so the value is safe for their cartridge.
Note the trap the house rule caught: the plausible derivation `gObjectEvents − 0x38` (which holds
in Emerald) gives `0x02036E00` and is **wrong** for FRLG.

**T3.8 The warp-kind table** (the normative classifier). Behaviour values from
`include/constants/metatile_behaviors.h` in each repo; the `0x60–0x71` block is **not** shared
between the engines and a common table would be a bug:

| behaviour | Emerald (RSE) | kind | FireRed/LeafGreen | kind |
|---|---|---|---|---|
| `0x0E` | MOSSDEEP_GYM_WARP | STEP | — | — |
| `0x0F` | MT_PYRE_HOLE | STEP | — | — |
| `0x1B` | STAIRS_OUTSIDE_ABANDONED_SHIP | DIR **N** | — | — |
| `0x1C` | SHOAL_CAVE_ENTRANCE | DIR **S** | — | — |
| `0x29` | LAVARIDGE_GYM_B1F_WARP | STEP | — | — |
| `0x60` | NON_ANIMATED_DOOR | STEP | CAVE_DOOR | STEP |
| `0x61` | LADDER | STEP | LADDER | STEP |
| `0x62` | EAST_ARROW_WARP | DIR **E** | EAST_ARROW_WARP | DIR **E** |
| `0x63` | WEST_ARROW_WARP | DIR **W** | WEST_ARROW_WARP | DIR **W** |
| `0x64` | NORTH_ARROW_WARP | DIR **N** | NORTH_ARROW_WARP | DIR **N** |
| `0x65` | SOUTH_ARROW_WARP | DIR **S** | SOUTH_ARROW_WARP | DIR **S** |
| `0x66` | CRACKED_FLOOR_HOLE *(not a warp trigger)* | NONE | FALL_WARP | STEP |
| `0x67` | AQUA_HIDEOUT_WARP | STEP | REGULAR_WARP (warp pad) | STEP |
| `0x68` | LAVARIDGE_GYM_1F_WARP | STEP | LAVARIDGE_1F_WARP | STEP |
| **`0x69`** | **ANIMATED_DOOR** | **DOOR** | **WARP_DOOR** | **DOOR** |
| `0x6A` / `0x6B` | UP/DOWN_ESCALATOR | STEP | UP/DOWN_ESCALATOR | STEP |
| `0x6C` | WATER_DOOR | STEP | **UP_RIGHT_STAIR_WARP** | **DIR E** |
| `0x6D` | WATER_SOUTH_ARROW_WARP | DIR **S** | **UP_LEFT_STAIR_WARP** | **DIR W** |
| `0x6E` | DEEP_SOUTH_WARP | STEP | **DOWN_RIGHT_STAIR_WARP** | **DIR E** |
| `0x6F` | *(unused)* | NONE | **DOWN_LEFT_STAIR_WARP** | **DIR W** |
| `0x70` | BRIDGE_OVER_OCEAN *(reused as the Union Room exit,* `metatile_behavior.c:1142-1150`*)* | STEP | — | — |
| `0x71` | BRIDGE_OVER_POND_LOW | NONE | UNION_ROOM_WARP | STEP |

Everything not listed → **NONE** (today's behaviour). In particular `MB_PETALBURG_GYM_DOOR`
(EM `0x8D`), `MB_TRICK_HOUSE_PUZZLE_DOOR`, `MB_SKY_PILLAR_CLOSED_DOOR` (`0xEA`) are script doors,
**not** warp triggers, and must stay NONE.

**T3.9 Door tiles are impassable — measured, not assumed.** All 192 Emerald and 195 FireRed
`0x69` warp tiles read `collision == 1`; escalators, ladders, mats and arrow warps read
`collision == 0`. Three Emerald `0x69` tiles are passable (`coll=0`) — which is exactly why the
classifier must key on **behaviour, not collision**: those three still only warp on a north bump.

---

## T4. The fix

**T4.1 New profile field (do not reuse `mapHeader`).** Add `uint32_t mapHeaderPath;` to
`GameProfile` (append-only — `PROFILES[]` is positional-initialised): BPEE `0x02037318`, BPRE
`0x02036DFC`, BPGE `0x02036DFC`, all VERIFIED-SYM per T3.7. **Leave the existing `mapHeader` field
alone**, including its `0` for FR/LG: `main.c:885` gates the phase-14 HD-2D metatile-layer depth
path on `!p->mapHeader`, and filling it would silently switch on an untested 3D path for FireRed
and LeafGreen — a render change this phase is forbidden to make. Document the duplication; a later
phase may unify the two once the FR/LG depth path has its own hardware evidence.

**T4.2 New pure-C module `source/fieldpath.{c,h}`.** CLAUDE.md #4: the classifier and the planner
must be free of libctru/citro/mGBA headers so they dual-compile in `test/host/`. The module takes
a small read-callback vtable (`u8/u16/u32 read(void* ctx, uint32_t addr)`) plus the plan inputs,
and returns a plan struct; `touch.c` keeps only the gesture handling and the per-frame emit.
`touch.c` stays the caller, not the algorithm.

**T4.3 Warp classification** (`fieldpath_classify(engine, goalTile) -> WarpKind`):

1. `WK_NONE` unless the goal is inside the **current map's own bounds**
   (`0 <= x < backupW-15`, `0 <= y < backupH-14`) — outside those bounds the tile came from a map
   connection and its metatile id indexes the *wrong* tileset pair (the same limitation pret
   itself has; do not pretend otherwise).
2. `mid = block & 0x03FF`; `mid == 0x03FF` or `mid >= 1024` → `WK_NONE`.
3. Walk `mapHeaderPath +0x00 → MapLayout +0x10/+0x14 → Tileset` and read the attribute at the
   per-engine offset/stride/mask of T3.5. **Every pointer must be range-checked** (`>>24` in
   `{0x08,0x09}` for ROM, `0x02/0x03` rejected for tileset pointers) — a bad read must yield
   `WK_NONE`, never a wrong direction.
4. Map the behaviour through the **per-engine** table of T3.8. `engine` is derived from the
   already-present `code[]` (`BPEE` → RSE; `BPRE`/`BPGE` → FRLG).
5. *Confirmation only, optional:* if `MapEvents.warpCount` is sane (≤ 64) and no warp event
   matches the goal's x/y, downgrade DOOR/DIR/STEP to `WK_NONE`. This suppresses "door-looking
   tile with no warp" and yields the destination `mapGroup/mapNum` for the T6 proof. It must never
   *promote* a tile (T3.4).

**T4.4 Routing rules.** Let `G` be the tapped tile and `K = classify(G)`:

| K | terminal tile | search goal | after arrival |
|---|---|---|---|
| `WK_DOOR` | `A = (G.x, G.y+1)` | `A`, as a **normal walkable** goal | hold **NORTH** (T4.5) |
| `WK_DIR_d` | `G` | `G`, as a **normal walkable** goal | hold **d** |
| `WK_STEP` | `G` | `G`, normal walkable goal | nothing (the step warps) |
| `WK_NONE` | `G` | today's rule: blocked goal allowed as terminal | today's stall→A |

- If `K == WK_DOOR` and `A` is not enterable (T4.7) or not reachable → **fail: emit nothing**.
  Never fall back to walking into the door from the side; a documented no-op beats a wall tackle.
- If `K == WK_DIR_*` or `WK_STEP` and the goal reads impassable, treat it as `WK_NONE`
  (a contradiction between our tables and the live grid must not produce a confident wrong move).
- **Already on the approach tile** (`start == A`, and likewise `start == G` for `WK_DIR_*`): the
  plan is legal with **zero steps** and consists only of the terminal hold. Today
  `touch.c:345` ends such a route on frame 1, so this needs the explicit empty-path case.
  Note the interaction with tap-self (`touch.c:330`): a tap on **your own tile** stays the A/START
  gesture and is *not* re-interpreted as "leave via this mat" (Open Q3).
- The BFS neighbour order (T1.7) is **not** changed: with arrival constrained, the tie-break no
  longer decides anything that matters, and changing it would perturb every currently-working
  route.

**T4.5 Terminal hold semantics.** New route state `s_termDir` (0..3, −1 = none) and `s_termFrames`.
When the last path step completes (or immediately for a zero-step plan):

1. Emit `s_keyDir[s_termDir]` **continuously** — never pulsed. The game needs
   `heldDirection`/`heldDirection2` **and** `dpadDirection == playerDirection` (T3.1/T3.3), and
   the first 1–2 frames are spent turning the avatar.
2. Hold for at most `TERM_FRAMES = 30` frames (~0.5 s at 60 fps; scaled by nothing — it is an
   emulated-frame count at the injection seam, like `control.h`'s tap slots). Stop early on: the
   map changing (T4.6), or `px/py` changing (a `WK_DIR_*`/`WK_STEP` tile that moved us).
3. On timeout: cancel **silently**. **Do not fire the `s_aPulse`** (`touch.c:349`) at the end of a
   warp terminal — A on a door is at best a no-op and at worst opens a sign or an NPC dialogue.
   The stall→A behaviour is kept **only** for `WK_NONE` goals, which is what it was written for
   (signs, NPCs, cuttable trees).
4. `TERM_FRAMES` is **verify-on-hw-pending** in the same sense as `control.h`'s tap constants:
   30 frames must be re-counted at the real 60 fps and at the degraded frame rate of a wireless
   session before it is called done.

**T4.6 Cancel the route when the map changes.** Add `mapGroup`/`mapNum` to `TouchSmart`
(`touch.h:20-44`) — both already come out of `game_read` (`gamestate.c:120-121`), **no new
addresses**. `plan_bfs` records them; the follow loop aborts the whole route (path *and*
terminal) the moment either differs. Keep the existing `ptr/w/h` check as a second net. This is
what stops a route from continuing to drive the player around **inside** the building it just
entered.

**T4.7 Elevation-correct walkability (the biggest single win, T5.1).** `walkable()` becomes:

```
enterable(t) = t != MAPGRID_UNDEFINED
            && ((t & 0x0C00) >> 10) == 0
            && (pElev == 0 || tElev == 0 || tElev == 15 || tElev == pElev)
```

per pret `IsElevationMismatchAt` (pokeemerald `src/event_object_movement.c:7707-7723`;
`ELEVATION_TRANSITION = 0`, `ELEVATION_MULTI_LEVEL = 15`), where `tElev = (t & 0xF000) >> 12` —
**already in the `u16` we read, zero extra bus traffic** — and `pElev` =
`gObjectEvents[0] + 0x0B & 0x0F` (`currentElevation:4`, identical offset in both engines,
`global.fieldmap.h:0x0B`), one extra byte read per plan, from the `mapObjects` base that is
already in the profile and already hardware-exercised.

Safety rails (a wrong `pElev` would make the whole map unreachable):
- `mapObjects == 0` (game not mapped) → `pElev = 0` → the clause is a no-op = today's behaviour.
- If the **player's own tile** is not enterable under the rule, the read is self-inconsistent →
  fall back to `pElev = 0` for that plan and log it.
- Surfing is handled for free: `currentElevation` is the live avatar elevation, so a surfing
  player routes over water and not over land.

**T4.8 NPC re-read and one bounded replan** (T5.4/T5.5). When the follow loop detects a stall
(currently 24 frames → cancel+A), first **re-read the NPCs and replan once** toward the same
terminal, at most `REPLAN_MAX = 2` times per route. Only if that fails does the route end — and
for a warp route it ends silently (T4.5.3). A stall today presses A into whatever is in front,
which is how "the pathfinder talked to a random NPC" happens.

**T4.9 Cost.** `plan_bfs` already performs up to 4225 `busRead16` calls on the render thread per
**tap** (not per frame). The additions are +1 `read8` (player elevation), +4–5 reads for the
classification chain, +2+`warpCount` for the optional confirmation: **≤ ~20 extra reads per tap**,
i.e. under half a percent. The per-frame follow loop gains two integer comparisons. No new
allocation: the existing `static` arrays are reused, plus two ints of route state. Nothing moves
to a worker thread; nothing touches the GPU.

**T4.10 Diagnostics — do not ship this blind** (`docs/kb/touch-issues-todo.md` "Lesson", and the
phase-13 rule). Extend the existing touch-event log ring (`touch.c:466-566`) with a **plan row**
emitted once per planning attempt: `px,py, goalX,goalY, blockWord, behaviour, kind, approachX,
approachY, pathLen, firstDir, lastDir, termDir, pElev, result` where `result ∈ {planned,
unreachable, no-approach, unclassified, out-of-map, window}`. Plus a route-end row with the
outcome (`arrived / warped / stalled / replanned / mapchange / timeout`). The columns are the
difference between "the user says it bumped" and knowing *which* of the four candidate causes it
was.

**T4.11 A gdb-readable mirror for the harness** (needed by T6). Add one small
`g_fieldDbg` struct (LOGGING ONLY, updated in the same parked window the gs sampler already uses):
`{px, py, mapGroup, mapNum, goalX, goalY, kind, termDir, pathLen, planSeq, outcome}`. `planSeq`
increments per plan so `gdbio poll g_fieldDbg --changed` and `see rec --with-state` can both watch
it. This is the objective instrument; a screenshot cannot prove a warp fired.

**T4.12 Host suite (`test/host/test_fieldpath.c`, compile line in the file header).** Must cover:
the classifier for **every row of T3.8, per engine** (including the four `0x6C–0x6F` divergences —
a shared table must fail this test); the DOOR retarget on the extracted Mauville grid, from
(23,6), (22,6), (24,8) and (19,10); the zero-step plan; the "no walkable approach" refusal; the
`WK_DIR_S` mat plan; the elevation rule on the extracted Route-117 shoreline (must produce the
land detour, never the water crossing); the map-change abort; and the bounded replan. Fixtures are
literal arrays generated by `scratchpad/door/gen3map.py` (checked into
`test/host/fixtures_fieldpath.h` with a header comment naming map, group/num and the ROM the
bytes came from) so the host suite reproduces T2 exactly. The nine existing app suites and
`tools/emutest/tests/run_host_tests.sh` stay green.

---

## T5. Audit of the other "simple occasions"

**T5.1 Water — BROKEN, measured.** `walkable()` reads collision only, and Gen-3 water metatiles
are **collision 0**; they are impassable to a walking player purely by elevation (water is
elevation 1, land 3). Emerald group 0 (towns + routes) alone contains **18 240 `OCEAN_WATER` and
1 617 `POND_WATER` tiles with `collision == 0`**. Measured on Route 117: `plan_bfs` from the shore
tile (25,6) to (31,6) returns **`RRRRRR` — straight across five tiles of pond**. The player walks
to the shore, bumps, stalls 24 frames and presses A. With T4.7 the same query returns the land
detour `D R R D R R R R U U`. **Fixed by T4.7.**

**T5.2 Ledges — correct but limited.** All `MB_JUMP_*` tiles are `collision == 1` (measured: 99
JUMP_EAST, 133 JUMP_WEST, 314 JUMP_SOUTH in Emerald group 0), so the router already treats them as
walls: it never falls off one by accident, and it never uses one as a shortcut. Routes that
*require* a ledge hop simply are not found. **No change this phase; listed honestly.**

**T5.3 Tall grass — routed straight through.** `MB_TALL_GRASS`/`LONG_GRASS` are walkable, so a
route crosses grass and a wild encounter interrupts it. The route is correctly *dropped* (the
battle contexts call `walk_reset()`, `touch.c:590/600/608`) but nothing resumes afterwards. A
weighted search that prefers non-grass tiles is a Dijkstra change and is **out of scope**; listed.

**T5.4 NPCs that move — BROKEN, fixed cheaply.** NPCs are sampled once at plan time (T1.5). An NPC
stepping into the path stalls the route and then **presses A at it**. **Fixed by T4.8**
(re-read + one bounded replan; silent end for warp routes).

**T5.5 Goal blocked mid-route** — same mechanism, same fix (T4.8).

**T5.6 The stall→A side effect** (`touch.c:349`) fires at the end of *every* failed route,
including one that ended facing a sign, an NPC or a door. **Scoped to `WK_NONE` by T4.5.3.**

**T5.7 Surfing** works out of the box once elevation comes from the live avatar (T4.7) — no
special case, and it is worth stating because it is the one place where "water is walkable" is
correct.

**T5.8 Forced-movement tiles (ice, currents, spin tiles, muddy slopes)** are walkable and slide the
player, desyncing the follow loop into a stall. Classifying every tile would cost a behaviour
chain per BFS node (unaffordable). Cheap bounded mitigation, **optional this phase**: after a plan
is found, classify only the ≤64 tiles *on the path* and reject the plan if any is a
forced-movement behaviour. Listed, not required.

**T5.9 Map connections.** Tiles from a connected map are present in the backup grid and route fine,
but their metatile ids index the current map's tilesets, so **behaviour reads outside the current
map's bounds are unreliable** — pret has the identical limitation. T4.3.1 refuses to classify
there and falls back to today's behaviour.

**T5.10 Elevation-15 / multi-level tiles (bridges)** are handled by the pret rule in T4.7 (15 =
"compatible with anything").

**T5.11 The ±32 window** is far larger than the reachable tap area (±7 / ±5 tiles on screen); it
exists so a *detour* can be found. Keep it; measure its cost in T6.6.

**T5.12 Stale press-time player tile.** `s_downPx/Py` are latched on press (`touch.c:303`) but the
BFS starts from the *current* `px,py` (`:334`). If the player moves during the ≤12-frame tap
window (e.g. a previous route was still running), the goal is computed against a stale origin and
lands one tile off. Low frequency, but it is a candidate cause of the user's report; **require**
recomputing the goal from the *same* `px,py` the BFS starts from, or refusing to plan when the
player tile changed between press and release. Cheap; include.

---

## T6. Proof plan

Every case's verdict is a **state read**, never a screenshot. The instrument is `g_fieldDbg`
(T4.11) plus the game's own `SaveBlock1.location` mirrored into it.

**T6.1 Objective signals.**
- App-side, over gdb (`tools/emutest/run gdbio read-u32 g_fieldDbg+…`, `poll --changed`,
  `see rec --with-state g_fieldDbg`): `mapGroup`, `mapNum`, `px`, `py`, `kind`, `termDir`,
  `outcome`, `planSeq`.
- Game-side ground truth for the same values: `gSaveBlock1Ptr` (EM `0x03005D8C`, FR/LG
  `0x03005008`) → `+0x00 pos.x`, `+0x02 pos.y`, `+0x04 location.mapGroup`, `+0x05 location.mapNum`
  — the addresses `gamestate.c` already uses.
- **A warp is proven by `(mapGroup,mapNum)` changing**, which is exactly what a screenshot cannot
  show (a door animation without a warp looks similar for several frames).

**T6.2 Staging.** `azctl boot --gdb --fresh-sd-fixtures` (Tier B), `gdbio resume`, drive the ROM
picker into the dual-core session. Fixtures are copies; `dual-gba/` stays read-only; finish with
`azctl clean-fixtures` and **always** `azctl stop`. One Azahar at a time — coordinate with the
other phase-18 workstreams.

**T6.3 Getting into position.** The Emerald save starts in `MauvilleCity_PokemonCenter_2F` (10/6)
at (9,4) (T2.2), which conveniently sits above three different warp kinds. Use the phase-13 D4
control channel (`run sdmc arm-control`, `run sdmc drop move 1 "<script>"`, grammar in
`source/control.h`) to walk tile-exactly; every leg is verified by the map id changing:

| leg | script intent | expected `(group,num)` after |
|---|---|---|
| 2F → 1F | walk onto the **escalator** (1,6), `beh 0x6B` = STEP | (10,5) |
| 1F → city | walk onto the **exit mat** (6,8)/(7,8), `beh 0x65` = DIR **S**, then hold DOWN | (0,2) |
| city | walk to (22,6), in front of the Pokémon Center **door** (22,5), `beh 0x69` = DOOR | (0,2) |

**T6.4 Case matrix.** Each case: set up with a D4 script, then inject the touch with a synthesized
CTM (`run ctm make`), then read state.

| # | case | setup | action | PASS |
|---|---|---|---|---|
| 1 | **DOOR, from in front** | player (22,6) | tap the door tile | `g_fieldDbg.kind == DOOR`, `termDir == UP`, then `(0,2) → (10,5)` |
| 2 | **DOOR, from the side of the row** | player (24,6) | tap the door | path ends at (22,6), then the same warp |
| 3 | **DOOR, already on the approach tile** | player (22,6) | tap the door | `pathLen == 0`, warp still fires (the zero-step case, T4.4) |
| 4 | **DOOR, approach blocked** | park an NPC on (22,6) *or* choose a door whose south tile is occupied | tap the door | `outcome == no-approach`, **zero keys injected**, player does not move |
| 5 | **DIR S exit mat** | inside the PC, player (6,6) | tap (6,8) | `kind == DIR_S`, `(10,5) → (0,2)` — the case that is 100 % broken today |
| 6 | **STEP escalator/ladder** | 2F, player (3,6) | tap (1,6) | `(10,6) → (10,5)` with `termDir == none` |
| 7 | **FRLG directional stair** | gameB, any `0x6C–0x6F` tile | tap it | `kind == DIR_E`/`DIR_W` per T3.8, map changes |
| 8 | **water refusal** | Route 117 shore (25,6) | tap (31,6) | path contains **no** tile of elevation 1; either the land detour or `outcome == unreachable`. Regression proof for T5.1 |
| 9 | **NPC replan** | route past a moving NPC | tap a far tile | route completes, or ends with `outcome == replanned/stalled` — **never** an A press |
| 10 | **map-change abort** | trigger case 1 | — | after the warp, `planSeq` unchanged and **no** keys injected on the new map |
| 11 | **`WK_NONE` unchanged** | tap a sign / an NPC / a cuttable tree | — | today's behaviour bit-for-bit: route to it, stall, A |

**T6.5 Tap coordinate derivation.** The CTM touch point must be computed with the *same* mapping
the app uses (`main.c:684 touch_to_gba`, honouring the bottom-screen scale mode), from the GBA
pixel of the target tile: `gx = 16*(7 + Gx - px) + 8`, `gy = 16*(5 + Gy - py) + 8`. Case 3
doubles as the **(7,5) anchor check** (Open Q1): a tap computed for the player's own tile must
produce the tap-self A gesture, and a tap one row above it must classify as the door.

**T6.6 Cost measurement.** `see rec --with-state g_renderSeq` across a burst of taps: the render
sequence must not stutter when a plan runs. Record the worst frame time from the HUD in the run
dir.

**T6.7 Host suites first.** `test/host/test_fieldpath.c` (T4.12) must be green before any emulator
time is spent, and `bash tools/emutest/tests/run_host_tests.sh` must stay green after.

**T6.8 Hardware is still the gate.** CLAUDE.md #6: the terminal-hold length (T4.5.4) and anything
frame-rate dependent is **not done until it runs on the real New 3DS**. Azahar proves the logic
and the classification; it does not prove that 30 held frames is enough at the frame rate the app
actually achieves with two cores and a wireless session.

---

## Open Questions

**Q1. Is the (7,5) screen anchor exactly right?** Three places in our source assume it and none of
them was ever verified independently; `presence.h:54` already suspects the Y half by 8 px. If the
row is really 4, every tap resolves one tile too far north — which would land a "tap the door" on
the **wall above the door**, produce an unreachable goal, and could be the true cause of the user's
report. T6.5 case 3 settles it. **Do not build anything else on the anchor until it is measured.**

**Q2. Which of the four candidate causes produced the user's exact sentence?** The census (T2.5)
says a vanilla door cannot normally be approached from the side, so the candidates are: (a) the
anchor (Q1); (b) the user tapped the **upper** half of the two-tile door graphic; (c) an NPC on the
approach mat turning the plan into a detour; (d) the tile was not a `0x69` door at all (a Pokémon
Center counter, a cave mouth, a FRLG stair). The T4.10 plan row plus the T4.11 mirror identify it
on the first hardware run. **Ask the user which town/building, if a repro proves elusive.**

**Q3. Should tapping the mat you are standing on leave the building?** Today a tap on your own tile
is A / double-tap START (`touch.c:330`). Standing on an exit mat and tapping it is a natural
"leave" gesture, but overloading tap-self would break the interact gesture everywhere else. Left
unchanged in T4.4; needs a user decision.

**Q4. Ruby / Sapphire (AXVE / AXPE) have no profile at all** — the co-op workstream found the same
gap. Their metatile-behaviour constants are pokeruby's, **not** pokeemerald's, and must be read
from a byte-matched `pokeruby.sym` / behaviour header before the T3.8 RSE column is assumed to
apply. No RS row should be authored from an Emerald derivation.

**Q5. Should the optional warp-event confirmation (T4.3.5) ship in v1?** It costs `2 + warpCount`
reads and removes a class of false positives (door-looking tiles with no warp), but it also adds a
failure mode if `MapEvents` is mid-rebuild during a map load. Proposal: implement it behind a
compile-time gate, default **on** for Emerald (where `gMapHeader` is already hardware-exercised)
and **off** for FR/LG until their first successful hardware run.

**Q6. `TERM_FRAMES = 30`** is a guess of the same class as `control.h`'s tap constants. Needs a
frame count at 60 fps *and* at the ~4–5 emu-fps of a wireless session, where 30 emulated frames is
several real seconds of a held direction.

**Q7. Forced-movement tiles (T5.8)** — is the bounded path-only behaviour scan worth the extra
≤64 chain reads per plan, or is a stall on ice acceptable for now?
