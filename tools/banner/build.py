#!/usr/bin/env python3
"""Builds the banner's CGFX model: scene -> GLB -> pycgfx in its container -> the project's
fixes -> the independent checker.

  python3 -I build.py PYCGFX OUT_DIR [--image NAME]

PYCGFX is a checkout of pycgfx at the commit PYCGFX_HEAD names; OUT_DIR is a new folder
(nothing is cleared or reused); NAME is the image container/Dockerfile was built as
(default banner-pycgfx:1). In OUT_DIR:
  in.glb, plan.json   from glb.py and the scene (scene.py: scene.json, texture.png)
  raw.cgfx            pycgfx's main.py as it is, kept for comparison; the checker
                      must refuse it (it has no mesh node names)
  banner.cgfx         pycgfx's converter and writer with the fixes of
                      container/fix_cgfx.py; built twice, the two must be the same
  facts.json          what check_cgfx.py read back from banner.cgfx
Then banner.cgfx is compared with the plan (names, modes, counts, material), its
meshes with the scene's bit for bit (check_mesh.py), and the animation read back is
played against the scene's keys, frame by half frame.
Last, banner.cgfx is held against the tracked app/meta/banner.cgfx: the same bytes, or the
first offset where they differ. Exit 1 on the first failure.

Every container run: --network none, the pycgfx checkout mounted read-only, the output
folder as /work. Nothing of pycgfx runs outside the container.
"""
import hashlib
import json
import math
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.dont_write_bytecode = True
sys.path.insert(0, HERE)
import check_cgfx   # noqa: E402
import check_mesh   # noqa: E402
import glb          # noqa: E402
import scene        # noqa: E402

PYCGFX_HEAD = "6c2e0cd53a5f4c3265b9aa9a1f30806f8291116e"
IMAGE = "banner-pycgfx:1"
TRACKED = os.path.join(HERE, "..", "..", "app", "meta", "banner.cgfx")
MAX_TURN_ERROR = 0.01   # degrees
MAX_MOVE_ERROR = 1e-3   # units


class BuildError(Exception):
    pass


def container(pycgfx, image, work, log_name, *command):
    args = ["docker", "run", "--rm", "--network", "none",
            "-v", pycgfx + ":/pycgfx:ro",
            "-v", os.path.join(HERE, "container") + ":/fix:ro",
            "-v", work + ":/work", "-w", "/work", image] + list(command)
    done = subprocess.run(args, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, timeout=170)
    text = done.stdout.decode("utf-8", "replace")
    with open(os.path.join(work, log_name), "w") as f:
        f.write(" ".join(args) + "\n" + text)
    if done.returncode != 0:
        raise BuildError("%s failed (exit %d), see %s:\n%s"
                         % (" ".join(command), done.returncode, os.path.join(work, log_name),
                            "\n".join(text.splitlines()[-8:])))
    return text


def pycgfx_head(pycgfx):
    with open(os.path.join(pycgfx, ".git", "HEAD")) as f:
        head = f.read().strip()
    if head.startswith("ref: "):
        with open(os.path.join(pycgfx, ".git", head[5:])) as f:
            head = f.read().strip()
    return head


def rotation(x, y, z):
    """Rz * Ry * Rx, the order of the bone's Euler angles (pycgfx's own conversion)."""
    cx, sx, cy, sy, cz, sz = math.cos(x), math.sin(x), math.cos(y), math.sin(y), math.cos(z), math.sin(z)
    return ((cz * cy, cz * sy * sx - sz * cx, cz * sy * cx + sz * sx),
            (sz * cy, sz * sy * sx + cz * cx, sz * sy * cx - cz * sx),
            (-sy, cy * sx, cy * cx))


def angle_between(a, b):
    """The angle of the turn that takes pose a to pose b, in degrees."""
    trace = sum(a[i][j] * b[i][j] for i in range(3) for j in range(3))
    return math.degrees(math.acos(max(-1.0, min(1.0, (trace - 1.0) / 2.0))))


def wanted_pose(part, frame):
    keys = part["keys"]
    for k0, k1 in zip(keys, keys[1:]):
        if frame <= k1[0]:
            t = (frame - k0[0]) / (k1[0] - k0[0])
            offset = [k0[1][i] + (k1[1][i] - k0[1][i]) * t for i in range(3)]
            return [part["pivot"][i] + offset[i] for i in range(3)], k0[2] + (k1[2] - k0[2]) * t
    raise BuildError("frame %r past the last key" % frame)


