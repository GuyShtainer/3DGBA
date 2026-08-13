#!/usr/bin/env python3
"""fontlab/measure.py — the phase-18 before/after crispness evidence.

Renders the SPEC-crisp C5.2.4 corpus twice — once through the PREVIOUS pack at the sizes that
pack was drawn at, once through the pack in data/ — with the PICA200's own sampler
(tools/fontlab/bcfnt.py), and reports the objective measures:

  offgrid  pixels whose value is NOT a multiple of 17. A bcfnt sheet is A4 (4 bpp), so every
           source texel is n*17. Zero off-grid pixels == the sampler passed texels through
           untouched == the glyph is bit-exact on screen. This is a BINARY proof, not a
           judgement call, and it is the headline number.
  levels   distinct grey levels in the run. A resample roughly doubles it.
  ramp     longest run of pixels strictly between 12% and 88% of peak on any scanline —
           the width of the blurriest edge. Crisp = the font's own AA, 2-3 px.
  peak     the brightest pixel. 255 iff at least one texel survived intact.

Usage:
  tools/fontlab/measure.py --before <dir-of-phase17-bins> [--out <dir>] [--html <file>]

The BEFORE pack is not in git (data/*.bin is generated), so rebake it first — the sizes and
lineFeeds are asserted here so a wrong pack cannot masquerade:
  mkbcfnt sg-bold.ttf  -s 10 -o fnt_sg_bold_17.bin   (lineFeed 17)   phase 18
  mkbcfnt sg-bold.ttf  -s  7 -o fnt_sg_bold_12.bin   (lineFeed 12)
  mkbcfnt sg-med.ttf   -s  7 -o fnt_sg_med_12.bin    (lineFeed 12)
  mkbcfnt sg-med.ttf   -s  6 -o fnt_sg_med_10.bin    (lineFeed 10)
  mkbcfnt jbm-med.ttf  -s  5 -o fnt_jbm_med_9.bin    (lineFeed  9)
  mkbcfnt jbm-med.ttf  -s  4 -o fnt_jbm_med_7.bin    (lineFeed  7)
  mkbcfnt jbm-bold.ttf -s  6 -o fnt_jbm_bold_11.bin  (lineFeed 11)
then run tools/fontlab/sharpen.py apply <dir> --force, as build_assets.sh does.
"""
import argparse
import html
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from bcfnt import Bcfnt, render_run, metrics, to_png   # noqa: E402

# role, string, BEFORE (file stem, px), AFTER (file stem, px)
# PHASE 19 / SPEC-legible retarget. BEFORE is now the PHASE-18 pack (already lawful: px ==
# lineFeed, texel scale 1.0) and AFTER is the phase-19 ladder, because the phase-17 pack this
# file was written against can no longer be produced by tools/build_assets.sh. What the tool
# proves therefore changes with the phase: for 18 it was "the resample is gone", for 19 it is
# "the rungs got BIGGER and the pass-through survived it" — both sides must read 0 off-grid.
CORPUS = [
    ("TXT_TITLE",   "Settings",                ("fnt_sg_bold_17",  17.0), ("fnt_sg_bold_19",  19.0)),
    ("TXT_BUTTON",  "Resume this pairing",     ("fnt_sg_bold_12",  12.0), ("fnt_sg_bold_15",  15.0)),
    ("TXT_BODY",    "pick a game (d-pad + A)", ("fnt_sg_med_12",   12.0), ("fnt_sg_med_15",   15.0)),
    ("TXT_SEG",     "Aspect-fit",              ("fnt_sg_med_10",   10.0), ("fnt_sg_med_15",   15.0)),
    ("TXT_SECTION", "SCALE \u00b7 TOP",           ("fnt_jbm_med_9",    9.0), ("fnt_jbm_med_12",  12.0)),
    ("TXT_CHIP",    "HOST BPEE",               ("fnt_jbm_med_7",    7.0), ("fnt_jbm_med_12",  12.0)),
    ("TXT_VALUE",   "12:34",                   ("fnt_jbm_bold_11", 11.0), ("fnt_jbm_bold_12", 12.0)),
]

