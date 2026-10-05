/* vx_snapshot.c -- see vx_snapshot.h (3DGBA, GPLv3). Read-only copy of emulated memory. */
#include "vx_snapshot.h"

#include <string.h>

static uint32_t Rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Copy n bytes from emulated EWRAM address addr; false when the range leaves EWRAM. */
static bool CopyEwram(const VxMemSrc *src, uint32_t addr, void *dst, size_t n)
{
    if (addr < GBA_EWRAM_BASE || addr - GBA_EWRAM_BASE > GBA_EWRAM_SIZE
     || n > GBA_EWRAM_SIZE - (addr - GBA_EWRAM_BASE))
        return false;
    memcpy(dst, src->ewram + (addr - GBA_EWRAM_BASE), n);
    return true;
}

static bool CopyIwram(const VxMemSrc *src, uint32_t addr, void *dst, size_t n)
{
    if (addr < GBA_IWRAM_BASE || addr - GBA_IWRAM_BASE > GBA_IWRAM_SIZE
     || n > GBA_IWRAM_SIZE - (addr - GBA_IWRAM_BASE))
        return false;
    memcpy(dst, src->iwram + (addr - GBA_IWRAM_BASE), n);
    return true;
}

/* Little-endian u16 array from a host byte pointer. */
static void DecodeU16(uint16_t *dst, const uint8_t *raw, size_t count)
{
    for (size_t i = 0; i < count; ++i)
        dst[i] = (uint16_t)(raw[2 * i] | (raw[2 * i + 1] << 8));
}

static void CopyPalettes(VxSnapshot *snap, const VxMemSrc *src)
{
    DecodeU16(snap->plttUnfaded, src->ewram + (GBA_ADDR_PLTT_UNFADED - GBA_EWRAM_BASE), 512);
    DecodeU16(snap->pltt, src->pltt, 512);
}

bool vx_snapshot_take(VxSnapshot *snap, const VxMemSrc *src)
{
    uint8_t ptr[4];
    uint32_t w, h;

    if (snap == NULL || src == NULL || src->ewram == NULL || src->iwram == NULL
     || src->pltt == NULL || src->vram == NULL)
        return false;
    snap->valid = false;
    ++snap->seq;
    snap->dispcnt = src->dispcnt;
    snap->bldcnt = src->bldcnt;
    snap->bldalpha = src->bldalpha;
    snap->bldy = src->bldy;

    if (!CopyIwram(src, GBA_ADDR_GMAIN + GBA_OFF_MAIN_CALLBACK2, ptr, 4))
        return false;
    snap->cb2 = Rd32(ptr);
    snap->inBattle = (src->iwram[GBA_ADDR_GMAIN - GBA_IWRAM_BASE + GBA_OFF_MAIN_FLAGS]
                      & GBA_MAIN_INBATTLE_BIT) != 0;

    snap->sb1Valid = false;
    if (CopyIwram(src, GBA_ADDR_SB1_PTR, ptr, 4))
    {
        snap->sb1Ptr = Rd32(ptr);
        snap->sb1Valid = CopyEwram(src, snap->sb1Ptr, snap->sb1, sizeof(snap->sb1));
    }

    if (!CopyIwram(src, GBA_ADDR_BACKUP_LAYOUT, snap->backupLayout, sizeof(snap->backupLayout)))
        return false;
    w = Rd32(snap->backupLayout + GBA_OFF_BKL_WIDTH);
    h = Rd32(snap->backupLayout + GBA_OFF_BKL_HEIGHT);
    snap->backupMapCells = 0;
    if (w >= 1 && h >= 1 && w <= 271 && h <= 270 && w * h <= VX_BACKUP_MAP_MAX_CELLS)
    {
        if (GBA_ADDR_BACKUP_MAP - GBA_EWRAM_BASE + (size_t)(w * h) * 2u > GBA_EWRAM_SIZE)
            return false;
        DecodeU16(snap->backupMap, src->ewram + (GBA_ADDR_BACKUP_MAP - GBA_EWRAM_BASE), w * h);
        snap->backupMapCells = w * h;
    }

    if (!CopyEwram(src, GBA_ADDR_MAP_HEADER, snap->mapHeader, sizeof(snap->mapHeader))
     || !CopyEwram(src, GBA_ADDR_OBJECT_EVENTS, snap->objEvents, sizeof(snap->objEvents))
     || !CopyEwram(src, GBA_ADDR_PLAYER_AVATAR, snap->playerAvatar, sizeof(snap->playerAvatar))
     || !CopyEwram(src, GBA_ADDR_SPRITES, snap->sprites, sizeof(snap->sprites))
     || !CopyEwram(src, GBA_ADDR_PALETTE_FADE, snap->paletteFade, sizeof(snap->paletteFade)))
        return false;
    {
        uint8_t wx[0x40];

        if (!CopyEwram(src, GBA_ADDR_WEATHER, wx, sizeof(wx)))
            return false;
        snap->weather[0] = wx[GBA_OFF_WEATHER_CURR];
        snap->weather[1] = wx[GBA_OFF_WEATHER_PALSTATE];
        snap->weather[2] = wx[GBA_OFF_WEATHER_EVA];
        snap->weather[3] = wx[GBA_OFF_WEATHER_FOGH];
        snap->weather[4] = wx[GBA_OFF_WEATHER_FOGD];
    }
    CopyPalettes(snap, src);
    memcpy(snap->vramBg, src->vram, GBA_VRAM_BG_SIZE);
    memcpy(snap->vramObj, src->vram + GBA_VRAM_OBJ_OFF, GBA_VRAM_OBJ_SIZE);
    snap->valid = true;
    return true;
}
