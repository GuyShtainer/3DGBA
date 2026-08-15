# PHASE 26 — ADVERSARIAL AUDIT

_Auditor pass, 2026-08-15, on `main` at **`fb6d649`** (working tree clean apart from untracked
`.claude/worktrees/` and `PHASE25-AUDIT.md`). Two lanes: **W** = Waterfall + Strength (`7e512bf`,
`251f400`, `fb6d649`), **V** = Dive + Flash (`53aeb1a`, whose code rode in on `7e512bf`)._

The brief was to disbelieve — in particular to hunt (a) a constant that crossed an engine
boundary, (b) a golden regenerated from the implementation, (c) Dive modelled as a tile edge when
the engine models it as a map transition, (d) an FRLG row carrying an RSE Dive value, (e) a
touched `fieldpath`, an unrun suite, a committed ROM, and (f) a stale caveat.

**Headline: (a)–(e) are clean.** Every new constant is where the lanes say it is, in the right
decomp; every new golden bites; Dive is modelled correctly *as a map connection*; the FRLG dive
row is a named zero with its own guard; `fieldpath` is byte-identical; every suite reproduces;
nothing binary is tracked. **(f) is not clean** — and two claims about what has been *proved*
are wider than the evidence.

---

## 0. What I actually ran (nothing below is quoted from the lanes)

| # | Check | Result |
|---|---|---|
| 1 | All **18** host suites rebuilt and re-run from the project root at `fb6d649` | 0 failures; every count matches §4 |
| 2 | `test_fieldtrav` rebuilt from an archive of `7394be4` (the pre-phase-26 tree) | **1210** — lane V's baseline is the right one |
| 3 | **24 mutations** (lane W's 5 + lane V's 19), each applied, built, run, reverted | §5 — all reproduce |
| 4 | Every new constant re-read in **its own** decomp, at the cited `file:line` | §2 |
| 5 | The 62-column waterfall census **re-measured** off `roms/emerald.gba` | 62, 7 maps, byte-identical to the spec's table |
| 6 | FireRed (17) and Ruby (39) waterfall counts re-measured | reproduce — but not with the tool as banked (**O7**) |
| 7 | The **whole Dive measurement** re-derived independently from the user's ROM (my own scanner, not theirs) | 7 pairs / dims identical / **3064** dive tiles / **0** blocked twins / **5** surfacing exceptions, all `0x60`/`0x6C` — exact |
| 8 | The **shipped `fieldtrav_dive` run against the real Route 126 ↔ Underwater_Route126**, which no test does | plans real routes — and also plans a nonsense one (**O2**) |
| 9 | Emerald's Strength-boulder census re-run over all 518 maps | **10 sites, not 4** (**F1**) |
| 10 | `git diff` of `source/fieldpath.{c,h}` across the phase | **empty** — last touched at `010a138` (phase 18) |
| 11 | `git ls-files` + `git log --all` for `*.gba` / `*.sav` / `roms/` | **nothing tracked, nothing ever committed** |
| 12 | `ps`, `tools/emutest/state*/azctl.lock`, `runs*/` timestamps | no Azahar since **00:38**; lanes ran **03:38–03:52** (**O1**) |

Suite counts at `fb6d649`, all 0 failures — celiolink 1259 · control 6940 · diag 376 · excseq 677 ·
**fieldpath 1808 (FROZEN)** · **fieldtrav 3397** · netlink 66 · peersprite 62078 · presence 61376 ·
profiles 2546 · **progseq 902** · theme 83444 · tilt 1756 · touchgeom 371892 · trace 58 (+4 loud
SKIPs) · typography 1419 · uigeom 18332 · uihit 1834. **18 suites.**

> The phase-24 CWD note still applies and now covers more: `test_fieldtrav` prints a **PARTIAL
> SKIP** for TEST 22's ROM half (and TEST 23) when `roms/emerald.gba` is not openable from the CWD.
> 3397 is a project-root number.

---

## 1. Verdicts

### CONFIRMED

