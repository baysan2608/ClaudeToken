// FourfoldAudio - UFourfoldAudioSubsystem: the Unreal glue around the engine-free logic island (Private/Logic).
// Everything that decides WHAT plays (rules, limiters, voice caps, loop fades, steps, ambience schedule) lives in Logic and is
// unit-tested; this file only executes the decisions with the engine's audio API (UGameplayStatics / UAudioComponent).
#include "FourfoldAudioSubsystem.h"

#include "Components/AudioComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundWave.h"

#include "FourfoldCoords.h"
#include "FourfoldSettings.h"
#include "FourfoldSimSubsystem.h"

#include "Logic/FFALoops.h"
#include "Logic/FFAManifest.h"
#include "Logic/FFAMix.h"
#include "Logic/FFARules.h"
#include "Logic/FFAVoices.h"

DEFINE_LOG_CATEGORY_STATIC(LogFourfoldAudio, Log, All);

namespace
{
constexpr const TCHAR* kManifestFile = TEXT("Fourfold/Data/sfx_manifest.json");

FString ToFString(const std::string& S)
{
	return FString(UTF8_TO_TCHAR(S.c_str()));
}

std::string ToStd(const FString& S)
{
	const FTCHARToUTF8 Conv(*S);
	return std::string(Conv.Get(), static_cast<size_t>(Conv.Length()));
}

/** "/Game/Fourfold/Audio/SFX/S_name" -> "/Game/Fourfold/Audio/SFX/S_name.S_name" (object path for LoadObject). */
FString ObjectPathOf(const std::string& Asset)
{
	const FString Pkg = ToFString(Asset);
	const FString Name = FPackageName::GetShortName(Pkg);
	return Pkg + TEXT(".") + Name;
}
}  // namespace

/** A delayed one-shot (rule play with delay_s). */
struct FFourfoldAudioPending
{
	ffa::PlayRequest Play;
	double Due = 0.0;
};

/** Private state: manifest, logic objects, reusable per-frame buffers. */
struct FFourfoldAudioImpl
{
	ffa::Manifest Manifest;
	ffa::RuleEngine Rules;
	ffa::StepTracker Steps;
	ffa::Limiters Limits;
	ffa::VoiceBook Voices;
	ffa::Ducker Duck;
	ffa::LoopTracker Loops;
	ffa::AmbienceScheduler Ambience;
	ffa::Rng Random{0xA17D10u};
	ffa::BusVolumes Volumes;

	bool bReady = false;
	double Clock = 0.0;                    // real seconds since the subsystem started (never dilated)
	float DuckDb = 0.0f;
	int32 Dropped = 0;                     // plays refused by limiters / voice caps (diagnostic)
	int32 Played = 0;
	TArray<FFourfoldAudioPending> Pending;

	std::vector<ffa::PlayRequest> PlayBuf;
	std::vector<ffa::HoldRequest> HoldBuf;
	std::vector<ffa::LoopWant> WantBuf;
	std::vector<ffa::StepOut> StepBuf;
	std::vector<ffa::AccentPlay> AccentBuf;
};

// ------------------------------------------------------------------------------------------------------------ lifecycle

UFourfoldAudioSubsystem* UFourfoldAudioSubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UFourfoldAudioSubsystem>() : nullptr;
}

bool UFourfoldAudioSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}
	if (IsRunningDedicatedServer() || IsRunningCommandlet())
	{
		return false;   // no audio device
	}
	const UWorld* World = Cast<UWorld>(Outer);
	return World && (World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE);
}

void UFourfoldAudioSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Collection.InitializeDependency<UFourfoldSimSubsystem>();   // the sim hub exists before we bind to it
	Super::Initialize(Collection);
	Impl = MakeShared<FFourfoldAudioImpl>();
}

void UFourfoldAudioSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (!Impl.IsValid() || Impl->bReady)
	{
		return;
	}
	if (!LoadManifest())
	{
		UE_LOG(LogFourfoldAudio, Warning, TEXT("sfx_manifest.json missing or invalid: the game stays silent (run the editor setup, Fourfold > Build all)."));
		return;
	}
	UFourfoldSimSubsystem* Sim = UFourfoldSimSubsystem::Get(this);
	if (Sim == nullptr)
	{
		UE_LOG(LogFourfoldAudio, Warning, TEXT("no UFourfoldSimSubsystem in this world: audio not bound."));
		return;
	}
	FrameHandle = Sim->OnFrame.AddUObject(this, &UFourfoldAudioSubsystem::HandleFrame);
	UiHandle = Sim->OnUiCue.AddUObject(this, &UFourfoldAudioSubsystem::HandleUiCue);
	ScenarioHandle = Sim->OnScenarioLoaded.AddUObject(this, &UFourfoldAudioSubsystem::HandleScenarioLoaded);
	Impl->bReady = true;
	UE_LOG(LogFourfoldAudio, Log, TEXT("audio ready: %d sounds, %d event rules."), static_cast<int32>(Impl->Manifest.sounds.size()),
	       static_cast<int32>(Impl->Manifest.CountEventRules()));
}

void UFourfoldAudioSubsystem::Deinitialize()
{
	if (UFourfoldSimSubsystem* Sim = UFourfoldSimSubsystem::Get(this))
	{
		Sim->OnFrame.Remove(FrameHandle);
		Sim->OnUiCue.Remove(UiHandle);
		Sim->OnScenarioLoaded.Remove(ScenarioHandle);
	}
	FrameHandle.Reset();
	UiHandle.Reset();
	ScenarioHandle.Reset();
	for (TPair<FString, TObjectPtr<UAudioComponent>>& Pair : LoopComps)
	{
		if (UAudioComponent* C = Pair.Value)
		{
			C->Stop();
			C->DestroyComponent();
		}
	}
	LoopComps.Empty();
	for (TPair<uint64, TWeakObjectPtr<UAudioComponent>>& Pair : VoiceComps)
	{
		if (UAudioComponent* C = Pair.Value.Get())
		{
			C->Stop();
		}
	}
	VoiceComps.Empty();
	if (Impl.IsValid())
	{
		Impl->bReady = false;
	}
	Super::Deinitialize();
}

bool UFourfoldAudioSubsystem::IsReady() const
{
	return Impl.IsValid() && Impl->bReady;
}

bool UFourfoldAudioSubsystem::LoadManifest()
{
	FFourfoldAudioImpl& I = *Impl;
	const FString Path = FPaths::ProjectContentDir() / kManifestFile;
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *Path))
	{
		UE_LOG(LogFourfoldAudio, Warning, TEXT("cannot read %s"), *Path);
		return false;
	}
	std::string Error;
	if (!I.Manifest.Parse(ToStd(Text), &Error))
	{
		UE_LOG(LogFourfoldAudio, Warning, TEXT("%s: %s"), *Path, *ToFString(Error));
		return false;
	}
	I.Rules.SetManifest(&I.Manifest);
	I.Limits.Configure(I.Manifest.mix.limiters);
	I.Voices.Configure(I.Manifest.mix.global_voices);
	I.Duck.Configure(I.Manifest.mix.duck);
	I.Loops.Configure(I.Manifest.mix.fade, I.Manifest.mix.loop_voices);
	I.Ambience.Configure(I.Manifest.accents);

	// Load every sound now (a few ms each at most): a first-use load would hitch the frame the sound is needed.
	int32 Missing = 0;
	for (const auto& Pair : I.Manifest.sounds)
	{
		if (GetSound(ToFString(Pair.first)) == nullptr)
		{
			++Missing;
		}
	}
	if (Missing > 0)
	{
		UE_LOG(LogFourfoldAudio, Warning, TEXT("%d of %d sounds are missing (import the audio: Fourfold > Build one part > audio)."), Missing,
		       static_cast<int32>(I.Manifest.sounds.size()));
	}
	return true;
}

// ------------------------------------------------------------------------------------------------------------ assets

