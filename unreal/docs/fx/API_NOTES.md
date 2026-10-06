# Stream `fx` - Unreal API notes

Every Unreal API the FX stream relies on, with where it was verified. The code was written without Unreal on the
machine.

**How it was verified**
* **C++** (`Source/FourfoldFX`): every UE source file and one unity TU of the module were syntax-checked with
  `clang++ -std=c++20 -fsyntax-only` against the public headers of a **UE 5.8.2** source mirror
  (github.com/AFIshInWater/UE5.8, sparse checkout, prepared by the `game` stream) plus the ProceduralMeshComponent
  plugin sources of the same mirror, with mock UHT headers (`Tools/vfx/ue_check/check_fx.sh`): 0 diagnostics in
  `Source/FourfoldFX`; a negative control (bogus member call) is reported. Link errors and UHT rules are not covered.
* **Editor Python** (`Content/Python/fourfold/fx`): every class / property / enum name below was read in the 5.8.2
  headers of the same mirror (Python names are the snake_case of the UPROPERTY names; enum members are upper-cased,
  e.g. `TLM_SurfacePerPixelLighting` -> `TLM_SURFACE_PER_PIXEL_LIGHTING`). Dry runs here: the shared mock
  (`Tools/py_mock/run_with_mock_unreal.py ... --call fourfold.fx:build_all`) and the stateful mock
  `Tools/vfx/py_mock_fx.py` (links, custom inputs / outputs, idempotence). Version-drifting names are resolved
  defensively (`_cls` / `_enum` fallbacks, `_set` never raises and reports).
* **HLSL** (`Shaders/Common`, `Shaders/FX`): every Custom node of `spec.py` is wrapped like Unreal's generated
  `CustomExpressionN` and compiled with Microsoft DXC (vs/ps_6_0 DXIL and SPIR-V, HLSL 2018 and 2021):
  `Tools/vfx/shader_check/check_shaders.py` (116 compiles, 0 errors / warnings). Unreal's own Metal cross-compile
  (DXC -> SPIR-V -> Metal) first runs on the Mac.

## C++ (FourfoldFX module)

| API | Header (5.8.2) | Notes |
|---|---|---|
| `UWorldSubsystem` (`Initialize`, `Deinitialize`, `OnWorldBeginPlay`, `DoesSupportWorldType`) | Engine/Public/Subsystems/WorldSubsystem.h | `EWorldType::Game / PIE` only |
| `UFourfoldSimSubsystem::OnFrame / OnScenarioLoaded`, `FFourfoldFrame`, `UFourfoldSettingsSubsystem::GetEffectiveQuality`, `AFourfoldFighter::GetBodyMesh / GetBoneLocation` | Source/Fourfold/Public (FROZEN, stream `game`) | read-only consumer |
| `UProceduralMeshComponent::CreateMeshSection_LinearColor(Section, Vertices, Triangles, Normals, UV0, UV1, UV2, UV3, Colors, Tangents, bCreateCollision, bSRGBConversion)`, `UpdateMeshSection_LinearColor(Section, Vertices, Normals, UV0, UV1, UV2, UV3, Colors, Tangents, bSRGBConversion)`, `ClearMeshSection`, `bUseAsyncCooking` | Plugins/Runtime/ProceduralMeshComponent/.../ProceduralMeshComponent.h | Update requires the same vertex count as the section: the glue recreates on topology change (MeshData.topo); colours -> 8-bit FColor (`bSRGBConversion = false` = linear); UVs half precision; `FProcMeshTangent(FVector, bool)`; UV3 carries the vertex alpha |
| `UStaticMeshComponent::SetStaticMesh`, `UStaticMesh::GetMaterial(0)`, `UMaterialInterface::GetBaseMaterial` | Engine/Classes/Components/StaticMeshComponent.h, Engine/Classes/Engine/StaticMesh.h, Engine/Public/Materials/MaterialInterface.h | per-mesh instances (rock normal maps) become the MID parent |
| `UMaterialInstanceDynamic::Create(Parent, Outer)`, `SetScalarParameterValue`, `SetVectorParameterValue`, `SetTextureParameterValue`, `ClearParameterValues` | Engine/Public/Materials/MaterialInstanceDynamic.h | parameter values cached per component |
| `UPrimitiveComponent::SetCollisionEnabled(NoCollision)`, `SetGenerateOverlapEvents`, `SetCanEverAffectNavigation`, `SetCastShadow`, `SetBoundsScale`, `SetTranslucentSortPriority`, `bAffectDistanceFieldLighting`, `bReceivesDecals`, `CanCharacterStepUpOn` | Engine/Classes/Components/PrimitiveComponent.h | everything collision-free, shadowless unless the item asks |
| `USceneComponent::SetupAttachment`, `AttachToComponent(…, FAttachmentTransformRules(EAttachmentRule::SnapToTarget…) / ::KeepWorldTransform, Socket)`, `SetUsingAbsoluteRotation / Scale`, `SetWorldTransform`, `SetRelativeLocation`, `SetVisibility`, `SetMobility(Movable)`, `RegisterComponent` | Engine/Classes/Components/SceneComponent.h | status effects follow fighter bones (`USkeletalMeshComponent` socket names = UE5 Manny bones) |
| `UPointLightComponent::SetIntensity`, `SetIntensityUnits(ELightUnits::Candelas)`, `SetAttenuationRadius`, `SetSourceRadius`, `SetLightColor`, `SetCastShadows(false)`, `SetAffectTranslucentLighting`, `SetVolumetricScatteringIntensity` | Engine/Classes/Components/PointLightComponent.h, LocalLightComponent.h | <= 4 shadowless lights |
| `UWorld::SpawnActor` (`FActorSpawnParameters`, `ESpawnActorCollisionHandlingMethod::AlwaysSpawn`), `UGameplayStatics::GetPlayerCameraManager` (`GetCameraLocation / Rotation`) | Engine/Classes/Engine/World.h, Kismet/GameplayStatics.h | camera position for camera-facing geometry |
| `FFileHelper::LoadFileToString`, `FPaths::ProjectContentDir`, `TAutoConsoleVariable`, `FAutoConsoleCommandWithWorld` (`FConsoleCommandWithWorldDelegate`), `FPlatformTime::Seconds`, `UE_LOG` (`*FString` arguments) | Core | `fx_config.json` read with `ff::ParseJson` (no `FJsonObject`) |
| `AddShaderSourceDirectoryMapping`, `AllShaderSourceDirectoryMappings` (FourfoldShaders, PostConfigInit) | RenderCore/Public/ShaderCore.h | `/Fourfold` -> `unreal/Shaders`, guarded against a second mapping |

