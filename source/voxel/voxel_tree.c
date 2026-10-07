/* Modified for 3DGBA (GPLv3), 2026.
 * Original: ZallaxDev/pokeemerald-3Ds-dualscreen @ c330c0a, 3ds_port/src/voxel/voxel_tree.c,
 * MIT License - see source/voxel/NOTICE.md. */
#include <stddef.h>
#include <string.h>
#include "voxel_tree.h"
#include "voxel_relief.h"
#include "gba_game.h" /* 3DGBA: VXP() */
#include "voxel_atlas.h" /* 3DGBA: VOXEL_SHRUBS */

/* 3DGBA (Phase 34 T1): the tables live in the game profile (treePart / treeGround, flat int16 pairs, see
 * rg_gameprof.h) so each game brings its own metatile ids. They are expanded once per profile into two lookup tables:
 * these functions run per cell, per frame, and the ground map is walked over all 1024 ids on every atlas build. */
#define TREE_LUT_IDS 1024

static const GameProfile *sLutFor;
static int8_t sPartLut[TREE_LUT_IDS];
static int16_t sGroundLut[TREE_LUT_IDS];
/* 3DGBA (look L1): the profile's shrubs by id: for a primary id its shrub index, for a secondary id the first entry with
 * that id (the tileset is then checked along the run of entries with the same id). -1 = none. */
static int8_t sShrubLut[TREE_LUT_IDS];

static void TreeLut(void)
{
    const GameProfile *p = vx_prof();
    unsigned i;

    if (p == sLutFor)
        return;
    for (i = 0; i < TREE_LUT_IDS; ++i)
    {
        sPartLut[i] = -1;
        sGroundLut[i] = (int16_t)i;
        sShrubLut[i] = -1;
    }
    for (i = 0; p->shrubs != NULL && i < p->shrubCount && i < VOXEL_SHRUBS; ++i)
        if (p->shrubs[i].metatile < TREE_LUT_IDS && sShrubLut[p->shrubs[i].metatile] < 0)
            sShrubLut[p->shrubs[i].metatile] = (int8_t)i;
    for (i = 0; p->treePart != NULL && i < p->treePartCount; ++i)
        if ((unsigned)p->treePart[2 * i] < TREE_LUT_IDS)
            sPartLut[p->treePart[2 * i]] = (int8_t)p->treePart[2 * i + 1];
    for (i = 0; p->treeGround != NULL && i < p->treeGroundCount; ++i)
        if ((unsigned)p->treeGround[2 * i] < TREE_LUT_IDS)
            sGroundLut[p->treeGround[2 * i]] = p->treeGround[2 * i + 1];
    sLutFor = p;
}

int VoxelTree_Part(int metatileId)
{
    TreeLut();
    return (unsigned)metatileId < TREE_LUT_IDS ? sPartLut[metatileId] : -1;
}

int VoxelTree_GroundMetatile(int metatileId)
{
    TreeLut();
    return (unsigned)metatileId < TREE_LUT_IDS ? sGroundLut[metatileId] : metatileId;
}

/* 3DGBA (look L1): whether shrub entry k is drawn by this tileset pair. A primary entry needs the General tileset, a
 * secondary one its own secondary tileset, compared by ROM address (the interned pointers are reused across ROMs). */
static bool ShrubOnPair(const GameProfile *p, unsigned k, const struct Tileset *primary,
                        const struct Tileset *secondary)
{
    const GpShrub *e = &p->shrubs[k];

    if (e->tileset == 0)
        return e->metatile < VXP(nPrimMetatiles) && primary != NULL && primary->gbaAddr == VXP(tsGeneral);
    return e->metatile >= VXP(nPrimMetatiles) && secondary != NULL && secondary->gbaAddr == e->tileset;
}

static int ShrubOf(const struct Tileset *primary, const struct Tileset *secondary, int metatileId)
{
    const GameProfile *p = vx_prof();
    unsigned k;

    TreeLut();
    if ((unsigned)metatileId >= TREE_LUT_IDS || sShrubLut[metatileId] < 0)
        return -1;
    /* the same id can stand in several secondary tilesets: walk its entries (the table is short) */
    for (k = (unsigned)sShrubLut[metatileId]; k < p->shrubCount && k < VOXEL_SHRUBS; ++k)
        if (p->shrubs[k].metatile == metatileId && ShrubOnPair(p, k, primary, secondary))
            return (int)k;
    return -1;
}

int VoxelTree_Shrub(const VoxelMapInstance *inst, int metatileId)
{
    if (!VoxelWorld_UsesTreeSprites(inst))
        return -1;
    return ShrubOf((const struct Tileset *)inst->primaryTileset, (const struct Tileset *)inst->secondaryTileset,
                   metatileId);
}

