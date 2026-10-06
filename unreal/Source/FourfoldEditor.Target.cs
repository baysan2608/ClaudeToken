// Fourfold - editor target (Mac). FROZEN (architect).
using UnrealBuildTool;
using System.Collections.Generic;

public class FourfoldEditorTarget : TargetRules
{
	public FourfoldEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.AddRange(new string[] { "Fourfold", "FourfoldCore", "FourfoldFX", "FourfoldAudio", "FourfoldShaders" });
	}
}
