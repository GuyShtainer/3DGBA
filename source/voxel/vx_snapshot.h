/* vx_snapshot.h -- the parked-window copy of the emulated game state the voxel renderer reads
 * (3DGBA, GPLv3). Pure C, no libctru. See SPEC-port section 3.
 *
 * vx_snapshot_take() is called on the main thread while both emulator workers are parked; it only
 * reads GBA memory (plain memcpy), never writes it. Everything the voxel code reads later in the
 * frame comes from this copy, never from live GBA memory. */
#ifndef VX_SNAPSHOT_H
#define VX_SNAPSHOT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gba_game.h"

#define VX_BACKUP_MAP_MAX_CELLS 10240u

/* Host pointers to the emulated memory blocks plus the I/O registers (read by the caller). */
typedef struct
{
    const uint8_t *ewram; /* 256 KiB at 0x02000000 */
    const uint8_t *iwram; /* 32 KiB at 0x03000000 */
    const uint8_t *pltt;  /* 1 KiB at 0x05000000 */
    const uint8_t *vram;  /* 96 KiB at 0x06000000 */
    uint16_t dispcnt, bldcnt, bldalpha, bldy;
} VxMemSrc;

typedef struct
{
    bool valid;
    uint32_t seq;
    uint16_t dispcnt, bldcnt, bldalpha, bldy;
    /* gMain */
    uint32_t cb2;
    uint8_t inBattle;
    /* save block head, via gSaveBlock1Ptr */
    bool sb1Valid;
    uint32_t sb1Ptr;
    uint8_t sb1[8];
    /* map state */
    uint8_t backupLayout[12];
    uint16_t backupMap[VX_BACKUP_MAP_MAX_CELLS];
    uint32_t backupMapCells; /* w*h, 0 when the layout was implausible */
    uint8_t mapHeader[GBA_MAP_HEADER_BYTES];
    /* objects and sprites */
    uint8_t objEvents[GBA_OBJECT_EVENT_COUNT * GBA_OBJECT_EVENT_STRIDE];
    uint8_t playerAvatar[GBA_PLAYER_AVATAR_BYTES];
    uint8_t sprites[GBA_SPRITE_COUNT * GBA_SPRITE_STRIDE];
    /* palettes and fades */
    uint8_t paletteFade[GBA_PALETTE_FADE_BYTES];
    uint8_t weather[5]; /* currWeather, palProcessingState, currBlendEVA, fogH, fogD */
    uint16_t plttUnfaded[512];
    uint16_t pltt[512]; /* hardware palette RAM, post-fade */
    uint8_t vramBg[GBA_VRAM_BG_SIZE];
    uint8_t vramObj[GBA_VRAM_OBJ_SIZE];
} VxSnapshot;

/* Fills *snap from src. Returns true when the copy is complete and plausible; false leaves
 * snap->valid false (the voxel gate then drops to flat for the frame). */
bool vx_snapshot_take(VxSnapshot *snap, const VxMemSrc *src);

#endif
