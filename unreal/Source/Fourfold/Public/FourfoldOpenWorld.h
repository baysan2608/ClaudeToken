// Fourfold - the open-world valley at runtime (owner: open-world stream).
//
// Reads a world data set (Content/Fourfold/Data/<DataDirName>: world.json + heights.f32 [+ splat.rgba] [+ water layer];
// openworld = the valley written by unreal/Tools/openworld/gen_world.py, ruins = L_Ruins baked by
// unreal/Tools/ruins/bake_ruins.py). A level that brings its own meshes (L_Ruins) switches the terrain / water / solids /
// ground cover off and keeps the sim data, the props (HISMs from world.json, prop_assets names their meshes) and the
// encounter markers. Otherwise it builds what you see from the SAME heightfield the sim walks on:
// - terrain: 16 x 16 chunks of 128 m (procedural mesh); every chunk has an 8 m far section, chunks near the camera
//   get a 2 m section with collision (physics debris lands on it); skirts hide the seams between the two;
//   vertex colour = splat weights (R grass, G rock, B sand, A ash) for the terrain material;
// - the lake surface, the sim's solids (stone blocks), encounter-site markers, trees and rocks (instanced, culled).
// The level places one AFourfoldOpenWorld (tagged "FourfoldArena", so no placeholder arena spawns); the player
// controller starts roaming when it finds it (or with -scenario=roam).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ff/OpenWorld.h"

#include <memory>

#include "FourfoldOpenWorld.generated.h"

class UProceduralMeshComponent;
class UHierarchicalInstancedStaticMeshComponent;
class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;

UCLASS()
class FOURFOLD_API AFourfoldOpenWorld : public AActor
{
	GENERATED_BODY()

public:
	AFourfoldOpenWorld();

	/** The generated valley (Content/Fourfold/Data/openworld), loaded once per process (null + error when missing or bad). */
	static std::shared_ptr<const ff::WorldDef> LoadWorldDef(FString* OutError = nullptr);
	/** Any data set Content/Fourfold/Data/<DataDir> (openworld, ruins), loaded once per process and directory. */
	static std::shared_ptr<const ff::WorldDef> LoadWorldDefFrom(const FString& DataDir, FString* OutError = nullptr);

	/** Data set this level plays (Content/Fourfold/Data/<DataDirName>). */
	UPROPERTY(EditAnywhere, Category = "Fourfold|OpenWorld") FString DataDirName = TEXT("openworld");
	/** What BeginPlay builds from the data (L_Ruins keeps only the props and the encounter markers). */
	UPROPERTY(EditAnywhere, Category = "Fourfold|OpenWorld") bool bBuildTerrain = true;
	UPROPERTY(EditAnywhere, Category = "Fourfold|OpenWorld") bool bBuildWater = true;
	UPROPERTY(EditAnywhere, Category = "Fourfold|OpenWorld") bool bBuildSolids = true;
	UPROPERTY(EditAnywhere, Category = "Fourfold|OpenWorld") bool bBuildProps = true;
	UPROPERTY(EditAnywhere, Category = "Fourfold|OpenWorld") bool bBuildCover = true;
	/** Scale scanned props to game sizes (rocks ~3 m, trees <= 18 m); off when the data carries real sizes. */
	UPROPERTY(EditAnywhere, Category = "Fourfold|OpenWorld") bool bNormalizePropScale = true;

