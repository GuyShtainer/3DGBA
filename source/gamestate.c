// gamestate.c — see gamestate.h. Addresses verified vs pret's byte-matched sym maps (symbols branch),
// US v1.0/rev0. FRLG share one RAM map; LeafGreen's NEW symbols are FireRed-derived (see spec Risks).
#include <string.h>
#include <stdio.h>      // FILE / fprintf (the SD log dump)
#include <sys/stat.h>   // mkdir (ensure the netlogs dir exists)
#include "gamestate.h"

#define BMON_MOVES_OFF 0x0C   // BattlePokemon.moves[] offset (4x u16)
#define PM_TYPE_OFF    0x08   // gPartyMenu: low nibble menuType (0 field/1 battle), bits4-5 layout
#define PM_SLOT_OFF    0x09   // gPartyMenu.slotId (s8)

static const GameProfile PROFILES[] = {
  // code   sb1ptr      battleFlags  actionCur   moveCur     battleMons  bg0y
  //        partyMenu   partyCount  mainCb2     cb2Upd      cb2Init     newKeys
  //        ctrlFuncs   chooseTgt   multiCursor battlerPos  battlersCnt absentFlg   activeBat   mapLayout
  //        startCb     startCbInput sMenuBase  gWindows    startCursor gTasks      cb2BagRun   bagHandler  bagOpen
  { "BPEE", 0x03005D8Cu, 0x02022FECu, 0x020244ACu, 0x020244B0u, 0x02024084u, 0x02022E16u,
            0x0203CEC8u, 0x020244E9u, 0x030022C4u, 0x081B01B0u, 0x081B01E0u, 0x030022EEu,
            0x03005D60u, 0x08057824u, 0x03005D74u, 0x02024076u, 0x0202406Cu, 0x02024210u, 0x02024064u, 0x03005DC0u,
            0x03005DF4u, 0x0809FAC4u, 0x0203CD90u, 0x02020004u, 0x0203760Eu, 0x03005E00u, 0x081AAD5Cu, 0x081ABD28u, 0x00000000u,
            0x081B1370u, 0x080E215Cu, 0x080E2058u, 0x081B3730u, 0x0809FA34u, 0x02037318u,
            0x08038420u, 0x02037350u,
            0x020375BCu, 0x080D487Cu, 0x03005DD0u,
            /* link diag (EM): gLinkStatus gLinkErrorOccurred sLinkErrorBuffer gRemoteLinkPlayersNotReceived */
            0x030030E0u, 0x0300306Cu, 0x02022B00u, 0x03003078u,
            /* D2 vblankCtr (EM): gMain.vblankCounter1 = gMain+0x20 = mainCb2(0x030022C4)-4+0x20;
               derived-from-verified-anchors (newKeys 0x030022EE = gMain+0x2E), verify-on-hw-pending */
            0x030022E0u,
            /* phase 15 presence (SPEC-data D1.7): sb2ptr / spriteCoordOff / hbCtr — all VERIFIED-SYM
               vs pret's symbols branch, re-read 2026-08-04. EM: gSaveBlock2Ptr 0x03005D90
               (pokeemerald.sym:962), gSpriteCoordOffsetX 0x02021BBC (:20; Y = +2, :21),
               gMain.vblankCounter2 = gMain(0x030022C0, :894) + 0x24. */
            0x03005D90u, 0x02021BBCu, 0x030022E4u,
            /* phase 18 mapHeaderPath (SPEC-door T4.1): gMapHeader, pokeemerald.sym. VERIFIED-SYM.
               Same value as `mapHeader` above by construction; kept as its own field so the
               phase-14 depth gate (main.c:885 `!p->mapHeader`) is not disturbed for FR/LG. */
            0x02037318u,
            /* sbDirect (SPEC-coop P3.2.3): Emerald HAS gSaveBlock1Ptr/gSaveBlock2Ptr, so the two
               columns above stay POINTERS. Written explicitly rather than left to C's zero-fill —
               an implicit zero in a table this long is how a future append goes wrong (P3.5.2). */
            0,
            /* phase 20 peer sprite (SPEC S1.3): gSprites / gPlttBufferUnfaded / gPlayerAvatar,
               re-derived from pokeemerald.sym on 2026-08-13. VERIFIED-SYM.
                 02020630 g 00001144 gSprites            (0x1144 = 65 * 0x44)
                 02037714 g 00000400 gPlttBufferUnfaded  (0x400 = 512 u16)
                 02037590 g 00000024 gPlayerAvatar */
            0x02020630u, 0x02037714u, 0x02037590u,
            /* phase 22.0 rev-alternate ROM anchors: NONE for Emerald — one US revision is in play
               and the census live-verified the shipped primaries on it (CB2-HARVEST.md boot #2:
               BattleMainCB2 0x08038420 [exact], CB2_BagMenuRun 0x081AAD5C via E3, CB2_UpdateParty-
               Menu 0x081B01B0 via E2 — all read from the LIVE game). All 13 alternates = 0. */
            0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,
            0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,
            0x00000000u,
            /* phase 22.0 census promotion — EM cb2 screen classes. EVERY value live-read from the
               running game this census (gs-logger gdb channel) and resolved [exact] on
               pokeemerald.sym; per-screen provenance rows in CB2-HARVEST.md §Emerald.
               TITLE class: MainCB2_Intro (intro.c) / MainCB2 (title_screen.c) / CB2_MainMenu
               (also the Mystery-Gift shell, J12). */
            { 0x0816CC00u, 0x080AAB2Cu, 0x0802F6B0u },
            /* FULLUI class: option MainCB2, CB2_TrainerCard, CB2_FlyMap, CB2_FrontierPass,
               summary MainCB2, CB2_BerryTagScreen, CB2_Pokedex (whole dex), CB2_Pokenav (all
               sub-apps), CB2_PokeblockMenu, CB2_UsePokeblockMenu, CB2_PokeblockFeed,
               MCB2_FieldUpdateRegionMap (the RUN loop — catalog correction), CB2_WallClock,
               CB2_NamingScreen, CB2_PokeStorage, CB2_SlotMachine. */
            { 0x080BA4B0u, 0x080C2710u, 0x081248D4u, 0x080C5438u, 0x081BFAB4u, 0x08177C54u,
              0x080BB774u, 0x081C7400u, 0x0813591Cu, 0x0816631Cu, 0x08179B68u, 0x08170274u,
              0x08134C9Cu, 0x080E4F58u, 0x080C7D54u, 0x0812A670u },
            /* phase 22.1 keyboard+lists anchors (EM, one US revision — all alternates 0). Every
               value re-read from pokeemerald.sym this session (scratchpad syms/):
                 080e4f58 CB2_NamingScreen [census live-verified], 02039f94 sNamingScreen,
                 080e0ac8 Task_BuyMenu, 080e0d88 Task_BuyHowManyDialogueHandleInput,
                 0816c30c ItemStorage_ProcessInput, 081ae458 ListMenuDummyTask,
                 0203ce58 gBagPosition (+5 = pocket), 080bb7d4 Task_HandlePokedexInput,
                 02039b4c sPokedexView. Slots: shop data[7] (shop.c:412), pc data[5]
                 (player_pc.c:391). All ROM anchors sym-derived / verify-in-emulator except the
                 census-proven namingCb. */
            0x080E4F58u, 0x00000000u, 0x02039F94u,
            0x080E0AC8u, 0x00000000u, 0x080E0D88u, 0x00000000u,
            0x0816C30Cu, 0x00000000u, 0x081AE458u, 0x00000000u,
            0x0203CE5Du, 0x080BB7D4u, 0x02039B4Cu, 7, 5,
            /* phase 22.2 GRID family (SPEC-family-grid §1.1) — all re-read from the local
               pokeemerald.sym copy this session:
                 080c7d54 CB2_PokeStorage [census live-harvested exact],
                 02039d08 sStorage (+4 sInPartyMenu 02039d0c, +5 sCurrentBoxOption 02039d0d),
                 02039d78 sCursorArea (+1 sCursorPosition 02039d79, +2 sIsMonBeingMoved 02039d7a,
                 +3 sMovingMonOrigBoxId 02039d7b, +4 sMovingMonOrigBoxPos 02039d7c),
                 03005d94 gPokemonStoragePtr. Sym-derived / verify-in-emulator except storageCb. */
            0x080C7D54u, 0x00000000u, 0x02039D08u, 0x02039D78u, 0x03005D94u },
  // BPRE ROM anchors: the PRIMARIES below are FR rev0 (correct for a rev0 cart); the REV1 values —
  // the user's cart — live in the phase-22.0 ALTERNATE block at the end of the row. newKeys was
  // 0x0303011E (a digit transposition, RS-REV2-VERIFICATION.md §7): gMain 0x030030F0
  // (pokefirered.sym:745, rev0 AND rev1) + 0x2E = 0x0300311E. Confirmed unused by any code
  // (gamestate.h documents it as "unused — we inject via the returned key mask"); fixed WITH its
  // own citation + suite pin (test_profiles TEST 10), never silently.
  { "BPRE", 0x03005008u, 0x02022B4Cu, 0x02023FF8u, 0x02023FFCu, 0x02023BE4u, 0x02022976u,
            0x0203B0A0u, 0x02024029u, 0x030030F4u, 0x0811EBA0u, 0x0811EBD0u, 0x0300311Eu,
            0x03004FE0u, 0x0802E674u, 0x03004FF4u, 0x02023BD6u, 0x02023BCCu, 0x02023D70u, 0x02023BC4u, 0x03005040u,
            0x020370F0u, 0x0806F280u, 0x0203ADE4u, 0x020204B4u, 0x020370F4u, 0x03005090u, 0x08107EE0u, 0x08108F0Cu, 0x0203AD01u,
            0x0811FB28u, 0x0809CE54u, 0x0809CC98u, 0x08122C5Cu, 0x0806F1F0u, 0x00000000u,
            0x08011100u, 0x02036E38u,
            0x0203709Cu, 0x080981ACu, 0x03005050u,
            /* link diag (FRLG): gLinkStatus gLinkErrorOccurred sLinkErrorBuffer gRemoteLinkPlayersNotReceived */
            0x03003F20u, 0x03003EACu, 0x02022854u, 0x03003EB8u,
            /* D2 vblankCtr (FR): gMain.vblankCounter1 = gMain+0x20 = mainCb2(0x030030F4)-4+0x20;
               derived-from-verified-anchors, verify-on-hw-pending (user's FR = rev1; gMain is IWRAM
               and mainCb2 matched rev0/rev1 — SPEC D2.1/Open Q3).
               *** NOTE (SPEC-data D1.5.2, phase-13 follow-up / Open Q1): on FR/LG gMain+0x20 is a
               POINTER (`u32 *vblankCounter1`, pokefirered include/main.h:26), NULL unless a caller
               armed it — so this column reads 0 on the user's FireRed and the D2 Tier-B hang watch
               is disarmed there. REPORTED, NOT SILENTLY CHANGED (house rule). Presence uses hbCtr
               below instead; fixing D2/D3 needs its own hardware evidence. */
            0x03003110u,
            /* phase 15 presence (SPEC-data D1.7): FR values read from pokefirered.sym (rev0 AND
               rev1 agree): gSaveBlock2Ptr 0x0300500C (:810), gSpriteCoordOffsetX 0x02021BC8 (:23;
               Y = +2, :24), gMain.vblankCounter2 = gMain(0x030030F0, :745) + 0x24. */
            0x0300500Cu, 0x02021BC8u, 0x03003114u,
            /* phase 18 mapHeaderPath (SPEC-door T4.1): gMapHeader 0x02036DFC, read from
               pokefirered.sym AND pokefirered_rev1.sym (identical) — the user's FR is rev1.
               VERIFIED-SYM. NOT gObjectEvents-0x38 (that Emerald derivation is wrong here). */
            0x02036DFCu,
            /* sbDirect: FireRed has gSaveBlock1Ptr/gSaveBlock2Ptr -> pointers (P3.2.3). */
            0,
            /* phase 20 peer sprite (SPEC S1.3): read from pokefirered.sym AND pokefirered_rev1.sym
               (IDENTICAL — the user's FR is rev1). VERIFIED-SYM, not Emerald-derived.
                 0202063c g 00001144 gSprites
                 020371f8 g 00000400 gPlttBufferUnfaded
                 02037078 g 00000020 gPlayerAvatar  (FR's struct is 0x20, not Emerald's 0x24; the
                                                     two fields we read, flags @+0x00 and spriteId
                                                     @+0x04, are identical — FR diverges only after
                                                     +0x08, which we never touch) */
            0x0202063Cu, 0x020371F8u, 0x02037078u,
            /* phase 22.0 REV1 ALTERNATES — the fix for the census's headline finding: the user's
               FR cart is rev1 and rev1 MOVED every ROM anchor above, so battle/party/bag/menu
               detection was silently dead on it (VISITED-firered.md ⚠ table). Six values are
               live-verified [exact] census 2026-08-14 (read from the RUNNING rev1 game, resolved
               on pokefirered_rev1.sym — CB2-HARVEST.md FR pass + drift table :97-100):
                 BattleMainCB2              0x08011114
                 CB2_UpdatePartyMenu        0x0811EC18
                 CB2_BagMenuRun             0x08107F58
                 Task_HandleChooseMonInput  0x0811FBA0
                 Task_StartMenuHandleInput  0x0806F204
                 Task_HandleSelectionMenuInput 0x08122CD4
               The remaining seven are rev1 sym-derived / verify-in-emulator (the same
               pokefirered_rev1.sym that resolved every live read [exact]; RS-REV2-VERIFICATION.md
               §6 cross-validation lists them): CB2_InitPartyMenu 0x0811EC48, HandleInputChoose-
               Target 0x0802E688, StartCB_HandleInput 0x0806F294, Task_BagMenu_HandleInput
               0x08108F84, Task_YesNoMenu_HandleInput 0x0809CE68, Task_MultichoiceMenu_HandleInput
               0x0809CCAC, Task_MapNamePopup 0x080981C0. Order: battleMainCbAlt, cb2UpdPartyAlt,
               cb2InitPartyAlt, chooseTargetAlt, startCbInputAlt, cb2BagRunAlt, bagHandlerAlt,
               partyTaskAlt, yesNoTaskAlt, multiTaskAlt, selMenuTaskAlt, startMenuTaskAlt,
               mapNameTaskAlt. */
            0x08011114u, 0x0811EC18u, 0x0811EC48u, 0x0802E688u, 0x0806F294u,
            0x08107F58u, 0x08108F84u, 0x0811FBA0u, 0x0809CE68u, 0x0809CCACu,
            0x08122CD4u, 0x0806F204u, 0x080981C0u,
            /* phase 22.0 census promotion — FR cb2 screen classes, ALL live-read [exact] on the
               user's own rev1 cart this census (CB2-HARVEST.md §FireRed rev1 — a rev0 cart's
               values would differ; these serve the cart actually in the user's hands).
               TITLE: CB2_Intro, CB2_InitCopyrightScreenAfterTitleScreen, CB2_TitleScreenRun,
               CB2_MainMenu, CB2_NewGameScene (Oak speech). */
            { 0x080EC9E8u, 0x080EC878u, 0x08078BB0u, 0x0800C2E8u, 0x0812EB88u },
            /* FULLUI: CB2_InitOptionMenu (FR's RUN loop), CB2_TrainerCard, CB2_RegionMap (town
               AND fly map — one loop), CB2_RunPokemonSummaryScreen, CB2_BerryPouchIdle, TM-case
               CB2_Idle (catalog gap #4 correction), CB2_PSA, CB2_PokedexScreen (whole dex),
               TeachyTvCallback, MainCB2_FameCheckerMain, CB2_PokeStorage, CB2_NamingScreen,
               CB2_HofIdle, CB2_BuyMenu, CB2_RunSlotMachine, CB2_LoadMap2 (map-load transition —
               classifying it stops walk-key leaks BETWEEN overworld sessions). */
            { 0x08088370u, 0x08089084u, 0x080C08C8u, 0x08137F60u, 0x0813CE78u, 0x081318DCu,
              0x0811C774u, 0x0810254Cu, 0x0815AC0Cu, 0x0812C40Cu, 0x0808CDD8u, 0x0809FB84u,
              0x080F1E38u, 0x0809ADF8u, 0x0813F9C4u, 0x08056760u },
            /* phase 22.1 keyboard+lists anchors (FR: primaries rev0, alternates rev1 — the BPRE
               row convention). Re-read from pokefirered.sym / pokefirered_rev1.sym this session:
                 CB2_NamingScreen  0809fb70 / 0809fb84   (rev1 also census-live in cb2FullUi)
                 sNamingScreen     0203998c (rev-identical RAM)
                 Task_BuyMenu      0809bbc0 / 0809bbd4
                 Task_BuyHowManyDialogueHandleInput 0809bd8c / 0809bda0
                 Task_ItemPcMain   0810dea0 / 0810df18
                 ListMenuDummyTask 08106ecc / 08106f44
               bagPocket 0 (FRLG switches pockets by the on-screen arrow pair — no delta read
               needed); dexTask/dexView 0 (FR's dex is a different module — the P-D probe decides
               its tier, SPEC-family-lists L23). Slots: shop data[7] (FR shop.c:35), item_pc
               data[0] (FR item_pc.c:350). Sym-derived / verify-in-emulator. */
            0x0809FB70u, 0x0809FB84u, 0x0203998Cu,
            0x0809BBC0u, 0x0809BBD4u, 0x0809BD8Cu, 0x0809BDA0u,
            0x0810DEA0u, 0x0810DF18u, 0x08106ECCu, 0x08106F44u,
            0x00000000u, 0x00000000u, 0x00000000u, 7, 0,
            /* phase 22.2 GRID: all 0 — FRLG storage is a DIFFERENT module (pokefirered
               pokemon_storage_system) whose statics were not re-derived this slice. NAMED
               degradation: FR boxes stay GCTX_FULLUI (taps dead there, no leak). */
            0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u },
  // BPGE ROM anchors — REPLACED phase 22.0 (they were FireRed-rev0 values, wrong for EVERY
  // LeafGreen revision; battle/party/bag/menu detection was silently dead on LG). PRIMARIES are
  // now LG **rev1** — the user's cart is rev 1.1 — re-derived field-by-field from
  // pokeleafgreen_rev1.sym (RS-REV2-VERIFICATION.md §6, per-symbol line citations there).
  // The LG **rev0** values sit in the ALTERNATE block at the end of the row, so both LG
  // revisions detect. RAM columns were already correct (LG rev1 == LG rev0 == FR for every RAM
  // field — §6a). newKeys also carries the §7 transposition fix (0x0303011E -> gMain 0x030030F0
  // + 0x2E = 0x0300311E; unused field, fixed with citation + suite pin, never silently).
  //
  // LANE-B LIVE PROMOTION (2026-08-14, LANE-B-LG.md — the delta pass this row was waiting for):
  // 10 of the 13 rev1 primaries are now LIVE-VERIFIED [exact] on the RUNNING LG rev1.1 fixture
  // (instance b, gs-ring reads resolved on pokeleafgreen_rev1.sym):
  //   cb2UpdParty 0x0811EBF0, cb2InitParty 0x0811EC20 (ctx=party fired, first time ever on LG),
  //   cb2BagRun 0x08107F30 + bagHandler 0x08108F5C (ctx=bag fired), partyTask 0x0811FB78,
  //   multiTask 0x0809CC80 (nurse yes/no -> ctx=fmenu), selMenuTask 0x08122CAC,
  //   startMenuTask 0x0806F204 (ctx=fmenu fired), battleMainCb 0x08011114 (trainer battle;
  //   full b.oth -> b.act -> b.move gate sequence observed), mapNameTask 0x08098194.
  // The 3 honest non-verdicts stay flagged AS-IS (LANE-B-LG.md table #3/#4/#8):
  //   chooseTarget 0x0802E688  — NOT EXERCISED (needs a double battle's target-choose screen);
  //                              sym-verified [exact] on the map only.
  //   startCbInput 0x0806F294  — UNUSED-BY-CODE (START detection is task-only since the
  //                              callback-compare false-positive fix below); sym-verified value.
  //   yesNoTask   0x0809CE3C   — map-correct, LIVE-UNREACHED: FRLG routes every common yes/no
  //                              through OTHER handlers (save prompt stays under the start-menu
  //                              task; script yes/no = multiTask; bag-toss + mart-buy confirms =
  //                              Task_CallYesOrNoCallback — carried as yesNoTaskAlt below).
  { "BPGE", 0x03005008u, 0x02022B4Cu, 0x02023FF8u, 0x02023FFCu, 0x02023BE4u, 0x02022976u,
            0x0203B0A0u, 0x02024029u, 0x030030F4u, 0x0811EBF0u, 0x0811EC20u, 0x0300311Eu,
            0x03004FE0u, 0x0802E688u, 0x03004FF4u, 0x02023BD6u, 0x02023BCCu, 0x02023D70u, 0x02023BC4u, 0x03005040u,
            0x020370F0u, 0x0806F294u, 0x0203ADE4u, 0x020204B4u, 0x020370F4u, 0x03005090u, 0x08107F30u, 0x08108F5Cu, 0x0203AD01u,
            0x0811FB78u, 0x0809CE3Cu, 0x0809CC80u, 0x08122CACu, 0x0806F204u, 0x00000000u,
            0x08011114u, 0x02036E38u,
            0x0203709Cu, 0x08098194u, 0x03005050u,
            /* link diag (FRLG): gLinkStatus gLinkErrorOccurred sLinkErrorBuffer gRemoteLinkPlayersNotReceived */
            0x03003F20u, 0x03003EACu, 0x02022854u, 0x03003EB8u,
            /* D2 vblankCtr (LG): FR-derived per the house rule (BPGE addrs unverified; HANDOFF Gotchas).
               Same pointer-not-counter defect as BPRE above (D1.5.2). */
            0x03003110u,
            /* phase 15 presence (SPEC-data D1.7): unlike the rest of this row these three were read
               from LEAFGREEN'S OWN symbol map, not FR-derived — pokeleafgreen.sym:810 / :23-24 / :745
               (rev0 and rev1 agree): gSaveBlock2Ptr 0x0300500C, gSpriteCoordOffsetX 0x02021BC8,
               gMain.vblankCounter2 = gMain(0x030030F0) + 0x24. VERIFIED-SYM. */
            0x0300500Cu, 0x02021BC8u, 0x03003114u,
            /* phase 18 mapHeaderPath (SPEC-door T4.1): read from LEAFGREEN'S OWN maps —
               pokeleafgreen.sym and pokeleafgreen_rev1.sym (both on pret/pokefirered's
               `symbols` branch; there is no separate pokeleafgreen repo). Both give
               gMapHeader 0x02036DFC. VERIFIED-SYM, not FR-derived. */
            0x02036DFCu,
            /* sbDirect: LeafGreen has gSaveBlock1Ptr/gSaveBlock2Ptr -> pointers (P3.2.3). */
            0,
            /* phase 20 peer sprite (SPEC S1.3): read from LEAFGREEN'S OWN maps —
               pokeleafgreen.sym AND pokeleafgreen_rev1.sym (both on pret/pokefirered's `symbols`
               branch; both agree). VERIFIED-SYM, NOT FR-derived.
                 0202063c gSprites / 020371f8 gPlttBufferUnfaded / 02037078 gPlayerAvatar */
            0x0202063Cu, 0x020371F8u, 0x02037078u,
            /* phase 22.0 LG REV0 ALTERNATES (the primaries above are LG rev1 = the user's cart).
               All from pokeleafgreen.sym via RS-REV2-VERIFICATION.md §6's "LG rev0" column —
               sym-derived / verify-in-emulator, same order as the BPRE block:
               battleMainCbAlt, cb2UpdPartyAlt, cb2InitPartyAlt, chooseTargetAlt, startCbInputAlt,
               cb2BagRunAlt, bagHandlerAlt, partyTaskAlt, yesNoTaskAlt, multiTaskAlt,
               selMenuTaskAlt, startMenuTaskAlt, mapNameTaskAlt.
               *** yesNoTaskAlt DEVIATES from the rev0 convention (lane-B promotion, LANE-B-LG.md
               #8): it carries Task_CallYesOrNoCallback 0x080BF548 (LG rev1, LIVE-VERIFIED [exact]
               TWICE — bag-toss confirm AND mart buy confirm), because FRLG BYPASSES
               Task_YesNoMenu_HandleInput on every common yes/no flow — the rev1 primary above is
               loaded-but-never-fired, so without this alternate both money-and-item confirm
               screens read as `field` and taps leak walk keys. The displaced rev0 value
               (Task_YesNoMenu_HandleInput rev0 0x0809CE28) loses nothing: the bypass is a
               code-structure property of FRLG, not of a revision. Compare-only => fail-safe. */
            0x08011100u, 0x0811EB78u, 0x0811EBA8u, 0x0802E674u, 0x0806F280u,
            0x08107EB8u, 0x08108EE4u, 0x0811FB00u, 0x080BF548u, 0x0809CC6Cu,
            0x08122C34u, 0x0806F1F0u, 0x08098180u,
            /* phase 22.0 cb2 screen classes: still EMPTY. The lane-B LG boot DID spot-harvest
               live [exact] TITLE/FULLUI candidates (LANE-B-LG.md census table: CB2_Intro
               0x080EC9C0, CB2_TitleScreenRun 0x08078BB0, CB2_MainMenu 0x0800C2E8, options/dex/
               trainer-card/buy-menu loops) — but their promotion is its OWN slice, not this
               fold-in; the lists stay all-zero = the NAMED degradation (LG's unlisted screens
               keep the GCTX_OVERWORLD fall-through). FRLG cb2 ROM addresses differ per build,
               so FR's values would be WRONG here regardless. */
            { 0x00000000u },
            { 0x00000000u },
            /* phase 22.1 keyboard+lists anchors (LG: primaries rev1 = the user's 1.1 cart,
               alternates rev0 — the BPGE row convention since 305242f). Re-read from
               pokeleafgreen_rev1.sym / pokeleafgreen.sym this session:
                 CB2_NamingScreen  0809fb58 / 0809fb44
                 sNamingScreen     0203998c (rev-identical RAM, same as FR)
                 Task_BuyMenu      0809bba8 / 0809bb94
                 Task_BuyHowManyDialogueHandleInput 0809bd74 / 0809bd60
                 Task_ItemPcMain   0810def0 / 0810de78
                 ListMenuDummyTask 08106f1c / 08106ea4
               bagPocket/dex = 0 like FR. Slots 7 / 0 (FRLG shop.c + item_pc.c are shared
               modules). Sym-derived / verify-in-emulator (the LG delta pass proves them live). */
            0x0809FB58u, 0x0809FB44u, 0x0203998Cu,
            0x0809BBA8u, 0x0809BB94u, 0x0809BD74u, 0x0809BD60u,
            0x0810DEF0u, 0x0810DE78u, 0x08106F1Cu, 0x08106EA4u,
            0x00000000u, 0x00000000u, 0x00000000u, 7, 0,
            /* phase 22.2 GRID: all 0 — same named degradation as BPRE (different FRLG module). */
            0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u },

  // ===================== Ruby / Sapphire (SPEC-coop §P3) =====================================
  // Every RAM value below is VERIFIED-SYM against pret's byte-matched `symbols` branch, all FOUR
  // maps downloaded and diffed on 2026-08-13: pokeruby.sym, pokesapphire.sym, pokeruby_rev1.sym,
  // pokesapphire_rev1.sym. Whole-map comparison of every 0x02*/0x03* symbol: **727 symbols, ZERO
  // differences across all four maps** — Ruby == Sapphire and rev0 == rev1 in RAM. That is what
  // licenses ONE row body for two titles and two revisions (P3.3.2), and it is also the rule that
  // bans ROM addresses here: the 0x08 space differs between all four (Task_StartMenu alone is
  // 0x08071254 / 0x08071258 / 0x08071274 / 0x08071278), so any 0x08... value that is not
  // identical in all four maps would be wrong for three of the four cartridges it meets.
  //
  // The struct LAYOUTS were read from pret/pokeruby master this session and are byte-identical to
  // Emerald's for every field this app touches (VERIFIED-SRC):
  //   SaveBlock1  pos +0x00, location(WarpData: s8 mapGroup +0x04, s8 mapNum +0x05)  global.h:668-681
  //   SaveBlock2  playerName[8] +0x00, playerGender +0x08, playerTrainerId[4] +0x0A  global.h:841-847
  //   ObjectEvent active:1 @+0x00 bit0, currentCoords +0x10, facingDirection low nibble +0x18,
  //               previousElevation high nibble +0x0B, stride 0x24 (sym size 0x240 = 16*0x24)
  //   Main        callback1 +0x00, callback2 +0x04, vblankCounter1 +0x20, vblankCounter2 +0x24,
  //               newKeys +0x2E (main.h:10-44) — and unlike FR/LG, RS's vblankCounter1 is a real u32
  //   FieldCamera curMovementOffsetX +0x10 / Y +0x14, size 0x18 (field_camera.h:4-12; sym size 0x18)
  //   MapHeader   mapLayout +0x00 ... ; Tileset metatileAttributes +0x10 as const u16* (the RSE
  //               shape fieldpath.c assumes); MAPGRID_* masks identical -> fp_engine()'s code[2]
  //               test ('V'/'P' -> FP_ENG_RSE) is correct for AXVE/AXPE, and pokeruby's
  //               metatile_behaviors.h numbering matches the RSE table row for row (0x69
  //               MB_ANIMATED_DOOR, 0x60-0x6D block, 0x0E/0x0F/0x1B/0x1C/0x29) — re-read, not assumed.
  //
  // THE ONE STRUCTURAL DIFFERENCE is sbDirect=1 (last column): RS has no gSaveBlock1Ptr at all.
  //
  // Every 0 below is a NAMED degradation, never a guess (P3.4):
  //  * ROM function pointers (cb2UpdParty, cb2InitParty, chooseTarget, startCbInput, cb2BagRun,
  //    bagHandler, partyTask, yesNoTask, multiTask, selMenuTask, startMenuTask, mapNameTask) -> 0.
  //    Rev/title-sensitive per the rule above, and several (Task_HandleChooseMonInput, Task_Bag*,
  //    MapNamePopup*, HandleInputChooseTarget) DO NOT EXIST in the RS decomp — pokeruby still has
  //    those regions as sub_XXXXXXXX. Degradation: task_active() returns false for a 0 handler
  //    (gamestate.c:85) and find_bag_list_task finds nothing, so RS reports GCTX_OVERWORLD where
  //    Emerald would report GCTX_FIELDMENU/GCTX_PARTY/GCTX_BAG. CONCRETELY: smart touch's
  //    menu/bag/party/target features do not work on Ruby/Sapphire, and a peer avatar can be drawn
  //    while the RS player has the START menu open. Both fail-safe (the overworld still WALKS).
  //  * partyMenu, multiCursor, gWindowsBase, linkErrBuf, linkNotRecv -> 0: gPartyMenu,
  //    gMultiUsePlayerCursor, gWindows, sLinkErrorBuffer and gRemoteLinkPlayersNotReceived have NO
  //    symbol in any RS map (checked, all four). Two of the five link-diagnostic log columns are
  //    blank for RS; the rest are already guarded `p->x ? read : 0` (gamestate.c:131-135).
  //  * sMenuBase -> 0 (DEVIATION from SPEC-coop P3.3.3, which listed sMenu 0x020388B8): the RS
  //    symbol's SIZE is 4 bytes (pokeruby.sym:312) while touch.c:510-524 reads +1/+2/+4/+5/+8 —
  //    i.e. Emerald's `struct Menu` layout is NOT proven for RS, and a wrong-layout read is exactly
  //    what the house rule bans. Costs nothing: gWindowsBase is 0 and no menu task resolves, so the
  //    GCTX_FIELDMENU path is unreachable on RS regardless.
  //  * mapHeader -> 0 ON PURPOSE (not "unknown"): main.c:885 gates the phase-14 HD-2D metatile
  //    depth path on `!p->mapHeader`, and switching an untested render path on for a new game is a
  //    render change this phase is forbidden to make. mapHeaderPath (last-but-one column) carries
  //    the same gMapHeader for warp classification, which is the field this phase needs.
  //  * spriteCoordOff -> 0: gSpriteCoordOffsetX 0x030024D0 (:535) and ...Y 0x030027E0 (:540) are
  //    0x310 APART, not +2, and the field's whole contract is "Y = this + 2" (gamestate.h). It is
  //    diagnostic-only (the shipped sub-tile source is fieldCamera+0x10/+0x14, which RS has and
  //    which is verified above), so 0 costs nothing and a value here would read a wrong address.
  //
  // battleMainCb 0x0800F808 (BattleMainCB2, pokeruby.sym:1378) — THE ONE ROM ADDRESS SHIPPED in
  // the RAM body, and only because all four maps give the identical value. It is load-bearing:
  // without it inBattle is always false, RS always reports GCTX_OVERWORLD, and a peer avatar
  // would be painted over battle screens. The promotion run HAPPENED: lane B (LANE-B-RS.md §1,
  // 2026-08-14) entered a wild battle on the live Ruby fixture and cb2 flipped to 0x0800F808
  // [exact] with ctx leaving `field` for the first time (b.oth -> b.act -> b.move -> field) —
  // LIVE-VERIFIED in the emulator; the hardware run is still owed per the done-gate.
  // partyCount 0x03004350 (IWRAM, symbol size 4, read as a u8) stays verify-on-hw-pending: only
  // read inside the partyTask branch, which RS never enters (partyTask = 0) — inert today.
  //
  // EXECUTION STATUS (updated by the lane-B fold-in — this replaces the old "no RS ROM exists
  // on this machine" caveat): lane B BOOTED AND LIVE-VERIFIED both titles (LANE-B-RS.md,
  // 2026-08-14, instance b, resolved on pokeruby_rev2.sym / pokesapphire_rev2.sym = the correct
  // maps for the user's rev-2 carts). Ruby: profile row matched (p1=AXVE), mainCb2/sb1ptr(direct)
  // /mapObjects/gTasksBase all VERIFIED against the running game (closed-loop D4 walks, door warp
  // + route connection, 7 task fps resolving [exact]). Sapphire: AXPE matched in BOTH seats
  // (co-op gameB + solo primary). The P3.6 cheap proof ran as the RS CO-OP boot: the phase-18
  // universe gate PASSED live (both titles -> PRES_GAME_HOENN_RS, pairReason=0, CO-OP chip
  // rendered, record exchange symmetric ±(33,30)). Emulator-verified; hardware sign-off owed.
  //
  // AXVE and AXPE are TWO LITERAL ROWS sharing ONE RAM BODY (RS_PROFILE_BODY_RAM /
  // RS_PROFILE_BODY_TAIL), never one prefix match: the 4-char code is the app's only game
  // identity and profile_for compares all four bytes (P3.5.1). The ONE licensed per-title
  // difference is the cb2Title/cb2FullUi class lists between the two macro halves — lane B
  // proved Ruby and Sapphire ROM addresses DRIFT (title MainCB2 0x0807C474 vs 0x0807C478,
  // CB2_Overworld +4, CB2_PartyMenuMain drifts; LANE-B-RS.md §2 headline), so those lists carry
  // per-title live-read values and MUST NEVER be copied across titles. The phase-18 "727 RAM
  // symbols identical" proof was RAM-ONLY and does not extend to ROM.
  //   ***  IF YOU EDIT THE SHARED MACROS BELOW, BOTH ROWS CHANGE — that is the point. IF YOU
  //        EDIT A PER-TITLE cb2 LIST, NEVER COPY A VALUE TO THE OTHER TITLE (the drift).  ***
  #define RS_PROFILE_BODY_RAM \
            /* sb1ptr(DIRECT) battleFlags  actionCur    moveCur      battleMons   bg0y        */ \
            0x02025734u, 0x020239F8u, 0x02024E60u, 0x02024E64u, 0x02024A80u, 0x030042A0u,       \
            /* partyMenu=0   partyCount   mainCb2      cb2Upd=0     cb2Init=0    newKeys     */ \
            0x00000000u, 0x03004350u, 0x03001774u, 0x00000000u, 0x00000000u, 0x0300179Eu,       \
            /* ctrlFuncs     chooseTgt=0  multiCur=0   battlerPos   battlersCnt  absentFlg      activeBat    mapLayout */ \
            0x03004330u, 0x00000000u, 0x00000000u, 0x02024A72u, 0x02024A68u, 0x02024C0Cu, 0x02024A60u, 0x03004870u, \
            /* startCb       startCbIn=0  sMenuBase=0  gWindows=0   startCursor  gTasks         cb2BagRun=0  bagHandler=0 bagOpen=0 */ \
            0x03004AE8u, 0x00000000u, 0x00000000u, 0x00000000u, 0x0202E8FCu, 0x03004B20u, 0x00000000u, 0x00000000u, 0x00000000u, \
            /* partyTask=0   yesNoTask=0  multiTask=0  selMenu=0    startMenu=0  mapHeader=0 */ \
            0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,       \
            /* battleMainCb (the one ROM addr)  mapObjects */                                   \
            0x0800F808u, 0x030048A0u,                                                           \
            /* fieldMsgMode  mapNameTask=0 fieldCamera */                                       \
            0x030005A8u, 0x00000000u, 0x03004880u,                                              \
            /* link diag: gLinkStatus gLinkErrorOccurred sLinkErrorBuffer=0 gRemoteLinkPlayersNotReceived=0 */ \
            0x03002A60u, 0x0300295Cu, 0x00000000u, 0x00000000u,                                 \
            /* vblankCtr = gMain+0x20 — a REAL u32 on RS (not FR/LG's NULL pointer), so the D2
               Tier-B hang watch actually works here */                                         \
            0x03001790u,                                                                        \
            /* sb2ptr(DIRECT)  spriteCoordOff=0  hbCtr = gMain+0x24 */                          \
            0x02024EA4u, 0x00000000u, 0x03001794u,                                              \
            /* mapHeaderPath = gMapHeader (warp classification; NOT the HD-2D `mapHeader`) */    \
            0x0202E828u,                                                                        \
            /* sbDirect = 1: sb1ptr/sb2ptr above ARE the structs, not pointers (P3.2) */         \
            1,                                                                                  \
            /* phase 20 peer sprite (SPEC S1.3): gSprites / gPlttBufferUnfaded / gPlayerAvatar,
               read from ALL FOUR RS maps on 2026-08-13 — pokeruby.sym, pokesapphire.sym,
               pokeruby_rev1.sym, pokesapphire_rev1.sym — which AGREE 4/4, exactly like the 727
               RAM symbols the phase-18 block above diffed. RAM only, so the ROM-address ban that
               governs this row is not engaged. gSprites' size is 0x1144 here too, i.e. the same
               65 * 0x44 `struct Sprite` Emerald has.
                 02020004 g 00001144 gSprites
                 0202eac8 g 00000400 gPlttBufferUnfaded
                 0202e858 g 00000024 gPlayerAvatar
               *** VERIFIED-SYM / VERIFY-ON-HW for the sprite columns themselves: the lane-B
               co-op boot (LANE-B-RS.md §2) ran both titles and the presence pair gate passed
               live (pairReason=0, CO-OP chip rendered), but g_presDiag.sprReason==0 was not
               specifically read for these three columns — that named check is still owed. *** */ \
            0x02020004u, 0x0202EAC8u, 0x0202E858u,                                               \
            /* phase 22.0 rev-alternate ROM anchors: ALL 13 = 0. The ROM-address ban that governs
               this row (rev0/1/2 ROM symbols differ above 0x0803FBBC; RS-REV2-VERIFICATION.md
               §2.3) applies to alternates exactly as it does to primaries — and the primaries
               they would shadow are themselves 0 here. Explicit zeros, not C zero-fill (P3.5.2). */ \
            0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,        \
            0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,        \
            0x00000000u
  /* The cb2Title/cb2FullUi lists sit BETWEEN the two macro halves and are written PER ROW —
     the lane-B fold-in (LANE-B-RS.md, 2026-08-14). See the drift rule in the block comment. */
  #define RS_PROFILE_BODY_TAIL \
            /* phase 22.1 keyboard+lists anchors: ALL 0 — the ROM-address ban again (every one of
               these is a ROM fn or a module-static EWRAM ptr with no RS verification), plus the
               RS naming screen is an OLDER module the keyboard spec explicitly defers
               (SPEC-family-keyboard §Scope). Named degradation: GCTX_NAMING/GCTX_LIST never fire
               on RS; walk/battle touch is unchanged. Explicit zeros, not C zero-fill (P3.5.2). */ \
            0x00000000u, 0x00000000u, 0x00000000u,                                               \
            0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,                                  \
            0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,                                  \
            0x00000000u, 0x00000000u, 0x00000000u, 0, 0,                                         \
            /* phase 22.2 GRID: all 0 — the RS ROM/statics ban again (P3.5.2 explicit zeros). */  \
            0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u
  // Pokemon Ruby (US; the values below were LIVE-READ on the rev-2 fixture ROM = the user's
  // cart, and rev1 == rev2 are byte-identical maps — RS-REV2-VERIFICATION.md §5's promotion
  // rule; a rev0 cart's drifted screens simply keep the fall-through, compare-only fail-safe).
  { "AXVE", RS_PROFILE_BODY_RAM,
            /* lane-B RS promotion — RUBY's own cb2 screen classes, ALL live-read [exact] on the
               running game, resolved on pokeruby_rev2.sym (LANE-B-RS.md §1 harvest table):
               TITLE: MainCB2_Intro 0x0813B7B8 (GF intro), MainCB2 0x0807C474 (title screen),
                      CB2_MainMenu 0x080096C4. */
            { 0x0813B7B8u, 0x0807C474u, 0x080096C4u },
            /* FULLUI: CB2_PartyMenuMain 0x0806AEFC (party menu — RS has no partyTask symbol, so
               the cb2 class is the only party detection RS gets), bag run loop 0x080A3138
               (rev-drifted `sub_80A3118`+0x20 name; the address is the live run-loop entry). */
            { 0x0806AEFCu, 0x080A3138u },
            RS_PROFILE_BODY_TAIL },
  // Pokemon Sapphire (US; same promotion rule — every value below was measured on SAPPHIRE
  // itself, live [exact] on pokesapphire_rev2.sym; LANE-B-RS.md §2 drift table + §3 solo smoke).
  { "AXPE", RS_PROFILE_BODY_RAM,
            /* TITLE: MainCB2_Intro 0x0813B7B8 (same as Ruby — measured, not copied), MainCB2
               0x0807C478 (the +4 DRIFT vs Ruby — the headline), CB2_MainMenu 0x080096C4. */
            { 0x0813B7B8u, 0x0807C478u, 0x080096C4u },
            /* FULLUI: bag run loop 0x080A3138 (`sub_80A3118` [exact] on both RS maps). Sapphire's
               CB2_PartyMenuMain was NOT measured (Ruby's 0x0806AEFC resolves inside
               Task_ResetRtcScreen on the sapphire map — copying it across the drift is exactly
               the BPGE failure mode) -> NAMED degradation: the Sapphire party menu keeps the
               GCTX_OVERWORLD fall-through until a Sapphire party visit measures its own value. */
            { 0x080A3138u },
            RS_PROFILE_BODY_TAIL },
  #undef RS_PROFILE_BODY_RAM
  #undef RS_PROFILE_BODY_TAIL
};

