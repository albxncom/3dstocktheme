"""The kit's half of the image generator against the application's (S3-4a): the kit's generated files (ui_art.h,
ui_art.c, art_<look>.c) made with no --app (KIT_ART, the kit's own art folder of the same looks: art.py --look and
--join without the hook) are the same bytes as those of the build with the application's half (ART, the host
build's join). That the generator runs without the hook, writes no file of a part then and that the kit compiles
against it is the kit's own test, uikit/host/test_art_alone.py.

Run: python3 -I host/test_art_alone.py [build/host/art [uikit/build/art]]      (make test runs it, after the host
     build and the kit's art, for every look ART holds, Stock first)
"""
import sys
import unittest
from pathlib import Path

TREE = Path(__file__).resolve().parent.parent
ART = Path(sys.argv.pop(1)).resolve() if len(sys.argv) > 1 else TREE / "build" / "host" / "art"
KIT_ART = Path(sys.argv.pop(1)).resolve() if len(sys.argv) > 1 else TREE / "uikit" / "build" / "art"
LOOKS = sorted((p.stem for p in ART.glob("*.part")), key=lambda n: (n != "stock", n))
KIT_FILES = ["ui_art.h", "ui_art.c"] + ["art_%s.c" % n for n in LOOKS]


class ArtAlone(unittest.TestCase):
    def test_the_same_bytes_as_with_the_application(self):
        self.assertTrue(LOOKS and LOOKS[0] == "stock", "no looks in %s" % ART)
        self.assertEqual(sorted(p.stem for p in KIT_ART.glob("*.part")), sorted(LOOKS),
                         "%s and %s hold different looks" % (KIT_ART, ART))
        for name in KIT_FILES:
            with self.subTest(file=name):
                self.assertTrue((KIT_ART / name).is_file(), "%s not written without --app" % name)
                self.assertTrue((KIT_ART / name).read_bytes() == (ART / name).read_bytes(),
                                "%s differs between the generator with --app and without" % name)


if __name__ == "__main__":
    unittest.main()
