// test_voxel_frlg.c -- Phase 34 R2: the renderer path on the real FireRed / LeafGreen rev 1 ROMs. The adapter builds
// Pallet Town (group 3, map 0) from the ROM with NO data files: u32 attributes interned, border sizes, the 640/384
// tileset split, the weatherPtr deref, the live backup-map pointer, map connections, the world's cell queries.
// Real ROMs come from ROMGEN_ROM_FR / ROMGEN_ROM_LG (absolute paths) or firered.gba / leafgreen.gba beside ROMGEN_ROM;
// a missing ROM prints SKIP (the gate sets the variables, so no SKIP may appear there). No game bytes are committed.
//
//   make -C tools/romgen vtest   (builds this with the other world suites, run from the repo root)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rg_fixture.h"
#include "vx_fixture_frlg.h"
#include "voxel_world.h"
#include "voxel_tree.h"
#include "voxel_atlas.h"
#include "voxel_mesh_builder.h"

static int sChecks, sFails, sSkips;
#define CHECK(c) do { ++sChecks; if (!(c)) { ++sFails; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

static uint32_t Rd32R(const uint8_t *rom, uint32_t addr)
{
    const uint8_t *p = rom + (addr - 0x08000000u);
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Look backlog L1: the General round bush 0x005 is Kanto's shrub 0. On the instances built around Pallet (Pallet and
 * Route 1) every shrub cell is 0x005, the atlas composes its ground and leaves slots on the pair that has one, and an
 * interior shrub cell emits its flat ground plus one standing card: 12 vertices. */
static VoxelVertex sVerts[64];

static void ShrubsAround(const char *label)
{
    static uint16_t atlas[VOXEL_ATLAS_PIXELS];
    static VoxelAtlasMap map;
    const VoxelMapInstance *with = NULL;
    unsigned m = 0;
    int shrubs = 0, sx = 0, sy = 0;

    for (int i = 0; i < (int)VoxelWorld_InstanceCount(); ++i)
    {
        const VoxelMapInstance *in = VoxelWorld_Instance(i);

        CHECK(VoxelTree_ShrubSource(in->primaryTileset, in->secondaryTileset, 0, &m) && m == 0x005u);
        for (int y = in->originY + 1; y < in->originY + in->height - 1; ++y)
            for (int x = in->originX + 1; x < in->originX + in->width - 1; ++x)
            {
                int sh;

                if (VoxelWorld_GetInstanceAt(x, y) != in || (sh = VoxelTree_Shrub(in, VoxelWorld_GetMetatileId(x, y))) < 0
                 || VoxelTree_PropKind((unsigned)sh) != GP_PROP_BUSH)   /* L8: the props share the table */
                    continue;
                CHECK(VoxelTree_Shrub(in, VoxelWorld_GetMetatileId(x, y)) == 0 && VoxelWorld_GetMetatileId(x, y) == 0x005);
                if (with == NULL) { with = in; sx = x; sy = y; }
                ++shrubs;
            }
    }
    printf("  %s: %d interior shrub cells on %u instances, first at %d,%d\n", label, shrubs, VoxelWorld_InstanceCount(), sx, sy);
    CHECK(shrubs > 0 && with != NULL && VoxelTree_Part(0x005) == -1);
    if (with == NULL)
        return;
    CHECK(VoxelAtlas_Build(with, atlas, &map, false) && !map.overflowed);
    CHECK(map.slotOf[VOXEL_SHRUB_GROUND(0)] != 0 && map.slotOf[VOXEL_SHRUB_GROUND(0)] != VOXEL_SLOT_ABSENT);
    CHECK(map.slotOf[VOXEL_SHRUB_LEAVES(0)] != 0 && map.slotOf[VOXEL_SHRUB_LEAVES(0)] != VOXEL_SLOT_ABSENT);
    CHECK(map.slotOf[VOXEL_SHRUB_GROUND(0)] != map.slotOf[VOXEL_SHRUB_LEAVES(0)]);
    CHECK(map.slotOf[0x005] != 0 && map.slotOf[0x005] != map.slotOf[VOXEL_SHRUB_GROUND(0)]);
    {
        VoxelBuilder b;
        float top = -1.0f;

        VoxelMesh_BeginWindow(sx, sy, sx + 1, sy + 1);
        VoxelBuilder_Init(&b, sVerts, 64);
        VoxelBuilder_SetAtlas(&b, &map);
        VoxelMesh_EmitGroundRow(&b, with, sx, sx + 1, sy);
        for (unsigned i = 0; i < b.count; ++i)
            if (sVerts[i].y > top) top = sVerts[i].y;
        printf("  %s: shrub cell %d,%d emits %u vertices, card top %.3f\n", label, sx, sy, b.count, top);
        CHECK(b.count == 12 && b.dropped == 0);
        CHECK(top > 0.7f && top < 0.9f);
    }
}

