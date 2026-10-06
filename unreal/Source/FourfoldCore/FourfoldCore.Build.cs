// FourfoldCore - the engine-free C++20 simulation (also built with CMake by unreal/CoreTests). Owner: stream `core`.
// Rules: no UObject / UE headers in Public/ff or Private/ (except Private/UE/FourfoldCoreModule.cpp); no exceptions,
// no RTTI; every exported class / free function carries FOURFOLDCORE_API (editor builds are modular on the Mac).
using UnrealBuildTool;

public class FourfoldCore : ModuleRules
{
	public FourfoldCore(ReadOnlyTargetRules Target) : base(Target)
	{
		// Engine-free sources: no shared PCH force-included into them (keeps UE macros such as check / ensure / TEXT
		// out of the sim code), no unity merging (file-local helpers may share names).
		PCHUsage = PCHUsageMode.NoPCHs;
		bUseUnity = false;
		bEnableExceptions = false;
		bUseRTTI = false;

		PublicDefinitions.Add("FF_WITH_UE=1");
		PublicDependencyModuleNames.AddRange(new string[] { "Core" });
	}
}
