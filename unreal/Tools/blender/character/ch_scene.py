"""Blender scene assembly: one skinned mesh object per LOD on the frozen armature."""
import os
import sys

import bpy
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "common"))
import ff_rig_spec as spec  # noqa: E402

import ch_bl  # noqa: E402
import ch_part as P  # noqa: E402
import ch_weights as cw  # noqa: E402
from ch_assemble import SLOTS  # noqa: E402


def ensure_materials():
    for s in SLOTS:
        if bpy.data.materials.get(s) is None:
            bpy.data.materials.new(s)


def make_skinned(parts, name, arm):
    ensure_materials()
    merged = P.merge(parts, name)
    obj = P.to_object(merged, SLOTS)
    cw.to_vertex_groups(obj, merged.W)
    mod = obj.modifiers.new("Armature", "ARMATURE")
    mod.object = arm
    mod.use_deform_preserve_volume = False      # Unreal uses linear blend skinning: preview what it will show
    obj.parent = arm
    return obj, merged


def build_rig():
    arm = spec.build_armature()
    probs = spec.validate_armature(arm)
    if probs:
        raise RuntimeError("rig spec validation failed: " + "; ".join(probs[:5]))
    return arm


def part_object(part, arm=None, collection=None):
    """Mesh object for one part: material slots (all 8, fixed order), UVMap (packed) + PatternUV (metres),
    vertex groups for every deform bone, parented / armature-modified to `arm`."""
    ensure_materials()
    obj = P.to_object(part, SLOTS, collection)
    me = obj.data
    if getattr(part, "puv", None) is not None:
        lay = me.uv_layers.new(name="PatternUV")
        flat = [uv for f in part.puv for uv in f]
        lay.data.foreach_set("uv", np.array(flat, dtype=np.float32).ravel())
        me.uv_layers.active_index = 0
        me.uv_layers[0].active_render = True
    cw.to_vertex_groups(obj, part.W)
    if arm is not None:
        mod = obj.modifiers.new("Armature", "ARMATURE")
        mod.object = arm
        obj.parent = arm
    return obj


def decimate(obj, ratio, weights=None, symmetric=True):
    """Collapse decimation (vertex weights 0 = keep, 1 = free to collapse). Applies the modifier."""
    if ratio >= 0.999:
        return obj
    if weights is not None:
        vg = obj.vertex_groups.new(name="_decimate")
        w = np.asarray(weights, float)
        for val in np.unique(np.round(w, 3)):
            ids = np.nonzero(np.round(w, 3) == val)[0].tolist()
            vg.add(ids, float(val), "REPLACE")
    m = obj.modifiers.new("_dec", "DECIMATE")
    m.decimate_type = "COLLAPSE"
    m.ratio = ratio
    m.use_collapse_triangulate = False
    m.use_symmetry = symmetric
    m.symmetry_axis = "X"
    if weights is not None:
        m.vertex_group = "_decimate"
        m.vertex_group_factor = 1.0
    # keep the armature modifier last-applied state: move the decimate to the top and apply it alone
    ch_bl.select_only([obj])
    bpy.ops.object.modifier_move_to_index(modifier="_dec", index=0)
    bpy.ops.object.modifier_apply(modifier="_dec")
    if weights is not None:
        obj.vertex_groups.remove(obj.vertex_groups["_decimate"])
    return obj


def join(objs, name):
    ch_bl.select_only(objs, objs[0])
    bpy.ops.object.join()
    o = bpy.context.active_object
    o.name = name
    o.data.name = name
    return o


def duplicate(obj, name):
    o = obj.copy()
    o.data = obj.data.copy()
    o.name = name
    o.data.name = name
    for c in obj.users_collection:
        c.objects.link(o)
    return o


def clean_weights(obj, k=4):
    """Re-limit / normalise the vertex groups after decimation (collapse interpolates weights)."""
    W = cw.from_vertex_groups(obj)
    W = cw.limit(cw.normalize(W), k)
    for g in list(obj.vertex_groups):
        if g.name in cw.BI:
            obj.vertex_groups.remove(g)
    cw.to_vertex_groups(obj, W)
    return W


def triangulate_ngons(obj):
    """Split polygons with more than 4 corners (FBX tangents need tris / quads; Unreal triangulates anyway)."""
    import bmesh
    me = obj.data
    bm = bmesh.new()
    bm.from_mesh(me)
    ng = [f for f in bm.faces if len(f.verts) > 4]
    if ng:
        bmesh.ops.triangulate(bm, faces=ng, quad_method="BEAUTY", ngon_method="BEAUTY")
    bm.to_mesh(me)
    bm.free()
    me.update()
    return len(ng)
