// dio_fixture.h — host-only loader for the phase-31 diorama fixtures produced by
// tools/diorama/diodump.py (DIOF), tools/diorama/dioref.py (DIOG golden) and diodump --atlas (DIOA).
// Formats: docs/phase31-diorama/RULES-v1.md §6 and the tool headers. Pure C11, no dependencies
// beyond the C library; the fixtures are ROM-derived and git-ignored, so a missing file is a LOUD
// SKIP (dio_fixture_skip), never a silent pass (the house rule from tools/verdict.sh).
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
	uint8_t*        blob;     // whole file, owned; every pointer below points into it
	size_t          size;
	int32_t         w, h;
	uint8_t         mapType;
	uint32_t        layoutAddr, headerAddr;
	const uint16_t* border;   // 4
	const uint16_t* cells;    // w*h
	const uint16_t* attrP;    // 512
	const uint16_t* attrS;    // 512
	const uint16_t* mtlP;     // 512*8
	const uint16_t* mtlS;     // 512*8
	const uint8_t*  tilP;     // 512*32
	const uint8_t*  tilS;     // 512*32
	const uint16_t* palP;     // 16*16
	const uint16_t* palS;     // 16*16
	int             nConn;
	struct { uint8_t dir, group, num, match; int32_t offset, w, h; const uint16_t* cells; } conn[8];
	const char*     name;     // NOT NUL-terminated in the blob: use nameLen
	uint32_t        nameLen;
} DioFixture;

typedef struct {
	uint8_t*        blob;
	int32_t         w, h;
	const uint8_t*  cls;      // w*h, row-major, the RULES-v1 §3 enum
	uint32_t        nStructs;
	const int32_t*  structs;  // nStructs × {minx, miny, maxx, maxy, n, height} (n/height as u32)
} DioGolden;

