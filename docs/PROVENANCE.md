# Provenance of decomp-derived number tables

Phase 33 ROMGEN, `source/romgen/rg_rtables.c`. Rules (SPEC-S3 section 3.0): the code carries numbers only (a short
identifier may stand beside a number in a comment); every table names the decomp files consulted (path only), pinned at
**pokeemerald@731ad5b**, the derivation, and the ROM assertion that re-checks it on the user's dump
(`rg_rtables_check`, run by `test_romgen_relief_ledge.c` on the real ROM). The decomp is reference-only, never vendored.

| table | symbol (file) | decomp paths consulted @731ad5b | derivation | ROM assertion |
|---|---|---|---|---|
| upstream-outdoor layouts, name order (A.3) | `RG_OUTDOOR_BY_NAME` (rg_rtables.c) | `data/layouts/layouts.json`, `data/maps/<folder>/map.json` | layouts used by a map of type ROUTE/TOWN/CITY/UNDERWATER/OCEAN_ROUTE plus the A.4 alternates; ordered by layout name, `LC_ALL=C` code-point sort; stored as layout ids | T2 (set equality with the ROM's map headers; the order itself is decomp-only) |
| outdoor alternates (A.4) | `RG_OUTDOOR_ALTS` (rg_rtables.c) | `data/layouts/layouts.json` | the five outdoor layouts whose name ends `_alt`, paired with the layout the suffix-less name resolves to | T4 (same width, height and tileset pair as the base) |
| ENABLED layouts (A.1) | `RG_ENABLED` (rg_rtables.c) | `data/layouts/layouts.json` | the ids of the two layouts upstream names in its ENABLED list, in list order (id = 1-based list position) | T1 (width and height of 20 and 4) |
