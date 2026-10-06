"""Small bpy helpers shared by the character build (mesh creation, selection, modifiers, scene reset)."""
import bpy
import numpy as np


def reset_scene():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scn = bpy.context.scene
    scn.unit_settings.system = "METRIC"
    scn.unit_settings.scale_length = 1.0
    return scn


def link(obj, collection=None):
    (collection or bpy.context.scene.collection).objects.link(obj)
    return obj


def mesh_from_arrays(name, verts, faces, uvs=None, collection=None, smooth=True):
    """verts (n,3); faces list of index lists; uvs: optional per-loop list matching faces (list of (u,v) lists)."""
    me = bpy.data.meshes.new(name)
    me.from_pydata([tuple(map(float, p)) for p in verts], [], [list(map(int, f)) for f in faces])
    me.validate(clean_customdata=False)
    if uvs is not None:
        uvl = me.uv_layers.new(name="UVMap")
        flat = [uv for f in uvs for uv in f]
        if len(flat) == len(uvl.data):
            uvl.data.foreach_set("uv", np.array(flat, dtype=np.float32).ravel())
    if smooth:
        me.polygons.foreach_set("use_smooth", np.ones(len(me.polygons), dtype=bool))
    me.update()
    obj = bpy.data.objects.new(name, me)
    link(obj, collection)
    return obj


def get_verts(obj):
    me = obj.data
    a = np.empty(len(me.vertices) * 3, dtype=np.float64)
    me.vertices.foreach_get("co", a)
    return a.reshape(-1, 3)


def set_verts(obj, v):
    me = obj.data
    me.vertices.foreach_set("co", np.asarray(v, dtype=np.float64).ravel())
    me.update()


def faces_of(obj):
    return [list(p.vertices) for p in obj.data.polygons]


def select_only(objs, active=None):
    bpy.ops.object.select_all(action="DESELECT")
    for o in objs:
        o.select_set(True)
    bpy.context.view_layer.objects.active = active or objs[0]


def apply_modifiers(obj):
    select_only([obj])
    for m in list(obj.modifiers):
        bpy.ops.object.modifier_apply(modifier=m.name)


def tri_count(obj):
    return sum(len(p.vertices) - 2 for p in obj.data.polygons)


def evaluated_copy_verts(obj):
    """World-space vertex positions of the evaluated (posed / modified) object."""
    dg = bpy.context.evaluated_depsgraph_get()
    ev = obj.evaluated_get(dg)
    me = ev.to_mesh()
    a = np.empty(len(me.vertices) * 3)
    me.vertices.foreach_get("co", a)
    a = a.reshape(-1, 3)
    m = np.array(obj.matrix_world)
    a = a @ m[:3, :3].T + m[:3, 3]
    ev.to_mesh_clear()
    return a
