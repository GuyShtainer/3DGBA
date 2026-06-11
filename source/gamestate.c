// gamestate.c — see gamestate.h. Addresses verified vs pret's byte-matched sym maps (symbols branch),
// US v1.0/rev0. FRLG share one RAM map; LeafGreen's NEW symbols are FireRed-derived (see spec Risks).
#include <string.h>
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
            0x020375BCu, 0x080D487Cu },
  { "BPRE", 0x03005008u, 0x02022B4Cu, 0x02023FF8u, 0x02023FFCu, 0x02023BE4u, 0x02022976u,
            0x0203B0A0u, 0x02024029u, 0x030030F4u, 0x0811EBA0u, 0x0811EBD0u, 0x0303011Eu,
            0x03004FE0u, 0x0802E674u, 0x03004FF4u, 0x02023BD6u, 0x02023BCCu, 0x02023D70u, 0x02023BC4u, 0x03005040u,
            0x020370F0u, 0x0806F280u, 0x0203ADE4u, 0x020204B4u, 0x020370F4u, 0x03005090u, 0x08107EE0u, 0x08108F0Cu, 0x0203AD01u,
            0x0811FB28u, 0x0809CE54u, 0x0809CC98u, 0x08122C5Cu, 0x0806F1F0u, 0x00000000u,
            0x08011100u, 0x02036E38u,
            0x0203709Cu, 0x080981ACu },
  { "BPGE", 0x03005008u, 0x02022B4Cu, 0x02023FF8u, 0x02023FFCu, 0x02023BE4u, 0x02022976u,
            0x0203B0A0u, 0x02024029u, 0x030030F4u, 0x0811EBA0u, 0x0811EBD0u, 0x0303011Eu,
            0x03004FE0u, 0x0802E674u, 0x03004FF4u, 0x02023BD6u, 0x02023BCCu, 0x02023D70u, 0x02023BC4u, 0x03005040u,
            0x020370F0u, 0x0806F280u, 0x0203ADE4u, 0x020204B4u, 0x020370F4u, 0x03005090u, 0x08107EE0u, 0x08108F0Cu, 0x0203AD01u,
            0x0811FB28u, 0x0809CE54u, 0x0809CC98u, 0x08122C5Cu, 0x0806F1F0u, 0x00000000u,
            0x08011100u, 0x02036E38u,
            0x0203709Cu, 0x080981ACu },
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

// Find the live bag ListMenu task: scan gTasks (16 entries, 40-byte stride) for the active bag input
// handler, then return its list-task base (gTasks + 40*listTaskId + 8); 0 if none.
static uint32_t find_bag_list_task(GbaCore* c, const GameProfile* p) {
	for (int t = 0; t < 16; t++) {
		uint32_t task = p->gTasksBase + 40u * (uint32_t)t;
		if (gbacore_read8(c, task + 4) == 0) continue;                 // isActive
		if ((gbacore_read32(c, task + 0) & ~1u) != p->bagHandler) continue;
		int16_t listId = (int16_t)gbacore_read16(c, task + 8);          // data[0] = listTaskId
		if (listId < 0 || listId >= 16) return 0;
		return p->gTasksBase + 40u * (uint32_t)listId + 8u;
	}
	return 0;
}

bool game_read(GbaCore* c, const GameProfile* p, GameState* out) {
	memset(out, 0, sizeof *out);
	out->px = out->py = -1; out->actionCursor = out->moveCursor = -1; out->ctx = GCTX_NONE;
	out->partyCount = out->partyLayout = out->battlersCount = -1;
	if (!c || !p) return false;
	out->valid = true;

	uint32_t sb1 = gbacore_read32(c, p->sb1ptr);
	if ((sb1 >> 24) == 0x02) {
		out->px = (int16_t)gbacore_read16(c, sb1);
		out->py = (int16_t)gbacore_read16(c, sb1 + 2);
	}
	// 'In battle' = the battle main loop is the active callback2. (gBattleTypeFlags is zeroed at battle
	// SETUP, not end, so it lingers into the overworld -> the old battleFlags test made the whole
	// post-battle field read as a battle dialog = tap-anywhere-A. callback2 returns to the field cleanly.)
	bool inBattle = p->battleMainCb && (gbacore_read32(c, p->mainCb2) & ~1u) == p->battleMainCb;

	// Menu detection is TASK-BASED (scan gTasks for the menu's active input handler): robust and
	// FAIL-SAFE — a wrong/absent address just means "not detected" (the overworld still WALKS), never a
	// false-positive that blocks walking. Bag + party run in field AND battle, so they're top-level.
	{ uint32_t lb = find_bag_list_task(c, p);                    // bag (field or battle "ITEM")
	  if (lb) { out->ctx = GCTX_BAG; out->bagListTaskBase = lb; return true; } }
	// sMenu-driven popups (party SUMMARY/SWITCH popup, YES-NO, multichoice) — field OR battle. These all
	// drive sMenu.cursorPos, so GCTX_FIELDMENU's hit-test handles them. Task-detected => fail-safe.
	if (task_active(c, p, p->selMenuTask) || task_active(c, p, p->yesNoTask) || task_active(c, p, p->multiTask)) {
		out->ctx = GCTX_FIELDMENU; return true;
	}
	if (task_active(c, p, p->partyTask)) {                       // party slot pick (field, or battle send-out/use)
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
		if (task_active(c, p, p->startMenuTask)) { out->ctx = GCTX_FIELDMENU; return true; }
		out->ctx = GCTX_OVERWORLD;
		// On-screen field text — precise per-band kill signals for the renderer's DoF. The BG0
		// text-layer scan in main.c is the game-agnostic catch-all; these are exact backups.
		// Fail-safe: unknown address -> false (the BG0 scan still covers it).
		out->textDlg    = p->fieldMsgMode && gbacore_read8(c, p->fieldMsgMode) != 0;
		out->textBanner = task_active(c, p, p->mapNameTask);
		return true;
	}

	// In battle. Target-select is its own controller state — test it first (not by bg0y).
	int bc = gbacore_read8(c, p->battlersCount);
	bool targetSel = false;
	for (int b = 0; b < bc && b < 4; b++)
		if ((gbacore_read32(c, p->ctrlFuncs + 4u * b) & ~1u) == p->chooseTarget) { targetSel = true; break; }
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