USoundBase* UFourfoldAudioSubsystem::GetSound(const FString& SoundName)
{
	if (const TObjectPtr<USoundBase>* Found = SoundCache.Find(SoundName))
	{
		return Found->Get();
	}
	USoundBase* Sound = nullptr;
	if (Impl.IsValid())
	{
		if (const ffa::SoundDef* Def = Impl->Manifest.Find(ToStd(SoundName)))
		{
			Sound = LoadObject<USoundBase>(nullptr, *ObjectPathOf(Def->asset));
			if (Sound == nullptr)
			{
				UE_LOG(LogFourfoldAudio, Verbose, TEXT("sound asset not found: %s"), *ToFString(Def->asset));
			}
			else if (USoundWave* Wave = Cast<USoundWave>(Sound))
			{
				Wave->bLooping = Def->loop;   // the manifest is the truth: loops loop, one-shots never do
			}
		}
	}
	SoundCache.Add(SoundName, Sound);
	return Sound;
}

USoundAttenuation* UFourfoldAudioSubsystem::GetAttenuation(const FString& Class)
{
	if (const TObjectPtr<USoundAttenuation>* Found = Attenuations.Find(Class))
	{
		return Found->Get();
	}
	USoundAttenuation* Atten = nullptr;
	if (Impl.IsValid())
	{
		const auto It = Impl->Manifest.mix.attenuation.find(ToStd(Class));
		if (It != Impl->Manifest.mix.attenuation.end())
		{
			const ffa::AttenDef& D = It->second;
			Atten = NewObject<USoundAttenuation>(this);
			FSoundAttenuationSettings& S = Atten->Attenuation;
			S.bAttenuate = true;
			S.bSpatialize = true;
			S.DistanceAlgorithm = EAttenuationDistanceModel::NaturalSound;
			S.AttenuationShape = EAttenuationShape::Sphere;
			S.AttenuationShapeExtents = FVector(D.inner_cm, 0.0, 0.0);   // sphere: X = radius with full volume
			S.FalloffDistance = FMath::Max(D.max_cm - D.inner_cm, 1.0f);   // distance over which the level falls to dBAttenuationAtMax
			S.dBAttenuationAtMax = FMath::Clamp(D.db_at_max, -60.0f, 0.0f);
			S.FalloffMode = ENaturalSoundFalloffMode::Continues;
		}
	}
	Attenuations.Add(Class, Atten);
	return Atten;
}

// ------------------------------------------------------------------------------------------------------------ mixing

void UFourfoldAudioSubsystem::RefreshVolumes()
{
	if (const UFourfoldSettingsSubsystem* Settings = UFourfoldSettingsSubsystem::Get(this))
	{
		const FFourfoldSettings& S = Settings->GetSettings();
		Impl->Volumes.master = S.MasterVolume;
		Impl->Volumes.sfx = S.SfxVolume;
		Impl->Volumes.ui = S.UiVolume;
		Impl->Volumes.ambience = S.AmbienceVolume;
	}
}

void UFourfoldAudioSubsystem::StopVoice(uint64 VoiceId)
{
	if (TWeakObjectPtr<UAudioComponent>* Found = VoiceComps.Find(VoiceId))
	{
		if (UAudioComponent* C = Found->Get())
		{
			C->FadeOut(Impl->Manifest.mix.steal_fade_s, 0.0f);
		}
		VoiceComps.Remove(VoiceId);
	}
}

