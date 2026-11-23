"""Every sprite lies inside its texture (SR-TD6): in each generated art folder named on the command line
(build/host/art), for every look of looks_built.h, every place of the look's atlas table (art_<look>_sprites, in
the ART_ATLAS_W x ART_ATLAS_H atlas) and of its sheet table (art_<look>_sheet_sprites, in the glass sheet), the same
of the application's part (part_art.c, art_part_<look>_sprites and art_part_<look>_sheet_sprites, in the part's own
atlas and sheet at ART_APP_PART's sizes), and every place of the corners' table (ART_CORNER_SPRITES in ui_art.c),
has x + w within the texture's width and y + h within its height; no texture is taller than 1024. The tables are the same in every scheme (art.py scheme_parts),
so this holds for every scheme. On the tile line at 3b73342 POUR_MELT (15 frames of 40 px side by side) lay at
0, 978 600 px wide in the 512 wide atlas; its last frames' pixels were written 8 rows lower over other sprites and
drawn from past the texture's edge, and no host test saw it.

And no sprite touches an edge of its texture: each place of a sprite with pixels keeps BORDER texels to all four
edges (art.py's BORDER, S3-2a). A linear filter reaches one texel past a sprite; at the texture's edge the GPU
clamps and reads the sprite's own edge again, inside it reads the transparent gap, so a sprite at an edge would be
drawn differently from the same sprite packed elsewhere.

And every texture of sprites holds its own sprites alone (S3-2d): in every scheme's blob of the kit's atlas and sheet
and of the part's atlas and sheet (art_<look>.c, part_art_<look>.c) every texel outside the places of that texture's
table is transparent (all four bytes 0), so an image packed into the wrong texture fails here.

Run: python3 -I host/test_atlas.py build/host/art      (make test runs it)
"""
import re
import sys
import zlib
from pathlib import Path

MAX_H = 1024
BORDER = 1


def names(header, enum, drop):
    body = re.search(r"enum %s \{(.*?)\};" % enum, header, re.S)
    assert body, "enum %s not found" % enum
    return [n for n in re.findall(r"^\s*(ART_\w+)", body.group(1), re.M) if n not in drop]


def places(source, table, where):
    rows = re.search(r"const art_sprite %s\[[^]]*\] = \{\n(.*?)\n\};" % table, source, re.S)
    assert rows, "%s: table %s not found" % (where, table)
    return [tuple(map(int, m)) for m in re.findall(r"^\s*\{(\d+), (\d+), (\d+), (\d+), \{", rows.group(1), re.M)]


def blobs(source, table, where):
    """per scheme the inflated blob of a table of blobs (static or not) in a generated file: {name: bytes} once"""
    row = re.search(r"const uint8_t \*const %s\[ART_SCHEMES\] = \{([^}]*)\};" % table, source)
    assert row, "%s: blob table %s not found" % (where, table)
    out = {}
    for name in dict.fromkeys(n.strip() for n in row.group(1).split(",")):
        body = re.search(r"static const uint8_t %s\[\] = \{\n(.*?)\n\};" % name, source, re.S)
        assert body, "%s: blob %s not found" % (where, name)
        out[name] = zlib.decompress(bytes(int(b, 16) for b in re.findall(r"0x([0-9a-f]{2})", body.group(1))))
    return out


def gpu_index(x, y, width):
    return ((((y >> 3) * (width >> 3) + (x >> 3)) << 6) | (x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2)
            | ((x & 4) << 2) | ((y & 4) << 3))


def stray(data, at, tw, th):
    """how many texels of a texture tw x th (RGBA8 in the GPU's order) outside every place in at are not 0"""
    if len(data) != tw * th * 4:
        return tw * th
    outside = bytearray(b"\xff" * (tw * th * 4))
    for x0, y0, w, h in at:
        for y in range(y0, min(y0 + h, th)):
            for x in range(x0, min(x0 + w, tw)):
                k = 4 * gpu_index(x, y, tw)
                outside[k:k + 4] = b"\0\0\0\0"
    left = int.from_bytes(data, "big") & int.from_bytes(outside, "big")
    return 0 if not left else sum(1 for k in range(0, len(data), 4) if any(data[k:k + 4]) and outside[k])


