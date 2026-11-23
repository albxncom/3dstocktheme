# Banner model

The pipeline that made `app/meta/banner.cgfx`, the 3D model of the CIA's banner (the HOME Menu shows it above the icon): the house turning about its own axis and the words "3DStock" and "Theme" rising and falling, drawn as the application's Stock start screen draws them. `make -C app banner` packs the model with the chime (`bannertool makebanner -ci`); `app/meta/banner.txt` states its size and SHA-256, and `app/meta/test_banner.py` holds the file to them.

The steps: the scene, then a GLB (`glb.py`), then pycgfx's converter in a container, then the project's fixes (`container/fix_cgfx.py`, through pycgfx's own classes and writer), then an independent reader (`check_cgfx.py`) and the comparisons in `build.py`.

| File | What it is |
| --- | --- |
| `scene.json`, `texture.png` | the scene: three rigid parts (vertices with normals, colours and texture coordinates, triangles, pivot, keys), the material, the loop of 240 frames, the texture (256 x 128, RGBA4 in the file) |
| `scene.py` | reads the two into the form `glb.py` takes |
| `glb.py` | the GLB in the form pycgfx's `main.py` accepts, and the plan of fixes |
| `container/Dockerfile` | the image pycgfx runs in: Python 3.12 with gltflib and pillow |
| `container/fix_cgfx.py` | runs inside the container: converts with pycgfx, applies the fixes below, writes with pycgfx |
| `build.py` | the whole run, with every check, and the result against the tracked file |
| `check_cgfx.py` | the independent reader (standard library, no code of pycgfx's) |
| `check_mesh.py` | the meshes in a file against the scene, bit for bit |
| `test_check.py`, `test_tex.py` | spoiled copies of a good file, each of which the reader must refuse for its reason |

## pycgfx

pycgfx (MIT) is not in this repository: https://github.com/skyfloogle/pycgfx at commit `6c2e0cd53a5f4c3265b9aa9a1f30806f8291116e`, which `build.py` requires (`PYCGFX_HEAD`). It is run unchanged and only inside the container, without network, with its checkout mounted read-only. The image: `docker build -t banner-pycgfx:1 tools/banner/container`. Its versions are pinned to the image that made the tracked file (Python 3.12.16, gltflib 1.0.13, pillow 13.0.0 and their dependencies), not to the newest ones, because a newer pillow or Python is not proven to write the same bytes.

## Making the tracked file again

```
python3 -I tools/banner/build.py PYCGFX OUT_DIR
```

PYCGFX is the pycgfx checkout, OUT_DIR a new folder; `--image NAME` names another image. In OUT_DIR: `in.glb`, `plan.json`, `raw.cgfx` (pycgfx's plain output, which the reader must refuse), `banner.cgfx` and `banner2.cgfx` (two container runs that must be the same), `facts.json` (what the reader read back), `check.txt` and a log per container run. The run prints the file's size and SHA-256, the model as read back, the meshes and the motion against the scene, and last whether `banner.cgfx` is the tracked file byte for byte (122264 bytes, SHA-256 `efb49476344294a614cc44f467748c749b5ea6cfe51b7be22149daa60fd71287`) or the first offset where it is not. Exit 1 on the first failure.

The checks on their own:

```
python3 -I tools/banner/check_cgfx.py --anim yes --frames 240 FILE.cgfx
python3 -I tools/banner/check_mesh.py FILE.cgfx
python3 -I tools/banner/test_check.py OUT_DIR [FILE.cgfx]
python3 -I tools/banner/test_tex.py OUT_DIR [FILE.cgfx]
```

The two tests default to the tracked `banner.cgfx` and write their spoiled copies into the new folder OUT_DIR.

## The scene

`scene.json` and `texture.png` are the scene as it was built, numbers included, written with Python's exact float text; it was made by a larger work pipeline (not in this repository) that also made the test banners before it. What it holds:

- The house, a slab with depth, turns once about its own vertical axis per loop (its mesh is the same after half a turn, `same_after` 180); the two words stand on their own bones with billboard mode 4 (ScreenViewpoint) and rise and fall. No billboard mode on the roots.
- The material is unlit (ambient 1, diffuse 0), so every face shows its texels' colours at any angle; the vertex colours are white.
- The texture is the application's own drawing (`uikit/art/look_stock.py`: the lettering "3DStock" over "Theme", and the house picture) without its soft shadow, opaque (alpha OPAQUE: no alpha test, no blend). A texel the drawing covers at least half keeps its colour; the colours are carried 4 texels outward, so no pale or dark rim shows along a polygon's edge under either filter. The house's walls take one tone, the mean of the house picture's rim, from a 16 x 16 patch.
- The texture data starts on a multiple of 128 bytes in the file and inside IMAG: three spare copies of the house's last vertex (in no triangle) at the end of its list move it there.

To change the banner: edit the scene (`keys`, `material`) or draw a new `texture.png` of the same size into the same regions, build, and look at the result on a console (a test title of its own) before it replaces the tracked file; then state its size and SHA-256 in `banner.txt`.

## The fixes in `container/fix_cgfx.py`

1. One model `COMMON` and one skeletal animation `COMMON` or none; anything else is refused.
2. Every mesh's node name set to its bone's name (projects that tested on hardware do it; pycgfx leaves it empty).
3. Rigid only: a skinning mode or bone streams are refused (reported to freeze consoles).
4. The material as the plan says (here unlit), new colour objects (pycgfx's are shared class attributes), stage 2 passing the stage before on, the distribution lookup table removed.
5. The billboard mode of each bone from the plan.
6. The animation loops with the planned frame count, every member a Transform with nine curves over the whole loop (pycgfx leaves the z curves' start frame at 0); the rotation keys rewritten as x 0, z 0 and y the rising angle, after proving that pycgfx's own keys are the same poses (its Euler conversion flips past 90 degrees); the rest-pose hints cleared on animated bones.
7. Every dictionary's lookup tree rebuilt with pycgfx's `regenerate()`.
8. A file over 0x80000 bytes is refused. With a texture: the one image texture of the planned name, size, format and alignment, the material's only mapper on it, the blend and alpha test of the planned alpha mode; nothing of the texture is changed.

## What the checks cannot prove

The reader proves structure (every pointer, string and blob inside the file, every dictionary found through its tree, the model, the skeleton, the meshes, the material, the texture block, the animation); `build.py` and `check_mesh.py` prove that the file holds the scene. None of it proves that the HOME Menu accepts the file or how it shows it: that is known only from a console. This model showed on one without a fault as the banner of a test title of its own before it became the application's.
