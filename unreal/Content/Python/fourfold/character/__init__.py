"""Fourfold fighter import (stream `character`): textures, master materials + instances, skeletal mesh (+ LOD1 / LOD2),
skeleton and physics asset, material assignment, LOD screen sizes.

    import fourfold.character as fc; fc.build_all(force=False)   -> report dict

Sources (generated in Blender, committed): unreal/SourceArt/Character/SK_Fighter.fbx, SK_Fighter_LOD1.fbx,
SK_Fighter_LOD2.fbx, T_Fighter_<slot>_{BC,N,ORM}.png, T_Fighter_Detail_*.png; settings from
Content/Fourfold/Data/character.json. Produces (paths are a contract with the game / animation streams):
    /Game/Fourfold/Characters/Fighter/SK_Fighter, SKEL_Fighter, PA_Fighter
    /Game/Fourfold/Characters/Fighter/Materials/M_Fighter_{Skin,Cloth,Hair,Eyes}, MI_Fighter_<slot>
    /Game/Fourfold/Characters/Fighter/Textures/T_Fighter_*
Idempotent: existing assets are kept unless force=True. Never raises: failures are collected in the report.
APIs used are listed (with sources) in unreal/docs/character/API_NOTES.md.
"""
import json
import os
import traceback

import unreal

from . import materials as M

ROOT = "/Game/Fourfold/Characters/Fighter"
TEX_DIR = ROOT + "/Textures"
MAT_DIR = ROOT + "/Materials"
MESH = ROOT + "/SK_Fighter"
SKELETON = ROOT + "/SKEL_Fighter"
PHYSICS = ROOT + "/PA_Fighter"
SLOTS = ["skin", "hair", "eyes", "cloth_main", "cloth_accent", "wraps", "sash", "shoes"]
DETAIL_TEXTURES = ["T_Fighter_Detail_Weave_N", "T_Fighter_Detail_Skin_N", "T_Fighter_Detail_Noise"]
DEFAULT_SCREEN_SIZES = [1.0, 0.30, 0.12]
EAL = unreal.EditorAssetLibrary


def _log(msg):
    unreal.log(f"[Fourfold][character] {msg}")


def _err(msg):
    unreal.log_error(f"[Fourfold][character] {msg}")


def _paths():
    proj = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
    art = os.path.join(proj, "SourceArt", "Character")
    data = os.path.join(proj, "Content", "Fourfold", "Data", "character.json")
    return art, data


def _load_config(path, report):
    try:
        with open(path, encoding="utf-8") as f:
            return json.load(f)
    except Exception as e:  # noqa: BLE001
        report["notes"].append(f"character.json not readable ({e}); using defaults")
        return {}


def _set(obj, prop, value, report):
    try:
        obj.set_editor_property(prop, value)
        return True
    except Exception as e:  # noqa: BLE001
        report["notes"].append(f"could not set {prop}: {e}")
        return False


def _save(asset):
    try:
        EAL.save_loaded_asset(asset, False)
    except Exception:  # noqa: BLE001
        EAL.save_loaded_asset(asset)


# ------------------------------------------------------------------------------------------------ textures
def _texture_kind(name):
    if name.endswith("_BC"):
        return "BC"
    if name.endswith("_N"):
        return "N"
    if name.endswith("_ORM") or name.endswith("_Noise"):
        return "ORM"
    return "BC"


