// Fourfold - player controller (see FourfoldPlayerController.h).
#include "FourfoldPlayerController.h"

#include "FourfoldCameraRig.h"
#include "FourfoldCoords.h"
#include "FourfoldFeel.h"
#include "FourfoldLog.h"
#include "FourfoldPlaceholderArena.h"
#include "FourfoldSettings.h"
#include "FourfoldSimSubsystem.h"
#include "Logic/FFGDesktopInput.h"
#include "Logic/FFGUiScale.h"
#include "UI/FourfoldUi.h"
#include "UI/SFourfoldHud.h"
#include "UI/SFourfoldLabPanel.h"
#include "UI/SFourfoldMenus.h"
#include "UI/SFourfoldTouchOverlay.h"
#include "UI/SFourfoldUiRoot.h"

#include "Camera/CameraComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerInput.h"
#include "HAL/PlatformTime.h"
#include "InputCoreTypes.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Parse.h"

#include <algorithm>
#include <array>
#include <vector>

namespace
{
	constexpr bool kMobile = (PLATFORM_IOS || PLATFORM_ANDROID) != 0;

	/** Keys / buttons of each desktop action (docs/CONTROLS.md "Keyboard and mouse" + "Controller"). */
	struct FDeskBinding
	{
		ffg::DeskAction Action;
		TArray<FKey> Keys;
		FKey Axis;            // analog trigger counted as pressed above 0.5 (LT = ground, RT = technique)
	};

	const TArray<FDeskBinding>& DeskBindings()
	{
		static const TArray<FDeskBinding> Bindings = {
			{ffg::DeskAction::Attack, {EKeys::J, EKeys::LeftMouseButton, EKeys::Gamepad_FaceButton_Left}, FKey()},
			{ffg::DeskAction::Guard, {EKeys::K, EKeys::Gamepad_RightShoulder}, FKey()},
			{ffg::DeskAction::Evade, {EKeys::SpaceBar, EKeys::Gamepad_FaceButton_Bottom}, FKey()},
			{ffg::DeskAction::Tech, {EKeys::L, EKeys::RightMouseButton}, EKeys::Gamepad_RightTriggerAxis},
			{ffg::DeskAction::Cancel, {EKeys::Gamepad_LeftShoulder}, FKey()},
			{ffg::DeskAction::Pause, {EKeys::Escape, EKeys::BackSpace, EKeys::Gamepad_Special_Right}, FKey()},
			{ffg::DeskAction::Elem0, {EKeys::One, EKeys::Gamepad_DPad_Left}, FKey()},
			{ffg::DeskAction::Elem1, {EKeys::Two, EKeys::Gamepad_DPad_Down}, FKey()},
			{ffg::DeskAction::Elem2, {EKeys::Three, EKeys::Gamepad_DPad_Right}, FKey()},
			{ffg::DeskAction::Elem3, {EKeys::Four, EKeys::Gamepad_DPad_Up}, FKey()},
			{ffg::DeskAction::Target, {EKeys::Tab, EKeys::Gamepad_RightThumbstick}, FKey()},
			{ffg::DeskAction::Thrust, {EKeys::U, EKeys::Gamepad_FaceButton_Top}, FKey()},
			{ffg::DeskAction::Ground, {EKeys::N}, EKeys::Gamepad_LeftTriggerAxis},
			{ffg::DeskAction::Sweep, {EKeys::H, EKeys::Gamepad_FaceButton_Right}, FKey()},
			{ffg::DeskAction::SubPrev, {EKeys::Q}, FKey()},
			{ffg::DeskAction::SubNext, {EKeys::E}, FKey()},
		};
		return Bindings;
	}

	FString ToF(const std::string& S) { return FString(UTF8_TO_TCHAR(S.c_str())); }

	/** Which desktop actions are physically down right now (edge re-sync after menus / panels close). */
	std::array<bool, ffg::kDeskCount> DeskDownNow(const APlayerController& PC)
	{
		std::array<bool, ffg::kDeskCount> Down{};
		for (const FDeskBinding& B : DeskBindings())
		{
			const size_t I = size_t(ffg::DA(B.Action));
			for (const FKey& K : B.Keys)
			{
				Down[I] = Down[I] || PC.IsInputKeyDown(K);
			}
			if (B.Axis.IsValid())
			{
				Down[I] = Down[I] || PC.GetInputAnalogKeyState(B.Axis) > 0.5f;
			}
		}
		return Down;
	}

	ff::Vec3 LerpV(const ff::Vec3& A, const ff::Vec3& B, float T)
	{
		return ff::Vec3(A.x + (B.x - A.x) * T, A.y + (B.y - A.y) * T, A.z + (B.z - A.z) * T);
	}
}

/** Private state: Slate widgets, the desktop grammar, the feel director, perf / quality tracking. */
struct FFourfoldControllerImpl
{
	TSharedPtr<SFourfoldUiRoot> Ui;
	ffg::DesktopInput Desk;
	FFourfoldFeel Feel;
	FString LastDevice = kMobile ? TEXT("touch") : TEXT("keyboard");
	bool bTouchUi = kMobile;
	bool bPadAxisWasDown[2] = {false, false};
	// Menu navigation by stick (with repeat).
	float NavRepeatT = 0.0f;
	int32 NavStickDir = 0;
	// Perf text + adaptive quality.
	TArray<float> FrameMs;
	float QualityEvalT = 0.0f;
	float QualityTimer = 0.0f;
	double PerfT = 0.0;
	int32 PerfFrames = 0;
	FString PerfText;
	// Title / watch scenario options.
	ff::ScenarioOptions AttractOptions;
};

AFourfoldPlayerController::AFourfoldPlayerController()
{
	bAutoManageActiveCameraTarget = false;   // the view target is our camera rig, always
	bShowMouseCursor = !kMobile;
	bEnableClickEvents = false;
	bEnableTouchEvents = false;
	PrimaryActorTick.bTickEvenWhenPaused = true;
}

UFourfoldSimSubsystem* AFourfoldPlayerController::GetSim() const
{
	return UFourfoldSimSubsystem::Get(this);
}

