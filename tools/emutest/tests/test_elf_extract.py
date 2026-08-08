# test_elf_extract.py — stdlib ELF32 vaddr->file-bytes extraction goldens for gdbio.py
# (phase-16 slice E2; SPEC-harness H3.3/H6.2 row test_elf_extract). Run:
#   tools/emutest/.venv/bin/python -m unittest discover -s tools/emutest/tests -p 'test_*.py' -v
#
# Fixture: fixtures/tiny.elf, built ONCE in slice E2 (2026-08-08) from fixtures/tiny.c with
# devkitARM GCC 15.2.0 (`arm-none-eabi-gcc -nostdlib -nostartfiles -Ttext=0x100000`).
# Goldens frozen from that build (arm-none-eabi-nm/objdump, recorded in tiny.c's header):
#   PT_LOAD r-x  vaddr 0x00100000 filesz 0x50  (code + .rodata merged)
#   PT_LOAD rw-  vaddr 0x00101050 filesz 0x08 memsz 0x48  (.data + .bss tail)
#   fix_ro @0x00100040 = "RODATA-FIXTURE!\0"   fix_dw @0x00101050 = DE AD BE EF 01 02 03 04
#   fix_zz @0x00101058 = .bss (NOT file-backed -> bytes_at must return None)
#   .text  @0x00100000 starts 04 b0 2d e5 (push/str prologue)
# A live test at the bottom checks the real 3DGBA.elf's _deadbeef word (a compile-time
# constant from external/mgba/src/gba/memory.c:28 — {0x10,0xB7,0x10,0xE7}); skipped when
# the artifacts are absent.

import os
import subprocess
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import gdbio  # noqa: E402

FIX = os.path.join(os.path.dirname(os.path.abspath(__file__)), "fixtures")
TINY = os.path.join(FIX, "tiny.elf")


class TestTinyElf(unittest.TestCase):
    def setUp(self):
        self.elf = gdbio.ElfImage(TINY)

    def test_load_segments(self):
        # Exactly the two PT_LOADs frozen in the header comment; EXIDX etc. excluded.
        self.assertEqual(
            [(v, fs, ms) for v, fs, ms, _o in self.elf.loads],
            [(0x00100000, 0x50, 0x50), (0x00101050, 0x08, 0x48)])

    def test_rodata_golden(self):
        self.assertEqual(self.elf.bytes_at(0x00100040, 16), b"RODATA-FIXTURE!\x00")

    def test_data_golden(self):
        self.assertEqual(self.elf.bytes_at(0x00101050, 8),
                         bytes([0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02, 0x03, 0x04]))

    def test_text_golden(self):
        self.assertEqual(self.elf.bytes_at(0x00100000, 4), bytes([0x04, 0xB0, 0x2D, 0xE5]))

    def test_bss_not_file_backed(self):
        # fix_zz lives in memsz beyond filesz — H3.3 excludes .bss for exactly this reason.
        self.assertIsNone(self.elf.bytes_at(0x00101058, 4))

    def test_unmapped_returns_none(self):
        self.assertIsNone(self.elf.bytes_at(0x000FFF00, 4))
        self.assertIsNone(self.elf.bytes_at(0x40000000, 4))

    def test_window_crossing_segment_end_returns_none(self):
        # 0x100040 + 17 crosses filesz of the r-x segment — must refuse, not truncate.
        self.assertIsNone(self.elf.bytes_at(0x00100040, 17))
        # exact fit up to the boundary is fine
        self.assertIsNotNone(self.elf.bytes_at(0x00100040, 16))

    def test_rejects_non_elf(self):
        bad = os.path.join(FIX, "nm_snippet.txt")
        with self.assertRaises(ValueError):
            gdbio.ElfImage(bad)


class TestRealElf(unittest.TestCase):
    """Against the CURRENT 3DGBA.elf (values that are build-invariant only)."""

    def setUp(self):
        if not os.path.exists(gdbio.elf_path()):
            self.skipTest("3DGBA.elf not built")
        try:
            self.nm = gdbio.nm_tool()
        except SystemExit:
            self.skipTest("arm-none-eabi-nm not installed")
        self.elf = gdbio.ElfImage(gdbio.elf_path())

    def _addr_of(self, name):
        r = subprocess.run([self.nm, "-S", gdbio.elf_path()],
                           capture_output=True, text=True)
        syms = gdbio.nm_parse(r.stdout)
        self.assertIn(name, syms)
        self.assertEqual(len(syms[name]), 1)
        return syms[name][0][0]

    def test_deadbeef_word(self):
        # mGBA's illegal-instruction marker (external/mgba/src/gba/memory.c:28) — the
        # known compile-time word verify-base's data anchor leans on.
        addr = self._addr_of("_deadbeef")
        self.assertEqual(self.elf.bytes_at(addr, 4),
                         bytes([0x10, 0xB7, 0x10, 0xE7]))

    def test_bss_symbol_not_file_backed(self):
        addr = self._addr_of("g_renderSeq")   # main.c:91, .bss
        self.assertIsNone(self.elf.bytes_at(addr, 4))


if __name__ == "__main__":
    unittest.main()
