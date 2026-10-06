// Fourfold - game mode (see FourfoldGameMode.h).
#include "FourfoldGameMode.h"

#include "FourfoldPlayerController.h"

AFourfoldGameMode::AFourfoldGameMode()
{
	DefaultPawnClass = nullptr;
	SpectatorClass = nullptr;   // APlayerController::SpawnSpectatorPawn skips a null class
	PlayerControllerClass = AFourfoldPlayerController::StaticClass();
	bStartPlayersAsSpectators = false;
}

void AFourfoldGameMode::RestartPlayer(AController* NewPlayer)
{
	// Intentionally empty (no pawn); AGameModeBase would log FailedToRestartPlayer otherwise.
}
