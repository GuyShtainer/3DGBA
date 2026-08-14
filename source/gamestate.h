// gamestate.h — game-aware touch (v1.1): read live Gen-3 state from the running core so the
// touchscreen can act as a POINTER on the real game UI. All RAM addresses verified vs pret's
// byte-matched symbol maps; see docs/kb/gen3-ram-touch.md + gen3-touch-features-spec.md.
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "gbacore.h"

// On-screen context the bottom game is in (drives how touch is interpreted).
typedef enum {
	GCTX_NONE = 0,
	GCTX_OVERWORLD,
	GCTX_BATTLE_ACTION,   // FIGHT / BAG / POKEMON / RUN
	GCTX_BATTLE_MOVE,     // move-selection menu
	GCTX_BATTLE_TARGET,   // double-battle "choose a target" (HandleInputChooseTarget live)
	GCTX_PARTY,           // party menu open (in-battle send-out)
	GCTX_FIELDMENU,       // overworld sMenu up (START / YES-NO / script multichoice)
	GCTX_BAG,             // bag menu open (field or battle "ITEM")
	GCTX_BATTLE_OTHER,    // some other battle screen (dialog/animation) — tap = advance (A)
	// --- phase 22.0 census promotion (docs/phase21-touch-census/CB2-HARVEST.md). APPENDED so every
	// existing value is stable: main.c pins GCTX_OVERWORLD == TILT_CTX_FIELD == FIELD_CTX_OVERWORLD
	// with _Static_asserts, and the gs-log ring stores ctx as a u8. Both classes are matched
	// POSITIVELY by live-harvested cb2 fingerprints (GameProfile.cb2Title/cb2FullUi below), which
	// un-hides those screens from the GCTX_OVERWORLD fall-through: touch stops leaking walk/A/START
	// keys into them (touch.c dispatch: default -> 0 keys), and the tilt/presence/field gates —
	// which all test ctx == GCTX_OVERWORLD — shut on them with ZERO logic change at the gates.
	GCTX_TITLE,           // pre-game screens: intro / title / main menu / new-game scene (no save ctx)
	GCTX_FULLUI,          // a full-screen UI over a loaded save (dex/summary/card/storage/naming/...)
	// --- phase 22.1 (lane A): the first two touch FAMILIES, appended (values stay stable). Both
	// are matched MORE SPECIFICALLY than the phase-22.0 classes: game_read tests them before the
	// cb2Title/cb2FullUi loops, so a naming screen stops reading as bare GCTX_FULLUI and becomes
	// an interactive keyboard, and the ListMenu screens get a live list driver.
	GCTX_NAMING,          // the Gen-3 naming keyboard (cb2 == CB2_NamingScreen; SPEC-family-keyboard)
	GCTX_LIST,            // a driven ListMenu screen (mart buy / PC items / qty / dex — see ListKind)
	// --- phase 22.2 (overnight lane): the GRID family — the PC storage boxes UI. Appended (values
	// stay stable); matched by cb2 == CB2_PokeStorage (GameProfile.storageCb) BEFORE the cb2FullUi
	// loop, exactly like GCTX_NAMING (more specific wins; the fullui list keeps the value).
	GCTX_STORAGE,         // PC storage boxes (SPEC-family-grid; EM only in v1 — see storageCb)
	// --- phase 24 (lane B2): the MAP family — the region map, and with it TAP-TO-FLY. Appended
	// (every value above stays stable); matched by cb2 == GameProfile.rmFlyCb / rmWallCb BEFORE
	// the cb2FullUi loop, exactly like GCTX_NAMING and GCTX_STORAGE (more specific wins; both
	// values stay in cb2FullUi so a game without the map anchors keeps today's FAM-DLG default).
	GCTX_MAP,             // region map: fly map (A confirms) or wall map (cursor only) — see mapFly
	// --- phase 25 (lane C1): the INERT class — "detected, and deliberately SILENT". Appended, so
	// every value above stays stable. It is NOT the same thing as the pre-phase-23 default: an
	// unknown screen falls through to GCTX_OVERWORLD and gets the whole walk machinery, while this
	// says the classifier KNOWS the screen and the right verb there is *nothing*. Two members:
	//   * the CREDITS (TOUCH-PLAN L2, "tap = nothing — don't skip by accident"). Not a preference:
	//     pokeemerald src/credits.c:349-357 makes JOY_HELD(B_BUTTON) the credits FAST-FORWARD and
	//     latches sUsedSpeedUp, so the FAM-DLG hold verb would double-speed the credits under a
	//     resting finger. GameProfile.cb2Inert names the screen.
	//   * FRLG QUEST-LOG PLAYBACK (row K4) — a cutscene the player does not control, which runs
	//     under CB2_Overworld with no cb2 of its own and, as lane B1 MEASURED, with
	//     sLockFieldControls at 0 for most of it, so taps armed routes at a game the player was
	//     not driving. GameProfile.questLog names the state instead of the screen.
	// Dispatch: touch.c returns 0 keys for this ctx (and the tilt/presence/field gates, which all
	// test ctx == GCTX_OVERWORLD, shut on it with zero gate-logic change).
	GCTX_INERT
} GameCtx;

// Which list screen GCTX_LIST resolved to (GameState.listKind; SPEC-family-lists §2/§3).
// GCTX_BAG keeps its own context (shipped, hardware-exercised) but shares the same driver.
enum {
	LK_NONE = 0,
	LK_BUY,      // mart buy menu (Task_BuyMenu live; listBase = its tListTaskId's ListMenu)
	LK_PCITEM,   // PC item storage list (EM ItemStorage_ProcessInput / FRLG Task_ItemPcMain)
	LK_QTY,      // "how many?" quantity roller over the buy list (Task_BuyHowManyDialogueHandleInput)
	LK_DEX,      // EM Pokedex list (custom cursor model — key-injection only, L20/L21)
	// phase 24 (lane B2): a FULL-SCREEN UI whose list is DISCOVERED rather than anchored — see
	// GameProfile.cb2List. Same driver as LK_BUY/LK_PCITEM (list_update); the only difference is
	// where listBase came from.
	LK_FULLUI    // FR Berry Pouch / TM Case (TOUCH-PLAN E4/E5) and any future opt-in screen
};

