"""Fourfold animation import (stream `animation`): every SourceArt/Animation/A_<clip>.fbx -> an AnimSequence on
SKEL_Fighter.

    import fourfold.animation as fa; fa.build_all(force=False)   -> report dict

Contract (ARCHITECTURE.md §8.3 / §12):
  * skeleton  /Game/Fourfold/Characters/Fighter/SKEL_Fighter  (made by fourfold.character, which runs first; when it
    is missing nothing is imported and the report says so);
  * output    /Game/Fourfold/Characters/Fighter/Anims/A_<clip>  (UAnimSequence, animation only, no root motion,
    root locked, looping flag from Content/Fourfold/Data/clips.json, the engine's default ACL bone compression,
    frame stripping off so 2-frame contact holds survive on mobile);
  * idempotent: an existing A_<clip> is skipped unless force=True; never raises (failures go to the report).

Import path: Interchange (UE 5.5+ default for FBX) with an animation-only InterchangeGenericAssetsPipeline bound to the
skeleton, into a per-clip scratch folder; the AnimSequence it produces (Interchange names it after the take, e.g.
"Anim_0_Root" / "root_A_<clip>") is renamed to A_<clip>; anything else the import created (a skeleton, a mesh, a
morph-anim stub) is deleted with the scratch folder.  Fallback: the legacy FBX importer (FbxImportUI, FBXIT_ANIMATION)
with Interchange FBX switched off for the duration.  APIs and their sources: unreal/docs/animation/API_NOTES.md.
"""
import json
import os
import traceback

import unreal

ROOT = "/Game/Fourfold/Characters/Fighter"
SKELETON = ROOT + "/SKEL_Fighter"
MESH = ROOT + "/SK_Fighter"
ANIM_DIR = ROOT + "/Anims"
SCRATCH = ANIM_DIR + "/_Import"
COMPRESSION = "/Engine/Animation/DefaultAnimBoneCompressionSettings"
FPS = 60
EAL = unreal.EditorAssetLibrary


def _log(msg):
    unreal.log(f"[Fourfold][animation] {msg}")


def _err(msg):
    unreal.log_error(f"[Fourfold][animation] {msg}")


def _set(obj, props, value, notes, what=""):
    """Sets the first property name of `props` that the object accepts; logs instead of raising."""
    if isinstance(props, str):
        props = [props]
    last = None
    for p in props:
        try:
            obj.set_editor_property(p, value)
            return True
        except Exception as e:  # noqa: BLE001
            last = e
    notes.append(f"could not set {what or ''}{'/'.join(props)}: {last}")
    return False


def _get(obj, prop, notes=None):
    try:
        return obj.get_editor_property(prop)
    except Exception as e:  # noqa: BLE001
        if notes is not None:
            notes.append(f"could not read {prop}: {e}")
        return None


def _paths():
    proj = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
    return (os.path.join(proj, "SourceArt", "Animation"),
            os.path.join(proj, "Content", "Fourfold", "Data", "clips.json"))


def _load_clips(path, report):
    try:
        with open(path, encoding="utf-8") as f:
            doc = json.load(f)
        return doc.get("clips", {})
    except Exception as e:  # noqa: BLE001
        report["notes"].append(f"clips.json not readable ({e}); importing every FBX found, loop flags off")
        return None


def _asset_path(name):
    return f"{ANIM_DIR}/{name}"


# ------------------------------------------------------------------------------------------------ import paths
def _pipeline(skel, notes):
    pipe = unreal.InterchangeGenericAssetsPipeline()
    _set(pipe, "use_source_name_for_asset", True, notes, "pipeline.")
    common = _get(pipe, "common_skeletal_meshes_and_animations_properties", notes)
    if common is not None:
        _set(common, "import_only_animations", True, notes, "common.")
        _set(common, "skeleton", skel, notes, "common.")
    mesh = _get(pipe, "mesh_pipeline", notes)
    if mesh is not None:
        _set(mesh, "import_skeletal_meshes", False, notes, "mesh.")
        _set(mesh, "import_static_meshes", False, notes, "mesh.")
        _set(mesh, "create_physics_asset", False, notes, "mesh.")
    mat = _get(pipe, "material_pipeline", notes)
    if mat is not None:
        _set(mat, "import_materials", False, notes, "material.")
        tex = _get(mat, "texture_pipeline", notes)
        if tex is not None:
            _set(tex, "import_textures", False, notes, "texture.")
    anim = _get(pipe, "animation_pipeline", notes)
    if anim is not None:
        _set(anim, "import_animations", True, notes, "animation.")
        _set(anim, "import_bone_tracks", True, notes, "animation.")
        # sample the file at its own 60 fps (never the 30 Hz bake), whole exported range
        _set(anim, ["use30_hz_to_bake_bone_animation", "use30hz_to_bake_bone_animation"], False, notes, "animation.")
        _set(anim, "custom_bone_animation_sample_rate", FPS, notes, "animation.")
        _set(anim, "snap_to_closest_frame_boundary", True, notes, "animation.")
        try:
            _set(anim, "animation_range", unreal.InterchangeAnimationRange.TIMELINE, notes, "animation.")
        except Exception as e:  # noqa: BLE001
            notes.append(f"InterchangeAnimationRange: {e}")
        _set(anim, "do_not_import_curve_with_zero", True, notes, "animation.")
    return pipe


