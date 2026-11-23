"""Add a badge set to the HOME Menu's badge collection without rebuilding it, remove the last set again, and check
that every badge placement still points at a badge. Works on decrypted copies of the badge extdata only.

list         sets, counts and limits of a copy of extdata 000014d1; the NNID field only as set or not set
append       one set from a folder of PNG files: its badges take the indices after the last used one, set and badge IDs
             come from <data>/manifest.json and never change; writes a new extdata tree next to the input, after
             checking that every byte that exists stays as it is
remove-last  removes the last set, only if this tool appended it, and gives the two files back their exact earlier
             bytes (from the undo record append wrote to <data>/undo/, one per input); files changed since the append
             (by HOME) only with --allow-home-changes
check-slots  every placement in the live layout and in the eight saved layout slots (extdata 0000165c, MngFile.dat)
             points at a badge of the live collection (test B2)
decrypt      a copy of the encrypted extdata 000014d1 (and 0000165c, if there) from an SD root such as a backup, and
             its save3ds extract; the source is only read
encrypt      a TREE into a new copy of the encrypted extdata 000014d1 that a decrypt read, through save3ds --import,
             verified by extracting it again; the result is the folder to copy onto the card

A TREE is what save3ds --extract writes for one extdata: icon, boss/, user/. An SD root holds
Nintendo 3DS/<id0>/<id1>/extdata/00000000/<extdata>. Nothing here writes to a card. Format notes and sources: README.md.

Run: python3 -I badge_sets.py list        [--data DIR] [--in TREE]
     python3 -I badge_sets.py append      [--data DIR] [--in TREE] [--out TREE] [--name NAME] SET_DIR
     python3 -I badge_sets.py remove-last [--data DIR] [--in TREE] [--out TREE] [--allow-home-changes] [--undo FILE]
     python3 -I badge_sets.py check-slots [--data DIR] [--in TREE] [--slots TREE]
     python3 -I badge_sets.py decrypt     --sd SDROOT --out DIR KEYS
     python3 -I badge_sets.py encrypt     --from DIR [--out DIR] KEYS TREE
     (KEYS: --save3ds BIN --boot9 FILE --movable FILE, the console's keys where they are)
     (DIR defaults to BADGES_DATA from the environment or the repo's .env; --in defaults to DIR/work/current/000014d1,
     --slots to DIR/work/current/0000165c, --out to the input path plus .append-<set id> or .remove-<set id>)
"""
import argparse
import base64
import datetime
import hashlib
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import unicodedata
import zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent

# BadgeData.dat: names and images, by set index and badge index
DATA_SIZE = 0xF4DF80
SET_TITLES = 0x0                  # 100 x 16 languages x 0x8A, UTF-16LE
BADGE_TITLES = 0x35E80            # 1000 x 16 x 0x8A
TITLE_SIZE, LANGUAGES = 0x8A, 16
TITLE_BLOCK = TITLE_SIZE * LANGUAGES
SET_ICONS, SET_ICON_SIZE = 0x250F80, 0x2000          # 64x64 tiled RGB565, the icon in its top-left 48x48
BADGE_ICONS, BADGE_ICON_SIZE = 0x318F80, 0x2800      # 64x64 tiled RGB565 + A4
SMALL_ICONS, SMALL_ICON_SIZE = 0xCDCF80, 0xA00       # 32x32 tiled RGB565 + A4

# BadgeMngFile.dat: the catalogue and the live layout
MNG_SIZE = 0xD4A8
SET_COUNT, UNIQUE, PLACED, SELECTED_SET, SELECTED_COLUMN, TOTAL, NNID = 0x4, 0x8, 0xC, 0x10, 0x14, 0x18, 0x1C
USED_BADGES, USED_BADGES_SIZE = 0x358, 0x80
USED_SETS, USED_SETS_SIZE = 0x3D8, 0x10
BADGE_INFO, BADGE_INFO_SIZE = 0x3E8, 0x28
SET_INFO, SET_INFO_SIZE = 0xA028, 0x30
LAYOUT, LAYOUT_RECORD = 0xB2E8, 0x18

# MngFile.dat in extdata 0000165c: eight saved layouts, each with a full copy of BadgeMngFile.dat
SLOTS_SIZE = 0x4D1280
SLOT_FLAGS, SLOT_COUNT = 0x4, 8
SLOT_BADGES = 0x2ED10

MAX_SETS, MAX_BADGES, MAX_PLACED = 100, 1000, 360
QUANTITY = 0xFFFF                 # what Anemone3DS writes: unlimited copies
NO_SHORTCUT = b"\xff" * 16        # u64 title ID twice, all 0xFF = none
SET_UNKNOWN_0C = 0x2710
NONE = 0xFFFFFFFF
FOLDER_ICON = 0xF0FF
EMPTY_RECORD = bytes(4) + b"\xff" * 10 + bytes(2) + b"\xff" * 8   # a free layout record as HOME writes it
SUBS = {0, 0x100, 0x101, 0x1000, 0x1010, 0x1100, 0x1101, 0x1110, 0x1111}
NAME_UNITS = TITLE_SIZE // 2 - 1  # UTF-16 units before the terminating NUL

# Set and badge IDs this tool hands out. Anemone3DS and Advanced Badge Editor number sets 1..100 and badges 1..1000,
# GYTB uses set 0xEFBE and badges 1..n; neither range meets these, so a later Anemone rebuild cannot re-target a
# placement that names one of our badges (it empties it instead). Below 0x8000 in case any code reads them signed.
SET_IDS = range(0x7000, 0x8000)
BADGE_IDS = range(0x4000, 0x7000)

DAT_NAMES = ("BadgeData.dat", "BadgeMngFile.dat")

# save3ds v1.4.0 (github.com/wwylele/save3ds), the macOS arm64 build in 3DSort v1.3.0's release asset: the build this
# was tested with on this console's files. It receives the console keys, so any other binary is refused.
SAVE3DS_SHA256 = "f386b6899e3842254302d2602bb4e8a656b279c55570f573e7463046efd13fb4"
BADGE_EXTDATA, SLOTS_EXTDATA = "000014d1", "0000165c"
REWRITTEN = {"user/BadgeData.dat", "user/BadgeMngFile.dat"}   # the only files encrypt may change in a tree
TILE = 64
ICON = 48


class BadgeError(Exception):
    pass


# ---------------------------------------------------------------- settings and files

def setting(name):
    """From the environment, else from the repo's .env (one KEY=value per line, gitignored)."""
    if name in os.environ:
        return os.environ[name]
    env = HERE.parent.parent / ".env"
    for line in env.read_text(encoding="utf-8").splitlines() if env.is_file() else []:
        key, sep, value = line.partition("=")
        if sep and key.strip() == name:
            return value.strip().strip("\"'")
    return None


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def read_sized(path, size, what):
    if not path.is_file():
        raise BadgeError(f"{path} not found ({what}; see README, Getting the files)")
    raw = path.read_bytes()
    if len(raw) != size:
        raise BadgeError(f"{path} is {len(raw):#x} bytes, a {what} is {size:#x}: not a decrypted copy, or not this "
                         f"file")
    return raw


def read_tree(tree):
    tree = Path(tree)
    data = read_sized(tree / "user" / "BadgeData.dat", DATA_SIZE, "BadgeData.dat")
    mng = read_sized(tree / "user" / "BadgeMngFile.dat", MNG_SIZE, "BadgeMngFile.dat")
    return data, mng


