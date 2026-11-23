"""Stand-in theme zips for the renders, from the repository's own fixtures.py (solid-colour themes written from the
format documentation; names invented here). make_zips.py PACK_DIR OUT_DIR"""
import sys
import zipfile
from pathlib import Path

sys.dont_write_bytecode = True
sys.path.insert(0, sys.argv[1])
import fixtures  # noqa: E402

out = Path(sys.argv[2])
out.mkdir(parents=True, exist_ok=True)
THEMES = [("a.zip", "Morgenlicht", True), ("b.zip", "Kieselgrau", False), ("c.zip", "Waldweg", False),
          ("d.zip", "Tiefsee", True), ("e.zip", "Papierflieger", False)]
for file, name, bgm in THEMES:
    body = bytearray(fixtures.body((0x10, 0x80, 0x20), (0x08, 0x40, 0x10)))
    body[5] = 1 if bgm else 0
    with zipfile.ZipFile(out / file, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("body_LZ.bin", fixtures.lz11(bytes(body)))
        z.writestr("info.smdh", fixtures.smdh(name, "Stand-in theme for pictures", "nobody", (0x80, 0x60, 0xD0)))
        if bgm:
            z.writestr("bgm.bcstm", fixtures.bgm_stand_in())
