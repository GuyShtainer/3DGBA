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

/* A gable seen from the front, `width` px wide and `height` art rows tall: the wall carries rows eave..height, a
 * RG_H_PITCH slope rows ridge..eave, a 5-degree cap rows 0..ridge; the top ends one pixel short of the (skipped) back
 * drop, which rg_close_backs mirrors into the rear slope. */
bool rg_h_gable(RgPartList *out, double width, double height, double eave, double ridge);
/* The same with the cap starting at art row `top` (the rows above it are not the building's). */
bool rg_h_gable_t(RgPartList *out, double width, double height, double eave, double ridge, double top);

/* A flat-roofed block `width` px wide and `height` art rows tall: the wall carries rows wallTop..height, the level top
 * the rows above it. The top ends one pixel short of the (skipped) back drop, so the face rg_close_backs lays there sits
 * behind art row 0 instead of tying with the top's back edge. Part name "block". */
bool rg_h_flat(RgPartList *out, double width, double height, double wallTop);
/* The same with the top carrying rows top..wallTop only. */
bool rg_h_flat_t(RgPartList *out, double width, double height, double wallTop, double top);

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

/* ---- Verdanturf Town (layout 15; Ruby / Sapphire retarget it, rg_rsspecs.c) ---- */
#define L_H_VERDANTURF 15, 0x8866E384u
#define H_GRASS_V {0x001, 0x204, 0x205}, 3
bool rg_h_verdanturf_house(const RgSpec *s, int width, int a1, RgPartList *out);
bool rg_h_battle_tent_v(const RgSpec *s, int a0, int a1, RgPartList *out);
extern const RgExact rg_h_verdanturf_house_exact[3];
extern const RgExact rg_h_verdanturf_house_w_exact[3];
extern const RgExact rg_h_battle_tent_v_exact[2];
extern const RgSideCfg rg_h_verdanturf_house_side[1];
extern const RgSideCfg rg_h_battle_tent_v_side[1];

#define RG_HSPECS_VERDANTURF_ROWS                                                                                    \
    {"verdanturf_house", RG_SPEC_DIRECT, L_H_VERDANTURF, {0, 11, 4, 4}, {0, 0}, H_GRASS_V,                         \
     rg_h_verdanturf_house_exact, 3, rg_h_verdanturf_house, 64, 0, rg_h_verdanturf_house_side},                    \
    {"verdanturf_house_w", RG_SPEC_DIRECT, L_H_VERDANTURF, {8, 11, 5, 4}, {0, 0}, H_GRASS_V,                       \
     rg_h_verdanturf_house_w_exact, 3, rg_h_verdanturf_house, 80, 0, rg_h_verdanturf_house_side},                  \
    {"battle_tent_verdanturf", RG_SPEC_DIRECT, L_H_VERDANTURF, {1, 3, 5, 5}, {0, 0}, H_GRASS_V,                    \
     rg_h_battle_tent_v_exact, 2, rg_h_battle_tent_v, 0, 0, rg_h_battle_tent_v_side},

/* ---- Fallarbor Town (layout 14; Ruby / Sapphire retarget it, rg_rsspecs.c) ---- */
#define L_H_FALLARBOR 14, 0xE11C240Cu
#define H_DIRT {0x279}, 1
bool rg_h_fallarbor_house(const RgSpec *s, int a0, int a1, RgPartList *out);
extern const RgExact rg_h_fallarbor_house_exact[3];
extern const RgSideCfg rg_h_fallarbor_house_side[1];

#define RG_HSPECS_FALLARBOR_ROWS                                                                                     \
    {"fallarbor_house_n", RG_SPEC_DIRECT, L_H_FALLARBOR, {0, 3, 4, 4}, {0, 0}, H_DIRT,                             \
     rg_h_fallarbor_house_exact, 3, rg_h_fallarbor_house, 0, 0, rg_h_fallarbor_house_side},                        \
    {"fallarbor_house_s", RG_SPEC_DIRECT, L_H_FALLARBOR, {5, 14, 4, 4}, {0, 0}, H_DIRT,                            \
     rg_h_fallarbor_house_exact, 3, rg_h_fallarbor_house, 0, 0, rg_h_fallarbor_house_side},                       \
    {"battle_tent_fallarbor", RG_SPEC_DIRECT, L_H_FALLARBOR, {6, 3, 5, 5}, {0, 0}, H_DIRT,                         \
     rg_h_battle_tent_v_exact, 2, rg_h_battle_tent_v, 0, 0, rg_h_battle_tent_v_side},