bool VoxelTree_ShrubSource(const void *primaryTileset, const void *secondaryTileset,
                           unsigned k, unsigned *metatileId)
{
    const GameProfile *p = vx_prof();

    if (p->shrubs == NULL || k >= p->shrubCount || k >= VOXEL_SHRUBS
     || !ShrubOnPair(p, k, (const struct Tileset *)primaryTileset, (const struct Tileset *)secondaryTileset))
        return false;
    *metatileId = p->shrubs[k].metatile;
    return true;
}

/*
 * A shrub: its leaves stood up as one card, the tile's own 16x16 at its own
 * scale (one tile along the slant), leaning back at 60 degrees from the
 * cell's south edge - the bush's foot where the drawing has it - so its top
 * reaches 0.87 tiles. Like a crown card it is lit as the rounded volume it
 * stands for, and sunk a little so its foot meets the ground without a seam.
 * Two triangles; the ground under it is drawn flat by the terrain pass.
 */
void VoxelTree_EmitShrubCard(VoxelBuilder *builder, int x, int y,
                             float u0, float v0, float u1, float v1)
{
    const float rise = 0.866025f, run = 0.5f;   /* sin, cos 60 degrees */
    const float sink = -0.06f;
    float wx = (float)x, foot = (float)y + 1.0f;

    builder->rounded = true;
    VoxelBuilder_Quad(builder,
        &(VoxelVertex){wx,        sink + rise, foot - run, u0, v0, 1.0f},
        &(VoxelVertex){wx + 1.0f, sink + rise, foot - run, u1, v0, 1.0f},
        &(VoxelVertex){wx + 1.0f, sink,        foot,       u1, v1, 1.0f},
        &(VoxelVertex){wx,        sink,        foot,       u0, v1, 1.0f});
    builder->rounded = false;
}


/*
 * 3DGBA (look backlog L2): tall grass. A cell whose behaviour is in the profile's bladeGrass set stands its blades up as
 * a card. The blades are the LOWER layer's drawing (measured: the tall-grass metatile's upper layer is empty, the
 * rustle sprite is what covers the player's feet in the game), so the card is the cell's lower layer with the ground
 * colour keyed out (voxel_atlas.c) and the flat cell keeps its own ordinary drawing under it.
 *
 * Which metatiles: by behaviour, read from the tileset pair's attribute tables, in id order, so an entry's index k
 * (VOXEL_GRASS_BLADES(k)) is the same wherever the pair is asked, atlas and mesher alike. The lists are cached by the
 * pair's ROM addresses: the world asks once per cell, per frame of chunk building.
 * Left out: a metatile a tree table owns (treePart / treeGround: a tree's cell, or a canopy fringe drawn as tall grass),
 * the profile's measured exceptions (grassSkip), and any pair whose primary tileset is not the General one.
 */
#define GRASS_PAIRS 4

typedef struct GrassList
{
    const GameProfile *prof;
    uint32_t primary, secondary;   /* ROM addresses; 0 = the slot is free */
    unsigned stamp;
    uint8_t n;
    uint16_t id[VOXEL_GRASSES];
} GrassList;

static GrassList sGrassList[GRASS_PAIRS];
static unsigned sGrassStamp;

static bool GrassSkipped(const GameProfile *p, const struct Tileset *secondary, unsigned id)
{
    for (unsigned i = 0; p->grassSkip != NULL && i < p->grassSkipCount; ++i)
        if (p->grassSkip[i].metatile == id && p->grassSkip[i].tileset != 0
         && id >= p->nPrimMetatiles && secondary != NULL && secondary->gbaAddr == p->grassSkip[i].tileset)
            return true;
    return false;
}

