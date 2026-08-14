# LANE A1 (phase 24) — proving the HM-traversal touch feature end to end

Instance **a** (gdb 24689, `runs/`), on the user's own Emerald save as fixed by commit `af3bb50`
(`roms/emerald-fix.sav` — party carries FLY, SURF, CUT, ROCK SMASH; 8 badges). One Azahar boot
hosts BOTH games (`--stage-roms emerald-fix,emerald-fix`); touch drives the **bottom** game =
seat 2 = `move_p2.txt` (settings.bin `swapped = 0`).

Targets: **P1 SURF** (end to end, including the game's own YES) · **P2 CUT** · **P3 ROCK SMASH** ·
**P4 LAVARIDGE HOT SPRING** (the cross-map excursion).

Verdicts are recorded per target at the end of each section: PROVEN / FAILED / BLOCKED.

---

## Entry 0 — desk recon, banked before the first boot

Method: host-side only — the phase-23 recon rail (`savepeek.py`, `mapdump.py`, `reach.py`,
recovered from this project's scratchpad) plus the local pret pokeemerald clone
(`gba-toolkit/projects/PokeDNA/daycare map/pokeemerald`). Nothing booted, no source touched.

**The fixture save, read on the host** (`savepeek.py roms/emerald-fix.sav EM`, saveIndex 1963):

| Gate | Reading |
|---|---|
| Position | **(13,8) on map (26,28)** = `BattleFrontier_BattleArenaLobby` (indoors — Fly is illegal here) |
| Badges | flags 0x867..0x86E **all 1** — every HM gate open |
| Party | 6 mons, all checksum-OK: Tyranitar · Salamence · Dragonite · Milotic (SURF) · **Lugia (FLY, SURF)** · **Zigzagoon (ROCK SMASH, CUT, SURF)** |
| HM set | **CUT, FLY, ROCK_SMASH, SURF** — P1/P2/P3 all planable, and Fly unlocks P4's map |

**Where each proof can happen** (pret map data, this session):

| Target | Map | Tile | Why this one |
|---|---|---|---|
| P1 SURF | `BattleFrontier_OutsideEast` (26,14) | shore (47,58), ocean `MB_OCEAN_WATER 0x15` at x=48..56 | the only land-adjacent surfable water reachable from the save (phase-23 Entry 6, re-confirmed) |
| P2 CUT | `Route117` | `OBJ_EVENT_GFX_CUTTABLE_TREE` at **(15,2)** | 15 tiles east of the Verdanturf connection; Verdanturf fly point is (16,4) |
| P3 SMASH | `Route111` | `OBJ_EVENT_GFX_BREAKABLE_ROCK` at **(18,101)/(19,100)** | Mauville fly point (22,6), Route111 joins Mauville's north edge (offset 0, Route111 y=139) |
| P4 SPRING | `LavaridgeTown` (fly point **(9,7)**) | spring block x=3..6, y=3..5 | see below |

Every cut tree / breakable rock in Emerald is flagged `FLAG_TEMP_*` (cleared on map load), so
**P2/P3 are repeatable**: re-entering the map restores the obstacle. (The Rusturf Tunnel rocks are
the exception — `FLAG_HIDE_*`, permanently gone on an 8-badge save, so they are not a target.)

**P4 is exactly the canonical shape, confirmed off the map data:**

- Lavaridge fly lands at **(9,7)**, one tile below the PC front door **(9,6)** (town warp 3).
- `LavaridgeTown_PokemonCenter_1F` warp 3 at **(2,1)** returns to town warp 5 = **(9,2)** — the
  back door on the north terrace.
- The spring block (x 3..6, y 3..5) touches the terrace only through (6,2)→(6,1)→…→(9,1)→(9,2);
  row y=6 is solid wall under it, so **the spring is unreachable from the town on foot** — the
  dry router must fail, and only an out-and-back through the PC can succeed.
- Both old women stand at (4,4) and (5,4) (they block), so the goal tile is **(5,5)** —
  ddx −4 / ddy −2 from the fly tile, inside the ±7/±5 tap window.

**Consequence for the arc order:** P1 needs no Fly (walk from the lobby, phase-23's proven route);
P2/P3/P4 each start with a Fly, which is a menu arc driven by bare-direction D4 tokens (region-map
cursor: BATTLE FRONTIER is grid (22,12), LAVARIDGE (5,3), VERDANTURF (4,6), MAUVILLE (8,6)).

---

## P1 — SURF, end to end: **PROVEN**

Boot `runs/20260814-143803`, dual Emerald from `roms/emerald-shore.sav`, instance a.
One tap. Nothing else was sent to the game between the tap and the screenshot.

**The act:** `t 256 128 6 30` — a tap on the ocean six tiles east of the player
(tap geometry `screen = (40 + (7+ddx)*16 + 8, 40 + (5+ddy)*16 + 8)`, ddx +6 → world (53,58),
`MB_OCEAN_WATER 0x15`).

**What the game's own state said** (`g_fieldDbg` over gdb, one line per read, ~5 s apart):

| read | ctx | pos | SURF | prog |
|---|---|---|---|---|
| before | OVERWORLD | (47,58) | **0** | seq=0 |
| +5 s | OVERWORLD | (47,58) | 0 | seq=1 **PLANNED** moves=6 inter=1 hm=**SURF** phase=**5 (ANSWER)** aKeys=12 **answers=2** |
| +10 s | OVERWORLD | (47,58) | **1** | phase=6 (DONE) aKeys=22 answers=2 |
| +15 s | OVERWORLD | **(49,58)** | 1 | end=**HANDOFF** — the mount consumed the step, the replan handed the rest to the shipped walker (`walking=1`) |
| +20 s | OVERWORLD | **(53,58)** | **1** | plan seq=3 goal=(53,58) beh=0x15 end=ARRIVED |

`SURF` is the game's own `gPlayerAvatar` bit 3 (PLAYER_AVATAR_FLAG_SURFING) and (53,58) is a
water tile with elevation 1 — a walking player cannot stand there. Both had to be true, and both
are read off the game, not off a picture.

**`answers=2` is the load-bearing number.** It says the executor aimed at the YES twice: the first
press fell inside `Task_HandleYesNoInput`'s five dead frames and was discarded, the second landed.
Before the fix that first press was the ONLY one, which is exactly why the phase-23 run — and this
lane's first run — left the prompt open with YES highlighted forever.

Captures: `evidence/impl/EM-P24-P1a-shore-arrival.bottom.png` (the walked approach),
`EM-P24-P1b-surf-mounted.bottom.png` (the game's own "Would you like to SURF?" with ▶YES),
`EM-P24-P1c-surfing-on-the-tapped-tile.bottom.png` (afloat, on the tapped tile).

**Verdict: PROVEN** — plan, walk, face, A, the game's own YES, the mount, and the ride to the
tapped tile, from one tap, with the state read at every stage.

### The defect this target found (fixed, commit `2dde03e`)

`Task_HandleYesNoInput` (pokeemerald src/script_menu.c) ignores input for its first five frames;
our ctx flips to `GCTX_FIELDMENU` the instant that task exists, so the single A pulse was always
spent inside the dead window. The retry that should have covered it was gated on `textDlg`, and
`sFieldMessageBoxMode` returns to HIDDEN as soon as the text finishes printing — false for the
whole "box up, waiting for A" window. Fix: level-triggered ANSWER (cursor held on YES, a fresh
edge every 8 frames, capped at 8) + textDlg-free advances in YESNO/DONE.

---

## P2 — CUT, tap past the tree: **PROVEN**

Boot `runs/20260814-152731`, from `roms/emerald-r117.sav` (saved in game, by touch, standing at
Route 117 **(15,3)** with the cuttable tree at (15,2) still up).

**Why this tree.** A tap-past-a-tree proof is worthless if the router can walk around: tier 0 wins
by design and no HM is ever planned. `gate.py` (host recon, this session) answers it per obstacle —
removing Route 117's `OBJ_EVENT_GFX_CUTTABLE_TREE (15,2)` opens exactly **11 tiles**
(x 8..15, y 1..2), and the goal **(11,2)** is one of them. So the tree is the only way in.

**The act:** `t 96 112 6 30` — ddx −4 / ddy −1 from (15,3) → world (11,2).

| read | ctx | pos | prog |
|---|---|---|---|
| before | OVERWORLD | (15,3) | seq=0, face=3 (L) |
| +6 s | OVERWORLD | (15,3) | seq=1 **PLANNED** moves=5 inter=1 hm=**CUT** **edges=1** phase=4 (YESNO) aKeys=12 |
| +12 s | OVERWORLD | (15,3) | phase=6 (DONE) aKeys=25 **answers=2** |
| +18 s | OVERWORLD | **(11,2)** | end=**HANDOFF** — the tree gone, the rest of the route walked by the shipped walker |

`edges=1` is fieldtrav finding the tree as a conditional edge by its graphicsId; `answers=2` is the
level-triggered YES again; and **(11,2) is the proof**: with the tree standing that tile is
unreachable under the shipped walkability rule, so standing on it means the Cut really happened and
the route really continued through the opened tile — which is exactly what the target asked for.

Captures: `EM-P24-P2a-tree-standing.bottom.png` (the saved state this boot loaded, tree up),
`EM-P24-P2c-through-the-gap.bottom.png` (in the pocket, the tree tile now empty).

**Verdict: PROVEN.**

### Two defects this target found (both fixed)

1. `b0ae8d9` — one A at the object is not enough either; TPH_DLG now retries on a cadence.
2. `70f963d` — **the real one**: TPH_FACE held the direction for a fixed 8 frames, and Gen 3 only
   reads field input while the avatar is idle, so a hold that starts during the last walk step is
   swallowed whole and the avatar keeps facing the way it walked. Every A after that is aimed one
   tile off. FACE is now closed-loop on the game's own `gObjectEvents[0].facingDirection`, and
   TPH_A settles 10 released frames before pressing. New instrument: `progFacing` (+0xA8).

### Two operational facts worth keeping (they cost this lane ~40 minutes)

- **A cut tree does NOT come back when you reload a save made on the same map.** The obstacle's
  `FLAG_TEMP_11` is only cleared by a map LOAD (`ClearTempFieldEventData`, overworld.c:798/848), and
  continuing a save on the same map does not do one. Walk out of the map and back in — then the
  tree is up again (proven both ways this session).
- **Route 117 (15,4) is a static Lass** (`MOVEMENT_TYPE_FACE_RIGHT`), permanently blocking the
  column below the tree. A hand-written `d6` down that column times out forever; the planner has to
  route around her. Every walk script here should come out of `plan24.py` (which reads the map's own
  object events), not out of a mental picture of the map.

---

## P4 — LAVARIDGE HOT SPRING, the cross-map excursion: **PROVEN** (and it works in both directions)

Boot `runs/20260814-160325`, from `roms/emerald-lavaridge.sav` (saved in game, by touch, on the
fly tile). `smartTraverse = 2` (HM+Via).

**The premise, verified on the host first** (`gate.py`/flood over the real map data): from the fly
tile (9,7), **157 tiles** of Lavaridge are reachable on foot and **(5,5) is not one of them** — the
spring block (x 3..6, y 3..5) has solid wall under it. From the PC's back-door tile (9,2) it is one
of 16. So no dry route can exist, and the only way in is out-and-back through the Pokemon Center.

**The act:** `t 96 96 6 30` — ddx −4 / ddy −2 from (9,7) → world (5,5), a hot-spring tile.

**What the game's own state said**, one line per read (~6 s apart), nothing else sent:

| read | map | pos | note |
|---|---|---|---|
| tap | (0,12) Lavaridge | (9,7) | on the town floor |
| +6 s | **(4,5) PC 1F** | (7,8) | walked to the front door (9,6) and warped IN — leg 0 |
| +12 s | (4,5) | (5,8) | crossing the room, `walking=1` |
| +18 s | (4,5) | (2,4) | …still crossing — leg 1 planned on the interior's live grid |
| +24 s | (4,5) | **(2,1)** | the PC's BACK door |
| +30 s | **(0,12)** | (8,1) | warped OUT onto the north terrace, already walking — leg 2 |
| +36 s | (0,12) | **(5,5)** | **in the hot spring**, `plan goal=(5,5) beh=0x28 outcome=PLANNED end=ARRIVED`, `progMapSeq=2` |

**Then the same tap in reverse**: from (5,5), a tap on the town's (9,7) ran the whole excursion the
other way — back door → across the PC → front door → (9,7) — as `progSeq=2`, `mapSeq=2`. The
machine is not tuned to one direction.

Captures: `EM-P24-P4a-lavaridge-arrival.bottom.png` (the town, with the spring fenced off behind
the Pokemon Center), `EM-P24-P4b-inside-the-pokemon-center.bottom.png` (mid-excursion, inside the
PC), `EM-P24-P4d-in-the-hot-spring.bottom.png` (**standing in the spring beside the two bathers**),
`EM-P24-P4e-back-in-town.bottom.png` (the reverse trip).

**Verdict: PROVEN** — the user's own example ("the jakuzi is seen from outside… touch that and the
game understands how to reach it") works, from one tap, with every leg boundary read off the game.

### Four defects this target found — the excursion tier had never once run

| # | Fix | What it was |
|---|---|---|
| 1 | `e11b620` | `fieldtrav_excursion` demanded `ft_rom_ptr(m->mapHeader)`, but gMapHeader is an **EWRAM struct** — every excursion on real hardware returned BADMAP before reading a warp. The host fixture handed it the ROM header, so the suite could not see it; TEST 16 now serves the header at the EWRAM address the console uses (and fails 4 checks against the old line). |
| 2 | `e1c6041` | The next leg was planned on the frame `SaveBlock1.location` changed. A Gen-3 warp writes the location when it STARTS, so the plan read a half-built world, failed, and killed the excursion one door short. Boundaries now ARM a leg; the follower plans it when the world answers. |
| 3 | `a1cbdca` | The boundary detector sat inside the path-follow block. The last leg's terminal is a STEP warp — the walker finishes (`s_walking = false`) BEFORE the warp fires — so the second boundary was never seen. The watcher now runs unconditionally against the map the current leg is walked on. |
| 4 | `2fa4976` | Matching the location is not the world being loaded: leg 2 got planned on the Pokemon Center's 14x9 grid while the town's 20x20 was still loading, and died as MAPCHANGE one frame later. The settle rule now also requires the layout DIMENSIONS to be stable, and a layout-killed leg re-arms instead of ending the trip. |

---

## P3 — ROCK SMASH, tap past the rock: **PROVEN**

Same boot as P4 (`runs/20260814-160325`), reached by Fly (Lavaridge → Mauville, by touch through
the party menu + the region-map cursor) and a 57-step D4 walk north out of Mauville onto Route 111.

**Why this rock.** `gate.py`: removing `OBJ_EVENT_GFX_BREAKABLE_ROCK (18,101)` opens **1973 tiles**
— it is the rock that seals the whole north of Route 111 — while its neighbour (19,100) opens
nothing (there is a walk-around). The goal **(18,99)** is on the far side of the gate.

**The act:** `t 160 80 6 30` — ddx 0 / ddy −3 from (18,102) → world (18,99).

| read | pos | prog |
|---|---|---|
| before | (18,102) | seq=0 |
| +6 s | (18,102) | seq=1 **PLANNED** moves=3 inter=1 hm=**SMASH** **edges=2** phase=5 (ANSWER) aKeys=11 answers=1 |
| +12 s | (18,102) | phase=6 (DONE) aKeys=25 **answers=2** |
| +18 s | **(18,99)** | end=**HANDOFF**, **edges=1** |

Two independent confirmations in one trace: the player is standing north of a gate that only Rock
Smash opens, and the live conditional-edge count fell **2 → 1** — the game's own object slot for
that rock went inactive, which is what `prog_obj_active` waits on.

Capture: `EM-P24-P3c-past-the-rock.bottom.png` (through the gap; the second, walk-around rock is
still there beside the player).

**Verdict: PROVEN.**

---

## Lane close-out

### Verdicts

| target | verdict | the state read that proves it |
|---|---|---|
| **P1 SURF** | **PROVEN** | `progSurf` 0→1 (the game's own `gPlayerAvatar` surf bit) + the player standing on (53,58), an elevation-1 ocean tile |
| **P2 CUT** | **PROVEN** | the player standing on (11,2), one of the 11 tiles the Route 117 tree gates |
| **P3 ROCK SMASH** | **PROVEN** | the player standing on (18,99) past a rock that gates 1973 tiles, and `progEdges` 2→1 (the rock's own object slot went inactive) |
| **P4 LAVARIDGE HOT SPRING** | **PROVEN** | the whole leg trace: (0,12)(9,7) → (4,5)(7,8) → (4,5)(2,1) → (0,12)(8,1) → (0,12)(5,5), `progMapSeq=2`, and the same tap works in reverse |

Every target failed at least once first, and every failure was diagnosed off the game's own state or
its own source before anything was changed. **Six defects were found and fixed**, all committed
with the run that exposed them: `2dde03e` (the YES fell inside the yes/no's five dead frames),
`b0ae8d9` (one A at the object is not enough), `70f963d` (the A was aimed by a frame count instead
of the avatar's facing), `e11b620` (excursions rejected their own EWRAM map header — the tier had
never run), `e1c6041` (a leg planned on the frame the warp starts), `a1cbdca` (a boundary the
follow loop could not see), `2fa4976` (a leg planned on the previous map's grid).

One theme runs through all of them: **a Gen-3 field script is a LEVEL, not an edge.** Every fix was
"stop pressing once and hoping; watch the game's own state and keep acting until it answers".

### Host gate at close (fresh runs, this tree)

celiolink PASS · control **6940** · diag 376 · **fieldpath 1808 (frozen, unchanged)** ·
**fieldtrav 1099** · netlink PASS · peersprite 62078 · presence 61376 · profiles 1697 ·
theme 83444 · tilt 1756 · touchgeom 362691 · trace_replay PASS · typography 1419 · uigeom 18332 ·
uihit 1834 — **0 failures**; emutest harness `run_host_tests.sh` **169 tests, OK**.

### What is banked for the next session

- **Fixtures** (git-ignored, in `roms/`): `emerald-shore.sav` (Battle Frontier shore, one tap from
  the Surf proof), `emerald-r117.sav` (Route 117 under the cut tree), `emerald-lavaridge.sav`
  (Lavaridge fly tile, one tap from the excursion). All saved IN GAME, BY TOUCH.
- **Recon tools** (session scratchpad): `savepeek.py`, `mapdump.py`, `reach.py`, plus this lane's
  `gate.py` (is this obstacle a real gate, and which tiles does it open?) and `plan24.py` (a
  D4 route that avoids wild-encounter tiles and the map's own object events, with Match-Call
  `a` batches inserted where the avatar faces open ground).
- **`st.py`** — one-line live state: ctx + the whole `g_fieldDbg` (now including `progAKeys`,
  `progAnswers`, `progFacing`).

### Operational notes (each paid for in lost time)

1. `smartTraverse` must be **2 (HM+Via)** for P4-class excursions; the emulator's settings.bin was
   restored to the value this lane found it at (1 = HM). One line re-arms it:
   `python3 -c "import struct;p='<sdmc>/3DGBA/settings.bin';d=bytearray(open(p,'rb').read());struct.pack_into('<i',d,100,2);open(p,'wb').write(d)"`.
2. **Wait for a D4 script to FINISH, not to be picked up.** `move_p<N>.txt` disappears at PICKUP;
   a touch dropped while the script still runs aborts it (`ABORT real-input`, because the touch
   mask is routed to the seat as real input) and lands on whatever screen is up.
3. **Never `make` while a session is live** (landmine 7, paid again this session): the ELF relinks,
   every gdb read silently returns the wrong address, and the app looks dead.
4. The main menu classifies as `GCTX_TITLE` **while SaveBlock1 already holds the save's position** —
   so a live `pos` read is NOT proof the overworld is up. Check `ctx` too.

---

# LANE A2 (phase 24) — the two BANKED gesture decisions, D1 and D2

Continues directly from A1 in the same tree, same instance (**a**, gdb 24689, `runs/`).

**Entry state, verified on disk before the first edit:** all four A1 targets read **PROVEN**
(P1 SURF · P2 CUT · P3 ROCK SMASH · P4 LAVARIDGE) with a state read behind each, so nothing from
A1 is owed. `git log` ends at `df06524`. Host gate re-run fresh from this tree at A2 open —
**16 suites, 0 failures**, numbers identical to A1's close-out (celiolink 1259 · control 6940 ·
diag 376 · fieldpath 1808 · fieldtrav 1099 · netlink 66 · peersprite 62078 · presence 61376 ·
profiles 1697 · theme 83444 · tilt 1756 · touchgeom 362691 · trace 58 · typography 1419 ·
uigeom 18332 · uihit 1834).

Scope: `DECISIONS-overworld-gestures.md` **D1** (tap self = START, hold self = SELECT) and **D2**
(distance decides walk vs run), each proven host-side as a pure function first and then live.

## The host proofs (banked before the boot)

Both decisions are pure functions before they are wiring, and both are graded by the suite that
already owns the file they live in — no new suite, no new build line.

- **`owngest_step`** (touchgeom.c) resolves the own-tile gesture from a TIMELINE, because "the
  release after a hold must stay silent" cannot be said about a single frame. `test_touchgeom`
  **TEST 16** drives a synthetic touch one frame at a time and asserts over the recorded sequence:
  every press length from 1 to 4x the hold resolves to EXACTLY ONE event (never both, never none);
  everything below the threshold is a START on release; everything at or past it is a SELECT whose
  release fires nothing; every slop-crossing frame kills both verbs (or, after SELECT already
  fired, leaves it alone and still suppresses the release); a finger that was already down when
  the machine started is inert; and four hold+tap pairs give exactly 4 SELECTs and 4 STARTs, which
  is the `fired` latch being per gesture rather than sticky.
- **`rungeom_tile_ok`** is the engine's own `IsRunningDisallowed` metatile half, graded in
  **TEST 17** against an independently written oracle over **256 behaviours x 17 elevations x both
  engines** — including the anti-merge assertion that RSE blocks 7 behaviours and FRLG blocks 1.
- **`rungeom_decide`** is graded in **TEST 18** over every path length 0..64 and all **32**
  eligibility masks: exactly one mask runs, and each of the five gates is named as its own veto.
- **`FtEngCfg.runShoes`** (FLAG_SYS_B_DASH) is pinned per engine in `test_fieldtrav` TEST 2 —
  EM `0x8C0`, FR `0x82F`, and that the two DIFFER (FR's was fetched from pret master, not derived).

Suites after: touchgeom **362691 -> 371889**, fieldtrav **1099 -> 1102**, fieldpath **unchanged at
1808**. 16 suites, 0 failures. `make` clean.

## D1 — tap self = START, hold self = SELECT: **PROVEN**

Boot `runs/20260814-164526`, dual Emerald from `roms/emerald-lavaridge.sav`, instance a, touch on
the bottom seat. The own tile is bottom-screen **(160,128)** (the camera anchors the player at
screen tile (7,5); `screen = (40 + (7+ddx)*16 + 8, 40 + (5+ddy)*16 + 8)`).

| act | the game's own state |
|---|---|
| `t 160 128 6 30` — a 6-frame tap on the player | `ctx` **OVERWORLD -> FIELDMENU**, `ownStarts` 0 -> 1. The START menu is open on screen (POKéDEX/POKéMON/BAG/POKéNAV/GUYA/SAVE/OPTION/EXIT). |
| `t 160 128 45 30` — a 45-frame hold on the player (>= the 30-frame threshold) | `ownSelects` 0 -> 1, and **the player is on the BICYCLE** — the save's registered item, mounted by the game itself. |
| the next tap-to-walk plan | **`runElig` 0x1F -> 0x1B**: `RUNG_ONFOOT` (bit 2) went CLEAR. That bit is a read of the game's own `gPlayerAvatar` flags, so it can only be 0 if the SELECT press really reached the game and the game really mounted the bike. |
| a second 45-frame hold, then a plan | `ownSelects` 1 -> 2, **`runElig` back to 0x1F** — dismounted. |

**`ownStarts` stayed at 1 across BOTH holds.** That is the second half of D1 proven live: the
release that ends a hold does not also fire the tap.

Captures: `evidence/impl/EM-P24-D1a-start-menu.bottom.png` (one tap on yourself, the field menu),
`EM-P24-D1b-select-fired-the-bike.bottom.png` (one hold on yourself, on the bike).

## D2 — distance decides walk vs run: **PROVEN**, on the same seven tiles

Lavaridge row y=7 is seven clear `MB_NORMAL` tiles east of the fly tile (host recon: `runrecon.py`
over the real layout), so the same ground can be covered three ways in one boot.

| act | tiles | `runLeg` | injected mask | **frames per tile** | `runFrames` |
|---|---|---|---|---|---|
| hold-steer east from (9,7) (the walking baseline — the steer arm never presses B by construction) | 7 | 0 | `0x010` RIGHT | **16, 16, 16** | 0 |
| **one tap on (16,7)** from (9,7) | **7** | **1** | **`0x012` RIGHT+B** | **8, 8, 8, 8, 8, 8** | 51 -> 102 |
| one tap on (13,7) from (16,7) | **3** | 0 | `0x020` LEFT | **16, 16** | 102, unchanged |

Read off the game's own frame counter and its own position, sampled ~18 times a second over gdb.
The run and the baseline cross **the same tiles (10,7)..(13,7)**: 16 emulated frames each walking,
**8 each running — exactly the 2x the engine's `PlayerRun` gives** — and the 3-tile route on that
same row keeps all 16s and never adds a single frame to `runFrames`. The first tile of every route
costs ~18 frames whichever way it goes; that is the plan plus the turn, and it is why the means
(9.4 vs 16.7) understate a difference the steady state states exactly.

### The threshold boundary, live on both sides

Same row, same boot: a **4-tile** tap (exactly `RUNGEOM_MIN_TILES`) ran — `runLeg=1`, mask `0x012`,
deltas `[20, 8, 8, 8]` — and the **3-tile** tap on that row walked. The constant is where the
constant says it is.

### Gate 2, RUNG_MAP: proven INDOORS

`LavaridgeTown_PokemonCenter_1F` has `allow_running: false` in its own pret map.json. Walked in
through the front door (one tap) and tapped a **4-tile** route north — the same length that had
just run outside:

```
(8,8) -> (8,7) -> (8,6) -> (8,5) -> (8,4)   deltas [12, 16, 16, 16]   keys 0x040 (UP, no B)
runLeg=0   runElig=0x1D   runFrames 180 -> 180 (not one frame)
```

`0x1D` is `RUNG_MAP` clear and everything else open — the mask NAMES the gate that said no, which
is the whole reason the eligibility instrument is a mask and not a bool.

### Gate 5, RUNG_TERRAIN — and the defect it found

Lavaridge has two `MB_NO_RUNNING` (0x0A) sand baths reachable on foot, at (3,8) and (5,8); the
map's own object events put an OLD_MAN on (5,8), so **(3,8) is the one a player can stand on**.
Standing there, `runElig` read **0x0F** — `RUNG_TERRAIN` clear, off the game's own
`gObjectEvents[0].currentMetatileBehavior`.

And that read the defect out loud. On the first build the terrain bit was part of the LEG
decision, so a 5-tile tap from the bath walked **every** step (`runLeg=0`, deltas `[8, 16, 16]`)
— seven tiles thrown away because of the one tile the plan happened to be made on. The engine does
not do that: `PlayerNotOnBikeMoving` re-reads the behaviour **inside every step**. Fixed in
`755faf2` (`RUNG_LATCHED` = the four save/avatar gates decide the leg; the tile is asked live,
every frame B would be held), and re-run on the rebuilt app from the same tile:

```
(3,8)->(4,8)  +14f  keys 0x082   runFrames 81 -> 82     <- the bath itself: B withheld
(4,8)->(4,9)  +16f  keys 0x082   runFrames 82 -> 98     <- one lagged step (see below)
(4,9)->(4,10) + 8f  keys 0x012   ...and RUNNING from here on, 8 frames a tile, to (10,9)
runLeg=1 throughout
```

**One honest artefact, measured rather than assumed:** the veto costs one extra walked step beyond
the tile itself. The engine fixes a step's speed on the frame it *starts* it, and our behaviour
read lags that by a frame, so the step leaving the bath's neighbour is committed as a walk before
our B comes up. It errs on the safe side (we never press B where the game would refuse, we
occasionally miss one step) and it is one step on a rare tile class. Noted, not chased —
VERIFY-ON-HW along with the timing constants.

### The regression that matters: A1's flagship excursion, with D2 live

`smartTraverse = 2`, the same `emerald-lavaridge.sav` fixture, **one tap on the hot spring (5,5)**
— A1's P4 arc, re-run on the D2 build. It completed: `end=ARRIVED`, `beh=0x28`, the player standing
in the spring beside the two bathers. And the three-leg trace is the best single piece of evidence
this lane produced, because **one tap exercised three different gates in one route**:

| leg | where | `runLeg` | mask | frames/tile |
|---|---|---|---|---|
| 0-1 | **inside the Pokemon Center** (`allow_running: false`) | **0** | `0x020` / `0x040`, no B | **16, 16, 16, 16, 16, 16, 16, 16** |
| 2 | out the back door, across the north **terrace** | **1** | `0x022` / `0x082`, B held | **8, 8, 8, 8, 8** |
| 2 (tail) | the last tiles, **inside the hot spring** (`MB_HOT_SPRINGS 0x28`) | 1 | `0x080` / `0x020`, **B dropped** | **16, 16** |

`runFrames` climbs 0 -> 0 across the interior, 10 -> 52 on the terrace, and then **stops at 52**
the moment the player steps onto a spring tile; the final `runElig` reads **0x0F** with the player
standing in the water. Per-leg where the leg differs (the map gate), per-STEP where the tile
differs (the terrain gate) — which is exactly the shape D2 asks for, and exactly what the engine
does.

Capture: `evidence/impl/EM-P24-D2-excursion-ran-outdoors-walked-in-the-spring.bottom.png`.

### Documentation swept with the behaviour

`COVERAGE.md` §1a's gesture table carried `tap own tile = A` / `double-tap = START` as
hardware-verified rows; both are replaced, with the hardware-verified claim narrowed to the two
gestures that still exist (steer + tap-route) and the two new ones marked VERIFY-ON-HW-PENDING. The
in-app Smart-mode explainer said "Double-tap = START" — it now says "Tap yourself = START, hold =
SELECT", and it is SHORTER than the copy it replaced, which is the only thing the 3-line wrap
budget cares about (`test_typography` T14 re-run green). `TOUCH-PLAN.md`'s FAM-DLG line that
promised a double-tap is corrected in place rather than quietly left standing.
