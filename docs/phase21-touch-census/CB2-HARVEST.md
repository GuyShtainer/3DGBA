# CB2 harvest — live-confirmed screen fingerprints (phase 21 visit pass)

Method: `gMain.callback2` read live from the emulated game via the app's own gs-logger
statics (`s_lastCb2[2]` in gamestate.c, gdb channel), Thumb bit stripped, resolved against
the pret byte-matched symbol maps. `[exact]` = the address IS the symbol's address.
Static-symbol collisions (`MainCB2`, `MainCB2_Intro` etc. appear once per module) were
disambiguated by sym-file neighborhood (the surrounding symbols identify the module) —
noted per row. **Every row below was resolved on a LIVE system this session (2026-08-13,
Azahar, staged endgame saves); none are guesses.**

## Emerald (BPEE) — screen 0 of the census boot, `pokeemerald.sym` (exact-matched)

| Catalog row | Screen | cb2 (thumb-stripped) | Symbol | Module (neighborhood) |
|---|---|---|---|---|
| A1 | GF intro movie | 0x0816CC00 | `MainCB2_Intro` | intro.c |
| A1b | intro→title handoff | 0x080AAB2C | `MainCB2` | title_screen.c (next to `CB2_InitTitleScreen` 0x080AA7A4) — the TITLE SCREEN main loop |
| A3 | Main menu (Continue/New/…) | 0x0802F6B0 | `CB2_MainMenu` | main_menu.c |
| A5 | Options menu | 0x080BA4B0 | `MainCB2` | option_menu.c (next to `CB2_InitOptionMenu` 0x080BA4DC) |
| B1 | Overworld | 0x08085E5C | `CB2_Overworld` | overworld.c |
| B2/B4/B5 | Start menu / yes-no / save dialog | 0x08085E5C | `CB2_Overworld` | (window tasks over overworld — cb2 unchanged) |
| B6 | Trainer card (front+back) | 0x080C2710 | `CB2_TrainerCard` | trainer_card.c |
| B8 | Fly map | 0x081248D4 | `CB2_FlyMap` | region_map.c |
| D1 | Frontier Pass (+map feature) | 0x080C5438 | `CB2_FrontierPass` | frontier_pass.c (both the pass main and its zoomed map feature) |
| E1 | Summary screen (all 4 pages) | 0x081BFAB4 | `MainCB2` | pokemon_summary_screen.c (next to `CB2_InitSummaryScreen` 0x081BFAE4) |
| C22 | Forget-move summary hop | 0x081BFAB4 | `MainCB2` | pokemon_summary_screen.c — SAME loop as E1 (mode is internal state) |
| E2 | Party menu (all modes: field, HM-choose, sub-menus) | 0x081B01B0 | `CB2_UpdatePartyMenu` | party_menu.c |
| E3 | Bag (all 5 pockets, item sub-menus, HM-use flow) | 0x081AAD5C | `CB2_BagMenuRun` | item_menu.c |
| E6 | Berry tag screen | 0x08177C54 | `CB2_BerryTagScreen` | berry_tag_screen.c |
| F1–F5 | Pokedex (list, entry, area, cry, size, search) | 0x080BB774 | `CB2_Pokedex` | pokedex.c — ONE cb2 for the whole dex; sub-screens are task-driven |
| G1–G5 | PokeNav (main, map, condition, match call, ribbons) | 0x081C7400 | `CB2_Pokenav` | pokenav.c — ONE cb2, sub-apps internal (catalog gap #2 confirmed live) |
| H7 | Pokeblock case | 0x0813591C | `CB2_PokeblockMenu` | pokeblock.c |
| H9 | Use-pokeblock condition screen | 0x0816631C | `CB2_UsePokeblockMenu` | use_pokeblock.c |
| H8 | Pokeblock feed scene | 0x08179B68 | `CB2_PokeblockFeed` | pokeblock_feed.c |

## FireRed rev1 (BPRE r1) — screen 1 incidental confirmations, `pokefirered_rev1.sym`

(The FR visit agent owns FR; these fell out of the dual boot for free.)

| Screen | cb2 | Symbol |
|---|---|---|
| GF intro | 0x080EC9E8 | `CB2_Intro` [exact] |
| Copyright re-init (intro loop) | 0x080EC878 | `CB2_InitCopyrightScreenAfterTitleScreen` [exact] |
| Title screen | 0x08078BB0 | `CB2_TitleScreenRun` [exact] |

Notes for the tilt/presence promotion table:
- `pokeemerald.sym` and `pokefirered_rev1.sym` are the CORRECT maps for the user's carts —
  every resolution above landed [exact] on a symbol boundary; zero near-misses.
- Sub-screen discrimination inside CB2_Pokedex / CB2_Pokenav / summary / party needs task-fp
  or internal-state reads (the gs ring already captures `taskFp[8]` per edge — the promotion
  data exists in the netlog gs dumps banked this session).

## Emerald — boot #2 additions (all [exact] on pokeemerald.sym, live-read)

| Catalog row | Screen | cb2 | Symbol | Module |
|---|---|---|---|---|
| C1–C22 core | Interactive battle (all input screens) | 0x08038420 | `BattleMainCB2` | battle_main.c — the ONE battle cb2; C2/C3/C4 discrimination is the existing GCTX task/pointer matching (already positive) |
| B7 | Field region map (wall map) | 0x08170274 | `MCB2_FieldUpdateRegionMap` | field_region_map.c — **catalog correction**: `CB2_FieldShowRegionMap` is only the setup; THIS is the run loop |
| B8 | Fly map | 0x081248D4 | `CB2_FlyMap` | region_map.c |
| B9 | Wall clock (view) | 0x08134C9C | `CB2_WallClock` | wallclock.c — shared run loop for set & view modes |
| E10 | Naming screen | 0x080E4F58 | `CB2_NamingScreen` | naming_screen.c |
| E12 | Storage boxes (all sub-states: mon menu, hold, box menu, jump, party strip) | 0x080C7D54 | `CB2_PokeStorage` | pokemon_storage_system.c |
| I4 | Slot machine | 0x0812A670 | `CB2_SlotMachine` | slot_machine.c |
| A2 | Title screen | 0x080AAB2C | `MainCB2` | title_screen.c (photo + cb2 both banked this boot) |
| J12 | Mystery Gift shell (no adapter) | 0x0802F6B0 | `CB2_MainMenu` | the adapter refusal happens under the main-menu cb2 |

Promotion-table note: `s_lastCb2`/`s_lastCtx`/`s_gsLog` gdb reads + pret-sym resolution
proved a ZERO-GUESS harvest pipeline — every promoted address in this file was read from
the live game. The gs ring additionally has `taskFp[8]` per edge in the harvested netlogs
(runs/20260813-*/netlogs/) for the phase-22 sub-state discrimination work (Pokedex pages,
PokeNav apps, summary modes, storage modes).

## FireRed rev1 (BPRE r1) — FR visit pass, all [exact] on `pokefirered_rev1.sym`, live-read

| Catalog row | Screen | cb2 | Symbol | Module / note |
|---|---|---|---|---|
| A1 | GF intro | 0x080EC9E8 | `CB2_Intro` | intro.c |
| A2 | Title screen | 0x08078BB0 | `CB2_TitleScreenRun` | title_screen.c |
| A3 | Main menu | 0x0800C2E8 | `CB2_MainMenu` | main_menu.c |
| A4 | New-game Oak speech | 0x0812EB88 | `CB2_NewGameScene` | oak_speech.c |
| A5 | Options (start-menu route) | 0x08088370 | `CB2_InitOptionMenu` | option_menu.c — stays as the RUN loop cb2 (FR keeps the init symbol) |
| B1/K4 | Overworld AND Quest Log playback | 0x080565C8 | `CB2_Overworld` | overworld.c — K4 playback is cb2-INVISIBLE (needs a quest-log state flag for detection) |
| B5/B2 | Start menu / save dialog | 0x080565C8 | `CB2_Overworld` | window tasks over overworld |
| B6 | Trainer card | 0x08089084 | `CB2_TrainerCard` | trainer_card.c |
| B7 | Town Map | 0x080C08C8 | `CB2_RegionMap` | region_map.c — catalog's `CB2_OpenRegionMap` is setup only |
| C1-C22 core | Battle main loop | **0x08011114** | `BattleMainCB2` | battle_main.c — **rev1 moved it; profile has rev0 0x08011100 → FR battle detection dead** |
| C14 | Pokedude demo battle | 0x08011114 | `BattleMainCB2` | pokedude discrimination = internal controller fns, not cb2 |
| E1/C22 | Summary (all pages + forget-move + detail mode) | 0x08137F60 | `CB2_RunPokemonSummaryScreen` | pokemon_summary_screen.c |
| E2/E2d/D7 | Party (field, sub-menus, TM-choose) | 0x0811EC18 | `CB2_UpdatePartyMenu` | party_menu.c (rev0: 0x0811EBA0 — profile value!) |
| E3 | Bag (3 pockets + sub-menus) | 0x08107F58 | `CB2_BagMenuRun` | item_menu.c (rev0: 0x08107EE0 — profile value!) |
| E4 | Berry Pouch | 0x0813CE78 | `CB2_BerryPouchIdle` | berry_pouch.c |
| E5 | TM Case | 0x081318DC | `CB2_Idle` | tm_case.c — **catalog gap #4 correction: CB2_Idle IS the TM Case run loop, not unused** |
| E7 | Pokemon Special Anim | 0x0811C774 | `CB2_PSA` | pokemon_special_anim.c |
| F6 | Pokedex (TOC, lists, entry, area+size page, habitat grids) | 0x0810254C | `CB2_PokedexScreen` | pokedex_screen.c — ONE cb2 for the whole FR dex |
| K1 | Teachy TV | 0x0815AC0C | `TeachyTvCallback` | teachy_tv.c (sym name differs from catalog's "TeachyTvMainCallback") |
| K2 | Fame Checker | 0x0812C40C | `MainCB2_FameCheckerMain` | fame_checker.c |

**Rev0→rev1 anchor drift table (the phase-22 fix list, all verified against both sym files):**
`BattleMainCB2` 0x08011100→0x08011114 · `Task_HandleChooseMonInput` 0x0811FB28→0x0811FBA0 ·
`CB2_BagMenuRun` 0x08107EE0→0x08107F58 · `Task_StartMenuHandleInput` 0x0806F1F0→0x0806F204 ·
`Task_HandleSelectionMenuInput` 0x08122C5C→0x08122CD4 · `CB2_UpdatePartyMenu` 0x0811EBA0→0x0811EC18.
RAM-space anchors (gMain, saveblocks, gTasks 0x03005090) are rev-identical (live-verified: the
gs logger reads coordinates fine on rev1 with the rev0-derived RAM addresses).

## FireRed rev1 — final-leg additions (all [exact] on `pokefirered_rev1.sym`, live-read)

| Catalog row | Screen | cb2 | Symbol | Module |
|---|---|---|---|---|
| E11/E12 | PC storage boxes (all sub-states) | 0x0808CDD8 | `CB2_PokeStorage` | pokemon_storage_system |
| E10 | Naming screen (box name, both pages) | 0x0809FB84 | `CB2_NamingScreen` | naming_screen.c |
| E15 | Hall of Fame PC replay | 0x080F1E38 | `CB2_HofIdle` | hof_pc.c |
| I1 | Mart buy menu (+qty picker) | 0x0809ADF8 | `CB2_BuyMenu` | shop.c |
| I4 | Slot machine | 0x0813F9C4 | `CB2_RunSlotMachine` | slot_machine.c |
| B7/B8 | Town map AND fly map (one loop, mode internal) | 0x080C08C8 | `CB2_RegionMap` | region_map.c |
| (transition) | map-load transition (caught entering the prize room) | 0x08056760 | `CB2_LoadMap2` | overworld.c — transitions land here between CB2_Overworld sessions |
| C7 | Safari battle | 0x08011114 | `BattleMainCB2` | battle controller = safari (verbs internal, same cb2) |