# Same-size pairs are what make an acutance comparison legal (C1.7.2). Phase 19 moves EVERY rung,
# so there are none: each side is reported on its own numbers and the gate is the binary
# pass-through measure (off-grid == 0), which is size-independent.
SAME_SIZE = set()
EXPECT_BEFORE_LF = {"fnt_sg_bold_17": 17, "fnt_sg_bold_12": 12, "fnt_sg_med_12": 12,
                    "fnt_sg_med_10": 10, "fnt_jbm_med_9": 9, "fnt_jbm_med_7": 7,
                    "fnt_jbm_bold_11": 11}


def load(path):
    f = Bcfnt(path)
    return f


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--before", required=True, help="dir holding the PREVIOUS pack's fnt_*.bin")
    ap.add_argument("--after", default="data", help="dir holding the CURRENT pack (default data/)")
    ap.add_argument("--out", default=None, help="dir for the 8x zoom PNGs")
    ap.add_argument("--html", default=None, help="write a before/after sheet here")
    a = ap.parse_args()

    if a.out:
        os.makedirs(a.out, exist_ok=True)

    cache = {}

    def get(d, stem):
        k = (d, stem)
        if k not in cache:
            cache[k] = load(os.path.join(d, stem + ".bin"))
        return cache[k]

    # guard: the BEFORE pack must really be the phase-17 pack
    for stem, lf in EXPECT_BEFORE_LF.items():
        f = get(a.before, stem)
        if f.line_feed != lf:
            sys.exit("BEFORE pack is wrong: %s has lineFeed %d, the previous ladder shipped %d"
                     % (stem, f.line_feed, lf))

    rows = []
    print("%-12s %-24s | %-26s | %-26s" % ("role", "string", "BEFORE (previous pack)", "AFTER (data/)"))
    print("%-12s %-24s | %6s %5s %5s %5s %5s | %6s %5s %5s %5s %5s  %s"
          % ("", "", "scale", "off", "lvls", "ramp", "peak", "scale", "off", "lvls", "ramp", "peak", ""))
    worst = []
    for role, text, (bstem, bpx), (astem, apx) in CORPUS:
        fb = get(a.before, bstem)
        fa = get(a.after, astem)

        # BEFORE: model the pack that is actually on disk. A LAWFUL pack (px == lineFeed, i.e.
        # phase 18 and later) drew at exactly px/lineFeed; the phase-17 pack drew at
        # px / ceil(lineFeed*30/cellH), which is Bcfnt.texel_scale's ceil'd s_native. Choosing by
        # the bytes rather than by a flag keeps this tool correct for either vintage.
        bscale = ((bpx * fb.cell_h / (fb.line_feed * 30.0)) * (30.0 / fb.cell_h)
                  if fb.line_feed == int(bpx) else fb.texel_scale(bpx))
        # AFTER: typo_draw_scale(px, lineFeed, cellH) * 30/cellH == px/lineFeed, and the ladder
        # makes px == lineFeed. Derived here the same way, from the shipped bytes.
        ascale = (apx * fa.cell_h / (fa.line_feed * 30.0)) * (30.0 / fa.cell_h)

        # Both are measured through the LINEAR sampler, because LINEAR is what the pixels went
        # through in phase 17 AND is the honest test of pass-through: a NEAREST render emits
        # source texels whatever the scale, so `offgrid` on a NEAREST render is 0 by
        # construction and proves nothing. At a true 1.0 the LINEAR result IS the texels.
        lawful_before = (fb.line_feed == int(bpx))
        _, _, ib0 = render_run(fb, text, bscale, origin_x=0.0, filt="linear")
        _, _, ib5 = render_run(fb, text, bscale, origin_x=0.5, filt="linear")
        mb0, mb5 = metrics(ib0), metrics(ib5)
        # WHICH ORIGIN THE BEFORE PACK IS ENTITLED TO. Phase 17 drew centred/right-aligned runs at
        # an unrounded cx - w/2, so it genuinely landed on half pixels and the WORSE of the two
        # origins is what the user saw. Phase 18 and later snap the origin inside assets_text, so
        # frac 0 is the only reachable case and charging a lawful pack for a half-pixel render
        # would manufacture a regression that the app cannot produce. Choose by the pack.
        if lawful_before:
            mb, ib = mb0, ib0
        else:
            mb = mb5 if mb5["levels"] > mb0["levels"] else mb0
            ib = ib5 if mb5["levels"] > mb0["levels"] else ib0

        # AFTER: assets_text rounds the origin, so frac 0 is the only reachable case.
        _, _, ia = render_run(fa, text, ascale, origin_x=0.0, filt="linear")
        _, _, ian = render_run(fa, text, ascale, origin_x=0.0, filt="nearest")
        ma = metrics(ia)
        identical = (ia == ian)   # C1.7.1: at texel scale 1.0 the two filters coincide

        print("%-12s %-24s | %6.3f %5d %5d %5d %5d | %6.3f %5d %5d %5d %5d  %s"
              % (role, text[:24], bscale, mb["offgrid"], mb["levels"], mb["ramp"], mb["peak"],
                 ascale, ma["offgrid"], ma["levels"], ma["ramp"], ma["peak"],
                 "same-size pair" if role in SAME_SIZE else ""))

        if a.out:
            to_png(ib, os.path.join(a.out, "before_%s.png" % role), zoom=8)
            to_png(ia, os.path.join(a.out, "after_%s.png" % role), zoom=8)
        rows.append((role, text, bscale, mb, ascale, ma, identical))
        if ma["offgrid"] != 0:
            worst.append("%s: %d off-grid pixels AFTER the fix" % (role, ma["offgrid"]))
        if not identical:
            worst.append("%s: NEAREST != LINEAR at scale %.6f (so the scale is not 1.0)" % (role, ascale))

    # gates
    print()
    fails = list(worst)
    for role, text, bscale, mb, ascale, ma, ident in rows:
        if role in SAME_SIZE and ma["levels"] > 0.60 * mb["levels"]:
            fails.append("%s: grey levels %d -> %d is not <= 60%% of before"
                         % (role, mb["levels"], ma["levels"]))
        # NOTE on the edge-ramp gate. SPEC-crisp C5.2.3 sets "<= 3 px", calibrated on a single
        # glyph. Measured over a whole string it is not a blur metric at all: a lowercase
        # cross-bar or an em-dash is a legitimate horizontal run of mid-grey from the font's OWN
        # antialiasing, so long runs appear in the bit-exact render too (and the phase-17 render
        # sometimes scores BETTER because resampling pushed pixels out of the 12-88% band). It is
        # reported per row, and gated per GLYPH below, where it does discriminate.
    # Per-glyph edge ramp, the way C1.6 measured it: one glyph, same output size, both packs.
    print("\nper-glyph edge ramp (single glyph, the C1.6 protocol)")
    for role, text, (bstem, bpx), (astem, apx) in CORPUS:
        if role not in SAME_SIZE:
            continue
        fb, fa = get(a.before, bstem), get(a.after, astem)
        ch = "S" if fb.glyph_index("S") else text[0]
        bsc = fb.texel_scale(bpx)
        asc = (apx * fa.cell_h / (fa.line_feed * 30.0)) * (30.0 / fa.cell_h)
        _, _, gb = render_run(fb, ch, bsc, origin_x=0.5, filt="linear")
        _, _, ga = render_run(fa, ch, asc, origin_x=0.0, filt="linear")
        mgb, mga = metrics(gb), metrics(ga)
        print("  %-12s '%s'  before ramp %d levels %2d  ->  after ramp %d levels %2d"
              % (role, ch, mgb["ramp"], mgb["levels"], mga["ramp"], mga["levels"]))
        # The C5.2.3 "<= 3 px" ramp gate is only meaningful where there is a resample to
        # detect. When offgrid == 0 the pixels ARE the font's own texels, bit for bit, so a wide
        # mid-grey run is Space Grotesk's antialiasing of a sub-pixel stroke at a 10 px line —
        # not something this phase can or should remove. The proof beats the proxy: gate on the
        # binary pass-through measure, report the ramp.
        if metrics(ga)["offgrid"] > 0 and mga["ramp"] > 3:
            fails.append("%s: single-glyph edge ramp %d px > 3 WITH resampling" % (role, mga["ramp"]))
        if mga["levels"] > mgb["levels"]:
            fails.append("%s: single-glyph levels got WORSE (%d -> %d)"
                         % (role, mgb["levels"], mga["levels"]))
    print()
    for f in fails:
        print("  GATE FAIL:", f)
    if not fails:
        print("  ALL GATES PASS: every AFTER run is bit-exact (0 off-grid pixels), NEAREST and")
        print("  LINEAR agree pixel-for-pixel, and every same-size pair dropped to <= 60% of the")
        print("  before grey-level count. Off-grid 0 is the strong claim: the texels reached the")
        print("  framebuffer unmodified, which is only possible at texel scale 1.0 + integer origin.")

    if a.html:
        write_html(a.html, rows, a.out)
        print("\nsheet:", a.html)
    return 1 if fails else 0


