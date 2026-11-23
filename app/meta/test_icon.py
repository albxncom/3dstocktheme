#!/usr/bin/env python3
"""Checks the HOME Menu's icon that app/meta/icon.py draws: both pictures are the approved ones (C195). No console.

  python3 -I app/meta/test_icon.py            the pictures (make test)
  python3 -I app/meta/test_icon.py --print    the pins of the pictures icon.py draws now

A pin is a picture's size and the sha256 of its pixels: RGB, one byte a channel, row by row from the top left, as
icon.png and icon24.png hold them before compression. A change to the drawing fails here until its new pictures are
approved: draw them (python3 -I app/meta/icon.py OUT_DIR), look at them, and once they are approved put what --print
gives into APPROVED, in the same commit as the change.
Spoils: each picture with one channel of one pixel changed must fail.
Exit 0 when everything holds, else 1 with the first fault named.
"""
import hashlib
import os
import sys

META = os.path.dirname(os.path.abspath(__file__))
sys.dont_write_bytecode = True
sys.path.insert(0, META)
import icon   # noqa: E402

APPROVED = {
    48: "f733eceab914c960084b1fea1d738ef6b80c49506d2e965da70f4d1dfe051ac1",
    24: "bdaf554be990098ba0b82324d03f8077505f80c3a8d428ac9c79f81a7e375643",
}


class Fault(Exception):
    pass


def pixels(size):
    img = icon.icon(size)
    if (img.w, img.h) != (size, size):
        raise Fault("the %d px icon is %dx%d" % (size, img.w, img.h))
    return bytes(round(img.p[4 * k + c]) for k in range(size * size) for c in range(3))


def check(size, rgb):
    digest = hashlib.sha256(rgb).hexdigest()
    if len(rgb) != size * size * 3 or digest != APPROVED[size]:
        raise Fault("the %d px icon's pixels have sha256 %s, not the approved %s" % (size, digest, APPROVED[size]))


def must_fail(what, fn, *args):
    try:
        fn(*args)
    except Fault as e:
        print("ok   spoil refused, %s: %s" % (what, e))
        return
    raise Fault("spoil accepted, %s" % what)


def main(argv):
    if argv[1:] == ["--print"]:
        for size in APPROVED:
            print("    %d: \"%s\"," % (size, hashlib.sha256(pixels(size)).hexdigest()))
        return 0
    if argv[1:]:
        print(__doc__.split("\n\n")[1])
        return 2
    try:
        for size in APPROVED:
            rgb = pixels(size)
            check(size, rgb)
            print("ok   the %d px icon is the approved one, sha256 %s" % (size, APPROVED[size]))
            at = 3 * (size * size // 2 + size // 2)
            spoiled = bytearray(rgb)
            spoiled[at] ^= 0x08
            must_fail("the %d px icon with byte 0x%x changed" % (size, at), check, size, bytes(spoiled))
    except Fault as e:
        print("FAIL %s" % e)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