static inline uint8_t* dio_read_file(const char* path, size_t* outSize) {
	FILE* f = fopen(path, "rb");
	if (!f) return NULL;
	fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
	if (n <= 0) { fclose(f); return NULL; }
	uint8_t* b = (uint8_t*)malloc((size_t)n);
	if (!b || fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); fclose(f); return NULL; }
	fclose(f); *outSize = (size_t)n; return b;
}
static inline uint32_t dio_u32(const uint8_t* p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static inline int32_t  dio_s32(const uint8_t* p) { return (int32_t)dio_u32(p); }

// Returns 1 on success, 0 if the file is missing/corrupt (caller prints the SKIP).
static inline int dio_fixture_load(const char* path, DioFixture* fx) {
	memset(fx, 0, sizeof *fx);
	fx->blob = dio_read_file(path, &fx->size);
	if (!fx->blob || fx->size < 8 || memcmp(fx->blob, "DIOF", 4) != 0 || dio_u32(fx->blob + 4) != 1) return 0;
	size_t i = 8;
	while (i + 8 <= fx->size) {
		const uint8_t* tag = fx->blob + i; uint32_t n = dio_u32(fx->blob + i + 4); const uint8_t* p = fx->blob + i + 8;
		if (i + 8 + n > fx->size) return 0;
		if      (!memcmp(tag, "HEAD", 4) && n >= 20) { fx->w = dio_s32(p); fx->h = dio_s32(p + 4); fx->mapType = p[8]; fx->layoutAddr = dio_u32(p + 12); fx->headerAddr = dio_u32(p + 16); }
		else if (!memcmp(tag, "BORD", 4)) fx->border = (const uint16_t*)p;
		else if (!memcmp(tag, "CELL", 4)) fx->cells  = (const uint16_t*)p;
		else if (!memcmp(tag, "ATTP", 4)) fx->attrP  = (const uint16_t*)p;
		else if (!memcmp(tag, "ATTS", 4)) fx->attrS  = (const uint16_t*)p;
		else if (!memcmp(tag, "MTLP", 4)) fx->mtlP   = (const uint16_t*)p;
		else if (!memcmp(tag, "MTLS", 4)) fx->mtlS   = (const uint16_t*)p;
		else if (!memcmp(tag, "TILP", 4)) fx->tilP   = p;
		else if (!memcmp(tag, "TILS", 4)) fx->tilS   = p;
		else if (!memcmp(tag, "PALP", 4)) fx->palP   = (const uint16_t*)p;
		else if (!memcmp(tag, "PALS", 4)) fx->palS   = (const uint16_t*)p;
		else if (!memcmp(tag, "NAME", 4)) { fx->name = (const char*)p; fx->nameLen = n; }
		else if (!memcmp(tag, "CONN", 4)) {
			fx->nConn = (int)(n / 20); if (fx->nConn > 8) fx->nConn = 8;
			for (int k = 0; k < fx->nConn; k++) {
				const uint8_t* c = p + 20 * k;
				fx->conn[k].dir = c[0]; fx->conn[k].group = c[1]; fx->conn[k].num = c[2]; fx->conn[k].match = c[3];
				fx->conn[k].offset = dio_s32(c + 4); fx->conn[k].w = dio_s32(c + 8); fx->conn[k].h = dio_s32(c + 12);
				fx->conn[k].cells = (const uint16_t*)(uintptr_t)dio_u32(c + 16);   // offset into NCEL, fixed below
			}
		}
		else if (!memcmp(tag, "NCEL", 4)) {
			for (int k = 0; k < fx->nConn; k++) {
				uintptr_t off = (uintptr_t)fx->conn[k].cells;
				fx->conn[k].cells = (fx->conn[k].w && fx->conn[k].h) ? (const uint16_t*)(p + off) : NULL;
			}
		}
		i += 8 + n;
	}
	// NOTE: CONN precedes NCEL in the writer; if a future writer reorders them this fixup breaks — assert.
	return fx->w > 0 && fx->h > 0 && fx->cells && fx->attrP && fx->attrS && fx->mtlP && fx->mtlS && fx->tilP && fx->tilS && fx->palP && fx->palS;
}
static inline void dio_fixture_free(DioFixture* fx) { free(fx->blob); memset(fx, 0, sizeof *fx); }

static inline int dio_golden_load(const char* path, DioGolden* g) {
	memset(g, 0, sizeof *g);
	size_t n; g->blob = dio_read_file(path, &n);
	if (!g->blob || n < 16 || memcmp(g->blob, "DIOG", 4) != 0 || dio_u32(g->blob + 4) != 1) return 0;
	g->w = dio_s32(g->blob + 8); g->h = dio_s32(g->blob + 12);
	size_t cells = (size_t)g->w * (size_t)g->h;
	if (g->w <= 0 || g->h <= 0 || n < 16 + cells + 4) return 0;
	g->cls = g->blob + 16;
	g->nStructs = dio_u32(g->blob + 16 + cells);
	if (n < 16 + cells + 4 + (size_t)g->nStructs * 24) return 0;
	g->structs = (const int32_t*)(g->blob + 16 + cells + 4);
	return 1;
}
static inline void dio_golden_free(DioGolden* g) { free(g->blob); memset(g, 0, sizeof *g); }

// DIOA: returns the 1024×256 u16 raw-BGR555 table (owned; free() it) or NULL.
static inline const uint16_t* dio_atlas_load(const char* path, uint8_t** blobOut) {
	size_t n; uint8_t* b = dio_read_file(path, &n);
	if (!b || n != 8 + 1024u * 256u * 2u || memcmp(b, "DIOA", 4) != 0 || dio_u32(b + 4) != 1) { free(b); return NULL; }
	*blobOut = b; return (const uint16_t*)(b + 8);
}

static inline void dio_fixture_skip(const char* what, const char* path) {
	printf("SKIP  %s: fixture %s not found or unreadable — regenerate with tools/diorama/diodump.py "
	       "(ROM-derived, git-ignored). A SKIP is never a pass.\n", what, path);
}
