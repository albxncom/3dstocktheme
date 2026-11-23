"""Tests for the delta path: the installed record, the delta writer, verify-delta, and the import-test application's
delta check (import-test/source/delta.c, compiled for the Mac with host/check.c).

The checks that need makerom and ctrtool (in $THEME_PACK_DATA/bin) or a C compiler are skipped without them.
Run: python3 -I -m unittest discover -s . -p "test_*.py"
"""
import contextlib
import hashlib
import io
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))  # python -I does not add the script directory
import fixtures
import theme_pack as tp

TID = 0x0004008C00009809
APP = HERE / "import-test"


def tool_dir():
    data = tp.setting("THEME_PACK_DATA")
    folder = Path(data) / "bin" if data else None
    if folder and (folder / "makerom").is_file() and (folder / "ctrtool").is_file():
        return folder
    return None


def compile_checker(out_dir):
    """The application's delta check built for the host; None without a C compiler."""
    cc = shutil.which("cc")
    if not cc:
        return None
    exe = Path(out_dir) / "check"
    subprocess.run([cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-O2", "-I", str(APP / "source"),
                    str(APP / "host" / "check.c"), str(APP / "source" / "delta.c"), "-o", str(exe)], check=True)
    return exe


def run_checker(exe, delta_dir, installed_tmd, *not_installed):
    result = subprocess.run([str(exe), str(delta_dir), str(installed_tmd), *map(str, not_installed)],
                            capture_output=True, text=True)
    return result.returncode, result.stdout + result.stderr


def chunk(index, cid, data, optional=True):
    return {"id": cid, "index": index, "type": 0x4000 if optional else 0, "size": len(data),
            "sha256": hashlib.sha256(data).digest()}


def synthetic_tmd(version, chunks, tid=TID):
    """A TMD with the header the builder's TMDs have (issuer, format version 1, title type 0x40, zeros)."""
    base = bytearray(tp.TMD_CHUNKS)
    struct.pack_into(">I", base, 0, 0x00010004)
    base[tp.TMD_BODY:tp.TMD_BODY + 26] = b"Root-CA00000003-CP0000000b"
    base[tp.TMD_BODY + 0x40] = 1
    struct.pack_into(">QI", base, tp.TMD_BODY + 0x4C, tid, 0x40)
    return tp.tmd_with(bytes(base), chunks, version)


def patched(raw, offset, data):
    """raw with data at offset; header bytes outside the hashed info records, so the hash chain still holds."""
    return raw[:offset] + data + raw[offset + len(data):]


class TmdWriter(unittest.TestCase):
    def test_tmd_with_writes_a_consistent_hash_chain(self):
        chunks = [chunk(0, 0x10001, b"c0", False), chunk(1, 1, b"one"), chunk(5, 5, b"five")]
        raw = synthetic_tmd(3, chunks)
        errors = []
        tid, parsed = tp.v_tmd(raw, errors)  # the verify reader, not the writer's
        self.assertEqual(errors, [])
        self.assertEqual(tid, TID)
        self.assertEqual(tp.v_tmd_version(raw), 3)
        self.assertEqual([(c["index"], c["id"], c["size"]) for c in parsed], [(0, 0x10001, 2), (1, 1, 3), (5, 5, 4)])
        self.assertEqual(tp.tmd_parse(raw)["chunks"], chunks)

    def test_content0_ids_of_deltas_never_meet_theme_ids(self):
        self.assertGreater(tp.CONTENT0_DELTA_ID, 0x3FF)  # theme contents keep ID = index, at most 349


