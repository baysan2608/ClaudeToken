// Fourfold - game target (Mac, iOS). FROZEN (architect).
using UnrealBuildTool;
using System.Collections.Generic;

public class FourfoldTarget : TargetRules
{
	public FourfoldTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.AddRange(new string[] { "Fourfold", "FourfoldCore", "FourfoldFX", "FourfoldAudio", "FourfoldShaders" });
	}
}
