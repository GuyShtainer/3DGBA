# LANE B — LeafGreen rev1 live verification: the 13 sym-derived BPGE anchors

**Session 2026-08-14, overnight lane B (evidence-only).** Fixture = the user's LG cart
save (`roms/leafgreen.sav` — "poop", Lavender Town, 6 badges, dex 153, 199:37; lead mon
is a Lv10 Deoxys). Same harness recipe as LANE-B-RS.md (instance b, snapshot ELF, gs-ring
reads over gdb, `pokeleafgreen_rev1.sym` — the correct map for the user's rev-1.1 cart).

**Context:** commit 305242f shipped the BPGE row's 13 ROM anchors re-derived for LG rev1
(they were FR-rev0 before — battle/party/bag/fmenu detection silently dead on LG, proven
by the census). This document is the live promotion table those values were waiting for.

## THE TABLE — 13 anchors, live verdicts

| # | anchor | shipped (LG rev1) | live evidence (gs ring, instance b) | verdict |
|---|---|---|---|---|
| 1 | cb2UpdParty `CB2_UpdatePartyMenu` | 0x0811EBF0 | party open: cb2 == 0x0811EBF0 [exact], **ctx=party fired (first time ever on LG)** | **VERIFIED [exact]** |
| 2 | cb2InitParty `CB2_InitPartyMenu` | 0x0811EC20 | edge rows caught the transient: cb2 == 0x0811EC20 [exact] for ~2 rows, ctx=party already resolving on it | **VERIFIED [exact]** |
| 3 | chooseTarget `HandleInputChooseTarget` | 0x0802E688 | needs a DOUBLE battle's target-choose screen — not cheaply reachable on this save tonight | **NOT EXERCISED — sym-verified only (map [exact]); flag for a future double-battle pass** |
| 4 | startCbInput `StartCB_HandleInput` | 0x0806F294 | **the current build does not consume this column**: START-menu detection is task-only (gamestate.c:518-521 — the callback compare was retired for false-positives) | **UNUSED-BY-CODE (sym-verified value, runtime-moot)** |
| 5 | cb2BagRun `CB2_BagMenuRun` | 0x08107F30 | bag open: cb2 == 0x08107F30 [exact], **ctx=bag fired** | **VERIFIED [exact]** |
| 6 | bagHandler `Task_BagMenu_HandleInput` | 0x08108F5C | same rows: taskFp == 0x08108F5C [exact] | **VERIFIED [exact]** |
| 7 | partyTask `Task_HandleChooseMonInput` | 0x0811FB78 | party rows: taskFp == 0x0811FB78 [exact] | **VERIFIED [exact]** |
| 8 | yesNoTask `Task_YesNoMenu_HandleInput` | 0x0809CE3C | **four distinct yes/no flows exercised — NONE reaches this task**: save prompt runs under the start-menu chain (taskFp stays `Task_StartMenuHandleInput`); the nurse heal yes/no is `Task_MultichoiceMenu_HandleInput`; bag-toss confirm AND mart buy confirm are `Task_CallYesOrNoCallback` 0x080BF548 [exact] | **map-correct, LIVE-UNREACHED — see "promotion recommendation" below** |
| 9 | multiTask `Task_MultichoiceMenu_HandleInput` | 0x0809CC80 | nurse heal yes/no: taskFp == 0x0809CC80 [exact], **ctx=fmenu fired** | **VERIFIED [exact]** |
| 10 | selMenuTask `Task_HandleSelectionMenuInput` | 0x08122CAC | party mon A-menu: taskFp == 0x08122CAC [exact], ctx=fmenu | **VERIFIED [exact]** |
| 11 | startMenuTask `Task_StartMenuHandleInput` | 0x0806F204 | START menu: taskFp == 0x0806F204 [exact], **ctx=fmenu fired (first time ever on LG)** | **VERIFIED [exact]** |
| 12 | battleMainCb `BattleMainCB2` | 0x08011114 | trainer battle (Route 8 sight): cb2 == 0x08011114 [exact]; **full gate sequence b.oth → b.act → b.move observed** (ring rows [180]/[206]/[212] of boot 022703) | **VERIFIED [exact]** |
| 13 | mapNameTask `Task_MapNamePopup` | 0x08098194 | boundary re-cross with fast polling: taskFp == 0x08098194 [exact] caught in a heartbeat row (the popup is ~2 s transient) | **VERIFIED [exact]** |

**Score: 10/13 VERIFIED live [exact]; #3 unreachable tonight (honest skip), #4 dead
column by design, #8 map-correct but bypassed by every common flow.**

### Promotion recommendation from #8 (a real pipeline finding)

FRLG routes its common yes/no prompts through THREE different handlers, none of them
`Task_YesNoMenu_HandleInput`:
- start-menu save chain → stays under `Task_StartMenuHandleInput` (already detected),
- script yes/no (nurse) → `Task_MultichoiceMenu_HandleInput` (already detected),
- bag/mart money-and-item confirms → **`Task_CallYesOrNoCallback` 0x080BF548 (LG rev1,
  [exact] live twice)** — NOT currently in any anchor column, so those two confirm
  screens read as `field` (bag-toss row) / raw buy-screen cb2. The next BPRE/BPGE anchor
  edit should add it (with its FR-rev1 equivalent re-derived from the sym maps) as the
  yes/no alternate. Until then yesNoTask is a loaded-but-never-fired detection on FRLG.

## Census FULLUI/TITLE spot-harvest on LG (BPGE lists ship empty — these are the fill)

All live-read [exact] on `pokeleafgreen_rev1.sym`:

| screen | cb2 | task(s) | proposed class |
|---|---|---|---|
| GF intro | 0x080EC9C0 `CB2_Intro` | — | cb2Title |
| Title | 0x08078BB0 `CB2_TitleScreenRun` | — | cb2Title |
| Main menu | 0x0800C2E8 `CB2_MainMenu` (+init 0x0800C314) | — | cb2Title |
| Overworld | 0x080565C8 `CB2_Overworld` | — | baseline |
| Options | 0x08088344 `CB2_InitOptionMenu` (stays as run loop, the FR pattern) | `Task_OptionMenu` 0x08088768 | cb2FullUi |
| Pokedex | 0x08102524 `CB2_PokedexScreen` | `Task_PokedexScreen` 0x081028CC | cb2FullUi |
| Trainer card | 0x08089058 `CB2_TrainerCard` | `Task_TrainerCard` 0x080890A8 | cb2FullUi |
| Party (start-menu route) | transition 0x08126F00 `CB2_PartyMenuFromStartMenu` | — | (transition, note only) |
| Mart shop menu | overworld cb2 | `Task_ShopMenu` 0x0809ABF8 | (lists family: LK_BUY entry) |
| Mart buy screen | 0x0809ADCC `CB2_BuyMenu` | `Task_CallYesOrNoCallback` on the confirm | lists family LK_BUY |
| Bag list internals | — | `ListMenuDummyTask` 0x08106F1C, `Task_ScrollIndicatorArrowPair` 0x08133BF4 | lists-family corroboration |

## Ops notes / logger caveats (keep)

- **Quest-log playback leaks HISTORICAL map/pos into the gs ring** (rows showed map 3,2
  px (20,39) with cb2=CB2_Overworld before the real Lavender resume) — any consumer of
  early-boot FRLG ring rows must wait for the post-playback settle (~60-90 s).
- D4 grammar reminder that cost this lane 20 minutes: **bare L/R/U/D tokens are
  TAP-TURNS, not steps** — counted walks (`L1`) move, bare letters only rotate.
- START toggles the start menu — a stray `s` while a dialog is closing CLOSES the menu
  you just opened; give `b W120`+ after full-screen UIs before `s`.
- FRLG start-menu cursor REMEMBERS its slot across open/close (but resets on reboot);
  U×3 from POKEDEX wraps to SAVE — count from the known slot or expect the save prompt.
- Lavender: Center door (6,5); mart door (20,15) — approachable ONLY from the south
  (x=18 is walled for y=13-15); volunteer-house sign blocks (7,11). Mart clerk talk spot:
  (4,3) facing LEFT (across the register).
- Route 8 from Lavender: cross at y=9; the y=12 tree row forces the sand path; a trainer
  guards (62,14) and sight-triggers — used deliberately for the battle anchor. Trainer
  battles: Deoxys Lv10 vs Lv22 gets Sing-locked — kill the boot mid-battle instead of
  fighting (the anchor reads complete before any outcome; next boot resumes at the last
  in-game save).
- A mid-boot Lane-A rebuild of the main tree invalidates `build/3DGBA.map` (gdbio
  crosscheck WARNS — harmless with the snapshot ELF) — but the gdb broker/stub died once
  this session ("emulator closed the RSP socket"); a boot's one gdb client cannot be
  revived: reboot. Evidence-lane rule confirmed: snapshot ELF/3dsx + `EMUTEST_ELF`/
  `EMUTEST_APP` kept every read correct across the rebuild.

Captures: `evidence/leafgreen/` (16 files: menus, battle chain, dex/card/options, mart,
the four yes/no flows).
