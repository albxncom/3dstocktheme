"""Reads a 3DS start logo back and refuses a spoiled one. Python 3 standard library only.

What is and is not independent of the writer (logofmt.py): no function is shared, but three readers here are
adapted from themeplaza-app (KennLDN/themeplaza-app, commit 6183c8c0, MIT, notice below), the same source logofmt.py's
DARC and CLYT/CLAN writers follow: unlz11() from tools/logo.py lz11_decompress, read_darc() (its folder walk and name
reading) from tools/logo.py darc_list, sections() from tools/logo_build.py _sections. A misreading of the DARC or of
the section header taken over from that source would therefore sit in both and not be caught here. Written here and
independent: the bounds and refusals added to those three, and every other check (names, layouts, materials, panes,
groups, animations, textures, sizes, padding, MAC presence); the writer's LZ11 compressor is not from that source.

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

  python3 -I checklogo.py FILE [FILE ...]              a final logo: LZ11, 0x2000 bytes, DARC + 0x20-byte MAC
  python3 -I checklogo.py --unsigned FILE [FILE ...]   a bare DARC (what the writer makes before signing)
  python3 -I checklogo.py --describe FILE              the structure only (section names, counts, sizes); for
                                                       comparing with a logo a console is known to accept

What is checked (exit 1 and the first fault named, per file):
  LZ11      type byte 0x11, every reference inside what is already written, the stream ends inside the file,
            compressed size at most 0x2000, the final file exactly 0x2000 bytes with only zeros after the stream
  sizes     decompressed = the DARC's own length + 0x20 (the MAC); the MAC is not all zeros
  DARC      magic, byte order, header length 0x1C, version, length, table and data offsets, folder ends, names
            inside the table, every file inside the data, no two files overlapping
  names     blyt/NintendoLogo_U_00.bclyt and _D_00, anim/NintendoLogo_{U,D}_00_SceneOut{A,B,C}.bclan, nothing
            else outside timg/, and only .bclim files in timg/
  CLYT      header, section sizes adding up, known section kinds, lyt1 first with the canvas 400x240 (U) or 320x240
            (D), texture names that exist in timg/, material offsets and sizes, panes of the right size with unique
            names, pas1/pae1 balanced with one root, materials of pictures that exist, groups G_A_00, G_B_00 and
            G_C_00 present and naming panes that exist
  CLAN      header, exactly pat1 then pai1, the animation's name matching its file name, its groups present in the
            layout, frames > 0, every entry naming a pane (or material) of the layout, tags of known kinds, keys
            inside the section and of a known type
  CLIM      footer magic, header and file length, one imag block, a known format and a data length that matches
            the size and format
The MAC itself cannot be checked without the key (keytool.py verify does that).
"""
import struct
import sys

LIMIT = 0x2000
MAC_LEN = 0x20
REVISION = 0x02020000
REQUIRED = {"blyt/NintendoLogo_U_00.bclyt", "blyt/NintendoLogo_D_00.bclyt"} | {
    "anim/NintendoLogo_%s_00_SceneOut%s.bclan" % (s, x) for s in "UD" for x in "ABC"}
GROUPS = ("G_A_00", "G_B_00", "G_C_00")
CANVAS = {"U": (400.0, 240.0), "D": (320.0, 240.0)}
LAYOUT_SECTIONS = {"lyt1", "txl1", "fnl1", "mat1", "pan1", "pic1", "txt1", "wnd1", "bnd1", "pas1", "pae1",
                   "grp1", "grs1", "gre1", "usd1", "cnt1"}
PANES = {"pan1", "pic1", "txt1", "wnd1", "bnd1"}
TAGS = {"CLPA", "CLTS", "CLVI", "CLVC", "CLMC", "CLTP"}
BITS = {0: 8, 1: 8, 2: 8, 3: 16, 4: 16, 5: 16, 6: 24, 7: 16, 8: 16, 9: 32, 10: 4, 11: 8, 12: 4, 13: 4}


class Refused(Exception):
    pass


def need(cond, why, *args):
    if not cond:
        raise Refused(why % args if args else why)


def u32(d, o):
    need(0 <= o and o + 4 <= len(d), "read past the end at 0x%x", o)
    return struct.unpack_from("<I", d, o)[0]


