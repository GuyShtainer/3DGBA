---
name: emutest
description: >-
  Run, drive and verify the 3DGBA app in the Azahar emulator from the session: boot/stop
  with a managed profile, read app globals over GDB, press buttons via synthesized CTM
  movies or the app's own sdmc control files, screenshot and diff screens. Use whenever a
  3DGBA change needs a live check without the user's hardware — before asking the user to
  test anything, and after any source change that a test movie or a gdb-readable global
  can prove.
---

# emutest — the 3DGBA emulator self-test harness

Phase-16 tooling, committed at `tools/emutest/` (specs + BUILDLOG in
`docs/phase16-emutest/`). Everything below was LIVE-verified on this machine against
Azahar 2125.1.2 on 2026-08-08. The tools are self-describing — run any of them with
`--help`; this doc deliberately repeats **no** addresses, timeouts or byte offsets
(they live in the code, with citations).

## 1. Quickstart

All commands go through the venv shim `tools/emutest/run` (no activation needed).
First time (or after an Azahar update): `tools/emutest/setup.sh` — builds the venv,
runs the host tests, then the live smoke gate.

```bash
T=tools/emutest
$T/run azctl boot --gdb            # profile backup+apply, bundle launch, readiness
$T/run gdbio resume                # MANDATORY: this Azahar release parks EVERY
                                   # use_gdbstub boot until a gdb client says continue
$T/run gdbio read-u32 g_prefs+0x1c # read a global (symbols from 3DGBA.elf via nm)
                                   # (a read BEFORE `resume` is answered from a PARKED
                                   #  emulator: it prints a [HALTED] marker + warning —
                                   #  those bytes are the ELF's initialisers, not state)
$T/run gdbio poll g_renderSeq --changed          # ~60 Hz liveness
$T/run see shot both shots/s.png   # top+bottom screen crops (Screen Recording)
$T/run see rec --seconds 8 --fps 4 --screen both   # PNG frames + manifest + .mp4
$T/run ctm make script.json m.ctm  # synthesize a from-boot input movie
$T/run sdmc drop move 1 "U3 a W60" # in-game input via the app's own D4 channel
$T/run gdbio detach                # retire the boot's one gdb client (resumes first)
$T/run azctl stop                  # kill + harvest evidence + RESTORE THE USER CONFIG
```

**Always `azctl stop` when done** — it restores the user's `qt-config.ini`
byte-identically. A crashed session self-heals: the next `boot` restores the stale
backup first. The restore is refused (and the backup KEPT) while any Azahar is still
alive, because a live instance rewrites the INI on exit — close it, then `azctl restore`.
Concurrency: every mutating `azctl` command takes an advisory lock on `state/`, and a
booted instance is stamped with an owner (`EMUTEST_SESSION`, else the parent pid) so a
second session's restart/stop is announced instead of looking like your own. Per-run evidence lands in `tools/emutest/runs/<UTC stamp>/` (events.log,
azahar logs, harvested netlogs, boot timings, screenshots).

Image verdicts: `run compare ref.png new.png --tolerance N --max-diff N` (exit code =
verdict, 3-panel heat-map via `-o`), `run zoom in.png out.png --rect x,y,w,h --scale 4`
(read tiny glyphs), `run sheet out.html img:caption:ring=x,y:good ...` (one captioned
HTML artifact per claim — the evidence rule).

## 1b. Instances — TWO Azahars at once (phase-21 S3)

Every tool takes `run --instance <id> …` (or `EMUTEST_INSTANCE=<id>`); the single letter
derives EVERYTHING: state dir (`state-b/`: lock, pidfile, config backup, broker socket),
runs dir (`runs-b/`), gdb port (a=24689, b=24690, …; the INI pin and gdbio both follow),
and for non-default ids a PRIVATE Azahar at `tools/emutest/az-<id>/` — an APFS-clone
bundle copy plus its own `user/` data tree (config/sdmc/log), built automatically on
first boot. Mechanism + citations in `instance.py`'s module doc (Azahar has no user-dir
flag; the portable `user/`-next-to-the-bundle route is the probed override). Instance
`a` is the legacy default — identical paths/port to pre-S3, so plain calls change nothing.

```bash
T=tools/emutest
$T/run azctl boot --gdb --movie m.ctm --stage-roms emerald     # instance A, solo Emerald
$T/run --instance b azctl boot --gdb --movie m.ctm --stage-roms firered   # B, concurrent
$T/run --instance b gdbio resume        # B's own port + broker
$T/run --instance b see shot both b.png # B's own window (owner-PID matched)
$T/run --instance b azctl stop          # stopping one instance never touches the other
```

