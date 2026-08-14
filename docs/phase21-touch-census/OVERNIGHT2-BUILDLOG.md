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

## Lane A Entry 2 — suites green, milestone committed (+ the index race, resolved)

All 15 host suites green: profiles 1295->1417 (TEST 13 pins the 16 new columns x5 games,
TEST 14 drives naming/list detection through the real game_read incl. the bad-slot L10
degradation + RS never-fires), control 6897->6940 (D4-T grammar + tick timeline), NEW
touchgeom 115 396 (exhaustive 3-page x 240x160 hit sweep vs a brute-force oracle) —
**356 326 checks, 0 failures**; make + make cia clean.

COMMIT RACE (both lanes share one index): lane A staged its milestone; lane B's commit
swallowed it (0e521db), lane B reset to fix that, and lane A's retry then swallowed lane
B's re-staged evidence into f8f78cd (the "marker"). Net result — every change IS committed,
messages crossed once: f8f78cd = lane B's RS-coop evidence + this lane's buildlog entry;
**6b93a69 = the real lane-A code milestone (exactly the 12 lane-A files)**. No content lost,
history append-only. Lesson for the morning: two lanes in ONE work tree race on the INDEX,
not just the ref — worktrees per lane next time.

## Lane A Entry 3 — TWO instance-a harness landmines found + fixed (movie tail + sdmc snapshot)

Three lost boots (~40 min) root-caused, both findings verified by controlled repro:

1. **A CTM movie that ENDS wedges the whole app** (Azahar 2125.1.2, instance a, dual boot,
   gdb client attached): runs/sweep-verify/resume.ctm has only a ~67-emulated-second tail;
   at tonight's ~8-19 fps the end lands 3-5 min into the session and the WHOLE 3DS render
   freezes — two `see shot top` captures 8 s apart PIXEL-IDENTICAL (HUD clock stuck), D4
   token clock stopped, gdb stub unresponsive. This also retro-explains every "gdb died on
   me" event tonight. The census never hit it because boot_resume.ctm carries a 6-HOUR idle
   tail (VISITED-emerald.md:3). FIX: state/lane_a_resume.ctm — wait900 + tap A +
   780 000-frame tail (~3.6 h emulated; outlives any session).
2. **sdmc file visibility SNAPSHOTS at app spawn**: control files created on the host
   AFTER Azahar spawns are NEVER seen by the app (stat fails forever), even though files
   present at spawn are picked up and consumed normally. Verified: pre-staged files all
   consumed at session start; the same file re-dropped mid-run sat unconsumed 4+ min in a
   LIVE session (token clock advancing). Consequence: **the drop-a-file-mid-run workflow
   the census describes does NOT work on this box tonight** — every route + touch script
   must be PRE-STAGED before `azctl boot`, one proof arc per boot. (How the census
   iterated mid-run remains unexplained — flagged for the morning; suspicion: it may have
   always been boot-cycled without saying so, or an Azahar/macOS update changed FS
   behaviour in between.)

Also learned: D4.11 counts touch-injected keys as "real input" -> a touch op ABORTS a
running move script (by design). Sequencing rule: the touch script opens with w-waits
sized past the route's end.

## Lane A Entry 4 — 🎉 KEYBOARD FAMILY EMULATOR-PROVEN ON EMERALD (P1-P6)

Boot 20260814-023312 (instance a, dual Emerald, long-tail movie, one-shot pre-staged
route: NEW GAME -> Birch speech -> player naming; 48-token D4 route + 26-op touch script).
The SMART keyboard handled the naming screen end-to-end — every proof from the spec's §5
plan, live:

- **P1 (the headline): touch-taps typed "PIKA"** — four character taps (cursor-sprite
  write-then-A) and the game's OWN textBuffer read `CA C3 C5 BB FF...` over gdb
  (g_touchDbg+0x20) = 'P' 'I' 'K' 'A' + EOS, the exact charmap bytes the spec predicted.
  Screenshot: evidence/impl/EM-naming-PIKA-typed.bottom.png ("YOUR NAME? ▶PIKA_").
- **P3 page swap**: PAGE-button tap -> SELECT pulse -> nsPage 1->2 (LOWER), screenshot
  EM-naming-lower-page-after-swap.bottom.png.
- **P4 BACK**: buffer length shrank exactly 1 per BACK tap (PIKA -> PIK observed).
- **P5 OK commit**: the START-gap-A sequence pressed OK; state left STATE_HANDLE_INPUT,
  ctx left GCTX_NAMING, and Birch asked **"So it's PIK?"** — the game accepted the
  touch-typed name (EM-naming-OK-commit-so-its-PIK.bottom.png).
- **P6 no-leak**: grid taps never emitted a D-pad key; GCTX_NAMING swallowed every tap.
- **R9(d)/R10 observed live**: a lowercase tap fired DURING the page-swap anim was
  correctly DROPPED by the state gate; the R10 PAD-fallback frame injected one UP
  (naming-screen native D-pad — harmless). v1.1 suggestion banked: queue the tap until
  state==2 instead of falling back, for snappier page-swap typing.
