// ctr_shim.h — the two libctru/citro2d symbols source/theme.c needs, so the palette layer
// dual-compiles on the PC host harness (CLAUDE.md rule #4). NOT a citro2d mock: theme.c does colour
// arithmetic only, and everything it touches is here. Pulled in with `-DTHEME_HOST_SHIM -include`.
#pragma once
#include <stdint.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;

// citro2d packs colours as r | g<<8 | b<<16 | a<<24 (an inline fn there, so theme.c's static tables
// use its own constant-expression RGB() macro; this is only for the THEME_* role macros).
static inline u32 C2D_Color32(u8 r, u8 g, u8 b, u8 a) {
	return ((u32)r) | ((u32)g << 8) | ((u32)b << 16) | ((u32)a << 24);
}
