# test_mapsyms.py — nm-output and GNU-ld-.map parser goldens for gdbio.py (phase-16 slice
# E2; SPEC-harness H6.2 row test_mapsyms). Run:
#   tools/emutest/.venv/bin/python -m unittest discover -s tools/emutest/tests -p 'test_*.py' -v
#
# Fixtures are VERBATIM excerpts of the real build artifacts, extracted 2026-08-08 from the
# 2026-08-04 build (the golden addresses below are that build's — frozen in the fixture, so
# they never drift):
#   fixtures/nm_snippet.txt  — `arm-none-eabi-nm -S 3DGBA.elf` lines: 4-token (addr size
#     type name), 3-token (no size), addressless weak lines, and the REAL duplicate `kHeld`
#     (a static in two TUs).
#   fixtures/map_snippet.map — build/3DGBA.map lines: archive-member preamble, zero-addr
#     input sections, PROVIDE/ALIGN/assignment lines, output-section headers, input-section
#     continuation lines, *fill* lines, and plain two-token symbol lines (S2.5).
# A live consistency test at the bottom cross-checks the CURRENT build's nm vs map through
# gdbio.map_crosscheck (skipped when the toolchain/artifacts are absent).

import os
import subprocess
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import gdbio  # noqa: E402

FIX = os.path.join(os.path.dirname(os.path.abspath(__file__)), "fixtures")


def read_fixture(name):
    with open(os.path.join(FIX, name)) as f:
        return f.read()


class TestNmParse(unittest.TestCase):
    def setUp(self):
        self.syms = gdbio.nm_parse(read_fixture("nm_snippet.txt"))

    def test_four_token_lines(self):
        # `001eb328 000000c4 t ctl_log_line` — addr, size, type, name.
        self.assertEqual(self.syms["ctl_log_line"], [(0x001EB328, "t", 0xC4)])
        self.assertEqual(self.syms["g_prefs"], [(0x0049B49C, "D", 0x20)])
        self.assertEqual(self.syms["AUDIO_NAMES"], [(0x00235000, "R", 0xC)])
        self.assertEqual(self.syms["_deadbeef"], [(0x0049B4E4, "d", 4)])
        self.assertEqual(self.syms["g_renderSeq"], [(0x004D1884, "b", 4)])
        self.assertEqual(self.syms["g_quit"], [(0x004D1869, "b", 1)])
        self.assertEqual(self.syms["g_appActive"], [(0x0049B460, "d", 1)])

    def test_three_token_lines_no_size(self):
        # `00491454 r .LC2` — size None.
        self.assertEqual(self.syms[".LC2"], [(0x00491454, "r", None)])
        self.assertEqual(self.syms[".divsi3_skip_div0_test"], [(0x001DDE6C, "t", None)])

    def test_addressless_weak_lines_skipped(self):
        # `         w __cxa_type_match` has no address — must not appear.
        self.assertNotIn("__cxa_type_match", self.syms)
        self.assertNotIn("__cxa_begin_cleanup", self.syms)

    def test_duplicate_statics_all_kept(self):
        # kHeld is a file-scope static in two TUs (real duplicate, probed 2026-08-08).
        self.assertEqual(len(self.syms["kHeld"]), 2)
        addrs = sorted(a for a, _t, _s in self.syms["kHeld"])
        self.assertEqual(addrs, [0x0054BC38, 0x0054BC68])

    def test_lookup_refuses_ambiguous(self):
        cache = {"symbols": {k: [list(e) for e in v] for k, v in self.syms.items()},
                 "elf": "fixture"}
        with self.assertRaises(SystemExit):
            gdbio.lookup(cache, "kHeld")

    def test_lookup_unique_and_addr_form(self):
        cache = {"symbols": {k: [list(e) for e in v] for k, v in self.syms.items()},
                 "elf": "fixture"}
        addr, size, name = gdbio.lookup(cache, "g_prefs")
        self.assertEqual((addr, size, name), (0x0049B49C, 0x20, "g_prefs"))
        addr, size, name = gdbio.lookup(cache, "0x4a2640")
        self.assertEqual(addr, 0x004A2640)
        self.assertIsNone(size)

    def test_lookup_symbol_plus_offset(self):
        # E3: SYM+OFF for struct-field reads (g_prefs+0x1c = UiPrefs.tiltLevel).
        cache = {"symbols": {k: [list(e) for e in v] for k, v in self.syms.items()},
                 "elf": "fixture"}
        addr, size, name = gdbio.lookup(cache, "g_prefs+0x1c")
        self.assertEqual((addr, size, name), (0x0049B49C + 0x1C, None, "g_prefs+0x1c"))
        addr, _size, _name = gdbio.lookup(cache, "g_prefs+4")   # decimal offsets too
        self.assertEqual(addr, 0x0049B49C + 4)
        with self.assertRaises(SystemExit):
            gdbio.lookup(cache, "g_prefs+zz")


