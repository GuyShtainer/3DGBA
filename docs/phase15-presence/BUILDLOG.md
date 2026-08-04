# Phase 15 — co-op presence: BUILDLOG

One dated entry per slice (PHASE.md invariant 9). Baseline for the phase is commit `ae35079`
(phase 14 shipped): **10343 checks across six host suites, 0 failures**, `3DGBA.3dsx` builds clean.

---

## 2026-08-04 — slice M0: profile groundwork + the pure-C presence module (no drawing)

**Scope built:** SPEC-data.md D1 (the three new `GameProfile` columns), D2 (the snapshot contract
the module consumes), D3 (the 48-byte `PeerPresence` record, the source abstraction, staleness and
the two-tier liveness), D4.1-D4.3/D4.5-D4.7 (the gate ladder, map universes, the object-agreement
discriminator, the hold, and the `fieldgate.h` factoring the ladder needs), D5 (the anchor math,
the low-rate filter, the teleport guard, culling), D6 (the module + `test/host/test_presence.c`).

**Deliberately NOT built** (later slices, so this one stays provably neutral): any drawing, any art,
`presence_fill`, and any `main.c` wiring — M0 ships the module **inert**. Nothing calls it, so the
rendered frame is byte-for-byte identical to `ae35079` and the only shipped behaviour change in the
whole slice is three unread profile columns.

### Files

| File | What |
|---|---|
| `source/presence.h` / `source/presence.c` | **new** — the pure-C data half (no `<3ds.h>`, no citro, no `gamestate.h`, no `gbacore.h`, no `tilt.h`, no `<math.h>`) |
| `source/fieldgate.h` | **new** — the ONE field predicate, `static inline`, shared by tilt G5-G8 and presence P-G6 (D4.6) |
| `test/host/test_presence.c` | **new** — TEST 1-20 per D6.5; compile line in its header |
| `source/gamestate.h` / `.c` | appended `sb2ptr`, `spriteCoordOff`, `hbCtr` for BPEE/BPRE/BPGE with pret citations |
| `source/tilt.h` / `.c` | G5-G8 replaced by one `field_state_ok` call; `TILT_CTX_FIELD` is now an alias of `FIELD_CTX_OVERWORLD` |

### Verification

```
clang -std=c11 -Wall -Wextra -O0 -g -I source test/test_celiolink.c            -> 1259  PASS
clang -std=c11 -Wall -Wextra -O2 -I test/host -I source .../test_netlink_...   ->   66  PASS
clang -std=c11 -Wall -Wextra -O2 -I source test/host/test_diag.c               ->  369  PASS
clang -std=c11 -Wall -Wextra -O0 -g -I source test/host/test_control.c         -> 6897  PASS
clang -std=c11 -Wall -Wextra -O2 -I source test/host/test_trace_replay.c       ->   58  PASS (+4 skips)
clang -std=c11 -Wall -Wextra -O2 -I source test/host/test_tilt.c               -> 1694  PASS  <- UNMODIFIED
clang -std=c11 -Wall -Wextra -O0 -g -I source test/host/test_presence.c        -> 7394  PASS  <- NEW
                                                                        total   17737  (was 10343)
make -j8  ->  3DGBA.3dsx, zero warnings in presence.c / fieldgate.h / gamestate.c / tilt.c
```

`test_presence.c` also compiles and passes clean at `-O2` (the line SPEC-avatar A7.1 will use).
Pre-existing warnings elsewhere are unchanged: `main.c:1747 MENU_TAB_NAMES unused` and the vendored
mGBA `sha1.c -Wstringop-overread` notes.

**D4.6.1 discharged.** `test_tilt.c`'s 1694 checks — TEST 1-3 are the exhaustive gate truth table —
pass with **zero edits to the test**, which is the spec's stated proof that the `fieldgate.h`
refactor is behaviour-preserving. TEST 7 in the new suite then asserts, over the full enumerated
input space (`ok` x 9 `ctx` values x `sb1Valid` x 3 `px` cases x `textDlg`), that
`tilt_target_level` and `field_state_ok` give the same answer — so the two gates cannot drift.

**Frozen paths untouched.** `git diff` covers only `gamestate.{c,h}` and `tilt.{c,h}`;
`celiolink.c`, `netlink.c`, `gbacore.c` and `main.c` have an empty diff, as they have across
phases 13 and 14.

### The profile rows added (D1.7)

