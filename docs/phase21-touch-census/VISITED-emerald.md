# VISITED — Emerald (BPEE, user's endgame save, staged fixture copy)

Session 2026-08-13, census boot (movie `boot_resume.ctm` = wait 900f → tap A on the app's
resume prompt → 6h idle tail; ALL navigation via the D4 channel `move_p1.txt`, Emerald =
gameA = seat 1 = TOP screen / screen 0). Identification per row: live `gMain.callback2`
via the gs-logger statics over gdb, resolved on `pokeemerald.sym` (see CB2-HARVEST.md).
"Detected" = the app's live GCTX (`s_lastCtx[0]`): what touch currently thinks the screen
is. **residual** = falls through to overworld/field = the touch gap this census exists to
close. Evidence: `evidence/emerald/<id>.{top,bottom}.png` (native-res reconstructions).
Raw capture log: `CAPTURES-emerald.log`.

| ID | Catalog | Screen | cb2 → symbol | Detected | Evidence | Repro route |
|---|---|---|---|---|---|---|
| A1-intro | A1 | GF intro movie | `MainCB2_Intro` | field (residual) | A1-intro.* | boot, wait |
| — | A2 | Title screen | `MainCB2`(title_screen.c) — cb2 harvested, PHOTO MISSED (A-tap raced past; needs the planned second boot) | field (residual) | — | boot → A past intro |
| — | A3 | Main menu | `CB2_MainMenu` — cb2 harvested live; photo pending second boot | field (residual) | — | title → A |
| B1-overworld-indoor | B1 | Overworld (Battle Arena lobby) | `CB2_Overworld` | field ✓ (the home context) | B1-overworld-indoor.* | continue save |
| B2-startmenu | B2 | Start menu | `CB2_Overworld` (task) | **fmenu ✓ positive** | B2-startmenu.* | START |
| B3-scriptdialog | B3 | Script dialog (arena guide NPC) | `CB2_Overworld` | field (textDlg flag carries it) | B3-scriptdialog.* | A at counter NPC |
| B5-savedialog | B5 | Save dialog (info + yes/no) | `CB2_Overworld` (tasks) | **fmenu ✓ positive** | B5-savedialog.* | start menu → SAVE |
| B5b-saving | B5 | "SAVING… DON'T TURN OFF" | `CB2_Overworld` | fmenu | B5b-saving.* | confirm save |
| A5-options | A5 | Options menu (start-menu entry) | `MainCB2`(option_menu.c) | field (residual) | A5-options.* | start menu → OPTION |
| F1-dexlist | F1 | Pokedex list | `CB2_Pokedex` | field (**residual**) | F1-dexlist.* | start menu → POKEDEX |
| F2-dexentry | F2 | Dex entry page (AREA/CRY/SIZE/CANCEL buttons) | `CB2_Pokedex` | field (**residual**) | F2-dexentry.* | dex list → A |
| F3-dexarea | F3 | Dex area screen | `CB2_Pokedex` | field (**residual**) | F3-dexarea.* | entry → AREA |
| F4-dexcry | F4 | Dex cry screen (VU meter) | `CB2_Pokedex` | field (**residual**) | F4-dexcry.* | entry → CRY |
| F2b-dexsize | F2 | Dex size-compare page | `CB2_Pokedex` | field (**residual**) | F2b-dexsize.* | entry → SIZE |
| F5-dexsearch | F5 | Dex search UI (criteria grid) | `CB2_Pokedex` | field (**residual**) | F5-dexsearch.* | dex list → SELECT |
| E2-party | E2 | Party menu (field) | `CB2_UpdatePartyMenu` | **party ✓ positive** | E2-party.* | start menu → POKEMON |
| E2b-partysub | E2 | Party context sub-menu (SUMMARY/SWITCH/ITEM/CANCEL) | `CB2_UpdatePartyMenu` | **fmenu ✓ positive** | E2b-partysub.* | party → A |
| E2c | E2 | (same sub-menu, Salamence w/ FLY) | 〃 | fmenu ✓ | E2c-partysub-salamence.*, B8a-fly-submenu.* | |
| E2d-party-hmchoose | E2/D7-family | Party in HM-teach choose mode (ABLE/NOT ABLE labels) | `CB2_UpdatePartyMenu` | field (**residual** in this mode!) | E2d-party-hmchoose.* | bag → HM02 → USE |
| E1-summary-info | E1 | Summary p1 INFO | `MainCB2`(summary) | field (**residual**) | E1-summary-info.* | party → SUMMARY |
| E1-summary-skills | E1 | Summary p2 SKILLS | 〃 | field (**residual**) | E1-summary-skills.* | RIGHT |
| E1-summary-battlemoves | E1 | Summary p3 BATTLE MOVES | 〃 | field (**residual**) | E1-summary-battlemoves.* | RIGHT |
| E1-summary-contestmoves | E1 | Summary p4 CONTEST MOVES | 〃 | field (**residual**) | E1-summary-contestmoves.* | RIGHT |
| C22-forgetmove | C22 | Forget-move summary (5-move pick, FLY teach) | `MainCB2`(summary) | field (**residual**) | C22-forgetmove.* | HM teach → "deleted and replaced?" YES |
| E3-bag-items | E3 | Bag ITEMS pocket | `CB2_BagMenuRun` | **bag ✓ positive** | E3-bag-items.* | start menu → BAG |
| E3-bag-pokeballs | E3 | POKE BALLS pocket | 〃 | bag ✓ | E3-bag-pokeballs.* | RIGHT |
| E3-bag-tms | E3 | TMs & HMs pocket | 〃 | bag ✓ | E3-bag-tms.* | RIGHT |
| E3-bag-berries | E3 | BERRIES pocket | 〃 | bag ✓ | E3-bag-berries.* | RIGHT |
| E3-bag-keyitems | E3 | KEY ITEMS pocket | 〃 | bag ✓ | E3-bag-keyitems.* | RIGHT |
| E3b-berrysub | E3 | Bag item sub-menu (CHECK TAG/USE/GIVE/TOSS/CANCEL) | `CB2_BagMenuRun` | field (**residual** — GCTX_BAG drops out in sub-menus; audit finding) | E3b-berrysub.* | berries → A |
| E6-berrytag | E6 | Berry tag screen | `CB2_BerryTagScreen` | field (**residual**) | E6-berrytag.* | berry sub-menu → CHECK TAG |
| H7-pokeblockcase | H7 | Pokeblock case | `CB2_PokeblockMenu` | field (**residual**) | H7-pokeblockcase.* | key items → POKEBLOCK CASE → USE |
| H9-usepokeblock | H9 | Use-pokeblock condition screen (radar) | `CB2_UsePokeblockMenu` | field (**residual**) | H9-usepokeblock.* | case → block → USE |
| H9b | H9 | its yes/no confirm | 〃 | field | H9b-usepokeblock-confirm.top.png | A on mon |
| H8-pokeblockfeed | H8 | Pokeblock feed scene | `CB2_PokeblockFeed` | field (residual; cutscene) | H8-pokeblockfeed.* | confirm YES |
| G1-pokenav-main | G1 | PokeNav main menu | `CB2_Pokenav` | field (**residual**) | G1-pokenav-main.* | start menu → POKENAV |
| G2-pokenav-map | G2 | PokeNav Hoenn map (zoom view) | 〃 | field (**residual**) | G2-pokenav-map.* | → HOENN MAP |
| G3a | G3 | Condition sub-menu (PARTY/SEARCH/CANCEL) | 〃 | field | G3a-pokenav-condition-menu.* | → CONDITION |
| G3-pokenav-condition | G3 | Party condition radar graphs | 〃 | field (**residual**) | G3-pokenav-condition.* | → PARTY |
| G4-pokenav-matchcall | G4 | Match Call list | 〃 | field (**residual**) | G4-pokenav-matchcall.* | → MATCH CALL |
| G5-pokenav-ribbons | G5 | Ribbons holder list | 〃 | field (**residual**) | G5-pokenav-ribbons.* | → RIBBONS |
| G5b | G5 | Ribbon summary (per-mon) | 〃 | field | G5b-pokenav-ribbonsummary.* | → A on holder |
| D1-frontierpass | D1 | Frontier Pass (free pixel cursor!) | `CB2_FrontierPass` | field (**residual**) | D1-frontierpass.* | start menu → GUYA (FLAG_SYS_FRONTIER_PASS swaps the trainer-card entry) |
| D1b-frontiermap | D1 | Frontier map feature (facility list) | 〃 | field (**residual**) | D1b-frontiermap.* | pass → map area → A |
| B6-trainercard-front | B6 | Trainer card front | `CB2_TrainerCard` | field (**residual**) | B6-trainercard-front.* | pass → card area → A (in-frontier route) |
| B6b-trainercard-back | B6 | Trainer card back | 〃 | field (**residual**) | B6b-trainercard-back.* | A flips |
| B8-flymap | B8 | Fly map ("FLY to where?") | `CB2_FlyMap` | field (**residual**) | B8-flymap.*, tmp-fly-nav11 (Littleroot selected) | party → Salamence → FLY (outdoors only — indoors = "Can't use that here", captured) |

