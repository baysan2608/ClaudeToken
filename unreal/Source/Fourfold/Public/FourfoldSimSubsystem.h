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
struct FFourfoldSimImpl;   // private state (Private/FourfoldSimSubsystem.cpp)

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
/** (additive, game) Fills the local player's input for ONE 60 Hz tick and the camera yaw in sim radians (the camera
 *  looks along (sin yaw, 0, cos yaw)). Bound by AFourfoldPlayerController; unbound = idle input. */
/** (additive, open world) The sim bubble moved by Delta (sim metres): sim-space state kept across frames must shift by
 *  -Delta (FF::GSimOriginUE already moved by +Delta, so world positions stay put). Fired inside the step, before OnFrame. */
DECLARE_MULTICAST_DELEGATE_OneParam(FOnFourfoldSimRecenter, const ff::Vec3& /*Delta*/);
DECLARE_DELEGATE_TwoParams(FFourfoldPollInput, ff::InputFrame& /*OutInput*/, float& /*InOutCameraYawSim*/);

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

	// ---------------------------------------------------------------- additive API (stream `game`)
	/** Polled once per sim tick for the player's input (see FFourfoldPollInput). */
	FFourfoldPollInput PollInput;
	/** True once a scenario is loaded (GetSession() is always valid after Initialize). */
	bool HasScenario() const;
	/** Options the current scenario was loaded with (RestartScenario reuses them). */
	const ff::ScenarioOptions& GetScenarioOptions() const;
	/** Reloads the current scenario with the same options. */
	bool RestartScenario();
	/** The sim arena (sim space), valid after LoadScenario. */
	const ff::ArenaView& GetArena() const;
	uint64 GetFrameIndex() const;
	/** Global time dilation the hit-stop / slow-motion assist applies right now (1 = none). */
	float GetCurrentTimeDilation() const;
	/** Slow-motion assist (perfect deflect, settings): 0.55x for `RealSeconds`. */
	void RequestSlowmo(float RealSeconds);
	/** Reduced motion caps one hit-stop request at 3 frames (set by the feel director from the settings). */
	void SetReducedMotion(bool bReduced);
	/** Persistence under Saved/Fourfold/ (progress.json, lab_tuning.json). */
	void SaveProgress();
	bool SaveLabTuning();
	bool LoadLabTuning();

	// ---------------------------------------------------------------- open world (additive)
	/** Starts roaming `World` (ScenarioId "roam"): the sim bubble follows the player, encounters engage near sites. */
	bool LoadRoam(std::shared_ptr<const ff::WorldDef> World, const ff::RoamOptions& Options = ff::RoamOptions());
	bool IsRoaming() const;
	std::shared_ptr<const ff::WorldDef> GetWorldDef() const;
	FOnFourfoldSimRecenter OnSimRecenter;

	// USubsystem / FTickableGameObject
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickableWhenPaused() const override { return true; }

private:
	void StepOnce();
	void SyncFighters(bool bRespawnAll);
	void ApplyTimeDilation(float Dilation);

	TSharedPtr<FFourfoldSimImpl> Impl;
};
