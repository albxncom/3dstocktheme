"""Build a 3DS theme DLC pack (one CIA) from theme zips, and verify a built pack.

build   reads <data>/themes/*.zip (Anemone / Theme Plaza format: body_LZ.bin, optional bgm.bcstm, info.smdh),
        gives each new zip the next unused content index in <data>/manifest.json, and writes
        <data>/out/theme-pack.cia for the EUR theme DLC title 0004008C000098xx (default variation 09).
verify  extracts the CIA again with ctrtool and checks it against the manifest and the zips, with its own parsers.
installed  records the TMD now on the console (<data>/installed.json), from the CIA installed with FBI or from a
        delta dir imported with import-test.
delta   writes <data>/out/delta/ to add the themes the installed pack lacks without reinstalling it: a new TMD
        (installed records unchanged, title version + 1), a new content 0 and the new theme contents.
verify-delta  checks out/delta against installed.json and the manifest, with the verify parsers and ctrtool.
adopt   brings manifest.json and installed.json to the console's state after 3DStockTheme (app/, core/)
        installed themes: from the application's record (installed.tmd, copied off the card) and the theme files it
        installed from (zips, or theme installer CIAs), matched to the TMD's new contents by SHA-256.

The pack: content 0 holds the catalogue (MetaDataContentHeader.bin, ContentInfoArchive_EUR_<lang>.bin, icons/<index>.icn)
and an ExeFS icon; content <index> holds one theme (body_LZ.bin, bgm.bcstm). makerom builds the NCCHs, TMD and
ticket; this file writes only the catalogue and the pack icon, then patches two things in makerom's CIA
(finish_cia): the real content indices and ticket rights for 0..0x3FF.
Format notes and their sources: README.md.

Run: python3 -I theme_pack.py build  [--data DIR] [--variation N] [--bin DIR]
     python3 -I theme_pack.py verify [--data DIR] [--bin DIR]
     python3 -I theme_pack.py installed [--data DIR] CIA_OR_DELTA_DIR
     python3 -I theme_pack.py delta [--data DIR] [--bin DIR]
     python3 -I theme_pack.py verify-delta [--data DIR] [--bin DIR] [--base-cia CIA]
     python3 -I theme_pack.py adopt [--data DIR] [--bin DIR] --tmd INSTALLED_TMD FILE...
     (DIR defaults to THEME_PACK_DATA from the environment or the repo's .env; tools come from --bin, else DIR/bin)
"""
import argparse
import datetime
import hashlib
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent

TITLE_HIGH = 0x0004008C           # DLC
EUR_THEME_LOW = 0x00009800        # EUR HOME Menu unique ID 0x98 << 8; the low byte is the variation
BODY_MAX = 0x150000               # BodyCache.bin, HOME's body buffer: the limit for the file and its decoded size
BGM_MAX = 0x337000                # BgmCache.bin
MAX_THEMES = 100                  # HOME's limit, official themes included
# HOME 11.17 keeps one status byte per content index in a 350-byte row per variation (list loop 0x1fbcec, no range
# check): an index of 350 or more writes into the next variation's row. The ticket still grants 0..0x3FF.
MAX_INDEX = 349
FULL = (f"the pack is full: content indices are never reused, and HOME only handles indices 1 to {MAX_INDEX} "
        f"(one 350-byte row per variation in its theme list)")
TOOLS = {"makerom": "v0.19.0", "ctrtool": "v1.3.0"}  # the versions this was tested with

# Catalogue (content 0). Region and language IDs as in the DLC library: region 2 = EUR, language IDs from CFG.
REGION_EUR = 2
EUR_LANGUAGES = [("en", 1), ("es", 5), ("fr", 2), ("de", 3), ("it", 4), ("nl", 8), ("pt", 9), ("ru", 10)]
ENTRY_SIZE = 0xC8
NAME_SIZE, DESC_SIZE = 0x40, 0x80
ICON_SIZE = 0x1200                # 48x48 tiled RGB565, the same layout as the SMDH large icon

BODY_USE_BGM = 0x5                # decompressed body_LZ.bin: 0 = ignore bgm.bcstm, non-zero = play it
SMDH_SIZE = 0x36C0
SMDH_TITLE_SLOTS = 16             # 0x200 each from 0x8: short 0x80, long 0x100, publisher 0x80, UTF-16LE
SMDH_LARGE_ICON = 0x24C0
SMDH_SMALL_ICON, SMALL_ICON_SIZE = 0x2040, 0x480

PACK_NAME = "Custom themes"       # what Data Management shows for the pack
PACK_ICON_RGB = (0x40, 0x60, 0x80)
PLACEHOLDER_RGB = (0x80, 0x80, 0x80)

RSF = """BasicInfo:
  Title: THEMES
  CompanyCode: "00"
  ProductCode: CTR-N-THMP
  ContentType: Application
  Logo: None
TitleInfo:
  Category: AddOnContents
  UniqueId: 0x98
  Variation: $(VARIATION)
Option:
  UseOnSD: true
  EnableCrypt: false
  EnableCompress: false
  MediaFootPadding: false
RomFs:
  RootPath: $(ROOT)
"""


class PackError(Exception):
    pass


# ---------------------------------------------------------------- settings and tools

def setting(name):
    """From the environment, else from the repo's .env (one KEY=value per line, gitignored)."""
    if name in os.environ:
        return os.environ[name]
    env = HERE.parent.parent / ".env"
    for line in env.read_text(encoding="utf-8").splitlines() if env.is_file() else []:
        key, sep, value = line.partition("=")
        if sep and key.strip() == name:
            return value.strip().strip("\"'")
    return None


def tool(name, data, bin_dir=None):
    """--bin DIR wins when given, else <data>/bin. No PATH lookup: the pack depends on the tool version."""
    folder = Path(bin_dir) if bin_dir else Path(data) / "bin"
    path = folder / name
    if not (path.is_file() and os.access(path, os.X_OK)):
        raise PackError(f"{name} not found in {folder} (put it in <data>/bin/ or pass --bin DIR; see README, Setup)")
    probe = subprocess.run([str(path)], capture_output=True, text=True)
    banner = [line for line in (probe.stdout + probe.stderr).splitlines() if line.startswith("CTR")]
    if not banner or f" {TOOLS[name]} " not in banner[0]:
        print(f"warning: {path} is not {name} {TOOLS[name]}, the tested version ({banner[:1]})", file=sys.stderr)
    return path


def run(cmd, cwd=None):
    result = subprocess.run([str(c) for c in cmd], cwd=cwd, capture_output=True, text=True)
    if result.returncode != 0:
        raise PackError(f"{Path(cmd[0]).name} failed ({result.returncode}): {' '.join(map(str, cmd[1:]))}\n"
                        f"{result.stdout}{result.stderr}")
    return result.stdout


# ---------------------------------------------------------------- manifest

def title_id(variation):
    if not (isinstance(variation, int) and 0 <= variation <= 9):
        raise PackError(f"variation must be 0 to 9 (HOME reads theme DLC variations 0 to 9), got {variation!r}")
    return TITLE_HIGH << 32 | EUR_THEME_LOW | variation


def load_manifest(path):
    if not path.is_file():
        return None
    return json.loads(path.read_text(encoding="utf-8"))


def assign_indices(manifest, zip_names, variation, today):
    """The manifest after this build: every zip keeps its index; new zips get the next unused one, in name order.

    Indices are never renumbered and never reused: HOME's save, shuffle entries and layout slots reference a
    theme by (variation, content index). A zip that is gone keeps its entry, so its index stays reserved.
    """
    tid = f"{title_id(variation):016X}"
    if manifest is None:
        manifest = {"title_id": tid, "next_index": 1, "themes": []}
    elif manifest["title_id"] != tid:
        raise PackError(f"manifest.json belongs to {manifest['title_id']}, not {tid}: its indices only mean something "
                        f"for that title. Build without --variation, or start a new data dir for the other variation.")
    manifest = json.loads(json.dumps(manifest))
    themes = manifest["themes"]
    known = {t["zip"] for t in themes}
    indices = [t["index"] for t in themes]
    if len(indices) != len(set(indices)) or any(i < 1 for i in indices):
        raise PackError("manifest.json has a duplicate or out-of-range content index; fix it by hand")
    if any(i > MAX_INDEX for i in indices):
        raise PackError(f"manifest.json holds content index {max(indices)}, above {MAX_INDEX}: {FULL}")
    next_index = max([manifest.get("next_index", 1)] + [i + 1 for i in indices])
    for name in sorted(zip_names):
        if name in known:
            continue
        if next_index > MAX_INDEX:
            raise PackError(f"no content index left for {name}: {FULL}")
        themes.append({"index": next_index, "zip": name, "name": "", "description": "", "added": today})
        next_index += 1
    themes.sort(key=lambda t: t["index"])
    manifest["next_index"] = next_index
    return manifest


