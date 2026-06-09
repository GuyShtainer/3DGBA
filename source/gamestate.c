// gamestate.c — see gamestate.h.
#include <string.h>
#include "gamestate.h"

static const GameProfile PROFILES[] = {
	{ "BPEE", 0x03005D8Cu, 0x02022FECu },   // Pokemon Emerald
	{ "BPRE", 0x03005008u, 0x02022B4Cu },   // Pokemon FireRed
	{ "BPGE", 0x03005008u, 0x02022B4Cu },   // Pokemon LeafGreen (approx FR)
};

const GameProfile* profile_for(GbaCore* c) {
	if (!c) return NULL;
	char code[5]; gbacore_game_code(c, code);
	for (unsigned i = 0; i < sizeof PROFILES / sizeof PROFILES[0]; i++)
		if (!strncmp(code, PROFILES[i].code, 4)) return &PROFILES[i];
	return NULL;
}

bool game_state(GbaCore* c, const GameProfile* p, int* px, int* py, bool* inBattle) {
	if (!c || !p) return false;
	uint32_t sb1 = gbacore_read32(c, p->sb1ptr);
	if ((sb1 >> 24) != 0x02) { *px = *py = -1; }   // not a sane EWRAM pointer yet
	else { *px = (int16_t)gbacore_read16(c, sb1); *py = (int16_t)gbacore_read16(c, sb1 + 2); }
	*inBattle = gbacore_read32(c, p->battleFlags) != 0;
	return true;
}
