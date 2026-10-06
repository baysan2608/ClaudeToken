// FourfoldAudio - plays every sound of the game. Owner: stream `world_audio`.
//
// UFourfoldAudioSubsystem (one per game / PIE world) loads Content/Fourfold/Data/sfx_manifest.json, binds
// UFourfoldSimSubsystem::OnFrame and OnUiCue, and turns the frame's ff::Event list into one-shots through the data-driven rule
// table (Private/Logic: engine-free, unit-tested), keeps one looping voice alive per body / zone / actor state (fade in / out,
// positions follow the interpolated bodies), plays footsteps from ground speed, runs the ambience beds and random accents, and
// mixes everything with the player's volumes (UFourfoldSettingsSubsystem: master x SFX / UI / ambience).
// Rule language and file format: unreal/docs/audio/README.md and unreal/Tools/audio/event_rules.py.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

#include "FourfoldAudioSubsystem.generated.h"

class UAudioComponent;
class USoundAttenuation;
class USoundBase;
struct FFourfoldAudioImpl;   // private state (Private/FourfoldAudioSubsystem.cpp)
struct FFourfoldFrame;
namespace ffa { struct PlayRequest; }   // Private/Logic/FFARules.h

UCLASS()
class FOURFOLDAUDIO_API UFourfoldAudioSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	static UFourfoldAudioSubsystem* Get(const UObject* WorldContextObject);

	// USubsystem / UWorldSubsystem
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	/** True when the manifest loaded and the subsystem is bound to the sim. */
	bool IsReady() const;

	/** Plays the sound a UI cue name maps to (the game's Slate UI broadcasts the same names through OnUiCue). */
	bool PlayUiCue(FName Cue);

	/** Plays a manifest sound by name: 2D when WorldLocationUE is null (or the sound is a 2D / UI sound), else positioned
	 *  (Unreal centimetres). ExtraGainDb is added to the manifest gain. Returns true when the sound started. */
	bool PlaySoundByName(FName SoundName, const FVector* WorldLocationUE = nullptr, float ExtraGainDb = 0.0f);

	/** One-line state for the console command `ff.audio.dump` (voices, loops, drops). */
	FString DescribeState() const;

private:
	void HandleFrame(const FFourfoldFrame& Frame);
	void HandleUiCue(FName Cue);
	void HandleScenarioLoaded(const FString& ScenarioId);

	bool LoadManifest();
	USoundBase* GetSound(const FString& SoundName);
	USoundAttenuation* GetAttenuation(const FString& Class);
	bool ExecutePlay(const ffa::PlayRequest& Play);
	void StopVoice(uint64 VoiceId);
	void ApplyLoops();
	void RefreshVolumes();

	TSharedPtr<FFourfoldAudioImpl> Impl;

	/** Loaded sounds by manifest name (null entries mean "asset missing", logged once). */
	UPROPERTY(Transient)
	TMap<FString, TObjectPtr<USoundBase>> SoundCache;

	/** Runtime attenuation settings per distance class (near / mid / far / feel). */
	UPROPERTY(Transient)
	TMap<FString, TObjectPtr<USoundAttenuation>> Attenuations;

	/** One persistent component per running loop key (bodies, zones, actor states, event holds, ambience beds). */
	UPROPERTY(Transient)
	TMap<FString, TObjectPtr<UAudioComponent>> LoopComps;

	/** Running one-shots by voice id (weak: auto-destroying components finish on their own). */
	TMap<uint64, TWeakObjectPtr<UAudioComponent>> VoiceComps;

	FDelegateHandle FrameHandle;
	FDelegateHandle UiHandle;
	FDelegateHandle ScenarioHandle;
};
