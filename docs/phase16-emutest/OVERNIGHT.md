# Overnight plan — 2026-08-08 → 09 (user asleep, autonomous)

User's ask: finish the harness overnight, then **prove it works** — "I know a certain place in the
app where I would easily tell you it must be fixed. So you will spot it no issue."

That is a blind test. **Do NOT bias the sweep toward the known-unfinished list**
(in-game HUD still system font, Display/Touch pause tabs not pixel-1:1, only Indigo theme built).
Sweep everything unbiased, rank by severity, report honestly. The defect may or may not be in
that list; a sweep that only re-reports known gaps proves nothing.

## Sequence

1. **[in flight]** Workflow `wf_fe2e75e3-e04` — E4 (see.py + video via ffmpeg, smoke.sh, SKILL.md),
   2 adversarial reviews, final gate. E1–E3 replay from cache (committed at `53a95b6`).
2. **On completion:** verify independently (run host tests + `smoke.sh` myself, read the table,
   confirm no azahar strays, confirm user's `qt-config.ini` byte-identical), then COMMIT.
3. **Then: THE PROOF RUN — a full visual sweep.** Launch a workflow that, for every reachable
   screen, drives the app with CTM movies, screenshots top+bottom, and READS every capture:
   - splash / boot
   - game-select (1 Game and 2 Games segmented), drag-scroll list, ZR settings entry
   - standalone settings screen: Display, Audio, Enhance, Touch tabs — every control
   - in-game: HUD bar, pause menu all 6 tabs (SESSION/DISPLAY/AUDIO/ENHANCE/LINK/TOUCH)
   - toggle states: tilt levels 0–3, DoF/bloom/light on-off, theme, touch modes
   - wireless lobby screens reachable without a second console
   Compare against `design_handoff_3dgba_ui/` references where one exists (compare.py), and judge
   standalone where none does. Look for: text overflow/clipping, wrong fonts, misaligned or
   overlapping widgets, unreadable contrast, wrong colors vs theme, plate seams,widgets off-plate,
   z-order errors (text hidden behind panels — the known citro2d depth gotcha), stretched or
   9-slice-broken sprites, and anything that simply looks wrong.
4. **Report**: ranked defect list, each with the screenshot, a zoomed crop marking the issue, the
   repro (which CTM/nav steps), and the suspected source location. One captioned image per claim
   (the PokeDNA rule). Then fix what is clearly fixable, re-capture to prove the fix.

## Robustness rules for the night

- Session limit resets ~22:10 Asia/Jerusalem; if an agent dies on limit, the workflow still
  returns — resume with `resumeFromRunId` (cached agents replay free) rather than restarting.
- Never leave an azahar running; never touch `sdmc/dual-gba` originals; restore qt-config always.
- Commit after every green milestone so nothing is lost.
- If a step is genuinely blocked, write the blocker here and continue with what IS possible —
  do not stall the whole night on one wall.
