# FourfoldCore - Unreal API notes

FourfoldCore uses **no Unreal API** outside two files. Everything under `Public/ff` and `Private/` (except
`Private/UE/`) is plain C++20 + the standard library, built and tested here with CMake (g++ 13, clang 18).

## The only Unreal touch points

| File | API | Source / verification |
|---|---|---|
| `Source/FourfoldCore/Private/UE/FourfoldCoreModule.cpp` | `#include "Modules/ModuleManager.h"`, `IMPLEMENT_MODULE(FDefaultModuleImpl, FourfoldCore);` | Epic, "How to make a gameplay module" (`IMPLEMENT_MODULE(FDefaultModuleImpl, <Module>)` with `Modules/ModuleManager.h`): https://dev.epicgames.com/documentation/en-us/unreal-engine/how-to-make-a-gameplay-module-in-unreal-engine ; `FDefaultModuleImpl` (Runtime/Core, `Modules/ModuleManager.h`): https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Core/FDefaultModuleImpl |
| `Source/FourfoldCore/FourfoldCore.Build.cs` | `ModuleRules`, `ReadOnlyTargetRules`, `PCHUsage = PCHUsageMode.NoPCHs`, `bUseUnity = false`, `bEnableExceptions = false`, `bUseRTTI = false`, `PublicDefinitions.Add("FF_WITH_UE=1")`, `PublicDependencyModuleNames` (`Core`) | UnrealBuildTool module-file properties (`bUseRTTI`, `bEnableExceptions`, `PCHUsage`, `bUseUnity`), Epic docs "Module files": https://docs.unrealengine.com/4.27/en-US/ProductionPipelines/BuildTools/UnrealBuildTool/ModuleFiles ; long-stable fields, unchanged in 5.x |

Both files were provided by the architect; this stream did not change them.

## Rules that keep the sources compiling inside Unreal (checked here)

- `FOURFOLDCORE_API` on every non-inline class / free function in `Public/ff` (`ff_core_shared` + `ff_facade_link_test`
  link the facade against a `-fvisibility=hidden` build).
- No identifier equal to an Unreal macro (`check`, `verify`, `ensure`, `TEXT`, `PI`, `IN`, `OUT`, `INDEX_NONE`, ...):
  `ff_poison` compiles every source with `CoreTests/ue_macro_poison.h` force-included. (`Moves.ensure()` became
  `Moves::ensure_ready()` for this reason.)
- `#if defined(X)` only (`-Wundef`); no exceptions, RTTI, `std::any`, iostream or `thread_local` in library code.
- No PCH and no unity merging in the module (`Build.cs`); `ff_unity` still proves unity batches compile.
- `FF_WITH_UE` is defined only by `Build.cs`; `Public/ff/Config.h` is the only place that reads it.
