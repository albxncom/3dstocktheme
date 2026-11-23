"""The installing screen's images (R9, the tile d; C44: the tile is every look's): one generator for all looks. A
look's module hands pour_sprites its tones (the liquid's tone, whether the scheme is a dark one, the teal of the
marks, the line's tone and the ink) and gets the images under the names and in the sizes pour_ui.h draws; the forms,
the sizes and the frames are the same in every look (C16), the tones the look's. Stock's tones: look_stock.py."""
import colorsys
import math
import types

from liquid import LIQUID_FULL, liquid_tone, most_chroma, own_take   # the liquid's strength, the kit's rule

A = None            # art.py's module (set by the look's module before anything here is drawn)
BLACK = (0.0, 0.0, 0.0)

# ---------------------------------------------------------------- the installing screen (R9, the tile d)
# A glass tile seen straight from the front with the picture set into its face, balls on a line that turn into drops
# and a liquid in the scheme's pour tone (schemes.py, COL_POUR), drawn anew (the application's own shape, nothing taken
# from a console's menu, a game card or a cartridge). Its form is d "Nachbau" of the canvas (the owner's pick,
# 2026-11-15, "ok d gefällt mir am besten"): a rounded block of clear glass, thin walls, the liquid inside it around
# and behind the picture, graded from a deep tone under its surface to a light foot, one calm curved surface, soft
# light along the top edge, the liquid's colour glowing on the card under the tile. The liquid and the glass share
# the inner shape (pour_layout.h, POUR_INNER_*): the liquid's images are that shape, cut at the level by the code row
# by row.
#
# Every layer is drawn as the canvas's generator drew it: on the tile's canvas (the tile and POUR_AROUND px round it),
# its lights and shades as profiles across the edge (band) times ramps down or across the tile, laid over each other
# by plain alpha, at TILE_K x TILE_K points per pixel near an edge (the canvas's drawing scale) and 2 x 2 elsewhere;
# its soft parts (the halo, the foot's light, the shadow) are Gaussian blurs of the tile's coverage. The layers' order
# is the code's (pour_ui.h).

TILE_K = 8                      # points per pixel and axis near an edge
TILE_NEAR = 4.0                 # px from an edge within which a pixel is drawn with TILE_K x TILE_K points
SURFACE_PHASES = 16             # the surface strip in this many places within its first row
SURFACE_LIGHT = 0.35            # the soft light line just under the surface


def band(d, core, out, inner, tail=0.0, reach=1.0):
    """A form's profile across the edge: 1 at depth core, falling outwards over about out px and inwards over about
    inner px, a share tail of it as a long fall over reach px."""
    x = d - core
    if x < 0.0:
        return math.exp(-(x / out) ** 2)
    return (1.0 - tail) * math.exp(-(x / inner) ** 2) + tail * math.exp(-x / reach)


def hls(c, dh=0.0, kl=1.0, ks=1.0, light=None):
    """c with its hue turned by dh (0 to 1), its lightness scaled by kl (or set to light), its saturation by ks."""
    h, li, s = colorsys.rgb_to_hls(c[0] / 255.0, c[1] / 255.0, c[2] / 255.0)
    r, g, b = colorsys.hls_to_rgb((h + dh) % 1.0, A.clamp(li * kl if light is None else light), A.clamp(s * ks))
    return (round(r * 255.0), round(g * 255.0), round(b * 255.0))


def mixed(a, b, t):
    """a towards b by t, in whole steps as the canvas's tones."""
    return tuple(float(round(x + (y - x) * t)) for x, y in zip(a, b))


LIQUID_STEPS = dict(light=dict(deep=-0.06, mid=0.0, light=0.06, foot=0.12),
                    dark=dict(deep=-0.12, mid=-0.04, light=0.03, foot=0.07))
LIQUID_TOWARDS = dict(mid=0.3, light=0.5, foot=0.8)

def vivid_at(hue):
    """The lightness l (0.30 to 0.95, in steps of 0.01) at which sRGB shows hue h with the most chroma."""
    return max((most_chroma(hue, k / 100.0), k / 100.0) for k in range(30, 96))[1]


def liquid_step(tone, dl):
    """tone (liquid_tone's) with its lightness l moved by dl, its hue kept, its chroma LIQUID_FULL of the most there
    or its own if that is more (as far as sRGB reaches); in a scheme with a take of its own, at the take's hue (turned
    towards its warm hue when darker) and chroma."""
    S = A.schemes
    _, chroma, hue = S.oklch(tone)
    lit = A.clamp(S.luminance(tone) ** (1.0 / 3.0) + dl)
    take = own_take()
    if take:
        dark = S.luminance(A.rgb(A.L.COL_CARD)) < 0.2
        hue = take["hue"][dark]
        if dl < 0.0 and "warm" in take:
            hue += (take["warm"] - hue) * min(1.0, -dl / take["reach"])
        if "chroma" in take:
            return tuple(float(v) for v in S.colour_at(hue, take["chroma"][dark], lit ** 3))
    return tuple(float(v) for v in S.colour_at(hue, max(chroma, LIQUID_FULL * most_chroma(hue, lit)), lit ** 3))


def liquid_grade(tone, dark):
    """The liquid's deep, mid, light and foot tones from its tone (liquid_tone's) by the rule above."""
    steps = LIQUID_STEPS["dark" if dark else "light"]
    take = own_take()
    if dark:
        return {k: liquid_step(tone, d) for k, d in steps.items()}
    if take:
        return {k: liquid_step(tone, d) for k, d in take.get("steps", steps).items()}
    up = vivid_at(A.schemes.oklch(tone)[2]) - A.schemes.luminance(tone) ** (1.0 / 3.0)
    return {k: liquid_step(tone, max(d, LIQUID_TOWARDS.get(k, 0.0) * up) if d >= 0 else d) for k, d in steps.items()}


def tile_tones(tone, dark):
    """The tile's tones from the look's liquid tone (liquid_tone), as the canvas derived them for Weiss (light
    schemes) and Nacht (dark ones); the liquid's grade by the rule above."""
    white = A.WHITE
    grade = liquid_grade(tone, dark)
    if dark:
        return dict(grade, shade=BLACK, edge=BLACK, shadow=BLACK, lit=mixed(tone, white, 0.6),
                    halo=mixed(tone, white, 0.6), body=mixed(tone, white, 0.35), rim=mixed(tone, white, 0.7))
    return dict(grade, shade=mixed(tone, BLACK, 0.55), edge=hls(tone, kl=0.62, ks=1.3), shadow=(70.0, 80.0, 95.0),
                lit=mixed(tone, white, 0.3), halo=tone, body=None, rim=white)


def sd_rounded(cx, cy, w, h, r):
    """The signed distance to a rounded box w x h centred on cx, cy (negative inside)."""
    ax, ay = w / 2.0 - r, h / 2.0 - r

    def sd(x, y):
        px, py = abs(x - cx) - ax, abs(y - cy) - ay
        if px > 0.0 and py > 0.0:
            return math.hypot(px, py) - r
        return (px if px > py else py) - r
    return sd


def gauss(grid, w, h, sigma):
    """grid (w * h values) blurred by a Gaussian of sigma px, 0 outside."""
    r = int(math.ceil(3.5 * sigma))
    k = [math.exp(-0.5 * (i / sigma) ** 2) for i in range(-r, r + 1)]
    s = sum(k)
    k = [v / s for v in k]
    for across in (True, False):
        out = [0.0] * (w * h)
        n, lines, step, gap = (w, h, 1, w) if across else (h, w, w, 1)
        for j in range(lines):
            base = j * gap
            line = [grid[base + i * step] for i in range(n)]
            if not any(line):
                continue
            for i in range(n):
                lo, hi = max(0, i - r), min(n, i + r + 1)
                out[base + i * step] = sum(line[q] * k[q - i + r] for q in range(lo, hi))
        grid = out
    return grid


