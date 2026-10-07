// FourfoldFX - the actor that owns every pooled effect component of a world (procedural meshes, static meshes,
// point lights) and applies the logic's per-frame DrawList to them. Spawned by UFourfoldFxSubsystem; not placed in
// levels. Owner: stream `fx`.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
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

namespace ffx
{
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

private:
	TSharedPtr<FFourfoldFxRendererImpl> Impl;
	TSharedPtr<FFourfoldFxNiagara> Niagara;
	friend struct FFourfoldFxRendererImpl;
};
