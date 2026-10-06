# Host build of the romgen CLI (clang or cc). Output: build/romgen (git-ignored).
#   make -C tools/romgen        then   tools/romgen/build/romgen roms/emerald.gba OUTDIR --time
CC      ?= clang
ROOT    := ../..
SRC     := $(ROOT)/source/romgen
# vx_behavior.c: rg_gameprof.c builds the Emerald renderer behaviour sets from its predicates (Phase 34 R0).
CORES   := $(wildcard $(SRC)/rg_*.c) $(ROOT)/source/voxel/vx_lz77.c $(ROOT)/source/voxel/vx_behavior.c
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

# Renderer suites (Phase 34 R0): `make -C tools/romgen vtest` builds and runs every test/host/test_voxel_*.c with the
# command line from its own header (plus rg_gameprof.c, which gba_game.h's VXP() reads), and prints each check count.
VX := $(ROOT)/source/voxel
VFLAGS := -std=c11 -Wall -Wextra -O2 -fsanitize=address,undefined
VWORLD := $(foreach v,voxel_world voxel_regions voxel_relief voxel_building voxel_sign voxel_arena voxel_atlas \
          voxel_mesh_builder voxel_tree voxel_lighting voxel_grade voxel_entities voxel_camera vx_adapter vx_snapshot \
          vx_lz77 vx_behavior vx_data ctr_shims_pure vx_battle_stub,$(VX)/$(v).c) $(SRC)/rg_gameprof.c
VADAPT := $(foreach v,vx_adapter vx_snapshot vx_lz77 vx_behavior ctr_shims_pure,$(VX)/$(v).c) $(SRC)/rg_gameprof.c
vtest:
	@mkdir -p build/vtests
	@set -e; for t in entities mesh world; do \
	  $(CC) $(VFLAGS) -DVOXEL_HOST_FILES -DCTR_VOXEL_LIGHTING=1 -I$(VX) -I$(SRC) -I$(ROOT)/test/host \
	    $(ROOT)/test/host/test_voxel_$$t.c $(VWORLD) -lm -o build/vtests/$$t; \
	  echo "== test_voxel_$$t"; (cd $(ROOT) && tools/romgen/build/vtests/$$t) | tail -3; done
	@set -e; $(CC) $(VFLAGS) -I$(VX) -I$(SRC) -I$(ROOT)/test/host $(ROOT)/test/host/test_voxel_adapter.c $(VADAPT) \
	    -o build/vtests/adapter; echo "== test_voxel_adapter"; (cd $(ROOT) && tools/romgen/build/vtests/adapter) | tail -3
	@set -e; $(CC) -std=c11 -Wall -Wextra -O2 -I$(ROOT)/source $(ROOT)/test/host/test_voxel_gate.c -o build/vtests/gate; \
	  echo "== test_voxel_gate"; (cd $(ROOT) && tools/romgen/build/vtests/gate) | tail -3
	@set -e; $(CC) -std=c11 -Wall -Wextra -O2 -I$(VX) $(ROOT)/test/host/test_voxel_lz77.c $(VX)/vx_lz77.c -o build/vtests/lz77; \
	  echo "== test_voxel_lz77"; (cd $(ROOT) && tools/romgen/build/vtests/lz77) | tail -3
	@set -e; $(CC) -std=c11 -Wall -Wextra -O2 -I$(VX) $(ROOT)/test/host/test_voxel_overlay.c $(VX)/vx_overlay.c -o build/vtests/overlay; \
	  echo "== test_voxel_overlay"; (cd $(ROOT) && tools/romgen/build/vtests/overlay) | tail -3
	@set -e; $(CC) -std=c11 -Wall -Wextra -O2 -I$(VX) $(ROOT)/test/host/test_voxel_shims.c $(VX)/ctr_shims_pure.c $(VX)/vx_data.c \
	    -o build/vtests/shims; echo "== test_voxel_shims"; (cd $(ROOT) && tools/romgen/build/vtests/shims) | tail -3

clean:
	rm -rf build
.PHONY: clean mem test vtest
