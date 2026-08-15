
## 2026-08-13 — SESSION NOTE for the visit agents (from the orchestrator, mid-run)

The user provided their COMPLETE game library + endgame saves (copied to `roms/` — gitignored,
never commit). `sdmc/dual-gba/gameA.sav` (Emerald) and `gameB.sav` (FireRed) were JUST swapped
to the endgame saves (originals kept as `*.pre-census-backup`). Per the user:
- **Emerald: everything unlocked** — contests, pokeblocks, full pokenav, Battle Frontier are all
  reachable. Do NOT mark them unreachable; if an entry path fails, that is a navigation problem,
  not a save problem.
- FireRed: completed save. Ruby: ~600h completed save (`roms/ruby.sav`; `roms/ruby-alt.sav` is an
  older second save).
- If a boot happened BEFORE this note with the old save, re-stage (`--fresh-sd-fixtures`) and
  re-try previously-locked screens.

NEW FACT for the (future) RS/LG delta pass — NOT this workflow's job: the user's Ruby AND
Sapphire carts are **rev 2** (header byte 0xBC = 2 in both; LeafGreen is rev 1). The AXVE/AXPE
profile rows were sourced from pret symbol maps — before ANY RS boot, re-verify every profile
address against the REV 2 maps specifically (pokeruby/pokesapphire have per-rev symbol files).
A rev-mismatched profile reads garbage; the house rule (never run a guessed address) applies.

## 2026-08-13 — CATALOG.md written (research pass, no emulator)

`docs/phase21-touch-census/CATALOG.md` — 143 rows across 12 sections (A boot … L endgame),
schema: id | screen | games | entry path | pret module + main CB2 | shape | reachability
(judged vs the staged endgame saves). Sources banked:
- pret symbol files fetched fresh to the session scratchpad (pokeemerald.sym 249 CB2s,
  pokefirered(.rev1).sym 182 CB2s, pokeruby.sym 87 CB2s) — rev1 FR / rev2 RS caveats noted
  in the file's method section.
- Module lists: local full pokeemerald clone (gba-toolkit/projects/PokeDNA/daycare map/
  pokeemerald, 315 src modules) + pokefirered src via GitHub API (289 modules) + pokeruby src.