/* Look backlog L2: tall grass (behaviour 0x02) stands up as two blade cards over its own flat cell. On the instances
 * around Pallet the atlas composes the blade slot (distinct from the flat slot), a grass cell emits 6 flat + 12 card
 * vertices, and every grass cell is behaviour 0x02 only. */
static void GrassAround(const char *label)
{
    static uint16_t atlas[VOXEL_ATLAS_PIXELS];
    static VoxelAtlasMap map;
    const VoxelMapInstance *with = NULL;
    int cells = 0, gx = 0, gy = 0, k0 = -1;
    unsigned m = 0;

    for (int i = 0; i < (int)VoxelWorld_InstanceCount(); ++i)
    {
        const VoxelMapInstance *in = VoxelWorld_Instance(i);

        for (int y = in->originY + 1; y < in->originY + in->height - 1; ++y)
            for (int x = in->originX + 1; x < in->originX + in->width - 1; ++x)
            {
                int k;

                if (VoxelWorld_GetInstanceAt(x, y) != in || (k = VoxelTree_Grass(in, VoxelWorld_GetMetatileId(x, y))) < 0)
                    continue;
                CHECK(VoxelWorld_GetMetatileBehavior(x, y) == 0x02u);
                CHECK(VoxelTree_GrassSource(in->primaryTileset, in->secondaryTileset, (unsigned)k, &m)
                      && m == (unsigned)VoxelWorld_GetMetatileId(x, y));
                if (with == NULL) { with = in; gx = x; gy = y; k0 = k; }
                ++cells;
            }
    }
    printf("  %s: %d interior grass cells, first at %d,%d\n", label, cells, gx, gy);
    CHECK(cells > 0 && with != NULL);
    if (with == NULL)
        return;
    CHECK(!VoxelTree_GrassSource(with->primaryTileset, with->secondaryTileset, VOXEL_GRASSES, &m));
    CHECK(VoxelTree_Grass(with, -1) == -1 && VoxelTree_Grass(with, 1024) == -1 && VoxelTree_Grass(with, 0x005) == -1);
    CHECK(VoxelAtlas_Build(with, atlas, &map, false) && !map.overflowed);
    CHECK(map.slotOf[VOXEL_GRASS_BLADES(k0)] != 0 && map.slotOf[VOXEL_GRASS_BLADES(k0)] != VOXEL_SLOT_ABSENT);
    CHECK(map.slotOf[VOXEL_GRASS_BLADES(k0)] != map.slotOf[VoxelWorld_GetMetatileId(gx, gy)]);
    {
        VoxelBuilder b;
        float top = -1.0f;

        VoxelMesh_BeginWindow(gx, gy, gx + 1, gy + 1);
        VoxelBuilder_Init(&b, sVerts, 64);
        VoxelBuilder_SetAtlas(&b, &map);
        VoxelMesh_EmitGroundRow(&b, with, gx, gx + 1, gy);
        for (unsigned i = 0; i < b.count; ++i)
            if (sVerts[i].y > top) top = sVerts[i].y;
        printf("  %s: grass cell %d,%d emits %u vertices, card top %.3f\n", label, gx, gy, b.count, top);
        CHECK(b.count == 6 + 6 * VOXEL_GRASS_CARDS && b.dropped == 0);
        CHECK(top > 0.4f && top < 0.7f);
    }
}

