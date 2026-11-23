"""Generated test data for badge_sets.py, written from the format documentation only (no badge art, no console data).

- png(): PNG files in every colour type the reader takes, with all five row filters.
- badge_set(): a set folder of generated badge PNGs (patterns, not pictures), optionally with a 48x48 set icon.
- fake_save3ds(), fake_sdroot(): a stand-in for save3ds_fuse and an SD root it can read, for decrypt and encrypt.
- collection(): a BadgeData.dat / BadgeMngFile.dat pair laid out the way Anemone3DS writes one (set IDs 1.., badge
  ID = index + 1, quantity 0xFFFF, no shortcut, placed folder icons, a non-zero NNID field) with random bytes in the
  unused name and image areas, as Anemone3DS leaves them; slots() the eight saved layouts of MngFile.dat.

Run: python3 -I fixtures.py set DIR [--badges N] [--icon]   (writes N generated badge PNGs into DIR, default 2)
"""
import argparse
import json
import random
import struct
import sys
import zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))  # python -I does not add the script directory
import badge_sets as bs  # noqa: E402

NNID_VALUE = 0x5EED1234  # made up


def _chunk(kind, body):
    return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))


def png(width, height, rgba, color_type=6, palette=None, trns=None):
    """PNG bytes. rgba: width*height*4 bytes; colour types 0 and 4 take R as grey, 2 drops A, 3 maps to `palette`
    (a list of RGBA tuples, written as PLTE + tRNS). Rows cycle through filters 0 to 4."""
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[color_type]
    if color_type == 3 and not palette:
        raise ValueError("colour type 3 needs a palette")
    palette = palette or []
    lookup = {c: i for i, c in enumerate(palette)}
    rows = []
    for y in range(height):
        row = bytearray()
        for x in range(width):
            r, g, b, a = rgba[(y * width + x) * 4:(y * width + x) * 4 + 4]
            row += {0: bytes((r,)), 2: bytes((r, g, b)), 3: bytes((lookup.get((r, g, b, a), 0),)),
                    4: bytes((r, a)), 6: bytes((r, g, b, a))}[color_type]
        rows.append(bytes(row))
    raw, prev = bytearray(), bytes(width * channels)
    for y, row in enumerate(rows):
        kind = y % 5
        out = bytearray()
        for i, v in enumerate(row):
            a = row[i - channels] if i >= channels else 0
            b, c = prev[i], prev[i - channels] if i >= channels else 0
            if kind == 0:
                pred = 0
            elif kind == 1:
                pred = a
            elif kind == 2:
                pred = b
            elif kind == 3:
                pred = (a + b) >> 1
            else:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pred = a if pa <= pb and pa <= pc else b if pb <= pc else c
            out.append((v - pred) & 0xFF)
        raw += bytes((kind,)) + out
        prev = row
    body = _chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, color_type, 0, 0, 0))
    if color_type == 3:
        body += _chunk(b"PLTE", b"".join(bytes(c[:3]) for c in palette))
        body += _chunk(b"tRNS", bytes(c[3] for c in palette))
    if trns is not None:
        body += _chunk(b"tRNS", trns)
    body += _chunk(b"tEXt", b"Comment\0generated test pattern")
    return bs.PNG_SIGNATURE + body + _chunk(b"IDAT", zlib.compress(bytes(raw))) + _chunk(b"IEND", b"")


def pattern(width, height, seed):
    """RGBA bytes: gradients and a seed-dependent stripe, with a transparent corner and half-transparent pixels."""
    out = bytearray()
    for y in range(height):
        for x in range(width):
            r = (x * 4 + seed * 37) & 0xFF
            g = (y * 4 + seed * 91) & 0xFF
            b = ((x ^ y) * 8 + seed * 13) & 0xFF
            a = 0 if x < 6 and y < 6 else 0x80 if (x + y + seed) % 11 == 0 else 0xFF
            out += bytes((r, g, b, a))
    return bytes(out)


