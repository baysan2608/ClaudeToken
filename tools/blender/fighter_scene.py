"""Scene construction: armature, skinned mesh object, materials."""
import bmesh
import bpy
from mathutils import Vector

from fighter_skeleton import ALL_BONES, bone_table
from fighter_mesh import MAT_NAMES, MAT_COLORS, DOUBLE_SIDED
from fighter_character import build_character

ARM_NAME = "FighterArmature"
MESH_NAME = "FighterMesh"


def clear_scene():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    sc = bpy.context.scene
    sc.render.fps = 30
    sc.render.fps_base = 1.0
    sc.unit_settings.system = "METRIC"
    sc.unit_settings.scale_length = 1.0


def make_armature():
    table = bone_table()
    arm = bpy.data.armatures.new(ARM_NAME)
    ob = bpy.data.objects.new(ARM_NAME, arm)
    bpy.context.scene.collection.objects.link(ob)
    bpy.context.view_layer.objects.active = ob
    ob.select_set(True)
    bpy.ops.object.mode_set(mode="EDIT")
    ebs = {}
    for name in ALL_BONES:
        d = table[name]
        eb = arm.edit_bones.new(name)
        eb.head = d["head"]
        eb.tail = d["tail"]
        eb.align_roll(d["roll"])
        ebs[name] = eb
    for name in ALL_BONES:
        d = table[name]
        if d["parent"]:
            ebs[name].parent = ebs[d["parent"]]
            ebs[name].use_connect = bool(d["connect"])
    bpy.ops.object.mode_set(mode="OBJECT")
    for b in arm.bones:
        b.use_deform = True
        b.inherit_scale = "FULL"
    arm.display_type = "OCTAHEDRAL"
    for pb in ob.pose.bones:
        pb.rotation_mode = "QUATERNION"
    return ob


def make_materials():
    mats = []
    for name in MAT_NAMES:
        m = bpy.data.materials.new(name)
        m.use_nodes = True
        bsdf = m.node_tree.nodes.get("Principled BSDF")
        bsdf.inputs["Base Color"].default_value = MAT_COLORS[name]
        bsdf.inputs["Roughness"].default_value = 0.85
        bsdf.inputs["Metallic"].default_value = 0.0
        m.diffuse_color = MAT_COLORS[name]
        m.use_backface_culling = name not in DOUBLE_SIDED
        mats.append(m)
    return mats


def make_mesh_object(arm_ob, mb):
    mesh = bpy.data.meshes.new(MESH_NAME)
    mesh.from_pydata([tuple(v) for v in mb.verts], [], [list(f) for f in mb.faces])
    mesh.update()
    for m in make_materials():
        mesh.materials.append(m)
    mesh.polygons.foreach_set("material_index", mb.fmat)
    # per-corner UVs (packed atlas, one image per material) and the vertex-colour masks used by the texture bake
    uvl = mesh.uv_layers.new(name="UVMap")
    k = 0
    for poly, uvs in zip(mesh.polygons, mb.fuv):
        for li, (u, v) in zip(poly.loop_indices, uvs):
            uvl.data[li].uv = (u, v)
    col = mesh.color_attributes.new(name="Col", type="FLOAT_COLOR", domain="POINT")
    for vi, c in enumerate(mb.cols):
        col.data[vi].color = (c[0], c[1], c[2], 1.0)
    # smooth shading with hard edges where the dihedral angle is large
    bm = bmesh.new()
    bm.from_mesh(mesh)
    import math
    for f in bm.faces:
        f.smooth = True
    for e in bm.edges:
        if len(e.link_faces) == 2:
            a = e.link_faces[0].normal.angle(e.link_faces[1].normal, 0.0)
            if a > math.radians(62):
                e.smooth = False
    bm.to_mesh(mesh)
    bm.free()
    ob = bpy.data.objects.new(MESH_NAME, mesh)
    bpy.context.scene.collection.objects.link(ob)
    # vertex groups (one per bone, spec order)
    groups = {}
    for name in ALL_BONES:
        groups[name] = ob.vertex_groups.new(name=name)
    for vi, w in enumerate(mb.weights):
        for bn, val in w.items():
            groups[bn].add([vi], val, "REPLACE")
    ob.parent = arm_ob
    mod = ob.modifiers.new("Armature", "ARMATURE")
    mod.object = arm_ob
    return ob


def build_scene():
    clear_scene()
    arm = make_armature()
    mb = build_character()
    mesh_ob = make_mesh_object(arm, mb)
    return arm, mesh_ob, mb
