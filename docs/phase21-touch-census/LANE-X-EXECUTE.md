# LANE X (phase 28) — EXECUTING phase 26, and closing the hole its audit re-opened

_Instance **b** only (gdb 24690, `runs-b/20260815-013534`, `state-b`, `az-b`); phase 27 held
instance a throughout (`pgrep -fl azahar` showed pid 40057 on gdb 24689 the whole session, so a was
never free and never touched). Worktree `.claude/worktrees/lane-x-ph28` off `ba216e5`, because the
main tree had phase 27's uncommitted `excseq` edits in it (mtime 3 minutes before I started)._

Brief: run W1 (Waterfall) and W2 (Strength) live, cover `prog_tap_gate` (audit O3), fix the Dive
round-trip guard (O2), and apply the verified corrections (F1/F2/F3/O4/O6/O7/O8).

**Headline: W1 EXECUTED, and executing it found two defects the whole phase-26 argument missed.**
The PLANNER half of Waterfall is proven correct on the user's cartridge, live, including the
retarget that is the load-bearing claim. The EXECUTOR half does not work: the program reaches the
fall, faces it, presses A eighteen times and times out, because its FACE step swims the player onto
the fall. **W2 was NOT executed** — the site the plan named is not reachable from the fixture the
way the plan assumed, and I ran out of budget short of the one that is; §3 says exactly how far I
got and what the next session should do instead.

---

## 0. What was actually run (nothing below is inherited)

| # | Check | Result |
|---|---|---|
| 1 | W1 live on the user's Emerald, emulator instance b | **executed** — §1, 10 captures + a state transcript |
| 2 | W2 live | **NOT executed** — §3, with the blocker measured |
| 3 | `prog_tap_gate` extracted to `source/progtap.{c,h}` + `test_progtap` | **1405 checks**, 5 mutations bite (388/19/44/295/2) |
| 4 | The Dive tier-order precondition (audit O2) | fixed in `fieldtrav_dive`; **TEST 24** is the real-ROM repro; removing the guard costs 7 |
| 5 | 19 host suites rebuilt and re-run from this tree | 0 failures — §5 |
| 6 | `git diff source/fieldpath.{c,h}` | **empty** — still FROZEN |
| 7 | F1 re-measured (`bscan`, all 518 maps) | **ten** boulder sites — the audit is right |
| 8 | F2 re-read in the local pokeemerald | `flags.h:1398` = SYSTEM_FLAGS + 0x28 — the audit is right |
| 9 | F3 re-measured (my own `connscan` over the user's BPEE ROM) | **518 maps, 454 NULL connections** — the audit is right |
| 10 | O7 fixed and verified on all three titles | EM **62** · FR **17** · RB **39**, from one tool |
| 11 | User data | fixtures cleaned, originals re-hashed untouched, `qt-config.ini` restored byte-identically, `settings.bin` restored (same sha256 before/after) |

---

## 1. W1 — WATERFALL, LIVE

### 1.0 The plan in §7.1 could not be run as written, and the reason is a fact worth banking

**Ever Grande's Fly point is not (27,49) for this save.** Emerald has TWO heal locations on that map
(`src/data/heal_locations.json`): `HEAL_LOCATION_EVER_GRANDE_CITY` (27,49) and
`HEAL_LOCATION_EVER_GRANDE_CITY_POKEMON_LEAGUE` **(18,6)**. An 8-badge save flies to the League gate,
and I did: `map=(0,8) pos=(18,6)`. From there the fall is **unreachable on foot** — my own flood
(`foot`, elevation 7) covers y=6..36 and stops; the pool at y=68 is not in it. §7.1 step 2
("tap-walk west/south to (20,55)") would have failed on the first tap.

So I re-sited the proof by measurement, not by preference:

| candidate | why not / why |
|---|---|
| EverGrande (0,8), K=8 | the canonical fall, but the fly point lands on the wrong plateau (above) |
| BattleFrontier_OutsideEast (26,14), 20 columns K=3 | the shore bay (`emerald-shore.sav`) does **not** connect to the fall's pool — surf flood from (48,58) reaches y=58..60 only |
| **Route 114 (0,29), x9..12, K=3, top y=9, pool y=13** | **used** — 31 foot-steps from the Fallarbor connection, land at (13,13) one tap from the water, and the fall is one tile north of the mount tile |

Route 114's fall is also the one `test_fieldtrav` TEST 22 already grades on this ROM, so the host
suite and the live run now stand on the same map.

### 1.1 The arc (every step is a touch or a D4 token, all on seat 2)

Fly Lavaridge → (accidentally) Ever Grande → **Fallarbor (14,8)** by **tap-to-fly** (the phase-24
lane-B2 driver: tap the party row, tap the FLY row, tap the destination cell — `mapFlies 0→3`) →
`l7 D1 l7 U1 L1` into Route 114 at (39,8) → `l11 D2 l10 D1 l2 D2 l3` to **(13,13)**
(`EM-P28-W1a-pond-approach.bottom.png`).

**Tap geometry, corrected — this is a landmine the next session must not step on again.** LANE-A's
banked formula `screen = (40 + (7+ddx)*16 + 8, 40 + (5+ddy)*16 + 8)` is the **SCALE_1X** case. This
instance ships `scaleMode[1] = 1 = Aspect-fit`, where `main.c touch_to_gba` maps
`gba = (px*3/4, (py - 40/3)*3/4)`. The correct tap is
`screen = (round(gba_x*4/3), round(gba_y*4/3 + 40/3))` with the tile centre at
`gba = (8+16*(7+ddx), 8+16*(5+ddy))` — i.e. **ddy −4 is screen y 45, not 64**, and only the
player's own tile (ddx=ddy=0 → 160,131 vs LANE-A's 160,128) is nearly the same under both.
`taptile.py` computes it and asserts the round trip through the app's own integer inverse before
sending. (Audit **O9** — the `t 160 104` boundary bug — is real under SCALE_1X; under Aspect-fit 104,
109 and 112 all land in the same tile. The fix is to compute the tap from the live scale mode, which
is what I did.)

