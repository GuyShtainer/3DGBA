# VISITED — FireRed rev1 (BPRE r1, user's endgame save, staged fixture copy)

Session 2026-08-13, census boots (movie `boot_resume.ctm` = wait 900f → tap A on the app's
resume prompt → 6h idle tail; ALL navigation via the D4 channel `move_p2.txt`, FireRed =
gameB = seat 2 = screen 1 = BOTTOM screen). Identification per row: live `gMain.callback2`
via the gs-logger statics over gdb (`s_lastCb2[1]`), resolved on `pokefirered_rev1.sym`
(every hit [exact] — the rev1 map is byte-right for the user's cart). "Detected" = the
app's live GCTX (`s_lastCtx[1]`). Evidence: `evidence/firered/<id>.{top,bottom}.png`
(bottom = the FR game inside the touch-gamepad skin). Raw log: `CAPTURES-firered.log`.

## ⚠ HEADLINE AUDIT FINDING — the BPRE profile is REV0; the user's cart is REV1

**Every FR ROM-space fingerprint in `source/gamestate.c` BPRE row is a rev0 address, and
rev1 moved the code.** Live-proven this session:

| Anchor | Profile (rev0) | Live/rev1 | Result |
|---|---|---|---|
| `battleMainCb` (BattleMainCB2) | 0x08011100 | **0x08011114** | ALL battle contexts (b.act/b.move/b.tgt/b.oth) dead on FR |
| `Task_HandleChooseMonInput` (partyTask) | 0x0811FB28 | **0x0811FBA0** | party never detected |
| `CB2_BagMenuRun` family (bag anchors) | 0x08107EE0/0x08108F0C | **0x08107F58**/(+0x78) | bag never detected |
| `Task_StartMenuHandleInput` (startMenuTask) | 0x0806F1F0 | **0x0806F204** | start menu never detected |
| `Task_HandleSelectionMenuInput` (selMenuTask) | 0x08122C5C | **0x08122CD4** | popups never detected |

RAM-space anchors (gMain 0x030030F4, saveblock ptrs, coords) are rev-identical — that is
why FR walking/warp/gs-logging works while EVERY FR menu/battle context reads as residual
(`ctx=field`) on the user's cart. The EM agent's positives (fmenu/party/bag/battle) are
all NEGATIVE on FR — confirmed live on: start menu, yes/no save popup, party (field +
TM-choose), party sub-menu popup, bag, wild-battle screens (see rows). **Phase-22 item #1:
re-derive every BPRE ROM anchor from `pokefirered_rev1.sym` (and BPGE from
`pokeleafgreen_rev1.sym`); the rev0/rev1-identical claim in gamestate.c's comments is TRUE
for IWRAM/EWRAM but FALSE for ROM function addresses (shifts 0x14–0x78 observed).**

## Visited rows — boot #1 (partial) + boot #2

| ID | Catalog | Screen | cb2 → symbol | Detected | Evidence | Repro route |
|---|---|---|---|---|---|---|
| A1-intro | A1 | GF intro movie | `CB2_Intro` 0x080EC9E8 | field (residual) | A1-intro.* | boot, wait |
| A2-title | A2 | Title screen | `CB2_TitleScreenRun` 0x08078BB0 | field | A2-title.* | intro → a |
| A3-mainmenu | A3 | Main menu (CONTINUE box: SHOCK 64:27, dex 117, 8 badges / NEW GAME / OPTION; NO Mystery Gift row on this save) | `CB2_MainMenu` 0x0800C2E8 | field | A3-mainmenu.*, A3b-* | title → s |
| A4-newgame-oakspeech | A4 | New-game Oak intro (controls-help page; entered by accident boot #1, save untouched — .sav only written by in-game SAVE) | `CB2_NewGameScene` 0x0812EB88 | field | A4-newgame-oakspeech.* | main menu → NEW GAME |
| K4-questlog | K4 | Quest Log playback "Previously on your quest…" (grayscale replay incl. cable-club scenes) | `CB2_Overworld` (playback runs under overworld cb2 — cb2-blind vs B1!) | field | K4-questlog.*, K4b-questlog2.* | CONTINUE after an eventful session — plays AUTOMATICALLY before control returns |
| B1-overworld | B1 | Overworld — Indigo Plateau Pokemon Center 2F, trade room (map 13,1) | `CB2_Overworld` 0x080565C8 | field ✓ (home context) | B1-overworld.* | continue |
| B2-startmenu | B2 | Start menu (POKEDEX/POKEMON/BAG/SHOCK/SAVE/OPTION/EXIT) | `CB2_Overworld` (task) | field (**residual — EM detects fmenu; rev0 anchor**) | B2-startmenu.* | s |
| F6a-dex-mainmenu | F6 | Pokedex TABLE OF CONTENTS (numerical Kanto/National + habitat list; Seen/Owned counters) | `CB2_PokedexScreen` 0x0810254C | field (**residual**) | F6a-dex-mainmenu.* | start menu → POKEDEX |
| F6b-dex-list | F6 | Numerical list (Kanto) | 〃 | field | F6b-dex-list.* | TOC → A |
| F6c-dex-entry | F6 | Entry page (Bulbasaur; NEXT DATA footer) | 〃 | field | F6c-dex-entry.* | list → A |
| F6d-dex-page2 | F6 | Entry page 2 = combined AREA map + SIZE compare (ONE page — differs from EM's separate area/cry/size screens) | 〃 | field | F6d-dex-page2.* | entry → A |
| F6e-dex-habitat | F6 | Habitat page (Grassland, portrait GRID, PAGE 1/18, pick+flip-page) — FR-only layout, touch-native candidate | 〃 | field | F6e-dex-habitat.* | TOC → habitats → A |
| E2-party | E2 | Party menu (field) — Mewtwo L70 / Articuno L50 / Hoppip L10 / Zapdos L50 / Mew L30 / Moltres L50 (date-style nicknames) | `CB2_UpdatePartyMenu` 0x0811EC18 | field (**residual — EM detects party**) | E2-party.* | start menu → POKEMON |
| E2b-partysub | E2 | Mon sub-menu (SUMMARY/SWITCH/ITEM/CANCEL) | 〃 | field (**residual — EM detects fmenu**) | E2b-partysub.* | party → A |
| E1-summary-info/skills/moves | E1 | Summary pages 1-3 (FR has NO contest page — 3 pages vs EM's 4) | `CB2_RunPokemonSummaryScreen` 0x08137F60 | field (**residual**) | E1-summary-*.​* | party → SUMMARY, R cycles |
| E1b-summary-movedetail | E1 | Move-detail mode (POWER/ACCURACY/EFFECT pane + PICK/SWITCH reorder) | 〃 | field | E1b-summary-movedetail.* | KNOWN MOVES → A |
| E3-bag-* | E3 | Bag — FR 3 pockets (ITEMS/KEY ITEMS/POKE BALLS), R switches | `CB2_BagMenuRun` 0x08107F58 | field (**residual — EM detects bag**) | E3-bag-items/keyitems/pokeballs.* | start menu → BAG |
| E3b-keyitem-submenu | E3 | Key-item sub-menu (USE/REGISTER/CANCEL) | 〃 | field | E3b-keyitem-submenu.* | key item → A |
| E3c-keyitems-scrolled | E3 | Key items incl. FAME CHECKER/S.S. TICKET/VS SEEKER(registered)/OLD ROD | 〃 | field | E3c-keyitems-scrolled.* | scroll |
| B7-townmap | B7 | Town Map (cursor at INDIGO PLATEAU = save location) | `CB2_RegionMap` 0x080C08C8 | field (**residual**) | B7-townmap.* | bag → TOWN MAP → USE |
| K1-teachytv | K1 | Teachy TV chapter list (6 chapters) | `TeachyTvCallback` 0x0815AC0C (sym name has no "Main") | field (**residual**) | K1-teachytv.*, K1b-teachytv-show.* | bag → TEACHY TV → USE |
| C14-pokedude | C14 | Pokedude demo battle (Rattata vs Pidgey) — TV show dialogs need A to advance; B quits back to the chapter list | `BattleMainCB2` 0x08011114 | field (**residual — would be b.* with the rev1 anchor**) | C14-pokedude.* | Teachy TV → "Teach me how to battle" → A through the show |
| E5-tmcase | E5 | TM Case (HM01-05 visible; footer type/power/acc pane) — **catalog correction: the live run-loop cb2 is `CB2_Idle` 0x081318DC**, the "debug/unused" flag in CATALOG gap #4 is wrong for FR: tm_case's idle loop IS CB2_Idle | `CB2_Idle` 0x081318DC | field (**residual**) | E5-tmcase.*, E5b-tmcase-submenu.* | bag → TM CASE → USE |
| E2d-party-hmchoose | E2/D7 | Party in TM-teach choose mode (ABLE/NOT ABLE) | `CB2_UpdatePartyMenu` | field (residual) | E2d-party-hmchoose.* | TM case → FLY → USE |
| C22-forgetmove | C22 | "Which move should be forgotten?" + the summary forget-move screen (FLY over Zapdos' Agility — **fixture-save mutation**) | party cb2 → `CB2_RunPokemonSummaryScreen` | field | C22-forgetmove.*, C22b-forgetmove-summary.* | teach 5th move → YES |
| E7-psa-anim | E7 | Pokemon Special Anim ("Machine set!" TM machine scene — FR-only module) | `CB2_PSA` 0x0811C774 | field (residual; cutscene) | E7-psa-anim.* | plays automatically on TM/HM teach |
| E4-berrypouch | E4 | Berry Pouch (7+ berry types) | `CB2_BerryPouchIdle` 0x0813CE78 | field (**residual**) | E4-berrypouch.* | bag → BERRY POUCH → USE |
| K2-famechecker | K2 | Fame Checker (name list + faces grid; ? = unknown) + person fact-grid PICK mode | `MainCB2_FameCheckerMain` 0x0812C40C | field (**residual**) | K2-famechecker.*, K2b-famechecker-facts.* | bag → FAME CHECKER → USE |
| A5-options | A5 | Options (start-menu route; user plays TEXT FAST + **BUTTON MODE L=A** + FRAME 3) | `CB2_InitOptionMenu` 0x08088370 (stays as the run cb2) | field (**residual**) | A5-options.* | start menu → OPTION |
| B6-trainercard | B6 | Trainer card front (SHOCK, ₽292422, IDNo 34685) + back (HoF DEBUT 39:45:14, TRADES 15) — **B on back flips to front first; TWO b's exit** | `CB2_TrainerCard` 0x08089084 | field (**residual**) | B6-trainercard-front.*, B6b-trainercard-back.* | start menu → SHOCK; A flips |
| B5-savedialog | B5 | Save dialog (INDIGO PLATEAU info + YES/NO) → overwrite confirm → "SHOCK saved the game." (**fixture .sav saved in-game once**) | `CB2_Overworld` (tasks) | field (**residual — EM detects fmenu**) | B5-savedialog.*, B5b-saving.*, B5c-savedone.* | start menu → SAVE |

## Session facts
- **Save**: Indigo Plateau Pokemon Center 2F trade room (map 13,1) — the celiolink trade
  venue. Player SHOCK ♀, ₽292422, dex seen 143 Kanto/186 National, owned 117, 8 badges,
  HoF done (39:45:14), 15 trades. Button mode **L=A** (same as the user's EM save!).
- **Party**: Mewtwo/Articuno/Hoppip(L10)/Zapdos/Mew(★shiny?)/Moltres. No Surf user.
  **HM02 FLY taught to Zapdos over Agility this session (fixture only)** → fly map unlocked.
- Key items seen: BICYCLE, TOWN MAP, TEACHY TV, TM CASE, BERRY POUCH, POWDER JAR, FAME
  CHECKER, S.S. TICKET, VS SEEKER (registered to SELECT), OLD ROD (+more below fold).
- Boot #1 lesson: on the FR main menu the remembered cursor + input-lockout races make
  blind `D..a` scripts dangerous (accidentally entered NEW GAME → Oak speech; recovered by
  azctl stop/boot, ~3 min). VERIFY the cursor with a cheap snap before A on any main-menu
  script. Post-screen-exit lockouts swallow taps (EM lesson holds): pad `W60`+ after every
  screen return, and the trainer card needs b W90 b W90 (back→front→exit).
- Quest log playback (K4) runs BEFORE control returns on CONTINUE — any FR touch/boot
  automation must tolerate ~40-90s of uncontrollable cutscene with cb2 == CB2_Overworld.

## Visited rows — boot #2 continued (Viridian / Route 22 leg)

| ID | Catalog | Screen | cb2 → symbol | Detected | Evidence | Repro route |
|---|---|---|---|---|---|---|
| E2c-partysub-fly | E2 | Mon sub-menu with FIELD MOVE (FLY in blue) | `CB2_UpdatePartyMenu` | field | E2c-partysub-fly.* | party → Zapdos → A |
| B8-flymap | B8 | Fly map — **outdoors only** ("Can't use that here." indoors, captured); same `CB2_RegionMap` cb2 as the town map (mode internal!) | `CB2_RegionMap` 0x080C08C8 | field (**residual**) | B8-flymap.*, E2c (indoor refusal in B8 first try) | outdoors → party → Zapdos → FLY |
| B7b-wallmap | B7 | Field wall-map viewer (Indigo Center 1F wall poster → full region map, MB_REGION_MAP tile) | `CB2_RegionMap` | field | B7b-wallmap.* | examine wall map |
| E11-pctopmenu | E11 | PC top menu (BILL'S PC/SHOCK's PC/PROF. OAK's PC/HALL OF FAME/LOG OFF) | `CB2_Overworld` (multichoice) | field (**residual — EM fmenu**) | E11-pctopmenu.* | Center PC nook (Viridian: (11,2), enter via (11,4)→U2) |
| E12a-storagemenu | E12 | Storage task menu (WITHDRAW/DEPOSIT/MOVE POKEMON/MOVE ITEMS/SEE YA) | `CB2_Overworld` | field | E12a-storagemenu.* | BILL'S PC |
| E12-storage-boxes | E12 | Boxes UI 6×5 (MOVE POKEMON mode; box "AABURn" is FULL) | `CB2_PokeStorage` 0x0808CDD8 | field (**residual**) | E12-storage-boxes.* | storage menu |
| E12b/c/d/e | E12 | Mon popup (MOVE/SUMMARY/WITHDRAW/MARK/RELEASE/CANCEL) / holding popup (PLACE/…) / box-title menu (JUMP/WALLPAPER/NAME/CANCEL) / party-strip panel | 〃 | field | E12b-monmenu.*, E12c-holding-menu.*, E12d-boxtitlemenu.*, E12e-partystrip.* | in-boxes |
| E10-naming | E10 | Naming keyboard ("BOX NAME?", UPPER + lower pages; SELECT flips page, START=OK, B=back) — **empty-confirm verified a NO-OP in pret src (SaveInputText), box name untouched** | `CB2_NamingScreen` 0x0809FB84 | field (**residual**) | E10-naming.*, E10b-naming-lower.* | box menu → NAME |
| E15-hofpc | E15 | Hall of Fame PC replay (No.1 team incl. DRAGONITE L60) | `CB2_HofIdle` 0x080F1E38 | field (**residual**) | E15-hofpc.* | PC → HALL OF FAME |
| E16-oakpc | E16 | Prof. Oak's PC dex rating ("You've finally hit 100 species!…" multi-page) | `CB2_Overworld` (dialog) | field | E16-oakpc.*, E16b-oakrating.* | PC → PROF. OAK'S PC |
| I1a-martdialog | I1 | Mart clerk BUY/SELL/SEE YA menu (talk ACROSS the counter) | `CB2_Overworld` (multichoice) | field | I1a-martdialog.* | Viridian Mart clerk |
| I1-martbuy | I1 | Buy menu (money box + list + description) | `CB2_BuyMenu` 0x0809ADF8 | field (**residual**) | I1-martbuy.* | BUY |
| I1b-martqty | I1 | Buy quantity picker (×N + total) | 〃 | field | I1b-martqty.* | A on item |
| I2-martsell | I2 | Sell mode = the BAG reused (same `CB2_BagMenuRun`, sell verbs internal) | `CB2_BagMenuRun` | field (**residual**) | I2-martsell.* | SELL |
| C5-battleintro | C5/C6 | Wild battle intro (RATTATA, Route 22 grass) | `BattleMainCB2` 0x08011114 | field (**residual — rev0 anchor; EM = b.oth positive**) | C5-battleintro.* | grass wiggle |
| C2-battleaction | C2 | Action select FIGHT/BAG/POKEMON/RUN | 〃 | field (**residual — would be b.act**) | C2-battleaction.* | battle |
| C3-battlemove | C3 | Move select (4 moves + PP/TYPE pane) | 〃 | field (**residual — would be b.move**) | C3-battlemove.* | FIGHT |
| C2b-battlebag | C2/E3 | In-battle bag = same bag screen/cb2 (pocket remembered from field!) | `CB2_BagMenuRun` | field (**residual — EM detects bag in battle**) | C2b-battlebag.* | battle → BAG |
| K3-vsseeker | K3 | Vs Seeker sweep + "no TRAINERS within range" (registered item, SELECT) | `CB2_Overworld` | field | K3-vsseeker.* | SELECT on Route 22 |
| B10-itemfinder | B10 | Itemfinder sweep + "Nope! There's no response." — **the result message BLOCKS walking until dismissed** (cost two 4-min walk timeouts before diagnosis) | `CB2_Overworld` | field | B10-itemfinder.* | bag → ITEMFINDER → USE |
| B11-fishing | B11 | Old Rod cast (fishing pose at Route 22 pond north edge (22,7)) + result flow | `CB2_Overworld` | field | B11-fishing.*, B11b-fishresult.* | face water → bag → OLD ROD → USE |

**Navigation technique bank (works, reusable):** pret layout blockdata `map.bin` decodes to a
walkability grid — `collision=(block>>10)&3`, water = primary-tileset behavior 0x10-0x16 via
`metatile_attributes.bin` — planning routes from it eliminated the blind-walk timeouts (each
blind failure costs ~4-5 min: walk deadline + drop2 wait). Warp doors need one extra step INTO
the tile (Indigo escalator: enter from the RIGHT tile walking LEFT; exit doors: step D on the
door tile). Wild grass interrupts walk tokens with a battle (the token TIMEOUTs; the battle is
real — run/finish it, then resume). Additional mutations this leg: +1 Rattata KO'd (Mewtwo EXP,
no level), 1 Vs Seeker charge spent, save still Indigo (no re-save since).

## Visited rows — boot #2 final leg (Celadon / Fuchsia Safari / Pallet)

| ID | Catalog | Screen | cb2 → symbol | Detected | Evidence | Repro route |
|---|---|---|---|---|---|---|
| B1c-gamecorner | B1/I-family | Celadon Game Corner interior (slot rows + NPC players) | `CB2_Overworld` | field | B1c-gamecorner.* | fly Celadon → walk (route in BUILDLOG) |
| I4b-slotprompt2 | I4 | "A slot machine! Want to play?" YES/NO | `CB2_Overworld` (bg event (11,10)) | field | I4b-slotprompt2.* | face a machine tile → A |
| I4-slotmachine | I4 | Slot machine UI (CREDIT 8399, reels, COMBOS/WAGER/STOP/EXIT footer) | `CB2_RunSlotMachine` 0x0813F9C4 | field (**residual**) | I4-slotmachine.*, I4c-slotspin.* | YES |
| I4d-slotquit | I4 | "Quit playing?" YES/NO — **FR's B-exit WORKS (vs the EM stuck-B finding: B → quit confirm → out cleanly)** | 〃 | field | I4d-slotquit.* | B in the slot UI |
| I6-coinclerk | I6 | Game Corner counter clerk ("exchange COINS for prizes next door") | `CB2_Overworld` | field | I6-coinclerk.*, I6b-coinbuy.* | counter clerk |
| B12a-c | B12 | Safari entrance building + "Welcome to the SAFARI ZONE!" + pay flow (₽500 paid, money box shown) | `CB2_Overworld` | field | B12a-safari-entrance.*, B12b-safari-pay.*, B12c-safari-dialog.* | Fuchsia → entrance (BFS route; **CUT TREES at (24,22)/(30,10)/(32,16) block the direct path — no Cut user in party, rerouted east**) |
| B12-safarizone | B12 | Safari Zone overworld | `CB2_Overworld` | field ✓ | B12-safarizone.* | pay → enter |
| B12d-safari-startmenu | B12 | Safari START menu — **RETIRE item + the 586/600 steps + BALLS 30 HUD window** | `CB2_Overworld` (task) | field (**residual**) | B12d-safari-startmenu.* | START inside safari |
| C7-safaribattle | C7 | Safari battle (Scyther): **BALL/BAIT/ROCK/RUN verb grid + "SAFARI BALLS Left: 30" + trainer sprite stays** — same `BattleMainCB2`, verbs are controller-internal | `BattleMainCB2` 0x08011114 | field (**residual — b.act equivalent**) | C7-safaribattle.* | wiggle safari grass |
| B1d-bedroom | B1 | Player's house 2F (Pallet) | `CB2_Overworld` | field | B1d-bedroom.* | fly Pallet → house → stairs |

## Honest not-visited (FireRed, this pass)
- **E13/E14 player PC item storage & mailbox** — the E11 menu row "SHOCK's PC" IS captured;
  the sub-lists weren't entered (the bedroom-PC approach burned ~40 min on the 2-floor
  stair-warp loop: the (10,2) warp tile exists on BOTH floors and bounces you back; the gs
  map read lags interiors). CHEAPEST future route: any Center PC → SHOCK's PC (simple lists).
- **I8 prize exchange menu** — prize ROOM entered + photographed (I8-prizemenu.* shows the
  3-window counter); the per-window trigger tile wasn't found in 3 tries (multichoice shape
  known from EM's I8).
- **E17 move relearner/deleter (Two Island / Fuchsia deleter), B14 Seagallop, K6 Trainer
  Tower** — Sevii trip not attempted (time); fly map lists Sevii pages, ferry ride (B14)
  needs the Vermilion pier route. All reachable with this save per the passes in bag.
- **E8/E9 mail read/compose** — no mail item confirmed in the bag pockets visited.
- **E20 diploma** — dex 117/150 owned → NOT reachable (needs complete Kanto dex).
- **C4 target select** — needs a double battle; no easy repeatable double on this save's
  route net (Vs Seeker found no rematchable trainers on Route 22).
- **C15 Marowak, C13 old man, K7 SS Anne** — consumed by the endgame save (story one-shots).
- **J-family (link/union/trade), C11 link battle** — 2P; the celiolink work already proves
  J4/J5 live.
- **B19 whiteout** — would need deliberately fainting the endgame party; skipped.
- **A6 clear-save combo (Up+B+Select)** — chorded; D4 is single-key-per-token (same EM flag).
- **B18 map preview (dungeon entry)** — no dungeon entered this pass (Victory Road/Cerulean
  Cave entries would show it; cheap add-on to any future cave visit).

## Final session stats (FR pass)
2 boots, ~5h wall. 41 distinct catalog rows visited & photographed for FR, ~80 capture
pairs in evidence/firered/. Fixture-save mutations (NEVER touching sdmc:/dual-gba
originals — azctl hash-verified): Fly on Zapdos (over Agility), 1 in-game save (Indigo
Center 2F), ₽500 safari fee + ₽?? mart nothing bought, 2 Rattata encounters (1 KO), 1 Vs
Seeker charge, quest-log playback consumed on first continue.

Late additions/corrections: tmp captures renamed (E1c-moves-2..6 = full party moveset
audit; C22a-replace-prompt = the "deleted and replaced?" YES/NO; B1e-route22 = Route 22
overworld). **K5 (help overlay, L/R) was NOT attempted — and note the user plays BUTTON
MODE L=A, which remaps L to A: the help system is then only on R (touch plan: a HELP chip
may matter more on FR because of this).** I8 room shot kept as I8-prizemenu (menu itself
not triggered).
