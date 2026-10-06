"""Review renders (Cycles CPU) with preview materials that mirror the Unreal material logic (neutral BaseColor x
palette tint where BC.alpha = 1, DirectX normal maps flipped back for Blender, ORM roughness, mild AO)."""
import math
import os

import bpy
from mathutils import Vector

import ch_design as D
import ch_pose
import ch_render

SLOTS = ["skin", "hair", "eyes", "cloth_main", "cloth_accent", "wraps", "sash", "shoes"]


def _img(path, colorspace):
    name = os.path.basename(path)
    im = bpy.data.images.get(name)
    if im is None or im.filepath != path:
        im = bpy.data.images.load(path, check_existing=True)
    im.colorspace_settings.name = colorspace
    return im


def make_material(name, slot, tex_dir, palette):
    mat = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    mat.use_nodes = True
    nt = mat.node_tree
    nt.nodes.clear()
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    bsdf = nt.nodes.new("ShaderNodeBsdfPrincipled")
    nt.links.new(bsdf.outputs[0], out.inputs[0])
    uv = nt.nodes.new("ShaderNodeUVMap")
    uv.uv_map = "UVMap"
    bc = nt.nodes.new("ShaderNodeTexImage")
    bc.image = _img(os.path.join(tex_dir, f"T_Fighter_{slot}_BC.png"), "sRGB")
    bc.image.alpha_mode = "CHANNEL_PACKED"         # alpha is a data channel (tint / SSS mask), not coverage
    nm = nt.nodes.new("ShaderNodeTexImage")
    nm.image = _img(os.path.join(tex_dir, f"T_Fighter_{slot}_N.png"), "Non-Color")
    orm = nt.nodes.new("ShaderNodeTexImage")
    orm.image = _img(os.path.join(tex_dir, f"T_Fighter_{slot}_ORM.png"), "Non-Color")
    for t in (bc, nm, orm):
        nt.links.new(uv.outputs[0], t.inputs[0])
        t.interpolation = "Linear"
    sep = nt.nodes.new("ShaderNodeSeparateColor")
    nt.links.new(orm.outputs[0], sep.inputs[0])
    nt.links.new(sep.outputs[1], bsdf.inputs["Roughness"])
    bsdf.inputs["Metallic"].default_value = 0.0
    # base colour: tint where alpha = 1
    col = bc.outputs[0]
    tint_name = D.SLOT_TINT.get(slot)
    if tint_name:
        tint = nt.nodes.new("ShaderNodeRGB")
        tint.outputs[0].default_value = tuple(palette[tint_name][:3]) + (1.0,)
        mul = nt.nodes.new("ShaderNodeMix")
        mul.data_type = "RGBA"
        mul.blend_type = "MULTIPLY"
        mul.inputs[0].default_value = 1.0
        nt.links.new(col, mul.inputs[6])
        nt.links.new(tint.outputs[0], mul.inputs[7])
        gain = nt.nodes.new("ShaderNodeMix")
        gain.data_type = "RGBA"
        gain.blend_type = "MULTIPLY"
        gain.inputs[0].default_value = 1.0
        gain.inputs[7].default_value = (D.TINT_GAIN, D.TINT_GAIN, D.TINT_GAIN, 1.0)
        nt.links.new(mul.outputs[2], gain.inputs[6])
        sel = nt.nodes.new("ShaderNodeMix")
        sel.data_type = "RGBA"
        nt.links.new(bc.outputs[1], sel.inputs[0])
        nt.links.new(col, sel.inputs[6])
        nt.links.new(gain.outputs[2], sel.inputs[7])
        col = sel.outputs[2]
    # mild AO on the diffuse (Cycles already shades large-scale occlusion)
    aomix = nt.nodes.new("ShaderNodeMix")
    aomix.data_type = "RGBA"
    aomix.blend_type = "MULTIPLY"
    aomix.inputs[0].default_value = 0.5
    nt.links.new(col, aomix.inputs[6])
    nt.links.new(sep.outputs[0], aomix.inputs[7])
    nt.links.new(aomix.outputs[2], bsdf.inputs["Base Color"])
    # normal: DirectX -> OpenGL (flip green)
    sepn = nt.nodes.new("ShaderNodeSeparateColor")
    nt.links.new(nm.outputs[0], sepn.inputs[0])
    inv = nt.nodes.new("ShaderNodeMath")
    inv.operation = "SUBTRACT"
    inv.inputs[0].default_value = 1.0
    nt.links.new(sepn.outputs[1], inv.inputs[1])
    comb = nt.nodes.new("ShaderNodeCombineColor")
    nt.links.new(sepn.outputs[0], comb.inputs[0])
    nt.links.new(inv.outputs[0], comb.inputs[1])
    nt.links.new(sepn.outputs[2], comb.inputs[2])
    nmap = nt.nodes.new("ShaderNodeNormalMap")
    nmap.uv_map = "UVMap"
    nt.links.new(comb.outputs[0], nmap.inputs["Color"])
    nt.links.new(nmap.outputs[0], bsdf.inputs["Normal"])
    if slot == "skin":
        bsdf.subsurface_method = "BURLEY"          # random walk needs closed meshes (the scalp is cut away)
        bsdf.inputs["Subsurface Weight"].default_value = 0.35
        bsdf.inputs["Subsurface Radius"].default_value = (1.0, 0.35, 0.18)
        bsdf.inputs["Subsurface Scale"].default_value = 0.006
        bsdf.inputs["Specular IOR Level"].default_value = 0.45
    elif slot == "eyes":
        bsdf.inputs["Coat Weight"].default_value = 1.0
        bsdf.inputs["Coat Roughness"].default_value = 0.03
    elif slot == "hair":
        bsdf.inputs["Specular IOR Level"].default_value = 0.32
    else:
        bsdf.inputs["Sheen Weight"].default_value = 0.35
        bsdf.inputs["Sheen Roughness"].default_value = 0.45
        bsdf.inputs["Specular IOR Level"].default_value = 0.35
    return mat


