/* Modified for 3DGBA (GPLv3), 2026.
 * Original: ZallaxDev/pokeemerald-3Ds-dualscreen @ c330c0a, 3ds_port/scripts/gen_voxel_relief.py
 * (rock tile and colour sets, rel:559-631), MIT License - see source/voxel/NOTICE.md.
 * Portions Copyright (c) Dust Zallax, MIT. */
/* rg_rrock.h -- the General tileset's rock: metatile id sets and rock colours the relief generator reads (3DGBA,
 * GPLv3). Numeric in upstream (no names involved); ported verbatim. Pure C, header only. */
#ifndef RG_RROCK_H
#define RG_RROCK_H

#include <stdbool.h>
#include <stdint.h>

/* BOULDER (rel:571): a boulder drawn two cells tall sits on its terrace, all of it top. */
static inline bool rg_is_boulder(unsigned m) { return m == 0x93u || m == 0x94u || m == 0x9Bu || m == 0x9Cu; }
/* SIDE_WEST / SIDE_EAST (rel:576-577). */
static inline bool rg_is_side_west(unsigned m) { return m == 0x070u || m == 0x073u; }
static inline bool rg_is_side_east(unsigned m) { return m == 0x072u || m == 0x075u || m == 0x0A2u; }
/* SEA_CAPS (rel:583). */
static inline bool rg_is_sea_cap(unsigned m) { return m == 0x172u || m == 0x174u; }
/* FACE_SOUTH (rel:594-595). */
static inline bool rg_is_face_south(unsigned m)
{
    switch (m) {
    case 0x07C: case 0x0A9: case 0x09F: case 0x0A7: case 0x091: case 0x079: case 0x1B0: case 0x0AF: case 0x0CF:
    case 0x1F0: case 0x1F1: case 0x33B: case 0x33C:
        return true;
    default:
        return false;
    }
}
/* ROCK_TILES (rel:629-631): the ten ids, FACE_SOUTH, SIDE_WEST, SIDE_EAST. */
static inline bool rg_is_rock_tile(unsigned m)
{
    switch (m) {
    case 0x070: case 0x072: case 0x073: case 0x074: case 0x075: case 0x07B: case 0x07C: case 0x07D: case 0x089:
    case 0x0A9: case 0x0A2:
        return true;
    default:
        return rg_is_face_south(m);
    }
}
/* DIRT (rel:565): patches of bare soil drawn in the faces' colours, flat. */
static inline bool rg_is_dirt(unsigned m)
{
    return m == 0x113u || m == 0x114u || m == 0x115u || m == 0x14Bu || m == 0x14Cu || m == 0x14Du;
}
/* The `roles` union alias_of reads (rel:433): ROCK_TILES | SIDE_WEST | SIDE_EAST | SEA_CAPS | BOULDER. */
static inline bool rg_is_alias_role(unsigned m)
{
    return rg_is_rock_tile(m) || rg_is_sea_cap(m) || rg_is_boulder(m);
}

/* Rock colours, 8-bit RGB as PIL yields them (rel:559-563). ROCK_FLECK is in both the tops' speckle and the
 * faces' shading; ROCK_ALL is the union. */
static const uint8_t RG_ROCK_TOP[2][3] = {{0xde, 0xb4, 0xa4}, {0xbd, 0x94, 0x8b}};
static const uint8_t RG_ROCK_RIM[1][3] = {{0xee, 0xd5, 0xcd}};
static const uint8_t RG_ROCK_FACE[3][3] = {{0x83, 0x5a, 0x5a}, {0x62, 0x41, 0x52}, {0x41, 0x31, 0x41}};
static const uint8_t RG_ROCK_FLECK[1][3] = {{0x9c, 0x73, 0x73}};

#endif
