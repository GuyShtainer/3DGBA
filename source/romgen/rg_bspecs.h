/* Modified for 3DGBA (GPLv3), 2026.
 * Original: ZallaxDev/pokeemerald-3Ds-dualscreen @ c330c0a, 3ds_port/scripts/voxel_building_specs.py
 * (the SPECS table and its part builders), MIT License - see source/voxel/NOTICE.md.
 * Portions Copyright (c) Dust Zallax, MIT. */
/* rg_bspecs.h -- the building specs table and the part builders (3DGBA, GPLv3). Pure C. SPEC-S2 section 1.5, 3.
 *
 * S2.3 holds the two Littleroot houses; the rest of sp:1092-1375 arrives with S2.4-S2.6. */
#ifndef RG_BSPECS_H
#define RG_BSPECS_H

#include "rg_bcheck.h"

typedef enum { RG_SPEC_DIRECT, RG_SPEC_COMPONENTS, RG_SPEC_KIT, RG_SPEC_PROPS, RG_SPEC_INTERIOR } RgSpecKind;

/* S2.5 expander configs (the dict members of sp:1092-1375 rows with "components" / "kit" / "props"). */
typedef struct RgComponentsCfg {
    uint32_t secondaryAddr;      /* the secondary tileset's GBA address (gTileset_Petalburg, gTileset_Rustboro) */
    const uint16_t *tiles;
    unsigned nTiles;
    int height, hull, bridge, block;   /* block 0 = no blocks */
    bool upper;
    int flank;                   /* a metatile, -1 = none */
} RgComponentsCfg;

typedef struct RgKitCfg {
    uint16_t corner, foot;
    uint16_t top[2], end[2];
    uint8_t nTop, nEnd;
    uint8_t firstRoofRow;        /* flat_block_exact's third argument (7 stone, 8 olive) */
} RgKitCfg;

typedef enum { RG_OBJ_SEA_ROCK, RG_OBJ_SAND_BOULDER, RG_OBJ_SEA_STACK, RG_OBJ_COUNT } RgPropObject;
typedef struct RgPropsCfg {
    RgPropObject object;
    double rise;
    int step;
    int ring[1][3];
    unsigned nRing;
} RgPropsCfg;

typedef struct RgSpec {
    const char *name;
    RgSpecKind kind;
    uint16_t layoutId;           /* resolved 1-based id (SPEC-S2 section 3.1) */
    uint32_t layoutFnv;          /* FNV-1a-32 of the layout's blockdata: the name pin */
    int16_t rect[4];             /* x, y, w, h in cells (int16, the spec had int8: the expanders' rects can exceed 127) */
    int8_t matchRows[2];         /* (r0, r1); both 0 = default (0, h) */
    uint16_t ground[4];
    uint8_t nGround;
    const RgExact *exact;
    uint8_t nExact;
    bool (*parts)(const struct RgSpec *s, int arg0, int arg1, RgPartList *out);   /* the builder */
    int16_t arg0, arg1;          /* the lambda's arguments */
    const void *ext;             /* RgComponentsCfg / RgKitCfg / RgPropsCfg for the expanding kinds, else NULL */
} RgSpec;

extern const RgSpec rg_specs[];
extern const unsigned rg_spec_count;

/* sp:46-96 littleroot_house(plaster_x). Appends [ground_floor, roof_lo, storey, roof_hi]. */
bool rg_littleroot_house(const RgSpec *s, int plasterX, int unused, RgPartList *out);

