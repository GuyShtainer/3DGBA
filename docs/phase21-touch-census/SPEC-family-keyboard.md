# SPEC — touch family: KEYBOARD (the Gen-3 naming screen)

_Phase 21/22 design spec. NO implementation in this document. Written 2026-08-14 from the
banked census (CATALOG E10, CB2-HARVEST, VISITED-*, evidence captures) + pret source. All
coordinates are GBA-frame pixels (240×160) — the same space every `hit_*` in `touch.c`
already works in._

**Scope.** Catalog row **E10** — `naming_screen.c` in both engines: new-game player name
(A4), rival name (FR A4), mon nickname (caught / party / storage), box name, Walda phrase
hop (EM D9 enters via easy-chat, not here). One screen, five entry flows, identical UI.
**Games: BPEE + BPRE rev1 (BPGE rev1 = BPRE verbatim, addresses from `pokeleafgreen_rev1.sym`).
AXVE/AXPE: the RS naming screen is a different, older module — explicitly deferred** (RS
profiles are walk-only and rev-2-unverified; see CATALOG "RS vs Emerald deltas").

**Why this is the single biggest touch win:** the naming screen is pure grid-typing — the
worst possible fit for the current GCTX_OVERWORLD residual (taps leak walk keys into a
keyboard) and the best possible fit for a touchscreen. It is also self-contained: one cb2,
one struct, no scrolling, no variants.

---

## 1. Ground truth (verified from source + census)

### 1.1 Identity

| Game | cb2 (run loop) | Symbol | Status |
|---|---|---|---|
| BPEE | `0x080E4F58` | `CB2_NamingScreen` | **live-verified** (CB2-HARVEST boot #2, [exact], evidence `emerald/E10-naming.*`) |
| BPRE r1 | `0x0809FB84` | `CB2_NamingScreen` | sym-derived (`pokefirered_rev1.sym`) — **verify-live-pending**; FR evidence `firered/E10-naming.*`, `E10b-naming-lower.*` exists, cross-check the FR harvest table before promoting |
| BPGE r1 | from `pokeleafgreen_rev1.sym` | `CB2_NamingScreen` | sym-derived, **never executed** |

Detection = cb2 equality (the same gate pattern as `battleMainCb`). New context:
**`GCTX_NAMING`**, checked before the overworld fall-through.

### 1.2 The keyboard model (pret `naming_screen.c` — EM local clone; FR master fetched
2026-08-14, line-for-line identical in everything cited below)

- Grid: **4 rows × up to 8 character columns, plus a 3-slot button column** on the right.
- The game's own cursor lives in a **sprite**: `gSprites[sNamingScreen->cursorSpriteId]`,
  logical position in `data[0]`=col, `data[1]`=row (naming_screen.c `#define sX data[0]`,
  `sY data[1]`, ~line 1011 EM / 1029 FR); pixel position `x = sPageColumnXPos[page][col] + 38`,
  `y = row*16 + 88` (`SetCursorPos`, EM :1130-1144).
- Column tables (EM :301-310, FR :399-410, **identical values**):
  - LETTERS (upper & lower), 8 columns: xPos `{0,12,24, 56,68,80, 92, 123}` → cell centers
    `{38,50,62, 94,106,118, 130, 161}`; row texts `ABCDEF .` / `GHIJKL ,` / `MNOPQRS` /
    `TUVWXYZ` (col 6 = space, col 7 = `.` row0 / `,` row1, absent rows 2-3 — the table pads
    with terminator; a tap there must be dead).
  - SYMBOLS, 6 columns: xPos `{0,22,44,66,88,110}` → centers `{38,60,82,104,126,148}`;
    rows `01234` / `56789` / `!?♂♀/-` / `…""''` (col counts vary by row: 5/5/6/5).
- Rows: cell centers y = `{88,104,120,136}`, 16 px pitch.
- Button column sprites (EM :1228 + `CreateBackOkSprites`): PAGE at (204,88), BACK at
  (204,116), OK at (204,140). On-screen labels: "upper/lower/others + SELECT",
  "BACK + B BUTTON", "OK + START" — **the game itself advertises the key shortcuts**.
