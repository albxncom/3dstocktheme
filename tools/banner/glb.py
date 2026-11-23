#!/usr/bin/env python3
"""Writes a binary glTF (GLB) from a scene of rigid parts, in the form pycgfx's
main.py (commit 6c2e0cd) accepts, and the plan of fixes build.py applies after it.

Standard library only; build.py calls build() and plan().

A scene is a dict:
  name       str
  root       str, the root node's name; every part is a child of it
  material   {"name": str, "ambient": float, "diffuse": float}  one matte material
  frames     int, the loop's length in frames at 60 a second; 0 = no animation
  billboard_all  int or None: a billboard mode for every bone, the roots too
  bone_modes  optional {bone name: mode} for bones that are not parts: the root, and
              "Scene root", the bone pycgfx adds above it
  parts      list of
    name        str (node, mesh and bone name)
    pivot       (x, y, z): the node's rest position, the camera's units (10 px a unit)
    billboard   int: 0 off, 4 ScreenViewpoint, 5 YAxial
    vertices    list of ((x, y, z), (nx, ny, nz), (r, g, b, a)), relative to the pivot,
                colours 0..1; in a scene with a texture each vertex has a fourth item
                (u, v), glTF's texture coordinates (0, 0 at the image's top left)
    triangles   list of (a, b, c), counter-clockwise seen from outside
    keys        None, or a list of (frame, (dx, dy, dz), yaw_degrees): the offset
                from the pivot and the turn about the part's own vertical axis;
                the first key at frame 0, the last at `frames`, both the same pose
    same_after  optional, degrees: the mesh is the same after a turn this far about
                its own vertical axis (proven by whoever sets it), so the last key
                may stand that far from the first
  texture    optional (T10, T11, T13 and T14 only; no other scene has the key, so their GLB
             and plan bytes are unchanged): {"name", "png": bytes, "width", "height",
             "format": "RGBA4", "alpha": "BLEND", "MASK" or "OPAQUE" (T13: written as
             glTF's OPAQUE, no alpha test, no blend), "cutoff": float or None,
             "align": bytes, "sha256": of the texture data as the GPU holds it}. One
             image in the binary buffer, one sampler (linear, clamped to the edge), the
             material's base colour texture and its alphaMode.

What pycgfx needs and why this file writes it so (read in its main.py):
  - `scene` set and one root node: it reads scenes[scene].nodes and adds its own
    bone "Scene root" above them.
  - every mesh and every material named: an unnamed mesh raises a TypeError, the
    material's name is its dictionary key.
  - triangle lists (mode 4) with indices; POSITION, NORMAL and COLOR_0 as floats:
    it copies each accessor as one vertex stream with scale 1, so byte colours
    would come out 255 times too bright.
  - one primitive per mesh, one mesh per node: a mesh is bound to its node's bone.
  - animation: LINEAR samplers, times in seconds from 0; it writes 60 frames a
    second and takes the last time as the loop's length. Translation, rotation and
    scale are all written for an animated node, as the banners reported working on
    hardware have them.
  - no skin, not double sided; no texture and no alphaMode (opaque) unless the scene
    has a texture: then TEXCOORD_0 as floats, the image as a PNG in a buffer view
    (pycgfx's gltf_get_texture opens it with PIL, flips it and converts it to RGBA4).
"""
import json
import math
import struct

FPS = 60.0
FLOAT, USHORT = 5126, 5123
ARRAY_BUFFER, ELEMENT_BUFFER = 34962, 34963
LINEAR, CLAMP = 9729, 33071


def yaw_quaternion(degrees):
    half = math.radians(degrees) / 2.0
    return (0.0, math.sin(half), 0.0, math.cos(half))


def aligned(data, fill):
    """`data` with `fill` bytes appended up to the next multiple of 4."""
    return bytes(data) + fill * (-len(data) % 4)


class Buffer:
    """The GLB's single binary buffer, with one buffer view per accessor.

    Each view starts on a 4-byte boundary, which glTF asks of float data."""

    def __init__(self):
        self.data = bytearray()
        self.views = []
        self.accessors = []

    def accessor(self, payload, target=None, **fields):
        self.data += bytes(-len(self.data) % 4)
        view = {"buffer": 0, "byteOffset": len(self.data), "byteLength": len(payload)}
        if target:
            view["target"] = target
        self.data += payload
        self.views.append(view)
        self.accessors.append({"bufferView": len(self.views) - 1, **fields})
        return len(self.accessors) - 1


