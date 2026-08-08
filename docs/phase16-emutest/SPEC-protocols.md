# SPEC-protocols — the exact protocol facts for the phase-16 emutest harness

Researched 2026-08-08. Target emulator: **Azahar 2125.1.2** (installed at
`~/Applications/Azahar.app`, binary `Contents/MacOS/azahar`; data dir
`~/Library/Application Support/Azahar/`). Azahar is open source (GPLv2+, a Citra continuation):
github.com/azahar-emu/azahar. All source citations below were read from **master @
`53946252536ceb055e89775952e6aec56452ae79`** (committed 2026-08-07) via
raw.githubusercontent.com — the installed release tag `2125.1.2` may lag master slightly; every
fact that could drift carries an empirical re-verification step (marked **VERIFY**). Facts from
the live machine (config file, `.map`) are marked **probed**. Everything else is **verified in
source** unless explicitly marked **inferred**.

Line numbers refer to the master files as fetched; they are stable enough to locate the code by
the quoted identifiers even after drift.

---

## S1 — The CTM movie format (byte-exact)

Source of truth: `src/core/movie.cpp` (structs are defined in the .cpp, NOT in `movie.h` —
`movie.h` only forward-declares them).

### S1.1 CTMHeader — 256 bytes, `#pragma pack(1)`, all fields little-endian

`movie.cpp:114-129` (`struct CTMHeader`, `static_assert(sizeof(CTMHeader) == 256)`):

| Offset | Size | Field | Notes |
|---|---|---|---|
| 0x00 | 4 | `filetype` | magic `{'C','T','M',0x1B}` = bytes `43 54 4D 1B` (`movie.cpp:112`) |
| 0x04 | 8 | `program_id` u64_le | "ID of the ROM being executed. Also called title_id" — **0 for a .3dsx** (S1.10) |
| 0x0C | 20 | `revision` | raw bytes of the 40-hex-char git hash of the *recording* build (`Common::g_scm_rev`, `movie.cpp:507-510`) |
| 0x20 | 8 | `clock_init_time` u64_le | system-clock init time, seconds since 1970 (`movie.cpp:606-617`) |
| 0x28 | 8 | `id` u64_le | random per-movie id; only used to pair savestates with movies (`movie.cpp:119-120,564-566`); any fixed nonzero value is fine for playback |
| 0x30 | 32 | `author` char[32] | not NUL-terminated on disk; reader appends one (`movie.cpp:536-538`) |
| 0x50 | 4 | `rerecord_count` u32_le | cosmetic |
| 0x54 | 8 | `input_count` u64_le | number of **PadAndCircle** records only, not total records (`GetInputCount`, `movie.cpp:131-145`) |
| 0x5C | 8 | `timing_base_ticks` s64_le | base system tick count for core timing; any value ≥ 0 (S1.8) |
| 0x64 | 156 | `reserved` | zeros, pads header to 256 |

There is **no version field**; format identity = magic + revision hash.

### S1.2 ControllerState — 7 bytes per record, `#pragma pack(1)`

`movie.cpp:30-110`: byte 0 = `ControllerStateType` (u8): `0=PadAndCircle, 1=Touch,
2=Accelerometer, 3=Gyroscope, 4=IrRst, 5=ExtraHidResponse`; bytes 1-6 = a 6-byte union
(largest member `pad_and_circle`), `static_assert(sizeof == 7)`.

- **PadAndCircle** (type 0): `u16_le hex` button bits, `s16_le circle_pad_x`, `s16_le circle_pad_y`.
- **Touch** (type 1): `u16_le x`, `u16_le y`, `u8 valid` (bool), 1 trailing pad byte (write 0).
- **Accelerometer** (2) / **Gyroscope** (3): `s16_le x,y,z`.
- **IrRst** (4): `s16_le x,y` (c-stick), `u8 zl`, `u8 zr`.
- **ExtraHidResponse** (5): u32_le bitfield (battery_level 0-4, zl_not_held 5, zr_not_held 6,
  r_not_held 7, c_stick_x 8-19, c_stick_y 20-31).

### S1.3 Button bit order in `pad_and_circle.hex` (`movie.cpp:48-62`)

bit 0=A, 1=B, 2=Select, 3=Start, 4=Right, 5=Left, 6=Up, 7=Down, 8=R, 9=L, 10=X, 11=Y,
12=Debug, 13=Gpio14; bits 14-15 unused. (Same order as HID `PadState`; ZL/ZR are NOT here —
they are IrRst-service territory.)

