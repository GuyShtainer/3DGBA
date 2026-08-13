# Phase 20 — `BUILDLOG.md`: the peer's GENUINE trainer sprite

Spec: `docs/phase20-peersprite/SPEC.md`. Ask (verbatim, after seeing
`docs/phase18-crisp/evidence/coop-both-screens.png`):

> "Oh wow. Show more locations and fix the player image to the geniuene one"

---

## 2026-08-13 — slices P1–P4 (the whole non-pixel half), one pass. **No emulator taken.**

Phase 19 (text size) is running concurrently on the same Azahar and on the same `main.c`. Every
line of this entry is host-side work: the pure-C module, the profile columns, the reader, the art
generalisation, the `main.c` wiring, and the 14th host suite. §S6 (the location sweep) and the
two questions that need pixels are **not done** and are listed under "What is still unproven".

### What shipped

| file | what |
|---|---|
| `source/peersprite.{c,h}` **(new)** | The whole phase, in pure C: OAM decode, the 15-clause resolve ladder + reason names, 1D/2D tile addressing, the tile/palette gathers over a caller-supplied read-only bus, 4bpp→RGBA8 detile+flip, the edge bleed, the change key, the 3DS Morton tiled-texture encode and the §S3.7 tiling proof. `<stdint.h>` + `<string.h>` only. |
| `source/gamestate.h` / `.c` | Three appended `GameProfile` columns — `sprites`, `plttUnfaded`, `playerAvatar` — filled for **all five** games, each **re-derived this session** from the nine `.sym` maps on this machine (never copied from the spec). |
| `source/presence_read.{c,h}` | `presence_read_sprite()`: a ~30-line adaptor that hands `peersprite` a `PsprBus` of `gbacore_read8/16/32`. |
| `source/presence_art.{c,h}` | `presence_art_rect_wh` / `presence_art_clip_wh` / `presence_art_live_cell`; the phase-15 functions are now one-line instances of the first two. Three `_Static_assert`s pin the live block to the sheet. |
| `source/main.c` | The init tiling proof, the per-frame decode+blit beside `presence_solve`, the draw-path branch (placeholder retained as the fallback), the appended `g_presDiag` block + 11 offset asserts, the HUD `spr:` line, the `GsDepth`/gs-log growth, the parked-window read call. |
| `test/host/test_peersprite.c` **(new, 14th suite)** | T1–T14, **62078 checks**, plus a six-mutation gate that was actually run. |
| `test/host/test_presence.c` | TEST 40 / 41 — the two `_wh` identities, from the suite that owns the phase-15 functions. |
| `test/host/test_profiles.c` | TEST 9 — the three new columns, per game, against the symbol maps. |
| `test/host/test_typography.c` | The main.c system-font budget 13 → 14, **with the reason**, exactly as that test's own failure message instructs (see "Deviations" #4). |

### Build + suites

```
export DEVKITPRO=/opt/devkitpro; export DEVKITARM=/opt/devkitpro/devkitARM; make -j8   # clean
make cia                                                                              # 3DGBA.cia
```

Zero warnings from any new or edited file (`peersprite.c`, `presence_art.c`, `presence_read.c`,
`gamestate.c` compile silent; the `main.c` warnings present are the pre-existing
`-Wmisleading-indentation` / unused-symbol set that predate this phase).

| # | suite | checks |
|---|---|---|
| 1 | test_diag | 376, 0 fail |
| 2 | test_tilt | 1723, 0 |
| 3 | test_theme | 83444, 0 |
| 4 | test_control | 6897, 0 |
| 5 | test_uigeom | 18332, 0 |
| 6 | test_trace_replay | PASS |
| 7 | test_netlink_reliability | PASS |
| 8 | test_uihit | 1834, 0 |
| 9 | test_typography | 1408, 0 |
| 10 | test_fieldpath | 1808, 0 |
| 11 | test_presence | 61371, 0 (+ TEST 40/41) |
| 12 | test_profiles | 546, 0 (+ TEST 9) |
| 13 | test/test_celiolink | PASS |
| 14 | **test_peersprite (new)** | **62078, 0** |

Harness host tests: `tools/emutest/tests/run_host_tests.sh` → **149 tests, OK**.

### The mutation gate — every one was RUN, every one went red

A green suite proves nothing until it can go red (phase-18 discipline).

