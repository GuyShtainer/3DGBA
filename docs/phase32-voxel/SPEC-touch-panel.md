# Phase 32 — SPEC-touch-panel: the single-game bottom screen, Zallax-style

Track T of `PHASE.md`. Guy: *"fix the touch controls. It's not so good now but the demo of this
repo shows it working nicely."* The demo is Zallax's bottom screen (13 screenshots, archived study
copy in the session scratchpad): a permanent right column of START-menu buttons, the opened menu
shown left of it, and a dedicated battle command panel. Their implementation patches game code;
ours drives the unmodified game with key injection and the existing smart-touch drivers. **Ideas
only, our own code** (legal condition 5).

## T0. Where it applies

- **Single-game mode** (`single == true`, main.c:3129) with a mapped Gen-3 Pokémon profile
  (`GameProfile` found: BPEE, BPRE, BPGE; RS fall back). Dual-game mode is unchanged.
- New `TouchMode` value **`TOUCH_PANEL = 3`**, name "Panel". In single-game mode the touch row
  offers Off / Gamepad / Panel (today SMART is silently substituted by PAD, main.c:3371 — PANEL
  replaces that substitution). **Default for single-game becomes Panel** for mapped profiles once
  the emulator proof (T7) passes; unmapped games keep Gamepad.
- Physical buttons always keep working (the injection seam is additive, COVERAGE §5).

## T1. Layout (bottom screen 320×240)

```
x: 0                    240 244          316
   ┌──────────────────────┐ ┌────────────┐
   │                      │ │ MAP        │  8 buttons, 72×28, pitch 30, y0 = 0
   │   GAME VIEW 240×240  │ │ POKéMON    │  hit-test = nearest row inside the column
   │   (game frame 1:1 at │ │ BAG        │  (no dead gaps: every touch in x≥240 maps
   │    y=40, 240×160)    │ │ TRAINER    │   to a row), so a 14 px fingertip error
   │                      │ │ POKéDEX    │   still lands on the intended row
   │                      │ │ POKéNAV    │
   │                      │ │ SAVE       │
   │                      │ │ OPTION     │
   └──────────────────────┘ └────────────┘
```

- Column: own art via `ui_panel`/`ui_text` in the active theme (no franchise trade dress — legal
  condition 10; plain labels, "POKéMON" spelled as the game spells it is nominative use).
  A button whose action is absent from the live START list is drawn **dimmed** and inert
  (Pokédex/PokéNav before you get them). MAP is hidden on FR/LG (no START route to a map; the
  Town Map is a bag item) and the column re-flows to 7 rows of pitch 34.