def sampler(grid, w, h):
    """grid's values (at the pixels' middles) between them, by straight lines on both axes."""
    def at(x, y):
        gx, gy = min(max(x - 0.5, 0.0), w - 1.0), min(max(y - 0.5, 0.0), h - 1.0)
        x0, y0 = min(int(gx), w - 2), min(int(gy), h - 2)
        fx, fy = gx - x0, gy - y0
        i = y0 * w + x0
        top = grid[i] + (grid[i + 1] - grid[i]) * fx
        low = grid[i + w] + (grid[i + w + 1] - grid[i + w]) * fx
        return top + (low - top) * fy
    return at


def over(paints):
    """paints ((alpha, colour), ...) laid over each other from the first, on nothing: (alpha, r, g, b) premultiplied,
    or None."""
    a = r = g = b = 0.0
    for al, c in paints:
        if al > 0.0:
            al = min(al, 1.0)
            k = 1.0 - al
            a, r, g, b = al + a * k, c[0] * al + r * k, c[1] * al + g * k, c[2] * al + b * k
    return (a, r, g, b) if a > 0.0 else None


def cov(sd, n):
    """The share inside a shape of a point d px from its edge, drawn at n points per pixel."""
    v = 0.5 - sd * n
    return 0.0 if v <= 0.0 else 1.0 if v >= 1.0 else v


def inward(sd):
    """How far inside a shape (px) a point sd px from its edge lies, in steps of 1 / TILE_K px as on the canvas."""
    return min(int(-sd * TILE_K + 0.5), 255) / TILE_K if sd < 0.0 else 0.0


def outward(sd):
    return min(int(sd * TILE_K + 0.5), 255) / TILE_K if sd > 0.0 else 0.0


