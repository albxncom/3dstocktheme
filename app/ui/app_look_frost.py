"""Frost's drawing of 3DStockTheme's images (the application's half of art.py --look frost; app_art.py is the hook):
the carousel panes' rims in the scheme's tones and the installing screen's in Frost's material; with Frost's own
helpers (look_frost.py) and the generator's (A: app_images.py)."""
import math

import app_images as A
import schemes
from look_frost import BLACK, WHITE, hex_of, mix, tokens


def pane_rim(T, name, n, rim):
    """A carousel pane's rim as art.py draws the Shard one (pane_rim), its glass and the haze inside it in the
    scheme's tones (T.frame, T.accent) instead of the violet running into teal."""
    pad = A.L.PANE_PAD
    box, hole = A.pane_hole(n, rim, pad)
    img = A.Image(n + 2 * pad, n + 2 * pad + 2)
    img.paint(lambda x, y: (0.05 * A.cover(hole.sd(x, y)), WHITE))
    img.paint(lambda x, y: (0.26 * A.cover(hole.sd(x, y)) * math.exp(hole.sd(x, y) / 1.8), T.accent))
    for where, wide, a in ((0.62 * n, 0.10 * n, 0.14), (0.86 * n, 0.035 * n, 0.11)):
        img.paint(lambda x, y, where=where, wide=wide, a=a:
                  (a * A.cover(hole.sd(x, y)) * A.cover(abs(x + y - 2 * pad - where) / A.SQRT2 - wide / 2.0), WHITE))
    A.glass(img, lambda x, y: max(box.sd(x, y), -hole.sd(x, y)), "shard", "icon", n=n, rim=1.1, bands=(),
            tint=T.frame, diag=(2 * pad + 0.6 * n, 2 * pad + 1.8 * n), alpha=0.88)
    return A.Sprite(name, img, (pad, pad, pad, pad + 2))


# ---------------------------------------------------------------- the installing screen (R9, the tile d)
def pour_sprites():
    """The installing screen's images (pour_art.py: the generator every look draws its tile with) in Frost's tones and
    its own material (pour_material): the tile stands on Frost's pane (dark where the pane's tone is), the line, its
    knot and the rings in the scheme's frame tone (as the carousel panes' rims), the liquid and the pieces in the
    scheme's pour tone (schemes.py, COL_POUR) at the liquid's strength (pour_art.liquid_tone)."""
    import pour_art
    pour_art.A = A
    T = tokens()
    edge = A.rgb(hex_of(mix(T.frame[1] if T.dark else T.frame[0], T.pane[1], 0.35)))
    glow = A.rgb(A.L.COL_POUR)
    tone = pour_art.liquid_tone(glow)
    dark = schemes.luminance(A.rgb(hex_of(T.pane[0]))) < 0.2
    return pour_art.pour_sprites(tone=tone, dark=dark, teal=A.rgb(A.L.COL_TEAL), line=edge, ink=edge,
                                 material=pour_material(pour_art, T, tone, edge, dark, glow))