const GameProfile* profile_for(GbaCore* c) {
	if (!c) return NULL;
	char code[5]; gbacore_game_code(c, code);
	for (unsigned i = 0; i < sizeof PROFILES / sizeof PROFILES[0]; i++)
		if (!strncmp(code, PROFILES[i].code, 4)) return &PROFILES[i];
	return NULL;
}

// True if any active gTasks entry's func == `handler` (Thumb-masked). Robust menu detection.
static bool task_active(GbaCore* c, const GameProfile* p, uint32_t handler) {
	if (!handler) return false;
	for (int t = 0; t < 16; t++) {
		uint32_t task = p->gTasksBase + 40u * (uint32_t)t;
		if (gbacore_read8(c, task + 4) != 0 && (gbacore_read32(c, task + 0) & ~1u) == handler) return true;
	}
	return false;
}

// Phase 22.0 dual-revision matching: a ROM anchor and its rev-ALTERNATE are both tested, because
// profile_for keys on the 4-char game code and cannot see the cart's revision byte (FRLG rev1
// moved every ROM function — the census's headline finding). A wrong-rev anchor is compare-only:
// it never fires on the other revision's RAM, so testing both is fail-safe by construction.
static bool task_active2(GbaCore* c, const GameProfile* p, uint32_t handler, uint32_t handlerAlt) {
	return task_active(c, p, handler) || task_active(c, p, handlerAlt);
}