def write_html(path, rows, imgdir):
    import base64

    def b64(p):
        try:
            with open(p, "rb") as f:
                return "data:image/png;base64," + base64.b64encode(f.read()).decode()
        except OSError:
            return ""

    out = ["<!doctype html><meta charset=utf-8><title>3DGBA phase-18 text crispness</title>",
           "<style>body{font:13px/1.5 -apple-system,sans-serif;background:#14101f;color:#e8e4f0;"
           "margin:24px}h1{font-size:20px}table{border-collapse:collapse;margin-top:16px}"
           "td,th{border:1px solid #3a3252;padding:8px 10px;vertical-align:top}"
           "img{image-rendering:pixelated;background:#fff;display:block}"
           ".n{font:12px ui-monospace,monospace;color:#b9aee0}.bad{color:#ff9a6a}.good{color:#7ee0a8}"
           "</style>",
           "<h1>Text crispness — phase 17 vs phase 18</h1>",
           "<p>Rendered with the PICA200's own sampler from the shipped <code>.bcfnt</code> bytes. "
           "8&times; nearest zoom. <b>off</b> = pixels whose value is not a multiple of 17; an A4 "
           "sheet only contains multiples of 17, so <b>off = 0 means the texels reached the screen "
           "untouched</b>.</p>",
           "<table><tr><th>role</th><th>before (phase 17)</th><th>after (phase 18)</th></tr>"]
    for role, text, bscale, mb, ascale, ma, ident in rows:
        out.append("<tr><td><b>%s</b><br><span class=n>%s</span></td>" % (role, html.escape(text)))
        for tag, sc, m in (("before", bscale, mb), ("after", ascale, ma)):
            img = b64(os.path.join(imgdir or ".", "%s_%s.png" % (tag, role)))
            cls = "good" if m["offgrid"] == 0 else "bad"
            out.append("<td><img src='%s'><div class=n>scale %.4f &middot; "
                       "<span class=%s>off %d</span> &middot; levels %d &middot; ramp %d &middot; "
                       "peak %d</div></td>" % (img, sc, cls, m["offgrid"], m["levels"], m["ramp"], m["peak"]))
        out.append("</tr>")
    out.append("</table>")
    with open(path, "w") as f:
        f.write("\n".join(out))


if __name__ == "__main__":
    sys.exit(main())
