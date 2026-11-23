"""Stock's drawing of 3DStockTheme's images (the application's half of art.py --look stock; app_art.py is the hook):
the picture's badges and the step marks flat, the installing screen's rod, the line icons on the Designs and Badges
tiles, per scheme the start screen's word raised and the carousel's panes in a white card, the installing screen's in
Stock's tones; with Stock's own helpers (look_stock.py) and the generator's (A: app_images.py)."""
import math

import app_images as A
from look_stock import BLACK, empty_like, engrave, flat_fill, groove, house_shape, icon, raised, stroke_shape

# the application's sprites of 0.9.3 that only the other glass styles draw (look_stock.py, UNUSED): kept at their size,
# empty
UNUSED = {"STEP_TUBE", "STEP_TUBE_FILL", "STEP_CHAMBER_LATER", "STEP_CHAMBER_NOW", "STEP_CHAMBER_DONE",
          "STEP_CHAMBER_CORE", "STEP_SETTING_LATER", "STEP_SETTING_NOW", "STEP_SETTING_DONE", "STEP_SETTING_CORE"}


# the word's strokes as thick as art.py's letters times WORD_STROKE, on the same lines (R3 decided: "ein bisschen
# dünner, man kann das e kaum lesen"): the letters keep their places. 0.70 is the thickest at which both "e"s of
# "Theme" read open in every scheme (host/test_word.py: at least 4 px of ground in the counter and 2 on the way out
# of the opening; 0.80 left the opening 1 px, 0.71 too on Weiss)
WORD_STROKE = 0.70


# the frame round a design's picture (pane_rim, C23 "Abzug"): a white card as a photo print, CARD_W px wide (the small
# class's three quarters of it, as its rim) from CARD_IN px inside the picture's edge, its face graded from its top to
# its foot; on the light schemes a hairline CARD_HAIR px wide along its outer edge (never under 1 px in the small
# class, which is drawn reduced); a soft shadow under it CARD_SHADOW_DOWN px down, blurred by CARD_SHADOW_SIGMA px (a
# Gaussian's sigma). The same card in every scheme: one set of tones for the seven light schemes, one for Nacht
# (face top, face foot; hairline and its alpha; shadow and its alpha). CARD_PAD: what the sprite holds round the
# class's box for the shadow, CARD_PAD_LOW below.
CARD_W, CARD_IN, CARD_HAIR = 4.0, 1.0, 0.7


CARD_SHADOW_DOWN, CARD_SHADOW_SIGMA = 2.5, 2.6


CARD_PAD, CARD_PAD_LOW = 10, 13


CARD_SAMPLES = 6        # each pixel the mean of CARD_SAMPLES x CARD_SAMPLES points: the hairline sits on the edge


CARD_LIGHT = dict(face=(0xFFFFFF, 0xF0F3F7), hair=(0xAAB5C1, 0.90), shadow=(0x263244, 0.52))


CARD_DARK = dict(face=(0xF6F8FB, 0xDEE4EC), hair=None, shadow=(0x000000, 0.70))


def house(size, color):
    """art.py house() in Stock: the house engraved."""
    img = A.Image(size, size + 1)
    engrave(img, house_shape(size), color)
    return img


def line_icon(name, svg, n, color, width, slash=False, light=None):
    return icon(name, n, stroke_shape(svg, n, width, slash), color, light)


def disc_badge(name, n, top, low, svg, size, width, down=0.0, pad=3, shadow=0.30):
    """0.9.1's badge: a flat disc lighter at the top with the symbol in white, a soft shadow under it."""
    L, cover = A.L, A.cover
    img = A.Image(n + 2 * pad, n + 2 * pad)
    c = pad + n / 2.0
    disc = A.sd_circle(c, c, n / 2.0)
    img.paint(lambda x, y: (shadow * A.soft(disc(x, y - 1.5), 2.0) * (1.0 - cover(disc(x, y))), BLACK))
    img.paint(lambda x, y: (cover(disc(x, y)), A.lerp(top, low, (y - pad) / n)))
    mark = A.sd_strokes(A.lines_of(A.tabler(svg)), size / 24.0, width, c - size / 2.0, c - size / 2.0 + down)
    img.paint(lambda x, y: (0.25 * cover(mark(x, y - 1.0)), BLACK))
    img.paint(lambda x, y: (cover(mark(x, y)), A.rgb(L.COL_WHITE)))
    return A.Sprite(name, img, (pad, pad, pad, pad))


