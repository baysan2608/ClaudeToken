// Fourfold - third-person camera rig (the logic island's ffg::CameraLogic): free orbit behind the player, the lock-on
// two-shot, the spectator two-shot (title / watch), collision against the analytic arena (swing / side flip / lift /
// see-through), spring smoothing, and the feel layer in real time (rotational trauma shake with roll, spring kick, FOV
// punch held through hit-stop, cinematic dolly, 3 % zoom on transformations; reduced-motion rules). Updated every
// rendered frame from the player controller's OnFrame handler (camera input is applied there too, before the update).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Logic/FFGArena.h"
#include "Logic/FFGCamera.h"
#include "FourfoldCameraRig.generated.h"

class UCameraComponent;

UCLASS(NotBlueprintable)
class AFourfoldCameraRig : public AActor
{
	GENERATED_BODY()

public:
	AFourfoldCameraRig();

	ffg::CameraLogic Logic;
	ffg::ArenaGround Arena;

	/** New scenario: arena copy + snap behind the player looking at `LookAt` (sim space). */
	void ResetForScenario(const ff::ArenaView& InArena, const ff::Vec3& PlayerPos, const ff::Vec3& LookAt);
	/** Per frame. Positions in sim space; Target / Threat may be null. */
	void UpdateRig(float GameDt, float RealDt, const ff::Vec3& PlayerPos, const ff::Vec3* Target, const ff::Vec3* Threat);
	/** Solids the camera currently sits behind (drawn see-through by the arena owner). */
	const TArray<FString>& GetSeeThrough() const { return SeeThrough; }
	UCameraComponent* GetCamera() const { return Camera; }

protected:
	UPROPERTY(VisibleAnywhere, Category = "Fourfold") TObjectPtr<USceneComponent> Root;
	UPROPERTY(VisibleAnywhere, Category = "Fourfold") TObjectPtr<UCameraComponent> Camera;
	TArray<FString> SeeThrough;
};
