/* Modified for 3DGBA (GPLv3), 2026.
 * Original: ZallaxDev/pokeemerald-3Ds-dualscreen @ c330c0a, 3ds_port/scripts/gen_voxel_relief.py
 * (_hash64, _insert_clean, set_order, commonest: rel:61-150), MIT License - see source/voxel/NOTICE.md.
 * Portions Copyright (c) Dust Zallax, MIT. */
/* rg_pyset.h -- CPython's hash and set iteration order, 64-bit (3DGBA, GPLv3). Pure C.
 * Spec: docs/phase33-romgen/SPEC-S3.md section 1.2, known answers A.8. */
#ifndef RG_PYSET_H
#define RG_PYSET_H

#include <stddef.h>
#include <stdint.h>

/* hash(int) as 64-bit CPython: |v| mod (2^61-1), sign restored, -1 becomes -2. */
int64_t rg_hash64_int(int64_t v);
/* hash(tuple of ints): the xxHash-style tuple mix, unsigned 64-bit math. Each item is hashed with rg_hash64_int. */
int64_t rg_hash64_tuple(const int64_t *items, unsigned n);

/* Bytes of scratch rg_set_order needs for n keys (two hash tables, worst case: every key unique). */
size_t rg_set_scratch_bytes(unsigned n);
/* list(set(seq)) in 64-bit CPython's table order. keys[i] is an opaque 64-bit id compared by value, hashes[i] its
 * hash. `out` receives the unique keys in table order (room for n); returns how many. scratch must be 8-aligned
 * and rg_set_scratch_bytes(n) long. */
unsigned rg_set_order(const int64_t *keys, const int64_t *hashes, unsigned n, int64_t *out, void *scratch);

/* Scratch for rg_commonest*: rg_commonest_scratch_bytes(n) (8-aligned). */
size_t rg_commonest_scratch_bytes(unsigned n);
/* max(set(seq), key=seq.count): the first key of maximal count in set order (rel:148-150). n >= 1. */
int64_t rg_commonest(const int64_t *keys, const int64_t *hashes, unsigned n, void *scratch);
/* The three key kinds S3 uses. Levels: ints. Colours: BGR555, hashed as the tuple (r8, g8, b8) with
 * c5*255//31 channels (what PIL's getdata() yields upstream). */
int rg_commonest_int(const int *v, unsigned n, void *scratch);
uint16_t rg_commonest_rgb(const uint16_t *c555, unsigned n, void *scratch);

#endif