void AFourfoldPlayerController::UiCue(FName Cue) const
{
	if (UFourfoldSimSubsystem* Sim = GetSim())
	{
		Sim->OnUiCue.Broadcast(Cue);
	}
}

bool AFourfoldPlayerController::IsTouchUiActive() const
{
	return Impl.IsValid() && Impl->bTouchUi;
}

// ---------------------------------------------------------------------------------------------- lifecycle

void AFourfoldPlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (!IsLocalController())
	{
		return;
	}
	Impl = MakeShared<FFourfoldControllerImpl>();
	Impl->AttractOptions.autoplay = "duel";
	ActivateTouchInterface(nullptr);   // no engine virtual joystick: the Slate overlay is the touch HUD

	UWorld* World = GetWorld();
	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.ObjectFlags |= RF_Transient;
	CameraRig = World->SpawnActor<AFourfoldCameraRig>(AFourfoldCameraRig::StaticClass(), FTransform::Identity, Params);
	if (CameraRig)
	{
		SetViewTarget(CameraRig);
	}
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->ActorHasTag(FName(TEXT("FourfoldArena"))))
		{
			bLevelHasArena = true;
			break;
		}
	}

	BuildUi();

	if (UFourfoldSimSubsystem* Sim = GetSim())
	{
		Sim->PollInput.BindUObject(this, &AFourfoldPlayerController::PollSimInput);
		FrameHandle = Sim->OnFrame.AddUObject(this, &AFourfoldPlayerController::OnSimFrame);
		ScenarioHandle = Sim->OnScenarioLoaded.AddUObject(this, &AFourfoldPlayerController::OnScenarioLoaded);
	}
	if (UFourfoldSettingsSubsystem* Settings = UFourfoldSettingsSubsystem::Get(this))
	{
		SettingsHandle = Settings->OnChanged.AddUObject(this, &AFourfoldPlayerController::OnSettingsChanged);
		OnSettingsChanged(Settings->GetSettings());
	}
	DeactivateHandle = FCoreDelegates::ApplicationWillDeactivateDelegate.AddUObject(this, &AFourfoldPlayerController::OnAppDeactivate);
	BackgroundHandle = FCoreDelegates::ApplicationWillEnterBackgroundDelegate.AddUObject(this, &AFourfoldPlayerController::OnAppDeactivate);
	ReactivateHandle = FCoreDelegates::ApplicationHasReactivatedDelegate.AddUObject(this, &AFourfoldPlayerController::OnAppReactivate);

	SetMouseLook(false);

	// Command line: -scenario=<id> [-autoplay=duel] skips the title (captures, device tests).
	FString StartId, Autoplay;
	FParse::Value(FCommandLine::Get(), TEXT("scenario="), StartId);
	FParse::Value(FCommandLine::Get(), TEXT("autoplay="), Autoplay);
	if (!StartId.IsEmpty())
	{
		ff::ScenarioOptions Opts;
		Opts.autoplay = TCHAR_TO_UTF8(*Autoplay);
		if (!Autoplay.IsEmpty())
		{
			if (UFourfoldSimSubsystem* Sim = GetSim(); Sim && Sim->LoadScenario(StartId, Opts))
			{
				Mode = EFourfoldMode::Watch;
				if (Impl->Ui.IsValid())
				{
					Impl->Ui->GetMenus()->Open(EFFMenuPage::Watch);
				}
				return;
			}
		}
		else if (StartScenario(StartId, Opts))
		{
			return;
		}
	}
	ShowTitle();
}

void AFourfoldPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UFourfoldSimSubsystem* Sim = GetSim())
	{
		Sim->PollInput.Unbind();
		Sim->OnFrame.Remove(FrameHandle);
		Sim->OnScenarioLoaded.Remove(ScenarioHandle);
	}
	if (UFourfoldSettingsSubsystem* Settings = UFourfoldSettingsSubsystem::Get(this))
	{
		Settings->OnChanged.Remove(SettingsHandle);
	}
	FCoreDelegates::ApplicationWillDeactivateDelegate.Remove(DeactivateHandle);
	FCoreDelegates::ApplicationWillEnterBackgroundDelegate.Remove(BackgroundHandle);
	FCoreDelegates::ApplicationHasReactivatedDelegate.Remove(ReactivateHandle);
	if (Impl.IsValid())
	{
		Impl->Feel.ReleaseHaptics();
		if (Impl->Ui.IsValid())
		{
			if (UGameViewportClient* VC = GetWorld() ? GetWorld()->GetGameViewport() : nullptr)
			{
				VC->RemoveViewportWidgetContent(Impl->Ui.ToSharedRef());
			}
			Impl->Ui.Reset();
		}
	}
	Super::EndPlay(EndPlayReason);
}

