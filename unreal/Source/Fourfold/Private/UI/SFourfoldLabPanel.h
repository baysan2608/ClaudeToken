// Fourfold - the Lab dev panel (port of game/ui/lab/lab_panel.gd + lab_page_{dev,spawn,moves,combos,matrix,tuning}.gd)
// on the ff::Session Lab facade: time scale / freeze / frame step / cheats / rival AI / world (Dev), the spawn catalogue
// with parameters (Spawn), the move list with Try (Moves), the combo trainer with live step bars (Combos), the
// counter-matrix viewer with Stage (Matrix) and live tuning with save / load (Tuning). F2 on desktop, "Lab tools" in
// the pause menu on touch. Gameplay keeps running behind it; gameplay input is blocked while it is open.
#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"
#include "UI/SFourfoldMenus.h"
#include "Widgets/SCompoundWidget.h"
#include "ff/Session.h"

class SBorder;
class SScrollBox;
class SFFPressable;
class FFFPageBuilder;

struct FFourfoldLabHost
{
	TFunction<ff::Session*()> Session;
	TFunction<void(FName)> UiCue;
	TFunction<void(const FString&)> Toast;
	/** touch | keyboard | gamepad: which inputs the move list and combo steps show. */
	TFunction<FString()> Device;
	/** The local player's current element / sub-element (move list default). */
	TFunction<void(int32& /*Element*/, int32& /*Sub*/)> PlayerElementSub;
	TFunction<bool()> SaveTuning;
	TFunction<bool()> LoadTuning;
	TFunction<void()> RestartScenario;
	TFunction<void(const FString& /*ScenarioId*/)> LoadScenario;
	/** The panel opened / closed (the controller blocks gameplay input while open). */
	TFunction<void(bool /*bOpen*/)> OnOpenChanged;
};

class SFourfoldLabPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SFourfoldLabPanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const FFourfoldLabHost& InHost);

	void Open(int32 InPage = -1);
	void Close();
	void Toggle();
	bool IsOpen() const { return bOpen; }
	bool HandleNav(EFFNav Nav);
	void Rebuild();
	/** Scenario changed: pages that list scenario state refresh. */
	void OnScenarioLoaded();

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnTouchStarted(const FGeometry& MyGeometry, const FPointerEvent& InTouchEvent) override;
	virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual bool SupportsKeyboardFocus() const override { return false; }

private:
	ff::Session* S() const { return Host.Session ? Host.Session() : nullptr; }
	void Cue(const TCHAR* Name) const;
	void Say(const FString& Text) const;
	void BuildPage();
	void BuildDev(FFFPageBuilder& B);
	void BuildSpawn(FFFPageBuilder& B);
	void BuildMoves(FFFPageBuilder& B);
	void BuildCombos(FFFPageBuilder& B);
	void BuildMatrix(FFFPageBuilder& B);
	void BuildTuning(FFFPageBuilder& B);
	void SetLab(TFunctionRef<void(ff::LabState&)> Fn, bool bApplyAi = false);
	void UpdateMatrix();
	void RefreshTuningFields();
	void SetFocusIndex(int32 Index);
	FString InputText(ff::Slot Slot) const;
	FString DeviceName() const;

	FFourfoldLabHost Host;
	bool bOpen = false;
	int32 PageIndex = 0;
	TSharedPtr<SBorder> Card;
	TSharedPtr<SScrollBox> Scroll;
	TArray<TSharedRef<SFFPressable>> Focus;
	int32 FocusIndex = -1;
	FVector2D ViewSize = FVector2D(1280.0, 720.0);
	bool bRebuildQueued = false;

	// Spawn
	FString SpawnSelected = TEXT("stone_20");
	bool bSpawnLaunch = true;
	ff::LabSpawnParams SpawnParams;
	// Moves
	int32 MovesElement = 0;
	int32 MovesSub = 0;
	TMap<int32, int32> TierChoice;   // slot -> tier for Try
	// Combos
	FString ComboSelected = TEXT("melt_return");
	bool bComboSlow = true;
	// Matrix
	FString ThreatId = TEXT("stone_45");
	ff::LabSpawnParams ThreatParams;
	FString CounterId = TEXT("swallow");
	int32 CounterTier = 0;
	bool bPerfect = false;
	bool bThreatPicker = false;
	bool bCounterPicker = false;
	ff::MatrixResult Matrix;
	// Tuning
	int32 TuneElement = 0;
	int32 TuneSub = 0;
	FString TuneMove;
	FString TuneStatus;
	std::vector<ff::TuningField> TuneFields;
};
