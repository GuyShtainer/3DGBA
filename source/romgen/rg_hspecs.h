/* rg_hspecs.h -- the Phase 36 Hoenn building recipes (3DGBA original work, GPLv3). Pure C.
 * Plan: docs/phase36-hoenn/PHASE.md.
 *
 * The Hoenn recipes are Emerald rows: rg_bspecs.c puts the RG_HSPECS_* row lists below into `rg_specs`, ahead of the
 * interior rows, so every consumer of that table (romgen, the author tool, the tests) sees them, and Ruby / Sapphire get
 * them through rg_rsspecs.c's copy of the table. Each town's builders, exact rects and side patches live in
 * rg_hspecs_<town>.c, in the Kanto recipes' style: profile prisms whose visible faces are PROJ edges. */
#ifndef RG_HSPECS_H
#define RG_HSPECS_H

#include "rg_bspecs.h"

/* Roof slopes are authored at rg_close_backs' L6b pitch, so they are not laid down again. */
#define RG_H_PITCH 15.0

/* A profile prism over x0..x1: poly points (z, y) counter-clockwise, edge i PROJ-textured from art rows rows[i]
 * ({0, 0} = the edge is skipped). False on no memory or a bad point count. */
bool rg_h_profile(RgPartList *out, const char *name, double x0, double x1, bool ends, unsigned n,
                  const double (*pts)[2], const double (*rows)[2]);
/* The end (z1, y1) of a slope that climbs from (z0, y0) over `rows` art rows at `deg` degrees (art row = z - y). */
void rg_h_slope(double z0, double y0, double rows, double deg, double *z1, double *y1);

/* ---- Dewford Town (layout 12) ---- */
#define L_H_DEWFORD 12, 0xB67B1972u
#define H_SAND {0x124}, 1
#define RG_H_DEWFORD_HOUSE_W_NEXACT 2
#define RG_H_DEWFORD_HOUSE_NEXACT 2
bool rg_h_dewford_house(const RgSpec *s, int width, int a1, RgPartList *out);
extern const RgExact rg_h_dewford_house_w_exact[RG_H_DEWFORD_HOUSE_W_NEXACT];
extern const RgExact rg_h_dewford_house_exact[RG_H_DEWFORD_HOUSE_NEXACT];
extern const RgSideCfg rg_h_dewford_house_side[1];

#define RG_HSPECS_DEWFORD_ROWS                                                                                       \
    {"dewford_house_w", RG_SPEC_DIRECT, L_H_DEWFORD, {1, 0, 5, 4}, {0, 0}, H_SAND, rg_h_dewford_house_w_exact,       \
     RG_H_DEWFORD_HOUSE_W_NEXACT, rg_h_dewford_house, 80, 0, rg_h_dewford_house_side},                           \
    {"dewford_house", RG_SPEC_DIRECT, L_H_DEWFORD, {16, 11, 4, 4}, {1, 4}, H_SAND, rg_h_dewford_house_exact,        \
     RG_H_DEWFORD_HOUSE_NEXACT, rg_h_dewford_house, 64, 0, rg_h_dewford_house_side},                             \
    {"gym_dewford", RG_SPEC_DIRECT, L_H_DEWFORD, {5, 13, 6, 5}, {0, 0}, H_SAND, kGymExact, 4, rg_gym, 0, 0, NULL},

/* ---- Mauville City (layout 3) ---- */
#define L_H_MAUVILLE 3, 0x6FFC5818u
#define H_GRASS1 {0x001}, 1
bool rg_h_mauville_gable(const RgSpec *s, int width, int a1, RgPartList *out);
bool rg_h_mauville_block(const RgSpec *s, int a0, int a1, RgPartList *out);
bool rg_h_game_corner(const RgSpec *s, int a0, int a1, RgPartList *out);
extern const RgExact rg_h_mauville_house_exact[2];
extern const RgExact rg_h_mauville_wide_exact[2];
extern const RgExact rg_h_mauville_block_exact[2];
extern const RgExact rg_h_game_corner_exact[5];
extern const RgSideCfg rg_h_mauville_house_side[1];
extern const RgSideCfg rg_h_mauville_block_side[1];
extern const RgSideCfg rg_h_game_corner_side[1];

#define RG_HSPECS_MAUVILLE_ROWS                                                                                      \
    {"mauville_house", RG_SPEC_DIRECT, L_H_MAUVILLE, {18, 11, 4, 4}, {0, 0}, H_GRASS1, rg_h_mauville_house_exact,    \
     2, rg_h_mauville_gable, 64, 0, rg_h_mauville_house_side},                                                       \
    {"mauville_bike", RG_SPEC_DIRECT, L_H_MAUVILLE, {34, 2, 5, 4}, {0, 0}, H_GRASS1, rg_h_mauville_wide_exact,       \
     2, rg_h_mauville_gable, 80, 0, rg_h_mauville_house_side},                                                       \
    {"mauville_house_e", RG_SPEC_DIRECT, L_H_MAUVILLE, {31, 11, 5, 4}, {0, 0}, H_GRASS1, rg_h_mauville_wide_exact,   \
     2, rg_h_mauville_gable, 80, 0, rg_h_mauville_house_side},                                                       \
    {"mauville_block", RG_SPEC_DIRECT, L_H_MAUVILLE, {36, 11, 4, 5}, {0, 0}, H_GRASS1, rg_h_mauville_block_exact,    \
     2, rg_h_mauville_block, 0, 0, rg_h_mauville_block_side},                                                        \
    {"game_corner", RG_SPEC_DIRECT, L_H_MAUVILLE, {5, 10, 7, 4}, {0, 0}, H_GRASS1, rg_h_game_corner_exact,           \
     5, rg_h_game_corner, 0, 0, rg_h_game_corner_side},

#endif
