/* Modified for 3DGBA (GPLv3), 2026.
 * Original: ZallaxDev/pokeemerald-3Ds-dualscreen @ c330c0a, 3ds_port/scripts/gen_voxel_buildings.py
 * (build_models, cell_heights, cell_footprints, find_placements, pack_atlas, texel_offset, export),
 * MIT License - see source/voxel/NOTICE.md. Portions Copyright (c) Dust Zallax, MIT. */
/* rg_buildings.h -- models, placements, atlas and the VXB7 writer (3DGBA, GPLv3). Pure C. SPEC-S2 section 1.7, 4.
 *
 * S2.3-S2.5 cover direct, kit, components (hedges, railings) and props specs; the interior branches of
 * find_placements arrive with S2.6. Expanded models carry a private copy of their spec. */
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
#define RG_MAX_PAGE_MODELS 256u         /* distinct models one page (layout) places */

/* An expanded model's private spec: the name and its one exact rect live in the same heap block, so the pointers
 * in `spec` survive the model array growing or compacting. */
typedef struct RgXSpec { RgSpec spec; RgExact exact; char name[64]; } RgXSpec;

typedef struct RgAt { uint16_t lid; int16_t x, y; } RgAt;      /* a props model's copy: layout id, min cell */

typedef struct RgBuildModel {
    const RgSpec *spec;                 /* the spec, or xSpec for an expanded model */
    const RgLayout *layout;             /* the reference layout */
    RgImage art;
    RgMesh mesh;
    uint8_t w, h;
    uint16_t groundMetatile;
    uint8_t *owned;                     /* w*h flags, NULL = every cell */
    uint8_t *own;                       /* bare twins only (S2.6); NULL */
    /* ---- S2.5: expanded models (NULL / 0 for a direct spec) ---- */
    RgXSpec *xSpec;                     /* private copy of the spec (spec == &xSpec->spec); NULL for a direct row */
    const RgComponentsCfg *comp;        /* a hedge / railing piece */
    const RgPropsCfg *prop;             /* a props variant */
    uint8_t *south, *north;             /* w*h flags: cells whose run goes on past the block (piece_spec) */
    uint8_t *east, *west;               /* h flags */
    uint16_t (*repeat)[2];              /* repeat_at: where an identical block also stands (x, y) */
    unsigned nRepeat;
    RgAt *at;                           /* props: every copy found */
    unsigned nAt;
    unsigned seamClash;                 /* seam_art: south cells sharing a pixel column (0 expected) */
    uint8_t *quads;                     /* props: w*h quarter masks of the subtiles the model stands for */
    RgImage drawing;                    /* props: the judged reference (the art before with_ring) */
    bool hasDrawing;
    bool artReady;                      /* art composed by the expander (props) */
} RgBuildModel;

typedef struct RgBuildModels {
    RgBuildModel *m;
    unsigned n;
    unsigned skipped;                   /* specs whose layout fingerprint did not match (a hack / other revision) */
    const char *skippedNames[RG_MAX_SKIPPED];
    /* S2.5 diagnostics */
    unsigned cap;
    unsigned nHedge, nRailing, nKit, nProps;   /* expanded models by kind */
    unsigned seamClash;                 /* seam_art: south cells sharing a pixel column (must be 0) */
    unsigned connAmbiguous;             /* out-of-layout cells two connections claim (must be 0) */
    unsigned propTies;                  /* props groups tied on size: upstream's name order vs ours (A2) */
} RgBuildModels;

/* gen:877-939. The layout fingerprint must match or the spec is skipped (SPEC-S2 A10). */
RgErr rg_build_models(const RgWorld *w, const RgSpec *specs, unsigned nSpecs, RgBuildModels *out);
void rg_models_free(RgBuildModels *ms);
/* Appends a zeroed model (grows the array); NULL on no memory. For the expanders. */
RgBuildModel *rg_models_push(RgBuildModels *ms);
/* Frees everything one model owns (not the model slot). */
void rg_model_release(RgBuildModel *m);
/* Per-layout pair cache for the expanders and the writer. */
typedef struct RgPairCache { const RgWorld *w; RgPair *p; int idx; } RgPairCache;
RgPair *rg_pc_get(RgPairCache *c, uint16_t idx);
void rg_pc_close(RgPairCache *c);

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
    bool patchAll;                      /* every odd cell is patched, even under the model (an upper-layer relief) */
    unsigned nOdd;                      /* cells (owned) where the map paints something else, row-major in odd[] */
    uint8_t (*odd)[2];
    RgPatchCell *cells;                 /* filled by rg_placement_patches */
    unsigned nCells;
} RgPlacement;
typedef struct RgPlacementList { RgPlacement *p; unsigned n, cap; } RgPlacementList;
void rg_placements_free(RgPlacementList *l);

/* gen:1063-1188: layouts in id order, py then px. Owned (component) models stand only in their own layout at
 * repeat_at; props stand at their `at` list. */
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