### 1.2 The state reads (full transcript: `evidence/impl/EM-P28-W1-gdb.txt`)

| read | value | what it proves |
|---|---|---|
| surf mount, one tap | `pos (13,13) → (12,13)`, `surf 0 → 1`, `beh 0x15` | P1's machinery again, on a new map |
| **CONTROL tap (12,12), before HM07** | `progOutcome = UNREACHABLE (2)`, `progUsable = 0xE` (bit 4 **clear**) | §7.1's control, exactly as predicted |
| …same tap | **`progRetarget = 1`, `progGoal = (12,9)`** | the retarget fires from the tapped fall tile |
| **THE TAP (12,12), after HM07** | `progSeq +1`, `progOutcome = PLANNED (1)`, `progMoves/progInteracts = 1/1`, `progHm = 4 (FALLS)`, `progUsable = 0x1E` (bit 4 **set**), `progRetarget = 1`, `progGoal = (12,9)`, `progSurf = 1`, `progFacing = 2 (UP)` | **the whole planner claim, live** |
| …the same tap's end | `progPhase = DLG` forever, `progAKeys 3→18`, **`progAnswers = 0`**, `progEnd = TIMEOUT` | **DEFECT X1** — the executor never opened the prompt |
| the ride, driven by hand | `pos (12,13) → (12,9)`, `surf = 1` | the landing is **exactly** `progGoal`; `fieldtrav_waterfall_top` is right on the live map |
| the free descent, one `D1` | `tok D1 done at (12,10)` then `pos (12,13)` ≤203 frames later | **descending really is free** — no HM, no prompt, no interaction |

**Verdict W1: PLANNER PROVEN, EXECUTOR FAILED.** `progRetarget = 1` with `progGoal = (12,9)` against
a tap on (12,12), and a ride that ends on (12,9), is the pair the spec said nothing else can produce.

### 1.3 DEFECT X1 — the FACE step swims the player into the fall (executor, live-diagnosed)

