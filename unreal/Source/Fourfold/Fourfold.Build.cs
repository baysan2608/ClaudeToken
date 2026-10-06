// Fourfold - primary game module: sim subsystem, fighters + native anim runtime, camera, input (keyboard / gamepad /
// Slate touch overlay), HUD / menus / Lab panel (Slate), game flow, feel (hit-stop, shake, haptics). Owner: stream `game`.
using UnrealBuildTool;

public class Fourfold : ModuleRules
{
	public Fourfold(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core", "CoreUObject", "Engine", "InputCore", "FourfoldCore"
		});
		PrivateDependencyModuleNames.AddRange(new string[] {
			"Slate", "SlateCore", "ApplicationCore", "EnhancedInput", "AnimationCore"
		});
	}
}