// Capacity of the phase-22.0 cb2 screen-class fingerprint lists (GameProfile.cb2Title/cb2FullUi).
// Sized to the largest harvested set (FR: 5 title-class, 16 fullui-class cb2s) plus headroom for
// the LG/RS delta passes; unused slots are 0 and never match (a cb2 is never 0).
#define GS_N_TITLE  6
// PHASE 25 (lane C1): 18 -> 32. The phase-24 lane-B1 tap-verify found that nine of its ten BROKEN
// `TAP` rows share one cause — the screen has no fingerprint, so the "safe default" never runs
// there — and this lane's harvest adds 13 EM + 10 FR values (evolution, mail, Hall of Fame,
// contest results/painting, Berry Blender, link error). 32 keeps headroom for the LG/RS passes.
// The list is a linear compare of u32s on a frame that already does dozens of bus reads.
#define GS_N_FULLUI 32
// Capacity of the phase-25 INERT list (GameProfile.cb2Inert) — screens that must be DETECTED and
// then given nothing. v1 ships 2 per EM/FR row (the credits run loop + its multi-frame starter).
#define GS_N_INERT  4
// Capacity of the phase-23 FAM-DLG pager list (GameProfile.cb2Pager) — the FULLUI screens on which
// LEFT/RIGHT is a real page/value verb, so the tap-advance family gives them edge-zone taps. v1
// ships 2 per FRLG/EM row (summary + options); sized to 4 for the dex-entry / trainer-card / berry
// tag candidates the census still owes a live LEFT/RIGHT confirmation.
#define GS_N_PAGER  4
// Capacity of the phase-24 (lane B2) DISCOVERED-LIST whitelist (GameProfile.cb2List). v1 ships
// 2 entries (FR's Berry Pouch + TM Case); sized to 4 for the mailbox / move-relearner / dex-TOC
// candidates the census still owes a live confirmation.
#define GS_N_LISTCB2 4
// PHASE 25 (lane D1) — which REGION-MAP ENGINE a game runs (GameProfile.rmVariant). Not a family
// flag: the two engines really are different code with different struct layouts, and the shared
// half is the input model, which is what the FAM-MAP driver is built on. 0 = no support.
#define GS_RMAP_NONE 0
#define GS_RMAP_EM   1   // pokeemerald src/region_map.c   — two cb2s, one struct, cursor inside it
#define GS_RMAP_FR   2   // pokefirered src/region_map.c   — ONE cb2, mode in the struct, cursor out
// pokefirered `struct RegionMap` (src/region_map.c:93-116) — the ONE offset the FR path reads out
// of the map struct itself. The layout is pinned by pret's own `// size = 0x47C0` trailer:
//   mapName[19] +0x0000 · dungeonName[19] +0x0013 · layouts[5][600] u16 +0x0026 (0x1770 bytes) ·
//   bgTilemapBuffers[3][0x800] u16 +0x1796 (0x3000 bytes) · **type +0x4796** · permissions[4]
//   +0x4797 · selectedRegion +0x479B · playersRegion +0x479C · mainState/openState/loadGfxState
//   ALIGNED(4) +0x47A0/+0x47A4/+0x47A8 · 4 u16 +0x47AA · filler[6] +0x47B2 · mainTask +0x47B8 ·
//   savedCallback +0x47BC  ==> 0x47C0. No other reading of the ALIGNED(4) run lands on it.
#define GS_FR_RM_TYPE_OFF   0x4796
#define GS_FR_RMTYPE_NORMAL 0   // the bag's TOWN MAP (has the region SWITCH + dungeon previews)
#define GS_FR_RMTYPE_WALL   1   // a wall map read from the field
#define GS_FR_RMTYPE_FLY    2   // the FLY destination picker — the only mode that accepts an A

