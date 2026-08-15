# PHASE 27 — ADVERSARIAL AUDIT

_Auditor pass, 2026-08-15. Lane **R** audited at its close-out commit **`1c797ac`** (on `main`),
checked out into a detached worktree so nothing below was measured against a dirty tree — which
mattered: a **concurrent session was editing `source/` live** during this audit (`touch.c`,
`fieldtrav.c`, `progseq.c`, `progtap.c` all picked up uncommitted modifications between 06:33 and
06:36, plus a new untracked `tools/emutest/censusguard.py`). Every suite number below is from
`1c797ac`, not from the shared working tree._

This phase exists because the phase-25 audit caught four false *"unreachable"* claims, all of them
the same failure: **a negative claim inherited and never re-run.** So the first duty here was to
check that the cure did not reproduce the disease — every limiting statement in the lane report got
the same treatment as a positive one.

**It reproduced once, and squarely.** The single FALSE finding below is a caveat the lane copied
forward from the phase-25 audit's O6 and restated as present-tense fact, refutable by one `cat` of
a file the lane had already `stat`'d — and by a document committed *one commit earlier in the same
repo*.

---

## 0. What I actually ran (nothing here is quoted from the lane)

| # | Check | Result |
|---|---|---|
| 1 | All **19** host suites rebuilt and re-run at `1c797ac`, CWD = tree root, with `data/` **and** `roms/` present | 0 failures; **1 290 973 checks** — §6 |
| 2 | **M18a / M18b / M18c** applied to the shipped `source/excseq.c`, built, run, reverted — at the tip *and* at `d1976bf` | all red at both; the lane's 3/4/6 reproduce **exactly** at `d1976bf` — §2 |
| 3 | Independent pret **metatile scan** of `LAYOUT_POKEMON_CENTER_2F` + `LavaridgeTown_PokemonCenter_1F` from `master`, plus the warp JSON and the behaviour enum | reproduces the lane's numbers to the digit — §3 |
| 4 | Independent **Gen-3 save parse** of `roms/emerald-fix.sav` (party moves, all 14 PC boxes, flag array) | 195 box mons, Sandshrew Lv21, Goldeen Lv31, 8 badges, `FLAG_SYS_GAME_CLEAR` 1, Lugia knows FLY — §4 |
| 5 | Every cited pret line fetched fresh from `pret/pokeemerald` master and read | `scripts.inc:54/:61`, `post_battle_event_funcs.c`, `hall_of_fame.c`, `credits.c:349-351`, `evolution_scene.c:638-641`, `evolution.h:19` — all as quoted — §5 |
| 6 | Every new address resolved in `pokeemerald.sym` / `pokefirered_rev1.sym`; cart headers re-read | `BPEE r0`, `BPRE r1`; all `[exact]`; **no address was added to `source/` at all** — §5 |
| 7 | SHA-256 of **all 683 PNGs** under `evidence/`, and separately within `evidence/impl/` | 13 `EM-P27-R-*`, all distinct; only pre-existing `impl/` dup is the phase-23 pair — §7 |
| 8 | Opened **all 9** load-bearing P27 captures and checked the described detail against the pixels | all 9 show what they claim — §7 |
| 9 | The app's **own control logs** from all three runs (`runs/20260815-{012859,014027,015140}`) | the E19 route, both TIMEOUTs, the single tap, the wireless-box block, the L1 trainer-block and the 60-token battle batches are all in the game-produced log — §3, §5 |
| 10 | `git diff 010a138 HEAD -- source/fieldpath.{c,h}`; `git log --all --diff-filter=A -- '*.gba' '*.sav'`; `git ls-files` | empty / nothing / nothing |
| 11 | Every local branch vs `main`; `git worktree list` | all 9 branches **0 ahead**; all lane work is on `main` |
| 12 | `find tools/emutest/az tools/emutest/az-b -name '*.gba' -o -name '*.sav'`; both `azctl.lock` files read verbatim | **BOTH instances clean** — and that refutes the report — §8 |
| 13 | Re-hashed the user's own `~/Library/Application Support/Azahar/sdmc/3DGBA/*` and `config/qt-config.ini` | byte-identical to the pre-lane values — §8 |
| 14 | `arm-none-eabi-objdump -d 3DGBA.elf` on `excseq_layout_kill`; `nm` for `progtap_gate` | the shipped binary really carries the phase-27 fix and phase-28's gate — §9 |

Suite counts at `1c797ac`, all 0 failures: celiolink 1259 · control 6940 · diag 376 · **excseq 689** ·
**fieldpath 1808 (UNMODIFIED)** · fieldtrav 3652 · netlink 66 · peersprite 62078 · presence 61376 ·
profiles 2746 · progseq 902 · **progtap 1405** · theme 83444 · tilt 1756 · touchgeom 1040833 ·
trace_replay 58 (+4 loud SKIPs) · typography 1419 · uigeom 18332 · uihit 1834 = **1 290 973**,
**19 suites**.

> **Methodology note for the next auditor, on top of the two already on file** (`test_fieldtrav`
> needs the CWD; `test_typography` needs `data/`): **`test_fieldtrav` silently loses 327 checks
> without `roms/`.** From a bare worktree it reports **3325**, not 3652, and prints **no SKIP** —
> TEST 24 (the real-ROM DIVE case) just does not run. That briefly looked like the lane inflating
> its number against an uncommitted tree; it was my worktree missing a gitignored directory. A
> green `fieldtrav` number is only reproducible from a tree that has `data/` **and** `roms/`.

