// FourfoldFX - world subsystem: sim frame -> FX director -> pooled components. Owner: stream `fx`.
#include "FourfoldFxSubsystem.h"

#include "Camera/PlayerCameraManager.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "FourfoldCoords.h"
#include "FourfoldFighter.h"
#include "FourfoldFxActor.h"
#include "FourfoldSettings.h"
#include "FourfoldSimSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "Logic/FxDirector.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#include <string>
#include <vector>

DEFINE_LOG_CATEGORY_STATIC(LogFourfoldFx, Log, All);

static TAutoConsoleVariable<int32> CVarFourfoldFxEnable(TEXT("ff.fx.Enable"), 1,
	TEXT("Fourfold effects: 1 = on, 0 = off (every effect component is released)."), ECVF_Default);
static TAutoConsoleVariable<int32> CVarFourfoldFxStats(TEXT("ff.fx.Stats"), 0,
	TEXT("Fourfold effects: 1 = print the per-frame counters on screen."), ECVF_Default);

struct FFourfoldFxImpl
{
	ffx::FxDirector Director;
	ffx::FxConfig Config;
	std::vector<ffx::FxAnchors> Anchors;
	double LastUpdateMs = 0.0;
	double PeakUpdateMs = 0.0;
};

namespace
{
	bool LoadConfigFile(ffx::FxConfig& Out)
	{
		const FString Path = FPaths::ProjectContentDir() / TEXT("Fourfold/Data/fx_config.json");
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *Path))
		{
			UE_LOG(LogFourfoldFx, Warning, TEXT("fx_config.json not found at %s: built-in defaults are used."), *Path);
			return false;
		}
		const FTCHARToUTF8 Utf8(*Text);
		std::string Error, Warnings;
		ffx::FxConfig Cfg;
		if (!Cfg.LoadJson(std::string_view(Utf8.Get(), size_t(Utf8.Length())), &Error, &Warnings))
		{
			UE_LOG(LogFourfoldFx, Error, TEXT("fx_config.json: %s (defaults kept)"), UTF8_TO_TCHAR(Error.c_str()));
			return false;
		}
		if (!Warnings.empty())
		{
			UE_LOG(LogFourfoldFx, Warning, TEXT("fx_config.json: %s"), UTF8_TO_TCHAR(Warnings.c_str()));
		}
		Out = Cfg;
		return true;
	}

	void ReloadAllWorlds(UWorld* World)
	{
		if (UFourfoldFxSubsystem* Fx = World ? World->GetSubsystem<UFourfoldFxSubsystem>() : nullptr)
		{
			Fx->ReloadConfig();
		}
	}

	FAutoConsoleCommandWithWorld GFourfoldFxReloadCmd(TEXT("ff.fx.ReloadConfig"),
		TEXT("Fourfold effects: re-read Content/Fourfold/Data/fx_config.json."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&ReloadAllWorlds));

	ffx::Vec3 ToSimV(const FVector& V) { return FF::ToSim(V); }
	ffx::Vec3 ToSimDir(const FVector& V) { return FF::DirToSim(V); }
}

UFourfoldFxSubsystem* UFourfoldFxSubsystem::Get(const UObject* WorldContextObject)
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	return World ? World->GetSubsystem<UFourfoldFxSubsystem>() : nullptr;
}

bool UFourfoldFxSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UFourfoldFxSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Impl = MakeShared<FFourfoldFxImpl>();
	LoadConfigFile(Impl->Config);
	Impl->Director.SetConfig(Impl->Config);
}

void UFourfoldFxSubsystem::Deinitialize()
{
	if (UFourfoldSimSubsystem* Sim = SimWeak.Get())
	{
		Sim->OnFrame.Remove(FrameHandle);
		Sim->OnScenarioLoaded.Remove(ScenarioHandle);
	}
	FrameHandle.Reset();
	ScenarioHandle.Reset();
	if (FxActor)
	{
		FxActor->ReleaseAll();
		FxActor->Destroy();
		FxActor = nullptr;
	}
	Impl.Reset();
	Super::Deinitialize();
}

void UFourfoldFxSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	UFourfoldSimSubsystem* Sim = InWorld.GetSubsystem<UFourfoldSimSubsystem>();
	if (!Sim)
	{
		UE_LOG(LogFourfoldFx, Warning, TEXT("No UFourfoldSimSubsystem in this world: effects stay off."));
		return;
	}
	SimWeak = Sim;
	FrameHandle = Sim->OnFrame.AddUObject(this, &UFourfoldFxSubsystem::OnSimFrame);
	ScenarioHandle = Sim->OnScenarioLoaded.AddUObject(this, &UFourfoldFxSubsystem::OnScenarioLoaded);
	EnsureActor();
}

