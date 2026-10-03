#!/usr/bin/env python
"""Validation for the fighter asset (run after build_fighter.py).

    /home/user/tools/bpyenv/bin/python tools/blender/validate_fighter.py [--blend X] [--out-dir D]

  1. contact sheet PNG (Cycles CPU previews: Workbench needs libEGL which headless boxes lack) of the rest pose and key
     poses of the real baked actions loaded from the .blend
  2. foot planting: (a) bone variance of the ankle/ball over every run of frames where the foot is meant to be planted
     (non-gait clips), (b) mesh-level sole skating: shoe sole vertices in ground contact (z < 8 mm) must not move
     horizontally (gait clips compensate the ground speed)
  3. expected bone positions for a few clip/frames, used by validate_fighter.gd to cross-check the Godot import
"""
import argparse
import json
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))

import bpy  # noqa: E402
import numpy as np  # noqa: E402
from mathutils import Vector  # noqa: E402

import fighter_clips as C  # noqa: E402
import fighter_dsl as dsl  # noqa: E402
import fighter_pose as fp  # noqa: E402
import fighter_preview as pv  # noqa: E402

SHEET_POSES = [
    ("rest", None), ("idle", 0), ("stance_earth", 0), ("stance_water", 0), ("stance_fire", 0), ("stance_air", 0),
    ("guard", 0), ("deflect", "c"), ("fire_jab", "c"), ("fire_release", "c"), ("earth_throw", "c"), ("earth_heavy", "c"),
    ("water_whip", "c"), ("water_draw", "c"), ("lightning_release", "c"), ("air_gust", "c"), ("hit_heavy", 6),
    ("knockdown", "last"), ("run", 3), ("walk", 0),
]


def set_action(arm, name, frame):
    act = bpy.data.actions[name]
    arm.animation_data.action = act
    bpy.context.scene.frame_set(frame)
    bpy.context.view_layer.update()


def clip_frames(name):
    act = bpy.data.actions[name]
    return int(round(act.frame_range[1]))


def sheet(arm, clips, out_png, jpeg=None):
    pv.setup_preview_scene(tile_w=300, tile_h=420, ortho=1.9, samples=8)
    pv.emulate_backface_culling()
    cam, lbl = pv._STATE["cam"], pv._STATE["lbl"]
    tmp = os.path.join(os.path.dirname(out_png), "_tile.png")
    tiles = []
    for name, f in SHEET_POSES:
        if name == "rest":
            arm.animation_data.action = None
            fp.reset_pose(arm)
            bpy.context.view_layer.update()
            label = "rest"
        else:
            n = clip_frames(name)
            if f == "c":
                c = clips[name]["contact"]
                f = int(round(c * 30)) if c is not None else 0
            elif f == "last":
                f = n
            set_action(arm, name, f)
            label = f"{name}@{f}"
        for v in ("front", "side"):
            tiles.append(pv.render_view(cam, lbl, v, f"{label} [{v}]", tmp))
    pv.assemble_sheet(tiles, 8, out_png, jpeg)
    try:
        os.remove(tmp)
    except OSError:
        pass
    arm.animation_data.action = None
    fp.reset_pose(arm)
    print("[validate] contact sheet:", out_png, jpeg or "")


