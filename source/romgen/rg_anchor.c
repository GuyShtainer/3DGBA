/* rg_anchor.c -- the anchor self-check, see rg_anchor.h (Phase 34 R1, SPEC section 3.3; Phase 35 S1 adds the Ruby /
 * Sapphire rev 2 rows). GPLv3, pure C. */
#include "rg_anchor.h"

#include <string.h>

#define ROM_BASE 0x08000000u
#define EW_BASE 0x02000000u
#define EW_SIZE 0x40000u
#define IW_BASE 0x03000000u
#define IW_SIZE 0x8000u

/* Struct offsets (pokefirered@037335f numbers, the same in FireRed and LeafGreen; shared with gba_game.h). Ruby / Sapphire
 * share them all (pokeruby numbers) except gMain's inBattle byte, which the profile carries (`mainFlagsOff`). */
#define OFF_MAIN_CB2 0x04u
#define MAIN_INBATTLE 0x02u
#define OFF_MH_LAYOUT 0x00u
#define OFF_MH_LAYOUT_ID 0x12u
#define MAP_HEADER_BYTES 0x1Cu
#define OFF_LAYOUT_W 0x00u
#define OFF_LAYOUT_H 0x04u
#define OFF_LAYOUT_PRIMARY 0x10u
#define OFF_GFX_IMAGES 0x1Cu
#define OFF_TMPL_CB 0x14u
#define OFF_SP_ANIMS 0x08u   /* +0x14 (template) is a stack copy in FRLG (live: 0x03007DAC), so the anim table is the ROM-pointer test */
#define OBJ_STRIDE 0x24u
#define SPRITE_STRIDE 0x44u
#define BACKUP_MAX_CELLS 10240u
#define WEATHER_BYTES 0x740u

static uint32_t sLastValue;

uint32_t vx_anchor_last_value(void)
{
    return sLastValue;
}

static int fail(int n, uint32_t v)
{
    sLastValue = v;
    return n;
}

const char *vx_anchor_name(int check)
{
    static const char *const k[] = {
        "?", "header", "anchor outside ROM", "mapGroups probe", "mapLayouts all-headers", "General primary",
        "Building primary", "gfxInfo table", "fldeff templates", "weatherPtr", "CB2_Overworld", "callback2",
        "sb1Ptr", "backupLayout map", "backupLayout size", "mapHeader layoutId", "player object", "player sprite",
        "paletteFade y", "weather", "overworld callback"};
    return (check >= 1 && check <= 20) ? k[check] : k[0];
}

/* ---- ROM access ------------------------------------------------------------------------- */

static int rom_has(size_t size, uint32_t addr, uint32_t n)
{
    return addr >= ROM_BASE && addr - ROM_BASE <= size && n <= size - (addr - ROM_BASE);
}

static uint32_t r32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t r16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

/* Callers verify rom_has() first. */
static uint32_t rom32(const uint8_t *rom, uint32_t addr)
{
    return r32(rom + (addr - ROM_BASE));
}

static int is_rom_ptr(size_t size, uint32_t v)
{
    return rom_has(size, v, 4u);
}

static int is_thumb_rom(size_t size, uint32_t v)
{
    return (v & 1u) && rom_has(size, v & ~1u, 2u);
}

/* The header of map (group, num): its ROM address, or 0. */
static uint32_t header_of(const GameProfile *p, const uint8_t *rom, size_t size, unsigned group, unsigned num)
{
    uint32_t arr, hdr;

    if (group >= p->groupCount || num >= p->groupSizes[group] || !rom_has(size, p->mapGroups + 4u * group, 4u))
        return 0;
    arr = rom32(rom, p->mapGroups + 4u * group);
    if (!rom_has(size, arr + 4u * num, 4u))
        return 0;
    hdr = rom32(rom, arr + 4u * num);
    return rom_has(size, hdr, MAP_HEADER_BYTES) ? hdr : 0;
}

static int ewram_has(uint32_t addr, uint32_t n)
{
    return addr >= EW_BASE && addr - EW_BASE <= EW_SIZE && n <= EW_SIZE - (addr - EW_BASE);
}

/* ---- ROM checks ------------------------------------------------------------------------- */

/* The rows with harvested anchors: FireRed / LeafGreen rev 1 (Phase 34) and Ruby / Sapphire rev 2 (Phase 35). Emerald's
 * anchors are the Zallax-pinned constants and are not checked here. */
