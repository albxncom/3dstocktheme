"""Nachtglas's drawing of 3DStockTheme's images (the application's half of art.py --look night; app_art.py is the
hook): the scan frame's corners in light, the carousel's frames and the installing screen's in the look's material;
with Nachtglas's own helpers (look_night.py) and the generator's (A: app_images.py)."""
import math

import app_images as A
from look_night import BLACK, GLOW, ICE, SMOKE, WHITE, own


def bracket(name, path):
    """One corner of the scan frame (art.py bracket), in light."""
    L = A.L
    reach = L.BRACKET_INSET + L.BRACKET_ARM + L.BRACKET_STROKE
    lines = A.path_lines(path)
    xs = [p[0] for pts, _ in lines for p in pts]
    ys = [p[1] for pts, _ in lines for p in pts]
    ox = 0 if min(xs) < L.BRACKET / 2 else L.BRACKET - reach
    oy = 0 if min(ys) < L.BRACKET / 2 else L.BRACKET - reach
    img = A.Image(reach, reach)
    A.stroke(img, lines, 1.0, L.BRACKET_STROKE, own(ICE), -ox, -oy)
    return A.Sprite(name, img)


def frame(name, n, rim):
    """A carousel pane's rim of Shard glass round its picture (art.py pane_rim), in the look's own violet into blue
    (not the Shard glass's violet into teal: teal is what brings in) turned with the ground; in the sheet, so each
    scheme's sheet has its own (sheet)."""
    return A.pane_rim(name, "shard", n, rim, tones=(own(GLOW["VIOLET"]), own(GLOW["BLUE"])))


def pour_sprites():
    """The installing screen's images (pour_art.py: the generator every look draws its tile with) in Nachtglas's
    tones and its own material (pour_material): the liquid and the pieces are the loader's light (COL_POUR =
    COL_LOADER), the line, its knot and the rings half way from the dots' tone to the edge light, the ground dark in
    every scheme."""
    import pour_art
    L = A.L
    pour_art.A = A
    tone = A.rgb(L.COL_POUR)
    sprites = pour_art.pour_sprites(tone=tone, dark=A.schemes.luminance(A.rgb(L.COL_CARD)) < 0.2,
                                    teal=A.rgb(L.COL_TEAL), line=A.lerp(A.rgb(L.COL_DOTS), own(ICE), 0.5),
                                    ink=A.rgb(L.COL_INK), material=pour_material(pour_art, tone))
    return [done_mark() if s.name == "POUR_TICK" else s for s in sprites]


def done_mark():
    """A finished step's mark: pour_art's disc and check, the check in the smoked glass instead of white (G4-A).
    White on the look's pale teal stood at 1.4 to 1 and was faint at true size; the smoke stands at about 14 to 1
    on every ground, as if the check were cut through the disc into the glass."""
    L, cover = A.L, A.cover
    n = L.POUR_MARK
    img = A.Image(n, n)
    c = n / 2.0
    disc = A.sd_circle(c, c, c)
    tick = A.sd_strokes(A.lines_of(A.tabler("check")), 9 / 24.0, 3.6, c - 4.5, c - 4.5)
    teal, smoke = A.rgb(L.COL_TEAL), own(SMOKE)
    img.paint(lambda u, v: (cover(disc(u, v)), teal))
    img.paint(lambda u, v: (cover(tick(u, v)), smoke))
    return A.Sprite("POUR_TICK", img)


