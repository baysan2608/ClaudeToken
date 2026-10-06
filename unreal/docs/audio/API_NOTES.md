# Audio stream - Unreal APIs relied on (and where they were verified)

Two code paths: the runtime module `Source/FourfoldAudio` (C++) and the editor import script
`Content/Python/fourfold/audio/__init__.py`. "Header mirror" = a read-only copy of the UE 5.8.2 public headers made by the
`game` stream; `Tools/audio/ue_syntax_check/check_audio.sh` runs `clang++ -fsyntax-only` (mock UHT for `*.generated.h`) on every
`.cpp` of the module against it with warnings on: **0 errors, 0 warnings located in Source/FourfoldAudio**. That proves
declarations and signatures exist in 5.8.2; it cannot prove link / runtime behaviour (first real compile is on the Mac).

## C++ (Source/FourfoldAudio)

| API | Used for | Verified in |
|---|---|---|
| `UWorldSubsystem`: `ShouldCreateSubsystem(UObject*)`, `Initialize(FSubsystemCollectionBase&)`, `Deinitialize()`, `OnWorldBeginPlay(UWorld&)`; `Collection.InitializeDependency<T>()`; `World->GetSubsystem<T>()` | subsystem lifecycle, ordering after `UFourfoldSimSubsystem` | header mirror `Engine/Public/Subsystems/WorldSubsystem.h`, `Core/.../SubsystemCollection.h` |
| `UGameplayStatics::SpawnSound2D(Ctx, Sound, Vol, Pitch, Start, Concurrency, bPersist, bAutoDestroy)` | one-shot UI / 2D / ambience-accent sounds | header mirror `Engine/Classes/Kismet/GameplayStatics.h` line 699 |
| `UGameplayStatics::CreateSound2D(...)` (same parameters) + `UAudioComponent::Play()` | persistent 2D loops (ambience beds) with `bAutoDestroy=false` | same header, line 715 |
| `UGameplayStatics::SpawnSoundAtLocation(Ctx, Sound, Location, Rotation, Vol, Pitch, Start, USoundAttenuation*, USoundConcurrency*, bAutoDestroy)` | positioned one-shots and 3D loops | same header, line 754 |
| `UAudioComponent::FadeOut(Duration, FadeVolumeLevel, Curve)`, `Stop()`, `SetVolumeMultiplier(float)`, `SetUISound(bool)`, `SetWorldLocation` | voice steal (30 ms fade), loop gain updates every frame, "keep playing while the sim is paused" for UI / ambience | header mirror `Engine/Classes/Components/AudioComponent.h` (lines 511, 626, 634) |
| `USoundAttenuation::Attenuation` = `FSoundAttenuationSettings`: `bAttenuate`, `bSpatialize`, `DistanceAlgorithm = EAttenuationDistanceModel::NaturalSound`, `AttenuationShape = EAttenuationShape::Sphere`, `AttenuationShapeExtents.X` (inner radius, full volume), `FalloffDistance`, `dBAttenuationAtMax`, `FalloffMode = ENaturalSoundFalloffMode::Continues`; `NewObject<USoundAttenuation>(this)` | runtime attenuation classes near / mid / far / feel built from the manifest (no asset needed) | header mirror `Engine/Classes/Sound/SoundAttenuation.h`; semantics of NaturalSound (inner radius + falloff distance + dB at max) from Epic's "Sound Attenuation" documentation page (WebSearch summary) |
| `USoundWave::bLooping` (`uint8 bLooping : 1`) | the manifest is the truth for loop flags at runtime (also set by the import script) | header mirror `Engine/Classes/Sound/SoundWave.h` line 451 |
| `LoadObject<USoundBase>(nullptr, "/Game/...S_name.S_name")` | sounds loaded at `OnWorldBeginPlay` (no first-use hitch) | standard UObject API; object path = `Package.Name` |
| `FFileHelper::LoadFileToString`, `FPaths::ProjectContentDir()`, `FPackageName::GetShortName` | manifest read from `Content/Fourfold/Data` (staged as UFS by `DefaultGame.ini`) | Core headers in the mirror |
| `FAutoConsoleCommandWithWorld` + `FConsoleCommandWithWorldDelegate::CreateStatic` | console command `ff.audio.dump` | `Core/Public/HAL/IConsoleManager.h` |
| `IsRunningDedicatedServer()`, `IsRunningCommandlet()` | no subsystem on servers / commandlets | `CoreUObject` / `Core` headers |
| `FTCHARToUTF8`, `UTF8_TO_TCHAR` | `std::string` <-> `FString` for the engine-free logic | Core headers |
| `UFourfoldSimSubsystem::OnFrame / OnUiCue / OnScenarioLoaded` (`AddUObject`, `Remove`), `FFourfoldFrame` (`Prev`, `Curr`, `Alpha`, `Events`, `RealDeltaSeconds`, `GameDeltaSeconds`, `bPaused`), `UFourfoldSettingsSubsystem::GetSettings()` (`MasterVolume`, `SfxVolume`, `UiVolume`, `AmbienceVolume`), `FF::ToUE` / `FF::ToSim` | the project's frozen headers (`Source/Fourfold/Public`) | read directly |
| `ff::ParseJson` / `ff::Value` | manifest parsing in `Logic/FFAManifest.cpp` (FJsonObject is avoided: its key type changed in 5.8) | `Source/FourfoldCore/Public/ff/Json.h` |

