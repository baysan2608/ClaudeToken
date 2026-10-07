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
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "UnrealClient.h"

#include <string>
#include <vector>

DEFINE_LOG_CATEGORY_STATIC(LogFourfoldFx, Log, All);

static TAutoConsoleVariable<int32> CVarFourfoldFxEnable(TEXT("ff.fx.Enable"), 1,
	TEXT("Fourfold effects: 1 = on, 0 = off (every effect component is released)."), ECVF_Default);
static TAutoConsoleVariable<int32> CVarFourfoldFxStats(TEXT("ff.fx.Stats"), 0,
	TEXT("Fourfold effects: 1 = print the per-frame counters on screen."), ECVF_Default);
static TAutoConsoleVariable<int32> CVarFourfoldFxNiagara(TEXT("ff.fx.Niagara"), 1,
	TEXT("Fourfold effects: 1 = Niagara cue systems on (fx_config.json \"niagara\"), 0 = procedural one-shots only."),
	ECVF_Default);
static TAutoConsoleVariable<int32> CVarFourfoldFxPrewarm(TEXT("ff.fx.Prewarm"), 3,
	TEXT("Fourfold effects: pre-warm once the scenario is in view, hidden behind the floor on the camera's view ray, so ")
	TEXT("first uses mid-fight do not hitch on pipeline (PSO) / shader creation. Bits: 1 = play each Niagara cue system ")
	TEXT("once (small), 2 = draw every FX material and static FX mesh for a few frames. 0 = off (A/B)."), ECVF_Default);
static TAutoConsoleVariable<int32> CVarFourfoldFxDebris(TEXT("ff.fx.Debris"), 1,
	TEXT("Fourfold effects: 1 = broken stones / walls throw Chaos physics pieces (visual only), 0 = procedural chips."),
	ECVF_Default);
static TAutoConsoleVariable<float> CVarFourfoldFxShowcase(TEXT("ff.fx.Showcase"), 0.0f,
	TEXT("Fourfold effects: N > 0 = every N seconds (the first one N seconds after switching it on) play the next cue of ")
	TEXT("a fixed list (blast, stone, metal, glass, ice, lightning, sand, water, steam, magma, fire cone, plant) 3 m ")
	TEXT("beyond the first fighter through the normal event path (look checks and screenshots; the log names each cue)."),
	ECVF_Default);
static TAutoConsoleVariable<FString> CVarFourfoldFxShowcaseFilter(TEXT("ff.fx.ShowcaseFilter"), TEXT(""),
	TEXT("Fourfold effects: only showcase cues whose name contains this text (e.g. break, burst/stone); empty = all."),
	ECVF_Default);
static TAutoConsoleVariable<FString> CVarFourfoldFxShowcaseShots(TEXT("ff.fx.ShowcaseShots"), TEXT(""),
	TEXT("Fourfold effects: list of seconds (0.1|0.5) after each showcase cue (and after the pre-warm) at which to save ")
	TEXT("<-FFShotDir or Saved/Shots>/showcase_<n>_<cue>_<ms>.png (prewarm_<ms>.png)."), ECVF_Default);

struct FFourfoldFxImpl
{
	ffx::FxDirector Director;
	ffx::FxConfig Config;
	std::vector<ffx::FxAnchors> Anchors;
	std::vector<ff::Event> Events;   // sim events + showcase cues (only while ff.fx.Showcase is on)
	double LastUpdateMs = 0.0;
	double PeakUpdateMs = 0.0;
	double ShowcaseNext = -1.0;   // < 0: showcase off (the first cue comes one interval after it is switched on)
	int32 ShowcaseIndex = 0;
	TArray<TPair<double, FString>> ShowcaseShots;   // (world time, file) still to capture
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