def tile_sprites(T, dark, material):
    """The tile's layers in the scheme (the tile's tones T, light or dark), each as a sprite whose element is the tile
    (POUR_HALO, POUR_FOOT, POUR_BACK, POUR_FRONT, POUR_LIT_RIM) or the inner shape (POUR_LIQUID, POUR_LIT), and the
    surface's strips POUR_SURFACE. Their places do not depend on the scheme. The recess round the picture follows the
    picture's own corners (the icons' mask: DETAIL_ICON_R per 48 px). A look's material may draw any layer itself
    (pour_sprites, material["layers"]): it gets the shapes and the helpers below and gives a paint function per layer
    by the layer's name; the layers it leaves are Stock's."""
    L, white = A.L, A.WHITE
    tw, th, rim, m = L.POUR_TILE_W, L.POUR_TILE_H, L.POUR_RIM, L.POUR_AROUND
    cw, ch = tw + 2 * m, th + 2 * m                         # the canvas
    tile = sd_rounded(cw / 2.0, ch / 2.0, tw, th, L.POUR_TILE_R)
    cav = sd_rounded(cw / 2.0, ch / 2.0, tw - 2 * rim, th - 2 * rim, L.POUR_TILE_R - rim)
    pic = sd_rounded(cw / 2.0, ch / 2.0, L.POUR_PICTURE, L.POUR_PICTURE, L.DETAIL_ICON_R * L.POUR_PICTURE / 48.0)
    clamp, smooth = A.clamp, A.smooth

    def down(y):                # 0 at the tile's top, 1 at its foot
        return (y - m) / th

    def top_w(t):               # 1 along the top, gone by the middle
        return 1.0 - smooth(0.07, 0.43, t)

    def low_w(t):               # 1 along the foot
        return smooth(0.52, 0.92, t)

    def side_w(t):              # the edge's shade: little at the top, most low
        return 0.35 + 0.65 * smooth(0.15, 0.75, t)

    def near(shape, reach=TILE_NEAR):
        return lambda x, y: abs(shape(x, y)) < reach

    def draw(at, fine):
        """A layer of the whole canvas: at(x, y, n) at n x n points per pixel (TILE_K where fine(x, y) at the pixel's
        middle, else 2), averaged; the left half drawn, the right half its mirror image (every layer of Stock's is;
        a look's paint function that is not says so by its attribute whole, and is drawn over both halves)."""
        img = A.Image(cw, ch)
        p = img.p
        steps = {TILE_K: [(i + 0.5) / TILE_K for i in range(TILE_K)], 2: [0.25, 0.75]}
        mirror = not getattr(at, "whole", False)
        for y in range(ch):
            for x in range(cw // 2 if mirror else cw):
                n = TILE_K if fine(x + 0.5, y + 0.5) else 2
                a = r = g = b = 0.0
                for sy in steps[n]:
                    for sx in steps[n]:
                        got = at(x + sx, y + sy, n)
                        if got:
                            a, r, g, b = a + got[0], r + got[1], g + got[2], b + got[3]
                if a > 0.0:
                    k = 1.0 / (n * n)
                    for q in (4 * (y * cw + x), 4 * (y * cw + cw - 1 - x)) if mirror else (4 * (y * cw + x),):
                        p[q], p[q + 1], p[q + 2], p[q + 3] = r * k, g * k, b * k, a * k
        return img

    def coverage(shape, dy):
        """The shape moved dy px down, its share of each pixel."""
        g = [0.0] * (cw * ch)
        for y in range(ch):
            for x in range(cw):
                d = shape(x + 0.5, y + 0.5 - dy)
                if d <= -1.0:
                    g[y * cw + x] = 1.0
                elif d < 1.0:
                    g[y * cw + x] = sum(cov(shape(x + (i + 0.5) / TILE_K, y + (j + 0.5) / TILE_K - dy), TILE_K)
                                        for i in range(TILE_K) for j in range(TILE_K)) / TILE_K ** 2
        return g

    def blurred(dy, sigma):
        return sampler(gauss(coverage(tile, dy), cw, ch, sigma), cw, ch)

    def sprite(name, img, box, elem):
        """img's part box (x, y, w, h of the canvas) as a sprite for the element elem (the same)."""
        x0, y0, w, h = box
        out = A.Image(w, h)
        for y in range(h):
            out.p[4 * y * w:4 * (y + 1) * w] = img.p[4 * ((y0 + y) * cw + x0):4 * ((y0 + y) * cw + x0 + w)]
        pad = (elem[0] - x0, elem[1] - y0, x0 + w - elem[0] - elem[2], y0 + h - elem[1] - elem[3])
        return A.Sprite(name, out, pad)

    # HALO: the end's glow behind the tile
    def halo_layer():
        halo_blur = blurred(0, 7.0)

        def halo(x, y, n):
            d = tile(x, y)
            return over((((0.75 if dark else 0.8) * min(halo_blur(x, y), 1.0), T["halo"]),
                         (0.5 * math.exp(-outward(d) / 2.0) * (1.0 - cov(d, n)), T["halo"])))
        return halo

    # FOOT: the liquid's light on the card under the tile
    def foot_layer():
        foot_blur = blurred(5, 4.0)

        def foot(x, y, n):
            return over((((0.40 if dark else 0.45) * foot_blur(x, y) * (1.0 - cov(tile(x, y), n)) *
                          smooth(0.45, 0.95, down(y)), T["light"] if dark else T["mid"]),))
        return foot

    # BACK: the shadow under the tile, the glass body (the card seen through it with a faint tint of the liquid, a
    # little lighter at the top), the glass gathering the liquid's tint towards its edge
    def back_layer():
        shadow_blur = blurred(3, 3.0)

        def back(x, y, n):
            d = tile(x, y)
            c, t = cov(d, n), down(y)
            paints = [((0.5 if dark else 0.32) * shadow_blur(x, y) * (1.0 - c), T["shadow"])]
            if dark:
                paints.append((0.10 * c * clamp(1.0 - 0.35 * t), T["body"]))
            else:
                paints += [(0.07 * c * clamp(1.0 - 0.35 * t), white), (0.14 * c * clamp(0.4 + 0.6 * t), T["mid"])]
            paints.append(((0.20 if dark else 0.22) * band(inward(d), 0.6, 0.6, 3.2, 0.3, 7.0) * c, T["mid"]))
            return over(paints)
        return back

    # LIQUID: the inner shape filled, graded from the deep tone under the top to the light foot, deeper along the
    # wall (wall: the deep tone's profile across the wall and its strength), the light gathered at the foot inside it
    # (foot: its strength); shape: a look's own inner shape in place of the rounded one
    def liquid_layer(wall=(0.5, 2.4, 0.25, 6.0, 0.55), foot=0.8, shape=cav):
        stops = ((0.0, mixed(T["deep"], T["mid"], 0.35)), (0.42, T["mid"]), (0.80, T["light"]), (1.0, T["foot"]))
        gathered = mixed(T["foot"], white, 0.45)

        def graded(t):
            t = clamp(t)
            for (t0, c0), (t1, c1) in zip(stops, stops[1:]):
                if t <= t1:
                    return mixed(c0, c1, (t - t0) / (t1 - t0))
            return stops[-1][1]

        def liquid(x, y, n):
            d = shape(x, y)
            c = cov(d, n)
            if c <= 0.0:
                return None
            dd = inward(d)
            a, r, g, b = over(((0.97 * c, graded((y - m - rim) / (th - 2 * rim))),
                               (wall[4] * band(dd, 0.0, wall[0], wall[1], wall[2], wall[3]) * c, T["deep"]),
                               (foot * band(dd, 3.6, 1.2, 2.6, 0.3, 6.0) * c * low_w(down(y)), gathered)))
            k = 0.97 * c / a        # its colour as laid, its alpha the shape's alone
            return 0.97 * c, r * k, g * k, b * k
        return liquid

    # LIT: the liquid lighter at the end
    def lit_layer():
        def lit(x, y, n):
            return over(((cov(cav(x, y), n) * clamp(0.45 - 0.25 * down(y)), T["lit"]),))
        return lit

    # FRONT: the glass in front of everything: the face's sheen from the top, the shade inside the edge low and at the
    # sides, the edge darker where it turns away, the top light (a broad band following the corners, a brighter core
    # in it), the rim's own light, the light leaving at the foot; the recess round the picture (shaded above it, lit
    # under it) and the picture's own edge in shade at the top
    p_y, p_h = m + (th - L.POUR_PICTURE) / 2.0 - 3.0, L.POUR_PICTURE + 6.0

    def front(x, y, n):
        d, q = tile(x, y), pic(x, y)
        c, cp = cov(d, n), cov(q, n)
        if c <= 0.0 and q > 16.0:
            return None
        t, u, tp = down(y), (x - m) / tw, (y - p_y) / p_h
        dd, dq = inward(d), outward(q)
        p_top, p_low = 1.0 - 0.75 * smooth(0.0, 0.9, tp), smooth(0.55, 0.95, tp)
        hole = smooth(0.2, 1.2, dq) * (1.0 - cp) ** 2
        lights = top_w(t)
        return over((
            ((0.10 if dark else 0.14) * c * hole * (1.0 - smooth(0.0, 0.62, t)), white),
            ((0.30 if dark else 0.24) * band(dd, 0.9, 0.7, 2.6, 0.3, 6.0) * c * side_w(t), T["edge"]),
            ((0.5 if dark else 0.40) * band(dd, 1.3, 0.55, 1.2) * c * side_w(t), T["edge"]),
            ((0.62 if dark else 0.95) * band(dd, 2.5, 1.2, 2.6, 0.2, 6.0) * c * lights, white),
            ((0.55 if dark else 0.9) * band(dd, 1.9, 0.7, 1.1) * c * lights *
             clamp(1.0 - 0.55 * (2.0 * u - 1.0) ** 2), white),
            ((0.6 if dark else 0.95) * band(dd, 0.4, 0.35, 0.5) * c * (1.0 - 0.6 * smooth(0.0, 0.8, t)), T["rim"]),
            ((0.42 if dark else 0.7) * band(dd, 2.3, 0.8, 1.5) * c * low_w(t) *
             clamp(1.0 - 0.35 * abs(2.0 * u - 1.0) ** 3), white),
            ((0.32 if dark else 0.36) * band(dq, 0.0, 0.5, 1.9, 0.15, 4.0) * (1.0 - cp) * p_top, T["shade"]),
            ((0.3 if dark else 0.6) * band(dq, 1.3, 0.7, 1.3) * (1.0 - cp) * p_low, white),
            (0.22 * band(inward(q), 0.0, 0.5, 1.6) * cp * p_top, BLACK)))

    # LIT_RIM: the end's light along and round the edge
    rim_c = mixed(T["lit"], white, 0.5)

    def lit_rim(x, y, n):
        d = tile(x, y)
        c = cov(d, n)
        return over(((0.7 * math.exp(-outward(d) / 2.2) * (1.0 - c), T["lit"]),
                     (0.85 * band(inward(d), 0.6, 0.6, 1.2) * c, rim_c)))

    own = {}
    if "layers" in material:
        own = material["layers"](types.SimpleNamespace(
            T=T, dark=dark, m=m, tw=tw, th=th, tile=tile, cav=cav, pic=pic, p_y=p_y, p_h=p_h, down=down, top_w=top_w,
            low_w=low_w, side_w=side_w, blurred=blurred, liquid=liquid_layer, band=band, cov=cov, inward=inward,
            outward=outward, over=over, mixed=mixed))

    def layer(name, stock):
        return own[name] if name in own else stock()

    canvas, elem, inner = (0, 0, cw, ch), (m, m, tw, th), (m + rim, m + rim, tw - 2 * rim, th - 2 * rim)
    either = lambda x, y: abs(tile(x, y)) < TILE_NEAR or abs(pic(x, y)) < TILE_NEAR
    return [sprite("POUR_HALO", draw(layer("halo", halo_layer), near(tile, 2.0)), canvas, elem),
            sprite("POUR_FOOT", draw(layer("foot", foot_layer), near(tile, 1.5)), (0, m, cw, ch - m), elem),
            sprite("POUR_BACK", draw(layer("back", back_layer), near(tile)), canvas, elem),
            sprite("POUR_LIQUID", draw(layer("liquid", liquid_layer), near(cav)), inner, inner),
            surface_strips(T, material.get("surface", (SURFACE_LIGHT, 0.9, 0.7, 1.1))),
            sprite("POUR_LIT", draw(layer("lit", lit_layer), near(cav, 1.5)), inner, inner),
            sprite("POUR_FRONT", draw(layer("front", lambda: front), either), canvas, elem),
            sprite("POUR_LIT_RIM", draw(layer("lit_rim", lambda: lit_rim), near(tile)), canvas, elem)]


def surface_strips(T, light):
    """POUR_SURFACE, the surface over one column (the canvas's "Antwort"): a strip of POUR_UNDER + 3 rows
    for each of SURFACE_PHASES places of the surface within its first row (column p: the surface (p + 0.5) / phases
    px under the row's top), the first row covered as far as the liquid reaches into it; the deep tone with a soft
    light line just under the surface (light: its strength and its profile's core, out and inner, band), whole for 2
    rows and fading out over POUR_UNDER rows below. Drawn at TILE_K points per pixel down the strip."""
    under, n = A.L.POUR_UNDER, SURFACE_PHASES
    h = under + 3
    img = A.Image(n, h)
    glint = mixed(T["foot"], A.WHITE, 0.35)
    steps = [(i + 0.5) / TILE_K for i in range(TILE_K)]
    for p in range(n):
        f = (p + 0.5) / n
        for y in range(h):
            a = r = g = b = 0.0
            for sy in steps:
                t = y + sy - f
                if t < 0.0:
                    continue
                al = 1.0 - A.smooth(2.0, 2.0 + under, t)
                c = mixed(T.get("under", T["deep"]), glint, light[0] * band(t, light[1], light[2], light[3]))
                a, r, g, b = a + al, r + c[0] * al, g + c[1] * al, b + c[2] * al
            k = 1.0 / TILE_K
            img.p[4 * (y * n + p):4 * (y * n + p) + 4] = [r * k, g * k, b * k, a * k]
    return A.Sprite("POUR_SURFACE", img)


PIECE_K = 8                     # the piece is drawn at this many points per pixel and axis, then averaged
PIECE_TURNS = 12                # its turn frames over a quarter turn (the octahedron looks the same after it)
# the piece b "Gedreht" (the owner's pick, C18): an octahedron, the girdle's half width r, its top and foot point up
# and down from the girdle (px), turned phi degrees about the upright axis and its top leant tilt degrees towards the
# eye; lit from the upper left in front
PIECE = dict(r=6.9, up=8.2, down=8.2, phi=30.0, tilt=16.0)
PIECE_LIGHT = (-0.50, -0.72, 0.48)
# the light along the outline towards the light, the shade along the outline turned away, the soft light along the
# ridges, the glint on the facet facing the light most, the body's alpha (of 255)
PIECE_RIM, PIECE_EDGE, PIECE_RIDGE, PIECE_GLINT, PIECE_ALPHA = 0.9, 0.5, 0.24, 0.75, 235
# the hanging piece's shadow: this far down (px), blurred by this much (px), this alpha of the shadow's tone
PIECE_SHADOW_DOWN, PIECE_SHADOW_BLUR, PIECE_SHADOW = 2.5, 0.8, dict(dark=0.40, light=0.22)
# the piece's material (Stock's; a look's material may change any of it, pour_sprites): key (the facets' tone scaled),
# spread (the contrast from facet to facet), grade (the tone's run inside a facet), rim (the light along the outline
# towards the light), rim_all (and along the rest of it), gather (the tint gathered at the outline), ridge (the light
# along the ridges: strength, width px), glint (strength, size px), milk (a veil of the tones' milk and light: share),
# blur (what is in it goes soft, px; its outline stays), alpha (the body's, of 255)
PIECE_MATERIAL = dict(key=1.0, spread=1.0, grade=1.0, rim=PIECE_RIM, rim_all=0.0, gather=0.0,
                      ridge=(PIECE_RIDGE, 0.42), glint=(PIECE_GLINT, 0.8), milk=0.0, blur=0.0, alpha=PIECE_ALPHA)
PIECE_LIGHT_SCHEMES = dict(key=0.96, rim=0.8, gather=0.36)      # on the light schemes


def piece_faces(turn):
    """The facets of the piece that face the eye, turned by turn degrees more: per facet its three corners on the
    picture (px, the body's middle at 0, 0), its normal, whether its point is the top (apex 0) or the foot, which of
    its sides are the outline."""
    P = PIECE
    phi, th = math.radians(P["phi"] + turn), math.radians(P["tilt"])
    r, up, dn = P["r"], P["up"], P["down"]
    raw = [(0.0, -up, 0.0), (0.0, dn, 0.0)] + [(r * math.cos(phi + k * math.pi / 2.0), 0.0,
                                                 r * math.sin(phi + k * math.pi / 2.0)) for k in range(4)]
    V = [(x, y * math.cos(th) + z * math.sin(th), -y * math.sin(th) + z * math.cos(th)) for x, y, z in raw]
    ys = [v[1] for v in V]
    mid = (min(ys) + max(ys)) / 2.0
    V = [(x, y - mid, z) for x, y, z in V]
    faces = []
    for apex in (0, 1):
        for k in range(4):
            ids = (apex, 2 + k, 2 + (k + 1) % 4)
            a, b, c = (V[i] for i in ids)
            u, w = [b[i] - a[i] for i in range(3)], [c[i] - a[i] for i in range(3)]
            n = (u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0])
            cen = [(a[i] + b[i] + c[i]) / 3.0 - (0.0, -mid, 0.0)[i] for i in range(3)]
            if sum(n[i] * cen[i] for i in range(3)) < 0.0:
                n = tuple(-x for x in n)
            size = math.sqrt(sum(x * x for x in n)) or 1.0
            n = tuple(x / size for x in n)
            if n[2] > 0.02:
                faces.append(dict(ids=ids, n=n, apex=apex, p=[(V[i][0], V[i][1]) for i in ids]))
    seen = {}
    for f in faces:
        for k in range(3):
            e = frozenset((f["ids"][(k + 1) % 3], f["ids"][(k + 2) % 3]))
            seen[e] = seen.get(e, 0) + 1
    for f in faces:
        f["outline"] = [seen[frozenset((f["ids"][(k + 1) % 3], f["ids"][(k + 2) % 3]))] == 1 for k in range(3)]
    return faces