// Per-game RAM map (all absolute GBA bus addresses; EM=Emerald, FR=FireRed/LeafGreen).
typedef struct {
	char     code[5];
	uint32_t sb1ptr;        // gSaveBlock1Ptr (deref -> player x/y, first two s16)
	uint32_t battleFlags;   // gBattleTypeFlags (nonzero in battle)
	uint32_t actionCursor;  // gActionSelectionCursor[0]
	uint32_t moveCursor;    // gMoveSelectionCursor[0]
	uint32_t battleMons;    // gBattleMons[0] (moves[] at +0x0C)
	uint32_t bg0y;          // gBattle_BG0_Y (160 action / 320 move menu)
	// party menu
	uint32_t partyMenu;     // gPartyMenu base (+0x08 menuType|layout, +0x09 slotId)
	uint32_t partyCount;    // gPlayerPartyCount (u8)
	uint32_t mainCb2;       // gMain.callback2 (u32)
	uint32_t cb2UpdParty;   // CB2_UpdatePartyMenu (ROM, compare with Thumb bit masked)
	uint32_t cb2InitParty;  // CB2_InitPartyMenu (fade-in; not yet tappable)
	uint32_t newKeys;       // gMain.newKeys (u16) — unused (we inject via the returned key mask)
	// double-battle target select
	uint32_t ctrlFuncs;     // gBattlerControllerFuncs[4] (u32 ptrs)
	uint32_t chooseTarget;  // HandleInputChooseTarget (ROM, compare with Thumb bit masked)
	uint32_t multiCursor;   // gMultiUsePlayerCursor (u8) — targeted battler index
	uint32_t battlerPos;    // gBattlerPositions[4] (u8)
	uint32_t battlersCount; // gBattlersCount (u8)
	uint32_t absentFlags;   // gAbsentBattlerFlags (u8)
	uint32_t activeBattler; // gActiveBattler (u8)
	// overworld pathfinding
	uint32_t mapLayout;     // gBackupMapLayout / VMap (s32 width, s32 height, u16* map)
	// general field menu (sMenu) + bag
	uint32_t startCb;       // gMenuCallback / sStartMenuCallback (fn ptr; START active when == startCbInput)
	uint32_t startCbInput;  // HandleStartMenuInput / StartCB_HandleInput (ROM; compare Thumb-masked)
	uint32_t sMenuBase;     // struct Menu sMenu (+1 top, +2 cursorPos, +4 maxCursorPos, +5 windowId, +8 optHeight)
	uint32_t gWindowsBase;  // gWindows[] (12-byte stride: +0 bg, +1 left, +2 top, +3 width, +4 height)
	uint32_t startCursor;   // sStartMenuCursorPos (write too for the START menu)
	uint32_t gTasksBase;    // gTasks[] (40-byte stride: +0 func, +4 isActive, +8 data[])
	uint32_t cb2BagRun;     // CB2_BagMenuRun (ROM; gMain.callback2 == this when the bag is up)
	uint32_t bagHandler;    // Task_BagMenu_HandleInput (ROM; the live list-task owner)
	uint32_t bagOpen;       // FR gBagMenuState.bagOpen (bool8); 0 = unused (Emerald)
	uint32_t partyTask;     // Task_HandleChooseMonInput (party input handler — field OR battle)
	uint32_t yesNoTask;     // Task_HandleYesNoInput
	uint32_t multiTask;     // Task_HandleMultichoiceInput
	uint32_t selMenuTask;   // Task_HandleSelectionMenuInput (party SUMMARY/SWITCH/ITEM/CANCEL popup; uses sMenu)
	uint32_t startMenuTask; // Task_ShowStartMenu / Task_StartMenuHandleInput (active only while START menu is up)
	uint32_t mapHeader;     // gMapHeader (BPEE 0x02037318) for stereoscopic scenery depth; 0 = no M4
	uint32_t battleMainCb;  // BattleMainCB2 (callback2==this == interactive battle; battleFlags lingers post-battle)
	uint32_t mapObjects;    // gObjectEvents[16] (stride 0x24; +0 active:1, +0x10/+0x12 currentCoords x/y) -> NPC collision
	uint32_t fieldMsgMode;  // sFieldMessageBoxMode (EM) / sMessageBoxType (FRLG): u8, != 0 while a field textbox is up
	uint32_t mapNameTask;   // Task_MapNamePopUpWindow (EM) / Task_MapNamePopup (FRLG) — map-name banner task (ROM)
	uint32_t fieldCamera;   // gFieldCamera (+0x10 x, +0x14 y = sub-tile scroll, %%16) -> 3D depth scroll-align
	// --- link-error diagnostics (LOGGING ONLY; addresses verified vs pret symbols-branch rev0). When the
	// wireless trade dies the game enters CB2_PrintErrorMessage; these latch WHY (which watchdog / queue /
	// player-drop tripped) even though OUR transport was clean. 0 = not mapped for this game. ---
	uint32_t linkStatus;    // gLinkStatus (u32 bitfield: player count + LINK_STAT_* error bits, live)
	uint32_t linkErr;       // gLinkErrorOccurred (u8; 1 once the game's link layer flagged an error)
	uint32_t linkErrBuf;    // sLinkErrorBuffer (8B latched at error: status u32 + send/recv queue counts)
	uint32_t linkNotRecv;   // gRemoteLinkPlayersNotReceived (u32 player bitmask we stopped hearing from)
	// --- D2 game-heartbeat hang catcher (phase 13-prep, SPEC-firmware-diag D2.1; LOGGING ONLY) ---
	// gMain.vblankCounter1 (u32 @ gMain+0x20): incremented UNCONDITIONALLY in VBlankIntr (pret
	// pokeemerald src/main.c VBlankIntr(): "gMain.vblankCounter1++"; pokefirered same) — the game's
	// own IRQ-delivery heartbeat. Frozen while the core still produces video frames => the game's
	// interrupt path is dead (the D2 Tier-B watch; diag.h DiagHang). Derivation (no re-verified sym
	// map on this machine — SPEC D2.1): gMain base = the VERIFIED mainCb2 - 4 (gamestate.c game_read:
	// "gMain+0 = mainCb2-4"); offset 0x20 per pret include/main.h struct Main layout: callbacks
	// 0x00-0x18 (7 x MainCallback/IntrCallback ptrs), intrCheck u16 @0x1C (+2 pad), vblankCounter1
	// u32 @0x20, vblankCounter2 u32 @0x24, heldKeysRaw @0x28 ... newKeys u16 @0x2E — the layout
	// through 0x20 is cross-checked by the hw-exercised newKeys anchor (BPEE 0x030022EE = gMain+0x2E).
	// Status: derived-from-verified-anchors, VERIFY-ON-HW-PENDING (self-verifying once the D3 CSV
	// shows it ticking ~60/s); BPGE is FR-derived/unverified per the house rule (HANDOFF Gotchas).
	uint32_t vblankCtr;     // gMain.vblankCounter1 (u32); 0 = not mapped -> D2 Tier B stays disarmed
	// --- phase 15 co-op presence (SPEC-data.md D1). All VERIFIED-SYM against pret's byte-matched
	// `symbols` branch (re-read 2026-08-04); FR/LG values come from pokefirered.sym AND
	// pokeleafgreen.sym SEPARATELY (not FR-derived), and the rev0/rev1 maps agree on every one of
	// them — which matters because the user's FireRed is rev1 (MEMORY, run #12). 0 = not mapped,
	// and every 0 has a named graceful degradation in D1.9 (never a garbage read, never a draw at
	// a plausible-looking wrong position). Appending is the only safe edit: PROFILES[] in
	// gamestate.c is POSITIONAL-initialised (the D2 vblankCtr block above is the precedent). ---
	uint32_t sb2ptr;        // gSaveBlock2Ptr (deref -> +0x00 name[8] GBA charmap 0xFF-terminated,
	                        //   +0x08 playerGender 0=M/1=F, +0x0A visible TID = LE u16). D1.1/D1.2:
	                        //   EM sym:962 / FR sym:810 / LG sym:810; = sb1ptr+4 (a CONSEQUENCE of
	                        //   the read, not the derivation). Corroborated in-repo by
	                        //   docs/kb/gen3-ram-touch.md:48-49. VERIFIED-SYM.
	uint32_t spriteCoordOff;// gSpriteCoordOffsetX (s16); Y = this + 2. D1.4: EM sym:20-21,
	                        //   FR/LG sym:23-24. LOGGING/diagnostic ONLY in M0-M2 — the SHIPPED
	                        //   sub-tile source is fieldCamera+0x10/+0x14 (D1.3), which is already
	                        //   in this struct and already hardware-exercised by build_depth_grid.
	                        //   What this buys is the scripted CAMERA PAN term
	                        //   (gSpriteCoordOffset = gTotalCameraPixelOffset - sCameraPan,
	                        //   pokeemerald src/field_camera.c:459-462 == pokefirered :521-527);
	                        //   promote it into the anchor math only if a hardware run shows a
	                        //   pan-time misalignment (SPEC-data Open Q4). VERIFIED-SYM.
	uint32_t hbCtr;         // gMain.vblankCounter2 = gMain+0x24 — the presence liveness/wedge
	                        //   heartbeat (D1.5), incremented UNCONDITIONALLY in VBlankIntr in BOTH
	                        //   engines (pokeemerald src/main.c:355, pokefirered src/main.c:396).
	                        //   NOT vblankCtr: FR/LG's gMain+0x20 is declared `u32 *vblankCounter1`
	                        //   (pokefirered include/main.h:26) and is NULL unless SetVBlankCounter
	                        //   armed it, so the D2/D3 columns above are silently disarmed on the
	                        //   user's FireRed (a real, REPORTED-not-silently-changed defect in a
	                        //   LOGGING-ONLY path — SPEC-data D1.5.1 / Open Q1; presence routes
	                        //   around it instead of editing what a diagnostics column means
	                        //   mid-flight). gMain bases are themselves VERIFIED-SYM now: EM
	                        //   0x030022C0 (sym:894), FR/LG 0x030030F0 (sym:745) == mainCb2-4, which
	                        //   also retires the "derived" caveat on the vblankCtr ADDRESS above.
	                        //   VERIFIED-SYM base + VERIFIED-SRC offset (include/main.h:8-33).
	// --- phase 18 smart-touch warp routing (SPEC-door T4.1). gMapHeader again, but a SEPARATE
	// field from `mapHeader` above ON PURPOSE: main.c:885 gates the phase-14 HD-2D metatile-layer
	// depth path on `!p->mapHeader`, so filling that zero for FR/LG would silently switch on an
	// untested 3D path for two games — a render change this phase is forbidden to make. Values are
	// VERIFIED-SYM against pret's byte-matched `symbols` branch, re-read 2026-08-12:
	//   BPEE 0x02037318 (pokeemerald.sym "gMapHeader")
	//   BPRE 0x02036DFC (pokefirered.sym AND pokefirered_rev1.sym — identical; the user's FR is rev1)
	//   BPGE 0x02036DFC (pokeleafgreen.sym AND pokeleafgreen_rev1.sym, which live on the
	//                    pokefirered `symbols` branch — LG's OWN map, not FR-derived)
	// Note the trap the house rule catches: the plausible derivation `gObjectEvents - 0x38` holds
	// in Emerald and is WRONG for FRLG (it gives 0x02036E00). 0 = not mapped -> fieldpath.c
	// classifies nothing and the router keeps its pre-phase-18 behaviour. Appending is the only
	// safe edit here: PROFILES[] in gamestate.c is POSITIONAL-initialised.
	uint32_t mapHeaderPath; // gMapHeader for warp classification (MapLayout -> Tileset -> attrs)
	// --- phase 18 Ruby/Sapphire support (SPEC-coop P3.2). "RS is Emerald with different
	// addresses" is FALSE in exactly one structural way: Ruby/Sapphire have NO gSaveBlock1Ptr /
	// gSaveBlock2Ptr — the pointer indirection is an Emerald/FRLG-era change. pokeruby declares
	// `extern struct SaveBlock1 gSaveBlock1;` (include/global.h:668,756) and the symbol maps carry
	// the STRUCT, not a pointer to it: pokeruby.sym:106 `02025734 g 00003ac0 gSaveBlock1`,
	// :105 `02024ea4 g 00000890 gSaveBlock2`, and there is no *Ptr symbol in any of the four maps
	// (pokeruby / pokesapphire / pokeruby_rev1 / pokesapphire_rev1, all re-read + diffed
	// 2026-08-13: 727 RAM symbols, ZERO differences across all four).
	//   0 = sb1ptr/sb2ptr are POINTERS to deref (BPEE/BPRE/BPGE — unchanged behaviour)
	//   1 = sb1ptr/sb2ptr ARE the struct addresses (AXVE/AXPE)
	// Ignoring this is not a cosmetic bug: game_read's `(deref >> 24) == 0x02` validity test would
	// read SaveBlock1.pos (x | y<<16) as a pointer, sb1Valid would be false forever, and presence
	// would report OFF_FIELD on every frame. Exactly TWO call sites branch on it (game_read here
	// and ident_refresh in presence_read.c) — the only two `grep sb1ptr|sb2ptr source/` finds.
	// Appending is again the only safe edit: PROFILES[] is POSITIONAL-initialised.
	uint8_t  sbDirect;      // 1 = sb1ptr/sb2ptr are the structs themselves (RS), 0 = pointers
	// --- phase 20 peer sprite (docs/phase20-peersprite/SPEC.md S1.3). The three addresses that
	// turn `gObjectEvents[0].spriteId` into the peer's ACTUAL currently-displayed 16x32 trainer
	// frame. All VERIFIED-SYM against pret's byte-matched `symbols` branch, re-derived from the
	// nine maps on this machine on 2026-08-13 (the house rule: never ship someone else's word, not
	// even a spec's — the values below were re-read, not copied):
	//   gSprites            EM 0x02020630 | FR 0x0202063C = _rev1 | LG 0x0202063C = _rev1
	//                       RS 0x02020004 (pokeruby / pokesapphire / both rev1 — 4/4 AGREE)
	//   gPlttBufferUnfaded  EM 0x02037714 | FR 0x020371F8 = _rev1 | LG 0x020371F8 = _rev1
	//                       RS 0x0202EAC8 x4                        (4/4 AGREE)
	//   gPlayerAvatar       EM 0x02037590 (size 0x24) | FR/LG 0x02037078 (size 0x20, both revs)
	//                       RS 0x0202E858 (size 0x24) x4            (4/4 AGREE)
	// gSprites' symbol SIZE is 0x1144 == 65 * 0x44 (MAX_SPRITES + 1) in ALL NINE maps, which is
	// itself strong evidence `struct Sprite` is unchanged across RS / FRLG / Emerald.
	//
	// Every 0 is a NAMED degradation, never a guess: `sprites == 0` => that game falls back to the
	// phase-15 placeholder with reason PSPR_R_NOPROF; `plttUnfaded == 0` => the OBJ palette is read
	// from hardware PLTT 0x05000200 instead (which carries the peer's screen fades — a documented,
	// worse-but-correct source, SPEC S1.7); `playerAvatar == 0` => the spriteId cross-check is
	// SKIPPED rather than guessed. All five shipped profiles get real values, so no game degrades.
	//
	// Ruby/Sapphire stay VERIFIED-SYM / VERIFY-ON-HW for the same reason phase 18 gave: no RS ROM
	// exists on this machine. Appending is again the only safe edit — PROFILES[] is POSITIONAL.
	uint32_t sprites;       // gSprites[65], stride 0x44 (oam @+0x00, animNum/animCmdIndex @+0x2A,
	                        //   inUse/invisible @+0x3E, subspriteTableNum @+0x42)
	uint32_t plttUnfaded;   // gPlttBufferUnfaded u16[512]: BG banks 0-15 then OBJ banks 0-15, so
	                        //   OBJ bank n colour i is at +512 + 32*n + 2*i BYTES
	uint32_t playerAvatar;  // gPlayerAvatar (+0x00 flags, +0x04 spriteId) — the cross-check only
	// --- phase 22.0 (census S2 merge) REV-ALTERNATE ROM anchors. The census's FR visit proved the
	// one thing the row comments above got wrong: FRLG ROM function addresses MOVE between rev0 and
	// rev1 (shifts 0x14-0x78 observed; RAM is rev-identical) — the user's FireRed is rev1, so every
	// BPRE ROM anchor above was silently dead on the real cart (VISITED-firered.md headline).
	// profile_for keys on the 4-char game code and cannot see the header's revision byte, so the
	// row must detect BOTH revisions: each rev-sensitive ROM anchor gains an ALTERNATE that every
	// compare site in gamestate.c tests alongside the primary (task_active2 / inBattle / the
	// ctrlFuncs scan). A wrong-rev anchor is compare-only — it never fires, never dereferences —
	// so carrying both is fail-safe by construction. 0 = no alternate. Appending is the only safe
	// edit: PROFILES[] is POSITIONAL-initialised (same licence as every appended block above). ---
	uint32_t battleMainCbAlt;   // BattleMainCB2 (other rev)         BPRE/BPGE rev1: 0x08011114
	uint32_t cb2UpdPartyAlt;    // CB2_UpdatePartyMenu (other rev)
	uint32_t cb2InitPartyAlt;   // CB2_InitPartyMenu (other rev)
	uint32_t chooseTargetAlt;   // HandleInputChooseTarget (other rev)
	uint32_t startCbInputAlt;   // StartCB_HandleInput (other rev)
	uint32_t cb2BagRunAlt;      // CB2_BagMenuRun (other rev)
	uint32_t bagHandlerAlt;     // Task_BagMenu_HandleInput (other rev)
	uint32_t partyTaskAlt;      // Task_HandleChooseMonInput (other rev)
	uint32_t yesNoTaskAlt;      // Task_YesNoMenu_HandleInput (other rev). BPGE DEVIATES: it
	                            //   carries Task_CallYesOrNoCallback 0x080BF548 (LG rev1, live-
	                            //   verified twice) — FRLG bypasses Task_YesNoMenu_HandleInput on
	                            //   every common yes/no flow (LANE-B-LG.md #8), so the bag-toss /
	                            //   mart-buy confirms detect via this slot instead.
	uint32_t multiTaskAlt;      // Task_MultichoiceMenu_HandleInput (other rev)
	uint32_t selMenuTaskAlt;    // Task_HandleSelectionMenuInput (other rev)
	uint32_t startMenuTaskAlt;  // Task_StartMenuHandleInput (other rev)
	uint32_t mapNameTaskAlt;    // Task_MapNamePopup (other rev)
	// --- phase 22.0 (census S2 merge) cb2 SCREEN-CLASS fingerprints — the promotion of the
	// phase-21 harvest (docs/phase21-touch-census/CB2-HARVEST.md; every non-zero value below was
	// read [exact] from a LIVE game via the gs-logger gdb channel and resolved on the pret
	// byte-matched sym maps — zero guesses). game_read matches gMain.callback2 against these AFTER
	// every task-based menu check and BEFORE the overworld fall-through, so a listed screen stops
	// hiding in GCTX_OVERWORLD (where taps leak walk keys and the tilt/presence gates stay open).
	// 0 = unused slot (arrays are brace-initialised per row; trailing slots zero-fill, and a zero
	// slot never matches because a cb2 is never 0). Lane B (2026-08-14) filled the RS lists from
	// LIVE reads — PER TITLE, because Ruby and Sapphire ROM addresses drift (AXVE != AXPE here;
	// LANE-B-RS.md §2 — never copy a cb2 across the two). BPGE still carries all-zero lists — a
	// NAMED degradation: its undetected screens keep the pre-phase-22 fall-through behaviour
	// until the LG harvest (already banked in LANE-B-LG.md) is promoted in its own slice. ---
	uint32_t cb2Title[GS_N_TITLE];    // GCTX_TITLE class: intro / title / main menu / new-game
	uint32_t cb2FullUi[GS_N_FULLUI];  // GCTX_FULLUI class: full-screen UIs over a loaded save
	// --- phase 22.1 (lane A) — KEYBOARD + LISTS family anchors. Every non-zero value below was
	// RE-READ from the pret byte-matched symbol maps THIS session (2026-08-14 scratchpad syms/,
	// the same five maps the census used: pokeemerald.sym, pokefirered.sym, pokefirered_rev1.sym,
	// pokeleafgreen.sym, pokeleafgreen_rev1.sym) — never copied from a spec (house zero-guess
	// rule; the specs' values agreed on every overlap). ROM anchors follow the phase-22.0
	// dual-revision convention: primary = the row's primary revision, *Alt = the other, both
	// compare-only => fail-safe. Every 0 is a NAMED degradation: the feature is absent for that
	// game and its GCTX never fires (RS: the ROM-address ban, RS-REV2-VERIFICATION.md §2.3).
	// Appending is the only safe edit: PROFILES[] is POSITIONAL-initialised. ---
	uint32_t namingCb;        // CB2_NamingScreen (ROM) -> GCTX_NAMING (tested BEFORE cb2FullUi,
	                          //   which also lists it — more specific wins; the list is untouched)
	uint32_t namingCbAlt;     //   other revision
	uint32_t namingPtr;       // sNamingScreen (EWRAM static ptr -> struct NamingScreenData*;
	                          //   textBuffer +0x1800, state +0x1E10, currentPage +0x1E22,
	                          //   cursorSpriteId +0x1E23 — offsets VERIFIED-SRC both engines)
	uint32_t buyTask;         // Task_BuyMenu (ROM task fn) -> LK_BUY
	uint32_t buyTaskAlt;
	uint32_t buyQtyTask;      // Task_BuyHowManyDialogueHandleInput -> LK_QTY (without it the qty
	                          //   roller would fall through to the walk-key residual)
	uint32_t buyQtyTaskAlt;
	uint32_t pcItemTask;      // EM ItemStorage_ProcessInput / FRLG Task_ItemPcMain -> LK_PCITEM
	uint32_t pcItemTaskAlt;
	uint32_t lmDummyTask;     // ListMenuDummyTask (ROM) — the P-D DISCOVERY PROBE anchor: any live
	                          //   ListMenu owns a dummy task with this fn. LOGGING ONLY (mirrored
	                          //   to g_touchDbg on GCTX_FULLUI screens); never drives touch.
	uint32_t lmDummyTaskAlt;
	uint32_t bagPocket;       // EM gBagPosition.pocket = 0x0203CE58+5 (MainCallback 4B + location
	                          //   u8; VERIFIED-SRC include/item_menu.h:49-57 + sym). Read-only,
	                          //   for the pocket-dot tab delta. 0 = arrow-tap game (FRLG) or none.
	uint32_t dexTask;         // EM Task_HandlePokedexInput -> LK_DEX (key-injection only). 0 = none.
	uint32_t dexView;         // EM sPokedexView (EWRAM ptr; selectedPokemon +0x60E, count +0x60C,
	                          //   initialVOffset +0x62B u8, listVOffset +0x62E s16 — the L21
	                          //   derivation channel, LOGGING ONLY). 0 = none.
	uint8_t  buyListSlot;     // tListTaskId slot in the buy task's data[] (BOTH engines: data[7] —
	                          //   pokeemerald src/shop.c:412, pokefirered src/shop.c:35)
	uint8_t  pcItemListSlot;  // EM data[5] (player_pc.c:391) / FRLG data[0] (item_pc.c:350)
	// --- phase 22.2 (overnight lane) — the GRID family (PC storage boxes; SPEC-family-grid §1.1).
	// EM values re-read from the LOCAL pokeemerald.sym copy this session (gba-toolkit/projects/
	// rec2mp4/local/pokeemerald.sym — the same byte-matched pret map the census used); the five
	// statics are CONTIGUOUS there, so two bases + documented offsets carry all seven fields.
	// storageCb is the census live-harvest [exact] (CB2_PokeStorage, CB2-HARVEST.md; it stays in
	// cb2FullUi too — TEST 11 pins that list; GCTX_STORAGE simply tests first). All sym-derived /
	// verify-in-emulator except storageCb. FR/LG: storage is a DIFFERENT module whose statics were
	// not re-derived this slice -> all 0 = named degradation (boxes stay GCTX_FULLUI there). RS:
	// the ROM/statics ban -> 0. Appending is the only safe edit: PROFILES[] is POSITIONAL. ---
	uint32_t storageCb;       // CB2_PokeStorage (ROM) -> GCTX_STORAGE
	uint32_t storageCbAlt;    //   other revision
	uint32_t stStorage;       // sStorage (EWRAM static ptr; +4 sInPartyMenu u8, +5 sCurrentBoxOption
	                          //   u8 0=WITHDRAW 1=DEPOSIT 2=MOVE_MONS 3=MOVE_ITEMS)
	uint32_t stCursor;        // sCursorArea (u8; +1 sCursorPosition, +2 sIsMonBeingMoved,
	                          //   +3 sMovingMonOrigBoxId, +4 sMovingMonOrigBoxPos — contiguous)
	uint32_t pcStoragePtr;    // gPokemonStoragePtr (deref -> +0 currentBox u8, +4 boxes[14][30]
	                          //   of 80-byte BoxPokemon; flags byte +19 bit1 = hasSpecies).
	                          //   LOGGING/proof only (the g_touchDbg occupancy mask).
	// --- phase 22.2 (lane A) — the TRAVERSAL family (HM-aware routing; SPEC-family-traversal
	// §1.4/§3.1). Both values were RE-READ from the pret byte-matched symbol maps THIS session
	// (2026-08-14 scratchpad syms/: pokeemerald.sym, pokefirered[_rev1].sym,
	// pokeleafgreen[_rev1].sym, pokeruby[_rev1].sym, pokesapphire[_rev1].sym) rather than copied
	// from the spec — the house zero-guess rule; the spec's values agreed on every overlap.
	//
	// gPlayerParty (symbol size 0x258 == 6 x 100 in ALL NINE maps, which is itself the proof the
	// party stride is 100 and PARTY_SIZE is 6):
	//   BPEE 0x020244EC | BPRE 0x02024284 (rev0 == rev1) | BPGE 0x02024284 (LG's OWN maps, rev0
	//   == rev1) | AXVE/AXPE 0x03004360 — note RS's party lives in IWRAM, not EWRAM, which is why
	//   fieldtrav_party_has_move accepts bank 0x02 AND 0x03 instead of hard-coding one map.
	// It is read ONLY through fieldtrav_party_has_move, which verifies each mon's own Gen-3
	// checksum before believing a single move id, so a wrong address degrades to "no mon knows
	// that HM" (= no conditional edges = today's walk-only behaviour), never to a phantom HM.
	//
	// gMapGroups is the ROM map-header table the SLICE-2 excursion search walks. It is the one
	// value in this struct that is REV-SENSITIVE **and dereferenced** (every other rev-alternate
	// is compare-only, hence fail-safe by construction):
	//   BPEE 0x08486578 | BPRE rev0 0x083526A8 / rev1 0x08352718 | BPGE rev0 0x08352688 /
	//   rev1 0x083526F8 | AXVE 0x083085A0 | AXPE 0x08308530 (per-title: Ruby and Sapphire DIFFER,
	//   the LANE-B drift lesson).
	// Because profile_for keys on the 4-char game code and cannot see the header's revision byte,
	// the alternate is carried alongside and the excursion planner picks between them with a
	// SELF-VALIDATING probe: whichever table resolves the CURRENT (mapGroup,mapNum) to a header
	// whose mapLayout matches the LIVE gMapHeader's is the right one. A rev mismatch therefore
	// fails the probe and disables excursions rather than dereferencing a wrong pointer.
	// 0 in either slot = named degradation: no excursions for that game, same-map HM routing
	// (slice 1) is unaffected. Appending is the only safe edit: PROFILES[] is POSITIONAL. ---
	uint32_t partyBase;       // gPlayerParty (100-byte stride, 6 slots)
	uint32_t mapGroupsRom;    // gMapGroups (ROM: MapHeader** per group) — slice 2
	uint32_t mapGroupsRomAlt; // gMapGroups, other revision (0 = none)
	// --- phase 23 (lane B) — FAM-DLG's PAGER opt-in list. The tap-advance family gives every
	// GCTX_TITLE / GCTX_FULLUI screen tap=A + hold=B + drag=D-pad; this list names the FEW screens
	// where LEFT/RIGHT is additionally a real PAGE-or-VALUE verb, and only they get the left/right
	// EDGE-ZONE taps (touchgeom.h DLGGEOM_EDGE_PX). It is deliberately an opt-in whitelist, not a
	// class-wide rule: on a dialog an edge tap must still be A, or the box "doesn't advance".
	//
	// Every value is a cb2 ALREADY in this row's cb2FullUi list (that is what makes the screen
	// reach the family at all) — so nothing new was harvested and nothing can start detecting that
	// did not detect before; the list only re-classifies taps on screens the census already proved
	// [exact]. Provenance = CB2-HARVEST.md, live-read on the running games.
	//   BPEE  summary MainCB2 0x081BFAB4 (pokemon_summary_screen.c) — VERIFIED-SRC that LEFT/RIGHT
	//         is the page verb: pokeemerald src/pokemon_summary_screen.c Task_HandleInput calls
	//         ChangePage(taskId, -1) on DPAD_LEFT / +1 on DPAD_RIGHT.
	//   BPEE  options MainCB2 0x080BA4B0 (option_menu.c) — LEFT/RIGHT is the VALUE slider on the
	//         selected row (Task_OptionMenuProcessInput -> the per-row *_ProcessInput handlers all
	//         branch on DPAD_RIGHT / DPAD_LEFT); TOUCH-PLAN A5's "tap L/R halves of value".
	//   BPRE  summary CB2_RunPokemonSummaryScreen 0x08137F60, options CB2_InitOptionMenu
	//         0x08088370 (FR keeps the init symbol as the run loop — CB2-HARVEST FR row A5).
	//   BPGE / AXVE / AXPE: 0. LG's whole cb2FullUi list is still empty (its harvest is its own
	//         slice) and RS is under the ROM-address ban — so a pager entry would be unreachable
	//         at best and wrong at worst. NAMED degradation: those games get the class default
	//         (tap=A / hold=B / drag) on whatever screens they do classify, with no edge zones.
	// 0 = unused slot and never matches (a cb2 is never 0). Appending is the only safe edit:
	// PROFILES[] is POSITIONAL-initialised.
	uint32_t cb2Pager[GS_N_PAGER];
	// --- phase 24 (lane B1) — sLockFieldControls: THE "a script owns the field" byte -------------
	// Why a new column when `fieldMsgMode` already exists: this lane MEASURED `fieldMsgMode` live
	// for the first time (g_touchDbg +0xC4) and it is NOT what its name and every consumer assume.
	// `sFieldMessageBoxMode` is set while the text is being PRINTED and returns to 0 the instant
	// printing finishes — i.e. it reads **0 for the whole time the box sits waiting for A**, which
	// is precisely when a player touches the screen. Evidence: 20 consecutive emulated frames of
	// `msgMode = 0` with the box visibly up and the ▼ prompt drawn
	// (LANE-B-TAPVERIFY.md Entry 3 + evidence/impl/EM-msgmode-zero-while-box-waits.png).
	//
	// `sLockFieldControls` (pokeemerald renamed `ScriptContext2_Enable/Disable` to
	// `LockPlayerFieldControls`/`UnlockPlayerFieldControls`) is the canonical flag instead: set for
	// the WHOLE script-driven sequence — dialog, cutscene, forced movement — and cleared when the
	// player gets control back. It is IWRAM, so it is revision-INSENSITIVE (the rev0 and rev1 sym
	// maps agree exactly for both FRLG titles).
	//   BPEE 0x03000F2C  pokeemerald.sym  `03000f2c l 00000001 sLockFieldControls`
	//   BPRE / BPGE 0x03000F9C  pokefirered.sym AND pokefirered_rev1.sym AND both LG maps agree
	//   AXVE / AXPE 0x030006A4  pokeruby_rev2.sym / pokesapphire_rev2.sym (identical)
	// 0 = named degradation: that game keeps the old textDlg-only behaviour, never a wrong read.
	//
	// SAFETY ARGUMENT for letting it re-route touch: when field controls are locked the player
	// CANNOT MOVE, so a tap-to-walk route is doomed by construction. Handing those frames to
	// FAM-DLG therefore cannot take working behaviour away — it can only replace a route that was
	// going to stall with the A the player actually wanted.
	uint32_t fieldLock;
	// --- phase 24 (lane B2) — FAM-MAP: the region map / TAP-TO-FLY anchors -----------------------
	// TOUCH-PLAN rows B7/B8. Three columns carry the whole family because the engine keeps ONE
	// module-static pointer for every region-map instance:
	//   rmPtr    sRegionMap — a POINTER to the live `struct RegionMap` (pokeemerald.sym
	//            `0203a144 l 00000004 sRegionMap`; InitRegionMapData does `sRegionMap = regionMap`
	//            for the wall map, the fly map's embedded sFlyMap->regionMap and PokeNav alike).
	//            DEREFERENCED, so it is the one value here that must be right — every read is
	//            guarded (EWRAM bank + cursor in range + zoomed == 0) and a failed guard emits
	//            NOTHING, which is the shipped FULLUI behaviour, never a wrong key.
	//   rmFlyCb  the FLY map's run-loop cb2 (EM CB2_FlyMap 0x081248D4). On this screen an arrival
	//            A is emitted, but only when the live mapSecType is CITY_CANFLY/BATTLE_FRONTIER —
	//            the same test the game's own CB_HandleFlyMapInput makes.
	//   rmWallCb the FIELD/WALL map's RUN loop (EM MCB2_FieldUpdateRegionMap 0x08170274 — the
	//            census correction: CB2_FieldShowRegionMap is only the setup). Cursor-only: on
	//            this screen A **exits**, so a tap that fired one would close the map (M2).
	// Both cb2s are ALREADY in this row's cb2FullUi list, so this slice cannot make a screen start
	// detecting that did not detect before — it only upgrades two already-classified screens from
	// the FAM-DLG default (tap = A, drag = one D-pad edge per 14 px) to one-tap targeting.
	//
	// FR/LG: 0 — and the reason is specific, not laziness. The census harvested ONE cb2 for BOTH
	// FR screens (CB2_RegionMap 0x080C08C8, "town map AND fly map, mode internal"), so `fly` is
	// not decidable from the callback, and no pokefirered symbol map was available this session to
	// resolve FR's region-map struct pointer. Named degradation: FR keeps FAM-DLG on the map.
	// RS: 0 (the ROM/statics ban). Appending is the only safe edit: PROFILES[] is POSITIONAL.
	uint32_t rmPtr;
	uint32_t rmFlyCb;
	uint32_t rmWallCb;
	// --- phase 24 (lane B2) — the DISCOVERED-LIST whitelist: FAM-LIST's "free instantiation" ----
	// TOUCH-PLAN rows E4 (FR Berry Pouch) and E5 (FR TM Case) are described as "FAM-LIST free
	// instantiation" — a list screen that needs no new driver, only a list to point the shipped
	// one at. The obstacle was always the ANCHOR: `find_list_task` needs the screen's own input
	// task plus the data[] slot its ListMenu id hides in, and neither was resolvable for these
	// two FireRed modules this session (no pokefirered symbol map to hand).
	//
	// The way around it costs NO new addresses. Every live ListMenu in the engine owns a task
	// whose function is `ListMenuDummyTask` — that is the whole premise of the P-D discovery
	// probe, whose anchor (`lmDummyTask`/`lmDummyTaskAlt`) this row has carried since phase 22.1
	// as LOGGING ONLY. This column promotes that probe from an instrument to a driver, for a
	// NAMED SET OF SCREENS ONLY: if the live cb2 is in this list, the first live ListMenu found
	// by the dummy-task scan IS this screen's list, and it gets GCTX_LIST / LK_FULLUI.
	//
	// Three things keep it honest:
	//   * it is an OPT-IN WHITELIST of census-[exact] cb2s, never a class-wide rule — a screen
	//     that happens to keep a stale ListMenu alive cannot be hijacked unless it is named here;
	//   * a screen in the list with NO live ListMenu falls through to GCTX_FULLUI, i.e. exactly
	//     today's FAM-DLG behaviour — the failure mode is "no upgrade", never a wrong key;
	//   * the driver still applies the L10 sanity gate (`listgeom_valid`) to the discovered
	//     geometry and emits NOTHING when it fails.
	//   BPRE: CB2_BerryPouchIdle 0x0813CE78 (E4) + CB2_Idle 0x081318DC (E5, the census's catalog
	//         correction: `CB2_Idle` IS the TM Case run loop). Both are census-harvested [exact]
	//         and both already sit in this row's cb2FullUi list, so — exactly like cb2Pager and
	//         the FAM-MAP columns — this changes how a tap READS, never whether a screen detects.
	//   BPEE: 0. Emerald's Berry Pouch/TM Case do not exist (berries live in the bag), and its
	//         other list screens already have real anchors, which are strictly better.
	//   BPGE: 0 until the LG harvest slice gives it its own [exact] values (never copy FR's —
	//         the BPGE drift lesson). AXVE/AXPE: 0 (the ROM-address ban).
	// 0 = unused slot and never matches. Appending is the only safe edit: PROFILES[] is POSITIONAL.
	uint32_t cb2List[GS_N_LISTCB2];
	// --- PHASE 25 (lane C1): the INERT class (GCTX_INERT above) -------------------------------
	// cb2s that must be DETECTED and then given NOTHING. Tested before every other rule, because
	// "do not touch this screen" cannot be overridden by something more specific.
	//   BPEE: CB2_Credits 0x081754DC + CB2_StartCreditsSequence 0x08175620 (its multi-state
	//         starter, hall_of_fame.c:781 SetMainCallback2(CB2_StartCreditsSequence)).
	//   BPRE: CB2_Credits 0x080F3A60.
	//   BPGE/AXVE/AXPE: 0 — LG's whole class list is still empty (its census is its own slice) and
	//         RS ROM addresses are banned. NAMED degradation: those games keep today's behaviour.
	// VERIFIED-SYM, not [exact]: the credits play once, after the Elite Four, and Gen 3 has no
	// replay — no save on this machine can reach the screen, so this lane refused to claim a live
	// read. The read is compare-only: a wrong value simply never matches and nothing changes.
	uint32_t cb2Inert[GS_N_INERT];
	// FRLG QUEST-LOG state byte (row K4). `gQuestLogState`, EWRAM 0x0203ADFA — IDENTICAL in
	// pokefirered.sym, pokefirered_rev1.sym and BOTH LeafGreen maps, i.e. revision-insensitive
	// (checked on this machine, 2026-08-14). The game's own playback test is
	// `QL_IS_PLAYBACK_STATE` = state == QL_STATE_PLAYBACK (2) || state == QL_STATE_PLAYBACK_LAST
	// (3) (pokefirered include/constants/quest_log.h), and that is exactly what game_read applies:
	// never "non-zero", because ordinary play sits at QL_STATE_RECORDING (1) and would then be
	// mistaken for a cutscene. 0 = this game has no quest log (EM/RS) -> the guard never runs.
	uint32_t questLog;
	// --- PHASE 25 (lane D1) — FAM-MAP gains its SECOND ENGINE. Appended, so every offset above is
	// untouched and PROFILES[]'s positional initialisers stay valid.
	//
	// `rmVariant` names WHICH region-map implementation this game runs, because pokefirered's is a
	// separate one, not pokeemerald's behind a different entry point: different cell bounds,
	// different cell->pixel formula, a cursor in its OWN heap allocation, and one callback for all
	// three of its modes. GS_RMAP_NONE (0) = this game has no FAM-MAP support and nothing below is
	// read — the FR/LG/RS degradation lane B2 shipped, now still true for LG and RS only.
	//
	// `rmCurPtr` is the FireRed-only second pointer: `sMapCursor` (0x020399E4, `l 00000004` = a
	// POINTER, deref), holding x/y (s16 +0x00/+0x02), selectedMapsec (u16 +0x14) and
	// selectedMapsecType (u16 +0x16). Emerald keeps all four inside the struct `rmPtr` points at,
	// so its `rmCurPtr` is 0 and the EM read path never touches it.
	//
	// `rmCbAlt` is the rev-alternate for `rmWallCb` — needed because FireRed's ONE map callback is
	// the only FAM-MAP anchor that is a ROM address, and it MOVED between revisions
	// (`CB2_RegionMap` rev0 0x080C08B4 / rev1 0x080C08C8). The two EWRAM pointers did NOT move:
	// `sRegionMap` 0x020399D4 and `sMapCursor` 0x020399E4 are byte-identical in pokefirered.sym and
	// pokefirered_rev1.sym (both checked this session), so FireRed's map works on either cart.
	//
	//   BPEE: GS_RMAP_EM, rmCurPtr 0, rmCbAlt 0 — Emerald ships two distinct cb2s and one struct.
	//   BPRE: GS_RMAP_FR, rmPtr 0x020399D4, rmCurPtr 0x020399E4, rmFlyCb 0 (there is no separate
	//         fly callback), rmWallCb 0x080C08C8 (rev1, the user's cart) + rmCbAlt 0x080C08B4
	//         (rev0). Fly-vs-wall is then read from the LIVE struct: `sRegionMap->type` at +0x4796
	//         is REGIONMAP_TYPE_NORMAL 0 / _WALL 1 / _FLY 2 (include/region_map.h:7-12).
	//   BPGE/AXVE/AXPE: GS_RMAP_NONE. LeafGreen's region map is the same engine as FireRed's but
	//         its addresses are its own, and copying FireRed's is the exact BPGE failure mode
	//         phase 22.0 was spent undoing. Explicit zeros; named degradation.
	uint32_t rmVariant;
	uint32_t rmCurPtr;
	uint32_t rmCbAlt;
} GameProfile;