/* The pair's list, built on first use; NULL for a pair with none or whose attribute tables are not there yet. */
static const GrassList *GrassOf(const struct Tileset *primary, const struct Tileset *secondary)
{
    const GameProfile *p = vx_prof();
    GrassList *slot = &sGrassList[0];
    const uint16_t *pa, *sa;
    unsigned nPrim, nSec, id;

    if (primary == NULL || secondary == NULL || primary->gbaAddr != p->tsGeneral)
        return NULL;
    for (unsigned i = 0; i < GRASS_PAIRS; ++i)
    {
        if (sGrassList[i].prof == p && sGrassList[i].primary == primary->gbaAddr
         && sGrassList[i].secondary == secondary->gbaAddr)
        {
            sGrassList[i].stamp = ++sGrassStamp;
            return sGrassList[i].n != 0 ? &sGrassList[i] : NULL;
        }
        if (sGrassList[i].stamp < slot->stamp)
            slot = &sGrassList[i];
    }
    pa = Voxel_ResolveAttributes(primary);
    sa = Voxel_ResolveAttributes(secondary);
    if (pa == NULL || sa == NULL)
        return NULL;   /* not loaded yet: ask again, never cache the emptiness */
    TreeLut();
    memset(slot, 0, sizeof *slot);
    nPrim = Voxel_MetatileCount(primary, p->nPrimMetatiles);
    nSec = Voxel_MetatileCount(secondary, p->nMetatilesTotal - p->nPrimMetatiles);
    for (id = 0; id < p->nPrimMetatiles + nSec && slot->n < VOXEL_GRASSES; ++id)
    {
        unsigned beh;

        if (id < p->nPrimMetatiles)
        {
            if (id >= nPrim)
                continue;
            beh = (unsigned)pa[id] & (unsigned)p->behMask;
        }
        else
        {
            beh = (unsigned)sa[id - p->nPrimMetatiles] & (unsigned)p->behMask;
        }
        if (id >= TREE_LUT_IDS || !gp_beh(&p->bladeGrass, beh) || sPartLut[id] >= 0 || sGroundLut[id] != (int16_t)id
         || GrassSkipped(p, secondary, id))
            continue;
        slot->id[slot->n++] = (uint16_t)id;
    }
    slot->prof = p;
    slot->primary = primary->gbaAddr;
    slot->secondary = secondary->gbaAddr;
    slot->stamp = ++sGrassStamp;
    return slot->n != 0 ? slot : NULL;
}

int VoxelTree_Grass(const VoxelMapInstance *inst, int metatileId)
{
    const GrassList *g;

    if (inst == NULL || metatileId < 0 || !VoxelWorld_UsesTreeSprites(inst))
        return -1;
    g = GrassOf((const struct Tileset *)inst->primaryTileset, (const struct Tileset *)inst->secondaryTileset);
    for (unsigned k = 0; g != NULL && k < g->n; ++k)
        if (g->id[k] == (unsigned)metatileId)
            return (int)k;
    return -1;
}

bool VoxelTree_GrassSource(const void *primaryTileset, const void *secondaryTileset,
                           unsigned k, unsigned *metatileId)
{
    const GrassList *g = GrassOf((const struct Tileset *)primaryTileset, (const struct Tileset *)secondaryTileset);

    if (g == NULL || k >= g->n)
        return false;
    *metatileId = g->id[k];
    return true;
}

/*
 * Two low cards per cell, each leaning back from its foot: the blades' own 16x16 squeezed onto a slant of about 0.6 tile.
 * The front card stands on the cell's south edge and rises 0.55; the back one on its middle and rises 0.40, drawn
 * mirrored so a row of cells does not repeat to the pixel. Seen from the camera the rows interleave and the field reads
 * as standing grass; one card per cell left a bare stripe of flat ground between the rows (tried: bands like a hedge),
 * two equal cards a solid dark carpet that lost the game's tufted look (tried). Lit as the rounded volume they stand
 * for, like a crown card, and sunk a little so each foot meets the ground without a seam. Two quads, 12 vertices.
 */
#define GRASS_SINK (-0.04f)
void VoxelTree_EmitGrassCard(VoxelBuilder *builder, int x, int y,
                             float u0, float v0, float u1, float v1)
{
    static const float foot[VOXEL_GRASS_CARDS] = {0.5f, 1.0f};   /* back card first: the nearer one draws over it */
    static const float rise[VOXEL_GRASS_CARDS] = {0.40f, 0.55f};
    static const float run[VOXEL_GRASS_CARDS] = {0.12f, 0.14f};
    float wx = (float)x;

    builder->rounded = true;
    for (unsigned c = 0; c < VOXEL_GRASS_CARDS; ++c)
    {
        float z = (float)y + foot[c];
        float ua = c ? u0 : u1, ub = c ? u1 : u0;   /* the back card mirrored */

        VoxelBuilder_Quad(builder,
            &(VoxelVertex){wx,        GRASS_SINK + rise[c], z - run[c], ua, v0, 1.0f},
            &(VoxelVertex){wx + 1.0f, GRASS_SINK + rise[c], z - run[c], ub, v0, 1.0f},
            &(VoxelVertex){wx + 1.0f, GRASS_SINK,           z,          ub, v1, 1.0f},
            &(VoxelVertex){wx,        GRASS_SINK,           z,          ua, v1, 1.0f});
    }
    builder->rounded = false;
}

/* The small crown is 16:32: width 1, length 2 tiles, at the same 50 degrees
 * and sunk the same way as the large one, standing on its one-cell trunk. */
