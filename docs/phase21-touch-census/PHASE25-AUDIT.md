# PHASE 25 — ADVERSARIAL AUDIT

_Auditor pass, 2026-08-15. Lane **C** audited at its close-out commit **`7394be4`** (on `main`);
lane **D** at its close-out commit **`2cc34ee`** (branch `lane-d2-ph25`, **not merged to main**).
Both were checked out into detached audit worktrees so nothing below was run against a dirty tree.
No Azahar was running (`pgrep -x -U <uid> azahar` → none), so the emulator was left alone;
everything here was decided by re-running host code, re-reading the shipped ROMs and sym maps,
re-scanning pret's own data files, parsing the fixture saves, hashing the evidence, and diffing
the commits._

The brief was to disbelieve, and to hunt the specific failure modes the phase-24 audit found.
Three of those recurred in weakened form. **Three claims are outright false**, and all three are
the *same* failure — the "stale caveat": a screen written off as unreachable on a premise nobody
re-checked, in a phase where the sibling lane disproved exactly that kind of premise with a
four-minute desk scan.

---

## 0. What I actually ran (nothing here is quoted from the lanes)

| # | Check | Result |
|---|---|---|
| 1 | All **18** host suites rebuilt and re-run from an audit worktree at **`7394be4`** (lane C tip) | 0 failures; every count reproduces — §1 |
| 2 | All **18** rebuilt and re-run at **`2cc34ee`** (lane D tip) | 0 failures; `profiles 2746`, `touchgeom 1040833` |
| 3 | `profiles` + `touchgeom` re-run at **`2eca9c5`** (lane D2's pre-fix commit) | 2738 / 1040833 — D2 Entry 6's per-suite numbers are real |
| 4 | **17 targeted mutations** of the code the four new/grown suites cover | **17/17 went RED** — §2 |
| 5 | Every new address resolved against the **byte-matched sym maps**, and the maps md5-checked against the lanes' own copies | all exact; maps identical — §3 |
| 6 | ROM headers at 0xAC/0xBC for all five carts in `roms/` | BPEE r0 · BPRE **r1** · BPGE **r1** · AXVE r2 · AXPE r2 — matches every map the lanes cited |
| 7 | rev0-vs-rev1 delta for **every** FR/LG symbol shipped this phase | all 14 differ between revisions; the lanes used rev1 = the user's cart — §3 |
| 8 | SHA-256 of every PNG in `evidence/impl/` on **both** trees (90 files / 105 files) | one duplicate pair, the phase-23 one lane C1 itself reported. **No new duplicate.** — §4 |
| 9 | Opened 6 load-bearing captures and checked the described detail against the pixels | all 6 show what they claim — §4 |
| 10 | Independent re-run of lane D1's **pret metatile scan** (70 tilesets + every layout, tileset-filtered) | reproduces D1's `MB_REGION_MAP` table exactly — and disproves lane C1's E19 caveat — §5 |
| 11 | Gen-3 save parse of `emerald-fix.sav` (party **and all 14 PC boxes**) and `firered.sav` | 195 box mons C1 never looked at — §5 |
| 12 | `git diff 010a138 <ref> -- source/fieldpath.{c,h}` on `main` and both lane branches | **empty** — frozen since phase 18 |
| 13 | `git log --all --diff-filter=A` + `git ls-files` for `*.gba` / `*.sav` | **nothing tracked, nothing ever committed, on any ref** |
| 14 | SHA-256 of the user's own `~/Library/Application Support/Azahar/sdmc/3DGBA/*` vs the hashes lane C quoted | **both match exactly** — §6 |
| 15 | `touch.c` grep for any surviving copy of the extracted decisions | none — the shipped executor *is* `progseq`/`excseq` |

Suite counts at **`7394be4`** (all 0 failures): celiolink 1259 · control 6940 · diag 376 ·
**excseq 677** · **fieldpath 1808 (UNMODIFIED)** · fieldtrav 1210 · netlink 66 · peersprite 62078 ·
presence 61376 · profiles 2546 · **progseq 877** · theme 83444 · tilt 1756 · touchgeom 371892 ·
trace_replay 58 (+4 loud SKIPs) · typography 1419 · uigeom 18332 · uihit 1834 = **617 948**, which
is exactly what `085644e`'s commit message claims. Lane C1's 16-suite **616 394** also sums.

> Methodology note for the next lane, on top of phase 24's `test_fieldtrav`-needs-the-CWD one:
> **`test_typography` needs `data/` too.** In a bare worktree it reports *159 checks, 9 failures*
> because the `.bin` font atlases are gitignored. It is honest about it (`T15 cannot open data/`),
> but a green typography number is only reproducible from a tree that has `data/` present. That
> briefly looked like a lane failure; it is not.

---

## 1. Verdicts

### CONFIRMED

| Claim | What decided it |
|---|---|
| **Both gates are real.** 18 suites, 0 failures, at both lane tips | Checks 1–2. Every per-suite count reproduces to the digit, including `fieldpath 1808` unmodified. Lane C2's `617 948` and lane D2's close-out `1 287 089` both sum from their own listed numbers. |
| **Audit O2 is genuinely closed — `progseq`/`excseq` are barriers, not token tests** | This was the phase's biggest structural claim and the one most worth faking. **17 of 17 targeted mutations go red** (§2), including all six phase-24 fixes the audit named. Undoing `2dde03e` inside the *shipped* file produces 10 failures whose text is `THE FIX: the game's own yes/no task really pressed YES … which took more than one edge (1)`. That is a golden testing the truth. |
| **The extraction is not a second copy** | `touch.c` calls `progseq_*`/`excseq_*` at 13 sites and retains none of the extracted logic (no `TP_ANSWER_EVERY`, no `TPH_*` phase machine, no dims-history rule). The only references left are one struct initialiser and one `s_seq.phase == TPH_FACE` read-gate — the "expensive reads come back as requests" pattern C2 described. |
| **Every new address is right for the ROM actually in `roms/`** | §3. 25 Emerald + 14 FireRed/LeafGreen values, all resolving to the exact symbol at the exact address. **No sibling-engine arithmetic anywhere** — I specifically checked the LG values C1 quoted in Entry 0 and the one that looks like a copy-paste (`CB2_PrintErrorMessage` 0x0800AF40 in both FR rev1 and LG rev1) is a genuine coincidence, verified in both maps. |
| **`gQuestLogState` 0x0203ADFA really is rev-insensitive** | Present at that exact address in `pokefirered.sym`, `pokefirered_rev1.sym`, `pokeleafgreen.sym` **and** `pokeleafgreen_rev1.sym`. C1's "needs no rev-alternate" is correct, and it is correct *for the stated reason* (it is EWRAM, not ROM) — the one phase-25 value LeafGreen legitimately inherits. |
| **Lane D1 handled the revision split properly** | `rmCbAlt = 0x080C08B4` is truly `CB2_RegionMap` **rev0**, paired with rev1's `0x080C08C8`. Every other FR symbol this phase differs between revisions (all 14 checked) and the lanes used rev1 throughout, which is the user's cart. |
| **No duplicate evidence this phase** | Check 8. All 27 `*P25*` captures are distinct on both trees. The only duplicate pair in either tree is the phase-23 one **lane C1 itself found and reported** (`a388d48`) — the house's worst failure mode was hunted by the lane before the auditor got there. |
| **The captures show what they say** | I opened `FR-P25-D1-townmap-open`, `FR-P25-D1-tap-pallet`, `EM-P25-D2-pokenav-open`, `EM-P25-D1-wall-tap-littleroot-no-fly`, `EM-P25-hof-pc-replay-cb2`, `EM-P25-mail-read-cb2`, `FR-P25-questlog-inert`, `EM-P25-C2-p4-hot-spring`. Every stated detail is in the frame — §4. D1's *predicted* pixel positions are the strongest of these: SWITCH at capture (612,402) and CANCEL at (612,450) land dead on the two drawn buttons, and the mail letter reads `BE NICE / TO PLUSLE / ! VOLBEAT / WILL BE / FANTASTIC. From ROMAN` word for word. |
| **Lane D1's metatile scan is sound** | I wrote an independent scanner (70 tilesets → layouts, filtered by secondary tileset) and it reproduces D1's `MB_REGION_MAP` result **exactly**: Littleroot Brendan's/May's 2F (2,1)/(6,1), `POKEMON_CENTER_1F` and `LAVARIDGE_TOWN_POKEMON_CENTER_1F` (11,1)/(12,1), `EVER_GRANDE_CITY_POKEMON_LEAGUE_1F` (12,1)/(13,1). B7's "found by a pret metatile scan" is real work, correctly done. |
| **B7 VERIFIED is earned** | The wall map was reached, the cursor walked 8 presses for a Chebyshev-8 route, and `mapFlies` stayed **0** on a cell whose live `mapSecType` is 2 = `CITY_CANFLY` — i.e. the driver declined an A the *game would have accepted*. That is rule M2 proven on the only screen that can prove it, and the capture corroborates it (cursor on Littleroot, player icon still at Lavaridge, map still open). |
| **D1's "no Fly user in the FR party" caveat is TRUE** | I parsed `firered.sav`: Mewtwo Lv70 · Articuno Lv50 · Hoppip Lv10 · Zapdos Lv50 · Mew Lv30 · Moltres Lv50 — **none knows FLY** (move 19). D1's description of the party is accurate to the level, and its conclusion holds. (Method note only: this was one save-parse away from being *established* rather than left open after three blind party-menu attempts — the lane's own proposed next step.) |
| **C1's Emerald party measurement is accurate** | Parsed: Tyranitar 73 · Salamence 72 · Dragonite 74 · Milotic 73 · Lugia 71 · **Zigzagoon Lv9**. Exactly "five final-form Lv71-74 + a Lv9 Zigzagoon". The *party* half of the C20 measurement is honest. (The rest of it is not — F3.) |
| **`fieldpath.{c,h}` frozen** | Byte-identical to `010a138` (phase 18) on `main`, `lane-d1-frmap` and `lane-d2-ph25`; 1808 checks unchanged everywhere. |
| **No ROMs or saves committed** | Nothing in `git ls-files`, nothing added anywhere in history on any ref. Lane D's 29-file diff is source, tests, docs and 10 PNGs. |
| **User data restored** | §6. The user's own `settings.bin` hashes **`85dd487e…`** and `recent.bin` **`72c100ad…`** — the exact values lane C quoted. Both instances' `qt-config.ini` are 28545 B at their pre-lane mtime. Instance a is ROM-less (`clean-fixtures` worked; the phase-24 §3 residual is *remediated* there). Instance b is fully isolated — own app bundle, own `sdmc`, own `config`, own log. |
| **The INERT rules really do run first** | `gamestate.c:858-866` — the quest-log test and the `cb2Inert` loop both `return true` before the battle test at :869. "cannot be overridden by something more specific" is true as written. |
| **The `GS_CTXN` latent bug was real** | The table ended at `"stor"` (index 13) and `GCTX_MAP` is 14. Every phase-24 region-map log row really did print `?`. Found while in the file and reported rather than buried — the behaviour the house wants. |
| **The `excseq` dims-history residue is real and honestly pinned** | `excseq_boundary` does not clear `lastW/lastH`, so the first post-boundary frame compares the old map's dims with themselves and looks settled. `test_excseq` TEST 6 asserts the residue explicitly (`"and the residue is real: the stale dims history makes frame 0 look settled"`) instead of hiding it. A suite that reports a defect in the code it was written to protect is the opposite of a rubber stamp. |
| **H7's "ZERO new addresses" is literally true** | `dc46795` adds only `0x0813591C`, which was **already** the ninth entry of BPEE's `cb2FullUi` before the commit. The `promo+substate` symbol cost really was illusory. |
| **D2's own "the measurement found a defect" story checks out** | `navSteps 4` for a two-row hop is a real over-count, the cause cited (`pokenav.c:449-457` parking in case 2 while the looped task runs) is in the source, and the fix splits re-sends into `navRepress` so `navSteps` stays exact. The re-proof run's totals (2+2+1 = 5) are internally consistent. |

### OVERSTATED

**O1 — K4's "the identical script on the identical channel" is not identical, and it is the sentence doing the load-bearing work.**
Entry 4 argues: *"The second row is what makes the first one evidence rather than an absence: the **identical script** on the identical channel DOES something the moment the guard stops applying"*, and Entry 5 repeats *"after playback the **same script** → `planSeq` 0→2"*. The app's own control log from that very boot
(`runs/20260814-191846/netlogs/3DGBA_control_0101_020023.txt`) records **two** batches:

```
[ctl touch] picked up 8 touch ops      <- during playback
[ctl touch] picked up 4 touch ops      <- after playback
```

Eight ops, then four. Entry 4's own table meanwhile calls the second one "2 taps". Three different
accounts of the same thing, none of them "identical". The A/B is still a *valid* control — same
channel, and the after-batch demonstrably armed routes where the during-batch armed none — but it
is a different script, and the word "identical" was chosen to make the control airtight when it
was not. Related and milder: K4's deltas (`planSeq`, `dlgTaps`, `dlgHolds`, `curKeys`) are all
**our own counters**; the game-side reads (`ctx=15`, `qlState=2`) prove the *classification*, not
the *silence*. For an INERT class a game-side delta is impossible by construction, so this is a
definitional stretch rather than a fake — but the row should say so instead of borrowing the
strictness of E15/E8, which really do have game-side deltas.

**O2 — lane D1 Entry 1's suite total does not sum from its own numbers.**
Entry 1 quotes "**950 342** checks"; the 18 numbers printed in the same code block sum to
**952 312**, which is what Entry 4's close-out gate correctly says and what I measured. A 1 970-check
discrepancy inside one document.

**O3 — lane D2 Entry 6's suite total does not sum from its own numbers either.**
Entry 6 quotes "**1 288 241** checks"; its own listed numbers sum to **1 287 081** (off by 1 160).
The *per-suite* values in Entry 6 are honest — I re-ran `profiles` and `touchgeom` at `2eca9c5` and
got exactly **2738** and **1040833** — so this is arithmetic, not fabrication. Entry 11's close-out
(`1 287 089`, profiles 2746) is correct and reproduces. Counted claims miscounted twice in one
phase is the phase-24 **O8** pattern recurring.

**O4 — lane D1's stated base commit is wrong, and it produced a real defect.**
The header says the branch is *"based on `main` @ `6d70c48`, which already contains every
lane-B1/B2 commit"*. The actual merge-base is **`7394be4`** — lane C2's tip — which Entry 1 states
correctly, contradicting the header three pages earlier. This is not cosmetic: `6d70c48` predates
lane C1, so a reader taking the header at face value expects `profiles 1697`, not the 2546 baseline
D1 actually inherited. And it left a live artefact — **`test_profiles` now prints two different
"TEST 20" banners**, because D1 numbered its FireRed region-map test TEST 20 without noticing lane
C1 had already taken that number for the INERT class:

```
TEST 20: phase-25 INERT class (credits cb2 + FRLG quest-log playback)
TEST 20: phase-25 FAM-MAP second engine (FireRed region map)
TEST 21: phase-25 FAM-NAV (the PokéNav menus)
```

**O5 — the `.cia` is stale again.**
`3DGBA.cia` is **14 Aug 23:18**; `3DGBA.3dsx`/`.elf` are **15 Aug 03:52**. The `.cia` does contain
lane C's code (its last code commit `085644e` is 22:58), but it is behind everything on `main`
since, and CLAUDE.md rule 1 makes the `.cia` the real target. This is the phase-24 §3 finding
returning after being fixed once — it needs a `make cia` in the close-out checklist, not a fix each
phase.

**O6 — instance b was left with 16 MB of ROM fixtures staged.**
`az-b/user/sdmc/3DGBA/` still holds `gameA.gba`/`gameB.gba` (16 MB each) plus `gameB.sav`. Neither
lane D report *claims* it ran `clean-fixtures` — both say only "fixtures: originals re-hashed, all
untouched", which is true and which I verified (the `roms/` originals are all still 13 Aug 15:54).
But the phase-24 audit named exactly this residual for instance a, lane C then cleaned instance a
properly, and instance b inherited it. It is a harness-dir copy of gitignored files, so the risk is
disk, not correctness — but it should be symmetric.

**O7 — every lane-D claim is about a branch that is not on `main`.**
`lane-d2-ph25` is unmerged (`git branch --merged main` shows no lane-d). The FireRed region map,
FAM-NAV and the Pokéblock case ship nowhere yet, and the two "TOUCH-PLAN … DONE" rows are DONE on a
branch. The report reads as shipped work; it is staged work.

### FALSE

All three are lane C1, all three are the same failure, and all three were decidable at a desk
with a technique the *other lane in the same phase* was using that night.

---

**F1 — "the credits play once, after the Elite Four … Gen 3 has no credits replay. UNREACHABLE by construction" (row L2).**

The credits replay on **every** Elite Four clear. From pokeemerald, in the file lane C1 already had
open:

- `data/maps/EverGrandeCity_HallOfFame/scripts.inc:54` calls `special GameClear` **unconditionally**;
- `src/post_battle_event_funcs.c:13-31` — `GameClear()` checks `FLAG_SYS_GAME_CLEAR` only to decide
  whether to set `gHasHallOfFameRecords`, never to skip anything — and ends at `:84`
  `SetMainCallback2(CB2_DoHallOfFameScreen)`;
- `src/hall_of_fame.c` — the `Task_Hof_*` chain ends at `Task_Hof_HandleExit`, whose last statement
  is `StartCredits()`, which is `:781 SetMainCallback2(CB2_StartCreditsSequence)`. **No gate
  anywhere on that path.**

So L2 is not "unreachable by construction"; it is **budget-gated exactly like L1**, and by the very
same event — one Elite Four rematch delivers the Hall-of-Fame scene (L1) *and then* the credits
(L2), back to back. C1 graded L1 "PROMOTED · UNREACHABLE — the scene itself needs the league beaten
again" and L2 "UNREACHABLE by construction" in the same table; they are one visit.

The sting: C1's cited reason for making the credits INERT is `credits.c:349-357`, where the
fast-forward requires `JOY_HELD(B_BUTTON) **&& gHasHallOfFameRecords**`. And
`gHasHallOfFameRecords` is set TRUE only when `FLAG_SYS_GAME_CLEAR` was **already** set — i.e. only
on a **repeat** clear. If the credits truly played once, the hazard C1 built a whole new context
class around could never fire. The two halves of Entry 0 contradict each other, and the caveat is
the half that is wrong.

*The decision still stands* — `GCTX_INERT` for the credits is right, and arguably more right now.
The row's **reachability grade** and the shipped source comment are what is false. The same wrong
sentence is baked into `source/gamestate.c` (the BPEE `cb2Inert` comment: *"the credits play once
after the Elite Four and Gen 3 has no replay, so no save on this machine can visit them"*).

---

**F2 — "its screen is UNREACHABLE from a solo save … you cannot stand in front of that machine without a link partner" (row E19).**

C1 re-diagnosed E19 as *"a metatile in a cable-club room (`MAP_TRADE_CENTER` / `MAP_RECORD_CORNER` /
the Union-Room side), and those maps are only entered through a live link session."*

I ran lane D1's own scan — the one whose `MB_REGION_MAP` result it reproduces exactly (§0 check 10)
— for `MB_CABLE_BOX_RESULTS_1` (132) and `MB_CABLE_BOX_RESULTS_2` (231):

```
gTileset_PokemonCenter   metatile 226  (= block 738)  -> MB_CABLE_BOX_RESULTS_2
   LAYOUT_POKEMON_CENTER_2F   at (12,4)
```

`LAYOUT_POKEMON_CENTER_2F` is the **shared second floor of every Pokémon Center in Hoenn**. The
monitor tile (12,4) has collision 1; the tile directly below it, **(12,5), has collision 0** —
walkable, elevation 3 — and `MetatileBehavior_IsCableBoxResults2(tile, direction)` is precisely the
"is the player's north tile the monitor" test (`metatile_behavior.c:1366`). Stand at (12,5), face
north, press A. Solo.

It is reached by a **plain staircase warp**. From `data/maps/LavaridgeTown_PokemonCenter_1F/map.json`:

```
{ "x": 1, "y": 6, "dest_map": "MAP_LAVARIDGE_TOWN_POKEMON_CENTER_2F", "dest_warp_id": "0" }
```

`MAP_TRADE_CENTER` and `MAP_UNION_ROOM` are warps *out of* that 2F room — C1 mistook the rooms
beyond the machine for the room containing it.

Two things make this worse than a wrong guess. First, **C1's own Entry-0 reachability table had it
right** before the re-diagnosis overrode it: *"E19 — the record box lives in a Cable Club; the save
stands next to a PC in a Pokémon Center. One flight + one staircase. **PLANNED**."* The
re-diagnosis replaced a correct plan with a wrong exclusion. Second, **lane C2 walked through that
exact building**: Entry 9's P4 excursion crossed `LAVARIDGE_TOWN_POKEMON_CENTER_1F` — *"map (4,5)
PC 1F … crossing the room … the back door"* — and the staircase at (1,6) was in the room it
crossed, on the `emerald-lavaridge` fixture lane D1 also booted. E19 was a stairwell away from two
different boots on the same night.

The *mechanism* verdict (`lockall` + `waitbuttonpress`, so the B3 arm owns it) is correct and I am
not disputing it. The reachability half is false, and it is what downgraded E19 from a visit to a
`VERIFIED-mech`.

---

**F3 — "measured, not assumed … No cheap trigger existed" (row C20, evolution).**

C1's evidence: *"the party is five final-form Lv71-74 mons + a Lv9 Zigzagoon (Linoone is Lv20), the
bag has 7 Rare Candies (→ Lv16) and no evolution stone."* Every one of those statements is true — I
parsed the save and the party matches to the level. But the measurement covered the **party and the
bag** and stopped there. `emerald-fix.sav` also holds **195 Pokémon in its PC boxes**, and C1 was
sitting in the PC UI when it wrote the sentence: the whole E15/E8 arc was a PC session on that save.

Decoding the boxes (species, growth rate from `species_info.h`, level from the `experience_tables.h`
formulas, thresholds from `evolution.h`), **62 box mons evolve by level and 15 of them are inside
the 7 Rare Candies the bag already has**:

```
  1 candy    SANDSHREW  Lv21 -> evolves at Lv22
  1 candy    SEEDOT     Lv13 -> Lv14
  2 candies  ARON       Lv30 -> Lv32     GOLDEEN Lv31 -> Lv33     LAIRON Lv40 -> Lv42
  4 candies  PSYDUCK    Lv29 -> Lv33
  5 candies  MAGNEMITE Lv25->30 · MARILL Lv13->18 · SILCOON Lv5->10 (x2) · VOLTORB Lv25->30
  6 candies  DODUO Lv25->31 · MAGNEMITE Lv24->30
  7 candies  DUSKULL Lv30->37 · SURSKIT Lv15->22
```

**One Rare Candy on a Sandshrew already in the box**, withdrawn from the PC the lane was standing
at, fires `CB2_EvolutionSceneUpdate` — the exact callback C20 needed to move from `PROMOTED-SYM` to
`VERIFIED`. "No cheap trigger existed" is false; the cheapest possible trigger was in the save, and
the lane was one menu hop from it.

---

## 2. Mutation testing — the new suites, broken on purpose

The brief: *for any new suite, mutate the code it covers and confirm the test fails; report the
failure count.* Every mutation was applied to the **shipped** source in an audit worktree at
`2cc34ee`, compiled, run, and reverted. **17 applied, 17 went red, 0 stayed green.**

| # | Mutation | Suite | Failures | First failure text |
|---|---|---|---|---|
| M1 | ANSWER cadence → single edge (undo `2dde03e`) | progseq | **10** | `THE FIX: the game's own yes/no task really pressed YES … which took more than one edge (1)` |
| M2 | DLG A cadence → one pulse (undo `b0ae8d9`) | progseq | **3** | `a dialog that never opens gets FIVE A presses over the budget (got 0)` |
| M3 | ANSWER budget cap removed | progseq | **2** | `after exactly 8 aimed presses (31)` |
| M4 | layout-stability → trust first read (undo `2fa4976`) | excseq | **7** | `the first frame after the warp never plans` |
| M5 | boundary watcher → only while pending (undo `a1cbdca`) | excseq | **486** | `the STEP warp is noticed on the frame it fires` |
| M6 | predicted-map guard removed | excseq | **65** | `a map we did not predict ends the excursion` |
| M7 | arm-don't-plan → plan on the boundary frame (undo `e1c6041`) | excseq | **13** | `…armed, on the map it must be walked on` |
| M8 | `MAPGEOM_FR` origin 32,32 → Emerald's 8,16 | touchgeom | **4** | `FR: every legal cell round-trips through the pixel the GAME draws its cursor at (8*cell + 36) (330 bad)` |
| M9 | `MAPGEOM_FR` bounds → Emerald's 1..28 / 2..16 | touchgeom | **5** | `FR cursor bounds x 0..21 y 0..14` |
| M10 | FR CANCEL cell (21,13) → none | touchgeom | **2** | `FR CANCEL button lives at cell (21,13)` |
| M11 | `navnav_step` → straight-line (kill the ring optimum) | touchgeom | **39** | `type 0: 0 -> 2 costs 1 presses, the RING optimum (got 2)` |
| M12 | `NAVGEOM` type-3 `yStart` 56 → 42 | touchgeom | **7619** | `type 3 row 0 centre y = 56 (got 42)` |
| M13 | `questLog` 0x0203ADFA → …FB | profiles | **2** | `BPRE questLog` / `BPGE questLog` |
| M14 | `sMapCursor` → `sRegionMap`'s value | profiles | **1** | `BPRE rmCurPtr = sMapCursor … FireRed keeps the cursor in its OWN allocation` |
| M15 | `gPokenavResources` 0x0203CF40 → …44 | profiles | **1** | `BPEE pokenavPtr = gPokenavResources` |
| M16 | credits cb2 0x081754DC → …D0 | profiles | **1** | `BPEE cb2Inert[0]` |
| M17 | `CB2_HallOfFame` removed from `cb2FullUi` | profiles | **2** | `EM CB2_HallOfFame 0x08173560 -> GCTX_FULLUI (rows E15 + L1)` |

**M8 is the one that mattered most**: it is the phase-24 O3 bug re-staged — give FireRed Emerald's
numbers and see whether anything notices. 330 of 330 cells fail. The two engines are genuinely
independent tables, not one table with a second name.

The `profiles` mutations (M13–M17) are honest but *weak by shape*: they are literal pins, so they
catch drift, not error — a wrong address entered correctly in both places would pass. That is
tolerable only because the addresses themselves are independently verifiable, which is what §3 did.

---

## 3. New addresses vs. the ROMs and maps actually on this machine

The sym maps in `/private/tmp/pret/` are **md5-identical** to the scratchpad copies the lanes used
(`pokeemerald.sym` 73 204 lines / `39323581…`; `pokefirered_rev1.sym` 50 807 / `8a993325…`), so the
line counts the lanes quote are literally true and we are reading the same file they did.

Cart revisions read from the header: **BPEE r0 · BPRE r1 · BPGE r1 · AXVE r2 · AXPE r2.** The
lanes' choice of `pokefirered_rev1.sym` is correct for the FireRed cart in `roms/`.

**Emerald (25 values, all `pokeemerald.sym`, all exact):** `CB2_HallOfFame` 08173560 ·
`CB2_MailRead` 08121C64 · `CB2_InitMailRead` 081219F0 · `CB2_EvolutionSceneUpdate` 0813E3A4 ·
`CB2_TradeEvolutionSceneUpdate` 0813E3C0 · `CB2_EvolutionSceneLoadGraphics` 0813DD7C · trade twin
0813DF70 · `CB2_ShowContestResults` 080F5C00 · `CB2_HoldContestPainting` 0812FDF8 · `CB2_PlayBlender`
08081898 · `CB2_EndBlenderGame` 08081FC8 · `CB2_PrintErrorMessage` 0800B1A0 · `CB2_LinkError`
0800AF30 · `CB2_Credits` 081754DC · `CB2_StartCreditsSequence` 08175620 · `CB2_ReadHeldMail`
081B4A98 · `CB2_DoHallOfFameScreen` 08173694 · `CB2_DoHallOfFamePC` 08174194 ·
`CB2_StartShowContestResults` 080F5B00 · `CB2_ContestPainting` 0812FDEC · `CB2_QuitContestPainting`
0812FE0C · `CB2_LoadBerryBlender` 0807FAC8 · `CB2_StartBlenderLocal` 080808D4 · `CB2_PokeblockMenu`
0813591C · `CB2_Pokenav` 081C7400 · `gPokenavResources` 0203CF40 (`g 00000004` — a pointer, as D2
says).

**FireRed rev1 (14 values, all exact):** evolution 080CE724 / 080CE740 / 080CE0FC / 080CE2F0 · mail
080BF37C / 080BF124 / 080EC274 · `CB2_InitHofPC` 080F29F0 · `CB2_HofIdle` 080F1E38 ·
`CB2_PrintErrorMessage` 0800AF40 · `CB2_LinkError` 0800ACE8 · `CB2_Credits` 080F3A60 ·
`CB2_RegionMap` 080C08C8 · `sRegionMap` 020399D4 · `sMapCursor` 020399E4 ·
`MetatileBehavior_IsRegionMap` 0805A148.

**The sibling-engine trap was checked and is clean.** The LG values C1 quoted in Entry 0 (credits
080F3A38, evolution 080CE6F8/080CE714, mail 080BF350/080BF0F8, HoF idle 080F1E10) are all correct
for **LeafGreen rev1**, none of them borrowed from FireRed. The one that looks like a copy-paste —
`CB2_PrintErrorMessage` = 0x0800AF40 in *both* FR rev1 and LG rev1 — is a real coincidence,
confirmed in both maps. And C1 correctly refused to ship LG's `cb2Inert`/`cb2FullUi` at all.

One residual, low risk and named by the lane: the phase-25 FR additions to `cb2FullUi` are **rev1
only** (every one of the 14 differs in rev0 — e.g. `CB2_RegionMap` 080C08B4 vs 080C08C8). A rev0
FireRed cart gets no detection on those screens. The read is compare-only, so the failure mode is
"as broken as today", never a mis-key. Worth an explicit line in the row comment, since D1 *did*
carry a rev0 alternate for its own value and C1 did not for its ten.

---

## 4. Evidence-image integrity

90 PNGs in `evidence/impl/` on `main`, 105 on the lane-D branch. **One** duplicate pair in each —
the phase-23 `FR-dlg-P2-…` / `FR-dlg-P4-…` pair (`579bffca…`) that lane C1 hashed, found, and
reported itself in `a388d48`. **Every one of the 27 `*P25*` captures is distinct.** The house's
signature failure did not recur, and it did not recur because a lane went looking for it first.

Opened and checked against their captions:

| Capture | Claim | Verdict |
|---|---|---|
| `FR-P25-D1-townmap-open.png` | name window `INDIGO PLATEAU`; cursor at capture (156,210); SWITCH (612,402) and CANCEL (612,450) land on the drawn buttons | ✅ all four. The two button graphics are exactly where `8*cell+36` predicts. Nothing fitted. |
| `FR-P25-D1-tap-pallet.png` | window `PALLET TOWN`; cursor box at (204,402); **player icon has not moved** | ✅ — the Indigo Plateau icon is still at (155,210) while the cursor box sits on Pallet |
| `EM-P25-D2-pokenav-open.png` | five options `HOENN MAP/CONDITION/MATCH CALL/RIBBONS/SWITCH OFF`; HOENN MAP slid left; description "Check the map of the HOENN region."; rows at capture y 156/216/276/336/396 | ✅ all four, row centres within a pixel |
| `EM-P25-D1-wall-tap-littleroot-no-fly.png` | cursor on Littleroot, window `LITTLEROOT TOWN`, player icon still at Lavaridge, map still open | ✅ |
| `EM-P25-hof-pc-replay-cb2.bottom.png` | the HoF screen with ⓐEXIT, and the incidental "HALL OF FAME data is corrupted" | ✅ exactly, including the incidental C1 chose to name rather than hide |
| `EM-P25-mail-read-cb2.bottom.png` | "BE NICE / TO PLUSLE / ! VOLBEAT / WILL BE / FANTASTIC. From ROMAN" | ✅ word for word |
| `FR-P25-questlog-inert.bottom.png` | grayscale replay, header "Previously on your quest…4" | ✅ |
| `EM-P25-C2-p4-hot-spring-after-extraction.bottom.png` | avatar standing **in** the spring beside two bathers, Pokémon Center to the right | ✅ |

D2's capture-provenance table (naming which two frames predate the pacing fix and why the claim
survives both builds) is the right way to do this and should be copied by other lanes.