// Find a live ListMenu behind an anchor task: scan gTasks (16 entries, 40-byte stride) for the
// active anchor handler (either revision's — phase 22.0), read the u16 listTaskId out of the
// anchor's data[slot], and return the LIST task's data base (gTasks + 40*listTaskId + 8 — where
// ListMenuInit stores the struct: template +0, scrollOffset +24, selectedRow +26). 0 if none.
// Phase 22.1 (SPEC-family-lists L1): the bag's data[0] lookup generalised to a slot argument, so
// the mart (data[7]) and the PC item list (EM data[5] / FRLG data[0]) reuse the proven path.
static uint32_t find_list_task(GbaCore* c, const GameProfile* p,
                               uint32_t handler, uint32_t handlerAlt, int slot) {
	for (int t = 0; t < 16; t++) {
		uint32_t task = p->gTasksBase + 40u * (uint32_t)t;
		if (gbacore_read8(c, task + 4) == 0) continue;                 // isActive
		uint32_t fn = gbacore_read32(c, task + 0) & ~1u;
		if (!((handler && fn == handler) || (handlerAlt && fn == handlerAlt))) continue;
		int16_t listId = (int16_t)gbacore_read16(c, task + 8u + 2u * (uint32_t)slot);
		if (listId < 0 || listId >= 16) return 0;
		return p->gTasksBase + 40u * (uint32_t)listId + 8u;
	}
	return 0;
}
static uint32_t find_bag_list_task(GbaCore* c, const GameProfile* p) {
	return find_list_task(c, p, p->bagHandler, p->bagHandlerAlt, 0);   // bag: data[0] = tListTaskId
}

