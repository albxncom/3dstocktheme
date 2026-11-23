#!/usr/bin/env python3
"""An independent reader for the banner's CGFX: walks the written file and refuses it
(exit 1, with the reason) when something the HOME Menu is reported to need is missing.

  python3 -I check_cgfx.py [--anim yes|no] [--frames N] [--json] <file.cgfx>

Standard library only; shares no code with the writer (pycgfx). The layout was taken
from pycgfx's struct definitions and cgfx.hexpat as format documentation.

What it walks:
  - the header (magic, byte order mark, header size, the file size field, the block
    count), the DATA block and, if there, the IMAG block and their sizes;
  - every dictionary: magic, byte size, count, and every name looked up through the
    tree the way a lookup walks it (from the root's left child, down while the
    reference bit falls, then a compare of the name);
  - every pointer it follows lies inside the file, every string ends inside it,
    every data blob lies inside IMAG;
  - one model named COMMON with a skeleton; bones (index, parent, links, billboard
    mode); meshes (shape and material index, owner, a mesh node name that is not
    empty and names a bone); shapes (rigid, index and vertex streams, indices in
    range, colours in 0..1); materials (the shader is a reference to DefaultShader,
    lookup tables resolve; the shader's name and user data);
  - every animation group's members: path, object name, blend operation name and,
    where the field type has one, the parent name;
  - at most one skeletal animation, named COMMON, bound to the model's skeletal
    animation group, its members ordinary Transform members whose bones resolve,
    their curves and keys inside the loop;
  - textures (T10 and T11; the earlier files have none): every texture in DATA an
    image texture (TXOB type 0x20000011) of the name its dictionary gives, a known
    format other than RGBA8 and ETC, sides powers of two from 8 to 1024, one level,
    its image's sides the same, its data blob inside IMAG with exactly the bytes the
    sides and format take, starting on a multiple of 128 bytes in the file (the
    project's rule after a disputed report); every texture mapper of a material:
    its texture a reference (type 0x20000004) whose name is found through the
    textures' dictionary and whose pointer is that entry, its sampler's owner the
    mapper; a textured mesh's shape has texture coordinates (two floats);
    the material's blend, depth and alpha-test settings are read and reported;
  - the size limit 0x80000.
What it cannot know is in README.md: it proves structure, not that the HOME Menu
accepts the file.
"""
import json
import math
import struct
import sys

MAX_SIZE = 0x80000
DATA_DICTS = ("models", "textures", "lookup_tables", "materials", "shaders", "cameras",
              "lights", "fogs", "scenes", "skeletal_animations", "material_animations",
              "visibility_animations", "camera_animations", "light_animations", "emitters")
SLOTS = ("scale_x", "scale_y", "scale_z", "rot_x", "rot_y", "rot_z", "pos_x", "pos_y", "pos_z")
CONST_BITS = (0x40, 0x80, 0x100, 0x200, 0x400, 0x800, 0x2000, 0x4000, 0x8000)
IGNORE_BITS = (0x10000, 0x20000, 0x40000, 0x80000, 0x100000, 0x200000,
               0x800000, 0x1000000, 0x2000000)
SLOT_OFFSETS = (20, 24, 28, 32, 36, 40, 48, 52, 56)
TYPE_WIDTH = {0x1400: 1, 0x1401: 1, 0x1402: 2, 0x1403: 2, 0x1406: 4}
TEXTURE_FORMATS = {1: ("RGB8", 3), 2: ("RGBA5551", 2), 3: ("RGB565", 2), 4: ("RGBA4", 2), 5: ("LA8", 2),
                   6: ("HILO8", 2), 7: ("L8", 1), 8: ("A8", 1), 9: ("LA4", 1), 10: ("L4", 0.5), 11: ("A4", 0.5)}
TEXTURE_ALIGN = 128
BLEND_FUNCTIONS = ("Zero", "One", "SrcColor", "InvSrcColor", "DstColor", "InvDstColor", "SrcAlpha", "InvSrcAlpha",
                   "DstAlpha", "InvDstAlpha", "ConstColor", "InvConstColor", "ConstAlpha", "InvConstAlpha",
                   "SrcAlphaSaturate", "?15")
USAGE = {0: "position", 1: "normal", 2: "tangent", 3: "color", 4: "texcoord0", 5: "texcoord1",
         6: "texcoord2", 7: "bone_index", 8: "bone_weight"}


class CheckError(Exception):
    pass


def lookup(nodes, name):
    """The index of `name` in a dictionary's node list, walked as a lookup does, or
    None. nodes: (reference bit, left, right, name) with the root at 0."""
    key = name.encode("utf-8")
    parent = nodes[0]
    index = parent[1]
    for _ in range(len(nodes) + 1):
        if index >= len(nodes):
            return None
        node = nodes[index]
        if parent[0] <= node[0]:
            return index if index != 0 and node[3] == name else None
        bit = node[0]
        byte = bit >> 3
        one = byte < len(key) and (key[byte] >> (bit & 7)) & 1
        parent = node
        index = node[2] if one else node[1]
    return None