// One-pass snapshot of the live game.
typedef struct {
	bool    valid;
	GameCtx ctx;
	int     px, py;          // player tile (-1 if SaveBlock pointer not ready)
	int     actionCursor;    // 0..3 or -1
	int     moveCursor;      // 0..3 or -1
	bool    moveValid[4];
	// party
	int     partyCount;      // gPlayerPartyCount, -1 if N/A
	int     partyLayout;     // 0=SINGLE 1=DOUBLE 2=MULTI, -1 if N/A
	// double-battle target
	int     battlersCount;   // -1 if N/A
	uint8_t absentMask;      // gAbsentBattlerFlags
	uint8_t battlerPos[4];   // gBattlerPositions[0..3]
	// bag (live list-task base, computed in game_read; 0 if N/A)
	uint32_t bagListTaskBase; // gTasks + 40*listTaskId + 8 (+24 scroll, +26 row)
	// phase 22.1: GCTX_LIST resolution (LK_*). listBase = the live ListMenu struct (same shape as
	// bagListTaskBase; 0 when the kind carries none — LK_QTY/LK_DEX, or a failed slot resolve,
	// in which case the touch driver emits NOTHING while the ctx is positive: the L10 safety rule).
	uint32_t listBase;
	uint8_t  listKind;
	bool     textDlg;         // overworld: a field textbox is PRINTING (sFieldMessageBoxMode != 0 — NOT
	                          // "a box is up": it returns to 0 while the box waits for A, measured live
	                          // phase 24 lane B1. Use fieldLock for "a script owns the field".)
	bool     fieldLock;       // sLockFieldControls != 0 — a script owns the field: the player cannot
	                          // move, for the WHOLE sequence (dialog, cutscene, forced walk)
	bool     textBanner;      // overworld: the map-name banner task is live
	bool     mapFly;          // GCTX_MAP only: 1 = the FLY map (an arrival A is a fly confirm),
	                          // 0 = the wall/field map, where A EXITS — so the driver never fires
	                          // one there (touchgeom.h FAM-MAP, rule M2)
	// --- instrumentation (LOGGING ONLY; never gate touch/3D/gameplay on these) ---
	uint32_t cb1, cb2;        // raw gMain.callback1/callback2 (Thumb bit stripped) = the screen fingerprint.
	                          // cb2 is THE value an undetected screen (pokedex/townmap/summary/card/keyboard/
	                          // title) reveals when you visit it on hw, to be promoted into a profile later.
	bool     sb1Valid;        // gSaveBlock1Ptr deref valid (save loaded; px/py/map*/obj* meaningful)
	bool     ctxResolved;     // ctx came from a POSITIVE battle/menu match (true) vs the bare overworld
	                          // fall-through where undetected screens hide (false -> inspect cb2)
	int      mapGroup, mapNum;// SaveBlock1.location (-1 if sb1 not ready) — which map (for the NPC overlay)
	int      objX, objY;      // gObjectEvents[0].currentCoords true avatar tile (-1 if slot inactive)
	int      facing;          // gObjectEvents[0] facing 1=D 2=U 3=L 4=R (-1 if N/A) — verify-on-hw offset
	uint8_t  nTask;           // count of active gTasks func ptrs captured in taskFp[]
	uint32_t taskFp[8];       // active task func pointers (Thumb stripped) — IDs ambiguous-callback2 screens
	// --- link-error diagnostics (LOGGING ONLY) — see GameProfile link* fields ---
	uint32_t linkStatus;      // gLinkStatus (live bitfield)
	uint32_t linkErrBuf0, linkErrBuf1;   // sLinkErrorBuffer[0..3] / [4..7] (latched status + queue counts)
	uint32_t linkNotRecv;     // gRemoteLinkPlayersNotReceived
	uint8_t  linkErr;         // gLinkErrorOccurred (1 = the game flagged a link error)
	// PHASE 25 (lane C1): the raw GameProfile.questLog byte, published so "why did touch go dead"
	// / "why did it NOT go dead" is one gdb read instead of a theory (g_touchDbg.qlState). 0 when
	// the game has no quest log. LOGGING ONLY — the ctx decision is made in game_read.
	uint8_t  questLogState;
} GameState;

