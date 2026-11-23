"""Tests for adopt: the Mac's records follow what 3DStockTheme (app/, core/) installed on the console.

The console's TMD is made up here the way the installer makes it (content 0 with ID 0x10000 + version, each new
theme with ID = index and the content theme_content gives); that the installer's own contents hash the same is
tested in the application (host/test_host.py, class App, which runs adopt on what its install path wrote).
Needs makerom and ctrtool in $THEME_PACK_DATA/bin; skipped without them.
Run: python3 -I -m unittest discover -s . -p "test_*.py"
"""
import contextlib
import hashlib
import io
import json
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))  # python -I does not add the script directory
import fixtures
import theme_pack as tp

CIA_RSF = """BasicInfo:
  Title: THEME
  CompanyCode: "00"
  ProductCode: CTR-N-THEME
  ContentType: Application
  Logo: None
TitleInfo:
  Category: Application
  UniqueId: 0x11111
Option:
  UseOnSD: true
  EnableCrypt: false
  EnableCompress: false
  MediaFootPadding: false
RomFs:
  RootPath: {root}
"""


def tool_dir():
    data = tp.setting("THEME_PACK_DATA")
    folder = Path(data) / "bin" if data else None
    if folder and (folder / "makerom").is_file() and (folder / "ctrtool").is_file():
        return folder
    return None


