// Fourfold - placeholder arena: when the level has no actor tagged "FourfoldArena" (the world stream's built level),
// engine cubes are spawned for ff::Session::Arena() (floor around the pool, solids, pool basin + water, metal plate)
// so the game is playable before any world asset exists. Solids are named after the sim solids so the camera can draw
// the one it sits behind see-through. Also draws the debug overlay (arena boxes, bodies, zones).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ff/Snapshot.h"
#include "FourfoldPlaceholderArena.generated.h"

class UStaticMesh;
class UMaterialInterface;
class UStaticMeshComponent;

UCLASS(NotBlueprintable)
class AFourfoldPlaceholderArena : public AActor
{
	GENERATED_BODY()

public:
	AFourfoldPlaceholderArena();

	/** Rebuilds every piece for this arena. */
	void Build(const ff::ArenaView& Arena);
	/** Shows / hides solids by sim name (the camera's see-through list). Hidden ones still cast shadows. */
	void SetSeeThrough(const TArray<FString>& Names);

	/** Debug overlay (Lab overlay flag / Settings.bShowDebug): boxes, bodies, zones. */
	static void DrawDebug(UWorld* World, const ff::ArenaView& Arena, const ff::Snapshot& Snap);
	/** Applies the camera's see-through list to the level's own arena actors tagged "FFSolid_<name>" (world stream). */
	static void SetSeeThroughOnLevel(UWorld* World, const TArray<FString>& Names, TArray<FString>& InOutApplied);

protected:
	UStaticMeshComponent* AddBox(const FString& Name, const FVector& MinUE, const FVector& MaxUE, const FLinearColor& Color);

	UPROPERTY(VisibleAnywhere, Category = "Fourfold") TObjectPtr<USceneComponent> Root;
	UPROPERTY(Transient) TObjectPtr<UStaticMesh> Cube;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> BaseMaterial;
	UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> Pieces;
	TMap<FString, TObjectPtr<UStaticMeshComponent>> Solids;
	TArray<FString> SeeThroughNow;
};