_piece_points = {}


def piece_points(turn, P):
    """The piece's points at PIECE_K per pixel in its cell (POUR_PIECE_W x POUR_PIECE_H, its middle 1 px over the
    cell's) in the material P (PIECE_MATERIAL's keys): {(i, j): (tone, shade, light)}, the tone 0 to 255 along the
    tile's tones, the shade (towards the edge's tone) and the light (towards white) 0 to 1. Stock's on the light
    schemes holds its outline by a tint gathered towards its edge, on the dark ones by the light along it. A look's
    material may add core, a light inside the body: its strength, its width and height (px, about the body's middle)
    and what is left of the facets' tone under it."""
    key = (turn,) + tuple(sorted(P.items()))
    if key in _piece_points:
        return _piece_points[key]
    L, k, clamp = A.L, PIECE_K, A.clamp
    gain, rim, gather, spread, grade = P["key"], P["rim"], P["gather"], P["spread"], P["grade"]
    (ridge, ridge_w), (glint, glint_s) = P["ridge"], P["glint"]
    core = P.get("core")
    W, H = L.POUR_PIECE_W * k, L.POUR_PIECE_H * k
    ox, oy = L.POUR_PIECE_W / 2.0, L.POUR_PIECE_H / 2.0 - 1.0
    faces = piece_faces(turn)
    best = max(faces, key=lambda f: sum(a * b for a, b in zip(f["n"], PIECE_LIGHT)))
    out = {}
    for f in faces:
        (x0, y0), (x1, y1), (x2, y2) = f["p"]
        det = (y1 - y2) * (x0 - x2) + (x2 - x1) * (y0 - y2)
        if abs(det) < 1e-6:
            continue
        lit = sum(a * c for a, c in zip(f["n"], PIECE_LIGHT))
        base = 0.20 + 0.58 * clamp((lit + 0.3) / 1.2)
        if spread != 1.0:
            base = 0.5 + (base - 0.5) * spread
        g0, g1 = (0.20 * grade, 0.10 * grade) if f["apex"] == 0 else (0.40 * grade, 0.18 * grade)
        vals = (base + g0, base - g1, base - g1)
        sides = []                                                  # per side: its height, how it faces the light
        for j in range(3):
            pa, pb, pv = f["p"][(j + 1) % 3], f["p"][(j + 2) % 3], f["p"][j]
            ex, ey = pb[0] - pa[0], pb[1] - pa[1]
            length = math.hypot(ex, ey) or 1.0
            nx, ny = ey / length, -ex / length
            if nx * (pv[0] - pa[0]) + ny * (pv[1] - pa[1]) > 0.0:
                nx, ny = -nx, -ny                                   # pointing out of the facet
            sides.append((abs(det) / length, nx * -0.55 + ny * -0.83))
        gx, gy = (0.5 * x0 + 0.25 * x1 + 0.25 * x2, 0.5 * y0 + 0.25 * y1 + 0.25 * y2) if f is best else (None, None)
        for j in range(max(0, int((min(y0, y1, y2) + oy) * k)), min(H, int((max(y0, y1, y2) + oy) * k) + 1)):
            y = (j + 0.5) / k - oy
            for i in range(max(0, int((min(x0, x1, x2) + ox) * k)), min(W, int((max(x0, x1, x2) + ox) * k) + 1)):
                x = (i + 0.5) / k - ox
                w0 = ((y1 - y2) * (x - x2) + (x2 - x1) * (y - y2)) / det
                w1 = ((y2 - y0) * (x - x2) + (x0 - x2) * (y - y2)) / det
                w2 = 1.0 - w0 - w1
                if w0 < 0.0 or w1 < 0.0 or w2 < 0.0:
                    continue
                ws = (w0, w1, w2)
                light = shade = 0.0
                for m in range(3):
                    d = ws[m] * sides[m][0]
                    if d > 3.5:
                        continue
                    if f["outline"][m]:
                        face = sides[m][1]
                        if face > 0.0:
                            light += rim * face ** 0.6 * band(d, 0.45, 0.4, 0.6)
                            shade += gather * band(d, 1.3, 0.6, 1.0)
                        else:
                            if P["rim_all"]:
                                light += P["rim_all"] * band(d, 0.45, 0.4, 0.6)
                            shade += PIECE_EDGE * -face * band(d, 0.5, 0.5, 1.3) + gather * band(d, 0.4, 0.5, 0.9)
                    else:
                        light += ridge * math.exp(-(d / ridge_w) ** 2)
                if gx is not None and glint:
                    light += glint * math.exp(-((x - gx) ** 2 + (y - gy) ** 2) / (2.0 * glint_s ** 2))
                v = gain * (w0 * vals[0] + w1 * vals[1] + w2 * vals[2])
                if core:
                    v = core[3] * v + core[0] * math.exp(-(x / core[1]) ** 2 - (y / core[2]) ** 2)
                tone = int(255.0 * clamp(v))
                out[(i, j)] = (tone, clamp(shade), clamp(light))
    _piece_points[key] = out
    return out


