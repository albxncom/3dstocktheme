"""The badge core (build/<variant>/test_badge) against the Mac tool tools/badges/badge_sets.py on the same files.

On generated data (tools/badges/fixtures.py): the PNG reader pixel for pixel and refusal for refusal (also on
damaged files), the order of names, list and check-slots line for line, append byte for byte with the same
manifest, remove-last back to the same bytes, the refusals; a set from a zip and from one PNG against the same PNGs
in a folder; the listing for a screen (icon, removable). With BADGES_DATA (environment, else the repo's .env): the
same on the decrypted trees under <data>/work/, the console's own append of test B4 reproduced, and the badge images
of the card's set folders against the console's files. The data dir is only read; everything is written to a
temporary directory, and nothing from the data dir is printed beyond counts.

Run: python3 -I host/test_badges.py --build build/host     (make test runs it for build/host and build/asan)
"""
import argparse
import contextlib
import hashlib
import io
import json
import pathlib
import random
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import unicodedata
import unittest
import zipfile
import zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
BADGES = ROOT / "tools" / "badges"
sys.dont_write_bytecode = True     # nothing is written into tools/badges
sys.path.insert(0, str(BADGES))
import badge_sets as bs  # noqa: E402  (the reference)
import fixtures  # noqa: E402  (its generated test data)

BUILD = None
SANITIZER = ("AddressSanitizer", "runtime error:", "UndefinedBehaviorSanitizer", "LeakSanitizer")


def setting(name):
    import os
    if name in os.environ:
        return os.environ[name]
    env = ROOT / ".env"
    for line in env.read_text(encoding="utf-8").splitlines() if env.is_file() else []:
        key, sep, value = line.partition("=")
        if sep and key.strip() == name:
            return value.strip().strip("\"'")
    return None


def c_tool(*args):
    result = subprocess.run([str(BUILD / "test_badge"), *map(str, args)], capture_output=True, text=True)
    for marker in SANITIZER:
        if marker in result.stderr:
            raise AssertionError(f"sanitizer report from test_badge {args[0]}:\n{result.stderr}")
    return result.returncode, result.stdout, result.stderr


def py_tool(*args):
    out, err = io.StringIO(), io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
        code = bs.main([str(a) for a in args])
    return code, out.getvalue(), err.getvalue()


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def files_of(tree):
    return {name: sha(Path(tree) / "user" / name) for name in bs.DAT_NAMES}


# What badge_sets.py says, as the core's result code: its messages by class
CLASSES = [
    (r"bytes, a .* is 0x|not found \(", "BADGE_E_SIZE"),
    (r"live BadgeMngFile.dat header is not valid|MngFile.dat header word", "BADGE_E_SLOTS"),
    (r"header word 0x0|header counts out of range|used-badge bits|used-set bits|set record \d+ holds|set IDs are "
     r"empty|set indices out|badge record \d+ says|has no set record|repeated or unknown identity|claims \d+ badges|"
     r"counts \d+ copies|header total", "BADGE_E_FORMAT"),
    (r"is not a supported|is not supported; save it|interlaced or non-standard", "BADGE_E_PNG_KIND"),
    (r"not a PNG file|ends before its IEND|damaged PNG|does not start with IHDR|unexpected critical|without a valid "
     r"PLTE|image data has the wrong length|unknown PNG filter|palette index", "BADGE_E_PNG"),
    (r"multiples of 64|a set icon is 48x48|unsupported image size", "BADGE_E_IMAGE_SIZE"),
    (r"only PNG files|holds no badge PNG|is not a folder", "BADGE_E_FOLDER"),
    (r"empty name|longer than 68", "BADGE_E_NAME"),
    (r"manifest.json has missing", "BADGE_E_MANIFEST"),
    (r"no set ID left|no badge ID left", "BADGE_E_IDS"),
    (r"is already in the collection", "BADGE_E_PRESENT"),
    (r"HOME takes at most", "BADGE_E_FULL"),
    (r"set index \d+ is taken", "BADGE_E_INDEX"),
    (r"a badge ID from the manifest", "BADGE_E_TAKEN"),
    (r"would not keep the existing collection intact|^error: refused", "BADGE_E_REFUSED"),
    (r"the collection has no set", "BADGE_E_EMPTY"),
    (r"not appended by this tool", "BADGE_E_NOT_OURS"),
    (r"does not end at the last badge", "BADGE_E_LAST"),
    (r"put them back", "BADGE_E_PLACED"),
    (r"itself changed since the append", "BADGE_E_SET_CHANGED"),
    (r"no undo record", "BADGE_E_NO_UNDO"),
    (r"not what a recorded append wrote", "BADGE_E_CHANGED"),
    (r"undo records fit these files", "BADGE_E_AMBIGUOUS"),
    (r"is damaged", "BADGE_E_UNDO"),
]


def py_class(message):
    for pattern, code in CLASSES:
        if re.search(pattern, message):
            return code
    raise AssertionError(f"no class for badge_sets.py's message: {message}")


def c_class(stderr):
    m = re.match(r"error: (BADGE_E_\w+):", stderr)
    return m.group(1) if m else stderr.strip()


# ---------------------------------------------------------------- the manifest between JSON and the line file

def to_lines(manifest_json, path):
    m = json.loads(Path(manifest_json).read_text(encoding="utf-8")) if Path(manifest_json).is_file() else \
        {"next_set_id": bs.SET_IDS.start, "next_badge_id": bs.BADGE_IDS.start, "sets": []}
    lines = [f"next_set_id {m['next_set_id']}", f"next_badge_id {m['next_badge_id']}"]
    for s in m["sets"]:
        lines.append(f"set {s['set_id']}\t{s['folder']}\t{s['name']}")
        lines += [f"badge {b['tile']}\t{b['badge_id']}\t{b['file']}" for b in s["badges"]]
    Path(path).write_text("\n".join(lines) + "\n", encoding="utf-8")
    return path


def from_lines(path):
    m = {"next_set_id": None, "next_badge_id": None, "sets": []}
    for line in Path(path).read_text(encoding="utf-8").splitlines():
        key, _, rest = line.partition(" ")
        if key in ("next_set_id", "next_badge_id"):
            m[key] = int(rest)
        elif key == "set":
            set_id, folder, name = rest.split("\t")
            m["sets"].append({"folder": folder, "name": name, "set_id": int(set_id), "badges": []})
        elif key == "badge":
            tile, badge_id, file = rest.split("\t")
            m["sets"][-1]["badges"].append({"file": file, "tile": int(tile), "badge_id": int(badge_id)})
    return m