@unittest.skipUnless(shutil.which("cc"), "no C compiler")
class ApplicationCheck(unittest.TestCase):
    """delta.c on synthetic deltas: hashes against hashlib, the plan rules, the refusals."""

    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.exe = compile_checker(cls.tmp.name)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def setUp(self):
        self.dir = Path(tempfile.mkdtemp(dir=self.tmp.name))
        self.delta = self.dir / "delta"
        self.delta.mkdir()
        self.data = {0: b"old catalogue" * 100, 1: os.urandom(5000), 2: os.urandom(7000)}
        self.installed = [chunk(0, 0, self.data[0], False), chunk(1, 1, self.data[1]), chunk(2, 2, self.data[2])]
        self.write(self.dir / "installed.tmd", synthetic_tmd(0, self.installed))

    def write(self, path, data):
        Path(path).write_bytes(data)
        return path

    def make_delta(self, new, version=1, content0=b"new catalogue" * 120, content0_id=0x10001, kept=None, tid=TID):
        chunks = [chunk(0, content0_id, content0, False), *(kept if kept is not None else self.installed[1:]),
                  *(chunk(i, i, data) for i, data in sorted(new.items()))]
        self.write(self.delta / "tmd.bin", synthetic_tmd(version, sorted(chunks, key=lambda c: c["index"]), tid))
        self.write(self.delta / "0.app", content0)
        for i, data in new.items():
            self.write(self.delta / f"{i}.app", data)
        return chunks

    def check(self, installed=None, *not_installed):
        return run_checker(self.exe, self.delta, installed or self.dir / "installed.tmd", *not_installed)

    def test_fresh_delta_imports_content_0_then_the_new_ones(self):
        sizes = [0, 1, 55, 56, 63, 64, 65, 100003]  # around the SHA-256 block and padding edges
        self.make_delta({3 + k: os.urandom(n) for k, n in enumerate(sizes)})
        code, out = self.check()
        self.assertEqual(code, 0, out)
        self.assertIn("plan: phase1 yes, phase2 3 4 5 6 7 8 9 10\n", out)
        self.assertEqual(out.count("file OK"), 1 + len(sizes))

    def test_contents_deleted_by_home_do_not_matter(self):
        self.make_delta({3: b"three"})
        code, out = self.check(None, 2)
        self.assertEqual(code, 0, out)
        self.assertIn("phase2 3\n", out)

    def test_resume_after_phase_1_and_nothing_left(self):
        self.make_delta({3: b"three", 4: b"four"})
        new_tmd = self.delta / "tmd.bin"
        code, out = self.check(new_tmd, 4)
        self.assertEqual(code, 0, out)
        self.assertIn("plan: phase1 no, phase2 4\n", out)
        code, out = self.check(new_tmd)
        self.assertEqual(code, 1, out)
        self.assertIn("Nothing to do", out)
        code, out = self.check(new_tmd, 0)
        self.assertEqual(code, 2, out)
        self.assertIn("content 0 is not", out)

    def test_refusals(self):
        cases = {
            "wrong file": (lambda: (self.make_delta({3: b"three"}), self.write(self.delta / "3.app", b"thref")),
                           "WRONG SHA-256"),
            "short file": (lambda: (self.make_delta({3: b"three"}), self.write(self.delta / "3.app", b"thre")),
                           "WRONG SIZE"),
            "other title": (lambda: self.make_delta({3: b"x"}, tid=TID - 1), "this application handles only"),
            "kept record changed": (lambda: self.make_delta({3: b"x"}, kept=[chunk(1, 1, b"other"),
                                                                             self.installed[2]]),
                                    "installed content 1"),
            "kept record dropped": (lambda: self.make_delta({3: b"x"}, kept=[self.installed[2]]), "installed content 1"),
            "replaces an installed content": (lambda: (self.make_delta({3: b"x"}),
                                                       self.write(self.delta / "1.app", self.data[1])),
                                              "would replace content 1"),
            "new content without file": (lambda: (self.make_delta({3: b"x"}), (self.delta / "3.app").unlink()),
                                         "3.app is missing"),
            "content 0 keeps its ID": (lambda: self.make_delta({3: b"x"}, content0_id=0), "content ID 00000000"),
            "version not one up": (lambda: self.make_delta({3: b"x"}, version=2), "made for version 1"),
            "nothing new": (lambda: self.make_delta({}), "adds no theme"),
        }
        for label, (make, message) in cases.items():
            with self.subTest(label):
                shutil.rmtree(self.delta)
                self.delta.mkdir()
                make()
                code, out = self.check()
                self.assertEqual(code, 2, out)
                self.assertIn(message, out)

    def test_header_fields_must_be_this_packs(self):
        body = tp.TMD_BODY
        cases = {
            "format version": (body + 0x40, b"\x02"), "boot content": (body + 0xA1, b"\x01"),
            "issuer": (body + 0x18, b"X"), "title type": (body + 0x57, b"\x41"), "save data size": (body + 0x5A, b"\x01"),
            "access rights": (body + 0x9B, b"\x01"), "signature padding": (0x110, b"\x01"),
        }
        for label, (offset, data) in cases.items():
            with self.subTest(label):
                self.make_delta({3: b"three"})
                tmd = self.delta / "tmd.bin"
                self.write(tmd, patched(tmd.read_bytes(), offset, data))
                code, out = self.check()
                self.assertEqual(code, 2, out)
                self.assertIn(f"TMD header byte {offset:#x}" if offset >= body else "signature padding", out)
        self.make_delta({3: b"three"})
        self.assertEqual(self.check()[0], 0)

    def test_broken_tmd_and_missing_title(self):
        self.make_delta({3: b"three"})
        raw = bytearray((self.delta / "tmd.bin").read_bytes())
        raw[tp.TMD_CHUNKS + 0x30 + 0x20] ^= 1  # a hash byte in content record 1
        self.write(self.delta / "tmd.bin", bytes(raw))
        code, out = self.check()
        self.assertEqual(code, 2, out)
        self.assertIn("does not hash its content records", out)
        self.make_delta({3: b"three"})
        code, out = run_checker(self.exe, self.delta, "-")
        self.assertEqual(code, 2, out)
        self.assertIn("is not installed", out)