def write_tree(src, dst, data, mng):
    """A copy of the extract tree src at dst (never in place) with new user/ badge files, verified after writing."""
    src, dst = Path(src), Path(dst)
    if dst.exists():
        raise BadgeError(f"{dst} exists; this tool never overwrites, pass another --out or move it away")
    if dst.resolve() == src.resolve() or src.resolve() in dst.resolve().parents:
        raise BadgeError(f"--out must lie next to the input, not in it: {dst}")
    dst.parent.mkdir(parents=True, exist_ok=True)
    tmp = Path(tempfile.mkdtemp(prefix=f".{dst.name}.", dir=dst.parent))
    try:
        tmp.rmdir()
        shutil.copytree(src, tmp, ignore=lambda d, names: [n for n in names if Path(d) == src / "user"
                                                           and n in DAT_NAMES])
        for name, content in zip(DAT_NAMES, (data, mng)):
            with open(tmp / "user" / name, "wb") as f:
                f.write(content)
                f.flush()
                os.fsync(f.fileno())
        if read_tree(tmp) != (data, mng):
            raise BadgeError(f"the files written to {tmp} read back differently; nothing was kept")
        tmp.rename(dst)
    finally:
        if tmp.exists():
            shutil.rmtree(tmp)


# ---------------------------------------------------------------- reading the badge files

def u32(buf, off):
    return struct.unpack_from("<I", buf, off)[0]


def bit(buf, base, n):
    return buf[base + n // 8] >> (n % 8) & 1


def title(data, off):
    raw = data[off:off + TITLE_SIZE]
    end = next((i for i in range(0, TITLE_SIZE, 2) if raw[i:i + 2] == b"\0\0"), TITLE_SIZE)
    return raw[:end].decode("utf-16-le", "replace")


def set_name(data, index):
    for lang in (1, 0):            # English first, then Japanese, as 3DSort shows them
        name = title(data, SET_TITLES + index * TITLE_BLOCK + lang * TITLE_SIZE)
        if name.strip():
            return name
    return ""


def badge_record(mng, slot):
    off = BADGE_INFO + slot * BADGE_INFO_SIZE
    unknown, badge_id, set_id, index, sub, placed, quantity, flags = struct.unpack_from("<IIIHHHHI", mng, off)
    return {"slot": slot, "unknown": unknown, "badge_id": badge_id, "set_id": set_id, "index": index, "sub": sub,
            "placed": placed, "quantity": quantity, "flags": flags, "identity": mng[off:off + 16],
            "raw": mng[off:off + BADGE_INFO_SIZE]}


def set_record(mng, slot):
    off = SET_INFO + slot * SET_INFO_SIZE
    set_id, index, _, unique, total, start = struct.unpack_from("<IIIIII", mng, off + 0x10)
    return {"slot": slot, "set_id": set_id, "index": index, "unique": unique, "total": total, "start": start,
            "raw": mng[off:off + SET_INFO_SIZE]}


def layout_records(mng):
    """Every placement record of a BadgeMngFile layout table: (slot, record), free records left out."""
    out = []
    for n in range(MAX_PLACED):
        rec = mng[LAYOUT + n * LAYOUT_RECORD:LAYOUT + (n + 1) * LAYOUT_RECORD]
        if rec not in (EMPTY_RECORD, bytes(LAYOUT_RECORD)):
            out.append((n, rec))
    return out


def catalogue(mng):
    """Identity (16 bytes) of every badge whose used bit is set; tolerant, for reading placements only."""
    return {mng[BADGE_INFO + n * BADGE_INFO_SIZE:BADGE_INFO + n * BADGE_INFO_SIZE + 16]: n
            for n in range(MAX_BADGES) if bit(mng, USED_BADGES, n)}


def parse(data, mng):
    """The collection, or BadgeError when the files do not look the way this tool can append to safely.

    Refuses: wrong sizes, header word 0 not zero, counts above the limits, used badge bits that are not exactly
    0..unique-1, a set count that disagrees with the set records, sets whose badges are not one contiguous index
    range in record order, totals that disagree with the quantities. Placement inconsistencies are warnings: HOME
    recounts placements itself and empties those whose badge is gone.
    """
    if len(data) != DATA_SIZE or len(mng) != MNG_SIZE:
        raise BadgeError("the badge files have the wrong size")
    bad = []
    if u32(mng, 0):
        bad.append(f"header word 0x0 is {u32(mng, 0):#x}, must be 0")
    n_sets, unique, placed, total = u32(mng, SET_COUNT), u32(mng, UNIQUE), u32(mng, PLACED), u32(mng, TOTAL)
    if n_sets > MAX_SETS or unique > MAX_BADGES or placed > MAX_PLACED:
        bad.append(f"header counts out of range: {n_sets} sets, {unique} badges, {placed} placed")
    if bad:
        raise BadgeError("; ".join(bad))
    used = [n for n in range(USED_BADGES_SIZE * 8) if bit(mng, USED_BADGES, n)]
    if used != list(range(unique)):
        raise BadgeError(f"the header counts {unique} badges, but the used-badge bits are {summary(used)}: "
                         f"not one block from index 0 (gaps or another installer's layout; not supported)")
    if any(bit(mng, USED_SETS, n) for n in range(MAX_SETS, USED_SETS_SIZE * 8)):
        bad.append("used-set bits beyond set 99 are set")

    sets = [set_record(mng, n) for n in range(MAX_SETS)]
    live, free = sets[:n_sets], sets[n_sets:]
    for s in free:
        if s["set_id"] not in (0, NONE):
            bad.append(f"set record {s['slot']} holds set ID {s['set_id']:#x}, but the header counts {n_sets} sets")
    ids = [s["set_id"] for s in live]
    indices = [s["index"] for s in live]
    if any(i in (0, NONE) for i in ids) or len(set(ids)) != len(ids):
        bad.append(f"set IDs are empty or repeated: {[hex(i) for i in ids]}")
    if any(i >= MAX_SETS for i in indices) or len(set(indices)) != len(indices):
        bad.append(f"set indices out of range or repeated: {indices}")
    if bad:
        raise BadgeError("; ".join(bad))

    badges = [badge_record(mng, n) for n in range(unique)]
    by_set = {i: [] for i in ids}
    seen = set()
    for b in badges:
        key = (b["badge_id"], b["set_id"], b["sub"])
        if b["index"] != b["slot"]:
            bad.append(f"badge record {b['slot']} says index {b['index']}")
        elif b["set_id"] not in by_set:
            bad.append(f"badge {b['slot']} belongs to set ID {b['set_id']:#x}, which has no set record")
        elif b["sub"] not in SUBS or b["badge_id"] in (0, NONE) or key in seen:
            bad.append(f"badge {b['slot']} has an empty, repeated or unknown identity")
        else:
            by_set[b["set_id"]].append(b)
        seen.add(key)
        if len(bad) > 5:
            break
    if bad:
        raise BadgeError("; ".join(bad))
    start = 0
    for s in live:
        members = [b["slot"] for b in by_set[s["set_id"]]]
        quantities = sum(b["quantity"] for b in by_set[s["set_id"]]) & NONE
        if members != list(range(start, start + len(members))) or s["start"] != start or s["unique"] != len(members):
            bad.append(f"set {s['set_id']:#x} (record {s['slot']}) claims {s['unique']} badges from index "
                       f"{s['start']}, its badges are {summary(members)}, expected from index {start}")
        elif s["total"] != quantities:
            bad.append(f"set {s['set_id']:#x} counts {s['total']} copies, its badges hold {quantities}")
        start += len(members)
        s["name"] = set_name(data, s["index"])
        s["placed"] = sum(b["placed"] for b in by_set[s["set_id"]])
    if total != sum(b["quantity"] for b in badges) & NONE:
        bad.append(f"header total {total} disagrees with the badges' quantities")
    if bad:
        raise BadgeError("; ".join(bad))

    warnings = []
    layout = layout_records(mng)
    identities = {b["identity"]: b for b in badges}
    counts, unknown = {}, 0
    for _, rec in layout:
        b = identities.get(rec[:16])
        if b is None:
            unknown += 1
        else:
            counts[b["slot"]] = counts.get(b["slot"], 0) + 1
    if unknown:
        warnings.append(f"{unknown} placements name no badge of the collection; HOME empties them")
    if len(layout) != placed:
        warnings.append(f"the header counts {placed} placed badges, the layout holds {len(layout)}; HOME recounts")
    if any(b["placed"] != counts.get(b["slot"], 0) for b in badges):
        warnings.append("per-badge placed counts disagree with the layout; HOME recounts")
    return {"data": data, "mng": mng, "sets": live, "badges": badges, "layout": layout, "warnings": warnings,
            "unique": unique, "placed": placed, "nnid_set": u32(mng, NNID) != 0}


def summary(indices):
    if not indices:
        return "none"
    runs, first = [], indices[0]
    for a, b in zip(indices, indices[1:] + [None]):
        if b != a + 1:
            runs.append(f"{first}" if first == a else f"{first}-{a}")
            first = b
    return ", ".join(runs[:6]) + (", ..." if len(runs) > 6 else "")


# ---------------------------------------------------------------- PNG (8-bit, non-interlaced) and badge images

PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
CHANNELS = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}


