"""compare.py A B: two folders render.sh wrote, picture by picture (every .png under them, by relative path; every
channel of every pixel, exactly). Names each picture that differs, with the count of differing pixels and their box
per screen in true-size coordinates (the pictures are 2x: the top screen 800 x 480 above the bottom one at x 80),
each picture whose size differs, and each picture found on one side only. Prints the sum; exit 1 if anything
differs, 2 if a folder holds no picture."""
import pathlib
import sys

from PIL import Image, ImageChops

a, b = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
pa = {p.relative_to(a).as_posix() for p in a.rglob("*.png")}
pb = {p.relative_to(b).as_posix() for p in b.rglob("*.png")}
if not pa or not pb:
    print("no pictures in", a if not pa else b)
    sys.exit(2)
n = 0
for rel in sorted(pa ^ pb):
    print(rel, "only in", a if rel in pa else b)
    n += 1
for rel in sorted(pa & pb):
    x, y = Image.open(a / rel).convert("RGBA"), Image.open(b / rel).convert("RGBA")
    if x.size != y.size:
        print(rel, "size", x.size, y.size)
        n += 1
        continue
    # a pixel differs if any channel differs by any amount (a grey conversion would round small changes away)
    r, g, bl, al = ImageChops.difference(x, y).split()
    d = ImageChops.lighter(ImageChops.lighter(r, g), ImageChops.lighter(bl, al)).point(lambda v: 255 if v else 0)
    if not d.getbbox():
        continue
    n += 1
    parts = []
    for name, box in (("top", (0, 0, 800, 480)), ("bottom", (80, 480, 720, 960))):
        c = d.crop(box)
        bb = c.getbbox()
        if bb:
            px = c.histogram()[255] // 4
            parts.append("%s %d px in x %d-%d y %d-%d" % (name, px, bb[0] // 2, (bb[2] - 1) // 2, bb[1] // 2,
                                                         (bb[3] - 1) // 2))
    print(rel, "; ".join(parts) or "outside the screens")
print("%d of %d pictures differ" % (n, len(pa | pb)))
sys.exit(1 if n else 0)