bool game_read(GbaCore* c, const GameProfile* p, GameState* out) {
	memset(out, 0, sizeof *out);
	out->px = out->py = -1; out->actionCursor = out->moveCursor = -1; out->ctx = GCTX_NONE;
	out->partyCount = out->partyLayout = out->battlersCount = -1;
	out->mapGroup = out->mapNum = out->objX = out->objY = out->facing = -1;
	if (!c || !p) return false;
	out->valid = true;

	// SPEC-coop P3.2.4 — call site 1 of 2. Ruby/Sapphire have no gSaveBlock1Ptr, so for those
	// profiles the column IS the struct address and there is nothing to deref. The (>> 24) == 0x02
	// EWRAM sanity test is KEPT in both cases: for a direct block it is a constant-true "mapped"
	// check rather than a "loaded" check, which is honest — and the weaker signal is compensated
	// where it actually matters (the identity latch; presence_read.c ident_refresh, P3.2.5).
	uint32_t sb1 = p->sbDirect ? p->sb1ptr : gbacore_read32(c, p->sb1ptr);
	out->sb1Valid = (sb1 >> 24) == 0x02;
	if (out->sb1Valid) {
		out->px = (int16_t)gbacore_read16(c, sb1);
		out->py = (int16_t)gbacore_read16(c, sb1 + 2);
		out->mapGroup = gbacore_read8(c, sb1 + 0x04);   // SaveBlock1.location.mapGroup (WarpData)
		out->mapNum   = gbacore_read8(c, sb1 + 0x05);   // SaveBlock1.location.mapNum
	}

	// --- instrumentation (LOGGING ONLY): raw callbacks, player-avatar geo, active-task fingerprint.
	// All read-only; NOTHING below gates touch/3D/gameplay on these — they only feed the SD log. ---
	out->cb2 = gbacore_read32(c, p->mainCb2) & ~1u;            // raw gMain.callback2 = the screen fingerprint
	out->cb1 = gbacore_read32(c, p->mainCb2 - 4u) & ~1u;       // gMain.callback1 (gMain+0 = mainCb2-4)
	out->ctxResolved = true;                                   // default; cleared in the overworld fall-through
	// link-error diagnostics: WHY the game's own link layer aborted (latched in sLinkErrorBuffer even after
	// it enters CB2_PrintErrorMessage). All read-only; never gates anything. 0 addr -> 0 (game not mapped).
	out->linkStatus  = p->linkStatus  ? gbacore_read32(c, p->linkStatus)      : 0;
	out->linkErr     = p->linkErr     ? (uint8_t)gbacore_read8(c, p->linkErr) : 0;
	out->linkErrBuf0 = p->linkErrBuf  ? gbacore_read32(c, p->linkErrBuf)      : 0;
	out->linkErrBuf1 = p->linkErrBuf  ? gbacore_read32(c, p->linkErrBuf + 4u) : 0;
	out->linkNotRecv = p->linkNotRecv ? gbacore_read32(c, p->linkNotRecv)     : 0;
	for (int t = 0; t < 16 && out->nTask < 8; t++) {           // up to 8 active gTasks func ptrs (screen ID)
		uint32_t task = p->gTasksBase + 40u * (uint32_t)t;
		if (gbacore_read8(c, task + 4) == 0) continue;         // isActive
		out->taskFp[out->nTask++] = gbacore_read32(c, task + 0) & ~1u;
	}
	// Sort the fingerprint ascending so the SAME screen produces the SAME (diffable) signature on every
	// visit — gTasks SLOT order is unstable across visits, which would make one screen fingerprint differently.
	for (int i = 1; i < out->nTask; i++) {
		uint32_t v = out->taskFp[i]; int j = i - 1;
		while (j >= 0 && out->taskFp[j] > v) { out->taskFp[j + 1] = out->taskFp[j]; j--; }
		out->taskFp[j + 1] = v;
	}
	if (p->mapObjects && (gbacore_read32(c, p->mapObjects) & 1u)) {     // gObjectEvents[0] active (slot 0 = player)
		out->objX   = (int16_t)gbacore_read16(c, p->mapObjects + 0x10); // currentCoords.x (grid, +7)
		out->objY   = (int16_t)gbacore_read16(c, p->mapObjects + 0x12); // currentCoords.y
		out->facing = gbacore_read8 (c, p->mapObjects + 0x18) & 0x0F;   // facingDirection — offset verify-on-hw (FR/LG base also suspect)
	}

	// 'In battle' = the battle main loop is the active callback2. (gBattleTypeFlags is zeroed at battle
	// SETUP, not end, so it lingers into the overworld -> the old battleFlags test made the whole
	// post-battle field read as a battle dialog = tap-anywhere-A. callback2 returns to the field cleanly.)
	// Phase 22.0: EITHER revision's BattleMainCB2 counts (battleMainCbAlt = the census's FR-rev1 fix —
	// live-verified [exact]; without it every battle on the user's rev1 cart read as overworld).
	bool inBattle = (p->battleMainCb    && out->cb2 == p->battleMainCb)
	             || (p->battleMainCbAlt && out->cb2 == p->battleMainCbAlt);

	// Menu detection is TASK-BASED (scan gTasks for the menu's active input handler): robust and
	// FAIL-SAFE — a wrong/absent address just means "not detected" (the overworld still WALKS), never a
	// false-positive that blocks walking. Bag + party run in field AND battle, so they're top-level.
	{ uint32_t lb = find_bag_list_task(c, p);                    // bag (field or battle "ITEM")
	  if (lb) { out->ctx = GCTX_BAG; out->bagListTaskBase = lb; return true; } }
	// sMenu-driven popups (party SUMMARY/SWITCH popup, YES-NO, multichoice) — field OR battle. These all
	// drive sMenu.cursorPos, so GCTX_FIELDMENU's hit-test handles them. Task-detected => fail-safe.
	if (task_active2(c, p, p->selMenuTask, p->selMenuTaskAlt) || task_active2(c, p, p->yesNoTask, p->yesNoTaskAlt)
	    || task_active2(c, p, p->multiTask, p->multiTaskAlt)) {
		out->ctx = GCTX_FIELDMENU; return true;
	}
	if (task_active2(c, p, p->partyTask, p->partyTaskAlt)) {     // party slot pick (field, or battle send-out/use)
		out->ctx = GCTX_PARTY;
		uint8_t mt8 = gbacore_read8(c, p->partyMenu + PM_TYPE_OFF);
		int lay = (mt8 >> 4) & 0x03, cnt = gbacore_read8(c, p->partyCount);
		out->partyLayout = (lay <= 2) ? lay : 0;
		out->partyCount  = (cnt >= 1 && cnt <= 6) ? cnt : 6;
		return true;
	}

	if (!inBattle) {                                            // overworld
		// START menu — detect by its TASK (active only while open). The old callback compare
		// (gMenuCallback==HandleStartMenuInput) false-positived: gMenuCallback isn't cleared on close,
		// so once you opened START, the overworld read as a menu forever -> walk stuck on A (Emerald).
		if (task_active2(c, p, p->startMenuTask, p->startMenuTaskAlt)) { out->ctx = GCTX_FIELDMENU; return true; }
		// --- phase 22.1 (lane A): the KEYBOARD + LISTS families, tested BEFORE the phase-22.0
		// screen classes because they are MORE SPECIFIC (an interactive driver beats a bare
		// "full-screen UI, taps dead" classification). All task-detected or cb2-detected =>
		// fail-safe: a wrong/absent anchor just means the screen keeps its 22.0 behaviour. ---
		if (p->buyTask || p->buyTaskAlt) {                          // mart buy list (LK_BUY)
			uint32_t lb = find_list_task(c, p, p->buyTask, p->buyTaskAlt, p->buyListSlot);
			if (lb || task_active2(c, p, p->buyTask, p->buyTaskAlt)) {
				out->ctx = GCTX_LIST; out->listKind = LK_BUY; out->listBase = lb; return true;
			}
		}
		if (task_active2(c, p, p->buyQtyTask, p->buyQtyTaskAlt)) {  // "how many?" roller (LK_QTY)
			out->ctx = GCTX_LIST; out->listKind = LK_QTY; return true;
		}
		if (p->pcItemTask || p->pcItemTaskAlt) {                    // PC item storage list (LK_PCITEM)
			uint32_t lb = find_list_task(c, p, p->pcItemTask, p->pcItemTaskAlt, p->pcItemListSlot);
			if (lb || task_active2(c, p, p->pcItemTask, p->pcItemTaskAlt)) {
				out->ctx = GCTX_LIST; out->listKind = LK_PCITEM; out->listBase = lb; return true;
			}
		}
		if ((p->namingCb    && out->cb2 == p->namingCb) ||          // the naming keyboard
		    (p->namingCbAlt && out->cb2 == p->namingCbAlt)) {
			out->ctx = GCTX_NAMING; return true;
		}
		// Phase 22.2 (SPEC-family-grid G1): the PC storage boxes — cb2-matched, more specific
		// than the cb2FullUi row that also lists CB2_PokeStorage (this test runs first, so the
		// boxes become an interactive grid instead of a bare taps-dead FULLUI screen).
		if ((p->storageCb    && out->cb2 == p->storageCb) ||
		    (p->storageCbAlt && out->cb2 == p->storageCbAlt)) {
			out->ctx = GCTX_STORAGE; return true;
		}
		if (task_active(c, p, p->dexTask)) {                        // EM dex LIST (task is unique
			out->ctx = GCTX_LIST; out->listKind = LK_DEX;           //   to the list screen)
			return true;
		}
		// Phase 22.0 census promotion — POSITIVELY classify the screens whose cb2 the census
		// live-harvested ([exact], CB2-HARVEST.md), instead of letting them hide in the
		// GCTX_OVERWORLD fall-through where taps leak walk/A/START keys and the tilt/presence
		// gates (which test ctx == GCTX_OVERWORLD) stay open. Runs AFTER every task-based menu
		// check (a task match is more specific) and BEFORE the fall-through. A zero slot never
		// matches (a cb2 is never 0); ctxResolved stays true — these are positive matches.
		for (int i = 0; i < GS_N_TITLE; i++)
			if (p->cb2Title[i] && out->cb2 == p->cb2Title[i]) { out->ctx = GCTX_TITLE; return true; }
		for (int i = 0; i < GS_N_FULLUI; i++)
			if (p->cb2FullUi[i] && out->cb2 == p->cb2FullUi[i]) { out->ctx = GCTX_FULLUI; return true; }
		out->ctx = GCTX_OVERWORLD;
		out->ctxResolved = false;   // overworld OR an undetected screen that fell through here -> inspect cb2 in the log
		// On-screen field text — precise per-band kill signals for the renderer's DoF. The BG0
		// text-layer scan in main.c is the game-agnostic catch-all; these are exact backups.
		// Fail-safe: unknown address -> false (the BG0 scan still covers it).
		out->textDlg    = p->fieldMsgMode && gbacore_read8(c, p->fieldMsgMode) != 0;
		out->textBanner = task_active2(c, p, p->mapNameTask, p->mapNameTaskAlt);
		return true;
	}

	// In battle. Target-select is its own controller state — test it first (not by bg0y).
	// Phase 22.0: either revision's HandleInputChooseTarget counts (0 never matches — the read
	// is masked ~1u, so a live Thumb pointer is never 0).
	int bc = gbacore_read8(c, p->battlersCount);
	bool targetSel = false;
	for (int b = 0; b < bc && b < 4; b++) {
		uint32_t fn = gbacore_read32(c, p->ctrlFuncs + 4u * b) & ~1u;
		if ((p->chooseTarget && fn == p->chooseTarget) || (p->chooseTargetAlt && fn == p->chooseTargetAlt)) { targetSel = true; break; }
	}
	if (targetSel) {
		out->ctx = GCTX_BATTLE_TARGET;
		out->battlersCount = bc;
		out->absentMask = gbacore_read8(c, p->absentFlags);
		for (int i = 0; i < 4; i++) out->battlerPos[i] = (i < bc) ? gbacore_read8(c, p->battlerPos + i) : 0xFF;
		return true;
	}

	uint16_t bgy = gbacore_read16(c, p->bg0y);
	if (bgy == 160) {
		out->ctx = GCTX_BATTLE_ACTION;
		uint8_t a = gbacore_read8(c, p->actionCursor); out->actionCursor = (a < 4) ? a : -1;
	} else if (bgy == 320) {
		out->ctx = GCTX_BATTLE_MOVE;
		uint8_t m = gbacore_read8(c, p->moveCursor); out->moveCursor = (m < 4) ? m : -1;
		for (int i = 0; i < 4; i++) out->moveValid[i] = gbacore_read16(c, p->battleMons + BMON_MOVES_OFF + i * 2) != 0;
	} else {
		out->ctx = GCTX_BATTLE_OTHER;
	}
	return true;
}