Circle pad range: the HID pad polls write `s16` values; live-input path produces roughly
−0x9C..0x9C (libctru convention); 0 = centered. For button-only tests keep 0.

### S1.4 Touch coordinate units — native bottom-screen pixels

`src/core/hle/service/hid/hid.cpp:285-296` (local-input path of `Module::UpdatePadCallback`):
`touch_entry.x = u16(normalized_x * Core::kScreenBottomWidth)`, `.y = u16(normalized_y *
Core::kScreenBottomHeight)`, then `system.Movie().HandleTouchStatus(touch_entry)` records
exactly those values. `kScreenBottomWidth/Height = 320/240` (`src/core/3ds.h:18-19`). So CTM
touch x ∈ 0..320, y ∈ 0..240 — **native bottom-screen coordinates**, not window pixels; use
0..319/0..239 to stay in range. `valid=1` while pressed; a release is a record with `valid=0`.
Playback assigns them straight back into the HID touch entry (`movie.cpp:269-284`).

### S1.5 Stream composition and rates — records come in [Pad][Touch] pairs at 234 Hz

- The HID pad poll runs every `pad_update_ticks = BASE_CLOCK_RATE_ARM11 / 234` cycles
  (`hid.h:342`, `BASE_CLOCK_RATE_ARM11 = 268111856`, `core_timing.h:37`) → **234 polls/s**.
- Each poll handles **PadAndCircle first, then Touch** — both in the same callback
  (`hid.cpp:144-320`; movie handle calls at lines 163→203 in the artic path and 243→296 in the
  local path). During playback the same call order consumes the stream, so records MUST be
  written as strictly alternating `[PadAndCircle][Touch]` pairs; a missing Touch record makes
  `Play()` log `Expected to read type …` and go out of sync (`movie.cpp:243-247,274-278`).
- Accelerometer (104 Hz) and Gyroscope (101 Hz) records are interleaved **only while the game
  has enabled those sensors** (`hid.cpp:340-372,392-416,451,512`); IrRst only if the game
  activates the `ir:rst` service; ExtraHidResponse only for Circle Pad Pro via `ir:user`.
  3DGBA's `source/` has no `irrstInit`/accelerometer/gyroscope calls (probed via grep), so its
  stream should be pure pad+touch pairs — **VERIFY** with the S1.10 recording probe before
  trusting synthesized movies for long runs.
