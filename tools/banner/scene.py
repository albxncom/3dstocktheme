"""The banner's scene: scene.json and texture.png next to this file, read into the form glb.py takes (its docstring
says what each key means). The numbers are the scene as it was built, written with Python's exact float text, so the
GLB made from them is the same byte for byte.
"""
import json
import os

HERE = os.path.dirname(os.path.abspath(__file__))


def load(path=os.path.join(HERE, "scene.json")):
    with open(path) as f:
        scene = json.load(f)
    tex = scene["texture"]
    with open(os.path.join(os.path.dirname(os.path.abspath(path)), tex["png"]), "rb") as f:
        tex["png"] = f.read()
    for part in scene["parts"]:
        part["pivot"] = tuple(part["pivot"])
        part["vertices"] = [tuple(tuple(c) for c in v) for v in part["vertices"]]
        part["triangles"] = [tuple(t) for t in part["triangles"]]
        part["keys"] = [(frame, tuple(offset), yaw) for frame, offset, yaw in part["keys"]]
    return scene
