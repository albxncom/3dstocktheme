"""Nachtglas (uikit/art/look_night.py) on its eight grounds: "Farbe" turns the ground and every tone of the look's own
with it, the tones that say what something does stay, its text keeps 4.5 to 1, and the ground's lights are the look's
own colours.

python3 -I host/test_look_night.py"""
import functools
import re
import sys
import unittest
from pathlib import Path

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "uikit" / "art"))
import look_night as N  # noqa: E402
import schemes  # noqa: E402

GROUNDS = range(len(N.GROUNDS))
# the look's own tones as drawn for the first ground: glass, its light and its text
OWN = (N.SMOKE, N.ICE, N.GLOW["BLUE"], N.GLOW["VIOLET"], (54.0, 60.0, 112.0), (22.0, 23.0, 54.0), (70.0, 86.0, 150.0))
MEANINGS = ("COL_TEAL", "COL_ORANGE", "COL_ALERT", "COL_FOLDER", "COL_GREEN", "COL_PRESS_INK")
TEXT = ("COL_INK", "COL_MUTED", "COL_TILE_INK", "COL_DLG_INK", "COL_TEAL", "COL_ORANGE", "COL_ALERT")


def rgb(v):
    return ((v >> 16) & 255, (v >> 8) & 255, v & 255)


KIT = Path(N.__file__).parent.parent
HEADERS = KIT / "include"
SOURCE = KIT / "source"
UI = KIT.parent / "app" / "ui"
COLOURS = {n: int(v, 16) for path in (HEADERS / "ui_layout.h", HEADERS / "popup_band.h") for n, v in re.findall(
    r"^#define\s+(COL_[A-Z0-9_]+)\s+0x([0-9A-Fa-f]{6})\b", path.read_text(), re.M)}


def number(name, header=UI / "layout.h"):
    return int(re.search(r"^#define\s+%s\s+(\d+)" % name, header.read_text(), re.M).group(1))


@functools.lru_cache(maxsize=None)
def palette(index):
    """The look's COL_* on ground index, as its layout hook gives them to art.py."""
    return N.palette(schemes, index, COLOURS, number("TRAY_FLOOR", HEADERS / "ui_layout.h"))


