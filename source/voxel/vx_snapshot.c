/* vx_snapshot.c -- see vx_snapshot.h (3DGBA, GPLv3). Read-only copy of emulated memory. */
#include "vx_snapshot.h"

#include <string.h>

static uint32_t sWeatherBase; /* resolved weather struct address on FRLG (vx_snapshot_set_weather_base) */

void vx_snapshot_set_weather_base(uint32_t ewramAddr)
{
    sWeatherBase = ewramAddr;
}

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

/* Either bank, by address: the object events are in EWRAM (Emerald, FRLG) or IWRAM (Ruby / Sapphire). */
static bool CopyRam(const VxMemSrc *src, uint32_t addr, void *dst, size_t n)
{
    return addr >= GBA_IWRAM_BASE ? CopyIwram(src, addr, dst, n) : CopyEwram(src, addr, dst, n);
}

/* Little-endian u16 array from a host byte pointer. */
static void DecodeU16(uint16_t *dst, const uint8_t *raw, size_t count)
{
    for (size_t i = 0; i < count; ++i)
        dst[i] = (uint16_t)(raw[2 * i] | (raw[2 * i + 1] << 8));
}

static void CopyPalettes(VxSnapshot *snap, const VxMemSrc *src)
{
    DecodeU16(snap->plttUnfaded, src->ewram + (VXP(plttUnfaded) - GBA_EWRAM_BASE), 512);
    DecodeU16(snap->pltt, src->pltt, 512);
}

bool vx_snapshot_take(VxSnapshot *snap, const VxMemSrc *src)
{
    uint8_t ptr[4];
    uint32_t w, h;

    if (snap == NULL || src == NULL || src->ewram == NULL || src->iwram == NULL
     || src->pltt == NULL || src->vram == NULL || VXP(playerAvatarBytes) > sizeof(snap->playerAvatar))
        return false;
    snap->valid = false;
    ++snap->seq;
    snap->dispcnt = src->dispcnt;
    snap->bldcnt = src->bldcnt;
    snap->bldalpha = src->bldalpha;
    snap->bldy = src->bldy;

    if (!CopyIwram(src, VXP(gMain) + GBA_OFF_MAIN_CALLBACK2, ptr, 4))
        return false;
    snap->cb2 = Rd32(ptr);
    if (!CopyIwram(src, VXP(gMain) + VXP(mainFlagsOff), ptr, 1))   /* 0x439 Emerald / FRLG, 0x43D Ruby / Sapphire */
        return false;
    snap->inBattle = (ptr[0] & GBA_MAIN_INBATTLE_BIT) != 0;

    snap->sb1Valid = false;
    if (VXP(sb1Direct))   /* Ruby / Sapphire: gSaveBlock1 is the EWRAM struct itself */
    {
        snap->sb1Ptr = VXP(sb1Ptr);
        snap->sb1Valid = CopyEwram(src, snap->sb1Ptr, snap->sb1, sizeof(snap->sb1));
    }
    else if (CopyIwram(src, VXP(sb1Ptr), ptr, 4))
    {
        snap->sb1Ptr = Rd32(ptr);
        snap->sb1Valid = CopyEwram(src, snap->sb1Ptr, snap->sb1, sizeof(snap->sb1));
    }

    if (!CopyIwram(src, VXP(backupLayout), snap->backupLayout, sizeof(snap->backupLayout)))
        return false;
    w = Rd32(snap->backupLayout + GBA_OFF_BKL_WIDTH);
    h = Rd32(snap->backupLayout + GBA_OFF_BKL_HEIGHT);
    snap->backupMapCells = 0;
    snap->backupMapBase = 0;
    if (w >= 1 && h >= 1 && w <= 271 && h <= 270 && w * h <= VX_BACKUP_MAP_MAX_CELLS)
    {
        /* Emerald: a fixed EWRAM address. FireRed/LeafGreen (profile backupMap == 0): the live pointer in the
         * backup layout, which must lie in EWRAM with room for w*h cells (else no map this frame). */
        uint32_t base = VXP(backupMap) != 0 ? VXP(backupMap) : Rd32(snap->backupLayout + GBA_OFF_BKL_MAP);

        if (VXP(backupMap) == 0 && (base < GBA_EWRAM_BASE || (base & 1u) != 0
                                    || base - GBA_EWRAM_BASE + (size_t)(w * h) * 2u > GBA_EWRAM_SIZE))
            return false;
        if (base - GBA_EWRAM_BASE + (size_t)(w * h) * 2u > GBA_EWRAM_SIZE)
            return false;
        DecodeU16(snap->backupMap, src->ewram + (base - GBA_EWRAM_BASE), w * h);
        snap->backupMapCells = w * h;
        snap->backupMapBase = base;
    }

    if (!CopyEwram(src, VXP(mapHeader), snap->mapHeader, sizeof(snap->mapHeader))
     || !CopyRam(src, VXP(objEvents), snap->objEvents, sizeof(snap->objEvents))
     || !CopyEwram(src, VXP(playerAvatar), snap->playerAvatar, VXP(playerAvatarBytes))
     || !CopyEwram(src, VXP(sprites), snap->sprites, sizeof(snap->sprites))
     || !CopyEwram(src, VXP(paletteFade), snap->paletteFade, sizeof(snap->paletteFade)))
        return false;
    {
        /* Emerald's weather struct is at a fixed address; FRLG's was resolved through weatherPtr at bind time. */
        uint32_t wbase = VXP(weather) != 0 ? VXP(weather) : sWeatherBase;

        if (wbase == 0)
            return false;
        for (unsigned i = 0; i < 5; ++i)
            if (!CopyEwram(src, wbase + VXP(weatherOff)[i], &snap->weather[i], 1u))
                return false;
    }
    CopyPalettes(snap, src);
    memcpy(snap->vramBg, src->vram, GBA_VRAM_BG_SIZE);
    memcpy(snap->vramObj, src->vram + GBA_VRAM_OBJ_OFF, GBA_VRAM_OBJ_SIZE);
    snap->valid = true;
    return true;
}
