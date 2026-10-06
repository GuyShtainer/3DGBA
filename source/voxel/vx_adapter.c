/* vx_adapter.c -- snapshot decode and ROM interning for the voxel overworld (3DGBA, GPLv3).
 * Written from SPEC-port section 2 / SPEC-data section 10; field offsets live in gba_game.h.
 *
 * ROM objects (headers, layouts, tilesets, events, connections, graphics info) are decoded once
 * per GBA address into host mirror structs and never freed until the ROM changes, so the pointer
 * identity the vendored code memoises on stays valid. Payload pointers are direct host pointers
 * into the ROM buffer. */
#include "vx_adapter.h"

#include "ctr_shims.h"

#include <string.h>

#define INTERN_SLOTS 4096u
#define ARENA_BYTES (512u * 1024u)
#define SIZE_SLOTS 2048u
#define MAX_LAYOUT_CELLS VX_BACKUP_MAP_MAX_CELLS
#define MAX_OBJECT_TEMPLATES 64u
#define MAX_CONNECTIONS 16u

typedef enum
{
    K_NONE = 0,
    K_HEADER,
    K_LAYOUT,
    K_TILESET,
    K_EVENTS,
    K_CONNS,
    K_GFX
} Kind;

typedef struct
{
    uint32_t addr;
    uint8_t kind;
    void *obj;
} InternSlot;

typedef struct
{
    const void *ptr;
    uint32_t size;
} SizeSlot;

static const uint8_t *sRom;
static size_t sRomSize;
static InternSlot sIntern[INTERN_SLOTS];
static SizeSlot sSizes[SIZE_SLOTS];
static union { uint64_t align; uint8_t bytes[ARENA_BYTES]; } sArena;
static size_t sArenaUsed;

/* Host globals the vendored code reads. */
struct MapHeader gMapHeader;
struct BackupMapLayout gBackupMapLayout;
struct SaveBlock1 *gSaveBlock1Ptr;
struct ObjectEvent gObjectEvents[OBJECT_EVENTS_COUNT];
struct PlayerAvatar gPlayerAvatar;
struct Sprite gSprites[GBA_SPRITE_COUNT];
struct Main gMain;
struct Weather *gWeatherPtr;
struct PaletteFade gPaletteFade;
u16 gPlttBufferUnfaded[512];
GbaPtr gFieldEffectObjectTemplatePointers[GBA_FLDEFF_TEMPLATE_COUNT];
const GameProfile *gVxProf; /* the active game profile; NULL = the Emerald row (see gba_game.h VXP) */

static struct SaveBlock1 sSaveBlock1;
static struct Weather sWeather;
static VxSnapshot sEmptySnap;
static const VxSnapshot *sSnap = &sEmptySnap;
static VxError sError = VX_ERR_NO_ROM;

/* ---- little-endian reads ------------------------------------------------------------------ */
static uint16_t Rd16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t Rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* ---- ROM access --------------------------------------------------------------------------- */
/* Host pointer to len bytes at GBA address addr, or NULL when they are not all inside the ROM. */
static const uint8_t *RomPtr(uint32_t addr, size_t len)
{
    size_t off;

    if (sRom == NULL || (addr >> 24) != 0x08u)
        return NULL;
    off = addr - GBA_ROM_BASE;
    if (off >= sRomSize || len > sRomSize - off)
        return NULL;
    return sRom + off;
}

static bool RomWord(uint32_t addr, uint32_t *out)
{
    const uint8_t *p = RomPtr(addr, 4);

    if (p == NULL)
        return false;
    *out = Rd32(p);
    return true;
}

/* ---- arena + intern table ---------------------------------------------------------------- */
static void *Alloc(size_t n)
{
    void *p;

    n = (n + 7u) & ~(size_t)7u;
    if (n > ARENA_BYTES - sArenaUsed)
        return NULL;
    p = &sArena.bytes[sArenaUsed];
    sArenaUsed += n;
    memset(p, 0, n);
    return p;
}

static unsigned HashAddr(uint32_t addr, uint8_t kind)
{
    return (unsigned)(((addr ^ ((uint32_t)kind << 28)) * 2654435761u) >> 20) & (INTERN_SLOTS - 1u);
}