def cstr(raw):
    return raw.split(b"\0")[0].decode("ascii", "replace")


# ---------------------------------------------------------------- LZ11

def unlz11(src):
    """(data, bytes of src used). Refuses anything malformed instead of guessing."""
    need(len(src) >= 4 and src[0] == 0x11, "not LZ11 (type byte 0x%02x)", src[0] if src else -1)
    size, pos = src[1] | src[2] << 8 | src[3] << 16, 4
    if size == 0:
        need(len(src) >= 8, "LZ11 header cut off")
        size, pos = struct.unpack_from("<I", src, 4)[0], 8
    need(0 < size <= 0x100000, "LZ11 size 0x%x out of range", size)
    out = bytearray()
    while len(out) < size:
        need(pos < len(src), "LZ11 stream ends early (0x%x of 0x%x bytes)", len(out), size)
        flags = src[pos]
        pos += 1
        for bit in range(8):
            if len(out) >= size:
                break
            if not flags & (0x80 >> bit):
                need(pos < len(src), "LZ11 stream ends early")
                out.append(src[pos])
                pos += 1
                continue
            need(pos + 2 <= len(src), "LZ11 stream ends early")
            b0, ind = src[pos], src[pos] >> 4
            if ind == 0:
                need(pos + 3 <= len(src), "LZ11 stream ends early")
                n = ((b0 & 0xF) << 4 | src[pos + 1] >> 4) + 0x11
                disp = ((src[pos + 1] & 0xF) << 8 | src[pos + 2]) + 1
                pos += 3
            elif ind == 1:
                need(pos + 4 <= len(src), "LZ11 stream ends early")
                n = ((b0 & 0xF) << 12 | src[pos + 1] << 4 | src[pos + 2] >> 4) + 0x111
                disp = ((src[pos + 2] & 0xF) << 8 | src[pos + 3]) + 1
                pos += 4
            else:
                n = ind + 1
                disp = ((b0 & 0xF) << 8 | src[pos + 1]) + 1
                pos += 2
            need(disp <= len(out), "LZ11 reference 0x%x back at 0x%x points before the start", disp, len(out))
            need(len(out) + n <= size, "LZ11 reference runs past the declared size")
            for _ in range(n):
                out.append(out[-disp])
    return bytes(out), pos


# ---------------------------------------------------------------- DARC

def read_darc(d):
    """{path: data} and the DARC's own length (the bytes after it are the MAC)."""
    need(len(d) >= 0x1C and d[:4] == b"darc", "not a DARC")
    bom, hlen, version, total, toff, tlen, doff = struct.unpack_from("<HHIIIII", d, 4)
    need(bom == 0xFEFF, "DARC byte order 0x%04x", bom)
    need(hlen == 0x1C and toff == 0x1C, "DARC header length 0x%x / table offset 0x%x", hlen, toff)
    need(version == 0x01000000, "DARC version 0x%x", version)
    need(total <= len(d), "DARC says 0x%x bytes, only 0x%x there", total, len(d))
    need(toff + tlen <= doff <= total, "DARC table (0x%x + 0x%x) and data offset 0x%x do not fit", toff, tlen, doff)
    w0, parent, count = struct.unpack_from("<III", d, toff)
    need(w0 == 0x01000000 and parent == 0, "DARC root entry is not a folder")
    need(2 <= count and 12 * count < tlen, "DARC entry count %d does not fit the table", count)
    names_off = toff + 12 * count

    def name(off):
        p, raw = names_off + off, bytearray()
        while True:
            need(p + 2 <= toff + tlen, "DARC name runs out of the table")
            if d[p:p + 2] == b"\0\0":
                return raw.decode("utf-16-le")
            raw += d[p:p + 2]
            p += 2

    files, spans, stack = {}, [], []
    for i in range(count):
        w0, a, b = struct.unpack_from("<III", d, toff + 12 * i)
        need(w0 >> 24 in (0, 1), "DARC entry %d has flags 0x%x", i, w0 >> 24)
        nm = name(w0 & 0xFFFFFF)
        while stack and stack[-1][1] <= i:
            stack.pop()
        if w0 >> 24:
            need(i < b <= count, "DARC folder %r ends at %d (entry %d of %d)", nm, b, i, count)
            need(not stack or b <= stack[-1][1], "DARC folder %r ends after its parent", nm)
            stack.append((nm, b))
            continue
        need(nm and "/" not in nm, "DARC file name %r", nm)
        need(doff <= a and a + b <= total, "DARC file %r (0x%x + 0x%x) outside the data", nm, a, b)
        path = "/".join(s[0] for s in stack if s[0] not in ("", ".")) + "/" + nm
        need(path not in files, "DARC holds %r twice", path)
        files[path] = d[a:a + b]
        spans.append((a, a + b, path))
    spans.sort()
    for (a0, e0, p0), (a1, e1, p1) in zip(spans, spans[1:]):
        need(e0 <= a1, "DARC files %r and %r overlap", p0, p1)
    return files, total


