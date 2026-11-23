"""The Stock design's start screen word (uikit/art/look_stock.py, word; R3 decided: "ein bisschen dünner, man kann das e
kaum lesen"): drawn over the top screen's ground in every scheme, both "e"s of "Theme" read open at 1:1. A pixel reads
as ground when its luminance is nearer the ground's than the line's ink. The counter: on the e's middle column,
between the bar's centre line and the bowl's, at least COUNTER px of ground; the opening: on the straight way from
the bar's end to the arc's end, at least OPENING px of ground (at d6c028b both were 0 and 0 to 1).

Run: python3 -I host/test_word.py      (make test runs it)
"""
import sys
import unittest
from pathlib import Path

UI = Path(__file__).resolve().parent.parent / "app" / "ui"
KIT = UI.parent.parent / "uikit"
sys.dont_write_bytecode = True
sys.argv[1:] = ["--app", str(UI / "app_art.py"), "--look", "stock"]   # art.py reads them when imported
sys.path[:0] = [str(KIT / "art"), str(UI)]
import art as A  # noqa: E402
import app_images as I  # noqa: E402  (the application's half: the word and the panes)

S = I.LOOK        # Stock's drawing of the application's images
D = A.D
COUNTER, OPENING = 4, 2


def lum(c):
    return 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2]


def drawn(scheme):
    """The word's two sprites over the top ground's gradient at the word's rows, and the scheme's layout."""
    A.L = I.L = A.scheme_layout(D, scheme)
    top, low = S.word(None)
    ga, gb = A.rgb(A.L.COL_TOP_BG_A), A.rgb(A.L.COL_TOP_BG_B)
    img = A.Image(D.WORD_W, D.WORD_H)
    for y in range(D.WORD_H):
        t = (D.START_WORD_Y + y) / D.SCREEN_H
        ground = tuple(ga[i] + (gb[i] - ga[i]) * t for i in range(3))
        for x in range(D.WORD_W):
            img.over(y * D.WORD_W + x, ground, 1.0)
    img.blit(top.img, 0, D.WORD_TOP_ROW)
    img.blit(low.img, 0, D.WORD_LOW_ROW)
    return img, A.L


def es():
    """Each "e" of "Theme": its middle (its box's), the bar's end and the arc's end, px in the word's image."""
    _, text, unit, y, _, _ = S.word_places()[1]
    out = []
    for ch, (segs, (x0, y0, x1, y1)) in zip(text, S.word_letters(text, unit, y)):
        if ch == "e":
            bar, last = segs[0], segs[-1]
            out.append(((x0 + x1) / 2, (y0 + y1) / 2, (bar[0] + bar[2], bar[1] + bar[3]),
                        (last[0] + last[2], last[1] + last[3])))
    return out


class Word(unittest.TestCase):
    def test_e_reads_open_in_every_scheme(self):
        found = es()
        self.assertEqual(len(found), 2)
        for k, scheme in enumerate(A.schemes.SCHEMES):
            img, L = drawn(k)
            w = img.w
            near = lum(A.rgb(L.COL_TOP_BG_A)), lum(A.rgb(L.COL_TEAL))

            def ground(x, y):
                v = lum(img.p[4 * (y * w + x):4 * (y * w + x) + 3])
                return abs(v - near[0]) < abs(v - near[1])
            for i, (cx, cy, end, tail) in enumerate(found):
                radius = (end[0] - cx)
                counter = sum(ground(int(cx), y) for y in range(int(cy - radius), int(cy) + 1))
                way = {(int(end[0] + (tail[0] - end[0]) * t / 200), int(end[1] + (tail[1] - end[1]) * t / 200))
                       for t in range(201)}
                opening = sum(ground(x, y) for x, y in way)
                with self.subTest(scheme=scheme["name"], e=i + 1):
                    self.assertGreaterEqual(counter, COUNTER, "counter")
                    self.assertGreaterEqual(opening, OPENING, "opening")


if __name__ == "__main__":
    unittest.main(verbosity=2)
