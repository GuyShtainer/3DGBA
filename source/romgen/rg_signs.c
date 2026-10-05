/* Modified for 3DGBA (GPLv3), 2026.
 * Original: ZallaxDev/pokeemerald-3Ds-dualscreen @ c330c0a, 3ds_port/scripts/gen_voxel_sign_masks.py and
 * voxel_sign_mask.py, MIT License - see source/voxel/NOTICE.md.
 * Portions Copyright (c) Dust Zallax, MIT. */
#include "rg_signs.h"

#include <stdlib.h>
#include <string.h>

#include "voxel_regions.h"

#define HEAD_MAX_WIDTH 14
#define HEAD_MAX_PIXELS 160

static bool bit(const uint16_t rows[16], int x, int y)
{
    return x >= 0 && y >= 0 && x < 16 && y < 16 && ((rows[y] >> x) & 1u);
}

static unsigned popcount16(unsigned v)
{
    unsigned n = 0;
    while (v) {
        n += v & 1u;
        v >>= 1;
    }
    return n;
}

static bool any16(const uint16_t rows[16])
{
    unsigned k;
    for (k = 0; k < 16; k++)
        if (rows[k])
            return true;
    return false;
}

/* ---- cutout_mask (smask:28-44) ----------------------------------------------------------- */
void rg_cutout_mask(const uint16_t solid[16], uint16_t out[16])
{
    static const int8_t nb[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    uint16_t outside[16];
    uint8_t qx[256], qy[256];
    unsigned head = 0, tail = 0, y, x, k;

    memset(outside, 0, sizeof(outside));
    for (y = 0; y < 16; y++)
        for (x = 0; x < 16; x++)
            if ((x == 0 || x == 15 || y == 0 || y == 15) && !bit(solid, (int)x, (int)y)) {
                outside[y] |= (uint16_t)(1u << x);
                qx[tail] = (uint8_t)x;
                qy[tail++] = (uint8_t)y;
            }
    while (head < tail) {
        int cx = qx[head], cy = qy[head++];
        for (k = 0; k < 4; k++) {
            int nx = cx + nb[k][0], ny = cy + nb[k][1];
            if (nx < 0 || ny < 0 || nx > 15 || ny > 15 || bit(solid, nx, ny) || bit(outside, nx, ny))
                continue;
            outside[ny] |= (uint16_t)(1u << nx);
            qx[tail] = (uint8_t)nx;
            qy[tail++] = (uint8_t)ny;
        }
    }
    for (y = 0; y < 16; y++)
        out[y] = (uint16_t)~outside[y];
}

/* ---- metatile_mask (smask:14-25, 47-52) -------------------------------------------------- */
static void ground_colours(RgPair *p, const RgLayout *L, int x, int y, uint8_t *seen /* 4096 bytes: 32768 bits */)
{
    static const int8_t nb[5][2] = {{-1, 0}, {1, 0}, {0, 1}, {-1, 1}, {1, 1}};
    unsigned k, py, px;

    memset(seen, 0, 4096);
    for (k = 0; k < 5; k++) {
        int nx = x + nb[k][0], ny = y + nb[k][1];
        uint16_t mn;
        const RgLayer *l0;

        if (rg_off(L, nx, ny) || rg_blocked(L, nx, ny))
            continue;
        mn = rg_metatile(L, nx, ny);
        if (rg_layer_any(rg_layer(p, mn, 1)))
            continue;   /* something is drawn over that ground: not a clean sample */
        l0 = rg_layer(p, mn, 0);
        for (py = 0; py < 16; py++)
            for (px = 0; px < 16; px++)
                if (rg_layer_has(l0, (int)px, (int)py)) {
                    unsigned c = l0->c[py][px] & 0x7FFFu;
                    seen[c >> 3] |= (uint8_t)(1u << (c & 7u));
                }
    }
}

void rg_metatile_mask(RgPair *p, const RgLayout *L, int x, int y, uint16_t rows[16])
{
    uint8_t seen[4096];
    uint16_t m = rg_metatile(L, x, y), solid[16];
    const RgLayer *l0 = rg_layer(p, m, 0), *l1 = rg_layer(p, m, 1);
    unsigned py, px;

    ground_colours(p, L, x, y, seen);
    memset(solid, 0, sizeof(solid));
    for (py = 0; py < 16; py++) {
        for (px = 0; px < 16; px++) {
            if (rg_layer_has(l0, (int)px, (int)py)) {
                unsigned c = l0->c[py][px] & 0x7FFFu;
                if (!((seen[c >> 3] >> (c & 7u)) & 1u))
                    solid[py] |= (uint16_t)(1u << px);
            }
        }
        solid[py] |= l1->drawn[py];
    }
    rg_cutout_mask(solid, rows);
}

/* ---- head_mask (smask:55-87) ------------------------------------------------------------- */
void rg_head_mask(RgPair *p, const uint16_t signRows[16], uint16_t north, uint16_t out[16])
{
    const RgLayer *upper;
    uint16_t head[16];
    uint8_t qx[256], qy[256];
    unsigned qh = 0, qt = 0, count = 0, x, y;
    int minx = 16, maxx = -1, dx, dy;

    memset(out, 0, 16 * sizeof(uint16_t));
    memset(head, 0, sizeof(head));
    if (popcount16(signRows[0]) > HEAD_MAX_WIDTH)
        return;   /* a block that fills its cell has no lantern */
    upper = rg_layer(p, north, 1);
    for (x = 0; x < 16; x++) {
        if (rg_layer_has(upper, (int)x, 15)
            && (bit(signRows, (int)x - 1, 0) || bit(signRows, (int)x, 0) || bit(signRows, (int)x + 1, 0))) {
            head[15] |= (uint16_t)(1u << x);
            qx[qt] = (uint8_t)x;
            qy[qt++] = 15;
        }
    }
    while (qh < qt) {
        int cx = qx[qh], cy = qy[qh++];
        for (dy = -1; dy <= 1; dy++)
            for (dx = -1; dx <= 1; dx++) {
                int nx = cx + dx, ny = cy + dy;
                if (nx < 0 || ny < 0 || nx > 15 || ny > 15 || !rg_layer_has(upper, nx, ny) || bit(head, nx, ny))
                    continue;
                head[ny] |= (uint16_t)(1u << nx);
                qx[qt] = (uint8_t)nx;
                qy[qt++] = (uint8_t)ny;
            }
    }
    for (y = 0; y < 16; y++) {
        for (x = 0; x < 16; x++)
            if (bit(head, (int)x, (int)y)) {
                count++;
                if ((int)x < minx) minx = (int)x;
                if ((int)x > maxx) maxx = (int)x;
            }
    }
    if (count == 0 || maxx - minx + 1 > HEAD_MAX_WIDTH || count > HEAD_MAX_PIXELS)
        return;
    rg_cutout_mask(head, out);
}

/* ---- head_ground (signs:22-44) ----------------------------------------------------------- */
uint16_t rg_head_ground(RgPair *p, const RgLayout *L, int hx, int hy)
{
    const RgLayer *own = rg_layer(p, rg_metatile(L, hx, hy), 0);
    uint16_t seenM[8];
    unsigned seenN[8], n = 0, i, best = 0;
    int dx, dy;

    for (dy = -1; dy <= 1; dy++) {
        for (dx = -1; dx <= 1; dx++) {
            int nx = hx + dx, ny = hy + dy;
            uint16_t mn;

            if ((dx == 0 && dy == 0) || (dx == 0 && dy == 1) || rg_off(L, nx, ny) || rg_blocked(L, nx, ny))
                continue;
            mn = rg_metatile(L, nx, ny);
            if (rg_layer_any(rg_layer(p, mn, 1)))
                continue;
            if (rg_layer_equal(rg_layer(p, mn, 0), own))
                return mn;
            for (i = 0; i < n; i++)
                if (seenM[i] == mn)
                    break;
            if (i == n) {
                seenM[n] = mn;
                seenN[n++] = 0;
            }
            seenN[i]++;
        }
    }
    if (n > 0) {
        for (i = 1; i < n; i++)
            if (seenN[i] > seenN[best])
                best = i;   /* strictly greater: a tie keeps the first inserted */
        return seenM[best];
    }
    return !rg_off(L, hx, hy + 2) ? rg_metatile(L, hx, hy + 2) : 0;
}

/* ---- records ----------------------------------------------------------------------------- */
static bool push(RgSignList *list, const RgSignRec *r)
{
    if (list->count == list->cap) {
        size_t nc = list->cap ? list->cap * 2 : 128;
        RgSignRec *nr = (RgSignRec *)realloc(list->rec, nc * sizeof(RgSignRec));
        if (nr == NULL)
            return false;
        list->rec = nr;
        list->cap = nc;
    }
    list->rec[list->count++] = *r;
    return true;
}

bool rg_signs_layout(RgPair *p, const RgLayout *L, const uint8_t *roles, RgSignList *list)
{
    int x, y;

    if (p == NULL || L == NULL || roles == NULL || list == NULL)
        return false;
    if (!L->outdoor)
        return true;
    for (y = 0; y < (int)L->h; y++) {
        for (x = 0; x < (int)L->w; x++) {
            RgSignRec r;
            if (roles[(size_t)y * L->w + (size_t)x] != VOXEL_ROLE_SIGNPOST)
                continue;
            memset(&r, 0, sizeof(r));
            r.layout = L->id;
            r.x = (uint16_t)x;
            r.y = (uint16_t)y;
            rg_metatile_mask(p, L, x, y, r.rows);
            if (y > 0 && !rg_blocked(L, x, y - 1)) {
                rg_head_mask(p, r.rows, rg_metatile(L, x, y - 1), r.head);
                if (any16(r.head))
                    r.headGround = rg_head_ground(p, L, x, y - 1);
            }
            if (!push(list, &r))
                return false;
        }
    }
    return true;
}

static int cmp_rec(const void *a, const void *b)
{
    const RgSignRec *x = (const RgSignRec *)a, *y = (const RgSignRec *)b;
    if (x->layout != y->layout) return x->layout < y->layout ? -1 : 1;
    if (x->y != y->y) return x->y < y->y ? -1 : 1;
    if (x->x != y->x) return x->x < y->x ? -1 : 1;
    return 0;
}

void rg_signs_finish(RgSignList *list)
{
    if (list->count > 1)
        qsort(list->rec, list->count, sizeof(RgSignRec), cmp_rec);
}

void rg_signs_free(RgSignList *list)
{
    free(list->rec);
    memset(list, 0, sizeof(*list));
}

unsigned rg_signs_with_head(const RgSignList *list)
{
    size_t i;
    unsigned k, n = 0;

    for (i = 0; i < list->count; i++) {
        unsigned any = 0;
        for (k = 0; k < 16; k++)
            any |= list->rec[i].head[k];
        n += any != 0;
    }
    return n;
}

static void put16(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

size_t rg_signs_write(const RgSignList *list, uint8_t *out, size_t cap)
{
    size_t size, i, o = 8;
    unsigned k;

    if (list == NULL || list->count == 0 || list->count > 65535u)
        return 0;
    size = 8u + 72u * list->count;
    if (out == NULL)
        return size;
    if (cap < size)
        return 0;
    memcpy(out, "VXS2", 4);
    put16(out + 4, (uint32_t)(list->count & 0xFFFFu));
    put16(out + 6, (uint32_t)(list->count >> 16));
    for (i = 0; i < list->count; i++) {
        const RgSignRec *r = &list->rec[i];
        put16(out + o, r->layout);
        put16(out + o + 2, r->x);
        put16(out + o + 4, r->y);
        for (k = 0; k < 16; k++) {
            put16(out + o + 6 + 2 * k, r->rows[k]);
            put16(out + o + 38 + 2 * k, r->head[k]);
        }
        put16(out + o + 70, r->headGround);
        o += 72;
    }
    return size;
}
