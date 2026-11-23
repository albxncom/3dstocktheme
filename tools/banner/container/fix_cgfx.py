#!/usr/bin/env python3
"""Runs only inside the container (container/Dockerfile's image, no network), started by build.py:

  python3 -B -I /fix/fix_cgfx.py /pycgfx <in.glb> <plan.json> <out.cgfx>

Converts the GLB with pycgfx's own converter (its files unchanged, imported from the
read-only checkout), changes the converted objects as the plan says, and writes the
CGFX with pycgfx's own writer. Each fix and the report behind it is listed in ../README.md.
It refuses (exit 1) what the plan does not cover instead of guessing.

A plan with a "texture" (T10, T11, T13, T14 only; no other plan has the key, and without it
every step below runs as before) lets one texture through: it must be pycgfx's
ImageTexture of the planned name, size and format with one mip level, the material's
only texture mapper must reference it, the blend and alpha test must be pycgfx's for
the planned alpha mode, and in the written file the texture data must start on a
multiple of the planned alignment (else exit 1 with the offset, so the scene can move
it). Nothing of the texture is changed here.
"""
import hashlib
import json
import math
import sys

MAX_SIZE = 0x80000


def fail(text):
    print("fix_cgfx: " + text)
    sys.exit(1)


def all_dicts(root, shared):
    """Every DICT reachable from `root` through values()."""
    seen, found, stack = set(), [], [root]
    while stack:
        obj = stack.pop()
        if id(obj) in seen:
            continue
        seen.add(id(obj))
        if isinstance(obj, shared["DICT"]):
            found.append(obj)
        if isinstance(obj, shared["BaseObject"]):
            for value in obj.values():
                if isinstance(value, shared["BaseObject"]):   # a Reference is not followed
                    stack.append(value)
    return found


def rotation(x, y, z):
    """Rz * Ry * Rx, the order of a bone's Euler angles."""
    cx, sx, cy, sy, cz, sz = math.cos(x), math.sin(x), math.cos(y), math.sin(y), math.cos(z), math.sin(z)
    return ((cz * cy, cz * sy * sx - sz * cx, cz * sy * cx + sz * sx),
            (sz * cy, sz * sy * sx + cz * cx, sz * sy * cx - cz * sx),
            (-sy, cy * sx, cy * cx))


def pose_difference(a, b):
    """The angle between two Euler poses, in degrees."""
    ma, mb = rotation(*a), rotation(*b)
    trace = sum(ma[i][j] * mb[i][j] for i in range(3) for j in range(3))
    return math.degrees(math.acos(max(-1.0, min(1.0, (trace - 1.0) / 2.0))))