def check_names(files):
    missing = sorted(REQUIRED - set(files))
    need(not missing, "missing: %s", ", ".join(missing))
    for path in files:
        if path in REQUIRED:
            continue
        need(path.startswith("timg/") and path.endswith(".bclim") and path.count("/") == 1,
             "file %r is not one the logo may hold", path)


# ---------------------------------------------------------------- CLYT and CLAN

def sections(d, magic, what):
    need(len(d) >= 0x14 and d[:4] == magic, "%s: not %s", what, magic.decode())
    bom, hlen, rev, size, count = struct.unpack_from("<HHIII", d, 4)
    need(bom == 0xFEFF and hlen == 0x14, "%s: header (byte order 0x%x, length 0x%x)", what, bom, hlen)
    need(rev == REVISION, "%s: revision 0x%x", what, rev)
    need(size == len(d), "%s: says 0x%x bytes, is 0x%x", what, size, len(d))
    out, p = [], hlen
    for i in range(count):
        need(p + 8 <= len(d), "%s: section %d of %d missing", what, i + 1, count)
        kind, n = d[p:p + 4].decode("ascii", "replace"), u32(d, p + 4)
        need(n >= 8 and n % 4 == 0 and p + n <= len(d), "%s: section %s has size 0x%x", what, kind, n)
        out.append((kind, d[p:p + n]))
        p += n
    need(p == len(d), "%s: 0x%x bytes after the last section", what, len(d) - p)
    return out


def check_material(m, ntex, what):
    need(len(m) >= 0x34, "%s: material shorter than 0x34", what)
    flags = u32(m, 0x30)
    nmap, nmtx, ngen, ntev = flags & 3, flags >> 2 & 3, flags >> 4 & 3, flags >> 6 & 7
    fixed = 0x34 + 4 * nmap + 20 * nmtx + 4 * ngen
    need(len(m) >= fixed, "%s: material %r shorter than its maps", what, cstr(m[:0x14]))
    if flags >> 6 == 0:
        need(len(m) == fixed, "%s: material %r is 0x%x bytes, its flags say 0x%x", what, cstr(m[:0x14]), len(m), fixed)
    for k in range(nmap):
        idx = struct.unpack_from("<H", m, 0x34 + 4 * k)[0]
        need(idx < ntex, "%s: material %r uses texture %d of %d", what, cstr(m[:0x14]), idx, ntex)
    return {"maps": nmap, "matrices": nmtx, "gens": ngen, "tev": ntev, "bytes": len(m)}