/* ---- Slateport City (layout 2; Ruby / Sapphire retarget it, rg_rsspecs.c) ---- */
#define L_H_SLATEPORT 2, 0xD57B2886u
#define H_SLATE_GROUND {0x001, 0x202, 0x211}, 3
bool rg_h_slateport_house(const RgSpec *s, int width, int a1, RgPartList *out);
bool rg_h_slateport_house_g(const RgSpec *s, int a0, int a1, RgPartList *out);
bool rg_h_fan_club(const RgSpec *s, int a0, int a1, RgPartList *out);
bool rg_h_oceanic_museum(const RgSpec *s, int a0, int a1, RgPartList *out);
bool rg_h_shipyard(const RgSpec *s, int a0, int a1, RgPartList *out);
extern const RgExact rg_h_slateport_house_exact[2];
extern const RgExact rg_h_slateport_house_w_exact[2];
extern const RgExact rg_h_slateport_house_g_exact[3];
extern const RgExact rg_h_fan_club_exact[2];
extern const RgExact rg_h_oceanic_museum_exact[6];
extern const RgExact rg_h_shipyard_exact[2];
extern const RgSideCfg rg_h_slateport_house_side[1];
extern const RgSideCfg rg_h_slateport_house_g_side[1];
extern const RgSideCfg rg_h_fan_club_side[1];
extern const RgSideCfg rg_h_oceanic_museum_side[1];
extern const RgSideCfg rg_h_shipyard_side[1];

#define RG_HSPECS_SLATEPORT_ROWS                                                                                     \
    {"slateport_house", RG_SPEC_DIRECT, L_H_SLATEPORT, {4, 16, 4, 4}, {0, 0}, H_SLATE_GROUND,                      \
     rg_h_slateport_house_exact, 2, rg_h_slateport_house, 64, 0, rg_h_slateport_house_side},                       \
    {"slateport_house_w", RG_SPEC_DIRECT, L_H_SLATEPORT, {24, 41, 6, 4}, {0, 0}, H_SLATE_GROUND,                   \
     rg_h_slateport_house_w_exact, 2, rg_h_slateport_house, 96, 0, rg_h_slateport_house_side},                     \
    {"slateport_house_g", RG_SPEC_DIRECT, L_H_SLATEPORT, {2, 22, 5, 5}, {0, 0}, H_SLATE_GROUND,                    \
     rg_h_slateport_house_g_exact, 3, rg_h_slateport_house_g, 0, 0, rg_h_slateport_house_g_side},                  \
    {"slateport_fan_club", RG_SPEC_DIRECT, L_H_SLATEPORT, {25, 7, 7, 6}, {0, 0}, H_SLATE_GROUND,                   \
     rg_h_fan_club_exact, 2, rg_h_fan_club, 0, 0, rg_h_fan_club_side},                                             \
    {"oceanic_museum", RG_SPEC_DIRECT, L_H_SLATEPORT, {28, 22, 6, 6}, {0, 0}, H_SLATE_GROUND,                      \
     rg_h_oceanic_museum_exact, 6, rg_h_oceanic_museum, 0, 0, rg_h_oceanic_museum_side},                           \
    {"slateport_shipyard", RG_SPEC_DIRECT, L_H_SLATEPORT, {24, 32, 8, 7}, {0, 0}, H_SLATE_GROUND,                  \
     rg_h_shipyard_exact, 2, rg_h_shipyard, 0, 0, rg_h_shipyard_side},                                             \
    {"battle_tent_slateport", RG_SPEC_DIRECT, L_H_SLATEPORT, {8, 8, 5, 5}, {0, 0}, H_SLATE_GROUND,                 \
     rg_h_battle_tent_v_exact, 2, rg_h_battle_tent_v, 0, 0, rg_h_battle_tent_v_side},

/* ======== Phase 36 slice H2 ======== */

