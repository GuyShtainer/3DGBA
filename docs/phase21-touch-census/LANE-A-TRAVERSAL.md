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
