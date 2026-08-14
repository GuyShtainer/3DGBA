# RS-REV2-VERIFICATION — do the shipped AXVE/AXPE addresses hold on the user's REV-2 carts?

**Date:** 2026-08-14 (desk work only — no emulator, no build, no source edit; the phase-21 census
workflow owns the main tree tonight).
**Gate:** this document gates the first-ever Ruby/Sapphire boot. House rule: never run a guessed
address; every value below is cited to a pret symbol map fetched and diffed **on this machine this
session** (scratchpad `syms/`, md5s and git blob SHAs recorded below).

**Bottom line first:**

- **AXVE/AXPE (Ruby/Sapphire): every shipped address is CORRECT for rev 2. Zero edits needed.**
  All 31 non-zero fields verify SAME across all six pret maps (ruby/sapphire × rev0/rev1/rev2),
  including the one shipped ROM address (`BattleMainCB2 0x0800F808`). The rev1 and rev2 symbol
  maps are *byte-identical files* (git-blob-level proof, §2), and the 727-RAM-symbol whole-map
  diff that licensed the shared row body extends from 4 maps to **6 maps, zero differences**.
  Nothing is UNRESOLVED. Status remains **VERIFIED-SYM / VERIFY-ON-HW** (still never executed).
- **BPGE (LeafGreen, user's cart rev 1.1): all RAM fields SAME, but 13 ROM function pointers
  DIFFER** — the shipped BPGE row carries FireRed-rev0 values that are wrong for *every* LeafGreen
  revision (a known, documented risk — `gamestate.c:2`, COVERAGE.md §2 BPGE row — now quantified).
  Exact replacement rows in §6. All 13 are compare-only fingerprints, so today they degrade
  silently (battle/party/bag/menu detection dead on LG), never a bad read/write.
- One latent typo found in passing (§7): `newKeys` is `0x0303011E` in the BPRE and BPGE rows but
  gMain+0x2E = `0x0300311E`. The field is unused at runtime (keys are injected via the returned
  mask, `gamestate.h:37`) — zero behavioural impact, flagged for the next row edit.

---

## 1. What the shipped rows were derived from (provenance, read from source comments)

| Block | Source of shipped values | Where stated |
|---|---|---|
| AXVE/AXPE main row (phase 18, SPEC-coop §P3) | pret `symbols` branch: `pokeruby.sym`, `pokesapphire.sym`, `pokeruby_rev1.sym`, `pokesapphire_rev1.sym` — "all FOUR maps downloaded and diffed on 2026-08-13 … 727 symbols, ZERO differences" | `gamestate.c:116-123` |
| AXVE/AXPE phase-20 additions (gSprites / gPlttBufferUnfaded / gPlayerAvatar) | same four maps, same session, "4/4 AGREE" | `gamestate.c:214-227`, `gamestate.h:158-163` |
| The one ROM address `battleMainCb 0x0800F808` | "only because all four maps give the identical value"; explicitly verify-on-hw-pending | `gamestate.c:170-175` |
| BPGE row (touch-era fields) | **FireRed-rev0** values, LeafGreen-unverified (declared risk) | `gamestate.c:2`, COVERAGE.md §2 |
| BPGE later-phase fields (sb2ptr/hbCtr trio, mapHeaderPath, peer-sprite trio) | LeafGreen's OWN maps, rev0 AND rev1 agreeing | `gamestate.c:97-113` |

**So: rev2 was never in any diff set for RS** — the user's carts (both headers `0xBC = 2`, read on
this machine 2026-08-13) were outside the verified envelope until now.

## 2. Evidence chain for THIS verification (all fetched 2026-08-14)

Files: `pokeruby{,_rev1,_rev2}.sym`, `pokesapphire{,_rev1,_rev2}.sym` from **pret/pokeruby**
branch `symbols` (head `a2aba56bab1b`, 2026-03-27); `pokefirered{,_rev1}.sym`,
`pokeleafgreen{,_rev1}.sym` from **pret/pokefirered** branch `symbols`. (There is no separate
pokesapphire or pokeleafgreen repo — one repo builds both titles.)

Three integrity proofs, because "identical" is exactly the kind of result a bad fetch fakes:

