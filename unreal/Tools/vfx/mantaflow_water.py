"""Fourfold VFX - real fluid-sim water flipbooks (Blender Mantaflow FLIP + Cycles), replacing the numpy water_splash.

  /Applications/Blender.app/Contents/MacOS/Blender -b --factory-startup --python unreal/Tools/vfx/mantaflow_water.py -- \
      [--res 112] [--tile 256] [--only splash]

Writes unreal/SourceArt/VFX/Flipbooks/T_FX_FB_water_splash.png (house format: R light, G thickness, B 0, A alpha;
8 x 8 frames) and updates flipbooks.json. Shots:
  splash  - a 0.7 m ball of water slams down and bursts: crown sheet + spray droplets (Mantaflow secondary particles)
The water is shaded like real water (dielectric, fresnel-weighted opacity: grazing edges opaque and bright, faces facing
the camera thin), lit by a sun + sky, rendered on a transparent film from a 20 degree elevated side view.
Note: the bake needs cache_type ALL in background mode (REPLAY bakes only the current frame).
"""
import json
import math
import os
import shutil
import sys
import tempfile

import bpy
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
FB = os.path.normpath(os.path.join(HERE, "..", "..", "SourceArt", "VFX", "Flipbooks"))
FRAMES = 64


def args():
    a = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    o = {"res": 112, "tile": 256, "only": ""}
    for i in range(0, len(a) - 1, 2):
        o[a[i].lstrip("-")] = a[i + 1]
    o["res"] = int(o["res"])
    o["tile"] = int(o["tile"])
    return o


def water_material():
    m = bpy.data.materials.new("Water")
    m.use_nodes = True
    nt = m.node_tree
    for n in list(nt.nodes):
        nt.nodes.remove(n)
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    bsdf = nt.nodes.new("ShaderNodeBsdfPrincipled")
    bsdf.inputs["Base Color"].default_value = (0.55, 0.72, 0.8, 1.0)
    bsdf.inputs["Roughness"].default_value = 0.04
    bsdf.inputs["IOR"].default_value = 1.33
    for k in ("Specular IOR Level", "Specular"):
        if k in bsdf.inputs:
            bsdf.inputs[k].default_value = 0.9
    transp = nt.nodes.new("ShaderNodeBsdfTransparent")
    fres = nt.nodes.new("ShaderNodeLayerWeight")
    fres.inputs["Blend"].default_value = 0.35
    ramp = nt.nodes.new("ShaderNodeMapRange")
    ramp.inputs["From Min"].default_value = 0.0
    ramp.inputs["From Max"].default_value = 1.0
    ramp.inputs["To Min"].default_value = 0.2      # facing: thin, see-through water
    ramp.inputs["To Max"].default_value = 0.9      # grazing: opaque, bright rim
    nt.links.new(fres.outputs["Facing"], ramp.inputs["Value"])
    mix = nt.nodes.new("ShaderNodeMixShader")
    nt.links.new(ramp.outputs["Result"], mix.inputs["Fac"])
    nt.links.new(transp.outputs["BSDF"], mix.inputs[1])
    nt.links.new(bsdf.outputs["BSDF"], mix.inputs[2])
    nt.links.new(mix.outputs["Shader"], out.inputs["Surface"])
    return m


def scene_base(res):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    sc = bpy.context.scene
    sc.frame_start, sc.frame_end = 1, FRAMES
    sc.render.engine = "CYCLES"
    try:
        prefs = bpy.context.preferences.addons["cycles"].preferences
        prefs.compute_device_type = "METAL"
        prefs.get_devices()
        for d in prefs.devices:
            d.use = True
        sc.cycles.device = "GPU"
    except Exception:  # noqa: BLE001
        pass
    sc.cycles.samples = 48
    sc.cycles.use_denoising = True
    sc.render.film_transparent = True
    sc.render.resolution_x = sc.render.resolution_y = res
    sc.render.image_settings.file_format = "PNG"
    sc.render.image_settings.color_mode = "RGBA"
    sc.view_settings.view_transform = "Standard"
    world = bpy.data.worlds.new("Sky")
    sc.world = world
    world.use_nodes = True
    bg = world.node_tree.nodes["Background"]
    bg.inputs["Color"].default_value = (0.55, 0.7, 0.95, 1.0)
    bg.inputs["Strength"].default_value = 0.9
    sun = bpy.data.objects.new("Sun", bpy.data.lights.new("Sun", "SUN"))
    sun.data.energy = 4.0
    sun.rotation_euler = (math.radians(50), 0, math.radians(35))
    sc.collection.objects.link(sun)
    return sc


def camera(sc, target_z, width):
    cam = bpy.data.objects.new("Cam", bpy.data.cameras.new("Cam"))
    cam.data.type = "ORTHO"
    cam.data.ortho_scale = width
    el = math.radians(3)
    dist = 20.0
    cam.location = (0.0, -dist * math.cos(el), target_z + dist * math.sin(el))
    cam.rotation_euler = (math.radians(90) - el, 0.0, 0.0)
    sc.collection.objects.link(cam)
    sc.camera = cam