| Claim | What decided it |
|---|---|
| **`MB_WATERFALL = 0x13` in all three engines** | pokeemerald `metatile_behaviors.h:24` is the 20th entry of the enum opening at `:5` → 0x13 ✓ · pokefirered `:17` `#define MB_WATERFALL 0x13` ✓ · pokeruby `:23` ✓. Predicates at pokeemerald `metatile_behavior.c:995`, pokefirered `:594`, pokeruby `:1062` — all three are a single-value compare ✓. One function really does serve three engines. |
| **`badgeWaterfall`: EM 0x86E · FRLG 0x826 · RS 0x80E, each from its OWN flags.h** | EM `flags.h:1348` SYSTEM_FLAGS 0x860 + `:1366` BADGE08 (+0xE) = 0x86E ✓ · FR `:1324` SYS_FLAGS 0x800 + `:1370` **BADGE07** (+0x26) = 0x826 ✓ · RB `:779` 0x800 + `:796` BADGE08 (+0xE) = 0x80E ✓. Gates: EM `field_control_avatar.c:455` BADGE08 ✓, FR `:610` **BADGE07** ✓, RB `:511` BADGE08 ✓. The FRLG badge really is the seventh and the row really carries it. |
| **Strength badges + latches** | BADGE04 = 0x86A / 0x823 / 0x80A ✓; `FLAG_SYS_USE_STRENGTH` = EM `flags.h:1399` 0x889 ✓, FR `:1332` 0x805 ✓, RB `:817` 0x829 ✓. `ClearTempFieldEventData` clears it — `event_data.c:45` **exact** ✓. `EventScript_CheckActivatedBoulder` at `:157` **exact** ✓ (the no-yes/no branch the probe exists to avoid). |
| **The ride is MULTI-TILE, and DONE is the game's own loop condition inverted** | `field_effect.c:1873-1897` reads exactly as quoted: `ContinueRideOrEnd` re-issues the slow walk north while `MetatileBehavior_IsWaterfall(objectEvent->currentMetatileBehavior)` and only then `UnlockPlayerFieldControls()` ✓. `currentMetatileBehavior` is `+0x1E` — pokeemerald `global.fieldmap.h:248` ✓, pokeruby `:215` ✓. Modelling it as one `FtMove` for K+1 tiles is right. |
| **Descending is free, and refusing it is justified** | `sForcedMovementTestFuncs[**14**] = MetatileBehavior_IsWaterfall` and `sForcedMovementFuncs[**15**] = ForcedMovement_PushedSouthByCurrent` — the **indices are exactly right** (the cited line numbers are one off, O8). A waterfall tile takes the controls away; T5.8 covers it. |
| **The interaction is a yes/no in all three engines** | EM `field_move_scripts.inc:191`, FR `field_moves.inc:185`, RB `field_move_scripts.inc:194` — all `msgbox …, MSGBOX_YESNO` ✓ (line pointers ±1 in the doc, O8). No new cadence was needed. |
| **The defect lane W found is a real defect** | Every climbable fall tile on the user's cart reads **collision 0 / elevation 1** (I re-measured all 62). With elevation disarmed while surfing, `foot_ok` says TRUE, the dismount fires onto the fall, and the FOOT layer walks the column. Removing the new `transition()` guard costs exactly **5** checks — reproduced. |
| **DIVE is a MAP CONNECTION with identity coordinates — the design matches the engine** | `field_control_avatar.c:153` (B→emerge) / `:180` (A→dive) / `:465` / `:475` / `:965-983` `TrySetDiveWarp` using **`PlayerGetDestCoords`** ✓, and `overworld.c:756-782` `SetDiveWarp` → `GetMapConnection` → `SetWarpDestination(grp, num, WARP_ID_NONE, x, y)` ✓. Every line is where they say. **This is the failure mode the brief warned about and it is NOT present**: it is not modelled as a tile edge, and there is no FACE step. |
| **`MapConnection` stride 12 / `mapGroup` at +0x08, taken off the assembler** | pokeruby `global.fieldmap.h:129-135` really does annotate `/*0x01*/ u32 offset, /*0x05*/ mapGroup` — and it really is wrong; `pokeemerald asm/macros/map.inc:152-158` emits `.byte / .space 3 / .4byte / map / .space 2` = 12 bytes ✓. Mutations M9 (22) and M14 (21) make it load-bearing. |
| **FRLG has no Dive, confirmed four ways** | (1) `ProcessPlayerFieldInput` at `:193` has no dive hook ✓; (2) `TrySetDiveWarp` at `:1143` is `static`, **zero call sites repo-wide** ✓; (3) `data/scripts/field_moves.inc:210` is literally `@ Unused leftover from R/S` — **exact line** ✓; (4) FireRed's `map_groups.json` really has **425** maps and none underwater ✓. `MetatileBehavior_IsDiveable` survives at `:478-484` as `>= MB_FAST_WATER && <= MB_DEEP_WATER` — the trap they name ✓. |
| **No FRLG row carries an RSE Dive value** | `badgeDive = 0` for FRLG, guarded by `if (c->badgeDive && …)`. M1 (0 → 0x826) bites 3; M8 (drop the guard) bites 1. The dangerous value really is FRLG's own 0x826 = this table's `badgeWaterfall`. |
| **RS gets its own dive row from pokeruby's own file** | 0x80D = `flags.h:779` (0x800) + `:795` BADGE07 ✓; hooks at pokeruby `field_control_avatar.c:233`/`:259`, gates at `:521`/`:531` ✓. Nothing derived from Emerald's 0x86D. The phase-24 O3 third-numbering fix is intact (`flagsOff 0x1220`). |
| **The whole Dive measurement** | Re-derived with **my own** scanner over the user's ROM: 7 dive pairs, **all dimension-identical**, dive tiles per pair **0 / 188 / 0 / 1349 / 1036 / 491 / 0 = 3064**, **zero** blocked underwater twins, surfacing column **25(1) / 188(0) / 465(1) / 1350(0) / 1067(2) / 491(1) / 26(0)** and all **5** exceptions are underwater doors (`0x60`, `0x6C`). Every swimmable underwater tile is elevation 3 (or the single 0). Routes 105/125/129 really carry **0** ROM dive tiles — the abnormal-weather argument for reading the LIVE grid is sound. This table was measured, not remembered. |
| **The waterfall census** | 62 columns across 7 maps, reproduced column-for-column; Ever Grande (0,8) x15..26, fall y60..67, pool y68, landing **y=59**, K=8 ✓; Route 119's second column is collision 1 / dry top = the honest `surf_ok` refusal ✓; the tool enumerates **all 518** of vanilla Emerald's maps (I checked the group table against pret master), so "every waterfall column" is not a partial scan. |
| **FLASH is not a traversal gate** | Every citation exact: `fldeff_flash.c:72-91` (party-menu handler, `gMapHeader.cave && !FlagGet`), `:101-106` `FldEff_UseFlash`, `data/scripts/flash.inc:1-4` (three lines), `field_screen_effect.c:985-991` + `sFlashLevelToRadius` at `:53`, `battle_setup.c:704`, FR `fldeff_flash.c:164-175`. `flashLevel`/`FLAG_SYS_USE_FLASH` appear **0** times in `fieldmap.c`, `field_player_avatar.c`, `field_control_avatar.c` (EM) and FR's `fieldmap.c` ✓. Declining to implement it is correct. |
| **Strength is structurally a terminal** | `edge_at` returns a Strength edge only for `(gx,gy)`, and `bfs_pass` returns the moment the goal is dequeued, so it can never be a thoroughfare. Dropping **only** the goal-tile half of the test costs exactly **2** checks — lane W's number, reproduced. `progseq` completes on the latch and `step++` runs off the end, so the walk key is never emitted ✓. |
| **`fieldpath.{c,h}` frozen** | Byte-identical across the phase; 1808 checks unchanged. |
| **No ROMs/saves committed** | Nothing in `git ls-files`, nothing anywhere in history. |
| **Every mutation number** | 24 of 24 reproduce (one off by one: M15 = 29 here vs 28 claimed). M11 and M19 are honestly declared as **0** and both really are structurally unobservable. §5. |
| **Fiery Path's map facts** | 35×38, boulders at (10,15) **localId 2**, (17,15), (8,11), (3,12), (6,23), (5,24), all elevation 3 ✓. Ever Grande's Fly point is **(27,49)** ✓ (`heal_locations.json`). |