/* ---- Fortree City (layout 5) ---- */
#define L_H_FORTREE 5, 0xC5D353C6u
#define H_FOREST {0x0C6}, 1
bool rg_h_fortree_hut(const RgSpec *s, int a0, int a1, RgPartList *out);
extern const RgExact rg_h_fortree_hut_exact[3];
extern const RgSideCfg rg_h_fortree_hut_side[2];

#define RG_HSPECS_FORTREE_ROWS                                                                                       \
    {"fortree_hut", RG_SPEC_DIRECT, L_H_FORTREE, {8, 1, 5, 3}, {1, 3}, H_FOREST, rg_h_fortree_hut_exact, 3,         \
     rg_h_fortree_hut, 0, 0, rg_h_fortree_hut_side},                                                                 \
    {"gym_fortree", RG_SPEC_DIRECT, L_H_FORTREE, {19, 7, 6, 5}, {0, 0}, H_GRASS1, kGymExact, 4, rg_gym, 0, 0, NULL},

/* ---- Lavaridge Town (layout 13) ---- */
#define L_H_LAVARIDGE 13, 0x306B069Fu
#define RG_HSPECS_LAVARIDGE_ROWS                                                                                     \
    {"gym_lavaridge", RG_SPEC_DIRECT, L_H_LAVARIDGE, {2, 11, 6, 5}, {0, 0}, H_GRASS1, kGymExact, 4, rg_gym, 0, 0,   \
     NULL},

/* ---- Pacifidlog Town (layout 16): the five huts are one drawing, one model ---- */
#define L_H_PACIFIDLOG 16, 0x0CDAE2A1u
#define H_SEA {0x170}, 1
/* the plank deck (0x221, under all five huts) and the open sea are both ground, not hut art; the footprint fill
 * comes from try_place (rg_buildings.c), which picks the planks for a building in the sea */
#define H_PACIFIDLOG_DECK {0x221, 0x170}, 2
bool rg_h_pacifidlog_hut(const RgSpec *s, int a0, int a1, RgPartList *out);
extern const RgExact rg_h_pacifidlog_hut_exact[3];

#define RG_HSPECS_PACIFIDLOG_ROWS                                                                                    \
    {"pacifidlog_hut", RG_SPEC_DIRECT, L_H_PACIFIDLOG, {15, 10, 3, 4}, {0, 0}, H_PACIFIDLOG_DECK, rg_h_pacifidlog_hut_exact,    \
     3, rg_h_pacifidlog_hut, 0, 0, NULL},

/* ======== Phase 36 slice H3 ======== */

/* ---- Lilycove City (layout 6; Ruby / Sapphire retarget every row, rg_rsspecs.c: no RS-own row) ---- */
#define L_H_LILYCOVE 6, 0x7A998B47u
bool rg_h_lilycove_house(const RgSpec *s, int width, int a1, RgPartList *out);
extern const RgExact rg_h_lilycove_house_exact[2];
extern const RgExact rg_h_lilycove_house_w_exact[2];
extern const RgSideCfg rg_h_lilycove_house_side[1];
bool rg_h_lilycove_store(const RgSpec *s, int a0, int a1, RgPartList *out);
bool rg_h_lilycove_museum(const RgSpec *s, int a0, int a1, RgPartList *out);
bool rg_h_lilycove_hall(const RgSpec *s, int a0, int a1, RgPartList *out);
bool rg_h_lilycove_pavilion(const RgSpec *s, int a0, int a1, RgPartList *out);
bool rg_h_lilycove_wood(const RgSpec *s, int a0, int a1, RgPartList *out);
bool rg_h_lilycove_cave(const RgSpec *s, int a0, int a1, RgPartList *out);
extern const RgExact rg_h_lilycove_cave_exact[2];
extern const RgSideCfg rg_h_lilycove_cave_side[1];
extern const RgExact rg_h_lilycove_store_exact[2];
extern const RgExact rg_h_lilycove_museum_exact[4];
extern const RgExact rg_h_lilycove_hall_exact[4];
extern const RgExact rg_h_lilycove_pavilion_exact[3];
extern const RgExact rg_h_lilycove_wood_exact[3];
extern const RgSideCfg rg_h_lilycove_store_side[1];
extern const RgSideCfg rg_h_lilycove_museum_side[1];
extern const RgSideCfg rg_h_lilycove_hall_side[1];
extern const RgSideCfg rg_h_lilycove_pavilion_side[1];
extern const RgSideCfg rg_h_lilycove_wood_side[1];

