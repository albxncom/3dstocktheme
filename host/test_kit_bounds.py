"""The boundary between the UI kit (uikit/) and the application, in the application's words: no kit file names the
application's probe build's flag, nor does a file of the kit's example (uikit/example/, S5-1), and no file of the
folder names the installing screen's cut (app/ui/pour_layout.h). What holds for any user of the kit (its includes,
each source compiling alone, no path outside it) is the kit's own test, uikit/host/test_kit_bounds.py.

Run: python3 -I host/test_kit_bounds.py      (make test runs it)
"""
import re
import unittest
from pathlib import Path

TREE = Path(__file__).resolve().parent.parent
KIT = TREE / "uikit"
PROBE_WORD = re.compile(r"\bAPP_PROBE\b")
POUR_CUT_WORD = re.compile(rb"pour_cut|POUR_CUT")


EXAMPLE = KIT / "example"
HEADERS = KIT / "include"
SOURCE = KIT / "source"
GENERATOR = KIT / "art"


def kit_files(folder=KIT):
    return sorted(p for p in folder.iterdir() if p.is_file() and p.name != ".DS_Store")


def held_files():
    """The kit's files (its top level, include/, source/, art/) and its example's."""
    return [p for folder in (KIT, HEADERS, SOURCE, GENERATOR, EXAMPLE) for p in kit_files(folder=folder)]


class KitBounds(unittest.TestCase):
    def test_no_probe_flag(self):
        for p in held_files():
            name = p.relative_to(KIT).as_posix()
            with self.subTest(file=name):
                self.assertIsNone(PROBE_WORD.search(p.read_text()), f"uikit/{name} names the probe flag")

    def test_no_pour_cut(self):
        for p in sorted(q for q in KIT.rglob("*") if q.is_file()):
            name = p.relative_to(KIT).as_posix()
            with self.subTest(file=name):
                self.assertIsNone(POUR_CUT_WORD.search(p.read_bytes()),
                                  f"uikit/{name} names the installing screen's cut (the application's)")


if __name__ == "__main__":
    unittest.main()
