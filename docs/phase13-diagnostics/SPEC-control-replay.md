# SPEC — D4 file-driven tile-exact movement + D5 input record/replay

Phase 13-prep slices **D4** and **D5** (`PHASE.md` Scope-IN rows D4/D5). Source design:
`docs/kb/external/pm-bridge-forensics.md` — the melonDS move/go grammar (PART 2 #10,
EmuThread.cpp:275-408; #9 go-files, EmuThread.cpp:199-235), the DeSmuME record/replay
(PART 1 #2, mp_bridge.cpp:464-477 + 1584-1647), and the recipe discipline (#7 of the
ranked checklist + patterns §5). The PHASE.md **Invariants bind this spec**: the trade
path is FROZEN (Inv 1), everything active is flag-gated default-off (Inv 2), suites grow
(Inv 3), parsers are pure C (Inv 4), build proof after the slice (Inv 5), citations (Inv 6),
BUILDLOG entry (Inv 7), hardware-final (Inv 8).

Everything below was resolved against the ACTUAL source (file:line cites are from the
working tree as of 2026-08-03).

---

## 0. The existing key-injection seam (resolved — D4/D5 reuse it verbatim)

The comment at `gamestate.h:37` ("unused — we inject via the returned key mask") points at
this pipeline, confirmed end-to-end:

1. **Producer** — `touch_update()` returns a `u16` GBA key mask each render frame
   (`touch.h:54-59`): "Advance touch one frame; returns the GBA key mask to OR into the
   bottom game." Stored in the per-frame local `u16 tk` (`main.c:1402`, assigned at
   `main.c:1498`). Touch never writes `gMain.newKeys` in game RAM — injection is at the
   emulated KEYINPUT level, upstream of the game.
2. **Assembly** — real 3DS keys become a GBA mask via `to_gba_keys(kHeld)`
   (`main.c:252-265`, bit order = `GBAKEY_*`, `gbacore.h:15-19`) into local `u16 g`
   (`main.c:1469`), then the per-seat routing at **`main.c:1580-1583`**:

   ```c
   if (single) { emuA.keys = g | tk; emuB.keys = 0; }   // one game: pad + touch both drive it
   else {
       emuA.keys = ((focused == 0) ? g : 0) | (swapped ? tk : 0);
       emuB.keys = ((focused == 1) ? g : 0) | (swapped ? 0 : tk);
   }
   ```

   `EmuInstance.keys` is a `volatile u32` (`main.c:49`) written only here by the main
   thread.
3. **Consumer** — every worker path calls `gbacore_set_keys(e->core, (u16)e->keys)`
   before running the core: unlinked `emu_step` (`main.c:153`), in-process link runLoop
   (`main.c:175`), and — crucially — the **wireless `netLinked` free-run loop at
   `main.c:202`**, once per run-slice. `gbacore_set_keys` (`gbacore.h:37`) feeds mGBA's
   keypad; it never touches the SIO driver.

**Why D4/D5 need zero SIO/link changes (Invariant 1):** the injected mask enters at the
same point the player's thumbs do — the emulated keypad. The celiolink FSM, netlink
rounds, and the gbacore SIO fill sit entirely downstream of the game's own input handling
and are not on this path. The wireless worker already re-reads `e->keys` every slice
(`main.c:202`), so per-seat script keys flow into a live wireless session with **no new
code** on the link side. D4/D5 extend only the assembly expressions at `main.c:1580-1583`
(OR in a per-seat script mask) plus read-only file polling — additive, exactly what
PHASE.md Invariant 1 allows ("flag-gated input injection at the existing injection point").

**Per-seat naming.** `p1` = game **A** (slot A core, `emuA`), `p2` = game **B** (`emuB`),
fixed, independent of `swapped`/`focused`/screen. Rationale: each 3DS has its own SD card,
so PM's role-suffixed-files-in-a-shared-cwd problem (pm-bridge-forensics.md #10) does not
exist here — the operator stages each console's own `move_p1.txt`. During a wireless
session the participant is "the focused game at link start" (HANDOFF, commit `bfde410`);
the operator uses the file matching the slot they focused. The control log echoes the
mapping at session start (`# control p1=<gamecodeA> p2=<gamecodeB>`, D4.14).

