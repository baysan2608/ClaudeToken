// Fourfold - UFourfoldSimSubsystem: owns the engine-free ff::Session and steps it at a fixed 60 Hz.
// Each rendered frame: acc += dilated delta x Lab time scale; up to 4 ticks (Lab freeze: only requested steps); the
// local input is polled once per tick (PollInput); then OnFrame is broadcast ONCE with both snapshots, the alpha and
// every event of the ticks stepped. Hit-stop (requested by the feel director / FX) dilates global time for N real
// frames (<= 12 per rolling second). Fighters (one AFourfoldFighter per sim actor) are spawned / destroyed here.
#include "FourfoldSimSubsystem.h"

#include "FourfoldCoords.h"
#include "FourfoldFighter.h"
#include "FourfoldLog.h"
#include "FourfoldSettings.h"
#include "Logic/FFGFeel.h"

#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#include <string>

struct FFourfoldSimImpl
{
	TUniquePtr<ff::Session> Session;
	ff::Snapshot Prev;
	ff::Snapshot Curr;
	std::vector<ff::Event> Events;
	ff::ScenarioOptions Options;
	FString ScenarioId;
	bool bHasScenario = false;
	bool bPaused = false;
	double Acc = 0.0;
	float Alpha = 1.0f;
	uint64 FrameIndex = 0;
	TMap<int32, TWeakObjectPtr<AFourfoldFighter>> Fighters;
	ffg::HitStop HitStop;
	bool bReducedMotion = false;
	float Dilation = 1.0f;
	bool bTouchedDilation = false;
	float CameraYawSim = 3.14159265f;
	FString SaveDir;
};

namespace FourfoldSim
{
	static const ff::Snapshot& EmptySnapshot()
	{
		static const ff::Snapshot S;
		return S;
	}
	static const ff::ArenaView& EmptyArena()
	{
		static const ff::ArenaView A;
		return A;
	}
	static const ff::ScenarioOptions& EmptyOptions()
	{
		static const ff::ScenarioOptions O;
		return O;
	}
	static FString ToF(const std::string& S) { return FString(UTF8_TO_TCHAR(S.c_str())); }
	static std::string ToS(const FString& S) { return std::string(TCHAR_TO_UTF8(*S)); }
}

UFourfoldSimSubsystem* UFourfoldSimSubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UFourfoldSimSubsystem>() : nullptr;
}

bool UFourfoldSimSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}
	const UWorld* World = Cast<UWorld>(Outer);
	return World && (World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE);
}

void UFourfoldSimSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);   // UTickableWorldSubsystem only ticks after its own Initialize ran
	Impl = MakeShared<FFourfoldSimImpl>();
	Impl->SaveDir = FPaths::ProjectSavedDir() / TEXT("Fourfold");
	std::string Error;
	if (!ff::Session::DataOk(&Error))
	{
		UE_LOG(LogFourfold, Error, TEXT("Fourfold sim data failed to load: %s"), *FourfoldSim::ToF(Error));
	}
	ff::SessionConfig Cfg;
	Cfg.seed = 1;
	Impl->Session = MakeUnique<ff::Session>(Cfg);
	FString Text;
	if (FFileHelper::LoadFileToString(Text, *(Impl->SaveDir / TEXT("progress.json"))))
	{
		if (!Impl->Session->LoadProgress(FourfoldSim::ToS(Text)))
		{
			UE_LOG(LogFourfold, Warning, TEXT("progress.json could not be read; starting fresh"));
		}
	}
}

void UFourfoldSimSubsystem::Deinitialize()
{
	if (Impl.IsValid())
	{
		if (Impl->bHasScenario)
		{
			SaveProgress();
		}
		if (Impl->bTouchedDilation)
		{
			ApplyTimeDilation(1.0f);
		}
		for (auto& Pair : Impl->Fighters)
		{
			if (AFourfoldFighter* F = Pair.Value.Get())
			{
				F->Destroy();
			}
		}
		Impl->Fighters.Empty();
	}
	OnFrame.Clear();
	OnScenarioLoaded.Clear();
	OnUiCue.Clear();
	PollInput.Unbind();
	Impl.Reset();
	Super::Deinitialize();
}

