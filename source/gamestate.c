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
  { "BPEE", 0x03005D8Cu, 0x02022FECu, 0x020244ACu, 0x020244B0u, 0x02024084u, 0x02022E16u,
            0x0203CEC8u, 0x020244E9u, 0x030022C4u, 0x081B01B0u, 0x081B01E0u, 0x030022EEu,
            0x03005D60u, 0x08057824u, 0x03005D74u, 0x02024076u, 0x0202406Cu, 0x02024210u, 0x02024064u, 0x03005DC0u },
  { "BPRE", 0x03005008u, 0x02022B4Cu, 0x02023FF8u, 0x02023FFCu, 0x02023BE4u, 0x02022976u,
            0x0203B0A0u, 0x02024029u, 0x030030F4u, 0x0811EBA0u, 0x0811EBD0u, 0x0303011Eu,
            0x03004FE0u, 0x0802E674u, 0x03004FF4u, 0x02023BD6u, 0x02023BCCu, 0x02023D70u, 0x02023BC4u, 0x03005040u },
  { "BPGE", 0x03005008u, 0x02022B4Cu, 0x02023FF8u, 0x02023FFCu, 0x02023BE4u, 0x02022976u,
            0x0203B0A0u, 0x02024029u, 0x030030F4u, 0x0811EBA0u, 0x0811EBD0u, 0x0303011Eu,
            0x03004FE0u, 0x0802E674u, 0x03004FF4u, 0x02023BD6u, 0x02023BCCu, 0x02023D70u, 0x02023BC4u, 0x03005040u },
};

const GameProfile* profile_for(GbaCore* c) {
	if (!c) return NULL;
	char code[5]; gbacore_game_code(c, code);
	for (unsigned i = 0; i < sizeof PROFILES / sizeof PROFILES[0]; i++)
		if (!strncmp(code, PROFILES[i].code, 4)) return &PROFILES[i];
	return NULL;
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
	bool inBattle = gbacore_read32(c, p->battleFlags) != 0;

	// The OVERWORLD always walks. (We used to also detect a *field* party menu here, but the
	// callback2==CB2_UpdatePartyMenu compare could false-positive in the field -> ctx=GCTX_PARTY ->
	// walk never ran, only A. Party touch is now battle-only, where it's the high-value case.)
	if (!inBattle) { out->ctx = GCTX_OVERWORLD; return true; }

	// In-battle party "send out which Pokémon?": callback2 == the party-menu updater (Thumb bit masked),
	// AND the party struct reads sane (guards against a stray callback match).
	bool cbParty = (gbacore_read32(c, p->mainCb2) & ~1u) == p->cb2UpdParty;
	uint8_t menuType = 0;
	if (cbParty) {
		uint8_t mt8 = gbacore_read8(c, p->partyMenu + PM_TYPE_OFF);
		menuType = mt8 & 0x0F;                       // 0 field, 1 in-battle
		int lay = (mt8 >> 4) & 0x03, cnt = gbacore_read8(c, p->partyCount);
		if (lay <= 2 && menuType <= 1 && cnt >= 1 && cnt <= 6) { out->partyLayout = lay; out->partyCount = cnt; }
		else cbParty = false;                        // garbage -> not really the party menu
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
	} else if (cbParty && menuType == 1) {
		out->ctx = GCTX_PARTY;                        // in-battle "send out which Pokémon?"
	} else {
		out->ctx = GCTX_BATTLE_OTHER;
	}
	return true;
}
