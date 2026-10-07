/* rg_budget.c -- the vertex budget of every building model and every map chunk (3DGBA original work, GPLv3). Host only.
 * See rg_budget.h. Built into build/romgen (`romgen author ROM budget`) and included by test/host/test_romgen_budget.c.
 * Needs the vendored voxel consumer compiled with VOXEL_HOST_FILES and CTR_VOXEL_LIGHTING=1 (both builds do). */
#include "rg_budget.h"

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "rg_bspecs.h"
#include "rg_buildings.h"
#include "rg_kspecs.h"
#include "rg_run.h"
#include "rg_world.h"

#include "gba_game.h"
#include "voxel_atlas.h"
#include "voxel_building.h"
#include "voxel_lighting.h"
#include "voxel_mesh_builder.h"
#include "voxel_regions.h"
#include "voxel_relief.h"
#include "voxel_sign.h"
#include "voxel_tree.h"
#include "voxel_world.h"
#include "vx_adapter.h"
#include "vx_snapshot.h"

#define BUDGET_CHUNK 8               /* ctr_voxel.c VOXEL_CHUNK */
#define BUDGET_BIG 200000u           /* a scratch no chunk fills, so the demand is measured, not clipped */
#define BUDGET_SB1 0x0202552Cu       /* an EWRAM address for the save block (any free one) */
#define BUDGET_BACKUP_FR 0x02031DFCu /* FRLG: inside the real game's backup-map buffer */

/* ---- one placement per model copy, to name the models of a chunk ---- */
typedef struct { uint16_t layout; int16_t px, py; unsigned model; } BPlace;

static uint32_t rd32(const uint8_t *rom, size_t size, uint32_t addr)
{
    uint32_t o = addr - 0x08000000u;

    if (addr < 0x08000000u || (size_t)o + 4u > size)
        return 0;
    return (uint32_t)rom[o] | ((uint32_t)rom[o + 1] << 8) | ((uint32_t)rom[o + 2] << 16) | ((uint32_t)rom[o + 3] << 24);
}

static void e8(uint8_t *ew, uint32_t a, uint32_t v) { ew[a - 0x02000000u] = (uint8_t)v; }
static void e16(uint8_t *ew, uint32_t a, uint32_t v) { e8(ew, a, v & 0xFFu); e8(ew, a + 1, (v >> 8) & 0xFFu); }
static void i32w(uint8_t *iw, uint32_t a, uint32_t v)
{
    for (int i = 0; i < 4; ++i)
        iw[a - 0x03000000u + (uint32_t)i] = (uint8_t)(v >> (8 * i));
}

/* A live-state image of the overworld on map `hdr` with the player at (px, py): the anchors of the profile (the same
 * state test_voxel_frlg.c's FrBuild writes, with Emerald's fixed backup map and weather struct). */
