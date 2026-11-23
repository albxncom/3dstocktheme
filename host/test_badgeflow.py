"""The badge download (app/source/badge_download.c) through test_badgeflow download, against a server on 127.0.0.1:
a zip and a PNG kept under their names, a redirect, and every refusal leaving nothing on the card. The PNGs and zips
are generated here; no badge art from anywhere.

Run: python3 -I host/test_badgeflow.py --build build/host     (make test runs it for build/host and build/asan)
"""
import argparse
import http.server
import io
import json
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

BUILD = Path("build/host")

DL_OK, DL_NO_CA, DL_HTTP, DL_NOT_THEME, DL_BAD_THEME = 0, 2, 5, 7, 8
KIND_ZIP, KIND_PNG = 1, 2


def png(width, height, seed):
    """An RGBA PNG of generated pixels."""
    rows = b"".join(b"\0" + bytes((x * 7 + y * 13 + seed * 31 + c * 50) % 256 for x in range(width)
                                  for c in range(4)) for y in range(height))

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))

    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))


def zipped(members):
    out = io.BytesIO()
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        for name, data in members.items():
            z.writestr(name, data)
    return out.getvalue()


class Server(http.server.BaseHTTPRequestHandler):
    """Routes for the badge download; everything stays on 127.0.0.1."""
    files = {}

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
        path = self.path.split("?")[0]
        if path == "/get":
            self.send(200, self.files["zip"], [("Content-Disposition", 'attachment; filename="Tiere.zip"')])
        elif path == "/redirect":
            self.send(302, headers=[("Location", "/get")])
        elif path == "/sets/Stern.png":
            self.send(200, self.files["png"])
        elif path == "/sets/Stern.zip":
            self.send(200, self.files["zip"])
        elif path == "/":
            self.send(200, self.files["png"])
        elif path == "/page":
            self.send(200, b"<!DOCTYPE html><html>Just a moment...</html>", [("Content-Type", "text/html")])
        elif path == "/notes.zip":
            self.send(200, self.files["not_a_set"])
        elif path == "/damaged.zip":
            self.send(200, b"PK\x03\x04" + bytes(200))
        elif path == "/odd.png":
            self.send(200, self.files["odd"])
        elif path == "/to-https":
            self.send(302, headers=[("Location", f"https://127.0.0.1:{self.server.server_address[1]}/get")])
        else:
            self.send(404, b"not here")


class BadgeDownload(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = Path(tempfile.mkdtemp(prefix="badge-download-"))
        Server.files = {
            "zip": zipped({"Fuchs.png": png(64, 64, 1), "Voegel.png": png(128, 64, 2)}),
            "png": png(64, 64, 3),
            "not_a_set": zipped({"Fuchs.png": png(64, 64, 1), "liesmich.txt": b"x"}),
            "odd": png(60, 64, 4),
        }
        cls.server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Server)
        cls.base = f"http://127.0.0.1:{cls.server.server_address[1]}"
        threading.Thread(target=cls.server.serve_forever, daemon=True).start()

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def setUp(self):
        self.badges = Path(tempfile.mkdtemp(dir=self.tmp, prefix="Badges-"))
        self.part = self.tmp / "badge-download.part"

    def get(self, url):
        out = subprocess.run([str(BUILD / "test_badgeflow"), "download", url, str(self.part), str(self.badges)],
                             capture_output=True, text=True, check=True).stdout
        r = json.loads(out)
        self.assertEqual(r["part_left"], 0, r)
        return r

    def test_a_zip_through_a_redirect_named_by_its_header(self):
        r = self.get(self.base + "/redirect")
        self.assertEqual((r["status"], r["kind"], r["file"]), (DL_OK, KIND_ZIP, "Tiere.zip"), r)
        self.assertEqual((self.badges / "Tiere.zip").read_bytes(), Server.files["zip"])
        r = self.get(self.base + "/get")
        self.assertEqual(r["file"], "Tiere (2).zip", r)

    def test_a_png_and_a_name_taken_by_another_kind(self):
        r = self.get(self.base + "/sets/Stern.png")
        self.assertEqual((r["status"], r["kind"], r["file"]), (DL_OK, KIND_PNG, "Stern.png"), r)
        self.assertEqual((self.badges / "Stern.png").read_bytes(), Server.files["png"])
        r = self.get(self.base + "/sets/Stern.zip")
        self.assertEqual((r["status"], r["file"]), (DL_OK, "Stern (2).zip"), r)
        r = self.get(self.base + "/")
        self.assertEqual((r["status"], r["file"]), (DL_OK, "Badges.png"), r)

    def test_refusals_leave_nothing(self):
        for path, status, detail in (("/page", DL_NOT_THEME, ""), ("/notes.zip", DL_BAD_THEME, "BADGE_E_FOLDER"),
                                     ("/damaged.zip", DL_BAD_THEME, "BADGE_E_ZIP"),
                                     ("/odd.png", DL_BAD_THEME, "BADGE_E_IMAGE_SIZE"), ("/missing", DL_HTTP, ""),
                                     ("/to-https", DL_NO_CA, "")):
            with self.subTest(path):
                r = self.get(self.base + path)
                self.assertEqual((r["status"], r["detail"], r["file"]), (status, detail, ""), r)
                self.assertEqual(list(self.badges.iterdir()), [])


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--build", default="build/host")
    args, rest = ap.parse_known_args()
    BUILD = Path(args.build)
    unittest.main(argv=[sys.argv[0]] + rest)