Sampling the retry tightly caught the cause in two consecutive reads:

```
frame=47146  pos=(12,12)  progPhase=A     <- the player is ON the waterfall tile
frame=47150  pos=(12,13)  progPhase=A     <- ...and the current has flushed him back
```

A waterfall tile is collision 0 / elevation 1, so while SURFING it is *enterable*: TPH_FACE holds
the direction key until `facingDirection` matches, and from the pool that hold **is a step onto the
fall**. The game then takes the controls (`sForcedMovementTestFuncs[14]` →
`ForcedMovement_PushedSouthByCurrent`), the avatar oscillates, and every A the DLG cadence fires
lands on a non-idle frame — `FieldGetPlayerInput` only reads buttons when
`tileTransitionState == T_TILE_CENTER && !forcedMove` or `T_NOT_MOVING`
(pokeemerald `src/field_control_avatar.c:94-107`). Hence 18 A presses and no textbox.

**Proof that the interaction itself is fine:** one bare `U` (a single-slot press = a turn in place,
`pos` unchanged, `face 1 → 2`) followed by one `a` opened the game's own
*"It's a large waterfall. Would you like to use WATERFALL?"*
(`EM-P28-W1h-manual-turn-then-A-prompt.bottom.png`). So the fix is in the FACE step, not in the
constants, the gate or the plan.

**Second half of the same defect, found on the way:** after YES the script runs
`msgbox Text_MonUsedWaterfall, MSGBOX_DEFAULT` (`data/scripts/field_move_scripts.inc:192`) — the
"MILOTIC used WATERFALL." box **waits for a further A** before `dofieldeffect FLDEFF_USE_WATERFALL`
runs (`EM-P28-W1i-used-waterfall.bottom.png`: the box up, the player still at (12,13), 500+ frames).
A DONE phase that only waits for `currentMetatileBehavior` to stop being 0x13 would wait forever.

*Not fixed in this lane:* the fix belongs to whoever owns the executor next, and it must be proven
the same way this was found — the shape is "FACE must TURN, not step, when the faced tile is
enterable in the current mode", plus "DONE must keep advancing text until the ride starts". Both are
`progseq` properties, i.e. host-testable, which is the point of phase 25's extraction.

### 1.4 DEFECT X2 — the `gate == 2` fallback hands a swim up the fall to the frozen router

The control tap (Waterfall not yet usable) produced, in the same read:

```
prog seq=0 out=UNREACHABLE   wf retarget=1 progGoal=(12,9)
plan seq=2 goal=(12,9) beh=0x15 pathLen=4 end=ARRIVED      <- fieldpath, 4 steps, straight up
pos (12,13) unchanged, face=1 (DOWN)
```

`fieldtrav_plan` sets `wfRetarget` whenever the tap lands on a fall whose top resolves — **including
when the HM is not usable** — and `touch.c`'s `gate == 2` branch then hands that retargeted top to
`walk_plan`, i.e. to the FROZEN `fieldpath`, which cannot see forced movement and cheerfully plots a
swim **through** the column. The phase-26 comment says "neither router is ever asked for a route that
ENDS on a fall", which is true and beside the point: this route *passes through* one. The walker then
declared `end=ARRIVED` at a tile the player is not on.

*Not fixed in this lane* (it is a one-line change in `touch.c` — do not hand the fallback goal to the
dry router unless the column is actually climbable — but it needs its own live re-run, and lane X's
budget went on finding it). It is now in `COVERAGE.md` T5.8, which the audit also asked to be synced.

---

## 2. O3 — `prog_tap_gate` now has a barrier

Followed phase 25's precedent exactly: the pure decision moved to **`source/progtap.{c,h}`** line for
line, `touch.c` keeps only the reads and calls `progtap_gate(&o)`, and `prog_retargeted_goal` became
`progtap_retargeted_goal`. New suite **`test/host/test_progtap.c` — 1405 checks, 0 failures**, graded
against an oracle written from the rule, sweeping the whole input space (2⁵ boolean combinations ×
7 column heights × 6 player rows) rather than a handful of cases.

