"""Tests for badge_sets.py on generated data only: the PNG reader, the image conversion, the strict reader, append
and its invariant, remove-last back to the exact bytes, the manifest rules, check-slots and the fixtures CLI.

Run: python3 -I -m unittest discover -s . -p "test_*.py"
"""
import contextlib
import hashlib
import io
import json
import shutil
import struct
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))  # python -I does not add the script directory
import badge_sets as bs  # noqa: E402
import fixtures  # noqa: E402

TODAY = "2026-11-12"


def run(*argv):
    out, err = io.StringIO(), io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
        code = bs.main([str(a) for a in argv])
    return code, out.getvalue(), err.getvalue()


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def anemone_index_64(i):
    """Pixel (x, y) of tiled index i, as Anemone3DS's rgb565ToPngFile computes it (an independent formula)."""
    x = 8 * ((i // 64) % 8) + ((i % 64) & 1) + (((i % 64) & 4) >> 1) + (((i % 64) & 0x10) >> 2)
    y = 8 * (i // 512) + (((i % 64) & 2) >> 1) + (((i % 64) & 8) >> 2) + (((i % 64) & 0x20) >> 3)
    return x, y


def anemone_index_32(x, y):
    """Tiled index of (x, y) in a 32x32 image, as Anemone3DS's pngToRGB565 computes it."""
    return (4 * 64 * ((y // 8) % 4) | 64 * ((x // 8) % 4) | 32 * ((y // 4) % 2) | 16 * ((x // 4) % 2)
            | 8 * ((y // 2) % 2) | 4 * ((x // 2) % 2) | 2 * (y % 2) | (x % 2))


class Png(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.tmp)

    def decode(self, raw):
        path = self.tmp / "x.png"
        path.write_bytes(raw)
        return bs.read_png(path)

    def test_rgba_all_filters(self):
        rgba = fixtures.pattern(64, 70, 3)
        self.assertEqual(self.decode(fixtures.png(64, 70, rgba)), (64, 70, rgba))

    def test_other_colour_types(self):
        rgba = fixtures.pattern(16, 10, 5)
        w, h, rgb = self.decode(fixtures.png(16, 10, rgba, color_type=2))
        self.assertEqual(rgb, bytes(v if i % 4 != 3 else 255 for i, v in enumerate(rgba)))
        _, _, grey = self.decode(fixtures.png(16, 10, rgba, color_type=0))
        self.assertEqual(grey[:8], bytes((rgba[0],) * 3 + (255,) + (rgba[4],) * 3 + (255,)))
        _, _, grey_alpha = self.decode(fixtures.png(16, 10, rgba, color_type=4))
        self.assertEqual(grey_alpha[:4], bytes((rgba[0],) * 3 + (rgba[3],)))

    def test_palette_with_alpha(self):
        palette = [(255, 0, 0, 255), (0, 255, 0, 128), (0, 0, 255, 0)]
        rgba = b"".join(bytes(palette[(x + y) % 3]) for y in range(9) for x in range(9))
        self.assertEqual(self.decode(fixtures.png(9, 9, rgba, color_type=3, palette=palette))[2], rgba)

    def test_rgb_transparency_key(self):
        rgba = bytes((10, 20, 30, 255, 1, 2, 3, 255))
        _, _, out = self.decode(fixtures.png(2, 1, rgba, color_type=2, trns=bytes((0, 1, 0, 2, 0, 3))))
        self.assertEqual(out, bytes((10, 20, 30, 255, 1, 2, 3, 0)))

    def test_palette_refusals(self):
        palette = [(255, 0, 0, 255), (0, 255, 0, 255)]
        raw = fixtures.png(2, 1, bytes(palette[0] + palette[1]), color_type=3, palette=palette)
        plte = raw.index(b"PLTE") - 4
        with self.assertRaisesRegex(bs.BadgeError, "without a valid PLTE"):
            self.decode(raw[:plte] + raw[plte + 12 + 6:])     # the 6-byte PLTE chunk left out
        short = fixtures._chunk(b"PLTE", bytes(palette[0][:3]))
        with self.assertRaisesRegex(bs.BadgeError, "palette index 1 outside the palette"):
            self.decode(raw[:plte] + short + raw[plte + 12 + 6:])
        with self.assertRaisesRegex(ValueError, "needs a palette"):
            fixtures.png(1, 1, bytes(4), color_type=3)

    def test_refused(self):
        good = fixtures.png(4, 4, fixtures.pattern(4, 4, 1))
        sixteen = bytearray(good)
        sixteen[24] = 16   # IHDR bit depth
        sixteen[29:33] = struct.pack(">I", bs.zlib.crc32(bytes(sixteen[12:29])))
        laced = bytearray(good)
        laced[28] = 1      # IHDR interlace
        laced[29:33] = struct.pack(">I", bs.zlib.crc32(bytes(laced[12:29])))
        damaged = bytearray(good)
        damaged[-20] ^= 0xFF
        for raw, message in ((b"GIF89a", "not a PNG"), (bytes(sixteen), "16-bit"), (bytes(laced), "interlaced"),
                             (bytes(damaged), "damaged"), (good[:40], "ends before")):
            with self.subTest(message), self.assertRaisesRegex(bs.BadgeError, message):
                self.decode(raw)


class Images(unittest.TestCase):
    def test_tiling_matches_anemone(self):
        for i in range(64 * 64):
            self.assertEqual(bs.tiled(*anemone_index_64(i), 64), i)
        for y in range(32):
            for x in range(32):
                self.assertEqual(bs.tiled(x, y, 32), anemone_index_32(x, y))

    def test_solid_colour_and_downscale(self):
        rgba = bytes((0xFF, 0x80, 0x08, 0xF0)) * 4096
        big, small = bs.badge_images(rgba, 64, 0, 0)
        self.assertEqual(len(big), bs.BADGE_ICON_SIZE)
        self.assertEqual(len(small), bs.SMALL_ICON_SIZE)
        self.assertEqual(big[:2], struct.pack("<H", 0x1F << 11 | 0x20 << 5 | 0x01))
        self.assertEqual(big[0x2000:], b"\xff" * 0x800)
        self.assertEqual(small[:2], big[:2])
        self.assertEqual(small[0x800:], b"\xff" * 0x200)

    def test_downscale_is_the_truncated_mean(self):
        rgba = bytearray(64 * 64 * 4)
        for (x, y), px in zip(((0, 0), (1, 0), (0, 1), (1, 1)), ((255, 0, 7, 255), (0, 0, 0, 0), (1, 3, 9, 16),
                                                                  (8, 255, 0, 255))):
            rgba[(y * 64 + x) * 4:(y * 64 + x) * 4 + 4] = bytes(px)
        _, small = bs.badge_images(bytes(rgba), 64, 0, 0)
        r, g, b, a = 264 >> 5, 258 >> 4, 16 >> 5, 526 >> 6
        self.assertEqual(struct.unpack_from("<H", small, 0)[0], r << 11 | g << 5 | b)
        self.assertEqual(small[0x800] & 0xF, a)

    def test_tiles_of_a_sheet_column_by_column(self):
        tmp = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, tmp)
        fixtures.badge_set(tmp / "S", 2, sizes=[(64, 192), (128, 64)])
        tiles, icon = bs.read_set_folder(tmp / "S")
        self.assertIsNone(icon)
        self.assertEqual([(t[0], t[1], t[3], t[4]) for t in tiles],
                         [("Badge 01.png", 0, 0, 0), ("Badge 01.png", 1, 0, 64), ("Badge 01.png", 2, 0, 128),
                          ("Badge 02.png", 0, 0, 0), ("Badge 02.png", 1, 64, 0)])
        self.assertEqual({t[2] for t in tiles}, {"Badge 01", "Badge 02"})


class Reader(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.data, cls.mng = fixtures.collection()

    def test_fixture_reads(self):
        col = bs.parse(self.data, self.mng)
        self.assertEqual([(s["name"], s["unique"], s["start"], s["placed"]) for s in col["sets"]],
                         [("Alpha", 5, 0, 0), ("Beta", 3, 5, 2), ("Gamma", 4, 8, 0)])
        self.assertEqual((col["unique"], col["placed"], col["warnings"], col["nnid_set"]), (12, 2, [], True))

    def refused(self, mng, message):
        with self.assertRaisesRegex(bs.BadgeError, message):
            bs.parse(self.data, bytes(mng))

    def test_refuses_what_does_not_add_up(self):
        m = bytearray(self.mng)
        struct.pack_into("<I", m, 0, 1)
        self.refused(m, "header word 0x0")
        m = bytearray(self.mng)
        m[bs.USED_BADGES] &= ~0x04
        self.refused(m, "used-badge bits")
        m = bytearray(self.mng)
        struct.pack_into("<I", m, bs.SET_COUNT, 2)
        self.refused(m, "header counts 2 sets")
        m = bytearray(self.mng)
        struct.pack_into("<I", m, bs.SET_INFO + bs.SET_INFO_SIZE + 0x24, 4)
        self.refused(m, "claims 3 badges from index 4")
        m = bytearray(self.mng)
        struct.pack_into("<I", m, bs.TOTAL, 7)
        self.refused(m, "header total")
        m = bytearray(self.mng)
        struct.pack_into("<I", m, bs.BADGE_INFO + 3 * bs.BADGE_INFO_SIZE + 4, 1)
        self.refused(m, "repeated or unknown identity")
        with self.assertRaisesRegex(bs.BadgeError, "wrong size"):
            bs.parse(self.data[:-1], self.mng)

    def test_placement_problems_are_warnings(self):
        m = bytearray(self.mng)
        struct.pack_into("<I", m, bs.PLACED, 0)
        col = bs.parse(self.data, bytes(m))
        self.assertTrue(any("HOME recounts" in w for w in col["warnings"]))


class Commands(unittest.TestCase):
    """append, remove-last and check-slots through main(), on fixture trees in a temporary data dir."""

    @classmethod
    def setUpClass(cls):
        cls.data, cls.mng = fixtures.collection()

    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.tmp)
        self.dir = self.tmp / "data"
        self.tree = fixtures.tree(self.dir / "work" / "current" / "000014d1", self.data, self.mng)
        self.set = fixtures.badge_set(self.tmp / "sets" / "New Set", 2)

    def append(self, tree=None, set_dir=None, out=None, *extra):
        args = ["append", "--data", self.dir, "--in", tree or self.tree, set_dir or self.set] + list(extra)
        if out:
            args += ["--out", out]
        return run(*args)

    def test_append_keeps_everything_that_exists(self):
        code, out, err = self.append()
        self.assertEqual((code, err), (0, ""), out)
        new = self.tree.with_name("000014d1.append-7000")
        data = (new / "user" / "BadgeData.dat").read_bytes()
        mng = (new / "user" / "BadgeMngFile.dat").read_bytes()
        self.assertEqual(bs.read_tree(self.tree), (self.data, self.mng))   # the input is never touched
        self.assertEqual((new / "icon").read_bytes(), bytes(4))
        u, s = 12, 3
        unchanged = [   # every record and image slot that existed, and the layout, byte for byte
            ("mng", 0, 4), ("mng", bs.PLACED, bs.TOTAL - bs.PLACED), ("mng", bs.NNID, bs.USED_BADGES - bs.NNID),
            ("mng", bs.BADGE_INFO, u * bs.BADGE_INFO_SIZE), ("mng", bs.SET_INFO, s * bs.SET_INFO_SIZE),
            ("mng", bs.LAYOUT, bs.MAX_PLACED * bs.LAYOUT_RECORD),
            ("data", bs.SET_TITLES, s * bs.TITLE_BLOCK), ("data", bs.BADGE_TITLES, u * bs.TITLE_BLOCK),
            ("data", bs.SET_ICONS, s * bs.SET_ICON_SIZE), ("data", bs.BADGE_ICONS, u * bs.BADGE_ICON_SIZE),
            ("data", bs.SMALL_ICONS, u * bs.SMALL_ICON_SIZE)]
        files = {"data": (self.data, data), "mng": (self.mng, mng)}
        for f, off, size in unchanged:
            self.assertEqual(files[f][0][off:off + size], files[f][1][off:off + size], (f, hex(off)))
        self.assertEqual(bs.unchanged_outside({"data": self.data, "mng": self.mng}, {"data": data, "mng": mng},
                                              bs.regions(s, u, 2), {(bs.USED_BADGES, 12), (bs.USED_BADGES, 13)}), [])
        col = bs.parse(data, mng)
        self.assertEqual([x["name"] for x in col["sets"]], ["Alpha", "Beta", "Gamma", "New Set"])
        added = col["sets"][3]
        self.assertEqual((added["set_id"], added["index"], added["start"], added["unique"], added["total"]),
                         (0x7000, 3, 12, 2, 2 * 0xFFFF))
        self.assertEqual([(b["badge_id"], b["set_id"], b["index"], b["quantity"], b["sub"], b["flags"])
                          for b in col["badges"][12:]], [(0x4000, 0x7000, 12, 0xFFFF, 0, 0),
                                                         (0x4001, 0x7000, 13, 0xFFFF, 0, 0)])
        self.assertEqual(bs.title(data, bs.BADGE_TITLES + 13 * bs.TITLE_BLOCK + 5 * bs.TITLE_SIZE), "Badge 02")
        self.assertEqual(bs.bit(mng, bs.USED_SETS, 3), 0)    # like the sets before it
        self.assertEqual(struct.unpack_from("<I", mng, bs.NNID)[0], fixtures.NNID_VALUE)
        self.assertNotIn(str(fixtures.NNID_VALUE), out)
        self.assertNotIn(f"{fixtures.NNID_VALUE:x}", out.lower())
        manifest = json.loads((self.dir / "manifest.json").read_text())
        self.assertEqual(manifest["sets"][0]["badges"], [{"file": "Badge 01.png", "tile": 0, "badge_id": 0x4000},
                                                         {"file": "Badge 02.png", "tile": 0, "badge_id": 0x4001}])
        rgba = fixtures.pattern(64, 64, 1)
        self.assertEqual(data[bs.BADGE_ICONS + 12 * bs.BADGE_ICON_SIZE:bs.BADGE_ICONS + 13 * bs.BADGE_ICON_SIZE],
                         bs.badge_images(rgba, 64, 0, 0)[0])

    def test_ids_are_stable_and_never_reused(self):
        self.assertEqual(self.append()[0], 0)
        second = self.tmp / "again"
        self.assertEqual(self.append(None, None, second)[0], 0)
        self.assertEqual(sha(second / "user" / "BadgeMngFile.dat"),
                         sha(self.tree.with_name("000014d1.append-7000") / "user" / "BadgeMngFile.dat"))
        (self.set / "Badge 01.png").unlink()
        fixtures.badge_set(self.set, 1, prefix="Extra")
        third = self.tmp / "third"
        self.assertEqual(self.append(None, None, third)[0], 0)
        col = bs.parse(*bs.read_tree(third))
        self.assertEqual([b["badge_id"] for b in col["badges"][12:]], [0x4001, 0x4002])
        other = fixtures.badge_set(self.tmp / "sets" / "Other", 1)
        code, out, _ = self.append(third, other)
        self.assertEqual(code, 0, out)
        col = bs.parse(*bs.read_tree(third.with_name("third.append-7001")))
        self.assertEqual([(s["set_id"], s["index"]) for s in col["sets"][3:]], [(0x7000, 3), (0x7001, 4)])
        self.assertEqual(col["badges"][-1]["badge_id"], 0x4003)

    def test_remove_last_gives_back_the_exact_bytes(self):
        self.assertEqual(self.append()[0], 0)
        appended = self.tree.with_name("000014d1.append-7000")
        other = fixtures.badge_set(self.tmp / "sets" / "Other", 3, icon=True)
        self.assertEqual(self.append(appended, other)[0], 0)
        two = appended.with_name(appended.name + ".append-7001")
        code, out, err = run("remove-last", "--data", self.dir, "--in", two)
        self.assertEqual(code, 0, err)
        self.assertIn("byte-identical", out)
        one = two.with_name(two.name + ".remove-7001")
        for name in bs.DAT_NAMES:
            self.assertEqual(sha(one / "user" / name), sha(appended / "user" / name))
        code, _, err = run("remove-last", "--data", self.dir, "--in", one)
        self.assertEqual(code, 0, err)
        back = one.with_name(one.name + ".remove-7000")
        for name in bs.DAT_NAMES:
            self.assertEqual(sha(back / "user" / name), sha(self.tree / "user" / name))

    def test_remove_last_after_home_moved_other_badges(self):
        self.assertEqual(self.append()[0], 0)
        appended = self.tree.with_name("000014d1.append-7000")
        mng = bytearray((appended / "user" / "BadgeMngFile.dat").read_bytes())
        fixtures.place(mng, 2, 0, 40)      # HOME placed an old badge in between
        (appended / "user" / "BadgeMngFile.dat").write_bytes(bytes(mng))
        code, _, err = run("remove-last", "--data", self.dir, "--in", appended)
        self.assertEqual(code, 2)
        self.assertIn("not what a recorded append wrote", err)   # changed files need the explicit flag
        code, out, err = run("remove-last", "--data", self.dir, "--in", appended, "--allow-home-changes")
        self.assertEqual(code, 0, err)
        self.assertIn("not byte-identical to the files before the append", out)
        expected = bytearray(self.mng)
        fixtures.place(expected, 2, 0, 40)
        removed = appended.with_name(appended.name + ".remove-7000")
        self.assertEqual(bs.read_tree(removed), (self.data, bytes(expected)))

    def test_remove_last_resets_a_view_on_the_removed_set(self):
        self.assertEqual(self.append()[0], 0)
        appended = self.tree.with_name("000014d1.append-7000")
        mng = bytearray((appended / "user" / "BadgeMngFile.dat").read_bytes())
        struct.pack_into("<II", mng, bs.SELECTED_SET, 3, 5)    # HOME showed the new set's tab
        (appended / "user" / "BadgeMngFile.dat").write_bytes(bytes(mng))
        code, out, err = run("remove-last", "--data", self.dir, "--in", appended, "--allow-home-changes")
        self.assertEqual(code, 0, err)
        self.assertIn("back to its state before the append", out)
        self.assertIn("byte-identical to the files before the append (sha256 compared", out)  # the view was all
        self.assertEqual(bs.read_tree(appended.with_name(appended.name + ".remove-7000")), (self.data, self.mng))

    def test_remove_last_refuses(self):
        code, _, err = run("remove-last", "--data", self.dir, "--in", self.tree)
        self.assertEqual(code, 2)
        self.assertIn("not appended by this tool", err)
        self.assertEqual(self.append()[0], 0)
        appended = self.tree.with_name("000014d1.append-7000")
        mng = bytearray((appended / "user" / "BadgeMngFile.dat").read_bytes())
        fixtures.place(mng, 2, 12, 40)     # a badge of the new set is placed
        placed = fixtures.tree(self.tmp / "placed", (appended / "user" / "BadgeData.dat").read_bytes(), bytes(mng))
        code, _, err = run("remove-last", "--data", self.dir, "--in", placed)
        self.assertIn("put them back", err)
        fixtures.unplace(mng, 2)           # put back: removable again
        (placed / "user" / "BadgeMngFile.dat").write_bytes(bytes(mng))
        self.assertEqual(run("remove-last", "--data", self.dir, "--in", placed, "--out", self.tmp / "ok")[0], 0)
        data = bytearray((appended / "user" / "BadgeData.dat").read_bytes())
        image = bs.BADGE_ICONS + 12 * bs.BADGE_ICON_SIZE
        data[image] ^= 1    # the new set's image changed since the append
        changed = fixtures.tree(self.tmp / "changed", bytes(data),
                                (appended / "user" / "BadgeMngFile.dat").read_bytes())
        code, _, err = run("remove-last", "--data", self.dir, "--in", changed, "--allow-home-changes")
        self.assertEqual(code, 2)
        self.assertIn(f"regions that differ from the closest record: data {image:#x}", err)
        for path in (self.dir / "undo").iterdir():
            path.unlink()
        code, _, err = run("remove-last", "--data", self.dir, "--in", appended)
        self.assertEqual(code, 2)
        self.assertIn("no undo record", err)

    def test_undo_records_belong_to_one_input(self):
        """Two inputs that differ only inside the areas the append replaces give one output; each keeps its record."""
        data = bytearray(self.data)
        data[bs.BADGE_ICONS + 12 * bs.BADGE_ICON_SIZE + 100] ^= 0xFF      # leftover byte in the next image slot
        other = fixtures.tree(self.tmp / "other" / "000014d1", bytes(data), self.mng)
        code, out_a, _ = self.append(None, None, self.tmp / "out-a")
        self.assertEqual(code, 0)
        code, out_b, _ = self.append(other, None, self.tmp / "out-b")
        self.assertEqual(code, 0)
        record = {k: Path(next(line.split(" ", 2)[2].rsplit(" (", 1)[0] for line in o.splitlines()
                               if line.startswith("undo record ")))
                  for k, o in (("a", out_a), ("b", out_b))}
        self.assertNotEqual(record["a"], record["b"])
        self.assertEqual(len(list((self.dir / "undo").iterdir())), 2)
        for name in bs.DAT_NAMES:   # one output for both inputs
            self.assertEqual(sha(self.tmp / "out-a" / "user" / name), sha(self.tmp / "out-b" / "user" / name))
        code, _, err = run("remove-last", "--data", self.dir, "--in", self.tmp / "out-a")
        self.assertEqual(code, 2)
        self.assertIn("2 undo records fit these files", err)
        for k, source in (("a", self.tree), ("b", other)):
            code, out, err = run("remove-last", "--data", self.dir, "--in", self.tmp / "out-a", "--undo", record[k],
                                 "--out", self.tmp / f"back-{k}")
            self.assertEqual(code, 0, err)
            self.assertIn("byte-identical", out)
            self.assertEqual(bs.read_tree(self.tmp / f"back-{k}"), bs.read_tree(source))
        code, out, _ = self.append(None, None, self.tmp / "out-a2")       # the same input again
        self.assertEqual(code, 0)
        self.assertIn("(already there for this input, identical)", out)
        tampered = json.loads(record["a"].read_text())
        tampered["view_before"] = "00" * 8
        record["a"].write_text(json.dumps(tampered))
        code, _, err = self.append(None, None, self.tmp / "out-a3")
        self.assertEqual(code, 2)
        self.assertIn("holds something else", err)
        self.assertFalse((self.tmp / "out-a3").exists())

    def test_append_refuses(self):
        self.assertEqual(self.append()[0], 0)
        appended = self.tree.with_name("000014d1.append-7000")
        cases = []
        cases.append((self.append(appended), "already in the collection"))
        cases.append((self.append(None, None, appended), "exists"))
        bad = fixtures.badge_set(self.tmp / "sets" / "Bad", 1)
        (bad / "notes.txt").write_text("x")
        cases.append((self.append(None, bad), "only PNG files"))
        odd = self.tmp / "sets" / "Odd"
        odd.mkdir()
        (odd / "a.png").write_bytes(fixtures.png(64, 65, fixtures.pattern(64, 65, 1)))
        cases.append((self.append(None, odd), "multiples of 64"))
        icon = fixtures.badge_set(self.tmp / "sets" / "Icon", 1)
        (icon / "_seticon.png").write_bytes(fixtures.png(64, 64, fixtures.pattern(64, 64, 1)))
        cases.append((self.append(None, icon), "a set icon is 48x48"))
        long = fixtures.badge_set(self.tmp / "sets" / "Long", 1, prefix="x" * 70)
        cases.append((self.append(None, long), "longer than 68"))
        for (code, _, err), message in cases:
            with self.subTest(message):
                self.assertEqual(code, 2)
                self.assertIn(message, err)

    def test_limits(self):
        full = fixtures.tree(self.tmp / "full", *fixtures.collection(sets=[("Big", 999)], placed=()))
        code, _, err = self.append(full)
        self.assertEqual(code, 2)
        self.assertIn("at most 1000", err)
        many = fixtures.tree(self.tmp / "many", *fixtures.collection(sets=[(f"S{n}", 1) for n in range(100)],
                                                                     placed=()))
        code, _, err = self.append(many)
        self.assertEqual(code, 2)
        self.assertIn("at most 100", err)
        one_more = fixtures.tree(self.tmp / "998", *fixtures.collection(sets=[("Big", 998)], placed=()))
        self.assertEqual(self.append(one_more)[0], 0)

    def test_the_invariant_is_checked_inside_append(self):
        real = bs.regions
        with mock.patch.object(bs, "regions", lambda s, u, n: [r for r in real(s, u, n) if r[1] != bs.BADGE_TITLES
                                                                + u * bs.TITLE_BLOCK]):
            code, _, err = self.append()
        self.assertEqual(code, 2)
        self.assertIn("would not keep the existing collection intact", err)
        self.assertFalse(self.tree.with_name("000014d1.append-7000").exists())
        before = {"data": self.data, "mng": self.mng}
        flipped = bytearray(self.mng)
        flipped[bs.USED_BADGES + 1] |= 0x20   # bit 13 (new) and, wrongly, bit 9 (old) cleared
        flipped[bs.USED_BADGES + 1] &= ~0x02
        problems = bs.unchanged_outside(before, {"data": self.data, "mng": bytes(flipped)}, bs.regions(3, 12, 2),
                                        {(bs.USED_BADGES, 12), (bs.USED_BADGES, 13)})
        self.assertEqual(problems, [f"used bit 9 at {bs.USED_BADGES:#x} would change"])

    def test_set_bit_follows_the_sets_before(self):
        flagged = fixtures.tree(self.tmp / "flagged", *fixtures.collection(set_bits=True))
        self.assertEqual(self.append(flagged)[0], 0)
        mng = (flagged.with_name("flagged.append-7000") / "user" / "BadgeMngFile.dat").read_bytes()
        self.assertEqual(bs.bit(mng, bs.USED_SETS, 3), 1)

    def test_list_shows_no_nnid(self):
        self.assertEqual(self.append()[0], 0)
        code, out, _ = run("list", "--data", self.dir, "--in", self.tree.with_name("000014d1.append-7000"))
        self.assertEqual(code, 0)
        self.assertIn("NNID field: set", out)
        self.assertIn("New Set  (appended by this tool)", out)
        self.assertNotIn(str(fixtures.NNID_VALUE), out)
        self.assertNotIn(f"{fixtures.NNID_VALUE:x}", out.lower())

    def test_check_slots(self):
        lost = bytearray(self.mng)
        struct.pack_into("<I", lost, bs.LAYOUT + 4 * bs.LAYOUT_RECORD + 8, 0x99)   # set 0x99 is not live
        struct.pack_into("<I", lost, bs.LAYOUT + 4 * bs.LAYOUT_RECORD + 4, 1)
        moved = bytearray(self.mng)
        struct.pack_into("<H", moved, bs.LAYOUT + 0xC, 7)   # same badge and set ID, other index
        slots = fixtures.slots_tree(self.dir / "work" / "current" / "0000165c",
                                    fixtures.slots([self.mng, None, bytes(lost)], [1, 0, 0]))
        code, out, _ = run("check-slots", "--data", self.dir)
        self.assertEqual(code, 0, out)
        self.assertRegex(out, r"slot 1\s+yes\s+2\s+2\s+0\s+0")
        self.assertRegex(out, r"slot 3\s+no\s+3\s+2\s+1\s+0")   # not saved: does not count
        (slots / "user" / "MngFile.dat").write_bytes(fixtures.slots([self.mng, bytes(moved), bytes(lost)]))
        code, out, _ = run("check-slots", "--data", self.dir)
        self.assertEqual(code, 1, out)
        self.assertRegex(out, r"slot 2\s+yes\s+2\s+2\s+0\s+1")
        self.assertRegex(out, r"slot 3\s+yes\s+3\s+2\s+1\s+0")
        self.assertIn("FOUND", out)
        self.assertEqual(self.append()[0], 0)    # after an append every old placement still resolves
        code, out, _ = run("check-slots", "--data", self.dir, "--in", self.tree.with_name("000014d1.append-7000"),
                           "--slots", slots)
        self.assertRegex(out, r"slot 1\s+yes\s+2\s+2\s+0\s+0")

    def test_fixtures_cli(self):
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(fixtures.main(["set", str(self.tmp / "T"), "--badges", "3", "--icon"]), 0)
        self.assertEqual(sorted(p.name for p in (self.tmp / "T").iterdir()),
                         ["Test 01.png", "Test 02.png", "Test 03.png", "_seticon.png"])


def doc_fields(mng):
    """Header and set records read with the offsets written out from 3dbrew "Home Menu" (BadgeMngFile.dat,
    BadgeSetInfo) and Anemone3DS v3.0.1 source/badges.c (l.744-753 header, l.271-279 set record), on purpose
    without badge_sets' constants: (set count, unique badges, total copies, [(set ID, set index, badges, copies,
    first index)])."""
    sets, unique = struct.unpack_from("<II", mng, 0x4)
    total = struct.unpack_from("<I", mng, 0x18)[0]
    records = [struct.unpack_from("<IIIIII", mng, 0xA028 + i * 0x30 + 0x10) for i in range(sets)]
    return sets, unique, total, [(sid, index, count, copies, start) for sid, index, _, count, copies, start in records]


def doc_badge(data, mng, i):
    """Badge i read with written-out offsets: 3dbrew BadgeInfo at 0x3E8 + i x 0x28, used bit at 0x358; Anemone3DS
    l.136-153: name at 0x35E80 + i x 16 x 0x8A, 64x64 RGB565 at 0x318F80 + i x 0x2800 with A4 0x2000 after it, 32x32
    at 0xCDCF80 + i x 0xA00 with A4 0x800 after it."""
    unknown, badge_id, set_id, index, sub, placed, quantity = struct.unpack_from("<IIIHHHH", mng, 0x3E8 + i * 0x28)
    name = data[0x35E80 + i * 16 * 0x8A:0x35E80 + i * 16 * 0x8A + 0x8A].decode("utf-16-le").split("\0")[0]
    used = mng[0x358 + i // 8] >> (i % 8) & 1
    return {"ids": (unknown, badge_id, set_id, index, sub), "placed": placed, "quantity": quantity, "name": name,
            "used": used, "rgb": data[0x318F80 + i * 0x2800:0x318F80 + i * 0x2800 + 0x2000],
            "alpha": data[0x318F80 + i * 0x2800 + 0x2000:0x318F80 + (i + 1) * 0x2800],
            "small": data[0xCDCF80 + i * 0xA00:0xCDCF80 + i * 0xA00 + 0x800]}


class DocOffsets(unittest.TestCase):
    """The tool's output read back with the documented offsets, not with the tool's own constants."""

    def test_fixture_and_append(self):
        data, mng = fixtures.collection()
        self.assertEqual(doc_fields(mng), (3, 12, 12 * 0xFFFF, [(1, 0, 5, 5 * 0xFFFF, 0), (2, 1, 3, 3 * 0xFFFF, 5),
                                                                (3, 2, 4, 4 * 0xFFFF, 8)]))
        tmp = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, tmp)
        tree = fixtures.tree(tmp / "000014d1", data, mng)
        fixtures.badge_set(tmp / "New Set", 2)
        self.assertEqual(run("append", "--data", tmp / "data", "--in", tree, tmp / "New Set")[0], 0)
        new_data, new_mng = ((tmp / "000014d1.append-7000" / "user" / f).read_bytes() for f in bs.DAT_NAMES)
        sets, unique, total, records = doc_fields(new_mng)
        self.assertEqual((sets, unique, total), (4, 14, 14 * 0xFFFF))
        self.assertEqual(records[:3], doc_fields(mng)[3])
        self.assertEqual(records[3], (0x7000, 3, 2, 2 * 0xFFFF, 12))
        self.assertEqual(new_mng[0xA028 + 3 * 0x30:0xA028 + 3 * 0x30 + 0x10], b"\xff" * 8 + bytes(4) + b"\x10\x27\0\0")
        self.assertEqual(new_data[3 * 16 * 0x8A:3 * 16 * 0x8A + 16].decode("utf-16-le"), "New Set\0")
        rgba = fixtures.pattern(64, 64, 1)
        for i, badge_id, name in ((12, 0x4000, "Badge 01"), (13, 0x4001, "Badge 02")):
            b = doc_badge(new_data, new_mng, i)
            self.assertEqual((b["ids"], b["placed"], b["quantity"], b["name"], b["used"]),
                             ((0, badge_id, 0x7000, i, 0), 0, 0xFFFF, name, 1))
        b = doc_badge(new_data, new_mng, 12)
        for (x, y), n in (((0, 0), 0), ((1, 0), 1), ((0, 1), 2), ((7, 7), 63), ((8, 0), 64), ((0, 8), 512)):
            r, g, bl, a = rgba[(y * 64 + x) * 4:(y * 64 + x) * 4 + 4]     # z-order inside 8x8 tiles (3dbrew)
            self.assertEqual(struct.unpack_from("<H", b["rgb"], n * 2)[0], (r >> 3) << 11 | (g >> 2) << 5 | bl >> 3,
                             (x, y))
            self.assertEqual(b["alpha"][n // 2] >> (n % 2 * 4) & 0xF, a >> 4, (x, y))
        self.assertEqual(doc_badge(new_data, new_mng, 14)["used"], 0)

    def test_console_files(self):
        data_dir = bs.setting("BADGES_DATA")
        tree = Path(data_dir or "/nonexistent") / "work" / "current" / "000014d1" / "user"
        if not (tree / "BadgeMngFile.dat").is_file():
            self.skipTest("BADGES_DATA not set or without work/current/000014d1")
        sets, unique, total, records = doc_fields((tree / "BadgeMngFile.dat").read_bytes())
        self.assertEqual((sets, unique, total), (3, 288, 288 * 0xFFFF))
        self.assertEqual([(count, start) for _, _, count, _, start in records], [(243, 0), (11, 243), (34, 254)])
        self.assertEqual([copies for _, _, count, copies, _ in records], [c * 0xFFFF for c in (243, 11, 34)])
        cmd = bs.parse((tree / "BadgeData.dat").read_bytes(), (tree / "BadgeMngFile.dat").read_bytes())
        self.assertEqual([(s["set_id"], s["index"], s["unique"], s["total"], s["start"]) for s in cmd["sets"]],
                         records)


class WritePath(unittest.TestCase):
    """decrypt and encrypt with a stand-in for save3ds (fixtures.FAKE_SAVE3DS) on a made-up SD root."""

    @classmethod
    def setUpClass(cls):
        cls.data, cls.mng = fixtures.collection()

    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.tmp)
        self.tree = fixtures.tree(self.tmp / "src" / "000014d1", self.data, self.mng)
        slots = fixtures.slots_tree(self.tmp / "src" / "0000165c", fixtures.slots([self.mng]))
        self.sd, movable = fixtures.fake_sdroot(self.tmp / "card" / "sdroot",
                                                {"000014d1": self.tree, "0000165c": slots})
        fake = fixtures.fake_save3ds(self.tmp / "save3ds")
        patcher = mock.patch.object(bs, "SAVE3DS_SHA256", sha(fake))
        patcher.start()
        self.addCleanup(patcher.stop)
        self.keys = ["--save3ds", fake, "--boot9", self.tmp / "card" / "boot9.bin", "--movable", movable]
        self.dir = self.tmp / "data"

    def decrypt(self, sd, out):
        return run("decrypt", "--sd", sd, "--out", out, *self.keys)

    def encrypt(self, source, tree, out=None):
        return run("encrypt", "--from", source, *(["--out", out] if out else []), *self.keys, tree)

    def test_decrypt_only_reads(self):
        before = bs.listing(self.sd)
        code, out, err = self.decrypt(self.sd, self.tmp / "d")
        self.assertEqual(code, 0, err)
        self.assertEqual(bs.listing(self.tmp / "d" / "000014d1"), bs.listing(self.tree))
        self.assertEqual(bs.listing(self.tmp / "d" / "0000165c"), bs.listing(self.tmp / "src" / "0000165c"))
        self.assertEqual(bs.listing(self.sd), before)
        self.assertEqual(bs.listing(self.tmp / "d" / "sd"), before)
        lines = (self.tmp / "d" / "encrypted.sha256").read_text().splitlines()
        self.assertEqual(len(lines), sum(1 for h in before.values() if h != "dir"))
        self.assertTrue(all(line.split("  ")[1].startswith(("./000014d1/", "./0000165c/")) for line in lines))

    def test_round_trip(self):
        self.assertEqual(self.decrypt(self.sd, self.tmp / "d")[0], 0)
        d = self.tmp / "d"
        new_set = fixtures.badge_set(self.tmp / "sets" / "New", 2)
        self.assertEqual(run("append", "--data", self.dir, "--in", d / "000014d1", new_set)[0], 0)
        appended = d / "000014d1.append-7000"
        source = bs.listing(d / "sd")
        code, out, err = self.encrypt(d, appended)
        self.assertEqual(code, 0, err)
        self.assertIn("rewritten", out)
        sealed = appended.with_name(appended.name + ".encrypted")
        self.assertEqual(list(sealed.rglob("0000165c")), [])
        self.assertEqual(bs.listing(d / "sd"), source)            # the decrypt's encrypted copy is only read
        sums = {}
        for line in (sealed / "encrypted.sha256").read_text().splitlines():
            digest, path = line.split("  ", 1)
            sums[path.removeprefix("./000014d1/")] = digest
        folder = bs.extdata_folder(sealed, "000014d1")
        self.assertEqual({p: h for p, h in bs.listing(folder).items() if h != "dir"}, sums)
        code, _, err = self.decrypt(sealed, self.tmp / "check")
        self.assertEqual(code, 0, err)
        self.assertEqual(bs.listing(self.tmp / "check" / "000014d1"), bs.listing(appended))
        self.assertEqual(self.encrypt(d, d / "000014d1", self.tmp / "same")[0], 0)   # unchanged tree
        self.assertEqual(self.decrypt(self.tmp / "same", self.tmp / "same-check")[0], 0)
        self.assertEqual(bs.listing(self.tmp / "same-check" / "000014d1"), bs.listing(self.tree))

    def test_refusals(self):
        self.assertEqual(self.decrypt(self.sd, self.tmp / "d")[0], 0)
        d = self.tmp / "d"
        cases = []
        extra = self.tmp / "extra"
        shutil.copytree(d / "000014d1", extra)
        (extra / "user" / ".DS_Store").write_bytes(b"x")
        cases.append((self.encrypt(d, extra), "do not hold the same files"))
        icon = self.tmp / "icon"
        shutil.copytree(d / "000014d1", icon)
        (icon / "icon").write_bytes(b"abcd")
        cases.append((self.encrypt(d, icon), "only ['user/BadgeData.dat', 'user/BadgeMngFile.dat'] may differ"))
        broken = self.tmp / "broken"
        shutil.copytree(d / "000014d1", broken)
        mng = bytearray((broken / "user" / "BadgeMngFile.dat").read_bytes())
        mng[0] = 1
        (broken / "user" / "BadgeMngFile.dat").write_bytes(bytes(mng))
        cases.append((self.encrypt(d, broken), "header word 0x0"))
        cases.append((self.encrypt(d, d / "000014d1", d), "exists"))
        with mock.patch.dict("os.environ", {"FAKE_SAVE3DS": "bad-import"}):
            cases.append((self.encrypt(d, d / "000014d1", self.tmp / "bad"), "after the import"))
        with mock.patch.dict("os.environ", {"FAKE_SAVE3DS": "fail-import"}):
            cases.append((self.encrypt(d, d / "000014d1", self.tmp / "fail"), "save3ds --import of 000014d1 failed"))
        other = self.tmp / "other"
        shutil.copytree(d, other)
        data = bytearray((other / "000014d1" / "user" / "BadgeData.dat").read_bytes())
        data[5] ^= 1      # the decrypt no longer matches its encrypted copy
        (other / "000014d1" / "user" / "BadgeData.dat").write_bytes(bytes(data))
        cases.append((self.encrypt(other, other / "000014d1"), "do not belong together"))
        with mock.patch.object(bs, "SAVE3DS_SHA256", "0" * 64):
            cases.append((self.encrypt(d, d / "000014d1", self.tmp / "pin"), "not the save3ds build"))
        twice = self.tmp / "twice"
        shutil.copytree(self.sd, twice)
        shutil.copytree(bs.extdata_folder(twice, "000014d1"),
                        twice / "Nintendo 3DS" / ("0" * 31 + "a") / ("1" * 32) / "extdata" / "00000000" / "000014d1")
        cases.append((self.decrypt(twice, self.tmp / "twice-out"), "exactly one"))
        for (code, _, err), message in cases:
            with self.subTest(message):
                self.assertEqual(code, 2, err)
                self.assertIn(message, err)
        for name in ("bad", "fail", "pin", "twice-out"):
            self.assertFalse((self.tmp / name).exists(), name)
        self.assertEqual([p.name for p in self.tmp.iterdir() if p.name.startswith(".")], [])   # no leftovers


class Manifest(unittest.TestCase):
    def test_ranges_leave_other_tools_alone(self):
        for r in (bs.SET_IDS, bs.BADGE_IDS):
            self.assertGreater(r.start, 1000)          # Anemone3DS, Advanced Badge Editor, GYTB badges
            self.assertLessEqual(r.stop, 0x8000)
        self.assertNotIn(0xEFBE, bs.SET_IDS)           # GYTB's set
        self.assertNotIn(bs.FOLDER_ICON, bs.SET_IDS)

    def test_damaged_manifest_is_refused(self):
        tmp = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, tmp)
        bad = {"format": 1, "next_set_id": 0x7000, "next_badge_id": 0x4000,
               "sets": [{"folder": "A", "name": "A", "set_id": 0x7000, "added": TODAY, "badges": []}]}
        (tmp / "manifest.json").write_text(json.dumps(bad))
        with self.assertRaisesRegex(bs.BadgeError, "IDs never change"):
            bs.load_manifest(tmp)


if __name__ == "__main__":
    unittest.main()