DEFORM_POSES = {
    "elbows folded": dict(hl=dsl.Hd(p=(0.0, 0.14, 0.10), f=(0, 0.3, 1), m=(-1, 0, 0), e=(1, -0.3, -0.2)),
                          hr=dsl.Hd(p=(0.0, 0.14, 0.10), f=(0, 0.3, 1), m=(1, 0, 0), e=(-1, -0.3, -0.2))),
    "arms overhead": dict(hl=dsl.Hd(p=(0.05, 0.05, 0.54), f=(0, 0, 1), m=(-1, 0, 0), e=(1, -0.5, 0)),
                          hr=dsl.Hd(p=(-0.05, 0.05, 0.54), f=(0, 0, 1), m=(1, 0, 0), e=(-1, -0.5, 0))),
    "arms across": dict(hl=dsl.Hd(p=(-0.35, 0.28, -0.10), f=(-1, 0.3, 0), m=(0, 0, -1), e=(0.4, -0.2, -1)),
                        hr=dsl.Hd(p=(0.35, 0.28, -0.10), f=(1, 0.3, 0), m=(0, 0, -1), e=(-0.4, -0.2, -1))),
    "arms back": dict(hl=dsl.Hd(p=(0.20, -0.35, -0.20), f=(0, -1, -0.2), m=(-1, 0, 0), e=(0.5, -1, 0.5)),
                      hr=dsl.Hd(p=(-0.20, -0.35, -0.20), f=(0, -1, -0.2), m=(1, 0, 0), e=(-0.5, -1, 0.5))),
    "deep squat": dict(hp=(0, 0, -0.38), spine=(15, 0, 0), chest=(10, 0, 0), fl=dsl.Fd(x=0.20, y=0.05, yaw=15), fr=dsl.Fd(x=-0.20, y=0.05, yaw=-15)),
    "knee up L (90)": dict(hp=(0, 0, -0.03), fl=dsl.Fd(x=0.12, y=0.34, lift=0.62, pitch=-30, pv=0.0, kup=0.8), spine=(8, 0, 0)),
    "kick back R": dict(hp=(0, 0, -0.05), spine=(15, 0, 0), chest=(8, 0, 0), fr=dsl.Fd(x=-0.12, y=-0.55, lift=0.55, pitch=-50, pv=0.0, kup=0.6)),
    "twist + bend": dict(hips=(20, 0, -25), spine=(20, 0, -20), chest=(15, 10, -20), neck=(-25, 0, 40), head=(-10, 0, 30)),
}


def deform_check(arm, rig, out_png, jpeg=None):
    """Extreme joint poses (elbow/shoulder/knee/hip/spine) rendered front+side to eyeball skinning collapse."""
    pv.setup_preview_scene(tile_w=260, tile_h=380, ortho=2.0, samples=8)
    pv.emulate_backface_culling()
    cam, lbl = pv._STATE["cam"], pv._STATE["lbl"]
    tmp = os.path.join(os.path.dirname(out_png), "_tile.png")
    rest = dsl.rest_state()
    tiles = []
    arm.animation_data.action = None
    for label, spec in DEFORM_POSES.items():
        st = dsl.apply_spec(rig, rest, rest, spec)
        ql, hips_loc, diag = rig.solve(st)
        fp.reset_pose(arm)
        fp.apply_pose(arm, ql, hips_loc)
        bpy.context.view_layer.update()
        for v in ("front", "side"):
            tiles.append(pv.render_view(cam, lbl, v, f"{label} [{v}]", tmp, center_z=0.9))
    pv.assemble_sheet(tiles, 8, out_png, jpeg)
    try:
        os.remove(tmp)
    except OSError:
        pass
    fp.reset_pose(arm)
    print("[validate] deformation check:", out_png, jpeg or "")


def shoe_vertices(mesh_ob):
    me = mesh_ob.data
    idx = []
    side = []
    for v in me.vertices:
        w = {mesh_ob.vertex_groups[g.group].name: g.weight for g in v.groups}
        sole = sum(w.get(k, 0.0) for k in ("foot.L", "toe.L", "foot.R", "toe.R"))
        if sole > 0.9 and v.co.z < 0.05:
            idx.append(v.index)
            side.append("L" if v.co.x > 0 else "R")
    return np.array(idx), side


def eval_verts(mesh_ob, idx):
    dg = bpy.context.evaluated_depsgraph_get()
    ev = mesh_ob.evaluated_get(dg)
    me = ev.to_mesh()
    co = np.empty(len(me.vertices) * 3, dtype=np.float32)
    me.vertices.foreach_get("co", co)
    ev.to_mesh_clear()
    return co.reshape(-1, 3)[idx]


