// Fourfold - the actor that presents one sim fighter (skeletal mesh + native anim instance).
// FROZEN CONTRACT (architect): the three functions below are what other modules use; stream `game` owns and extends
// this class (components, anim runtime, materials, secondary motion ...).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/SkeletalMeshComponent.h"
#include "ff/Snapshot.h"
#include "FourfoldFighter.generated.h"

struct FFourfoldFrame;
struct FFourfoldFighterImpl;   // private state (anim director, smoothing): Private/FourfoldFighter.cpp
class UMaterialInstanceDynamic;
class UStaticMeshComponent;
class UStaticMesh;
class UMaterialInterface;
class UPhysicalAnimationComponent;

UCLASS()
class FOURFOLD_API AFourfoldFighter : public AActor
{
	GENERATED_BODY()

public:
	AFourfoldFighter();

	/** Sim actor id this fighter presents (ff::ActorView::id). */
	int32 GetSimActorId() const { return SimActorId; }
	/** The body mesh (UE5-Manny-compatible skeleton: sockets/bones hand_l, hand_r, foot_l, foot_r, spine_05, head, pelvis). */
	USkeletalMeshComponent* GetBodyMesh() const { return BodyMesh; }
	/** World position of a bone (falls back to the actor location). */
	FVector GetBoneLocation(FName Bone) const;

	// ---------------------------------------------------------------- additive API (stream `game`)
	/** Called once by UFourfoldSimSubsystem right after spawning: binds OnFrame, loads the mesh, palette, anim runtime. */
	void InitFromSim(const ff::ActorView& Actor, const ff::Snapshot& Snapshot);
	/** "player" | "rival" | "dummy" (palette role). */
	const FString& GetRole() const { return Role; }
	/** True while the engine-shape stand-in is shown (SK_Fighter not imported yet). */
	bool UsesFallbackBody() const { return bFallbackBody; }
	/** One line describing what the anim runtime plays (debug overlay). */
	FString GetAnimDebug() const;

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Yaw (degrees) between the actor's forward and the mesh asset's forward (UE5 Manny faces +Y: -90). */
	UPROPERTY(EditAnywhere, Category = "Fourfold") double MeshYawOffset = -90.0;

protected:
	void OnSimFrame(const FFourfoldFrame& Frame);
	void SetupBody();
	void BuildFallbackBody();
	void UpdateFallbackPose(const ff::ActorView& Actor, float Dt);
	void UpdateMaterials(const ff::ActorView& Actor, float Dt);
	void DriveAnimation(const FFourfoldFrame& Frame, const ff::ActorView& Cur, const ff::ActorView& Prev, float AnimDt);
	/** Physical reactions (presentation only, the sim stays the authority): an upper-body flinch on hits, a ragdoll fall
	 *  on knockdowns handed back to the get-up clip through a pose snapshot. */
	void UpdatePhysicalReactions(const ff::ActorView& Cur, float Dt, bool bHit, const FVector& HitDir, float Strength);
	void SetBodyPhysics(bool bOn);
	void AlignGetup(const FVector& PelvisW, const FVector& HeadW, float Window);
	void ResetGetupAlign();

	UPROPERTY(VisibleAnywhere, Category = "Fourfold") TObjectPtr<USceneComponent> Root;
	UPROPERTY(VisibleAnywhere, Category = "Fourfold") TObjectPtr<USkeletalMeshComponent> BodyMesh;
	UPROPERTY(Transient) TObjectPtr<UPhysicalAnimationComponent> PhysAnim;
	UPROPERTY(VisibleAnywhere, Category = "Fourfold") int32 SimActorId = -1;
	UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> BodyMaterials;
	UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> FallbackParts;
	UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> FallbackMaterials;
	UPROPERTY(Transient) TObjectPtr<UStaticMesh> ShapeCylinder;
	UPROPERTY(Transient) TObjectPtr<UStaticMesh> ShapeSphere;
	UPROPERTY(Transient) TObjectPtr<UStaticMesh> ShapeCube;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> ShapeMaterial;

	FString Role = TEXT("dummy");
	bool bFallbackBody = false;
	FDelegateHandle FrameHandle;
	TSharedPtr<FFourfoldFighterImpl> Impl;
};
