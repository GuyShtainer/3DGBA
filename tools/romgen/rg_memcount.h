/* rg_memcount.h -- host-only counting allocator for the romgen CLI (3DGBA, GPLv3).
 * Force-included (-include) ONLY by the `make mem` build, so the normal CLI and the device build pay nothing.
 * Every block gets a 16-byte size header; the peak is the high-water mark of live bytes since the last reset. */
#ifndef RG_MEMCOUNT_H
#define RG_MEMCOUNT_H
#include <stddef.h>
#include <stdlib.h>

void  *rgm_malloc(size_t n);
void  *rgm_calloc(size_t c, size_t n);
void  *rgm_realloc(void *p, size_t n);
void   rgm_free(void *p);
void   rgm_reset(void);               /* baseline = live bytes now (peak/live are reported relative to it), count = 0 */
size_t rgm_peak(void);
size_t rgm_live(void);
size_t rgm_count(void);

#define malloc(n)     rgm_malloc(n)
#define calloc(c, n)  rgm_calloc((c), (n))
#define realloc(p, n) rgm_realloc((p), (n))
#define free(p)       rgm_free(p)
#endif
