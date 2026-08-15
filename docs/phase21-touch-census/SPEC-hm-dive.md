# SPEC — HM DIVE (and the FLASH verdict)

_Phase 26, lane V. Derivation + design + proof plan. Written 2026-08-15 against pret source read
this session: the local `pokeemerald` clone (`gba-toolkit/projects/PokeDNA/daycare map/pokeemerald`),
`pret/pokefirered` @ master (fetched and diffed against the cached extract in `/tmp/pret/fr`), and
`pret/pokeruby` @ master (fetched). **No emulator was booted** — phase 25 owns both Azahar
instances. The live proof (§7) is a deliberate follow-on._

**The user's point:** Gen 3 has **eight** HMs, and Waterfall and Dive should work by touch. They
are right, and the gap was bigger than it looked. Measured at the start of this phase:

| HM | State before phase 26 |
|---|---|
| Cut | PLANNED + live-proven (object edge) |
| Fly | works — but through a different system (the region-map tap-to-fly driver) |
| Surf | PLANNED + live-proven (metatile edge, mounts at the shore) |
| Strength | obstacle DETECTED only (`fieldtrav.c` `scan_edges`), never a planned edge |
| Flash | absent — **and it should stay absent; §5 settles it** |
| Rock Smash | PLANNED + live-proven (object edge) |
| Waterfall | STUB: enum + eligibility + a `FALLS >` HUD label, never planned (lane W) |
| **Dive** | **entirely absent — no enum, no move id, no eligibility, nothing** |

This document owns **Dive**, and settles **Flash**.

---

## 1. The architectural verdict, established before a line was written

**Dive is not a tile edge. It is not a warp event either. It is a MAP CONNECTION with an
IDENTITY coordinate map** — a third kind of map transition, and the only one in the engine whose
destination coordinate is not stored anywhere.

The chain, read top to bottom:

```
pokeemerald src/field_control_avatar.c:180   if (input->pressedAButton && TrySetupDiveDownScript())
                                    :153     if (input->pressedBButton && TrySetupDiveEmergeScript())
                                    :463-471 TrySetupDiveDownScript:
                                               FlagGet(FLAG_BADGE07_GET) && TrySetDiveWarp() == 2
                                               -> ScriptContext_SetupScript(EventScript_UseDive)
                                    :473-481 TrySetupDiveEmergeScript:
                                               FlagGet(FLAG_BADGE07_GET)
                                               && gMapHeader.mapType == MAP_TYPE_UNDERWATER
                                               && TrySetDiveWarp() == 1
                                    :965-983 TrySetDiveWarp:
                                               PlayerGetDestCoords(&x, &y)          <-- THE PLAYER'S
                                               MapGridGetMetatileBehaviorAt(x, y)       OWN TILE
                                               underwater && !IsUnableToEmerge -> SetDiveWarpEmerge
                                               IsDiveable                       -> SetDiveWarpDive
pokeemerald src/overworld.c:756-782  SetDiveWarp(dir, x, y):
                                       GetMapConnection(CONNECTION_DIVE | CONNECTION_EMERGE)
                                       -> SetWarpDestination(connection->mapGroup,
                                                             connection->mapNum,
                                                             WARP_ID_NONE, x, y)
```

Three things fall out of that, and all three shaped the design:

1. **The HM is used on the tile you are STANDING ON, not the tile you face.** `TrySetDiveWarp`
   calls `PlayerGetDestCoords`, where Surf/Cut/Rock Smash all go through
   `GetInFrontOfPlayerPosition`. So there is no FACE step, and a face step would be actively
   harmful — the tile next to a dive spot is water, so turning to face it would swim the player
   off the spot.
2. **The mechanism is `GetMapConnection`, not the warp table.** The destination is a whole map,
   named by the same `MapConnections` list that carries the north/south/east/west route seams.
   `WARP_ID_NONE` is passed for the warp id, and `connection->offset` is never read.
