"""Self-test for the frozen rig spec + FBX settings (run with the bpy venv):
    /home/user/tools/bpyenv/bin/python unreal/Tools/blender/common/test_rig_spec.py [out_dir]
Builds the armature, validates it, checks the +X flexion convention numerically, exports a proxy skinned mesh FBX and
a test animation FBX, re-imports both into an empty scene and compares bone names / parents, and renders a rest-pose
and flexion contact sheet (Cycles CPU) into out_dir."""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bpy  # noqa: E402
from mathutils import Quaternion, Vector  # noqa: E402

import ff_rig_spec as spec  # noqa: E402
import ff_fbx_export as fx  # noqa: E402

OUT = sys.argv[1] if len(sys.argv) > 1 else "/tmp/ff_rig_test"
os.makedirs(OUT, exist_ok=True)


def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)


def world_tip(obj, name):
    pb = obj.pose.bones[name]
    return obj.matrix_world @ pb.tail


def world_head(obj, name):
    pb = obj.pose.bones[name]
    return obj.matrix_world @ pb.head


def check_flexion(obj):
    """+X rotation must move each bone's tail toward its local +Z at rest (the documented convention)."""
    bad = []
    for b in spec.BONES:
        if b["name"].startswith(("ik_", "interaction", "center_of_mass")):
            continue
        pb = obj.pose.bones[b["name"]]
        rest_tail = Vector(b["tail"])
        yax = (Vector(b["tail"]) - Vector(b["head"])).normalized()
        z = Vector(b["z"]) - yax * Vector(b["z"]).dot(yax)
        z = z.normalized()
        pb.rotation_quaternion = Quaternion((1, 0, 0), math.radians(20))
        bpy.context.view_layer.update()
        moved = world_tip(obj, b["name"]) - rest_tail
        pb.rotation_quaternion = Quaternion()
        bpy.context.view_layer.update()
        if moved.length < 1e-6 or moved.normalized().dot(z) < 0.5:
            bad.append(b["name"])
    return bad


def proxy_mesh(arm):
    """One capsule-ish cylinder per deform bone, rigidly weighted (preview only)."""
    objs = []
    for b in spec.BONES:
        if not b["deform"]:
            continue
        h, t = Vector(b["head"]), Vector(b["tail"])
        ln = (t - h).length
        r = 0.012 if any(k in b["name"] for k in ("index", "middle", "ring", "pinky", "thumb")) else 0.035
        if b["name"].startswith(("spine", "pelvis")):
            r = 0.11
        if b["name"] == "head":
            r = 0.09
        if b["name"].startswith(("thigh_", "calf_")) and "twist" not in b["name"]:
            r = 0.06
        if "twist" in b["name"] or b["name"].startswith("ff_"):
            continue
        bpy.ops.mesh.primitive_cylinder_add(vertices=10, radius=r, depth=ln, location=(h + t) / 2)
        o = bpy.context.active_object
        o.name = "px_" + b["name"]
        o.rotation_mode = "QUATERNION"
        o.rotation_quaternion = Vector((0, 0, 1)).rotation_difference((t - h).normalized())
        vg = o.vertex_groups.new(name=b["name"])
        vg.add(list(range(len(o.data.vertices))), 1.0, "REPLACE")
        objs.append(o)
    bpy.ops.object.select_all(action="DESELECT")
    for o in objs:
        o.select_set(True)
    bpy.context.view_layer.objects.active = objs[0]
    bpy.ops.object.join()
    body = bpy.context.active_object
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    body.name = "SK_Proxy"
    mod = body.modifiers.new("Armature", "ARMATURE")
    mod.object = arm
    body.parent = arm
    return body


def render_sheet(arm, act):
    """Rest pose (front, side) and the flexed test frame, Cycles CPU, small."""
    scn = bpy.context.scene
    scn.render.engine = "CYCLES"
    scn.cycles.device = "CPU"
    scn.cycles.samples = 8
    scn.render.resolution_x, scn.render.resolution_y = 360, 480
    scn.render.film_transparent = False
    if scn.world is None:
        scn.world = bpy.data.worlds.new("w")
    scn.world.color = (0.8, 0.8, 0.82)
    sun = bpy.data.objects.new("sun", bpy.data.lights.new("sun", "SUN"))
    sun.rotation_euler = (math.radians(50), 0, math.radians(30))
    scn.collection.objects.link(sun)
    cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
    cam.data.type = "ORTHO"
    cam.data.ortho_scale = 2.1
    scn.collection.objects.link(cam)
    scn.camera = cam
    shots = [("front_rest", 0, (0, -4, 0.95), (math.radians(90), 0, 0)),
             ("side_rest", 0, (4, 0, 0.95), (math.radians(90), 0, math.radians(90))),
             ("front_flex", 30, (0, -4, 0.95), (math.radians(90), 0, 0)),
             ("side_flex", 30, (4, 0, 0.95), (math.radians(90), 0, math.radians(90)))]
    for nm, fr, loc, rot in shots:
        scn.frame_set(fr)
        cam.location = loc
        cam.rotation_euler = rot
        scn.render.filepath = os.path.join(OUT, nm + ".png")
        bpy.ops.render.render(write_still=True)


def main():
    reset()
    scn = bpy.context.scene
    scn.unit_settings.scale_length = 1.0
    arm = spec.build_armature()
    probs = spec.validate_armature(arm)
    print("validate:", "OK" if not probs else probs[:10])
    bad = check_flexion(arm)
    print("flexion convention:", "OK" if not bad else bad)
    body = proxy_mesh(arm)
    mesh_fbx = fx.export_skeletal_mesh_fbx(os.path.join(OUT, "SK_Proxy.fbx"), arm, [body])
    # test animation: elbows + knees flex over 30 frames
    act = bpy.data.actions.new("A_test_flex")
    arm.animation_data_create()
    arm.animation_data.action = act
    for f, ang in ((0, 0.0), (30, 70.0)):
        for n in ("lowerarm_l", "lowerarm_r", "calf_l", "calf_r", "spine_03", "index_01_l", "thigh_l"):
            pb = arm.pose.bones[n]
            pb.rotation_quaternion = Quaternion((1, 0, 0), math.radians(ang))
            pb.keyframe_insert("rotation_quaternion", frame=f)
    anim_fbx = fx.export_animation_fbx(os.path.join(OUT, "A_test_flex.fbx"), arm, act, 0, 30)
    render_sheet(arm, act)
    # re-import both FBX files into an empty scene and compare the hierarchy
    for path in (mesh_fbx, anim_fbx):
        reset()
        bpy.ops.import_scene.fbx(filepath=path, automatic_bone_orientation=False)
        arms = [o for o in bpy.data.objects if o.type == "ARMATURE"]
        assert arms, "no armature re-imported from " + path
        a = arms[0]
        names = {b.name for b in a.data.bones}
        want = {b["name"] for b in spec.BONES}
        missing, extra = sorted(want - names), sorted(names - want)
        par_bad = [b["name"] for b in spec.BONES if b["name"] in names and
                   ((a.data.bones[b["name"]].parent.name if a.data.bones[b["name"]].parent else None) != b["parent"])]
        print(os.path.basename(path), "armature object:", a.name, "bones:", len(names), "missing:", missing[:5],
              "extra:", extra[:5], "parent mismatches:", par_bad[:5])
        if a.animation_data and a.animation_data.action:
            ac = a.animation_data.action
            print("  action:", ac.name, "frame range:", tuple(ac.frame_range), "fps:", bpy.context.scene.render.fps)
    print("OK" if not probs and not bad else "PROBLEMS")


main()
