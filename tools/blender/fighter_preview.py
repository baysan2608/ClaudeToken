"""Preview rendering helpers (Cycles CPU, tiny sample counts) used for the visual validation sheet.

The Workbench engine needs libEGL which is unavailable in the headless build environment, so previews use
Cycles with a flat lighting rig.  Nothing in here is part of the exported asset.
"""
import math

import bpy
import numpy as np
from mathutils import Vector, Euler

_PREVIEW_OBJECTS = []
_STATE = {}


def setup_preview_scene(tile_w=360, tile_h=480, ortho=2.0, samples=10):
    sc = bpy.context.scene
    if _STATE.get("scene") is sc and _STATE.get("cam") is not None:
        co, to = _STATE["cam"], _STATE["lbl"]
        sc.cycles.samples = samples
        sc.render.resolution_x = tile_w
        sc.render.resolution_y = tile_h
        co.data.ortho_scale = ortho
        to.location = (-0.5 * ortho * tile_w / tile_h + 0.03, 0.5 * ortho - 0.11, -3.0)
        return co, to
    sc.render.engine = "CYCLES"
    sc.cycles.device = "CPU"
    sc.cycles.samples = samples
    sc.cycles.use_denoising = False
    sc.cycles.max_bounces = 2
    sc.render.resolution_x = tile_w
    sc.render.resolution_y = tile_h
    sc.render.resolution_percentage = 100
    sc.render.image_settings.file_format = "PNG"
    sc.view_settings.view_transform = "Standard"
    sc.render.film_transparent = False
    # world
    w = bpy.data.worlds.new("PreviewWorld")
    w.use_nodes = True
    bg = w.node_tree.nodes["Background"]
    bg.inputs["Color"].default_value = (0.80, 0.82, 0.86, 1.0)
    bg.inputs["Strength"].default_value = 0.9
    sc.world = w
    # lights
    for loc, energy in (((-1.5, -3.0, 3.5), 3.0), ((3.0, 1.5, 2.0), 1.2)):
        ld = bpy.data.lights.new("sun", "SUN")
        ld.energy = energy
        lo = bpy.data.objects.new("sun", ld)
        sc.collection.objects.link(lo)
        d = -Vector(loc)
        lo.rotation_euler = d.to_track_quat("-Z", "Y").to_euler()
        _PREVIEW_OBJECTS.append(lo)
    # floor slab
    bpy.ops.mesh.primitive_cube_add(size=1.0, location=(0, 0, -0.01))
    fl = bpy.context.active_object
    fl.scale = (3.0, 3.0, 0.02)
    fm = bpy.data.materials.new("floor")
    fm.use_nodes = True
    fm.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (0.35, 0.37, 0.40, 1)
    fl.data.materials.append(fm)
    fl.name = "PreviewFloor"
    _PREVIEW_OBJECTS.append(fl)
    # camera
    cam = bpy.data.cameras.new("PreviewCam")
    cam.type = "ORTHO"
    cam.ortho_scale = ortho
    co = bpy.data.objects.new("PreviewCam", cam)
    sc.collection.objects.link(co)
    sc.camera = co
    _PREVIEW_OBJECTS.append(co)
    # label text
    tc = bpy.data.curves.new("lbl", "FONT")
    tc.body = ""
    tc.size = 0.085
    to = bpy.data.objects.new("lbl", tc)
    sc.collection.objects.link(to)
    tm = bpy.data.materials.new("lblmat")
    tm.use_nodes = True
    nt = tm.node_tree
    nt.nodes.clear()
    em = nt.nodes.new("ShaderNodeEmission")
    em.inputs["Color"].default_value = (0.02, 0.02, 0.05, 1)
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    nt.links.new(em.outputs[0], out.inputs[0])
    tc.materials.append(tm)
    to.parent = co
    to.location = (-0.5 * ortho * tile_w / tile_h + 0.03, 0.5 * ortho - 0.11, -3.0)
    _PREVIEW_OBJECTS.append(to)
    _STATE.update(scene=sc, cam=co, lbl=to)
    return co, to


def _render_to_array(path):
    sc = bpy.context.scene
    sc.render.filepath = path
    bpy.ops.render.render(write_still=True)
    img = bpy.data.images.load(path, check_existing=False)
    w, h = img.size
    px = np.array(img.pixels[:], dtype=np.float32).reshape(h, w, 4)
    bpy.data.images.remove(img)
    return px[::-1] if False else px     # bottom-up, as Blender stores it