| mutation | failures |
|---|---|
| drop the `hFlip` term from `pspr_decode` | 3072 |
| swap the 1D tile formula for the 2D one | 11 |
| swap the nibble order in `pspr_tile_index` | 10 |
| drop `animCmdIndex` from `pspr_hdr_changed` | 6 |
| write R,G,B,A instead of A,B,G,R in `pspr_blit_tiled` | 4078 |
| invert the block-row order in `pspr_tex_offset` | 20389 |

### The addresses, re-derived (house rule: never ship someone else's word)

Read out of `/tmp/pret/*.sym` this session, not copied from SPEC S1.3:

```
gSprites            EM 02020630 | FR 0202063c = _rev1 | LG 0202063c = _rev1 | RS 02020004 x4
gPlttBufferUnfaded  EM 02037714 | FR 020371f8 = _rev1 | LG 020371f8 = _rev1 | RS 0202eac8 x4
gPlayerAvatar       EM 02037590 | FR 02037078 = _rev1 | LG 02037078 = _rev1 | RS 0202e858 x4
```

`gSprites`' symbol **size is `0x1144` = 65 × `0x44`** in **all nine** maps — independent evidence
`struct Sprite` is unchanged across RS / FRLG / Emerald, which is what licenses one decoder for five
games. RS remains **VERIFIED-SYM / VERIFY-ON-HW** (no RS ROM on this machine), same posture as
phase 18.

### Cost, as measured by the suite rather than asserted in prose

`test_peersprite` T14 counts the fake bus's reads:

* **steady state (peer standing): exactly 28 emulated-bus reads per game per frame** — 5 `rd8`
  (spriteId, graphicsId, avatarFlags, avatarSpriteId, subTbl) + 22 `rd16` (DISPCNT, attr0/1/2, anim,
  sflags, then 16 palette colours) + 1 `rd32` (the `active` bit). **Zero pixel reads.**
* **on a walk step: +64 `rd32`** (16×32; 128 for a 32×32 form), which the suite drives end to end.
* **gate closed / unmapped game: ZERO bus reads.**
* GPU work: **none**. No new texture (the live cells reuse rows 96–127 of the existing 64 KB
  presence sheet), no display transfer on the per-frame path, no extra bind, no extra draw call.
* Per changed frame: ~1 KB decoded + bled, a 4 KB CPU store into the sheet, and a **16 KB** cache
  flush (block rows 12–15 = the last 16 KB, `_Static_assert`ed, and T12 proves in software that a
  live-cell blit touches nothing below byte 49152).

### Two defects found and fixed during implementation

1. **Stale columns on a form change.** Blitting only the sprite's own `w × h` into the 32×32 slot
   would leave the previous form's pixels in the rest of it — invisible to the draw (which uses the
   new `w`) but *not* invisible to a `GPU_LINEAR` tap at the sprite's right edge under tilt, which
   would smear a stripe of last-form pixels down the trainer's side when a peer dismounts a bike.
   Fixed: the whole 32×32 cell is zeroed, composed and written every time.
2. **A wrong golden in the spec.** SPEC S7 T1 asks for `v8 == (v5*255 + 15) / 31`. That is false for
   bit replication — see Deviations #2. The test found it on its first run.

---

## Deviations from `SPEC.md`, and why

1. **S1.6's "shape = 2 (TALL), size = 1 ⇒ 16×32" is a spec typo.** On the hardware table shape 2
   size 1 is **8×32**; 16×32 is shape 2 **size 2**. Confirmed against pret's own
   `object_event_subsprites.h` (`SPRITE_SHAPE(16x32)` / `SPRITE_SIZE(16x32)`). `pspr_oam_size`
   ships the hardware table; the deviation is stated at the function.
2. **S7 T1's golden is arithmetically wrong.** Bit replication computes `floor(33v/4)`; round-to-
   nearest computes `round(255v/31)`. They disagree at v = 3, 7, 24, 28. Replication is still the
   right choice (branch-free, and **exact at both endpoints**, which is the property that matters —
   a palette white must come out `0xFFFFFF`). T1 now pins the properties replication actually has:
   the exact expression, ≤1 LSB from exact, strictly increasing, both endpoints exact, bit 15
   ignored, all three channels agreeing.
3. **The tile gather is COMPACT (raster order), not "read the whole tileOffset span".** The 1D/2D
   mapping is applied once, in `pspr_gather_tiles`, so a 2D-mapped sprite costs the same 64 reads
   and 256 bytes as a 1D one instead of 784 reads and 3136 bytes. `pspr_decode` therefore never
   sees `map1d`. T3/T4 drive both modes end to end through a fake VRAM, so the "tile soup"
   regression is still caught — it is asserted on the ADDRESSES that would really be formed.