// ============================ game-state instrumentation logger ============================
// LOGGING ONLY — reads the snapshot game_read already produced and records it; never writes game RAM,
// never changes touch/3D/gameplay. Edge-triggered ring + one-shot SD dump (the netlog pattern, verbatim).

static const char* const GS_CTXN[] = {   // index = GameCtx; matches main.c's teal-line names
	"none", "field", "b.act", "b.move", "b.tgt", "party", "fmenu", "bag", "b.oth",
	"title", "fullui",                   // phase 22.0 census promotion (GCTX_TITLE / GCTX_FULLUI)
	"naming", "list",                    // phase 22.1 keyboard + lists families
	"stor"                               // phase 22.2 grid family (PC storage boxes)
};
const char* gamestate_ctx_name(int ctx) {
	return (ctx >= 0 && ctx < (int)(sizeof GS_CTXN / sizeof GS_CTXN[0])) ? GS_CTXN[ctx] : "?";
}

#define GSLOG_N 1024
#define GS_HEARTBEAT_FRAMES 600u   // ~10s @ 60fps: a liveness/drift row even when nothing changed (EMULATED frames)
#define GS_HEARTBEAT_MS     2000u  // ~2s WALL-CLOCK: keeps logging a STUCK/FROZEN game (emulated clock stopped ->
                                   // the emulated-frame heartbeat can't fire). This is how we capture FireRed's
                                   // immediate hang and Emerald's post-error freeze (cb2 frozen at the error CB).
