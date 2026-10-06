// FourfoldFX - every visual effect: persistent body views driven by ff::Snapshot (stone / lava / water / ice / steam /
// sand / metal / fire / lightning / wind / vortex / vacuum / sound / plant), one-shot cues from ff::Event, charge /
// status visuals, pooled CPU-driven procedural meshes with Python-built materials. Owner: stream `fx`.
// Private/Logic is engine-free (unit-tested with CMake: Private/Logic/tests); its tests compile to nothing here
// (FF_LOGIC_TESTS is only defined by that CMake project).
using System.IO;
using UnrealBuildTool;

public class FourfoldFX : ModuleRules
{
	public FourfoldFX(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core", "CoreUObject", "Engine", "FourfoldCore", "Fourfold"
		});
		PrivateDependencyModuleNames.AddRange(new string[] {
			"ProceduralMeshComponent", "RenderCore"
		});
		PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "Private"));
		PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "Private", "Logic"));
	}
}