def check_layout(d, side, textures_present, what):
    secs = sections(d, b"CLYT", what)
    need(secs and secs[0][0] == "lyt1", "%s: does not start with lyt1", what)
    info = {"sections": [(k, len(b)) for k, b in secs], "textures": [], "materials": [], "panes": {},
            "groups": {}}
    depth, roots, last_pane, group_depth = 0, 0, None, 0
    for kind, b in secs:
        need(kind in LAYOUT_SECTIONS, "%s: unknown section %r", what, kind)
        if kind == "lyt1":
            need(len(b) == 0x14, "%s: lyt1 size 0x%x", what, len(b))
            canvas = struct.unpack_from("<2f", b, 0xC)
            need(canvas == CANVAS[side], "%s: canvas %r, not %r", what, canvas, CANVAS[side])
        elif kind == "txl1":
            n = u32(b, 8)
            need(0xC + 4 * n <= len(b), "%s: txl1 offsets past its end", what)
            for i in range(n):
                o = 0xC + u32(b, 0xC + 4 * i)
                need(o < len(b) and b"\0" in b[o:], "%s: txl1 name %d outside the section", what, i)
                t = cstr(b[o:])
                need(t in textures_present, "%s: texture %r is not in timg/", what, t)
                info["textures"].append(t)
        elif kind == "mat1":
            n = u32(b, 8)
            offs = [u32(b, 0xC + 4 * i) for i in range(n)] + [len(b)]
            need(n == 0 or offs[0] == 0xC + 4 * n, "%s: first material not after the offset table", what)
            for i in range(n):
                need(offs[i] < offs[i + 1] <= len(b), "%s: material %d offsets out of order", what, i)
                info["materials"].append(check_material(b[offs[i]:offs[i + 1]], len(info["textures"]), what))
        elif kind in PANES:
            need(len(b) >= 0x4C, "%s: %s shorter than a pane", what, kind)
            name = cstr(b[0xC:0x1C])
            need(name, "%s: pane without a name", what)
            need(name not in info["panes"], "%s: two panes named %r", what, name)
            if kind == "pan1":
                need(len(b) == 0x4C, "%s: pan1 %r size 0x%x", what, name, len(b))
            elif kind == "pic1":
                need(len(b) >= 0x60, "%s: pic1 %r too short", what, name)
                mat, ncoord = struct.unpack_from("<HH", b, 0x5C)
                need(len(b) == 0x60 + 0x20 * ncoord, "%s: pic1 %r size 0x%x for %d coordinate sets",
                     what, name, len(b), ncoord)
                need(mat < len(info["materials"]), "%s: pic1 %r uses material %d of %d",
                     what, name, mat, len(info["materials"]))
            if depth == 0:
                roots += 1
                need(roots == 1, "%s: more than one root pane", what)
            info["panes"][name] = kind
            last_pane = name
        elif kind == "pas1":
            need(len(b) == 8 and last_pane, "%s: pas1 without a pane before it", what)
            depth += 1
        elif kind == "pae1":
            need(len(b) == 8 and depth > 0, "%s: pae1 without pas1", what)
            depth -= 1
        elif kind == "grp1":
            need(len(b) >= 0x1C, "%s: grp1 too short", what)
            gname, n = cstr(b[8:0x18]), u32(b, 0x18)
            need(len(b) == 0x1C + 0x10 * n, "%s: group %r size 0x%x for %d panes", what, gname, len(b), n)
            refs = [cstr(b[0x1C + 0x10 * i:0x2C + 0x10 * i]) for i in range(n)]
            for r in refs:
                need(r in info["panes"], "%s: group %r names pane %r, which is not there", what, gname, r)
            need(gname not in info["groups"], "%s: two groups named %r", what, gname)
            info["groups"][gname] = refs
        elif kind == "grs1":
            group_depth += 1
        elif kind == "gre1":
            need(group_depth > 0, "%s: gre1 without grs1", what)
            group_depth -= 1
    need(depth == 0, "%s: %d pas1 without pae1", what, depth)
    need(group_depth == 0, "%s: grs1 without gre1", what)
    need(roots == 1, "%s: no root pane", what)
    for g in GROUPS:
        need(g in info["groups"], "%s: group %s missing", what, g)
        need(info["groups"][g], "%s: group %s names no pane", what, g)
    return info