@unittest.skipUnless(tool_dir(), "makerom and ctrtool not found in $THEME_PACK_DATA/bin")
class Adopt(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.data = self.root / "data"
        self.card = self.root / "card"
        self.card.mkdir()
        self.bin = tool_dir()
        fixtures.write_all(self.data)
        self.assertEqual(self.cli("build")[0], 0)
        self.assertEqual(self.cli("installed", str(self.data / "out" / "theme-pack.cia"))[0], 0)

    def tearDown(self):
        self.tmp.cleanup()

    def cli(self, command, *args):
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = tp.main([command, *args, "--data", str(self.data), "--bin", str(self.bin)])
        return code, out.getvalue(), err.getvalue()

    def record(self):
        return json.loads((self.data / "installed.json").read_text(encoding="utf-8"))

    def manifest(self):
        return json.loads((self.data / "manifest.json").read_text(encoding="utf-8"))

    def zip_on_card(self, name, title, colour):
        path = self.card / name
        fixtures.theme_zip(path, title, f"{title} (adopt test)", colour, colour, colour, False)
        return path

    def console_tmd(self, zips, first_index, base=None, version=None, name="installed.tmd"):
        """The TMD the installer would leave after adding zips at first_index, first_index + 1, ..."""
        base = base or bytes.fromhex(self.record()["tmd"])
        parsed = tp.tmd_parse(base)
        version = version if version is not None else parsed["version"] + len(zips)
        catalogue = b"catalogue built on the console" * 64
        chunks = [{"id": tp.CONTENT0_DELTA_ID + version, "index": 0, "type": 0, "size": len(catalogue),
                   "sha256": hashlib.sha256(catalogue).digest()}]
        chunks += [c for c in parsed["chunks"] if c["index"]]
        with tempfile.TemporaryDirectory() as tmp:
            for k, z in enumerate(zips):
                content = tp.as_installer(tp.theme_content(self.bin / "makerom", parsed["title_id"],
                                                           tp.read_theme(z), Path(tmp), f"c{k}"))
                chunks.append({"id": first_index + k, "index": first_index + k, "type": 0x4000,
                               "size": len(content), "sha256": hashlib.sha256(content).digest()})
        path = self.card / name
        path.write_bytes(tp.tmd_with(base, sorted(chunks, key=lambda c: c["index"]), version))
        return path

    def test_adopts_one_theme_and_the_mac_can_go_on(self):
        z = self.zip_on_card("Four Arms.zip", "Four Arms", (0x30, 0xA0, 0x30))
        tmd = self.console_tmd([z], 3)
        code, out, err = self.cli("adopt", "--tmd", str(tmd), str(z))
        self.assertEqual(code, 0, err)
        self.assertIn("version 0 -> 1", out)
        record = self.record()
        self.assertEqual((record["title_version"], [c["index"] for c in record["contents"]]), (1, [0, 1, 2, 3]))
        self.assertEqual(bytes.fromhex(record["tmd"]), tmd.read_bytes())
        m = self.manifest()
        self.assertEqual(m["next_index"], 4)
        self.assertEqual([(t["index"], t["zip"], t["name"]) for t in m["themes"]][-1], (3, "Four Arms.zip", "Four Arms"))
        self.assertEqual((self.data / "themes" / "Four Arms.zip").read_bytes(), z.read_bytes())
        # the Mac's next delta builds on the console's state: content 3 (built on the console) is kept
        fixtures.theme_zip(self.data / "themes" / "next.zip", "Next", "after adopt", (9, 9, 9), (9, 9, 9),
                           (9, 9, 9), False)
        code, out, err = self.cli("delta")
        self.assertEqual(code, 0, err)
        self.assertIn("version 1 -> 2, import [0, 4], keep [1, 2, 3]", out)
        code, out, err = self.cli("verify-delta")
        self.assertEqual(code, 0, err)
        # adopting the same record again finds nothing newer
        self.assertEqual(self.cli("adopt", "--tmd", str(tmd), str(z))[0], 1)

    def test_a_theme_installer_cia_counts_as_its_zip(self):
        z = self.zip_on_card("inner.zip", "Inside A CIA", (0xA0, 0x30, 0x30))
        root = self.root / "cia-root"
        root.mkdir()
        shutil.copy(z, root / "theme.zip")
        (root / "config.ini").write_text("destination=Themes/x.zip\nlocation=theme.zip\n", encoding="utf-8")
        (self.root / "cia.rsf").write_text(CIA_RSF.format(root=root), encoding="utf-8")
        tp.run([self.bin / "makerom", "-f", "cfa", "-rsf", self.root / "cia.rsf", "-target", "t", "-o",
                self.root / "c0.cfa"])
        cia = self.card / "Pocket Theme.cia"
        tp.run([self.bin / "makerom", "-f", "cia", "-target", "t", "-o", cia, "-i", f"{self.root / 'c0.cfa'}:0:0"])
        tmd = self.console_tmd([z], 3)
        code, out, err = self.cli("adopt", "--tmd", str(tmd), str(cia))
        self.assertEqual(code, 0, err)
        self.assertEqual((self.data / "themes" / "Pocket Theme.zip").read_bytes(), z.read_bytes())
        self.assertEqual(self.manifest()["themes"][-1]["index"], 3)

    def test_two_installs_at_once_any_order_of_files(self):
        a = self.zip_on_card("a.zip", "Theme A", (1, 2, 3))
        b = self.zip_on_card("b.zip", "Theme B", (3, 2, 1))
        tmd = self.console_tmd([a, b], 5)          # the installer's mark was above the Mac's next index
        code, out, err = self.cli("adopt", "--tmd", str(tmd), str(b), str(a))
        self.assertEqual(code, 0, err)
        m = self.manifest()
        self.assertEqual([(t["index"], t["zip"]) for t in m["themes"]][-2:], [(5, "a.zip"), (6, "b.zip")])
        self.assertEqual(m["next_index"], 7)

    def test_an_index_the_manifest_holds_is_refused(self):
        # The Mac made a delta with a zip at index 3 that was never imported; the console then gave 3 to another
        # theme. Adopting must not put two themes under index 3.
        fixtures.theme_zip(self.data / "themes" / "never-installed.zip", "Never Installed", "a delta not imported",
                           (5, 5, 5), (5, 5, 5), (5, 5, 5), False)
        code, out, err = self.cli("delta")
        self.assertEqual(code, 0, err)
        self.assertIn("import [0, 3]", out)
        a = self.zip_on_card("a.zip", "Theme A", (1, 2, 3))
        tmd = self.console_tmd([a], 3)
        self.refused("--tmd", tmd, a, says="index 3, which the console gave to a theme it installed, is held in "
                                         "manifest.json by never-installed.zip")
        self.assertFalse((self.data / "themes" / "a.zip").exists())

    def refused(self, *args, says):
        before = ((self.data / "manifest.json").read_bytes(), (self.data / "installed.json").read_bytes())
        code, out, err = self.cli("adopt", *map(str, args))
        self.assertEqual(code, 1, out)
        self.assertIn(says, err)
        self.assertEqual(before, ((self.data / "manifest.json").read_bytes(), (self.data / "installed.json").read_bytes()))

    def test_refusals_change_nothing(self):
        a = self.zip_on_card("a.zip", "Theme A", (1, 2, 3))
        b = self.zip_on_card("b.zip", "Theme B", (3, 2, 1))
        tmd = self.console_tmd([a], 3)
        self.refused("--tmd", tmd, b, says="none of the contents the console added")
        self.refused("--tmd", tmd, a, b, says="none of the contents the console added")
        self.refused("--tmd", tmd, a, a, says="are the same theme")
        both = self.console_tmd([a, b], 3, name="both.tmd")
        self.refused("--tmd", both, a, says="have no matching file")
        old = self.data / "themes" / "fixture-red.zip"
        self.refused("--tmd", tmd, old, says="recorded already")
        self.refused("--tmd", self.console_tmd([a], 3, version=0, name="same-version.tmd"), a, says="nothing newer")
        broken = bytearray(tmd.read_bytes())
        broken[-1] ^= 1
        (self.card / "broken.tmd").write_bytes(broken)
        self.refused("--tmd", self.card / "broken.tmd", a, says="hash chain")
        # a TMD that does not follow the record: content 1 changed (the pack was reinstalled from another CIA)
        parsed = tp.tmd_parse(tmd.read_bytes())
        chunks = [dict(c, sha256=bytes(32)) if c["index"] == 1 else c for c in parsed["chunks"]]
        (self.card / "other.tmd").write_bytes(tp.tmd_with(tmd.read_bytes(), chunks, parsed["version"]))
        self.refused("--tmd", self.card / "other.tmd", a, says="not carried over unchanged")
        (self.card / "page.zip").write_bytes(b"<html>a page</html>")
        self.refused("--tmd", tmd, self.card / "page.zip", says="neither a theme zip nor a theme installer CIA")
        shutil.copy(old, self.card / "fixture-red.zip")                  # a name the manifest has, other theme
        shutil.copy(a, self.card / "fixture-red.zip")
        self.refused("--tmd", tmd, self.card / "fixture-red.zip", says="in the manifest already")


if __name__ == "__main__":
    unittest.main()
