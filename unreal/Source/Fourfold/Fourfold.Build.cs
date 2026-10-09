// Fourfold - primary game module: sim subsystem, fighters + native anim runtime, camera, input (keyboard / gamepad /
// Slate touch overlay), HUD / menus / Lab panel (Slate), game flow, feel (hit-stop, shake, haptics). Owner: stream `game`.
// Private/Logic/ is the engine-free "logic island" (no Unreal headers; unit-tested with CMake in Private/Logic/tests).
using System.IO;
using UnrealBuildTool;

public class Fourfold : ModuleRules
{
	public Fourfold(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core", "CoreUObject", "Engine", "InputCore", "FourfoldCore"
		});
		// Slate / SlateCore: HUD, touch overlay, menus, Lab panel. ApplicationCore: FPlatformApplicationMisc (screen density).
		// Input is read from APlayerController key state (Engine + InputCore); no Enhanced Input assets are needed.
		PrivateDependencyModuleNames.AddRange(new string[] {
			"Slate", "SlateCore", "ApplicationCore",
			"ProceduralMeshComponent",   // open-world terrain chunks (FourfoldOpenWorld)
			"HairStrandsCore"   // MetaHuman grooms (GroomComponent)
		});

		// "Logic/FFG*.h" from the module's own sources (Private is also a default include path; listed for clarity).
		PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "Private"));
	}
}