1. **The rev2 files are genuinely distinct paths with identical content.** GitHub tree of
   `pret/pokeruby@symbols`: `pokeruby_rev1.sym` and `pokeruby_rev2.sym` are two entries sharing
   blob SHA `503f8ca84636`; `pokesapphire_rev1/rev2.sym` share `debc6ee08cfd`. Local md5s agree
   (rev1 == rev2, ≠ rev0). So *pret's own byte-matched builds produce identical symbol tables for
   rev1 and rev2* — a result, not an artifact.
2. **rev2 is a real byte-matched build target**, not a placeholder: `pret/pokeruby@master` has
   `ruby_rev2.sha1` (`5b64eacf892920518db4ec664e62a086dd5f5bc8  pokeruby_rev2.gba`) and
   `sapphire_rev2.sha1` (`89b45fb172e6b55d51fc0e61989775187f6fe63c  pokesapphire_rev2.gba`), with
   `Makefile:263/267` (`GAME_REVISION=2`) and `compare_ruby_rev2`/`compare_sapphire_rev2` targets.
   The ROMs differ per revision (six distinct .sha1 values) while the symbol *layout* does not
   move between rev1 and rev2.
3. **The six maps do differ where they should**: ROM symbols drift between rev0 and rev1/2 —
   first drifted symbol `sub_803FBBC` (rev0 `0x0803FBBC` → rev1/2 `0x0803FBDC`, +0x20; 44,879 of
   50,234 ROM symbols move; `Task_StartMenu` `0x08071254→74` matches the example already cited in
   `gamestate.c:121-122`). Everything below `0x0803FB68+sz` is identical — which is *why* the one
   shipped ROM address is safe (§3, row `battleMainCb`).

Whole-map RAM diff, this session: **727 `0x02*/0x03*` symbols per map, ZERO differences across
all six maps** (name, address, and size all equal). This extends the phase-18 four-map proof to
six and is the load-bearing fact for every row below.

## 3. THE TABLE — every field the AXVE/AXPE profile uses, shipped vs rev2

Method: each shipped address was reverse-looked-up in `pokeruby.sym` (rev0) to confirm its symbol
identity, then that symbol re-derived independently from `pokeruby_rev2.sym` and
`pokesapphire_rev2.sym` (and cross-checked in the other three maps). gMain-relative fields were
re-derived as base+offset with the struct offsets from `gamestate.h:76-122` (offsets are
VERIFIED-SRC against pret `include/main.h`, unchanged this session). Line numbers cited are
`pokeruby_rev2.sym`; the rev0/rev1 and both sapphire files carry the same symbol on the same line
(byte-identical layout).

