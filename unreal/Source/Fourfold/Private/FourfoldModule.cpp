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
#include "FourfoldDevCapture.h"

DEFINE_LOG_CATEGORY(LogFourfold);

namespace
{
struct FBurstSpec
{
	FString Tag;
	int32 Count = 8;
	double Interval = 0.15;
};
struct FBurst
{
	FString Tag;
	int32 Index = 0, Done = 0, Count = 0;
	double Next = 0.0, Interval = 0.15;
};
TArray<FBurstSpec> GBurstSpecs;
TArray<FBurst> GBursts;
int32 GBurstsStarted = 0, GBurstsFinished = 0, GBurstQuit = 0;
FString GShotDir;
bool GBurstParsed = false;

void ParseBurstSpecs()
{
	if (GBurstParsed)
	{
		return;
	}
	GBurstParsed = true;
	FString Spec;
	if (!FParse::Value(FCommandLine::Get(), TEXT("-FFBurst="), Spec, false))
	{
		return;
	}
	TArray<FString> Items;
	Spec.ParseIntoArray(Items, TEXT(","));
	for (const FString& It : Items)
	{
		TArray<FString> P;
		It.ParseIntoArray(P, TEXT(":"));
		if (P.Num() >= 1)
		{
			FBurstSpec B;
			B.Tag = P[0];
			B.Count = P.Num() > 1 ? FMath::Max(1, FCString::Atoi(*P[1])) : 8;
			B.Interval = P.Num() > 2 ? FMath::Max(0.02, FCString::Atod(*P[2])) : 0.15;
			GBurstSpecs.Add(B);
		}
	}
	FParse::Value(FCommandLine::Get(), TEXT("-FFBurstQuit="), GBurstQuit);
	if (!FParse::Value(FCommandLine::Get(), TEXT("-FFShotDir="), GShotDir))
	{
		GShotDir = FPaths::ProjectSavedDir() / TEXT("Shots");
	}
}

// Dev capture for automated look checks (works with a locked screen, unlike an OS screenshot):
//   -FFShot=<seconds after start>[,<seconds>...]  -FFShotDir=<folder>  [-FFShotQuit]
// Writes <folder>/shot_<n>.png from the game viewport; -FFShotQuit exits after the last one.
//   -FFExec="<seconds>:<console command>|<seconds>:<command>..." runs console commands at those times (perf A/B in one
//   run, e.g. "30:csvprofile frames=300|36:r.VolumetricCloud 0|37:csvprofile frames=300").
class FFourfoldModule final : public FDefaultGameModuleImpl
{
public:
	virtual void StartupModule() override
	{
		ParseBurstSpecs();
		FString Spec;
		if (FParse::Value(FCommandLine::Get(), TEXT("-FFExec="), Spec, false))
		{
			TArray<FString> Parts;
			Spec.ParseIntoArray(Parts, TEXT("|"));
			for (const FString& P : Parts)
			{
				FString When, Cmd;
				if (P.Split(TEXT(":"), &When, &Cmd)) Execs.Add({FCString::Atod(*When), Cmd.TrimStartAndEnd()});
			}
			Execs.Sort([](const FTimedExec& A, const FTimedExec& B) { return A.Time < B.Time; });
		}
		if (GBurstSpecs.Num() > 0 || Execs.Num() > 0)
		{
			Start = FPlatformTime::Seconds();
			Handle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FFourfoldModule::Tick));
		}
		if (!FParse::Value(FCommandLine::Get(), TEXT("-FFShot="), Spec, false)) return;
		TArray<FString> Parts;
		Spec.ParseIntoArray(Parts, TEXT(","));
		for (const FString& P : Parts) Times.Add(FCString::Atod(*P));
		Times.Sort();
		if (!FParse::Value(FCommandLine::Get(), TEXT("-FFShotDir="), Dir)) Dir = FPaths::ProjectSavedDir() / TEXT("Shots");
		bQuit = FParse::Param(FCommandLine::Get(), TEXT("FFShotQuit"));
		Start = FPlatformTime::Seconds();
		if (!Handle.IsValid())
		{
			Handle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FFourfoldModule::Tick));
		}
	}

	virtual void ShutdownModule() override
	{
		if (Handle.IsValid()) FTSTicker::GetCoreTicker().RemoveTicker(Handle);
	}

private:
	bool Tick(float)
	{
		const double Now = FPlatformTime::Seconds();
		for (int32 b = GBursts.Num() - 1; b >= 0; --b)
		{
			FBurst& B = GBursts[b];
			if (Now >= B.Next)
			{
				const FString File = GShotDir / FString::Printf(TEXT("%s_%d_%02d.png"), *B.Tag, B.Index, B.Done);
				FScreenshotRequest::RequestScreenshot(File, false, false);
				B.Next = Now + B.Interval;
				if (++B.Done >= B.Count)
				{
					GBursts.RemoveAt(b);
					++GBurstsFinished;
					UE_LOG(LogFourfold, Display, TEXT("FFBurst %s done (%d finished)"), *File, GBurstsFinished);
				}
			}
		}
		if (GBurstQuit > 0 && GBurstsFinished >= GBurstQuit && GBursts.Num() == 0 && GEngine)
		{
			GBurstQuit = 0;
			GEngine->DeferredCommands.Add(TEXT("QUIT"));
		}
		const double T = FPlatformTime::Seconds() - Start;
		while (NextExec < Execs.Num() && T >= Execs[NextExec].Time && GEngine)
		{
			UE_LOG(LogFourfold, Display, TEXT("FFExec at %.1fs: %s"), T, *Execs[NextExec].Cmd);
			GEngine->DeferredCommands.Add(Execs[NextExec++].Cmd);
		}
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

	struct FTimedExec
	{
		double Time;
		FString Cmd;
	};
	TArray<FTimedExec> Execs;
	int32 NextExec = 0;
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

void FourfoldDev::Trigger(const TCHAR* Tag)
{
	ParseBurstSpecs();
	for (const FBurstSpec& S : GBurstSpecs)
	{
		if (S.Tag == Tag)
		{
			FBurst B;
			B.Tag = S.Tag;
			B.Count = S.Count;
			B.Interval = S.Interval;
			B.Index = GBurstsStarted++;
			B.Next = FPlatformTime::Seconds();
			GBursts.Add(B);
			UE_LOG(LogFourfold, Display, TEXT("FFBurst %s #%d started"), Tag, B.Index);
		}
	}
}