class TestMapParse(unittest.TestCase):
    def setUp(self):
        self.entries = gdbio.map_parse(read_fixture("map_snippet.map"))
        self.by_name = {n: (a, s) for n, a, s in self.entries}

    def test_exact_symbol_set(self):
        # Only plain two-token symbol lines qualify (S2.5); everything else in the
        # fixture (PROVIDE, ALIGN, assignments, input sections, *fill*, objects) is noise.
        self.assertEqual(sorted(self.by_name), [
            "AUDIO_NAMES", "g_ctlStat", "g_diagMainCrumb", "g_diagNetCrumb",
            "g_diagRxSeq", "g_diagSioCrumb", "g_prefs", "g_ui"])

    def test_addresses_and_sections(self):
        # Golden addresses from the 2026-08-04 build (frozen in the fixture).
        self.assertEqual(self.by_name["AUDIO_NAMES"], (0x00235000, ".rodata"))
        self.assertEqual(self.by_name["g_ui"], (0x0049B478, ".data"))
        self.assertEqual(self.by_name["g_prefs"], (0x0049B49C, ".data"))
        self.assertEqual(self.by_name["g_ctlStat"], (0x004A2600, ".bss"))
        self.assertEqual(self.by_name["g_diagRxSeq"], (0x004A2640, ".bss"))
        self.assertEqual(self.by_name["g_diagMainCrumb"], (0x004A2660, ".bss"))
        self.assertEqual(self.by_name["g_diagSioCrumb"], (0x004A2680, ".bss"))
        self.assertEqual(self.by_name["g_diagNetCrumb"], (0x004A26A0, ".bss"))

    def test_noise_lines_excluded(self):
        # PROVIDE / `. = ALIGN` / `a = b` assignment lines and 3-token continuation
        # lines must not leak in as symbols.
        for bad in ["PROVIDE", "__start__", "__sync_synchronize", "__bss_start__",
                    "__preinit_array_start", "main.o", "ALIGN", "*fill*"]:
            self.assertNotIn(bad, self.by_name)
        # the 0x0-addressed crt0 input-section line is 3 tokens -> excluded
        self.assertNotIn("3dsx_crt0.o", self.by_name)


class TestCrossCheck(unittest.TestCase):
    def test_fixture_crosscheck_agrees(self):
        nm = gdbio.nm_parse(read_fixture("nm_snippet.txt"))
        mp = gdbio.map_parse(read_fixture("map_snippet.map"))
        xc = gdbio.map_crosscheck(nm, mp)
        # Shared unambiguous symbols in the two fixtures: AUDIO_NAMES, g_prefs,
        # g_ctlStat, g_diagRxSeq (4) — all agree because both came from the same build.
        self.assertEqual(xc["checked"], 4)
        self.assertEqual(xc["mismatches"], [])

    def test_mismatch_detected(self):
        nm = {"g_prefs": [(0x0049B49C, "D", 0x20)]}
        mp = [("g_prefs", 0x0049C49C, ".data")]
        xc = gdbio.map_crosscheck(nm, mp)
        self.assertEqual(len(xc["mismatches"]), 1)
        self.assertEqual(xc["mismatches"][0]["name"], "g_prefs")

    def test_ambiguous_names_not_checked(self):
        nm = {"kHeld": [(0x0054BC38, "b", 4), (0x0054BC68, "b", 4)]}
        mp = [("kHeld", 0x0054BC38, ".bss")]
        xc = gdbio.map_crosscheck(nm, mp)
        self.assertEqual(xc["checked"], 0)

    def test_tls_symbols_excluded(self):
        # REAL case (probed 2026-08-08): nm reports .tbss symbols by their offset within
        # the TLS block (< IMAGE_BASE 0x100000), the map by their link placement — not a
        # mismatch, not a checkable address. `B __ctru_dev_path_buf` = 0x804 vs 0x49da5c.
        nm = {"__ctru_dev_path_buf": [(0x00000804, "B", 0x401)],
              "__ctru_dev_utf16_buf": [(0x00000000, "B", 0x802)]}
        mp = [("__ctru_dev_path_buf", 0x0049DA5C, ".bss"),
              ("__ctru_dev_utf16_buf", 0x0049D2D8, ".bss")]
        xc = gdbio.map_crosscheck(nm, mp)
        self.assertEqual(xc["checked"], 0)
        self.assertEqual(xc["mismatches"], [])


class TestLiveArtifacts(unittest.TestCase):
    """Consistency against the CURRENT build (not goldens — addresses drift per build).
    Skips cleanly when the toolchain or artifacts are absent (H6.1: host-pure by default)."""

    def setUp(self):
        self.elf = gdbio.elf_path()
        self.map = gdbio.map_path()
        try:
            self.nm = gdbio.nm_tool()
        except SystemExit:
            self.skipTest("arm-none-eabi-nm not installed")
        if not os.path.exists(self.elf):
            self.skipTest("3DGBA.elf not built")

    def test_real_nm_has_the_smoke_symbols(self):
        r = subprocess.run([self.nm, "-S", self.elf], capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stderr)
        syms = gdbio.nm_parse(r.stdout)
        for name in ["g_renderSeq", "g_appActive", "g_quit", "g_prefs",
                     "ctl_log_line", "_deadbeef"]:
            self.assertIn(name, syms, "smoke symbol missing from the current build")
            self.assertEqual(len(syms[name]), 1, "smoke symbol became ambiguous")

    def test_real_map_agrees_with_real_nm(self):
        if not os.path.exists(self.map):
            self.skipTest("build/3DGBA.map not present")
        r = subprocess.run([self.nm, "-S", self.elf], capture_output=True, text=True)
        syms = gdbio.nm_parse(r.stdout)
        with open(self.map) as f:
            entries = gdbio.map_parse(f.read())
        self.assertGreater(len(entries), 100, "map parse found suspiciously few symbols")
        xc = gdbio.map_crosscheck(syms, entries)
        self.assertGreater(xc["checked"], 100)
        self.assertEqual(xc["mismatches"], [],
                         "map/nm disagree — stale build pair? rebuild both with make")


if __name__ == "__main__":
    unittest.main()
