// touch.c — see touch.h.
#include <citro2d.h>
#include "gbacore.h"   // GBAKEY_*
#include "touch.h"

u16 touch_keys(int px, int py) {
	// D-pad cross, bottom-left cluster (center ~55,180).
	if (px < 112 && py > 118) {
		int dx = px - 55, dy = py - 180;
		if (dx > -24 && dx < 24 && dy < -16) return 1 << GBAKEY_UP;
		if (dx > -24 && dx < 24 && dy >  16) return 1 << GBAKEY_DOWN;
		if (dy > -24 && dy < 24 && dx < -16) return 1 << GBAKEY_LEFT;
		if (dy > -24 && dy < 24 && dx >  16) return 1 << GBAKEY_RIGHT;
		return 0;
	}
	if (px > 252 && py > 150) return 1 << GBAKEY_A;                 // A (big, bottom-right)
	if (px > 198 && px <= 252 && py > 176) return 1 << GBAKEY_B;    // B (left of A)
	if (px > 128 && px < 192 && py > 214) return 1 << GBAKEY_START; // START (bottom-center)
	if (py < 28 && px < 56)  return 1 << GBAKEY_L;                  // L (top-left)
	if (py < 28 && px > 264) return 1 << GBAKEY_R;                  // R (top-right)
	return 0;
}

void touch_overlay(u16 held) {
	const u32 pad = C2D_Color32(0xF5, 0xD0, 0x42, 0x40);   // faint gold
	const u32 lit = C2D_Color32(0xF5, 0xD0, 0x42, 0xB0);   // bright when pressed
	#define ZONE(x,y,w,h,k) C2D_DrawRectSolid((x),(y),0.0f,(w),(h),(held & (1<<(k))) ? lit : pad)
	ZONE(33, 138, 44, 30, GBAKEY_UP);
	ZONE(33, 192, 44, 30, GBAKEY_DOWN);
	ZONE(7, 162, 32, 36, GBAKEY_LEFT);
	ZONE(71, 162, 32, 36, GBAKEY_RIGHT);
	ZONE(252, 150, 60, 60, GBAKEY_A);
	ZONE(198, 176, 50, 44, GBAKEY_B);
	ZONE(128, 214, 64, 22, GBAKEY_START);
	ZONE(4, 4, 52, 22, GBAKEY_L);
	ZONE(264, 4, 52, 22, GBAKEY_R);
	#undef ZONE
}