def compare_motion(scene, facts):
    """Plays the animation read back from the file against the scene's keys."""
    report = []
    animation = facts["animation"]
    for part in scene["parts"]:
        if not part.get("keys"):
            continue
        slots = animation["members"][part["name"]]["slots"]
        if any(slots[s] is None for s in check_cgfx.SLOTS):
            raise BuildError("%s: a channel is marked ignored" % part["name"])
        worst_turn = worst_move = 0.0
        for half in range(0, 2 * scene["frames"] + 1):
            frame = half / 2.0
            v = {s: check_cgfx.sample(slots[s], frame) for s in check_cgfx.SLOTS}
            position, yaw = wanted_pose(part, frame)
            if any(abs(v["scale_" + a] - 1.0) > 1e-6 for a in "xyz"):
                raise BuildError("%s: scale is not 1 at frame %g" % (part["name"], frame))
            worst_move = max(worst_move, max(abs(v["pos_" + a] - position[i]) for i, a in enumerate("xyz")))
            worst_turn = max(worst_turn, angle_between(rotation(v["rot_x"], v["rot_y"], v["rot_z"]),
                                                       rotation(0.0, math.radians(yaw), 0.0)))
        if worst_turn > MAX_TURN_ERROR or worst_move > MAX_MOVE_ERROR:
            raise BuildError("%s: the animation in the file is off the scene's by %.3f degrees, %.5f units"
                             % (part["name"], worst_turn, worst_move))
        report.append("%s off by at most %.3f degrees and %.6f units" % (part["name"], worst_turn, worst_move))
    return report


def compare_plan(scene, plan, facts):
    model = facts["model"]
    bones = {b["name"]: b for b in model["bones"]}
    for name, mode in plan["billboard"].items():
        want = mode if plan["billboard_all"] is None else plan["billboard_all"]
        if bones[name]["billboard"] != want:
            raise BuildError("bone %s: billboard mode %d in the file, %d wanted" % (name, bones[name]["billboard"], want))
    if plan["billboard_all"] is not None and any(b["billboard"] != plan["billboard_all"] for b in model["bones"]):
        raise BuildError("not every bone has billboard mode %d" % plan["billboard_all"])
    if plan["billboard_all"] is None and any(
            b["billboard"] for b in model["bones"] if b["name"] not in plan["billboard"]):
        raise BuildError("a bone outside the plan has a billboard mode")
    if {m["name"]: m["node"] for m in model["meshes"]} != plan["mesh_nodes"]:
        raise BuildError("mesh node names differ from the plan")
    if (model["triangles"], model["vertices"]) != (plan["triangles"], plan["vertices"]):
        raise BuildError("%d triangles and %d vertices in the file, the scene has %d and %d"
                         % (model["triangles"], model["vertices"], plan["triangles"], plan["vertices"]))
    if len(model["bones"]) != len(scene["parts"]) + 2:
        raise BuildError("%d bones, expected pycgfx's root, the scene's root and %d parts"
                         % (len(model["bones"]), len(scene["parts"])))
    for part in scene["parts"]:
        rest = wanted_pose(part, 0)[0] if part.get("keys") else list(part["pivot"])
        if any(abs(bones[part["name"]]["position"][i] - rest[i]) > 1e-5 for i in range(3)):
            raise BuildError("bone %s rests at %r, not at %r" % (part["name"], bones[part["name"]]["position"], rest))
    if len(model["materials"]) != 1:
        raise BuildError("%d materials" % len(model["materials"]))
    mat = model["materials"][0]
    want = plan["material"]
    if (abs(mat["ambient"][0] - want["ambient"]) > 1e-6 or abs(mat["diffuse"][0] - want["diffuse"]) > 1e-6
            or mat["constant0"][:3] != [0.0, 0.0, 0.0] or mat["lighting_tables"]
            or mat["textures"] != (1 if plan.get("texture") else 0)
            or mat["stages"][2]["src_rgb"] != 0xFFF or mat["stages"][2]["combine_rgb"] != 0):
        raise BuildError("the material is not the matte one of the plan: %r" % mat)
    compare_texture(plan, facts)
    animation = facts["animation"]
    if plan["animation"]:
        if sorted(animation["members"]) != plan["animated"]:
            raise BuildError("animated bones %r, the plan says %r" % (sorted(animation["members"]), plan["animated"]))
        for name in plan["animated"]:
            if bones[name]["flags"] & 7:   # identity, translation zero, rotation zero
                raise BuildError("animated bone %s still carries a rest-pose hint (flags 0x%X)"
                                 % (name, bones[name]["flags"]))
    elif animation is not None:
        raise BuildError("an animation in a still scene")


BLEND_WANTED = {"BLEND": (["SrcAlpha", "InvSrcAlpha"] * 2, 1, 0), "MASK": (["One", "Zero"] * 2, 3, 1),
                "OPAQUE": (["One", "Zero"] * 2, 3, 0)}


