/* Modified for 3DGBA (GPLv3), 2026.
 * Original: ZallaxDev/pokeemerald-3Ds-dualscreen @ c330c0a, 3ds_port/scripts/gen_voxel_relief.py
 * (alias_of, AliasArt, layout_art, own_id: rel:408-502), MIT License - see source/voxel/NOTICE.md.
 * Portions Copyright (c) Dust Zallax, MIT. */
/* rg_ralias.h -- Lavaridge-art layouts read as the General tileset's rock (3DGBA, GPLv3). Pure C.
 * Spec: docs/phase33-romgen/SPEC-S3.md section 1.5 (rg_ralias), slice S3.2. */
#ifndef RG_RALIAS_H
#define RG_RALIAS_H

#include "rg_bimg.h"
#include "rg_world.h"

/* alias_of's result for one layout of RG_ALIAS_LAYOUTS (13, 136, 292). */
typedef struct RgAlias {
    uint16_t layoutId;
    unsigned nAlias;
    uint16_t *own, *gen;            /* alias: own id -> General id, in upstream's dict (first-use) order */
    unsigned nBack;
    uint16_t *backGen, *backOwn;    /* back: General id -> own id (the most used own id of each General id) */
    unsigned nColour;
    uint32_t *colFrom, *colTo;      /* colour: own rgb -> General rgb, 0xRRGGBB, insertion order */
    uint16_t toGen[1024];           /* alias.get(m, m), dense */
    uint16_t toOwn[1024];           /* back.get(m, m), dense */
} RgAlias;

/* rel:422-458. *out = NULL (and RG_OK) when upstream returns None: not an alias layout, not on the Lavaridge
 * secondary, or no tile of it is a recoloured General rock tile. Free with rg_alias_free. */
RgErr rg_alias_of(const RgWorld *w, uint16_t layoutId, RgAlias **out);
void rg_alias_free(RgAlias *a);

/* AliasArt.metatile / own_metatile (rel:471-477): the metatile the analysis reads at (x, y), and the layout's own.
 * `a` may be NULL (the plain LayoutArt). */
uint16_t rg_alias_metatile(const RgAlias *a, const RgLayout *L, int x, int y);
/* own_id (rel:491-502): the metatile the console has for an id the analysis read. */
uint16_t rg_alias_own_id(const RgAlias *a, uint16_t m);
/* AliasArt.cell_image (rel:479-490): the layout's own pair draws, ids >= 512 recoloured through the colour map. */
bool rg_alias_cell_image(const RgAlias *a, RgPair *ownPair, uint16_t m, bool lowerOnly, RgImage *out16);

#endif
