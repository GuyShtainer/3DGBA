# Phase 21 — smart-touch coverage TODAY (read-only audit)

_What the shipped smart-touch system does right now, exactly, with file:line evidence._
_Sources: `source/touch.{c,h}`, `source/gamestate.{c,h}`, `source/fieldpath.{c,h}`, `source/main.c`,
`docs/kb/gen3-ram-touch.md`, `docs/kb/gen3-touch-features-spec.md`, `docs/kb/gen3-menu-bag-touch-spec.md`,
`docs/kb/touch-issues-todo.md`, `docs/phase18-crisp/SPEC-door.md` (T5 audit) + `RESULTS.md`, git log._
_No source was changed to produce this document._

The three touch modes (`touch.h:16`): **OFF** (any tap opens the app pause menu), **PAD** (virtual
gamepad, fixed screen zones, `touch.c:75-91` — the universal fallback), **SMART** (the pointer on the
real game UI — this document's subject). In SMART, the "menu" chip rect (`UIHIT_MCHIP_*`,
`touch.c:32-45`) is reserved app chrome: a tap there opens the pause menu (`main.c:3423-3426`) and is
never forwarded to the game. SMART is unavailable in single-game mode — `tmEff` silently substitutes
PAD (`main.c:3306`) because there is no bottom-screen game to point at.

---

## 1. The GCTX_* → behaviour matrix

Context detection runs every gameplay frame: `main.c:3493-3513` calls `game_read` on the **bottom**
core, fills `TouchSmart sm`, then `touch_update` (`main.c:3514`) dispatches on `sm.ctx`
(`touch.c:777-850`). The core select pattern everywhere except overworld/bag is **"write the game's
own cursor to RAM, then pulse A on the next tick"** (`select_pulse`, `touch.c:164-170`) — tick 0
writes only, tick 1 writes + returns A, then clears. Deterministic; no closed-loop D-pad walking.

