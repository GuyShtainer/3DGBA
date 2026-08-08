# test_zoom.py — host-pure tests for zoom.py (phase-16 slice E4; SPEC-harness H6.2
# "test_zoom": generated PIL fixtures, exact-pixel nearest-neighbour expectations).
# Run line:
#   tools/emutest/.venv/bin/python -m unittest discover -s tools/emutest/tests -p 'test_*.py' -v

import os
import sys
import tempfile
import unittest

from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import zoom


class TestZoom(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()

    def tearDown(self):
        self._tmp.cleanup()

    def img(self, name, size=(20, 10)):
        """Checkerboard-ish fixture: pixel (x,y) = (x*10 % 256, y*20 % 256, 7)."""
        im = Image.new("RGB", size)
        for y in range(size[1]):
            for x in range(size[0]):
                im.putpixel((x, y), (x * 10 % 256, y * 20 % 256, 7))
        p = os.path.join(self._tmp.name, name)
        im.save(p)
        return p

    def test_crop_and_scale_exact(self):
        src = self.img("in.png")
        dst = os.path.join(self._tmp.name, "out.png")
        out = zoom.zoom(src, dst, (2, 1, 3, 2), 4)
        self.assertEqual(out.size, (12, 8))
        im = Image.open(dst).convert("RGB")
        # Nearest-neighbour: every 4x4 block is one source pixel, block (0,0) = src (2,1).
        for bx, by, sx, sy in ((0, 0, 2, 1), (1, 0, 3, 1), (2, 1, 4, 2)):
            expect = (sx * 10, sy * 20, 7)
            for dx in range(4):
                for dy in range(4):
                    self.assertEqual(im.getpixel((bx * 4 + dx, by * 4 + dy)), expect,
                                     (bx, by))

    def test_rect_bounds_checked(self):
        src = self.img("in.png")
        dst = os.path.join(self._tmp.name, "out.png")
        with self.assertRaises(ValueError):
            zoom.zoom(src, dst, (18, 0, 5, 5), 2)     # x+w > 20
        with self.assertRaises(ValueError):
            zoom.zoom(src, dst, (0, 0, 0, 5), 2)      # empty

    def test_cli(self):
        src = self.img("in.png")
        dst = os.path.join(self._tmp.name, "out.png")
        self.assertEqual(zoom.main([src, dst, "--rect", "0,0,5,5", "--scale", "3"]), 0)
        self.assertEqual(Image.open(dst).size, (15, 15))
        self.assertEqual(zoom.main([src, dst, "--rect", "0,0,99,5"]), 2)   # out of bounds
        self.assertEqual(zoom.main([src, dst, "--rect", "0,0,5,5", "--scale", "0"]), 2)


if __name__ == "__main__":
    unittest.main()