def splash(o, out_dir):
    sc = scene_base(o["tile"])
    cache = tempfile.mkdtemp(prefix="ff_mf_")
    # 6 x 6 x 3 m domain, its floor is the ground the water hits (z 0)
    bpy.ops.mesh.primitive_cube_add(size=1, location=(0, 0, 1.5))
    dom = bpy.context.object
    dom.scale = (6.0, 6.0, 3.0)
    m = dom.modifiers.new("Fluid", "FLUID")
    m.fluid_type = "DOMAIN"
    d = m.domain_settings
    d.domain_type = "LIQUID"
    d.resolution_max = o["res"]
    d.cache_directory = cache
    d.cache_type = "ALL"
    d.cache_frame_start, d.cache_frame_end = 1, FRAMES
    d.use_mesh = True
    d.mesh_scale = 2
    if hasattr(d, "particle_type"):
        try:
            d.particle_type = {"SPRAY"}
        except Exception:  # noqa: BLE001
            pass
    if hasattr(d, "sndparticle_potential_max_wavecrest"):
        d.sndparticle_potential_max_wavecrest = 60.0   # fewer spray droplets (only the violent crests)
    d.time_scale = 0.35                            # 64 frames cover ~0.9 s: the impact, crown and spray only
    # a shallow pool (0.3 m) the ball drops into: crown sheet + rebound jet (filmed from the side, the surface is a line)
    bpy.ops.mesh.primitive_cube_add(size=1, location=(0, 0, 0.15))
    pool = bpy.context.object
    pool.scale = (5.9, 5.9, 0.3)
    pf = pool.modifiers.new("Fluid", "FLUID")
    pf.fluid_type = "FLOW"
    pf.flow_settings.flow_type = "LIQUID"
    pf.flow_settings.flow_behavior = "GEOMETRY"
    pool.hide_render = True
    # the ball of water slams in
    bpy.ops.mesh.primitive_uv_sphere_add(radius=0.34, location=(0, 0, 0.95))
    ball = bpy.context.object
    f = ball.modifiers.new("Fluid", "FLUID")
    f.fluid_type = "FLOW"
    fs = f.flow_settings
    fs.flow_type = "LIQUID"
    fs.flow_behavior = "GEOMETRY"
    fs.use_initial_velocity = True
    fs.velocity_coord = (0.0, 0.0, -7.0)
    ball.hide_render = True
    bpy.context.view_layer.objects.active = dom
    bpy.ops.fluid.bake_all()
    dom.data.materials.append(water_material())
    # spray droplets: render the secondary particles as tiny spheres
    drop = None
    for ps in dom.particle_systems:
        if drop is None:
            bpy.ops.mesh.primitive_ico_sphere_add(radius=0.012, subdivisions=1, location=(0, 0, -50))
            drop = bpy.context.object
            drop.data.materials.append(dom.data.materials[0])
        st = ps.settings
        st.render_type = "OBJECT"
        st.instance_object = drop
        st.particle_size = 1.0
        st.display_size = 0.012
    camera(sc, 1.55, 3.2)
    frames = []
    tmp = tempfile.mkdtemp(prefix="ff_mfr_")
    for i in range(FRAMES):
        sc.frame_set(i + 1)
        sc.render.filepath = os.path.join(tmp, f"f_{i:03d}.png")
        bpy.ops.render.render(write_still=True)
        frames.append(sc.render.filepath)
    shutil.rmtree(cache, ignore_errors=True)
    return frames, tmp


def pack(frames, tile, dst):
    atlas = np.zeros((tile * 8, tile * 8, 4), dtype=np.float32)
    for i, p in enumerate(frames):
        img = bpy.data.images.load(p)
        px = np.array(img.pixels[:], dtype=np.float32).reshape(img.size[1], img.size[0], 4)[::-1]
        bpy.data.images.remove(img)
        if px.shape[0] != tile:
            continue
        rgb, a = px[..., :3], px[..., 3]
        lum = rgb @ np.array([0.2126, 0.7152, 0.0722], dtype=np.float32)
        light = np.where(a > 1e-3, lum / np.maximum(a, 1e-3), 0.0)   # unpremultiply (Cycles film is premultiplied)
        fade = min(1.0, (len(frames) - 1 - i) / (0.25 * len(frames)))   # the tail thins out instead of lingering
        a = a * fade
        tile_px = np.stack([np.clip(light, 0, 1), a, np.zeros_like(a), a], axis=-1)
        r, c = divmod(i, 8)
        atlas[r * tile:(r + 1) * tile, c * tile:(c + 1) * tile] = tile_px
    out = bpy.data.images.new("atlas", tile * 8, tile * 8, alpha=True)
    out.pixels = atlas[::-1].ravel().tolist()
    out.filepath_raw = dst
    out.file_format = "PNG"
    out.save()


def main():
    o = args()
    meta_path = os.path.join(FB, "flipbooks.json")
    meta = json.load(open(meta_path))
    if not o["only"] or "splash" in o["only"]:
        frames, tmp = splash(o, FB)
        dst = os.path.join(FB, "T_FX_FB_water_splash.png")
        pack(frames, o["tile"], dst)
        shutil.rmtree(tmp, ignore_errors=True)
        e = meta["flipbooks"]["water_splash"]
        e.update({"frames": FRAMES, "grid": 8, "tile": o["tile"], "seconds": round(FRAMES / 24.0 * 0.35, 3),
                  "source": f"Blender Mantaflow FLIP (res {o['res']}) + Cycles", "generator": "unreal/Tools/vfx/mantaflow_water.py"})
        print("WROTE", dst)
    json.dump(meta, open(meta_path, "w"), indent=2)


main()
