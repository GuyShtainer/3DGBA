# LANE B — Ruby/Sapphire live verification (instance b)

**Session 2026-08-14, overnight lane B (evidence-only — no source/test edits).**
Boot recipe: `run --instance b azctl boot --gdb --movie boot_resume.ctm --wipe-netlogs
--stage-roms ruby` (NOTE for the ops book: `--fresh-sd-fixtures` is the dual-gba tier-B
staging path and FAILS on instance b — `--stage-roms` alone is correct there), then
`gdbio resume`, D4 via `sdmc drop move 1 …`, live reads via the gs-logger statics.

**Hermetic-lane rule used (recommended for every two-lane session):** the ELF+3dsx were
snapshotted to the session scratchpad and every azctl/gdbio call ran with
`EMUTEST_ELF`/`EMUTEST_APP` pointing at the snapshot — a Lane-A rebuild of the main tree
mid-session cannot skew instance-b symbol reads or the shared `emutest.mapsyms` cache.
gs statics were re-derived from the snapshot by nm (they SHIFTED vs the census build:
`s_gsLogN 0x00531a60`, `s_lastCb2 0x00531a64`, `s_gsLog 0x00531a7c` (1024×156 B),
`s_lastCtx 0x0052442c`, `g_presDiag 0x00564a80`). Query tool: scratchpad
`laneb/gsqb.py`; resolution against `pokeruby_rev2.sym` / `pokesapphire_rev2.sym`
(rev1 == rev2 byte-identical; correct maps for the user's rev-2 carts per
RS-REV2-VERIFICATION.md §2 — names in those maps are rev0-derived `sub_*` in places, the
ADDRESSES are what verify).

## 1. RUBY (AXVE) FIRST BOOT — the first time this profile ever executed

The staged save is the user's real 600-hour Ruby save (main menu: GUY, TIME 614:45,
POKEDEX 259, BADGES 8 — `evidence/ruby/A3-mainmenu-600h-save.png`).

### Anchor verdicts (all read from the LIVE game, resolved on pokeruby_rev2.sym)

