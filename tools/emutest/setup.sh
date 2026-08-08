#!/bin/bash
# setup.sh — build the emutest venv, ensure the .gitignore hunk, run host tests, then the
# live smoke gate (phase-16 slice E1; SPEC-harness H1.2/H1.3 — the PokeDNA setup.sh pattern).
#
#   ./setup.sh            # full gate: venv -> host tests -> smoke.sh (live Azahar boot)
#   ./setup.sh --no-live  # stop after host tests (CI-ish); the default is the full gate
#
# Idempotent: the venv is rebuilt only when the pinned package list mismatches pip freeze;
# host tests + smoke always run (H1.3).
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
VENV="$HERE/.venv"

NO_LIVE=0
for a in "$@"; do
  case "$a" in
    --no-live) NO_LIVE=1 ;;
    *) echo "usage: setup.sh [--no-live]" >&2; exit 2 ;;
  esac
done

# --- pinned wheels (H1.2) -----------------------------------------------------------------
# pillow                 — PNG encode/crop/diff/zoom for see/compare/zoom/sheet (E3).
# pyobjc-framework-Quartz — CGWindowListCopyWindowInfo window lookup (see.py only; every
#                           other tool runs without it).
# NO numpy — justification in SPEC-harness H1.2 (PIL histogram/difference are exact for
# our <=400x480 metrics; numpy existed in PokeDNA only for libmgba-py's raw C buffer).
# Versions pinned 2026-08-08 from a live install against /usr/bin/python3 3.9.6 (BUILDLOG E1).
PINS="pillow==11.3.0
pyobjc-framework-Quartz==11.1"

pick_python() {
  if /usr/bin/python3 -c 'import venv, ensurepip' >/dev/null 2>&1; then
    echo /usr/bin/python3
  else
    command -v python3  # Homebrew fallback (H1.2)
  fi
}

venv_matches() {
  [ -x "$VENV/bin/python" ] || return 1
  local frozen
  frozen="$("$VENV/bin/python" -m pip freeze --disable-pip-version-check 2>/dev/null)" || return 1
  while IFS= read -r pin; do
    # pip freeze spells it "Pillow==…" vs pin "pillow==…" — compare case-insensitively.
    echo "$frozen" | grep -qi "^${pin}$" || return 1
  done <<< "$PINS"
  return 0
}

echo "== emutest setup: venv =="
if venv_matches; then
  echo "venv up to date ($VENV)"
else
  rm -rf "$VENV"
  PY="$(pick_python)"
  echo "building venv with $PY ($("$PY" --version 2>&1))"
  "$PY" -m venv "$VENV"
  "$VENV/bin/python" -m pip install --quiet --disable-pip-version-check --upgrade pip
  # shellcheck disable=SC2086
  "$VENV/bin/python" -m pip install --quiet --disable-pip-version-check \
      --require-virtualenv $PINS
  echo "installed: $(echo $PINS | tr '\n' ' ')"
fi

# Optional OS-input channel (Accessibility-gated; absence is a SKIP, never a failure — H1.2):
command -v cliclick >/dev/null 2>&1 || \
  echo "note: cliclick not installed — OS-click channel will SKIP. Optional: brew install cliclick"

echo "== emutest setup: .gitignore hunk (H1.4) =="
GI="$REPO/.gitignore"
if grep -q 'tools/emutest/\.venv/' "$GI" 2>/dev/null; then
  echo ".gitignore hunk present"
else
  cat >> "$GI" <<'EOF'

# phase-16 emulator harness — machine-local state, never source
tools/emutest/.venv/
tools/emutest/runs/
tools/emutest/state/
tools/emutest/emutest.mapsyms
# E2: the checked-in ELF test fixture must survive the global *.elf ignore
!tools/emutest/tests/fixtures/tiny.elf
EOF
  echo ".gitignore hunk appended"
fi
# E2 sub-hunk (idempotent for trees that already had the E1 hunk):
if ! grep -q 'tiny\.elf' "$GI" 2>/dev/null; then
  printf '# E2: the checked-in ELF test fixture must survive the global *.elf ignore\n!tools/emutest/tests/fixtures/tiny.elf\n' >> "$GI"
  echo ".gitignore E2 sub-hunk appended"
fi

echo "== emutest setup: host tests (H6) =="
"$HERE/tests/run_host_tests.sh"

if [ "$NO_LIVE" = 1 ]; then
  echo "== emutest setup: --no-live: skipping smoke.sh (the live gate) =="
  echo "setup OK (host-only). Run $HERE/smoke.sh before trusting the harness (H6.4)."
  exit 0
fi

echo "== emutest setup: smoke gate =="
exec "$HERE/smoke.sh"
