"""Open-world ground detail (CC0 Poly Haven scans, unreal/Tools/openworld/prep_vegetation.py): textures, material instances of
the tree masters (M_FF_TreeLeaf for foliage: masked, two-sided, wind; M_FF_TreeBark for boulders, no sway) and the LOD'd
meshes /Game/Fourfold/OpenWorld/Vegetation/SM_OW_*. LODs 1-2 need the full editor (-ExecutePythonScript)."""
import json
import os

import unreal

from fourfold.world import common as C
from fourfold.world import foliage as FO
from fourfold.world import meshes as WM

ROOT = "/Game/Fourfold/OpenWorld/Vegetation"
TEX = ROOT + "/Textures"
LOD_SCREEN = [0.5, 0.18, 0.06]
MEL = unreal.MaterialEditingLibrary


def _src():
    proj = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
    return os.path.join(proj, "SourceArt", "OpenWorld", "Meshes")


def _import(files, dest, rep, force, settings=None):
    tasks = []
    for src, name in files:
        dst = f"{dest}/{name}"
        if C.asset_exists(dst) and not force:
            rep["skipped"].append(dst)
            continue
        t = unreal.AssetImportTask()
        for k, v in (("filename", src), ("destination_path", dest), ("destination_name", name), ("replace_existing", True),
                     ("automated", True), ("save", False)):
            t.set_editor_property(k, v)
        tasks.append((t, dst))
    if tasks:
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([t for t, _ in tasks])
    for _t, dst in tasks:
        a = unreal.load_asset(dst) if C.asset_exists(dst) else None
        if a is None:
            rep["failed"].append({"item": dst, "error": "import produced nothing"})
            continue
        if settings:
            settings(a, dst)
        C.save_asset(a)
        rep["created"].append(dst)


def _tex_settings(rep):
    def f(tex, dst):
        name = dst.rsplit("/", 1)[-1]
        kind = "normal" if name.endswith("_N") else "orm" if name.endswith("_ORM") else "op" if name.endswith("_OP") else "color"
        FO._tex_settings(tex, {"normal": "normal", "orm": "masks", "op": "opacity", "color": "color"}[kind], rep)
    return f


def build_all(force=False):
    rep = {"created": [], "skipped": [], "failed": [], "notes": []}
    src = _src()
    man_path = os.path.join(src, "vegetation.json")
    if not os.path.exists(man_path):
        rep["failed"].append({"item": "vegetation", "error": f"missing {man_path} (run Tools/openworld/prep_vegetation.py)"})
        return rep
    man = json.load(open(man_path))
    C.make_dirs(ROOT, TEX)
    # textures (BC sRGB, N, ORM masks, OP grayscale)
    pngs = sorted(f for f in os.listdir(src) if f.startswith("T_OW_") and f.endswith(".png") and "_Dry" not in f)
    _import([(os.path.join(src, f), f[:-4]) for f in pngs], TEX, rep, force, _tex_settings(rep))
    leaf = unreal.load_asset(FO.TREE_DIR + "/M_FF_TreeLeaf")
    bark = unreal.load_asset(FO.TREE_DIR + "/M_FF_TreeBark")
    if leaf is None or bark is None:
        rep["failed"].append({"item": "masters", "error": "M_FF_TreeLeaf / M_FF_TreeBark missing (run the world part first)"})
        return rep
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    mis = {}
    for mesh, m in man["meshes"].items():
        for slot, tdef in m["slots"].items():
            key = f"{slot}_{mesh}" if m["kind"] == "boulder" else slot
            if key in mis:
                continue
            name = f"MI_OW_{key}"
            path = f"{ROOT}/{name}"
            if C.asset_exists(path) and not force:
                mis[key] = unreal.load_asset(path)
                continue
            mi = unreal.load_asset(path) if C.asset_exists(path) else tools.create_asset(
                name, ROOT, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
            MEL.set_material_instance_parent(mi, leaf if m.get("masked") else bark)
            for k, param in (("BC", "BaseColor"), ("N", "Normal"), ("ORM", "ORM")):
                t = unreal.load_asset(f"{TEX}/{tdef[k][:-4]}") if k in tdef else None
                if t is not None:
                    MEL.set_material_instance_texture_parameter_value(mi, param, t)
            if m.get("masked"):
                op = unreal.load_asset(f"{TEX}/{tdef['BC'][:-4].replace('_BC', '_OP')}")
                if op is not None:
                    MEL.set_material_instance_texture_parameter_value(mi, "Opacity", op)
                MEL.set_material_instance_scalar_parameter_value(mi, "SwayCm", 3.0 if m["kind"] == "grass" else 5.0)
                MEL.set_material_instance_scalar_parameter_value(mi, "FlutterCm", 1.5)
                MEL.set_material_instance_scalar_parameter_value(mi, "ColorVariation", 0.18)
                if m["kind"] == "grass":
                    MEL.set_material_instance_scalar_parameter_value(mi, "Brightness", 2.4)
                    MEL.set_material_instance_vector_parameter_value(mi, "Transmission", unreal.LinearColor(0.75, 0.85, 0.35, 1.0))
            else:
                MEL.set_material_instance_scalar_parameter_value(mi, "SwayCm", 0.0)
            MEL.update_material_instance(mi)
            C.save_asset(mi)
            mis[key] = mi
            rep["created"].append(path)
    # meshes (LOD0 import + LOD1/2), slot materials, no collision
    sms = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    for mesh, m in man["meshes"].items():
        dst = f"{ROOT}/{mesh}"
        if C.asset_exists(dst) and not force:
            rep["skipped"].append(dst)
            continue
        pipe = WM._pipeline(mesh, rep)
        stack = unreal.InterchangePipelineStackOverride()
        if hasattr(stack, "add_pipeline"):
            stack.add_pipeline(pipe)
        else:
            stack.get_editor_property("override_pipelines").append(pipe)
        t = unreal.AssetImportTask()
        for k, v in (("filename", os.path.join(src, m["lods"][0]["fbx"])), ("destination_path", ROOT), ("destination_name", mesh),
                     ("replace_existing", True), ("automated", True), ("save", False), ("options", stack)):
            t.set_editor_property(k, v)
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([t])
        sm = unreal.load_asset(dst) if C.asset_exists(dst) else None
        if sm is None:
            rep["failed"].append({"item": dst, "error": "no static mesh after import"})
            continue
        for i, lod in enumerate(m["lods"][1:], start=1):
            FO._import_lod(sm, i, os.path.join(src, lod["fbx"]), rep)
        try:
            C.set_prop(sm, "auto_compute_lod_screen_size", False, rep, quiet=True)
            sms.set_lod_screen_sizes(sm, LOD_SCREEN[:len(m["lods"])])
        except Exception as e:  # noqa: BLE001
            rep["notes"].append(f"{mesh}: LOD screen sizes not set ({e})")
        WM._remove_collision(sm, rep)
        slots = list(sm.get_editor_property("static_materials") or [])
        for s in slots:
            nm = str(s.get_editor_property("material_slot_name")).split(".")[0]
            key = f"{nm}_{mesh}" if m["kind"] == "boulder" else nm
            if key in mis:
                s.set_editor_property("material_interface", mis[key])
            else:
                rep["notes"].append(f"{mesh}: slot {nm} without a material")
        sm.set_editor_property("static_materials", slots)
        C.save_asset(sm)
        rep["created"].append(dst)
    return rep
