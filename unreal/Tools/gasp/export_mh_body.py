"""Run in the Fourfold project (UE 5.8): exports the fighters' complete MetaHuman body (SKM_FF_Body, made by setup part
`metahuman`) with its skeleton to SourceArt/Character/MetaHuman/SKM_FF_Body.fbx - the input of the training-pants generator
(Tools/blender/character/mh_outfit.py). The FBX is Epic sample content and stays git-ignored.

  UnrealEditor-Cmd unreal/Fourfold.uproject -run=pythonscript -script=unreal/Tools/gasp/export_mh_body.py -unattended
"""
import os

import unreal

SRC = "/Game/Fourfold/Characters/MetaHuman/SKM_FF_Body"
OUT = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()), "SourceArt", "Character", "MetaHuman",
                   "SKM_FF_Body.fbx")

mesh = unreal.load_asset(SRC)
if mesh is None:
    raise RuntimeError(f"{SRC} missing: run setup part metahuman first")
os.makedirs(os.path.dirname(OUT), exist_ok=True)
opts = unreal.FbxExportOption()
opts.set_editor_property("export_morph_targets", False)
opts.set_editor_property("level_of_detail", False)
opts.set_editor_property("collision", False)
opts.set_editor_property("vertex_color", False)
task = unreal.AssetExportTask()
task.set_editor_property("object", mesh)
task.set_editor_property("filename", OUT)
task.set_editor_property("automated", True)
task.set_editor_property("prompt", False)
task.set_editor_property("replace_identical", True)
task.set_editor_property("options", opts)
ok = unreal.Exporter.run_asset_export_task(task)
unreal.log(f"[FFEXPORT] {SRC} -> {OUT}: {'ok' if ok else 'FAILED'} ({os.path.getsize(OUT) if os.path.exists(OUT) else 0} bytes)")
