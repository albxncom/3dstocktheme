"""The boundary in the image generator between the kit's half (the Python files of uikit/art/: art.py, look_*.py,
schemes.py, liquid.py) and the application's (the Python files of app/ui/: the hook app_art.py, app_images.py,
app_look_*.py, pour_art.py), which art.py reaches through its hook alone (art.py, APP):
- no kit file names a sprite of the application: the names are those the application's half drew, as the
  generated part_art.h holds them (the application's part, ids from ART_ALL on), and the generated ui_art.h, the
  kit's, holds none of them;
- in C (S3-2c), no file or header under uikit/ (and uikit/example/, S5-1) names an id of the application's (ART_<name>) or includes
  part_art.h: the application hands its part over (ui_part), the kit knows it by its type alone;
- no kit file names a function, constant or module of the application (what the hook offers, APP.x, aside), or
  imports one;
- no kit file opens a file of the application's by name (app/ui/ holds the application's headers and sources);
- no kit file reaches a path outside its own folder: what it opens lies beside it (a path it builds names no folder
  of the application's, app/ or ui/, and does not go up, no "..", no .parent past the kit's folder);
- no file of the kit's half lies under app/ui/: no file or folder there has the name of one in uikit/, and no look's
  generator (look_*.py) and no icon (*.svg) lies there;
- no kit file reads a number that only the application's headers define (layout.h, pour_layout.h);
- no kit file names the turning house's code: since S3-4a it is the application's (app_images.py, app_look_stock.py),
  its pictures the part's (APP.pictures). That the generator runs without the application's half at all is
  host/test_art_alone.py's.

Run: python3 -I host/test_art_bounds.py [build/host/art]      (make test runs it, after the host build)
"""
import ast
import itertools
import re
import symtable
import sys
import unittest
from pathlib import Path

TREE = Path(__file__).resolve().parent.parent
KIT = TREE / "uikit"
HEADERS = KIT / "include"
SOURCE = KIT / "source"
GENERATOR = KIT / "art"
UI = TREE / "app" / "ui"
ART = Path(sys.argv.pop(1)).resolve() if len(sys.argv) > 1 else TREE / "build" / "host" / "art"
KIT_PY = sorted(GENERATOR.glob("*.py"))
APP_PY = sorted(UI.glob("*.py"))
# the turning house's numbers and code, the application's since S3-4a
HOUSE_NUMBERS = {"HERO_ICON", "LOGO_SHEET", "LOGO_THICK_10", "LOGO_FRAMES", "LOGO_STEP", "LOGO_SWEEP"}
HOUSE_CODE = {"Turner", "TURN_N", "TURN_FINE", "TURN_THICK", "house_glass", "logo_sheet", "house_picture"}
DEFINE = re.compile(r"^#define\s+([A-Z][A-Z0-9_]*)\s", re.M)
OUTSIDE = {"..", "app", "ui"}       # path segments that leave uikit/ or name the application's folders


def enum(header, name):
    """The names of enum name, without ART_."""
    body = re.search(r"enum %s \{\n(.*?)\n\};" % name, header, re.S).group(1)
    return re.findall(r"^\s*ART_(\w+)", body, re.M)


def top_names(path):
    out = set()
    for node in ast.parse(path.read_text(encoding="utf-8")).body:
        if isinstance(node, (ast.FunctionDef, ast.ClassDef)):
            out.add(node.name)
        elif isinstance(node, (ast.Assign, ast.AnnAssign)):
            targets = node.targets if isinstance(node, ast.Assign) else [node.target]
            out |= {n.id for t in targets for n in ast.walk(t) if isinstance(n, ast.Name)}
    return out


def read_names(tree):
    """Every name a module reads or calls, bare or as an attribute (APP.x, the hook's, aside)."""
    names = {n.id for n in ast.walk(tree) if isinstance(n, ast.Name)}
    return names | {n.attr for n in ast.walk(tree) if isinstance(n, ast.Attribute)
                    and not (isinstance(n.value, ast.Name) and n.value.id == "APP")}


def built_paths(tree):
    """The strings a module builds paths from (Path(s), open(s), x / s, s / x), and how many .parent its longest
    chain of .parent has."""
    strings, depth = [], 0
    for n in ast.walk(tree):
        if isinstance(n, ast.Call) and getattr(n.func, "id", getattr(n.func, "attr", None)) in ("Path", "open"):
            strings += [a.value for a in n.args if isinstance(a, ast.Constant) and isinstance(a.value, str)]
        elif isinstance(n, ast.BinOp) and isinstance(n.op, ast.Div):
            strings += [a.value for a in (n.left, n.right) if isinstance(a, ast.Constant) and isinstance(a.value, str)]
        elif isinstance(n, ast.Attribute) and n.attr == "parent":
            d, v = 1, n.value
            while isinstance(v, ast.Attribute) and v.attr == "parent":
                d, v = d + 1, v.value
            depth = max(depth, d)
    return strings, depth


def global_names(path):
    """The names a module reads as its own globals in any of its scopes (its locals and arguments aside), and those
    it reads from the generator's module as A.x."""
    source = path.read_text(encoding="utf-8")
    out = set()

    def walk(table):
        for s in table.get_symbols():
            if s.is_referenced() and (table.get_type() == "module" or s.is_global()):
                out.add(s.get_name())
        for child in table.get_children():
            walk(child)
    walk(symtable.symtable(source, str(path), "exec"))
    tree = ast.parse(source)
    return out | {n.attr for n in ast.walk(tree) if isinstance(n, ast.Attribute) and isinstance(n.value, ast.Name)
                  and n.value.id == "A"}