void AFourfoldPlayerController::BuildUi()
{
	UGameViewportClient* VC = GetWorld() ? GetWorld()->GetGameViewport() : nullptr;
	if (!VC || !FSlateApplication::IsInitialized())
	{
		UE_LOG(LogFourfold, Warning, TEXT("No game viewport: the Fourfold UI is not created (dedicated server / commandlet?)"));
		return;
	}
	TWeakObjectPtr<AFourfoldPlayerController> WeakThis(this);

	FFourfoldMenuHost Menu;
	Menu.UiCue = [WeakThis](FName Cue) {
		if (WeakThis.IsValid())
		{
			WeakThis->UiCue(Cue);
		}
	};
	Menu.GetSettings = [WeakThis]() {
		const UFourfoldSettingsSubsystem* S = WeakThis.IsValid() ? UFourfoldSettingsSubsystem::Get(WeakThis.Get()) : nullptr;
		return S ? S->GetSettings() : FFourfoldSettings();
	};
	Menu.SetSettings = [WeakThis](const FFourfoldSettings& NewSettings, bool bSave) {
		if (UFourfoldSettingsSubsystem* S = WeakThis.IsValid() ? UFourfoldSettingsSubsystem::Get(WeakThis.Get()) : nullptr)
		{
			S->SetSettings(NewSettings, bSave);
		}
	};
	Menu.GetPracticeItems = [WeakThis]() {
		UFourfoldSimSubsystem* Sim = WeakThis.IsValid() ? WeakThis->GetSim() : nullptr;
		return Sim ? Sim->GetSession().PracticeItems() : std::vector<ff::PracticeItem>();
	};
	Menu.StartScenario = [WeakThis](const FString& Id, const ff::ScenarioOptions& Opts) {
		return WeakThis.IsValid() && WeakThis->StartScenario(Id, Opts);
	};
	Menu.StartWatch = [WeakThis]() {
		if (WeakThis.IsValid())
		{
			WeakThis->StartWatch();
		}
	};
	Menu.Resume = [WeakThis]() {
		if (WeakThis.IsValid())
		{
			WeakThis->ResumeGame();
		}
	};
	Menu.RestartScenario = [WeakThis]() {
		if (UFourfoldSimSubsystem* Sim = WeakThis.IsValid() ? WeakThis->GetSim() : nullptr)
		{
			Sim->RestartScenario();
			WeakThis->ResumeGame();
		}
	};
	Menu.QuitToTitle = [WeakThis]() {
		if (WeakThis.IsValid())
		{
			WeakThis->QuitToTitle();
		}
	};
	Menu.OpenLabPanel = [WeakThis]() {
		if (WeakThis.IsValid())
		{
			WeakThis->ResumeGame();
			if (!WeakThis->bLabOpen)
			{
				WeakThis->ToggleLabPanel();
			}
		}
	};
	Menu.ResetProgress = [WeakThis]() {
		if (UFourfoldSimSubsystem* Sim = WeakThis.IsValid() ? WeakThis->GetSim() : nullptr)
		{
			Sim->GetSession().ResetProgress();
			Sim->SaveProgress();
			if (Sim->HasScenario() && WeakThis->Mode == EFourfoldMode::Play)
			{
				Sim->RestartScenario();
			}
		}
	};
	Menu.Toast = [WeakThis](const FString& Text) {
		if (WeakThis.IsValid() && WeakThis->Impl.IsValid() && WeakThis->Impl->Ui.IsValid())
		{
			WeakThis->Impl->Ui->GetHud()->Toast(Text);
		}
	};

	FFourfoldLabHost Lab;
	Lab.Session = [WeakThis]() -> ff::Session* {
		UFourfoldSimSubsystem* Sim = WeakThis.IsValid() ? WeakThis->GetSim() : nullptr;
		return (Sim && Sim->HasScenario()) ? &Sim->GetSession() : nullptr;
	};
	Lab.UiCue = Menu.UiCue;
	Lab.Toast = Menu.Toast;
	Lab.Device = [WeakThis]() { return WeakThis.IsValid() && WeakThis->Impl.IsValid() ? WeakThis->Impl->LastDevice : FString(TEXT("keyboard")); };
	Lab.PlayerElementSub = [WeakThis](int32& Element, int32& Sub) {
		UFourfoldSimSubsystem* Sim = WeakThis.IsValid() ? WeakThis->GetSim() : nullptr;
		if (Sim && Sim->HasScenario())
		{
			const ff::Snapshot& Snap = Sim->GetSnapshot();
			if (const ff::ActorView* P = Snap.FindActor(Snap.player_id))
			{
				Element = FMath::Clamp(P->element, 0, 3);
				Sub = FMath::Clamp(P->sub, 0, 3);
			}
		}
	};
	Lab.SaveTuning = [WeakThis]() {
		UFourfoldSimSubsystem* Sim = WeakThis.IsValid() ? WeakThis->GetSim() : nullptr;
		return Sim && Sim->SaveLabTuning();
	};
	Lab.LoadTuning = [WeakThis]() {
		UFourfoldSimSubsystem* Sim = WeakThis.IsValid() ? WeakThis->GetSim() : nullptr;
		return Sim && Sim->LoadLabTuning();
	};
	Lab.RestartScenario = [WeakThis]() {
		if (UFourfoldSimSubsystem* Sim = WeakThis.IsValid() ? WeakThis->GetSim() : nullptr)
		{
			Sim->RestartScenario();
		}
	};
	Lab.LoadScenario = [WeakThis](const FString& Id) {
		if (WeakThis.IsValid())
		{
			WeakThis->StartScenario(Id, ff::ScenarioOptions());
		}
	};
	Lab.OnOpenChanged = [WeakThis](bool bOpen) {
		if (WeakThis.IsValid())
		{
			WeakThis->bLabOpen = bOpen;
			WeakThis->ReleaseAllInput();
		}
	};

	Impl->Ui = SNew(SFourfoldUiRoot, Menu, Lab);
	VC->AddViewportWidgetContent(Impl->Ui.ToSharedRef(), 10);
}

// ---------------------------------------------------------------------------------------------- flow

void AFourfoldPlayerController::ShowTitle()
{
	UFourfoldSimSubsystem* Sim = GetSim();
	if (!Sim || !Impl.IsValid())
	{
		return;
	}
	bPaused = false;
	Sim->SetPaused(false);
	if (bLabOpen && Impl->Ui.IsValid())
	{
		Impl->Ui->GetLab()->Close();
	}
	Mode = EFourfoldMode::Title;
	// The attract duel: the scenario's AI drives both fighters behind the title.
	ff::ScenarioOptions Opts = Impl->AttractOptions;
	Opts.spar_difficulty = "master";
	Opts.spar_kit = "all";
	if (!Sim->LoadScenario(TEXT("spar"), Opts))
	{
		Sim->LoadScenario(TEXT("lab"), Opts);
	}
	if (Impl->Ui.IsValid())
	{
		Impl->Ui->FadeIn(0.6f);
		Impl->Ui->GetMenus()->Open(EFFMenuPage::Title);
	}
	ReleaseAllInput();
}

