# Resuming phase 16 (emutest harness)

Updated 2026-08-08 at the end of slice **E4**. (The original pause note — waiting for the
user to restart VS Code so the Screen Recording / Accessibility grants applied — is
resolved: both grants are live and verified, see below.)

## State

- Specs DONE and committed: `PHASE.md`, `pokedna-harness-report.md`, `SPEC-harness.md`,
  `SPEC-protocols.md` (the last one CORRECTED by E2/E3/E4 findings — read BUILDLOG before
  trusting a master-branch citation in it).
- Implementation **E1–E4 DONE and live-proven** (`tools/emutest/`): azctl (RUN), gdbio
  (READ-STATE), ctm + sdmc (PRESS), see incl. the `rec` video channel (SEE), compare/zoom/
  sheet, smoke.sh, setup.sh, 125 host tests, `.claude/skills/emutest/SKILL.md`.
- **`smoke.sh --rom` = 7/7 PASS, exit 0** (run / read-state / press-ctm / press-d4 / sdmc /
  see / see-rec). `./setup.sh` (venv → host tests → smoke) is the one-command gate.
- Not done in E4: the phase's review pass + final gate/HANDOFF update (the orchestrator's
  remaining steps). No app `source/` change was made or is needed.

## Start here in a new session

```bash
cd projects/3DGBA
./tools/emutest/setup.sh          # rebuilds the venv if needed, host tests, live smoke
# then read .claude/skills/emutest/SKILL.md — it is the operating manual
```

Per-slice detail, every measured number and every honest deviation live in `BUILDLOG.md`
(E4's entry has the SEE/VIDEO evidence, the single-window trap, and the rate measurements).

## Environment facts that bite (all live-verified)

- Launch Azahar via the **.app bundle** (`open -a`), never `Contents/MacOS/azahar` — the raw
  binary pops a blocking modal. GUI launches from the Bash tool may need
  `dangerouslyDisableSandbox: true`.
- Every `use_gdbstub` boot **parks** until `gdbio resume`; one gdb client per boot.
- Screen Recording: granted + working (window contents visible). Accessibility: granted —
  **`CGEventPost` works**; `osascript`/System Events is still denied (-1743), do not use it.
- Azahar is single-window: the same window shows the game list when no app is running, so a
  crop only means something while `g_renderSeq` advances.
