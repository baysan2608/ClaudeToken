// Fourfold - the hub between the engine-free sim (ff::Session) and every Unreal-side consumer.
// FROZEN CONTRACT (architect): the declarations below are fixed (additive changes allowed). Owner: stream `game`
// (it implements Private/FourfoldSimSubsystem.cpp). Consumers: FourfoldFX (VFX), FourfoldAudio (sound), the game's
// own fighters / camera / HUD.
//
// Each rendered frame Tick() steps the session at a fixed 60 Hz (accumulator x Lab time scale, hit-stop slows the
// world through global time dilation), then broadcasts OnFrame ONCE with both snapshots, the interpolation alpha and
// every event of the ticks stepped this frame. Consumers do all per-frame work inside OnFrame (no ordering issues).
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ff/FourfoldCore.h"

#include <vector>

#include "FourfoldSimSubsystem.generated.h"

class AFourfoldFighter;

/** One rendered frame of simulation output. Pointers are valid only during the OnFrame broadcast. */
struct FFourfoldFrame
{
	const ff::Snapshot* Prev = nullptr;              // state at the previous sim tick
	const ff::Snapshot* Curr = nullptr;              // state at the latest sim tick
	float Alpha = 1.0f;                              // interpolate Prev -> Curr (accumulator / dt), 0..1
	const std::vector<ff::Event>* Events = nullptr;  // events of the ticks stepped this frame, in order
	int32 TicksStepped = 0;
	float RealDeltaSeconds = 0.0f;                   // undilated frame time (camera shake, UI, audio fades)
	float GameDeltaSeconds = 0.0f;                   // dilated frame time (hit-stop slows it)
	uint64 FrameIndex = 0;
	bool bPaused = false;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FOnFourfoldFrame, const FFourfoldFrame& /*Frame*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnFourfoldScenarioLoaded, const FString& /*ScenarioId*/);
/** UI sound cue requested by the game's Slate UI (FourfoldAudio plays it): ui_tap ui_select ui_back ui_open ui_close
 *  ui_toggle ui_ring_open ui_ring_pick ui_error ui_pause ui_resume ui_toast. */
DECLARE_MULTICAST_DELEGATE_OneParam(FOnFourfoldUiCue, FName /*CueName*/);

UCLASS()
class FOURFOLD_API UFourfoldSimSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	static UFourfoldSimSubsystem* Get(const UObject* WorldContextObject);

	ff::Session& GetSession();
	const ff::Session& GetSession() const;
	const ff::Snapshot& GetSnapshot() const;          // latest tick
	const ff::Snapshot& GetPrevSnapshot() const;      // previous tick
	float GetAlpha() const;

	bool LoadScenario(const FString& ScenarioId, const ff::ScenarioOptions& Options = ff::ScenarioOptions());
	FString GetScenarioId() const;

	/** Fighter actor that presents sim actor `SimActorId` (nullptr when none). */
	AFourfoldFighter* FindFighter(int32 SimActorId) const;
	int32 GetPlayerActorId() const;

	/** Pause stops stepping (OnFrame keeps firing with bPaused = true so UI / camera keep running). */
	void SetPaused(bool bInPaused);
	bool IsPaused() const;

	/** Presentation hit-stop request (frames at 60 Hz, scaled by power); the game's feel director owns the policy. */
	void RequestHitStop(int32 Frames);

	FOnFourfoldFrame OnFrame;
	FOnFourfoldScenarioLoaded OnScenarioLoaded;
	FOnFourfoldUiCue OnUiCue;

	// USubsystem / FTickableGameObject
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickableWhenPaused() const override { return true; }
};
