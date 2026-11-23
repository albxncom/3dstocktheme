#!/usr/bin/env python3
"""Proves that check_cgfx.py refuses spoiled texture blocks (package BN-H).

  python3 -I test_tex.py OUT_DIR [good.cgfx]     (default: the tracked app/meta/banner.cgfx, a file with one texture)

Each spoil changes one thing of the texture block in a copy of a good file, in memory, and the checker must refuse
it with the expected reason; the first three are also written into the new folder OUT_DIR and refused by the
checker run as a command. Shown as well, not refused by check_cgfx.py (what it cannot prove): a texel changed (the
checker knows the block's form, not its picture; build.py compares the bytes with the scene's own RGBA4 encoding),
and the mapper's filter and wrap words (pycgfx's own values; what the console makes of them is unknown).
Exit 0 only if the good file passes and every spoil is refused for its reason.
"""
import os
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.dont_write_bytecode = True
sys.path.insert(0, HERE)
import check_cgfx   # noqa: E402


def u32(d, at):
    return struct.unpack_from("<I", d, at)[0]


def put(d, at, value, fmt="<I"):
    d = bytearray(d)
    struct.pack_into(fmt, d, at, value)
    return bytes(d)


def places(data):
    """(texture at, image at, reference at, mapper at) of the file's one texture."""
    facts = check_cgfx.check(data)
    if len(facts["textures"]) != 1:
        raise SystemExit("test_tex: the good file must hold one texture")
    tex = facts["textures"][0]["offset"]
    image = tex + 56 + struct.unpack_from("<i", data, tex + 56)[0]
    refs = [i for i in range(0, len(data) - 32, 4) if u32(data, i) == 0x20000004 and data[i + 4:i + 8] == b"TXOB"]
    if len(refs) != 1:
        raise SystemExit("test_tex: %d texture references" % len(refs))
    mappers = [i for i in range(0, len(data) - 12, 4) if u32(data, i) == 0x80000000
               and i + 8 + struct.unpack_from("<i", data, i + 8)[0] == refs[0]]
    return tex, image, refs[0], mappers[0]


def main(argv):
    if len(argv) not in (2, 3):
        print(__doc__.split("\n\n")[1])
        return 2
    path = argv[2] if len(argv) > 2 else os.path.join(HERE, "..", "..", "app", "meta", "banner.cgfx")
    with open(path, "rb") as f:
        good = f.read()
    check_cgfx.check(good)
    tex, image, ref, mapper = places(good)
    data_ptr = image + 12
    name_ptr = ref + 24
    other_name = None
    # a string of the file that is not a texture's name: the material's
    mat_name = "matte" if good.find(b"matte\0") >= 0 else "unlit"     # T10, T11: matte; T13, T14: unlit
    at = good.find(mat_name.encode() + b"\0")
    other_name = at - name_ptr
    spoils = [
        ("data-moved", put(good, data_ptr, struct.unpack_from("<i", good, data_ptr)[0] + 16, "<i"), "not on a multiple of 128"),
        ("data-size", put(good, image + 8, u32(good, image + 8) - 128), "data bytes"),
        ("reference-name", put(good, name_ptr, other_name, "<i"), "which is not in the file"),
        ("reference-pointer", put(good, ref + 28, struct.unpack_from("<i", good, ref + 28)[0] + 4, "<i"), "pointer is not the texture"),
        ("format-rgba8", put(good, tex + 52, 0), "RGBA8"),
        ("format-etc1", put(good, tex + 52, 12), "never writes"),
        ("width-200", put(good, tex + 28, 200), "power of two"),
        ("width-2048", put(good, tex + 28, 2048), "power of two"),
        ("levels-2", put(good, tex + 40, 2), "levels"),
        ("image-height", put(good, image, 64), "its image is"),
        ("not-image-texture", put(good, tex, 0x20000009), "not an image texture"),
        ("sampler-owner", put(good, mapper + 12 + struct.unpack_from("<i", good, mapper + 12)[0] + 4,
                                struct.unpack_from("<i", good, mapper + 12 + struct.unpack_from("<i", good, mapper + 12)[0] + 4)[0] + 4, "<i"),
         "owner"),
        ("data-outside", put(good, data_ptr, len(good) - data_ptr - 64, "<i"), "outside"),
    ]
    bad = 0
    out = os.path.abspath(argv[1])
    os.makedirs(out)
    for i, (name, data, reason) in enumerate(spoils):
        try:
            check_cgfx.check(data)
            print("%-18s PASSED the checker (must be refused)" % name)
            bad += 1
            continue
        except check_cgfx.CheckError as e:
            text = str(e)
        ok = reason in text
        bad += not ok
        print("%-18s refused%s: %s" % (name, "" if ok else " FOR ANOTHER REASON (wanted %r)" % reason, text))
        if i < 3:
            spoiled = os.path.join(out, "spoiled-%s.cgfx" % name)
            with open(spoiled, "xb") as f:
                f.write(data)
            done = subprocess.run([sys.executable, "-I", "-B", os.path.join(HERE, "check_cgfx.py"), spoiled],
                                  stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            line = done.stdout.decode().splitlines()[0]
            print("  as a command: exit %d, %s" % (done.returncode, line))
            bad += done.returncode != 1
    # what the checker cannot see
    facts = check_cgfx.check(good)
    t = facts["textures"][0]
    texel = put(good, t["data_at"] + 1000, u32(good, t["data_at"] + 1000) ^ 0xFFFF)
    try:
        check_cgfx.check(texel)
        print("%-18s passes the checker, as expected: it proves the block's form, not its picture"
              " (build.py compares the bytes with the scene's own encoding)" % "texel-changed")
    except check_cgfx.CheckError as e:
        print("texel-changed refused (unexpected): %s" % e)
        bad += 1
    words = facts["model"]["materials"][0]["mappers"][0]["commands"]
    print("the mapper's command words as pycgfx writes them (not judged): %s" % " ".join("%08X" % w for w in words))
    print("test_tex: %s, files in %s" % ("all held" if not bad else "%d FAILED" % bad, out))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
