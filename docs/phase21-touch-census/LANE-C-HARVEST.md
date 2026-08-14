# LANE C1 (phase 25) — the cb2 harvest that unblocks nine BROKEN `TAP` rows

_Session 2026-08-14. Main tree, Azahar **instance a** (state/, runs/, gdb port 24689). Instance b
belongs to another lane and is never touched._

**The brief.** Lane B1 verified all 48 class-`TAP` rows and found 10 BROKEN — nine of them for ONE
reason that is not a code bug: `TAP` was scored as "the generic safe default is enough", but the
default only runs on a screen the classifier can NAME, and these screens have no cb2 fingerprint
harvested. One harvest unblocks nine rows. Rows: **A10 · C20 · E8 · E15(EM) · E19 · H4/H5 · H6 ·
L1(EM) · L2**, plus **K4** (the tenth, a different cause: the FR quest-log guard).

---

## Entry 0 — the desk pass: candidates, and TWO of the ten re-diagnosed before a single boot

Method is `CB2-HARVEST.md` verbatim: the value that matters is the one sitting in
`gMain.callback2` **while the screen is up**, read live off the app's own gs-logger statics
(`s_lastCb2[screen]`, `s_gsLog` ring) over gdb and resolved against the pret `symbols`-branch maps.
Symbol maps re-fetched this session to the session scratchpad (`pokeemerald.sym` 73 204 lines,
`pokefirered_rev1.sym` 50 807, plus the LG/RS rev files) — the same source every earlier promotion
cited. `[exact]` keeps its meaning: **the live value IS the symbol's address**.

### The two re-diagnoses (both from the engine source, before any emulator time)

**1. E19 is NOT an undetected cb2 — it is a `lockall` field script, so lane B1's fix already
covers it.** B1 graded it "the Frontier record screens are their own cb2s, none harvested". The
catalog said otherwise ("`battle_records.c` — window over overworld") and the engine agrees:

```
EventScript_CableBoxResults::            (pokeemerald data/scripts/cable_club.inc:646-653)
	lockall
	setvar VAR_0x8004, 0
	special ShowLinkBattleRecords          <- src/battle_records.c:315: AddWindow + text, NO SetMainCallback2
	waitbuttonpress
	special RemoveRecordsWindow
	releaseall
```

`lockall` is `LockPlayerFieldControls()` ⇒ `sLockFieldControls = 1` for the whole window, which is
**exactly** the signal lane B1 shipped (`GameProfile.fieldLock`). So E19 needs no fingerprint at
all; it needs a **visit** to prove `fieldLock == 1` on it. Cheap, and it turns a BROKEN row into a
verified one without a line of code.

**2. L2 has a mechanism the plan only guessed at, and it makes the row's "taps dead" mandatory.**
`pokeemerald src/credits.c:344-357`:

```c
static void CB2_Credits(void)
{
    RunTasks(); AnimateSprites();
    if ((JOY_HELD(B_BUTTON)) && gHasHallOfFameRecords
     && gTasks[sSavedTaskId].func == Task_CreditsMain)
    { /* Speed up credits */ VBlankCB_Credits(); RunTasks(); AnimateSprites(); sUsedSpeedUp = TRUE; }
```

**B HELD is the credits fast-forward** — and the FAM-DLG default maps a hold to a level-triggered
B. So promoting the credits into `cb2FullUi` like every other screen would replace one wrong
behaviour (walk-key leak) with another (a finger resting on the screen double-speeds the credits
and latches `sUsedSpeedUp`). The row's target — "tap = nothing (don't skip by accident)" — is
therefore not a preference, it is what the engine's own input handling demands. That needs a class
that is **detected but silent**, which this project does not have yet. See Entry 2.

### Candidate table (sym-derived; every one gets live-confirmed or is reported as UNREACHABLE)

Run loops are what `gMain.callback2` holds for most of a screen's life; the init/loader callbacks
run for a handful of frames each and leak just as badly, so both are candidates.

