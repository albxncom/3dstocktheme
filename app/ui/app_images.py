"""3DStockTheme's images as 0.9.3 draws them, the application's half of the image generator (art.py, the kit's;
app_art.py is the hook between them): the sprites only this application draws (the Designs and Badges symbols, the
picture's badges, the step marks and the installing screen's tube, rod and chambers, the placeholder name, the scan
frame's corners, its own symbols and glyphs; on the sheet the start screen's word and the carousel's panes), drawn with
the kit's helpers, which this module imports from art. Each look draws them in its material in app_look_<look>.py,
which reaches this module as A and is its LOOK (the hooks house(size, color) and pane_hole(n, rim, pad)). The
developer's views (--sheet, --compare) are here too."""
import importlib
import math
import struct
from pathlib import Path

import art
import schemes
from art import (SCHEME, Box, GLASS_COOL, GLASS_STYLES, Image, L, LIQ_TUBE_SHADE, NO_PAD, SQRT2, Sprite, TO_LIGHT, WHITE,
                 clamp, cover, deeper, fill, glass, glass_house, glass_preset, glass_slab, glass_style, glass_surface,
                 glyph, lerp, lines_of, liq_glass, liq_lit_tube, liq_looks, liq_metal, liq_shade_of, liq_shard,
                 liq_teal, liq_tints, liq_under, look_sprites, path_lines, rgb, sd_circle, sd_poly, sd_strokes,
                 sd_without, shape_lines, smooth, soft, stroke, style_name, symbol, tabler)
from art import sprites as kit_sprites


# Our own drawings. The HOME Menu's house, measured on a screenshot of the console (2026-11-13; nothing taken from
# it): 1.12 wide to 1 high, the roof half the height, the body 73 % of the roof's width, the window 29 % of the
# width and 26 % of the height, from just under the roof line to 20 % of the height above the ground. Filled
# even-odd, with a 0.6 stroke of the same colour for its soft corners.
HOUSE = "M12 4.09L21 11.67V11.75H18.5V20H5.5V11.75H3V11.67ZM9 12.3V17.2H15V12.3Z"


HOUSE_SOFT = 0.6


def house(size, color, light=True):
    if light and hasattr(LOOK, "house"):
        return LOOK.house(size, color)
    img = Image(size, size + 1 if light else size)
    scale = size / 24.0
    lines = path_lines(HOUSE)
    if light:
        k, a = rgb(L.COL_SYMBOL_LIGHT), L.SYMBOL_LIGHT_ALPHA / 100.0
        under = Image(size, size + 1)
        fill(under, lines, scale, k, 0, 1, evenodd=True)
        stroke(under, lines, scale, HOUSE_SOFT * scale, k, 0, 1)
        for j in range(len(img.p)):
            img.p[j] = under.p[j] * a
    fill(img, lines, scale, color, evenodd=True)
    stroke(img, lines, scale, HOUSE_SOFT * scale, color)
    return img


def sd_turned(shape, cx, cy, degrees):
    """shape turned clockwise about cx, cy."""
    c, s = math.cos(math.radians(degrees)), math.sin(math.radians(degrees))
    return lambda u, v: shape(cx + (u - cx) * c + (v - cy) * s, cy - (u - cx) * s + (v - cy) * c)


def seal_shape(lobes=10):
    cx, cy, r = 28.0, 27.5, 21.0
    pts = []
    for k in range(lobes * 12):
        a = 2.0 * math.pi * k / (lobes * 12)
        d = r + 1.7 * math.cos(lobes * a)
        pts.append((cx + d * math.cos(a), cy + d * math.sin(a)))
    star = []
    for k in range(10):
        a, d = math.radians(36 * k - 90), 10.5 if k % 2 == 0 else 4.6
        star.append((cx + d * math.cos(a), cy + 0.6 + d * math.sin(a)))
    return sd_without(sd_poly(pts, 1.2), sd_poly(star, 0.7))


def tile_symbol(name, style, slabs, faint=0.0):
    """A tile's symbol of one or more slabs (shape, colour, the shade inside it, Shard's alpha), the first at the
    bottom. faint: how much of a slab's colour Clear keeps."""
    img = Image(L.MAIN_ICON, L.MAIN_ICON + L.TILE_SYMBOL_DROP)
    for shape, colour, shade, alpha in slabs:
        more = dict(shade=rgb(shade), alpha=alpha) if style == "shard" else dict(faint=faint)
        glass(img, shape, style, "tile", tint=rgb(colour), **more)
    return Sprite(style_name(name, style), img, (0, 0, 0, L.TILE_SYMBOL_DROP))


def designs_symbol(style, faint=None):
    px, py = 28.0, 46.5                 # the pin
    strip = Box(px - 7.5, py - 37.0, px + 7.5, py + 5.5, 3.5).sd
    samples = ((-L.SWATCH_FAN, L.COL_SWATCH_A, L.COL_SWATCH_A_SHADE), (0, L.COL_SWATCH_B, L.COL_SWATCH_B_SHADE),
               (L.SWATCH_FAN, L.COL_SWATCH_C, L.COL_SWATCH_C_SHADE))
    return tile_symbol("DESIGNS", style,
                       [(sd_without(sd_turned(strip, px, py, degrees), sd_circle(px, py, 2.1)), colour, shade, 0.74)
                        for degrees, colour, shade in samples],
                       L.SWATCH_CLEAR_TINT / 100.0 if faint is None else faint)


def badges_symbol(style):
    return tile_symbol("BADGES", style, [(seal_shape(), L.COL_SEAL, L.COL_SEAL_SHADE, 0.58)])


# The picture's two badges, at its lower right corner: a disc of glass with the note (a theme with music) or the
# check (installed). One image size for the three styles, BADGE_PAD px around the disc. Clear: the symbol in white on
# the disc, a soft dark copy 1 px below it. Shard: the same on deep glass, the disc 2 px larger within the image.
# Platin: the symbol cut through the metal, the cut filled with clear glass (a little white and the bands' shine,
# no rim of its own), so the picture still shows through it.
def glass_badge(name, style, n, icon, size, width, down, top, low):
    """n: the disc; icon: its Tabler symbol, size px, its stroke width in the symbol's 24 units, down px below the
    middle; top, low: the badge's colour (Shard's glass)."""
    side = n + 2 * L.BADGE_PAD
    c = side / 2.0
    img = Image(side, side)
    lines = lines_of(tabler(icon))
    if style == "platin":
        big = size + 2
        mark = sd_strokes(lines, big / 24.0, width, c - big / 2.0, c - big / 2.0 + down, 0.5)
        glass(img, sd_circle(c, c, n / 2.0 - 0.6), "clear", "badge", n=n, centre=c, boost=L.PLATIN_CUT_BOOST / 100.0,
              rim_alpha=0.0, inner=0.0, glow=0.0, hair=0.0, shadow=0.0)
        glass(img, sd_without(sd_circle(c, c, n / 2.0), mark), style, "badge", n=n, centre=c)
    else:
        mark = sd_strokes(lines, size / 24.0, width, c - size / 2.0, c - size / 2.0 + down)
        if style == "shard":
            glass(img, sd_circle(c, c, n / 2.0 + 1.0), style, "badge", tint=(top, low), n=n + 2, centre=c, deep=True)
        else:
            glass(img, sd_circle(c, c, n / 2.0), style, "badge", n=n, centre=c)
        shade = deeper(low) if style == "shard" else GLASS_COOL
        img.paint(lambda x, y: (0.45 * cover(mark(x, y - 1.0)), shade))
        img.paint(lambda x, y: (cover(mark(x, y)), WHITE))
    pad = L.BADGE_PAD
    return Sprite(style_name(name, style), img, (pad, pad, pad, pad))