static void *InternFind(uint32_t addr, Kind kind)
{
    unsigned i = HashAddr(addr, kind);

    for (unsigned n = 0; n < INTERN_SLOTS; ++n, i = (i + 1u) & (INTERN_SLOTS - 1u))
    {
        if (sIntern[i].kind == K_NONE)
            return NULL;
        if (sIntern[i].addr == addr && sIntern[i].kind == kind)
            return sIntern[i].obj;
    }
    return NULL;
}

static bool InternPut(uint32_t addr, Kind kind, void *obj)
{
    unsigned i = HashAddr(addr, kind);

    for (unsigned n = 0; n < INTERN_SLOTS; ++n, i = (i + 1u) & (INTERN_SLOTS - 1u))
    {
        if (sIntern[i].kind == K_NONE)
        {
            sIntern[i].addr = addr;
            sIntern[i].kind = (uint8_t)kind;
            sIntern[i].obj = obj;
            return true;
        }
    }
    return false;
}

static void RegisterSize(const void *p, uint32_t size)
{
    unsigned i = (unsigned)(((uintptr_t)p >> 2) * 2654435761u >> 20) & (SIZE_SLOTS - 1u);

    for (unsigned n = 0; n < SIZE_SLOTS; ++n, i = (i + 1u) & (SIZE_SLOTS - 1u))
    {
        if (sSizes[i].ptr == NULL || sSizes[i].ptr == p)
        {
            sSizes[i].ptr = p;
            sSizes[i].size = size;
            return;
        }
    }
}

const void *Port_ResolveAssetPointer(const void *p)
{
    return p;
}

u32 Port_GetAssetSizeExact(const void *p)
{
    unsigned i;

    if (p == NULL)
        return 0;
    i = (unsigned)(((uintptr_t)p >> 2) * 2654435761u >> 20) & (SIZE_SLOTS - 1u);
    for (unsigned n = 0; n < SIZE_SLOTS; ++n, i = (i + 1u) & (SIZE_SLOTS - 1u))
    {
        if (sSizes[i].ptr == NULL)
            return 0;
        if (sSizes[i].ptr == p)
            return sSizes[i].size;
    }
    return 0;
}

u32 Port_GetSpriteFrameSize(const void *data, u32 declaredSize)
{
    return data != NULL ? declaredSize : 0;
}

const u8 *Port_PeekSpriteFramePointer(const void *data, u32 size, u32 offset)
{
    const u8 *p = data;

    if (p == NULL || size == 0 || offset >= size || p < sRom || sRom == NULL
     || (size_t)(p - sRom) + size > sRomSize)
        return NULL;
    return p + offset;
}

/* ---- ROM object decoders -------------------------------------------------------------------- */
/* A pointer to an array of 16-bit values: in ROM, 2-aligned, len bytes available. */
static const u16 *RomU16Array(uint32_t addr, size_t bytes)
{
    const uint8_t *p;

    if ((addr & 1u) != 0)
        return NULL;
    p = RomPtr(addr, bytes);
    return (const u16 *)(const void *)p;
}

static size_t Clip(uint32_t addr, size_t want)
{
    size_t off = addr - GBA_ROM_BASE;

    return want <= sRomSize - off ? want : sRomSize - off;
}

static const struct Tileset *InternTileset(uint32_t addr)
{
    struct Tileset *ts = InternFind(addr, K_TILESET);
    const uint8_t *raw;
    uint32_t tiles, pal, meta, attr;
    size_t tilesBytes;

    if (ts != NULL)
        return ts;
    raw = RomPtr(addr, GBA_ROM_TILESET_BYTES);
    if (raw == NULL || raw[0] > 1 || raw[1] > 1)
        return NULL;
    tiles = Rd32(raw + 4);
    pal = Rd32(raw + 8);
    meta = Rd32(raw + 0xC);
    attr = Rd32(raw + 0x10);
    if (RomPtr(tiles, 1) == NULL)
        return NULL;
    if (raw[0] != 0)
    {
        const uint8_t *packed = RomPtr(tiles, 4);
        size_t decoded = 0;

        tilesBytes = vx_lz77_scan(packed, Clip(tiles, 0x20000u), &decoded);
        if (tilesBytes == 0 || decoded > 0x10000u)
            return NULL;
    }
    else
        tilesBytes = Clip(tiles, 512u * TILE_SIZE_4BPP);
    {
        const u16 *palP = RomU16Array(pal, 16u * 16u * 2u);
        const u16 *metaP = RomU16Array(meta, (size_t)VXP(nPrimMetatiles) * 16u);
        const u16 *attrP = RomU16Array(attr, (size_t)VXP(nPrimMetatiles) * 2u);

        if (palP == NULL || metaP == NULL || attrP == NULL)
            return NULL;
        ts = Alloc(sizeof(*ts));
        if (ts == NULL)
            return NULL;
        ts->isCompressed = raw[0];
        ts->isSecondary = raw[1];
        ts->tiles = RomPtr(tiles, 1);
        ts->palettes = palP;
        ts->metatiles = metaP;
        ts->metatileAttributes = attrP;
        ts->gbaAddr = addr;
    }
    RegisterSize(ts->tiles, (uint32_t)tilesBytes);
    RegisterSize(ts->palettes, (uint32_t)Clip(pal, 512u));
    RegisterSize(ts->metatiles, (uint32_t)Clip(meta, (size_t)VXP(nPrimMetatiles) * 16u));
    RegisterSize(ts->metatileAttributes, (uint32_t)Clip(attr, (size_t)VXP(nPrimMetatiles) * 2u));
    return InternPut(addr, K_TILESET, ts) ? ts : NULL;
}