- Frame↔poll conversion (for humans): the UI's "input index" = `round(input / 234.0 *
  SCREEN_REFRESH_RATE)` (`movie.cpp:222-227`), `SCREEN_REFRESH_RATE = 268111856/4481136 ≈
  59.83122 Hz` (`core_timing.h:37,42` — CORRECTED 2026-08-09 from the earlier 59.83400 /
  3.9108, which were wrong in the 4th significant digit; ctm.py carries the exact
  Fraction, so no synthesized bytes were ever affected) → **= 3.911001 polls per frame**.
  The PokeDNA "hold 8 frames, release 8 frames" rule ≈ hold 32 polls, release 32 polls.

### S1.6 Playback consumption and termination

- File = 256-byte header + N×7-byte records; `StartPlayback` reads everything after the header
  into `recorded_input` (`movie.cpp:543-544`).
- Records are consumed sequentially by the HID callbacks; after each record
  `CheckInputEnd()` sets `PlayMode::MovieFinished` when `current_byte + 7 >
  recorded_input.size()` and fires the completion callback (`movie.cpp:229-235,680-689`).
  Playback end therefore comes from **byte exhaustion, not `input_count`**. After the movie
  finishes, live input takes over; the emulator keeps running (Qt shows a status message via
  `OnMoviePlaybackCompleted`, `citra_qt.cpp:437-439` — non-modal).
- Trailing partial record bytes (<7) are ignored.

### S1.7 Validation — what can reject a synthesized movie

`ValidateHeader` (`movie.cpp:463-478`): magic mismatch → `Invalid` (rejected); revision ≠ this
build's `g_scm_rev` → **warning only** ("This movie was created on a different version of
Citra, playback may desync"), still plays. **`program_id` is never checked on playback.**
`input_count` is checked only by `ValidateMovie` — which the CLI path never calls (S1.9); it
gates only the GUI's MoviePlayDialog. `StartPlayback` rejects only `Invalid`
(`movie.cpp:529-532`).

### S1.8 Clock/tick overrides — the movie header is auto-honoured

`BootGame` calls `movie.PrepareForPlayback(path)` before the system boots
(`citra_qt.cpp:1494-1499`), which loads `init_time = header.clock_init_time` and `base_ticks =
header.timing_base_ticks` (`movie.cpp:597-604`). `System::Init` then constructs `Timing` with
`movie.GetOverrideBaseTicks()` and the kernel with `movie.GetOverrideInitTime()`
(`core.cpp:513-519`; override applies when `base_ticks >= 0`, `core_timing.cpp:24-27`). So the
INI `init_clock`/`init_time` are **overridden by the movie during playback** — determinism
comes free; put the pinned epoch into the header. Set `timing_base_ticks` to a fixed value ≥ 0
(recording uses `GenerateBaseTicks()`: random unless `init_ticks_type=1 (Fixed)`,
`core_timing.cpp:37-44`).

### S1.9 CLI flow — no dialogs, no validation

`citra_qt.cpp:278-390` parses `-d/--dump-video <f>`, `-g/--gdbport <n>`, `-p/--movie-play <f>`,
`-r/--movie-record <f>`, `-a/--movie-record-author <s>`, `-w/--windowed`, plus the positional
app path (all probed on this machine too). On boot with `-p`: `PrepareForPlayback` →
`StartPlayback` (`citra_qt.cpp:1552-1555`) — **no `ValidateMovie`, no QMessageBox**; a bad
magic just logs `Movie … does not have valid header` to
`~/Library/Application Support/Azahar/log/azahar_log.txt`. Grep the log for
`Loaded Movie, ID:` (success) and `Playback finished` (completion) — both from
`movie.cpp:551,231`.

### S1.10 program_id for a .3dsx, and the recording bootstrap

- The 3DSX loader creates its CodeSet with `CreateCodeSet("", 0)` → `program_id = 0`
  (`src/core/loader/3dsx.cpp:228`), and `AppLoader_THREEDSX` does **not** override
  `ReadProgramId` (no such method in `3dsx.cpp`; base returns `ErrorNotImplemented` leaving the
  out-param untouched, `loader.h:219-221`). `StartRecording` initializes `program_id = 0` then
  calls `ReadProgramId` (`movie.cpp:569-570`) → **header program_id = 0**. Write 0 when
  synthesizing; nothing ever compares it on playback (S1.7).
- **Bootstrap step (do once per Azahar build):** record a real movie —
  `azahar -w -r /tmp/probe.ctm -a probe <app.3dsx>`, press a few buttons, quit **cleanly**
  (recordings are written only by `SaveMovie` at shutdown/close-movie, `movie.cpp:665-668`;
  `kill -9` loses the file). Then read from `/tmp/probe.ctm`: (a) the 20 `revision` bytes to
  stamp into synthesized headers (kills the desync warning and pins the format against
  release-vs-master drift), (b) the record-type composition (S1.5 verify), (c) a golden
  reference for the harness's parser tests.

### S1.11 Golden example — 3-frame movie, START pressed on frame 2 (424 bytes)

12 polls ≈ 3.068 frames (12 / 3.911001). Polls 0-3 = frame 1 idle, polls 4-7 = frame 2 with
START held (`hex = 0x0008`, bit 3), polls 8-11 = frame 3 idle. Layout per S1.5: 12 ×
`[PadAndCircle][Touch]` = 24 records = 168 bytes after the 256-byte header.

Header (hex, offsets per S1.1) — revision zeroed (desync-warning variant; substitute the
harvested bytes in production), `clock_init_time` = 946684800 (2000-01-01T00:00:00Z),
`id` = 0x0123456789ABCDEF, author `"3dgba-emutest"`, rerecord 1, input_count 12 (pad records
only!), timing_base_ticks 1000:

```
0x000: 43 54 4D 1B 00 00 00 00  00 00 00 00 00 00 00 00   CTM. + program_id=0
0x010: 00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00   revision[20] (zeros)
0x020: 80 43 6D 38 00 00 00 00  EF CD AB 89 67 45 23 01   clock_init_time, id
0x030: 33 64 67 62 61 2D 65 6D  75 74 65 73 74 00 00 00   "3dgba-emutest"
0x040: 00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00   author cont.
0x050: 01 00 00 00 0C 00 00 00  00 00 00 00 E8 03 00 00   rerecord=1, input_count=12, ticks
0x060: 00 00 00 00 00 00 00 00  ...                       ticks hi bytes + reserved[156]=0
                                                          (all zero through 0x0FF)
```

Body, starting at 0x100 — three distinct 14-byte poll pairs:

```
idle pair : 00 00 00 00 00 00 00   01 00 00 00 00 00 00
            └type=Pad, hex=0x0000, cpx=0, cpy=0┘ └type=Touch, x=0,y=0,valid=0,pad┘