def comparable(m):
    return (m["next_set_id"], m["next_badge_id"],
            [(s["folder"], s["name"], s["set_id"], [(b["file"], b["tile"], b["badge_id"]) for b in s["badges"]])
             for s in m["sets"]])


def stdout_lines(text, drop=("wrote ", "undo record ")):
    """The output without the lines that name this run's own paths; a record's file name in parentheses at the end
    of remove-last's first line is cut."""
    out = []
    for line in text.splitlines():
        if line.startswith(drop):
            continue
        if line.startswith("removed set "):
            line = re.sub(r" \([^()]*\)$", "", line)
        out.append(line)
    return out


class Both:
    """One data dir per tool (manifest, undo/) with the same manifest at the start."""

    def __init__(self, root, manifest_json=None):
        self.py = Path(root) / "py"
        self.c = Path(root) / "c"
        self.py.mkdir(parents=True)
        self.c.mkdir(parents=True)
        if manifest_json and Path(manifest_json).is_file():
            shutil.copy(manifest_json, self.py / "manifest.json")
        to_lines(self.py / "manifest.json", self.c / "manifest.txt")
        self.n = 0
        self.records = []

    def append(self, tree, set_dir, name=None, c_set=None):
        """badge_sets.py append and test_badge append of the same set onto the same tree: (py, c) results and
        output trees. c_set: another source of the same PNGs for the core (a zip, one PNG)."""
        self.n += 1
        py_out, c_out = self.py / f"out-{self.n}", self.c / f"out-{self.n}"
        extra = ["--name", name] if name else []
        p = py_tool("append", "--data", self.py, "--in", tree, "--out", py_out, *extra, set_dir)
        undo = self.c / f"undo-{self.n}.bin"
        c = c_tool("append", "--in", tree, "--out", c_out, "--manifest", self.c / "manifest.txt",
                   "--manifest-out", self.c / "manifest.new", "--undo-out", undo, *extra, c_set or set_dir)
        if c[0] == 0:
            shutil.move(self.c / "manifest.new", self.c / "manifest.txt")
            self.records.append(undo)
        return p, c, py_out, c_out

    def remove(self, tree, allow=False):
        self.n += 1
        py_out, c_out = self.py / f"out-{self.n}", self.c / f"out-{self.n}"
        flag = ["--allow-home-changes"] if allow else []
        p = py_tool("remove-last", "--data", self.py, "--in", tree, "--out", py_out, *flag)
        undo = [a for r in self.records for a in ("--undo", r)]
        c = c_tool("remove-last", "--in", tree, "--out", c_out, "--manifest", self.c / "manifest.txt", *undo, *flag)
        return p, c, py_out, c_out


class Base(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp(prefix="badges-"))
        self.addCleanup(shutil.rmtree, self.tmp, ignore_errors=True)

    def same_result(self, p, c, what):
        """Both done with the same output, or both refused for the same reason."""
        if p[0] == 0 or c[0] == 0:
            self.assertEqual((p[0], c[0]), (0, 0), f"{what}: badge_sets.py {p[0]} {p[2].strip()}, core {c[0]} "
                                                    f"{c[2].strip()}")
            self.assertEqual(stdout_lines(p[1]), stdout_lines(c[1]), what)
        else:
            self.assertEqual((p[0], c[0]), (2, 2), what)
            self.assertEqual(py_class(p[2]), c_class(c[2]), f"{what}: {p[2].strip()} / {c[2].strip()}")

    def same_append(self, both, tree, set_dir, name=None, c_set=None, what="append"):
        p, c, py_out, c_out = both.append(tree, set_dir, name, c_set)
        self.same_result(p, c, what)
        if p[0] == 0:
            self.assertEqual(files_of(py_out), files_of(c_out), f"{what}: the files differ")
            self.assertEqual(comparable(json.loads((both.py / "manifest.json").read_text(encoding="utf-8"))),
                             comparable(from_lines(both.c / "manifest.txt")), f"{what}: the manifests differ")
        return p, c, py_out, c_out


# ---------------------------------------------------------------- the C unit tests

class Unit(unittest.TestCase):
    def test_unit(self):
        code, out, err = c_tool("unit")
        self.assertEqual(code, 0, out + err)
        self.assertIn(" 0 failed", out)
        print("\n" + out.strip(), file=sys.stderr)


# ---------------------------------------------------------------- the PNG reader

def palette_png(width, height, seed, alpha=True):
    rnd = random.Random(seed)
    palette = [(rnd.randrange(256), rnd.randrange(256), rnd.randrange(256), rnd.randrange(256) if alpha else 255)
               for _ in range(20)]
    rgba = b"".join(bytes(palette[rnd.randrange(20)]) for _ in range(width * height))
    return fixtures.png(width, height, rgba, color_type=3, palette=palette)


def generated_pngs():
    """(name, bytes): every colour type and filter, transparency keys, odd sizes, extra chunks"""
    out = []
    for ct in (0, 2, 4, 6):
        for w, h in ((64, 64), (13, 7), (1, 1), (128, 64)):
            out.append((f"ct{ct}-{w}x{h}", fixtures.png(w, h, fixtures.pattern(w, h, ct + w), color_type=ct)))
    out.append(("palette", palette_png(64, 64, 1)))
    out.append(("palette-opaque", palette_png(32, 16, 2, alpha=False)))
    grey = fixtures.pattern(16, 16, 5)
    out.append(("grey-key", fixtures.png(16, 16, grey, color_type=0, trns=struct.pack(">H", grey[0]))))
    out.append(("grey-key-high", fixtures.png(16, 16, grey, color_type=0, trns=bytes((1, grey[0])))))
    out.append(("rgb-key", fixtures.png(16, 16, grey, color_type=2, trns=struct.pack(">HHH", *grey[:3]))))
    out.append(("rgb-key-short", fixtures.png(16, 16, grey, color_type=2, trns=bytes(4))))
    raw = fixtures.png(32, 32, fixtures.pattern(32, 32, 9))
    idat = raw.index(b"IDAT") - 4
    length = struct.unpack(">I", raw[idat:idat + 4])[0]
    body = raw[idat + 8:idat + 8 + length]
    split = b"".join(fixtures._chunk(b"IDAT", body[i:i + 50]) for i in range(0, len(body), 50))
    out.append(("idat-split", raw[:idat] + split + raw[idat + 12 + length:]))
    out.append(("idat-trailing", raw[:idat] + fixtures._chunk(b"IDAT", body + b"junk") + raw[idat + 12 + length:]))
    out.append(("after-iend", raw + b"garbage after IEND"))
    out.append(("ancillary", raw[:33] + fixtures._chunk(b"gAMA", bytes(4)) + raw[33:]))
    return out


