"""Run inside the Game Animation Sample project: copies a MetaHuman's parts (meshes, grooms, groom bindings, materials;
NOT the sample's anim blueprints) into Fourfold and writes Content/Fourfold/Data/metahuman.json describing them.

  UnrealEditor-Cmd "<GASP>/GameAnimationSample.uproject" -run=pythonscript -script="<this file>" -unattended
  (FF_METAHUMAN=/Game/MetaHumans/Kellan/BP_Kellan by default)

metahuman.json: {"name", "body": path, "parts": [{"name", "kind": "mesh"|"groom", "asset", "binding", "attach",
"materials": [per-slot override material or null]}]}.
The copied content is git-ignored (Epic sample content); re-run after a fresh clone.
"""
import json
import os

import unreal

FF_CONTENT = os.environ.get("FF_CONTENT", os.path.expanduser("~/Fourfold/unreal/Content"))
BP = os.environ.get("FF_METAHUMAN", "/Game/MetaHumans/Kellan/BP_Kellan")


def _path(obj):
    return obj.get_path_name().split(".")[0] if obj else None


bp = unreal.load_asset(BP)
sds = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
parts, seen, packages = [], set(), set()
body = None
for h in sds.k2_gather_subobject_data_for_blueprint(bp):
    d = unreal.SubobjectDataBlueprintFunctionLibrary.get_data(h)
    obj = unreal.SubobjectDataBlueprintFunctionLibrary.get_object(d)
    if obj is None:
        continue
    name = obj.get_name().replace("_GEN_VARIABLE", "")
    if name in seen:
        continue
    seen.add(name)
    attach = None
    try:
        parent = obj.get_attach_parent()
        attach = parent.get_name().replace("_GEN_VARIABLE", "") if parent else None
    except Exception:  # noqa: BLE001
        pass
    if isinstance(obj, unreal.SkeletalMeshComponent):
        mesh = obj.get_editor_property("skeletal_mesh_asset")
        if mesh is None:
            continue
        p = _path(mesh)
        packages.add(p)
        if name == "Body":
            body = p
        mats = []
        try:
            mats = [_path(m) for m in (obj.get_editor_property("override_materials") or [])]
        except Exception:  # noqa: BLE001
            pass
        packages.update(m for m in mats if m)
        parts.append({"name": name, "kind": "mesh", "asset": p, "attach": attach, "materials": mats})
    elif type(obj).__name__ == "GroomComponent":
        groom = obj.get_editor_property("groom_asset")
        if groom is None:
            continue
        binding = obj.get_editor_property("binding_asset")
        gp, bp_ = _path(groom), _path(binding)
        packages.update(x for x in (gp, bp_) if x)
        parts.append({"name": name, "kind": "groom", "asset": gp, "binding": bp_, "attach": attach})

unreal.log(f"[FFMH] {BP}: body {body}, {len(parts)} parts, migrating {len(packages)} packages (+ dependencies)")
opts = unreal.MigrationOptions()
opts.set_editor_property("prompt", False)
opts.set_editor_property("ignore_dependencies", False)
opts.set_editor_property("asset_conflict", unreal.AssetMigrationConflict.SKIP)
unreal.AssetToolsHelpers.get_asset_tools().migrate_packages([unreal.Name(p) for p in sorted(packages)], FF_CONTENT, opts)
out = {"schema": "fourfold.metahuman/1", "name": BP.rsplit("/", 1)[1].replace("BP_", ""), "body": body, "parts": parts}
with open(os.path.join(FF_CONTENT, "Fourfold", "Data", "metahuman.json"), "w") as f:
    json.dump(out, f, indent=1)
unreal.log("[FFMH] done")