---

## 5. The scans and parses that decided F1–F3

Both techniques were already in this phase's own toolkit; neither was applied to lane C1's rows.

- **Metatile scan** — 70 `metatile_attributes.bin` files → block ids → every layout's `map.bin`,
  filtered by secondary tileset. Control: reproduces D1's `MB_REGION_MAP` table exactly.
  Result: `MB_CABLE_BOX_RESULTS_2` at `LAYOUT_POKEMON_CENTER_2F` (12,4), stand-tile (12,5)
  collision 0, entered by an ordinary staircase warp → **F2**.
- **Gen-3 save parse** — newest slot by `saveIndex`, sections 1-4 → SaveBlock1, 5-13 → PC buffer,
  per-mon substructure decrypt (`pv ^ otid`, the 24 orderings), level from EXP via the growth-rate
  formulas. Self-validating: the Emerald party comes back as exactly the six mons C1 described, and
  the FireRed party as exactly the six D1 described. Result: 195 box mons, 15 within the bag's
  candies → **F3**; and no Fly user in FireRed → D1's caveat **confirmed**.

---

## 6. Hygiene, and one scare that turned out to be a clock

The user's own profile is intact and matches lane C's quoted hashes exactly:

```
85dd487ee7681182…  ~/Library/Application Support/Azahar/sdmc/3DGBA/settings.bin   (C1+C2 claimed 85dd487e…)
72c100ad2e385c69…  ~/Library/Application Support/Azahar/sdmc/3DGBA/recent.bin     (C1 claimed 72c100ad…)
```