- Pages cycle SYMBOLS→UPPER→LOWER→SYMBOLS; current page in `sNamingScreen->currentPage`
  (0=SYMBOLS, 1=UPPER, 2=LOWER — the KBPAGE order, EM :87-91).
- Input model (EM :1580-1656): `JOY_NEW` for A/B/SELECT/START, `JOY_REPEAT` for D-pad.
  `SELECT` = swap page, `B` = backspace (`DeleteTextCharacter`), `START` =
  `MoveCursorToOKButton()` (instant `SetCursorPos(colCount, 2)`, EM :1154-1157), `A` =
  act on cursor (`GetKeyRoleAtCursorPos` :1184-1194: col < colCount = char, else button
  row 0/1/2 = PAGE/BACK/OK).
- Input is live only while `sNamingScreen->state == STATE_HANDLE_INPUT` (=2, enum :122-133);
  page-swap anim and OK-commit disable it (`SetInputState(INPUT_STATE_DISABLED)`).

### 1.3 `struct NamingScreenData` offsets (annotated in FR source :150-177; EM struct
identical field-for-field, verified side-by-side this session)

| Offset | Field | Use here |
|---|---|---|
| +0x1800 | `textBuffer[16]` | the live typed text — **the proof channel** |
| +0x1E10 | `state` | gate: only act when ==2 (STATE_HANDLE_INPUT) |
| +0x1E22 | `currentPage` | col-count + char mapping; page-swap proof |
| +0x1E23 | `cursorSpriteId` | resolve the cursor sprite |
| +0x1E28 | `template` ptr | optional: maxChars at template+1 |
| +0x1E30 | `destBuffer` ptr | commit target (OK-time proof) |

### 1.4 RAM anchors (per game)

| Anchor | BPEE | BPRE r1 | Status |
|---|---|---|---|
| `sNamingScreen` (EWRAM ptr) | `0x02039F94` | `0x0203998C` | sym-derived (`pokeemerald.sym` / `pokefirered_rev1.sym`); statics have fixed addresses; **verify-live before promote** (one gdb read while on E10 — ptr must be in EWRAM range) |
| `gSprites` | `0x02020630` | `0x0202063C` | sym-derived, global; same rule |
| struct Sprite | stride **0x44**; `x` +0x20, `y` +0x22, `data[0..7]` +0x2E | same | pret `include/sprite.h` annotations (byte-matched builds) |

House rule note: none of these four have been executed live yet → every one is
**verify-on-emulator-pending**; the proof plan (§5) is exactly that verification. The cb2
for EM is the only line already live-proven.

---

## 2. Touch design

### 2.1 Hit geometry (derived from the game's own tables, §1.2 — not measured pixels;
the captures confirm the tables visually)

- **R1.** Character cells: rows `r = (gy-80)/16` for gy ∈ [80,144); columns resolve to the
  **nearest cell center** for the current page (centers per §1.2), accepted only if
  `|gx - center| <= 6` on letter pages (12 px cells) and `<= 11` on symbols (22 px cells).
  Anything else in the keyboard area is dead (the inter-group gutters at gx 68..88 etc.
  must NOT snap — mistypes are worse than ignored taps).
- **R2.** Page-dependent validity: col count 8/8/6 by `currentPage` (read every frame);
  on letter pages rows 2-3 have no col 7; symbols rows have their §1.2 row lengths. A tap
  on a nonexistent cell is dead.
- **R3.** Button column: gx ∈ [176,232) with y bands PAGE [76,102), BACK [102,128),
  OK [128,154) (centers 88/116/140 ± half-pitch, made contiguous so the column has no
  dead gutters).
- **R4.** Everything else on the screen (title, text-entry box, backdrop) is dead EXCEPT
  the text-entry underscores row (see R11) and the reserved app menu chip (unchanged).

### 2.2 Tap → action synthesis