// Optional 3D-effect health, logged alongside the TOP game's row (pass NULL for the bottom game).
// Mirrors the main.c DepthSnap scalars so a wrong-looking 3D pop can be correlated with the screen.
// Newer per-sprite disparity-detail fields are APPENDED (size-tolerant, like the settings loader):
// callers that don't set them leave them 0, and the dump prints them in trailing columns so an old
// reader still parses the leading ones. Disparities are in px-at-FULL-slider (the pop_eye unit before
// it multiplies by eyeSl), so the values are slider-independent (a wrong pop is visible regardless).
typedef struct {
	uint8_t overworld, textTop, textBot;   // depth gating flags
	short   nspr, nui, nfg;                // on-screen sprite / BG0-panel / foreground-tile counts
	float   maxd;                          // strongest in-view stereoscopic depth
	short   camX, camY;                    // gFieldCamera sub-tile scroll
	// --- per-sprite stereoscopic-disparity detail (LOGGING ONLY) — proves the 3D EFFECT, not just counts.
	// All px @ full slider; mirror pop_eye's feet base = RAMP_AT(fy)+floorD and head = base+POP3D_STANDUP. ---
	float   feetMin, feetMax;              // min/max grounded-feet disparity across on-screen sprites
	float   headMin, headMax;              // min/max head disparity (feet + standup, clamped)
	short   tallOk, tallFail;              // sprites where head exceeds feet by ~POP3D_STANDUP (tall-is-taller) vs not
	uint8_t orderOk;                       // 1 = on-screen set is MONOTONIC in screen-y vs feet disparity (front-is-front)
	uint8_t s3d;                           // 1 = stereoscopic 3D engaged this frame (slider>thresh & not in menu); 0 = flat
	// --- phase 14 HD-2D tilt (LOGGING ONLY; SPEC-integration I6.4) — the level/angle actually in
	// force, so a hardware photo of a tilted screen can be read against the gate that allowed it.
	// BOTH screens ride the TOP row because gs_log_sample is only called with depth != NULL for
	// screen 0 (main.c). Appended, size-tolerantly, exactly as the header note above licenses.
	// Values are the previous frame's settled tween (the gs sample runs in the parked window, the
	// tween is stepped later in the render phase) — i.e. the angle of the frame just displayed.
	// NOTE: no tilt EDGE trigger was added (I6.6) — the ring is edge-triggered on ctx/cb2/link plus
	// a 600-frame and a 2 s heartbeat; tilt changes are already correlated with the ctx edges that
	// cause them, and a tween edge would emit ~15 rows per transition into a 1024-entry ring. ---
	uint8_t tiltLvl;                       // effective (clamped) level in force on the TOP screen, 0..3
	float   tiltAngTop, tiltAngBot;        // tweened angle in DEGREES per screen (0 = flat)
	// --- phase 15 co-op presence (LOGGING ONLY; SPEC-avatar A6.5.3) — the PEER of the TOP game,
	// appended size-tolerantly exactly as the phase-14 tilt block above was. This is the surface
	// that actually FIRES in a same-console phase: the D3 CSV's peer columns only write during a
	// wireless session, and gate P-G3 turns presence off for the whole of one (SPEC-data D4.8), so
	// the gs ring is where a "why can I not see my friend" run is read back from. Values are the
	// PREVIOUS frame's solve, like tiltLvl above: this block is stamped in the parked window and
	// presence_solve runs later, in the render phase — i.e. the state of the frame just displayed.
	// Nothing reads these back; they change no behaviour. ---
	uint8_t prLive;                        // presence_liveness: 0 none / 1 connected / 2 active
	uint8_t prDrawn;                       // 1 = the P-G ladder resolved to DRAW this game's peer
	uint8_t prReason;                      // PRES_OFF_* (0 = drawing) — "why not" without a rebuild
	int8_t  prFace;                        // peer facing 1=D 2=U 3=L 4=R (-1 = no record)
	int16_t prMapG, prMapN;                // peer map (-1 = n/a); != this row's mapG/mapN is THE
	                                       //   commonest reason the avatar is absent
	int16_t prPx, prPy;                    // peer tile (-1 = n/a)
	// --- phase 20 peer sprite (docs/phase20-peersprite/SPEC.md S4.3). Appended size-tolerantly
	// again, and for the same reason the block above exists: this is the surface a "the peer is
	// still magenta" run is read back from, and it must say WHY without a rebuild. LOGGING ONLY. ---
	uint8_t  prSprReason;                  // PSPR_R_* (0 = the peer's OWN frame is being drawn)
	uint8_t  prSprW, prSprH;               // the decoded cell size actually in the sheet
	uint16_t prSprGfx;                     // graphicsId — the FORM (normal / bike / surf / ...)
} GsDepth;

