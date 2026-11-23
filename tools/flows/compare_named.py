"""compare_named.py [--calls] A B: the second comparison of two folders record.sh wrote, the one that does not rest
on the atlas's places. The draw lists of the second form (named/) as two sequences each: the calls (every line but
the texture events: clears, rectangles, images, text) line for line, and the texture events (E) on their own, so an
event added or missing does not shift the calls. A list whose calls differ is named by its look, scheme, walk and
state with its first differing call (its number among the calls, the sprite's name on each side) and both lines;
then, over every differing call of every list, the sprites whose calls differ and how often. The texture events are
matched in order (difflib) after the textures are renumbered past the application's part's (an event that ends in a
word "part-..." is the part's, rec.c): what is on one side only is counted by kind (create, delete, flush,
cacheflush, with the part's texture's word) and the first one shown. A texture the kit made before it was the
part's (RENAMED: its pictures, the house's texture until S3-4a, made at the same place) shifts no number, and an
event of it that differs only by its word (and a create by its size before it) is renamed, not changed: counted on
its own. The sprite lists (sprites/<set>.txt) by look,
scheme and name: a name on one side only is listed, never hidden, and so is a name whose size or pixels differ.
tests.txt line by line as compare_records.py does. art.txt and lists/ are the first form's (compare_records.py),
source.txt and summary.txt are not compared. Prints the first LIMIT differing lists and sprites (env LIMIT, default
20) and the counts; exit 1 if anything differs, with --calls only if a call line, a list, a sprite or tests.txt
does (texture events alone exit 0); exit 2 if a folder holds no second form."""
import collections
import difflib
import os
import pathlib
import sys

args = sys.argv[1:]
calls_only = args[:1] == ["--calls"]
if calls_only:
    args = args[1:]
if len(args) != 2:
    sys.exit(__doc__)
a, b = pathlib.Path(args[0]), pathlib.Path(args[1])
limit = int(os.environ.get("LIMIT", "20"))


def files(root, top):
    return {p.relative_to(root).as_posix() for p in (root / top).rglob("*") if p.is_file()}


def cut(s):
    return s if len(s) <= 150 else s[:147] + "..."


def where(rel):
    """named/<look>-s<scheme>/<walk>/<tag>-<state>.dl: look, scheme, walk, state"""
    parts = rel.split("/")
    look, _, scheme = parts[1].rpartition("-s")
    return "look %s scheme %s %s %s" % (look, scheme, parts[2], parts[3].split("-", 1)[-1][:-3])


def sprite(line):
    """an image call's sprite name (second field after the target), else None"""
    p = line.split(" ") if line else []
    return p[2] if len(p) > 2 and p[0] == "I" else None


RENAMED = ("part-pictures",)


def part_word(line):
    """the part's texture's word an event ends in (rec.c), else None"""
    word = line.rsplit(" ", 1)[-1]
    return word if word.startswith("part-") else None


def unnamed(line):
    """an event of a RENAMED texture without its word (and a create without its size before it)"""
    if part_word(line) not in RENAMED:
        return line
    p = line.split(" ")[:-1]
    return " ".join(p[:-1] if len(p) > 4 and p[4] == "create" else p)


def event_kind(line):
    p = line.split(" ")
    kind = p[4] if len(p) > 4 and p[1] != "lost" else "lost"
    return kind + (" " + part_word(line) if part_word(line) else "")


def part_lives(root, rels):
    """per walk's folder (one run of a driver: the textures' numbers count from 0) the numbers of the part's"""
    out = collections.defaultdict(set)
    for rel in rels:
        for line in (root / rel).read_text(encoding="utf-8").split("\n"):
            if line.startswith("E ") and part_word(line) not in (None,) + RENAMED and line.split(" ")[1] != "lost":
                out[rel.rsplit("/", 1)[0]].add(int(line.split(" ")[5]))
    return out


def renumbered(line, lives):
    """an event of a texture not the part's with its number as if the part's textures had not been made"""
    p = line.split(" ")
    if (part_word(line) and part_word(line) not in RENAMED) or len(p) < 6 or p[1] == "lost" or int(p[5]) < 0:
        return line
    p[5] = str(int(p[5]) - sum(1 for k in lives if k < int(p[5])))
    return " ".join(p)


la, lb = files(a, "named"), files(b, "named")
if not la or not lb:
    print("no draw lists of the second form in", a if not la else b)
    sys.exit(2)
lives_a, lives_b = part_lives(a, la), part_lives(b, lb)
n = n_calls = shown = 0
call_lines = event_lines = event_lists = renamed = 0
per = collections.Counter()
by_sprite = collections.Counter()
lists_by_sprite = collections.defaultdict(set)
by_kind = collections.Counter()
for rel in sorted(la ^ lb):
    print(where(rel), "only in", a if rel in la else b)
    n += 1
    n_calls += 1
