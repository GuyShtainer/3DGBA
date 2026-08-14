# OVERNIGHT2 build log — S1b spec lane (desk work only) + S2 merge lane

## S2 Entry 0 — DEVIATION: the promotion worktree is GONE; re-implementing S1a from the banked docs

S2 session 2026-08-14 (subagent). The merge checklist said fetch branch `phase21-cb2-promotion`
from worktree `wf_6488b05b-da8-1` (commit d370f2a). **That worktree no longer exists** —
`.claude/worktrees/` is empty, `git cat-file -t d370f2a` fails in the main repo, no dangling
commit matches (fsck checked), no bundle/patch banked anywhere on disk (mdfind + find swept).
The branch's OBJECTS are lost; its CONTENT is not: OVERNIGHT2.md §S1-results describes exactly
what it did (40 [exact] fingerprints -> GCTX_TITLE + GCTX_FULLUI, FR-rev1 battleMainCbAlt,
tilt/presence gates fixed with zero logic change, 14 suites green) and every input it consumed
is banked (CB2-HARVEST.md, COVERAGE.md, VISITED-*.md, RS-REV2-VERIFICATION.md). Per the
robustness rule ("check what's banked on disk FIRST — the work usually survived"), S1a is being
RE-IMPLEMENTED in the main tree from those docs, fused with the S2 rev reconciliation the task
ordered anyway. Frozen paths (celiolink.c/netlink.c) untouched.

## S2 Entry 1 — the promotion + rev reconciliation IMPLEMENTED, all suites green

What landed (source/gamestate.{h,c} + test/host/test_profiles.c only; celiolink.c/netlink.c/
gbacore.c diffs EMPTY):

1. **GCTX_TITLE + GCTX_FULLUI** appended to GameCtx (values stable; the main.c _Static_asserts
   pinning GCTX_OVERWORLD==TILT_CTX_FIELD==FIELD_CTX_OVERWORLD are undisturbed). New GameProfile
   arrays `cb2Title[6]` / `cb2FullUi[18]`; game_read matches gMain.callback2 against them AFTER
   every task-based menu check and BEFORE the overworld fall-through. The 40 census [exact]
   fingerprints promoted: EM 3 title + 16 fullui, FR 5 title + 16 fullui (incl. CB2_LoadMap2 —
   map-load transitions stop leaking walk keys too). BPGE/RS lists empty = named degradation
   (no LG/RS census visit yet; their delta passes harvest live). Touch dispatch: both new ctxs
   hit the default arm -> 0 keys injected (the walk-key leak on ~40 screens is DEAD); tilt/
   presence/field gates (all test ctx==GCTX_OVERWORLD) shut on them with ZERO gate-logic change.
2. **Dual-revision ROM anchors**: 13 `*Alt` fields appended to GameProfile; `task_active2`, the
   inBattle test, the ctrlFuncs target scan and find_bag_list_task all match primary OR alt
   (compare-only -> fail-safe). BPRE: primaries stay FR rev0, alternates = FR rev1 (the user's
   cart) — six live-verified [exact] census 2026-08-14 (BattleMainCB2 0x08011114,
   CB2_UpdatePartyMenu 0x0811EC18, CB2_BagMenuRun 0x08107F58, Task_HandleChooseMonInput
   0x0811FBA0, Task_StartMenuHandleInput 0x0806F204, Task_HandleSelectionMenuInput 0x08122CD4),
   seven rev1 sym-derived/verify-in-emulator (CB2_InitPartyMenu 0x0811EC48,
   HandleInputChooseTarget 0x0802E688, StartCB_HandleInput 0x0806F294, Task_BagMenu_HandleInput
   0x08108F84, Task_YesNoMenu_HandleInput 0x0809CE68, Task_MultichoiceMenu_HandleInput
   0x0809CCAC, Task_MapNamePopup 0x080981C0). This un-deads ALL FireRed battle/party/bag/menu
   touch on the user's rev1 cart.