def png_chunks(raw, name):
    """(kind, body) of every chunk after the signature, CRC checked, up to IEND (not included)."""
    pos = 8
    while True:
        if pos + 12 > len(raw):
            raise BadgeError(f"{name}: the PNG ends before its IEND chunk")
        length, kind = struct.unpack_from(">I4s", raw, pos)
        body = raw[pos + 8:pos + 8 + length]
        crc = raw[pos + 8 + length:pos + 12 + length]
        if len(body) != length or len(crc) != 4 or zlib.crc32(kind + body) != int.from_bytes(crc, "big"):
            raise BadgeError(f"{name}: damaged PNG chunk {kind!r}")
        pos += 12 + length
        if kind == b"IEND":
            return
        yield kind, body


def read_png(path):
    """(width, height, RGBA bytes). 8-bit grey, RGB, palette, grey+alpha and RGBA, non-interlaced; tRNS honoured."""
    raw = Path(path).read_bytes()
    name = Path(path).name
    if raw[:8] != PNG_SIGNATURE:
        raise BadgeError(f"{name}: not a PNG file")
    chunks = png_chunks(raw, name)
    kind, body = next(chunks, (b"IEND", b""))
    if kind != b"IHDR" or len(body) != 13:
        raise BadgeError(f"{name}: the PNG does not start with IHDR")
    width, height, depth, ctype, compression, filtering, interlace = struct.unpack(">IIBBBBB", body)
    palette, trns, idat = b"", None, []
    for kind, body in chunks:
        if kind == b"PLTE":
            palette = body
        elif kind == b"tRNS":
            trns = body
        elif kind == b"IDAT":
            idat.append(body)
        elif kind == b"IHDR" or not kind[0] & 0x20:
            raise BadgeError(f"{name}: unexpected critical PNG chunk {kind!r}")
    if depth != 8 or ctype not in CHANNELS:
        raise BadgeError(f"{name}: {depth}-bit PNG colour type {ctype} is not supported; save it as 8-bit RGBA")
    if interlace or compression or filtering:
        raise BadgeError(f"{name}: interlaced or non-standard PNG is not supported; save it without interlacing")
    colours = []   # palette entries as RGBA, alpha from tRNS
    if ctype == 3:
        if not palette or len(palette) % 3:
            raise BadgeError(f"{name}: palette PNG without a valid PLTE chunk")
        alphas = trns or b""
        colours = [palette[i:i + 3] + bytes((alphas[i // 3] if i // 3 < len(alphas) else 255,))
                   for i in range(0, len(palette), 3)]
    if width == 0 or height == 0 or width * height > 4096 * 4096:
        raise BadgeError(f"{name}: unsupported image size {width}x{height}")
    try:
        pixels = zlib.decompress(b"".join(idat))
    except zlib.error as e:
        raise BadgeError(f"{name}: damaged PNG image data ({e})") from None
    bpp = CHANNELS[ctype]
    stride = width * bpp
    if len(pixels) != height * (stride + 1):
        raise BadgeError(f"{name}: PNG image data has the wrong length")
    rows, prev = [], bytearray(stride)
    for y in range(height):
        kind, line = pixels[y * (stride + 1)], bytearray(pixels[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        if kind == 1:
            for i in range(bpp, stride):
                line[i] = (line[i] + line[i - bpp]) & 0xFF
        elif kind == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 0xFF
        elif kind == 3:
            for i in range(stride):
                left = line[i - bpp] if i >= bpp else 0
                line[i] = (line[i] + ((left + prev[i]) >> 1)) & 0xFF
        elif kind == 4:
            for i in range(stride):
                a = line[i - bpp] if i >= bpp else 0
                b, c = prev[i], prev[i - bpp] if i >= bpp else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 0xFF
        elif kind:
            raise BadgeError(f"{name}: unknown PNG filter {kind}")
        rows.append(line)
        prev = line
    if ctype == 6:
        return width, height, b"".join(rows)
    rgba = bytearray(width * height * 4)
    o = 0
    for line in rows:
        for x in range(width):
            if ctype == 2:
                r, g, b = line[x * 3:x * 3 + 3]
                key = trns is not None and len(trns) == 6 and (r, g, b) == (trns[1], trns[3], trns[5])
                rgba[o:o + 4] = bytes((r, g, b, 0 if key else 255))
            elif ctype == 3:
                if line[x] >= len(colours):
                    raise BadgeError(f"{name}: palette index {line[x]} outside the palette")
                rgba[o:o + 4] = colours[line[x]]
            elif ctype == 4:
                g, a = line[x * 2:x * 2 + 2]
                rgba[o:o + 4] = bytes((g, g, g, a))
            else:
                g = line[x]
                key = trns is not None and len(trns) == 2 and g == trns[1]
                rgba[o:o + 4] = bytes((g, g, g, 0 if key else 255))
            o += 4
    return width, height, bytes(rgba)


def tiled(x, y, size):
    """Index of pixel (x, y) in a size x size image stored as 8x8 tiles, row by row, Morton order inside a tile."""
    m = (x & 1) | (y & 1) << 1 | (x & 2) << 1 | (y & 2) << 2 | (x & 4) << 2 | (y & 4) << 3
    return (y >> 3) * (size // 8) * 64 + (x >> 3) * 64 + m


def badge_images(rgba, width, x0, y0):
    """(0x2800 bytes 64x64 RGB565+A4, 0xA00 bytes 32x32 RGB565+A4) for the 64x64 tile at (x0, y0).

    RGB565 and A4 by truncation; the 32x32 image is the mean of each 2x2 block, truncated (Anemone3DS's rule).
    """
    def px(x, y):
        o = ((y0 + y) * width + x0 + x) * 4
        return rgba[o:o + 4]

    out = []
    for size in (64, 32):
        rgb = [0] * (size * size)
        alpha = bytearray(size * size // 2)
        for y in range(size):
            for x in range(size):
                if size == 64:
                    r, g, b, a = px(x, y)
                    r, g, b, a = r >> 3, g >> 2, b >> 3, a >> 4
                else:
                    q = [px(2 * x + dx, 2 * y + dy) for dy in (0, 1) for dx in (0, 1)]
                    r, g, b, a = (sum(p[c] for p in q) for c in range(4))
                    r, g, b, a = r >> 5, g >> 4, b >> 5, a >> 6
                i = tiled(x, y, size)
                rgb[i] = r << 11 | g << 5 | b
                alpha[i >> 1] |= a << ((i & 1) * 4)
        out.append(struct.pack(f"<{size * size}H", *rgb) + bytes(alpha))
    return out[0], out[1]


def set_icon_image(rgba, width, height):
    """0x2000 bytes: a 48x48 icon (alpha ignored, as HOME stores no alpha for set icons) in a 64x64 tiled RGB565
    image, the rest black. A 64x64 image (the default from the first badge) is scaled to 48x48, nearest pixel."""
    step = {ICON: 1, TILE: 4 / 3}[width]
    rgb = [0] * (64 * 64)
    for y in range(ICON):
        for x in range(ICON):
            o = (int(y * step) * width + int(x * step)) * 4
            r, g, b = rgba[o:o + 3]
            rgb[tiled(x, y, 64)] = (r >> 3) << 11 | (g >> 2) << 5 | b >> 3
    return struct.pack("<4096H", *rgb)


def title_block(name):
    raw = name.encode("utf-16-le")
    return (raw + b"\0\0").ljust(TITLE_SIZE, b"\0") * LANGUAGES


def check_name(name, what):
    if not name.strip():
        raise BadgeError(f"{what}: empty name")
    if len(name.encode("utf-16-le")) // 2 > NAME_UNITS:
        raise BadgeError(f"{what}: name {name!r} is longer than {NAME_UNITS} UTF-16 units; shorten it")
    return name


def read_set_folder(folder):
    """Badge tiles of a set folder: [(file, tile, name, x0, y0, rgba, width)], and the set icon (rgba, w, h) or None.

    Like Anemone3DS: every PNG whose sides are multiples of 64 is cut into 64x64 badges, column by column, each
    named after the file up to its first dot; _seticon.png (48x48) is the set icon. Files in name order.
    """
    folder = Path(folder)
    if not folder.is_dir():
        raise BadgeError(f"{folder} is not a folder")
    icon, pngs = None, []
    for p in sorted(folder.iterdir(), key=lambda p: (nfc(p.name).casefold(), nfc(p.name))):
        if p.name.startswith("."):
            continue                   # .DS_Store, AppleDouble ._ files
        if p.is_dir() or p.suffix.lower() != ".png":
            raise BadgeError(f"{p.name} in {folder.name}: only PNG files belong in a set folder "
                             f"(zip sets and subfolders are not supported); move it out")
        if nfc(p.name).lower() == "_seticon.png":
            icon = p
        else:
            pngs.append(p)
    if not pngs:
        raise BadgeError(f"{folder} holds no badge PNG")
    tiles = []
    for p in pngs:
        width, height, rgba = read_png(p)
        if width % TILE or height % TILE:
            raise BadgeError(f"{p.name}: {width}x{height}; a badge image's sides must be multiples of 64")
        name = check_name(nfc(p.name).split(".", 1)[0], p.name)
        for tx in range(width // TILE):
            for ty in range(height // TILE):
                tiles.append((nfc(p.name), tx * (height // TILE) + ty, name, tx * TILE, ty * TILE, rgba, width))
    icon_image = None
    if icon is not None:
        width, height, rgba = read_png(icon)
        if (width, height) != (ICON, ICON):
            raise BadgeError(f"{icon.name}: {width}x{height}; a set icon is 48x48")
        icon_image = (rgba, width, height)
    return tiles, icon_image


def nfc(text):
    return unicodedata.normalize("NFC", text)


# ---------------------------------------------------------------- manifest and undo records

def load_json(path):
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as e:
        raise BadgeError(f"{path} cannot be read ({e})") from None


def load_manifest(data_dir):
    path = Path(data_dir) / "manifest.json"
    manifest = (load_json(path) if path.is_file() else
                {"format": 1, "next_set_id": SET_IDS.start, "next_badge_id": BADGE_IDS.start, "sets": []})
    try:
        set_ids = [s["set_id"] for s in manifest["sets"]]
        badge_ids = [b["badge_id"] for s in manifest["sets"] for b in s["badges"]]
        folders = [s["folder"] for s in manifest["sets"]]
        good = (manifest.get("format") == 1 and len(set(set_ids)) == len(set_ids)
                and len(set(badge_ids)) == len(badge_ids) and len(set(folders)) == len(folders)
                and all(i in SET_IDS for i in set_ids) and all(i in BADGE_IDS for i in badge_ids)
                and manifest["next_set_id"] > max(set_ids, default=SET_IDS.start - 1)
                and manifest["next_badge_id"] > max(badge_ids, default=BADGE_IDS.start - 1))
    except (KeyError, TypeError):
        good = False
    if not good:
        raise BadgeError(f"{path} has missing, repeated or out-of-range IDs or counters; fix it by hand "
                         f"(IDs never change)")
    return manifest


def save_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(path.name + ".tmp")
    tmp.write_text(json.dumps(value, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    tmp.replace(path)


def assign_ids(manifest, folder, tiles, name, today):
    """(manifest after this append, its set entry, the entry's badges present in the folder, in manifest order).

    A set keeps its ID and every (file, tile) keeps its badge ID forever; new tiles get the next unused IDs.
    A tile that is gone from the folder keeps its entry and ID, it is only not written.
    """
    manifest = json.loads(json.dumps(manifest))
    entry = next((s for s in manifest["sets"] if s["folder"] == folder), None)
    if entry is None:
        if manifest["next_set_id"] not in SET_IDS:
            raise BadgeError(f"no set ID left in {SET_IDS.start:#x}-{SET_IDS.stop - 1:#x}")
        entry = {"folder": folder, "name": name, "set_id": manifest["next_set_id"], "added": today, "badges": []}
        manifest["sets"].append(entry)
        manifest["next_set_id"] += 1
    entry["name"] = name
    known = {(b["file"], b["tile"]) for b in entry["badges"]}
    for file, tile, *_ in tiles:
        if (file, tile) not in known:
            if manifest["next_badge_id"] not in BADGE_IDS:
                raise BadgeError(f"no badge ID left in {BADGE_IDS.start:#x}-{BADGE_IDS.stop - 1:#x}")
            entry["badges"].append({"file": file, "tile": tile, "badge_id": manifest["next_badge_id"]})
            manifest["next_badge_id"] += 1
    present = {(t[0], t[1]): t for t in tiles}
    return manifest, entry, [(b, present[(b["file"], b["tile"])]) for b in entry["badges"]
                             if (b["file"], b["tile"]) in present]


def regions(s, u, n):
    """Every byte range an append of a set at set slot and index s with n badges from index u writes."""
    return [
        ("mng", SET_COUNT, 4), ("mng", UNIQUE, 4), ("mng", TOTAL, 4),
        ("mng", USED_BADGES + u // 8, (u + n - 1) // 8 - u // 8 + 1),
        ("mng", USED_SETS + s // 8, 1),
        ("mng", BADGE_INFO + u * BADGE_INFO_SIZE, n * BADGE_INFO_SIZE),
        ("mng", SET_INFO + s * SET_INFO_SIZE, SET_INFO_SIZE),
        ("data", SET_TITLES + s * TITLE_BLOCK, TITLE_BLOCK),
        ("data", BADGE_TITLES + u * TITLE_BLOCK, n * TITLE_BLOCK),
        ("data", SET_ICONS + s * SET_ICON_SIZE, SET_ICON_SIZE),
        ("data", BADGE_ICONS + u * BADGE_ICON_SIZE, n * BADGE_ICON_SIZE),
        ("data", SMALL_ICONS + u * SMALL_ICON_SIZE, n * SMALL_ICON_SIZE),
    ]


def region_bytes(files, regs):
    return b"".join(files[f][off:off + size] for f, off, size in regs)


def unchanged_outside(before, after, regs, allowed_bits):
    """Problems, if any byte outside regs differs, or a bitfield byte inside regs changes a bit not allowed."""
    problems = []
    for f in ("data", "mng"):
        spans = sorted((off, off + size) for g, off, size in regs if g == f)
        pos = 0
        for start, end in spans + [(len(before[f]), len(before[f]))]:
            if before[f][pos:start] != after[f][pos:start]:
                first = next(i for i in range(pos, start) if before[f][i] != after[f][i])
                problems.append(f"{f} byte {first:#x} would change")
            pos = max(pos, end)
    for base, size in ((USED_BADGES, USED_BADGES_SIZE), (USED_SETS, USED_SETS_SIZE)):
        for n in range(size * 8):
            if bit(before["mng"], base, n) != bit(after["mng"], base, n) and (base, n) not in allowed_bits:
                problems.append(f"used bit {n} at {base:#x} would change")
    return problems


def same_parse(old, new, n_sets, n_badges):
    """Problems, if the first sets and badges of `new` differ in any field from `old`, or the layout differs."""
    problems = []
    for a, b in zip(old["sets"][:n_sets], new["sets"]):
        if a != b:
            problems.append(f"set {a['set_id']:#x} would change")
    for a, b in zip(old["badges"][:n_badges], new["badges"]):
        if a != b:
            problems.append(f"badge {a['slot']} would change")
    if old["layout"] != new["layout"] or len(new["sets"]) < n_sets or len(new["badges"]) < n_badges:
        problems.append("the layout or the old sets would change")
    return problems


def undo_dir(data_dir):
    return Path(data_dir) / "undo"


# ---------------------------------------------------------------- commands

def cmd_list(data_dir, tree):
    col = parse(*read_tree(tree))
    mine = {s["set_id"] for s in load_manifest(data_dir)["sets"]}
    print(f"{tree}")
    print(f"sets {len(col['sets'])}/{MAX_SETS}, badges {col['unique']}/{MAX_BADGES}, placed {col['placed']}/"
          f"{MAX_PLACED}, NNID field: {'set' if col['nnid_set'] else 'not set (0)'}")
    print(f"{'slot':>4}  {'set id':>10}  {'badges':>6}  {'from':>5}  {'placed':>6}  name")
    for s in col["sets"]:
        print(f"{s['slot']:>4}  {s['set_id']:#10x}  {s['unique']:>6}  {s['start']:>5}  {s['placed']:>6}  "
              f"{s['name']}{'  (appended by this tool)' if s['set_id'] in mine else ''}")
    print(f"free: {MAX_SETS - len(col['sets'])} sets, {MAX_BADGES - col['unique']} badges")
    for w in col["warnings"]:
        print(f"warning: {w}")


def default_out(tree, action, set_id):
    tree = Path(tree)
    return tree.with_name(f"{tree.name}.{action}-{set_id:04x}")


def cmd_append(data_dir, tree, out, set_dir, name, today):
    data, mng = read_tree(tree)
    col = parse(data, mng)
    set_dir = Path(set_dir)
    folder = nfc(set_dir.resolve().name)
    tiles, icon = read_set_folder(set_dir)
    manifest = load_manifest(data_dir)
    known = next((s for s in manifest["sets"] if s["folder"] == folder), None)
    name = check_name(nfc(name) if name else (known["name"] if known else folder), "set")
    manifest, entry, badges = assign_ids(manifest, folder, tiles, name, today)
    set_id, s, u, n = entry["set_id"], len(col["sets"]), col["unique"], len(badges)

    if any(x["set_id"] == set_id for x in col["sets"]):
        raise BadgeError(f"set {folder!r} (ID {set_id:#x}) is already in the collection")
    if s + 1 > MAX_SETS:
        raise BadgeError(f"the collection holds {s} sets; HOME takes at most {MAX_SETS}")
    if u + n > MAX_BADGES:
        raise BadgeError(f"{u} badges + {n} new = {u + n}; HOME takes at most {MAX_BADGES}")
    if s in [x["index"] for x in col["sets"]]:
        raise BadgeError(f"set index {s} is taken by an existing set; this file numbers its sets differently "
                         f"from Anemone3DS (not supported)")
    taken = {b["badge_id"] for b in col["badges"]}
    if any(b["badge_id"] in taken for b, _ in badges):
        raise BadgeError("a badge ID from the manifest is already in the collection; check manifest.json")
    out = out or default_out(tree, "append", set_id)
    if out.exists():
        raise BadgeError(f"{out} exists; this tool never overwrites, pass another --out or move it away")

    new = {"data": bytearray(data), "mng": bytearray(mng)}
    m, d = new["mng"], new["data"]
    struct.pack_into("<I", m, SET_COUNT, s + 1)
    struct.pack_into("<I", m, UNIQUE, u + n)
    struct.pack_into("<I", m, TOTAL, (u32(mng, TOTAL) + n * QUANTITY) & NONE)
    allowed = {(USED_BADGES, i) for i in range(u, u + n)}
    set_bit = bit(mng, USED_SETS, col["sets"][-1]["slot"]) if col["sets"] else 1
    if set_bit:
        m[USED_SETS + s // 8] |= 1 << (s % 8)
        allowed.add((USED_SETS, s))
    for k, (entry_badge, (_, _, badge_name, x0, y0, rgba, width)) in enumerate(badges):
        i = u + k
        m[USED_BADGES + i // 8] |= 1 << (i % 8)
        struct.pack_into("<IIIHHHHI16s", m, BADGE_INFO + i * BADGE_INFO_SIZE, 0, entry_badge["badge_id"], set_id,
                         i, 0, 0, QUANTITY, 0, NO_SHORTCUT)
        big, small = badge_images(rgba, width, x0, y0)
        d[BADGE_TITLES + i * TITLE_BLOCK:BADGE_TITLES + (i + 1) * TITLE_BLOCK] = title_block(badge_name)
        d[BADGE_ICONS + i * BADGE_ICON_SIZE:BADGE_ICONS + (i + 1) * BADGE_ICON_SIZE] = big
        d[SMALL_ICONS + i * SMALL_ICON_SIZE:SMALL_ICONS + (i + 1) * SMALL_ICON_SIZE] = small
    struct.pack_into("<8sIIIIIIIIII", m, SET_INFO + s * SET_INFO_SIZE, b"\xff" * 8, 0, SET_UNKNOWN_0C, set_id, s,
                     NONE, n, n * QUANTITY, u, 0, 0)
    d[SET_TITLES + s * TITLE_BLOCK:SET_TITLES + (s + 1) * TITLE_BLOCK] = title_block(name)
    if icon is None:   # the first badge's 64x64 tile
        _, _, _, x0, y0, rgba, width = badges[0][1]
        icon = (b"".join(rgba[((y0 + y) * width + x0) * 4:((y0 + y) * width + x0 + TILE) * 4] for y in range(TILE)),
                TILE, TILE)
        icon_from = "first badge, scaled to 48x48"
    else:
        icon_from = "_seticon.png"
    d[SET_ICONS + s * SET_ICON_SIZE:SET_ICONS + (s + 1) * SET_ICON_SIZE] = set_icon_image(*icon)

    # The invariant, checked on the finished buffers before anything is written: nothing that exists changes.
    regs = regions(s, u, n)
    before, after = {"data": data, "mng": mng}, {"data": bytes(d), "mng": bytes(m)}
    problems = unchanged_outside(before, after, regs, allowed)
    result = parse(after["data"], after["mng"])
    problems += same_parse(col, result, s, u)
    if (u32(m, SET_COUNT), u32(m, UNIQUE), u32(m, TOTAL)) != (s + 1, u + n, (u32(mng, TOTAL) + n * QUANTITY) & NONE):
        problems.append("the header counts did not move by exactly the new set")
    added = result["sets"][s] if len(result["sets"]) == s + 1 else None
    if (added is None or (added["set_id"], added["start"], added["unique"], added["name"]) != (set_id, u, n, name)
            or [b["badge_id"] for b in result["badges"][u:]] != [b["badge_id"] for b, _ in badges]):
        problems.append("the new set does not read back as written")
    if problems:
        raise BadgeError("refused, the append would not keep the existing collection intact: " + "; ".join(problems))

    sums_in = {"data": sha256(data), "mng": sha256(mng)}
    record = json.loads(json.dumps({
        "set_id": set_id, "folder": folder, "regions": regs,
        "before": base64.b64encode(zlib.compress(region_bytes(before, regs), 9)).decode("ascii"),
        "after_sha256": sha256(region_bytes(after, regs)),
        "after_region_sha256": [sha256(region_bytes(after, [r])) for r in regs],
        "view_before": mng[SELECTED_SET:TOTAL].hex(),
        "input_sha256": sums_in,
        "output_sha256": {"data": sha256(after["data"]), "mng": sha256(after["mng"])}}))
    # Named after input and output together: two inputs that differ only inside the replaced areas give the same
    # output, and each needs its own earlier bytes back.
    key = "".join(record[side][f] for side in ("input_sha256", "output_sha256") for f in ("data", "mng"))
    record_path = undo_dir(data_dir) / f"{set_id:04x}-{sha256(key.encode())[:16]}.json"
    if record_path.is_file():
        if load_json(record_path) != record:
            raise BadgeError(f"{record_path} is the undo record of this input and this result, but holds something "
                             f"else; look at it before anything else (nothing was written)")
        record_note = "already there for this input, identical"
    else:
        save_json(record_path, record)
        record_note = "new"
    write_tree(tree, out, after["data"], after["mng"])
    save_json(Path(data_dir) / "manifest.json", manifest)

    print(f"appended set {name!r} (ID {set_id:#x}, set slot {s}) with {n} badges at indices {u}-{u + n - 1}:")
    for k, (b, t) in enumerate(badges):
        print(f"  {u + k:4d}  {b['badge_id']:#06x}  {t[2]}  ({t[0]} tile {t[1]})")
    print(f"set icon: {icon_from}; used-set bit "
          f"{'set' if set_bit else 'clear, like the sets before it'}")
    print(f"checked: every byte outside the new set's records, names and images is unchanged; sets {s} -> {s + 1}, "
          f"badges {u} -> {u + n}")
    for w in result["warnings"]:
        print(f"warning (unchanged from the input): {w}")
    print(f"wrote {out}")
    print(f"undo record {record_path} ({record_note})")


def cmd_remove_last(data_dir, tree, out, allow_changes=False, undo=None):
    data, mng = read_tree(tree)
    col = parse(data, mng)
    if not col["sets"]:
        raise BadgeError("the collection has no set")
    last = col["sets"][-1]
    manifest = load_manifest(data_dir)
    entry = next((s for s in manifest["sets"] if s["set_id"] == last["set_id"]), None)
    if entry is None:
        raise BadgeError(f"the last set {last['name']!r} (ID {last['set_id']:#x}) was not appended by this tool "
                         f"(not in manifest.json); removing it is not supported")
    s, u, n = last["slot"], last["start"], last["unique"]
    if u + n != col["unique"] or n == 0:
        raise BadgeError("the last set does not end at the last badge index")
    placed = [b["slot"] for b in col["badges"][u:] if b["placed"]] + \
             [slot for slot, rec in col["layout"] if u32(rec, 8) == last["set_id"]]
    if placed:
        raise BadgeError(f"badges of {last['name']!r} are placed in the live layout; put them back into the case "
                         f"on the console first")
    regs = [list(r) for r in regions(s, u, n)]
    current = {"data": data, "mng": mng}
    given = {"data": sha256(data), "mng": sha256(mng)}
    now = sha256(region_bytes(current, regs))
    paths = [Path(undo)] if undo else sorted(undo_dir(data_dir).glob(f"{last['set_id']:04x}-*.json"))
    records = [(path, rec) for path, rec in ((path, load_json(path)) for path in paths)
               if rec.get("set_id") == last["set_id"] and rec.get("regions") == regs]
    if not records:
        raise BadgeError(f"no undo record of set {last['name']!r} at indices {u}-{u + n - 1} "
                         f"({undo or undo_dir(data_dir)}); the exact earlier bytes are unknown")
    candidates = [(path, rec) for path, rec in records if rec["output_sha256"] == given]
    if not candidates:
        if not allow_changes:
            raise BadgeError("these files are not what a recorded append wrote (their sha256 differ from every undo "
                             "record's output): something changed them since, HOME for example. With "
                             "--allow-home-changes only the set's areas and the case's view get their earlier bytes "
                             "back and every other change is kept")
        candidates = [(path, rec) for path, rec in records if rec["after_sha256"] == now]
        if not candidates:
            near = [f"{f} {off:#x}+{size:#x}" for (f, off, size), h in zip(regs, records[0][1]["after_region_sha256"])
                    if sha256(region_bytes(current, [(f, off, size)])) != h]
            raise BadgeError(f"set {last['name']!r} itself changed since the append, so no undo record fits; the "
                             f"exact earlier bytes are unknown; regions that differ from the closest record: "
                             f"{', '.join(near)}")
    if len(candidates) > 1:
        names = ", ".join(p.name for p, _ in candidates)
        raise BadgeError(f"{len(candidates)} undo records fit these files ({names}): "
                         f"appends onto inputs that differ only inside the set's areas give the same output, so the "
                         f"input is unknown; pass --undo with the record that append printed")
    path, record = candidates[0]
    exact = record["output_sha256"] == given
    old = zlib.decompress(base64.b64decode(record["before"]))
    if len(old) != sum(size for _, _, size in regs):
        raise BadgeError(f"{path} is damaged")
    new = {"data": bytearray(data), "mng": bytearray(mng)}
    pos = 0
    for f, off, size in regs:
        new[f][off:off + size] = old[pos:pos + size]
        pos += size
    # The badge case's view (selected set, selected column) may name the removed set: back to its state before.
    view = bytes.fromhex(record["view_before"])
    view_reset = mng[SELECTED_SET:TOTAL] != view
    new["mng"][SELECTED_SET:TOTAL] = view
    after = {"data": bytes(new["data"]), "mng": bytes(new["mng"])}
    problems = unchanged_outside(current, after, regs + [["mng", SELECTED_SET, TOTAL - SELECTED_SET]],
                                 {(USED_BADGES, i) for i in range(u, u + n)} | {(USED_SETS, s)})
    result = parse(after["data"], after["mng"])
    problems += same_parse(col, result, s, u)
    if len(result["sets"]) != s or result["unique"] != u:
        problems.append("the set is still there after the restore")
    identical = {"data": sha256(after["data"]), "mng": sha256(after["mng"])} == record["input_sha256"]
    if exact and not identical:
        problems.append("the restored files differ from the files before the append (sha256 in the undo record)")
    if problems:
        raise BadgeError("refused: " + "; ".join(problems))
    if out is None:
        out = default_out(tree, "remove", last["set_id"])
    write_tree(tree, out, after["data"], after["mng"])
    print(f"removed set {last['name']!r} (ID {last['set_id']:#x}, {n} badges at indices {u}-{u + n - 1}); "
          f"its regions hold their bytes from before the append again ({path.name})")
    if view_reset:
        print("the badge case's view (selected set and column, header 0x10 and 0x14) was changed since the append; "
              "it is back to its state before the append")
    if identical:
        print("both files are byte-identical to the files before the append (sha256 compared with the undo record)")
    else:
        print("not byte-identical to the files before the append: the input had changed since the append "
              "(--allow-home-changes); the set's regions and the case's view hold their bytes from before the "
              "append, everything else is kept as it is in the input")
    print(f"wrote {out}")


class Save3ds:
    """save3ds_fuse --extract / --import of one SD extdata, always on a copy made by this tool."""

    def __init__(self, exe, boot9, movable):
        for path, flag in ((exe, "--save3ds"), (boot9, "--boot9"), (movable, "--movable")):
            if not Path(path).is_file():
                raise BadgeError(f"{flag} {path} is not a file")
        if sha256(Path(exe).read_bytes()) != SAVE3DS_SHA256:
            raise BadgeError(f"{exe} is not the save3ds build this tool was tested with (sha256 differs); it receives "
                             f"the console keys, so it is refused")
        self.exe, self.boot9, self.movable = Path(exe), Path(boot9), Path(movable)

    def run(self, extdata, sd, mode, target):
        r = subprocess.run([str(self.exe), "--sdext", f"00000000{extdata}", "--sd", str(sd), "--boot9", str(self.boot9),
                            "--movable", str(self.movable), f"--{mode}", str(target)], capture_output=True, text=True)
        if r.returncode != 0:
            raise BadgeError(f"save3ds --{mode} of {extdata} failed (exit {r.returncode}): "
                             f"{(r.stderr or r.stdout).strip()[-400:]}")


def extdata_folder(sd, extdata):
    hits = sorted(Path(sd).glob(f"Nintendo 3DS/*/*/extdata/00000000/{extdata}"))
    if len(hits) != 1:
        raise BadgeError(f"{sd} must hold exactly one Nintendo 3DS/<id0>/<id1>/extdata/00000000/{extdata}, "
                         f"found {len(hits)}")
    return hits[0]


def listing(folder):
    """{path below folder: sha256 of the file, or "dir"}, hidden entries included (save3ds would import them)."""
    folder = Path(folder)
    return {p.relative_to(folder).as_posix(): "dir" if p.is_dir() else sha256(p.read_bytes())
            for p in sorted(folder.rglob("*"))}


def sums(folders):
    """encrypted.sha256 lines for {extdata: folder}: "<sha256>  ./<extdata>/<path>", as the card backups list them."""
    return "".join(f"{h}  ./{ext}/{path}\n" for ext, folder in folders.items()
                   for path, h in listing(folder).items() if h != "dir")


def new_dir_next_to(out):
    """A fresh hidden folder in out's parent; renamed to out at the end, removed on failure."""
    if out.exists():
        raise BadgeError(f"{out} exists; this tool never overwrites, pass another --out or move it away")
    out.parent.mkdir(parents=True, exist_ok=True)
    return Path(tempfile.mkdtemp(prefix=f".{out.name}.", dir=out.parent))


def cmd_decrypt(sd, out, tool):
    sd, out = Path(sd), Path(out)
    folders = {BADGE_EXTDATA: extdata_folder(sd, BADGE_EXTDATA)}
    if list(sd.glob(f"Nintendo 3DS/*/*/extdata/00000000/{SLOTS_EXTDATA}")):
        folders[SLOTS_EXTDATA] = extdata_folder(sd, SLOTS_EXTDATA)
    before = {ext: listing(f) for ext, f in folders.items()}
    tmp = new_dir_next_to(out)
    try:
        copies = {}
        for ext, src in folders.items():
            copies[ext] = tmp / "sd" / src.relative_to(sd)
            shutil.copytree(src, copies[ext])
        if {ext: listing(f) for ext, f in copies.items()} != before:
            raise BadgeError("the copy differs from the source; nothing was kept")
        for ext in folders:
            tool.run(ext, tmp / "sd", "extract", tmp / ext)
        read_tree(tmp / BADGE_EXTDATA)
        if SLOTS_EXTDATA in folders:
            read_sized(tmp / SLOTS_EXTDATA / "user" / "MngFile.dat", SLOTS_SIZE, "MngFile.dat (extdata 0000165c)")
        if {ext: listing(f) for ext, f in folders.items()} != before or \
                {ext: listing(f) for ext, f in copies.items()} != before:
            raise BadgeError("the encrypted files changed while they were read; nothing was kept")
        (tmp / "encrypted.sha256").write_text(sums(copies), encoding="utf-8")
        tmp.rename(out)
    finally:
        if tmp.exists():
            shutil.rmtree(tmp)
    print(f"decrypted {', '.join(folders)} from {sd} (only read):")
    for ext in folders:
        print(f"  {out / ext}/")
    print(f"the encrypted copy as read: {out / 'sd'}/, its sums: {out / 'encrypted.sha256'}")
    if SLOTS_EXTDATA not in folders:
        print(f"no {SLOTS_EXTDATA} in {sd}: check-slots needs it from another decrypt")


def cmd_encrypt(source, tree, out, tool):
    """source: a decrypt output (source/sd, source/000014d1). tree: what to write, made from source/000014d1."""
    source, tree = Path(source), Path(tree)
    out = Path(out) if out else tree.with_name(tree.name + ".encrypted")
    expect = source / BADGE_EXTDATA
    folder = extdata_folder(source / "sd", BADGE_EXTDATA)
    rel = folder.relative_to(source / "sd")
    parse(*read_tree(tree))
    new, old = listing(tree), listing(expect)
    if set(new) != set(old):
        raise BadgeError(f"{tree} and {expect} do not hold the same files and folders (only in the tree: "
                         f"{sorted(set(new) - set(old))}, only in the decrypt: {sorted(set(old) - set(new))}); "
                         f"save3ds would import every one of them")
    other = sorted(p for p in new if new[p] != old[p] and p not in REWRITTEN)
    if other:
        raise BadgeError(f"{tree} changes {other}; only {sorted(REWRITTEN)} may differ from the decrypt")
    encrypted = listing(folder)
    tmp = new_dir_next_to(out)
    try:
        sd = tmp / "out"
        shutil.copytree(folder, sd / rel)
        tool.run(BADGE_EXTDATA, sd, "extract", tmp / "before")
        if listing(tmp / "before") != old:
            raise BadgeError(f"{folder} does not decrypt to {expect}; the decrypt and its encrypted copy do not belong "
                             f"together")
        tool.run(BADGE_EXTDATA, sd, "import", tree)
        tool.run(BADGE_EXTDATA, sd, "extract", tmp / "after")
        if listing(tmp / "after") != new:
            raise BadgeError(f"after the import the extdata does not decrypt to {tree}; nothing was kept")
        if listing(folder) != encrypted:
            raise BadgeError(f"{folder} changed while it was read; nothing was kept")
        result = listing(sd / rel)
        (sd / "encrypted.sha256").write_text(sums({BADGE_EXTDATA: sd / rel}), encoding="utf-8")
        sd.rename(out)
    finally:
        if tmp.exists():
            shutil.rmtree(tmp)
    print(f"encrypted {tree} into {out}/")
    print("checked: before the import the copy decrypted to the --from tree, after it it decrypts to TREE")
    print(f"encrypted files in {BADGE_EXTDATA}/ (before -> after):")
    for path in sorted(set(encrypted) | set(result)):
        a, b = encrypted.get(path, "-"), result.get(path, "-")
        state = "folder" if "dir" in (a, b) else "unchanged" if a == b else "new" if a == "-" else \
            "removed" if b == "-" else "rewritten"
        print(f"  {path:<20} {a[:12]:<12} -> {b[:12]:<12} {state}")
    print(f"{SLOTS_EXTDATA} is not in the output: only {BADGE_EXTDATA} was copied and imported")
    print(f"sums for the copy onto the card: {out / 'encrypted.sha256'}")


def slot_report(mng, live, label, saved):
    if u32(mng, 0) != 0:
        return f"{label:<8}{saved:<7}damaged copy (header word 0x0 not 0)", False
    placements = layout_records(mng)
    by_ids = {(k[4:12], k[14:16]) for k in live}
    missing = sum(1 for _, r in placements if (r[4:12], r[14:16]) not in by_ids)
    differs = sum(1 for _, r in placements if (r[4:12], r[14:16]) in by_ids and r[:16] not in live)
    icons = sum(1 for _, r in placements if u32(r, 0x14) == FOLDER_ICON)
    count = "" if u32(mng, PLACED) == len(placements) else f" (header says {u32(mng, PLACED)})"
    line = f"{label:<8}{saved:<7}{len(placements):>11}{icons:>14}{missing:>9}{differs:>15}{count}"
    return line, missing == 0 and differs == 0


def cmd_check_slots(tree, slots_tree):
    _, mng = read_tree(tree)
    if u32(mng, 0) or u32(mng, UNIQUE) > MAX_BADGES:
        raise BadgeError("the live BadgeMngFile.dat header is not valid")
    slots = read_sized(Path(slots_tree) / "user" / "MngFile.dat", SLOTS_SIZE, "MngFile.dat (extdata 0000165c)")
    if u32(slots, 0):
        raise BadgeError("MngFile.dat header word 0x0 is not 0")
    live = catalogue(mng)
    print(f"live collection: {len(live)} badges ({tree})")
    print(f"{'layout':<8}{'saved':<7}{'placements':>11}{'folder icons':>14}{'missing':>9}{'index differs':>15}")
    line, ok = slot_report(mng, live, "live", "-")
    print(line)
    for n in range(SLOT_COUNT):
        copy = slots[SLOT_BADGES + n * MNG_SIZE:SLOT_BADGES + (n + 1) * MNG_SIZE]
        saved = slots[SLOT_FLAGS + n] != 0
        line, good = slot_report(copy, live, f"slot {n + 1}", "yes" if saved else "no")
        print(line)
        ok = ok and (good or not saved)
    print("missing: no badge with that badge ID, set ID and sub ID (HOME empties the placement when it loads the "
          "slot); index differs: the badge exists, other identity bytes differ")
    print("OK: every placement in the live layout and the saved slots points at a badge of the live collection" if ok
          else "FOUND: placements that point at no badge of the live collection (see the table)")
    return 0 if ok else 1


def main(argv=None):
    parser = argparse.ArgumentParser(description=(__doc__ or "Badge sets.").split("\n\n")[0])
    sub = parser.add_subparsers(dest="command", required=True)
    default = setting("BADGES_DATA")
    for name in ("list", "append", "remove-last", "check-slots"):
        p = sub.add_parser(name)
        p.add_argument("--data", default=default, required=default is None, metavar="DIR",
                       help="data dir with manifest.json, undo/, work/ (default: BADGES_DATA)")
        p.add_argument("--in", dest="tree", metavar="TREE",
                       help="save3ds extract of extdata 000014d1 (default: DIR/work/current/000014d1)")
    for name in ("append", "remove-last"):
        sub.choices[name].add_argument("--out", metavar="TREE", help="where the new tree goes (must not exist)")
    sub.choices["remove-last"].add_argument("--allow-home-changes", action="store_true",
                                            help="accept files changed since the append (by HOME): restore only the "
                                                 "set's areas and the case's view")
    sub.choices["remove-last"].add_argument("--undo", metavar="RECORD",
                                            help="the undo record to use (when several fit)")
    sub.choices["append"].add_argument("--name", help="the set's name in HOME (default: the folder name)")
    sub.choices["append"].add_argument("set_dir", metavar="SET_DIR", help="folder of badge PNGs, as for Anemone3DS")
    sub.choices["check-slots"].add_argument("--slots", metavar="TREE",
                                            help="save3ds extract of extdata 0000165c (default: "
                                                 "DIR/work/current/0000165c)")
    for name in ("decrypt", "encrypt"):
        p = sub.add_parser(name)
        p.add_argument("--save3ds", required=True, metavar="BIN", help="save3ds_fuse (the tested build, by sha256)")
        p.add_argument("--boot9", required=True, metavar="FILE", help="the console's boot9.bin")
        p.add_argument("--movable", required=True, metavar="FILE", help="the console's movable.sed")
        p.add_argument("--out", required=name == "decrypt", metavar="DIR", help="new folder for the result")
    sub.choices["decrypt"].add_argument("--sd", required=True, metavar="SDROOT",
                                        help="folder with Nintendo 3DS/<id0>/<id1>/extdata/00000000/000014d1")
    sub.choices["encrypt"].add_argument("--from", dest="source", required=True, metavar="DIR",
                                        help="the decrypt output the tree was made from")
    sub.choices["encrypt"].add_argument("tree", metavar="TREE", help="the tree to write (append or remove-last output)")
    args = parser.parse_args(argv)
    try:
        if args.command in ("decrypt", "encrypt"):
            tool = Save3ds(args.save3ds, args.boot9, args.movable)
            if args.command == "decrypt":
                cmd_decrypt(args.sd, args.out, tool)
            else:
                cmd_encrypt(args.source, args.tree, args.out, tool)
            return 0
        tree = Path(args.tree) if args.tree else Path(args.data) / "work" / "current" / "000014d1"
        if args.command == "list":
            cmd_list(args.data, tree)
        elif args.command == "append":
            cmd_append(args.data, tree, Path(args.out) if args.out else None, args.set_dir, args.name,
                       datetime.date.today().isoformat())
        elif args.command == "remove-last":
            cmd_remove_last(args.data, tree, Path(args.out) if args.out else None, args.allow_home_changes,
                            args.undo)
        else:
            return cmd_check_slots(tree, Path(args.slots) if args.slots else
                                   Path(args.data) / "work" / "current" / "0000165c")
    except BadgeError as e:
        print(f"error: {e}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
