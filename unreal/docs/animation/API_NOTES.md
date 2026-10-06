# Animation stream - Unreal APIs relied on

Only one Unreal-facing file: `Content/Python/fourfold/animation/__init__.py` (editor Python, run by the owner through
`fourfold_setup.py` or `import fourfold.animation as fa; fa.build_all()`). Every property write goes through `_set()`,
which tries the listed names and logs instead of raising, so a renamed property shows up in the report `notes`
(`Saved/Fourfold/setup_report.json`) instead of aborting the import. Nothing else in this stream touches Unreal: the
runtime that plays the clips is the `game` stream's native anim instance (ARCHITECTURE §8.4).

## Import

| API | Used for | Verified in |
|---|---|---|
| `unreal.AssetImportTask` (`filename`, `destination_path`, `replace_existing`, `automated`, `save`, `options`, `imported_object_paths`) + `unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([t])` | one import task per FBX, into a scratch folder `Anims/_Import/<clip>` | ARCHITECTURE §12; character stream API_NOTES; github.com/xavier150/Blender-For-UnrealEngine-Addons `bfu_import_module/import_module_tasks_class.py` |
| `unreal.InterchangeGenericAssetsPipeline()` with `use_source_name_for_asset`; `common_skeletal_meshes_and_animations_properties.{import_only_animations, skeleton}`; `mesh_pipeline.{import_skeletal_meshes, import_static_meshes, create_physics_asset}`; `material_pipeline.import_materials`, `material_pipeline.texture_pipeline.import_textures` | animation-only Interchange import bound to `SKEL_Fighter` | Epic Python API pages InterchangeGenericCommonSkeletalMeshesAndAnimationsProperties ("import_only_animations ... you must also set a valid skeleton"); xavier150 `asset_import.py`; github.com/ChristianVerghis/Fourfold `Tools/import_cmu2.py` (same animation-only pattern) |
| `animation_pipeline.{import_animations, import_bone_tracks, custom_bone_animation_sample_rate, snap_to_closest_frame_boundary, animation_range, do_not_import_curve_with_zero}` and `use30_hz_to_bake_bone_animation` (fallback spelling `use30hz_to_bake_bone_animation`), `unreal.InterchangeAnimationRange.TIMELINE` | sample at the file's 60 fps over the exported range (never the 30 Hz bake) | Epic Python API page InterchangeGenericAnimationPipeline (web-search summary: `animation_range`, `frame_import_range`, `use30_hz_to_bake_bone_animation`, `snap_to_closest_frame_boundary`, `custom_bone_animation_sample_rate` = 0 auto); InterchangeAnimationRange enum TIMELINE / ANIMATED / SET_RANGE; xavier150 `bfu_import_animations_utils.py`; github.com/scenario-labs/skills `ue_anim.py` (5.8) |
| `unreal.InterchangePipelineStackOverride()` + `.add_pipeline(p)` (fallback `.override_pipelines.append(p)`) | passes the pipeline as `AssetImportTask.options` | xavier150 `import_module_tasks_class.py` (both code paths) |
| Legacy fallback: `unreal.FbxImportUI` (`import_mesh`, `import_as_skeletal`, `import_animations`, `import_materials`, `import_textures`, `create_physics_asset`, `skeleton`, `mesh_type_to_import` / `original_import_type` = `unreal.FBXImportType.FBXIT_ANIMATION`, `anim_sequence_import_data.{import_bone_tracks, animation_length = unreal.FBXAnimationLengthImportType.FBXALIT_EXPORTED_TIME, use_default_sample_rate, custom_sample_rate, snap_to_closest_frame_boundary, remove_redundant_keys, do_not_import_curve_with_zero}`) with `unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX 0/1")` | only when Interchange produced no AnimSequence | ARCHITECTURE §12 / §15; xavier150 `asset_import.py` (FBXIT_ANIMATION, `original_import_type`); scenario-labs `ue_anim.py` `_legacy_fbx_options` |
| Interchange naming quirk: an FBX animation import creates `Anim_0_Root` (the take) and a `Root_MorphAnim_0` stub | the script picks the AnimSequence named after the clip, else the longest non-morph one, renames it to `A_<clip>` and deletes the scratch folder | xavier150 `bfu_import_animations_utils.py` (`apply_interchange_post_import`, comment on 5.x behaviour) |

## AnimSequence settings

| API | Value | Verified in |
|---|---|---|
| `AnimSequence.loop` (bool) | from `clips.json` `loop` | Epic Python API AnimSequence ("loop: default looping behavior") |
| `AnimSequence.enable_root_motion` | False (the sim moves the fighter) | Epic Python API AnimSequence |
| `AnimSequence.force_root_lock` | True | Epic Python API AnimSequence ("Force Root Bone Lock even if Root Motion is not enabled") |
| `AnimSequence.allow_frame_stripping` | False: mobile platforms may strip every other frame; our 2-frame contact holds and 60 Hz contact sync need every frame | Epic Python API AnimSequence ("Can be disabled if animation has high frequency movements that are being lost") |
| `AnimSequence.bone_compression_settings` = `/Engine/Animation/DefaultAnimBoneCompressionSettings` (ACL in UE 5) | explicit default (good size / speed on iOS) | Epic Python API AnimSequence `bone_compression_settings`; engine content path is the UE 5 default asset (left unchanged when missing) |
| `AnimationAsset.set_preview_skeletal_mesh(mesh)` | preview with `SK_Fighter` | xavier150 `import_module_post_treatment.set_sequence_preview_skeletal_mesh` |
| `AnimSequence.skeleton` (read) | must be `SKEL_Fighter`, otherwise reported as failed | Epic Python API AnimationAsset |

## Asset plumbing
`unreal.EditorAssetLibrary.{does_asset_exist, does_directory_exist, make_directory, delete_directory, delete_asset,
list_assets, rename_asset, save_loaded_asset}`, `unreal.load_asset`, `unreal.Paths.{project_dir,
convert_relative_path_to_full}`, `unreal.log / log_error` - ARCHITECTURE §12, character stream API_NOTES.

## If something fails on the Mac
* `notes` lists every property that could not be set: rename it in `_pipeline()` / `_configure()`.
* `failed` with "no AnimSequence after import": open `Anims/_Import/<clip>` before it is cleaned up (comment out the
  `finally` cleanup) and check what Interchange produced; the legacy importer runs automatically as a second try.
* An AnimSequence bound to another skeleton (Interchange in 5.1-5.4 sometimes created a new one) is reported; the bone
  names of every clip are identical to `SKEL_Fighter` (validated here on re-import), so re-running with `force=True`
  after fixing the pipeline is safe.

## Sources
* https://github.com/xavier150/Blender-For-UnrealEngine-Addons (`bfu_import_module/asset_import.py`,
  `import_module_tasks_class.py`, `bfu_import_animations/bfu_import_animations_utils.py`)
* https://github.com/scenario-labs/skills (`skills/game-engines/unreal/scenario-unreal-animation/scripts/ue_anim.py`)
* https://github.com/ChristianVerghis/Fourfold (`Tools/import_cmu2.py`)
* Epic Python API pages (through web-search summaries; dev.epicgames.com / docs.unrealengine.com are not fetchable
  from the build container): InterchangeGenericAnimationPipeline, InterchangeAnimationRange,
  InterchangeGenericCommonSkeletalMeshesAndAnimationsProperties, AnimSequence.