- Bonus 22.0 live proof: the Birch speech classifies GCTX_TITLE (CB2_MainMenu is EM's
  new-game loop) and taps were DEAD there — the census promotion's walk-leak cure seen
  working on a real screen it was designed for.

## Lane A Entry 5 — LIST family live on EM: bag driver proven (boot 20260814-024128)

- **fmenu chain**: touch-tap on the START menu's BAG row (the shipped fmenu write-then-A)
  opened the bag — GCTX_BAG resolved (gdb ctx=7), the whole route driven by ONE pre-staged
  touch script after CONTINUE on the endgame save.
- **Drag scroll (L4)**: first drag scrolled 6 rows down (capture: list at REPEL..MAX ETHER,
  cursor mid-list, both scroll arrows up+down live: EM-bag-drag-scrolled.bottom.png);
  second drag returned scrollOffset to 0 — read live via g_touchDbg (lScroll 0, lRow 0,
  lTotal 16 = ITEMS 15+CANCEL, lMaxShowed 8 — the live template read working).
- **Tap-select (L3/P-A)**: tap on visible row 2 wrote selectedRow=2 + A -> "REPEAT BALL is
  selected." + GIVE/TOSS/CANCEL popup (EM-bag-rowtap-submenu-repeatball.bottom.png). The
  popup itself resolved GCTX_FIELDMENU (the L-C tier working over the list).
- **DEFECT found + FIXED: pocket-dot delta edges at 1-per-2-frames lost 2 of 3 edges** (the
  bag ignores switch keys during its ~16f swap anim) — landed on POKE BALLS instead of
  BERRIES. Fix: LIST_SEQ_FRAMES=24 pacing (committed; re-proof rides the next bag touch).
- Fling: exercised in-run (no isolated read window this boot — re-proven on FR next).
- Blank-row clamp: host-proven (115k-check sweep); live negative probe deferred.

## Entry 11 — LANE B: LeafGreen 13-anchor live verification COMPLETE (10/13 [exact])