- **R5. Character tap = the house write-then-A idiom** (same shape as `select_pulse`,
  touch.c:164-170): tick 0 write only, tick 1 write + A, then clear. The write places the
  game's own cursor on the tapped cell:
  - chain: read32 `sNamingScreen` → validate (R9) → `id = read8(ptr+0x1E23)` →
    `spr = gSprites + 0x44*id`;
  - write16 `spr+0x2E` = col (sX), `spr+0x30` = row (sY), `spr+0x32` = col, `spr+0x34` =
    row (sPrevX/sPrevY — keeps the squish anim coherent);
  - write16 `spr+0x20` = center.x, `spr+0x22` = center.y (the visual cursor follows —
    unlike party/bag we CAN move the real highlight, because it is a sprite, not a
    reprint);
  - then the A pulse. The A lands as `JOY_NEW(A)` → `HandleKeyboardEvent` reads the key
    role at the just-written cursor → `AddTextCharacter` commits the tapped char.
- **R6. PAGE tap (and R3 PAGE band) = inject SELECT pulse.** No RAM write — `SELECT` is
  the game's own page-swap. The button shows the NEXT page name (game behavior); reaching
  a specific page can take two taps, exactly like the physical UI. Two-tap acceptance is
  by design; do not build a "jump to page X" writer in v1.
- **R7. BACK tap = inject B pulse** (`DeleteTextCharacter`), works from any cursor
  position, no write.
- **R8. OK tap = pure-key two-step sequence:** frame t: START (cursor jumps to OK —
  instant, §1.2), frame t+1: release, frame t+2: A. Implemented as a 3-frame mask queue
  inside the naming handler (see §3). No RAM write, no risk of committing with a stale
  cursor.
- **R9. Validation gates — ALL must pass before any write or synthetic key:**
  (a) profile cb2 == naming cb2 (GCTX_NAMING);
  (b) `sNamingScreen` read32 ∈ [0x02000000, 0x02040000);
  (c) `cursorSpriteId` < 64;
  (d) `state` (ptr+0x1E10) == 2 (STATE_HANDLE_INPUT) — drops taps during page-swap anim,
      OK commit, fades (the game would ignore the keys anyway; we must not leave a
      half-executed write-then-A pending);
  (e) `currentPage` (ptr+0x1E22) < 3.
- **R10. Fallback when uncertain:** if (a) holds but any of (b)-(e) fails, the handler
  emits **nothing** for grid taps and the mode behaves as PAD for that frame (the
  universal fallback — virtual gamepad zones still let the user type by hand). NEVER fall
  through to the overworld walk residual while (a) holds: GCTX_NAMING must swallow the
  tap. This is the family's core safety property — today's residual injects walk keys
  into the keyboard; after this spec, worst case is "tap did nothing".
- **R11. Text-entry row (optional, v1.1):** tap on the underscore strip (y ∈ [52,68),
  x from `inputCharBaseXPos`, ptr+0x1E16) = BACK (B) — a natural "delete here"
  affordance. v1 may ship without it; the band stays reserved (dead) in v1.

### 2.3 What a tap must NOT do

- **R12.** No D-pad keys are ever injected in GCTX_NAMING (no cursor walking — placement
  is by write, §2.2). This kills the residual's worst behavior on this screen.
- **R13.** Never write `textBuffer`, `destBuffer`, `state`, or `currentPage` directly —
  all text mutation goes through the game's own handlers via A/B/SELECT/START. (The
  cursor sprite fields are the only written state, and they are exactly what the game's
  own `SetCursorPos` writes.)
- **R14.** Holds repeat nothing: a held touch on a char cell types once (tap on release
  like the bag, or edge-triggered on press — pick ONE in implementation and log it;
  recommendation: act on press for typing snappiness, ignore until release, drag ≤6 px
  slop cancels nothing since there is no drag gesture here).

---

## 3. Seam verdict (the question this spec was asked to answer)

**No injection-seam change is required.** Reasoning against COVERAGE §5:
- The design needs: per-frame masks (A/B/SELECT/START pulses — already first-class),
  a 3-frame ordered sequence for OK (expressible: the handler is called every frame and
  returns a mask; a tiny per-handler queue like `s_*Tick` already ships in five places),
  and RAM writes of u8/s16 fields (write8/write16 exist; nothing needs write32).