| Row | Screen | game | candidate cb2 (symbol) | note |
|---|---|---|---|---|
| C20 | Evolution | EM | `CB2_EvolutionSceneUpdate` **0x0813E3A4**, `CB2_TradeEvolutionSceneUpdate` 0x0813E3C0, loaders 0x0813DD7C / 0x0813DF70 | run loop set at evolution_scene.c:308/372 (trade: :462/:527) |
| C20 | Evolution | FR | `CB2_EvolutionSceneUpdate` **0x080CE724**, trade 0x080CE740, loader 0x080CE0FC | |
| E8 | Mail read | EM | `CB2_MailRead` **0x08121C64**, `CB2_InitMailRead` 0x081219F0 | held-mail entry `CB2_ReadHeldMail` 0x081B4A98 |
| E8 | Mail read | FR | `CB2_RunShowMailCB` **0x080BF37C**, `CB2_InitMailView` 0x080BF124 | mailbox `CB2_ReturnToMailbox` 0x080EC274 |
| E15+L1 | HoF PC replay **and** the HoF scene | EM | `CB2_HallOfFame` **0x08173560** | ONE value covers both: `CB2_DoHallOfFameScreen` (:417) and `CB2_DoHallOfFamePC` (:797) both end at `SetMainCallback2(CB2_HallOfFame)` (:401, :852). Init loops 0x08173694 / 0x08174194 |
| L1 | HoF scene | FR | `CB2_HofIdle` 0x080F1E38 — **already shipped** | B1 marked FR's half covered |
| L2 | Credits | EM | `CB2_Credits` **0x081754DC** | + `CB2_StartCreditsSequence` 0x08175620 |
| L2 | Credits | FR | `CB2_Credits` **0x080F3A60** | |
| H4 | Contest results | EM | `CB2_ShowContestResults` **0x080F5C00**, setup 0x080F5B00 | |
| H5 | Contest painting | EM | `CB2_HoldContestPainting` **0x0812FDF8**, 0x0812FDEC / 0x0812FE0C | |
| H6 | Berry Blender | EM | `CB2_PlayBlender` **0x08081898**, 0x0807FAC8 / 0x080808D4 / 0x08081FC8 | |
| A10 | Link error | EM | `CB2_PrintErrorMessage` **0x0800B1A0**, `CB2_LinkError` 0x0800AF30 | |
| A10 | Link error | FR | `CB2_PrintErrorMessage` **0x0800AF40**, `CB2_LinkError` 0x0800ACE8 | |
| E19 | Battle records | EM | **none needed** — `lockall` script (above) | |
| K4 | FR quest-log playback | FR/LG | `gQuestLogState` **0x0203ADFA** (u8) | the game's own test is `QL_IS_PLAYBACK_STATE` = `state == 2 (PLAYBACK) \|\| state == 3 (PLAYBACK_LAST)` (constants/quest_log.h). **rev-insensitive**: 0x0203ADFA in `pokefirered.sym` AND `pokefirered_rev1.sym` AND both LeafGreen maps — checked, all four identical |

LG (BPGE) inherits the FR values at its own addresses (`CB2_Credits` 0x080F3A38, evolution
0x080CE6F8/0x080CE714, mail 0x080BF350/0x080BF0F8, HoF idle 0x080F1E10, error 0x0800AF40) — but
LG's whole `cb2Title`/`cb2FullUi` pair is still empty (its census is its own slice), so this lane
does not open that file: a partial LG list would claim a census that never happened.

### Reachability, from the user's own saves (`roms/emerald-fix.sav` = the by-touch fixture)

- **E19** — the record box lives in a Cable Club; the save stands next to a PC in a Pokémon
  Center. One flight + one staircase. **PLANNED.**
- **E15 / L1(EM)** — the PC's `HALL OF FAME` row exists on a post-league save (this one has 8
  badges + Frontier access). **PLANNED** — and it is the same PC the save is standing at.
- **E8** — needs mail that exists. Player-PC `MAILBOX` first (same PC); a held-mail read is the
  fallback. **PLANNED, conditional on the save actually having mail.**
- **C20** — needs a mon that evolves: an evolution stone or a Rare Candy on a mon one level short.
  Bag contents unknown until the boot. **PLANNED, conditional.**