bool AFourfoldPlayerController::StartScenario(const FString& ScenarioId, const ff::ScenarioOptions& Options)
{
	UFourfoldSimSubsystem* Sim = GetSim();
	if (!Sim || !Impl.IsValid())
	{
		return false;
	}
	ff::ScenarioOptions Opts = Options;
	Opts.autoplay.clear();
	if (!Sim->LoadScenario(ScenarioId, Opts))
	{
		return false;
	}
	Mode = EFourfoldMode::Play;
	bPaused = false;
	Sim->SetPaused(false);
	if (Impl->Ui.IsValid())
	{
		Impl->Ui->FadeIn(0.35f);
		Impl->Ui->GetMenus()->Close();
	}
	ReleaseAllInput();
	UiCue(FName(TEXT("ui_select")));
	return true;
}

void AFourfoldPlayerController::StartWatch()
{
	UFourfoldSimSubsystem* Sim = GetSim();
	if (!Sim || !Impl.IsValid())
	{
		return;
	}
	ff::ScenarioOptions Opts = Impl->AttractOptions;
	Opts.spar_difficulty = "master";
	Opts.spar_kit = "all";
	if (!Sim->LoadScenario(TEXT("spar"), Opts))
	{
		return;
	}
	Mode = EFourfoldMode::Watch;
	bPaused = false;
	Sim->SetPaused(false);
	if (Impl->Ui.IsValid())
	{
		Impl->Ui->FadeIn(0.35f);
		Impl->Ui->GetMenus()->Open(EFFMenuPage::Watch);
	}
	ReleaseAllInput();
}

void AFourfoldPlayerController::PauseGame()
{
	UFourfoldSimSubsystem* Sim = GetSim();
	if (!Sim || !Impl.IsValid() || Mode != EFourfoldMode::Play || bPaused)
	{
		return;
	}
	if (bLabOpen && Impl->Ui.IsValid())
	{
		Impl->Ui->GetLab()->Close();
	}
	bPaused = true;
	Sim->SetPaused(true);
	ReleaseAllInput();
	if (Impl->Ui.IsValid())
	{
		Impl->Ui->GetMenus()->Open(EFFMenuPage::Pause);
	}
	UiCue(FName(TEXT("ui_pause")));
}

void AFourfoldPlayerController::ResumeGame()
{
	UFourfoldSimSubsystem* Sim = GetSim();
	if (!Sim || !Impl.IsValid() || !bPaused)
	{
		return;
	}
	bPaused = false;
	Sim->SetPaused(false);
	if (Impl->Ui.IsValid())
	{
		Impl->Ui->GetMenus()->Close();
	}
	ReleaseAllInput();
	// The Esc / Start that closed the menu is still down: it must not pause again.
	Impl->Desk.ResyncEdgeActions(DeskDownNow(*this));
	UiCue(FName(TEXT("ui_resume")));
}

void AFourfoldPlayerController::QuitToTitle()
{
	if (UFourfoldSimSubsystem* Sim = GetSim())
	{
		Sim->SaveProgress();
	}
	ShowTitle();
}

void AFourfoldPlayerController::ToggleLabPanel()
{
	if (!Impl.IsValid() || !Impl->Ui.IsValid() || Mode != EFourfoldMode::Play || bPaused)
	{
		return;
	}
	Impl->Ui->GetLab()->Toggle();
}

void AFourfoldPlayerController::ReleaseAllInput()
{
	if (!Impl.IsValid())
	{
		return;
	}
	Impl->Desk.CancelAll();
	if (Impl->Ui.IsValid())
	{
		Impl->Ui->GetTouch()->ReleaseAll();
	}
}

void AFourfoldPlayerController::OnAppDeactivate()
{
	// OS interruption (call, notification centre, app switch): release every control and pause, so nothing stays held
	// or keeps charging in the background. A held technique is cancelled, never fired.
	ReleaseAllInput();
	if (Mode == EFourfoldMode::Play && !bPaused)
	{
		PauseGame();
	}
	if (UFourfoldSimSubsystem* Sim = GetSim())
	{
		Sim->SaveProgress();
	}
	if (Impl.IsValid())
	{
		Impl->Feel.ReleaseHaptics();
	}
}

void AFourfoldPlayerController::OnAppReactivate()
{
	ReleaseAllInput();
}

void AFourfoldPlayerController::SetMouseLook(bool bOn)
{
	bMouseLook = bOn && !kMobile;
	if (bMouseLook)
	{
		FInputModeGameOnly InputMode;
		InputMode.SetConsumeCaptureMouseDown(false);
		SetInputMode(InputMode);
		bShowMouseCursor = false;
	}
	else
	{
		FInputModeGameAndUI InputMode;
		InputMode.SetHideCursorDuringCapture(false);
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		SetInputMode(InputMode);
		bShowMouseCursor = !kMobile;
	}
}

// ---------------------------------------------------------------------------------------------- settings

void AFourfoldPlayerController::OnSettingsChanged(const FFourfoldSettings& Settings)
{
	if (!Impl.IsValid())
	{
		return;
	}
	if (Impl->Ui.IsValid())
	{
		Impl->Ui->GetTouch()->ApplySettings(Settings);
		Impl->Ui->GetTouch()->SetMouseEmulation(Settings.TouchUiMode == 1 && !kMobile);
	}
	Impl->Desk.settings.camera_sensitivity = Settings.CameraSensitivity;
	Impl->Desk.settings.invert_y = Settings.bInvertY;
	if (UFourfoldSimSubsystem* Sim = GetSim())
	{
		Impl->Feel.OnSettings(Settings, *Sim, CameraRig);
	}
}

// ---------------------------------------------------------------------------------------------- per frame

void AFourfoldPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);   // processes this frame's input: key states below are current
	if (!Impl.IsValid() || !Impl->Ui.IsValid())
	{
		return;
	}
	const float RealDt = FMath::Clamp(float(FApp::GetDeltaTime()), 0.0f, 0.25f);
	HandleUiKeys();
	UpdateInputRouting();
	SampleDevices(RealDt);
	AdaptQuality(RealDt);

	// Touch outputs: haptics, ring cues, pause button.
	SFourfoldTouchOverlay& Touch = Impl->Ui->GetTouch().Get();
	ffg::TouchControls& Tc = Touch.Controls();
	const UFourfoldSettingsSubsystem* SettingsSys = UFourfoldSettingsSubsystem::Get(this);
	const bool bHaptics = SettingsSys ? SettingsSys->GetSettings().bHaptics : true;
	for (const std::string& K : Tc.haptics)
	{
		Impl->Feel.Haptic(K, bHaptics);
	}
	Tc.haptics.clear();
	if (Tc.ui_ring_opened > 0)
	{
		UiCue(FName(TEXT("ui_ring_open")));
	}
	if (Tc.ui_ring_picked > 0)
	{
		UiCue(FName(TEXT("ui_ring_pick")));
	}
	Tc.ui_ring_opened = 0;
	Tc.ui_ring_picked = 0;
	const bool bTouchPause = Tc.pause_requests > 0;
	Tc.pause_requests = 0;
	const bool bDeskPause = Impl->Desk.pause_requests > 0;
	Impl->Desk.pause_requests = 0;
	if ((bTouchPause || bDeskPause) && Mode == EFourfoldMode::Play && !bPaused && !bLabOpen)
	{
		PauseGame();
	}
}

void AFourfoldPlayerController::HandleUiKeys()
{
	SFourfoldMenus& Menus = Impl->Ui->GetMenus().Get();
	SFourfoldLabPanel& Lab = Impl->Ui->GetLab().Get();

	// Developer keys.
	if (WasInputKeyJustPressed(EKeys::F2) && Mode == EFourfoldMode::Play && !bPaused)
	{
		ToggleLabPanel();
	}
	if (WasInputKeyJustPressed(EKeys::F1))
	{
		SetMouseLook(!bMouseLook);
	}

	// Menu / Lab navigation with keys and pads (touch and mouse use the widgets directly).
	const bool bMenus = Menus.IsOpen() && (Menus.IsModal() || Mode == EFourfoldMode::Watch);
	if (!bMenus && !bLabOpen)
	{
		Impl->NavStickDir = 0;
		return;
	}
	auto Just = [this](std::initializer_list<FKey> Keys) {
		for (const FKey& K : Keys)
		{
			if (WasInputKeyJustPressed(K))
			{
				return true;
			}
		}
		return false;
	};
	auto Nav = [&](EFFNav N) {
		if (bLabOpen)
		{
			Lab.HandleNav(N);
		}
		else
		{
			Menus.HandleNav(N);
		}
	};
	// While the Lab panel is open, WASD / arrows stay with the panel only when a pad or the arrow keys are used.
	if (Just({EKeys::Up, EKeys::Gamepad_DPad_Up}) || (!bLabOpen && Just({EKeys::W}))) Nav(EFFNav::Up);
	if (Just({EKeys::Down, EKeys::Gamepad_DPad_Down}) || (!bLabOpen && Just({EKeys::S}))) Nav(EFFNav::Down);
	if (Just({EKeys::Left, EKeys::Gamepad_DPad_Left}) || (!bLabOpen && Just({EKeys::A}))) Nav(EFFNav::Left);
	if (Just({EKeys::Right, EKeys::Gamepad_DPad_Right}) || (!bLabOpen && Just({EKeys::D}))) Nav(EFFNav::Right);
	if (Just({EKeys::Enter, EKeys::Gamepad_FaceButton_Bottom}) || (!bLabOpen && Just({EKeys::SpaceBar}))) Nav(EFFNav::Accept);
	if (Just({EKeys::Escape, EKeys::BackSpace, EKeys::Gamepad_FaceButton_Right})) Nav(EFFNav::Back);
	if (Just({EKeys::Gamepad_Special_Right}) && bPaused)
	{
		ResumeGame();
	}
	// Left stick: one step per push, repeating while held.
	const float Sy = GetInputAnalogKeyState(EKeys::Gamepad_LeftY);
	const float Sx = GetInputAnalogKeyState(EKeys::Gamepad_LeftX);
	int32 Dir = 0;
	if (FMath::Abs(Sy) > 0.6f && FMath::Abs(Sy) >= FMath::Abs(Sx)) Dir = Sy > 0.0f ? 1 : 2;
	else if (FMath::Abs(Sx) > 0.6f) Dir = Sx > 0.0f ? 4 : 3;
	const float Dt = FMath::Clamp(float(FApp::GetDeltaTime()), 0.0f, 0.1f);
	if (Dir != 0 && (Dir != Impl->NavStickDir || (Impl->NavRepeatT -= Dt) <= 0.0f))
	{
		Impl->NavRepeatT = Dir != Impl->NavStickDir ? 0.4f : 0.12f;
		Nav(Dir == 1 ? EFFNav::Up : (Dir == 2 ? EFFNav::Down : (Dir == 3 ? EFFNav::Left : EFFNav::Right)));
	}
	Impl->NavStickDir = Dir;
}

void AFourfoldPlayerController::UpdateInputRouting()
{
	const SFourfoldMenus& Menus = Impl->Ui->GetMenus().Get();
	const bool bWant = Mode == EFourfoldMode::Play && !bPaused && !Menus.IsModal() && !bLabOpen;
	if (bGameplayInput && !bWant)
	{
		Impl->Desk.CancelAll();   // let go of everything; a held technique is cancelled, not fired
	}
	else if (!bGameplayInput && bWant)
	{
		// Keys still down from the menu / panel that just closed (Esc, Enter, A) must not fire edge actions.
		Impl->Desk.ResyncEdgeActions(DeskDownNow(*this));
	}
	bGameplayInput = bWant;

	const UFourfoldSettingsSubsystem* SettingsSys = UFourfoldSettingsSubsystem::Get(this);
	// -FFTouchUi (dev captures on the Mac): the touch overlay as on a phone, without touching the saved settings
	static const bool bForceTouch = FParse::Param(FCommandLine::Get(), TEXT("FFTouchUi"));
	const int32 TouchMode = bForceTouch ? 1 : SettingsSys ? SettingsSys->GetSettings().TouchUiMode : 0;
	SFourfoldTouchOverlay& Touch = Impl->Ui->GetTouch().Get();
	Impl->bTouchUi = TouchMode == 1 || (TouchMode == 0 && (kMobile || Touch.SawTouch()));
	Touch.SetActive(bGameplayInput && Impl->bTouchUi);
	Impl->Ui->GetHud()->SetGameplayVisible(Mode != EFourfoldMode::Title && !(bPaused && Menus.IsModal()));
}

