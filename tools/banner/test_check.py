#!/usr/bin/env python3
"""Proves that check_cgfx.py refuses spoiled files.

  python3 -I test_check.py OUT_DIR [good.cgfx]     (default: the tracked app/meta/banner.cgfx)

From a file the checker passes it makes five spoiled copies, each with one thing
wrong, writes them into the new folder OUT_DIR and runs the checker on each
as a command; each must exit 1 with the expected reason:
  spoiled-tree.cgfx    one link of the bones' lookup tree bent
  spoiled-mesh.cgfx    one mesh's node name pointed at an empty string
  spoiled-size.cgfx    padded past 0x80000 bytes (sizes in the header kept right)
  spoiled-member.cgfx  a skeletal group member's parent name (+40) aimed past the end
  spoiled-shader.cgfx  the shader's user data: one entry, its pointer past the end
Beyond these: every dictionary of the file is bent in turn (in memory) and each
must be refused, and so must each of a group member's three name pointers aimed
outside the file; the good file must pass, and so must a copy padded to exactly
0x80000 bytes. Exit 0 only if all of that holds.
"""
import os
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.dont_write_bytecode = True
sys.path.insert(0, HERE)
import check_cgfx   # noqa: E402

LIMIT = 0x80000


def s32(data, off):
    return struct.unpack_from("<i", data, off)[0]


def u32(data, off):
    return struct.unpack_from("<I", data, off)[0]


def follow(data, off):
    return off + s32(data, off)


def cstring(data, off):
    return bytes(data[off:data.index(b"\0", off)]).decode()


def dictionaries(data):
    """[(offset, [names])] of every DICT in the file, found by its magic and size."""
    out, at = [], data.find(b"DICT")
    while at >= 0:
        count = u32(data, at + 8)
        if u32(data, at + 4) == 12 + 16 * (count + 1) and 0 < count < 1000:
            names = [cstring(data, follow(data, at + 12 + 16 * i + 8)) for i in range(1, count + 1)]
            out.append((at, names))
        at = data.find(b"DICT", at + 4)
    return out


def bend(data, at, count):
    """The root's left link (the first step of every lookup) set to another node."""
    spoiled = bytearray(data)
    left = struct.unpack_from("<H", data, at + 12 + 4)[0]
    struct.pack_into("<H", spoiled, at + 12 + 4, left % count + 1 if count > 1 else 0)
    return bytes(spoiled)


def first_model(data):
    models = follow(data, 0x1C + 4)                 # DATA's first dictionary
    return follow(data, models + 12 + 16 + 12)      # its first entry's data


def bones_dictionary(data):
    skeleton = follow(data, first_model(data) + 224)
    return follow(data, skeleton + 28)


def empty_mesh_node_name(data):
    """The first mesh's node name pointer moved to its string's end: an empty string."""
    table = follow(data, first_model(data) + 184)   # the model's mesh list
    mesh = follow(data, table)
    name_at = follow(data, mesh + 112)
    length = len(cstring(data, name_at))
    if not length:
        raise SystemExit("test_check: the good file's mesh node name is already empty")
    spoiled = bytearray(data)
    struct.pack_into("<i", spoiled, mesh + 112, s32(data, mesh + 112) + length)
    return bytes(spoiled)


def first_entry(data, dict_info):
    """The data of the first entry of the DICT a count-and-pointer pair names."""
    return follow(data, follow(data, dict_info + 4) + 12 + 16 + 12)


def skeletal_member(data):
    """The first member of the model's animation group "SkeletalAnimation"."""
    groups = follow(data, first_model(data) + 44)
    for i in range(1, u32(data, groups + 8) + 1):
        node = groups + 12 + 16 * i
        if cstring(data, follow(data, node + 8)) == "SkeletalAnimation":
            return first_entry(data, follow(data, node + 12) + 16)
    raise SystemExit("test_check: the good file has no skeletal animation group")


