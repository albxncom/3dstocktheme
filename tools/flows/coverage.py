"""coverage.py TREE WORK: what the records of record.sh (run with COVERAGE=1) execute of the drawing code. WORK is
its OUT.work: the drivers WORK/<walk>-drv/driver and their profiles WORK/prof/<walk>-*.profraw. Per file of the
drawing code (FILES, and the drawing functions of main.c, MAIN), the functions and lines executed of all, by the two
walks alone and with the sheet; then by name each function no walk executes, and each that stays unexecuted with
the sheet too. A function counts as its lines from its first to the next function's."""
import pathlib
import re
import subprocess
import sys

FILES = ["uikit/source/ui.c", "app/ui/app_ui.c", "uikit/include/widget.h", "uikit/source/look.c",
         "uikit/source/look_stock.c", "uikit/source/look_frost.c", "uikit/source/look_night.c",
         "uikit/source/look_prism.c", "uikit/source/hits.c", "uikit/source/wrap.c", "uikit/source/scroll.c",
         "uikit/source/touch.c", "uikit/source/popup.c", "app/ui/pour.c", "app/source/progress.c"]
# main.c's generic drawing: the cursor, the scroll bar and the loader (C201)
MAIN = ("app/source/main.c", ["draw_cursor", "draw_scrollbar", "draw_row_cursor", "job_loader", "loader_now",
                              "job_top", "job_bottom", "job_line", "job_notice", "end_stays"])

tree, work = pathlib.Path(sys.argv[1]).resolve(), pathlib.Path(sys.argv[2]).resolve()


def run(*args):
    return subprocess.run(["xcrun", *map(str, args)], check=True, capture_output=True, text=True).stdout


def lcov(walk):
    """The lcov export of one driver with its own profiles (one binary each: the drivers hold functions of one name
    that differ, such as main): per file {first line: (name, count)} and {line: count}."""
    raws = sorted((work / "prof").glob("%s-*.profraw" % walk))
    if not raws:
        raise SystemExit("coverage.py: no profiles of %s in %s" % (walk, work / "prof"))
    data = work / ("%s.profdata" % walk)
    run("llvm-profdata", "merge", "-o", data, *raws)
    text = run("llvm-cov", "export", "-format=lcov", "-instr-profile", data, work / ("%s-drv" % walk) / "driver")
    fns, lines, names, path = {}, {}, {}, None
    for row in text.splitlines():
        if row.startswith("SF:"):
            path = pathlib.Path(row[3:]).resolve()
            path = path.relative_to(tree).as_posix() if path.is_relative_to(tree) else str(path)
            fns.setdefault(path, {})
            lines.setdefault(path, {})
            names = {}
        elif row.startswith("FN:"):
            at, raw = row[3:].split(",", 1)
            names[raw] = int(at)
            fns[path][int(at)] = (re.sub(r"^.*[:;]", "", raw), 0)
        elif row.startswith("FNDA:"):
            count, raw = row[5:].split(",", 1)
            name, old = fns[path][names[raw]]
            fns[path][names[raw]] = (name, old + int(count))
        elif row.startswith("DA:"):
            at, count = row[3:].split(",")[:2]
            lines[path][int(at)] = lines[path].get(int(at), 0) + int(count)
    return fns, lines


def merged(walks):
    """The drivers' counts added up, by file and line."""
    fns, lines = {}, {}
    for walk in walks:
        f, l = lcov(walk)
        for path in f:
            for at, (name, count) in f[path].items():
                fns.setdefault(path, {})[at] = (name, fns.get(path, {}).get(at, (name, 0))[1] + count)
        for path in l:
            for at, count in l[path].items():
                lines.setdefault(path, {})[at] = lines.get(path, {}).get(at, 0) + count
    return fns, lines


def tally(fns, lines, path, only=None):
    """(functions executed, functions, lines executed, lines, [(name, line) unexecuted]) of path, or of the
    functions named in only."""
    f, l = fns.get(path, {}), lines.get(path, {})
    starts = sorted(f)
    fe = ft = le = lt = 0
    missed = []
    for k, at in enumerate(starts):
        name, count = f[at]
        if only is not None and name not in only:
            continue
        end = starts[k + 1] if k + 1 < len(starts) else 1 << 30
        ft += 1
        fe += count > 0
        if not count:
            missed.append((name, at))
        for ln in l:
            if at <= ln < end:
                lt += 1
                le += l[ln] > 0
    return fe, ft, le, lt, missed


walks = merged(["designs", "badges"])
every = merged(["designs", "badges", "sheet"])
rows = [(p, p, None) for p in FILES] + [(MAIN[0] + " (drawing)", MAIN[0], set(MAIN[1]))]
print("%-34s %21s   %21s" % ("executed of all", "the walks", "the walks and the sheet"))
print("%-34s %10s %10s   %10s %10s" % ("", "functions", "lines", "functions", "lines"))
total = [0] * 8
for label, path, only in rows:
    a, b = tally(*walks, path, only), tally(*every, path, only)
    if not b[1]:
        print("%-34s not compiled into the drivers" % label)
        continue
    for k, v in enumerate(a[:4] + b[:4]):
        total[k] += v
    print("%-34s %10s %10s   %10s %10s" % (label, "%d/%d" % a[:2], "%d/%d" % a[2:4], "%d/%d" % b[:2],
                                            "%d/%d" % b[2:4]))
print("%-34s %10s %10s   %10s %10s" % ("all", "%d/%d" % tuple(total[0:2]), "%d/%d" % tuple(total[2:4]),
                                       "%d/%d" % tuple(total[4:6]), "%d/%d" % tuple(total[6:8])))
for title, cov, other in (("executed by no walk (* : by the sheet)", walks, every),
                          ("unexecuted with the sheet too", every, None)):
    print("\n" + title + ":")
    for label, path, only in rows:
        missed = tally(*cov, path, only)[4]
        if other:
            done = {at for name, at in missed} - {at for name, at in tally(*other, path, only)[4]}
            missed = [(n + (" *" if at in done else ""), at) for n, at in missed]
        if missed:
            print("  %s: %s" % (label, ", ".join("%s (%d)" % m for m in missed)))
