"""Run inside the Game Animation Sample project (UE 5.8) to copy the mocap Fourfold uses into the Fourfold project.

  UnrealEditor-Cmd "<GASP>/GameAnimationSample.uproject" -run=pythonscript -script="<this file>" -unattended

Copies (with every dependency, same /Game paths) the UEFN mannequin mesh + IK rig and the locomotion, jump and ragdoll
get-up animation folders into <Fourfold>/unreal/Content. Those folders are git-ignored (Epic sample content, large);
re-run this after a fresh clone. The retarget onto the Fourfold fighter is done in the Fourfold project
(Content/Python/fourfold/animation/mocap.py).
"""
import os

import unreal

FOURFOLD_CONTENT = os.environ.get("FF_CONTENT", os.path.expanduser("~/Fourfold/unreal/Content"))
ROOT = "/Game/Characters/UEFN_Mannequin"
FOLDERS = ["Animations/Idle", "Animations/Walk", "Animations/Run", "Animations/Sprint", "Animations/Jump",
           "Animations/Ragdoll"]
SINGLE = ["Meshes/SKM_UEFN_Mannequin", "Meshes/SK_UEFN_Mannequin", "Rigs/IK_UEFN_Mannequin", "Rigs/PA_UEFN_Mannequin"]

reg = unreal.AssetRegistryHelpers.get_asset_registry()
packages = [f"{ROOT}/{s}" for s in SINGLE]
for folder in FOLDERS:
    for ad in reg.get_assets_by_path(f"{ROOT}/{folder}", recursive=True):
        packages.append(str(ad.package_name))
packages = sorted(set(packages))
unreal.log(f"[FFGASP] migrating {len(packages)} packages (+ dependencies) to {FOURFOLD_CONTENT}")
opts = unreal.MigrationOptions()
opts.set_editor_property("prompt", False)
opts.set_editor_property("ignore_dependencies", False)
opts.set_editor_property("asset_conflict", unreal.AssetMigrationConflict.SKIP)
unreal.AssetToolsHelpers.get_asset_tools().migrate_packages([unreal.Name(p) for p in packages], FOURFOLD_CONTENT, opts)
unreal.log("[FFGASP] migration done")