### OVERSTATED

**O1 — "No emulator was booted — phase 25 owns both Azahar instances." The caveat was stale, and
it wrote off a proof that was available.**
This is the exact pattern phase 24 flagged. The evidence: no Azahar process is or was running;
instance **a**'s last activity is `state/azctl.lock … at=2026-08-14T20:16Z` with its last run dir
at **23:11**; instance **b**'s last boot is `runs-b/20260815-003000`, stopped at **00:38:05Z**;
no run directory was created after that. The lanes committed at **03:38–03:52**. So instance a had
been idle ~4.5 h and instance b ~3 h, and neither lane attempted a boot. Both proof plans (§7 of
each spec) are fully banked with fixture, tap geometry and every state read — W1 in particular is
also the only way to measure the `TP_DONE_BUDGET` question the spec itself raises. The caveat was
inherited from phase 25's condition rather than re-checked.

**O2 — "the one shape that could produce nonsense … is refused explicitly" (SPEC-hm-dive §4.5, and
`test_fieldtrav` TEST 21(g)2) is a property of the synthetic world, not of the function.**
The assertion is `"a goal plain surfing already reaches is refused, not answered with a round trip
to the same tile"`. I ran the **shipped** `fieldtrav_dive` against the user's real
`roms/emerald.gba`, map **(0,41) Route 126** — something no host test does — with a synthetic
badge-07 + MOVE_DIVE party:

```
start (20,40) beh=12   goal (20,41) beh=12      <- one plain surf step away
RESULT ok=1 outcome=1(PLANNED) pair=(0,51)
  dive@(20,40) -> up@(20,41)   out/mid/back = 0/1/0
```