`sdmc/3DGBA/` on instance a is **ROM-less** — `clean-fixtures` did its job and the phase-24 §3
residual is gone there. Both instances' `qt-config.ini` are 28545 B at their original 8 Aug mtime,
i.e. restored byte-identically. `settings.bin.p24-laneA.bak` now hashes identical to the live file,
so the phase-24 **O7** backup-size mismatch is also resolved.

**The scare, recorded because it cost me an hour and will cost the next auditor one too:** instance
a's `config/` and `log/` show mtime **23:11** and its `settings.bin` **23:16** on 14 Aug — inside
lane D1's session window, which claims *"instance a … never touched"*. That looked like an
isolation breach. It is not. `events.log` timestamps are **UTC** (`…T23:05:01Z`) and the filesystem
is **local (UTC+3)**. Lane C2's second boot is `runs/20260814-201122` = 20:11 UTC = **23:11 local**,
and the `settings.bin` write at 23:16 local is C2's own `traverse 1→2` restore. Instance a's last
run directory is `20260814-201122`; lane D booted only `az-b` (own bundle, own sdmc, own config,
port 24690) in all five of its sessions. **Lane D's isolation claim is true.** Future lanes: quote
one timezone or the other, not both.

---

## 7. Remediation, in priority order

1. **Withdraw the three false caveats and re-grade the rows.** (F1/F2/F3)
   - **E19** → not "UNREACHABLE solo". The machine is on Pokémon Center 2F, `(12,4)`, stand at
     `(12,5)` facing north. It is one staircase from `emerald-lavaridge.sav`, the fixture two lanes
     already booted. This is a **cheap visit**, not a deferral.
   - **L2** → not "unreachable by construction". Re-grade to **budget-gated, jointly with L1** —
     one Elite Four rematch yields the HoF scene and then the credits. **Fix the shipped comment in
     `source/gamestate.c`**, which currently states the false reason in the code itself. Add the
     real finding: the B fast-forward requires `gHasHallOfFameRecords`, which is only TRUE on a
     *repeat* clear, so `GCTX_INERT` is protecting exactly the run C1 thought impossible.
   - **C20** → withdraw "measured … no cheap trigger existed". A box **Sandshrew Lv21** evolves on
     **one** of the bag's 7 Rare Candies. State the real measurement (party + bag + **PC**), or
     take the visit — it is the cheapest remaining row in the phase.
