# SPEC — touch family: TRAVERSAL (HM-aware routing + cross-map warp excursions)

_Phase 21/22 design spec. NO implementation in this document. Written 2026-08-14 (overnight S1
addendum lane) from the phase-18 fieldpath substrate + pret source (local pokeemerald clone at
`gba-toolkit/projects/PokeDNA/daycare map/pokeemerald`, pokefirered files fetched from pret
master 2026-08-14, sym maps re-read from this session's scratchpad). Desk work only: nothing was
booted, no source/ file was touched._

**The user's ask, verbatim (OVERNIGHT2.md addendum, given before sleep):**

> "if I touch a place in the overworld which requires a HM, such as a spot in a pond where i CAN
> go, or a place beyond some rocks or a tree, that the player auto does that as well (surfs,
> break brick, cut etc). A step beyond could be to include paths in the BFS algorithm through
> doors. A great example would be lavaridge town, where the jakuzi is seen from outside of it,
> but freely entered through the pokecenter, being able to simply touch that and the game
> understanding how to reach that place would be outstanding."

**It gets its own toggle** (user requirement; §4). **Games: BPEE first, BPRE rev1 second, BPGE
by the same tables (sym-derived, never executed). AXVE/AXPE deferred** — RS profiles are
walk-only, the rev-2 re-verification is still pending (OVERNIGHT2.md S1c), and RS's
`gPlayerParty` lives in IWRAM (0x03004360, `pokeruby_rev2.sym`), a different read path.

**Foundations this spec builds on, and does not re-litigate:**
- `source/fieldpath.{c,h}` — the phase-18 pure-C substrate: live-grid BFS (±32 window),
  collision+elevation walkability (`fieldpath_enterable`), warp classification with terminal
  semantics (DOOR/DIR/STEP + terminal hold), the warp kill-switch, NPC avoidance, `g_fieldDbg` +
  the fplog SD ring. Host-tested against the user's real ROM map data (1,808 checks).
- `source/touch.c` walk loop — tap/steer gestures, TERM_FRAMES hold, replan rule, staleness
  guards; `source/gamestate.{c,h}` — profiles, parked-window reads, `textDlg`/`yesNoTask`/
  `playerAvatar`/`partyCount`/`mapObjects` already exposed.
- `COVERAGE.md` §5 — the injection seam: per-frame additive key masks, holds first-class,
  write8/write16, bottom game only.
- The learn skill `references/gen3-pokemon-saves-and-licensing.md` — Gen-3 party-mon
  encryption (substruct order = personality % 24, XOR key = personality ^ otId).

**Seam verdict up front (the recurring family question): no injection-seam change is needed.**
Everything below is per-frame masks (walk keys, terminal holds, A/B pulses — all already
first-class), the shipped write-then-A idiom for the YES answer, and read-only RAM/ROM reads
through the existing `FpBus`/`gbacore_read*` seam. What IS new is a **route PROGRAM layer**
(multi-leg, closed-loop) above the single-leg walker — touch.c-local state, exactly like every
other family's handler state.

---

## 0. Design shape: from "a route" to "a route program"

Today a tap produces ONE leg: `fieldpath_plan` → follow → terminal hold → done/abort. This
family generalizes that to a **program**: an ordered list of steps, each either

- `LEG(map, goal, terminal)` — a fieldpath plan followed exactly as today, or
- `INTERACT(script)` — a per-HM closed-loop interaction (face, A, yes/no, await state change), or
- `AWAIT(predicate, budget)` — watch a gs/gamestate read until it flips or times out.

**H0.1** The program executor lives beside `walk_update` in touch.c; each step is driven
per-frame off the same reads the walker already uses. Between steps the executor re-reads the
live state and **re-plans the next leg from reality** — never from the plan's assumptions.
**H0.2** ANY of: unexpected map id, ctx leaving GCTX_OVERWORLD (battle, menu), a physical GBA
key press, a new touch, or a step budget expiring → the whole program aborts silently (§2.4).
The seam is additive (COVERAGE §5) — we cannot suppress user input, so we yield to it.
**H0.3** The conditional-edge planner + program builder is **pure C** (`fieldtrav.{c,h}`,
CLAUDE.md rule #4), reading through `FpBus`, host-tested like fieldpath — every table and
transition below is assertable off ROM-derived fixtures before an emulator ever runs.

---

## 1. T1 — the HM edge model

### 1.1 Two edge classes

| Class | Members | How the blocker is represented in-game | How WE read it |
|---|---|---|---|
| **Metatile edges** | Surf (water), Waterfall, Dive | metatile BEHAVIOUR of the tile | `fieldpath_behaviour_at` — already implemented and bounds-guarded |
| **Object edges** | Cut tree, Rock-Smash rock, Strength boulder | an OBJECT EVENT standing on the tile (its metatile is ordinary ground) | `gObjectEvents[i]` — the same table `read_npcs` (touch.c:328-338) already walks; add `graphicsId` u8 at **+0x05** (pokeemerald include/global.fieldmap.h struct ObjectEvent /\*0x05\*/ — identical offset block to the +0x0B/+0x10 fields fieldpath already uses) |

Object edges are why the phase-18 router sees trees as plain NPC blockers today: they are
active object events, collected into the NPC block list. The edge model REMOVES them from the
block list when (and only when) they are eligible edges (§1.4), and replaces them with a
conditional transition.

### 1.2 Metatile edge tables (per engine — separate tables, the fieldpath precedent)

**RSE surfable set** — pret pokeemerald `src/metatile_behavior.c:25-90`
(`sTileBitAttributes[...] & TILE_FLAG_SURFABLE`), numeric values re-derived from the
`include/constants/metatile_behaviors.h` enum this session:

| Value | Behaviour | Edge treatment |
|---|---|---|
| 0x10 | MB_POND_WATER | surf-enterable |
| 0x11 | MB_INTERIOR_DEEP_WATER | surf-enterable |
| 0x12 | MB_DEEP_WATER | surf-enterable (also the Dive trigger — deferred, §5) |
| 0x13 | MB_WATERFALL | **waterfall edge only** (§2.3.4) — surfable in the game's table but never a free surf tile for us |
| 0x14 | MB_SOOTOPOLIS_DEEP_WATER | surf-enterable |
| 0x15 | MB_OCEAN_WATER | surf-enterable |
| 0x19 | MB_NO_SURFACING | surf-enterable |
| 0x22 / 0x2A | MB_SEAWEED / MB_SEAWEED_NO_SURFACING | surf-enterable |
| 0x50-0x53 | MB_*_CURRENT | **EXCLUDED** — forced movement, the T5.8 rule (COVERAGE §3): the router must never enter tiles that move the player on their own |

**FRLG surfable set** — pret pokefirered `src/metatile_behavior.c:5-17`
(`sBehaviorSurfable[]`), values from `include/constants/metatile_behaviors.h` (fetched
2026-08-14): MB_POND_WATER 0x10, MB_FAST_WATER 0x11, MB_DEEP_WATER 0x12, MB_WATERFALL 0x13
(same waterfall-only rule), MB_OCEAN_WATER 0x15, MB_UNUSED_WATER 0x1A, MB_CYCLING_ROAD_WATER
0x1B, currents 0x50-0x53 (same EXCLUDED rule).

**H1.1** The tables live next to `kind_rse`/`kind_frlg` in the same two-table-never-merged
shape (the 0x60-block lesson, fieldpath.c:1-6). Warp classification takes **precedence**: a
tile that classifies as a warp kind (e.g. RSE 0x6C MB_WATER_DOOR, 0x6D water arrow) is a warp,
not a surf tile — `fieldpath_classify` runs first, unchanged.

### 1.3 Object edge detection

**H1.2** An object edge is an ACTIVE `gObjectEvents` slot (active bit +0x00 bit0, the read
`read_npcs` already does) whose `graphicsId` (+0x05) matches the per-engine id:

| Edge | BPEE (pokeemerald include/constants/event_objects.h:89-94) | BPRE/BPGE (pokefirered event_objects.h:101-103, fetched) |
|---|---|---|
| Cut tree | OBJ_EVENT_GFX_CUTTABLE_TREE = **82** | OBJ_EVENT_GFX_CUT_TREE = **95** |
| Rock-Smash rock | OBJ_EVENT_GFX_BREAKABLE_ROCK = **86** | OBJ_EVENT_GFX_ROCK_SMASH_ROCK = **96** |
| Strength boulder | OBJ_EVENT_GFX_PUSHABLE_BOULDER = **87** | OBJ_EVENT_GFX_PUSHABLE_BOULDER = **97** |

The graphicsId IS the binding: every such object's template carries the matching field-move
script (verified across map.json data: e.g. every CUTTABLE_TREE on Route 103/116 carries
`EventScript_CutTree`). We do NOT read the ROM script pointer in v1 — the gfx id is
sufficient and engine-checked; the ROM template chain (MapHeader→events→objectEvents→script
+0x10, struct ObjectEventTemplate) is named here as the escalation path if a live run ever
finds a mismatched object.

**H1.3** Position comes from `currentCoords` (+0x10/+0x12, grid +7 space — as read_npcs).
For a boulder this matters: it may have been pushed off its template spot; the LIVE coords are
authoritative. Berry trees (OBJ_EVENT_GFX_BERRY_TREE) and everything else stay plain blockers.

### 1.4 Eligibility: badge flag + a party mon that knows the move

The game's own gates, which we mirror EXACTLY (never prompt what the game will refuse):

| Edge | The game's gate (citations) | Badge flag id EM | Badge flag id FRLG |
|---|---|---|---|
| Surf | `FLAG_BADGE05_GET && PartyHasMonWithSurf() && IsPlayerFacingSurfableFishableWater()` — pokeemerald src/field_control_avatar.c:450 == pokefirered :605 | 0x86B | 0x824 |
| Waterfall | badge + `IsPlayerSurfingNorth()`; the script itself re-checks `checkpartymove MOVE_WATERFALL` — EM field_control_avatar.c:453-458 + data/scripts/field_move_scripts.inc; FR :608-613 (**BADGE07**, not 08) | 0x86E (BADGE08) | 0x826 (BADGE07) |
| Cut | `goto_if_unset FLAG_BADGE0x_GET` + `checkpartymove MOVE_CUT` — EM field_move_scripts.inc:1-16 (BADGE01); FR data/scripts/field_moves.inc:1-16 (**BADGE02**) | 0x867 | 0x821 |
| Rock Smash | same shape — EM BADGE03 (field_move_scripts.inc:62-75); FR **BADGE06** (field_moves.inc:59-74) | 0x869 | 0x825 |
| Strength | badge + once-per-map-load latch `FLAG_SYS_USE_STRENGTH` — EM field_move_scripts.inc:119-137 (BADGE04); FR field_moves.inc:119-132 (BADGE04) | 0x86A / latch **0x889** | 0x823 / latch **0x805** |

Flag-id derivations: EM `SYSTEM_FLAGS = 0x860` (flags.h:1348), badges = +0x7..0xE
(flags.h:1359-1366), FLAG_SYS_USE_STRENGTH = +0x29 (flags.h:1399). FR `SYS_FLAGS = 0x800`
(flags.h:1324), badges = +0x20..0x27 (flags.h:1364-1371), strength latch = +0x5 (flags.h:1332).

**H1.4 The badge read.** `FlagGet(f)` = bit `f&7` of `SaveBlock1.flags[f>>3]`:
`read8(sb1 + flagsOff + (f>>3)) >> (f&7) & 1`, where `sb1` is the (deref'd per `sbDirect`)
`sb1ptr` the profiles already expose, and `flagsOff` is an engine constant:
**EM 0x1270** (pokeemerald include/global.h:1020 `/*0x1270*/ u8 flags[NUM_FLAG_BYTES]`),
**FRLG 0x0EE0** (pokefirered include/global.h:790 — from the LOCAL partial FR checkout,
`PokeDNA/reference/pokefirered`). Same guarded-deref rails as every sb1 read in gamestate.c.

**H1.5 The party read.** New profile field `partyBase` (positional append, the only safe edit —
gamestate.h:93-95): **EM 0x020244EC** (`pokeemerald.sym gPlayerParty`, size 0x258 = 6×100),
**FR 0x02024284** (rev0 AND rev1 agree — both maps re-read this session), **LG 0x02024284**
(rev0 = rev1, LG's own maps). Per mon `i` at `partyBase + 100*i`, `i < partyCount` (profile
field already shipped):
- `personality` u32 +0, `otId` u32 +4 (BoxPokemon, pokemon.h:196-199);
- **egg exclusion without decryption**: byte +19 bit 2 (`isEgg:1`, pokemon.h BoxPokemon
  bitfield — the unencrypted mirror of the substruct3 bit the game's
  `ScrCmd_checkpartymove` checks via `MON_DATA_IS_EGG`, src/scrcmd.c). `hasSpecies` = bit 1 of
  the same byte replaces the species!=0 check.
- **moves**: encrypted block +32..79 = 4 × 12-byte substructs, order = the standard
  permutation table indexed `personality % 24`, each u32 XOR `personality ^ otId`
  (src/pokemon.c GetSubstruct/decryption; algorithm documented in the learn skill
  `gen3-pokemon-saves-and-licensing.md`). The **Attacks substruct** (type 1) holds
  `moves[4]` u16 at its +0..7. Compare against MOVE_CUT 15, MOVE_SURF 57, MOVE_STRENGTH 70,
  MOVE_WATERFALL 127, MOVE_ROCK_SMASH 249 (+ MOVE_FLASH 148, MOVE_FLY 19, MOVE_DIVE 291 for
  completeness — pokeemerald include/constants/moves.h:19-295; move ids are
  engine-invariant).
- Decrypt-integrity rail: verify the mon's `checksum` (+28, u16 = sum of the 24 decrypted
  u16s) before trusting the moves; a mismatch = that mon is ineligible, never a garbage move
  read. Surf extra rule: `PartyHasMonWithSurf` returns FALSE while already surfing
  (field_player_avatar.c:1297-1305) — irrelevant to us because a mid-water tap plans in the
  surf layer, not a new mount.

**H1.6** Eligibility is evaluated ONCE per plan (at tap time) and re-checked once at each
INTERACT step's start — it cannot change mid-route except by exotic means (trade/deposit mid
route is impossible while the program owns the overworld; a re-check is still cheap honesty).

### 1.5 Cost model: dry paths win

**H1.7** Planning runs in **activation tiers**: tier 0 = today's BFS, no conditional edges
(bit-identical to shipped behaviour). If tier 0 reaches the goal, that plan SHIPS — a wet or
destructive detour must never displace a walkable route ("prefer dry paths when equal" and
also when longer: tier order, not step count, is the primary key). Tier 1 = allow conditional
edges, breadth-first over a **layered state space (tile, mode)**, mode ∈ {FOOT, SURF}:
- FOOT→FOOT: `fieldpath_enterable` (unchanged);
- FOOT→SURF: mount edge — target behaviour ∈ surfable set, collision bits 0, eligibility
  (Surf) holds; this edge costs an INTERACT (§2.3.3);
- SURF→SURF: behaviour ∈ surfable set, collision 0 (the foot elevation rule does not apply on
  water — water is elevation 1 vs land 3, exactly what keeps tier 0 dry);
- SURF→FOOT: dismount edge — target passes `fieldpath_enterable` with the PRE-SURF foot
  elevation; free (the game auto-hops ashore on movement, §2.3.3);
- FOOT→FOOT *through an object edge*: the object's tile becomes enterable iff eligible; costs
  an INTERACT (Cut / Rock Smash; Strength excluded from v1 pathing, §5).
**H1.8** Among tier-1 plans, minimize (a) number of INTERACTs, then (b) steps — implemented as
iterative deepening on INTERACT count (≤ 2 per program, a hard cap: a route needing three HM
activations is out of v1 scope and fails honestly).
**H1.9** The tapped GOAL may itself be a surfable tile ("a spot in a pond where I CAN go" —
the user's own example): a SURF-mode terminal is legal; the program simply ends afloat.

### 1.6 Emerald vs FRLG differences (the summary the implementer reads first)

- Badge mapping differs for Cut (01 vs 02), Rock Smash (03 vs 06), Waterfall (08 vs 07);
  Surf and Strength agree (05 / 04). Flag bases and flags-array offsets differ (§1.4).
- Gfx ids differ (82/86/87 vs 95/96/97).
- FRLG has **no obtainable Dive** (7 HMs; `MetatileBehavior_IsDiveable` exists in the engine,
  field_control_avatar.c:1130, but no Kanto dive spots) — Dive is EM-only and deferred anyway.
- FRLG scripts open with `goto_if_questlog` (field_moves.inc:2) — during a quest-log replay
  scripts bail early; our H0.2 abort-on-anything-unexpected covers it.
- FRLG surfable set includes FAST_WATER 0x11 / CYCLING_ROAD_WATER 0x1B; RSE's includes the
  Sootopolis/seaweed family. The two tables must not merge (H1.1).

---

## 2. T2 — auto-use sequencing (closed-loop, never frame-count-blind)

### 2.1 The observables (all already exposed by gamestate/gbacore reads)

| Signal | Read | Meaning in a program |
|---|---|---|
| `textDlg` | `fieldMsgMode != 0` (GameState.textDlg, gamestate.h:199) | a field textbox is up — the script is talking |
| yes/no up | `yesNoTask` active → ctx GCTX_FIELDMENU (gamestate.c:329-331) | the MSGBOX_YESNO decision point |
| object gone | the tracked `gObjectEvents` slot's active bit clears, or its graphicsId no longer matches | `removeobject` fired = Cut/Smash SUCCEEDED |
| surfing | `playerAvatar` (+0x00 flags) bit 3 PLAYER_AVATAR_FLAG_SURFING (global.fieldmap.h:291); profile field shipped in phase 20 | mount/dismount proof |
| map id | SaveBlock1.location (mapG/mapN — shipped) | leg boundary / abort tripwire |
| position | px/py + objX/objY (shipped) | step progress, arrival |
| ctx | GCTX_* | battle/menu intrusion = abort |

### 2.2 The INTERACT step, generic shape

1. **FACE**: the leg ends on the approach tile with a terminal hold TOWARD the obstacle
   (the existing terminal-hold machinery, touch.c:465-477; the obstacle tile is impassable so
   the hold bumps in place, which is the game's own turn-to-face). Hold FACE_FRAMES ≈ 8
   (VERIFY-ON-EMULATOR, same status as TERM_FRAMES) then release.
2. **A** (3-frame pulse, the shipped `s_aPulse` shape) → **AWAIT textDlg == true**, budget ~90
   frames. The eligibility pre-check (H1.6) is what makes this dialog OURS — we never A-press
   at an object we did not verify by graphicsId + eligibility.
3. **AWAIT yes/no**: ctx == GCTX_FIELDMENU with `yesNoTask` live, budget ~120 frames (the
   want-to-use text can be two boxes; A-advance while `textDlg && !yesNoTask`).
4. **ANSWER YES**: the SHIPPED fmenu path — write `sMenu.cursorPos = 0` + A
   (`fmenu_select`, touch.c:547-555; YES is row 0 in both engines' yes-no menus).
5. **AWAIT the state change** (per-HM below), budget ~300 frames (cutscene length,
   VERIFY-ON-EMULATOR). A-advance any lingering textboxes (`Text_MonUsedFieldMove`).
6. **REPLAN** the next leg from live state.

If step 3 never sees a yes/no (we interacted with something unexpected despite the checks): A
once more to close any textbox, then **abort the whole program** — never answer YES to a
prompt we did not predict. That is this family's core safety property.

### 2.3 Per-HM specifics

**2.3.1 Cut** (EM field_move_scripts.inc:1-55; FR field_moves.inc:1-57): generic shape;
success = the tree's slot deactivates (`removeobject VAR_LAST_TALKED`). The tile is then
plain ground — replan walks through it. Quirk: trees regrow on map reload only; within the
program's life the tile stays open.

**2.3.2 Rock Smash** (EM :62-118; FR :59-117): generic shape; success = slot deactivates.
Quirk: **a wild encounter can fire after the smash** (EM `RockSmashWildEncounter`,
FR `special CheckForRockSmashEncounter`? — FR path re-verified at implementation; both
engines can battle here). ctx flips to battle → H0.2 aborts the program. v1 does NOT resume
after the battle (consistent with the T5.3 tall-grass precedent); the user re-taps.

**2.3.3 Surf mount / dismount** (EM data/scripts/surf.inc; gate field_control_avatar.c:450):
approach = the land tile adjacent to the first water tile of the crossing, face the water
(bump — water is foot-impassable), A → prompt → YES → `FLDEFF_USE_SURF` cutscene. Success
gate = playerAvatar flags bit 3 SETS; then the program continues its water leg (planned in
SURF mode). Dismount needs NO interaction: the surf-layer leg simply steps onto the shore
tile and the game hops off (success gate = bit 3 CLEARS). Never plan a mount while already
surfing (H1.5's game rule).

**2.3.4 Waterfall** (EM :453-458 + field_move_scripts.inc:185-210; FR :608-613): only
reachable mid-surf, directly below the fall, facing NORTH (`IsPlayerSurfingNorth`). Edge =
vertical: (tile below fall, SURF) → (tile above fall, SURF). Sequence: arrive below, hold UP
(face), A → prompt → YES → forced ascent. Success gate = py decreased past the fall rows.
DOWNWARD travel over a waterfall is NOT modeled in v1 (whether Gen-3 descent is free or
blocked is unverified — VERIFY-ON-EMULATOR before any down-edge ships).

**2.3.5 Strength** (EM :119-183; FR :119-155) — **activation + push are two different
things.** Activation is a normal INTERACT at the boulder (prompt → YES →
`FLAG_SYS_USE_STRENGTH` sets — readable, ids in §1.4). The latch is **per map load**: pret
clears it in the overworld map-load paths (pokeemerald src/overworld.c:374-405), so a program
must re-read the FLAG, never assume "already activated this session"; if set, interacting
again just prints the already-active text (script routes to `EventScript_CheckActivatedBoulder`)
— so the correct program SKIPS the interact when the flag reads 1. Pushing = walking into the
boulder (one tile per bump, the boulder's `currentCoords` advance is the per-push success
gate). Path planning through boulders is a Sokoban problem and is **deferred** (§5); see Open
Q4.

### 2.4 Timeouts and aborts (H0.2 made precise)

- Every AWAIT carries a frame budget (numbers above; all VERIFY-ON-EMULATOR, then re-counted
  on hardware — the TERM_FRAMES precedent, touch.c:246-250).
- Abort triggers: budget expiry; unexpected map id; ctx ∉ {OVERWORLD, FIELDMENU-with-yesNo
  during step 3-4}; any physical GBA key edge; any new touch (which may then aim a fresh tap);
  eligibility re-check failure; the toggle turning off.
- Abort behaviour: inject NOTHING further (at most one A to close a dangling textbox in the
  step-3 failure case), clear program state, log the end reason. The player is left wherever
  the program got to — visible, honest, recoverable (§4 HUD says STOPPED).
- Everything logs: extend the fplog entry (touch.c:302-325) with `progSeq/step/hm` columns and
  mirror the same into `g_fieldDbg` for the gdb channel — the phase-18 instrumentation
  pattern, one ring, one dump.

---

## 3. T3 — cross-map routing (the Lavaridge class)

### 3.1 Where the warp graph lives

- **Current map**: warps from RAM via `gMapHeader → events → warps` — ALREADY implemented and
  bounds-guarded (`warp_at`, fieldpath.c:158-177; profile field `mapHeaderPath`).
- **Neighbour maps are NOT in RAM** — their headers, warp lists and layouts must come from
  ROM: `gMapGroups[group][num] → struct MapHeader` (pret Overworld_GetMapHeaderByGroupAndId).
  New profile field **`mapGroupsRom`** (positional append), sym-derived this session:

| Game | gMapGroups | Note |
|---|---|---|
| BPEE | 0x08486578 | pokeemerald.sym |
| BPRE rev0 | 0x083526A8 | pokefirered.sym |
| BPRE **rev1** | **0x08352718** | pokefirered_rev1.sym — **differs from rev0; the user's cart is rev1** (the exact trap the house rule exists for) |
| BPGE rev0 / rev1 | 0x08352688 / 0x083526F8 | pokeleafgreen[_rev1].sym — LG's own maps |
| AXVE/AXPE | 0x083085A0 (ruby rev2) | recorded for completeness; RS deferred |

  Reads through the ROM chain use the same `rom_ptr()` + sane-count guards as `warp_at`
  (warpCount ≤ 64 exists; add group/num bounds from the destination header's own validity —
  a bad pointer anywhere degrades to "no excursion", never a wander).
- **Neighbour connectivity** (is the interior crossable?) comes from the destination map's ROM
  `mapLayout → map` grid — the DEFAULT layout: u16 words in the same
  metatile/collision/elevation format the live grid uses, so `fieldpath_enterable`'s rule
  applies verbatim through a ROM-grid bus adapter. **Honesty limits, stated:** the ROM grid
  does not see runtime changes (opened Rusturf walls, moved boulders) and has NO object
  events (NPCs invisible at plan time) — so plan-time connectivity is optimistic-but-static,
  and each leg re-plans on the LIVE grid at arrival (H3.4), where NPCs exist again.

### 3.2 Bounding the search

**H3.1** The tapped tile is on the CURRENT map by construction (a tap is a screen offset from
the player, touch.c:426). Therefore every useful excursion RETURNS to the current map, and the
v1 search is exactly **one out-and-back excursion** (depth 2 warps):
1. Enumerate warps `Wi` on the current map reachable by a tier-0/1 plan (§1.5).
2. For each `Wi` → destination map `D` (group,num,warpId from the warp event — dest_warp_id
   indexes D's OWN warp table; arrival tile = that warp's x,y).
3. Enumerate D's warps `Wj` whose destination is the CURRENT map; require ROM-grid
   connectivity arrival(Wi) → approach(Wj) inside D (approach per `fieldpath_classify`'s
   kind rules, run against D's ROM grid + tilesets).
4. Require current-map connectivity arrival(Wj) → goal.
5. Pick the program with fewest INTERACTs, then fewest warps, then fewest total steps.
**H3.2** Hard caps: ≤ 8 candidate Wi (nearest-first), ≤ 64 warps scanned per map (existing
guard), ≤ 2 warp legs, ≤ 2 INTERACTs, one ROM-grid BFS window per candidate D (maps are small;
the BFS reuses the FP_WBOX arrays — render-thread cost bounded to plan time exactly like
today's replans). Excursion search runs ONLY if tier 0/1 same-map planning failed — same-map
routes never regress.

### 3.3 Leg execution and re-localization

**H3.3** A warp leg is EXACTLY today's warp route: plan to `Wi` with its classified terminal
(door hold / arrow hold / step). The existing map-change kill-switch (touch.c:458-463) becomes
the **leg boundary**: when SaveBlock1.location flips, compare against the EXPECTED (group,num)
— match → re-read px/py (re-localization; the arrival should be `Wi`'s destination warp tile,
tolerate ±1 for door step-off), plan the next leg on the NEW live RAM grid; mismatch → abort.
**H3.4** Arrival replanning is mandatory (never execute a ROM-planned path blind): the interior
leg is re-planned live, which picks up NPCs, runtime layout changes, and the correct warp
terminal kinds. If the live replan fails (an NPC camps the doorway), abort honestly — the
player is left at the arrival point, HUD says STOPPED (§4), no wandering.
**H3.5** Failure honesty (the T3 ask, verbatim): warp graph incomplete / any ROM read fails
validation / no candidate survives → **plan NOTHING** — the tap behaves exactly as an
unreachable tap does today (outcome logged FP_OUT_UNREACHABLE-equivalent with a new
`excursion-none` reason). Never a partial program, never "walk toward the door and hope".

### 3.4 The canonical scenario, traced with real ids (pret pokeemerald map data, read this
session from `data/maps/*/map.json` + `map_groups.json`)

Lavaridge Town = **map (0,12)**; Pokemon Center 1F = **(4,5)** (gMapGroup_IndoorLavaridge).
The hot spring sits on the NE terrace at elevation 3, fenced from the town floor — exactly
the user's "seen from outside, entered through the pokecenter".

| Town (0,12) warps | PC 1F (4,5) warps |
|---|---|
| 3: (9,6) → PC warp 0 — the FRONT door | 0: (7,8) → town warp 3 (front, left leaf) |
| 5: (9,2) elev 3 → PC warp 3 — the BACK door mouth on the terrace | 1: (6,8) → town warp 3 (front, right leaf) |
| (0,1,2,4: herb shop / gym / mart / house) | 3: (2,1) → town warp 5 — the BACK door |

Expected program for a tap on the spring (around (10..13, 2..4)):
1. tier 0/1 fail (terrace unreachable on foot from town floor — elevation + fence);
2. excursion search: Wi = town warp 3 at (9,6), a DOOR (classified live: approach (9,7),
   terminal hold UP) → arrive PC (7,8);
3. ROM connectivity inside (4,5): (7,8) → warp 3 at (2,1) (its live classification decides
   the terminal — expected DOOR/STEP on the north wall);
4. arrive town warp 5 at (9,2), ON the terrace (elev 3);
5. final leg (9,2) → the tapped spring tile.
Legs: [town: walk (9,7), hold U] → [PC: walk to (2,1)'s approach, terminal per kind] →
[town-terrace: walk to goal]. Map-id series (0,12) → (4,5) → (0,12) is the proof spine (§6).

**H3.6** Non-goals in this tier: map CONNECTIONS (seamless route edges — blocked today by the
±32 window + the T5.9 neighbour-tileset refusal), >1 excursion, Fly, Dive transitions.

---

## 4. T4 — the toggle + UX

**H4.1 Setting.** `g_prefs` gains `int smartTraverse` (UiPrefs append, theme.h:43-59), values
**0 = Off (ship default), 1 = HM (current-map edges), 2 = HM + Via (excursions)** — the two
slices are user-visible tiers, and default-Off follows the tiltLevel precedent for
hardware-unproven features (theme.h:57). Persisted via the size-tolerant Settings append
(main.c:2372-2412 pattern: files that end before the field load fine, the field defaults 0).
Settings row: the TOUCH group, a PK_SEG 3-state "HM Routes: Off / HM / HM+Via", label
typography per the phase-19 roles (TXT_BODY row + TXT_VALUE state, the existing rows' shape).
Only consulted in SMART touch mode; PAD/OFF unaffected.

**H4.2 HUD intent.** While a program runs, a **route chip** (the `touch_chip` idiom,
touch.c:53-57, TXT_CHIP + `theme_on_scrim` ink) appears beside the mode chip showing the
program's intent, updating per step:
- leg to an edge: `CUT >` / `SMASH >` / `SURF >` / `FALLS >` (the HM verb the user is about
  to see used);
- warp legs: `VIA DOOR - LEG 1/3` (map-NAME lookup from ROM region-map strings is deferred;
  the leg counter is the honest v1);
- terminal states, held ~1 s: `DONE` / `STOPPED`.
The chip is visible from plan time — the user sees "SURF >" BEFORE the walk-to-shore starts,
which is the no-preview compromise (Open Q2).

**H4.3 Cancel.** Any new tap while a program runs CANCELS it and does NOT aim a new route
(first tap stops, second tap re-aims — mis-taps during a long program must not redirect it);
a hold steers (which already cancels, touch.c:416-417); any physical key cancels (H0.2). The
route chip's own rect is also a tap target = STOP.

---

## 5. T5 — scope cuts, plainly

**Slice 1 — HM edges on the CURRENT map (EM + FR):** the fieldtrav layered planner (FOOT/SURF)
+ object edges, eligibility reads (badge + decrypted party moves), INTERACT sequencer for
**Cut, Rock Smash, Surf mount/dismount**, tier-0-first cost rule, toggle value 1, HUD chip,
logs. Host suite for tables/eligibility/planner; emulator proof P1-P3 (§6).

**Slice 2 — single-warp excursions + Waterfall:** `mapGroupsRom` plumbing, ROM-grid bus +
connectivity BFS, the out-and-back search (H3.1), leg re-localization, toggle value 2,
Waterfall up-edge (cheap once the SURF layer exists). Lavaridge is the acceptance test (P4).

**Deferred, with reasons:**
- **Strength beyond activation** — pushing is Sokoban: reachability depends on boulder-push
  sequences, mis-planning can soft-lock a puzzle until re-entry. Activation-only ships when a
  route is blocked by an UNPUSHED boulder position... which is itself rare; so Strength ships
  as: eligibility + activation INTERACT only when the user taps the boulder ITSELF (explicit
  intent), no path planning through boulders. (Open Q4.)
- **Dive** — EM-only, two-level map pairs via `TrySetDiveWarp` (field_control_avatar.c:463+),
  a different warp mechanism + a second goal-space; no FRLG counterpart. Later tier.
- **Fly** — not an edge; a party-menu teleport with its own UI. Different family entirely.
- **Multi-warp chains / map connections** — search-space and window-refusal work (T5.9) that
  the Lavaridge class does not need.
- **Battle resume after a Rock-Smash encounter / grass encounter** — the T5.3 precedent
  stands; programs die on ctx change.
- **RS/LG execution** — RS: rev-2 verification pending + IWRAM party base; LG: no ROM on this
  machine. Tables and addresses are recorded above so both light up by profile append alone.

---

## 6. T6 — proof plan (emulator first, hardware sign-off after — CLAUDE.md #6)

All scenarios drive the emutest gdb channel against `g_fieldDbg` (+ the new prog columns) and
the game's own RAM — the phase-18 doctrine: screenshots are evidence, RAM reads are proof.
Save prep note: proofs need a save with the right badges/moves; the census saves may not
qualify — prep is "load the user's late-game save" or a learn-skill-built .sav (bank which
was used in the run log).

**Slice 1:**
- **P1 — Surf, Route 103 (0,18):** stand on the south bank, tap a north-bank tile across the
  pond. Observe: plan rows (tier 1, one INTERACT-SURF); `gPlayerAvatar.flags` bit 3
  (EM 0x02037590+0) 0→1 at mount, →0 at the far shore; px/py series crossing tiles whose
  behaviour ∈ §1.2; arrival == tap. Negative controls: badge-less/Surf-less save plans
  NOTHING (no prompt ever fires); toggle 0 reproduces today's UNREACHABLE.
- **P2 — Cut, Route 116 (0,31):** the tree at (21,6) (Rustboro side; alternates (24,9),
  (28,11) — all carry EventScript_CutTree in map.json, verified). Tap the far side; observe
  the tracked slot (graphicsId 82) active→0, then the route completes THROUGH the tree tile;
  fplog shows FACE→A→YESNO→YES→OBJGONE→REPLAN with no D-pad leaks during dialogs.
- **P3 — Rock Smash, Rusturf Tunnel (24,4):** rocks at (24,4)/(24,5) (graphicsId 86,
  map.json). Same spine as P2 + the encounter branch: force/await a smash encounter run →
  program aborts on ctx=battle, nothing injected during the battle (the tlog `inj` column is
  the proof).
- Justification of spots: all three are early-map, minimal-story-gate, and each isolates ONE
  edge type; Route 103's pond is the smallest surf crossing adjacent to a fresh-game area,
  Route 116/Rusturf pair Cut and Rock Smash within one short trip.

**Slice 2:**
- **P4 — Lavaridge (0,12), the headline:** tap the spring from the town floor. Proof spine:
  `planSeq`/prog rows showing the 3-leg program; SaveBlock1.location series
  (0,12)→(4,5)→(0,12) via `poll --changed`; final px/py at the goal; the four-capture set for
  the user (town tap with chip "VIA DOOR - LEG 1/3", PC interior transit, terrace arrival,
  standing in the spring).
- **P5 — Waterfall:** EM Route 119's waterfall (the standard mid-game one) mid-surf; observe
  the up-edge program + py drop across the fall rows. (Exact tile bank recorded at
  implementation from map.json, same method as above.)
- **P6 — excursion honesty:** tap a genuinely unreachable tile (fenced with no returning
  warp pair) → excursion search logs `excursion-none`, ZERO keys injected.

**Hardware pass (the done gate):** repeat P1/P2/P4 on the New 3DS; re-count FACE/AWAIT budgets
at real frame rates (and note they are NOT validated under a wireless session's degraded rate
— traversal during a link session inherits the same VERIFY flag as TERM_FRAMES, SPEC-door
Open Q6).

---

## 7. Open questions for the user

1. **Flash?** Flash is not a movement edge — it lights dark caves via a one-time script.
   Auto-triggering it on entering a dark map is a gameplay-feel call (it also burns a move
   slot use and a prompt). Recommendation: leave Flash OUT of auto-use entirely; say if you
   want a "tap-self in a dark cave offers Flash" affordance instead.
2. **Route preview:** should a multi-leg or HM route show its intent and wait for a CONFIRM
   tap before moving (one extra tap, zero surprises), or execute immediately with the HUD chip
   as the only signal (current spec: immediate + chip)? Cheap to flip either way.
3. **Toggle default:** spec ships Off (hardware-unproven precedent). Happy to default it to
   HM+Via once it survives its first hardware session?
4. **Strength ambition:** is "activate on tapping the boulder, you push manually" enough, or
   do you want push-path planning (straight-line pushes only) in a later slice? Full boulder
   puzzles (Sokoban) I recommend never.
5. **Resume after battle:** if a Rock-Smash (or grass) encounter interrupts a program, should
   the route resume after the battle ends (needs battle-exit detection + a staleness rule), or
   stay dead as spec'd?
6. **Waterfall descent:** worth modeling the downward edge if the emulator shows Gen 3 lets
   you ride down without the HM? (v1 models UP only.)
7. **Dive (Emerald):** how much do you care? It is the one edge that changes MAPS underneath
   you (two-level pairs), so it rides the excursion machinery — doable as a slice 3, but only
   if you actually play those areas.
