/* Modified for 3DGBA (GPLv3), 2026.
 * Original: ZallaxDev/pokeemerald-3Ds-dualscreen @ c330c0a, 3ds_port/scripts/gen_voxel_relief.py (drawn_canvas,
 * drawn_role, ROLE_REFERENCE, rocky water, the majority vote: rel:1089-1325), MIT License - see source/voxel/NOTICE.md.
 * Portions Copyright (c) Dust Zallax, MIT. */
/* rg_rcanvas.h -- a drawn group's per-pixel canvas (3DGBA, GPLv3). Pure C.
 * Spec: docs/phase33-romgen/SPEC-S3.md sections 1.5, 6 (S3.4). Host-only in S3 (the 12 M pixel canvas of the
 * largest group is the "74 MB" slice; S3.9 bounds it). */
#ifndef RG_RCANVAS_H
#define RG_RCANVAS_H

#include "rg_ralias.h"
#include "rg_rdrawn.h"
#include "rg_roles.h"

/* Pixel kinds (rel:596: GROUND, TOP, FACE, FLECK, RIM, VOID, FREE = range(7)). */
enum { RG_K_GROUND = 0, RG_K_TOP, RG_K_FACE, RG_K_FLECK, RG_K_RIM, RG_K_VOID, RG_K_FREE, RG_K_COUNT };

/* Per-cell `flat` (rel:1103): upstream False, "floor", "water", "fall", "bridge", "signpost". Nonzero = truthy. */
enum { RG_FLAT_NONE = 0, RG_FLAT_FLOOR, RG_FLAT_WATER, RG_FLAT_FALL, RG_FLAT_BRIDGE, RG_FLAT_SIGNPOST };

/* One member layout placed on the canvas, in cells. */
typedef struct RgCanvasMember { uint16_t layout; int16_t ox, oy; uint16_t w, h; } RgCanvasMember;

/* What drawn_canvas returns (layouts, CW, CH, voted, blocked, side, flat, face_low) plus the pier set and the
 * aliased metatile of every canvas cell (metatile_at, rel:1597-1603). Arrays are malloc'd; cell arrays are
 * cw*ch row major, pixel arrays (cw*16) x (ch*16). */
typedef struct RgCanvas {
    unsigned nMem;
    RgCanvasMember *mem;         /* BFS member order of the group (upstream dict order) */
    int cw, ch;
    uint8_t *kind;               /* the voted pixel kinds: RG_K_* with FLECK voted away (upstream `voted`) */
    uint8_t *blocked;            /* a face goes down behind a roof or a tree, not a signpost */
    int8_t *side;                /* +1 a face turned west, -1 east, 0 none */
    uint8_t *flat;               /* RG_FLAT_* */
    uint8_t *faceLow;            /* a south face as drawn (upstream face_low set) */
    uint8_t *pier;               /* canvas cells of pier (upstream _PIER[name]) */
    uint16_t *meta;              /* aliased metatile of the first member holding the cell, 0xFFFF where none */
} RgCanvas;

/* State shared by every group's canvas, as upstream's module-level caches are: _ROLES_DRAWN (drawn_role by
 * (primary, secondary, metatile), first computation wins, with the reference set read once off the first art
 * seen), _ROCKY_WATER by (secondary, metatile), and the open props table. */
typedef struct RgRCtx RgRCtx;

RgRCtx *rg_rctx_new(const RgWorld *w, const RgRoles *r, RgErr *err);
void rg_rctx_free(RgRCtx *c);
/* Test hook: when set, every cache hit recomputes the value and counts a disagreement (first-wins would then
 * have changed the bytes). Returns the number of disagreements so far. */
void rg_rctx_verify(RgRCtx *c, bool on);
unsigned rg_rctx_conflicts(const RgRCtx *c);

/* rel:1089-1268, 1238-1260. Fills *out for the group's members; RG_ERR_NOMEM on failure (out left empty). */
RgErr rg_canvas_build(RgRCtx *c, const RgDrawn *d, const RgDrawnGroup *g, RgCanvas *out);
void rg_canvas_free(RgCanvas *cv);

/* rel:1226-1260: voted[y][x] = TOP if tops > faces else FACE over the clipped (2R+1)^2 window (R = MAJORITY 2),
 * for pixels of kind TOP, FACE or FLECK; every other pixel is copied. `kind` and `voted` are w*h bytes, distinct.
 * False on no memory. The window sums are exact integers (upstream's prefix sums, here as sliding column sums: same values). */
bool rg_canvas_vote(const uint8_t *kind, int w, int h, uint8_t *voted);

/* The metatile ids drawn_role reads: (role, lowerOnly) of metatile m as drawn: 0 none, 1 face, 2 west, 3 east, with
 * bit 2 set when read by its lower layer alone. Exposed for the test; uses and fills the shared cache. */
uint8_t rg_canvas_drawn_role(RgRCtx *c, const RgAlias *al, RgPair *pair, const RgLayout *L, uint16_t m);

#endif
