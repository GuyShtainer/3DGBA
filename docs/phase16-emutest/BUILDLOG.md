# Phase 16 BUILDLOG — emulator self-test harness

One dated entry per slice (PHASE.md deliverables; SPEC-harness H6.4: every entry records the
smoke table verbatim).

## 2026-08-08 — E1: azctl.py + setup.sh (launch/stop/status, profile state machine, run dirs)

**Files** (all new unless noted):
- `tools/emutest/azctl.py` — boot/stop/status/restore; profile backup→apply→restore state
  machine (CLEAN/APPLIED); crash recovery; foreign-instance abort + `--force`; per-run dirs
  with `events.log`/`cmdline.txt`/`profile.diff`/`boot.json`; harvest of azahar logs +
  new netlogs; `assert_sd_writable` guard; `--wipe-netlogs` / `--fresh-sd-fixtures` (H4).
- `tools/emutest/run` — venv shim (H2).
- `tools/emutest/setup.sh` — venv build (pinned `pillow==11.3.0`,
  `pyobjc-framework-Quartz==11.1` on `/usr/bin/python3` 3.9.6; NO numpy per H1.2), gitignore
  hunk check, host tests, smoke gate; `--no-live`.
- `tools/emutest/smoke.sh` — E1 PLACEHOLDER (E4 replaces wholesale): preflight + live
  boot→status→stop cycle with byte-identical-restore check; unimplemented channels print TODO.
- `tools/emutest/tests/run_host_tests.sh`, `tests/test_profile.py` — 10 host-pure tests
  (INI editor incl. `\default` rule + prefix-key disambiguation + byte-identical untouched
  lines + idempotency; backup/restore/crash-recovery; sd_guard allow/deny).
- `.gitignore` — the H1.4 hunk was already present in the working tree (pre-added with the
  spec commit prep); setup.sh verifies/appends it idempotently.

**LIVE-verified on this machine (Azahar 2125.1.2, real 3DGBA.3dsx, 6 boots total):**
- Profile apply → boot → restore, 4 full cycles + smoke's: user `qt-config.ini`
  (sha256 `dd20792e…3286`, 28545 B) backed up, harness profile applied (20 pins +
  screenshotPath), and after every `stop` the file is **byte-identical restored** (sha
  re-verified after each of the 4 cycles; smoke does `cmp` against a pre-run snapshot).
- Boot readiness observables: `open -a` launch → pgrep pid **0.0–0.1 s**, log rotated
  **0.6 s**, **gdb stub LISTEN on 24689 at 1.2 s warm / 1.7 s cold-ish** (lsof shows the
  azahar pid owns the socket). App-booted log anchor: `archive_selfncch.cpp:Register:255
  … "this might be a 3dsx file"` at 0.668 s emulated-log time. `BOOT_TIMEOUT_S=30` kept
  ([M] measured max 1.7 s — wide margin for cold caches).
- The INI-route gdb stub works: log shows `Debugging_UseGdbstub: true`,
  `Debugging_GdbstubPort: 24689` read from OUR pins; port stays open while running;
  `status` (lsof, **no TCP connect** — zero interference with the single-client stub) is
  repeatable call-after-call.
- **Update dialog gone on next boot:** `check_for_update_on_start=false` (+`\default=false`)
  held in the live file during runs; **0 'update' lines in either run's azahar_log.txt**
  (no check ran at all); app sat unwedged 10+ s and stop proceeded on schedule.
- Crash recovery: kill -9'd azahar with backup+pidfile left behind → next `boot` printed
  `recovered stale profile backup`, restored byte-identically, then booted fresh. PASS.
- Foreign-instance protection: an azahar the harness didn't start → `boot` aborts rc=1
  without touching the config (profile stayed CLEAN); `status` reports `process=foreign`;
  `--force` takes over. PASS.
- Harvest: run dirs contain `azahar_log.txt`, `azahar_log.old.txt`, `events.log`,
  `boot.json` (timings), `cmdline.txt`, `profile.diff`.
- Full `setup.sh` gate green (venv idempotency hit the up-to-date path on re-run). Smoke
  table verbatim:

```
CHANNEL    VERDICT DETAIL
run        PASS    boot->status->stop; pid=17704 gdb-ready 1.16s; config restored byte-identical
read-state TODO    gdbio lands in E2
press-ctm  TODO    ctm synthesizer lands in E3
press-d4   TODO    Tier B (--rom) lands in E4
sdmc       TODO    netlog/gs-log assertions land in E4
see        TODO    see.py lands in E3 (Screen Recording grant pending -> will SKIP)
```

**Timings for E4** ([M]): boot→gdb-ready 1.2 s warm / 1.7 s cold-ish (azctl `boot` returns
in ~2 s); `stop` takes a flat **5.4 s** (see SIGTERM finding); crash-recovery boot adds ~0.3 s.

**Decisions / deviations (honest log):**
1. **Launch via `open -a <bundle>`**, not spawning `AZ_BIN` directly as H1.5 step 5 wrote —
   the probed LAUNCH RULE (raw binary pops a blocking modal + once swallowed SIGTERM).
   Consequence: azahar stdout/stderr can't land in the run dir (H2.8 lists them);
   `azahar_log.txt` is the harvested ground truth instead. Tracking = pgrep + rotating log.