def refused(raw):
    """Damaged and unsupported variants of a PNG"""
    def ihdr(**fields):
        b = bytearray(raw)
        for off, value in fields.items():
            b[{"depth": 24, "ct": 25, "comp": 26, "filt": 27, "lace": 28}[off]] = value
        b[29:33] = struct.pack(">I", zlib.crc32(bytes(b[12:29])))
        return bytes(b)
    sized = bytearray(raw)
    sized[16:24] = struct.pack(">II", 0, 64)
    sized[29:33] = struct.pack(">I", zlib.crc32(bytes(sized[12:29])))
    huge = bytearray(raw)
    huge[16:24] = struct.pack(">II", 4097, 4096)
    huge[29:33] = struct.pack(">I", zlib.crc32(bytes(huge[12:29])))
    no_idat = raw[:33] + fixtures._chunk(b"IEND", b"")
    return [("gif", b"GIF89a" + raw[6:]), ("16-bit", ihdr(depth=16)), ("ct5", ihdr(ct=5)), ("laced", ihdr(lace=1)),
            ("comp", ihdr(comp=1)), ("filt", ihdr(filt=1)), ("zero", bytes(sized)), ("huge", bytes(huge)),
            ("cut", raw[:40]), ("no-idat", no_idat), ("critical", raw[:33] + fixtures._chunk(b"ABCD", b"") + raw[33:]),
            ("two-ihdr", raw[:33] + raw[8:33] + raw[33:]), ("iend-first", raw[:8] + fixtures._chunk(b"IEND", b""))]


class Png(Base):
    def compare(self, name, raw):
        path = self.tmp / "x.png"
        path.write_bytes(raw)
        try:
            expect = bs.read_png(path)
            p = None
        except bs.BadgeError as e:
            expect, p = None, py_class(f"error: {e}")
        code, _, err = c_tool("png", path, self.tmp / "out")
        if expect is None:
            self.assertEqual(code, 2, f"{name}: badge_sets.py refuses ({p}), the core reads it")
            self.assertEqual(c_class(err), p, name)
            return False
        self.assertEqual(code, 0, f"{name}: the core refuses ({err.strip()}), badge_sets.py reads it")
        got = (self.tmp / "out").read_bytes()
        head, _, pixels = got.partition(b"\n")
        self.assertEqual(tuple(map(int, head.split())), expect[:2], name)
        self.assertEqual(pixels, expect[2], f"{name}: other pixels")
        return True

    def test_generated(self):
        for name, raw in generated_pngs():
            with self.subTest(name):
                self.assertTrue(self.compare(name, raw))

    def test_refused(self):
        raw = fixtures.png(8, 8, fixtures.pattern(8, 8, 1))
        palette = palette_png(4, 4, 3)
        plte = palette.index(b"PLTE") - 4
        no_plte = palette[:plte] + palette[plte + 12 + 60:]
        short = palette[:plte] + fixtures._chunk(b"PLTE", palette[plte + 8:plte + 11]) + palette[plte + 12 + 60:]
        for name, bad in refused(raw) + [("no-plte", no_plte), ("short-plte", short)]:
            with self.subTest(name):
                self.assertFalse(self.compare(name, bad))

    def test_damage(self):
        """Random damage, with and without fixed CRCs: the same answer from both"""
        rnd = random.Random(7)
        seeds = [raw for _, raw in generated_pngs()[:6]] + [palette_png(16, 16, 4)]
        accepted = refused_count = 0
        for n in range(400):
            raw = bytearray(rnd.choice(seeds))
            for _ in range(rnd.randrange(1, 4)):
                raw[rnd.randrange(8, len(raw))] ^= 1 << rnd.randrange(8)
            if n % 2:
                pos = 8
                while pos + 12 <= len(raw):
                    length = struct.unpack(">I", raw[pos:pos + 4])[0]
                    if length > len(raw) - pos - 12:
                        break
                    raw[pos + 8 + length:pos + 12 + length] = struct.pack(">I", zlib.crc32(raw[pos + 4:pos + 8 +
                                                                                                 length]))
                    pos += 12 + length
            with self.subTest(n):
                if self.compare(f"damage {n}", bytes(raw)):
                    accepted += 1
                else:
                    refused_count += 1
        self.assertGreater(refused_count, 100)
        self.assertGreater(accepted, 10)


# ---------------------------------------------------------------- the order of names

