// Fourfold - menus (see SFourfoldMenus.h).
#include "UI/SFourfoldMenus.h"

#include "UI/FourfoldPageBuilder.h"
#include "UI/FourfoldUi.h"

#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

using FFUi::T;

namespace
{
	const TCHAR* const kSparDifficulty[3] = {TEXT("novice"), TEXT("adept"), TEXT("master")};
	const TCHAR* const kSparKits[6] = {TEXT("mixed"), TEXT("earth"), TEXT("water"), TEXT("fire"), TEXT("air"), TEXT("all")};

	FText Pct(float V) { return FText::FromString(FString::Printf(TEXT("%d%%"), FMath::RoundToInt(V * 100.0f))); }
	FText Times(float V) { return FText::FromString(FString::Printf(TEXT("%.2fx"), V)); }
}

void SFourfoldMenus::Construct(const FArguments& InArgs, const FFourfoldMenuHost& InHost)
{
	Host = InHost;
	SetCanTick(true);
	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SAssignNew(Backdrop, SBorder)
			.BorderImage(FFUiBrushes::Dim())
			.Visibility(EVisibility::HitTestInvisible)
		]
		+ SOverlay::Slot()
		.Padding(TAttribute<FMargin>::CreateLambda([]() {
			const FMargin& S = FFUi::SafeLocal;
			const float M = FFUi::Mm(3.0f);
			return FMargin(S.Left + M, S.Top + M, S.Right + M, S.Bottom + M);
		}))
		[
			SAssignNew(PlacementBox, SBox)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SAssignNew(CardBox, SBox)
				.HAlign(HAlign_Fill)
				.VAlign(VAlign_Fill)
				[
					SAssignNew(Card, SBorder)
					.BorderImage(FFUiBrushes::Card())
					.Padding(TAttribute<FMargin>::CreateLambda([]() { return FMargin(FFUi::Mm(3.0f), FFUi::Mm(2.4f)); }))
				]
			]
		]
	];
	SetVisibility(EVisibility::Collapsed);
}

void SFourfoldMenus::Cue(const TCHAR* Name) const
{
	if (Host.UiCue)
	{
		Host.UiCue(FName(Name));
	}
}

void SFourfoldMenus::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	ViewSize = FVector2D(AllottedGeometry.GetLocalSize());
}

FReply SFourfoldMenus::OnMouseButtonDown(const FGeometry&, const FPointerEvent&)
{
	// Modal pages: a click / touch beside the card must never reach the game viewport.
	return IsModal() ? FReply::Handled() : FReply::Unhandled();
}

FReply SFourfoldMenus::OnMouseButtonUp(const FGeometry&, const FPointerEvent&)
{
	return IsModal() ? FReply::Handled() : FReply::Unhandled();
}

FReply SFourfoldMenus::OnTouchStarted(const FGeometry&, const FPointerEvent&)
{
	return IsModal() ? FReply::Handled() : FReply::Unhandled();
}

FReply SFourfoldMenus::OnMouseWheel(const FGeometry&, const FPointerEvent&)
{
	return IsModal() ? FReply::Handled() : FReply::Unhandled();
}

FText SFourfoldMenus::PageTitle(EFFMenuPage Page) const
{
	switch (Page)
	{
	case EFFMenuPage::Spar: return T(TEXT("Free Spar"));
	case EFFMenuPage::Practice: return T(TEXT("Practice"));
	case EFFMenuPage::Settings: return T(TEXT("Settings"));
	case EFFMenuPage::Confirm: return T(TEXT("Reset progress"));
	case EFFMenuPage::Pause: return T(TEXT("Paused"));
	default: return FText::GetEmpty();
	}
}

void SFourfoldMenus::Open(EFFMenuPage Page)
{
	while (Stack.Num() > 0)
	{
		LeavingPage(Stack.Pop());
	}
	if (Page != EFFMenuPage::None)
	{
		Stack.Add(Page);
	}
	BuildPage();
}

void SFourfoldMenus::Push(EFFMenuPage Page)
{
	if (Page == EFFMenuPage::None)
	{
		return;
	}
	Stack.Add(Page);
	Cue(TEXT("ui_open"));
	BuildPage();
}

