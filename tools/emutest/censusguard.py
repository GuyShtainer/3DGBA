#!/usr/bin/env python3
"""censusguard.py — the capture-time novelty assertion for census evidence.

WHY THIS EXISTS (phase 29 lane E, EVIDENCE-INTEGRITY.md).
The phase-21 census banked 683 PNGs of which only 649 are byte-distinct. Almost all of
that is benign — the census captures BOTH 3DS screens but only ONE of them is the subject,
and the other holds a second, undriven game whose static screens repeat. But one real
defect hid inside the noise: `firered/I4c-slotspin` is a byte-identical re-shot of
`firered/I4-slotmachine` taken 32 s earlier. The reels never moved, and nothing in the
pipeline noticed, because:

  * `see.py shot` is a single stateless grab — it has no memory of the previous capture;
  * the cb2 read that identified the screen was a SEPARATE gdbio command whose output was
    hand-copied into `CAPTURES-*.log`, so nothing bound the picture to the state; and
  * cb2 identifies a screen FAMILY, not a screen. Both slot captures read the same
    `CB2_RunSlotMachine`, so the operator's own check had no power to separate them.

This module supplies the check that does have power, and it needs no emulator: two
captures of the same screen family must not carry the same GBA frame.

THE SUBJECT RECT — measured, not assumed.
A census capture is a native-resolution 3DS screen (top 400x240, bottom 320x240) with the
emulated 240x160 GBA frame drawn inside it. Everything else is app chrome: the top
screen's HUD strip (title, FPS, clock — which CHANGES every capture and therefore masks
duplicates from a whole-file hash) and the bottom screen's touch-gamepad skin (which is
static). Calibrated 2026-08-15 by per-pixel variation across 40 captures of each screen:

    emerald/*.top.png    varying bbox (ignoring the HUD strip y<30) = (80,40)-(320,200)
    firered/*.bottom.png varying bbox                               = (40,40)-(280,200)

Both are exactly 240x160 centred horizontally at y=40, i.e. ((W-240)//2, (H-160)//2) —
one formula, verified on both screen widths. Hash THAT and the HUD clock stops hiding
repeats.

CLI:
  censusguard frame CAP.png OUT.png            write the 240x160 subject frame
  censusguard digest CAP.png [CAP.png ...]     print the subject-frame sha256 per file
  censusguard check NEW.png --against F|DIR..  exit 1 if NEW repeats a banked frame
  censusguard bank SHOT.png OUT.png --screen top|bottom --against DIR
                                               reconstruct to native res, ASSERT, then write
  censusguard audit DIR [DIR ...]              the whole duplicate map, subject vs companion

`bank` is the verb the next census pass should use. `see shot` + `native.py` + a manual
eyeball is what produced I4c-slotspin; `bank` makes the banked file a PRODUCT of the check,
so the check cannot be skipped — nothing is written when the frame is a repeat or a blank.

Exit codes: 0 ok · 1 a repeat / blank frame was found · 2 usage or unreadable input.
"""

import hashlib
import os
import sys

GBA_W, GBA_H = 240, 160
# The two screens a census capture can be. Anything else is not a native 3DS screen.
NATIVE_SCREENS = {(400, 240): "top", (320, 240): "bottom"}
# The subject screen per evidence directory: which of the two screens holds the game
# under census. Emerald ran as gameA = TOP (seat 1); FireRed as gameB = BOTTOM (seat 2)
# — VISITED-emerald.md:4 and VISITED-firered.md:4. The other file in the pair photographs
# the OTHER, undriven game and carries no claim.
SUBJECT_SCREEN = {"emerald": "top", "firered": "bottom"}


def subject_rect(size):
    """(W, H) of a native 3DS screen -> the GBA frame rect (x0, y0, x1, y1).

    Centred: x0 = (W-240)//2, y0 = (H-160)//2. Measured on both widths (module doc)."""
    w, h = size
    if (w, h) not in NATIVE_SCREENS:
        raise ValueError("not a native 3DS screen: {}x{}".format(w, h))
    x0, y0 = (w - GBA_W) // 2, (h - GBA_H) // 2
    return (x0, y0, x0 + GBA_W, y0 + GBA_H)


def gba_frame(path):
    """-> PIL RGB image of the 240x160 emulated frame inside a native-res capture."""
    from PIL import Image
    im = Image.open(path).convert("RGB")
    return im.crop(subject_rect(im.size))


def frame_digest(path):
    """sha256 of the subject frame's raw pixels — HUD/clock/FPS independent."""
    return hashlib.sha256(gba_frame(path).tobytes()).hexdigest()


