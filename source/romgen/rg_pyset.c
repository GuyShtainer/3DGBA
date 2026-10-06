/* Modified for 3DGBA (GPLv3), 2026.
 * Original: ZallaxDev/pokeemerald-3Ds-dualscreen @ c330c0a, 3ds_port/scripts/gen_voxel_relief.py
 * (_hash64, _insert_clean, set_order, commonest: rel:61-150), MIT License - see source/voxel/NOTICE.md.
 * Portions Copyright (c) Dust Zallax, MIT. */
#include "rg_pyset.h"

#include <assert.h>

#define P61 ((((uint64_t)1) << 61) - 1u)
#define MAX_PROBES 9u                 /* LINEAR_PROBES in CPython's set table */

typedef struct Entry { int64_t h, key; uint8_t used; } Entry;

int64_t rg_hash64_int(int64_t v)
{
    uint64_t a = v < 0 ? (uint64_t)0 - (uint64_t)v : (uint64_t)v;
    int64_t h = (int64_t)(a % P61);                /* n = 1: the modular inverse of 1 is 1 */

    if (v < 0)
        h = -h;
    return h == -1 ? -2 : h;
}

int64_t rg_hash64_tuple(const int64_t *items, unsigned n)
{
    uint64_t acc = 2870177450012600261ull;
    unsigned i;

    assert(items != NULL || n == 0);
    for (i = 0; i < n; i++) {
        acc += (uint64_t)rg_hash64_int(items[i]) * 14029467366897019727ull;
        acc = (acc << 31) | (acc >> 33);
        acc *= 11400714785074694791ull;
    }
    acc += (uint64_t)n ^ (2870177450012600261ull ^ 3527539ull);
    if (acc == ~(uint64_t)0)
        return 1546275796;
    return (int64_t)acc;
}

/* Table size after a resize at `used` entries (rel:128-131). */
static size_t grown_size(size_t used)
{
    size_t size = 8, minused = used > 50000 ? used * 2u : used * 4u;

    while (size <= minused)
        size <<= 1;
    return size;
}

/* Largest table a set of n unique keys reaches. */
static size_t max_table(unsigned n)
{
    size_t size = 8, used;

    for (used = 1; used <= n; used++)
        if (used * 5u >= (size - 1u) * 3u)
            size = grown_size(used);
    return size;
}

size_t rg_set_scratch_bytes(unsigned n)
{
    return 2u * max_table(n) * sizeof(Entry);
}

/* rel:84-93: place into a table known to hold no equal key. */
static void insert_clean(Entry *table, size_t mask, int64_t key, int64_t h)
{
    uint64_t perturb = (uint64_t)h;
    size_t i = (size_t)(perturb & mask), j;
    unsigned guard;

    for (guard = 0; guard < 100000u; guard++) {
        if (!table[i].used) {
            table[i].h = h; table[i].key = key; table[i].used = 1;
            return;
        }
        if (i + MAX_PROBES <= mask)
            for (j = i + 1; j < i + 1 + MAX_PROBES; j++)
                if (!table[j].used) {
                    table[j].h = h; table[j].key = key; table[j].used = 1;
                    return;
                }
        perturb >>= 5;
        i = (size_t)((i * 5u + 1u + perturb) & mask);
    }
    assert(0 && "set table full");
}

/* rel:104-127: the slot for key, -1 when it is already present. */
static long find_slot(const Entry *table, size_t mask, int64_t key, int64_t h)
{
    uint64_t perturb = (uint64_t)h;
    size_t i = (size_t)(perturb & mask);
    unsigned guard;

    for (guard = 0; guard < 100000u; guard++) {
        unsigned probes = (i + MAX_PROBES <= mask) ? MAX_PROBES : 0;
        size_t j = i;

        for (;;) {
            if (!table[j].used)
                return (long)j;
            if (table[j].h == h && table[j].key == key)
                return -1;
            if (probes == 0)
                break;
            probes--;
            j++;
        }
        perturb >>= 5;
        i = (size_t)((i * 5u + 1u + perturb) & mask);
    }
    assert(0 && "set table full");
    return -1;
}

unsigned rg_set_order(const int64_t *keys, const int64_t *hashes, unsigned n, int64_t *out, void *scratch)
{
    size_t cap = max_table(n), mask = 7, k;
    Entry *halves[2], *table;
    unsigned used = 0, i, cur = 0, count = 0;

    assert(scratch != NULL && out != NULL && (keys != NULL || n == 0) && (hashes != NULL || n == 0));
    halves[0] = (Entry *)scratch;
    halves[1] = halves[0] + cap;
    table = halves[0];
    for (k = 0; k < 8; k++)
        table[k].used = 0;
    for (i = 0; i < n; i++) {
        long slot = find_slot(table, mask, keys[i], hashes[i]);

        if (slot < 0)
            continue;
        table[slot].h = hashes[i]; table[slot].key = keys[i]; table[slot].used = 1;
        used++;
        if ((size_t)used * 5u >= mask * 3u) {
            size_t size = grown_size(used);
            Entry *nt = halves[cur ^ 1u];

            assert(size <= cap);
            for (k = 0; k < size; k++)
                nt[k].used = 0;
            for (k = 0; k <= mask; k++)
                if (table[k].used)
                    insert_clean(nt, size - 1u, table[k].key, table[k].h);
            table = nt;
            cur ^= 1u;
            mask = size - 1u;
        }
    }
    for (k = 0; k <= mask; k++)
        if (table[k].used)
            out[count++] = table[k].key;
    return count;
}

size_t rg_commonest_scratch_bytes(unsigned n)
{
    return (size_t)n * sizeof(int64_t) * 3u + rg_set_scratch_bytes(n);
}

int64_t rg_commonest(const int64_t *keys, const int64_t *hashes, unsigned n, void *scratch)
{
    int64_t *order = (int64_t *)scratch;
    unsigned u, i, best = 0, bestCount = 0;

    assert(n >= 1 && scratch != NULL);
    u = rg_set_order(keys, hashes, n, order, order + n);
    for (i = 0; i < u; i++) {
        unsigned c = 0, j;

        for (j = 0; j < n; j++)
            if (keys[j] == order[i])
                c++;
        if (c > bestCount) {            /* max() keeps the first of equal counts */
            bestCount = c;
            best = i;
        }
    }
    return order[best];
}

/* Scratch layout for the wrappers: keys[n], hashes[n], then rg_commonest's order[n] + tables. */
int rg_commonest_int(const int *v, unsigned n, void *scratch)
{
    int64_t *keys = (int64_t *)scratch, *hashes = keys + n;
    unsigned i;

    assert(n >= 1);
    for (i = 0; i < n; i++) {
        keys[i] = v[i];
        hashes[i] = rg_hash64_int(v[i]);
    }
    return (int)rg_commonest(keys, hashes, n, hashes + n);
}

uint16_t rg_commonest_rgb(const uint16_t *c555, unsigned n, void *scratch)
{
    int64_t *keys = (int64_t *)scratch, *hashes = keys + n;
    unsigned i;

    assert(n >= 1);
    for (i = 0; i < n; i++) {
        int64_t ch[3];

        ch[0] = (int64_t)((c555[i] & 31u) * 255u / 31u);
        ch[1] = (int64_t)(((c555[i] >> 5) & 31u) * 255u / 31u);
        ch[2] = (int64_t)(((c555[i] >> 10) & 31u) * 255u / 31u);
        keys[i] = c555[i] & 0x7FFFu;
        hashes[i] = rg_hash64_tuple(ch, 3);
    }
    return (uint16_t)rg_commonest(keys, hashes, n, hashes + n);
}
