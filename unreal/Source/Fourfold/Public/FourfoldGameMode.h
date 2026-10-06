// Fourfold - game mode (DefaultEngine.ini: GlobalDefaultGameMode=/Script/Fourfold.FourfoldGameMode). No pawn and no
// spectator pawn: fighters are AFourfoldFighter actors spawned by UFourfoldSimSubsystem, the view is the camera rig
// owned by AFourfoldPlayerController.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "FourfoldGameMode.generated.h"

UCLASS()
class FOURFOLD_API AFourfoldGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AFourfoldGameMode();

	/** Nothing to spawn or possess: the controller drives the sim, not a pawn. */
	virtual void RestartPlayer(AController* NewPlayer) override;
};
