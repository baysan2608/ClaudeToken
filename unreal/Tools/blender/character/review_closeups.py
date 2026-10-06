"""Close-up review renders of a saved build scene (iteration aid; nothing here is shipped).

    B=/home/user/tools/bpyenv/bin/python
    $B unreal/Tools/blender/character/build_character.py --no-previews --quick --out /tmp/q --blend /tmp/q.blend
    $B unreal/Tools/blender/character/review_closeups.py /tmp/q.blend /tmp/q /tmp/q/review chest,face,tailB [pose]

Shots: see SHOTS (target, camera yaw, distance, camera height, lens); fistL / fistR frame the hand of the pose.
[pose] is a name from ch_pose.library() (default "rest"). Cycles CPU, 480 px, 24 samples.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "..", "common"))

import bpy  # noqa: E402
from mathutils import Vector  # noqa: E402

# name: (target, yaw deg (0 = in front), distance m, camera height m, lens mm)
SHOTS = {
    "body": ((0, 0, 0.92), 0, 5.0, 1.05, 60),
    "chest": ((0, -0.05, 1.32), 0, 1.3, 1.36, 60),
    "chestClose": ((0, -0.08, 1.30), 0, 0.55, 1.22, 85),
    "chest34": ((0, -0.05, 1.32), 35, 1.3, 1.36, 60),
    "back": ((0, 0.05, 1.32), 180, 1.3, 1.36, 60),
    "face": ((0, -0.06, 1.655), 0, 0.85, 1.67, 85),
    "face34": ((0, -0.06, 1.655), 28, 0.85, 1.67, 85),
    "mouth": ((0, -0.09, 1.585), 10, 0.40, 1.60, 85),
    "earL": ((0.07, 0.0, 1.62), 70, 0.45, 1.64, 85),
    "earR": ((-0.07, 0.0, 1.62), -70, 0.45, 1.64, 85),
    "earFrontL": ((0.07, -0.02, 1.64), 0, 0.30, 1.645, 85),
    "ear34": ((0.07, -0.02, 1.64), 35, 0.30, 1.66, 85),
    "tail": ((0, 0.10, 1.66), 115, 0.65, 1.70, 85),
    "tailB": ((0, 0.12, 1.66), 180, 0.65, 1.72, 85),
    "tailB34": ((0, 0.12, 1.66), 150, 0.65, 1.62, 85),
}


def main(argv):
    blend, tex, out, shots = argv[0], argv[1], argv[2], argv[3].split(",")
    pose_name = argv[4] if len(argv) > 4 else "rest"
    bpy.ops.wm.open_mainfile(filepath=blend)
    import ch_pose
    import ch_previews
    import ch_render
    lod0 = bpy.data.objects["SK_Fighter"]
    for o in bpy.data.objects:
        if o.type == "MESH" and o.name.startswith("SK_Fighter_LOD"):
            o.hide_render = True
    arm = [o for o in bpy.data.objects if o.type == "ARMATURE"][0]
    arm.hide_render = True
    ch_previews.assign_preview_materials([lod0], tex, "player")
    ch_previews._studio(True, (480, 480))
    bpy.context.scene.cycles.samples = 24
    cam = ch_render.camera(lens=85)
    {p.name: p for p in ch_pose.library()}[pose_name].apply(arm)
    bpy.context.view_layer.update()
    os.makedirs(out, exist_ok=True)
    for s in shots:
        if s in ("fistL", "fistR"):
            sd = s[-1].lower()
            mw = arm.matrix_world
            tip = mw @ arm.pose.bones[f"middle_03_{sd}"].tail
            wrist = mw @ arm.pose.bones[f"hand_{sd}"].head
            sx = 1.0 if sd == "l" else -1.0
            cam.data.lens = 85
            ch_render.look_dir(cam, wrist.lerp(tip, 0.55), Vector((-0.55 * sx, -0.80, 0.15)), 0.40)
        else:
            t, yaw, dist, h, lens = SHOTS[s]
            cam.data.lens = lens
            ch_render.look_at(cam, t, yaw, dist, h)
        ch_render.render(os.path.join(out, f"{s}.png"))


if __name__ == "__main__":
    main(sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else sys.argv[1:])