bool SFourfoldMenus::Pop()
{
	if (Stack.Num() <= 1)
	{
		return false;
	}
	LeavingPage(Stack.Pop());
	Cue(TEXT("ui_back"));
	BuildPage();
	return true;
}

void SFourfoldMenus::LeavingPage(EFFMenuPage Page)
{
	if (Page == EFFMenuPage::Settings && bSettingsDirty && Host.SetSettings)
	{
		Host.SetSettings(Edit, true);   // persisted once when the page closes
		bSettingsDirty = false;
	}
}

void SFourfoldMenus::Rebuild()
{
	if (IsOpen())
	{
		BuildPage();
	}
}

void SFourfoldMenus::EditSettings(TFunctionRef<void(FFourfoldSettings&)> Fn)
{
	Fn(Edit);
	bSettingsDirty = true;
	if (Host.SetSettings)
	{
		Host.SetSettings(Edit, false);   // applied live
	}
	if (Host.GetSettings)
	{
		Edit = Host.GetSettings();       // clamped copy
	}
}

void SFourfoldMenus::SetFocusIndex(int32 Index)
{
	if (Focus.IsValidIndex(FocusIndex))
	{
		Focus[FocusIndex]->bNavFocus = false;
	}
	FocusIndex = Focus.Num() > 0 ? (Index % Focus.Num() + Focus.Num()) % Focus.Num() : -1;
	if (Focus.IsValidIndex(FocusIndex))
	{
		Focus[FocusIndex]->bNavFocus = true;
		if (Scroll.IsValid())
		{
			Scroll->ScrollDescendantIntoView(Focus[FocusIndex], true, EDescendantScrollDestination::IntoView, FFUi::Mm(4.0f));
		}
	}
}

bool SFourfoldMenus::HandleNav(EFFNav Nav)
{
	if (!IsOpen())
	{
		return false;
	}
	const bool bHadFocus = Focus.IsValidIndex(FocusIndex);
	switch (Nav)
	{
	case EFFNav::Up:
	case EFFNav::Down:
		// The first key press only shows the focus ring.
		SetFocusIndex(!bHadFocus ? 0 : FocusIndex + (Nav == EFFNav::Down ? 1 : -1));
		Cue(TEXT("ui_tap"));
		return true;
	case EFFNav::Left:
	case EFFNav::Right:
		if (bHadFocus)
		{
			Focus[FocusIndex]->NavAdjust(Nav == EFFNav::Right ? 1 : -1);
		}
		return true;
	case EFFNav::Accept:
		if (!bHadFocus)
		{
			SetFocusIndex(0);
			return true;
		}
		if (Focus[FocusIndex]->IsPressable())
		{
			// Activation may rebuild the page (and free this widget): keep a reference while it runs.
			const TSharedRef<SFFPressable> Target = Focus[FocusIndex];
			Target->NavActivate();
		}
		return true;
	case EFFNav::Back:
		if (Pop())
		{
			return true;
		}
		if (GetPage() == EFFMenuPage::Pause && Host.Resume)
		{
			Host.Resume();
			return true;
		}
		if (GetPage() == EFFMenuPage::Watch && Host.QuitToTitle)
		{
			Host.QuitToTitle();
			return true;
		}
		return true;
	}
	return false;
}