def glb_container(doc, binary):
    """The GLB 2.0 layout: a 12-byte header (magic, version, total length), then a
    JSON chunk padded with spaces and a BIN chunk padded with zero bytes; every
    chunk is its length, its type and its data; all numbers u32 little-endian."""
    chunks = b""
    for kind, payload in ((b"JSON", aligned(json.dumps(doc, separators=(",", ":")).encode("ascii"), b" ")),
                          (b"BIN\0", binary)):
        chunks += struct.pack("<I", len(payload)) + kind + payload
    return struct.pack("<4sII", b"glTF", 2, 12 + len(chunks)) + chunks


def check_scene(scene):
    names = [scene["root"]] + [p["name"] for p in scene["parts"]]
    if len(set(names)) != len(names) or any(not n for n in names):
        raise ValueError("node names must be set and unique: %r" % names)
    if "Scene root" in names:
        raise ValueError("'Scene root' is the name pycgfx gives its own root bone")
    frames = scene["frames"]
    for part in scene["parts"]:
        count = len(part["vertices"])
        if count == 0 or count > 65535 or not part["triangles"]:
            raise ValueError("part %s: needs 1..65535 vertices and a triangle" % part["name"])
        for tri in part["triangles"]:
            if len(tri) != 3 or min(tri) < 0 or max(tri) >= count:
                raise ValueError("part %s: triangle %r out of range" % (part["name"], tri))
        keys = part.get("keys")
        if keys:
            if not frames:
                raise ValueError("part %s has keys but the scene has no frames" % part["name"])
            times = [k[0] for k in keys]
            if times[0] != 0 or times[-1] != frames or any(b <= a for a, b in zip(times, times[1:])):
                raise ValueError("part %s: keys must rise from frame 0 to %d" % (part["name"], frames))
            first, last = keys[0], keys[-1]
            again = part.get("same_after", 360.0)
            turn = (last[2] - first[2]) % again
            if tuple(first[1]) != tuple(last[1]) or min(turn, again - turn) > 1e-9:
                raise ValueError("part %s: the loop does not close" % part["name"])
    texture = scene.get("texture")
    if texture:
        if texture["alpha"] not in ("BLEND", "MASK", "OPAQUE") or (texture["alpha"] == "MASK") != (texture["cutoff"] is not None):
            raise ValueError("texture: alpha %r with cutoff %r" % (texture["alpha"], texture["cutoff"]))
        for part in scene["parts"]:
            if any(len(v) != 4 or not all(0.0 <= c <= 1.0 for c in v[3]) for v in part["vertices"]):
                raise ValueError("part %s: a vertex without texture coordinates in 0..1" % part["name"])
    elif any(len(v) != 3 for part in scene["parts"] for v in part["vertices"]):
        raise ValueError("texture coordinates in a scene without a texture")
    if frames and not any(p.get("keys") for p in scene["parts"]):
        raise ValueError("frames set but no part has keys")
    for name in scene.get("bone_modes", {}):
        if name in names[1:] or name not in (scene["root"], "Scene root"):
            raise ValueError("bone_modes: %r is a part or no bone of the scene" % name)


