#!/bin/bash
# smoke.sh — THE phase-16 gate (slice E4 final form; SPEC-harness H2.7/H3, PHASE inv. 4):
# per-channel PASS / FAIL / SKIP(reason), exit 1 iff any FAIL (SKIPs never fail the gate
# but are always printed — the phase-13 verdict.sh rule).
#
# Channels and what each PROVES live (design sources cited inline):
#   run        E1: profile apply -> bundle launch -> readiness -> stop -> byte-identical
#              config restore (H1.5/H3.2).
#   read-state E2: gdbio broker over the one-client-per-boot release stub — resume (the
#              release parks EVERY use_gdbstub boot pre-first-instruction, core.cpp:574),
#              verify-base 3/3 ELF anchors, renderSeq ~60 Hz, g_appActive/g_quit (H3.3/H3.4).
#   see        E4: Quartz window lookup + screencapture + S4.1 crops; SKIP 75 while Screen
#              Recording is missing (H2.4; grant verified working 2026-08-08).
#   press-ctm  E3: Tier A zero-permission closed loop — synthesized movie drives the
#              ROM-less session's pause menu: tiltLevel 0->3 (gdb-read), menu QUIT ->
#              app exit closes the RSP session (H3.5 as corrected in BUILDLOG E3).
#   press-d4   E4 (--rom only): Tier B — fixtures staged, movie drives the REAL ROM picker
#              into a dual-core session; pre-dropped move_p1.txt is consumed (remove-on-
#              pickup ACK, main.c:460), s_ctlStat pickup counter flips, control log says
#              "picked up" (H3.6).
#   sdmc       E4 (--rom only): the read-side of the phase-13 bridge — the quit-time
#              3DGBA_gs_*.txt appears (gs_dump(-1), main.c:4147; non-empty because a
#              Pokemon session captures rows, gamestate.c:348) with a well-formed header.
#              The D3 CSV is NOT expected (wireless-only, diag.h:312-313 — H4.4).
#
# Flags (H2.7): --no-live (preflight only, every live channel SKIPs), --rom (arm Tier B;
# without it press-d4/sdmc SKIP — they need the user's dual-gba ROM fixtures).
set -u

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
AZ_DATA="${EMUTEST_AZ_DATA:-$HOME/Library/Application Support/Azahar}"
AZ_CFG="$AZ_DATA/config/qt-config.ini"
AZ_SD="$AZ_DATA/sdmc"
AZ_BIN="${EMUTEST_AZ_BIN:-$HOME/Applications/Azahar.app/Contents/MacOS/azahar}"
APP="${EMUTEST_APP:-$REPO/3DGBA.3dsx}"
RUN="$HERE/run"
PY="$HERE/.venv/bin/python"

NO_LIVE=0
ROM=0
for a in "$@"; do
  case "$a" in
    --no-live) NO_LIVE=1 ;;
    --rom) ROM=1 ;;
    *) echo "usage: smoke.sh [--no-live] [--rom]" >&2; exit 2 ;;
  esac
done

declare -a ROWS
FAILED=0
row() { ROWS+=("$(printf '%-10s %-7s %s' "$1" "$2" "$3")"); [ "$2" = FAIL ] && FAILED=1; }

fail_run() { row run FAIL "$1"; finish; }
finish() {
  echo
  printf '%-10s %-7s %s\n' CHANNEL VERDICT DETAIL
  for r in "${ROWS[@]}"; do echo "$r"; done
  exit $FAILED
}

