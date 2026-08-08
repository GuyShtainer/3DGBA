# test_sheet.py — host-pure tests for sheet.py (phase-16 slice E4): item parsing (markers
# recognized from the right so captions may contain ':'), dual-screen-geometry cells
# (400x240 + 320x240 in one sheet — the PHASE.md "sheet adapted to dual-screen geometry"
# deliverable), self-contained HTML output.
# Run line:
#   tools/emutest/.venv/bin/python -m unittest discover -s tools/emutest/tests -p 'test_*.py' -v

import os
import sys
import tempfile
import unittest

from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import sheet


class TestParseItem(unittest.TestCase):
    def test_bare_path(self):
        it = sheet.parse_item("/x/top.png")
        self.assertEqual(it["path"], "/x/top.png")
        self.assertEqual(it["caption"], "top.png")   # falls back to the basename

    def test_caption_and_markers(self):
        it = sheet.parse_item("a.png:BEFORE — press R:ring=10,20:good")
        self.assertEqual(it["caption"], "BEFORE — press R")
        self.assertEqual(it["ring"], (10, 20))
        self.assertTrue(it["good"])

    def test_caption_with_colon(self):
        # Markers are recognized from the RIGHT; the remaining segments re-join as the
        # caption, so a caption containing ':' survives.
        it = sheet.parse_item("a.png:tilt: level 3:rect=1,2,3,4:bad")
        self.assertEqual(it["caption"], "tilt: level 3")
        self.assertEqual(it["rect"], (1, 2, 3, 4))
        self.assertIs(it["good"], False)

    def test_bad_marker_exits(self):
        with self.assertRaises(SystemExit):
            sheet.parse_item("a.png:cap:ring=1,2,3")   # ring wants exactly x,y


class TestBuildSheet(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()

    def tearDown(self):
        self._tmp.cleanup()

    def img(self, name, size, color=(10, 20, 30)):
        p = os.path.join(self._tmp.name, name)
        Image.new("RGB", size, color).save(p)
        return p

    def test_dual_screen_cells_inlined(self):
        # One 400x240 top crop + one 320x240 bottom crop — each cell keeps its natural
        # size (the dual-screen adaptation; the original hardcoded 240x160).
        top = self.img("top.png", (400, 240))
        bot = self.img("bottom.png", (320, 240))
        out = os.path.join(self._tmp.name, "s.html")
        items = [sheet.parse_item(top + ":top screen:good"),
                 sheet.parse_item(bot + ":bottom screen")]
        sheet.build_sheet(out, items, title="dual <screen> & sheet")
        with open(out) as f:
            doc = f.read()
        self.assertEqual(doc.count("data:image/png;base64,"), 2)  # self-contained
        self.assertIn('width="400" height="240"', doc)
        self.assertIn('width="320" height="240"', doc)
        self.assertIn("dual &lt;screen&gt; &amp; sheet", doc)     # escaped title
        self.assertIn('figure class="good"', doc)
        self.assertIn('figure class="neutral"', doc)

    def test_markers_drawn_into_pixels(self):
        # A ring marker changes pixels near its center (drawn INTO the image, module doc).
        p = self.img("a.png", (100, 80))
        png, w, h = sheet.render_item(
            {"path": p, "caption": "", "ring": (50, 40), "rect": None, "good": False})
        self.assertEqual((w, h), (100, 80))
        import io
        im = Image.open(io.BytesIO(png)).convert("RGB")
        rr = max(9, 80 // 24)  # the ring radius rule
        self.assertEqual(im.getpixel((50 + rr, 40)), sheet.BAD)  # colour ring on the rim
        self.assertEqual(im.getpixel((50, 40)), (10, 20, 30))    # center untouched

    def test_zoom_scales_cells(self):
        p = self.img("a.png", (40, 30))
        png, w, h = sheet.render_item(
            {"path": p, "caption": "", "ring": None, "rect": None, "good": None}, zoom=3)
        self.assertEqual((w, h), (120, 90))

    def test_cli_missing_image(self):
        out = os.path.join(self._tmp.name, "s.html")
        self.assertEqual(sheet.main([out, os.path.join(self._tmp.name, "nope.png")]), 2)


if __name__ == "__main__":
    unittest.main()