bool UFourfoldAudioSubsystem::ExecutePlay(const ffa::PlayRequest& R)
{
	FFourfoldAudioImpl& I = *Impl;
	const ffa::SoundDef* Def = I.Manifest.Find(R.sound);
	if (Def == nullptr)
	{
		return false;
	}
	// named limiter of the rule, then the global stinger gap (sounds of the "system" family: charge tiers, counters, clashes)
	if (!I.Limits.Peek(R.limit, R.limit_key, I.Clock) || (Def->stinger && !I.Limits.Peek("sting", -1, I.Clock)))
	{
		++I.Dropped;
		return false;
	}
	USoundBase* Sound = GetSound(ToFString(R.sound));
	if (Sound == nullptr)
	{
		return false;
	}
	const float Pitch = FMath::Max(R.pitch * (1.0f + I.Random.Range(-Def->pitch_var, Def->pitch_var)), 0.1f);
	const ffa::Decision D = I.Voices.Admit(*Def, I.Clock, static_cast<double>(Def->duration_s / Pitch));
	for (uint64 Stolen : D.steal)
	{
		StopVoice(Stolen);
	}
	if (!D.play)
	{
		++I.Dropped;
		return false;
	}
	I.Limits.Commit(R.limit, R.limit_key, I.Clock);
	if (Def->stinger)
	{
		I.Limits.Commit("sting", -1, I.Clock);
	}

	const int32 Bus = I.Manifest.BusIndex(Def->bus);
	const float Db = Def->gain_db + R.gain_db + I.Manifest.mix.bus_gain_db[Bus] + (Bus == 2 ? I.DuckDb : 0.0f);
	const float Volume = ffa::DbToLinear(Db) * I.Volumes.For(Bus);
	UAudioComponent* Comp = nullptr;
	if (R.two_d || !R.has_pos || Def->attenuation == "2d")
	{
		Comp = UGameplayStatics::SpawnSound2D(this, Sound, Volume, Pitch);
	}
	else
	{
		Comp = UGameplayStatics::SpawnSoundAtLocation(this, Sound, FF::ToUE(R.pos), FRotator::ZeroRotator, Volume, Pitch, 0.0f,
		                                              GetAttenuation(ToFString(Def->attenuation)));
	}
	if (Comp != nullptr)
	{
		if (Bus != 0)
		{
			Comp->SetUISound(true);   // UI and ambience keep playing while the game menu pauses the sim
		}
		VoiceComps.Add(D.id, TWeakObjectPtr<UAudioComponent>(Comp));
	}
	++I.Played;
	if (I.Duck.IsTrigger(R.sound))
	{
		I.Duck.Trigger();
	}
	return true;
}

bool UFourfoldAudioSubsystem::PlaySoundByName(FName SoundName, const FVector* WorldLocationUE, float ExtraGainDb)
{
	if (!IsReady())
	{
		return false;
	}
	ffa::PlayRequest R;
	R.sound = ToStd(SoundName.ToString());
	R.gain_db = ExtraGainDb;
	if (WorldLocationUE != nullptr)
	{
		R.has_pos = true;
		const ff::Vec3 P = FF::ToSim(*WorldLocationUE);
		R.pos = P;
	}
	else
	{
		R.two_d = true;
	}
	return ExecutePlay(R);
}

bool UFourfoldAudioSubsystem::PlayUiCue(FName Cue)
{
	if (!IsReady())
	{
		return false;
	}
	const auto It = Impl->Manifest.ui.find(ToStd(Cue.ToString()));
	if (It == Impl->Manifest.ui.end())
	{
		return false;
	}
	ffa::PlayRequest R;
	R.sound = It->second;
	R.two_d = true;
	return ExecutePlay(R);
}

void UFourfoldAudioSubsystem::HandleUiCue(FName Cue)
{
	PlayUiCue(Cue);
}

void UFourfoldAudioSubsystem::HandleScenarioLoaded(const FString& /*ScenarioId*/)
{
	if (!Impl.IsValid() || !Impl->bReady)
	{
		return;
	}
	// A new scenario: bodies of the old one are gone from the next snapshot, so their loops fade out where they were by the normal
	// loop logic (no snap, and the ambience beds keep playing through round resets).
	Impl->Steps.Reset();
	Impl->Pending.Reset();
}

// ------------------------------------------------------------------------------------------------------------ the frame

