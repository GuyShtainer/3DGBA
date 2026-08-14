# Overworld gesture decisions (user, 2026-08-14)

Binding design calls from the user. Implement in the overworld touch handler + the route
follower; the traversal route-PROGRAM layer (SPEC-family-traversal T2) is the natural home for
the run half, since it already owns per-leg key masks.

## D1 — Tap self = START, HOLD self = SELECT

Replaces today's `tap-self = A` / `double-tap = START` (COVERAGE.md §1).

- **Tap** on the player's own tile → START (the field menu). Fires on RELEASE, immediately.
- **Hold** on the player's own tile → SELECT (the registered item). Fires the moment the hold
  threshold is crossed, while still held; the subsequent release must NOT also fire the tap.
- Rationale, and why NOT double-tap (offered, user agreed): a double-tap binding forces EVERY
  single tap to wait out the double-tap window (~250 ms) before it can fire, so the most-used
  action becomes the laggiest. Tap-vs-hold costs nothing: tap resolves on release, hold resolves
  on its own timer.
- Threshold: reuse the existing hold/drag conventions rather than inventing a third
  (UIHIT_DRAG_PX slop for "did not move", and a hold threshold consistent with the list-family
  fling/hold timing). A finger that MOVES past the slop is a drag/steer, not a tap or a hold.
- **`A` is not lost:** it remains available by tapping the thing you want (tap-to-route terminates
  on the blocked goal and interacts — the phase-18 door/NPC terminals), and by the tap-advance
  class on dialogs. If a live pass shows a real gap (an interaction reachable ONLY by pressing A
  while standing still), raise it — do not silently re-add a third self-gesture.

## D2 — Distance decides walk vs run

- A tap-to-walk route **runs** when the routed distance is at or beyond a threshold, and **walks**
  when it is shorter. "Close = walk, far = run."
- Threshold: start at **4 tiles** (path length, not straight-line — a short hop around a corner
  should still walk). Make it a named constant so it is tunable after a hardware feel-test.
- Mechanism is already proven: the D4 control grammar's sprint mode holds `KEY_B` in the same
  mask as the direction (control.c:151, lowercase direction letters). The touch route follower
  currently never presses B — that is the whole gap.
- **Eligibility gates — verify each from the decomp, then degrade SILENTLY to walking if any
  fails** (never stall, never spam B):
  1. Running Shoes obtained (the B-dash flag; get its id per engine and cite it).
  2. The current map permits running — Gen-3 disallows it in several map types (indoors in
     particular). Confirm the actual rule from pret rather than assuming; the check the game
     itself uses is the one to mirror.
  3. Not already in a state that overrides speed: surfing, biking, or any forced movement.
  4. Terrain that cancels a dash mid-route (ice, ash, mud, sand) — the route must not fight the
     game; if the dash drops, keep routing at whatever speed the game gives.
- Interaction with traversal: a route that crosses a Surf/Cut/Smash edge should re-evaluate per
  LEG (the on-foot leg may run; the surf leg has its own speed and must not hold B).
- Interaction with the HM/warp excursions: legs are independent, so decide per leg, not once for
  the whole program.

## Proof plan (both)

- Host: gesture-resolution table (tap vs hold vs drag from a synthetic touch timeline), and the
  walk/run decision as a pure function of (path length, eligibility flags) with goldens.
- Emulator: tap own tile → START menu opens (capture + the start-menu context read over gdb);
  hold own tile → registered item fires (capture); a 2-tile route shows no B in the injected mask
  while a 10-tile route holds B and completes measurably faster (frame count between the same two
  tiles, read from the position series).
