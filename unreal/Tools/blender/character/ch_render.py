"""Review renders (Cycles CPU): studio lighting, cameras, contact sheets with labels."""
import math
import os

import bpy
from mathutils import Vector


def setup_studio(samples=48, res=(480, 720), bg=(0.42, 0.44, 0.47), floor=True, exposure=0.0):
    scn = bpy.context.scene
    scn.render.engine = "CYCLES"
    scn.cycles.device = "CPU"
    scn.cycles.samples = samples
    scn.cycles.use_adaptive_sampling = True
    scn.cycles.max_bounces = 6
    scn.cycles.transparent_max_bounces = 16
    try:
        scn.cycles.use_denoising = True
        scn.cycles.denoiser = "OPENIMAGEDENOISE"
    except Exception:
        pass
    scn.render.resolution_x, scn.render.resolution_y = res
    scn.render.resolution_percentage = 100
    scn.render.film_transparent = False
    try:
        scn.view_settings.view_transform = "AgX"
        scn.view_settings.look = "AgX - Medium High Contrast"
    except Exception:
        scn.view_settings.view_transform = "Filmic"
    scn.view_settings.exposure = exposure
    world = bpy.data.worlds.get("ff_world") or bpy.data.worlds.new("ff_world")
    world.use_nodes = True
    nt = world.node_tree
    bgn = nt.nodes.get("Background")
    bgn.inputs[0].default_value = (*bg, 1.0)
    bgn.inputs[1].default_value = 0.55
    scn.world = world
    col = bpy.data.collections.get("ff_studio")
    if col is None:
        col = bpy.data.collections.new("ff_studio")
        scn.collection.children.link(col)

    def area(name, loc, energy, size, color=(1, 1, 1)):
        o = bpy.data.objects.get(name)
        if o is None:
            ld = bpy.data.lights.new(name, "AREA")
            o = bpy.data.objects.new(name, ld)
            col.objects.link(o)
        o.data.energy = energy
        o.data.size = size
        o.data.color = color
        o.location = loc
        d = Vector((0, 0, 1.0)) - Vector(loc)
        o.rotation_euler = d.to_track_quat("-Z", "Y").to_euler()
        return o

    area("ff_key", (-2.2, -3.0, 3.0), 650, 2.0, (1.0, 0.96, 0.9))
    area("ff_fill", (3.0, -2.2, 1.6), 220, 3.0, (0.85, 0.9, 1.0))
    area("ff_rim", (0.5, 3.5, 2.8), 520, 1.5, (1.0, 1.0, 1.0))
    if floor and bpy.data.objects.get("ff_floor") is None:
        me = bpy.data.meshes.new("ff_floor")
        s = 30.0
        me.from_pydata([(-s, -s, 0), (s, -s, 0), (s, s, 0), (-s, s, 0)], [], [(0, 1, 2, 3)])
        fl = bpy.data.objects.new("ff_floor", me)
        col.objects.link(fl)
        m = bpy.data.materials.new("ff_floor_mat")
        m.use_nodes = True
        p = m.node_tree.nodes.get("Principled BSDF")
        p.inputs["Base Color"].default_value = (*[c * 0.9 for c in bg], 1)
        p.inputs["Roughness"].default_value = 0.8
        fl.data.materials.append(m)
    return scn


def camera(name="ff_cam", lens=50.0, ortho=None):
    cam = bpy.data.objects.get(name)
    if cam is None:
        cam = bpy.data.objects.new(name, bpy.data.cameras.new(name))
        bpy.context.scene.collection.objects.link(cam)
    if ortho:
        cam.data.type = "ORTHO"
        cam.data.ortho_scale = ortho
    else:
        cam.data.type = "PERSP"
        cam.data.lens = lens
    cam.data.clip_start = 0.01
    bpy.context.scene.camera = cam
    return cam


def look_at(cam, target, yaw_deg, dist, height):
    """Camera on a circle around target. yaw 0 = in front of the character (character faces -Y)."""
    t = Vector(target)
    a = math.radians(yaw_deg)
    loc = t + Vector((math.sin(a) * dist, -math.cos(a) * dist, 0.0))
    loc.z = height
    cam.location = loc
    cam.rotation_euler = (t - loc).to_track_quat("-Z", "Y").to_euler()


def look_dir(cam, target, direction, dist):
    """Camera at target + direction * dist, looking at target (world up stays up)."""
    t = Vector(target)
    dv = Vector(direction).normalized()
    loc = t + dv * dist
    cam.location = loc
    cam.rotation_euler = (t - loc).to_track_quat("-Z", "Y").to_euler()


def render(path):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    bpy.context.scene.render.filepath = path
    bpy.ops.render.render(write_still=True)
    return path


def contact_sheet(paths, labels, out, cols, label_h=22, bg=(30, 30, 34)):
    from PIL import Image, ImageDraw
    ims = [Image.open(p).convert("RGB") for p in paths]
    w, h = ims[0].size
    rows = (len(ims) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * w, rows * (h + label_h)), bg)
    dr = ImageDraw.Draw(sheet)
    for i, (im, lab) in enumerate(zip(ims, labels)):
        x, y = (i % cols) * w, (i // cols) * (h + label_h)
        sheet.paste(im, (x, y + label_h))
        dr.text((x + 6, y + 4), lab, fill=(230, 230, 230))
    os.makedirs(os.path.dirname(out), exist_ok=True)
    sheet.save(out, quality=90)
    return out
