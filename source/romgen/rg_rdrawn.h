/* Modified for 3DGBA (GPLv3), 2026.
 * Original: ZallaxDev/pokeemerald-3Ds-dualscreen @ c330c0a, 3ds_port/scripts/gen_voxel_relief.py (find_drawn,
 * drawn_group, drawn_ok, DRAWN_EXCLUDED, map_links, _seam_cells: rel:633-851), MIT License - see source/voxel/NOTICE.md.
 * Portions Copyright (c) Dust Zallax, MIT. */
/* rg_rdrawn.h -- which layouts have drawn mountains, how they group, how maps link (3DGBA, GPLv3). Pure C.
 * Spec: docs/phase33-romgen/SPEC-S3.md sections 1.5, 2.4, 6 (S3.3).
 *
 * A group is named by its seed's layout id (upstream: the seed's lower-case name); groups compare by rg_name_rank. */
#ifndef RG_RDRAWN_H
#define RG_RDRAWN_H

#include "rg_world.h"

#define RG_DRAWN_MIN 5               /* cells of rock tile that make a layout a seed (rel:627) */
#define RG_DRAWN_SEAM 2              /* cells either side of a seam that count as "at" it (rel:628) */
#define RG_DRAWN_MAX_GROUPS 64u
#define RG_DRAWN_MAX_MEMBERS 1024u   /* pool over every group (87 layouts + 5 alternate copies fit) */
#define RG_DRAWN_MAX_LINKS 512u
#define RG_MAP_LINKS_MAX 512u

/* find_drawn's link list entry: layout b stands at a's origin + (dx, dy), in cells. Lists are kept per layout in
 * upstream's insertion order (A.5 folder order; each connection is recorded for both sides). */
typedef struct RgDrawnLink { uint16_t a, b; int16_t dx, dy; } RgDrawnLink;

typedef struct RgDrawnMember { uint16_t layout; int16_t x, y; } RgDrawnMember;   /* cells, shifted to min 0 */

typedef struct RgDrawnGroup {
    uint16_t key;                    /* seed layout id (an alternate group: the alternate's id) */
    uint8_t isAlt;
    unsigned first, nMembers;        /* into RgDrawn.pool, BFS discovery order (seed first) */
    unsigned cellsW, cellsH;         /* canvas size in cells: max(x + w), max(y + h) over the members */
} RgDrawnGroup;

typedef struct RgDrawn {
    uint8_t blockLayout[512];        /* layout id: outdoor, General primary, >= 1 rock-tile cell, not an alternate */
    uint8_t seed[512];               /* ... and >= RG_DRAWN_MIN rock-tile cells */
    uint16_t rockCount[512];         /* rock-tile cells (after alias), blockLayout only */
    unsigned nSeeds, nLinks, nGroups, nPool;
    RgDrawnLink links[RG_DRAWN_MAX_LINKS];
    RgDrawnGroup groups[RG_DRAWN_MAX_GROUPS];   /* seed groups in name order, then alternate groups (rel:725-731) */
    RgDrawnMember pool[RG_DRAWN_MAX_MEMBERS];
} RgDrawn;

/* rel:633-733. Fills *out (caller-owned, ~14 KB). RG_ERR_NOMEM / RG_ERR_TABLES on a capacity or ROM mismatch. */
RgErr rg_drawn_find(const RgWorld *w, RgDrawn *out);

/* The group a layout is drawn in, or NULL (rel:746-753). The first group containing it (dict order). With
 * checked, only a group that is not excluded and (when regionOk != NULL) whose regionOk[group index] is set:
 * that array is world_levels()["regions"] membership, supplied by S3.5; NULL here means "exclusion only". */
const RgDrawnGroup *rg_drawn_group(const RgDrawn *d, uint16_t layoutId, bool checked, const uint8_t *regionOk);
/* DRAWN_EXCLUDED (rel:771): the group whose name-giving seed is RG_EXCLUDED_GROUP_SEED. */
bool rg_drawn_excluded(const RgDrawnGroup *g);

/* rel:787-831: every connection between two outdoor layouts, ROM direction codes (1 down, 2 up, 3 left, 4 right),
 * alternates mirrored, de-duplicated, sorted by (rank a, rank b, A.7 direction key, offset). Returns the count. */
typedef struct RgMapLink { uint16_t a, b; uint8_t dir; int32_t offset; } RgMapLink;
unsigned rg_map_links_sorted(const RgWorld *w, RgMapLink *out, unsigned max);

/* rel:834-851 _seam_cells: edge codes are ROM direction codes. */
typedef struct RgSeamCell { uint8_t aEdge; int16_t aIdx; uint8_t bEdge; int16_t bIdx; } RgSeamCell;
unsigned rg_seam_cells(uint8_t dir, int offset, int aw, int ah, int bw, int bh, RgSeamCell *out, unsigned max);

#endif