_piece_shadow = {}


def piece_shadow(alpha):
    """The hanging piece's shadow at PIECE_K per pixel: its body's alpha (0 to 255) blurred, moved down (what leaves
    the cell's foot comes in at its top, as on the canvas)."""
    if alpha not in _piece_shadow:
        L, k = A.L, PIECE_K
        W, H = L.POUR_PIECE_W * k, L.POUR_PIECE_H * k
        grid = [0.0] * (W * H)
        for i, j in piece_points(0.0, PIECE_MATERIAL):
            grid[j * W + i] = float(alpha)
        grid = gauss(grid, W, H, PIECE_SHADOW_BLUR * k)
        down = int(round(PIECE_SHADOW_DOWN * k))
        _piece_shadow[alpha] = grid[(H - down) * W:] + grid[:(H - down) * W]
    return _piece_shadow[alpha]


def piece_matt(pts, W, H, sigma):
    """A matt body: what is in it blurred by sigma points, its outline kept (its alpha at most twice the blurred
    one's, never more than its own)."""
    chans = [gauss([p[c] if p else 0.0 for p in pts], W, H, sigma) for c in range(4)]
    out = [None] * (W * H)
    for n, p in enumerate(pts):
        ba = chans[3][n]
        if p is None or ba <= 0.0:
            continue
        a = min(p[3], 2.0 * ba)
        out[n] = (chans[0][n] / ba * a, chans[1][n] / ba * a, chans[2][n] / ba * a, a)
    return out


def piece_frame(T, dark, turn, shadow, P):
    """The piece turned by turn degrees (with its hanging shadow) in the tile's tones T and the material P at PIECE_K
    points per pixel in its cell: (premultiplied r, g, b, alpha) per point, None where clear."""
    L, k, white = A.L, PIECE_K, A.WHITE
    W, H = L.POUR_PIECE_W * k, L.POUR_PIECE_H * k
    stops = ((0.0, mixed(T["deep"], BLACK, 0.25)), (0.28, T["deep"]), (0.5, T["mid"]), (0.7, T["light"]),
             (0.88, T["foot"]), (1.0, white))

    def tone(v):
        for (t0, c0), (t1, c1) in zip(stops, stops[1:]):
            if v <= t1:
                return mixed(c0, c1, (v - t0) / (t1 - t0))
        return stops[-1][1]
    table = [tone(i / 255.0) for i in range(256)]
    edge = mixed(T["deep"], BLACK, 0.5) if dark else T["edge"]
    veil = mixed(T["milk"], T["light"], 0.5) if P["milk"] else None
    body = P["alpha"] / 255.0
    pts = [None] * (W * H)
    for (i, j), (t, sh, li) in piece_points(turn, P).items():
        c = table[t]
        if veil:
            c = mixed(c, veil, P["milk"])
        if sh > 0.0:
            c = mixed(c, edge, sh)
        if li > 0.0:
            c = mixed(c, white, li)
        pts[j * W + i] = (c[0] * body, c[1] * body, c[2] * body, body)
    if P["blur"]:
        pts = piece_matt(pts, W, H, P["blur"] * k)
    if shadow:
        under, s = piece_shadow(P["alpha"]), PIECE_SHADOW["dark" if dark else "light"]
        for n in range(W * H):
            a = int(min(255.0, under[n] * s) + 0.5) / 255.0
            if a <= 0.0:
                continue
            p, c = pts[n], T["shadow"]
            if p is None:
                pts[n] = (c[0] * a, c[1] * a, c[2] * a, a)
            else:
                q = 1.0 - p[3]
                pts[n] = (p[0] + c[0] * a * q, p[1] + c[1] * a * q, p[2] + c[2] * a * q, p[3] + a * q)
    return pts


def piece_sprites(T, dark, P):
    """POUR_PIECE, the piece hanging on its holder with its shadow, and POUR_PIECE_TURN, its PIECE_TURNS turn frames
    side by side as it falls (a quarter turn), in the tile's tones and the material P (Stock's: a clear body tinted by
    the liquid's tone, each facet graded (the crown lighter towards its point, the foot facets deep under the girdle
    and light at the point), a light along the outline towards the light, a soft light along the ridges, one glint;
    no drawn outline)."""
    L, k = A.L, PIECE_K
    w, h = L.POUR_PIECE_W, L.POUR_PIECE_H
    W = w * k

    def reduce(pts, img, x0):
        """pts averaged over each pixel's points into img's cell from column x0."""
        for y in range(h):
            for x in range(w):
                s = [0.0, 0.0, 0.0, 0.0]
                for j in range(y * k, y * k + k):
                    for p in pts[j * W + x * k:j * W + x * k + k]:
                        if p is not None:
                            s[0] += p[0]
                            s[1] += p[1]
                            s[2] += p[2]
                            s[3] += p[3]
                n = 4 * (y * img.w + x0 + x)
                img.p[n:n + 4] = [v / (k * k) for v in s]

    hang = A.Image(w, h)
    reduce(piece_frame(T, dark, 0.0, True, P), hang, 0)
    turns = A.Image(w * PIECE_TURNS, h)
    for n in range(PIECE_TURNS):
        reduce(piece_frame(T, dark, 90.0 * n / PIECE_TURNS, False, P), turns, n * w)
    return [A.Sprite("POUR_PIECE", hang), A.Sprite("POUR_PIECE_TURN", turns)]


