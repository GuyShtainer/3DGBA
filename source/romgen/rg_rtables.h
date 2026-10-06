/* rg_rtables.h -- the pret-resolved number tables of relief generation (3DGBA, GPLv3). Pure C.
 * Numbers only; provenance in docs/PROVENANCE.md. Spec: docs/phase33-romgen/SPEC-S3.md section 1.3, Appendix A.
 * A.1 ids, A.3 name order, A.4 alternates, A.5 outdoor maps by folder, A.7 direction key; assertions T1-T9. */
#ifndef RG_RTABLES_H
#define RG_RTABLES_H

#include <stdbool.h>
#include <stdint.h>

#include "rg_world.h"

#define RG_OUTDOOR_COUNT 87u
#define RG_ALT_COUNT 5u

/* A.3: every upstream-outdoor layout, in layout-name order. */
extern const uint16_t RG_OUTDOOR_BY_NAME[RG_OUTDOOR_COUNT];
/* A.4: outdoor alternates (alt -> base). */
typedef struct RgOutdoorAlt { uint16_t alt, base; } RgOutdoorAlt;
extern const RgOutdoorAlt RG_OUTDOOR_ALTS[RG_ALT_COUNT];

/* A.1: the two layouts upstream solves by name (LAYOUT_ROUTE104, LAYOUT_RUSTBORO_CITY), in that order. */
extern const uint16_t RG_ENABLED[2];
bool rg_is_enabled_layout(uint16_t layoutId);
#define RG_WORLD_ROOT 10u                /* LAYOUT_LITTLEROOT_TOWN */
#define RG_ALIAS_REFERENCE 32u           /* LAYOUT_ROUTE116 */
#define RG_EXCLUDED_GROUP_SEED 38u       /* the drawn group whose name-giving seed this is (route122) is dropped */
extern const uint16_t RG_ALIAS_LAYOUTS[3];       /* {13, 136, 292}: Lavaridge, Mt Chimney, Jagged Pass */
extern const uint16_t RG_WRAP_GROUP_SEEDS[3];    /* {20, 21, 22}: the groups named route104/105/106 */

/* A.5: the 82 outdoor maps in map-folder name order, with their layout. */
#define RG_OUTDOOR_MAP_COUNT 82u
typedef struct RgOutdoorMap { uint8_t group, num; uint16_t layout; } RgOutdoorMap;
extern const RgOutdoorMap RG_OUTDOOR_MAPS_BY_FOLDER[RG_OUTDOOR_MAP_COUNT];

/* A.7: connection direction -> sort key (strings sort down < left < right < up). ROM dir: 1 down, 2 up, 3 left,
 * 4 right; anything else (dive, emerge) is not a link: 4. */
static inline unsigned rg_dir_sort_key(unsigned dir)
{
    return dir == 1u ? 0u : dir == 3u ? 1u : dir == 4u ? 2u : dir == 2u ? 3u : 4u;
}
/* The base layout an A.4 alternate stands for, 0 when `layoutId` is not one (S3's own table, not S0's rule). */
uint16_t rg_relief_alt_base(uint16_t layoutId);

/* Index in RG_OUTDOOR_BY_NAME, 0xFFFF when absent. */
uint16_t rg_name_rank(uint16_t layoutId);
/* Used by an outdoor map, or an A.4 alternate of one (A.3 membership; layout 442 is excluded: deviation D1). */
bool rg_relief_outdoor(uint16_t layoutId);
/* Phase 34 L1: the outdoor test per game. Emerald: rg_relief_outdoor(id), unchanged. FireRed / LeafGreen: the layout is
 * referenced by a map header of type 1, 2, 3, 5 or 6 (RgLayout.outdoor, after alternate inheritance). */
bool rg_relief_outdoor_layout(const RgWorld *w, uint16_t layoutId);

/* Every assertion of SPEC-S3 3.3 (T1-T9) against the opened world. False + a reason string on the first mismatch:
 * the generator then refuses (RG_ERR_TABLES), never writing a relief from a ROM the tables do not describe. */
bool rg_rtables_check(const RgWorld *w, const char **why);

#endif
