// Fourfold - third-person camera rig (port of game/presentation/camera_rig.gd through the logic island's
// ffg::CameraLogic): orbit behind the player, lock-on framing, collision against the analytic arena (swing / lift /
// see-through), smoothing, and the feel layer in real time (shake with distance falloff, kick, FOV punch, 3 % zoom on
// transformations; reduced-motion rules). Updated from the player controller's OnFrame handler.
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