void SFourfoldMenus::BuildPage()
{
	Focus.Reset();
	FocusIndex = -1;
	const EFFMenuPage Page = GetPage();
	if (Page == EFFMenuPage::None)
	{
		SetVisibility(EVisibility::Collapsed);
		return;
	}
	// Watch: only the small bar takes input; the duel stays visible and the rest of the screen passes through.
	SetVisibility(Page == EFFMenuPage::Watch ? EVisibility::SelfHitTestInvisible : EVisibility::Visible);
	Backdrop->SetVisibility((Page == EFFMenuPage::Title || Page == EFFMenuPage::Watch) ? EVisibility::Collapsed : EVisibility::HitTestInvisible);
	if (Page == EFFMenuPage::Settings && Host.GetSettings)
	{
		Edit = Host.GetSettings();
		bSettingsDirty = false;
	}

	// Card placement: title on the left over the attract duel, watch bar top centre, the rest centred.
	const float MaxW = FFUi::Mm(Page == EFFMenuPage::Title ? 92.0f : (Page == EFFMenuPage::Watch ? 110.0f : 132.0f));
	PlacementBox->SetHAlign(Page == EFFMenuPage::Title ? HAlign_Left : HAlign_Center);
	PlacementBox->SetVAlign(Page == EFFMenuPage::Watch ? VAlign_Top : VAlign_Center);
	CardBox->SetMaxDesiredWidth(MaxW);
	CardBox->SetWidthOverride(Page == EFFMenuPage::Watch ? FOptionalSize() : FOptionalSize(MaxW));

	TSharedRef<SVerticalBox> Content = SNew(SVerticalBox);
	FFFPageBuilder B(Content, Focus, [this](FName N) {
		if (Host.UiCue)
		{
			Host.UiCue(N);
		}
	});

	// Header: page title + back (all pages below a root).
	const FText Title = PageTitle(Page);
	TSharedPtr<SFFButton> BackButton;
	TSharedRef<SVerticalBox> Outer = SNew(SVerticalBox);
	if (!Title.IsEmpty())
	{
		TSharedRef<SHorizontalBox> Head = SNew(SHorizontalBox);
		Head->AddSlot().FillWidth(1.0f).VAlign(VAlign_Center)
		[
			SNew(STextBlock).Text(Title).Font(FFUi::Font(FFUi::Mm(4.6f), true)).ColorAndOpacity(FSlateColor(FFUi::Ink))
		];
		if (Stack.Num() > 1)
		{
			BackButton = SNew(SFFButton)
				.Label(T(TEXT("Back")))
				.bCentered(true)
				.HeightMm(9.0f)
				.OnClicked(FSimpleDelegate::CreateLambda([this]() { Pop(); }));
			Head->AddSlot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(FFUi::Mm(22.0f))[BackButton.ToSharedRef()]];
		}
		Outer->AddSlot().AutoHeight().Padding(FMargin(0.0f, 0.0f, 0.0f, FFUi::Mm(1.4f)))[Head];
	}
	Outer->AddSlot().FillHeight(1.0f)
	[
		SAssignNew(Scroll, SScrollBox)
		.ScrollBarThickness(FVector2f(FFUi::Mm(1.2f), FFUi::Mm(1.2f)))
		+ SScrollBox::Slot()[Content]
	];

	switch (Page)
	{
	case EFFMenuPage::Title: BuildTitle(B); break;
	case EFFMenuPage::Spar: BuildSpar(B); break;
	case EFFMenuPage::Practice: BuildPractice(B); break;
	case EFFMenuPage::Settings: BuildSettings(B); break;
	case EFFMenuPage::Confirm: BuildConfirm(B); break;
	case EFFMenuPage::Pause: BuildPause(B); break;
	case EFFMenuPage::Watch: BuildWatch(B); break;
	default: break;
	}
	if (BackButton.IsValid())
	{
		Focus.Add(BackButton.ToSharedRef());   // last in the navigation order
	}
	// The card never grows past the safe area (it scrolls instead).
	const float MaxH = float(ViewSize.Y) - FFUi::SafeLocal.Top - FFUi::SafeLocal.Bottom - FFUi::Mm(6.0f);
	CardBox->SetMaxDesiredHeight(FMath::Max(MaxH, FFUi::Mm(40.0f)));
	Card->SetContent(Outer);
}

// ---------------------------------------------------------------------------------------------- pages

