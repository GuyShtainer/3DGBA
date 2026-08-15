// wfscan.c — find every MB_WATERFALL (0x13) column in a Gen-3 ROM, through the SHIPPED readers
// (fieldtrav_rom_map + fieldtrav_rom_bus + fieldpath_behaviour_at). Read-only.
//
//   clang -std=c11 -O2 -I source wfscan.c source/fieldtrav.c source/fieldpath.c -o /tmp/wfscan
//
//   /tmp/wfscan [rom] [gMapGroupsHex] [eng] [nGroups] [counts...]     (from the project root)
//     eng: 0 = RSE (default), 1 = FRLG — FRLG reads metatile ATTRIBUTES differently, so this is
//          not cosmetic; with the wrong engine the scan reports 0 columns.
//     nGroups + counts: the title's own map-group table. Emerald's 34 groups are the built-in
//          default; anything else MUST be supplied or fieldtrav_rom_map walks past a group's end.
//
//   PHASE 28 / lane X (phase-26 audit O7): this tool used to hardcode Emerald's 34-group table AND
//   FP_ENG_RSE, so run exactly as its README documented against roms/firered.gba it answered
//   **0 columns** — silently, for two of the five shipped titles. The FireRed number the spec banks
//   (17) reproduces only with FireRed's 43-group table and eng=1:
//
//     /tmp/wfscan roms/firered.gba 08352718 1 43 \
//        5 123 60 66 4 6 8 10 6 8 20 10 8 2 10 4 2 2 2 1 1 2 2 3 2 3 2 1 1 1 1 7 5 5 8 8 5 5 1 1 1 2 1
//     /tmp/wfscan roms/ruby.gba 083085A0 0 34 \
//        54 5 5 6 7 7 8 7 7 13 8 17 10 24 13 13 14 2 2 2 3 1 1 1 86 44 12 2 1 13 1 1 3 1
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

static const int kEmeraldGroupN[34] =
	{57,5,5,6,7,8,9,7,7,14,8,17,10,23,13,15,15,2,2,2,3,1,1,1,108,61,89,2,1,13,1,1,3,1};

int main(int argc, char** argv) {
	const char* path = argc > 1 ? argv[1] : "roms/emerald.gba";
	uint32_t mg = EM_MAPGROUPS;
	if (argc > 2) mg = (uint32_t)strtoul(argv[2], 0, 16);
	FpEngine eng = (argc > 3 && atoi(argv[3]) == 1) ? FP_ENG_FRLG : FP_ENG_RSE;
	int nGroups = (argc > 4) ? atoi(argv[4]) : 34;
	static int groupN[64];
	if (nGroups < 1 || nGroups > 64) { printf("nGroups out of range\n"); return 2; }
	if (argc > 5) {
		if (argc - 5 < nGroups) { printf("need %d group counts, got %d\n", nGroups, argc - 5); return 2; }
		for (int i = 0; i < nGroups; i++) groupN[i] = atoi(argv[5 + i]);
	} else {
		if (nGroups != 34) { printf("no group table given and nGroups != 34 (Emerald)\n"); return 2; }
		for (int i = 0; i < 34; i++) groupN[i] = kEmeraldGroupN[i];
	}
	FILE* f = fopen(path, "rb"); if (!f) { printf("no rom %s\n", path); return 1; }
	fseek(f, 0, SEEK_END); g_romLen = ftell(f); fseek(f, 0, SEEK_SET);
	g_rom = malloc((size_t)g_romLen);
	if (fread(g_rom, 1, (size_t)g_romLen, f) != (size_t)g_romLen) return 1;
	fclose(f);

	FpBus bus = { r8, r16, r32, 0 };
	int totalCols = 0;
	for (int grp = 0; grp < nGroups; grp++) {
		for (int num = 0; num < groupN[grp]; num++) {
			FtRomMap rm;
			if (!fieldtrav_rom_map(&bus, mg, grp, num, &rm)) continue;
			// behaviour reads need an FpMap whose grid is the ROM grid; use the shipped adapter
			FtRomBus rb; FpBus dbus; FpMap dmap;
			fieldtrav_rom_bus(&rb, &bus, &rm, eng, &dbus, &dmap);
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