---

## 1. Verdicts

### CONFIRMED

| Claim | What decided it |
|---|---|
| **E19 = VISITED SOLO, and C1's "you cannot stand in front of that machine without a link partner" is FALSE** | Reproduced from pret `master` with my own scanner: `LAYOUT_POKEMON_CENTER_2F` is 14×10, **(12,4) = block 738 = secondary index 226, behaviour 0xE7, collision 1, elev 0**; stand tile **(12,5) = block 615, collision 0, elev 3**; the sibling **(8,4) = block 739 / idx 227 / 0xE8** one tile family over. The enum self-checks (`MB_UNUSED_81` = 0x81, `MB_UP_ESCALATOR` = 0x6A) pin `MB_CABLE_BOX_RESULTS_2` = **231 = 0xE7**. `LavaridgeTown_PokemonCenter_1F/map.json` carries `{x:1, y:6, elevation:4, dest MAP_LAVARIDGE_TOWN_POKEMON_CENTER_2F}`. **And the route story is in the game's own log**, not just the report: `runs/20260815-012859/netlogs/…020058.txt` shows `U2 start (1,8)->(1,6)` → `TIMEOUT tok 1 U2 at (1,7)`, a second `U1` → `TIMEOUT`, then `R1`→(2,7), `U1`→(2,6), `L1`→(1,6), then `R10`→(12,6), `U1`→**(12,5)**. Which is exactly what the elevations predict: (1,7) elev 3 → (1,6) elev 4 is forbidden; (2,6) is elev **0**, the any-level tile. |
| **The E19 tap is backed by a delta the GAME produced** | `fieldLock` 1→0 is `releaseall` running after `waitbuttonpress` consumed the A — not our counter. The control log shows exactly `[ctl touch] picked up 1 touch ops` at that point. Both captures corroborate (window open → overworld at the machine). This is the house's own definition of VERIFIED, met. |
| **The wireless boundary is real and measured, not assumed** | `U1 start (8,5)->(8,4)` → `TIMEOUT` (collision 1), then `a` → *"The Wireless Adapter is not connected properly."* in `EM-P27-R-e19-wireless-adapter-gated.bottom.png`. One tile family apart from a machine that opens. |
| **The excursion leg-boundary fix is real and covered** | §2. All three mutations red. **M18a 3 · M18b 4 · M18c 6 reproduce EXACTLY at `d1976bf`**, the commit that states them. At the shipped tip they are **5 / 4 / 8** because TEST 7b adds two more assertions on the same invariant. |
| **The previously-pinned anti-test was RE-POINTED, not deleted** | `test_excseq` TEST 6's two residue assertions (`first == 0` "the stale dims history makes frame 0 look settled" / `wastedOnOldGrid == 1`) are replaced in place by `s.lastW == -1 && s.lastH == -1` ("THE FIX: arming a leg forgets the previous leg's dims"), `wastedOnOldGrid == 0` and `first == EXC_SETTLE_EVERY`. Same fixture, same trace, moved expectation. Nothing was dropped. |
| **TEST 7b pins the remaining defect deliberately, and it is a real property of the rule** | The suite now asserts, in code, that with the departing map's dims persisting 30 frames the plan still lands on the **14×9** grid at the first cadence frame and has to be killed. `EXC_SETTLE_EVERY` is 8 frames; a Gen-3 warp fade is longer; "stable" is satisfied by a map that has not started moving. The fix removes the frame-0-fossil case and cannot see further. Stated as a measurement that **disproved the lane's own expectation**. |
| **Every address is exact for the carts actually in `roms/`** | Headers re-read: **BPEE r0**, **BPRE r1**. `pokeemerald.sym`: `CB2_EvolutionSceneUpdate` **0813e3a4** `l 0000001a`; `CB2_UpdatePartyMenu` **081b01b0** `l 0000001a`. `pokefirered_rev1.sym`: `CB2_EvolutionSceneUpdate` **080ce724**. All `[exact]`. **No sibling-engine arithmetic is possible here, because no address was added at all** — the `source/gamestate.c` diff is comment-only; the value arrays are untouched context lines. |
| **The L1/L2 chain is ungated end to end — the re-grade is right** | Fetched from `pret/pokeemerald` master and read: `EverGrandeCity_HallOfFame/scripts.inc` **:54** (male) and **:61** (female) both `special GameClear` with **no guard**; `post_battle_event_funcs.c` tests `FLAG_SYS_GAME_CLEAR` **only** to pick `gHasHallOfFameRecords` (`else` branch sets it FALSE *and* sets the flag), and ends `SetMainCallback2(CB2_DoHallOfFameScreen)`; `hall_of_fame.c` `Task_Hof_HandleExit` → `StartCredits()` → `SetMainCallback2(CB2_StartCreditsSequence)`. **"UNREACHABLE by construction" is correctly withdrawn.** |
| **The sting is real and live on this fixture** | `credits.c:349-351` is `JOY_HELD(B_BUTTON) && gHasHallOfFameRecords && gTasks[sSavedTaskId].func == Task_CreditsMain`, and `gHasHallOfFameRecords` is TRUE only on a **repeat** clear. My own parse of `roms/emerald-fix.sav`: `FLAG_SYS_GAME_CLEAR` (SYSTEM_FLAGS 0x860 + 4) = **1**. So `GCTX_INERT` is protecting exactly the run that was called impossible. This is now the shipped comment. |
| **All three L1 preconditions re-measured independently** | Badges `[1,1,1,1,1,1,1,1]`. `FLAG_SYS_GAME_CLEAR` 1. Party: species 248 Lv73 · 397 Lv72 · 149 Lv74 · 329 Lv73 · **249 (Lugia) Lv71 with move 19 = FLY** · 288 Lv9. The party capture `EM-P27-R-c20-party-before-candy` shows those same five levels with SANDSHREW Lv21 swapped into slot 6 — the driver and the save agreeing. |
| **C20's desk parse reproduces to the mon, including the alignment trap** | `PokemonStorage.boxes` at PC-buffer offset **4** → **195** valid box mons (checksum == stored, species in range); at offset **1** → **0** valid, i.e. the trap is real. `currentBox` = **9**. Box 0 slot 14 = **species 27 (Sandshrew), exp 9261 = 21³ → Lv21**, and `evolution.h:19` `[SPECIES_SANDSHREW] = {{EVO_LEVEL, 22, SPECIES_SANDSLASH}}`. The *unused* recipe checks out too: box 2 slot 7 = **species 118 (Goldeen), exp 29791 = 31³ → Lv31**. |
| **The C20 evolution really happened, on the screen, with the fingerprint claimed** | `EM-P27-R-c20-box-sandshrew-lv21` (panel reads `SANDSHREW /SANDSHREW ♀ Lv21`, "SANDSHREW is selected."), `EM-P27-R-c20-bag-rare-candy` (`RARE CANDY ×7`, cursor on it, "Raises the level of a POKéMON by one."), `EM-P27-R-c20-evolution-scene-cb2` ("What? SANDSHREW is evolving!"), `EM-P27-R-c20-evolved-sandslash` ("Congratulations! Your SANDSHREW evolved into SANDSLASH!"). |
| **The C20 cancel gap is diagnosed correctly** | `evolution_scene.c:637-641` is `gMain.heldKeys == B_BUTTON && tState == EVOSTATE_WAIT_CYCLE_MON_SPRITE && gTasks[sEvoGraphicsTaskId].isActive && tBits & TASK_BIT_CAN_STOP` — equality, not a mask, and one mid-animation state out of eighteen. The lane's account of why its hold missed is exactly right, and it reported it against itself. |
| **The L1 distance covered is real, and stated as a distance** | Game-produced: `EM-P27-R-l1-flew-to-pokemon-league` (the player on the league steps), `EM-P27-R-l1-sidney-challenge` ("Welcome, challenger! I'm SIDNEY of the ELITE FOUR."), `EM-P27-R-l1-sidney-defeated` ("You've got what it takes to go far. Now, go on to the ne…"). The control log corroborates failure mode 3 verbatim: `U1 start (6,7)->(6,6)` done, then `U1 start (6,6)->(6,5)` → **`TIMEOUT tok 0 U1 at (6,6)`** — the trainer blocking the tile. And the `60 tokens` batches are there, nine of them. **"I did not reach the Hall of Fame or the credits" is stated plainly and is true.** |
| **User data untouched; instance a is the user's real profile and is clean** | `instance.py` is explicit that instance **a** *is* `~/Library/Application Support/Azahar` — so this is the user's own profile, not a sandbox. `settings.bin` **`85dd487ee7681182…`**, `recent.bin` **`72c100ad2e385c69…`**, `config/qt-config.ini` **`dd20792e32878733…`** 28545 B still at its **8 Aug 19:53** mtime. `sdmc/3DGBA/` is **ROM-less**. All three runs' `events.log` end with `fixtures: originals re-hashed, all untouched` + `profile: restored qt-config.ini byte-identically, backup deleted (CLEAN)`. `roms/` mtimes are all 13–14 Aug, i.e. **before** the lane's first boot; `emerald-fix.sav` hashes `d222c5cd…`, the value the lane quotes. **The user's real save was never written to.** |
| **`fieldpath.{c,h}` frozen; nothing ROM-shaped in history; nothing stranded on a branch** | `git diff 010a138 HEAD -- source/fieldpath.{c,h}` = **0 lines**, `fieldpath 1808`. `git log --all --diff-filter=A -- '*.gba' '*.sav'` = empty, `git ls-files` = clean. All nine local branches are **0 ahead of `main`**; the lane's five commits are on `main`, interleaved with phase 28's and rebased rather than overwritten, as claimed. |
| **The `.cia` is current and genuinely carries this lane's fix** | `.cia` 06:21 > `.elf` 06:20 > every source file at close-out; `build/excseq.o` 04:26 post-dates `source/excseq.c` 04:21. And the binary itself proves it: `arm-none-eabi-objdump -d 3DGBA.elf` on `excseq_layout_kill` shows `mvn r3, #0` with `strd r2, [r0, #52]` / `str r3, [r0, #60]` — **`lastW`/`lastH` written to −1**, which is the phase-27 fix. `nm` also finds `progtap_gate`, so phase 28 is in there too. The audit's standing item 1 is closed for this phase. |
| **Evidence integrity** | 683 PNGs hashed. **13 `EM-P27-R-*`, all distinct**, none colliding with anything else in the tree. Inside `evidence/impl/` the only duplicate pair is still the phase-23 `579bffca…` one lane C1 found and reported itself. (The 19 other duplicate hashes live in `evidence/emerald/` + `evidence/firered/` and long predate this phase; `tools/closeout.sh` now *warns* about them, correctly, as a pre-existing condition.) |