def badge_set(folder, count=2, icon=False, sizes=None, prefix="Badge"):
    """count PNGs named <prefix> 01.png ... (64x64, or the (w, h) sizes given) and optionally _seticon.png."""
    folder = Path(folder)
    folder.mkdir(parents=True, exist_ok=True)
    for n in range(count):
        w, h = (sizes or [(64, 64)] * count)[n]
        (folder / f"{prefix} {n + 1:02d}.png").write_bytes(png(w, h, pattern(w, h, n + 1)))
    if icon:
        (folder / "_seticon.png").write_bytes(png(48, 48, pattern(48, 48, 99), color_type=2))
    return folder


def collection(sets=(("Alpha", 5), ("Beta", 3), ("Gamma", 4)), placed=((1, 0, 0xD), (1, 2, 0xC)), seed=1,
               set_bits=False):
    """(BadgeData.dat, BadgeMngFile.dat). placed: (set slot, badge within the set, HOME position) folder icons."""
    rnd = random.Random(seed)
    data = bytearray(rnd.randbytes(bs.DATA_SIZE))
    mng = bytearray(bs.MNG_SIZE)
    index = 0
    for s, (name, count) in enumerate(sets):
        data[bs.SET_TITLES + s * bs.TITLE_BLOCK:bs.SET_TITLES + (s + 1) * bs.TITLE_BLOCK] = bs.title_block(name)
        data[bs.SET_ICONS + s * bs.SET_ICON_SIZE:bs.SET_ICONS + (s + 1) * bs.SET_ICON_SIZE] = \
            bytes([0x40 + s]) * bs.SET_ICON_SIZE
        struct.pack_into("<8sIIIIIIIIII", mng, bs.SET_INFO + s * bs.SET_INFO_SIZE, b"\xff" * 8, 0, 0x2710, s + 1, s,
                         bs.NONE, count, count * bs.QUANTITY, index, 0, 0)
        if set_bits:
            mng[bs.USED_SETS + s // 8] |= 1 << (s % 8)
        for k in range(count):
            struct.pack_into("<IIIHHHHI16s", mng, bs.BADGE_INFO + index * bs.BADGE_INFO_SIZE, 0, index + 1, s + 1,
                             index, 0, 0, bs.QUANTITY, 0, bs.NO_SHORTCUT)
            mng[bs.USED_BADGES + index // 8] |= 1 << (index % 8)
            off = bs.BADGE_TITLES + index * bs.TITLE_BLOCK
            data[off:off + bs.TITLE_BLOCK] = bs.title_block(f"{name} {k + 1}")
            big, small = bs.badge_images(pattern(64, 64, index + 7), 64, 0, 0)
            data[bs.BADGE_ICONS + index * bs.BADGE_ICON_SIZE:bs.BADGE_ICONS + (index + 1) * bs.BADGE_ICON_SIZE] = big
            data[bs.SMALL_ICONS + index * bs.SMALL_ICON_SIZE:bs.SMALL_ICONS + (index + 1) * bs.SMALL_ICON_SIZE] = \
                small
            index += 1
    for s in range(len(sets), bs.MAX_SETS):
        struct.pack_into("<8sIIIIIIIIII", mng, bs.SET_INFO + s * bs.SET_INFO_SIZE, b"\xff" * 8, 0, 0x2710, bs.NONE,
                         bs.NONE, bs.NONE, 0, 0, bs.NONE, 0, 0)
    struct.pack_into("<IIIIIIII", mng, 0, 0, len(sets), index, 0, bs.NONE, 0, index * bs.QUANTITY, NNID_VALUE)
    for n in range(bs.MAX_PLACED):
        off = bs.LAYOUT + n * bs.LAYOUT_RECORD
        mng[off:off + bs.LAYOUT_RECORD] = bs.EMPTY_RECORD
    starts = [sum(c for _, c in sets[:s]) for s in range(len(sets))]
    for n, (s, k, pos) in enumerate(placed):
        place(mng, n, starts[s] + k, pos, bs.FOLDER_ICON)
    return bytes(data), bytes(mng)


def place(mng, record, badge, pos, folder=bs.NONE):
    """Put badge (index) into layout record `record` and update the placed counts, as HOME does."""
    off = bs.LAYOUT + record * bs.LAYOUT_RECORD
    info = bs.BADGE_INFO + badge * bs.BADGE_INFO_SIZE
    mng[off:off + 16] = mng[info:info + 16]
    struct.pack_into("<II", mng, off + 16, pos, folder)
    struct.pack_into("<I", mng, bs.PLACED, struct.unpack_from("<I", mng, bs.PLACED)[0] + 1)
    struct.pack_into("<H", mng, info + 0x10, struct.unpack_from("<H", mng, info + 0x10)[0] + 1)


def unplace(mng, record):
    off = bs.LAYOUT + record * bs.LAYOUT_RECORD
    info = next(bs.BADGE_INFO + n * bs.BADGE_INFO_SIZE for n in range(bs.MAX_BADGES)
                if mng[bs.BADGE_INFO + n * bs.BADGE_INFO_SIZE:bs.BADGE_INFO + n * bs.BADGE_INFO_SIZE + 16]
                == mng[off:off + 16])
    mng[off:off + bs.LAYOUT_RECORD] = bs.EMPTY_RECORD
    struct.pack_into("<I", mng, bs.PLACED, struct.unpack_from("<I", mng, bs.PLACED)[0] - 1)
    struct.pack_into("<H", mng, info + 0x10, struct.unpack_from("<H", mng, info + 0x10)[0] - 1)


def slots(copies, saved=None):
    """MngFile.dat bytes: copies is a list of up to eight BadgeMngFile.dat copies (None = an empty copy); saved says
    which slots are marked as saved (default: those with a copy)."""
    out = bytearray(bs.SLOTS_SIZE)
    empty = bytearray(bs.MNG_SIZE)
    for r in range(bs.MAX_PLACED):
        empty[bs.LAYOUT + r * bs.LAYOUT_RECORD:bs.LAYOUT + (r + 1) * bs.LAYOUT_RECORD] = bs.EMPTY_RECORD
    for n, copy in enumerate(copies):
        off = bs.SLOT_BADGES + n * bs.MNG_SIZE
        out[off:off + bs.MNG_SIZE] = empty if copy is None else copy
        out[bs.SLOT_FLAGS + n] = (copy is not None) if saved is None else saved[n]
    return bytes(out)


def tree(folder, data, mng):
    """A save3ds extract tree of extdata 000014d1: icon, boss/, user/BadgeData.dat, user/BadgeMngFile.dat."""
    folder = Path(folder)
    (folder / "user").mkdir(parents=True, exist_ok=True)
    (folder / "boss").mkdir(exist_ok=True)
    (folder / "icon").write_bytes(bytes(4))
    (folder / "user" / "BadgeData.dat").write_bytes(data)
    (folder / "user" / "BadgeMngFile.dat").write_bytes(mng)
    return folder


def slots_tree(folder, mngfile):
    folder = Path(folder)
    (folder / "user").mkdir(parents=True, exist_ok=True)
    (folder / "boss").mkdir(exist_ok=True)
    (folder / "icon").write_bytes(bytes(4))
    (folder / "user" / "MngFile.dat").write_bytes(mngfile)
    return folder


FAKE_SAVE3DS = """#!{python} -I
# Stands in for save3ds_fuse in the tests: the same command line, extdata kept as numbered files XORed with the first
# byte of the movable file. Like save3ds, --import first deletes every file, then recreates them with new numbers.
# FAKE_SAVE3DS=fail-import exits 1 after the delete, FAKE_SAVE3DS=bad-import changes one byte of what it imports.
import json, os, sys
from pathlib import Path
opt = dict(zip(sys.argv[1::2], sys.argv[2::2]))
key = Path(opt["--movable"]).read_bytes()[0]
folder = next(Path(opt["--sd"]).glob("Nintendo 3DS/*/*/extdata/00000000/" + opt["--sdext"][-8:])) / "00000000"
crypt = lambda b: bytes(x ^ key for x in b)
meta = json.loads(crypt((folder / "00000001").read_bytes()))
if "--extract" in opt:
    out = Path(opt["--extract"])
    out.mkdir(parents=True)
    for d in meta["dirs"]:
        (out / d).mkdir(parents=True, exist_ok=True)
    for path, n in meta["files"].items():
        (out / path).parent.mkdir(parents=True, exist_ok=True)
        (out / path).write_bytes(crypt((folder / ("%08x" % n)).read_bytes())[1:])
else:
    for n in meta["files"].values():
        (folder / ("%08x" % n)).unlink()
    if os.environ.get("FAKE_SAVE3DS") == "fail-import":
        sys.exit(1)
    src, files, dirs, gen = Path(opt["--import"]), {}, [], meta["gen"] + 1
    for p in sorted(src.rglob("*"), reverse=True):
        rel = p.relative_to(src).as_posix()
        if p.is_dir():
            dirs.append(rel)
            continue
        files[rel] = len(files) + 2
        body = bytearray(p.read_bytes())
        if os.environ.get("FAKE_SAVE3DS") == "bad-import" and rel == "user/BadgeData.dat":
            body[0] ^= 1
        (folder / ("%08x" % files[rel])).write_bytes(crypt(bytes([gen % 256]) + body))
    (folder / "00000001").write_bytes(crypt(json.dumps({"files": files, "dirs": dirs, "gen": gen}).encode()))
print("Finished")
"""


def fake_save3ds(path):
    path = Path(path)
    path.write_text(FAKE_SAVE3DS.replace("{python}", sys.executable), encoding="utf-8")
    path.chmod(0o755)
    return path


def fake_sdroot(root, trees, key=0x5A):
    """An SD root whose extdata folders hold `trees` ({extdata: tree folder}) in the fake save3ds format; returns
    (root, movable file). The id0 and id1 folder names are made up."""
    root = Path(root)
    for ext, src in trees.items():
        folder = root / "Nintendo 3DS" / ("0" * 31 + "a") / ("0" * 31 + "b") / "extdata" / "00000000" / ext / "00000000"
        folder.mkdir(parents=True)
        files, dirs = {}, []
        for p in sorted(Path(src).rglob("*")):
            rel = p.relative_to(src).as_posix()
            if p.is_dir():
                dirs.append(rel)
                continue
            files[rel] = len(files) + 2
            (folder / f"{files[rel]:08x}").write_bytes(bytes(x ^ key for x in b"\0" + p.read_bytes()))
        meta = json.dumps({"files": files, "dirs": dirs, "gen": 0}).encode()
        (folder / "00000001").write_bytes(bytes(x ^ key for x in meta))
    movable = root.parent / "movable.sed"
    movable.write_bytes(bytes([key]) + bytes(0x13F))
    (root.parent / "boot9.bin").write_bytes(bytes(0x10000))
    return root, movable


def main(argv=None):
    parser = argparse.ArgumentParser(description="Write a set folder of generated badge PNGs.")
    sub = parser.add_subparsers(dest="command", required=True)
    p = sub.add_parser("set")
    p.add_argument("dir", metavar="DIR")
    p.add_argument("--badges", type=int, default=2)
    p.add_argument("--icon", action="store_true", help="also write a generated _seticon.png")
    args = parser.parse_args(argv)
    if Path(args.dir).exists():
        print(f"error: {args.dir} exists", file=sys.stderr)
        return 1
    badge_set(args.dir, args.badges, args.icon, prefix="Test")
    print(f"wrote {args.badges} badges{' and _seticon.png' if args.icon else ''} to {args.dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
