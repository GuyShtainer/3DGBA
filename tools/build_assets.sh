#!/usr/bin/env bash
# build_assets.sh — turn the design_handoff art pack into embeddable 3DS assets.
# Fonts (OFL Space Grotesk + JetBrains Mono) -> .bcfnt; plates + widget atlas -> .t3x.
# Everything lands in data/ as *.bin so the Makefile's bin2s rule embeds it into the ELF
# (works for both .3dsx and .cia; no romfs plumbing). Re-runnable; fonts are cached.
set -euo pipefail
export DEVKITPRO=${DEVKITPRO:-/opt/devkitpro}
export PATH="$DEVKITPRO/tools/bin:$PATH"

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ART="$ROOT/design_handoff_3dgba_ui/assets_3ds"
DATA="$ROOT/data"
FCACHE="$ROOT/tools/.fontcache"
THEME="${1:-indigo}"
mkdir -p "$DATA" "$FCACHE"

echo "== fonts =="
fetch() { [ -s "$FCACHE/$2" ] || curl -sL --max-time 40 "$1" -o "$FCACHE/$2"; }
# JetBrains Mono ships static TTFs; Space Grotesk on Google Fonts is a variable font, so
# fetch it once and instance the weights we need with fonttools (both OFL, embeddable).
fetch "https://github.com/JetBrains/JetBrainsMono/raw/master/fonts/ttf/JetBrainsMono-Medium.ttf" jbm-med.ttf
fetch "https://github.com/JetBrains/JetBrainsMono/raw/master/fonts/ttf/JetBrainsMono-Bold.ttf"   jbm-bold.ttf
fetch "https://cdn.jsdelivr.net/gh/google/fonts@main/ofl/spacegrotesk/SpaceGrotesk%5Bwght%5D.ttf" sg-var.ttf
[ -s "$FCACHE/sg-bold.ttf" ] || python3 -m fontTools.varLib.instancer "$FCACHE/sg-var.ttf" wght=700 -o "$FCACHE/sg-bold.ttf" >/dev/null
[ -s "$FCACHE/sg-med.ttf"  ] || python3 -m fontTools.varLib.instancer "$FCACHE/sg-var.ttf" wght=500 -o "$FCACHE/sg-med.ttf"  >/dev/null
# PHASE 18 / SPEC-crisp. Every face is baked so that its FINF.lineFeed is EXACTLY the px the
# app draws it at (source/typography.h, R1) — a bcfnt drawn at any other size is a resampled
# bitmap, which is what made the phase-17 UI blurry on hardware. mkbcfnt's pt -> lineFeed map is
# quantised and version-dependent, so `bake` PARSES the produced file and fails if the rung moved:
# a silent mkbcfnt bump is exactly how the previous ladder rotted (the old comments here claimed
# "~14px native" for a face whose lineFeed was 19).
#
bake_checked() {   # <ttf> <pt> <sym> <expected lineFeed = the px it is drawn at>
	mkbcfnt "$FCACHE/$1" -s "$2" -o "$DATA/fnt_$3.bin"
	read -r LF CW CH <<EOF
$(python3 -c "
b=open('$DATA/fnt_$3.bin','rb').read()
i=b.find(b'FINF'); j=b.find(b'TGLP')
print(b[i+9], b[j+8], b[j+9])")
EOF
	if [ "$LF" != "$4" ]; then
		echo "  !! fnt_$3: mkbcfnt -s $2 gave lineFeed=$LF, the ladder declares $4." >&2
		echo "     Re-measure the pt->lineFeed table and update source/typography.h." >&2
		exit 1
	fi
	printf '  fnt_%-12s -s %-2s  lineFeed=%-3s cell=%sx%-3s  (drawn at %s px, scale 1.000)\n' \
	       "$3" "$2" "$LF" "$CW" "$CH" "$4"
}
# PHASE 19 / SPEC-legible L1.5.2. Same law (lineFeed == draw px), higher rungs: mkbcfnt -s N is
# POINTS at 96 dpi (em = N*4/3, lineFeed ~= 1.28*em), so phase 18's "-s 7 gives 12 px" faces were
# em 9.3 where the design says em 12 — 0.78x, uniformly, which is the whole "everything is too
# small" complaint. Five faces now, not seven: TXT_SEG draws the BODY face and TXT_CHIP draws the
# SECTION face (typography.h), which removes two ~527 KB bakes and ~2 MB of runtime linear heap.
#              ttf          pt  data/fnt_<sym>.bin  lineFeed == draw px
bake_checked sg-bold.ttf   11  sg_bold_19          19   # TXT_TITLE            screen titles
bake_checked sg-bold.ttf    9  sg_bold_15          15   # TXT_BUTTON           button labels, steppers
bake_checked sg-med.ttf     9  sg_med_15           15   # TXT_BODY + TXT_SEG   rows, names, prose, segs
bake_checked jbm-med.ttf    7  jbm_med_12          12   # TXT_SECTION + TXT_CHIP  captions, HUD, chips
bake_checked jbm-bold.ttf   7  jbm_bold_12         12   # TXT_VALUE            caps / emphasised values
# Stale faces MUST go before sharpen.py runs (it globs data/fnt_*.bin) and before bin2s sees them:
# a leftover .bin is another ~527 KB embedded, and T5 in the host suite fails on it. Two vintages
# to clear: the phase-17 four (drawable at 1.0 by nobody) and the phase-18 seven (0.78x too small).
rm -f "$DATA/fnt_sg_bold.bin" "$DATA/fnt_sg_med.bin" "$DATA/fnt_jbm_med.bin" "$DATA/fnt_jbm_bold.bin" \
      "$DATA/fnt_sg_bold_17.bin" "$DATA/fnt_sg_bold_12.bin" "$DATA/fnt_sg_med_12.bin" \
      "$DATA/fnt_sg_med_10.bin"  "$DATA/fnt_jbm_med_9.bin"  "$DATA/fnt_jbm_med_7.bin" \
      "$DATA/fnt_jbm_bold_11.bin"

# PHASE 18 / the SECOND cause of the blur the user reported. Getting the draw to texel scale
# 1.0 (above) stops the app RESAMPLING the bitmap; it cannot fix a bitmap that was soft when
# it was baked. mkbcfnt v2.3.0 rasterises with unhinted grayscale AA and exposes no hinting,
# gamma or contrast switch, so at 7-12 px a Medium stem straddles two pixel columns and lands
# on neither: fnt_jbm_med_7 shipped with ZERO fully-opaque texels in the whole alphabet and
# 87.7% of its ink stranded between 25% and 75%. sharpen.py does the part of hinting that
# needs no outline surgery — a monotone 16-entry alpha LUT per face (a bcfnt sheet is A4, so
# 16 entries is the complete space of order-preserving alpha edits). Metrics only; no header,
# no advance width and no byte outside the glyph sheets changes, so the R1 ladder above and
# every layout measurement in test_typography T10 are untouched.
# --force: sharpen.py's default refuses a face that already clears the solidity floor, which is
# the right guard for a HAND run (the pass is not idempotent — its gain stage renormalises
# against the face's own distribution). Here every .bin was rasterised fresh by mkbcfnt three
# lines ago, so all five get the same treatment; skipping the bold cuts would leave the ladder
# with two different edge profiles.
echo "== stem-snap (alpha LUT) =="
python3 "$ROOT/tools/fontlab/sharpen.py" apply "$DATA" --force || {
	echo "  !! sharpen.py failed or a face stayed below the solidity floor." >&2; exit 1; }

# PHASE 19 / SPEC-legible L3.2.7 — THE INK BOX, reported at bake time.
# source/typography.h now DECLARES cellH / inkTop / inkH per rung, because typo_center_y (every
# "centre a label in a box" site in the app) and every fixed row offset in §L3.2 are computed from
# them. lineFeed is guarded above by bake_checked; this is the same class of number and it moves
# for the same reason (an mkbcfnt bump changes the rasterisation), so the bake PRINTS it and the
# host suite ASSERTS it: test_typography T18 re-measures these bytes and fails on any drift, and
# T16 then re-grades every row against the new box. Measured over "AHgpy1:9" — caps, ascender,
# descenders, digits, colon: the tallest and deepest thing a UI string can contain.
echo "== ink box (compare with source/typography.h; test_typography T18 asserts it) =="
python3 - "$DATA" "$ROOT/tools/fontlab" <<'INKPY'
import sys, glob, os
sys.path.insert(0, sys.argv[2])   # $ROOT/tools/fontlab (the heredoc is quoted: no shell expansion)
from bcfnt import Bcfnt
for path in sorted(glob.glob(os.path.join(sys.argv[1], "fnt_*.bin"))):
    f = Bcfnt(path)
    top = bot = None
    for ch in "AHgpy1:9":
        gi = f.glyph_index(ch)
        if gi is None or gi < 0:
            continue
        for y, row in enumerate(f.glyph_bitmap(gi)):
            if any(v > 0 for v in row):
                top = y if top is None or y < top else top
                bot = y if bot is None or y > bot else bot
    cap = None
    gi = f.glyph_index("H")
    if gi is not None and gi >= 0:
        rows = [y for y, r in enumerate(f.glyph_bitmap(gi)) if any(v > 0 for v in r)]
        cap = rows[-1] - rows[0] + 1 if rows else None
    print("  %-16s cell %2d  ink rows %2d..%-2d (inkTop %d inkH %d)  cap %s"
          % (os.path.basename(path)[4:-4], f.cell_h, top, bot, top, bot - top + 1, cap))
INKPY

# This tex3ds build only does single-image -> t3x (no atlas / .t3s), so every plate and every
# widget becomes its own tiny compressed t3x. The loader (assets.c) loads each into a 1-image
# spritesheet. A generated header lists the names as X-macros so the loader stays in sync.
GEN="$ROOT/source/assets_gen.h"
# PHASE 17 / SPEC-widgets W4.3.a: stamp WHICH theme's art is embedded. Only one pack is baked, but
# every label used to take its colour from the ACTIVE theme — Daylight ink on indigo art measured
# 1.2:1 (sweep D10). assets_init() feeds this to theme_init_art() so ink on a baked surface comes
# from the art's own palette. Keep the ids in step with ThemeId in source/theme.h.
case "$THEME" in indigo) TID=0;; oled) TID=1;; daylight) TID=2;; duo) TID=3;;
                 retro) TID=4;;  custom) TID=5;; *) TID=0;; esac
{
  echo "// GENERATED by tools/build_assets.sh — do not edit. Plate + widget id lists."
  echo "#pragma once"
  echo "// The ThemeId (source/theme.h) whose art pack is embedded in data/ — read by assets_init()."
  echo "#define ASSET_THEME_ID $TID"
  echo "// X(id, sym) : id = C string used at runtime; sym = the embedded _bin symbol stem."
  echo "#define ASSET_PLATES \\"
} > "$GEN"

