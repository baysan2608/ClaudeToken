#!/usr/bin/env python
"""Quick Cycles previews of the built fighter (geometry iteration).
   bpy-python tools/blender/preview_cli.py OUTDIR [--views front,side,back,face,hands,q] [--clip NAME --frame F]
Builds the scene from scratch (no export) and renders each view as OUTDIR/<view>.png."""
import argparse, math, os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import bpy
import fighter_scene as fsc
import fighter_pose as fp
import fighter_preview as pv

ap = argparse.ArgumentParser()
ap.add_argument("out")
ap.add_argument("--views", default="front,side,back,face")
ap.add_argument("--hide", default="")
ap.add_argument("--samples", type=int, default=24)
ap.add_argument("--size", type=int, default=640)
a = ap.parse_args(sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else sys.argv[1:])
os.makedirs(a.out, exist_ok=True)
arm, mesh_ob, mb = fsc.build_scene()
tris = sum(len(f) - 2 for f in mb.faces)
print(f"mesh: {len(mb.verts)} verts, {tris} tris, {len(arm.data.bones)} bones")
VIEWS = {  # name: (azimuth, center_x, center_y, center_z, ortho)
    "front": (0, 0, 0, 0.88, 1.9), "side": (90, 0, 0, 0.88, 1.9), "back": (180, 0, 0, 0.88, 1.9), "q": (38, 0, 0, 0.88, 1.9),
    "face": (20, 0, 0, 1.62, 0.34), "face_side": (90, 0, 0, 1.62, 0.34), "faceq": (50, 0, 0, 1.62, 0.34), "head_back": (150, 0, 0, 1.64, 0.4),
    "hands": (0, 0, 0, 1.0, 0.9), "hand_l": (-30, 0.335, 0.1, 0.80, 0.30), "hand_r": (30, -0.335, 0.1, 0.80, 0.30),
    "torso": (20, 0, 0, 1.2, 0.9), "legs": (30, 0, 0, 0.45, 0.95), "feet": (50, 0, 0, 0.1, 0.5), "waist_back": (160, 0, 0, 1.0, 0.7),
}
for m in a.hide.split(","):
    if m:
        idx = [i for i, ms in enumerate(mesh_ob.data.materials) if ms.name == m][0]
        import bmesh
        bm = bmesh.new(); bm.from_mesh(mesh_ob.data)
        bm.faces.ensure_lookup_table()
        bmesh.ops.delete(bm, geom=[f for f in bm.faces if f.material_index == idx], context="FACES")
        bm.to_mesh(mesh_ob.data); bm.free()
pv.setup_preview_scene(tile_w=a.size, tile_h=a.size, ortho=1.9, samples=a.samples)
pv.emulate_backface_culling()
cam, lbl = pv._STATE["cam"], pv._STATE["lbl"]
lbl.data.body = ""
for v in a.views.split(","):
    az, cx, cy, cz, orth = VIEWS[v]
    cam.data.ortho_scale = orth
    lbl.hide_render = True
    ang = math.radians(az)
    from mathutils import Euler
    cam.location = (cx - 8 * math.sin(ang), cy - 8 * math.cos(ang), cz)
    cam.rotation_euler = Euler((math.radians(90), 0, -ang))
    bpy.context.scene.render.filepath = os.path.join(a.out, v + ".png")
    bpy.ops.render.render(write_still=True)
print("done")