def foot_report(arm, mesh_ob, rig, clips):
    report = {}
    idx, side = shoe_vertices(mesh_ob)
    side = np.array(side)
    print(f"[validate] {len(idx)} sole vertices tracked")
    for name in clips:
        cd = C.CLIPS[name]
        n = clips[name]["frames"]
        act = bpy.data.actions[name]
        arm.animation_data.action = act
        P = []
        ank = {"L": [], "R": []}
        ball = {"L": [], "R": []}
        for f in range(n + 1):
            bpy.context.scene.frame_set(f)
            bpy.context.view_layer.update()
            P.append(eval_verts(mesh_ob, idx))
            for s in "LR":
                ank[s].append(np.array(arm.pose.bones[f"foot.{s}"].head))
                ball[s].append(np.array(arm.pose.bones[f"toe.{s}"].head))
        P = np.array(P)                                   # frames x verts x 3
        gait = cd.extra.get("speed")
        comp = np.zeros(3)
        if gait:
            d = cd.extra["dir"]
            comp = -gait * np.array([d[0], -d[1], 0.0])   # ground velocity in root frame (blender axes), m/s
        contact = P[:, :, 2] < 0.008                      # frames x verts
        worst = 0.0
        worst_info = None
        nruns = 0
        for k in range(P.shape[1]):
            f = 0
            while f <= n:
                if not contact[f, k]:
                    f += 1
                    continue
                g = f
                while g + 1 <= n and contact[g + 1, k]:
                    g += 1
                if g - f + 1 >= 3:
                    nruns += 1
                    seg = P[f:g + 1, k, :2]
                    t = np.arange(g - f + 1)[:, None] / 30.0
                    drift = np.linalg.norm(seg - seg[0] - comp[None, :2] * t, axis=1).max()
                    if drift > worst:
                        worst, worst_info = drift, (int(k), f, g)
                f = g + 1
        entry = {"sole_skate_cm": round(worst * 100, 2), "contact_runs": nruns}
        if worst_info:
            entry["worst_run"] = {"vertex": worst_info[0], "frames": [worst_info[1], worst_info[2]]}
        # bone variance on frames where the foot is meant to be planted (authored lift == 0), non-gait clips only
        if not gait:
            res = dsl.build_clip(rig, cd)
            var = {}
            for s, key in (("L", "fl"), ("R", "fr")):
                planted = [abs(st[key][2]) < 1e-5 for st in res["states"]]
                runs = []
                f = 0
                while f <= n:
                    if planted[f]:
                        g = f
                        while g + 1 <= n and planted[g + 1]:
                            g += 1
                        if g - f + 1 >= 3:
                            runs.append((f, g))
                        f = g + 1
                    else:
                        f += 1
                best_a, best_b = 0.0, 0.0
                for (f, g) in runs:
                    A = np.array(ank[s][f:g + 1])
                    B = np.array(ball[s][f:g + 1])
                    best_a = max(best_a, np.linalg.norm(A - A.mean(0), axis=1).max())
                    best_b = max(best_b, np.linalg.norm(B - B.mean(0), axis=1).max())
                var[s] = {"ankle_cm": round(best_a * 100, 2), "ball_cm": round(best_b * 100, 2), "runs": runs}
            entry["planted_bone_variance"] = var
        report[name] = entry
    arm.animation_data.action = None
    fp.reset_pose(arm)
    return report