TStatId UFourfoldSimSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UFourfoldSimSubsystem, STATGROUP_Tickables);
}

ff::Session& UFourfoldSimSubsystem::GetSession()
{
	check(Impl.IsValid() && Impl->Session.IsValid());
	return *Impl->Session;
}

const ff::Session& UFourfoldSimSubsystem::GetSession() const
{
	check(Impl.IsValid() && Impl->Session.IsValid());
	return *Impl->Session;
}

const ff::Snapshot& UFourfoldSimSubsystem::GetSnapshot() const
{
	return Impl.IsValid() ? Impl->Curr : FourfoldSim::EmptySnapshot();
}

const ff::Snapshot& UFourfoldSimSubsystem::GetPrevSnapshot() const
{
	return Impl.IsValid() ? Impl->Prev : FourfoldSim::EmptySnapshot();
}

float UFourfoldSimSubsystem::GetAlpha() const
{
	return Impl.IsValid() ? Impl->Alpha : 1.0f;
}

bool UFourfoldSimSubsystem::HasScenario() const
{
	return Impl.IsValid() && Impl->bHasScenario;
}

const ff::ScenarioOptions& UFourfoldSimSubsystem::GetScenarioOptions() const
{
	return Impl.IsValid() ? Impl->Options : FourfoldSim::EmptyOptions();
}

const ff::ArenaView& UFourfoldSimSubsystem::GetArena() const
{
	return (Impl.IsValid() && Impl->Session.IsValid() && Impl->bHasScenario) ? Impl->Session->Arena() : FourfoldSim::EmptyArena();
}

uint64 UFourfoldSimSubsystem::GetFrameIndex() const
{
	return Impl.IsValid() ? Impl->FrameIndex : 0;
}

float UFourfoldSimSubsystem::GetCurrentTimeDilation() const
{
	return Impl.IsValid() ? Impl->Dilation : 1.0f;
}

bool UFourfoldSimSubsystem::LoadScenario(const FString& ScenarioId, const ff::ScenarioOptions& Options)
{
	if (!Impl.IsValid() || !Impl->Session.IsValid())
	{
		return false;
	}
	if (!Impl->Session->LoadScenario(FourfoldSim::ToS(ScenarioId), Options))
	{
		UE_LOG(LogFourfold, Warning, TEXT("LoadScenario('%s') failed"), *ScenarioId);
		return false;
	}
	Impl->ScenarioId = FourfoldSim::ToF(Impl->Session->ScenarioId());
	Impl->Options = Options;
	Impl->bHasScenario = true;
	Impl->Curr = Impl->Session->GetSnapshot();
	Impl->Prev = Impl->Curr;
	Impl->Acc = 0.0;
	Impl->Alpha = 1.0f;
	Impl->HitStop.Reset();
	SyncFighters(true);
	if (Options.autoplay.empty())
	{
		SaveProgress();   // the last scenario is part of the progress (as in the Godot build)
	}
	UE_LOG(LogFourfold, Log, TEXT("Scenario '%s' loaded (%d actors)"), *Impl->ScenarioId, int32(Impl->Curr.actors.size()));
	OnScenarioLoaded.Broadcast(Impl->ScenarioId);
	return true;
}

bool UFourfoldSimSubsystem::RestartScenario()
{
	if (!HasScenario())
	{
		return false;
	}
	const FString Id = Impl->ScenarioId;
	const ff::ScenarioOptions Opts = Impl->Options;
	return LoadScenario(Id, Opts);
}

FString UFourfoldSimSubsystem::GetScenarioId() const
{
	return Impl.IsValid() ? Impl->ScenarioId : FString();
}

