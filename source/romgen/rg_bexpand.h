/* Modified for 3DGBA (GPLv3), 2026.
 * Original: ZallaxDev/pokeemerald-3Ds-dualscreen @ c330c0a, 3ds_port/scripts/gen_voxel_buildings.py
 * (component_specs, piece_spec, prop_specs, seam_art, pick_side, flank_band, kit_specs) and voxel_props.py
 * (OBJECTS, find, cells_of, beyond, connections), MIT License - see source/voxel/NOTICE.md.
 * Portions Copyright (c) Dust Zallax, MIT. */
/* rg_bexpand.h -- the spec expanders: hedges/railings (components), Rustboro's kit blocks, props found by their
 * subtiles (3DGBA, GPLv3). Pure C. SPEC-S2 section 1.5-1.6, 6.1 row S2.5. */
#ifndef RG_BEXPAND_H
#define RG_BEXPAND_H

#include "rg_buildings.h"

/* Phase 1: append the unbuilt models of one spec (name, layout, rect, spec copy, owned / at / quads ...). Counts
 * go to ms->nHedge etc. Layout fingerprints are checked for kit rows (skipped++ on mismatch). */
RgErr rg_expand_components(const RgWorld *w, const RgSpec *s, RgBuildModels *ms);
RgErr rg_expand_kit(const RgWorld *w, const RgSpec *s, RgBuildModels *ms);
RgErr rg_expand_props(const RgWorld *w, const RgSpec *s, RgBuildModels *ms);

/* Phase 2: the art and mesh of an expanded model (m->mesh already initialised, pair = its layout's). */
RgErr rg_build_component(const RgWorld *w, RgPair *pair, RgBuildModel *m);
RgErr rg_build_prop(RgBuildModel *m);

/* gen:231-267: an 8-column tile of the object's own drawn front. */
RgTile rg_pick_side(const RgImage *art, int height);
/* gen:174-228. `out` = art when there is no seam (a copy). openS/openN: art->w flags each (zeroed by the caller);
 * *clash counts south cells sharing a pixel column (the order of upstream's frozenset iteration then matters). */
bool rg_seam_art(RgPair *pair, const RgLayout *L, const RgBuildModel *m, const RgImage *art, int height,
                 RgImage *out, uint8_t *openS, uint8_t *openN, unsigned *clash);
/* gen:270-288: with a flank metatile, the band appended below. *has = false when there is none (out = copy). */
bool rg_flank_band(RgPair *pair, const RgImage *art, const RgComponentsCfg *c, int height, RgImage *out,
                   RgTile *tile, bool *has);


/* G8 (SPEC-S3 1.4): voxel_props.cells_in, all three objects. cellFlags = w*h bytes of the layout, rewritten; bit o set
 * when object o (kObjects order: sea_rock 0, sand_boulder 1, sea_stack 2) is drawn over the cell, own finds and the
 * neighbours' finds that a seam cuts. The one-shot form builds the connection table each call; hold an RgProps to
 * reuse it. RG_ERR_LAYOUT_TABLE when the layout is absent. */
typedef struct RgProps RgProps;
RgProps *rg_props_open(const RgWorld *w, RgErr *err);
RgErr rg_props_cells(RgProps *p, uint16_t layoutId, uint8_t *cellFlags);
void rg_props_close(RgProps *p);
RgErr rg_props_cells_in(const RgWorld *w, uint16_t layoutId, uint8_t *cellFlags);

#endif
