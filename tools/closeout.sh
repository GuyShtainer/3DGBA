#!/usr/bin/env bash
# closeout.sh — the phase close-out gate.
#
# Every check here exists because an ADVERSARIAL AUDIT caught it, more than once, after the phase
# had already been reported as finished. They are cheap, mechanical, and were previously left to
# vigilance — which failed three times each. Run this before calling any phase done.
#
#   tools/closeout.sh          # report
#   tools/closeout.sh --fix    # also rebuild a stale .cia
#
# Exit 0 = clean, 1 = something needs attention.
set -uo pipefail
cd "$(dirname "$0")/.." || exit 1
FIX=0; [ "${1:-}" = "--fix" ] && FIX=1
FAIL=0
ok(){ printf '  \033[32mok\033[0m   %s\n' "$1"; }
bad(){ printf '  \033[31mFAIL\033[0m %s\n' "$1"; FAIL=1; }
warn(){ printf '  \033[33mwarn\033[0m %s\n' "$1"; }

echo "== 1. the .cia is the real install target (CLAUDE.md rule 1) =="
# Caught stale by audit in phases 24, 25 and 28. A stale .cia silently blocks every hardware test.
if [ ! -f 3DGBA.cia ]; then bad "no 3DGBA.cia at all"
elif [ ! -f 3DGBA.elf ]; then warn "no .elf to compare against"
elif [ 3DGBA.cia -ot 3DGBA.elf ]; then
  if [ "$FIX" = 1 ]; then
    echo "     stale -> rebuilding"
    DEVKITPRO=${DEVKITPRO:-/opt/devkitpro} DEVKITARM=${DEVKITARM:-/opt/devkitpro/devkitARM} \
      make cia >/dev/null 2>&1 && ok "rebuilt $(date -r 3DGBA.cia '+%m-%d %H:%M')" || bad "rebuild failed"
  else
    bad ".cia ($(date -r 3DGBA.cia '+%m-%d %H:%M')) is OLDER than .elf ($(date -r 3DGBA.elf '+%m-%d %H:%M')) — run with --fix"
  fi
else ok ".cia current ($(date -r 3DGBA.cia '+%m-%d %H:%M'))"; fi

echo "== 2. no work stranded on a lane branch =="
# Phases 25 and 28 both reported shipped work that was sitting only on a branch.
STRANDED=0
for b in $(git for-each-ref --format='%(refname:short)' refs/heads/ | grep -vx main); do
  n=$(git rev-list --count "main..$b" 2>/dev/null || echo 0)
  [ "$n" -gt 0 ] && { bad "$b has $n commit(s) not on main"; STRANDED=1; }
done
[ "$STRANDED" = 0 ] && ok "every branch is merged into main"

echo "== 3. frozen files =="
# fieldpath.{c,h} are proven and must not drift.
if git diff --quiet HEAD -- source/fieldpath.c source/fieldpath.h; then ok "fieldpath.{c,h} clean"
else bad "fieldpath.{c,h} has uncommitted changes — these are FROZEN"; fi

echo "== 4. no ROMs or saves tracked, ever =="
if git log --all --diff-filter=A --name-only --format='' -- '*.gba' '*.sav' 2>/dev/null | grep -q .; then
  bad "a ROM or save appears in history"
elif git ls-files | grep -qE '\.(gba|sav)$'; then bad "a ROM or save is tracked right now"
else ok "no ROM/save tracked or in history"; fi

echo "== 5. no emulator left running, no fixtures staged =="
if pgrep -f -i azahar >/dev/null 2>&1; then bad "an Azahar is still running"; else ok "no Azahar running"; fi
STAGED=0
for d in tools/emutest/state tools/emutest/state-b; do
  [ -d "$d" ] || continue
  n=$(find "$d" -name '*.gba' -size +1M 2>/dev/null | wc -l | tr -d ' ')
  [ "$n" -gt 0 ] && { warn "$d still holds $n staged ROM fixture(s)"; STAGED=1; }
done
[ "$STAGED" = 0 ] && ok "no ROM fixtures left staged"

echo "== 6. evidence integrity =="
# A byte-identical capture cited as independent proof has already been withdrawn from this tree.
#
# PHASE 29 LANE E: the whole-file hash this check used to run was BOTH too loud and too
# quiet. Too loud, because a census capture photographs BOTH 3DS screens and only one is
# the subject — the other holds a second, undriven game whose static screens repeat, which
# is 18 of the 20 groups it reported and carries no claim. Too quiet, because the subject
# screen's HUD strip (clock + FPS) changes every capture, so two shots of one unchanged
# game screen hash differently and slip through. `censusguard` hashes the 240x160 GBA
# frame of the SUBJECT screen only — the exact thing a census row claims.
EV=docs/phase21-touch-census/evidence
GUARD=tools/emutest/.venv/bin/python
if [ -d "$EV" ] && [ -x "$GUARD" ]; then
  # exit 1 ONLY when a group names two census rows; aux/ working captures may repeat.
  if OUT=$("$GUARD" tools/emutest/censusguard.py audit "$EV/emerald" "$EV/firered" 2>&1); then
    ok "$(printf '%s\n' "$OUT" | head -1 | sed 's/^censusguard: //')"
  else
    bad "two census rows share one subject frame — one of them has no evidence of its own:"
    printf '%s\n' "$OUT" | awk '/\[CENSUS ROW\]/{p=1} /\[aux\]/{p=0} p' | sed 's/^/       /'
  fi
elif [ -d "$EV" ]; then
  warn "censusguard needs tools/emutest/.venv (run tools/emutest/setup.sh) — evidence unchecked"
fi
# impl/ captures are single-screen and ZOOMED (720x480), not native 3DS screens, so
# censusguard cannot crop a GBA frame out of them — the whole-file hash IS the right check
# there and stays. Known standing item: the phase-23 FR-dlg-P2 / FR-dlg-P4 pair
# (579bffca…), found and reported by lane C1 in a388d48; P4's claim rests on its gdb
# deltas, not on that picture.
if [ -d "$EV/impl" ]; then
  IDUP=$(find "$EV/impl" -name '*.png' -exec shasum -a 256 {} + 2>/dev/null \
         | awk '{print $1}' | sort | uniq -d | wc -l | tr -d ' ')
  if [ "$IDUP" -le 1 ]; then ok "evidence/impl: $IDUP duplicate pair (the known phase-23 one)"
  else
    warn "evidence/impl has $IDUP duplicate hash(es) — one file must not carry two claims:"
    find "$EV/impl" -name '*.png' -exec shasum -a 256 {} + 2>/dev/null | sort \
      | awk '{if($1==p)print "         "$2; p=$1}' | head -8
  fi
fi

echo
[ "$FAIL" = 0 ] && echo "CLOSE-OUT CLEAN" || echo "CLOSE-OUT HAS FAILURES — fix before calling the phase done"
exit $FAIL