void AFourfoldPlayerController::SampleDevices(float RealDt)
{
	ffg::DeviceSample S;
	S.dt = RealDt;
	bool bAnyPad = false, bAnyKey = false;
	for (const FDeskBinding& B : DeskBindings())
	{
		const size_t I = size_t(ffg::DA(B.Action));
		for (const FKey& K : B.Keys)
		{
			const bool bDown = IsInputKeyDown(K);
			const bool bJust = WasInputKeyJustPressed(K);
			S.down[I] = S.down[I] || bDown;
			S.just_pressed[I] = S.just_pressed[I] || bJust;
			if (bJust)
			{
				(K.IsGamepadKey() ? bAnyPad : bAnyKey) = true;
			}
		}
		if (B.Axis.IsValid())
		{
			const int32 AxisSlot = B.Action == ffg::DeskAction::Tech ? 0 : 1;
			const bool bDown = GetInputAnalogKeyState(B.Axis) > 0.5f;
			const bool bWas = Impl->bPadAxisWasDown[AxisSlot];
			Impl->bPadAxisWasDown[AxisSlot] = bDown;
			S.down[I] = S.down[I] || bDown;
			S.just_pressed[I] = S.just_pressed[I] || (bDown && !bWas);
			bAnyPad = bAnyPad || (bDown && !bWas);
		}
	}
	// Move: WASD + left stick; camera: arrows + right stick (y up).
	auto Axis = [this](const FKey& Pos, const FKey& Neg) { return (IsInputKeyDown(Pos) ? 1.0f : 0.0f) - (IsInputKeyDown(Neg) ? 1.0f : 0.0f); };
	const ff::Vec2 Keys(Axis(EKeys::D, EKeys::A), Axis(EKeys::W, EKeys::S));
	const ff::Vec2 Pad(GetInputAnalogKeyState(EKeys::Gamepad_LeftX), GetInputAnalogKeyState(EKeys::Gamepad_LeftY));
	S.move = (Keys + Pad).limit_length(1.0f);
	const ff::Vec2 CamKeys(Axis(EKeys::Right, EKeys::Left), Axis(EKeys::Up, EKeys::Down));
	const ff::Vec2 CamPad(GetInputAnalogKeyState(EKeys::Gamepad_RightX), GetInputAnalogKeyState(EKeys::Gamepad_RightY));
	S.cam_stick = (CamKeys + CamPad).limit_length(1.0f);
	if (Pad.length_squared() > 0.04f || CamPad.length_squared() > 0.04f)
	{
		bAnyPad = true;
	}
	// Mouse: raw pixel deltas (UE's MouseY is up-positive; the grammar wants y down).
	if (PlayerInput)
	{
		S.mouse_delta = ff::Vec2(PlayerInput->GetRawKeyValue(EKeys::MouseX), -PlayerInput->GetRawKeyValue(EKeys::MouseY));
	}
	S.mouse_look = bMouseLook || IsInputKeyDown(EKeys::MiddleMouseButton);

	// Active device: the Lab move list shows its inputs; a pad on a phone hides the touch HUD until the next touch.
	if (bAnyPad)
	{
		Impl->LastDevice = TEXT("gamepad");
		if (kMobile)
		{
			Impl->Ui->GetTouch()->SetGamepadHidden(true);
		}
	}
	else if (bAnyKey)
	{
		Impl->LastDevice = TEXT("keyboard");
	}
	if (Impl->bTouchUi && !Impl->Ui->GetTouch()->IsGamepadHidden() && (kMobile || Impl->Ui->GetTouch()->SawTouch()))
	{
		Impl->LastDevice = bAnyPad ? FString(TEXT("gamepad")) : FString(TEXT("touch"));
	}

	if (!bGameplayInput)
	{
		return;
	}
	// Aim radius: min(0.15 vh, 140) px, >= 40 (desktop_input.gd).
	int32 Vx = 0, Vy = 0;
	GetViewportSize(Vx, Vy);
	Impl->Desk.aim_radius_px = FMath::Max(40.0f, FMath::Min(0.15f * float(Vy), 140.0f));
	Impl->Desk.Sample(S);
}

void AFourfoldPlayerController::PollSimInput(ff::InputFrame& Out, float& InOutCameraYawSim)
{
	ff::InputFrame F;
	if (Impl.IsValid() && Impl->Ui.IsValid())
	{
		SFourfoldTouchOverlay& Touch = Impl->Ui->GetTouch().Get();
		if (bGameplayInput)
		{
			if (Touch.IsActive())
			{
				Touch.FillFrame(F);
			}
			ff::InputFrame D;
			Impl->Desk.FillFrame(D);
			F.MergeFrom(D);
		}
		else
		{
			// Drain whatever was latched before input was blocked (never delivered late).
			ff::InputFrame Scratch;
			Impl->Desk.FillFrame(Scratch);
			Touch.FillFrame(Scratch);
		}
	}
#if !UE_BUILD_SHIPPING
	// Dev input script for animation captures: -FFMove="<s>:<x>,<y>|<s>:<x>,<y>|..." holds the stick at (x, y) from
	// each time (seconds after the first polled frame), e.g. "3:0,1|4.5:0,0" = run forward 1.5 s, then stop.
	{
		static TArray<TPair<double, ff::Vec2>> Script;
		static double ScriptStart = -1.0;
		static bool bParsed = false;
		if (!bParsed)
		{
			bParsed = true;
			FString Spec;
			if (FParse::Value(FCommandLine::Get(), TEXT("-FFMove="), Spec, false))
			{
				TArray<FString> Parts;
				Spec.ParseIntoArray(Parts, TEXT("|"));
				for (const FString& P : Parts)
				{
					FString When, XY, X, Y;
					if (P.Split(TEXT(":"), &When, &XY) && XY.Split(TEXT(","), &X, &Y))
					{
						Script.Add({FCString::Atod(*When), ff::Vec2(FCString::Atof(*X), FCString::Atof(*Y))});
					}
				}
			}
		}
		if (Script.Num() > 0)
		{
			const double Now = FPlatformTime::Seconds();
			if (ScriptStart < 0.0) ScriptStart = Now;
			const double T = Now - ScriptStart;
			for (int32 i = Script.Num() - 1; i >= 0; --i)
			{
				if (T >= Script[i].Key)
				{
					F.move = Script[i].Value;
					break;
				}
			}
		}
	}
#endif
	if (CameraRig)
	{
		CameraRig->Logic.AddInput(F.cam_delta);
		InOutCameraYawSim = CameraRig->Logic.ViewYaw();
	}
	Out = F;
}