typedef struct {
	uint32_t frame; uint32_t cb1, cb2;
	uint16_t inj;
	int16_t  px, py, objX, objY;
	int16_t  mapG, mapN;   // u8 source (0..255) -> int16_t so FR/LG map numbers >=128 aren't sign-wrapped; -1 = N/A
	int8_t   face;
	uint8_t  scr, ctx, sb1V, resolved, nTask;
	uint32_t taskFp[8];
	uint8_t  dValid, dOw, dTT, dTB;            // 3D-effect health (top game only)
	int16_t  dNspr, dNui, dNfg, dCamX, dCamY;
	float    dMaxd;
	// per-sprite stereoscopic-disparity detail (proves the 3D EFFECT) — px @ full slider
	float    dFeetMin, dFeetMax, dHeadMin, dHeadMax;
	int16_t  dTallOk, dTallFail;
	uint8_t  dOrderOk, dS3d;
	uint8_t  dTiltLvl;                         // phase 14: effective tilt level on the TOP screen (I6.4)
	float    dTiltAngT, dTiltAngB;             // ...and the tweened angle per screen, DEGREES
	uint8_t  dPrLive, dPrDrawn, dPrReason;     // phase 15 co-op presence (A6.5.3) — the TOP game's
	int8_t   dPrFace;                          //   peer: liveness tier / drew / PRES_OFF_* / facing
	int16_t  dPrMapG, dPrMapN, dPrPx, dPrPy;   //   ...and its map + tile. LOGGING ONLY.
	uint8_t  dPrSprReason, dPrSprW, dPrSprH;   // phase 20 peer sprite (SPEC S4.3): why the live
	uint16_t dPrSprGfx;                        //   frame did/didn't resolve, its size, and the FORM
	uint32_t lstat, lbuf0, lbuf1, lnotrecv;    // link-error diagnostics (gLinkStatus / sLinkErrorBuffer / notRecv)
	uint8_t  lerr;                             // gLinkErrorOccurred
} GsLogEntry;
static GsLogEntry s_gsLog[GSLOG_N];
static uint32_t   s_gsLogN = 0;
// per screen-slot (0=top,1=bottom) edge cache: log only when ctx or raw cb2 changes, or the heartbeat fires.
static uint8_t  s_lastCtx[2]   = { 0xFF, 0xFF };
static uint32_t s_lastCb2[2]   = { 0, 0 };
static uint32_t s_lastFrame[2] = { 0, 0 };
static uint8_t  s_lastLinkErr[2] = { 0xFF, 0xFF };   // edge on the link-error flag flipping -> always log the death
static uint32_t s_lastTickMs[2]  = { 0, 0 };          // wall-clock of the last logged row (frozen-game heartbeat)
// Edge on the LINK STATE so we capture the exact failure SEQUENCE (CONN_ESTABLISHED appearing, RECEIVED_NOTHING
// starting, the player/master/error bits, and gRemoteLinkPlayersNotReceived changing) — not just at the coarse
// heartbeat. We mask lstat to the STABLE bits (local-id/playerCount/master/established/errors) and DROP the noisy
// RECEIVED_NOTHING(0x100)/UNK_9(0x200) that toggle most frames, so an established link doesn't flood the ring.
#define GS_LSTAT_KEY(s) ((uint32_t)(s) & 0x0007F07Fu)   // 0x7F000 errors | 0x40 established | 0x20 master | 0x1C count | 0x03 id
static uint32_t s_lastLstatKey[2] = { 0xFFFFFFFFu, 0xFFFFFFFFu };
static uint32_t s_lastNotRecv[2]  = { 0xFFFFFFFFu, 0xFFFFFFFFu };