Rules that make it safe: `see` matches the window by **kCGWindowOwnerPID** from the
instance's pidfile (never by owner name alone — that cross-captures); azctl claims only
azahar processes from its own bundle path (the default instance still refuses to boot
over a user-launched Azahar, and never touches `az-*` pids); non-default launches use
`open -n` (LaunchServices dedupes by bundle id otherwise). `--stage-roms NAME[,NAME]`
copies `roms/NAME.gba(+.sav)` to `sdmc:/3DGBA/gameA[,gameB]` and writes a matching
`recent.bin`, so a wait+tap-A movie boots straight into the game; `clean-fixtures`
reverses it. Instance B never touches the user's real Azahar data or `dual-gba/`
originals. Measured cost of a second instance (2026-08-14, this machine): see the
S3 gate entry in `docs/phase21-touch-census/OVERNIGHT2-BUILDLOG.md`.

**Seeing motion (`see rec`).** Azahar's own `--dump-video` is dead on this machine
(strict libavutil major check vs Homebrew's avutil.60), so the harness records by
capturing the window on a timer: numbered PNG frames (`top_00037.png` — the model READS
frames, video is for the user), a `manifest.json` (per-frame timestamps, window geometry,
and with `--with-state SYM[,SYM]` the gdb-read value of a global AT each frame), and an
ffmpeg-assembled `rec_top.mp4` / `rec_bottom.mp4`. Cite evidence as "frame 37" and point
at the manifest. Practical rates: ~6 fps capture-only; each `--with-state` symbol adds a
~0.4 s halt→read→cont blink per frame (measured: 4 fps request → 1.53 fps, and the
emulated app runs correspondingly slower while you record). No ffmpeg → frames still
land and the manifest says `video: null` with the reason (never claim video that failed).

## 2. The closed loop (what smoke proves; copy-paste recipes)

`tools/emutest/smoke.sh` is THE gate — per-channel PASS/FAIL/SKIP table, exit 1 iff any
FAIL. Run it after ANY harness change; run `smoke.sh --rom` to also exercise Tier B.

**Tier A — zero permission, no ROMs** (the default): boot ROM-less (the app falls
through the empty picker into a dead-core session), play a synthesized movie that drives
the pause menu, watch `g_prefs.voxPitch` flip over gdb, menu-QUIT, confirm the app exit
closed the RSP session. RUN → PRESS → READ-STATE with nothing granted.

**Tier B — `--rom`**: stages copies of the user's `dual-gba/` ROMs+saves as fixtures
(`azctl boot --fresh-sd-fixtures`), movie-drives the real ROM picker into a dual-core
Pokemon session, proves the app's own D4 file-input channel (pre-dropped
`move_p1.txt` consumed = filesystem ACK; pickup counter over gdb; `# control` log), then
menu-QUIT and asserts the quit-time gs log. Teardown: `azctl clean-fixtures` (originals
hash-verified before and after), settings.bin restored.

Movie boots pin the emulated model to Old 3DS (`is_new_3ds=false`) — on the N3DS model
libctru starts an ir:rst input consumer whose movie interleave is unsynthesizable
(desync). Anything model-sensitive goes through non-movie channels or hardware.

Tier-A movies REQUIRE the ROM-less SD state; `azctl clean-fixtures` restores it.

## 3. Honest limits (CLAUDE.md #6 — binding)

Azahar cannot prove core-2 contention, the 804 MHz budget, L2 effects, UDS latency,
stereo fusion, or real audio/frame pacing. This harness makes ITERATION self-serve;
**real New-3DS hardware remains the sign-off gate** for anything timing-sensitive (the
frame budget, audio sync, the link cable). Two-instance Azahar + a local room MAY carry
UDS code — strictly an experiment, never evidence (PHASE.md invariant 5). Note the
second instance needs its own data dir AND its own gdb client, or it boots parked.

## 4. Permission SKIPs

`see` (and any future OS-input path) exits **75** = SKIP, loudly, when its TCC grant is
missing — a SKIP is never a pass, and the zero-permission loop (gdb + CTM + sdmc) is
always available. Grants live in System Settings → Privacy & Security → Screen
Recording (for `see`) / Accessibility (for CGEvent/cliclick clicks), enabled for the
HOST app (Visual Studio Code), which must be quit and reopened after granting. Status
on this machine 2026-08-08: **both granted and verified working**; cliclick not
installed (optional; CTM/D4 are the primary input channels regardless).
Session note: GUI launches from the Bash tool may need `dangerouslyDisableSandbox: true`.

