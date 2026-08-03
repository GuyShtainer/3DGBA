# Capabilities, Limits, and Cross-Platform Notes

A reference for what is realistic to build on handheld-flashcart homebrew (GBA-class)
and how that scales to DS/3DS. It separates **feasible** from **infeasible**, explains the
**RTC** date/time clock and its hard limits, defines the **hardware-validation gate** (the SD/RTC/
destructive-write path is not emulated), and gives **cross-platform** notes plus a quick feasibility
heuristic.

- [Feasibility heuristic (read this first)](#feasibility-heuristic-read-this-first)
- [Feasible on GBA-class hardware](#feasible-on-gba-class-hardware)
- [Infeasible on GBA-class hardware](#infeasible-on-gba-class-hardware)
- [The cartridge RTC (date/time only)](#the-cartridge-rtc-datetime-only)
- [The hardware-validation gate](#the-hardware-validation-gate)
- [Cross-platform: GBA vs DS vs 3DS](#cross-platform-gba-vs-ds-vs-3ds)

## Feasibility heuristic (read this first)

Before promising a feature, score it against the host's real budget:

1. **Does it need real-time codec decode (audio/video/PDF/font rasterization)?** On GBA-class
   hardware (single ~16.78 MHz ARM7TDMI, no FPU, 256 KiB main work RAM) the answer is almost
   always **infeasible** in real time. Parsing the file's *metadata* almost always **is** feasible.
2. **Does the working set fit in main RAM?** If the honest minimum (decode tables + buffers + one
   output frame) exceeds your RAM budget, it's out — there's no swap/heap to lean on.
3. **Does it touch the SD card, the RTC, or overwrite user data?** If yes, it is **not "done" until
   validated on real hardware** — emulators don't model those paths (see the gate below).
4. **Is there floating-point math in a hot loop?** No FPU on GBA → use fixed-point; avoid soft-float
   in inner loops (general homebrew; DS/3DS have more headroom, details differ).

Honest substitute pattern: when real decode is infeasible, offer **metadata + hex/text view** of the
file instead of a fake "preview." That keeps the tool truthful about the hardware.

## Feasible on GBA-class hardware

Proven or implemented on GBA flashcarts (EZ-Flash Omega/Omega DE, EverDrive GBA X5):

- **Read/parse/edit SD files** over a real filesystem (FAT/FAT32/exFAT via FatFs): list directories,
  open/read/write/stat/unlink/rename/mkdir, copy/move/delete, recursive walks. (GBA-specific cart I/O;
  the filesystem layer is general.)
- **View images** — BMP is implemented (uncompressed 8/16/24/32-bit, integer-downscaled to fit the
  240×160 screen). PNG (no-heap inflate, per-scanline), GIF first-frame (small LZW), and baseline
  JPEG (e.g. picojpeg, sub-second) are feasible as stretch goals. **Key technique:** decode and blit
  **one scanline/row at a time** to the framebuffer — never materialize a full RGBA frame in RAM.
- **File-metadata + hex/text view** — pure parsers that read a few hundred bytes and show structure.
  This is the honest "open" for media/document files you can't decode.
- **Gen-3 Pokémon save editing** — parse/decrypt/edit/bit-exact rewrite of `.sav` data (proven on
  Ruby/Sapphire and Emerald). Keep the parse/crypt core in portable C so it host-tests on a PC.
  (GBA save format; DS/3DS use different generations and containers — see cross-platform.)
- **RTC date/time read** — wall-clock date and time from the cartridge RTC (see next section).
- **Reboot to the flashcart loader** without a power cycle (proven on Omega DE: lands on the
  EZ-Flash kernel menu). (GBA-specific.)

## Infeasible on GBA-class hardware

Do not promise these as real playback/rendering — the CPU/RAM/codec budget isn't there. Offer the
metadata substitute instead:

| Want | Verdict | Why / honest substitute |
|---|---|---|
| **Real MP3 / AAC audio playback** | **INFEASIBLE** | ~16.78 MHz, no FPU — no real-time MP3 decode; and during any SD read the ROM (with your decode tables and IRQ handlers) is unmapped, so you cannot stream-decode from the card. → show **ID3/metadata + bitrate/duration**. Stretch: a short WAV/ADPCM clip loaded *wholly into RAM first*, then played with ROM mapped. |
| **MP4 / H.264 / MPEG video** | **INFEASIBLE** | Orders of magnitude short of real-time decode. (Commercial "GBA Video" carts used a custom codec baked into ROM, not media-on-SD.) → show **container/`moov`-atom metadata** (resolution/duration/codec), labelled "preview not supported." |
| **PDF page rendering** | **INFEASIBLE** | Needs object/xref parser + inflate + font rasterizer + graphics interpreter (+ embedded image codecs), MB-class working set, no "one page" shortcut. → show **trailer/Info metadata** (page count, title, encrypted/Flate flags) + hex/text. |

(All three are **GBA-specific** verdicts; DS has more RAM and DS/3DS have far more CPU, so some of
these become possible there — see cross-platform. The general lesson is universal: metadata is cheap,
real decode is not.)

There is also a hardware telemetry limit worth knowing: **the console battery is invisible to software
on GBA** — original GBA (2×AA) and the **SP / Micro (internal Li-ion)** alike. The ARM7TDMI has no ADC,
the documented I/O map has **no battery/voltage/charge register and no battery interrupt**, the
low-battery LED is a standalone **analog comparator**, and the Game Pak edge connector carries **no
battery line**, so a cartridge program can't sense the cell even in principle. There is also **no smart
fuel-gauge IC anywhere on the bus**, so it isn't just "battery level" — **charge %, voltage, cycle
count, design/current capacity, and manufacture date are all INFEASIBLE** on GBA. (The RTC backup
coin-cell exposes only a binary "power lost" flag — see below — never a voltage or capacity.)
(GBA-specific; this ask first becomes partly real on **3DS** — see cross-platform.)

## The cartridge RTC (date/time only)

GBA carts (and emulating flashcarts like the Omega DE) expose a **Seiko S-3511A** real-time clock —
the same chip Pokémon Ruby/Sapphire/Emerald use. It is reached by a bit-banged serial protocol over
three GPIO registers mapped in cartridge/ROM space. (GBA-specific hardware; DS/3DS have their own
system RTC reached very differently.)

What you get and what you don't:

- **Date + time**: year (2 BCD digits → century hard-coded to **2000–2099**), month, day, hour
  (12h/24h modes), minute, second. All fields are BCD; decode is `((v>>4)*10)+(v&0x0F)`.
- **Day-of-week** byte is clocked off the chip but typically read-and-ignored (no weekday field).
- **A "lost power / backup coin-cell dead" status bit** — the S-3511A status register's high bit
  (bit 7) flags that the RTC lost power since last set (the backup cell is dead/removed and the clock
  is no longer trustworthy). This is a **binary flag, not a battery level**.
- **No battery-level telemetry of any kind** — see infeasible note above.

Practical protocol facts (so you don't lose a day re-deriving them):

- **Write the control register (read-enable) before any access**, or reads come back 0.
- Pins: SCK on bit0, **SIO (data) on bit1** (shift by 1 when presenting/sampling — not bit0),
  CS on bit2. The direction register must flip SIO between **output (command, MSB-first)** and
  **input (reply, LSB-first)**.
- **The redundant identical data-register writes in the bit loops are intentional settling delays**
  for the slow serial clock — they're copied from hardware-proven code; do not "optimize" them away.
- 12/24h: a status bit selects the mode; in 12h mode bit7 of the hour byte is PM, and `12` maps to `0`
  before the AM/PM adjustment.
- **Backup-cell power-lost flag:** issue the **status-read command (`0x63`)** and test **bit 7** of the
  returned byte (`1` = the RTC lost power since it was last set → the coin cell is dead/removed and the
  clock is untrustworthy). An **all-ones `0xFF` status read = the RTC isn't driven** (open bus) → treat
  as "not exposed", the status-read analogue of the date/time range check. Keep the date/time read as
  the authoritative presence test, and trust the power-lost bit only when a date/time read also
  succeeds. (A 12-hour-mode, no-loss idle status can legitimately read `0x00`, so reject only `0xFF`,
  not `0x00`.)

**The "no fallback" design.** After decoding, **range-validate** every field
(`year 0..99, month 1..12, day 1..31, hour 0..23, minute 0..59, second 0..59`). If anything is
implausible, return "no RTC" rather than substituting a fake date — **the range check IS the presence
test; there is no separate detect call.** Downstream (e.g. FatFs `get_fattime()`) then writes a *zero*
(unset) timestamp instead of fabricating one, so files get **no** timestamp rather than a wrong one.

```c
/* Range check doubles as the presence test — reject implausible readings. */
if (y > 99 || mo < 1 || mo > 12 || d < 1 || d > 31 ||
    h > 23 || mi > 59 || se > 59)
    return false;            /* "no RTC exposed to this ROM" */
/* On success: year = 2000 + y; */
```

```c
/* FatFs timestamp hook returns 0 (unset) on RTC failure — never a fake date. */
DWORD get_fattime(void) {
    GbaRtcTime t;
    if (rtc_get(&t)) {
        return ((DWORD)(t.year - 1980) << 25) | ((DWORD)t.month << 21)
             | ((DWORD)t.day << 16) | ((DWORD)t.hour << 11)
             | ((DWORD)t.minute << 5) | ((DWORD)(t.second / 2)); /* 2s DOS resolution */
    }
    return 0;   /* unset timestamp, not a fabricated date */
}
```

**Caveat that bites in practice:** an emulating flashcart (Omega DE) only answers the GPIO RTC for
ROMs it *treats as RTC-enabled*. Plain homebrew may read garbage that the range check rejects, so your
file timestamps come out blank/zero. That's expected behavior, not a bug — but verify on **your** cart
whether the RTC is live before trusting any timestamp. (FatFs DOS datetime also has only **2-second
resolution**: it stores `second / 2`.)

## The hardware-validation gate

**The SD/flashcart path, the RTC, and destructive write/save ops are NOT emulated.** mGBA/melonDS
don't model the flashcart OS-mode register protocol, so SD reads/writes either no-op or fake-succeed
in emulation. **A green emulator build proves nothing about SD I/O, the RTC, or any write.**
(GBA-specific in the particulars; the principle — "the storage/clock/destructive path isn't in the
emulator" — applies broadly to homebrew on real cart/SD hardware.)

The rule: **any change touching the SD card, the RTC, or destructive file/save ops is "not done"
until it is validated on real hardware** — and first on **disposable copies**, never your only data.

Why it's load-bearing:

- **Writes have no retry on this hardware** — a single transient hiccup fails the write outright.
  Therefore **verify every write yourself**: write to a temp file, re-read it, **byte-compare**, and
  only then unlink + rename over the original. Take an immutable backup first. Never ship a bare write.
- **Debug-console logging is a no-op on hardware** — the mGBA debug-register handshake fails on a real
  cart, so any path that "logs" only to the emulator console is silent on silicon. Always also write to
  an on-screen buffer and/or an SD log file you can pull and read afterward.
- **During SD I/O the ROM window is unmapped** — every function/IRQ live during a transfer must run
  from work RAM, and you must not trigger a soft-reset or touch ROM mid-transfer.
- **RTC may simply be unexposed** to your ROM → timestamps come out 0 (see above). Confirm which state
  you're in on your cart.

Minimum bench protocol when the SD/RTC/write path is involved:

1. **Back up the microSD** (image it, or at least copy every file the tool touches) — that backup is
   your only undo.
2. Confirm the tool **detects the cart** at startup (don't assume).
3. Run the full flow on the **low-risk cart first** (e.g. Omega DE: no SRAM autosave), then on the
   trickier variants (the original Omega has an SRAM→SD autosave that can crash if you read the card
   too soon after a save; slower ROM wait-states may be required).
4. For **multi-MB copies**, validate sustained throughput and stability, not just one small write
   (every transfer cycles into OS mode and back, thousands of times).
5. **After the run**, pull the card, read the SD log, confirm timestamps, and **diff every written
   file against the pre-run backup** to prove the change is exactly what you intended and nothing else
   moved.
6. Treat read/browse as low-risk; ship every **write/edit** behind explicit "back up your card first"
   warnings.

## Cross-platform: GBA vs DS vs 3DS

The save/file/hardware *concepts* port across these handhelds; the SDKs, RAM budgets, and exact
formats differ. Tag your assumptions by platform.

| | **GBA** | **DS** | **3DS** |
|---|---|---|---|
| CPU | single ARM7TDMI (~16.78 MHz), **no FPU** | adds an ARM9; more clock | dual/quad ARM11 + more — far more headroom |
| Main work RAM | **256 KiB** (small; no swap/heap to lean on) | more RAM than GBA | much more RAM |
| Typical homebrew SDK | devkitARM + libtonc/libgba (general homebrew) | **libnds** (two screens, touch) | **libctru** |
| Pokémon saves | **Gen 3** (.sav, the format covered above) | **Gen 4–5** (different containers/encryption) | **Gen 6–7** (different again) |
| Math in hot loops | **fixed-point; avoid soft-float** (no FPU) | fixed-point still preferred; more headroom | hardware FP available; details differ |
| Battery telemetry | **none** — no ADC/registers/connector line | coarse **low-battery flag** only (verify in libnds; DSi exposes more) | **charge %, charging/adapter/shell state, voltage** via libctru **PTMU** + **MCU::HWC** |

Notes:

- **Pure-C cores port; platform glue doesn't.** Keep parse/crypt/file-op logic free of platform
  headers (only `<stdint.h>`/`<string.h>`, explicit little-endian helpers) so it host-tests on a PC and
  can be lifted toward a DS/3DS tool. The save-format *facts* differ by generation, but the
  clean-room/no-fake-data discipline carries over.
- **DS** adds two screens, a touchscreen, and more RAM — a richer UI and bigger working sets than GBA,
  with Gen 4–5 saves. (Applies to DS Pokémon too; format details differ.)
- **3DS** has dramatically more CPU/RAM, so some things that are infeasible on GBA (e.g. heavier image
  or even some media decode) move into reach there. Don't assume; confirm against the actual budget.
- **Battery telemetry is a platform divider.** A "read my battery" tool is **impossible on GBA** (no
  ADC, no battery line) and only coarse on **DS** (a low-battery indication from the power-management
  IC; DSi exposes a finer level — verify in libnds). It first becomes genuinely useful on **3DS**:
  libctru **PTMU** gives the battery **percentage** + **charging/adapter/shell** state, and **MCU::HWC**
  (`mcuHwcInit`) gives **raw %/voltage**. But the 3DS pack is still a "dumb" Li-ion cell with **no smart
  fuel-gauge IC**, so **cycle count, design/current capacity, and manufacture date stay unavailable**
  even there. So the honest scope grows with the platform but never reaches the full "smart-battery"
  ask on any of the three — confirm each field against the libctru headers before promising it.

**Calibration example — an impossible ask.** "Run two full GBA games on the DS's two screens at the
same time" is **hardware-impossible**: DS GBA-compatibility uses the single ARM7 in a dedicated GBA
mode to run *one* GBA title, not two emulators in parallel on two displays. When a request implies
two independent full-system contexts (or real-time decode that the silicon can't sustain), say so
plainly and offer the honest substitute rather than a fake.