3. **The coordinate map is the IDENTITY.** `SetWarpDestination(..., x, y)` is handed the player's
   own `x, y`. You dive at (x,y); you arrive at (x,y). Surfacing is the same in reverse.

That is why this is **not** `fieldtrav_excursion` with a different constant. The phase-24
excursion machinery is keyed on warp *indices* and on `ft_warp_approach`'s door/arrow/step
terminal semantics; a dive has neither. What it does share is the **shape** — leave the map, cross
the other one, come back somewhere new — so the search reuses that shape with the warp pair
replaced by (a) any diveable tile reachable here and (b) any surfacing tile reachable there.

### 1.1 The two maps are dimension-aligned — measured, not assumed

Because the coordinate map is the identity, the pair only works if the two layouts are the same
size. Checked across every dive pair in pret's own layout data (`data/layouts/layouts.json` +
`data/maps/*/map.json` + the tilesets' `metatile_attributes.bin`, decoded this session):

| Pair | Dims agree | Surface dive spots (ROM) | Emergeable underwater tiles whose surface twin is surfable + collision-free |
|---|---|---|---|
| Route 105 | 40×80 ✓ | **0** | 25 (1 exception) |
| Route 124 | 80×80 ✓ | 188 | 188 (0) |
| Route 125 | 80×40 ✓ | **0** | 465 (1) |
| Route 126 | 80×80 ✓ | 1349 | 1350 (0) |
| Route 127 | 80×80 ✓ | 1036 | 1067 (2) |
| Route 128 | 120×40 ✓ | 491 | 491 (1) |
| Route 129 | 80×40 ✓ | **0** | 26 (0) |

- **Every one of the 3064 diveable surface tiles has a walkable underwater counterpart. Zero
  exceptions.** The identity map is sound in the direction that matters most.
- The five surfacing exceptions are all underwater **warp** tiles — behaviour `MB_NON_ANIMATED_DOOR`
  0x60 or `MB_WATER_DOOR` 0x6C, i.e. the doors into the Sealed Chamber / Marine Cave / Seafloor
  Cavern / Abandoned Ship. They are not surfacing spots, `fieldpath_classify` already owns them as
  warps, and the planner never picks them because their surface twin is not surfable.
- Every diveable surface tile is at **elevation 1** (the water layer) and every swimmable
  underwater tile is at **elevation 3, collision 0**, with every blocked one at collision 1.
  That is the measurement the underwater leg's movement rule rests on (§4.3).
- **Routes 105 / 125 / 129 carry no diveable tile in their shipped ROM layout at all.** Their dive
  spots are written at runtime by the abnormal-weather event: `Route105_OnLoad` →
  `AbnormalWeather_EventScript_PlaceTilesRoute105North` (`data/scripts/abnormal_weather.inc:41-49`)
  paints `METATILE_General_RoughDeepWater` into the map. **This is exactly why the dive spot is
  read from the LIVE `gBackupMapLayout` and never from ROM** — a ROM-only reader would declare
  Kyogre's lair undiveable.

### 1.2 The dive spots we deliberately refuse

`SetDiveWarp` has a second branch: when `GetMapConnection` returns NULL it runs
`RunOnDiveWarpMapScript()` and uses `sFixedDiveWarp`, a destination set by a map script's
`setdivewarp` command (`src/overworld.c:766-769`, `SetFixedDiveWarp` at :695). Fourteen Emerald
maps take that path — Sootopolis, the Sealed Chamber, Marine Cave, Seafloor Cavern, the Abandoned
Ship (×2) and Route 134 and their underwater twins.