def music_badge(name):
    L = A.L
    return disc_badge(name, L.GAL_BADGE, A.rgb(L.COL_BADGE_TOP), A.rgb(L.COL_BADGE_LOW), "music",
                      L.GAL_BADGE_NOTE, 2.4, 0.5)


def done_badge(name, n=None):
    L = A.L
    n = n or L.DONE_BADGE
    green = A.rgb(L.COL_GREEN)
    return disc_badge(name, n, A.lerp(green, A.WHITE, 0.12), green, "check", n * L.DONE_CHECK / L.DONE_BADGE, 3.0)


def step_now(name):
    """The step under way: a flat disc in the dotted lines' tone, a little deeper, its light under it."""
    L = A.L
    n, pad = L.STEP_DROP_NOW, L.BADGE_PAD
    c = pad + n / 2.0
    img = A.Image(n + 2 * pad, n + 2 * pad)
    disc = A.sd_circle(c, c, n / 2.0)
    img.paint(lambda x, y: (0.20 * A.soft(disc(x, y - 1.5), 2.0) * (1.0 - A.cover(disc(x, y))), BLACK))
    img.paint(lambda x, y: (A.cover(disc(x, y)), A.lerp(A.rgb(L.COL_DOTS), A.rgb(L.COL_INK), 0.12)))
    return A.Sprite(name, img, (pad, pad, pad, pad))


def rod():
    """The installing screen's line (the size of art.py liq_rod): a groove in the card, the teal fill in it."""
    L = A.L
    top, low, n = 2, 4, L.STEP_TUBE_H
    h = top + n + low
    card, teal = A.rgb(L.COL_CARD), A.rgb(L.COL_TEAL)
    img = A.Image(14, h)
    groove(img, A.Box(0.0, top, 60.0, top + n, n / 2.0).sd, card)
    track = A.Sprite("STEP_ROD", img, (0, top, 0, low), (6, 0, 2, 0))
    img = A.Image(20, h)
    flat_fill(img, A.Box(-40.0, top + 1, 20.0 - 1, top + n - 1, (n - 2) / 2.0).sd, A.lerp(teal, A.WHITE, 0.15), teal,
              top + 1, top + n - 1)
    return [track, A.Sprite("STEP_ROD_FILL", img, (0, top, 0, low), (6, 0, 12, 0))]


def tile_symbols():
    """The main menu's tiles (0.9.1): Designs a brush, Badges an award."""
    L = A.L
    return {"DESIGNS": lambda name: line_icon(name, "brush", L.MAIN_ICON, L.COL_TEAL, 1.8),
            "BADGES": lambda name: line_icon(name, "award", L.MAIN_ICON, L.COL_ORANGE, 1.8)}


def word_places():
    """The word's two lines as art.py word_lines places them in the WORD_W x WORD_H image: (sprite, text, px to a
    unit, the capitals' top, the image's rows the sprite keeps, its ink as the layout names it)."""
    L = A.L
    wide = A.text_width("3DStock", A.WORD_UNIT)
    low = wide / A.text_width("Theme", 1.0)
    pad, half = A.WORD_PAD, A.WORD_HALF
    y1 = pad + half * A.WORD_UNIT + 1
    y2 = y1 + (10 + half) * A.WORD_UNIT + 3 + half * low
    return (("WORD_TOP", "3DStock", A.WORD_UNIT, y1, (L.WORD_TOP_ROW, L.WORD_TOP_H), "COL_INK"),
            ("WORD_LOW", "Theme", low, y2, (L.WORD_LOW_ROW, L.WORD_LOW_H), "COL_TEAL"))


def word_letters(text, unit, y):
    """A line's letters as art.py places them (letters, with its bounce)."""
    return A.letters(text, unit, A.WORD_PAD, y, 0.35 * unit)