class Reader:
    def __init__(self, data):
        self.d = data
        self.imag = None      # (start, end) of the IMAG payload
        self.notes = []
        self.dicts = 0
        self.names = 0

    def need(self, off, size, what):
        if off < 0 or off + size > len(self.d):
            raise CheckError("%s at 0x%X (+%d) lies outside the file (%d bytes)"
                             % (what, off, size, len(self.d)))

    def u8(self, off, what="byte"):
        self.need(off, 1, what)
        return self.d[off]

    def u16(self, off, what="u16"):
        self.need(off, 2, what)
        return struct.unpack_from("<H", self.d, off)[0]

    def u32(self, off, what="u32"):
        self.need(off, 4, what)
        return struct.unpack_from("<I", self.d, off)[0]

    def s32(self, off, what="s32"):
        self.need(off, 4, what)
        return struct.unpack_from("<i", self.d, off)[0]

    def f32(self, off, what="float"):
        self.need(off, 4, what)
        value = struct.unpack_from("<f", self.d, off)[0]
        if not math.isfinite(value):
            raise CheckError("%s at 0x%X is not a finite number" % (what, off))
        return value

    def floats(self, off, count, what="floats"):
        return [self.f32(off + 4 * i, what) for i in range(count)]

    def magic(self, off, want, what):
        self.need(off, 4, what)
        got = self.d[off:off + 4]
        if got != want:
            raise CheckError("%s at 0x%X: magic %r, expected %r" % (what, off, got, want))

    def ptr(self, off, what, size=4):
        """A self-relative pointer; None when null. The target must hold `size` bytes."""
        rel = self.s32(off, what)
        if rel == 0:
            return None
        target = off + rel
        if target < 0x14:
            raise CheckError("%s at 0x%X points to 0x%X, before the DATA block" % (what, off, target))
        self.need(target, size, what + " target")
        return target

    def string(self, off, what):
        """The string a pointer names; None when the pointer is null."""
        target = self.ptr(off, what, 1)
        if target is None:
            return None
        end = self.d.find(b"\0", target)
        if end < 0:
            raise CheckError("%s at 0x%X: the string does not end inside the file" % (what, target))
        try:
            return self.d[target:end].decode("utf-8")
        except UnicodeDecodeError:
            raise CheckError("%s at 0x%X: the string is not text" % (what, target))

    def blob(self, off, what):
        """(size, start) of a data blob: a size and a pointer; it must lie in IMAG."""
        size = self.u32(off, what + " size")
        if size == 0:
            return 0, None
        start = self.ptr(off + 4, what, size)
        if start is None:
            raise CheckError("%s at 0x%X: %d bytes but a null pointer" % (what, off, size))
        if self.imag is None or start < self.imag[0] or start + size > self.imag[1]:
            raise CheckError("%s at 0x%X lies outside the IMAG block" % (what, start))
        return size, start

    def pointer_list(self, off, what, size=4):
        """A count and a pointer to that many pointers; the targets."""
        count = self.u32(off, what + " count")
        if count == 0:
            return []
        if count > 0x10000:
            raise CheckError("%s: count %d is not believable" % (what, count))
        table = self.ptr(off + 4, what + " table", 4 * count)
        if table is None:
            raise CheckError("%s: count %d but a null pointer" % (what, count))
        out = []
        for i in range(count):
            target = self.ptr(table + 4 * i, "%s[%d]" % (what, i), size)
            if target is None:
                raise CheckError("%s[%d] is null" % (what, i))
            out.append(target)
        return out

    def int_list(self, off, what):
        count = self.u32(off, what + " count")
        if count == 0:
            return []
        if count > 0x10000:
            raise CheckError("%s: count %d is not believable" % (what, count))
        table = self.ptr(off + 4, what + " table", 4 * count)
        if table is None:
            raise CheckError("%s: count %d but a null pointer" % (what, count))
        return [self.s32(table + 4 * i) for i in range(count)]

    def dictionary(self, off, what, size=4):
        """A count and a pointer to a DICT: [(name, data offset)], and the node list
        for later lookups. Every name must be found through the tree."""
        count = self.u32(off, what + " count")
        if count == 0:
            return [], [(0xFFFFFFFF, 0, 0, None)]
        if count > 0xFFFE:
            raise CheckError("%s: count %d is not believable" % (what, count))
        at = self.ptr(off + 4, what, 12 + 16 * (count + 1))
        if at is None:
            raise CheckError("%s: count %d but a null pointer" % (what, count))
        self.magic(at, b"DICT", what)
        if self.u32(at + 4) != 12 + 16 * (count + 1):
            raise CheckError("%s at 0x%X: byte size %d, expected %d for %d entries"
                             % (what, at, self.u32(at + 4), 12 + 16 * (count + 1), count))
        if self.u32(at + 8) != count:
            raise CheckError("%s at 0x%X: count %d inside, %d outside" % (what, at, self.u32(at + 8), count))
        nodes, entries = [], []
        for i in range(count + 1):
            n = at + 12 + 16 * i
            ref, left, right = self.u32(n), self.u16(n + 4), self.u16(n + 6)
            if left > count or right > count:
                raise CheckError("%s at 0x%X: node %d links to %d/%d, past the last node %d"
                                 % (what, at, i, left, right, count))
            if i == 0:
                name = None
                if ref != 0xFFFFFFFF:
                    raise CheckError("%s at 0x%X: the root's reference bit is 0x%X" % (what, at, ref))
            else:
                name = self.string(n + 8, "%s name %d" % (what, i))
                if name is None:
                    raise CheckError("%s at 0x%X: entry %d has no name" % (what, at, i))
                target = self.ptr(n + 12, "%s[%r]" % (what, name), size)
                if target is None:
                    raise CheckError("%s at 0x%X: entry %r has no data" % (what, at, name))
                entries.append((name, target))
            nodes.append((ref, left, right, name))
        names = [e[0] for e in entries]
        if len(set(names)) != len(names):
            raise CheckError("%s at 0x%X: a name is there twice: %r" % (what, at, names))
        for i, name in enumerate(names):
            found = lookup(nodes, name)
            if found != i + 1:
                raise CheckError("%s at 0x%X: the lookup of %r ends at %s, not at its entry %d"
                                 % (what, at, name, "nothing" if found is None else "entry %d" % found, i + 1))
        self.dicts += 1
        self.names += count
        return entries, nodes


