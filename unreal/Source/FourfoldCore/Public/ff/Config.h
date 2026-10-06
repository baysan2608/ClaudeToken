// Fourfold core - build configuration (engine-free C++20; also compiled as the UE module "FourfoldCore").
// FROZEN CONTRACT (architect). Owner after creation: stream `core` (additive changes only).
#pragma once

// FOURFOLDCORE_API marks every non-inline class / free function that other modules call.
//  * UE build: UnrealBuildTool defines it as DLLEXPORT / DLLIMPORT; HAL/Platform.h defines those, so include it first.
//  * CMake build: defined on the command line (visibility("default") in the shared-library check build, empty otherwise).
// Never use `#if FF_WITH_UE` (UE compiles with -Wundef as an error): use `#if defined(FF_WITH_UE)`.
#if defined(FF_WITH_UE)
#include "HAL/Platform.h"
#endif
#ifndef FOURFOLDCORE_API
#define FOURFOLDCORE_API
#endif

#define FF_CORE_API_VERSION 1

// Identifiers that are macros in Unreal and must NEVER be used as names in ff code (they break the UE build):
//   check checkf checkSlow verify verifyf ensure ensureMsgf ensureAlways TEXT PI HALF_PI INV_PI UE_PI
//   SMALL_NUMBER KINDA_SMALL_NUMBER BIG_NUMBER DELTA THRESH_* MAX_FLT INDEX_NONE IN OUT OPTIONAL CONSTEXPR
//   FORCEINLINE FORCENOINLINE RESTRICT ABSTRACT LIKELY UNLIKELY DEPRECATED TRUE FALSE (Apple) nil Nil YES NO
//   int8 int16 int32 int64 uint8 uint16 uint32 uint64 TCHAR (UE typedefs: use <cstdint> types)
// The core CMake build compiles every source once more with tests/ue_macro_poison.h force-included to catch these.
