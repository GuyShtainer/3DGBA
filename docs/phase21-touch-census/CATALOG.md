# Phase 21 — Gen-3 Screen Census (CATALOG)

**Purpose.** The definitive list of every screen a player can be on across the five supported
games — Ruby (AXVE rev2), Sapphire (AXPE rev2), Emerald (BPEE), FireRed (BPRE rev1), LeafGreen
(BPGE rev1) — as the ground truth for the smart-touch master plan. This file is *research-derived*
(pret decomps + byte-matched symbol maps); the emulator visit pass photographs each reachable row
and confirms its CB2 live.

**Method / provenance.**
- Module authority: `pret/pokeemerald` `src/` (315 modules; full local clone at
  `gba-toolkit/projects/PokeDNA/daycare map/pokeemerald/`) and `pret/pokefirered` `src/`
  (289 modules; listed via GitHub API 2026-08-13). RS deltas from `pret/pokeruby` `src/` +
  symbol files.
- Main-callback authority: symbol files fetched 2026-08-13 to the session scratchpad —
  `pokeemerald.sym` (249 `CB2_*` symbols), `pokefirered.sym` / `pokefirered_rev1.sym`
  (182 `CB2_*`), `pokeruby.sym` (87 `CB2_*`).
- **Identification technique (per visited screen):** read `gMain.callback2`
  (EM `0x030022C4`, FR `0x030030F4` — the verified profile rows in `source/gamestate.c`),
  mask the Thumb bit, resolve against the matching symbol file (user's FR is **rev1**;
  user's R/S carts are **rev2** — use the per-rev sym files, never the rev0 map). A screen
  identified this way is CERTAIN. Never promote a guessed address.
- Reachability is judged against the **staged endgame saves** (see BUILDLOG session note:
  Emerald = everything unlocked incl. Frontier/contests/pokenav; FireRed = completed;
  Ruby ~600h completed).

**Reachability legend:** `yes` = reachable with the staged saves, single console ·
`new-game` = only on a fresh file (would trash the save slot — use a scratch .sav copy) ·
`2P` = needs a second console/link (the project's own wireless link, or defer) ·
`event` = needs distribution/e-reader-era data · `n/a` = not really a player screen.

**Interaction shapes:** `list` (vertical cursor list), `grid` (2-D cursor), `keyboard`
(character/easy-chat matrix), `map` (spatial cursor), `dialog` (cursorless textbox; tap = A),
`yesno/multi` (small popup list), `minigame` (real-time), `cutscene` (watch; tap = advance/skip),
`hybrid` (noted).

Current touch coverage (the audit baseline): `GCTX_OVERWORLD` (tap-to-walk BFS + door
terminals), `GCTX_BATTLE_ACTION/MOVE/TARGET`, `GCTX_PARTY` (in-battle), `GCTX_FIELDMENU`
(start menu / yes-no / multichoice), `GCTX_BAG`, `GCTX_BATTLE_OTHER` (tap=A). **Everything
else in this catalog currently falls through to the GCTX_OVERWORLD residual** — that is the
gap this census exists to close.

---

## A. Boot, title, file select

| # | Screen | Games | Entry path | pret module + main CB2 | Shape | Reach |
|---|--------|-------|-----------|------------------------|-------|-------|
| A1 | GameFreak/copyright intro movie | all | power-on | `intro.c` — EM `CB2_InitCopyrightScreenAfterBootup`→`MainCB2_Intro`; FR `CB2_Intro`/`CB2_SetUpIntro`; RS `CB2_InitCopyrightScreenAfterBootup` | cutscene | yes |
| A2 | Title screen | all | after intro / soft-reset | `title_screen.c` — EM/RS `CB2_InitTitleScreen`; FR `CB2_InitTitleScreen`→`CB2_TitleScreenRun` | dialog (Start) | yes |
| A3 | Main menu (Continue / New Game / Mystery Gift/Event / Options) | all | title → Start | `main_menu.c` — `CB2_InitMainMenu`→`CB2_MainMenu` (FR also `CB2_InitMainMenu_2`) | list | yes |
| A4 | New-game intro (Birch / Oak speech, gender+name) | all | main menu → New Game | EM/RS `main_menu.c` (Birch tasks under `CB2_MainMenu`→`CB2_NewGameBirchSpeech_*`); FR `oak_speech.c` (`CB2_NewGameScene`) | dialog + yesno + keyboard hop | new-game |
| A5 | Options menu | all | main menu or start menu → Option | `option_menu.c` — `CB2_InitOptionMenu` (FR also `CB2_OptionMenu`, `CB2_OptionsMenuFromStartMenu`) | list (l/r sliders) | yes |
| A6 | Clear-save screen (Up+B+Select) | all | boot combo | `clear_save_data_screen.c` — EM `CB2_InitClearSaveDataScreen`; FR `CB2_RunClearSaveDataScreen`/`CB2_SaveClearScreen_Init` | yesno | yes (don't confirm!) |
| A7 | Reset RTC screen (Select+B+Left at title) | RSE | boot combo | `reset_rtc_screen.c` — `CB2_InitResetRtcScreen`→`CB2_ResetRtcScreen` | grid (digits) | yes |
| A8 | Berry-fix / multiboot program screen | RSE hosts it; FRLG `berry_fix` | cable-linked RS berry glitch fix | `berry_fix_program.c` — `CB2_InitBerryFixProgram` | dialog | 2P |
| A9 | Save-failed screen | all | flash write failure | `save_failed_screen.c` — `CB2_SaveFailedScreen` | dialog | n/a (fault path) |
| A10 | Link error screen | all | link desync/timeout | `link.c` — `CB2_LinkError`→`CB2_PrintErrorMessage` | dialog | 2P (fault path) |

## B. Overworld & field layer

| # | Screen | Games | Entry path | pret module + main CB2 | Shape | Reach |
|---|--------|-------|-----------|------------------------|-------|-------|
| B1 | Overworld (walk/bike/surf/dive) | all | continue | `overworld.c` — `CB2_Overworld` (+`CB2_LoadMap`, `CB2_ReturnToField*` transitions) | map (tap-to-walk) | yes |
| B2 | Start menu | all | START | `start_menu.c` — task over `CB2_Overworld` (`Task_ShowStartMenu`) | list | yes |
| B3 | Script dialog / textboxes | all | talk to anything | `field_message_box.c` over `CB2_Overworld` | dialog | yes |
| B4 | Yes/No + script multichoice popups | all | scripts everywhere | `script_menu.c` over `CB2_Overworld` | yesno/multi | yes |
| B5 | Save dialog (start menu → Save) | all | start menu | `start_menu.c` tasks over `CB2_Overworld` | yesno + dialog | yes |
| B6 | Trainer card (front/back flip) | all | start menu → card; link lobbies | `trainer_card.c` — `CB2_InitTrainerCard`→`CB2_TrainerCard` (`CB2_ShowCard` variants) | dialog (A flips) | yes |
| B7 | Region map viewer (field, no fly) | all | wall maps / PokeNav-less check | EM `field_region_map.c` `CB2_FieldShowRegionMap`; FR `region_map.c` `CB2_OpenRegionMap`→`CB2_RegionMap`; RS `CB2_FieldInitRegionMap` | map (cursor pan) | yes |
| B8 | Fly map | all | party/HM Fly or item | `region_map.c` — `CB2_OpenFlyMap`→`CB2_FlyMap` (RS `CB2_InitFlyRegionMap`→`CB2_FlyRegionMap`) | map (pick landing) | yes |
| B9 | Wall clock (set/view) | RSE | new-game bedroom; wall clock in home | `wallclock.c` — `CB2_StartWallClock` / `CB2_ViewWallClock` | grid (analog hands) | yes (view); set = new-game |
| B10 | Itemfinder sweep | all | bag → Itemfinder | EM `item_use.c`, FR `itemfinder.c` — task over `CB2_Overworld` | map feedback | yes |
| B11 | Fishing minigame | all | rod use | `field_player_avatar.c` tasks over `CB2_Overworld` | dialog (timed A) | yes |
| B12 | Safari Zone gating (steps/balls HUD + exit prompt) | all (RSE Hoenn, FRLG Kanto) | Safari entrance | `safari_zone.c` over `CB2_Overworld` (+ safari battle C7) | dialog | yes |
| B13 | Cable car ride | RSE | Rusturf tunnel-side station | `cable_car.c` — `CB2_LoadCableCar`→`CB2_CableCar` | cutscene | yes |
| B14 | Seagallop ferry ride | FRLG | Vermilion/One–Seven Is. piers | `seagallop.c` — `CB2_SetUpSeagallopScene`→`MainCB2_SeaGallop` | cutscene | yes |
| B15 | Braille wall interactions | RSE (FRLG Dotted Hole) | Sealed Chamber etc. | `braille_puzzles.c` (RSE), FR `braille_text.c` — dialog over overworld | dialog | yes |
| B16 | Secret base (own + others') | RSE | Secret Power on spots | `secret_base.c` over `CB2_Overworld` | map | yes |
| B17 | Decoration place/tidy (base + bedroom) | RSE | Player PC → Decoration | `decoration.c` — list menus + placement grid over overworld | list + grid | yes |
| B18 | Map-section name popup / map preview | FR preview: dungeon entry; all: name popup | walk into areas | `map_name_popup.c`; FR `map_preview_screen.c` | n/a (passive) | yes |
| B19 | Whiteout / warp-to-heal transition | all | faint everything | `overworld.c` — `CB2_WhiteOut` | cutscene | yes |
| B20 | Field HM/field-move prompts (Cut/Surf/Strength/Flash/Dig/Teleport/Rock Smash/Waterfall/Dive/Sweet Scent/Softboiled) | all | party menu → move, or A-prompts | `fldeff_*.c`, `field_effect.c` over overworld | yesno | yes |
| B21 | Rotating gate / rotating tile puzzles, Mirage Tower, trick house | RSE | Fortree/Mossdeep gym, Route 111, Route 110 | `rotating_gate.c`, `rotating_tile_puzzle.c`, `mirage_tower.c` — overworld mechanics | map | yes |
| B22 | Amusement: Dewford Hall trends / quiz walls | RSE | Dewford Hall NPCs | `dewford_trend.c` + easy chat (D8) | dialog→keyboard | yes |

## C. Battle

All battle UIs run under the battle main loop (`battle_main.c`: `CB2_InitBattle`→
`CB2_InitBattleInternal`→`BattleMainCB2`, ends via the `CB2_End*Battle` family). The *variant*
changes contents, not the four core input screens (C2–C5) — which are already GCTX-covered.

| # | Screen | Games | Entry path | pret module + main CB2 | Shape | Reach |
|---|--------|-------|-----------|------------------------|-------|-------|
| C1 | Battle transition/intro swirl | all | any encounter | `battle_transition.c` (+`battle_transition_frontier.c` EM) | cutscene | yes |
| C2 | Action select (Fight/Bag/Pokemon/Run) | all | in battle | `battle_controller_player.c` (`HandleInputChooseAction`) | grid 2×2 | yes |
| C3 | Move select | all | Fight | `battle_controller_player.c` (`HandleInputChooseMove`) | grid 2×2 | yes |
| C4 | Target select (doubles) | all | move in double battle | `battle_controller_player.c` (`HandleInputChooseTarget`) | grid (mons) | yes |
| C5 | Battle dialog / animations / level-up box | all | in battle | `battle_message.c`, `battle_script_commands.c` | dialog | yes |
| C6 | Wild battle (single) | all | grass etc. | `CB2_EndWildBattle` (exit) | — | yes |
| C7 | Safari battle (Ball/Bait/Rock/Run) | all | Safari Zone | `battle_controller_safari.c`, `CB2_EndSafariBattle` | grid 2×2 (different verbs) | yes |
| C8 | Trainer battle (single/rematch) | all | trainers, Vs Seeker (FR) | `battle_setup.c`, `CB2_EndTrainerBattle`/`CB2_EndRematchBattle` | — | yes |
| C9 | Double battle (incl. 2-on-1 trainers) | all | double trainers, tag NPCs (EM) | same core + C4 | — | yes |
| C10 | Multi battle (2v2 with NPC partner: Steven Mossdeep; multi link) | EM (+link all) | scripted / link | `CB2_HandleStartMultiPartnerBattle`, `CB2_PreInitIngamePlayerPartnerBattle` | — | yes (EM scripted) / 2P |
| C11 | Link battle (cable/wireless, Union Room) | all | cable club / union room | `CB2_HandleStartBattle` link path, `CB2_EndLinkBattle`, `CB2_UnionRoomBattle` | — | 2P |
| C12 | Recorded battle playback + "record this battle?" ask | EM | after link/frontier battle; Battle TV/Vs Recorder equiv | `recorded_battle.c` — `CB2_RecordedBattle(End)`, `CB2_InitAskRecordBattle`, `battle_tv.c` | cutscene + yesno | 2P-ish (needs a recorded battle) |
| C13 | Old-man / Wally catch tutorial | FRLG (old man), RSE (Wally) | new-game story beat | FR `battle_controller_oak_old_man.c`; RSE scripted | cutscene | new-game |
| C14 | Pokedude demo battles | FRLG | Teachy TV segments | `battle_controller_pokedude.c`, `CB2_QuitPokedudeBattle` | cutscene | yes |
| C15 | Ghost Marowak battle | FRLG | Pokemon Tower (story) | `CB2_EndMarowakBattle` | — | done in save (repeat n/a) |
| C16 | Roamer / legendary / scripted wild (Rayquaza first battle etc.) | all | statics + roamers | `CB2_EndScriptedWildBattle`, `CB2_StartFirstBattle`/`CB2_EndFirstBattle` (first battle = new-game) | — | yes |
| C17 | Trainer Tower battles | FRLG | Sevii Trainer Tower | `trainer_tower.c` + battle core, `CB2_EndTrainerTowerBattle` | — | yes |
| C18 | Battle Tower / Frontier-facility battles | RS Tower; EM 7 facilities | facility lobbies | `battle_tower.c` + per-facility modules (D-section) | — | yes |
| C19 | E-reader battle (trainer from card) | JP/e-reader era | e-reader hardware | `ereader_helpers.c`, FR `CB2_FinishEReaderBattle` | — | event |
| C20 | Evolution scene (+ trade evolution) | all | level-up/stone/trade | `evolution_scene.c` — `CB2_EvolutionSceneUpdate`, `CB2_TradeEvolutionSceneUpdate` | cutscene (B cancels) | yes |
| C21 | Egg hatch scene | all | walk with ready egg | `egg_hatch.c` — EM `CB2_LoadEggHatch`; FR/RS `CB2_EggHatch_0/1` | cutscene + naming hop | yes |
| C22 | Level-up move learn / forget-move summary hop | all | 5th move | `learn_move` flow → `CB2_ShowSummaryScreenToForgetMove` | list (summary E1) | yes |

## D. Emerald Battle Frontier & friends (EM-only unless noted)

| # | Screen | Games | Entry path | pret module + main CB2 | Shape | Reach |
|---|--------|-------|-----------|------------------------|-------|-------|
| D1 | Frontier Pass (map/badges/record) | EM | START in frontier / pass item | `frontier_pass.c` — `CB2_InitFrontierPass`→`CB2_FrontierPass`, `CB2_ShowFrontierPassFeature` | grid + map | yes |
| D2 | Battle Factory rental select | EM (+ Slateport Battle Tent) | Factory challenge | `battle_factory_screen.c` — `CB2_InitSelectScreen`→`CB2_SelectScreen` | grid (6 rentals) | yes |
| D3 | Battle Factory swap screen | EM | after Factory win | `battle_factory_screen.c` — `CB2_InitSwapScreen` | grid | yes |
| D4 | Battle Dome tourney tree | EM | Dome challenge | `battle_dome.c` — `CB2_TourneyTree` | map (bracket) | yes |
| D5 | Battle Pyramid bag | EM | START inside Pyramid | `battle_pyramid_bag.c` — `CB2_PyramidBag(MenuFromStartMenu)` | list | yes |
| D6 | Battle Pike / Palace / Arena / Tower lobbies + halls | EM (Tower also RS) | facility lobbies | `battle_pike.c`, `battle_palace.c`, `battle_arena.c`, `battle_tower.c` — overworld + battle core | map + dialog | yes |
| D7 | "Choose Frontier party" (half-party select) | EM (+RS tower, FR tower) | facility signup | `party_menu.c` — `CB2_ReturnFromChooseBattleFrontierParty` / FR `...BattleTowerParty` / `...ChooseHalfParty` | list | yes |
| D8 | Easy-chat screen (all uses: mail, trends, quiz, profile, Walda, Wonder News wishes, apprentice) | all (uses differ) | many NPC flows | `easy_chat.c` — `CB2_EasyChatScreen` (FR `easy_chat*.c`) | keyboard (word matrix) | yes |
| D9 | Walda phrase (wallpaper code) | EM | Rustboro toddler flow | `walda_phrase.c` — `CB2_HandleGivenWaldaPhrase` (via D8) | keyboard | yes |
| D10 | Trainer Hill lobby + records | EM | Trainer Hill (post-game) | `trainer_hill.c` — `MainCB2_TrainerHillRecords` (records); battles = C18 | dialog + list | yes |
| D11 | Apprentice flows | EM | Battle Tower lobby NPC | `apprentice.c` — dialogs + D8 + party picks | dialog | yes |
| D12 | Lilycove Lady (Quiz/Favor/Contest lady) | EM | Lilycove Contest Hall lobby | `lilycove_lady.c` — dialog + bag hops (`CB2_QuizLadyQuestion`, `CB2_FavorLadyExitBagMenu`) | dialog + list | yes |
| D13 | Rayquaza awakening scene | EM | Sootopolis story | `rayquaza_scene.c` — `CB2_InitRayquazaScene` | cutscene | done in save |
| D14 | Cable car / Mauville trader / Bard song etc. | RSE | misc NPCs | `trader.c`, `mauville_old_man.c` + D8 | dialog | yes |

## E. Core menus (bag, party, summary, mail, PC)

| # | Screen | Games | Entry path | pret module + main CB2 | Shape | Reach |
|---|--------|-------|-----------|------------------------|-------|-------|
| E1 | Summary screen (Info/Skills/Moves + RSE Contest page + ribbons) | all | party/box → Summary | `pokemon_summary_screen.c` — `CB2_InitSummaryScreen`→`CB2_ShowPokemonSummaryScreen` (FR `CB2_RunPokemonSummaryScreen`) | list + page L/R | yes |
| E2 | Party menu (field; give/use/switch; choose-mon flows for trade/contest/tutor/minigames) | all | start menu / prompts | `party_menu.c` — `CB2_InitPartyMenu`→`CB2_UpdatePartyMenu` + the `CB2_Choose*`/`CB2_ReturnToPartyMenu*` family | grid (6 slots) + sub-list | yes |
| E3 | Bag (field): 3 pockets FRLG / 5 pockets RSE, sort, register, quantity pickers | all | start menu → Bag | EM `item_menu.c` — `CB2_Bag*` family; FR `bag.c`/`item_menu.c` — `CB2_OpenBagMenu`, `CB2_BagMenuRun` | list + pocket tabs + qty | yes |
| E4 | Berry Pouch | FRLG | bag → Berry Pouch item | `berry_pouch.c` — `CB2_InitBerryPouch`→`CB2_BerryPouchIdle` | list | yes |
| E5 | TM Case | FRLG | bag → TM Case item | `tm_case.c` — `CB2_SetUpTMCaseUI_Blocking` (+`CB2_UseTMHMAfterForgettingMove`) | list | yes |
| E6 | Berry tag screen (berry info/flavor) | RSE only (no FRLG equivalent — confirmed: zero BerryTag symbols in pokefirered.sym) | bag Berries pocket → Check Tag | `berry_tag_screen.c` — `CB2_InitBerryTagScreen`→`CB2_BerryTagScreen` | dialog + L/R flip | yes |
| E7 | Item-use special anim (FR "Pokemon Special Anim": rare candy/stone/etc.) | FRLG | using items on mons | `pokemon_special_anim*.c` — `CB2_PSA`, `CB2_UseItem`, `CB2_UseEvolutionStone` | cutscene | yes |
| E8 | Mail read (held / mailbox) | all | mail flows | `mail.c` — `CB2_InitMailRead`/`CB2_MailRead`; FR `CB2_InitMailView`, `CB2_ReadHeldMail` | dialog | yes |
| E9 | Mail compose | all | give mail item | easy chat (D8) via `CB2_WriteMailToGiveMon(FromBag)` | keyboard | yes |
| E10 | Naming screen (player/rival(FR)/mon/box/Walda) | all | many flows | `naming_screen.c` — `CB2_LoadNamingScreen`→`CB2_NamingScreen` | keyboard | yes |
| E11 | PC top menu ("Someone's PC…") | all | PC in centers/home | script multichoice over overworld (B4) | multi | yes |
| E12 | Pokemon Storage System (boxes: move/deposit/withdraw/item mode, box jump, wallpaper) | all | PC → Bill's/Someone's PC | `pokemon_storage_system*.c` — `CB2_PokeStorage` (+`CB2_(Exit|ReturnTo)PokeStorage`) | grid 6×5 + hand cursor | yes |
| E13 | Player PC item storage (withdraw/deposit/toss) | all | bedroom/player PC | EM `player_pc.c` (`CB2_PlayerPCExitBagMenu`, `CB2_GoToItemDepositMenu`); FR `item_pc.c` + `player_pc.c` | list | yes |
| E14 | Mailbox (PC) | all | player PC → Mailbox | EM `player_pc.c`; FR `mailbox_pc.c` — `CB2_ReturnToMailbox*` | list | yes |
| E15 | Hall of Fame PC (replay inductions) | all | PC → Hall of Fame | `hof_pc.c` — EM `CB2_DoHallOfFamePC`; FR `CB2_InitHofPC`→`CB2_HofIdle` | cutscene/list | yes (E4 beaten) |
| E16 | Lanette's PC / Prof. Oak's PC rating | all | PC options | dialog (B3); FR `prof_pc.c` | dialog | yes |
| E17 | Move relearner / tutor / deleter | all | Fallarbor (RS/EM), Two Island (FR) etc. | EM `move_relearner.c` `CB2_InitLearnMove`→`CB2_MoveRelearnerMain`; FR `learn_move.c` `CB2_MoveRelearner`; RS `CB2_MoveTutorMenu`; deleter/tutors = dialog+party | list (moves) + yesno | yes |
| E18 | Daycare (drop/pick up, compatibility) | all | Route 117 / Four Island / Kanto daycare | `daycare.c` — dialogs + party hops | dialog | yes |
| E19 | Battle records (link results table) | all | cable-club PC area / Vs Seeker room | `battle_records.c` — window over overworld | dialog | yes |
| E20 | Diploma | all | complete dex, Game Freak building | `diploma.c` — `CB2_ShowDiploma` (FR `CB2_Diploma`) | dialog | yes (if dex done) |

## F. Pokedex

| # | Screen | Games | Entry path | pret module + main CB2 | Shape | Reach |
|---|--------|-------|-----------|------------------------|-------|-------|
| F1 | Pokedex list (Hoenn/National, sort/search results) | RSE | start menu → Pokedex | `pokedex.c` — `CB2_OpenPokedex`→`CB2_Pokedex` (`Task_HandlePokedexInput`) | list + D-pad page | yes |
| F2 | Pokedex entry/info page | RSE | F1 → A | `pokedex.c` (`Task_OpenPokedexMainPage`) | dialog + page | yes |
| F3 | Pokedex area screen | RSE | entry → Area | `pokedex_area_screen.c` (+`pokedex_area_region_map.c`) — `Task_HandlePokedexAreaScreenInput` | map | yes |
| F4 | Pokedex cry screen | RSE | entry → Cry | `pokedex_cry_screen.c` — `Task_HandleCryScreenInput` | grid (meter) | yes |
| F5 | Pokedex search (RSE search UI) | RSE | dex → Select/search | `pokedex.c` (`Task_StartPokedexSearch`) | grid (criteria) | yes |
| F6 | FRLG Pokedex (habitat/category pages, Kanto/National list, entry, area+cry pages) | FRLG | start menu → Pokedex | `pokedex_screen.c` — `CB2_PokedexScreen`→`CB2_ClosePokedex`; area data `wild_pokemon_area.c`, markers `pokedex_area_markers.c` | list + category grid + map | yes |

## G. PokeNav (RSE handheld — one CB2, sub-apps are internal states)

Detection note: everything below runs under EM `CB2_InitPokeNav`→`CB2_Pokenav` — sub-app
identification needs a pokenav-internal state read (a phase-22 address-capture task), the cb2
alone only says "in PokeNav". RS equivalents run under RS pokenav callbacks in `pokenav_*`
(older decomp naming; verify on rev2 syms before use).

| # | Screen | Games | Entry path | pret module + main CB2 | Shape | Reach |
|---|--------|-------|-----------|------------------------|-------|-------|
| G1 | PokeNav main menu | RSE | start menu → PokeNav | `pokenav_main_menu.c` / `pokenav_menu_handler*.c` under `CB2_Pokenav` | list (ring) | yes |
| G2 | Hoenn map (+ fly-less browse, landmarks) | RSE | PokeNav → Map | `pokenav_region_map.c` | map | yes |
| G3 | Condition graphs (party/search) | RSE | PokeNav → Condition | `pokenav_conditions*.c` | grid + radar chart | yes |
| G4 | Match Call list + call/options (EM) / Trainer's Eyes (RS) | EM / RS | PokeNav → Match Call | `pokenav_match_call_*.c`, `match_call.c` — `CB2_HandleMatchCallInput` etc. | list + sub-dialog | yes |
| G5 | Ribbons list + ribbon summary | RSE | PokeNav → Ribbons | `pokenav_ribbons_list.c`, `pokenav_ribbons_summary.c` | list + grid | yes |

## H. Contests, Pokeblocks (RSE) & TV

| # | Screen | Games | Entry path | pret module + main CB2 | Shape | Reach |
|---|--------|-------|-----------|------------------------|-------|-------|
| H1 | Choose contest mon | RSE | contest hall signup | `party_menu.c` — `CB2_ChooseContestMon` | grid (party) | yes |
| H2 | Contest intro/lobby (rank select, judging intro) | RSE | Lilycove hall (EM; 4 towns RS) | `contest_util.c`, `contest_link*.c` — overworld scripts → `CB2_StartContest` | dialog + multi | yes |
| H3 | Contest appeals (the 5-round main screen) | RSE | contest start | `contest.c` — `CB2_ContestMain` | grid (move picks) + cutscene rounds | yes |
| H4 | Contest results | RSE | after appeals | `contest_util.c` — `CB2_StartShowContestResults`→`CB2_ShowContestResults` | cutscene | yes |
| H5 | Contest painting (win → museum) | RSE | high score / Lilycove museum | `contest_painting.c` — `CB2_ContestPainting`, `CB2_HoldContestPainting` | cutscene | yes |
| H6 | Berry Blender | RSE | contest-hall blenders | `berry_blender.c` — `CB2_LoadBerryBlender`→`CB2_PlayBlender` (+`CB2_StartBlenderLocal/Link`); berry pick via `CB2_ChooseBerry` | minigame (timed A) | yes (NPC blending) |
| H7 | Pokeblock case | RSE | bag/blender/feed flows | `pokeblock.c` — `CB2_InitPokeblockMenu`/`CB2_OpenPokeblockFromBag`→`CB2_PokeblockMenu` | list | yes |
| H8 | Pokeblock feeding scene | RSE | case → feed | `pokeblock_feed.c` — `CB2_PokeblockFeed` | cutscene | yes |
| H9 | Use-Pokeblock condition screen (enhance sheen/condition) | RSE | case → use on mon | `use_pokeblock.c` — `CB2_UsePokeblockMenu` (+`CB2_ShowUsePokeblockMenuForResults`) | grid + radar chart | yes |
| H10 | BuzzNav/TV programs & interviews | RSE | TVs, reporters | `tv.c` — dialog over overworld | dialog | yes |
| H11 | Link contest lobby/flow | RSE | contest hall link option | `contest_link.c` — link variants of H2–H4 | — | 2P |

## I. Shops, money games

| # | Screen | Games | Entry path | pret module + main CB2 | Shape | Reach |
|---|--------|-------|-----------|------------------------|-------|-------|
| I1 | Mart buy menu | all | clerk → Buy | `shop.c` — `CB2_InitBuyMenu`→`CB2_BuyMenu` (FR + `buy_menu_helpers.c`) | list + qty picker | yes |
| I2 | Mart sell (via bag) | all | clerk → Sell | `shop.c` — `CB2_GoToSellMenu`/`CB2_ExitSellMenu` + bag E3 | list + qty | yes |
| I3 | Decoration/vendor special shops (Slateport market, Lilycove rooftop) | RSE | vendors | `shop.c` decoration path + `decoration.c` | list | yes |
| I4 | Slot machine | all (RSE Mauville; FRLG Celadon) | game corner | `slot_machine.c` — EM `CB2_SlotMachineSetup`→`CB2_SlotMachine`; FR `CB2_InitSlotMachine`→`CB2_RunSlotMachine`; RS `CB2_SlotMachineLoop` | minigame (reel stops) | yes |
| I5 | Roulette | RSE | Mauville game corner | `roulette.c` — `CB2_LoadRoulette`→`CB2_Roulette` | grid (bet board) + minigame | yes |
| I6 | Coin case / coin purchase prompts | all | game corner clerks | dialogs (B3/B4) | dialog | yes |
| I7 | Lottery corner | RSE | Lilycove dept. store | `lottery_corner.c` — dialog | dialog | yes |
| I8 | Game Corner prize exchange | all | prize clerks | dialogs + multi | multi | yes |

## J. Link, wireless, mystery gift

| # | Screen | Games | Entry path | pret module + main CB2 | Shape | Reach |
|---|--------|-------|-----------|------------------------|-------|-------|
| J1 | Cable club / Wireless club lobby flows | all | Pokemon Center 2F | `cable_club.c` — dialogs + `CB2_TransitionToCableClub`, `CB2_ReturnToFieldCableClub` | dialog | 2P |
| J2 | Union Room (walk-around) | FRLG+EM | Center 2F wireless | `union_room.c`, `union_room_player_avatar.c` — overworld variant | map | 2P |
| J3 | Union Room chat | FRLG+EM | union room chat option | `union_room_chat*.c` — `CB2_UnionRoomChatMain` | keyboard | 2P |
| J4 | Trade selection screen (3×2 parties) | all | club/union trade | `trade.c` — `CB2_(Start)CreateTradeMenu`→`CB2_TradeMenu` | grid | 2P (proven live by the celiolink work) |
| J5 | Trade animation + save-and-end | all | after confirm | `trade_scene`/`trade.c` — `CB2_LinkTrade`→`CB2_UpdateLinkTrade`→`CB2_SaveAndEndTrade` (+`CB2_SaveAndEndWirelessTrade` EM) | cutscene | 2P |
| J6 | In-game NPC trade | all | trade NPCs | `trade.c` — `CB2_InitInGameTrade`→`CB2_InGameTrade` | cutscene | yes |
| J7 | Record mixing | RSE (+FRLG record corner) | club/record corner | `record_mixing.c` — dialog + `CB2_ReturnFromRecord` | dialog | 2P |
| J8 | Wireless communication status screen | FRLG+EM | wireless club counter | `wireless_communication_status_screen.c` — `CB2_InitWirelessCommunicationScreen`→`CB2_RunWirelessCommunicationScreen` | dialog (live counts) | yes (screen shows; data needs peers) |
| J9 | Dodrio Berry Picking | FRLG+EM (code) | wireless minigame lobby | `dodrio_berry_picking.c` — `CB2_DodrioGame` | minigame | 2P |
| J10 | Pokemon Jump | FRLG+EM (code) | wireless minigame lobby | `pokemon_jump.c` — `CB2_PokemonJump` | minigame | 2P |
| J11 | Berry Crush | FRLG (EM has the module; venue unverified) | Joyful Game Corner (FR) | `berry_crush.c` — static `MainCB` (berry_crush.c:1033) + `BerryCrush_InitVBlankCB` | minigame | 2P |
| J12 | Mystery Gift menu (wireless/friend) + Wonder Card & Wonder News viewers | FRLG+EM | title main menu (after flag) | `mystery_gift_menu.c`, `mystery_gift.c`, `mystery_gift_view.c`/`mystery_gift_show_card/news.c`, `wonder_news.c` — `CB2_InitMysteryGift`, `CB2_ShowCard` | list + dialog | menu: yes; content: event |
| J13 | Mystery Event menu (RSE serial/e-reader era) | RS+EM | title main menu | `mystery_event_menu.c` — `CB2_InitMysteryEventMenu`→`CB2_MysteryEventMenu` | dialog | event |
| J14 | E-reader screens | all (JP-era) | link to e-reader | `ereader_screen.c` — `CB2_InitEReader`, `CB2_MysteryGiftEReader` | dialog | event |
| J15 | "Play again?" after link battle/trade | all | end of link session | `CB2_CheckPlayAgainLink/Local` (EM), `CB2_ReturnFromCableClubBattle` | yesno | 2P |

## K. FRLG-specific systems

| # | Screen | Games | Entry path | pret module + main CB2 | Shape | Reach |
|---|--------|-------|-----------|------------------------|-------|-------|
| K1 | Teachy TV | FRLG | bag → Teachy TV item | `teachy_tv.c` — `TeachyTvMainCallback` (+`CB2_ReturnToTeachyTV`) | list (chapters) + cutscene | yes |
| K2 | Fame Checker | FRLG | key items → Fame Checker | `fame_checker.c` — `MainCB2_LoadFameChecker`→`MainCB2_FameCheckerMain` | grid (faces) + list (facts) | yes |
| K3 | Vs Seeker sweep | FRLG | key items → Vs Seeker | `vs_seeker.c` — field animation over `CB2_Overworld` | map feedback | yes |
| K4 | Quest Log playback ("Previously on your quest…") | FRLG | continue after eventful session | `quest_log*.c` — `CB2_LoadMapForQLPlayback`, `CB2_SetUpOverworldForQLPlayback(WithWarpExit)`, `CB2_EnterFieldFromQuestLog` | cutscene | yes |
| K5 | Help system overlay (L/R) | FRLG | L/R anywhere in field/menus | `help_system*.c` — IRQ-driven overlay (no CB2 change!) | list | yes |
| K6 | Trainer Tower lobby/records | FRLG | Sevii Is. 7 tower | `trainer_tower.c` — scripts (records = printed dialog) | dialog | yes |
| K7 | SS Anne / intro cutscenes (truck FR-equiv etc.) | all | story | `ss_anne.c` (FR), `field_special_scene.c` | cutscene | done in save |

## L. Endgame & credits

| # | Screen | Games | Entry path | pret module + main CB2 | Shape | Reach |
|---|--------|-------|-----------|------------------------|-------|-------|
| L1 | Hall of Fame induction | all | beat E4 | `hall_of_fame.c` — `CB2_DoHallOfFameScreen(DontSaveData)` | cutscene | yes (rematch E4) |
| L2 | Credits | all | after HoF | `credits.c` — `CB2_StartCreditsSequence` (EM), `CB2_Credits` | cutscene | yes |
| L3 | Starter choose (Birch's bag) | RSE | new-game Route 101 | `starter_choose.c` — `CB2_ChooseStarter` | grid (3 balls) | new-game |

---

## RS vs Emerald deltas (screens absent or different in Ruby/Sapphire)

Confirmed against `pret/pokeruby` src + `pokeruby.sym` (87 CB2s; older decomp naming — RS
addresses MUST come from the rev2 sym files for the user's carts):

- **Absent in RS entirely:** Battle Frontier (all D1–D6 facilities except Battle Tower),
  Battle Tents, Frontier Pass, Trainer Hill, Apprentice, Lilycove Lady, Match Call
  (`match_call.c`), wireless anything (Union Room, union chat, wireless status screen,
  Wonder Card/News wireless Mystery Gift, Dodrio/Jump/Crush minigames), `recorded_battle`
  / Battle TV, Walda phrase, Rayquaza scene, Mirage Tower, Faraway Island. RS Mystery
  *Event* (J13) exists instead of wireless Mystery Gift.
- **Different in RS:** PokeNav has **Trainer's Eyes** instead of Match Call (G4); contests run
  in 4 towns (Verdanturf/Fallarbor/Slateport/Lilycove) not Lilycove-only; move relearner is
  `CB2_MoveTutorMenu` (naming); region-map callbacks are the `CB2_FieldRegionMap` /
  `CB2_FlyRegionMap` family; party menu is `CB2_PartyMenuMain`; slot machine is
  `CB2_SlotMachineLoop`; Pokedex area screen exists (`CB2_UnusedPokedexAreaScreen` name in
  pokeruby is the *unused duplicate* — the live one is task-based as in EM).
- **RS-only leftovers:** `CB2_SoundCheckMenu` (debug, not player-reachable).
- Rev caveat: user's carts are **rev 2** (both). Re-verify every address on
  `pokeruby_rev2`/`pokesapphire_rev2` symbol data before any live probe.

## FR vs LG delta

No screen-level differences — LeafGreen is the same engine build (BPGE) with version-exclusive
species/text. Everything in sections A–C, E–F, I–K applies verbatim; user's LG cart is rev 1,
FR cart rev 1 → `pokefirered_rev1.sym` / `pokeleafgreen_rev1.sym` respectively.

---

## Count & honest gaps

**Distinct catalog rows: 143** (A:10, B:22, C:22, D:14, E:20, F:6, G:5, H:11, I:8, J:15,
K:7, L:3). Discounting battle *variants* that reuse the same four input screens (C6–C19),
passive/fault paths (A9, A10, B18, B19), and pure cutscenes, the number of **touch-distinct
interactive surfaces to design for is ≈ 70** — versus the 8 GCTX contexts covered today.

**Could not fully classify (flagged, not guessed):**
1. **Berry Crush in Emerald** — `berry_crush.c` is compiled into EM, but the documented venue
   is FRLG's Joyful Game Corner; whether EM exposes it to players (vs link-partner-only code)
   is unverified. Photograph only if a 2P session ever lands there.
2. **PokeNav sub-app discrimination** — all of G1–G5 share `CB2_Pokenav`; the internal
   state variable/address for sub-app identification is not yet captured (phase-22 task;
   candidate: the pokenav main struct pointer in `pokenav.c`).
3. **Berry Crush / Dodrio / Jump minigame internals** — real-time input models need per-game
   study before any touch mapping; catalogued as minigame, mapping deferred.
4. **`CB2_LinkTest`, `CB2_UnusedBattleInit`, `CB2_TestBattleTransition`, RS
   `CB2_SoundCheckMenu`, FR `CB2_Idle`/`CB2_unused`** — debug/unused callbacks; excluded
   from the census (not player screens) but listed here so the CB2 inventory reconciles.
5. **Help-system overlay (K5)** — runs *without changing cb2* (IRQ-hooked render); cb2-based
   detection cannot see it. Needs its own flag-address capture if touch should interact.
6. **FRLG old-man/Marowak/story one-shots (C13, C15, K7, D13)** — already consumed by the
   endgame saves; photographable only from a scratch new-game save.
7. **E-reader / Mystery Event content (J13, J14, C19)** — no event data available; menu shells
   are reachable, content screens are not.

**Next (visit pass):** photograph every `yes` row via the emutest harness, capture+resolve the
live cb2 per the method above, and log each confirmation into
`docs/phase21-touch-census/evidence/` + BUILDLOG.