| GCTX (gamestate.h:10-20) | Detected by | What a tap does | Verification status |
|---|---|---|---|
| **GCTX_OVERWORLD** | fall-through: `!inBattle` and no menu task matched (`gamestate.c:341-354`). **`ctxResolved=false` here — this is where every undetected screen hides.** | See the full overworld behaviour table below (§1a). | **Hardware-verified** for steer + tap-route (user, phase-17/21: "our overworld walking is good") — note the two SELF gestures it was verified with (tap = A, double-tap = START) were REPLACED in phase 24 lane A2 by tap = START / hold = SELECT, which are emulator-proven and VERIFY-ON-HW-PENDING. The phase-18 **warp routing (door/arrow/stair/step) is host-suite-proven only** (1,808 checks off the user's real ROM map data) and "has never been watched working in an actual overworld" (`phase18-crisp/RESULTS.md:16`); `TERM_FRAMES=30` is VERIFY-ON-HW-PENDING (`touch.c:246-250`). |
| **GCTX_BATTLE_ACTION** | `cb2 == battleMainCb` AND `gBattle_BG0_Y == 160` (`gamestate.c:320,369-372`) | Tap one of the 2×2 FIGHT/BAG/POKEMON/RUN cells (`hit_action`, `touch.c:173-176`, rect x136-231 y120-151) → `menu_select` writes `gActionSelectionCursor` for BOTH player slots 0 and 2 (`touch.c:181-188`) + A pulse. | **Hardware-verified** (user: "battles is fine"; post-battle A-spam fix `fc0777b` was hardware-driven). |
| **GCTX_BATTLE_MOVE** | same, `BG0_Y == 320` (`gamestate.c:373-376`) | Tap a move cell (`hit_move`, `touch.c:177-180`, x16-151 y120-151) → same double-slot write + A. Empty move slots are read (`moveValid[]`, `gamestate.c:376`) but **hit_move does NOT check them** — the write+A goes through regardless (the game itself rejects empty slots). | **Hardware-verified** (same sessions). |
| **GCTX_BATTLE_TARGET** | any `gBattlerControllerFuncs[b] == HandleInputChooseTarget` (`gamestate.c:356-366`) | Tap a battler's screen rect (`hit_battler`, `touch.c:212-218`, four fixed ±28 px rects) → position→index via `gBattlerPositions` scan skipping absent battlers (`touch.c:219-223`) → write `gMultiUsePlayerCursor` + A (`touch.c:805-812`). | **Shipped, no recorded hardware sign-off** (no double-battle hw test on record; rects are a "tune on hardware" heuristic, `gen3-touch-features-spec.md` §3.3/Risk 4). |
| **GCTX_PARTY** | `Task_HandleChooseMonInput` active (`task_active`, `gamestate.c:332-339`) — field OR battle | Tap a slot (`hit_party`, `touch.c:191-208`: SINGLE and DOUBLE rect tables + Cancel x192-239 y136-151) → write `gPartyMenu.slotId` (+0x09) + A (`touch.c:813-820`). Guard: slot must be `< partyCount` or ==7/Cancel. | **Shipped, no hardware sign-off since the task-based rework** (the 2026-06-10 hw run predates it). Cosmetic: the highlight sprite does not animate to the written slot. |
| **GCTX_FIELDMENU** | `Task_HandleSelectionMenuInput` OR `Task_HandleYesNoInput` OR `Task_HandleMultichoiceInput` active (`gamestate.c:329-331`, tested BEFORE party → the popup-over-party fix for issue #5), or `Task_StartMenu*` active in the field (`gamestate.c:345`) | Tap a row: `hit_fieldmenu` reads the LIVE `sMenu` window (windowId→`gWindows` rect, top, pitch, maxCursorPos — `touch.c:534-546`) so it follows whatever menu is up → write `sMenu.cursorPos`, plus the `sStartMenuCursorPos` mirror iff `gMenuCallback == HandleStartMenuInput` (`touch.c:547-555`) + A. Covers START menu, YES/NO, script multichoice, and the party SUMMARY/SWITCH/ITEM/CANCEL popup. | **Shipped, no explicit hardware sign-off recorded** for the popup fix (`a661b80`) or START-by-task fix (`10d768f`); the START fix cured the "Emerald walk stuck on A" hw bug, so it is at least indirectly hw-exercised. |
| **GCTX_BAG** | live `Task_BagMenu_HandleInput` in gTasks → ListMenu task base resolved (`find_bag_list_task`, `gamestate.c:253-263`; tested FIRST, before all other menus — field or battle ITEM) | Phone-style (`bag_update`, `touch.c:575-598`): clean TAP on a visible row (`hit_bag`, `touch.c:565-571`: EM 8 rows from y16 / FRLG 6 rows from y8, x-band 112..231 / 88..231, pitch 16) → write the live ListMenu row (u16 at taskBase+26) + A. Vertical DRAG → one UP/DOWN key per 14 px (the game scrolls itself; scroll field +24 never written). Horizontal SWIPE ≥30 px → LEFT/RIGHT = pocket switch. Bottom row = CLOSE BAG. | **Shipped, no recorded hardware sign-off** for the gesture set. |
| **GCTX_BATTLE_OTHER** | in battle, none of the above (`gamestate.c:377-378`) | Tap anywhere = **held A while touching** (`touch.c:835-838`) — advance dialog/animations. | Hardware-exercised implicitly in every battle. |
| **GCTX_NONE / sm invalid** | no profile (non-Pokémon ROM) or `game_read` failed | `all_reset()`, nothing injected (`touch.c:785, 839-842`). | By construction. |

### 1a. GCTX_OVERWORLD in full (`walk_update_inner`, `touch.c:395-512`)

| Gesture | Behaviour |
|---|---|
| **HOLD / slide** | Steer toward the touch: dominant axis of (touch tile − player anchor (7,5)) → that D-pad key held every frame (`touch.c:416-422`). Works at any speed incl. bike (the fix for issue #6). Cancels any active route. |
| **Quick tap** (≤ `TAP_FRAMES` 12, moved ≤8 px) on another tile | Plan a route via `fieldpath_plan` (pure C, `fieldpath.c`): BFS over the live `gBackupMapLayout` grid in a ±32-tile window, collision + **elevation** walkability (pret's rule), NPC tiles blocked (`read_npcs`, `touch.c:328-338`), and warp-aware terminals (`fieldpath.h:37-42`): **DOOR** → walk to the tile SOUTH, then HOLD UP; **DIR** (arrow mat / FRLG stair) → stand ON it, HOLD its direction; **STEP** (ladder/escalator/pad) → arriving fires it; **NONE** → legacy blocked-goal-allowed terminal, stall → A pulse (sign/NPC interact). Terminal hold = sustained key ≤ `TERM_FRAMES` 30 (`touch.c:465-477`), silent timeout (never A at a door). Route dies the instant `SaveBlock1.location` changes = the warp kill-switch (`touch.c:458-463`). Mid-route stall (24 frames) → re-read NPCs + replan ≤ `REPLAN_MAX` 2, suppressed at a WK_NONE terminal (`fieldpath_should_replan`, `fieldpath.h:155-157`). Tap refused if the map changed or the player jumped >1 tile between press and release (`touch.c:433-434` — the T5.12 staleness guard). |
| **Tap own tile** | **START pulse**, fired on RELEASE (`touch.c` decision D1 / `owngest_step`). PHASE 24 lane A2: this REPLACED the A pulse. |
| **Hold own tile** (>= `OWNGEOM_HOLD_FRAMES` 30, unmoved) | **SELECT pulse**, fired the moment the threshold is crossed, while still held; the release that ends it fires nothing. PHASE 24 lane A2: this REPLACED the double-tap START. `A` is not lost — a tap on the THING you want still routes to it and interacts, and every dialog is a FAM-DLG tap-advance screen (DECISIONS-overworld-gestures.md D1). |
| **A routed leg of >= `RUNGEOM_MIN_TILES` 4 tiles** | The follower also holds **B** (run), per LEG, gated on the engine's own five-way conjunction — Running Shoes, `gMapHeader.allowRunning`, on foot, not a forced move, and the tile under the player re-read every frame. Any gate closed = the leg walks, silently. PHASE 24 lane A2, decision D2 (`rungeom_decide` / `rungeom_eligible`). |
| **No map loaded** (`px < 0`: title / intro / main menu) | tap = A, double-tap = START, no walking (`touch.c:406-414`). |

**The undetected-screen residual (critical for this phase):** any full-screen UI whose task/cb2 is
not fingerprinted — Pokédex, Town Map, summary, trainer card, naming keyboard, PokéNav, PC/storage,
options, save/load, shops, Mystery Gift, contests, Pokéblocks, game corner, mail, move-learn,
evolution, credits, Frontier screens — reads as **GCTX_OVERWORLD with `ctxResolved=false`**
(`gamestate.c:346-347`). Since `SaveBlock1` is still valid there, `px >= 0`, so **the full walk
machinery runs against the invisible overworld map underneath**: holds inject steering D-pad keys
into the menu (semi-random list scrolling), taps can arm a BFS route that injects walk keys, and the
two own-tile gestures (phase 24 lane A2: tap = START, hold = SELECT) fire there too. This is the known residual the same fall-through also inflicts on
tilt/presence gating. Only when the save block is not yet loaded (title screen) does the safe
tap=A/START path apply.

---

## 2. Detection inventory per profile (`PROFILES[]`, `gamestate.c:12-231`)

Fingerprints actually consumed at runtime (everything else in the profile is dormant or logging-only):

- **Task-based** (via `task_active` `gamestate.c:242-249` / `find_bag_list_task` `:253-263`):
  `bagHandler`, `selMenuTask`, `yesNoTask`, `multiTask`, `partyTask`, `startMenuTask`, and
  `mapNameTask` (text-banner flag only).
- **cb2-based**: `battleMainCb` (the in-battle gate, `gamestate.c:320`). `mainCb2` itself is read
  for the gate and for the cb2 **logging** fingerprint (`gamestate.c:289`).
- **Controller-func**: `chooseTarget` vs `gBattlerControllerFuncs[]` (`gamestate.c:358-360`).
- **Value-based**: `bg0y == 160/320` (battle action vs move).
- **Dormant profile fields** (present, never consumed by detection): `cb2UpdParty`, `cb2InitParty`,
  `cb2BagRun`, `bagOpen`, `newKeys` (verified by grep this session — `startCbInput` is used only as
  the START-mirror gate inside `fmenu_select`, `touch.c:550`).

| Profile | Detection capability | Caveats |
|---|---|---|
| **BPEE** (Emerald) | Everything: all 8 contexts reachable. | The reference profile; every touch address VERIFIED-SYM (`gen3-ram-touch.md`, symbols branch). |
| **BPRE** (FireRed) | Everything. | User's cart is **rev1**; the core data symbols matched rev0 (`gamestate.c:57-59`). FRLG bag uses its own x/y bands + 6 rows (`touch.c:565-571`). |
| **BPGE** (LeafGreen) | Same row as BPRE (`gamestate.c:85-113`). | The touch-era ROM addresses (task handlers, chooseTarget, battle addrs) are **FireRed-derived and LeafGreen-UNVERIFIED** (`gen3-touch-features-spec.md` Risk 1; house rule noted at `gamestate.c:94`). Only the later-phase fields (presence sb2ptr/hbCtr, `mapHeaderPath`, peer-sprite trio) were read from LeafGreen's OWN sym maps. No LG ROM on this machine — never executed. |
| **AXVE/AXPE** (Ruby/Sapphire) | **Overworld only, plus (on paper) battle action/move/other.** All menu/party/bag/target detection fields are 0 → `task_active` returns false → RS reports GCTX_OVERWORLD wherever Emerald would report FIELDMENU/PARTY/BAG, and GCTX_BATTLE_TARGET is unreachable (`chooseTarget=0`) (`gamestate.c:142-160`). Tap-to-walk INCLUDING phase-18 warp classification is fully wired (`mapLayout`, `mapObjects`, `mapHeaderPath`, `sbDirect=1` all present). Battle detection hangs on the ONE shipped ROM address `battleMainCb 0x0800F808` (identical in all four RS maps) — without it executing correctly, RS battles read as overworld. | **VERIFIED-SYM / VERIFY-ON-HW: not one address ever executed** (no RS ROM on this machine, `gamestate.c:179-183`). The named degradations are fail-safe (the overworld still walks). |

**Screens with NO fingerprint in ANY profile today** (all fall through to GCTX_OVERWORLD,
`resolved=0`): title/intro/main menu (partially safe via `px<0`), Pokédex, Town Map, Pokémon summary,
trainer card, naming keyboard, PokéNav (EM), Pokémon Storage/PC, options menu, save dialog beyond the
yes/no popup, marts/shops (the buy list is a ListMenu but NOT the bag handler), Mystery Gift/Event,
contests + Pokéblock case/berry blender (EM), Battle Frontier apps, game corner/slots, mail, party
summary page-switching, move-learn/forget, evolution scene, credits, FRLG Teachy TV / Fame Checker /
VS Seeker UIs, FRLG minigames. The two source-comment lists agree: `gamestate.c:522`
("pokedex/townmap/summary/card/keyboard/title") and `touch.h:44-45`
("map/PokeNav/Pokemon PC/move-learn/intro/Battle Frontier").

---

## 3. Known-broken / known-missing

From `docs/kb/touch-issues-todo.md` (2026-06-10 hardware run) — current status:

| # | Issue | Status today |
|---|---|---|
| 1 | Walk broken | **FIXED + hw-verified** (task-based START detection `10d768f`, hybrid steer/tap walk `7fb6f53`; user confirms walking is good). |
| 2 | Naming keyboard touch | **Still missing** (no detection, no hit map). Falls through to the overworld residual. |
| 3 | Bag: tap the pocket tab directly | **Still missing** — pocket switch is horizontal-swipe only (`touch.c:588-589`). |
| 4 | Summary page-switch by touch (tap the page dots) | **Still missing** — the summary screen isn't even detected. |
| 5 | Popup menus don't register (party SUMMARY/SWITCH popup, yes-no) | **FIXED in code** (`a661b80`: selMenu/yesNo/multi tasks detected with precedence over party, `gamestate.c:329-331`); no recorded hw sign-off. |
| 6 | Bike moves wrong direction | **FIXED + hw-exercised** (hold = directional steering, no BFS while touching). |

From the phase-18 T5 audit (`docs/phase18-crisp/SPEC-door.md:464-520`) — residual limitations still true:

- **T5.2 Ledges:** `MB_JUMP_*` are collision-1 walls to the router — never falls off one, but routes
  that REQUIRE a ledge hop are never found.
- **T5.3 Tall grass:** routes cross it; a wild encounter drops the route correctly, but nothing
  resumes it after the battle.
- **T5.8 Forced-movement tiles** (ice, currents, spin, muddy slopes): walkable to the router, slide
  the player, desync into a stall. **Waterfalls + currents are now mitigated in BOTH routers'
  answers (phase 29 / lane F); ice, spin mats and muddy slopes are not.**
  - phase 26: `fieldtrav`'s `transition()` refuses a WATERFALL as a transit tile in both modes, so
    the traversal layer stopped plotting a swim up a fall (5 checks in `test_fieldtrav` TEST 22).
  - phase 28 / lane X measured what was left, live (Route 114, emulator): with Waterfall not yet
    usable, a tap on the fall handed the retargeted goal to the FROZEN `fieldpath`, which planned a
    4-step swim UP the column and reported `end=ARRIVED` while the player never moved (defect **X2**,
    LANE-X-EXECUTE.md §1.4) — the case that bites at the SHIPPED DEFAULT, where the tap gate does
    not run at all.
  - **phase 29 / lane F closed both halves.** (a) The CURRENTS twin (0x50–0x53) joined the waterfall
    in the same `transition()` refusal — `fieldtrav_is_current`, read in all three engines' own
    headers. (b) `walk_plan` (touch.c) now SCREENS every path the frozen router returns through
    `fieldtrav_path_forced` and refuses the plan when it enters a waterfall or a current — at the one
    choke point every route in the app is planned through, so the default is covered too.
    `fieldpath.{c,h}` is still byte-identical to phase 18's `010a138`. Graded on the user's own
    cartridge in `test_fieldtrav` TEST 25 (the phase-28 repro reproduced and then refused); the
    live re-run is **still owed** (LANE-F-EXECUTOR.md §3 — the emulator's state channel wedged).
  - Still unmitigated: ice (0x02), the secret-base spin/jump mats, and muddy slopes. None of them is
    reachable by a surfing route, but all four are `sForcedMovementTestFuncs` entries and the same
    screen would take them in one term each.
- **T5.9 Connected maps:** behaviour reads outside the current map's bounds are refused
  (classification falls back to legacy) — pret has the identical limitation.
- Bag: visible-rows-only select; no direct scroll-field write (deliberate — racy); no item-count
  clamp, so a tap in blank space below a short pocket hits the highest visible row (often CLOSE BAG —
  accepted in spec §2d).
- Party: **MULTI layout (layout==2) has no rect table** — `hit_party` treats any non-DOUBLE layout as
  SINGLE (`touch.c:199`), i.e. multi-battle party menus get WRONG rects, not a safe no-op (the spec
  said "fall through"; the code does not). Confirm slot 6 (chooseHalf) unmapped.
- Battle target rects are untuned ±28 px heuristics; the highlight doesn't follow the written cursor
  (cosmetic, both party + target).
- `TERM_FRAMES=30` terminal hold and the whole door/arrow/stair routing: **hardware-unproven**
  (phase-18 shipped after the last hands-on; "your hardware is the first real test").
- Smart touch during a wireless link runs as an accepted **benign EWRAM race** (`main.c:3493`,
  `main.c:3619`) — note this SUPERSEDES the old rule in `gen3-ram-touch.md:54-57` ("skipped while a
  link is active"); that doc is stale on this point.
- Y-anchor row 5 (`gy/16 - 5`, `touch.c:418,426`): flagged ±1-uncertain in the spec, since
  hw-exercised by working walking — treat as settled unless a report says otherwise.

---

## 4. The promotion pipeline as it exists

The design rule (KB): **never ship a guessed address — capture the real cb2 live, resolve it against
pret's byte-matched sym maps, then promote it into the profile.** The instrumentation for that is
already fully wired (all LOGGING ONLY):

1. **Every frame**, `game_read` captures the raw screen fingerprint regardless of detection: `cb2`,
   `cb1` (Thumb-stripped, `gamestate.c:289-290`), `ctxResolved`, and up to 8 active `gTasks` function
   pointers, **sorted so the same screen fingerprints identically on every visit**
   (`gamestate.c:299-310`).
2. **The gs logger** (`gs_log_sample`, `gamestate.c:458-504`) appends a ring row on every ctx/cb2
   edge (plus link-state edges, a 600-frame emulated heartbeat and a 2 s wall-clock heartbeat that
   keeps logging a frozen game), per screen slot. One SD dump per session
   (`gamestate_log_dump` → `sdmc:/cias/netlogs/…`), whose header **documents the pipeline itself**:
   "read the cb2 column for each [undetected screen] you visit, then promote that value into a
   GameProfile later (logging only; no detection wired yet)" (`gamestate.c:521-523`). Each row also
   carries the phase-13 diagnostics: player/avatar geo (px/py, objX/objY, mapG/mapN, facing), the
   injected key mask, sb1V/resolved flags, 3D-effect health, tilt, presence and peer-sprite columns,
   and the link-error block (`gamestate.c:539`).
3. **The touch logger** (`touch_log_sample`, `touch.c:667-702`) records per touch EVENT: ctx +
   fingerprint (cb2 + 8 taskFp), the context's authoritative cursor read BEFORE and AFTER the handler
   (the "touch moved the game's cursor" proof, `touch.c:619-637`), and the injected key mask. Its dump
   header names the identification recipe for undetected screens: "identify them by cb2 + t0..t7"
   (`touch.c:746`). The fieldpath plan ring (`fplog`, `touch.c:302-325,707-733`) appends one row per
   route plan/end into the same file.
4. **The gdb mirror** `g_fieldDbg` (`touch.h:59-87`, non-static by design) exposes the live route +
   `SaveBlock1.location` to the emutest harness (`gdbio read-u32 g_fieldDbg+N`, `poll --changed`,
   `see rec --with-state`) — the objective warp-proof channel on the emulator.
5. **Promotion precedent:** the FR/LG `mapObjects` saga (memory: "0x02036E38 was correct all along")
   and every VERIFIED-SYM comment block in `PROFILES[]` show the finished pipeline: live capture →
   sym-map resolution (rev0 AND rev1, LG's own maps not FR-derived) → a commented, positional append
   to the profile. Appending is the only safe edit — `PROFILES[]` is positional-initialised
   (`gamestate.h:93-95`).

What is NOT wired: nothing promotes automatically; there is no per-screen GCTX for any harvested cb2
yet, and the `GameCtx` enum + `touch_update` switch must be extended by hand per screen.

---

## 5. The input-injection seam and its limits

**Path:** `touch_update` returns a `u16` GBA key mask (`touch.c:777`) → `main.c` ORs it into the
bottom seat's key word together with the physical pad (focused seat only) and D4 script keys:
`emuA.keys = ((focused==0)?g:0) | (swapped?tk:0) | ckA;` etc. (`main.c:3899-3903`, strictly additive,
nothing clears bits) → `gbacore_set_keys` per frame (`main.c:521,543,570`). The injected mask is
recorded in both logs (`inj` column).

**What IS expressible:**
- **All ten GBA keys**: the mask covers A, B, SELECT, START, D-pad, R, L (`gbacore.h:17-20`). PAD
  mode already injects L/R/START/B; SMART today only ever emits A, START, and D-pad.
- **HOLDS** — yes, first-class. The mask is level-triggered per frame: the overworld steer returns
  the direction every frame while touching, the terminal hold sustains one direction for up to 30
  frames (`touch.c:465-476`), and GCTX_BATTLE_OTHER returns A for as long as the finger is down
  (`touch.c:837`). So hold-to-scroll a list = returning DOWN each frame is already expressible (the
  bag instead rate-limits to one press per 14 px of drag, `touch.c:586-587` — a design choice, not a
  seam limit).
- **Pulses with clean edges**: the tick0/tick1 pattern guarantees the game sees a fresh 0→1 A edge
  (`select_pulse`), and A/START pulses run 3 frames (`s_aPulse=3`, `touch.c:409`).
- **Chords** — any same-frame combination (SELECT+direction, L+R, etc.) is one OR'd mask; nothing in
  the seam forbids it. No current handler emits a chord.
- **RAM writes alongside keys**: `gbacore_write8/write16` (`gbacore.h:123-124`) let a handler place
  the game's own cursor before the A lands (the write-then-A idiom). The write lands before the
  worker runs the frame (main thread ordering — spec Risk 5).

**Limits that constrain touch-native designs:**
- **Additive only**: injection can never SUPPRESS a physical key or a script key (D4 rule,
  `main.c:3896-3898`). A design that needs "block the real D-pad while dragging" cannot have it.
- **No sub-frame timing**: one mask per emulated frame; a press shorter than a frame or two distinct
  edges within a frame are impossible. Repeat-rate for lists is therefore capped at the game's own
  key-repeat handling unless the game treats held keys as auto-repeat (Gen-3 list menus do).
- **Bottom game only**: `tk` routes to whichever core is on the bottom screen (`swapped` selects,
  `main.c:3901-3902`); the top game is reachable only by the physical pad/focus toggle.
- **Cross-thread race during wireless**: reads/writes during a link are a tolerated benign EWRAM race
  (`main.c:3619`), fine for cursors, not fine for anything that must be transactional.
- **write8/write16 exist, write32 does not** (`gbacore.h:123-124`) — any future screen whose cursor
  is a 32-bit field needs two write16s or a new primitive.
- **Hold durations are frame-count constants** (`TAP_FRAMES`/`TERM_FRAMES` etc.) counted in EMULATED
  frames — they need re-validation at degraded wireless frame rates (SPEC-door Open Q6).

---

## 6. One-line summary for the planner

Touch-native today = overworld (hw-proven core + hw-unproven phase-18 warp terminals), battle
action/move (hw-proven), battle-other tap-advance, and four menu systems (target select, party,
sMenu popups/START/yes-no/multichoice, bag) that are shipped but lack a recorded hardware sign-off —
**on BPEE/BPRE/BPGE only** (RS = walk-only on paper, never executed). Everything else in all five
games falls into the GCTX_OVERWORLD residual where taps leak walk/A/START keys into whatever screen
is actually showing. The harvest→promote pipeline (cb2 + sorted taskFp in both SD logs + the gdb
mirror) is fully instrumented and waiting; the injection seam can express holds, chords and all ten
keys, so no currently-conceivable touch-native design is blocked by the seam itself.