2. **Crash recovery reordered kill-before-restore** (H1.5 says restore "before anything
   else"): a still-live tracked instance saves the INI on its exit (S3.2,
   citra_qt.cpp:1543) and would overwrite the restore. Host-tested + live-tested.
3. **`aspect_ratio` not pinned** — no such key in 2125.1.2's qt-config.ini (probed grep);
   absent key ⇒ built-in default `0` = the S3.3 pin value; an inserted unknown key would be
   dropped on Azahar's own rewrite anyway.
4. **Rotated log is `azahar_log.old.txt`** (probed), not H1.5's `.old` suffix guess; it is
   harvested unconditionally (rotation-at-launch makes its mtime always pre-spawn — it's
   the pre-run log, kept for context).
5. **SIGTERM does NOT quit Azahar 2125.1.2 while emulating** even with `confirmClose=false`
   (all 4 live cycles survived the 5 s grace → SIGKILL). azctl's escalation handles it
   deterministically, but **E2/E3 note:** clean CTM recording needs a graceful quit —
   gdb `k` = `system.RequestShutdown()` (S2.3) — because kill -9 loses the movie (S1.10).
6. **Azahar rewrites qt-config.ini on boot** (S3.2 confirmed live): pins whose value equals
   a built-in default get `\default` normalized back to `true` (observed `gdbstub_port`,
   `layout_option`) — values survive; non-default pins kept `\default=false` (observed
   `use_gdbstub`, `init_clock`, `init_time`, `check_for_update_on_start`, `confirmClose`,
   `showStatusBar`). The S3.1 force-false rule at apply time stays load-bearing.
7. `init_time` pinned to `946684800` (S3.3 / the S1.11 golden epoch), superseding H1.6's
   keep-the-probed-value option.
8. `--wipe-netlogs` / `--fresh-sd-fixtures` implemented per H2.1+H4.2/H4.3 (sd_guard
   host-tested) but **not live-verified in E1** — Tier B is E4's territory; fixtures also
   re-hash the dual-gba originals on `stop` and fail loudly on mismatch.
9. GUI launches from the session's Bash needed `dangerouslyDisableSandbox: true` (as the
   phase notes predicted); recorded for the skill doc (E4/H5).

No app/source changes (PHASE inv. 6). No stray azahar left running; final state CLEAN;
user config sha re-verified `dd20792e…3286` after the last run.

## 2026-08-08 — E2: gdbio.py (RSP client + symbols + verify-base) — and the release-stub correction

**Files** (new unless noted):
- `tools/emutest/gdbio.py` — symbol table (nm primary, `build/3DGBA.map` cross-check,
  `emutest.mapsyms` cache keyed on ELF mtime+size), stdlib ELF32 PT_LOAD extractor, RSP
  packet layer (build/escape/RLE/PacketReader), the **broker** (`gdbio serve`,
  auto-spawned, unix socket `state/gdbio.sock`), and the CLI: `syms / resume /
  verify-base / read / read-u32 / read-u8 / poll / status / detach / serve`.
- `tools/emutest/tests/test_rsp.py` (14), `test_mapsyms.py` (13), `test_elf_extract.py`
  (10) + fixtures `tiny.c`/`tiny.elf` (built once with devkitARM GCC 15.2.0),
  `nm_snippet.txt`, `map_snippet.map` (verbatim excerpts of the 2026-08-04 build).
  Host suite now **50 tests, all green** (was 10).
- `tools/emutest/smoke.sh` — edited: the read-state row is now live (resume →
  verify-base → renderSeq poll → g_appActive/g_quit → detach).
- `.gitignore` + `setup.sh` — one negation line
  (`!tools/emutest/tests/fixtures/tiny.elf`): the repo's global `*.elf` ignore would
  have silently garbage-collected the checked-in fixture (verified with
  `git add --dry-run`: fixture addable, `3DGBA.elf` still ignored).

**THE HEADLINE: Azahar 2125.1.2 ships the old vanilla-Citra gdbstub, not master's.**
SPEC-protocols S2 (researched from master@5394625) is wrong for the installed release in
load-bearing ways. Re-verified against the release-tag source (fetched
`src/core/gdbstub/gdbstub.cpp` @2125.1.2, 1281 lines; + `core.cpp`, `citra_qt.cpp`) AND
live on the wire (raw transcript probe). The release dialect — now the basis of gdbio.py
(full citations in its module doc):

1. **One gdb client per boot, and every `use_gdbstub=true` boot PARKS pre-first-
   instruction** until that client connects (`DeferStart()` unconditional in
   System::Init, core.cpp:574; blocking `accept()` on the emu thread, gdbstub.cpp:1203;
   halt_loop=true, :1157). Live: `g_renderSeq == 0` a full minute after boot; ran only
   after our `c`. **`-g` adds NOTHING** — it just sets use_gdbstub+port
   (citra_qt.cpp:295-300); INI route ≡ `-g` (both verified live).
   ⇒ **E1 correction:** E1's boots (no gdb client ever connected) never emulated a
   single instruction — the E1 "run PASS" proved process/log/port, not emulation.
   ⇒ **`gdbio resume` is now MANDATORY after every `azctl boot`** (smoke does it).