static bool state_build(uint8_t *ew, uint8_t *iw, const uint8_t *rom, size_t size, const GameProfile *p,
                        const struct MapHeader *hdr, unsigned group, unsigned num, int px, int py)
{
    const struct MapLayout *lay = hdr->mapLayout;
    int w = lay->width, h = lay->height;
    uint32_t backup = p->backupMap != 0 ? p->backupMap : BUDGET_BACKUP_FR;
    uint32_t wBase = p->weatherPtr != 0 ? rd32(rom, size, p->weatherPtr) : p->weather;

    if ((unsigned)((w + 15) * (h + 14)) > VX_BACKUP_MAP_MAX_CELLS)
        return false;
    memset(ew, 0, 0x40000);
    memset(iw, 0, 0x8000);
    i32w(iw, p->gMain + 4, p->cb2Overworld);
    i32w(iw, p->sb1Ptr, BUDGET_SB1);
    e16(ew, BUDGET_SB1, (uint32_t)px); e16(ew, BUDGET_SB1 + 2, (uint32_t)py);
    e8(ew, BUDGET_SB1 + 4, group); e8(ew, BUDGET_SB1 + 5, num);
    i32w(iw, p->backupLayout, (uint32_t)(w + 15));
    i32w(iw, p->backupLayout + 4, (uint32_t)(h + 14));
    i32w(iw, p->backupLayout + 8, backup);
    for (int y = 0; y < h + 14; ++y)
        for (int x = 0; x < w + 15; ++x) {
            int mx = x - MAP_OFFSET, my = y - MAP_OFFSET;
            uint32_t cell;

            if (mx >= 0 && my >= 0 && mx < w && my < h)
                cell = lay->map[my * w + mx];
            else
                cell = (lay->border[vx_border_cell(lay, x, y)] & 0x3FFu) | 0xC00u;
            e16(ew, backup + 2u * (uint32_t)(y * (w + 15) + x), cell);
        }
    if (hdr->gbaAddr < 0x08000000u || (size_t)(hdr->gbaAddr - 0x08000000u) + 28u > size)
        return false;
    memcpy(ew + (p->mapHeader - 0x02000000u), rom + (hdr->gbaAddr - 0x08000000u), 28);
    e8(ew, p->objEvents, 0x01); e8(ew, p->objEvents + 2, 0x01);
    e8(ew, p->objEvents + 0xB, 3);
    e16(ew, p->objEvents + 0x10, (uint32_t)(px + 7)); e16(ew, p->objEvents + 0x12, (uint32_t)(py + 7));
    e16(ew, p->objEvents + 0xC, (uint32_t)(px + 7)); e16(ew, p->objEvents + 0xE, (uint32_t)(py + 7));
    e16(ew, p->objEvents + 0x14, (uint32_t)(px + 7)); e16(ew, p->objEvents + 0x16, (uint32_t)(py + 7));
    e8(ew, p->objEvents + 0x18, 1);
    e8(ew, p->playerAvatar, 0x21);
    if (wBase >= 0x02000000u && wBase < 0x02040000u - 0x800u) {
        e8(ew, wBase + p->weatherOff[0], 2); e8(ew, wBase + p->weatherOff[1], 3); e8(ew, wBase + p->weatherOff[2], 9);
    }
    return true;
}

static bool write_file(const char *dir, const char *name, const uint8_t *buf, size_t n)
{
    char path[256];
    FILE *fp;
    bool ok;

    snprintf(path, sizeof(path), "%s/voxel/%s", dir, name);
    fp = fopen(path, "wb");
    if (fp == NULL)
        return false;
    ok = n == 0 || fwrite(buf, 1, n, fp) == n;
    return fclose(fp) == 0 && ok;
}

static void remove_files(const char *dir)
{
    static const char *const names[4] = {"regions.bin", "signposts.bin", "buildings.bin", "relief.bin"};
    char path[256];

    for (unsigned i = 0; i < 4; ++i) {
        snprintf(path, sizeof(path), "%s/voxel/%s", dir, names[i]);
        (void)unlink(path);
    }
    snprintf(path, sizeof(path), "%s/voxel", dir);
    (void)rmdir(path);
    (void)rmdir(dir);
}

static bool push_chunk(RgBudget *b, const RgBudgetChunk *c)
{
    if (b->nChunks == b->capChunks) {
        unsigned cap = b->capChunks ? b->capChunks * 2u : 1024u;
        RgBudgetChunk *n = (RgBudgetChunk *)realloc(b->chunk, cap * sizeof(*n));

        if (n == NULL)
            return false;
        b->chunk = n;
        b->capChunks = cap;
    }
    b->chunk[b->nChunks++] = *c;
    return true;
}

