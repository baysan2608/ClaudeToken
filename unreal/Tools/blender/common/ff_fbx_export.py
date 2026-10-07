"""Fourfold UE port - FROZEN FBX export settings for the shared skeleton (architect-owned, read-only).

Both the `character` stream (skeletal mesh) and the `animation` stream (one FBX per clip) export through these two
functions so Unreal sees identical skeletons. Conventions (see unreal/docs/ARCHITECTURE.md "Character & animation"):
  * the build scenes work in metres (unit scale 1.0), the armature OBJECT is named "root" at the origin with identity
    transform. The FILE is written in centimetres: both exporters export a temporary copy whose mesh / bone data and
    bone location keys are scaled x100 under a scene unit scale of 0.01, so the FBX unit factor is 1 (cm).
    (Writing metres - UnitScaleFactor 100 - made Unreal's importers put the conversion on the top node, which is the
    root bone: a x100 root scale under which every bone offset was in metres. That collapsed IK-retargeted clips and
    fed the native anim runtime a skeleton 100x too small.)
  * apply_scale_options='FBX_SCALE_ALL', apply_unit_scale=True;
  * Blender default axes (forward -Z, up Y) and bone axes (primary Y, secondary X) - what Epic's Send to Unreal uses;
  * add_leaf_bones=False; all bones exported (helpers ik_* / interaction / center_of_mass are part of the skeleton);
  * animations: 60 fps, one action per file, every frame baked, no key simplification, start/end keyed.
The character faces Blender -Y; after import Unreal shows it facing +Y like UE5 Manny (actors rotate the mesh -90 yaw).
"""
import os

FPS = 60
CM = 100.0                    # metres -> centimetres


def _select_only(objs):
    import bpy
    bpy.ops.object.select_all(action="DESELECT")
    for o in objs:
        o.select_set(True)
    bpy.context.view_layer.objects.active = objs[0]


def _common():
    return dict(
        use_selection=True,
        apply_unit_scale=True,
        apply_scale_options="FBX_SCALE_ALL",
        global_scale=1.0,
        axis_forward="-Z",
        axis_up="Y",
        bake_space_transform=False,
        primary_bone_axis="Y",
        secondary_bone_axis="X",
        add_leaf_bones=False,
        use_armature_deform_only=False,
        armature_nodetype="NULL",
        use_custom_props=False,
        path_mode="AUTO",
        embed_textures=False,
    )


def _fcurves(action):
    """All F-curves of an action (Blender 4.4+ layered actions, with the legacy list as fallback)."""
    out = []
    for layer in getattr(action, "layers", []):
        for strip in layer.strips:
            for cb in getattr(strip, "channelbags", []):
                out.extend(cb.fcurves)
    if not out and hasattr(action, "fcurves"):
        out = list(action.fcurves)
    return out


class _CentimetreCopy:
    """Temporary copy of the armature (+ meshes, + action) in centimetres, named "root"; removed on exit."""

    def __init__(self, armature_obj, mesh_objs=(), action=None):
        self.src_arm, self.src_meshes, self.src_action = armature_obj, list(mesh_objs), action

    def __enter__(self):
        import bpy
        from mathutils import Matrix
        scn = bpy.context.scene
        self.unit = (scn.unit_settings.system, scn.unit_settings.scale_length)
        coll = scn.collection
        S = Matrix.Scale(CM, 4)
        self.src_name = self.src_arm.name
        self.src_arm.name = "root__m"
        arm = self.src_arm.copy()
        arm.data = self.src_arm.data.copy()
        arm.name = "root"
        coll.objects.link(arm)
        arm.data.transform(S)                       # bone heads / tails (rest pose) to cm
        arm.animation_data_clear()
        self.objs, self.datas = [arm], [arm.data]
        meshes = []
        for o in self.src_meshes:
            m = o.copy()
            m.data = o.data.copy()
            coll.objects.link(m)
            m.data.transform(S)
            m.parent = arm
            m.matrix_parent_inverse = o.matrix_parent_inverse.copy()
            m.location = o.location * CM
            for mod in m.modifiers:
                if mod.type == "ARMATURE" and mod.object == self.src_arm:
                    mod.object = arm
            meshes.append(m)
            self.objs.append(m)
            self.datas.append(m.data)
        self.action = None
        if self.src_action is not None:
            act = self.src_action.copy()
            for fc in _fcurves(act):
                if fc.data_path.endswith("location"):
                    for kp in fc.keyframe_points:
                        kp.co.y *= CM
                        kp.handle_left.y *= CM
                        kp.handle_right.y *= CM
            arm.animation_data_create()
            arm.animation_data.action = act
            if hasattr(arm.animation_data, "action_slot") and arm.animation_data.action_slot is None and len(act.slots):
                arm.animation_data.action_slot = act.slots[0]
            self.action = act
        scn.unit_settings.system = "METRIC"
        scn.unit_settings.scale_length = 1.0 / CM   # 1 Blender unit = 1 cm -> FBX unit factor 1
        self.arm, self.meshes = arm, meshes
        return self

    def __exit__(self, *exc):
        import bpy
        scn = bpy.context.scene
        scn.unit_settings.system, scn.unit_settings.scale_length = self.unit
        for o in self.objs:
            bpy.data.objects.remove(o, do_unlink=True)
        for d in self.datas:
            if d.users == 0:
                (bpy.data.armatures if d.__class__.__name__ == "Armature" else bpy.data.meshes).remove(d)
        if self.action is not None and self.action.users == 0:
            bpy.data.actions.remove(self.action)
        self.src_arm.name = self.src_name
        return False


def export_skeletal_mesh_fbx(path, armature_obj, mesh_objs):
    """Skeletal mesh (+ skeleton) FBX: the armature and its skinned meshes, no animation."""
    import bpy
    assert armature_obj.name == "root", "the armature object must be named 'root'"
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with _CentimetreCopy(armature_obj, mesh_objs) as cm:
        _select_only([cm.arm] + cm.meshes)
        kw = _common()
        kw.update(object_types={"ARMATURE", "MESH"}, use_mesh_modifiers=True, mesh_smooth_type="FACE",
                  use_tspace=True, use_triangles=False, bake_anim=False)
        bpy.ops.export_scene.fbx(filepath=path, **kw)
    return path


def export_animation_fbx(path, armature_obj, action, frame_start, frame_end):
    """One clip per FBX: armature only, the given action baked at FPS on every frame."""
    import bpy
    assert armature_obj.name == "root", "the armature object must be named 'root'"
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    scn = bpy.context.scene
    scn.render.fps = FPS
    scn.render.fps_base = 1.0
    scn.frame_start = int(frame_start)
    scn.frame_end = int(frame_end)
    with _CentimetreCopy(armature_obj, (), action) as cm:
        _select_only([cm.arm])
        kw = _common()
        kw.update(object_types={"ARMATURE"}, bake_anim=True, bake_anim_use_all_bones=True,
                  bake_anim_use_nla_strips=False, bake_anim_use_all_actions=False,
                  bake_anim_force_startend_keying=True, bake_anim_step=1.0, bake_anim_simplify_factor=0.0)
        bpy.ops.export_scene.fbx(filepath=path, **kw)
    return path