def render_view(cam_ob, label_ob, view, label, tmp_path, center_z=0.88, center_x=0.0, center_y=0.0):
    """view: 'front' | 'side' | 'back' | 'q' (three-quarter) or a number = camera azimuth in degrees
    (0 = in front of the character, 90 = looking at its right side).  Returns an (h, w, 4) float array."""
    az = {"front": 0.0, "side": 90.0, "back": 180.0, "q": 38.0}.get(view, view)
    a = math.radians(az)
    cam_ob.location = (center_x - 8.0 * math.sin(a), center_y - 8.0 * math.cos(a), center_z)
    cam_ob.rotation_euler = Euler((math.radians(90), 0, -a))
    label_ob.data.body = label
    return _render_to_array(tmp_path)


def assemble_sheet(tiles, ncols, out_path, jpeg_path=None):
    """tiles: list of (h, w, 4) float arrays (same size).  Writes a PNG via bpy."""
    h, w, _ = tiles[0].shape
    nrows = (len(tiles) + ncols - 1) // ncols
    sheet = np.ones((nrows * h, ncols * w, 4), dtype=np.float32)
    for k, t in enumerate(tiles):
        r, c = divmod(k, ncols)
        # Blender image rows are bottom-up: row 0 of sheet array = bottom. flip tile placement vertically
        y0 = (nrows - 1 - r) * h
        sheet[y0:y0 + h, c * w:(c + 1) * w] = t
        # thin border
        sheet[y0:y0 + h, c * w] = 0.5
        sheet[y0:y0 + h, (c + 1) * w - 1] = 0.5
        sheet[y0, c * w:(c + 1) * w] = 0.5
        sheet[y0 + h - 1, c * w:(c + 1) * w] = 0.5
    img = bpy.data.images.new("sheet", width=ncols * w, height=nrows * h, alpha=True)
    img.pixels.foreach_set(sheet.ravel())
    img.filepath_raw = out_path
    img.file_format = "PNG"
    img.save()
    if jpeg_path:
        img.filepath_raw = jpeg_path
        img.file_format = "JPEG"
        img.save()
    bpy.data.images.remove(img)


def render_pose_sheet(arm_ob, items, out_path, views=("front", "side"), ncols=None, tmp_dir=None,
                      center_z=0.88, tile=(300, 400), ortho=1.9, samples=8):
    """items: list of (label, ql, hips_loc).  One tile per (item, view).  Returns nothing; writes the PNG."""
    import fighter_pose as fp
    import os
    co, lo = setup_preview_scene(tile_w=tile[0], tile_h=tile[1], ortho=ortho, samples=samples)
    emulate_backface_culling()
    tmp = os.path.join(tmp_dir or os.path.dirname(out_path), "_tile.png")
    tiles = []
    for label, ql, hips_loc in items:
        fp.reset_pose(arm_ob)
        fp.apply_pose(arm_ob, ql, hips_loc)
        bpy.context.view_layer.update()
        for v in views:
            tiles.append(render_view(co, lo, v, f"{label} [{v}]", tmp, center_z=center_z))
    ncols = ncols or len(views) * (3 if len(views) == 2 else 1)
    assemble_sheet(tiles, ncols, out_path)
    try:
        os.remove(tmp)
    except OSError:
        pass
    fp.reset_pose(arm_ob)


def emulate_backface_culling():
    """Cycles ignores material backface culling (the game/glTF uses it): make single-sided materials transparent
    on their back faces so previews match the engine."""
    for m in bpy.data.materials:
        if not m.use_nodes or not m.use_backface_culling or m.name in ("floor", "lblmat"):
            continue
        nt = m.node_tree
        if nt.nodes.get("_cull"):
            continue
        out = nt.nodes.get("Material Output")
        bsdf = nt.nodes.get("Principled BSDF")
        if out is None or bsdf is None:
            continue
        geo = nt.nodes.new("ShaderNodeNewGeometry")
        tr = nt.nodes.new("ShaderNodeBsdfTransparent")
        mix = nt.nodes.new("ShaderNodeMixShader")
        mix.name = "_cull"
        nt.links.new(geo.outputs["Backfacing"], mix.inputs[0])
        nt.links.new(bsdf.outputs[0], mix.inputs[1])
        nt.links.new(tr.outputs[0], mix.inputs[2])
        nt.links.new(mix.outputs[0], out.inputs["Surface"])