- **H4/H5/H6** — a contest run and a Berry Blender game are progression events costing many
  minutes of emulated time each at ~20 fps. **BUDGET-GATED** — attempted only after the cheap rows
  land.
- **L2** — the credits play once, after the Elite Four. This save cleared the league long ago and
  Gen 3 has no credits replay. **UNREACHABLE by construction** (see Entry 2 for what ships instead).
- **A10** — a deliberate link failure. The app's link path is `celiolink.c`, which is FROZEN for
  this lane. **UNREACHABLE** unless a non-link route appears.
- **K4** — free: FireRed replays the quest log on every CONTINUE.

Fixture discipline: every EM arc runs on `roms/emerald-fix.{gba,sav}` (a real copy, the `.gba` a
symlink), never the user's `emerald.sav`.

---

## Entry 1 — 🎉 E15(EM) harvested [exact] AND proven by touch, on the first screen of the arc

Boot `runs/20260814-184139`, instance a, **`--stage-roms firered,emerald-fix`** — the pair, with
Emerald as gameB so it renders on the BOTTOM screen and owns touch (main.c:3547); FireRed sits on
the top screen at its title, deliberately never continued (that skips its ~3.5-minute quest-log
replay until the boot that needs it). `emerald-fix.sav` wakes at **26.28 (13,8)** — the Battle
Arena lobby, two tiles below its PC, which is where the by-touch storage arc saved it.

Route: `U1` + `a` (KEY) → "GUYA booted up the PC." → `a` → the **LANETTE'S PC / GUYA's PC / HALL OF
FAME / LOG OFF** multichoice → `D D a`.

| # | reading | value |
|---|---|---|
| 1 | gs ring, screen 1, while the HoF screen is up | **`cb2 = 0x08173560`** = `CB2_HallOfFame` **[exact]** on `pokeemerald.sym` |
| 2 | `ctx` at the same moment | **1 (field)** — undetected, exactly the BROKEN diagnosis |
| 3 | `fieldLock` / `dlgOwns` | **1 / 1** |
| 4 | one synthetic tap (`t 208 80 8`) | `dlgTaps` **0 → 1** |
| 5 | the game | the ⓐEXIT prompt fired: cb2 `0x08173560 → 0x08085E5C`, ctx `field → fmenu`, back at "Which PC should be accessed?" |

Captures: `evidence/impl/EM-P25-hof-pc-replay-cb2.bottom.png` (the screen the harvest was read on)
and `EM-P25-hof-tap-exited.bottom.png` (the same screen after the tap).

**Row E15 (EM half) = VERIFIED**, with a nuance worth more than the row: rows 3-5 say the tap
already works there *today*, before any promotion — because the **PC's own `lockall` is still
held** while the Hall-of-Fame sub-program runs, so lane B1's `fieldLock` route claims the frame.
B1's diagnosis ("no fingerprint ⇒ the walker owns it") is right about the fingerprint and wrong
about the consequence **for screens entered from a locked field script**. The promotion still
matters and still ships: the REAL Hall of Fame (row **L1**) is entered from the league battle
chain with no script holding the field, so it has nothing to fall back on.

Incidental: this save's HoF sector reads **"The HALL OF FAME data is corrupted."** — the screen,
its cb2 and its ⓐEXIT verb are all real, but the induction replay itself never plays here. Named,
not hidden.

## Entry 2 — E8 (mail read) harvested [exact] and proven, same boot, same PC

Continuing the same PC session: `GUYA's PC` → `MAILBOX` → the save's one letter, **ROMAN's MAIL** →
`READ`.

