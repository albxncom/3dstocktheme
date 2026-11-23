"""compare_records.py A B: two folders record.sh wrote. The draw lists byte for byte (each that differs is named by
its look, scheme, walk and state, with its first differing line: a texture's pixels (X) or the call's number among
the calls, and both lines), the art hashes (art.txt) and the test counts (tests.txt), line by line; a record found
on one side only is a difference. source.txt and summary.txt are not compared, nor the second form (named/,
sprites/: compare_named.py), whose files are only counted, so records with it compare against records without it. Prints the first LIMIT differing
draw lists in full (env LIMIT, default 20) and then the count per look, scheme and walk; exit 1 if anything
differs, 2 if a folder holds no records."""
import collections
import os
import pathlib
import sys

NOT_COMPARED = {"source.txt", "summary.txt", "coverage.txt"}
SECOND_FORM = ("named/", "sprites/")     # compare_named.py's: not counted here, only their number said
a, b = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])


def records(root):
    return {p.relative_to(root).as_posix() for p in root.rglob("*") if p.is_file()
            and p.relative_to(root).as_posix() not in NOT_COMPARED
            and not p.relative_to(root).as_posix().startswith(SECOND_FORM)}


second = [sum(1 for p in r.rglob("*") if p.is_file() and p.relative_to(r).as_posix().startswith(SECOND_FORM))
          for r in (a, b)]


ra, rb = records(a), records(b)
if not any(r.endswith(".dl") for r in ra) or not any(r.endswith(".dl") for r in rb):
    print("no draw lists in", a if not any(r.endswith(".dl") for r in ra) else b)
    sys.exit(2)
limit = int(os.environ.get("LIMIT", "20"))


def where(rel):
    """lists/<look>-s<scheme>/<walk>/<tag>-<state>.dl: look, scheme, walk, state"""
    parts = rel.split("/")
    if len(parts) == 4 and parts[0] == "lists":
        look, _, scheme = parts[1].rpartition("-s")
        return "look %s scheme %s %s %s" % (look, scheme, parts[2], parts[3].split("-", 1)[-1][:-3])
    return rel


def cut(s):
    return s if len(s) <= 150 else s[:147] + "..."


n = shown = 0
per = collections.Counter()
for rel in sorted(ra ^ rb):
    print(where(rel), "only in", a if rel in ra else b)
    n += 1
for rel in sorted(ra & rb):
    x, y = (a / rel).read_bytes(), (b / rel).read_bytes()
    if x == y:
        continue
    n += 1
    if not rel.endswith(".dl"):
        lx, ly = x.decode().splitlines(), y.decode().splitlines()
        print(rel, "differs:")
        for line in sorted(set(lx) - set(ly)):
            print("  A:", cut(line))
        for line in sorted(set(ly) - set(lx)):
            print("  B:", cut(line))
        if lx != ly and set(lx) == set(ly):
            print("  the same lines in another order")
        continue
    per[rel.rsplit("/", 1)[0]] += 1
    if shown >= limit:
        continue
    shown += 1
    lx, ly = x.decode().split("\n"), y.decode().split("\n")
    calls = 0
    for k in range(max(len(lx), len(ly))):
        p, q = lx[k] if k < len(lx) else None, ly[k] if k < len(ly) else None
        if p is not None and not p.startswith("X"):
            calls += 1
        if p == q:
            continue
        what = ("texture %s" % (p or q).split()[1]) if (p or q or "").startswith("X") else "call %d" % calls
        print("%s: %s (line %d) differs" % (where(rel), what, k + 1))
        print("  A:", cut(p) if p is not None else "(the list ends)")
        print("  B:", cut(q) if q is not None else "(the list ends)")
        break
if sum(per.values()) > shown:
    print("draw lists that differ, per look, scheme and walk:")
    for k in sorted(per):
        print("  %s: %d" % (where(k + "/x-x.dl").rsplit(" ", 1)[0], per[k]))
if any(second):
    print("files of the second form (named/, sprites/), not compared here (compare_named.py): %d and %d" % tuple(second))
print("%d of %d records differ" % (n, len(ra | rb)))
sys.exit(1 if n else 0)