3. **BPGE reconciliation** (RS-REV2-VERIFICATION.md §6): the 13 ROM primaries — FR-rev0 values,
   wrong for EVERY LeafGreen revision — replaced with the LG **rev1** column (user's cart is
   rev 1.1), LG **rev0** column shipped as the alternates; all sym-derived, marked
   verify-in-emulator for the LG delta pass. AXVE/AXPE untouched (rev2-verified SAME; new
   columns explicit zeros — the RS ROM-address ban covers alternates).
4. **newKeys transposition fix** (§7, OVERNIGHT2 item 4 — with citation + suite pin, never
   silent): BPRE + BPGE 0x0303011E -> 0x0300311E (gMain 0x030030F0 + 0x2E). Field confirmed
   unused at runtime.
5. **Suites extended**: test_profiles TEST 10 (all 26 primary/alt anchors + newKeys pinned per
   profile, alt-is-ROM + alt!=primary structure), TEST 11 (both class lists pinned exact,
   ROM-space + no-duplicate + never-visited-games-ship-empty-lists), TEST 12 (behaviour through
   the real game_read: rev1 battle/party/start/bag detect via alternates, rev0 still detects,
   title/fullui classify positively, task beats class precedence, unknown cb2 still falls
   through resolved=0, ctx-name table). test_profiles 546 -> **1295 checks**.

Gate (fresh runs, this session): celiolink 1259 · control 6897 · diag 376 · fieldpath 1808 ·
netlink 66 · peersprite 62078 · presence 61376 · profiles **1295** · theme 83444 · tilt 1723 ·
trace_replay 58 (4 loud SKIPs) · typography 1419 · uigeom 18332 · uihit 1834 = **240 765
checks, 0 failures** (14 suites). emutest harness host tests **149, OK**. `make -j8` ->
3DGBA.3dsx 4 366 904 B; `make cia` -> 3DGBA.cia 2 009 024 B (both clean, 2026-08-14 03:45).
The only build warnings are the pre-existing mgba/celiolink ones (frozen files untouched).

## S2 Entry 2 — COMMITTED (all three pieces, each independently green)

- **6fcda63** `docs(phase21)` — the census bank (545 files: catalog/visited/harvest/coverage/
  plan/specs/RS-rev2 verification/evidence PNGs + logs) + the HANDOFF census update.
- **a51c50d** `feat(phase22.0)` — the promotion re-implementation: GCTX_TITLE/GCTX_FULLUI +
  the 40 [exact] EM/FR fingerprints + the 13 rev-alternate columns with the FR rev1 set
  (test_profiles 546 -> 1108 at this commit; verified green before committing).
- **305242f** `fix(profiles)` — BPGE re-pointed to LG rev1 (LG rev0 as alternates) + the
  BPRE/BPGE newKeys transposition fix + TEST 10 (test_profiles -> 1295; verified green).

Final state rebuilt after the last commit: `make -j8` + `make cia` clean. The split was done
by temporarily reverting the (iii) hunks, verifying, committing (ii), then restoring — so BOTH
source commits were compile+suite verified at their own tree state, not just the tip.

Session 2026-08-14 (subagent, S1b of OVERNIGHT2.md). Constraint honored: no emutest edits, no
main-tree rebuild, no emulator boot, no main-tree source writes. Deliverables = two spec files
in this directory. Suites: N/A (nothing built — spec-only lane).

## Entry 1 — research pass done (banked before writing specs)

Sources actually opened this session (all read-only):
- `COVERAGE.md` (seam limits §5, GCTX matrix, bag gesture constants), `CATALOG.md`,
  `CB2-HARVEST.md`, `VISITED-emerald.md`, `VISITED-firered.md`, `OVERNIGHT2.md`.
- `source/touch.c` (select_pulse :164-170, hit_fieldmenu/fmenu_select :534-555,
  hit_bag/bag_update :565-598), `source/uihit.h` (UIHIT_DRAG_PX 6 = the picker
  drag-vs-tap convention, uihit.h:34 / uihit.c:39 one-way latch).
- pret pokeemerald LOCAL clone (`gba-toolkit/projects/PokeDNA/daycare map/pokeemerald/`):
  `src/naming_screen.c`, `src/shop.c`, `src/player_pc.c`, `src/pokedex.c`,
  `include/list_menu.h`, `include/sprite.h`, `charmap.txt`.
