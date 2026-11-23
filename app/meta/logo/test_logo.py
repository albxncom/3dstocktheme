#!/usr/bin/env python3
"""Checks the CIA's start logo, the finished signed file app/meta/logo.bin (app/meta/logo.txt). No key, no console.

  python3 -I app/meta/logo/test_logo.py                                 the file (make test)
  python3 -I app/meta/logo/test_logo.py --cia CIA --ctrtool CTRTOOL      also a built CIA (make cia)

The file: 0x2000 bytes with the size and sha256 logo.txt states, accepted by checklogo.py, and the DARC inside it
equal to makelogo.py's "word" (the repository's sources are this logo's; a change to its drawing needs a new
signed file). A CIA: ctrtool reads it; its NCCH logo region is empty, its ExeFS holds .code, banner, icon and logo,
and the ExeFS logo (size and hash as ctrtool prints them, and the extracted bytes) equals logo.bin byte for byte.
Spoils: copies with one byte changed (in the file at several places; in the CIA inside its logo) must fail.
Exit 0 when everything holds, else 1 with the first fault named.
"""
import hashlib
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
META = os.path.dirname(HERE)
sys.dont_write_bytecode = True
sys.path.insert(0, HERE)
import checklogo   # noqa: E402
import makelogo   # noqa: E402

LOGO = os.path.join(META, "logo.bin")
SIZE = 0x2000
SECTIONS = [".code", "banner", "icon", "logo"]


class Fault(Exception):
    pass


def stated():
    with open(os.path.join(META, "logo.txt")) as f:
        text = f.read()
    size = re.search(r"(?m)^size\s+(\d+)\s*$", text)
    digest = re.search(r"(?m)^sha256\s+([0-9a-f]{64})\s*$", text)
    if not size or not digest:
        raise Fault("logo.txt states no size or sha256 line")
    return int(size.group(1)), digest.group(1)


def check_file(raw):
    size, digest = stated()
    if len(raw) != size or size != SIZE:
        raise Fault("the file is %d bytes; logo.txt says %d, the logo must be %d" % (len(raw), size, SIZE))
    if hashlib.sha256(raw).hexdigest() != digest:
        raise Fault("sha256 %s, not the %s logo.txt states" % (hashlib.sha256(raw).hexdigest(), digest))
    try:
        report = checklogo.check(raw)
    except (checklogo.Refused, ValueError, IndexError) as e:
        raise Fault("checklogo refuses it: %s" % e)
    darc = checklogo.unlz11(raw)[0][:report["darc"]]
    if darc != makelogo.build("word"):
        raise Fault("its DARC is not makelogo.py's \"word\": the sources changed, the signed file did not")
    return report


def field(info, name):
    found = re.findall(r"(?m)^%s:\s*(\S+)\s*$" % re.escape(name), info)
    if len(found) != 1:
        raise Fault("ctrtool prints %d lines \"%s\", not one" % (len(found), name))
    return found[0]


def check_cia(cia, ctrtool, logo):
    with tempfile.TemporaryDirectory() as tmp:
        exefs = os.path.join(tmp, "exefs")
        run = subprocess.run([ctrtool, "--exefsdir=" + exefs, cia], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        info = run.stdout.decode("utf-8", "replace")
        if run.returncode != 0:
            raise Fault("ctrtool exit %d: %s" % (run.returncode, info.strip().splitlines()[-1:]))
        if int(field(info, "Logo size"), 16) != 0:
            raise Fault("the NCCH logo region is not empty (the logo belongs in the ExeFS, -exefslogo)")
        names = re.findall(r"(?m)^Section name:\s*(\S+)\s*$", info)
        if names != SECTIONS:
            raise Fault("ExeFS sections %s, not %s" % (names, SECTIONS))
        sizes = re.findall(r"(?m)^Section size:\s*(\S+)\s*$", info)
        hashes = re.findall(r"(?m)^Section hash:\s*(\S+)\s*$", info)
        if len(sizes) != len(names) or len(hashes) != len(names):
            raise Fault("ctrtool's section list is not one size and one hash per section")
        if int(sizes[names.index("logo")], 16) != len(logo):
            raise Fault("the ExeFS logo is %s bytes, not 0x%x" % (sizes[names.index("logo")], len(logo)))
        if hashes[names.index("logo")].lower() != hashlib.sha256(logo).hexdigest():
            raise Fault("the ExeFS logo's hash is %s, not logo.bin's" % hashes[names.index("logo")])
        try:
            with open(os.path.join(exefs, "logo.bin"), "rb") as f:
                inside = f.read()
        except OSError:
            raise Fault("ctrtool wrote no ExeFS logo")
        if inside != logo:
            raise Fault("the ExeFS logo ctrtool wrote is not logo.bin byte for byte")
        return names, sizes, hashes


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


def spoil_cia(cia, ctrtool, logo):
    with open(cia, "rb") as f:
        data = f.read()
    at = data.find(logo)
    if at < 0 or data.find(logo, at + 1) >= 0:
        raise Fault("logo.bin is not found exactly once in the CIA's bytes (an encrypted ExeFS?)")
    with tempfile.TemporaryDirectory() as tmp:
        copy = os.path.join(tmp, "spoiled.cia")
        with open(copy, "wb") as f:
            f.write(spoiled(data, at + 0x100))
        must_fail("the CIA with byte 0x%x changed (in its logo)" % (at + 0x100), check_cia, copy, ctrtool, logo)
    must_fail("the CIA against logo.bin with one byte changed", check_cia, cia, ctrtool, spoiled(logo, 0x40))


def main(argv):
    args = argv[1:]
    cia = ctrtool = None
    if args[:1] == ["--cia"] and len(args) == 4 and args[2] == "--ctrtool":
        cia, ctrtool = args[1], args[3]
    elif args:
        print(__doc__.split("\n\n")[1])
        return 2
    try:
        with open(LOGO, "rb") as f:
            raw = f.read()
        report = check_file(raw)
        print("ok   app/meta/logo.bin: 0x%x bytes, sha256 %s, compressed 0x%x, DARC 0x%x + MAC, equal to "
              "makelogo.py \"word\"" % (len(raw), hashlib.sha256(raw).hexdigest(), report["compressed"],
                                        report["darc"]))
        used = report["compressed"]
        for at in (0, 4, used // 2, used - 1, used, SIZE - 1):
            must_fail("logo.bin with byte 0x%x changed" % at, check_file, spoiled(raw, at))
        if cia:
            if not (ctrtool and os.access(ctrtool, os.X_OK)):
                raise Fault("ctrtool not found at %r" % ctrtool)
            names, sizes, hashes = check_cia(cia, ctrtool, raw)
            print("ok   %s: NCCH logo region empty; ExeFS %s; logo %s bytes, hash %s = logo.bin byte for byte"
                  % (os.path.basename(cia), ", ".join(names), sizes[names.index("logo")],
                     hashes[names.index("logo")].lower()))
            spoil_cia(cia, ctrtool, raw)
    except Fault as e:
        print("FAIL %s" % e)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
