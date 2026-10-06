"""Blender look-dev mock-up of the courtyard from the gameplay camera (Cycles CPU).  NOT part of the game: it only lets us LOOK at
the generated meshes + textures + layout before the owner builds the level in Unreal.

    /home/user/tools/bpyenv/bin/python Tools/world/mockup_render.py [--view default|high|wall|south|pool|east|overview|all] [--out DIR]
        [--samples 48] [--res 960x540] [--no-scenery]

The scene is assembled from the same builders as the FBX export (export_env.py) at the positions of level_layout.json.
"""
from __future__ import annotations

import argparse
import math
import os
import sys

import bpy
import numpy as np
from mathutils import Vector

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bpy_io as IO               # noqa: E402
import env_spec as ES            # noqa: E402
import export_env as EX          # noqa: E402

VIEWS = {
    # name: (camera position sim (x, y, z), look-at sim, vertical fov deg)
    "default": ((0.0, 3.1, 12.8), (0.0, 1.5, 0.0), 62.0),              # behind the player (spawn z = +7) looking at the rival
    "high": ((0.0, 5.0, 12.5), (0.0, 1.2, -1.0), 62.0),
    "wall": ((8.0, 1.9, -6.0), (-2.0, 2.3, -16.0), 62.0),                # north wall detail, banners and lanterns
    "south": ((0.0, 3.1, -12.8), (0.0, 1.5, 0.0), 62.0),                 # from the rival's side
    "pool": ((3.0, 3.4, 12.0), (10.0, 0.0, -1.0), 62.0),
    "east": ((-6.0, 3.0, 3.0), (16.0, 2.0, -2.0), 62.0),
    "overview": ((0.0, 38.0, 46.0), (0.0, 0.0, -2.0), 45.0),
    "gate": ((0.0, 4.5, 12.0), (10.0, 5.5, -50.0), 50.0),
    "ledge": ((-6.0, 3.6, -2.0), (-14.0, 1.2, -13.0), 62.0),
}


def sim_vec(p):
    b = IO.sim_to_blender(np.array(p, float))
    return Vector((float(b[0]), float(b[1]), float(b[2])))


def build_scene(scenery=True, quality=None):
    sc = IO.reset_scene()
    mats = {}

    def lookup(slot):
        if slot not in mats:
            mats[slot] = IO.textured_material(slot)
        return mats[slot]

    objs = EX.build_all_objects(lookup, scenery=scenery)
    return sc, objs


def setup_world(sc, sun_strength=3.2):
    w = bpy.data.worlds.new("W")
    sc.world = w
    w.use_nodes = True
    nt = w.node_tree
    nt.nodes.clear()
    out = nt.nodes.new("ShaderNodeOutputWorld")
    bg = nt.nodes.new("ShaderNodeBackground")
    tc = nt.nodes.new("ShaderNodeTexCoord")
    sepn = nt.nodes.new("ShaderNodeSeparateXYZ")
    mp = nt.nodes.new("ShaderNodeMapRange")
    mp.inputs["From Min"].default_value = -0.05
    mp.inputs["From Max"].default_value = 0.9
    mp.clamp = True
    ramp = nt.nodes.new("ShaderNodeValToRGB")
    nt.links.new(tc.outputs["Generated"], sepn.inputs["Vector"])      # in a world shader: the view direction
    nt.links.new(sepn.outputs["Z"], mp.inputs["Value"])
    nt.links.new(mp.outputs["Result"], ramp.inputs["Fac"])
    cr = ramp.color_ramp
    cr.elements[0].position = 0.0
    cr.elements[0].color = (0.9 ** 2.2, 0.72 ** 2.2, 0.56 ** 2.2, 1)
    e = cr.elements.new(0.35)
    e.color = (0.32, 0.36, 0.5, 1)
    cr.elements[2].position = 1.0
    cr.elements[2].color = (0.05, 0.12, 0.35, 1)
    nt.links.new(ramp.outputs["Color"], bg.inputs["Color"])
    bg.inputs["Strength"].default_value = 1.1
    nt.links.new(bg.outputs["Background"], out.inputs["Surface"])
    # sun
    sun = bpy.data.lights.new("Sun", "SUN")
    sun.energy = sun_strength
    sun.color = ES.SUN_COLOR
    sun.angle = math.radians(2.5)
    so = bpy.data.objects.new("Sun", sun)
    sc.collection.objects.link(so)
    d = sim_vec(ES.sun_dir_sim())            # direction the light travels
    so.rotation_euler = d.to_track_quat("-Z", "Y").to_euler()
    return so


def add_stand_ins(sc):
    me = bpy.data.meshes.new("cap")
    mat = bpy.data.materials.new("stand")
    mat.diffuse_color = (0.3, 0.3, 0.35, 1)
    for i, (x, z) in enumerate(((0.0, 7.0), (0.0, -7.0))):
        bpy.ops.mesh.primitive_cylinder_add(radius=0.28, depth=1.7, location=tuple(sim_vec((x, 0.85, z))))
        o = bpy.context.active_object
        o.name = f"Fighter{i}"
        o.data.materials.append(mat)


def add_camera(sc, view, res):
    pos, look, fov = VIEWS[view]
    cd = bpy.data.cameras.new("Cam")
    cd.sensor_fit = "VERTICAL"
    cd.angle = math.radians(fov)
    cd.clip_end = 5000
    co = bpy.data.objects.new("Cam", cd)
    sc.collection.objects.link(co)
    co.location = sim_vec(pos)
    d = sim_vec(look) - co.location
    co.rotation_euler = d.to_track_quat("-Z", "Y").to_euler()
    sc.camera = co


def render(view, out_dir, samples, res, scenery):
    sc, objs = build_scene(scenery)
    setup_world(sc)
    add_stand_ins(sc)
    add_camera(sc, view, res)
    sc.render.engine = "CYCLES"
    sc.cycles.device = "CPU"
    sc.cycles.samples = samples
    sc.cycles.use_denoising = True
    sc.cycles.max_bounces = 4
    sc.render.resolution_x, sc.render.resolution_y = res
    sc.render.resolution_percentage = 100
    sc.view_settings.view_transform = "AgX" if "AgX" in [i.identifier for i in bpy.types.ColorManagedViewSettings.bl_rna.properties["view_transform"].enum_items] else "Filmic"
    os.makedirs(out_dir, exist_ok=True)
    path = os.path.join(out_dir, f"view_{view}.png")
    sc.render.filepath = path
    bpy.ops.render.render(write_still=True)
    print("rendered", path)
    return path


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--view", default="default")
    ap.add_argument("--out", default="/tmp/mock")
    ap.add_argument("--samples", type=int, default=48)
    ap.add_argument("--res", default="960x540")
    ap.add_argument("--no-scenery", action="store_true")
    a = ap.parse_args()
    res = tuple(int(v) for v in a.res.split("x"))
    views = list(VIEWS) if a.view == "all" else a.view.split(",")
    for v in views:
        render(v, a.out, a.samples, res, not a.no_scenery)