class ArtBounds(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        header = (ART / "ui_art.h").read_text(encoding="utf-8")
        cls.kit_sprites = enum(header, "art_id") + enum(header, "art_glass_id")
        cls.sprites = enum((ART / "part_art.h").read_text(encoding="utf-8"), "part_art_id")
        kit_defs = set().union(*(top_names(p) for p in KIT_PY))
        cls.app_defs = set().union(*(top_names(p) for p in APP_PY)) - kit_defs
        cls.app_modules = {p.stem for p in APP_PY}
        cls.app_files = {p.name for p in UI.iterdir() if p.is_file()}
        kit_h = set().union(*(DEFINE.findall(h.read_text(encoding="utf-8")) for h in HEADERS.glob("*.h")))
        app_h = set().union(*(DEFINE.findall((UI / h).read_text(encoding="utf-8")) for h in ("layout.h", "pour_layout.h")))
        cls.app_numbers = app_h - kit_h

    def test_the_application_drew_sprites(self):
        self.assertIn("POUR_HALO", self.sprites)
        self.assertIn("WORD_TOP", self.sprites)
        self.assertNotIn("TILE", self.sprites)
        self.assertIn("TILE", self.kit_sprites)
        self.assertEqual(sorted(set(self.kit_sprites) & set(self.sprites)), [], "ui_art.h holds the application's")

    def test_no_id_of_the_application_in_the_kit_c(self):
        ids = {"ART_" + n for n in self.sprites}
        for p in sorted(itertools.chain(KIT.glob("source/*.c"), KIT.glob("include/*.h"), KIT.glob("example/*.c"),
                                        KIT.glob("example/*.h"))):     # the kit's example too (S5-1)
            name = p.relative_to(KIT).as_posix()
            with self.subTest(file=name):
                text = p.read_text(encoding="utf-8")
                words = set(re.findall(r"\bART_[A-Z0-9_]+\b", text))
                self.assertEqual(sorted(words & ids), [], f"uikit/{name} names the application's ids")
                self.assertIsNone(re.search(r'^\s*#\s*include\s*"part_art\.h"', text, re.M),
                                  f"uikit/{name} includes the application's part_art.h")

    def test_no_sprite_of_the_application(self):
        for p in KIT_PY:
            with self.subTest(file=p.name):
                words = set(re.findall(r"\b[A-Z][A-Z0-9_]*\b", p.read_text(encoding="utf-8")))
                self.assertEqual(sorted(words & set(self.sprites)), [], f"{p.name} names the application's sprites")

    def test_no_function_or_module_of_the_application(self):
        for p in KIT_PY:
            with self.subTest(file=p.name):
                tree = ast.parse(p.read_text(encoding="utf-8"))
                imported = {a.name.split(".")[0] for n in ast.walk(tree) if isinstance(n, ast.Import) for a in n.names}
                imported |= {n.module.split(".")[0] for n in ast.walk(tree) if isinstance(n, ast.ImportFrom)
                             and n.module}
                self.assertEqual(sorted(imported & self.app_modules), [], f"{p.name} imports the application's")
                named = global_names(p) & (self.app_defs | self.app_modules)
                self.assertEqual(sorted(named), [], f"{p.name} names the application's")

    def test_no_file_of_the_application(self):
        for p in KIT_PY:
            with self.subTest(file=p.name):
                tree = ast.parse(p.read_text(encoding="utf-8"))
                strings = {n.value for n in ast.walk(tree) if isinstance(n, ast.Constant) and isinstance(n.value, str)}
                named = {s for s in strings if s in self.app_files or s.rsplit("/", 1)[-1] in self.app_files}
                self.assertEqual(sorted(named), [], f"{p.name} opens the application's files by name")

    def test_no_path_outside_the_kit(self):
        for p in KIT_PY:
            with self.subTest(file=p.name):
                strings, depth = built_paths(ast.parse(p.read_text(encoding="utf-8")))
                outside = sorted(s for s in strings if OUTSIDE & set(s.split("/")))
                self.assertEqual(outside, [], f"{p.name} builds a path outside uikit/")
                self.assertLessEqual(depth, len(p.relative_to(KIT).parts),
                                     f"{p.name} goes up from the kit's folder (.parent past it)")

    def test_no_file_of_the_kit_under_app_ui(self):
        names = {q.name for folder in (KIT, HEADERS, SOURCE, GENERATOR) for q in folder.iterdir()}
        self.assertEqual(sorted(q.name for q in UI.iterdir() if q.name in names), [],
                         "app/ui/ holds a file or folder of the kit's")
        self.assertEqual(sorted(q.name for q in UI.glob("look_*.py")), [], "app/ui/ holds a look's generator")
        self.assertEqual(sorted(str(q.relative_to(UI)) for q in UI.rglob("*.svg")), [], "app/ui/ holds an icon")
        self.assertTrue(KIT_PY, "uikit/ holds no Python file")

    def test_no_number_of_the_application(self):
        self.assertTrue(HOUSE_NUMBERS <= self.app_numbers, "the house's numbers are not the application's")
        for p in KIT_PY:
            with self.subTest(file=p.name):
                read = read_names(ast.parse(p.read_text(encoding="utf-8")))
                self.assertEqual(sorted(read & self.app_numbers), [], f"{p.name} reads the application's numbers")

    def test_no_code_of_the_house(self):
        self.assertTrue(HOUSE_CODE <= self.app_defs, "the house's code is not the application's alone")
        for p in KIT_PY:
            with self.subTest(file=p.name):
                self.assertEqual(sorted(global_names(p) & HOUSE_CODE), [], f"{p.name} names the house's code")


if __name__ == "__main__":
    unittest.main()
