"""Blender side of the environment pipeline: meshkit.Mesh -> bpy object (+ FBX export, + textured mock-up materials).

Run with /home/user/tools/bpyenv/bin/python (bpy as a module).  Coordinates: sim (x, y-up, z) -> Blender (x, -z, y), a proper rotation, so
the FBX import in Unreal (Blender default axes, FBX_SCALE_ALL, as the frozen character export) lands vertices at UE = 100 * (x, z, y).
"""
from __future__ import annotations

import os

import bpy
import numpy as np

import env_spec as ES

FBX_KW = dict(
    use_selection=True, apply_unit_scale=True, apply_scale_options="FBX_SCALE_ALL", global_scale=1.0, axis_forward="-Z", axis_up="Y",
    bake_space_transform=False, object_types={"MESH"}, use_mesh_modifiers=False, mesh_smooth_type="EDGE", use_custom_props=False,
    add_leaf_bones=False, path_mode="AUTO", embed_textures=False, bake_anim=False, colors_type="LINEAR",
)


def sim_to_blender(P):
    P = np.asarray(P, float)
    return np.stack([P[..., 0], -P[..., 2], P[..., 1]], -1)


def reset_scene():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    sc = bpy.context.scene
    sc.unit_settings.system = "METRIC"
    sc.unit_settings.scale_length = 1.0
    return sc


def to_object(mesh, pivot=(0.0, 0.0, 0.0), mat_lookup=None, collection=None, name=None, place=True):
    """Creates a bpy mesh object from a meshkit.Mesh whose geometry is in sim space; the object origin is `pivot` (sim): vertices are
    stored relative to it and (place=True) the object is moved there, so the scene looks like the level."""
    data = mesh.finalize()
    piv = np.asarray(pivot, float)
    verts = sim_to_blender(data["verts"] - piv)
    me = bpy.data.meshes.new(name or mesh.name)
    polys = [list(map(int, p)) for p in data["polys"]]
    me.from_pydata(verts.tolist(), [], polys)
    me.update()
    uv_flat = np.concatenate([np.asarray(u, float).reshape(-1, 2) for u in data["uv"]])
    col_flat = np.concatenate([np.asarray(c, float).reshape(-1, 4) for c in data["col"]])
    has_uv1 = any(u is not None for u in data["uv1"])
    me.uv_layers.new(name="UVMap")
    if has_uv1:
        me.uv_layers.new(name="UVLight")
    me.color_attributes.new(name="Col", type="BYTE_COLOR", domain="CORNER")
    # (re-fetch the layers after every creation: earlier references dangle when attributes are added)
    me.uv_layers["UVMap"].data.foreach_set("uv", uv_flat.reshape(-1).tolist())
    if has_uv1:
        u1 = np.concatenate([np.asarray(u if u is not None else np.zeros((len(p), 2)), float).reshape(-1, 2)
                             for u, p in zip(data["uv1"], polys)])
        me.uv_layers["UVLight"].data.foreach_set("uv", u1.reshape(-1).tolist())
    me.color_attributes["Col"].data.foreach_set("color", col_flat.reshape(-1).tolist())
    for i, (sname, _) in enumerate(mesh.slots):
        me.materials.append(mat_lookup(sname) if mat_lookup else dummy_material(sname))
    me.polygons.foreach_set("material_index", data["mat"].astype(np.int32))
    me.polygons.foreach_set("use_smooth", data["smooth"].astype(bool))
    me.update()
    ob = bpy.data.objects.new(name or mesh.name, me)
    (collection or bpy.context.scene.collection).objects.link(ob)
    if place:
        ob.location = tuple(float(v) for v in sim_to_blender(piv))
    return ob


_dummy = {}


def dummy_material(name):
    if name not in _dummy or _dummy[name].name not in bpy.data.materials:
        m = bpy.data.materials.new(name)
        _dummy[name] = m
    return _dummy[name]


def export_fbx(ob, path):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    bpy.ops.object.select_all(action="DESELECT")
    ob.select_set(True)
    bpy.context.view_layer.objects.active = ob
    bpy.ops.export_scene.fbx(filepath=path, **FBX_KW)
    return path


