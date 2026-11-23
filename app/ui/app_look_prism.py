"""Prisma's drawing of 3DStockTheme's images (the application's half of art.py --look prism; app_art.py is the
hook): the placeholder name's box, the carousel's panes cut as the icons and the installing screen's in Prisma's
material; with Prisma's own helpers (look_prism.py) and the generator's (A: app_images.py)."""
import math

import app_images as A
import schemes
from look_prism import BLACK, Cut, S2, mix, named, pour_tone, tones


def pane_hole(n, rim, pad):
    """A carousel pane's outline and the picture's inside it (art.py's pane_hole()): the picture's corners cut as
    the icons' mask, enlarged; the rim as wide all round."""
    L = A.L
    p = n - 2 * rim
    leg = p * L.PRISM_ICON_CUT / float(L.DETAIL_ICON) + rim * (2.0 - S2)
    box = Cut(pad, pad, pad + n, pad + n, (leg,) * 4)
    return box, box.inset(rim)


def pour_sprites(t):
    """The installing screen's images (pour_art.py: the generator every look draws its tile with) in Prisma's tones
    and its own material (pour_material): the liquid and the pieces in pour_tone, the line, its knot and the rings in
    the stone's edge tone (as a stone's cut edge)."""
    import pour_art
    L = A.L
    dark = schemes.luminance(A.rgb(L.COL_CARD)) < 0.2
    pour_art.A = A
    tone = pour_art.liquid_tone(pour_tone(t))
    return pour_art.pour_sprites(tone=tone, dark=dark, teal=A.rgb(L.COL_TEAL),
                                 line=t["edge_light"] if dark else t["edge_dark"], ink=A.rgb(L.COL_INK),
                                 material=pour_material(pour_art, t, tone, dark))


POUR_BEVEL = 4.5                # the bevel of facets round the tile's face, px (the canvas's)


POUR_RIDGE = 0.3                # half the width of the ridge where two facets meet, px


# the reflection across the table: its two bands, from and to (px along x + y from the tile's upper left corner), and
# how strong each is
POUR_SHINE = ((59.8, 78.0, 0.9), (82.2, 86.3, 0.55))


# the bevel's eight facets from the top clockwise (odd ones the cuts): the light's share (of white on the light
# schemes, of the plate's light on the dark crystal), a negative one the shade's (of the edge tone, of black)
POUR_FACETS = dict(light=(0.62, 0.30, -0.14, -0.30, -0.22, -0.06, 0.40, 0.85),
                   dark=(0.34, 0.15, -0.26, -0.42, -0.34, -0.12, 0.20, 0.52))


POUR_LIT_FACETS = (7, 0, 6, 1)  # the facets towards the light: their hairlines are light


def cut_box(cx, cy, w, h, leg):
    """A box w x h centred on cx, cy with its corners cut off at 45 degrees, leg px along each edge: at(x, y) gives
    how far the point is outside it (negative: inside; the greatest of its edges' distances), the edge it is nearest
    to (0 the top, clockwise to 7; odd ones the cuts) and how much nearer that edge is than the next one."""
    hw, hh, k = w / 2.0, h / 2.0, 1.0 / S2

    def at(x, y):
        dx, dy = x - cx, y - cy
        qx, qy = abs(dx) - hw, abs(dy) - hh
        qc = (qx + qy + leg) * k
        if qc >= qx and qc >= qy:
            return qc, (1 if dx > 0.0 else 7) if dy < 0.0 else (3 if dx > 0.0 else 5), qc - max(qx, qy)
        if qy > qx:
            return qy, 0 if dy < 0.0 else 4, qy - max(qx, qc)
        return qx, 2 if dx > 0.0 else 6, qx - max(qy, qc)
    return at