def main(argv):
    if len(argv) != 5:
        fail("usage: fix_cgfx.py <pycgfx dir> <in.glb> <plan.json> <out.cgfx>")
    pycgfx_dir, in_glb, plan_path, out_path = argv[1:]
    with open(plan_path) as f:
        plan = json.load(f)

    sys.path.insert(0, pycgfx_dir)
    import gltflib
    import main as pycgfx
    from cgfx.canm import CANMBoneTransform, FloatAnimationCurve, StepLinear64Key
    from cgfx.dict import DICT
    from cgfx.mtob import BlendFunction, ColorFloat, DepthFlag, FragmentLightingFlags, TexInfo
    from cgfx.primitives import VertexAttributeUsage
    from cgfx.shared import BaseObject
    from cgfx.sobj import BillboardMode, BoneFlag
    from cgfx.txob import ImageTexture, ReferenceTexture, TextureFormat

    gltf = gltflib.GLTF.load(in_glb, load_file_resources=True)
    cgfx = pycgfx.convert_gltf(gltf)
    done = []

    # 1. names: one model COMMON, one skeletal animation COMMON (or none)
    if list(cgfx.data.models) != [plan["model"]]:
        fail("models are %r, expected one %r" % (list(cgfx.data.models), plan["model"]))
    model = cgfx.data.models[plan["model"]]
    anims = list(cgfx.data.skeletal_animations)
    if anims != ([plan["animation"]] if plan["animation"] else []):
        fail("skeletal animations are %r, the plan says %r" % (anims, plan["animation"]))
    tex = plan.get("texture")
    if cgfx.data.material_animations.len() or (cgfx.data.textures.len() and not tex):
        fail("a material animation or a texture, which the plan does not cover")
    bones = model.skeleton.bones
    bone_names = list(bones)
    done.append("names: model %s, animation %s" % (plan["model"], plan["animation"]))

    # 2. every mesh names its node (pycgfx leaves the name empty)
    meshes = model.meshes.data.contents
    if sorted(m.name for m in meshes) != sorted(plan["mesh_nodes"]):
        fail("meshes are %r, the plan has %r" % ([m.name for m in meshes], sorted(plan["mesh_nodes"])))
    for mesh in meshes:
        node = plan["mesh_nodes"][mesh.name]
        if node not in bone_names:
            fail("mesh %s: node %s is not a bone" % (mesh.name, node))
        mesh.mesh_node_name = node
    done.append("mesh node names: %d set" % len(meshes))

    # 3. rigid parts only
    for shape in model.shapes.data.contents:
        for pset in shape.primitive_sets.data.contents:
            if pset.skinning_mode != 0 or len(pset.related_bones) != 1:
                fail("shape %s is skinned" % shape.name)
        for attr in shape.vertex_attributes.data.contents:
            if attr.usage in (VertexAttributeUsage.BoneIndex, VertexAttributeUsage.BoneWeight):
                fail("shape %s has bone streams" % shape.name)

    # 4. the material: matte with an ambient fill
    want = plan["material"]
    if list(model.materials) != [want["name"]]:
        fail("materials are %r, expected one %r" % (list(model.materials), want["name"]))
    mat = model.materials[want["name"]]
    a, d = float(want["ambient"]), float(want["diffuse"])
    if not (0.0 <= a <= 1.0 and 0.0 <= d <= 1.0 and a + d <= 1.0 + 1e-9):
        fail("ambient %r and diffuse %r must lie in 0..1 and sum to at most 1" % (a, d))
    mat.material_color.ambient = ColorFloat(a, a, a, 1.0)     # new objects: the class
    mat.material_color.diffuse = ColorFloat(d, d, d, 1.0)     # attributes are shared
    mat.material_color.constant[0] = ColorFloat(0.0, 0.0, 0.0, 1.0)
    stage = mat.fragment_shader.texture_combiners[2]          # the highlight stage:
    stage.src_rgb, stage.combine_rgb = 0xFFF, 0               # pass the stage before on
    mat.fragment_shader.fragment_lighting.flags = FragmentLightingFlags(0)
    mat.fragment_shader.fragment_lighting_table.distribution_0_sampler = None
    done.append("material %s: ambient %.2f, diffuse %.2f, no highlight" % (want["name"], a, d))

    # 4b. the texture the plan names, and nothing else, as pycgfx made it
    if tex:
        if list(cgfx.data.textures) != [tex["name"]]:
            fail("textures are %r, the plan has %r" % (list(cgfx.data.textures), tex["name"]))
        txob = cgfx.data.textures[tex["name"]]
        image = txob.pixel_based_image
        if (type(txob) is not ImageTexture or txob.name != tex["name"]
                or txob.hw_format != TextureFormat[tex["format"]] or txob.mipmap_level_count != 1
                or (txob.width, txob.height) != (tex["width"], tex["height"])
                or (image.width, image.height) != (tex["width"], tex["height"])
                or len(image.data) != tex["width"] * tex["height"] * 2):
            fail("texture %s is not a %dx%d %s image with one level" % (tex["name"], tex["width"], tex["height"], tex["format"]))
        if hashlib.sha256(image.data).hexdigest() != tex["sha256"]:
            fail("texture %s: its data is not the scene's (sha256 %s)" % (tex["name"], hashlib.sha256(image.data).hexdigest()))
        mapper = mat.texture_mappers[0]
        if (type(mapper) is not TexInfo or type(mapper.txob) is not ReferenceTexture or mapper.txob.txob is not txob
                or any(m is not None for m in mat.texture_mappers[1:]) or mat.used_texture_coordinates_count != 1):
            fail("material %s: its texture mappers are not the one reference to %s" % (want["name"], tex["name"]))
        blend = mat.fragment_operations.blend_operation
        depth = mat.fragment_operations.depth_operation.flags
        test = mat.fragment_shader.alpha_test
        if tex["alpha"] == "BLEND":
            ok = ((blend.src_color, blend.dst_color, blend.src_alpha, blend.dst_alpha)
                  == (BlendFunction.SrcAlpha, BlendFunction.InvSrcAlpha) * 2
                  and depth == DepthFlag.TestEnabled and not test.enabled)
        elif tex["alpha"] == "OPAQUE":
            ok = ((blend.src_color, blend.dst_color, blend.src_alpha, blend.dst_alpha)
                  == (BlendFunction.One, BlendFunction.Zero) * 2
                  and depth == DepthFlag.TestEnabled | DepthFlag.MaskEnabled and not test.enabled)
        else:
            ok = ((blend.src_color, blend.dst_color, blend.src_alpha, blend.dst_alpha)
                  == (BlendFunction.One, BlendFunction.Zero) * 2
                  and test.enabled and test.cutoff == int(tex["cutoff"] * 255))
        if not ok:
            fail("material %s: the blend and alpha test are not pycgfx's %s" % (want["name"], tex["alpha"]))
        done.append("texture %s: %dx%d %s, %d bytes, the scene's data; alpha %s" % (
            tex["name"], tex["width"], tex["height"], tex["format"], len(image.data), tex["alpha"]))

    # pycgfx never sets a lookup table's own name; give it its dictionary name
    for set_name in cgfx.data.lookup_tables:
        tables = cgfx.data.lookup_tables[set_name].tables
        for table_name in tables:
            tables[table_name].name = table_name

    # 5. billboard modes
    for name, mode in plan["billboard"].items():
        if name not in bone_names:
            fail("billboard: no bone %s" % name)
        bones[name].billboard_mode = BillboardMode(mode)
    if plan.get("billboard_all") is not None:
        for name in bone_names:
            bones[name].billboard_mode = BillboardMode(plan["billboard_all"])
    done.append("billboard: " + ", ".join(
        "%s=%d" % (n, int(bones[n].billboard_mode)) for n in bone_names))

    # 6. the animation is what the plan says: rigid Transform members with all nine
    #    curves over the whole loop
    if plan["animation"]:
        anim = cgfx.data.skeletal_animations[plan["animation"]]
        if not anim.looping or anim.frame_size != plan["frames"]:
            fail("animation: looping %r, %r frames, the plan says %d"
                 % (anim.looping, anim.frame_size, plan["frames"]))
        members = list(anim.member_animations_data)
        if sorted(members) != plan["animated"]:
            fail("animated bones are %r, the plan says %r" % (sorted(members), plan["animated"]))
        for name in members:
            member = anim.member_animations_data[name]
            if type(member) is not CANMBoneTransform:
                fail("animation member %s is not a Transform" % name)
            for slot in ("scale_x", "scale_y", "scale_z", "rot_x", "rot_y", "rot_z",
                         "pos_x", "pos_y", "pos_z"):
                curve = getattr(member, slot)
                if not isinstance(curve, FloatAnimationCurve):
                    fail("animation %s.%s is not a curve" % (name, slot))
                if curve.start_frame != 0 or curve.end_frame != plan["frames"]:
                    fail("animation %s.%s runs %r to %r, not 0 to %d"
                         % (name, slot, curve.start_frame, curve.end_frame, plan["frames"]))
        # The turn: pycgfx turns each quaternion key into Euler angles with y kept
        # within 90 degrees either side, so beyond that x and z jump by pi and the
        # run between two keys tumbles. The plan's turn is about Y only, so it is
        # written as it is: x and z zero, y the rising angle. pycgfx's own keys are
        # first proven to be the same poses at the same frames.
        for name, keys in plan["yaw_keys"].items():
            member = anim.member_animations_data[name]
            old = [getattr(member, slot).segments[0].keys for slot in ("rot_x", "rot_y", "rot_z")]
            if any(len(o) != len(keys) for o in old):
                fail("animation %s: %d rotation keys, the plan has %d" % (name, len(old[0]), len(keys)))
            for kx, ky, kz, (frame, degrees) in zip(old[0], old[1], old[2], keys):
                if abs(ky.frame - frame) > 1e-3:
                    fail("animation %s: a rotation key at frame %r, the plan has %r" % (name, ky.frame, frame))
                off = pose_difference((kx.value, ky.value, kz.value), (0.0, math.radians(degrees), 0.0))
                if off > 0.05:
                    fail("animation %s frame %r: pycgfx's pose is %.3f degrees off the plan's" % (name, frame, off))
            for slot, values in (("rot_x", [0.0] * len(keys)),
                                 ("rot_y", [math.radians(d) for _, d in keys]),
                                 ("rot_z", [0.0] * len(keys))):
                getattr(member, slot).segments[0].keys = [
                    StepLinear64Key(float(frame), value) for (frame, _), value in zip(keys, values)]
        # A precaution of our own (no report asks for it): pycgfx flags a bone from its
        # rest pose ("translation zero", "rotation zero", "identity"); an animated bone
        # must not carry a hint its animation makes untrue. Clearing a hint only costs
        # the console a matrix product.
        hints = BoneFlag.IsIdentity | BoneFlag.IsTranslateZero | BoneFlag.IsRotateZero
        for name in members:
            bones[name].flags = BoneFlag(bones[name].flags & ~hints)
        done.append("animation: %d bones, %d frames, looping; the turn about Y written as a rising angle;"
                    " rest-pose hints cleared on animated bones" % (len(members), plan["frames"]))

    # 7. every name-lookup tree rebuilt (pycgfx sorts the bones after building theirs)
    rebuilt = 0
    for d_ in all_dicts(cgfx, {"DICT": DICT, "BaseObject": BaseObject}):
        if d_.len():
            d_.regenerate()
            rebuilt += 1
    done.append("lookup trees rebuilt: %d" % rebuilt)

    data = pycgfx.write(cgfx)
    if len(data) > MAX_SIZE:
        fail("the CGFX is %d bytes, over %d" % (len(data), MAX_SIZE))
    if tex:
        pixels = cgfx.data.textures[tex["name"]].pixel_based_image.data
        at = data.find(pixels)
        if at < 0 or data.find(pixels, at + 1) >= 0:
            fail("the texture data is not found once in the written file")
        if at % tex["align"]:
            fail("the texture data starts at 0x%X, %d bytes past a multiple of %d: the name must grow by %d"
                 " characters" % (at, at % tex["align"], tex["align"], (tex["align"] - at % tex["align"]) % tex["align"]))
        done.append("texture data at 0x%X in the file, a multiple of %d" % (at, tex["align"]))
    with open(out_path, "wb") as f:
        f.write(data)
    for line in done:
        print("fix_cgfx: " + line)
    print("fix_cgfx: wrote %s, %d bytes" % (out_path, len(data)))


if __name__ == "__main__":
    main(sys.argv)
