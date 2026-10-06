/* Modified for 3DGBA (GPLv3), 2026.
 * Original: ZallaxDev/pokeemerald-3Ds-dualscreen @ c330c0a, 3ds_port/scripts/gen_voxel_relief.py (_cut_mask, _lifted,
 * FILL, cut_cells, plain_ground, _plain_tile, _plain_background, _behind: rel:2810-3000), MIT License - see
 * source/voxel/NOTICE.md. Portions Copyright (c) Dust Zallax, MIT. */
/* rg_rcut.h -- the cut tiles of a drawn layout: which pixels of a rock tile draw the ground behind it, and which
 * ground that is (3DGBA, GPLv3). Pure C. Spec: docs/phase33-romgen/SPEC-S3.md sections 1.5, 4, 6 (S3.7).
 *
 * Upstream's art object is `layout_art(lid)` (the Lavaridge-art layouts are read through the General tileset's
 * recolouring, rel:459-490); here that is RgCutArt: the layout, its open tileset pair and its RgAlias (NULL when
 * the layout is read as it is), with the 16x16 colour images memoised per metatile. `plain_ground`'s per-metatile
 * summary is a MODULE-LEVEL cache upstream (`_PLAIN`, keyed by metatile id alone, first computation wins), so it
 * lives in RgCutShared, one per relief build, shared by every layout in export order. */
#ifndef RG_RCUT_H
#define RG_RCUT_H

#include "rg_ralias.h"
#include "rg_rlat.h"
#include "rg_world.h"

#define RG_CUT_LIFT 2.0              /* pixels: background lifted more than this is cut away (rel:2813) */
#define RG_CUT_PIXELS 4              /* and only where that many pixels of it are (rel:2814) */
#define RG_GOES_ON 2                 /* rel:567: a fill that goes on under the cell's back */
#define RG_NO_FACE 0xFFFEu           /* voxel_relief.h VOXEL_RELIEF_NO_FACE (rel:3089) */
#define RG_MT_MAX 1024u              /* metatile ids are 10 bits */

typedef struct RgCutShared {
    uint8_t plainHave[RG_MT_MAX];    /* `_PLAIN[m]` is set */
    uint32_t plainC[RG_MT_MAX];      /* its commonest colour 0xRRGGBB */
    uint16_t plainCount[RG_MT_MAX];  /* how many pixels of it */
    uint16_t plainForeign[RG_MT_MAX];/* pixels more than 150 away in summed channel distance */
    void *scratch;                   /* rg_commonest scratch for 256 keys */
    bool oom;                        /* sticky: an image allocation failed (results then not trustworthy) */
} RgCutShared;

bool rg_cut_shared_init(RgCutShared *s);
void rg_cut_shared_free(RgCutShared *s);

typedef struct RgCutArt {
    RgCutShared *sh;
    const RgWorld *w;
    const RgLayout *L;
    RgPair *pair;                    /* the layout's own pair */
    const RgAlias *alias;            /* NULL: a plain LayoutArt */
    uint32_t *img[RG_MT_MAX];        /* cell_image(m) as 256 x 0xRRGGBB, j-major (pixel (i, j) at j*16 + i) */
    int8_t plainTile[RG_MT_MAX];     /* _plain_tile memo: -1 unknown */
} RgCutArt;

/* All three borrowed (not owned): `shared`, `pair` and `alias` outlive the art. */
void rg_cut_art_init(RgCutArt *a, RgCutShared *shared, const RgWorld *w, const RgLayout *L, RgPair *pair,
                     const RgAlias *alias);
void rg_cut_art_free(RgCutArt *a);
/* art.metatile(x, y) / art.own_metatile(x, y) (rel:467-477) and cell_image(m) (NULL on no memory). */
uint16_t rg_cut_meta(const RgCutArt *a, int x, int y);
uint16_t rg_cut_own(const RgCutArt *a, int x, int y);
const uint32_t *rg_cut_image(RgCutArt *a, uint16_t m);
/* ROCK_ALL / ROCK_TOP | ROCK_RIM membership of 0xRRGGBB (rel:559-563). */
bool rg_rock_all_rgb(uint32_t rgb);
bool rg_rock_light_rgb(uint32_t rgb);
/* commonest() over n colours in list order (rel:148-150, set-order tie-break). n in 1..256. */
uint32_t rg_cut_commonest(RgCutShared *s, const uint32_t *px, unsigned n);

/* rel:2828-2847 _cut_mask: rows of bits, bit i of rows[j] = 1 where cell (x, y) draws the ground behind its rock.
 * `rock` = w*h flags (the cells counted as rock). Returns false for upstream None (nothing beside it is flat ground). */
bool rg_cut_mask(RgCutArt *a, int x, int y, const uint8_t *rock, uint16_t rows[16]);
/* rel:2850-2866 _lifted: pixels of the background the lattice lifts off its foot (> CUT_LIFT), and the foot. */
unsigned rg_cut_lifted(const RgGrid *g, const uint16_t mask[16], double *foot);
/* rel:2934-2947 _plain_tile, rel:2950-2969 _plain_background. */
bool rg_cut_plain_tile(RgCutArt *a, uint16_t m);
bool rg_cut_plain_background(RgCutArt *a, uint16_t m, const uint16_t mask[16], uint16_t ground);
/* rel:2904-2948 plain_ground: the metatile of the plainest ground near (x, y), or -1 (upstream None; only when
 * kind_forced). `side`: 0 none, 'S', 'E', 'W'. kind: the forced colour (used when kind_forced). */
int32_t rg_cut_plain_ground(RgCutArt *a, const uint8_t *content, int x, int y, char side, bool kind_forced,
                            uint32_t kind);
/* rel:2975-3000 _behind: the ground a cut tile is drawn over, or -1. mask may be NULL. */
int32_t rg_cut_behind(RgCutArt *a, const uint8_t *rock, int x, int y, const uint16_t *mask);

/* One cut_cells entry (rel:2870-2900): (x, y, metatile, mask, foot, ground behind), behind -1 = None. */
typedef struct RgCutRec {
    uint8_t x, y;
    uint16_t meta;
    uint16_t mask[16];
    double foot;
    int32_t behind;
} RgCutRec;

/* cut_cells for a drawn layout: `rock` = w*h flags of the cells that are rock tiles (kinds), `h` the lattice.
 * Records in (y, x) order. *out is malloc'd (NULL when 0); returns the count or -1 on no memory. */
int rg_cut_cells(RgCutArt *a, const uint8_t *rock, const RgLat *h, RgCutRec **out);

#endif
