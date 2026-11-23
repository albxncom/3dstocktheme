"""End-to-end tests of the C core against the Python builder's independent checks.

Builds packs and deltas with the core (through build/<variant>/tpack) from the fixture zips of
tools/theme-pack/fixtures.py and checks them with `theme_pack.py verify` and `verify-delta` (ctrtool and their
own parsers), compares them with what the Python builder (makerom) makes from the same zips, and feeds the core
malformed zips. Also runs the C unit tests (build/<variant>/test_core, test_app and test_main), and the application's install
path (build/<variant>/test_app install: plan, AM sequence into files, record) on a pack in the state the console is
in after T3, checked by `verify-delta`.

Run: python3 -I host/test_host.py --build build/host     (make test runs it for build/host and build/asan)
Needs makerom and ctrtool: THEME_PACK_BIN, else THEME_PACK_DATA/bin (environment, else the repo's .env). The data
dir is only read; everything is written to a temporary directory.
"""
import argparse
import hashlib
import http.server
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import threading
import unittest
import zipfile
import zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
PACK = ROOT / "tools" / "theme-pack"
THEME_PACK = PACK / "theme_pack.py"
sys.dont_write_bytecode = True     # nothing is written into tools/theme-pack
sys.path.insert(0, str(PACK))
import fixtures  # noqa: E402  (the fixture zips of the Python builder)
import theme_pack  # noqa: E402  (its zip reader, to compare names and flags with the core's)

BUILD = None
BIN = None
SANITIZER = ("AddressSanitizer", "runtime error:", "UndefinedBehaviorSanitizer", "LeakSanitizer")


def setting(name):
    if name in os.environ:
        return os.environ[name]
    env = ROOT / ".env"
    for line in env.read_text(encoding="utf-8").splitlines() if env.is_file() else []:
        key, sep, value = line.partition("=")
        if sep and key.strip() == name:
            return value.strip().strip("\"'")
    return None


def tools_dir():
    if setting("THEME_PACK_BIN"):
        return Path(setting("THEME_PACK_BIN"))
    data = setting("THEME_PACK_DATA")
    return Path(data) / "bin" if data else None


def run(cmd, expect=0):
    result = subprocess.run([str(c) for c in cmd], capture_output=True, text=True)
    for marker in SANITIZER:
        if marker in result.stderr:
            raise AssertionError(f"sanitizer report from {cmd[0]}:\n{result.stderr}")
    if expect is not None and result.returncode != expect:
        raise AssertionError(f"{' '.join(map(str, cmd))} exited {result.returncode}, expected {expect}\n"
                             f"{result.stdout}{result.stderr}")
    return result


def tpack(*args, expect=0):
    return run([BUILD / "tpack", *args], expect)


def python_tool(*args, expect=0):
    return run([sys.executable, "-I", THEME_PACK, *args, "--bin", BIN], expect)


# ---------------------------------------------------------------- data dirs and the manifest

def data_dir(root, name, zips):
    d = root / name
    (d / "themes").mkdir(parents=True)
    (d / "out").mkdir()
    for z in zips:
        shutil.copy(z, d / "themes" / Path(z).name)
    return d


def manifest(d):
    path = d / "manifest.json"
    return json.loads(path.read_text(encoding="utf-8")) if path.is_file() else None


