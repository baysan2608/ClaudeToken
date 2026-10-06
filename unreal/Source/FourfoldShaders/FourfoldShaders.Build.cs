// FourfoldShaders - maps unreal/Shaders to the virtual shader path /Fourfold so material Custom nodes can
// #include "/Fourfold/...ush". Loads at PostConfigInit (before any shader compiles). Owner: stream `fx`.
using UnrealBuildTool;

public class FourfoldShaders : ModuleRules
{
	public FourfoldShaders(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PrivateDependencyModuleNames.AddRange(new string[] { "Core", "RenderCore" });
	}
}
