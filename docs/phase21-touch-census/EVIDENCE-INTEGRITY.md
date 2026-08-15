# EVIDENCE INTEGRITY — the census duplicate map, its mechanism, and the corrected counts

_Phase 29, lane **E**. Desk work only; no emulator was booted (lane F owns instance b).
Everything below was decided by hashing, cropping and opening the banked files, re-parsing
`CAPTURES-emerald.log` / `CAPTURES-firered.log`, and reading the harness source. Written
2026-08-15._

**Headline, stated the way the house wants a negative stated: the alarm that opened this
lane is FALSE, and there is exactly one real defect underneath it.** The close-out gate's
"683 files, 649 unique — 54 files sharing 20 hashes" is correct arithmetic on the wrong
pixels. A census capture photographs **both** 3DS screens but only **one** of them is the
subject; the other photographs a *second, undriven game*, and 18 of the 20 groups are that
companion screen repeating exactly as it should. Of the 170 census-row **subject** frames,
**169 are distinct**. The one that is not is `firered/I4c-slotspin` — a byte-identical
re-shot of `firered/I4-slotmachine` labelled "mid-spin", withdrawn here.

No catalog row loses its evidence. The **61 distinct visited rows** re-measure and hold.
What does not hold is the phrase *"identified with certainty"* applied to a screen: the cb2
read is certain to the **callback family**, and a family covers up to 32 rows.

---

## 0. What I actually ran

| # | Check | Result |
|---|---|---|
| 1 | sha256 of all 683 PNGs under `evidence/` | 649 distinct; 20 groups / 54 files — the gate's numbers reproduce exactly |
| 2 | Every duplicate group classified **subject vs companion** by which screen the game under census was on | 18 groups companion-only or aux-only; **2 subject** (§1) |
| 3 | Opened the two shared images and 12 of the 54 members' subject frames | §1 — the shared images are named, and every subject frame shows the screen its row claims |
| 4 | Per-pixel variation across 40 emerald tops and 40 firered bottoms, to find the GBA frame without assuming it | subject rect = **240×160 at ((W−240)//2, 40)** on both screens — measured, then encoded in `censusguard.py` |
| 5 | Content-only re-hash: the GBA frame alone, HUD excluded, over all 260 subject captures | 249 distinct frames, 10 duplicate groups — a **different and better** answer than the whole-file hash (§2.3) |
| 6 | `CAPTURES-*.log` parsed (185 + 175 entries) and cross-joined to the duplicate groups | the mechanism, proven by the game's own state (§2) |
| 7 | `tools/emutest/see.py` `shot()` / `native.py` read end to end | no state binding, no novelty check, no settle — by construction it cannot detect a stale frame (§2.4) |
| 8 | Every banked census capture matched against a cb2 line in the logs | 29 of 170 have **no** state read at all (§3.2) |
| 9 | Every logged cb2 grouped by how many census captures share it | EM: 12 of 59 unique; FR: 6 of 82 (§3.1) |
| 10 | `VISITED-*.md` re-parsed row by row and the catalog union recomputed | **61 distinct rows** (43 EM + 42 FR, 24 in both) — §4 |
| 11 | 19 host suites rebuilt and run from the tree root | 0 failures, 1 295 539 checks (§6) |
| 12 | `tools/closeout.sh` | §2–§4, §6 clean; §1 and §5 fail on a stale `.cia` and a live Azahar that both belong to a concurrent lane (§6) |

---

## 1. The duplicate map — all 20 groups, and what the shared image IS

A census capture is a **pair**: `<id>.top.png` and `<id>.bottom.png`. Only one is the
subject, because the app runs two games at once:

* **Emerald pass** — Emerald is gameA, seat 1, **TOP** screen (`VISITED-emerald.md:4`).
  The bottom file is the touch-gamepad skin with **FireRed** behind it, undriven.
* **FireRed pass** — FireRed is gameB, seat 2, **BOTTOM** screen (`VISITED-firered.md:4-5`).
  The top file is **Emerald**, undriven.

That single fact dissolves the alarm. Below, **S** = subject (a census claim rests on it),
**C** = companion (photographs the other, undriven game — carries no claim), **aux** = an
`emerald/aux/tmp-*` working capture (a scratch pad, not evidence).

### The two big clusters — both companion-only

| Hash | n | Class | What the shared image IS |
|---|---|---|---|
| `7399b694…` | **13** | **C** | The touch-gamepad skin over a **completely black game-B viewport**. All 13 are `emerald/*.bottom.png`. |
| `b7c1015f…` | **5** | **C** | The touch-gamepad skin over FireRed's **©2004 Pokémon / Nintendo / Creatures / GAME FREAK copyright screen**. All 5 are `emerald/*.bottom.png`. |

Both are proven by the census's own state log, not by inference:

* Every member of the 5-file cluster has `screen1 = FR:CB2_InitCopyrightScreenAfterTitleScreen`
  at the moment of capture — and there are **exactly five** such reads in the whole
  185-entry Emerald log. Five reads, five files, one hash. The copyright screen is static,
  so every capture that lands on it is byte-identical.
* The 13-file cluster splits across `FR:CB2_TitleScreenRun` and `FR:CB2_Intro`, the two
  callbacks that own FireRed's attract loop. Both contain long fade-to-black stretches; a
  black frame under a static skin is byte-identical whichever produced it. Across the whole
  Emerald pass game B read `CB2_TitleScreenRun` **126** times, `CB2_Intro` **53**,
  `CB2_InitCopyrightScreenAfterTitleScreen` **5**, `CB2_SetUpIntro` **1** — it was never
  driven at all.

**The four screens the brief named are four different pictures.** `C3-battlemove.top`,
`D1b-frontiermap.top`, `E2b-partysub.top`, `G3a-pokenav-condition-menu.top` hash to four
distinct values; I opened all four and each shows exactly its row's screen (a Tyranitar
battle move grid; the BATTLE FRONTIER pass map with BATTLE TOWER selected; the
SUMMARY/SWITCH/ITEM/CANCEL popup over the party; the POKéMON NAVIGATOR CONDITION menu with
PARTY/SEARCH/CANCEL). Same for the five-file cluster: `E3-bag-berries.top` is the BERRIES
pocket, `F1-dexlist.top` is the Pokédex list at №001 TREECKO, `C22-forgetmove.top` is the
five-move BATTLE MOVES picker with FLY appended.

