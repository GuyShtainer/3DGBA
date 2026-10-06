/* vx_adapter.h -- decodes a VxSnapshot into the host globals the vendored voxel code reads, and
 * interns ROM objects (3DGBA, GPLv3). See SPEC-port section 2 and 3.4. Pure C. */
#ifndef VX_ADAPTER_H
#define VX_ADAPTER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gba_game.h"
#include "vx_lz77.h"
#include "vx_snapshot.h"

typedef enum
{
    VX_OK = 0,
    VX_ERR_NO_ROM,
    VX_ERR_SNAPSHOT,     /* snapshot not valid */
    VX_ERR_A1_MAP_PTR,   /* backup map pointer not the backup buffer */
    VX_ERR_A2_DIMS,      /* backup dimensions implausible */
    VX_ERR_A3_MISMATCH,  /* backup and layout disagree (transition in flight) */
    VX_ERR_A4_ROM_PTR,   /* header/layout/tileset pointer outside the ROM */
    VX_ERR_LAYOUT,       /* layout or tileset rejected (alignment, LZ, bounds) */
    VX_ERR_A9_PLAYER,    /* player object not assembled */
    VX_ERR_CB2           /* not an overworld callback */
} VxError;

/* FireRed/LeafGreen attributes: n little-endian u32 words at src -> n host u16 (behaviour bits 0..behMask in the low
 * bits, layer type from bits 29-30 into bits 12-13: Emerald's packing). Pure. */
void vx_intern_attrs32(const uint8_t *src, size_t n, uint16_t *out);

/* Per-game loose-data directory, no trailing slash (a static buffer, valid until the next call):
 * Emerald "sdmc:/3ds/3DGBA/voxel" (unchanged), FireRed ".../voxel/BPRE", LeafGreen ".../voxel/BPGE".
 * vx_profile_pak_path: Emerald's pak, and for FRLG a name that cannot exist (the Emerald pak is pinned to the
 * Emerald ROM and must never be offered to another game). */
const char *vx_profile_data_dir(const GameProfile *prof);
const char *vx_profile_pak_path(const GameProfile *prof);

/* True when the voxel path may draw a map of this type (gMapHeader + 0x17): towns, cities, routes, underwater and
 * ocean routes {1, 2, 3, 5, 6}. Indoor, cave and secret-base maps are handed back to the 2D frame on FRLG. */
bool vx_map_is_outdoor(unsigned mapType);

/* The ROM buffer the adapter resolves payload pointers into; NULL/0 unloads it and clears every
 * interned object. The buffer must stay unmodified while set. */
void vx_adapter_set_rom(const uint8_t *rom, size_t size);

/* Decodes snap into the host globals. Returns true when the field state is assembled and every
 * guard passed. On false the globals say "no map" (VoxelWorld_IsMapAvailable() is false).
 * The snapshot must stay alive and unmodified until the next decode. */
bool vx_adapter_decode(const VxSnapshot *snap);
VxError vx_adapter_error(void);

/* The snapshot the last decode read; an all-zero one before the first. Never NULL. */
const VxSnapshot *vx_snap(void);

/* Whether a sprite template address is one of the screen-space weather objects. */
bool8 CtrSprite_IsVoxelWeather(const struct Sprite *sprite);

#endif