- pret pokefirered `src/naming_screen.c` fetched from GitHub master (2509 lines) to
  session scratchpad `fr_naming_screen.c` — **FR naming screen is line-for-line identical
  to EM in struct layout (offsets annotated 0x1800/0x1E10/0x1E22/0x1E23), keyboard tables
  (sPageColumnXPos/counts/chars) and cursor math (x=colX+38, y=row*16+88)**. The census
  captures (evidence/emerald/E10-naming.top.png vs evidence/firered/E10-naming.bottom.png)
  confirm pixel-identical layouts.
- Symbol maps: this session's scratchpad `syms/` (pokeemerald.sym, pokefirered_rev1.sym,
  + LG/RS rev files) — the same fetch the census banked.

Key address findings (details + status flags inside the specs):
- EM: sNamingScreen 0x02039F94, gSprites 0x02020630, gTasks 0x03005E00, gWindows 0x02020004,
  sPokedexView 0x02039B4C, Task_BuyMenu 0x080E0AC8, ItemStorage_ProcessInput 0x0816C30C,
  gPlayerPCItemPageInfo 0x0203BCB8 (all pokeemerald.sym; cb2 anchors already live-harvested).
- FR rev1: sNamingScreen 0x0203998C, gSprites 0x0202063C, gTasks 0x03005090, gWindows
  0x020204B4, CB2_NamingScreen 0x0809FB84, Task_BuyMenu 0x0809BBD4, Task_ItemPcMain
  0x0810DF18 (pokefirered_rev1.sym; sym-derived → live-confirm before promotion per the
  zero-guess rule).
- struct ListMenu = 24-byte template + scrollOffset(+24) + selectedRow(+26), embedded at
  gTasks[listTaskId]+8 — matches the shipped bag write (+26). tListTaskId slots: shop
  data[7] (+22), player_pc data[5] (+18), bag data[0] (+8).
- struct Sprite: x/y at +0x20/+0x22, data[8] at +0x2E, stride 0x44 (sprite.h annotations).
- Charmap (proof values): 'A'=0xBB 'I'=0xC3 'K'=0xC5 'P'=0xCA 'a'=0xD5 '0'=0xA1 '!'=0xAB.

