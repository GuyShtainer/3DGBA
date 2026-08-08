#!/bin/bash
# run_host_tests.sh — host-pure test runner (SPEC-harness H6.1): stdlib unittest, no pytest.
# Run line (printed at the top of every test file):
#   tools/emutest/.venv/bin/python -m unittest discover -s tools/emutest/tests -p 'test_*.py' -v
set -euo pipefail
d="$(cd "$(dirname "$0")/.." && pwd)"
exec "$d/.venv/bin/python" -m unittest discover -s "$d/tests" -p 'test_*.py' -v