Behaviour facts relied on (WebSearch of Epic documentation / forum answers, 2026):
* Global time dilation (the game's hit-stop uses 0.05) does **not** pitch or slow USoundBase playback, so one-shots keep
  their pitch during hit-stop; the footstep cadence uses the dilated delta on purpose (it freezes with the sim).
* `USoundAttenuation` objects can be created at runtime and passed to `SpawnSoundAtLocation`; `bAutoDestroy=false`
  components must be stopped / destroyed by the owner (`ApplyLoops` does it when a loop finishes, `Deinitialize` for the rest).
* The engine's per-platform "Max Channels" (default 32; iOS value in `Config/DefaultEngine.ini` `[/Script/IOSRuntimeSettings.IOSRuntimeSettings]`)
  is the hard ceiling; the module's own caps (`mix.global_voices` = 28 one-shots, `mix.loop_voices` = 14 loops) are in the manifest.

## Editor Python (Content/Python/fourfold/audio/__init__.py)

Every property write goes through `_set()` (never raises; unknown names are listed in `setup_report.json` notes).

| API | Used for | Verified in |
|---|---|---|
| `unreal.AssetImportTask` (`filename`, `destination_path`, `destination_name`, `replace_existing`, `automated`, `save`) + `unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([...])` | WAV -> `USoundWave` (factory chosen by the engine from the extension; no options needed) | ARCHITECTURE §12; open-source: github.com/alexrios/ArenaGame `Tools/RepairOACharacters.py`, github.com/owenpkent/crab-sim `Art/unreal/build_content.py` |
| `SoundWave` editor properties `looping` (bLooping), `sound_group` (`unreal.SoundGroup.SOUNDGROUP_EFFECTS / SOUNDGROUP_UI`), `loading_behavior` (`unreal.SoundWaveLoadingBehavior.PRIME_ON_LOAD / RETAIN_ON_LOAD`), `compression_quality` (int), `sound_class_object` | loop flag, streaming / memory policy, class assignment | UPROPERTY names in the header mirror (`SoundWave.h`, `SoundBase.h`); Python name = property name without the `b` prefix / in snake_case (Epic Python API rules) |
| `unreal.SoundClassFactory`, `unreal.SoundClass` (`child_classes`) via `AssetTools.create_asset` | optional `SC_Master > SC_SFX / SC_UI / SC_Ambience` (volumes stay 1.0, mixing is done by the module) | Epic Python API index (WebSearch); wrapped in try / except, skipped on failure |
| `unreal.EditorAssetLibrary.does_asset_exist / save_loaded_asset`, `unreal.load_asset`, `unreal.Paths.project_dir / convert_relative_path_to_full` | plumbing | ARCHITECTURE §12 |

If a sound does not play on the Mac: console `ff.audio.dump` (ready / voices / loops / dropped / missing names), then
`LogFourfoldAudio` in `Saved/Logs/Fourfold.log`.