2. **Fix the duplicate `TEST 20`** in `test_profiles.c` (renumber lane D1's FAM-MAP test), and
   correct lane D1's header base commit to `7394be4`. (O4)
3. **Correct the two suite totals** — D1 Entry 1 `950 342` → 952 312, D2 Entry 6 `1 288 241` →
   1 287 081. Better: stop hand-summing. Have the gate script print the total. (O2/O3)
4. **Soften K4's "identical script"** to what the control log actually shows (8 ops vs 4 ops), and
   say plainly that an INERT row's delta is necessarily our own counters plus a game-side
   *classification* read. (O1)
5. **`make cia`** — the `.cia` is 4½ hours and a phase behind the `.elf`. Put it in the close-out
   checklist so this stops recurring. (O5)
6. **Give the ten rev1-only FR `cb2FullUi` additions a rev0 line in the row comment**, matching what
   D1 did with `rmCbAlt`. Compare-only makes it safe, not documented. (§3)
7. **`clean-fixtures` on instance b**, and make the lane close-out template ask for it on whichever
   instance the lane used. (O6)
8. **Merge lane D or say it is staged.** Nothing in LANE-D-FULL.md is on `main`. (O7)
9. Carried from lane C2's own list and still owed: clear `lastW/lastH` in `excseq_boundary` (the
   pinned residue), with a live P4 to re-prove it.