/* Every chunk of instance `inst`, built as ctr_voxel.c JobStart/JobStep build it, into the big scratch. */
static bool measure_instance(RgBudget *b, const VoxelMapInstance *inst, const VoxelAtlasMap *map, VoxelVertex *scratch,
                             const BPlace *pl, unsigned nPl, const RgBudgetModel *models)
{
    for (int cy = 0; cy * BUDGET_CHUNK < inst->height; ++cy)
        for (int cx = 0; cx * BUDGET_CHUNK < inst->width; ++cx) {
            int x0 = inst->originX + cx * BUDGET_CHUNK, y0 = inst->originY + cy * BUDGET_CHUNK;
            int x1 = x0 + BUDGET_CHUNK, y1 = y0 + BUDGET_CHUNK;
            VoxelBuilder vb;
            RgBudgetChunk c;
            unsigned k;

            if (x1 > inst->originX + inst->width) x1 = inst->originX + inst->width;
            if (y1 > inst->originY + inst->height) y1 = inst->originY + inst->height;
            memset(&c, 0, sizeof(c));
            c.group = (uint8_t)inst->mapGroup; c.num = (uint8_t)inst->mapNum; c.layout = (uint16_t)inst->layoutId;
            c.indoor = inst->indoor; c.cx = cx; c.cy = cy;
            VoxelBuilder_Init(&vb, scratch, BUDGET_BIG);
            VoxelBuilder_SetAtlas(&vb, map);
            VoxelBuilder_SetOrigin(&vb, x0, y0);
            vb.base = VoxelRelief_Base(inst);
            vb.lighting = !inst->indoor;
            vb.lightingRefine = true;
            VoxelMesh_BeginWindow(x0 - 1, y0 - VOXEL_CHUNK_MARGIN_NORTH, x1 + 1, y1 + 1);
            for (int row = y0; row < y1; ++row)
                for (int col = x0; col < x1; ++col)
                    VoxelMesh_EmitGroundRow(&vb, inst, col, col + 1, row);
            c.ground = vb.count;
            for (int row = y0; row < y1; ++row)
                for (int col = x0; col < x1; ++col)
                    VoxelTree_EmitInstance(&vb, inst, col, row, col + 1, row + 1);
            c.trees = vb.count - c.ground;
            VoxelBuildings_EmitInstance(&vb, inst, x0, y0, x1, y1);
            c.models = vb.count - c.ground - c.trees;
            c.total = vb.count;
            if (vb.dropped != 0)
                return false;
            for (k = 0; k < nPl; ++k) {
                size_t len = strlen(c.what);

                if (pl[k].layout != (unsigned)inst->layoutId || pl[k].px / BUDGET_CHUNK != cx || pl[k].py / BUDGET_CHUNK != cy)
                    continue;
                snprintf(c.what + len, sizeof(c.what) - len, "%s%s", len ? " " : "", models[pl[k].model].name);
            }
            if (c.models != 0 && c.ground + c.trees > b->maxTerrainUnderModel)
                b->maxTerrainUnderModel = c.ground + c.trees;
            if (c.total > RG_BUDGET_SCRATCH)
                b->overChunks++;
            else if (c.total * 5u >= RG_BUDGET_SCRATCH * 4u)
                b->nearChunks++;
            if (!push_chunk(b, &c))
                return false;
        }
    return true;
}

static int cmp_chunk(const void *a, const void *b)
{
    const RgBudgetChunk *x = (const RgBudgetChunk *)a, *y = (const RgBudgetChunk *)b;

    return x->total < y->total ? 1 : x->total > y->total ? -1 : 0;
}

static int cmp_model(const void *a, const void *b)
{
    const RgBudgetModel *x = (const RgBudgetModel *)a, *y = (const RgBudgetModel *)b;

    return x->verts < y->verts ? 1 : x->verts > y->verts ? -1 : strcmp(x->name, y->name);
}

