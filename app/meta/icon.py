"""Draws the HOME Menu's icon of the application at build time (C195: the Stock design's house raised in teal on a
light ground, as the Stock start screen raises its house), 48 px and its own plainer drawing at 24 px. The CIA's
banner is not drawn here: its model is the finished file banner.cgfx (banner.txt).

The icon is 5-6-5 colour in the SMDH: it is written as 5-6-5 keeps it, without dither.

The icon is fixed: it does not follow the colour scheme or the design, so this script has its own colours and does
not read layout.h or import uikit/art/art.py. Its house is uikit/art/look_stock.py's raised (with its blurred and SURFACE) as it was
when the icon was chosen. Python 3 standard library only; the same input gives the same bytes. The pictures are the
approved ones while test_icon.py passes (make test).

Usage:
  python3 -I icon.py OUT_DIR       writes OUT_DIR/icon.png (48x48) and icon24.png (24x24)
  python3 -I icon.py --small ICN   puts the 24 px icon into the SMDH that bannertool made (its makesmdh takes one
                                   picture and makes the small icon by scaling it down)
"""
import math
import struct
import sys
import zlib
from pathlib import Path

INF = float("inf")
WHITE = (255.0, 255.0, 255.0)
BLACK = (0.0, 0.0, 0.0)


def rgb(v):
    return ((v >> 16) & 255, (v >> 8) & 255, v & 255)


TEAL = rgb(0x16877C)                # COL_TEAL: the house, as Stock's symbols
SKY = (rgb(0xFFFFFF), rgb(0xDBE0E7))                 # the light ground, top to foot
SHADOW_COL = rgb(0x282B2E)          # the raised house's shadow
RAISED = dict(reach=1, light=0.55, shade=0.30, side=0.10, lift=0.12)    # look_stock.py SURFACE
U = 64.0                            # the house's drawing units


# ---------------------------------------------------------------- pixels (as uikit/art/art.py)


class Image:
    """Premultiplied RGBA, 0..255 per colour channel and 0..1 alpha."""

    def __init__(self, w, h):
        self.w, self.h = w, h
        self.p = [0.0] * (w * h * 4)

    def over(self, i, color, a):
        p, k, j = self.p, 1.0 - a, 4 * i
        p[j] = color[0] * a + p[j] * k
        p[j + 1] = color[1] * a + p[j + 1] * k
        p[j + 2] = color[2] * a + p[j + 2] * k
        p[j + 3] = a + p[j + 3] * k

    def paint(self, shade):
        """shade(x, y) at each pixel's centre returns (alpha, colour) or None."""
        for y in range(self.h):
            cy = y + 0.5
            for x in range(self.w):
                got = shade(x + 0.5, cy)
                if got and got[0] > 0.0:
                    self.over(y * self.w + x, got[1], min(got[0], 1.0))

    def png(self):
        """An opaque PNG (colour type 2)."""
        rows = []
        for y in range(self.h):
            row = bytearray(self.w * 3)
            for x in range(self.w):
                j = 4 * (y * self.w + x)
                a = self.p[j + 3]
                if round(min(a, 1.0) * 255):
                    for c in range(3):
                        row[3 * x + c] = max(0, min(255, round(self.p[j + c] / a)))
            rows.append(b"\0" + bytes(row))

        def chunk(kind, payload):
            return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", zlib.crc32(kind + payload))
        header = struct.pack(">IIBBBBB", self.w, self.h, 8, 2, 0, 0, 0)
        return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(b"".join(rows), 9))
                + chunk(b"IEND", b""))


def cover(d):
    """The share of a pixel inside a shape whose edge is d px away from the pixel's centre (negative: inside)."""
    return 1.0 if d <= -0.5 else 0.0 if d >= 0.5 else 0.5 - d


def mix(a, b, t):
    return (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t)


def clamp(t):
    return 0.0 if t < 0.0 else 1.0 if t > 1.0 else t


def smooth(a, b, t):
    t = clamp((t - a) / (b - a))
    return t * t * (3.0 - 2.0 * t)


# ---------------------------------------------------------------- shapes: signed distances, negative inside


def box(x0, y0, x1, y1, r):
    """A rectangle with round corners of radius r."""
    cx, cy, hw, hh = (x0 + x1) / 2.0, (y0 + y1) / 2.0, (x1 - x0) / 2.0, (y1 - y0) / 2.0

    def sd(px, py):
        dx, dy = abs(px - cx) - hw + r, abs(py - cy) - hh + r
        if dx > 0 and dy > 0:
            return math.hypot(dx, dy) - r
        return max(dx, dy) - r
    return sd