def merge(d, result, reserved=()):
    """manifest.json after a core run: the core's names for the themes it read, new themes appended, as build and
    delta of theme_pack.py would leave it."""
    r = json.loads(result.read_text(encoding="utf-8"))
    m = manifest(d) or {"title_id": r["title_id"], "next_index": 1, "themes": []}
    by_zip = {t["zip"]: t for t in m["themes"]}
    for t in r["themes"]:
        entry = by_zip.get(t["zip"])
        if entry is None:
            entry = {"index": t["index"], "zip": t["zip"], "name": "", "description": "", "added": "2026-11-12"}
            m["themes"].append(entry)
        if entry["index"] != t["index"]:
            raise AssertionError(f"{t['zip']}: the core gave index {t['index']}, the manifest has {entry['index']}")
        if t["present"]:
            entry["name"], entry["description"] = t["name"], t["description"]
    for index, zip_name in reserved:
        if zip_name not in by_zip and all(t["zip"] != zip_name for t in m["themes"]):
            m["themes"].append({"index": index, "zip": zip_name, "name": "", "description": "", "added": "2026-11-12"})
    m["themes"].sort(key=lambda t: t["index"])
    m["next_index"] = r["next_index"]
    (d / "manifest.json").write_text(json.dumps(m, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    return r


def entries(d, new=()):
    m = manifest(d)
    return [f"{t['index']}={t['zip']}" for t in (m["themes"] if m else [])] + list(new)


def c_pack(d, certs, new=(), fixed=(), next_index=None, expect=0):
    m = manifest(d)
    next_index = next_index or (m["next_index"] if m else 1)
    result = d / "result.json"
    out = tpack("pack", "--themes", d / "themes", "--next", next_index, "--certs-from",
                certs, "--cia", d / "out" / "theme-pack.cia", "--result", result, *fixed, *entries(d, new),
                expect=expect)
    return out, (merge(d, result) if expect == 0 else None)


def c_delta(d, new=(), expect=0):
    record = json.loads((d / "installed.json").read_text(encoding="utf-8"))
    (d / "installed-tmd.bin").write_bytes(bytes.fromhex(record["tmd"]))
    shutil.rmtree(d / "out" / "delta", ignore_errors=True)
    result = d / "result.json"
    out = tpack("delta", "--themes", d / "themes", "--installed-tmd", d / "installed-tmd.bin", "--next",
                manifest(d)["next_index"], "--dir", d / "out" / "delta", "--result", result, *entries(d, new),
                expect=expect)
    return out, (merge(d, result) if expect == 0 else None)


# ---------------------------------------------------------------- reading packs (for comparisons only)

def cia_parts(path):
    raw = Path(path).read_bytes()
    hs, cs, ts, ms = struct.unpack_from("<I4xIII", raw, 0)
    al = lambda x: -(-x // 64) * 64
    tik_at = al(hs) + al(cs)
    tmd_at = tik_at + al(ts)
    tmd = raw[tmd_at:tmd_at + ms]
    count = struct.unpack_from(">H", tmd, 0x140 + 0x9E)[0]
    contents, offset = {}, tmd_at + al(ms)
    records = []
    for k in range(count):
        cid, index, ctype, size = struct.unpack_from(">IHHQ", tmd, 0xB04 + 0x30 * k)
        records.append((cid, index, ctype, size))
        contents[index] = raw[offset:offset + size]
        offset += al(size)
    return {"certs": raw[al(hs):al(hs) + cs], "ticket": raw[tik_at:tik_at + ts], "tmd": tmd, "records": records,
            "contents": contents}


def romfs_order(ncch):
    """File names of an NCCH's RomFS in file-table order."""
    romfs = struct.unpack_from("<I", ncch, 0x1B0)[0] * 0x200
    master = struct.unpack_from("<I", ncch, romfs + 8)[0]
    l3 = romfs + -(-(0x60 + master) // 0x1000) * 0x1000
    table, size = struct.unpack_from("<II", ncch, l3 + 0x1C)
    names, p = [], 0
    while p < size:
        name_len = struct.unpack_from("<I", ncch, l3 + table + p + 0x1C)[0]
        names.append(ncch[l3 + table + p + 0x20:l3 + table + p + 0x20 + name_len].decode("utf-16-le"))
        p += 0x20 + -(-name_len // 4) * 4
    return names


NCCH_FIELDS = [   # the NCCH header after its signature: (name, offset, size)
    ("magic", 0x100, 4), ("content size", 0x104, 4), ("partition ID", 0x108, 8), ("maker code", 0x110, 2),
    ("version", 0x112, 2), ("seed check", 0x114, 4), ("program ID", 0x118, 8), ("reserved 0x120", 0x120, 0x10),
    ("logo hash", 0x130, 0x20), ("product code", 0x150, 0x10), ("exheader hash", 0x160, 0x20),
    ("exheader size", 0x180, 4), ("reserved 0x184", 0x184, 4), ("flags", 0x188, 8), ("plain offset", 0x190, 4),
    ("plain size", 0x194, 4), ("logo offset", 0x198, 4), ("logo size", 0x19C, 4), ("ExeFS offset", 0x1A0, 4),
    ("ExeFS size", 0x1A4, 4), ("ExeFS hash size", 0x1A8, 4), ("reserved 0x1AC", 0x1AC, 4), ("RomFS offset", 0x1B0, 4),
    ("RomFS size", 0x1B4, 4), ("RomFS hash size", 0x1B8, 4), ("reserved 0x1BC", 0x1BC, 4), ("ExeFS hash", 0x1C0, 0x20),
    ("RomFS hash", 0x1E0, 0x20),
]


def ncch_fields(ncch):
    return {name: ncch[offset:offset + size] for name, offset, size in NCCH_FIELDS}


def romfs_tables(ncch):
    """What of an NCCH's RomFS does not depend on the order of its files: the IVFC header, the level 3 header,
    the directory tables, and the file table as {(parent, name): size}."""
    romfs = struct.unpack_from("<I", ncch, 0x1B0)[0] * 0x200
    master = struct.unpack_from("<I", ncch, romfs + 8)[0]
    l3 = romfs + -(-(0x60 + master) // 0x1000) * 0x1000
    header = struct.unpack_from("<10I", ncch, l3)
    table, size = header[7], header[8]
    files, p = {}, 0
    while p < size:
        parent, _, _, data_size, _, name_len = struct.unpack_from("<IIQQII", ncch, l3 + table + p)
        name = ncch[l3 + table + p + 0x20:l3 + table + p + 0x20 + name_len].decode("utf-16-le")
        files[(parent, name)] = data_size
        p += 0x20 + -(-name_len // 4) * 4
    return {"ivfc": ncch[romfs:romfs + 0x5C], "level 3 header": header,
            "directories": ncch[l3 + header[1]:l3 + header[3] + header[4]], "files": files}


def extract(cia, out):
    """{index: {relative path: bytes}} of every content's RomFS and ExeFS, through ctrtool."""
    ctrtool = BIN / "ctrtool"
    out.mkdir(parents=True)
    run([ctrtool, f"--contents={out / 'c'}", cia])
    files = {}
    for path in sorted(out.glob("c.*")):
        index = int(path.name.split(".")[1], 16)
        x = out / f"x{index}"
        (x / "exefs").mkdir(parents=True)
        run([ctrtool, f"--romfsdir={x / 'romfs'}", f"--exefsdir={x / 'exefs'}", path])
        files[index] = {str(p.relative_to(x)): p.read_bytes() for p in x.rglob("*") if p.is_file()}
    return files


# ---------------------------------------------------------------- zips made here (synthetic, not themes)

def literal_lz11(size, fill=lambda i: i & 0xFF, byte5=0):
    """An LZ11 stream of exactly `size` compressed bytes: literal tokens only (size >= 13)."""
    groups, rest = divmod(size - 4, 9)
    if rest == 0:
        groups, rest = groups - 1, 9
    literals = groups * 8 + rest - 1
    data = bytearray(fill(i) for i in range(literals))
    if literals > 5:
        data[5] = byte5
    out = bytearray(b"\x11" + literals.to_bytes(3, "little"))
    for g in range(groups + 1):
        chunk = data[8 * g:8 * g + 8]
        out += b"\x00" + chunk
    assert len(out) == size, (len(out), size)
    return bytes(out)


def long_lz11(size, extended=False):
    """An LZ11 stream that decodes to `size` bytes in few tokens: one literal, then copies from 1 back."""
    out = bytearray(b"\x11" + (bytes(3) + size.to_bytes(4, "little") if extended else size.to_bytes(3, "little")))
    tokens, left = [(0, b"A")], size - 1
    while left:
        n = min(left, 0x10110)
        if n >= 0x111:
            v = n - 0x111
            tokens.append((1, bytes([0x10 | v >> 12, v >> 4 & 0xFF, (v & 0xF) << 4, 0])))
        elif n >= 0x11:
            v = n - 0x11
            tokens.append((1, bytes([v >> 4, (v & 0xF) << 4, 0])))
        elif n >= 3:
            tokens.append((1, bytes([(n - 1) << 4, 0])))
        else:
            n = 1
            tokens.append((0, b"A"))
        left -= n
    for g in range(0, len(tokens), 8):
        group = tokens[g:g + 8]
        out.append(sum(0x80 >> i for i, (copy, _) in enumerate(group) if copy))
        out += b"".join(data for _, data in group)
    return bytes(out)


def raw_zip(members, comment=b"", descriptor=False):
    """A zip from (name, data, options) where options may lie: method, csize/size/crc overrides, flags, a
    compressed payload, a different local name."""
    body, central, offset = bytearray(), bytearray(), 0
    for name, data, o in members:
        method = o.get("method", 8)
        payload = o.get("payload")
        if payload is None:
            if method == 8:
                c = zlib.compressobj(9, zlib.DEFLATED, -15)
                payload = c.compress(data) + c.flush()
            else:
                payload = data
        crc = o.get("crc", zlib.crc32(data))
        size, csize = o.get("size", len(data)), o.get("csize", len(payload))
        flags = o.get("flags", 0) | (8 if descriptor else 0)
        local_name = o.get("local_name", name).encode()
        sizes = (0, 0, 0) if descriptor else (crc, csize, size)
        body += struct.pack("<IHHHHHIIIHH", 0x04034B50, 20, flags, method, 0, 0, *sizes, len(local_name), 0)
        body += local_name + payload
        if descriptor:
            body += struct.pack("<IIII", 0x08074B50, crc, csize, size)
        central += struct.pack("<IHHHHHHIIIHHHHHII", 0x02014B50, 20, 20, flags, method, 0, 0, crc, csize, size,
                               len(name.encode()), 0, 0, 0, 0, 0, o.get("local", offset)) + name.encode()
        offset = len(body)
    end = struct.pack("<IHHHHIIH", 0x06054B50, 0, 0, len(members), len(members), len(central), len(body), len(comment))
    return bytes(body + central + end + comment)


def smdh_raw(slots):
    """An SMDH with raw UTF-16LE bytes per slot: {slot: (short, long)}."""
    out = bytearray(fixtures.smdh("x", "x", "x", (1, 2, 3)))
    for i in range(16):
        out[0x8 + 0x200 * i:0x8 + 0x200 * i + 0x180] = bytes(0x180)
    for i, (short, long_) in slots.items():
        out[0x8 + 0x200 * i:0x8 + 0x200 * i + len(short)] = short
        out[0x88 + 0x200 * i:0x88 + 0x200 * i + len(long_)] = long_
    return bytes(out)


def theme_zip(path, name, description, with_bgm, byte5, icon=(0x20, 0xA0, 0x40)):
    body = bytearray(fixtures.body((0x10, 0x80, 0x20), (0x08, 0x40, 0x10)))
    body[5] = byte5
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("body_LZ.bin", fixtures.lz11(bytes(body)))
        z.writestr("info.smdh", fixtures.smdh(name, description, "installer tests", icon))
        if with_bgm:
            z.writestr("bgm.bcstm", fixtures.bgm_stand_in())
        z.writestr("preview.png", b"not a png, ignored")
    return path


class ArtPaths(unittest.TestCase):
    """uikit/art/art.py's SVG path reader: the commands Tabler's icons use, and a clear refusal of anything else."""

    @classmethod
    def setUpClass(cls):
        import importlib.util
        spec = importlib.util.spec_from_file_location("art", ROOT / "uikit" / "art" / "art.py")
        cls.art = importlib.util.module_from_spec(spec)
        sys.modules["art"] = cls.art    # art.py hands itself to the application's half under this name
        argv, sys.argv[1:] = sys.argv[1:], ["--app", str(ROOT / "app" / "ui" / "app_art.py")]   # read when imported
        spec.loader.exec_module(cls.art)
        sys.argv[1:] = argv

    def test_reads_every_command(self):
        lines = self.art.path_lines("M1 1L2 2H3V3C4 4 5 5 6 6S7 7 8 8Q9 9 10 10T11 11A1 1 0 0 1 12 12Z"
                                    "m1 1l1 1h1v1c1 1 1 1 1 1s1 1 1 1q1 1 1 1t1 1a1 1 0 0 1 1 1z")
        self.assertEqual(len(lines), 2)
        self.assertTrue(all(closed for _, closed in lines))

    def test_refuses_with_a_message(self):
        for d in ("M0 0L1 1Z 5 5", "M0 0X1", "M0 0L1", "5 5", "M0 0L1 1#"):
            with self.subTest(d=d), self.assertRaises(ValueError):
                self.art.path_lines(d)


class Core(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = Path(tempfile.mkdtemp(prefix="theme-installer-test-"))
        fx = cls.tmp / "fixtures"
        fixtures.write_all(fx)
        cls.red, cls.blue = fx / "themes" / "fixture-red.zip", fx / "themes" / "fixture-blue.zip"
        cls.green = theme_zip(fx / "fixture-green.zip", "Fixture Green", "BGM stand-in, use-BGM byte on", True, 1)
        cls.violet = theme_zip(fx / "fixture-violet.zip", "Fixture Violet", "use-BGM byte on, no bgm", False, 1)
        cls.long = theme_zip(fx / "fixture-long.zip", "Ünïcødé ✓ " + "Ω" * 40, "€" * 60, False, 0)
        # The Python builder's pack of red and blue: the base for deltas, and the certificate chain tpack copies.
        cls.python = data_dir(cls.tmp, "python", [cls.red, cls.blue])
        python_tool("build", "--data", cls.python)
        cls.base_cia = cls.tmp / "base-python.cia"
        shutil.copy(cls.python / "out" / "theme-pack.cia", cls.base_cia)

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def verify(self, d):
        return python_tool("verify", "--data", d).stdout

    # ------------------------------------------------------------ the C unit tests

    def test_00_unit(self):
        out = run([BUILD / "test_core", self.blue, self.red]).stdout
        self.assertIn(" 0 failed", out)
        print("\n" + out.strip(), file=sys.stderr)

    def test_00_unit_app(self):
        cia = theme_cia(self.tmp / "unit-cia", {"theme.zip": self.blue.read_bytes()})
        out = run([BUILD / "test_app", "unit", self.blue, self.red, cia]).stdout
        self.assertIn(" 0 failed", out)
        print("\n" + out.strip(), file=sys.stderr)

    def test_00_unit_main(self):
        # app/source/main.c with a simulated touch screen, keys and clock: double taps and held touches around the
        # popups, keys pressed over and over (host/test_main.c)
        out = run([BUILD / "test_main", self.blue, self.red]).stdout
        self.assertRegex(out.rstrip().splitlines()[-1], r"^test_main: [1-9][0-9]* checks, 0 failed$", out[-2000:])
        print("\n" + "\n".join(line for line in out.splitlines() if "first gap" in line or "failed" in line),
              file=sys.stderr)

    def test_01_sha256_like_hashlib(self):
        d = self.tmp / "sha"
        d.mkdir()
        files = []
        for size in (0, 1, 55, 56, 63, 64, 65, 119, 120, 4095, 4096, 4097, 100003):
            p = d / f"{size}.bin"
            p.write_bytes(os.urandom(size))
            files.append(p)
        lines = tpack("sha256", *files).stdout.splitlines()
        for line, p in zip(lines, files):
            self.assertEqual(line.split()[0], hashlib.sha256(p.read_bytes()).hexdigest(), p.name)

    # ------------------------------------------------------------ reading zips as theme_pack.py does

    def theme(self, *paths, expect=0):
        return [json.loads(line) for line in tpack("theme", *paths, expect=expect).stdout.splitlines()]

    def test_02_theme_reading_matches_python(self):
        d = self.tmp / "smdh"
        d.mkdir()
        u = lambda s: s.encode("utf-16-le")
        cases = {
            "spaces.zip": {0: (u(" 　 Name with spaces\t "), u("  desc  "))},
            "slot3.zip": {3: (u("Only slot three"), u("")), 5: (u(""), u("Description in slot five"))},
            "lone-high.zip": {0: (b"\x00\xd8A\x00", b"\x00\xd8\x00\x00")},
            "lone-low.zip": {0: (b"\x00\xdcB\x00\x3d\xd8\x00\xde", b"")},
            "pair-at-end.zip": {0: (u("x" * 63) + b"\x3d\xd8", b"")},
            "no-nul.zip": {0: (u("N" * 64), u("D" * 128))},
            "surrogates.zip": {0: (u("\U0001F3A8 art \U0001F600"), u("\U00010348" * 64))},
            "empty.zip": {},
        }
        paths = []
        for name, slots in cases.items():
            with zipfile.ZipFile(d / name, "w", zipfile.ZIP_DEFLATED) as z:
                z.writestr("body_LZ.bin", fixtures.lz11(fixtures.body((1, 2, 3), (4, 5, 6))))
                z.writestr("info.smdh", smdh_raw(slots))
            paths.append(d / name)
        paths += [self.red, self.blue, self.green, self.violet, self.long]
        for got, path in zip(self.theme(*paths), paths):
            want = theme_pack.read_theme(path)
            self.assertEqual(got["result"], "TP_OK", path.name)
            self.assertEqual(got["name"], want["name"], path.name)
            self.assertEqual(got["description"], want["description"], path.name)
            self.assertEqual(got["icon_sha256"], hashlib.sha256(want["icon"]).hexdigest(), path.name)
            self.assertEqual(got["body"], len(want["body"]), path.name)
            self.assertEqual(got["bgm"], None if want["bgm"] is None else len(want["bgm"]), path.name)
            self.assertEqual(got["use_bgm"], theme_pack.lz11_head(want["body"], 6)[5] != 0, path.name)
        by_name = {Path(t["zip"]).name: t for t in self.theme(self.red, self.blue, self.green, self.violet)}
        self.assertEqual(by_name["fixture-red.zip"]["warnings"], 0)
        self.assertEqual(by_name["fixture-blue.zip"]["warnings"], 1)       # bgm.bcstm, byte 0x5 off
        self.assertEqual(by_name["fixture-green.zip"]["warnings"], 0)
        self.assertEqual(by_name["fixture-violet.zip"]["warnings"], 2)     # byte 0x5 on, no bgm.bcstm
        self.assertEqual(self.theme(d / "empty.zip")[0]["name"], "empty")  # the zip name, as theme_pack.py

    # ------------------------------------------------------------ full packs

    def test_03_full_pack_passes_verify_and_matches_python(self):
        d = data_dir(self.tmp, "c-full", [self.red, self.blue])
        out, result = c_pack(d, self.base_cia, new=["fixture-blue.zip", "fixture-red.zip"])
        self.assertLess(result["call_peak_bytes"], 256 * 1024)
        text = self.verify(d)
        self.assertIn("contents [0, 1, 2]", text)
        self.assertEqual([(t["index"], t["zip"], t["name"], t["description"]) for t in manifest(d)["themes"]],
                         [(t["index"], t["zip"], t["name"], t["description"]) for t in manifest(self.python)["themes"]])

        c, py = cia_parts(d / "out" / "theme-pack.cia"), cia_parts(self.base_cia)
        self.assertEqual(c["certs"], py["certs"])
        self.assertEqual([r[:3] for r in c["records"]], [r[:3] for r in py["records"]])   # ID, index, type
        self.assertEqual([r[3] for r in c["records"]], [r[3] for r in py["records"]])     # sizes
        ticket = lambda t: t[:4] + t[0x104:0x1BF] + t[0x1CF:0x1D0] + t[0x1D8:]  # without signature, key, ticket ID
        self.assertEqual(ticket(c["ticket"]), ticket(py["ticket"]))
        tmd_header = lambda t: t[:4] + t[0x104:0x1E4]                       # without signature and hashes
        self.assertEqual(tmd_header(c["tmd"]), tmd_header(py["tmd"]))
        self.assertEqual(sorted(c["contents"]), sorted(py["contents"]))
        for index, ncch in py["contents"].items():
            mine = c["contents"][index]
            self.assertEqual(len(mine), len(ncch), f"content {index}")
            if index:
                # A theme content must be makerom's bytes apart from the signature. makerom takes the file order
                # from the directory listing (bgm.bcstm first on APFS); with another order the bytes cannot match,
                # and that is a failure here, not a skip.
                self.assertEqual(romfs_order(mine), romfs_order(ncch),
                                 f"content {index}: makerom's RomFS order differs, byte comparison impossible")
                self.assertEqual(mine[0x100:], ncch[0x100:], f"content {index} apart from its signature")
            else:
                # Content 0 lists its files in another order than makerom: every NCCH header field but the RomFS
                # hash, the ExeFS, and the order-free parts of the RomFS must be equal; the files are compared
                # after extraction below.
                mine_fields, their_fields = ncch_fields(mine), ncch_fields(ncch)
                for name, _, _ in NCCH_FIELDS:
                    if name != "RomFS hash":
                        self.assertEqual(mine_fields[name], their_fields[name], f"content 0, NCCH header: {name}")
                romfs_at = struct.unpack_from("<I", ncch, 0x1B0)[0] * 0x200
                self.assertEqual(mine[0x200:romfs_at], ncch[0x200:romfs_at], "content 0: ExeFS")
                self.assertEqual(romfs_tables(mine), romfs_tables(ncch), "content 0: RomFS tables")
                self.assertNotEqual(romfs_order(mine), romfs_order(ncch))  # the one known difference
        self.assertEqual(extract(d / "out" / "theme-pack.cia", self.tmp / "x-c-full"),
                         extract(self.base_cia, self.tmp / "x-python-full"))

    def test_04_reserved_index_gap(self):
        d = data_dir(self.tmp, "c-gap", [self.red, self.blue])
        c_pack(d, self.base_cia, fixed=["1=fixture-red.zip", "2=gone.zip", "3=fixture-blue.zip"], next_index=4)
        text = self.verify(d)
        self.assertIn("contents [0, 1, 3]", text)
        self.assertIn("(reserved, not in the pack: gone.zip)", text)
        # The Python builder from the same manifest: the same catalogue, byte for byte.
        p = self.tmp / "python-gap"
        shutil.copytree(d, p)
        python_tool("build", "--data", p)
        self.assertEqual(extract(d / "out" / "theme-pack.cia", self.tmp / "x-c-gap")[0],
                         extract(p / "out" / "theme-pack.cia", self.tmp / "x-python-gap")[0])

    def test_05_bgm_and_size_limits(self):
        big = self.tmp / "big"
        big.mkdir()
        with zipfile.ZipFile(big / "at-limits.zip", "w", zipfile.ZIP_DEFLATED) as z:
            z.writestr("body_LZ.bin", literal_lz11(theme_pack.BODY_MAX, byte5=1))
            z.writestr("bgm.bcstm", b"CSTM" + bytes(theme_pack.BGM_MAX - 4))
            z.writestr("info.smdh", fixtures.smdh("At the limits", "body 0x150000, bgm 0x337000", "tests", (9, 9, 9)))
        d = data_dir(self.tmp, "c-bgm", [self.red, self.green, big / "at-limits.zip"])
        _, result = c_pack(d, self.base_cia, new=["at-limits.zip", "fixture-green.zip", "fixture-red.zip"])
        text = self.verify(d)
        self.assertIn("body 0x150000  bgm 0x337000, flag on", text)
        self.assertIn("Fixture Green  body", text)
        self.assertIn("bgm 0x1000, flag on", text)
        self.assertLess(result["call_peak_bytes"], 256 * 1024)
        print(f"\npeak in the core with the largest theme: {result['peak_bytes']} bytes, one content call "
              f"{result['call_peak_bytes']}", file=sys.stderr)

    def test_06_index_limit(self):
        d = data_dir(self.tmp, "c-349", [self.red])
        c_pack(d, self.base_cia, fixed=["349=fixture-red.zip"], next_index=350)
        self.assertIn("contents [0, 349]", self.verify(d))
        cases = ((["350=fixture-red.zip"], [], 351), ([], ["fixture-red.zip"], 350),
                 (["349=fixture-blue.zip"], ["fixture-red.zip"], 1))
        for n, (fixed, new, next_index) in enumerate(cases):
            e = data_dir(self.tmp, f"c-over-{n}", [self.red, self.blue])
            out, _ = c_pack(e, self.base_cia, new=new, fixed=fixed, next_index=next_index, expect=1)
            self.assertEqual(out.stdout.split(), ["TP_E_INDEX"])

    # ------------------------------------------------------------ deltas

    def check_delta(self, d, base_cia=None, expect_import=None):
        args = ["verify-delta", "--data", d] + (["--base-cia", base_cia] if base_cia else [])
        text = python_tool(*args).stdout
        if expect_import is not None:
            self.assertIn(f"import {expect_import}", text)
        return text

    def test_07_delta_on_python_pack(self):
        d = self.tmp / "delta-python"
        shutil.copytree(self.python, d)
        python_tool("installed", "--data", d, self.base_cia)
        shutil.copy(self.green, d / "themes")
        c_delta(d, new=["fixture-green.zip"])
        self.check_delta(d, self.base_cia, "[0, 3]")
        mine = theme_pack.tmd_parse((d / "out" / "delta" / "tmd.bin").read_bytes())

        # The Python delta of the same state: the same plan, records and sizes.
        p = self.tmp / "delta-python-reference"
        shutil.copytree(self.python, p)
        python_tool("installed", "--data", p, self.base_cia)
        shutil.copy(self.green, p / "themes")
        python_tool("delta", "--data", p)
        theirs = theme_pack.tmd_parse((p / "out" / "delta" / "tmd.bin").read_bytes())
        self.assertEqual(mine["version"], theirs["version"])
        self.assertEqual([(c["id"], c["index"], c["type"], c["size"]) for c in mine["chunks"]],
                         [(c["id"], c["index"], c["type"], c["size"]) for c in theirs["chunks"]])
        self.assertEqual(mine["chunks"][1:3], theirs["chunks"][1:3])        # installed records, byte for byte
        self.assertEqual(sorted(f.name for f in (d / "out" / "delta").iterdir()),
                         sorted(f.name for f in (p / "out" / "delta").iterdir()))

        # theme_pack.py records the C delta as installed; a second delta goes on top of it.
        python_tool("installed", "--data", d, d / "out" / "delta")
        shutil.copy(self.violet, d / "themes")
        out, _ = c_delta(d, new=["fixture-violet.zip"])
        self.assertIn("version 1 -> 2", out.stdout)
        self.assertIn("content ID 00010002", out.stdout)
        self.check_delta(d, None, "[0, 4]")

    def test_08_delta_on_c_pack(self):
        d = data_dir(self.tmp, "delta-c", [self.red, self.blue])
        c_pack(d, self.base_cia, new=["fixture-blue.zip", "fixture-red.zip"])
        base = self.tmp / "base-c.cia"
        shutil.copy(d / "out" / "theme-pack.cia", base)
        python_tool("installed", "--data", d, base)
        shutil.copy(self.green, d / "themes")
        c_delta(d, new=["fixture-green.zip"])
        self.check_delta(d, base, "[0, 3]")

    def test_09_delta_with_gap_and_returning_theme(self):
        d = data_dir(self.tmp, "delta-gap", [self.red, self.blue])
        c_pack(d, self.base_cia, fixed=["1=fixture-red.zip", "2=gone.zip", "3=fixture-blue.zip"], next_index=4)
        base = self.tmp / "base-gap.cia"
        shutil.copy(d / "out" / "theme-pack.cia", base)
        python_tool("installed", "--data", d, base)
        shutil.copy(self.green, d / "themes")
        out, result = c_delta(d, new=["fixture-green.zip"])
        self.assertEqual([t["index"] for t in result["themes"] if t["zip"] == "fixture-green.zip"], [4])
        self.check_delta(d, base, "[0, 4]")
        # The reserved theme comes back under its old index 2, below the installed 3 and 4.
        python_tool("installed", "--data", d, d / "out" / "delta")
        shutil.copy(self.violet, d / "themes" / "gone.zip")
        c_delta(d)
        self.check_delta(d, None, "[0, 2]")

    def test_10_delta_refusals(self):
        d = data_dir(self.tmp, "delta-refuse", [self.red, self.blue])
        c_pack(d, self.base_cia, new=["fixture-blue.zip", "fixture-red.zip"])
        python_tool("installed", "--data", d, d / "out" / "theme-pack.cia")
        out, _ = c_delta(d, expect=1)
        self.assertEqual(out.stdout.split(), ["TP_E_NOTHING"])
        (d / "themes" / "fixture-red.zip").unlink()
        shutil.copy(self.green, d / "themes")
        out, _ = c_delta(d, new=["fixture-green.zip"], expect=1)
        self.assertEqual(out.stdout.split(), ["TP_E_GONE"])

    # ------------------------------------------------------------ hostile zips

    def test_12_without_music(self):
        """tpack nobgm: the content holds only body_LZ.bin, which ctrtool reads and theme_pack's own decoder finds
        equal to the zip's body except byte 0x5, now 0; a zip without bgm.bcstm is refused."""
        def decoded(data):
            size = int.from_bytes(data[1:4], "little") or int.from_bytes(data[4:8], "little")
            return theme_pack.lz11_head(data, size)
        for zip_path, flag in ((self.green, 1), (self.blue, 0)):
            with self.subTest(zip=zip_path.name):
                out = self.tmp / f"nobgm-{zip_path.stem}"
                out.mkdir()
                result = json.loads(tpack("nobgm", zip_path, out / "1.app").stdout)
                self.assertEqual(result["result"], "TP_OK")
                self.assertFalse(result["has_bgm"] or result["use_bgm"] or result["warnings"])
                run([BIN / "ctrtool", f"--romfsdir={out / 'romfs'}", out / "1.app"])
                self.assertEqual(sorted(p.name for p in (out / "romfs").iterdir()), ["body_LZ.bin"])
                body = (out / "romfs" / "body_LZ.bin").read_bytes()
                with zipfile.ZipFile(zip_path) as z:
                    original = bytearray(decoded(z.read("body_LZ.bin")))
                    unchanged = z.read("body_LZ.bin")
                self.assertEqual(original[5], flag)
                if flag:
                    original[5] = 0
                    self.assertEqual(decoded(body), bytes(original))
                else:           # the music was off already: the body is the zip's, byte for byte
                    self.assertEqual(body, unchanged)
        refused = json.loads(tpack("nobgm", self.red, self.tmp / "nobgm-red.app", expect=1).stdout)
        self.assertEqual(refused["result"], "TP_E_ARG")

    def test_11_malformed_zips_are_refused(self):
        body = fixtures.lz11(fixtures.body((1, 2, 3), (4, 5, 6)))
        smdh = fixtures.smdh("Bad", "malformed", "tests", (1, 1, 1))
        good = [("body_LZ.bin", body, {}), ("info.smdh", smdh, {})]
        valid = raw_zip(good)
        deflated = zlib.compressobj(9, zlib.DEFLATED, -15)
        deflated = deflated.compress(body) + deflated.flush()
        cases = {
            "empty": (b"", "TP_E_ZIP"),
            "cut-in-half": (valid[:len(valid) // 2], "TP_E_ZIP"),
            "cut-end-record": (valid[:-10], "TP_E_ZIP"),
            "no-body": (raw_zip(good[1:]), "TP_E_MISSING"),
            "no-smdh": (raw_zip(good[:1]), "TP_E_MISSING"),
            "in-a-folder": (raw_zip([("t/body_LZ.bin", body, {}), ("t/info.smdh", smdh, {})]), "TP_E_MISSING"),
            "deflate-cut": (raw_zip([("body_LZ.bin", body, {"payload": deflated[:len(deflated) // 2]}), good[1]]),
                            "TP_E_ZIP"),
            "longer-than-declared": (raw_zip([("body_LZ.bin", body, {"size": len(body) - 1}), good[1]]), "TP_E_ZIP"),
            "shorter-than-declared": (raw_zip([("body_LZ.bin", body, {"size": len(body) + 1}), good[1]]), "TP_E_ZIP"),
            "bad-crc": (raw_zip([("body_LZ.bin", body, {"crc": zlib.crc32(body) ^ 1}), good[1]]), "TP_E_ZIP"),
            "stored-sizes-differ": (raw_zip([("body_LZ.bin", body, {"method": 0, "size": len(body) + 4}), good[1]]),
                                    "TP_E_ZIP"),
            "bzip2": (raw_zip([("body_LZ.bin", body, {"method": 12}), good[1]]), "TP_E_ZIP"),
            "encrypted": (raw_zip([("body_LZ.bin", body, {"flags": 1}), good[1]]), "TP_E_ZIP"),
            "twice": (raw_zip(good + good[:1]), "TP_E_ZIP"),
            "local-name-differs": (raw_zip([("body_LZ.bin", body, {"local_name": "body_LZ.bix"}), good[1]]),
                                   "TP_E_ZIP"),
            "csize-past-end": (raw_zip([("body_LZ.bin", body, {"csize": 0x7FFFFFF0}), good[1]]), "TP_E_ZIP"),
            "local-past-end": (raw_zip([("body_LZ.bin", body, {"local": 0x7FFFFFF0}), good[1]]), "TP_E_ZIP"),
            "zip64-marker": (raw_zip([("body_LZ.bin", body, {"csize": 0xFFFFFFFF}), good[1]]), "TP_E_ZIP"),
            "not-lz11": (raw_zip([("body_LZ.bin", b"\x10" + body[1:], {}), good[1]]), "TP_E_LZ11"),
            "lz11-cut": (raw_zip([("body_LZ.bin", body[:len(body) - 3], {}), good[1]]), "TP_E_LZ11"),
            "lz11-before-start": (raw_zip([("body_LZ.bin", b"\x11\x08\x00\x00\x40A\x70\x01", {}), good[1]]),
                                  "TP_E_LZ11"),
            "lz11-past-end": (raw_zip([("body_LZ.bin", b"\x11\x09\x00\x00\x20AB\x70\x01", {}), good[1]]),
                              "TP_E_LZ11"),
            "lz11-size-too-small": (raw_zip([("body_LZ.bin", b"\x11\x05\x00\x00\x00abcde", {}), good[1]]),
                                    "TP_E_LZ11"),
            "smdh-short": (raw_zip([good[0], ("info.smdh", smdh[:-1], {})]), "TP_E_SMDH"),
            "smdh-long": (raw_zip([good[0], ("info.smdh", smdh + b"\0", {})]), "TP_E_SMDH"),
            "smdh-magic": (raw_zip([good[0], ("info.smdh", b"SMDX" + smdh[4:], {})]), "TP_E_SMDH"),
            "body-over-limit": (raw_zip([("body_LZ.bin", literal_lz11(theme_pack.BODY_MAX) + b"\0", {}), good[1]]),
                                "TP_E_TOO_BIG"),
            "bgm-over-limit": (raw_zip(good + [("bgm.bcstm", bytes(theme_pack.BGM_MAX + 1), {})]), "TP_E_TOO_BIG"),
            "bgm-declared-huge": (raw_zip(good + [("bgm.bcstm", b"x", {"size": 0xFFFFFF00})]), "TP_E_TOO_BIG"),
            "body-declared-huge": (raw_zip([("body_LZ.bin", body, {"size": 0x7FFFFFFF}), good[1]]), "TP_E_TOO_BIG"),
            "decodes-over-limit": (raw_zip([("body_LZ.bin", long_lz11(theme_pack.BODY_MAX + 1), {}), good[1]]),
                                   "TP_E_TOO_BIG"),
            "declares-4-gib": (raw_zip([("body_LZ.bin", b"\x11\0\0\0\xff\xff\xff\xff" + literal_lz11(0x40000)[4:],
                                         {}), good[1]]), "TP_E_TOO_BIG"),
            # accepted
            "at-limits": (raw_zip([("body_LZ.bin", literal_lz11(theme_pack.BODY_MAX), {}), good[1],
                                   ("bgm.bcstm", bytes(theme_pack.BGM_MAX), {})]), "TP_OK"),
            "comment": (raw_zip(good, comment=b"a plain archive comment"), "TP_OK"),
            "comment-with-signature": (raw_zip(good, comment=b"PK\x05\x06 looks like an end record"), "TP_OK"),
            "data-after-zip": (valid + b"appended", "TP_OK"),
            "data-descriptor": (raw_zip(good, descriptor=True), "TP_OK"),
            "stored": (raw_zip([(n, b, {"method": 0}) for n, b, _ in good]), "TP_OK"),
            "extra-members": (raw_zip([("preview.png", b"x" * 100, {}), *good, ("readme.txt", b"hi", {})]), "TP_OK"),
            "trailing-lz11-padding": (raw_zip([("body_LZ.bin", body + b"\0\0\0", {}), good[1]]), "TP_OK"),
            "decodes-to-the-limit": (raw_zip([("body_LZ.bin", long_lz11(theme_pack.BODY_MAX), {}), good[1]]), "TP_OK"),
            "decodes-to-the-limit-32-bit": (raw_zip([("body_LZ.bin", long_lz11(theme_pack.BODY_MAX, True), {}),
                                                     good[1]]), "TP_OK"),
        }
        d = self.tmp / "malformed"
        d.mkdir()
        for name, (data, want) in cases.items():
            path = d / f"{name}.zip"
            path.write_bytes(data)
            result = tpack("theme", path, expect=0 if want == "TP_OK" else 1)
            self.assertEqual(json.loads(result.stdout)["result"], want, name)


class Installed(unittest.TestCase):
    """The reader of an installed content 0 and the refusals that keep the application on its own title."""

    @classmethod
    def setUpClass(cls):
        cls.tmp = Path(tempfile.mkdtemp(prefix="theme-installer-installed-"))
        fx = cls.tmp / "fixtures"
        fixtures.write_all(fx)
        cls.zips = sorted((fx / "themes").glob("*.zip"))
        cls.long = theme_zip(fx / "fixture-long.zip", "Ünïcødé ✓ " + "Ω" * 40, "€" * 60, False, 0)
        cls.python = data_dir(cls.tmp, "python", cls.zips)
        python_tool("build", "--data", cls.python)
        cls.certs = cls.python / "out" / "theme-pack.cia"

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def read_installed(self, cia, out):
        extract(cia, out)
        romfs = out / "x0" / "romfs"
        listings = []
        for lang in ("en", "es", "fr", "de", "it", "nl", "pt", "ru"):
            text = tpack("installed", romfs / f"ContentInfoArchive_EUR_{lang}.bin", romfs / "icons").stdout
            listings.append([json.loads(line) for line in text.splitlines()])
        self.assertTrue(all(listing == listings[0] for listing in listings))
        return listings[0]

    def check(self, d, listing):
        m = manifest(d)
        self.assertEqual(len(listing) - 1, max(t["index"] for t in m["themes"]))
        self.assertEqual(listing[0], {"index": 0, "enabled": False, "name": "Dummy", "description": "",
                                      "icon_sha256": None})
        grey = hashlib.sha256(struct.pack("<H", 0x8410) * (theme_pack.ICON_SIZE // 2)).hexdigest()
        by_index = {t["index"]: t for t in m["themes"] if (d / "themes" / t["zip"]).is_file()}
        for entry in listing[1:]:
            self.assertTrue(entry["enabled"])
            theme = by_index.get(entry["index"])
            if theme is None:
                self.assertEqual((entry["name"], entry["description"], entry["icon_sha256"]), ("(removed)", "", grey))
                continue
            cut = lambda text, size: theme_pack.utf8_field(text, size).rstrip(b"\0").decode("utf-8")
            self.assertEqual(entry["name"], cut(theme["name"], 0x40))
            self.assertEqual(entry["description"], cut(theme["description"], 0x80))
            icon = theme_pack.read_theme(d / "themes" / theme["zip"])["icon"]
            self.assertEqual(entry["icon_sha256"], hashlib.sha256(icon).hexdigest())

    def test_reads_back_makerom_and_core_catalogues(self):
        for name, builder in (("by-python", "python"), ("by-core", "core")):
            d = data_dir(self.tmp, name, [*self.zips, self.long])
            fixed = ["1=fixture-blue.zip", "2=gone.zip", "3=fixture-long.zip", "4=fixture-red.zip"]
            if builder == "python":
                c_pack(d, self.certs, fixed=fixed, next_index=5)     # writes the manifest; then makerom builds
                python_tool("build", "--data", d)
            else:
                c_pack(d, self.certs, fixed=fixed, next_index=5)
            self.check(d, self.read_installed(d / "out" / "theme-pack.cia", self.tmp / f"x-{name}"))

    def test_only_variation_09(self):
        d = data_dir(self.tmp, "variation-4", self.zips)
        python_tool("build", "--data", d, "--variation", "4")       # a test pack for an official title, never installed
        python_tool("installed", "--data", d, d / "out" / "theme-pack.cia")
        shutil.copy(self.long, d / "themes")
        out, _ = c_delta(d, new=["fixture-long.zip"], expect=1)
        self.assertEqual(out.stdout.split(), ["TP_E_TITLE"])

    def test_an_index_handed_out_once_is_not_given_again(self):
        d = data_dir(self.tmp, "mark", self.zips)
        c_pack(d, self.certs, fixed=["1=fixture-blue.zip", "2=fixture-red.zip"], next_index=4)  # 3 was handed out
        self.assertIn("contents [0, 1, 2]", python_tool("verify", "--data", d).stdout)
        python_tool("installed", "--data", d, d / "out" / "theme-pack.cia")
        shutil.copy(self.long, d / "themes")
        _, result = c_delta(d, new=["fixture-long.zip"])
        self.assertEqual([t["index"] for t in result["themes"] if t["new"]], [4])
        self.assertIn("import [0, 4]", python_tool("verify-delta", "--data", d, "--base-cia",
                                                   d / "out" / "theme-pack.cia").stdout)


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


def theme_cia(where, files):
    """A theme installer CIA made by makerom: one CFA content whose RomFS holds files {name: bytes}."""
    root = where / "root"
    root.mkdir(parents=True)
    for name, data in files.items():
        (root / name).write_bytes(data)
    (where / "cia.rsf").write_text(CIA_RSF.format(root=root), encoding="utf-8")
    run([BIN / "makerom", "-f", "cfa", "-rsf", where / "cia.rsf", "-target", "t", "-o", where / "c0.cfa"])
    run([BIN / "makerom", "-f", "cia", "-target", "t", "-o", where / "theme.cia", "-i", f"{where / 'c0.cfa'}:0:0"])
    return where / "theme.cia"


def cia_layout(raw):
    """Offsets in a CIA: the TMD, its first content record, content 0."""
    al = lambda x: -(-x // 64) * 64
    hs, _, _, cs, ts, ms = struct.unpack_from("<IHHIII", raw, 0)
    tmd = al(hs) + al(cs) + al(ts)
    body = tmd + {0x10003: 0x240, 0x10004: 0x140, 0x10005: 0x80}[struct.unpack_from(">I", raw, tmd)[0]]
    return {"tmd": tmd, "record": body + 0xC4 + 0x900, "content": tmd + al(ms)}


class Cia(unittest.TestCase):
    """The core's reader of theme installer CIAs (tpack cia, and test_core with a CIA for the failure paths and
    the mutation run), on CIAs makerom builds from the fixture zips, and on the user's example if one is set."""

    @classmethod
    def setUpClass(cls):
        cls.tmp = Path(tempfile.mkdtemp(prefix="theme-installer-cia-"))
        fx = cls.tmp / "fixtures"
        fixtures.write_all(fx)
        cls.blue, cls.red = fx / "themes" / "fixture-blue.zip", fx / "themes" / "fixture-red.zip"
        cls.config = b"destination=Themes/fixture-blue.zip\nlocation=theme.zip\n\n"
        cls.cia = theme_cia(cls.tmp / "good", {"config.ini": cls.config, "theme.zip": cls.blue.read_bytes()})

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def read(self, path, expect=0):
        return json.loads(tpack("cia", path, expect=expect).stdout)

    def variant(self, name, files):
        return theme_cia(self.tmp / name, files)

    def patched(self, name, patch):
        raw = bytearray(self.cia.read_bytes())
        patch(raw, cia_layout(raw))
        path = self.tmp / f"{name}.cia"
        path.write_bytes(raw)
        return path

    def test_reads_the_zip_out(self):
        r = self.read(self.cia)
        self.assertEqual((r["kind"], r["result"], r["theme"]), (2, "TP_OK", "TP_OK"))
        self.assertEqual((r["location"], r["destination"]), ("theme.zip", "Themes/fixture-blue.zip"))
        self.assertEqual(r["title_id"], "0004000001111100")
        raw = self.cia.read_bytes()
        self.assertEqual(raw[r["offset"]:r["offset"] + r["size"]], self.blue.read_bytes())
        self.assertEqual(r["name"], theme_pack.read_theme(self.blue)["name"])

    def test_config_ini(self):
        zip_bytes = self.red.read_bytes()
        r = self.read(self.variant("no-config", {"theme.zip": zip_bytes}))
        self.assertEqual((r["result"], r["location"], r["destination"]), ("TP_OK", "theme.zip", ""))
        r = self.read(self.variant("other", {"config.ini": b"\xef\xbb\xbf; comment\r\n[theme]\r\n  LOCATION = Red Theme.zip \r\n"
                                                           b"destination=Th\xc3\xa9mes/x.zip\r\n",
                                             "Red Theme.zip": zip_bytes, "theme.zip": self.blue.read_bytes()}))
        self.assertEqual((r["result"], r["location"], r["destination"]), ("TP_OK", "Red Theme.zip", ""))
        self.assertEqual(r["name"], theme_pack.read_theme(self.red)["name"])
        for name, config in (("missing", b"location=gone.zip\n"), ("path", b"location=../theme.zip\n"),
                             ("subdir", b"location=a/theme.zip\n"), ("empty", b"location=\n"),
                             ("dot", b"location=.theme.zip\n")):
            with self.subTest(config=config):
                r = self.read(self.variant(name, {"config.ini": config, "theme.zip": zip_bytes}), expect=1)
                self.assertEqual(r["result"], "TP_E_CIA")
        r = self.read(self.variant("big-config", {"config.ini": b"#" * 0x1001, "theme.zip": zip_bytes}), expect=1)
        self.assertEqual(r["result"], "TP_E_CIA")
        r = self.read(self.variant("not-a-zip", {"theme.zip": b"<html>challenge</html>"}), expect=1)
        self.assertEqual((r["result"], r["theme"]), ("TP_OK", "TP_E_ZIP"))

    def test_refusals(self):
        def encrypted_record(raw, at):
            raw[at["record"] + 7] |= 1
        def no_nocrypto(raw, at):
            raw[at["content"] + 0x18F] &= ~0x04
        def header_size(raw, at):
            raw[0] = 0x21
        def no_content_0(raw, at):
            raw[0x20] = 0
        def damaged_zip(raw, at):
            raw[-0x200] ^= 0xFF if raw[-0x200] else 1
        def not_ncch(raw, at):
            raw[at["content"] + 0x100] = ord("X")
        def romfs_outside(raw, at):
            struct.pack_into("<I", raw, at["content"] + 0x1B4, 0x7FFFFFFF)
        def ivfc(raw, at):
            off = at["content"] + struct.unpack_from("<I", raw, at["content"] + 0x1B0)[0] * 0x200
            raw[off] = ord("X")
        def record_index(raw, at):
            struct.pack_into(">H", raw, at["record"] + 4, 1)
        cases = (("encrypted-record", encrypted_record, "TP_E_ENCRYPTED"), ("no-nocrypto", no_nocrypto, "TP_E_ENCRYPTED"),
                 ("header-size", header_size, "TP_E_CIA"), ("no-content-0", no_content_0, "TP_E_CIA"),
                 ("not-ncch", not_ncch, "TP_E_CIA"), ("romfs-outside", romfs_outside, "TP_E_CIA"),
                 ("ivfc", ivfc, "TP_E_CIA"), ("record-index", record_index, "TP_E_CIA"))
        for name, patch, want in cases:
            with self.subTest(name):
                self.assertEqual(self.read(self.patched(name, patch), expect=1)["result"], want)
        # a changed byte in content 0 fails its SHA-256 (the reader checks it before the zip is used)
        raw = bytearray(self.cia.read_bytes())
        at = cia_layout(raw)
        r = self.read(self.cia)
        raw[r["offset"] + 40] ^= 0x01
        (self.tmp / "hash.cia").write_bytes(raw)
        self.assertEqual(self.read(self.tmp / "hash.cia", expect=1)["result"], "TP_E_CIA")
        self.assertGreater(at["content"], 0)
        for cut in (1, 0x100, len(raw) // 2):
            (self.tmp / "cut.cia").write_bytes(self.cia.read_bytes()[:-cut])
            self.assertEqual(self.read(self.tmp / "cut.cia", expect=1)["result"], "TP_E_CIA")
        r = self.read(self.blue, expect=1)
        self.assertEqual((r["kind"], r["result"]), (1, "TP_E_CIA"))

    def test_failure_paths_and_mutations(self):
        out = run([BUILD / "test_core", self.blue, self.red, self.cia]).stdout
        self.assertIn(" 0 failed", out)
        self.assertIn("mutations of the theme installer CIA", out)

    def test_users_example(self):
        sample = setting("THEME_SAMPLE_CIA")
        if not sample or not Path(sample).is_file():
            self.skipTest("THEME_SAMPLE_CIA not set (a theme installer CIA on this machine, see README)")
        r = self.read(sample)
        self.assertEqual((r["result"], r["theme"], r["location"]), ("TP_OK", "TP_OK", "theme.zip"))


class Server(http.server.BaseHTTPRequestHandler):
    """Routes for the download tests; everything stays on 127.0.0.1."""
    files = {}
    agents = []
    https_redirects = 0

    def log_message(self, *args):
        pass

    def send(self, status, body=b"", headers=()):
        self.send_response(status)
        for k, v in headers:
            self.send_header(k, v)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        Server.agents.append(self.headers.get("User-Agent"))
        path = self.path.split("?")[0]
        if path == "/theme":
            self.send(200, self.files["zip"], [("Content-Disposition", 'attachment; filename="Fancy Theme.zip"')])
        elif path == "/redirect":
            self.send(302, headers=[("Location", "/theme")])
        elif path == "/redirect-body":
            self.send(302, b"moved " * 400, [("Location", "/theme")])
        elif path == "/loop":
            self.send(302, headers=[("Location", "/loop")])
        elif path == "/to-ftp":
            self.send(302, headers=[("Location", "ftp://127.0.0.1/x.zip")])
        elif path == "/to-https":
            Server.https_redirects += 1
            self.send(302, headers=[("Location", f"https://127.0.0.1:{self.server.server_address[1]}/theme")])
        elif path == "/files/77":
            self.send(200, self.files["cia"])
        elif path == "/page":
            self.send(200, b"<!DOCTYPE html><html>Just a moment...</html>", [("Content-Type", "text/html")])
        elif path == "/damaged.zip":
            self.send(200, b"PK\x03\x04" + bytes(200))
        elif path == "/big":
            self.send_response(200)
            self.send_header("Content-Type", "application/zip")
            self.end_headers()              # no length: the limit has to stop it while it streams
            try:
                for _ in range(17 * 16):
                    self.wfile.write(b"PK\x03\x04" + bytes(0x10000 - 4))
            except (BrokenPipeError, ConnectionResetError):
                pass
        else:
            self.send(404, b"not here")


class Download(unittest.TestCase):
    """download.c through test_app download, against a server on 127.0.0.1: redirects, names, the size limit, the
    kind by content, refusals, cancel; never a partial file left."""

    @classmethod
    def setUpClass(cls):
        cls.tmp = Path(tempfile.mkdtemp(prefix="theme-installer-download-"))
        fx = cls.tmp / "fixtures"
        fixtures.write_all(fx)
        blue = fx / "themes" / "fixture-blue.zip"
        Server.files = {"zip": blue.read_bytes(),
                        "cia": theme_cia(cls.tmp / "cia", {"theme.zip": blue.read_bytes()}).read_bytes()}
        cls.server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Server)
        cls.base = f"http://127.0.0.1:{cls.server.server_address[1]}"
        threading.Thread(target=cls.server.serve_forever, daemon=True).start()

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def setUp(self):
        self.themes = Path(tempfile.mkdtemp(dir=self.tmp, prefix="Themes-"))
        self.part = self.tmp / "download.part"

    def get(self, url, *extra):
        out = run([BUILD / "test_app", "download", url, self.part, self.themes, *extra]).stdout
        r = json.loads(out)
        self.assertEqual(r["part_left"], 0, r)
        return r

    def test_a_zip_through_a_redirect(self):
        Server.agents.clear()
        r = self.get(self.base + "/redirect")
        self.assertEqual((r["status"], r["kind"], r["file"]), (0, 1, "Fancy Theme.zip"))
        self.assertEqual((self.themes / "Fancy Theme.zip").read_bytes(), Server.files["zip"])
        self.assertEqual(Server.agents, ["3DStockTheme/0.0.0-test"] * 2)
        r = self.get(self.base + "/theme")
        self.assertEqual(r["file"], "Fancy Theme (2).zip")

    def test_the_bar_follows_the_final_response(self):
        # a redirect with a body of its own: the bar must not be full before the file itself has arrived
        r = self.get(self.base + "/redirect-body")
        self.assertEqual((r["status"], r["kind"]), (0, 1), r)
        self.assertEqual(r["full_at"], r["bytes"], r)

    def test_the_transfer_measures_itself(self):
        # the numbers of the log's "download:" line (main.c, download_done), where the host can tell them
        r = self.get(self.base + "/redirect")
        self.assertEqual(r["status"], 0, r)
        self.assertTrue(1 <= r["pieces"] and 0 < r["largest"] <= r["bytes"] <= r["pieces"] * r["largest"], r)
        self.assertTrue(all(0 <= r[k] < 30000 for k in ("first_ms", "body_ms", "file_ms")), r)
        self.assertGreater(r["rcvbuf_before"], 0, r)            # the host's sockets answer getsockopt
        self.assertGreaterEqual(r["rcvbuf_after"], r["rcvbuf_before"], r)    # asked for more, never less
        self.assertEqual(r["file_buffer"], 0x40000, r)          # download.c, FILE_BUFFER
        r = self.get(self.base + "/big")                       # stopped by the limit: what it had
        self.assertEqual(r["status"], 6, r)
        self.assertTrue(r["pieces"] >= 1 and r["largest"] > 0 and r["rcvbuf_before"] > 0, r)
        r = self.get("not an address")                           # refused before a socket: nothing read
        self.assertEqual((r["pieces"], r["largest"], r["bytes"], r["rcvbuf_before"], r["rcvbuf_after"],
                          r["file_buffer"]), (0, 0, 0, -1, -1, 0), r)

    def test_a_cia_named_by_its_content(self):
        r = self.get(self.base + "/files/77")
        self.assertEqual((r["status"], r["kind"], r["file"]), (0, 2, "77.cia"))

    def test_refusals_leave_nothing(self):
        for path, status in (("/page", 7), ("/damaged.zip", 8), ("/missing", 5), ("/big", 6), ("/loop", 3),
                             ("/to-ftp", 3)):
            with self.subTest(path):
                r = self.get(self.base + path)
                self.assertEqual(r["status"], status, r)
        self.assertEqual(list(self.themes.iterdir()), [])
        for url, status in (("ftp://127.0.0.1/x.zip", 1), ("file:///etc/hosts", 1), ("not an address", 1),
                            ("https://127.0.0.1:9/x.zip", 2)):       # https without a certificate list on the card
            with self.subTest(url):
                self.assertEqual(self.get(url)["status"], status)
        # without the certificate list on the card (the file named does not exist), a redirect to https is not
        # followed: refused as "no list", and no TLS connection is made
        missing = self.tmp / "no-cacert.pem"
        r = self.get(self.base + "/to-https", missing)
        self.assertEqual((r["status"], r["http"]), (2, 302))
        self.assertEqual(Server.https_redirects, 1)
        self.assertEqual(self.get(self.base + "/theme", "--cancel")["status"], 9)
        self.assertEqual(list(self.themes.iterdir()), [])
        self.part.write_bytes(b"left from a power cut")
        self.assertEqual(self.get(self.base + "/theme")["status"], 0)   # the old part file is replaced


class App(unittest.TestCase):
    """The application's install path (app/source through test_app install) on a makerom-built pack plus one
    imported delta, which is the state of the console after T3 (title version 1, content 0 with ID 0x10001)."""

    @classmethod
    def setUpClass(cls):
        cls.tmp = Path(tempfile.mkdtemp(prefix="theme-installer-app-"))
        fx = cls.tmp / "fixtures"
        fixtures.write_all(fx)
        cls.blue, cls.red = fx / "themes" / "fixture-blue.zip", fx / "themes" / "fixture-red.zip"
        cls.green = theme_zip(fx / "fixture-green.zip", "Fixture Green", "BGM stand-in, use-BGM byte on", True, 1)
        cls.long = theme_zip(fx / "fixture-long.zip", "Ünïcødé ✓ " + "Ω" * 40, "€" * 60, False, 0)
        cls.d = data_dir(cls.tmp, "console", [cls.blue, cls.red])
        python_tool("build", "--data", cls.d)
        cls.base_cia = cls.tmp / "base.cia"
        shutil.copy(cls.d / "out" / "theme-pack.cia", cls.base_cia)
        python_tool("installed", "--data", cls.d, cls.base_cia)
        shutil.copy(cls.green, cls.d / "themes")
        python_tool("delta", "--data", cls.d)
        python_tool("installed", "--data", cls.d, cls.d / "out" / "delta")
        cls.v0_tmd = cls.tmp / "v0-tmd.bin"
        cls.v0_tmd.write_bytes(cia_parts(cls.base_cia)["tmd"])
        cls.v1 = cls.tmp / "v1"
        shutil.move(str(cls.d / "out" / "delta"), cls.v1)
        cls.v1_tmd = cls.v1 / "tmd.bin"
        run([BIN / "ctrtool", f"--romfsdir={cls.tmp / 'content0'}", cls.v1 / "0.app"])
        cls.archive = cls.tmp / "content0" / "ContentInfoArchive_EUR_en.bin"
        cls.icons = cls.tmp / "content0" / "icons"
        cls.manifest = (cls.d / "manifest.json").read_text(encoding="utf-8")
        cls.record = (cls.d / "installed.json").read_text(encoding="utf-8")

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def setUp(self):
        self.store = Path(tempfile.mkdtemp(dir=self.tmp, prefix="store-"))
        shutil.copy(self.v1_tmd, self.store / "installed.tmd")
        self.out = self.d / "out" / "delta"
        shutil.rmtree(self.out, ignore_errors=True)
        self.out.mkdir()
        (self.d / "manifest.json").write_text(self.manifest, encoding="utf-8")
        (self.d / "installed.json").write_text(self.record, encoding="utf-8")
        for extra in (self.long, self.green, self.d / "themes" / "fixture-next.zip",
                      self.d / "themes" / "fixture-quiet-ohne-musik.zip"):
            (self.d / "themes" / Path(extra).name).unlink(missing_ok=True)
        shutil.copy(self.green, self.d / "themes")

    def install(self, zip_path, *extra, expect=0):
        out = run([BUILD / "test_app", "install", "--store", self.store, "--archive", self.archive, "--icons",
                   self.icons, "--zip", zip_path, "--out", self.out, *extra], expect)
        return json.loads(out.stdout)

    def add_to_manifest(self, zip_path, index):
        m = manifest(self.d)
        theme = theme_pack.read_theme(zip_path)
        m["themes"].append({"index": index, "zip": zip_path.name, "name": theme["name"],
                            "description": theme["description"], "added": "2026-11-13"})
        m["next_index"] = index + 1
        (self.d / "manifest.json").write_text(json.dumps(m, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
        shutil.copy(zip_path, self.d / "themes")

    def test_install_passes_verify_delta(self):
        r = self.install(self.long)
        self.assertEqual((r["index"], r["version"], r["done"], r["outcome"], r["calls"] > 8), (4, 2, 1, 0, True))
        self.assertEqual((r["from_staged"], r["record_ok"], r["cancels"]), (0, 1, 0))
        self.assertEqual(sorted(p.name for p in self.out.iterdir()), ["0.app", "4.app", "delta.json", "tmd.bin"])
        self.add_to_manifest(self.long, 4)
        out = python_tool("verify-delta", "--data", self.d).stdout
        self.assertIn("import [0, 4]", out)
        self.assertEqual((self.store / "installed.tmd").read_bytes(), (self.out / "tmd.bin").read_bytes())
        self.assertFalse((self.store / "installed.tmd.new").exists())
        # the installed records are carried over byte for byte from the console's TMD
        old, new = self.v1_tmd.read_bytes(), (self.out / "tmd.bin").read_bytes()
        self.assertEqual(new[0xB04 + 0x30:0xB04 + 0x30 * 4], old[0xB04 + 0x30:0xB04 + 0x30 * 4])
        self.assertEqual(new[:0x140 + 0x9C], old[:0x140 + 0x9C])

    def test_the_mac_adopts_what_the_application_installed(self):
        # The installer's contents are built by the core; theme_pack.py adopt must find them by hash (makerom's
        # content with the signature 0xFF), record the console's TMD and give the zip its index.
        r = self.install(self.long)
        self.assertEqual((r["index"], r["done"]), (4, 1))
        self.assertFalse((self.d / "themes" / self.long.name).exists())
        out = python_tool("adopt", "--data", self.d, "--tmd", self.store / "installed.tmd", self.long).stdout
        self.assertIn("version 1 -> 2", out)
        record = json.loads((self.d / "installed.json").read_text(encoding="utf-8"))
        self.assertEqual(bytes.fromhex(record["tmd"]), (self.store / "installed.tmd").read_bytes())
        self.assertEqual([(t["index"], t["zip"]) for t in manifest(self.d)["themes"]][-1], (4, self.long.name))
        # and the Mac's own delta goes on from there
        shutil.rmtree(self.out)
        theme_zip(self.d / "themes" / "fixture-next.zip", "Fixture Next", "after adopt", False, 0)
        self.assertIn("version 2 -> 3, import [0, 5], keep [1, 2, 3, 4]", python_tool("delta", "--data", self.d).stdout)
        python_tool("verify-delta", "--data", self.d)

    def test_the_mac_adopts_an_install_without_music(self):
        # "Ohne Musik installieren": the content holds only the body, its music byte cleared. The Mac's adopt matches
        # contents by building them from a zip, so it is given a zip of that body (tpack nobgm writes it) and the
        # theme's info.smdh, without bgm.bcstm; with it the console's TMD is recorded as for any install.
        quiet = theme_zip(self.tmp / "fixture-quiet.zip", "Fixture Quiet", "with music", True, 1)
        r = self.install(quiet, "--music", "off")
        self.assertEqual((r["index"], r["done"]), (4, 1))
        run([BIN / "ctrtool", f"--romfsdir={self.tmp / 'quiet-romfs'}", self.out / "4.app"])
        self.assertEqual(sorted(p.name for p in (self.tmp / "quiet-romfs").iterdir()), ["body_LZ.bin"])
        body = self.tmp / "quiet-body.bin"
        tpack("nobgm", quiet, self.tmp / "quiet.app", body)
        self.assertEqual(body.read_bytes(), (self.tmp / "quiet-romfs" / "body_LZ.bin").read_bytes())
        silent = self.tmp / "fixture-quiet-ohne-musik.zip"
        with zipfile.ZipFile(quiet) as src, zipfile.ZipFile(silent, "w", zipfile.ZIP_DEFLATED) as z:
            z.writestr("body_LZ.bin", body.read_bytes())
            z.writestr("info.smdh", src.read("info.smdh"))
        with self.assertRaises(AssertionError):     # the zip as it is matches nothing the console added
            python_tool("adopt", "--data", self.d, "--tmd", self.store / "installed.tmd", quiet)
        out = python_tool("adopt", "--data", self.d, "--tmd", self.store / "installed.tmd", silent).stdout
        self.assertIn("version 1 -> 2", out)
        self.assertEqual([(t["index"], t["zip"]) for t in manifest(self.d)["themes"]][-1], (4, silent.name))

    def test_installed_theme_is_refused_deleted_one_gets_a_new_index(self):
        r = self.install(self.blue, expect=1)
        self.assertEqual((r["plan"], r["duplicate"]), (1, 1))
        self.assertEqual(list(self.out.iterdir()), [])
        r = self.install(self.blue, "--not-usable", "1")          # deleted in HOME: added again, new index
        self.assertEqual((r["index"], r["done"]), (4, 1))

    def test_every_failure_is_cancelled_and_the_record_follows_the_commit(self):
        calls = self.install(self.long)["calls"]
        for n in range(calls):
            with self.subTest(fail=n):
                self.setUp()
                r = self.install(self.long, "--fail", str(n), expect=None)
                self.assertEqual((r["done"], r["cancels"]), (0, 1))
                main_record = (self.store / "installed.tmd").read_bytes()
                staged = self.store / "installed.tmd.new"
                if r["committed1"]:
                    self.assertNotEqual(main_record, self.v1_tmd.read_bytes())
                    self.assertFalse(staged.exists())
                    self.assertEqual(r["outcome"], 4)                 # phase 2 failed, nothing lost
                else:
                    self.assertEqual(main_record, self.v1_tmd.read_bytes())
                    self.assertTrue(staged.exists())
                    self.assertEqual(r["outcome"], 2 if n == 0 else 3)

    def test_no_index_is_handed_out_twice_after_a_recovery(self):
        r = self.install(self.long)
        self.assertEqual((r["index"], r["done"]), (4, 1))
        self.assertEqual((self.store / "installed.mark").read_text(), "4\n")
        # The pack is reinstalled from a CIA without theme 4 and the record seeded from it: AM, the record and the
        # catalogue say 3 again. The mark still says 4.
        shutil.copy(self.v1_tmd, self.store / "installed.tmd")
        r = self.install(self.violet())
        self.assertEqual((r["index"], r["done"]), (5, 1))
        # Without the mark (lost with the card's folder) only the catalogue and the TMD count.
        shutil.copy(self.v1_tmd, self.store / "installed.tmd")
        (self.store / "installed.mark").unlink()
        r = self.install(self.violet())
        self.assertEqual(r["index"], 4)

    def violet(self):
        path = self.tmp / "fixture-violet.zip"
        if not path.exists():
            theme_zip(path, "Fixture Violet", "for the mark test", False, 0, icon=(0x80, 0x20, 0xA0))
        return path

    def test_first_install_is_tpack_pack_and_takes_a_delta(self):
        # 1.0.0-beta.2: on a console without the pack title (an earlier pack's record on the card, which does not
        # count) the application's plan and sequence on files give tpack pack's TMD, content 0 and theme content for
        # the same theme and index byte for byte, and its ticket when the console has none (one found is kept); a
        # second theme then installs as a delta on that result and passes verify-delta against tpack's CIA.
        for mark, ticket in ((0, False), (4, True)):
            with self.subTest(mark=mark, ticket=ticket):
                index = mark + 1
                store = Path(tempfile.mkdtemp(dir=self.tmp, prefix="first-store-"))
                shutil.copy(self.v1_tmd, store / "installed.tmd")
                if mark:
                    (store / "installed.mark").write_text(f"{mark}\n")
                first = Path(tempfile.mkdtemp(dir=self.tmp, prefix="first-out-"))
                r = json.loads(run([BUILD / "test_app", "install", "--no-title", *(["--ticket"] if ticket else []),
                                    "--store", store, "--zip", self.green, "--out", first]).stdout)
                self.assertEqual((r["first"], r["index"], r["top"], r["version"], r["done"], r["outcome"],
                                  r["ticket"], r["cancels"]), (1, index, index, 0, 1, 0, int(not ticket), 0))
                self.assertEqual(sorted(p.name for p in first.iterdir()),
                                 sorted(["0.app", f"{index}.app", "tmd.bin"] + ([] if ticket else ["ticket.bin"])))
                self.assertEqual((store / "installed.tmd").read_bytes(), (first / "tmd.bin").read_bytes())
                self.assertFalse((store / "installed.tmd.new").exists())
                self.assertEqual((store / "installed.mark").read_text(), f"{index}\n")

                d = data_dir(self.tmp, f"first-{index}", [self.green])
                c_pack(d, self.base_cia, new=[self.green.name], next_index=index)
                cia = d / "out" / "theme-pack.cia"
                parts = cia_parts(cia)
                self.assertEqual(sorted(parts["contents"]), [0, index])
                self.assertEqual((first / "tmd.bin").read_bytes(), parts["tmd"])
                self.assertEqual((first / "0.app").read_bytes(), parts["contents"][0])
                self.assertEqual((first / f"{index}.app").read_bytes(), parts["contents"][index])
                if not ticket:
                    self.assertEqual((first / "ticket.bin").read_bytes(), parts["ticket"])

                # the second theme: the record is the first install's, AM's view made from it
                python_tool("installed", "--data", d, cia)
                romfs = self.tmp / f"first-{index}-content0"
                run([BIN / "ctrtool", f"--romfsdir={romfs}", first / "0.app"])
                delta = d / "out" / "delta"
                delta.mkdir()
                r = json.loads(run([BUILD / "test_app", "install", "--store", store, "--archive",
                                    romfs / "ContentInfoArchive_EUR_en.bin", "--icons", romfs / "icons", "--zip",
                                    self.long, "--out", delta]).stdout)
                self.assertEqual((r["first"], r["index"], r["version"], r["done"], r["outcome"], r["ticket"]),
                                 (0, index + 1, 1, 1, 0, 0))
                self.assertEqual(sorted(p.name for p in delta.iterdir()),
                                 ["0.app", f"{index + 1}.app", "delta.json", "tmd.bin"])
                m = manifest(d)
                theme = theme_pack.read_theme(self.long)
                m["themes"].append({"index": index + 1, "zip": self.long.name, "name": theme["name"],
                                    "description": theme["description"], "added": "2026-11-23"})
                m["next_index"] = index + 2
                (d / "manifest.json").write_text(json.dumps(m, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
                shutil.copy(self.long, d / "themes")
                out = python_tool("verify-delta", "--data", d, "--base-cia", cia).stdout
                self.assertIn(f"import [0, {index + 1}]", out)
                self.assertEqual((store / "installed.tmd").read_bytes(), (delta / "tmd.bin").read_bytes())

    def test_a_failed_first_install_is_cancelled_and_leaves_no_title(self):
        # Every importer call failing in turn on a console without the title, the ticket's first: one cancel, the
        # record as for a delta (before the first commit the earlier pack's installed.tmd and the new TMD staged,
        # after it the new TMD), the outcome cancelled or, after the first commit, the pack created without its theme
        def first_install(*extra, expect=0):
            store = Path(tempfile.mkdtemp(dir=self.tmp, prefix="first-fail-"))
            shutil.copy(self.v1_tmd, store / "installed.tmd")
            out = Path(tempfile.mkdtemp(dir=self.tmp, prefix="first-fail-out-"))
            r = run([BUILD / "test_app", "install", "--no-title", "--store", store, "--zip", self.long, "--out", out,
                     *extra], expect)
            return json.loads(r.stdout), store, out

        calls = first_install()[0]["calls"]
        for n in range(calls):
            r, store, out = first_install("--fail", str(n), expect=None)
            with self.subTest(fail=n):
                self.assertEqual((r["first"], r["done"], r["cancels"], r["wrote"]), (1, 0, 1, 1))
                staged = store / "installed.tmd.new"
                if r["committed1"]:
                    self.assertEqual((store / "installed.tmd").read_bytes(), (out / "tmd.bin").read_bytes())
                    self.assertFalse(staged.exists())
                    self.assertEqual(r["outcome"], 4)                 # phase 2 failed, the pack created
                else:
                    self.assertEqual((store / "installed.tmd").read_bytes(), self.v1_tmd.read_bytes())
                    self.assertTrue(staged.exists())
                    self.assertEqual(r["outcome"], 3)                 # cancelled, the ticket's failure too
        self.assertGreater(calls, 10)

    def test_the_record_must_match_am(self):
        shutil.copy(self.v0_tmd, self.store / "installed.tmd")
        r = self.install(self.long, "--am-tmd", self.v1_tmd, expect=1)
        self.assertEqual(r["pack"], 7)                                # PACK_STALE
        self.assertIn("another title version", r["text"])
        shutil.copy(self.v1_tmd, self.store / "installed.tmd.new")    # power cut after a commit
        r = self.install(self.long, "--am-tmd", self.v1_tmd)
        self.assertEqual((r["from_staged"], r["done"], r["index"]), (1, 1, 4))
        for name in ("installed.tmd", "installed.tmd.new"):
            (self.store / name).unlink(missing_ok=True)
        r = self.install(self.long, "--am-tmd", self.v1_tmd, expect=1)
        self.assertEqual(r["pack"], 5)                                # PACK_NO_STORE


def main():
    global BUILD, BIN
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", required=True, help="build/host or build/asan")
    args, rest = parser.parse_known_args()
    BUILD = (ROOT / args.build).resolve()
    BIN = tools_dir()
    if BIN is None or not (BIN / "makerom").is_file() or not (BIN / "ctrtool").is_file():
        sys.exit("makerom and ctrtool not found: set THEME_PACK_BIN, or THEME_PACK_DATA with them in its bin/ "
                 "(environment or the repo's .env); see README, Tests")
    for name in ("tpack", "test_core", "test_app"):
        if not (BUILD / name).is_file():
            sys.exit(f"{BUILD / name} missing: run make host asan")
    print(f"tests with {BUILD.relative_to(ROOT)}", file=sys.stderr)
    unittest.main(argv=[sys.argv[0], *rest], verbosity=2)


if __name__ == "__main__":
    main()
