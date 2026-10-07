// FourfoldFX - the actor that owns every pooled effect component of a world (procedural meshes, static meshes,
// point lights) and applies the logic's per-frame DrawList to them. Spawned by UFourfoldFxSubsystem; not placed in
// levels. Owner: stream `fx`.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include <vector>

#include "FourfoldFxActor.generated.h"

class UMaterialInterface;
class UMaterialInstanceDynamic;
class UPointLightComponent;
class UPrimitiveComponent;
class UProceduralMeshComponent;
class UStaticMesh;
class UStaticMeshComponent;
class UTexture;
class UFourfoldSimSubsystem;
struct FFourfoldFxRendererImpl;
struct FFourfoldFxNiagara;
struct FFourfoldFxDebris;

namespace ff
{
	struct ArenaView;
}
namespace ffx
{
	struct DebrisImpact;
	struct DrawList;
	struct FxConfig;
}

UCLASS(NotPlaceable, Transient)
class FOURFOLDFX_API AFourfoldFxActor : public AActor
{
	GENERATED_BODY()

public:
	AFourfoldFxActor();

	/** Loads materials / meshes / flipbooks named by the config and pre-creates pooled components (hidden). */
	void Setup(const ffx::FxConfig& Config, int32 Quality);
	/** Binds this frame's draw items to components (creating / recycling as needed), sets the lights and spawns the
	 *  frame's Niagara cue systems (unless bNiagara is false: ff.fx.Niagara 0). */
	void Apply(const ffx::DrawList& List, UFourfoldSimSubsystem* Sim, bool bNiagara = true);
	/** Hides and releases every component (scenario change). */
	void ReleaseAll();
	/** One line of counters for the debug overlay. */
	FString GetDebugLine() const;
	/** Bit per ffx::NCue: the cue's Niagara system loaded (fed back to the logic as FxFrameIn::niagaraLoaded). */
	uint64 GetNiagaraLoadedMask() const;
	/** Bit per ffx::LCue: persistent Niagara slot loaded (FxFrameIn::niagaraLoopsLoaded). */
	uint64 GetNiagaraLoopsLoadedMask() const;
	/** True while a loaded Niagara cue system has not been pre-warmed in this world. */
	bool NeedsNiagaraPrewarm() const;
	/** Plays each loaded Niagara cue system once, small, at Location (a point hidden behind the floor) so its first-use
	 *  costs are paid now; returns the number of systems spawned. */
	int32 PrewarmNiagara(const FVector& Location);
	/** Draws a small triangle per FX material and each static FX mesh at Location (hidden behind the floor) for a few
	 *  frames, so every look's render pipelines are built at load rather than on its first appearance mid-fight (PSO
	 *  precaching is compiled out of editor builds; packaged builds only precache components that exist). */
	void PrewarmMaterials(const FVector& Location);
	bool NeedsMaterialPrewarm() const { return !bMaterialsPrewarmed; }
	/** Rebuilds the physics debris' collision copy of the sim arena (scenario load). */
	void BuildDebrisArena(const ff::ArenaView& Arena);
	/** Physics debris can run (arena collision built, quality budget > 0): FxFrameIn::physicsDebris. */
	bool IsDebrisReady() const;
	/** Hard debris landings since the last ClearDebrisImpacts (FxFrameIn::debrisImpacts; null when there are none). */
	const std::vector<ffx::DebrisImpact>* GetDebrisImpacts() const;
	void ClearDebrisImpacts();
	/** OnComponentHit of the debris pieces. */
	UFUNCTION()
	void OnDebrisHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse,
		const FHitResult& Hit);

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

protected:
	UPROPERTY(VisibleAnywhere, Category = "Fourfold") TObjectPtr<USceneComponent> Root;
	/** Every component the pools created (keeps them referenced for the GC). */
	UPROPERTY(Transient) TArray<TObjectPtr<UPrimitiveComponent>> PooledComponents;
	UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> PooledMaterials;
	UPROPERTY(Transient) TArray<TObjectPtr<UPointLightComponent>> Lights;
	/** Assets resolved from fx_config.json (index = ffx::MatSlot / MeshAsset / Flipbook). */
	UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInterface>> SlotMaterials;
	UPROPERTY(Transient) TArray<TObjectPtr<UStaticMesh>> AssetMeshes;
	UPROPERTY(Transient) TArray<TObjectPtr<UTexture>> FlipbookTextures;
	/** Niagara systems of the cue slots (fx_config.json "niagara"). */
	UPROPERTY(Transient) TArray<TObjectPtr<UObject>> NiagaraSystems;
	/** Material pre-warm draws (destroyed after a few frames). */
	UPROPERTY(Transient) TArray<TObjectPtr<UPrimitiveComponent>> PrewarmComponents;
	/** Physics debris: pooled pieces + their materials, and the arena collision copy (rebuilt per scenario). */
	UPROPERTY(Transient) TArray<TObjectPtr<UObject>> DebrisObjects;
	UPROPERTY(Transient) TArray<TObjectPtr<UObject>> DebrisArena;

private:
	void EndMaterialPrewarm();

	int32 PrewarmFrames = 0;
	bool bMaterialsPrewarmed = false;
	TSharedPtr<FFourfoldFxRendererImpl> Impl;
	TSharedPtr<FFourfoldFxNiagara> Niagara;
	TSharedPtr<FFourfoldFxDebris> Debris;
	friend struct FFourfoldFxRendererImpl;
};
