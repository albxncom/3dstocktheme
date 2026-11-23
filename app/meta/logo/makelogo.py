#!/usr/bin/env python3
"""Writes our own start logos as unsigned DARC files (signing, compression and padding: keytool.py sign).

  python3 -I makelogo.py OUTDIR          OUTDIR must not exist yet; writes <name>.darc for every logo below and
                                         prints its size, sha256 and the compressed size with a stand-in MAC

Logos (nothing taken from another logo; layouts, animations and pictures are built here):
  black      both screens: RootPane > "all" (a pane that draws nothing) > "body" (the same). No texture, no material.
  black-tex  the fallback if a logo without a texture is refused: "body" on each screen is a full-screen picture
             of one 8x8 black texture (4-bit alpha, fully opaque) in black.
  word       = word1 below, the one built into test title L3.
  word1..3   black; on the top screen "body" is a picture of the word 3DStockTheme (lettering.py) in light grey,
             the bottom screen as in black. Variants differ in size, stroke weight and height (see VARIANTS).
Animations, the same in every logo: A (30 frames) "all" alpha (word: fades in over 10 frames; black: held at
255), B (60 frames, looping while the application loads) "body" alpha held at 255, C (15 frames) "all" fades to 0
by frame 10 and stays there. Groups: G_A_00 and G_C_00 hold "all", G_B_00 holds "body".
"""
import hashlib
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.dont_write_bytecode = True
sys.path.insert(0, HERE)
import lettering   # noqa: E402
import logofmt as F   # noqa: E402

LENGTHS = {"A": 30, "B": 60, "C": 15}
LIGHT = (232, 232, 232)
# name: (pixels per letter unit, stroke half-thickness in letter units, pane centre's height above the middle)
VARIANTS = {
    "word1": (2.0, 1.2, 0),       # 231 x 25 px, centred
    "word2": (2.6, 0.9, 0),       # 282 x 31 px, larger and lighter, centred
    "word3": (1.6, 1.0, -44),     # 177 x 20 px, smaller, in the lower third
}
STAND_IN_MAC = hashlib.sha256(b"stand-in for the 32 bytes of a real MAC: not a key, not a MAC").digest()


def screen(width, body, fade_in):
    root = F.pane("pan1", "RootPane", w=width, h=240, flags=1,
                  children=[F.pane("pan1", "all", flags=3, children=[body])])
    groups = {"G_A_00": ["all"], "G_B_00": ["body"], "G_C_00": ["all"]}
    a_keys = F.linear([(0, 0), (10, 255), (LENGTHS["A"] - 1, 255)]) if fade_in else [(0, 255, 0)]
    anims = {
        "A": [("all", F.ALPHA, a_keys)],
        "B": [("body", F.ALPHA, [(0, 255, 0)])],
        "C": [("all", F.ALPHA, F.linear([(0, 255), (10, 0), (LENGTHS["C"] - 1, 0)]))],
    }
    return root, groups, {x: F.animation(x, LENGTHS, t) for x, t in anims.items()}


def build(name):
    files = {}
    if name in ("black", "black-tex"):
        if name == "black-tex":
            files[("timg", "black.bclim")] = F.clim_a4(8, 8, lambda x, y: 15)
            tex, mats = ["black.bclim"], [F.material("black", 0, (0, 0, 0))]
            body = lambda w: F.pane("pic1", "body", w=w, h=240, material=0)
        else:
            tex, mats = [], []
            body = lambda w: F.pane("pan1", "body")
        sides, fade = [("U", 400, body(400), tex, mats), ("D", 320, body(320), tex, mats)], False
    else:
        scale, half, y = VARIANTS["word1" if name == "word" else name]
        tw, th, alpha, _ = lettering.texture(scale, half)
        files[("timg", "word.bclim")] = F.clim_a4(tw, th, alpha)
        top = F.pane("pic1", "body", y=y, w=tw, h=th, material=0)
        sides = [("U", 400, top, ["word.bclim"], [F.material("word", 0, LIGHT)]),
                 ("D", 320, F.pane("pan1", "body"), [], [])]
        fade = True
    for side, width, body_pane, tex, mats in sides:
        root, groups, anims = screen(width, body_pane, fade)
        files[("blyt", "NintendoLogo_%s_00.bclyt" % side)] = F.layout(width, tex, mats, root, groups)
        for x, data in anims.items():
            files[("anim", "NintendoLogo_%s_00_SceneOut%s.bclan" % (side, x))] = data
    return F.darc(files)


NAMES = ["black", "black-tex", "word", "word1", "word2", "word3"]


def main(argv):
    if len(argv) != 2:
        print(__doc__.split("\n\n")[1])
        return 2
    out = argv[1]
    os.makedirs(out)      # fails if it is there: a folder is never reused
    for name in NAMES:
        d = build(name)
        with open(os.path.join(out, name + ".darc"), "wb") as f:
            f.write(d)
        packed = len(F.lz11(d + STAND_IN_MAC))
        print("%-10s DARC 0x%04x bytes, sha256 %s, compressed with a stand-in MAC 0x%04x of 0x%x%s"
              % (name, len(d), hashlib.sha256(d).hexdigest(), packed, F.LIMIT,
                 "" if packed <= F.LIMIT else "  OVER THE LIMIT"))
        if packed > F.LIMIT:
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