	UPROPERTY(EditAnywhere, Category = "Fourfold|OpenWorld") TSoftObjectPtr<UMaterialInterface> TerrainMaterial;
	UPROPERTY(EditAnywhere, Category = "Fourfold|OpenWorld") TSoftObjectPtr<UMaterialInterface> WaterMaterial;
	UPROPERTY(EditAnywhere, Category = "Fourfold|OpenWorld") TSoftObjectPtr<UMaterialInterface> StoneMaterial;
	UPROPERTY(EditAnywhere, Category = "Fourfold|OpenWorld") TSoftObjectPtr<UMaterialInterface> MarkerMaterial;
	/** world.json prop "mesh" key -> mesh (tree_broadleaf_a, tree_fir_b, rock_granite, ...). */
	UPROPERTY(EditAnywhere, Category = "Fourfold|OpenWorld") TMap<FName, TSoftObjectPtr<UStaticMesh>> PropMeshes;
	/** Chunks closer than this (cm, to the chunk's box) get the 2 m section + collision. */
	UPROPERTY(EditAnywhere, Category = "Fourfold|OpenWorld") float NearRadius = 34000.0f;
	UPROPERTY(EditAnywhere, Category = "Fourfold|OpenWorld") float TreeCullDistance = 42000.0f;
	/** Trees sway (WPO) only closer than this (cm). */
	UPROPERTY(EditAnywhere, Category = "Fourfold|OpenWorld") float TreeWindDistance = 6000.0f;
	UPROPERTY(EditAnywhere, Category = "Fourfold|OpenWorld") float RockCullDistance = 30000.0f;
	UPROPERTY(EditAnywhere, Category = "Fourfold|OpenWorld") float CliffCullDistance = 300000.0f;
	/** Flip triangle winding if the terrain renders inside out on some platform. */
	UPROPERTY(EditAnywhere, Category = "Fourfold|OpenWorld") bool bFlipWinding = false;
	/** Ground cover (grass / ferns / flowers / shrubs) streamed around the camera: radius (cm) and density scale. */
	UPROPERTY(EditAnywhere, Category = "Fourfold|OpenWorld") float CoverRadius = 6400.0f;
	UPROPERTY(EditAnywhere, Category = "Fourfold|OpenWorld") float CoverDensity = 1.0f;
	/** The player controller starts roaming on BeginPlay when the level has this actor. */
	UPROPERTY(EditAnywhere, Category = "Fourfold|OpenWorld") bool bAutoStartRoam = true;

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Terrain height (world metres) under a world point given in Unreal cm (for spawning / tools). */
	float TerrainHeightUE(const FVector& WorldUE) const;

private:
	struct FChunk
	{
		int32 Cx = 0, Cz = 0;
		TObjectPtr<UProceduralMeshComponent> Mesh;
		bool bNear = false;
		bool bNearBuilt = false;
		FBox Bounds;
		uint32 EditRev = 0;          // TerrainEdits::chunk_rev last meshed
		TArray<int32> Props;         // indices into PropRefs standing on this chunk
	};

	struct FPropRef
	{
		TObjectPtr<UHierarchicalInstancedStaticMeshComponent> H;
		int32 Index = 0;
		FTransform Base;
		double Wx = 0.0, Wz = 0.0;
		bool bTree = false;
		bool bDown = false;          // toppled by moving ground (stays down)
	};

	void BuildAll();
	void BuildChunkSection(FChunk& C, int32 Section, int32 Step, bool bCollision);
	void BuildWater();
	void BuildSolids();
	void BuildSitesAndProps();
	void UpdateLods(const FVector& ViewUE);
	/** Re-meshes chunks the sim's terrain edits touched; props on moved ground ride it or topple. */
	void ApplyTerrainEdits();
	void UpdateProp(FPropRef& P);
	/** Rebuilds the ground cover around ViewUE when the camera moved far enough or the terrain changed. */
	void UpdateCover(const FVector& ViewUE, bool bForce);
	int32 ChunkIndexAt(double Wx, double Wz) const;

	std::shared_ptr<const ff::WorldDef> World;
	TArray<uint8> Splat;          // RGBA per sample (may be empty)
	TArray<FChunk> Chunks;
	int32 ChunkCells = 64;        // samples per chunk edge - 1
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> TerrainMat;
	UPROPERTY(Transient) TArray<TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> PropComps;
	TArray<FPropRef> PropRefs;
	UPROPERTY(Transient) TArray<TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> CoverComps;
	FVector CoverCenter = FVector(1e12);
	uint64 CoverEditRevision = 0;
	uint64 SeenEditRevision = 0;
	float LodTimer = 0.0f;
	bool bBuilt = false;
};
