# Phase 16 — the emulator self-test harness (run / see / press / read-state)

Decided 2026-08-08. The user: "I want you to have as much control as possible to run tests right
as you build the app. This is important as it saves me a lot of time doing it myself."

Model: gba-toolkit's PokeDNA harness (in-process `libmgba-py`: run_frame / set_video_buffer /
set_keys / read_mem). **No 3DS equivalent library exists** — Azahar is GUI-only — so the same four
capabilities are assembled from Azahar's outside surfaces instead. Probed and confirmed on this
machine (Azahar 2125.1.2 at `~/Applications/Azahar.app`, data at
`~/Library/Application Support/Azahar/`):

| Capability | Channel | Permission needed |
|---|---|---|
| RUN | CLI: `azahar -w [--gdbport N] [--movie-play f] <3dsx>`; managed config profile | none |
| READ-STATE | `--gdbport` GDB RSP stub + symbol addresses from the build's `.map`; PLUS the phase-13 diagnostics artifacts on the virtual SD (`sdmc/cias/netlogs/` CSV + gs logs are directly readable files on the host!) | none |
| PRESS | (a) CTM TAS movies (`--movie-play`, format from Azahar's open source — buttons AND touch); (b) the phase-13 D4 control files (`sdmc/cias/control/move_p*.txt`) for in-GBA-game input — our own app injects them; (c) OS-level clicks/keys (cliclick/osascript) for ad-hoc Qt UI | (a)(b) none; (c) Accessibility |
| SEE | window-targeted `screencapture`; optional `--dump-video` revival (strict libavutil major check) | Screen Recording (user grant pending) |

The zero-permission closed loop that must work FIRST: boot the `.3dsx` → play a synthesized CTM
(or drop a D4 control file) → verify the effect by GDB-reading a known global from the `.map`
and/or by reading the app's own sdmc logs. Pixels are the last channel, not the first.

## Invariants (binding)

1. **Committed to THIS repo** (`tools/emutest/`). The PokeDNA harness lived only in a session
   scratchpad and parts of it have already been garbage-collected — that mistake is the reason
   this phase exists as a phase. Docs + a project skill so any future session can drive it.
2. **The user's emulator data is user data.** `qt-config.ini` is backed up before the harness
   profile is applied and restored on teardown; ROMs/saves on the virtual SD are copied, never
   edited in place (`gameA.sav` is a real save). The harness never deletes anything it did not
   create.
3. **Deterministic profile**: windowed, fixed window size/position, `layout_option=0` (top over
   bottom — makes touch→pixel math static), `init_clock=1` + fixed `init_time` (reproducible
   screenshots — the RTC-pinning lesson), `check_for_update_on_start=false` (the update dialog
   hijacked a run and swallowed SIGTERM), single-instance discipline (kill before launch).
4. **Every capability self-verifies.** `smoke.sh` proves the whole loop and prints per-channel
   PASS / FAIL / **SKIP(reason)** — permission-gated channels SKIP loudly until granted, never
   silently pass (the phase-13 verdict.sh rule).
5. **Honest limits stated in the skill doc**: Azahar cannot prove core-2 contention, the 804 MHz
   budget, UDS latency, stereo fusion, or real frame pacing (CLAUDE.md #6) — this harness makes
   ITERATION self-serve; hardware runs remain the sign-off gate. Two-instance Azahar + a local
   multiplayer room MAY carry our UDS code — treat strictly as an experiment, never as evidence.
6. **Suites stay green; nothing in `source/` changes.** This phase is tooling + docs only. If a
   harness need reveals an app-side gap (e.g. a debug read address), record it in BUILDLOG as a
   follow-up — do not slip app edits into a tooling phase.
7. Pure-Python/pure-shell, macOS-native; any venv lives under `tools/emutest/.venv` (gitignored)
   with a `setup.sh` that rebuilds and SMOKE-gates it (the PokeDNA setup.sh pattern). No secrets,
   no ROMs, no user paths hard-coded beyond the documented Azahar defaults.

## Deliverables

- `tools/emutest/setup.sh` — venv (pyobjc-Quartz for window geometry, pillow for image ops),
  cliclick install note, ends with a mandatory smoke gate.
- `tools/emutest/azctl.py` — launch/stop with the managed profile (backup/apply/restore),
  `--gdbport`, movie flags; single-instance enforcement.
- `tools/emutest/gdbio.py` — minimal GDB RSP client (connect, read/write mem, symbol lookup from
  the build `.map`), with a tiny CLI (`gdbio.py read g_prefs 32`).
- `tools/emutest/ctm.py` — CTM synthesizer: buttons + touch timelines → `.ctm`; golden-byte host
  test pinned against the format read from Azahar's source.
- `tools/emutest/see.py` — window-id lookup (Quartz) + targeted `screencapture`; crops for top
  (400x240) / bottom (320x240) screens from the known layout; SKIPs with instructions when the
  permission is missing.
- `tools/emutest/compare.py`, `zoom.py`, `sheet.py` — ported from the PokeDNA harness (they are
  platform-agnostic; sheet adapted to dual-screen geometry).
- `tools/emutest/smoke.sh` — the end-to-end gate; per-channel verdicts.
- `.claude/skills/emutest/SKILL.md` — how a session drives all of it, including the D4/D3
  sdmc bridge (control files in, netlogs/CSV out) and the honest-limits section.
- BUILDLOG.md per slice; HANDOFF updated.
