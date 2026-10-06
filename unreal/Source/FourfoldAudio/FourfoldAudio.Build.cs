// FourfoldAudio - event -> sound mapping, body / zone loops, voice caps, ambience, mixing (port of the Godot
// AudioDirector + FxDirector audio cues). Owner: stream `world_audio`.
using UnrealBuildTool;

public class FourfoldAudio : ModuleRules
{
	public FourfoldAudio(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core", "CoreUObject", "Engine", "FourfoldCore", "Fourfold"
		});
	}
}