A one-tile underwater hop to a tile the player could have surfed to. The implemented guard is only
`ux == cx && uy == cy` (the *same* tile); TEST 21(g)2 passes because the 16×8 fixture world has
exactly one dive tile and one surfacing tile. The real safety is the tier order **in a caller that
does not exist yet**. Latent, not live — but it is precisely what would ship wrong when the
executor lands, so it belongs in §6's brief. (Positively: the same sweep planned **892** legitimate
routes out of 1681 goals, all through pair (0,51) with identity coordinates — the planner works on
real cartridge data.)

**O3 — the tap gate is the riskiest new code in the phase and has ZERO regression cover, and its
scope is wider than the argument given for it.**
`prog_tap_gate` lives in `touch.c`, and **no host suite compiles `touch.c`** — that is phase-24
finding O2, which phase 25 closed for the sequencer (`progseq`/`excseq`) and which this phase
re-opens for the gate. Uncovered: the 2/1/0/−1 classification, the `gate == -1` "plan **nothing**"
path (a tap that used to do something now does nothing), the `prog_retargeted_goal()` fallback,
`prog_on_waterfall`, `prog_strength_on`. Separately, the justification given for `gate == 1`
("tier 0 wins with a *wrong* answer — a swim up the fall") only covers taps involving a fall, but
the rule fires for **every** upward mid-surf tap whenever Waterfall is usable. `fieldtrav_plan`'s
own K=0 pass mostly re-derives tier 0 and returns `FT_OUT_TIER0` (which is what keeps H1.7
structural), but it is *not* `fieldpath_plan`: where the two disagree — a blocked goal, which
`fieldpath` accepts as a terminal and `fieldtrav` does not — an interact program can now be
preferred to a plain walk-and-hold. No test can see it, and no emulator has.

**O4 — the shipped default is never restated, and the settings doc now under-describes level 1.**
Phase-24 owed item #1. `theme.c` still ships `smartTraverse 0 = Off`, so **nothing in phase 26 does
anything on a default install** — not Waterfall, not Strength, not the tap gate. Neither spec says
so where the feature is described (only §7.0 mentions setting it ≥ 1 for the proof). And
`theme.h:59-64` still documents level 1 as "conditional edges on the CURRENT map (Surf
mount/dismount, Cut, Rock Smash)" — Waterfall and Strength were added to that level without
updating the text a user reads.

**O5 — "added Waterfall and Dive to the traversal layer" is only half true for Dive.**
Lane V's own §6 says this plainly and honestly ("NOT shipped, deliberately: the executor and the
tier wiring"), and I confirmed it: `fieldtrav_dive` has **no caller** outside the test file,
`touch.c`/`progseq.*`/`g_prefs` carry no `FT_HM_DIVE` path. The overstatement is in the phase-level
framing, not in lane V's document. After phase 26 the app still cannot dive.

**O6 — counted claims that are simply wrong numbers.**
(a) SPEC-hm-dive §8 and the BUILDLOG say **"17 host suite binaries"**; there are **18**, and all 18
are green — lane W's count is the right one. (b) Lane W's "fieldtrav 3397 (**2805** before this
lane)" is true only of the shared working tree mid-phase; the phase baseline at `7394be4` is
**1210** (measured), which is the number lane V quotes. (c) "Ruby: 39 columns, **the same maps as
Emerald**" — the count is right (I measured 39) but the maps are not the same set: Ruby lacks
Battle Frontier Outside East (20) and Safari Zone Southeast (3), which is exactly 62 − 23 = 39.

**O7 — the banked recon tool cannot reproduce the FRLG number it is banked for.**
`tools/recon-phase26/wfscan.c` hardcodes Emerald's 34-group table **and** `FP_ENG_RSE`. Run exactly
as its README documents against `roms/firered.gba` (either gMapGroups revision) it reports
**0 columns**. The claimed **17** is correct — I reproduced it only after supplying FireRed's
43-group table *and* `FP_ENG_FRLG` (FRLG metatile attributes are read differently). §7.3 hands the
next session a tool that will silently answer zero on two of the five shipped titles.

**O8 — a cluster of citation line-drift in the specs (values right, pointers 1–2 lines off).**
`field_move_scripts.inc:184` (actual 185, `EventScript_UseWaterfall`), `:190` (191, the YESNO),
`:123-133` and `:145` (124-137 and **147**, `setflag FLAG_SYS_USE_STRENGTH` — repeated in
`progseq.c` and `progseq.h`), `:125` (126, the BADGE04 line), FRLG `field_moves.inc:124` (122),
`field_player_avatar.c:159/185` (160/184 — the **indices** [14]/[15] are right), `fldeff_flash.c:73-74`
(74-75). Worth naming because this project's own rule is that a citation is checkable: shipped-code
comments came out noticeably better than the prose (`fieldtrav.c` names 126 / 122 / 157 correctly).