START pair: 00 08 00 00 00 00 00   01 00 00 00 00 00 00
            └type=Pad, hex=0x0008 (bit3=Start)┘
touch pair (reference, tap at native 160,120):
            00 00 00 00 00 00 00   01 A0 00 78 00 01 00
                                   └x=0x00A0=160, y=0x0078=120, valid=1┘
```

File = header + 4×idle + 4×START + 4×idle pairs. Note: this proves the FORMAT; a behavioral
smoke test must pad the front with enough idle polls to cover app boot (seconds), and hold
buttons ≥ 32 polls (S1.5).

### S1.12 Recording-side INI interaction

When **recording**, `clock_init_time` is computed from `init_clock`/`init_time`/
`init_time_offset` settings (`movie.cpp:606-617`) and `timing_base_ticks` from
`init_ticks_type`/`init_ticks_override` (`core_timing.cpp:37-44`) — set the S3 determinism
profile before the bootstrap recording so the harvested header is already deterministic.

---

## S2 — The GDB stub

Source of truth: `src/core/gdbstub/gdbstub.cpp` (Azahar's stub is heavily reworked vs vanilla
Citra: per-process attach via kernel thread unscheduling, extended mode, no single-step).

### S2.1 Enablement, port, and the two launch modes

- INI keys `[Debugging] use_gdbstub`, `gdbstub_port` (default 24689; live file lines 215-225,
  probed; `gdbstub.cpp:162`). Applied by `System::ApplySettings` → `GDBStub::ToggleServer`
  (`core.cpp:763-770`).
- CLI `-g <port>` overrides the port AND sets `SetDebugNextProcessFlag()`
  (`citra_qt.cpp:1525-1527`), which makes the app process **pause at its first instruction**:
  `Process::Run` sets `UnscheduleMode::GDB` on the new process when the flag + server are on
  (`process.cpp:266-277`, log line `Pausing process {} at start`). Resume requires a gdb client
  (S2.3).
- INI-only route (`use_gdbstub=true`, no `-g`): server listens, the app **runs freely from
  boot**; attach any time. This is the harness's default mode; use `-g` only when you need to
  catch code before/at boot.
- Server socket: TCP, `INADDR_ANY`, single client (`listen(_,1)`), accept is non-blocking and
  polled (`gdbstub.cpp:1658-1705`). Packets are processed on the emulator's main loop —
  `System::RunLoop` calls `GDBStub::HandlePacket` once per loop iteration (`core.cpp:89-97`),
  so responses arrive between emulation slices (latency ~ms, fine for scripting; don't expect
  µs turnaround).

### S2.2 Attach model — connecting does NOT halt; the first `?` does

- TCP connect alone changes nothing (`HandlePacket` just accepts, `gdbstub.cpp:1496-1524`).
- **Non-extended mode** (what a default client does): the first `?` (stop-reason) packet finds
  the process whose `codeset->program_id` equals the app loader's program id — for a .3dsx both
  are 0 (S1.10) — sets `UnscheduleMode::GDB` on it (halting **only that process's threads**;
  the emulator, GUI, and any other process keep running), selects the lowest-id thread (= main
  thread) and replies `T00…` (`HandleGetStopReason`, `gdbstub.cpp:861-895`; `SetThread`
   746-768). **Inferred:** with HLE services a .3dsx boot has the app as the only
  program_id-0 process, so the match is unambiguous — **VERIFY** once via
  `qXfer:osdata:read:processes` (lists `pid` + codeset name; the 3dsx codeset name = the
  filename, `3dsx.cpp:289`, `gdbstub.cpp:711-724`).
- **Extended mode** (deterministic alternative): send `!` (`gdbstub.cpp:807-814`), then
  `vAttach;<pid-hex>` (`gdbstub.cpp:1404-1426`) — halts exactly that pid.
- Minimal read cycle: connect → `?` (halts app) → `m`-reads → `D` (detach: replies OK,
  restarts the listener for the next client, resumes execution — `gdbstub.cpp:1571-1578`).
  The app is frozen for the duration between `?` and `D`; keep it short during timing-sensitive
  runs (frame pacing across the freeze is disturbed — acceptable for state peeks, not during
  link-timing measurements).
- **Quirk:** never send the interrupt byte `0x03` before a `?`/`vAttach` has set
  `current_process` — `BreakImpl` dereferences it unguarded (`gdbstub.cpp:897-909,920-923` —
  null-deref crash risk).

### S2.3 Packet dialect (deviations from standard RSP that matter)

- `qSupported` → `PacketSize=9800;qXfer:features:read+;qXfer:osdata:read+;qXfer:threads:read+;vContSupported+`
  (`gdbstub.cpp:688-691`). `qXfer:features:read:target.xml` serves an `arm` +
  `org.gnu.gdb.arm.vfp` description; **CPSR is regnum 25** (historical FPA gap), d0-d15 =
  26-41, fpscr 42, fpexc 43 (`gdbstub.cpp:87-146`).
- Memory read `m<addr-hex>,<len-hex>`: hex-string reply. Requires `current_process` (else empty
  reply). Address validated only at the **start** address (`IsValidVirtualAddress`); invalid →
  **empty reply `$#00`, not an E-code** (`gdbstub.cpp:1136-1173`). Reply buffer is
  `GDB_BUFFER_SIZE-4 = 9996` hex chars, and the over-length branch is missing a `return`
  (`gdbstub.cpp:1154-1156`) — keep single reads ≤ 4096 bytes.
