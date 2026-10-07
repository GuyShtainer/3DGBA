/* romgen_dev.h -- DEV-ONLY device test hook for the romgen driver (3DGBA, GPLv3). Platform glue, outside source/romgen/.
 *
 * Compiled in only with -DROMGEN_DEV_HOOK=1 (`make ROMGEN_DEV_HOOK=1`); the default is 0 and then main.c does not
 * even reference this file's API. With it on, rg_run runs ONCE per game by itself ~10 s after an Emerald, FireRed rev 1, LeafGreen rev 1, Ruby rev 2 or Sapphire rev 2 ROM is loaded, on a
 * worker thread against the ROM's bytes (read-only); dropping sdmc:/cias/control/romgen_go.txt re-runs it (consumed
 * on pickup). It creates every folder it writes to. Output: regions.bin, signposts.bin, buildings.bin and
 * romgen_timings.txt in the game's voxel dir (Emerald sdmc:/3ds/3DGBA/voxel/, FireRed .../voxel/BPRE/, LeafGreen .../voxel/BPGE/, Ruby .../voxel/AXVE/, Sapphire .../voxel/AXPE/), plus the same report as sdmc:/cias/netlogs/3DGBA_romgen_<time>.txt.
 * S4 owns the real UI, progress and cache; this exists to time and prove the generator on a real New 3DS. */
#ifndef ROMGEN_DEV_H
#define ROMGEN_DEV_H

#include <stddef.h>
#include <stdint.h>

#ifndef ROMGEN_DEV_HOOK
#define ROMGEN_DEV_HOOK 0
#endif

#if ROMGEN_DEV_HOOK
/* Main thread, once per frame (cheap: it stats the go file only every ~2 s). `rom` is a loaded supported ROM image (Emerald,
 * FireRed or LeafGreen rev 1, Ruby or Sapphire rev 2; mGBA's buffer, never written, valid until the cores are torn down) or NULL. */
void romgen_dev_poll(const uint8_t *rom, size_t romSize);
/* Call before the cores are destroyed: cancels a run in flight and joins the worker. */
void romgen_dev_stop(void);
#endif

#endif