def dump_expected(arm, clips, out_json):
    picks = [("idle", 0), ("fire_jab", "c"), ("earth_heavy", "c"), ("walk", 9), ("knockdown", "last"), ("air_gust", "c"), ("run", 5)]
    out = []
    for name, f in picks:
        n = clips[name]["frames"]
        if f == "c":
            f = int(round(clips[name]["contact"] * 30))
        elif f == "last":
            f = n
        set_action(arm, name, f)
        bones = {}
        for b in ("hips", "head", "hand.L", "hand.R", "foot.L", "foot.R", "toe.L", "toe.R", "forearm.L"):
            bones[b] = [round(x, 5) for x in arm.pose.bones[b].head]
        out.append({"clip": name, "frame": f, "time": f / 30.0, "bones_blender": bones})
    arm.animation_data.action = None
    fp.reset_pose(arm)
    with open(out_json, "w") as fh:
        json.dump(out, fh, indent=1)
    print("[validate] expected bone positions:", out_json)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--blend", default=os.path.join(REPO, "assets_src", "fighter.blend"))
    ap.add_argument("--clips-json", default=os.path.join(REPO, "game", "assets", "characters", "fighter_clips.json"))
    ap.add_argument("--out-dir", default=os.path.join(REPO, "assets_src"),
                    help="reports (json) and the small jpeg copies of the sheets")
    ap.add_argument("--png-dir", default=None, help="where the full-size PNG sheets go (default: --out-dir)")
    ap.add_argument("--skip-sheet", action="store_true")
    ap.add_argument("--skip-feet", action="store_true")
    a = ap.parse_args(sys.argv[1:])
    os.makedirs(a.out_dir, exist_ok=True)
    bpy.ops.wm.open_mainfile(filepath=a.blend)
    arm = bpy.data.objects["FighterArmature"]
    mesh_ob = bpy.data.objects["FighterMesh"]
    clips = json.load(open(a.clips_json))
    rig = fp.RigModel(arm)
    if arm.animation_data is None:
        arm.animation_data_create()

    # sanity: scale / rotation / bounds
    me = mesh_ob.data
    zs = [v.co.z for v in me.vertices]
    print(f"[validate] mesh z range {min(zs):.4f} .. {max(zs):.4f} m; object scale {tuple(mesh_ob.scale)}, rot {tuple(mesh_ob.rotation_euler)}; "
          f"armature scale {tuple(arm.scale)}, rot {tuple(arm.rotation_euler)}")
    print(f"[validate] tris: {sum(len(p.vertices) - 2 for p in me.polygons)}; materials: {[m.name for m in me.materials]}")

    dump_expected(arm, clips, os.path.join(a.out_dir, "fighter_expected_bones.json"))
    if not a.skip_feet:
        rep = foot_report(arm, mesh_ob, rig, clips)
        with open(os.path.join(a.out_dir, "fighter_foot_report.json"), "w") as fh:
            json.dump(rep, fh, indent=1)
        print("[validate] foot report (sole skate cm / planted bone variance cm):")
        for name, e in rep.items():
            pb = e.get("planted_bone_variance")
            extra = ""
            if pb:
                extra = "  bone var ankle L/R {:.2f}/{:.2f} cm, ball L/R {:.2f}/{:.2f} cm".format(
                    pb["L"]["ankle_cm"], pb["R"]["ankle_cm"], pb["L"]["ball_cm"], pb["R"]["ball_cm"])
            wr = e.get("worst_run")
            wtxt = f" worst frames {wr['frames'][0]}-{wr['frames'][1]}" if wr and e["sole_skate_cm"] > 0.5 else ""
            print(f"   {name:18s} skate {e['sole_skate_cm']:6.2f} cm  ({e['contact_runs']} runs){wtxt}{extra}")
    if not a.skip_sheet:
        png_dir = a.png_dir or a.out_dir
        os.makedirs(png_dir, exist_ok=True)
        sheet(arm, clips, os.path.join(png_dir, "fighter_contact_sheet.png"), os.path.join(a.out_dir, "fighter_contact_sheet.jpg"))
        deform_check(arm, rig, os.path.join(png_dir, "fighter_deform_check.png"), os.path.join(a.out_dir, "fighter_deform_check.jpg"))


if __name__ == "__main__":
    main()
