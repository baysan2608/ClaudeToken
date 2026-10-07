// Fourfold primary game module. Owner: stream `game`.
#include "Modules/ModuleManager.h"
#include "Containers/Ticker.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "HAL/PlatformTime.h"
#include "UnrealClient.h"
#include "Engine/Engine.h"
#include "FourfoldLog.h"

DEFINE_LOG_CATEGORY(LogFourfold);

namespace
{
// Dev capture for automated look checks (works with a locked screen, unlike an OS screenshot):
//   -FFShot=<seconds after start>[,<seconds>...]  -FFShotDir=<folder>  [-FFShotQuit]
// Writes <folder>/shot_<n>.png from the game viewport; -FFShotQuit exits after the last one.
class FFourfoldModule final : public FDefaultGameModuleImpl
{
public:
	virtual void StartupModule() override
	{
		FString Spec;
		if (!FParse::Value(FCommandLine::Get(), TEXT("-FFShot="), Spec, false)) return;
		TArray<FString> Parts;
		Spec.ParseIntoArray(Parts, TEXT(","));
		for (const FString& P : Parts) Times.Add(FCString::Atod(*P));
		Times.Sort();
		if (!FParse::Value(FCommandLine::Get(), TEXT("-FFShotDir="), Dir)) Dir = FPaths::ProjectSavedDir() / TEXT("Shots");
		bQuit = FParse::Param(FCommandLine::Get(), TEXT("FFShotQuit"));
		Start = FPlatformTime::Seconds();
		Handle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FFourfoldModule::Tick));
	}

	virtual void ShutdownModule() override
	{
		if (Handle.IsValid()) FTSTicker::GetCoreTicker().RemoveTicker(Handle);
	}

private:
	bool Tick(float)
	{
		const double T = FPlatformTime::Seconds() - Start;
		if (Next < Times.Num() && T >= Times[Next])
		{
			const FString File = Dir / FString::Printf(TEXT("shot_%d.png"), Next);
			FScreenshotRequest::RequestScreenshot(File, false, false);
			UE_LOG(LogFourfold, Display, TEXT("FFShot %d at %.1fs -> %s"), Next, T, *File);
			++Next;
			QuitAt = T + 2.0;
		}
		if (bQuit && Next >= Times.Num() && T >= QuitAt && QuitAt > 0.0 && GEngine)
		{
			GEngine->DeferredCommands.Add(TEXT("QUIT"));
			return false;
		}
		return true;
	}

	TArray<double> Times;
	FString Dir;
	bool bQuit = false;
	int32 Next = 0;
	double Start = 0.0;
	double QuitAt = 0.0;
	FTSTicker::FDelegateHandle Handle;
};
}

IMPLEMENT_PRIMARY_GAME_MODULE(FFourfoldModule, Fourfold, "Fourfold");