def pointed_outside(data, at):
    """The pointer at `at` aimed past the file's end."""
    spoiled = bytearray(data)
    struct.pack_into("<i", spoiled, at, len(data) + 0x1000 - at)
    return bytes(spoiled)


def shader_user_data_outside(data):
    """The first material's shader: its user data dictionary given one entry past the file's end."""
    shader = follow(data, first_entry(data, first_model(data) + 188) + 644)
    spoiled = bytearray(pointed_outside(data, shader + 20))
    struct.pack_into("<I", spoiled, shader + 16, 1)
    return bytes(spoiled)


def padded(data, size):
    """The file grown to `size` bytes with zeros in IMAG, both size fields kept right."""
    if u32(data, 16) != 2:
        raise SystemExit("test_check: the good file has no IMAG block to pad")
    imag = 0x14 + u32(data, 0x18)
    grown = bytearray(data) + bytes(size - len(data))
    struct.pack_into("<I", grown, 12, size)
    struct.pack_into("<I", grown, imag + 4, size - imag - 8)
    return bytes(grown)


def refused(data):
    try:
        check_cgfx.check(data)
    except check_cgfx.CheckError as e:
        return str(e)
    return None


def run_checker(path):
    done = subprocess.run([sys.executable, "-I", os.path.join(HERE, "check_cgfx.py"), path],
                          stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                          timeout=60)
    return done.returncode, done.stdout.decode("utf-8", "replace").strip().splitlines()[0]


def main(argv):
    if len(argv) not in (2, 3):
        print(__doc__.split("\n\n")[1])
        return 2
    good_path = argv[2] if len(argv) > 2 else os.path.join(HERE, "..", "..", "app", "meta", "banner.cgfx")
    with open(good_path, "rb") as f:
        good = f.read()
    failures = 0

    def expect(ok, text):
        nonlocal failures
        failures += not ok
        print(("ok    " if ok else "FAIL  ") + text)

    folder = os.path.abspath(argv[1])
    os.makedirs(folder)

    code, line = run_checker(good_path)
    expect(code == 0, "the good file passes (exit %d): %s" % (code, os.path.basename(good_path)))

    dicts = dictionaries(good)
    bones = next(d for d in dicts if d[0] == bones_dictionary(good))
    cases = (
        ("spoiled-tree.cgfx", bend(good, bones[0], len(bones[1])), "lookup of"),
        ("spoiled-mesh.cgfx", empty_mesh_node_name(good), "mesh node name is empty"),
        ("spoiled-size.cgfx", padded(good, LIMIT + 16), "over the limit"),
        ("spoiled-member.cgfx", pointed_outside(good, skeletal_member(good) + 40), "parent name"),
        ("spoiled-shader.cgfx", shader_user_data_outside(good), "shader user data"),
    )
    for name, data, reason in cases:
        path = os.path.join(folder, name)
        with open(path, "wb") as f:
            f.write(data)
        code, line = run_checker(path)
        shown = line.replace(folder + os.sep, "")
        expect(code == 1 and reason in line, "%s refused (exit %d): %s" % (name, code, shown[:150]))

    bent = [refused(bend(good, at, len(names))) for at, names in dicts]
    expect(all(b and "lookup of" in b for b in bent),
           "each of the file's %d dictionaries, bent in turn, is refused (%d refused)"
           % (len(dicts), sum(1 for b in bent if b)))
    member = skeletal_member(good)
    names = [refused(pointed_outside(good, member + off)) for off in (8, 12, 40)]
    expect(all(n and "outside the file" in n for n in names),
           "each of a group member's three name pointers (+8, +12, +40), aimed outside, is refused")
    expect(len(dicts) == check_cgfx.check(good)["dictionaries"],
           "the checker walked as many dictionaries as the file holds (%d)" % len(dicts))
    expect(refused(padded(good, LIMIT)) is None, "a copy padded to exactly 0x80000 bytes passes")
    expect(refused(good[:-16]) is not None, "a copy cut short by 16 bytes is refused")

    print("test_check: %s, files in %s" % ("all held" if not failures else "%d FAILED" % failures, folder))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
