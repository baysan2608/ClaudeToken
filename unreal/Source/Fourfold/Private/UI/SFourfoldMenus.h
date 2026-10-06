// Fourfold - menus (Slate): title over the attract duel, Free Spar setup, Practice list (Session::PracticeItems with
// options), Watch bar, Settings (every FFourfoldSettings field), Pause and the reset-progress confirmation.
// Port of game/ui/settings_panel.gd (+ the title / spar / watch flow the Unreal build adds). Touch, mouse, keyboard
// and gamepad all work: pressables are touch-first, and HandleNav() drives a focus ring for keys / pads.
#pragma once

#include "CoreMinimal.h"
#include "FourfoldSettings.h"
#include "Templates/Function.h"
#include "Widgets/SCompoundWidget.h"
#include "ff/Session.h"

#include <vector>

class SBorder;
class SBox;
class SScrollBox;
class SFFPressable;
class FFFPageBuilder;

enum class EFFMenuPage : uint8 { None, Title, Spar, Practice, Settings, Confirm, Pause, Watch };
enum class EFFNav : uint8 { Up, Down, Left, Right, Accept, Back };

/** What the menus ask of the game (bound by AFourfoldPlayerController). */
struct FFourfoldMenuHost
{
	TFunction<void(FName)> UiCue;
	TFunction<FFourfoldSettings()> GetSettings;
	TFunction<void(const FFourfoldSettings&, bool /*bSave*/)> SetSettings;
	TFunction<std::vector<ff::PracticeItem>()> GetPracticeItems;
	/** Loads a scenario and starts playing it (menus close). */
	TFunction<bool(const FString& /*Id*/, const ff::ScenarioOptions& /*Options*/)> StartScenario;
	TFunction<void()> StartWatch;
	TFunction<void()> Resume;
	TFunction<void()> RestartScenario;
	TFunction<void()> QuitToTitle;
	TFunction<void()> OpenLabPanel;
	TFunction<void()> ResetProgress;
	TFunction<void(const FString&)> Toast;
};

class SFourfoldMenus : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SFourfoldMenus) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const FFourfoldMenuHost& InHost);

	/** Replaces the page stack with `Page` (None closes). */
	void Open(EFFMenuPage Page);
	void Push(EFFMenuPage Page);
	/** Back one page; returns false at the root (the caller decides: resume, stay on the title). */
	bool Pop();
	void Close() { Open(EFFMenuPage::None); }
	EFFMenuPage GetPage() const { return Stack.Num() > 0 ? Stack.Last() : EFFMenuPage::None; }
	bool IsOpen() const { return GetPage() != EFFMenuPage::None; }
	/** Modal pages stop gameplay input (everything except the Watch bar). */
	bool IsModal() const { return IsOpen() && GetPage() != EFFMenuPage::Watch; }
	/** Keyboard / gamepad navigation. Returns true when consumed. */
	bool HandleNav(EFFNav Nav);
	/** Physical size changed (rotation, window resize): rebuild the current page at the new millimetre scale. */
	void Rebuild();

	// SWidget
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnTouchStarted(const FGeometry& MyGeometry, const FPointerEvent& InTouchEvent) override;
	virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual bool SupportsKeyboardFocus() const override { return false; }

private:
	void BuildPage();
	void BuildTitle(FFFPageBuilder& B);
	void BuildSpar(FFFPageBuilder& B);
	void BuildPractice(FFFPageBuilder& B);
	void BuildSettings(FFFPageBuilder& B);
	void BuildConfirm(FFFPageBuilder& B);
	void BuildPause(FFFPageBuilder& B);
	void BuildWatch(FFFPageBuilder& B);
	void LeavingPage(EFFMenuPage Page);
	void SetFocusIndex(int32 Index);
	void Cue(const TCHAR* Name) const;
	FText PageTitle(EFFMenuPage Page) const;
	void EditSettings(TFunctionRef<void(FFourfoldSettings&)> Fn);

	FFourfoldMenuHost Host;
	TArray<EFFMenuPage> Stack;
	TSharedPtr<SBorder> Backdrop;
	/** Fills the safe area and places CardBox (page-dependent alignment); CardBox sizes the card (SBox overrides only
	 *  size the widget itself, so the alignment must live one level up). */
	TSharedPtr<SBox> PlacementBox;
	TSharedPtr<SBox> CardBox;
	TSharedPtr<SBorder> Card;
	TSharedPtr<SScrollBox> Scroll;
	TArray<TSharedRef<SFFPressable>> Focus;
	int32 FocusIndex = -1;
	FVector2D ViewSize = FVector2D(1280.0, 720.0);

	// Settings page edits (applied live, saved when the page closes).
	FFourfoldSettings Edit;
	bool bSettingsDirty = false;

	// Free Spar setup.
	int32 SparDifficulty = 1;   // novice adept master
	int32 SparKit = 0;          // mixed earth water fire air all
	int32 SparElement = -1;     // -1 scenario default

	// Practice option values (key -> value), seeded from the items.
	TMap<FString, FString> PracticeOptions;
};