**Live game state comes free.** The injection site sits immediately after the existing
per-frame `game_read()` calls for BOTH games (`main.c:1559-1579`: `gst` for the top core,
`gsb` for the bottom core, every frame, both dual and wireless — the same "benign EWRAM
race" the SMART-touch path accepts, comment `main.c:1478`). D4's closed-loop coordinates
are taken from these already-read `GameState` snapshots; **no new RAM reads**. Mapping
slots→snapshots: game A's snapshot is `swapped ? gsb : gst`, game B's is
`swapped ? gst : gsb` (from `main.c:1562-1563`).

**The scheduler clock is the target core's EMULATED frame counter** —
`gbacore_frame_counter()` (`gbacore.h:85`), sampled per render frame at the injection
site. All token timers, tap slots, waits, and deadlines count emulated frames of the
scripted core. Consequences (all desirable):

- Wireless at ~4-5 emu-fps: waits/taps scale with the game's real rate instead of wall
  time (a `W60` is one game-second, not one wall-second).
- Pause menu open (`menuOpen` skips the whole input block, `main.c:1413`), app
  backgrounded (`main.c:1397`), or seat paused (the wireless peer game is paused,
  unpause at `main.c:1598`): the counter stops → the script freezes in place instead of
  timing out or blind-firing.
- A wedged game freezes its script visibly (last status line names the active token);
  diagnosing the wedge is D2's job (the hang catcher), not D4's.

---

## D4 — file-driven tile-exact movement + go-files

### Files and lifecycle

- **D4.1** Control files live in **`sdmc:/cias/control/`**: `move_p1.txt` / `move_p2.txt`
  (scripts), `go_p1.txt` / `go_p2.txt` (one-shot triggers, D4.10). The whole feature is
  **enabled iff the directory `sdmc:/cias/control` exists**, checked ONCE per
  `run_session` (`main.c:1278`) start. No directory → zero polls, zero overhead
  (Invariant 2 flag-gated default-off; the operator opts in by `mkdir`-ing once, no UI
  needed).
- **D4.2** Poll cadence: when a seat has **no active script**, `stat()` its `move_p<N>.txt`
  every **10 render frames**, seats staggered (seat A at `frame % 20 == 0`, seat B at
  `frame % 20 == 10`) so at most one sdmc `stat` happens per frame. This mirrors PM's
  10-frame idle poll (EmuThread.cpp:275-408) and satisfies Invariant 2: no fopen on the
  hot path — `fopen` happens only on the poll tick where `stat` succeeded.
- **D4.3** **Consumed on pickup**: on a successful `stat`, read the whole file (cap
  **512 bytes**; larger → reject loudly, still remove), `remove()` it, then parse. A
  parse error consumes the file, logs `[ctl pN] parse error: <why>` and queues nothing —
  the operator re-drops a fixed file. (PM consumes on pickup so a stale script can never
  re-fire; EmuThread.cpp:296-308.)
- **D4.4** A file whose first non-whitespace byte is **`!`** is an ABORT file: clear the
  seat's active token + queue **and any armed/running replay (D5)**, remove the file, log
  `[ctl pN] ABORT by file`. (PM: EmuThread.cpp:296-308.) Abort files are honored even
  mid-script — this is the poll-cadence exception: while a script IS active, the 10-frame
  poll still stats `move_p<N>.txt` but only acts if the file starts with `!` (a non-abort
  file dropped mid-script is left in place untouched, picked up when the script ends).

### Grammar (adopted from PM, PART 2 #10, with GBA-specific mappings)

- **D4.5** Whitespace-separated tokens, **max 64 tokens** per file:

  | Token | Meaning | Mechanics |
  |---|---|---|
  | `L<n> R<n> U<n> D<n>` (n ≥ 1) | walk n tiles, closed-loop | hold that d-pad bit until the live tile hits target (D4.6) |
  | `l<n> r<n> u<n> d<n>` (n ≥ 1) | sprint n tiles | same closed loop, mask also holds `1<<GBAKEY_B` (Running Shoes; indoors the game ignores B and walks — the closed loop doesn't care) |
  | bare `L R U D` / `l r u d` | one TAP | pressed **4** emu frames of a **16**-frame slot — face-turn / menu navigation (case does NOT add B on a bare tap; PM treats n==0 as tap, EmuThread.cpp:334-346) |
  | `a b s c x y` | button tap | A, B, START, SELECT, **L**, **R** — pressed **12** emu frames of a **40**-frame slot (PM: EmuThread.cpp:367-381). **The GBA has no X/Y**: PM's `x y` letters are remapped to GBA `L`/`R` (`1<<GBAKEY_L`, `1<<GBAKEY_R`). Letters `{a,b,s,c,x,y}` are disjoint from the direction letters, so the grammar stays unambiguous |
  | `W<n>` / `w<n>` | wait n emu frames (n ≤ 3600) | inject nothing |
  | `G` | go-gate — FIRST token only | queue parses but nothing runs until `go_p<N>.txt` appears (D4.10) |

  Tap/press frame constants live in one `#define` table in `control.h`
  (`CTL_TAP_DIR_PRESS/SLOT`, `CTL_TAP_BTN_PRESS/SLOT`) — they are
  **verify-on-hw-pending**: the Gen-3 turn-vs-step threshold (pret
  `src/field_player_avatar.c`, turn-then-walk-if-still-held) has not been frame-counted
  on hardware; PM's 12/34-40 numbers are DS-era. Tune after run #13, one place.
- **D4.6** **Closed-loop walk**: at token start, latch `start = (px, py)` from the seat's
  GameState snapshot — **`GsSnap px/py`**, read as the first two `s16` behind the
  dereferenced **`GameProfile.sb1ptr`** (`gSaveBlock1Ptr` → SaveBlock1 `pos.x/pos.y`;
  `gamestate.h:25`, read at `gamestate.c:86-90`, guarded by `sb1Valid` at
  `gamestate.c:87`). Target = start ± n on the moved axis. Every tick, hold the direction
  (+B if sprint) until the live moved-axis coordinate **equals** the target. Compare the
  moved axis only; if the cross axis drifted, complete anyway but log
  `[ctl pN] tok <i> cross-drift (<dx>,<dy>)` (a diagnostic, not a failure — currents/
  forced-slide tiles do this legitimately).
- **D4.7** **Warp completes a walk token**: if `mapGroup`/`mapNum` change mid-token
  (SaveBlock1 `location` bytes, `gamestate.c:91-92`), the token completes successfully —
  the step onto a warp tile teleports the player into a NEW coordinate space, so the
  "target" comparison is meaningless and the walk achieved its purpose (it reached the
  warp). Scripts must be written so a walk token ENDS at the warp and a `W<n>` rides the
  fade (Appendix rule R4).
- **D4.8** **Per-token wall(-blocked) timeout**: deadline = **`n*60 + 240` emulated
  frames** from token start (PM's guard, EmuThread.cpp:349-361 — generous vs the ~16
  frames/tile walk cost, so only a genuine wall/NPC block trips it). On expiry: **abort
  the remaining script** (not skip-to-next), log
  `[ctl pN] TIMEOUT tok <i> <tok> at (<px>,<py>)`. Rationale: every later walk target is
  start-relative; continuing after a missed walk executes the rest of the choreography
  from the wrong tile — silently wrong beats loudly stopped (the touch-arc reliability
  rule, auto-memory `touch-reliability-over-piling`).
- **D4.9** **Coordinate validity**: a walk token only STARTS when the seat's snapshot has
  `valid && ctx == GCTX_OVERWORLD && sb1Valid && px >= 0` (fields per `gamestate.h:80-104`;
  note `GCTX_OVERWORLD` is the fall-through ctx, `ctxResolved` stays false there —
  `gamestate.h:102-103` — so do NOT require `ctxResolved`). While invalid the token waits
  (its deadline NOT yet armed); if invalid for **600 consecutive emu frames**, abort with
  `[ctl pN] NO-FIELD tok <i>`. Taps/waits have no coordinate precondition (they are used
  inside menus deliberately).

### Go-files (one-shot, re-armable — PM #9)

- **D4.10** A script whose first token is `G` parses + queues, then holds before token 0.
  While holding, `stat` `go_p<N>.txt` every 10 render frames; on found: `remove()` it
  (consume), log `[ctl pN] GO`, start the script. The go file is one-shot and re-armable
  by nature (consumed each time; the NEXT `G`-script waits for the next touch of the
  file) — the "on my command" trigger for two-console choreography
  (EmuThread.cpp:199-235; mp_bridge.cpp:464-470: the operator stages the scene by hand,
  then touches the file). A `go_p<N>.txt` with no waiting script is left in place (it
  will gate the next `G`-script; delete it manually if stale).

### Real input wins — the abort rule

- **D4.11** **Any real input targeting the scripted seat ABORTS that seat's script (and
  replay) immediately.** "Real input" = the seat's non-script mask contribution as
  already routed at `main.c:1580-1583`: for game A, `single ? (g|tk) :
  ((focused==0?g:0) | (swapped?tk:0))`; mirrored for B. Nonzero → abort + log
  `[ctl pN] ABORT real-input at tok <i>` and inject nothing that frame. Decision
  rationale (charter asked pause-vs-abort):
  - PM's own line: record NEVER overrides live input (mp_bridge.cpp:1614 early-return) —
    generalized here to "the human always outranks the harness".
  - A pause-and-resume script is a lie after manual movement: walk targets are latched
    start-relative (D4.6), so resuming from an operator-moved tile silently walks wrong
    paths — the exact class of quiet corruption the touch arc taught us to refuse
    (auto-memory: reliability first, verify one thing at a time).
  - Abort is loud (status line names the token) and cheap to recover (drop the file
    again — it was consumed anyway).
  Keys that do NOT reach the seat's mask never abort: 3DS-level HUD keys (Y/X/ZL/ZR,
  menu combo), touch while `touchMode == TOUCH_OFF`, and pad input for the OTHER
  (unfocused) game — by construction, since the trigger is the routed expression itself.
