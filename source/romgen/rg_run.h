/* rg_run.h -- the romgen driver shared by the host CLI and (from S4) the device (3DGBA, GPLv3). Pure C.
 * Opens the world, runs S1 pair by pair, hands back buffers; no file I/O. SPEC-S0-S1 section 6. */
#ifndef RG_RUN_H
#define RG_RUN_H

#include "rg_buildings.h"
#include "rg_world.h"

typedef struct RgRunOpts {
    RgProgressFn progress;       /* optional: done of total layouts */
    void *ctx;
    const volatile int *cancel;  /* optional: non-zero aborts with RG_ERR_CANCELLED */
    double (*nowMs)(void);       /* optional clock; when set the ms* fields of RgOutput are filled */
    bool wantSigns;              /* false: roles and regions.bin only */
    bool wantBuildings;          /* true: also build the models, gate them and write buildings.bin (SPEC-S2 1.8) */
} RgRunOpts;

typedef struct RgOutput {
    uint8_t *regions;            /* regions.bin (VXR5), malloc'd */
    size_t regionsSize;
    uint8_t *signs;              /* signposts.bin (VXS2), malloc'd; NULL when there are no records */
    size_t signsSize;
    uint8_t *buildings;          /* buildings.bin (VXB7), malloc'd; NULL unless wantBuildings */
    size_t buildingsSize;
    unsigned bModels, bPages, bPageModels, bPlacements, bVertices, bMasks, bVariants;
    unsigned buildingsFailed;    /* models whose gate (ortho + density) failed; the file is still written */
    char failedNames[RG_MAX_SKIPPED][64];   /* the first few failing model names */
    unsigned layouts, maps, tilesets, pairs, outdoorMaps;
    unsigned signCount, headCount, emptyMasks;
    unsigned roleCount[11];
    double msWorld, msRoles, msSigns, msWrite;
    double msBuildModels, msChecks, msWriteBuildings;   /* S2: build_models, the gates, rg_buildings_write (placements run inside the write) */
} RgOutput;

RgErr rg_run(const uint8_t *rom, size_t romSize, const RgRunOpts *opts, RgOutput *out);
void  rg_output_free(RgOutput *out);

#endif