### The remaining 18 groups

| Hash | n | Class | Members | Reading |
|---|---|---|---|---|
| `48a9518d…` | 2 | **S** | `firered/I4-slotmachine.bottom` · `firered/I4c-slotspin.bottom` | **THE DEFECT.** §1.1 |
| `579bffca…` | 2 | **S** | `impl/FR-dlg-P2-tapped-through-to-overworld-startmenu.bottom` · `impl/FR-dlg-P4-hold-B-exited-dex.bottom` | The FireRed Pokémon Center with the START menu open (POKéDEX/POKéMON/BAG/SHOCK/SAVE/OPTION/EXIT). Pre-existing, phase 23 (`62b3976`); found and reported by lane C1 in `a388d48`. §1.2 |
| `3bee4618…` + `4c5f6413…` | 2+2 | S+C | `emerald/B3-scriptdialog` · `aux/tmp-menu2` | **File copy**, aux→census (§1.3) |
| `349ba410…` + `a9f2464e…` | 2+2 | S+C | `emerald/B5b-saving` · `aux/tmp-saved` | file copy |
| `cc7fb232…` + `2ce43888…` | 2+2 | S+C | `emerald/E12f-storage-boxjump` · `aux/tmp-exitbox7` | file copy |
| `69b0f1cc…` + `1887faca…` | 2+2 | S+C | `emerald/E2d-party-hmchoose` · `aux/tmp-fly7` | file copy |
| `2cbe5392…` + `f771ab98…` | 2+2 | S+C | `emerald/G4b-matchcall-incoming` · `aux/tmp-blocker2` | file copy |
| `09d9e265…` | 2 | aux | `aux/tmp-house2.top` · `aux/tmp-house3.top` | Two aux shots, **identical top, different bottom** — time passed and game A did not change: a nav step that did not land. Working captures; no row rests on them |
| `0e591f3f…` | 2 | C | `E1-summary-contestmoves.bottom` · `F4-dexcry.bottom` | companion |
| `29106b4c…` | 2 | C | `E1-summary-skills.bottom` · `aux/tmp-exitbox4.bottom` | companion |
| `613ed578…` | 2 | C | `H7-pokeblockcase.bottom` · `I4-slotmachine.bottom` | companion |
| `e5722af3…` | 2 | C | `aux/tmp-fly-nav3.bottom` · `aux/tmp-moves-geng.bottom` | companion |
| `46b9dc95…` | 2 | C | `aux/tmp-place2.bottom` · `aux/tmp-state.bottom` | companion |