void SFourfoldMenus::BuildTitle(FFFPageBuilder& B)
{
	B.Add(SNew(STextBlock)
	          .Text(T(TEXT("FOURFOLD")))
	          .Font([]() {
		          FSlateFontInfo F = FFUi::Font(FFUi::Mm(9.0f), true);
		          F.LetterSpacing = 180;
		          return F;
	          }())
	          .ColorAndOpacity(FSlateColor(FFUi::Ink)),
	      0.2f);
	B.Label(T(TEXT("An elemental combat lab: earth, water, fire and air, moved by real martial arts and real physics.")), 2.6f, true);
	B.Spacer(2.0f);
	B.Button(T(TEXT("Lab")), FSimpleDelegate::CreateLambda([this]() {
		if (Host.StartScenario)
		{
			Host.StartScenario(TEXT("lab"), ff::ScenarioOptions());
		}
	}), true, T(TEXT("Every tool: spawn threats, try moves, train combos")), FFUi::ElementColor(0));
	B.Button(T(TEXT("Free Spar")), FSimpleDelegate::CreateLambda([this]() { Push(EFFMenuPage::Spar); }), false, T(TEXT("Fight the rival at your level")),
	         FFUi::ElementColor(2));
	B.Button(T(TEXT("Practice")), FSimpleDelegate::CreateLambda([this]() { Push(EFFMenuPage::Practice); }), false, T(TEXT("Drills and mastery challenges")),
	         FFUi::ElementColor(1));
	B.Button(T(TEXT("Watch")), FSimpleDelegate::CreateLambda([this]() {
		if (Host.StartWatch)
		{
			Host.StartWatch();
		}
	}), false, T(TEXT("Two fighters duel on their own")), FFUi::ElementColor(3));
	B.Button(T(TEXT("Settings")), FSimpleDelegate::CreateLambda([this]() { Push(EFFMenuPage::Settings); }));
}

void SFourfoldMenus::BuildSpar(FFFPageBuilder& B)
{
	B.Label(T(TEXT("One rival, one arena. Rounds reset after a knockdown.")));
	B.Choice(T(TEXT("Rival")), {TEXT("Easy"), TEXT("Normal"), TEXT("Hard")}, TAttribute<int32>::CreateLambda([this]() { return SparDifficulty; }),
	         FFFOnInt::CreateLambda([this](int32 I) { SparDifficulty = FMath::Clamp(I, 0, 2); }));
	B.Choice(T(TEXT("Rival kit")), {TEXT("Earth + Fire"), TEXT("Earth"), TEXT("Water"), TEXT("Fire"), TEXT("Air"), TEXT("All four")},
	         TAttribute<int32>::CreateLambda([this]() { return SparKit; }), FFFOnInt::CreateLambda([this](int32 I) { SparKit = FMath::Clamp(I, 0, 5); }));
	B.Choice(T(TEXT("Your element")), {TEXT("Default"), TEXT("Earth"), TEXT("Water"), TEXT("Fire"), TEXT("Air")},
	         TAttribute<int32>::CreateLambda([this]() { return SparElement + 1; }),
	         FFFOnInt::CreateLambda([this](int32 I) { SparElement = FMath::Clamp(I, 0, 4) - 1; }),
	         TAttribute<FLinearColor>::CreateLambda([this]() { return SparElement >= 0 ? FFUi::ElementColor(SparElement) : FFUi::Accent; }));
	B.Spacer(1.5f);
	B.Button(T(TEXT("Start")), FSimpleDelegate::CreateLambda([this]() {
		ff::ScenarioOptions O;
		O.spar_difficulty = TCHAR_TO_UTF8(kSparDifficulty[FMath::Clamp(SparDifficulty, 0, 2)]);
		O.spar_kit = TCHAR_TO_UTF8(kSparKits[FMath::Clamp(SparKit, 0, 5)]);
		O.player_element = SparElement;
		if (Host.StartScenario)
		{
			Host.StartScenario(TEXT("spar"), O);
		}
	}), true);
}

