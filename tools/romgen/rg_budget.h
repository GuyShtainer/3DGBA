/* rg_budget.h -- the vertex budget of every building model and every map chunk (3DGBA original work, GPLv3). Host only.
 *
 * Look L7 found that a chunk whose geometry overflows the device's per-chunk vertex scratch (ctr_voxel.c
 * VOXEL_CHUNK_SCRATCH, 9344 vertices with lighting) silently refuses the TAIL of what it emits: the models come last, so
 * a porch or a door vanished. This measures, with the device's own emitters (voxel_mesh_builder, voxel_tree,
 * voxel_building, voxel_lighting) in the device's order (ground rows, then trees/shrubs/grass, then the models whose
 * placement origin lies in the chunk), what every chunk of every map would put into that scratch.
 *
 * The data files are produced in-process by rg_run (regions, signposts, buildings, relief FULL: the worst case, the
 * file a PC user copies to the SD card) and read back through the vendored consumer from a temp directory.
 * `romgen author ROM budget` prints the tables; test_romgen_budget.c fails on any chunk over the scratch. */
#ifndef RG_BUDGET_H
#define RG_BUDGET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* ctr_voxel.c with CTR_VOXEL_LIGHTING (the device build): 8192 + 64 * 18. Kept in step by hand; the test pins it. */
#define RG_BUDGET_SCRATCH (8192u + 64u * 18u)
/*
 * The per-model limit `author check` enforces: what one model may emit and still leave its chunk room for the terrain
 * under it. Measured (`author budget`, 2026-10-07, relief FULL): the heaviest terrain any chunk that holds a model
 * carries is 5892 vertices (Emerald 0/8 chunk 1,8: lit relief + refinement); FRLG's is 2430. 9344 - 5892 = 3452, so
 * 3300 vertices (1100 triangles) fits a model on that chunk with 152 to spare. The largest model after the flat-cap
 * change is 2808 (Emerald rustboro_stone_3_43); before it Pokemon Tower was 61392. Several models in one chunk, or a
 * model on still heavier terrain, are the chunk check's business (test_romgen_budget), not this one's.
 */
#define RG_BUDGET_MODEL_VERTS 3300u

typedef struct RgBudgetModel {
    char name[64];
    unsigned tris, verts;        /* the model's mesh (3 vertices a triangle, as the device emits it) */
    unsigned placements;
} RgBudgetModel;

typedef struct RgBudgetChunk {
    uint8_t group, num;          /* the first map using the layout */
    uint16_t layout;
    bool indoor;
    int cx, cy;
    unsigned ground, trees, models, total;   /* vertices the device would write, by phase */
    char what[96];               /* the models whose placement lies in this chunk */
} RgBudgetChunk;

typedef struct RgBudget {
    RgBudgetModel *model;
    unsigned nModels;
    RgBudgetChunk *chunk;
    unsigned nChunks, capChunks;
    unsigned layoutsMeasured, layoutsSkipped;
    unsigned overModels, overChunks, nearChunks;   /* over RG_BUDGET_MODEL_VERTS / over the scratch / >= 80% */
    unsigned maxTerrainUnderModel;                  /* worst ground + trees of a chunk that holds a model */
    char game[16];
} RgBudget;

/* Measures one ROM. False with `why` when something could not run (no file is left behind either way). */
bool rg_budget_run(const uint8_t *rom, size_t size, RgBudget *out, char *why, size_t whySize);
void rg_budget_free(RgBudget *b);
/* The tables: every model at or over `listPct` percent of the model limit, every chunk at or over `listPct` of the
 * scratch, the worst chunks first; then the summary line. */
void rg_budget_print(const RgBudget *b, FILE *fp, unsigned listPct);

#endif