def pour_material(P, T, tone, line, dark, glow):
    """The tile in Frost's material (C48: the canvas's Frost a "ganz mattiert", SR-T7 gen.py layers_frost): frosted
    glass. The frost scatters the light, so every light is broader and softer with no bright core, the shade softer,
    the body milky behind the liquid, and over the whole face inside the polished edge lies a frost in the pane's tone
    with a fine grain, thinner towards the foot. The piece is the same body matt (facets closer in tone, a veil, broad
    soft lights, a soft light for the glint), the tongs thicker and rounder in the frame tone with a bead of ice as
    the joint, the clamp a pill. P is pour_art (its helpers), T the scheme's tokens, tone its pour tone (at the liquid's
    strength), line the line's tone, glow the pour tone as the scheme gives it: the end's light and glow on the dark
    schemes, as soft as before the liquid grew stronger (host/test_ui.c, pour_glow_check). Through the frost on the
    light schemes the liquid's light end and the piece went milky (the canvas's weak point: the piece, the first pool
    and the landing read faint): there the liquid's light end keeps its colour (the shared rule, pour_art.LIQUID_STEPS),
    its wall's deep tone is stronger, the frost is a little thinner and thinner still towards the foot (where the liquid
    stands first), the top light is weaker and shorter (the piece falls through it), and the piece is deeper, its
    outline gathering more of the edge's tone, its veil thinner (host/test_look.c, frost_tile_material)."""
    rgb = lambda c: tuple(float(round(v)) for v in c)
    frame = T.frame[1] if dark else T.frame[0]
    if dark:
        milk = rgb(mix(mix(T.ground, WHITE, 0.5), T.frame[1], 0.2))
        tones = dict(lit=P.mixed(glow, WHITE, 0.55), halo=P.mixed(glow, WHITE, 0.5), shadow=rgb(T.shadow), milk=milk)
        piece = dict(spread=0.8, grade=0.7, rim=0.55, rim_all=0.16, ridge=(0.2, 0.6), glint=(0.30, 1.6), milk=0.14,
                     blur=0.26, alpha=245)
    else:
        milk = rgb(T.pane[0])
        tones = dict(shade=P.mixed(tone, BLACK, 0.45), edge=P.hls(tone, kl=0.7, ks=1.2),
                     shadow=rgb(mix(mix(T.shadow, T.frame[0], 0.5), WHITE, 0.2)), lit=P.mixed(tone, WHITE, 0.4),
                     halo=P.mixed(tone, WHITE, 0.15), milk=milk)
        piece = dict(key=0.74, spread=0.85, grade=0.7, rim=0.5, gather=0.7, ridge=(0.26, 0.6), glint=(0.30, 1.6),
                     milk=0.04, blur=0.26, alpha=250)
    arm = rgb(mix(frame, T.pane[1], 0.15 if dark else 0.18))
    ice = rgb(mix(T.frame[1], WHITE, 0.5))
    shadow = tones["shadow"]
    return dict(tones=tones, layers=lambda G: frost_layers(P, G, line), surface=(0.24, 1.3, 1.0, 1.9), piece=piece,
                tongs=dict(stroke=1.75, arm=(rgb(mix(arm, shadow, 0.3)), arm, rgb(mix(arm, WHITE, 0.6))), rivet=1.6,
                           joint=(rgb(mix(ice, arm, 0.6)), ice, WHITE)),
                clamp_r=3.0)