void SFourfoldMenus::BuildPractice(FFFPageBuilder& B)
{
	const std::vector<ff::PracticeItem> Items = Host.GetPracticeItems ? Host.GetPracticeItems() : std::vector<ff::PracticeItem>();
	if (Items.empty())
	{
		B.Label(T(TEXT("No practice drills available yet.")));
		return;
	}
	for (const ff::PracticeItem& Item : Items)
	{
		const FString Id = FFUi::F(Item.id);
		FString Sub = FFUi::F(Item.subtitle);
		if (Item.locked)
		{
			Sub = TEXT("LOCKED  ") + Sub;
		}
		// Option keys this item uses (sent along when it starts).
		TArray<FString> Keys;
		for (const ff::PracticeOption& Opt : Item.options)
		{
			const FString Key = FFUi::F(Opt.key);
			Keys.Add(Key);
			if (!PracticeOptions.Contains(Key))
			{
				PracticeOptions.Add(Key, FFUi::F(Opt.value));
			}
		}
		B.Button(T(Item.title), FSimpleDelegate::CreateLambda([this, Id, Keys]() {
			ff::ScenarioOptions O;
			for (const FString& K : Keys)
			{
				const FString* V = PracticeOptions.Find(K);
				if (!V)
				{
					continue;
				}
				if (K == TEXT("spar_difficulty"))
				{
					O.spar_difficulty = FFUi::U8(*V);
				}
				else if (K == TEXT("spar_kit"))
				{
					O.spar_kit = FFUi::U8(*V);
				}
			}
			if (Host.StartScenario && !Host.StartScenario(Id, O) && Host.Toast)
			{
				Host.Toast(TEXT("That drill is not available in this build"));
			}
		}), false, FText::FromString(Sub), FFUi::Accent, !Item.locked);
		for (const ff::PracticeOption& Opt : Item.options)
		{
			const FString Key = FFUi::F(Opt.key);
			TArray<FString> Labels, Values;
			for (size_t i = 0; i < Opt.values.size(); ++i)
			{
				Values.Add(FFUi::F(Opt.values[i]));
				Labels.Add(i < Opt.labels.size() ? FFUi::F(Opt.labels[i]) : FFUi::F(Opt.values[i]));
			}
			B.Choice(T(Opt.label), Labels, TAttribute<int32>::CreateLambda([this, Key, Values]() {
				const FString* V = PracticeOptions.Find(Key);
				return V ? Values.IndexOfByKey(*V) : -1;
			}), FFFOnInt::CreateLambda([this, Key, Values](int32 I) {
				if (Values.IsValidIndex(I))
				{
					PracticeOptions.Add(Key, Values[I]);
				}
			}));
		}
	}
}