def dark_fraction(path, threshold=16):
    """Fraction of the subject frame darker than `threshold` (0..1).

    A fully faded / not-yet-drawn screen is ~1.0. Used only as a loud tripwire: the
    census's darkest real subject frame measures 0.40 of its own GBA frame, so 0.99 is a
    blank frame and not a screen."""
    from PIL import Image
    im = gba_frame(path).convert("L")
    px = list(im.getdata())
    return sum(1 for v in px if v < threshold) / float(len(px))


def is_subject(path):
    """True when `path` is the SUBJECT screen of its evidence directory (SUBJECT_SCREEN).

    Falls back to True for paths outside the two census dirs — a lone capture with no
    companion is its own subject."""
    parts = os.path.abspath(path).split(os.sep)
    base = os.path.basename(path)
    for d, screen in SUBJECT_SCREEN.items():
        if d in parts:
            if base.endswith(".top.png"):
                return screen == "top"
            if base.endswith(".bottom.png"):
                return screen == "bottom"
            return True
    return True


def _pngs(paths):
    out = []
    for p in paths:
        if os.path.isdir(p):
            for root, _dirs, files in os.walk(p):
                out += [os.path.join(root, f) for f in sorted(files) if f.endswith(".png")]
        elif p.endswith(".png"):
            out.append(p)
    return out


def duplicate_map(paths, subject_only=True):
    """-> {digest: [path, ...]} for every subject frame that appears more than once.

    Non-native captures (anything that is not 400x240 / 320x240) are skipped and returned
    separately, never silently dropped."""
    groups, skipped = {}, []
    for p in _pngs(paths):
        if subject_only and not is_subject(p):
            continue
        try:
            d = frame_digest(p)
        except ValueError as e:
            skipped.append((p, str(e)))
            continue
        groups.setdefault(d, []).append(p)
    return {k: v for k, v in groups.items() if len(v) > 1}, skipped


# ------------------------------------------------------------------------------ CLI

def _cmd_frame(argv):
    if len(argv) != 2:
        print("usage: censusguard frame CAP.png OUT.png", file=sys.stderr)
        return 2
    gba_frame(argv[0]).save(argv[1])
    print("censusguard: {} -> {} ({}x{})".format(argv[0], argv[1], GBA_W, GBA_H))
    return 0


def _cmd_digest(argv):
    if not argv:
        print("usage: censusguard digest CAP.png [CAP.png ...]", file=sys.stderr)
        return 2
    for p in _pngs(argv):
        print("{}  {}".format(frame_digest(p), p))
    return 0


def _cmd_check(argv):
    if "--against" not in argv:
        print("usage: censusguard check NEW.png --against F.png|DIR [...] "
              "[--max-dark F]", file=sys.stderr)
        return 2
    i = argv.index("--against")
    new = argv[:i]
    rest = argv[i + 1:]
    max_dark = 0.99
    if "--max-dark" in rest:
        j = rest.index("--max-dark")
        max_dark = float(rest[j + 1])
        rest = rest[:j] + rest[j + 2:]
    if len(new) != 1:
        print("censusguard: check takes exactly one NEW.png", file=sys.stderr)
        return 2
    new = new[0]
    try:
        d = frame_digest(new)
        dark = dark_fraction(new)
    except (ValueError, OSError) as e:
        print("censusguard: FAIL — cannot read {} ({})".format(new, e), file=sys.stderr)
        return 2
    rc = 0
    if dark >= max_dark:
        print("censusguard: FAIL — {} is a BLANK frame ({:.3f} of the GBA frame is black). "
              "The screen had not drawn when the shot fired.".format(new, dark),
              file=sys.stderr)
        rc = 1
    hits = [p for p in _pngs(rest)
            if os.path.abspath(p) != os.path.abspath(new) and is_subject(p)
            and _safe_digest(p) == d]
    if hits:
        print("censusguard: FAIL — {} repeats a frame already banked:".format(new),
              file=sys.stderr)
        for h in hits:
            print("    {}".format(h), file=sys.stderr)
        print("  The game did not change between the two shots. Either the nav step did "
              "not land or the capture fired too early — re-drive, do not bank.",
              file=sys.stderr)
        rc = 1
    if rc == 0:
        print("censusguard: ok — novel frame ({}), dark {:.3f}".format(d[:12], dark))
    return rc


def _safe_digest(p):
    try:
        return frame_digest(p)
    except (ValueError, OSError):
        return None