### 1.1 The one real defect — `firered/I4c-slotspin`

`CAPTURES-firered.log:436` banks `I4-slotmachine` at **22:24:34Z**; `:439` banks
`I4c-slotspin` at **22:25:06Z**, annotated *"mid-spin"*. The two GBA frames are
**byte-identical**: the reels sit in the same position, CREDIT 8399 / PAYOUT 8008 unchanged.
Nothing in the picture distinguishes a spinning machine from an idle one, because the
machine was idle.

Why the census's own check could not catch it: **both captures read the same cb2**,
`0x0813F9C4 FR:CB2_RunSlotMachine [exact]`. The state read that protected every other row
had no power here, because the two screens live inside one callback.

**Action taken.** Both files moved to `evidence/firered/withdrawn/` with a README stating
why — kept, never deleted. `VISITED-firered.md`'s I4 row is corrected. **Catalog row I4
keeps its evidence**: `I4-slotmachine` is a real, distinct capture with an exact cb2, and
`I4a`/`I4b`/`I4d` are untouched. What is withdrawn is the *second observation* — the claim
that a spinning machine was ever photographed.

### 1.2 The `impl/` pair — settled, not re-litigated

Lane C1 found it, opened it, and reasoned correctly (`LANE-C-HARVEST.md:325-338` (hashes at :329-330)): the frame
IS P2's claim, and is *also* what P4's end state looks like, so a byte comparison cannot
separate them — "which is precisely why one file must not carry two claims". **P4's actual
evidence is unaffected**: it rests on the gdb deltas (`dlgHolds` 0→1, `ctx` 10→6, `dlgTaps`
unchanged), not on the picture. Settlement: P4's *picture* proves nothing P2's does not;
P4's *row* stands on its state delta. The gate now reports this pair explicitly as the one
accepted `impl/` duplicate instead of burying it in a list of twenty.

### 1.3 The five aux→census file copies

`B3-scriptdialog`, `B5b-saving`, `E12f-storage-boxjump`, `E2d-party-hmchoose` and
`G4b-matchcall-incoming` are each **byte-identical to an `aux/tmp-*` file shot 18–25 s
earlier**, HUD clock and FPS counter included. A second `see shot` cannot reproduce the
clock, so these are **copies**, not re-shots: the operator explored into `aux/tmp-<name>`,
saw the screen they wanted, and promoted the file under its census id.

