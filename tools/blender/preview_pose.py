#!/usr/bin/env python
"""Cycles previews of a clip frame.
   bpy-python tools/blender/preview_pose.py OUTDIR clip:frame[,clip:frame...] [--views front,side,q] [--center Z] [--ortho S]
frame may be 'c' (contact frame).  Builds the scene (set FIGHTER_NO_TEXTURES=1 to skip the texture bake)."""
import argparse, math, os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import bpy
import fighter_clips as C
import fighter_dsl as dsl
import fighter_pose as fp
import fighter_preview as pv
import fighter_scene as fsc
from mathutils import Euler

ap = argparse.ArgumentParser()
ap.add_argument("out")
ap.add_argument("items")
ap.add_argument("--views", default="front,q")
ap.add_argument("--center", type=float, default=0.9)
ap.add_argument("--cx", type=float, default=0.0)
ap.add_argument("--ortho", type=float, default=1.9)
ap.add_argument("--size", type=int, default=480)
ap.add_argument("--samples", type=int, default=16)
a = ap.parse_args(sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else sys.argv[1:])
os.makedirs(a.out, exist_ok=True)
arm, mesh_ob, mb = fsc.build_scene()
rig = fp.RigModel(arm)
pv.setup_preview_scene(tile_w=a.size, tile_h=a.size, ortho=a.ortho, samples=a.samples)
pv.emulate_backface_culling()
cam, lbl = pv._STATE["cam"], pv._STATE["lbl"]
lbl.hide_render = True
AZ = {"front": 0, "side": 90, "back": 180, "q": 38, "q2": -38}
for item in a.items.split(","):
    name, fr = item.split(":")
    cd = C.CLIPS[name]
    res = dsl.build_clip(rig, cd)
    f = int(round(cd.contact * 30)) if fr == "c" else int(fr)
    f = min(f, res["n"])
    ql, hips = res["frames"][f]
    fp.reset_pose(arm)
    fp.apply_pose(arm, ql, hips)
    bpy.context.view_layer.update()
    for v in a.views.split(","):
        ang = math.radians(AZ.get(v, float(v) if v.lstrip("-").isdigit() else 0))
        cam.data.ortho_scale = a.ortho
        cam.location = (a.cx - 8 * math.sin(ang), -8 * math.cos(ang), a.center)
        cam.rotation_euler = Euler((math.radians(90), 0, -ang))
        bpy.context.scene.render.filepath = os.path.join(a.out, f"{name}_{f}_{v}.png")
        bpy.ops.render.render(write_still=True)
print("done")