# The landing (C24, C31; the canvas's impact z2 "Schwer", SR-T6 gen.py impact_pose, toned, impact_frame): from its touch
# the piece goes flat in one go into a thick lens that spreads and runs out. Its frames (pour_layout.h POUR_MELT_N at
# POUR_MELT_FPS, frame k showing MELT_LATE of a frame after its time, so frame 0 is already pressed in) are the landing
# turn frame laid row by row onto the pose's outline (half width per height over its ground, its foot cut off as it
# sinks in), going over into the liquid's look, its points rounded off, the liquid's deep skin along its upper outline;
# drawn on the canvas's MELT_LW x MELT_LH canvas MELT_K times too large with the ground on row MELT_LG, as PIL draws
# it (its blur: three extended box passes each way; its mesh: each band of 4 points a quad read bilinearly), averaged.
MELT_K = 8
MELT_LATE = 0.75
MELT_TURN = 8                   # every piece touches down in this turn frame of POUR_PIECE_TURN
MELT_LW, MELT_LH, MELT_LG = 48, 36, 24
MELT_SKIN = 1.1                 # px: the skin's line


def melt_pose(k):
    """Frame k's pose: the piece's height over its ground (px), its foot cut off (px), its half width hw(a, h0) at
    the height a (h0 its own there), how far it has the liquid's look, its points' rounding (px), its alpha."""
    L = A.L
    tau = (k + MELT_LATE) / L.POUR_MELT_FPS
    run = melt_smooth((tau - 0.22) / 0.28)
    height = 2.0 * PIECE["up"] * math.cos(math.radians(PIECE["tilt"]))
    sy = (0.26 + 0.20 * math.exp(-tau / 0.12) + 0.54 * math.exp(-tau / 0.03)) * (1.0 - 0.36 * run)
    cut = 7.0 * (1.0 - math.exp(-tau / 0.05))
    top = height * sy
    wm = (top / (height - cut)) ** -0.7
    m = melt_smooth(tau / 0.085)
    rb = min(19.0, 100.0 / (1.57 * top))
    return dict(top=top, cut=cut, height=height,
                hw=lambda a, h0: (1.0 - m) * h0 * wm + m * rb * max(0.0, 1.0 - (a / top) ** 2) ** 0.45,
                tone=0.9 * melt_smooth(tau / 0.13), round=min(0.6 + 2.0 * melt_smooth(tau / 0.1), 0.22 * top),
                alpha=1.0 - melt_smooth((tau - 0.32) / 0.18))


def melt_smooth(t):
    t = A.clamp(t)
    return t * t * (3.0 - 2.0 * t)


def box_pass(line, l, ww, fw):
    pad = [0.0] * (l + 1) + line + [0.0] * (l + 1)
    s = [0.0]
    for v in pad:
        s.append(s[-1] + v)
    return [ww * (s[x + 2 * l + 2] - s[x + 1]) + fw * (pad[x] + pad[x + 2 * l + 2]) for x in range(len(line))]


def box_gauss(grid, w, h, radius):
    """grid (w * h values, 0 outside) blurred as PIL's GaussianBlur(radius): three extended box passes each way."""
    if radius <= 0.0:
        return grid
    sigma2 = radius * radius / 3.0
    l = math.floor((math.sqrt(12.0 * sigma2 + 1.0) - 1.0) / 2.0)
    r = l + (2 * l + 1) * (l * (l + 1) - 3 * sigma2) / (6 * (sigma2 - (l + 1) * (l + 1)))
    ww = 1.0 / (2 * r + 1)
    fw, l = (1.0 - (2 * l + 1) * ww) / 2.0, int(l)
    rows = [grid[j * w:(j + 1) * w] for j in range(h)]
    for _ in range(3):
        rows = [box_pass(row, l, ww, fw) if any(row) else row for row in rows]
    cols = [list(c) for c in zip(*rows)]
    for _ in range(3):
        cols = [box_pass(col, l, ww, fw) if any(col) else col for col in cols]
    return [v for row in zip(*cols) for v in row]


def melt_toned(master, T, q):
    """The landing turn frame going over into the liquid's look by q / 16 (its facets soften, its colours go to the
    liquid's middle tone): premultiplied r, g, b and alpha (0 to 255) per point."""
    L, k = A.L, MELT_K
    W, H = L.POUR_PIECE_W * k, L.POUR_PIECE_H * k
    a = [p[3] if p else 0.0 for p in master]
    C = [[p[c] / p[3] if p else 0.0 for p in master] for c in range(3)]
    if q:
        s = q / 16.0
        sa = box_gauss(a, W, H, 1.6 * k * s)
        soft = [box_gauss([p[c] if p else 0.0 for p in master], W, H, 1.6 * k * s) for c in range(3)]
        flat = mixed(T["mid"], T["light"], 0.3)
        t1, t2 = min(1.0, 1.5 * s), 0.8 * s
        for c in range(3):
            C[c] = [(C[c][n] * (1.0 - t1) + soft[c][n] / sa[n] * t1) * (1.0 - t2) + flat[c] * t2 if a[n] > 0.0
                    else 0.0 for n in range(W * H)]
    return [[C[c][n] * a[n] for n in range(W * H)] for c in range(3)] + [[v * 255.0 for v in a]]


def melt_frame(master, sil, T, k, toned):
    """Frame k on the canvas, MELT_K points per px: premultiplied r, g, b and alpha (0 to 255) per point."""
    L, K, clamp = A.L, MELT_K, A.clamp
    pose = melt_pose(k)
    q = int(round(16.0 * clamp(pose["tone"])))
    if q not in toned:
        toned[q] = melt_toned(master, T, q)
    src = toned[q]
    MW, MH = L.POUR_PIECE_W * K, L.POUR_PIECE_H * K
    W, H = MELT_LW * K, MELT_LH * K
    top, cut, height = pose["top"], pose["cut"], pose["height"]
    foot = L.POUR_PIECE_H / 2.0 - 1.0 + height / 2.0       # the foot point in the piece's cell, px from its top

    def at(Y):
        """Where the canvas's row Y reads the piece: its row there (points) and its half width (points)."""
        a = MELT_LG - Y / float(K)
        s = cut + a * (height - cut) / max(0.05, top)
        h0 = sil[int(min(max((foot - s) * K, 0), MH - 1))] if 0.0 <= s <= height else 0.0
        w = min(max(pose["hw"](max(a, 0.0), h0) / max(h0, 0.7), 0.25), 9.0)
        return (foot - s) * K, (MELT_LW / 2.0) / w * K

    out = [[0.0] * (W * H) for _ in range(4)]
    c0 = MW / 2.0
    for y0 in range(0, MELT_LG * K, 4):
        (s0, h0), (s1, h1) = at(y0), at(y0 + 4)
        if max(s0, s1) < -K or min(s0, s1) > MH + K:
            continue
        for Y in range(y0, y0 + 4):
            v = (Y - y0 + 0.5) / 4.0
            sy, hh = s0 + v * (s1 - s0), h0 + v * (h1 - h0)
            if not 0.0 <= sy < MH or hh <= 0.0:
                continue
            y_ = math.floor(sy - 0.5)
            dy = sy - 0.5 - y_
            ya, yb = min(max(y_, 0), MH - 1), y_ + 1 if y_ + 1 < MH else None
            rows = []
            for ch in src:
                r1 = ch[ya * MW:(ya + 1) * MW]
                rows.append(r1 if yb is None else [p + (q2 - p) * dy for p, q2 in zip(r1, ch[yb * MW:(yb + 1) * MW])])
            if not any(rows[3]):
                continue
            for X in range(max(0, int((0.5 - c0 / (2.0 * hh)) * W - 1)), min(W, int((0.5 + c0 / (2.0 * hh)) * W + 2))):
                sx = c0 + (2.0 * (X + 0.5) / W - 1.0) * hh
                if not 0.0 <= sx < MW:
                    continue
                x_ = math.floor(sx - 0.5)
                dx = sx - 0.5 - x_
                xa, xb = min(max(x_, 0), MW - 1), min(max(x_ + 1, 0), MW - 1)
                if rows[3][xa] or rows[3][xb]:
                    for c in range(4):
                        out[c][Y * W + X] = rows[c][xa] + (rows[c][xb] - rows[c][xa]) * dx
    A_ = out[3]
    if pose["round"] > 0.15:            # liquid has no points: they shrink away
        b = box_gauss(A_, W, H, 0.55 * pose["round"] * K)
        for n in range(W * H):
            if A_[n] > 0.0:
                t = int(clamp((b[n] / 255.0 - 0.42) * 4.0 + 0.5) * 255.0 + 0.5)
                if t < A_[n]:
                    for c in range(3):
                        out[c][n] *= t / A_[n]
                    A_[n] = float(t)
    for n in range(MELT_LG * K * W, W * H):
        for c in range(4):
            out[c][n] = 0.0
    if pose["tone"] > 0.05:             # as it turns liquid it gets the liquid's skin along its upper outline
        d = int(MELT_SKIN * K)
        e = d * 2 // 3

        def alpha(x, y):
            return A_[y * W + x] if 0 <= x < W and 0 <= y < H else 0.0
        edge = [max(0.0, A_[y * W + x] - min(alpha(x, y - d), alpha(x - e, y - e), alpha(x + e, y - e)))
                if A_[y * W + x] else 0.0 for y in range(H) for x in range(W)]
        edge = box_gauss(edge, W, H, 0.25 * K)
        kk, deep = 0.85 * clamp(pose["tone"]), T.get("skin", T["deep"])
        for n in range(W * H):
            ao = A_[n] / 255.0
            sa = int(min(255.0, edge[n] * kk) + 0.5) / 255.0 if ao > 0.0 else 0.0
            if sa > 0.0:
                tot = sa + ao * (1.0 - sa)
                for c in range(3):
                    out[c][n] = (deep[c] * sa + out[c][n] * (1.0 - sa)) / tot * ao
    return out