// Profile for a core's ROM (by header game code), or NULL if unknown.
const GameProfile* profile_for(GbaCore* c);

// Read the running game's live state into `out`. Returns false (out->valid=false) if no profile.
bool game_read(GbaCore* c, const GameProfile* p, GameState* out);

// --- Game-state instrumentation logger (LOGGING ONLY — never changes gameplay/touch/3D) -----------
// Edge-triggered in-memory ring: gs_log_sample appends a row ONLY when the screen (ctx) or raw
// gMain.callback2 changes, or a ~10s heartbeat elapses, per screen-slot. Flushed to one SD text file
// on gamestate_log_dump. Mirrors the netlog ring + one-shot-dump pattern (no per-frame file I/O).
void        gs_log_reset(void);   // clear the ring + per-slot edge cache (call once when a session starts)
// Phase 14 / SPEC-integration I5.3: stamp the CONSOLE MODEL and whether the 804 MHz + L2 speedup
// actually engaged into the dump header. Tilt is deliberately NOT clamped on the speedup probe (a
// .3dsx from the Homebrew Launcher can never claim it, so clamping would make the effect
// un-iterable on the whole make-and-3dslink dev loop) — instead the fact is recorded, so a slow
// hardware photo is never mistaken for a tilt cost. Call once at startup; header-only, no ring row.
void        gs_log_set_env(int isN3DS, int speedupActive);
// nowMs = a wall-clock millisecond stamp (osGetTime). Drives a WALL-CLOCK heartbeat so a FROZEN/STUCK game
// (emulated clock stopped — e.g. FireRed hanging on connect) keeps emitting rows; an emulated-frame heartbeat
// alone goes silent the instant the game stops advancing, hiding exactly the stuck state we want to capture.
void        gs_log_sample(GbaCore* c, const GameProfile* p, const GameState* gs,
                          int screen, uint16_t injKeys, const GsDepth* depth, uint32_t nowMs);  // screen: 0=top/3D, 1=bottom/touch
void        gamestate_log_dump(const char* path);   // flush the ring to an SD file (mkdir's sdmc:/cias/netlogs)
const char* gamestate_ctx_name(int ctx);            // GameCtx -> short name (for the log + HUD)
