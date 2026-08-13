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
#              ttf          pt  data/fnt_<sym>.bin  lineFeed == draw px
bake_checked sg-bold.ttf   10  sg_bold_17          17   # TXT_TITLE   screen titles
bake_checked sg-bold.ttf    7  sg_bold_12          12   # TXT_BUTTON  button labels, steppers
bake_checked sg-med.ttf     7  sg_med_12           12   # TXT_BODY    list rows, names, prose
bake_checked sg-med.ttf     6  sg_med_10           10   # TXT_SEG     segmented-control labels
bake_checked jbm-med.ttf    5  jbm_med_9            9   # TXT_SECTION section labels, HUD, hints
bake_checked jbm-med.ttf    4  jbm_med_7            7   # TXT_CHIP    chips, badges, ROM codes
bake_checked jbm-bold.ttf   6  jbm_bold_11         11   # TXT_VALUE   caps / emphasised values
# The four phase-17 faces (fnt_sg_bold/sg_med/jbm_med/jbm_bold) are GONE: none of them could be
# drawn at 1.0 by any call site in the app. Remove stale copies so bin2s cannot resurrect them.
rm -f "$DATA/fnt_sg_bold.bin" "$DATA/fnt_sg_med.bin" "$DATA/fnt_jbm_med.bin" "$DATA/fnt_jbm_bold.bin"

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
# lines ago, so all seven get the same treatment; skipping the bold cuts would leave the ladder
# with two different edge profiles.
echo "== stem-snap (alpha LUT) =="
python3 "$ROOT/tools/fontlab/sharpen.py" apply "$DATA" --force || {
	echo "  !! sharpen.py failed or a face stayed below the solidity floor." >&2; exit 1; }

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