| field | shipped value | rev2 value | verdict | citation (pokeruby_rev2.sym) |
|---|---|---|---|---|
| sb1ptr (= `gSaveBlock1` struct, sbDirect) | 0x02025734 | 0x02025734 | **SAME** (6/6) | :106 `02025734 g 00003ac0 gSaveBlock1` |
| battleFlags (`gBattleTypeFlags`) | 0x020239F8 | 0x020239F8 | **SAME** (6/6) | :23 |
| actionCursor (`gActionSelectionCursor`) | 0x02024E60 | 0x02024E60 | **SAME** (6/6) | :94 |
| moveCursor (`gMoveSelectionCursor`) | 0x02024E64 | 0x02024E64 | **SAME** (6/6) | :95 |
| battleMons (`gBattleMons`) | 0x02024A80 | 0x02024A80 | **SAME** (6/6) | :37 |
| bg0y (`gBattle_BG0_Y`) | 0x030042A0 | 0x030042A0 | **SAME** (6/6) | :599 (sz 4) |
| partyCount (`gPlayerPartyCount`) | 0x03004350 | 0x03004350 | **SAME** (6/6) | :613 (sz 4, read as u8 — inert while partyTask=0) |
| mainCb2 (`gMain`+0x04) | 0x03001774 | 0x03001774 | **SAME** (6/6) | :526 `03001770 g 00000440 gMain` |
| newKeys (`gMain`+0x2E) | 0x0300179E | 0x0300179E | **SAME** (6/6) | :526 (offset per include/main.h; unused at runtime) |
| ctrlFuncs (`gBattlerControllerFuncs`) | 0x03004330 | 0x03004330 | **SAME** (6/6) | :608 (sz 0x10) |
| battlerPos (`gBattlerPositions`) | 0x02024A72 | 0x02024A72 | **SAME** (6/6) | :32 |
| battlersCount (`gBattlersCount`) | 0x02024A68 | 0x02024A68 | **SAME** (6/6) | :30 |
| absentFlags (`gAbsentBattlerFlags`) | 0x02024C0C | 0x02024C0C | **SAME** (6/6) | :54 |
| activeBattler (`gActiveBattler`) | 0x02024A60 | 0x02024A60 | **SAME** (6/6) | :28 |
| mapLayout (`gBackupMapLayout`) | 0x03004870 | 0x03004870 | **SAME** (6/6) | :630 (sz 0xC) |
| startCb (`gMenuCallback`) | 0x03004AE8 | 0x03004AE8 | **SAME** (6/6) | :637 (sz 4) |
| startCursor (`sStartMenuCursorPos`) | 0x0202E8FC | 0x0202E8FC | **SAME** (6/6) | :162 (sz 1) |
| gTasksBase (`gTasks`) | 0x03004B20 | 0x03004B20 | **SAME** (6/6) | :642 (sz 0x280 = 16×40 ✓) |
| **battleMainCb (`BattleMainCB2`) — the ONE ROM addr** | 0x0800F808 | 0x0800F808 | **SAME** (6/6) | :1378 (sz 0x20). Safe *because* it sits below the rev0→rev1/2 ROM drift point 0x0803FBBC (§2.3). The "all maps agree" licence now covers rev2. Still verify-on-hw (enter an RS battle, see ctx leave the field — `gamestate.c:170-175`). |
| mapObjects (`gObjectEvents`) | 0x030048A0 | 0x030048A0 | **SAME** (6/6) | :634 (sz 0x240 = 16×0x24 ✓) |
| fieldMsgMode (`sMessageBoxMode`) | 0x030005A8 | 0x030005A8 | **SAME** (6/6) | :464 (sz 1) |
| fieldCamera (`gFieldCamera`) | 0x03004880 | 0x03004880 | **SAME** (6/6) | :631 (sz 0x18 ✓ matches field_camera.h) |
| linkStatus (`gLinkStatus`) | 0x03002A60 | 0x03002A60 | **SAME** (6/6) | :558 |
| linkErr (`gLinkErrorOccurred`) | 0x0300295C | 0x0300295C | **SAME** (6/6) | :549 |
| vblankCtr (`gMain`+0x20) | 0x03001790 | 0x03001790 | **SAME** (6/6) | :526 (a real u32 on RS — `gamestate.c:205-206`) |
| sb2ptr (= `gSaveBlock2` struct, sbDirect) | 0x02024EA4 | 0x02024EA4 | **SAME** (6/6) | :105 `02024ea4 g 00000890 gSaveBlock2` |
| hbCtr (`gMain`+0x24) | 0x03001794 | 0x03001794 | **SAME** (6/6) | :526 |
| mapHeaderPath (`gMapHeader`) | 0x0202E828 | 0x0202E828 | **SAME** (6/6) | :126 (sz 0x1C) |
| sprites (`gSprites`) — phase 20 | 0x02020004 | 0x02020004 | **SAME** (6/6) | :5 (sz 0x1144 = 65×0x44 ✓) |
| plttUnfaded (`gPlttBufferUnfaded`) — phase 20 | 0x0202EAC8 | 0x0202EAC8 | **SAME** (6/6) | :174 (sz 0x400) |
| playerAvatar (`gPlayerAvatar`) — phase 20 | 0x0202E858 | 0x0202E858 | **SAME** (6/6) | :130 (sz 0x24, Emerald-shaped ✓) |
| sbDirect = 1 | 1 | 1 | **SAME** | no `gSaveBlock1Ptr`/`gSaveBlock2Ptr` symbol exists in any of the six maps (re-checked in rev2) |

**Zeroed fields — every named degradation re-confirmed against rev2** (the reasoning that
licensed each 0 still holds verbatim):