Seam verdict (the task's explicit question): **no seam change is needed for either family.**
The per-frame mask return already expresses pulses/holds/sequences; write8/write16 cover
every field we touch (u8/s16/u16). Both specs say so with the reasoning.

## Entry 2 — SPEC-family-keyboard.md written

Banked: `docs/phase21-touch-census/SPEC-family-keyboard.md` (requirements R1–R14 + seam
verdict + per-game table + proof plan P1–P7). Design core: char keys = cursor-sprite write-then-A (house idiom); PAGE/BACK/OK =
the game's own SELECT/B/START+A key equivalents (no RAM write); micro-sequencer lives in
the handler, not the seam; PAD-mode fallback on any validation-gate failure.

## Entry 3 — SPEC-family-lists.md written

Banked: `docs/phase21-touch-census/SPEC-family-lists.md` (requirements L1–L24 + per-screen
tables + proof plan P-A…P-F). Design core: ONE generic ListMenu driver (live template read: window
rect, totalItems, maxShowed, selectedRow+26) instantiated per screen by an anchor-task
table; EM Pokédex is the one custom-list adapter (relative taps + step injection); mart
sell = the bag; party context menus = the already-shipped sMenu fmenu path. Blank-row
clamp from live totalItems fixes the known "tap blank → CLOSE BAG" wart.

Both specs end with emulator proof plans (gdb symbol chains named per screen) explicitly
deferred to phase-22 lane A — nothing was booted this session.

## Entry 4 — SPEC-family-traversal lane opened (separate subagent, desk work only)

Task: the user's HM-aware touch + cross-map (Lavaridge) routing spec → SPEC-family-traversal.md.
Constraint honored: no emulator, no source/ edits. Research banked so far (all read-only):
- Foundations re-read: fieldpath.{c,h} (FpKind/terminal-hold model, warp_at, enterable rule),
  gamestate.h (profile fields incl. playerAvatar/partyCount/mapHeaderPath), touch.c walk loop
  (TERM_FRAMES hold, warp kill-switch, replan rule, fplog/g_fieldDbg), COVERAGE.md (seam limits),
  SPEC-family-keyboard.md (spec format), OVERNIGHT2.md addendum (the verbatim ask).
- pret pokeemerald LOCAL clone (PokeDNA/daycare map/pokeemerald): surfable = TILE_FLAG_SURFABLE
  rows of sTileBitAttributes (metatile_behavior.c:25-90; POND 0x10 / INTERIOR_DEEP 0x11 / DEEP
  0x12 / WATERFALL 0x13 / SOOTOPOLIS_DEEP 0x14 / OCEAN 0x15 / NO_SURFACING 0x19 / SEAWEED 0x22 /
  0x2A / CURRENTS 0x50-0x53); surf interaction gate = FLAG_BADGE05_GET && PartyHasMonWithSurf()
  && IsPlayerFacingSurfableFishableWater() (field_control_avatar.c:450); waterfall = BADGE08 +
  IsPlayerSurfingNorth, script checks MOVE_WATERFALL (:453-458 + field_move_scripts.inc); dive =
  BADGE07 + TrySetDiveWarp()==2; cut/rocksmash/strength scripts + badge gates BADGE01/03/04 +
  checkpartymove (field_move_scripts.inc:1-185); FLAG_SYS_USE_STRENGTH cleared in overworld.c
  :374-405 (the once-per-map-load latch); EM SaveBlock1.flags @0x1270 (global.h:1020), badges =
  SYSTEM_FLAGS(0x860)+7..0xE (flags.h:1348-1366); EM gfx ids CUTTABLE_TREE 82 / BREAKABLE_ROCK
  86 / PUSHABLE_BOULDER 87 (event_objects.h:89-94).
- pokefirered local checkout is PARTIAL (save headers only: SaveBlock1.flags @0x0EE0,
  global.h:790) → FR behaviours/badges/gfx-ids/scripts must be fetched from pret GitHub master
  next; every FR number in the spec gets the fetch citation + verify-live flag.

## Entry 5 — SPEC-family-traversal.md written (the lane's deliverable)

Banked: `docs/phase21-touch-census/SPEC-family-traversal.md` — H0-H4 requirements + slices +
proof plan + 7 user open questions. Research completed after entry 4 (all banked in the spec
with citations):
- FR fetches (scratchpad fr/): surfable set sBehaviorSurfable (POND 0x10 / FAST 0x11 / DEEP
  0x12 / WATERFALL 0x13 / OCEAN 0x15 / UNUSED 0x1A / CYCLING_ROAD 0x1B / currents 0x50-53),
  badges SYS_FLAGS 0x800 +0x20..0x27 (Cut=BADGE02, RockSmash=BADGE06, Waterfall=BADGE07 —
  all THREE differ from Emerald), FLAG_SYS_USE_STRENGTH 0x805, gfx ids CUT_TREE 95 /
  ROCK_SMASH_ROCK 96 / PUSHABLE_BOULDER 97, field_moves.inc gates (+ the questlog guard).
- EM: FLAG_SYS_USE_STRENGTH 0x889; surf script data/scripts/surf.inc (yes/no shape identical
  to cut/smash); ScrCmd_checkpartymove skips eggs; BoxPokemon has an UNENCRYPTED isEgg/
  hasSpecies byte (+19 bits 1-2) → egg exclusion without decryption; moves need the Attacks
  substruct decrypt (personality%24 order, key personality^otId, checksum rail) per the learn
  skill gen3-saves reference; struct ObjectEvent graphicsId @+0x05 (the object-edge detector).
- Sym re-reads (scratchpad syms/, house zero-guess rule): gPlayerParty EM 0x020244EC,
  FR/LG 0x02024284 (rev0=rev1); gMapGroups EM 0x08486578, FR rev0 0x083526A8 vs
  **rev1 0x08352718 — a real rev trap, user's FR is rev1**, LG 0x08352688/0x083526F8,
  RS 0x083085A0; RS gPlayerParty is IWRAM 0x03004360 → RS deferred.
- Lavaridge trace from map.json: town (0,12) warp 3 (9,6 front door → PC warp 0) + warp 5
  (9,2 terrace ← PC warp 3 back door at (2,1)); PC 1F = (4,5). Test spots: Route 103 (0,18)
  surf pond + cut trees, Route 116 (0,31) cut tree (21,6), Rusturf Tunnel (24,4) smash rocks
  (24,4)/(24,5).
Design core: route PROGRAM layer (LEG/INTERACT/AWAIT) over the unchanged fieldpath substrate;
tier-0-dry-first layered (FOOT/SURF) BFS; eligibility = badge-flag read via sb1ptr+flagsOff
(EM 0x1270 / FRLG 0x0EE0) + decrypted party moves; closed-loop sequencer on
textDlg/yesNoTask/object-slot/avatar-flag reads with never-answer-an-unpredicted-YES as the
safety property; excursions = one out-and-back warp pair, ROM-planned + live-replanned per
leg, plan-NOTHING on any doubt. Toggle = g_prefs.smartTraverse 0/1/2 (Off default). Seam
verdict: no injection-seam change needed. No emulator was booted; no source/ file touched.

## Entry 6 — S3 dual-emulator upgrade STARTED (instance profiles) — plan + probes

Goal (OVERNIGHT2 S3): two fully-isolated Azahar instances A (default, unchanged) and B.
Probes banked BEFORE code (house rule — cite, don't guess):
- Azahar 2125.1.2 has NO user-dir CLI flag (full `-x, --…` help-string dump from the binary:
  only movie/dump/fullscreen/gdbport/install/help/movie-play/movie-record/version/windowed).
  `%CITRA_USER_DIR%` in strings is a shortcut-template placeholder, not an env override.
- THE data-dir mechanism (release source, tag 2125.1.2): citra_qt.cpp:4394-4397 forces
  cwd = <dir containing Azahar.app> at startup (`SetCurrentDir(GetBundleDirectory()+"..")`),
  then common/file_util.cpp:964-969 uses `<cwd>/user/` as the PORTABLE user dir when it
  exists (USERDATA_DIR "user", common_paths.h:21; GetCurrentDir appends the trailing '/',
  file_util.cpp:806-808). macOS default otherwise = ~/Library/Application Support/Azahar
  (EMU_APPLE_DATA_DIR, common_paths.h:36).
  => instance B = a PRIVATE BUNDLE COPY at tools/emutest/az-b/Azahar.app + sibling az-b/user/
  (APFS clone, `cp -Rc`, ~0 extra disk). A symlinked bundle would resolve to the real
  location (CFBundleCopyBundleURL) and leak to the user's data dir — copy, not symlink.
- The direct-binary modal ("run directly rather than via the Azahar.app bundle",
  citra_qt.cpp:487-495) only fires via AppleUtils::IsRunningFromTerminal() — `open -n -a
  <copied bundle>` avoids it; `-n` forces a second instance despite the shared bundle id.
- recent.bin (rompicker.c RECENT_PATH/RecentPair): 512 B = char a[256] + char b[256],
  NUL-terminated (b[0]==0 => single mode); verified against state/recent.bin.bak hexdump.
- Game-code-per-instance gdb read: `romBuffer` (libmgba global, nm B 0x005d68ec this build)
  points at the LAST loaded core's ROM (gbacore.c:151-158: first core reuses the boot
  romBuffer, the SECOND load repoints the global) → the gate boots each instance SOLO
  (recent.bin single mode) so romBuffer+0xAC = THAT instance's game code, unambiguous.
Plan: new instance.py (id→state/runs/gdb-port/az-bin/az-data derivation, env overrides win);
azctl routes paths + pgrep-by-executable-path per instance + `open -n` + template build +
`--stage-roms` (roms/ → sdmc:/3DGBA fixtures + recent.bin, manifest-tracked); gdbio port
per instance; see.py window-by-kCGWindowOwnerPID with a pure host-testable seam; per-instance
locks come free with the per-instance state dir. Gate at the end (5 live proofs + fps tax).

## Entry 7 — S3 GATE PASSED: two isolated Azahar instances live, all 5 proofs + fps tax

Harness upgrade landed (instance.py + azctl/gdbio/see/run + 17 new host tests, suite
169/169 green). Live gate 2026-08-14 ~01:10Z, Emerald on A / FireRed on B, both booted
solo via `--stage-roms` (recent.bin single-mode pairing + the wait900/tap-A/idle movie):

(i)  CONCURRENT, DISTINCT PIDS — A pid 10296 (user bundle, port 24689, state/), B pid
     10477 (az-b clone, port 24690, state-b/); each `azctl status` shows its own pid and
     lists the other under other_instances_pids. B's boot built the az-b template on
     first use (APFS bundle clone + qt-config template from A's CLEAN backup + sdmc
     skeleton + 0-byte dspfirm.cdc) and Azahar ADOPTED the portable az-b/user/ tree
     (its log rotation + config write-back happened there — the readiness probe's own
     evidence). Proof the S3 probe was right: cited in instance.py.
(ii) GDB READS DON'T CROSS — the SAME address (romBuffer -> 0x0800a280, +0xAC) read
     back-to-back through the two ports returned BPEE (A/Emerald) and BPRE (B/FireRed).
(iii) SEE CAPTURES DON'T CROSS — both windows sat at IDENTICAL bounds (323,154
     1280x568), so owner-name matching alone WOULD have cross-captured; the new
     kCGWindowOwnerPID seam returned each instance's own window: A = "Pokemon Emerald"
     HUD (GF intro flash), B = FireRed title screen w/ Charizard. Shots read + archived:
     runs/20260814-010955/gate_A_emerald.top.png, runs-b/20260814-011026/gate_B_firered.top.png.