def import_textures(art, force, report):
    names = [f"T_Fighter_{s}_{k}" for s in SLOTS for k in ("BC", "N", "ORM")] + DETAIL_TEXTURES
    tasks = []
    for name in names:
        src = os.path.join(art, name + ".png")
        dst = f"{TEX_DIR}/{name}"
        if not os.path.exists(src):
            report["failed"].append({"item": dst, "error": f"missing source {src}"})
            continue
        if EAL.does_asset_exist(dst) and not force:
            report["skipped"].append(dst)
            continue
        t = unreal.AssetImportTask()
        t.set_editor_property("filename", src)
        t.set_editor_property("destination_path", TEX_DIR)
        t.set_editor_property("destination_name", name)
        t.set_editor_property("replace_existing", True)
        t.set_editor_property("automated", True)
        t.set_editor_property("save", False)
        tasks.append((t, name, dst))
    if tasks:
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([t for t, _, _ in tasks])
    TC = unreal.TextureCompressionSettings
    TG = unreal.TextureGroup
    for _t, name, dst in tasks:
        tex = unreal.load_asset(dst)
        if tex is None:
            report["failed"].append({"item": dst, "error": "texture import produced nothing"})
            continue
        kind = _texture_kind(name)
        if kind == "BC":
            _set(tex, "srgb", True, report)
            _set(tex, "compression_settings", TC.TC_DEFAULT, report)
            _set(tex, "lod_group", TG.TEXTUREGROUP_CHARACTER, report)
        elif kind == "N":
            _set(tex, "srgb", False, report)
            _set(tex, "compression_settings", TC.TC_NORMALMAP, report)
            _set(tex, "flip_green_channel", False, report)      # already DirectX (green down)
            _set(tex, "lod_group", TG.TEXTUREGROUP_CHARACTER_NORMAL_MAP, report)
        else:
            _set(tex, "srgb", False, report)
            _set(tex, "compression_settings", TC.TC_MASKS, report)
            _set(tex, "lod_group", TG.TEXTUREGROUP_CHARACTER_SPECULAR, report)
        _save(tex)
        report["created"].append(dst)


def _tex(name):
    path = f"{TEX_DIR}/{name}"
    return unreal.load_asset(path) if EAL.does_asset_exist(path) else None


# ------------------------------------------------------------------------------------------------ materials
def build_materials(cfg, force, report):
    tiling = cfg.get("detail_tiling", {})
    master_tex = {
        "M_Fighter_Skin": "skin", "M_Fighter_Cloth": "cloth_main", "M_Fighter_Hair": "hair", "M_Fighter_Eyes": "eyes"}
    masters = {}
    for name, builder in M.MASTERS.items():
        path = f"{MAT_DIR}/{name}"
        try:
            mat, rebuild = M.get_or_create_material(MAT_DIR, name, force)
            masters[name] = mat
            if not rebuild:
                report["skipped"].append(path)
                continue
            slot = master_tex[name]
            tex = {"BC": _tex(f"T_Fighter_{slot}_BC"), "N": _tex(f"T_Fighter_{slot}_N"),
                   "ORM": _tex(f"T_Fighter_{slot}_ORM"), "NOISE": _tex("T_Fighter_Detail_Noise"),
                   "DETAIL": _tex("T_Fighter_Detail_Skin_N" if slot == "skin" else "T_Fighter_Detail_Weave_N"),
                   "detail_tiling": float(tiling.get(slot, 60.0))}
            builder(mat, tex, report["notes"])
            M.recompile(mat, report["notes"])
            _save(mat)
            report["created"].append(path)
        except Exception as e:  # noqa: BLE001
            report["failed"].append({"item": path, "error": f"{e}\n{traceback.format_exc()}"})
    tints = cfg.get("slot_tint_select", {})
    params = cfg.get("slot_params", {})
    instances = {}
    for slot in SLOTS:
        name = f"MI_Fighter_{slot}"
        path = f"{MAT_DIR}/{name}"
        parent = masters.get(M.SLOT_MASTER[slot])
        if parent is None:
            report["failed"].append({"item": path, "error": "parent material missing"})
            continue
        try:
            mi, rebuild = M.get_or_create_instance(MAT_DIR, name, parent, force)
            instances[slot] = mi
            if not rebuild:
                report["skipped"].append(path)
                continue
            for k in ("BC", "N", "ORM"):
                t = _tex(f"T_Fighter_{slot}_{k}")
                pname = {"BC": "BaseColor", "N": "Normal", "ORM": "ORM"}[k]
                if t is not None:
                    M.MEL.set_material_instance_texture_parameter_value(mi, pname, t)
            det = _tex("T_Fighter_Detail_Skin_N" if slot == "skin" else "T_Fighter_Detail_Weave_N")
            if det is not None and slot not in ("hair", "eyes"):
                M.MEL.set_material_instance_texture_parameter_value(mi, "DetailNormal", det)
            if slot in tiling and slot not in ("hair", "eyes"):
                M.MEL.set_material_instance_scalar_parameter_value(mi, "DetailTiling", float(tiling[slot]))
            if slot in tints:
                r, gg, b = tints[slot]
                M.MEL.set_material_instance_vector_parameter_value(mi, "TintSelect", unreal.LinearColor(r, gg, b, 1.0))
            for pname, val in params.get(slot, {}).items():
                if isinstance(val, (list, tuple)):
                    M.MEL.set_material_instance_vector_parameter_value(mi, pname, unreal.LinearColor(*[float(c) for c in (list(val) + [1.0])[:4]]))
                else:
                    M.MEL.set_material_instance_scalar_parameter_value(mi, pname, float(val))
            # default palette = player
            pal = cfg.get("palettes", {}).get("player", {})
            for pname, rgba in pal.items():
                M.MEL.set_material_instance_vector_parameter_value(mi, pname, unreal.LinearColor(*[float(c) for c in rgba]))
            M.MEL.update_material_instance(mi)
            _save(mi)
            report["created"].append(path)
        except Exception as e:  # noqa: BLE001
            report["failed"].append({"item": path, "error": f"{e}\n{traceback.format_exc()}"})
    return instances


