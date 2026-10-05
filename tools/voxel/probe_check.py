#!/usr/bin/env python3
"""probe_check.py -- assert the gba_game.h offsets against RAM dumps (3DGBA, GPLv3).
Usage: probe_check.py ROM.gba DUMPDIR [DUMPDIR...]   (dumps from tools/voxel/probe_ram.c)
Each check compares one offset with a relation the game itself guarantees (the player's object
coords equal the save-block position + 7, ROM pointers
point into the ROM, and so on). Prints PASS/FAIL per check, exits 1 on any failure."""
import struct
import sys

fails = 0


def check(name, ok, detail=""):
    global fails
    print(("PASS " if ok else "FAIL ") + name + (" : " + detail if detail else ""))
    fails += 0 if ok else 1


def main():
    rom = open(sys.argv[1], "rb").read()
    for d in sys.argv[2:]:
        E = open(d + "/ewram.bin", "rb").read()
        I = open(d + "/iwram.bin", "rb").read()
        u32 = lambda buf, o: struct.unpack_from("<I", buf, o)[0]
        u16 = lambda buf, o: struct.unpack_from("<H", buf, o)[0]
        rb = lambda a, n: rom[a - 0x8000000:a - 0x8000000 + n]
        tag = d + ": "
        check(tag + "gMain.callback2 = CB2_Overworld", u32(I, 0x22C4) == 0x08085E5D)
        sb = u32(I, 0x5D8C) - 0x2000000
        px, py = struct.unpack_from("<hh", E, sb)
        grp, num = E[sb + 4], E[sb + 5]
        oe = lambda i: E[0x37350 + i * 0x24:0x37350 + (i + 1) * 0x24]
        pa = E[0x37590:0x37590 + 0x24]
        pl = oe(pa[5])
        check(tag + "player avatar -> active isPlayer object", bool(pl[0] & 1) and bool(pl[2] & 1))
        cx, cy = struct.unpack_from("<hh", pl, 0x10)
        check(tag + "object coords = sb1 pos + 7", (cx, cy) == (px + 7, py + 7), "%s vs %s" % ((cx, cy), (px, py)))
        check(tag + "player sprite is a live gSprites entry", bool(E[0x20630 + pl[4] * 0x44 + 0x3E] & 1))
        mh = E[0x37318:0x37318 + 28]
        check(tag + "header layout/events pointers in ROM", all(u32(mh, o) >> 24 == 8 for o in (0, 4)))
        lay = u32(mh, 0)
        w, h = struct.unpack_from("<ii", rb(lay, 8))
        check(tag + "backup dims = layout dims + (15,14)",
              (u32(I, 0x5DC0), u32(I, 0x5DC4)) == (w + 15, h + 14), "%dx%d" % (w, h))
        wcur = E[0x38454 + 0x6D0]
        check(tag + "currWeather = header weather (settled)", wcur == mh[0x16] or mh[0x17] in (8, 9), "%d vs %d" % (wcur, mh[0x16]))
        check(tag + "palProcessingState idle", E[0x38454 + 0x6C6] == 3)
        for i in range(65):
            t = u32(E, 0x20630 + i * 0x44 + 0x14)
            if E[0x20630 + i * 0x44 + 0x3E] & 1 and not (t >> 24 in (3, 8)):
                check(tag + "sprite %d template pointer sane" % i, False, hex(t))
        gi = u32(rb(0x08505620 + 4 * pl[5], 4), 0)
        im = u32(rb(gi + 0x1C, 4), 0)
        imd, imsz = struct.unpack_from("<IH", rb(im, 6))
        check(tag + "gfx info images -> {ROM data, size}", imd >> 24 == 8 and imsz in (128, 256, 512, 1024), hex(imd) + "/" + str(imsz))
        ev = u32(mh, 4)
        evb = rb(ev, 20)
        check(tag + "events: object templates graphicsId @1",
              all(rb(u32(evb, 4) + k * 0x18, 2)[0] != 0 for k in range(evb[0])) if evb[0] else True)
    print("fails=%d" % fails)
    sys.exit(1 if fails else 0)


main()