2. `?` does NOT attach/halt (report-only; replies `T00` before any halt — live-matched).
   HALT = raw `0x03` → `T05` stop-reply. **Wire quirk found live: the T05 is followed by
   a spurious empty packet `$#00`** which shifts every later reply off-by-one unless
   drained — `halt()` drains 0.3 s (the bug cost one debugging cycle; transcript probe
   pinned it byte-exactly: `0x03` → `$T05#b9$#00`).
3. **`D` (detach) is unsupported** (empty reply — live). Closing the TCP calls the
   stub's Shutdown: **disconnect-while-halted freezes emulation FOREVER** (reproduced
   live — the probe-A wedge: CLOSE_WAIT socket, dead listener, parked app); disconnect
   after `c` lets the app run on, but **gdb is spent until the next boot** (no
   re-accept: defer_start cleared, gdbstub.cpp:1230).
4. `k` = stub-off + Continue (gdbstub.cpp:1072-77) — NOT an emulator shutdown. The
   planned `gdbio shutdown` clean-quit channel **does not exist on this release**
   (dropped; see follow-ups).
5. Memory reads: invalid addr → `E00` (not master's empty reply); reads use the kernel's
   current process; chunked ≤4096 B. qSupported (drift detector, recorded per broker
   start): `PacketSize=2000;qXfer:features:read+;qXfer:threads:read+` — no osdata, no
   vAttach, no vCont.

**Consequence — the broker.** Per-command TCP (the H2.2 sketch) is impossible on a
one-client-per-boot stub: gdbio keeps THE connection in an auto-spawned daemon
(`gdbio serve`, JSON-lines on `state/gdbio.sock`, log `state/gdbio-broker.log`); every
read is a halt→read→resume blink (~ms); the broker self-exits on emulator death (TCP
EOF watch) and on `gdbio detach` (which resumes first — rule 3). Deviation from
SPEC-harness H2.2 recorded here: commands added `resume/status/detach/serve`,
`read-u8` (g_quit/g_appActive are 1-byte bools), `poll --width`; dropped `ps` (no
osdata) and `shutdown` (no such semantics).

**LIVE-verified E2 flow (Azahar 2125.1.2, real 3DGBA.3dsx, this machine, 4 boots):**
- `azctl boot --gdb` → gdb-ready 1.2–3.5 s; app parked. Broker handshake: `stop=T05`,
  qSupported as above.
- **verify-base on the PRISTINE (never-executed) app: 3/3 MATCH** —
  `text ctl_log_line @0x001eb328 64B`, `rodata AUDIO_NAMES @0x00235000 64B`,
  `data _deadbeef @0x0049b4e4 4B word=0xe710b710` (mGBA's illegal-instruction marker,
  external/mgba/src/gba/memory.c:28 — the known compile-time word). Pristine `g_prefs`
  = `00000000 cd000000 a8000000 0e000000 …` = the ELF bytes = theme.c:21's
  `{0,205,168,14,…}` (S2.7 confirmed). ⇒ **map/nm addresses ARE gdb addresses, offset
  zero, no slide (S2.6 proven for this build).**
- `gdbio resume` → app boots for real; render loop reached ~5–10 s later (emulated app
  boot); `poll g_renderSeq --changed`: **637 → 700 in 1.0 s ≈ 63 Hz** (main.c:2584's
  ~60 Hz counter). `g_appActive=1`, `g_quit=0` (main.c:85/74). verify-base re-run on the
  RUNNING app: 3/3 MATCH (the `_deadbeef` data anchor is runtime-stable as predicted).
  Running `g_prefs` still equals the pristine bytes (settings.bin matches defaults —
  don't rely on that; the pristine read is the real check).
- `gdbio detach` → broker gone, socket gone, **azahar alive 10 s later** (process alive +
  release-source guarantee: post-`c` close cannot halt; renderSeq advancing right up to
  detach). `azctl stop` → SIGTERM **still** swallowed even while running emulation
  (SIGKILL escalation; E1 deviation 5 re-confirmed for a running app), config restored
  byte-identical.
- Map cross-check vs nm: **2938 shared symbols, 0 mismatches**; `.tbss` TLS symbols
  excluded (nm reports TLS offsets 0x0/0x804 for `__ctru_dev_*_buf`, not vaddrs —
  found live, host-tested).
- Full `setup.sh` gate green (host 50/50 + smoke). Smoke table verbatim:

```
CHANNEL    VERDICT DETAIL
run        PASS    boot->status->stop; pid=44661 gdb-ready 1.19s; config restored byte-identical
read-state PASS    resume; verify-base 3/3 anchors; renderSeq changed 0 -> 35; g_appActive=1 g_quit=0; detach
press-ctm  TODO    ctm synthesizer lands in E3
press-d4   TODO    Tier B (--rom) lands in E4
sdmc       TODO    netlog/gs-log assertions land in E4
see        TODO    see.py lands in E3 (Screen Recording grant pending -> will SKIP)
```

**Timings** ([M]): boot→gdb-ready 1.2 s warm / 3.5 s cold; resume→render-loop-running
5–10 s (smoke polls 30 s); broker handshake <1 s; halt→read→resume blink ~0.4 s
(dominated by the 0.3 s post-halt drain).

**Follow-ups for E3/E4 (binding knowledge):**
1. **Movie runs vs the parked boot:** with the profile's `use_gdbstub=true`, a
   `--record`/`--movie` boot parks until `gdbio resume` — E3 must resume before
   expecting playback, or boot recordings without the stub (azctl change: optional
   `--no-gdb` profile variant). Decide in E3.
2. **Clean quit for CTM recording is UNSOLVED:** SaveMovie needs a graceful exit
   (S1.10); SIGTERM is swallowed (re-verified on a running app) and gdb `k` doesn't
   quit this release. Candidates for E3: the app's own Quit path via CTM/D4 input
   (g_quit flip → app exit → does Azahar close?), or Qt-level automation (permission-
   gated). Record the answer in E3's BUILDLOG.
3. Two-instance UDS experiments (H open question 9) must remember: SECOND instance
   also needs its one gdb client, or boots parked — a second data dir AND a different
   gdbstub_port (or use_gdbstub=false) are both required.

No app/source changes (PHASE inv. 6). No stray azahar/broker left running; final state
CLEAN (`profile=CLEAN`, no `state/gdbio.sock`, `pgrep azahar` empty).

## 2026-08-08 — E3: ctm.py + sdmc.py (PRESS via movies + the sdmc bridge) — and the ir:rst wall

**Files** (new unless noted):
- `tools/emutest/ctm.py` — tests-as-data CTM synthesizer/inspector (H2.3/S1): JSON timeline
  (frame ops `wait/tap/hold/release/touch/touch_hold/touch_release` + poll-exact
  `wait_polls/hold_polls/touch_polls`) → strictly-alternating `[Pad][Touch]` pairs at 234 Hz;
  exact-Fraction frame→poll conversion (234·4481136/268111856 ≈ 3.9110/frame, cumulative
  rounding); header defaults = the profile's pinned epoch/ticks (the movie header OVERRIDES
  the INI, release movie.cpp:597-604); `inspect` decodes back to runs + flags desync-shaped
  problems. A held state shorter than 1 poll fails loudly (a press the console never sees).
- `tools/emutest/sdmc.py` — the H4 bridge: `arm-control / drop / drop-abort /
  wait-consumed / netlogs / latest / cat / control-status`; every write through
  azctl's sd_guard, tmp+rename in-dir (atomic vs the app's 10-frame poll); string-level
  move-grammar pre-flight (control.h tokens; the app's parser stays the authority).
- `tools/emutest/tests/test_ctm.py` (17), `tests/test_sdmc.py` (15) — golden S1.11 bytes
  built in-test field-by-field, frame-conversion pins (120f→469 polls, tap→32/31),
  alternation/count/magic problem detection, grammar table, guard/atomicity/latest/parse.
  `tests/test_mapsyms.py` +1 (`SYM+OFF`). Host suite **83 tests, all green** (was 50).
- `tools/emutest/tests/fixtures/movie_menu_tilt_quit.json` — THE Tier-A movie (smoke uses it).
- `tools/emutest/gdbio.py` — edited: `lookup()` accepts `SYM+OFF` (`g_prefs+0x1c` =
  UiPrefs.tiltLevel, theme.h:38-53).
- `tools/emutest/azctl.py` — edited: `apply_profile(…, extra_pins)`; `--movie/--record` now
  auto-pin `[System] is_new_3ds=false` (`--keep-n3ds` opts out) — see the wall below.
- `tools/emutest/smoke.sh` — press-ctm channel is live (movie → tilt global → quit → log).

**Format re-verified against the installed release** (the E2 lesson): fetched
`movie.cpp`/`hid.cpp`/`ir_rst.cpp`/`apt.cpp`/`ptm.cpp` from tag `2125.1.2` — the CTM
structs/magic/validation/playback/clock-override and the 234 Hz pad→touch order are
**byte-identical to SPEC-protocols S1** (every ctm.py citation is release-verified; golden
`ctm make` output == the S1.11 424-byte example, host-tested).

**THE WALL (S1.5's open question 2, hit live) and its fix.** First live movie run (N3DS
model): loads fine (`Loaded Movie, ID: 0123456789ABCDEF`, warning-only zero revision), pad+
touch stay in sync ~9 s, then `Play:328: Expected to read type 4, but found 0` + cascade —
**type 4 = IrRst**. Chain (all verified): libctru `hidInit` → weak `hidShouldUseIrrst` →
`APT_CheckNew3DS` (disassembled from 3DGBA.elf @0x213618) → release APT→`PTM::CheckNew3DS`
→ `Settings::values.is_new_3ds` (ptm.cpp:121-128); on "new 3DS" the app's hidInit starts
ir:rst → a SECOND movie-consuming callback (ir_rst.cpp:70-144) whose period is app-chosen
and whose phase depends on emulated boot timing → the interleave is **unsynthesizable**.
FIX: movie/record boots pin `is_new_3ds=false` — no irrstInit, stream stays pure pairs.
Cost documented in azctl: movie runs emulate an Old 3DS (ZL/ZR/c-stick gone — CTM pad
records can't press them anyway; N3DS perf claims are hardware territory, invariant 5).
Re-run: **0 desync lines** (type-mismatch lines are Error-level = flushed immediately even
into the lazily-buffered log, so the zero is meaningful; the INFO tail is lost to the
SIGKILL stop — known, not load-bearing).

**Spec correction (H3.5):** "START at the ROM-less picker quits" is wrong — with no ROMs
`scan_roms()==0` makes `rompicker_run` return false with NO input (rompicker.c:194) and
main.c:4448 falls through into `run_session` with dead cores (that session loop is the
~60 Hz renderSeq E2 measured). The Tier-A closed loop therefore drives the SESSION pause
menu instead, and its state global is `g_prefs.tiltLevel` (the tilt SEG writes the pref
even where the effect is N3DS-only, main.c:3220-3233).

**LIVE loop 1 — Tier A (zero-permission, ROM-less), twice (manual + smoke):**
movie = wait 1200f → START+SELECT 40f (menu combo, main.c:2700-2704) → touch 40,110
(ENHANCE tab) → touch 250,210 / 290,210 (TILT seg 2 then 3, PT_ENHANCE row 5) → touch
40,20 (SESSION tab) → touch 200,125 (QUIT, ACT_QUIT=18) → end.
- `gdbio read-u32 g_prefs+0x1c` BEFORE = **0**; `poll --expect 3` → **PASS (0 → 3)**
  (9 samples manual / 23 samples in smoke, ~10-24 s wall after resume).
- Quit observable pinned: the emulated app's exit **closes the gdb stub's TCP session** —
  broker exits with `emulator closed the RSP socket` while the Azahar shell stays alive
  (pgrep alive, port dead). smoke greps the broker log tail for exactly that line.
- settings.bin (sdmc:/3DGBA) really was rewritten by the movie's menu taps (sha
  `7ac253c6…` → `58f750b7…`) and restored byte-identically from the pre-run snapshot;
  smoke snapshots/restores it every ctm cycle (and removes a created one if none existed).
- User qt-config.ini restored byte-identically after every cycle.

**LIVE loop 2 — the D4 channel, proven end-to-end too** ("one channel is the gate, both is
the goal" — done in-slice via a scratchpad picker movie; E4 formalizes it as `--rom`):
`sdmc arm-control` + pre-drop `move_p1.txt` = `W60` → boot `--movie pick_and_wait.ctm
--fresh-sd-fixtures` (wait 900f → taps A, A, DOWN, A, X — splash-safe ordering) → the CTM
channel drove the REAL ROM PICKER into a dual-core session of the user's fixtures:
- `gdbio poll g_ctlStat+6 --expect 1 --width 2` → **PASS 0 → 1** (seat-0 pickups counter
  @0x004a2606, control.h:170-176);
- `sdmc wait-consumed move 1` → **consumed** (remove-on-pickup ACK, main.c:460);
- `sdmc control-status --expect-pickup` → header `# control p1=BPEE p2=BPRE` (real
  Emerald+FireRed cores) + `[ctl p1] picked up 1 tokens`; log filename
  `3DGBA_control_0101_020026.txt` = **2000-01-01 02:00** — the movie-header clock override
  visibly pinning the app's RTC (S1.8 confirmed on-device).
- Teardown: azctl stop re-hashed the dual-gba originals (**all untouched**), fixture
  copies + app-written `recent.bin` removed, settings.bin restored (sha `7ac253c6…`),
  `sdmc:/3DGBA` back to settings.bin-only. `cias/control/` left armed (E4 needs it; empty
  dir = zero injection) and the one app-written control netlog left in the user's netlogs
  (harvested copy in run dir 20260808-180242).

**Full `setup.sh` gate green** (venv up-to-date path, host 83/83, live smoke ~3 min).
Smoke table verbatim:

```
CHANNEL    VERDICT DETAIL
run        PASS    boot->status->stop; pid=55860 gdb-ready 1.21s; config restored byte-identical
read-state PASS    resume; verify-base 3/3 anchors; renderSeq changed 0 -> 35; g_appActive=1 g_quit=0; detach
press-ctm  PASS    movie menu-drive: tiltLevel 0->3; quit: app exit closed the RSP session (broker down, azahar shell alive); Loaded Movie in log, 0 desyncs
press-d4   TODO    Tier B (--rom) lands in E4 (sdmc.py drop/wait-consumed ready, host-tested)
sdmc       TODO    live netlog/gs-log assertions land in E4 (sdmc.py readers ready, host-tested)
see        TODO    see.py pending (Screen Recording grant pending -> will SKIP)
```

**Timings** ([M]): emulated app boot → ROM-less session ≤ 20 s emulated (the movie's 1200f
lead is safe margin); resume → tilt=3 observed 10-24 s wall; quit → broker-down observable
~2 s later; the whole press-ctm smoke cycle ~90-120 s; D4 pickup ≤ 20 samples (~20 s) after
resume incl. picker drive.

**Follow-ups / notes for E4+:**
1. E2 follow-up 2 (clean quit for `-r` recording) is HALF-solved: a movie can quit the APP
   cleanly (menu Quit), but a `-r` recording run has no input channel until Accessibility
   is granted — the revision-harvest bootstrap stays open; zero-revision movies play fine
   (warning line only).
2. Tier-A movies REQUIRE the ROM-less state (`sdmc:/3DGBA` without .gba files): staged
   fixtures change boot flow to the picker — smoke's tilt movie would misfire. azctl stop
   never removes fixtures; wipe them (per manifest) before a Tier-A movie run, as done here.
3. `gs`/`touch` netlogs are dumped only when something was CAPTURED (gamestate.c/touch.c
   early-return on empty) — a ROM-less quit writes none; Tier B's Pokémon session is where
   the gs-log sdmc assertion belongs (H4.4 already says so for the CSV).
4. movie runs emulate an Old 3DS (the pin); anything model-sensitive (tilt EFFECT, core-2
   pinning) must be exercised via non-movie channels or on hardware.

## 2026-08-08 — E4: see.py (+ the VIDEO channel), compare/zoom/sheet proven live, smoke's final form, SKILL.md

**Files** (E3's session had *written* see/compare/zoom/sheet/smoke/SKILL.md but died before
running any of them live — this slice is where they meet a real emulator):
- `tools/emutest/see.py` — EDITED: (a) refactor `window_geometry()` / `_grab()` /
  `_uniform()` / `require_window()` out of `shot()`; (b) **the new `rec` video channel**
  (`rec_plan`, `frame_name`/`frame_pattern`, `ffmpeg_bin`/`ffmpeg_cmd`, `default_rec_dir`,
  `_StateReader`, `rec()`, CLI `--seconds/--fps/--screen/--out/--with-state/--state-width/
  --format/--keep-window`); (c) a live bug fix (below); (d) module doc rewritten around
  LIVE-probed constants instead of the E3 session's untested guesses.
- `tools/emutest/tests/test_see_rec.py` — NEW, 14 host tests (schedule, frame naming ⇔
  ffmpeg pattern, argv, ffmpeg discovery, window geometry incl. the live case, the blank
  tripwire, manifest shape). Host suite **125 tests, all green** (was 111).
- `tools/emutest/smoke.sh` — EDITED: new **`see-rec` row** (12-frame clip → manifest cross-
  checked against the files on disk → mp4 size; PASS / SKIP-with-reason / FAIL) + its
  `--no-live` row.
- `.claude/skills/emutest/SKILL.md` — EDITED (H5): `see rec` in the quickstart, a "Seeing
  motion" section (why not `--dump-video`, the measured rates, how to cite "frame 37"), and
  a troubleshooting entry for the trap below.
- `compare.py` / `zoom.py` / `sheet.py` — UNCHANGED code, first time exercised on real
  captures (results below).

**THE TRAP (found live, cost one capture): Azahar is SINGLE-WINDOW.** The first real
capture came back showing Azahar's **game list**, not the 3DS screens — because the movie
had already driven the emulated app through its menu-QUIT, and the same window reverts to
the list when the app exits. Probed with `CGWindowListOptionAll`: 11 Azahar window entries,
**exactly one on-screen** (`id=139029 layer=0 bounds=(323,154 1280x568 pt)
name='Azahar 2125.1.2'`), i.e. there is no separate render window to find — the window
*content* changes. Consequence: a crop is only meaningful while the app is running
(cross-check `g_renderSeq`), now stated in see.py's module doc and SKILL.md §8.

**LIVE-verified SEE — PASS, with what the crops actually showed** (Screen Recording grant
confirmed effective: full-window capture 2560x1136 px with real content):
- Geometry, recomputed per shot (S4.5): window 1280x568 pt → factor **2.00**, title
  **56 px**, client **2560x1080** → S4.1 rects **top (830,56)-(1730,596) = 900x540** and
  **bottom (920,596)-(1640,1136) = 720x540** (= 400/320 × 2.25 — no chrome, no bleed).
- **Top crop** (`runs/20260808-203721/rec-menu/top_00114.png`): the app's pause screen —
  `❚❚ PAUSED`, `gameA ⇄ gameB`, the chip row `3D | DoF | Bloom | Light | Tilt | Touch Off |
  Co-op | Link`, `settings on the touch screen ↓`.
- **Bottom crop** (`bottom_00114.png`): the ENHANCE settings tab — tab rail Session/Display/
  Audio/**Enhance**/Link/Touch, rows Stereoscopic 3D, Tilt-shift DoF, LDR Bloom, Time-of-day
  light, Vivid mode, and the `TILT  Off | Low | Mid | Max` segmented control with **Mid**
  selected; `bottom_00118.png` shows **Max** selected.
- Smoke's own crop (`runs/20260808-204446/see/screen.bottom.png`): the ROM-less session
  bottom screen — HUD `+ gameB   59 fps   02:00` and `tap screen · pause menu`. The
  **02:00** is the pinned RTC epoch rendering on screen (S3.3 `init_time=946684800`).
- **The three channels agree**: gdb-read `g_prefs+0x1c` = 2 at frames 113-116 and 3 from
  117 ⇔ the pixels show Mid then Max ⇔ the recorded manifest state. That cross-check is the
  point of the whole harness.

**LIVE-verified VIDEO (`see rec`) — the added scope.** Azahar's `--dump-video` was NOT
chased (brief: strict libavutil major check vs Homebrew avutil.60). Instead a 32 s clip of
the CTM-driven menu interaction (E3's `movie_menu_tilt_quit.json`, replayed unchanged):
- `see rec --seconds 32 --fps 4 --screen both --with-state g_prefs+0x1c` → **128 frames**,
  `manifest.json` with per-frame `t_rel`/`t_utc`/files/state, `ended_reason=duration`.
- ffmpeg 8.1.2 (`/opt/homebrew/bin/ffmpeg`, probed) assembled **`rec_top.mp4` 51 766 B**
  and **`rec_bottom.mp4` 83 161 B**; `ffprobe`: h264, **900x540 / 720x540, 128 frames,
  83.48 s** each — real, playable, non-trivial.
- Frame-addressable evidence works: `manifest.frames[113].state = 2`, `[117] = 3`, and the
  matching PNGs read back exactly those two UI states (above).
- **Rates measured [M]**: `screencapture -x -o -l<id>` = **0.08 s** (3 timed runs);
  capture-only loop sustained **6.04 fps at `--fps 6`**; with one `--with-state` symbol the
  4 fps request delivered **1.53 fps** (each state read is a gdbio halt→read→cont blink,
  ~0.4 s, which also slows the emulated app while recording). Both numbers are now in
  see.py's doc and `--help`.
- Smoke's cheap in-gate proof: 12 frames @ **4.03 fps** → `rec_top.mp4` 9 421 B.
- `--format gif` verified separately by running its argv over the same 128 real frames:
  rc=0, **17 983 713 B** — it works but a palette-free gif is ~200x the mp4, so gif stays
  what its docstring says: short clips only (the probe file was deleted, not banked).

**LIVE-verified compare / zoom / sheet** (they had only ever run on generated fixtures):
- `compare frame114 frame114` → `IDENTICAL — 720x540, 0 differing pixels`, **exit 0**.
- `compare frame114 frame118 -o diff_tilt.png` → `DIFFERENT — 9615/388800 pixels (2.473%),
  max channel delta 199, mean 2.565, changed bbox x487-693 y449-533`, **exit 1** — and that
  bbox is exactly the TILT segmented control, i.e. the heat map localizes the real change.
- `compare … --max-diff 200000` → **exit 0** (the slack path).
- `zoom bottom_00118.png --rect 300,435,420,80 --scale 3` → 1260x240, `Off | Low | Mid |
  [Max]` plainly readable.
- `sheet e4-evidence.html … ring=…:good/bad` → one self-contained 512 KB HTML, 6 cells,
  mixing 900x540 top, 720x540 bottom, a zoom and a 3-panel diff (the dual-screen adaptation
  works). Artifacts: `tools/emutest/runs/20260808-203721/evidence/`.

**Live bug found and fixed:** `see shot --raw-window DIR/window.png` crashed with
`FileNotFoundError` when the parent dir did not exist — the crop paths were `makedirs`'d,
the raw-window path was not. smoke.sh had masked it by pre-creating its `see/` dir. Fixed
(and the fix is what let the first real capture land).

**Smoke — full coverage, `smoke.sh --rom`, exit 0** (this slice ran Tier B inside the gate
for the first time; E3 had proven those two channels by hand):

```
CHANNEL    VERDICT DETAIL
run        PASS    boot->status->stop; pid=89572 gdb-ready 1.19s; config restored byte-identical
read-state PASS    resume; verify-base 3/3 anchors; renderSeq changed 0 -> 35; g_appActive=1 g_quit=0; detach
press-ctm  PASS    movie menu-drive: tiltLevel 0->3; quit: app exit closed the RSP session (broker down, azahar shell alive); Loaded Movie in log, 0 desyncs
press-d4   PASS    move_p1 consumed (pickup ctr 0->1); header: p1=BPEE p2=BPRE dir=sdmc:/cias/control; app quit (RSP session closed)
sdmc       PASS    quit-time gs log: 3DGBA_gs_0101_020032.txt: header ok, 7 data rows
see        PASS    window captured + cropped: top 900x540 2.3% lit; bottom 720x540 1.0% lit (crops in .../runs/20260808-204446/see)
see-rec    PASS    12 frames @ 4.03 fps -> rec_top.mp4 (9421 B) (in .../runs/20260808-204446/rec)
```

**Final gate — `./setup.sh` (venv → 125 host tests → live smoke), exit 0**, run AFTER the
last edit (H6.4):

```
CHANNEL    VERDICT DETAIL
run        PASS    boot->status->stop; pid=92116 gdb-ready 1.18s; config restored byte-identical
read-state PASS    resume; verify-base 3/3 anchors; renderSeq changed 0 -> 35; g_appActive=1 g_quit=0; detach
press-ctm  PASS    movie menu-drive: tiltLevel 0->3; quit: app exit closed the RSP session (broker down, azahar shell alive); Loaded Movie in log, 0 desyncs
press-d4   SKIP    Tier B needs --rom (stages copies of the user's dual-gba ROMs, H4.2)
sdmc       SKIP    Tier B needs --rom (the gs-log assertion needs a Pokemon session, H4.4)
see        PASS    window captured + cropped: top 900x540 2.3% lit; bottom 720x540 1.0% lit (crops in .../runs/20260808-204848/see)
see-rec    PASS    12 frames @ 3.98 fps -> rec_top.mp4 (9473 B) (in .../runs/20260808-204848/rec)
```

**User-data integrity after everything** (PHASE inv. 2): `qt-config.ini` sha
`dd20792e32878733` = the same bytes E1/E2/E3 recorded; `sdmc:/3DGBA` back to
`settings.bin` only; dual-gba originals re-hashed **untouched** by azctl; `cias/control/`
empty (armed, no injection); no azahar process, no `state/gdbio.sock`, profile CLEAN.

**The settings.bin incident (honest log).** Two by-hand movie boots I ran *outside* smoke
(smoke snapshots/restores settings.bin; ad-hoc runs do not) left the app's persisted
`tiltLevel` at Max — the user's file went `7ac253c6…` → `58f750b7…`. Recovered exactly:
brute-forcing single-field edits showed the only difference is the u32 at **offset 92
(0x5C)**, 3 → 0, giving back sha **7ac253c67fb50ce8** = E3's recorded pre-run original;
independently confirmed by the next smoke run reading `tiltLevel before movie taps: 0`.
Rule for future sessions (now in the skill's user-data section by implication): drive
movies through smoke, or snapshot `sdmc:/3DGBA/settings.bin` yourself first.

**OS-input capability probe (the brief's open question).** `osascript`/System Events stays
denied (-1743), but **`CGEventPost` WORKS** for this session (Accessibility granted): a
synthetic `kCGEventMouseMoved` moved the cursor to the requested point and it was restored
(probe printed `CGEventPost mouse-move: WORKS`). **No tooling was built on it** — CTM and
D4 remain the primary input channels (they are deterministic and permission-free); the
CGEvent path is recorded as available for Qt-UI-level actions a movie cannot reach.

**Decisions / deviations (honest log):**
1. **`see rec` replaces `--dump-video`** (brief). Frames are the deliverable; video is a
   bonus assembled by ffmpeg. `--screen both` writes **two** videos (`rec_top.mp4`,
   `rec_bottom.mp4`) rather than compositing — the screens have different geometry
   (400x240 vs 320x240) and each stream stays pixel-exact for compare.py.
2. **`rec` exits 0 whenever frames exist**; the video/state sub-verdicts live in
   `manifest.json`, so exit **75 keeps meaning "permission SKIP" only** and smoke can still
   render a SKIP row for "frames ok, no ffmpeg" (PHASE inv. 4 honesty without overloading
   the code).
3. Recording **stops early and honestly** on `window-gone` (the Tier-A movie ends with an
   in-app QUIT) or `window-resized` (a mid-clip resize would ruin the assembly) — partial
   frames + a reason beat ragged frames.
4. Smoke's `see-rec` row is deliberately tiny (3 s @ 4 fps, **no** `--with-state`): it
   proves the channel, not a long clip, and shares the boot with the other rows.
5. `--with-state` is documented as **interfering** (halt→read→cont per frame): never use it
   while measuring anything timing-sensitive; the manifest's real timestamps are the record.
6. No app/`source/` changes (PHASE inv. 6). E3's fixtures and movies were reused verbatim.

**Observation, NOT a claim (H6.5).** In both `bottom_00114.png` (Mid) and `bottom_00118.png`
(Max) the **selected** TILT label renders smeared/doubled while the unselected labels are
crisp; the later frames of that recording had already moved to another tab, so there is no
settled-state frame to compare. Most likely the segment-selection animation caught by a
1.5 fps sampler. Recorded as a follow-up probe (a dedicated movie that opens ENHANCE, taps
one segment and then waits), **not** as a defect — and no app change either way.

**Timings [M]:** `see shot` ≈ 1 s; `see rec` 3 s clip ≈ 4 s incl. ffmpeg; `smoke.sh --rom`
≈ 5 min wall; `setup.sh` full gate ≈ 3.5 min; press-ctm tilt observed after 23 poll samples,
Tier-B pickup after 27.

**Follow-ups for the next slice / phase:**
1. A CGEvent-based `press` sub-channel (Qt menus, Azahar's own dialogs) is now known to be
   POSSIBLE; build it only when a test needs the emulator UI itself, never for app input.
2. The TILT-label smear probe (above).
3. `--record` (`-r`) movie harvesting still lacks a graceful-quit path when the run has no
   in-app QUIT (E2 follow-up 2, unchanged): CGEvent could close the window now — worth a
   look if a real recording bootstrap is ever needed (zero-revision movies play fine, so it
   is not blocking).
