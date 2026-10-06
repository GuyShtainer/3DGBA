/* Modified for 3DGBA (GPLv3), 2026.
 * Original: ZallaxDev/pokeemerald-3Ds-dualscreen @ c330c0a, 3ds_port/scripts/gen_voxel_buildings.py
 * (_inside, _inside_grid, cell_keys, reuse_pieces, register_piece, place_reused, reuse_everywhere, interior_specs,
 * room_check), MIT License - see source/voxel/NOTICE.md. Portions Copyright (c) Dust Zallax, MIT. */
/* rg_binterior.h -- interior rooms cut into pieces, furniture reused across rooms, bare twins, room_check
 * (3DGBA, GPLv3). Pure C. SPEC-S2 section 1.6, 5.4, 6.1 row S2.6. */
#ifndef RG_BINTERIOR_H
#define RG_BINTERIOR_H

#include "rg_brooms.h"
#include "rg_buildings.h"

/* gen:327-351 _inside and gen:354-378 _inside_grid: pixel centre (x + .5, y + .5); rectangles half-open, ellipses
 * <= 1, polygons even-odd. The grid is W*H flags, asked only within the shape's bounds +-2. */
bool rg_inside(const RgShape *s, int x, int y);
void rg_inside_grid(const RgShape *s, int W, int H, uint8_t *out);

/* Called by rg_build_models: sets up the reuse state when the table has interior rows. */
RgErr rg_interior_prepare(RgBuildModels *ms, const RgWorld *w, const RgSpec *specs, unsigned nSpecs);
/* One interior row (gen:559-874): a model per piece, in table order. A fingerprint mismatch skips the row. */
RgErr rg_expand_interior(const RgWorld *w, const RgSpec *s, RgBuildModels *ms);
/* gen:542-556 reuse_everywhere, then the bare twins (gen:925-937). Frees the reuse state. */
RgErr rg_interior_finish(const RgWorld *w, RgBuildModels *ms);
void rg_interior_free(RgBuildModels *ms);

/* The interior descriptor of a layout, or NULL (the `open` polygons room_check skips). */
const RgRoomDef *rg_room_of_layout(uint16_t layoutId);

typedef struct RgRoomResult {
    unsigned bad;                       /* pixels that differ from the room's drawing */
    int firstX, firstY;                 /* the first differing pixel (row-major) */
    int firstOwner;                     /* model index drawn there, -1 terrain, -2 nothing / patch */
    RgErr err;
} RgRoomResult;

/* gen:1518-1593: the whole room as the console composes it against its drawing. `pls[i]` is
 * rg_find_placements of ms->m[i] (the caller computes them once for all rooms). */
RgRoomResult rg_room_check(const RgWorld *w, const RgBuildModels *ms, const RgPlacementList *pls, uint16_t layoutId);

#endif
