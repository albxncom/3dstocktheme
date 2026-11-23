#!/usr/bin/env python3
"""The word 3DStockTheme in the project's own lettering, as a 4-bit coverage mask. Pure Python.

The letters are the project's own drawing: round-ended strokes of even thickness, in letter units (capitals from
y = 0 down to the baseline at y = 10, small letters from 3.6). The stroke table was first drawn for the flat banner
(GLYPHS in what is now app/meta/icon.py, removed with that banner by C170) and came here as numbers; no font file is used.
Here the letters sit on one straight baseline (the banner's bounce is left out: plain lettering).
"""
import math

TEXT = "3DStockTheme"
SAMPLES = 4                    # 4 x 4 samples per pixel for the coverage


def glyphs(step=10.0):
    def arc(cx, cy, r, a0, a1):
        n = max(2, int(round(abs(a1 - a0) / step)))
        return [(cx + r * math.cos(math.radians(a0 + (a1 - a0) * k / n)),
                 cy + r * math.sin(math.radians(a0 + (a1 - a0) * k / n))) for k in range(n + 1)]
    return {
        "3": (5.1, [arc(2.5, 2.4, 2.4, -160, 90), arc(2.5, 7.4, 2.6, -90, 160)]),
        "D": (6.2, [[(0, 10), (0, 0), (1.2, 0)] + arc(1.2, 5, 5, -90, 90) + [(0, 10)]]),
        "S": (5.2, [arc(2.6, 2.45, 2.45, -25, -270), arc(2.6, 7.45, 2.55, -90, 155)]),
        "t": (4.0, [[(1.5, 0.8), (1.5, 8.2)] + arc(3.3, 8.2, 1.8, 180, 90) + [(4.0, 10)], [(0, 3.6), (3.8, 3.6)]]),
        "o": (6.4, [arc(3.2, 6.8, 3.2, 0, 360)]),
        "c": (5.8, [arc(3.2, 6.8, 3.2, 50, 310)]),
        "k": (4.6, [[(0, 0), (0, 10)], [(0, 7.4), (4.2, 3.6)], [(1.7, 5.9), (4.6, 10)]]),
        "T": (6.4, [[(0, 0), (6.4, 0)], [(3.2, 0), (3.2, 10)]]),
        "h": (5.2, [[(0, 0), (0, 10)], [(0, 6.2)] + arc(2.6, 6.2, 2.6, 180, 360) + [(5.2, 10)]]),
        "e": (6.4, [[(0, 6.8), (6.4, 6.8)], arc(3.2, 6.8, 3.2, 0, -310)]),
        "m": (8.4, [[(0, 3.6), (0, 10)], [(0, 5.7)] + arc(2.1, 5.7, 2.1, 180, 360) + [(4.2, 10)],
                    [(4.2, 5.7)] + arc(6.3, 5.7, 2.1, 180, 360) + [(8.4, 10)]]),
    }


def segment_distance(px, py, ax, ay, bx, by):
    vx, vy, wx, wy = bx - ax, by - ay, px - ax, py - ay
    vv = vx * vx + vy * vy
    t = 0.0 if vv == 0 else max(0.0, min(1.0, (vx * wx + vy * wy) / vv))
    dx, dy = wx - t * vx, wy - t * vy
    return math.sqrt(dx * dx + dy * dy)


def mask(px_per_unit, half):
    """(width, height, rows): rows[y][x] = coverage 0..15 of the word drawn with strokes `half` units thick each
    side, `px_per_unit` pixels to a letter unit; the box is the strokes' outer edge, rounded up to whole pixels."""
    g = glyphs()
    space = 2 * half + 1.5
    pen, segs = half, []
    for ch in TEXT:
        width, strokes = g[ch]
        mine = []
        for line in strokes:
            for (ax, ay), (bx, by) in zip(line, line[1:]):
                mine.append((ax + pen, ay + half, bx + pen, by + half))
        segs.append((pen - half, pen + width + half, mine))
        pen += width + space
    total = pen - space + half
    w, h = int(math.ceil(total * px_per_unit)), int(math.ceil((10 + 2 * half) * px_per_unit))
    rows = [[0] * w for _ in range(h)]
    r = half * px_per_unit
    for x0, x1, mine in segs:
        lines = [tuple(v * px_per_unit for v in s) for s in mine]
        for y in range(h):
            for x in range(max(0, int(x0 * px_per_unit) - 1), min(w, int(x1 * px_per_unit) + 2)):
                hit = 0
                for sy in range(SAMPLES):
                    for sx in range(SAMPLES):
                        qx, qy = x + (sx + 0.5) / SAMPLES, y + (sy + 0.5) / SAMPLES
                        if any(segment_distance(qx, qy, *s) <= r for s in lines):
                            hit += 1
                rows[y][x] = max(rows[y][x], (hit * 15 + SAMPLES * SAMPLES // 2) // (SAMPLES * SAMPLES))
    return w, h, rows


def pot(n):
    p = 8
    while p < n:
        p *= 2
    return p


def texture(px_per_unit, half):
    """(texture width, height, alpha(x, y)) with the word in the middle of a power-of-two picture, and its box."""
    w, h, rows = mask(px_per_unit, half)
    tw, th = pot(w), pot(h)
    ox, oy = (tw - w) // 2, (th - h) // 2

    def alpha(x, y):
        x, y = x - ox, y - oy
        return rows[y][x] if 0 <= x < w and 0 <= y < h else 0
    return tw, th, alpha, (w, h)
