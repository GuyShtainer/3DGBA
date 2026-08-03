# Flashcart SD Sector I/O (cartridge-resident homebrew)

How cartridge-resident GBA homebrew reads/writes the flashcart's microSD by sector,
for **EZ-Flash Omega DE** and **EverDrive GBA X5**. The hard part is not the SD
protocol — it is surviving the moment the cartridge unmaps the game ROM out from under
the running CPU. (GBA-specific in the constants; the *shape* of the problem recurs on
any flashcart, see last section.)

- [The contract](#the-contract)
- [Cart detection & activation](#cart-detection--activation)
- [The OS-mode rule (ROM disappears)](#the-os-mode-rule-rom-disappears)
- [Why I/O code+data must live in EWRAM](#why-io-codedata-must-live-in-ewram)
- [IRQ discipline & the is_reading kill-switch](#irq-discipline--the-is_reading-kill-switch)
- [DMA notes](#dma-notes)
- [Read vs write asymmetry](#read-vs-write-asymmetry)
- [Write is Omega-only](#write-is-omega-only)
- [Reboot to loader as an exit](#reboot-to-loader-as-an-exit)
- [How this generalizes](#how-this-generalizes)

## The contract

Both backends expose one uniform contract behind a dispatch layer: **512-byte LBA
sectors**. A read/write takes `(sector, buffer, count)` where `count` is a sector count,
so `buffer` must be `>= count * 512` bytes. There is no sub-sector or arbitrary-length
primitive — everything is whole 512-byte sectors. (GBA-specific API; 512-byte LBA is a
general SD/MMC fact.)

```c
enum ActiveFlashcart { NO_FLASHCART, EVERDRIVE_GBA_X5, EZ_FLASH_OMEGA };
extern ActiveFlashcart active_flashcart;          // set by activate()
extern volatile bool   flashcartio_is_reading;    // IRQ guard, see below

bool flashcartio_activate(void);                              // detect+init once
bool flashcartio_read_sector (u32 sector, u8* dst, u16 count);
bool flashcartio_write_sector(u32 sector, const u8* src, u16 count); // Omega only
```

A higher filesystem layer (FatFs `diskio`) normally sits on top; apps rarely call the
sector API directly except `activate()` at startup.

## Cart detection & activation

Call `activate()` **once at startup**; it detects the cart, initializes it, and sets
`active_flashcart`. Halt the app if it returns `false` (no supported cart).

**Detection order is EverDrive-FIRST, EZ-Flash-second** — preserve this order:

- The EverDrive probe is a cheap, non-invasive register-writability test: unlock by
  writing a key byte to a write-only key register, then confirm a config register is
  *unwritable before* the key and *writable after* it.
- The EZ-Flash probe is **invasive**: it unmaps ROM and compares the ROM-header checksum.
  Its logic is *inverted by intent* — it reads the header-checksum complement, maps the
  bootloader page, and if the checksum **still matches**, this is NOT an EZ-Flash. It then
  locates where the running ROM was remapped (try the PSRAM page first, then scan the NOR
  pages) and stores that "home" page for later restore.

Run the cheap/safe path first so EverDrive hardware never has its ROM map disturbed by the
EZ-Flash probe. After EverDrive init succeeds, **lock its registers**; every later read
must bracket itself with unlock/lock.

(EverDrive-specific) Set the emulated save type to match the game being run; a wrong save
type can corrupt OS-mode behavior.

## The OS-mode rule (ROM disappears)

**This is the single load-bearing fact.** While the EZ-Flash performs an SD op it remaps
the **entire `0x08000000` game-ROM window** to the bootloader/OS page. For the duration of
the transfer, *all game ROM is gone*. Any code the CPU executes, any constant/data it
reads, or any IRQ vector that touches ROM during the op will crash.

The EZ-Flash op is bracketed by a rompage flip: enter OS/SD mode (map bootloader page) →
transfer → restore the running game's page. The "home" page to restore is discovered once
at startup and kept in EWRAM so it survives across calls (it cannot be read from the
now-unmapped ROM).

- (EZ-Flash) Whole ROM window unmapped → maximally hostile.
- (EverDrive) Only its *last 16 MB* of ROM is disabled — less hostile — but disciplined
  code still brackets EverDrive ops with IRQs off and treats it the same way.

Practical consequence: **read into an EWRAM buffer, let the call return, THEN render.**

```c
// SAFE: transfer completes, ROM is back, then we draw.
flashcartio_read_sector(lba, ewram_buf, n);   // ROM unmapped *inside* this call
draw_from(ewram_buf);                          // ROM mapped again here
```

VRAM (`0x06000000`) is **never** unmapped — only the gamepak window is — so blitting
*after* a read returns is always safe. There is **no decode-while-streaming**: an audio
IRQ and an SD read cannot coexist, which is why streamed audio/video off the SD is
impossible — load-to-EWRAM-then-play is the only path.

## Why I/O code+data must live in EWRAM

The transfer routine cannot run from ROM — it would be executing code that vanishes
mid-op. So **every function on the I/O hot path, and every byte of data it touches during
a transfer, must reside in EWRAM** (`0x02xxxxxx`), which is never unmapped.

- Mark hot-path functions with the EWRAM-code attribute (section `.ewram`). Mark large
  static buffers EWRAM-BSS (section `.sbss`), 4-byte aligned.
- Use **EWRAM** (256 KiB) for buffers, **never the IWRAM stack** (32 KiB — it holds the
  stack; a big buffer there overflows it).
- (devkitARM detail) You generally do **not** need `long_call` on declarations: GNU `ld`
  auto-inserts long-branch veneers for out-of-range `bl`s between ROM callers and EWRAM
  code. Put the EWRAM attribute on the **definition**; a plain prototype in the header is
  enough.

## IRQ discipline & the is_reading kill-switch

Two layers of protection, both intentional — keep both (belt-and-suspenders):

1. **`REG_IME` off around the transfer.** The interrupt master enable is saved/cleared
   before the rompage flip and restored after, *inside* the driver, **and** around the
   dispatch call site. Disabling in both places is deliberate.
2. **`flashcartio_is_reading` — the IRQ kill-switch.** Set `true` around *both* reads and
   writes (the write path reuses the read guard). Any IRQ/VBlank handler that *could* fire
   must consult this flag and **refuse to touch the cart ROM or `SoftReset`** while it is
   true. A `SoftReset` or a ROM-reading vector firing mid-transfer is a crash.

```c
void on_vblank(void) {
    if (flashcartio_is_reading) return;   // ROM is gone — do nothing ROM-touching
    /* ... normal ROM-touching handler ... */
}
```

The general rule: anything that runs asynchronously must be inert (or ROM-free) while a
transfer is in flight.

## DMA notes

The transfer moves data with a **32-bit-word DMA** (copies `size>>2` words). 512 is a
multiple of 4, so sectors are fine — but **buffers must be 4-byte aligned** or tail bytes
drop.

- (GBA-specific) Defaults to **DMA3**. If the app drives audio over DMA1/DMA2 (which are
  **higher priority** than DMA3 and will corrupt a DMA3 SD transfer), either move SD reads
  to **DMA1** or **disable DMA** entirely (a plain word-copy loop — slow but safe).
- EverDrive auto-routes a destination in ROM space into PSRAM during reads (you usually
  read into EWRAM anyway).

## Read vs write asymmetry

These differ and the difference matters when reimplementing (EZ-Flash specifics):

- **Transfers run in bursts of at most 4 sectors per hardware command.** Loop in steps of
  4; there is no single-shot arbitrary-`count` command.
- **Reads retry once; writes do NOT retry.** The read burst retries on timeout (a small
  fixed retry count with a delay); the write burst returns failure immediately on the
  first timeout. **The caller MUST verify every write itself** (read-back / byte-compare /
  checksum) — never assume a write succeeded.
- **Write DMAs data into the hardware window BEFORE issuing the command; reads DMA out
  AFTER.** This ordering is opposite between the two paths — preserve it if reimplementing.
- A status register reports "busy" with a specific sentinel value; success is detected by
  reading *anything else*, with a timeout after a bounded spin count. Don't invert the
  polarity.

The "no write retry" point is why the **verified-write pattern** exists: write to a
temporary, re-read and byte-compare, back up the original, then atomically rename. Always
take an immutable backup before a destructive write. (General homebrew principle; the
EZ-Flash no-retry behavior makes it mandatory here.)

## Write is Omega-only

In this dispatch, **`write_sector` only implements the EZ-Flash Omega case**; everything
else (including EverDrive) returns `false`. The EverDrive driver *has* a working low-level
multi-block write, it is simply not wired through the dispatch — so **EverDrive runs as a
read-only tool**. Gate every write feature on `active_flashcart == EZ_FLASH_OMEGA`; never
assume writes work on EverDrive.

(Non-critical config files — where worst case is "defaults next launch" — can skip the
full verified-write pipeline, but still gate the *write* on Omega.)

## Reboot to loader as an exit

There is **no documented "return to menu" API**. The working equivalent (GBA-specific),
which must run from an **EWRAM-code** function because ROM vanishes the instant the page
flips, after quiescing IRQ/DMA/timers:

- **EZ-Flash Omega DE:** set the rompage to the **bootloader** page, then BIOS `swi 0x00`
  (`SoftReset`). Proven to reach the EZ-Flash kernel menu on Omega DE.
- **EverDrive GBA X5:** `ed_unlock_regs()` first (mandatory — registers are locked after
  activation), then `ed_reboot(0)`.

## How this generalizes

The portable lesson: **whenever the storage device hijacks the address space the CPU is
executing from, the transfer routine and its data must live in memory that stays mapped,
with interrupts quiesced and any async handler gated on a "transfer in flight" flag.**

- Other GBA flashcarts (e.g. EverDrive vs EZ-Flash here) expose the *same* 512-byte LBA
  sector contract but completely different low-level register protocols — abstract behind a
  per-cart dispatch keyed on a detected-cart enum.
- (Applies to DS/3DS too, details differ) DS/3DS homebrew reaches the SD via libnds /
  libctru / a FAT library rather than raw vendor MMIO, and on those platforms the OS keeps
  storage mapped — so the "ROM disappears mid-transfer" hazard and the EWRAM-resident-code
  requirement are largely **GBA-specific** to this flashcart design. The *durable* parts —
  512-byte sectors, treat-all-SD-data-as-hostile, verify writes, back up before destructive
  ops — carry over unchanged.
