"""Theme zips for tests and dry runs, written from the format documentation only (no Nintendo data, no theme art).

Each zip holds body_LZ.bin (LZ11), info.smdh (name, description, author, a solid-colour icon) and, for one fixture,
a bgm.bcstm stand-in. The body is a solid-colour theme: top screen draw type 1 with one colour, bottom screen draw
type 1 with the outer-area and icon-area colour blocks set, every other block off, no sound effects. Byte 0x5 (use
bgm.bcstm) stays 0 in every fixture: the stand-in is not real audio and HOME must not try to play it.

Run: python3 -I fixtures.py DIR   (writes DIR/themes/fixture-red.zip and DIR/themes/fixture-blue.zip)
"""
import struct
import sys
import zipfile
from pathlib import Path

FIXTURES = [
    # file, name, description, top colour, bottom colour, icon colour, with the bgm.bcstm stand-in
    ("fixture-red.zip", "Fixture Red", "Solid red test theme (theme_pack fixture)",
     (0xC0, 0x10, 0x10), (0x60, 0x08, 0x08), (0xE0, 0x20, 0x20), False),
    ("fixture-blue.zip", "Fixture Blue", "Solid blue test theme with a BGM stand-in (theme_pack fixture)",
     (0x10, 0x30, 0xC0), (0x08, 0x18, 0x60), (0x20, 0x40, 0xE0), True),
]


def lz11(data):
    """LZ11 (header 0x11 + 24-bit size; flag byte per 8 tokens, MSB first; 1 = back-reference), greedy matching."""
    if len(data) >= 1 << 24:
        raise ValueError("fixture too large for a 24-bit LZ11 header")
    out = bytearray(b"\x11" + len(data).to_bytes(3, "little"))
    pos = 0
    while pos < len(data):
        flag_at = len(out)
        out.append(0)
        for bit in range(8):
            if pos >= len(data):
                break
            best_len, best_disp = 0, 0
            for disp in range(1, min(pos, 0x1000) + 1):
                length = 0
                while length < 0x10110 and pos + length < len(data) and data[pos + length] == data[pos + length - disp]:
                    length += 1
                if length > best_len:
                    best_len, best_disp = length, disp
            if best_len < 3:
                out.append(data[pos])
                pos += 1
                continue
            out[flag_at] |= 0x80 >> bit
            d = best_disp - 1
            if best_len <= 0x10:
                out += bytes([(best_len - 1) << 4 | d >> 8, d & 0xFF])
            elif best_len <= 0x110:
                n = best_len - 0x11
                out += bytes([n >> 4, (n & 0xF) << 4 | d >> 8, d & 0xFF])
            else:
                n = best_len - 0x111
                out += bytes([0x10 | n >> 12, n >> 4 & 0xFF, (n & 0xF) << 4 | d >> 8, d & 0xFF])
            pos += best_len
    return bytes(out)


def body(top, bottom):
    """Decompressed body_LZ.bin: header 0xC4 bytes padded to 0xD0, then the colour blocks, size aligned to 0x10."""
    head = bytearray(0xD0)
    blocks = bytearray()

    def block(data):
        offset = 0xD0 + len(blocks)
        blocks.extend(data)
        blocks.extend(bytes(-len(blocks) % 0x10))
        return offset

    dark = tuple(c // 2 for c in bottom)
    struct.pack_into("<I", head, 0x00, 1)                    # version
    struct.pack_into("<I", head, 0x0C, 1)                    # top screen: solid colour
    # 5 bytes: colour, gradient (0xFF = full fade to white; read as alpha by the other description), squares
    struct.pack_into("<I", head, 0x14, block(bytes(top) + b"\xff\x00"))
    struct.pack_into("<I", head, 0x20, 1)                    # bottom screen: solid colour
    struct.pack_into("<I", head, 0x80, 1)                    # icon area: shadow, background, border, glow (RGBA)
    struct.pack_into("<I", head, 0x84, block(bytes(dark) + bytes(bottom) + bytes(dark) + bytes(dark) + b"\xff"))
    struct.pack_into("<I", head, 0x88, 1)                    # outer area: stripes, background, edge glow
    struct.pack_into("<I", head, 0x8C, block(bytes(dark) + bytes(bottom) + bytes(dark)))
    return bytes(head + blocks)


def rgb565(rgb, count):
    r, g, b = rgb
    return struct.pack("<H", (r >> 3) << 11 | (g >> 2) << 5 | b >> 3) * count


def smdh(name, description, author, icon_rgb):
    """0x36C0 bytes: magic, 16 title slots (UTF-16LE short 0x80, long 0x100, publisher 0x80), settings, icons."""
    out = bytearray(0x36C0)
    out[0:4] = b"SMDH"
    for i in range(16):
        base = 0x8 + 0x200 * i
        for offset, size, text in ((0, 0x80, name), (0x80, 0x100, description), (0x180, 0x80, author)):
            out[base + offset:base + offset + size] = text.encode("utf-16-le")[:size - 2].ljust(size, b"\0")
    struct.pack_into("<I", out, 0x2018, 0x7FFFFFFF)
    out[0x2040:0x24C0] = rgb565(icon_rgb, 24 * 24)
    out[0x24C0:0x36C0] = rgb565(icon_rgb, 48 * 48)
    return bytes(out)


def bgm_stand_in():
    """Not audio: a CSTM magic and zeros, only to exercise the bgm.bcstm path and size checks."""
    return b"CSTM\xff\xfe\x40\x00" + bytes(0x1000 - 8)


def theme_zip(path, name, description, top, bottom, icon_rgb, with_bgm, author="theme_pack fixtures"):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("body_LZ.bin", lz11(body(top, bottom)))
        z.writestr("info.smdh", smdh(name, description, author, icon_rgb))
        if with_bgm:
            z.writestr("bgm.bcstm", bgm_stand_in())
    return path


def write_all(data_dir):
    themes = Path(data_dir) / "themes"
    return [theme_zip(themes / f, n, d, top, bottom, icon, bgm) for f, n, d, top, bottom, icon, bgm in FIXTURES]


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("Run: python3 -I fixtures.py DIR")
    for p in write_all(sys.argv[1]):
        print(p)