**Mutations, all applied / built / run / reverted:**

| mutation | failures |
|---|---|
| `PT_GATE_REFUSE` → `PT_GATE_SHIPPED` (the "plan nothing" path removed) | **388** |
| the window test `>` → `>=` | **19** |
| drop the `upward` half of the surf rule | **44** |
| drop the boulder rule entirely | **295** |
| `progtap_retargeted_goal` ignores `wfRetarget` | **2** |

**The `gate == 1` scope question, answered in writing (and in the file).** The audit is right that
the rule fires for every upward mid-surf tap while its justification only covers falls. I kept the
broad form, because consulting the conditional planner first **cannot change the outcome** when no
fall is involved: `FT_OUT_TIER0` makes `prog_plan` return −1 and the tap runs `walk_plan`; a decline
returns 0 and the tap runs `walk_plan`. Both disagreement directions — including the blocked-goal
case where `fieldpath` accepts a terminal that `fieldtrav` does not — land on the plain walk. The
cost is one extra BFS on that tap; the benefit is that a fall-crossing route is findable at all. The
argument is written at the branch in `progtap.c`, not only here.

---

## 3. W2 — STRENGTH: **NOT EXECUTED.** How far I got, and what to do instead

I did not get to a boulder inside my budget. What I did establish, by measurement:

1. **F1 re-measured and CONFIRMED** — `bscan` over all 518 Emerald maps gives **ten** boulder sites,
   exactly the audit's table, and Magma Hideout 1F's three are localIds 4/5/6 at (5,22), (7,22),
   (6,23) (pret agrees).
2. **The Fiery Path plan's approach does not work from a fly point on the Lavaridge side.** Route 112
   is cut in two by the mountain: the foot flood from the Lavaridge connection (0,47) reaches the
   Jagged Pass warp (6,46) in 13 steps and **nothing else** — Fiery Path's own warps (11,36) and
   (22,10), and the cable-car station (28,27), are all `-1`.
3. **Magma Hideout is behind a Mach-Bike wall.** Jagged Pass climbs from its Route-112 warp (14,40)
   to **(9,33)** and stops: column x=9, y=30..32 is a muddy slope (behaviour **0xD1**, collision 0),
   and `ForcedMovement_MuddySlope` (`src/field_player_avatar.c:566-580`) pushes anything that is not
   a Mach Bike at `PLAYER_SPEED_FASTEST` back south. I mounted the save's registered bike (D4 `c`,
   the phase-24 D1 proof's mechanism) and rode north from (9,35) — the only run-up the map allows,
   x=9 being blocked at y=36 — and did not move (`EM-P28-W2a-jagged-pass-muddy-slope.bottom.png`,
   `TIMEOUT tok 5 u6 at (9,33)`).