(iv) INDEPENDENT TEARDOWN — `azctl stop` on A (harvest + byte-identical restore of the
     USER config) left B running (uptime 230s, port open, BPRE still readable, capture ok).
(v)  RESTORE — user's qt-config.ini sha256 dd20792e…9083286 identical pre/post; dual-gba
     originals re-hashed untouched on BOTH stops; clean-fixtures restored the user's
     recent.bin byte-identically; sdmc:/3DGBA back to ROM-less.

FPS TAX (renderSeq deltas over ~20 s, display asleep, GBA-core HUD in parens):
  concurrent: A 22.4/s (HUD ~29fps), B 17.2/s (HUD ~17fps)
  B solo (after stopping A): 29.1/s
  => a second instance costs roughly 25-40% of each instance's rate on this machine;
  budget lanes accordingly (census-era solo boots already ran 13-28 fps in heavy scenes).
Ops notes for the lanes: `run --instance b <tool> …` everywhere; B's gdb needs its own
`gdbio resume`; stage via `--stage-roms NAME[,NAME]` (writes recent.bin, movie boots
land on the resume prompt); NEVER `open` az-b/Azahar.app by hand without -n.

## Entry 8 — LANE B: RUBY FIRST BOOT (instance b) — AXVE profile LIVE for the first time

