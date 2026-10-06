# World stream - Unreal APIs relied on (and where they were verified)

Code: `Content/Python/fourfold/world/*.py`, `fourfold_setup.py`, `init_unreal.py` (editor Python); `Shaders/Env/FFEnv.ush` (HLSL); `Config/*.ini`.
**Verification levels**: *H* = name / signature / enum member found in the UE 5.8.2 public headers (a read-only header mirror made by the
`game` stream) - `python3 Tools/world/verify_props.py` re-checks every property name; *W* = Epic Python API page / forum / open-source
project found by web search (sources below); *D* = DXC-compiled here (`Tools/world/check_hlsl.py`); *?* = unverified, guarded by
try / except and reported in `setup_report.json` notes.  Every property write goes through `common.set_prop` (never raises).

## Level / actors (`level.py`)

| API | Used for | Verified |
|---|---|---|
| `unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)` `.new_level(path)`, `.load_level(path)`, `.save_current_level()`, `.build_light_maps(quality, with_reflection_captures)` | create / open / save the map, bake lighting | W: Epic Python API page `LevelEditorSubsystem` ("build_light_maps: Builds Light Maps and optionally the reflection captures", quality default Production); `new_level` + `save_current_level` in github.com/chengdagong/ue-mcp `tests/scripts/generate_test_level.py` |
| `unreal.LightingBuildQuality.QUALITY_PREVIEW / MEDIUM / HIGH / PRODUCTION` | bake quality | H: `ELightingBuildQuality` in `EngineTypes.h` |
| `unreal.get_editor_subsystem(unreal.EditorActorSubsystem)` `.spawn_actor_from_class(cls, Vector, Rotator)`, `.get_all_level_actors()`, `.destroy_actor(a)` | spawn / clean up actors | W: Epic page `EditorActorSubsystem`; ue-mcp example above (`spawn_actor_from_class(Class, Vector, Rotator)` then `set_actor_label`) |
| `unreal.Rotator(roll=, pitch=, yaw=)` (keywords: the positional order of the Python wrapper is roll, pitch, yaw) | all rotations | W (forum answers) |
| `actor.set_actor_label`, `.set_folder_path`, `.set_actor_scale3d`, `.set_editor_property("tags", [Name])`, `.get_component_by_class(cls)` | organisation, tags (`FourfoldArena`, `FFSolid_<name>`), scale | W: ue-mcp example (`set_actor_label`, `get_component_by_class`) |
| `StaticMeshActor.static_mesh_component`, `StaticMeshComponent.set_static_mesh`, `.set_mobility(unreal.ComponentMobility.STATIC / MOVABLE / STATIONARY)`, `.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)`, `.set_generate_overlap_events` | visual-only meshes (the sim is the collision) | W: ue-mcp example (`set_static_mesh`, `set_mobility`, `set_collision_enabled`) |
| Primitive props `cast_shadow`, `cast_dynamic_shadow`, `affect_distance_field_lighting`, `affect_dynamic_indirect_lighting` | baked shadows for static meshes, none for sky / ridges | H |
| `unreal.DirectionalLight` + `DirectionalLightComponent` props `dynamic_shadow_distance_stationary_light / movable_light`, `dynamic_shadow_cascades`, `cascade_distribution_exponent`, `cascade_transition_fraction`, `light_source_angle`, `atmosphere_sun_light`, `forward_shading_priority`, `use_inset_shadows_for_movable_objects`; base `intensity`, `light_color` (Color), `cast_shadows` | the sun (stationary, 3.2 lux) | H (`DirectionalLightComponent.h`, `LightComponentBase.h`) ; `intensity` / `light_color` / `atmosphere_sun_light` usage: W (ue-mcp example) |
| `unreal.SkyLight` + `SkyLightComponent`: `source_type` (`unreal.SkyLightSourceType.SLS_CAPTURED_SCENE`), `real_time_capture`, `lower_hemisphere_is_black`, `lower_hemisphere_color`, `cubemap_resolution`, `recapture_sky()` | sky light from the dome | H (`SkyLightComponent.h`; `recapture_sky` is a BlueprintCallable `RecaptureSky`) |
| `unreal.ExponentialHeightFog` + component: `fog_density`, `fog_height_falloff`, `start_distance`, `fog_max_opacity`, `fog_inscattering_luminance`, `directional_inscattering_luminance / exponent`, `enable_volumetric_fog` | fog (note: 5.x renamed `FogInscatteringColor` to `...Luminance`) | H |
| `unreal.PointLight` + `PointLightComponent`: `intensity_units` (`unreal.LightUnits.CANDELAS`), `intensity`, `attenuation_radius`, `use_inverse_squared_falloff`, `source_radius`, `cast_shadows` | lantern / brazier lights | H (`LocalLightComponent.h`: `IntensityUnits`, `Scene.h`: `ELightUnits`) |
| `unreal.SphereReflectionCapture` + `SphereReflectionCaptureComponent.influence_radius` | reflection captures | H |
| `unreal.PostProcessVolume`: `unbound`, `settings` (struct: write `override_<name>` + `<name>` then set back): `auto_exposure_method` (`unreal.AutoExposureMethod.AEM_BASIC`), `auto_exposure_min_brightness` / `max_brightness` / `bias`, `bloom_intensity`, `bloom_threshold`, `motion_blur_amount`, `lens_flare_intensity`, `vignette_intensity`, `ambient_occlusion_intensity`, `color_saturation`, `color_contrast` (`unreal.Vector4`), `scene_color_tint` | grading + FIXED exposure (EV100 -0.263 => scale 1.0) | H (`Scene.h`, `PostProcessVolume.h`); python struct copy semantics: W (Epic Python docs: structs are copied on get) |
| `unreal.LightmassImportanceVolume`, `unreal.LightmassCharacterIndirectDetailVolume` (+ `set_actor_scale3d`) | bake bounds / character indirect samples | ? (class names from the engine's volume classes; guarded with `getattr`) |
| `unreal.TargetPoint` | the `FourfoldArena` marker actor (has a scene root, unlike a bare `Actor`) | ? (plain `ATargetPoint`) |
| `unreal.UnrealEditorSubsystem.get_editor_world()` (fallback `EditorLevelLibrary.get_editor_world()`), `world.get_world_settings()`, `lightmass_settings` struct fields (`num_indirect_lighting_bounces`, `use_ambient_occlusion`, `max_occlusion_distance`, `fully_occluded_samples_fraction`, `direct/indirect_illumination_occlusion_fraction`, `occlusion_exponent`, `static_lighting_level_scale`) | Lightmass world settings | H (`WorldSettings.h`) for the fields; subsystem name W |

## Assets (`textures.py`, `meshes.py`, `materials.py`)

| API | Used for | Verified |
|---|---|---|
| `unreal.AssetImportTask` + `AssetToolsHelpers.get_asset_tools().import_asset_tasks` | PNG textures (factory chosen by extension) and FBX | W / ARCHITECTURE §12 (same calls as the character stream) |
| `Texture2D` props `srgb`, `compression_settings` (`TC_DEFAULT / TC_NORMALMAP / TC_MASKS`), `flip_green_channel`, `lod_group`, `max_texture_size`, `address_x / y` (`TA_CLAMP`), `mip_gen_settings`, `compression_no_alpha` | sRGB colour / DirectX normals / linear masks / clamped arena mask | H (`Texture.h`; note `CompressionNoAlpha`, not "compress_without_alpha") |
| Interchange: `InterchangeGenericAssetsPipeline` (`mesh_pipeline.{import_static_meshes, import_skeletal_meshes, combine_static_meshes, build_nanite, generate_lightmap_u_vs}`, `common_meshes_properties.{force_all_mesh_as_type (IFMT_STATIC_MESH), recompute_normals, recompute_tangents}`, `material_pipeline.{import_materials, texture_pipeline.import_textures}`), `InterchangePipelineStackOverride` (+ `add_pipeline`), `AssetImportTask.options = stack` | static mesh FBX import without materials | W: same pattern as `fourfold/character` (xavier150 / ArenaGame / crab-sim sources listed in docs/character/API_NOTES.md); the three static-mesh pipeline property names are unverified (?) - guarded |
| Fallback `unreal.FbxImportUI` (`mesh_type_to_import = FBXIT_STATIC_MESH`, `static_mesh_import_data.{combine_meshes, auto_generate_collision, generate_lightmap_u_vs, normal_import_method = FBXNIM_IMPORT_NORMALS}`) with `Interchange.FeatureFlags.Import.FBX 0` | classic importer | W (ARCHITECTURE §15) |
| `StaticMesh` props `light_map_resolution`, `light_map_coordinate_index`, `static_materials` (`StaticMaterial.material_slot_name / imported_material_slot_name / material_interface`), `get_bounds()` (`box_extent`, `origin`) | lightmap settings, slot -> material by FBX material name, size / mirror check | H (`StaticMesh.h`); W (Epic Python page StaticMesh: `get_bounds`) |
| `StaticMeshEditorSubsystem.remove_collisions(mesh)` / `EditorStaticMeshLibrary.remove_collisions` | drop generated collision | ? (both tried; components have collision disabled anyway) |
| `MaterialEditingLibrary`: `create_material_expression`, `connect_material_expressions`, `connect_material_property`, `get_material_expressions`, `delete_material_expression`, `recompile_material`, `get_statistics`, `set_material_instance_{parent,texture_parameter_value,scalar_parameter_value,vector_parameter_value}`, `update_material_instance` | master graphs + instances | W (same calls as the character stream; Epic MaterialEditingLibrary page) |
| Expression classes `MaterialExpression{TextureCoordinate, TextureSampleParameter2D, ScalarParameter, VectorParameter, Constant, Constant3Vector, Multiply, Max, LinearInterpolate, DotProduct, Time, WorldPosition, ObjectPositionWS, VertexColor, VertexNormalWS, CameraVectorWS, CollectionParameter, Custom}` and their props (`parameter_name`, `default_value`, `group`, `texture`, `sampler_type`, `sampler_source`, `coordinate_index`, `r`, `constant`, `collection`, `code`, `output_type`, `inputs`, `additional_outputs`, `include_file_paths`) | graph nodes | H (all headers in `Engine/Public/Materials/`) |
| Output pin names: texture sample `RGB / R / G / B / A`, vertex colour `R G B A`, parameters / custom `""` | linking | W (Python snippets use `'RGB'` for texture samples); `Graph.link/out` try several spellings and report `link failed` |
| Material props `blend_mode` (`BLEND_MASKED / BLEND_TRANSLUCENT`), `shading_model` (`MSM_UNLIT`), `two_sided`, `opacity_mask_clip_value`, `is_sky`, `translucency_lighting_mode` (`TLM_SURFACE_PER_PIXEL_LIGHTING`) | per master | H (`Material.h`, `EngineTypes.h` `ETranslucencyLightingMode`) |
| `MaterialParameterCollectionFactoryNew`, `MaterialParameterCollection.scalar_parameters` (`CollectionScalarParameter.parameter_name / default_value`) | `MPC_Arena` (Wetness) | ? guarded: without it the materials only use their instance `Wetness` |
| `SamplerSourceMode.SSM_WRAP_WORLD_GROUP_SETTINGS / SSM_FROM_TEXTURE_ASSET`, `MaterialSamplerType.SAMPLERTYPE_COLOR / NORMAL / MASKS` | shared wrap sampler (16-sampler limit), clamped arena mask | H / W |

## Orchestrator + menu

| API | Used for | Verified |
|---|---|---|
| `unreal.EditorLoadingAndSavingUtils.save_dirty_packages(save_map_packages, save_content_packages)`; fallback `EditorAssetLibrary.save_directory` | save after every part | W |
| `unreal.ScopedSlowTask(total, text)` (`make_dialog`, `enter_progress_frame`, `should_cancel`) | progress dialog | W (used manually as context manager; optional - skipped if it raises) |
| `unreal.ToolMenus.get()`, `.find_menu("LevelEditor.MainMenu")`, `ToolMenu.add_sub_menu(owner, section_name, name, label, tool_tip)`, `.add_section`, `.add_menu_entry(section, entry)`, `ToolMenuEntry(name=, type=unreal.MultiBlockType.MENU_ENTRY)`, `.set_label`, `.set_tool_tip`, `.set_string_command(unreal.ToolMenuStringCommandType.PYTHON, "", code)`, `ToolMenus.refresh_all_widgets()` | the "Fourfold" menu | W: Epic Python pages `ToolMenu` (`add_sub_menu`) and `ToolMenuEntry` (`set_string_command(type, custom_type, string)`), `ToolMenuStringCommandType.PYTHON` |
| `unreal.SystemLibrary.get_engine_version()`, `.launch_url(url)`, `.execute_console_command` | report header, "Open report" | W |

## HLSL (`Shaders/Env/FFEnv.ush`)

Plain functions, `#pragma once`, no wave ops / SM6-only intrinsics, no texture access; Custom-node outputs are assigned by name
(`OutRough = r;`) from local variables passed as `out` parameters, exactly like the character stream's `FFFighter.ush`.  Compiled with
Microsoft DXC (`ps_6_0` DXIL and `-spirv`) by `Tools/world/check_hlsl.py` for every Custom-node body in `materials.CUSTOM_BODIES` (D).
Unreal's own cross-compile for Metal first happens on the Mac.  `Time`, `WorldPosition` (cm) and `ObjectPositionWS` are fed through normal
material nodes (LWC-safe), never read as `Parameters.*` inside the Custom code.

## Config keys (`Config/*.ini`) - verified in `RendererSettings.h`, `IOSRuntimeSettings.h`, `InputSettings.h`, `Engine.h` (H); see README "Config review".

## Sources
* https://docs.unrealengine.com/5.1/en-US/PythonAPI/class/LevelEditorSubsystem.html (via web search summary), https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/LevelEditorSubsystem
* https://docs.unrealengine.com/5.0/en-US/PythonAPI/class/EditorActorSubsystem.html, https://dev.epicgames.com/documentation/unreal-engine/API/Editor/UnrealEd/UEditorActorSubsystem/SpawnActorFromClass
* https://github.com/chengdagong/ue-mcp (`tests/scripts/generate_test_level.py`: new_level, spawn_actor_from_class, set_actor_label, get_component_by_class, set_static_mesh, set_mobility, set_collision_enabled, light component properties, save_current_level)
* https://docs.unrealengine.com/5.0/en-US/PythonAPI/class/StaticMesh.html (`get_bounds`, `light_map_resolution`, `static_materials`), https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/PostProcessVolume
* https://docs.unrealengine.com/5.1/en-US/PythonAPI/class/ToolMenuEntry.html, https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/ToolMenu, https://docs.unrealengine.com/PythonAPI/class/ToolMenuStringCommand.html
* https://dev.epicgames.com/documentation/unreal-engine/mobile-rendering-and-shading-modes-for-unreal-engine (forward shading, `r.Mobile.ShadingPath`, MSAA), https://dev.epicgames.com/documentation/unreal-engine/using-modern-xcode-in-unreal-engine
* https://github.com/microsoft/DirectXShaderCompiler/releases (DXC used for the HLSL check)