**O9 — one tap in the live-proof plan sits exactly on a tile boundary.**
W2 prescribes `t 160 104` for ddy = −1. The plan's own formula (`40 + (5+ddy)*16 + 8`) gives
**112**; row 4 spans y = 104..119, so 104 is the first pixel of the tile and 103 is a different
tile. W1's `t 160 64` is correctly centred. Use 112.

### FALSE

**F1 — "Fiery Path is the only Strength boulder site in Emerald outside Seafloor Cavern
(Dive-gated), Victory Road and the Trick House — measured by scanning every `ObjectEventTemplate`
with `graphicsId == 87` across all 34 map groups" (SPEC-hm-waterfall §7.2).**
I re-ran that scan with the lane's own banked tool over all 518 maps. Boulder sites:

| map | name | boulders |
|---|---|---|
| (24,14) | FieryPath | 6 |
| (24,28/29/30/32/35) | SeafloorCavern Rooms 1/2/3/5/8 | 2/6/7/4/12 |
| (24,44) | VictoryRoad_B1F | 8 |
| **(24,49)** | **ShoalCave_LowTideLowerRoom** | **1** |
| **(24,86)** | **MagmaHideout_1F** | **3** |
| (29,6) | Route110_TrickHousePuzzle4 | 10 |

**Ten sites, not four.** Shoal Cave and the Magma Hideout are neither Seafloor Cavern, nor Victory
Road, nor the Trick House, and neither is Dive-gated. The claim is contradicted by the very
measurement it cites. Consequence is small — it only bears on the *choice* of proof site, and
Fiery Path is still a good one — but an exhaustive claim that its own scan refutes is a false
finding, not a nit. (Magma Hideout 1F is the better second site: three boulders, no tide.)

**F2 — "`FLAG_SYS_USE_FLASH` (Emerald `flags.h` SYSTEM_FLAGS + 0x2A)" (SPEC-hm-waterfall §6.5).**
It is **SYSTEM_FLAGS + 0x28** = 0x888 (`flags.h:1398`). `+0x2A` is `FLAG_SYS_WEATHER_CTRL`
(`:1400`). Harmless — nothing reads it and Flash is deliberately unimplemented — but it is a wrong
constant in a document whose whole purpose is that its constants are right. (Lane V's Flash
section, which is the one that settles the question, cites no flag id and is entirely correct.)

**F3 — "455 of Emerald's **869** maps take that branch" (`source/fieldtrav.c`, the
`fieldtrav_connection` comment).**
Vanilla Emerald has **518** maps (pret master `map_groups.json`, fetched and summed; the local
PokeDNA clone has 519 because it carries an added `newdaycare` map). Measured on the user's own
BPEE ROM over all 518 slots: **454** carry a non-ROM (NULL) `connections` pointer. Both numbers in
the comment are wrong; the shape of the claim (most maps have none) is right. Documentation only.

---

## 2. The new constants, each re-read in its own decomp

| Constant | Emerald | FRLG | Ruby/Sapphire | Verdict |
|---|---|---|---|---|
| `MB_WATERFALL` | `metatile_behaviors.h:24` (enum, 20th) = 0x13 | `:17` 0x13 | `:23` 0x13 | ✅ each in its own header |
| Waterfall gate | `field_control_avatar.c:455` BADGE08 | `:610` **BADGE07** | `:511` BADGE08 | ✅ |
| `badgeWaterfall` | 0x86E (`flags.h:1348`+`:1366`) | 0x826 (`:1324`+`:1370`) | 0x80E (`:779`+`:796`) | ✅ |
| `badgeStrength` | 0x86A (`:1362`) | 0x823 (`:1367`) | 0x80A (`:792`) | ✅ |
| `strengthLatch` | 0x889 (`:1399`) | 0x805 (`:1332`) | 0x829 (`:817`) | ✅ |
| `badgeDive` | 0x86D (`:1348`+`:1365`), gate `field_control_avatar.c:465`/`:475` | **0** — named zero + guard | 0x80D (`:779`+`:795`), gate pokeruby `:521`/`:531` | ✅ no cross-engine carry |
| `MOVE_WATERFALL` / `MOVE_DIVE` | `moves.h:131` = 127 · `:295` = 291 | — | — | ✅ |
| Diveable set | `metatile_behavior.c:853-861` → 0x11/0x12/0x14 | none (predicate survives at `:478-484`) | `:927-935` → same three from **its own** `metatile_behaviors.h:21/22/24` | ✅ |
| Cannot-emerge | `:863-877` → 0x19/0x2A, `MB_WATER_DOOR` only under `#ifdef BUGFIX` | none | `:937-944` → 0x19/0x2A from `:29`/`:46` | ✅ incl. the BUGFIX reasoning |
| `CONNECTION_DIVE/EMERGE` | `constants/global.h:153`/`:154` = 5/6 | same | same | ✅ |
| `MAP_TYPE_UNDERWATER` | `map_types.h:9` = 5 | same | pokeruby `:9` | ✅ |
| `MapHeader` +0x0C / +0x17 | `global.fieldmap.h:176`/`:182` | same block | pokeruby `:148`/`:154` | ✅ |
| `MapConnection` stride 12, mapGroup +0x08 | `asm/macros/map.inc:152-158` | — | pokeruby's C comment **is** wrong, as claimed | ✅ |
| `currentMetatileBehavior` +0x1E | `global.fieldmap.h:248` | same block | pokeruby `:215` | ✅ |
| `SaveBlock1.flags` | 0x1270 (`global.h:1020`) | 0x0EE0 (`:790`) | 0x1220 (`:701`), `vars[]` 0x1340 (`:702`) | ✅ phase-24 O3 fix intact |