/* S2.4 direct builders (sp:101-577). Each appends its parts in upstream list order. */
bool rg_littleroot_lab(const RgSpec *s, int a0, int a1, RgPartList *out);       /* sp:101 */
bool rg_pokemon_center(const RgSpec *s, int a0, int a1, RgPartList *out);       /* sp:164, crown = true */
bool rg_poke_mart(const RgSpec *s, int a0, int a1, RgPartList *out);            /* sp:164, crown = false */
bool rg_oldale_house(const RgSpec *s, int a0, int a1, RgPartList *out);         /* sp:204 */
bool rg_briney_house(const RgSpec *s, int a0, int a1, RgPartList *out);         /* sp:243 */
bool rg_flower_shop(const RgSpec *s, int a0, int a1, RgPartList *out);          /* sp:292 */
bool rg_kit_house(const RgSpec *s, int width, int a1, RgPartList *out);         /* sp:327, arg0 = width px */
bool rg_gym(const RgSpec *s, int a0, int a1, RgPartList *out);                  /* sp:367 */
bool rg_devon(const RgSpec *s, int a0, int a1, RgPartList *out);                /* sp:507 */
bool rg_fountain(const RgSpec *s, int a0, int a1, RgPartList *out);             /* sp:534 */
/* sp:464, 471: a kit row's block. width/height are rect*16; arg0 = the olive block's ventilation-unit flag. */
bool rg_stone_block(const RgSpec *s, int a0, int a1, RgPartList *out);
bool rg_olive_block(const RgSpec *s, int a0, int a1, RgPartList *out);
/* sp:479: [(0, first_roof_row, width, height)]. */
void rg_flat_block_exact(RgExact *out, int width, int height, int firstRoofRow);

/* sp:482 flat_part. roof = {fixed, repeat, tail} (each a row pair), cornice, brick and rim = art rects. */
bool rg_flat_part(RgPartList *out, const char *name, double x0, double x1, double front, double back,
                  const double roof[3][2], const double cornice[2], double facade_top, const double brick[4],
                  const double rim[4], bool west, bool east);
/* sp:408 flat_block; unit = NULL or {ux0, ux1, t0, t1, t2}. Used by stone_block/olive_block (S2.5). */
bool rg_flat_block(RgPartList *out, double width, double height, const double roof[3][2], const double cornice[2],
                   double facade_top, const double *unit);

/* Phase 34 side walls (Kanto table only). A prism's end faces are drawn only through a cap band, so `west`/`east` alone
 * leave a wall open. A DIRECT Kanto spec whose `ext` points at one of these gets every prism end that a camera due west or
 * due east can see closed after its builder ran (rg_close_sides, rg_prism_exposed): the leftmost x-range west, the
 * rightmost east, a step that stands out from the next prism too; the inner slices of a run stay open. A prism that sets
 * its own caps is left alone. The end face is a plain patch of the model's own art: `wall` (u0, v0, u1, v1) below
 * y = `eave`, `roof` above it. `ext` is an array of entries, the first whose `part` matches a prism wins. */
typedef struct RgSideCfg {
    const char *part;            /* applies to the prisms whose name starts with this; NULL = any prism */
    double wall[4], roof[4];
    double eave;
    bool last;                   /* the last entry of the spec's array */
} RgSideCfg;
/* True (a no-op) for a spec without a RgSideCfg. False when a band could not be built. */
bool rg_close_sides(const RgSpec *s, RgPartList *parts);

/* look L6: closes every solid prism the art left open at the back (a rear slope mirrored about the ridge, else flat
 * faces over the open edges). See rg_bspecs.c. rg_close_backs_log, when set, hears what each prism got. */
/* deny (NULL = none), one per part: mask bit i = leave edge i open, bit 31 = do not mirror; trim = px of a closure
 * face left out at x0, x1 (rg_spec_parts' front guard fills it). */
typedef struct RgBackDeny { uint32_t mask; uint8_t trim[RG_PRISM_PTS][2]; } RgBackDeny;
bool rg_close_backs(const RgSpec *s, RgPartList *parts, const RgBackDeny *deny);
extern void (*rg_close_backs_log)(const char *spec, const char *part, const char *what);

/* A building spec's finished part list, as rg_build_models emits it: the builder, then rg_close_backs, then
 * rg_close_sides. The author check rebuilds the parts through this so it judges what ships. */
/* With `art` (the model's art), closure faces the front camera would see are denied and the parts rebuilt (look L6:
 * the front never changes). */
bool rg_spec_parts(const RgSpec *s, const RgImage *art, RgPartList *parts);

#endif
