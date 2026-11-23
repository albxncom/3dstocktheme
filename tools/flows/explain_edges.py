"""explain_edges.py A B: whether every line in which the second-form lists (named/) of two folders record.sh wrote
differ is explained by a sprite that stood at an edge of its texture in A's packing and stands at none in B's.
Such a line is an image call that names the same sprite on both sides, takes in the texel past a side of its region
(ring L T R B) on which its sprite touched the texture's edge in A, and differs from its counterpart in the hash of
its texels only: at the texture's edge the GPU clamps and reads the sprite's own edge again, inside the texture the
transparent gap. Every other differing line, a list on one side only and a list of another length is unexplained.
The places come from each folder's generated art (<folder>.work/art, the set with the most looks; the tables of a
look are the same in every set, the textures' sizes must be too): the atlas's and the glass sheet's sprites per
look (the kit's and, where the set has one, the application's part's: part_art.h, part_art.c), the selection's
corners once. Prints the explained lines per look and sprite with the side, the number of
lists and the screens (walk and state, the first LIMIT, env, default 8), then the first LIMIT unexplained lines with
where they are; exit 0 if every differing line is explained, 1 if any is not, 2 if a folder lacks what it needs."""
import collections
import os
import pathlib
import re
import sys

SIDES = ("left", "top", "right", "bottom")
a, b = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
limit = int(os.environ.get("LIMIT", "8"))


def fail(why):
    print(why)
    sys.exit(2)


def sizes(folder):
    h = (folder / "ui_art.h").read_text(encoding="utf-8")
    got = {}
    for n in ("ATLAS", "GLASS_SHEET", "CORNERS"):
        w, t = (re.search(r"#define ART_%s_%s (\d+)" % (n, k), h) for k in "WH")
        if not (w and t):
            fail("%s: ART_%s_W or _H not found" % (folder, n))
        got[n] = int(w.group(1)), int(t.group(1))
    return h, got


def places(text, table, ids, size, where):
    body = re.search(r"%s\[[A-Z_ -]+\] = \{\n(.*?)\n\};" % table, text, re.S)
    if not body:
        fail("%s: table %s not found" % (where, table))
    rows = [tuple(map(int, re.findall(r"\d+", line)[:4])) for line in body.group(1).split("\n")]
    if len(rows) != len(ids):
        fail("%s: %d names, %d places in %s" % (where, len(ids), len(rows), table))
    return {n: (r, size) for n, r in zip(ids, rows)}


def tables(root):
    """(look or None, name) -> ((x, y, w, h), (texture w, h)) from root's generated art"""
    art = pathlib.Path(str(root) + ".work") / "art"
    if not art.is_dir():
        fail("%s: no generated art (%s)" % (root, art))
    sets = sorted(p for p in art.iterdir() if (p / "ui_art.h").is_file())
    if not sets:
        fail("%s: no art set" % art)
    if len({tuple(sorted(sizes(p)[1].items())) for p in sets}) != 1:
        fail("%s: the textures' sizes differ between the sets" % art)
    full = max(sets, key=lambda p: len(p.name.split("-")))
    h, size = sizes(full)

    def enum(name, last):
        body = re.search(r"enum %s \{\n(.*?)%s\n" % (name, last), h, re.S)
        if not body:
            fail("%s: enum %s not found" % (full, name))
        return re.findall(r"^    ART_(\w+)", body.group(1), re.M)

    ids, glass, corners = enum("art_id", "    ART_COUNT"), enum("art_glass_id", "    ART_ALL"), \
        enum("art_corner_id", "    ART_CORNER_COUNT")
    out = {(None, n): v for n, v in places((full / "ui_art.c").read_text(encoding="utf-8"), "ART_CORNER_SPRITES",
                                           corners, size["CORNERS"], full).items()}
    for f in sorted(full.glob("art_*.c")):
        look, text = f.stem[4:], f.read_text(encoding="utf-8")
        for table, names, s in (("art_%s_sprites" % look, ids, size["ATLAS"]),
                                ("art_%s_sheet_sprites" % look, glass, size["GLASS_SHEET"])):
            out.update({(look, n): v for n, v in places(text, table, names, s, f).items()})
    if (full / "part_art.h").is_file():
        part_h, part_c = (full / "part_art.h").read_text(encoding="utf-8"), (full / "part_art.c").read_text(
            encoding="utf-8")
        part = re.findall(r"^    ART_(\w+)", part_h[part_h.index("enum part_art_id {"):part_h.index("};")], re.M)
        app_n = int(re.search(r"^#define ART_APP_COUNT (\d+)$", part_h, re.M).group(1))
        # the part's own textures' sizes (S3-2d: ART_APP_PART's last four numbers), else the kit's atlas and sheet
        own = re.search(r"ART_APP_PART = \{art_part_looks, \d+, (\d+), (\d+), (\d+), (\d+)[,}]", part_c)
        atlas, sheet = (tuple(map(int, own.groups()[:2])), tuple(map(int, own.groups()[2:]))) if own else \
            (size["ATLAS"], size["GLASS_SHEET"])
        for f in sorted(full.glob("art_*.c")):
            look = f.stem[4:]
            for table, names, s in (("art_part_%s_sprites" % look, part[:app_n], atlas),
                                    ("art_part_%s_sheet_sprites" % look, part[app_n:], sheet)):
                out.update({(look, n): v for n, v in places(part_c, table, names, s, full / "part_art.c").items()})
    return out