This is not a false claim — I opened all five subject frames and each shows its row's screen
(`E2d` shows the party with ABLE!/NOT ABLE! labels and *"SALAMENCE wants to learn the move
FLY."*; `G4b` shows the Match Call ring-in box over the overworld). It is a **provenance**
point: the aux file is the original observation and the census file is the same observation
under a claim-bearing name, so the two must never be counted as independent corroboration.
(Two further pairs — `E3-bag-tms`/`aux/tmp-tmpocket` 43 min apart, `I4-slotmachine`/
`aux/tmp-slotquit` 6 min apart — share a *frame* but not a *file*: genuine separate shots of
a screen that does not animate at rest. Those are fine.)

---

## 2. The mechanism

### 2.1 The companion screen is a second game, not a stale frame

There is no transition-timing bug behind the 18 companion groups. The census ran **two
games at once** and drove **one**. The undriven one free-ran its boot attract loop for the
whole session, so its screen is repeatedly the copyright screen or a fade-black frame, and
the touch-gamepad skin drawn over it is static (per-pixel variation over 40 firered bottoms
shows **zero** variation outside the 240×160 game rect). Identical companion files are the
*expected* output of that arrangement.

### 2.2 The real mechanism, in the census's own log: nav steps that did not land

The census log shows the operator hitting exactly the failure the brief suspected, repeatedly
— and, where the cb2 read had discriminating power, **catching it**:

```
== B6-trainercard-front  14:55:14Z   screen0: EM:CB2_FrontierPass      <- not there yet
== B6-trainercard-front  14:57:35Z   screen0: EM:CB2_FrontierPass      <- still not there
== B6-trainercard-front  14:59:41Z   screen0: EM:CB2_TrainerCard  [ok] <- landed
== E3-bag-items          14:34:12Z   screen0: EM:CB2_Overworld         <- mid-fade
== E3-bag-items          14:34:42Z   screen0: EM:CB2_BagMenuRun   [ok] <- "(post-fade)"
== B8-flymap             15:36:14Z / 15:36:41Z / 15:40:35Z             <- three misses
== B8-flymap             15:43:23Z   screen0: EM:CB2_FlyMap      [ok]
```

Each retry **overwrites the same filename**, so only the final capture survives — which is
why every banked file corresponds to the *last* log entry for its id, and why `A2-title`
(missed at 14:25:52 on `CB2_Overworld`, landed at 17:21:34 on the second boot) banks the
real title screen while `VISITED-emerald.md:15` still honestly records the miss.

So the protection was real. It was also **entirely manual**, and it had two blind spots:

1. **The check has no power inside a callback family.** `I4-slotmachine` and
   `I4c-slotspin` both read `CB2_RunSlotMachine`. The operator saw a matching cb2 and banked.
2. **The check sometimes did not run.** 29 of 170 banked census captures have no cb2 line
   at all (§3.2), and at least two rows' cb2 was logged under a *different* id —
   `E7-psa-anim`'s `CB2_PSA 0x0811C774` was read under `tmp-taught` (19:47:22Z), and
   `A5-options`'s `CB2_InitOptionMenu 0x08088370` was read under `B6-trainercard-front`
   (19:51:22Z, before that id was re-shot). The reads are real and `[exact]`; the *binding*
   to the picture is narrative, not recorded.

### 2.3 Why a whole-file hash is the wrong instrument, in both directions

The Emerald top screen carries the app's HUD strip — title, `3D`, an FPS counter and a
clock — which **changes every capture**. Measured: the varying bbox across 40 emerald tops
is `(80,8)-(375,200)`; restricted below the HUD it is exactly `(80,40)-(320,200)` = the
240×160 GBA frame. So:

* **too loud** — 18 of 20 groups are companion screens carrying no claim;
* **too quiet** — two shots of one *unchanged* game screen hash differently because the clock
  ticked, and slip straight through.

Re-hashing the **subject GBA frame only** gives the true picture: 260 subject captures →
249 distinct frames, 10 groups, of which exactly **one names two census rows** (the slot
pair). Two of the ten (`E3-bag-tms`/`tmp-tmpocket`, `I4-slotmachine`/`tmp-slotquit`) are
invisible to a whole-file hash and only appear under this instrument.

### 2.4 The harness could not have caught any of it

`see.py`'s `shot()` (`tools/emutest/see.py:310`) is a single stateless grab: find window →
`screencapture` → crop → save. Its only content check is `_uniform()`, a tripwire for a
missing Screen Recording grant. It has **no memory of the previous capture**, no expected
state, and no settle. `native.py` is a pure nearest-neighbour reconstruction with no state
either. The cb2 read was a **separate `gdbio` invocation** whose output was hand-copied into
`CAPTURES-*.log` — nothing in the pipeline binds the picture, the state read and the
filename together. The `id` field in the log is the operator's label, not a property of the
capture.

*(This is the same shape as phase-28 audit **O3**: the state reads exist only as hand-typed
lines. The lesson repeats because the tooling never changed.)*

---

## 3. Blast radius — identification vs picture, reported separately

### 3.1 The cb2 read identifies a FAMILY, not a screen

This is the finding with the widest reach, and it is measured, not argued. Grouping every
banked census capture by the cb2 read at its capture time:

| | Emerald | FireRed |
|---|---|---|
| banked census captures | 74 | 96 |
| …with a cb2 read at capture time | 59 | 82 |
| …whose cb2 is **unique** to that capture | **12** | **6** |
| …whose cb2 is **shared** with ≥1 sibling capture | 47 | 76 |

The worst offenders: `FR:CB2_Overworld` covers **32** FireRed captures (every task-over-map
dialog, shop, Safari and PC screen); `EM:CB2_Overworld` covers 10; `EM:CB2_Pokedex` covers
all 7 dex captures; `EM:MainCB2` covers the four summary pages *and* the forget-move picker;
`FR:CB2_RunSlotMachine` covers the 2 that became this lane's defect.

**Consequence, stated plainly:** *"visited live and identified with certainty"* is true of
the **callback**. It is not true of the **screen**. Inside a family, which screen you are
looking at is established by the picture and nothing else. Nothing in the touch plan breaks
because of this — the plan's detection fingerprints ARE callbacks, so the independent half
is precisely the half the plan consumes — but the sentence must be corrected wherever it
appears, and it is (§4).

### 3.2 Captures with no state read at all (29 of 170)

Evidence for these rows is the picture alone.

* **Emerald (15)** — `B1-overworld-indoor`, `B3-scriptdialog`, `B5b-saving`,
  `E12e-storage-partystrip`, `E12f-storage-boxjump`, `E1b-moves-{dragonite,gengar,metagross,
  milotic,salamence}`, `E2d-party-hmchoose`, `G3a-pokenav-condition-menu`,
  `G4b-matchcall-incoming`, `H10-tvnews`, `H9b-usepokeblock-confirm`.
* **FireRed (14)** — `A5-options`, `B1e-route22`, `C22a-replace-prompt`, `C3-battlemove`,
  `E12c-holding-menu`, `E12e-partystrip`, `E1c-moves-{2,3,4,5,6}`, `E7-psa-anim`,
  `I4d-slotquit`, `K4-questlog`.

I opened the load-bearing ones — `E2d-party-hmchoose`, `G3a-pokenav-condition-menu`,
`G4b-matchcall-incoming`, `H10-tvnews`, `A5-options` — and every one shows exactly the screen
its row names, unmistakably. **None of these rows is withdrawn.** But note *what* is missing:
for a row whose cb2 is `CB2_Overworld` the state read carried the "Detected" column too
(`ctx=…`), so for these 29 the *detected-context* value is equally unbanked. If a future
phase re-grades a detection claim from one of these rows, it must re-read the state, not
inherit it.

### 3.3 Rows whose evidence is WITHDRAWN

| Row / capture | Withdrawn | Does the catalog row survive? |
|---|---|---|
| `firered/I4c-slotspin` — the "mid-spin" observation | **Yes** — duplicate subject frame, same cb2, nothing distinguishes it | **Yes.** Row I4 keeps `I4-slotmachine` (distinct frame + exact cb2) and `I4a`/`I4b`/`I4d` |
| `impl/FR-dlg-P4-hold-B-exited-dex.bottom.png` **as a picture** | Already reported by lane C1; settled here | **Yes.** P4's claim rests on its gdb deltas, not the frame |

**That is the whole list. One census capture and one impl picture.** No catalog row, and no
TOUCH-PLAN class, loses its basis.

### 3.4 One mis-named artefact (no claim attached)

`emerald/E13a-playerpc-menu.{top,bottom}.png` is banked, but the picture shows the overworld
with the flavour line *"GUYA flipped open the notebook.♥"* — the moment **before** the
ITEM STORAGE / MAILBOX / DECORATION / TURN OFF menu draws. The filename names a screen the
picture does not show. **The census is already honest about this**: `VISITED-emerald.md:105`
lists E13/E14 under *"Honest not-visited"* ("the sub-screens weren't entered"), and no row
cites the file. Left in place, named here so nobody promotes it later on the strength of its
name.

---

## 4. The corrected counts

| Claim | Where | Verdict |
|---|---|---|
| **143 catalog rows** | `CATALOG.md:304`, `TOUCH-PLAN.md:3` | **Stands.** A paper count from the decomps, untouched by this investigation |
| **61 distinct catalog rows visited** | `REPORT.md`, `TOUCH-PLAN.md:4,477` | **Stands, and now reconciles.** I re-parsed both VISITED tables: **43 Emerald + 42 FireRed, 24 in both → 43 + 42 − 24 = 61**. Unchanged by the withdrawal (row I4 keeps other evidence) |
| **"41 Emerald + 41 FireRed, 21 FR-only"** | `REPORT.md` | **Corrected.** It does not reconcile with itself (41 + 21 = 62 ≠ 61) and does not reproduce; the measured split is 43 / 42 / 24 |
| **"82 visited"** | *nowhere in the docs* | **Never write it.** 82 = 41 + 41, a per-game sum **with the 24-row overlap double-counted**. On my numbers the same sum is 85. It is not a count of screens |
| **"identified with certainty"** | `CATALOG.md:17`, `TOUCH-PLAN.md:4`, `REPORT.md` | **Qualified** (§3.1): certain to the **callback family**; within a family the screen rests on the picture. Corrected in all three |
| **"~165 screenshot pairs banked"** | `REPORT.md` | **Corrected to what is countable:** **170 census-row subject frames** (74 Emerald tops + 96 FireRed bottoms), all distinct, **+1 withdrawn**; plus 183 `emerald/aux/` working captures and one companion screen per pair |
| `COVERAGE.md` | — | **Nothing to correct.** It is a source-audit of shipped touch behaviour with file:line citations and carries no census count. Checked, not assumed |

**The honest smaller number, if one is wanted:** of the 170 banked census captures, **141**
carry a live state read taken at capture time and **29 do not**; and of the 141, only **18**
sit on a callback that no sibling capture shares. So *"a screen photographed AND
independently identified by a state read that could not have named any other screen"* is
**18 captures**. Every other row is a picture plus a family. That is a much smaller number
than 61 and it is the one to quote when a row's identity — rather than its callback — is
what a decision depends on.

---

## 5. The fix that prevents recurrence

### 5.1 The assertion

Two captures of the same screen family must not carry the same GBA frame — and the check
must run at bank time, not at audit time.

**Implemented: `tools/emutest/censusguard.py`** (new, host-tested, no emulator required).

* `subject_rect(size)` — the 240×160 GBA frame at `((W−240)//2, (H−160)//2)`, **measured**
  from 40 real captures per screen (§0 row 4), not assumed; refuses any non-native size so a
  2.25× `see shot` crop or a 720×480 `impl` capture can never be hashed against the wrong
  rect.
* `frame_digest(path)` — sha256 of the subject frame. HUD-clock independent, which is what
  makes it strictly stronger than the whole-file hash in both directions (§2.3).
* `is_subject(path)` — encodes the seat map: Emerald = top, FireRed = bottom. A companion
  is never graded.
* **`censusguard bank SHOT.png OUT.png --screen top|bottom --against <evidence dir>`** — the
  verb the next census pass should use. It reconstructs to native resolution (reusing
  `native.py`'s proven algorithm rather than a second copy of it), asserts the frame is
  novel and not blank, and **only then writes**. A refused capture leaves *nothing* on disk.
  This is the structural fix: the banked file becomes a **product of the check**, so the
  check cannot be skipped the way a manual eyeball can.
* `censusguard audit DIR…` — the whole duplicate map, separating groups that name **two
  census rows** (a defect, exit 1) from groups confined to `aux/` and `withdrawn/` (normal:
  a retried nav step legitimately re-shoots an unchanged screen).

`test_censusguard.py` — **16 checks, green**, including four that run against the real banked
evidence: the slot pair is kept as a live regression fixture, the four "unrelated" screens
are asserted to be four distinct subject frames and one shared companion, no subject frame
is blank, and **no two census rows share a subject frame**.

### 5.2 The gate

`tools/closeout.sh` §6 rewritten: it now runs `censusguard audit` over
`evidence/{emerald,firered}` and **fails** only when a group names two census rows, while
still whole-file-hashing `evidence/impl/` (single-screen zoomed captures, where the
whole-file hash IS the right instrument) and reporting the known phase-23 pair explicitly
rather than burying it among twenty.

### 5.3 What I did NOT implement, and why

The stronger assertion — *the capture must be bracketed by two cb2 reads that agree with
each other and with the expected screen* — needs a live gdb session through `gdbio.py`'s
broker. **This lane was desk-only and could not exercise it, so I did not ship it.** The
specification, for whoever runs the next pass:

> Before the shot, read `gMain.callback2` (EM `0x030022C4` / FR `0x030030F4`) for the
> subject seat. Take the shot. Read it again. Require: (a) both reads equal — otherwise the
> screen was mid-transition; (b) the value equals the `--expect` symbol; (c) the resulting
> frame passes `censusguard`'s novelty test. Emit the `CAPTURES-*.log` line **from the same
> command**, so the id, the picture and the state can never again be bound by narrative
> alone. Call sites: `gdbio.broker_session()` + `cmd_read_u32`; the natural home is a
> `--expect-cb2` flag on `censusguard bank`.

Requirement (a) is the one that would have caught the `B6-trainercard-front` and
`E3-bag-items` misses automatically instead of by eye; requirement (c) is the one that
catches `I4c-slotspin`, and it is shipped today.

---

## 6. Gate

**19 host suites, 0 failures, 1 295 539 checks**, all rebuilt and run from the tree root:
celiolink 1259 · control 6940 · diag 376 · excseq **689** · **fieldpath 1808 (FROZEN,
untouched)** · fieldtrav 4199 · netlink 66 · peersprite 62078 · presence 61376 ·
profiles 2746 · progseq 924 · progtap 5402 · theme 83444 · tilt 1756 · touchgeom 1040833 ·
trace_replay 58 (+4 loud SKIPs) · typography 1419 · uigeom 18332 · uihit 1834.

> **Read that number honestly.** This lane changed **no C source at all** — its diff is
> `tools/` and `docs/`. At the time of the run the shared working tree carried **another
> lane's uncommitted edits** to `source/{touch,progseq,progtap,fieldtrav}.{c,h}` and
> `test/host/test_progseq.c`, which is why `progtap` reads 5402 (phase 28: 1405),
> `fieldtrav` 4199 (3652) and `progseq` 924 (902). Those counts belong to lane F, not here.
> `excseq 689` matches the phase-28 audit's measured value, and `fieldpath` is byte-identical
> to `main`.

`tools/closeout.sh`: §2 branches clean · §3 `fieldpath` clean · §4 no ROM/save tracked ever ·
§5 no staged ROM fixtures · §6 **clean** (170 census-row subject frames, 0 groups naming two
rows; `impl/` 1 duplicate = the known phase-23 pair).

**Two checks fail, and I deliberately fixed neither. Both belong to a concurrent lane.**

* **§1 — the `.cia` (06:21) is older than the `.elf` (06:45).** The concurrent lane edited
  `source/` at 06:36–06:44 and rebuilt. Running `--fix` here would bake **another lane's
  uncommitted work-in-progress** into the install target and hand the user a binary nobody
  has tested. It is stale, it is not mine to rebuild, and it must be rebuilt by whoever
  merges that source work.
* **§5 — an Azahar is running.** It is `tools/emutest/az-b/…azahar -g 24690` = **instance
  b**, which this lane's brief assigns to lane **F**. Killing it would destroy another
  lane's live session.

*Standing hazard, recorded because it bit this lane: the lanes share one working tree, and a
concurrent `git add -A` swept two of this lane's files — `TOUCH-PLAN.md` and the
`I4c-slotspin` withdrawal renames — into commit `2aeb7e5` ("fix(phase27): re-grade C20"),
whose message does not mention them. Nothing was lost, but the attribution is wrong, and a
close-out gate cannot distinguish "my tree" from "our tree". Parallel lanes on one checkout
want either a worktree each or a staged-paths-only commit discipline.*

---

## 7. What this lane changes about how the project reads its own evidence

Phase 25 named the rule: **a negative claim is a claim**. This lane adds its mirror, because
the failure here was the opposite shape — a *positive* number that nobody had defined:

> **A count is a claim, and a count needs a denominator.** "683 files" and "649 unique" were
> both true and together they were misleading, because neither said *which pixels carry a
> claim*. The census's real unit is not a file and not a pair — it is **one 240×160 game
> frame on one screen**. Every number about the evidence should be stated in that unit, and
> the tooling now is.

The corollary for the identification half is the same discipline applied to a word:
**"certain" needs a scope.** `CB2_Pokedex` read live and resolved `[exact]` is certain — of
the callback. Six different screens live under it. Say which one the certainty covers.