void SFourfoldMenus::BuildSettings(FFFPageBuilder& B)
{
	// Controls.
	B.Section(T(TEXT("Controls")));
	B.Slider(T(TEXT("Control size")), TAttribute<float>::CreateLambda([this]() { return Edit.ControlScale; }), 0.8f, 1.4f, 0.05f,
	         TAttribute<FText>::CreateLambda([this]() { return Times(Edit.ControlScale); }),
	         FFFOnFloat::CreateLambda([this](float V) { EditSettings([V](FFourfoldSettings& S) { S.ControlScale = V; }); }));
	B.Slider(T(TEXT("Control opacity")), TAttribute<float>::CreateLambda([this]() { return Edit.ControlOpacity; }), 0.3f, 1.0f, 0.05f,
	         TAttribute<FText>::CreateLambda([this]() { return Pct(Edit.ControlOpacity); }),
	         FFFOnFloat::CreateLambda([this](float V) { EditSettings([V](FFourfoldSettings& S) { S.ControlOpacity = V; }); }));
	B.Choice(T(TEXT("Layout")), {TEXT("Default"), TEXT("Compact"), TEXT("Wide")}, TAttribute<int32>::CreateLambda([this]() {
		return Edit.LayoutPreset == TEXT("compact") ? 1 : (Edit.LayoutPreset == TEXT("wide") ? 2 : 0);
	}), FFFOnInt::CreateLambda([this](int32 I) {
		EditSettings([I](FFourfoldSettings& S) { S.LayoutPreset = I == 1 ? TEXT("compact") : (I == 2 ? TEXT("wide") : TEXT("default")); });
	}));
	B.Choice(T(TEXT("Touch controls")), {TEXT("Auto"), TEXT("Always"), TEXT("Never")}, TAttribute<int32>::CreateLambda([this]() { return Edit.TouchUiMode; }),
	         FFFOnInt::CreateLambda([this](int32 I) { EditSettings([I](FFourfoldSettings& S) { S.TouchUiMode = I; }); }));
	B.Toggle(T(TEXT("Left-handed layout")), TAttribute<bool>::CreateLambda([this]() { return Edit.bLeftHanded; }),
	         FFFOnBool::CreateLambda([this](bool b) { EditSettings([b](FFourfoldSettings& S) { S.bLeftHanded = b; }); }));
	B.Toggle(T(TEXT("Strong button labels")), TAttribute<bool>::CreateLambda([this]() { return Edit.bStrongLabels; }),
	         FFFOnBool::CreateLambda([this](bool b) { EditSettings([b](FFourfoldSettings& S) { S.bStrongLabels = b; }); }));
	B.Slider(T(TEXT("Camera sensitivity")), TAttribute<float>::CreateLambda([this]() { return Edit.CameraSensitivity; }), 0.3f, 2.5f, 0.05f,
	         TAttribute<FText>::CreateLambda([this]() { return Times(Edit.CameraSensitivity); }),
	         FFFOnFloat::CreateLambda([this](float V) { EditSettings([V](FFourfoldSettings& S) { S.CameraSensitivity = V; }); }));
	B.Toggle(T(TEXT("Invert camera Y")), TAttribute<bool>::CreateLambda([this]() { return Edit.bInvertY; }),
	         FFFOnBool::CreateLambda([this](bool b) { EditSettings([b](FFourfoldSettings& S) { S.bInvertY = b; }); }));

	B.Section(T(TEXT("Comfort and feedback")));
	B.Slider(T(TEXT("Screen shake")), TAttribute<float>::CreateLambda([this]() { return Edit.ScreenShake; }), 0.0f, 1.0f, 0.05f,
	         TAttribute<FText>::CreateLambda([this]() { return Pct(Edit.ScreenShake); }),
	         FFFOnFloat::CreateLambda([this](float V) { EditSettings([V](FFourfoldSettings& S) { S.ScreenShake = V; }); }));
	B.Slider(T(TEXT("Flashes")), TAttribute<float>::CreateLambda([this]() { return Edit.Flashes; }), 0.0f, 1.0f, 0.05f,
	         TAttribute<FText>::CreateLambda([this]() { return Pct(Edit.Flashes); }),
	         FFFOnFloat::CreateLambda([this](float V) { EditSettings([V](FFourfoldSettings& S) { S.Flashes = V; }); }));
	B.Toggle(T(TEXT("Haptics")), TAttribute<bool>::CreateLambda([this]() { return Edit.bHaptics; }),
	         FFFOnBool::CreateLambda([this](bool b) { EditSettings([b](FFourfoldSettings& S) { S.bHaptics = b; }); }));
	B.Toggle(T(TEXT("Reduced motion")), TAttribute<bool>::CreateLambda([this]() { return Edit.bReducedMotion; }),
	         FFFOnBool::CreateLambda([this](bool b) { EditSettings([b](FFourfoldSettings& S) { S.bReducedMotion = b; }); }));
	B.Toggle(T(TEXT("Slow-motion assist")), TAttribute<bool>::CreateLambda([this]() { return Edit.bSlowmoAssist; }),
	         FFFOnBool::CreateLambda([this](bool b) { EditSettings([b](FFourfoldSettings& S) { S.bSlowmoAssist = b; }); }));

	B.Section(T(TEXT("Audio")));
	struct FVol
	{
		const TCHAR* Label;
		float FFourfoldSettings::*Field;
	};
	const FVol Vols[4] = {{TEXT("Master volume"), &FFourfoldSettings::MasterVolume},
	                      {TEXT("Effects"), &FFourfoldSettings::SfxVolume},
	                      {TEXT("Ambience"), &FFourfoldSettings::AmbienceVolume},
	                      {TEXT("Interface"), &FFourfoldSettings::UiVolume}};
	for (const FVol& Vol : Vols)
	{
		float FFourfoldSettings::*Field = Vol.Field;
		B.Slider(T(Vol.Label), TAttribute<float>::CreateLambda([this, Field]() { return Edit.*Field; }), 0.0f, 1.0f, 0.05f,
		         TAttribute<FText>::CreateLambda([this, Field]() { return Pct(Edit.*Field); }),
		         FFFOnFloat::CreateLambda([this, Field](float V) { EditSettings([V, Field](FFourfoldSettings& S) { S.*Field = V; }); }));
	}

	B.Section(T(TEXT("Graphics")));
	B.Choice(T(TEXT("Quality")), {TEXT("Auto"), TEXT("Low"), TEXT("Medium"), TEXT("High")}, TAttribute<int32>::CreateLambda([this]() { return Edit.Quality + 1; }),
	         FFFOnInt::CreateLambda([this](int32 I) { EditSettings([I](FFourfoldSettings& S) { S.Quality = I - 1; }); }));
	B.Choice(T(TEXT("Frame rate")), {TEXT("30"), TEXT("60"), TEXT("120")}, TAttribute<int32>::CreateLambda([this]() {
		return Edit.FrameRateCap >= 120 ? 2 : (Edit.FrameRateCap >= 60 ? 1 : 0);
	}), FFFOnInt::CreateLambda([this](int32 I) { EditSettings([I](FFourfoldSettings& S) { S.FrameRateCap = I == 2 ? 120 : (I == 1 ? 60 : 30); }); }));

	B.Section(T(TEXT("Developer")));
	B.Toggle(T(TEXT("Show debug overlay")), TAttribute<bool>::CreateLambda([this]() { return Edit.bShowDebug; }),
	         FFFOnBool::CreateLambda([this](bool b) { EditSettings([b](FFourfoldSettings& S) { S.bShowDebug = b; }); }));

	B.Section(T(TEXT("Data")));
	B.Button(T(TEXT("Reset settings to defaults")), FSimpleDelegate::CreateLambda([this]() {
		EditSettings([](FFourfoldSettings& S) { S = FFourfoldSettings(); });
		if (Host.Toast)
		{
			Host.Toast(TEXT("Settings restored"));
		}
	}));
	B.Button(T(TEXT("Reset progress...")), FSimpleDelegate::CreateLambda([this]() { Push(EFFMenuPage::Confirm); }), false, FText::GetEmpty(), FFUi::Danger);
}

