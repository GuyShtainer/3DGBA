# Phase 21 — THE SMART-TOUCH MASTER PLAN

_Synthesis of the census (CATALOG.md, 143 rows), the coverage audit (COVERAGE.md), the two
emulator visit passes (VISITED-emerald.md + VISITED-firered.md, 61 distinct catalog rows
photographed and cb2-certain), the live fingerprint harvest (CB2-HARVEST.md, ~60 [exact]
zero-guess resolutions), and the three overnight family specs (SPEC-family-keyboard.md,
SPEC-family-lists.md, SPEC-family-traversal.md). Written 2026-08-14. **No touch was
implemented in this phase — this plan is the deliverable.**_

---

## 0. The three facts that shape everything below

1. **The residual is the enemy, not missing features.** Today every unfingerprinted screen
   falls through to GCTX_OVERWORLD with the save block still valid, so the FULL walk
   machinery runs against the invisible map underneath: holds leak steering D-pad into the
   Pokédex, taps arm BFS routes inside the naming screen (COVERAGE §1a). The single
   highest-value change is therefore **detection, with a safe default (tap=A, hold=B,
   nothing else)** — that alone turns ~40 photographed screens from "actively hostile" to
   "usable". The harvest already contains every cb2 needed, live-verified [exact].
2. **FireRed touch is entirely dead on the user's cart, and we know exactly why.** The BPRE
   profile carries rev0 ROM anchors; the cart is rev1; five anchors moved 0x14–0x78
   (VISITED-firered headline table). Every EM positive (fmenu/party/bag/battle) is a live
   NEGATIVE on FR. The rev1 replacement values are all harvested. Same class of bug on
   LeafGreen (13 wrong ROM pointers, exact fixes ready in RS-REV2-VERIFICATION.md §6).
   **Nothing FR/LG in this plan works until that promotion lands — it is slice 0.**
