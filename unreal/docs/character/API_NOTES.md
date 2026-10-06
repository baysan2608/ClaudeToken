# Character stream - Unreal APIs relied on (and where they were verified)

All editor-Python calls live in `unreal/Content/Python/fourfold/character/` (`__init__.py`, `materials.py`). Every
property write goes through a `_set()` helper that logs instead of raising, so a renamed property shows up in
`setup_report.json` (`notes`) instead of aborting the import. If something fails on the Mac, the report line names
the property; fix it here.

## Import (FBX skeletal mesh + LODs, textures)

| API | Used for | Verified in |
|---|---|---|
| `unreal.AssetImportTask` (`filename`, `destination_path`, `destination_name`, `replace_existing`, `automated`, `save`, `options`, `imported_object_paths`) + `unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([...])` | textures and the skeletal mesh | ARCHITECTURE §12; github.com/alexrios/ArenaGame `Tools/RepairOACharacters.py`; github.com/owenpkent/crab-sim `Art/unreal/build_content.py` |
| `unreal.InterchangeGenericAssetsPipeline()`; `.asset_name`, `.use_source_name_for_asset`; `mesh_pipeline.{import_skeletal_meshes, import_static_meshes, create_physics_asset, import_morph_targets}`; `common_meshes_properties.{force_all_mesh_as_type, recompute_normals, recompute_tangents}`; `common_skeletal_meshes_and_animations_properties.import_only_animations`; `material_pipeline.import_materials`; `material_pipeline.texture_pipeline.import_textures`; `animation_pipeline.import_animations` | Interchange FBX import of `SK_Fighter.fbx` without materials / textures / animations, with a physics asset | ArenaGame `RepairOACharacters.py`, `ModernizeArenaRoster.py`; github.com/xavier150/Blender-For-UnrealEngine-Addons `bfu_import_module/import_module_tasks_class.py`, `asset_import.py`; Epic Python API pages (InterchangeGenericCommonSkeletalMeshesAndAnimationsProperties, InterchangeGenericMaterialPipeline) |
| `unreal.InterchangePipelineStackOverride()` + `.add_pipeline(p)` (5.3+), fallback `.override_pipelines.append(p)` | passing the pipeline to `AssetImportTask.options` | xavier150 `import_module_tasks_class.py` (both code paths) |
| `unreal.InterchangeForceMeshType.IFMT_SKELETAL_MESH` | force skeletal | xavier150 `asset_import.py` |
| Fallback: `unreal.FbxImportUI` (`import_mesh`, `import_as_skeletal`, `import_materials`, `import_textures`, `import_animations`, `create_physics_asset`, `mesh_type_to_import`, `skeletal_mesh_import_data.{import_morph_targets, normal_import_method}`), `unreal.FBXImportType.FBXIT_SKELETAL_MESH`, `unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS_AND_TANGENTS`, console `Interchange.FeatureFlags.Import.FBX 0/1` via `unreal.SystemLibrary.execute_console_command(None, ...)` | only if Interchange produced no `SK_Fighter` | ARCHITECTURE §12/§15; crab-sim `fbx_ui()`; xavier150 `asset_import.py` |
| `unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem)` `.get_lod_count(mesh)`, `.import_lod(mesh, index, fbx_path)` (returns -1 on failure) | LOD1 / LOD2 from `SK_Fighter_LOD1.fbx`, `SK_Fighter_LOD2.fbx` | Epic's own github.com/EpicGames/BlenderTools `send2ue/dependencies/unreal.py` (`import_skeletal_mesh_lod`) |
| `SkeletalMesh.lod_info` (array of `SkeletalMeshLODInfo`), `SkeletalMeshLODInfo.screen_size` = `unreal.PerPlatformFloat()` with `.default` | LOD screen sizes 1.0 / 0.30 / 0.12 | github.com/wevet/UnrealEngine_ThirdPerson `generate_skeletalmesh_lod.py`; github.com/indik47/fbt `skeletal_mesh_lod_ops.py` |
| `SkeletalMesh.skeleton`, `.physics_asset`, `.materials` (array of `SkeletalMaterial`: `material_slot_name`, `imported_material_slot_name`, `material_interface`) | rename to SKEL_Fighter / PA_Fighter, assign MI_Fighter_<slot> by slot name | ArenaGame `RepairOACharacters.py` |
| `unreal.EditorAssetLibrary` `.does_asset_exist`, `.does_directory_exist`, `.make_directory`, `.rename_asset(src, dst)`, `.save_loaded_asset(asset, only_if_is_dirty)`, `.save_directory(path, only_if_is_dirty, recursive)`, `unreal.load_asset` | asset plumbing | ARCHITECTURE §12; MobMaterials `author_surface.py`; crab-sim |
| `Texture2D.srgb`, `.compression_settings` (`unreal.TextureCompressionSettings.TC_DEFAULT / TC_NORMALMAP / TC_MASKS`), `.flip_green_channel`, `.lod_group` (`unreal.TextureGroup.TEXTUREGROUP_CHARACTER / _CHARACTER_NORMAL_MAP / _CHARACTER_SPECULAR`) | BC sRGB; N linear + normal compression (already DirectX, no flip); ORM / noise linear masks | crab-sim `build_content.py` (same three presets) |
| `unreal.Paths.project_dir()`, `.convert_relative_path_to_full()` | locate `SourceArt/Character` | ARCHITECTURE §12 / py_mock |