**Zero engine-boundary crossings found.** The one place a reader could have been fooled — pokeruby's
`data/field_move_scripts.inc`, which is **not** in the local sparse clone (`include/ src/ graphics/`
only) and whose fetch in `/tmp/pret/rb/` is a 14-byte `404: Not Found` — I fetched myself from
pret master: `S_PushableBoulder` at `:124-139` with BADGE04 at **`:126`** (the value `fieldtrav.c`
cites, exactly) and `S_UseWaterfall` at **`:187-199`** with the YESNO at **`:194`** (both exactly as
cited). The Ruby citations are right despite the failed fetch.

---

## 3. The measurements, re-measured

| Lane claim | My independent measurement |
|---|---|
| 62 waterfall columns, 7 maps, all collision 0 / elevation 1 | **62**, maps (0,8)×12 (0,29)×4 (0,34)×6 (24,0)×11 (24,45)×6 (26,13)×3 (26,14)×20; runs 3×33, 4×6, 5×8, 6×3, 8×12 ✅ |
| Ever Grande K=8, fall y60..67, landing y=59, Fly (27,49) | ✅ exact, all 12 columns |
| Route 119's second column is decorative | fall collision **1**, top behaviour **0x00** collision 1 ✅ |
| 3 of 7 sites are taller than a tap (ddy ≥ −5) | runs 5, 6, 8 exceed K ≤ 4 ✅ (`touch.c:559` really is `gy/16 - 5`) |
| FireRed 17 / Ruby 39 columns | **17** / **39** ✅ (FR only after fixing the tool — O7) |
| 7 dive pairs, dimension-identical | ✅ 40×80, 80×80, 80×40, 80×80, 80×80, 120×40, 80×40 — both sides |
| 3064 diveable tiles, zero exceptions in the down direction | **3064**, **0** blocked twins ✅ |
| Surfacing column 25/188/465/1350/1067/491/26 with 5 exceptions | ✅ **exact**, and all 5 exceptions are `0x60`/`0x6C` doors, as claimed |
| Every swimmable underwater tile is elevation 3 (or the single 0) | ✅ 0 counter-examples in all 7 pairs |
| Routes 105/125/129 carry no ROM dive tile | ✅ 0 each — the live-grid argument holds |
| 14 Emerald maps take the `setdivewarp` path | 14 underwater maps exist, **7** have no `CONNECTION_EMERGE`; +7 surface twins = 14 ✅ consistent |
| Fiery Path 35×38, boulder localId 2 at (10,15) | ✅ |
| "the only Strength site outside …" | ❌ **F1** — 10 sites |

---

## 4. Suites

