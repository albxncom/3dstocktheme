"""texture_changes.py A [B]: how often a frame changes texture between two calls, from the first form's draw lists
(lists/<look>-s<scheme>/<walk>/*.dl, one frame each) of a folder record.sh wrote. The calls that read a texture are
taken in their order: an image call reads the texture of its number, a text call the font (one texture here: the
recorder does not know the font's sheets); a clear or a rectangle reads none and is skipped. A change is two such
calls in a row that read different textures, which on the console ends a batch of citro2d's. What it does not see:
a texture drawn once on whole pixels and once smoothed (atlas and atlas_smooth share a number), the font's sheets,
the targets (a change of screen is counted as any other). With A alone: the sum over every list, the lists, the
largest of a single list. With B too: both sums, both largest, and the LIMIT lists with the largest rise from A to
B (env LIMIT, default 10); a list on one side only is named. Exit 0, 2 if a folder holds no first form."""
import os
import pathlib
import sys


def changes(path):
    last, count = None, 0
    for line in path.read_text(encoding="utf-8").split("\n"):
        p = line.split(" ", 3)
        tex = p[2] if p[0] == "I" and len(p) > 2 else "font" if p[0] == "T" else None
        if tex is None:
            continue
        count += last is not None and tex != last
        last = tex
    return count


def where(rel):
    """<look>-s<scheme>/<walk>/<tag>-<state>.dl: look, scheme, walk, state"""
    parts = rel.split("/")
    look, _, scheme = parts[0].rpartition("-s")
    return "look %s scheme %s %s %s" % (look, scheme, parts[1], parts[2].split("-", 1)[-1][:-3])


def folder(root):
    lists = root / "lists"
    out = {p.relative_to(lists).as_posix(): changes(p) for p in lists.rglob("*.dl")}
    if not out:
        print("no draw lists of the first form in", root)
        sys.exit(2)
    return out


if not 2 <= len(sys.argv) <= 3:
    sys.exit(__doc__)
limit = int(os.environ.get("LIMIT", "10"))
sides = [folder(pathlib.Path(p)) for p in sys.argv[1:]]
for name, side in zip("AB", sides):
    top = max(side, key=lambda k: (side[k], k))
    print("%s: %d texture changes in %d lists; the largest %d, %s" % (name, sum(side.values()), len(side), side[top],
                                                                         where(top)))
if len(sides) == 2:
    a, b = sides
    for rel in sorted(set(a) ^ set(b)):
        print(where(rel), "only in", "A" if rel in a else "B")
    both = sorted(set(a) & set(b), key=lambda k: (-(b[k] - a[k]), k))
    print("rise from A to B: %d in the lists of both; the %d largest:" % (sum(b[k] - a[k] for k in both), limit))
    for rel in both[:limit]:
        print("  %s: %d to %d (%+d)" % (where(rel), a[rel], b[rel], b[rel] - a[rel]))