def build(scene):
    """The GLB bytes."""
    check_scene(scene)
    buf = Buffer()
    add = buf.accessor
    material = scene["material"]
    doc = {
        "asset": {"version": "2.0", "generator": "bn/pipe/glb.py"},
        "scene": 0,
        "scenes": [{"name": scene["name"], "nodes": [0]}],
        "nodes": [{"name": scene["root"], "children": []}],
        "meshes": [],
        "materials": [{
            "name": material["name"],
            "pbrMetallicRoughness": {"metallicFactor": 0.0, "roughnessFactor": 1.0},
        }],
    }
    samplers, channels = [], []
    for part in scene["parts"]:
        verts = part["vertices"]
        pos = [c for v in verts for c in v[0]]
        nrm = [c for v in verts for c in v[1]]
        col = [c for v in verts for c in v[2]]
        idx = [i for tri in part["triangles"] for i in tri]
        n = len(verts)
        lo = [min(pos[i::3]) for i in range(3)]
        hi = [max(pos[i::3]) for i in range(3)]
        a_pos = add(struct.pack("<%df" % len(pos), *pos), ARRAY_BUFFER,
                    componentType=FLOAT, count=n, type="VEC3", min=lo, max=hi)
        a_nrm = add(struct.pack("<%df" % len(nrm), *nrm), ARRAY_BUFFER,
                    componentType=FLOAT, count=n, type="VEC3")
        a_col = add(struct.pack("<%df" % len(col), *col), ARRAY_BUFFER,
                    componentType=FLOAT, count=n, type="VEC4")
        attributes = {"POSITION": a_pos, "NORMAL": a_nrm, "COLOR_0": a_col}
        if scene.get("texture"):
            uv = [c for v in verts for c in v[3]]
            attributes["TEXCOORD_0"] = add(struct.pack("<%df" % len(uv), *uv), ARRAY_BUFFER,
                                           componentType=FLOAT, count=n, type="VEC2")
        a_idx = add(struct.pack("<%dH" % len(idx), *idx), ELEMENT_BUFFER,
                    componentType=USHORT, count=len(idx), type="SCALAR")
        doc["meshes"].append({"name": part["name"], "primitives": [{
            "attributes": attributes,
            "indices": a_idx, "material": 0, "mode": 4}]})
        node = {"name": part["name"], "mesh": len(doc["meshes"]) - 1}
        pivot = [float(c) for c in part["pivot"]]
        keys = part.get("keys")
        if keys:
            # the rest pose is the first key
            node["translation"] = [pivot[i] + keys[0][1][i] for i in range(3)]
            node["rotation"] = list(yaw_quaternion(keys[0][2]))
            k = len(keys)
            times = [key[0] / FPS for key in keys]
            a_time = add(struct.pack("<%df" % k, *times), componentType=FLOAT, count=k,
                         type="SCALAR", min=[times[0]], max=[times[-1]])
            a_tra = add(b"".join(struct.pack("<3f", *(pivot[i] + key[1][i] for i in range(3)))
                                 for key in keys), componentType=FLOAT, count=k, type="VEC3")
            a_rot = add(b"".join(struct.pack("<4f", *yaw_quaternion(key[2])) for key in keys),
                        componentType=FLOAT, count=k, type="VEC4")
            a_sca = add(struct.pack("<3f", 1.0, 1.0, 1.0) * k,
                        componentType=FLOAT, count=k, type="VEC3")
            node_index = len(doc["nodes"])
            for path, out in (("translation", a_tra), ("rotation", a_rot), ("scale", a_sca)):
                channels.append({"sampler": len(samplers),
                                 "target": {"node": node_index, "path": path}})
                samplers.append({"input": a_time, "output": out, "interpolation": "LINEAR"})
        else:
            node["translation"] = pivot
        doc["nodes"][0]["children"].append(len(doc["nodes"]))
        doc["nodes"].append(node)
    if channels:
        doc["animations"] = [{"name": "loop", "channels": channels, "samplers": samplers}]
    texture = scene.get("texture")
    if texture:
        buf.data += bytes(-len(buf.data) % 4)
        buf.views.append({"buffer": 0, "byteOffset": len(buf.data), "byteLength": len(texture["png"])})
        buf.data += texture["png"]
        doc["images"] = [{"name": texture["name"], "mimeType": "image/png", "bufferView": len(buf.views) - 1}]
        doc["samplers"] = [{"magFilter": LINEAR, "minFilter": LINEAR, "wrapS": CLAMP, "wrapT": CLAMP}]
        doc["textures"] = [{"sampler": 0, "source": 0}]
        mat = doc["materials"][0]
        mat["pbrMetallicRoughness"]["baseColorTexture"] = {"index": 0, "texCoord": 0}
        mat["alphaMode"] = texture["alpha"]
        if texture["cutoff"] is not None:
            mat["alphaCutoff"] = texture["cutoff"]
    binary = aligned(buf.data, b"\0")
    doc["buffers"] = [{"byteLength": len(binary)}]
    doc["bufferViews"] = buf.views
    doc["accessors"] = buf.accessors
    return glb_container(doc, binary)


def plan(scene):
    """What build.py's fix step must set in the CGFX, and what the checker expects."""
    material = scene["material"]
    mesh_name = lambda part: part["name"] + "_" + material["name"]   # pycgfx's mesh name
    out = {
        "scene": scene["name"],
        "model": "COMMON",
        "animation": "COMMON" if scene["frames"] else None,
        "frames": scene["frames"],
        "animated": sorted(p["name"] for p in scene["parts"] if p.get("keys")),
        # the turn about each animated part's own vertical axis: [frame, degrees]
        "yaw_keys": {p["name"]: [[k[0], k[2]] for k in p["keys"]]
                     for p in scene["parts"] if p.get("keys")},
        "material": {"name": material["name"], "ambient": material["ambient"],
                     "diffuse": material["diffuse"]},
        "mesh_nodes": {mesh_name(p): p["name"] for p in scene["parts"]},
        "billboard": {**{p["name"]: p.get("billboard", 0) for p in scene["parts"]},
                      **scene.get("bone_modes", {})},
        "billboard_all": scene.get("billboard_all"),
        "root": scene["root"],
        "triangles": sum(len(p["triangles"]) for p in scene["parts"]),
        "vertices": sum(len(p["vertices"]) for p in scene["parts"]),
    }
    texture = scene.get("texture")
    if texture:
        out["texture"] = {key: texture[key] for key in
                          ("name", "width", "height", "format", "alpha", "cutoff", "align", "sha256")}
    return out