static int is_checked(const GameProfile *p)
{
    return gp_is_kanto(p) || gp_is_rs(p);
}

static uint32_t primary_of(const GameProfile *p, const uint8_t *rom, size_t size, unsigned group, unsigned num)
{
    uint32_t hdr = header_of(p, rom, size, group, num), lay;

    if (hdr == 0)
        return 0;
    lay = rom32(rom, hdr + OFF_MH_LAYOUT);
    return rom_has(size, lay, p->layoutBytes) ? rom32(rom, lay + OFF_LAYOUT_PRIMARY) : 0;
}

int vx_anchor_check_rom(const GameProfile *p, const uint8_t *rom, size_t size)
{
    static const uint8_t kFldUsed[] = {0, 3, 4, 5, 7, 11, 15, 23, 27};
    unsigned g, n, i;
    uint32_t v, prev = 0;

    sLastValue = 0;
    /* 1: header */
    if (!is_checked(p) || rom == NULL || size < 0xC0u)
        return fail(1, 0);
    if (memcmp(rom + 0xAC, p->code, 4) != 0 || rom[0xBC] != p->rev)
        return fail(1, ((uint32_t)rom[0xAC] << 24) | ((uint32_t)rom[0xAD] << 16) | ((uint32_t)rom[0xAE] << 8) | rom[0xAF]);
    /* 2: every anchor and the extent we read behind it lies inside the image */
    if (!rom_has(size, p->mapGroups, 4u * p->groupCount))
        return fail(2, p->mapGroups);
    if (!rom_has(size, p->mapLayouts, 4u * p->layoutSlots))
        return fail(2, p->mapLayouts);
    if (!rom_has(size, p->tsGeneral, p->layoutBytes) || !rom_has(size, p->tsBuilding, p->layoutBytes))
        return fail(2, p->tsGeneral);
    if (!rom_has(size, p->gfxInfoPtrs, 4u * p->gfxInfoCount))
        return fail(2, p->gfxInfoPtrs);
    if (!rom_has(size, p->fldeffTemplates, 4u * p->fldeffCount))
        return fail(2, p->fldeffTemplates);
    if (!rom_has(size, p->weatherPtr, 4u))
        return fail(2, p->weatherPtr);
    if (!rom_has(size, p->cb2Overworld & ~1u, 0x30u) || !rom_has(size, p->cb2OverworldBasic & ~1u, 0x10u))
        return fail(2, p->cb2Overworld);
    /* 3: the group table: ROM pointers, ascending, each group's span equals the pinned map count */
    for (g = 0; g < p->groupCount; g++) {
        v = rom32(rom, p->mapGroups + 4u * g);
        if (!is_rom_ptr(size, v) || (g > 0 && v <= prev))
            return fail(3, v);
        if (g > 0 && (v - prev) != 4u * p->groupSizes[g - 1])
            return fail(3, g - 1u);
        prev = v;
    }
    if (!is_rom_ptr(size, rom32(rom, rom32(rom, p->mapGroups + 4u * (p->groupCount - 1u)))))
        return fail(3, p->groupCount - 1u);
    /* 4: the all-headers rule: every map's layout pointer equals the table entry its layoutId names */
    for (g = 0; g < p->groupCount; g++) {
        for (n = 0; n < p->groupSizes[g]; n++) {
            uint32_t hdr = header_of(p, rom, size, g, n), id;

            if (hdr == 0)
                return fail(4, (g << 8) | n);
            id = r16(rom + (hdr - ROM_BASE) + OFF_MH_LAYOUT_ID);
            if (id < 1u || id > p->layoutSlots || rom32(rom, p->mapLayouts + 4u * (id - 1u)) != rom32(rom, hdr + OFF_MH_LAYOUT))
                return fail(4, (g << 8) | n);
        }
    }
    /* 5, 6: the two primary tilesets, read from an outdoor and an indoor map whose layouts use them: FRLG (3, 0) / (4, 0),
     * Ruby / Sapphire (0, 0) / (1, 0) (Petalburg City and a house of Littleroot, measured: their layouts' primary pointers) */
    g = gp_is_rs(p) ? 0u : 3u;
    v = primary_of(p, rom, size, g, 0);
    if (v != p->tsGeneral)
        return fail(5, v);
    v = primary_of(p, rom, size, g + 1u, 0);
    if (v != p->tsBuilding)
        return fail(6, v);
    /* 7: the object graphics table: ROM pointers to records whose images pointer is a ROM pointer */
    for (i = 0; i < p->gfxInfoCount; i++) {
        v = rom32(rom, p->gfxInfoPtrs + 4u * i);
        if (!rom_has(size, v, 0x24u) || !is_rom_ptr(size, rom32(rom, v + OFF_GFX_IMAGES)))
            return fail(7, i);
    }
    /* 8: the field-effect template table: NULL or ROM pointers; the indices the renderer uses carry a callback */
    for (i = 0; i < p->fldeffCount; i++) {
        v = rom32(rom, p->fldeffTemplates + 4u * i);
        if (v != 0 && !rom_has(size, v, 0x18u))
            return fail(8, i);
    }
    for (i = 0; i < sizeof kFldUsed; i++) {
        if (kFldUsed[i] >= p->fldeffCount)
            continue;
        v = rom32(rom, p->fldeffTemplates + 4u * kFldUsed[i]);
        if (v == 0 || !is_thumb_rom(size, rom32(rom, v + OFF_TMPL_CB)))
            return fail(8, kFldUsed[i]);
    }
    /* 9: gWeatherPtr's ROM constant holds an EWRAM address with the whole struct behind it */
    v = rom32(rom, p->weatherPtr);
    if ((v & 3u) != 0 || !ewram_has(v, WEATHER_BYTES))
        return fail(9, v);
    for (i = 0; i < 5; i++)
        if (p->weatherOff[i] >= WEATHER_BYTES)
            return fail(9, p->weatherOff[i]);
    /* 10: both callbacks are thumb pointers; CB2_Overworld's literal pool holds &gPaletteFade, which ties the two
     * anchors together; CB2_OverworldBasic is a push {lr} thunk */
    if (!is_thumb_rom(size, p->cb2Overworld) || !is_thumb_rom(size, p->cb2OverworldBasic))
        return fail(10, p->cb2Overworld);
    if (r16(rom + (p->cb2OverworldBasic & ~1u) - ROM_BASE) != 0xB500u)
        return fail(10, p->cb2OverworldBasic);
    {
        const uint8_t *fn = rom + ((p->cb2Overworld & ~1u) - ROM_BASE);
        int found = 0;

        for (i = 0; i + 4u <= 0x30u; i += 4u)
            if (r32(fn + i) == p->paletteFade)
                found = 1;
        if (!found)
            return fail(10, p->paletteFade);
    }
    sLastValue = 0;
    return 0;
}

