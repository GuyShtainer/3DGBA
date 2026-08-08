# Resuming phase 16 (emutest harness)

Paused 2026-08-08 so the user can quit/reopen VS Code to apply the Accessibility +
Screen Recording grants (granted to **Visual Studio Code**, not the "Claude" desktop app).

## State at pause
- PHASE.md + pokedna-harness-report.md + **SPEC-harness.md DONE** (committed).
- SPEC-protocols.md (CTM format / gdb stub / config keys / window geometry) was IN FLIGHT —
  not written; its agent re-runs on resume.
- Implementation E1-E4, reviews, final gate: NOT started. tools/emutest/ does not exist yet.
- No Azahar strays; user's qt-config.ini untouched (profile management not yet built).

## Resume (after restart, user says "continue")
Same session id (conversation resumed): relaunch with
  Workflow({ scriptPath: "/Users/guyshtainer/.claude/projects/-Users-guyshtainer-VSCodeProjects-3ds-toolkit/148c788c-2820-4164-be0b-618f60c5c7f5/workflows/scripts/phase16-emutest-wf_8ce6730f-57e.js",
             resumeFromRunId: "wf_8ce6730f-57e" })
— SPEC-harness replays from cache; SPEC-protocols runs live; then E1-E4 → reviews → gate.
Fresh session fallback: launch the same scriptPath without resumeFromRunId (re-runs both specs;
SPEC-harness.md on disk makes that cheap) — or edit the script to read the committed spec.

## First checks after the restart (permissions now granted?)
  osascript -e 'tell application "System Events" to key code 49'   # no -1743 = Accessibility OK
  screencapture -x -o /tmp/t.png                                    # windows visible = Screen Recording OK
Expected end state of the phase: tools/emutest/smoke.sh prints RUN/READ-STATE/PRESS/SEE all PASS.