@unittest.skipUnless(tool_dir(), "makerom and ctrtool not found in $THEME_PACK_DATA/bin")
class BuildDelta(unittest.TestCase):
    """Real packs in a temporary data dir: n themes installed, one zip added, the delta, its independent checks."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.data = self.root / "data"
        self.bin = str(tool_dir())
        fixtures.write_all(self.data)
        self.assertEqual(self.cli("build")[0], 0)
        self.base = self.root / "installed.cia"
        shutil.copy(self.data / "out" / "theme-pack.cia", self.base)
        self.assertEqual(self.cli("installed", str(self.base))[0], 0)

    def tearDown(self):
        self.tmp.cleanup()

    def cli(self, command, *args, data=None):
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = tp.main([command, *args, "--data", str(data or self.data), "--bin", self.bin])
        return code, out.getvalue(), err.getvalue()

    def add_zip(self, name, title, colour, data=None):
        fixtures.theme_zip((data or self.data) / "themes" / name, title, f"{title} (delta test)", colour, colour,
                           colour, False)

    def ctrtool_extract(self, cia, folder):
        ctrtool = Path(self.bin) / "ctrtool"
        subprocess.run([str(ctrtool), f"--contents={folder / 'c'}", f"--tmd={folder / 'tmd.bin'}", str(cia)],
                       check=True, capture_output=True)
        errors = []
        _, chunks = tp.v_tmd((folder / "tmd.bin").read_bytes(), errors)
        self.assertEqual(errors, [])
        return {c["index"]: (c, (folder / f"c.{c['index']:04x}.{c['id']:08x}").read_bytes()[:c["size"]])
                for c in chunks}

    def test_build_is_deterministic_in_tmd_and_contents(self):
        other = self.root / "other"
        shutil.copytree(self.data / "themes", other / "themes")
        shutil.copy(self.data / "manifest.json", other / "manifest.json")
        self.assertEqual(self.cli("build", data=other)[0], 0)
        self.assertEqual(tp.cia_tmd_and_contents(self.base), tp.cia_tmd_and_contents(other / "out" / "theme-pack.cia"))

    def test_delta_passes_verify_delta_and_equals_a_full_build(self):
        self.add_zip("fixture-green.zip", "Fixture Green", (0x10, 0xC0, 0x10))
        code, out, err = self.cli("delta")
        self.assertEqual(code, 0, err)
        self.assertIn("import [0, 3], keep [1, 2]", out)
        delta = self.data / "out" / "delta"
        self.assertEqual(sorted(p.name for p in delta.iterdir()), ["0.app", "3.app", "delta.json", "tmd.bin"])
        code, out, err = self.cli("verify-delta", "--base-cia", str(self.base))
        self.assertEqual(code, 0, err)
        self.assertIn("version 0 -> 1, import [0, 3], keep [1, 2], kept contents checked", out)

        # What the console holds after the import (installed contents from the installed CIA via ctrtool, plus the
        # delta files) equals a full build of the same n+1 themes, except content 0's ID and the title version.
        full = self.root / "full"
        shutil.copytree(self.data / "themes", full / "themes")
        shutil.copy(self.data / "manifest.json", full / "manifest.json")
        self.assertEqual(self.cli("build", data=full)[0], 0)
        (self.root / "x-base").mkdir()
        (self.root / "x-full").mkdir()
        base = self.ctrtool_extract(self.base, self.root / "x-base")
        rebuilt = self.ctrtool_extract(full / "out" / "theme-pack.cia", self.root / "x-full")
        errors = []
        _, delta_chunks = tp.v_tmd((delta / "tmd.bin").read_bytes(), errors)
        self.assertEqual(errors, [])
        after = {c["index"]: (c, (delta / f"{c['index']}.app").read_bytes() if (delta / f"{c['index']}.app").exists()
                              else base[c["index"]][1]) for c in delta_chunks}
        self.assertEqual(sorted(after), sorted(rebuilt))
        for index, (c, data) in after.items():
            want_record, want_data = rebuilt[index]
            self.assertEqual(data, want_data, f"content {index}")
            self.assertEqual(hashlib.sha256(data).hexdigest(), c["sha256"], f"content {index}")
            self.assertEqual({**c, "id": 0} if index == 0 else c, want_record, f"record {index}")
        self.assertEqual(after[0][0]["id"], 0x10001)
        self.assertEqual(tp.v_tmd_version((delta / "tmd.bin").read_bytes()), 1)

        if shutil.which("cc"):
            exe = compile_checker(self.root)
            code, out = run_checker(exe, delta, self.root / "x-base" / "tmd.bin")
            self.assertEqual(code, 0, out)
            self.assertIn("plan: phase1 yes, phase2 3\n", out)
            code, out = run_checker(exe, delta, self.root / "x-base" / "tmd.bin", 2)  # 2 deleted in HOME
            self.assertEqual(code, 0, out)

    def test_the_record_moves_with_each_delta(self):
        self.add_zip("fixture-green.zip", "Fixture Green", (0x10, 0xC0, 0x10))
        self.assertEqual(self.cli("delta")[0], 0)
        delta = str(self.data / "out" / "delta")
        code, out, err = self.cli("installed", delta)
        self.assertEqual(code, 0, err)
        record = json.loads((self.data / "installed.json").read_text(encoding="utf-8"))
        self.assertEqual((record["title_version"], [c["index"] for c in record["contents"]]), (1, [0, 1, 2, 3]))
        self.assertEqual(record["contents"][0]["id"], "00010001")
        code, _, err = self.cli("installed", delta)  # the same delta twice: its base is no longer the record
        self.assertEqual(code, 1)
        self.assertIn("made for another installed state", err)

        self.add_zip("fixture-yellow.zip", "Fixture Yellow", (0xC0, 0xC0, 0x10))
        code, out, err = self.cli("delta")
        self.assertEqual(code, 0, err)
        self.assertIn("version 1 -> 2, import [0, 4], keep [1, 2, 3]", out)
        info = json.loads((self.data / "out" / "delta" / "delta.json").read_text(encoding="utf-8"))
        self.assertEqual(info["import"][0]["id"], "00010002")
        code, out, err = self.cli("verify-delta")
        self.assertEqual(code, 0, err)
        self.assertIn("version 1 -> 2", out)
        self.assertEqual(json.loads((self.data / "manifest.json").read_text(encoding="utf-8"))["next_index"], 5)

    def test_delta_refusals(self):
        code, _, err = self.cli("delta")
        self.assertEqual(code, 1)
        self.assertIn("nothing to add", err)
        self.add_zip("fixture-green.zip", "Fixture Green", (0x10, 0xC0, 0x10))
        blue = (self.data / "themes" / "fixture-blue.zip").read_bytes()
        (self.data / "themes" / "fixture-blue.zip").unlink()
        code, _, err = self.cli("delta")
        self.assertEqual(code, 1)
        self.assertIn("contents [1] are in the installed pack, but their zips are not in themes/", err)
        (self.data / "themes" / "fixture-blue.zip").write_bytes(blue)
        self.add_zip("fixture-red.zip", "Fixture Red", (0x11, 0x22, 0x33))  # same zip name, other theme
        code, _, err = self.cli("delta")
        self.assertEqual(code, 1)
        self.assertIn("contents [2] built from themes/ differ from the installed ones", err)
        self.assertFalse((self.data / "out" / "delta").exists())
        (self.data / "installed.json").unlink()
        code, _, err = self.cli("delta")
        self.assertEqual(code, 1)
        self.assertIn("record the pack that is on the console first", err)

    def test_installed_refuses_another_title(self):
        other = self.root / "other"
        fixtures.write_all(other)
        self.assertEqual(self.cli("build", "--variation", "4", data=other)[0], 0)
        code, _, err = self.cli("installed", str(other / "out" / "theme-pack.cia"))
        self.assertEqual(code, 1)
        self.assertIn("is title 0004008C00009804, the manifest is 0004008C00009809", err)

    def test_verify_delta_catches_mismatches(self):
        self.add_zip("fixture-green.zip", "Fixture Green", (0x10, 0xC0, 0x10))
        self.assertEqual(self.cli("delta")[0], 0)
        delta = self.data / "out" / "delta"
        saved = {p.name: p.read_bytes() for p in delta.iterdir()}
        tmd = tp.tmd_parse(saved["tmd.bin"])
        kept_changed = [dict(c, size=c["size"] + 0x200) if c["index"] == 1 else c for c in tmd["chunks"]]
        same_id = [dict(c, id=0) if c["index"] == 0 else c for c in tmd["chunks"]]
        cases = {
            "content bytes": (lambda: (delta / "3.app").write_bytes(saved["3.app"][:-1] + b"\x01"), "3.app: size or SHA"),
            "missing file": (lambda: (delta / "0.app").unlink(), "content 0 missing"),
            "kept record": (lambda: (delta / "tmd.bin").write_bytes(tp.tmd_with(saved["tmd.bin"], kept_changed, 1)),
                            "installed content 1 is not carried over unchanged"),
            "content 0 ID": (lambda: (delta / "tmd.bin").write_bytes(tp.tmd_with(saved["tmd.bin"], same_id, 1)),
                             "which an installed content already has"),
            "version": (lambda: (delta / "tmd.bin").write_bytes(tp.tmd_with(saved["tmd.bin"], tmd["chunks"], 0)),
                        "expected the installed 0 + 1"),
            "extra file": (lambda: (delta / "1.app").write_bytes(b"x"), "files in"),
            "format version": (lambda: (delta / "tmd.bin").write_bytes(patched(saved["tmd.bin"], 0x180, b"\x02")),
                               "TMD header differs from the installed one at byte 0x180"),
            "boot content": (lambda: (delta / "tmd.bin").write_bytes(patched(saved["tmd.bin"], 0x1E0, b"\x00\x01")),
                             "TMD header differs from the installed one at byte 0x1e1"),
        }
        def restore():
            for p in delta.iterdir():
                p.unlink()
            for name, data in saved.items():
                (delta / name).write_bytes(data)

        for label, (change, message) in cases.items():
            with self.subTest(label):
                restore()
                change()
                code, _, err = self.cli("verify-delta", "--base-cia", str(self.base))
                self.assertEqual(code, 1)
                self.assertIn(message, err)
        restore()
        self.assertEqual(self.cli("verify-delta", "--base-cia", str(self.base))[0], 0)


if __name__ == "__main__":
    unittest.main()
