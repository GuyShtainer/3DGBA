/* rg_rtables.c -- numbers resolved through pokeemerald@731ad5b (3DGBA, GPLv3). Pure C.
 *
 * NUMBERS ONLY. Every table is derived from the decomp's data files at that commit by a fixed rule and is recorded
 * in docs/PROVENANCE.md (table, symbol, decomp paths consulted, derivation, ROM assertion). No decomp text,
 * comments or code appear here. Spec: docs/phase33-romgen/SPEC-S3.md Appendix A. */
#include "rg_rtables.h"

#include <assert.h>
#include <string.h>

#include "gba_game.h"
#include "rg_behavior.h"

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

/* A.1: pokeemerald@731ad5b data/layouts/layouts.json: LAYOUT_LAVARIDGE_TOWN 13, LAYOUT_MT_CHIMNEY 136,
 * LAYOUT_JAGGED_PASS 292 (ROM: T1 dims, T6 secondary tileset). */
const uint16_t RG_ALIAS_LAYOUTS[3] = {13, 136, 292};
/* A.1: the layouts whose names give the WRAP groups: LAYOUT_ROUTE104 20, LAYOUT_ROUTE105 21, LAYOUT_ROUTE106 22. */
const uint16_t RG_WRAP_GROUP_SEEDS[3] = {20, 21, 22};

/* A.5: pokeemerald@731ad5b data/maps/map_groups.json + data/maps/<folder>/map.json: the maps of type ROUTE/TOWN/CITY/
 * UNDERWATER/OCEAN_ROUTE, ordered by folder name (LC_ALL=C code-point sort), as {group, num, layout id}.
 * ROM: T2 (set), T3 (each map's layout). */
const RgOutdoorMap RG_OUTDOOR_MAPS_BY_FOLDER[RG_OUTDOOR_MAP_COUNT] = {
    {24, 60, 192}, {24, 64, 196}, {26, 14, 345}, {26, 4, 265},
    {0, 11, 12}, {0, 8, 9}, {0, 13, 14}, {0, 4, 5},
    {24, 13, 292}, {0, 12, 13}, {0, 5, 6}, {0, 9, 10},
    {0, 2, 3}, {0, 6, 7}, {24, 12, 136}, {24, 21, 302},
    {24, 22, 303}, {0, 10, 11}, {0, 15, 16}, {0, 0, 1},
    {24, 11, 135}, {0, 16, 17}, {0, 17, 18}, {0, 18, 19},
    {0, 19, 20}, {27, 0, 287}, {0, 20, 21}, {0, 21, 22},
    {0, 22, 23}, {0, 23, 24}, {0, 24, 25}, {0, 25, 26},
    {0, 26, 27}, {0, 27, 28}, {0, 28, 29}, {0, 29, 30},
    {0, 30, 31}, {0, 31, 32}, {0, 32, 33}, {0, 33, 34},
    {0, 34, 35}, {0, 35, 36}, {0, 36, 37}, {0, 37, 38},
    {0, 38, 39}, {0, 39, 40}, {0, 40, 41}, {0, 41, 42},
    {0, 42, 43}, {0, 43, 44}, {0, 44, 45}, {0, 45, 263},
    {0, 46, 47}, {0, 47, 48}, {0, 48, 49}, {0, 49, 50},
    {0, 3, 4}, {26, 1, 239}, {26, 12, 394}, {26, 0, 238},
    {26, 3, 241}, {26, 13, 395}, {26, 2, 240}, {24, 78, 321},
    {24, 85, 331}, {0, 1, 2}, {0, 7, 8}, {26, 9, 290},
    {26, 10, 291}, {24, 101, 406}, {0, 55, 410}, {0, 50, 274},
    {0, 56, 411}, {0, 51, 51}, {0, 52, 52}, {0, 53, 53},
    {0, 54, 412}, {24, 69, 282}, {24, 26, 146}, {24, 70, 283},
    {24, 5, 130}, {0, 14, 15},
};

uint16_t rg_relief_alt_base(uint16_t layoutId)
{
    unsigned k;

    for (k = 0; k < RG_ALT_COUNT; k++)
        if (RG_OUTDOOR_ALTS[k].alt == layoutId)
            return RG_OUTDOOR_ALTS[k].base;
    return 0;
}

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