static const struct MapLayout *InternLayout(uint32_t addr)
{
    struct MapLayout *lay = InternFind(addr, K_LAYOUT);
    const uint8_t *raw;
    int32_t w, h;
    const struct Tileset *prim, *sec;

    if (lay != NULL)
        return lay;
    raw = RomPtr(addr, GBA_ROM_MAPLAYOUT_BYTES);
    if (raw == NULL)
        return NULL;
    w = (int32_t)Rd32(raw);
    h = (int32_t)Rd32(raw + 4);
    if (w < 1 || h < 1 || w > 271 || h > 270 || (uint32_t)w * (uint32_t)h > MAX_LAYOUT_CELLS)
        return NULL;
    prim = InternTileset(Rd32(raw + 0x10));
    sec = InternTileset(Rd32(raw + 0x14));
    if (prim == NULL || sec == NULL)
        return NULL;
    {
        const u16 *border = RomU16Array(Rd32(raw + 8), 8);
        const u16 *map = RomU16Array(Rd32(raw + 0xC), (size_t)w * (size_t)h * 2u);

        if (border == NULL || map == NULL)
            return NULL;
        lay = Alloc(sizeof(*lay));
        if (lay == NULL)
            return NULL;
        lay->width = w;
        lay->height = h;
        lay->border = border;
        lay->map = map;
        lay->primaryTileset = prim;
        lay->secondaryTileset = sec;
    }
    return InternPut(addr, K_LAYOUT, lay) ? lay : NULL;
}

static const struct MapEvents *InternEvents(uint32_t addr)
{
    struct MapEvents *ev = InternFind(addr, K_EVENTS);
    const uint8_t *raw;
    unsigned objCount, bgCount;
    size_t mark;

    if (ev != NULL)
        return ev;
    raw = RomPtr(addr, 20);
    if (raw == NULL)
        return NULL;
    objCount = raw[0];
    bgCount = raw[3];
    if (objCount > MAX_OBJECT_TEMPLATES)
        return NULL;
    mark = sArenaUsed;
    ev = Alloc(sizeof(*ev));
    if (ev == NULL)
        return NULL;
    if (objCount > 0)
    {
        const uint8_t *src = RomPtr(Rd32(raw + 4), (size_t)objCount * 0x18u);
        struct ObjectEventTemplate *dst = Alloc(sizeof(*dst) * objCount);

        if (src == NULL || dst == NULL)
        {
            sArenaUsed = mark;
            return NULL;
        }
        for (unsigned i = 0; i < objCount; ++i)
            dst[i].graphicsId = src[i * 0x18u + 1u];
        ev->objectEvents = dst;
        ev->objectEventCount = (u8)objCount;
    }
    if (bgCount > 0)
    {
        const uint8_t *src = RomPtr(Rd32(raw + 0x10), (size_t)bgCount * GBA_ROM_BGEVENT_STRIDE);
        struct BgEvent *dst = Alloc(sizeof(*dst) * bgCount);

        if (src == NULL || dst == NULL)
        {
            sArenaUsed = mark;
            return NULL;
        }
        for (unsigned i = 0; i < bgCount; ++i)
        {
            const uint8_t *e = src + i * GBA_ROM_BGEVENT_STRIDE;

            dst[i].x = Rd16(e);
            dst[i].y = Rd16(e + 2);
            dst[i].elevation = e[4];
            dst[i].kind = e[5];
        }
        ev->bgEvents = dst;
        ev->bgEventCount = (u8)bgCount;
    }
    return InternPut(addr, K_EVENTS, ev) ? ev : NULL;
}

