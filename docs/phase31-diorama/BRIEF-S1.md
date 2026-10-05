# Brief — slice S1: the pure-C world module (`source/diorama.{c,h}` + `test/host/test_diorama.c`)

Binding inputs, in precedence order: `PHASE.md` → `RULES-v1.md` → `SPEC-world.md` (API, data
model, memory caps, test plan) → `SPEC-data.md` (every address/struct/encoding; §2, §3, §5.3
behaviour values, §7.2 placement, §8 cadence) → `RESEARCH-classification.md` (ideas only).
Never open `projects/_reference/`. Model: opus (judgment + correctness work).

## Deliverables
1. `source/diorama.h` / `source/diorama.c` — `<stdint.h>`/`<string.h>`/`<stddef.h>` only; no
   libctru/citro/mGBA/gbacore includes; no floats in the classification path (floats allowed in
   camera/mesh math); read-only `DioBus` callbacks; fixed storage sized from BUILDLOG's measured
   facts (largest map+neighbours 22,400 cells; layouts ≤ 80×80 / 40×140; ≤ 5 connections); NASA
   Power-of-10 style per `~/.claude/c-golden-rules.md` (bounded loops, explicit BFS queue, asserts).
2. `test/host/test_diorama.c` — compile line in its header, CHECK-macro style of `test_tilt.c`,
   synthetic fixtures + the REAL fixtures via `test/host/dio_fixture.h` (loads DIOF / DIOG / DIOA;
   loud SKIP when a fixture file is absent). Must reproduce, for every `test/fixtures/dio_*.bin`
   present: the golden class grid BYTE-FOR-BYTE and the structure table record-for-record
   (`dio_*.golden.bin`, ordering per RULES-v1 §4). Target: thousands of checks.
3. A `BUILDLOG.md` entry (dated, numbers measured): check count, bytes of static storage, the
   largest fixture's classify+group+mesh time on the host (order of magnitude only).

## Non-negotiables
- Classification rule order = RULES-v1 §3 exactly (VEG period 1..4, HMAX 6, indoor behaviour-only,
  blank-metatile VOID, alcove). Any deviation you believe is necessary: implement RULES-v1 as
  written, and list the deviation you wanted in BUILDLOG "open questions" — do not silently change
  a rule the goldens were generated from.
- Neighbours: instance the first ring (N/S/W/E, skip DIVE/EMERGE) with the origin formulas of
  SPEC-data §3.4 (== RULES-v1 §2); classify across instance borders; texture flag from the
  tileset-pair equality.
- The mesh emitter outputs `DioVert {float x,y,z; float u,v; float r,g,b,a;}` (36 B) + u16 indices
  into caller-provided fixed arrays with a capacity-exceeded flag; UVs come from a caller-supplied
  slot function or the 32-per-row 512×512 atlas rule (SPEC-render decides — read it if present;
  otherwise implement the 32-per-row rule with a half-texel inset and note it).
- Camera + billboard math per SPEC-world §8/§9 (pitch 40°, yaw 0, fov 35°, distance 8+0.2·max(w,h)
  clamped +5, follow lerp 0.15, snap on warp, rebase on connection walk); pure-C 4×4 matrices with
  the layout SPEC-world specifies for `C3D_Mtx`.
- Self-revert: if any existing suite goes red or `make` breaks, revert your change before reporting.
  Run at least `test_tilt` and `test_peersprite` (compile lines in their headers) before and after.
- Bank early: create both source files and the test file as skeletons in your first minutes and
  keep them compiling; save after each function group. A killed session must lose nothing.

## Fixture facts you can rely on (measured 2026-09-10)
Littleroot (20×20, 1 north conn, tilesets match): FLAT 268 / LOW 3 / WALL 71 / ROOF 17 / VEG 41,
23 structures, max height 4. Rustboro (40×60, 3 conns): WALL 308 / ROOF 114 / VEG 585 / WATER 142 /
LOW 89, 319 structures, max height 6. Battle Arena lobby (16×13 indoor): WALL 116 / ROOF 18 /
FURNITURE 1 / FLAT 73, 2 structures. Brendan's house 1F (11×9 indoor): FLAT 61 / LOW 2 / WALL 22 /
ROOF 13 / FURNITURE 1. Regenerate with `tools/diorama/dioref.py` if in doubt — the Python model is
the spec's executable twin; where it and RULES-v1 disagree, say so in BUILDLOG (RULES-v1 wins).