# ------------------------------------------------------------------------------------------------ mesh
def _interchange_pipeline(report):
    pipe = unreal.InterchangeGenericAssetsPipeline()
    _set(pipe, "asset_name", "SK_Fighter", report)
    _set(pipe, "use_source_name_for_asset", False, report)
    mp = pipe.get_editor_property("mesh_pipeline")
    _set(mp, "import_skeletal_meshes", True, report)
    _set(mp, "import_static_meshes", False, report)
    _set(mp, "create_physics_asset", True, report)
    _set(mp, "import_morph_targets", False, report)
    cm = pipe.get_editor_property("common_meshes_properties")
    _set(cm, "force_all_mesh_as_type", unreal.InterchangeForceMeshType.IFMT_SKELETAL_MESH, report)
    _set(cm, "recompute_normals", False, report)
    _set(cm, "recompute_tangents", False, report)
    sk = pipe.get_editor_property("common_skeletal_meshes_and_animations_properties")
    _set(sk, "import_only_animations", False, report)
    matp = pipe.get_editor_property("material_pipeline")
    _set(matp, "import_materials", False, report)
    try:
        _set(matp.get_editor_property("texture_pipeline"), "import_textures", False, report)
    except Exception as e:  # noqa: BLE001
        report["notes"].append(f"texture_pipeline: {e}")
    try:
        _set(pipe.get_editor_property("animation_pipeline"), "import_animations", False, report)
    except Exception as e:  # noqa: BLE001
        report["notes"].append(f"animation_pipeline: {e}")
    return pipe


def _import_fbx_interchange(fbx, report):
    pipe = _interchange_pipeline(report)
    stack = unreal.InterchangePipelineStackOverride()
    if hasattr(stack, "add_pipeline"):
        stack.add_pipeline(pipe)
    else:
        stack.get_editor_property("override_pipelines").append(pipe)
    t = unreal.AssetImportTask()
    t.set_editor_property("filename", fbx)
    t.set_editor_property("destination_path", ROOT)
    t.set_editor_property("destination_name", "SK_Fighter")
    t.set_editor_property("replace_existing", True)
    t.set_editor_property("automated", True)
    t.set_editor_property("save", False)
    t.set_editor_property("options", stack)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([t])
    return list(t.get_editor_property("imported_object_paths") or [])