| anchor (shipped value) | live evidence | verdict |
|---|---|---|
| profile row match (4-char code) | control log header `p1=AXVE` | **VERIFIED** |
| `mainCb2` = gMain+4 (0x03001774) | every screen change tracked; six distinct cb2 values below, all resolving **[exact]** on symbol boundaries — a wrong base would give offset garbage | **VERIFIED [exact]** |
| `sb1ptr` 0x02025734 (sbDirect) | px/py tracked every walk token closed-loop (D2: (10,2)→(10,4); L3: (10,4)→(7,4); etc.), map (13,6)→(0,5)→(0,36) across a door warp and a map connection | **VERIFIED** |
| `mapObjects` 0x030048A0 | objX/objY = px+7/py+7 on every single ring row (the MAP_OFFSET relation), facing byte tracks turns | **VERIFIED** |
| `gTasksBase` 0x03004B20 | 7 distinct live task fps ALL resolve [exact]: `Task_RunPerStepCallback` 0x0806945C, `Task_RunTimeBasedEvents` 0x080694D8, `Task_MuddySlope` 0x0806A208, `Task_WeatherMain` 0x0807CA54, start-menu task 0x080712D4, `HandleDefaultPartyMenu` 0x08089CF4, bag task 0x080A50E8 | **VERIFIED** |
| **`battleMainCb` 0x0800F808 — the ONE shipped RS ROM address** | wild Linoone on Route 121: cb2 flipped to 0x0800F808 `BattleMainCB2` **[exact]**, ctx left `field` for the first time (resolved=1) | **VERIFIED [exact] — the verify-on-hw flag in gamestate.c:170-175 can be considered live-proven (emulator); hw run still owed per the done-gate** |
| battle sub-gates (`actionCursor`/`moveCursor`/`ctrlFuncs`/`battlerPos`/`battlersCount`/`absentFlags`/`activeBattler`) | ctx sequence over the battle: `b.oth` (intro dialog) → `b.act` (FIGHT/BAG/POKEMON/RUN) → `b.move` (move menu) → back to `b.act` → fled → `field`. Each state matched the on-screen truth (captures C1–C3) | **VERIFIED (behavioral)** |
| `startCb`/`startCursor` (0x03004AE8/0x0202E8FC) | not independently verifiable live — RS ships `startCbInput=0`/`startMenuTask=0` (symbols don't exist in RS), so no fmenu detection exists to exercise them; START menu correctly stays `field` | **N/A by design (COVERAGE.md §2)** |
| zeroed ROM pointers (partyTask, cb2BagRun, bagHandler, …) | party/bag/START screens all read ctx=`field`, resolved=0 — the documented degradation, no false positives | **VERIFIED (degrades as documented)** |

### Census-promoted gates on RS — behave exactly as the source says

GCTX_TITLE / GCTX_FULLUI did NOT fire on Ruby's intro/title/main-menu — **correct**:
the RS `cb2Title`/`cb2FullUi` lists ship empty (gamestate.c:653). The harvest below is
the promotion payload that would light them up.

### RS cb2 harvest (promotion candidates, all live-read [exact], pokeruby_rev2.sym)

| screen | cb2 | symbol | module (neighborhood) | proposed class |
|---|---|---|---|---|
| GF intro | 0x0813B7B8 | `MainCB2_Intro` | intro.c | cb2Title |
| Title screen | 0x0807C474 | `MainCB2` | title_screen.c (between `CB2_InitTitleScreen` 0x0807C110 and `Task_TitleScreenPhase1` 0x0807C48C) | cb2Title |
| Main menu | 0x080096C4 | `CB2_MainMenu` | main_menu.c | cb2Title |
| Overworld | 0x080543C4 | `CB2_Overworld` | overworld.c | (the field baseline) |
| Party menu | 0x0806AEFC | `CB2_PartyMenuMain` | party_menu | cb2FullUi (or a future RS partyTask promotion: task `HandleDefaultPartyMenu` 0x08089CF4 [exact]) |
| Bag | 0x080A3138 | `sub_80A3118`+0x20-named run loop (rev-drifted name; address is the live run-loop entry) | item_menu (bag task 0x080A50E8 `sub_80A50C8` [exact]) | cb2FullUi |

**Promotion rule reminder (RS-REV2-VERIFICATION.md §5):** promote RS ROM values from the
rev1/rev2 maps only (these were read live on the rev-2 fixture ROM, so they ARE the
user's-cart values).

### Substrate / ops findings

- **D4 closed-loop walking works on AXVE** — per-token start/target verification against
  SaveBlock coords, warp-complete detection fired correctly on BOTH a door warp
  (13.6→0.5) and a route connection (0.5→0.36). This is the same coord/collision
  substrate smart-touch walking rides on.
- Save resumes inside the Lilycove Pokemon Center facing the PC — blind A-taps BOOT THE
  PC (as happened); b-spam closes it. The 600h save's party lead is a L100 Dragonite
  ("GUY") — wild battles are risk-free.
- Route 121 grass reached from the Center: out the door, D to y=15, L to the west edge
  (one sign blocks y=15 at x≈6 — dodge via y=16), cross at (0,16), then on the route
  L to x=74, U1/D-route around the fence corner, D6 to y=12, U2 at x=72 into the grass
  row (y=10-11). Encounter fired on the 3rd grass tile.
- fps solo on instance b: 20-30 fps in the field (HUD), matches the census-era envelope.

Captures: `evidence/ruby/` (A1 intro, A2 title, A3 main menu, B1/B1b/B1c overworlds,
B2 start menu, E2 party, E3 bag, C1 battle intro, C2 action, C3 move select).

## 2. RS CO-OP (ruby + sapphire, one console) — the phase-18 universe-gate answer

Boot: `--stage-roms ruby,sapphire` (pair mode), presence enabled by pre-seeding
`sdmc:/3DGBA/settings.bin` word 24 = 1 BEFORE the boot (the 25-word ladder,
main.c:2350-2378 — patching the existing file preserves every other pref; `--stage-roms`
does not touch settings.bin). Control header: `p1=AXVE p2=AXPE` — **the AXPE row matched
live too** (both RS profiles now executed).

### The universe-gate question (SPEC-coop P3.1, phase 18): AXVE vs AXPE — EMPIRICALLY ANSWERED

`g_presDiag` (magic 'PRS1' verified), both games in their overworlds:

```
enabled=1  pairReason=0  artOk=1
game0 (Ruby):     reason=map  live=active  gameId=3  pairGame=3  selfMap=13,6  peerMap=0,7   dTile=(33,30)
game1 (Sapphire): reason=map  live=active  gameId=3  pairGame=3  selfMap=0,7   peerMap=13,6  dTile=(-33,-30)
```

- **`presence_game_id` maps BOTH titles to PRES_GAME_HOENN_RS (3) and the pair gate
  PASSES (`pairReason=0`)** — the code's shared-universe claim (presence.c:122-129) is
  now live-proven, and the **CO-OP chip renders in the HUD**
  (`evidence/rs-coop/coop-chip-ruby-top.png`).
- Record exchange works both directions: each game's peerMap/dTile mirrors the other
  (symmetric ±(33,30)), liveness `active` both sides.
- The ONLY blocker is the honest one: `reason=map` (PRES_OFF_MAP) — Ruby resumes in the
  Lilycove Center (13,6), Sapphire in the Sootopolis Center (15,2 → city 0,7).
  **Same-map was NOT cheap and was not forced:** Sootopolis is the dive-locked city —
  reaching Lilycove needs Surf+Dive across routes 124-126 (an HM-traversal job, exactly
  the SPEC-family-traversal territory). Recorded honestly per the lane brief.
- Bonus finding: during the pre-CONTINUE menu phase both SaveBlocks briefly read equal
  garbage/defaults → `blitN=546` avatar quads were submitted before the saves loaded and
  diverged. Harmless (logging shows drawn=0 once real records exist) but worth knowing
  when reading blitN in future diags.

### HEADLINE for the RS promotion pipeline: Ruby and Sapphire ROM addresses DRIFT

The maps `pokeruby_rev2.sym` vs `pokesapphire_rev2.sym` are DIFFERENT files (md5
acb6be37… vs 3c95f804…, both 50,963 lines) and the LIVE reads confirm real per-title
ROM drift — the phase-18 "727 RAM symbols identical across all six maps" proof was
**RAM-only** and does NOT extend to ROM:

| screen | Ruby (live [exact]) | Sapphire (live [exact]) | drift |
|---|---|---|---|
| GF intro `MainCB2_Intro` | 0x0813B7B8 | 0x0813B7B8 | same |
| Title `MainCB2` | 0x0807C474 | 0x0807C478 | **+4** |
| `CB2_MainMenu` | 0x080096C4 | 0x080096C4 | same |
| `CB2_Overworld` | 0x080543C4 | 0x080543C8 | **+4** |
| `BattleMainCB2` (shipped anchor) | 0x0800F808 | 0x0800F808 (map-verified [exact]; battle not re-entered on Sapphire) | same — the 6/6 desk proof holds |
| Party `CB2_PartyMenuMain` | 0x0806AEFC | differs (ruby's value resolves inside `Task_ResetRtcScreen` on the sapphire map) | **drifts** |
| Bag run loop | 0x080A3138 | 0x080A3138 (`sub_80A3118`[exact] both maps) | same |

Sapphire's own extra harvest (live, [exact] on pokesapphire_rev2.sym):
`CB2_GoToMainMenu` 0x0807C798, `CB2_InitMainMenu` 0x080096F0, `CB2_ContinueSavedGame`
0x08054758, `CB2_ReturnToFieldLocal` 0x08054634.

**Rule this establishes: any future RS cb2Title/cb2FullUi promotion must carry PER-TITLE
values (AXVE row ≠ AXPE row for ROM pointers) — the shared `RS_PROFILE_BODY` macro is
still correct today only because its ONE ROM value (`BattleMainCB2`) happens to sit
below the drift point.** A shared-body promotion would silently break one title's
detection — the exact BPGE failure mode all over again.

### Ops notes

- Dual-RS on instance b: 16-21 fps per game (Lane A was idle; budget less when both
  lanes run).
- Sapphire save resumes facing its Center PC like Ruby — same blind-A hazard.
- Sootopolis Center exit mat is at local (7,9); the D5-then-down-the-middle route dead-ends
  at (8,8) — go L to x=7 first.

Captures: `evidence/rs-coop/` (both-screen pairs + the CO-OP chip frame).

## 3. Sapphire solo smoke (task 4)

One solo boot (`--stage-roms sapphire`): control header `p1=AXPE` — **the AXPE row also
matches in the PRIMARY seat** (the co-op boot only proved it as gameB); intro
`MainCB2_Intro` 0x0813B7B8 [exact], overworld `CB2_Overworld` 0x080543C8 [exact] (the
sapphire-drifted value, §2), sb1/mapObjects coherent (px 10,2 / obj 17,9 / map 15,2 =
the Sootopolis Center resume), closed-loop D3 walk (10,2)→(10,5) verified per-token.
gs ring logging sane throughout. Captures: `evidence/sapphire/` (3 files).
Instance stopped clean (config restored byte-identically, fixtures re-hashed untouched).