- Fast repeated cursor moves are NOT needed because placement is write-based (R5); the
  one sequence (R8) is 3 frames. The seam's additive-only and one-mask-per-frame limits
  are never hit.
- New handler-internal state (pending cell, tick, OK queue) mirrors `s_bag*`/`s_fmenu*`
  — touch.c-local, seam untouched.

Profile additions (positional append per the house rule, gamestate.h:93-95):
`namingCb` (ROM), `sNamingScreenPtr` (EWRAM), `gSpritesBase` (EWRAM). GCTX enum +
`touch_update` case + `all_reset` hook extend by hand as documented in COVERAGE §4.

---

## 4. Per-game instantiation table

| Item | BPEE | BPRE r1 / BPGE r1 | RS |
|---|---|---|---|
| cb2 gate | 0x080E4F58 ✅ live | 0x0809FB84 / LG-sym ⏳ | — deferred |
| sNamingScreen | 0x02039F94 ⏳ | 0x0203998C / LG-sym ⏳ | — |
| gSprites | 0x02020630 ⏳ | 0x0202063C / LG-sym ⏳ | — |
| geometry (§2.1) | from EM source ✅ | **same tables, verified in FR source + captures** ✅ | different module |
| entry flows | box name ✅ visited; nickname/Walda reachable | box name/nickname reachable; player+rival = new-game | — |

(✅ = verified this census; ⏳ = sym-derived, live-confirm before promotion.)

---

## 5. Emulator PROOF PLAN (phase-22 lane A — nothing runs while the census workflow owns
the emulator)

Route: the exact VISITED-emerald E10 path — Lanette's PC → MOVE POKEMON → box title menu
→ NAME (D4 channel), then touch taps. FR: storage box naming via the same PC family.

- **P1 — a tap landed = the naming buffer changed.** gdb chain:
  `p = read32(sNamingScreen)`; watch `p+0x1800..p+0x180F`. Tap cells for P, I, K, A on
  the UPPER page → bytes become `CA C3 C5 BB` then `FF` (EOS) — charmap values verified
  from pret `charmap.txt` this session ('A'=0xBB … 'P'=0xCA; lowercase 'a'=0xD5,
  '0'=0xA1, '!'=0xAB for the other pages). **This read IS the "keyboard taps work"
  proof** — one symbol chain, no screenshot needed (screenshots stay as evidence).
- **P2 — cursor write landed** (isolates R5's write from the A): after tick 0, read
  `spr+0x2E/+0x30` == tapped col/row; the touch log's before/after cursor columns
  (touch.c logger already captures per-event pre/post — wire the naming cursor read into
  it) show the same.
- **P3 — page swap:** tap PAGE → `p+0x1E22` cycles 1→2→0→1; type one char per page and
  P1-check its charmap byte (proves the col-count remap, R2).
- **P4 — BACK:** buffer tail byte returns to 0xFF; length shrinks by exactly 1 per tap.
- **P5 — OK commit:** tap OK → `p+0x1E10` leaves 2 (→ STATE_PRESSED_OK=6 → fades), cb2
  edge back to `CB2_PokeStorage` 0x080C7D54 (live-harvested) appears in the gs ring, and
  for the box-name flow the new name is at the destBuffer target (read `p+0x1E30` before
  OK, read the string there after).
- **P6 — no-leak safety (R10/R12):** with GCTX_NAMING active, the touch log's `inj`
  column over a full scripted session contains only A/B/SELECT/START bits — zero D-pad
  bits. Negative control: tap the dead gutters (R1) → zero events.
- **P7 — FR parity:** repeat P1-P6 on FR rev1 with the FR addresses; P1 charmap bytes are
  identical (same charset).

Hardware note: nothing here is timing-critical beyond the 3-frame OK queue; still, the
family follows the house "done" gate — emulator-proven per this plan, then one hardware
pass (naming a box end-to-end) before the family is declared shipped.

## 6. Open questions (tracked, non-blocking)

- Q1: act-on-press vs act-on-release for char taps (R14) — decide in implementation,
  A/B test on hardware.
- Q2: R11 text-entry-row delete — v1.1.
- Q3: LG smoke run (S4 lane B) to turn the BPGE column live.
