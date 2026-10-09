// Fourfold - the local player's controller: game flow (title over an AI-vs-AI attract duel, Lab, Free Spar, Practice,
// Watch, pause), input (keyboard / mouse / gamepad sampled from the key state into the logic island's DesktopInput, the
// Slate touch overlay, merged into one latched ff::InputFrame per 60 Hz tick through UFourfoldSimSubsystem::PollInput),
// the camera rig as view target, the Slate UI (HUD, touch overlay, menus, Lab panel), the feel director, the
// placeholder arena, settings and app-lifecycle handling (release everything + pause when the app loses focus).
// Port of game/game.gd (flow, HUD wiring) + game/ui/player_input_hub.gd.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "ff/FourfoldCore.h"
#include "FourfoldPlayerController.generated.h"

class AFourfoldCameraRig;
class AFourfoldPlaceholderArena;
struct FFourfoldFrame;
struct FFourfoldSettings;
struct FFourfoldControllerImpl;   // private: UI widgets, input logic, feel (Private/FourfoldPlayerController.cpp)

UENUM()
enum class EFourfoldMode : uint8
{
	Title,   // title menu over the attract duel
	Play,    // the player controls a fighter (Lab, Free Spar, Practice drills)
	Watch,   // two AI fighters duel; a small bar returns to the title
};

UCLASS()
class FOURFOLD_API AFourfoldPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AFourfoldPlayerController();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void PlayerTick(float DeltaTime) override;

	// ---------------------------------------------------------------- flow
	void ShowTitle();
	bool StartScenario(const FString& ScenarioId, const ff::ScenarioOptions& Options);
	void StartWatch();
	/** Open world: roam the generated valley (needs Content/Fourfold/Data/openworld; the level's AFourfoldOpenWorld draws it). */
	bool StartRoam();
	void PauseGame();
	void ResumeGame();
	void QuitToTitle();
	void ToggleLabPanel();
	EFourfoldMode GetMode() const { return Mode; }
	bool IsGamePaused() const { return bPaused; }
	/** The touch HUD is shown (settings "touch controls": auto = phones / tablets). */
	bool IsTouchUiActive() const;

	AFourfoldCameraRig* GetCameraRig() const { return CameraRig; }

protected:
	void BuildUi();
	void OnSimFrame(const FFourfoldFrame& Frame);
	void OnScenarioLoaded(const FString& ScenarioId);
	void PollSimInput(ff::InputFrame& Out, float& InOutCameraYawSim);
	void OnSettingsChanged(const FFourfoldSettings& Settings);
	void OnAppDeactivate();
	void OnAppReactivate();
	void SampleDevices(float RealDt);
	void HandleUiKeys();
	void UpdateInputRouting();
	void AdaptQuality(float RealDt);
	void EnsureArena();
	void UiCue(FName Cue) const;
	void SetMouseLook(bool bOn);
	void ReleaseAllInput();
	/** Sim position -> viewport pixels through this frame's rig camera (the camera manager's POV lags a frame). */
	bool ProjectSim(const ff::Vec3& SimPos, FVector2D& OutPixels) const;
	class UFourfoldSimSubsystem* GetSim() const;

	UPROPERTY(Transient) TObjectPtr<AFourfoldCameraRig> CameraRig;
	UPROPERTY(Transient) TObjectPtr<AFourfoldPlaceholderArena> PlaceholderArena;

	EFourfoldMode Mode = EFourfoldMode::Title;
	bool bPaused = false;
	bool bLabOpen = false;
	bool bMouseLook = false;
	bool bLevelHasArena = false;
	bool bGameplayInput = false;
	TArray<FString> LevelSeeThrough;

	FDelegateHandle FrameHandle;
	FDelegateHandle ScenarioHandle;
	FDelegateHandle RecenterHandle;
	bool bLevelHasOpenWorld = false;
	FDelegateHandle SettingsHandle;
	FDelegateHandle DeactivateHandle;
	FDelegateHandle BackgroundHandle;
	FDelegateHandle ReactivateHandle;

	TSharedPtr<FFourfoldControllerImpl> Impl;
};