4. **`test_typography.c` (phase 19's file) was touched** — one budget number, 13 → 14, with the
   reason written in. The new HUD `spr:` line is a *second* `ui_text` and that suite's T12 counts
   them. Its own failure message says: "route it through `assets_text(role)`; if it genuinely
   cannot be, raise the budget here WITH the reason." Routing it through the baked face would make
   the two halves of one debug readout disagree in font and size, and the baked faces are ~2× wider
   at their smallest — which is what pushed these lines off the 400 px screen in the first place.
5. **`gPlayerAvatar.flags` is read but NOT written into `PeerPresence.avatarFlags`.** S1.3 proposed
   filling the reserved wire field. It lands in `PsprCapture.avatarFlags` and `g_presDiag` instead:
   growing the 48-byte record is an M4 decision, not a rendering one, and nothing consumes the wire
   field today. The read is still made, so S1.8's 12-header-read count is honest.
6. **The capture gate is the PREVIOUS frame's `presDraw[gi ^ 1]`.** The parked window runs before
   `presence_solve`, so this frame's verdict does not exist yet. Cost of being one frame late: one
   frame of placeholder when a peer first comes into view. The alternative — reading
   unconditionally — would spend 28 reads per game per frame on a peer nobody can see.
7. **Clause 4 is split** into `PSPR_R_NOSURF` (the sheet was never built) and `PSPR_R_TILEFMT` (the
   §S3.7 proof went red), because S4.2 defines both codes and a single flag could not produce them.

---

## What is still unproven without pixels — the honest list

Nothing below is a *known* problem; each is a claim this slice could not close on a PC.

| # | claim | how it gets settled |
|---|---|---|
| Q1 | `gbacore_read16(c, 0x04000000)` really returns `DISPCNT` under this mGBA build | one boot; `g_presDiag.sprFlags` bit 0 / the HUD's ` noio` suffix. **Correctness does not depend on it** — a `0x0000`/`0xFFFF` answer falls back to the pret-cited mode-0/1D default and says so. |
| Q2 | **the §S3.7 tiling proof passes on the device** | `g_presDiag.sprTileOk` on the first boot. If `GX_TRANSFER_FLIP_VERT(0)` does something other than "row 0 stays row 0" the proof goes red, the live path stays off for the run, and the placeholder still draws — the wrong answer was made safe rather than the right answer certain. |
| Q3 | Ruby / Sapphire | VERIFIED-SYM only; no RS ROM exists here. Proof: boot Ruby beside Sapphire, check `sprReason == 0`. |
| Q4 | the edge bleed is worth its cost | one A/B capture in a dark cave (SPEC S6 #5). |
| Q5 | 32×32 forms (bike / surf) anchor correctly | argued from pret's subsprite tables (`.x = -16, .y = -16` vs `-8, -16` — same bottom edge); **never observed**, because the fixture saves may not own a bike. `sprGfxId` / `sprAvFlags` in the diag will say whether the form ever changed even without a photograph. |
| Q6 | the 1-texel seam at sheet row 96 | the live block sits directly under the placeholder's last row with no gutter, so a `GPU_LINEAR` tap at the sprite's top edge could pick up a fraction of placeholder ink under tilt. **This is inherited, not new** — the phase-15 placeholder's own 18 cells are packed edge-to-edge and were hardware-validated that way. Verify-on-hw; if it shows, the fix is a 1-px gutter and a 31-px cell for 16×32 forms. |
| Q7 | **it actually looks like the peer** | §S6 — the location sweep, the motion capture and the `gender.py` stretch goal. **Not started; needs Azahar, which phase 19 holds.** |

## Next

* **P5 / §S6** — six captures + the motion recording, once the emulator is free. The single most
  decisive image is the `gender.py` one (flip seat B's `SaveBlock2.playerGender` in the *fixture
  copy*): two visibly different trainers on the two screens is a frame no placeholder can produce.
* On the first hardware/emulator boot, read `g_presDiag` at `sprTileOk` (0xbc) and `sprFlags`
  (0xc0) before anything else — they answer Q1 and Q2 in one shot.

---

## 2026-08-13 — slice P5 / §S6: the emulator slice. **Azahar taken (shared with phase 19).**

Banked incrementally, as the findings landed, rather than at the end.

### Two harness facts that cost a boot each, and are worth more than the boots

**H1. A CTM movie that RUNS OUT stops Azahar's emulation and wedges the gdb stub.** Every
previously-shipped movie (smoke Tier A/B, the phase-18 door movies) ends with an in-app QUIT, so
playback never simply *finished* before — and the failure does not announce itself. What you see is
a session that renders a few frames and then freezes: two `see shot`s 60 s apart are **byte-
identical**, the app's own fps counter and clock are frozen mid-digit, and every `gdbio` read fails
with `timeout (5.0s) waiting for stub data` (the stub is serviced from the CPU thread, so a stopped
CPU cannot answer). It is indistinguishable, from the outside, from an app hang — and I first
mis-diagnosed it as exactly that, and then as a phase-20 regression.

The A/B that settled it: the same boot with `presence = 0` froze identically, so it was not phase 20;
the same boot with a movie whose trailing `wait` is **150 000 frames** (`movie_pick_long.ctm`,
8.3 MB) ran fine, gdb answered, and the games booted. Timing confirms it exactly — the freeze lands
where the movie's 1690 frames run out (~15 s of 60 fps picker + ~40 s of 17 fps session).

*Rule for any run that must outlive its own input: pad the movie's tail past the whole session.*

**H2. Never `pkill` the broker — it spends the boot's one gdb client.** Killing `gdbio serve`
closes the RSP socket without the `c` that `gdbio detach` sends first, and this Azahar's stub does
not re-listen: the port goes closed and no new broker can attach. (I did this while chasing H1, and
lost a boot to it.) The per-op RSP timeout is 5 s and a client `--timeout` does **not** reach the
broker, so under two saturated GBA cores reads can legitimately time out — that is not a reason to
restart the broker, and in this session it turned out to be H1 anyway.

### Q1 and Q2 — both open questions from P1–P4, ANSWERED on the first live session

Read straight out of `g_presDiag` (decoder: `evidence/tools/presdiag.py`, one 0xC4-byte read so
every field is the same frame):

```
magic=50525331 frame=3220 enabled=1 artOk=1 pairReason=0 sprTileOk=1 sprFlags=0x00
```

* **Q2 → GREEN. `sprTileOk = 1`.** The §S3.7 init proof passed on the device: our CPU Morton
  tiled-texture encoder is **byte-identical** to what `C3D_SyncDisplayTransfer` produces, including
  block-row order, byte order and the `GX_TRANSFER_FLIP_VERT(0)` row convention. This was the one
  genuine risk in the upload path and the reason the live path was built to fail safe.
* **Q1 → GREEN. `sprFlags = 0x00`, i.e. no `PSPR_D_NOIO`.** `gbacore_read16(c, 0x04000000)` really
  does return DISPCNT under this mGBA build, so the mode/1D-2D-mapping bits are *read*, not assumed.
  The pret-cited fallback was never needed.

### A real defect in the *fixture* tooling: `place.py` desyncs the camera from the player

`SaveBlock1.pos` is the **camera** tile (`presence.h:150` says so). Patching it in the save does
**not** move the player: on CONTINUE the game spawns the player from its own warp/location data, and
`pos` is thereafter advanced *incrementally* by the field engine. So a same-map `place.py` x/y patch
leaves a permanent offset between `pos` and `gObjectEvents[0].currentCoords`, and presence's
object-agreement gate (P-G7) correctly refuses to draw a peer at a tile the world does not back.

Proved from the app's own `s_gsLog` ring, read live over gdb (148-byte `GsLogEntry`, `px/py/objX/objY`
at +14):

```
i=663 f=13278 scr=1 ctx=1 sb1V=1 res=0  pos=(11,5) obj=(16,12)  obj-pos=(+5,+7)  map=10-6
```

`objY - py = +7` = `MAP_OFFSET`, correct. `objX - px = +5` — short by exactly the **+2** that
`place.py` added to x. The live player is at x = 9, the camera thinks 11. Walking does not heal it
(a `R2` moved `pos.x` 11 → 13, keeping the +2 error), and it is visible in the capture: seat B's
trainer sits left of centre while seat A's is centred.

**This retro-explains phase 18's run 4** ("steady state settles on `reason 8 (obj)` / `12 (selfobj)`"),
which was recorded there as a puzzle. It is not a presence bug — presence is right and the fixture
was lying to it.

*Rule: `place.py` is safe for changing the MAP (a real warp re-syncs everything) and unsafe for
moving within the map already loaded. Separate the two seats by WALKING them with the D4 channel,
which moves player and camera together.*

### Recipe that works (for whoever runs the sweep next)

1. `azctl boot --gdb --movie <LONG>.ctm --fresh-sd-fixtures` — parks pre-first-instruction.
2. In the parked window: `cp gameA.gba gameB.gba`, `cp gameA.sav gameB.sav` (co-op needs one map
   universe; the picker refuses the same FILE twice, so it must be two files), `setpref.py
   settings.bin presence 1`. Only `place.py` for a MAP change.
3. `gdbio resume`; the movie drives the picker (A, A, DOWN, A, X).
4. D4 through the GBA boot, per seat: `s W90 s W90` (intro → title), `s W150` (→ main menu),
   `a W240` (CONTINUE → overworld). Verify with `see shot` between steps.
5. Separate the seats with a real walk: `sdmc drop move 2 "R2"`.

### What the slice delivered — `RESULTS.md` has the full read-out

* **G1 the genuine sprite: PROVEN.** `SPR reason=0(ok) 16x32 gfxId=0x59 uploads=65/72` read over gdb
  at the captured frame, on both games — so the avatar on screen is the live cell, not a fallback
  that happens to look right. `evidence/g1-mauvillepc.{top,bottom}.png`, native reconstructions and
  an 8× zoom.
* **G2 animation + flip: PROVEN**, and by state rather than by eye. `see rec` read `sprAnim[1]` and
  `sprPal[1]` at all 120 frames (`state_error: null`). `animCmdIndex` cycles 0→1→2→3 within one
  direction (the stride), `animNum` goes 4/6/7/5 in exactly the order the D4 script walked
  south/west/east/north, and `hFlip` is 1 **only** for east. Frames 12 and 22 show the identical
  artwork mirrored the correct way round.
* **G3 locations: three**, all reached by WALKING (see the `place.py` finding): PC 2F link room, PC
  1F lobby, Mauville City outdoors in daylight. Both screens, native resolution, plus zooms.
* **G4 the fallback: OBSERVED, not simulated** — `reason=0(ok) drawn=1` with
  `SPR reason=9(notinuse)` while the peer's game sat on the main menu; the phase-15 placeholder drew
  cleanly over it. No garbage, no crash.
* **Q1 and Q2: both GREEN** (see above).
* Contact sheet for the user: `evidence/peer-sprite.html`.

### New evidence tooling (all under `evidence/tools/`)

`presdiag.py` (one 0xC4-byte gdb read → the whole `PresDiag`, decoded, so every field is the same
frame), `setpref.py` (read/patch/backup/restore one field of the app's `settings.bin` — the co-op
pref cannot be toggled from the app's menu once a movie has finished, because D4 injects GBA keys
and not 3DS HID), `gender.py` (§S6.3; **written and round-trip tested byte-exact, NOT used** — the
sweep never got a boot to spare for it), `movie_pick_long.{json,ctm}`, `sweeploc.sh`, `runloc.sh`.

`gender.py` verified its own offsets rather than trusting them: the name at `SaveBlock2 +0x00`
decodes through the Gen-3 charmap to **"GUYA"**, which is exactly the nameplate the phase-18 capture
drew — an independent confirmation of the struct base before anything was written.

### Housekeeping / handover

* The user's `settings.bin` `presence` pref was snapshot before the first boot and **restored to 0**
  (their own value). `sdmc/dual-gba/` was never written; `azctl` re-hashed the originals after every
  stop and reported "all untouched" each time.
* **Left behind:** the staged fixtures `sdmc/3DGBA/gameB.{gba,sav}` are still COPIES of `gameA`
  (Emerald+Emerald, which is what co-op needs). `azctl clean-fixtures` was refused because another
  session had already taken the emulator back. Any boot with `--fresh-sd-fixtures` restores them, so
  this is self-healing, but run `azctl clean-fixtures` when the emulator is next free to be tidy.
* Emulator etiquette held: the lock was waited for, never forced, no foreign Azahar was killed, and
  the instance was stopped between phases.

### Next

* The remaining §S6 locations need a walked route or a fixture tool that moves the PLAYER (§S6's
  `place.py` plan does not work — see above). Phase 18's door slice used `place.py` the same way and
  should be re-checked on the same suspicion.
* Q5 (32×32 bike/surf forms) never fired: `gfxId` stayed `0x59` all session.
* `gender.py` is ready and unused — one boot with it would give the "two visibly different trainers"
  frame that §S6.3 calls the most decisive image in the set.
* Unchanged and still binding: the frame budget with the live path on is a **real-hardware** question
  (CLAUDE.md #6). Azahar ran 17–20 fps here, which is its own ceiling for two GBA cores and proves
  nothing about the New 3DS.
