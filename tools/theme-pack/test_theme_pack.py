"""Tests for theme_pack.py: manifest rules, the content 0 writer, zip reading, the fixtures, and (when makerom and
ctrtool are available in $THEME_PACK_DATA/bin) a full build and verify in a temporary data dir.

Run: python3 -I -m unittest discover -s . -p "test_*.py"
"""
import ast
import contextlib
import io
import json
import os
import struct
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))  # python -I does not add the script directory
import fixtures
import theme_pack as tp

TODAY = "2026-11-12"


def lz11_decompress(data):
    """Independent of fixtures.lz11: the reference decoder the fixture bodies must pass."""
    assert data[0] == 0x11
    size = int.from_bytes(data[1:4], "little")
    out, pos = bytearray(), 4
    while len(out) < size:
        flags = data[pos]
        pos += 1
        for bit in range(8):
            if len(out) >= size:
                break
            if not flags & 0x80 >> bit:
                out.append(data[pos])
                pos += 1
                continue
            indicator = data[pos] >> 4
            if indicator == 0:
                length = ((data[pos] & 0xF) << 4 | data[pos + 1] >> 4) + 0x11
                disp = ((data[pos + 1] & 0xF) << 8 | data[pos + 2]) + 1
                pos += 3
            elif indicator == 1:
                length = ((data[pos] & 0xF) << 12 | data[pos + 1] << 4 | data[pos + 2] >> 4) + 0x111
                disp = ((data[pos + 2] & 0xF) << 8 | data[pos + 3]) + 1
                pos += 4
            else:
                length = indicator + 1
                disp = ((data[pos] & 0xF) << 8 | data[pos + 1]) + 1
                pos += 2
            for _ in range(length):
                out.append(out[-disp])
    return bytes(out)


class Manifest(unittest.TestCase):
    def test_first_build_numbers_zips_in_name_order_from_one(self):
        m = tp.assign_indices(None, ["b.zip", "a.zip"], 9, TODAY)
        self.assertEqual(m["title_id"], "0004008C00009809")
        self.assertEqual([(t["index"], t["zip"]) for t in m["themes"]], [(1, "a.zip"), (2, "b.zip")])
        self.assertEqual(m["next_index"], 3)

    def test_existing_zips_keep_their_index_and_new_ones_get_the_next(self):
        m = tp.assign_indices(None, ["b.zip", "c.zip"], 9, TODAY)
        m = tp.assign_indices(m, ["a.zip", "b.zip", "c.zip"], 9, TODAY)
        self.assertEqual({t["zip"]: t["index"] for t in m["themes"]}, {"b.zip": 1, "c.zip": 2, "a.zip": 3})

    def test_removed_zip_keeps_its_index_reserved_and_gets_it_back(self):
        m = tp.assign_indices(None, ["a.zip", "b.zip"], 9, TODAY)
        m = tp.assign_indices(m, ["b.zip", "c.zip"], 9, TODAY)
        self.assertEqual({t["zip"]: t["index"] for t in m["themes"]}, {"a.zip": 1, "b.zip": 2, "c.zip": 3})
        m = tp.assign_indices(m, ["a.zip", "c.zip"], 9, TODAY)
        self.assertEqual({t["zip"]: t["index"] for t in m["themes"]}, {"a.zip": 1, "b.zip": 2, "c.zip": 3})

    def test_an_index_is_never_reused_even_if_its_entry_was_deleted_by_hand(self):
        m = tp.assign_indices(None, ["a.zip", "b.zip"], 9, TODAY)
        m["themes"] = [t for t in m["themes"] if t["zip"] != "b.zip"]
        m = tp.assign_indices(m, ["a.zip", "c.zip"], 9, TODAY)
        self.assertEqual({t["zip"]: t["index"] for t in m["themes"]}, {"a.zip": 1, "c.zip": 3})

    def test_input_manifest_is_not_modified(self):
        m = tp.assign_indices(None, ["a.zip"], 9, TODAY)
        before = json.dumps(m)
        tp.assign_indices(m, ["a.zip", "b.zip"], 9, TODAY)
        self.assertEqual(json.dumps(m), before)

    def test_other_variation_than_the_manifest_is_refused(self):
        m = tp.assign_indices(None, ["a.zip"], 9, TODAY)
        with self.assertRaisesRegex(tp.PackError, "belongs to 0004008C00009809"):
            tp.assign_indices(m, ["a.zip"], 8, TODAY)

    def test_only_variations_0_to_9_of_the_eur_theme_title(self):
        self.assertEqual(tp.title_id(0), 0x0004008C00009800)
        self.assertEqual(tp.title_id(9), 0x0004008C00009809)
        for bad in (-1, 10, 0x10, "9", None):
            with self.assertRaises(tp.PackError):
                tp.title_id(bad)

    def test_duplicate_or_out_of_range_index_in_manifest_is_refused(self):
        m = tp.assign_indices(None, ["a.zip", "b.zip"], 9, TODAY)
        m["themes"][1]["index"] = 1
        with self.assertRaisesRegex(tp.PackError, "duplicate"):
            tp.assign_indices(m, ["a.zip"], 9, TODAY)
        m["themes"][1]["index"] = 0
        with self.assertRaisesRegex(tp.PackError, "duplicate or out-of-range"):
            tp.assign_indices(m, ["a.zip"], 9, TODAY)

    def test_no_index_above_349(self):
        self.assertEqual(tp.MAX_INDEX, 349)
        m = tp.assign_indices(None, ["a.zip"], 9, TODAY)
        m["next_index"] = 349
        m = tp.assign_indices(m, ["a.zip", "b.zip"], 9, TODAY)
        self.assertEqual(m["themes"][-1]["index"], 349)
        with self.assertRaisesRegex(tp.PackError, "no content index left for c.zip: the pack is full.*1 to 349"):
            tp.assign_indices(m, ["a.zip", "b.zip", "c.zip"], 9, TODAY)
        m["themes"] = m["themes"][:1]  # even after a zip is dropped and its entry deleted by hand: never reused
        with self.assertRaisesRegex(tp.PackError, "the pack is full"):
            tp.assign_indices(m, ["a.zip", "c.zip"], 9, TODAY)

    def test_manifest_with_an_index_above_349_is_refused(self):
        m = tp.assign_indices(None, ["a.zip", "b.zip"], 9, TODAY)
        m["themes"][1]["index"] = 350
        with self.assertRaisesRegex(tp.PackError, "holds content index 350, above 349"):
            tp.assign_indices(m, ["a.zip", "b.zip"], 9, TODAY)

    def test_verify_flags_indices_above_349(self):
        errors = []
        tp.v_index_limit([0, 1, 349], "TMD", errors)
        self.assertEqual(errors, [])
        tp.v_index_limit([0, 1, 350, 0x3FF], "TMD", errors)
        self.assertEqual(len(errors), 1)
        self.assertIn("TMD: content index 350, 1023 above 349", errors[0])


