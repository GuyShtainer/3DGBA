/* Modified for 3DGBA (GPLv3), 2026.
 * Original: ZallaxDev/pokeemerald-3Ds-dualscreen @ c330c0a, 3ds_port/scripts/gen_voxel_buildings.py
 * (build_models, cell_heights, cell_footprints, find_placements, pack_atlas, texel_offset, export),
 * MIT License - see source/voxel/NOTICE.md. Portions Copyright (c) Dust Zallax, MIT. */
/* rg_buildings.h -- models, placements, atlas and the VXB7 writer (3DGBA, GPLv3). Pure C. SPEC-S2 section 1.7, 4.
 *
 * S2.3 covers direct specs only (no `owned` masks, no relief, no interior pieces, no props variants): the
 * components/kit/props/interior branches of find_placements and the variants table arrive with S2.5-S2.6. */
#ifndef RG_BUILDINGS_H
#define RG_BUILDINGS_H

#include "rg_bspecs.h"
#include "rg_world.h"

#define RG_OWN_GROUND 0xFFFFu           /* gen:34 */
#define RG_MAX_VARIANTS 128u            /* gen:35 */
#define RG_FOOTPRINT_FULL 240u          /* gen:1011 */
#define RG_MAX_PAGES 256u               /* gen:1410 */
#define RG_MAX_TEXTURE_W 512
#define RG_MAX_TEXTURE_H 512
#define RG_MAX_SKIPPED 8

typedef struct RgBuildModel {
    const RgSpec *spec;
    const RgLayout *layout;             /* the reference layout */
    RgImage art;
    RgMesh mesh;
    uint8_t w, h;
    uint16_t groundMetatile;
    uint8_t *owned;                     /* w*h flags, NULL = every cell */
    uint8_t *own;                       /* bare twins only (S2.6); NULL */
} RgBuildModel;

typedef struct RgBuildModels {
    RgBuildModel *m;
    unsigned n;
    unsigned skipped;                   /* specs whose layout fingerprint did not match (a hack / other revision) */
    const char *skippedNames[RG_MAX_SKIPPED];
} RgBuildModels;

/* gen:877-939 for direct specs. The layout fingerprint must match or the spec is skipped (SPEC-S2 A10). */
RgErr rg_build_models(const RgWorld *w, const RgSpec *specs, unsigned nSpecs, RgBuildModels *out);
void rg_models_free(RgBuildModels *ms);

/* FNV-1a-32 of a layout's blockdata (its w*h u16 entries as ROM bytes): the spec's name pin. */
uint32_t rg_layout_fnv(const RgLayout *L);

/* The gate (SPEC-S2 section 5): ortho check over the spec's exact rects plus an empty density list. */
bool rg_model_gate(const RgBuildModel *m, RgOrthoResult *ortho, unsigned *densityBad);

/* gen:978-1006: the tallest point over each cell in pixels (min(255, round-half-even)), w*h bytes. 255 is NOT
 * applied for unowned cells here (the writer does that). */
bool rg_cell_heights(const RgBuildModel *m, uint8_t *out);

/* gen:1014-1051 */
typedef struct RgMaskSet { uint16_t (*rows)[16]; unsigned n, cap; } RgMaskSet;
void rg_masks_free(RgMaskSet *s);
/* maskOf[cell] = index into `masks` (values are deduplicated, first-seen order) or -1 for a box. */
bool rg_cell_footprints(const RgBuildModel *m, int32_t *maskOf, RgMaskSet *masks);

typedef struct RgPatchCell { uint8_t i, j; RgImage img; int patch; } RgPatchCell;
typedef struct RgPlacement {
    uint16_t layout;                    /* 1-based layout id the building stands in */
    int16_t px, py;
    uint16_t ground;
    unsigned nOdd;                      /* cells (owned) where the map paints something else, row-major in odd[] */
    uint8_t (*odd)[2];
    RgPatchCell *cells;                 /* filled by rg_placement_patches */
    unsigned nCells;
} RgPlacement;
typedef struct RgPlacementList { RgPlacement *p; unsigned n, cap; } RgPlacementList;
void rg_placements_free(RgPlacementList *l);

/* gen:1063-1188 for a direct model: layouts in id order, py then px. */
RgErr rg_find_placements(const RgWorld *w, const RgBuildModel *m, RgPlacementList *out);
/* gen:1296-1306 placement_patches, applied to one placement of a model. */
RgErr rg_placement_patches(const RgWorld *w, const RgBuildModel *m, RgPlacement *pl);

/* gen:1209-1244. spots[i] = top-left of image i. False when nothing up to 1024x1024 fits. */
bool rg_pack_atlas(const RgImage *const *imgs, unsigned n, int *tw, int *th, int (*spots)[2]);
/* gen:1250-1255: 8x8 Morton tiles, the PICA200 order (a texel index, not a byte offset). */
uint32_t rg_texel_offset(unsigned x, unsigned y, unsigned width);

typedef struct RgBuildStats {
    unsigned models, pages, pageModels, placements, vertices, masks, variants;
    size_t fileSize;
    uint16_t pageW[RG_MAX_PAGES], pageH[RG_MAX_PAGES];
    const char *errField;               /* names the field that overflowed (RG_ERR_TOO_BIG) */
    RgErr err;
} RgBuildStats;

/* gen:1293-1451. out == NULL returns the size; otherwise writes and returns the bytes written. 0 = error (stats->err). */
size_t rg_buildings_write(const RgWorld *w, const RgBuildModels *models, uint8_t *out, size_t cap, RgBuildStats *stats);

#endif