---

## 8. What the lanes did right, in the places I most expected to find a lie

1. **The extraction is real.** Seventeen mutations, seventeen reds. `test_progseq` TEST 4 does not
   check that our code does what our code does — it drives the shipped sequencer at a transcription
   of `Task_HandleYesNoInput` and asserts the *game* got its YES. That is the difference between a
   barrier and a rubber stamp, and audit O2 is properly closed.
2. **A lane hunted the house's own worst failure before the auditor did.** Lane C1 hashed every PNG
   in `evidence/impl/`, found the phase-23 duplicate pair, and reported it in another lane's file
   without renaming or deleting it. Zero new duplicates this phase.
3. **D1's pixel predictions were falsifiable and held.** "SWITCH at (612,402), CANCEL at (612,450),
   neither fitted, both from `CreateMapCursor`" is a claim a screenshot can *refute*, and it did not.
4. **`test_excseq` reported a defect in the code it was written to protect** rather than being
   tuned green. So did lane D2's own live run, which caught its driver's pacing lying and said so.

**The pattern worth keeping.** Phase 24's lesson was *a screenshot cannot prove more than a
screenshot*; the lanes learned it — every image here does exactly the work it can do. Phase 25's
lesson is the next one along: **a caveat is a claim, and it needs evidence like any other.** All
three false findings are sentences beginning "unreachable", written from belief and never re-run —
while, in the same phase and the same nights, lane D1 turned exactly such a sentence ("an
`MB_REGION_MAP` tile was not located") into a visited screen with four minutes of grep. The
technique that would have caught all three was already in the building.

---

## Disposition (main session, 2026-08-15)

Verified independently before accepting. Actions taken:

| # | Verified | Action |
|---|---|---|
| **O7** unmerged | `git rev-list --count main..lane-d1-frmap` = 7, `..lane-d2-ph25` = 18 | **Merged** (`d68c17e`) after dry-running both against main in a throwaway worktree — both clean. The FR region map, wall map, FAM-NAV and Pokéblock case are now actually shipped. |
| **O5** stale `.cia` | `.cia` 14 Aug 23:18 vs `.elf` 15 Aug 03:52 | **Rebuilt.** Second recurrence — see the standing item below. |
| **O4** TEST-number collision | Two `printf("TEST 20:` in `test_profiles.c` — C1's INERT class and D1's FR map | **Renumbered** D1's to TEST 22; zero duplicates remain. |
| **F1/F2/F3** false unreachability | Spot-checked F2 against the map data: the cable-club results machine is on the shared Pokémon Center 2F, and lane C2's own P4 excursion walked through that building | **Accepted — and being acted on**, since all three screens are reachable and were written off. |
| **O1/O2/O3** | Arithmetic and an overstated "identical" | Accepted, cosmetic; the per-suite numbers are honest. |
| **O6** instance b fixtures | Reported | Accepted; fold into the standing hygiene item. |

### The pattern worth more than any single finding

Three of this audit's findings (F1, F2, F3) and one from phase 24 are **the same failure, and it is
not carelessness about evidence — it is the exact opposite of carelessness, applied asymmetrically:**

> **This project rigorously verifies what it CLAIMS, and asserts what it DECLINES.**

A positive claim ("Surf works") is made to earn a live state read, a mutation-tested golden and an
adversarial pass. A negative claim — *unreachable*, *impossible*, *no cheap trigger*, *no ROM on
this machine* — is reasoned out once and then inherited verbatim, sometimes for phases:

- "No Ruby or Sapphire ROM exists on this machine" — both were in `roms/`, and lane B had already
  booted them. Cost: a decisive live proof written off as impossible for a whole phase.
- "Gen 3 has no credits replay" — they replay on **every** league clear (`GameClear` is
  unconditional). The cited INERT rationale needs `gHasHallOfFameRecords`, which is only true on a
  *repeat* clear — i.e. the hazard exists **only** on the run that was called impossible.
- "You cannot stand at the battle-records machine without a link partner" — it is on the shared
  Pokémon Center 2F, one staircase up, and a lane walked through that very building the same night.
- "No cheap evolution trigger existed" — measured the party and the bag, but not the **195 PC-box
  mons** it was standing in front of; a box Sandshrew is one of the bag's own Rare Candies away.

**The rule this earns:** a negative claim is a claim. It gets the same standard as a positive one —
a citation or a measurement, dated, with the thing that was actually checked named. "I could not
reach it in the time I had" is honest and cheap; "it is unreachable" is a finding and must be
earned. When inheriting a caveat from an earlier phase, re-check it or re-date it; never copy it
forward as fact.

### Standing items (recurring, not one-offs)

1. **The `.cia` goes stale after every phase.** It has now been caught twice by audit rather than by
   process. CLAUDE.md rule 1 makes it the real install target, so a stale `.cia` silently blocks
   every hardware test. It belongs in the close-out of any phase that touches `source/`.
2. **Lane branches are not main.** Work that lives only on a lane branch reads as shipped in a
   report and is not. Merge (or state prominently that it is unmerged) at lane close.
3. **Fixtures staged on an instance are user-visible residue.** Clean both instances, not just the
   one that noticed.