def music_badge(style):
    return glass_badge("MUSIC_BADGE", style, L.GAL_BADGE, "music", L.GAL_BADGE_NOTE, 2.4, 0.5, rgb(L.COL_BADGE_TOP),
                       rgb(L.COL_BADGE_LOW))


def done_badge(style):
    green = rgb(L.COL_GREEN)
    return glass_badge("DONE_BADGE", style, L.DONE_BADGE, "check", L.DONE_CHECK, 3.0, 0.0, lerp(green, WHITE, 0.22),
                       green)


# ---------------------------------------------------------------- carousel (0.9.3, round 14 F3)
# The panes of the list's swarm (layout.h, PANE_*; swarm.h). Per class the rim of glass in the style round the
# picture's own box and corners (ART_MASK's, scaled), with the glass that lies over the picture in its hole (a faint
# veil, a shade along the rim, two faint bands of light) in the same image: picture and pane are drawn into one box
# and scaled together, so they stay concentric at every size. And the white that fills an empty pane's hole, under
# the pane. After round 14's pane_layers (logo_carousel5.py).
PANE_CLASSES = (("PANE_BIG", L.PANE_BIG, L.PANE_BIG_RIM), ("PANE_SMALL", L.PANE_SMALL, L.PANE_SMALL_RIM))


def pane_hole(n, rim, pad):
    """The class's box and the picture's box inside it (its hole), pad px from the image's corner (the look's own:
    its pane_hole hook)."""
    if hasattr(LOOK, "pane_hole"):
        return LOOK.pane_hole(n, rim, pad)
    p = n - 2 * rim
    box = Box(pad, pad, pad + n, pad + n, p * L.DETAIL_ICON_R / 48.0 + rim)
    return box, box.inset(rim, rim, rim, rim)


def pane_rim(name, style, n, rim, tones=None):
    """tones: Shard's two tones, where not COL_SHARD_A into COL_SHARD_B (a look that turns them with its ground)."""
    pad = L.PANE_PAD
    box, hole = pane_hole(n, rim, pad)
    img = Image(n + 2 * pad, n + 2 * pad + 2)
    img.paint(lambda x, y: (0.05 * cover(hole.sd(x, y)), WHITE))
    img.paint(lambda x, y: (0.26 * cover(hole.sd(x, y)) * math.exp(hole.sd(x, y) / 1.8), GLASS_COOL))
    for where, wide, a in ((0.62 * n, 0.10 * n, 0.14), (0.86 * n, 0.035 * n, 0.11)):
        img.paint(lambda x, y, where=where, wide=wide, a=a:
                  (a * cover(hole.sd(x, y)) * cover(abs(x + y - 2 * pad - where) / SQRT2 - wide / 2.0), WHITE))
    more = dict(tint=tones or (rgb(L.COL_SHARD_A), rgb(L.COL_SHARD_B)), diag=(2 * pad + 0.6 * n, 2 * pad + 1.8 * n),
                alpha=0.88) if style == "shard" else {}
    glass(img, lambda x, y: max(box.sd(x, y), -hole.sd(x, y)), style, "icon", n=n, rim=1.1, bands=(), **more)
    return Sprite(name + "_RIM", img, (pad, pad, pad, pad + 2))


def pane_fill(name, n, rim):
    p = n - 2 * rim
    _, hole = pane_hole(n, rim, 0)
    img = Image(p, p)
    img.paint(lambda x, y: (L.PANE_EMPTY_PCT / 100.0 * cover(hole.sd(x + rim, y + rim)), WHITE))
    return Sprite(name + "_FILL", img)


def pane_light():
    """The idle light's strip: PANE_LIGHT px high (the picture's height), the band once across it along x + y, with a
    picture's length of nothing before and after it and its width at both ends, so that a window as large as the
    picture, moved from the strip's right end to its left, sees the band come in at the upper left corner and leave
    at the lower right."""
    p = L.PANE_LIGHT
    wide = L.PANE_LIGHT_WIDE_PCT / 100.0 * p * L.PANE_BIG / (L.PANE_BIG - 2 * L.PANE_BIG_RIM)
    edge = int(math.ceil(wide))
    img = Image(3 * p + 2 * edge, p)
    img.paint(lambda x, y: (L.PANE_LIGHT_PCT / 100.0 * max(0.0, 1.0 - abs(x + y - 2 * p - edge) / wide) ** 1.5, WHITE))
    return Sprite("PANE_LIGHT", img)


def carousel_sprites(style):
    return [f(name, style, n, rim) if f is pane_rim else f(name, n, rim)
            for name, n, rim in PANE_CLASSES for f in (pane_rim, pane_fill)] + [pane_light()]


# The word (round 13): letters of our own, each its width and its strokes in units: capitals reach from y 0 down to
# the baseline at 10, small letters start at 3.6; a stroke is WORD_HALF units thick each side.
def arc(cx, cy, r, a0, a1):
    """Points along a circle from a0 to a1 degrees (0 to the right, 90 down)."""
    n = max(3, int(abs(a1 - a0) / 15.0))
    angles = [math.radians(a0 + (a1 - a0) * k / n) for k in range(n + 1)]
    return [(cx + r * math.cos(a), cy + r * math.sin(a)) for a in angles]


WORD_HALF = 1.45