def poly(pts):
    """A closed polygon."""
    segs = [(a[0], a[1], b[0] - a[0], b[1] - a[1]) for a, b in zip(pts, pts[1:] + pts[:1])]

    def sd(u, v):
        best, inside = INF, False
        for ax, ay, vx, vy in segs:
            wx, wy = u - ax, v - ay
            t = (wx * vx + wy * vy) / (vx * vx + vy * vy)
            t = 0.0 if t < 0.0 else 1.0 if t > 1.0 else t
            ex, ey = wx - t * vx, wy - t * vy
            d2 = ex * ex + ey * ey
            if d2 < best:
                best = d2
            if (ay > v) != (ay + vy > v) and wx < vx * wy / vy:
                inside = not inside
        return -math.sqrt(best) if inside else math.sqrt(best)
    return sd


def without(f, g):
    return lambda u, v: max(f(u, v), -g(u, v))


def fillet(pts, radii, steps=8):
    """A polygon with each corner cut round to its radius (0: left sharp), as points."""
    out, n = [], len(pts)
    for i, (p, r) in enumerate(zip(pts, radii)):
        if not r:
            out.append(p)
            continue
        a, b = pts[i - 1], pts[(i + 1) % n]
        d1 = ((a[0] - p[0]) / math.dist(a, p), (a[1] - p[1]) / math.dist(a, p))
        d2 = ((b[0] - p[0]) / math.dist(b, p), (b[1] - p[1]) / math.dist(b, p))
        half_angle = math.acos(max(-1.0, min(1.0, d1[0] * d2[0] + d1[1] * d2[1]))) / 2.0
        along, to_centre = r / math.tan(half_angle), r / math.sin(half_angle)
        mid = math.hypot(d1[0] + d2[0], d1[1] + d2[1])
        c = (p[0] + (d1[0] + d2[0]) / mid * to_centre, p[1] + (d1[1] + d2[1]) / mid * to_centre)
        a0 = math.atan2(p[1] + d1[1] * along - c[1], p[0] + d1[0] * along - c[0])
        a1 = math.atan2(p[1] + d2[1] * along - c[1], p[0] + d2[0] * along - c[0])
        turn = (a1 - a0 + math.pi) % (2.0 * math.pi) - math.pi
        out += [(c[0] + r * math.cos(a0 + turn * k / steps), c[1] + r * math.sin(a0 + turn * k / steps))
                for k in range(steps + 1)]
    return out


def house_shape(small=False):
    """The house in its 64 units: roof and walls as one outline with round corners (sharp where the roof overhangs
    the walls), the window cut out. small: the plainer one for 24 px, with a wider window and blunter corners."""
    apex, eave, roof, wall, bottom, top_r, tip_r, corner = (5.0, 30.0, 27.5, 20.5, 57.0, 3.0, 2.4, 3.6) if small else \
        (5.0, 30.0, 27.5, 19.0, 56.0, 2.6, 1.6, 2.8)
    pts = [(32.0, apex), (32.0 + roof, eave), (32.0 + wall, eave), (32.0 + wall, bottom), (32.0 - wall, bottom),
           (32.0 - wall, eave), (32.0 - roof, eave)]
    window = box(22.5, 33.0, 41.5, 49.5, 2.2) if small else box(25.0, 35.0, 39.0, 48.0, 1.6)
    return without(poly(fillet(pts, (top_r, tip_r, 0, corner, corner, 0, tip_r))), window)


def placed(shape, n, ox, oy):
    """shape (64 units) drawn n px square with its corner at (ox, oy), in px."""
    s = n / U
    return lambda x, y: shape((x - ox) / s, (y - oy) / s) * s


# ---------------------------------------------------------------- raised, as look_stock.py


def blurred(grid, w, h, r):
    """look_stock.py blurred(): twice a box of 2r + 1 px each way, 0 outside."""
    def box_blur(g, across):
        out, k = [0.0] * (w * h), 1.0 / (2 * r + 1)
        n, lines = (w, h) if across else (h, w)
        for j in range(lines):
            at = [j * w + i for i in range(n)] if across else [i * w + j for i in range(n)]
            line = [g[i] for i in at]
            s = sum(line[:r + 1])
            for i in range(n):
                out[at[i]] = s * k
                if i + r + 1 < n:
                    s += line[i + r + 1]
                if i - r >= 0:
                    s -= line[i - r]
        return out
    for _ in range(2):
        grid = box_blur(box_blur(grid, True), False)
    return grid


