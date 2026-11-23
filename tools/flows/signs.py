"""signs.py WALK_DIR ART_DIR LOOK: the rule of the signs over a walk's draw lists (rec.c's .dl): for every problem, refusal
and empty state (captures dX-*, bX-*, bD-02 to bD-06) the signs drawn from an atlas on the top screen's
left eye (target 0) and on the bottom screen (target 2), each sprite named by the look's sprite table
(ART_DIR/art_<LOOK>.c, the names in ART_DIR/ui_art.h) in the kit's atlas (texture 1), the application's part's
(part_art.c, part_art.h: its atlas's ART_APP_COUNT) in the part's atlas (texture 3, ART_APP_PART's size). The rule (the box shows no sign): no
meaning drawn on both screens; a state may show none, and exactly NONE (below) are expected to show none. The
meanings are this script's own: the head's ART_WARN and ART_FOLDER_BIG and the footer's ART_WARN_FOOT,
ART_FOLDER_FOOT and ART_HOME_FOOT are the box's warning, folder and house. Prints a line per capture and a sum; exit 1 if a capture breaks the rule."""
import pathlib
import re
import sys

walk, art, look = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2]), sys.argv[3]
hdr = (art / "ui_art.h").read_text(encoding="utf-8")
enum = hdr[hdr.index("enum art_id {"):]
names, kit_n = [], 0
for ln in enum.splitlines()[1:]:
    m = re.match(r"\s+(ART_\w+),", ln)
    if not m:
        break
    if m.group(1) == "ART_COUNT":
        break
    names.append(m.group(1))
part_h = (art / "part_art.h").read_text(encoding="utf-8")
app_n = int(re.search(r"^#define ART_APP_COUNT (\d+)$", part_h, re.M).group(1))
kit_n = len(names)
names += re.findall(r"^\s+(ART_\w+)", part_h[part_h.index("enum part_art_id {"):], re.M)[:app_n]
rows = []
for source, table in (("art_%s.c" % look, "art_%s_sprites[ART_COUNT]" % look),
                      ("part_art.c", "art_part_%s_sprites[ART_APP_COUNT]" % look)):
    with open(art / source, encoding="utf-8") as f:
        started = False
        for ln in f:
            if not started:
                started = ln.startswith("static const art_sprite %s" % table)
                continue
            m = re.match(r"\s+\{(\d+), (\d+), (\d+), (\d+),", ln)
            if not m:
                break
            rows.append(tuple(int(v) for v in m.groups()))
assert len(rows) == len(names), (len(rows), len(names))
MEANING = {"ART_WARN": "warn", "ART_INFO_WARN": "warn", "ART_FOLDER_BIG": "folder", "ART_INFO_FOLDER": "folder",
           "ART_WARN_FOOT": "warn", "ART_FOLDER_FOOT": "folder", "ART_HOME_FOOT": "home",
           "ART_INFO_SD": "sd", "ART_INFO_WIFI": "wlan", "ART_INFO_CAMERA": "camera", "ART_INFO_HOME": "home"}
W, H = (int(v) for v in re.search(r"#define ART_ATLAS_W (\d+)\n#define ART_ATLAS_H (\d+)", hdr).groups())
PW, PH = (int(v) for v in re.search(r"ART_APP_PART = \{art_part_looks, \d+, (\d+), (\d+),",
                                    (art / "part_art.c").read_text(encoding="utf-8")).groups())
TEXTURES = {"1": (W, H, 0, kit_n), "3": (PW, PH, kit_n, len(names))}   # the kit's atlas, the part's


def sprite(tex, u0, v0):
    w0, h0, first, end = TEXTURES[tex]
    sx, sy = u0 * w0 + 0.01, (1 - v0) * h0 + 0.01
    for name, (x, y, w, h) in zip(names[first:end], rows[first:end]):
        if w and x <= sx < x + w and y <= sy < y + h:
            return name
    return None


bad = checked = 0
none = []
NONE = ["bX-02", "bX-06", "bX-10", "bX-11", "dX-02", "dX-07"]
for dl in sorted(walk.glob("*.dl")):
    cap = dl.name.split("-", 1)[1][:-3]
    if not (cap.startswith(("dX-", "bX-")) or re.match(r"bD-0[2-6]", cap)):
        continue
    signs = {"0": set(), "2": set()}
    for ln in dl.read_text(encoding="utf-8").splitlines():
        p = ln.split(" ")
        if p[0] != "I" or p[2] not in TEXTURES or p[1] not in signs:
            continue
        name = sprite(p[2], float(p[7]), float(p[8]))
        if name in MEANING:
            signs[p[1]].add(MEANING[name])
    ok = not (signs["0"] & signs["2"])
    if not (signs["0"] | signs["2"]):
        none.append(cap[:5])
    checked += 1
    bad += not ok
    print("%s %s: top %s, bottom %s" % ("ok  " if ok else "BAD ", cap, ",".join(sorted(signs["0"])) or "-",
                                       ",".join(sorted(signs["2"])) or "-"))
print("%s: %d states, %d with a sign twice, %d with none: %s (expected among %s)" % (
    walk.name, checked, bad, len(none), ",".join(none) or "-", ",".join(NONE)))
bad += any(n not in NONE for n in none)
sys.exit(1 if bad or not checked else 0)
