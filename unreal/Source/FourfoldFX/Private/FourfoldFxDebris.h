// FourfoldFX - physics debris: Chaos rigid bodies for the logic's FractureReq pieces (broken stones / walls). Visual
// only: the pieces collide with a collision copy of the sim arena (ground, pool basin, metal plate, solids) and with
// each other on the Destructible channel and ignore everything else (fighters, cameras, traces). Every piece is a
// pooled procedural mesh whose convex hull collision is built once per piece shape; a component keeps its shape when
// it goes idle, so reusing it for the same shape is just a teleport and a parameter push. After its life a piece stops
// simulating and sinks into the ground, then goes back to the pool. Owner: stream `fx`.
#pragma once

#include "CoreMinimal.h"
#include "Logic/FxContext.h"   // ffx::DebrisImpact

#include <vector>

class AActor;
class AFourfoldFxActor;
class UPrimitiveComponent;
class UBoxComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UObject;
class UPhysicalMaterial;
class UProceduralMeshComponent;
class USceneComponent;

namespace ff
{
	struct ArenaView;
}
namespace ffx
{
	struct DrawList;
	struct FracturePiece;
	struct FractureReq;
	struct FxConfig;
}

struct FFourfoldFxDebris
{
	/** Physics look and budget (quality's pieces_max); pieces in flight keep running. */
	void Setup(const ffx::FxConfig& Config, int32 Quality);
	/** (Re)builds the collision copy of the sim arena. Created objects are appended to OutRefs (kept for the GC). */
	void BuildArena(AActor* Owner, USceneComponent* Root, const ff::ArenaView& Arena, TArray<TObjectPtr<UObject>>& OutRefs);
	/** The logic may request pieces (FxFrameIn::physicsDebris). */
	bool IsReady() const { return bArena && MaxPieces > 0; }
	/** Spawns the frame's requests (materials by ffx::MatSlot), ages, sinks and recycles pieces. Pieces report their
	 *  hits to Owner's OnDebrisHit, which forwards them to OnHit. */
	void Update(AFourfoldFxActor* Owner, const ffx::DrawList& List, const TArray<TObjectPtr<UMaterialInterface>>& SlotMaterials,
		float Dt, TArray<TObjectPtr<UObject>>& OutRefs);
	/** Keeps one kinematic box per ffx::ColliderReq (raised walls) so pieces bounce off them; the rest go idle. */
	void UpdateColliders(AActor* Owner, USceneComponent* Root, const ffx::DrawList& List, TArray<TObjectPtr<UObject>>& OutRefs);
	/** A piece hit something: hard landings become ffx::DebrisImpact (dust puffs in the logic). */
	void OnHit(const UPrimitiveComponent* Comp, const FVector& Location, const FVector& NormalImpulse);
	/** Impacts since the last ClearImpacts (sim space): FxFrameIn::debrisImpacts. */
	const std::vector<ffx::DebrisImpact>& Impacts() const { return ImpactList; }
	void ClearImpacts() { ImpactList.clear(); }
	/** Hides every piece at once (scenario change / effects off); the arena collision stays. */
	void ReleaseAll();
	FString GetDebugLine() const;

private:
	struct FPiece
	{
		UProceduralMeshComponent* Comp = nullptr;      // referenced through OutRefs
		UMaterialInstanceDynamic* MID = nullptr;
		UMaterialInterface* MIDParent = nullptr;
		UPhysicalMaterial* PhysMatSet = nullptr;
		const ffx::FracturePiece* Shape = nullptr;   // geometry + collision currently built on Comp
		float Age = 0.0f;
		float Life = 0.0f;
		float SinkDepth = 0.0f;                      // cm
		float HitCooldown = 0.0f;                    // s until its next impact counts
		FVector SinkFrom = FVector::ZeroVector;
		bool bActive = false;
		bool bSinking = false;
	};

	int32 Acquire(AFourfoldFxActor* Owner, const ffx::FracturePiece* Shape, TArray<TObjectPtr<UObject>>& OutRefs);
	void Spawn(FPiece& P, const ffx::FractureReq& R, const ffx::FracturePiece& Shape, UMaterialInterface* Mat, AActor* Owner,
		TArray<TObjectPtr<UObject>>& OutRefs, uint32 Seed);
	void Idle(FPiece& P);

	TArray<FPiece> Pieces;
	std::vector<ffx::DebrisImpact> ImpactList;
	UPhysicalMaterial* PhysMat = nullptr;          // referenced through OutRefs
	TArray<UBoxComponent*> ArenaBoxes;              // referenced through OutRefs
	TMap<uint32, UBoxComponent*> Colliders;         // ColliderReq key -> box (referenced through OutRefs)
	TArray<UBoxComponent*> IdleColliders;
	bool bArena = false;
	int32 MaxPieces = 0;
	int32 Active = 0;
	int32 Spawned = 0;
	int32 Skipped = 0;
	float SinkTime = 0.6f;
	float Spin = 9.0f;
	float LinearDamping = 0.12f;
	float AngularDamping = 0.35f;
	float MaxDepenetration = 60.0f;   // cm/s
	float Friction = 0.75f;
	float Restitution = 0.22f;
	float Density = 2.4f;
};