def raised(img, sd, color, down):
    """look_stock.py raised(): the shape standing out of the ground in color, as the start screen's house and word:
    a soft shadow down px under it, the face lighter at its top, light where it faces up, shade where it faces down
    and on the sides."""
    w, h, s = img.w, img.h, RAISED
    cov = [cover(sd(k % w + 0.5, k // w + 0.5)) for k in range(w * h)]
    shadow = blurred([0.0] * (w * down) + cov[:w * (h - down)], w, h, 1)
    for k in range(w * h):
        if shadow[k] > 0.0 and cov[k] < 1.0:
            img.over(k, SHADOW_COL, 0.40 * shadow[k] * (1.0 - cov[k]))
    rows = [y for y in range(h) if max(cov[y * w:(y + 1) * w]) > 0.0]
    y0, y1, n = rows[0], rows[-1], 2 * s["reach"] + 1
    top = mix(color, WHITE, s["lift"])
    bevel = blurred(cov, w, h, s["reach"])

    def at(x, y):
        return bevel[y * w + x] if 0 <= x < w and 0 <= y < h else 0.0
    for k in range(w * h):
        c = cov[k]
        if c <= 0.0:
            continue
        x, y = k % w, k // w
        img.over(k, mix(top, color, (y - y0) / max(1.0, y1 - y0)), c)
        up = (at(x, y + 1) - at(x, y - 1)) / 2.0 * n
        side = abs(at(x + 1, y) - at(x - 1, y)) / 2.0 * n
        if up > 0.0:
            img.over(k, WHITE, s["light"] * min(1.0, up) * c)
        elif up < 0.0:
            img.over(k, BLACK, s["shade"] * min(1.0, -up) * c)
        if side > 0.0:
            img.over(k, BLACK, s["side"] * min(1.0, side) * c)


# ---------------------------------------------------------------- grounds, and what the console's formats keep


def ground(size, a, b, way):
    """An opaque ground running from a to b along way."""
    w, h = size
    img = Image(w, h)
    span = abs(way[0]) * w + abs(way[1]) * h

    def at(x, y):
        t = (max(0.0, way[0]) * x + max(0.0, -way[0]) * (w - x) + max(0.0, way[1]) * y +
             max(0.0, -way[1]) * (h - y)) / span
        return (1.0, mix(a, b, smooth(0.0, 1.0, t)))
    img.paint(at)
    return img


def quantised(img, levels):
    """An opaque image as a format with so many levels a channel keeps it."""
    out = Image(img.w, img.h)
    for k in range(img.w * img.h):
        px = []
        for c in range(3):
            n = levels[c] - 1
            px.append(min(n, max(0, int(math.floor(img.p[4 * k + c] / 255.0 * n + 0.5)))) * 255.0 / n)
        out.p[4 * k:4 * k + 4] = px + [1.0]
    return out


# ---------------------------------------------------------------- the icon


def icon(size):
    """The light ground running down from white, the house raised in teal on it, large and 1 px above the middle; at
    24 px the plainer house, drawn 26 px so that it fills the picture, centred, with a shorter shadow."""
    small = size < 40
    img = ground((size, size), SKY[0], SKY[1], (0.0, 1.0))
    n = 26 if small else 46
    raised(img, placed(house_shape(small), n, (size - n) / 2.0, (size - n) / 2.0 - (0.0 if small else 1.0)), TEAL,
           1 if small else 2)
    return quantised(img, (32, 64, 32))


SMDH_SIZE = 0x36C0
SMALL_AT = 0x2040                   # the small icon in an SMDH: 24x24 RGB565, 8x8 tiles, each in Morton order


def rgb565_tiled(img):
    out = bytearray()
    for ty in range(0, img.h, 8):
        for tx in range(0, img.w, 8):
            for i in range(64):
                x = tx + (i & 1) + ((i >> 1) & 2) + ((i >> 2) & 4)
                y = ty + ((i >> 1) & 1) + ((i >> 2) & 2) + ((i >> 3) & 4)
                r, g, b = (round(img.p[4 * (y * img.w + x) + c]) for c in range(3))
                out += struct.pack("<H", (r * 31 + 127) // 255 << 11 | (g * 63 + 127) // 255 << 5 |
                                   (b * 31 + 127) // 255)
    return bytes(out)


def main():
    if len(sys.argv) == 3 and sys.argv[1] == "--small":
        path = Path(sys.argv[2])
        data = bytearray(path.read_bytes())
        if data[:4] != b"SMDH" or len(data) != SMDH_SIZE:
            sys.exit("%s is not an SMDH" % path)
        small = rgb565_tiled(icon(24))
        data[SMALL_AT:SMALL_AT + len(small)] = small
        path.write_bytes(bytes(data))
        return
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    out = Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)
    (out / "icon.png").write_bytes(icon(48).png())
    (out / "icon24.png").write_bytes(icon(24).png())


if __name__ == "__main__":
    main()