bool rg_budget_run(const uint8_t *rom, size_t size, RgBudget *out, char *why, size_t whySize)
{
    RgRunOpts opts;
    RgOutput o;
    RgWorld w;
    RgBuildModels ms;
    const GameProfile *prof;
    const RgSpec *specs;
    unsigned nSpecs = 0, i, nPl = 0, capPl = 0;
    BPlace *pl = NULL;
    char dir[64] = "/tmp/rgbudget.XXXXXX", cwd[1024], path[128];
    bool ok = false, inDir = false;
    uint8_t *ew = NULL, *iw = NULL, *pltt = NULL, *vram = NULL, *done = NULL;
    VoxelVertex *scratch = NULL;
    uint16_t *atlas = NULL;
    VoxelAtlasMap *map = NULL;
    VxSnapshot *snap = NULL;
    RgErr e;

    memset(out, 0, sizeof(*out));
    memset(&o, 0, sizeof(o));
    memset(&ms, 0, sizeof(ms));
    snprintf(why, whySize, "?");
    prof = gameprof_detect(rom, size);
    if (prof == NULL) {
        snprintf(why, whySize, "not a supported game");
        return false;
    }
    snprintf(out->game, sizeof(out->game), "%.4s", prof->code);

    /* 1. the data files, exactly as `romgen ROM OUT` writes them (relief FULL) */
    memset(&opts, 0, sizeof(opts));
    opts.wantSigns = true;
    opts.wantBuildings = true;
    opts.relief = RG_RELIEF_FULL;
    e = rg_run(rom, size, &opts, &o);
    if (e != RG_OK) {
        snprintf(why, whySize, "rg_run: %s", rg_err_str(e));
        return false;
    }
    if (mkdtemp(dir) == NULL || getcwd(cwd, sizeof(cwd)) == NULL) {
        rg_output_free(&o);
        snprintf(why, whySize, "no temp directory");
        return false;
    }
    snprintf(path, sizeof(path), "%s/voxel", dir);
    if (mkdir(path, 0755) != 0 || !write_file(dir, "regions.bin", o.regions, o.regionsSize) ||
        (o.signs != NULL && !write_file(dir, "signposts.bin", o.signs, o.signsSize)) ||
        (o.buildings != NULL && !write_file(dir, "buildings.bin", o.buildings, o.buildingsSize)) ||
        (o.relief != NULL && !write_file(dir, "relief.bin", o.relief, o.reliefSize))) {
        rg_output_free(&o);
        remove_files(dir);
        snprintf(why, whySize, "cannot write the data files");
        return false;
    }
    rg_output_free(&o);

    /* 2. the models (the same table rg_run built) and where each copy stands */
    e = rg_world_open(&w, rom, size);
    if (e != RG_OK) {
        remove_files(dir);
        snprintf(why, whySize, "rg_world_open: %s", rg_err_str(e));
        return false;
    }
    if (w.prof->game == GP_EMERALD) {
        specs = rg_specs;
        nSpecs = rg_spec_count;
    } else {
        specs = rg_kspecs_table(w.prof, &nSpecs);
    }
    e = rg_build_models(&w, specs, nSpecs, &ms);
    if (e != RG_OK) {
        snprintf(why, whySize, "rg_build_models: %s", rg_err_str(e));
        goto done;
    }
    out->model = (RgBudgetModel *)calloc(ms.n ? ms.n : 1u, sizeof(RgBudgetModel));
    if (out->model == NULL)
        goto done;
    out->nModels = ms.n;
    for (i = 0; i < ms.n; i++) {
        RgPlacementList list;
        unsigned k;

        snprintf(out->model[i].name, sizeof(out->model[i].name), "%s", ms.m[i].spec->name);
        out->model[i].tris = ms.m[i].mesh.n;
        out->model[i].verts = ms.m[i].mesh.n * 3u;
        if (out->model[i].verts > RG_BUDGET_MODEL_VERTS)
            out->overModels++;
        memset(&list, 0, sizeof(list));
        if (rg_find_placements(&w, &ms.m[i], &list) != RG_OK) {
            snprintf(why, whySize, "rg_find_placements failed for %s", out->model[i].name);
            goto done;
        }
        out->model[i].placements = list.n;
        for (k = 0; k < list.n; k++) {
            if (nPl == capPl) {
                unsigned cap = capPl ? capPl * 2u : 256u;
                BPlace *n2 = (BPlace *)realloc(pl, cap * sizeof(*n2));

                if (n2 == NULL) {
                    rg_placements_free(&list);
                    goto done;
                }
                pl = n2;
                capPl = cap;
            }
            pl[nPl].layout = list.p[k].layout;
            pl[nPl].px = list.p[k].px;
            pl[nPl].py = list.p[k].py;
            pl[nPl].model = i;
            nPl++;
        }
        rg_placements_free(&list);
    }

    /* 3. the consumer, on the files, from the temp directory */
    if (chdir(dir) != 0) {
        snprintf(why, whySize, "cannot enter the temp directory");
        goto done;
    }
    inDir = true;
    gVxProf = prof;
    vx_adapter_set_rom(rom, size);
    VoxelRegions_Init();
    VoxelRelief_Init();
    VoxelSign_Init();
    if (!VoxelBuildings_Init()) {
        snprintf(why, whySize, "VoxelBuildings_Init refused the file");
        goto done;
    }
    ew = (uint8_t *)calloc(1, 0x40000);
    iw = (uint8_t *)calloc(1, 0x8000);
    pltt = (uint8_t *)calloc(1, 0x400);
    vram = (uint8_t *)calloc(1, 0x18000);
    scratch = (VoxelVertex *)malloc(BUDGET_BIG * sizeof(VoxelVertex));
    atlas = (uint16_t *)malloc(VOXEL_ATLAS_PIXELS * sizeof(uint16_t));
    map = (VoxelAtlasMap *)calloc(1, sizeof(*map));
    snap = (VxSnapshot *)calloc(1, sizeof(*snap));
    done = (uint8_t *)calloc(w.layoutCount + 2u, 1);
    if (!ew || !iw || !pltt || !vram || !scratch || !atlas || !map || !snap || !done)
        goto done;

    /* 4. every layout once, through the first map that uses it: outdoor maps, and Emerald's indoor ones (FRLG hands
     * indoor maps back to the 2D frame) */
    for (i = 0; i < w.mapCount; i++) {
        const RgMap *rm = &w.maps[i];
        struct MapConnection conn;
        const struct MapHeader *hdr;
        VxMemSrc src;
        const VoxelMapInstance *inst = NULL;

        if (rm->layoutId == 0 || rm->layoutId > w.layoutCount || done[rm->layoutId])
            continue;
        memset(&conn, 0, sizeof(conn));
        conn.mapGroup = rm->group;
        conn.mapNum = rm->num;
        hdr = GetMapHeaderFromConnection(&conn);
        if (hdr == NULL || hdr->mapLayout == NULL || hdr->mapLayout->map == NULL)
            continue;
        if (!vx_map_is_outdoor(hdr->mapType) && !prof->interiors3d)
            continue;
        done[rm->layoutId] = 1;
        if (!state_build(ew, iw, rom, size, prof, hdr, rm->group, rm->num, hdr->mapLayout->width / 2, hdr->mapLayout->height / 2)) {
            out->layoutsSkipped++;
            continue;
        }
        memset(&src, 0, sizeof(src));
        src.ewram = ew; src.iwram = iw; src.pltt = pltt; src.vram = vram; src.dispcnt = 0x1040;
        if (!vx_snapshot_take(snap, &src) || !vx_adapter_decode(snap)) {
            out->layoutsSkipped++;
            continue;
        }
        VoxelWorld_BeginBatch();
        VoxelWorld_BuildInstances();
        VoxelLighting_Reset();
        for (unsigned k = 0; k < VoxelWorld_InstanceCount(); k++)
            if (VoxelWorld_Instance(k)->mapGroup == rm->group && VoxelWorld_Instance(k)->mapNum == rm->num)
                inst = VoxelWorld_Instance(k);
        if (inst == NULL) {
            out->layoutsSkipped++;
            continue;
        }
        memset(map, 0, sizeof(*map));
        if (!VoxelAtlas_Build(inst, atlas, map, false)) {
            out->layoutsSkipped++;
            continue;
        }
        /* VoxelAtlas_Build composes page zero only; the shrub/prop slots that spill to a later page stay PENDING and
           would emit nothing. The vertex count does not depend on which slot it is, so count them as placed (L8). */
        for (unsigned m = 0; m < VOXEL_METATILE_IDS; ++m)
            if (map->slotOf[m] == VOXEL_SLOT_PENDING)
                map->slotOf[m] = 1;
        if (!measure_instance(out, inst, map, scratch, pl, nPl, out->model)) {
            snprintf(why, whySize, "a chunk of %u/%u overflowed the measuring scratch", rm->group, rm->num);
            goto done;
        }
        out->layoutsMeasured++;
    }
    qsort(out->chunk, out->nChunks, sizeof(*out->chunk), cmp_chunk);
    qsort(out->model, out->nModels, sizeof(*out->model), cmp_model);
    ok = true;

done:
    if (inDir) {
        VoxelBuildings_Shutdown();
        if (chdir(cwd) != 0)
            ok = false;
    }
    gVxProf = NULL;
    vx_adapter_set_rom(NULL, 0);
    remove_files(dir);
    free(ew); free(iw); free(pltt); free(vram); free(scratch); free(atlas); free(map); free(snap); free(done); free(pl);
    rg_models_free(&ms);
    rg_world_close(&w);
    if (!ok && why[0] == '?')
        snprintf(why, whySize, "out of memory");
    return ok;
}

