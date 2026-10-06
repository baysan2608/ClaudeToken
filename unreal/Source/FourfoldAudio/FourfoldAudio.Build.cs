// FourfoldAudio - event -> sound mapping, body / zone loops, voice caps, ambience, mixing (port of the Godot
// AudioDirector + FxDirector audio cues, data-driven by Content/Fourfold/Data/sfx_manifest.json). Owner: stream `world_audio`.
// Private/Logic is the engine-free "logic island" (rule engine, limiters, voice caps, loop fades, steps, ambience schedule),
// unit-tested with CMake (Private/Logic/tests); its tests compile to nothing here (FF_LOGIC_TESTS is only defined by that project).
using System.IO;
using UnrealBuildTool;

public class FourfoldAudio : ModuleRules
{
	public FourfoldAudio(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core", "CoreUObject", "Engine", "FourfoldCore", "Fourfold"
		});

		// "Logic/FFA*.h" (and the Logic files' own flat includes) from the module's sources.
		PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "Private"));
		PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "Private", "Logic"));
	}
}
