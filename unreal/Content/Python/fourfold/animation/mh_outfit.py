"""Fighter outfit pieces for the MetaHuman fighters (called by setup part `metahuman`).

Training pants + waist sash: SourceArt/Character/MetaHuman/SKM_FF_{Pants,Sash}.fbx (Tools/blender/character/mh_outfit.py,
made from the exported SKM_FF_Body) imported onto metahuman_base_skel under /Game/Fourfold/Characters/MetaHuman.
M_FF_Cloth: Poly Haven CC0 rough linen (Tools/world/fetch_polyhaven.py set "Cloth"), desaturated and multiplied by FF_Tint,
tiled by the scan size (the UVs are in metres). The pants use MI_FF_Pants (charcoal), the sash takes the role colour.
pieces() returns the metahuman.json parts, or [] when an input is missing (the cargo pants then stay).
"""
import json
import os

import unreal

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
MH_ROOT = "/Game/Fourfold/Characters/MetaHuman"
PANTS = MH_ROOT + "/SKM_FF_Pants"
SASH = MH_ROOT + "/SKM_FF_Sash"
PANTS_MI = MH_ROOT + "/Materials/MI_FF_Pants"
PANTS_TINT = (0.20, 0.19, 0.18)        # charcoal linen; the sash carries the role colour
TEX_DIR = MH_ROOT + "/Textures"
CLOTH = MH_ROOT + "/Materials/M_FF_Cloth"
BODY = MH_ROOT + "/SKM_FF_Body"


def _project():
    return unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())


def _load(path):
    return unreal.load_asset(path) if EAL.does_asset_exist(path) else None


def _import_texture(png, name, kind, force, rep):
    dst = f"{TEX_DIR}/{name}"
    if EAL.does_asset_exist(dst) and not force:
        return _load(dst)
    t = unreal.AssetImportTask()
    t.set_editor_property("filename", png)
    t.set_editor_property("destination_path", TEX_DIR)
    t.set_editor_property("destination_name", name)
    t.set_editor_property("replace_existing", True)
    t.set_editor_property("automated", True)
    t.set_editor_property("save", False)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([t])
    tex = _load(dst)
    if tex is None:
        rep["failed"].append({"item": dst, "error": f"texture import of {png} produced nothing"})
        return None
    if kind == "normal":
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
        tex.set_editor_property("srgb", False)
    elif kind == "masks":
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
        tex.set_editor_property("srgb", False)
    EAL.save_loaded_asset(tex)
    rep["created"].append(dst)
    return tex


