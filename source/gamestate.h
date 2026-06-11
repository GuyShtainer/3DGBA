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
	GCTX_BATTLE_OTHER     // some other battle screen (dialog/animation) — tap = advance (A)
} GameCtx;

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
	bool     textUp;          // overworld only: field textbox / map-name banner on screen (DoF suppression)
} GameState;

// Profile for a core's ROM (by header game code), or NULL if unknown.
const GameProfile* profile_for(GbaCore* c);

// Read the running game's live state into `out`. Returns false (out->valid=false) if no profile.
bool game_read(GbaCore* c, const GameProfile* p, GameState* out);
