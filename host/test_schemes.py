"""The colour schemes' table (uikit/art/schemes.py) as art.py reads it: eight schemes, each with a loader tone of its
own that reads on its bottom screen, a pour tone (the installing screen's balls, drops and liquid, R9) that reads on the
top screen's card and the glass box, and a pressed tone of its own (Weiss and Nacht no longer fall back to the yellow
original's held look).

Run: python3 -I host/test_schemes.py      (make test runs it)
"""
import re
import sys
import unittest
from pathlib import Path

UI = Path(__file__).resolve().parent.parent / "app" / "ui"
KIT = UI.parent.parent / "uikit"
HEADERS = KIT / "include"
sys.dont_write_bytecode = True
sys.path.insert(0, str(KIT / "art"))
import schemes  # noqa: E402

NAMES = ("WEISS", "GELB", "SAND", "MINZE", "HIMMEL", "FLIEDER", "ROSE", "NACHT")


def read_layout(path, values):
    """The numbers #defined in path and in the files it #includes from its own folder or the kit's include/ (as art.py
    read_layout)."""
    for line in path.read_text(encoding="utf-8").splitlines():
        m = re.match(r"#define\s+([A-Z][A-Z0-9_]*)\s+(0x[0-9A-Fa-f]+|\d+)\b", line)
        if m:
            values[m.group(1)] = int(m.group(2), 0)
        inc = re.match(r'#include\s+"([^"]+)"', line)
        found = [d / inc.group(1) for d in (path.parent, HEADERS) if (d / inc.group(1)).is_file()] if inc else []
        if found:
            read_layout(found[0], values)
    return values


LAYOUT = read_layout(UI / "layout.h", {})
COLOURS = {n: v for n, v in LAYOUT.items() if n.startswith("COL_")}
PALETTES = {s["name"]: schemes.palette(i, COLOURS, LAYOUT["TRAY_FLOOR"]) for i, s in enumerate(schemes.SCHEMES)}
HELD = sorted(n for n in COLOURS if schemes.kind_of(n) == "held") + ["COL_PRESS_INK"]


def contrast(a, b):
    return schemes.contrast(schemes.rgb(a), schemes.rgb(b))


def hue_apart(a, b):
    d = abs(schemes.oklch(schemes.rgb(a))[2] - schemes.oklch(schemes.rgb(b))[2]) % 360.0
    return min(d, 360.0 - d)


class Schemes(unittest.TestCase):
    def test_eight_in_order(self):
        self.assertEqual(tuple(s["name"] for s in schemes.SCHEMES), NAMES)

    def test_loader_tone_per_scheme(self):
        tones = []
        for s in schemes.SCHEMES:
            with self.subTest(s["name"]):
                p = PALETTES[s["name"]]
                self.assertIsInstance(s.get("loader"), int)
                self.assertTrue(0 <= s["loader"] <= 0xFFFFFF)
                self.assertEqual(p["COL_LOADER"], s["loader"])
                self.assertNotEqual(p["COL_LOADER"], p["COL_TEAL"], "the loader is the scheme's, not the accent")
                for ground in ("COL_BOT_BG_A", "COL_BOT_BG_B"):
                    self.assertGreaterEqual(contrast(p["COL_LOADER"], p[ground]), schemes.LOADER_MIN, ground)
                tones.append(s["loader"])
        self.assertEqual(len(set(tones)), len(NAMES))

    def test_pour_tone_per_scheme(self):
        for s in schemes.SCHEMES:
            with self.subTest(s["name"]):
                p = PALETTES[s["name"]]
                self.assertIsInstance(s.get("pour"), int)
                self.assertEqual(p["COL_POUR"], s["pour"])
                card = schemes.rgb(p["COL_CARD"])
                self.assertGreaterEqual(contrast(p["COL_POUR"], p["COL_CARD"]), schemes.POUR_MIN, "the card")
                self.assertGreaterEqual(schemes.contrast(schemes.rgb(p["COL_POUR"]), schemes.glass(card)),
                                        schemes.POUR_MIN, "the glass")
                self.assertGreaterEqual(schemes.POUR_MIN, 3.0)
                self.assertLessEqual(hue_apart(p["COL_POUR"], p["COL_LOADER"]), 2.0, "the loader's hue")
                self.assertLessEqual(schemes.luminance(schemes.rgb(p["COL_POUR"])),
                                     schemes.luminance(schemes.rgb(p["COL_LOADER"])) + 1e-9, "darkened, not lightened")

    def test_pressed_tone_per_scheme(self):
        for name in NAMES:
            with self.subTest(name):
                p = PALETTES[name]
                self.assertTrue(set(HELD) <= set(p))
                self.assertGreater(contrast(p["COL_PRESS_TOP"], p["COL_FACE_TOP"]), 1.1, "pressed differs from rest")
                self.assertGreater(contrast(p["COL_PRESS_INK"], p["COL_PRESS_TOP"]), 2.5, "the held ink reads")

    def test_weiss_and_nacht_not_yellow(self):
        yellow = PALETTES["GELB"]
        for name in ("WEISS", "NACHT"):
            p = PALETTES[name]
            for n in HELD:
                with self.subTest(name=name, colour=n):
                    self.assertNotEqual(p[n], yellow[n])
                    _, chroma, _ = schemes.oklch(schemes.rgb(p[n]))
                    self.assertTrue(chroma < 0.02 or hue_apart(p[n], yellow["COL_PRESS_TOP"]) > 60.0,
                                    "%s %06X has the yellow's hue" % (n, p[n]))

    def test_pressed_hue_is_the_schemes(self):
        for s in schemes.SCHEMES:
            if "ground" not in s:
                continue
            with self.subTest(s["name"]):
                d = abs(s["held"][0] - s["ground"][0]) % 360.0
                self.assertLessEqual(min(d, 360.0 - d), schemes.HUE_SPAN)


if __name__ == "__main__":
    unittest.main()