static void RunGame(const char *env, GpGame game, const char *label)
{
    size_t n = 0;
    uint8_t *rom = fxr_load_rom(env, &n);
    const GameProfile *prof;
    struct MapConnection conn = {0, 0, 3, 0};
    const struct MapHeader *pal;
    const struct MapLayout *lay;
    FrState st;
    int water = 0, doors = 0, trees = 0, northRoute1 = 0;

    if (rom == NULL) { printf("SKIP %s: ROM not found (%s)\n", label, env); ++sSkips; return; }
    prof = gameprof_detect(rom, n);
    CHECK(prof != NULL && prof->game == game);
    if (prof == NULL) { free(rom); return; }
    gVxProf = prof;
    vx_adapter_set_rom(rom, n);

    pal = GetMapHeaderFromConnection(&conn);
    CHECK(pal != NULL && pal->mapLayout != NULL);
    if (pal == NULL || pal->mapLayout == NULL) { free(rom); gVxProf = NULL; return; }
    lay = pal->mapLayout;
    CHECK(lay->width == 24 && lay->height == 20);
    CHECK(lay->borderWidth == 2 && lay->borderHeight == 2 && lay->border != NULL);
    CHECK(pal->mapType == 1 && vx_map_is_outdoor(pal->mapType));          /* MAP_TYPE_TOWN */
    CHECK(lay->primaryTileset == &gTileset_General && lay->secondaryTileset != NULL);
    CHECK(!lay->primaryTileset->isSecondary && lay->secondaryTileset->isSecondary);
    CHECK(lay->primaryTileset->metatileAttributes != NULL && lay->secondaryTileset->metatileAttributes != NULL);
    CHECK(Port_GetAssetSizeExact(lay->primaryTileset->metatileAttributes) == 640u * 2u);
    CHECK(pal->connections != NULL);
    for (int i = 0; pal->connections != NULL && i < pal->connections->count; ++i)
        if (pal->connections->connections[i].direction == 2 && pal->connections->connections[i].mapGroup == 3
            && pal->connections->connections[i].mapNum == 19)
            northRoute1 = 1;
    CHECK(northRoute1);   /* Route 1 (3/19) joins Pallet's north edge */

    /* T1: Route 1's layout (3/19), straight from the ROM: its tree walls resolve to tree parts. The Kanto ids are the
     * 2x2 tree blocks 1C 1D / 14 15 (+ edge variants 1E 1F / 16 17, trunk row 24 25 / 26 27). */
    {
        struct MapConnection c1 = {0, 0, 3, 19};
        const struct MapHeader *r1 = GetMapHeaderFromConnection(&c1);
        int cells = 0, parts = 0, top = 0, bottom = 0, stray = 0;

        CHECK(r1 != NULL && r1->mapLayout != NULL && r1->mapLayout->map != NULL);
        if (r1 != NULL && r1->mapLayout != NULL && r1->mapLayout->map != NULL)
        {
            const struct MapLayout *l1 = r1->mapLayout;

            CHECK(l1->primaryTileset == &gTileset_General);
            for (int i = 0; i < l1->width * l1->height; ++i)
            {
                int mt = l1->map[i] & 0x3FF, part = VoxelTree_Part(mt);

                ++cells;
                if (part >= 0) ++parts;
                if (part == 0 || part == 1) ++top;
                if (part == 2 || part == 3) ++bottom;
                /* nothing outside the 12 Kanto ids is a part, and every one of the 12 is */
                if ((part >= 0) != ((mt >= 0x14 && mt <= 0x17) || (mt >= 0x1C && mt <= 0x1F) || (mt >= 0x24 && mt <= 0x27)))
                    ++stray;
            }
            printf("  %s: Route 1 %dx%d, %d tree-part cells (%d top row, %d bottom row)\n", label, (int)l1->width, (int)l1->height,
                   parts, top, bottom);
            CHECK(stray == 0);
            CHECK(parts == 278 && top == 138 && bottom == 140);   /* measured: the walls along both sides and the north end */
            CHECK(cells == l1->width * l1->height);
        }
    }

    /* The Pallet door metatile 0x2A3 (secondary tileset, local index 0x2A3 - 640): attribute word read straight from the
     * ROM must equal the interned u16: behaviour 0x69 in the low 9 bits, layer type from bits 29-30 into bits 12-13. */
    {
        uint32_t tsAddr = Rd32R(rom, prof->mapLayouts + 4u * (pal->mapLayoutId - 1u));
        uint32_t secTs = Rd32R(rom, tsAddr + 0x14);
        uint32_t attrAddr = Rd32R(rom, secTs + prof->tilesetAttrOff);
        uint32_t raw = Rd32R(rom, attrAddr + 4u * (0x2A3u - 640u));
        uint16_t got = lay->secondaryTileset->metatileAttributes[0x2A3u - 640u];

        CHECK((raw & 0x1FFu) == 0x69u);
        CHECK(got == (uint16_t)((raw & 0x1FFu) | (((raw >> 29) & 3u) << 12)));
        CHECK((got & 0x1FFu) == 0x69u);
        printf("  %s: Pallet door metatile 0x2A3 attribute raw=0x%08X -> u16 0x%04X (layer %u)\n", label, raw, got, (got >> 12) & 3u);
    }

    /* A live-state image of Pallet at (12,12), facing south, then the real snapshot -> adapter -> world path. */
    FrBuild(&st, rom, prof, pal, 12, 12);
    {
        VxMemSrc src = {st.ewram, st.iwram, st.pltt, st.vram, 0x1040, 0, 0, 0};
        VxSnapshot *snap = calloc(1, sizeof *snap);

        CHECK(snap != NULL && vx_snapshot_take(snap, &src));
        CHECK(snap->backupMapBase == FR_BACKUP_MAP && snap->backupMapCells == (24u + 15u) * (20u + 14u));
        CHECK(snap->weather[0] == 2 && snap->weather[1] == 3 && snap->weather[2] == 9);   /* resolved through weatherPtr */
        CHECK(vx_adapter_decode(snap) && vx_adapter_error() == VX_OK);
        CHECK(gBackupMapLayout.map == snap->backupMap && gBackupMapLayout.width == 39 && gBackupMapLayout.height == 34);
        CHECK(gMapHeader.mapLayout == lay && gMapHeader.mapLayoutId == pal->mapLayoutId);
        CHECK(gPlayerAvatar.objectEventId == 0 && gObjectEvents[0].isPlayer && gObjectEvents[0].currentCoords.x == 19);
        CHECK(GetCurrentWeather() == 2);
        VoxelWorld_BeginBatch();
        VoxelWorld_BuildInstances();
        CHECK(VoxelWorld_IsMapAvailable());
        CHECK(VoxelWorld_InstanceCount() >= 2);   /* Pallet + at least Route 1 to the north (and no other connection) */
        CHECK(VoxelWorld_GetInstanceAt(5, -3) != NULL && VoxelWorld_GetInstanceAt(5, -3) != VoxelWorld_Instance(0));
        /* The three Pallet doors are behaviour 0x69 and blocked (SPEC 2: a door is entered through its warp). */
        CHECK(VoxelWorld_GetMetatileBehavior(6, 7) == 0x69u && VoxelWorld_GetMetatileBehavior(15, 7) == 0x69u
              && VoxelWorld_GetMetatileBehavior(16, 13) == 0x69u);
        for (int y = 0; y < 20; ++y)
            for (int x = 0; x < 24; ++x)
            {
                unsigned b = VoxelWorld_GetMetatileBehavior(x, y);
                VoxelVisualShape s = VoxelWorld_ClassifyTile(x, y);

                if (b == 0x15u) { ++water; CHECK(s == VOXEL_SHAPE_WATER); }
                if (b == 0x69u) ++doors;
                if (VoxelWorld_UsesTreeSprites(VoxelWorld_Instance(0)) && VoxelTree_Part(VoxelWorld_GetMetatileId(x, y)) >= 0) ++trees;
            }
        CHECK(water >= 8);       /* the pond */
        CHECK(doors == 3);
        CHECK(trees > 0);        /* Pallet's tree wall resolves to tree parts (Kanto's table, slice T1) */
        CHECK(VoxelWorld_BorderMetatile(-2, -2) >= 0 && VoxelWorld_BorderMetatile(30, 30) >= 0);
        ShrubsAround(label);
        GrassAround(label);
        free(snap);
    }
    FrFree(&st);
    gVxProf = NULL;
    vx_adapter_set_rom(NULL, 0);
    free(rom);
}

int main(void)
{
    RunGame(FXR_ENV_FR, GP_FIRERED, "FireRed");
    RunGame(FXR_ENV_LG, GP_LEAFGREEN, "LeafGreen");
    printf("test_voxel_frlg: %d checks, %d failures, %d skipped\n", sChecks, sFails, sSkips);
    return sFails != 0;
}