# ---------------------------------------------------------------- theme zips

def read_member(archive, name, limit):
    try:
        info = archive.getinfo(name)
    except KeyError:
        return None
    if info.file_size > limit:
        raise PackError(f"{name} is {info.file_size:#x} bytes, the limit is {limit:#x}")
    return archive.read(info)


def smdh_text(smdh, offset, size):
    return smdh[offset:offset + size].decode("utf-16-le", "replace").split("\0", 1)[0].strip()


def lz11_head(data, count):
    """The first `count` decompressed bytes of an LZ11 stream (flag byte per 8 tokens, MSB first, 1 = copy)."""
    size, pos = int.from_bytes(data[1:4], "little"), 4
    if size == 0:
        size, pos = int.from_bytes(data[4:8], "little"), 8
    if size < count:
        raise PackError(f"body_LZ.bin decompresses to {size:#x} bytes, too short for a theme body")
    if size > BODY_MAX:
        raise PackError(f"body_LZ.bin decompresses to {size:#x} bytes, the limit is {BODY_MAX:#x} (HOME's body buffer)")
    out = bytearray()
    try:
        while len(out) < count:
            flags = data[pos]
            pos += 1
            for bit in range(8):
                if len(out) >= count:
                    break
                if not flags & 0x80 >> bit:
                    out.append(data[pos])
                    pos += 1
                    continue
                kind = data[pos] >> 4
                if kind == 0:
                    length = ((data[pos] & 0xF) << 4 | data[pos + 1] >> 4) + 0x11
                    pos += 1
                elif kind == 1:
                    length = ((data[pos] & 0xF) << 12 | data[pos + 1] << 4 | data[pos + 2] >> 4) + 0x111
                    pos += 2
                else:
                    length = kind + 1
                disp = ((data[pos] & 0xF) << 8 | data[pos + 1]) + 1
                pos += 2
                if disp > len(out):
                    raise IndexError
                for _ in range(length):
                    out.append(out[-disp])
    except IndexError:
        raise PackError("body_LZ.bin is not valid LZ11") from None
    return bytes(out[:count])


def read_theme(path):
    """body, bgm (or None), name, description and the 48x48 icon of one theme zip. Files must sit at the zip root."""
    try:
        with zipfile.ZipFile(path) as archive:
            body = read_member(archive, "body_LZ.bin", BODY_MAX)
            bgm = read_member(archive, "bgm.bcstm", BGM_MAX)
            smdh = read_member(archive, "info.smdh", SMDH_SIZE)
    except (zipfile.BadZipFile, PackError) as e:
        raise PackError(f"{path.name}: {e}") from None
    if body is None or smdh is None:
        raise PackError(f"{path.name}: body_LZ.bin and info.smdh must be at the zip root")
    if body[:1] != b"\x11":
        raise PackError(f"{path.name}: body_LZ.bin is not LZ11 compressed (first byte {body[:1].hex()})")
    if len(smdh) != SMDH_SIZE or smdh[:4] != b"SMDH":
        raise PackError(f"{path.name}: info.smdh is not an SMDH ({len(smdh):#x} bytes)")
    try:
        use_bgm = lz11_head(body, BODY_USE_BGM + 1)[BODY_USE_BGM] != 0
    except PackError as e:
        raise PackError(f"{path.name}: {e}") from None
    if bgm is not None and not use_bgm:
        print(f"warning: {path.name}: has bgm.bcstm, but the body's use-BGM byte ({BODY_USE_BGM:#x}) is off: "
              f"HOME will not play the music", file=sys.stderr)
    elif bgm is None and use_bgm:
        print(f"warning: {path.name}: the body's use-BGM byte ({BODY_USE_BGM:#x}) is on, but there is no bgm.bcstm",
              file=sys.stderr)
    # Slot 0 first, as Anemone reads it; Theme Plaza fills every slot with the same text.
    slots = [0x8 + 0x200 * i for i in range(SMDH_TITLE_SLOTS)]
    name = next((n for n in (smdh_text(smdh, s, 0x80) for s in slots) if n), path.stem)
    description = next((d for d in (smdh_text(smdh, s + 0x80, 0x100) for s in slots) if d), "")
    icon = smdh[SMDH_LARGE_ICON:SMDH_LARGE_ICON + ICON_SIZE]
    return {"body": body, "bgm": bgm, "name": name, "description": description, "icon": icon}


# ---------------------------------------------------------------- content 0: catalogue and pack icon

def utf8_field(text, size):
    """NUL-terminated UTF-8 in a fixed field, cut at a character boundary."""
    raw = text.encode("utf-8")[:size - 1]
    while raw:
        try:
            raw.decode("utf-8")
            break
        except UnicodeDecodeError:
            raw = raw[:-1]
    return raw.ljust(size, b"\0")


def meta_header(count):
    """MetaDataContentHeader.bin for `count` catalogue entries after the dummy, laid out like a real EUR DLC header."""
    out = struct.pack("<5I", 1, count, count, 0x00300030, 0xDEADBEEF)
    for region in range(7):
        out += struct.pack("<2H", REGION_EUR, 1) if region == REGION_EUR else b"\xff" * 4
    out += struct.pack("<I", len(EUR_LANGUAGES))
    out += b"".join(struct.pack("<2H", REGION_EUR, lang) for _, lang in EUR_LANGUAGES)
    return out


def catalogue_entry(name, description, index, enabled):
    return utf8_field(name, NAME_SIZE) + utf8_field(description, DESC_SIZE) + struct.pack("<2I", index, enabled)


def content_info_archive(entries):
    """entries: {index: (name, description)}. HOME reads entry i at 8 + 0xC8 * i, so every index up to the highest
    gets an entry; an index without a content (reserved) is listed like an unbought official theme."""
    top = max(entries)
    out = struct.pack("<2I", 1, top) + catalogue_entry("Dummy", "", 0, 0)
    for index in range(1, top + 1):
        name, description = entries.get(index, ("(removed)", ""))
        out += catalogue_entry(name, description, index, 1)
    return out


