#!/usr/bin/env python3
"""Writers for the files of a 3DS start logo: layout (CLYT), layout animation (CLAN), picture (CLIM, 4-bit alpha),
the DARC archive, LZ11 compression and the 0x2000-byte padding. Python 3 standard library only.

Nothing here reads another logo. The byte layouts follow 3dbrew's DARC, CLYT, CLAN, CLIM and LZ11 pages and
themeplaza-app's tools/logo_build.py and tools/logo_scene.py (KennLDN/themeplaza-app, commit 6183c8c0, MIT, notice
below): the code is written anew here, the field order and the A/B/C timeline are taken from there.

    MIT License

    Copyright (c) 2026 Matt Kenny

    Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
    documentation files (the "Software"), to deal in the Software without restriction, including without limitation
    the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and
    to permit persons to whom the Software is furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in all copies or substantial portions
    of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
    THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
    CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
    IN THE SOFTWARE.
"""
import struct

REVISION = 0x02020000          # CLYT, CLAN and CLIM revision (the one makerom's Homebrew logo carries too)
LIMIT = 0x2000                 # the compressed logo, padded, is exactly this long
MAC_LEN = 0x20                 # HMAC-SHA256 after the DARC, inside the compressed data
A4 = 13                        # CLIM format number of 4-bit alpha


def fixed(text, n):
    raw = text.encode("ascii")
    if len(raw) >= n:
        raise ValueError("name %r does not fit %d bytes" % (text, n))
    return raw + bytes(n - len(raw))


def pad4(raw):
    return raw + bytes(-len(raw) % 4)


def nw_file(magic, sections):
    """A CLYT or CLAN file: 0x14-byte header, then (name, body) sections, each with its 8-byte head."""
    body = b"".join(name.encode("ascii") + struct.pack("<I", 8 + len(data)) + data for name, data in sections)
    return magic + struct.pack("<HHIII", 0xFEFF, 0x14, REVISION, 0x14 + len(body), len(sections)) + body


# ---------------------------------------------------------------- layout

def pane(kind, name, x=0.0, y=0.0, w=32.0, h=32.0, flags=1, alpha=255, children=(), **pic):
    """A pane: 'pan1' (draws nothing) or 'pic1' (a rectangle with one material; pic needs 'material',
    optionally 'corners' (4 RGBA vertex colours, top left, top right, bottom left, bottom right))."""
    p = {"kind": kind, "name": name, "x": float(x), "y": float(y), "w": float(w), "h": float(h), "flags": flags,
         "alpha": alpha, "children": list(children)}
    p.update(pic)
    return p


def _pane_bytes(p):
    # flags (bit 0 visible, bit 1 alpha passed to children), origin 4 = the pane's middle, alpha, padding
    raw = bytes([p["flags"], 4, p["alpha"], 0]) + fixed(p["name"], 0x10) + bytes(8)
    raw += struct.pack("<10f", p["x"], p["y"], 0.0, 0.0, 0.0, 0.0, 1.0, 1.0, p["w"], p["h"])
    if p["kind"] == "pic1":
        corners = p.get("corners", [b"\xff\xff\xff\xff"] * 4)
        raw += b"".join(corners) + struct.pack("<HH", p["material"], 1)
        raw += struct.pack("<8f", 0.0, 0.0, 1.0, 0.0, 0.0, 1.0, 1.0, 1.0)
    return raw


def material(name, texture, colour):
    """One texture shown through its alpha in one colour: the material's 'black' colour carries the RGB (an alpha
    texture reads as RGB 0), its 'white' colour the full alpha; no TEV stages, no blend or alpha-compare block."""
    return {"name": name, "texture": texture, "colour": bytes(colour)}


def _material_bytes(m):
    flags = 1 | 1 << 2 | 1 << 4                       # one texture map, one texture matrix, one coordinate generator
    raw = fixed(m["name"], 0x14) + m["colour"] + b"\x00" + b"\xff" * 4 * 6 + struct.pack("<I", flags)
    raw += struct.pack("<HBB", m["texture"], 0 | 1 << 2, 0 | 1 << 2)     # clamp, linear, clamp, linear
    raw += struct.pack("<5f", 0.0, 0.0, 0.0, 1.0, 1.0)
    return raw + bytes(4)                                                # 2x4 matrix from texture coordinates 0