static void EmitSmallCell(VoxelBuilder *builder, int x, int y)
{
    float wx = (float)x, wz = (float)y;
    const float rise = 1.532089f, run = 1.285575f;
    const float baseHeight = -0.10f;
    /* 0.15 further forward than half the large tree's: any less and the
     * leaves stand through the ground behind the trunk. */
    float baseZ = wz + 0.825f;

    VoxelMesh_Top(builder, wx, wz, 0.0f, 0.0f,
                  48.0f / VOXEL_TREE_TEXTURE_DIM, 0.5f, 1.0f, 0.25f, 1.0f);
    builder->rounded = true;
    VoxelBuilder_Quad(builder,
        &(VoxelVertex){wx,        baseHeight + rise, baseZ - run, 0.5f,  0.5f, 1.0f},
        &(VoxelVertex){wx + 1.0f, baseHeight + rise, baseZ - run, 0.75f, 0.5f, 1.0f},
        &(VoxelVertex){wx + 1.0f, baseHeight,        baseZ,       0.75f, 0.0f, 1.0f},
        &(VoxelVertex){wx,        baseHeight,        baseZ,       0.5f,  0.0f, 1.0f});
    builder->rounded = false;
}

static void EmitCell(VoxelBuilder *builder, int x, int y, int part)
{
    int col = part & 1, row = part >> 1;
    float wx = (float)x, wz = (float)y;
    float u0 = (32.0f + col * 16.0f) / VOXEL_TREE_TEXTURE_DIM;
    float u1 = u0 + 16.0f / VOXEL_TREE_TEXTURE_DIM;
    float v0 = 1.0f - row * 16.0f / VOXEL_TREE_TEXTURE_DIM;
    float v1 = v0 - 16.0f / VOXEL_TREE_TEXTURE_DIM;
    /* The crown keeps its 32:36 aspect ratio: width 2, length 2.25 tiles.
     * sin/cos(50 degrees), fixed in the world rather than camera billboarding.
     * Lower the transparent bottom margin into the ground so the visible
     * leaves overlap the trunk instead of exposing a horizontal cut. */
    const float rise = 1.723600f, run = 1.446272f;
    const float baseHeight = -0.10f;
    float baseZ = wz - row + 1.35f;
    float top = 1.0f - row * 0.5f, bottom = top - 0.5f;

    if (part == VOXEL_TREE_SMALL)
    {
        EmitSmallCell(builder, x, y);
        return;
    }
    VoxelMesh_Top(builder, wx, wz, 0.0f, 0.0f, u0, v0, u1, v1, 1.0f);

    u0 = col * 16.0f / VOXEL_TREE_TEXTURE_DIM;
    u1 = u0 + 16.0f / VOXEL_TREE_TEXTURE_DIM;
    v0 = 1.0f - row * 18.0f / VOXEL_TREE_TEXTURE_DIM;
    v1 = v0 - 18.0f / VOXEL_TREE_TEXTURE_DIM;
    /* A crown card: lit as the rounded crown it stands for. */
    builder->rounded = true;
    VoxelBuilder_Quad(builder,
        &(VoxelVertex){wx,        baseHeight + top * rise,    baseZ - top * run,    u0, v0, 1.0f},
        &(VoxelVertex){wx + 1.0f, baseHeight + top * rise,    baseZ - top * run,    u1, v0, 1.0f},
        &(VoxelVertex){wx + 1.0f, baseHeight + bottom * rise, baseZ - bottom * run, u1, v1, 1.0f},
        &(VoxelVertex){wx,        baseHeight + bottom * rise, baseZ - bottom * run, u0, v1, 1.0f});
    builder->rounded = false;
}

void VoxelTree_EmitInstance(VoxelBuilder *builder, const VoxelMapInstance *inst,
                            int x0, int y0, int x1, int y1)
{
    if (!VoxelWorld_UsesTreeSprites(inst))
        return;
    if (x0 < inst->originX) x0 = inst->originX;
    if (y0 < inst->originY) y0 = inst->originY;
    if (x1 > inst->originX + inst->width) x1 = inst->originX + inst->width;
    if (y1 > inst->originY + inst->height) y1 = inst->originY + inst->height;
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x)
        {
            int part = VoxelTree_Part(VoxelWorld_GetMetatileId(x, y));
            if (part >= 0)
            {
                builder->lift = VoxelRelief_CellLift(inst, x, y);
                builder->shift = VoxelRelief_CellShift(inst, x, y);
                EmitCell(builder, x, y, part);
                builder->lift = 0.0f;
                builder->shift = 0.0f;
            }
        }
}

void VoxelTree_EmitBorder(VoxelBuilder *builder, int x0, int y0, int x1, int y1)
{
    if (!VoxelWorld_UsesTreeSprites(VoxelWorld_Instance(0)))
        return;
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x)
        {
            int part;
            if (VoxelWorld_GetInstanceAt(x, y) != NULL)
                continue;
            part = VoxelTree_Part(VoxelWorld_BorderMetatile(x, y));
            if (part >= 0)
                EmitCell(builder, x, y, part);
        }
}