| # | reading | value |
|---|---|---|
| 1 | gs ring, screen 1, mail viewer up | **`cb2 = 0x08121C64`** = `CB2_MailRead` **[exact]** (`pokeemerald.sym`) |
| 2 | `ctx` | **1 (field)** — undetected, as diagnosed |
| 3 | `fieldLock` / `dlgOwns` | 1 / 1 (again the PC script's `lockall`) |
| 4 | one synthetic tap | `dlgTaps` **1 → 2** |
| 5 | the game | the viewer closed and the MAILBOX list came back; cb2 `0x08121C64 → 0x08085E5C` |

Captures: `EM-P25-mail-read-cb2.bottom.png` (the letter — "BE NICE / TO PLUSLE / ! VOLBEAT / WILL
BE / FANTASTIC. From ROMAN") and `EM-P25-mail-tap-closed.bottom.png`.

**Row E8 = VERIFIED.** Same nuance as E15: the mailbox route inherits the PC script's `lockall`, so
that half already worked; the **held-mail** route (party → MAIL → READ) is opened from a menu with
no script holding the field, and only the promoted fingerprint can cover it.

### E19 — re-diagnosed, and the screen is out of reach for a different reason than B1 gave

`EventScript_CableBoxResults` is reached from `field_control_avatar.c:378-380 / 404-406` —
`MetatileBehavior_IsCableBoxResults1/2`, i.e. the results machine is a **metatile in a cable-club
room** (`MAP_TRADE_CENTER` / `MAP_RECORD_CORNER` / the Union-Room side), and those maps are only
entered through a live link session. So:

- the row is **NOT** blocked on a fingerprint (there is no cb2 to harvest — `ShowLinkBattleRecords`
  only adds a window; `lockall` + `waitbuttonpress` hold the field around it), and
- its **screen is UNREACHABLE from a solo save** — you cannot stand in front of that machine
  without a link partner, and the app's link path (`celiolink.c`) is frozen for this lane.

Verdict: **VERIFIED-mech** (the mechanism is B3's, proven live in lane B1 and again on this boot's
PC dialog: `fieldLock 1` → `dlgOwns 1` → tap = A), with the honest note that the screen itself was
not visited. That is a strictly better answer than "BROKEN: undetected cb2", which was wrong.

---

## Entry 3 — what shipped: the promotion, and a class that was missing (`GCTX_INERT`)

Commit `f859a82` (`source/gamestate.{h,c}` + `source/touch.{c,h}` + `test/host/test_profiles.c`).

**1. `cb2FullUi` 18 → 32 slots, 13 EM + 10 FR values.** Two are the live [exact] reads above; the
other 21 are VERIFIED-SYM and say so in the row comment, per screen. The read is a compare, so a
wrong value can only leave a screen exactly as broken as it is today — it can never mis-key one.

**2. A new context, `GCTX_INERT` — "detected, and deliberately SILENT".** This project did not have
one. Until now a screen was either NAMED (and got the tap-advance verbs) or UNKNOWN (and fell
through to the walker with the whole route machinery). L2 and K4 both need the third thing, and for
the same reason: *the player is watching, not playing*.

- **L2, the credits**, named by cb2 (`GameProfile.cb2Inert`). Entry 0's `credits.c:349` finding is
  what forces it: a hold would be the fast-forward. The list is pinned DISJOINT from
  `cb2Title`/`cb2FullUi` so one screen can never classify two ways.
- **K4, FRLG quest-log playback**, named by STATE (`GameProfile.questLog` = `gQuestLogState`
  0x0203ADFA). The guard is the game's own `QL_IS_PLAYBACK_STATE` — state **2 or 3** — and NOT
  "non-zero", which would have been the tempting one-liner and would have killed touch for the
  whole game, because ordinary FRLG play sits at `QL_STATE_RECORDING` (1). Both rules run before
  every other rule in `game_read`, including the battle test: "do not touch this screen" cannot be
  overridden by something more specific.

**3. A latent bug, found while in the file and reported rather than quietly fixed:** the ctx-name
table (`gamestate.c` `GS_CTXN`) stopped at `GCTX_STORAGE` (13). `GCTX_MAP` is 14, so **every
region-map row in the gs ring and on the diag HUD has been printing `?` since lane B2 shipped
tap-to-fly**. Table extended with `"map"` and `"inert"`; TEST 20 pins both.

Suites, re-run from this tree: **test_profiles 1697 → 2546** (TEST 20: the columns per game, the
disjointness, `questLog` being EWRAM — which is *why* it needs no rev-alternate — and behaviour
through the real `game_read`: the credits go inert, the two live-harvested EM values classify,
Emerald is untouched by the FRLG guard, and the quest-log state boundary 0/1/2/3/4 does the right
thing including beating a live start-menu task and a battle callback). All 16 suites green:
celiolink 1259 · control 6940 · diag 376 · fieldpath **1808 (UNMODIFIED)** · fieldtrav 1210 ·
netlink 66 · peersprite 62078 · presence 61376 · **profiles 2546** · theme 83444 · tilt 1756 ·
touchgeom 371892 · trace_replay 58 (4 skips) · typography 1419 · uigeom 18332 · uihit 1834 =
**616 394 checks, 0 failures**. `make -j8` clean, `3DGBA.3dsx` 4 403 340 B (only the pre-existing
mgba/indentation warnings).

---

## Entry 4 — 🎉 the verification boot: K4 proven by A/B on one boot, and the promotion seen taking effect

Boot `runs/20260814-191846`, the **rebuilt** app, `--stage-roms emerald-fix,firered` — Emerald as
gameA (top, D4 seat `move_p1`), FireRed as gameB (bottom, so **FireRed owns touch**). One boot,
two games, each doing its own half of the proof, which is the technique the user named.

### K4 — the FRLG quest-log guard, before and after, same channel, same coordinates

FireRed continues straight into its quest-log playback:

| moment | `ctx` | `qlState` | `fieldLock` | touch script | result |
|---|---|---|---|---|---|
| **during playback** | **15 = GCTX_INERT** | **2** (`QL_STATE_PLAYBACK`) | 0 | 3 taps + one 90-frame hold, **all 8 ops picked up** (`[ctl touch] picked up 8 touch ops`) | `planSeq` **0**, `dlgTaps` 0, `dlgHolds` 0, `curKeys` 0, `walking` 0 |
| **after playback** (same boot, same taps) | 1 = field | **0** | 0 | 2 taps | `planSeq` **0 → 2** — the walker armed routes again |

Lane B1's BEFORE on this exact screen, for comparison: `fieldLock` 0, `msgMode` 0, and **three taps
produced `planSeq = 5`** — routes armed at a game the player does not control.

The second row is what makes the first one evidence rather than an absence: the identical script on
the identical channel DOES something the moment the guard stops applying, so "nothing happened"
cannot be "the taps never arrived". Captures: `FR-P25-questlog-inert.bottom.png` (the grayscale
"Previously on your quest…4" replay, mid-guard) and `FR-P25-questlog-over-taps-live.bottom.png`
(the same map in full colour after the replay, with the walker's own target ring drawn on the tile
the tap picked).

**Row K4 = VERIFIED.** The guard is the game's own playback test, so it costs one EWRAM byte and no
heuristics — and it is the same `GCTX_INERT` the credits use.

### The promotion, seen taking effect on the screen this lane harvested

While FireRed replayed, Emerald was driven by `move_p1` back to its PC and into **HALL OF FAME**.
The gs ring, both screens in the same dump:

```
frame  scr ctx      cb2         map      pos
 3946   0 fullui   0x08173560  26.28   13,7      <- Emerald: the Hall-of-Fame PC screen
 3980   1 inert    0x080565C8  13.1     3,4      <- FireRed: quest-log playback
```

That same screen read **`ctx = field`** in Entry 1, on the pre-promotion build, at the same
coordinates. One value changed and the classifier now names it. Capture:
`EM-P25-hof-now-fullui.top.png` (the TOP screen this time — Emerald's HUD, 26 fps, the same
"HALL OF FAME data is corrupted" screen with its ⓐEXIT).

Emulator hygiene: `azctl stop` (profile CLEAN, qt-config restored byte-identically) +
`clean-fixtures` (ROM originals re-hashed untouched, the user's `recent.bin` restored — sha256
`72c100ad…` identical to the pre-lane copy), `settings.bin` **never written** (sha256
`85dd487e…` identical before and after both boots), no Azahar left running, instance **b** never
touched.