## Editor Python (`fourfold/fx/__init__.py`)

| API | Verified in (5.8.2) | Notes |
|---|---|---|
| `unreal.AssetImportTask` (`filename`, `destination_path`, `destination_name`, `replace_existing`, `automated`, `save`, `options`, `imported_object_paths`), `AssetToolsHelpers.get_asset_tools().import_asset_tasks / create_asset` | ARCHITECTURE §12; character stream | PNG and FBX |
| `InterchangeGenericAssetsPipeline` -> `mesh_pipeline` (`import_static_meshes`, `import_skeletal_meshes`, `build_nanite` **default True**, `collision` default True), `common_meshes_properties` (`force_all_mesh_as_type = InterchangeForceMeshType.IFMT_STATIC_MESH`, `recompute_normals` / `recompute_tangents` default True -> False to keep the Blender normals and MikkT tangents the normal maps were baked with), `material_pipeline.import_materials`, `texture_pipeline.import_textures`; `InterchangePipelineStackOverride.add_pipeline` | Plugins/Interchange/Runtime/Source/Pipelines/Public/InterchangeGenericMeshPipeline.h, InterchangeGenericAssetsPipelineSharedSettings.h | fallback: `FbxImportUI` (+ `static_mesh_import_data.generate_lightmap_u_vs / combine_meshes / normal_import_method`) with `Interchange.FeatureFlags.Import.FBX 0` |
| `StaticMeshEditorSubsystem.set_generate_lightmap_uv` (ScriptName of `SetGenerateLightmapUVs`), `get_num_uv_channels` | Editor/StaticMeshEditor/Public/StaticMeshEditorSubsystem.h | rocks need UV1 / UV2 (melt blob offsets) |
| `StaticMesh.set_material(index, material)` | Engine/Classes/Engine/StaticMesh.h (`UFUNCTION SetMaterial`) | slot 0 = `MI_FX_Rock_<k>` or the master |
| `Texture2D` props `srgb`, `compression_settings` (`TextureCompressionSettings.TC_DEFAULT / TC_NORMALMAP / TC_MASKS`), `flip_green_channel`, `lod_group` (`TEXTUREGROUP_EFFECTS`, `TEXTUREGROUP_WORLD_NORMAL_MAP`) | Engine/Classes/Engine/TextureDefines.h, Texture.h | normal maps already DirectX |
| `MaterialEditingLibrary.create_material_expression / connect_material_expressions / connect_material_property / get_material_expressions / delete_material_expression / recompile_material / set_material_instance_parent / set_material_instance_texture_parameter_value / update_material_instance` | Editor/MaterialEditor/Private/MaterialEditingLibrary.cpp | `connect_material_expressions(a, "", …)` = first output; unnamed outputs match "R/G/B/A" masks (read in `GetExpressionOutputIndexByName`) |
| `MaterialExpressionCustom` (`code`, `output_type` = `CustomMaterialOutputType.CMOT_FLOAT1..4`, `description`, `inputs` = `[CustomInput(input_name)]`, `additional_outputs` = `[CustomOutput(output_name, output_type)]`, `include_file_paths`) | Engine/Public/Materials/MaterialExpressionCustom.h | texture-object inputs arrive as `<Name>` + `<Name>Sampler`; additional outputs assigned by name in the code |
| `MaterialExpressionTransform` (`transform_source_type` = `MaterialVectorCoordTransformSource.TRANSFORMSOURCE_LOCAL / TANGENT / WORLD`, `transform_type` = `MaterialVectorCoordTransform.TRANSFORM_WORLD / LOCAL`) | MaterialExpressionTransform.h | WPO local -> world; normal map tangent -> world; unit axes local -> world |
| `MaterialExpressionPreSkinnedPosition` (5.8 name; `PreSkinnedLocalPosition` in older engines, `LocalPosition` as last fallback), `MaterialExpressionPreSkinnedNormal`, `MaterialExpressionVertexInterpolator` (one input), `MaterialExpressionVertexNormalWS`, `MaterialExpressionCameraVectorWS`, `MaterialExpressionWorldPosition.world_position_shader_offset = WorldPositionIncludedOffsets.WPT_CAMERA_RELATIVE`, `MaterialExpressionTextureCoordinate.coordinate_index`, `MaterialExpressionComponentMask.r/g/b/a`, `MaterialExpressionConstant(r)`, `MaterialExpressionConstant3Vector(constant)`, `MaterialExpressionTime`, `MaterialExpressionVertexColor` | Engine/Public/Materials/*.h | |
| `MaterialExpressionScalarParameter(parameter_name, default_value, group)`, `MaterialExpressionVectorParameter(… LinearColor)`, `MaterialExpressionTextureObjectParameter(parameter_name, texture, sampler_type)`, `MaterialExpressionTextureSampleParameter2D` (+ `UVs` pin, `RGB` output), `MaterialSamplerType.SAMPLERTYPE_MASKS / LINEAR_COLOR / NORMAL` | MaterialExpressionScalarParameter.h, VectorParameter.h, TextureObjectParameter.h, Classes/Engine/EngineTypes.h | sampler type must match the texture compression (Masks / Default+linear / Normalmap) |
| `Material` props `blend_mode` (`BlendMode.BLEND_OPAQUE / ALPHA_COMPOSITE / ADDITIVE / TRANSLUCENT`), `shading_model` (`MSM_DEFAULT_LIT / MSM_UNLIT`), `two_sided`, `tangent_space_normal`, `translucency_lighting_mode` (`TLM_SURFACE_PER_PIXEL_LIGHTING` = "Surface ForwardShading"), `used_with_instanced_static_meshes`, `float_precision_mode` (`MaterialFloatPrecisionMode.MFPM_FULL`; `bUseFullPrecision` is deprecated) | Engine/Public/Materials/Material.h, Classes/Engine/EngineTypes.h | AlphaComposite = premultiplied (Emissive + Dest x (1 - Opacity)) works on mobile forward |
| `MaterialInstanceConstant` + `MaterialInstanceConstantFactoryNew`, `EditorAssetLibrary.does_asset_exist / save_loaded_asset`, `unreal.load_asset` | character stream (same calls) | |

## Material-compiler conventions the HLSL relies on
* Custom-node includes are absolute virtual paths (`/Fourfold/FX/FFRock.ush`); nested includes are guarded
  (`#ifndef FF_NOISE_USH #include "/Fourfold/Common/FFNoise.ush"`) so the C++ shim can include the same files.
* No `Sample` in vertex-stage nodes (WPO functions are procedural); `ddx/ddy` only inside node code (water facets),
  never in the shared files; no vector single-argument constructors; no HLSL reserved words as names (`line`).
* Mobile materials default to half precision (`r.Mobile.FloatPrecisionMode`); rock, lava, water, crystal, ground
  materials set `MFPM_FULL` (Voronoi hashing and cm-scale positions).

## Sources
* UE 5.8.2 source mirror: https://github.com/AFIshInWater/UE5.8 (headers / MaterialEditingLibrary.cpp read locally
  from a sparse checkout).
* Epic Python API pages (via search summaries; dev.epicgames.com is not fetchable from the container):
  MaterialExpressionTransform / MaterialVectorCoordTransformSource, WorldPositionIncludedOffsets,
  MaterialExpressionPreSkinnedPosition, MaterialEditingLibrary.
* Unreal Custom-node HLSL behaviour (texture object `<Name>Sampler`, additional outputs): ARCHITECTURE §12, character
  stream `docs/character/API_NOTES.md`.
* Microsoft DXC release linux_dxc_2024_07_31: https://github.com/microsoft/DirectXShaderCompiler/releases
