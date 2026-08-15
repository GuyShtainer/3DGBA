# PHASE 28 — ADVERSARIAL AUDIT

_Auditor pass, 2026-08-15, on lane **X** (`lane-x-ph28`: `6ee8e93`, `73fc1db`, `e5db603`, three
commits on top of phase 27's `8897071`; the worktree is clean at HEAD). `main` is **still
`8897071`** — nothing in phase 28 is merged. Report audited:
`docs/phase21-touch-census/LANE-X-EXECUTE.md`._

The brief: this is the first phase family with **live-execution claims**, so verify each against
the artefacts (distinct capture hashes, a game-produced state delta per verb); mutate every branch
the tap-gate extraction claims to cover; reproduce the Dive round trip on the real ROM; spot-check
the corrections for a *new* wrong constant; and scrutinise every NEGATIVE at least as hard as the
positives — five of this project's worst findings were false negatives.

**Headline: the live work is real and the planner claim survives independent re-derivation — and
both of the lane's load-bearing NEGATIVES are wrong.** The Ever Grande fly point is not
badge-determined (it is the *cell you tap*, `region_map.c:2007`), so §7.1's K=8 plan — the one that
would have measured the frame budget — was runnable; and the save's registered bike **is** the Mach
Bike (`registeredItem = 259`), so "Magma Hideout is behind a Mach-Bike wall" is a misdiagnosis. The
tap-gate extraction, the Dive fix and every correction check out exactly.

---

## 0. What I actually ran (nothing below is quoted from the lane)

| # | Check | Result |
|---|---|---|
| 1 | All **19** host suites rebuilt and re-run at the lane's committed HEAD, CWD = tree root | 0 failures; every count matches except **excseq** (§6) |
| 2 | **17 mutations** of `progtap.c` (the lane's 5 + 12 of my own), each applied / built / run / reverted | §3 — the lane's five reproduce *exactly*; every verdict-changing branch bites |
| 3 | The audit's O2 repro — shipped `fieldtrav_dive` on the real Route 126 pair, my own driver | refused as **TIER0**; with the guard removed I reproduce the audit's pre-fix line byte for byte |
| 4 | The W1 **planner** claim re-derived host-side on Route 114's real ROM map | reproduces exactly: `UNREACHABLE`/retarget=1/goal (12,9) before, `PLANNED`/1 move/1 interact/`hm=4`/goal (12,9) after |
| 5 | Defect **X2** re-derived host-side (`fieldpath_plan` (12,13)→(12,9)) | `pathLen=4`, four UP steps straight through the fall — the defect is real and unfixed |
| 6 | Ever Grande foot flood from (18,6) and from (27,49), plus the surf flood from the pool | the lane's *observation* holds; its *inference* does not (**F1**) |
| 7 | `region_map.c` + `heal_locations.json` + `region_map_layout.h` re-read | Ever Grande is a **two-cell** fly section; (27,49) is one cursor cell away (**F1**) |
| 8 | The staged fixture save parsed (`roms/emerald-lavaridge.sav`, slot 1, counter 1967) | `registeredItem = 259 = ITEM_MACH_BIKE`; no Acro Bike in the pocket (**F2**) |
| 9 | Route 112 / Jagged Pass geometry re-measured (`foot`, `mapdump2`) | the lane's blocker numbers are **exact** |
| 10 | Boulder census re-run (`bscan`, 518 maps) | **ten** sites, counts 6/2/6/7/4/12/8/1/3/10; Magma Hideout localIds 4/5/6 ✓ |
| 11 | `wfscan` re-run on all three titles with the README's documented arguments | EM **62** · FR **17** · RB **39** ✓ (O7 really is fixed) |
| 12 | F3 re-measured with my own connection scanner through the shipped readers | **518** maps, **454** non-ROM `+0x0C` ✓ |
| 13 | Every EM-P28 capture hashed; all 125 files in `evidence/impl` hashed | 10/10 distinct; the claimed pre-existing duplicate pair is the **only** duplicate |
| 14 | `git diff` of `source/fieldpath.{c,h}`, `git log --all` for `*.gba`/`*.sav` | frozen (byte-identical, last touched `010a138`); nothing binary ever committed |
| 15 | Both emulator instances, both `settings.bin`, both sdmc trees | b is clean; **a is not** (§7) |
| 16 | Build artefacts hashed; `nm` on both ELFs | the worktree really rebuilt `.3dsx`+`.cia` (carries `progtap_gate`); **main's `.cia` is stale** |

Suite counts I measured at `e5db603`, all 0 failures — celiolink 1259 · control 6940 · diag 376 ·
**excseq 689** · **fieldpath 1808 (FROZEN)** · **fieldtrav 3652** (ROM half ran; no PARTIAL SKIP) ·
netlink 66 · peersprite 62078 · presence 61376 · profiles 2746 · progseq 902 · **progtap 1405** ·
theme 83444 · tilt 1756 · touchgeom 1040833 · trace 58 (+4 SKIPs) · typography 1419 · uigeom 18332 ·
uihit 1834. **19 suites.**

---

## 1. Verdicts

### CONFIRMED

| Claim | What decided it |
|---|---|
| **W1 was really executed, live, on instance b** | `runs-b/20260815-013534` is a genuine run: `boot.json` pid 41635 / Azahar 2125.1.2, `cmdline.txt` boots **the lane's own worktree `.3dsx`**, `events.log` runs `01:35:34Z → 02:28:52Z` and ends with `fixtures: originals re-hashed, all untouched` + `profile: restored qt-config.ini byte-identically`. The app's OWN log (`netlogs/3DGBA_control_0101_020116.txt`, 393 lines) is game-produced evidence: the Fallarbor→Route 114 walk `l11 D2 l10 D1 l2 D2 l3` ends `done at (13,13)` exactly as §1.1 says. |
| **The PLANNER half of Waterfall is proven** — retarget from a tap on (12,12) to (12,9), `PLANNED`/`FALLS`, `progUsable` bit 4 0→1 | I re-derived it **host-side against the same cartridge bytes**, no emulator involved: Route 114 (0,29) has the fall at x9..12 y10..12 with `waterfall_top(12,12) = 9`; `fieldtrav_plan` from (12,13) to (12,12) gives — without a Waterfall mon — `outcome=UNREACHABLE, wfRetarget=1, goal=(12,9), usable bit4=0`, and with one — `ok=1, outcome=PLANNED, nMoves=1, nInteracts=1, mv[0].hm=4 (FT_HM_WATERFALL), wfRetarget=1, goal=(12,9), usable bit4=1`. Every number in the lane's two state reads falls out of the shipped planner on the real map. The live run is corroboration, not the sole source. |
| **The EXECUTOR fails** | Not merely asserted: `EM-P28-W1e-tap-timeout.bottom.png` shows the avatar surfing at the fall's base, **facing up, no textbox**, and `EM-P28-W1h-manual-turn-then-A-prompt.bottom.png` shows the game's own *"It's a large waterfall. Would you like to use WATERFALL?"* from the **same tile** after one hand-driven `U` + `a`. 18 injected A presses with `progAnswers = 0` against a prompt that a human opens in two frames is a failure by any reading. |
| **X1's mechanism is the right shape** | `progseq.c` TPH_FACE states the assumption in its own comment — *"The tile is impassable (that is the whole point), so this bumps in place"* — and a `MB_WATERFALL` metatile is collision 0 / elevation 1, i.e. **enterable while surfing** (I re-measured Route 114: the whole column is 0x13 with the pool 0x15 below). `FieldGetPlayerInput` (`field_control_avatar.c:94-100`) really does drop A unless `tileTransitionState == T_TILE_CENTER && !forcedMove`, and `sForcedMovementTestFuncs[14]`/`sForcedMovementFuncs[15]` really are the waterfall/pushed-south pair. The diagnosis is sound *(see O3 for what is single-sourced)*. |
| **The second half of X1** — after YES the game runs `msgbox …, MSGBOX_DEFAULT` and waits for another A | `data/scripts/field_move_scripts.inc:193` is exactly `msgbox Text_MonUsedWaterfall, MSGBOX_DEFAULT` ✓ (the lane cites `:192`, a fresh one-line drift — O5). A DONE phase that only watches `currentMetatileBehavior` would indeed hang there. |
| **Descending a fall is free** | The app's own log: `tok 1 D1 start (12,9)->(12,10) f=46277` → `done at (12,10) f=46278`, no interaction, no prompt. |
| **DEFECT X2 is real, and unfixed** | Reproduced host-side with the frozen router: `fieldpath_plan(12,13 → 12,9)` on the real map returns `ok=1, pathLen=4`, **four UP steps** straight through the 0x13 column. And `touch.c`'s `gate == 2` branch does hand `prog_retargeted_goal()`'s (12,9) to `walk_plan`. Both halves verified in code, not just in a log line. |
| **The tap-geometry correction** | `main.c:720-730`: Aspect-fit gives `f = min(320/240, 240/160) = 4/3`, `ox = 0`, `oy = 40/3` → `gba = (px·3/4, (py − 40/3)·3/4)` — the lane's formula exactly. Instance b's `settings.bin` really does carry `scaleMode[1] = 1`. `ddy −1 → (160, 109)` and `ddy −4 → y 45` both check out, and audit **O9**'s boundary bug genuinely does not bite under Aspect-fit (104 / 109 / 112 all land in the same tile row). |
| **`prog_tap_gate` was extracted line for line, and there is exactly one copy of the verdict** | I diffed the pre- and post-extraction logic branch by branch: the fall branch (`topY < 0 \|\| \|topY − py\| > FP_WHALF → −1 else 2`), the surf-upward rule, the boulder rule, the two defensive `return 0`s and the short-circuit order are all semantically identical; `touch.c` now holds only reads. *(One predicate is mirrored for cost — O2.)* |
| **The tap-gate mutation numbers** | All five reproduce **exactly**: REFUSE→SHIPPED **388** · `>`→`>=` **19** · drop `upward` **44** · drop the boulder rule **295** · retarget ignores `wfRetarget` **2**. §3 adds twelve more. |
| **The Dive tier-order precondition fixes the audit's O2 case** | My own driver, shipped `fieldtrav_dive`, real ROM, map (0,41), start (20,40), goal (20,41): `ok=0 outcome=0(TIER0)`, nothing left in the plan. With the guard removed I get the audit's pre-fix line verbatim — `ok=1 outcome=1(PLANNED) pair=(0,51) dive@(20,40) -> up@(20,41) out/mid/back=0/1/0`. Across a 25×25 sweep the guard converts **326 of 448** former plans into TIER0. Removing it costs exactly **7** failures in `test_fieldtrav` (4306 checks, 7 fail) — the claimed number. |
| **F1 re-measured: ten boulder sites** | My own `bscan` over all 518 Emerald maps: (24,14) 6 · (24,28/29/30/32/35) 2/6/7/4/12 · (24,44) 8 · (24,49) 1 · (24,86) 3 · (29,6) 10 — ten maps, and Magma Hideout 1F's three really are localIds **4/5/6** at (5,22), (7,22), (6,23). |
| **The Route 112 blocker** | Exact: the foot flood from the Lavaridge connection (0,47) reaches the Jagged Pass warp (6,46) in **13** steps and gives **−1** for (11,36), (22,10) and (28,27). |
| **The Jagged Pass slope geometry** | Exact: column x=9, y=30/31/32 all read behaviour **0xD1**, collision 0, elevation 3; x=9 y=36 is collision 1, so the run-up really is two tiles. `ForcedMovement_MuddySlope` (`field_player_avatar.c:567-581`) really does require `movementDirection == DIR_NORTH && GetPlayerSpeed() >= PLAYER_SPEED_FASTEST`. |
| **Ever Grande is foot-unreachable from the League gate** | My own flood from (18,6): reachable rows 6..31 only; (20,55), (20,59), (20,68) and (26,68) all **−1**, and the surf body below the fall (rows 67+) is disjoint from the plateau. The *observation* is honest. *(The conclusion drawn from it is not — F1.)* |
| **F2 / F3 / O6c / O7 / O8 corrections** | `FLAG_SYS_USE_FLASH` = SYSTEM_FLAGS + **0x28** (`flags.h:1398`) with `+0x2A` = `FLAG_SYS_WEATHER_CTRL` (`:1400`) ✓ · **518** maps / **454** non-ROM connections, my own scan ✓ · Ruby's 39 = Emerald's 62 minus Battle Frontier Outside East (20) and Safari Zone Southeast (3), map set compared column for column ✓ · `wfscan` now answers EM 62 / FR 17 / RB 39 from one binary with the README's arguments ✓ · `EventScript_StrengthBoulder` at `:124` with BADGE04 at `:126` ✓. |
| **W2 was NOT executed, and the report says so** | True, and stated without softening in §3 and §6.3. The honest consequence is in §2 below. |
| **Evidence hygiene** | All ten `EM-P28-*` captures hash distinct; across all 125 files in `evidence/impl` the only byte-identical pair is the pre-existing `FR-dlg-P2…` / `FR-dlg-P4…` (`579bffca…`), exactly as reported. |
| **`fieldpath` frozen · no ROM/save in history** | Byte-identical to `main` (`f1e6d95d…` / `6a52bb1f…`), last touched at phase 18's `010a138`; `git log --all --diff-filter=A` finds no `*.gba`/`*.sav`/`roms/` ever added. |
| **Instance b is clean** | No staged `gameA/gameB` files, empty control dir, and `settings.bin` = `8fe6a815497902d2923baeef8595b57bf365ca15d524d75c62158d1705c7f4af` with the `traverse` field (offset 100, `Settings` field 25 — verified against `main.c`'s struct and its `_Static_assert`) reading **0**. |

### OVERSTATED

**O1 — "Re-sited by measurement, not preference" is true of the flood and false of the decision.**
The measurement (the foot flood from (18,6)) is exact and I reproduced it. But it only answers
*"can I walk to the pool from where this particular tap put me?"* — a question created by the tap.
The prior question (*"is (27,49) available?"*) was never measured, and the answer is yes (**F1**).
Route 114 is a legitimate site and TEST 22 already grades it, so the run is not wasted; but the
phase-26 audit wanted Ever Grande **specifically because K=8 is the frame-budget measurement**, and
that measurement is still missing with a false reason attached to it.

**O2 — "the pure decision moved to `source/progtap.{c,h}` line for line … and there is exactly one
copy of it" — one predicate is still duplicated.**
`touch.c` gates its own reads with `if (!(o.upward && o.surfing && o.waterfallUsable))` before
computing `strengthTap`. That is rule (2)'s predicate, restated at the call site as a cost
discipline. It changes nothing today (both paths return `PT_GATE_FIRST`), but it means the file is
not observation-only: if rule 2 were ever **narrowed** — which is exactly what audit O3 asked the
next session to consider — `touch.c` would keep suppressing the boulder read on the old condition,
and no mutation of `progtap.c` can see it. Name it, or move the cost gate behind a
`progtap_needs_strength(&o)` helper so both live in the covered file.

**O3 — "caught in two reads" is the one W1 claim with no artefact behind it.**
Every `g_fieldDbg` value in this lane — including the two frames that name X1's cause
(`f=47146 pos=(12,12)` / `f=47150 pos=(12,13)`) — exists only as **hand-typed lines in
`EM-P28-W1-gdb.txt`**. No raw `st28.py` output was banked, the reader itself
(`st28.py`, `taptile.py`) lives only in a session scratchpad and is not in the repo, and the ELF
that produced the reads was overwritten by the 05:48 rebuild with no hash recorded in the run dir.
The netlog and the screenshots are raw and they carry the *outcome* (walk, timeout, prompt-by-hand,
free descent); the *cause* rests on transcription. I re-derived the planner numbers independently
(§1 row 2), which is why the planner verdict stands — but nothing independent reaches the two
mid-program position reads. Bank the raw capture next time; it is one `tee`.

**O4 — "the `gate == 1` scope is defended in writing … no walkable route can be displaced" is
broader than the argument supports.**
The real structural guarantee is one line in `fieldtrav_plan`: `usedK == 0 → FT_OUT_TIER0`. So a
conditional program can only be armed where **fieldtrav's own dry BFS** fails — not where
*fieldpath* fails. The defence names the one known divergence (a blocked goal, which `fieldpath`
accepts as a terminal) and asserts that "both disagreement directions … land on the plain walk";
that is not so — a blocked goal carrying an eligible HM edge yields an interact program instead of
the walk-and-hold. That is the *feature*, so this is not a defect, but the sentence claims more than
it proves, and any other divergence between the two dry models (door/warp handling, head-retarget,
elevation, the NPC list) is uncovered by the same argument. Separately, `test_progtap`'s oracle is
structurally isomorphic to the implementation: it grades whether the rule was *coded* right, never
whether the rule *is* right.

**O5 — new citation drift, in the lane that fixed the old drift.**
`LANE-X-EXECUTE.md` §1.3 cites `data/scripts/field_move_scripts.inc:192` for
`msgbox Text_MonUsedWaterfall, MSGBOX_DEFAULT`; it is at **:193**. And the O8 sweep did not touch
`SPEC-hm-waterfall` §6.2's Ruby row, which still reads `field_move_scripts.inc:128` for BADGE04
while the phase-26 audit's own fetch of pret master put `S_PushableBoulder`'s BADGE04 line at
**:126**. Small, but this project's rule is that a citation is checkable.

**O6 — counted claims that are wrong, again.**
(a) §5 lists **excseq 677**; measured at this tree it is **689** (677 is the `ba216e5` number). (b)
The same §5 note says "this tree is `ba216e5`" — the branch is three commits on top of phase 27's
`8897071`, which is *why* excseq, touchgeom and profiles moved. Both are the exact class of error
the lane spent §4 correcting in someone else's document (O6a/O6b/O6c).

**O7 — `test_fieldtrav` TEST 24 contains one tautology and one comment that does not match its
assertion.**
`CHECK(planned == 0 || planned > 0, …)` can never fail; it inflates the count by one. And (d) is
introduced as *"a goal the player cannot reach in the home mode (dry land on this map) is still not
a dive, but it must not be TIER0"* and then asserts the **opposite case** — the player's own tile,
which *is* TIER0. The dry-land case the comment describes is untested. Both are cosmetic against a
test that otherwise bites hard (7 failures on the guard mutation).

**O8 — the phase has no BUILDLOG entry.**
`BUILDLOG.md` was edited only to correct the "17 suites" line. The progtap extraction, the Dive
guard, the two new defects and the first live execution in this family are recorded nowhere in the
build log the project keeps for exactly that.

### FALSE

**F1 — "Ever Grande's Fly point is not (27,49) for this save. An 8-badge save flies to the League
gate … §7.1's plan is unrunnable."**
The fly destination is not chosen by badge count. `src/region_map.c:2007`:

```c
SetWarpDestinationToHealLocation(FlagGet(FLAG_LANDMARK_POKEMON_LEAGUE)
    && sFlyMap->regionMap.posWithinMapSec == 0
    ? HEAL_LOCATION_EVER_GRANDE_CITY_POKEMON_LEAGUE : HEAL_LOCATION_EVER_GRANDE_CITY);
```

`MAPSEC_EVER_GRANDE_CITY` occupies **two** region-map cells (`region_map_layout.h`, column 27 in two
consecutive rows) and is the game's only `sMultiNameFlyDestinations` entry — with
`FLAG_LANDMARK_POKEMON_LEAGUE` set the picker shows two names and
`GetPositionOfCursorWithinMapSec` returns 0 for the upper cell, 1 for the lower. Tapping the **lower**
cell flies to `HEAL_LOCATION_EVER_GRANDE_CITY` = **(27,49)** — precisely the fly point §7.1 assumed.
The lane tapped the League cell and generalised the result into a property of the save.

And the plan really does work from there. My foot measurement from (27,49): the shore at **(20,55)
is 13 steps** away (the walk descends through the elevation-0 transition tiles at y=54), the water
at y=56..59 above the fall is surfable, the fall is x15..26 y60..67 with its landing at y=59, and
the descent into the pool at y=68 is the free ride the lane itself proved. So the K=8 site — the one
the phase-26 audit named *because* it is the `TP_DONE_BUDGET` measurement — was runnable, and the
frame budget is still unmeasured for a reason that is not true.

*(The re-site to Route 114 is otherwise a good call: the fall is the one TEST 22 grades, so the host
suite and the live run now stand on the same map. It is the stated reason that is false, not the
site.)*

**F2 — "Magma Hideout is behind a Mach-Bike wall … `ForcedMovement_MuddySlope` needs a Mach Bike;
the save's registered bike did not climb it."**
The staged fixture save that ran — `roms/emerald-lavaridge.sav`, the file `events.log` records being
copied to `gameB.sav` — has, in its newest slot (save counter 1967):

```
SaveBlock1 +0x496  registeredItem = 259 = ITEM_MACH_BIKE
key items pocket   259 (Mach Bike) present;  272 (Acro Bike) ABSENT
```

**The registered bike IS the Mach Bike.** So the climb did not fail for want of the right bike, and
"Mach-Bike wall" names a wall that is not there. What the decomp actually requires is
`GetPlayerSpeed() >= PLAYER_SPEED_FASTEST`, i.e. `sMachBikeSpeeds[bikeFrameCounter]` with
`bikeFrameCounter == 2` — the counter increments once per tile advanced and is reset to 0 by
`Bike_UpdateBikeCounterSpeed(0)` on every push-back and by `Bike_SetBikeStill` when the D-pad is
released. The observed `TIMEOUT tok 5 u6 at (9,33)` and `TIMEOUT tok 1 U8 at (9,33)` are exactly what
a **token-driven run-up that never reaches full speed** looks like — a harness limitation, not a map
one. The correct statement is *"my control tokens could not build Mach-Bike speed on the two-tile
run-up"*, and the fix to try first is a held-input token, not a 57-step detour via Route 111.

This matters beyond wording: §3.4's "cheapest banked route to a boulder" is derived from the false
wall, so the next session may spend its budget on the wrong approach.

**F3 — "Suites: 19, 0 failures … excseq 677."** The suite gate is real and I reproduced all 19 at 0
failures, but the excseq count in it is wrong: **689** at this tree. Filed as FALSE rather than
OVERSTATED because it is a measured number stated as measured (see O6).

---

## 2. The W2 negative the brief asked about, stated plainly

The load-bearing assertion for Strength is a **negative — the boulder must not move** — and the
audit brief asked whether it was measured or merely seen in a picture. Neither: **it has never been
measured at all.** W2 did not run, no boulder was ever reached, and the two mirrors this lane shipped
for exactly that question (`progObjSlot/progObjX/progObjY`, `progLatch`) have only ever been observed
at their defaults. The channel now exists and is correctly wired — `p->mapObjects + 0x24·slot + 0x10/0x12`
minus the engine's +7 grid bias, which matches `touch.c`'s own NPC reads and the pret struct — but
"the boulder does not move" remains an argument from `edge_at`'s shape plus two host mutations,
exactly as it was after phase 26.

The lane says this without softening ("This is a budget statement, not an impossibility claim"),
which is the right posture. What is *not* right is the blocker analysis it banks alongside (F2).

---

## 3. Mutations — the tap gate, 17 of them

Base: `test_progtap` = **1405** checks, 0 failures.

| # | mutation | failures |
|---|---|---|
| 1 | `PT_GATE_REFUSE` → `PT_GATE_SHIPPED` (the "plan nothing" path removed) | **388** (lane: 388 ✓) |
| 2 | window test `>` → `>=` | **19** (lane: 19 ✓) |
| 3 | drop the `upward` half of the surf rule | **44** (lane: 44 ✓) |
| 4 | drop the boulder rule entirely | **295** (lane: 295 ✓) |
| 5 | `progtap_retargeted_goal` ignores `wfRetarget` | **2** (lane: 2 ✓) |
| 6 | drop the `surfing` half of the surf rule | 43 |
| 7 | drop the `waterfallUsable` half of the surf rule | 43 |
| 8 | remove the surf-upward rule outright | 42 |
| 9 | drop the `topY < 0` guard | 48 |
| 10 | drop the `abs()` on the window distance | 161 |
| 11 | `PT_GATE_FALL` → `PT_GATE_FIRST` (the raw fall tile may reach a router) | 295 |
| 12 | remove the whole fall branch | 683 |
| 13 | final fall-through `SHIPPED` → `FIRST` | 299 |
| 14 | NULL observation → `FALL` instead of `SHIPPED` | 1 |
| 15 | boulder rule ordered before the surf rule | **0 — inert, and correctly so** |
| 16 | `progtap_retargeted_goal` always returns 0 | 2 |
| 17 | …returns 0 but still writes the goal | 1 |

**Every branch that can change a verdict bites.** #15 is the only zero and it is a genuine
semantic no-op (both rules return `PT_GATE_FIRST`), not a coverage hole. The claim "1405 checks,
oracle-swept" is accurate; see O4 for what an oracle written from the same paragraph cannot catch.

---

## 4. Dive — the audit's own case, reproduced twice

My driver, the **shipped** `fieldtrav_dive`, `roms/emerald.gba`, Route 126 (0,41), badge-07 +
`MOVE_DIVE`, start (20,40), goal (20,41) — both tiles `MB_DEEP_WATER` (0x12), one plain surf step
apart:

```
with the phase-28 guard:     RESULT ok=0 outcome=0(TIER0)  out/mid/back=0/0/0
guard removed (my mutation): RESULT ok=1 outcome=1(PLANNED) pair=(0,51)
                                    dive@(20,40) -> up@(20,41)  out/mid/back=0/1/0
```

The second line is the phase-26 audit's finding, character for character. Over a 25×25 goal sweep
the guard turns **326** former plans into TIER0 and leaves **122** (all of which cross the paired
map). `test_fieldtrav` loses exactly **7** checks when the guard is removed. **The fix is real, it is
in the planner rather than in a caller, and the outcome name (`FT_OUT_TIER0`) is the honest one.**

---

## 5. What phase 28 did NOT change

* **Dive still has no executor.** Zero callers of `fieldtrav_dive` in `source/` outside its own
  module; `touch.c`/`progseq`/`g_prefs` carry no `FT_HM_DIVE` path.
* **The shipped default is still `smartTraverse = 0`.** `theme.c:35` unchanged — nothing in phase
  26 or phase 28 does anything on a default install. `theme.h`'s level-1 text is now correct (O4 of
  the phase-26 audit, applied).
* **Defect X2 is open**, and it is the one that bites at the shipped default's *neighbour* (level 1):
  a fall tap with the HM unusable still hands a retargeted goal to the frozen router, which plans a
  swim up the column and reports ARRIVED.
* **The Waterfall executor is broken** and nothing in the tree fixes it.
* **The frame budget is still unmeasured** — and F1 removes the stated excuse.

---

## 6. Suites

All 19 rebuilt and re-run by me at `e5db603` from the tree root. Every count in §5 of the lane
report reproduces **except excseq (677 claimed → 689 measured)**; the header note naming the base as
`ba216e5` is stale (the branch sits on `8897071`). `fieldtrav` is 3652 **with the ROM half actually
running** — I checked for the PARTIAL SKIP banner and there is none, so TEST 22/23/24 really were
graded against the user's cartridge. `fieldpath` is 1808 and byte-identical to `main`.

---

## 7. Standing items

1. **Unmerged.** `main` is `8897071`; the entire phase — the tap-gate barrier, the Dive guard, the
   corrections, the live findings — lives on `lane-x-ph28`. The lane says so plainly, which is what
   phase 25's standing item asks for, but "shipped" it is not.
2. **The `.cia`.** The worktree's is current (`05:48:59`, its own hash, its ELF exports
   `progtap_gate`). **`main`'s `.cia` is stale for the third phase running** — 15 Aug 04:14 against
   `main`'s own `3DGBA.3dsx` at 05:16 — so a hardware install today gets pre-phase-27 code, and it
   will need rebuilding again the moment lane X merges.
3. **Instance cleanliness — b yes, a no.** Instance b is spotless (verified byte for byte). The
   **default instance a — the user's real `~/Library/Application Support/Azahar` — still carries
   staged fixtures**: `gameA.gba`/`gameB.gba` plus `gameA.sav`/`gameB.sav` whose sha256 is
   `d222c5cd…` = `roms/emerald-fix.sav`, and a `settings.bin` with **`traverse = 1`** (the feature
   left switched on in the user's own profile). Timestamps put this on **phase 27**, not lane X — but
   phase 25's standing item is "clean both instances, not just the one that noticed", and nobody has.
4. **No ROM or save is tracked or has ever been committed.** Clean.
5. **Recon tools:** `wfscan`/`bscan`/`foot`/`reach2`/`mapdump2` are banked and all five run from the
   README as documented (I built and ran four of them). The tools this lane actually leaned on for
   the live work — `st28.py` (the state reader) and `taptile.py` (the tap geometry, which encodes the
   scale-mode landmine) — are **not banked**; they are in a session scratchpad. That is the O7 lesson
   with the names changed.

---

## 8. The eight HMs after phase 28 — implemented / live-proven / never executed

Everything in the traversal layer needs **Settings → smart traverse ≥ 1**; the shipped default is
**0 = Off**. Tap-to-Fly is a different subsystem and is not gated by it.

| HM | Implemented? | Ever executed? | Plain statement |
|---|---|---|---|
| **01 Cut** | **Yes** — object edge, level ≥ 1 | **LIVE-PROVEN** (phase 24 P2, emulator) | works end to end |
| **02 Fly** | **Yes** — region-map driver (not the traversal layer) | **LIVE-PROVEN** (phase 24 lane B2; used again this phase to reach Fallarbor) | works end to end. Newly documented this audit: Ever Grande is a **two-cell** fly section, and which cell you tap decides (18,6) vs (27,49) |
| **03 Surf** | **Yes** — metatile edge, level ≥ 1 | **LIVE-PROVEN** (P1; re-proven phase 25; mounted again live this phase) | works end to end |
| **04 Strength** | **Yes, as a TERMINAL only** — the tap activates the game's own prompt; the router never routes *through* a boulder and never pushes one | **NEVER EXECUTED** | host-graded + mutation-graded only. Phase 28 shipped the live channel it needs (`progLatch`, `progObjSlot/X/Y`) and then never reached a boulder. **The safety negative — the boulder does not move — has never been measured.** The stated blocker ("Mach-Bike wall") is false: the save's registered bike **is** the Mach Bike; what failed was the harness's run-up |
| **05 Flash** | **No — deliberately** | n/a | settled with citations: it changes no tile's walkability. Nothing to prove |
| **06 Rock Smash** | **Yes** — object edge, level ≥ 1 | **LIVE-PROVEN** (phase 24 P3) | works end to end |
| **07 Waterfall** | **Yes** — multi-tile edge with goal-retarget, level ≥ 1 | **PLANNER LIVE-PROVEN; EXECUTOR LIVE-DISPROVEN** | the plan is right — proven live on Route 114 *and* re-derived by me from the cartridge bytes (retarget (12,12) → (12,9), `PLANNED`/`FALLS`, eligibility bit flipping across the HM teach). The **executor does not work**: its FACE step swims the surfing player onto the fall, every A lands on a forced-movement frame, `progAnswers = 0`, `TIMEOUT` (defect X1) — while the same tile and facing open the prompt by hand in two presses. A second, independent executor bug is banked (the `MSGBOX_DEFAULT` box after YES needs another A). And defect **X2** is open: with the HM unusable, the same tap hands the retargeted goal to the frozen router, which plots a 4-step swim UP the column and calls it ARRIVED (reproduced host-side). **The ride has never completed under program control**, and the K=8 frame budget has never been measured |
| **08 Dive** | **Planner only** — correctly modelled as a map connection with identity coordinates; `touch.c`/`progseq`/`g_prefs` untouched, **zero callers** | **NEVER EXECUTED** (there is nothing to execute) | host-graded in a synthetic world, 19 mutations, and now on the **real** Route 126 pair — where phase 28 fixed the audit's nonsense round trip at the source (`FT_OUT_TIER0` for anything the home mode already reaches; I reproduced both the bug and the fix). Still no executor |

**Score, without softening: six of eight are planned by touch** (Cut, Fly, Surf, Rock Smash,
Waterfall, and Strength as an activation-only terminal). **Four have ever run end to end** — Cut,
Fly, Surf, Rock Smash. **Waterfall has run as far as its plan and no further.** **Strength has never
run at all, and its safety negative has never been measured.** **Dive cannot run.** **Flash is
deliberately out.** And none of it is on `main`, none of it is in `main`'s `.cia`, and all of it is
off by default.

---

## 9. What is owed

1. **Fix the executor (X1)** — FACE must **turn, not step**, when the faced tile is enterable in the
   current mode, and DONE must keep advancing text until the ride actually starts. Both are
   `progseq` properties, i.e. host-testable, which is the whole point of phase 25's extraction.
2. **Fix X2** — do not hand the `gate == 2` fallback goal to the dry router unless the column is
   climbable. One line, then a live re-run.
3. **Run W1 at EVER GRANDE (K=8)** — the fly point is one region-map cell away (F1), the walk is 13
   steps, and it is the only way to measure `TP_DONE_BUDGET`.
4. **Run W2** with a held-input run-up on Jagged Pass before spending a session on the Route 111
   detour (F2), and read `progObjX/progObjY` **before and after** the prompt — that negative is the
   whole point of the row.
5. **Merge the lane and rebuild `main`'s `.cia`** (standing item, third recurrence).
6. **Clean instance a** — staged ROM/save fixtures and `traverse = 1` in the user's own emulator
   profile (standing item, second recurrence).
7. **Bank `st28.py` and `taptile.py`** next to the recon tools, and `tee` the raw state reads into
   `evidence/` (O3) — a hand-typed transcript is not a checkable citation.
8. **Correct the counted claims** (excseq 689, the base commit `8897071`), the `:192` → `:193` drift
   and the Ruby `:128` → `:126` row, drop TEST 24's tautology, and fix its (d) comment or write the
   dry-land case it describes.
9. **Move the cost gate into `progtap.c`** (O2) so the file that is covered owns the whole rule.

---

_Method note: three of this audit's findings (F1, F2, and the excseq count) came from re-running or
re-parsing the lane's own inputs rather than from reading its prose — and two of the three are
negatives, again. The pattern phase 25 named is holding: **this project verifies what it claims and
asserts what it declines.** A "wall" needs the same standard as a feature. Both walls this lane
banked dissolved on the first measurement._