def _import_fbx_legacy(fbx, report):
    """Fallback: the classic FBX importer (Interchange FBX switched off for the duration)."""
    unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX 0")
    try:
        ui = unreal.FbxImportUI()
        _set(ui, "import_mesh", True, report)
        _set(ui, "import_as_skeletal", True, report)
        _set(ui, "import_materials", False, report)
        _set(ui, "import_textures", False, report)
        _set(ui, "import_animations", False, report)
        _set(ui, "create_physics_asset", True, report)
        _set(ui, "mesh_type_to_import", unreal.FBXImportType.FBXIT_SKELETAL_MESH, report)
        smd = ui.get_editor_property("skeletal_mesh_import_data")
        _set(smd, "import_morph_targets", False, report)
        _set(smd, "normal_import_method", unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS_AND_TANGENTS, report)
        t = unreal.AssetImportTask()
        t.set_editor_property("filename", fbx)
        t.set_editor_property("destination_path", ROOT)
        t.set_editor_property("destination_name", "SK_Fighter")
        t.set_editor_property("replace_existing", True)
        t.set_editor_property("automated", True)
        t.set_editor_property("save", False)
        t.set_editor_property("options", ui)
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([t])
        return list(t.get_editor_property("imported_object_paths") or [])
    finally:
        unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX 1")


def _find_in_root(cls):
    out = []
    for p in EAL.list_assets(ROOT, recursive=False, include_folder=False) or []:
        a = unreal.load_asset(p)
        if a is not None and isinstance(a, cls):
            out.append(a)
    return out


def _rename(asset, target_path, report):
    cur = asset.get_path_name().split(".")[0]
    if cur == target_path:
        return True
    if EAL.does_asset_exist(target_path):
        report["notes"].append(f"{target_path} exists; leaving {cur} in place")
        return False
    ok = EAL.rename_asset(cur, target_path)
    if ok:
        report["notes"].append(f"renamed {cur} -> {target_path}")
    return ok


def import_mesh(art, force, report):
    fbx = os.path.join(art, "SK_Fighter.fbx")
    if EAL.does_asset_exist(MESH) and not force:
        report["skipped"].append(MESH)
        return unreal.load_asset(MESH)
    if not os.path.exists(fbx):
        report["failed"].append({"item": MESH, "error": f"missing {fbx}"})
        return None
    paths = []
    try:
        paths = _import_fbx_interchange(fbx, report)
    except Exception as e:  # noqa: BLE001
        report["notes"].append(f"Interchange import raised ({e}); trying the legacy FBX importer")
    mesh = unreal.load_asset(MESH) if EAL.does_asset_exist(MESH) else None
    if mesh is None or not isinstance(mesh, unreal.SkeletalMesh):
        report["notes"].append(f"Interchange produced {paths}; trying the legacy FBX importer")
        try:
            paths = _import_fbx_legacy(fbx, report)
        except Exception as e:  # noqa: BLE001
            report["failed"].append({"item": MESH, "error": f"legacy import raised: {e}"})
        mesh = unreal.load_asset(MESH) if EAL.does_asset_exist(MESH) else None
    if mesh is None:
        report["failed"].append({"item": MESH, "error": f"no skeletal mesh after import (imported: {paths})"})
        return None
    report["created"].append(MESH)
    # skeleton / physics asset to the contract names
    try:
        skel = mesh.get_editor_property("skeleton")
        if skel is not None:
            _rename(skel, SKELETON, report)
        phys = mesh.get_editor_property("physics_asset")
        if phys is not None:
            _rename(phys, PHYSICS, report)
    except Exception as e:  # noqa: BLE001
        report["failed"].append({"item": SKELETON, "error": f"rename failed: {e}"})
    for p, cls in ((SKELETON, unreal.Skeleton), (PHYSICS, unreal.PhysicsAsset)):
        if EAL.does_asset_exist(p):
            report["created"].append(p)
        else:
            report["notes"].append(f"{p} not found after import (looked for {cls})")
    return mesh


