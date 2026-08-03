# Resuming the phase13-diagnostics workflow

Stopped 2026-08-03 ~20:05 at the usage limit. Re-run when the limit resets.

## State
- **DONE + committed:** 3 specs, D1 (crumbs+watchdog), D2 (hang catcher), D3 (CSV),
  D4 (move scripts), D5 (record/replay), D6 (fingerprint), and **D7a** (laggyPair shim).
- **TODO:** D7b-D7e (see BUILDLOG's D7 entry), then the 2 adversarial reviews, the fix
  pass, and the final gate (`make cia` + HANDOFF run-#13 checklist update).
- **Tree green** at the checkpoint commit: celiolink 1171 / netlink 66 / diag 360 /
  control 6897, all PASS; `3DGBA.3dsx` builds with no new warnings.

## Preferred: resume in THIS session (run id survives while the session lives)
Workflow({
  scriptPath: "/Users/guyshtainer/.claude/projects/-Users-guyshtainer-VSCodeProjects-3ds-toolkit-projects-3DGBA/148c788c-2820-4164-be0b-618f60c5c7f5/workflows/scripts/phase13-diagnostics-wf_95967381-d4a.js",
  resumeFromRunId: "wf_95967381-d4a"
})
Specs + D1-D6 replay instantly from cache; D7 re-runs live, then review/fix/final gate.
Before relaunching, edit the r7 prompt in that script to say D7a is already landed and
green (1171 checks) and only D7b-D7e remain — same technique used for the D3 resume, so
the cached prefix (specs + D1-D6) stays valid.

## Fresh chat fallback (resumeFromRunId is same-session-only)
In the script: delete the three Spec agent() calls and r1..r6 (all complete in the tree
per BUILDLOG), keep IMPL_COMMON + r7 onward with the D7a-already-done note, then launch
with {scriptPath} only. Every inter-slice decision lives in the specs + BUILDLOG, so
nothing is lost with the dead session.