- **D4.12** Script injection is **strictly OR-only** into the existing assembly — the
  final per-seat expressions become e.g.
  `emuA.keys = <real-A> | ckA;` where `ckA` is the control mask. No existing bit is ever
  cleared, no write happens anywhere else (Invariant 1: additive).

### Status echo into the netlog

- **D4.13** `control.c` (pure C) never does file I/O; it queues status lines in a small
  ring (**16 lines × 96 chars** per seat) drained by the glue every frame. The glue
  appends them to **`sdmc:/cias/netlogs/3DGBA_control_<MMDD>_<HHMMSS>.txt`** — opened
  ONCE, lazily, on the first drained line; `fprintf` + `fflush` per line. Justification
  vs Invariant 2 ("no fopen on the hot path, open once, flush on cadence"): status events
  are rare (pickup/GO/token transitions/abort — a few per minute), and per-line fflush is
  the PM crash-safety rule (mp_bridge.cpp §1 "append + fflush per line (crash-safe)") —
  these scripts run precisely in hang-prone hardware sessions where a buffered ring dies
  with the power switch (the run-#11 lost-log lesson: HANDOFF Next-steps #2 says "POWER
  OFF — don't press Quit"). Same `mkdir("sdmc:/cias", …); mkdir("sdmc:/cias/netlogs", …)`
  preamble as the existing writer (`gbacore.c:834-835`); same timestamped-name pattern as
  `gs_dump` (`main.c:120-135`). This reuses the established netlog folder + naming +
  writer pattern; it does NOT invent a new transport.
- **D4.14** Cheap cross-links into the EXISTING logs (all additive):
  - The gs log already carries an injected-keys column: `gs_log_sample(..., injKeys, ...)`
    (`gamestate.h:150-151`). Today the bottom row gets `tk` and the top row gets `0`
    (`main.c:1575, 1578`); the glue passes `tk|ck<bot>` and `ck<top>` respectively, so
    script keys appear in the same timeline as ctx/geo — free correlation.
  - One `# control` summary line in the wireless netlog header: `control.c` exports
    counters (`ctl_counters`: tokens done, aborts, timeouts, files picked up) via two
    globals mirrored the way `s_celio*` mirrors celiolink state; `gbacore_net_log_dump`
    (`gbacore.c:833`) gains one `fprintf` next to the `# celio` line (`gbacore.c:896`).
  - Session-start mapping echo: `# control p1=<codeA> p2=<codeB> dir=1` as the control
    log's first line (game codes via `gbacore_game_code`, `gbacore.h:96`).

---

## D5 — input record/replay

### Recording

- **D5.1** Armed by a marker file **`sdmc:/cias/control/record_p<N>.txt`** (content
  ignored), picked up by the same 10-frame poll, consumed on pickup, logged
  `[ctl pN] record armed`. Recording is **passive**: it reads the seat's final assembled
  mask and injects nothing, so it can never fight live input (PM's rule, mp_bridge.cpp:1614,
  satisfied by construction).
- **D5.2** **Anchor = overworld (field) entry edge**, detected on the seat's GameState
  snapshot as the predicate `valid && ctx == GCTX_OVERWORLD && sb1Valid && px >= 0`
  (same fields as D4.9) holding for **3 consecutive render frames** (debounce), edge =
  first frame after a non-field state. On the edge, latch
  `anchorFrame = gbacore_frame_counter(core)` and log `[ctl pN] record anchored f=<n>`.
  Known weakness (documented, accepted): `GCTX_OVERWORLD` is the fall-through context
  (`gamestate.h:102-103`), so an UNDETECTED save-loaded screen (Pokénav, PC…) also
  matches; discipline: arm recording while parked in a menu or pre-field so the first
  matching edge IS the field entry (Appendix R2 — park the save at the scene). PM's
  equivalent anchor was "overworld frameCounter advancing" (mp_bridge.cpp:1379-1385) and
  carries the same class of caveat.
- **D5.3** Output: **`sdmc:/cias/netlogs/rec_p<N>_<MMDD>_<HHMMSS>.txt`** (an artifact of
  the run → netlogs, which the run workflow archives; the operator copies it to
  `control/replay_p<N>.txt` to replay). Header lines start `#` (skipped by the loader):
  `# 3DGBA rec v1 seat=pN game=<code> anchored=<emuFrame> date=<...>` plus a free-text
  `# save:` line the operator can fill in. Body lines: **`<frameOffset> <mask>`** —
  `frameOffset` = emu frames since anchor (decimal), `mask` = the seat's final injected
  mask that frame in **hex, GBA KEYINPUT bit order** (`GBAKEY_*`, `gbacore.h:15-19` —
  NOT PM's DS layout; documented in the header line). **On-change only** (first line at
  the anchor with the then-current mask, typically `0`), `fflush` after every line
  (crash-safe, PM rule), cap **65536 lines** → stop recording + `[ctl pN] record cap`.
- **D5.4** The recorded mask is the seat's **final assembled mask** (`emuA.keys` /
  `emuB.keys` value as computed this frame, i.e. real pad + touch + any script keys) —
  what the core actually received is the only thing that replays faithfully.
