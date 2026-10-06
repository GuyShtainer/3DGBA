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

clean:
	rm -rf build
.PHONY: clean mem
