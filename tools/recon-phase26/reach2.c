// reach2.c — surf/foot flood over a ROM map, through the shipped readers.
//   /tmp/reach2 <rom> <mgHex> <grp> <num> <sx> <sy> [eng]
// Prints an ASCII map of what is reachable in SURF mode (water only, waterfall EXCLUDED) from
// (sx,sy), plus the distance to any tile you ask about on stdin-free extra args.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fieldpath.h"
#include "fieldtrav.h"

static uint8_t* g_rom; static long g_romLen;
static uint8_t r8(void* c, uint32_t a) {
	(void)c; if ((a >> 24) != 0x08u && (a >> 24) != 0x09u) return 0;
	uint32_t o = a & 0x01FFFFFFu; return (g_rom && (long)o < g_romLen) ? g_rom[o] : 0;
}
static uint16_t r16(void* c, uint32_t a) { return (uint16_t)(r8(c,a) | (r8(c,a+1) << 8)); }
static uint32_t r32(void* c, uint32_t a) { return (uint32_t)r16(c,a) | ((uint32_t)r16(c,a+2) << 16); }

static int W, H;
static short dist[300][300];

int main(int argc, char** argv) {
	if (argc < 7) { printf("usage: %s rom mgHex grp num sx sy [eng]\n", argv[0]); return 2; }
	FILE* f = fopen(argv[1], "rb"); if (!f) return 1;
	fseek(f, 0, SEEK_END); g_romLen = ftell(f); fseek(f, 0, SEEK_SET);
	g_rom = malloc((size_t)g_romLen);
	if (fread(g_rom, 1, (size_t)g_romLen, f) != (size_t)g_romLen) return 1;
	fclose(f);
	uint32_t mg = (uint32_t)strtoul(argv[2], 0, 16);
	int grp = atoi(argv[3]), num = atoi(argv[4]), sx = atoi(argv[5]), sy = atoi(argv[6]);
	FpEngine eng = (argc > 7 && atoi(argv[7]) == 1) ? FP_ENG_FRLG : FP_ENG_RSE;
	FpBus bus = { r8, r16, r32, 0 };
	FtRomMap rm;
	if (!fieldtrav_rom_map(&bus, mg, grp, num, &rm)) { printf("bad map\n"); return 1; }
	FtRomBus rb; FpBus dbus; FpMap dmap;
	fieldtrav_rom_bus(&rb, &bus, &rm, eng, &dbus, &dmap);
	W = rm.w; H = rm.h;
	for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) dist[y][x] = -1;
	static short qx[90000], qy[90000]; int head = 0, tail = 0;
	dist[sy][sx] = 0; qx[tail] = (short)sx; qy[tail++] = (short)sy;
	const int dx[4] = {1,-1,0,0}, dy[4] = {0,0,1,-1};
	while (head < tail) {
		int x = qx[head], y = qy[head]; head++;
		for (int d = 0; d < 4; d++) {
			int nx = x + dx[d], ny = y + dy[d];
			if (nx < 0 || nx >= W || ny < 0 || ny >= H || dist[ny][nx] >= 0) continue;
			int b = fieldpath_behaviour_at(&dbus, &dmap, nx, ny);
			// SURF layer: this engine's surfable set (waterfall EXCLUDED by fieldtrav) + collision 0
			if (!fieldtrav_is_surfable(eng, b)) continue;
			if (!fieldpath_enterable(&dbus, &dmap, nx, ny, 0)) continue;
			dist[ny][nx] = (short)(dist[y][x] + 1);
			qx[tail] = (short)nx; qy[tail++] = (short)ny;
		}
	}
	for (int i = 8; i + 1 < argc; i += 2) {
		int tx = atoi(argv[i]), ty = atoi(argv[i+1]);
		printf("dist to (%d,%d) = %d\n", tx, ty, (tx>=0&&tx<W&&ty>=0&&ty<H)?dist[ty][tx]:-1);
	}
	int lo = 0, hi = H - 1;
	for (int y = lo; y <= hi; y++) {
		printf("%3d ", y);
		for (int x = 0; x < W && x < 100; x++) {
			int b = fieldpath_behaviour_at(&dbus, &dmap, x, y);
			char c;
			if (dist[y][x] >= 0)  c = (x==sx&&y==sy) ? 'S' : '~';
			else if (b == 0x13)   c = 'W';
			else if (fieldtrav_is_surfable(eng, b)) c = 'o';   // water NOT reachable from S
			else if (fieldpath_enterable(&dbus, &dmap, x, y, 3)) c = '.';
			else c = '#';
			putchar(c);
		}
		printf("\n");
	}
	return 0;
}