def pour_material(P, t, tone, dark):
    """The tile in Prisma's material (C48: the canvas's Prisma a "geschnittene Ecken", SR-T7 gen.py layers_prism):
    cut crystal, nothing graded round a corner (prism_layers). The liquid is the look's crystal tone with a narrower
    wall and a fine hard light line under its surface; its light end keeps its chroma by the shared rule
    (pour_art.LIQUID_STEPS). The piece is cut harder (each facet nearly one flat tone, more contrast
    from facet to facet, fine bright ridges, a small hard glint), the tongs a little wider in the slate of the look's
    inks with a joint in the spectrum's violet, the clamp nearly square. P is pour_art (its helpers), t the scheme's
    tones, tone its pour tone."""
    white = A.WHITE
    rgb = lambda c: tuple(float(round(v)) for v in c)
    if dark:
        tones = dict(shadow=rgb(t["shadow"]), lit=P.mixed(tone, white, 0.55), halo=P.mixed(tone, white, 0.5),
                     body=P.mixed(tone, white, 0.4))
        piece = dict(spread=1.25, grade=0.12, rim=0.7, rim_all=0.2, ridge=(0.62, 0.3), glint=(0.9, 0.5), alpha=240)
        arm, joint = rgb(t["edge_light"]), rgb(mix(t["spectrum"][2], white, 0.35))
    else:
        tones = dict(edge=rgb(t["muted"]), shadow=rgb(t["shadow"]))
        piece = dict(key=0.96, spread=1.25, grade=0.12, rim=0.85, gather=0.0, ridge=(0.7, 0.3), glint=(0.9, 0.5),
                     alpha=240)
        arm, joint = rgb(t["muted"]), rgb(t["spectrum"][2])
    return dict(tones=tones, layers=lambda G: prism_layers(P, G, t), surface=(0.55, 0.7, 0.5, 0.55), piece=piece,
                tongs=dict(stroke=1.5, arm=(rgb(mix(arm, BLACK, 0.35)), arm, rgb(mix(arm, white, 0.5))), rivet=1.5,
                           joint=(rgb(mix(joint, BLACK, 0.3)), joint, rgb(mix(joint, white, 0.6)))),
                clamp_r=1.0)