def compare_texture(plan, facts):
    """T10, T11, T13, T14: the one texture of the plan, its material's mapper, blend, depth flags and alpha test as read
    back by the checker; a scene without a texture has none."""
    tex = plan.get("texture")
    got = facts["textures"]
    if not tex:
        if got:
            raise BuildError("a texture in a scene without one")
        return
    if len(got) != 1 or (got[0]["name"], got[0]["width"], got[0]["height"], got[0]["format"]) != (
            tex["name"], tex["width"], tex["height"], tex["format"]) or got[0]["data_at"] % tex["align"]:
        raise BuildError("the texture in the file is not the plan's: %r" % got)
    mat = facts["model"]["materials"][0]
    blend, depth, test = BLEND_WANTED[tex["alpha"]]
    if ([m["texture"] for m in mat["mappers"]] != [tex["name"]] or mat["blend"] != blend
            or mat["depth_flags"] != depth or mat["alpha_test"]["enabled"] != test
            or (test and mat["alpha_test"]["cutoff"] != int(tex["cutoff"] * 255))):
        raise BuildError("the material's texture, blend or alpha test is not the plan's %s: %r" % (tex["alpha"], mat))


def build(pycgfx, image, work):
    scene_ = scene.load()
    os.makedirs(work)   # fails if it is there: an output folder is never reused
    plan = glb.plan(scene_)
    with open(os.path.join(work, "in.glb"), "wb") as f:
        f.write(glb.build(scene_))
    with open(os.path.join(work, "plan.json"), "w") as f:
        json.dump(plan, f, indent=1, sort_keys=True)
        f.write("\n")

    container(pycgfx, image, work, "raw.log", "python3", "-B", "/pycgfx/main.py", "in.glb", "raw.cgfx")
    with open(os.path.join(work, "raw.cgfx"), "rb") as f:
        raw = f.read()
    try:
        check_cgfx.check(raw)
        raise BuildError("the checker passed pycgfx's plain output; it must refuse it")
    except check_cgfx.CheckError as e:
        raw_verdict = str(e)

    for out in ("banner.cgfx", "banner2.cgfx"):
        container(pycgfx, image, work, out + ".log", "python3", "-B", "-I", "/fix/fix_cgfx.py", "/pycgfx",
                  "in.glb", "plan.json", out)
    with open(os.path.join(work, "banner.cgfx"), "rb") as f:
        data = f.read()
    with open(os.path.join(work, "banner2.cgfx"), "rb") as f:
        if f.read() != data:
            raise BuildError("two builds of the same scene differ")

    try:
        facts = check_cgfx.check(data, "yes" if plan["animation"] else "no",
                                 plan["frames"] if plan["animation"] else None)
    except check_cgfx.CheckError as e:
        raise BuildError("check_cgfx refuses banner.cgfx: %s" % e)
    with open(os.path.join(work, "facts.json"), "w") as f:
        json.dump(facts, f, indent=1, sort_keys=True)
        f.write("\n")
    compare_plan(scene_, plan, facts)
    for t in facts["textures"]:
        if hashlib.sha256(data[t["data_at"]:t["data_at"] + t["bytes"]]).hexdigest() != plan["texture"]["sha256"]:
            raise BuildError("the texture data in the file is not the scene's own RGBA4 encoding")
    try:
        meshes = check_mesh.compare(scene_, data)
    except check_cgfx.CheckError as e:
        raise BuildError("check_mesh refuses banner.cgfx: %s" % e)
    motion = compare_motion(scene_, facts) if plan["animation"] else []

    lines = ["%s, %d bytes, sha256 %s" % (os.path.join(work, "banner.cgfx"), len(data), hashlib.sha256(data).hexdigest()),
             "  raw.cgfx (%d bytes) refused: %s" % (len(raw), raw_verdict),
             "  two builds identical; the plan's names, modes, counts and material are in the file"]
    lines += ["  " + line for line in check_cgfx.summary(facts)]
    lines += ["  mesh: " + line for line in meshes]
    lines += ["  motion: " + line for line in motion]
    lines.append("  " + against_tracked(data))
    with open(os.path.join(work, "check.txt"), "w") as f:
        f.write("\n".join(lines) + "\n")
    return lines


def against_tracked(data):
    """The made file against the tracked one: the same, or where they first differ."""
    with open(TRACKED, "rb") as f:
        tracked = f.read()
    if data == tracked:
        return "the same bytes as the tracked app/meta/banner.cgfx"
    at = next((i for i, (a, b) in enumerate(zip(data, tracked)) if a != b), min(len(data), len(tracked)))
    return "NOT the tracked app/meta/banner.cgfx (%d bytes): the first difference at offset 0x%X" % (len(tracked), at)


def main(argv):
    args = argv[1:]
    image = IMAGE
    if "--image" in args:
        i = args.index("--image")
        image, args = args[i + 1], args[:i] + args[i + 2:]
    if len(args) != 2:
        print(__doc__.split("\n\n")[1])
        return 2
    pycgfx, work = (os.path.abspath(a) for a in args)
    try:
        if pycgfx_head(pycgfx) != PYCGFX_HEAD:
            raise BuildError("the pycgfx checkout is at %s, not at %s" % (pycgfx_head(pycgfx), PYCGFX_HEAD))
        for line in build(pycgfx, image, work):
            print(line)
    except (BuildError, subprocess.TimeoutExpired, OSError) as e:
        print("build.py: FAILED: %s" % e)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
