# PHASE 29 — ADVERSARIAL AUDIT

_Auditor pass, 2026-08-15, on `main` at **`4272812`** (working tree clean apart from the untracked
`.claude/worktrees/`). Two lanes audited: **F** (`dc1e4b3`, `406ed59`, `c9611b4`, `9e8ca56`,
`cd70d38`, `4272812`) against `docs/phase21-touch-census/LANE-F-EXECUTOR.md`, and **E** (`db4df51`)
against `docs/phase21-touch-census/EVIDENCE-INTEGRITY.md`. Baselines: `PHASE28-AUDIT.md` and phase
25's Disposition._

The brief: (1) did the Waterfall ride complete under program control — decided by evidence and
state reads, not narrative; (2) is W2's safety negative **measured**; (3) mutate every new/changed
test and report counts, because an inert mutation is an uncovered branch; (4) check the
evidence lane's NEGATIVES independently, especially whether the cb2 identification was really
banked apart from the capture; (5) every new constant — right decomp, right revision, no
sibling-engine arithmetic; (6) run the gate myself.

**Headline: this is the cleanest lane pair the family has produced — every mutation number in both
reports reproduces to the digit, and the two hardest constants (the currents block and the Ever
Grande fly cell) survive independent re-derivation from pret. Three things are wrong.** The
harness transcript's run-4 control proves the opposite of what it says (its banked frame is
Azahar's own loader, and its log shows the emulated workers never started); lane E's three
"worst offender" callback counts are wrong and one has been copied into `REPORT.md`; and X1's
stated invariant mirrors `facingDirection` while the engine's precondition tests
`movementDirection`. **And the answer to the brief's first question is NO: nothing ran live at all
this phase, so the ride has still never completed under program control, and W2's negative has
still never been measured.**

---

## 0. What I actually ran (nothing below is quoted from either lane)