## Session facts worth keeping
- **Party (endgame)**: Tyranitar / Salamence / Metagross / Gengar / Dragonite / Milotic
  (L72–74). NO Fly user originally; **HM02 Fly taught to Salamence over Steel Wing this
  session (fixture save only)** — movesets photographed (tmp-moves-*.png in the log dir).
  Milotic has Surf. Button mode = **L=A** in the user's options!
- Save location: Battle Frontier, Battle Arena lobby (map 26,28). Frontier Pass: 31 BP,
  0 symbols visible, S.S. Ticket + Mystic/Aurora/OldSeaMap tickets in bag, Coin Case yes,
  Pokeblock Case with 3 Indigo pokeblocks (one fed to Tyranitar this session).
- In-game save performed once (Battle Arena lobby) — fixture copy only.
- D4 lessons: emu runs 13–28 fps in this dual boot → W-frame waits are EMULATED frames
  (wall time ×2.5–4); always poll the control log for `script done` (census drop.sh).
  Post-screen-return input lockouts swallow taps — pad a W60+ after every screen return.

## Boot #2 (same day) — title family, battles, PC family, Mauville

| ID | Catalog | Screen | cb2 → symbol | Detected | Evidence | Repro route |
|---|---|---|---|---|---|---|
| A2-title | A2 | Title screen (photo landed this boot) | `MainCB2` (title_screen.c 0x080AAB2C) | field (residual) | A2-title.* | boot → A past intro |
| A3-mainmenu | A3 | Main menu (CONTINUE 117:21 / NEW GAME / MYSTERY GIFT / OPTION) | `CB2_MainMenu` | field (residual) | A3-mainmenu.* | title → A |
| J12-mysterygift | J12 | Mystery Gift shell — "The Wireless Adapter is not connected." | `CB2_MainMenu` (the adapter check happens before the gift cb2 swap) | field | J12-mysterygift.* | main menu → MYSTERY GIFT |
| C5-battledialog | C5/C6 | Wild battle intro ("Wild POOCHYENA appeared!") | `BattleMainCB2` 0x08038420 | **b.oth ✓ positive** | C5-battledialog.* | walk Route 101 grass |
| C2-battleaction | C2 | Action select | `BattleMainCB2` | **b.act ✓ positive** | C2-battleaction.* | battle → A |
| C3-battlemove | C3 | Move select | `BattleMainCB2` | **b.move ✓ positive** | C3-battlemove.* | FIGHT |
| B7-fieldregionmap | B7 | Field region map (wall map, Oldale Center) | `MCB2_FieldUpdateRegionMap` [exact] — catalog's `CB2_FieldShowRegionMap` is the *setup*; THIS is the live loop | field (residual) | B7-fieldregionmap.* | Center wall map → A |
| E11-pctopmenu | E11 | PC top menu (LANETTE'S PC / GUYA's PC / HALL OF FAME / LOG OFF) | `CB2_Overworld` (script multichoice) | **fmenu ✓ positive** | E11-pctopmenu.* | Center PC → A |
| E12a | E12 | Storage task menu (WITHDRAW/DEPOSIT/MOVE POKEMON/MOVE ITEMS/SEE YA) | `CB2_Overworld` (multichoice) | fmenu ✓ | E12a-storagemenu.* | Lanette's PC |
| E12-storage-boxes | E12 | Boxes UI (6×5 grid, hand cursor) | `CB2_PokeStorage` 0x080C7D54 [exact] | field (**residual**) | E12-storage-boxes.* | MOVE POKEMON |
| E12b/c/d/e/f | E12 | mon menu / holding / box-title menu (JUMP-WALLPAPER-NAME-CANCEL) / party strip / box-jump selector | `CB2_PokeStorage` | field (**residual**) | E12b..E12f-*.png | in-boxes navigation |
| E10-naming | E10 | Naming keyboard ("BOX NAME?") | `CB2_NamingScreen` 0x080E4F58 [exact] | field (**residual**) | E10-naming.* | box menu → NAME |
| B9-wallclock | B9 | Wall clock view (bedroom) | `CB2_WallClock` 0x08134C9C [exact] | field (**residual**) | B9-wallclock.* | bedroom clock → A |
| H10-tvnews | H10 | TV programs (POKEMON NEWS) | `CB2_Overworld` (dialog) | field | H10-tvnews.* | bedroom TV → A |
| G4b | G4 | PokeNav Match Call INCOMING-call overlay (rings mid-walk!) | `CB2_Overworld` (Task_DrawFieldMessage) | field | G4b-matchcall-incoming.* | walk with 80 registered callers |
| I6-coinclerk / I6b | I6 | Coin clerk dialog + coin purchase menu (50/500/EXIT, MONEY+COINS windows; save has **510 coins**) | `CB2_Overworld` | fmenu ✓ (menu) | I6-coinclerk.*, I6b-coinbuy.* | Game Corner clerk (11,2) |
| I4-slotmachine | I4 | Slot machine UI (CREDIT/PAYOUT, SELECT INFO, B QUIT) | `CB2_SlotMachine` 0x0812A670 [exact] | field (**residual**) | I4-slotmachine.* | Game Corner slot (8,6) |
| E1b (bonus) | — | All six party movesets (Fly audit) | summary `MainCB2` | field | E1b-moves-*.png | summary D-cycling |

