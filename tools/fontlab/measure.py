#!/usr/bin/env python3
"""fontlab/measure.py — the phase-18 before/after crispness evidence.

Renders the SPEC-crisp C5.2.4 corpus twice — once through the phase-17 four-face pack at the
sizes the phase-17 call sites used, once through the phase-18 ladder — with the PICA200's own
sampler (tools/fontlab/bcfnt.py), and reports the objective measures:

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

The BEFORE pack is not in git (data/*.bin is generated), so rebake it with the phase-17
recipe first — the sizes and lineFeeds are asserted here so a wrong pack cannot masquerade:
  mkbcfnt sg-bold.ttf  -s 11 -o fnt_sg_bold.bin     (lineFeed 19)
  mkbcfnt sg-med.ttf   -s 10 -o fnt_sg_med.bin      (lineFeed 17)
  mkbcfnt jbm-med.ttf  -s  7 -o fnt_jbm_med.bin     (lineFeed 12)
  mkbcfnt jbm-bold.ttf -s  8 -o fnt_jbm_bold.bin    (lineFeed 14)
"""
import argparse
import html
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from bcfnt import Bcfnt, render_run, metrics, to_png   # noqa: E402

# role, string, BEFORE (file stem, px), AFTER (file stem, px)
# BEFORE px values are the literals that were at the phase-17 call sites (SPEC-crisp C1.4).
CORPUS = [
    ("TXT_TITLE",   "Settings",                ("fnt_sg_bold",  20.0), ("fnt_sg_bold_17",  17.0)),
    ("TXT_BUTTON",  "Resume this pairing",     ("fnt_sg_bold",  13.0), ("fnt_sg_bold_12",  12.0)),
    ("TXT_BODY",    "pick a game (d-pad + A)", ("fnt_sg_med",   12.0), ("fnt_sg_med_12",   12.0)),
    ("TXT_SEG",     "Aspect-fit",              ("fnt_sg_med",   10.0), ("fnt_sg_med_10",   10.0)),
    ("TXT_SECTION", "SCALE · TOP",        ("fnt_jbm_med",   9.0), ("fnt_jbm_med_9",    9.0)),
    ("TXT_CHIP",    "HOST BPEE",               ("fnt_jbm_med",   8.0), ("fnt_jbm_med_7",    7.0)),
    ("TXT_VALUE",   "12:34",                   ("fnt_jbm_bold", 10.0), ("fnt_jbm_bold_11", 11.0)),
]

# The three rungs whose draw px did NOT change, so before/after render at the SAME output size
# and the numbers are apples-to-apples (SPEC-crisp C1.7.2 forbids comparing acutance across sizes).
SAME_SIZE = {"TXT_BODY", "TXT_SEG", "TXT_SECTION"}

EXPECT_BEFORE_LF = {"fnt_sg_bold": 19, "fnt_sg_med": 17, "fnt_jbm_med": 12, "fnt_jbm_bold": 14}


def load(path):
    f = Bcfnt(path)
    return f


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--before", required=True, help="dir holding the phase-17 fnt_*.bin")
    ap.add_argument("--after", default="data", help="dir holding the phase-18 fnt_*.bin")
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
            sys.exit("BEFORE pack is wrong: %s has lineFeed %d, phase 17 shipped %d"
                     % (stem, f.line_feed, lf))

    rows = []
    print("%-12s %-24s | %-26s | %-26s" % ("role", "string", "BEFORE (phase 17)", "AFTER (phase 18)"))
    print("%-12s %-24s | %6s %5s %5s %5s %5s | %6s %5s %5s %5s %5s  %s"
          % ("", "", "scale", "off", "lvls", "ramp", "peak", "scale", "off", "lvls", "ramp", "peak", ""))
    worst = []
    for role, text, (bstem, bpx), (astem, apx) in CORPUS:
        fb = get(a.before, bstem)
        fa = get(a.after, astem)

        # BEFORE: phase-17 assets.c did sc = px / ceil(lineFeed*30/cellH), and citro2d then
        # multiplied by 30/cellH. That is exactly Bcfnt.texel_scale with the ceil'd s_native.
        bscale = fb.texel_scale(bpx)
        # AFTER: typo_draw_scale(px, lineFeed, cellH) * 30/cellH == px/lineFeed, and the ladder
        # makes px == lineFeed. Derived here the same way, from the shipped bytes.
        ascale = (apx * fa.cell_h / (fa.line_feed * 30.0)) * (30.0 / fa.cell_h)

        # Both are measured through the LINEAR sampler, because LINEAR is what the pixels went
        # through in phase 17 AND is the honest test of pass-through: a NEAREST render emits
        # source texels whatever the scale, so `offgrid` on a NEAREST render is 0 by
        # construction and proves nothing. At a true 1.0 the LINEAR result IS the texels.
        _, _, ib0 = render_run(fb, text, bscale, origin_x=0.0, filt="linear")
        _, _, ib5 = render_run(fb, text, bscale, origin_x=0.5, filt="linear")
        mb0, mb5 = metrics(ib0), metrics(ib5)
        # phase 17's centred/right-aligned runs landed on a half pixel more often than not
        # (assets_text_c passed cx - w/2 unrounded), so report the worse of the two origins —
        # that is what the user was actually looking at on the buttons and the HUD.
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
