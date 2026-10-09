"""Poly Haven scanned rock faces -> distant mountain meshes for Unreal (Blender 5.0, background).

  /Applications/Blender.app/Contents/MacOS/Blender -b --disable-autoexec --factory-startup \
      --python unreal/Tools/blender/world/prep_rocks.py

Input: SourceArt/Environment/PolyHaven/Models/<asset>/<asset>_2k.blend (Tools/world/fetch_polyhaven_models.py, role "mountain").
Output: SourceArt/Environment/PolyHaven/Rocks/SM_Rock_<name>_LOD<n>.fbx + rocks.json (git-ignored, rebuildable), read by
Content/Python/fourfold/world/foliage.py (meshes kind "rock"), placed x8-25 as the mountain ring (Tools/world/scenery_instances.py).
The scans are one-sided faces: the manifest stores face_yaw_deg (Unreal yaw of the area-weighted face normal) so the
placement turns each face toward the arena.  Same mesh plumbing as prep_trees.py (UV attribute, origin at the base).
"""
import json
import math
import os
import sys

import bpy
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import prep_trees as PT  # noqa: E402

OUT = os.path.join(PT.PH, "Rocks")
# mesh -> (asset, [(source LOD object, decimate ratio or None)] per game LOD)
ROCKS = {
    "SM_Rock_Strata": ("mountainside", [("mountainside_LOD2", None), ("mountainside_LOD3", None)]),
    "SM_Rock_Granite": ("namaqualand_cliff_02", [("namaqualand_cliff_02_LOD2", None), ("namaqualand_cliff_02_LOD3", None)]),
    "SM_Rock_Cliff": ("coastal_cliff_02", [("coastal_cliff_02_LOD3", 0.5), ("coastal_cliff_02_LOD3", 0.15)]),
}


def face_yaw(me):
    """Unreal yaw (deg) of the area-weighted horizontal face normal.  Blender (x, y, z) lands in Unreal at (x, -y, z)."""
    n = np.empty(len(me.polygons) * 3, np.float32)
    a = np.empty(len(me.polygons), np.float32)
    me.polygons.foreach_get("normal", n)
    me.polygons.foreach_get("area", a)
    v = (n.reshape(-1, 3) * a[:, None]).sum(0)
    return round(math.degrees(math.atan2(-float(v[1]), float(v[0]))), 2)


def main():
    os.makedirs(OUT, exist_ok=True)
    manifest = {"license": "CC0 (polyhaven.com)", "meshes": {}}
    for name, (asset, lods) in ROCKS.items():
        bpy.ops.wm.open_mainfile(filepath=os.path.join(PT.MODELS, asset, f"{asset}_2k.blend"), load_ui=False)
        tex_dir = os.path.join(PT.MODELS, asset, "textures")
        entry = dict(kind="rock", source=asset, lods=[])
        for li, (src_name, ratio) in enumerate(lods):
            src = bpy.data.objects[src_name]
            ob, slots, info = PT.make_game_mesh(src, f"{name}_LOD{li}", dict(bark=ratio) if ratio else None, tex_dir)
            if li == 0:
                entry["face_yaw_deg"] = face_yaw(ob.data)
                entry["slots"] = {"Rock": next(iter(slots.values()))}
            for i, m in enumerate(ob.data.materials):             # one slot named Rock
                ob.data.materials[i] = bpy.data.materials.get("Rock") or bpy.data.materials.new("Rock")
            fbx = os.path.join(OUT, f"{name}_LOD{li}.fbx")
            PT.export(ob, fbx)
            entry["lods"].append(dict(fbx=os.path.relpath(fbx, PT.PH), source=src_name, **info))
            bpy.data.objects.remove(ob)
            print(f"[prep_rocks] {name} LOD{li} <- {src_name}: {info}")
        entry["height_m"] = entry["lods"][0]["height_m"]
        manifest["meshes"][name] = entry
    with open(os.path.join(OUT, "rocks.json"), "w") as f:
        json.dump(manifest, f, indent=1)
    print("[prep_rocks] wrote", os.path.join(OUT, "rocks.json"))


if __name__ == "__main__":
    main()
