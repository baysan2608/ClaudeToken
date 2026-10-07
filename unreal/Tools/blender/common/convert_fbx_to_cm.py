"""Re-writes existing Fourfold FBX files through the shared exporter (ff_fbx_export), which now writes centimetres.

One-off migration for files exported before the centimetre fix (their skeleton imported into Unreal with a x100 root
scale). Each file is imported into an empty scene (metres, Blender's FBX importer undoes the old unit factor) and
exported again with the frozen settings, so the result matches what a fresh build would write.

  Blender -b --python unreal/Tools/blender/common/convert_fbx_to_cm.py -- [--out DIR] FILE.fbx [FILE.fbx ...]
Without --out the files are overwritten in place. Skeletal meshes: SK_*.fbx; everything else is treated as one clip.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import ff_fbx_export as fx  # noqa: E402


def _import(path):
    import bpy
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(filepath=path, use_custom_normals=True, use_image_search=False, use_custom_props=False,
                             ignore_leaf_bones=False, automatic_bone_orientation=False, primary_bone_axis="Y",
                             secondary_bone_axis="X", anim_offset=0.0, global_scale=1.0)
    arms = [o for o in bpy.context.scene.objects if o.type == "ARMATURE"]
    if len(arms) != 1:
        raise RuntimeError(f"{path}: expected one armature, found {len(arms)}")
    arm = arms[0]
    arm.name = "root"
    meshes = [o for o in bpy.context.scene.objects if o.type == "MESH"]
    return arm, meshes


def convert(path, out_path):
    import bpy
    arm, meshes = _import(path)
    name = os.path.basename(path)
    if name.startswith("SK_"):
        fx.export_skeletal_mesh_fbx(out_path, arm, meshes)
        return f"mesh {name}: {len(meshes)} mesh object(s), {len(arm.data.bones)} bones"
    act = arm.animation_data.action if arm.animation_data else None
    if act is None:
        raise RuntimeError(f"{path}: no action")
    f0, f1 = (int(round(v)) for v in act.frame_range)
    fx.export_animation_fbx(out_path, arm, act, f0, f1)
    for o in list(bpy.data.objects):
        if o.type == "MESH":
            bpy.data.objects.remove(o, do_unlink=True)
    return f"clip {name}: frames {f0}..{f1}"


def main(argv):
    out_dir = None
    if argv[:1] == ["--out"]:
        out_dir, argv = argv[1], argv[2:]
    ok, bad = 0, []
    for p in argv:
        dst = os.path.join(out_dir, os.path.basename(p)) if out_dir else p
        try:
            print("FFCM", convert(p, dst))
            ok += 1
        except Exception as e:  # noqa: BLE001
            bad.append(f"{p}: {e}")
            print("FFCM FAILED", p, e)
    print(f"FFCM done: {ok} converted, {len(bad)} failed")
    if bad:
        sys.exit(1)


if __name__ == "__main__":
    main(sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else [])