void AFourfoldPlayerController::AdaptQuality(float RealDt)
{
	// Perf line (debug overlay) once per second.
	Impl->PerfT += RealDt;
	++Impl->PerfFrames;
	if (Impl->PerfT >= 1.0)
	{
		Impl->PerfText = FString::Printf(TEXT("%.0f fps  %.1f ms"), double(Impl->PerfFrames) / Impl->PerfT, 1000.0 * Impl->PerfT / FMath::Max(1, Impl->PerfFrames));
		Impl->PerfT = 0.0;
		Impl->PerfFrames = 0;
	}
	// Sustained frame-time p95 over budget for 4 s steps the auto quality tier down (never up mid-session).
	UFourfoldSettingsSubsystem* SettingsSys = UFourfoldSettingsSubsystem::Get(this);
	if (!SettingsSys || SettingsSys->GetSettings().Quality >= 0 || SettingsSys->GetEffectiveQuality() == 0 || bPaused)
	{
		Impl->FrameMs.Reset();
		return;
	}
	Impl->FrameMs.Add(RealDt * 1000.0f);
	if (Impl->FrameMs.Num() > 240)
	{
		Impl->FrameMs.RemoveAt(0, Impl->FrameMs.Num() - 240);
	}
	if (Impl->FrameMs.Num() < 240)
	{
		Impl->QualityEvalT = 0.0f;
		return;
	}
	Impl->QualityEvalT += RealDt;
	if (Impl->QualityEvalT < 0.25f)
	{
		return;
	}
	TArray<float> Sorted = Impl->FrameMs;
	Sorted.Sort();
	const float P95 = Sorted[FMath::Clamp(int32(Sorted.Num() * 0.95f), 0, Sorted.Num() - 1)];
	const float Budget = 1000.0f / float(FMath::Max(30, SettingsSys->GetSettings().FrameRateCap)) * 1.11f;
	if (P95 > Budget)
	{
		Impl->QualityTimer += Impl->QualityEvalT;
		if (Impl->QualityTimer > 4.0f && SettingsSys->StepDownAutoQuality())
		{
			UE_LOG(LogFourfold, Log, TEXT("Quality stepped down to %d (p95 %.1f ms)"), SettingsSys->GetEffectiveQuality(), P95);
			Impl->QualityTimer = 0.0f;
			Impl->FrameMs.Reset();
		}
	}
	else
	{
		Impl->QualityTimer = 0.0f;
	}
	Impl->QualityEvalT = 0.0f;
}

// ---------------------------------------------------------------------------------------------- sim frame

void AFourfoldPlayerController::OnScenarioLoaded(const FString& ScenarioId)
{
	UFourfoldSimSubsystem* Sim = GetSim();
	if (!Sim)
	{
		return;
	}
	EnsureArena();
	const ff::Snapshot& Snap = Sim->GetSnapshot();
	const ff::ActorView* P = Snap.FindActor(Snap.player_id);
	const ff::Vec3 PlayerPos = P ? P->pos : ff::Vec3();
	ff::Vec3 Look = PlayerPos + ff::Vec3(0.0f, 0.0f, -5.0f);
	if (const ff::ActorView* R = Snap.FindActor(Snap.rival_id))
	{
		Look = R->pos;
	}
	else
	{
		for (const ff::ActorView& A : Snap.actors)
		{
			if (A.id != Snap.player_id)
			{
				Look = A.pos;
				break;
			}
		}
	}
	if (CameraRig)
	{
		CameraRig->ResetForScenario(Sim->GetArena(), PlayerPos, Look);
	}
	if (Impl.IsValid() && Impl->Ui.IsValid())
	{
		Impl->Ui->GetLab()->OnScenarioLoaded();
	}
	ReleaseAllInput();
}

void AFourfoldPlayerController::EnsureArena()
{
	UFourfoldSimSubsystem* Sim = GetSim();
	if (bLevelHasArena || !Sim)
	{
		return;
	}
	if (!PlaceholderArena)
	{
		FActorSpawnParameters Params;
		Params.Owner = this;
		Params.ObjectFlags |= RF_Transient;
		PlaceholderArena = GetWorld()->SpawnActor<AFourfoldPlaceholderArena>(AFourfoldPlaceholderArena::StaticClass(), FTransform::Identity, Params);
	}
	if (PlaceholderArena)
	{
		PlaceholderArena->Build(Sim->GetArena());
	}
}

bool AFourfoldPlayerController::ProjectSim(const ff::Vec3& SimPos, FVector2D& OutPixels) const
{
	if (!CameraRig || !CameraRig->GetCamera())
	{
		return false;
	}
	const UCameraComponent* Cam = CameraRig->GetCamera();
	const FVector Local = Cam->GetComponentTransform().InverseTransformPositionNoScale(FF::ToUE(SimPos));   // X fwd, Y right, Z up
	if (Local.X < 1.0)
	{
		return false;
	}
	int32 Vx = 0, Vy = 0;
	GetViewportSize(Vx, Vy);
	if (Vx <= 0 || Vy <= 0)
	{
		return false;
	}
	// Horizontal FOV, square pixels (AspectRatio_MaintainXFOV, the default axis constraint).
	const double HalfTan = FMath::Tan(FMath::DegreesToRadians(double(Cam->FieldOfView)) * 0.5);
	const double F = (double(Vx) * 0.5) / FMath::Max(HalfTan, 1e-3);
	OutPixels = FVector2D(double(Vx) * 0.5 + Local.Y / Local.X * F, double(Vy) * 0.5 - Local.Z / Local.X * F);
	return true;
}