def check_animation(d, letter, lay, what):
    secs = sections(d, b"CLAN", what)
    need([k for k, _ in secs] == ["pat1", "pai1"], "%s: sections %r, not pat1 then pai1", what, [k for k, _ in secs])
    pat, pai = secs[0][1], secs[1][1]
    need(len(pat) >= 0x1C, "%s: pat1 too short", what)
    order, ngroups, name_off, groups_off, start, end = struct.unpack_from("<HHIIhh", pat, 8)
    need(name_off < groups_off and groups_off + 0x14 * ngroups <= len(pat), "%s: pat1 offsets", what)
    name = cstr(pat[name_off:groups_off])
    need(name == "SceneOut" + letter, "%s: animation named %r", what, name)
    need(ngroups >= 1, "%s: animation binds no group", what)
    groups = [cstr(pat[groups_off + 0x14 * i:groups_off + 0x14 * i + 0x10]) for i in range(ngroups)]
    for g in groups:
        need(g in lay["groups"], "%s: binds group %r, not in the layout", what, g)
    need(start <= end, "%s: start %d after end %d", what, start, end)
    need(len(pai) >= 0x14, "%s: pai1 too short", what)
    frames, loop, _, ntex, nent, ent_off = struct.unpack_from("<HBBHHI", pai, 8)
    need(frames > 0, "%s: no frames", what)
    need(ent_off >= 0x14 + 4 * ntex and ent_off + 4 * nent <= len(pai), "%s: pai1 entry table", what)
    keys_total = 0
    for i in range(nent):
        e = u32(pai, ent_off + 4 * i)
        need(e + 0x18 <= len(pai), "%s: entry %d outside pai1", what, i)
        target, ntags, kind = cstr(pai[e:e + 0x14]), pai[e + 0x14], pai[e + 0x15]
        need(kind in (0, 1), "%s: entry %r kind %d", what, target, kind)
        if kind == 0:
            need(target in lay["panes"], "%s: animates pane %r, which is not in the layout", what, target)
        for t in range(ntags):
            to = e + u32(pai, e + 0x18 + 4 * t)
            need(to + 8 <= len(pai), "%s: tag outside pai1", what)
            magic = pai[to:to + 4].decode("ascii", "replace")
            need(magic in TAGS, "%s: unknown tag %r", what, magic)
            for k in range(pai[to + 4]):
                ko = to + u32(pai, to + 8 + 4 * k)
                need(ko + 0xC <= len(pai), "%s: target outside pai1", what)
                _, _, dtype, nkeys, _, koff = struct.unpack_from("<BBHHHI", pai, ko)
                need(dtype in (1, 2) and nkeys > 0, "%s: %s keys of type %d, %d of them", what, magic, dtype, nkeys)
                need(ko + koff + nkeys * (12 if dtype == 2 else 8) <= len(pai), "%s: keys outside pai1", what)
                keys_total += nkeys
    return {"sections": [(k, len(b)) for k, b in secs], "frames": frames, "loop": loop, "entries": nent,
            "keys": keys_total, "groups": groups, "start": start, "end": end, "order": order}


def check_clim(d, what):
    need(len(d) >= 0x28 and d[-0x28:-0x24] == b"CLIM", "%s: no CLIM footer", what)
    f = d[-0x28:]
    bom, hlen, rev, size, blocks = struct.unpack_from("<HHIII", f, 4)
    need(bom == 0xFEFF and hlen == 0x14 and blocks == 1, "%s: CLIM header", what)
    need(rev == REVISION, "%s: CLIM revision 0x%x", what, rev)
    need(size == len(d), "%s: CLIM says 0x%x bytes, is 0x%x", what, size, len(d))
    magic, isize, w, h, fmt, dsize = struct.unpack_from("<4sIHHII", f, 0x14)
    need(magic == b"imag" and isize == 0x10, "%s: no imag block", what)
    need(fmt in BITS, "%s: unknown format %d", what, fmt)
    need(dsize == len(d) - 0x28, "%s: data length 0x%x, 0x%x there", what, dsize, len(d) - 0x28)
    p2 = lambda v: 1 << max(3, (v - 1).bit_length())
    need(w > 0 and h > 0 and dsize * 8 in (w * h * BITS[fmt], p2(w) * p2(h) * BITS[fmt]),
         "%s: %dx%d in format %d does not take 0x%x bytes", what, w, h, fmt, dsize)
    return {"size": (w, h), "format": fmt, "bytes": len(d)}


# ---------------------------------------------------------------- the whole logo