def layout(width, textures, materials, root, groups):
    """root: the RootPane; groups: {'G_A_00': [pane names], ...} under a RootGroup."""
    sections = [("lyt1", struct.pack("<I2f", 1, float(width), 240.0))]
    names, offsets = b"", []
    for t in textures:
        offsets.append(4 * len(textures) + len(names))
        names += t.encode("ascii") + b"\0"
    sections.append(("txl1", struct.pack("<I", len(textures)) + b"".join(struct.pack("<I", o) for o in offsets)
                     + pad4(names)))
    mats = [_material_bytes(m) for m in materials]
    pos, table = 8 + 4 + 4 * len(mats), b""
    for m in mats:
        table += struct.pack("<I", pos)
        pos += len(m)
    sections.append(("mat1", struct.pack("<I", len(mats)) + table + b"".join(mats)))

    def add(p):
        sections.append((p["kind"], _pane_bytes(p)))
        if p["children"]:
            sections.append(("pas1", b""))
            for c in p["children"]:
                add(c)
            sections.append(("pae1", b""))
    add(root)

    def group(name, panes):
        return ("grp1", fixed(name, 0x10) + struct.pack("<I", len(panes)) + b"".join(fixed(n, 0x10) for n in panes))
    sections.append(group("RootGroup", []))
    sections.append(("grs1", b""))
    for name, panes in groups.items():
        sections.append(group(name, panes))
    sections.append(("gre1", b""))
    return nw_file(b"CLYT", sections)


# ---------------------------------------------------------------- animation

ALPHA = ("CLVC", 16)           # vertex colour tag, target 16: the pane's own alpha


def animation(which, lengths, tracks):
    """One of SceneOutA/B/C. lengths: {'A': n, 'B': n, 'C': n} frames; tracks: [(pane, (tag, target),
    [(frame, value, slope), ...])], hermite keys. On the authoring timeline B comes first, then A, then C."""
    order = "ABC".index(which)
    start = {"A": lengths["B"] + 1, "B": 0, "C": lengths["B"] + 1 + lengths["A"]}[which]
    end = start + lengths[which] - (0 if which == "B" else 1)
    name = pad4(("SceneOut" + which).encode("ascii") + b"\0")
    name += bytes(max(0, 12 - len(name)))
    group = "G_%s_00" % which
    pat = struct.pack("<HHIIhhB3x", order, 1, 0x1C, 0x1C + len(name), start, end, 1) + name + fixed(group, 0x14)

    entries = []
    for pane_name in dict.fromkeys(t[0] for t in tracks):
        tags = []
        for tag in ("CLPA", "CLVI", "CLVC"):
            mine = sorted((t for t in tracks if t[0] == pane_name and t[1][0] == tag), key=lambda t: t[1][1])
            if not mine:
                continue
            targets = []
            for _, (_, target), keys in mine:
                targets.append(struct.pack("<BBHHHI", 0, target, 2, len(keys), 0, 0xC)
                               + b"".join(struct.pack("<3f", *map(float, k)) for k in keys))
            pos, table = 8 + 4 * len(targets), b""
            for t in targets:
                table += struct.pack("<I", pos)
                pos += len(t)
            tags.append(tag.encode("ascii") + struct.pack("<B3x", len(targets)) + table + b"".join(targets))
        pos, table = 0x18 + 4 * len(tags), b""
        for t in tags:
            table += struct.pack("<I", pos)
            pos += len(t)
        entries.append(fixed(pane_name, 0x14) + struct.pack("<BBH", len(tags), 0, 0) + table + b"".join(tags))
    pos, table = 0x14 + 4 * len(entries), b""
    for e in entries:
        table += struct.pack("<I", pos)
        pos += len(e)
    pai = struct.pack("<HBBHHI", lengths[which], 1 if which == "B" else 0, 0, 0, len(entries), 0x14)
    return nw_file(b"CLAN", [("pat1", pat), ("pai1", pai + table + b"".join(entries))])


def linear(points):
    """Hermite keys whose slopes join (frame, value) points with straight lines."""
    keys = []
    for i, (f, v) in enumerate(points):
        if i + 1 < len(points):
            s = (points[i + 1][1] - v) / (points[i + 1][0] - f)
        elif i:
            s = (v - points[i - 1][1]) / (f - points[i - 1][0])
        else:
            s = 0.0
        keys.append((f, v, s))
    return keys


# ---------------------------------------------------------------- picture

def tiled_index(x, y, width):
    """Place of pixel (x, y) in the GPU's order: 8x8 tiles in rows, Morton order inside a tile."""
    m = 0
    for i in range(3):
        m |= ((x >> i) & 1) << (2 * i) | ((y >> i) & 1) << (2 * i + 1)
    return ((y >> 3) * (width >> 3) + (x >> 3)) * 64 + m


