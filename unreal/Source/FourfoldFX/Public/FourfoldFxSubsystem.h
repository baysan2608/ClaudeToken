// FourfoldFX - the world subsystem that turns every rendered sim frame into effects. It binds
// UFourfoldSimSubsystem::OnFrame in OnWorldBeginPlay (Game / PIE worlds only), runs the engine-free FX director
// (Private/Logic) on the frame's snapshots + events and hands the resulting draw list to AFourfoldFxActor.
// Persistent visuals come only from body state, one-shot cues only from events (docs/fx/README.md).
// It also mirrors the FX layer's arena state (DrawList::env: the wind gust of air moves) into the world's material
// parameter collection (fx_config "mpc_path", scalars WindGust / WindDirX / WindDirY; each skipped when missing).
// Console: ff.fx.Enable 0/1, ff.fx.ReloadConfig, ff.fx.Stats 0/1. Owner: stream `fx`.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "FourfoldFxSubsystem.generated.h"

class AFourfoldFxActor;
class UFourfoldSimSubsystem;
class UMaterialParameterCollection;
struct FFourfoldFrame;
struct FFourfoldFxImpl;

UCLASS()
class FOURFOLDFX_API UFourfoldFxSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	static UFourfoldFxSubsystem* Get(const UObject* WorldContextObject);

	/** Re-reads Content/Fourfold/Data/fx_config.json (look tuning without a rebuild). Returns false on a parse error. */
	bool ReloadConfig();
	/** Turns all effects off / on (off releases every component). */
	void SetEnabled(bool bInEnabled);
	bool IsEnabled() const { return bEnabled; }
	/** Counters of the last frame (views, one-shots, items, triangles, lights, update time). */
	FString GetDebugLine() const;

	// USubsystem / UWorldSubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	void OnSimFrame(const FFourfoldFrame& Frame);
	void OnScenarioLoaded(const FString& ScenarioId);
	void EnsureActor();
	/** Writes the arena wind gust into MPC_Arena (loaded once; parameters that do not exist are skipped). */
	void ApplyEnv(float WindGust, float WindDirX, float WindDirY);

	UPROPERTY(Transient) TObjectPtr<AFourfoldFxActor> FxActor;
	UPROPERTY(Transient) TObjectPtr<UMaterialParameterCollection> ArenaMpc;

	TSharedPtr<FFourfoldFxImpl> Impl;
	TWeakObjectPtr<UFourfoldSimSubsystem> SimWeak;
	FDelegateHandle FrameHandle;
	FDelegateHandle ScenarioHandle;
	FDelegateHandle RecenterHandle;   // open world: debris colliders follow the sim bubble
	bool bEnabled = true;
	bool bDebrisArenaDirty = true;   // rebuild the physics debris' arena collision on the next frame with a scenario
	int32 SetupQuality = -1;
	bool bMpcTried = false;          // ArenaMpc load attempted (again after ff.fx.ReloadConfig)
	bool bMpcHas[3] = {false, false, false};   // WindGust, WindDirX, WindDirY exist in the collection
	float MpcLast[3] = {-1.0f, -9.0f, -9.0f};  // last values written (writes only on change)
};