def edges(t, look, name):
    """the sides on which the sprite touches its texture's edge, or None for a name without pixels in a table"""
    got = t.get((look, name)) or t.get((None, name))
    if not got or not got[0][2] or not got[0][3]:
        return None
    (x, y, w, h), (tw, th) = got
    return x == 0, y == 0, x + w == tw, y + h == th


def where(rel):
    """named/<look>-s<scheme>/<walk>/<tag>-<state>.dl: look, scheme, walk, state"""
    parts = rel.split("/")
    look, _, scheme = parts[1].rpartition("-s")
    return look, scheme, "%s %s" % (parts[2], parts[3].split("-", 1)[-1][:-3])


def why_not(s, t, ta, tb, look):
    """None if the differing pair s, t is explained, else why not; with the sides it is explained by"""
    f, g = s.split(" "), t.split(" ")
    if f[0] != "I" or g[0] != "I" or "ring" not in f or "ring" not in g:
        return "not an image call with a ring", None
    if f[2] != g[2]:
        return "another sprite", None
    k = f.index("ring")
    if f[:k + 5] != g[:k + 5] or f[k + 6:] != g[k + 6:]:
        return "differs beyond its texels' hash", None
    name = f[2].split("+")[0]
    ea, eb = edges(ta, look, name), edges(tb, look, name)
    if ea is None or eb is None:
        return "no place in a table: " + name, None
    if any(eb):
        return "%s stands at an edge in B" % name, None
    sides = [side for side, ring, at in zip(SIDES, f[k + 1:k + 5], ea) if ring == "1" and at]
    if not sides:
        return "%s is at no edge on a side the call takes in" % name, None
    return None, " ".join(sides)


def main():
    ta, tb = tables(a), tables(b)
    la = {p.relative_to(a).as_posix() for p in (a / "named").rglob("*.dl")}
    lb = {p.relative_to(b).as_posix() for p in (b / "named").rglob("*.dl")}
    if not la or not lb:
        fail("no second form in %s" % (a if not la else b))
    explained = collections.defaultdict(lambda: [0, set()])     # (look, sprite, sides) -> lines, screens
    lists = collections.defaultdict(set)
    other = []
    for rel in sorted(la - lb):
        other.append("%s: in A only" % rel)
    for rel in sorted(lb - la):
        other.append("%s: in B only" % rel)
    total = 0
    for rel in sorted(la & lb):
        x = (a / rel).read_text(encoding="utf-8").split("\n")
        y = (b / rel).read_text(encoding="utf-8").split("\n")
        look, scheme, screen = where(rel)
        if len(x) != len(y):
            other.append("%s: %d lines in A, %d in B" % (rel, len(x), len(y)))
            continue
        for n, (s, t) in enumerate(zip(x, y), 1):
            if s == t:
                continue
            total += 1
            why, sides = why_not(s, t, ta, tb, look)
            if why:
                other.append("%s line %d: %s\n  A %s\n  B %s" % (rel, n, why, s[:150], t[:150]))
                continue
            key = look, s.split(" ")[2].split("+")[0], sides
            explained[key][0] += 1
            explained[key][1].add("s%s %s" % (scheme, screen))
            lists[key].add(rel)
    for (look, name, sides), (count, screens) in sorted(explained.items()):
        shown = sorted(screens)
        print("explained: look %s %s (at the %s edge in A): %d lines in %d lists; %s%s"
              % (look, name, sides, count, len(lists[look, name, sides]), ", ".join(shown[:limit]),
                 " and %d more" % (len(shown) - limit) if len(shown) > limit else ""))
    for line in other[:limit]:
        print("UNEXPLAINED", line)
    print("%d differing lines, %d explained by a sprite off a texture's edge (%d sprites of looks), %d unexplained"
          % (total, sum(v[0] for v in explained.values()), len(explained), len(other)))
    return 1 if other else 0


if __name__ == "__main__":
    sys.exit(main())