def _task(fbx, dest_dir, options):
    t = unreal.AssetImportTask()
    t.set_editor_property("filename", fbx)
    t.set_editor_property("destination_path", dest_dir)
    t.set_editor_property("replace_existing", True)
    t.set_editor_property("automated", True)
    t.set_editor_property("save", False)
    t.set_editor_property("options", options)
    return t


def _import_interchange(fbx, dest_dir, skel, notes):
    pipe = _pipeline(skel, notes)
    stack = unreal.InterchangePipelineStackOverride()
    if hasattr(stack, "add_pipeline"):
        stack.add_pipeline(pipe)
    else:
        stack.get_editor_property("override_pipelines").append(pipe)
    t = _task(fbx, dest_dir, stack)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([t])
    return list(t.get_editor_property("imported_object_paths") or [])


def _import_legacy(fbx, dest_dir, skel, notes):
    """Classic FBX importer, animation only, Interchange FBX switched off for the duration."""
    unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX 0")
    try:
        ui = unreal.FbxImportUI()
        _set(ui, "import_mesh", False, notes, "fbx.")
        _set(ui, "import_as_skeletal", True, notes, "fbx.")
        _set(ui, "import_animations", True, notes, "fbx.")
        _set(ui, "import_materials", False, notes, "fbx.")
        _set(ui, "import_textures", False, notes, "fbx.")
        _set(ui, "create_physics_asset", False, notes, "fbx.")
        _set(ui, "skeleton", skel, notes, "fbx.")
        _set(ui, ["mesh_type_to_import", "original_import_type"], unreal.FBXImportType.FBXIT_ANIMATION, notes, "fbx.")
        ad = _get(ui, "anim_sequence_import_data", notes)
        if ad is not None:
            _set(ad, "import_bone_tracks", True, notes, "fbx.anim.")
            _set(ad, "animation_length", unreal.FBXAnimationLengthImportType.FBXALIT_EXPORTED_TIME, notes, "fbx.anim.")
            _set(ad, "use_default_sample_rate", False, notes, "fbx.anim.")
            _set(ad, "custom_sample_rate", FPS, notes, "fbx.anim.")
            _set(ad, "snap_to_closest_frame_boundary", True, notes, "fbx.anim.")
            _set(ad, "remove_redundant_keys", False, notes, "fbx.anim.")
            _set(ad, "do_not_import_curve_with_zero", True, notes, "fbx.anim.")
        t = _task(fbx, dest_dir, ui)
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([t])
        return list(t.get_editor_property("imported_object_paths") or [])
    finally:
        unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX 1")


def _assets_in(folder):
    out = []
    try:
        for p in EAL.list_assets(folder, recursive=True, include_folder=False) or []:
            a = unreal.load_asset(str(p).split(".")[0])
            if a is not None:
                out.append(a)
    except Exception:  # noqa: BLE001
        pass
    return out


def _pick_sequence(assets, clip):
    """The imported AnimSequence of this clip: prefer one named after the clip / action, else the longest."""
    seqs = [a for a in assets if isinstance(a, unreal.AnimSequence)]
    if not seqs:
        return None
    want = ("A_" + clip).lower()

    def score(a):
        n = a.get_name().lower()
        try:
            length = float(a.get_play_length())
        except Exception:  # noqa: BLE001
            length = 0.0
        return (want in n, "morph" not in n, length)
    return sorted(seqs, key=score, reverse=True)[0]


def _configure(seq, entry, skel, compression, mesh, notes):
    loop = bool(entry.get("loop", False)) if entry else False
    _set(seq, "loop", loop, notes, "anim.")
    _set(seq, "enable_root_motion", False, notes, "anim.")
    _set(seq, "force_root_lock", True, notes, "anim.")
    _set(seq, "allow_frame_stripping", False, notes, "anim.")
    if compression is not None:
        _set(seq, "bone_compression_settings", compression, notes, "anim.")
    if mesh is not None:
        try:
            seq.set_preview_skeletal_mesh(mesh)
        except Exception as e:  # noqa: BLE001
            notes.append(f"set_preview_skeletal_mesh: {e}")
    s = _get(seq, "skeleton", notes)
    if s is not None and skel is not None and s.get_path_name() != skel.get_path_name():
        notes.append(f"{seq.get_name()}: bound to {s.get_path_name()} instead of {skel.get_path_name()}")
        return False
    return True