class Nachtglas(unittest.TestCase):
    def test_eight_grounds(self):
        self.assertEqual(len(N.GROUNDS), len(schemes.SCHEMES))
        self.assertEqual([g["name"] for g in N.GROUNDS], [s["name"] for s in schemes.SCHEMES])
        self.assertEqual(len(set(N.grounds(schemes))), len(schemes.SCHEMES))

    def test_ground_in_the_code_colours(self):
        """look_night.c draws the top screen from COL_TOP_BG_A to _B and the bottom one from COL_BOT_BG_A to _B: the
        two screens meet in one colour."""
        for index, row in zip(GROUNDS, N.grounds(schemes)):
            p = palette(index)
            self.assertEqual((p["COL_TOP_BG_A"], p["COL_TOP_BG_B"], p["COL_BOT_BG_B"]), row[:3], index)
            self.assertEqual(p["COL_BOT_BG_A"], p["COL_TOP_BG_B"], index)

    def test_lights_are_own_colours(self):
        self.assertEqual(N.OWN_COLOURS, ("COL_NG_LAMP_A", "COL_NG_LAMP_B", "COL_NG_LAMP_C"))
        self.assertEqual(len({row[3:] for row in N.grounds(schemes)}), len(schemes.SCHEMES))

    def test_first_ground_keeps_the_look_tones(self):
        self.assertEqual(N.turning(0), (0.0, 1.0))
        for c in OWN:
            self.assertEqual(N.turned(schemes, c, 0), c)
        self.assertEqual(palette(0)["COL_MUTED"], N.MUTED)

    def test_tones_keep_their_luminance(self):
        for index in GROUNDS:
            for c in OWN:
                y, turned = schemes.luminance(c), schemes.luminance(N.turned(schemes, c, index))
                self.assertLess(abs(turned - y), max(0.002, 0.04 * y), (index, c))

    def test_tones_take_the_ground_hue(self):
        """The look's light (the selection, the loader, the scroll bar) stands where the ground's foot stands."""
        _, _, first = schemes.oklch(N.GLOW["BLUE"])
        for index, g in zip(GROUNDS, N.GROUNDS):
            _, chroma, hue = schemes.oklch(N.turned(schemes, N.GLOW["BLUE"], index))
            want = (first + g["hue"][1] - N.GROUNDS[0]["hue"][1]) % 360.0
            self.assertLess(abs((hue - want + 180.0) % 360.0 - 180.0), 4.0, g["name"])
            self.assertGreater(chroma, 0.02, g["name"])

    def test_meanings_stay(self):
        first = palette(0)
        for index in GROUNDS:
            for name in MEANINGS:
                self.assertEqual(palette(index)[name], first[name], (index, name))

    def test_text_turns_with_the_ground(self):
        muted = {palette(index)["COL_MUTED"] for index in GROUNDS}
        self.assertEqual(len(muted), len(schemes.SCHEMES))

    def test_text_contrast(self):
        """Every ink that stands on the bare ground, 4.5 to 1 on the ground's three colours, also under one or two of
        its lights at their strongest (look_night.h NG_LAMP_PCT)."""
        pct = number("NG_LAMP_PCT", HEADERS / "look_night.h") / 100.0
        for index, row in zip(GROUNDS, N.grounds(schemes)):
            cols = [rgb(v) for v in row]
            inks = palette(index)
            for ground in cols[:3]:
                for lamps in ((), (3,), (4,), (5,), (3, 4), (3, 5), (4, 5)):
                    under = ground
                    for k in lamps:
                        under = tuple(u + (c - u) * pct for u, c in zip(under, cols[k]))
                    for name in TEXT:
                        self.assertGreaterEqual(schemes.contrast(rgb(inks[name]), under), 4.5, (index, name, lamps))

    def test_ground_still_in_the_probe_build(self):
        """The ground's lights read no clock but ui_f.moving_ms, which ui.c holds at 0 in the probe build, so the
        probe's still frames see them stand (the pattern over them runs on the same clock in ui.c)."""
        look = (SOURCE / "look_night.c").read_text()
        body = re.search(r"^static void ground\(float y0, float y1, float alpha\)\n\{\n(.*?)^\}\n", look,
                         re.S | re.M).group(1)
        self.assertIn("ui_f.moving_ms", body)
        self.assertNotRegex(body, r"ui_f\.ms\b|osGetTime|svcGetSystemTick")
        ui = (SOURCE / "ui.c").read_text()
        probe = (HEADERS / "widget.h").read_text()
        self.assertRegex(probe, r"#ifdef UI_PROBE\n(?:.*\n){0,6}#define MOVING_MS\(ms\) \(\(ms\) \* 0\)\n#else\n")
        self.assertIn("ui_f.moving_ms = MOVING_MS(ui_f.ms);", ui)

    def test_installing_screen_draws_stocks_names(self):
        """The installing screen (pour_ui.h, shared) draws the glass tile's sprites: Nachtglas makes them with the
        generator Stock uses (pour_art.py), which makes every one the drawing names, so none falls back to an id of
        art.py's. Nachtglas replaces one of them by its own drawing under the generator's name: the finished step's
        mark, its check in the smoke (G4-A)."""
        def own(path):
            source = path.read_text()
            body = source[source.index("\ndef pour_sprites():"):]
            return body[:body.index("\n\n\n")] if "\n\n\n" in body else body
        for look in ("app_look_night.py", "app_look_stock.py"):
            self.assertIn("pour_art.pour_sprites(", own(UI / look))
        self.assertIn("return pour_art.pour_sprites(", own(UI / "app_look_stock.py"))
        self.assertNotRegex(own(UI / "app_look_stock.py"), r'"POUR_[A-Z_]+"')
        self.assertEqual(set(re.findall(r'"(POUR_[A-Z_]+)"', own(UI / "app_look_night.py"))), {"POUR_TICK"})
        made = set(re.findall(r'"(POUR_[A-Z_]+)"', (UI / "pour_art.py").read_text()))
        drawn = set(re.findall(r"\bART_(POUR_[A-Z_]+)", (UI / "pour_ui.h").read_text()))
        self.assertEqual(made, drawn)
        self.assertIn("POUR_PIECE_TURN", made)


if __name__ == "__main__":
    unittest.main()
