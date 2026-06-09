// gamestate.c — see gamestate.h.
#include <string.h>
#include "gamestate.h"

#define BMON_MOVES_OFF 0x0C   // BattlePokemon.moves[] offset (4x u16); struct stride 0x58

// All addresses verified vs pret's byte-matched sym maps (symbols branch). FRLG share one RAM map.
// US v1.0 (rev0) builds. cols:  sb1ptr        battleFlags   actionCur     moveCur       battleMons    bg0y
static const GameProfile PROFILES[] = {
	{ "BPEE", 0x03005D8Cu, 0x02022FECu, 0x020244ACu, 0x020244B0u, 0x02024084u, 0x02022E16u },  // Emerald
	{ "BPRE", 0x03005008u, 0x02022B4Cu, 0x02023FF8u, 0x02023FFCu, 0x02023BE4u, 0x02022976u },  // FireRed
	{ "BPGE", 0x03005008u, 0x02022B4Cu, 0x02023FF8u, 0x02023FFCu, 0x02023BE4u, 0x02022976u },  // LeafGreen
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
	if (!c || !p) return false;
	out->valid = true;

	uint32_t sb1 = gbacore_read32(c, p->sb1ptr);
	if ((sb1 >> 24) == 0x02) {                       // sane EWRAM pointer
		out->px = (int16_t)gbacore_read16(c, sb1);
		out->py = (int16_t)gbacore_read16(c, sb1 + 2);
	}

	if (gbacore_read32(c, p->battleFlags) == 0) { out->ctx = GCTX_OVERWORLD; return true; }

	// In battle: gBattle_BG0_Y is 160 while the action menu shows, 320 while the move menu shows.
	uint16_t bg0y = gbacore_read16(c, p->bg0y);
	if (bg0y == 160) {
		out->ctx = GCTX_BATTLE_ACTION;
		uint8_t a = gbacore_read8(c, p->actionCursor); out->actionCursor = (a < 4) ? a : -1;
	} else if (bg0y == 320) {
		out->ctx = GCTX_BATTLE_MOVE;
		uint8_t m = gbacore_read8(c, p->moveCursor); out->moveCursor = (m < 4) ? m : -1;
		for (int i = 0; i < 4; i++)                  // a slot exists iff its move id is non-zero
			out->moveValid[i] = gbacore_read16(c, p->battleMons + BMON_MOVES_OFF + i * 2) != 0;
	} else {
		out->ctx = GCTX_BATTLE_OTHER;
	}
	return true;
}