**We do not interpret map scripts, so those spots are refused, not guessed** (`FT_OUT_NODIVE`).
Their destination is story-conditional (Sootopolis's changes with progress), so a static read
would be wrong rather than merely incomplete. Named degradation; the seven connection pairs above
are the whole supported set.

---

## 2. The constants, each cited at the line that uses it

Every value below is transcribed in `source/fieldtrav.c` at the line that consumes it. Nothing is
derived by arithmetic from another engine's number — that is the `c2a58db` rule (Ruby/Sapphire
spent months reading Emerald's badge offsets out of `SaveBlock1.vars[]`).

| Thing | Emerald (BPEE) | FRLG (BPRE/BPGE) | Ruby/Sapphire (AXVE/AXPE) |
|---|---|---|---|
| Badge gate | **0x86D** `FLAG_BADGE07_GET` — `flags.h:1348` (SYSTEM_FLAGS 0x860) + `:1365` (+0xD); gate at `field_control_avatar.c:465`/`:475` | **0** — no Dive (§3) | **0x80D** — pokeruby `flags.h:779` (SYSTEM_FLAGS 0x800) + `:795`; gate at pokeruby `field_control_avatar.c:521`/`:531` |
| Move id | `MOVE_DIVE` **291** — `include/constants/moves.h:295`; script check `data/scripts/field_move_scripts.inc:220` (`EventScript_UseDive`) and `:243` (`EventScript_UseDiveUnderwater`) | (id exists, unreachable) | same |
| Diveable behaviours | **0x11 / 0x12 / 0x14** — `metatile_behavior.c:853-861` `MetatileBehavior_IsDiveable` (MB_INTERIOR_DEEP_WATER / MB_DEEP_WATER / MB_SOOTOPOLIS_DEEP_WATER), values counted off `constants/metatile_behaviors.h` | none | **0x11 / 0x12 / 0x14** — pokeruby `metatile_behavior.c:927-935` under the older names MB_SEMI_DEEP_WATER / MB_UNUSED_DEEP_WATER / MB_SOOTOPOLIS_DEEP_WATER, values from pokeruby `metatile_behaviors.h:21/:22/:24` |
| Cannot surface | **0x19 / 0x2A** — `metatile_behavior.c:863-877` `MetatileBehavior_IsUnableToEmerge` (MB_NO_SURFACING / MB_SEAWEED_NO_SURFACING). Its `MB_WATER_DOOR` arm is `#ifdef BUGFIX`, which vanilla does **not** define — pret's own comment calls the vanilla behaviour "the dive glitch" — so it is **not** in our set: we target the shipped cartridge | none | **0x19 / 0x2A** — pokeruby `metatile_behavior.c:937-943` `MetatileBehavior_IsNotSurfacable`, values from pokeruby `metatile_behaviors.h:29/:46` |
| Connection direction ids | `CONNECTION_DIVE` **5**, `CONNECTION_EMERGE` **6** — `include/constants/global.h:153/154` | same values (`:125/126`) | same |
| `MAP_TYPE_UNDERWATER` | **5** — `include/constants/map_types.h:9` | same | pokeruby `:9` |
| `MapHeader` | `+0x0C connections`, `+0x17 mapType` — `include/global.fieldmap.h:176/182` | same block | pokeruby `global.fieldmap.h:148/154` |
| `MapConnections` | `+0x00 s32 count`, `+0x04 MapConnection*` — `global.fieldmap.h:165-169` | same | same |
| `MapConnection` | `+0x00 direction`, `+0x04 offset`, `+0x08 mapGroup`, `+0x09 mapNum`, **stride 12** | same | same |

> **The `MapConnection` layout came off the ASSEMBLER, not the C struct.** pokeruby's
> `global.fieldmap.h:129-135` annotates the fields `/*0x00*/ direction, /*0x01*/ u32 offset,
> /*0x05*/ mapGroup, /*0x06*/ mapNum` — those comments are **wrong**, because the compiler must
> 4-align `offset`. `pokeemerald asm/macros/map.inc:152-158` emits the record the ROM actually
> contains: `.byte direction ; .space 3 ; .4byte offset ; map (2 bytes) ; .space 2` = 12 bytes with
> mapGroup at +0x08. Mutation M9 (§8) is the regression test for exactly this trap.

**Note the badge off-by-one.** Dive is HM**08** and its gate is BADGE**07**. Read off the gate,
never off the HM number. (Waterfall is the mirror image: HM07, gated on BADGE08 in Emerald and
BADGE07 in FireRed.)

---

## 3. The FRLG verdict — Dive does not exist there

The brief said to confirm this from pokefirered rather than take it on faith. It is confirmed
**four independent ways**, against `pret/pokefirered` @ master fetched this session (and the fetched
`src/field_control_avatar.c` is byte-identical to the cached extract already in the tree):

1. **No hook.** `src/field_control_avatar.c:193-299` `ProcessPlayerFieldInput` contains no
   `TrySetupDiveDownScript` and no `TrySetupDiveEmergeScript`. Where pokeemerald's `:153` and
   `:180` call them, FireRed simply has neither call. A player can press A or B on any tile in the
   game and no dive script will ever run.
2. **Dead code.** `TrySetDiveWarp` (`:1143`) is declared `static` and has **zero** call sites in
   the file; `dive_warp` (`:1118`) has none either. Both are R/S leftovers the port never wired.
3. **pret says so in its own words.** `data/scripts/field_moves.inc:210` — the comment directly
   above the dive script block — reads `@ Unused leftover from R/S`.
4. **Nowhere to go.** FireRed has **no underwater map**: none of the 425 maps in
   `data/maps/map_groups.json` is one, so a `CONNECTION_DIVE` would have nothing to point at.

The trap this creates, and the reason a "close enough" value would be worse than nothing:
**FireRed's own `FLAG_BADGE07_GET` is 0x826 — which is `badgeWaterfall` in our FRLG row.** Putting
Emerald's 0x86D, or worse FRLG's 0x826, into `badgeDive` would make any FireRed save with the Soul
Badge read as dive-eligible. So `badgeDive` is **a named zero**, guarded separately in
`fieldtrav_usable` (`if (c->badgeDive && ...)`) because flag id 0 is a live TEMP flag
(`flags.h:11 TEMP_FLAGS_START 0x0`), not a hole — falling through would answer from a scratch
script flag. `fieldtrav_is_diveable` / `fieldtrav_can_emerge` refuse for FRLG for the same reason,
and note that pokefirered's `MetatileBehavior_IsDiveable` (`src/metatile_behavior.c:478-484`) still
*exists* and would answer TRUE for 0x11/0x12: the predicate survived the port, its callers did not.

Ruby and Sapphire, by contrast, **do** have Dive — hooked at pokeruby
`src/field_control_avatar.c:233` (B → emerge) and `:259` (A → dive) — so they get a real row.

---

## 4. The design

### 4.1 Where it sits

A fourth tier, below the three that already exist and consulted only after all of them decline:

```
tier 0   fieldpath_plan        plain dry walk                      (dry paths win, H1.7)
tier 1   fieldtrav_plan        conditional edges: Cut/Smash/Surf   (same map)
tier 2   fieldtrav_excursion   one out-and-back through a WARP     (the Lavaridge class)
tier 3   fieldtrav_dive        one out-and-back through a MAP CONNECTION   <-- this document
```

`fieldtrav_dive` does not enforce that precondition — the caller does — but it is written so that
being asked out of turn produces a refusal rather than nonsense: §4.5.

### 4.2 The search

```
0. eligibility: badge AND a party mon that knows MOVE_DIVE, through the shipped
   fieldtrav_usable rail. FRLG can never light the bit.        -> else FT_OUT_NOEDGE
1. direction:   FT_DIVE_UP iff gMapHeader.mapType == MAP_TYPE_UNDERWATER (the game's own
                emerge gate). On a surface map, require startSurfing.  -> else FT_OUT_NODIVE
2. the pair:    gMapHeader.connections -> the CONNECTION_DIVE (or _EMERGE) record -> (grp,num);
                resolve it through gMapGroups; then require the RETURN connection back to the
                current map.                                    -> else FT_OUT_NODIVE / BADMAP
3. leg 1:       BFS from the player over THIS map, in the mode the player is in.
   leg 3:       BFS from the GOAL over THIS map, same mode — one pass serves every candidate
                return tile.
4. candidates:  tiles leg 1 reached whose LIVE behaviour takes the HM (diveable going down,
                surfacing going up). Nearest first, capped at FT_DIVE_CAND = 8.
5. for each:    BFS the PAIRED map from the tile of the same name (identity!), find the tile
                that takes the HM back and whose home twin leg 3 reached; score
                out + mid + back, keep the best.                -> else FT_OUT_NODIVE
```

### 4.3 The two movement modes, and why the underwater leg is not "surf"

The excursion tier only ever walks. A dive route is swum, so `ft_dry_bfs` gained a mode:

- `FT_BFS_FOOT` — `fieldpath_enterable(pElev)`. Byte-for-byte the pre-phase-26 function; the three
  excursion call sites pass it and nothing about them changed.
- `FT_BFS_WATER` — surfable behaviour **and** collision, elevation disarmed. `surf_ok`'s rule
  verbatim. Used for every leg on a **surface** map.
- `FT_BFS_FOOT_BEH` — `FT_BFS_FOOT` plus a per-tile behaviour cache. Used for every leg on an
  **underwater** map.

The non-obvious half is the last one. Underwater tiles are **not** water behaviours — decoding
`Underwater_Route126` gives `MB_NORMAL` ×5694, `MB_SEAWEED` ×587, `MB_NO_SURFACING` ×70,
`MB_SEAWEED_NO_SURFACING` ×48 — so a surfable-set test would reject the entire map. What underwater
tiles *are* is collision 0 at elevation 3, which is the ordinary walk rule. Hence FOOT, with the
behaviour read kept only because the *surfacing* test needs it.

Elevation on the paired map's leg is disarmed (0 = `ELEVATION_TRANSITION`), the excursion tier's
own choice for a map we cannot see the player standing on. Measured harmless: no swimmable
underwater tile in any of the seven pairs sits at an elevation other than 3 (or the single
transition tile at 0), and every blocked one is collision 1.

### 4.4 The honest refusals

| Outcome | When |
|---|---|
| `FT_OUT_NOEDGE` | no badge, no mon, or the engine has no Dive (FRLG) |
| `FT_OUT_NODIVE` | not surfing on a surface map; no dive/emerge connection (**this covers every scripted spot**); the connection points at this map; no return connection; no reachable dive spot; no surfacing spot whose home twin reaches the goal; a goal the player could not occupy |
| `FT_OUT_WINDOW` | goal outside the ±32 search window |
| `FT_OUT_BADMAP` | `gMapGroups` not ROM, no `gMapHeader`, insane dimensions, the paired map fails its pointer/size rails |

Two of those deserve their reasons in writing:

- **The RETURN connection is required.** A dive we cannot undo would strand the route — and the
  player — on a map the tap never named. Both directions or nothing.
- **The goal must be occupiable.** A goal-rooted BFS seeds `dist[goal] = 0` without asking whether
  the goal can be stood on, so it would happily score a route that ends by bumping a rock. Tier 0
  allows exactly that (tapping a sign and walking into it *is* the interaction). A whole dive round
  trip that ends in a bump is not the same trade, so the goal is held to the rule the search
  expands by.

### 4.5 What the caller must still be told

`fieldtrav_dive` is pure and does not know about tiers, so it will answer a question it should
never have been asked. The one shape that could produce nonsense — "dive here and surface right
back here" — is refused explicitly (`ux == cx && uy == cy`), and the host suite proves it by asking
for a goal that plain surfing already reaches and requiring `FT_OUT_NODIVE`.

---

## 5. FLASH — settled: **not a traversal gate. Implement nothing.**

The question was whether Flash gates *movement* or only *visibility*. It is visibility, and the
decomp is unambiguous:

- **Flash is a party-menu field move, not an overworld interaction.** `SetUpFieldMove_Flash`
  (pokeemerald `src/fldeff_flash.c:72-91`) is one of the `SetUpFieldMove_*` handlers the Pokémon
  menu calls; it is gated on `gMapHeader.cave == TRUE && !FlagGet(FLAG_SYS_USE_FLASH)`. There is no
  A-press path to it at all. FireRed is identical (`src/fldeff_flash.c:164-175`).
- **Its entire effect is a scanline window radius.** `FldEff_UseFlash` (`:101-106`) sets
  `FLAG_SYS_USE_FLASH` and runs `EventScript_UseFlash`, which is three lines —
  `data/scripts/flash.inc:1-4`: `animateflash 1` / `setflashlevel 1` / `end`. `flashLevel` is then
  consumed by exactly two things: `WriteFlashScanlineEffectBuffer` (`src/field_screen_effect.c:985-991`,
  radii in `sFlashLevelToRadius` at `:53`) and the battle *transition* type
  (`src/battle_setup.c:704`).
- **The decisive negative:** `flashLevel` and `FLAG_SYS_USE_FLASH` appear **zero times** in
  `src/fieldmap.c`, `src/field_player_avatar.c` and `src/field_control_avatar.c` — the collision,
  movement and interaction paths (grepped this session; pokefirered `src/fieldmap.c` likewise: 0
  hits). `GetCollisionAtCoords` (`src/event_object_movement.c:4658-4672`) is the whole movement
  rule and Flash is not in it.

So a player in an unlit cave can walk every tile they could walk with Flash active; only the
visible radius changes. **Flash is not a movement gate, there is nothing for the traversal layer to
plan, and no code was written.** Auto-using it would be a gameplay-feel change (it burns a prompt
and a move-slot use) with no routing benefit — which was already the recommendation in
`SPEC-family-traversal.md` Open Q1 and is now a finding rather than a recommendation.

**One genuine near-miss, recorded so it is not rediscovered as a contradiction:** in *Emerald only*,
`SetUpFieldMove_Flash` opens with `ShouldDoBrailleRegisteelEffect()` (`fldeff_flash.c:76`) — using
Flash inside Registeel's tomb opens the door. That is a **puzzle interaction driven from the party
menu**, not a tile edge, and pret's own comment at `:73-74` notes Ruby/Sapphire use **Fly** for the
same puzzle. It belongs to a "use a field move on the current map" affordance if one is ever built;
it is not traversal.

---

## 6. What shipped in this lane, and what deliberately did not

**Shipped (pure, host-graded):**

- `FT_HM_DIVE`, `FT_MOVE_DIVE 291`, `badgeDive` (three rows), `FT_OUT_NODIVE` — all appended, so
  every existing enum value and every positionally-initialised table row keeps its number. A
  `_Static_assert(FT_HM_COUNT <= 8)` now guards the 3-bit edge tag packed into `s_meta`, so the
  next HM lands on a compile error rather than a wrong route.
- `fieldtrav_is_diveable` / `fieldtrav_can_emerge` — the allow-list and the deny-list, per variant.
- `fieldtrav_underwater` — `gMapHeader.mapType == MAP_TYPE_UNDERWATER`.
- `fieldtrav_connection` — the `MapConnections` walker, with its NULL/ROM/count/stride rails.
- `fieldtrav_dive` — the three-leg identity-mapped out-and-back.
- `fieldtrav_usable` gains the DIVE bit, guarded on `badgeDive != 0`.
- `ft_dry_bfs` gains its mode (the excursion call sites pass `FT_BFS_FOOT` and are unchanged).

**Deliberately NOT shipped: the EXECUTOR and the tier wiring.** `touch.c`, `progseq.{c,h}` and
`g_prefs` are untouched by this lane, so nothing in the app can produce an `FT_HM_DIVE` step yet.
The reason is the project's own rule (memory: *"finish/verify smart-touch on hardware one feature
at a time; stop blind batches"*) plus this phase's constraint: **no emulator was available**, and a
dive interaction is genuinely new sequencer behaviour that must be graded frame by frame before it
can be trusted:

| | Cut / Smash / Surf (shipped) | **Dive** |
|---|---|---|
| Target tile | the tile you FACE | **the tile you are ON** |
| Approach | walk adjacent, then FACE-hold | walk onto it, **no FACE step at all** — a face step would swim the player off the spot |
| Trigger | A | **A to dive, B to surface** (`field_control_avatar.c:153` vs `:180`) |
| Prompt | `MSGBOX_YESNO` → cursor row 0 + A | same (`field_move_scripts.inc:225`/`:248`) |
| Success gate | object slot deactivates / surf bit sets | **`SaveBlock1.location` changes** — a map boundary |
| After | replan on the same map | **re-localise on the new map**, exactly the excursion leg rule (H3.4) |

The last two rows are why the executor is the excursion **leg machine** (`excseq.c`: arm →
boundary-on-map-change → settle-until-layout-stable → replan live) with a dive INTERACT spliced
onto the end of legs 0 and 1 — and why it wants its own pure module and its own frame-by-frame
suite rather than a phase adding a case to `progseq`. That is the next slice; §7 is its acceptance
test, banked now.

---

## 7. LIVE PROOF PLAN (banked — for the executor slice)

Emulator first, hardware after (CLAUDE.md #6). Screenshots are evidence; RAM reads are proof.

**Step 0 — the fixture party knows no Dive.** Teaching HM08 is a precondition, not a step of the
proof. Prepare a BPEE save with: the **Mind Badge** (`FLAG_BADGE07_GET` 0x86D — verify with
`fieldtrav_flag_get`, do not assume the badge count), a party mon that knows **MOVE_DIVE 291** *and*
**MOVE_SURF 57**, and a position on Route 126. Bank which save was used in the run log.

- **PD1 — the dive itself, Route 126 (map (0,26) — confirm the group/num from `map_groups.json` at
  run time).** Surf onto the deep water and tap a spot across a rock formation the surface cannot
  reach. Expect: `progOutcome = FT_OUT_PLANNED`, `dir = FT_DIVE_DOWN`, `dGroup/dNum` = the
  `UNDERWATER_ROUTE126` ids, and the proof spine `SaveBlock1.location` (0,26) → the underwater map
  → (0,26). Read `gPlayerAvatar.flags` (BPEE 0x02037590): bit 3 SURFING before, **bit 4 UNDERWATER**
  (`PLAYER_AVATAR_FLAG_UNDERWATER`, `global.fieldmap.h:292`) after the dive, bit 3 again after the
  surface. Read `gMapHeader.mapType` (BPEE `mapHeaderPath` + 0x17) = 5 while under.
  **The identity check that makes this lane's whole claim falsifiable:** px/py at the frame before
  the dive must equal px/py at the frame after it. Same for the surface.
- **PD2 — the negative control.** The same tap on a save with the badge but **no** mon that knows
  Dive: `progUsable` has no DIVE bit, `progOutcome = FT_OUT_NOEDGE`, and the `inj` column shows
  **zero** keys injected. Then the mirror: a mon that knows Dive but no Mind Badge.
- **PD3 — the scripted refusal.** Stand on Sootopolis's dive spot (a `setdivewarp` map) and tap
  across. Expect `FT_OUT_NODIVE` and nothing injected — the honest refusal of §1.2, on the exact
  map that motivated it.
- **PD4 — the return trip.** From underwater, tap a surface destination reachable only by
  surfacing elsewhere: `dir = FT_DIVE_UP`, B-press not A, location series underwater → (0,26) →
  underwater.
- **PD5 — the abnormal-weather spot (optional, event-gated).** If the save has the Kyogre event
  live on Route 105, tap across it: the dive spot exists only as a runtime `setmetatile`, so this
  is the one scenario that proves the live-grid read (§1.1) rather than a ROM read.

**Hardware pass:** repeat PD1 and PD2 on the New 3DS and re-count the AWAIT budgets at real frame
rates (the TERM_FRAMES precedent). Not validated under a wireless session's degraded rate — same
VERIFY flag as every other traversal timing.

---

## 8. Gate

**Host suites: 17 binaries, 0 failures**, run from the committed tree.
`test_fieldtrav` went **1210 → 3397 checks** (this lane and lane W's Waterfall/Strength lane both
landed in it; the DIVE blocks are TEST 20 and TEST 21). `test_fieldpath` unchanged at 1808.
`make -j8` — see the lane log. **No emulator was booted and `make cia` was not run.**

TEST 21 brings its own bus, ROM image, live grid and save, so it needs no ROM on disk and cannot
collide with the other lane's blocks. The world is built so that every asserted step count is
forced rather than a tie broken by scan order: exactly one tile per side takes the HM.

**Mutation gate — 17 of 19 mutations bite:**

| # | Mutation | Failures |
|---|---|---|
| M1 | FRLG `badgeDive` 0 → 0x826 (its own badge07 = the Waterfall badge) | 3 |
| M2 | `fieldtrav_is_diveable` drops the FRLG refusal | 4 |
| M3 | `fieldtrav_can_emerge` loses its deny-list | 13 |
| M4 | the RETURN-connection requirement dropped | 1 |
| M5 | the identity coordinate map broken (arrive at 0,0) | 13 |
| M6 | the goal-occupiable check dropped | 1 |
| M7 | `FT_BFS_WATER` loses the surfable-behaviour test | 5 |
| M8 | `fieldtrav_usable` drops the `badgeDive != 0` guard | 1 |
| M9 | connection reader uses pokeruby's **wrong** C offsets (+5/+6) | 22 |
| M10 | `mapType` read at +0x16 (weather) instead of +0x17 | 9 |
| M11 | `FT_BFS_WATER` stops disarming elevation | **0** |
| M12 | the BFS root tile's behaviour is not cached | 1 |
| M13 | the dive-and-surface-in-place skip dropped | 1 |
| M14 | `MapConnection` stride 12 → 8 | 21 |
| M15 | connection reader stops scanning (takes record 0) | 28 |
| M16 | the `FT_MAX_CONN` sanity cap removed | 1 |
| M17 | the connections ROM-pointer guard removed | 1 |
| M18 | `startSurfing` not required on a surface map | 1 |
| M19 | the same-map connection guard removed | **0** |

Three of those were **added because the first pass did not bite**: M7 (a collision-free SANDBAR
across the west chamber, and a REEF on the upward route's surface leg — the surfable test is
otherwise indistinguishable from the collision bits), M14/M15 (a three-record connection list with
the dive record **last**, mirroring Route 124), and M17 (a well-formed decoy `MapConnections` in
EWRAM). M12 and M13 got their own degenerate-shape cases.

**The two that still do not bite, and why they stay:**

- **M11** — every `FT_BFS_WATER` caller passes `pElev = 0` by construction: a nonzero elevation
  only exists on the underwater legs, and those use `FT_BFS_FOOT_BEH`. The literal `0` states the
  disarm rule locally instead of inheriting it, and the rule itself is graded by `fieldtrav_plan`'s
  own `surf_ok` in TEST 9/14. Structurally unobservable, not untested.
- **M19** — a map whose dive connection points at itself is refused twice over: the guard catches
  it, and so does the RETURN-connection requirement (the same map has no `CONNECTION_EMERGE`).
  Making it bite would require inventing a map with a self-directed dive **and** a self-directed
  emerge, which no vanilla map has. Defence in depth, kept, and its redundancy recorded here rather
  than discovered later.