/* ---- RAM checks ------------------------------------------------------------------------- */

static const uint8_t *ew_at(const VxaRam *r, uint32_t addr, uint32_t n)
{
    return ewram_has(addr, n) ? r->ewram + (addr - EW_BASE) : NULL;
}

static const uint8_t *iw_at(const VxaRam *r, uint32_t addr, uint32_t n)
{
    if (addr < IW_BASE || addr - IW_BASE > IW_SIZE || n > IW_SIZE - (addr - IW_BASE))
        return NULL;
    return r->iwram + (addr - IW_BASE);
}

/* Either work RAM bank, by address (the object events are in EWRAM on FRLG, in IWRAM on Ruby / Sapphire). */
static const uint8_t *ram_at(const VxaRam *r, uint32_t addr, uint32_t n)
{
    return addr >= IW_BASE ? iw_at(r, addr, n) : ew_at(r, addr, n);
}

int vx_anchor_check_ram(const GameProfile *p, const uint8_t *rom, size_t size, const VxaRam *ram)
{
    const uint8_t *main_, *sb1, *bkl, *mh, *pa, *obj, *spr, *pf, *wp;
    uint32_t cb2, sb1a, w, h, map, hdr, lay, id, v;
    int sx, sy;
    unsigned grp, num, oid, sid;

    sLastValue = 0;
    if (!is_checked(p) || rom == NULL || ram == NULL || ram->ewram == NULL || ram->iwram == NULL)
        return fail(11, 0);
    /* 11: callback2 */
    main_ = iw_at(ram, p->gMain, p->mainFlagsOff + 1u);
    if (main_ == NULL || p->mainFlagsOff <= OFF_MAIN_CB2)
        return fail(11, 0);
    cb2 = r32(main_ + OFF_MAIN_CB2);
    if (!is_thumb_rom(size, cb2))
        return fail(11, cb2);
    /* 12: sb1: through the IWRAM pointer (FRLG), or the EWRAM struct itself (Ruby / Sapphire) */
    if (p->sb1Direct) {
        sb1a = p->sb1Ptr;
    } else {
        const uint8_t *s = iw_at(ram, p->sb1Ptr, 4u);
        sb1a = (s != NULL) ? r32(s) : 0;
    }
    sb1 = ew_at(ram, sb1a, 8u);
    if (sb1 == NULL || (sb1a & 3u) != 0)
        return fail(12, sb1a);
    sx = (int16_t)r16(sb1 + 0);
    sy = (int16_t)r16(sb1 + 2);
    grp = sb1[4];
    num = sb1[5];
    /* 13: the backup layout's map buffer */
    bkl = iw_at(ram, p->backupLayout, 12u);
    if (bkl == NULL)
        return fail(13, 0);
    w = r32(bkl + 0);
    h = r32(bkl + 4);
    map = r32(bkl + 8);
    if (w < 1u || h < 1u || w > 271u || h > 270u || w * h > BACKUP_MAX_CELLS || !ewram_has(map, 2u * w * h))
        return fail(13, map);
    /* 14: its size follows from the ROM layout of sb1's map: width + 15, height + 14 */
    hdr = header_of(p, rom, size, grp, num);
    if (hdr == 0)
        return fail(14, (grp << 8) | num);
    lay = rom32(rom, hdr + OFF_MH_LAYOUT);
    if (!rom_has(size, lay, p->layoutBytes))
        return fail(14, lay);
    if (w != rom32(rom, lay + OFF_LAYOUT_W) + 15u)
        return fail(14, w);
    if (h != rom32(rom, lay + OFF_LAYOUT_H) + 14u)
        return fail(14, h | 0x10000u);
    /* 15: the live map header's layoutId equals the ROM header's */
    mh = ew_at(ram, p->mapHeader, MAP_HEADER_BYTES);
    if (mh == NULL)
        return fail(15, 0);
    id = r16(mh + OFF_MH_LAYOUT_ID);
    if (id != r16(rom + (hdr - ROM_BASE) + OFF_MH_LAYOUT_ID))
        return fail(15, id);
    /* 16: the player object */
    pa = ew_at(ram, p->playerAvatar, p->playerAvatarBytes);
    obj = ram_at(ram, p->objEvents, 16u * OBJ_STRIDE);
    if (pa == NULL || obj == NULL)
        return fail(16, 0);
    oid = pa[5];
    if (oid >= 16u)
        return fail(16, oid);
    obj += oid * OBJ_STRIDE;
    if ((obj[2] & 1u) == 0)
        return fail(16, oid);
    if ((int16_t)r16(obj + 0x10) != sx + 7 || (int16_t)r16(obj + 0x12) != sy + 7)
        return fail(16, ((uint32_t)r16(obj + 0x10) << 16) | r16(obj + 0x12));
    /* 17: the player's sprite */
    sid = pa[4];
    spr = ew_at(ram, p->sprites, 65u * SPRITE_STRIDE);
    if (spr == NULL || sid >= 65u)
        return fail(17, sid);
    v = r32(spr + sid * SPRITE_STRIDE + OFF_SP_ANIMS);
    if (!is_rom_ptr(size, v))
        return fail(17, v);
    /* 18: the palette fade's y field */
    pf = ew_at(ram, p->paletteFade, 12u);
    if (pf == NULL || ((r16(pf + 4) >> 6) & 31u) > 16u)
        return fail(18, pf ? (unsigned)((r16(pf + 4) >> 6) & 31u) : 0u);
    /* 19: weather, through the ROM constant */
    v = rom_has(size, p->weatherPtr, 4u) ? rom32(rom, p->weatherPtr) : 0;
    wp = ew_at(ram, v, WEATHER_BYTES);
    if (wp == NULL)
        return fail(19, v);
    if (wp[p->weatherOff[0]] > 14u)
        return fail(19, wp[p->weatherOff[0]]);
    if (wp[p->weatherOff[1]] > 3u)
        return fail(19, 0x100u | wp[p->weatherOff[1]]);
    /* 20: an overworld callback, not in battle (the gMain +4 / +mainFlagsOff offsets are consistent) */
    if (cb2 != p->cb2Overworld && cb2 != p->cb2OverworldBasic)
        return fail(20, cb2);
    if ((main_[p->mainFlagsOff] & MAIN_INBATTLE) != 0)
        return fail(20, main_[p->mainFlagsOff]);
    sLastValue = 0;
    return 0;
}
