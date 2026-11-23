"""The Stock design's frame round a design's picture (uikit/art/look_stock.py, pane_rim): since C23 ("C Abzug looks the
best") a white card as a photo print with a soft shadow, the same in every scheme (one set of tones for the seven light
schemes, one for Nacht). Drawn as app_ui.c draws a pane (pane, ui_done_card): a stand-in picture of one mid tone, its
corners rounded by ART_MASK (art.py icon_mask) scaled into the hole with linear filtering, the class's rim sprite over
it, both on the top screen's ground and scaled together, in every scheme.
- The card's numbers: CARD_W px wide (the small class three quarters of it) from CARD_IN px inside the picture's edge,
  opaque across, its face light; the sprite holds CARD_PAD round the class's box (CARD_PAD_LOW below); the light
  schemes' sprites alike and Nacht's its own; the neighbours SWARM_STOCK_APART 9 further out (ui_layout.h).
- Flush (R19: "der border des rahmens ist nicht gleich wie der karte. man sieht einen ganz feinen space dazwischen"):
  from inside the picture outwards, along the middle of each edge and the diagonal of each corner, the card begins
  within a pixel of the picture's edge and no pixel between them is lighter or darker than both (no gap of ground).

Run: python3 -I host/test_frame.py      (make test runs it)
"""
import math
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
PICTURE = (102, 112, 56)        # the stand-in picture's tone: a mid olive, as the design in the owner's close-up
CARD_LIGHT = 215                # the card's face at least this light (luminance of 255) in every scheme


def lum(c):
    return 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2]


