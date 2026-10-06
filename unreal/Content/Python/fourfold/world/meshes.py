"""Static mesh import for the courtyard (FBX -> UStaticMesh) + per-mesh settings.

Source: unreal/SourceArt/Environment/Meshes/SM_Env_*.fbx and meshes.json (Tools/world/export_env.py).  Meshes are authored at the
real arena size with the pivot at the box centre; the FBX slot (material) names are the env slot names (Floor, StoneWall, ...), mapped
to MI_Env_<slot> here.  Interchange is used first (pipeline: static meshes only, no materials / textures), the classic FBX importer
(Interchange FBX switched off for the duration) is the fallback."""
import os

import unreal

from . import common as C


def _pipeline(name, report):
    pipe = unreal.InterchangeGenericAssetsPipeline()
    C.set_prop(pipe, "asset_name", name, report, quiet=True)
    C.set_prop(pipe, "use_source_name_for_asset", False, report, quiet=True)
    mp = C.get_prop(pipe, "mesh_pipeline")
    if mp is not None:
        C.set_prop(mp, "import_static_meshes", True, report)
        C.set_prop(mp, "import_skeletal_meshes", False, report)
        C.set_prop(mp, "combine_static_meshes", True, report, quiet=True)
        C.set_prop(mp, "build_nanite", False, report, quiet=True)
        C.set_prop(mp, "generate_lightmap_u_vs", False, report, quiet=True)      # UV1 comes from the FBX (Tools/world/meshkit lightmap packer)
    cm = C.get_prop(pipe, "common_meshes_properties")
    if cm is not None:
        C.set_prop(cm, "force_all_mesh_as_type", C.enum("InterchangeForceMeshType", "IFMT_STATIC_MESH"), report, quiet=True)
        C.set_prop(cm, "recompute_normals", False, report, quiet=True)              # smoothing groups / normals come from the file
        C.set_prop(cm, "recompute_tangents", True, report, quiet=True)
    matp = C.get_prop(pipe, "material_pipeline")
    if matp is not None:
        C.set_prop(matp, "import_materials", False, report, quiet=True)
        tp = C.get_prop(matp, "texture_pipeline")
        if tp is not None:
            C.set_prop(tp, "import_textures", False, report, quiet=True)
    return pipe


def _import_interchange(fbx, name, report):
    pipe = _pipeline(name, report)
    stack = unreal.InterchangePipelineStackOverride()
    if hasattr(stack, "add_pipeline"):
        stack.add_pipeline(pipe)
    else:
        stack.get_editor_property("override_pipelines").append(pipe)
    t = unreal.AssetImportTask()
    t.set_editor_property("filename", fbx)
    t.set_editor_property("destination_path", C.MESH_DIR)
    t.set_editor_property("destination_name", name)
    t.set_editor_property("replace_existing", True)
    t.set_editor_property("automated", True)
    t.set_editor_property("save", False)
    t.set_editor_property("options", stack)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([t])


def _import_legacy(fbx, name, report):
    unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX 0")
    try:
        ui = unreal.FbxImportUI()
        C.set_prop(ui, "import_mesh", True, report)
        C.set_prop(ui, "import_as_skeletal", False, report)
        C.set_prop(ui, "import_materials", False, report)
        C.set_prop(ui, "import_textures", False, report)
        C.set_prop(ui, "import_animations", False, report)
        C.set_prop(ui, "mesh_type_to_import", C.enum("FBXImportType", "FBXIT_STATIC_MESH"), report, quiet=True)
        sd = C.get_prop(ui, "static_mesh_import_data")
        if sd is not None:
            C.set_prop(sd, "combine_meshes", True, report, quiet=True)
            C.set_prop(sd, "auto_generate_collision", False, report, quiet=True)
            C.set_prop(sd, "generate_lightmap_u_vs", False, report, quiet=True)
            C.set_prop(sd, "normal_import_method", C.enum("FBXNormalImportMethod", "FBXNIM_IMPORT_NORMALS"), report, quiet=True)
        t = unreal.AssetImportTask()
        t.set_editor_property("filename", fbx)
        t.set_editor_property("destination_path", C.MESH_DIR)
        t.set_editor_property("destination_name", name)
        t.set_editor_property("replace_existing", True)
        t.set_editor_property("automated", True)
        t.set_editor_property("save", False)
        t.set_editor_property("options", ui)
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([t])
    finally:
        unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX 1")


def _remove_collision(mesh, report):
    for owner, fn in ((getattr(unreal, "StaticMeshEditorSubsystem", None), "remove_collisions"),
                      (getattr(unreal, "EditorStaticMeshLibrary", None), "remove_collisions")):
        if owner is None:
            continue
        try:
            sub = unreal.get_editor_subsystem(owner) if owner.__name__ == "StaticMeshEditorSubsystem" else owner
            getattr(sub, fn)(mesh)
            return True
        except Exception:  # noqa: BLE001
            continue
    report["notes"].append(f"{mesh.get_name()}: could not remove generated collision (components have collision disabled anyway)")
    return False