void AFourfoldPlayerController::OnSimFrame(const FFourfoldFrame& Frame)
{
	UFourfoldSimSubsystem* Sim = GetSim();
	if (!Sim || !Impl.IsValid() || !Frame.Curr || !Frame.Prev || !Sim->HasScenario())
	{
		return;
	}
	const ff::Snapshot& Cur = *Frame.Curr;
	const ff::Snapshot& Prev = *Frame.Prev;
	const UFourfoldSettingsSubsystem* SettingsSys = UFourfoldSettingsSubsystem::Get(this);
	const FFourfoldSettings Settings = SettingsSys ? SettingsSys->GetSettings() : FFourfoldSettings();

	// ---- camera: the interpolated player, the locked target and the nearest incoming threat
	const ff::ActorView* P = Cur.FindActor(Cur.player_id);
	if (CameraRig && P)
	{
		const ff::ActorView* PP = Prev.FindActor(Cur.player_id);
		const ff::Vec3 PlayerPos = PP ? LerpV(PP->pos, P->pos, Frame.Alpha) : P->pos;
		ff::Vec3 TargetPos, ThreatPos;
		const ff::Vec3* Target = nullptr;
		const ff::Vec3* Threat = nullptr;
		if (const ff::ActorView* T = Cur.FindActor(P->lock_target))
		{
			const ff::ActorView* TP = Prev.FindActor(T->id);
			TargetPos = TP ? LerpV(TP->pos, T->pos, Frame.Alpha) : T->pos;
			Target = &TargetPos;
		}
		const ff::Vec3 Chest = P->pos + ff::Vec3(0.0f, 1.25f, 0.0f);
		for (const ff::BodyView& B : Cur.bodies)
		{
			if (B.attack_id == 0 || B.attack_owner == Cur.player_id)
			{
				continue;
			}
			const ff::Vec3 To = Chest - B.pos;
			if (To.length() < 14.0f && (B.form == ff::Form::Wave || B.vel.dot(To) > 0.0f))
			{
				ThreatPos = B.pos;
				Threat = &ThreatPos;
				break;
			}
		}
		CameraRig->UpdateRig(Frame.GameDeltaSeconds, Frame.RealDeltaSeconds, PlayerPos, Target, Threat);
		if (PlaceholderArena)
		{
			PlaceholderArena->SetSeeThrough(CameraRig->GetSeeThrough());
		}
		else if (bLevelHasArena)
		{
			AFourfoldPlaceholderArena::SetSeeThroughOnLevel(GetWorld(), CameraRig->GetSeeThrough(), LevelSeeThrough);
		}
	}

	if (!Impl->Ui.IsValid())
	{
		return;
	}
	SFourfoldHud& Hud = Impl->Ui->GetHud().Get();

	// ---- feel (hit-stop, shake, haptics, flashes, toasts). Behind the title and in Watch nobody holds the phone's
	// fighter: no haptics there, and the title stays free of toasts / flashes.
	FFourfoldSettings FeelSettings = Settings;
	FeelSettings.bHaptics = Settings.bHaptics && Mode == EFourfoldMode::Play;
	if (!Frame.bPaused && Impl->Feel.Apply(Frame, FeelSettings, *Sim, CameraRig, Mode == EFourfoldMode::Title ? nullptr : &Hud) > 0)
	{
		UiCue(FName(TEXT("ui_toast")));
	}

	// ---- HUD + control contexts
	ff::Session& Session = Sim->GetSession();
	const ff::HudModel Model = Session.BuildHud();
	Impl->Ui->GetTouch()->SetHud(Model);
	ffg::DesktopContextFromHud(Model, Impl->Desk);

	const ff::LabState& LabState = Session.Lab();
	SFourfoldHud::FFrameInput In;
	In.RealDt = Frame.RealDeltaSeconds;
	if (CameraRig)
	{
		In.CamForwardSim = CameraRig->Logic.ForwardFlat();
	}
	int32 Vx = 0, Vy = 0;
	GetViewportSize(Vx, Vy);
	In.ViewportPixels = FVector2D(FMath::Max(Vx, 1), FMath::Max(Vy, 1));
	In.bLabOverlay = LabState.overlay;
	In.bShowDebug = Settings.bShowDebug;
	In.bReducedMotion = Settings.bReducedMotion;
	In.Flashes = Settings.Flashes;
	if (Mode == EFourfoldMode::Play)
	{
		TArray<FString> Bits;
		if (LabState.frozen) Bits.Add(TEXT("FROZEN"));
		if (FMath::Abs(LabState.time_scale - 1.0f) > 0.01f) Bits.Add(FString::Printf(TEXT("x%.2f"), LabState.time_scale));
		if (LabState.god) Bits.Add(TEXT("GOD"));
		if (LabState.infinite) Bits.Add(TEXT("INF"));
		In.LabStatus = FString::Join(Bits, TEXT("  "));
	}
	if (Settings.bShowDebug)
	{
		for (const std::string& Line : Session.DebugLines())
		{
			In.DebugLines.Add(ToF(Line));
		}
		In.PerfText = Impl->PerfText;
	}
	In.Events = Frame.bPaused ? nullptr : Frame.Events;
	Hud.UpdateFrame(Model, Cur, In, [this](const ff::Vec3& SimPos, FVector2D& OutPx) { return ProjectSim(SimPos, OutPx); });

	// ---- world-space debug overlay
	if (LabState.overlay || Settings.bShowDebug)
	{
		AFourfoldPlaceholderArena::DrawDebug(GetWorld(), Sim->GetArena(), Cur);
	}
}