void SFourfoldMenus::BuildConfirm(FFFPageBuilder& B)
{
	B.Title(T(TEXT("Reset all progress?")), 4.0f);
	B.Label(T(TEXT("This erases unlocked elements and practice records. It cannot be undone.")), 2.7f);
	B.Spacer(1.5f);
	TSharedRef<SHorizontalBox> Row = B.Row();
	B.RowButton(Row, T(TEXT("Cancel")), FSimpleDelegate::CreateLambda([this]() { Pop(); }));
	B.RowButton(Row, T(TEXT("Erase progress")), FSimpleDelegate::CreateLambda([this]() {
		if (Host.ResetProgress)
		{
			Host.ResetProgress();
		}
		Pop();
	}), true, FFUi::Danger);
}

void SFourfoldMenus::BuildPause(FFFPageBuilder& B)
{
	B.Button(T(TEXT("Resume")), FSimpleDelegate::CreateLambda([this]() {
		if (Host.Resume)
		{
			Host.Resume();
		}
	}), true);
	B.Button(T(TEXT("Restart scenario")), FSimpleDelegate::CreateLambda([this]() {
		if (Host.RestartScenario)
		{
			Host.RestartScenario();
		}
	}));
	B.Button(T(TEXT("Practice")), FSimpleDelegate::CreateLambda([this]() { Push(EFFMenuPage::Practice); }));
	B.Button(T(TEXT("Settings")), FSimpleDelegate::CreateLambda([this]() { Push(EFFMenuPage::Settings); }));
	B.Button(T(TEXT("Lab tools")), FSimpleDelegate::CreateLambda([this]() {
		if (Host.OpenLabPanel)
		{
			Host.OpenLabPanel();
		}
	}), false, T(TEXT("Spawn, moves, combos, matrix, tuning")));
	B.Button(T(TEXT("Quit to title")), FSimpleDelegate::CreateLambda([this]() {
		if (Host.QuitToTitle)
		{
			Host.QuitToTitle();
		}
	}));
}

void SFourfoldMenus::BuildWatch(FFFPageBuilder& B)
{
	TSharedRef<SHorizontalBox> Row = B.Row();
	Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(FFUi::Mm(1.0f), 0.0f))
	[
		SNew(STextBlock).Text(T(TEXT("Watching: AI vs AI"))).Font(FFUi::Font(FFUi::Mm(2.8f), true)).ColorAndOpacity(FSlateColor(FFUi::Ink))
	];
	B.RowButton(Row, T(TEXT("Back")), FSimpleDelegate::CreateLambda([this]() {
		if (Host.QuitToTitle)
		{
			Host.QuitToTitle();
		}
	}));
}