void UFourfoldAudioSubsystem::HandleFrame(const FFourfoldFrame& Frame)
{
	if (!Impl.IsValid() || !Impl->bReady || Frame.Curr == nullptr)
	{
		return;
	}
	FFourfoldAudioImpl& I = *Impl;
	const float Dt = FMath::Clamp(Frame.RealDeltaSeconds, 0.0f, 0.25f);
	I.Clock += static_cast<double>(Dt);
	RefreshVolumes();

	// 1. one-shots from this frame's events (rules decide; delayed plays wait in the pending list)
	const ffa::SnapshotWorld World(*Frame.Curr);
	I.PlayBuf.clear();
	I.HoldBuf.clear();
	if (Frame.Events != nullptr)
	{
		for (const ff::Event& E : *Frame.Events)
		{
			I.Rules.ProcessEvent(E, World, I.PlayBuf, I.HoldBuf);
		}
	}
	for (int32 i = I.Pending.Num() - 1; i >= 0; --i)
	{
		if (I.Clock >= I.Pending[i].Due)
		{
			const ffa::PlayRequest Due = I.Pending[i].Play;
			I.Pending.RemoveAtSwap(i);
			ExecutePlay(Due);
		}
	}
	for (const ffa::PlayRequest& R : I.PlayBuf)
	{
		if (R.delay_s > 0.0f)
		{
			FFourfoldAudioPending P;
			P.Play = R;
			P.Play.delay_s = 0.0f;
			P.Due = I.Clock + static_cast<double>(R.delay_s);
			I.Pending.Add(MoveTemp(P));
		}
		else
		{
			ExecutePlay(R);
		}
	}

	// 2. footsteps (not while the sim is paused; dilated time so hit-stop freezes the cadence)
	if (!Frame.bPaused)
	{
		I.StepBuf.clear();
		I.Steps.Update(I.Manifest, I.Rules, *Frame.Curr, FMath::Clamp(Frame.GameDeltaSeconds, 0.0f, 0.1f), I.StepBuf);
		for (const ffa::StepOut& S : I.StepBuf)
		{
			ffa::PlayRequest R;
			R.sound = S.sound;
			R.has_pos = true;
			R.pos = S.pos;
			R.gain_db = S.gain_db;
			ExecutePlay(R);
			if (S.cloth)
			{
				ffa::PlayRequest Cloth;
				Cloth.sound = S.cloth_sound;
				Cloth.has_pos = true;
				Cloth.pos = S.pos;
				Cloth.gain_db = -4.0f;
				Cloth.limit = "cloth";
				Cloth.limit_key = S.actor;
				ExecutePlay(Cloth);
			}
		}
	}

	// 3. loops: states of bodies / actors, event holds, ambience beds
	I.Loops.BeginFrame();
	I.WantBuf.clear();
	I.Rules.EvaluateLoops(*Frame.Curr, Frame.Prev, Frame.Alpha, I.WantBuf);
	for (const ffa::LoopWant& W : I.WantBuf)
	{
		I.Loops.Want(W);
	}
	for (const ffa::BedDef& Bed : I.Manifest.beds)
	{
		ffa::LoopWant W;
		W.key = "amb:" + Bed.sound;
		W.sound = Bed.sound;
		W.gain_db = Bed.gain_db;
		W.zone = true;
		W.two_d = true;
		I.Loops.Want(W);
	}
	for (const ffa::HoldRequest& H : I.HoldBuf)
	{
		I.Loops.Hold(H, I.Clock);
	}
	I.Loops.EndFrame(I.Clock, Dt);
	I.DuckDb = I.Duck.Step(Dt);
	ApplyLoops();

	// 4. random ambience accents (wind chimes, a bamboo knock)
	I.AccentBuf.clear();
	I.Ambience.Update(Dt, I.AccentBuf);
	for (const ffa::AccentPlay& A : I.AccentBuf)
	{
		ffa::PlayRequest R;
		R.sound = A.sound;
		R.two_d = true;
		R.gain_db = A.gain_db;
		ExecutePlay(R);
	}

	// 5. bookkeeping: forget one-shot voices whose estimated end has passed
	std::vector<uint64_t> Expired;
	I.Voices.Prune(I.Clock, &Expired);
	for (uint64_t Id : Expired)
	{
		VoiceComps.Remove(Id);
	}
}