def pour_material(P, tone):
    """The tile in Nachtglas's material (C48: the canvas's Nachtglas as shown, SR-T7 gen.py layers_night): smoked
    glass the ground shows through, the edge light in the look's ice, no sheen on the face; the liquid is the loader's
    light, lit from within, its surface the brightest line; the piece smoked crystal with the light inside it, its
    outline and ridges in the edge light; the tongs thin arms of light, their joint a point of the teal. P is pour_art
    (its helpers), tone the scheme's pour tone. The tones are the look's own in every scheme (own: ICE, SMOKE; the
    card's tone; the loader's tone at the liquid's strength, pour_art.liquid_tone, the liquid's inner light and deep
    tone apart from it in lightness alone, pour_art.liquid_step). For the canvas's weak point the light keeps its
    colour: the liquid is lit by a light tint of its own tone and not by white, the end's glow and halo likewise; the
    level reads by the white surface line over a deeper tone and the deep wall, the landing by a light skin
    (host/test_look.c, night_tile_material)."""
    rgb = lambda c: tuple(float(round(v)) for v in c)
    ice, smoke, teal = rgb(own(ICE)), rgb(own(SMOKE)), rgb(A.rgb(A.L.COL_TEAL))
    light = rgb(P.liquid_tone(tone))
    deep = P.liquid_step(light, -0.25)
    hot = P.hls(light, light=0.86, ks=1.5)                  # the light inside the piece and at the end
    inside = P.liquid_step(light, 0.10)                     # the light inside the liquid: lighter, in its colour
    tones = dict(deep=P.mixed(deep, smoke, 0.4), mid=P.hls(light, kl=0.9), light=P.hls(light, light=0.8, ks=1.2),
                 foot=P.hls(light, light=0.9, ks=1.3), under=P.mixed(light, deep, 0.25), skin=P.mixed(hot, WHITE, 0.6),
                 shade=BLACK, edge=BLACK, shadow=BLACK, lit=hot, halo=P.mixed(light, hot, 0.5))
    arm = P.mixed(ice, light, 0.25)
    return dict(tones=tones, surface=(0.9, 0.6, 0.6, 0.9),
                layers=lambda G: night_layers(G, ice, smoke, light, deep, inside, rgb(A.rgb(A.L.COL_CARD))),
                piece=dict(key=0.5, spread=0.9, grade=0.6, rim=0.95, rim_all=0.38, ridge=(0.5, 0.36), glint=(0.0, 0.8),
                           core=(0.82, 2.9, 4.2, 0.45), alpha=238),
                tongs=dict(stroke=1.15, arm=(P.mixed(arm, smoke, 0.6), arm, WHITE), rivet=1.25,
                           joint=(P.mixed(teal, BLACK, 0.45), teal, WHITE)))