def _save(asset):
    try:
        EAL.save_loaded_asset(asset, False)
    except Exception:  # noqa: BLE001
        EAL.save_loaded_asset(asset)


def import_clip(clip, fbx, entry, skel, compression, mesh, force, report):
    dst = _asset_path("A_" + clip)
    if EAL.does_asset_exist(dst):
        if not force:
            report["skipped"].append(dst)
            return
        EAL.delete_asset(dst)
    scratch = f"{SCRATCH}/{clip}"
    if EAL.does_directory_exist(scratch):
        EAL.delete_directory(scratch)
    EAL.make_directory(scratch)
    notes = []
    try:
        seq = None
        try:
            _import_interchange(fbx, scratch, skel, notes)
            seq = _pick_sequence(_assets_in(scratch), clip)
        except Exception as e:  # noqa: BLE001
            notes.append(f"Interchange import raised ({e})")
        if seq is None:
            notes.append("Interchange produced no AnimSequence; trying the legacy FBX importer")
            _import_legacy(fbx, scratch, skel, notes)
            seq = _pick_sequence(_assets_in(scratch), clip)
        if seq is None:
            report["failed"].append({"item": dst, "error": "no AnimSequence after import; " + "; ".join(notes[-6:])})
            return
        src = seq.get_path_name().split(".")[0]
        if not EAL.rename_asset(src, dst):
            report["failed"].append({"item": dst, "error": f"rename {src} -> {dst} failed"})
            return
        seq = unreal.load_asset(dst)
        ok = _configure(seq, entry, skel, compression, mesh, notes)
        _save(seq)
        if ok:
            report["created"].append(dst)
        else:
            report["failed"].append({"item": dst, "error": "; ".join(notes[-3:])})
    except Exception as e:  # noqa: BLE001
        report["failed"].append({"item": dst, "error": f"{e}\n{traceback.format_exc()}"})
    finally:
        try:
            if EAL.does_directory_exist(scratch):
                EAL.delete_directory(scratch)
        except Exception as e:  # noqa: BLE001
            notes.append(f"scratch cleanup: {e}")
        for n in notes:
            if n not in report["notes"]:
                report["notes"].append(f"A_{clip}: {n}")


def build_all(force=False):
    report = {"created": [], "skipped": [], "failed": [], "notes": []}
    try:
        art, data = _paths()
        if not EAL.does_asset_exist(SKELETON):
            report["failed"].append({"item": SKELETON, "error": "SKEL_Fighter is missing: run fourfold.character first "
                                                                "(it imports SK_Fighter and its skeleton)"})
            _err("SKEL_Fighter missing - nothing imported")
            return report
        skel = unreal.load_asset(SKELETON)
        if skel is None or not isinstance(skel, unreal.Skeleton):
            report["failed"].append({"item": SKELETON, "error": "SKEL_Fighter is not a Skeleton asset"})
            return report
        mesh = unreal.load_asset(MESH) if EAL.does_asset_exist(MESH) else None
        compression = unreal.load_asset(COMPRESSION) if EAL.does_asset_exist(COMPRESSION) else None
        if compression is None:
            report["notes"].append(f"{COMPRESSION} not found: keeping the project default bone compression")
        clips = _load_clips(data, report)
        if clips is None:
            names = sorted(f[2:-4] for f in os.listdir(art) if f.startswith("A_") and f.endswith(".fbx"))
            clips = {n: {} for n in names}
        if not EAL.does_directory_exist(ANIM_DIR):
            EAL.make_directory(ANIM_DIR)
        for clip in sorted(clips):
            entry = clips[clip]
            fbx = os.path.join(art, (entry.get("asset") or "A_" + clip) + ".fbx")
            if not os.path.exists(fbx):
                report["failed"].append({"item": _asset_path("A_" + clip), "error": f"missing source {fbx}"})
                continue
            import_clip(clip, fbx, entry, skel, compression, mesh, force, report)
        try:
            if EAL.does_directory_exist(SCRATCH):
                EAL.delete_directory(SCRATCH)
        except Exception:  # noqa: BLE001
            pass
        _log(f"created {len(report['created'])}, skipped {len(report['skipped'])}, failed {len(report['failed'])}")
    except Exception as e:  # noqa: BLE001
        report["failed"].append({"item": "fourfold.animation", "error": f"{e}\n{traceback.format_exc()}"})
        _err(str(e))
    return report


if __name__ == "__main__":
    print(json.dumps(build_all(), indent=1, default=str)[:4000])