def sample(get, w, h, u, v):
    """Linear filtering of get(x, y) (w x h texels, clamped at the edges) at texel coordinates u, v."""
    u, v = u - 0.5, v - 0.5
    x0, y0 = math.floor(u), math.floor(v)
    fx, fy = u - x0, v - y0
    at = lambda x, y: get(min(w - 1, max(0, x)), min(h - 1, max(0, y)))   # noqa: E731
    return [(a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy
            for a, b, c, d in zip(at(x0, y0), at(x0 + 1, y0), at(x0, y0 + 1), at(x0 + 1, y0 + 1))]


def framed(sprite, n, rim, k, ground):
    """The pane's box (n px at scale k) drawn as app_ui.c draws it: ground, picture, rim; straight RGB per pixel, and
    the picture's box (x0, y0, side) in the result, which holds the sprite's whole area."""
    pad = sprite.pad
    w, h = sprite.img.w, sprite.img.h
    W, H = math.ceil(w * k), math.ceil(h * k)
    side = (n - 2 * rim) * k
    x0, y0 = (pad[0] + rim) * k, (pad[1] + rim) * k
    mask, m = A.icon_mask(), D.DETAIL_ICON
    rimp = sprite.img.p
    out = []
    for y in range(H):
        for x in range(W):
            col = list(ground)
            u, v = (x + 0.5 - x0) / side * m, (y + 0.5 - y0) / side * m
            if -1 <= u <= m + 1 and -1 <= v <= m + 1:
                a = sample(lambda i, j: (mask[j * m + i] / 255.0,) if 0 <= i < m and 0 <= j < m else (0.0,),
                           m, m, u, v)[0] if 0 <= u <= m and 0 <= v <= m else 0.0
                col = [PICTURE[i] * a + col[i] * (1 - a) for i in range(3)]
            px = sample(lambda i, j: rimp[4 * (j * w + i):4 * (j * w + i) + 4], w, h, (x + 0.5) / k, (y + 0.5) / k)
            a = px[3]
            col = [px[i] + col[i] * (1 - a) for i in range(3)]       # premultiplied over
            out.append(col)
    return out, W, (x0, y0, side)


def rays(box, r):
    """From inside the picture outwards: the middle of each edge and each corner's diagonal through its arc."""
    x0, y0, s = box
    cx, cy, half = x0 + s / 2, y0 + s / 2, s / 2
    out = [((cx, y0 + 6), (0, -1)), ((cx, y0 + s - 6), (0, 1)), ((x0 + 6, cy), (-1, 0)), ((x0 + s - 6, cy), (1, 0))]
    for sx in (-1, 1):
        for sy in (-1, 1):
            ax, ay = cx + sx * (half - r), cy + sy * (half - r)     # the corner arc's centre
            out.append(((ax, ay), (sx / math.sqrt(2), sy / math.sqrt(2))))
    return out


def straight(sprite):
    """The sprite's straight RGBA at texel x, y (alpha 0 to 1)."""
    p, w = sprite.img.p, sprite.img.w

    def at(x, y):
        j = 4 * (y * w + x)
        a = p[j + 3]
        return (p[j] / a, p[j + 1] / a, p[j + 2] / a, a) if a > 0 else (0.0, 0.0, 0.0, 0.0)
    return at


def schemes_drawn():
    for index, scheme in enumerate(A.schemes.SCHEMES):
        A.L = I.L = A.scheme_layout(D, index)
        yield scheme


def is_dark(scheme):
    return A.schemes.luminance(A.rgb(A.L.COL_CARD)) < 0.2


class Frame(unittest.TestCase):
    def test_card_numbers(self):
        self.assertEqual(D.SWARM_STOCK_APART, 9)
        drawn = {}
        for scheme in schemes_drawn():
            for name, n, rim in I.PANE_CLASSES:
                sprite = S.pane_rim(name, n, rim)
                at, width = straight(sprite), S.CARD_W * rim / float(D.PANE_BIG_RIM)
                drawn[scheme["name"], name] = (is_dark(scheme), bytes(sprite.img.rgba()))
                with self.subTest(scheme=scheme["name"], pane=name):
                    self.assertEqual(sprite.pad, (S.CARD_PAD, S.CARD_PAD, S.CARD_PAD, S.CARD_PAD_LOW))
                    self.assertEqual((sprite.img.w, sprite.img.h),
                                     (n + 2 * S.CARD_PAD, n + S.CARD_PAD + S.CARD_PAD_LOW))
                    x = S.CARD_PAD + n // 2                     # the middle of the upper and of the left edge
                    edge = S.CARD_PAD + rim                     # the picture's first row, px from the sprite's corner
                    for along in (lambda d: at(x, d), lambda d: at(d, x)):
                        self.assertGreaterEqual(along(edge)[3], 0.99, "the card reaches over the picture's edge")
                        for d in range(edge - math.ceil(width) + 1, edge):
                            texel = along(d)
                            self.assertGreaterEqual(texel[3], 0.99, "the card opaque across, %d px out" % (edge - d))
                            self.assertGreaterEqual(lum(texel), CARD_LIGHT, "the card's face light")
                        self.assertLess(along(edge - math.ceil(width) - 1)[3], 0.5, "the card no wider than CARD_W")
        for name, _, _ in I.PANE_CLASSES:
            light = {data for (_, n), (dark, data) in drawn.items() if n == name and not dark}
            dark = {data for (_, n), (dark, data) in drawn.items() if n == name and dark}
            with self.subTest(pane=name):
                self.assertEqual((len(light), len(dark)), (1, 1), "one card for the light schemes, one for Nacht")
                self.assertNotEqual(light, dark)

    def test_card_flush_on_the_picture(self):
        for scheme in schemes_drawn():
            ground = A.rgb(A.L.COL_TOP_BG_A)
            for name, n, rim in I.PANE_CLASSES:
                sprite = S.pane_rim(name, n, rim)
                scales = [1.0] + ([D.DONE_PICTURE / (n - 2 * rim)] if name == "PANE_BIG" else [])
                for k in scales:
                    pixels, W, box = framed(sprite, n, rim, k, ground)
                    r = (n - 2 * rim) * k * D.DETAIL_ICON_R / D.DETAIL_ICON
                    edge = A.Box(box[0], box[1], box[0] + box[2], box[1] + box[2], r)
                    for (sx, sy), (dx, dy) in rays(box, r):
                        way = []
                        for t in range(0, int(4 * (rim + S.CARD_PAD) * k + 40)):
                            x, y = int(sx + dx * t / 4), int(sy + dy * t / 4)
                            if 0 <= x < W and 0 <= y < len(pixels) // W and (not way or way[-1][0] != (x, y)):
                                way.append(((x, y), lum(pixels[y * W + x]), edge.sd(x + 0.5, y + 0.5)))
                        last_in = max(i for i, (_, _, d) in enumerate(way) if d < -1.0)
                        card = next((i for i in range(last_in, len(way)) if way[i][1] >= CARD_LIGHT), None)
                        with self.subTest(scheme=scheme["name"], pane=name, k=round(k, 3), ray=(dx, dy)):
                            self.assertIsNotNone(card, "no card outside the picture")
                            self.assertLessEqual(way[card][2], 1.0, "the card begins %.1f px out" % way[card][2])
                            lo, hi = min(lum(PICTURE), way[card][1]), max(lum(PICTURE), way[card][1])
                            for (x, y), v, _ in way[last_in:card]:
                                self.assertTrue(lo - 2.0 <= v <= hi + 2.0, "a gap at %s: %.0f" % ((x, y), v))


if __name__ == "__main__":
    unittest.main(verbosity=1)