- Memory write `M<addr>,<len>:<hex>` → `OK` / `E0E` (invalid addr) / `E01` (no process); flushes
  the JIT instruction cache after every write (`gdbstub.cpp:1176-1201`).
- Continue: `c` (or `vCont;c`); `C<sig>` accepted. **`s` (single-step) is NOT supported** —
  returns `E5F` (`gdbstub.cpp:1608-1611`). Breakpoints: `Z0/z0` (execute, forced len 1),
  `Z2`(write)/`Z3`(read)/`Z4`(access) watchpoints; JIT needs `use_cpu_jit=false` for accurate
  watchpoints (`gdbstub.cpp:897-901`).
- Threads: `qfThreadInfo` → `m<tid>,<tid>,…` (real kernel thread ids of the attached process);
  `Hg<tid>` selects for `g`/`p` register reads; `Hc-1`/`Hc0` for continue scope. `D` detach:
  OK + full server restart + resume (S2.2). `k`: **shuts down the whole emulator**
  (`system.RequestShutdown()`, `gdbstub.cpp:1579-1586`) — usable as a clean programmatic quit.
- `0x03` (interrupt byte, only after attach): halts and stop-replies `T05` (SIGTRAP).
- Checksums are standard RSP (`$<data>#<2-hex>`, `+`/`-` acks).

### S2.4 Halt scope and coexistence

Halting = `Process::SetUnscheduleMode(GDB)` unschedules only the target process's threads
(`process.cpp:627-641`); rendering, audio, the Qt loop, and the gdb polling all continue. So
attach→read→detach around a *paused menu screen* is effectively invisible to the app, and even
mid-gameplay it's a clean freeze/resume (the app's own frame pacing hiccups once).
`Continue`/`D` with no specific thread resumes with an empty thread list = all threads
(**inferred** from `ClearUnscheduleMode(GDB, {})`, `gdbstub.cpp:1571-1577,1229-1255` —
VERIFY in smoke: app must visibly resume after detach).

### S2.5 The devkitARM `.map` — symbol source

- Regenerate with `export DEVKITPRO=/opt/devkitpro DEVKITARM=$DEVKITPRO/devkitARM && make -j8`;
  the map lands at **`build/3DGBA.map`** (probed — NOT at the repo root; the root `3DGBA.lst`
  is stale).
- Format: GNU ld map. Output sections (probed from the current build, 2026-08-04):
  `.text 0x00100000 0x1347d0`, `.rodata 0x00235000`, `.data 0x0049b000`, `.bss 0x0049d2e4`,
  and `PROVIDE (__start__ = 0x100000)`. Symbol lines match
  `^\s+0x00[0-9a-f]{6}\s+<name>$` under their section — a 20-line Python parse.

### S2.6 Map addresses == gdb addresses (offset ZERO), and why

Azahar loads a .3dsx at `Memory::PROCESS_IMAGE_VADDR = 0x00100000` (`3dsx.cpp:287`,
`memory.h:192`), placing rodata at `base + align(code_size, 0x1000)` and data after rodata
likewise (`Load3DSXFile`, `3dsx.cpp:113-132`). devkitARM's 3dsx linker script links at the
same base with the same 0x1000 section alignment — probed: map rodata `0x235000` = `0x100000 +
align(0x1347d0)`, data `0x49b000` = `0x235000 + align(0x2651e8)` — so **the .map virtual
addresses are exactly what the gdb stub sees**. `.bss` is inside the data segment
(`data_seg_size` includes bss; the loader zero-fills it, `3dsx.cpp:150-155`), so bss globals
are readable too.