- **D5.5** Recording stops (file closed, status logged) on: session end, the abort file
  (D4.4), or the cap. It does NOT stop on menu-open/pause — those freeze the emu frame
  counter, so they simply add no lines (the offset clock is emulated frames, D0).

### Replay

- **D5.6** Table file: **`sdmc:/cias/control/replay_p<N>.txt`** (the record format,
  `#` lines skipped). **Armed by `sdmc:/cias/control/replay_go_p<N>.txt`** (one-shot,
  polled at the 10-frame cadence, consumed on trigger — exactly the charter's file
  name). On arming: load + parse the whole table; **cap 8192 entries** — deliberately
  below PM's 65536 (desktop) because the table is heap-resident next to two mGBA cores
  (6 B/entry → 48 KB; 8192 on-change entries ≈ tens of minutes of play). An
  over-cap or malformed table **fails loudly** (`[ctl pN] replay load error …`, nothing
  armed, files consumed) — never a silent truncation.
- **D5.7** After arming, replay waits for the SAME field-entry edge as D5.2, latches its
  own anchor, then per tick advances
  `while (idx < n && tab[idx].f <= emuFrame - anchor) latest = tab[idx++].mask;` and
  injects `latest` (ORed like a script mask, D4.12). PM's exact loop
  (mp_bridge.cpp:1616-1647). Done when `idx == n` and `latest` has been released →
  `[ctl pN] replay done` (one status line).