for rel in sorted(la & lb):
    x, y = (a / rel).read_bytes(), (b / rel).read_bytes()
    if x == y:
        continue
    n += 1
    walk = rel.rsplit("/", 1)[0]
    lx, ly = x.decode().split("\n"), y.decode().split("\n")
    cx, cy = [s for s in lx if not s.startswith("E")], [s for s in ly if not s.startswith("E")]
    ex = [renumbered(s, lives_a[walk]) for s in lx if s.startswith("E")]
    ey = [renumbered(s, lives_b[walk]) for s in ly if s.startswith("E")]
    first, count = None, 0
    for k in range(max(len(cx), len(cy))):
        p, q = cx[k] if k < len(cx) else None, cy[k] if k < len(cy) else None
        if p == q:
            continue
        count += 1
        for name in {sprite(p), sprite(q)} - {None}:
            by_sprite[name.split("+")[0]] += 1
            lists_by_sprite[name.split("+")[0]].add(rel)
        if first is None:
            first = (k, p, q)
    first_event, events = None, 0
    for tag, i0, i1, j0, j1 in difflib.SequenceMatcher(None, [unnamed(s) for s in ex], [unnamed(s) for s in ey],
                                                       autojunk=False).get_opcodes():
        if tag == "equal":
            renamed += sum(1 for s, t in zip(ex[i0:i1], ey[j0:j1]) if s != t)
            continue
        for side, lines in (("A", ex[i0:i1]), ("B", ey[j0:j1])):
            for s in lines:
                by_kind[side, event_kind(s)] += 1
        events += (i1 - i0) + (j1 - j0)
        if first_event is None:
            first_event = (ex[i0] if i1 > i0 else None, ey[j0] if j1 > j0 else None)
    call_lines, event_lines = call_lines + count, event_lines + events
    event_lists += events > 0
    if count:
        n_calls += 1
        per[walk] += 1
    if shown >= limit or (calls_only and not count):
        continue
    shown += 1
    if first:
        k, p, q = first
        names = " ".join("%s: %s" % (s, sprite(v)) for s, v in (("A", p), ("B", q)) if sprite(v))
        print("%s: call %d differs%s" % (where(rel), k + 1, " (sprite %s)" % names if names else ""))
        print("  A:", cut(p) if p is not None else "(the calls end)")
        print("  B:", cut(q) if q is not None else "(the calls end)")
        if len(cx) != len(cy):
            print("  the lists have %d and %d calls: the calls after an inserted or missing one are compared shifted"
                  % (len(cx), len(cy)))
    if first_event:
        print("%s: %d texture events differ, the first:" % (where(rel), events))
        print("  A:", cut(first_event[0]) if first_event[0] is not None else "(none)")
        print("  B:", cut(first_event[1]) if first_event[1] is not None else "(none)")
if sum(per.values()) > shown:
    print("draw lists whose calls differ, per look, scheme and walk:")
    for k in sorted(per):
        print("  %s: %d" % (where(k + "/x-x.dl").rsplit(" ", 1)[0], per[k]))
if by_sprite:
    print("differing calls per sprite (lines, lists):")
    for name, count in by_sprite.most_common(limit):
        print("  %s: %d in %d" % (name, count, len(lists_by_sprite[name])))
    if len(by_sprite) > limit:
        print("  and %d sprites more" % (len(by_sprite) - limit))
print("call lines that differ: %d in %d lists" % (call_lines, sum(per.values())))
print("texture-event lines that differ: %d in %d lists" % (event_lines, event_lists))
print("texture-event lines renamed (%s): %d" % (", ".join(RENAMED), renamed))
for (side, kind), count in sorted(by_kind.items()):
    print("  only in %s: %s %d" % (side, kind, count))
moved = sum(1 for rel in lb for line in (b / rel).read_text(encoding="utf-8").split("\n")
            if line.startswith("E") and renumbered(line, lives_b[rel.rsplit("/", 1)[0]]) != line)
if any(lives_a.values()) or any(lives_b.values()):
    print("texture events renumbered past the part's textures: %d in B" % moved)


def sprite_list(path):
    """<look> <scheme> <name> <the rest>: {(look, scheme, name): rest}"""
    out = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        look, scheme, name, rest = (line.split(" ", 3) + [""])[:4]
        out[look, scheme, name] = rest
    return out


sa, sb = files(a, "sprites"), files(b, "sprites")
for rel in sorted(sa ^ sb):
    print(rel, "only in", a if rel in sa else b)
    n += 1
    n_calls += 1
for rel in sorted(sa & sb):
    x, y = sprite_list(a / rel), sprite_list(b / rel)
    if x == y:
        continue
    n += 1
    n_calls += 1
    keys = sorted(set(x) | set(y))
    diff = [k for k in keys if x.get(k) != y.get(k)]
    print("%s: %d sprites differ" % (rel, len(diff)))
    for k in diff[:limit]:
        print("  %s %s %s: A %s, B %s" % (k + (x.get(k, "(missing)"), y.get(k, "(missing)"))))
    by_name = collections.Counter(k[2] for k in diff)
    if len(diff) > limit:
        print("  by name (looks and schemes):", ", ".join("%s %d" % c for c in by_name.most_common(limit)))

for rel in ("tests.txt",):
    pa, pb = a / rel, b / rel
    if pa.is_file() != pb.is_file():
        print(rel, "only in", a if pa.is_file() else b)
        n += 1
        n_calls += 1
    elif pa.is_file() and pa.read_bytes() != pb.read_bytes():
        n += 1
        n_calls += 1
        lx, ly = pa.read_text().splitlines(), pb.read_text().splitlines()
        print(rel, "differs:")
        for line in sorted(set(lx) - set(ly)):
            print("  A:", cut(line))
        for line in sorted(set(ly) - set(lx)):
            print("  B:", cut(line))
        if lx != ly and set(lx) == set(ly):
            print("  the same lines in another order")
total = len(la | lb) + len(sa | sb) + 1
print("%d of %d records of the second form differ, %d in a call line, a sprite or a test" % (n, total, n_calls))
sys.exit(1 if (n_calls if calls_only else n) else 0)
