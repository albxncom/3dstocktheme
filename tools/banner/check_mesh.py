#!/usr/bin/env python3
"""Compares the meshes inside a CGFX with the scene they were built from, bit for bit.

  python3 -I check_mesh.py <file.cgfx>      (against the banner's scene, scene.py)

check_cgfx.py proves the file's structure and counts; this proves the content: per
part the shape that hangs on the part's bone holds the scene's positions, normals and
colours as 32-bit floats in the scene's order (scale 1), and the scene's triangles as
its one index list. Standard library and check_cgfx's reader only. Exit 1 with the
reason.
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.dont_write_bytecode = True
sys.path.insert(0, HERE)
import check_cgfx   # noqa: E402

USAGES = {0: ("positions", 0, 3), 1: ("normals", 1, 3), 3: ("colours", 2, 4),     # name, place in a vertex, floats
          4: ("texture coordinates", 3, 2)}                                         # (T10, T11 only)


def compare(scene, data):
    """A line per part; raises check_cgfx.CheckError on the first difference."""
    facts = check_cgfx.check(data)
    r = check_cgfx.Reader(data)
    data_end = 0x14 + r.u32(0x18)
    r.imag = (data_end + 8, len(data))
    model = r.dictionary(0x1C, "models")[0][0][1]
    shapes = r.pointer_list(model + 196, "shapes", 68)
    bones = facts["model"]["bones"]
    by_bone = {}
    for at, shape in zip(shapes, facts["model"]["shapes"]):
        name = bones[shape["bone"]]["name"]
        if name in by_bone:
            raise check_cgfx.CheckError("two shapes hang on bone %r" % name)
        by_bone[name] = at
    if sorted(by_bone) != sorted(p["name"] for p in scene["parts"]):
        raise check_cgfx.CheckError("shapes hang on %r, the scene's parts are %r"
                                    % (sorted(by_bone), sorted(p["name"] for p in scene["parts"])))
    lines = []
    for part in scene["parts"]:
        at, name, count = by_bone[part["name"]], part["name"], len(part["vertices"])
        seen = []
        for s in r.pointer_list(at + 56, "vertex attributes", 52):
            usage, fmt, comps = r.u32(s + 4), r.u32(s + 36), r.u32(s + 40)
            if usage not in USAGES:
                raise check_cgfx.CheckError("part %s: a stream of usage %d, which the scene does not have" % (name, usage))
            what, place, floats = USAGES[usage]
            if fmt != 0x1406 or comps != floats or r.f32(s + 44, "stream scale") != 1.0:
                raise check_cgfx.CheckError("part %s: the %s are not %d floats of scale 1" % (name, what, floats))
            size, start = r.blob(s + 20, what)
            want = struct.pack("<%df" % (floats * count), *(c for v in part["vertices"] for c in v[place]))
            if bytes(data[start:start + size]) != want:
                raise check_cgfx.CheckError("part %s: the %s in the file are not the scene's" % (name, what))
            seen.append(what)
        streams = ["colours", "normals", "positions"] + (["texture coordinates"] if len(part["vertices"][0]) == 4 else [])
        if sorted(seen) != sorted(streams):
            raise check_cgfx.CheckError("part %s: streams %r" % (name, seen))
        lists = []
        for ps in r.pointer_list(at + 44, "primitive sets", 20):
            for p in r.pointer_list(ps + 12, "primitives", 24):
                for ix in r.pointer_list(p, "index streams", 44):
                    size, start = r.blob(ix + 8, "indices")
                    width = check_cgfx.TYPE_WIDTH[r.u32(ix)]
                    lists.append((width, struct.unpack_from("<%d%s" % (size // width, "BH"[width - 1]), data, start)))
        if len(lists) != 1 or lists[0][1] != tuple(i for t in part["triangles"] for i in t):
            raise check_cgfx.CheckError("part %s: the triangles in the file are not the scene's" % name)
        lines.append("%s: %d vertices (positions, normals, colours%s) and %d triangles (%d-bit indices) are the "
                     "scene's, bit for bit" % (name, count, ", texture coordinates" if len(streams) == 4 else "",
                                               len(part["triangles"]), 8 * lists[0][0]))
    return lines


def main(argv):
    if len(argv) != 2:
        print(__doc__.split("\n\n")[1])
        return 2
    import scene
    with open(argv[1], "rb") as f:
        data = f.read()
    try:
        for line in compare(scene.load(), data):
            print(line)
    except check_cgfx.CheckError as e:
        print("REFUSED: %s" % e)
        return 1
    print("OK %s holds the meshes of the banner's scene" % argv[1])
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