## 5. Pinned emulator version

The harness profile disables Azahar's update check; **never update Azahar
mid-investigation** — the E2 lesson is that the release ships a DIFFERENT (older) gdb
stub than master, so surfaces can shift under you. Every run dir records the version.
After a deliberate update: re-run `tools/emutest/setup.sh` (host tests + smoke) before
trusting anything, and re-check the release-vs-master notes in gdbio.py/ctm.py.

## 6. User data rules

- `sdmc/dual-gba/` (real ROMs + the real `gameA.sav`) is READ+COPY only — never write,
  rename or delete there. Enforced in code by `azctl.assert_sd_writable`.
- Fixtures are COPIES in `sdmc/3DGBA/` via `--fresh-sd-fixtures`; manifest-tracked;
  `clean-fixtures` removes only manifest-listed files and re-hashes the originals.
- Netlogs are the user's data: wiped only with an explicit `--wipe-netlogs`; `stop`
  always copies new ones into the run dir (wipe-before / archive-after, the established
  hardware workflow).
- The user's `qt-config.ini` and emulator-side `settings.bin` are snapshot/restored
  around every smoke cycle.

## 7. The sdmc bridge cheat-sheet

On Azahar the 3DS SD card is a plain host directory — the app's phase-13 diagnostics
become a zero-permission state channel:

- **In**: `run sdmc arm-control` once (the app arms D4/D5 iff the dir exists at session
  start), then `run sdmc drop move 1|2 "<script>"` — grammar per `source/control.h`
  (walks `L R U D` + count, lowercase = sprint; buttons `a b s c x y`; waits `W<n>`;
  `G` = wait-for-go). Consumed file = pickup ACK (`run sdmc wait-consumed move 1`).
- **Out**: `run sdmc latest control|gs|touch|csv|net|wd|hang|rec`, `run sdmc cat`,
  `run sdmc control-status --expect-pickup`. Which logs exist when: `control` lazily at
  the first status line; `gs`/`touch` dumped on Quit/Change-games/link-error and ONLY if
  something was captured (empty on ROM-less runs); the D3 `csv` and `net` logs are
  WIRELESS-SESSIONS-ONLY — their absence in a solo run is normal, never "fix" a test by
  expecting them.

## 8. Troubleshooting

- **Stale profile backup / crashed run** → next `azctl boot` auto-restores and says so.
- **Foreign azahar running** → `boot` refuses; close it or `boot --force` (it may be
  the user's own session — check first).
- **Missing `3DGBA.elf`/`.3dsx`** → `export DEVKITPRO=/opt/devkitpro
  DEVKITARM=$DEVKITPRO/devkitARM && make -j8`.
- **Everything reads 0 / renderSeq frozen** → you forgot `gdbio resume` (parked boot).
- **gdb dead after a detach** → by design: one client per boot; `azctl boot` again.
- **SIGTERM ignored on stop** → known (modal-class Qt behavior); azctl escalates to
  SIGKILL automatically. Consequence: a `--record` movie needs an in-app QUIT (movies
  are only written on graceful exit).
- **Movie desync `Expected to read type 4`** → the is_new_3ds pin regressed (§2).
- **`see` SKIPs though granted** → the host app wasn't restarted after granting.
- **The capture shows Azahar's GAME LIST, not the 3DS screens** → the emulated app is not
  running (it never booted, or it already quit — a Tier-A movie ends with an in-app QUIT).
  Azahar is single-window: the same window carries the game list and the render surface.
  Confirm with `run gdbio poll g_renderSeq --changed` before trusting any crop — smoke.sh
  now does exactly this automatically, reading `g_renderSeq` immediately before and after
  every gate capture and FAILing the `see` row when it did not advance.
- **`gdbio poll` fails immediately on a healthy boot** → fixed 2026-08-09: `poll --timeout`
  is a WALL-CLOCK deadline defaulting to 30 s (it used to inherit the 5 s per-RSP-op
  timeout, which expires before the app reaches its render loop ~5-10 s after `resume`).
- **`gdbio resume` says "app running" but nothing moves** → was a zombie broker surviving
  its dead emulator (fixed 2026-08-09: the broker's EOF watch no longer skips while
  halted, and every request re-checks). If you still see it: `pkill -f "gdbio.py serve"`
  and `azctl boot` again.
