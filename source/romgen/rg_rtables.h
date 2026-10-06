/* rg_rtables.h -- the pret-resolved number tables of relief generation (3DGBA, GPLv3). Pure C.
 * Numbers only; provenance in docs/PROVENANCE.md. Spec: docs/phase33-romgen/SPEC-S3.md section 1.3, Appendix A.
 * S3.1 carries only what the ledge slice needs (A.3, A.4); S3.2 adds the rest. */
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

/* Index in RG_OUTDOOR_BY_NAME, 0xFFFF when absent. */
uint16_t rg_name_rank(uint16_t layoutId);
/* Used by an outdoor map, or an A.4 alternate of one (A.3 membership; layout 442 is excluded: deviation D1). */
bool rg_relief_outdoor(uint16_t layoutId);

/* The assertions of SPEC-S3 3.3 that exist so far (T1 for the ids S3.1 uses; T2: the layouts of outdoor-type maps plus the A.4 alts == A.3;
 * T4: each alt has its base's size and tileset pair). False + a reason on the first mismatch. */
bool rg_rtables_check(const RgWorld *w, const char **why);

#endif