void UFourfoldFxSubsystem::EnsureActor()
{
	UWorld* World = GetWorld();
	if (FxActor || !World || !Impl)
	{
		return;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags |= RF_Transient;
	FxActor = World->SpawnActor<AFourfoldFxActor>(AFourfoldFxActor::StaticClass(), FTransform::Identity, Params);
	if (FxActor)
	{
		const UFourfoldSettingsSubsystem* Settings = UFourfoldSettingsSubsystem::Get(this);
		SetupQuality = Settings ? Settings->GetEffectiveQuality() : 2;
		FxActor->Setup(Impl->Config, SetupQuality);
	}
}

bool UFourfoldFxSubsystem::ReloadConfig()
{
	if (!Impl)
	{
		return false;
	}
	const bool bOk = LoadConfigFile(Impl->Config);
	Impl->Director.SetConfig(Impl->Config);
	if (FxActor)
	{
		FxActor->Setup(Impl->Config, FMath::Max(SetupQuality, 0));
	}
	UE_LOG(LogFourfoldFx, Log, TEXT("fx_config.json reloaded (%s)."), bOk ? TEXT("ok") : TEXT("defaults"));
	return bOk;
}

void UFourfoldFxSubsystem::SetEnabled(bool bInEnabled)
{
	if (bEnabled == bInEnabled)
	{
		return;
	}
	bEnabled = bInEnabled;
	if (!bEnabled)
	{
		if (Impl)
		{
			Impl->Director.Reset();
		}
		if (FxActor)
		{
			FxActor->ReleaseAll();
		}
	}
}

void UFourfoldFxSubsystem::OnScenarioLoaded(const FString& ScenarioId)
{
	if (Impl)
	{
		Impl->Director.Reset();
	}
	if (FxActor)
	{
		FxActor->ReleaseAll();
	}
}

void UFourfoldFxSubsystem::OnSimFrame(const FFourfoldFrame& Frame)
{
	SetEnabled(CVarFourfoldFxEnable.GetValueOnGameThread() != 0);
	if (!bEnabled || !Impl || !Frame.Curr)
	{
		return;
	}
	EnsureActor();
	if (!FxActor)
	{
		return;
	}
	const double T0 = FPlatformTime::Seconds();
	UFourfoldSimSubsystem* Sim = SimWeak.Get();
	UWorld* World = GetWorld();

	ffx::FxFrameIn In;
	In.prev = Frame.Prev ? Frame.Prev : Frame.Curr;
	In.curr = Frame.Curr;
	In.alpha = FMath::Clamp(Frame.Alpha, 0.0f, 1.0f);
	In.events = Frame.Events;
	In.dt = Frame.GameDeltaSeconds;
	In.paused = Frame.bPaused;
	if (Sim && Sim->HasScenario())
	{
		In.arena = &Sim->GetArena();
	}
	// camera (sim space)
	if (APlayerCameraManager* Cam = World ? UGameplayStatics::GetPlayerCameraManager(World, 0) : nullptr)
	{
		const FRotator Rot = Cam->GetCameraRotation();
		const FRotationMatrix RM(Rot);
		In.cam.pos = ToSimV(Cam->GetCameraLocation());
		In.cam.fwd = ToSimDir(RM.GetScaledAxis(EAxis::X));
		In.cam.up = ToSimDir(RM.GetScaledAxis(EAxis::Z));
	}
	// settings
	if (const UFourfoldSettingsSubsystem* Settings = UFourfoldSettingsSubsystem::Get(this))
	{
		const FFourfoldSettings& S = Settings->GetSettings();
		In.quality = Settings->GetEffectiveQuality();
		In.flashes = S.Flashes;
		In.reducedMotion = S.bReducedMotion;
	}
	if (In.quality != SetupQuality)
	{
		SetupQuality = In.quality;
		FxActor->Setup(Impl->Config, SetupQuality);   // grows the pre-warmed pools when the quality goes up
	}
	// fighter bone anchors (hands, feet, chest ...)
	Impl->Anchors.clear();
	if (Sim)
	{
		for (const ff::ActorView& A : Frame.Curr->actors)
		{
			const AFourfoldFighter* F = Sim->FindFighter(A.id);
			if (!F)
			{
				continue;
			}
			ffx::FxAnchors& An = Impl->Anchors.emplace_back();
			An.actor = A.id;
			for (int32 b = 0; b < int32(ffx::Bone::Count); ++b)
			{
				const std::string Name(ffx::kBoneNames[size_t(b)]);
				An.bones[size_t(b)] = ToSimV(F->GetBoneLocation(FName(UTF8_TO_TCHAR(Name.c_str()))));
			}
		}
	}
	In.anchors = &Impl->Anchors;

	const ffx::DrawList& List = Impl->Director.Update(In);
	FxActor->Apply(List, Sim);

	Impl->LastUpdateMs = (FPlatformTime::Seconds() - T0) * 1000.0;
	Impl->PeakUpdateMs = FMath::Max(Impl->PeakUpdateMs * 0.995, Impl->LastUpdateMs);
	if (CVarFourfoldFxStats.GetValueOnGameThread() != 0 && GEngine)
	{
		GEngine->AddOnScreenDebugMessage(uint64(0x5FF0F0), 0.0f, FColor::Cyan, GetDebugLine());
	}
}

FString UFourfoldFxSubsystem::GetDebugLine() const
{
	if (!Impl)
	{
		return TEXT("fx: off");
	}
	const ffx::FxStats& St = Impl->Director.Stats();
	FString Line = FString::Printf(TEXT("fx: %d views (+%d fading), %d one-shots, %d actor fx, %d tris, %.2f ms (peak %.2f)"),
		St.views, St.dyingViews, St.oneShots, St.actorFx, St.triangles, Impl->LastUpdateMs, Impl->PeakUpdateMs);
	if (FxActor)
	{
		Line += TEXT(" | ") + FxActor->GetDebugLine();
	}
	if (St.unmappedBodies > 0)
	{
		Line += FString::Printf(TEXT(" | %d unmapped"), St.unmappedBodies);
	}
	return Line;
}