def solid_icon(rgb, size):
    r, g, b = rgb
    return struct.pack("<H", (r >> 3) << 11 | (g >> 2) << 5 | b >> 3) * (size // 2)


def pack_smdh():
    """The ExeFS icon of content 0: what Data Management shows for the pack."""
    out = bytearray(SMDH_SIZE)
    out[0:4] = b"SMDH"
    for i in range(SMDH_TITLE_SLOTS):
        base = 0x8 + 0x200 * i
        for offset, size, text in ((0, 0x80, PACK_NAME), (0x80, 0x100, PACK_NAME), (0x180, 0x80, "theme_pack")):
            out[base + offset:base + offset + size] = text.encode("utf-16-le").ljust(size, b"\0")
    struct.pack_into("<I", out, 0x2018, 0x7FFFFFFF)  # region lockout: all regions
    out[SMDH_SMALL_ICON:SMDH_SMALL_ICON + SMALL_ICON_SIZE] = solid_icon(PACK_ICON_RGB, SMALL_ICON_SIZE)
    out[SMDH_LARGE_ICON:SMDH_LARGE_ICON + ICON_SIZE] = solid_icon(PACK_ICON_RGB, ICON_SIZE)
    return bytes(out)


def write_content0(root, themes, top):
    """themes: {index: theme} of the packed themes; top: the highest index in the manifest."""
    entries = {i: (t["name"], t["description"]) for i, t in themes.items()}
    entries.setdefault(top, ("(removed)", ""))
    (root / "icons").mkdir(parents=True)
    (root / "MetaDataContentHeader.bin").write_bytes(meta_header(top))
    archive = content_info_archive(entries)
    for code, _ in EUR_LANGUAGES:
        (root / f"ContentInfoArchive_EUR_{code}.bin").write_bytes(archive)
    for index in range(1, top + 1):
        icon = themes[index]["icon"] if index in themes else solid_icon(PLACEHOLDER_RGB, ICON_SIZE)
        (root / "icons" / f"{index}.icn").write_bytes(icon)


# ---------------------------------------------------------------- CIA

def cia_sections(f):
    """Offsets of the ticket and the TMD in a CIA (sections are aligned to 64 bytes)."""
    f.seek(0)
    header_size, cert_size, ticket_size, tmd_size = struct.unpack("<I4xIII", f.read(0x14))
    ticket = -(-header_size // 64) * 64 + -(-cert_size // 64) * 64
    return ticket, ticket_size, ticket + -(-ticket_size // 64) * 64, tmd_size


def finish_cia(cia, tid, indices):
    """Two fixes to makerom's output, both under signatures that are fake anyway (makerom -target t; Luma patches
    the checks on the console):

    - Content indices: makerom 0.19.0 crashes on a gap in the indices (ImportNcchContent opens contentPath[i] with
      the compacted position i), so it gets positions 0..n-1 and the real indices are written here: the TMD chunk
      records, the TMD's info-record hash chain and the CIA header's content bitmap.
    - Ticket rights: makerom grants only the included indices; this grants all of 0..0x3FF, so a theme added later
      needs no new ticket.
    """
    with open(cia, "r+b") as f:
        ticket_at, ticket_size, tmd_at, tmd_size = cia_sections(f)
        f.seek(ticket_at)
        ticket = bytearray(f.read(ticket_size))
        if (len(ticket) != 0x350 or struct.unpack_from(">I", ticket, 0)[0] != 0x00010004
                or struct.unpack_from(">Q", ticket, 0x1DC)[0] != tid
                or struct.unpack_from(">II", ticket, 0x2BC) != (1, 0x84)
                or struct.unpack_from(">I", ticket, 0x2CC)[0] != 0):
            raise PackError("makerom's ticket does not have the expected layout (one RSA-2048 ticket, one rights "
                            "segment for 0..0x3FF); not patching it")
        ticket[0x2D0:0x350] = b"\xff" * 0x80
        f.seek(ticket_at)
        f.write(ticket)

        f.seek(tmd_at)
        tmd = bytearray(f.read(tmd_size))
        body, chunks = 0x140, 0x140 + 0xC4 + 0x900
        records = [struct.unpack_from(">IH", tmd, chunks + 0x30 * k) for k in range(len(indices))]
        if (struct.unpack_from(">I", tmd, 0)[0] != 0x00010004 or struct.unpack_from(">Q", tmd, body + 0x4C)[0] != tid
                or struct.unpack_from(">H", tmd, body + 0x9E)[0] != len(indices)
                or records != [(i, k) for k, i in enumerate(indices)]
                or struct.unpack_from(">HH", tmd, body + 0xC4) != (0, len(indices))):
            raise PackError("makerom's TMD does not have the expected layout; not patching it")
        for k, index in enumerate(indices):
            struct.pack_into(">H", tmd, chunks + 0x30 * k + 4, index)
        tmd[body + 0xC4 + 4:body + 0xC4 + 0x24] = hashlib.sha256(tmd[chunks:chunks + 0x30 * len(indices)]).digest()
        tmd[body + 0xA4:body + 0xC4] = hashlib.sha256(tmd[body + 0xC4:body + 0xC4 + 0x900]).digest()
        f.seek(tmd_at)
        f.write(tmd)

        bitmap = bytearray(0x2000)
        for index in indices:
            bitmap[index // 8] |= 0x80 >> (index & 7)
        f.seek(0x20)
        f.write(bitmap)


def theme_zips(data):
    themes_dir = Path(data) / "themes"
    zips = sorted(p for p in themes_dir.glob("*.zip") if p.is_file()) if themes_dir.is_dir() else []
    if not zips:
        raise PackError(f"no theme zips in {themes_dir}")
    if len(zips) > MAX_THEMES:
        raise PackError(f"{len(zips)} themes; HOME lists at most {MAX_THEMES}, official themes included")
    return zips


def prepare(data, zips, variation):
    """The manifest after this run, the themes to pack {index: theme} and the title ID."""
    old = load_manifest(Path(data) / "manifest.json")
    if old is not None and variation is None:
        variation = int(old["title_id"][-2:], 16)
    tid = title_id(9 if variation is None else variation)
    manifest = assign_indices(old, [p.name for p in zips], tid & 0xFF, datetime.date.today().isoformat())
    by_zip = {t["zip"]: t for t in manifest["themes"]}
    packed = {}
    for path in zips:
        theme = read_theme(path)
        entry = by_zip[path.name]
        entry["name"], entry["description"] = theme["name"], theme["description"]
        packed[entry["index"]] = theme
    return manifest, packed, tid


def make_cia(makerom, tid, packed, top, tmp):
    """Builds the whole pack in the folder tmp; returns the path of the CIA there."""
    (tmp / "pack.rsf").write_text(RSF, encoding="utf-8")
    write_content0(tmp / "c0", packed, top)
    (tmp / "pack.smdh").write_bytes(pack_smdh())
    cfa = ["-f", "cfa", "-rsf", "pack.rsf", "-target", "t", f"-DVARIATION={tid & 0xFF}"]
    run([makerom, *cfa, "-DROOT=c0", "-icon", "pack.smdh", "-o", "c0.cfa"], cwd=tmp)
    contents = ["-i", "c0.cfa:0:0"]
    for position, (index, theme) in enumerate(sorted(packed.items()), 1):
        root = tmp / f"c{index}"
        root.mkdir()
        (root / "body_LZ.bin").write_bytes(theme["body"])
        if theme["bgm"] is not None:
            (root / "bgm.bcstm").write_bytes(theme["bgm"])
        run([makerom, *cfa, f"-DROOT=c{index}", "-o", f"c{index}.cfa"], cwd=tmp)
        contents += ["-i", f"c{index}.cfa:{position}:{index}"]  # content ID = real index, see finish_cia
    run([makerom, "-f", "cia", "-dlc", "-target", "t", "-o", "pack.cia", *contents], cwd=tmp)
    finish_cia(tmp / "pack.cia", tid, [0, *sorted(packed)])
    return tmp / "pack.cia"


def write_json(path, value):
    part = path.with_name(path.name + ".part")
    part.write_text(json.dumps(value, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    os.replace(part, path)


def build(data, variation, bin_dir):
    data = Path(data)
    zips = theme_zips(data)
    makerom = tool("makerom", data, bin_dir)
    manifest, packed, tid = prepare(data, zips, variation)
    top = max(t["index"] for t in manifest["themes"])

    out = data / "out"
    out.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="theme-pack-") as tmp:
        cia = make_cia(makerom, tid, packed, top, Path(tmp))
        shutil.move(str(cia), out / "theme-pack.cia.part")
    os.replace(out / "theme-pack.cia.part", out / "theme-pack.cia")
    write_json(data / "manifest.json", manifest)

    for entry in manifest["themes"]:
        if entry["index"] in packed:
            print(f"{entry['index']:4d}  {entry['name']}")
        else:
            print(f"{entry['index']:4d}  (reserved: {entry['zip']} is not in themes/)")
    print(f"wrote {out / 'theme-pack.cia'}: {len(packed)} themes, title {tid:016X}")
    print(f"after installing it with FBI: theme_pack.py installed {out / 'theme-pack.cia'}")


# ---------------------------------------------------------------- installed record and delta
# installed.json holds the TMD that is on the console, byte for byte (from the CIA installed with FBI, or from a
# delta imported with import-test). A delta keeps every record of that TMD unchanged and adds the new themes; so
# the record, not a rebuild, is the source of the installed contents' IDs, sizes and hashes.

TMD_BODY = 0x140                  # RSA-2048 signature (type 0x10004) and padding, as makerom writes it
TMD_CHUNKS = TMD_BODY + 0xC4 + 0x900
# Content 0 of a delta gets a content ID no content of the title ever had (themes keep ID = index, below 0x400):
# AM writes a content to <ID>.app and drops the files of the old TMD at the commit, like a DLC update does.
CONTENT0_DELTA_ID = 0x10000       # + the delta's title version


def tmd_parse(tmd):
    if struct.unpack_from(">I", tmd, 0)[0] != 0x00010004 or len(tmd) < TMD_CHUNKS:
        raise PackError("not a TMD with an RSA-2048 signature")
    tid, version, count = struct.unpack_from(">Q", tmd, TMD_BODY + 0x4C)[0], *struct.unpack_from(">HH", tmd,
                                                                                                TMD_BODY + 0x9C)
    if len(tmd) != TMD_CHUNKS + 0x30 * count:
        raise PackError(f"TMD is {len(tmd):#x} bytes, {count} content records need {TMD_CHUNKS + 0x30 * count:#x}")
    chunks = []
    for k in range(count):
        cid, index, ctype, size = struct.unpack_from(">IHHQ", tmd, TMD_CHUNKS + 0x30 * k)
        sha = tmd[TMD_CHUNKS + 0x30 * k + 0x10:TMD_CHUNKS + 0x30 * (k + 1)]
        chunks.append({"id": cid, "index": index, "type": ctype, "size": size, "sha256": sha})
    return {"title_id": tid, "version": version, "chunks": chunks}


def tmd_with(base, chunks, version):
    """base's signature and header, with these content records, title version and fresh info-record hashes."""
    tmd = bytearray(base[:TMD_CHUNKS])
    records = b"".join(struct.pack(">IHHQ", c["id"], c["index"], c["type"], c["size"]) + c["sha256"] for c in chunks)
    info = bytearray(0x900)
    struct.pack_into(">HH", info, 0, 0, len(chunks))
    info[4:0x24] = hashlib.sha256(records).digest()
    struct.pack_into(">HH", tmd, TMD_BODY + 0x9C, version, len(chunks))
    tmd[TMD_BODY + 0xA4:TMD_BODY + 0xC4] = hashlib.sha256(info).digest()
    tmd[TMD_BODY + 0xC4:TMD_CHUNKS] = info
    return bytes(tmd) + records


def cia_tmd_and_contents(cia):
    """The TMD bytes and {index: content bytes} of a CIA; contents follow the TMD in record order, 64-aligned."""
    with open(cia, "rb") as f:
        _, _, tmd_at, tmd_size = cia_sections(f)
        f.seek(tmd_at)
        tmd = f.read(tmd_size)
        offset = tmd_at + -(-tmd_size // 64) * 64
        contents = {}
        for c in tmd_parse(tmd)["chunks"]:
            f.seek(offset)
            contents[c["index"]] = f.read(c["size"])
            if hashlib.sha256(contents[c["index"]]).digest() != c["sha256"]:
                raise PackError(f"{cia}: content {c['index']} does not match its TMD record")
            offset += -(-c["size"] // 64) * 64
    return tmd, contents


def chunk_json(c):
    return {"index": c["index"], "id": f"{c['id']:08X}", "type": f"{c['type']:04X}", "size": c["size"],
            "sha256": c["sha256"].hex()}


def load_installed(data):
    path = Path(data) / "installed.json"
    if not path.is_file():
        raise PackError(f"no {path}: record the pack that is on the console first "
                        f"(theme_pack.py installed <the CIA you installed with FBI>)")
    record = json.loads(path.read_text(encoding="utf-8"))
    tmd = bytes.fromhex(record["tmd"])
    parsed = tmd_parse(tmd)
    if f"{parsed['title_id']:016X}" != record["title_id"] or parsed["version"] != record["title_version"]:
        raise PackError(f"{path}: its fields do not match its own TMD; fix or re-record it")
    return tmd, parsed, record


def installed(data, source):
    """Records the TMD now on the console: from the CIA just installed with FBI, or from a delta dir just imported."""
    data, source = Path(data), Path(source)
    if source.is_dir():
        tmd = (source / "tmd.bin").read_bytes()
        info = json.loads((source / "delta.json").read_text(encoding="utf-8"))
        if hashlib.sha256(tmd).hexdigest() != info["tmd_sha256"]:
            raise PackError(f"{source}: tmd.bin is not the TMD delta.json names")
        base = hashlib.sha256(load_installed(data)[0]).hexdigest()
        if info["base_tmd_sha256"] != base:
            raise PackError(f"{source} was made for another installed state than installed.json records (base TMD "
                            f"{info['base_tmd_sha256'][:12]}..., recorded {base[:12]}...); it is already recorded, or "
                            f"installed.json is stale")
        digest = hashlib.sha256(tmd).hexdigest()
    elif source.is_file():
        tmd = cia_tmd_and_contents(source)[0]
        digest = hashlib.sha256(source.read_bytes()).hexdigest()
    else:
        raise PackError(f"{source}: no such CIA or delta dir")
    parsed = tmd_parse(tmd)
    manifest = load_manifest(data / "manifest.json")
    if manifest is not None and manifest["title_id"] != f"{parsed['title_id']:016X}":
        raise PackError(f"{source} is title {parsed['title_id']:016X}, the manifest is {manifest['title_id']}")
    write_json(data / "installed.json", {
        "title_id": f"{parsed['title_id']:016X}", "title_version": parsed["version"],
        "source": str(source), "source_sha256": digest, "recorded": datetime.date.today().isoformat(),
        "contents": [chunk_json(c) for c in parsed["chunks"]], "tmd": tmd.hex()})
    print(f"recorded as installed: title {parsed['title_id']:016X} version {parsed['version']}, contents "
          f"{[c['index'] for c in parsed['chunks']]}, from {source}")


def delta(data, bin_dir):
    """Writes <data>/out/delta/: the TMD, content 0 and the content of every theme that the installed pack lacks."""
    data = Path(data)
    base_tmd, base, _ = load_installed(data)
    if not base["chunks"] or base["chunks"][0]["index"] != 0:
        raise PackError("the installed TMD has no content 0 as its first record")
    old = load_manifest(data / "manifest.json")
    if old is None or old["title_id"] != f"{base['title_id']:016X}":
        raise PackError(f"manifest.json does not belong to the installed title {base['title_id']:016X}")
    zips = theme_zips(data)
    makerom = tool("makerom", data, bin_dir)
    manifest, packed, tid = prepare(data, zips, None)
    kept = {c["index"]: c for c in base["chunks"] if c["index"] != 0}
    by_index = {t["index"]: t for t in manifest["themes"]}
    gone = [i for i in sorted(kept) if i not in packed]
    if gone:
        raise PackError(f"contents {gone} are in the installed pack, but their zips are not in themes/ "
                        f"({', '.join(by_index[i]['zip'] if i in by_index else '?' for i in gone)}): the new "
                        f"catalogue would list them as removed. Put the zips back, or build and reinstall the pack")
    new = sorted(i for i in packed if i not in kept)
    if not new:
        raise PackError("nothing to add: every theme in themes/ is already in the installed pack")
    version = base["version"] + 1
    if version > 0xFFFF:
        raise PackError("the title version is at its maximum; build and reinstall the whole pack")

    with tempfile.TemporaryDirectory(prefix="theme-pack-delta-") as tmp:
        fresh_tmd, contents = cia_tmd_and_contents(make_cia(makerom, tid, packed, max(by_index), Path(tmp)))
    fresh = {c["index"]: c for c in tmd_parse(fresh_tmd)["chunks"]}

    def same(i):   # as built here, or as the installer built it on the console (signature 0xFF, see adopt)
        if fresh[i] == kept[i]:
            return True
        return {**fresh[i], "sha256": hashlib.sha256(as_installer(contents[i])).digest()} == kept[i]
    changed = [i for i in sorted(kept) if not same(i)]
    if changed:
        raise PackError(f"contents {changed} built from themes/ differ from the installed ones (zip or tools changed "
                        f"since that install); a delta only adds themes: build and reinstall the whole pack")
    content0 = {**fresh[0], "id": CONTENT0_DELTA_ID + version}
    if content0["id"] in {c["id"] for c in base["chunks"]}:
        raise PackError(f"content ID {content0['id']:08X} for the new content 0 is already used by the installed pack")
    chunks = sorted([content0, *kept.values(), *(fresh[i] for i in new)], key=lambda c: c["index"])
    tmd = tmd_with(base_tmd, chunks, version)

    out = data / "out" / "delta"
    part = out.with_name("delta.part")
    shutil.rmtree(part, ignore_errors=True)
    part.mkdir(parents=True)
    (part / "tmd.bin").write_bytes(tmd)
    for index in [0, *new]:
        (part / f"{index}.app").write_bytes(contents[index])
    write_json(part / "delta.json", {
        "title_id": f"{tid:016X}", "made": datetime.date.today().isoformat(),
        "base_title_version": base["version"], "base_tmd_sha256": hashlib.sha256(base_tmd).hexdigest(),
        "title_version": version, "tmd_sha256": hashlib.sha256(tmd).hexdigest(),
        "import": [{**chunk_json(c), "file": f"{c['index']}.app",
                    "name": by_index[c["index"]]["name"] if c["index"] else "catalogue (content 0)"}
                   for c in chunks if c["index"] in (0, *new)],
        "kept": sorted(kept)})
    shutil.rmtree(out, ignore_errors=True)
    os.replace(part, out)
    write_json(data / "manifest.json", manifest)

    print(f"   0  import  catalogue, content ID {content0['id']:08X} (installed: {base['chunks'][0]['id']:08X})")
    for index in sorted([*kept, *new]):
        print(f"{index:4d}  {'import' if index in new else 'kept  '}  {by_index[index]['name']}")
    print(f"wrote {out}: title {tid:016X} version {base['version']} -> {version}, import {[0, *new]}, "
          f"keep {sorted(kept)}")
    print(f"after importing it on the console: theme_pack.py installed {out}")


# ---------------------------------------------------------------- adopt: what 3DStockTheme installed

NCCH_SIGNATURE = 0x100            # the NCCH header's signature: makerom signs with its test key, the installer writes 0xFF


def tmd_chain_ok(tmd):
    """The info-record hash in the header and every info record's hash over its content records."""
    info = tmd[TMD_BODY + 0xC4:TMD_CHUNKS]
    if hashlib.sha256(info).digest() != tmd[TMD_BODY + 0xA4:TMD_BODY + 0xC4]:
        return False
    covered = 0
    for r in range(64):
        offset, count = struct.unpack_from(">HH", info, 0x24 * r)
        if count:
            records = tmd[TMD_CHUNKS + 0x30 * offset:TMD_CHUNKS + 0x30 * (offset + count)]
            if len(records) != 0x30 * count or hashlib.sha256(records).digest() != info[0x24 * r + 4:0x24 * r + 0x24]:
                return False
            covered += count
    return covered == (len(tmd) - TMD_CHUNKS) // 0x30


def zip_of(path, ctrtool, tmp):
    """The theme zip in a file: the file if it is a zip, the zip in a theme installer CIA's RomFS (config.ini's
    location=, default theme.zip) through ctrtool otherwise. Decided by the first bytes, as the installer does."""
    head = path.read_bytes()[:8]
    if head[:4] == b"PK\x03\x04":
        return path.read_bytes()
    if struct.unpack_from("<IHH", head.ljust(8, b"\0"))[0:3] != (0x2020, 0, 0):
        raise PackError(f"{path.name}: neither a theme zip nor a theme installer CIA")
    work = tmp / f"cia-{path.stem}"
    work.mkdir()
    run([ctrtool, f"--contents={work / 'c'}", path])
    content0 = sorted(work.glob("c.0000.*"))
    if not content0:
        raise PackError(f"{path.name}: ctrtool found no content 0 in it")
    run([ctrtool, f"--romfsdir={work / 'romfs'}", content0[0]])
    location = "theme.zip"
    config = work / "romfs" / "config.ini"
    if config.is_file():
        for line in config.read_text(encoding="utf-8", errors="replace").splitlines():
            key, sep, value = line.strip().lstrip("\ufeff").partition("=")
            if sep and key.strip().lower() == "location":
                location = value.strip()
    inner = work / "romfs" / location
    if "/" in location or "\\" in location or location.startswith(".") or not inner.is_file():
        raise PackError(f"{path.name}: no theme zip at location={location} in its RomFS")
    return inner.read_bytes()


def as_installer(content):
    """A theme content as the installer writes it: the same NCCH with its header signature 0xFF instead of makerom's
    test signature (docs/README.md, "What it writes"; tested byte for byte there)."""
    return b"\xff" * NCCH_SIGNATURE + content[NCCH_SIGNATURE:]


def theme_content(makerom, tid, theme, tmp, name):
    """A theme's content as build makes it with makerom."""
    (tmp / "pack.rsf").write_text(RSF, encoding="utf-8")
    root = tmp / name
    root.mkdir()
    (root / "body_LZ.bin").write_bytes(theme["body"])
    if theme["bgm"] is not None:
        (root / "bgm.bcstm").write_bytes(theme["bgm"])
    run([makerom, "-f", "cfa", "-rsf", "pack.rsf", "-target", "t", f"-DVARIATION={tid & 0xFF}", f"-DROOT={name}",
         "-o", f"{name}.cfa"], cwd=tmp)
    return (tmp / f"{name}.cfa").read_bytes()


def adopt(data, bin_dir, tmd_path, files):
    """Records what the installer did on the console: installed.json becomes its TMD, and the manifest gets each
    new content's theme under the index the console gave it. Refuses unless every new content of the TMD has
    exactly one file whose theme content hashes to it, and every file matches one."""
    data, tmd_path, files = Path(data), Path(tmd_path), [Path(f) for f in files]
    base_tmd, base, _ = load_installed(data)
    tmd = tmd_path.read_bytes()
    new = tmd_parse(tmd)
    manifest = load_manifest(data / "manifest.json")
    if manifest is None or manifest["title_id"] != f"{new['title_id']:016X}" or new["title_id"] != base["title_id"]:
        raise PackError(f"{tmd_path.name} is for title {new['title_id']:016X}; the manifest and installed.json are "
                        f"for {base['title_id']:016X}")
    if not tmd_chain_ok(tmd):
        raise PackError(f"{tmd_path.name}: its hash chain is broken (damaged copy?)")
    if new["version"] <= base["version"]:
        raise PackError(f"{tmd_path.name} has title version {new['version']}, installed.json already "
                        f"{base['version']}: nothing newer to adopt")
    by_index = {c["index"]: c for c in new["chunks"]}
    kept = [c for c in base["chunks"] if c["index"] != 0]
    lost = [c["index"] for c in kept if by_index.get(c["index"]) != c]
    if lost:
        raise PackError(f"contents {lost} are not carried over unchanged in {tmd_path.name}: it does not follow the "
                        f"recorded pack (was the pack reinstalled? record that CIA with 'installed' first)")
    if 0 not in by_index or by_index[0]["id"] != CONTENT0_DELTA_ID + new["version"]:
        raise PackError(f"{tmd_path.name}: content 0 has not the ID the installer gives it "
                        f"({CONTENT0_DELTA_ID + new['version']:08X})")
    added = sorted(i for i in by_index if i and i not in {c["index"] for c in kept})
    if not added:
        raise PackError(f"{tmd_path.name} adds no theme to the recorded pack")
    for i in added:
        c = by_index[i]
        if c["id"] != i or c["type"] != 0x4000 or i > MAX_INDEX:
            raise PackError(f"content {i}: ID {c['id']:08X}, type {c['type']:04X}: not a theme the installer adds")
    held = {t["index"]: t for t in manifest["themes"] if t["index"] in added}
    if held:
        i, t = min(held.items())
        raise PackError(f"index {i}, which the console gave to a theme it installed, is held in manifest.json by "
                        f"{t['zip']} ('{t['name']}'), from a build or delta that never reached the console. Remove "
                        f"that entry from manifest.json (its zip gets a new index at the next build or delta), or "
                        f"restore manifest.json from before that run, then adopt again")
    if not files:
        raise PackError(f"give the theme file of each new content: {added}")

    makerom, ctrtool = tool("makerom", data, bin_dir), tool("ctrtool", data, bin_dir)
    themes_dir = data / "themes"
    matched = {}
    with tempfile.TemporaryDirectory(prefix="theme-pack-adopt-") as tmp:
        tmp = Path(tmp)
        for n, path in enumerate(files):
            zip_bytes = zip_of(path, ctrtool, tmp)
            zip_path = tmp / f"{n}-{path.stem}.zip"
            zip_path.write_bytes(zip_bytes)
            theme = read_theme(zip_path)
            built = theme_content(makerom, new["title_id"], theme, tmp, f"t{n}")
            content = as_installer(built)
            digest = hashlib.sha256(content).digest()
            hits = [i for i in added if by_index[i]["sha256"] == digest and by_index[i]["size"] == len(content)]
            if not hits:
                known = [c["index"] for c in kept if c["sha256"] in (digest, hashlib.sha256(built).digest())]
                raise PackError(f"{path.name} is none of the contents the console added "
                                f"({'it is recorded already as theme ' + str(known[0]) if known else 'no hash matches'})")
            if hits[0] in matched:
                raise PackError(f"{path.name} and {matched[hits[0]]['path'].name} are the same theme")
            matched[hits[0]] = {"path": path, "zip": zip_bytes, "theme": theme}
        missing = [i for i in added if i not in matched]
        if missing:
            raise PackError(f"contents {missing} have no matching file: give the zip or CIA each was installed from")

        names = {t["zip"] for t in manifest["themes"]}
        for i, m in sorted(matched.items()):
            m["name"] = f"{m['path'].stem}.zip"
            target = themes_dir / m["name"]
            if m["name"] in names:
                raise PackError(f"{m['name']} is in the manifest already under another index; rename the file you "
                                f"give (a zip's name is its identity in the manifest)")
            if target.exists() and target.read_bytes() != m["zip"]:
                raise PackError(f"themes/{m['name']} exists with other bytes; rename the file you give")
            names.add(m["name"])
        for i, m in sorted(matched.items()):
            (themes_dir / m["name"]).write_bytes(m["zip"])
            manifest["themes"].append({"index": i, "zip": m["name"], "name": m["theme"]["name"],
                                       "description": m["theme"]["description"],
                                       "added": datetime.date.today().isoformat()})
    manifest["themes"].sort(key=lambda t: t["index"])
    manifest["next_index"] = max(manifest["next_index"], max(added) + 1)
    write_json(data / "manifest.json", manifest)
    write_json(data / "installed.json", {
        "title_id": f"{new['title_id']:016X}", "title_version": new["version"],
        "source": str(tmd_path), "source_sha256": hashlib.sha256(tmd).hexdigest(),
        "recorded": datetime.date.today().isoformat(),
        "contents": [chunk_json(c) for c in new["chunks"]], "tmd": tmd.hex()})
    for i, m in sorted(matched.items()):
        print(f"{i:4d}  {m['theme']['name']}  ({m['path'].name} -> themes/{m['name']})")
    print(f"adopted {tmd_path.name}: title {new['title_id']:016X} version {base['version']} -> {new['version']}, "
          f"contents {[c['index'] for c in new['chunks']]}; next index {manifest['next_index']}")


# ---------------------------------------------------------------- verify
# Everything below reads the pack back through ctrtool and parses it with its own code: nothing from the build
# section above is used except the settings and tool lookup, so a writer bug cannot hide behind a matching reader.

V_SIZES = {"body_LZ.bin": 0x150000, "bgm.bcstm": 0x337000}
V_LANGS = {1: "en", 2: "fr", 3: "de", 4: "it", 5: "es", 8: "nl", 9: "pt", 10: "ru"}
V_MAX_INDEX = 349  # HOME 11.17's theme list keeps a 350-byte row per variation (0x1fbcec)


def v_index_limit(indices, where, errors):
    high = [i for i in indices if i > V_MAX_INDEX]
    if high:
        errors.append(f"{where}: content index {', '.join(map(str, high))} above {V_MAX_INDEX}, which HOME cannot "
                      f"handle (one 350-byte row per variation)")


def v_c_string(raw):
    return raw.split(b"\0", 1)[0].decode("utf-8")


def v_tmd(raw, errors):
    sig_type = struct.unpack_from(">I", raw, 0)[0]
    body = {0x10003: 0x240, 0x10004: 0x140, 0x10005: 0x80}[sig_type]
    tid = struct.unpack_from(">Q", raw, body + 0x4C)[0]
    count = struct.unpack_from(">H", raw, body + 0x9E)[0]
    info, start = body + 0xC4, body + 0xC4 + 0x900
    chunks = []
    for i in range(count):
        cid, index, ctype, size = struct.unpack_from(">IHHQ", raw, start + 0x30 * i)
        sha = raw[start + 0x30 * i + 0x10:start + 0x30 * i + 0x30]
        chunks.append({"id": cid, "index": index, "type": ctype, "size": size, "sha256": sha.hex()})
    if hashlib.sha256(raw[info:info + 0x900]).digest() != raw[body + 0xA4:body + 0xC4]:
        errors.append("TMD: the info-record hash in the header is wrong")
    covered = 0
    for r in range(64):
        offset, n = struct.unpack_from(">HH", raw, info + 0x24 * r)
        if n:
            if hashlib.sha256(raw[start + 0x30 * offset:start + 0x30 * (offset + n)]).digest() != \
                    raw[info + 0x24 * r + 4:info + 0x24 * r + 0x24]:
                errors.append(f"TMD: info record {r} does not hash its content records")
            covered += n
    if covered != count:
        errors.append(f"TMD: info records cover {covered} of {count} content records")
    return tid, chunks


def v_ctrtool_lists(text):
    """The content indices ctrtool lists under the CIA header (EnabledContent) and the TMD (ContentInfo)."""
    def section(start, end):
        return text.split(start, 1)[1].split(end, 1)[0] if start in text else ""
    header = section("CiaHeader:", "Certificate Chain:").split("EnabledContent:", 1)[-1]
    enabled = [int(line.split("0x")[1], 16) for line in header.splitlines() if line.strip()[3:5] == "0x"]
    tmd = section("TitleMetaData:", "\nNCCH:").split("ContentInfo:", 1)[-1]
    listed = [int(line.strip()[3:9], 16) for line in tmd.splitlines() if line.rstrip().endswith(":")
              and line.strip()[3:5] == "0x"]
    return enabled, listed


def v_ticket_rights(raw):
    sig_type = struct.unpack_from(">I", raw, 0)[0]
    body = {0x10003: 0x240, 0x10004: 0x140, 0x10005: 0x80}[sig_type]
    tid = struct.unpack_from(">Q", raw, body + 0x9C)[0]
    section = body + 0x164
    seg_count, seg_size = struct.unpack_from(">II", raw, section + 0x18)
    rights = set()
    for s in range(seg_count):
        seg = section + 0x28 + s * seg_size
        level = struct.unpack_from(">I", raw, seg)[0]
        bits = raw[seg + 4:seg + 4 + 0x80]
        rights |= {level + i for i in range(0x400) if bits[i // 8] >> (i % 8) & 1}
    return tid, rights


def v_body_byte(body, offset):
    """One byte of the decompressed body, decoding only as far as needed (None if the LZ11 stream ends first)."""
    out, pos = bytearray(), 4 if body[1:4] != b"\0\0\0" else 8
    while len(out) <= offset and pos < len(body):
        flags, pos = body[pos], pos + 1
        for bit in range(8):
            if len(out) > offset or pos >= len(body):
                break
            if flags << bit & 0x80:
                b = body[pos:pos + 4]
                if b[0] >> 4 > 1:
                    length, disp, pos = (b[0] >> 4) + 1, (b[0] & 0xF) << 8 | b[1], pos + 2
                elif b[0] >> 4 == 1:
                    length = ((b[0] & 0xF) << 12 | b[1] << 4 | b[2] >> 4) + 0x111
                    disp, pos = (b[2] & 0xF) << 8 | b[3], pos + 4
                else:
                    length, disp, pos = ((b[0] & 0xF) << 4 | b[1] >> 4) + 0x11, (b[1] & 0xF) << 8 | b[2], pos + 3
                for _ in range(length):
                    out.append(out[-disp - 1] if disp < len(out) else 0)
            else:
                out.append(body[pos])
                pos += 1
    return out[offset] if len(out) > offset else None


def v_files(root):
    return sorted(str(p.relative_to(root)) for p in root.rglob("*") if p.is_file())


def verify(data, bin_dir):
    data = Path(data)
    ctrtool = tool("ctrtool", data, bin_dir)
    cia = data / "out" / "theme-pack.cia"
    manifest_path = data / "manifest.json"
    if not cia.is_file() or not manifest_path.is_file():
        raise PackError(f"need {cia} and {manifest_path}: run build first")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    errors = []
    expect_tid = int(manifest["title_id"], 16)
    if expect_tid >> 8 != 0x0004008C000098 or expect_tid & 0xFF > 9:
        errors.append(f"manifest title {manifest['title_id']} is not an EUR theme variation 0004008C00009800..09")
    themes_dir = data / "themes"
    present = {t["index"]: t for t in manifest["themes"] if (themes_dir / t["zip"]).is_file()}
    listed = {t["zip"] for t in manifest["themes"]}
    for extra in sorted(p.name for p in themes_dir.glob("*.zip") if p.name not in listed):
        errors.append(f"{extra} is in themes/ but not in the manifest: run build")

    with tempfile.TemporaryDirectory(prefix="theme-pack-verify-") as tmp:
        tmp = Path(tmp)
        run([ctrtool, f"--contents={tmp / 'c'}", f"--tmd={tmp / 'tmd.bin'}", f"--tik={tmp / 'tik.bin'}", cia])
        tid, chunks = v_tmd((tmp / "tmd.bin").read_bytes(), errors)
        enabled, listed = v_ctrtool_lists(run([ctrtool, "-i", cia]))
        tik_tid, rights = v_ticket_rights((tmp / "tik.bin").read_bytes())
        if tid != expect_tid or tik_tid != expect_tid:
            errors.append(f"title ID: TMD {tid:016X}, ticket {tik_tid:016X}, manifest {manifest['title_id']}")
        indices = [c["index"] for c in chunks]
        v_index_limit(indices, "TMD", errors)
        v_index_limit([t["index"] for t in manifest["themes"]], "manifest", errors)
        if not (indices == enabled == listed):
            errors.append(f"content indices: TMD {indices}, ctrtool CIA header {enabled}, ctrtool TMD {listed}")
        if indices != sorted([0, *present]):
            errors.append(f"content indices {indices}, expected {sorted([0, *present])} (manifest entries whose zip "
                          f"is in themes/)")
        if not set(range(0x400)) <= rights:
            errors.append(f"ticket rights miss {len(set(range(0x400)) - rights)} indices of 0..0x3FF")
        content_dirs = {}
        for c in chunks:
            path = tmp / f"c.{c['index']:04x}.{c['id']:08x}"
            if not path.is_file():
                errors.append(f"content {c['index']}: ctrtool did not extract {path.name}")
                continue
            raw = path.read_bytes()
            if hashlib.sha256(raw[:c["size"]]).hexdigest() != c["sha256"]:
                errors.append(f"content {c['index']}: SHA-256 does not match the TMD")
            if c["index"] and not c["type"] & 0x4000:
                errors.append(f"content {c['index']}: not marked optional (DLC) in the TMD")
            out = tmp / f"x{c['index']}"
            (out / "exefs").mkdir(parents=True)  # ctrtool writes ExeFS files only into an existing folder
            run([ctrtool, f"--romfsdir={out / 'romfs'}", f"--exefsdir={out / 'exefs'}", path])
            content_dirs[c["index"]] = out

        top = max(t["index"] for t in manifest["themes"])
        names = v_content0(content_dirs.get(0), present, top, errors)
        sizes = {}
        for index, entry in sorted(present.items()):
            if index in content_dirs:
                sizes[index] = v_theme(content_dirs[index], entry, themes_dir / entry["zip"], content_dirs.get(0),
                                       errors)
    if errors:
        for e in errors:
            print(f"MISMATCH {e}", file=sys.stderr)
        raise PackError(f"verify failed: {len(errors)} mismatch(es) in {cia}")
    for entry in manifest["themes"]:
        if entry["index"] in present:
            print(f"{entry['index']:4d}  {names[entry['index']]}  {sizes[entry['index']]}")
        else:
            print(f"{entry['index']:4d}  (reserved, not in the pack: {entry['zip']})")
    print(f"OK {cia}: title {expect_tid:016X}, {len(present)} themes, contents {sorted([0, *present])}")


def v_content0(out, present, top, errors):
    """The catalogue names by index, as HOME will read them."""
    if out is None:
        errors.append("content 0 missing")
        return {}
    romfs = out / "romfs"
    files = v_files(romfs)
    expected = sorted(["MetaDataContentHeader.bin", *(f"ContentInfoArchive_EUR_{c}.bin" for c in V_LANGS.values()),
                       *(f"icons/{i}.icn" for i in range(1, top + 1))])
    if files != expected:
        errors.append(f"content 0 files: extra {sorted(set(files) - set(expected))}, "
                      f"missing {sorted(set(expected) - set(files))}")
        return {}
    if not (out / "exefs" / "icon.bin").is_file():
        errors.append("content 0 has no ExeFS icon (Data Management shows the pack by it)")
    head = (romfs / "MetaDataContentHeader.bin").read_bytes()
    first, enabled, icons, _, magic = struct.unpack_from("<5I", head, 0)
    regions = [struct.unpack_from("<2H", head, 0x14 + 4 * i) for i in range(7)]
    langs = [struct.unpack_from("<2H", head, 0x34 + 4 * i) for i in range(struct.unpack_from("<I", head, 0x30)[0])]
    if magic != 0xDEADBEEF or first != 1 or enabled != top or icons != top:
        errors.append(f"MetaDataContentHeader: magic {magic:#x}, counts {first}/{enabled}/{icons}, expected 1/{top}/{top}")
    if (2, 1) not in regions or sorted(langs) != sorted((2, lang) for lang in V_LANGS):
        errors.append(f"MetaDataContentHeader: regions {regions}, languages {langs}")
    archives = {c: (romfs / f"ContentInfoArchive_EUR_{c}.bin").read_bytes() for c in V_LANGS.values()}
    if len(set(archives.values())) != 1:
        errors.append("the eight ContentInfoArchive files differ")
    cat = archives["en"]
    version, count = struct.unpack_from("<2I", cat, 0)
    if version != 1 or count != top or len(cat) != 8 + 0xC8 * (top + 1):
        errors.append(f"ContentInfoArchive header {version}/{count}, size {len(cat):#x}, expected 1/{top}")
        return {}
    names = {}
    for i in range(top + 1):
        e = cat[8 + 0xC8 * i:8 + 0xC8 * (i + 1)]
        try:
            name, desc = v_c_string(e[:0x40]), v_c_string(e[0x40:0xC0])
        except UnicodeDecodeError:
            errors.append(f"catalogue entry {i}: text is not UTF-8")
            continue
        index, on = struct.unpack_from("<2I", e, 0xC0)
        if index != i or on != (0 if i == 0 else 1):
            errors.append(f"catalogue entry {i}: index {index}, enabled {on}")
        if i in present:
            want = present[i]
            if not (name and v_cut_from(name, want["name"], 0x40) and v_cut_from(desc, want["description"], 0x80)):
                errors.append(f"catalogue entry {i}: {name!r} / {desc!r}, manifest {want['name']!r} / "
                              f"{want['description']!r}")
            names[i] = name
    return names


def v_cut_from(text, full, field):
    """text is full, or full cut short only because it did not fit the field (a UTF-8 character is <= 4 bytes)."""
    return text == full or (full.startswith(text) and len(text.encode("utf-8")) > field - 1 - 4)


def v_theme(out, entry, zip_path, content0, errors):
    index = entry["index"]
    romfs = out / "romfs"
    files = v_files(romfs)
    if "body_LZ.bin" not in files or set(files) - set(V_SIZES):
        errors.append(f"content {index}: files {files}, expected body_LZ.bin and optionally bgm.bcstm")
        return ""
    if (out / "exefs").exists() and v_files(out / "exefs"):
        errors.append(f"content {index}: has an ExeFS, a theme content holds only a RomFS")
    with zipfile.ZipFile(zip_path) as z:
        names = set(z.namelist())
        for name, limit in V_SIZES.items():
            packed = (romfs / name).read_bytes() if name in files else None
            source = z.read(name) if name in names else None
            if packed != source:
                errors.append(f"content {index}: {name} differs from {zip_path.name}")
            if packed is not None and len(packed) > limit:
                errors.append(f"content {index}: {name} is {len(packed):#x} bytes, limit {limit:#x}")
        body = (romfs / "body_LZ.bin").read_bytes()
        if body[:1] != b"\x11":
            errors.append(f"content {index}: body_LZ.bin does not start with the LZ11 marker 0x11")
        decoded = int.from_bytes(body[1:4], "little") or int.from_bytes(body[4:8], "little")
        if decoded > V_SIZES["body_LZ.bin"]:
            errors.append(f"content {index}: body_LZ.bin decodes to {decoded:#x} bytes, limit "
                          f"{V_SIZES['body_LZ.bin']:#x}")
        icon = content0 / "romfs" / "icons" / f"{index}.icn" if content0 else None
        if icon and icon.is_file() and "info.smdh" in names:
            if icon.read_bytes() != z.read("info.smdh")[0x24C0:0x36C0]:
                errors.append(f"icons/{index}.icn is not the large icon of {zip_path.name}'s info.smdh")
    bgm = romfs / "bgm.bcstm"
    flag = "on" if v_body_byte(body, 0x5) else "off"  # 0x5: use bgm.bcstm (3dbrew Home Menu/Themes)
    music = f"bgm {bgm.stat().st_size:#x}" if bgm.is_file() else "no bgm"
    return f"body {len(body):#x}  {music}, flag {flag}"


def v_tmd_version(raw):
    body = {0x10003: 0x240, 0x10004: 0x140, 0x10005: 0x80}[struct.unpack_from(">I", raw, 0)[0]]
    return struct.unpack_from(">H", raw, body + 0x9C)[0]


def v_header_diff(base, new):
    """Byte offsets where two TMDs differ in signature or header, outside title version, content count and the
    info-record hash (the only fields a delta may change)."""
    body = {0x10003: 0x240, 0x10004: 0x140, 0x10005: 0x80}.get(struct.unpack_from(">I", new, 0)[0], 0x140)
    end = body + 0xC4
    if len(base) < end or len(new) < end:
        return [min(len(base), len(new))]
    free = set(range(body + 0x9C, body + 0xA0)) | set(range(body + 0xA4, end))
    return [i for i in range(end) if i not in free and base[i] != new[i]]


def v_extract(ctrtool, path, out):
    (out / "exefs").mkdir(parents=True)  # ctrtool writes ExeFS files only into an existing folder
    run([ctrtool, f"--romfsdir={out / 'romfs'}", f"--exefsdir={out / 'exefs'}", path])
    return out


def verify_delta(data, bin_dir, base_cia):
    """Checks out/delta against installed.json and the manifest: what the console holds after the import must be the
    installed contents unchanged plus a new content 0 and the new themes, each matching the new TMD. With base_cia
    (the CIA that installed.json was recorded from) the kept contents are checked too, through ctrtool."""
    data = Path(data)
    ctrtool = tool("ctrtool", data, bin_dir)
    out, record_path, manifest_path = data / "out" / "delta", data / "installed.json", data / "manifest.json"
    for path in (out / "tmd.bin", record_path, manifest_path):
        if not path.is_file():
            raise PackError(f"need {path}: record the installed pack and run delta first")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    record = json.loads(record_path.read_text(encoding="utf-8"))
    errors = []
    base_raw, new_raw = bytes.fromhex(record["tmd"]), (out / "tmd.bin").read_bytes()
    base_tid, base_chunks = v_tmd(base_raw, errors)
    new_tid, new_chunks = v_tmd(new_raw, errors)
    expect_tid = int(manifest["title_id"], 16)
    if not base_tid == new_tid == expect_tid:
        errors.append(f"title ID: delta TMD {new_tid:016X}, installed {base_tid:016X}, manifest {manifest['title_id']}")
    diff = v_header_diff(base_raw, new_raw)
    if diff:
        errors.append(f"TMD header differs from the installed one at byte {', '.join(f'{i:#x}' for i in diff[:8])} "
                      f"(only the title version, the content count and the hashes may change)")
    base_version, new_version = v_tmd_version(base_raw), v_tmd_version(new_raw)
    if new_version != base_version + 1:
        errors.append(f"title version {new_version}, expected the installed {base_version} + 1")
    base_by = {c["index"]: c for c in base_chunks}
    new_by = {c["index"]: c for c in new_chunks}
    indices = [c["index"] for c in new_chunks]
    kept = sorted(i for i in base_by if i)
    for i in kept:
        if new_by.get(i) != base_by[i]:
            errors.append(f"installed content {i} is not carried over unchanged (ID, type, size, hash): "
                          f"{base_by[i]} -> {new_by.get(i)}")
    if 0 not in new_by:
        errors.append("the delta TMD has no content 0")
    elif new_by[0]["id"] in {c["id"] for c in base_chunks}:
        errors.append(f"content 0 has content ID {new_by[0]['id']:08x}, which an installed content already has")
    themes_dir = data / "themes"
    present = {t["index"]: t for t in manifest["themes"] if (themes_dir / t["zip"]).is_file()}
    expected = sorted({0, *base_by, *present})
    if indices != expected:
        errors.append(f"delta TMD contents {indices}, expected {expected} (the installed ones and every theme in "
                      f"themes/, in index order)")
    v_index_limit(indices, "delta TMD", errors)
    imports = [i for i in indices if i == 0 or i not in base_by]
    files = sorted(p.name for p in out.iterdir())
    want = sorted(["tmd.bin", "delta.json", *(f"{i}.app" for i in imports)])
    if files != want:
        errors.append(f"files in {out}: {files}, expected {want}")

    with tempfile.TemporaryDirectory(prefix="theme-pack-verify-delta-") as tmp:
        tmp = Path(tmp)
        dirs = {}
        for i in imports:
            path, c = out / f"{i}.app", new_by[i]
            if not path.is_file():
                continue
            raw = path.read_bytes()
            if len(raw) != c["size"] or hashlib.sha256(raw).hexdigest() != c["sha256"]:
                errors.append(f"{path.name}: size or SHA-256 does not match the delta TMD")
                continue
            if c["type"] & 0x1:
                errors.append(f"content {i} is marked encrypted in the TMD, the file is a plain NCCH")
            if i and not c["type"] & 0x4000:
                errors.append(f"content {i}: not marked optional (DLC) in the TMD")
            dirs[i] = v_extract(ctrtool, path, tmp / f"x{i}")
        if base_cia is not None:
            run([ctrtool, f"--contents={tmp / 'c'}", f"--tmd={tmp / 'base-tmd.bin'}", base_cia])
            if (tmp / "base-tmd.bin").read_bytes() != base_raw:
                errors.append(f"{base_cia}: its TMD is not the one installed.json records")
            for i in kept:
                path = tmp / f"c.{i:04x}.{base_by[i]['id']:08x}"
                if not path.is_file():
                    errors.append(f"content {i}: ctrtool did not extract it from {base_cia}")
                    continue
                if hashlib.sha256(path.read_bytes()[:base_by[i]["size"]]).hexdigest() != new_by.get(i, {}).get("sha256"):
                    errors.append(f"content {i} of {base_cia} does not match the delta TMD")
                if i in present:
                    dirs[i] = v_extract(ctrtool, path, tmp / f"x{i}")
        top = max(t["index"] for t in manifest["themes"])
        names = v_content0(dirs.get(0), present, top, errors)
        for i, entry in sorted(present.items()):
            if i in dirs:
                v_theme(dirs[i], entry, themes_dir / entry["zip"], dirs.get(0), errors)
    if errors:
        for e in errors:
            print(f"MISMATCH {e}", file=sys.stderr)
        raise PackError(f"verify-delta failed: {len(errors)} mismatch(es) in {out}")
    for i in indices:
        name = "catalogue" if i == 0 else names.get(i, "(not in themes/)")
        print(f"{i:4d}  {'import' if i in imports else 'kept  '}  {name}")
    print(f"OK {out}: title {expect_tid:016X} version {base_version} -> {new_version}, import {imports}, keep {kept}"
          + (f", kept contents checked in {Path(base_cia).name}" if base_cia else ""))


# ---------------------------------------------------------------- command line

def main(argv=None):
    parser = argparse.ArgumentParser(description=(__doc__ or "Build and verify a 3DS theme DLC pack.").split("\n\n")[0])
    sub = parser.add_subparsers(dest="command", required=True)
    default = setting("THEME_PACK_DATA")
    for name in ("build", "verify", "installed", "delta", "verify-delta", "adopt"):
        p = sub.add_parser(name)
        p.add_argument("--data", default=default, required=default is None, metavar="DIR",
                       help="data dir with themes/, manifest.json, out/, bin/ (default: THEME_PACK_DATA)")
        p.add_argument("--bin", metavar="DIR", help="folder with makerom and ctrtool (default: DIR/bin)")
    sub.choices["build"].add_argument("--variation", type=int, metavar="N",
                                      help="theme DLC variation 0 to 9 (default: the manifest's, else 9)")
    sub.choices["installed"].add_argument("source", metavar="CIA_OR_DELTA",
                                          help="the CIA just installed with FBI, or the delta dir just imported")
    sub.choices["verify-delta"].add_argument("--base-cia", metavar="CIA",
                                             help="the CIA installed.json was recorded from: also check the kept "
                                                  "contents")
    sub.choices["adopt"].add_argument("--tmd", required=True, metavar="INSTALLED_TMD",
                                      help="the installer's record, copied off the card (sdmc:/theme-pack/installed.tmd)")
    sub.choices["adopt"].add_argument("files", nargs="+", metavar="FILE",
                                      help="the theme zips or theme installer CIAs the installer installed from")
    args = parser.parse_args(argv)
    try:
        if args.command == "build":
            build(args.data, args.variation, args.bin)
        elif args.command == "verify":
            verify(args.data, args.bin)
        elif args.command == "installed":
            installed(args.data, args.source)
        elif args.command == "delta":
            delta(args.data, args.bin)
        elif args.command == "adopt":
            adopt(args.data, args.bin, args.tmd, args.files)
        else:
            verify_delta(args.data, args.bin, args.base_cia)
    except PackError as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
