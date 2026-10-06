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
	/** Binds this frame's draw items to components (creating / recycling as needed) and sets the lights. */
	void Apply(const ffx::DrawList& List, UFourfoldSimSubsystem* Sim);
	/** Hides and releases every component (scenario change). */
	void ReleaseAll();
	/** One line of counters for the debug overlay. */
	FString GetDebugLine() const;

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

private:
	TSharedPtr<FFourfoldFxRendererImpl> Impl;
	friend struct FFourfoldFxRendererImpl;
};
