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
EV=docs/phase21-touch-census/evidence
if [ -d "$EV" ]; then
  DUPS=$(find "$EV" -name '*.png' -exec shasum {} + 2>/dev/null | awk '{print $1}' | sort | uniq -d | wc -l | tr -d ' ')
  if [ "$DUPS" = 0 ]; then ok "no duplicate captures"
  else
    warn "$DUPS duplicate capture hash(es) — a duplicate is only OK if no claim rests on it:"
    find "$EV" -name '*.png' -exec shasum {} + 2>/dev/null | sort | awk '{if($1==p)print "         "$2; p=$1}' | head -6
  fi
fi

echo
[ "$FAIL" = 0 ] && echo "CLOSE-OUT CLEAN" || echo "CLOSE-OUT HAS FAILURES — fix before calling the phase done"
exit $FAIL
