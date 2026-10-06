"""Iteration helper: regenerate the textures from a saved build scene (build_character.py --blend X) without
rebuilding the mesh. The AO bake is cached next to the textures (ao_cache.npz) and reused while the mesh is the same.

    /home/user/tools/bpyenv/bin/python unreal/Tools/blender/character/retexture.py <scene.blend> <out_dir> [slot ...]
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "..", "common"))

import bpy  # noqa: E402

import ch_body  # noqa: E402
import ch_regions as R  # noqa: E402
import ch_textures  # noqa: E402


class _Named:
    def __init__(self, name):
        self.name = name


def main(argv):
    blend, out = argv[0], argv[1]
    slots = argv[2:] or None
    bpy.ops.wm.open_mainfile(filepath=blend)
    obj = bpy.data.objects["SK_Fighter"]
    for o in bpy.data.objects:
        if o.type == "MESH" and o is not obj:
            o.hide_render = True
    bpy.data.objects["root"].hide_render = True
    names = [n for n in obj.get("ff_part_names", [])] or None
    if names is None:
        raise SystemExit("scene has no ff_part_names (rebuild with build_character.py)")
    st = ch_body.BodyState()
    lm = R.landmarks(st)
    ch_textures.build_all(obj, [_Named(n) for n in names], lm, out, slots=slots,
                          ao_cache=os.path.join(out, f"ao_cache_{int(os.path.getmtime(blend))}.npz"))


if __name__ == "__main__":
    main(sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else sys.argv[1:])
