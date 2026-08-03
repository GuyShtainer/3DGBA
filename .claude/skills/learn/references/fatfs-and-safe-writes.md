# FatFs over Flashcart SD + the Never-Corrupt-User-Data Write Pipeline

A reference for FatFs (elm-chan `f_*` API) running on top of a flashcart SD block device in console homebrew, and the verified-write pattern that guarantees the original file survives every failure. General to any FatFs-on-SD homebrew; GBA-specific memory/timing facts are tagged.

- [FatFs mounting & block-device glue](#fatfs-mounting--block-device-glue)
- [ffconf.h knobs that gate each f_* op](#ffconfh-knobs-that-gate-each-f_-op)
- [Listing, copy, delete, move recipes](#listing-copy-delete-move-recipes)
- [Why every write must be verified](#why-every-write-must-be-verified)
- [The verified-write pipeline](#the-verified-write-pipeline)
- [Multi-file commit & atomicity caveat](#multi-file-commit--atomicity-caveat)
- [Load-bearing gotchas checklist](#load-bearing-gotchas-checklist)

## FatFs mounting & block-device glue

FatFs is filesystem-only. You supply a thin media layer that maps its disk callbacks onto your flashcart's sector read/write primitives (general homebrew):

| Callback | Responsibility |
|---|---|
| `disk_status` | `0` (ready) for the mounted drive, else `STA_NOINIT`. |
| `disk_initialize` | `0` once the SD/flashcart is detected; no per-drive init beyond that. |
| `disk_read` | Read N 512-byte sectors into a buffer. |
| `disk_write` | Write N sectors (only when `FF_FS_READONLY == 0`). `RES_ERROR` on any failed sector. |
| `disk_ioctl` | Answer `CTRL_SYNC`, `GET_SECTOR_SIZE` (512), `GET_BLOCK_SIZE`, and (only if you enable `f_mkfs`) `GET_SECTOR_COUNT`. |
| `get_fattime` | Pack the RTC into a DOS datetime DWORD for file timestamps; return `0` when no RTC. |

**Mount discipline (general):**

```c
FATFS fs;                       /* MUST outlive all FatFs use — give it static/long-lived storage */
f_mount(&fs, "", 1);            /* third arg 1 = force mount now, not lazily */
```

The `FATFS` object backs the whole volume; if it goes out of scope while files are open, the volume state is gone. Mount it once for the life of the program.

**Buffer-alignment trap (GBA-specific, but the principle is general).** A DMA-based sector copy may require a word-aligned buffer. Glue code that only tests the low bit (`((u32)buff & 0x1)`) lets a merely 2-byte-aligned buffer slip onto the "direct" DMA path and corrupt. Stage odd/unaligned buffers through a static aligned scratch buffer and `memcpy`; pass aligned buffers straight through. Process a few sectors per iteration to bound the scratch size.

**Where buffers live (GBA-specific).** Staging scratch and any large working array must be `static` in EWRAM (the 256 KiB external work RAM), never on the IWRAM stack (~32 KiB, holds the stack). On the GBA this is `__attribute__((section(".sbss")))` (commonly an `EWRAM_BSS` macro), 4-byte aligned. (Applies to DS/3DS too: keep big buffers off the stack — the stack/heap sizes and section names differ.)

**Timestamps.** `get_fattime` reads the cartridge RTC and packs `((year-1980)<<25)|(month<<21)|(day<<16)|(hour<<11)|(minute<<5)|(second/2)`. Return `0` on RTC failure rather than a fabricated date — FatFs treats `0` as "unset". With `FF_FS_NORTC 0` the fixed-date fallback constants are inert.

## ffconf.h knobs that gate each f_* op

FatFs is compile-time trimmed. The options that decide which calls even exist (general):

| Option | Gates | Set to (for a file manager) |
|---|---|---|
| `FF_FS_READONLY` | all write/create/delete/rename + `f_write`/`f_sync`/`f_unlink`/`f_mkdir`/`f_rename`/`f_truncate`/`f_getfree` | `0` (write API on) |
| `FF_FS_MINIMIZE` | master feature gate (levels below) | `0` (everything present) |
| `FF_USE_FIND` | `f_findfirst`/`f_findnext` (needs MINIMIZE ≤ 1) | `0` if you filter in C |
| `FF_USE_MKFS` | `f_mkfs`/`f_fdisk` | `0` (lets you skip `GET_SECTOR_COUNT`) |
| `FF_USE_CHMOD` | `f_chmod`/`f_utime` | `1` (needed to clear `AM_RDO` before deleting read-only files) |
| `FF_FS_RPATH` | `f_chdir`/`f_chdrive` (≥1), `f_getcwd` (=2) | `2` for full relative-path nav |
| `FF_USE_LFN` | long filenames in `FILINFO.fname` | `1` (static working buffer) |
| `FF_LFN_UNICODE` | path/name encoding | `2` → UTF-8, `TCHAR = char` |
| `FF_FS_EXFAT` | exFAT support | `1` (**requires** `FF_USE_LFN`) |
| `FF_FS_LOCK` | duplicate-open / open-object guard | `0` = **no guard** (see gotchas) |
| `FF_FS_REENTRANT` | thread-safety wrappers | `0` (single-threaded only) |

**`FF_FS_MINIMIZE` levels** (cumulative removals):

| Level | Removes |
|---|---|
| 0 | nothing |
| 1 | `f_stat`, `f_getfree`, `f_unlink`, `f_mkdir`, `f_truncate`, `f_rename` |
| 2 | + `f_opendir`, `f_readdir`, `f_closedir` |
| 3 | + `f_lseek` |

A delete/create/move/stat file manager needs `FF_FS_MINIMIZE == 0`; directory listing alone needs ≤ 1.

**Consequences to remember:**
- `FF_USE_LFN 1` uses a **static BSS working buffer** → FatFs is **not thread-safe**; combined with `FF_FS_REENTRANT 0`, single-threaded use only (fine for a cooperative console main loop).
- `FF_LFN_UNICODE 2` makes **every path/filename string UTF-8**; `FILINFO.fname` is a UTF-8 long name.
- `FF_FS_EXFAT 1` requires LFN and drops C89 compatibility — don't disable LFN while exFAT is on.
- Enabling LFN imposes linking the Unicode conversion table (`ffunicode.c`).
- **There is no `f_copy` in FatFs, ever** — copies are hand-rolled (below).
- If `FF_USE_MKFS 0`, `GET_SECTOR_COUNT` in `disk_ioctl` can stay unimplemented. Enabling `f_mkfs` later forces you to implement it.

## Listing, copy, delete, move recipes

### Directory listing — `f_opendir` + `f_readdir`

End-of-directory is signalled by **`fno.fname[0] == 0` returned WITHOUT an error**, not by an error code. Forgetting this loops forever or misreads the last entry.

```c
DIR dir; FILINFO fno;
if (f_opendir(&dir, path) != FR_OK) return -1;
while (f_readdir(&dir, &fno) == FR_OK && fno.fname[0]) {
    if (fno.fattrib & AM_DIR) { /* directory */ }
    else                      { /* file: fno.fname is UTF-8, fno.fsize is size */ }
}
f_closedir(&dir);
```

`f_readdir(dp, NULL)` rewinds the directory. With `FF_USE_FIND 0`, wildcard filtering (e.g. `*.sav`) is **not** available — filter in C on `fno.fname`.

### File copy — manual `f_read`/`f_write` loop

No `f_copy` exists. Open source `FA_READ`, dest `FA_WRITE | FA_CREATE_ALWAYS` (or `FA_CREATE_NEW` to refuse clobbering). **EOF = `br < btr`; disk-full = short write `bw < btw`.** Stream through a fixed buffer (e.g. 4 KiB in EWRAM), treat a short write as an error, and promote a failing `f_close` on the *write* handle to a write error (a successful close is what flushes the last buffer).

```c
f_open(&src, s, FA_READ);
f_open(&dst, d, FA_WRITE | FA_CREATE_ALWAYS);
for (;;) {
    UINT br, bw;
    f_read(&src, buf, CHUNK, &br);
    if (br == 0) break;                 /* EOF */
    f_write(&dst, buf, br, &bw);
    if (bw != br) { err = WRITE; break; } /* short write => disk full */
}
f_close(&src);
if (f_close(&dst) != FR_OK) err = WRITE; /* close error is NOT silently ignored */
```

### Delete — `f_unlink`

`f_unlink(path)` removes a file or sub-directory. Caveats:
- A read-only object (`AM_RDO`) → `FR_DENIED`. Clear it first: `f_chmod(path, 0, AM_RDO)` (needs `FF_USE_CHMOD 1`), then unlink.
- A non-empty sub-directory, or the current directory → `FR_DENIED`. For "delete folder", list-and-remove children first.
- The object must not be open. With `FF_FS_LOCK 0` this is **not** rejected for you — `f_close` first or risk corrupting the volume.

### Move / rename — `f_rename` (same volume only)

`f_rename(old, new)` renames and/or moves **within one volume**. It cannot cross drives, cannot rename an open object, and fails `FR_EXIST` if `new` already exists. For cross-volume moves, fall back to copy-then-`f_unlink`. `f_rename` is also the atomic swap step of the verified write below.

## Why every write must be verified

EZ-Flash (and similar flashcart) SD writes have **no retry / no error correction at the cart level** — a write either lands or silently does not, and the hardware will not re-attempt it for you (GBA-specific; the principle that flashcart/SD writes are fallible generalizes to DS/3DS homebrew). Because of that:

- **Never destroy the original until the replacement is confirmed byte-identical on disk.**
- **Treat short reads/writes as failures** (`br != want`, `bw != len`).
- **Re-read after writing and `memcmp`** against the intended bytes — do not trust the write return code alone.
- **Take an immutable backup first** so even a catastrophic mid-swap failure leaves a recoverable copy.

## The verified-write pipeline

A reusable "never corrupt the original" sequence. Stage it in three primitives.

**1. Immutable backup (rotate, never overwrite).** Pick the first non-existent name among `<src>.bak`, `<src>.bak1` … `<src>.bakN` (a bounded loop). Copy src → that name, then **re-verify the copy byte-for-byte** before declaring success. If all slots are taken, fail (`SF_ERR_BACKUP`) and abort the commit — do not overwrite an existing backup. (Bounded slot count means a long-running tool eventually needs the user to clear old `.bak*` files.)

**2. Verified write into a temp, then atomic swap.** The original is untouched unless the new bytes verify on disk:

```text
1. WRITE TEMP   write buf(len) -> <path>.tmp (FA_WRITE|FA_CREATE_ALWAYS)
                check fr==FR_OK AND bw==len AND f_close==FR_OK
                any failure -> f_unlink(tmp), return WRITE error
2. RE-READ      re-open <path>.tmp FA_READ, compare to intended buf in
                fixed-size chunks; treat br != want as failure
                mismatch -> f_unlink(tmp), return VERIFY error
3. SWAP         f_unlink(path)   (ignore error if absent)
                f_rename(tmp, path)
                rename failure -> return RENAME error, LEAVING the verified
                .tmp on disk for manual recovery
```

**The rename gap.** Step 3 unlinks the original *before* the rename. If the rename then fails, the original is gone but the **verified `.tmp` still exists** — this is the one window where the original isn't present, and callers/users must know to recover from `<path>.tmp`. This ordering is deliberate: it is the only point of risk, and it is recoverable.

**Byte-compare equality (the verify primitive).** To compare two files: short-circuit on size mismatch, then read both in fixed chunks into **two distinct buffers** and `memcmp` each chunk. Sharing one buffer silently makes every comparison pass — a "cleanup" that collapses them turns verification into a no-op. A length mismatch on any chunk is a failure.

**Dry-run primitive.** Write `work[0..size)` to `<path><suffix>`, re-read, byte-compare to `work`, then `f_unlink` — return OK iff identical. This proves the SD write path works on the real bytes **without touching the original**, and is the leaf behind any "validate without writing" mode.

**Read-whole-file with clean close.** Open `FA_READ`, single `f_read` up to `cap`, **`f_close` first, then check the read result** — so the handle is never leaked on an error path. Report actual bytes read to the caller.

A generic error enum that any tool can reuse: open / read / write / size / verify / backup / parse / rename / layout — most are generic file-IO outcomes; only parse/validate and layout are domain-specific.

## Multi-file commit & atomicity caveat

To update N files "together", gate all writes behind a full validation pass:

| Phase | Action | Writes? |
|---|---|---|
| 1. Snapshot all | Read every input fully into RAM before anything changes. | none |
| 2. Compute all | Produce every output buffer in RAM from the snapshots. | none |
| 3. Validate all | Run each output through the dry-run round-trip + re-parse. | temp only, deleted |
| 4. Commit each | Only if **all** validated: per file, backup then verified-write, in order. | yes |

Stopping after Phase 3 gives a pure dry run (nothing modified).

**Atomicity caveat:** validation is all-or-nothing, but the writes in Phase 4 are **sequential and per-file**. If file B's write fails after A committed, A is already updated (A has a `.bak` for recovery). True cross-file atomicity is **not** guaranteed — only per-file safety (verified write + backup) plus the all-or-nothing *validation* gate. Anything needing stronger guarantees must build a journal on top.

## Load-bearing gotchas checklist

- **No `f_copy`** — hand-roll `f_read`/`f_write`; EOF = `br < btr`, disk-full = `bw < btw`.
- **Dir-listing end is `fname[0]==0` without an error**, not an error code.
- **`FF_FS_LOCK 0` gives no open-object guard** — always `f_close` before `f_unlink`/`f_rename` on the same object or risk volume corruption.
- **LFN static buffer + `FF_FS_REENTRANT 0` = single-threaded only.**
- **`f_mount` must be forced (arg 1) and `FATFS` must outlive all use.**
- **No large stack buffers (GBA-specific)** — big working arrays are `static` in EWRAM, 4-byte aligned; an IWRAM stack local of tens of KiB silently corrupts memory.
- **Buffer alignment for DMA (GBA-specific)** — testing only the low bit lets a 2-byte-aligned buffer onto the direct path; stage odd buffers through aligned scratch.
- **Short reads/writes are failures** — check `br != want` and `bw != len` everywhere.
- **Verify needs two distinct compare buffers** — one shared buffer makes every compare pass.
- **EZ-Flash writes don't retry (GBA-specific)** — re-read and byte-compare every write; never trust the return code alone.
- **The rename gap** — original is briefly absent during the swap; the verified `.tmp` is the recovery artifact.
- **`get_fattime` returns 0 on RTC failure** — no fabricated dates.
- **Backup slots are bounded and never overwritten** — commits start failing once full; user must clear old `.bak*`.