class Content0(unittest.TestCase):
    # MetaDataContentHeader.bin of an EUR DLC with 25 entries, as printed on 3dbrew "Title list/DLC" (Fire Emblem
    # Awakening, EUR): counts 1/25/25, 0x00300030, magic, EUR region record (2, en), eight EUR languages.
    EUR_EXAMPLE = bytes.fromhex(
        "01000000190000001900000030003000"
        "EFBEADDEFFFFFFFFFFFFFFFF02000100"
        "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF"
        "08000000020001000200050002000200"
        "02000300020004000200080002000900"
        "02000A00")

    def test_meta_header_matches_the_documented_eur_example(self):
        self.assertEqual(tp.meta_header(25), self.EUR_EXAMPLE)

    def test_catalogue_is_positional_with_a_disabled_dummy_first(self):
        cat = tp.content_info_archive({1: ("One", "first"), 3: ("Three", "")})
        self.assertEqual(struct.unpack_from("<2I", cat), (1, 3))
        self.assertEqual(len(cat), 8 + 0xC8 * 4)
        entries = [cat[8 + 0xC8 * i:8 + 0xC8 * (i + 1)] for i in range(4)]
        self.assertEqual([struct.unpack_from("<2I", e, 0xC0) for e in entries], [(0, 0), (1, 1), (2, 1), (3, 1)])
        self.assertEqual(entries[1][:4], b"One\0")
        self.assertEqual(entries[1][0x40:0x46], b"first\0")
        self.assertEqual(entries[2][:10], b"(removed)\0")  # reserved index: listed, but no content to show

    def test_text_is_nul_terminated_utf8_cut_at_a_character_boundary(self):
        self.assertEqual(tp.utf8_field("Café", 8), "Café".encode() + b"\0\0\0")
        cut = tp.utf8_field("é" * 40, 0x40)  # 80 bytes of UTF-8 into a 0x40 field
        self.assertEqual(len(cut), 0x40)
        self.assertEqual(cut[:62].decode("utf-8"), "é" * 31)
        self.assertEqual(cut[62:], b"\0\0")
        self.assertEqual(tp.utf8_field("a" * 100, 0x40)[0x3F:], b"\0")

    def test_write_content0_lays_out_all_files(self):
        icon = bytes(range(256)) * 18
        themes = {1: {"name": "A", "description": "a", "icon": icon}, 2: {"name": "B", "description": "", "icon": icon}}
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp) / "c0"
            tp.write_content0(root, themes, 3)
            files = sorted(str(p.relative_to(root)) for p in root.rglob("*") if p.is_file())
            langs = ["de", "en", "es", "fr", "it", "nl", "pt", "ru"]
            self.assertEqual(files, sorted([*(f"ContentInfoArchive_EUR_{c}.bin" for c in langs),
                                            "MetaDataContentHeader.bin", "icons/1.icn", "icons/2.icn", "icons/3.icn"]))
            archives = {(root / f"ContentInfoArchive_EUR_{c}.bin").read_bytes() for c in langs}
            self.assertEqual(len(archives), 1)
            self.assertEqual((root / "icons/1.icn").read_bytes(), icon)
            self.assertEqual(len((root / "icons/3.icn").read_bytes()), 0x1200)
            self.assertEqual((root / "MetaDataContentHeader.bin").read_bytes(), tp.meta_header(3))

    def test_pack_icon_is_an_smdh(self):
        smdh = tp.pack_smdh()
        self.assertEqual(len(smdh), 0x36C0)
        self.assertEqual(smdh[:4], b"SMDH")
        self.assertEqual(smdh[8:8 + 26].decode("utf-16-le"), "Custom themes")