def assign_preview_materials(objs, tex_dir, palette_name="player", suffix=""):
    pal = D.PALETTES[palette_name]
    mats = {s: make_material(f"PV_{s}{suffix}", s, tex_dir, pal) for s in SLOTS}
    for o in objs:
        for i, slot in enumerate(o.material_slots):
            base = slot.name.split(".")[0].replace("PV_", "")
            base = base[: -len(suffix)] if suffix and base.endswith(suffix) else base
            if base in mats:
                o.material_slots[i].material = mats[base]
    return mats


def _studio(quick, res):
    scn = ch_render.setup_studio(samples=16 if quick else 64, res=res, bg=(0.36, 0.37, 0.40))
    for ob in bpy.data.objects:
        if ob.name == "ff_key":
            ob.data.energy = 420
        elif ob.name == "ff_fill":
            ob.data.energy = 150
        elif ob.name == "ff_rim":
            ob.data.energy = 380
    return scn


def _hide(objs, hide=True):
    for o in objs:
        o.hide_render = hide


def render_all(arm, lods, out_dir, quick=False):
    """Writes previews/*.jpg contact sheets into out_dir/previews."""
    pv = os.path.join(out_dir, "previews")
    tmp = os.path.join(pv, "_frames")
    os.makedirs(tmp, exist_ok=True)
    lod0 = lods[0]
    others = [o for k, o in lods.items() if k != 0]
    _hide(others)
    assign_preview_materials([lod0], out_dir, "player")
    arm.hide_render = True
    s = 0.5 if quick else 1.0
    _studio(quick, (int(560 * s), int(900 * s)))
    cam = ch_render.camera(lens=60)
    out = {}
    lib = {p.name: p for p in ch_pose.library()}
    lib["rest"].apply(arm)
    # turnaround
    paths, labels = [], []
    for yaw, lab in ((0, "front"), (35, "3/4"), (90, "side"), (180, "back")):
        ch_render.look_at(cam, (0, 0, 0.92), yaw, 5.0, 1.05)
        paths.append(ch_render.render(os.path.join(tmp, f"turn_{yaw}.png")))
        labels.append(lab)
    out["turnaround"] = ch_render.contact_sheet(paths, labels, os.path.join(pv, "turnaround.jpg"), 4)
    # face + hands
    scn = bpy.context.scene
    scn.render.resolution_x = scn.render.resolution_y = int(640 * s)
    cam.data.lens = 85
    paths, labels = [], []
    for yaw, lab in ((0, "face front"), (28, "face 3/4"), (75, "face side")):
        ch_render.look_at(cam, (0, -0.06, 1.655), yaw, 0.85, 1.67)
        paths.append(ch_render.render(os.path.join(tmp, f"face_{yaw}.png")))
        labels.append(lab)
    out["face"] = ch_render.contact_sheet(paths, labels, os.path.join(pv, "face_closeup.jpg"), 3)
    paths, labels = [], []
    for pname, lab in (("rest", "hand relaxed"), ("horse stance", "fist"), ("arms overhead", "sword fingers / tiger claw")):
        lib[pname].apply(arm)
        bpy.context.view_layer.update()
        hb = arm.matrix_world @ arm.pose.bones["middle_metacarpal_l"].head
        hr = arm.matrix_world @ arm.pose.bones["middle_metacarpal_r"].head
        for hh, tag in ((hb, "L"), (hr, "R")):
            if pname == "rest" and tag == "R":
                continue
            yaw = 60 if tag == "L" else -60
            ch_render.look_at(cam, hh, yaw, 0.45, hh.z + 0.08)
            paths.append(ch_render.render(os.path.join(tmp, f"hand_{len(paths)}.png")))
            labels.append(f"{lab} {tag}")
    out["hands"] = ch_render.contact_sheet(paths, labels, os.path.join(pv, "hands_closeup.jpg"), len(paths))
    # deformation sheet
    cam.data.lens = 50
    scn.render.resolution_x, scn.render.resolution_y = int(400 * s), int(560 * s)
    paths, labels = [], []
    for pose in ch_pose.library():
        pose.apply(arm)
        bpy.context.view_layer.update()
        for yaw in (25, 115):
            ch_render.look_at(cam, (0, 0, 0.92), yaw, 4.8, 1.15)
            paths.append(ch_render.render(os.path.join(tmp, f"def_{len(paths)}.png")))
            labels.append(f"{pose.name} ({yaw})")
    out["deformation"] = ch_render.contact_sheet(paths, labels, os.path.join(pv, "deformation_sheet.jpg"), 8)
    lib["rest"].apply(arm)
    # palettes side by side (three copies)
    copies = []
    for k, (pal, dx) in enumerate((("player", -1.0), ("rival", 0.0), ("dummy", 1.0))):
        if pal == "player":
            o = lod0
            o.location.x = dx
        else:
            o = lod0.copy()
            o.data = lod0.data.copy()
            bpy.context.scene.collection.objects.link(o)
            o.location.x = dx
            copies.append(o)
            assign_preview_materials([o], out_dir, pal, suffix="_" + pal)
    scn.render.resolution_x, scn.render.resolution_y = int(1200 * s), int(820 * s)
    cam.data.lens = 40
    ch_render.look_at(cam, (0, 0, 0.92), 18, 6.4, 1.15)
    out["palettes"] = ch_render.render(os.path.join(pv, "palettes.png"))
    for o in copies:
        bpy.data.objects.remove(o)
    lod0.location.x = 0.0
    # size check: 1.80 m ruler with 10 cm ticks
    ruler = _ruler()
    lod0.location.x = -0.25
    scn.render.resolution_x, scn.render.resolution_y = int(700 * s), int(900 * s)
    cam.data.type = "ORTHO"
    cam.data.ortho_scale = 2.05
    ch_render.look_at(cam, (0.05, 0, 0.95), 0, 6.0, 0.95)
    out["size"] = ch_render.render(os.path.join(pv, "size_check.png"))
    cam.data.type = "PERSP"
    lod0.location.x = 0.0
    for o in ruler:
        bpy.data.objects.remove(o)
    _hide(others, False)
    return out


def _ruler():
    objs = []
    mat = bpy.data.materials.new("ruler")
    mat.use_nodes = True
    mat.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (0.9, 0.85, 0.2, 1)
    dark = bpy.data.materials.new("ruler_dark")
    dark.use_nodes = True
    dark.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (0.05, 0.05, 0.05, 1)
    bpy.ops.mesh.primitive_cube_add(size=1, location=(0.35, 0.0, 0.9))
    bar = bpy.context.active_object
    bar.scale = (0.03, 0.03, 1.8)
    bar.data.materials.append(mat)
    objs.append(bar)
    for i in range(19):
        z = i * 0.1
        bpy.ops.mesh.primitive_cube_add(size=1, location=(0.35 - 0.03, -0.02, z))
        t = bpy.context.active_object
        t.scale = (0.06 if i % 5 == 0 else 0.035, 0.01, 0.006)
        t.data.materials.append(dark)
        objs.append(t)
    bpy.ops.mesh.primitive_cube_add(size=1, location=(0.25, -0.02, 1.80))
    top = bpy.context.active_object
    top.scale = (0.30, 0.01, 0.004)
    top.data.materials.append(dark)
    objs.append(top)
    return objs