## Materials

| API | Used for | Verified in |
|---|---|---|
| `create_asset(name, path, unreal.Material, unreal.MaterialFactoryNew())`, `create_asset(..., unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())` | masters + instances | ARCHITECTURE §12; github.com/Vaei/MobMaterials `Python/author_surface.py` |
| `MaterialEditingLibrary` `.create_material_expression(mat, cls, x, y)`, `.connect_material_expressions(a, out, b, in)`, `.connect_material_property(a, out, unreal.MaterialProperty.MP_*)`, `.get_material_expressions`, `.delete_material_expression`, `.recompile_material`, `.set_material_instance_parent`, `.set_material_instance_{texture,scalar,vector}_parameter_value`, `.update_material_instance` | graph building, instances | MobMaterials `author_surface.py`; crab-sim |
| Material props `blend_mode` (`BlendMode.BLEND_OPAQUE`), `shading_model` (`MaterialShadingModel.MSM_DEFAULT_LIT`), `two_sided`, `used_with_skeletal_mesh` | per master | MobMaterials (`used_with_*`, two_sided, shading model) |
| Expressions: `MaterialExpressionTextureCoordinate(coordinate_index)`, `MaterialExpressionTextureSampleParameter2D(parameter_name, texture, sampler_type, sampler_source, group)` with pins `UVs` / outputs `RGB R G B A`, `MaterialExpressionScalarParameter(parameter_name, default_value, group)`, `MaterialExpressionVectorParameter(... unreal.LinearColor)`, `MaterialExpressionMultiply/Add` (`A`,`B`), `MaterialExpressionConstant(r)`, `MaterialExpressionFresnel(exponent, base_reflect_fraction)`, `MaterialExpressionTime` | graph | MobMaterials, crab-sim |
| `MaterialExpressionCustom` (`code`, `output_type` = `CustomMaterialOutputType.CMOT_FLOAT1/3`, `description`, `inputs` = list of `unreal.CustomInput()` with `input_name`, `additional_outputs` = list of `unreal.CustomOutput()` with `output_name` / `output_type`, `include_file_paths`) | HLSL helpers from `/Fourfold/Character/FFFighter.ush` | ARCHITECTURE §12; MobMaterials `custom()` (structs have no constructor kwargs) |
| `MaterialSamplerType.SAMPLERTYPE_COLOR / NORMAL / MASKS`, `SamplerSourceMode.SSM_WRAP_WORLD_GROUP_SETTINGS` | sampler types; shared wrap sampler (ES3.1 16-sampler limit) | MobMaterials `_fn_sample()` |

## HLSL (`unreal/Shaders/Character/FFFighter.ush`)
Plain functions (no wave ops / SM6 features), `#pragma once`, included from Custom nodes; Custom-node additional
outputs are assigned by name in the node code (`OutRough = r; OutEmissive = e;`). The `/Fourfold` virtual path is
registered by the fx stream's `FourfoldShaders` module (ARCHITECTURE §3 / §15). Compiled here with Microsoft DXC
(release `linux_dxc_2024_07_31`, DXIL `ps_6_0` and `-spirv`) through `Tools/blender/character/check_hlsl.py`, which wraps
every helper the way the Custom nodes call it (`STATUS_CODE` from `materials.py` pasted verbatim, additional outputs as
`inout`). Unreal's own cross-compile for Metal (ES3.1 / SM5) first happens on the Mac.

## Sources
* https://github.com/EpicGames/BlenderTools (send2ue `dependencies/unreal.py`)
* https://github.com/xavier150/Blender-For-UnrealEngine-Addons (`bfu_import_module/*`)
* https://github.com/alexrios/ArenaGame (`Tools/RepairOACharacters.py`, `Tools/ModernizeArenaRoster.py`)
* https://github.com/Vaei/MobMaterials (`Python/author_surface.py`)
* https://github.com/owenpkent/crab-sim (`Art/unreal/build_content.py`)
* https://github.com/wevet/UnrealEngine_ThirdPerson, https://github.com/indik47/fbt (LOD info / screen sizes)
* Epic Python API index pages for SkeletalMeshEditorSubsystem, InterchangeGenericAssetsPipeline,
  InterchangeGenericCommonSkeletalMeshesAndAnimationsProperties, InterchangeGenericMaterialPipeline (via web search
  summaries; dev.epicgames.com is not fetchable from the build container)
