// Fourfold - the actor that presents one sim fighter (skeletal mesh + native anim instance).
// FROZEN CONTRACT (architect): the three functions below are what other modules use; stream `game` owns and extends
// this class (components, anim runtime, materials, secondary motion ...).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/SkeletalMeshComponent.h"
#include "FourfoldFighter.generated.h"

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

protected:
	UPROPERTY(VisibleAnywhere, Category = "Fourfold") TObjectPtr<USkeletalMeshComponent> BodyMesh;
	UPROPERTY(VisibleAnywhere, Category = "Fourfold") int32 SimActorId = -1;
};