class Names(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        code, out, err = c_tool("points")
        assert code == 0, err
        cls.ranges, cls.folds, cls.spaces = [], {}, set()
        for line in out.splitlines():
            kind, *values = line.split()
            values = [int(v, 16) for v in values]
            if kind == "range":
                cls.ranges.append((values[0], values[1]))
            elif kind == "fold":
                cls.folds[values[0]] = "".join(map(chr, values[1:]))
            else:
                cls.spaces.add(values[0])
        cls.accepted = [c for a, b in cls.ranges for c in range(a, b + 1)]

    def test_every_code_point_the_core_takes(self):
        """Each one: NFC leaves it as it is, also next to others (no combining class, never the second part of a
        canonical composition, nor does its decomposition start with one), its casefold and isspace() are the
        core's, and lower() never makes ASCII of it; every combining mark is outside"""
        seconds = set(range(0x1161, 0x1176)) | set(range(0x11A8, 0x11C3))     # Hangul V and T jamo
        for c in range(0x110000):
            d = unicodedata.decomposition(chr(c))
            if d and not d.startswith("<") and len(d.split()) == 2:
                seconds.add(int(d.split()[1], 16))
        accepted = set(self.accepted)
        problems = []
        for c in self.accepted:
            ch = chr(c)
            if (unicodedata.normalize("NFC", ch) != ch or unicodedata.combining(ch) or c in seconds
                    or ord(unicodedata.normalize("NFD", ch)[0]) in seconds):
                problems.append(f"{c:x} normalisation")
            if ch.casefold() != self.folds.get(c, ch):
                problems.append(f"{c:x} casefold")
            if ch.isspace() != (c in self.spaces):
                problems.append(f"{c:x} isspace")
            if c > 0x7F and any(ord(x) < 0x80 for x in ch.lower()):
                problems.append(f"{c:x} lower")
        self.assertEqual(problems[:10], [])
        self.assertFalse([c for c in range(0x110000) if unicodedata.combining(chr(c)) and c in accepted])
        for c in (0x0301, 0x3099, 0x309A, 0x302A, 0xFE20, 0x1161, 0xF900, 0x0414, 0x0130, 0x212A):
            self.assertNotIn(c, accepted, hex(c))
        rnd = random.Random(5)
        for _ in range(3000):
            s = "".join(chr(rnd.choice(self.accepted)) for _ in range(rnd.randrange(1, 8)))
            self.assertEqual(unicodedata.normalize("NFC", s), s, ascii(s))
        print(f"\n  names: {len(self.accepted)} code points in {len(self.ranges)} ranges checked", file=sys.stderr)

    def test_order_like_python(self):
        rnd = random.Random(3)
        alphabet = ("aAbBsSzZ _-.0129" + "".join(chr(c) for c in range(0xA0, 0x100)) + "\xb5\xdf\x7f\x1f" +
                    "\u304b\u304c\u30ab\u30ac\u30fc\u6f22\u5b57\uac00\uff21\uff41\uff22\uff42\u3000\u2605" +
                    "\U0001f600\U0001f3ae\U00020000")
        for _ in range(30):
            names = list({"".join(rnd.choice(alphabet) for _ in range(rnd.randrange(1, 6))) for _ in range(40)})
            expect = sorted(names, key=lambda n: (bs.nfc(n).casefold(), bs.nfc(n)))
            code, out, err = c_tool("order", *names)
            self.assertEqual(code, 0, err)
            self.assertEqual(out.split("\n")[:-1], expect)

    def test_what_needs_normalising_is_refused(self):
        # badge_sets.py takes these (NFC composes or changes them); the core refuses rather than guess
        for name in ("Caf\u0065\u0301", "\u30cf\u3099", "\uf900", "\u0414", "\u0130", "\u212b"):
            self.assertNotEqual(unicodedata.normalize("NFC", name).casefold(), name, ascii(name))
            code, _, err = c_tool("order", "a", name)
            self.assertEqual((code, c_class(err)), (2, "BADGE_E_NAME"), ascii(name))


# ---------------------------------------------------------------- the commands on generated collections

def sheet_set(folder, sizes, icon=False, prefix="Sheet"):
    folder = Path(folder)
    folder.mkdir(parents=True)
    for n, (w, h) in enumerate(sizes):
        (folder / f"{prefix} {n + 1:02d}.png").write_bytes(fixtures.png(w, h, fixtures.pattern(w, h, n + 3)))
    if icon:
        (folder / "_seticon.png").write_bytes(palette_png(48, 48, 5))
    return folder


class Commands(Base):
    def setUp(self):
        super().setUp()
        self.data, self.mng = fixtures.collection()
        self.tree = fixtures.tree(self.tmp / "work" / "000014d1", self.data, self.mng)
        self.both = Both(self.tmp / "dirs")

    def same_list(self, tree, what="list"):
        p = py_tool("list", "--data", self.both.py, "--in", tree)
        c = c_tool("list", "--in", tree, "--manifest", self.both.c / "manifest.txt")
        self.same_result(p, c, what)
        self.assertEqual(p[1], c[1], what)

    def test_list(self):
        self.same_list(self.tree)
        mng = bytearray(self.mng)
        struct.pack_into("<I", mng, bs.PLACED, 0)           # a warning
        fixtures.place(mng, 5, 7, 40)                       # another placement
        struct.pack_into("<I", mng, bs.LAYOUT + 6 * bs.LAYOUT_RECORD + 4, 0x999)   # one that names no badge
        self.same_list(fixtures.tree(self.tmp / "warn", self.data, bytes(mng)), "warnings")
        flagged = fixtures.tree(self.tmp / "flagged", *fixtures.collection(set_bits=True, seed=2))
        self.same_list(flagged, "used-set bits")

    def test_append_remove_like_python(self):
        sets = [fixtures.badge_set(self.tmp / "sets" / "New Set", 2),
                fixtures.badge_set(self.tmp / "sets" / "With Icon", 3, icon=True),
                sheet_set(self.tmp / "sets" / "Sheets", [(128, 64), (64, 192), (192, 128)]),
                sheet_set(self.tmp / "sets" / "Grüße été", [(64, 64)], icon=True,
                          prefix="Äpfel")]
        tree = self.tree
        for k, set_dir in enumerate(sets):
            with self.subTest(set_dir.name):
                name = "Renamed ä" if k == 1 else None
                p, c, py_out, c_out = self.same_append(self.both, tree, set_dir, name, what=set_dir.name)
                self.same_list(c_out, f"list after {set_dir.name}")
                tree = c_out
        # remove them again, one by one, back to the exact bytes of each step
        steps = [self.tree] + [self.both.c / f"out-{n}" for n in range(1, len(sets) + 1)]
        for k in range(len(sets), 0, -1):
            with self.subTest(f"remove {k}"):
                p, c, py_out, c_out = self.both.remove(steps[k])
                self.same_result(p, c, f"remove {k}")
                self.assertEqual(files_of(c_out), files_of(steps[k - 1]))
                self.assertEqual(files_of(py_out), files_of(steps[k - 1]))

    def test_names_beyond_latin1(self):
        """Kana, kanji, Hangul, fullwidth letters, emoji (surrogate pairs) in file and set names; the length limit
        in UTF-16 units; a decomposed name, which only badge_sets.py takes"""
        folder = self.tmp / "sets" / "\u30d0\u30c3\u30b8"
        folder.mkdir(parents=True)
        for n, (name, size) in enumerate((("\u30d0\u30c3\u30b8 01", (64, 64)), ("\u6f22\u5b57", (128, 64)),
                                         ("\U0001f600 Smile", (64, 64)), ("\uff26\uff55\uff4c\uff4c", (64, 64)),
                                         ("\uff46\uff55\uff4c\uff4c 2", (64, 64)), ("\uac00\ub098", (64, 64)),
                                         ("\u304b\u306a\u30fc", (64, 128)), ("\U00020000", (64, 64)))):
            (folder / f"{name}.png").write_bytes(fixtures.png(*size, fixtures.pattern(*size, n + 20)))
        p, _, _, out = self.same_append(self.both, self.tree, folder, what="kana and kanji")
        self.assertEqual(p[0], 0, p[2])
        self.assertIn("\u30d0\u30c3\u30b8", p[1])
        self.same_list(out, "list with a Japanese set")
        p, _, _, _ = self.same_append(self.both, out, fixtures.badge_set(self.tmp / "sets" / "Second", 1),
                                      "\u65e5\u672c\u8a9e\u306e\u540d\u524d \U0001f3ae", what="a Japanese set name")
        self.assertEqual(p[0], 0, p[2])
        p, c, _, back = self.both.remove(out)
        self.same_result(p, c, "remove the Japanese set")
        self.assertEqual((p[0], files_of(back)), (0, files_of(self.tree)))
        limit = self.tmp / "sets" / "Limit"
        limit.mkdir()
        (limit / ("\u6f22" * 68 + ".png")).write_bytes(fixtures.png(64, 64, fixtures.pattern(64, 64, 1)))
        p, _, _, _ = self.same_append(self.both, self.tree, limit, what="68 units")
        self.assertEqual(p[0], 0, p[2])
        over = self.tmp / "sets" / "Over"
        over.mkdir()
        (over / ("\u6f22" * 67 + "\U0001f600.png")).write_bytes(fixtures.png(64, 64, fixtures.pattern(64, 64, 2)))
        p, c, _, _ = self.same_append(self.both, self.tree, over, what="67 units and a surrogate pair")
        self.assertEqual((p[0], c_class(c[2])), (2, "BADGE_E_NAME"))
        decomposed = self.tmp / "sets" / "Decomposed"
        decomposed.mkdir()
        nfd = "\u30cf\u3099\u30c3\u30b7\u3099.png"
        (decomposed / nfd).write_bytes(fixtures.png(64, 64, fixtures.pattern(64, 64, 3)))
        p, c, _, _ = self.both.append(self.tree, decomposed)
        self.assertEqual((p[0], c[0], c_class(c[2])), (0, 2, "BADGE_E_NAME"), "only badge_sets.py composes")

    def test_ids_stay(self):
        set_dir = fixtures.badge_set(self.tmp / "sets" / "Stable", 2)
        _, _, _, out1 = self.same_append(self.both, self.tree, set_dir, what="first")
        p, c, _, back = self.both.remove(out1)
        self.same_result(p, c, "remove")
        (set_dir / "Badge 01.png").unlink()                 # one gone, two new: the old IDs stay
        (set_dir / "Badge 00.png").write_bytes(fixtures.png(64, 64, fixtures.pattern(64, 64, 40)))
        (set_dir / "Badge 03.png").write_bytes(fixtures.png(128, 64, fixtures.pattern(128, 64, 41)))
        self.same_append(self.both, back, set_dir, what="again")
        other = fixtures.badge_set(self.tmp / "sets" / "Other", 1)
        self.same_append(self.both, back, other, what="other set")

    def test_remove_after_home_changes(self):
        set_dir = fixtures.badge_set(self.tmp / "sets" / "New Set", 2)
        _, _, _, out = self.same_append(self.both, self.tree, set_dir)
        data, mng = bs.read_tree(out)
        mng = bytearray(mng)
        fixtures.unplace(mng, 0)                            # HOME moved old badges and the case's view
        fixtures.place(mng, 9, 4, 33, bs.FOLDER_ICON)
        mng[bs.SELECTED_SET:bs.SELECTED_SET + 4] = struct.pack("<I", 0x7000)
        home = fixtures.tree(self.tmp / "home", data, bytes(mng))
        p, c, _, _ = self.both.remove(home)
        self.same_result(p, c, "changed by HOME, without the flag")
        p, c, py_out, c_out = self.both.remove(home, allow=True)
        self.same_result(p, c, "changed by HOME, with the flag")
        self.assertEqual(files_of(py_out), files_of(c_out))
        own = bytearray(data)
        own[bs.BADGE_ICONS + 12 * bs.BADGE_ICON_SIZE] ^= 1  # the set's own image changed
        p, c, _, _ = self.both.remove(fixtures.tree(self.tmp / "own", bytes(own), bytes(mng)), allow=True)
        self.same_result(p, c, "the set itself changed")
        placed = bytearray(mng)
        fixtures.place(placed, 20, 12, 41)                  # a badge of the new set placed
        p, c, _, _ = self.both.remove(fixtures.tree(self.tmp / "placed", data, bytes(placed)), allow=True)
        self.same_result(p, c, "placed")

    def test_refusals(self):
        good = fixtures.badge_set(self.tmp / "sets" / "Good", 2)
        cases = []
        full = fixtures.tree(self.tmp / "full", *fixtures.collection(sets=[("Big", 999)], placed=()))
        cases.append(("1000 badges", full, good))
        many = fixtures.tree(self.tmp / "many", *fixtures.collection(sets=[(f"S{n}", 1) for n in range(100)],
                                                                     placed=()))
        cases.append(("100 sets", many, good))
        short = fixtures.tree(self.tmp / "short", self.data[:-1], self.mng)
        cases.append(("wrong size", short, good))
        header = bytearray(self.mng)
        struct.pack_into("<I", header, 0, 1)
        cases.append(("header word", fixtures.tree(self.tmp / "header", self.data, bytes(header)), good))
        gaps = bytearray(self.mng)
        gaps[bs.USED_BADGES] &= ~0x04
        cases.append(("used bits", fixtures.tree(self.tmp / "gaps", self.data, bytes(gaps)), good))
        index = bytearray(self.mng)
        struct.pack_into("<I", index, bs.SET_INFO + 0x14, 3)    # set 0 at index 3, the next set's index
        cases.append(("index taken", fixtures.tree(self.tmp / "index", self.data, bytes(index)), good))
        bad_png = fixtures.badge_set(self.tmp / "sets" / "BadPng", 2)
        raw = bytearray((bad_png / "Badge 02.png").read_bytes())
        raw[-20] ^= 0xFF
        (bad_png / "Badge 02.png").write_bytes(bytes(raw))
        cases.append(("bad PNG", self.tree, bad_png))
        odd = self.tmp / "sets" / "Odd"
        odd.mkdir()
        (odd / "a.png").write_bytes(fixtures.png(64, 65, fixtures.pattern(64, 65, 1)))
        cases.append(("64x65", self.tree, odd))
        icon = fixtures.badge_set(self.tmp / "sets" / "Icon", 1)
        (icon / "_seticon.png").write_bytes(fixtures.png(64, 64, fixtures.pattern(64, 64, 1)))
        cases.append(("icon 64x64", self.tree, icon))
        cases.append(("long name", self.tree, fixtures.badge_set(self.tmp / "sets" / "Long", 1, prefix="x" * 70)))
        notes = fixtures.badge_set(self.tmp / "sets" / "Notes", 1)
        (notes / "notes.txt").write_text("x")
        cases.append(("notes.txt", self.tree, notes))
        sub = fixtures.badge_set(self.tmp / "sets" / "Sub", 1)
        (sub / "inner").mkdir()
        cases.append(("subfolder", self.tree, sub))
        empty = self.tmp / "sets" / "Empty"
        empty.mkdir()
        (empty / ".DS_Store").write_bytes(b"x")
        cases.append(("no badge", self.tree, empty))
        blank = fixtures.badge_set(self.tmp / "sets" / "Blank", 1)
        (blank / " .png").write_bytes((blank / "Badge 01.png").read_bytes())
        cases.append(("blank name", self.tree, blank))
        for what, tree, set_dir in cases:
            with self.subTest(what):
                p, c, _, _ = self.same_append(self.both, tree, set_dir, what=what)
                self.assertEqual(p[0], 2, what)
        with self.subTest("present"):
            _, _, _, out = self.same_append(self.both, self.tree, good)
            self.same_append(self.both, out, good, what="present")
        with self.subTest("not ours, no record"):
            p, c, _, _ = self.both.remove(self.tree)
            self.same_result(p, c, "not ours")
            (self.both.py / "undo").rename(self.both.py / "undo-gone")
            self.both.records = []
            p, c, _, _ = self.both.remove(out)
            self.same_result(p, c, "no undo record")
        with self.subTest("damaged manifest"):
            m = json.loads((self.both.py / "manifest.json").read_text())
            m["next_badge_id"] = 0x4000
            (self.both.py / "manifest.json").write_text(json.dumps(m))
            to_lines(self.both.py / "manifest.json", self.both.c / "manifest.txt")
            self.same_append(self.both, self.tree, fixtures.badge_set(self.tmp / "sets" / "After", 1),
                             what="damaged manifest")
            self.same_list(self.tree, "list, damaged manifest")

    def test_check_slots(self):
        set_dir = fixtures.badge_set(self.tmp / "sets" / "New Set", 2)
        _, _, _, appended = self.same_append(self.both, self.tree, set_dir)
        _, after = bs.read_tree(appended)
        gone = bytearray(self.mng)
        struct.pack_into("<I", gone, bs.LAYOUT + 4, 0x999)                      # a placement whose badge is gone
        index = bytearray(self.mng)
        struct.pack_into("<H", index, bs.LAYOUT + 12, 3)                        # its index differs
        damaged = bytearray(self.mng)
        struct.pack_into("<I", damaged, 0, 7)
        for what, copies, saved, tree in (
                ("clean", [self.mng, None, self.mng], None, self.tree),
                ("after append", [after, self.mng], None, appended),
                ("missing, saved", [self.mng, bytes(gone)], None, self.tree),
                ("missing, not saved", [self.mng, bytes(gone)], [1, 0], self.tree),
                ("index differs", [bytes(index)], None, self.tree),
                ("damaged copy", [bytes(damaged), self.mng], None, self.tree)):
            with self.subTest(what):
                slots = fixtures.slots_tree(self.tmp / f"slots-{what}", fixtures.slots(copies, saved))
                p = py_tool("check-slots", "--data", self.both.py, "--in", tree, "--slots", slots)
                c = c_tool("check-slots", "--in", tree, "--slots", slots)
                self.assertEqual((p[0], p[1]), (c[0], c[1]), what)
        with self.subTest("bad headers"):
            bad = fixtures.slots_tree(self.tmp / "bad", b"\x01" + bytes(bs.SLOTS_SIZE - 1))
            p = py_tool("check-slots", "--data", self.both.py, "--in", self.tree, "--slots", bad)
            c = c_tool("check-slots", "--in", self.tree, "--slots", bad)
            self.assertEqual((p[0], py_class(p[2])), (c[0], c_class(c[2])))
            live = fixtures.tree(self.tmp / "live-bad", self.data, bytes(damaged))
            p = py_tool("check-slots", "--data", self.both.py, "--in", live, "--slots", bad)
            c = c_tool("check-slots", "--in", live, "--slots", bad)
            self.assertEqual((p[0], py_class(p[2])), (c[0], c_class(c[2])))

    def test_listing_for_a_screen(self):
        """sets: per set the icon (48x48 tiled, from the file) and whether remove-last would take it"""
        set_dir = fixtures.badge_set(self.tmp / "sets" / "Icon Set", 2, icon=True)
        _, _, _, out = self.same_append(self.both, self.tree, set_dir)
        placed = bytearray(bs.read_tree(out)[1])
        fixtures.place(placed, 3, 13, 50)
        placed_tree = fixtures.tree(self.tmp / "placed", bs.read_tree(out)[0], bytes(placed))
        for what, tree, removable in (("before", self.tree, None), ("appended", out, 3),
                                      ("placed", placed_tree, None)):
            with self.subTest(what):
                code, text, err = c_tool("sets", "--in", tree, "--manifest", self.both.c / "manifest.txt")
                self.assertEqual(code, 0, err)
                data, mng = bs.read_tree(tree)
                col = bs.parse(data, mng)
                lines = text.splitlines()
                rows = [line.split("\t") for line in lines[1:]]
                self.assertEqual(len(rows), len(col["sets"]))
                for row, s in zip(rows, col["sets"]):
                    icon = bytearray(0x1200)
                    base = bs.SET_ICONS + s["index"] * bs.SET_ICON_SIZE
                    for y in range(48):
                        for x in range(48):
                            o = base + 2 * bs.tiled(x, y, 64)
                            icon[2 * bs.tiled(x, y, 48):2 * bs.tiled(x, y, 48) + 2] = data[o:o + 2]
                    self.assertEqual(row[:3], [str(s["slot"]), str(s["index"]), str(s["unique"])])
                    self.assertEqual(row[5], hashlib.sha256(bytes(icon)).hexdigest())
                    self.assertEqual(row[6], s["name"])
                    self.assertEqual(row[4], "1" if s["slot"] == removable else "0")
                # whether badge_sets.py remove-last gets past the same checks
                p = py_tool("remove-last", "--data", self.both.py, "--in", tree, "--out", self.tmp / f"rm-{what}")
                structural = p[0] == 2 and py_class(p[2]) in ("BADGE_E_EMPTY", "BADGE_E_NOT_OURS", "BADGE_E_LAST",
                                                              "BADGE_E_PLACED")
                self.assertEqual(lines[0] == "remove BADGE_OK", not structural, p[2])


# ---------------------------------------------------------------- a set from a zip and from one PNG

def zip_of(path, members, compress=zipfile.ZIP_DEFLATED):
    with zipfile.ZipFile(path, "w", compress) as z:
        for name, data in members:
            if name.endswith("/"):
                z.writestr(zipfile.ZipInfo(name), b"")
            else:
                z.writestr(name, data)
    return path


@contextlib.contextmanager
def extra_entry(folder, name):
    """badge_sets.py sees one more entry in folder (as on a case-sensitive file system)"""
    original = pathlib.Path.iterdir

    def iterdir(self):
        items = list(original(self))
        return iter(items + [folder / name] if self == folder else items)
    pathlib.Path.iterdir = iterdir
    try:
        yield
    finally:
        pathlib.Path.iterdir = original


class Sources(Base):
    def setUp(self):
        super().setUp()
        self.data, self.mng = fixtures.collection()
        self.tree = fixtures.tree(self.tmp / "work" / "000014d1", self.data, self.mng)

    def test_zip_equals_folder(self):
        folder = sheet_set(self.tmp / "sets" / "Zipped", [(64, 64), (128, 64), (64, 128)], icon=True)
        files = sorted(folder.iterdir())
        layouts = {
            "root": [(p.name, p.read_bytes()) for p in reversed(files)],
            "top folder": [("Zipped/", b"")] + [(f"Zipped/{p.name}", p.read_bytes()) for p in files] +
                          [("__MACOSX/Zipped/._Sheet 01.png", b"junk"), ("Zipped/.DS_Store", b"x")],
            "stored": [(p.name, p.read_bytes()) for p in files],
        }
        for what, members in layouts.items():
            with self.subTest(what):
                both = Both(self.tmp / f"dirs-{what}")
                zip_path = self.tmp / f"{what}" / "Zipped.zip"
                zip_path.parent.mkdir()
                zip_of(zip_path, members, zipfile.ZIP_STORED if what == "stored" else zipfile.ZIP_DEFLATED)
                self.same_append(both, self.tree, folder, c_set=zip_path, what=f"zip, {what}")

    def test_two_icon_names(self):
        """_seticon.png and _SETICON.png (a zip or a case-sensitive card can hold both): neither is a badge, the
        last in the order is the icon. badge_sets.py gets the same folder through a second directory entry; it
        never reads the one it drops, so its folder holds only the kept icon"""
        badge = fixtures.png(64, 64, fixtures.pattern(64, 64, 1))
        small, big = palette_png(48, 48, 5), fixtures.png(64, 64, fixtures.pattern(64, 64, 9))
        for what, dropped, kept in (("48x48 kept", big, small), ("64x64 kept", small, big)):
            with self.subTest(what):
                both = Both(self.tmp / f"dirs-{what}")
                folder = self.tmp / what / "Icons"
                folder.mkdir(parents=True)
                (folder / "Badge.png").write_bytes(badge)
                (folder / "_seticon.png").write_bytes(kept)
                zip_path = zip_of(self.tmp / what / "Icons.zip", [("_seticon.png", kept), ("Badge.png", badge),
                                                                 ("_SETICON.png", dropped)])
                with extra_entry(folder, "_SETICON.png"):
                    p, c, _, _ = self.same_append(both, self.tree, folder, c_set=zip_path, what=what)
                self.assertEqual(p[0], 0 if what == "48x48 kept" else 2)

    def test_zip_rules(self):
        png = fixtures.png(64, 64, fixtures.pattern(64, 64, 1))
        for what, members, expect in (
                ("subfolder", [("a.png", png), ("sub/b.png", png)], "BADGE_E_FOLDER"),
                ("two top folders", [("A/a.png", png), ("B/b.png", png)], "BADGE_E_FOLDER"),
                ("hidden folder", [("a.png", png), (".git/x", b"x")], None),
                ("text file", [("a.png", png), ("readme.txt", b"x")], "BADGE_E_FOLDER"),
                ("twice", [("a.png", png), ("x/../a.png", png)], "BADGE_E_FOLDER"),
                ("empty", [], "BADGE_E_FOLDER")):
            with self.subTest(what):
                path = zip_of(self.tmp / f"{what}.zip", members)
                code, out, err = c_tool("files", path)
                self.assertEqual(c_class(err) if code else None, expect, out + err)
        with self.subTest("not a zip"):
            path = self.tmp / "bad.zip"
            path.write_bytes(b"PK\3\4 not really")
            code, _, err = c_tool("files", path)
            self.assertEqual(c_class(err), "BADGE_E_ZIP")
        with self.subTest("UTF-8 names"):
            path = zip_of(self.tmp / "utf8.zip", [("Äpfel.png", png)])
            code, out, err = c_tool("files", path)
            self.assertEqual(code, 0, err)
            self.assertIn("tile Äpfel.png\t0\t0\t0\tÄpfel", out)

    def test_one_png(self):
        for what, size in (("one badge", (64, 64)), ("a sheet", (192, 128))):
            with self.subTest(what):
                both = Both(self.tmp / f"dirs-{what}")
                folder = self.tmp / what / "Sheet"
                folder.mkdir(parents=True)
                (folder / "Sheet.png").write_bytes(fixtures.png(*size, fixtures.pattern(*size, 8)))
                single = self.tmp / what / "single" / "Sheet.png"
                single.parent.mkdir()
                shutil.copy(folder / "Sheet.png", single)
                self.same_append(both, self.tree, folder, c_set=single, what=f"one PNG, {what}")


# ---------------------------------------------------------------- the console's files (local only)

def data_dir():
    d = setting("BADGES_DATA")
    return Path(d) if d and (Path(d) / "work").is_dir() else None


@unittest.skipIf(data_dir() is None, "BADGES_DATA not set or without work/ (the decrypted console files)")
class ConsoleFiles(Base):
    @classmethod
    def setUpClass(cls):
        cls.data = data_dir()
        cls.trees = sorted(p.parent.parent for p in (cls.data / "work").glob("**/000014d1/user/BadgeData.dat")
                           if p.parent.parent.name == "000014d1")

    def test_list_and_check_slots(self):
        """Every decrypted tree: list line for line (with the real manifest), check-slots line for line"""
        both = Both(self.tmp / "dirs", self.data / "manifest.json")
        checked = slotted = 0
        for tree in self.trees:
            p = py_tool("list", "--data", both.py, "--in", tree)
            c = c_tool("list", "--in", tree, "--manifest", both.c / "manifest.txt")
            self.assertEqual((p[0], p[1]), (c[0], c[1]), "list of a work tree")
            checked += 1
            slots = tree.parent / "0000165c"
            if (slots / "user" / "MngFile.dat").is_file():
                p = py_tool("check-slots", "--data", both.py, "--in", tree, "--slots", slots)
                c = c_tool("check-slots", "--in", tree, "--slots", slots)
                self.assertEqual((p[0], p[1]), (c[0], c[1]), "check-slots of a work tree")
                slotted += 1
        self.assertGreater(checked, 0)
        print(f"\n  console files: list compared on {checked} trees, check-slots on {slotted}", file=sys.stderr)

    def test_current_numbers(self):
        """The state the spec names: 3 sets, 288 badges, 11 placements, slots 1 to 6 saved with 0 missing"""
        tree = self.data / "work" / "2026-11-13-b4" / "3-after-remove" / "000014d1"
        if not tree.is_dir():
            self.skipTest("work/2026-11-13-b4/3-after-remove not there")
        code, out, err = c_tool("list", "--in", tree)
        self.assertEqual(code, 0, err)
        self.assertIn("sets 3/100, badges 288/1000, placed 11/360", out)
        code, out, err = c_tool("check-slots", "--in", tree, "--slots", tree.parent / "0000165c")
        self.assertEqual(code, 0, err)
        rows = [line.split() for line in out.splitlines()
                if line.startswith(("live ", "slot ")) and not line.startswith("live collection")]
        self.assertEqual(rows[0][2], "11")
        saved = [r for r in rows[1:] if r[2] == "yes"]
        self.assertEqual([r[1] for r in saved], ["1", "2", "3", "4", "5", "6"])
        self.assertTrue(all(r[5] == "0" for r in rows if len(r) > 5))

    def test_b4_append_reproduced(self):
        """The append that test B4 put onto the console, and the removal after HOME had changed the files"""
        b4 = self.data / "work" / "2026-11-13-b4"
        before, written = b4 / "1-before" / "000014d1", b4 / "1-before" / "000014d1.append-7000"
        home, removed = b4 / "2-after-append" / "000014d1", b4 / "2-after-append" / "000014d1.remove-7000"
        set_dir = self.data / "sets" / "B4 Test"
        if not all(p.is_dir() for p in (before, written, home, removed, set_dir)):
            self.skipTest("the B4 trees are not all there")
        both = Both(self.tmp / "dirs", self.data / "manifest.json")
        p, c, py_out, c_out = self.same_append(both, before, set_dir, what="B4 append")
        self.assertEqual(files_of(c_out), files_of(written), "the core's append differs from what went onto the card")
        p, c, py_out, c_out = both.remove(home, allow=True)
        self.same_result(p, c, "B4 removal after HOME's changes")
        self.assertEqual(files_of(c_out), files_of(removed), "the core's removal differs from what went onto the card")

    def test_append_remove_on_console_files(self):
        """A generated set onto copies of the console's files: the same bytes as badge_sets.py, and back exactly"""
        sets = fixtures.badge_set(self.tmp / "sets" / "Generated", 3, icon=True)
        sheet = sheet_set(self.tmp / "sets" / "Sheet Set", [(128, 128)])
        for tree in self.trees[:4]:
            with self.subTest(tree.parent.name):
                both = Both(self.tmp / f"dirs-{len(list(self.tmp.iterdir()))}", self.data / "manifest.json")
                p, c, _, out = self.same_append(both, tree, sets, what="generated set")
                if p[0]:
                    continue                                # refused by both, for the same reason
                p, c, _, out2 = self.same_append(both, out, sheet, what="a second set")
                for step, target in ((out2, out), (out, tree)):
                    p, c, _, back = both.remove(step)
                    self.same_result(p, c, "remove")
                    self.assertEqual(files_of(back), files_of(target))

    def test_card_folders_give_the_console_images(self):
        """The card's set folders through the core's PNG reader and conversion: every badge image and set icon as
        the console's BadgeData.dat holds it (matched by name, a sheet's tiles in order)"""
        cards = sorted((self.data / "backups").glob("*/card-Badges"))
        tree = self.data / "work" / "current" / "000014d1"
        if not cards or not tree.is_dir():
            self.skipTest("no card-Badges backup or work/current")
        data, mng = bs.read_tree(tree)
        col = bs.parse(data, mng)
        by_name = {}
        for b in col["badges"]:
            title = bs.title(data, bs.BADGE_TITLES + b["slot"] * bs.TITLE_BLOCK + bs.TITLE_SIZE)
            by_name.setdefault((b["set_id"], title), []).append(b["slot"])
        matched = icons = 0
        for s in col["sets"]:
            folder = cards[-1] / s["name"]
            if not folder.is_dir():
                continue
            code, out, err = c_tool("files", folder)
            self.assertEqual(code, 0, err)
            both = Both(self.tmp / f"img-{s['slot']}")
            empty = fixtures.tree(self.tmp / f"empty-{s['slot']}", *fixtures.collection(sets=[], placed=()))
            p, c, _, c_out = both.append(empty, folder)
            self.assertEqual((p[0], c[0]), (0, 0), c[2])
            new_data, _ = bs.read_tree(c_out)
            seen = {}
            for k, line in enumerate(l for l in out.splitlines() if l.startswith("tile ")):
                name = line.split("\t")[-1]
                n = seen.get(name, 0)
                seen[name] = n + 1
                real = by_name[(s["set_id"], name)][n]
                for base, size in ((bs.BADGE_ICONS, bs.BADGE_ICON_SIZE), (bs.SMALL_ICONS, bs.SMALL_ICON_SIZE)):
                    self.assertEqual(new_data[base + k * size:base + (k + 1) * size],
                                     data[base + real * size:base + (real + 1) * size], "a badge image differs")
                matched += 1
            if (folder / "_seticon.png").is_file():
                ours = new_data[bs.SET_ICONS:bs.SET_ICONS + bs.SET_ICON_SIZE]
                theirs = data[bs.SET_ICONS + s["index"] * bs.SET_ICON_SIZE:][:bs.SET_ICON_SIZE]
                rows = [(y, x) for y in range(48) for x in range(48)]
                self.assertTrue(all(ours[2 * bs.tiled(x, y, 64):2 * bs.tiled(x, y, 64) + 2] ==
                                    theirs[2 * bs.tiled(x, y, 64):2 * bs.tiled(x, y, 64) + 2] for y, x in rows),
                                "a set icon differs in its 48x48 part")
                icons += 1
        self.assertEqual(matched, sum(s["unique"] for s in col["sets"] if (cards[-1] / s["name"]).is_dir()))
        print(f"\n  console files: {matched} badge images and {icons} set icons from the card's folders match",
              file=sys.stderr)


def main():
    global BUILD
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", required=True, help="build/host or build/asan")
    args, rest = parser.parse_known_args()
    BUILD = (ROOT / args.build).resolve()
    if not (BUILD / "test_badge").is_file():
        sys.exit(f"{BUILD / 'test_badge'} missing: run make host asan")
    print(f"badge tests with {BUILD.relative_to(ROOT)}", file=sys.stderr)
    unittest.main(argv=[sys.argv[0], *rest], verbosity=2)


if __name__ == "__main__":
    main()
