/* rg_rtables.c -- numbers resolved through pokeemerald@731ad5b (3DGBA, GPLv3). Pure C.
 *
 * NUMBERS ONLY. Every table is derived from the decomp's data files at that commit by a fixed rule and is recorded
 * in docs/PROVENANCE.md (table, symbol, decomp paths consulted, derivation, ROM assertion). No decomp text,
 * comments or code appear here. Spec: docs/phase33-romgen/SPEC-S3.md Appendix A. */
#include "rg_rtables.h"

#include <assert.h>
#include <string.h>

#include "gba_game.h"

#define RG_MAX_LAYOUTS 1024u

/* A.3: upstream-outdoor layouts in layout-name order (LC_ALL=C code-point sort). pokeemerald@731ad5b,
 * data/layouts/layouts.json + data/maps/<folder>/map.json. ROM assertion: T2 (the set; the order is decomp-only). */
const uint16_t RG_OUTDOOR_BY_NAME[RG_OUTDOOR_COUNT] = {
    192, 196, 345, 265, 12, 9, 14, 5, 292, 13, 6, 10,
    3, 7, 136, 302, 303, 11, 16, 1, 135, 17, 18, 19,
    20, 287, 21, 22, 23, 24, 25, 26, 27, 392, 28, 29,
    30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41,
    42, 43, 44, 45, 263, 46, 47, 319, 48, 49, 50, 4,
    239, 394, 238, 241, 395, 240, 321, 331, 438, 2, 8, 357,
    290, 291, 406, 410, 274, 411, 51, 52, 53, 412, 282, 146,
    283, 130, 15,
};

/* A.4: alt -> base, pokeemerald@731ad5b data/layouts/layouts.json (the five outdoor "_alt" layouts). ROM: T4. */
const RgOutdoorAlt RG_OUTDOOR_ALTS[RG_ALT_COUNT] = {
    {46, 263}, {319, 47}, {357, 8}, {392, 27}, {438, 331},
};

/* A.1: pokeemerald@731ad5b data/layouts/layouts.json (LAYOUT_ROUTE104 = 20, LAYOUT_RUSTBORO_CITY = 4). ROM: T1. */
const uint16_t RG_ENABLED[2] = {20, 4};

bool rg_is_enabled_layout(uint16_t layoutId)
{
    return layoutId == RG_ENABLED[0] || layoutId == RG_ENABLED[1];
}

uint16_t rg_name_rank(uint16_t layoutId)
{
    unsigned i;

    for (i = 0; i < RG_OUTDOOR_COUNT; i++)
        if (RG_OUTDOOR_BY_NAME[i] == layoutId)
            return (uint16_t)i;
    return 0xFFFFu;
}

bool rg_relief_outdoor(uint16_t layoutId)
{
    return rg_name_rank(layoutId) != 0xFFFFu;
}

/* T1 (the ids this slice reads): 20 = 40x80, 4 = 40x60, 17 (Route 101, the test place) = 20x20. */
static bool check_t1(const RgWorld *w, const char **why)
{
    static const struct { uint16_t id, w, h; } kDims[3] = {{20, 40, 80}, {4, 40, 60}, {17, 20, 20}};
    unsigned i;

    for (i = 0; i < 3; i++) {
        const RgLayout *L = &w->layouts[kDims[i].id - 1u];

        if (L->w != kDims[i].w || L->h != kDims[i].h) {
            *why = "T1: a named layout has other dimensions than the table says";
            return false;
        }
    }
    return true;
}

static bool same_pair(const RgLayout *a, const RgLayout *b)
{
    return a->ts[0]->addr == b->ts[0]->addr && a->ts[1]->addr == b->ts[1]->addr;
}

/* T2: the layouts of outdoor-type maps (ROUTE/TOWN/CITY/UNDERWATER/OCEAN_ROUTE) + the A.4 alts == the A.3 set. */
static bool check_t2(const RgWorld *w, const char **why)
{
    uint8_t outdoor[RG_MAX_LAYOUTS + 1u];
    unsigned i, k;

    memset(outdoor, 0, sizeof(outdoor));
    for (i = 0; i < w->mapCount; i++) {
        const RgMap *m = &w->maps[i];

        if ((m->mapType == MAP_TYPE_ROUTE || m->mapType == MAP_TYPE_TOWN || m->mapType == MAP_TYPE_UNDERWATER
             || m->mapType == MAP_TYPE_CITY || m->mapType == MAP_TYPE_OCEAN_ROUTE) && m->layoutId <= RG_MAX_LAYOUTS)
            outdoor[m->layoutId] = 1;
    }
    for (k = 0; k < RG_ALT_COUNT; k++)
        outdoor[RG_OUTDOOR_ALTS[k].alt] = 1;
    for (i = 1; i <= w->layoutCount && i <= RG_MAX_LAYOUTS; i++) {
        bool tabled = rg_name_rank((uint16_t)i) != 0xFFFFu;

        if ((outdoor[i] != 0) != tabled) {
            *why = "T2: the outdoor layout set differs from the table";
            return false;
        }
    }
    return true;
}

static bool check_t4(const RgWorld *w, const char **why)
{
    unsigned k;

    for (k = 0; k < RG_ALT_COUNT; k++) {
        const RgLayout *a = &w->layouts[RG_OUTDOOR_ALTS[k].alt - 1u];
        const RgLayout *b = &w->layouts[RG_OUTDOOR_ALTS[k].base - 1u];

        if (a->w != b->w || a->h != b->h || !same_pair(a, b)) {
            *why = "T4: an alternate differs from its base in size or tilesets";
            return false;
        }
    }
    return true;
}

bool rg_rtables_check(const RgWorld *w, const char **why)
{
    const char *dummy;

    assert(w != NULL);
    if (why == NULL)
        why = &dummy;
    if (w->layoutCount < 442u) {
        *why = "fewer layouts than the tables describe";
        return false;
    }
    return check_t1(w, why) && check_t2(w, why) && check_t4(w, why);
}