def _cloth_material(force, rep):
    ph = os.path.join(_project(), "SourceArt", "Environment", "PolyHaven")
    pngs = {k: os.path.join(ph, f"T_Env_Cloth_{k}.png") for k in ("BC", "N", "ORM")}
    if not all(os.path.exists(p) for p in pngs.values()):
        rep["notes"].append("cloth textures missing (python3 Tools/world/fetch_polyhaven.py --only Cloth): pants use a flat colour")
    if EAL.does_asset_exist(CLOTH) and not force:
        rep["skipped"].append(CLOTH)
        return CLOTH
    size_m = 0.271
    try:
        size_m = json.load(open(os.path.join(ph, "polyhaven.json")))["sets"]["Cloth"]["size_m"]
    except (OSError, KeyError, ValueError):
        pass
    bc = _import_texture(pngs["BC"], "T_FF_Cloth_BC", "color", force, rep) if os.path.exists(pngs["BC"]) else None
    nm = _import_texture(pngs["N"], "T_FF_Cloth_N", "normal", force, rep) if os.path.exists(pngs["N"]) else None
    orm = _import_texture(pngs["ORM"], "T_FF_Cloth_ORM", "masks", force, rep) if os.path.exists(pngs["ORM"]) else None
    if EAL.does_asset_exist(CLOTH):
        EAL.delete_asset(CLOTH)
    EAL.make_directory(CLOTH.rsplit("/", 1)[0])
    mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset("M_FF_Cloth", CLOTH.rsplit("/", 1)[0], unreal.Material,
                                                                  unreal.MaterialFactoryNew())
    mat.set_editor_property("used_with_skeletal_mesh", True)
    P = unreal.MaterialProperty
    uv = MEL.create_material_expression(mat, unreal.MaterialExpressionTextureCoordinate, -1200, 0)
    scale = MEL.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -1200, 120)
    scale.set_editor_property("parameter_name", "UVScale")
    scale.set_editor_property("default_value", round(1.0 / max(size_m, 0.05), 4))
    uvm = MEL.create_material_expression(mat, unreal.MaterialExpressionMultiply, -1000, 40)
    MEL.connect_material_expressions(uv, "", uvm, "A")
    MEL.connect_material_expressions(scale, "", uvm, "B")
    tint = MEL.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -600, -260)
    tint.set_editor_property("parameter_name", "FF_Tint")
    tint.set_editor_property("default_value", unreal.LinearColor(1.0, 1.0, 1.0, 1.0))
    base = MEL.create_material_expression(mat, unreal.MaterialExpressionMultiply, -300, -200)
    if bc is not None:
        t = MEL.create_material_expression(mat, unreal.MaterialExpressionTextureSampleParameter2D, -800, -120)
        t.set_editor_property("parameter_name", "BaseColorTex")
        t.set_editor_property("texture", bc)
        MEL.connect_material_expressions(uvm, "", t, "UVs")
        des = MEL.create_material_expression(mat, unreal.MaterialExpressionDesaturation, -600, -120)
        MEL.connect_material_expressions(t, "RGB", des, "")
        lift = MEL.create_material_expression(mat, unreal.MaterialExpressionMultiply, -450, -120)
        k = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant, -600, -40)
        k.set_editor_property("r", 0.9)
        MEL.connect_material_expressions(des, "", lift, "A")
        MEL.connect_material_expressions(k, "", lift, "B")
        MEL.connect_material_expressions(lift, "", base, "A")
    else:
        g = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, -600, -120)
        g.set_editor_property("constant", unreal.LinearColor(0.45, 0.42, 0.38, 1.0))
        MEL.connect_material_expressions(g, "", base, "A")
    MEL.connect_material_expressions(tint, "", base, "B")
    MEL.connect_material_property(base, "", P.MP_BASE_COLOR)
    if nm is not None:
        t = MEL.create_material_expression(mat, unreal.MaterialExpressionTextureSampleParameter2D, -800, 160)
        t.set_editor_property("parameter_name", "NormalTex")
        t.set_editor_property("texture", nm)
        t.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
        MEL.connect_material_expressions(uvm, "", t, "UVs")
        MEL.connect_material_property(t, "RGB", P.MP_NORMAL)
    if orm is not None:
        t = MEL.create_material_expression(mat, unreal.MaterialExpressionTextureSampleParameter2D, -800, 400)
        t.set_editor_property("parameter_name", "ORMTex")
        t.set_editor_property("texture", orm)
        t.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)
        MEL.connect_material_expressions(uvm, "", t, "UVs")
        MEL.connect_material_property(t, "R", P.MP_AMBIENT_OCCLUSION)
        MEL.connect_material_property(t, "G", P.MP_ROUGHNESS)
    else:
        r = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant, -300, 300)
        r.set_editor_property("r", 0.85)
        MEL.connect_material_property(r, "", P.MP_ROUGHNESS)
    spec = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant, -300, 360)
    spec.set_editor_property("r", 0.3)
    MEL.connect_material_property(spec, "", P.MP_SPECULAR)
    MEL.recompile_material(mat)
    EAL.save_loaded_asset(mat)
    rep["created"].append(CLOTH)
    return CLOTH


def _import_skel(name, skeleton, force, rep):
    """SourceArt/Character/MetaHuman/<name>.fbx -> MH_ROOT/<name> on the body's skeleton (re-imported when the generator
    wrote a newer file)."""
    dst = f"{MH_ROOT}/{name}"
    fbx = os.path.join(_project(), "SourceArt", "Character", "MetaHuman", f"{name}.fbx")
    if not os.path.exists(fbx):
        rep["notes"].append(f"{fbx} missing (Tools/gasp/export_mh_body.py, then Tools/blender/character/mh_outfit.py)")
        return None
    if EAL.does_asset_exist(dst) and not force and os.path.getmtime(fbx) <= _stamp(name):
        rep["skipped"].append(dst)
        return dst
    unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX 0")
    try:
        ui = unreal.FbxImportUI()
        for k, v in (("import_mesh", True), ("import_as_skeletal", True), ("import_materials", False), ("import_textures", False),
                     ("import_animations", False), ("create_physics_asset", False), ("skeleton", skeleton),
                     ("mesh_type_to_import", unreal.FBXImportType.FBXIT_SKELETAL_MESH)):
            ui.set_editor_property(k, v)
        smd = ui.get_editor_property("skeletal_mesh_import_data")
        smd.set_editor_property("import_morph_targets", False)
        smd.set_editor_property("update_skeleton_reference_pose", False)
        smd.set_editor_property("normal_import_method", unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS)
        t = unreal.AssetImportTask()
        t.set_editor_property("filename", fbx)
        t.set_editor_property("destination_path", MH_ROOT)
        t.set_editor_property("destination_name", name)
        t.set_editor_property("replace_existing", True)
        t.set_editor_property("automated", True)
        t.set_editor_property("save", False)
        t.set_editor_property("options", ui)
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([t])
    finally:
        unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX 1")
    m = _load(dst)
    if not isinstance(m, unreal.SkeletalMesh):
        rep["failed"].append({"item": dst, "error": "FBX import produced no skeletal mesh"})
        return None
    EAL.save_loaded_asset(m)
    _stamp(name, write=True)
    rep["created"].append(dst)
    return dst