def _cmd_bank(argv):
    """reconstruct a `see shot` crop to native res, ASSERT novelty, only then write.

    The reconstruction is native.py's (a nearest-neighbour downsample that is byte-exact
    because Azahar is pinned to integer-scaling=off + filter_mode=nearest — native.py's
    module doc carries the proof and its --verify reproduces the capture at 0 differing
    pixels). Imported rather than reimplemented so there is one copy of that algorithm."""
    if "--screen" not in argv or "--against" not in argv:
        print("usage: censusguard bank SHOT.png OUT.png --screen top|bottom "
              "--against DIR [--max-dark F]", file=sys.stderr)
        return 2
    i = argv.index("--screen")
    screen = argv[i + 1]
    if screen not in ("top", "bottom"):
        print("censusguard: --screen must be top or bottom", file=sys.stderr)
        return 2
    j = argv.index("--against")
    against = argv[j + 1:]
    max_dark = 0.99
    if "--max-dark" in against:
        k = against.index("--max-dark")
        max_dark = float(against[k + 1])
        against = against[:k] + against[k + 2:]
    pos = argv[:min(i, j)]
    if len(pos) != 2:
        print("censusguard: bank takes SHOT.png and OUT.png before the flags",
              file=sys.stderr)
        return 2
    src, dst = pos
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import native
    from PIL import Image
    w, h = (400, 240) if screen == "top" else (320, 240)
    nat = native.reconstruct(Image.open(src).convert("RGB"), w, h)

    dark = sum(1 for v in nat.crop(subject_rect((w, h))).convert("L").getdata() if v < 16)
    dark /= float(GBA_W * GBA_H)
    digest = hashlib.sha256(nat.crop(subject_rect((w, h))).tobytes()).hexdigest()
    if dark >= max_dark:
        print("censusguard: NOT banked — {} would be a BLANK frame ({:.3f} black). The "
              "screen had not drawn when the shot fired.".format(dst, dark),
              file=sys.stderr)
        return 1
    hits = [p for p in _pngs(against) if is_subject(p) and _safe_digest(p) == digest]
    if hits:
        print("censusguard: NOT banked — {} repeats a frame already banked:".format(dst),
              file=sys.stderr)
        for hpath in hits:
            print("    {}".format(hpath), file=sys.stderr)
        print("  The game did not change between the two shots. Either the nav step did "
              "not land or the capture fired too early — re-drive, do not bank.",
              file=sys.stderr)
        return 1
    d = os.path.dirname(os.path.abspath(dst))
    if d:
        os.makedirs(d, exist_ok=True)
    nat.save(dst)
    print("censusguard: banked {} ({}x{}, frame {}, dark {:.3f})".format(
        dst, w, h, digest[:12], dark))
    return 0


# Directories whose contents are NOT a census claim.
#   aux/       — a scratch pad: while a nav step is retried the operator legitimately
#                re-shoots an unchanged screen.
#   withdrawn/ — captures a later pass proved carry no claim of their own. Kept, never
#                deleted (evidence is not destroyed), but excluded from the audit.
NON_CLAIM_DIRS = ("aux", "withdrawn")


def is_aux(path):
    """True for a capture under a non-claim directory (NON_CLAIM_DIRS).

    Two non-claim captures sharing a frame is normal; two CENSUS ROWS sharing one means
    one of them has no evidence of its own."""
    p = os.path.abspath(path) + os.sep
    return any((os.sep + d + os.sep) in p for d in NON_CLAIM_DIRS)


def _cmd_audit(argv):
    if not argv:
        print("usage: censusguard audit DIR [DIR ...]", file=sys.stderr)
        return 2
    dups, skipped = duplicate_map(argv, subject_only=True)
    subjects = [p for p in _pngs(argv) if is_subject(p)]
    rows = [p for p in subjects if not is_aux(p)]
    rowdups = {d: f for d, f in dups.items()
               if sum(1 for p in f if not is_aux(p)) > 1}
    auxdups = {d: f for d, f in dups.items() if d not in rowdups}
    print("censusguard: {} census-row subject frames (+{} non-claim: aux/ + withdrawn/), "
          "{} duplicate group(s) — {} naming two census rows, {} non-claim only".format(
              len(rows), len(subjects) - len(rows), len(dups), len(rowdups), len(auxdups)))
    for label, group in (("CENSUS ROW", rowdups), ("aux", auxdups)):
        for d, files in sorted(group.items(), key=lambda kv: -len(kv[1])):
            print("  [{}] {}  n={}".format(label, d[:12], len(files)))
            for f in sorted(files):
                print("      {}".format(f))
    if skipped:
        print("  ({} file(s) skipped — not a native 3DS screen)".format(len(skipped)))
    return 1 if rowdups else 0


def main(argv=None):
    argv = list(sys.argv[1:] if argv is None else argv)
    if not argv:
        print(__doc__.strip().split("CLI:")[-1].strip(), file=sys.stderr)
        return 2
    cmd, rest = argv[0], argv[1:]
    table = {"frame": _cmd_frame, "digest": _cmd_digest, "check": _cmd_check,
             "bank": _cmd_bank, "audit": _cmd_audit}
    if cmd not in table:
        print("censusguard: unknown command {!r}".format(cmd), file=sys.stderr)
        return 2
    return table[cmd](rest)


if __name__ == "__main__":
    raise SystemExit(main())