def prism_layers(P, G, t):
    """The tile's layers in cut crystal (pour_art.tile_sprites' G: its helpers; the shapes are the look's own, the
    tile's box with its corners cut POUR_PRISM_CUT px, the inner shape POUR_RIM px inside it, the picture's place
    with the corners of the look's picture frame), each a paint function (x, y, n) as Stock's: the glow, the foot's
    light and the shadow round the cut outline, a clear body, the liquid in the cut inner shape; in front a bevel of
    flat facets round the face (one per edge and per cut, each one tone: light towards the upper left, in shade low
    right), fine ridges where facets meet, the table's edge and the outline as hairlines, one reflection across the
    table at 45 degrees with hard edges, the recess round the picture as hairlines; at the end the facets light up."""
    L, white = A.L, A.WHITE
    T, dark, band, cov, inward, outward, over = G.T, G.dark, G.band, G.cov, G.inward, G.outward, G.over
    clamp, smooth, cover, down, m, tw, th = A.clamp, A.smooth, A.cover, G.down, G.m, G.tw, G.th
    cx, cy, rim = m + tw / 2.0, m + th / 2.0, L.POUR_RIM
    tile = cut_box(cx, cy, tw, th, L.POUR_PRISM_CUT)
    cav = cut_box(cx, cy, tw - 2 * rim, th - 2 * rim, L.POUR_PRISM_CUT - (2.0 - S2) * rim)
    pic = cut_box(cx, cy, L.POUR_PICTURE, L.POUR_PICTURE, L.PRISM_ICON_CUT * L.POUR_PICTURE / float(L.DETAIL_ICON))
    inner = lambda x, y: cav(x, y)[0]
    halo_blur, foot_blur, shadow_blur = G.blurred(0, 6.0), G.blurred(5, 3.5), G.blurred(3, 2.4)
    light = tuple(float(round(v)) for v in mix(t["plate_light"], white, 0.6)) if dark else white
    shade = BLACK if dark else T["edge"]
    facets = [(a, light) if a > 0.0 else (-a, shade) for a in POUR_FACETS["dark" if dark else "light"]]

    def halo(x, y, n):
        d = tile(x, y)[0]
        return over((((0.75 if dark else 0.8) * min(halo_blur(x, y), 1.0), T["halo"]),
                     (0.5 * math.exp(-outward(d) / 1.7) * (1.0 - cov(d, n)), T["halo"])))

    def foot(x, y, n):
        return over((((0.40 if dark else 0.42) * foot_blur(x, y) * (1.0 - cov(tile(x, y)[0], n)) *
                      smooth(0.45, 0.95, down(y)), T["light"] if dark else T["mid"]),))

    def back(x, y, n):
        c, u = cov(tile(x, y)[0], n), down(y)
        paints = [((0.55 if dark else 0.34) * shadow_blur(x, y) * (1.0 - c), T["shadow"])]
        if dark:
            paints.append((0.12 * c * clamp(1.0 - 0.35 * u), T["body"]))
        else:
            paints += [(0.30 * c * clamp(1.0 - 0.3 * u), white), (0.10 * c * clamp(0.4 + 0.6 * u), T["mid"])]
        return over(paints)

    def lit(x, y, n):
        return over(((cov(inner(x, y), n) * clamp(0.45 - 0.25 * down(y)), T["lit"]),))

    def front(x, y, n):
        d, f, gap = tile(x, y)
        c = cov(d, n)
        if c <= 0.0:
            return None
        q = pic(x, y)[0]
        cp, dd, dq, tp = cov(q, n), inward(d), outward(q), (y - G.p_y) / G.p_h
        p_top, p_low = 1.0 - 0.75 * smooth(0.0, 0.9, tp), smooth(0.55, 0.95, tp)
        table = cov(d + POUR_BEVEL, n)
        ring = (1.0 - table) * c
        lit_side = f in POUR_LIT_FACETS
        s = x + y - 2 * m
        shine = sum(k * min(cover((a - s) / S2), cover((s - b) / S2)) for a, b, k in POUR_SHINE)
        step, line = band(dd, POUR_BEVEL + 0.2, 0.3, 0.35) * c, band(dd, 0.4, 0.3, 0.4) * c
        return over((
            (facets[f][0] * ring, facets[f][1]),
            ((0.5 if dark else 0.8) * cov(gap / (2.0 * math.sin(math.pi / 8.0)) - POUR_RIDGE, n) * ring, light),
            ((0.10 if dark else 0.20) * shine * smooth(0.2, 1.2, dq) * (1.0 - cp) ** 2 * table * c, light),
            # the table's edge: a hairline, light where the facet beside it is lit, dark where it is in shade
            (((0.5 if dark else 0.9) if lit_side else (0.5 if dark else 0.38)) * step, light if lit_side else shade),
            # the outline, the look's cut edge: a hairline; in the dark crystal a soft light over the whole rim
            # under it on the lit side, so the top edge stays closed (test_ui, R9)
            ((0.36 if dark and lit_side else 0.0) * band(dd, 1.0, 0.9, 1.0) * c, light),
            (((0.8 if lit_side else 0.22) if dark else (0.32 if lit_side else 0.62)) * line,
             light if dark else shade),
            # the recess round the picture: hairlines too, shade above, light below; the picture's own edge
            ((0.42 if dark else 0.40) * band(dq, 0.3, 0.4, 0.9) * (1.0 - cp) * p_top, T["shade"]),
            ((0.4 if dark else 0.8) * band(dq, 0.9, 0.4, 0.5) * (1.0 - cp) * p_low, light),
            (0.2 * band(inward(q), 0.0, 0.5, 1.2) * cp * p_top, BLACK)))
    front.whole = True          # lit from the upper left: not its own mirror image

    ring_c, line_c = G.mixed(T["lit"], white, 0.4), G.mixed(T["lit"], white, 0.7)

    def lit_rim(x, y, n):
        d = tile(x, y)[0]
        c, dd = cov(d, n), inward(d)
        return over(((0.7 * math.exp(-outward(d) / 1.8) * (1.0 - c), T["lit"]),
                     (0.42 * (1.0 - cov(d + POUR_BEVEL, n)) * c, ring_c),
                     (0.9 * band(dd, 0.4, 0.3, 0.4) * c, line_c)))

    liquid = G.liquid(wall=(0.4, 1.5, 0.2, 4.0, 0.5), foot=0.7, shape=inner)
    return dict(halo=halo, foot=foot, back=back, liquid=liquid, lit=lit, front=front, lit_rim=lit_rim)


def sprites(items, index):
    """The application's sprites for scheme index (which A.L is drawn in) with this look's in the place of those that
    differ, the installing screen's (pour_sprites) after them."""
    L, t = A.L, tones(index)
    own = [named(A.dashed("EMPTY_NAME", L.EMPTY_NAME_W, A.name_box(), 2.5), "EMPTY_NAME")]
    by_name = {s.name: s for s in own}
    out = [by_name.pop(s.name, s) for s in items]
    out += pour_sprites(t)
    assert not by_name, sorted(by_name)
    return out


def sheet(names):
    """The application's part of the sheet in the scheme drawn: 0.9.3's word in Shard, and the carousel's panes
    (pane_rim, the hole cut as pane_hole) with their rims in the scheme's rim tones instead of the glass sheet's violet
    into teal, so they follow "Farbe"."""
    t = tones(A.SCHEME)
    out = A.word_lines("shard")
    for name, n, rim in A.PANE_CLASSES:
        out += [A.pane_rim(name, "shard", n, rim, tones=t["rim"]), A.pane_fill(name, n, rim)]
    out.append(A.pane_light())
    assert [s.name for s in out] == names
    return out