void UFourfoldAudioSubsystem::ApplyLoops()
{
	FFourfoldAudioImpl& I = *Impl;
	for (const ffa::LoopState& S : I.Loops.States())
	{
		const FString Key = ToFString(S.key);
		const ffa::SoundDef* Def = I.Manifest.Find(S.sound);
		if (Def == nullptr)
		{
			continue;
		}
		const int32 Bus = I.Manifest.BusIndex(Def->bus);
		const float Db = Def->gain_db + S.vol_db + I.Manifest.mix.bus_gain_db[Bus] + (Bus == 2 ? I.DuckDb : 0.0f);
		const float Volume = ffa::DbToLinear(Db) * I.Volumes.For(Bus);

		if (S.started)
		{
			USoundBase* Sound = GetSound(ToFString(S.sound));
			if (Sound == nullptr)
			{
				continue;
			}
			UAudioComponent* Comp = nullptr;
			if (S.two_d || Def->attenuation == "2d")
			{
				Comp = UGameplayStatics::CreateSound2D(this, Sound, Volume, 1.0f, 0.0f, nullptr, false, false);
				if (Comp != nullptr)
				{
					Comp->Play();
				}
			}
			else
			{
				Comp = UGameplayStatics::SpawnSoundAtLocation(this, Sound, FF::ToUE(S.pos), FRotator::ZeroRotator, Volume, 1.0f, 0.0f,
				                                              GetAttenuation(ToFString(Def->attenuation)), nullptr, false);
			}
			if (Comp != nullptr)
			{
				if (Bus != 0)
				{
					Comp->SetUISound(true);
				}
				if (TObjectPtr<UAudioComponent>* Old = LoopComps.Find(Key))
				{
					if (UAudioComponent* OldComp = Old->Get())
					{
						OldComp->Stop();
						OldComp->DestroyComponent();
					}
				}
				LoopComps.Add(Key, Comp);
			}
			continue;
		}

		TObjectPtr<UAudioComponent>* Found = LoopComps.Find(Key);
		UAudioComponent* Comp = Found ? Found->Get() : nullptr;
		if (S.finished)
		{
			if (Comp != nullptr)
			{
				Comp->Stop();
				Comp->DestroyComponent();
			}
			LoopComps.Remove(Key);
			continue;
		}
		if (Comp == nullptr)
		{
			continue;
		}
		Comp->SetVolumeMultiplier(Volume);
		if (!S.two_d && Def->attenuation != "2d")
		{
			Comp->SetWorldLocation(FF::ToUE(S.pos));
		}
	}
}

// ------------------------------------------------------------------------------------------------------------ debug

FString UFourfoldAudioSubsystem::DescribeState() const
{
	if (!Impl.IsValid())
	{
		return TEXT("audio: not initialised");
	}
	const FFourfoldAudioImpl& I = *Impl;
	return FString::Printf(TEXT("audio: ready=%d sounds=%d voices=%d loops=%d (components %d) pending=%d played=%d dropped=%d missing-sound-names=%d duck=%.1f dB"),
	                       I.bReady ? 1 : 0, static_cast<int32>(I.Manifest.sounds.size()), I.Voices.Active(), I.Loops.ActiveCount(),
	                       LoopComps.Num(), I.Pending.Num(), I.Played, I.Dropped, I.Rules.MissingSounds(), I.DuckDb);
}

static void FourfoldAudioDump(UWorld* World)
{
	if (const UFourfoldAudioSubsystem* Audio = UFourfoldAudioSubsystem::Get(World))
	{
		UE_LOG(LogFourfoldAudio, Log, TEXT("%s"), *Audio->DescribeState());
	}
	else
	{
		UE_LOG(LogFourfoldAudio, Log, TEXT("audio: no subsystem in this world"));
	}
}

static FAutoConsoleCommandWithWorld GFourfoldAudioDumpCmd(
	TEXT("ff.audio.dump"), TEXT("Prints the Fourfold audio state (voices, loops, dropped plays)."),
	FConsoleCommandWithWorldDelegate::CreateStatic(&FourfoldAudioDump));