| Field | BPEE | BPRE | BPGE | mark |
|---|---|---|---|---|
| `sb2ptr` | `0x03005D90` | `0x0300500C` | `0x0300500C` | VERIFIED-SYM (EM sym:962 / FR sym:810 / **LG's own** sym:810; rev0 == rev1) |
| `spriteCoordOff` | `0x02021BBC` | `0x02021BC8` | `0x02021BC8` | VERIFIED-SYM (EM sym:20-21 / FR sym:23-24 / **LG's own** sym:23-24) — **logged, not steered on** |
| `hbCtr` | `0x030022E4` | `0x03003114` | `0x03003114` | VERIFIED-SYM base (`gMain` EM sym:894 / FR+LG sym:745) + VERIFIED-SRC offset `+0x24` |

Two findings recorded in the source rather than acted on, per the standing house rule:

1. **FR/LG's `vblankCtr` (`gMain+0x20`) is a POINTER, not a counter** (`pokefirered
   include/main.h:26`, `u32 *vblankCounter1`, NULL unless `SetVBlankCounter` armed it), so the
   phase-13 D2 Tier-B hang watch and the D3 `vbl` column are silently disarmed on the user's
   FireRed. **Reported, not changed** — presence routes around it via `hbCtr` (`gMain+0x24`,
   incremented unconditionally in `VBlankIntr` in both engines). Fixing D2/D3 is a phase-13
   follow-up with its own hardware evidence (Open Q1).
2. **`mapObjects` for FR/LG is CORRECT.** `pokefirered.sym:205` and `pokeleafgreen.sym:205` both
   give `gObjectEvents = 0x02036E38` — the shipped value. The long-standing suspicion that it
   "likely should be ~`0x02037038`" is refuted: `0x02037078` is `gPlayerAvatar`, the next symbol.
   Nothing was changed; the note is here so it is not re-litigated.

### Deviations from SPEC-data.md (and why)

1. **The gate ladder was built in M0**, though the slice brief names D1/D2/D3/D5/D6 and the ladder
   lives in D4. `presence_solve` is a D6.2 deliverable and D5.5's filter is defined as running
   *inside* it, so the function cannot exist without its gate. Building it now also means the ladder
   is host-tested before anything draws, which is the whole point of a "prove the data first" slice.
2. **P-G6 and P-G7 each split into a self-side and a peer-side reason code**
   (`PRES_OFF_SELF` / `PRES_OFF_FIELD` / `PRES_OFF_OBJ`). This is a strict refinement — same order,
   same short-circuit — and it is load-bearing rather than cosmetic: D4.7 holds the last good anchor
   when the *peer's* field/obj gate blinks but must stop immediately when the *host's* does, so the
   two sides must be distinguishable at the point the decision is made. The mapping table lives in
   `presence.h` above the codes.
3. **`out->reason` is populated on hold frames too**, where `draw == 1` and `held == 1`. D6.2 says
   "`reason`: `PRES_OFF_*` when `draw == 0`"; carrying it through the hold costs nothing and lets the
   M1 HUD print "held: field" instead of an unexplained frozen avatar. Consumers check `draw` first;
   the contract is stated in the `PresenceOut` comment.
4. **A non-holdable gate closure clears the hold *and* the smoothing filter.** D4.7.4 only specifies
   this for an expired hold. Extending it to every non-`FIELD`/`OBJ` closure (including the self-side
   failure D4.7.3 calls "stop immediately") means a re-appearance always snaps to truth rather than
   sliding in from a stale position, which is the same property D4.7.4 wanted.
5. **`presence_obj_agree` returns a tri-state** (1 agrees / 0 disagrees / **-1 unavailable**) instead
   of leaning on `PRES_F_OBJOK`. The flag alone cannot distinguish "`mapObjects` is unmapped" from
   "the object disagrees", and D4.5.1 requires the unmapped case to be reported as *gate
   unavailable*, never as a pass. The record's `objX = -1` sentinel (already `game_read`'s own,
   `gamestate.c:91,128`) carries that distinction; `PRES_F_OBJOK` stays as the producer's logged
   verdict.
6. **`PRES_SNAP_PX` is applied per axis** (max-norm), not to the vector length. D5.5 writes
   `|target - sm| > PRES_SNAP_PX` without saying which; per-axis is cheaper and strictly more eager
   to snap, and snapping is the safe direction.
7. **`PresenceState` grew fields beyond D3.4's list** — `snapNext`, `holdHave`, and the three
   diagnostics counters `mapChgN` / `teleN` / `dropN`. `PresenceState` is consumer-side state, not a
   wire type; only `PeerPresence` is size-locked (and it is, by `_Static_assert`). The counters are
   what "log it" in D5.6 and D3.4.2 actually means at this layer.
8. **`presence_rec_init` / `presence_game_id` were added to the API.** Both are pure-C producer-side
   helpers that keep M1's `presence_fill` a dumb reader: the first stamps `ver`/`seat`/`gameId` and
   the `objX/objY = -1` "gate unavailable" sentinels, the second is D4.3's game-code → map-universe
   mapping, which had to live somewhere host-testable.
9. **`gbatext.c` was NOT created.** SPEC-avatar A4.1 wants a UTF-8 charmap decoder in its own module;
   SPEC-data D6.3.1 wants an ASCII one called `presence_name_ascii`. This slice is the data half, so
   it ships D6.3.1's. The render half should make `gbatext_decode` the superset (adding `0xB4 '` and
   the UTF-8 `♂`/`♀`) and, if it wants one decoder, re-express `presence_name_ascii` on top of it —
   the TEST 17 in-repo anchor (`{0xC8,0xDD,0xE0,0xE7,0xFF} → "Nils"`, Celio's canned demo identity at
   `celiolink_payloads.h:98-100`) carries over unchanged.

10. **The two specs disagree about `PeerPresence`, and SPEC-data wins** (it is the later document and
    it explicitly owns the definition; SPEC-avatar A0.1 labels its own copy "reproduced here as the
    CONSUMED contract"). The shipped record is D3.2's 48-byte PM-shaped one. Translation for the
    render half: A0.1's `live` → `PresenceOut.draw` / `presence_liveness`; `tileX/tileY` → `px/py`;
    `mapGroup/mapNum`, `subX/subY`, `facing`, `gender`, `tid`, `name[8]`, `round` are unchanged.
    Two consequences worth reading before the draw slice starts:
    - **`PRES_SUB_SIGN` (A0.3.3) does not exist and should not be added.** A0.3.2 derived the
      sub-tile sign by inverting `warp_grid_eye`'s convention and flagged it "the single most likely
      thing to be wrong in the whole draw path" (hardware item H2). SPEC-data D5.3 instead reads the
      term straight out of the engine (`SetSpritePosToMapCoords`,
      `event_object_movement.c:4801-4819`): `SUB(c) = -c + 16*sgn(c)`, applied to **both** cameras.
      That is a stronger result than a sign to be discovered on hardware, and TEST 3 machine-checks
      the exact failure H2 was written to catch — a discontinuity at the tile boundary — at 1, 2 and
      4 px/frame, both roles, both axes, both directions. Presence hands the render half a finished
      **foot anchor in frame space**; the render half must not re-apply a sub-tile term.
    - **The anchor convention differs by 16 px on purpose.** SPEC-data returns the FOOT anchor
      (`120, 88`); SPEC-avatar A1.1/A2.2 works in CELL top-left (`112, 80`) and draws the sprite at
      `cellY - 16`. The relation is `cell = (footX - 8, footY - 16)`, so the render half should
      derive its rect from `PresenceOut.footX/footY` and never recompute a tile→screen mapping of
      its own (D0.2/§0.3). A0.2.1's degenerate check still holds: peer == host ⇒ foot `(120, 88)` ⇒
      cell `(112, 80)` ⇒ sprite top-left `(112, 64)` == `(POP3D_PLAYER_GX, POP3D_PLAYER_GY)`. TEST 2
      asserts the foot form of exactly that, at every camera phase in −15..15.

### Open, and now testable

- **`PRES_ANCHOR_Y = 88` is the one number here that could be 8 px wrong** (Open Q2). It is derived
  from pret's sprite chain; `main.c:691-692`'s comment implies 96, but that constant is unused and
  unvalidated. The M2 calibration run (D5.4.1) settles it with one photograph of both players on the
  same tile. `PRES_ANCHOR_X = 120` is corroborated twice and is not at risk.
- **Hardware checklist items this slice adds nothing to yet** — M0 draws nothing, so H1-H13 in
  SPEC-avatar A7.2 all still belong to M1/M2.

### Suite totals

| Suite | Before | After |
|---|---|---|
| celiolink | 1259 | 1259 |
| netlink reliability | 66 | 66 |
| diag | 369 | 369 |
| control | 6897 | 6897 |
| trace replay | 58 | 58 |
| tilt | 1694 | 1694 |
| **presence** | — | **7394** |
| **total** | **10343** | **17737** |

---

## 2026-08-04 — slice M1: same-map detection + the HUD readout (still no avatar art)

**Scope built:** the whole producer half and the surface that proves it. SPEC-data D2.1/D2.2 (the
three new reads, in the existing parked window, from the `GameState` pair `game_read` already
produced), D2.3 (the cached identity latch), D2.4.1 (the raw facing nibble kept beside the folded
one), D3.2 (the record, filled for real), D3.4 (`presence_publish` wired as THE seam), D6.2/D6.4
(the solve, in the render phase, one per game), and SPEC-avatar A4.4.3 (the HUD line naming the
peer + their tile), A6.1/A6.2/A6.3 (the LINK-tab `CO-OP` toggle and its persistence), A6.4 (the
`CO-OP` HUD chip whose colour carries the gate).

M0 shipped the module inert. **M1 is the first slice that changes a shipped frame** — and only when
the new pref is on, which it is not by default.

### Files

| File | What |
|---|---|
| `source/presence_read.h` / `.c` | **new** — the ONE file that touches game RAM for presence: the three reads, the D2.3 identity latch, `PresenceIdent` |
| `source/presence.h` / `.c` | added `PresenceSrc` + `presence_fill_core` (the pure producer core) and `presence_ident_due` (the D2.3 cadence) |
| `source/main.c` | fill+publish in the parked window; solve in the render phase; the readout + `CO-OP` chip; `ACT_PRESENCE` on the LINK tab through all four dispatch sites; `Settings.presence` + the length ladder |
| `test/host/test_presence.c` | TEST 21-24 (see the numbering note below) |
| `test/host/test_tilt.c` | TEST 6's `Settings` mirror + length ladder updated in the same edit, as A6.3.2 requires |

### Verification

```
clang -std=c11 -Wall -Wextra -O0 -g -I source test/test_celiolink.c            -> 1259  PASS
clang -std=c11 -Wall -Wextra -O2 -I test/host -I source .../test_netlink_...   ->   66  PASS
clang -std=c11 -Wall -Wextra -O2 -I source test/host/test_diag.c               ->  369  PASS
clang -std=c11 -Wall -Wextra -O0 -g -I source test/host/test_control.c         -> 6897  PASS
clang -std=c11 -Wall -Wextra -O2 -I source test/host/test_trace_replay.c       ->   58  PASS (+4 skips)
clang -std=c11 -Wall -Wextra -O2 -I source test/host/test_tilt.c               -> 1723  PASS  (was 1694)
clang -std=c11 -Wall -Wextra -O0 -g -I source test/host/test_presence.c        -> 7694  PASS  (was 7394)
                                                                        total   18066  (was 17737)
make -j8  ->  3DGBA.3dsx, zero warnings in presence.c / presence_read.c / presence.h / fieldgate.h
```

`test_presence.c` also passes clean at `-O2`. Pre-existing `main.c` warnings are unchanged and none
are in new code (`MENU_TAB_NAMES` / `PAD_EDGE_NAMES` / `menu_w_h` / `menu_w_sel` / `menu_layout` /
`splash_panel` / `ease_out` unused, three `clr*` unused, `menuScroll` set-but-unused, four
`-Wmisleading-indentation` at `main.c:977/2288/2299/3689`, plus the vendored mGBA `sha1.c` notes).

**Frozen paths untouched.** `git diff` on `celiolink.c`, `netlink.c`, `gbacore.c` and
`celiolink_payloads.h` is **empty**, as it has been across phases 13 and 14.

### What the readout looks like (the slice's acceptance test)

Bottom-left of the top screen, drawn only while the pref is on and that screen's HUD is enabled:

```
CO-OP me 3-12@14,9 | Nils#01234 3-12@17,8 f1/1 d+3,-1 a0 h0 L2 ok @120,88
```

`me <group>-<num>@<x>,<y>` · peer `<name>#<TID>` and the same · `f<folded>/<raw>` facing ·
`d<dx>,<dy>` tile delta · `a<age>` frames since the record arrived · `h<stall>` frames since the
heartbeat moved · `L<tier>` liveness (0 none / 1 connected / 2 active) · the `PRES_OFF_*` reason ·
` HOLD` on a D4.7 hold frame · `@x,y` = the frame-space foot anchor, appended only when the gate is
open. The `CO-OP` chip in the HUD chip row is **accent** when the ladder resolved to draw and
**dim** when it did not, so a photograph localises a gate bug before anyone opens the renderer.

Both maps are printed side by side because a map mismatch is the single commonest reason for "I
cannot see my friend", and both facings because D2.4.1's promotion criterion is that the nibble
reads 1/2/3/4 as the player walks D/U/L/R — printing only the folded value would let a garbage
nibble masquerade as a genuine "facing south".

### Deviations from the specs (and why)

1. **`presence_fill` was split into a pure core plus a thin reader**, rather than living whole in
   `main.c`. SPEC-data D6.4 permits either ("main.c (or a thin `presence_read.c`)"), and the split
   goes one step further: every *decision* the producer makes — the flag assembly, the shared field
   predicate, the ±15 sub-tile guard, the facing fold, the survival of the `objX = -1` "gate
   unavailable" sentinel — is now in `presence_fill_core` (pure C, TEST 22/23/24), and
   `presence_read.c` only fetches bytes. That is CLAUDE.md #4's rule applied to the one part of the
   phase that would otherwise have been untestable, and it is why the gate truth table can be driven
   **from raw game state** instead of from hand-built records.
2. **Presence state is keyed by GAME, not by SCREEN.** SPEC-avatar A2.7.1 says "index = SCREEN,
   mirroring `TiltSnap tiltSnap[2]`". A tilt is a property of a *display*, so per-screen is right
   for it; the identity latch, the smoothing filter, the D4.7 hold and the teleport detector are
   properties of a *world*, and keying them by screen would make an X screen swap look like a warp
   (filter cleared, hold dropped, a teleport counted) for no reason the player caused. The renderer
   still gets one record per screen — it resolves screen → game with `swapped`, exactly as it
   already does for `topG`/`botG` — so A2.7.1's actual requirement is met.
3. **Two `PresenceState`s, one peer each**, rather than one state with two slots. `PRES_MAX_PEERS`
   is 1, so D6.4's sketched `presence_publish(&s_pres, sc ^ 1, &r)` would have silently dropped one
   of the two publishes (slot 1 is out of range). One state per consumer with its single remote peer
   is also literally M4's shape: when the beacon lands, each console has exactly this, and the only
   line that changes is who calls `presence_publish`.
4. **`presence_ident_due` has a short retry rung** (60 frames) as well as D2.3's 600-frame refresh.
   D2.3 lists three refresh triggers, one of which is "no identity is latched for that core yet" —
   as written that is either "read every frame until it works" (which contradicts "cached, not
   polled") or "wait 600 frames" (a 10-second blank nameplate on any boot where the save is not yet
   loaded). The two-rung form is what the rule plainly intends; TEST 21 enumerates it, including
   the property that a *latched* identity costs exactly one re-read per 600 frames.
5. **A name whose first byte is already `0xFF` does not count as well-formed.** D1.2 only requires
   the pointer sanity check. But D2.3 latches the *first* well-formed result and keeps it, so
   accepting an empty name off a title screen would freeze an empty nameplate for the whole session.
   The pointer check still guards the read itself; this only decides whether to *latch*.
6. **`mapLayoutId` is left 0.** D4.4 wants `SaveBlock1 + 0x32` read into the record for corroboration
   logging. `game_read` does not expose the `sb1` pointer, so reading it means a second deref plus a
   `read16` per game — doubling D2.2's advertised three-reads-per-game budget for a field that is
   explicitly forbidden from gating anything. Deferred to the slice that adds the CSV/gs-log columns
   (A6.5), where it has a consumer. The field stays in the record; nothing reads it.
7. **The test numbering reservation moved.** D6.5 reserved TEST 1-20 for SPEC-data and 21+ for the
   render suite, but M0 spent 1-20 and the producer needed four more. The producer tests take
   **21-24**; **the render half starts at 25**. Stated at the top of `test_presence.c` too.
8. **The settings row shipped with this slice**, though the brief names only the data + readout.
   Without it the feature is unreachable (A6.3.4 mandates default-off), so M1 could not be verified
   on hardware at all — and the toggle is fully specified in A6.1/A6.2/A6.3. It brought two things
   with it that are easy to get wrong and are done here: `PTABN[4]` 6 → 7 (A6.1.1's *silent* trap —
   the row would simply never draw), and `run_settings` gaining a `presenceOn` local, because that
   screen's `SETSAVE()` writes the whole struct and would otherwise wipe the pref on every visit
   even though the LINK tab is not one of the four tabs it exposes.
9. **Not built, deliberately:** the pause-summary `Co-op` pill (A6.4.3), the `DiagCsvRow` peer
   columns and the `GsDepth` peer fields (A6.5) — the CSV only writes during a wireless session, so
   those columns are dormant in a same-console phase and belong with the slice that has something to
   correlate them against. The LINK-tab toggle already answers "is it on".

### Open, and now answerable on hardware

- **`PRES_ANCHOR_Y` (Open Q2)** is now *printed* — the `@x,y` suffix is the frame-space foot anchor,
  so the M2 calibration run (D5.4.1) can be read off the same readout that proves the data.
- **The facing offset (D2.4.1)** is promotable from this slice: walk one player D/U/L/R and read
  `f<folded>/<raw>` on the other screen.
- **P-G7 flicker (Open Q3)**: if `obj` appears in the reason field during ordinary walking, widen
  `PRES_OBJ_TOL` to 2 — do not delete the gate.
- **The FR/LG `vblankCtr` defect (Open Q1)** stays reported-not-changed; presence reads `hbCtr`, and
  the readout's `h<stall>` column is what shows the heartbeat ticking on FireRed.
- **Cost (Open Q6)**: `worstMs` with the pref on vs off, with both games in the same overworld, is
  now measurable — six bus reads per frame plus one text run.

---

## 2026-08-04 — slice M2: the avatar (art, flat draw, walk, tilt composition)

**Scope built:** the whole render half short of identity. SPEC-avatar A1 (the sprite asset: the
16x32 cell, the 9-frame 3x3 sheet with EAST mirrored, the two gender variants at stride 64, the
procedurally-generated placeholder with its two magenta tells, the upload, and the drop-in path for
real art), A2.1/A2.2/A2.3 (where the draw goes in each per-screen sequence, the shared `calc_xform`
fit, the citro2d call), A2.4 (cull + source-space clip), A2.5.3 (order is the z), A2.6.1/A2.6.2
(dim tint per screen, `NULL` on the right eye), A2.6.3/A2.6.4 (the walk cycle), A2.7 (the bottom
screen and its inverted roles), A2.8 (zero disparity, argued not skipped), A3.1-A3.3 (the tilted
draw: foot anchor through `tilt_project`, pure translation, unscaled, citro2d not the raw-C3D
block), A3.4 (the y-sorted billboard loop).

**Deliberately NOT built** (slice M3 owns them): the nameplate, the Card panel, `gbatext_decode`,
the adjacency/facing meeting predicate and the `Ⓐ CARD` prompt (A4, A5). Also not built: A6.5's
`DiagCsvRow` peer columns and `GsDepth` peer fields, deferred by M1 for the reason recorded there
(the CSV only writes during a wireless session), and A6.4.3's pause-summary `Co-op` pill.

### Files

| File | What |
|---|---|
| `source/presence_art.h` / `.c` | **new** — the render half's pure-C core: the sheet layout, the sprite rect, the cull/clip, the walk cycle, the y-sort, and the placeholder generator. No `<3ds.h>`, no citro, no `<math.h>`, no `tilt.h` |
| `source/main.c` | `presence_art_init/fini` (the texture, once, at app init), `presence_draw_screen` (the only citro2d + `tilt_project` code), the once-per-frame walk step beside `presence_solve`, and the three per-screen call sites |
| `source/assets.h` / `.c` | `assets_img_cell` — A1.4.3's promotion of the private `img_subrect` to a public sub-cell helper, so the sheet math is not copied a third time |
| `test/host/test_presence.c` | TEST 25-32 (see below); `presence_art.c` added to the includes |

### Verification

```
clang -std=c11 -Wall -Wextra -O0 -g -I source test/test_celiolink.c            -> 1259  PASS
clang -std=c11 -Wall -Wextra -O2 -I test/host -I source .../test_netlink_...   ->   66  PASS
clang -std=c11 -Wall -Wextra -O2 -I source test/host/test_diag.c               ->  369  PASS
clang -std=c11 -Wall -Wextra -O0 -g -I source test/host/test_control.c         -> 6897  PASS
clang -std=c11 -Wall -Wextra -O2 -I source test/host/test_trace_replay.c       ->   58  PASS (+4 skips)
clang -std=c11 -Wall -Wextra -O2 -I source test/host/test_tilt.c               -> 1723  PASS  <- UNMODIFIED
clang -std=c11 -Wall -Wextra -O0 -g -I source test/host/test_presence.c        -> 26108 PASS  (was 7694)
                                                                        total   36480  (was 18066)
make -j8  ->  3DGBA.3dsx, ZERO warnings in presence_art.c / presence_art.h / assets.c / assets.h
```

`test_presence.c` also passes clean at `-O2`. `main.c`'s warning set is **byte-identical** to slice
M1's (`MENU_TAB_NAMES` / `PAD_EDGE_NAMES` / `menu_w_h` / `menu_w_sel` / `menu_layout` /
`splash_panel` / `ease_out` unused, three `clr*` unused, `menuScroll` set-but-unused, four
`-Wmisleading-indentation`), plus the vendored mGBA `sha1.c` notes. No new warning anywhere.

**Frozen paths untouched.** `git diff` on `celiolink.c`, `celiolink.h`, `celiolink_payloads.h`,
`netlink.c`, `netlink.h` and `gbacore.c` is **empty**, as it has been across phases 13 and 14.
`presence.c` / `presence.h` are also unchanged by this slice — the render half consumed the M0/M1
seam exactly as published and needed nothing added to it.

### The new tests

| # | Proves | A7.1 |
|---|---|---|
| 25 | Sheet cells: exhaustive over `facing` 0..255 x `pose` -1..3 x `gender` -1..2 — nothing escapes the 128x128 sheet; E and W are literally ONE cell (`mirror` differs, x/y identical); any unknown facing lands on row 0 col 0; the 18 authored cells cover the two 48x96 blocks **exactly and disjointly** (painted-coverage proof) | P3 |
| 26 | Draw position: the same-tile anchor identity at every camera phase, the `foot -> (foot-8, foot-32)` conversion over a spread, and TEST 3's whole-step continuity sweep **re-run through the rect** at 1/2/4 px per frame, both roles, both directions | P1, P2 |
| 27 | Cull/clip: exhaustive 1 px sweep over `[-32,272] x [-48,208]` x mirrored/not (~156k positions) against the truth predicate, with the clipped rect never leaving `[0,240]x[0,160]`, the source window never leaving the cell, and registration preserved — plus a hand-computed case per edge and per corner | P4 |
| 28 | The MIRRORED clip takes its cuts off the **opposite** source edge (the defect that is invisible mid-screen and slides the sprite inside its own box at the frame edge), and mirroring never changes visibility, height or the vertical source offset | P4 |
| 29 | The tilt composition is a **pure translation**, driven by the real shipped `tilt_view_init`/`tilt_project`: width and height bitwise unchanged at every ladder angle; exactly `(0,0)` at angle 0; the translated foot lands exactly on the projected ground point; and a **non-vacuity** check that the billboard rule genuinely differs from a per-corner projection | P5 |
| 30 | Placeholder art: a guard region proves it writes exactly `128*128*4` bytes; nothing outside the two variant blocks; all 18 cells drawn; **both magenta tells present in every cell** (the checker asserted pixel-exact); the three poses differ, the three facings differ, and the two gender variants differ in **shape** as well as colour; the GPU_RGBA8 byte order asserted byte by byte | P10 |
| 31 | Walk selection: the world-px channel advances by exactly the speed with no hitch at the tile boundary at 1/2/4 px/f; a 64-frame walk follows the 4-beat cycle frame by frame; one 16 px tile plays all three poses; STAND after exactly `PRES_IDLE_FRAMES` with the accumulator cleared; map change / rewound round / stale gap / teleport all restart at STAND while a legitimate 12 px bike frame does not; NULLs are safe; the accumulator wrap preserves the phase | P9 |
| 32 | The y-sort comparator is antisymmetric over a spread, NULL-safe, and every permutation of three billboards sorts to the same ascending-foot-y order with `tid` breaking the equal-y tie | P6 |

### Deviations from SPEC-avatar.md (and why)

1. **`PRES_SUB_SIGN` was not implemented and no sub-tile term is applied in the render half.** A0.3
   defines the render half's own sub-tile arithmetic and its `PRES_SUB_SIGN` constant; SPEC-data
   D5.3 supersedes both by reading the term straight out of the engine
   (`SetSpritePosToMapCoords`) and folding it into `PresenceOut.footX/footY` for **both** cameras,
   which BUILDLOG M0 deviation 10 already records as binding. Applying A0.3's expression again here
   would double the BG-scroll tracking. TEST 26 pins that by re-running the whole-step continuity
   sweep through the drawn rect, so the doubling would show as a 2x delta rather than as a subtly
   fast avatar. **Nothing in this slice re-derives a tile->screen mapping** (SPEC-data D0.2 §0.3).
2. **`presence_cell_frame` / `presence_clip` / `presence_nameplate` do not exist under those names.**
   A3.2's sketch names them; the shipped equivalents are `presence_art_rect` (the foot->sprite
   conversion, since the cell-frame computation belongs to the data half now) and
   `presence_art_clip`. `presence_nameplate` is M3's.