GLYPHS = {
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


WORD_SPACE = 2 * WORD_HALF + 0.9        # from one letter's strokes to the next one's


WORD_UNIT = 3.0                         # px to a unit of the first line


WORD_PAD = 6                            # the word's image round its letters


# the letters' look per style: Shard nearly opaque, Clear with more white, all with a stronger hairline and shadow
WORD_LOOKS = {"shard": dict(alpha=0.90, hair=0.55, shadow=0.50), "clear": dict(boost=0.14, hair=0.50, shadow=0.45),
              "platin": dict(hair=0.55, shadow=0.50)}


def text_width(text, unit):
    return (sum(GLYPHS[ch][0] for ch in text) + WORD_SPACE * (len(text) - 1) + 2 * WORD_HALF) * unit


def letters(text, unit, x, y, bounce):
    """The letters of text, unit px to a unit, the first one's strokes from x, the capitals' top at y, every other
    letter bounce px lower, the others as much higher; each as (segments, box)."""
    out, pen = [], x + WORD_HALF * unit
    for i, ch in enumerate(text):
        width, lines = GLYPHS[ch]
        dy = bounce if i % 2 == 0 else -bounce
        segs = []
        for line in lines:
            pts = [(pen + px * unit, y + py * unit + dy) for px, py in line]
            segs += [(a[0], a[1], b[0] - a[0], b[1] - a[1]) for a, b in zip(pts, pts[1:])]
        xs = [q[0] for q in segs] + [q[0] + q[2] for q in segs]
        ys = [q[1] for q in segs] + [q[1] + q[3] for q in segs]
        out.append((segs, (min(xs), min(ys), max(xs), max(ys))))
        pen += (width + WORD_SPACE) * unit
    return out


def letter_strokes(placed, half):
    """Letters as one shape: the distance to their strokes less half; further than reach from every letter, just
    "far" (reach leaves room for the glass's shadow to fade)."""
    reach = 8.0 if half <= 3.7 else half + 8.0

    def sd(x, y):
        best = reach * reach
        for segs, (x0, y0, x1, y1) in placed:
            if x < x0 - reach or x > x1 + reach or y < y0 - reach or y > y1 + reach:
                continue
            for ax, ay, vx, vy in segs:
                wx, wy = x - ax, y - ay
                ll = vx * vx + vy * vy
                t = clamp((wx * vx + wy * vy) / ll) if ll else 0.0
                ex, ey = wx - t * vx, wy - t * vy
                best = min(best, ex * ex + ey * ey)
        return math.sqrt(best) - half
    return sd


def shine(x0, x1, y, step):
    """Highlight bands for a line of letters from x0 to x1 about the height y: a wide and a thin one every step px."""
    out, x = [], x0 + 10.0
    while x < x1:
        out += [(x + y, 4.0, 0.50), (x + 9.0 + y, 1.5, 0.40)]
        x += step
    return tuple(out)


def word_lines(style):
    """"3DStock" over "Theme" in a style, the second line's letters larger so that both lines are as wide, their
    left ends in line; each line in an image of the whole word's size (WORD_W x WORD_H), cut to its rows."""
    wide = text_width("3DStock", WORD_UNIT)
    low = wide / text_width("Theme", 1.0)
    pad, half = WORD_PAD, WORD_HALF
    y1 = pad + half * WORD_UNIT + 1
    y2 = y1 + (10 + half) * WORD_UNIT + 3 + half * low
    w, h = int(math.ceil(wide)) + 2 * pad, int(math.ceil(y2 + (10 + half) * low)) + pad + 5
    assert (w, h) == (L.WORD_W, L.WORD_H), (w, h)
    tint = (rgb(L.COL_SHARD_WORD_A), rgb(L.COL_SHARD_WORD_B)) if style == "shard" else None
    out = []
    for name, text, unit, y, row, rows in (("WORD_TOP", "3DStock", WORD_UNIT, y1, L.WORD_TOP_ROW, L.WORD_TOP_H),
                                           ("WORD_LOW", "Theme", low, y2, L.WORD_LOW_ROW, L.WORD_LOW_H)):
        img = Image(w, h)
        mid = y + 5.0 * unit
        look = dict(WORD_LOOKS[style], rim=max(1.0, 0.5 * unit), bands=shine(pad, w - pad, mid, 19.0 * unit),
                    diag=(pad + mid, w - pad + mid))
        body, look, slab_only = glass_style(style, tint, glass_preset("icon", 10.0 * unit), **look)
        glass_slab(img, letter_strokes(letters(text, unit, pad, y, 0.35 * unit), half * unit), 1.0, body,
                   **slab_only, **look)
        cut = Image(w, rows)
        cut.p = img.p[4 * w * row:4 * w * (row + rows)]
        if any(img.p[4 * k + 3] >= 0.5 / 255 for k in range(w * h) if not row <= k // w < row + rows):
            raise SystemExit("the word's line %s reaches past its rows" % text)
        out.append(Sprite(name, cut))
    return out


def step_later():
    """A step of the install still to come: the flat small circle, an outline on the card's colour. (The step under
    way and the finished one are glass: step_bead, step_done.)"""
    size = L.STEP_DROP_LATER
    img = Image(size, size)
    b = Box(0, 0, size, size, size / 2.0)
    inner = b.inset(1.5, 1.5, 1.5, 1.5)
    img.paint(lambda x, y: (cover(b.sd(x, y)), rgb(L.COL_CARD)))
    img.paint(lambda x, y: (max(0.0, cover(b.sd(x, y)) - cover(inner.sd(x, y))), rgb(L.COL_DOTS)))
    return Sprite("STEP_DROP_LATER", img)


def step_bead(style):
    """The step under way as a raised bead of glass in a style: round, STEP_DROP_NOW px, no yellow. Clear lets the
    card through; Shard is the tone of the mark still to come (its ring's colour), deeper and nearly opaque; Platin
    its plain metal. Over the glass a soft light on the upper left and a shade towards the lower right, so the disc
    reads as a bead. The image is BADGE_PAD px larger all round."""
    n, pad = L.STEP_DROP_NOW, L.BADGE_PAD
    side = n + 2 * pad
    c, r, tone = side / 2.0, n / 2.0, rgb(L.COL_DOTS)
    disc = sd_circle(c, c, r)
    img = Image(side, side)
    glass(img, disc, style, "badge", tint=tone, n=n, centre=c, deep=style == "shard")
    light, shade = L.STEP_BEAD_LIGHT / 100.0, L.STEP_BEAD_SHADE / 100.0
    lx, ly = c - 0.20 * n, c - 0.24 * n

    def dome(x, y):
        inside = cover(disc(x, y) + 1.5)                # within the rim
        if inside <= 0.0:
            return None
        return (light * inside * math.exp(-((x - lx) ** 2 + (y - ly) ** 2) / (2 * (0.17 * n) ** 2)), WHITE)

    def under(x, y):
        inside = cover(disc(x, y) + 1.5)
        if inside <= 0.0:
            return None
        return (shade * inside * smooth(0.15, 0.95, ((x - c) * 0.5 + (y - c) * 0.866) / r),
                rgb(L.COL_GLASS_SHADOW))
    img.paint(under)
    img.paint(dome)
    return Sprite(style_name("STEP_NOW_GLASS", style), img, (pad, pad, pad, pad))


def step_done(style):
    """A finished step: the check in the style's check badge make (done_badge), STEP_DROP_DONE px."""
    green, n = rgb(L.COL_GREEN), L.STEP_DROP_DONE
    return glass_badge("STEP_DONE_GLASS", style, n, "check", n * L.DONE_CHECK / L.DONE_BADGE, 3.0, 0.0,
                       lerp(green, WHITE, 0.22), green)


def dashed(name, w, h, r):
    """A placeholder: the outline of a rounded box in dashes, evenly spread so that they close up."""
    width = L.EMPTY_STROKE_10 / 10.0
    img = Image(w, h)
    (pts, _), = shape_lines(("rect", width / 2, width / 2, w - width, h - width, r - width / 2))
    pts = pts + [pts[0]]
    lengths = [0.0]
    for a, b in zip(pts, pts[1:]):
        lengths.append(lengths[-1] + math.hypot(b[0] - a[0], b[1] - a[1]))
    total = lengths[-1]
    n = max(1, round(total / (L.EMPTY_DASH + L.EMPTY_DASH_GAP)))
    period = total / n
    on = period * L.EMPTY_DASH / (L.EMPTY_DASH + L.EMPTY_DASH_GAP)

    def point(at):
        for k in range(1, len(lengths)):
            if at <= lengths[k]:
                a, b = pts[k - 1], pts[k]
                f = (at - lengths[k - 1]) / ((lengths[k] - lengths[k - 1]) or 1)
                return (a[0] + (b[0] - a[0]) * f, a[1] + (b[1] - a[1]) * f)
        return pts[-1]
    lines = []
    for i in range(n):
        start = i * period
        lines.append(([point(start + on * j / 8) for j in range(9)], False))
    stroke(img, lines, 1.0, width, rgb(L.COL_MUTED))
    return Sprite(name, img)


def name_cap():
    """The cap height of the list's and the check's name (GAL_NAME_PX in the system font, FONT_CAP at FONT_PX), as
    whole pixels. The dashed placeholder stands where the name's capitals will be and is EMPTY_NAME_MORE px taller:
    EMPTY_NAME_BELOW px of it under their baseline, the rest above them."""
    return round(L.FONT_CAP * L.GAL_NAME_PX / L.FONT_PX)


def name_box():
    return name_cap() + L.EMPTY_NAME_MORE


def bracket(name, path):
    """One corner of the scan frame, cut out of the prototype's 150-unit drawing."""
    reach = L.BRACKET_INSET + L.BRACKET_ARM + L.BRACKET_STROKE
    lines = path_lines(path)
    xs = [p[0] for pts, _ in lines for p in pts]
    ys = [p[1] for pts, _ in lines for p in pts]
    ox = 0 if min(xs) < L.BRACKET / 2 else L.BRACKET - reach
    oy = 0 if min(ys) < L.BRACKET / 2 else L.BRACKET - reach
    img = Image(reach, reach)
    stroke(img, lines, 1.0, L.BRACKET_STROKE, rgb(L.COL_BG), -ox, -oy)
    return Sprite(name, img)


LIQ_WALL, LIQ_NECK, LIQ_RING = 1.4, 5.0, 4.8     # the tube's wall, the liquid neck of a chamber, P2's ring (px)


def liq_smin(a, b, k):
    """The smaller of two distances, the corner between the two shapes filled in as a liquid's neck (k px)."""
    h = max(k - abs(a - b), 0.0) / k
    return min(a, b) - h * h * k * 0.25


def liq_joined(shape, own, along, weight):
    """The shade of a mark that runs out into the tube on both sides: its own at the mark, the tube's along the tube
    (weight: 0 at the mark, 1 on the tube)."""
    a, b = liq_shade_of(shape, **own), liq_shade_of(shape, **along)

    def at(x, y):
        w = weight(x, y)
        return b(x, y) if w >= 1.0 else a(x, y) if w <= 0.0 else a(x, y) + (b(x, y) - a(x, y)) * w
    return at


def liq_between(a, b, t):
    if t <= 0.0:
        return a
    if t >= 1.0:
        return b
    return {k: (lerp(a[k], b[k], t) if isinstance(a[k], tuple) else a[k] + (b[k] - a[k]) * t) for k in a}


def liq_lit_round(cx, cy, r):
    return lambda x, y: ((x - cx) * TO_LIGHT[0] + (y - cy) * TO_LIGHT[1]) / r


def liq_check(img, n, c):
    """The white check of a done step, sized as step_done sizes it for a disc of n px about c, with a teal copy 1 px
    lower."""
    size = n * L.DONE_CHECK / L.DONE_BADGE
    mark = sd_strokes(lines_of(tabler("check")), size / 24.0, 3.0, c[0] - size / 2.0, c[1] - size / 2.0)
    img.paint(lambda x, y: (0.45 * cover(mark(x, y - 1.0)), rgb(L.COL_TUBE_FILL_LOW)))
    img.paint(lambda x, y: (cover(mark(x, y)), WHITE))


def liq_core(name, r, liquid_r, look):
    """The teal liquid of a done mark alone, under its glass's lights: it wells up from the middle while a step
    becomes done (drawn scaled between the mark under way and the done one)."""
    side = int(2 * r)
    c = side / 2.0
    img = Image(side, side)
    liq_glass(img, sd_circle(c, c, r), lambda x, y: look, liquid=sd_circle(c, c, liquid_r),
              lit=liq_lit_round(c, c, liquid_r), only_liquid=True)
    return Sprite(name, img)


def liq_tube():
    """C1's and P2's line: the empty glass tube and the liquid in it, each stretching across, the tube's middle
    STEP_TUBE_H / 2 + 2 px below the image's top."""
    _, look = liq_looks()
    top, low, half = 2, 5, L.STEP_TUBE_H / 2.0
    cy, h = top + half, top + L.STEP_TUBE_H + low
    img = Image(14, h)
    shape = Box(0.0, top, 14.0, top + L.STEP_TUBE_H, half).sd
    liq_under(img, shape, liq_shade_of(shape, **LIQ_TUBE_SHADE))
    liq_glass(img, shape, lambda x, y: look)
    fill = Image(14, h)
    inner = half - LIQ_WALL
    liquid = Box(LIQ_WALL, cy - inner, 14.0 - 0.6, cy + inner, inner).sd         # round at both ends: the meniscus
    whole = Box(0.0, top, 60.0, top + L.STEP_TUBE_H, half).sd
    liq_glass(fill, whole, lambda x, y: look, liquid=liquid, lit=liq_lit_tube(cy, inner), only_liquid=True)
    return [Sprite("STEP_TUBE", img, (0, top, 0, low), (6, 0, 6, 0)),
            Sprite("STEP_TUBE_FILL", fill, (0, top, 0, low), (5, 0, 6, 0))]


def liq_chambers():
    """C1, Clear: the tube swells into a chamber at each step. To come a small flat chamber; under way a thick clear
    bead; done the bead full of teal liquid with the check; and the liquid alone. Each mark's image reaches along
    the tube on both sides and covers it."""
    bead_look, tube_look = liq_looks()
    flat_look = dict(tube_look, dark=0.14, bright=0.14, rim_lit=0.50, rim_side=0.26)
    half = L.STEP_TUBE_H / 2.0

    def mark(name, r, reach, look, shade, liquid_r=0.0, check=False):
        w, h = int(2 * reach), int(2 * (r + L.BADGE_PAD))
        cx, cy = w / 2.0, h / 2.0
        img = Image(w, h)
        run = lambda x, y: abs(y - cy) - half
        disc = sd_circle(cx, cy, r)
        shape = lambda x, y: liq_smin(disc(x, y), run(x, y), LIQ_NECK)
        far = lambda x, y: smooth(r - 1.5, r + 2.5, math.hypot(x - cx, y - cy))
        liq_under(img, shape, liq_joined(shape, shade, LIQ_TUBE_SHADE, far), liq_shade_of(run, **LIQ_TUBE_SHADE))
        look_at = lambda x, y: liq_between(look, tube_look, far(x, y))
        liquid = lit = None
        if liquid_r:
            inside, wet = sd_circle(cx, cy, liquid_r), lambda x, y: abs(y - cy) - (half - LIQ_WALL)
            liquid = lambda x, y: liq_smin(inside(x, y), wet(x, y), LIQ_NECK - 1.0)
            round_, along = liq_lit_round(cx, cy, liquid_r), liq_lit_tube(cy, half - LIQ_WALL)
            lit = lambda x, y: round_(x, y) + (along(x, y) - round_(x, y)) * far(x, y)
        liq_glass(img, shape, look_at, liquid=liquid, lit=lit, bead=(cx, cy, r))
        if check:
            liq_check(img, 2 * r, (cx, cy))
        return Sprite(name, img, (int(reach - r), L.BADGE_PAD) * 2)
    r_later, r_now, r_done = L.STEP_DROP_LATER / 2.0, L.STEP_DROP_NOW / 2.0, L.STEP_DROP_DONE / 2.0
    return [mark("STEP_CHAMBER_LATER", r_later, r_later + 6, flat_look, LIQ_TUBE_SHADE),
            mark("STEP_CHAMBER_NOW", r_now, r_now + 5, bead_look, dict(shadow=0.50, hair=0.40, drop=2.5, blur=4.0)),
            mark("STEP_CHAMBER_DONE", r_done, r_done + 5, bead_look, dict(shadow=0.40, hair=0.36, drop=2.0, blur=3.0),
                 liquid_r=r_done - 3.0, check=True),
            liq_core("STEP_CHAMBER_CORE", r_done, r_done - 3.0, bead_look)]


def liq_settings():
    """P2, Platin: C1's tube with a glass window in a platinum ring at each step. To come a thin dull ring; under way
    the ring raised and thick, its window clear; done the window full of teal liquid with the check."""
    bead_look, tube_look = liq_looks()
    window_look = dict(bead_look, dark=0.60, dark_w=3.2, spec=0.70, spec_at=2.6, rim_lit=0.55, rim_side=0.20)
    half = L.STEP_TUBE_H / 2.0

    def mark(name, r, ring, raised, liquid=False, check=False):
        reach = r + 5
        w, h = int(2 * reach), int(2 * (r + L.BADGE_PAD))
        cx, cy = w / 2.0, h / 2.0
        img = Image(w, h)
        run = lambda x, y: abs(y - cy) - half
        disc = sd_circle(cx, cy, r)
        whole = lambda x, y: min(disc(x, y), run(x, y))
        shade = dict(shadow=0.44, hair=0.40, drop=2.5, blur=3.5) if raised else LIQ_TUBE_SHADE
        far = lambda x, y: smooth(r, r + 4.0, math.hypot(x - cx, y - cy))
        liq_under(img, whole, liq_joined(whole, shade, LIQ_TUBE_SHADE, far), liq_shade_of(run, **LIQ_TUBE_SHADE))
        liq_glass(img, run, lambda x, y: tube_look, liquid=(lambda x, y: abs(y - cy) - (half - LIQ_WALL))
                  if liquid else None, lit=liq_lit_tube(cy, half - LIQ_WALL))
        pane = sd_circle(cx, cy, r - ring + 0.6)
        pane_look = window_look if raised else dict(window_look, dark=0.22, bright=0.2, spec=0.0)
        liq_glass(img, pane, lambda x, y: pane_look, liquid=pane if liquid else None,
                  lit=liq_lit_round(cx, cy, r - ring))
        band = lambda x, y: abs(math.hypot(x - cx, y - cy) - (r - ring / 2.0)) - ring / 2.0
        liq_metal(img, band, lambda x, y, e, nx, ny: ((x - cx) / r * 0.9, (y - cy) / r * 0.9, 0.06), bow=0.10,
                  spot=(cx + TO_LIGHT[0] * (r - ring / 2.0), cy + TO_LIGHT[1] * (r - ring / 2.0), 1.4) if raised
                  else None)
        if not raised:                  # to come: the ring dull, half sunk into the card's colour
            img.paint(lambda x, y: (0.42 * cover(band(x, y)), rgb(L.COL_CARD)))
        if check:
            liq_check(img, 2 * (r - LIQ_RING) + 3, (cx, cy))
        return Sprite(name, img, (5, L.BADGE_PAD) * 2)
    r_later, r_now, r_done = L.STEP_DROP_LATER / 2.0, L.STEP_DROP_NOW / 2.0, L.STEP_DROP_DONE / 2.0
    return [mark("STEP_SETTING_LATER", r_later, 2.2, False),
            mark("STEP_SETTING_NOW", r_now, LIQ_RING, True),
            mark("STEP_SETTING_DONE", r_done, LIQ_RING, True, liquid=True, check=True),
            liq_core("STEP_SETTING_CORE", r_done - LIQ_RING + 0.6, r_done - LIQ_RING + 0.6, window_look)]


def liq_rod():
    """S1, Shard: the line as a rod of Shard glass, pale; the stretch behind the progress teal glass, its front cut
    on a slant (the top leading)."""
    top, low, n = 2, 4, L.STEP_TUBE_H
    h = top + n + low
    pale, teal = liq_tints()
    img = Image(14, h)
    body, look, slab = liq_shard(pale, False, 0.62, y0=top, y1=top + n)
    glass_slab(img, Box(0.0, top, 60.0, top + n, 2.0).sd, 1.0, body, **slab, **look)
    track = Sprite("STEP_ROD", img, (0, top, 0, low), (6, 0, 2, 0))
    slant = sd_poly([(-40.0 + 1.5, top + 1.5), (20.0 - 2.5, top + 1.5), (20.0 - 7.5, top + n - 1.5),
                     (-40.0 + 1.5, top + n - 1.5)], 1.5)
    left = Box(0.0, top, 60.0, top + n, 2.0).sd
    img = Image(20, h)
    body, look, slab = liq_shard(teal, True, y0=top, y1=top + n)
    glass_slab(img, lambda x, y: max(left(x, y), slant(x, y)), 1.0, body, **slab, **look)
    return [track, Sprite("STEP_ROD_FILL", img, (0, top, 0, low), (6, 0, 12, 0))]


# ---------------------------------------------------------------- the application's sprites

def sprites():
    """The application's sprites of the atlas as 0.9.3 draws them, in the order of their ids (after the kit's)."""
    teal, orange, folder, muted, ink = L.COL_TEAL, L.COL_ORANGE, L.COL_FOLDER, L.COL_MUTED, L.COL_INK
    i, a, b = L.BRACKET_INSET, L.BRACKET_ARM, L.BRACKET - L.BRACKET_INSET

    def sym(name, icon, size, color, width, light=True, slash=False):
        """A sprite of the Tabler icon named icon."""
        return Sprite(name, symbol(tabler(icon), size, rgb(color), width, light=light, slash=slash),
                      (0, 0, 0, 1) if light else NO_PAD)

    def named(name, sprite):
        sprite.name = name
        return sprite
    return [
        step_later(),
        step_bead("shard"),
        step_done("shard"),
        sym("FOLDER_HERO", "folder", L.GAL_FOLDER, folder, 1.8),
        sym("STEPS_TITLE", "download", L.STEPS_TITLE_ICON, teal, 2.2, light=False),
        dashed("EMPTY_NAME", L.EMPTY_NAME_W, name_box(), name_box() // 2),
        *(designs_symbol(style) for style in GLASS_STYLES),
        *(badges_symbol(style) for style in GLASS_STYLES),
        sym("FOLDER_BIG", "folder", L.DETAIL_ICON, folder, 1.8),
        named("FOLDER_FOOT", glyph(tabler("folder"), L.CAP_LINE, rgb(muted), 2.2)),
        *(music_badge(style) for style in GLASS_STYLES),
        named("MUSIC_ROW", glyph(tabler("music"), L.CAP_ROW_SUB, rgb(muted), 2.6, light=True)),
        *(done_badge(style) for style in GLASS_STYLES),
        sym("POWER_OFF", "power", L.SAFE_ICON, orange, 1.8, slash=True),
        sym("SD_OFF", "device-sd-card", L.SAFE_ICON, orange, 1.8, slash=True),
        named("HOME", glyph([("path", HOUSE)], L.CAP_WAY, rgb(ink), HOUSE_SOFT, filled=True)),
        named("HOME_FOOT", glyph([("path", HOUSE)], L.CAP_LINE, rgb(muted), HOUSE_SOFT, filled=True)),
        named("CHEVRON", glyph(tabler("chevron-right"), L.CAP_WAY, rgb(muted), 2.6)),
        sym("FACT_CHECK", "check", L.FACT_ICON, L.COL_GREEN, 2.6),
        sym("FACT_FILE", "file", L.FACT_ICON, ink, 2.0),
        Sprite("FACT_HOME", house(L.FACT_ICON, rgb(ink)), (0, 0, 0, 1)),
        sym("GLOBE", "world", L.GLOBE, muted, 1.8),
        sym("GLOBE_SMALL", "world", L.SMALL_ICON, muted, 1.8),
        sym("QR_SCAN", "qrcode", L.SCAN_ICON, teal, 1.8),
        sym("WARN_ROW", "alert-triangle", L.ROW_WARN, orange, 2),
        sym("INFO_SD", "device-sd-card", L.INFO_ICON, muted, 1.8),
        bracket("BRACKET_TL", "M{0} {1}V{0}h{2}".format(i, i + a, a)),
        bracket("BRACKET_TR", "M{0} {1}h{2}v{2}".format(b - a, i, a)),
        bracket("BRACKET_BR", "M{0} {1}v{2}h-{2}".format(b, b - a, a)),
        bracket("BRACKET_BL", "M{0} {1}H{2}v-{3}".format(i + a, b, i, a)),
        # installing, 0.9.3 (round 12)
        *liq_tube(), *liq_chambers(), *liq_settings(), *liq_rod(),
    ]


def sheet_sprites(style):
    """The application's sprites of one style's glass sheet, in the order of their ids (after the kit's): the start
    screen's word, the carousel's panes."""
    return word_lines(style) + carousel_sprites(style)


# ---------------------------------------------------------------- the contact sheet


def spans(size, a, b, total):
    if not a and not b:
        return [(0, size, 0, size)]
    return [(0, a, 0, a), (a, size - b, a, total - b), (size - b, size, total - b, total)]


def compose(s, w, h):
    """The sprite drawn for an element of w x h, as the application draws it (nearest texels)."""
    W, H = w + s.pad[0] + s.pad[2], h + s.pad[1] + s.pad[3]
    if not s.cap[0] and not s.cap[2]:
        W = s.img.w
    if not s.cap[1] and not s.cap[3]:
        H = s.img.h
    out = Image(W, H)
    for sx0, sx1, dx0, dx1 in spans(s.img.w, s.cap[0], s.cap[2], W):
        for sy0, sy1, dy0, dy1 in spans(s.img.h, s.cap[1], s.cap[3], H):
            for dy in range(dy0, dy1):
                sy = sy0 + int((dy - dy0 + 0.5) * (sy1 - sy0) / (dy1 - dy0))
                for dx in range(dx0, dx1):
                    sx = sx0 + int((dx - dx0 + 0.5) * (sx1 - sx0) / (dx1 - dx0))
                    j, k = 4 * (sy * s.img.w + sx), 4 * (dy * W + dx)
                    out.p[k:k + 4] = s.img.p[j:j + 4]
    return out


def sheet(path):
    items = {s.name: s for half in look_sprites() for s in half}
    scale, width, margin = 2, 680, 12
    placed, x, y, row = [], margin, margin, 0

    def put(img, dx=0, dy=0):
        nonlocal x, y, row
        if x + img.w > width - margin:
            x, y, row = margin, y + row + margin, 0
        placed.append((img, x + dx, y + dy))
        x += img.w + margin
        row = max(row, img.h)

    def newline():
        nonlocal x, y, row
        x, y, row = margin, y + row + 2 * margin, 0

    for s in items.values():
        put(s.img)
    newline()

    def drawn(parts, w, h):
        """Element sprites drawn over each other for one w x h element, as the application layers them."""
        pad = [max(items[n].pad[k] for n in parts) for k in range(4)]
        img = Image(w + pad[0] + pad[2], h + pad[1] + pad[3])
        for n in parts:
            s = items[n]
            img.blit(compose(s, w, h), pad[0] - s.pad[0], pad[1] - s.pad[1])
        return img
    for parts, w, h in ((("TILE",), L.TILE_W, L.TILE_H), (("TILE_DOWN",), L.TILE_W, L.TILE_H),
                        (("TILE_OFF",), L.TILE_W, L.TILE_H), (("CHOICE",), L.TILE_W, L.CHOICE_H),
                        (("ROW",), L.LIST_W, L.ROW_H), (("ROW_OFF",), L.LIST_W, L.ROW_H),
                        (("FOLDER_ROW",), L.LIST_W, L.FOLDER_H), (("GO",), L.GO_W, L.GO_H),
                        (("BAR",), L.BOTTOM_W, L.BAR_H), (("BAR_DOWN",), L.BOTTOM_W, L.BAR_H),
                        (("CARD",), L.TOP_W - 2 * L.TOP_PAD, L.CARD_H),
                        (("TRACK",), L.SCROLL_W, L.LIST_BOTTOM - L.SCROLL_Y), (("CORNER",), 120, L.CORNER_H),
                        (("CORNER_DOWN",), 120, L.CORNER_H), (("CORNER_OFF",), L.CORNER_W, L.CORNER_H),
                        (("DLG",), L.DLG_W, L.DLG_H), (("DLG_BUTTON",), L.DLG_W, L.DLG_BTN_H + 1),
                        (("DLG_BUTTON_DOWN",), L.DLG_W, L.DLG_BTN_H + 1),
                        (("THUMB",), L.SCROLL_W, 118), (("PROGRESS",), 336, L.PROGRESS_H),
                        (("PROGRESS_FILL",), 208, L.PROGRESS_H),
                        (("START",), 40, L.START_H)):
        put(drawn(parts, w, h))
    height = y + row + margin
    canvas = Image(width * scale, height * scale, rgb(L.COL_BG))
    for img, px, py in placed:
        canvas.blit(img, px * scale, py * scale, scale)
    Path(path).write_bytes(canvas.png(opaque=True))


def read_bmp(path):
    """A 24-bit BMP (the console's screenshots) as an opaque Image."""
    data = Path(path).read_bytes()
    off = struct.unpack_from("<I", data, 10)[0]
    w, h = struct.unpack_from("<ii", data, 18)
    if struct.unpack_from("<H", data, 28)[0] != 24:
        raise SystemExit("not a 24-bit BMP: " + str(path))
    stride = (w * 3 + 3) & ~3
    img = Image(w, abs(h))
    for y in range(abs(h)):
        base = off + (abs(h) - 1 - y if h > 0 else y) * stride
        for x in range(w):
            k = 4 * (y * w + x)
            img.p[k:k + 4] = [float(data[base + 3 * x + 2]), float(data[base + 3 * x + 1]), float(data[base + 3 * x]),
                              1.0]
    return img


def start_menu(pressed, bar_held=False):
    """The start screen's bottom screen as the application draws it, without text: the background, the two tiles
    with their symbols and the focus ring, the bar; pressed: the left tile held down; bar_held: the bar held down,
    with its START key."""
    items = {s.name: s for s in kit_sprites() + sprites()}
    img = Image(L.BOTTOM_W, L.SCREEN_H)
    top, low = rgb(L.COL_BOT_BG_A), rgb(L.COL_BOT_BG_B)
    img.paint(lambda x, y: (1.0, lerp(top, low, y / L.SCREEN_H)))

    def put(name, x, y, w=None, h=None):
        s = items[name]
        el_w = w if w is not None else s.img.w - s.pad[0] - s.pad[2]
        el_h = h if h is not None else s.img.h - s.pad[1] - s.pad[3]
        img.blit(compose(s, el_w, el_h), x - s.pad[0], y - s.pad[1])
    for k, (x, symbol_name) in enumerate(((L.TILE_X1, "DESIGNS"), (L.TILE_X2, "BADGES"))):
        held = pressed and k == 0
        put("TILE_DOWN" if held else "TILE", x, L.TILE_Y, L.TILE_W, L.TILE_H)
        top_y = L.TILE_Y + (L.TILE_H - (L.MAIN_ICON + L.TILE_GAP + L.MAIN_LH)) // 2 + (1 if held else 0)
        put(symbol_name, x + (L.TILE_W - L.MAIN_ICON) // 2, top_y)
    put("BAR_DOWN" if bar_held else "BAR", 0, L.BAR_Y)
    if bar_held:
        put("START_DOWN", 116, L.BAR_Y + (L.BAR_H - L.START_H) // 2, 40, L.START_H)
    return img


def compare(path, ref_normal, ref_held, ref_bar=None):
    """Ours next to the System Settings, three times the size: the start screen's bottom screen plain, with the left
    tile held and (with ref_bar) with the bar held, then the bar's left corner six times the size and the scroll
    bar's thumb at rest and held (no console picture of that yet)."""
    ours, held = start_menu(False), start_menu(True)
    ref_n, ref_h = read_bmp(ref_normal), read_bmp(ref_held)
    pairs = [(ours, ref_n), (held, ref_h)]
    if ref_bar:
        pairs.append((start_menu(False, bar_held=True), read_bmp(ref_bar)))
    gap, s = 12, 3
    corner = (0, L.BAR_Y - 14, 64, L.SCREEN_H)

    def crop(src, box):
        x0, y0, x1, y1 = box
        out = Image(x1 - x0, y1 - y0)
        for y in range(y0, y1):
            out.p[4 * (y - y0) * out.w:4 * (y - y0 + 1) * out.w] = src.p[4 * (y * src.w + x0):4 * (y * src.w + x1)]
        return out
    cw, ch = (corner[2] - corner[0]) * 2 * s, (corner[3] - corner[1]) * 2 * s
    width = 2 * L.BOTTOM_W * s + 3 * gap
    height = len(pairs) * (L.SCREEN_H * s + gap) + ch + 2 * gap
    canvas = Image(width, height, (0x24, 0x22, 0x1D))
    for row, (a, b) in enumerate(pairs):
        y = gap + row * (L.SCREEN_H * s + gap)
        canvas.blit(a, gap, y, s)
        canvas.blit(b, 2 * gap + L.BOTTOM_W * s, y, s)
    y = gap + len(pairs) * (L.SCREEN_H * s + gap)
    canvas.blit(crop(ours, corner), gap, y, 2 * s)
    canvas.blit(crop(ref_n, corner), 2 * gap + cw, y, 2 * s)
    items = {k.name: k for k in kit_sprites()}
    for n, name in enumerate(("THUMB", "THUMB_DOWN")):
        thumb_img = Image(L.SCROLL_W + 8, 70, rgb(L.COL_BOT_BG_A))
        thumb_img.blit(compose(items[name], L.SCROLL_W, 60), 4 - items[name].pad[0], 4 - items[name].pad[1])
        canvas.blit(thumb_img, 3 * gap + 2 * cw + n * (thumb_img.w * s + gap), y, s)
    Path(path).write_bytes(canvas.png(opaque=True))


# ---------------------------------------------------------------- the start screen's turning house
# The part's pictures (app_art.py, pictures): per look and scheme one texture, LOGO_SHEET px square, of the house's
# LOGO_FRAMES pictures; drawn by app_ui.c's start_logo.


def house_glass(style):
    """How the house follows the setting: (the glass, its look, the still mark's hairline and shadow, the colour of
    the cut edge when the slab is seen from the side). Shard: violet running into teal along the diagonal, with a
    stronger hairline and shadow."""
    more = dict(diag=(34.0, 92.0), hair=0.55, shadow=0.50) if style == "shard" else {}
    body, look, slab_only = glass_style(style, (rgb(L.COL_SHARD_A), rgb(L.COL_SHARD_B)), glass_preset("logo"), **more)
    edge = {"clear": WHITE, "shard": rgb(L.COL_SHARD_EDGE), "platin": rgb(L.COL_PLATIN_EDGE)}[style]
    return body, look, slab_only, edge


GLASSES = tuple(house_glass(style) for style in GLASS_STYLES)


# The turn. The slab is THICK * 2 units thick and turns about the vertical line through the middle of its frame, seen
# without perspective. It is rasterised by spans: in each of TURN_FINE lines per pixel row the exact part of every
# pixel that the face, the cut edge and the back cover, so its edges are anti-aliased as every other image's.
# The house's numbers (HERO_ICON, LOGO_THICK_10, LOGO_FRAMES, LOGO_STEP, LOGO_SWEEP) are the application's (layout.h).
TURN_N = L.HERO_ICON
TURN_FINE = 4
TURN_THICK = L.LOGO_THICK_10 / 10.0
assert L.LOGO_FRAMES * L.LOGO_STEP == 180 and (L.LOGO_SHEET // TURN_N) ** 2 >= L.LOGO_FRAMES


def blurred(src, sigma, dy=0):
    """src (TURN_N x TURN_N) blurred by a Gaussian, moved down dy px."""
    n, reach = TURN_N, int(math.ceil(3 * sigma))
    k = [math.exp(-0.5 * (i / sigma) ** 2) for i in range(-reach, reach + 1)]
    total = sum(k)
    k = [v / total for v in k]
    tmp = [0.0] * (n * n)
    for y in range(n):
        row = src[y * n:(y + 1) * n]
        if not any(row):
            continue
        for x in range(n):
            acc = 0.0
            for i, w in enumerate(k):
                j = x + i - reach
                if 0 <= j < n:
                    acc += row[j] * w
            tmp[y * n + x] = acc
    out = [0.0] * (n * n)
    for y in range(n):
        for i, w in enumerate(k):
            j = y - dy + i - reach
            if 0 <= j < n:
                for x in range(n):
                    v = tmp[j * n + x]
                    if v:
                        out[y * n + x] += v * w
    return out


def merged(spans):
    out = []
    for a, b in sorted(spans):
        if out and a <= out[-1][1]:
            out[-1] = (out[-1][0], max(out[-1][1], b))
        else:
            out.append((a, b))
    return out


def add_spans(cov, base, spans, weight):
    """Adds to a row of cov (from base) the part of each pixel that the spans cover, times weight."""
    for a, b in spans:
        for px in range(max(0, int(a)), min(TURN_N, int(math.ceil(b)))):
            part = min(b, px + 1.0) - max(a, float(px))
            if part > 0.0:
                cov[base + px] += part * weight


class Turner:
    """The house's slab, turning: its shape on a fine grid (shared by every glass), and a picture for an angle."""

    def __init__(self):
        shape, n = glass_house(), TURN_N * TURN_FINE
        self.grid = [[shape(i / TURN_FINE, (j + 0.5) / TURN_FINE) for i in range(n + 1)] for j in range(n)]
        self.rows = []                      # for each line: where the glass is, from x to x
        for g in self.grid:
            spans, start = [], None
            for i in range(n):
                a, b = g[i], g[i + 1]
                if (a < 0.0) != (b < 0.0):
                    x = (i + a / (a - b)) / TURN_FINE
                    if a >= 0.0:
                        start = x
                    elif start is not None:
                        spans.append((start, x))
                        start = None
            self.rows.append(spans)

    def sd(self, x, y):
        """The shape's signed distance, read from the grid."""
        fx, fy = x * TURN_FINE, y * TURN_FINE - 0.5
        n = TURN_N * TURN_FINE
        if not (0.0 <= fx < n and 0.0 <= fy < n - 1):
            return 4.0
        i, j = int(fx), int(fy)
        tx, ty = fx - i, fy - j
        g0, g1 = self.grid[j], self.grid[j + 1]
        return (g0[i] * (1 - tx) + g0[i + 1] * tx) * (1 - ty) + (g1[i] * (1 - tx) + g1[i + 1] * tx) * ty

    def picture(self, glass, index):
        """Picture index of LOGO_FRAMES: the slab at -90 + index * LOGO_STEP degrees (LOGO_FRAMES / 2: face-on)."""
        body, look, _, edge = GLASSES[glass]
        turn = math.radians(-90 + index * L.LOGO_STEP)
        N, thick = TURN_N, TURN_THICK
        c, s = max(0.0, math.cos(turn)), math.sin(turn)
        mid, hs, w = N / 2.0, thick * abs(s), 1.0 / TURN_FINE
        sil, back, face, wall_out, wall_in = ([0.0] * (N * N) for _ in range(5))
        for j, spans in enumerate(self.rows):
            if not spans:
                continue
            base = (j // TURN_FINE) * N
            flat = [(mid + (a - mid) * c, mid + (b - mid) * c) for a, b in spans]
            add_spans(sil, base, merged([(a - hs, b + hs) for a, b in flat]), w)
            add_spans(face, base, [(a + thick * s, b + thick * s) for a, b in flat], w)
            add_spans(back, base, [(a - thick * s, b - thick * s) for a, b in flat], w)
            # the cut edge that faces us: the left one of each piece while s > 0, the right one otherwise;
            # its half next to the back is shaded
            if s > 0:
                add_spans(wall_out, base, merged([(a - hs, a) for a, _ in flat]), w)
                add_spans(wall_in, base, merged([(a, a + hs) for a, _ in flat]), w)
            else:
                add_spans(wall_in, base, merged([(b - hs, b) for _, b in flat]), w)
                add_spans(wall_out, base, merged([(b, b + hs) for _, b in flat]), w)
        shade = look.get("shade", GLASS_COOL)
        rim = max(1.0, look.get("rim", 2.3))
        lit = s > 0                                             # the cut edge faces the light
        edge_in = edge if lit else lerp(edge, shade, 0.16)
        edge_out = lerp(edge, shade, 0.24 if lit else 0.36)
        glint = 0.20 * math.exp(-((math.degrees(turn) + 24.0) / 9.0) ** 2)
        dark = rgb(L.COL_GLASS_SHADOW)
        below, around = blurred(sil, 1.5, dy=2), blurred(sil, 0.7)
        img = Image(N, N)
        for k in range(N * N):
            if below[k] < 0.004 and around[k] < 0.004:
                continue
            x, y = k % N + 0.5, k // N + 0.5
            a, col = 0.0, (0.0, 0.0, 0.0)

            def over(a2, c2):
                nonlocal a, col
                if a2 <= 0.0:
                    return
                total = a2 + a * (1.0 - a2)
                col = tuple((c2[i] * a2 + col[i] * a * (1.0 - a2)) / total for i in range(3))
                a = total
            out = 1.0 - min(1.0, sil[k])
            over(0.40 * below[k] * out, dark)
            over(0.36 * min(1.0, 2.0 * around[k]) * out, dark)
            if back[k] > 0.0 and c > 0.02:                      # the back, seen through the glass
                u = mid + (x - mid + thick * s) / c
                e = max(0.0, -self.sd(u, y))
                ab, cb = body(u, y)
                over(back[k] * 0.6 * ab, cb)
                over(back[k] * 0.40 * clamp(rim + 0.5 - e), WHITE)
            over(min(1.0, wall_out[k]) * 0.92, edge_out)
            over(min(1.0, wall_in[k]) * 0.92, edge_in)
            if face[k] > 0.0 and c > 0.02:
                u = mid + (x - mid - thick * s) / c
                d = self.sd(u, y)
                nx, ny = self.sd(u + 0.3, y) - self.sd(u - 0.3, y), self.sd(u, y + 0.3) - self.sd(u, y - 0.3)
                n = math.hypot(nx, ny) or 1.0
                af, cf = glass_surface(u, y, max(0.0, -d), (nx * TO_LIGHT[0] + ny * TO_LIGHT[1]) / n, 1.0, body,
                                       sweep=L.LOGO_SWEEP * s, veil=glint, **look)
                over(min(1.0, face[k]) * af, cf)
            img.p[4 * k:4 * k + 4] = [col[0] * a, col[1] * a, col[2] * a, a]
        return img

    def sheet(self, glass):
        """All pictures of one glass as the texture the application unpacks: LOGO_SHEET px square, picture i in
        column i % (LOGO_SHEET / HERO_ICON), row i / that."""
        img, per_row = Image(L.LOGO_SHEET, L.LOGO_SHEET), L.LOGO_SHEET // TURN_N
        for i in range(L.LOGO_FRAMES):
            img.blit(self.picture(glass, i), (i % per_row) * TURN_N, (i // per_row) * TURN_N)
        return img


def logo_sheet():
    """The turning house's pictures for a look without its own (its logo hook): 0.9.3's in Shard, the glass the
    settings keep, the same in every scheme; LOGO_SHEET px square."""
    return Turner().sheet(GLASS_STYLES.index("shard"))


# the application's module of the look drawn (its redraws of these images, the hooks house and pane_hole), None
# without a look; imported last, as it reaches this module as A
LOOK = importlib.import_module("app_look_" + art.LOOK.NAME) if art.LOOK else None