def night_layers(G, ice, smoke, light, deep, inside, card):
    """The tile's layers in smoked glass (pour_art.tile_sprites' G: its shapes and helpers), each a paint function
    (x, y, n) as Stock's. It reads as glass by what the light does in it: the ground shows through the smoke, the
    light from above enters at the top edge and thins out downwards inside the body, the rim seen through the pane's
    thickness lies in shade, and the edge light is strongest along the top and thin down the sides. The liquid is lit
    from within: lightest in a broad glow in its middle, deep along the wall, where the ground comes through."""
    T, band, cov, inward, outward, over, mixed = G.T, G.band, G.cov, G.inward, G.outward, G.over, G.mixed
    clamp, smooth, tile, cav, pic, down, m, tw = A.clamp, A.smooth, G.tile, G.cav, G.pic, G.down, G.m, G.tw
    rim = A.L.POUR_RIM
    iw, ih = tw - 2 * rim, G.th - 2 * rim
    halo_blur, foot_blur, shadow_blur = G.blurred(0, 8.0), G.blurred(4, 5.0), G.blurred(4, 4.0)
    halo_rim = mixed(T["halo"], WHITE, 0.15)

    def halo(x, y, n):
        d = tile(x, y)
        return over(((0.72 * min(halo_blur(x, y), 1.0), T["halo"]),
                     (0.55 * math.exp(-outward(d) / 2.0) * (1.0 - cov(d, n)), halo_rim)))

    def foot(x, y, n):
        return over(((0.5 * foot_blur(x, y) * (1.0 - cov(tile(x, y), n)) * smooth(0.3, 0.9, down(y)), T["light"]),))

    def back(x, y, n):
        d = tile(x, y)
        c, t = cov(d, n), down(y)
        return over(((0.5 * shadow_blur(x, y) * (1.0 - c), BLACK),
                     (c * (0.16 + 0.16 * t), smoke),
                     # the light from above inside the body: most under the top edge, thinning out to the foot
                     (0.22 * c * (0.25 + 0.75 * (1.0 - smooth(0.0, 0.6, t)) ** 1.5), ice),
                     (0.16 * band(inward(d), 3.2, 1.6, 5.0, 0.3, 9.0) * c * G.top_w(t), ice)))

    stops = ((0.0, mixed(light, inside, 0.15)), (0.5, light), (1.0, mixed(light, deep, 0.5)))
    glow, wall = mixed(inside, WHITE, 0.35), mixed(deep, card, 0.5)

    def graded(t):
        t = clamp(t)
        (t0, c0), (t1, c1) = stops[:2] if t <= stops[1][0] else stops[1:]
        return mixed(c0, c1, (t - t0) / (t1 - t0))

    def liquid(x, y, n):
        d = cav(x, y)
        c = cov(d, n)
        if c <= 0.0:
            return None
        dd, u, t = inward(d), (x - m - rim) / iw, (y - m - rim) / ih
        a, r, g, b = over(((0.96 * c, graded(t)),
                           # the light inside: from a few px off the wall inwards, most in a broad glow in the middle
                           (0.5 * smooth(4.0, 15.0, dd) * c, inside),
                           (0.4 * math.exp(-((u - 0.46) / 0.52) ** 2 - ((t - 0.56) / 0.48) ** 2) * c, glow),
                           # along the wall the liquid is thin: deep, and the ground's colour comes through
                           (0.62 * math.exp(-dd / 2.8) * c, wall)))
        k = 0.96 * c / a        # its colour as laid, its alpha the shape's alone
        return 0.96 * c, r * k, g * k, b * k

    def lit(x, y, n):
        return over(((cov(cav(x, y), n) * clamp(0.5 - 0.2 * down(y)), T["lit"]),))

    leaving = mixed(light, WHITE, 0.6)

    def front(x, y, n):
        d, q = tile(x, y), pic(x, y)
        c, cp = cov(d, n), cov(q, n)
        if c <= 0.0 and q > 16.0:
            return None
        t, u, tp = down(y), (x - m) / tw, (y - G.p_y) / G.p_h
        dd, dq = inward(d), outward(q)
        p_top, p_low = 1.0 - 0.75 * smooth(0.0, 0.9, tp), smooth(0.55, 0.95, tp)
        hole = smooth(0.2, 1.2, dq) * (1.0 - cp) ** 2
        return over((
            (0.10 * c * hole, smoke),
            # the rim through the pane's thickness: in shade, deeper low
            (0.34 * band(dd, 2.4, 1.3, 2.2, 0.25, 5.0) * c * (0.55 + 0.45 * t), BLACK),
            # the edge light over the whole rim (POUR_RIM, so the top edge stays closed): graded across the edge,
            # strongest along the top, thin down the sides, a breath at the foot
            (0.95 * band(dd, 0.7, 0.7, 0.9) * c * (1.0 - 0.84 * smooth(0.0, 0.8, t)), ice),
            (0.5 * band(dd, 1.7, 0.7, 1.3, 0.2, 4.0) * c * G.top_w(t) * clamp(1.0 - 0.55 * (2.0 * u - 1.0) ** 2), ice),
            # the light that leaves at the foot, inside the low edge: in the liquid's tone
            (0.3 * band(dd, 2.2, 0.8, 1.4) * c * G.low_w(t) * clamp(1.0 - 0.35 * abs(2.0 * u - 1.0) ** 3), leaving),
            (0.45 * band(dq, 0.0, 0.5, 2.2, 0.2, 4.0) * (1.0 - cp) * p_top, BLACK),
            (0.3 * band(dq, 1.2, 0.6, 1.0) * (1.0 - cp) * p_low, ice),
            (0.22 * band(inward(q), 0.0, 0.5, 1.6) * cp * p_top, BLACK)))

    rim_c = mixed(ice, WHITE, 0.6)

    def lit_rim(x, y, n):
        d = tile(x, y)
        c = cov(d, n)
        return over(((0.75 * math.exp(-outward(d) / 2.2) * (1.0 - c), T["lit"]),
                     (0.9 * band(inward(d), 0.6, 0.6, 1.2) * c, rim_c)))

    return dict(halo=halo, foot=foot, back=back, liquid=liquid, lit=lit, front=front, lit_rim=lit_rim)


def sprites(base, index):
    """The application's sprites (base) in Nachtglas (index: the scheme, which A.L is drawn in): the scan frame's
    corners redrawn under their names, the rest kept in the look's colours, the installing screen's after them."""
    L = A.L
    i, a, b = L.BRACKET_INSET, L.BRACKET_ARM, L.BRACKET - L.BRACKET_INSET
    mine = [
        bracket("BRACKET_TL", "M{0} {1}V{0}h{2}".format(i, i + a, a)),
        bracket("BRACKET_TR", "M{0} {1}h{2}v{2}".format(b - a, i, a)),
        bracket("BRACKET_BR", "M{0} {1}v{2}h-{2}".format(b, b - a, a)),
        bracket("BRACKET_BL", "M{0} {1}H{2}v-{3}".format(i + a, b, i, a)),
    ]
    redrawn = {s.name: s for s in mine}
    missing = set(redrawn) - {s.name for s in base}
    if missing:
        raise SystemExit("app_look_night.py: names that are not the atlas's: %s" % sorted(missing))
    return [redrawn.get(s.name, s) for s in base] + pour_sprites()


def sheet(names):
    """The scheme's sheet: the application's Shard one of 0.9.3, the carousel's frames in the look's tones."""
    made = {s.name: s for s in A.sheet_sprites("shard")}
    for name, n, rim in A.PANE_CLASSES:
        made[name + "_RIM"] = frame(name, n, rim)
    return [made[name] for name in names]