echo "== plates ($THEME) =="
for png in "$ART/plates/$THEME"/*.png; do
  name="$(basename "$png" .png)"; sym="pl_$(echo "$name" | tr '-' '_')"
  tex3ds -f rgba8 -z auto "$png" -o "$DATA/$sym.bin"
  printf '\tX("%s", %s) \\\n' "$name" "$sym" >> "$GEN"
done
echo >> "$GEN"; echo "#define ASSET_WIDGETS \\" >> "$GEN"
echo "  $(ls "$ART/plates/$THEME"/*.png | wc -l | tr -d ' ') plates"

echo "== widgets ($THEME) =="
for png in "$ART/widgets/$THEME"/*.png; do
  name="$(basename "$png" .png)"; sym="wg_$(echo "$name" | tr '-' '_')"
  tex3ds -f rgba8 -z auto "$png" -o "$DATA/$sym.bin"
  printf '\tX("%s", %s) \\\n' "$name" "$sym" >> "$GEN"
done
echo >> "$GEN"
echo "  $(ls "$ART/widgets/$THEME"/*.png | wc -l | tr -d ' ') widgets -> source/assets_gen.h"

echo "== done =="
ls "$DATA" | wc -l | awk '{printf "  %d files, ", $1}'
du -sh "$DATA" | cut -f1
