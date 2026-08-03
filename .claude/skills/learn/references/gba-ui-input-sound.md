# On-Cartridge UI, Input, and Sound with libtonc

Self-contained reference for building a UI that runs *on the device* (no host OS) using **libtonc** on the Game Boy Advance. Covers Mode-3 framebuffer rendering and tile-text, list pickers, panic screens, a triple logger, tonc input edge/repeat semantics, and PSG square-wave sound jingles.

- [Scope & platform notes](#scope--platform-notes)
- [tonc init & frame tick](#tonc-init--frame-tick)
- [Rendering primitives & text](#rendering-primitives--text)
- [The ~29-column footer constraint](#the-29-column-footer-constraint)
- [List pickers / menus](#list-pickers--menus)
- [Panic / halt screens](#panic--halt-screens)
- [Input: key_poll / key_hit / key_repeat](#input-key_poll--key_hit--key_repeat)
- [The triple logger](#the-triple-logger)
- [Sound: PSG square-wave jingles](#sound-psg-square-wave-jingles)
- [Animations & sound around SD writes (OS-mode rule)](#animations--sound-around-sd-writes-os-mode-rule)

## Scope & platform notes

- libtonc (the tonc library: Text Engine "TTE", `key_*` input, `RGB15`, `EWRAM_*` macros) ships in the **devkitPro GBA toolchain** (`devkitpro/devkitarm`). (GBA-specific.)
- **Mode 3** is a GBA video mode: a single 240×160 16-bit framebuffer at `0x06000000`, `vid_mem[y*240 + x]`, each pixel **u16 BGR555**. (GBA-specific — DS and 3DS use entirely different video stacks: DS has dual engines/2 screens, 3DS has GPU-backed framebuffers. The framebuffer-blit *idea* ports; the addresses, modes, and pixel format do not.)
- **VRAM is always mapped**; only the gamepak ROM window gets unmapped during a flashcart SD transfer (see last section). Blitting after a read returns is always safe. (GBA-specific.)
- This document distills two render layers seen in practice: a **BG-mode-0 tile-text UI** (TTE on a tilemap) and a **Mode-3 8×8-font `ui.c` layer**. Both use the same TTE/key model below.

## tonc init & frame tick

Bring up a VBlank-paced tile-text UI on BG0 (mode 0):

```c
static void init_system(void) {
  irq_init(NULL);                                  // install tonc's IRQ dispatcher
  irq_add(II_VBLANK, NULL);                         // enable VBlank IRQ (frame pacing only)
  REG_DISPCNT = DCNT_MODE0 | DCNT_BG0;              // mode 0, BG0 on
  tte_init_se_default(0, BG_CBB(0) | BG_SBB(31));   // TTE in se (tilemap) mode on BG0
}
```

- `tte_init_se_default(layer, ctrl)` initializes the tonc Text Engine in **se** mode (regular tilemap text). `BG_CBB(0)` = char-base block 0 (tile graphics), `BG_SBB(31)` = screen-base block 31 (the tilemap) — the standard non-overlapping placement.
- The VBlank IRQ handler can be `NULL`; its only job is to wake `VBlankIntrWait()` for frame pacing.

The one-frame tick — **the only correct place to poll input**:

```c
static void vsync(void) { VBlankIntrWait(); key_poll(); }
```

`key_poll()` **must run exactly once per VBlank.** `key_hit`/`key_held` edge detection compares this frame's poll to last frame's; polling 0× or 2× per frame breaks rising-edge logic.

## Rendering primitives & text

Full-screen redraw via TTE escape codes:

```c
static void render(const char* text) {
  tte_erase_screen();
  tte_write("#{P:0,0}");   // TTE cursor-home escape
  tte_write(text);
}
```

Assemble the screen text line-by-line into **one fixed EWRAM buffer**, then hand it to `render()`. `strcat` has no `snprintf`-style guard, so **bounds-check every append manually**:

```c
static char EWRAM_BSS g_screen[2048];   // big buffers MUST be EWRAM_BSS, not on the stack
char line[80];
g_screen[0] = 0;
strcat(g_screen, "Title\n");
for (int i = 0; i < n; i++) {
  siprintf(line, "%s%s\n", (i == sel ? ">> " : "   "), names[i]);  // siprintf, NOT sprintf
  if (strlen(g_screen) + strlen(line) < sizeof(g_screen) - 1)       // manual overflow guard
    strcat(g_screen, line);
}
render(g_screen);
```

- **Use `siprintf`, never `sprintf`.** `sprintf` pulls in float-formatting code and a bigger stack frame; keep the codebase integer-only on purpose. (Also `sniprintf`/`vsnprintf` for bounded variants.)
- **Big/static buffers carry `EWRAM_BSS`** (and 4-byte `ALIGNED` for large ones). IWRAM is **32 KiB and holds the stack**; EWRAM is **256 KiB**. A save image, an 8 KiB log buffer, or a 2 KiB screen scratch on the IWRAM stack overflows it.

Color caveat (Mode-3 / tonc):
- `RGB15(r,g,b)` is an **`INLINE` function, not a constant macro** — it cannot appear in a `static` initializer (`error: initializer element is not constant`); it works fine as a *runtime* argument. For `static const` palettes/theme tables, define your own packing macro: `#define C(r,g,b) ((u16)((r)|((g)<<5)|((b)<<10)))`. Bit order is **BGR555**: bits 0-4 red, 5-9 green, 10-14 blue.
- **Runtime theming with zero call-site churn:** redefine `UI_*` color macros from `RGB15(...)` literals to fields of a live `Theme g_theme` struct; every existing `ui_text(..., UI_TITLE, ...)` recolors on the next render with no edits — switching themes becomes a struct copy.

Mode-3 8×8-font text quirks:
- **`ui_truncate` caps DISPLAY COLUMNS, not bytes.** Filenames can be UTF-8 (`FF_LFN_UNICODE`); a single column may be a 4-byte codepoint, so a buffer truncated to `max_cols` columns needs **`max_cols*4 + 1`** bytes. Undersizing overflows the stack (caused three real stack-buffer overflows). When in doubt, size on-screen-string buffers to 128.
- The 8×8 font is **ASCII-only**: map non-printable/high bytes to `.` when rendering arbitrary data.

### Decoding an image to the Mode-3 framebuffer (BMP — the byte-level footguns)

`vid_mem[y*240 + x]` is u16 **BGR555** and is always mapped (blit *after* the SD read returns). Decode **one source row at a time** into a small EWRAM buffer, convert, blit — never materialize a full 240×160×4 = 150 KiB RGBA frame. **Integer-decimation downscale** to fit: `while (w/scale>240 || h/scale>160) scale++;` then sample `src[ox*scale]` (no floats — no FPU). The BMP traps (each one bit; they generalize to other raster formats):

- **Row stride is 4-byte padded:** `stride = ((w*bpp + 31)/32)*4`, *not* `w*bpp/8`.
- **Bottom-up by default:** `biHeight > 0` ⇒ the *first* file row is the *bottom* image row; `< 0` ⇒ top-down. Flip the file-row index for bottom-up.
- **`INT_MIN`-safe height:** `biHeight == 0x80000000` makes `-h` signed-overflow UB — negate in unsigned: `(int)(0u - (u32)h)`.
- **Pixel byte order is BGR** (24/32-bit: `B,G,R[,A]`). **16-bit BMP is X1R5G5B5**, so swap vs GBA's red-low order: `r=(px>>10)&31; g=(px>>5)&31; b=px&31; out = r | g<<5 | b<<10`. 8-bit is palettized (entries are `B,G,R,0`).
- **Treat the file as hostile:** bound width/height, require `stride ≤ buffer` and `dataOffset < fileSize`, and reject what you don't handle (RLE compression, pre-v3 `biSize < 40` core headers) with a clear message instead of rendering garbage.

### Partial redraw — kill the full-screen "reload" on cursor moves

Mode 3 is a **single framebuffer with no page flip**, so the naive `for(;;){ render_full(); wait_key(); }` loop re-clears and re-paints the *entire* screen on every keypress. When the background is expensive (a tiled/blitted image, a sprite grid — e.g. a Pokémon PC box with a wallpaper + 30 icons), that full repaint **flickers and reads as a jarring "reload"** even though only the cursor moved. The fix is a partial redraw:

- Split rendering into **`render_full()`** (called on entry, and whenever the underlying data/page actually changes) and a light **`move_cursor(old, new)`** for cursor-only moves.
- `move_cursor` does three cheap things and **no `ui_clear`**: (1) *erase* the old cursor by repainting only its bounding rect — a **clipped** background repaint (`bg_patch(x,y,w,h)` that redraws just the tiles/fill inside the rect) plus any sprites whose rect overlaps it; (2) draw the cursor at the new position; (3) redraw only the side panel whose content changed (the selected item's detail).
- Drive it from the input loop: snapshot `old_sel` before handling the key; set a `need_full` flag for anything that changes the page/data; after handling, `if (!need_full && sel != old_sel) move_cursor(old_sel, sel);`.
- Result: a ~40 000-px/keypress repaint becomes a few hundred px, with no clear-flash. (Removing the `ui_clear` alone also removes the black-flash even if you still repaint everything — but the clipped patch is what makes it cheap.)
- You need a **clip-aware background paint**: the version that fills the whole region won't do — write one that only plots pixels inside `[cx,cy,cw,ch]` (clamped to the region). Forget to repaint overlapping sprites in the erased rect and you'll leave a hole where the cursor used to sit.

## The ~29-column footer constraint

The 8×8 font yields **30 columns at 240 px**; starting text at x=2 leaves **~29 usable columns**. `ui_text` does **not** truncate — an over-long footer **wraps to a sliver near the bottom of the screen and looks broken**. Keep footer/hint strings **≤29 chars**, or run them through `ui_truncate`. (GBA Mode-3-specific; the 8×8/240px math is the source of the number.)

## List pickers / menus

### Menu navigation idiom

Wrap-around modular arithmetic, with a `>> ` prefix marking the selected row:

```c
if (k & KEY_DOWN)      sel = (sel + 1) % n;
else if (k & KEY_UP)   sel = (sel == 0) ? n - 1 : sel - 1;
// row text: siprintf(line, "%s%s\n", (i == sel ? ">> " : "   "), names[i]);
```

The main list typically waits on mask `KEY_UP|KEY_DOWN|KEY_A|KEY_START|KEY_SELECT`; action/toggle screens on `KEY_L|KEY_R|KEY_A|KEY_B`.

### Scrolling-window list (for any menu that can outgrow the panel)

A fixed-row menu silently draws past its box and over the footer once it has more items than fit (a real bug at 10+ items). Keep a `sel` and a `top`:

- clamp `top` into `[0, max(0, n - VIS)]`;
- draw only `VIS` rows, mapping screen-row `r` → item `top + r` (compute y from `r`, do the highlight test on the item index);
- show `^` / `v` carets when the list is clipped above/below.

Behaviour is pixel-identical when the list is short. (This is the windowing a directory list-picker uses; `browse_scan` scans entries, a renderer draws the window with a selection bar, and a key loop runs the cursor.)

### Share cursor/state with sub-screens by pointer

When a sub-screen is opened from a parent (e.g. a stats view opened from a picker via SELECT), have the **child take the parent's cursor `(x,y)` by pointer and write it back**, and share mutable selection state the same way (pass toggle flags by pointer). The parent then reopens on the same item, and a toggle made in the child is reflected in the parent and the final operation.

## Panic / halt screens

A fatal path that leaves evidence on **both** screen and SD:

```c
static void halt_msg(const char* msg) {
  log_line("HALT: %s", msg);
  log_flush_to_sd(LOG_PATH);   // persist before spinning
  render(log_text());          // dump the whole log on screen
  while (1) vsync();           // spin, still pacing frames
}
```

Use it for unrecoverable startup failures (no flashcart, SD mount failed). It logs, flushes to SD, renders the full log, and spins — so a crash always leaves an on-screen dump *and* an SD artifact. (The logger/flush part is general homebrew; the rendering is Mode-3/tonc-specific.)

## Viewers (showing file content on screen)

### Open-by-type dispatch (route a file to the right viewer)

A tiny extension→handler registry keeps "open" extensible without editing the menu each time. Get the lowercased extension, look it up in a `{ext, fn, label}` table; **the default/fallback is always the universal hex/text viewer** (a type is listed only when a real handler exists, so "open" never dispatches to a stub). A recognized type adds an "Open …" row above the always-present "View (hex/text)". New format = one `view_*(path, name, size) -> modified` function + one table row — no enum/switch churn. Keep the extension-parsing helper pure C (host-testable; re-derive the basename, return the chars after the final dot, "" for no-dot / leading-dot / trailing-dot).

### Word-wrap on a byte-windowed text viewer

A viewer that pages by *byte offset* (read N bytes at `off`, scroll by changing `off`) can still render **word-wrapped** text without a line index: lay the page's bytes into ≤`VIS_ROWS` lines of ≤`COLS`, breaking at `\n` (skip `\r`), expanding tabs to fixed stops, mapping non-printable/high bytes to `.` (ASCII font), and wrapping a long line at the **last space** (else hard char-wrap, carrying the remainder to the next line). Navigation stays byte-based, so a page edge may split a line — acceptable for a reader, and it avoids the cost of a true line-offset model.

## Input: key_poll / key_hit / key_repeat

tonc key constants: `KEY_UP/DOWN/LEFT/RIGHT/A/B/L/R/START/SELECT`. (GBA-specific button set; DS adds X/Y + touch, 3DS adds C-stick/touch — the `key_hit`/`key_repeat` *model* is libtonc/GBA.)

**`key_hit(mask)` — rising edge, no auto-repeat.** Returns only keys that went up→down **this** frame. A blocking wait never auto-repeats:

```c
static u16 wait_keys(u16 mask) {
  u16 hit;
  do { vsync(); hit = key_hit(mask); } while (!hit);
  return hit;
}
```

`key_hit` is a **pure read** (no internal state), so it is safe to call alongside `key_repeat`, and it is unaffected by the repeat mask. Use it for **discrete actions** (confirm, toggle, insert a char) so holding a button doesn't spam the action.

**`key_repeat(mask)` — held-slide auto-repeat, and the two gotchas:**

1. **Read it ONCE per frame with the full mask, then bit-test.** The repeat cadence is driven by per-frame state from `key_poll()` plus the global `key_repeat_limits`/`key_repeat_mask`; structure the loop around a single `key_repeat(...)` call carrying every navigable direction and split the result, rather than calling it several times with different sub-masks. One read keeps all directions on the same repeat clock and avoids subtle desync:
   ```c
   u16 rep = key_repeat(KEY_UP|KEY_DOWN|KEY_LEFT|KEY_RIGHT);
   u16 nav = rep & (KEY_UP|KEY_DOWN);
   u16 chg = rep & (KEY_LEFT|KEY_RIGHT);
   ```

2. **`key_repeat()` only returns keys present in the `key_repeat_mask`.** The default mask is often UP|DOWN only. If you navigate a **horizontal** picker with LEFT/RIGHT, you **must** add `KEY_LEFT|KEY_RIGHT` to the mask (`key_repeat_mask(KEY_UP|KEY_DOWN|KEY_LEFT|KEY_RIGHT)`) or the cursor appears **stuck** — the keys are simply filtered out of the repeat result. (`key_hit` is edge-triggered and **unaffected** by the mask, so a key can work for discrete actions yet appear dead for held movement.)

**Repeat cadence** comes from the **global** `key_repeat_limits(delay, speed)` (a UI can drive this from a user setting).

**`key_repeat_mask` has no getter — wrap it to save/restore.** A modal that widens the mask then hard-restores a literal (`key_repeat_mask(KEY_UP|KEY_DOWN)`) **clobbers a nested caller's mask** — e.g. a viewer using UP/DOWN/L/R opens an on-screen keyboard; if the keyboard restores UP/DOWN only, the viewer's L/R paging-repeat silently dies on return. Track the current mask yourself (`ui_get_repeat_mask`/`ui_set_repeat_mask`) and have every modal do:
```c
u16 saved = ui_get_repeat_mask();
ui_set_repeat_mask(mine);
/* ... modal ... */
ui_set_repeat_mask(saved);
```
(A real shipped bug.)

**Edge-keys don't leak between screens — read with `key_hit`, not levels.** A child screen entered on a key-press can exit on the **same** key without the parent re-acting: the parent consumed the press-edge opening the child; the child re-polls and the key is still *held* (no new edge). You get this for free **only** by reading actions with `key_hit` (rising edge), never with `key_state`/level polling.

## The triple logger

Three sinks so output is visible **wherever the ROM runs**. (The screen sink is GBA/tonc-specific; the mGBA-channel and SD-file sinks are general homebrew patterns — any emulator with a debug channel + any FAT SD works the same way.)

1. **In-RAM text buffer** the UI prints on screen — the only sink that works on real hardware.
2. **mGBA debug console** — only when running under the mGBA emulator.
3. **A file on the SD card** — the persistent artifact for hardware debugging.

API shape:

| Function | What it does |
|---|---|
| `log_init()` | Probe the mGBA channel, then clear the buffer. Call once at startup before any `log_line`. |
| `log_under_mgba()` | `1` if running under mGBA (channel live), else `0`. |
| `log_line(fmt, ...)` | printf-style (`vsnprintf` into a 256-byte stack tmp, ≤255 chars). Emits to mGBA first, then appends the line + `\n` to the RAM buffer. Safe before SD mount. |
| `log_clear()` | Reset the in-RAM buffer to empty. |
| `log_text()` | The accumulated NUL-terminated buffer, for on-screen rendering (`render(log_text())`). |
| `log_flush_to_sd(path)` | Write the whole buffer to `path` with `FA_WRITE\|FA_CREATE_ALWAYS`. Returns `0` on success, the FatFs `FRESULT` on FS error, `-1` on short write. Call after SD is mounted. |

**mGBA detection & emit** (fixed memory-mapped registers; respond **only** under the emulator):
```c
#define MGBA_REG_ENABLE (*(volatile unsigned short*)0x4FFF780)
#define MGBA_REG_FLAGS  (*(volatile unsigned short*)0x4FFF700)
#define MGBA_LOG_BUF    ((volatile char*)0x4FFF600)
#define MGBA_LEVEL_INFO 3

MGBA_REG_ENABLE = 0xC0DE;                      // enable
s_mgba = (MGBA_REG_ENABLE == 0x1DEA) ? 1 : 0;  // readback == 0x1DEA → under mGBA
```
To emit: copy ≤255 bytes to `MGBA_LOG_BUF`, NUL-terminate, then write `MGBA_REG_FLAGS = 0x100 | level` (level 3 = INFO). On real hardware `s_mgba == 0` and emit is a no-op — **SD + screen are your only sinks on hardware.** (These register addresses/magics are mGBA-specific; the *technique* generalizes to any emulator's debug channel.)

**In-RAM buffer is a keep-newest ring** (`EWRAM_BSS s_buf`, e.g. 8 KiB cap). On overflow it **drops the oldest half** rather than truncating new output:
```c
if (s_len + n + 2 >= LOG_CAP) {
  unsigned keep = LOG_CAP / 2;
  if (s_len > keep) { memmove(s_buf, s_buf + (s_len - keep), keep); s_len = keep; }
  else              { s_len = 0; }
}
```
Consequence: chatty sessions lose their **earliest** lines, on screen *and* in the file. Single lines are capped ~255 chars.

**SD flush OVERWRITES, never appends.** `FA_CREATE_ALWAYS` truncates-or-creates, so each flush replaces the whole file with the current buffer (the live ring) — an overflowed early line is gone from the file too. A useful side effect: the **first** `log_flush_to_sd` right after `f_mount` doubles as proof the SD write path works.

**Canonical startup wiring:**
```c
init_system();                                   // tonc UI up
log_init();                                       // probe mGBA, clear buffer
log_line("=== app start ===");
if (!flashcart_activate())  halt_msg("No flashcart detected!");
if (f_mount(&fs, "", 1) != FR_OK) halt_msg("SD mount failed!");
/* ... scan dir ... */
log_flush_to_sd(LOG_PATH);                         // first flush = SD-write proof
```

## Sound: PSG square-wave jingles

Drive **self-contained PSG (programmable sound generator) square-wave jingles** on a single sound channel **from the main loop** — **no maxmod, no sample assets**. (GBA-specific: the 4 PSG channels + tone/sweep registers are the original-Game-Boy-derived audio block. DS/3DS have their own audio hardware/APIs; the "synthesize a jingle, no samples" *idea* ports, the registers do not.)

Key properties to preserve when lifting this kind of code:
- One channel, simple square tones, sequenced by the **main loop** (set a tone, advance on frame ticks) — not by a streaming/IRQ mixer.
- No external audio files; tones are generated, so the jingle costs ~0 asset space.

**Verified working recipe (libtonc register names, builds + links clean).** This whole engine is ~2.5 KiB of code, **0 EWRAM, no IRQ/timer**. The trick that makes it trivial: a *decreasing volume envelope* means every note **silences itself** — you never have to stop a channel, so the effect functions are fire-and-forget and safe to call every frame.

```c
// init once at boot:
REG_SNDSTAT   = 0x0080;   // SOUNDCNT_X bit7 = master enable
REG_SNDDMGCNT = 0xFF77;   // SOUNDCNT_L: lo byte = L/R master vol (7 each);
                          //   hi byte = per-channel L/R enables (0xFF = all 4 chans, both speakers)
REG_SNDDSCNT  = 0x0002;   // SOUNDCNT_H bits0-1 DMG mix: 00=25% 01=50% 10=100%

// one square beep (channel 1): hz, volume 0-15, env step 0-7 (decay speed), duty 0-3 (2 = 50%)
static inline u16 hz_to_rate(u16 hz){ u32 d = 131072u/(hz?hz:1); return (d>=2048)?0:(2048-d); } // bits0-10
static void sq(u16 hz, u8 vol, u8 env, u8 duty){
    REG_SND1SWEEP = 0;                                       // no sweep -> no pitch drift
    REG_SND1CNT   = ((u16)vol<<12) | ((u16)env<<8) | ((u16)duty<<6); // env dir bit11=0 => decrease => auto-silence
    REG_SND1FREQ  = 0x8000 | hz_to_rate(hz);                 // bit15 = restart/trigger
}
// a noise "buzz" for errors (channel 4): REG_SND4CNT = (vol<<12)|(env<<8); REG_SND4FREQ = 0x8000 | freqdiv;
```

Earcon palette that reads well: cursor-move = a *short, quiet, high* tick (vol ~5, env 1); A/confirm = brighter; B/back = lower; deny/error = the noise channel. Multi-note jingles (save success, boot) = a tiny **frame-ticked queue** advanced once per frame from your VBlank tick — non-blocking, far better than blocking `VBlankIntrWait()` loops between notes.

**The non-obvious integration win — and its one gotcha.** Hook the earcons at the **single input chokepoint** (your central wait-for-key helper) and the whole app gets sound from one edit. But play the cursor tick on **FRESH presses only** (`key_hit`), **never on `key_repeat`** — otherwise a held d-pad scroll machine-guns the speaker. (See the key-repeat gotcha above; it bites here too.) Screens that bypass the central wait helper with their own `do{ key_poll(); }while` loop need the hook added locally, and their per-frame tick must call the jingle-queue advance or the multi-note jingles won't play there.

(GBA-specific: the 4 PSG channels are the original-Game-Boy audio block. DS/3DS have their own audio APIs; the "synthesize fire-and-forget beeps via a decay envelope, no samples" *idea* ports, the registers do not.)

## Animations & sound around SD writes (OS-mode rule)

**The hard rule (GBA flashcart-specific):** during *any* EZ-Flash SD transfer the **gamepak ROM is unmapped**, and audio IRQ + SD read **cannot coexist**. Therefore:

- Run **all audio and animation AROUND the SD op** — while ROM is mapped and IRQs are on — **never during the transfer.**
- Pattern: `f_read`/`f_lseek` into an **EWRAM** buffer **completes first**, *then* you draw/animate/play. VRAM is never unmapped, so blitting after a read returns is safe; ROM-resident UI code simply isn't executing mid-transfer (it's blocked inside the EWRAM-resident driver).
- Consequence: **no decode-while-streaming.** Load-to-EWRAM-then-play is the only audio path; this is why mp3/video *streaming* is impossible on this setup, and why a progress animation must tick between read chunks, not during one.

(DS/3DS flashcart and storage stacks differ; the "ROM-window-unmapped during SD I/O" constraint is specific to GBA flashcart OS-mode. The general lesson — don't run an IRQ-driven mixer/animation concurrently with a blocking storage transfer that steals the bus/mapping — is worth carrying anywhere.)