### OVERSTATED

**O1 — "18 suites, 0 failures, 1 289 568 checks" is a gate over 18 of the tree's 19 suites.**
The number is *exact* — I reproduce **1 289 568** by summing the 18 suites the lane names — and every
one of them is green. But `test_progtap` (**1405 checks**) is not among them, and it has been on
`main` since **`6ee8e93`**, which `git merge-base --is-ancestor` confirms is an ancestor of the
lane's own tip. `1 289 568 + 1405 = 1 290 973`, which is what I measure. The aggravating detail:
`docs/phase21-touch-census/PHASE28-AUDIT.md` — committed at **`2d226de`, 06:20, three minutes and
one commit before the lane's close-out** — opens with *"All **19** host suites"* and prints
`progtap 1405` in its own count. A whole suite was missing from the gate while the correct number
sat in the same directory. (No defect is hidden: progtap is green at `1c797ac`.)

**O2 — "the total is printed by the runner now, not hand-summed" (commit `8897071`) — there is no such runner.**
Nothing in `tools/` runs the suites and prints a total. The only close-out tooling in the tree is
`tools/closeout.sh` (`1f489bb`, a sibling lane's), which checks the `.cia`, branches, frozen files,
ROMs, staged fixtures and duplicate captures — and never touches the suites. The lane report's own
Entry 7 is honest about this (*"a sibling lane has since started `tools/closeout.sh`, so that is
where the check belongs"*); the commit message asserts a capability that does not exist. And O1 is
the direct consequence: **machine-summing a hand-picked list is still a hand-picked list.**

**O3 — C20's `VERIFIED (EM)` grade rests on verbs that no game-side delta backs, and one of them never can.**
The lane names half of this — the hold's CANCEL is honestly reported UNPROVEN, with a citation and a
recipe. It does not name the other half. `dlgTaps` 0→1 and `dlgHolds` 0→1 are **our own counters**;
the report's "the harvest" table lists them beside the `[exact]` cb2 and the `ctx 10` read, which are
game-side, and the TOUCH-PLAN cell cites them as part of what makes the row VERIFIED. But in
`Task_EvolutionScene`, **`A_BUTTON` is read at exactly one place — line 928, inside
`EVOSTATE_REPLACE_MOVE`'s yes/no.** The animation and every message ignore A. So on the screen the
lane visited, a tap **cannot** produce a game-side delta, ever. By this project's own definition of
VERIFIED (*"the verb was proven by a state delta the game produced"*, TOUCH-PLAN §1), C20 is
VERIFIED for **reach + `[exact]` fingerprint + classification** and **UNPROVEN for both verbs**.
This is phase-25's **O1** recurring: an own-counter delta borrowing the strictness of rows that have
real ones.

**O4 — "all nine `EM-P27-R-*` PNGs are distinct" (Entry 5) undercounts its own evidence.**
Ten existed when Entry 5 was written (four E19 + five C20 + one P4); thirteen at close, which the
close-out says correctly. Distinctness itself is true at every count — I hashed all thirteen. A
third miscounted count in three phases, on the smallest possible thing.

**O5 — "it was byte-identical … *because* the run never saved in-game" is a non-sequitur about a deleted artefact.**
`roms/emerald-p27.sav` was staged *into* the emulator's sdmc; the `roms/` original is untouched
whether or not the game wrote a save, so its hash proves nothing about saving. And the file was
deleted at close, so the claim can now be neither confirmed nor refuted. The claim it was offered in
support of — that the user's fixtures are untouched — is independently **CONFIRMED** by mtime, hash
and all three `events.log` lines, so nothing rests on it. Recorded because deleting the artefact
that would settle a claim is the wrong reflex in this house.

**O6 — the live P4 re-proof is single-sourced.**
`runs/20260815-014027` harvested only the control log (which does show exactly the one touch op
claimed) and `azshots` is empty; the `planSeq 4 → routeEnd 6 → planSeq 5`, `endSeq 3`, `beh 12` vs
`beh 40` readings exist only in the report. The *mechanism* is independently reproduced on the host
(TEST 7b, which I ran and mutated), and the claim runs directly against the lane's own interest —
it is a self-disproof, the least likely kind of thing to fake. But "lane C2's pre-fix numbers, line
for line" is a strong specific claim with no archived transcript behind it. Dump the gdb reads into
the run folder next time; it costs nothing.

### FALSE

---

**F1 — "Instance b was NOT cleaned … the O6 residual is still there … a concurrent session owns it."**

Instance b was cleaned **54 minutes before the close-out was written**, and the lane had already
looked at the file that says so.

1. **`tools/emutest/state-b/azctl.lock`, verbatim:**
   ```
   pid=56654 cmd=clean-fixtures at=2026-08-15T02:28:59Z
   ```
   `02:28:59Z` = **05:28:59 local** (UTC+3) — the exact mtime the lane quotes as evidence that
   *"a concurrent session owns it"*. The lane read the file's **timestamp** and not its **one line
   of contents**, which names the command. The last thing anyone did to instance b was clean it.

2. **The fixtures are gone.** `find tools/emutest/az tools/emutest/az-b -name '*.gba' -o -name '*.sav'`
   returns **nothing**. `az-b/user/sdmc/3DGBA/` holds `settings.bin` and one `.bak` — the 16 MB
   `gameA.gba`/`gameB.gba` and the `recent.bin` the report lists as *"still there"* are not there,
   and the directory's own mtime is **05:28:59**, i.e. the moment of the clean.

3. **It was already written down, in this repo, one commit earlier.**
   `docs/phase21-touch-census/PHASE28-AUDIT.md` (`2d226de`, 06:20) states:
   *"**Instance b is clean** — No staged `gameA/gameB` files, empty control dir…"* and
   *"Instance cleanliness — b yes, a no."* `2d226de` is an ancestor of `1c797ac`.

So the residual the report *"leaves open on purpose, named, with the owner and the fix"* did not
exist; the concurrency it declined to disturb had ended; and the command it hands to the next lane
(`tools/emutest/run --instance b azctl clean-fixtures`) is a no-op. Meanwhile the half that **was**
open — instance a, which phase 28 had just flagged — the lane genuinely closed:
`state/azctl.lock` reads `cmd=clean-fixtures at=2026-08-15T03:21:04Z` (06:21 local) and
`az`/`sdmc/3DGBA` is ROM-less.

**Both instances are clean. The report says one of them is dirty.**

This is the phase's own disease, reproduced inside the lane that was created to cure it: a limiting
claim inherited verbatim from the phase-25 audit's **O6**, restated in the present tense, refutable
by one `cat`, one `find`, or by reading the file committed three minutes earlier. The rule the
disposition wrote — *"When inheriting a caveat from an earlier phase, re-check it or re-date it;
never copy it forward as fact"* — was applied rigorously to E19, C20, L1 and L2, and not applied to
the one caveat that was about the lane's own housekeeping.

*What is right about it:* the **decision** not to run `clean-fixtures` blind against an instance
another session might be mid-arc on is correct, and the reasoning is the kind this house wants. It
is the factual claim wrapped around the decision that is false.

---

## 2. Mutation testing — the excursion fix, broken on purpose

Applied to the shipped `source/excseq.c`, compiled, run, reverted. `exc_forget_dims()` is called
from both re-arm sites; each mutation deletes one call.

| # | Mutation | at `d1976bf` (682 checks) | at the tip `1c797ac` (689 checks) | First failure |
|---|---|---|---|---|
| **M18a** | undo the `excseq_boundary` clear | **3** (lane: 3 ✓) | **5** | `THE FIX: arming a leg forgets the previous leg's dims` |
| **M18b** | undo the `excseq_layout_kill` clear | **4** (lane: 4 ✓) | **4** | `THE FIX, other site: a layout kill forgets the dims too` |
| **M18c** | undo both (= the pre-phase-27 shipped file) | **6** (lane: 6 ✓) | **8** | both of the above |

The lane's numbers are the `d1976bf` numbers, which is where its commit message states them and
where they are exactly right. The report (Entry 2) restates them in a document whose gate is the
689-check tip, without re-dating; at the tip M18a and M18c each pick up TEST 7b's two extra
assertions on the same invariant. Not a defect — worth one clause.

**Both sites are genuinely load-bearing**, which is the part that mattered: M18b is a separate red
from M18a, so the second call site (the same-size-swap path, where only `gBackupMapLayout`'s
*pointer* changes and the stale dims match the new map exactly) is not decoration.

---

## 3. The E19 scan, re-derived

My scanner (behaviour = attribute byte, block = word & 0x3FF, collision = >>10 & 3, elevation =
>>12 & 0xF, secondary index = block − 512), run against files fetched fresh from `pret/pokeemerald`
master:

```
LAYOUT_POKEMON_CENTER_2F  14x10   prim gTileset_Building  sec gTileset_PokemonCenter
  ( 8,4) block 739  sec 227  beh 0xE8  coll 1  elev 0     <- MB_WIRELESS_BOX_RESULTS
  (12,4) block 738  sec 226  beh 0xE7  coll 1  elev 0     <- MB_CABLE_BOX_RESULTS_2
  STAND (12,5)      block 615          coll 0  elev 3     <- WALKABLE
  ( 5,1) / ( 9,1)   block 612          coll 1  elev 3     <- the rooms BEYOND the machine

LAVARIDGE_TOWN_POKEMON_CENTER_1F  14x9
  (1,6) block 649  beh 0x6A MB_UP_ESCALATOR  coll 0  elev 4
  (2,6) block 514  beh 0x00                  coll 0  elev 0
  (1,7) block 657  beh 0x00                  coll 0  elev 3
```

`metatile_behaviors.h` is an **enum**, and the indices self-check: `MB_UNUSED_81`/`MB_UNUSED_82` land
on 0x81/0x82, `MB_UP_ESCALATOR` on 0x6A (the value actually found at Lavaridge (1,6)),
`MB_CABLE_BOX_RESULTS_1` on 0x84 and **`MB_CABLE_BOX_RESULTS_2` on 0xE7**. The lane's "one honest
wobble" (an off-by-one first parse) resolves the same way mine does.

`LavaridgeTown_PokemonCenter_1F/map.json` warp 2: `{"x":1,"y":6,"elevation":4,"dest_map":"MAP_LAVARIDGE_TOWN_POKEMON_CENTER_2F","dest_warp_id":"0"}`.

**And the elevation story is not theory** — the app's own log has the two timeouts and the
right-up-left detour, in order, with coordinates.

---

## 4. The save parse, re-derived

Newest slot by footer `saveIndex` (1963), sections 1-4 → SaveBlock1 (15752 B), 5-13 → PC buffer
(33744 B), per-mon `pv ^ otid` decrypt over the 24 substructure orderings, checksum-validated.

```
FLAG_SYS_GAME_CLEAR (SYSTEM_FLAGS 0x860 + 4)   1
badges                                          [1,1,1,1,1,1,1,1]
party  248 Lv73 · 397 Lv72 · 149 Lv74 · 329 Lv73 · 249 Lv71 (move 19 = FLY) · 288 Lv9
currentBox                                      9
valid box mons  @offset 4 -> 195   |   @offset 1 -> 0
box 0 slot 14   species  27  exp  9261 = 21^3  -> Lv21   (EVO_LEVEL 22, evolution.h:19)
box 2 slot 7    species 118  exp 29791 = 31^3  -> Lv31   (the unused GOLDEEN recipe)
```

Every number the lane published from this save is right, including the alignment trap (at offset 1
my parser finds **zero** valid mons, so the trap is real and the lane's account of it is the
generous version).

---

## 5. Citations and addresses

Every pret line was fetched fresh and read, not inherited:

| Cited | Verdict |
|---|---|
| `EverGrandeCity_HallOfFame/scripts.inc:54` / `:61` — `special GameClear`, unconditional both branches | ✅ verbatim |
| `post_battle_event_funcs.c` — `FLAG_SYS_GAME_CLEAR` only picks `gHasHallOfFameRecords`; `SetMainCallback2(CB2_DoHallOfFameScreen)` | ✅ |
| `hall_of_fame.c` — `Task_Hof_HandleExit` → `StartCredits()` → `SetMainCallback2(CB2_StartCreditsSequence)` | ✅ |
| `credits.c:349-351` — `JOY_HELD(B_BUTTON) && gHasHallOfFameRecords && gTasks[sSavedTaskId].func == Task_CreditsMain` | ✅ |
| `evolution_scene.c:638-641` — `heldKeys == B_BUTTON && tState == EVOSTATE_WAIT_CYCLE_MON_SPRITE && …isActive && tBits & TASK_BIT_CAN_STOP` | ✅ (and equality, not a mask, as stated) |
| `evolution.h:19` — `[SPECIES_SANDSHREW] = {{EVO_LEVEL, 22, SPECIES_SANDSLASH}}` | ✅ |

Addresses, against the carts on this machine (**BPEE r0**, **BPRE r1**):

| Symbol | Claimed | `.sym` | |
|---|---|---|---|
| `CB2_EvolutionSceneUpdate` (EM) | 0x0813E3A4 | `0813e3a4 l 0000001a` | ✅ exact |
| `CB2_UpdatePartyMenu` (EM) | 0x081B01B0 | `081b01b0 l 0000001a` | ✅ exact |
| `CB2_EvolutionSceneUpdate` (FR rev1) | 0x080CE724 | `080ce724 l 0000001a` | ✅ exact |

**No address was added to `source/` by this lane** — the `gamestate.c` diff is comment-only, so
"sibling-engine arithmetic" has no surface here. The FR rev0 caveat the phase-25 audit asked for
(item 6) is now written into the BPRE `cb2Inert` comment.

---

## 6. Suites

19 suites, 0 failures, **1 290 973** checks at `1c797ac`, run by me from a detached worktree with
`data/` and `roms/` linked in. `excseq` **689** (the lane's 677 → 689 is right), `fieldpath`
**1808** unmodified, `progtap` **1405** — the one the gate did not count.

---

## 7. Evidence integrity

683 PNGs hashed across `evidence/`. The 13 `EM-P27-R-*` captures are mutually distinct and distinct
from everything else in the tree; the only duplicate pair inside `evidence/impl/` remains the
phase-23 `579bffca…` one lane C1 reported itself. I opened all nine load-bearing P27 captures:

| Capture | Claim | Verdict |
|---|---|---|
| `e19-battle-records-open` | "GUYA's BATTLE RESULTS / TOTAL RECORD W:0 L:0 D:0", WIN/LOSE/DRAW columns, five empty rows | ✅ all of it |
| `e19-tap-closed-it` | player back in the overworld at the machine | ✅ |
| `e19-wireless-adapter-gated` | "The Wireless Adapter is not connected properly." | ✅ word for word |
| `e19-2f-reached-solo` | on 2F beside the stairwell | ✅ |
| `c20-box-sandshrew-lv21` | panel `SANDSHREW /SANDSHREW ♀ Lv21`, "SANDSHREW is selected." | ✅ |
| `c20-bag-rare-candy` | RARE CANDY **×7**, cursor on the row, "Raises the level of a POKéMON by one." | ✅ (and ×7 matches the save) |
| `c20-party-before-candy` | "Use on which POKéMON?" with Tyranitar 73 / Salamence 72 / Dragonite 74 / Milotic 73 / Lv71 / **SANDSHREW Lv21** | ✅ — and the five levels match my save parse exactly, with the Lv9 Zigzagoon correctly absent |
| `c20-evolution-scene-cb2` | "What? SANDSHREW is evolving!" | ✅ |
| `c20-evolved-sandslash` | "Congratulations! Your SANDSHREW evolved into SANDSLASH!" | ✅ |
| `l1-flew-to-pokemon-league` | the league steps, player on the tile below the door | ✅ |
| `l1-sidney-challenge` | "Welcome, challenger! I'm SIDNEY of the ELITE FOUR." | ✅ |
| `l1-sidney-defeated` | "You've got what it takes to go far. Now, go on to the ne…" | ✅ |
| `p4-excursion-after-dimsfix` | avatar standing in the hot spring beside two bathers, PC to the right | ✅ (byte-distinct from the phase-25 C2 capture of the same spot) |

---

## 8. Hygiene

```
85dd487ee7681182…  ~/Library/Application Support/Azahar/sdmc/3DGBA/settings.bin
72c100ad2e385c69…  ~/Library/Application Support/Azahar/sdmc/3DGBA/recent.bin
dd20792e32878733…  ~/Library/Application Support/Azahar/config/qt-config.ini   28545 B, 8 Aug 19:53
d222c5cd446db2e4…  roms/emerald-fix.sav                                        14 Aug 16:37
```

All re-hashed by me, all matching. `roms/` mtimes all predate the lane's first boot (04:28 local).
Instance a **is** the user's profile (`instance.py`), and it is ROM-less. Instance b is ROM-less.
No Azahar running. `tools/closeout.sh` reports **CLOSE-OUT CLEAN** with one pre-existing duplicate
warning. The only false line in the hygiene section is F1.

---

## 9. What the lane did right, in the places I most expected to find a lie

1. **It went and got the screens.** Two rows written off as unreachable are now screens with
   pixels, game-produced logs and a game-produced delta behind them. E19 is the cleanest VERIFIED
   in several phases: the metatile scan, the elevation rule, the two timeouts and the `fieldLock`
   release all agree, and I reproduced every one of them from primary sources.
2. **It re-derived the audit's own recipes instead of inheriting them** — and recorded the off-by-one
   that nearly produced a wrong "the audit is wrong". A scanner that disagrees with a cited claim is
   a reason to check the scanner first; that sentence should outlive this phase.
3. **It published two measurements that went against it.** The live P4 disproved the improvement the
   lane expected from its own fix, and TEST 7b now pins the defect *in code* so a future fix goes
   red. The C20 hold's cancel is reported UNPROVEN with the engine line that explains why and a
   ten-minute recipe to close it. Neither was rounded up.
4. **The fix is a real barrier, not a rubber stamp**, at both call sites, and the anti-test it
   replaced was re-pointed rather than deleted.
5. **The `.cia` was rebuilt and the binary proves it** — standing item 1 is closed for this phase by
   disassembly, not by a timestamp.

---

## 10. Row status — no softening

**Genuinely DONE:**

- **E19 — Battle records: DONE.** Visited solo, screen captured, and the tap proven by a state delta
  the *game* produced (`fieldLock` 1→0 from `releaseall`). Meets the house definition of VERIFIED.
- **Phase-25 remediation item 9 (the `excseq` dims residue): DONE as code**, at both re-arm sites,
  mutation-tested red three ways, shipped in the `.cia`. With the caveat the lane itself published:
  **it changes nothing on the live P4 trace**, and TEST 7b now says so in the suite.
- **Phase-25 remediation F1 (the false reason in `source/gamestate.c`): DONE.** Replaced with a
  chain I re-verified line by line against pret master, plus the `gHasHallOfFameRecords` sting, plus
  the rev0 note remediation item 6 asked for.
- **Standing item 1 (`.cia` currency): DONE for this phase**, verified in the binary.

**NOT done:**

- **C20 — Evolution scene: PARTIAL, and the TOUCH-PLAN grade overstates it.** Done: reach, the
  `[exact]` `0x0813E3A4` fingerprint read off the live screen, `ctx 10 = GCTX_FULLUI`, and the
  `fieldLock/dlgOwns 0/0` fact that makes the phase-25 promotion the only thing classifying it.
  **Not done: both verbs.** The hold's CANCEL fired outside the engine's window (the lane says so).
  The tap has no game-side delta and **cannot have one** — `A_BUTTON` is read only inside
  `EVOSTATE_REPLACE_MOVE` (the lane does not say so). FireRed half still sym-only.
- **L1 — Hall of Fame: NOT DONE.** Not reached. Distance covered and honestly stated: flew to the
  POKéMON LEAGUE by one tap, lobby, Hall5, Sidney defeated. Four members + the champion remain,
  ~60-75 minutes measured.
- **L2 — Credits: NOT DONE.** Not reached. Correctly re-graded from "UNREACHABLE by construction"
  (false) to **budget-gated, jointly with L1**; the chain is ungated and I verified it.
- **The excursion's remaining wasted BFS** — the observe-a-CHANGE rule with a frame grace is
  specified, not taken; TEST 7b goes red when someone takes it.
- **The boot anomaly** — three sightings across two lanes, still unexplained.
- **The gate** — `test_progtap` must join it (O1), and a real all-suites runner must print the total
  (O2), or the next lane will hand-pick a list again.

---

## 11. Remediation, in priority order

1. **Correct the instance-b paragraph in `LANE-R-REACH.md`.** Both instances are clean; the O6
   residual is closed; the command handed to the next lane is a no-op. State that the *decision* not
   to touch a possibly-live instance was right and the *fact* was not re-checked. **(F1)**
2. **Add `test_progtap` to the gate and correct the totals** — 19 suites, **1 290 973** checks at
   `1c797ac`. **(O1)**
3. **Write the runner the commit message already claims exists** — one script that builds and runs
   every `test/host/test_*.c` **discovered by glob**, refuses to run without `data/` and `roms/`, and
   prints the total. A hand-maintained list is what produced O1; discovery is what prevents it.
   **(O2)**
4. **Re-grade C20 in TOUCH-PLAN** to `VERIFIED — reach + fingerprint + classification; both verbs
   UNPROVEN`, and add the engine fact that the tap can never have a game-side delta here. **(O3)**
5. **Archive gdb transcripts into the run folder** so a live claim is never single-sourced in a
   markdown file. **(O6)**
6. Fix Entry 5's "nine" → thirteen. **(O4)**
7. Keep the deleted-fixture reflex in check: do not delete the artefact that would settle a claim
   you are making about it. **(O5)**

### The pattern, one phase on

Phase 25's lesson was *a caveat is a claim*. Lane R applied it to four rows — brilliantly, with
primary sources, and twice against its own interest — and then, in the last paragraph it wrote,
inherited a caveat about its own housekeeping and shipped it as fact. The rule is not hard to state
and it is not hard to apply to the interesting rows; it fails on the boring ones. **The next
refinement is mechanical, not moral: anything the close-out asserts about the state of a directory,
a process or a file should be produced by a command in `tools/closeout.sh`, not typed.** Note that
`closeout.sh` already checks staged fixtures on both instances — and reports them clean. The gate
the lane helped ask for already knew the answer.

---

## Disposition (main session, 2026-08-15)

Verified independently before accepting.

**F1 — CONFIRMED, and it is the sharpest finding of the whole run.** The lane closed by reporting
that instance b was still dirty and "a concurrent session owns it", handing the next lane a command
to run. I read the file it cited:

```
tools/emutest/state-b/azctl.lock:  pid=56654 cmd=clean-fixtures at=2026-08-15T02:28:59Z
```

The lock's single line of contents **names the command as the cleanup itself**. The lane read the
timestamp and inferred an active session; `find` over both instance dirs returns no `.gba`/`.sav`
at all. Both instances were clean, and the handed-over command is a no-op.

This is the **eighth** occurrence of the negative-claim failure, and the most instructive: it
happened *inside the phase created to cure that exact failure*, in a report whose own opening
paragraph re-states the rule. The decision not to blind-clean a possibly-live instance was correct;
the *fact* wrapped around it was false, and one `cat` would have shown it.

What that teaches, beyond "check your negatives": **a stale artefact is not evidence of a live
process.** An mtime says when something last changed, never who owns it now. The lane had the
authoritative answer open and read the wrong part of it.

**O1/O2 — CONFIRMED.** The tree has **19** suites (`ls test/host/test_*.c test/test_*.c`), not the
18 the lane gated on; `test_progtap` (1405 checks) was already on `main` and in the previous phase's
audit. And "the total is printed by the runner now" describes a runner that does not exist —
`tools/closeout.sh` never touches the suites. Machine-summing a hand-picked list is precisely what
produced the wrong 18.

**O3 — CONFIRMED, and acted on.** C20 was graded `VERIFIED (EM)` on `dlgTaps`/`dlgHolds`, which are
**our own counters**. In `Task_EvolutionScene`, `A_BUTTON` is read at exactly one place
(`evolution_scene.c:928`, inside `EVOSTATE_REPLACE_MOVE`) — the animation and its messages ignore A
entirely. So on that screen a tap *cannot* produce a game-side delta, and no amount of re-running
will make it. The row is re-graded **SCREEN VISITED; VERBS UNPROVEN**, which is the true statement:
reaching the evolution scene by touch (withdraw the mon, feed the candy) is a real and hard-won
result; "tap = A works here" was never shown.

**What the lane got right, and it is the bigger half:** E19 and C20 were both reached, both had been
declared unreachable, and both were reproduced from pret before being trusted. The excursion fix
landed with mutation numbers 3/4/6 — and then its own live re-proof **disproved the improvement it
expected**, because the departing map's dimensions stay *stable* through the fade, so "wait for
stable" is satisfied by the old map sitting still. The lane pinned that as a deliberate defect
(`test_excseq` TEST 7b) instead of quietly claiming the win. That is the behaviour to keep.

L1/L2 were re-graded from "UNREACHABLE by construction" to **budget-gated** with the distance
stated as a distance: flew to the League by touch, cleared Sidney, ~60–75 minutes of Elite Four
remaining, Hall of Fame and credits not reached. An honest partial beats a false absolute.
