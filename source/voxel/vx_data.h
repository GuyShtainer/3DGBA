/* vx_data.h -- voxel data access: user-built pak first, loose files second (3DGBA, GPLv3).
 * SPEC-port section 4.5. Pure C; host-buildable (funopen on macOS, fopencookie on newlib/glibc). */
#ifndef VX_DATA_H
#define VX_DATA_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef enum
{
    VXD_NONE = 0,      /* neither pak nor loose data found */
    VXD_OK_PAK,
    VXD_OK_LOOSE,
    VXD_BAD_PAK,       /* header/index/CRC/format failure: "data damaged" */
    VXD_WRONG_ROM      /* pak built for a different ROM */
} VxDataStatus;

/* Where to look. Defaults: sdmc:/3ds/emerald3ds/emerald3ds.pak and sdmc:/3ds/3DGBA/voxel. */
void vx_data_set_paths(const char *pakPath, const char *looseDir);
/* SHA-1 of the loaded ROM (20 bytes), or NULL to skip the check. Resets the cached pak. */
void vx_data_set_rom_sha1(const uint8_t sha1[20]);
VxDataStatus vx_data_status(void);

/* Opens "voxel/<name>.bin" style paths. Returns a read-only FILE*, or NULL. */
FILE *CtrData_Open(const char *path);

/* Helpers exposed for tests and the ROM loader. */
uint32_t vx_crc32(const void *data, size_t len);
uint64_t vx_fnv1a64(const char *s);
void vx_sha1(const void *data, size_t len, uint8_t out[20]);

#endif