def import_lods(mesh, art, cfg, force, report):
    sub = unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem)
    try:
        count = sub.get_lod_count(mesh)
    except Exception:  # noqa: BLE001
        count = 1
    if isinstance(count, int) and count >= 3 and not force:
        report["skipped"].append(MESH + " LOD1/LOD2")
    else:
        for lod in (1, 2):
            fbx = os.path.join(art, f"SK_Fighter_LOD{lod}.fbx")
            if not os.path.exists(fbx):
                report["failed"].append({"item": f"LOD{lod}", "error": f"missing {fbx}"})
                continue
            try:
                res = sub.import_lod(mesh, lod, fbx)
                if res == -1:
                    raise RuntimeError("import_lod returned INDEX_NONE")
                report["created"].append(f"{MESH} LOD{lod}")
            except Exception as e:  # noqa: BLE001
                report["failed"].append({"item": f"LOD{lod}", "error": str(e)})
    sizes = cfg.get("lods", {}).get("screen_sizes", DEFAULT_SCREEN_SIZES)
    try:
        infos = list(mesh.get_editor_property("lod_info") or [])
        for i, info in enumerate(infos):
            if i < len(sizes):
                ps = unreal.PerPlatformFloat()
                ps.set_editor_property("default", float(sizes[i]))
                info.set_editor_property("screen_size", ps)
        if infos:
            mesh.set_editor_property("lod_info", infos)
    except Exception as e:  # noqa: BLE001
        report["notes"].append(f"LOD screen sizes not set: {e}")


def _slot_of(name):
    n = str(name).lower().strip()
    for s in sorted(SLOTS, key=len, reverse=True):
        if n == s or n.startswith(s) or n.endswith(s):
            return s
    return None


def assign_materials(mesh, instances, report):
    try:
        slots = list(mesh.get_editor_property("materials") or [])
        done = []
        for sm in slots:
            nm = sm.get_editor_property("material_slot_name")
            imp = sm.get_editor_property("imported_material_slot_name")
            slot = _slot_of(nm) or _slot_of(imp)
            if slot and slot in instances:
                sm.set_editor_property("material_interface", instances[slot])
                done.append(slot)
        if slots:
            mesh.set_editor_property("materials", slots)
        missing = [s for s in SLOTS if s not in done]
        if missing and slots:
            report["notes"].append(f"material slots not found on SK_Fighter: {missing}")
    except Exception as e:  # noqa: BLE001
        report["failed"].append({"item": MESH + " materials", "error": str(e)})


# ------------------------------------------------------------------------------------------------ entry
def build_all(force=False):
    report = {"created": [], "skipped": [], "failed": [], "notes": []}
    try:
        art, data = _paths()
        cfg = _load_config(data, report)
        for d in (ROOT, TEX_DIR, MAT_DIR):
            if not EAL.does_directory_exist(d):
                EAL.make_directory(d)
        steps = []
        try:
            import_textures(art, force, report)
            steps.append("textures")
        except Exception as e:  # noqa: BLE001
            report["failed"].append({"item": "textures", "error": f"{e}\n{traceback.format_exc()}"})
        instances = {}
        try:
            instances = build_materials(cfg, force, report)
            steps.append("materials")
        except Exception as e:  # noqa: BLE001
            report["failed"].append({"item": "materials", "error": f"{e}\n{traceback.format_exc()}"})
        mesh = None
        try:
            mesh = import_mesh(art, force, report)
            steps.append("mesh")
        except Exception as e:  # noqa: BLE001
            report["failed"].append({"item": MESH, "error": f"{e}\n{traceback.format_exc()}"})
        if mesh is not None:
            try:
                import_lods(mesh, art, cfg, force, report)
                steps.append("lods")
            except Exception as e:  # noqa: BLE001
                report["failed"].append({"item": "lods", "error": f"{e}\n{traceback.format_exc()}"})
            assign_materials(mesh, instances, report)
            try:
                _save(mesh)
            except Exception as e:  # noqa: BLE001
                report["failed"].append({"item": MESH, "error": f"save failed: {e}"})
        try:
            EAL.save_directory(ROOT, only_if_is_dirty=True, recursive=True)
        except Exception as e:  # noqa: BLE001
            report["notes"].append(f"save_directory: {e}")
        report["notes"].append("steps: " + ", ".join(steps))
    except Exception as e:  # noqa: BLE001
        report["failed"].append({"item": "character", "error": f"{e}\n{traceback.format_exc()}"})
    for f in report["failed"]:
        _err(f"{f['item']}: {f['error']}")
    _log(f"created {len(report['created'])}, skipped {len(report['skipped'])}, failed {len(report['failed'])}")
    return report
