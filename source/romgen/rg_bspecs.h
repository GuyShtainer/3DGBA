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
} RgSpec;

extern const RgSpec rg_specs[];
extern const unsigned rg_spec_count;

/* sp:46-96 littleroot_house(plaster_x). Appends [ground_floor, roof_lo, storey, roof_hi]. */
bool rg_littleroot_house(const RgSpec *s, int plasterX, int unused, RgPartList *out);

#endif