| zeroed field(s) | licence in `gamestate.c` | rev2 re-check |
|---|---|---|
| partyMenu, multiCursor, gWindowsBase, linkErrBuf, linkNotRecv | :152-155 — symbols don't exist in RS | `gPartyMenu`, `gMultiUsePlayerCursor`, `gWindows`, `sLinkErrorBuffer`, `gRemoteLinkPlayersNotReceived` — **all still absent** in both rev2 maps |
| sMenuBase | :156-160 — RS `sMenu` is sz 4, not Emerald's layout | `sMenu` still `020388b8 l 00000004` in rev2 (:312) — 0 stays right |
| spriteCoordOff | :165-168 — X/Y are 0x310 apart, breaking the "+2" contract | rev2: `gSpriteCoordOffsetX 0x030024D0` (:535) / `Y 0x030027E0` — still 0x310 apart |
| ROM task/callback pointers (cb2Upd, cb2Init, chooseTgt, startCbInput, cb2BagRun, bagHandler, partyTask, yesNoTask, multiTask, selMenuTask, startMenuTask, mapNameTask) | :143-151 — rev/title-sensitive; several don't exist in the RS decomp | still correct — and now *provably* rev-sensitive: 44,879 ROM symbols moved rev0→rev1/2 (§2.3). The equivalents that DO exist in RS maps (e.g. `Task_YesNoMenu`-analogues) remain unshipped, correctly. |

**UNRESOLVED fields: none.** Nothing degrades relative to what is already documented; the RS
feature envelope stays exactly as COVERAGE.md §2 describes (overworld + on-paper battle gate;
menus degrade to GCTX_OVERWORLD by design).

## 4. Replacement rows for AXVE/AXPE

**None. The shipped `RS_PROFILE_BODY` (`gamestate.c:188-227`) is correct for rev 2 verbatim** —
both rows, all fields, including both phase-20 additions. The only edit this document licenses is
a *comment* update when someone next touches the file (not tonight — source is frozen): the
provenance note "all FOUR maps" may be strengthened to "all SIX maps incl. rev2 (RS-REV2-
VERIFICATION.md, 2026-08-14)", and rev0/rev1 in the two row-comments may say rev0/1/2.

**RS boot tonight: GREEN from the address side.** Every address the profile will exercise is now
verified for the exact cartridge revision in the user's hands. The row's status stays
VERIFIED-SYM / VERIFY-ON-HW — nothing here has ever *executed*; the P3.6 cheap proof stands
(boot Ruby beside Sapphire, confirm tap-to-walk on both, then check `g_presDiag.sprReason == 0`;
battle entry promotes `battleMainCb`).

## 5. Phase-21 detection additions — RS equivalents

Phase 21 has wired **no new profile fields** (the census is logging-only: cb2 identities are
harvested into CB2-HARVEST.md for *later* promotion — COVERAGE.md §"promotion pipeline"). So
there is nothing further to re-derive for RS today. **Rule for the future promotion pass,
established by this verification:** any RS screen-cb2 promoted from a future RS census visit must
be read from `pokeruby_rev1/rev2.sym` (identical), **not** `pokeruby.sym` (rev0) — ROM addresses
above `0x0803FBBC` differ by +0x20 between rev0 and the user's rev2 carts. RAM promotions may use
any of the six maps (727-symbol identity). The old "identical in all four maps" ROM rule becomes
"identical in all six, else ship the rev2 value" now that both cart revisions are pinned.

## 6. LeafGreen (BPGE) — user's cart is rev 1.1

Checked: every field the BPGE row uses, against `pokeleafgreen_rev1.sym` (with
`pokeleafgreen.sym`, `pokefirered.sym`, `pokefirered_rev1.sym` alongside). Two results:

**(a) Every RAM field is SAME** — LG rev1 == LG rev0 == FR rev0/rev1 for all of: sb1ptr
0x03005008, battleFlags 0x02022B4C, actionCursor 0x02023FF8, moveCursor 0x02023FFC, battleMons
0x02023BE4, bg0y 0x02022976, partyMenu 0x0203B0A0, partyCount 0x02024029, gMain 0x030030F0
(→ mainCb2/vblankCtr/hbCtr/newKeys offsets), ctrlFuncs 0x03004FE0, multiCursor 0x03004FF4,
battlerPos/battlersCount/absentFlags/activeBattler, mapLayout (`VMap` 0x03005040,
pokeleafgreen_rev1.sym:820), startCb (`sStartMenuCallback` 0x020370F0, :240), sMenu 0x0203ADE4,
gWindows 0x020204B4, startCursor 0x020370F4, gTasks 0x03005090, bagOpen (`gBagMenuState`
0x0203ACFC +0x05 = 0x0203AD01, :440), mapObjects 0x02036E38, fieldMsgMode 0x0203709C,
fieldCamera 0x03005050, the four link-diag fields, sb2ptr 0x0300500C, spriteCoordOff 0x02021BC8,
mapHeaderPath 0x02036DFC, sprites 0x0202063C, plttUnfaded 0x020371F8, playerAvatar 0x02037078.
(The later-phase fields were already read from LG's own maps — re-confirmed.)

**(b) All 13 ROM function pointers DIFFER.** The shipped values are FireRed-rev0; they are wrong
for LG rev0 *and* LG rev1 (four distinct value sets exist across FRLG maps). The user's cart is
rev 1.1 → column "LG rev1" governs:

| field | shipped (FR rev0) | LG rev0 | **LG rev1 (correct)** | verdict | citation (pokeleafgreen_rev1.sym) |
|---|---|---|---|---|---|
| cb2UpdParty (`CB2_UpdatePartyMenu`) | 0x0811EBA0 | 0x0811EB78 | **0x0811EBF0** | DIFFERS | :10994 |
| cb2InitParty (`CB2_InitPartyMenu`) | 0x0811EBD0 | 0x0811EBA8 | **0x0811EC20** | DIFFERS | :10996 |
| chooseTarget (`HandleInputChooseTarget`) | 0x0802E674 | 0x0802E674 | **0x0802E688** | DIFFERS | :1992 |
| startCbInput (`StartCB_HandleInput`) | 0x0806F280 | 0x0806F280 | **0x0806F294** | DIFFERS | :4862 |
| cb2BagRun (`CB2_BagMenuRun`) | 0x08107EE0 | 0x08107EB8 | **0x08107F30** | DIFFERS | :10173 |
| bagHandler (`Task_BagMenu_HandleInput`) | 0x08108F0C | 0x08108EE4 | **0x08108F5C** | DIFFERS | :10215 |
| partyTask (`Task_HandleChooseMonInput`) | 0x0811FB28 | 0x0811FB00 | **0x0811FB78** | DIFFERS | :11028 |
| yesNoTask (`Task_YesNoMenu_HandleInput`) | 0x0809CE54 | 0x0809CE28 | **0x0809CE3C** | DIFFERS | :6725 |
| multiTask (`Task_MultichoiceMenu_HandleInput`) | 0x0809CC98 | 0x0809CC6C | **0x0809CC80** | DIFFERS | :6721 |
| selMenuTask (`Task_HandleSelectionMenuInput`) | 0x08122C5C | 0x08122C34 | **0x08122CAC** | DIFFERS | :11152 |
| startMenuTask (`Task_StartMenuHandleInput`) | 0x0806F1F0 | 0x0806F1F0 | **0x0806F204** | DIFFERS | :4860 |
| battleMainCb (`BattleMainCB2`) | 0x08011100 | 0x08011100 | **0x08011114** | DIFFERS | :1562 |
| mapNameTask (`Task_MapNamePopup`) | 0x080981AC | 0x08098180 | **0x08098194** | DIFFERS | :6522 |

Cross-validation: the census's live FR-rev1 harvest (CB2-HARVEST.md:85-100) independently found
the same class of drift on FireRed; my desk derivation of the FR-rev1 column agrees with all six
values the harvest lists (BattleMainCB2 0x08011114, CB2_UpdatePartyMenu 0x0811EC18, CB2_BagMenuRun
0x08107F58, Task_HandleChooseMonInput 0x0811FBA0, Task_StartMenuHandleInput 0x0806F204,
Task_HandleSelectionMenuInput 0x08122CD4) — two independent methods, one answer.

**What degrades while BPGE ships the wrong pointers:** all 13 are equality-compare fingerprints
(`task_active`, cb2 gates, controller-func scan) — they never dereference, so nothing crashes and
nothing wrong is written. But on the user's LG rev1: battle detection is dead (`cb2 !=
battleMainCb` → GCTX_BATTLE_* unreachable → battles read as overworld, taps try to *walk*),
and party/bag/START-menu/yes-no/multichoice/selection detection never fires (GCTX stays
OVERWORLD). Same failure signature the census already proved live for FR rev1.

**Ready-to-paste replacement — the BPGE row's changed literals** (`gamestate.c:85-91` layout
preserved; only **bold-marked** positions change; lines 92-113 of the row are untouched):

```c
  { "BPGE", 0x03005008u, 0x02022B4Cu, 0x02023FF8u, 0x02023FFCu, 0x02023BE4u, 0x02022976u,
            0x0203B0A0u, 0x02024029u, 0x030030F4u, 0x0811EBF0u, 0x0811EC20u, 0x0300311Eu,
            0x03004FE0u, 0x0802E688u, 0x03004FF4u, 0x02023BD6u, 0x02023BCCu, 0x02023D70u, 0x02023BC4u, 0x03005040u,
            0x020370F0u, 0x0806F294u, 0x0203ADE4u, 0x020204B4u, 0x020370F4u, 0x03005090u, 0x08107F30u, 0x08108F5Cu, 0x0203AD01u,
            0x0811FB78u, 0x0809CE3Cu, 0x0809CC80u, 0x08122CACu, 0x0806F204u, 0x00000000u,
            0x08011114u, 0x02036E38u,
            0x0203709Cu, 0x08098194u, 0x03005050u,
```

Changed vs shipped: positions cb2Upd (→0x0811EBF0), cb2Init (→0x0811EC20), newKeys (→0x0300311E,
the §7 typo fix — unused field, optional), chooseTgt (→0x0802E688), startCbInput (→0x0806F294),
cb2BagRun (→0x08107F30), bagHandler (→0x08108F5C), partyTask (→0x0811FB78), yesNoTask
(→0x0809CE3C), multiTask (→0x0809CC80), selMenu (→0x08122CAC), startMenu (→0x0806F204),
battleMainCb (→0x08011114), mapNameTask (→0x08098194). **These values are pinned to LG rev 1.1
(the user's cart) and are wrong for an LG rev0 ROM** — the profile keys on the 4-char code and
cannot see the revision byte, so the row must document which rev it serves (BPRE now has the same
property). Apply only in a phase that may edit source/, with the suite green, alongside the
equivalent BPRE-rev1 fix the census already specified (CB2-HARVEST.md:98-100; the FR-rev1 column
for the remaining fields is: cb2Init 0x0811EC48, chooseTgt 0x0802E688, startCbInput 0x0806F294,
bagHandler 0x08108F84, yesNoTask 0x0809CE68, multiTask 0x0809CCAC, mapNameTask 0x080981C0).

## 7. Latent typo (flag only — do not fix tonight)

`newKeys` in the BPRE row (`gamestate.c:49`) and BPGE row (`:86`) is `0x0303011E`; gMain+0x2E is
`0x0300311E` (gMain 0x030030F0, all four FRLG maps, `pokefirered.sym:745`). Digits transposed.
Harmless today — `gamestate.h:37` documents the field as unused ("we inject via the returned key
mask") and no code reads `p->newKeys` — but it is a loaded gun for any future edit that starts
using the field. The Emerald and RS rows are correct. Recorded here so the next row edit fixes it
deliberately.

## 8. Working artifacts

Scratchpad (session-local, not committed): `syms/` (ten .sym files + md5s), `rs_rev2_check.py`
(the comparison script: address→symbol reverse lookup in rev0, independent re-derivation in the
other five maps, whole-map RAM diff, LG field-by-field check), `rs_rev2_out.txt` (full output).
Incremental log: `OVERNIGHT2-BUILDLOG.md` (this doc's findings were banked there before this file
was written).