## Honest not-visited (Emerald, this pass)
- **I5 roulette, I8 prize exchange** — in the same Game Corner room; the visit ended stuck
  inside CB2_SlotMachine (injected B did not quit the machine — itself a finding worth a
  phase-22 look, since a touch QUIT button would need the same path) and the session was
  closed for time. Route documented: tables at (14-15,6-8)/(18-19,6-8), prize clerks (13,2),(14,2).
- **I1/I2 mart buy/sell** — Oldale Mart door (14,6); not reached before wrap. (FRLG mart is
  the same shop.c family — the FR agent's mart rows will cover the shape; EM photo still owed.)
- **E13/E14 player-PC item storage & mailbox, E15 Hall of Fame PC, E16 Lanette dialog** —
  the E11 menu that leads to all four IS captured; the sub-screens weren't entered (storage
  exit tangle ate the Center time budget). All are list/dialog shapes.
- **H1–H6 contests + blender, D12 Lilycove Lady, I7 lottery, D2–D11 frontier facility
  screens, E17 relearner, E18 daycare, J6 in-game trade, B10–B22 field misc** — reachable
  per the save but not visited this pass; entry paths in CATALOG.md.
- **J13 Mystery Event** — NOT on this save's main menu (only MYSTERY GIFT shows) →
  unreachable-with-this-save until the Petalburg questionnaire enables it.
- **A6/A7 boot combos (clear-save / RTC reset)** — need CHORDED holds; the D4 channel is
  single-key-per-token by design. Flagged for a CTM-movie-only capture if ever needed.