Boot 2026-08-14 ~01:36Z: `run --instance b azctl boot --gdb --movie boot_resume.ctm
--wipe-netlogs --stage-roms ruby` (NOTE: `--fresh-sd-fixtures` is the dual-gba tier-B
path and FAILS on instance b — no dual-gba originals there; --stage-roms alone is the
correct recipe). Lane-B hermetic setup: ELF+3dsx SNAPSHOTTED to scratchpad laneb/ and
every azctl/gdbio call runs with EMUTEST_ELF/EMUTEST_APP pointing at the snapshot, so a
Lane-A rebuild of the main tree cannot skew instance-b symbol reads. gs statics re-derived
from the snapshot (SHIFTED vs the census build — s_gsLog 0x00530a7c→0x00531a7c, s_lastCb2
0x00530a64→0x00531a64, s_lastCtx 0x0052342c→0x0052442c, s_gsLogN 0x00531a60; nm size of
s_gsLog 0x27000 = 1024×156 = entry layout unchanged). Query tool: scratchpad
laneb/gsqb.py (instance-b port, resolves on pokeruby_rev2.sym).

First-ever AXVE execution, all live-read [exact] on `pokeruby_rev2.sym`:
- control log header `p1=AXVE` -> the profile ROW MATCHED on first boot.
- GF intro   cb2 0x0813B7B8 `MainCB2_Intro` [exact]
- Title      cb2 0x0807C474 `MainCB2` [exact] (title_screen module — neighborhood
  CB2_InitTitleScreen 0x0807C110 / Task_TitleScreenPhase1 0x0807C48C)