def read_groups(r, off, what):
    """The animation group dictionary of a model or light: {name: (member type, nodes)}."""
    groups = {}
    entries, nodes = r.dictionary(off, what + " animation groups", 36)
    for name, at in entries:
        if r.string(at + 8, "animation group name") != name:
            raise CheckError("animation group %r at 0x%X carries another name" % (name, at))
        members, member_nodes = r.dictionary(at + 16, "animation group %r members" % name, 44)
        for key, m in members:
            what = "animation group %r member %r" % (name, key)
            if r.string(m + 4, "member path") is None:
                raise CheckError("animation group %r: member %r has no path" % (name, key))
            r.string(m + 8, what + " object name")
            r.string(m + 12, what + " blend operation name")
            # pycgfx's writer: field types 0 to 5 end in a parent name and a field
            # index (48 bytes); the others in a parent index (44 bytes)
            if r.u32(m + 28, what + " field type") <= 5:
                r.need(m, 48, what)
                r.string(m + 40, what + " parent name")
        r.int_list(at + 24, "animation group %r blend operations" % name)
        groups[name] = {"member_type": r.u32(at + 12), "members": len(members),
                        "nodes": member_nodes}
    return groups, nodes


def read_skeleton(r, at):
    if r.u32(at) != 0x02000000:
        raise CheckError("skeleton at 0x%X: type 0x%X" % (at, r.u32(at)))
    r.magic(at + 4, b"SOBJ", "skeleton")
    r.dictionary(at + 16, "skeleton user data")
    entries, nodes = r.dictionary(at + 24, "bones", 224)
    offsets = [e[1] for e in entries]
    bones = []
    for i, (name, b) in enumerate(entries):
        if r.string(b, "bone name") != name:
            raise CheckError("bone %r at 0x%X carries another name" % (name, b))
        joint, parent = r.s32(b + 8), r.s32(b + 12)
        if joint != i:
            raise CheckError("bone %r: index %d inside, entry %d in the dictionary" % (name, joint, i))
        if parent < -1 or parent >= len(entries):
            raise CheckError("bone %r: parent index %d" % (name, parent))
        links = []
        for k, label in enumerate(("parent", "child", "previous", "next")):
            target = r.ptr(b + 16 + 4 * k, "bone %r %s" % (name, label), 224)
            if target is not None and target not in offsets:
                raise CheckError("bone %r: its %s pointer does not name a bone" % (name, label))
            links.append(None if target is None else offsets.index(target))
        if links[0] != (None if parent == -1 else parent):
            raise CheckError("bone %r: parent pointer and parent index differ" % name)
        scale, rotation, position = r.floats(b + 32, 3), r.floats(b + 44, 3), r.floats(b + 56, 3)
        if min(abs(s) for s in scale) < 0.01:
            raise CheckError("bone %r: scale %r is (nearly) nothing" % (name, scale))
        mode = r.u32(b + 212)
        if mode > 6:
            raise CheckError("bone %r: billboard mode %d" % (name, mode))
        bones.append({"name": name, "parent": parent, "flags": r.u32(b + 4), "scale": scale,
                      "rotation": rotation, "position": position, "billboard": mode})
    root = r.ptr(at + 32, "root bone", 224)
    if root is None or root not in offsets or bones[offsets.index(root)]["parent"] != -1:
        raise CheckError("skeleton at 0x%X: the root bone pointer does not name a parentless bone" % at)
    if sum(1 for b in bones if b["parent"] == -1) != 1:
        raise CheckError("skeleton: not exactly one parentless bone")
    return bones, nodes