// Phase 14 / I5.3 — header-only environment stamp (see gamestate.h). Deliberately NOT reset by
// gs_log_reset: the model and the 804 MHz probe are properties of the BOOT, not of a play session.
static int s_envKnown = 0, s_envN3DS = 0, s_envSpeedup = 0;
void gs_log_set_env(int isN3DS, int speedupActive) {
	s_envKnown = 1; s_envN3DS = isN3DS ? 1 : 0; s_envSpeedup = speedupActive ? 1 : 0;
}

void gs_log_reset(void) {
	s_gsLogN = 0;
	s_lastCtx[0] = s_lastCtx[1] = 0xFF;
	s_lastCb2[0] = s_lastCb2[1] = 0;
	s_lastFrame[0] = s_lastFrame[1] = 0;
	s_lastLinkErr[0] = s_lastLinkErr[1] = 0xFF;
	s_lastTickMs[0] = s_lastTickMs[1] = 0;
	s_lastLstatKey[0] = s_lastLstatKey[1] = 0xFFFFFFFFu;
	s_lastNotRecv[0]  = s_lastNotRecv[1]  = 0xFFFFFFFFu;
}

void gs_log_sample(GbaCore* c, const GameProfile* p, const GameState* gs,
                   int screen, uint16_t injKeys, const GsDepth* depth, uint32_t nowMs) {
	if (!c || !p || !gs || !gs->valid || screen < 0 || screen > 1) return;
	uint32_t frame = gbacore_frame_counter(c);
	uint32_t lstatKey = GS_LSTAT_KEY(gs->linkStatus);
	bool edge = (gs->ctx != s_lastCtx[screen]) || (gs->cb2 != s_lastCb2[screen])
	            || (gs->linkErr != s_lastLinkErr[screen])   // capture the exact frame the link error flips
	            || (lstatKey != s_lastLstatKey[screen])     // ...and each link-state transition (established/errors/roles)
	            || (gs->linkNotRecv != s_lastNotRecv[screen])  // ...and when a remote player stops being heard from
	            || (frame - s_lastFrame[screen] >= GS_HEARTBEAT_FRAMES)
	            || (nowMs - s_lastTickMs[screen] >= GS_HEARTBEAT_MS);   // WALL-CLOCK: keep logging a frozen game
	if (!edge) return;                                 // cheap no-op on the common (unchanged) frame
	s_lastCtx[screen] = (uint8_t)gs->ctx; s_lastCb2[screen] = gs->cb2; s_lastFrame[screen] = frame;
	s_lastLinkErr[screen] = gs->linkErr; s_lastTickMs[screen] = nowMs;
	s_lastLstatKey[screen] = lstatKey; s_lastNotRecv[screen] = gs->linkNotRecv;

	GsLogEntry* e = &s_gsLog[s_gsLogN % GSLOG_N];
	memset(e, 0, sizeof *e);
	e->frame = frame; e->scr = (uint8_t)screen; e->ctx = (uint8_t)gs->ctx;
	e->sb1V = gs->sb1Valid ? 1 : 0; e->resolved = gs->ctxResolved ? 1 : 0; e->nTask = gs->nTask;
	e->cb1 = gs->cb1; e->cb2 = gs->cb2; e->inj = injKeys;
	e->px = (int16_t)gs->px; e->py = (int16_t)gs->py;
	e->objX = (int16_t)gs->objX; e->objY = (int16_t)gs->objY;
	e->mapG = (int16_t)gs->mapGroup; e->mapN = (int16_t)gs->mapNum; e->face = (int8_t)gs->facing;
	for (int i = 0; i < 8; i++) e->taskFp[i] = gs->taskFp[i];
	if (depth) {
		e->dValid = 1; e->dOw = depth->overworld; e->dTT = depth->textTop; e->dTB = depth->textBot;
		e->dNspr = depth->nspr; e->dNui = depth->nui; e->dNfg = depth->nfg;
		e->dMaxd = depth->maxd; e->dCamX = depth->camX; e->dCamY = depth->camY;
		e->dFeetMin = depth->feetMin; e->dFeetMax = depth->feetMax;
		e->dHeadMin = depth->headMin; e->dHeadMax = depth->headMax;
		e->dTallOk = depth->tallOk; e->dTallFail = depth->tallFail;
		e->dOrderOk = depth->orderOk; e->dS3d = depth->s3d;
		e->dTiltLvl = depth->tiltLvl;                                   // phase 14 (I6.4)
		e->dTiltAngT = depth->tiltAngTop; e->dTiltAngB = depth->tiltAngBot;
		e->dPrLive = depth->prLive; e->dPrDrawn = depth->prDrawn;       // phase 15 (A6.5.3)
		e->dPrReason = depth->prReason; e->dPrFace = depth->prFace;
		e->dPrMapG = depth->prMapG; e->dPrMapN = depth->prMapN;
		e->dPrPx = depth->prPx;     e->dPrPy = depth->prPy;
		e->dPrSprReason = depth->prSprReason;                           // phase 20 (SPEC S4.3)
		e->dPrSprW = depth->prSprW; e->dPrSprH = depth->prSprH;
		e->dPrSprGfx = depth->prSprGfx;
	}
	e->lstat = gs->linkStatus; e->lbuf0 = gs->linkErrBuf0; e->lbuf1 = gs->linkErrBuf1;
	e->lnotrecv = gs->linkNotRecv; e->lerr = gs->linkErr;
	s_gsLogN++;
}

