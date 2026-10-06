/* rg_memcount.c -- see rg_memcount.h. Built WITHOUT the force-include so it reaches the real allocator. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* the force-included header redirects malloc/free: undo that here so this file reaches the real allocator */
#undef malloc
#undef calloc
#undef realloc
#undef free

#define HDR 16u

static size_t sLive, sPeak, sCount, sBase;

static void note_up(size_t n)
{
    sLive += n;
    sCount++;
    if (sLive > sPeak)
        sPeak = sLive;
}

void *rgm_malloc(size_t n)
{
    uint8_t *b = (uint8_t *)malloc(n + HDR);

    if (b == NULL)
        return NULL;
    memcpy(b, &n, sizeof(n));
    note_up(n);
    return b + HDR;
}

void *rgm_calloc(size_t c, size_t n)
{
    void *p;

    if (n != 0 && c > (SIZE_MAX - HDR) / n)
        return NULL;
    p = rgm_malloc(c * n);
    if (p != NULL)
        memset(p, 0, c * n);
    return p;
}

void rgm_free(void *p)
{
    uint8_t *b;
    size_t n;

    if (p == NULL)
        return;
    b = (uint8_t *)p - HDR;
    memcpy(&n, b, sizeof(n));
    sLive -= n;
    free(b);
}

void *rgm_realloc(void *p, size_t n)
{
    uint8_t *b, *nb;
    size_t old;

    if (p == NULL)
        return rgm_malloc(n);
    b = (uint8_t *)p - HDR;
    memcpy(&old, b, sizeof(old));
    nb = (uint8_t *)realloc(b, n + HDR);
    if (nb == NULL)
        return NULL;
    memcpy(nb, &n, sizeof(n));
    sLive -= old;
    sLive += n;
    if (sLive > sPeak)
        sPeak = sLive;
    return nb + HDR;
}

void rgm_reset(void) { sPeak = sLive; sBase = sLive; sCount = 0; }
size_t rgm_peak(void) { return sPeak - sBase; }
size_t rgm_live(void) { return sLive - sBase; }
size_t rgm_count(void) { return sCount; }