- Main menu  cb2 0x080096C4 `CB2_MainMenu` [exact] — shows the 600h save (GUY, 614:45,
  dex 259, 8 badges)
- Overworld  cb2 0x080543C4 `CB2_Overworld` [exact], ctx=field, sb1V=1, px=10,py=2,
  obj=17,9 (= px+7/py+7 — the MAP_OFFSET relation holds => sb1ptr AND mapObjects both
  correct live), map=13,6 (a Pokemon Center interior), face read working.
- D4 channel operational on AXVE (5-token script picked up + completed).
- ctx stays `field` on intro/title/menu as EXPECTED — the RS cb2Title/cb2FullUi lists
  are empty (gamestate.c:653); the four cb2 values above are the promotion candidates.
Captures: laneb/rb_boot1|rb_title|rb_title2|rb_overworld (to be curated into
docs/phase21-touch-census/evidence/ruby/).

## Entry 9 — LANE B: Ruby solo pass COMPLETE — battleMainCb 0x0800F808 VERIFIED LIVE

Wild Linoone on Route 121: cb2 -> 0x0800F808 `BattleMainCB2` [exact] and the ctx gate
sequence b.oth -> b.act -> b.move -> b.act -> field tracked the on-screen truth exactly —
the ONE shipped RS ROM anchor plus the whole battle sub-gate chain
(actionCursor/moveCursor/ctrlFuncs/battlerPos/…) are live-proven on the user's-rev ROM.
Party/bag/START correctly degrade to field (RS zeroed pointers — no false positives).
RS cb2 harvest for future promotion (all [exact]): intro 0x0813B7B8 MainCB2_Intro, title
0x0807C474 (title MainCB2), main menu 0x080096C4 CB2_MainMenu, overworld 0x080543C4
CB2_Overworld, party 0x0806AEFC CB2_PartyMenuMain (+task HandleDefaultPartyMenu
0x08089CF4), bag 0x080A3138 (+task 0x080A50E8). Full verdict table + ops routes:
LANE-B-RS.md §1. 12 captures -> evidence/ruby/. Next: ruby+sapphire co-op boot (the
AXVE-vs-AXPE universe-gate empirical answer).

## Lane A Entry 1 — keyboard + lists families IMPLEMENTED (code complete, suites pending)

Session 2026-08-14 (lane A subagent, instance a). Code landed per SPEC-family-keyboard +
SPEC-family-lists (both read in full; deviations named below):

1. **source/touchgeom.{c,h}** (NEW, pure C, host-testable): naming-screen hit geometry (page
   column tables re-verified against the LOCAL pret pokeemerald clone this session —
   sPageColumnXPos :306-310, sPageToKeyboardId :598-603, sKeyboardChars :280-299, WIN_COUNT=5
   => struct offsets 0x1800/0x1E10/0x1E22/0x1E23 re-derived by field-size walk, spec confirmed);
   generic ListGeom valid/tap-row-clamp/arrow-band/fling math; EM pocket-dot rects from
   DrawPocketIndicatorSquare (item_menu.c:1407-1414: tile (pocket+5,3) => x 40+8i, y 24..32);
   FR arrow rects from sPocketSwitchArrowPairTemplate (FR item_menu.c:287-299: L(8,72) R(72,72)).
