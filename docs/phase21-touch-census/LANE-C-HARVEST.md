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
