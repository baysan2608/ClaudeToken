"""Fourfold UE port - FROZEN FBX export settings for the shared skeleton (architect-owned, read-only).

Both the `character` stream (skeletal mesh) and the `animation` stream (one FBX per clip) export through these two
functions so Unreal sees identical skeletons. Conventions (see unreal/docs/ARCHITECTURE.md "Character & animation"):
  * scene unit scale 1.0 (metres), the armature OBJECT is named "root" at the origin with identity transform;
  * apply_scale_options='FBX_SCALE_ALL' (cm baked into the file, no 100x root scale in UE), apply_unit_scale=True;
  * Blender default axes (forward -Z, up Y) and bone axes (primary Y, secondary X) - what Epic's Send to Unreal uses;
  * add_leaf_bones=False; all bones exported (helpers ik_* / interaction / center_of_mass are part of the skeleton);
  * animations: 60 fps, one action per file, every frame baked, no key simplification, start/end keyed.
The character faces Blender -Y; after import Unreal shows it facing +Y like UE5 Manny (actors rotate the mesh -90 yaw).
"""
import os

FPS = 60


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


def export_skeletal_mesh_fbx(path, armature_obj, mesh_objs):
    """Skeletal mesh (+ skeleton) FBX: the armature and its skinned meshes, no animation."""
    import bpy
    assert armature_obj.name == "root", "the armature object must be named 'root'"
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    _select_only([armature_obj] + list(mesh_objs))
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
    if armature_obj.animation_data is None:
        armature_obj.animation_data_create()
    armature_obj.animation_data.action = action
    scn.frame_start = int(frame_start)
    scn.frame_end = int(frame_end)
    _select_only([armature_obj])
    kw = _common()
    kw.update(object_types={"ARMATURE"}, bake_anim=True, bake_anim_use_all_bones=True,
              bake_anim_use_nla_strips=False, bake_anim_use_all_actions=False,
              bake_anim_force_startend_keying=True, bake_anim_step=1.0, bake_anim_simplify_factor=0.0)
    bpy.ops.export_scene.fbx(filepath=path, **kw)
    return path
