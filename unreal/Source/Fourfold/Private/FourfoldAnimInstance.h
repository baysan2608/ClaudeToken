// Fourfold - native animation runtime (ARCHITECTURE §8.4): a UAnimInstance with its own FAnimInstanceProxy, no Anim
// Blueprint. The fighter (game thread) runs the logic-island AnimDirector each frame and hands the resulting recipe
// to SetFrame(); the proxy copies it in PreUpdate and evaluates it on the worker thread:
//   clip blend (locomotion / action / reaction) -> legs-only gait layer -> additive block impact -> hand-shape
//   overrides -> inertial cross-fade from the last output -> procedural layers (pelvis drop + landing, hip tilt, lean,
//   hit springs, chest aim, head look-at, breathing) -> foot IK with planting / locking on the sim ground -> spring
//   chains for ff_hair_* / ff_sash_* / ff_hem_*.
// Missing clips -> reference pose; missing bones -> that layer is skipped. Nothing here can crash on bad data.
#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/PoseSnapshot.h"
#include "Logic/FFGArena.h"
#include "Logic/FFGSprings.h"
#include "FourfoldAnimInstance.generated.h"

class UAnimSequence;

struct FFourfoldClipRef
{
	UAnimSequence* Seq = nullptr;   // kept alive by UFourfoldAnimLibrarySubsystem
	float Time = 0.0f;
	float Weight = 0.0f;
	bool bLoop = false;
};

/** One frame of animation input (game thread -> proxy). Model space = the mesh component's space, metres. */
struct FFourfoldAnimFrame
{
	TArray<FFourfoldClipRef, TInlineAllocator<6>> Base;
	TArray<FFourfoldClipRef, TInlineAllocator<6>> Legs;
	float LegsWeight = 0.0f;
	FFourfoldClipRef Additive;
	FFourfoldClipRef Hand[2];   // 0 = left, 1 = right
	uint32 TransitionSerial = 0;
	float TransitionTime = 0.1f;
	// procedural layers
	float LeanPitch = 0.0f, LeanRoll = 0.0f, LandY = 0.0f;
	ffg::Vec3 SpringTorso, SpringHead, SpringArmL, SpringArmR;
	float AimYaw = 0.0f, AimWeight = 0.0f;
	bool bHasLook = false;
	ffg::Vec3 LookTarget;
	float LookWeight = 0.0f;
	float IkWeight = 0.0f, LockWeight = 0.0f;
	int32 PlantHint[2] = {-1, -1};
	float Breathe = 0.0f, BreathePhase = 0.0f;
	int32 Lod = 0;
	bool bChains = true;
	float Dt = 0.0f;
	float GroundSpeed = 0.0f, LocalSpeed = 0.0f;
	float ModelLift = 0.0f;   // metres
	bool bReset = false;      // teleport / respawn: drop locks, springs and cross-fades
	// Physics hand-back (ragdoll / flinch end): cross-fade from this pose instead of the last animated one.
	TSharedPtr<const FPoseSnapshot, ESPMode::ThreadSafe> BlendFromPose;
	uint32 BlendFromSerial = 0;
	float BlendFromTime = 0.35f;
	ffg::ModelAxes Axes;
	TSharedPtr<const ffg::ArenaGround, ESPMode::ThreadSafe> Arena;
};

UCLASS(Transient, NotBlueprintable)
class UFourfoldAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	/** Game thread, once per frame (the proxy picks it up in its next PreUpdate). */
	void SetFrame(const FFourfoldAnimFrame& InFrame);
	/** Last evaluated debug line (game thread copy). */
	FString DebugText;

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override;

private:
	friend struct FFourfoldAnimProxy;
	FFourfoldAnimFrame Pending;
	bool bHasPending = false;
};
