#!/usr/bin/env python3
"""Checks the CIA's banner model, the finished file app/meta/banner.cgfx (app/meta/banner.txt). No console.

  python3 -I app/meta/test_banner.py                                 the file (make test)
  python3 -I app/meta/test_banner.py --cia CIA --ctrtool CTRTOOL      also a built CIA (make cia)

The file: a CGFX with the size and sha256 banner.txt states. A CIA: ctrtool reads it, its ExeFS holds a banner,
and that banner (bannertool's CBMD: the common model LZ11-packed at the offset in 0x8, no region's own model, the
chime as a CWAV at the offset in 0x84) unpacks to banner.cgfx byte for byte.
Spoils: copies with one byte changed (in the file at several places; in the CIA inside its banner) must fail.
Exit 0 when everything holds, else 1 with the first fault named.
"""
import hashlib
import os
import re
import struct
import subprocess
import sys
import tempfile

META = os.path.dirname(os.path.abspath(__file__))
sys.dont_write_bytecode = True
sys.path.insert(0, os.path.join(META, "logo"))
import checklogo   # noqa: E402  (its LZ11 reader)

CGFX = os.path.join(META, "banner.cgfx")


class Fault(Exception):
    pass


def stated():
    with open(os.path.join(META, "banner.txt")) as f:
        text = f.read()
    size = re.search(r"(?m)^size\s+(\d+)\s*$", text)
    digest = re.search(r"(?m)^sha256\s+([0-9a-f]{64})\s*$", text)
    if not size or not digest:
        raise Fault("banner.txt states no size or sha256 line")
    return int(size.group(1)), digest.group(1)


def check_file(raw):
    size, digest = stated()
    if len(raw) != size:
        raise Fault("the file is %d bytes; banner.txt says %d" % (len(raw), size))
    if hashlib.sha256(raw).hexdigest() != digest:
        raise Fault("sha256 %s, not the %s banner.txt states" % (hashlib.sha256(raw).hexdigest(), digest))
    if raw[:4] != b"CGFX":
        raise Fault("not a CGFX (magic %r)" % raw[:4])


def unpacked(bnr):
    """The common model inside a CBMD banner."""
    if len(bnr) < 0x88 or bnr[:4] != b"CBMD":
        raise Fault("the banner is not a CBMD (magic %r, %d bytes)" % (bnr[:4], len(bnr)))
    common, sound = struct.unpack_from("<I", bnr, 0x8)[0], struct.unpack_from("<I", bnr, 0x84)[0]
    if any(struct.unpack_from("<13I", bnr, 0xC)):
        raise Fault("the banner has a region's own model")
    if not 0x88 <= common < sound <= len(bnr) - 4 or bnr[sound:sound + 4] != b"CWAV":
        raise Fault("the banner's model at 0x%x and sound at 0x%x do not fit (no CWAV there?)" % (common, sound))
    try:
        return checklogo.unlz11(bnr[common:sound])[0]
    except (checklogo.Refused, IndexError) as e:
        raise Fault("the banner's model does not unpack: %s" % e)


def check_cia(cia, ctrtool, cgfx):
    with tempfile.TemporaryDirectory() as tmp:
        exefs = os.path.join(tmp, "exefs")
        run = subprocess.run([ctrtool, "--exefsdir=" + exefs, cia], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        info = run.stdout.decode("utf-8", "replace")
        if run.returncode != 0:
            raise Fault("ctrtool exit %d: %s" % (run.returncode, info.strip().splitlines()[-1:]))
        names = re.findall(r"(?m)^Section name:\s*(\S+)\s*$", info)
        if names.count("banner") != 1:
            raise Fault("ExeFS sections %s: not one banner" % names)
        try:
            with open(os.path.join(exefs, "banner.bin"), "rb") as f:
                bnr = f.read()
        except OSError:
            raise Fault("ctrtool wrote no ExeFS banner")
        model = unpacked(bnr)
        if model != cgfx:
            where = next((i for i, (a, b) in enumerate(zip(model, cgfx)) if a != b), min(len(model), len(cgfx)))
            raise Fault("the CIA's banner model (%d bytes) differs from banner.cgfx (%d bytes) from byte 0x%x"
                        % (len(model), len(cgfx), where))
        return bnr


def spoiled(raw, at):
    b = bytearray(raw)
    b[at] ^= 0x01
    return bytes(b)


def must_fail(what, fn, *args):
    try:
        fn(*args)
    except Fault as e:
        print("ok   spoil refused, %s: %s" % (what, e))
        return
    raise Fault("spoil accepted, %s" % what)


def spoil_cia(cia, ctrtool, cgfx, bnr):
    with open(cia, "rb") as f:
        data = f.read()
    at = data.find(bnr)
    if at < 0 or data.find(bnr, at + 1) >= 0:
        raise Fault("the banner is not found exactly once in the CIA's bytes (an encrypted ExeFS?)")
    at += struct.unpack_from("<I", bnr, 0x8)[0] + 0x1000
    with tempfile.TemporaryDirectory() as tmp:
        copy = os.path.join(tmp, "spoiled.cia")
        with open(copy, "wb") as f:
            f.write(spoiled(data, at))
        must_fail("the CIA with byte 0x%x changed (in its banner's model)" % at, check_cia, copy, ctrtool, cgfx)
    must_fail("the CIA against banner.cgfx with one byte changed", check_cia, cia, ctrtool, spoiled(cgfx, 0x1000))


def main(argv):
    args = argv[1:]
    cia = ctrtool = None
    if args[:1] == ["--cia"] and len(args) == 4 and args[2] == "--ctrtool":
        cia, ctrtool = args[1], args[3]
    elif args:
        print(__doc__.split("\n\n")[1])
        return 2
    try:
        with open(CGFX, "rb") as f:
            raw = f.read()
        check_file(raw)
        print("ok   app/meta/banner.cgfx: %d bytes, sha256 %s" % (len(raw), hashlib.sha256(raw).hexdigest()))
        for at in (0, len(raw) // 2, len(raw) - 1):
            must_fail("banner.cgfx with byte 0x%x changed" % at, check_file, spoiled(raw, at))
        if cia:
            if not (ctrtool and os.access(ctrtool, os.X_OK)):
                raise Fault("ctrtool not found at %r" % ctrtool)
            bnr = check_cia(cia, ctrtool, raw)
            print("ok   %s: ExeFS banner %d bytes, its model unpacks to banner.cgfx byte for byte"
                  % (os.path.basename(cia), len(bnr)))
            spoil_cia(cia, ctrtool, raw, bnr)
    except Fault as e:
        print("FAIL %s" % e)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
