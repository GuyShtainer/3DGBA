# test_compare.py — host-pure tests for compare.py (phase-16 slice E4; SPEC-harness H6.2
# "test_compare": image ops on generated-in-test PIL fixtures, exit-code verdict behavior
# incl. --tolerance boundaries — no binary fixtures needed).
# Run line:
#   tools/emutest/.venv/bin/python -m unittest discover -s tools/emutest/tests -p 'test_*.py' -v

import os
import sys
import tempfile
import unittest

from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import compare


class Base(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()

    def tearDown(self):
        self._tmp.cleanup()

    def img(self, name, size=(40, 30), color=(10, 20, 30), pokes=()):
        """Write a solid PNG with optional per-pixel pokes [(x, y, (r,g,b)), ...]."""
        im = Image.new("RGB", size, color)
        for x, y, c in pokes:
            im.putpixel((x, y), c)
        p = os.path.join(self._tmp.name, name)
        im.save(p)
        return p


class TestCompare(Base):
    def test_identical(self):
        a = self.img("a.png")
        b = self.img("b.png")
        d = compare.compare_png(a, b)
        self.assertTrue(d.identical)
        self.assertEqual(d.diff_pixels, 0)
        self.assertIsNone(d.bbox)
        self.assertIn("IDENTICAL", d.summary())

    def test_counts_and_bbox(self):
        a = self.img("a.png")
        b = self.img("b.png", pokes=[(5, 6, (200, 20, 30)), (20, 25, (10, 20, 31))])
        d = compare.compare_png(a, b)
        self.assertEqual(d.diff_pixels, 2)
        # bbox is inclusive (like the numpy original's min/max of nonzero indices)
        self.assertEqual(d.bbox, (5, 6, 20, 25))
        self.assertEqual(d.max_channel_delta, 190)  # 200-10 on R at (5,6)
        self.assertIn("DIFFERENT", d.summary())

    def test_tolerance_boundary(self):
        # Predicate is delta > tolerance (strict, like the original): a delta of exactly
        # `tolerance` does NOT count as changed; tolerance-1 does.
        a = self.img("a.png")
        b = self.img("b.png", pokes=[(3, 3, (13, 20, 30))])  # delta 3 on R
        self.assertEqual(compare.compare_png(a, b, tolerance=2).diff_pixels, 1)
        self.assertEqual(compare.compare_png(a, b, tolerance=3).diff_pixels, 0)

    def test_size_mismatch_raises(self):
        a = self.img("a.png", size=(40, 30))
        b = self.img("b.png", size=(41, 30))
        with self.assertRaises(ValueError):
            compare.compare_png(a, b)

    def test_missing_file_raises(self):
        a = self.img("a.png")
        with self.assertRaises(FileNotFoundError):
            compare.compare_png(a, os.path.join(self._tmp.name, "nope.png"))

    def test_diff_image_layout(self):
        # 3 panels + two 1px separators; heat pixel red where changed, dimmed grey where not.
        a = self.img("a.png", size=(8, 4), color=(90, 90, 90))
        b = self.img("b.png", size=(8, 4), color=(90, 90, 90), pokes=[(2, 1, (0, 0, 0))])
        out = os.path.join(self._tmp.name, "d.png")
        d = compare.compare_png(a, b, diff_path=out)
        self.assertEqual(d.diff_path, out)
        im = Image.open(out).convert("RGB")
        self.assertEqual(im.size, (8 * 3 + 2, 4))
        self.assertEqual(im.getpixel((8, 0)), (0, 255, 0))          # separator
        self.assertEqual(im.getpixel((8 + 1 + 8 + 1 + 2, 1)), (255, 0, 0))  # heat: changed
        self.assertEqual(im.getpixel((8 + 1 + 8 + 1 + 0, 0)), (30, 30, 30))  # dimmed 90//3

    def test_cli_exit_codes(self):
        a = self.img("a.png")
        b = self.img("b.png", pokes=[(1, 1, (200, 20, 30))])
        self.assertEqual(compare.main([a, a]), 0)                    # identical
        self.assertEqual(compare.main([a, b]), 1)                    # differs
        self.assertEqual(compare.main([a, b, "--max-diff", "1"]), 0)  # allowed
        self.assertEqual(compare.main([a, b, "--tolerance", "200"]), 0)
        self.assertEqual(compare.main([a, os.path.join(self._tmp.name, "x.png")]), 2)

    def test_assert_identical(self):
        a = self.img("a.png")
        b = self.img("b.png", pokes=[(1, 1, (200, 20, 30))])
        compare.assert_identical(a, a)
        compare.assert_identical(a, b, max_diff=1)
        with self.assertRaises(AssertionError):
            compare.assert_identical(a, b)


if __name__ == "__main__":
    unittest.main()