def check(folder):
    header = (folder / "ui_art.h").read_text(encoding="utf-8")
    defines = {k: int(v) for k, v in re.findall(r"^#define\s+(\w+)\s+(\d+)\s*$", header, re.M)}
    looks = re.findall(r"X\((\w+), \w+\)", (folder / "looks_built.h").read_text(encoding="utf-8"))
    assert looks, "%s: no look in looks_built.h" % folder
    atlas = names(header, "art_id", ("ART_COUNT",))
    sheet = names(header, "art_glass_id", ("ART_ALL",))
    corner = names(header, "art_corner_id", ("ART_CORNER_COUNT",))
    part_h = (folder / "part_art.h").read_text(encoding="utf-8")
    part = names(part_h, "part_art_id", ())
    app_n = int(re.search(r"^#define ART_APP_COUNT (\d+)$", part_h, re.M).group(1))
    part_c = (folder / "part_art.c").read_text(encoding="utf-8")
    size = re.search(r"ART_APP_PART = \{art_part_looks, \d+, (\d+), (\d+), (\d+), (\d+), \d+, \d+\};", part_c)
    assert size, "part_art.c: ART_APP_PART's sizes not found"
    for k, n in enumerate(("PART_ATLAS_W", "PART_ATLAS_H", "PART_SHEET_W", "PART_SHEET_H")):
        defines[n] = int(size.group(k + 1))
    tables = [("corners", "ART_CORNERS", corner, places((folder / "ui_art.c").read_text(encoding="utf-8"),
                                                        "ART_CORNER_SPRITES", "ui_art.c"), {})]
    for look in looks:
        source = (folder / ("art_%s.c" % look)).read_text(encoding="utf-8")
        own = (folder / ("part_art_%s.c" % look)).read_text(encoding="utf-8")
        up = look.upper()
        tables.append((look, "ART_ATLAS", atlas, places(source, "art_%s_sprites" % look, look),
                       blobs(source, "art_%s_atlas_z" % look, look)))
        tables.append((look, "ART_GLASS_SHEET", sheet, places(source, "art_%s_sheet_sprites" % look, look),
                       blobs(source, "art_%s_sheet_z" % look, look)))
        tables.append((look, "PART_ATLAS", part[:app_n], places(part_c, "art_part_%s_sprites" % look, look),
                       blobs(own, "ART_PART_%s_ATLAS_Z" % up, look) if app_n else {}))
        tables.append((look, "PART_SHEET", part[app_n:], places(part_c, "art_part_%s_sheet_sprites" % look, look),
                       blobs(own, "ART_PART_%s_SHEET_Z" % up, look) if len(part) > app_n else {}))
    bad, edge, count, held, rows = [], [], 0, 0, {}
    alone, scanned = [], 0
    for texture in ("ART_ATLAS", "ART_GLASS_SHEET", "ART_CORNERS", "PART_ATLAS", "PART_SHEET"):
        if defines[texture + "_H"] > MAX_H:
            bad.append("%s is %d rows high, more than %d" % (texture, defines[texture + "_H"], MAX_H))
    for owner, texture, ids, at, inflated in tables:
        assert len(ids) == len(at), "%s: %d names, %d places in %s" % (owner, len(ids), len(at), texture)
        tw, th = defines[texture + "_W"], defines[texture + "_H"]
        for name, (x, y, w, h) in zip(ids, at):
            count += 1
            if w > tw or x + w > tw or y + h > th:
                bad.append("%s %s at %d, %d size %d x %d passes its texture (%s %d x %d)"
                           % (owner, name[4:], x, y, w, h, texture, tw, th))
            if w and h:
                held += 1
                if min(x, y, tw - x - w, th - y - h) < BORDER:
                    edge.append("%s %s at %d, %d size %d x %d is within %d texel of an edge of %s %d x %d"
                                % (owner, name[4:], x, y, w, h, BORDER, texture, tw, th))
        if texture == "ART_ATLAS":
            rows[owner] = max([rows.get(owner, 0)] + [y + h for x, y, w, h in at])
        for name, data in inflated.items():
            scanned += 1
            n = stray(data, [(x, y, w, h) for x, y, w, h in at if w and h], tw, th)
            if n:
                alone.append("%s %s: %d texels outside the places of its table are not transparent (%s %d x %d)"
                             % (owner, name, n, texture, tw, th))
    print("     atlas rows used: %s of %d" % (", ".join("%s %d" % kv for kv in rows.items()), defines["ART_ATLAS_H"]))
    return count, held, len(looks), bad, edge, alone, scanned


def main():
    total, drawn, looks, bad, edge, alone, blobs_seen = 0, 0, 0, [], [], [], 0
    for arg in sys.argv[1:]:
        count, held, many, wrong, near, stray_ones, scanned = check(Path(arg))
        total += count
        drawn += held
        looks += many
        bad += wrong
        edge += near
        alone += stray_ones
        blobs_seen += scanned
    for line in bad + edge + alone:
        print("FAIL", line)
    print("%s test_atlas: %d sprites of %d looks in %d builds, %d wrong (outside a texture or too high)"
          % ("ok  " if total and not bad else "FAIL", total, looks, len(sys.argv) - 1, len(bad)))
    print("%s test_atlas border: %d sprites with pixels, %d within %d texel of an edge of their texture"
          % ("ok  " if drawn and not edge else "FAIL", drawn, len(edge), BORDER))
    print("%s test_atlas alone: %d blobs of the kit's and the part's atlases and sheets, %d with a texel outside "
          "their table's places" % ("ok  " if blobs_seen and not alone else "FAIL", blobs_seen, len(alone)))
    print("%d failed" % (len(bad) + len(edge) + len(alone) + (not total) + (not drawn) + (not blobs_seen)))
    return 1 if bad or edge or alone or not total or not drawn or not blobs_seen else 0


if __name__ == "__main__":
    sys.exit(main())