2. **gamestate.{h,c}**: GCTX_NAMING + GCTX_LIST appended (+ LK_* kinds); GameProfile grew 14
   ROM/RAM anchors + 2 slot bytes (ALL re-read from the 5 sym maps this session, banked in the
   session scratchpad syms/; FR primaries rev0 + rev1 alts, LG primaries rev1 + rev0 alts —
   the row conventions kept; RS all-zero = the ROM ban, named); find_bag_list_task generalised
   to find_list_task(handler, alt, slot) (EM shop data[7]/pc data[5]; FR shop data[7]/item_pc
   data[0] — each verified in that engine's shop.c/player_pc.c/item_pc.c); detection order:
   buy -> qty -> pcItem -> namingCb -> EM dexTask, all BEFORE the 22.0 title/fullui loops
   (more specific wins), AFTER every existing task menu check.
3. **touch.c**: bespoke bag handler replaced by ONE generic list driver (live template+window
   read every frame; blank-row clamp = the L3 wart fix; drag 14px/edge; fling = capped held key
   avg-of-4 velocity; scroll-arrow bands; EM pocket-dot delta seq / FR arrow taps); qty roller
   (drag ±1/±10, tap=A); EM dex adapter (key-injection only: drag/fling/swipe page-jump +
   START/SELECT footer chips at pokedex.c:2791-2801 anchors; relative taps DISABLED until the
   L21 formula is derived — g_touchDbg carries count/selected/initialVOffset/listVOffset for
   that); the NAMING handler (write-then-A on the cursor SPRITE incl. x/y so the visual cursor
   follows; SELECT/B/START-gap-A for PAGE/BACK/OK; R9 gates 1-4 + R10 PAD-fallback frame;
   act-on-press chosen for R14 — logged here as the decision); g_touchDbg gdb mirror exported
   (touch.h documents every offset; nsText[16] = the P1 charmap proof channel; P-D
   ListMenuDummyTask probe stamped on GCTX_FULLUI frames).
4. **control.{h,c} + main.c**: D4-T synthetic-touch script channel — sdmc:/cias/control/touch.txt
   (t/d/w grammar, pure-C parser+scheduler in control.c, consumed-on-pickup + '!' abort + the
   same opt-in dir), merged in main.c UPSTREAM of the real touch_update path (a synthetic tap is
   indistinguishable from a stylus downstream => end-to-end proofs). Real touch wins over synth.
5. DEVIATIONS from the specs, reasoned: (a) L8 qty window-band taps replaced by the rect-free
   drag roller + tap-confirm (no qty rect in task data, no banked capture rects; drags cannot
   mis-hit); (b) the spec's "gSpritesBase profile addition" NOT added — GameProfile.sprites
   (phase 20, VERIFIED-SYM) already carries gSprites; (c) naming cb2 values stay in cb2FullUi
   (pinned by TEST 11) — GCTX_NAMING simply tests first.

`make -j8` clean (3DGBA.3dsx 4 377 368 B, 0 new warnings); test_profiles 1295/1295 still green
with the appended columns; host suites for the new geometry + grammar are NEXT (then the
emulator proof plans P1-P7 / P-A..P-F).

## Entry 10 — LANE B: RS co-op — universe gate ANSWERED + a per-title ROM-drift headline

ruby+sapphire pair boot (presence pre-seeded via settings.bin word24=1): `p1=AXVE p2=AXPE`
both matched; g_presDiag: gameId 3/3, pairReason=0, live=active both, peer records crossing
both directions, CO-OP chip RENDERS on the HUD -> **AXVE+AXPE = same universe id, empirically
proven** (phase-18 P3.1 closed). Only blocker = honest `reason=map`: Ruby wakes in Lilycove,
Sapphire in SOOTOPOLIS (dive-locked; same-map = a Surf+Dive traversal job -> recorded, not
forced). **HEADLINE: Ruby-vs-Sapphire ROM addresses DRIFT (+4 at title MainCB2 and
CB2_Overworld, party cb2 drifts too; intro/mainmenu/battle/bag identical)** — the 727-symbol
six-map identity was RAM-ONLY; future RS promotions must be PER-TITLE (a shared-body ROM
promotion = the BPGE failure mode again). BattleMainCB2 0x0800F808 verified same on both
maps (why the shipped shared anchor is safe). Full table: LANE-B-RS.md §2; captures ->
evidence/rs-coop/. Next: LeafGreen 13-anchor live verification.