| # | Check | Result |
|---|---|---|
| 1 | All **19** host suites rebuilt and re-run at `4272812`, CWD = project root | 0 failures; **every count matches lane F's §5 to the digit** — §7 |
| 2 | `tools/closeout.sh`, verbatim | **CLEAN, exit 0** — §7 |
| 3 | **8 mutations of `progseq.c`** (the lane's 2 + 6 of mine) | the lane's two reproduce **exactly** (3 / 1); all 8 bite — §3 |
| 4 | **13 mutations of `fieldtrav.c`** (the lane's 6 + 7 of mine) | the lane's six reproduce **exactly** (1/7/1/3/1/7); **2 of mine are INERT** — §3 |
| 5 | **7 mutations of `progtap.c`** (the lane's 4 + 3 of mine) | 491 / 141 / 4 reproduce exactly, and the O2 hazard reproduces at **76** with a narrowing I chose myself; **1 inert** — §3 |
| 6 | **3 mutations of the O7-rewritten TEST 24 subject** | the Dive guard now costs **8** (was 7 — O7 strengthened it); **the off-by-one is INERT** — §3 |
| 7 | 3 phase-28 tap-gate mutations re-run at the new check count | 388 / 295 / 19, unchanged — TEST 7 added ~4 000 checks without weakening TEST 1 |
| 8 | Every pret citation in the X1/X2 diffs re-read in the local decomp *and* against pret master | all exact; **one new drift found** (O9) — §5 |
| 9 | The three O5 corrections fetched from pret master | EM `:193` ✓, EM `:147` ✓, **Ruby `:126` in `data/field_move_scripts.inc`** ✓ — genuinely Ruby's file, not Emerald arithmetic |
| 10 | `region_map_layout.h` + `region_map.c` fetched and parsed | Ever Grande at rows **8 and 9**, col **27** of 28; `MAPCURSOR_X_MIN/Y_MIN = 1/2`; the cursor walk confirms lower cell = `posWithinMapSec` 1 — §5 |
| 11 | Every phase-29 run dir opened; `cmdline.txt`, `events.log`, `azahar_log.txt` read; `azshots/` listed | run 5's control is genuine; **run 4's is not** (F1) — §2 |
| 12 | Both banked `EM-P29-*.png` hashed and **opened** | `e21b9cba…` = the frozen GAME FREAK frame ✓; `56e41a7e…` = **Azahar's "Launching…" splash** (F1) |
| 13 | All 685 PNGs under `evidence/` hashed and every duplicate group classified by my own subject rule | reproduces lane E's map **group for group and member for member** — §6 |
| 14 | Per-pixel varying bbox over 40 emerald tops and 40 firered bottoms | (80,8)-(375,200) → below-HUD (80,40)-(320,200); FR (40,40)-(280,200) — **lane E's rect, reproduced** |
| 15 | `CAPTURES-*.log` re-parsed and cross-joined to the banked captures | 74/59/12/47 and 96/82/6/76 reproduce **exactly** by cb2 ADDRESS; the two "no read" lists match member for member — §6 |
| 16 | **Every distinct cb2 triple resolved against the byte-matched sym maps** | **49 of 49 exact**; **25 of 25 FireRed triples are rev1-ONLY** — §6, and the answer to brief item 4 |
| 17 | `VISITED-*.md` re-parsed with compound labels expanded | **43 + 42 − 24 = 61** ✓ |
| 18 | `see.py shot()` read end to end; `evidence/` searched for any banked raw state capture | stateless grab, no state parameter, no memory; **no census gdb output banked anywhere** — §6 |
| 19 | `tools/emutest/tests/test_censusguard.py` | **Ran 16 tests … OK**, exit 0 |
| 20 | `fstate.py`'s 59 FIELDS cross-checked field-by-field against `FieldDbg` | **exact**, every pre-existing offset unchanged; `planForced +0xE0`, `planForcedN +0xE4`, `progFaceEnter +0xE8` ✓ |
| 21 | `nm` + `objdump` on the shipped `3DGBA.elf` | exports all four new symbols; `fieldtrav_is_current` disassembles to `sub r0,r1,#0x50; cmp r0,#3` — **the `.cia` really carries the fix** |
| 22 | `git log --all --diff-filter=A` for `*.gba`/`*.sav`; `git diff 010a138 -- source/fieldpath.{c,h}`; commit authors | nothing binary ever committed; **fieldpath byte-identical** (`f1e6d95d…`/`6a52bb1f…`); every commit on the no-reply identity |
| 23 | Both emulator instances' `settings.bin`; `pgrep azahar` | b restored to `8fe6a815…` ✓; **a still `traverse = 1`** at offset 100 (`85dd487e…`); no Azahar running |
| 24 | Callers of `fieldpath_plan` across `source/` | **exactly one** (`walk_plan`, `touch.c:477`) — the choke-point claim is TRUE |

Suite counts I measured at `4272812`, all 0 failures — celiolink 1259 · control 6940 · diag 376 ·
excseq **689** · **fieldpath 1808 (FROZEN)** · **fieldtrav 4199** (ROM half ran: no PARTIAL SKIP, no
TEST 25(d) NOT-RUN banner) · netlink 66 · peersprite 62078 · presence 61376 · profiles 2746 ·
**progseq 924** · **progtap 5402** · theme 83444 · tilt 1756 · touchgeom 1040833 · trace 58 (+4
SKIPs) · typography 1419 · uigeom 18332 · uihit 1834. **19 suites.**

---

## 1. Verdicts

### CONFIRMED

| Claim | What decided it |
|---|---|
| **X1's cause really is one clause, and the clause is derived from the engine** | Every cited line is exact in the local pokeemerald: `CheckMovementInputNotOnBike` **`field_player_avatar.c:588-596`** (TURN_DIRECTION only while `direction != GetPlayerMovementDirection()`), `PlayerTurnInPlace` **`:1027`**, `TryInterruptObjectEventSpecialAnim` **`:353`** (and I read its body: a `WALK_IN_PLACE_FAST` held movement falls outside the interruptible id range, so the whole 8-frame turn really does swallow input). The one link the lane left uncited I supplied: `InitMoveInPlace` (`event_object_movement.c:5704-5711`) calls `SetObjectEventDirection` **first**, which writes `facingDirection`/`movementDirection` immediately (`:2361-2371`) — so the new facing IS readable on the turn's first frame, which is what makes "exactly one key per turn" true. *(The field it is read from is the wrong one — O2.)* |
| **The X1 fix addresses the measured failure** | Phase 28's live tap was planned at `progFacing = 2 (UP)`, i.e. already facing. Under the fix, `faceEnterable = 1` + `face == want` accepts on frame 1 and emits **no key at all**, so the step that put the player on (12,12) cannot happen. |
| **X1's two mutation numbers** | Reproduce **exactly**: dropping `\|\| o->faceEnterable` = **3**, dropping the unreadable-facing refusal = **1**. Six more of mine all bite (§3). |
| **The second half of X1 is real and now graded** | `data/scripts/field_move_scripts.inc:193` is exactly `msgbox Text_MonUsedWaterfall, MSGBOX_DEFAULT`, `:194` `dofieldeffect FLDEFF_USE_WATERFALL` — fetched from pret master. Killing TPH_DONE's cadence costs **3**; making it a single edge costs **3**. |
| **X2 is applied at the real choke point** | `fieldpath_plan` has **exactly one caller in `source/`** — `walk_plan`, `touch.c:477` — and the screen runs there, before any walker state is committed (`s_mapW`/`s_goalX`/… are assigned after the `return false`). "Every leg of every route" is literally true, and the `gate == 2` branch genuinely does not restate the rule. |
| **X2's six mutation numbers** | Reproduce **exactly**: 1 / 7 / 1 / 3 / 1 / 7. |
| **The currents constant, in all three engines' own headers** | pokeemerald `metatile_behaviors.h:85-88` — and I did not take the line numbers on trust, I counted the enum: `MB_NORMAL` at `:5` is 0x00 and `MB_EASTWARD_CURRENT` at `:85` is **0x50**, `MB_SOUTHWARD_CURRENT` at `:88` is **0x53**. pokefirered `:62-65` are literal `#define … 0x50 … 0x53` ✓. pokeruby `:84-87` ✓ (0x50 spelled `MB_UNUSED_EASTWARD_CURRENT`). The four single-value predicates are at `metatile_behavior.c:409/417/425/433` ✓. **No sibling-engine arithmetic anywhere.** |
| **The shipped `.cia` carries the fix** | `.cia` 07:32 > `.elf` 06:45; `nm` exports `fieldtrav_is_current`, `fieldtrav_path_forced`, `progtap_needs_waterfall`, `progtap_needs_strength`; and `objdump` shows `fieldtrav_is_current` as `sub r0, r1, #0x50 / cmp r0, #3` — the range test itself, in the install target. |
| **O2 is genuinely closed, and its headline number is independently reproducible** | I did not use the lane's narrowing. I narrowed rule (2) with a different predicate (`&& o->topY >= 0`) and left `progtap_needs_strength` alone: **76 failures**, the lane's number to the digit. Narrowing both consistently drops it to 6 (TEST 1's oracle, correctly). `needs_strength` always 0 = **491**, `needs_waterfall` always 0 = **141**, both always 1 = **4** — all exact. |
| **TEST 7 did not weaken TEST 1** | Phase 28's tap-gate mutations still bite identically at the new count: REFUSE→SHIPPED **388**, drop the boulder rule **295**, `>`→`>=` **19**. |
| **O5 — all three citation corrections are right, and the Ruby one is genuinely Ruby's** | Fetched from pret master: pokeemerald `data/scripts/field_move_scripts.inc:193` msgbox ✓, `:147` `setflag FLAG_SYS_USE_STRENGTH` ✓, `:124` `EventScript_StrengthBoulder` / `:126` `FLAG_BADGE04_GET` ✓. Ruby's is at **`data/field_move_scripts.inc`** (a *different path* — `data/scripts/` 404s), where `S_PushableBoulder` is `:124` and BADGE04 `:126`. The two engines agreeing on `:126` is a coincidence I checked rather than inherited. |
| **O6 — the count correction** | `excseq` really is **689** at this tree. *(The base-commit half is only half-applied — O4.)* |
| **O7 — the tautology is gone and the replacement is a real invariant** | `CHECK(planned == 0 \|\| planned > 0, …)` is deleted; `refused + planned + other == 25*25 − 1` is a genuine bookkeeping check, and removing the Dive tier-order guard now costs **8** failures rather than phase 28's 7 — the rewrite strengthened the test by exactly the property it added. |
| **O8 — the BUILDLOG entries exist, both of them** | `BUILDLOG.md:187` "PHASE 28 / lane X (recorded late; the phase had no entry — audit O8)" and `:221` "PHASE 29 / lane F". |
| **W1 and W2 were NOT executed, and the report says so without softening** | Verified independently, not accepted: **all ten phase-29 `runs-b/` dirs have an EMPTY `azshots/`**, no netlog or gs-log was produced, and the only successful read in the entire lane is `g_renderSeq = 663`. Nothing about a waterfall, a boulder or a program was observed live. |
| **The blocker measurement's control (run 5) is genuine** | Not taken on trust: `runs-b/20260815-041245/cmdline.txt` really launches `.claude/worktrees/lane-x-ph28/3DGBA.3dsx`, its `boot.json` really says Azahar **2125.1.2**, and its `azahar_log.txt` really stops at `unimplemented SVC function 11 GetCurrentProcessorNumber` at **t = 20.428 s** — the same instant as the phase-29 builds (20.380 / 20.419 / 20.393). The banked `EM-P29-harness-gdb-wedge-frozen-at-gamefreak.bottom.png` hashes to **`e21b9cba…`**, the transcript's own value, and shows the emulated GAME FREAK intro. **The freeze is not a regression from lane F's changes.** |
| **The Ever Grande fly cell, re-derived from pret rather than from the lane** | `region_map_layout.h`: `MAPSEC_EVER_GRANDE_CITY` at rows **8** and **9**, column **27** of a 28-column layout. `region_map.c:43-44` `MAPCURSOR_X_MIN 1` / `MAPCURSOR_Y_MIN 2` → cursor cells **(28,10)** and **(28,11)**. `GetPositionOfCursorWithinMapSec` scans left and up, so the upper cell is 0 (the League gate) and the **lower cell is 1 = `HEAL_LOCATION_EVER_GRANDE_CITY` = (27,49)** — the audit's F1 correction, and lane F's row/cursor numbers, both exact. The token route `L1 D1 L1 D2 L5 D3` is 7 left + 6 down = (27,49)→(20,55) in **13** steps ✓. |
| **`fstate.py` is banked and its offsets are right** | All **59** `FIELDS` entries match `FieldDbg`'s declaration order field-for-field (the multi-declaration lines expanded), and every stated offset checks out — `kind +0x24`, `progFacing +0xA8`, `progObjY +0xDC`, `planForced +0xE0`, `planForcedN +0xE4`, `progFaceEnter +0xE8`. **Every pre-existing offset is unchanged**, so phase-28 tooling still reads the same fields. |
| **`planForced`'s "nothing" value is −1 at both initialisers** | `g_fieldDbg = { .planForced = -1 }` at the definition **and** re-set after the `memset` in `touch_log_reset`. A fresh boot cannot read as "refused at step 0". |
| **`fieldpath.{c,h}` frozen · no ROM/save ever committed · correct git identity** | Byte-identical to phase 18's `010a138` (`f1e6d95d…` / `6a52bb1f…`, the phase-28 audit's own hashes); `git log --all --diff-filter=A` finds no `*.gba`/`*.sav`; all seven phase-29 commits are authored `Guy Shtainer <293649481+GuyShtainer@users.noreply.github.com>`. |
| **Instance b was left clean; instance a really is still dirty** | b's `settings.bin` = `8fe6a815…`, the pre-phase value. a's = `85dd487e…` with **offset 100 = 1**. I read the byte myself. The standing item is genuinely open, third recurrence — and the lane's self-correction about the mtime inference (`4272812`) is the right correction, made in the right direction. |
| **Lane E's duplicate map is exactly right** | My own walk of all 685 PNGs: **20 duplicate groups / 54 files** (685/651 today vs lane E's 683/649 — the delta is lane F's two harness PNGs, both distinct). Every group's membership matches. The 13-file cluster and the 5-file cluster are **all `emerald/*.bottom.png`**, and the 18 subject `.top.png` counterparts are **18 distinct files**. The four screens the original alarm named are four different pictures. |
| **Lane E's subject rect is measured, not assumed — and it reproduces** | My own per-pixel variance over 40 emerald tops: varying bbox **(80,8)-(375,200)**; restricted below the HUD, **(80,40)-(320,200)** = 240×160 at (80,40). Over 40 firered bottoms: **(40,40)-(280,200)** — i.e. *zero* variation outside the game rect, exactly as claimed. |
| **Lane E's identification table** | Reproduces **exactly** when grouped by cb2 ADDRESS (which is the honest key): EM banked 74 / with-read 59 / unique **12** / shared 47; FR 96 / 82 / **6** / 76. The 29 captures with no read at all match **member for member**, both lists. "18 captures" (12 + 6) is right. *(The prose "worst offenders" numbers beside the table are not — F2.)* |
| **The 61-row count reconciles** | Independent parse with compound labels (`E2/D7-family`, `B1/I-family`, `C2/E3`, `C5/C6`) expanded: **43 Emerald + 42 FireRed, 24 in both → 61**. Lane E's correction of `REPORT.md`'s "41 + 41 / 21 FR-only" stands. |
| **The withdrawal costs no catalog row, and no other row leans on a withdrawn or mis-named file** | Nothing cites `I4c-slotspin` as evidence; `VISITED-firered.md:126`'s I4 row retains `I4-slotmachine.*` with its `[exact]` cb2. `E13a-playerpc-menu` is cited by no row and E13/E14 are already under "Honest not-visited" (`VISITED-emerald.md:105`). `COVERAGE.md` carries no census count — I grepped rather than assumed. |
| **`censusguard` is real and its gate is wired** | `test_censusguard.py` — **Ran 16 tests … OK**, exit 0, including the live regression against the banked slot pair. `closeout.sh` §6 runs `censusguard audit` and reports **"170 census-row subject frames (+90 non-claim), 10 duplicate group(s) — 0 naming two census rows"**. |
| **Brief item 4 — the cb2 reads themselves ARE independently checkable, and they check out** | This is the one thing lane E under-claimed. I resolved **every distinct `(engine, address, symbol)` triple** in both CAPTURES logs against the byte-matched sym maps: **49 of 49 resolve exactly**, and **all 25 FireRed triples resolve in `pokefirered_rev1.sym` and in NO rev0 map**. You cannot hand-invent 49 exact addresses on the right revision. The reads happened, on the user's cart. *(What is narrative is the BINDING — see below.)* |
| **…and lane E's account of why the binding is narrative is correct** | I read `see.py`'s `shot()` end to end (`tools/emutest/see.py:310`): find window → `_grab` → `window_geometry` → crop → save. No state parameter, no previous-capture memory, no settle; its only content check is `_uniform()`. `--with-state` exists on `rec`, not on `shot`. And `evidence/` contains **no banked raw gdb output for the census at all** — the only three `.txt` artefacts are `EM-P28-W1-gdb.txt`, `RS-P24-gdb-badgeprobe.txt` and this phase's harness transcript. So the identification is real and separately recorded; **what no artefact establishes is that a given read belongs to a given picture.** |

### OVERSTATED

**O1 — "the app runs fine with no gdb client" is the transcript's own contradiction.**
See F1: filed FALSE because it is a measured claim whose measurement says the opposite. Named here
too because the *consequence* is an overstatement of the whole isolation: with run 4 removed, the
transcript has **no working no-client control**. Run 8 (also no client) reported the identical
"Launching…" splash as a failure. What survives is still decisive — run 5 on phase 28's own binary
freezes at the same emulated instant with the same banked frame — but "the app runs fine without a
client" is not among the things this transcript established.

**O2 — X1's stated invariant mirrors the wrong field.**
`progseq.c`'s new comment says every key is issued under *"`direction != facingDirection` —
`CheckMovementInputNotOnBike`'s own TURN_DIRECTION precondition."* The engine's precondition tests
`GetPlayerMovementDirection()`, which returns **`movementDirection`** (`field_player_avatar.c:1187-1190`)
— the **high** nibble of `ObjectEvent+0x18` (`global.fieldmap.h:237-238`). `prog_facing`
(`touch.c:932-936`) reads `+0x18 & 0x0F` = **`facingDirection`**, the low nibble. `SetObjectEventDirection`
writes both, so they agree in the ordinary case and the fix works — but they diverge exactly when
`facingDirectionLocked` (`global.fieldmap.h:205`) is set, where `facingDirection` goes stale while
`movementDirection` advances. In that state FACE would keep pressing, every press would arrive with
`direction == movementDirection` → MOVING → **the swim, back again**. Narrow, but the invariant as
written is not the field that is read, and a fix whose whole argument is "we mirror the engine's own
precondition" should mirror the engine's own field. One line: read the high nibble too, and require
both.

**O3 — the covered files are covered; the wiring that makes them fire is not.**
`progseq`, `progtap` and `fieldtrav_path_forced` are all mutation-graded. The three things that
decide whether any of it runs are graded by nothing, because **`touch.c` still has no host suite**
(phase-24 audit O2, still open): `o.faceEnterable`'s map read + its `fdx/fdy` table + the `surfing`
gate; the `walk_plan` call site; the `g_fieldDbg` mirrors. TEST 13 proves the sequencer does the
right thing *given* the observation. Nothing proves `touch.c` computes the observation. (I checked
the direction tables by eye and they are consistent — `FP_R/L/D/U = 0/1/2/3` with
`dx {1,−1,0,0} / dy {0,0,1,−1}` in `fieldpath.c:248`, `fieldtrav_path_forced`, `touch.c`'s `fdx/fdy`
and `progseq`'s `DIR_*` — but "I checked by eye" is what this project keeps replacing with tests.)

**O4 — O6's base-commit correction is half-applied.**
`LANE-X-EXECUTE.md` §5's parenthetical now names `8897071`, but the document's **header (line 5)
still says the worktree is "off `ba216e5`"** — the exact sentence the phase-28 audit flagged.
`git merge-base --is-ancestor 8897071 lane-x-ph28` succeeds and `git rev-list --count
ba216e5..lane-x-ph28` = 6. A correction that leaves the wrong number in the more prominent place
is half a correction.

**O5 — the X2 fix changes shipped-default behaviour, and the reports do not say so plainly.**
Everything phases 26/28 shipped was behind `smartTraverse ≥ 1` (still defaulted to 0 —
`theme.c:35`). The X2 screen is **not** gated: it runs inside `walk_plan`, which is the ordinary
tap-to-walk router at level 0. That is the lane's own stated design goal and it is the right call —
but it means **phase 29 is the first traversal change that alters what a default install does**, and
it has never run in an emulator, let alone on hardware. Both reports frame the screen as closing a
hole; neither frames it as a default-path behaviour change awaiting its first live frame.

**O6 — lane E's headline double-uses its own denominator.**
"Of the **170** census-row subject frames, **169** are distinct" (§ headline) against "**170** …
all distinct, **+1 withdrawn**" (§4). I measured: 74 EM tops + 96 FR bottoms = **170 surviving, all
170 whole-file distinct**; with `I4c` that is 171 captured of which 170 distinct. The headline uses
170 as both the pre- and post-withdrawal count. It is precisely the "a count needs a denominator"
failure the lane's own §7 names one page later.

**O7 — the banked tap calculator does not reproduce the report's tap.**
`LANE-F` §3.1 gives touch **(305,137)** for the fly cell, "round-trip asserted through the app's own
integer inverse by `fstate tile`/`fstate tap`". Run it: `fstate.gba_to_touch(228, 92, 1)` returns
**(304,136)**. Both map back to GBA (228,92) under `main.c`'s `touch_to_gba` — I checked both — so
the plan is unharmed; but the stated provenance does not reproduce, and the next session will
compare its tool's answer against a report that disagrees with it.

**O8 — audit O3 ("bank the raw capture; it is one `tee`") is closed halfway.**
`fstate.py` is in the tree and correct, and one of the transcript's two load-bearing frames is
banked as a PNG whose hash matches. But **every `azshots/` directory in all ten run dirs is empty**,
no `tee`d read was banked, and so the *attribution* of the frozen frame to run 5 rather than run 3
rests on the transcript alone. The transcript also contains one transcription slip of its own kind:
it gives RUN 1 the timestamp **20.38 s**, but `runs-b/20260815-035148/azahar_log.txt` stops at
**18.52 s** — 20.38 s is RUN 3's number. Nothing turns on it; it is the same class the audit named.

**O9 — new citation drift, in the lane that fixed three.**
`fieldtrav.c:777` cites *"`sForcedMovementTestFuncs[6..9]` (pokeemerald src/field_player_avatar.c:158)"*
and `fieldtrav.h:171` cites *":158-160"* for the currents **and** the waterfall. The **indices are
exactly right** (`[6..9]` = the four currents, `[14]` = `MetatileBehavior_IsWaterfall`), but the
lines are not: the array's element 0 is at `:146`, so the currents are at **`:152-155`** and the
waterfall at `:160`. `:158` is `MetatileBehavior_IsSlideWest`, and the range `:158-160` excludes the
currents entirely.

### FALSE

**F1 — "RUN 4 … DISTINCT frames, 40 % CPU: the app runs fine with no gdb client."**
The banked frame for that run, `EM-P29-harness-no-gdb-client-runs.bottom.png`
(`56e41a7e…`, the transcript's own hash for run 4's first shot), is **Azahar's own "Launching…"
splash** — a black screen with a loader progress bar. I opened it. And
`runs-b/20260815-041025/azahar_log.txt` is **12 331 bytes ending at t = 0.415 s**, with **no
`GetCurrentProcessorNumber` line anywhere in it** — the marker that every other run logs at ~20.4 s
when `worker_main` calls `svcGetProcessorID`. The emulated GBA workers never started; the app never
reached a game session. The "distinct frames" are the progress bar advancing.

This matters twice. First, it is the *same observation* the transcript reports six rows later as a
**failure**: RUN 8, "Azahar's own 'Launching…' splash, still there after 6 minutes, frames
changing". One phenomenon, reported once as proof the app is healthy and once as proof it never
started. Second, it means the isolation has no working no-client control at all, so "the freeze
needs a gdb client" is supported only by the *contrast with phase 28's earlier 53-minute session*,
not by anything measured in this transcript. The load-bearing conclusion — that the freeze is not
lane F's regression — survives intact on run 5, which I verified independently and which is the row
the report itself calls the one that settles it.

**F2 — lane E's three "worst offender" callback counts are wrong, and one has been propagated.**
Measured over the banked captures, grouping by the cb2 read at capture time:

| lane E says | measured |
|---|---|
| `FR:CB2_Overworld` covers **32** FireRed captures | **30** |
| `EM:CB2_Overworld` covers **10** | **7** |
| `EM:CB2_Pokedex` covers **all 7** dex captures | **6** (F1, F2, F2b, F3, F4, F5 — there is no seventh) |
| `EM:MainCB2` covers the four summary pages *and* the forget-move picker | **5** ✓ |
| `FR:CB2_RunSlotMachine` covers **2** | **2** ✓ |

The **table** in §3.1 is exact to the digit; it is the prose beside it that is wrong. And the "32"
has been copied into the corrected `REPORT.md:13` — whose Pokédex figure in the same sentence says
**6**, so the two documents lane E rewrote now disagree with each other. Filed FALSE rather than
OVERSTATED for the same reason phase 28 filed the excseq count that way: these are measured numbers
stated as measured, in a lane whose entire subject is counting the evidence honestly.

---

## 2. The brief's first question, answered plainly

> **Did the Waterfall ride actually complete under program control this time?**

**No. Nothing ran live at all.** This is not the lane's claim taken at face value — I looked for the
evidence that would exist if it had:

* all ten phase-29 `runs-b/` dirs have an **empty `azshots/`**; no screenshot the harness took in
  this lane was banked in its run dir;
* no netlog, no `gs_*` game-state log, no `touch_*` log was produced by any phase-29 boot;
* the **only** successful state read in the entire lane is `g_renderSeq = 663` (run 2), which is a
  frame counter, not a field observation;
* `g_fieldDbg`'s program fields — `progPhase`, `progEnd`, `progAnswers`, `progAKeys`, and the new
  `planForced`/`planForcedN`/`progFaceEnter` — have **never been read on a running game**, this
  phase or any other since they were added;
* the banked frozen frame is the emulated **GAME FREAK intro**, i.e. the app froze before either
  game reached its title screen, let alone Route 114.

So the state of the Waterfall executor is: **the fix for the defect that broke it is host-graded,
mutation-graded and derived correctly from the engine's own source — and the ride has still never
completed under program control.** Phase 28 proved the planner and disproved the executor; phase 29
fixed the executor on paper. Nothing has moved the live line.

The one thing that *did* advance is the arc for next time, and it survives audit: the K=8 site is
fully derived (fall `0x13` at x15..26 y60..67, landing y59, pool y68), the fly cell is the **lower**
of the two Ever Grande cells (layout row 9 → cursor (28,11)), and the walk is 13 steps. I
re-derived the cell from pret independently. The tap coordinate is one digit off the tool that
allegedly produced it (O7) but lands on the same GBA pixel.

---

## 3. Mutations — 31 of them, and five inert

Base: `progseq` 924 · `fieldtrav` 4199 · `progtap` 5402, all 0 failures. Each mutation applied to
the committed tree, built, run, reverted; the tree is clean and the three suites re-verified at
their base counts afterwards.

### X1 — `progseq.c` (lane: 2, mine: 6)

| # | mutation | failures |
|---|---|---|
| 1 | drop `\|\| o->faceEnterable` (= the pre-phase-29 rule) | **3** (lane: 3 ✓) |
| 2 | drop the unreadable-facing refusal | **1** (lane: 1 ✓) |
| 3 | the escape fires ALWAYS (accept the moment facing matches, even on an impassable tile) | 5 |
| 4 | drop the `face == want` test entirely | 712 |
| 5 | the unreadable-enterable refusal ends `ARRIVED` instead of `STALL` | 1 |
| 6 | the refusal fires on ANY unreadable facing (impassable included) | 2 |
| 7 | TPH_DONE never pulses A (the `MSGBOX_DEFAULT` box never closes) | 3 |
| 8 | TPH_DONE pulses once (an edge, not a cadence) | 3 |

**All eight bite.** #3 and #6 are the two ways to over-apply the new escape, and both are caught —
the "a Cut tree still gets its two-frame floor" assertion is doing real work.

### X2 — `fieldtrav.c` (lane: 6, mine: 7)

| # | mutation | failures |
|---|---|---|
| 1 | `transition()` drops the currents term (the phase-26 state) | **1** (lane: 1 ✓) |
| 2 | the path screen is inert (`return -1` always) | **7** (lane: 7 ✓) |
| 3 | the screen ignores currents | **1** (lane: 1 ✓) |
| 4 | `is_current` HI off by one (0x50..0x52) | **3** (lane: 3 ✓) |
| 5 | unreadable counts as forced | **1** (lane: 1 ✓) |
| 6 | the screen uses the wrong D/U sign | **7** (lane: 7 ✓) |
| 7 | `is_current` LO off by one (0x51..0x53) | 3 |
| 8 | the screen ignores waterfalls (currents only) | 6 |
| 9 | the screen returns the LAST forced index, not the first | 2 |
| 10 | `transition()` drops the WATERFALL term, keeps currents | 5 |
| 11 | a garbage step code REFUSES instead of answering −1 | 1 |
| 12 | `is_current` says YES to an unreadable behaviour | 1 |
| 13 | the screen skips the last step of the path | 5 |
| **14** | **L/R swapped in the screen's step table** | **0 — INERT** |
| **15** | **the screen also grades the STARTING tile** | **0 — INERT** |
| 16 | `pathLen <= 0` → `pathLen < 0` | 0 — a genuine no-op (the loop is empty either way) |

**The two inert ones are real coverage holes, and one of them is the mirror of a mutation the lane
did run.** The lane graded the **D/U** sign (7 failures) and never graded **L/R**: a screen that
walked the wrong way horizontally passes every check in TEST 25, because the only horizontal cases
are "a plain swim ALONG the pool is CLEAN" (clean either way) and the `[L,R,U]` detour (which
returns to the same tile whichever way the first two steps go). #15 is uncovered because no test
ever plans from a tile that is *itself* forced — which is exactly the state a player is in while
riding a fall.

### O2 — `progtap.c` (lane: 4, mine: 3)

| # | mutation | failures |
|---|---|---|
| 1 | `progtap_needs_strength` always 0 | **491** (lane: 491 ✓) |
| 2 | `progtap_needs_waterfall` always 0 | **141** (lane: 141 ✓) |
| 3 | both always 1 (degenerate into "always read") | **4** (lane: 4 ✓) |
| 4 | **the O2 hazard: narrow rule 2, do NOT widen the gate** — my own narrowing (`&& o->topY >= 0`) | **76** (lane: 76 ✓) |
| 5 | the same narrowing applied to BOTH consistently | 6 (TEST 1's oracle, correctly) |
| 6 | `needs_strength` drops the `goalIsWaterfall` short-circuit | 1 |
| **7** | **`needs_waterfall` drops the `goalIsWaterfall` short-circuit** | **0 — INERT** |

#4 is the strongest single result in either lane: the headline number reproduces exactly under a
narrowing the lane never wrote. #7 is inert because it is a pure *cost* regression (the gate asks
for a read it does not need), and TEST 7's "must not degenerate into always-read" half only bites on
full degeneracy. Not a defect; a named gap in the half of the property that is about cost.

### O7 — the rewritten TEST 24 (3 of mine)

| # | mutation | failures |
|---|---|---|
| 1 | remove the phase-28 Dive tier-order guard | **8** (phase 28: 7 — the O7 rewrite added one) |
| 2 | the guard fires always | 19 |
| **3** | **the guard reads the wrong window cell (`FP_WHALF + 1`)** | **0 — INERT** |

O7's new property — *"`fieldtrav_dive`'s TIER0 refusal agrees with `fieldtrav_plan`'s own tier-0
verdict on every goal, 0 disagreements over 25×25"* — is the right property, and it does not
discriminate an off-by-one in the guard's own index, because Route 126's dive field is one connected
body and tier-0 reachability is uniform across the window. The property needs a fixture with a
reachability boundary inside the window to bite.

---

## 4. W2's safety negative — still not measured, and the channel is now provably correct

The brief asked for the boulder's `currentCoords` **before and after**. It does not exist.

* `progObjSlot` / `progObjX` / `progObjY` are at `FieldDbg +0xD4/+0xD8/+0xDC` — I verified the
  offsets by expanding the struct and counting, and `fstate.py` names all three at the right
  indices. The channel is real and correctly wired.
* **No run has ever read them on a live boulder**, this phase or last. Phase 28 shipped them and
  never reached a boulder; phase 29 never reached a game session.
* Host-side, `test_fieldtrav` TEST 23 grades that Strength is a *terminal* — `fieldtrav_strength_tap`
  answers with the object slot for an eligible un-activated boulder and refuses a Cut tree, an empty
  tile and the tile beside it — and `test_progseq`'s Strength test grades that the executor completes
  on `FlagGet(FLAG_SYS_USE_STRENGTH)` and times out otherwise. **Neither of those is the negative.**
  "The boulder does not move" is still an argument from `edge_at`'s shape plus mutations, exactly as
  it was after phase 26 and after phase 28.

Lane F carries phase 28's two corrections forward correctly and without softening — ten boulder
sites, and the fixture save's `registeredItem = 259 = ITEM_MACH_BIKE`, so the Jagged Pass blocker is
a harness problem (a held input to reach `bikeFrameCounter == 2`), not a map one. That is the right
posture; it is also the third phase in a row in which the row's whole point has gone unmeasured.

---

## 5. New constants — all correct, one citation wrong

| Constant | Verdict |
|---|---|
| `MB_CURRENT_LO/HI = 0x50/0x53` | ✓ Counted from `MB_NORMAL` in pokeemerald's own enum (not inferred from a line number); literal `#define`s in pokefirered and pokeruby at the cited lines; the four game predicates are single-value compares at `metatile_behavior.c:409/417/425/433`. **Engine-invariant is correct** — all three engines use the same four values. |
| `field_move_scripts.inc:193` / `:194` | ✓ pret master, pokeemerald |
| `field_move_scripts.inc:147` (`setflag FLAG_SYS_USE_STRENGTH`) | ✓ pret master, pokeemerald — the lane found this drift in its own subject |
| Ruby `field_move_scripts.inc:126` (BADGE04) | ✓ pret master, pokeruby — and at **`data/field_move_scripts.inc`**, a different path from Emerald's `data/scripts/…`. The shared `:126` is a verified coincidence, not sibling arithmetic. |
| Ever Grande layout rows **8/9**, col **27**; cursor **(28,10)/(28,11)** | ✓ `region_map_layout.h` parsed; `MAPCURSOR_X_MIN/Y_MIN = 1/2` at `region_map.c:43-44`; `GetPositionOfCursorWithinMapSec` scans left-and-up so the lower cell is `posWithinMapSec = 1` |
| Fly-cell tap GBA (228,92) → touch | ✓ arithmetically (both (304,136) and (305,137) invert to (228,92) under `main.c:720-730`) — but see **O7**: the banked tool answers (304,136), not the report's (305,137) |
| `sForcedMovementTestFuncs[6..9]` / `[14]` | **indices ✓, lines ✗** — the currents are `:152-155`, not `:158`; see **O9** |
| `FieldDbg` offsets `+0xE0/+0xE4/+0xE8` | ✓ verified by expanding the struct; every pre-existing offset unchanged |

No revision error anywhere. The census's FireRed identifications are **rev1-only** in all 25 cases,
which is the user's cart; the Emerald ones all resolve in `pokeemerald.sym`.

---

## 6. The evidence lane's negatives, checked one at a time

Every claim below is a NEGATIVE ("carries no claim", "loses no evidence", "could not have caught
it"), which is the class this project gets wrong. Each was re-measured, not read.

| Negative | Verdict | How |
|---|---|---|
| "18 of the 20 groups are the companion screen and carry no claim" | **CONFIRMED** | My own classifier over all 685 PNGs reproduced the map group-for-group. The 13- and 5-file clusters are *entirely* `emerald/*.bottom.png`; the 18 subject `.top.png` counterparts are 18 distinct files. |
| "Emerald = gameA = TOP, FireRed = gameB = BOTTOM" (the fact the whole dissolution rests on) | **CONFIRMED** | Stated in both VISITED headers at the cited lines, and corroborated by the per-pixel variance: the varying region of a firered *bottom* is exactly the 240×160 game rect, and of an emerald *top* is the game rect plus the HUD strip. |
| "No catalog row loses its evidence" | **CONFIRMED** | Grepped every `.md`: nothing cites `I4c-slotspin` as evidence; the I4 row keeps `I4-slotmachine.*` with its `[exact]` cb2 plus `I4a/I4b/I4d`. |
| "`E13a-playerpc-menu` is mis-named but no row cites it" | **CONFIRMED** | Only `EVIDENCE-INTEGRITY.md` mentions the file; E13/E14 are already under "Honest not-visited". |
| "`COVERAGE.md` — nothing to correct" | **CONFIRMED** | It carries no census count; it is a source audit with file:line citations. |
| "29 of 170 captures have no state read at all" | **CONFIRMED** | 15 EM + 14 FR, and **both lists match member for member**. |
| "the cb2 read identifies a FAMILY, not a screen" | **CONFIRMED** (table) / **FALSE** (prose) | 12/47 and 6/76 reproduce exactly by address. The three "worst offender" counts beside them do not — **F2**. |
| "the harness could not have caught any of it" | **CONFIRMED** | `see.py shot()` read end to end: stateless grab, no expected state, no settle, no memory of the previous capture. `--with-state` is a `rec` feature only. |
| **"the cb2 read was a separate `gdbio` invocation hand-copied into `CAPTURES-*.log`"** | **CONFIRMED, and stronger than lane E claims** | The reads are **independently verifiable**: 49/49 distinct triples resolve exactly on the byte-matched maps, 25/25 FR on rev1 only. So they were real reads on the right ROM. What no artefact establishes is the **binding** — there is no banked raw gdb output for the census anywhere under `evidence/`, and nothing in the pipeline writes the id, the picture and the read together. Lane E's negative is right; its evidence for it was weaker than the evidence available. |
| "the five aux→census pairs are file copies, not re-shots" | **CONFIRMED** (as an inference) | The argument is sound given the measurement I reproduced: the HUD clock/FPS strip varies across every emerald top, so two independent grabs 18–25 s apart cannot be byte-identical. It remains an inference from the varying-bbox measurement rather than a direct observation, which is how lane E states it. |
| "61 distinct rows re-measure and hold" | **CONFIRMED** | Independent parse: 43 + 42 − 24 = 61. |
| "`censusguard bank` makes the banked file a product of the check" | **CONFIRMED in code, UNEXERCISED in practice** | 16 tests green including the live slot-pair regression. But no census pass has run through it — it is a gate for a pass that has not happened yet, and the stronger bracketing check (§5.3) was deliberately not implemented. |

---

## 7. The gate, run by me

`tools/closeout.sh`, verbatim:

```
== 1. the .cia is the real install target (CLAUDE.md rule 1) ==
  ok   .cia current (08-15 07:32)
== 2. no work stranded on a lane branch ==
  ok   every branch is merged into main
== 3. frozen files ==
  ok   fieldpath.{c,h} clean
== 4. no ROMs or saves tracked, ever ==
  ok   no ROM/save tracked or in history
== 5. no emulator left running, no fixtures staged ==
  ok   no Azahar running
  ok   no ROM fixtures left staged
== 6. evidence integrity ==
  ok   170 census-row subject frames (+90 non-claim: aux/ + withdrawn/), 10 duplicate group(s) — 0 naming two census rows, 10 non-claim only
  ok   evidence/impl: 1 duplicate pair (the known phase-23 one)

CLOSE-OUT CLEAN
```

Exit 0. And the 19 suites at `4272812`, 0 failures, every count as listed in §0. This is the first
phase in this family where **nothing is stranded on a branch** and the `.cia` is current at audit
time — the two standing items from phase 25's Disposition are, for once, both satisfied. The third
(instance cleanliness) is not: instance a still reads `traverse = 1`.

*(Hygiene note: my mutation runs rewrote `source/{progseq,fieldtrav,progtap}.c` in place and
restored them. `git status` is clean and all three suites re-verified at their base counts
afterwards, but their **mtimes now postdate the `.cia`**. That is my doing, not the lane's — at
audit start no source file was newer than the `.elf`.)*

---

## 8. The eight HMs after phase 29

Everything in the traversal layer needs **Settings → smart traverse ≥ 1** except the **X2 screen**,
which is new this phase and runs at the shipped default of **0**. Tap-to-Fly is a separate subsystem
and is not gated.

| HM | Implemented? | Ever executed? | Plain statement |
|---|---|---|---|
| **01 Cut** | **Yes** — object edge, level ≥ 1 | **LIVE-PROVEN** (phase 24 P2, emulator) | works end to end. Untouched by phase 29 |
| **02 Fly** | **Yes** — region-map driver (not the traversal layer) | **LIVE-PROVEN** (phase 24 lane B2; used again phase 28 to reach Fallarbor) | works end to end. Phase 29 re-derived the Ever Grande two-cell geometry from pret and pinned **which** cell: layout row 9 / cursor (28,11) is (27,49); row 8 / (28,10) is the League gate |
| **03 Surf** | **Yes** — metatile edge, level ≥ 1 | **LIVE-PROVEN** (P1; re-proven phase 25; mounted again phase 28) | works end to end. **Changed this phase**: the four CURRENT behaviours (0x50–0x53) are now refused by `transition()` in both modes, closing the hole SPEC §4.1 named at phase 26. That is a *surf-tier* route change, host-graded (1 mutation) and never run live |
| **04 Strength** | **Yes, as a TERMINAL only** — the tap activates the game's own prompt; the router never routes *through* a boulder and never pushes one | **NEVER EXECUTED** | Unchanged by phase 29 except one citation fix (`:145` → `:147`). **The safety negative — the boulder's `currentCoords` unchanged — has still never been measured**, third phase running. The live channel (`progLatch`, `progObjSlot/X/Y` at `+0xD0..+0xDC`) exists, is correctly offset, and has never been read on a boulder. Phase 28's corrections stand and are carried forward: **ten** sites, and the registered bike **is** the Mach Bike, so Jagged Pass is a harness problem (held input to reach `bikeFrameCounter == 2`) |
| **05 Flash** | **No — deliberately** | n/a | settled with citations; changes no tile's walkability. Nothing to prove |
| **06 Rock Smash** | **Yes** — object edge, level ≥ 1 | **LIVE-PROVEN** (phase 24 P3) | works end to end. Untouched |
| **07 Waterfall** | **Yes** — multi-tile edge with goal-retarget, level ≥ 1; **plus a default-level path screen (new)** | **PLANNER LIVE-PROVEN; EXECUTOR LIVE-DISPROVEN; THE FIX IS UNRUN** | The plan was proven live on Route 114 (phase 28) and re-derived from the cartridge by the phase-28 audit. The executor defect **X1** — FACE stepping the surfing player onto the fall — is now **fixed and mutation-graded against a transcription of the engine's own input path**, and the second half (the `MSGBOX_DEFAULT` after YES) is graded rather than assumed. **X2** — the frozen router plotting a 4-step swim UP the column — is now screened at `walk_plan`, the single caller of `fieldpath_plan`, so it is fixed for the **shipped default** as well as for `gate == 2`. Both fixes are host-only: **the ride has still never completed under program control**, `TP_DONE_BUDGET` at K=8 is still a prediction (~256 ride frames against 480), and the K=8 site has still never been visited. One stated invariant mirrors `facingDirection` where the engine tests `movementDirection` (O2) |
| **08 Dive** | **Planner only** — a map connection with identity coordinates; **zero callers** outside its module, no `FT_HM_DIVE` in `touch.c`/`progseq.c` | **NEVER EXECUTED** (there is nothing to execute) | Unchanged in behaviour. Its test got better: O7 replaced TEST 24's tautology with a real bookkeeping invariant and added a reachability-agreement property, and the tier-order guard now costs **8** failures rather than 7. The guard's own window index remains uncovered (an off-by-one is inert) |

**Score, without softening: six of eight are planned by touch** (Cut, Fly, Surf, Rock Smash,
Waterfall, and Strength as an activation-only terminal). **Four have ever run end to end** — Cut,
Fly, Surf, Rock Smash. **Waterfall's executor is now believed fixed and has never been run.**
**Strength has never run, and its safety negative has never been measured.** **Dive cannot run.**
**Flash is deliberately out.** All of it is on `main` and in `main`'s `.cia` — a first for this
family — and all of it except the X2 screen is off by default. **Nothing in phases 26, 28 or 29 has
ever run on real 3DS hardware.**

---

## 9. The census's real coverage, stated honestly

Of the catalogue's **143** paper rows:

* **61 distinct rows have a photograph** (43 Emerald + 42 FireRed, 24 in both) — re-measured
  independently this audit and unchanged by the withdrawal.
* **170 subject frames** are banked (74 Emerald tops + 96 FireRed bottoms), **all whole-file
  distinct**, plus **1 withdrawn** (`I4c-slotspin`), **183** `emerald/aux/` working captures, and one
  **companion** frame per pair photographing the *other, undriven* game — 170 more images that carry
  no claim at all. The census's unit is one 240×160 game frame on one screen, and that rect is
  measured, not assumed: 240×160 at ((W−240)//2, 40) on both screens.
* **141 of the 170** carry a `gMain.callback2` read taken at capture time; **29 do not** and rest on
  the picture alone.
* **18 of the 170** (12 Emerald + 6 FireRed) sit on a callback that **no sibling capture shares**.
  Everything else is a picture plus a callback *family* — `FR:CB2_Overworld` alone covers 30
  captures.
* The reads themselves are sound: **49 of 49** distinct `(engine, address, symbol)` triples resolve
  exactly against the byte-matched symbol maps, and **all 25 FireRed triples are rev1-only**, i.e.
  the user's actual cartridge revision. That is a stronger result than either lane claimed.
* **Zero** captures have a machine-verified binding between the picture and the read. `see.py`'s
  `shot()` is a stateless grab; no raw census gdb output is banked anywhere; the id in
  `CAPTURES-*.log` is the operator's label. So even the 18 are *"a real read of the right ROM, and
  an operator's word that it belongs to this picture"*.
* **5 census files are byte-copies of an `aux/` working file** promoted under a claim-bearing name.
  Their screens are correct; they are not independent corroboration of the aux shot, and must never
  be counted as a second observation.

**The honest sentence:** *61 of 143 catalogue rows have been photographed; 18 of the 170 photographs
carry a callback read that could not name any other screen; and no photograph anywhere in the census
is bound to its read by anything but a hand-written log line.* Everything downstream of that —
including every "identified with certainty" the touch plan inherits — should be read at that
strength. The plan's own detection fingerprints *are* callbacks, so the half the plan consumes is
the half that is independently solid; it is the *screen* identities, not the callback identities,
that rest on pictures.

---

## 10. What is owed

1. **Run W1 at Ever Grande (K=8).** Everything except the emulator is now derived and audited: the
   fall, the fly cell, the walk, the tap. Start with the transcript's own §3.3 step 1 (re-run the
   phase-28 binary with a client attached — three minutes, and it says whether the machine healed).
2. **Run W2 and read `progObjX`/`progObjY` before and after.** Third phase of asking.
3. **Fix O2** — read `ObjectEvent+0x18`'s **high** nibble too and require both, so the fix mirrors
   the field the engine actually tests.
4. **Close the three uncovered branches §3 found**: the screen's **L/R** step arithmetic, a plan
   whose **start tile is itself forced**, and the Dive guard's **window index** (which needs a
   fixture with a reachability boundary inside the 25×25 window).
5. **Correct F1 in the transcript** — run 4 shows Azahar's loader, not the app; and correct F2's
   three counts (30 / 7 / 6), including the "32" now sitting in `REPORT.md:13`.
6. **Give `touch.c` a host suite**, or at minimum host-cover the `faceEnterable` computation and the
   `walk_plan` screen call. Phase-24 audit O2 is now three phases old and it is the only place the
   two new fixes can silently fail.
7. **Clean instance a** — `settings.bin` offset 100 → 0. Third recurrence; it needs a session with
   write permission there.
8. **Fix O9's line numbers** (`:152-155`), O4's header (`8897071`), O6's denominator, and O7's tap
   (make the report quote what `fstate` returns).
9. **Bank the raw captures.** `azshots/` is empty in all ten run dirs. The lesson audit O3 wrote is
   still one `tee` away from being followed.

---

_Method note: this audit's three findings all came from re-running or re-opening the lanes' own
inputs — opening a banked PNG, counting a log's own rows, and reading one field's nibble in pret.
Two of the three are again about a NEGATIVE stated more strongly than it was measured (F1's control,
O2's invariant). But the direction of travel is real and worth saying: **every single mutation
number in both lane reports reproduced to the digit, including one I re-derived with a mutation the
lane never wrote.** The arithmetic in this phase is trustworthy. What is still missing is not rigour
— it is a running emulator._