- **D5.8** Real input aborts replay under the D4.11 rule (identical trigger + log). The
  abort file (D4.4) clears an armed-or-running replay too.
- **D5.9** **Determinism assumptions — documented, operator-facing** (from
  mp_bridge.cpp:464-470 plus our platform facts; put verbatim in a comment block in
  `control.h`):
  1. The replaying console starts from the **same battery `.sav`** (and the same ROM
     rev) as the recording — "the replay instance must start from the SAME save/state
     as the recording".
  2. The **field-entry anchor washes out boot/menu timing** — frame offsets are relative
     to the aligning event, not to power-on.
  3. Same starting tile + facing (park the save at the scene, Appendix R2).
  4. RTC caveat: Emerald's RTC seeds berry/tide/Feebas state — irrelevant over
     minutes-long scripts, noted for honesty; mGBA's RTC is host-clock-derived, so two
     runs are not RTC-identical.
  5. **Wireless sessions are NOT deterministic** (radio timing, peer's game) — replay is
     a solo/local repro + regression tool, and the two-console **Tier-3 determinism
     experiment** (record seat A on console 1, replay from the same save on console 2,
     compare state checksums — pm-bridge-forensics.md checklist #3) is exactly the
     experiment this enables; divergence there is a RESULT, not a bug in D5.

---

## C — the pure-C split, full API, glue, tests

### Module layout

- **C.1** New files `source/control.c` + `source/control.h` — **pure C** (PHASE Inv 4 /
  CLAUDE.md rule 4): includes limited to `<stdint.h> <stdbool.h> <string.h> <stdio.h
  (snprintf only)>`; **no libctru, no file I/O, no clocks**. All sdmc `stat/fopen/
  remove/fprintf` and all `GameState`/core reads live in the `main.c` glue. The module
  is buffer-in (file text, per-tick snapshots) / buffer-out (key mask, status lines,
  record lines) so the whole grammar + scheduler + record encoder unit-tests on the PC.
  Makefile auto-globs `source/` (toolkit convention) — no build edits.
- **C.2** House style: heavy `//` comments citing pm-bridge-forensics.md sections +
  pret/kb lines exactly like `celiolink.c`; tabs as in surrounding code; every game-RAM
  address named in comments cites its pret symbol + verified/pending status (this spec's
  addresses are all EXISTING `gamestate.c` reads — already verified; D4/D5 add **no new
  game-RAM addresses**).

### `control.h` API (complete)

```c
// control.h — D4/D5: file-driven tile-exact movement + input record/replay (pure C).
// Design: docs/kb/external/pm-bridge-forensics.md (PART 2 #9/#10, PART 1 #2);
// spec: docs/phase13-diagnostics/SPEC-control-replay.md. No libctru — the glue
// (main.c) owns all sdmc I/O and feeds per-tick snapshots.
#pragma once
#include <stdint.h>
#include <stdbool.h>

// Tunables (one place; verify-on-hw-pending per SPEC D4.5)
#define CTL_FILE_MAX      512    // move/replay-table file byte cap (D4.3)
#define CTL_TOK_MAX       64     // tokens per script (D4.5)
#define CTL_TAP_DIR_PRESS 4      // bare-dir tap: pressed frames (D4.5)
#define CTL_TAP_DIR_SLOT  16     //               slot frames
#define CTL_TAP_BTN_PRESS 12     // button tap: pressed frames (PM EmuThread.cpp:367-381)
#define CTL_TAP_BTN_SLOT  40
#define CTL_WALK_DL(n)    ((uint32_t)(n) * 60u + 240u)  // walk deadline, emu frames (D4.8)
#define CTL_NOFIELD_DL    600    // walk-token field-validity wait cap (D4.9)
#define CTL_WAIT_MAX      3600   // W<n> cap
#define CTL_STATUS_N      16     // status ring depth (lines)
#define CTL_STATUS_LEN    96     // status line cap
#define CTL_REC_MAX_LINES 65536u // record line cap (D5.3)
#define CTL_REP_MAX       8192   // replay table entry cap (D5.6)

// Per-tick input snapshot, assembled by the glue at the existing parked-window
// read site (main.c ~1559-1583) from the ALREADY-read GameState (no new RAM reads).
typedef struct {
	uint32_t emuFrame;    // gbacore_frame_counter(core) — THE scheduler clock (SPEC §0)
	bool     fieldValid;  // gs.valid && ctx==GCTX_OVERWORLD && sb1Valid && px>=0 (D4.9/D5.2)
	int16_t  px, py;      // GsSnap px/py = SaveBlock1 pos (gamestate.c:89-90)
	int16_t  mapGroup, mapNum;  // SaveBlock1.location (gamestate.c:91-92); -1 unknown
	uint16_t realKeys;    // the seat's NON-script routed mask this frame (abort trigger, D4.11)
	bool     goSeen;      // glue consumed go_p<N>.txt this tick (D4.10)
} CtlIn;

// ---- D4 script scheduler (one per seat) ----
typedef struct CtlSched CtlSched;   // fully defined in control.h (embeddable, no malloc)
void     ctl_init(CtlSched* cs, int seat);              // seat 0 = p1/game A, 1 = p2/game B
// Parse a move-file body (NUL-terminated, <= CTL_FILE_MAX). Returns token count,
// 0 for an abort file ('!' — also clears sched+replay via the glue calling ctl_abort/
// ctl_rep_abort), or -1 with err filled. Never starts running by itself if token 0 is 'G'.
int      ctl_load(CtlSched* cs, const char* text, char* err, int errCap);
bool     ctl_active(const CtlSched* cs);                // glue polls move files only when false
bool     ctl_waiting_go(const CtlSched* cs);            // glue polls go_p<N>.txt only when true
void     ctl_abort(CtlSched* cs, const char* why);      // file-/input-/glue-driven abort
uint16_t ctl_tick(CtlSched* cs, const CtlIn* in);       // returns mask to OR into the seat
// Drain ONE queued status line into line[cap]; returns length or 0 when empty.
int      ctl_status(CtlSched* cs, char* line, int cap);
// Counters for the '# control' netlog summary (D4.14): any out-param may be NULL.
void     ctl_counters(const CtlSched* cs, uint16_t* toksDone, uint16_t* aborts,
                      uint16_t* timeouts, uint16_t* pickups);

// ---- D5 record encoder (one per seat; passive) ----
typedef struct CtlRec CtlRec;
void     ctl_rec_init(CtlRec* r, int seat);
void     ctl_rec_arm(CtlRec* r);                        // record_p<N>.txt consumed (D5.1)
bool     ctl_rec_armed(const CtlRec* r);
// Feed one tick + the seat's FINAL assembled mask (D5.4). Returns the length of a
// "<frameOffset> <mask>\n" line written into line[cap] when one is due (anchor latch +
// on-change handled inside), 0 when silent, -1 exactly once when the cap stops recording.
int      ctl_rec_tick(CtlRec* r, const CtlIn* in, uint16_t finalMask, char* line, int cap);

// ---- D5 replay (one per seat) ----
typedef struct CtlRep CtlRep;
void     ctl_rep_init(CtlRep* r, int seat);
// Parse a replay table ('#' lines skipped; "<off> <hexmask>" rows; cap CTL_REP_MAX —
// over-cap/malformed = -1 with err filled, NOTHING armed; loud not truncated, D5.6).
int      ctl_rep_load(CtlRep* r, const char* text, char* err, int errCap);
bool     ctl_rep_active(const CtlRep* r);
void     ctl_rep_abort(CtlRep* r, const char* why);
uint16_t ctl_rep_tick(CtlRep* r, const CtlIn* in);      // waits for the field edge, then plays
int      ctl_rep_status(CtlRep* r, char* line, int cap);
```

(Sched/Rec/Rep structs are fully declared in the header — fixed-size arrays, no
allocation; the replay table is the one sized buffer: `CTL_REP_MAX` × `{uint32_t f;
uint16_t mask;}`.)

### main.c glue (thin, all sdmc I/O here)

- **C.3** Session init (top of `run_session`, `main.c:1278`): one `stat` of
  `sdmc:/cias/control` → `ctlEnabled`; init the six module states; `FILE*`s NULL.
- **C.4** Per-frame, inside the existing `!menuOpen` input block, AFTER the gs reads
  (`main.c:1559-1579`) and REPLACING nothing — the assembly at `main.c:1580-1583` gains
  one OR-term per seat:
  1. Build `CtlIn` per seat from `gst`/`gsb` (slot mapping per §0) +
     `gbacore_frame_counter` + the routed `realKeys` expression (D4.11) + `goSeen` from
     the staggered polls.
  2. `ck = ctl_tick(...) | ctl_rep_tick(...)` per seat; OR into the assembly; pass
     `tk|ck` / `ck` into the two `gs_log_sample` calls (D4.14).
  3. `ctl_rec_tick(seat, in, finalMask, ...)` with the just-assembled `emuX.keys`;
     append+fflush any returned line to the seat's rec file (opened on anchor).
  4. Drain `ctl_status`/`ctl_rep_status` → control log (open-once, fflush per line).
  Poll work (`stat`/read/remove of move/go/record/replay-go files) runs only on that
  seat's stagger tick and only in the states named in D4.2/D4.10/D5.1/D5.6.
  NOTE (whitespace gotcha, HANDOFF "Gotchas"): `main.c` edits go through Python scripts
  with exact `\t` strings + `.count()` asserts.
- **C.5** Session teardown (all exits of `run_session`, next to the existing
  `wl_dump`/`gs_dump` sites, e.g. `main.c:1595-1596`): drain remaining statuses, close
  the control log + any open rec file. The `# control` summary counters ride the
  existing dumps (D4.14).

### Tests (PHASE Inv 3: suites stay green and GROW)

- **C.6** New host suite **`test/test_control.c`**, compiled exactly like the celiolink
  suite: `clang -std=c11 -Wall -Wextra -O0 -g -I source test/test_control.c -o /tmp/tcl
  && /tmp/tcl` (it `#include`s `source/control.c` directly, the `test_celiolink.c`
  pattern). Required cases, driving `ctl_*` with synthetic `CtlIn` streams:
  1. Grammar: every token kind parses; case/sprint semantics; 64-token and 512-byte caps;
     reject garbage with a useful `err`; `!` file; `G` gating (no output until `goSeen`).
  2. Closed-loop walk: coord advance completes exactly at target; cross-drift completes
     with status; warp (mapGroup/Num change) completes (D4.7); deadline `n*60+240`
     aborts the whole queue (D4.8); no-field wait then `CTL_NOFIELD_DL` abort (D4.9).
  3. Taps: press/slot frame counts exact (mask on for press frames, off for the
     remainder); `x`/`y` produce GBA L/R bits; `W<n>` injects nothing for n frames.
  4. Real-input abort: `realKeys != 0` aborts script AND replay the same tick, injects 0.
  5. Frozen-clock behavior: `emuFrame` not advancing → no token progress, no deadline
     expiry (menu/pause semantics, §0).
  6. Record: anchor debounce (3 frames), on-change lines only, first line at anchor,
     offset arithmetic across a simulated pause (frozen emuFrame → no line), cap → -1
     once; line format round-trips through `ctl_rep_load`.
  7. Replay: table load (skip `#`, cap, malformed → -1), edge-wait, the
     `tab[idx].f <= now` catch-up loop ORing the LATEST mask (multiple entries in one
     tick), done detection, abort.
  8. Status ring: overflow drops oldest, drain order FIFO, counters accurate.
- **C.7** Build proof after the slice (PHASE Inv 5): `make` → clean `3DGBA.3dsx`; the
  celiolink (417+) and netlink (66+) suites still pass; BUILDLOG entry appended
  (PHASE Inv 7) with file list + new check count.

---

## Appendix — recipe discipline for run-#13 scripts (pm-bridge-forensics.md #5/#7)

Rules (guidance for whoever writes the actual run-#13 scripts):

- **R1 — anchor on state, never wall clock.** PM latched scripts on the IN-GAME peer
  mask, because latching on the transport "fired at the title screen and ran the whole
  script against a solo session" (mp_bridge.cpp:1387-1392). Our equivalents: D4 walk
  tokens self-anchor on live coords; script START is anchored by the operator via
  `G` + `go_p<N>.txt` at a **celio FSM section transition** — watch the HUD/`# celio`
  section (`CL_SEC_SETUP/CONNECTION/DISCONNECT/LOUNGE`, `celiolink.h:186-191`; the
  `# celio-trace` lines, `gbacore.c:904`, timestamp every transition). Concretely for
  run #13: touch the go file when both consoles are back in the room post-trade
  (DISCONNECT→LOUNGE), not "about 30 seconds after the trade".
- **R2 — park the save AT the scene; script only the trigger** (startercrash,
  mp_bridge.cpp:1471-1478). For the room-exit test: save AT the cable-club counter
  before the run; the script never navigates from a fresh boot.
- **R3 — one decisive long hold beats oscillating taps** (seamwalk: discrete steps
  "oscillated the map header ~16x and never let the partner settle",
  mp_bridge.cpp:1479-1515). D4's closed-loop hold IS this — write `D4`, not
  `D1 D1 D1 D1`.
- **R4 — never straddle a warp with one walk token** (D4.7): end the walk ON the warp
  tile, then `W` through the fade, then continue in the new map's coordinates.
- **R5 — exact single steps are closed-loop `n=1` walks**, not timed taps (PM's
  "24 frames over-shot and left the house", mp_bridge.cpp:1516-1541 — the class of bug
  the closed loop removes).
- **R6 — tile counts are measured once, manually.** Walk the path once on hardware,
  read the tile deltas off the gs log (px/py columns), then freeze the script.

### Example script 1 — run-#13 item (c): post-trade ROOM EXIT, both consoles

`move_p1.txt` on BOTH consoles (each drives its own local game; counts are TEMPLATES —
measure per R6; the trade-room exit is a short walk DOWN through the door warp):

```
G W120 D2 W300 D1 W600
```

- `G` — operator touches `go_p1.txt` on BOTH consoles once both games are back in the
  room after the trade (R1: DISCONNECT→LOUNGE on the `# celio` HUD line).
- `W120` — settle (let the room re-establishment finish; the LOUNGE fix of run #12).
- `D2` — closed-loop walk to the tile ABOVE the door mat (adjust count per R6).
- `W300` — breathe; lets the `CL_EV_EXIT_ROOM` relay from whichever console moved first
  do its both-leave work before forcing the second.
- `D1` — the single decisive step ONTO the warp (R4/R5) — completes on map change.
- `W600` — ride the fade + re-entry to the club lobby; script ends; the netlogs
  (`# celio exitP=/sessEnd=`, control log, gs log) carry the verdict.

### Example script 2 — run-#13 item (d): walk BACK to the counter and re-link

`move_p1.txt` (drop AFTER script 1 completed and both consoles sit in the lobby;
counts are templates per R6 — lobby door → counter is a short up-walk):

```
G W60 U3 W60 a W90 a W90 a W90 a W600
```

- `G` — trigger when both consoles show the lobby overworld (R1).
- `U3` — closed-loop back to the attendant's counter tile, facing up (walking up leaves
  facing up — no turn tap needed).
- `a` ×4 with `W90` gaps — talk through the attendant dialog (each tap advances one
  textbox; taps are edge-safe at 12/40, D4.5). Adjust the tap count to the dialog
  length per R6; over-tapping is harmless on the yes/no prompt only if the default
  cursor is YES — verify once and pin (R6).
- `W600` — hold still through the re-link; the lazy gap-reset (`cl_reset_session`) must
  produce a fresh session — the run-#13 checklist's expected result (HANDOFF Next
  steps #2d), now hands-free and identical every run.

---

## Open Questions (implementer decides / verifies)

1. **Tap frame constants** (`CTL_TAP_DIR_PRESS/SLOT` = 4/16, `CTL_TAP_BTN_*` = 12/40) —
   PM's numbers are DS-era; the Gen-3 turn-vs-step frame threshold was not pinned in
   pret during this spec. Verify on hardware (a bare-dir tap must turn WITHOUT stepping
   when not already facing; a button tap must register exactly one `newKeys` edge at
   both 60 emu-fps and wireless ~4-5 emu-fps) and tune the four defines.
2. **Scripts on a paused seat** (the wireless non-participant is paused,
   `main.c:1598`): this spec leaves a picked-up script frozen at token 0 (emu clock
   stopped). Should pickup on a paused seat instead be rejected loudly
   (`[ctl pN] seat paused`)? Cosmetic either way; decide at implementation.
3. **Record mask provenance**: D5.4 records the final assembled seat mask (incl. touch +
   script). Confirm no 3DS-only chord (START+SELECT menu combo) leaks GBA START/SELECT
   bits into a recording frame before `menuOpen` freezes the block — if it does, record
   the pre-combo mask or document the one-frame artifact.
4. **Field-entry anchor false-positives** (D5.2/D4.9): an undetected save-loaded screen
   reads as `GCTX_OVERWORLD`+`sb1Valid`. Operator discipline (R2) is the chosen
   mitigation; if run-#13 practice shows misfires, consider adding `objX >= 0`
   (`gamestate.h:105`, gObjectEvents[0] active) to the predicate — but verify it is 0
   only off-field first (verify-on-hw-pending).
5. **sdmc `stat` cost at ~6 Hz during a wireless session**: believed negligible (the
   netlog writer already fopen/fprints at session close; polls are off the emulation
   threads). Confirm via the existing HUD worst-frame-ms after the slice lands; if
   visible, drop the cadence to 30 frames (PM's go-file cadence) — a constant.
6. **Auto-go from celio FSM transitions**: R1 uses a MANUAL go at a section transition.
   A future hook (glue watches `s_celioSection` and synthesizes `goSeen` on a named
   transition, e.g. `G@LOUNGE`) would remove the last human timing act — deliberately
   deferred: it couples control glue to celiolink state and run #13 does not need it.
7. **Replay table cap 8192** (vs PM's 65536): revisit only if a real recording overflows
   it (the loud load failure names the fix).
8. **`x`/`y` → GBA L/R letter mapping** (D4.5): kept for PM grammar compatibility;
   alternative dedicated letters would collide with `l`/`r` sprint-walk tokens. Confirm
   the choice reads acceptably in practice scripts before it fossilizes into recipes.