def check_darc(darc_bytes, after):
    """after: the bytes the decompressed data holds past the DARC (the MAC), or None for a bare DARC."""
    files, total = read_darc(darc_bytes)
    if after is None:
        need(total == len(darc_bytes), "0x%x bytes after the DARC in an unsigned file", len(darc_bytes) - total)
    check_names(files)
    present = {p[5:] for p in files if p.startswith("timg/")}
    report = {"files": {p: len(v) for p, v in files.items()}, "darc": total}
    for p, data in sorted(files.items()):
        if p.startswith("timg/"):
            report[p] = check_clim(data, p)
    for side in "UD":
        p = "blyt/NintendoLogo_%s_00.bclyt" % side
        lay = check_layout(files[p], side, present, p)
        report[p] = lay
        for letter in "ABC":
            q = "anim/NintendoLogo_%s_00_SceneOut%s.bclan" % (side, letter)
            report[q] = check_animation(files[q], letter, lay, q)
    return report


def check(raw, unsigned=False):
    if unsigned:
        return check_darc(raw, None)
    if len(raw) > LIMIT:
        _, used = unlz11(raw)
        need(used <= LIMIT, "compressed stream is 0x%x bytes, over the 0x%x limit", used, LIMIT)
    need(len(raw) == LIMIT, "file is 0x%x bytes, not 0x%x", len(raw), LIMIT)
    data, used = unlz11(raw)
    need(used <= LIMIT, "compressed stream is 0x%x bytes", used)
    need(not any(raw[used:]), "non-zero bytes after the compressed stream (from 0x%x)", used)
    need(len(data) > MAC_LEN, "decompressed data too short")
    total = struct.unpack_from("<I", data, 0xC)[0] if len(data) >= 0x10 else -1
    need(total + MAC_LEN == len(data), "decompressed 0x%x bytes, the DARC is 0x%x: not DARC + 0x20", len(data), total)
    need(any(data[total:]), "the 0x20 bytes after the DARC are all zero: not signed")
    report = check_darc(data[:total], data[total:])
    report["compressed"] = used
    return report


def describe(report):
    lines = ["DARC 0x%x bytes, %d files%s" % (report["darc"], len(report["files"]),
             ", compressed 0x%x" % report["compressed"] if "compressed" in report else "")]
    for p in sorted(report["files"]):
        r = report.get(p, {})
        if p.endswith(".bclyt"):
            kinds = {}
            for k, _ in r["sections"]:
                kinds[k] = kinds.get(k, 0) + 1
            lines.append("%s 0x%x: sections %s; %d textures, %d materials %r, %d panes, groups %s"
                         % (p, report["files"][p], " ".join("%s x%d" % kv for kv in kinds.items()),
                            len(r["textures"]), len(r["materials"]),
                            [(m["maps"], m["tev"], m["bytes"]) for m in r["materials"]], len(r["panes"]),
                            {g: len(v) for g, v in r["groups"].items()}))
        elif p.endswith(".bclan"):
            lines.append("%s 0x%x: %s; %d frames, loop %d, order %d, timeline %d..%d, %d entries, %d keys, groups %s"
                         % (p, report["files"][p], " ".join("%s 0x%x" % s for s in r["sections"]), r["frames"],
                            r["loop"], r["order"], r["start"], r["end"], r["entries"], r["keys"], r["groups"]))
        elif p.endswith(".bclim"):
            lines.append("%s 0x%x: %dx%d format %d" % (p, report["files"][p], r["size"][0], r["size"][1], r["format"]))
    return lines


def main(argv):
    args = argv[1:]
    unsigned = describe_only = False
    while args and args[0].startswith("--"):
        flag = args.pop(0)
        if flag == "--unsigned":
            unsigned = True
        elif flag == "--describe":
            describe_only = True
        else:
            args = []
    if not args:
        print(next(x for x in __doc__.split("\n\n") if x.startswith("  python3")))
        return 2
    bad = 0
    for path in args:
        with open(path, "rb") as f:
            raw = f.read()
        if describe_only and not unsigned:
            unsigned = raw[:4] == b"darc"
        try:
            report = check(raw, unsigned)
        except (Refused, struct.error, UnicodeDecodeError, IndexError, ValueError) as e:
            print("REFUSED %s: %s" % (path, e))
            bad += 1
            continue
        if describe_only:
            print("\n".join(describe(report)))
        else:
            print("ok %s: %s" % (path, "unsigned DARC 0x%x bytes" % report["darc"] if unsigned else
                                 "compressed 0x%x of 0x%x bytes, DARC 0x%x + MAC" % (report["compressed"], LIMIT,
                                                                                      report["darc"])))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