// Decode a GBA key mask (bit order A0 B1 Sel2 St3 Right4 Left5 Up6 Down7 R8 L9; see GBAKEY_* in gbacore.h).
static void gs_keystr(uint16_t k, char* out, int cap) {
	static const char* const N[] = { "A","B","s","S",">","<","^","v","R","L" };
	int n = 0;
	for (int i = 0; i < 10 && n < cap - 2; i++) if (k & (1u << i)) { const char* t = N[i]; while (*t && n < cap - 1) out[n++] = *t++; }
	if (n == 0) out[n++] = '-';
	out[n] = '\0';
}

void gamestate_log_dump(const char* path) {
	if (s_gsLogN == 0) return;           // nothing captured (e.g. non-Pokemon ROMs) -> don't litter SD with empty files
	mkdir("sdmc:/cias", 0777);           // ensure the parent dir exists (ignored if already present)
	mkdir("sdmc:/cias/netlogs", 0777);   // ...and the dedicated netlogs folder (same as the net logger)
	FILE* f = fopen(path, "w");
	if (!f) return;
	fprintf(f, "# 3DGBA game-state log  heartbeat=%u frames  scr: 0=top/3D 1=bottom/touch  (cb1/cb2 = raw gMain callbacks, Thumb-stripped)\n", GS_HEARTBEAT_FRAMES);
	fprintf(f, "# undetected screens fall through to ctx=field with resolved=0 (phase 22.0 promoted the census-harvested EM/FR sets to ctx=title/fullui; lane B added per-title RS sets — AXVE != AXPE, ROM drift; BPGE lists are still empty):\n");
	fprintf(f, "# read the cb2 column for each one you visit, then promote that value into that profile's cb2Title/cb2FullUi list (the census pipeline, CB2-HARVEST.md).\n");
	fprintf(f, "# geo: px,py=camera tile; objX,objY=true avatar tile; mapG,mapN=which map; face 1=D 2=U 3=L 4=R (NPC-overlay inputs). inj=injected touch key. d_*=3D-effect health (top rows).\n");
	fprintf(f, "# 3D detail (top rows; px @ FULL slider = the pop_eye disparity unit BEFORE *eyeSl, so slider-independent): d_feetMin/Max=grounded-feet disparity range; d_headMin/Max=head disparity (feet+standup, clamped); d_tallOk/d_tallFail=#sprites whose head exceeds feet by ~standup (tall renders taller) vs not; d_ordOk=1 if the on-screen set is monotonic in screen-y vs feet disparity (lower/closer pops >=); d_s3d=1 stereoscopic engaged this frame.\n");
	fprintf(f, "# link: lerr=gLinkErrorOccurred (1=game flagged a link error); lstat=gLinkStatus (live); lbuf0/lbuf1=sLinkErrorBuffer 8B LATCHED at error (lbuf0=status word, lbuf1 low bytes=send/recv queue counts+disconnected); lnotrecv=gRemoteLinkPlayersNotReceived. cb2=0800B1A0(EM)/0800AF2C(FR) = CB2_PrintErrorMessage = the red error screen.\n");
	// phase 14 (SPEC-integration I6.4/I5.3): the tilt columns + the environment stamp that keeps a
	// SLOW hardware photo from being blamed on the tilt (tilt is never clamped on the speedup probe).
	fprintf(f, "# tilt (top rows): d_tiltLvl=effective CLAMPED level 0..3 in force on the top screen (0=flat; the SAVED preference is never rewritten by a clamp); d_tiltAngT/d_tiltAngB=tweened angle in DEGREES per screen. Ladder: 0/10/15/20 deg. A level>0 with angle 0 = the gate is shut (menu/battle/dialog/touch/link/Old-3DS/stereo/frameskip).\n");
	if (s_envKnown)
		fprintf(f, "# env: model=%s speedup804=%s%s\n", s_envN3DS ? "New3DS" : "Old3DS",
		        s_envSpeedup ? "YES" : "NO",
		        s_envSpeedup ? "" : "  <- NOT running at 804MHz/L2 (.3dsx from the Homebrew Launcher cannot claim it): a low fps here is NOT a tilt cost");
	// phase 15 (SPEC-avatar A6.5.3): the co-op peer of the TOP game. This is the surface that fires
	// in a same-console run — the D3 CSV's peer columns only write during a wireless session, and
	// presence gate P-G3 turns the feature off for the whole of one (SPEC-data D4.8).
	fprintf(f, "# co-op (top rows): d_prLive=peer liveness 0 none/1 connected(record fresh, peer NOT game-active)/2 active; d_prDrawn=1 the gate resolved to DRAW; d_prReason=PRES_OFF_* code (0=drawing, and see presence.h: 1 off 2 menu 3 link 4 noprof 5 universe 6 self 7 field 8 obj 9 map 10 stale 11 cull); d_prMapG/d_prMapN != this row's mapG/mapN is THE commonest reason there is no avatar; d_prPx/d_prPy=peer tile; d_prFace=peer facing 1=D 2=U 3=L 4=R. Values are the PREVIOUS frame's solve (stamped in the parked window, solved in the render phase) = the frame the player just saw. LOGGING ONLY.\n");
	fprintf(f, "# peer sprite (top rows, phase 20): d_prSprReason=PSPR_R_* for the PEER's genuine overworld frame, read live out of THEIR core's OAM+OBJ VRAM+palette (0=the peer's own sprite is on screen; see peersprite.h: 1 remote 2 noprof 3 nosurf 4 tilefmt 5 ctx 6 noobj 7 badid 8 mismatch 9 notinuse 10 hidden 11 affine 12 bpp 13 size 14 mode 15 tile 16 pending); d_prSprW/d_prSprH=the decoded cell size in the sheet (16x32 walking, 32x32 bike/surf); d_prSprGfx=graphicsId, i.e. the FORM. ANY nonzero reason means the phase-15 magenta PLACEHOLDER is drawing instead — which is the designed fallback, never garbage. LOGGING ONLY.\n");
	fprintf(f, "idx,frame,scr,ctx,ctxName,cb1,cb2,sb1V,resolved,px,py,objX,objY,mapG,mapN,face,inj,nTask,t0,t1,t2,t3,t4,t5,t6,t7,d_ow,d_nspr,d_nui,d_nfg,d_maxd,d_camX,d_camY,d_feetMin,d_feetMax,d_headMin,d_headMax,d_tallOk,d_tallFail,d_ordOk,d_s3d,d_tiltLvl,d_tiltAngT,d_tiltAngB,d_prLive,d_prDrawn,d_prReason,d_prFace,d_prMapG,d_prMapN,d_prPx,d_prPy,d_prSprReason,d_prSprW,d_prSprH,d_prSprGfx,lerr,lstat,lbuf0,lbuf1,lnotrecv\n");
	uint32_t n    = (s_gsLogN < GSLOG_N) ? s_gsLogN : GSLOG_N;
	uint32_t base = (s_gsLogN < GSLOG_N) ? 0u : (s_gsLogN % GSLOG_N);   // oldest retained entry
	for (uint32_t i = 0; i < n; i++) {
		const GsLogEntry* e = &s_gsLog[(base + i) % GSLOG_N];
		char ks[12]; gs_keystr(e->inj, ks, sizeof ks);
		fprintf(f, "%lu,%lu,%u,%u,%s,%08lX,%08lX,%u,%u,%d,%d,%d,%d,%d,%d,%d,%s,%u,%08lX,%08lX,%08lX,%08lX,%08lX,%08lX,%08lX,%08lX",
		        (unsigned long)i, (unsigned long)e->frame, e->scr, e->ctx, gamestate_ctx_name(e->ctx),
		        (unsigned long)e->cb1, (unsigned long)e->cb2, e->sb1V, e->resolved,
		        e->px, e->py, e->objX, e->objY, e->mapG, e->mapN, e->face, ks, e->nTask,
		        (unsigned long)e->taskFp[0], (unsigned long)e->taskFp[1], (unsigned long)e->taskFp[2], (unsigned long)e->taskFp[3],
		        (unsigned long)e->taskFp[4], (unsigned long)e->taskFp[5], (unsigned long)e->taskFp[6], (unsigned long)e->taskFp[7]);
		if (e->dValid) {
			fprintf(f, ",%u,%d,%d,%d,%.2f,%d,%d", e->dOw, e->dNspr, e->dNui, e->dNfg, e->dMaxd, e->dCamX, e->dCamY);
			fprintf(f, ",%.2f,%.2f,%.2f,%.2f,%d,%d,%u,%u",          // per-sprite disparity detail (px @ full slider)
			        e->dFeetMin, e->dFeetMax, e->dHeadMin, e->dHeadMax, e->dTallOk, e->dTallFail, e->dOrderOk, e->dS3d);
			fprintf(f, ",%u,%.2f,%.2f", e->dTiltLvl, (double)e->dTiltAngT, (double)e->dTiltAngB);   // phase 14 tilt
			fprintf(f, ",%u,%u,%u,%d,%d,%d,%d,%d",                  // phase 15 co-op presence (A6.5.3)
			        e->dPrLive, e->dPrDrawn, e->dPrReason, e->dPrFace,
			        e->dPrMapG, e->dPrMapN, e->dPrPx, e->dPrPy);
			fprintf(f, ",%u,%u,%u,%u",                              // phase 20 peer sprite (S4.3)
			        e->dPrSprReason, e->dPrSprW, e->dPrSprH, e->dPrSprGfx);
			// I6.5: 7 d_* + 8 detail + 3 tilt + 8 presence + 4 peer-sprite = 30 empty fields on a
			// non-depth (bottom) row. Getting this count wrong shifts every LATER column on those
			// rows and silently corrupts the link columns, which is the one thing this log exists
			// to make readable. THE COMMA RUN BELOW MUST HAVE EXACTLY 30 COMMAS.
		} else fprintf(f, ",,,,,,,,,,,,,,,,,,,,,,,,,,,,,,");
		fprintf(f, ",%u,%08lX,%08lX,%08lX,%08lX\n", e->lerr,                       // link-error diagnostics
		        (unsigned long)e->lstat, (unsigned long)e->lbuf0, (unsigned long)e->lbuf1, (unsigned long)e->lnotrecv);
	}
	fclose(f);
}