static const struct MapConnections *InternConnections(uint32_t addr)
{
    struct MapConnections *cs = InternFind(addr, K_CONNS);
    const uint8_t *raw, *src;
    struct MapConnection *dst;
    uint32_t count, kept = 0;
    size_t mark;

    if (cs != NULL)
        return cs;
    raw = RomPtr(addr, 8);
    if (raw == NULL)
        return NULL;
    count = Rd32(raw);
    if (count < 1 || count > MAX_CONNECTIONS)
        return NULL;
    src = RomPtr(Rd32(raw + 4), (size_t)count * GBA_ROM_CONNECTION_STRIDE);
    if (src == NULL)
        return NULL;
    mark = sArenaUsed;
    cs = Alloc(sizeof(*cs));
    dst = Alloc(sizeof(*dst) * count);
    if (cs == NULL || dst == NULL)
    {
        sArenaUsed = mark;
        return NULL;
    }
    for (uint32_t i = 0; i < count; ++i)
    {
        const uint8_t *e = src + i * GBA_ROM_CONNECTION_STRIDE;
        int32_t off = (int32_t)Rd32(e + 4);

        /* A8: direction 1..6, group < 34, |offset| <= 512; a bad entry is skipped. */
        if (e[0] < 1 || e[0] > 6 || e[8] >= VXP(groupCount) || off > 512 || off < -512)
            continue;
        dst[kept].direction = e[0];
        dst[kept].offset = off;
        dst[kept].mapGroup = e[8];
        dst[kept].mapNum = e[9];
        ++kept;
    }
    cs->count = (s32)kept;
    cs->connections = dst;
    return InternPut(addr, K_CONNS, cs) ? cs : NULL;
}

/* Decodes a 28-byte map header (from ROM or from the snapshot) into out. */
static bool DecodeHeader(const uint8_t *raw, uint32_t gbaAddr, struct MapHeader *out)
{
    uint32_t lay = Rd32(raw + GBA_OFF_MH_LAYOUT);

    memset(out, 0, sizeof(*out));
    out->mapLayout = InternLayout(lay);
    if (out->mapLayout == NULL)
        return false;
    out->events = InternEvents(Rd32(raw + GBA_OFF_MH_EVENTS));
    out->connections = InternConnections(Rd32(raw + GBA_OFF_MH_CONNECTIONS));
    out->mapLayoutId = Rd16(raw + GBA_OFF_MH_LAYOUT_ID);
    out->regionMapSectionId = raw[GBA_OFF_MH_REGION_SEC];
    out->cave = raw[GBA_OFF_MH_CAVE];
    out->weather = raw[GBA_OFF_MH_WEATHER];
    out->mapType = raw[GBA_OFF_MH_MAPTYPE];
    out->gbaAddr = gbaAddr;
    return true;
}

static const struct MapHeader *InternHeader(uint32_t addr)
{
    struct MapHeader *hd = InternFind(addr, K_HEADER);
    struct MapHeader tmp;
    const uint8_t *raw;

    if (hd != NULL)
        return hd;
    raw = RomPtr(addr, GBA_MAP_HEADER_BYTES);
    if (raw == NULL || !DecodeHeader(raw, addr, &tmp))
        return NULL;
    hd = Alloc(sizeof(*hd));
    if (hd == NULL)
        return NULL;
    *hd = tmp;
    return InternPut(addr, K_HEADER, hd) ? hd : NULL;
}

const struct Tileset *vx_tileset_at(GbaPtr addr)
{
    return InternTileset(addr);
}

const struct MapHeader *GetMapHeaderFromConnection(const struct MapConnection *conn)
{
    uint32_t groupPtr, headerPtr;

    if (conn == NULL || conn->mapGroup >= VXP(groupCount))
        return NULL;
    if (!RomWord(VXP(mapGroups) + 4u * conn->mapGroup, &groupPtr)
     || !RomWord(groupPtr + 4u * conn->mapNum, &headerPtr))
        return NULL;
    return InternHeader(headerPtr);
}