/* T1: the named layouts' dimensions (A.1) plus Route 101 (17), the test place. */
static bool check_t1(const RgWorld *w, const char **why)
{
    static const struct { uint16_t id, w, h; } kDims[] = {
        {20, 40, 80}, {4, 40, 60}, {10, 20, 20}, {13, 20, 20}, {292, 30, 46}, {136, 40, 47},
        {32, 100, 20}, {38, 40, 40}, {21, 40, 80}, {22, 80, 20}, {17, 20, 20}};
    unsigned i;

    for (i = 0; i < sizeof(kDims) / sizeof(kDims[0]); i++) {
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

/* T3: each A.5 map's layout id in the ROM's header; every A.5 layout is an A.3 layout. */
static bool check_t3(const RgWorld *w, const char **why)
{
    unsigned i;

    for (i = 0; i < RG_OUTDOOR_MAP_COUNT; i++) {
        const RgMap *m = rg_world_map(w, RG_OUTDOOR_MAPS_BY_FOLDER[i].group, RG_OUTDOOR_MAPS_BY_FOLDER[i].num);

        if (m == NULL || m->layoutId != RG_OUTDOOR_MAPS_BY_FOLDER[i].layout) {
            *why = "T3: an outdoor map has another layout than the table says";
            return false;
        }
        if (rg_name_rank(RG_OUTDOOR_MAPS_BY_FOLDER[i].layout) == 0xFFFFu) {
            *why = "T3: an A.5 layout is missing from A.3";
            return false;
        }
    }
    return true;
}

/* T5: every A.3 layout has the General primary tileset (the primary of layout 10). */
static bool check_t5(const RgWorld *w, const char **why)
{
    unsigned i;

    for (i = 0; i < RG_OUTDOOR_COUNT; i++)
        if (w->layouts[RG_OUTDOOR_BY_NAME[i] - 1u].ts[0]->addr != w->layouts[RG_WORLD_ROOT - 1u].ts[0]->addr) {
            *why = "T5: an outdoor layout does not use the General primary tileset";
            return false;
        }
    return true;
}

/* T6: the layouts on layout 13's secondary tileset (the Lavaridge rock) are exactly the A.2 list. */
static bool check_t6(const RgWorld *w, const char **why)
{
    static const uint16_t kWant[13] = {13, 28, 136, 292, 293, 336, 337, 338, 339, 340, 341, 379, 380};
    uint32_t sec = w->layouts[RG_ALIAS_LAYOUTS[0] - 1u].ts[1]->addr;
    unsigned id, n = 0, k;

    for (id = 1; id <= w->layoutCount; id++) {
        bool in = false, want = false;

        if (w->layouts[id - 1u].present && w->layouts[id - 1u].ts[1]->addr == sec)
            in = true;
        for (k = 0; k < 13; k++)
            want = want || kWant[k] == id;
        if (in != want) {
            *why = "T6: the layouts on the Lavaridge secondary tileset differ from the table";
            return false;
        }
        n += in ? 1u : 0u;
    }
    return n == 13u;
}

static bool is_outdoor_type(unsigned t)
{
    return t == MAP_TYPE_ROUTE || t == MAP_TYPE_TOWN || t == MAP_TYPE_UNDERWATER || t == MAP_TYPE_CITY
           || t == MAP_TYPE_OCEAN_ROUTE;
}

/* T7: the connection census over outdoor maps (all entries, dive and emerge included). Index = ROM dir 1..6. */
static bool check_t7(const RgWorld *w, const char **why)
{
    static const unsigned kWant[7] = {0, 27, 27, 40, 40, 7, 7};   /* down, up, left, right, dive, emerge */
    unsigned census[7] = {0, 0, 0, 0, 0, 0, 0}, i, d;

    for (i = 0; i < w->mapCount; i++) {
        RgConn buf[64];
        unsigned n;

        if (!is_outdoor_type(w->maps[i].mapType))
            continue;
        n = rg_map_connections_all(w, w->maps[i].group, w->maps[i].num, buf, 64);
        if (n > 64u)
            n = 64u;
        for (d = 0; d < n; d++)
            if (buf[d].dir >= 1u && buf[d].dir <= 6u)
                census[buf[d].dir]++;
    }
    for (d = 1; d <= 6; d++)
        if (census[d] != kWant[d]) {
            *why = "T7: the outdoor connection census differs from the table";
            return false;
        }
    return true;
}

/* T8: Route 104's map (0/19): [up -> (0/3) offset 0, down -> (0/20) offset 0, right -> (0/0) offset 50] in ROM order. */
static bool check_t8(const RgWorld *w, const char **why)
{
    static const RgConn kWant[3] = {{2, 0, 0, 3}, {1, 0, 0, 20}, {4, 50, 0, 0}};
    RgConn buf[8];
    unsigned n = rg_map_connections(w, 0, 19, buf, 8), i;

    bool same = n == 3u;

    for (i = 0; same && i < 3; i++)
        same = buf[i].dir == kWant[i].dir && buf[i].offset == kWant[i].offset && buf[i].group == kWant[i].group
               && buf[i].num == kWant[i].num;
    if (!same) {
        *why = "T8: Route 104's connections differ from the table (order matters)";
        return false;
    }
    return true;
}

/* T9: Route 101 (17) has jump cells; every FLAT behaviour is below the enum count 0xF0. */
static bool check_t9(const RgWorld *w, const char **why)
{
    const RgLayout *L = &w->layouts[16];
    unsigned b;
    int x, y;
    bool jump = false;

    for (y = 0; y < (int)L->h && !jump; y++)
        for (x = 0; x < (int)L->w; x++)
            if (rg_is_jump(rg_behaviour(L, x, y))) {
                jump = true;
                break;
            }
    if (!jump) {
        *why = "T9: Route 101 has no jump-behaviour cell";
        return false;
    }
    for (b = 0xF0; b < 0x100; b++)
        if (rg_is_flat_behaviour(b)) {
            *why = "T9: a FLAT behaviour is at or past the enum count";
            return false;
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
    return check_t1(w, why) && check_t2(w, why) && check_t3(w, why) && check_t4(w, why) && check_t5(w, why)
           && check_t6(w, why) && check_t7(w, why) && check_t8(w, why) && check_t9(w, why);
}
