# GBA Build & Memory Layout (devkitARM + libtonc)

How to build GBA homebrew with the devkitARM toolchain and libtonc, and how to live
within the GBA's split RAM. The build is a few standard pieces (an auto-globbing devkitPro
Makefile, an ELF→GBA→gbafix pipeline) wrapped around one hard constraint: **32 KiB of fast
RAM that also holds the stack.** Most of the section/attribute conventions below exist only
because of that constraint.

devkitARM is the shared ARM toolchain for all three Nintendo ARM platforms — GBA (libtonc /
libgba), DS (libnds), and 3DS (libctru) — so the Makefile/toolchain shape here is familiar
across DS/3DS, but the memory map and placement attributes below are GBA-specific.

- [Build flow](#build-flow-source--lib-auto-glob--elfgbagbafix)
- [Compiler & link flags](#compiler--link-flags)
- [Memory map](#memory-map-gba-specific)
- [Placement attributes: EWRAM_CODE / EWRAM_BSS / ALIGNED](#placement-attributes-ewram_code--ewram_bss--aligned-gba-specific)
- [The `*.iwram.c` fast path](#the-iwramc-arm-fast-path-gba-specific)
- [Watching the memory budget](#watching-the-memory-budget)
- [The `u8`/`u16`/`u32`-is-a-macro pitfall](#the-u8u16u32-is-a-macro-pitfall-gba-specific)
- [Pure-C core discipline (dual-compile host tests)](#pure-c-core-discipline-dual-compile-host-tests)

## Build flow: source/ + lib/ auto-glob → ELF→GBA→gbafix

Two ways to build, both producing the same `.gba`:

- **Docker (no host toolchain):** run the official `devkitpro/devkitarm` image (pin a date
  tag, e.g. `:20241104`, for reproducibility). The image already bundles devkitARM +
  libtonc with `DEVKITPRO`/`DEVKITARM` set.
  ```bash
  docker run --rm \
    --user "$(id -u):$(id -g)" \      # artifacts owned by you, not root
    -v "$(pwd)":/project \            # bind-mount the repo; container is disposable
    devkitpro/devkitarm:20241104 \
    bash -c 'cd /project && make rebuild'   # rebuild = clean + build
  ```
- **Local `gba-dev`:** install devkitARM + libtonc and export both env vars, then
  `make rebuild`. On a bare host the vars are NOT in the login shell, so set them inline:
  ```bash
  DEVKITPRO=/opt/devkitpro DEVKITARM=/opt/devkitpro/devkitARM \
  PATH=/opt/devkitpro/devkitARM/bin:/opt/devkitpro/tools/bin:$PATH \
  make -C /abs/path/to/project rebuild
  ```
  Use an **absolute** `-C` path; a tool/agent's working dir can drift between calls.

**Auto-globbing — new `.c` files need NO Makefile edit.** The devkitPro Makefile discovers
every `*.c`/`*.cpp`/`*.s` under its source roots (commonly `source` plus a vendored
`lib/...` tree) with `wildcard` and builds an object for each. Drop a file into a
listed dir and rebuild — done. Adding a *brand-new directory* still requires appending it to
the `SRCDIRS` and `INCDIRS` lists.

**ELF → GBA → gbafix pipeline.** Link produces an `.elf`; an implicit rule runs
`objcopy -O binary` to strip it to a raw `.gba`; then `gbafix` patches the cartridge header
(Nintendo logo checksum, complement check, title):

```make
%.gba : %.elf
	$(OBJCOPY) -O binary $< $@
	gbafix $@ -t$(TITLE)        # TITLE is the ≤12-char cart title
```

The link step typically also emits a symbol map (`arm-none-eabi-nm -Sn`).

**Starting a new tool:** add a `source/main.c`, set the Makefile `TITLE` (≤12 chars; the
gbafix cart title) and the output/project name, drop your `.c` files into any directory
already globbed, build, and watch the memory report (below).

## Compiler & link flags

Representative flags for the ARM7TDMI:

```
-mcpu=arm7tdmi -mtune=arm7tdmi -O2 -Wall -ffast-math -fno-strict-aliasing
```

- **Default codegen is Thumb** (`-mthumb-interwork -mthumb`): plain `.c` compiles to Thumb
  and lands in ROM. To get ARM-in-IWRAM you must name the file `*.iwram.c` (see below).
- `-fno-strict-aliasing` is **load-bearing** for save-format code that type-puns through
  pointers — do not drop it.
- `-ffast-math` is fine (no FPU) but gives up IEEE NaN guarantees.
- C++ adds `-fno-rtti -fno-exceptions`.
- The link step passes `-Wl,--print-memory-usage` so every build prints region usage.
- **Cartridge vs multiboot specs:** the default `-specs=gba.specs` lays code/data for a real
  cartridge (ROM at `0x08000000`, data in EWRAM/IWRAM); `-specs=gba_mb.specs` builds a
  multiboot image that runs entirely from EWRAM with no cart ROM.

## Memory map (GBA-specific)

| Region | Size | Speed | Holds | Rule |
| --- | --- | --- | --- | --- |
| IWRAM | 32 KiB | fast (32-bit bus, 0-wait) | **the stack**, `*.iwram.c` ARM code | Keep tiny. **No big stack buffers.** |
| EWRAM | 256 KiB | slower (16-bit bus, wait states) | all large static buffers (`EWRAM_BSS`), `EWRAM_CODE` functions | Default home for working memory. |
| ROM | up to 32 MiB | slow | default Thumb code + rodata | Where plain `.c` and big `const` tables go. |

Big read-only tables (name tables, lookup maps) belong in ROM as `const` — they cost 0 RAM.
(DS/3DS have far more main RAM and a different map, so this 32 KiB-stack discipline largely
does not apply there; the EWRAM/IWRAM split is GBA-specific.)

## Placement attributes: EWRAM_CODE / EWRAM_BSS / ALIGNED (GBA-specific)

Provided by a small runtime header (commonly a vendored `sys.h`), not by libtonc:

```c
#define EWRAM_CODE __attribute__((section(".ewram"), long_call))
#define EWRAM_BSS  __attribute__((section(".sbss")))
```

- **`EWRAM_BSS`** → a static/global variable lands in EWRAM (256 KiB) instead of IWRAM. Use
  it for **every large buffer**: directory listings, save buffers, log/scratch buffers,
  image row buffers, copy/verify buffers, find-results arrays.
  ```c
  static char EWRAM_BSS s_buf[8192];   // 8 KiB log buffer — never on the stack
  ```
- **`EWRAM_CODE`** → relocates a function body into EWRAM and calls it via long-call. Used
  for code that must keep running when ROM is unmapped (e.g. flashcart SD drivers; a
  reboot-to-loader routine that flips the rompage so ROM vanishes mid-call).
- **`ALIGNED`** → 4-byte alignment, required for DMA buffers. This is typically a **local**
  per-file macro, NOT in the runtime header — copy it when you add a DMA-fed buffer.

**RULE: never declare a multi-KiB array as a local (stack) variable.** Any buffer over a few
hundred bytes on the IWRAM stack silently overflows it and corrupts execution. Large/static
buffers are `static EWRAM_BSS ... ALIGNED`.

**No C recursion for deep walks** (e.g. directory trees): naive recursion blows the 32 KiB
stack. Use an explicit, bounded stack of state in `EWRAM_BSS` instead.

You generally do NOT need `long_call` on *declarations* of EWRAM-resident functions: GNU
`ld` automatically inserts long-branch veneers for out-of-range `bl`s, so an EWRAM function
called from ROM only needs the attribute on its **definition** plus a plain prototype.

## The `*.iwram.c` ARM fast-path (GBA-specific)

Plain `.c` is Thumb in slow ROM. To place hot code in 32 KiB IWRAM, compiled as ARM, **name
the file `*.iwram.c`** (or `.iwram.cpp`). Dedicated Makefile pattern rules build those with
a different arch flag set:

```
-mthumb-interwork -marm -mlong-calls
```

`-marm` forces ARM instructions; `-mlong-calls` lets IWRAM code reach ROM addresses. Linker
placement into the IWRAM section is handled by the gba specs/linker script. Reserve this for
genuinely hot code — IWRAM is scarce and shared with the stack.

## Watching the memory budget

`--print-memory-usage` prints IWRAM / EWRAM / ROM region usage on every link — **watch IWRAM
to catch creep before it overflows the stack.**

**Caveat:** the `.sbss` section (where `EWRAM_BSS` buffers live) is **not** counted in that
EWRAM region report. So `--print-memory-usage` will *not* warn you when your big buffers grow
— **a clean link (no overflow error) is the real check that they fit.** To inspect sizes
explicitly, use:

```bash
arm-none-eabi-size -A your_tool.elf   # per-section sizes, incl. .sbss / .ewram
```

## The `u8`/`u16`/`u32`-is-a-macro pitfall (GBA-specific)

The common vendored runtime header defines the integer aliases as **object-like macros, not
typedefs**:

```c
#define u8  unsigned char
#define u16 unsigned short
#define u32 unsigned int
// (+ volatile vu8/vu16/vu32 variants)
```

Consequences:
- You **cannot** use `u8` as a struct/field/variable name.
- A header with a real `typedef ... u8;` clashes with it.
- Another header that re-`#define`s these silently wins or breaks depending on include order.

Treat these names as reserved keywords: include the header once, and keep them out of any
module you want to stay portable/host-testable (next section).

## Pure-C core discipline (dual-compile host tests)

Keep algorithm/parse/file-op logic — save parsers, checksums, mix logic, config readers,
format/metadata parsers, string helpers — written as **plain freestanding C with no libtonc,
no GBA registers, and no `sys.h`**. Include only `<stdint.h>` / `<string.h>` (plus a pure
header like FatFs's `ff.h` for file-op code), use explicit `uint8_t`/`uint16_t`, and read
multi-byte fields through explicit **little-endian byte helpers** rather than pointer casts.

Two payoffs:

1. **Host dual-compile.** The *same* `.c` files compile under clang/gcc into a unit-test
   binary (`tests/host_test.c`) and run on the dev machine — no emulator. Hardware-only
   pieces (registers, DMA, tonc text) stay out of these modules. (general homebrew — applies
   to any platform; the discipline is portable.)
2. **Avoids the unaligned-access fault.** On ARM7TDMI an unaligned 16/32-bit load
   (`*(u16*)(buf+off)` at an odd `off`) faults or silently rotates. Byte-readers
   (`rd16`/`rd32`) sidestep it entirely. Only overlay a packed struct where alignment is
   *proven*, and lock it with `_Static_assert(sizeof(T) == N, "...")` (devkitARM gcc defaults
   to gnu11+, so `_Static_assert` is available). (GBA-specific fault; DS/3DS ARM cores have
   their own alignment rules, but byte-reader discipline is portable.)

When vendoring a format/logic module into a new tool, preserve this convention — keep it free
of `sys.h`/tonc so it stays host-compilable.

**Watch the host-test link cascade.** Host tests are linked by hand-listed `.c` files (not the
Makefile's auto-glob), so the moment one pure-C core starts *referencing* a symbol in another
(e.g. a box module gains a "set name" that calls the string **encoder** living in the edit module),
**every host test that links the first file now needs the second added too** — or it fails at link
with `undefined reference`, not at compile. When you add a cross-core call, grep the test
compile-commands for every test that links the caller and add the new dependency (and update the
`cc …` comment in each test header). The GBA ROM build hides this because its glob links everything;
only the host tests surface it.
