# Overnight plan — 2026-08-14 (user asleep; loop-driven)

User's instruction: keep working through the night with loops + wake agents; start Emerald work
immediately; use two emulators in parallel if possible. Screenshots ARE allowed (user asleep,
machine free — the opposite of the earlier daytime constraint).

## Constraints that shape the sequencing

- The census workflow (wf_c626c1b2-019, FR visit + Plan synthesis) is STILL RUNNING. It invokes
  tools/emutest/*.py fresh on every call and boots the MAIN TREE's 3DGBA.3dsx. Therefore until it
  completes: NO edits to tools/emutest/, NO rebuild of the main tree, NO second default-profile
  Azahar. Work that starts now runs in a git WORKTREE and does not touch the emulator.
- Ruby/Sapphire are REV 2 (BUILDLOG note 2026-08-13): every AXVE/AXPE profile address must be
  re-verified against rev-2 pret symbol maps BEFORE any RS boot. EM cb2 values must NOT be copied
  to RS blindly — different builds.

## Stages (the loop sequences these on each wake)

S1 [NOW, worktree-isolated, no emulator] — workflow phase22-prep:
   (a) cb2 PROMOTION: promote CB2-HARVEST.md's emulator-verified [exact] identities into
       gamestate.c profiles (EM fully, FR as-harvested-so-far), extending GCTX detection for the
       screens COVERAGE.md lists as fall-through. Suites + build green IN THE WORKTREE.
   (b) SPEC-family-1: the touch design for the first two families — the naming KEYBOARD and
       VERTICAL LISTS (bag/mart/pokedex/PC item lists) — instantiated for Emerald screens first,
       from the banked catalog/coverage/evidence. Spec only.
   (c) RS REV-2 VERIFICATION (desk work): fetch pokeruby/pokesapphire rev-2 symbol maps, re-derive
       every AXVE/AXPE profile address, and write the verified rev-2 rows + a diff vs the shipped
       rows. No boot.
S2 [when census completes] — verify the census results, merge S1a into the main tree, rebuild,
   commit census + promotion. Publish the screen gallery to the artifact page.
S3 — dual-emulator harness upgrade (instance profiles: EMUTEST_STATE_DIR/AZ_DATA/RUNS_DIR,
   per-instance gdb port, open -n, PID-based window lookup in see.py, lock per instance; smoke
   gate = both instances boot different games simultaneously, each see.py capture shows ITS OWN
   game, gdb reads do not cross).
S4 — two parallel lanes on instances A/B:
   Lane A: phase-22 implementation — keyboard + list families on Emerald, emulator-proven per the
           house method (captures + GDB state reads).
   Lane B: RS/LG delta pass — first-ever RS boot on the verified rev-2 rows, RS screen deltas
           (pokenav/contest variants), LG smoke, and the Ruby+Sapphire same-console co-op proof.
S5 — commit green milestones as they land; refresh HANDOFF; final morning report with screenshots.

## Robustness

- Wakes: task-notifications are primary; ScheduleWakeup 1800s is the fallback heartbeat.
- On any agent/workflow failure: check what's banked on disk FIRST (the work usually survived);
  resume with resumeFromRunId rather than restarting; never leave the tree red or an azahar up.
- The user's qt-config.ini and dual-gba originals stay byte-identical; roms/ copies are the
  working set. Never commit ROMs/saves (gitignored; verify before any commit).
- If genuinely blocked, bank the blocker here and continue with what IS possible.

## Addendum (user, before sleep) — GREEN LIGHT + the traversal feature

User: green light to continue autonomously once the current plan lands. NEW FEATURE (own toggle):
HM-aware touch — tapping a spot beyond water/tree/rock/boulder auto-uses Surf/Cut/Rock Smash/
Strength (route to the obstacle edge, face it, A + confirm through the game's own prompt; key
injection only, party+badge eligibility checked by reads). STEP BEYOND: cross-map BFS through
WARPS — the Lavaridge case: the hot spring is fenced off on the town map, reachable only via the
Pokemon Center's back door; a tap on the spring should route enter-PC -> cross -> back-exit ->
spring, executed leg-by-leg with re-localization on each map change. SPEC tonight
(SPEC-family-traversal.md, workflow launched); implementation joins the lanes AFTER keyboard/lists
or in the morning phases. Lavaridge + a Surf pond + a Cut tree = the canonical proof scenarios.

## S1 RESULTS (all green) + the S2 merge checklist

1. cb2 PROMOTION ready on branch `phase21-cb2-promotion` (worktree wf_6488b05b-da8-1, commit
   d370f2a, parent 4afc01e): 40 [exact] fingerprints -> GCTX_TITLE + GCTX_FULLUI; FR-rev1 battle
   detection un-deaded (battleMainCbAlt 0x08011114); tilt/presence gates fixed with ZERO logic
   change; 14 suites green in the worktree. MERGE at S2:
   `git fetch <worktree>/projects/3DGBA phase21-cb2-promotion && git merge FETCH_HEAD`
2. KEYBOARD + LISTS specs banked (SPEC-family-keyboard.md / SPEC-family-lists.md) — lane A input.
3. RS REV-2: **green-lit** — shipped AXVE/AXPE rows verified SAME across all six maps incl. rev2;
   RS boot may proceed on the address side. COLLATERAL FINDING for S2: the BPGE row carries
   FR-rev0 ROM pointers wrong for EVERY LeafGreen revision (battle/party/bag/menu detection
   silently dead on LG; fail-safe compare-only). Exact LG-rev1 replacements are ready-to-paste in
   RS-REV2-VERIFICATION.md — apply them in the same S2 merge commit, cited, marked
   sym-derived / verify-in-emulator (the LG delta pass then proves them live).
4. Also flagged, do NOT silently fix: BPRE/BPGE newKeys 0x0303011E is a digit transposition of
   0x0300311E — confirmed unused by any code; fix only with its own citation + suite pin.

## Addendum 2 (user): PUSH THROUGH + 2-HOURLY GALLERY

- Do NOT stop at the morning report. Keep implementing touch families (keyboard, lists, grids/PC,
  map, traversal slices) past morning until the plan is exhausted or the user intervenes. The
  morning report is a milestone, not a stop condition.
- EVERY ~2 HOURS: the loop republishes the artifact page (same URL) with the newest screenshots
  (census evidence + implementation proofs). Publish timestamp tracked in
  docs/phase21-touch-census/.last-gallery-publish; only the main session can publish (not agents).