def _stamp(name, write=False):
    """Import time of an outfit FBX (re-import when the generator wrote a newer file)."""
    p = os.path.join(_project(), "Saved", "Fourfold", f"{name}_import.stamp")
    if write:
        os.makedirs(os.path.dirname(p), exist_ok=True)
        open(p, "w").write("ok")
        return 0.0
    return os.path.getmtime(p) if os.path.exists(p) else 0.0


def _pants_instance(force, rep):
    if EAL.does_asset_exist(PANTS_MI) and not force:
        return PANTS_MI
    if EAL.does_asset_exist(PANTS_MI):
        EAL.delete_asset(PANTS_MI)
    mi = unreal.AssetToolsHelpers.get_asset_tools().create_asset(PANTS_MI.rsplit("/", 1)[1], PANTS_MI.rsplit("/", 1)[0],
                                                                 unreal.MaterialInstanceConstant,
                                                                 unreal.MaterialInstanceConstantFactoryNew())
    MEL.set_material_instance_parent(mi, _load(CLOTH))
    MEL.set_material_instance_vector_parameter_value(mi, "FF_Tint", unreal.LinearColor(*PANTS_TINT, 1.0))
    MEL.update_material_instance(mi)
    EAL.save_loaded_asset(mi)
    rep["created"].append(PANTS_MI)
    return PANTS_MI


def check_bind(rep, seq_path):
    """The pants must share the body's bind pose bone for bone (they follow its leader pose): compare a clip evaluated on
    both meshes."""
    seq, body, pants = _load(seq_path), _load(BODY), _load(PANTS)
    if not (seq and body and pants):
        return
    worst = 0.0
    for mesh_pair in ((body, pants),):
        poses = []
        for mesh in mesh_pair:
            opts = unreal.AnimPoseEvaluationOptions()
            opts.set_editor_property("optional_skeletal_mesh", mesh)
            poses.append(unreal.AnimPoseExtensions.get_anim_pose_at_time(seq, 0.3, opts))
        for bone in ("pelvis", "thigh_l", "calf_l", "foot_l", "thigh_r", "calf_r", "foot_r", "spine_01"):
            a = unreal.AnimPoseExtensions.get_bone_pose(poses[0], bone, unreal.AnimPoseSpaces.WORLD)
            b = unreal.AnimPoseExtensions.get_bone_pose(poses[1], bone, unreal.AnimPoseSpaces.WORLD)
            worst = max(worst, (a.translation - b.translation).length(),
                        a.rotation.angular_distance(b.rotation) * 57.3 / 10.0)   # 10 deg counts like 1 cm
    rep["notes"].append(f"pants bind check vs SKM_FF_Body: worst {worst:.3f} (cm / 10 deg)")
    if worst > 0.5:
        rep["failed"].append({"item": PANTS, "error": f"pants bind pose differs from the body ({worst:.2f})"})


def pieces(skeleton, force, rep):
    """metahuman.json parts for the outfit (pants + sash), [] when the pants FBX is missing."""
    pants = _import_skel("SKM_FF_Pants", skeleton, force, rep)
    if pants is None:
        return []
    mat = _cloth_material(force, rep)
    out = [{"name": "Pants", "kind": "mesh", "asset": pants, "attach": None, "materials": [_pants_instance(force, rep)],
            "enabled": True}]
    sash = _import_skel("SKM_FF_Sash", skeleton, force, rep)
    if sash is not None:
        out.append({"name": "Sash", "kind": "mesh", "asset": sash, "attach": None, "materials": [mat], "tint_param": "FF_Tint",
                    "enabled": True})
    return out