	// ff.fx.Showcase: one synthetic "fx" event (the shape the sim emits) 4 m in front of the camera.
	ff::Event ShowcaseEvent(int32 Index, const ffx::FxFrameIn& In, int ActorId, FString& OutName)
	{
		static const char* const kCues[][2] = {{"burst", "blast"}, {"burst", "stone"}, {"burst", "metal"}, {"burst", "glass"},
			{"burst", "ice"}, {"burst", "lightning"}, {"burst", "sand"}, {"burst", "water"}, {"burst", "steam"},
			{"erupt", "magma"}, {"cone", "flame"}, {"erupt", "plant"}, {"break", "rock"}, {"break", "wall"}};
		constexpr int32 kNum = int32(sizeof(kCues) / sizeof(kCues[0]));
		// ff.fx.ShowcaseFilter: the Index-th cue among those whose "fx/mat" name contains the filter
		const FString Filter = CVarFourfoldFxShowcaseFilter.GetValueOnGameThread();
		int32 Matches[kNum];
		int32 NumMatches = 0;
		for (int32 i = 0; i < kNum; ++i)
		{
			const FString N = FString(UTF8_TO_TCHAR(kCues[i][0])) + TEXT("/") + UTF8_TO_TCHAR(kCues[i][1]);
			if (Filter.IsEmpty() || N.Contains(Filter))
			{
				Matches[NumMatches++] = i;
			}
		}
		const char* const* Cue = kCues[NumMatches > 0 ? Matches[Index % NumMatches] : Index % kNum];
		ffx::Vec3 Fwd(In.cam.fwd.x, 0.0f, In.cam.fwd.z);
		Fwd = ffx::Norm(Fwd, ffx::Vec3(0.0f, 0.0f, -1.0f));
		const ffx::Vec3 Right(-Fwd.z, 0.0f, Fwd.x);
		// 3 m beyond the first fighter as seen from the camera (or 6 m ahead of the camera)
		const ff::ActorView* A = In.curr ? In.curr->FindActor(ActorId) : nullptr;
		ffx::Vec3 At = A ? A->pos + Fwd * 3.0f : In.cam.pos + Fwd * 6.0f;
		At.y = ffx::GroundUnder(In.arena, ffx::Vec3(At.x, At.y + 2.0f, At.z)) + 1.0f;
		const bool bCone = FCStringAnsi::Strcmp(Cue[0], "cone") == 0;
		OutName = FString(UTF8_TO_TCHAR(Cue[0])) + TEXT("/") + UTF8_TO_TCHAR(Cue[1]);
		if (FCStringAnsi::Strcmp(Cue[0], "break") == 0)
		{
			// a stone (1 m up, falling) or a wall breaking into physics debris with no sim body behind it
			const bool bWall = FCStringAnsi::Strcmp(Cue[1], "wall") == 0;
			ff::Dict B;
			B.set("kind", ff::Value(Cue[1]));
			B.set("pos", ff::Value(bWall ? ffx::Vec3(At.x, At.y - 1.0f, At.z) : At));
			B.set("yaw", ff::Value(std::atan2(Fwd.x, Fwd.z)));
			B.set("seed", ff::Value(Index));
			ff::Event E;
			E.type = "fx_test_break";
			E.data = ff::Value(B);
			return E;
		}
		ff::Dict D;
		D.set("fx", ff::Value(Cue[0]));
		D.set("mat", ff::Value(Cue[1]));
		D.set("actor", ff::Value(ActorId));
		D.set("tier", ff::Value(3));
		D.set("pos", ff::Value(bCone ? At - Right * 2.0f : At));
		D.set("dir", ff::Value(bCone ? Right : ffx::Vec3(0.0f, 1.0f, 0.0f)));
		D.set("radius", ff::Value(2.0f));
		D.set("length", ff::Value(4.0f));
		D.set("power", ff::Value(30.0f));
		ff::Event E;
		E.type = "fx";
		E.data = ff::Value(D);
		return E;
	}
}

