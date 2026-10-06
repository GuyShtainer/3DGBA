# Host build of the romgen CLI (clang or cc). Output: build/romgen (git-ignored).
#   make -C tools/romgen        then   tools/romgen/build/romgen roms/emerald.gba OUTDIR --time
CC      ?= clang
ROOT    := ../..
SRC     := $(ROOT)/source/romgen
CORES   := $(wildcard $(SRC)/rg_*.c) $(ROOT)/source/voxel/vx_lz77.c
CFLAGS  ?= -std=c11 -Wall -Wextra -O2 -ffp-contract=off
CFLAGS  += -I$(SRC) -I$(ROOT)/source/voxel

build/romgen: romgen_cli.c $(CORES) $(wildcard $(SRC)/*.h)
	@mkdir -p build
	$(CC) $(CFLAGS) romgen_cli.c $(CORES) -lm -o $@

# Counting-allocator build: `make -C tools/romgen mem` -> build/romgen_mem; --time then also prints the peak heap.
mem: build/romgen_mem
build/romgen_mem: romgen_cli.c rg_memcount.c rg_memcount.h $(CORES) $(wildcard $(SRC)/*.h)
	@mkdir -p build
	$(CC) $(CFLAGS) -DRG_MEMCOUNT -I. -include rg_memcount.h romgen_cli.c rg_memcount.c $(CORES) -lm -o $@

# Host suites: `make -C tools/romgen test` builds every test/host/test_romgen_*.c with ASan+UBSan against all of
# source/romgen and the voxel consumer, and runs it (the real-ROM parts when ROMGEN_ROM is exported, else SKIP).
# One suite: `make -C tools/romgen test T=relief_ledge`. Uses build/tests/ (git-ignored with build/).
T ?= *
VOXSRC := voxel_world voxel_regions voxel_relief voxel_building voxel_sign voxel_arena voxel_atlas voxel_mesh_builder \
          voxel_tree voxel_lighting voxel_grade voxel_entities voxel_camera vx_adapter vx_snapshot vx_lz77 vx_behavior \
          vx_data ctr_shims_pure vx_battle_stub
TESTFLAGS := -std=c11 -Wall -Wextra -O2 -ffp-contract=off -fsanitize=address,undefined -DVOXEL_HOST_FILES \
             -DCTR_VOXEL_LIGHTING=1 -I$(SRC) -I$(ROOT)/source/voxel -I$(ROOT)/test/host
test:
	@mkdir -p build/tests
	@set -e; for f in $(ROOT)/test/host/test_romgen_$(T).c; do \
	  n=$$(basename $$f .c); \
	  $(CC) $(TESTFLAGS) $$f $(wildcard $(SRC)/rg_*.c) $(foreach v,$(VOXSRC),$(ROOT)/source/voxel/$(v).c) -lm -o build/tests/$$n; \
	  echo "== $$n"; build/tests/$$n | tail -3; \
	done

clean:
	rm -rf build
.PHONY: clean mem test