def read_shape(r, at, bone_count):
    if r.u32(at) != 0x10000001:
        raise CheckError("shape at 0x%X: type 0x%X" % (at, r.u32(at)))
    r.magic(at + 4, b"SOBJ", "shape")
    name = r.string(at + 12, "shape name")
    r.dictionary(at + 16, "shape user data")
    r.ptr(at + 28, "shape bounding box", 64)
    streams, vertex_count = {}, None
    for s in r.pointer_list(at + 56, "shape %r vertex attributes" % name, 52):
        kind = r.u32(s)
        if kind != 0x40000001:
            raise CheckError("shape %r: vertex attribute type 0x%X is not a plain stream" % (name, kind))
        usage, fmt, comps = r.u32(s + 4), r.u32(s + 36), r.u32(s + 40)
        if usage in (7, 8):
            raise CheckError("shape %r has bone streams (skinning)" % name)
        if usage not in USAGE or fmt not in TYPE_WIDTH or not 1 <= comps <= 4:
            raise CheckError("shape %r: stream usage %d, format 0x%X, %d components" % (name, usage, fmt, comps))
        size, start = r.blob(s + 20, "shape %r %s data" % (name, USAGE[usage]))
        step = TYPE_WIDTH[fmt] * comps
        if size == 0 or size % step:
            raise CheckError("shape %r: %s data of %d bytes, %d a vertex" % (name, USAGE[usage], size, step))
        if vertex_count not in (None, size // step):
            raise CheckError("shape %r: streams of %d and %d vertices" % (name, vertex_count, size // step))
        vertex_count = size // step
        if USAGE[usage] in streams:
            raise CheckError("shape %r: two %s streams" % (name, USAGE[usage]))
        streams[USAGE[usage]] = (fmt, comps, start, r.f32(s + 44, "stream scale"))
    if "position" not in streams or streams["position"][:2] != (0x1406, 3):
        raise CheckError("shape %r: no position stream of three floats" % name)
    start = streams["position"][2]
    pos = struct.unpack_from("<%df" % (3 * vertex_count), r.d, start)
    if not all(math.isfinite(v) for v in pos):
        raise CheckError("shape %r: a position is not a finite number" % name)
    lo = [min(pos[i::3]) for i in range(3)]
    hi = [max(pos[i::3]) for i in range(3)]
    if "color" in streams:
        fmt, comps, start, scale = streams["color"]
        if fmt != 0x1406:
            raise CheckError("shape %r: colours are not floats (pycgfx writes scale 1)" % name)
        col = struct.unpack_from("<%df" % (comps * vertex_count), r.d, start)
        if not all(0.0 <= v <= 1.0 for v in col):
            raise CheckError("shape %r: a colour lies outside 0..1" % name)
    triangles, bones = 0, []
    sets = r.pointer_list(at + 44, "shape %r primitive sets" % name, 20)
    if not sets:
        raise CheckError("shape %r has no primitive set" % name)
    for ps in sets:
        related = r.int_list(ps, "shape %r related bones" % name)
        if any(b < 0 or b >= bone_count for b in related):
            raise CheckError("shape %r: related bones %r, there are %d bones" % (name, related, bone_count))
        if r.u32(ps + 8) != 0 or len(related) != 1:
            raise CheckError("shape %r is skinned (mode %d, %d bones)" % (name, r.u32(ps + 8), len(related)))
        bones += related
        for p in r.pointer_list(ps + 12, "shape %r primitives" % name, 24):
            for ix in r.pointer_list(p, "shape %r index streams" % name, 44):
                fmt = r.u32(ix)
                if fmt not in (0x1401, 0x1403):
                    raise CheckError("shape %r: index type 0x%X" % (name, fmt))
                size, start = r.blob(ix + 8, "shape %r indices" % name)
                width = TYPE_WIDTH[fmt]
                if size == 0 or size % (3 * width):
                    raise CheckError("shape %r: %d index bytes are no triangle list" % (name, size))
                idx = struct.unpack_from("<%d%s" % (size // width, "B" if width == 1 else "H"), r.d, start)
                if max(idx) >= vertex_count:
                    raise CheckError("shape %r: index %d, %d vertices" % (name, max(idx), vertex_count))
                triangles += len(idx) // 3
            r.int_list(p + 8, "shape %r buffer objects" % name)
    if not triangles:
        raise CheckError("shape %r has no triangle" % name)
    if "texcoord0" in streams:
        fmt, comps, start, scale = streams["texcoord0"]
        if fmt == 0x1406 and not all(math.isfinite(v) for v in struct.unpack_from("<%df" % (comps * vertex_count), r.d, start)):
            raise CheckError("shape %r: a texture coordinate is not a finite number" % name)
    return {"name": name, "vertices": vertex_count, "triangles": triangles, "bone": bones[0],
            "streams": sorted(streams), "formats": [streams[k][:2] for k in sorted(streams)], "min": lo, "max": hi}


def read_texture(r, at, key):
    """A texture in DATA: (facts). Only image textures with one level, in a format this pipeline can mean."""
    r.need(at, 60, "texture %r" % key)
    kind = r.u32(at)
    if kind != 0x20000011:
        raise CheckError("texture %r at 0x%X: type 0x%X, not an image texture" % (key, at, kind))
    r.magic(at + 4, b"TXOB", "texture")
    if r.string(at + 12, "texture name") != key:
        raise CheckError("texture %r at 0x%X carries another name" % (key, at))
    r.dictionary(at + 16, "texture %r user data" % key)
    height, width, levels, fmt = r.u32(at + 24), r.u32(at + 28), r.u32(at + 40), r.u32(at + 52)
    if fmt == 0:
        raise CheckError("texture %r: RGBA8, reported broken on the HOME Menu" % key)
    if fmt not in TEXTURE_FORMATS:
        raise CheckError("texture %r: format %d, which this pipeline never writes" % (key, fmt))
    for side in (width, height):
        if side < 8 or side > 1024 or side & (side - 1):
            raise CheckError("texture %r: %d x %d, a side is no power of two from 8 to 1024" % (key, width, height))
    if levels != 1:
        raise CheckError("texture %r: %d levels, this pipeline writes one" % (key, levels))
    image = r.ptr(at + 56, "texture %r image" % key, 32)
    if image is None:
        raise CheckError("texture %r has no image" % key)
    if (r.u32(image), r.u32(image + 4)) != (height, width):
        raise CheckError("texture %r: its image is %d x %d" % (key, r.u32(image + 4), r.u32(image)))
    size, start = r.blob(image + 8, "texture %r data" % key)
    name, bpp = TEXTURE_FORMATS[fmt]
    if size != int(width * height * bpp):
        raise CheckError("texture %r: %d data bytes, %d x %d %s takes %d" % (key, size, width, height, name,
                                                                            int(width * height * bpp)))
    if start % TEXTURE_ALIGN:
        raise CheckError("texture %r: its data starts at 0x%X, not on a multiple of %d bytes" % (key, start, TEXTURE_ALIGN))
    return {"name": key, "offset": at, "width": width, "height": height, "format": name, "levels": levels,
            "bytes": size, "data_at": start, "data_in_imag": start - r.imag[0],
            "location_flag": r.u32(at + 48), "image_words": [r.u32(image + 16 + 4 * i) for i in range(4)]}


def read_mapper(r, m, key, i, textures):
    """A material's texture mapper (TexInfo): its texture reference and its sampler."""
    if r.u32(m) != 0x80000000:
        raise CheckError("material %r mapper %d: type 0x%X" % (key, i, r.u32(m)))
    ref = r.ptr(m + 8, "material %r mapper %d texture" % (key, i), 32)
    if ref is None:
        raise CheckError("material %r mapper %d has no texture" % (key, i))
    if r.u32(ref) != 0x20000004:
        raise CheckError("material %r mapper %d: its texture (type 0x%X) is not a reference" % (key, i, r.u32(ref)))
    r.magic(ref + 4, b"TXOB", "texture reference")
    r.string(ref + 12, "texture reference name")
    r.dictionary(ref + 16, "texture reference user data")
    target_name = r.string(ref + 24, "referenced texture name")
    entries, nodes = textures
    found = lookup(nodes, target_name or "")
    if found is None:
        raise CheckError("material %r mapper %d names the texture %r, which is not in the file" % (key, i, target_name))
    if r.ptr(ref + 28, "referenced texture", 60) != entries[found - 1][1]:
        raise CheckError("material %r mapper %d: the reference's pointer is not the texture %r" % (key, i, target_name))
    sampler = r.ptr(m + 12, "material %r mapper %d sampler" % (key, i), 28)
    if sampler is None or r.u32(sampler) != 0x80000000:
        raise CheckError("material %r mapper %d: no sampler" % (key, i))
    if r.ptr(sampler + 4, "sampler owner") != m:
        raise CheckError("material %r mapper %d: the sampler's owner is not the mapper" % (key, i))
    return {"texture": target_name, "min_filter": r.u32(sampler + 8), "border": r.floats(sampler + 12, 4),
            "commands": [r.u32(m + 16 + 4 * k) for k in range(14)], "command_size": r.u32(m + 72)}


def read_material(r, at, key, luts, textures=None):
    if r.u32(at) != 0x08000000:
        raise CheckError("material %r at 0x%X: type 0x%X" % (key, at, r.u32(at)))
    r.magic(at + 4, b"MTOB", "material")
    r.need(at, 728, "material %r" % key)
    if r.string(at + 12, "material name") != key:
        raise CheckError("material %r at 0x%X carries another name" % (key, at))
    r.dictionary(at + 16, "material user data")
    mappers = []
    for i in range(4):
        m = r.ptr(at + 628 + 4 * i, "texture mapper", 76)
        if m is not None:
            mappers.append(read_mapper(r, m, key, i, textures or ([], [(0xFFFFFFFF, 0, 0, None)])))
    shader = r.ptr(at + 644, "material %r shader" % key, 32)
    if shader is None:
        raise CheckError("material %r has no shader" % key)
    r.magic(shader + 4, b"SHDR", "shader")
    if r.u32(shader) != 0x80000001:
        raise CheckError("material %r: the shader (type 0x%X) is not a reference" % (key, r.u32(shader)))
    r.string(shader + 12, "material %r shader name" % key)
    r.dictionary(shader + 16, "material %r shader user data" % key)
    reference = r.string(shader + 24, "shader reference")
    if reference != "DefaultShader":
        raise CheckError("material %r: the shader reference is %r, not 'DefaultShader'" % (key, reference))
    frag = r.ptr(at + 648, "material %r fragment shader" % key, 244)
    if frag is None:
        raise CheckError("material %r has no fragment shader" % key)
    tables = 0
    table = r.ptr(frag + 40, "lighting table", 24)
    if table is not None:
        for i in range(6):
            entry = r.ptr(table + 4 * i, "lighting lookup table", 12)
            if entry is None:
                continue
            sampler = r.ptr(entry + 8, "lookup table sampler", 16)
            if sampler is None:
                raise CheckError("material %r: a lighting table without a sampler" % key)
            path, name = r.string(sampler + 4, "lookup table set"), r.string(sampler + 8, "lookup table name")
            if path not in luts or lookup(luts[path], name or "") is None:
                raise CheckError("material %r: the lookup table %r/%r does not resolve" % (key, path, name))
            tables += 1
    stages = []
    for i in range(6):
        s = frag + 44 + 28 * i
        stages.append({"constant": r.u32(s), "src_rgb": r.u16(s + 4), "src_alpha": r.u16(s + 6),
                       "combine_rgb": r.u16(s + 16), "combine_alpha": r.u16(s + 18)})
    blend, test = r.u32(at + 328), r.u32(frag + 212)
    return {"name": key, "flags": r.u32(at + 24), "emission": r.floats(at + 36, 4),
            "ambient": r.floats(at + 52, 4), "diffuse": r.floats(at + 68, 4),
            "specular0": r.floats(at + 84, 4), "constant0": r.floats(at + 116, 4),
            "cull": r.u32(at + 272), "textures": len(mappers), "lighting_flags": r.u32(frag + 16),
            "lighting_tables": tables, "stages": stages, "light_set": r.u32(at + 664),
            "mappers": mappers, "translucency": r.u32(at + 32), "depth_flags": r.u32(at + 280),
            "blend_mode": r.u32(at + 300),
            "blend": [BLEND_FUNCTIONS[(blend >> s) & 15] for s in (16, 20, 24, 28)],
            "alpha_test": {"enabled": test & 1, "function": (test >> 4) & 7, "cutoff": (test >> 8) & 255},
            "texcoords_used": r.u32(at + 360), "texcoord0_source": r.u32(at + 364)}


def read_model(r, at, key, luts, textures=None):
    kind = r.u32(at)
    r.magic(at + 4, b"CMDL", "model")
    if kind != 0x40000092:
        raise CheckError("model %r: type 0x%X, a model with a skeleton is 0x40000092" % (key, kind))
    r.need(at, 228, "model %r" % key)
    if r.string(at + 12, "model name") != key:
        raise CheckError("model %r at 0x%X carries another name" % (key, at))
    r.dictionary(at + 16, "model user data")
    groups, group_nodes = read_groups(r, at + 40, "model")
    skeleton = r.ptr(at + 224, "skeleton", 44)
    if skeleton is None:
        raise CheckError("model %r has no skeleton" % key)
    bones, bone_nodes = read_skeleton(r, skeleton)
    material_entries, _ = r.dictionary(at + 188, "model materials", 728)
    materials = [read_material(r, m, name, luts, textures) for name, m in material_entries]
    shapes = [read_shape(r, s, len(bones)) for s in r.pointer_list(at + 196, "shapes", 68)]
    mesh_nodes, _ = r.dictionary(at + 204, "mesh nodes")
    meshes = []
    for m in r.pointer_list(at + 180, "meshes", 116):
        if r.u32(m) != 0x01000000:
            raise CheckError("mesh at 0x%X: type 0x%X" % (m, r.u32(m)))
        r.magic(m + 4, b"SOBJ", "mesh")
        name = r.string(m + 12, "mesh name")
        if not name:
            raise CheckError("mesh at 0x%X has no name" % m)
        r.dictionary(m + 16, "mesh user data")
        shape, material = r.u32(m + 24), r.u32(m + 28)
        if shape >= len(shapes) or material >= len(materials):
            raise CheckError("mesh %r: shape %d of %d, material %d of %d"
                             % (name, shape, len(shapes), material, len(materials)))
        if r.ptr(m + 32, "mesh owner") != at:
            raise CheckError("mesh %r: its owner pointer does not name the model" % name)
        node = r.string(m + 112, "mesh node name")
        if not node:
            raise CheckError("mesh %r: the mesh node name is empty" % name)
        if lookup(bone_nodes, node) is None:
            raise CheckError("mesh %r: its node %r is not a bone" % (name, node))
        if bones[shapes[shape]["bone"]]["name"] != node:
            raise CheckError("mesh %r: node %r, but its shape hangs on bone %r"
                             % (name, node, bones[shapes[shape]["bone"]]["name"]))
        if materials[material]["textures"]:
            streams = dict(zip(shapes[shape]["streams"], shapes[shape]["formats"]))
            if streams.get("texcoord0") != (0x1406, 2):
                raise CheckError("mesh %r: its material has a texture, its shape no texture coordinates of two floats" % name)
        meshes.append({"name": name, "shape": shape, "material": material, "node": node,
                       "visible": r.u8(m + 36), "node_visibility_index": r.u16(m + 38)})
    if not meshes:
        raise CheckError("model %r has no mesh" % key)
    if len(set(m["name"] for m in meshes)) != len(meshes):
        raise CheckError("model %r: two meshes share a name" % key)
    return {"name": key, "bones": bones, "meshes": meshes, "shapes": shapes, "materials": materials,
            "mesh_nodes": len(mesh_nodes),
            "groups": {n: {"member_type": g["member_type"], "members": g["members"]}
                       for n, g in groups.items()},
            "triangles": sum(s["triangles"] for s in shapes),
            "vertices": sum(s["vertices"] for s in shapes)}, groups, group_nodes, bone_nodes


def read_curve(r, at, what, frame_size):
    start, end = r.f32(at), r.f32(at + 4)
    if not 0.0 <= start < end <= frame_size:
        raise CheckError("%s: runs %g to %g, the loop is 0 to %g" % (what, start, end, frame_size))
    count = r.u32(at + 16)
    if not 1 <= count <= 1024:
        raise CheckError("%s: %d segments" % (what, count))
    r.need(at, 20 + 4 * count, what)
    keys = []
    for i in range(count):
        seg = r.ptr(at + 20 + 4 * i, what + " segment", 16)
        if seg is None:
            raise CheckError("%s: segment %d is null" % (what, i))
        s0, s1, flags = r.f32(seg), r.f32(seg + 4), r.u32(seg + 8)
        if flags & 1:
            raise CheckError("%s: a single-value segment, which is not what the pipeline writes" % what)
        interpolation, quantization = (flags >> 2) & 7, flags >> 5
        if quantization != 6 or interpolation not in (0, 1):
            raise CheckError("%s: interpolation %d, key form %d; expected step or linear keys of two floats"
                             % (what, interpolation, quantization))
        n = r.u32(seg + 12)
        if not 2 <= n <= 4096:
            raise CheckError("%s: %d keys" % (what, n))
        r.f32(seg + 16, what + " speed")
        r.need(seg + 20, 8 * n, what + " keys")
        for k in range(n):
            frame, value = r.f32(seg + 20 + 8 * k), r.f32(seg + 24 + 8 * k)
            if frame < s0 or frame > s1 or (keys and frame <= keys[-1][0]):
                raise CheckError("%s: key %d at frame %g is out of order or outside %g to %g"
                                 % (what, k, frame, s0, s1))
            keys.append((frame, value))
    if keys[0][0] != start or keys[-1][0] != end:
        raise CheckError("%s: keys run %g to %g, the curve %g to %g" % (what, keys[0][0], keys[-1][0], start, end))
    return {"start": start, "end": end, "pre": r.u8(at + 8), "post": r.u8(at + 9), "keys": keys}


def read_animation(r, at, key, groups, group_nodes, bone_nodes):
    r.magic(at, b"CANM", "animation")
    r.need(at, 40, "animation %r" % key)
    if r.string(at + 8, "animation name") != key:
        raise CheckError("animation %r at 0x%X carries another name" % (key, at))
    target = r.string(at + 12, "animation target group")
    if target is None or lookup(group_nodes, target) is None:
        raise CheckError("animation %r: its target group %r is not in the model" % (key, target))
    if groups[target]["member_type"] != 1:
        raise CheckError("animation %r: the group %r is not the skeletal one" % (key, target))
    looping, frames = r.u32(at + 16), r.f32(at + 20)
    if looping not in (0, 1) or frames <= 0:
        raise CheckError("animation %r: loop flag %d, %g frames" % (key, looping, frames))
    r.dictionary(at + 32, "animation user data")
    members = {}
    entries, _ = r.dictionary(at + 24, "animation %r members" % key, 60)
    for name, m in entries:
        if r.string(m + 4, "animation member path") != name:
            raise CheckError("animation member %r at 0x%X carries another path" % (name, m))
        if lookup(bone_nodes, name) is None or lookup(groups[target]["nodes"], name) is None:
            raise CheckError("animation member %r does not resolve to a bone of the model" % name)
        if r.u32(m + 16) != 5:
            raise CheckError("animation member %r: kind %d, an ordinary Transform is 5" % (name, r.u32(m + 16)))
        flags, slots = r.u32(m), {}
        for i, slot in enumerate(SLOTS):
            what = "animation %s.%s" % (name, slot)
            if flags & CONST_BITS[i]:
                slots[slot] = {"const": r.f32(m + SLOT_OFFSETS[i], what)}
            elif flags & IGNORE_BITS[i]:
                if r.s32(m + SLOT_OFFSETS[i]) != 0:
                    raise CheckError("%s is marked ignored but has a pointer" % what)
                slots[slot] = None
            else:
                curve = r.ptr(m + SLOT_OFFSETS[i], what, 24)
                if curve is None:
                    raise CheckError("%s: neither constant nor ignored, and no curve" % what)
                slots[slot] = read_curve(r, curve, what, frames)
        members[name] = {"flags": flags, "slots": slots}
    if not members:
        raise CheckError("animation %r moves nothing" % key)
    return {"name": key, "target": target, "looping": looping, "frames": frames, "members": members}


def check(data, anim=None, frames=None, max_size=MAX_SIZE):
    """The facts of a CGFX as a dict; raises CheckError when it must be refused."""
    r = Reader(data)
    imag_size = None
    if len(data) > max_size:
        raise CheckError("the file is %d bytes, over the limit of %d (0x%X)" % (len(data), max_size, max_size))
    r.need(0, 0x94, "the header and DATA block")
    r.magic(0, b"CGFX", "header")
    if r.u16(4) != 0xFEFF or r.u16(6) != 0x14:
        raise CheckError("header: byte order mark 0x%X, header size 0x%X" % (r.u16(4), r.u16(6)))
    if r.u32(12) != len(data):
        raise CheckError("header: the file size field says %d, the file has %d bytes" % (r.u32(12), len(data)))
    blocks = r.u32(16)
    r.magic(0x14, b"DATA", "DATA block")
    data_end = 0x14 + r.u32(0x18)
    if blocks == 1:
        if data_end != len(data):
            raise CheckError("DATA block ends at 0x%X, the file at 0x%X" % (data_end, len(data)))
    elif blocks == 2:
        r.magic(data_end, b"IMAG", "IMAG block")
        # the size field: pycgfx counts the bytes after the block's 8 byte header, the
        # banner bannertool builds from its own model counts the header too
        size = r.u32(data_end + 4)
        if data_end + 8 + size == len(data):
            imag_size = "payload"
        elif data_end + size == len(data):
            imag_size = "block"
        else:
            raise CheckError("IMAG block at 0x%X: its size %d does not end at the file's end" % (data_end, size))
        r.imag = (data_end + 8, len(data))
    else:
        raise CheckError("header: %d blocks" % blocks)

    top = {}
    for i, name in enumerate(DATA_DICTS):
        top[name] = r.dictionary(0x1C + 8 * i, name)

    luts = {}
    for name, at in top["lookup_tables"][0]:
        r.magic(at + 4, b"LUTS", "lookup table set")
        r.need(at, 32, "lookup table set")
        if r.u32(at) != 0x04000000 or r.string(at + 12, "lookup table set name") != name:
            raise CheckError("lookup table set %r at 0x%X: wrong type or name" % (name, at))
        r.dictionary(at + 16, "lookup table set user data")
        tables, nodes = r.dictionary(at + 24, "lookup tables of %r" % name, 20)
        for table_name, t in tables:
            own = r.string(t + 4, "lookup table name")
            if own != table_name:
                # pycgfx leaves it empty; lookups go by the dictionary's name
                r.notes.append("lookup table %r carries the name %r inside" % (table_name, own))
            r.blob(t + 12, "lookup table %r" % table_name)
        luts[name] = nodes

    if [n for n, _ in top["models"][0]] != ["COMMON"]:
        raise CheckError("models are %r; the HOME Menu looks for one named 'COMMON'"
                         % [n for n, _ in top["models"][0]])
    textures = [read_texture(r, at, name) for name, at in top["textures"][0]]
    model, groups, group_nodes, bone_nodes = read_model(r, top["models"][0][0][1], "COMMON", luts, top["textures"])
    used = set(m["texture"] for mat in model["materials"] for m in mat["mappers"])
    for t in textures:
        if t["name"] not in used:
            r.notes.append("texture %r is used by no material" % t["name"])

    lights = []
    for name, at in top["lights"][0]:
        r.magic(at + 4, b"CFLT", "light")
        r.need(at, 308, "light %r" % name)
        if r.string(at + 12, "light name") != name:
            raise CheckError("light %r carries another name" % name)
        r.dictionary(at + 16, "light user data")
        read_groups(r, at + 40, "light")
        lights.append({"name": name, "enabled": r.u32(at + 180), "kind": r.u32(at + 184),
                       "ambient": r.floats(at + 188, 4), "diffuse": r.floats(at + 204, 4),
                       "direction": r.floats(at + 268, 3)})
    for name, at in top["scenes"][0]:
        r.magic(at + 4, b"CENV", "scene")
        r.need(at, 48, "scene %r" % name)
        r.dictionary(at + 16, "scene user data")
        r.pointer_list(at + 24, "scene cameras", 12)
        for ls in r.pointer_list(at + 32, "scene light sets", 12):
            for light in r.pointer_list(ls + 4, "scene lights", 12):
                light_name = r.string(light + 4, "scene light name")
                if lookup(top["lights"][1], light_name or "") is None:
                    raise CheckError("scene %r names the light %r, which is not in the file" % (name, light_name))
    for name in ("textures", "materials", "shaders", "cameras", "fogs", "material_animations",
                 "visibility_animations", "camera_animations", "light_animations", "emitters"):
        for entry, at in top[name][0]:
            r.need(at, 8, "%s %r" % (name, entry))

    animations = [read_animation(r, at, name, groups, group_nodes, bone_nodes)
                  for name, at in top["skeletal_animations"][0]]
    if len(animations) > 1 or (animations and animations[0]["name"] != "COMMON"):
        raise CheckError("skeletal animations are %r; the HOME Menu plays one named 'COMMON'"
                         % [a["name"] for a in animations])
    animation = animations[0] if animations else None
    if anim == "yes" and animation is None:
        raise CheckError("no skeletal animation, one was expected")
    if anim == "no" and animation is not None:
        raise CheckError("a skeletal animation in a file expected to be still")
    if frames is not None and (animation is None or animation["frames"] != frames or not animation["looping"]):
        raise CheckError("expected a looping animation of %d frames, found %s" % (
            frames, "none" if animation is None else
            "%g frames, loop flag %d" % (animation["frames"], animation["looping"])))

    return {"size": len(data), "revision": r.u32(8), "blocks": blocks, "data_end": data_end,
            "imag_size_counts": imag_size,
            "counts": {name: len(top[name][0]) for name in DATA_DICTS},
            "dictionaries": r.dicts, "names": r.names, "notes": r.notes, "model": model, "lights": lights,
            "textures": textures,
            "animation": animation}


def sample(curve, frame):
    """A slot's value at a frame: a constant, or linear between its keys."""
    if "const" in curve:
        return curve["const"]
    keys = curve["keys"]
    if frame <= keys[0][0]:
        return keys[0][1]
    for (f0, v0), (f1, v1) in zip(keys, keys[1:]):
        if frame <= f1:
            return v0 + (v1 - v0) * (frame - f0) / (f1 - f0)
    return keys[-1][1]


def summary(facts):
    m = facts["model"]
    lines = ["size %d bytes (limit %d), %d block(s), %d dictionaries, %d names found through their trees"
             % (facts["size"], MAX_SIZE, facts["blocks"], facts["dictionaries"], facts["names"]),
             "model %s: %d bones, %d meshes, %d shapes, %d materials, %d triangles, %d vertices"
             % (m["name"], len(m["bones"]), len(m["meshes"]), len(m["shapes"]), len(m["materials"]),
                m["triangles"], m["vertices"])]
    for b in m["bones"]:
        lines.append("  bone %-12s parent %2d  billboard %d  at (%.3f, %.3f, %.3f)"
                     % ((b["name"], b["parent"], b["billboard"]) + tuple(b["position"])))
    for mesh in m["meshes"]:
        lines.append("  mesh %s: node %s, shape %d, material %d" % (mesh["name"], mesh["node"], mesh["shape"], mesh["material"]))
    for mat in m["materials"]:
        lines.append("  material %s: ambient %.2f, diffuse %.2f, constant0 %.2f, %d textures, %d lighting tables, stage 2 source 0x%03X"
                     % (mat["name"], mat["ambient"][0], mat["diffuse"][0], mat["constant0"][0],
                        mat["textures"], mat["lighting_tables"], mat["stages"][2]["src_rgb"]))
    for mat in m["materials"]:
        if mat["mappers"]:
            lines.append("  material %s: blend %s, depth flags %d, alpha test %s, translucency %d, texture coordinates %d"
                         % (mat["name"], "/".join(mat["blend"]), mat["depth_flags"],
                            "on, cutoff %d" % mat["alpha_test"]["cutoff"] if mat["alpha_test"]["enabled"] else "off",
                            mat["translucency"], mat["texcoords_used"]))
            for mp in mat["mappers"]:
                lines.append("    mapper: texture %s, min filter %d" % (mp["texture"], mp["min_filter"]))
    for t in facts["textures"]:
        lines.append("texture %s: %d x %d %s, %d level, %d bytes at 0x%X in the file (0x%X in IMAG, %d past a multiple of %d)"
                     % (t["name"], t["width"], t["height"], t["format"], t["levels"], t["bytes"], t["data_at"],
                        t["data_in_imag"], t["data_at"] % TEXTURE_ALIGN, TEXTURE_ALIGN))
    a = facts["animation"]
    if a is None:
        lines.append("animation: none")
    else:
        lines.append("animation %s: %g frames, loop flag %d, target %s, %d bones"
                     % (a["name"], a["frames"], a["looping"], a["target"], len(a["members"])))
        for name, member in a["members"].items():
            keyed = ["%s:%d" % (s, len(c["keys"])) for s, c in member["slots"].items() if c and "keys" in c]
            lines.append("  %s: %s" % (name, " ".join(keyed)))
    for note in facts["notes"]:
        lines.append("note: " + note)
    return lines


def main(argv):
    anim = frames = None
    as_json = False
    args = argv[1:]
    paths = []
    while args:
        arg = args.pop(0)
        if arg == "--anim" and args:
            anim = args.pop(0)
        elif arg == "--frames" and args:
            frames = int(args.pop(0))
        elif arg == "--json":
            as_json = True
        else:
            paths.append(arg)
    if len(paths) != 1 or anim not in (None, "yes", "no"):
        print(__doc__.split("\n\n")[1])
        return 2
    with open(paths[0], "rb") as f:
        data = f.read()
    try:
        facts = check(data, anim, frames)
    except CheckError as e:
        print("REFUSED %s: %s" % (paths[0], e))
        return 1
    except (struct.error, IndexError, KeyError, ValueError) as e:
        print("REFUSED %s: the walk broke (%s: %s)" % (paths[0], type(e).__name__, e))
        return 1
    if as_json:
        print(json.dumps(facts, indent=1, sort_keys=True))
    else:
        print("OK %s" % paths[0])
        for line in summary(facts):
            print(line)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