4. **So the cheapest banked route to a boulder is Fiery Path from the ROUTE 111 side**: fly Mauville
   (22,6) → north onto Route 111 (lane A's proven 57-step arc, `P3`) → west into Route 112 → the
   north warp **(22,10)** → **28 foot-steps** to (10,16), then tap the boulder at (10,15) with
   `ddx 0 / ddy −1` = **screen (160, 109)** under Aspect-fit. Everything after the walk is short: the
   control tap, the HM04 teach (the same bag arc W1 used, ~2 minutes now that the geometry is known)
   and the tap. The reads to take are unchanged, plus the two mirrors this lane added: `progLatch`
   (FlagGet(0x889), live, every frame) and `progObjSlot/progObjX/progObjY` (the boulder the plan
   aimed at, live) — which is exactly the "did it move?" channel W2 needs and phase 26 did not have.

This is a budget statement, not an impossibility claim: nothing here says W2 cannot be run, and §3.4
is a complete recipe.

---

## 4. Corrections applied (each re-verified before being written)

| id | correction | how I verified it |
|---|---|---|
| **F1** | ten boulder sites, not four (`SPEC-hm-waterfall` §7.2 rewritten, with the approach facts) | re-ran `bscan` over all 518 maps |
| **F2** | `FLAG_SYS_USE_FLASH` = SYSTEM_FLAGS + **0x28** = 0x888 (`flags.h:1398`); `+0x2A` is `FLAG_SYS_WEATHER_CTRL` (`:1400`) | re-read the local pokeemerald, counted the lines |
| **F3** | `fieldtrav.h`: "455 of Emerald's 869 maps" → **454 of 518** | my own `connscan` over the user's BPEE ROM through the shipped readers |
| **O4** | `theme.h` level-1 now lists Waterfall + the Strength terminal (and says Dive is not wired). **The shipped default is untouched — still 0 = Off** | read back |
| **O6a** | "17 host suite binaries" → 18 (BUILDLOG, SPEC-hm-dive §8) | counted |
| **O6b** | "fieldtrav 3397 (2805 before this lane)" → the phase baseline is **1210** | the audit's measurement, restated where the claim lives |
| **O6c** | Ruby's 39 columns are **not** the same map set (no Battle Frontier East, no Safari Zone SE: 62 − 23 = 39) | re-ran `wfscan` on ruby.gba |
| **O7** | `wfscan.c` now takes `[rom] [mgHex] [eng] [nGroups] [counts…]`; README carries the two non-Emerald invocations | ran all three: EM **62**, FR **17**, RB **39** |
| **O8** | the ~10 citation line-drifts in `SPEC-hm-waterfall` | applied the audit's list |
| **§6.9** | `COVERAGE.md` T5.8 synced — and now also carries defect X2, measured | rewritten |

Not corrected, deliberately: **O5** (the phase-level "added Dive to the traversal layer" framing) —
lane V's own §6 already says it plainly, and after this lane the app still cannot dive.

**One new finding of the same class as the audit's duplicate-capture rule:** two evidence files in
`evidence/impl/` are byte-identical — `FR-dlg-P2-tapped-through-to-overworld-startmenu.bottom.png`
and `FR-dlg-P4-hold-B-exited-dex.bottom.png` (both sha256 `579bffca…`). They are from phase 23/24,
not this lane; every `EM-P28-*` capture hashes distinct.

---

## 5. The gate

**19 host suites, 0 failures**, all built and run from this tree (project root as CWD):

celiolink 1259 · control 6940 · diag 376 · excseq 677 · **fieldpath 1808 (FROZEN, byte-identical)** ·
**fieldtrav 3652** (3397 + TEST 24) · netlink 66 · peersprite 62078 · presence 61376 · profiles 2746 ·
progseq 902 · **progtap 1405 (new)** · theme 83444 · tilt 1756 · touchgeom 1040833 · trace 58 (+4 loud
SKIPs) · typography 1419 · uigeom 18332 · uihit 1834.

> The 18 the audit counted were measured at `fb6d649`; this tree is `ba216e5` (phase 25 lane D
> merged), which is why touchgeom reads 1040833 and profiles 2746 — the OVERNIGHT2 buildlog records
> both jumps. The 19th is `test_progtap`.

`make -j8` clean → `3DGBA.3dsx`, no new warnings. **Dive mutation:** removing the new tier-order
precondition costs **7** failures in `test_fieldtrav` (and inflates the check count, because the
sweep's per-plan assertions start running — itself a measurement of how much the guard refuses).

---

## 6. What is still NOT proven

1. **The Waterfall EXECUTOR does not work** (defect X1) and nothing in the tree fixes it yet.
2. **Defect X2 is open**: a fall tap with the HM unusable still hands a swim to the frozen router.
3. **W2 has never run** (§3) — the latch, and the boulder-does-not-move negative, are still untested.
4. **The frame budget is still unmeasured.** `TP_DONE_BUDGET` (480) was never reached, because the
   program timed out in DLG long before the ride. The hand-driven ride shows the shape of the cost:
   the "used WATERFALL" box waits for A, then the show-mon cutscene, then K+1 slow walks.
5. **Dive still has no executor**, deliberately; TEST 24 grades the planner on real cartridge data.
6. **Nothing here ran on real 3DS hardware**, and `3DGBA.cia` is older than this tree.