def word(style):
    """"3DStock" over "Theme" as art.py word_lines draws them (same letters, places and image), raised, the strokes
    WORD_STROKE as thick: "3DStock" in the scheme's ink, "Theme" in its teal, so the accent stays on one line as in
    System Settings."""
    L = A.L
    w, h = L.WORD_W, L.WORD_H
    out = []
    for name, text, unit, y, (row, rows), ink in word_places():
        img = A.Image(w, h)
        sd = A.letter_strokes(word_letters(text, unit, y), WORD_STROKE * A.WORD_HALF * unit)
        raised(img, [A.cover(sd(k % w + 0.5, k // w + 0.5)) for k in range(w * h)], A.rgb(getattr(L, ink)))
        cut = A.Image(w, rows)
        cut.p = img.p[4 * w * row:4 * w * (row + rows)]
        out.append(A.Sprite(name, cut))
    return out


def pane_rim(name, n, rim):
    """A pane of the list's swarm in a white card (C23, "Abzug"; the canvas's variant c): the card lies CARD_IN px
    over the picture's edge and follows its corners, so nothing shows between them where the picture's rounded corners
    fade out (its mask's edge, scaled and filtered; R19), and reaches CARD_W px beyond it; its soft shadow below. The
    lengths scale with the class (its rim per PANE_BIG_RIM)."""
    L = A.L
    import schemes
    tones = CARD_DARK if schemes.luminance(A.rgb(L.COL_CARD)) < 0.2 else CARD_LIGHT
    u = rim / float(L.PANE_BIG_RIM)
    _, hole = A.pane_hole(n, rim, CARD_PAD)
    outer, inner = hole.grown(CARD_W * u), hole.grown(-CARD_IN)
    img = A.Image(n + 2 * CARD_PAD, n + CARD_PAD + CARD_PAD_LOW)
    shade, alpha = A.rgb(tones["shadow"][0]), tones["shadow"][1]
    down, blur = CARD_SHADOW_DOWN * u, 2.0 * CARD_SHADOW_SIGMA * u          # A.soft's blur: twice the sigma
    top, foot = (A.rgb(c) for c in tones["face"])
    line, hair = (A.rgb(tones["hair"][0]), tones["hair"][1]) if tones["hair"] else (None, 0.0)
    width = (max(1.0, CARD_HAIR * u) if u < 1 else CARD_HAIR) if line else 0.0

    def at(x, y):
        d = outer.sd(x, y)
        if d >= 0.0:
            return (alpha * A.soft(outer.sd(x, y - down), blur), shade)
        if inner.sd(x, y) < 0.0:
            return None
        face = A.lerp(top, foot, min(1.0, max(0.0, (y - CARD_PAD) / n)))
        return (1.0, A.lerp(face, line, hair) if d > -width else face)
    supersampled(img, at, CARD_SAMPLES)
    return A.Sprite(name + "_RIM", img, (CARD_PAD, CARD_PAD, CARD_PAD, CARD_PAD_LOW))


def pane_fill(name, n, rim):
    p = n - 2 * rim
    _, hole = A.pane_hole(n, rim, 0)
    img = A.Image(p, p)
    img.paint(lambda x, y: (A.cover(hole.sd(x + rim, y + rim)), A.rgb(A.L.COL_CARD)))
    return A.Sprite(name + "_FILL", img)


def supersampled(img, at, n=4):
    """img.paint with at(x, y) (alpha, colour) averaged over n x n points in each pixel: graded forms a few px wide."""
    steps = [-0.5 + (i + 0.5) / n for i in range(n)]

    def pixel(u, v):
        a = r = g = b = 0.0
        for dy in steps:
            for dx in steps:
                got = at(u + dx, v + dy)
                if got and got[0] > 0.0:
                    w, c = min(got[0], 1.0), got[1]
                    a, r, g, b = a + w, r + w * c[0], g + w * c[1], b + w * c[2]
        return (a / (n * n), (r / a, g / a, b / a)) if a > 0.0 else None
    img.paint(pixel)


# ---------------------------------------------------------------- the installing screen (R9, the tile d)
def pour_sprites():
    """The installing screen's images (pour_art.py: the generator every look draws its tile with) in Stock's tones:
    the liquid and the pieces in the scheme's pour tone (schemes.py, COL_POUR) at the liquid's strength
    (pour_art.liquid_tone), the marks and the clamp in the teal, the line, its knot and the rings in the dots' tone a
    little towards the ink."""
    import pour_art
    import schemes
    L = A.L
    pour_art.A = A
    return pour_art.pour_sprites(tone=pour_art.liquid_tone(A.rgb(L.COL_POUR)),
                                 dark=schemes.luminance(A.rgb(L.COL_CARD)) < 0.2,
                                 teal=A.rgb(L.COL_TEAL), line=A.lerp(A.rgb(L.COL_DOTS), A.rgb(L.COL_INK), 0.12),
                                 ink=A.rgb(L.COL_INK))


def sprites(items, index):
    """The scheme's atlas for Stock (index: the scheme, which A.L is drawn in): the application's sprites with every
    glass one redrawn, the installing screen's after them."""
    L = A.L
    styled = {"MUSIC_BADGE": music_badge, "DONE_BADGE": done_badge, **tile_symbols()}
    by_style = {A.style_name(base, style): (base, style) for base in styled for style in A.GLASS_STYLES}
    own = {"STEP_NOW_GLASS_SHARD": lambda name: step_now(name),
           "STEP_DONE_GLASS_SHARD": lambda name: done_badge(name, L.STEP_DROP_DONE),
           "STEP_ROD": None, "STEP_ROD_FILL": None}
    rods = {s.name: s for s in rod()}
    out = []
    for s in items:
        if s.name in rods:
            new = rods[s.name]
        elif s.name in own:
            new = own[s.name](s.name)
        elif s.name in by_style:
            new = styled[by_style[s.name][0]](s.name)
        elif s.name in UNUSED:
            new = empty_like(s)
        else:
            new = s
        out.append(new)
    return out + pour_sprites()


def sheet(names):
    """The scheme's sheet: the application's sprites named as on its glass sheet, in that order."""
    made = {s.name: s for s in word("stock")}
    for name, n, rim in A.PANE_CLASSES:
        made[name + "_RIM"] = pane_rim(name, n, rim)
        made[name + "_FILL"] = pane_fill(name, n, rim)
    light = A.pane_light()
    made["PANE_LIGHT"] = empty_like(light)
    return [made[name] for name in names]


# ---------------------------------------------------------------- the turning house, flat (per scheme in Stock)

def logo(turner):
    """The start screen's house as app_images.py's Turner turns it, in the scheme's teal: its face raised as the word,
    its cut edge a deeper teal; LOGO_SHEET px square."""
    L = A.L
    img, per_row = A.Image(L.LOGO_SHEET, L.LOGO_SHEET), L.LOGO_SHEET // A.TURN_N
    for i in range(L.LOGO_FRAMES):
        img.blit(house_picture(turner, i), (i % per_row) * A.TURN_N, (i // per_row) * A.TURN_N)
    return img


def house_picture(turner, index):
    L, N, thick = A.L, A.TURN_N, A.TURN_THICK
    turn = math.radians(-90 + index * L.LOGO_STEP)
    c, s = max(0.0, math.cos(turn)), math.sin(turn)
    mid, hs, w = N / 2.0, thick * abs(s), 1.0 / A.TURN_FINE
    face, wall = [0.0] * (N * N), [0.0] * (N * N)
    for j, spans in enumerate(turner.rows):
        if not spans:
            continue
        base = (j // A.TURN_FINE) * N
        flat = [(mid + (a - mid) * c, mid + (b - mid) * c) for a, b in spans]
        A.add_spans(face, base, [(a + thick * s, b + thick * s) for a, b in flat], w)
        if s > 0:
            A.add_spans(wall, base, A.merged([(a - hs, a + thick * s) for a, _ in flat]), w)
        else:
            A.add_spans(wall, base, A.merged([(b + thick * s, b + hs) for _, b in flat]), w)
    teal = A.rgb(L.COL_TEAL)
    img = A.Image(N, N)
    raised(img, [min(1.0, v) for v in face], teal, ([0.95 * min(1.0, v) for v in wall], A.lerp(teal, BLACK, 0.30)))
    return img