The 305242f BPGE fix is REAL on the user's rev-1.1 save: ctx=fmenu/party/bag/b.oth/b.act/
b.move ALL fired on LG for the first time ever, each keyed on the exact shipped LG-rev1
value (startMenuTask 0x0806F204, partyTask 0x0811FB78 + cb2Upd/Init 0x0811EBF0/0x0811EC20,
bagHandler 0x08108F5C + cb2BagRun 0x08107F30, selMenu 0x08122CAC, multiTask 0x0809CC80,
BattleMainCB2 0x08011114 via a Route-8 trainer-sight battle, Task_MapNamePopup 0x08098194
caught in a 2s heartbeat window). Honest non-verdicts: chooseTarget (needs a double
battle), startCbInput (dead column — task-only detection since the callback-compare
retirement), and **yesNoTask = map-correct but BYPASSED by all four common yes/no flows**
(save→start-menu chain, nurse→multichoice task, bag-toss AND mart-buy→
`Task_CallYesOrNoCallback` 0x080BF548 [exact] ×2) → promotion rec: add 0x080BF548 as the
FRLG yes/no alternate. FULLUI harvest banked (intro/title/menu/options/dex/card/shop/buy
+ bag-list internals — the LK_BUY anchors Lane A's lists family needs). Logger caveat
discovered: FRLG quest-log playback leaks HISTORICAL map/pos rows into the gs ring.
Full table + ops routes: LANE-B-LG.md; 16 captures -> evidence/leafgreen/.
Mid-leg incident: a Lane-A rebuild coincided with the gdb stub dying ("emulator closed
the RSP socket") — one reboot lost; the snapshot-ELF hermetic rule kept all reads valid.

## Entry 12 — LANE B: Sapphire solo smoke + lane wrap

Sapphire solo boot: p1=AXPE matched in the PRIMARY seat, intro/overworld [exact] on the
sapphire map (incl. its drifted CB2_Overworld 0x080543C8), sb1 coherent, closed-loop D3
walk verified. Instance b stopped CLEAN (config byte-identical, fixtures untouched).
LANE B COMPLETE: all four tasks delivered — (1) Ruby first boot: AXVE fully live-verified
incl. battleMainCb 0x0800F808 [exact] + b.oth/b.act/b.move; (2) RS co-op: universe gate
ANSWERED (gameId 3/3, pairReason=0, CO-OP chip live) + the per-title Ruby/Sapphire ROM
drift headline; (3) LG: 10/13 anchors VERIFIED [exact] + the yesNoTask-bypass promotion
rec (add Task_CallYesOrNoCallback 0x080BF548) + FULLUI/LK_BUY harvest; (4) Sapphire smoke
green. Deliverables: LANE-B-RS.md, LANE-B-LG.md, evidence/{ruby,rs-coop,leafgreen,
sapphire}/ (36 captures), 4 commits (8d797df, f8f78cd-carried, 35105be, 997ede0, + this).

## Lane A Entry 6 — EM dex adapter live (boot 20260814-024742) + FR mart attempts

DEX (tier L-B): touch-tap on the START menu's POKEDEX row opened the dex; **GCTX_LIST /
LK_DEX resolved live** (gdb ctx=12 kind=4; evidence/impl/EM-dex-list-touch-opened.bottom.png);
drag scroll moved the dex's own cursor (dexSelected 0->1 read live; dexCount=202); the
**SELECT-SEARCH footer chip tap opened the search UI** (the SELECT pulse working —
EM-dex-search-chip-select.bottom.png). L21 derivation samples banked (count/selected/
initialVOffset/listVOffset via g_touchDbg+0x5C..0x68; note: the +0x62E listVOffset read 512
at one sample — the struct-offset walk past monSpriteIds needs a re-check before any
relative-tap formula ships, exactly why L21 keeps relative taps disabled). FINDING (same
class as the pocket-dot one): the dex ignores scroll-key edges landing inside its per-row
scroll anim — drag edges under-deliver; the dex prefers HELD keys (fling path). Follow-up:
pace LK_DEX drag edges ~8-10 frames or convert drag-to-steer for this screen.

FR mart (LK_BUY/LK_QTY live proof): attempt #1's first tap fired during the QUEST-LOG
replay window (FR continues into a cb2-blind playback — VISITED-firered.md:82 warned) and
the route diverged (player walked to the 2F direct-corner desk instead of the 1F stairs).
Attempt #2 restaged with +900 frames of tap delay + an idempotent stairs re-tap (in flight
as this entry is written). FR spawn recon banked: map (13,1) = Indigo Plateau Center 2F,
player (3,4); 1F mart clerk at (0,7), stairs warp (1,6)->1F(1,14) (pret map jsons, this
session's scratchpad frmaps/).

## Lane A Entry 7 — FR mart verdict + session close (instance a CLEAN)

FR mart attempt #2: SAME end position as #1 — and the recon boot (NO touch script at all)
also ends there. **Root cause: the QUEST-LOG REPLAY WALKS THE AVATAR.** The FR save's
post-continue truth is NOT the wake position the first field read shows — the grayscale
replay drives the player from (3,4) to **(14,2) on map (13,1)** before control returns,
so every route staged against the early-read position aims from the wrong square. Both
attempts' taps then walked benignly around the club lobby (no misfires into menus — the
ctx gates held). LK_BUY/LK_QTY live proof therefore moves to the morning list with the
exact re-plan banked: stage against (14,2), the 1F clerk sits at (0,7) with stairs
(1,6)->1F(1,14) (frmaps/ jsons); OR simpler, use a Viridian-side save. The FR-side family
code needs no change for this: the census already live-verified the FR rev1 naming cb2
[exact] and six rev1 anchors; the buy/qty/naming FR rows are sym-derived + host-pinned
(TEST 13/14) with the driver EM-proven.

Instance a returned CLEAN: azctl stop + clean-fixtures (originals re-hashed untouched,
user's recent.bin restored), settings.bin restored byte-identical from the .laneA.bak
(touchMode back to the user's PAD), control dir emptied, no azahar left running.

### Lane A scoreboard (for the morning report)
- KEYBOARD family: code + 115k-check host suite + **EMULATOR-PROVEN on EM end-to-end**
  (types PIKA -> charmap bytes CA C3 C5 BB read from the game's own buffer; page-swap,
  back, OK-commit "So it's PIK?"). FR: detection anchors census-live; geometry
  source-identical; live parity pass = morning item.
- LISTS family: generic driver + host suites; **EM bag live** (drag scroll + live template
  read + tap-select -> submenu), **EM dex live** (LK_DEX + chip -> SELECT + L21 samples);
  qty + pcitem detection host-proven via game_read (TEST 14), live pass = morning item
  (FR mart re-plan above; EM PC needs a Center route).
- 2 defects found live + 1 fixed in-code (pocket-tab pacing committed; dex drag pacing
  banked as follow-up), 3 harness landmines documented (movie tail, sdmc spawn snapshot,
  quest-log walk).

## Grid Entry 1 — the GRID family (PC storage boxes): spec + code + host proofs, all green

Session 2026-08-14 (overnight continuation subagent). SPEC-family-grid.md written first
(30-min mini-design from the pret pokemon_storage_system.c read — 10 059 lines, local clone —
+ the census E12 captures + the LOCAL pokeemerald.sym copy at
gba-toolkit/projects/rec2mp4/local/pokeemerald.sym). Headline design facts, all cited in the
spec: the five storage statics are CONTIGUOUS EWRAM (sCursorArea 0x02039D78 … origBoxPos
0x02039D7C; sStorage 0x02039D08 +4 inParty +5 boxOption); **the storage popups are the GLOBAL
sMenu** (HandleMenuInput -> Menu_GetCursorPos, :8024-8059) so the shipped fieldmenu
write-then-A driver drives them for free — gated by the AddMenu bottom-right fingerprint
(wl+ww==29 && wt+wh==15 + allocated window) because menu.c never clears sMenu.windowId (the
stale-window trap, spec §1.3); grid cells 24x24 from the icon-center math (100+24c, 44+24r).

Implementation (tap-tap-move, NO drag-and-drop):
1. touchgeom.{h,c}: storgeom_hit (grid/title/arrows/buttons/party rects, party-panel
   occlusion) + stornav_step (the engine's own per-area transition table, §1.2 cites).
2. gamestate.{h,c}: GCTX_STORAGE (appended, "stor") + 5 profile columns (EM sym-derived /
   verify-in-emulator except the census-[exact] storageCb 0x080C7D54; FR/LG/RS all-0 named
   degradations); detection = cb2 match BEFORE the cb2FullUi loop (the value STAYS in that
   list — TEST 11 untouched, TEST 15 pins the precedence).
3. touch.c: storage_update — closed-loop navigator (read live area/pos every frame, ONE
   d-pad edge per step with release gaps, arrival-settle delay 20f because SetCursorPosition
   updates the statics at slide START and HandleInput is not polled mid-slide, A only when
   live==target, 360f drop timeout = never a mis-slot A); popup delegation to the fmenu
   machinery; MOVE_ITEMS mode = emit nothing (named v1 limit); SELECT never emitted.
4. touch.h: TouchDbg storage mirror +0x80..+0xAC incl. the 30-bit current-box OCCUPANCY mask
   (hasSpecies bit) — the complete gdb move-proof channel (src bit clears, dst bit sets).
5. main.c: the TOUCH_DIAG_HUD ctx-name table (latent 9-entry OOB since 22.0) ->
   gamestate_ctx_name.

Suites: touchgeom 115 396 -> **119 659** (TEST 9 exhaustive 240x160x2 rect sweep vs a
restated oracle + engine-coordinate anchors; TEST 10 navigator convergence from EVERY start
to EVERY target over a pure-C model of the engine's handlers, incl. the never-leaves-the-box
G3 property + party cycling + the box->party unroutable drop), profiles 1480 -> **1519**
(TEST 15 columns + contiguity relation + storage-beats-fullui + FR named-degradation
behaviour). All 15 suites re-run green this session (celiolink/control/diag/fieldpath/
netlink/peersprite/presence/profiles/theme/tilt/touchgeom/trace_replay/typography/uigeom/
uihit); `make -j8` -> 3DGBA.3dsx 4 380 680 B, `make cia` -> 3DGBA.cia 2 017 216 B, no new
warnings. Emulator proofs (P-G1 recon + P-G2 the real move) = next, after the lane-A
deferred proofs.

## Grid Entry 2 — two NEW harness landmines banked (single-stage kills SMART; warps eat a walk token)

Live phase, 2026-08-14 ~04:05-04:15. Two lost boot pairs, both root-caused from logs:

1. **`--stage-roms NAME` (single) boots the app in SINGLE-GAME mode and SMART touch is
   FORCED TO PAD there** (control header `p2=----`; main.c:3343 `tmEff = single && SMART ?
   PAD : mode`, and D4's move_p2 seat does not exist). Every touch-proof boot MUST stage
   `NAME,NAME` (dual same-ROM — what lane A's `p1=BPEE p2=BPEE` headers actually were).
   Symptom to watch: g_touchDbg.seq frozen at 0.
2. **A warp consumes the WHOLE in-flight walk token** (`tok 9 d14 warp-complete
   26.28->26.14` — the d14 meant for the arrival map was spent crossing the door), and a
   door exit adds an auto forward-step that shows as `cross-drift`. Route grammar that
   works: give every door/stair crossing its OWN 1-tile token (`d1` through the door),
   `W300` for the load + auto-step, then plan the next leg from the warp coord ±1
   forward-step (doors drift, stairs don't).
   Empirical drift table from tonight's runs (all from control-log start coords):
   building EXIT door -> outside: arrival = warp coord, then ONE auto step out (drift +1
   in the exit direction, seen as `cross-drift` mid-token); outside -> INTERIOR door:
   arrival = the warp tile EXACTLY, NO auto-step (v2's u2 started at (7,8) = the mat);
   stairs/escalator: no drift either side. FR quest-log replay end measured live:
   (9,4) on map (13,1) (Indigo 2F) — NOT the (14,2) lane A recorded (their read was
   likely mid-replay); the replay is ~800-2600 EMULATED frames, so W3600+W1500 after
   CONTINUE covers it at any fps.

## Grid Entry 3 — 🎉 FR MART LK_BUY + LK_QTY LIVE-PROVEN (deferred proof #1 closed)

Boot runs-b/20260814-042757 (instance b, dual FR rev1, movie + pre-staged D4+touch).
Route that finally worked (replay-end anchor (9,4) on Indigo 2F, measured live): d2 l8
(the l8's last step lands ON the stairs (1,6) -> warp-complete) -> 1F arrival (2,14)
(stairs drift +1 x) -> u2 r1 u5 l1 L a -> the clerk ACROSS the counter -> a = BUY ->
**ctx flipped to GCTX_LIST live (gdb monitor, value 12) = LK_BUY detected on the user's
FR rev1** (find_list_task via Task_BuyMenu rev1 0x0809BBD4 + data[7]). The pre-staged
touch ops then ran the whole §P-E chain BLIND and the capture proves the outcome:
**evidence/impl/FR-mart-qty13-confirm.bottom.png — "FULL RESTORE, and you want 13.
That will be ₽39000. Okay?" with the YES/NO up.** 13 = EXACTLY the gesture arithmetic
(qty roller: vertical drag = +3 ones, horizontal drag = +10, then the clean tap = A
confirm) — LK_QTY's drag/±10/tap-A semantics live-proven in one number. No purchase
made (the run stopped at the unanswered confirm; fixture save unmutated).

Bonus harvest, live [exact] on the CONFIRM stage (gs-ring rows read over gdb):
- FR rev1 `Task_CallYesOrNoCallback` = **0x080BF574** (the LG bypass finding's FR
  sibling) is the ONLY input task on the confirm screen (+ ListMenuDummyTask
  0x08106F44 = the shipped lmDummyTaskAlt, still resident). NOT promoted into
  yesNoTaskAlt: on the buy screen the same task may be resident during the LIST phase
  too, and the fieldmenu test runs before the buy test — promotion would risk stealing
  the whole buy screen into GCTX_FIELDMENU. Flagged for a dedicated slice with a
  list-phase task snapshot.
- The P-D DISCOVERY PROBE fired live on the confirm screen (g_touchDbg+0x6C):
  probeListBase 0x030050E8 (gTasks slot 2), totalItems 8, maxShowed 6, windowId 4 —
  the SPEC-family-lists P-D proof, delivered by the FR mart for free.

TWO more landmines banked: (1) `azctl stop` KILLS the app before the SD ring dumps
flush -> the touch/gs logs of a stopped session are LOST; live gdb reads + captures are
the only reliable evidence channel unless the session ends via the app's own Quit.
(2) A mid-run mirror read samples ONE moment — the ctx flap I chased (12 -> 10) was
just the confirm stage; read the gs RING (s_gsLog over gdb) for history, not the
instantaneous mirror.

## Promotion Entry 1 — lane-B verdicts FOLDED into the profiles, all suites green

Serial code slice (subagent, 2026-08-14 ~06:20). Sources: LANE-B-RS.md + LANE-B-LG.md.
What landed (source/gamestate.{h,c} + test/host/test_profiles.c only; frozen paths untouched):

1. **BPGE (LANE-B-LG.md):** the row comment now carries the live verdicts — 10 of the 13 rev1
   primaries marked LIVE-VERIFIED [exact] (party/bag/fmenu ctxs fired on LG for the first time;
   full b.oth→b.act→b.move battle chain; mapNameTask caught in a heartbeat row); the 3 honest
   non-verdicts stay flagged AS-IS (chooseTarget = needs a double battle; startCbInput =
   unused-by-code; yesNoTask = map-correct, LIVE-UNREACHED). The #8 bypass finding is now CODE:
   **yesNoTaskAlt = Task_CallYesOrNoCallback 0x080BF548** (LG rev1, live [exact] TWICE —
   bag-toss + mart-buy confirms), displacing the never-fired rev0 Task_YesNoMenu alternate
   (bypass is a code-structure property of FRLG, not a revision; displacement cost = zero,
   named in the comment). The two money-and-item confirm screens stop reading as `field`.
   LG's TITLE/FULLUI spot-harvest stays UNPROMOTED (its own slice; comment updated honestly).
2. **RS (LANE-B-RS.md):** RS_PROFILE_BODY split into RS_PROFILE_BODY_RAM + _TAIL with the
   cb2Title/cb2FullUi lists written PER ROW — the drift headline made structural. AXVE gets
   Ruby's live-read [exact] set (title: MainCB2_Intro 0x0813B7B8 / MainCB2 0x0807C474 /
   CB2_MainMenu 0x080096C4; fullui: CB2_PartyMenuMain 0x0806AEFC + bag run 0x080A3138); AXPE
   gets SAPPHIRE's OWN measured set (0x0813B7B8 / 0x0807C478 (+4 drift) / 0x080096C4; fullui:
   bag 0x080A3138 only — the party slot stays EMPTY, named degradation: Ruby's value resolves
   inside Task_ResetRtcScreen on the sapphire map, never copied across). Stale row comments
   fixed: battleMainCb 0x0800F808 promoted to LIVE-VERIFIED-in-emulator (lane-B wild battle,
   ctx left field; hw still owed), "no RS ROM on this machine / never EXECUTED" replaced with
   the lane-B execution record (both titles booted, P3.6 co-op proof ran, pairReason=0).
3. **Suites:** TEST 5 rewritten (shared RAM body pinned with the cb2 lists excluded + the
   drift itself pinned: intro/mainmenu identical-measured, title +4, Ruby's party cb2 banned
   from every Sapphire slot); TEST 10 pins the new BPGE yesNoTaskAlt; TEST 11 pins both RS
   per-title lists exact (visited=1); TEST 12 grew (g) RS behaviour — per-title classes
   classify positively AND cross-title drifted values fall through unresolved on BOTH titles —
   and (h) Task_CallYesOrNoCallback → GCTX_FIELDMENU with the rev1 primary still detecting.
   test_profiles 1295 → **1480 checks, 0 failures**.

Gate (fresh runs this slice): celiolink 1259 · control 6940 · diag 376 · fieldpath 1808 ·
netlink 66 · peersprite 62078 · presence 61376 · profiles **1480** · theme 83444 · tilt 1723 ·
touchgeom 115396 · trace_replay 58 (4 loud SKIPs) · typography 1419 · uigeom 18332 · uihit
1834 = **356 589 checks, 0 failures** (15 suites). `make -j8` → 3DGBA.3dsx 4 377 424 B;
`make cia` → 3DGBA.cia 2 015 168 B (both clean 06:23; only the pre-existing frozen-mgba
warnings). Note: test_profiles' link line now needs source/peersprite.c (presence_read
references pspr_capture since phase 20; the header comment's 4-file line no longer links).

## 2026-08-14 — USER DESIGN CALLS (binding): overworld gestures

See DECISIONS-overworld-gestures.md. Two calls, both from the user this morning:
D1 tap-self = START, HOLD-self = SELECT (replaces tap=A / double-tap=START; double-tap was
   offered and rejected because it taxes every single tap with the double-tap window).
D2 tap-to-walk RUNS when the routed path is >= ~4 tiles, WALKS when shorter — with silent
   degradation to walking whenever running is not permitted (shoes flag / map type / surf /
   bike / dash-cancelling terrain), decided PER LEG for traversal routes.
Mechanism precedent: control.c:151's sprint mode already holds KEY_B in the direction mask; the
touch route follower simply never does. Implement in the overworld handler + the route-PROGRAM
layer. Not yet scheduled — pick this up in the next slice after the phase-23 lanes land.

## Lane A Entry 6 (phase 23) — TRAVERSAL emulator-proof RECON: what the user's save can actually prove

Desk work before any boot (2026-08-14 ~09:40), because the SPEC's P1-P3 spots assume a
save that this project does not have. Method: parse the fixture `.sav` on the HOST and read
the pret map data — no emulator, no guessing. Two scratchpad tools (kept, they are the
recon rail for every future traversal arc):

- `savepeek.py <sav> EM|FR` — walks the Gen-3 save-slot footer (`sig 0x08012025`, highest
  `saveIndex` wins), concatenates sections 1..4 into the SaveBlock1 image, then reads
  `pos`/`location` (+0x00/+0x04), the badge flags (`flags` @0x1270 EM / @0x0EE0 FR, bit
  `f&7` of byte `f>>3`), `playerPartyCount` @0x234 and the party @0x238 — decrypting each
  mon exactly as `fieldtrav.c` does on hardware (substruct order = `personality % 24`, u32
  XOR `personality ^ otId`, **the 24-u16 checksum verified before a move id is believed**).
- `mapdump.py` / `reach.py` — the pret `layouts.json` -> `map.bin` -> tileset
  `metatile_attributes.bin` chain (behaviour = `attr & 0xFF`), rendered as a walk/surf grid,
  plus a fieldpath-rule BFS (collision 0 + elevation equal-or-either-0/15) that reports
  every land tile reachable on foot that TOUCHES surfable water = a legal SURF mount.

**Verdict on `roms/emerald.sav` (the fixture the harness stages):**

| Gate | Reading |
|---|---|
| Position | **(7,8) on map (26,28) = `BattleFrontier_BattleArenaLobby`** — not Littleroot, not Route 103 |
| Badges | **all 8 set** (flags 0x867..0x86E all 1) — every HM badge gate passes |
| Party | 6 mons, all checksum-OK, none an egg |
| HM moves present | **SURF only** (mon 5, species 329). No CUT, no ROCK SMASH, no STRENGTH, **no FLY** |

Consequences, stated honestly rather than worked around:

1. **P1 (Surf) is provable; P2 (Cut) and P3 (Rock Smash) are NOT** on this save as positive
   proofs — `fieldtrav`'s eligibility will (correctly) refuse to plan them, which makes them
   this arc's **negative controls** instead. A positive Cut/Smash proof needs an HM taught
   first (the bag TM/HM pocket + the party teach flow — its own arc), or a different save.
2. **No FLY ⇒ the Hoenn mainland is unreachable from the Battle Frontier island.** SPEC T6's
   Route 103 / Route 116 / Rusturf / **Lavaridge** spots are all off-limits from this save
   without a save edit. Slice 2's Lavaridge scenario therefore cannot be run as written; the
   excursion machinery has to be proven on a map pair that IS reachable.
3. The proof spot that IS reachable: **`BattleFrontier_OutsideEast` (26,14)**, out of the
   arena lobby's single warp (lobby (7,12) -> OutsideEast warp 1 at (39,29), +1 auto-step
   out = **(39,30)**). `reach.py` from there: 1688 dry-reachable tiles, and the **nearest
   land-adjacent surfable water is 28 tiles away** — the south beach at y=58.
4. The crossing chosen, read off the real layout: at y=58 land runs x=30..47 (elev 3),
   **ocean water `MB_OCEAN_WATER` 0x15 runs x=48..56 (elev 1)**, land resumes at x=57
   (elev 3). Standing on **(47,58)** the mount tile is due EAST — a 9-tile channel with a
   far shore, i.e. the live twin of the host suite's Route 117 crossing.
5. Tap geometry, pinned: settings have `scaleMode[1] = SCALE_1X`, so `touch_to_gba` is a
   pure translate — **gba = screen - (40,40)** — and the tapped tile is
   `(player + (gx/16 - 7, gy/16 - 5))` (touch.c:432). So a tile offset (ddx,ddy) is the
   screen point `(40 + (7+ddx)*16 + 8, 40 + (5+ddy)*16 + 8)`. Reachable offsets are
   **ddx -7..+7, ddy -5..+4** — a tap can never name a tile further than that, which is why
   a 28-tile approach has to be walked by D4 first.
6. Route to the shore, BFS'd around the map's 26 object events:
   `d4 r1 d7 r6 d7 r1 d1 r1 d1 r3 d2 r1 d2 l1 d1 l4 d3` = 46 steps, (39,30) -> (47,58).

## Lane A Entry 7 (phase 23) — two more harness landmines, both paid for in lost boots

Both found driving the P1 surf arc from the Battle Frontier save; both are general, not
traversal-specific, so they belong beside the other four.

**5. `b` does NOT close a Gen-3 message box — only `a` advances it.** The obvious-looking
"press A a few times to get through intro → title → main menu, then B to clear anything I
opened by accident" prefix is a trap: the SECOND `a` landed in the loaded overworld, the
player's saved facing pointed at the lobby's BLACK_BELT (7,7), and the resulting dialog ate
the rest of the boot. The two `b` tokens did nothing at all — in Gen 3, B never dismisses a
textbox. Control log, verbatim:
`tok 9 a done f=1845` … `tok 15 d3 start (7,8)->(7,11) f=3671` … `TIMEOUT tok 15 d3 at (7,8)`.
**The rule: the boot prefix must contain exactly ONE `a`, and every press before it must be
`s` (START).** START skips the GF intro, opens the title, and is INERT on the main menu, so
a whole ladder of `s` tokens is free insurance; the single `a` is then the only key that can
reach the overworld, and it lands on CONTINUE. Measured on this save: main menu is up by
f≈1500, so `W600 s W300 s W300 s W300 s W300 a W1800` is comfortable at any fps.

**6. A `d1` walk token onto a SOUTH-ARROW warp arrives and STOPS — it never fires the warp.**
The Battle Arena lobby's exit (7,12) is `MB_SOUTH_ARROW_WARP` (**0x65**, resolved off pret's
`metatile_behaviors.h`), not a door: the game warps you only when you press the arrow's
direction WHILE STANDING ON IT. A closed-loop walk token's job is finished the moment the
coords match, so it releases the key on arrival and the script then sat on the tile for 600
frames (`p(7,12)` on map (26,28) at f=4094→4332, unchanged). This is exactly fieldpath's
**WK_DIR** class (stand ON it, hold its direction) seen from the harness side.
**The rule: an arrow-warp crossing costs TWO tokens** — `d1` onto the tile, then a second
`d1` whose DOWN press fires the warp (and which the warp then consumes whole, per landmine
"a warp eats the in-flight token"). Doors (`MB_ANIMATED_DOOR` 0x69 / `MB_NON_ANIMATED_DOOR`
0x60) still take one. Cheap pre-flight: dump the exit tile's behaviour from the pret layout
before writing the route — 0x65/0x66/0x67/0x68 (the four arrow warps) mean two tokens.

## Lane A Entry 8 (phase 23) — landmine 7: never `make` while an emutest session is live

Cost: one poisoned poll window. The harness resolves `g_fieldDbg` (and every other symbol)
out of `3DGBA.elf` **at read time**, not at boot time. A `make -j8` run while Azahar is
still executing the OLD `.3dsx` relinks the ELF and the same symbol moves —
`g_fieldDbg+0x50` went **0x005eccc4 -> 0x005f2fcc** mid-session — so every subsequent
`gdbio read` silently returns whatever now lives at the new address (zeros, in this case).
The failure looks exactly like "the app died", which is the expensive part.

Two ways out, both cheap: (1) the lane-B recipe — snapshot ELF+3dsx before the boot and
point `EMUTEST_ELF`/`EMUTEST_APP` at the snapshot; or (2) what this session did once it
noticed — resolve the symbol ONCE at boot and then read the RAW ADDRESS for the rest of the
run (`gdbio read 0x005eccc4 80`). The scratchpad poller now takes `FD_BASE` for exactly that.

## Lane A Entry 9 (phase 23) — SLICE 2 IS HOST-PROVEN ON THE USER'S OWN ROM (TEST 15/16)

`fieldtrav_excursion` + the ROM map graph landed and are graded by two host tests that do
something new for this suite: they open **`roms/emerald.gba` at run time** and walk the real
`gMapGroups -> MapHeader -> MapEvents/MapLayout -> warps + grid` chain instead of a
compiled-in fixture (loud SKIP when the ROM is absent). test_fieldtrav 1032 -> **1066
checks, 0 failures**; fieldpath UNMODIFIED at 1808.

What TEST 15 pins, all read from the cartridge the user actually plays:
- gMapGroups **0x08486578** resolves (26,14) to a 72x72 layout with **14 warps**, warp 1 at
  **(39,29) -> (26,28)** — the exact warp the live arc crossed, arriving at (39,30).
- the lobby (26,28) has ONE warp, at **(7,12)**, targeting OutsideEast warp 1 — the
  `MB_SOUTH_ARROW_WARP` that cost a boot to learn about (Entry 7).
- the ROM-grid bus adapter is graded by running **fieldpath's own reads** through it:
  (39,29) = MB_ANIMATED_DOOR 0x69 and not walkable, (39,30) walkable, (50,58) =
  MB_OCEAN_WATER 0x15. Off-map reads refuse rather than guess.

TEST 16 is the Lavaridge-class proof **relocated to a map this save can reach**:
`BattleFrontier_OutsideWest`'s RECEPTION GATE. Two door tiles — **(26,61)** north
(MB_SOUTH_ARROW_WARP 0x65) and **(26,65)** south (MB_NON_ANIMATED_DOOR 0x60) — with the
9x14 gate building between them and no way around. Premise proven first (the dry router
cannot cross), then the excursion: out through the north warp, across the gate from (4,1)
to (4,13), back out at (26,65). Symmetric in the other direction; a goal inside the gate's
wall reports `excursion-none` and plans NOTHING.

One real bug the tests caught in the HARNESS, worth remembering for every future map test:
the "live" bus must fall through to ROM. The map header and both tilesets are ROM reads even
for the CURRENT map, so a bus that only answers the grid makes `fieldpath_classify` silently
blind — it returns kind=NONE for every warp and the whole search comes back empty.

## Lane A Entry 10 (phase 23) — the OutsideEast walk dies at EXACTLY 6 steps, twice

Not randomness — a step-counted script. Two independent attempts, two different routes:

| attempt | route after the warp | stopped at | steps walked |
|---|---|---|---|
| 2 | `d4 r1 d7` (down column 40) | (40,35), inside `d7` | 4+1+1 = **6** |
| 3 | `d11` (down column 39) | (39,36), inside `d11` | **6** |

Both left a field MESSAGE BOX open (captured: a small Pokemon-shaped portrait plus
"…………" and the ▼ prompt) with the avatar frozen, and in both cases the ROM's own
blockdata — re-read from the cartridge through `gMapGroups`, not just from pret, after the
pret data was (wrongly) suspected — says the tile ahead is **collision 0, elevation 3, i.e.
walkable**. So it is a script, and it fires on a step counter shortly after the map load
(the Match-Call family of "walked N steps outdoors" triggers is the obvious candidate).

Consequences for every long D4 route on a late-game save:
1. **A closed-loop walk token cannot survive it.** The box blocks movement, the token's
   `CTL_WALK_DL(n) = n*60+240` deadline expires, and a TIMEOUT aborts the whole script.
2. **The fix is to put the boundary between tokens.** Both attempts COMPLETED their 6th
   step before freezing, so a route whose first outdoor tokens sum to exactly 6 tiles ends
   cleanly, and a run of `a` tokens right after it advances the box (remember Entry 7: only
   `a` advances a Gen-3 textbox, never `b`).
3. Place those `a` tokens where the avatar faces open ground, so an A that finds NO box
   open does nothing instead of starting an NPC conversation.

## Lane A Entry 11 (phase 23) — slice 2's LEG MACHINE, and what is still owed

`touch.c` now executes an excursion as three legs with a boundary check, not as one plan.
The shape worth remembering (it is the reusable pattern for ANY multi-map touch feature):

- Every leg is an **ordinary fieldpath route to a warp tile**, so the shipped terminal
  semantics (door hold / arrow hold / step) fire the warp. There is no second walker, and
  nothing about the phase-18 router changed.
- The phase-18 **warp kill-switch IS the leg boundary**. `SaveBlock1.location` changing is
  still the only trusted signal; with an excursion running it is compared against the map
  the plan predicted, and any other map ends the excursion where it stands. That is exactly
  what the kill-switch was built for — it just gained one legal continuation.
- The next leg is **re-planned on the LIVE grid at arrival**, never executed from the ROM
  plan. The ROM plan has no object events in it, so an NPC standing in the doorway is
  invisible at plan time and only the live re-plan can see it.
- Tier order is now three deep and structural: `walk_plan` (dry) -> `prog_plan` (HM edges)
  -> `exc_plan` (excursion), each consulted only when the previous DECLINED, and the last
  only at `smartTraverse >= 2`.

**Still owed for slice 2** (named honestly rather than implied done):
1. an emulator proof of the leg machine. The planner is proven on the user's ROM (TEST 16),
   but the executor has never run — it needs a save standing near a two-door building, and
   the only one this save can reach is the reception gate on `BattleFrontier_OutsideWest`,
   which is across the map CONNECTION from OutsideEast (~40 tiles of walking the router
   refuses to plan, T5.9). The cheap unlock is the same one P1 needs: **walk once, then
   SAVE in-game**, after which every arc starts where the proof needs it.
2. Waterfall (the other half of the slice) — the up-edge is cheap now that the SURF layer
   exists, but there is no waterfall this save can reach either.
3. `progMapSeq` carries the leg index; the fplog `progSeq/step/hm` columns do not yet carry
   a leg column of their own.