class ReadTheme(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def zip(self, name, members):
        path = self.dir / name
        with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
            for member, data in members.items():
                z.writestr(member, data)
        return path

    def good(self):
        return {"body_LZ.bin": fixtures.lz11(fixtures.body((1, 2, 3), (4, 5, 6))),
                "info.smdh": fixtures.smdh("Name", "Desc", "Me", (255, 0, 0))}

    def test_fixture_zips_read_back(self):
        paths = fixtures.write_all(self.dir)
        red, blue = self.read_quietly(paths[0])[0], self.read_quietly(paths[1])[0]
        self.assertEqual((red["name"], red["bgm"]), ("Fixture Red", None))
        self.assertEqual(blue["name"], "Fixture Blue")
        self.assertEqual(blue["bgm"], fixtures.bgm_stand_in())
        self.assertEqual(red["icon"], fixtures.rgb565((0xE0, 0x20, 0x20), 48 * 48))
        self.assertTrue(red["description"].startswith("Solid red"))

    def test_name_falls_back_to_the_first_filled_title_slot(self):
        smdh = bytearray(fixtures.smdh("", "", "", (0, 0, 0)))
        smdh[0x8 + 0x200 * 1:0x8 + 0x200 * 1 + 10] = "Slot1".encode("utf-16-le")
        t = tp.read_theme(self.zip("x.zip", {**self.good(), "info.smdh": bytes(smdh)}))
        self.assertEqual(t["name"], "Slot1")
        t = tp.read_theme(self.zip("plain.zip", {**self.good(), "info.smdh": fixtures.smdh("", "", "", (0, 0, 0))}))
        self.assertEqual(t["name"], "plain")

    def test_refuses_broken_zips(self):
        good = self.good()
        cases = {
            "no body": ({"info.smdh": good["info.smdh"]}, "must be at the zip root"),
            "no smdh": ({"body_LZ.bin": good["body_LZ.bin"]}, "must be at the zip root"),
            "body in a folder": ({"sub/body_LZ.bin": good["body_LZ.bin"], "info.smdh": good["info.smdh"]},
                                 "must be at the zip root"),
            "not lz11": ({**good, "body_LZ.bin": b"\x10abc"}, "not LZ11"),
            "short smdh": ({**good, "info.smdh": b"SMDH" + bytes(100)}, "not an SMDH"),
            "body too big": ({**good, "body_LZ.bin": b"\x11" + bytes(0x150000)}, "limit is 0x150000"),
            "body decodes too big": ({**good, "body_LZ.bin": b"\x11\x01\x00\x15" + bytes(9)},
                                     "decompresses to 0x150001 bytes, the limit is 0x150000"),
            "body declares 4 GiB": ({**good, "body_LZ.bin": b"\x11\0\0\0\xff\xff\xff\xff" + bytes(0x40000)},
                                    "decompresses to 0xffffffff bytes, the limit is 0x150000"),
            "bgm too big": ({**good, "bgm.bcstm": bytes(0x337001)}, "limit is 0x337000"),
        }
        for label, (members, message) in cases.items():
            with self.subTest(label), self.assertRaisesRegex(tp.PackError, message):
                tp.read_theme(self.zip(label.replace(" ", "-") + ".zip", members))
        (self.dir / "junk.zip").write_bytes(b"not a zip")
        with self.assertRaisesRegex(tp.PackError, "junk.zip"):
            tp.read_theme(self.dir / "junk.zip")

    def body_with_flag(self, flag):
        raw = bytearray(fixtures.body((1, 2, 3), (4, 5, 6)))
        raw[0x5] = flag
        return fixtures.lz11(bytes(raw))

    def read_quietly(self, path):
        err = io.StringIO()
        with contextlib.redirect_stderr(err):
            theme = tp.read_theme(path)
        return theme, err.getvalue()

    def test_bgm_without_the_use_bgm_flag_warns(self):
        theme, err = self.read_quietly(fixtures.write_all(self.dir)[1])  # blue: BGM stand-in, flag off
        self.assertIn("fixture-blue.zip: has bgm.bcstm, but the body's use-BGM byte (0x5) is off", err)
        self.assertIn("HOME will not play the music", err)
        self.assertIsNotNone(theme["bgm"])

    def test_use_bgm_flag_without_bgm_warns(self):
        body = self.body_with_flag(1)
        theme, err = self.read_quietly(self.zip("flag.zip", {**self.good(), "body_LZ.bin": body}))
        self.assertIn("flag.zip: the body's use-BGM byte (0x5) is on, but there is no bgm.bcstm", err)
        self.assertEqual(theme["body"], body)  # never changed

    def test_consistent_bgm_flag_is_quiet(self):
        self.assertEqual(self.read_quietly(fixtures.write_all(self.dir)[0])[1], "")  # red: no BGM, flag off
        with_bgm = {**self.good(), "body_LZ.bin": self.body_with_flag(1), "bgm.bcstm": fixtures.bgm_stand_in()}
        self.assertEqual(self.read_quietly(self.zip("music.zip", with_bgm))[1], "")

    def test_flag_is_read_through_back_references(self):
        raw = bytearray(0x100)
        raw[0] = 1
        raw[0x5] = 7  # reached through a copy token: the zeros before it compress into a back-reference
        self.assertEqual(tp.lz11_head(fixtures.lz11(bytes(raw)), 6), bytes(raw[:6]))
        self.assertEqual(tp.v_body_byte(fixtures.lz11(bytes(raw)), 5), 7)
        with self.assertRaisesRegex(tp.PackError, "not valid LZ11"):
            tp.lz11_head(b"\x11\x00\x01\x00\x80\x20\x00", 6)  # copy from before the start

    def test_limits_are_inclusive(self):
        members = {**self.good(), "bgm.bcstm": bytes(0x337000)}
        members["body_LZ.bin"] = members["body_LZ.bin"].ljust(0x150000, b"\0")
        t = self.read_quietly(self.zip("max.zip", members))[0]
        self.assertEqual((len(t["body"]), len(t["bgm"])), (0x150000, 0x337000))
        for header in (b"\x11\x00\x00\x15", b"\x11\0\0\0\x00\x00\x15\x00"):   # decodes to 0x150000, 24 and 32 bit
            members["body_LZ.bin"] = header + bytes(9)
            self.read_quietly(self.zip("max-decoded.zip", members))

    def test_verify_flags_a_body_that_decodes_over_the_limit(self):
        romfs = self.dir / "x1" / "romfs"
        romfs.mkdir(parents=True)
        for header, flagged in ((b"\x11\x01\x00\x15", True), (b"\x11\x00\x00\x15", False),
                                (b"\x11\0\0\0\x01\x00\x15\x00", True), (b"\x11\0\0\0\xff\xff\xff\xff", True)):
            body = header + bytes(9)
            (romfs / "body_LZ.bin").write_bytes(body)
            members = {**self.good(), "body_LZ.bin": body}
            path = self.zip("source.zip", members)
            errors = []
            tp.v_theme(self.dir / "x1", {"index": 1}, path, None, errors)
            self.assertEqual(any("decodes to" in e and "limit 0x150000" in e for e in errors), flagged, (header, errors))


class Fixtures(unittest.TestCase):
    def test_lz11_round_trip(self):
        for data in (b"", b"a", bytes(5000), bytes(range(256)) * 3, b"ab" * 40000, os.urandom(3000)):
            with self.subTest(len(data)):
                self.assertEqual(lz11_decompress(fixtures.lz11(data)), data)

    def test_body_follows_the_documented_constraints(self):
        raw = fixtures.body((0xC0, 0x10, 0x10), (0x60, 0x08, 0x08))
        word = lambda o: struct.unpack_from("<I", raw, o)[0]
        self.assertEqual(word(0), 1)                                     # version must be 1
        self.assertEqual(raw[5], 0)                                      # never play the BGM stand-in
        self.assertEqual(len(raw) % 0x10, 0)
        self.assertGreaterEqual(len(raw), 0xD0)
        self.assertEqual((word(0x0C), word(0x20)), (1, 1))               # solid colour, both screens
        for flag in (0x2C, 0x34, 0x3C, 0x48, 0x50, 0x5C, 0x64, 0x6C, 0x78, 0x80, 0x88, 0x90, 0x98, 0xA0, 0xA8,
                     0xB0, 0xB8):
            self.assertLess(word(flag), 2, hex(flag))
        self.assertEqual({o for o in range(0x2C, 0xBC, 4) if word(o)}, {0x80, 0x84, 0x88, 0x8C})
        for offset, size in ((word(0x14), 5), (word(0x84), 0xD), (word(0x8C), 9)):
            self.assertLessEqual(offset + size, len(raw))
            self.assertGreaterEqual(offset, 0xD0)
        self.assertEqual(raw[word(0x14):word(0x14) + 3], bytes((0xC0, 0x10, 0x10)))

    def test_smdh_layout(self):
        s = fixtures.smdh("N", "D", "A", (0, 0, 255))
        self.assertEqual((len(s), s[:4]), (0x36C0, b"SMDH"))
        for slot in range(16):
            base = 0x8 + 0x200 * slot
            self.assertEqual(s[base:base + 4], "N\0".encode("utf-16-le"))
            self.assertEqual(s[base + 0x80:base + 0x82], "D".encode("utf-16-le"))
            self.assertEqual(s[base + 0x180:base + 0x182], "A".encode("utf-16-le"))
        self.assertEqual(s[0x24C0:0x24C2], struct.pack("<H", 0x001F))

    def test_fixtures_differ(self):
        bodies = {fixtures.lz11(fixtures.body(top, bottom)) for _, _, _, top, bottom, _, _ in fixtures.FIXTURES}
        self.assertEqual(len(bodies), len(fixtures.FIXTURES))


class VerifyIsIndependent(unittest.TestCase):
    def test_verify_section_uses_no_build_code(self):
        source = (HERE / "theme_pack.py").read_text(encoding="utf-8")
        build_part, verify_part = source.split("# ---------------------------------------------------------------- verify")
        verify_part = verify_part.split("# ---------------------------------------------------------------- command")[0]
        tree = ast.parse(build_part)
        build_names = {n.name for n in tree.body if isinstance(n, (ast.FunctionDef, ast.ClassDef))}
        build_names |= {t.id for n in tree.body if isinstance(n, ast.Assign) for t in n.targets if isinstance(t, ast.Name)}
        allowed = {"setting", "tool", "run", "PackError"}
        used = {n.id for n in ast.walk(ast.parse(verify_part)) if isinstance(n, ast.Name)}
        self.assertEqual(used & build_names - allowed, set())


def tool_dir():
    data = tp.setting("THEME_PACK_DATA")
    folder = Path(data) / "bin" if data else None
    if folder and (folder / "makerom").is_file() and (folder / "ctrtool").is_file():
        return folder
    return None


@unittest.skipUnless(tool_dir(), "makerom and ctrtool not found in $THEME_PACK_DATA/bin")
class BuildAndVerify(unittest.TestCase):
    """A real build and verify in a temporary data dir; the data dir named by THEME_PACK_DATA is only read (bin/)."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.data = Path(self.tmp.name)
        self.bin = str(tool_dir())
        fixtures.write_all(self.data)

    def tearDown(self):
        self.tmp.cleanup()

    def cli(self, *args):
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = tp.main([*args, "--data", str(self.data), "--bin", self.bin])
        return code, out.getvalue(), err.getvalue()

    def manifest(self):
        return json.loads((self.data / "manifest.json").read_text(encoding="utf-8"))

    def test_build_then_verify(self):
        code, out, err = self.cli("build")
        self.assertEqual(code, 0, err)
        self.assertTrue((self.data / "out" / "theme-pack.cia").is_file())
        self.assertIn("   1  Fixture Blue", out)
        self.assertIn("   2  Fixture Red", out)
        code, out, err = self.cli("verify")
        self.assertEqual(code, 0, err)
        lines = out.splitlines()
        self.assertEqual(lines[0], "   1  Fixture Blue  body 0x34  bgm 0x1000, flag off")
        self.assertEqual(lines[1], "   2  Fixture Red  body 0x34  no bgm, flag off")
        self.assertTrue(lines[2].startswith("OK ") and "0004008C00009809" in lines[2] and "[0, 1, 2]" in lines[2])

    def test_rebuild_keeps_indices_and_reserves_removed_themes(self):
        self.assertEqual(self.cli("build")[0], 0)
        fixtures.theme_zip(self.data / "themes" / "aaa.zip", "Third", "", (0, 0x80, 0), (0, 0x40, 0), (0, 0xFF, 0),
                           False)
        (self.data / "themes" / "fixture-blue.zip").unlink()
        code, out, err = self.cli("build")
        self.assertEqual(code, 0, err)
        self.assertEqual({t["zip"]: t["index"] for t in self.manifest()["themes"]},
                         {"fixture-blue.zip": 1, "fixture-red.zip": 2, "aaa.zip": 3})
        self.assertIn("(reserved: fixture-blue.zip", out)
        code, out, err = self.cli("verify")
        self.assertEqual(code, 0, err)
        self.assertIn("contents [0, 2, 3]", out)
        self.assertIn("   1  (reserved", out)

    def test_verify_catches_mismatches(self):
        self.assertEqual(self.cli("build")[0], 0)
        cases = []
        m = self.manifest()
        m["themes"][0]["name"] = "Someone else"
        cases.append(("name", lambda: (self.data / "manifest.json").write_text(json.dumps(m)), "catalogue entry 1"))
        cases.append(("new zip", lambda: fixtures.theme_zip(self.data / "themes" / "new.zip", "New", "", (1, 1, 1),
                                                            (1, 1, 1), (1, 1, 1), False), "not in the manifest"))
        cases.append(("changed body", lambda: fixtures.theme_zip(self.data / "themes" / "fixture-red.zip", "Fixture Red",
                                                                 "Solid red test theme (theme_pack fixture)",
                                                                 (1, 2, 3), (4, 5, 6), (0xE0, 0x20, 0x20), False),
                      "body_LZ.bin differs"))
        original = (self.data / "manifest.json").read_text(encoding="utf-8")
        red = (self.data / "themes" / "fixture-red.zip").read_bytes()
        for label, change, message in cases:
            with self.subTest(label):
                change()
                code, out, err = self.cli("verify")
                self.assertEqual(code, 1)
                self.assertIn(message, err)
                (self.data / "manifest.json").write_text(original, encoding="utf-8")
                (self.data / "themes" / "fixture-red.zip").write_bytes(red)
                (self.data / "themes" / "new.zip").unlink(missing_ok=True)
        self.assertEqual(self.cli("verify")[0], 0)

    def test_verify_fails_on_a_manifest_index_above_349(self):
        self.assertEqual(self.cli("build")[0], 0)
        m = self.manifest()
        m["themes"][1]["index"] = 350
        (self.data / "manifest.json").write_text(json.dumps(m), encoding="utf-8")
        code, _, err = self.cli("verify")
        self.assertEqual(code, 1)
        self.assertIn("manifest: content index 350 above 349", err)
        code, _, err = self.cli("build")
        self.assertEqual(code, 1)
        self.assertIn("above 349", err)

    def test_variation_flag(self):
        code, _, err = self.cli("build", "--variation", "10")
        self.assertEqual(code, 1)
        self.assertIn("0 to 9", err)
        self.assertEqual(self.cli("build", "--variation", "4")[0], 0)
        self.assertEqual(self.manifest()["title_id"], "0004008C00009804")
        code, _, err = self.cli("build", "--variation", "9")
        self.assertEqual(code, 1)
        self.assertIn("belongs to 0004008C00009804", err)
        code, out, err = self.cli("verify")
        self.assertEqual(code, 0, err)
        self.assertIn("0004008C00009804", out)

    def test_no_zips_is_an_error(self):
        for p in (self.data / "themes").glob("*.zip"):
            p.unlink()
        code, _, err = self.cli("build")
        self.assertEqual(code, 1)
        self.assertIn("no theme zips", err)


if __name__ == "__main__":
    unittest.main()