- Game view, by context:
  - **field / any full-screen UI / field menus**: the live game frame 1:1 at (0,40); the 40-px
    bands above and below carry the frame's edge colour (cheap stand-in for Zallax's carried-out
    art). Touch maps `gx = sx, gy = sy − 40` and goes to the existing **SMART** families
    (`touch_update(TOUCH_SMART, …)` with a `TouchSmart` filled from the single game, i.e. emuA —
    today it is filled from the bottom-screen game only).
  - **battle action / move**: the battle command panel (T3) instead of the frame.
  - **dialogue anywhere** (`textDlg || fieldLock` with no menu): the frame is shown; a tap is A
    (already FAM-DLG's behaviour).
- **Top screen while a full-screen menu is open** (`GCTX_PARTY, BAG, FULLUI, LIST, STORAGE, MAP,
  POKENAV, NAMING` outside battle): holds the **last field frame** (or the last voxel frame when
  track V is on), dimmed 15%, with the live menu only on the bottom — Zallax's model. Setting
  `panelHoldTop` (default on). In battle the top stays live. **As built:** the hold frame is captured
  only after 30 stable field frames — a START selection fades to black while the game still reads
  as the field, and capturing every field frame held the black fade (emulator run, 2026-10-05).
  The setting itself is not built; the hold is always on in Panel mode. Screens the game-state
  reader does not classify as full-screen UI (the Battle Frontier pass) keep a live top.

## T2. The column — opening a START entry, closed-loop

Action ids (pret `src/start_menu.c`):

| Button | EM `MENU_ACTION_*` | FR/LG `STARTMENU_*` |
|---|---|---|
| POKéDEX | 0 | 0 |
| POKéMON | 1 | 1 |
| BAG | 2 | 2 |
| POKéNAV | 3 | — (hidden) |
| TRAINER | 4 (PLAYER) or 9 (PLAYER_LINK) | 3 or 8 |
| SAVE | 5 | 4 |
| OPTION | 6 | 5 |
| MAP | POKéNAV, then the HOENN MAP row (main-menu cursor 0) | — (hidden) |

Live list (IWRAM-free, EWRAM, same on FR rev0/rev1 per both sym files):

| Game | count (u8) | actions (u8[9]) |
|---|---|---|
| BPEE | `sNumStartMenuActions` 0x0203760F | `sCurrentStartMenuActions` 0x02037610 |
| BPRE/BPGE | `sNumStartMenuItems` 0x020370F5 | `sStartMenuOrder` 0x020370F6 |

Profile fields to add: `startCount`, `startActions` (BPGE: verify against `pokeleafgreen.sym`
before enabling; until verified, LG gets the column with every button live and no dimming).

Sequencer (pure C, `source/panel.c`, a small state machine stepped once per frame with the
current `GameState`; emits a GBA key mask; no RAM writes beyond what smart touch already does):

1. `ctx == OVERWORLD`, `!fieldLock`, START menu not up → press **START** (one frame), wait.
2. START menu up (`startCb == startCbInput`, the existing detector) → read count + actions; find
   the index `i` of the wanted action. Absent → abort (button was dimmed anyway). **As built:** write
   the cursor (`sMenu.cursorPos` + the START mirror, exactly smart touch's shipped `fmenu_select`
   writes) on tick 0, then write again and press **A** on tick 1. (The D-pad stepping first drafted
   here was replaced: the cursor write is the hardware-proven idiom and needs no per-step waits.)
3. MAP only: wait for `ctx == POKENAV` with `pnMenuIdx == 0` and `pnCursor` valid; move to cursor
   0; **A**.
4. From a **full-screen menu or the START menu showing another entry**: press **B** (one per 8
   frames) until `ctx == OVERWORLD`, max 6 presses, then step 1. A different button pressed while
   a sequence runs replaces it.
5. Every wait has a frame timeout (60 frames); on timeout the sequence aborts silently and logs to
   the existing touch log (`touch_log_*`).
6. A physical key edge cancels the sequence (the smart-touch yield rule, SPEC H0.2).
7. Never starts in battle, during a link, or while `fieldLock` (a script owns the field).

## T3. The battle panel

Shown when `ctx == GCTX_BATTLE_ACTION` or `GCTX_BATTLE_MOVE` and the player's controller is
waiting for input (the same predicate smart touch uses for those families).

- **Header strip** (y 0–56): player's active mon and the foe — nickname, level, HP bar and
  `hp/maxHP` for the player, HP bar only for the foe (what the game itself shows). Source:
  `gBattleMons` (BPEE 0x02024084, BPRE 0x02023BE4; stride 0x58; `+0x28 hp`, `+0x2A level`,
  `+0x2C maxHP`, `+0x30 nickname[11]` decoded with `gbatext_decode`). Battler 0 = player,
  battler 1 = foe in singles; doubles show battlers 0 and 2 (player side) and only the first
  live foe.
- **Action view** (`BATTLE_ACTION`): FIGHT as a wide button (y 64–136) showing the active mon's
  move types as small chips; BAG · POKéMON · RUN as three buttons (y 144–232). Tap → the
  existing smart-touch battle-action driver for slot 0/1/2/3 (FIGHT=0, BAG=1, POKéMON=2, RUN=3 —
  the game's own `gActionSelectionCursor` order).
- **Move view** (`BATTLE_MOVE`): 2×2 move buttons (each ~150×64): move name, type chip, `PP x/y`.
  Empty slots (move id 0) are not drawn. A **BACK** chip = B. Tap → the existing move driver
  (slot 0–3). Moves with PP 0 are drawn dimmed but still tappable (the game prints its own
  "no PP" message — we do not second-guess it).
- Data: move ids `gBattleMons[b].moves[4]` (+0x0C), PP `+0x24`; max PP = `gBattleMoves[id].pp`
  (+4, stride 12) adjusted by `ppBonuses` (+0x3B, 2 bits per move: `base + base*bonus/5`);
  type `gBattleMoves[id].type` (+2); name `gMoveNames[id]` (13 B, ROM); type name
  `gTypeNames[t]` (7 B, ROM).

  | ROM table | BPEE | BPRE rev0 | BPRE rev1 |
  |---|---|---|---|
  | gMoveNames | 0x0831977C | 0x08247094 | 0x08247104 |
  | gBattleMoves | 0x0831C898 | 0x08250C04 | 0x08250C74 |
  | gTypeNames | 0x0831AE38 | 0x0824F1A0 | 0x0824F210 |

  Revision from ROM header byte 0xBC (the existing rev-alternate profile mechanism). BPGE: verify
  against `pokeleafgreen.sym`/`_rev1` first; until verified LG shows names as "MOVE 1..4" with no
  type chip (never a wrong name).
- Type chip colours: our own palette per type id 0–17 (a table of 18 colours we choose).
- **How a panel tap reaches the game:** the smart battle drivers hit-test GBA coordinates
  (`hit_action`/`hit_move`, touch.c:190-197). The panel does not duplicate them: an accepted
  panel tap is replayed into `touch_update(TOUCH_SMART, …)` as a **synthetic stylus contact** at
  the centre of that cell in GBA space (touching for `TOUCH_SETTLE + 3` frames, then released),
  so the cursor driving, `select_pulse` timing and A press stay exactly the proven code. The cell
  centres come from the same geometry those two functions use (export them from touch.c as
  `touch_battle_cell_centre(kind, slot, &gx, &gy)`).
- Every other battle screen (target select, party, bag in battle, messages) shows the game view
  with the smart families, as today.

## T4. Drawing and per-frame reads

- Reads happen in the parked window with the other gamestate reads; the panel keeps a
  `PanelSnap` (start list, battle mons ×4, move rows ×4 with decoded names) — names are decoded
  only when a move id changes (cache by id).
- Drawing: one function `panel_draw(const PanelSnap*, const PanelUi*, C2D_TextBuf)` in main.c's
  bottom-screen block, after the game-view frame quad. Pressed-state feedback: a button shows a
  pressed fill for 6 frames after its tap is accepted.

## T5. Touch-handling rules carried from hardware (learn: touch-ui-on-real-hardware.md)

- Hit-test after `TOUCH_SETTLE` (2 frames), never on the press edge.
- Tap-vs-drag threshold 12 px (finger), not 6.
- Panel buttons act on **release** inside the same button (lets a finger slide off to cancel).
- Smallest panel target: 72×28 column buttons (adjacent, no gaps); battle buttons ≥ 64 px tall.

## T6. Module boundaries

- `source/panel.{c,h}` — pure C (CLAUDE.md rule 4): layout geometry, hit-test, the T2 sequencer,
  the battle view model (from a read-only bus callback struct, the `PsprBus` shape), PP math.
- `test/host/test_panel.c` — geometry (every pixel of the column maps to exactly one row; MAP
  hidden re-flow), sequencer traces for: field→BAG, START-open→OPTION, PARTY→SAVE (B-out), dimmed
  POKéNAV, timeout abort, physical-key cancel; battle view from a synthetic gBattleMons (names,
  PP bonus math, empty slots).
- main.c: mode plumbing, reads into `PanelSnap`, the draw call, the top-hold, routing game-view
  touches into SMART with emuA's `TouchSmart`.
- No edits to `celiolink.c`, `netlink.c`, `wireless.c`.

## T7. Proof

- Host suite green with the new checks.
- Emulator (tools/emutest): screenshots of (a) field with column, (b) BAG opened from the column
  with the top holding the field, (c) battle action panel, (d) move panel with real names/PP.
  Taps driven by the harness's touch injection.
- Hardware checklist (HANDOFF): thumb accuracy on the column, every button's sequence on EM and
  FR, battle panel names on FR rev1.