**Empirical offset check (do in smoke):** pick a symbol, get ground truth from the ELF —
`arm-none-eabi-objdump -s -j .data --start-address=0x49b49c --stop-address=0x49b4bc 3DGBA.elf`
— and compare with the gdb `m49b49c,20` read. Bytes equal ⇒ offset 0 confirmed for this build.

### S2.7 Concrete smoke-test symbols (addresses from the CURRENT build's map — re-parse per build)

| Symbol | Addr (2026-08-04 build) | Section | What a read proves |
|---|---|---|---|
| `g_prefs` | 0x0049b49c | .data | `UiPrefs` = 8×int32_le, compile-time init `{0,205,168,14,0,0,0,0}` (`source/theme.c:21`) → expect `00000000 CD000000 A8000000 0E000000 …` at boot (caveat: `settings_load` overwrites from sdmc if a settings file exists — pristine only on a fresh SD profile); also the **live theme/gameMode/tiltLevel** state |
| `g_diagNetCrumb` | 0x004a26a0 | .bss | phase-13 D1 netlink breadcrumb word (site 1000-1499 + iter, decode `site = v - v%100`, `source/diag.h:69-98`) |
| `g_diagSioCrumb` | 0x004a2680 | .bss | gbacore driver-gate crumb (sites 2000-3299) — the run-#4 wedge class, live |
| `g_diagMainCrumb` | 0x004a2660 | .bss | render-thread blocker crumb (sites 4000-4299) |
| `g_diagRxSeq` | 0x004a2640 | .bss | free-running RX-thread heartbeat (~2 kHz when a link session is up) — read twice 100 ms apart: advancing ⇒ radio thread alive (`diag.h:101-106`) |
| `g_ctlStat` | 0x004a2600 | .bss | D4/D5 control-channel counters per seat (`source/control.h:176`) — cross-checks the sdmc `# control` mirror |

