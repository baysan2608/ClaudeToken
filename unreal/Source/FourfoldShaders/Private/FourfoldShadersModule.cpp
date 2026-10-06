// FourfoldShaders: virtual shader directory /Fourfold -> <Project>/Shaders. Owner: stream `fx`.
// Materials built by the editor-Python scripts reference e.g. "/Fourfold/FX/FFFlame.ush" in Custom-node include paths.
#include "Modules/ModuleManager.h"
#include "Misc/Paths.h"
#include "ShaderCore.h"

class FFourfoldShadersModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		const FString ShaderDir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("Shaders")));
		// Cooked games compile no shaders and may not ship the folder: map it only when it exists, and only once.
		if (FPaths::DirectoryExists(ShaderDir) && !AllShaderSourceDirectoryMappings().Contains(TEXT("/Fourfold")))
		{
			AddShaderSourceDirectoryMapping(TEXT("/Fourfold"), ShaderDir);
		}
	}
};

IMPLEMENT_MODULE(FFourfoldShadersModule, FourfoldShaders);