def frost_layers(P, G, line):
    """The tile's layers in frosted glass (pour_art.tile_sprites' G: its shapes and helpers), each a paint function
    (x, y, n) as Stock's: the halo and the foot's light wider and softer, a soft deep shadow, the milky body, the
    liquid with a softer wall, the frost over the face (grain per pixel, thinner towards the foot), the polished
    edge's soft lights, the recess round the picture softer, the end's light broader."""
    T, dark, band, cov, inward, outward, over = G.T, G.dark, G.band, G.cov, G.inward, G.outward, G.over
    clamp, smooth, tile, cav, pic, down, m, tw = A.clamp, A.smooth, G.tile, G.cav, G.pic, G.down, G.m, G.tw
    halo_blur, foot_blur, shadow_blur = G.blurred(0, 9.0), G.blurred(5, 4.5), G.blurred(3.5, 4.5)

    def halo(x, y, n):
        d = tile(x, y)
        return over((((0.8 if dark else 0.85) * min(halo_blur(x, y), 1.0), T["halo"]),
                     (0.3 * math.exp(-outward(d) / 3.5) * (1.0 - cov(d, n)), T["halo"])))

    def foot(x, y, n):
        return over((((0.36 if dark else 0.38) * foot_blur(x, y) * (1.0 - cov(tile(x, y), n)) *
                      smooth(0.45, 0.95, down(y)), T["light"] if dark else T["mid"]),))

    def back(x, y, n):
        d = tile(x, y)
        c, t = cov(d, n), down(y)
        paints = [((0.55 if dark else 0.30) * shadow_blur(x, y) * (1.0 - c), T["shadow"])]
        if dark:
            paints.append((0.22 * c * clamp(1.0 - 0.4 * t), T["milk"]))
        else:
            paints += [(0.50 * c * clamp(1.0 - 0.3 * t), T["milk"]), (0.10 * c * clamp(0.3 + 0.7 * t), line)]
        paints.append(((0.20 if dark else 0.24) * band(inward(d), 0.6, 0.6, 3.6, 0.35, 8.0) * c, line))
        return over(paints)

    def lit(x, y, n):
        return over(((cov(cav(x, y), n) * clamp(0.5 - 0.25 * down(y)), T["lit"]),))

    # the frost: whole from 3 px inside the polished edge, off the picture's recess, thinner towards the foot (on the
    # light schemes more, where the liquid stands first), a fine grain per pixel (the same every run)
    thin = 0.35 if dark else 0.6
    # the top light: strength, inner fall, long fall's share and reach; on the light schemes weaker, narrower and
    # shorter, so that the piece falling through it under the top edge stays whole
    top = (0.42, 3.8, 0.45, 8.0) if dark else (0.6, 3.0, 0.3, 5.0)

    def front(x, y, n):
        d, q = tile(x, y), pic(x, y)
        c, cp = cov(d, n), cov(q, n)
        if c <= 0.0 and q > 16.0:
            return None
        t, u, tp = down(y), (x - m) / tw, (y - G.p_y) / G.p_h
        dd, dq = inward(d), outward(q)
        p_top, p_low = 1.0 - 0.75 * smooth(0.0, 0.9, tp), smooth(0.55, 0.95, tp)
        hole = smooth(0.2, 1.2, dq) * (1.0 - cp) ** 2
        grain = 1.0 - 0.20 * (((int(x) * 73856093) ^ (int(y) * 19349663)) % 1009) / 1009.0
        return over((
            ((0.24 if dark else 0.26) * smooth(1.2, 3.2, dd) * c * hole * clamp(1.0 - thin * t) * grain, T["milk"]),
            ((0.22 if dark else 0.16) * band(dd, 1.1, 0.8, 4.5, 0.35, 7.0) * c * G.side_w(t), T["edge"]),
            ((0.34 if dark else 0.22) * band(dd, 1.3, 0.6, 1.4) * c * G.side_w(t), T["edge"]),
            (top[0] * band(dd, 3.4, 1.5, top[1], top[2], top[3]) * c * G.top_w(t), WHITE),
            # the polished rim's own light over the whole rim (POUR_RIM), so the top edge stays closed
            ((0.7 if dark else 0.9) * band(dd, 1.0, 0.9, 1.0) * c * (1.0 - 0.55 * smooth(0.0, 0.8, t)), T["rim"]),
            ((0.26 if dark else 0.5) * band(dd, 2.6, 1.0, 2.4) * c * G.low_w(t) *
             clamp(1.0 - 0.35 * abs(2.0 * u - 1.0) ** 3), WHITE),
            ((0.24 if dark else 0.26) * band(dq, 0.0, 0.5, 1.9, 0.15, 4.0) * (1.0 - cp) * p_top, T["shade"]),
            ((0.22 if dark else 0.5) * band(dq, 1.3, 0.7, 1.3) * (1.0 - cp) * p_low, WHITE),
            (0.16 * band(inward(q), 0.0, 0.5, 1.6) * cp * p_top, BLACK)))

    rim_c = G.mixed(T["lit"], WHITE, 0.5)

    def lit_rim(x, y, n):
        d = tile(x, y)
        c = cov(d, n)
        return over(((0.55 * math.exp(-outward(d) / 3.2) * (1.0 - c), T["lit"]),
                     (0.7 * band(inward(d), 1.0, 0.9, 2.6) * c, rim_c)))

    liquid = G.liquid(wall=(0.5, 3.0, 0.3, 7.0, 0.42 if dark else 0.55), foot=0.6 if dark else 0.35)
    return dict(halo=halo, foot=foot, back=back, liquid=liquid, lit=lit, front=front, lit_rim=lit_rim)


def sprites(delivered, index):
    """The scheme's atlas in Frost (index: the scheme, which A.L is drawn in): the application's sprites as
    delivered, the installing screen's after them."""
    return delivered + pour_sprites()


def sheet(names):
    """The scheme's sheet: the application's Shard one, the carousel panes' rims drawn in the scheme's tones
    (pane_rim, F8)."""
    T = tokens()
    made = {s.name: s for s in A.sheet_sprites("shard")}
    for name, n, rim in A.PANE_CLASSES:
        made[name + "_RIM"] = pane_rim(T, name + "_RIM", n, rim)
    return [made[name] for name in names]
