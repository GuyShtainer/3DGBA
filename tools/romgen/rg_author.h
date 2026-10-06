/* rg_author.h -- the building-authoring toolchain (3DGBA original work, GPLv3). Host only.
 * Spec: docs/phase34-frlg/SPEC.md section 5.2. `romgen author ROM <command> ...`. */
#ifndef RG_AUTHOR_H
#define RG_AUTHOR_H

#include <stdbool.h>
#include <stdio.h>

#include "rg_buildings.h"
#include "rg_world.h"

/* One building placement of the census: the doors of one outdoor map that lead to one indoor destination map. */
typedef struct RgCensusRow {
    uint8_t group, num;          /* the outdoor map */
    uint16_t layout;             /* its layout id */
    uint8_t mapsec;              /* the map header's region map section */
    uint8_t destGroup, destNum;  /* the indoor destination map */
    uint8_t nDoors;              /* door warps of this placement (capped at RG_CENSUS_DOORS stored) */
    int16_t door[8][2];
    int16_t rect[4];             /* the seed rect: x, y, w, h */
    uint32_t sig;                /* FNV-1a-32 of the rect's blockdata cells (w, h, then u16 each) */
    int model;                   /* index of the first covering model in the table, -1 = none */
    bool sevii;
} RgCensusRow;

typedef struct RgCensus {
    RgCensusRow *row;
    unsigned n;
    unsigned outdoorWarps;       /* every warp event on an outdoor map (FR rev 1: 277 = 191 + 55 + 31) */
    unsigned doorWarps;          /* ... whose destination is an indoor map (type 8): the doors (191) */
    unsigned mapWarps, caveWarps;   /* ... to another outdoor map (type 3, 55) / a cave (type 4, 31); not buildings */
    unsigned outdoorMaps;
    unsigned mainland, sevii;    /* placements */
    unsigned covered;
} RgCensus;

/* Builds the census of an opened FRLG (or Emerald) world; `specs` (may be NULL / 0) fills the `model` column. */
bool rg_author_census(const RgWorld *w, const RgSpec *specs, unsigned nSpecs, RgCensus *out);
void rg_census_free(RgCensus *c);
/* The mapsec names the census uses to split the Sevii Islands off ("" = unknown). */
const char *rg_author_mapsec_name(unsigned mapsec);
bool rg_author_mapsec_is_sevii(unsigned mapsec);

/* The commands. `outDir` is the directory the images land in (created). Return 0 on success. */
int rg_author_art(const RgWorld *w, const char *outDir, unsigned layout, int x, int y, int cw, int ch);
int rg_author_preview(const RgWorld *w, const char *outDir, const RgSpec *spec);
int rg_author_check(const RgWorld *w, const RgSpec *spec, int expect, FILE *fp);   /* 0 pass, 1 fail */
int rg_author_placements(const RgWorld *w, const RgSpec *spec, FILE *fp);
/* T1: foliage metatiles and 2x2 tree blocks of the General tileset; with sheetHi > sheetLo a contact-sheet PNG instead. */
int rg_author_trees(const RgWorld *w, FILE *fp, const char *outDir, int sheetLo, int sheetHi);
int rg_author_trees_list(const RgWorld *w, FILE *fp);

/* Pixel helpers shared with the tests. */
int rg_author_mkdir_p(const char *path);

/* argv[1] == "author". */
int rg_author_main(int argc, char **argv);

#endif