def melt_sprite(T, dark, P):
    """POUR_MELT: the landing's POUR_MELT_N frames in rows of POUR_MELT_COLS, each POUR_MELT_W x POUR_MELT_H, its ground
    under the cell's last row and the place of the touch on its middle column's left edge (the canvas's columns
    MELT_LW / 2 - POUR_MELT_W / 2 on, its rows over MELT_LG)."""
    L, K = A.L, MELT_K
    MW, MH = L.POUR_PIECE_W * K, L.POUR_PIECE_H * K
    w, h, n = L.POUR_MELT_W, L.POUR_MELT_H, L.POUR_MELT_N
    master = piece_frame(T, dark, 90.0 * MELT_TURN / PIECE_TURNS, False, P)
    sil = []
    for j in range(MH):                 # the turn frame's half width per row (px)
        xs = [i for i in range(MW) if master[j * MW + i] and master[j * MW + i][3] * 255.0 > 60]
        sil.append((max(xs) - min(xs) + 1) / (2.0 * K) if xs else 0.0)
    cols = L.POUR_MELT_COLS
    img, toned, W, x0 = A.Image(w * cols, h * ((n + cols - 1) // cols)), {}, MELT_LW * K, MELT_LW // 2 - w // 2
    for f in range(n):
        out = melt_frame(master, sil, T, f, toned)
        for y in range(MELT_LG):
            for x in range(MELT_LW):
                s = [sum(sum(out[c][j * W + x * K:j * W + x * K + K]) for j in range(y * K, y * K + K)) / (K * K)
                     for c in range(4)]
                inside = 0 <= x - x0 < w and y >= MELT_LG - h
                if not inside:
                    assert s[3] < 0.5, "POUR_MELT: frame %d reaches past its cell at %d, %d" % (f, x, y)
                    continue
                i = 4 * ((f // cols * h + y - (MELT_LG - h)) * img.w + f % cols * w + x - x0)
                img.p[i:i + 4] = [s[0], s[1], s[2], s[3] / 255.0]
    return A.Sprite("POUR_MELT", img)


# The holder B "Zange" (C24; the canvas's SR-T5 gen.py holder_art h2): two angled arms from one hinge, their points on
# the piece's upper facets, a rivet on the hinge, a short stem up into the clamp; in px from the line's middle over the
# holder, drawn TONGS_K times too large and averaged, in pour.h's poses (holds, opens, lets go, springs back, empty) by
# how far open (a share of TONGS_OPEN degrees each arm). Each stroke has a little form: a shade along its lower right,
# a light along its upper left. The rivet's steel is the line's colour TONGS_STEEL of the way to the ink (light and
# dark schemes), the canvas's steel.
TONGS_K = 8
TONGS_OPEN = 24.0
TONGS_POSES = (0.0, 0.5, 1.0, 0.75, 0.375)
TONGS_HINGE = (0.0, 3.7)
TONGS_ARM = ((-0.9, 2.9), (0.0, 3.7), (4.7, 7.6), (2.7, 11.3))
TONGS_STROKE = 1.4
TONGS_RIVET = 1.3
TONGS_STEM = (-0.8, 1.6, 0.8, 3.7)
TONGS_STEEL = dict(light=0.33, dark=0.64)


def tongs_sprite(line, ink, teal, dark, own):
    """POUR_TONGS: the tongs in their poses side by side, each POUR_TONGS_W x POUR_TONGS_H with the line's middle at
    the cell's top middle. own (a look's material, pour_sprites) may give the arms' stroke and their three tones
    (shade, body, light; the stem in the body's), the rivet's radius and its three tones."""
    L, k, white = A.L, TONGS_K, A.WHITE
    w, h = L.POUR_TONGS_W, L.POUR_TONGS_H
    greens = (mixed(teal, BLACK, 0.38), mixed(teal, teal, 0.0), mixed(teal, white, 0.5))
    steel = mixed(line, ink, TONGS_STEEL["dark" if dark else "light"])
    steels = (mixed(steel, BLACK, 0.3), steel, mixed(steel, white, 0.55))
    width, greens = own.get("stroke", TONGS_STROKE), own.get("arm", greens)
    rivet, steels = own.get("rivet", TONGS_RIVET), own.get("joint", steels)
    hx, hy = TONGS_HINGE

    def turned(pt, ang):
        x, y = pt[0] - hx, pt[1] - hy
        return (hx + x * math.cos(ang) - y * math.sin(ang), hy + x * math.sin(ang) + y * math.cos(ang))

    def oval(cx, cy, r):
        """A disc as the canvas's drawing grid takes it: its box cut to whole points, the ellipse filling that box."""
        x0, x1, y0, y1 = (math.floor(v * k) for v in (cx - r, cx + r, cy - r, cy + r))
        mx, my, rx, ry = (x0 + x1 + 1) / 2.0, (y0 + y1 + 1) / 2.0, (x1 - x0 + 1) / 2.0, (y1 - y0 + 1) / 2.0

        def inside(x, y):
            u, v = (math.floor(x * k) + 0.5 - mx) / rx, (math.floor(y * k) + 0.5 - my) / ry
            return u * u + v * v <= 1.0
        return inside

    def band(ax, ay, bx, by, n):
        """A straight piece of a line n points wide from a to b (whole points), as the canvas's drawing took it: a
        four-cornered shape whose corners lie on whole points, filled on whole points, half a point beyond its sides
        across a row."""
        ex, ey = bx - ax, by - ay
        if not (ex or ey):
            return lambda i, j: i == ax and j == ay
        side = (n - 1) / 2.0
        hi, lo = math.floor(side + 0.5) / math.hypot(ex, ey), math.ceil(side - 0.5) / math.hypot(ex, ey)
        dxl, dxh, dyl, dyh = (math.ceil(v - 0.5) if v >= 0 else -math.ceil(-v - 0.5)
                              for v in (lo * ey, hi * ey, lo * ex, hi * ex))
        quad = ((ax - dxl, ay + dyh), (bx - dxl, by + dyh), (bx + dxh, by - dyl), (ax + dxh, ay - dyl))
        top, low = min(y for _, y in quad), max(y for _, y in quad)
        edges = list(zip(quad, quad[1:] + quad[:1]))

        def inside(i, j):
            if not top <= j <= low:
                return False
            xs = [x0 + (j - y0) * (x1 - x0) / (y1 - y0) for (x0, y0), (x1, y1) in edges
                  if y0 != y1 and min(y0, y1) <= j <= max(y0, y1)]
            return bool(xs) and math.floor(min(xs) + 0.5) <= i <= math.ceil(max(xs) - 0.5)
        return inside

    def stroke(pts, width, color, dx=0.0, dy=0.0):
        """A line of width through pts with round ends (and joints), as the canvas drew it, as (inside test, colour)."""
        q = [(x + dx, y + dy) for x, y in pts]
        n = round(width * k)
        g = [(math.floor(x * k), math.floor(y * k)) for x, y in q]
        bands = [band(ax, ay, bx, by, n) for (ax, ay), (bx, by) in zip(g, g[1:])]
        ends = [oval(x, y, width / 2.0) for x, y in (q[0], q[-1])]
        if n > 4:
            ends += [oval(x, y, width / 2.0 - 1.0 / k) for x, y in q[1:-1]]
        x0, x1 = min(x for x, _ in q) - width, max(x for x, _ in q) + width
        y0, y1 = min(y for _, y in q) - width, max(y for _, y in q) + width

        def inside(x, y):
            if not (x0 <= x <= x1 and y0 <= y <= y1):
                return False
            i, j = math.floor(x * k), math.floor(y * k)
            return any(b(i, j) for b in bands) or any(e(x, y) for e in ends)
        return inside, color

    def disc(cx, cy, r, color):
        return oval(cx, cy, r), color

    def layers(o):
        # the stem on the canvas's drawing grid, as its box takes it: from the point its corner is in to the one
        # before its far corner's
        ix0, iy0, ix1, iy1 = (math.floor(v * k - (n > 1)) for n, v in enumerate(TONGS_STEM))
        out = [((lambda x, y: ix0 <= math.floor(x * k) <= ix1 and iy0 <= math.floor(y * k) <= iy1), greens[1])]
        for side in (-1, 1):
            pts = [turned(pt, math.radians(-TONGS_OPEN * o)) for pt in TONGS_ARM]
            pts = [(side * x, y) for x, y in pts]
            out += [stroke(pts, width, greens[0], 0.2, 0.26), stroke(pts, width, greens[1]),
                    stroke(pts, width * 0.38, greens[2], -0.18, -0.22)]
        r = rivet
        out += [disc(hx + 0.15, hy + 0.2, r, steels[0]), disc(hx, hy, r, steels[1]),
                disc(hx - 0.25 * r, hy - 0.3 * r, 0.4 * r, steels[2])]
        return out[::-1]

    img = A.Image(w * len(TONGS_POSES), h)
    for n, o in enumerate(TONGS_POSES):
        top = layers(o)
        for py in range(h):
            for px in range(w):
                acc = [0.0, 0.0, 0.0, 0.0]
                for j in range(k):
                    y = py + (j + 0.5) / k
                    for i in range(k):
                        x = px + (i + 0.5) / k - w / 2.0
                        for inside, c in top:
                            if inside(x, y):
                                acc[0] += c[0]
                                acc[1] += c[1]
                                acc[2] += c[2]
                                acc[3] += 1.0
                                break
                m = 4 * (py * img.w + n * w + px)
                img.p[m:m + 4] = [v / (k * k) for v in acc]
    return A.Sprite("POUR_TONGS", img)


def pour_sprites(tone, dark, teal, line, ink, material=None):
    """The installing screen's images in a look's tones (each an (r, g, b) of 0 to 255): tone the liquid's and the
    pieces' (the look's tone at the liquid's strength, liquid_tone), dark whether the scheme is a dark one, teal the
    marks' and the clamp's, line the line's, its knot's and the rings', ink what the tongs' steel leans towards. Drawn
    in Stock's material; a look draws them in its own by material, a dict whose entries each replace a part of Stock's
    (none is needed; the names, sizes and places stay):
      tones    the tile's tones (tile_tones' names, and "milk" for the piece's veil, "under" for the tone under the
               surface, "skin" for a landing's skin: both the deep tone unless given) that differ from Stock's rule
      layers   f(the tile's shapes and helpers) -> {layer name: paint function} (tile_sprites); a paint function
               whose left and right halves differ carries the attribute whole (True)
      surface  the surface's light line: strength, core, out, inner (surface_strips)
      piece    the piece's material numbers that differ from Stock's (PIECE_MATERIAL)
      tongs    the arms' stroke and tones, the rivet's radius and tones (tongs_sprite)
      clamp_r  the clamp's corner radius, px"""
    L, cover = A.L, A.cover
    white = A.WHITE
    material = material or {}

    def holder(name):           # the clamp on the line (the canvas's): the tongs swing under it (tongs_sprite)
        img = A.Image(L.POUR_HOLDER_W, L.POUR_HOLDER_H)
        clamp = A.Box(0.0, 0.0, L.POUR_HOLDER_W, L.POUR_HOLDER_H, material.get("clamp_r", 2.0))
        img.paint(lambda u, v: (cover(clamp.sd(u, v)), teal))
        return A.Sprite(name, img)

    def strip(name):
        img = A.Image(8, 2)
        img.paint(lambda u, v: (0.8, line))
        return A.Sprite(name, img, (0, 0, 0, 0), (2, 0, 2, 0))

    def knot(name):
        n = L.POUR_KNOT
        img = A.Image(n, n)
        disc = A.sd_circle(n / 2.0, n / 2.0, n / 2.0)
        img.paint(lambda u, v: (cover(disc(u, v)), line))
        return A.Sprite(name, img)

    def mark(name, state):      # a step's mark: to come a ring, under way a ring with a dot in the accent tone, done
        n = L.POUR_MARK         # the check on a disc
        img = A.Image(n, n)
        c = n / 2.0
        disc = A.sd_circle(c, c, c)
        if state == "done":
            img.paint(lambda u, v: (cover(disc(u, v)), teal))
            tick = A.sd_strokes(A.lines_of(A.tabler("check")), 9 / 24.0, 3.6, c - 4.5, c - 4.5)
            img.paint(lambda u, v: (cover(tick(u, v)), white))
        elif state == "now":
            dot = A.sd_circle(c, c, 2.2)
            img.paint(lambda u, v: (cover(min(abs(disc(u, v) + 1.0) - 1.0, dot(u, v))), teal))
        else:
            img.paint(lambda u, v: (cover(abs(disc(u, v) + 0.75) - 0.75), line))
        return A.Sprite(name, img)

    T = dict(tile_tones(tone, dark), **material.get("tones", {}))
    P = dict(PIECE_MATERIAL, **({} if dark else PIECE_LIGHT_SCHEMES))
    P.update(material.get("piece", {}))
    take = own_take()
    piece = T if dark or not take or "piece" not in take else \
        dict(T, **{k: liquid_step(T[k], take["piece"]) for k in ("deep", "mid", "light", "foot")})
    return tile_sprites(T, dark, material) + piece_sprites(piece, dark, P) + [melt_sprite(T, dark, P),
            holder("POUR_HOLDER"), tongs_sprite(line, ink, teal, dark, material.get("tongs", {})), strip("POUR_LINE"),
            knot("POUR_KNOT"),
            mark("POUR_RING", "later"), mark("POUR_NOW", "now"), mark("POUR_TICK", "done"),
            A.Sprite("POUR_HEAD", A.symbol(A.tabler("download"), L.POUR_HEAD_ICON, teal, 2.6, light=False)),
            # the check of the end's line (its name from when it stood in the title)
            A.Sprite("POUR_TITLE_DONE", A.symbol(A.tabler("check"), L.POUR_END_TICK, teal, 3.0, light=False))]