def _assign_materials(mesh, instances, report):
    """Assign MI_Env_<slot> by the FBX material (slot) name."""
    try:
        slots = list(mesh.get_editor_property("static_materials") or [])
        missing = []
        for sm in slots:
            nm = str(sm.get_editor_property("material_slot_name"))
            imp = str(sm.get_editor_property("imported_material_slot_name"))
            key = None
            for cand in (nm, imp):
                base = cand.split(".")[0]
                if base in instances:
                    key = base
                    break
            if key is None:
                missing.append(nm)
                continue
            sm.set_editor_property("material_interface", instances[key])
        if slots:
            mesh.set_editor_property("static_materials", slots)
        if missing:
            report["notes"].append(f"{mesh.get_name()}: material slots without an env material: {missing}")
    except Exception as e:  # noqa: BLE001
        report["failed"].append({"item": mesh.get_name() + " materials", "error": str(e)})


def _bounds_cm(mesh):
    """(size[3], center[3]) of the imported mesh in Unreal cm, or None."""
    try:
        b = mesh.get_bounds()
        ext = b.get_editor_property("box_extent")
        org = b.get_editor_property("origin")
        return [2.0 * ext.x, 2.0 * ext.y, 2.0 * ext.z], [org.x, org.y, org.z]
    except Exception:  # noqa: BLE001
        return None


def import_all(force, instances, report):
    """Imports every mesh of meshes.json.  Returns {name: dict(path, size, center, ratio)} (ratio = actual / expected size)."""
    paths = C.project_paths()
    manifest = C.load_json(paths["meshes_json"], report, "meshes.json")
    out = {}
    if not manifest:
        report["failed"].append({"item": "meshes", "error": "meshes.json missing (run Tools/world/export_env.py)"})
        return out
    C.make_dirs(C.ENV_ROOT, C.MESH_DIR)
    for name, info in manifest["meshes"].items():
        dst = f"{C.MESH_DIR}/{name}"
        fbx = os.path.join(paths["art"], info["file"])
        fresh = False
        if C.asset_exists(dst) and not force:
            report["skipped"].append(dst)
        else:
            if not os.path.exists(fbx):
                report["failed"].append({"item": dst, "error": f"missing {fbx}"})
                continue
            try:
                _import_interchange(fbx, name, report)
            except Exception as e:  # noqa: BLE001
                report["notes"].append(f"{name}: Interchange import raised ({e}); trying the classic FBX importer")
            if not C.asset_exists(dst):
                try:
                    _import_legacy(fbx, name, report)
                except Exception as e:  # noqa: BLE001
                    report["failed"].append({"item": dst, "error": f"legacy import raised: {e}"})
            fresh = True
        mesh = unreal.load_asset(dst) if C.asset_exists(dst) else None
        if mesh is None or not isinstance(mesh, unreal.StaticMesh):
            report["failed"].append({"item": dst, "error": "no static mesh after import"})
            continue
        if fresh:
            lm = info.get("lightmap_res")
            if lm:
                C.set_prop(mesh, "light_map_resolution", int(lm), report, quiet=True)
                C.set_prop(mesh, "light_map_coordinate_index", 1, report, quiet=True)
            _remove_collision(mesh, report)
            _assign_materials(mesh, instances, report)
            C.save_asset(mesh)
            report["created"].append(dst)
        bc = _bounds_cm(mesh)
        entry = dict(path=dst, expected=info["expected_size_cm"], info=info)
        if bc:
            size, center = bc
            entry["size"], entry["center"] = size, center
            ratios = [s / e for s, e in zip(size, info["expected_size_cm"]) if e > 1.0]
            if ratios:
                ratios.sort()
                entry["ratio"] = ratios[len(ratios) // 2]
        out[name] = entry
    return out


def detect_mirror_y(meshes, report):
    """True when the imported meshes are mirrored in Y relative to meshes.json (a wrong FBX axis assumption).  Votes over every mesh
    whose bounds centre is far from its pivot (pool basin, pavilions, halls, tower, ...)."""
    votes_ok = votes_mirror = 0
    for name, e in meshes.items():
        if "center" not in e:
            continue
        b = e["info"]["bounds_sim"]            # [[minx, miny, minz], [maxx, maxy, maxz]] relative to the pivot, sim axes (y up)
        exp_y = 100.0 * 0.5 * (b[0][2] + b[1][2])      # Unreal Y = sim z
        if abs(exp_y) < 80.0:
            continue
        act_y = e["center"][1]
        if exp_y * act_y > 0:
            votes_ok += 1
        elif exp_y * act_y < 0:
            votes_mirror += 1
    if votes_mirror > votes_ok:
        report["notes"].append(f"FBX import looks MIRRORED in Y ({votes_mirror} vs {votes_ok} meshes): actors get scale Y = -1 to compensate")
        return True
    report["notes"].append(f"FBX axis check: {votes_ok} meshes match the expected Y, {votes_mirror} mirrored")
    return False