#define RG_HSPECS_LILYCOVE_ROWS                                                                                     \
    {"lilycove_house", RG_SPEC_DIRECT, L_H_LILYCOVE, {54, 12, 4, 4}, {2, 4}, H_GRASS1, rg_h_lilycove_house_exact,    \
     2, rg_h_lilycove_house, 64, 0, rg_h_lilycove_house_side},                                                       \
    {"lilycove_house_w", RG_SPEC_DIRECT, L_H_LILYCOVE, {37, 11, 5, 4}, {0, 0}, H_GRASS1,                            \
     rg_h_lilycove_house_w_exact, 2, rg_h_lilycove_house, 80, 0, rg_h_lilycove_house_side},                         \
    {"lilycove_store", RG_SPEC_DIRECT, L_H_LILYCOVE, {23, 0, 9, 7}, {0, 0}, H_GRASS1, rg_h_lilycove_store_exact, 2,  \
     rg_h_lilycove_store, 0, 0, rg_h_lilycove_store_side},                                                           \
    {"lilycove_museum", RG_SPEC_DIRECT, L_H_LILYCOVE, {7, 0, 10, 6}, {0, 0}, H_GRASS1, rg_h_lilycove_museum_exact,   \
     4, rg_h_lilycove_museum, 0, 0, rg_h_lilycove_museum_side},                                                      \
    {"lilycove_hall", RG_SPEC_DIRECT, L_H_LILYCOVE, {20, 18, 7, 7}, {0, 0}, H_GRASS1, rg_h_lilycove_hall_exact, 4,   \
     rg_h_lilycove_hall, 0, 0, rg_h_lilycove_hall_side},                                                             \
    {"lilycove_pavilion", RG_SPEC_DIRECT, L_H_LILYCOVE, {9, 27, 7, 6}, {0, 0}, H_GRASS1,                            \
     rg_h_lilycove_pavilion_exact, 3, rg_h_lilycove_pavilion, 0, 0, rg_h_lilycove_pavilion_side},                    \
    {"lilycove_wood", RG_SPEC_DIRECT, L_H_LILYCOVE, {36, 20, 6, 5}, {0, 0}, H_GRASS1, rg_h_lilycove_wood_exact, 3,   \
     rg_h_lilycove_wood, 0, 0, rg_h_lilycove_wood_side},                                                             \
    {"lilycove_cave", RG_SPEC_DIRECT, L_H_LILYCOVE, {69, 4, 3, 2}, {0, 0}, H_GRASS1, rg_h_lilycove_cave_exact, 2,    \
     rg_h_lilycove_cave, 0, 0, rg_h_lilycove_cave_side},

/* ======== Phase 36 slice H4 ======== */

/* ---- Mossdeep City (layout 7) ---- */
#define L_H_MOSSDEEP 7, 0xAD4D637Fu
bool rg_h_mossdeep_house(const RgSpec *s, int width, int a1, RgPartList *out);
bool rg_h_mossdeep_space(const RgSpec *s, int a0, int a1, RgPartList *out);
bool rg_h_mossdeep_wide(const RgSpec *s, int a0, int a1, RgPartList *out);
extern const RgExact rg_h_mossdeep_wide_exact[3];
extern const RgSideCfg rg_h_mossdeep_wide_side[1];
extern const RgExact rg_h_mossdeep_house_exact[2];
extern const RgExact rg_h_mossdeep_space_exact[5];
extern const RgSideCfg rg_h_mossdeep_house_side[1];
extern const RgSideCfg rg_h_mossdeep_space_side[1];

#define RG_HSPECS_MOSSDEEP_ROWS                                                                                      \
    {"mossdeep_house", RG_SPEC_DIRECT, L_H_MOSSDEEP, {17, 13, 4, 4}, {1, 4}, H_GRASS1, rg_h_mossdeep_house_exact,    \
     2, rg_h_mossdeep_house, 64, 0, rg_h_mossdeep_house_side},                                                       \
    {"mossdeep_space", RG_SPEC_DIRECT, L_H_MOSSDEEP, {60, 8, 9, 8}, {0, 0}, H_GRASS1, rg_h_mossdeep_space_exact, 5,  \
     rg_h_mossdeep_space, 0, 0, rg_h_mossdeep_space_side},                                                           \
    {"mossdeep_wide", RG_SPEC_DIRECT, L_H_MOSSDEEP, {35, 20, 5, 5}, {0, 0}, {0x001, 0x124}, 2, rg_h_mossdeep_wide_exact, 3,   \
     rg_h_mossdeep_wide, 0, 0, rg_h_mossdeep_wide_side},