void rg_budget_free(RgBudget *b)
{
    free(b->model);
    free(b->chunk);
    memset(b, 0, sizeof(*b));
}

void rg_budget_print(const RgBudget *b, FILE *fp, unsigned listPct)
{
    unsigned i;

    fprintf(fp, "budget %s: scratch %u vertices a chunk, model limit %u vertices (%u triangles)\n", b->game,
            RG_BUDGET_SCRATCH, RG_BUDGET_MODEL_VERTS, RG_BUDGET_MODEL_VERTS / 3u);
    fprintf(fp, "models (%u; listed: >= %u%% of the model limit)\n", b->nModels, listPct);
    for (i = 0; i < b->nModels; i++) {
        const RgBudgetModel *m = &b->model[i];

        if (m->verts * 100u < RG_BUDGET_MODEL_VERTS * listPct)
            break;
        fprintf(fp, "  %-28s %6u tris %6u verts  %3u%% of limit  %3u%% of scratch  x%u%s\n", m->name, m->tris, m->verts,
                m->verts * 100u / RG_BUDGET_MODEL_VERTS, m->verts * 100u / RG_BUDGET_SCRATCH, m->placements,
                m->verts > RG_BUDGET_SCRATCH ? "  OVER SCRATCH" : m->verts > RG_BUDGET_MODEL_VERTS ? "  OVER LIMIT" : "");
    }
    fprintf(fp, "chunks (%u in %u layouts, %u skipped; listed: >= %u%% of the scratch)\n", b->nChunks, b->layoutsMeasured,
            b->layoutsSkipped, listPct);
    for (i = 0; i < b->nChunks; i++) {
        const RgBudgetChunk *c = &b->chunk[i];

        if (c->total * 100u < RG_BUDGET_SCRATCH * listPct)
            break;
        fprintf(fp, "  %2u/%-3u L%-3u %s chunk %d,%d  %6u verts (ground %u trees %u models %u)  %3u%%%s  %s\n", c->group, c->num,
                c->layout, c->indoor ? "in " : "out", c->cx, c->cy, c->total, c->ground, c->trees, c->models,
                c->total * 100u / RG_BUDGET_SCRATCH, c->total > RG_BUDGET_SCRATCH ? " OVER" : "", c->what);
    }
    fprintf(fp, "summary %s: %u model(s) over the limit, %u chunk(s) over the scratch, %u at 80%% or more; worst chunk %u "
                "verts, worst model %u verts, worst terrain under a model %u verts\n",
            b->game, b->overModels, b->overChunks, b->nearChunks, b->nChunks ? b->chunk[0].total : 0u,
            b->nModels ? b->model[0].verts : 0u, b->maxTerrainUnderModel);
}