AFourfoldFighter* UFourfoldSimSubsystem::FindFighter(int32 SimActorId) const
{
	if (!Impl.IsValid())
	{
		return nullptr;
	}
	const TWeakObjectPtr<AFourfoldFighter>* Found = Impl->Fighters.Find(SimActorId);
	return Found ? Found->Get() : nullptr;
}

int32 UFourfoldSimSubsystem::GetPlayerActorId() const
{
	return Impl.IsValid() ? Impl->Curr.player_id : -1;
}

void UFourfoldSimSubsystem::SetPaused(bool bInPaused)
{
	if (Impl.IsValid())
	{
		Impl->bPaused = bInPaused;
		if (bInPaused)
		{
			Impl->HitStop.Reset();
			ApplyTimeDilation(1.0f);
		}
	}
}

bool UFourfoldSimSubsystem::IsPaused() const
{
	return Impl.IsValid() && Impl->bPaused;
}

void UFourfoldSimSubsystem::RequestHitStop(int32 Frames)
{
	if (Impl.IsValid() && !Impl->bPaused)
	{
		Impl->HitStop.Request(Frames, FPlatformTime::Seconds(), Impl->bReducedMotion);
	}
}

void UFourfoldSimSubsystem::RequestSlowmo(float RealSeconds)
{
	if (Impl.IsValid())
	{
		Impl->HitStop.RequestSlowmo(RealSeconds);
	}
}

void UFourfoldSimSubsystem::SetReducedMotion(bool bReduced)
{
	if (Impl.IsValid())
	{
		Impl->bReducedMotion = bReduced;
	}
}

void UFourfoldSimSubsystem::StepOnce()
{
	ff::InputFrame Input;
	float Yaw = Impl->CameraYawSim;
	PollInput.ExecuteIfBound(Input, Yaw);
	Impl->CameraYawSim = Yaw;
	Impl->Prev = Impl->Curr;
	Impl->Session->Step(Input, Yaw);
	Impl->Session->TakeEvents(Impl->Events);
	Impl->Curr = Impl->Session->GetSnapshot();
}

void UFourfoldSimSubsystem::Tick(float DeltaTime)
{
	if (!Impl.IsValid() || !Impl->Session.IsValid())
	{
		return;
	}
	const float GameDt = FMath::Clamp(DeltaTime, 0.0f, 0.25f);
	const float RealDt = FMath::Clamp(float(FApp::GetDeltaTime()), 0.0f, 0.25f);
	Impl->Events.clear();
	int32 Ticks = 0;
	if (Impl->bHasScenario && !Impl->bPaused)
	{
		ff::Session& S = *Impl->Session;
		if (S.Frozen())
		{
			const int32 Requests = FMath::Clamp(S.TakeStepRequests(), 0, 60);
			for (int32 i = 0; i < Requests; ++i)
			{
				StepOnce();
				++Ticks;
			}
			Impl->Acc = 0.0;
			Impl->Alpha = 1.0f;
		}
		else
		{
			Impl->Acc += double(GameDt) * double(FMath::Max(S.TimeScale(), 0.0f));
			while (Impl->Acc >= ff::kSimDt && Ticks < 4)
			{
				StepOnce();
				Impl->Acc -= ff::kSimDt;
				++Ticks;
			}
			if (Impl->Acc > ff::kSimDt)
			{
				Impl->Acc = ff::kSimDt;   // a long hitch: drop time instead of spiralling
			}
			Impl->Alpha = FMath::Clamp(float(Impl->Acc / ff::kSimDt), 0.0f, 1.0f);
		}
		if (Ticks > 0)
		{
			SyncFighters(false);
		}
	}

	FFourfoldFrame Frame;
	Frame.Prev = &Impl->Prev;
	Frame.Curr = &Impl->Curr;
	Frame.Alpha = Impl->Alpha;
	Frame.Events = &Impl->Events;
	Frame.TicksStepped = Ticks;
	Frame.RealDeltaSeconds = RealDt;
	Frame.GameDeltaSeconds = Impl->bPaused ? 0.0f : GameDt;
	Frame.FrameIndex = Impl->FrameIndex;
	Frame.bPaused = Impl->bPaused;
	OnFrame.Broadcast(Frame);

	// Progress worth keeping right away (a mastered challenge).
	for (const ff::Event& E : Impl->Events)
	{
		if (E.type == "app_challenge" && E.data["done"].as_bool(false))
		{
			SaveProgress();
			break;
		}
	}

	// Hit-stop / slow-motion for the NEXT frame (requests arrived during OnFrame).
	const float Wanted = Impl->bPaused ? 1.0f : Impl->HitStop.FrameTick(FPlatformTime::Seconds(), RealDt);
	ApplyTimeDilation(Wanted);
	++Impl->FrameIndex;
}

