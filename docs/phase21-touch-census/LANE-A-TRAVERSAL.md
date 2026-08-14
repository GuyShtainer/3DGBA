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