const struct MapLayout *Port_GetMapLayoutById(u16 layoutId)
{
    uint32_t addr;

    if (layoutId == 0 || !RomWord(VXP(mapLayouts) + 4u * (layoutId - 1u), &addr))
        return NULL;
    return InternLayout(addr);
}

const struct ObjectEventGraphicsInfo *GetObjectEventGraphicsInfo(u8 graphicsId)
{
    uint32_t addr;
    struct ObjectEventGraphicsInfo *info;
    const uint8_t *raw;
    uint32_t imagesAddr, dataAddr;

    /* Ids past the table (variable-driven ids included) fall back to entry 0. */
    if (graphicsId >= VXP(gfxInfoCount))
        graphicsId = 0;
    if (!RomWord(VXP(gfxInfoPtrs) + 4u * graphicsId, &addr))
        return NULL;
    info = InternFind(addr, K_GFX);
    if (info != NULL)
        return info;
    raw = RomPtr(addr, 0x20);
    info = raw != NULL ? Alloc(sizeof(*info)) : NULL;
    if (info == NULL)
        return NULL;
    info->size = Rd16(raw + GBA_ROM_GFXINFO_SIZE_OFF);
    info->width = (s16)Rd16(raw + GBA_ROM_GFXINFO_WIDTH_OFF);
    info->height = (s16)Rd16(raw + GBA_ROM_GFXINFO_HEIGHT_OFF);
    imagesAddr = Rd32(raw + GBA_ROM_GFXINFO_IMAGES_OFF);
    if (RomPtr(imagesAddr, GBA_ROM_IMAGE_STRIDE) != NULL)
    {
        const uint8_t *img = RomPtr(imagesAddr, GBA_ROM_IMAGE_STRIDE);
        struct SpriteFrameImage *frame = Alloc(sizeof(*frame));

        dataAddr = Rd32(img);
        if (frame != NULL && RomPtr(dataAddr, 1) != NULL)
        {
            frame->data = RomPtr(dataAddr, 1);
            frame->size = Rd16(img + 4);
            info->images = frame;
        }
    }
    return InternPut(addr, K_GFX, info) ? info : NULL;
}

u8 GetCurrentWeather(void)
{
    return gWeatherPtr != NULL ? gWeatherPtr->currWeather : 0;
}

/* ---- weather sprite templates (BPEE, SPEC-port 4.6) ----------------------------------------- */
bool8 CtrSprite_IsVoxelWeather(const struct Sprite *sprite)
{
    static const uint32_t templates[] = {0x0854FB78u, 0x0854FC2Cu, 0x0854FC8Cu, 0x0854FD18u,
                                         0x0854FD58u, 0x0854FD8Cu, 0x0854FDC4u, 0x0854FE44u};

    if (sprite == NULL || !sprite->inUse)
        return FALSE;
    for (unsigned i = 0; i < sizeof(templates) / sizeof(templates[0]); ++i)
        if (sprite->template == templates[i])
            return TRUE;
    return FALSE;
}

/* ---- ROM lifecycle ----------------------------------------------------------------------------- */
void vx_adapter_set_rom(const uint8_t *rom, size_t size)
{
    memset(sIntern, 0, sizeof(sIntern));
    memset(sSizes, 0, sizeof(sSizes));
    sArenaUsed = 0;
    memset(gFieldEffectObjectTemplatePointers, 0, sizeof(gFieldEffectObjectTemplatePointers));
    sRom = rom;
    sRomSize = rom != NULL ? size : 0;
    sError = rom != NULL ? VX_OK : VX_ERR_NO_ROM;
    if (rom == NULL)
        return;
    for (unsigned i = 0; i < VXP(fldeffCount); ++i)
        (void)RomWord(VXP(fldeffTemplates) + 4u * i, &gFieldEffectObjectTemplatePointers[i]);
}

/* ---- per-snapshot decode ------------------------------------------------------------------------ */
static void ClearGlobals(void)
{
    memset(&gMapHeader, 0, sizeof(gMapHeader));
    memset(&gBackupMapLayout, 0, sizeof(gBackupMapLayout));
    gSaveBlock1Ptr = NULL;
    gWeatherPtr = &sWeather;
}