// ff.fx.ShowcaseShots: queue "<prefix>_<ms>.png" screenshots that many seconds after Now (showcase cues, pre-warm).
static void QueueShots(TArray<TPair<double, FString>>& Shots, double Now, const FString& Prefix)
{
	TArray<FString> Offsets;
	static const TCHAR* const kSeparators[] = {TEXT(","), TEXT("|"), TEXT(" ")};   // -ExecCmds splits on commas: use 0.1|0.5
	CVarFourfoldFxShowcaseShots.GetValueOnGameThread().ParseIntoArray(Offsets, kSeparators, 3);
	FString Dir;
	if (!FParse::Value(FCommandLine::Get(), TEXT("-FFShotDir="), Dir))
	{
		Dir = FPaths::ProjectSavedDir() / TEXT("Shots");
	}
	for (const FString& O : Offsets)
	{
		const double T = FCString::Atod(*O);
		Shots.Add({Now + T, Dir / FString::Printf(TEXT("%s_%04d.png"), *Prefix, int32(T * 1000.0))});
	}
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
	bDebrisArenaDirty = true;
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
		if (bDebrisArenaDirty)
		{
			FxActor->BuildDebrisArena(Sim->GetArena());
			bDebrisArenaDirty = false;
		}
	}
	In.physicsDebris = CVarFourfoldFxDebris.GetValueOnGameThread() != 0 && FxActor->IsDebrisReady();
	In.debrisImpacts = FxActor->GetDebrisImpacts();
	// camera (sim space)
	APlayerCameraManager* Cam = World ? UGameplayStatics::GetPlayerCameraManager(World, 0) : nullptr;
	if (Cam)
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
	const bool bNiagara = CVarFourfoldFxNiagara.GetValueOnGameThread() != 0;
	In.niagaraLoaded = bNiagara ? FxActor->GetNiagaraLoadedMask() : 0;
	In.niagaraLoopsLoaded = bNiagara ? FxActor->GetNiagaraLoopsLoadedMask() : 0;
	// pre-warm: Niagara cue systems and FX materials drawn once on the view ray behind the floor (in the frustum, so
	// their pipelines get built, but hidden); retried every frame until the camera looks at the floor
	const int32 Prewarm = CVarFourfoldFxPrewarm.GetValueOnGameThread();
	const bool bPrewarmNiagara = (Prewarm & 1) != 0 && bNiagara && FxActor->NeedsNiagaraPrewarm();
	const bool bPrewarmMaterials = (Prewarm & 2) != 0 && FxActor->NeedsMaterialPrewarm();
	ffx::Vec3 Hidden;
	if (Cam && (bPrewarmNiagara || bPrewarmMaterials) &&
		ffx::PointBehindFloor(In.arena, In.cam.pos, ffx::Norm(In.cam.fwd), 3.5f, 1.5f, 30.0f, Hidden))
	{
		const FVector Where = FF::ToUE(Hidden);
		const int32 Num = bPrewarmNiagara ? FxActor->PrewarmNiagara(Where) : 0;
		if (bPrewarmMaterials)
		{
			FxActor->PrewarmMaterials(Where);
		}
		UE_LOG(LogFourfoldFx, Display, TEXT("Pre-warm at %s: %d Niagara systems%s"), *Where.ToCompactString(), Num,
			bPrewarmMaterials ? TEXT(", FX materials") : TEXT(""));
		CSV_EVENT_GLOBAL(TEXT("ff.fx prewarm %d%s"), Num, bPrewarmMaterials ? TEXT(" +materials") : TEXT(""));
		if (World)
		{
			QueueShots(Impl->ShowcaseShots, World->GetTimeSeconds(), TEXT("prewarm"));   // look check: nothing may show
		}
	}
	// look checks: inject the next showcase cue
	const float Showcase = CVarFourfoldFxShowcase.GetValueOnGameThread();
	if (Showcase <= 0.0f || !World)
	{
		Impl->ShowcaseNext = -1.0;
	}
	else if (Impl->ShowcaseNext < 0.0)
	{
		Impl->ShowcaseNext = World->GetTimeSeconds() + double(Showcase);   // let the scenario settle first
	}
	else if (World->GetTimeSeconds() >= Impl->ShowcaseNext)
	{
		Impl->ShowcaseNext = World->GetTimeSeconds() + double(Showcase);
		Impl->Events = Frame.Events ? *Frame.Events : std::vector<ff::Event>();
		FString Name;
		const int ActorId = Frame.Curr->actors.empty() ? -1 : Frame.Curr->actors[0].id;
		Impl->Events.push_back(ShowcaseEvent(Impl->ShowcaseIndex++, In, ActorId, Name));
		In.events = &Impl->Events;
		UE_LOG(LogFourfoldFx, Display, TEXT("Showcase %d: %s at %.1fs"), Impl->ShowcaseIndex - 1, *Name, World->GetTimeSeconds());
		CSV_EVENT_GLOBAL(TEXT("ff.fx showcase %s"), *Name);
		QueueShots(Impl->ShowcaseShots, World->GetTimeSeconds(),
			FString::Printf(TEXT("showcase_%02d_%s"), Impl->ShowcaseIndex - 1, *Name.Replace(TEXT("/"), TEXT("_"))));
	}
	// one pending showcase screenshot per frame (FScreenshotRequest holds a single request)
	for (int32 i = 0; World && i < Impl->ShowcaseShots.Num(); ++i)
	{
		if (World->GetTimeSeconds() >= Impl->ShowcaseShots[i].Key)
		{
			FScreenshotRequest::RequestScreenshot(Impl->ShowcaseShots[i].Value, false, false);
			UE_LOG(LogFourfoldFx, Display, TEXT("Showcase shot %s"), *Impl->ShowcaseShots[i].Value);
			Impl->ShowcaseShots.RemoveAt(i);
			break;
		}
	}

	const ffx::DrawList& List = Impl->Director.Update(In);
	FxActor->ClearDebrisImpacts();   // consumed (new hits arrive during this frame's physics)
	FxActor->Apply(List, Sim, bNiagara);

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
