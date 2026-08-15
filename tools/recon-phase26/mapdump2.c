// mapdump2.c — dump behaviour/collision/elevation for a rectangle of one ROM map, through the
// shipped readers.  /tmp/mapdump2 <rom> <mapgroupsHex> <grp> <num> <x0> <y0> <x1> <y1>
#include <stdio.h>
#include <stdlib.h>
#include "fieldpath.h"
#include "fieldtrav.h"

static uint8_t* g_rom; static long g_romLen;
static uint8_t r8(void* c, uint32_t a) {
	(void)c; if ((a >> 24) != 0x08u && (a >> 24) != 0x09u) return 0;
	uint32_t o = a & 0x01FFFFFFu; return (g_rom && (long)o < g_romLen) ? g_rom[o] : 0;
}
static uint16_t r16(void* c, uint32_t a) { return (uint16_t)(r8(c,a) | (r8(c,a+1) << 8)); }
static uint32_t r32(void* c, uint32_t a) { return (uint32_t)r16(c,a) | ((uint32_t)r16(c,a+2) << 16); }

int main(int argc, char** argv) {
	if (argc < 9) { printf("usage: %s rom mgHex grp num x0 y0 x1 y1 [eng]\n", argv[0]); return 2; }
	FILE* f = fopen(argv[1], "rb"); if (!f) { printf("no rom\n"); return 1; }
	fseek(f, 0, SEEK_END); g_romLen = ftell(f); fseek(f, 0, SEEK_SET);
	g_rom = malloc((size_t)g_romLen);
	if (fread(g_rom, 1, (size_t)g_romLen, f) != (size_t)g_romLen) return 1;
	fclose(f);
	uint32_t mg = (uint32_t)strtoul(argv[2], 0, 16);
	int grp = atoi(argv[3]), num = atoi(argv[4]);
	int x0 = atoi(argv[5]), y0 = atoi(argv[6]), x1 = atoi(argv[7]), y1 = atoi(argv[8]);
	FpEngine eng = (argc > 9 && atoi(argv[9]) == 1) ? FP_ENG_FRLG : FP_ENG_RSE;
	FpBus bus = { r8, r16, r32, 0 };
	FtRomMap rm;
	if (!fieldtrav_rom_map(&bus, mg, grp, num, &rm)) { printf("bad map\n"); return 1; }
	printf("map(%d,%d) %dx%d layout=%08X grid=%08X\n", grp, num, rm.w, rm.h, rm.layout, rm.grid);
	FtRomBus rb; FpBus dbus; FpMap dmap;
	fieldtrav_rom_bus(&rb, &bus, &rm, eng, &dbus, &dmap);
	printf("      ");
	for (int x = x0; x <= x1; x++) printf("%-9d", x);
	printf("\n");
	for (int y = y0; y <= y1; y++) {
		printf("y=%-4d", y);
		for (int x = x0; x <= x1; x++) {
			int b = fieldpath_behaviour_at(&dbus, &dmap, x, y);
			uint16_t w = dbus.read16(dbus.ctx, dmap.gridPtr + 2u * (uint32_t)((x + 7) + dmap.backupW * (y + 7)));
			printf("%02X/%d/%-3d ", b & 0xFF, (w >> 10) & 3, (w >> 12) & 15);
		}
		printf("\n");
	}
	return 0;
}