static void DecodeOam(struct OamData *oam, const uint8_t *raw)
{
    uint16_t a0 = Rd16(raw), a1 = Rd16(raw + 2), a2 = Rd16(raw + 4);

    oam->y = a0 & 0xFFu;
    oam->affineMode = (a0 >> 8) & 3u;
    oam->objMode = (a0 >> 10) & 3u;
    oam->mosaic = (a0 >> 12) & 1u;
    oam->bpp = (a0 >> 13) & 1u;
    oam->shape = (a0 >> 14) & 3u;
    oam->x = a1 & 0x1FFu;
    oam->matrixNum = (a1 >> 9) & 31u;
    oam->size = (a1 >> 14) & 3u;
    oam->tileNum = a2 & 0x3FFu;
    oam->priority = (a2 >> 10) & 3u;
    oam->paletteNum = (a2 >> 12) & 15u;
    oam->affineParam = Rd16(raw + 6);
}

static void DecodeSprites(const VxSnapshot *s)
{
    for (unsigned i = 0; i < GBA_SPRITE_COUNT; ++i)
    {
        const uint8_t *r = s->sprites + i * GBA_SPRITE_STRIDE;
        struct Sprite *sp = &gSprites[i];

        memset(sp, 0, sizeof(*sp));
        DecodeOam(&sp->oam, r + GBA_OFF_SP_OAM);
        sp->template = Rd32(r + GBA_OFF_SP_TEMPLATE);
        sp->x = (s16)Rd16(r + GBA_OFF_SP_X);
        sp->y = (s16)Rd16(r + GBA_OFF_SP_Y);
        sp->x2 = (s16)Rd16(r + GBA_OFF_SP_X2);
        sp->y2 = (s16)Rd16(r + GBA_OFF_SP_Y2);
        sp->centerToCornerVecX = (s8)r[GBA_OFF_SP_C2C_X];
        sp->centerToCornerVecY = (s8)r[GBA_OFF_SP_C2C_Y];
        for (unsigned k = 0; k < 8; ++k)
            sp->data[k] = (s16)Rd16(r + GBA_OFF_SP_DATA + 2u * k);
        sp->inUse = r[GBA_OFF_SP_FLAGS] & 1u;
        sp->coordOffsetEnabled = (r[GBA_OFF_SP_FLAGS] >> 1) & 1u;
        sp->invisible = (r[GBA_OFF_SP_FLAGS] >> 2) & 1u;
        sp->subpriority = r[GBA_OFF_SP_SUBPRIORITY];
    }
}

static void DecodeObjects(const VxSnapshot *s)
{
    for (unsigned i = 0; i < GBA_OBJECT_EVENT_COUNT; ++i)
    {
        const uint8_t *r = s->objEvents + i * GBA_OBJECT_EVENT_STRIDE;
        struct ObjectEvent *o = &gObjectEvents[i];

        memset(o, 0, sizeof(*o));
        o->active = r[GBA_OFF_OE_FLAGS0] & 1u;
        o->singleMovementActive = (r[GBA_OFF_OE_FLAGS0] >> 1) & 1u;
        o->heldMovementActive = (r[GBA_OFF_OE_FLAGS0] >> 6) & 1u;
        o->invisible = (r[GBA_OFF_OE_FLAGS1] >> 5) & 1u;
        o->isPlayer = r[GBA_OFF_OE_FLAGS2] & 1u;
        o->hasReflection = (r[GBA_OFF_OE_FLAGS2] >> 1) & 1u;
        o->spriteId = r[GBA_OFF_OE_SPRITE_ID];
        o->graphicsId = r[GBA_OFF_OE_GFX_ID];
        o->currentElevation = r[GBA_OFF_OE_ELEVATION] & 15u;
        o->facingDirection = r[GBA_OFF_OE_FACING] & 15u;
        o->currentCoords.x = (s16)Rd16(r + GBA_OFF_OE_CUR_X);
        o->currentCoords.y = (s16)Rd16(r + GBA_OFF_OE_CUR_X + 2);
        o->previousCoords.x = (s16)Rd16(r + GBA_OFF_OE_PREV_X);
        o->previousCoords.y = (s16)Rd16(r + GBA_OFF_OE_PREV_X + 2);
    }
    gPlayerAvatar.flags = s->playerAvatar[GBA_OFF_PA_FLAGS];
    gPlayerAvatar.spriteId = s->playerAvatar[GBA_OFF_PA_SPRITE_ID];
    gPlayerAvatar.objectEventId = s->playerAvatar[GBA_OFF_PA_OBJECT_ID];
}

static bool Fail(VxError e)
{
    sError = e;
    ClearGlobals();
    return false;
}

