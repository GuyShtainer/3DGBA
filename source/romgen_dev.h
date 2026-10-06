/* romgen_dev.h -- DEV-ONLY device test hook for the romgen driver (3DGBA, GPLv3). Platform glue, outside source/romgen/.
 *
 * Compiled in only with -DROMGEN_DEV_HOOK=1 (`make ROMGEN_DEV_HOOK=1`); the default is 0 and then main.c does not
 * even reference this file's API. With it on, the hook is still inert until the runtime opt-in: the control
 * directory sdmc:/cias/control exists AND a file sdmc:/cias/control/romgen_go.txt is dropped; the file is consumed
 * on pickup and rg_run then runs once on a worker thread against the loaded Emerald ROM's bytes (read-only).
 * Output: regions.bin, signposts.bin, buildings.bin and romgen_timings.txt in sdmc:/3ds/3DGBA/voxel/.
 * S4 owns the real UI, progress and cache; this exists to time and prove the generator on a real New 3DS. */
#ifndef ROMGEN_DEV_H
#define ROMGEN_DEV_H

#include <stddef.h>
#include <stdint.h>

#ifndef ROMGEN_DEV_HOOK
#define ROMGEN_DEV_HOOK 0
#endif

#if ROMGEN_DEV_HOOK
/* Main thread, once per frame (cheap: it stats the go file only every ~2 s). `rom` is the loaded BPEE ROM image
 * (mGBA's buffer, never written, valid until the cores are torn down) or NULL when no Emerald is loaded. */
void romgen_dev_poll(const uint8_t *rom, size_t romSize);
/* Call before the cores are destroyed: cancels a run in flight and joins the worker. */
void romgen_dev_stop(void);
#endif

#endif