3. **The avatar's screen-space work lives in `main.c`, not in a `presence_draw.c`.** A3.2 sketches
   a `presence_draw.c`; every other render pass in this project (`pop_eye`, `light_pass`,
   `warp_grid_eye`, `ui_pop_eye`, `dof_bands`, `tilt_draw_image`) is a `static` function in
   `main.c`, and the draw needs `calc_xform`, which is `static` there. A separate file would have
   had to duplicate it — the one thing A2.2 forbids. Everything host-testable went to
   `presence_art.c` instead, which is the split the pure-C rule actually asks for.
4. **The placeholder is THEME-NEUTRAL, resolving open question O-A5** in the "placeholder goes
   theme-neutral" direction. A1.5.2's table says the head uses `accent`; the shipped generator still
   takes `accentRGBA`/`inkRGBA` exactly as A1.5.1 specifies, but `main.c` passes two fixed constants
   rather than `g_ui.acc`. Reasons: the texture is baked once and the theme is switchable at
   runtime, so a themed placeholder either goes stale or needs a re-bake on the render thread; and
   A1.4.2 already argues world content should not re-skin with the chrome, so a theme-neutral
   placeholder is what will look the same as the real art.
5. **The female variant is distinguished by a derived accent (an RGB rotation) plus a SHAPE tell**
   (a 1-px flare on the body's lower rows). A1.3 requires two variants but does not say how the
   *placeholder* should tell them apart, and a second colour parameter would have changed the
   signature A1.5.1 fixes. The shape tell is what survives a monochrome photo and colour-blind eyes;
   TEST 30 asserts the variants differ in alpha coverage, not only in colour.
6. **STEP_B is STEP_A's leg offsets swapped (`+1 / -2`), not a geometric mirror of the cell.**
   A1.5.2 says "mirrored STEP_A"; mirroring the whole cell would also mirror the facing pip and turn
   a south-facing walk frame into a different facing. Swapping the stride is the intended reading and
   keeps the three poses equally distinguishable (TEST 30 asserts each differs from STAND).
7. **`presence_draw_screen` takes an explicit `nPeers` instead of assuming `PRES_MAX_PEERS`.**
   `presOut`/`presPose` are indexed by GAME, so `main.c` hands over single-element views; reading
   `PRES_MAX_PEERS` entries out of those the day the 3-4-player bound grows would be a silent
   out-of-bounds read — exactly the class of trap A6.1.1 names. The count is passed and clamped.
8. **`PresenceOut.moving` is not consumed.** A2.6.3 specifies a travel accumulator, which is strictly
   more informative than the boolean (it carries speed and phase, and it degrades correctly when
   `PRES_F_CAM` is clear — TEST 31 asserts that whole-tile case explicitly). The flag remains for the
   HUD/logging consumers the data half wrote it for.
9. **The walk also restarts on a >2-tile single-frame jump** (`PRES_WALK_JUMP_PX 32`). A2.6.4 lists
   map change, `live` edge and round gap; a warp within one map is none of those but would spin the
   cycle to an arbitrary phase, which is the same defect the listed three prevent. 4 px/frame (bike)
   is the fastest legitimate motion, so 32 px cannot fire on real movement — TEST 31 asserts a 12 px
   frame is accumulated and an 80 px frame restarts.
10. **Sub-pixel slivers at the frame edge are culled, not drawn.** A2.4.2's clip is integer; a
    fractional anchor (only reachable from M4's smoothing filter — same-console every anchor is an
    integer) can leave less than one source column visible, and the clip rounds **outward** so
    nothing bleeds into the letterbox. That is the direction `draw_pop_tex`'s own rule picks
    (`main.c:731-732`, per-eye rivalry on the border). TEST 27 asserts both halves.
11. **`assets_img_cell(C2D_Image src, ...)` rather than A1.4.3's `assets_wgt_cell(id, ...)`.** The
    generated placeholder is not a widget, and both paths must hand the same cell geometry to the
    same draw code (A1.5.4), so the helper takes an image rather than a manifest id.

### The 8 px that is now machine-pinned, not silently chosen (Open Q2)

SPEC-avatar A0.2.1 states the degenerate case as sprite top-left `(112, 64)`; SPEC-data D5.4 derives
the foot anchor from pret's own sprite chain and gets `88`, i.e. sprite top `56`, and records
`main.c:694`'s implied `96` as an **unused** constant — a comment, not a validated value. The two
specs therefore disagree by exactly 8 px, and M0 shipped `PRES_ANCHOR_Y = 88` marked VERIFY-ON-HW.

This slice did **not** pick a side. TEST 26 asserts (a) the conversion is exact in terms of the
shipped constant and (b) `sprite_top - POP3D_PLAYER_GY == -8` **right now**, with the whole argument
in the comment. Hardware item **H1** settles it; whichever way it lands, one number changes
(`PRES_ANCHOR_Y`) and that assertion fires, so the edit has to be deliberate. `PRES_ANCHOR_X` is
corroborated twice and is asserted against `POP3D_PLAYER_GX` directly.

### Hardware checklist this slice arms (SPEC-avatar A7.2)

**H1** (does it appear at the right tile — and the `PRES_ANCHOR_Y` calibration, D5.4.1: stand both
players on the SAME tile and photograph), **H3** (sprite direction all four ways; also confirms
citro2d's negative-scale mirror, A1.2.2), **H4** (facing correctness on FireRed specifically),
**H5** (frame-edge trimming, flat and tilted), **H6** (composition with DoF/bloom/light — the A2.1
ordering claim), **H7** (tilt: feet planted, sprite pixel-crisp and the SAME SIZE at every row,
tracking the tween), **H8** (touch unaffected), **H9/H10** (frame budget on and off), **H13**
(placeholder honesty — the photo must read as a placeholder).

**H2 does not apply as written**: it exists to discover `PRES_SUB_SIGN` on hardware, and D5.3
removed the constant by reading the term out of the engine. Its *failure mode* — a 16 px hitch at
the tile boundary — is machine-checked at 1/2/4 px per frame, both roles, both axes, both
directions, by TEST 3 and again through the drawn rect by TEST 26. Open question **O-A2**'s second
outcome (a smooth-but-offset avatar, meaning `SaveBlock1.pos` updates at the END of a step) is still
a hardware question and is what H1's photograph would show.

### Open, and answerable only on hardware

- **The GPU_RGBA8 byte order** (A,B,G,R ascending) is asserted byte-exact in the host suite but is
  a *claim about the PICA200*, not a fact the PC can settle. If the placeholder photographs with red
  and blue swapped, the fix is `put_px` in `presence_art.c` and nothing else. Flagged in the source.
- **citro2d's negative horizontal scale** (the EAST mirror, A1.2.2). Sanctioned fallback if it does
  not mirror about the draw origin: author 3 extra East frames, a pure art change plus one table row.
- **O-A1** (does a 16x32 unoccluded overlay read as a person or as a sticker) and **O-A4**
  (nameplate policy) need a photograph at real size; **O-A3** (stereo disparity) needs its own
  comfort session and is deliberately unbuilt.

### Suite totals

| Suite | Before | After |
|---|---|---|
| celiolink | 1259 | 1259 |
| netlink reliability | 66 | 66 |
| diag | 369 | 369 |
| control | 6897 | 6897 |
| trace replay | 58 | 58 |
| tilt | 1723 | 1723 |
| presence | 7694 | **26108** |
| **total** | **18066** | **36480** |

---

## 2026-08-04 — slice M3: identity card, interaction trigger, UI/settings/diagnostics

**Scope built:** the last half of the phase. SPEC-avatar A4.1/A4.2 (the codebase's first Gen-3
charmap decoder, UTF-8, with the in-repo "Nils" anchor), A4.3.2 (the five-digit trainer ID, and
never the secret one), A4.4.1 (the nameplate over the peer's head), A4.4.4 (the Card panel),
A4.4.2/A4.5 (which surfaces are up, one policy function, and the decode cache), A5.1/A5.2/A5.2.1
(adjacency + facing, with the degraded mode that keeps the feature alive on FireRed), A5.3 (the
A-press is OBSERVED, never consumed), A5.4 (the prompt and the card's one bool), **A5.5.3 (trade
and battle NOT built — the recorded design and the missing-symbol list live in a comment, and the
card carries the sanctioned one-line disclosure instead of a greyed button)**, A6.4.3 (the
pause-summary `Co-op` pill) and A6.5 (the CSV peer columns + the `GsDepth` peer fields, both
deferred by M1/M2 to the slice that owns them).

**Already shipped by M1, not rebuilt here:** the LINK-tab `CO-OP` toggle through all four dispatch
sites, `Settings.presence` + the length ladder, and the `CO-OP` HUD chip whose colour carries the
gate (A6.1/A6.2/A6.3/A6.4). This slice adds the *semantics* test that the byte-layout mirror in
`test_tilt.c` TEST 6 cannot see.

### Files

| File | What |
|---|---|
| `source/gbatext.h` / `.c` | **new** — the Gen-3 charmap -> UTF-8 decoder (A4.1/A4.2). Pure C, cap-driven, 256-entry table indexed by a `uint8_t` so no index can escape it |
| `source/presence_ui.h` / `.c` | **new** — slice M3's pure-C core: the meeting predicate, the card's one-bool FSM, the surface policy, the card's text, and two hand-rolled integer formatters so the module needs no `<stdio.h>` |
| `source/main.c` | the nameplate + prompt inside `presence_draw_screen` (a new `PresChrome*`), `presence_draw_card`, the per-frame M3 step beside the solve, the decoded-name cache, the pause-summary pill, and the CSV + gs-log peer fills |
| `source/diag.h` / `.c` | 12 appended CSV peer columns + the matching header names |
| `source/gamestate.h` / `.c` | 8 appended `GsDepth`/`GsLogEntry` peer fields, their dump columns, the empty-field count 18 -> 26, and a header comment that decodes `d_prReason` |
| `test/host/test_presence.c` | TEST 33-37 |
| `test/host/test_diag.c` | the peer columns added to the golden column list, the sample row, the comma-count assertions, plus a localization proof and a "no peer" sentinel row |

### Verification

```
clang -std=c11 -Wall -Wextra -O0 -g -I source test/test_celiolink.c            -> 1259  PASS
clang -std=c11 -Wall -Wextra -O2 -I test/host -I source .../test_netlink_...   ->   66  PASS
clang -std=c11 -Wall -Wextra -O2 -I source test/host/test_diag.c               ->  376  PASS  (was 369)
clang -std=c11 -Wall -Wextra -O0 -g -I source test/host/test_control.c         -> 6897  PASS
clang -std=c11 -Wall -Wextra -O2 -I source test/host/test_trace_replay.c       ->   58  PASS (+4 skips)
clang -std=c11 -Wall -Wextra -O2 -I source test/host/test_tilt.c               -> 1723  PASS  <- UNMODIFIED
clang -std=c11 -Wall -Wextra -O0 -g -I source test/host/test_presence.c        -> 48856 PASS  (was 26108)
                                                                        total   59235  (was 36480)
make -j8  ->  3DGBA.3dsx, ZERO warnings in gbatext.c / gbatext.h / presence_ui.c / presence_ui.h /
              diag.c / diag.h / gamestate.c / gamestate.h
```

`test_presence.c` passes identically at `-O2`. `main.c`'s warning set is **unchanged in kind and
count** from slices M1/M2 (`MENU_TAB_NAMES` / `PAD_EDGE_NAMES` / `menu_w_h` / `menu_w_sel` /
`menu_layout` / `splash_panel` / `ease_out` unused, three `clr*` unused, `menuScroll`
set-but-unused, four `-Wmisleading-indentation`), plus the vendored mGBA `sha1.c` notes. No new
warning anywhere.

**Frozen paths untouched.** `git diff` on `celiolink.c`, `celiolink.h`, `celiolink_payloads.h`,
`netlink.c`, `netlink.h` and `gbacore.c` is **empty**, as it has been across phases 13, 14 and
every slice of 15. `presence.c` / `presence.h` / `presence_art.c` / `presence_art.h` /
`presence_read.c` are also **unchanged** by this slice — M3 consumed the M0/M1/M2 seams exactly as
published and needed nothing added to them.

### What the player sees

- Walking up to the peer, a pill with their **decoded name** rides over their head, and a second
  `A - CARD` chip appears under it the moment the two are **adjacent and facing each other**.
- Pressing **A** (which still reaches the game, untouched) opens a card: name, gender, the
  five-digit trainer ID, the peer's map and tile, `same map - read-only`, and one dim line —
  `trade & battle use the game's own Union Room - not from here`.
- Another **A** or **B** closes it; so does walking away, turning away, the pause menu, the HUD
  being hidden, or the pref being switched off.
- The pause screen's feature-pill row gains `Co-op`, so "is it on" is answerable without opening
  the LINK tab.

### The new tests

| # | Proves | A7.1 |
|---|---|---|
| 33 | The charmap decoder: the in-repo Celio `{C8,DD,E0,E7,FF} -> "Nils"` anchor; the letter/digit range ends; **the table is complete** (exactly one NULL entry, at 0xFF — an under-supplied `[256]` initialiser would silently truncate every name at the first high byte); a **256-value cross-check against `presence_name_ascii`** with the three deliberately-added rows enumerated so a fourth cannot appear unnoticed; the terminator, the unterminated 8-byte name, the empty name; and a cap sweep 0..32 on the worst-case input (eight 3-byte glyphs) proving no write past `cap`, always NUL-terminated, return == `strlen`, and never half a UTF-8 sequence | P7 |
| 34 | Adjacency + facing, **exhaustive** over `dx,dy in [-2,2]` x both raw nibbles in `-1..9` (3025 states) against an INDEPENDENT reference formulation written from the spec's prose; diagonals are never adjacent; `meet` never without adjacency; the four legal meetings spelled out so a sign flip is named; A5.2.1's degraded mode is **eager, not dead**, and never invents adjacency; and the preconditions (different map, different **map universe**, invalid SaveBlock1, NULLs) | P8 |
| 35 | The card is one bool: A opens it only when meeting AND drawing; it survives frames with no edge; **seven** close conditions each from a fresh open; an involuntary close **wins over** the A that would open it; NULL safety. Plus the surface policy, including the pinned fact that the shipped policy ignores distance (so O-A4's on-approach variant is a deliberate one-line edit) | — |
| 36 | The card's text: the formatters at their boundaries (`INT_MIN`'s negation trap, a number that does not fit is **dropped, not truncated**); a fully-identified peer; the **degraded** card (no identity => `?` and a HIDDEN TID, but the position still shown; no valid position => no tile at all rather than a plausible wrong one); the 8-glyph-with-a-gendered-sign worst case exactly filling the field; and A5.5.3 asserted **structurally** — `PresCardText` is three text fields and two ints, with no action, no selection and nothing pressable | — |
| 37 | The co-op pref's semantics: every stored word (including negative and `0x80000000`) normalises to a bool; the default is OFF; and P-G1 is **first** — it closes with its own reason code even when every later rule would also have closed the gate, so the readout can never blame a later rule for a switched-off feature | P12 (the half `test_tilt` TEST 6's byte mirror cannot see) |

### Deviations from SPEC-avatar.md (and why)

1. **Two decoders coexist, and are machine-cross-checked rather than merged.** A4.1 asks for
   `gbatext.c`; SPEC-data D6.3.1 already shipped `presence_name_ascii` in `presence.c` (M0,
   host-tested by TEST 17/24). They are NOT merged, because their contracts genuinely differ: the
   ASCII one is a 9-byte, 8-character-bounded decoder the dense M1 HUD line depends on (a UTF-8
   name would blow that line's budget), and the UTF-8 one must carry the two gendered signs for the
   card. Merging would also have meant `presence.c` growing an include D6.1 forbids. Instead TEST
   33(d) asserts the two agree on **all 256 byte values** except three enumerated rows — which is a
   stronger guarantee than a shared implementation would have given, because the two tables were
   transcribed independently.
2. **`gbatext_decode` returns BYTES, not "characters".** A4.1's prose says characters; for a
   multi-byte glyph that is ambiguous, and every caller does length arithmetic. The return value is
   `strlen(out)`, asserted as such exhaustively. Stated in `gbatext.h`.
3. **The card's gender field is `M`/`F`/`?`, not the glyph A4.4.4 draws.** The card renders through
   the **baked bcfnt** faces (`tools/build_assets.sh` bakes Space Grotesk + JetBrains Mono), whose
   coverage of U+2642/U+2640 is unverified on this machine, and a missing bcfnt glyph draws as
   nothing. A blank where a value should be is the one failure a readout whose purpose is
   verification (A7.2 H11 compares it against the game's own trainer card) must not have. Names
   still decode with the real signs, because that is where they occur and because the nameplate
   draws with the **3DS shared font**, which does cover them.
4. **The prompt reads `A - CARD`, not `Ⓐ CARD`.** U+24B6 is not in the 3DS shared font — `ui.h`
   documents exactly this class of gap for the arrow glyphs, which is why `ui_tri` exists. An
   un-renderable glyph in the one chip that teaches the feature is worse than plain ASCII.
5. **The nameplate stacks ABOVE the prompt, not below it.** A5.4.1 says the prompt sits "just below
   the nameplate"; taken literally from A4.4.1's `y = headY - 13` that puts the prompt over the
   avatar's face. Stacking upward from the head satisfies the same relative order and leaves the
   sprite clear. When the plate is alone it lands exactly where A4.4.1 puts it.
6. **The em dash and the middle dot in A4.4.4/A5.5.3's literal strings are ASCII `-`.** Same font
   reason as 3 and 4; the wording is otherwise verbatim.
7. **The Card draws on the FOCUSED game's screen, not always the top.** A4.4.4 says "a top-screen
   overlay", which is the single-peer reading. With two games each having their own peer, pinning
   it to the top would put a card about the *bottom* game's neighbour on the *other* screen. Only
   one card can be open at a time (the A edge is fed to the focused game alone), so the two call
   sites are mutually exclusive in practice.
8. **`hudOn` is a field of `PresCardIn`, so the FSM closes on it.** A5.4.4 suppresses the card with
   the rest of that screen's chrome; gating only the draw would leave a card "open" but invisible,
   and it would then swallow the next A press to close something nobody saw.
9. **The CSV gains 12 peer columns, not A6.5's 11** — `prReason` is added. The `PRES_OFF_*` code is
   already computed every frame and is the single most useful diagnostic in the set ("it did not
   draw" must answer "why" without a rebuild, D4.1); leaving it out of the row while the gs log
   carries it would have made the two logs disagree.
10. **`prLive` is the liveness TIER (0/1/2), not A6.5's 0/1 bit.** The tier IS `pm-rom-abi.md`
    §7.4's two-tier signal the column is described as carrying (transport-connected != game-active);
    a boolean would have thrown away the distinction the column exists to record.
11. **`mapLayoutId` is still 0** (D4.4), for the third time and now with a sharper reason: A6.5's
    column list — which is this slice's contract — does not include it, and reading it costs a
    second `sb1` deref plus a `read16` per game per frame, i.e. it would nearly double D2.2's
    advertised three-reads-per-game budget for a field that is explicitly forbidden from gating
    anything and still has no consumer.
12. **The whole M3 per-frame block is skipped when the pref is off** (no decode, no `memcmp`, no
    predicate), and the card is force-closed. `presence_solve` still runs either way, because slice
    M1's readout must be able to say WHY nothing drew — but nothing M3 added costs anything on a
    presence-off frame, which is PHASE.md invariant 6 read literally.
13. **`presence_meet` takes the RAW facing nibbles as explicit parameters.** `PeerPresence.facing`
    is always 1..4 (`presence_fill_core` folds an unavailable facing to SOUTH so a wrong offset can
    only cost a wrong sprite direction), so the record alone cannot distinguish "facing south" from
    "we have no idea" — and A5.2.1's degraded mode is exactly that distinction. `main.c` already
    kept the raw nibbles beside the records for D2.4.1's promotion criterion; this slice gave that
    LOGGING-ONLY field a second, load-bearing job, which is recorded at its declaration.

### A5.5 — trade and battle: NOT BUILT, and why (recorded, not re-derived)

Nothing in this slice writes emulated RAM, touches `callback2`, or renders a greyed "Trade"/"Battle"
affordance. The four independent blockers (invariant 1 forbids the write; a trade is a live link
state machine, not a callable function; `gbacore.h` has no `write32` and no ROM-call trampoline; the
link path is frozen pending HW run #13) and the recorded later design — path (a) the real Union Room
via the game's own specials, path (b) the fragile `gSpecialVar_0x8004` sub-path, the per-game symbol
list that must first join `GameProfile`, and battle being the **input-sync tier** rather than the
local-termination tier — are in the block comment above `PRES_CARD_UNION_NOTE` in
`source/presence_ui.h`. TEST 36(e) asserts the disclosure exists and that `PresCardText` is
structurally incapable of carrying a button.

### Hardware checklist this slice arms (SPEC-avatar A7.2)

**H11** (the Card: name / ID / gender must match what each game's own trainer card shows — this is
the only real verification of `sb2ptr` AND of the charmap, and of the punctuation rows that have no
in-repo witness), **H4** (facing correctness per game — if FireRed's nibble is wrong, record it,
leave the address alone, and confirm the degraded adjacency-only prompt still fires), **H9** (frame
budget with the card and both pills up — the decode cache is the first suspect if `worstMs` moves,
and it is now the only text work that could), **H10** (presence off costs nothing — deviation 12 is
what makes that literally true for M3), **H12** (nothing was written: both saves load clean after a
session with the card opened repeatedly).

New, specific to this slice:

- [ ] **H14 — the prompt fires when it should.** Walk the two players face to face on the same map.
      **Accept:** the `A - CARD` chip appears only when adjacent AND facing each other, and
      disappears the moment either turns away. **If it appears whenever merely adjacent**, the
      facing nibble is unavailable on that game (A5.2.1's degraded mode) — that is the *designed*
      fallback, not a bug; record which game and check `f<folded>/<raw>` in the M1 readout.
- [ ] **H15 — A still reaches the game.** With the prompt up, face a real sign or NPC one tile away
      and press A. **Accept:** the game's textbox opens exactly as it always did, and the presence
      gate closes by itself while it is up (A5.3's "obtained for free" claim).
- [ ] **H16 — the card's fonts.** Photograph the card. **Accept:** every line renders — in
      particular the two dim disclosure lines at 8 px. If a glyph is missing, deviation 3's
      reasoning applies to whatever is missing and the fix is the string, not the font.

### Open, and answerable only on hardware

- **The punctuation rows `0xAB..0xBA`** are the one part of the charmap with no in-repo witness (no
  pret checkout on this machine). They are marked verify-on-hw-pending in `gbatext.c`, and the blast
  radius is bounded by construction: a wrong entry is one cosmetic glyph, never an out-of-range read
  and never an overrun. H11 settles them.
- **O-A4 (nameplate always-on vs on-approach)** is deliberately unresolved and now costs one line in
  `presence_surfaces`, with TEST 35(e) already pointed at it.
- **O-A1** (does an unoccluded 16x32 overlay read as a person or a sticker) is unchanged by this
  slice; the nameplate arguably helps, which is itself a hardware question.

### Suite totals

| Suite | Before | After |
|---|---|---|
| celiolink | 1259 | 1259 |
| netlink reliability | 66 | 66 |
| diag | 369 | **376** |
| control | 6897 | 6897 |
| trace replay | 58 | 58 |
| tilt | 1723 | 1723 |
| presence | 26108 | **48856** |
| **total** | **36480** | **59235** |

---

## 2026-08-04 — FIX PASS: three adversarial reviews of the phase-15 diff vs `ae35079`

**Scope:** no new feature. Every finding from the three reviews was re-verified against the code
before acting; one was **refuted with evidence** and is recorded as such rather than "fixed". The
safety lens (writes to emulated RAM, unguarded derefs, out-of-park reads, name-decode overruns)
came up **empty** — nothing in the diff writes a game bus, every new read is still inside the
existing parked window, and `gbatext_decode` / `presence_name_ascii` remain cap-driven.

### Per finding

| # | File | Sev | Verdict | What shipped |
|---|---|---|---|---|
| 1 | `presence_ui.c:25` | minor | **CONFIRMED** | `presence_fmt_int` negated `INT32_MIN` through a signed `long`, which is 32-bit on devkitARM ⇒ signed-overflow UB the optimiser may act on (invisible on the 64-bit host, which is why only reading finds it). Now `u = 0u - (unsigned)v` — well-defined modular arithmetic on every ABI. TEST 36's existing INT_MIN assertion now carries the reasoning. |
| 2 | `main.c:1337` | minor | **CONFIRMED** | The A1.5.4 baked-art drop-in validated only *existence*. A mis-sized `peer-walk-*.png` bypassed the placeholder and then fed cell coords up to (47, 95) into `assets_img_cell` → `img_subrect`, which computes UVs with **no clamp**, sampling past the subtexture rect (and baked textures never get `GPU_CLAMP_TO_EDGE`). New `presence_art_fits()` requires `PRES_VAR_W x PRES_VAR_H` (48x96 — the real used extent of one variant) or the placeholder wins. Garbage pixels, not a fault, hence minor — but "obvious placeholder beats plausible-looking wrong" is A1.5.3. |
| 3 | `presence.c:351` | major | **CONFIRMED (as a doc + UX defect, not a gate defect)** | The map-universe gate is **correct and stays** — `(mapGroup, mapNum)` is meaningful only inside one game's map table, so Emerald+FireRed would otherwise paint a peer onto an unrelated map. What was broken is that the whole A7.2 hardware checklist was written for **Emerald + FireRed**, the one pair that can never draw, and nothing told the user why. Fixed both ends: A7.2's opener now demands a same-universe pair (FR+LG / FR+FR / EM+EM) with the reason inline, and the pause-menu row now says `Co-op: on — Hoenn vs Kanto: no peer` / `— no profile: nothing draws` instead of leaving the CO-OP chip silently dim. |
| 4 | `main.c:1473` | major | **CONFIRMED in part** | The clip ran on the unprojected rect and the trimmed rect was then translated (A3.2.1's decided order), so under tilt the trim was taken at a boundary the sprite no longer touches. **The real defect is AMPUTATION**: a peer with its foot at frame x=4 at 20° loses 4 source columns — a quarter of the body — and the survivors draw at x≈-25 where the near row spills to -26.5, i.e. a slice missing out of a complete sprite standing on plainly visible ground. `presence_art_clip` gained a `margin`; `main.c` passes the view's own **spill** (4 `tilt_project` calls on the frame corners, tilt-only) so the box matches the boundary the base image itself honours — the same argument phase 14 used to ship `TILT_SCISSOR 0`. **The reviewer's second case (a "false cull" at frame x=250) does not hold:** the horizontal map at any row is `x → 120 + s·(x−120)`, `s>0`, so a flat x outside `[0,240]` lands outside that row's projected extent *exactly* — that peer is not on the tilted image and culling it is right. The vacuous host test the reviewer flagged (`presence_art_clip` compared to itself) is replaced by flat-box-vs-spill-box comparisons plus both worked amputation cases and the "must not rescue a culled peer" case. |
| 5 | `presence.c:398` | minor | **CONFIRMED** | `PRES_CULL_M` 8 → **0**, and the rect test's edge comparisons are now **closed** (`<=`, `>=`). The 8 px margin made `draw` — which colours the CO-OP chip, prints `ok` in the M1 readout and gates the walk accumulator — report an avatar that the renderer's exact clip drew nothing for, and the band landed on **whole-tile** positions (`dTileX = ±8`, `dTileY = -6`) crossed constantly while walking, inverting A7.2 H1's triage rule. New **TEST 16b** is the standing proof: over every integer anchor across the frame and past every edge, `on_screen == (presence_art_clip != 0)`. Still correct under tilt, for the §4 reason. |
| 6 | `fieldgate.h:45` | minor | **CONFIRMED, fixed asymmetrically** | The `textDlg` term was a *tilt* rule ("don't tilt a dialog") applied to the peer, where the position stays perfectly valid — so a friend who read a sign vanished for the length of every NPC line (the D4.7 hold covers only 0.5 s). New `PRES_F_TEXT` bit carries the textbox separately; `PRES_F_FIELD` no longer folds it. **Peer side: no longer gated** (their box is on their screen). **Self side: still gated**, on an independent presence-side justification the reviewer did not have — the *host's* dialog box is drawn in the very frame the avatar composites over, so a peer on the lower rows would be painted on top of it, which is D4.7.3's "no stray sprite over their menu". `field_state_ok` itself is untouched, so tilt's 1723 checks pass unmodified (D4.6.1's regression proof still holds). Residual, logged not hidden: Open Q4's unmodelled `sHorizontalCameraPan` coincides with scripts, and `PRES_F_TEXT` is what lets a hardware run identify those frames. |
| 7 | `presence.c:352` | minor | **PREMISE REFUTED; the diagnosability half fixed** | The finding rests on the standing "FR/LG `mapObjects` is probably wrong" suspicion. That is **already refuted in this phase's own spec**: SPEC-data D1.8.1 reads `gObjectEvents = 0x02036E38` out of `pokefirered.sym:205` **and** `pokeleafgreen.sym:205` (rev0 and rev1), and identifies `0x02037078` — the address the suspicion proposed — as `gPlayerAvatar`, the *next* symbol. So P-G7 is not silently dead on FireRed. The valid residual is that `PRES_OFF_SELF` conflated "the host is not in the field" with "the host's object slot disagrees with its SaveBlock1", and the second is what a wrong base *would* look like: new `PRES_OFF_SELFOBJ` (appended at the end, so every shipped CSV code keeps its value) prints as `selfobj`. |
| 8 | `main.c:1545` | major | **CONFIRMED** | The nameplate and the `A - CARD` prompt were clamped to `y >= 2` **independently**, so for any peer ≳3 tiles above the player (3 of the ~10 on-screen rows) both collapsed onto y=2 and drew on top of each other — inside the 0..16 px HUD bar, which is drawn *after* them and is translucent. Now one pure-C `presence_pill_y()` clamps the **stack as a unit** against `PRES_PILL_TOP` (18 = 14 px bar + 2 px focus rule + air), with `presence_pill_x()` carrying the old right-then-left clamp somewhere a PC test can reach. **TEST 38** sweeps every head y on both screens for overlap, gap rigidity, bar intrusion and bottom overflow. |
| 9 | `main.c:1552` | minor | **CONFIRMED** | Each pill cost **three** `C2D_TextParse`+`C2D_TextOptimize` passes over the same string (measure → chip measures again → chip draws), ×2 pills ×3 draw passes = **18 per frame** on the render thread that feeds two saturated 804 MHz workers. Widths are now measured **once per screen per frame** into `PresChrome` and drawn through new `ui_chip_w` / `ui_chip_fill_w` (the old two delegate, so every other call site is unchanged): **8**. Zero when the gate is closed, as before. |
| 10 | `main.c:1340` | minor | **CONFIRMED** | `presence_art_init()` ran unconditionally at boot and held a 64 KB RGBA8 texture for the whole run even though the co-op pref ships **default-off**. Now `presence_art_ensure()` — idempotent, called from `run_session` when the stored pref is on and from the pause-menu row the moment it is turned on. Both sites are outside `C3D_FrameBegin/End`, which the blocking `C3D_SyncDisplayTransfer` requires. Still built once, still off the per-frame path. |
| 11 | `main.c:3841` | minor | **CONFIRMED** | The M1 readout formatted two **unbounded** `%u` counters into a line whose own budget is ~73 chars / ~340 px of a 400 px screen, and `ui_text` has no measurement, no ellipsis and no clamp — so ten minutes of link (where publishing is skipped but `presence_begin_round` keeps running) pushed the reason code, the HOLD flag and the foot anchor off the right edge, i.e. exactly the half a photograph needs. Both are capped at `999` for display; they are only ever read as "0 / small / saturated". |

### Deviations from the specs, and why

1. **`PRES_CULL_M` is 0, not SPEC-data D5.7's 8** (finding 5). D5.7 gave the margin no stated
   purpose and it made `draw` lie on whole-tile positions. D5.7.1's rule is unchanged — presence
   still culls against the flat frame and never imports tilt geometry; what changed is that the
   render half no longer culls *more* than the data half does.
2. **`PRES_F_FIELD` no longer carries the `textDlg` term** (finding 6); it rides `PRES_F_TEXT` and
   is applied on the self side only. D4.6's "call the same function for both sides" is still
   honoured — `field_state_ok` is the predicate on both sides and is itself unmodified.
3. **`presence_art_clip` gained a `margin` parameter** (finding 4). A3.2.1's decided order (clip
   unprojected, then translate) is preserved verbatim; only the box widens, and only under tilt.
4. **A new reason code** `PRES_OFF_SELFOBJ` (finding 7), appended so the D3 CSV's existing values
   are stable.
5. **SPEC-avatar A7.2's cart pair and H5's accept criterion were corrected in place** (findings 3
   and 4), because both are executable hardware instructions that were wrong as written.

### Hardware checklist deltas

- **A7.2 opener** — the run needs FR+LG, FR+FR or EM+EM. Emerald + FireRed proves nothing about
  presence by design.
- **H5** — re-shoot with tilt on: the accept criterion is now "whole sprite, may overhang the ground
  by ≲ half a cell", and the *fail* signature is a vertical slice missing from a complete sprite.
- **New: H17 — the pill stack.** Stand the peer 3+ tiles above the player with both pills up.
  **Accept:** two distinct pills, 15 px apart, both entirely below the HUD bar. **Fail:** one pill,
  or text overlapping the bar's clock/FOCUS/TILT/CO-OP row.
- **New: H18 — the co-op toggle explains itself.** Turn the row on with an Emerald + FireRed pair.
  **Accept:** the status line names the mismatch. This is the one case the CO-OP chip alone cannot
  distinguish from "not on the same map yet".

### Suite totals

| Suite | Before (M3) | After (fix pass) |
|---|---|---|
| celiolink | 1259 | 1259 |
| netlink reliability | 66 | 66 |
| diag | 376 | 376 |
| control | 6897 | 6897 |
| trace replay | 58 | 58 |
| tilt | 1723 | 1723 (**unmodified**, D4.6.1) |
| presence | 48856 | **49756** |
| **total** | **59235** | **60135** |

`make -j8` → `3DGBA.3dsx`, zero new warnings in any file this phase creates (the surviving
`main.c` / `ui.c` `-Wmisleading-indentation` and `-Wunused-*` lines are all pre-existing and
untouched; `ui.c:80` is `ui_seg_hit`, unchanged at `HEAD:source/ui.c:76`).

---

## 2026-08-04 — FINAL GATE: independent re-verification + the docs pass

**Scope:** no feature work and **no source change** — the gate's mandate was to verify, and to touch
code only if something turned out to be broken. Nothing was. Every number below was re-measured from
a clean shell rather than copied from the slice entries above.

### Suites — each run by its OWN header compile line, from a clean shell

```
clang -std=c11 -Wall -Wextra -O0 -g -I source test/test_celiolink.c                      -> 1259  PASS
clang -std=c11 -Wall -Wextra -O2 -I test/host -I source .../test_netlink_reliability.c   ->   66  PASS
clang -std=c11 -Wall -Wextra -O2 -I source test/host/test_diag.c                         ->  376  PASS
clang -std=c11 -Wall -Wextra -O0 -g -I source test/host/test_control.c                   -> 6897  PASS
clang -std=c11 -Wall -Wextra -O2 -I source test/host/test_trace_replay.c                 ->   58  PASS (+4 loud skips)
clang -std=c11 -Wall -Wextra -O2 -I source test/host/test_tilt.c                         -> 1723  PASS
clang -std=c11 -Wall -Wextra -O0 -g -I source test/host/test_presence.c                  -> 49756 PASS
clang -std=c11 -Wall -Wextra -O2      -I source test/host/test_presence.c                -> 49756 PASS  <- A7.1's -O2 line
                                                                                   total   60135  0 failures
```

Matches the fix-pass table exactly. `test_presence.c` gives an identical count at `-O0` and `-O2`,
which is the A7.1 requirement (an optimiser-sensitive count would mean UB somewhere in the module).

### Build

```
make clean && make -j8   -> 3DGBA.3dsx   3,806,412 B   exit 0
make cia                 -> 3DGBA.cia    1,927,616 B   exit 0
```

### The warning audit, done mechanically instead of on faith

"Pre-existing" was **not** taken on faith. A detached **`git worktree` at `ae35079`** (the phase-14
commit, i.e. this phase's true baseline) was built with the same toolchain — the two untracked build
inputs, `source/assets_gen.h` and `data/`, were supplied from the main tree, which is faithful
because **this phase bakes no art**: `assets_gen.h` is unchanged and the placeholder is generated in
code (A1.5), so the asset set is byte-identical on both sides. Both build logs were then normalised
(mGBA/LTO lines dropped, line numbers stripped, sorted **with** duplicates preserved so a repeated
message cannot hide a new one) and diffed:

| | baseline `ae35079` | phase 15 | delta |
|---|---|---|---|
| warnings (our source) | **31** | **31** | **0 added, 0 removed** |

Per file, identically on both sides: `main.c` 15, `rompicker.c` 7, `touch.c` 4, and one each in
`celiolink.c` / `netlink.c` / `theme.c` / `ui.c` / `wireless.c`. (The vendored mGBA `sha1.c`
`-Wstringop-overread` notes and the `lto-wrapper` line are link-time LTO output from the prebuilt
`libmgba.a` — they carry the *original* `projects/dual-gba/…` build paths, which is the tell that
they are not produced from our tree at all.)

**The five files this phase created emit ZERO warnings** — `presence.c`, `presence_read.c`,
`presence_art.c`, `presence_ui.c`, `gbatext.c` — and that zero was confirmed to be real rather than
the silence of a file nobody compiled, by checking each one produced a `.o`:

```
build/presence.o 34336   build/presence_art.o 28568   build/presence_ui.o 22092
build/presence_read.o 12288   build/gbatext.o 8508
```

### Docs pass

- **`docs/HANDOFF.md`** — Current status gains the phase-15 track paragraph (what shipped, that it is
  **same-console only**, that **M4 is deferred until after HW run #13** by the user's own decision,
  and **the honest ceiling**: no collision, no occlusion, no talking — the engine does not know the
  avatar exists). The what's-left-to-test list gains the presence item and the current artifact
  facts. **Next steps #5** is the new H1–H18 hardware checklist, opening with the cart-pair trap and
  closing with an explicit *what counts as FAILURE* vs *what is designed behaviour* split. A dated
  newest-first session-log entry covers the phase (phase 14 was already logged and was left alone).
  One stale line was corrected while there: the "everything is UNCOMMITTED" bullet predated
  `19375eb`/`ae35079` — phases 13 and 14 **are** committed, and phase 15 is now the only uncommitted
  work.
- **`docs/kb/coop-shared-overworld.md`** — a **SHIPPED (M0–M3, same-console)** banner at the top
  pointing at the phase docs and code, and its **§3 M0 Ruby/Sapphire prerequisite corrected in
  place**: PHASE.md overrode it, the phase started on the existing verified BPEE/BPRE/BPGE rows, and
  RS became optional rather than a gate. The correction also records the two things that study could
  not have known — the FR/LG `mapObjects` suspicion is **refuted**, and a **cross-universe pair can
  never draw a peer**.
- **`docs/phase15-presence/SPEC-avatar.md`** — A7.2 backfilled with **H14–H18**, which had been
  authored in the M3 and fix-pass BUILDLOG entries but never folded into the spec. A7.2 is now the
  single complete bench checklist; the BUILDLOG stays the per-slice narrative.

### Status

Phase 15 is **code-complete, PC-green and build-green — and entirely hardware-unproven**, which is
this phase's exit gate and nothing more (CLAUDE.md #6). The next action on this track is the H1–H18
run on a real New 3DS with a **same-universe** cart pair; the next action on the *project* is HW run
#13, which is also what unblocks M4.