/* The RAM-side checks (A1-A3, A9) plus header/layout decode. */
static bool DecodeMap(const VxSnapshot *s)
{
    uint32_t bw = Rd32(s->backupLayout + GBA_OFF_BKL_WIDTH);
    uint32_t bh = Rd32(s->backupLayout + GBA_OFF_BKL_HEIGHT);
    uint32_t bmap = Rd32(s->backupLayout + GBA_OFF_BKL_MAP);

    if (bmap < VXP(backupMap) || bmap >= VXP(backupMap) + GBA_BACKUP_MAP_BYTES
     || ((bmap - VXP(backupMap)) & 1u) != 0)
        return Fail(VX_ERR_A1_MAP_PTR);
    if (s->backupMapCells == 0 || bw * bh != s->backupMapCells)
        return Fail(VX_ERR_A2_DIMS);
    if (!DecodeHeader(s->mapHeader, VXP(mapHeader), &gMapHeader))
        return Fail(VX_ERR_A4_ROM_PTR);
    if ((uint32_t)gMapHeader.mapLayout->width + 15u != bw
     || (uint32_t)gMapHeader.mapLayout->height + 14u != bh)
        return Fail(VX_ERR_A3_MISMATCH);
    gBackupMapLayout.width = (s32)bw;
    gBackupMapLayout.height = (s32)bh;
    gBackupMapLayout.map = (u16 *)(uintptr_t)(const void *)(s->backupMap + (bmap - VXP(backupMap)) / 2u);
    return true;
}

bool vx_adapter_decode(const VxSnapshot *snap)
{
    if (snap == NULL)
        return Fail(VX_ERR_SNAPSHOT);
    sSnap = snap;
    vx_video_set_bg_vram(snap->vramBg);
    if (sRom == NULL)
        return Fail(VX_ERR_NO_ROM);
    if (!snap->valid)
        return Fail(VX_ERR_SNAPSHOT);

    gMain.callback2 = snap->cb2;
    gMain.inBattle = snap->inBattle;
    DecodeObjects(snap);
    DecodeSprites(snap);
    gPaletteFade.y = (u8)((Rd16(snap->paletteFade + GBA_OFF_FADE_Y_WORD) >> 6) & 31u);
    gPaletteFade.active = (u8)((Rd16(snap->paletteFade + GBA_OFF_FADE_ACTIVE_WORD) >> 15) & 1u);
    memcpy(gPlttBufferUnfaded, snap->plttUnfaded, sizeof(gPlttBufferUnfaded));
    sWeather.currWeather = snap->weather[0];
    sWeather.palProcessingState = snap->weather[1];
    sWeather.currBlendEVA = snap->weather[2];
    sWeather.fogHSpritesCreated = snap->weather[3];
    sWeather.fogDSpritesCreated = snap->weather[4];
    gWeatherPtr = &sWeather;

    if (!DecodeMap(snap))
        return false;
    if (!snap->sb1Valid)
        return Fail(VX_ERR_A9_PLAYER);
    sSaveBlock1.pos.x = (s16)Rd16(snap->sb1 + GBA_OFF_SB1_POSX);
    sSaveBlock1.pos.y = (s16)Rd16(snap->sb1 + GBA_OFF_SB1_POSY);
    sSaveBlock1.location.mapGroup = snap->sb1[GBA_OFF_SB1_MAPGROUP];
    sSaveBlock1.location.mapNum = snap->sb1[GBA_OFF_SB1_MAPNUM];
    gSaveBlock1Ptr = &sSaveBlock1;
    if (gPlayerAvatar.objectEventId >= GBA_OBJECT_EVENT_COUNT
     || !gObjectEvents[gPlayerAvatar.objectEventId].active
     || !gObjectEvents[gPlayerAvatar.objectEventId].isPlayer)
        return Fail(VX_ERR_A9_PLAYER);
    if (gMain.callback2 != VXP(cb2Overworld) && gMain.callback2 != VXP(cb2OverworldBasic))
    {
        /* Decoded fine (battle stub and the gate read cb2); the world reports unavailable
         * through VoxelWorld_IsMapAvailable() itself. */
        sError = VX_ERR_CB2;
        return true;
    }
    sError = VX_OK;
    return true;
}

VxError vx_adapter_error(void)
{
    return sError;
}

const VxSnapshot *vx_snap(void)
{
    return sSnap;
}