/* ---- Sootopolis City (layout 8): stone floor 0x2D9 (729) and its variants; the lake is 0x279 (633) ---- */
#define L_H_SOOTOPOLIS 8, 0xA3CC5D0Bu
#define H_STONE {0x2D9, 0x2E1, 0x2D3, 0x2E2}, 4
bool rg_h_sootopolis_tower(const RgSpec *s, int a0, int a1, RgPartList *out);
bool rg_h_sootopolis_box(const RgSpec *s, int a0, int a1, RgPartList *out);
extern const RgExact rg_h_sootopolis_tower_exact[2];
extern const RgExact rg_h_sootopolis_box_exact[2];
extern const RgSideCfg rg_h_sootopolis_tower_side[1];
extern const RgSideCfg rg_h_sootopolis_box_side[1];

#define RG_HSPECS_SOOTOPOLIS_ROWS                                                                                    \
    {"sootopolis_tower", RG_SPEC_DIRECT, L_H_SOOTOPOLIS, {43, 14, 3, 4}, {1, 4}, H_STONE, rg_h_sootopolis_tower_exact,\
     2, rg_h_sootopolis_tower, 0, 0, rg_h_sootopolis_tower_side},                                                    \
    {"sootopolis_box", RG_SPEC_DIRECT, L_H_SOOTOPOLIS, {44, 3, 3, 4}, {2, 4}, H_STONE, rg_h_sootopolis_box_exact, 2, \
     rg_h_sootopolis_box, 0, 0, rg_h_sootopolis_box_side},                                                           \
    {"gym_sootopolis", RG_SPEC_DIRECT, L_H_SOOTOPOLIS, {28, 28, 6, 5}, {0, 0}, H_GRASS1, kGymExact, 4, rg_gym, 0, 0, \
     NULL},

/* ======== Phase 36 slice H5 ======== */

/* ---- routes and landmarks: one layout per map, pins read off each layout (Ruby / Sapphire retarget them, rg_rsspecs.c) ---- */
#define L_H_EVERGRANDE 9, 0x5476D808u
#define L_H_R109 25, 0xA525346Du
#define L_H_R110 26, 0x4A7EB534u
#define L_H_R111 27, 0x31657B1Au
#define L_H_R112 28, 0xBE8B08F3u
#define L_H_R113 29, 0xA32A722Cu
#define L_H_R114 30, 0x5DAFC653u
#define L_H_R119 35, 0xB691459Du
#define L_H_R121 37, 0xCDAAAC93u
#define L_H_CHIMNEY 136, 0x737C0DC4u
#define L_H_FRONTIER 241, 0xB13E9480u
bool rg_h_league_gate(const RgSpec *s, int a0, int a1, RgPartList *out);
bool rg_h_seashore_house(const RgSpec *s, int width, int a1, RgPartList *out);
bool rg_h_trick_house(const RgSpec *s, int a0, int a1, RgPartList *out);
bool rg_h_cycling_gate(const RgSpec *s, int width, int a1, RgPartList *out);
bool rg_h_cable_car(const RgSpec *s, int a0, int a1, RgPartList *out);
bool rg_h_slat_house(const RgSpec *s, int a0, int a1, RgPartList *out);
bool rg_h_weather_institute(const RgSpec *s, int a0, int a1, RgPartList *out);
bool rg_h_entrance_gate(const RgSpec *s, int width, int a1, RgPartList *out);
extern const RgExact rg_h_league_gate_exact[2];
extern const RgExact rg_h_seashore_house_exact[2];
extern const RgExact rg_h_trick_house_exact[3];
extern const RgExact rg_h_cycling_gate_exact[2];
extern const RgExact rg_h_winstrate_house_exact[2];
extern const RgExact rg_h_cable_car_exact[2];
extern const RgExact rg_h_slat_house_exact[2];
extern const RgExact rg_h_weather_institute_exact[2];
extern const RgExact rg_h_entrance_gate_exact[2];
extern const RgExact rg_h_entrance_gate_wide_exact[2];
extern const RgSideCfg rg_h_league_gate_side[1];
extern const RgSideCfg rg_h_seashore_house_side[1];
extern const RgSideCfg rg_h_trick_house_side[1];
extern const RgSideCfg rg_h_gate_side[1];
extern const RgSideCfg rg_h_cable_car_side[1];
extern const RgSideCfg rg_h_slat_house_side[1];
extern const RgSideCfg rg_h_weather_institute_side[1];
extern const RgSideCfg rg_h_entrance_gate_side[1];

