// vx_fixture_frlg.h -- Phase 34 R2: builds a live-state image (EWRAM/IWRAM/palette/VRAM buffers) of a FireRed /
// LeafGreen overworld at the profile's anchor addresses, from a real ROM's map header and layout. The values are
// invented here except what is read back from the ROM image the test already holds. Header-only.
// Phase 35 S2: also Ruby / Sapphire (test_voxel_rs.c): every write goes to the bank its address names (RS keeps the
// object events in IWRAM), and a `sb1Direct` profile gets SaveBlock1 written at sb1Ptr itself instead of a pointer.
#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "vx_adapter.h"
#include "vx_snapshot.h"
#include "voxel_tree.h"

#define FR_BACKUP_MAP 0x02031DFCu   /* an EWRAM address inside the real game's backup-map buffer */
#define FR_SB1 0x0202552Cu

typedef struct { uint8_t *ewram, *iwram, *pltt, *vram; } FrState;

/* One byte at a GBA address: EWRAM (0x02...) or IWRAM (0x03...). */
static uint8_t *FrAt(FrState *s, uint32_t a)
{
    return a >= 0x03000000u ? &s->iwram[a - 0x03000000u] : &s->ewram[a - 0x02000000u];
}
static void FrE16(FrState *s, uint32_t a, uint32_t v) { FrAt(s, a)[0] = (uint8_t)v; FrAt(s, a + 1)[0] = (uint8_t)(v >> 8); }
static void FrE8(FrState *s, uint32_t a, uint32_t v) { FrAt(s, a)[0] = (uint8_t)v; }
static void FrI32(FrState *s, uint32_t a, uint32_t v)
{
    for (int i = 0; i < 4; ++i) FrAt(s, a + (uint32_t)i)[0] = (uint8_t)(v >> (8 * i));
}

/* The player at map position (px, py) of the layout of `hdr` (map group, num in SaveBlock1), facing south. */
static void FrBuildAt(FrState *s, const uint8_t *rom, const GameProfile *p, const struct MapHeader *hdr, unsigned group,
                      unsigned num, int px, int py)
{
    const struct MapLayout *lay = hdr->mapLayout;
    int w = lay->width, h = lay->height;
    uint32_t wBase = (uint32_t)rom[p->weatherPtr - 0x08000000u] | ((uint32_t)rom[p->weatherPtr - 0x08000000u + 1] << 8)
                   | ((uint32_t)rom[p->weatherPtr - 0x08000000u + 2] << 16) | ((uint32_t)rom[p->weatherPtr - 0x08000000u + 3] << 24);
    uint32_t hdrAddr = 0, sb1 = p->sb1Direct ? p->sb1Ptr : FR_SB1;

    s->ewram = calloc(1, 0x40000); s->iwram = calloc(1, 0x8000);
    s->pltt = calloc(1, 0x400); s->vram = calloc(1, 0x18000);
    FrI32(s, p->gMain + 4, p->cb2Overworld);
    if (!p->sb1Direct)
        FrI32(s, p->sb1Ptr, FR_SB1);
    FrE16(s, sb1, (uint32_t)px); FrE16(s, sb1 + 2, (uint32_t)py); FrE8(s, sb1 + 4, group); FrE8(s, sb1 + 5, num);
    FrI32(s, p->backupLayout, (uint32_t)(w + 15)); FrI32(s, p->backupLayout + 4, (uint32_t)(h + 14));
    FrI32(s, p->backupLayout + 8, FR_BACKUP_MAP);
    for (int y = 0; y < h + 14; ++y)
        for (int x = 0; x < w + 15; ++x)
        {
            int mx = x - MAP_OFFSET, my = y - MAP_OFFSET;
            uint32_t cell;

            if (mx >= 0 && my >= 0 && mx < w && my < h)
                cell = lay->map[my * w + mx];
            else
                cell = (lay->border[vx_border_cell(lay, x, y)] & 0x3FFu) | 0xC00u;
            FrE16(s, FR_BACKUP_MAP + 2u * (uint32_t)(y * (w + 15) + x), cell);
        }
    /* the 28-byte map header, copied back from the ROM through the interned header's own address */
    hdrAddr = hdr->gbaAddr;
    memcpy(s->ewram + (p->mapHeader - 0x02000000u), rom + (hdrAddr - 0x08000000u), 28);
    /* the player: object 0 (active, isPlayer), avatar index 0 */
    FrE8(s, p->objEvents, 0x01); FrE8(s, p->objEvents + 2, 0x01); FrE8(s, p->objEvents + 4, 0); FrE8(s, p->objEvents + 5, 0x00);
    FrE8(s, p->objEvents + 0xB, 3);
    FrE16(s, p->objEvents + 0x10, (uint32_t)(px + 7)); FrE16(s, p->objEvents + 0x12, (uint32_t)(py + 7));
    FrE16(s, p->objEvents + 0xC, (uint32_t)(px + 7)); FrE16(s, p->objEvents + 0xE, (uint32_t)(py + 7));
    FrE16(s, p->objEvents + 0x14, (uint32_t)(px + 7)); FrE16(s, p->objEvents + 0x16, (uint32_t)(py + 7));
    FrE8(s, p->objEvents + 0x18, 1);
    FrE8(s, p->playerAvatar, 0x21); FrE8(s, p->playerAvatar + 4, 0); FrE8(s, p->playerAvatar + 5, 0);
    /* weather: sunny (2), palette state idle (3), EVA 9 */
    FrE8(s, wBase + p->weatherOff[0], 2); FrE8(s, wBase + p->weatherOff[1], 3); FrE8(s, wBase + p->weatherOff[2], 9);
}

/* FireRed / LeafGreen: Pallet Town (3, 0). */
static void FrBuild(FrState *s, const uint8_t *rom, const GameProfile *p, const struct MapHeader *hdr, int px, int py)
{
    FrBuildAt(s, rom, p, hdr, 3, 0, px, py);
}

static void FrFree(FrState *s)
{
    free(s->ewram); free(s->iwram); free(s->pltt); free(s->vram);
    memset(s, 0, sizeof *s);
}