# --- S1 preflight (H3.1, host) ------------------------------------------------------------
[ -x "$AZ_BIN" ] || fail_run "AZ_BIN missing: $AZ_BIN"
VER="$(plutil -extract CFBundleShortVersionString raw "${AZ_BIN%/Contents/MacOS/*}/Contents/Info.plist" 2>/dev/null || echo unknown)"
[ "$VER" = "2125.1.2" ] || echo "smoke: WARNING — Azahar $VER != pinned 2125.1.2 (recorded; never update mid-investigation, H5)"
[ -f "$APP" ] || fail_run "APP missing: $APP — export DEVKITPRO=/opt/devkitpro DEVKITARM=\$DEVKITPRO/devkitARM && make -j8"
[ -f "$REPO/3DGBA.elf" ] || echo "smoke: WARNING — 3DGBA.elf missing (E2's symbol source); same make line regenerates it"
[ -x "$PY" ] || fail_run ".venv missing — run $HERE/setup.sh"
[ -f "$AZ_CFG" ] || fail_run "qt-config.ini missing at $AZ_CFG (run Azahar once manually)"

if [ "$NO_LIVE" = 1 ]; then
  row run       SKIP "--no-live: preflight only (Azahar $VER, app + venv present)"
  row read-state SKIP "--no-live: gdbio needs a live boot"
  row press-ctm SKIP "--no-live: needs a live boot"
  row press-d4  SKIP "--no-live (and Tier B additionally needs --rom)"
  row sdmc      SKIP "--no-live (and Tier B additionally needs --rom)"
  row see       SKIP "--no-live: needs a live window"
  finish
fi

# Tier-A movies REQUIRE the ROM-less state (E3 follow-up 2): a previous aborted Tier-B
# run may have left fixtures staged — clean them (originals are hash-verified first).
if [ -f "$HERE/state/fixtures.json" ]; then
  echo "smoke: stale ROM fixtures staged — cleaning before the Tier-A phases"
  "$RUN" azctl clean-fixtures || fail_run "stale fixture cleanup failed (see above)"
fi

# =========================================================================================
# Phase 1 — run + read-state + see (one ROM-less boot serves all three)
# =========================================================================================
SNAP="$(mktemp -t emutest-cfg-snap)"
cp "$AZ_CFG" "$SNAP"

if ! "$RUN" azctl boot; then
  rm -f "$SNAP"; fail_run "azctl boot failed (see run dir events.log)"
fi

STATUS="$("$RUN" azctl status)"
echo "$STATUS" | sed 's/^/smoke: status: /'
DETAIL="$(echo "$STATUS" | grep -o 'pid=[0-9]*' | head -1)"
LRD1="$(cat "$HERE/state/last_run" 2>/dev/null)"
BOOTJ="$(cat "$LRD1/boot.json" 2>/dev/null || echo '{}')"
T_GDB="$(echo "$BOOTJ" | "$PY" -c 'import json,sys; print(json.load(sys.stdin).get("t_gdb_port_s","?"))')"

OK=1
echo "$STATUS" | grep -q '^process=ours' || { echo "smoke: FAIL status: not ours"; OK=0; }
echo "$STATUS" | grep -q '^profile=APPLIED' || { echo "smoke: FAIL status: profile not APPLIED"; OK=0; }
echo "$STATUS" | grep -q '^gdb_port=open' || { echo "smoke: FAIL status: gdb stub not listening"; OK=0; }
# The backup must equal the pre-run user config byte-for-byte (invariant 2).
cmp -s "$SNAP" "$HERE/state/qt-config.ini.bak" || { echo "smoke: FAIL backup != pre-run config"; OK=0; }

# --- read-state (E2: resume is MANDATORY — the release stub parks every boot) ------------
RS_OK=1
"$RUN" gdbio resume                       || RS_OK=0
VB="$("$RUN" gdbio verify-base)" && echo "$VB" | sed 's/^/smoke: /' || { echo "$VB" | sed 's/^/smoke: /'; RS_OK=0; }
# renderSeq needs the app's emulated boot to reach the render loop (~10 s after resume
# on this machine, E2 measurement) — poll generously, PASS on first change.
PL="$("$RUN" gdbio poll g_renderSeq --changed --timeout 30 --interval 1000)" \
  && echo "$PL" | sed 's/^/smoke: /' || { echo "$PL" | sed 's/^/smoke: /'; RS_OK=0; }
"$RUN" gdbio read-u8 g_appActive | grep -q '= 1 ' || { echo "smoke: FAIL g_appActive != 1"; RS_OK=0; }
"$RUN" gdbio read-u8 g_quit      | grep -q '= 0 ' || { echo "smoke: FAIL g_quit != 0"; RS_OK=0; }
SEQ="$(echo "$PL" | grep -o 'changed [0-9]* -> [0-9]*' || true)"

# --- see (E4): capture the live window, crop per S4.1, verify content --------------------
# Runs while the app is provably rendering (renderSeq just advanced). Exit 75 = the
# Screen Recording SKIP (H2.4); content check = crops exist, keep the 400:240 / 320:240
# aspect, and are not blank (the app HUD is always drawn — probed E4).
SEE_DIR="$LRD1/see"
mkdir -p "$SEE_DIR"
SEE_ROW_V=""
SEE_ROW_D=""
SEE_OUT="$("$RUN" see shot both "$SEE_DIR/screen.png" --raw-window "$SEE_DIR/window.png" 2>&1)"
SEE_RC=$?
echo "$SEE_OUT" | sed 's/^/smoke: /'
if [ "$SEE_RC" = 0 ]; then
  SEE_CHK="$("$PY" - "$SEE_DIR/screen.top.png" "$SEE_DIR/screen.bottom.png" <<'PYEOF'
import sys
from PIL import Image
ok, parts = True, []
for path, w0, h0, name in ((sys.argv[1], 400, 240, "top"), (sys.argv[2], 320, 240, "bottom")):
    im = Image.open(path).convert("L")
    w, h = im.size
    if abs(w * h0 - h * w0) > max(w0, h0):        # cross-multiplied aspect check (+-1px trunc)
        ok = False; parts.append("%s ASPECT OFF %dx%d" % (name, w, h))
        continue
    hist = im.histogram()
    frac = sum(hist[16:]) / float(w * h)          # non-black fraction (HUD text/borders)
    parts.append("%s %dx%d %.1f%% lit" % (name, w, h, 100 * frac))
    if frac < 0.003:
        ok = False; parts.append("%s BLANK" % name)
print("; ".join(parts))
sys.exit(0 if ok else 1)
PYEOF
)"
  if [ $? = 0 ]; then
    SEE_ROW_V=PASS; SEE_ROW_D="window captured + cropped: $SEE_CHK (crops in $SEE_DIR)"
  else
    SEE_ROW_V=FAIL; SEE_ROW_D="capture ok but crops wrong: $SEE_CHK"
  fi
  echo "smoke: see crops: $SEE_CHK"
elif [ "$SEE_RC" = 75 ]; then
  SEE_ROW_V=SKIP; SEE_ROW_D="Screen Recording not granted -> System Settings grant + restart the host app"
else
  SEE_ROW_V=FAIL; SEE_ROW_D="see shot rc=$SEE_RC (see smoke output above)"
fi

"$RUN" gdbio detach || RS_OK=0            # resume + retire the boot's one gdb client

"$RUN" azctl stop || OK=0
pgrep -x azahar >/dev/null && { echo "smoke: FAIL azahar still running after stop"; OK=0; }
if cmp -s "$SNAP" "$AZ_CFG"; then
  RESTORED="config restored byte-identical"
else
  echo "smoke: FAIL qt-config.ini not byte-identical after stop"; OK=0; RESTORED="RESTORE MISMATCH"
fi
rm -f "$SNAP"

if [ "$OK" = 1 ]; then
  row run PASS "boot->status->stop; $DETAIL gdb-ready ${T_GDB}s; $RESTORED"
else
  row run FAIL "see smoke output above + run dir events.log"
fi
if [ "$RS_OK" = 1 ]; then
  row read-state PASS "resume; verify-base 3/3 anchors; renderSeq $SEQ; g_appActive=1 g_quit=0; detach"
else
  row read-state FAIL "see smoke output above (gdbio broker log: $HERE/state/gdbio-broker.log)"
fi

# =========================================================================================
# Phase 2 — press-ctm (E3, Tier A): the zero-permission closed loop
# =========================================================================================
# Movie = tests/fixtures/movie_menu_tilt_quit.json, synthesized fresh by ctm.py per run.
# What it does inside the ROM-less app session (all source facts probed in slice E3):
#   no ROMs in sdmc:/3DGBA -> rompicker_run returns false IMMEDIATELY (scan_roms n==0,
#   rompicker.c:194) -> main.c:4448 falls through to run_session with the default paths ->
#   cores NULL, the session render loop runs anyway (that is the ~60 Hz g_renderSeq E2 saw).
#   [wait 1200f]           past splash (170f auto-end, main.c run_splash) + app boot margin
#   [START+SELECT 40f]     the pause-menu combo (main.c:2700-2704; kHeld both)
#   [touch 40,110]         tab rail -> ENHANCE (t2=(py-8)/30=3; main.c:3195)
#   [touch 250,210]        TILT seg index 2 (PT_ENHANCE row 5: x140 y198 w170 h26 nseg 4,
#   [touch 290,210]        then index 3) -> g_prefs.tiltLevel = 2 then 3 (main.c:3220-3226)
#   [touch 40,20]          tab rail -> SESSION (t2=0)
#   [touch 200,125]        the QUIT button (PT_SESSION row 2: x93 y108 w216 h43, ACT_QUIT=18)
#                          -> SESSION_QUIT -> main returns -> the emulated app exits
# Observables (zero permission): g_prefs.tiltLevel 0x1c into g_prefs (theme.h:38-53) flips
# to 3 = THE state-global change; then the app quits = azahar's stub socket dies;
# azahar_log carries "Loaded Movie, ID:" (release movie.cpp:551).
# settings.bin note: the menu taps settings_save into sdmc:/3DGBA/settings.bin (harness-
# writable fixture dir, H4.2) — snapshotted before boot and restored after, so the user's
# emulator-side prefs survive smoke byte-identically.
PC_OK=1
SETTINGS="$AZ_SD/3DGBA/settings.bin"
SET_SNAP=""
if [ -f "$SETTINGS" ]; then SET_SNAP="$(mktemp -t emutest-settings)"; cp "$SETTINGS" "$SET_SNAP"; fi
MOVIE="$(mktemp -t emutest-movie).ctm"
SNAP2="$(mktemp -t emutest-cfg-snap2)"
cp "$AZ_CFG" "$SNAP2"
BLOG="$HERE/state/gdbio-broker.log"
B0="$(wc -l < "$BLOG" 2>/dev/null | tr -d ' ' || echo 0)"; B0="${B0:-0}"

"$RUN" ctm make "$HERE/tests/fixtures/movie_menu_tilt_quit.json" "$MOVIE" || PC_OK=0
# azctl --movie auto-pins is_new_3ds=false: on the N3DS model libctru's hidInit starts the
# ir:rst movie consumer whose interleave is unsynthesizable ("Expected to read type 4"
# desync — E3 live finding; azctl cmd_boot comment has the full cite chain).
if [ "$PC_OK" = 1 ] && "$RUN" azctl boot --gdb --movie "$MOVIE"; then
  "$RUN" gdbio resume || PC_OK=0
  TILT_BEFORE="$("$RUN" gdbio read-u32 g_prefs+0x1c | grep -o '= [0-9]*' | head -1 | cut -d' ' -f2)"
  echo "smoke: tiltLevel before movie taps: ${TILT_BEFORE:-?}"
  # The tilt taps land ~24 s into EMULATED time; poll wide (halt->read->cont blinks).
  PT="$("$RUN" gdbio poll g_prefs+0x1c --expect 3 --timeout 120 --interval 1000)" \
    && echo "$PT" | sed 's/^/smoke: /' || { echo "$PT" | sed 's/^/smoke: /'; PC_OK=0; }
  # Quit phase (~3 s emulated after the tilt change). PROVEN observable (E3 live run):
  # the emulated app's exit closes the gdb stub's TCP session -> the broker exits with
  # "emulator closed the RSP socket" while the Azahar shell stays alive. Also accept a
  # full azahar exit (future-proofing).
  QUIT_SEEN=""
  QT_DEADLINE=$(( $(date +%s) + 90 ))
  while [ -z "$QUIT_SEEN" ] && [ "$(date +%s)" -lt "$QT_DEADLINE" ]; do
    if ! pgrep -x azahar >/dev/null; then QUIT_SEEN="azahar exited"; break; fi
    if tail -n "+$((B0 + 1))" "$BLOG" 2>/dev/null | grep -q 'emulator closed the RSP socket'; then
      QUIT_SEEN="app exit closed the RSP session (broker down, azahar shell alive)"
      break
    fi
    sleep 2
  done
  if [ -n "$QUIT_SEEN" ]; then
    echo "smoke: quit observed: $QUIT_SEEN"
  else
    echo "smoke: FAIL — no quit observable within 90s"; PC_OK=0
  fi
  "$RUN" azctl stop || PC_OK=0
  LRD="$(cat "$HERE/state/last_run" 2>/dev/null)"
  if grep -q 'Loaded Movie, ID:' "$LRD/azahar_log.txt" 2>/dev/null; then
    echo "smoke: azahar_log: movie playback confirmed (Loaded Movie, ID:)"
  else
    echo "smoke: FAIL — no 'Loaded Movie' line in the harvested log"; PC_OK=0
  fi
  # Desync guard: type-mismatch lines are Error-level (= flushed immediately even into
  # the lazily-buffered log), so zero hits is meaningful evidence the stream stayed pure.
  if grep -q 'Expected to read type' "$LRD/azahar_log.txt" 2>/dev/null; then
    echo "smoke: FAIL — movie desync lines in the log (is_new_3ds pin regressed?)"; PC_OK=0
  fi
  cmp -s "$SNAP2" "$AZ_CFG" || { echo "smoke: FAIL config not restored after ctm cycle"; PC_OK=0; }
else
  PC_OK=0
  "$RUN" azctl stop >/dev/null 2>&1 || true
fi
# Restore the user's emulator-side settings.bin (mutated by the movie's menu taps);
# if there was none before the run, remove the one the movie run created.
if [ -n "$SET_SNAP" ] && [ -f "$SET_SNAP" ]; then
  cp "$SET_SNAP" "$SETTINGS"; rm -f "$SET_SNAP"
elif [ -z "$SET_SNAP" ] && [ -f "$SETTINGS" ]; then
  rm -f "$SETTINGS"
fi
rm -f "$MOVIE" "$SNAP2"
if [ "$PC_OK" = 1 ]; then
  row press-ctm PASS "movie menu-drive: tiltLevel ${TILT_BEFORE:-?}->3; quit: ${QUIT_SEEN}; Loaded Movie in log, 0 desyncs"
else
  row press-ctm FAIL "see smoke output above"
fi

# =========================================================================================
# Phase 3 — press-d4 + sdmc (E4, Tier B, --rom only): fixtures + the app's own channels
# =========================================================================================
# Movie = tests/fixtures/movie_pick_play_quit.json (E3's live-proven picker drive + the
# Tier-A quit segment): wait 900f past splash -> picker taps A,A,DOWN,A,X = both fixture
# ROMs picked, session starts (E3 loop 2) -> 700f of dual-core play (the pre-dropped
# move_p1.txt "W60" is consumed here: pickup needs a live core, main.c:2947) -> pause-menu
# combo -> SESSION tab -> QUIT -> gs_dump(-1) writes 3DGBA_gs_*.txt (main.c:4147; rows ARE
# captured because the fixtures are Pokemon and gamestate ticks on ctx changes/600f
# heartbeat, gamestate.c:302) -> app exits (same broker observable as Tier A).
if [ "$ROM" = 1 ]; then
  D4_OK=1
  SD_OK=1
  D4_DETAIL=""
  if [ ! -f "$AZ_SD/dual-gba/gameA.gba" ]; then
    row press-d4 FAIL "no $AZ_SD/dual-gba/gameA.gba to stage as a fixture (H4.2)"
    row sdmc     FAIL "Tier B prerequisites missing (see press-d4)"
  else
    SET_SNAP3=""
    if [ -f "$SETTINGS" ]; then SET_SNAP3="$(mktemp -t emutest-settings3)"; cp "$SETTINGS" "$SET_SNAP3"; fi
    SNAP3="$(mktemp -t emutest-cfg-snap3)"
    cp "$AZ_CFG" "$SNAP3"
    MOVIE3="$(mktemp -t emutest-movie3).ctm"
    B0="$(wc -l < "$BLOG" 2>/dev/null | tr -d ' ' || echo 0)"; B0="${B0:-0}"

    "$RUN" sdmc arm-control || D4_OK=0                 # the app's opt-in stat (main.c:2521)
    "$RUN" sdmc drop move 1 "W60" || D4_OK=0           # pre-drop: pickup is consume-on-read
    "$RUN" ctm make "$HERE/tests/fixtures/movie_pick_play_quit.json" "$MOVIE3" || D4_OK=0
    if [ "$D4_OK" = 1 ] && "$RUN" azctl boot --gdb --movie "$MOVIE3" --fresh-sd-fixtures; then
      LRD3="$(cat "$HERE/state/last_run" 2>/dev/null)"
      SPAWN3="$("$PY" -c 'import json,sys; print(json.load(open(sys.argv[1])).get("spawn_ts",0))' "$LRD3/boot.json" 2>/dev/null || echo 0)"
      "$RUN" gdbio resume || D4_OK=0
      # Seat-0 pickup counter (g_ctlStat+6, u16 — control.h:170-176; E3 live: 0 -> 1).
      PK="$("$RUN" gdbio poll g_ctlStat+6 --expect 1 --width 2 --timeout 120 --interval 1000)" \
        && echo "$PK" | sed 's/^/smoke: /' || { echo "$PK" | sed 's/^/smoke: /'; D4_OK=0; }
      "$RUN" sdmc wait-consumed move 1 --timeout 30 || D4_OK=0   # remove-on-pickup ACK
      CS="$("$RUN" sdmc control-status --expect-pickup)" \
        && echo "$CS" | sed 's/^/smoke: /' || { echo "$CS" | sed 's/^/smoke: /'; D4_OK=0; }
      D4_DETAIL="$(echo "$CS" | grep '^header:' | head -1)"
      # Quit watch (movie length ~34 s emulated; same PROVEN observable as Tier A).
      QUIT3=""
      QT_DEADLINE=$(( $(date +%s) + 120 ))
      while [ -z "$QUIT3" ] && [ "$(date +%s)" -lt "$QT_DEADLINE" ]; do
        if ! pgrep -x azahar >/dev/null; then QUIT3="azahar exited"; break; fi
        if tail -n "+$((B0 + 1))" "$BLOG" 2>/dev/null | grep -q 'emulator closed the RSP socket'; then
          QUIT3="app quit (RSP session closed)"
          break
        fi
        sleep 2
      done
      if [ -n "$QUIT3" ]; then
        echo "smoke: tier-B quit observed: $QUIT3"
      else
        echo "smoke: FAIL — no Tier-B quit observable within 120s"; D4_OK=0; SD_OK=0
      fi
      "$RUN" azctl stop || D4_OK=0
      # --- sdmc channel: the quit-time gs log (new, well-formed, non-empty) --------------
      GS_CHK="$("$PY" - "$AZ_SD/cias/netlogs" "$SPAWN3" <<'PYEOF'
import glob, os, sys
d, since = sys.argv[1], float(sys.argv[2])
cands = [p for p in glob.glob(os.path.join(d, "3DGBA_gs_*.txt"))
         if os.path.getmtime(p) >= since - 1]
if not cands:
    print("no NEW 3DGBA_gs_*.txt (mtime >= boot) in " + d)
    sys.exit(1)
p = max(cands, key=os.path.getmtime)
lines = open(p, errors="replace").read().splitlines()
if not lines or not lines[0].startswith("# 3DGBA game-state log"):
    print("%s: malformed first line %r" % (os.path.basename(p), lines[0] if lines else ""))
    sys.exit(1)
hdr = [i for i, l in enumerate(lines) if l.startswith("idx,frame,scr,")]
if not hdr:
    print("%s: no idx,frame,scr CSV header" % os.path.basename(p))
    sys.exit(1)
rows = [l for l in lines[hdr[0] + 1:] if l and not l.startswith("#")]
if not rows:
    print("%s: CSV header but 0 data rows" % os.path.basename(p))
    sys.exit(1)
print("%s: header ok, %d data rows" % (os.path.basename(p), len(rows)))
PYEOF
)"
      if [ $? = 0 ]; then
        echo "smoke: gs log: $GS_CHK"
      else
        echo "smoke: FAIL gs log: $GS_CHK"; SD_OK=0
      fi
      cmp -s "$SNAP3" "$AZ_CFG" || { echo "smoke: FAIL config not restored after Tier B"; D4_OK=0; }
    else
      D4_OK=0; SD_OK=0
      "$RUN" azctl stop >/dev/null 2>&1 || true
    fi
    # Teardown: fixtures out (originals re-hashed inside), settings restored, tmp gone.
    # The app-written netlogs stay in the user's netlogs dir (harvested copies are in the
    # run dir) — the established wipe-only-on-request rule (H4.3).
    "$RUN" azctl clean-fixtures || D4_OK=0
    if [ -n "$SET_SNAP3" ] && [ -f "$SET_SNAP3" ]; then
      cp "$SET_SNAP3" "$SETTINGS"; rm -f "$SET_SNAP3"
    elif [ -z "$SET_SNAP3" ] && [ -f "$SETTINGS" ]; then
      rm -f "$SETTINGS"
    fi
    rm -f "$MOVIE3" "$SNAP3"
    if [ "$D4_OK" = 1 ]; then
      row press-d4 PASS "move_p1 consumed (pickup ctr 0->1); ${D4_DETAIL:-header ok}; ${QUIT3:-}"
    else
      row press-d4 FAIL "see smoke output above"
    fi
    if [ "$SD_OK" = 1 ] && [ "$D4_OK" = 1 ]; then
      row sdmc PASS "quit-time gs log: ${GS_CHK:-}"
    elif [ "$SD_OK" = 1 ]; then
      row sdmc FAIL "gs log ok but the Tier-B session it depends on failed (press-d4)"
    else
      row sdmc FAIL "${GS_CHK:-see smoke output above}"
    fi
  fi
else
  row press-d4 SKIP "Tier B needs --rom (stages copies of the user's dual-gba ROMs, H4.2)"
  row sdmc     SKIP "Tier B needs --rom (the gs-log assertion needs a Pokemon session, H4.4)"
fi

row see "${SEE_ROW_V:-FAIL}" "${SEE_ROW_D:-internal: see phase never ran}"

finish