#define RG_HSPECS_ROUTES_ROWS                                                                                        \
    {"league_gate", RG_SPEC_DIRECT, L_H_EVERGRANDE, {13, 0, 11, 6}, {0, 0}, H_GRASS1, rg_h_league_gate_exact, 2,     \
     rg_h_league_gate, 0, 0, rg_h_league_gate_side},                                                                 \
    {"seashore_house", RG_SPEC_DIRECT, L_H_R109, {10, 2, 5, 4}, {0, 0}, {0x124}, 1, rg_h_seashore_house_exact, 2,    \
     rg_h_seashore_house, 80, 0, rg_h_seashore_house_side},                                                          \
    {"trick_house", RG_SPEC_DIRECT, L_H_R110, {9, 61, 5, 6}, {0, 0}, H_GRASS1, rg_h_trick_house_exact, 3,           \
     rg_h_trick_house, 0, 0, rg_h_trick_house_side},                                                                 \
    {"cycling_gate", RG_SPEC_DIRECT, L_H_R110, {14, 12, 6, 5}, {2, 5}, H_GRASS1, rg_h_cycling_gate_exact, 2,         \
     rg_h_cycling_gate, 96, 0, rg_h_gate_side},                                                                      \
    {"winstrate_house", RG_SPEC_DIRECT, L_H_R111, {11, 109, 5, 5}, {0, 0}, H_GRASS1, rg_h_winstrate_house_exact, 2,    \
     rg_h_cycling_gate, 80, 0, rg_h_gate_side},                                                                      \
    {"cable_car_station", RG_SPEC_DIRECT, L_H_R112, {27, 23, 6, 5}, {0, 0}, {0x271}, 1, rg_h_cable_car_exact, 2,     \
     rg_h_cable_car, 0, 0, rg_h_cable_car_side},                                                                     \
    {"glass_workshop", RG_SPEC_DIRECT, L_H_R113, {32, 2, 4, 4}, {0, 0}, {0x20A}, 1, rg_h_slat_house_exact, 2,        \
     rg_h_slat_house, 0, 0, rg_h_slat_house_side},                                                                   \
    {"route114_house", RG_SPEC_DIRECT, L_H_R114, {28, 2, 4, 4}, {0, 0}, {0x279}, 1, rg_h_slat_house_exact, 2,        \
     rg_h_slat_house, 0, 0, rg_h_slat_house_side},                                                                   \
    {"weather_institute", RG_SPEC_DIRECT, L_H_R119, {1, 26, 11, 7}, {0, 0}, H_GRASS1, rg_h_weather_institute_exact, 2,\
     rg_h_weather_institute, 0, 0, rg_h_weather_institute_side},                                                     \
    {"entrance_gate", RG_SPEC_DIRECT, L_H_R121, {35, 0, 5, 6}, {0, 0}, H_GRASS1, rg_h_entrance_gate_exact, 2,        \
     rg_h_entrance_gate, 80, 0, rg_h_entrance_gate_side},                                                            \
    {"entrance_gate_wide", RG_SPEC_DIRECT, L_H_FRONTIER, {30, 29, 10, 5}, {0, 0}, H_GRASS1,                         \
     rg_h_entrance_gate_wide_exact, 2, rg_h_entrance_gate, 160, 0, rg_h_entrance_gate_side},                         \
    {"cable_car_chimney", RG_SPEC_DIRECT, L_H_CHIMNEY, {14, 32, 6, 5}, {0, 0}, {0x271}, 1, rg_h_cable_car_exact, 2,  \
     rg_h_cable_car, 0, 0, rg_h_cable_car_side},

#endif
