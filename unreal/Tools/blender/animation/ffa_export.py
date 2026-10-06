"""Fourfold animation toolkit - Blender side: bake solved clips onto the frozen export armature and write the FBX
through the frozen ff_fbx_export.export_animation_fbx (60 fps, one action per file, first key at frame 0).

The export armature is exactly ff_rig_spec.build_armature() (validated before every export); the authoring control
layer lives in numpy (ffa_solver), so nothing extra is ever added to the exported skeleton.  Every ANIM_BONES bone
gets a rotation key on every frame and the pelvis a location key; helpers (ik_*, interaction, center_of_mass) and the
ff_* spring bones stay at rest.
"""
import math
import os

import bpy
import numpy as np

import ffa_rig as rig
import ff_fbx_export as fbx
import ff_rig_spec as spec
from ffa_math import mat_to_quat

_ARM = {}


def reset_scene():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scn = bpy.context.scene
    scn.unit_settings.system = "METRIC"
    scn.unit_settings.scale_length = 1.0
    scn.render.fps = 60
    scn.render.fps_base = 1.0
    _ARM.clear()


def armature():
    if "a" in _ARM and _ARM["a"].name in bpy.data.objects:
        return _ARM["a"]
    arm = spec.build_armature()
    probs = spec.validate_armature(arm)
    if probs:
        raise RuntimeError("export armature does not match the frozen spec: " + "; ".join(probs[:5]))
    _ARM["a"] = arm
    return arm


def _quats(res):
    """Per bone: (N+1, 4) quaternion arrays with sign continuity."""
    out = {}
    for b in rig.ANIM_BONES:
        qs = np.array([mat_to_quat(p.q[b]) for p in res.poses])
        for i in range(1, len(qs)):
            if float(np.dot(qs[i], qs[i - 1])) < 0.0:
                qs[i] = -qs[i]
        out[b] = qs
    return out


def make_action(arm, res):
    name = "A_" + res.name
    old = bpy.data.actions.get(name)
    if old is not None:
        bpy.data.actions.remove(old)
    act = bpy.data.actions.new(name)
    if arm.animation_data is None:
        arm.animation_data_create()
    arm.animation_data.action = act
    frames = np.arange(res.frames + 1, dtype=float)
    q = _quats(res)

    def curve(path, idx, group, vals):
        fc = act.fcurves.new(path, index=idx, action_group=group)
        fc.keyframe_points.add(len(vals))
        co = np.empty(2 * len(vals))
        co[0::2] = frames
        co[1::2] = vals
        fc.keyframe_points.foreach_set("co", co.tolist())
        fc.keyframe_points.foreach_set("interpolation", [0] * len(vals))   # CONSTANT; the exporter bakes per frame
        fc.update()

    for b in rig.ANIM_BONES:
        for i in range(4):
            curve(f'pose.bones["{b}"].rotation_quaternion', i, b, q[b][:, i])
    loc = np.array([p.pelvis_loc for p in res.poses])
    for i in range(3):
        curve('pose.bones["pelvis"].location', i, "pelvis", loc[:, i])
    return act


def export_clip(res, out_dir):
    arm = armature()
    probs = spec.validate_armature(arm)
    if probs:
        raise RuntimeError("armature changed: " + "; ".join(probs[:5]))
    for pb in arm.pose.bones:
        pb.rotation_mode = "QUATERNION"
    act = make_action(arm, res)
    path = os.path.join(out_dir, f"A_{res.name}.fbx")
    fbx.export_animation_fbx(path, arm, act, 0, res.frames)
    arm.animation_data.action = None
    bpy.data.actions.remove(act)
    for pb in arm.pose.bones:
        pb.rotation_quaternion = (1, 0, 0, 0)
        pb.location = (0, 0, 0)
    return path


def check_fbx(path, res, frames=None, tol_deg=0.12, tol_m=0.0008):
    """Re-import an exported clip into an empty scene and compare bone transforms with the solver's FK.
    Returns a dict (ok, worst angle / offset, frame range, bone count)."""
    reset_scene()
    bpy.ops.import_scene.fbx(filepath=path, automatic_bone_orientation=False, anim_offset=0.0)
    arms = [o for o in bpy.data.objects if o.type == "ARMATURE"]
    if not arms:
        return {"ok": False, "error": "no armature"}
    a = arms[0]
    act = a.animation_data.action if a.animation_data else None
    if act is None:
        return {"ok": False, "error": "no action"}
    f0 = int(round(act.frame_range[0]))
    nkeys = int(round(act.frame_range[1] - act.frame_range[0])) + 1
    frames = frames or sorted({0, res.frames // 2, res.frames})
    worst_a, worst_p = 0.0, 0.0
    for f in frames:
        bpy.context.scene.frame_set(f0 + f)
        W, Hd, _ = rig.fk(res.poses[f])
        for b in rig.ANIM_BONES:
            pb = a.pose.bones[b]
            M = np.array(pb.matrix.to_3x3())
            c = (np.trace(M.T @ W[b]) - 1.0) * 0.5
            worst_a = max(worst_a, math.degrees(math.acos(max(-1.0, min(1.0, c)))))
            worst_p = max(worst_p, float(np.linalg.norm(np.array(pb.head) - Hd[b])))
    names = {b.name for b in a.data.bones}
    want = {b["name"] for b in spec.BONES}
    ok = worst_a < tol_deg and worst_p < tol_m and names == want and nkeys == res.frames + 1 and f0 == 0
    reset_scene()
    return {"ok": ok, "worst_deg": round(worst_a, 4), "worst_mm": round(worst_p * 1000, 3), "first_frame": f0,
            "keys": nkeys, "bones": len(names), "bones_match": names == want}