def clim_a4(width, height, alpha):
    """A BCLIM in 4-bit alpha from alpha(x, y) -> 0..15, row 0 at the top. Sides: powers of two, 8 or more."""
    for side in (width, height):
        if side < 8 or side & (side - 1):
            raise ValueError("%dx%d: sides must be powers of two, 8 or more" % (width, height))
    data = bytearray(width * height // 2)
    for y in range(height):
        for x in range(width):
            v = alpha(x, y)
            if v:
                i = tiled_index(x, y, width)
                data[i // 2] |= (v & 15) << (4 * (i & 1))
    foot = b"CLIM" + struct.pack("<HHIII", 0xFEFF, 0x14, REVISION, len(data) + 0x28, 1)
    foot += b"imag" + struct.pack("<IHHII", 0x10, width, height, A4, len(data))
    return bytes(data) + foot


# ---------------------------------------------------------------- archive

def darc(files):
    """files: {(folder, name): data}. Folders and files sorted by name; data at multiples of 0x20, pictures 0x80."""
    folders = sorted({f for f, _ in files})
    entries = [("", None, None), (".", None, None)]
    for folder in folders:
        entries.append((folder, None, None))
        entries += [(n, files[(folder, n)], folder) for f, n in sorted(files) if f == folder]
    names, name_off = b"", []
    for n, _, _ in entries:
        name_off.append(len(names))
        names += n.encode("utf-16-le") + b"\0\0"
    table_len = 12 * len(entries) + len(names)
    data_off = (0x1C + table_len + 0x1F) // 0x20 * 0x20
    table, blob, pos = b"", b"", data_off
    for i, (n, data, _) in enumerate(entries):
        if data is None:
            if i < 2:
                parent, end = 0, len(entries)
            else:
                parent, end = 1, i + 1
                while end < len(entries) and entries[end][1] is not None:
                    end += 1
            table += struct.pack("<III", name_off[i] | 0x01000000, parent, end)
        else:
            align = 0x80 if n.endswith(".bclim") else 0x20
            start = (pos + align - 1) // align * align
            blob += bytes(start - pos) + data
            pos = start + len(data)
            table += struct.pack("<III", name_off[i], start, len(data))
    head = b"darc" + struct.pack("<HHIIIII", 0xFEFF, 0x1C, 0x01000000, pos, 0x1C, table_len, data_off)
    return head + table + names + bytes(data_off - 0x1C - table_len) + blob


# ---------------------------------------------------------------- LZ11

def lz11(data):
    """LZ11 with matches 3 to 0x10110 bytes long up to 0x1000 back, greedy with a one-step look-ahead; the output
    is padded to a multiple of 4. Deterministic: the same input always gives the same bytes."""
    n = len(data)
    if n >= 1 << 24:
        raise ValueError("too long for a 3-byte LZ11 size")
    heads = {}

    def longest(p):
        best_len = best_back = 0
        for q in reversed(heads.get(data[p:p + 3], ())[-64:]):
            if p - q > 0x1000:
                break
            m = 0
            while m < 0x10110 and p + m < n and data[q + m] == data[p + m]:
                m += 1
            if m > best_len:
                best_len, best_back = m, p - q
        return best_len, best_back

    def remember(p):
        if p + 3 <= n:
            heads.setdefault(data[p:p + 3], []).append(p)

    out = bytearray([0x11, n & 0xFF, n >> 8 & 0xFF, n >> 16 & 0xFF])
    p = 0
    while p < n:
        flag_at = len(out)
        out.append(0)
        for bit in range(8):
            if p >= n:
                break
            m, back = longest(p) if p + 3 <= n else (0, 0)
            if m >= 3 and p + 4 <= n:
                remember(p)
                m2, _ = longest(p + 1)
                heads[data[p:p + 3]].pop()
                if m2 > m + 1:
                    m = 0                                # a literal now buys a longer match next
            if m >= 3:
                out[flag_at] |= 0x80 >> bit
                d = back - 1
                if m <= 0x10:
                    out += bytes([(m - 1) << 4 | d >> 8, d & 0xFF])
                elif m <= 0x110:
                    k = m - 0x11
                    out += bytes([k >> 4, (k & 0xF) << 4 | d >> 8, d & 0xFF])
                else:
                    k = m - 0x111
                    out += bytes([0x10 | k >> 12, k >> 4 & 0xFF, (k & 0xF) << 4 | d >> 8, d & 0xFF])
                for q in range(p, p + m):
                    remember(q)
                p += m
            else:
                out.append(data[p])
                remember(p)
                p += 1
    return bytes(out + bytes(-len(out) % 4))


def pack(signed):
    """The final logo file from the DARC with its MAC appended: LZ11, then zeros up to exactly 0x2000 bytes."""
    packed = lz11(signed)
    if len(packed) > LIMIT:
        raise ValueError("compressed logo is 0x%x bytes, over the 0x%x limit" % (len(packed), LIMIT))
    return packed + bytes(LIMIT - len(packed))