Every count in both specs reproduces exactly (§0). `fieldpath` is 1808 and byte-identical.
The pre-phase baseline is 1210 (lane V's number), not 2805 (O6b).

---

## 5. Mutations — all 24 re-run

**Lane W (5/5 reproduce).**

| mutation | claimed | measured |
|---|---|---|
| `fieldtrav_is_waterfall` always false | 15 | **15** |
| T5.8 guard removed from `transition()` | 5 | **5** |
| `edge_at` makes Strength a thoroughfare (goal-tile half only) | 2 | **2** |
| ride completes on first movement (drop `!onWaterfall`) | 6 | **6** (progseq) |
| Strength proven by object slot instead of the flag | 4 | **4** (progseq) |

*(Dropping the whole `if (!strengthGoal || x != gx || y != gy)` line costs 5, and dropping only the
`strengthGoal` half costs 1 — the claimed "2" is the goal-tile half, which is the mutation the
sentence describes.)*

**Lane V (19/19 reproduce).** M1 3 · M2 4 · M3 13 · M4 1 · M5 13 · M6 1 · M7 5 · M8 1 · M9 22 ·
M10 9 · M11 **0** · M12 1 · M13 1 · M14 21 · M15 **29** (claimed 28 — mutation-shape difference) ·
M16 1 · M17 1 · M18 1 · M19 **0**. The two declared non-biters really are non-biters, and both
explanations hold: every `FT_BFS_WATER` caller passes `pElev = 0` structurally (M11), and a
self-directed dive is caught twice over (M19).

**These are goldens testing the truth, not the code.** TEST 22 is graded against the user's own
Emerald ROM (real map bytes), which is the strongest form available here. TEST 20/21 are graded
against a hand-built 16×8 world — internally consistent and well mutated, but see O2 for what that
cannot prove.

---

## 6. What is NOT proven

1. **Nothing in phase 26 has ever executed.** Not on hardware, not in an emulator — and, unlike
   phase 25, not because the emulator was busy (O1).
2. **`3DGBA.cia` is stale again** — 14 Aug 23:18 vs `3DGBA.3dsx` 15 Aug 03:52. CLAUDE.md rule 1
   makes the `.cia` the real target, so a hardware test today would install pre-phase-26 code.
   This is the phase-24 §3 finding recurring.
3. **`touch.c` still has no host coverage** (O3): the tap gate, the −1 refusal, the retarget
   fallback and both new observables are emulator-only.
4. **The frame budget is inherited and untested.** `TP_DONE_BUDGET` is 480 emulated frames; an
   8-tile ride is 8 slow walks plus the show-mon cutscene. Ever Grande is the longest fall in the
   game — W1 is the measurement.
5. **The `movementDirection` vs `facingDirection` proxy** is unobserved (the spec says so).
6. **Dive has never been planned against real cartridge data by any test** — I did it here for the
   first time; it plans, and it also plans a one-tile round trip when asked out of turn (O2).
7. **RS and FRLG are table-only** for Waterfall; RS traversal does not execute at all.
8. **Two named-and-open defects remain**: the currents (0x50–0x53) twin inside `fieldtrav`'s SURF
   tier, and — the one that matters most — **the frozen `fieldpath` will still plot a swim up a
   waterfall at the shipped default** (`smartTraverse = 0`), because the containment is entirely in
   the tap gate, which only runs at level ≥ 1.
9. **`COVERAGE.md` T5.8 still reads "Unmitigated"** though this phase mitigated its waterfall half
   in `fieldtrav`; the coverage doc was not synced.

---

## 7. The eight HMs after phase 26 — plainly

**Read the first column with the toggle in mind:** everything in the traversal layer needs
**Settings → smart traverse ≥ 1**, and the shipped default is **0 = Off**. Tap-to-Fly is a
different subsystem and is not gated by it.

| HM | Planned by touch? | How it is modelled | Proof status |
|---|---|---|---|
| **01 Cut** | **Yes** (level ≥ 1) | object edge — the tree's object slot deactivates | **Live-proven** (phase 24 P2, emulator) |
| **02 Fly** | **Yes**, via a different system | region-map screen driver, tap-to-fly + the game's own acceptance test | Live-proven (phase 24 lane B2) |
| **03 Surf** | **Yes** (level ≥ 1) | metatile edge — mount at the shore | **Live-proven** (P1, re-proven phase 25 on the extracted sequencer) |
| **04 Strength** | **Yes, as a TERMINAL only** (new) | tap the boulder = the game's activation (badge + move + yes/no + `setflag`). The router **never** plans a route *through* a boulder and **never** pushes one | Host-graded + mutation-graded. **Still never executed** — phase 28 lane X tried and did not reach a boulder in budget: Route 112 from the Lavaridge side reaches only the Jagged Pass warp, and Jagged Pass stops at the muddy slope (x=9, y=30..32, beh 0xD1) that needs a Mach Bike. The live channel now exists though — `progLatch` + `progObjSlot/X/Y` mirrors — and the Route-111 approach is banked: LANE-X-EXECUTE.md §3 |
| **05 Flash** | **No — deliberately** | not a traversal gate at all: it changes no tile's walkability. Settled with citations; nothing implemented | Question closed; nothing to prove |
| **06 Rock Smash** | **Yes** (level ≥ 1) | object edge | **Live-proven** (phase 24 P3) |
| **07 Waterfall** | **Yes** (new, level ≥ 1) | **multi-tile** edge: one interact carries the player up the whole column; goal-retarget so tapping any tile of a fall means "take me up it"; DONE = the game's own ride condition inverted | **PLANNER LIVE-PROVEN** (phase 28 lane X, Route 114 on the emulator: `progRetarget=1` / `progGoal=(12,9)` from a tap on (12,12), `progHm=4`, `progUsable` bit 4 0→1 across the HM07 teach, and a ride that lands on exactly that tile). **EXECUTOR FAILS** — `progPhase` sticks in DLG, `progAKeys` 18, `progAnswers=0`, `progEnd=TIMEOUT`: the FACE hold swims the player ONTO the fall (`pos` caught at (12,12) mid-program) and every A then lands on a forced-movement frame. LANE-X-EXECUTE.md defect X1 |
| **08 Dive** | **No — planner only** (new) | correctly modelled as a **map connection with identity coordinates** (not a tile edge, not a warp). `fieldtrav_dive` exists and works; **`touch.c`, `progseq` and `g_prefs` are untouched, so nothing in the app can emit a dive step** | Host-graded in a synthetic world + 19 mutations, and now on the REAL Route 126 pair too (`test_fieldtrav` TEST 24, phase 28 lane X) — where O2's nonsense round trip is fixed at the source: `fieldtrav_dive` answers `FT_OUT_TIER0` for any goal the player can already reach in the mode they are in. **No executor, still never executed** |

So: **five of eight are planned by touch today** (Cut, Fly, Surf, Rock Smash, Waterfall) plus
Strength as an activation-only terminal — **four of those six have ever actually run** (Cut, Fly,
Surf and Rock Smash are proven end to end; **Waterfall has now run as far as its plan**, which is
where lane X found that its executor is broken; Strength still has not). **Flash is deliberately
out. Dive is built but not connected.**

> **Updated 2026-08-15 by phase 28, lane X** (LANE-X-EXECUTE.md). Waterfall moved from "never
> executed" to "planner proven, executor failed" — a better outcome than either half of the old row,
> because the failure is now a measured defect with a two-read cause instead of an unknown. §8 item 1
> ("run W1 and W2") is DONE for W1 and open for W2; items 3, 5, 6, 7, 8 and 9 are done; item 2 (the
> `.cia`) and item 4 (the `gate == 1` scope) are addressed in lane X's §2 and §5.

**What remains unproven on hardware or in an emulator, in one list:** Waterfall end-to-end (the
ride, the retarget, the 480-frame budget on Ever Grande's K=8 climb, the FACE-direction proxy);
Strength activation (the latch flipping, and the boulder *not* moving); the whole tap gate in
`touch.c` including its "plan nothing" refusal; Dive in every respect (there is no executor to
test); Waterfall on Ruby/Sapphire and FireRed (table-only); and *every* phase-26 claim on real
3DS hardware — `3DGBA.cia` has not been rebuilt since 14 August.

---

## 8. What is owed

1. **Run W1 and W2.** The emulator was free during the phase and is free now (O1); the plans are
   complete. W1 also settles the frame budget. Fix the W2 tap to `t 160 112` first (O9).
2. **Rebuild the `.cia`** before anything is installed (§6.2) — same item phase 24 raised.
3. **Give `prog_tap_gate` a barrier** (O3), the way phase 25 extracted `progseq`/`excseq`. The
   classification is pure: tile behaviour + surf bit + `fieldtrav_strength_tap` in, 2/1/0/−1 out.
4. **Narrow or defend the `gate == 1` rule** for upward mid-surf taps that involve no fall (O3).
5. **Before wiring the Dive executor, add the tier-order precondition to `fieldtrav_dive` itself**
   (or a "the goal is already reachable in the home mode" refusal), and re-test it against a real
   map pair, not only the synthetic world (O2).
6. **Correct F1** (ten boulder sites; Magma Hideout 1F is the cleaner second proof site), **F2**
   (`FLAG_SYS_USE_FLASH` = SYSTEM_FLAGS + 0x28) and **F3** (`455 of 869` → `454 of 518`).
7. **Fix `wfscan.c`** to take the engine and the group table as arguments (O7), so §7.3's banked
   tool cannot silently answer zero.
8. **Restate the default and update `theme.h`'s level-1 description** to include Waterfall and
   Strength (O4), and sync `COVERAGE.md` T5.8 (§6.9).
9. **Fix the line-number drift** in both specs (O8) and the "17 suites"/"2805"/"same maps as
   Emerald" counts (O6).

---

_Method note, for the record: three findings here (O2, F1, F3) came from re-running the lanes' own
measurements rather than from reading their prose. Every measured table in both specs that I could
re-derive came back **exact** — the dive census to the tile. The failures are all in claims that
were reasoned rather than measured, or measured once and then generalised._