# ------------------------------------------------------------------------------------------------ mock-up materials
def _img(path, colorspace):
    if not os.path.exists(path):
        return None
    im = bpy.data.images.load(path, check_existing=True)
    im.colorspace_settings.name = colorspace
    return im


def textured_material(slot, opts=None):
    """Principled material from the same PNG sets the Unreal materials use (look-dev only; the real ones are built by Python in the editor)."""
    opts = opts or {}
    key = f"MU_{slot}"
    if key in bpy.data.materials:
        return bpy.data.materials[key]
    m = bpy.data.materials.new(key)
    m.use_nodes = True
    nt = m.node_tree
    nt.nodes.clear()
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    bsdf = nt.nodes.new("ShaderNodeBsdfPrincipled")
    nt.links.new(bsdf.outputs["BSDF"], out.inputs["Surface"])
    bsdf.inputs["Roughness"].default_value = 0.8
    tile_m = ES.TILE.get(slot, 1.0)
    sat = opts.get("sat", {}).get(slot, 0.8)
    if slot in ES.SLOT_TEXTURES:
        s = ES.SLOT_TEXTURES[slot]
        base = os.path.join(ES.TEX_DIR, f"T_Env_{s}_")
        uv = nt.nodes.new("ShaderNodeUVMap")
        uv.uv_map = "UVMap"
        bc = nt.nodes.new("ShaderNodeTexImage")
        bc.image = _img(base + "BC.png", "sRGB")
        nrm = nt.nodes.new("ShaderNodeTexImage")
        nrm.image = _img(base + "N.png", "Non-Color")
        orm = nt.nodes.new("ShaderNodeTexImage")
        orm.image = _img(base + "ORM.png", "Non-Color")
        for t in (bc, nrm, orm):
            nt.links.new(uv.outputs["UV"], t.inputs["Vector"])
            t.interpolation = "Cubic" if False else "Linear"
        hs = nt.nodes.new("ShaderNodeHueSaturation")
        hs.inputs["Saturation"].default_value = sat
        nt.links.new(bc.outputs["Color"], hs.inputs["Color"])
        tint = nt.nodes.new("ShaderNodeMix")
        tint.data_type = "RGBA"
        tint.blend_type = "MULTIPLY"
        tint.inputs["Factor"].default_value = 1.0
        tint.inputs[7].default_value = opts.get("tint", {}).get(slot, (1, 1, 1, 1))
        nt.links.new(hs.outputs["Color"], tint.inputs[6])
        nt.links.new(tint.outputs[2], bsdf.inputs["Base Color"])
        sep = nt.nodes.new("ShaderNodeSeparateColor")
        nt.links.new(orm.outputs["Color"], sep.inputs["Color"])
        nt.links.new(sep.outputs["Green"], bsdf.inputs["Roughness"])
        nt.links.new(sep.outputs["Blue"], bsdf.inputs["Metallic"])
        # DirectX normal (green down) -> Blender OpenGL: flip green
        sn = nt.nodes.new("ShaderNodeSeparateColor")
        nt.links.new(nrm.outputs["Color"], sn.inputs["Color"])
        inv = nt.nodes.new("ShaderNodeMath")
        inv.operation = "SUBTRACT"
        inv.inputs[0].default_value = 1.0
        nt.links.new(sn.outputs["Green"], inv.inputs[1])
        cn = nt.nodes.new("ShaderNodeCombineColor")
        nt.links.new(sn.outputs["Red"], cn.inputs["Red"])
        nt.links.new(inv.outputs["Value"], cn.inputs["Green"])
        nt.links.new(sn.outputs["Blue"], cn.inputs["Blue"])
        nm = nt.nodes.new("ShaderNodeNormalMap")
        nm.uv_map = "UVMap"
        nt.links.new(cn.outputs["Color"], nm.inputs["Color"])
        nt.links.new(nm.outputs["Normal"], bsdf.inputs["Normal"])
    elif slot == "Glow":
        bsdf.inputs["Base Color"].default_value = (0.3, 0.12, 0.04, 1)
        bsdf.inputs["Emission Color"].default_value = (1.0, 0.55, 0.2, 1)
        bsdf.inputs["Emission Strength"].default_value = opts.get("glow", 6.0)
    elif slot in ("Foliage", "Ridge"):
        im = _img(os.path.join(ES.TEX_DIR, "T_Env_Foliage_BC.png"), "sRGB") if slot == "Foliage" else None
        vc = nt.nodes.new("ShaderNodeVertexColor")
        vc.layer_name = "Col"
        if slot == "Ridge":
            em = nt.nodes.new("ShaderNodeEmission")
            nt.links.new(vc.outputs["Color"], em.inputs["Color"])
            em.inputs["Strength"].default_value = 1.0
            nt.links.new(em.outputs["Emission"], out.inputs["Surface"])
        else:
            uv = nt.nodes.new("ShaderNodeUVMap")
            uv.uv_map = "UVMap"
            t = nt.nodes.new("ShaderNodeTexImage")
            t.image = im
            nt.links.new(uv.outputs["UV"], t.inputs["Vector"])
            t.extension = "CLIP"
            mulc = nt.nodes.new("ShaderNodeMix")
            mulc.data_type = "RGBA"
            mulc.blend_type = "MULTIPLY"
            mulc.inputs["Factor"].default_value = 1.0
            nt.links.new(t.outputs["Color"], mulc.inputs[6])
            mulc.inputs[7].default_value = (1.1, 1.0, 0.9, 1)
            nt.links.new(mulc.outputs[2], bsdf.inputs["Base Color"])
            nt.links.new(mulc.outputs[2], bsdf.inputs["Emission Color"])           # fake translucency / sky fill
            bsdf.inputs["Emission Strength"].default_value = 0.35
            mixn = nt.nodes.new("ShaderNodeMixShader")
            tr = nt.nodes.new("ShaderNodeBsdfTransparent")
            nt.links.new(t.outputs["Alpha"], mixn.inputs["Fac"])
            nt.links.new(tr.outputs["BSDF"], mixn.inputs[1])
            nt.links.new(bsdf.outputs["BSDF"], mixn.inputs[2])
            nt.links.new(mixn.outputs["Shader"], out.inputs["Surface"])
            bsdf.inputs["Roughness"].default_value = 0.8
    elif slot == "Banner":
        im = _img(os.path.join(ES.TEX_DIR, "T_Env_Banners_BC.png"), "sRGB")
        uv = nt.nodes.new("ShaderNodeUVMap")
        uv.uv_map = "UVMap"
        t = nt.nodes.new("ShaderNodeTexImage")
        t.image = im
        t.extension = "CLIP"
        nt.links.new(uv.outputs["UV"], t.inputs["Vector"])
        nt.links.new(t.outputs["Color"], bsdf.inputs["Base Color"])
        # alpha cut-out + two-sided
        mixn = nt.nodes.new("ShaderNodeMixShader")
        tr = nt.nodes.new("ShaderNodeBsdfTransparent")
        nt.links.new(t.outputs["Alpha"], mixn.inputs["Fac"])
        nt.links.new(tr.outputs["BSDF"], mixn.inputs[1])
        nt.links.new(bsdf.outputs["BSDF"], mixn.inputs[2])
        nt.links.new(mixn.outputs["Shader"], out.inputs["Surface"])
        bsdf.inputs["Roughness"].default_value = 0.9
    else:
        colors = dict(Shrub=(0.1, 0.2, 0.06, 1), Foliage=(0.1, 0.22, 0.07, 1), Ridge=(0.5, 0.55, 0.6, 1), Water=(0.05, 0.25, 0.3, 1),
                      Bark=(0.2, 0.14, 0.09, 1), Sky=(0.4, 0.6, 0.9, 1))
        bsdf.inputs["Base Color"].default_value = colors.get(slot, (0.5, 0.5, 0.5, 1))
    if slot == "Bronze":
        bsdf.inputs["Base Color"].default_value = (0.5, 0.33, 0.14, 1)
        bsdf.inputs["Metallic"].default_value = 0.9
        bsdf.inputs["Roughness"].default_value = 0.4
    if slot == "Iron":
        bsdf.inputs["Base Color"].default_value = (0.06, 0.06, 0.07, 1)
        bsdf.inputs["Metallic"].default_value = 0.75
        bsdf.inputs["Roughness"].default_value = 0.5
    m.use_backface_culling = False
    return m
