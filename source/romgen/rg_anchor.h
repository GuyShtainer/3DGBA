/* rg_anchor.h -- the runtime anchor self-check of a game profile (Phase 34 R1, SPEC section 3.3).
 *
 * Pure C (stdint/stdbool/stddef only) so the host suite test_romgen_gameprof.c exercises the exact code the device
 * runs. vx_host.c calls the ROM checks once per bind and the RAM checks on an overworld frame. Both return the number
 * of the FIRST failing check, or 0 when every check passes. Numbers are stable (they go to voxel.log):
 *
 *   ROM checks (1..10)                          RAM checks (11..20)
 *    1 header code / revision                    11 gMain.callback2 is a thumb ROM pointer
 *    2 every anchor lies inside the image        12 sb1Ptr holds an EWRAM pointer
 *    3 mapGroups probe + group-size pin          13 backupLayout.map in EWRAM, w*h <= 10240
 *    4 mapLayouts: the all-headers rule          14 backupLayout w/h = layout w + 15 / h + 14
 *    5 General primary tileset                   15 gMapHeader.layoutId = ROM header's, for sb1's (group, num)
 *    6 Building primary tileset                  16 the player object: isPlayer, coords = sb1 pos + 7
 *    7 object graphics table                     17 the player sprite's anim table is a ROM pointer
 *    8 field-effect template table               18 gPaletteFade.y in 0..16
 *    9 weatherPtr const -> EWRAM struct          19 weather: current id 0..14, palette state 0..3
 *   10 CB2_Overworld / CB2_OverworldBasic        20 callback2 is an overworld callback and not in battle
 *
 * Only the FireRed / LeafGreen rows are checked (the Emerald row is validated by its macros and is untouched).
 */
#ifndef RG_ANCHOR_H
#define RG_ANCHOR_H

#include <stddef.h>
#include <stdint.h>

#include "rg_gameprof.h"

#define VXA_ROM_FIRST 1
#define VXA_ROM_LAST 10
#define VXA_RAM_FIRST 11
#define VXA_RAM_LAST 20

typedef struct VxaRam {
    const uint8_t *ewram;   /* 0x40000 bytes at 0x02000000 */
    const uint8_t *iwram;   /* 0x8000 bytes at 0x03000000 */
} VxaRam;

/* The ROM checks. `prof` must be a FireRed / LeafGreen row (anything else returns 1). */
int vx_anchor_check_rom(const GameProfile *prof, const uint8_t *rom, size_t size);

/* The RAM checks, valid on an overworld frame. The ROM is needed to resolve sb1's map and its layout. */
int vx_anchor_check_ram(const GameProfile *prof, const uint8_t *rom, size_t size, const VxaRam *ram);

/* The value a failing check reports in voxel.log (the offending number: pointer, id, size). */
uint32_t vx_anchor_last_value(void);

/* Short name of a check ("backupLayout width", ...) for the log; "?" for an unknown number. */
const char *vx_anchor_name(int check);

#endif