3. **The injection seam blocks nothing.** Per-frame additive key mask (all ten keys, holds
   first-class, chords possible) + write8/write16 + the write-cursor-then-A idiom cover
   every design in this document (COVERAGE §5; both family specs independently reached the
   same verdict). The honest limits: no key suppression, no sub-frame timing (list repeat
   = the game's own JOY_REPEAT cadence), bottom game only, no write32. No seam change is
   required for any slice below.

**Overnight head start (already built, in a worktree, awaiting merge):** branch
`phase21-cb2-promotion` (commit d370f2a) promotes 40 [exact] fingerprints into
`gamestate.c` as two new contexts — `GCTX_TITLE` (save block not loaded; tap=A/START only)
and `GCTX_FULLUI` (detected full-screen UI; safe default) — plus the FR-rev1 battle anchor
(`battleMainCbAlt 0x08011114`), with 14 suites green. Slice 0 = merge + verify + extend
with the FR task anchors.

---

## 1. THE MASTER MATRIX

Column key:
- **Now** — what touch does today on the user's carts. `leak` = GCTX_OVERWORLD residual
  (walk keys leak in); `leak-safe` = residual but save block not loaded, tap=A only.
  EM positives are marked; **on FR every "✓" is dead until slice 0** (rev0 anchors).
- **Target** — the touch-native design, by family (§2).
- **Class** — `DONE` (already touch-native) · `TAP` (tap-advance/safe-default is enough)
  · `FULL` (full mapping worth building) · `DEFER` (with reason).
- **Fam** — design family (§2). **Eff** — S/M/L implementation effort.
- **Sym** — new profile symbols needed beyond the harvest: `-` none · `promo` = in the
  harvested/promotion set already · `substate` = internal state/task-fp read to capture
  · `sym` = sym-derived, verify-live before promoting · `rev1` = blocked on the FR/LG
  rev fix · `rs2` = RS rev2-verified on paper, never executed.
- **Ver** — strongest evidence today: `hw` (real 3DS), `emu` (this census, cb2-certain),
  `sym` (paper only), `—` (not visited).
- **B1:** — phase-24 lane B1's live verdict for every `TAP` row
  (`docs/phase21-touch-census/LANE-B-TAPVERIFY.md`, 2026-08-14):
  `VERIFIED` = the screen was reached and the verb proved itself with a state delta ·
  `VER-mech` = the screen was not visited but it is literally the same dispatch arm that was
  proved live (a field dialog differs only in which NPC is talking) ·
  `BROKEN` = the row's premise is false, with the diagnosis in the lane log ·
  `UNREACH` = the mechanism is in place, this save/session cannot get to the screen.
  Tally: **6 VERIFIED · 19 VER-mech · 10 BROKEN · 13 UNREACHABLE**. Nine of the ten BROKEN rows
  share one diagnosis — the screen has **no cb2 fingerprint**, so the "safe default" the `TAP`
  class assumes never runs there. That is a census gap, and the per-screen harvest plan is
  written out at the end of the lane log.

### A. Boot, title, file select

| # | Screen | Now | Target | Class | Fam | Eff | Sym | Ver |
|---|---|---|---|---|---|---|---|---|
| A1 | GF intro | leak-safe (tap=A) | tap=A skip; nothing else | TAP | DLG | S | promo | emu · **B1:VERIFIED** |
| A2 | Title screen | leak-safe | tap=Start; DONE once GCTX_TITLE lands | TAP | DLG | S | promo | emu · **B1:VERIFIED** |
| A3 | Main menu | leak-safe | list rows tappable (Continue/New/Gift/Option) | FULL | LIST | S | promo+substate (menu task) | emu |
| A4 | New-game intro (Birch/Oak) | leak | tap=A advance; naming hop = FAM-KB; gender yes/no = fmenu | TAP | DLG+KB | S | promo | emu (FR, accidental) · **B1:UNREACH** |
| A5 | Options menu | leak | rows tappable; tap L/R halves of value = LEFT/RIGHT slider | FULL | LIST | M | promo+substate | emu |
| A6 | Clear-save screen | leak | detect + tap=B only (NEVER map yes) — safety screen | TAP | DLG | S | sym | — (chorded boot combo; D4 can't) · **B1:UNREACH** |
| A7 | Reset RTC (RSE) | leak | defer — obscure, D-pad digits fine | DEFER (niche) | GRID | — | sym | — |
| A8 | Berry-fix multiboot | leak | defer | DEFER (2P) | — | — | — | — |
| A9 | Save-failed screen | leak | nothing (fault path) | DEFER (fault) | — | — | — | — |
| A10 | Link error | leak | tap=A acknowledge | TAP | DLG | S | sym | — · **B1:BROKEN** |

### B. Overworld & field layer

| # | Screen | Now | Target | Class | Fam | Eff | Sym | Ver |
|---|---|---|---|---|---|---|---|---|
| B1 | Overworld | **tap-to-walk BFS + steer + door terminals ✓** | keep; extend via FAM-TRAV (HM edges, cross-map warps) | DONE→FULL (trav) | TRAV | L | trav spec tables (sym) | hw (core) / suite (warps) |
| B2 | Start menu | fmenu ✓ (EM) | done; FR needs rev1 anchors | DONE | POPUP | S | rev1 | hw-adjacent (EM) |
| B3 | Script dialog | tap=A via walk path | tap=A, **hold ≥½s = B** (decline/speed-through); stop walk-key leak under dialogs | TAP | DLG | S | - | hw · **B1:VERIFIED** |
| B4 | Yes/No + multichoice | fmenu ✓ (EM) | done; FR rev1 | DONE | POPUP | — | rev1 | emu |
| B5 | Save dialog | fmenu ✓ (EM) | done | DONE | POPUP | — | rev1 | emu |
| B6 | Trainer card | leak | tap=flip (A), second tap=exit; page dots optional | TAP | PAGE | S | promo | emu · **B1:UNREACH** |
| B7 | Region map (field/wall) | leak | tap = move the map cursor to that map square (write cursor, no key mash); drag = pan | FULL | MAP | M | promo+substate (cursor addr) | emu |
| B8 | Fly map | leak | **tap-to-fly**: tap a city = write cursor + A, confirm popup = fmenu | FULL | MAP | M | promo+substate | emu |
| B9 | Wall clock | leak | view: tap=exit. set: defer (new-game only; drag-the-hands is a gimmick) | TAP | DLG | S | promo | emu · **B1:UNREACH** |
| B10 | Itemfinder sweep | leak (dialog blocks walking!) | tap=A dismiss; the FULLUI default covers it | TAP | DLG | S | - | emu (FR) · **B1:VER-mech** |
| B11 | Fishing | tap=A works by accident | tap=A on the "!" — already the right verb; keep | DONE | MINI | — | - | emu (FR) |
| B12 | Safari HUD/gating | overworld ✓; START menu leak | fmenu covers RETIRE menu once detected | TAP | POPUP | S | rev1 | emu (FR) · **B1:VER-mech** |
| B13/B14 | Cable car / Seagallop | leak | tap=A; cutscene | TAP | DLG | S | - | — · **B1:VER-mech** |
| B15 | Braille walls | tap=A | done (dialog) | DONE | DLG | — | - | — |
| B16 | Secret base | overworld ✓ | works via B1; decoration placement = E-family grid below | DONE | TRAV | — | - | — |
| B17 | Decoration place/tidy | leak | lists = FAM-LIST; placement grid: tap tile = move cursor (grid write) — RSE-only, low priority | FULL (late) | GRID | L | substate | — |
| B18 | Map name popup | passive | nothing | DEFER (passive) | — | — | — | emu |
| B19 | Whiteout | leak-ish | nothing (cutscene) | DEFER (cutscene) | — | — | — | — |
| B20 | Field HM prompts | fmenu ✓ | done; superseded by FAM-TRAV auto-HM anyway | DONE | POPUP | — | - | emu |
| B21 | Rotating puzzles etc. | overworld ✓ | works via B1 (forced-move tiles stay a known walker limit, T5.8) | DONE | TRAV | — | - | hw |
| B22 | Dewford trends | dialog + easy chat | FAM-KB (easy-chat instance) | FULL | KB | S (after D8) | substate | — |

### C. Battle (all variants share BattleMainCB2 + the four input screens)

| # | Screen | Now | Target | Class | Fam | Eff | Sym | Ver |
|---|---|---|---|---|---|---|---|---|
| C1 | Transition/intro | b.oth tap=A (EM) | done | DONE | DLG | — | rev1 | hw |
| C2 | Action select | **b.act ✓ (EM)** | done ("battles is fine" — user); FR = rev1 | DONE | GRID | — | rev1 | hw (EM) |
| C3 | Move select | **b.move ✓ (EM)** | done | DONE | GRID | — | rev1 | hw (EM) |
| C4 | Target select | shipped, rects untuned | keep; tune ±28px rects on hw; doubles repro needed | DONE (tune) | GRID | S | rev1 | emu-untested |
| C5 | Battle dialog | b.oth ✓ | done | DONE | DLG | — | rev1 | hw |
| C6–C11, C16–C19 | battle variants (wild/safari/trainer/double/multi/link/tower…) | inherit C2–C5 | inherit; safari verbs are the same 2×2 grid (verified emu: C7) | DONE | GRID | — | rev1 | emu/hw |
| C12 | Recorded battle ask | leak | fmenu (yes/no) | TAP | POPUP | S | - | — · **B1:VER-mech** |
| C13/C15 | tutorials/Marowak | consumed by saves | inherit battle | DEFER (new-game) | — | — | — | — |
| C14 | Pokedude demo | leak (FR) | tap=A advance, B=quit chip (it's a TV show) | TAP | DLG | S | rev1 | emu · **B1:UNREACH** |
| C20 | Evolution scene | leak | tap=A, hold=B (cancel evolution is a real verb!) | TAP | DLG | S | promo-family | — · **B1:BROKEN** |
| C21 | Egg hatch | leak | tap=A; naming hop = FAM-KB | TAP | DLG | S | - | — · **B1:VER-mech** |
| C22 | Forget-move summary hop | leak | FAM-PAGE summary instance: tap a move row = select, tap page arrows | FULL | PAGE | S (with E1) | promo | emu |

### D. Emerald Battle Frontier & friends

| # | Screen | Now | Target | Class | Fam | Eff | Sym | Ver |
|---|---|---|---|---|---|---|---|---|
| D1 | Frontier Pass | leak | **the showcase**: the game runs a FREE PIXEL CURSOR (2px/frame) — touch = drive the cursor straight to the finger (write x/y or hold-direction closed-loop), tap=A. Most "touch-native candidate #1" screen in the census | FULL | MAP | M | substate (cursor var) | emu |
| D2/D3 | Factory rental/swap | leak | grid of 6 → tap = write cursor + A | FULL | GRID | M | substate | — |
| D4 | Dome tourney tree | leak | tap bracket node = cursor write; L/R page taps | FULL (late) | MAP | M | substate | — |
| D5 | Pyramid bag | leak | FAM-LIST instance | FULL | LIST | S (with lists) | substate | — |
| D6 | Pike/Palace/Arena/Tower halls | overworld+dialog | B1 + DLG defaults | TAP | DLG | — | - | — · **B1:VERIFIED** |
| D7 | Choose-frontier-party | party ✓ partially (choose mode is residual!) | extend GCTX_PARTY to choose-half mode (slot 6 = confirm) — known gap (COVERAGE §3) | FULL | GRID | S | promo | emu (E2d) |
| D8 | Easy-chat screen | leak | **FAM-KB second instance**: word-matrix keyboard (groups list → word grid) | FULL | KB | M | substate | — |
| D9 | Walda phrase | leak | = D8 | FULL | KB | — | substate | — |
| D10 | Trainer Hill records | leak | tap=A | TAP | DLG | S | - | — · **B1:VER-mech** |
| D11 | Apprentice | dialog+party | covered by fmenu/party/D8 | TAP | POPUP | — | - | — · **B1:VER-mech** |
| D12 | Lilycove Lady | dialog+bag hops | covered by DLG + bag | TAP | DLG | — | - | — · **B1:VER-mech** |
| D13 | Rayquaza scene | consumed | — | DEFER (cutscene) | — | — | — | — |
| D14 | Trader/Bard | dialog + D8 | covered | TAP | DLG | — | - | — · **B1:VER-mech** |

### E. Core menus

| # | Screen | Now | Target | Class | Fam | Eff | Sym | Ver |
|---|---|---|---|---|---|---|---|---|
| E1 | Summary (3/4 pages) | leak | FAM-PAGE flagship: swipe or tap edge-arrows = L/R page; tap move row = A-into-detail; B chip | FULL | PAGE | M | promo+substate (page idx) | emu |
| E2 | Party menu | **party ✓ (EM)** | done for SINGLE/DOUBLE; fix MULTI rect table + choose-mode (D7) | DONE (fix) | GRID | S | rev1 | emu |
| E2b | Party context popup | fmenu ✓ (EM) | done | DONE | POPUP | — | rev1 | emu |
| E3 | Bag (top level) | **bag ✓ (EM)**: tap/drag/swipe pockets | add: tap pocket ICONS directly (known-missing #3), blank-row clamp | DONE (polish) | LIST | S | rev1 | emu |
| E3b | Bag item sub-menu | **leak — GCTX_BAG drops out in sub-menus** (census finding) | fmenu instance (it IS an sMenu window) — detection-order fix | FULL | POPUP | S | promo | emu |
| E4 | Berry Pouch (FR) | leak | FAM-LIST free instantiation | FULL | LIST | S | rev1+substate | emu |
| E5 | TM Case (FR) | leak | FAM-LIST (cb2 = `CB2_Idle` — harvested, catalog corrected) | FULL | LIST | S | rev1+substate | emu |
| E6 | Berry tag (RSE) | leak | tap=exit, L/R flip taps | TAP | PAGE | S | promo | emu · **B1:UNREACH** |
| E7 | PSA anim (FR) | leak | tap=A (cutscene) | TAP | DLG | S | promo | emu · **B1:UNREACH** |
| E8 | Mail read | leak | tap=advance/exit | TAP | DLG | S | - | — · **B1:BROKEN** |
| E9 | Mail compose | leak | FAM-KB (easy-chat) | FULL | KB | — | substate | — |
| E10 | Naming screen | leak (worst offender: taps leak walk keys into a keyboard) | **FAM-KB flagship — full tap keyboard** (SPEC-family-keyboard.md, ready) | FULL | KB | M | promo + sNamingScreen (sym) | emu |
| E11 | PC top menu | fmenu ✓ (EM) | done | DONE | POPUP | — | rev1 | emu |
| E12 | Storage boxes | leak (all 6 sub-states photographed) | **FAM-GRID flagship**: tap slot = move hand (cursor write) ; tap-tap-move honest v1; drag-and-drop = v2 ambition (hold≥N enters "holding", drop on release — uses the game's own multi-move?) ; box tabs = L/R taps; party strip toggle chip | FULL | GRID | L | promo+substate (cursor/mode) | emu |
| E13 | Player PC items | leak | FAM-LIST instance (anchors researched: gPlayerPCItemPageInfo / Task_ItemPcMain) | FULL | LIST | S | sym | emu (menu only) |
| E14 | Mailbox | leak | FAM-LIST | FULL | LIST | S | sym | — |
| E15 | HoF PC replay | leak | tap=A advance | TAP | DLG | S | promo | emu (FR) · **B1:BROKEN** |
| E16 | Oak/Lanette dialog | dialog | tap=A | TAP | DLG | — | - | emu (FR) · **B1:VER-mech** |
| E17 | Move relearner | leak | FAM-LIST (move list + yes/no) | FULL | LIST | S | substate | — |
| E18 | Daycare | dialog+party | covered | TAP | DLG | — | - | — · **B1:VER-mech** |
| E19 | Battle records | leak | tap=exit | TAP | DLG | S | - | — · **B1:BROKEN** |
| E20 | Diploma | leak | tap=exit | TAP | DLG | S | - | — (FR dex 117/150 — locked) · **B1:UNREACH** |

### F. Pokédex

| # | Screen | Now | Target | Class | Fam | Eff | Sym | Ver |
|---|---|---|---|---|---|---|---|---|
| F1 | EM dex list | leak | FAM-LIST custom adapter (sPokedexView, not ListMenu — spec §5): tap row = select, drag = scroll, fling = held key | FULL | LIST | M | substate (sPokedexView 0x02039B4C sym) | emu |
| F2/F2b | EM entry + size | leak | FAM-PAGE: tap AREA/CRY/SIZE/CANCEL buttons directly (they're on screen!) | FULL | PAGE | S | substate | emu |
| F3 | EM area screen | leak | tap=exit (map is display-only) | TAP | MAP | S | substate | emu · **B1:VERIFIED** |
| F4 | EM cry screen | leak | tap the play button = A | TAP | MINI | S | substate | emu · **B1:VERIFIED** |
| F5 | EM search UI | leak | grid: tap criterion cell = cursor write; tap OK = start | FULL | GRID | M | substate | emu |
| F6 | FR dex (TOC/list/entry/area+size/habitat) | leak | TOC+list = FAM-LIST; entry = PAGE; **habitat portrait-grid pages = natural touch grid** (census flagged it touch-native candidate) | FULL | LIST+PAGE+GRID | M | rev1+substate | emu |

### G. PokeNav (EM; one cb2, sub-apps internal — sub-state read is the prerequisite)

| # | Screen | Now | Target | Class | Fam | Eff | Sym | Ver |
|---|---|---|---|---|---|---|---|---|
| G1 | Main menu (ring) | leak | tap ring item = cursor write + A; B chip | FULL | LIST | M | substate (pokenav struct ptr) | emu |
| G2 | Hoenn map | leak | = FAM-MAP (pan + tap-select landmark) | FULL | MAP | M | substate | emu |
| G3 | Condition graphs | leak | tap party slot tabs; radar is display-only | TAP | PAGE | S | substate | emu · **B1:UNREACH** |
| G4 | Match Call list | leak | FAM-LIST (tap row, tap CALL sub-option) | FULL | LIST | M | substate | emu |
| G5 | Ribbons | leak | FAM-LIST + tap ribbon cell on the summary grid | FULL | LIST | S | substate | emu |

### H. Contests & Pokéblocks (RSE)

| # | Screen | Now | Target | Class | Fam | Eff | Sym | Ver |
|---|---|---|---|---|---|---|---|---|
| H1 | Choose contest mon | party-family | = E2 choose-mode fix | FULL | GRID | — | promo | — |
| H2 | Contest lobby | dialog/multi | fmenu covers | TAP | POPUP | — | - | — · **B1:VER-mech** |
| H3 | Contest appeals | leak | tap move card = cursor write + A (4-wide picker each round); rounds between = tap-advance | FULL | GRID | M | substate | — |
| H4/H5 | Results / painting | leak | tap=A | TAP | DLG | S | - | — · **B1:BROKEN** |
| H6 | Berry Blender | leak | minigame: tap = A at the arrow (timing) — honest mapping IS just tap=A; keep | TAP | MINI | S | substate | — · **B1:BROKEN** |
| H7 | Pokéblock case | leak | FAM-LIST | FULL | LIST | S | promo+substate | emu |
| H8 | Pokéblock feed | leak | tap=A (cutscene) | TAP | DLG | S | promo | emu · **B1:UNREACH** |
| H9 | Use-Pokéblock condition | leak | tap mon portrait = cursor; tap block = feed | FULL | GRID | M | promo+substate | emu |
| H10 | TV / BuzzNav | dialog | tap=A | TAP | DLG | — | - | emu · **B1:VER-mech** |
| H11 | Link contest | — | — | DEFER (2P) | — | — | — | — |

### I. Shops & money games

| # | Screen | Now | Target | Class | Fam | Eff | Sym | Ver |
|---|---|---|---|---|---|---|---|---|
| I1 | Mart buy | leak | FAM-LIST + **qty picker: drag up/down = ±1 per notch, L/R taps = ±10** (maps to D-pad repeats) | FULL | LIST | M | promo (EM owed photo)+substate | emu (FR) |
| I2 | Mart sell | leak on FR / bag ✓ EM | = the bag (same cb2) — free once rev1 lands | DONE-via-E3 | LIST | — | rev1 | emu |
| I3 | Vendor specials | leak | FAM-LIST | FULL | LIST | S | substate | — |
| I4 | Slot machine | leak | v1: tap reel-stop buttons = A timed by the player, tap footer EXIT = B. **Blocked on the EM stuck-B finding** (injected B didn't quit — investigate first; FR B-quit works) | FULL (late) | MINI | M | promo+substate | emu |
| I5 | Roulette | leak | tap bet square on the board grid = cursor write + A | FULL (late) | GRID | M | substate | — (missed: EM game corner exit bug) |
| I6 | Coin clerk | fmenu ✓ | done | DONE | POPUP | — | - | emu |
| I7 | Lottery | dialog | tap=A | TAP | DLG | — | - | — · **B1:VER-mech** |
| I8 | Prize exchange | fmenu-family | multichoice — covered once fmenu fires there (FR trigger tile still unfound) | TAP | POPUP | S | - | emu (room) · **B1:VER-mech** |

### J. Link, wireless, mystery gift

| # | Screen | Now | Target | Class | Fam | Eff | Sym | Ver |
|---|---|---|---|---|---|---|---|---|
| J1/J2 | Club lobbies / Union Room | dialog/overworld | DLG + B1 defaults; union room walk = tap-to-walk already | TAP | DLG | — | - | 2P · **B1:UNREACH** |
| J3 | Union chat | leak | FAM-KB instance | DEFER (2P, until wireless arc resumes) | KB | — | — | — |
| J4 | Trade select (3×2) | leak | grid: tap mon = cursor write + A — **celiolink-proven venue**; high sync-risk: cursor writes must not race the link FSM → design with the wireless team | FULL (careful) | GRID | M | substate | hw (screen itself, via celiolink runs) |
| J5 | Trade anim | leak | tap=A | TAP | DLG | S | - | hw · **B1:UNREACH** |
| J6 | NPC trade | leak | tap=A | TAP | DLG | S | - | — · **B1:VER-mech** |
| J7 | Record mixing | dialog | DLG default | DEFER (2P) | — | — | — | — |
| J8 | Wireless status screen | leak | tap=exit | TAP | DLG | S | - | — · **B1:UNREACH** |
| J9–J11 | Dodrio/Jump/Crush | — | bespoke minigame mappings | DEFER (2P + bespoke study) | MINI | — | — | — |
| J12 | Mystery Gift menu | leak (shell) | FAM-LIST for the menu; content screens = event-locked | TAP | LIST | S | - | emu (shell) · **B1:VER-mech** |
| J13/J14 | Mystery Event / e-reader | leak | — | DEFER (event data unavailable) | — | — | — | — |
| J15 | "Play again?" | fmenu-family | covered | TAP | POPUP | — | - | — · **B1:VER-mech** |

### K. FRLG systems

| # | Screen | Now | Target | Class | Fam | Eff | Sym | Ver |
|---|---|---|---|---|---|---|---|---|
| K1 | Teachy TV | leak | FAM-LIST (chapter list); shows = tap=A | FULL | LIST | S | rev1+substate | emu |
| K2 | Fame Checker | leak | faces GRID (tap face = cursor+A); fact-grid PICK = same | FULL | GRID | M | rev1+substate | emu |
| K3 | Vs Seeker | overworld | works via B1 | DONE | TRAV | — | - | emu |
| K4 | Quest Log playback | leak (cb2-invisible! runs under CB2_Overworld) | suppress touch during playback — needs the quest-log state flag, else taps inject into a cutscene | TAP (guard) | DLG | S | substate (QL flag) | emu · **B1:BROKEN** |
| K5 | Help overlay (L/R) | invisible to cb2 (IRQ overlay) | optional HELP chip → inject R (user plays L=A, so L is taken!); needs the help-system flag address to detect it's open | FULL (late) | LIST | M | substate (flag) | — |
| K6 | Trainer Tower records | dialog | tap=A | TAP | DLG | — | - | — · **B1:VER-mech** |
| K7 | Story cutscenes | consumed | — | DEFER (new-game) | — | — | — | — |

### L. Endgame

| # | Screen | Now | Target | Class | Fam | Eff | Sym | Ver |
|---|---|---|---|---|---|---|---|---|
| L1 | Hall of Fame | leak | tap=A | TAP | DLG | S | - | — · **B1:BROKEN** |
| L2 | Credits | leak | tap=nothing (don't skip by accident); B chip maybe | TAP | DLG | S | - | — · **B1:BROKEN** |
| L3 | Starter choose | — | grid of 3 balls (new-game only) | DEFER (new-game) | GRID | — | — | — |

**Tally (143 catalog rows):** DONE (incl. done-after-rev1-fix) **≈ 24** · TAP-ADVANCE
**≈ 52** · FULL mapping **≈ 42** · DEFER **≈ 25** (2P: 13, event: 3, new-game/cutscene: 7,
fault/passive: 2). The 42 FULL rows collapse into **8 design families** — §2.

---

## 2. DESIGN FAMILIES — design once, instantiate everywhere

The gesture vocabulary is shared across all families (matches the shipped conventions):
**tap** = ≤12 frames, ≤6 px slop (UIHIT_DRAG_PX one-way latch) · **drag** = >6 px ·
**fling** = release velocity >3 px/frame · **hold** = finger down, level-triggered ·
app chrome (the menu chip) always wins. Every family's fallback when detection fails or
a validation gate trips: **do nothing** (return 0) — never leak walk keys; PAD mode
remains the universal manual fallback.

### FAM-KB — the keyboard (SPEC-family-keyboard.md — ready)
- **Instances:** E10 naming (player/rival/nickname/box/Walda hop) — then D8 easy-chat
  (mail E9, trends B22, quiz, profile, Walda D9) as a second matrix layout. J3 union chat
  deferred with 2P.
- **Gestures→keys:** tap char cell → write cursor sprite data[0]/[1] (col,row) + A pulse
  (the house write-then-A idiom); tap PAGE→SELECT, BACK→B, OK→START then A; hold BACK =
  repeated B (backspace repeat). Page-aware hit tables (UPPER/lower/others column x-tables
  differ — verified from pret, EM/FR identical).
- **Seam:** nothing new. **Detection:** cb2 [exact] harvested both games.
- **Why first:** worst residual offender (taps leak walk keys into typing); biggest
  touch-over-buttons delta of any screen; zero variants; both games pixel-identical.

### FAM-LIST — the vertical ListMenu (SPEC-family-lists.md — ready)
- **Instances (one generic driver + per-screen anchor table):** bag E3 (upgrade the
  shipped driver into the generic one), mart I1/I2, player-PC E13, mailbox E14, berry
  pouch E4, TM case E5, relearner E17, pyramid bag D5, Teachy TV K1, Mystery Gift J12,
  main menu A3, options A5 (rows + L/R value taps), pokéblock case H7, PokeNav lists
  G1/G4/G5 (after the substate read). EM Pokédex F1 = the one custom adapter
  (sPokedexView, not ListMenu).
- **Gestures→keys:** tap visible row → write `selectedRow` (+26) + A, **clamped to
  totalItems** (kills the "tap blank → CLOSE BAG" wart); drag → 1 UP/DOWN edge per 14 px;
  fling → held direction key ≤60 frames (the game's own JOY_REPEAT does fast scroll —
  the honest ceiling, no fake momentum); never write scrollOffset (+24) (racy).
- **Seam:** none needed. Hold-to-repeat exists (level-triggered mask).

### FAM-GRID — 2-D cursor surfaces
- **Instances:** storage boxes E12 (flagship), party E2 (shipped; fix MULTI rects +
  choose-half D7/H1), battle grids C2–C4 (shipped), factory D2/D3, fame checker K2,
  dex search F5, FR habitat F6e, contest appeals H3, use-pokéblock H9, roulette board I5,
  trade select J4 (careful: link FSM), decoration grid B17.
- **Gestures→keys:** tap cell → write the screen's cursor (row/col or slot id) + A.
  **v1 move policy = tap-tap-move (honest):** tap mon → the game's own popup (fmenu) →
  tap MOVE → tap destination. **v2 ambition = drag-and-drop** (press-hold ≥16 frames on a
  mon = synthesize pickup (A), finger-follow = cursor writes each frame, release = place
  (A)) — feasible with the seam (cursor writes are per-frame), but needs the storage
  sub-state (normal/holding) read to be safe; ships only after v1 proves the writes.
- **Box tabs:** tap left/right box arrows = L/R key. Party strip = the game's own toggle.

### FAM-PAGE — multi-page viewers
- **Instances:** summary E1/C22 (flagship), trainer card B6, dex entries F2/F6c-d, berry
  tag E6, condition G3, ribbon summary G5b, mail read E8.
- **Gestures→keys:** horizontal swipe ≥30 px = LEFT/RIGHT (the bag pocket convention);
  tap on-screen page dots/arrows where the UI draws them; tap list row inside a page
  (summary moves) = cursor write + A; tap=A advance, hold=B back out.

### FAM-MAP — spatial cursors (the fly map is the prize)
- **Instances:** fly map B8 (**tap-to-fly**), field/wall region map B7, PokeNav map G2,
  dex area F3 (display-only → tap=exit), dome bracket D4, **Frontier Pass D1** (free
  pixel cursor — the census's most touch-native screen: drive the game's own cursor to
  the finger, closed-loop, then A).
- **Gestures→keys:** tap a map square → write the map cursor (each region-map struct has
  cursor col/row) + A; for free-pixel cursors (D1) → per-frame held direction toward the
  finger until |Δ|<threshold (closed loop on the live cursor read), then A on tap.
  Drag = pan where the game supports it (zoomed region map).
- **Fly map confirm** ("Fly to PETALBURG?") is fmenu — already shipped.

### FAM-DLG — cursorless dialogs & cutscenes (the safe default)
- **Instances:** every `TAP` row above — script dialogs, TV, cutscenes, rides, HoF, PSA,
  quest log (with the playback guard), title/intro.
- **Gestures→keys:** tap = A pulse; **hold ≥30 frames = B held** (decline yes/no default,
  soft-cancel, speed-up); double-tap = START only where a menu exists (overworld rule).
  Nothing else. This is what GCTX_FULLUI ships as the default for EVERY detected screen
  before its family mapping lands — the residual killer.
- **Status 2026-08-14 (phase 24, lane B1).** SHIPPED and live-proven, plus one correction the
  family could not have worked without. FAM-DLG originally ran only for `GCTX_TITLE` /
  `GCTX_FULLUI`, i.e. only for screens with their own cb2 — so the ~19 `TAP` rows that are FIELD
  DIALOGS (script text, TV, NPC trade, cutscenes, rides) never reached it at all: they run under
  `CB2_Overworld`, landed in `walk_update`, and a tap there planned a BFS route while a hold
  steered the D-pad, both at a game frozen in a textbox. Witnessed live (`curKeys = 0x10` held
  for 20+ emulated frames, `planSeq` +3 per tap, avatar never moved). The fix routes an overworld
  frame to FAM-DLG whenever **a script owns the field**, and the signal for that is
  `sLockFieldControls`, NOT `sFieldMessageBoxMode` — the latter was measured for the first time
  this session and turns out to be a *text-is-printing* flag that reads 0 for the whole time a
  box waits for A. Details, evidence and the per-row verdicts: `LANE-B-TAPVERIFY.md`.

### FAM-POPUP — sMenu windows (shipped, extend by detection only)
- **Instances:** start menu, yes/no, multichoice, party popup, bag sub-menus E3b (the
  census found GCTX_BAG drops out there — a detection-ordering fix, the handler already
  exists), PC menus, coin/prize clerks, "play again?".
- Already reads the live sMenu window geometry — new instances are free once their
  screens detect.

### FAM-MINI — minigames (bespoke or honest-tap)
- Slots I4 (tap stop buttons; **EM B-quit bug first**), roulette I5 (grid bets), blender
  H6 (tap=timed A — already honest), fishing B11 (tap=A, works), dex cry F4 (tap=play).
  Wireless minigames J9–J11 deferred with 2P. Each is bespoke; none blocks the families.

### FAM-TRAV — overworld traversal (SPEC-family-traversal.md — SHIPPED, host-proven)
- HM-aware edges (surf/cut/smash/strength/waterfall/dive) with badge+party eligibility
  reads, closed-loop INTERACT scripts (never answer an unpredicted YES), and cross-map
  warp excursions (the Lavaridge case). Own toggle, off by default. Pure-C `fieldtrav`
  layer, host-tested like fieldpath.
- **Status 2026-08-14 (phase 22.2 + 23).** SLICE 1 (Surf / Cut / Rock Smash on the current
  map) and SLICE 2 (the cross-map excursion planner + the three-leg executor) are both
  IMPLEMENTED and green: `test_fieldtrav` 1066 checks, fieldpath UNMODIFIED at 1808, and
  TEST 15/16 grade the slice-2 pointer chain and the excursion against the **user's own
  `roms/emerald.gba`** (loud SKIP if the ROM is absent) rather than a fixture.
- **Emulator proof status: NOT YET LANDED, and the reason is the SAVE, not the code.** The
  fixture Emerald save stands in `BattleFrontier_BattleArenaLobby` with all 8 badges but
  **only SURF** among the HMs and **no FLY**, so the mainland — and with it every spot
  SPEC T6 names (Route 103, Route 116, Rusturf, Lavaridge) — is unreachable. The nearest
  water is 28 tiles away on `BattleFrontier_OutsideEast`'s south beach, and the walk there
  is broken by **step-counted field scripts at ~6 and ~36 outdoor steps** (OVERNIGHT2
  Entry 10) that time out a D4 walk token and abort the script.
- **The unlock, for whoever picks this up:** walk the approach ONCE and then **SAVE
  in-game**, so every later arc resumes standing where the proof needs it. That single
  save turns P1/P2/P3 and the slice-2 excursion proof from a 10-minute gamble each into a
  short, repeatable arc — and it is also what a Cut/Rock-Smash proof needs, since those
  first require teaching an HM through the (already live-proven) bag + party touch chain.

---

## 3. PRIORITIZED SLICES (phase 22+)

Ordering rationale: kill the residual first (it poisons every screen), revive FR (half
the product is dead on the user's cart), then ship the four biggest QoL wins the user's
prior names (keyboard, PC boxes, bag/mart lists, tap-to-fly), then breadth. Every slice
ends with the house proof: emulator captures + GDB state reads (cursor before/after, the
touch logger's own proof channel) banked to evidence/, hardware sign-off deferred to the
next hw session's checklist.

| Slice | Content | Why now | Proof plan |
|---|---|---|---|
| **22.0 Residual kill + FR revival** (S) | Merge `phase21-cb2-promotion` (GCTX_TITLE + GCTX_FULLUI + FR rev1 battle anchor); extend with the FR rev1 task anchors (party/bag/fmenu/start — drift table is exact) + LG row fix (RS-REV2 §6, cited, sym-derived flag) + the newKeys typo note. FULLUI default = FAM-DLG (tap=A, hold=B, **zero walk keys**). | Turns ~40 photographed screens from hostile to usable in one merge; un-deads ALL FR touch; unblocks every later slice. | Suites green; emulator: boot EM+FR, visit dex/summary/naming/storage/bag-submenu + FR battle/party/bag, gdbio `s_lastCtx` shows the new contexts (not field), touch log shows inj=A-only on FULLUI screens. Photograph each. |
| **22.1 KEYBOARD** (M) | FAM-KB on E10 (both games). | Biggest touch-over-buttons delta; spec ready; self-contained. | Spec P1–P7: type "GUYA" via taps, gdb-read the text buffer + cursor sprite before/after each tap; photograph; empty-confirm no-op regression. |
| **22.2 LISTS** (M) | The generic ListMenu driver: bag upgrade (clamp + pocket-icon taps) + mart buy/sell + player-PC + berry pouch/TM case free rides; EM dex adapter. | Highest-frequency screens in normal play; spec ready; one driver, many screens. | Spec P-A…P-F: per screen — tap row N, read selectedRow==N; drag 3 rows, read scrollOffset moved by the GAME; blank-tap = no-op read; buy 1 Potion end-to-end. |
| **22.3 PC BOXES** (L) | FAM-GRID on E12: tap-tap-move v1 + box tabs + party strip; MULTI/choose-half party rect fix rides along (D7/H1). | The user's named QoL win; storage sub-states all photographed; popup handler already exists. | Move a mon box→box via taps only; gdb-read cursor + mode at each step; withdraw/deposit flows; photograph the 6 sub-states under touch. |
| **22.4 TAP-TO-FLY + maps** (M) | FAM-MAP on B8/B7 (+G2 once pokenav substate lands): tap city → cursor write → A → fmenu confirm. | "Tap the map, go there" is the definitive touch demo; small after lists. | Tap Littleroot on the fly map, read cursor mapsec before/after, confirm warp via SaveBlock1.location change (the walker's own kill-switch read). |
| **22.5 PAGES + summary** (M) | FAM-PAGE on E1/C22/B6/F2/E6: swipes + page dots + move-row taps. | Completes the everyday loop (party→summary is constant); C22 makes HM-teaching touch-clean. | Swipe through 4 EM pages reading the page index; forget-move by tap; card flip. |
| **22.6 TRAVERSAL** (L) | FAM-TRAV slices per its spec (surf edge → cut/smash → strength → Lavaridge warp excursion). | The user's explicitly requested feature; spec + proof scenarios (Route 103 pond, Route 116 tree, Lavaridge) ready. | Host-suite the fieldtrav tables off ROM fixtures; emulator: tap-across-pond auto-surfs (read avatar flags), Lavaridge tap routes through the Center. |
| **22.7 EM handhelds** (M) | PokeNav substate capture → G1–G5 lists/map; Frontier Pass free-cursor D1; dex search F5. | EM-exclusive polish; needs the one substate harvest the census already staged (taskFp rings banked). | gdb-capture the pokenav state var across sub-app hops; then per-family proofs. |
| **22.8 Minigames & stragglers** (M) | Slots (after the EM B-quit investigation), roulette board, fame checker grid, Teachy TV list, help-overlay chip (R), quest-log guard flag. | Long tail; each small; none blocking. | Per-screen one-proof-each. |
| **RS/LG lane** (parallel, gated) | First RS boot on the rev2-verified rows (paper-green, §RS-REV2); harvest RS cb2s live (RS = walk-only by design today); LG smoke on the fixed row. | Covers the remaining 3 games; RS is DIFFERENT builds — EM values must never be copied. | The census method verbatim: boot, gdb cb2 harvest, resolve on rev2 syms, photograph, promote. |

Hardware note: everything above is emulator-provable except feel (drag thresholds, fling
tuning, TERM_FRAMES at degraded wireless fps) — those keep their VERIFY-ON-HW flags and
join the next hardware session checklist (with the still-pending phase-18 door-terminal
sign-off).

---

## 4. OPEN QUESTIONS FOR THE USER

1. **Save-progression-locked screens** — confirm we skip these until a scratch save exists:
   - *New-game one-shots:* A4 gender/naming intro (FR captured by accident), L3 starter
     pick, C13 catch tutorials, B9-set wall clock, K7/D13 story scenes. Unlock = a scratch
     new-game .sav on a fixture copy (an evening of play or a save-editor bootstrap).
   - *Progress-locked:* E20 diploma on FR (dex 117/150 owned), J13 Mystery Event on EM
     (needs the Petalburg questionnaire phrase — cheap: FAM-KB could type it!), C12
     recorded battle (needs a recorded battle), EM I5/I8 photos (Game Corner re-visit).
2. **2P-only screens** (union room/chat, wireless minigames, record mixing, link contests,
   trade/battle screens beyond what celiolink already proved): keep DEFERRED until the
   wireless arc resumes, or should the trade-select grid (J4) get touch earlier since the
   venue already works? (Risk note: cursor writes during a live link need the wireless
   team's review.)
3. **PC boxes taste call:** ship tap-tap-move v1 first (guaranteed-honest), with
   drag-and-drop as v2 — or hold the slice until drag-and-drop works? (Plan assumes v1
   first.)
4. **Page-turn taste call:** swipe (bag-style), tap-arrows, or both? (Plan assumes both.)
5. **Dialog hold=B:** is hold-to-B on dialogs/cutscenes wanted, or tap=A only? (B can
   decline yes/no defaults and cancel evolutions — powerful but occasionally surprising.)
6. **Slots policy:** EM injected-B didn't exit the slot machine (census bug). Worth a
   phase-22 investigation before mapping touch there, or park slots entirely?
7. **FR help overlay:** you play BUTTON MODE L=A, so the FRLG help system is only on R.
   Want a HELP chip on FR screens (needs the help-flag address), or skip?
8. **Keyboard scope:** naming screen first is fixed; is easy-chat (mail compose, trends,
   quiz, Walda) wanted in the same phase or later? (Plan: later, same family.)
9. **RS/LG lane timing:** run it parallel to the EM/FR slices (needs a second emulator
   instance — the S3 dual-instance harness upgrade), or after 22.5?
10. **Credits:** should taps be dead during credits (no accidental skip), or tap=skip?

---

## 5. Cross-references

- Census ground truth: `CATALOG.md` (143 rows) · `VISITED-emerald.md` / `VISITED-firered.md`
  (61 rows live-certain) · `CB2-HARVEST.md` (~60 [exact] fingerprints, zero guesses).
- Audit: `COVERAGE.md` (GCTX matrix §1, profile inventory §2, known-broken §3, promotion
  pipeline §4, seam §5).
- Family specs (ready for phase 22): `SPEC-family-keyboard.md`, `SPEC-family-lists.md`,
  `SPEC-family-traversal.md`.
- RS/LG gate: `RS-REV2-VERIFICATION.md` (RS rows rev2-green on paper; LG fix table §6).
- Visual census: `evidence/sheets/` (per-family contact sheets), `evidence/emerald/`,
  `evidence/firered/`, plus the published gallery artifact.