void UFourfoldSimSubsystem::ApplyTimeDilation(float Dilation)
{
	if (!Impl.IsValid() || FMath::IsNearlyEqual(Dilation, Impl->Dilation))
	{
		return;
	}
	Impl->Dilation = Dilation;
	Impl->bTouchedDilation = true;
	if (UWorld* World = GetWorld())
	{
		UGameplayStatics::SetGlobalTimeDilation(World, Dilation);
	}
}

void UFourfoldSimSubsystem::SyncFighters(bool bRespawnAll)
{
	UWorld* World = GetWorld();
	if (!World || !Impl.IsValid())
	{
		return;
	}
	if (bRespawnAll)
	{
		for (auto& Pair : Impl->Fighters)
		{
			if (AFourfoldFighter* F = Pair.Value.Get())
			{
				F->Destroy();
			}
		}
		Impl->Fighters.Empty();
	}
	TSet<int32> Alive;
	for (const ff::ActorView& A : Impl->Curr.actors)
	{
		Alive.Add(A.id);
		TWeakObjectPtr<AFourfoldFighter>& Slot = Impl->Fighters.FindOrAdd(A.id);
		if (Slot.IsValid())
		{
			continue;
		}
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Params.ObjectFlags |= RF_Transient;
		const FTransform Xf(FF::FacingToRotator(A.facing), FF::ToUE(A.pos));
		AFourfoldFighter* F = World->SpawnActor<AFourfoldFighter>(AFourfoldFighter::StaticClass(), Xf, Params);
		if (F)
		{
			F->InitFromSim(A, Impl->Curr);
			Slot = F;
		}
	}
	for (auto It = Impl->Fighters.CreateIterator(); It; ++It)
	{
		if (!Alive.Contains(It.Key()))
		{
			if (AFourfoldFighter* F = It.Value().Get())
			{
				F->Destroy();
			}
			It.RemoveCurrent();
		}
	}
}

void UFourfoldSimSubsystem::SaveProgress()
{
	if (!Impl.IsValid() || !Impl->Session.IsValid())
	{
		return;
	}
	IFileManager::Get().MakeDirectory(*Impl->SaveDir, true);
	const FString Text = FourfoldSim::ToF(Impl->Session->SaveProgress());
	if (!FFileHelper::SaveStringToFile(Text, *(Impl->SaveDir / TEXT("progress.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		UE_LOG(LogFourfold, Warning, TEXT("Could not write %s/progress.json"), *Impl->SaveDir);
	}
}

bool UFourfoldSimSubsystem::SaveLabTuning()
{
	if (!Impl.IsValid() || !Impl->Session.IsValid())
	{
		return false;
	}
	IFileManager::Get().MakeDirectory(*Impl->SaveDir, true);
	const FString Text = FourfoldSim::ToF(Impl->Session->LabSaveTuning());
	return FFileHelper::SaveStringToFile(Text, *(Impl->SaveDir / TEXT("lab_tuning.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

bool UFourfoldSimSubsystem::LoadLabTuning()
{
	if (!Impl.IsValid() || !Impl->Session.IsValid())
	{
		return false;
	}
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *(Impl->SaveDir / TEXT("lab_tuning.json"))))
	{
		return false;
	}
	return Impl->Session->LabLoadTuning(FourfoldSim::ToS(Text));
}