All four diag words are volatile u32 at 0x20-byte cache-line spacing (`diag.h:91-98`) — single
4-byte reads are coherent. `g_renderSeq` is `static` in main.c (not in the map's global list);
use `g_diagRxSeq` for liveness instead.

---

## S3 — The config profile (`qt-config.ini`)

Live file (probed): `~/Library/Application Support/Azahar/config/qt-config.ini`. Sections
present: `[Audio] [Camera] [Controls] [Core] [Data%20Storage] [Debugging] [Layout]
[Miscellaneous] [Renderer] [System] [UI] [Utility] [VideoDumping] [WebService]`.

### S3.1 THE critical write rule — `key\default` companion lines

`QtConfig::ReadSetting` (`src/citra_qt/configuration/config.cpp:1464-1472`): **if
`key/default` is `true`, the stored value is IGNORED and the built-in default used.** Every
key the harness sets to a non-default value MUST also set `key\default=false`, or the change
silently does nothing. (Values that happen to equal the default may leave `\default=true`.)

### S3.2 Timing of edits

Azahar reads the INI at launch and **saves it on boot and exit** (`config->Save()` in
`BootGame`, `citra_qt.cpp:1543`) — edit only while Azahar is not running; restore only after
it has exited (else your restore is overwritten).

### S3.3 The determinism/automation profile (each key probed in the live file)

| Section | Key | Harness value | Why / source |
|---|---|---|---|
| [Layout] | `layout_option` | `0` | `LayoutOption::Default` (enum order `settings.h:39-50`) → the S4 math |
| [Layout] | `screen_gap` | `0` (default) | part of the S4 math (`framebuffer_layout.cpp:162`) |
| [Layout] | `swap_screen` / `upright_screen` | `false` | S4 assumes unswapped, rotated-landscape |
| [Layout] | `screen_top_stretch` / `screen_bottom_stretch` | `false` | stretch replaces the S4 rects (`framebuffer_layout.cpp:122-132` analog in LargeFrameLayout path) |
| [Layout] | `aspect_ratio` | `0` | `AspectRatio::Default` (`settings.h:589`) |
| [Layout] | `use_integer_scaling` | `false` | pins the MaxRectangle branch (`settings.h:552`) |
| [Layout] | `render_3d` | `0` | stereo off — the side-by-side touch remap must not trigger (`emu_window.cpp:158-176`) |
| [Layout] | `factor_3d` | `0` | 3D slider off |
| [System] | `init_clock` | `1` | `InitClock::FixedTime` (`settings.h:28-31`) — RTC pinning |
| [System] | `init_time` | fixed, e.g. `946684800` | seconds since 1970; live default 946681277 |
| [System] | `init_ticks_type` | `1` | `InitTicks::Fixed` — kills tick-seeded randomness (`core_timing.cpp:37-44`) |
| [System] | `init_ticks_override` | fixed, e.g. `1000` | ditto |
| [Debugging] | `use_gdbstub` | `true` | S2.1 INI route (no pause-at-start) |
| [Debugging] | `gdbstub_port` | `24689` | default; make it explicit |
| [Miscellaneous] | `check_for_update_on_start` | `false` | the update dialog hijacked a run (phase-16 invariant 3) |
| [UI] | `confirmClose` | `false` | the close-confirm QMessageBox is the SIGTERM-swallower class |
| [UI] | `fullscreen` | `false` | windowed |
| [UI] | `showStatusBar` | `false` | removes the status bar from the client area → cleaner S4 crops |
| [UI] | `Paths\screenshotPath` | harness dir | used only if the GUI screenshot action is ever driven |
| [Renderer] | `graphics_api`, `use_vsync`, `frame_limit` | leave defaults (`2`,`true`,`100`) | stability > tuning; do not churn the backend |
| [Core] | `cpu_clock_percentage` | `100` (default) | timing sanity |

Do NOT attempt to pin the window size via `UILayout\geometry` / `geometryRenderWindow` — they
are opaque Qt `saveGeometry()` QByteArray blobs (versioned, screen-dependent); measure the
window at runtime instead (S4.5). The custom_layout keys are irrelevant while
`layout_option=0`.

### S3.4 Backup/restore discipline (phase-16 invariant 2)

Before first apply: copy `qt-config.ini` → `qt-config.ini.emutest-backup` (create-once,
never overwrite an existing backup). Apply = full-file rewrite of only the keys above (+ their
`\default=false`). Teardown = restore the backup after Azahar exits and delete only files the
harness created. The log file `log/azahar_log.txt` rotates to `.old` per launch (probed) —
copy, don't move, when banking evidence.

### S3.5 Dialogs & modality inventory (things that can wedge a headless run)

1. macOS "running from terminal" warning: `AppleUtils::IsRunningFromTerminal()` triggers a
   modal QMessageBox at startup (`citra_qt.cpp:492-501`) — Bash-tool launches have no TTY so it
   has not fired in probes, but launching from an interactive shell WILL wedge; prefer
   detached/no-tty launches. 2. update-check dialog (disabled by profile). 3. close-confirm
   (disabled by profile). 4. `.cia` positional arg → "must be installed" question
   (`citra_qt.cpp:1475-1483`) — never pass the `.cia`, always the `.3dsx`. 5. A crashed
   previous instance can leave a "crash detected" style prompt — the pkill + fresh-launch
   discipline (env-probed: `pkill -x azahar`, then `kill -9` survivors) stays mandatory.

---

## S4 — Window geometry math (layout_option=0)

Source of truth: `src/core/frontend/framebuffer_layout.cpp`. With `layout_option=0`,
`EmuWindow::UpdateCurrentFramebufferLayout(width,height)` → `DefaultFrameLayout(w,h,swapped,
upright)` → `LargeFrameLayout(w, h, false, false, scale_factor=1.0, SmallScreenPosition::
BelowLarge)` (`framebuffer_layout.cpp:57-60`; dispatch in `emu_window.cpp:211-228` and
`framebuffer_layout.cpp:521-532`). `width`/`height` = the **render window's client area in
pixels** (Retina: Qt passes device pixels — see S4.5 calibration).

### S4.1 The exact algorithm (unswapped, not upright, gap=0, no integer scaling, stretch off)

From `LargeFrameLayout` (`framebuffer_layout.cpp:149-269`), with large=top 400×240,
small=bottom 320×240, vertical stacking:

```
emu_w = 400 ; emu_h = 240 + 240 + gap = 480            # gap = screen_gap = 0
scale  = min(W / 400, H / 480)                          # float; MaxRectangle L45-47
tw, th = int(400*scale), int(480*scale)                 # truncation, L51-54
ox, oy = (W - tw)//2, (H - th)//2                       # centering, L186-187
sa     = th / 480.0                                     # scale_amount, L189
top    = (ox,               oy,               ox+int(400*sa), oy+int(240*sa))   # L192-195
bh, bw = int(240*sa), int(320*sa)
bx     = ox + int(400*sa)//2 - int(320*sa)//2           # BelowLarge centering, L253-256
by     = oy + (top.bottom - top.top)                    # shifted below the top screen
bottom = (bx, by, bx+bw, by+bh)
```

Letterboxing rule: the whole 400:480 emulation box is aspect-fit and centered in the window;
the 320-wide bottom screen is then centered under the 400-wide top screen (left margin
≈ 40·sa). All arithmetic on u32 rects — replicate the truncations exactly when cropping.

### S4.2 Worked examples

- W×H = 400×480 (1:1): top = (0,0)-(400,240); bottom = (40,240)-(360,480).
- W×H = 800×960 (2x): top = (0,0)-(800,480); bottom = (80,480)-(720,960).
- W×H = 600×800: scale=1.5, box 600×720 at oy=40 → top = (0,40)-(600,400);
  bottom = (60,400)-(540,760).

### S4.3 Screen crops for see.py

`top_screen` rect ↔ the 400×240 top screen, `bottom_screen` rect ↔ the 320×240 bottom screen
(`framebuffer_layout.cpp:262-263`). GBA content inside each screen is the app's own
letterboxing (240×160 source scaled by the app) — crop the screen rect first, then apply
3DGBA's known in-screen placement.

### S4.4 Touch: window pixel → native coordinate (for cliclick), and inverse

`EmuWindow::TouchPressed` (`emu_window.cpp:155-191`): with `render_3d=0` and the normal
landscape layout (`is_rotated=true` ⇒ no axis swap):

```
norm_x = (px - bottom.left) / (bottom.right - bottom.left)
norm_y = (py - bottom.top)  / (bottom.bottom - bottom.top)
native = (norm_x * 320, norm_y * 240)                    # hid.cpp:292-293
```

Inverse (harness → cliclick): `px = bottom.left + tx/320 * bottom_w`,
`py = bottom.top + ty/240 * bottom_h`, then add the window's client-area origin in screen
coords. Clicks outside `bottom_screen` are ignored (`IsWithinTouchscreen`,
`emu_window.cpp:156`). CTM touch records skip ALL of this — they inject native coordinates
directly (S1.4) and are the preferred touch channel.

### S4.5 Client-area calibration (macOS, empirical — needs Screen Recording anyway)

The Quartz window bounds include the title bar (~28 pt); the menu bar is global (not in the
window); the status bar is removed by the S3 profile; Retina scale (pt→px, ×2) applies to
`screencapture` output. Calibrate once per session instead of hardcoding: screenshot the
window, find the emulation box (non-black bounding region or a known splash), and solve S4.1
backwards for W,H,ox,oy. Until the permission is granted this whole channel **SKIPs loudly**
(phase-16 invariant 4).

### S4.6 Minimum sizes

`UpdateCurrentFramebufferLayout` clamps the window to the layout minimum
(`GetMinimumSizeFromLayout`, `emu_window.cpp:222-228`) — for Default that is 400×480; never
request smaller.

---

## Open questions (could not be pinned from source)

1. **Process-list composition at attach time** — whether any HLE-boot .3dsx session ever has a
   second program_id-0 process that could steal the non-extended `?` attach (S2.2). Resolve
   once with `qXfer:osdata:read:processes`; fall back to `!` + `vAttach` if ambiguous.
2. **Record-type composition of 3DGBA's own HID stream** — expected pure [Pad][Touch] pairs
   (no irrst/motion init found in source/), but ZR *is* read in the app's menus and the ir:rst
   activation path is service-driven; the S1.10 recording probe settles it byte-exactly.
3. **`D` (detach) resume scope** — empty thread list ⇒ all threads is inferred from the code
   shape (S2.4); smoke must confirm the app visibly resumes after detach.
4. **Release 2125.1.2 vs master drift** — all citations are master@5394625; the movie struct
   layout, gdb dialect, and layout math are old, stable Citra inheritance, but the bootstrap
   recording (S1.10) + one attach/read/detach smoke are the drift detectors. If the recorded
   header's revision differs from any future re-probe, re-run this research against the tag.
5. **Retina/device-pixel factor of the render-window client area** (S4.5) — whether Qt hands
   `UpdateCurrentFramebufferLayout` logical or device pixels on this Mac decides a ×2 in the
   crop math; calibration resolves it without needing the answer a priori.
6. **`azahar_log.txt` verbosity of Movie/GDB channels at default log filter** — the S1.9/S2.1
   grep anchors assume INFO-level lines are emitted; if the default filter hides them, add
   `log_filter` (`[Miscellaneous]`) to the S3 profile.