Key findings for the visit pass: FRLG has NO berry tag screen (zero BerryTag symbols in
pokefirered.sym — RSE-only); FRLG help overlay (L/R) changes NO cb2 (IRQ overlay — cb2
detection blind); all PokeNav sub-apps share CB2_Pokenav (needs an internal-state address
capture); TeachyTV main cb2 = TeachyTvMainCallback, FameChecker = MainCB2_FameCheckerMain,
SeaGallop = MainCB2_SeaGallop (non-CB2_ prefixes — don't grep only "CB2_"); berry_crush's
main loop is a static MainCB (berry_crush.c:1033). EM Berry Crush venue unverified (module
compiled in, documented venue is FRLG-only) — flagged, not guessed.

## 2026-08-13 — PAUSED (user is working on the Mac)

The user is actively using the machine: NO Azahar boots and NO screen captures until they say
the machine is free. Catalog phase completed (CATALOG.md + COVERAGE.md banked, cached in the run
journal). The EM visit agent was interrupted early — on resume, re-stage fixtures and restart the
EM visit from its VISITED-emerald.md checkpoint (if any). Resume:
Workflow({scriptPath: ".../phase21-touch-census-wf_c626c1b2-019.js", resumeFromRunId: "wf_c626c1b2-019"})

## 2026-08-13 — EM visit pass, boot #1 (census movie + D4), IN PROGRESS
- Boot recipe that works: `azctl boot --gdb --movie boot_resume.ctm --fresh-sd-fixtures`
  (movie = wait 900f, tap A on the app resume prompt, then a ~6h idle tail so playback
  NEVER runs out — the phase-20 wedge rule; recent.bin already paired gameA+gameB).
  `gdbio resume`, then ALL navigation via D4 seat 1 (Emerald = gameA = screen 0/top).
- **Live identification channel: the gs-logger statics** — `s_lastCb2[2]` @ 0x00530a64,
  `s_lastCtx[2]` @ 0x0052342c, ring `s_gsLog` (156 B entries) — no new addresses, the app's
  own instrumentation read over gdb (census/gsq.py). Every EM resolution landed [exact] on
  pokeemerald.sym = the map is byte-right for the user's cart.
- 30+ screens photographed & cb2-confirmed so far → VISITED-emerald.md + CB2-HARVEST.md
  (banked incrementally). Headline audit finds: the whole Pokedex/PokeNav/summary/
  Frontier-Pass/pokeblock family is one-cb2-per-module with task-driven sub-screens;
  GCTX_BAG drops to residual inside bag sub-menus; the party HM-choose mode is residual
  too; Frontier Pass = free 2px/frame pixel cursor (touch-native candidate #1).
- Fixture-save mutations (deliberate, fixtures only): taught HM02 Fly to Salamence
  (over Steel Wing) to unlock fly-map + town-hopping; fed 1 Indigo pokeblock to Tyranitar
  (H8/H9 evidence); saved once in the Battle Arena lobby.
- D4 operational lessons (in VISITED notes): wall-clock waits race the emu (13–28 fps);
  poll the control log for `script done`; multi-page dialogs (TV news, notebook) keep
  fieldValid TRUE while blocking walks -> walk tokens stall to TIMEOUT (~2-4 min wall
  each) — close dialogs with a/b spam BEFORE any walk token; bedroom PC at (8,1)
  unreachable in this save (decoration blocks (8,2)) -> use any Center PC instead.

## 2026-08-13 — EM visit pass CLOSED (2 boots, ~5h)
Deliverables: VISITED-emerald.md (41 catalog rows photographed + cb2-confirmed, ~60 distinct
captures incl. sub-states), CB2-HARVEST.md (28 live-confirmed EM cb2 fingerprints + 3 FR
incidentals, zero guesses), evidence/emerald/ (143 top-level pngs + aux/ navigation shots),
CAPTURES-emerald.log (raw per-capture cb2/ctx readouts). Azahar stopped cleanly both boots
(config restored; fixture originals re-hashed untouched).
Detection-audit headline (for the master plan): POSITIVE detections today = start menu/
yes-no/multichoice (fmenu), party, bag top-level, battle action/move/other. EVERYTHING else
photographed — the whole Pokedex, PokeNav, summary (incl. HM forget-move), bag sub-menus,
party HM-choose, berry tag, pokeblock case + use/feed, Frontier Pass + trainer card, fly map,
field region map, wall clock, naming keyboard, storage boxes (all modes), slot machine, TV,
title/main-menu family — runs in the GCTX_OVERWORLD residual, i.e. touch is tap=nothing-
useful there today.
Two operational bugs worth phase-22 attention, seen live: (1) battle-start garbles the walk
warp-detector (SaveBlock reads mid-transition produced 0.16->26.28/11.0 "warp-complete"
lines); (2) injected B did not exit CB2_SlotMachine (footer says B QUIT) — investigate before
mapping a touch QUIT chip there. Also: Match Call rings interrupt walking constantly on this
save (80 registered callers) — any touch walking UX must survive surprise field messages.
Save-state note: the EM fixture .sav now carries: Fly on Salamence (over Steel Wing), one
Indigo pokeblock fed, 2 Poochyena KO'd, one in-game save at Route 101 (11,15). Originals in
dual-gba/ untouched (hash-verified by azctl).

## 2026-08-13/14 — FR visit pass (BPRE rev1, seat 2 / bottom screen), boots #1-#2

Same census-boot recipe as EM (movie `boot_resume.ctm`, D4 seat 2 `move_p2.txt`, gsq.py
statics over gdb, resolution on `pokefirered_rev1.sym` — every hit [exact]).

**HEADLINE: the BPRE gamestate profile carries REV0 ROM addresses; the user's cart is REV1
→ every FR task/ROM-pointer detection is silently dead** (battle b.*/party/bag/fmenu/start
menu ALL read `field` residual live; RAM anchors are rev-identical so walking works).
Live-proven drift: BattleMainCB2 0x08011100→0x08011114, Task_HandleChooseMonInput
0x0811FB28→0x0811FBA0, CB2_BagMenuRun 0x08107EE0→0x08107F58, Task_StartMenuHandleInput
0x0806F1F0→0x0806F204, Task_HandleSelectionMenuInput 0x08122C5C→0x08122CD4. Full table +
phase-22 fix list in VISITED-firered.md + CB2-HARVEST.md. This also retroactively explains
why FR battles never showed touch b.act/b.move on hardware.

Boot #1 lesson: blind main-menu scripts entered NEW GAME (Oak speech, no save damage;
A4 captured as a bonus) → recovered by azctl stop/boot (~3 min). Boot #2 = the long pass:
Indigo Plateau save → dex/party/summary/bag/TM-case/berry-pouch/teachy-tv(Pokedude)/
fame-checker/town-map/trainer-card/save-flow → taught HM02 Fly to Zapdos (fixture only,
PSA anim captured; in-game save once at Indigo) → fly map → Viridian (PC family complete:
E11/E12 all sub-states/E10 naming (empty-confirm no-op verified in pret src)/E15 HoF/
E16 Oak rating; mart I1/I2) → Route 22 (wild battle C2/C3/C5 + in-battle bag, Vs Seeker,
Itemfinder, Old-Rod fishing) → Celadon Game Corner (slots I4 — **FR's B→"Quit playing?"
works, unlike the EM stuck-B finding**, coin clerk I6, prize room entered; I8 window
trigger tile not found in 3 tries — shape known, skipped) → Safari Zone leg in progress.

Ops notes worth keeping: pret layout `map.bin` collision decode + BFS = reliable no-vision
navigation (killed the blind-walk timeouts, each of which costs ~4-5 min); dialogs (Oak
rating, itemfinder result) BLOCK walking while open — spam a/b before any walk; trainer
card needs TWO b (back→front→exit); quest-log playback owns the first ~60-90s of every
FR continue (cb2-invisible, runs under CB2_Overworld).

## 2026-08-14 — FR visit pass CLOSED (2 boots, ~5.5h)
Deliverables: VISITED-firered.md (41 catalog rows photographed + cb2-confirmed for BPRE
rev1, ~98 capture pairs incl. sub-states in evidence/firered/), CB2-HARVEST.md (+29 FR
live-confirmed fingerprints, zero guesses — every resolution [exact] on
pokefirered_rev1.sym), CAPTURES-firered.log (raw per-capture readouts). Azahar stopped
cleanly (config restored byte-identically; dual-gba originals re-hashed untouched).
Detection-audit headline: **ZERO positive GCTX detections on FR beyond the overworld home
context — root cause = rev0 ROM anchors in the BPRE profile vs the user's rev1 cart**
(drift table in VISITED/CB2-HARVEST; RAM anchors fine). Phase-22 must re-derive the BPRE
(and BPGE) ROM columns from the rev1 sym files before ANY FR touch mapping can work.
Not-visited rows flagged honestly in VISITED (Sevii trip, player-PC item lists, doubles
target select, mail, diploma, whiteout, chorded boot combos, 2P family).

## 2026-08-15 — phase 26 lane V: HM **DIVE** derived + planned, FLASH settled shut
Deliverable: `SPEC-hm-dive.md` (derivation, the map-transition verdict, the FRLG verdict,
the FLASH finding, the design, the banked live-proof plan) + the pure planner in
`source/fieldtrav.{c,h}` + TEST 20/21 in `test/host/test_fieldtrav.c`.

**The verdict that shaped everything: Dive is neither a tile edge nor a warp — it is a MAP
CONNECTION with an IDENTITY coordinate map.** `TrySetDiveWarp` (pokeemerald
src/field_control_avatar.c:965-983) reads the player's OWN tile via `PlayerGetDestCoords` —
not the tile in front — and `SetDiveWarp` (src/overworld.c:756-782) looks the destination up
in `GetMapConnection(CONNECTION_DIVE|CONNECTION_EMERGE)`, then calls
`SetWarpDestination(grp, num, WARP_ID_NONE, x, y)` with those same coordinates. There is no
warp record and no destination coordinate anywhere: you dive at (x,y), you arrive at (x,y).
Measured against pret's layout data, all seven Emerald pairs are dimension-identical and all
3064 diveable surface tiles have a walkable underwater counterpart (0 exceptions).

**FRLG: no Dive, confirmed four ways** — `ProcessPlayerFieldInput` has no dive/emerge hook;
`TrySetDiveWarp` is `static` with zero call sites; `data/scripts/field_moves.inc:210` says
`@ Unused leftover from R/S` in pret's own words; and FireRed has no underwater map among its
425. `badgeDive` is therefore a NAMED ZERO with its own guard — the tempting wrong value,
0x826, is FRLG's own badge07, i.e. this table's `badgeWaterfall`, so a Soul-Badge save would
have read as dive-eligible. Ruby/Sapphire DO have Dive and get their own row (0x80D, off
pokeruby's own flags.h, never arithmetic from Emerald — the c2a58db rule).

**FLASH: not a traversal gate. Nothing implemented, and the question is closed.**
`SetUpFieldMove_Flash` is a PARTY-MENU handler (fldeff_flash.c:72-91), its entire effect is
`setflashlevel 1` (data/scripts/flash.inc:1-4), and `flashLevel` is consumed only by a
scanline window radius and a battle transition type. `flashLevel`/`FLAG_SYS_USE_FLASH` appear
ZERO times in fieldmap.c / field_player_avatar.c / field_control_avatar.c. Emerald's Registeel
braille door is the one near-miss and is a party-menu puzzle, not a tile edge.

Gate: 18 host suite binaries, 0 failures ("17" was wrong — audit O6a, corrected phase 28 / lane X;
19 from phase 28, which adds `test_progtap`); `test_fieldtrav` 1210 -> 3397 checks (shared with
lane W's Waterfall/Strength blocks; DIVE is TEST 20/21). `make -j8` clean, no new warnings.
`source/fieldpath.{c,h}` byte-identical (frozen, rule 2). Mutation gate: 17 of 19 bite; the
two that do not are named and explained in SPEC-hm-dive §8 rather than papered over.
NO emulator was booted (phase 25 owns both instances) and `make cia` was not run.

**Not shipped, deliberately: the executor + the tier wiring.** `touch.c`/`progseq.*`/`g_prefs`
are untouched by this lane, so nothing can produce an FT_HM_DIVE step yet. A dive INTERACT is
new sequencer behaviour (own tile, no FACE step, **B** to surface, and a MAP CHANGE as the
success gate) and belongs on the excursion leg machine with its own frame-by-frame suite —
not a blind batch written with no way to run it. §6/§7 of the spec are its brief.

**Lane hazard worth recording:** lanes V and W ran in the SAME working tree on the same three
files. Lane W's commit `7e512bf` swept this lane's `fieldtrav.{c,h}` + `test_fieldtrav.c`
changes in with its own. Nothing was lost and the tree is green, but the DIVE implementation
lives in a commit titled for Waterfall/Strength — future archaeology should start from this
entry and SPEC-hm-dive.md, not from the commit subject.

---

## 2026-08-15 — PHASE 28 / lane X (recorded late; the phase had no entry — audit O8)

Recorded here for the archaeology, from `LANE-X-EXECUTE.md` + `PHASE28-AUDIT.md`, because the
build log is the one place the project keeps this and phase 28 skipped it.

**Shipped:** `source/progtap.{c,h}` — the phase-26 tap gate lifted out of `touch.c` line for line
(phase-26 audit O3), with `test/host/test_progtap.c` (1405 checks; the lane's 5 mutations bite
388/19/44/295/2, and the auditor's 12 more all bite except one genuine semantic no-op). The DIVE
tier-order precondition inside `fieldtrav_dive` (`usedK`-equivalent goal-leg lookup → `FT_OUT_TIER0`
for anything the home mode already reaches), with `test_fieldtrav` TEST 24 as the real-cartridge
repro; removing the guard costs 7 checks. Plus the F1/F2/F3/O4/O6/O7/O8 corrections and the two live
mirrors `progLatch` / `progObjSlot,X,Y`.

**The first LIVE EXECUTION in this family, and what it found.** W1 (Waterfall) ran on the user's own
Emerald on emulator instance b. The PLANNER half is proven — a tap on Route 114's fall at (12,12)
from (12,13) retargets to (12,9) and reports `PLANNED`/`FALLS` with the eligibility bit flipping
across the HM teach — and the audit independently re-derived every one of those numbers from the
cartridge bytes, host-side. The EXECUTOR half is live-DISPROVEN: `progAnswers = 0`, `progEnd =
TIMEOUT`, 18 A presses, no prompt (defect **X1**), while one hand-driven `U` + `a` opens the game's
own prompt from the same tile. A second defect was banked: with the HM unusable the tap hands the
retargeted goal to the FROZEN router, which plans a 4-step swim UP the column and reports ARRIVED
(defect **X2**). W2 (Strength) was NOT executed.

**Gate:** 19 host suites, 0 failures. The lane reported `excseq 677`; the measured number at its own
tree is **689** (audit O6/F3 — corrected in LANE-X-EXECUTE.md by phase 29 / lane F).

**Both of the lane's load-bearing NEGATIVES were wrong**, and the audit measured both: Ever Grande's
fly point is chosen by the region-map CELL you tap, not by badge count (`region_map.c:2007`), so the
K=8 site was runnable; and the fixture save's registered item IS the Mach Bike (`registeredItem =
259`), so "Magma Hideout is behind a Mach-Bike wall" named a wall that is not there. Two more
occurrences of the negative-claim failure phase 25 named; phase 27's `2aeb7e5` counts the running
total at **eight**, and its narrower lesson is the one that keeps earning its keep — *a stale
artefact is not evidence of a live process; an mtime says when, never who.*

## 2026-08-15 — PHASE 29 / lane F: the two defects phase 28 found, FIXED

**X1 — FACE turns instead of stepping.** The root cause is one clause, and it is now derived from
the engine rather than from a symptom: `TPH_FACE`'s accept test required `s->frames >= 2` even when
the avatar ALREADY faced the obstacle, so frame 1 always emitted a direction key — and
`CheckMovementInputNotOnBike` (pokeemerald `src/field_player_avatar.c:588-596`) turns in place ONLY
while the held direction differs from the direction being faced; otherwise it MOVES. Against a Cut
tree that frame is a bump; against a waterfall (collision 0 / elevation 1, enterable while surfing)
it is a step, and the phase-28 tap was planned at `progFacing = 2 (UP)` already. `ProgObs` gains
`faceEnterable` (touch.c reads the faced tile's behaviour in TPH_FACE only, through
`fieldtrav_is_waterfall` / `fieldtrav_is_current`, and only while surfing); on an enterable tile FACE
accepts the moment the read says we face the target, so every key it emits is issued under the
engine's own TURN_DIRECTION precondition. An UNREADABLE facing at an enterable tile now ends the
program (`TPE_STALL`) instead of falling back to a blind fixed hold, which is the defect itself.
The impassable path keeps `frames >= 2` verbatim — it is what four HMs were live-proven on.
The second half of X1 needed no new code: `msgbox Text_MonUsedWaterfall, MSGBOX_DEFAULT`
(`field_move_scripts.inc:193`) is advanced by the DONE phase's existing `TP_ADVANCE_EVERY` cadence;
TEST 13 now grades it.

**X2 — the frozen router's answer is SCREENED.** `fieldtrav_path_forced` walks a finished
`FpPlan.path` and returns the index of the first step that enters a waterfall or a current;
`walk_plan` refuses any plan it bites. That is SPEC-hm-waterfall §4.2's instruction applied on the
caller side (the file is still frozen — `git diff source/fieldpath.{c,h}` is empty), at the ONE
choke point every route in the app is planned through, so the SHIPPED DEFAULT is covered and not
just the `gate == 2` fallback. The CURRENTS twin §4.1 named and left is fixed in the same phase:
`fieldtrav_is_current` (0x50-0x53, read in all three engines' own headers) joins the waterfall in
`transition()`. An unreadable behaviour is deliberately NOT a refusal.

**O2** — the cost gate that `touch.c` mirrored moved into `progtap.c` as `progtap_needs_waterfall`
/ `progtap_needs_strength`, and `test_progtap` TEST 7 grades the property that makes a cost gate
correct rather than the fact that two copies agree: *a read the gate says it does not need can never
change the gate's verdict.* Narrowing rule (2) without widening the gate now costs 76 checks.

**Also:** TEST 24's tautology (`planned == 0 || planned > 0`) replaced with a real bookkeeping
invariant, and its (d) case rewritten so the comment and the assertion describe the same thing —
plus the unreachable-goal case it described, graded as a property against `fieldtrav_plan`'s own
tier-0 verdict over the whole 25x25 window (0 disagreements). Citation drift: `:192` → `:193`,
Ruby `:128` → `:126`, `:145` → `:147`, excseq 677 → 689, base commit `ba216e5` → `8897071`.

**Banked, per audit O3:** `tools/emutest/fstate.py` — the g_fieldDbg reader and the scale-mode-aware
tap calculator that phase 28 kept in a session scratchpad, now in the tree with the Aspect-fit
landmine written at the top of the file.

**Gate:** 19 host suites, 0 failures — progseq 902 → **924**, progtap 1405 → **5402**, fieldtrav
3652 → **4199**; every other count unchanged. Mutation numbers in LANE-F-EXECUTOR.md §1/§2.
`make -j8` clean. `source/fieldpath.{c,h}` byte-identical (frozen, rule 2).

**NOT done: the live re-runs (W1 at K=8, W2).** Not "cannot" — measured: with a gdb client attached
and resumed, the app freezes ~20 s into a GAME session on this machine, and the same freeze
reproduces byte-for-byte on **phase 28's own binary**, so it is not this lane's change. Details,
reproduction and the next session's shortest path in LANE-F-EXECUTOR.md §3.

**Standing item, third recurrence:** instance a still carries `traverse = 1` in the user's own
`settings.bin` (offset 100). This lane confirmed by the LOCK FILE and `pgrep`, not by an mtime, that
nothing owns that instance — and then could not write there (permission). It needs a session that
can. Its staged `gameA/gameB` fixtures are already gone.
