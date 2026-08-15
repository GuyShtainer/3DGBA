// wfscan.c — find every MB_WATERFALL (0x13) column in the user's own Emerald ROM, through the
// SHIPPED readers (fieldtrav_rom_map + fieldtrav_rom_bus + fieldpath_behaviour_at). Read-only.
//
//   clang -std=c11 -O2 -I source wfscan.c source/fieldtrav.c source/fieldpath.c -o /tmp/wfscan
//   /tmp/wfscan            (run from the 3DGBA project root)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fieldpath.h"
#include "fieldtrav.h"

#define EM_MAPGROUPS 0x08486578u

static uint8_t* g_rom; static long g_romLen;
static uint8_t r8(void* c, uint32_t a) {
	(void)c; if ((a >> 24) != 0x08u && (a >> 24) != 0x09u) return 0;
	uint32_t o = a & 0x01FFFFFFu; return (g_rom && (long)o < g_romLen) ? g_rom[o] : 0;
}
static uint16_t r16(void* c, uint32_t a) { return (uint16_t)(r8(c,a) | (r8(c,a+1) << 8)); }
static uint32_t r32(void* c, uint32_t a) { return (uint32_t)r16(c,a) | ((uint32_t)r16(c,a+2) << 16); }

int main(int argc, char** argv) {
	const char* path = argc > 1 ? argv[1] : "roms/emerald.gba";
	uint32_t mg = EM_MAPGROUPS;
	if (argc > 2) mg = (uint32_t)strtoul(argv[2], 0, 16);
	FILE* f = fopen(path, "rb"); if (!f) { printf("no rom %s\n", path); return 1; }
	fseek(f, 0, SEEK_END); g_romLen = ftell(f); fseek(f, 0, SEEK_SET);
	g_rom = malloc((size_t)g_romLen);
	if (fread(g_rom, 1, (size_t)g_romLen, f) != (size_t)g_romLen) return 1;
	fclose(f);

	FpBus bus = { r8, r16, r32, 0 };
	static const int kGroupN[34] = {57,5,5,6,7,8,9,7,7,14,8,17,10,23,13,15,15,2,2,2,3,1,1,1,108,61,89,2,1,13,1,1,3,1};
	int totalCols = 0;
	for (int grp = 0; grp < 34; grp++) {
		for (int num = 0; num < kGroupN[grp]; num++) {
			FtRomMap rm;
			if (!fieldtrav_rom_map(&bus, mg, grp, num, &rm)) continue;
			// behaviour reads need an FpMap whose grid is the ROM grid; use the shipped adapter
			FtRomBus rb; FpBus dbus; FpMap dmap;
			fieldtrav_rom_bus(&rb, &bus, &rm, FP_ENG_RSE, &dbus, &dmap);
			for (int x = 0; x < rm.w; x++) {
				for (int y = 0; y < rm.h; y++) {
					int b = fieldpath_behaviour_at(&dbus, &dmap, x, y);
					if (b != 0x13) continue;
					// only report the BOTTOM of each column
					int below = (y + 1 < rm.h) ? fieldpath_behaviour_at(&dbus, &dmap, x, y + 1) : -1;
					if (below == 0x13) continue;
					int k = 0, yy = y;
					while (yy >= 0 && fieldpath_behaviour_at(&dbus, &dmap, x, yy) == 0x13) { k++; yy--; }
					int top = (yy >= 0) ? fieldpath_behaviour_at(&dbus, &dmap, x, yy) : -1;
					uint16_t wF = dbus.read16(dbus.ctx, dmap.gridPtr + 2u * (uint32_t)((x + 7) + dmap.backupW * (y + 7)));
					uint16_t wB = (below >= 0) ? dbus.read16(dbus.ctx, dmap.gridPtr + 2u * (uint32_t)((x + 7) + dmap.backupW * (y + 8))) : 0;
					uint16_t wT = (yy >= 0) ? dbus.read16(dbus.ctx, dmap.gridPtr + 2u * (uint32_t)((x + 7) + dmap.backupW * (yy + 7))) : 0;
					printf("map(%2d,%3d) %3dx%-3d  fallBase(%3d,%3d) run=%d  "
					       "below beh=%02X coll=%d elev=%2d | fall coll=%d elev=%2d | top(%3d,%3d) beh=%02X coll=%d elev=%2d\n",
					       grp, num, rm.w, rm.h, x, y, k,
					       below, (wB >> 10) & 3, (wB >> 12) & 15,
					       (wF >> 10) & 3, (wF >> 12) & 15,
					       x, yy, top, (wT >> 10) & 3, (wT >> 12) & 15);
					totalCols++;
				}
			}
		}
	}
	printf("total waterfall columns: %d\n", totalCols);
	return 0;
}
