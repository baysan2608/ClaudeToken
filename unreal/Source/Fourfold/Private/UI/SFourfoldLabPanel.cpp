// Fourfold - Lab dev panel (see SFourfoldLabPanel.h).
#include "UI/SFourfoldLabPanel.h"

#include "UI/FourfoldPageBuilder.h"
#include "UI/FourfoldUi.h"

#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#include <algorithm>
#include <set>

using FFUi::T;

namespace
{
	const TCHAR* const kPages[6] = {TEXT("Dev"), TEXT("Spawn"), TEXT("Moves"), TEXT("Combos"), TEXT("Matrix"), TEXT("Tuning")};
	const char* const kAiPresets[3] = {"novice", "adept", "master"};
	const char* const kAiKits[6] = {"mixed", "earth", "water", "fire", "air", "all"};
	const char* const kAiDrills[3] = {"", "passive", "matrix"};
	const float kTimeScales[4] = {0.1f, 0.25f, 0.5f, 1.0f};

	int32 IndexOf(const char* const* List, int32 N, const std::string& V)
	{
		for (int32 i = 0; i < N; ++i)
		{
			if (V == List[i])
			{
				return i;
			}
		}
		return 0;
	}

	/** Input text per slot and device (MoveListData.INPUTS). */
	const TCHAR* SlotInput(ff::Slot Slot, const FString& Device)
	{
		static const TCHAR* const Touch[10] = {TEXT("ATTACK tap (hold = charge)"), TEXT("ATTACK flick up"), TEXT("ATTACK flick down"),
		                                       TEXT("ATTACK flick left / right"), TEXT("GUARD hold (just before contact = perfect)"),
		                                       TEXT("GUARD flick up"), TEXT("GUARD flick down"), TEXT("TECH hold, drag, lift (ATTACK tap = shape)"),
		                                       TEXT("EVADE tap"), TEXT("EVADE hold")};
		static const TCHAR* const Keys[10] = {TEXT("J (hold = charge)"), TEXT("U"), TEXT("N"), TEXT("H"), TEXT("K (tap before contact = perfect)"),
		                                      TEXT("K + J"), TEXT("K + N"), TEXT("L hold (J = shape)"), TEXT("Space"), TEXT("Space hold")};
		static const TCHAR* const Pad[10] = {TEXT("X (hold = charge)"), TEXT("Y"), TEXT("LT"), TEXT("B"), TEXT("RB (tap before contact = perfect)"),
		                                     TEXT("RB + X"), TEXT("RB + LT"), TEXT("RT hold (X = shape)"), TEXT("A"), TEXT("A hold")};
		const int32 I = int32(Slot);
		if (I < 0 || I >= 10)
		{
			return TEXT("");
		}
		if (Device == TEXT("touch"))
		{
			return Touch[I];
		}
		return Device == TEXT("gamepad") ? Pad[I] : Keys[I];
	}

	/** FString::Printf needs a literal format (UE 5 static_assert): pick one per precision. */
	FString FormatNum(float V, int32 Decimals, const TCHAR* Suffix)
	{
		switch (Decimals)
		{
		case 0: return FString::Printf(TEXT("%.0f%s"), V, Suffix);
		case 1: return FString::Printf(TEXT("%.1f%s"), V, Suffix);
		default: return FString::Printf(TEXT("%.2f%s"), V, Suffix);
		}
	}

	FString Num(float V)
	{
		return FMath::Abs(V - FMath::RoundToFloat(V)) < 0.05f ? FString::Printf(TEXT("%d"), FMath::RoundToInt(V)) : FString::Printf(TEXT("%.1f"), V);
	}

	/** A slider range around a value (x0 .. x3, or -1 .. 1 for zero) - lab_page_tuning.gd range_for / step_for. */
	void RangeFor(double V, float& Lo, float& Hi)
	{
		if (FMath::Abs(V) < 1e-6)
		{
			Lo = -1.0f;
			Hi = 1.0f;
			return;
		}
		const float H = float(FMath::Abs(V) * 3.0);
		Lo = V > 0.0 ? 0.0f : -H;
		Hi = V > 0.0 ? H : 0.0f;
	}
	float StepFor(float Hi)
	{
		return Hi <= 2.0f ? 0.01f : (Hi <= 20.0f ? 0.05f : (Hi <= 200.0f ? 0.25f : 1.0f));
	}

	/** One combo step's timing bar: 0 waiting / locked, 1 current (window used), 2 done. */
	class SFFStepBar : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SFFStepBar) {}
			SLATE_ATTRIBUTE(int32, State)
			SLATE_ATTRIBUTE(float, Used)
			SLATE_ARGUMENT(FLinearColor, Accent)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			State = InArgs._State;
			Used = InArgs._Used;
			Accent = InArgs._Accent;
			ForceVolatile(true);
			SetVisibility(EVisibility::HitTestInvisible);
		}
		virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(FFUi::Mm(20.0f), FFUi::Mm(2.2f)); }
		virtual int32 OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle&,
		                      bool) const override
		{
			const FFUi::FPainter P(G, Out, Layer);
			const FVector2D Size = FVector2D(G.GetLocalSize());
			P.Rect(FVector2D(0, 0), Size, FLinearColor(1, 1, 1, 0.08f), float(Size.Y * 0.3));
			const int32 St = State.Get(0);
			if (St == 1)
			{
				const float U = FMath::Clamp(Used.Get(0.0f), 0.0f, 1.0f);
				const FLinearColor Warn = FFUi::SRGB(1.0f, 0.4f, 0.3f, 0.9f);
				const FLinearColor Base = FFUi::WithAlpha(Accent, U < 0.8f ? 0.55f : 0.9f);
				P.Rect(FVector2D(0, 0), FVector2D(Size.X * U, Size.Y), Base + (Warn - Base) * FMath::Clamp((U - 0.7f) / 0.3f, 0.0f, 1.0f), float(Size.Y * 0.3));
			}
			else if (St == 2)
			{
				P.Rect(FVector2D(0, 0), Size, FFUi::SRGB(0.4f, 0.85f, 0.5f, 0.55f), float(Size.Y * 0.3));
			}
			P.RoundRect(FVector2D(0, 0), Size, FLinearColor::Transparent, FLinearColor(1, 1, 1, 0.22f), 1.0f, float(Size.Y * 0.3));
			return Layer + 1;
		}

	private:
		TAttribute<int32> State;
		TAttribute<float> Used;
		FLinearColor Accent = FLinearColor::White;
	};
}

void SFourfoldLabPanel::Construct(const FArguments& InArgs, const FFourfoldLabHost& InHost)
{
	Host = InHost;
	SetCanTick(true);
	ChildSlot
	[
		SNew(SOverlay)
		.Visibility(EVisibility::SelfHitTestInvisible)   // only the panel card takes hits
		+ SOverlay::Slot()
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Fill)
		.Padding(TAttribute<FMargin>::CreateLambda([]() {
			const FMargin& Safe = FFUi::SafeLocal;
			const float M = FFUi::Mm(2.0f);
			return FMargin(Safe.Left + M, Safe.Top + M, Safe.Right + M, Safe.Bottom + M);
		}))
		[
			SNew(SBox)
			.WidthOverride(TAttribute<FOptionalSize>::CreateLambda([this]() {
				return FOptionalSize(FMath::Min(FFUi::Mm(150.0f), float(ViewSize.X) * 0.62f));
			}))
			[
				SAssignNew(Card, SBorder)
				.BorderImage(FFUiBrushes::Card())
				.Padding(TAttribute<FMargin>::CreateLambda([]() { return FMargin(FFUi::Mm(2.6f), FFUi::Mm(2.0f)); }))
			]
		]
	];
	SetVisibility(EVisibility::Collapsed);
}

void SFourfoldLabPanel::Cue(const TCHAR* Name) const
{
	if (Host.UiCue)
	{
		Host.UiCue(FName(Name));
	}
}

void SFourfoldLabPanel::Say(const FString& Text) const
{
	if (Host.Toast)
	{
		Host.Toast(Text);
	}
}

void SFourfoldLabPanel::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	ViewSize = FVector2D(AllottedGeometry.GetLocalSize());
	if (bRebuildQueued)
	{
		bRebuildQueued = false;
		BuildPage();
	}
}

FReply SFourfoldLabPanel::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	// Only the panel card is hit-testable (the root and its overlay are SelfHitTestInvisible): a click that bubbles up
	// to here landed on the panel and must not fall through to the game viewport.
	return FReply::Handled();
}

FReply SFourfoldLabPanel::OnTouchStarted(const FGeometry& MyGeometry, const FPointerEvent& InTouchEvent)
{
	return FReply::Handled();
}

FReply SFourfoldLabPanel::OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	return FReply::Handled();
}

void SFourfoldLabPanel::Open(int32 InPage)
{
	if (InPage >= 0 && InPage < 6)
	{
		PageIndex = InPage;
	}
	if (Host.PlayerElementSub)
	{
		Host.PlayerElementSub(MovesElement, MovesSub);
		TuneElement = MovesElement;
		TuneSub = MovesSub;
	}
	bOpen = true;
	// The root never takes hits itself: the game stays playable / visible beside the panel.
	SetVisibility(EVisibility::SelfHitTestInvisible);
	Cue(TEXT("ui_open"));
	BuildPage();
	if (Host.OnOpenChanged)
	{
		Host.OnOpenChanged(true);
	}
}

void SFourfoldLabPanel::Close()
{
	if (!bOpen)
	{
		return;
	}
	bOpen = false;
	SetVisibility(EVisibility::Collapsed);
	Cue(TEXT("ui_close"));
	if (Host.OnOpenChanged)
	{
		Host.OnOpenChanged(false);
	}
}

void SFourfoldLabPanel::Toggle()
{
	if (bOpen)
	{
		Close();
	}
	else
	{
		Open();
	}
}

void SFourfoldLabPanel::Rebuild()
{
	if (bOpen)
	{
		BuildPage();
	}
}

void SFourfoldLabPanel::OnScenarioLoaded()
{
	if (bOpen)
	{
		bRebuildQueued = true;
	}
}

void SFourfoldLabPanel::SetFocusIndex(int32 Index)
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

bool SFourfoldLabPanel::HandleNav(EFFNav Nav)
{
	if (!bOpen)
	{
		return false;
	}
	const bool bHad = Focus.IsValidIndex(FocusIndex);
	switch (Nav)
	{
	case EFFNav::Up:
	case EFFNav::Down:
		SetFocusIndex(!bHad ? 0 : FocusIndex + (Nav == EFFNav::Down ? 1 : -1));
		return true;
	case EFFNav::Left:
	case EFFNav::Right:
		if (bHad)
		{
			Focus[FocusIndex]->NavAdjust(Nav == EFFNav::Right ? 1 : -1);
		}
		return true;
	case EFFNav::Accept:
		if (!bHad)
		{
			SetFocusIndex(0);
		}
		else if (Focus[FocusIndex]->IsPressable())
		{
			const TSharedRef<SFFPressable> Target = Focus[FocusIndex];
			Target->NavActivate();
		}
		return true;
	case EFFNav::Back:
		Close();
		return true;
	}
	return false;
}

FString SFourfoldLabPanel::DeviceName() const
{
	return Host.Device ? Host.Device() : FString(TEXT("keyboard"));
}

FString SFourfoldLabPanel::InputText(ff::Slot Slot) const
{
	return SlotInput(Slot, DeviceName());
}

void SFourfoldLabPanel::SetLab(TFunctionRef<void(ff::LabState&)> Fn, bool bApplyAi)
{
	ff::Session* Ses = S();
	if (!Ses)
	{
		return;
	}
	ff::LabState L = Ses->Lab();
	Fn(L);
	Ses->SetLab(L);
	if (bApplyAi)
	{
		Ses->SetRivalAi(L.ai_preset, L.ai_kit, L.ai_sub);
	}
}

void SFourfoldLabPanel::BuildPage()
{
	Focus.Reset();
	FocusIndex = -1;
	if (!Card.IsValid())
	{
		return;
	}
	TSharedRef<SVerticalBox> Content = SNew(SVerticalBox);
	FFFPageBuilder B(Content, Focus, [this](FName N) {
		if (Host.UiCue)
		{
			Host.UiCue(N);
		}
	});

	// Header: title + close; then the page tabs.
	TSharedRef<SVerticalBox> Outer = SNew(SVerticalBox);
	TSharedRef<SHorizontalBox> Head = SNew(SHorizontalBox);
	Head->AddSlot().FillWidth(1.0f).VAlign(VAlign_Center)
	[
		SNew(STextBlock).Text(T(TEXT("Lab"))).Font(FFUi::Font(FFUi::Mm(3.8f), true)).ColorAndOpacity(FSlateColor(FFUi::Ink))
	];
	TSharedRef<SFFButton> CloseBtn = SNew(SFFButton).Label(T(TEXT("Close"))).bCentered(true).HeightMm(9.0f).OnClicked(FSimpleDelegate::CreateLambda([this]() { Close(); }));
	Head->AddSlot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(FFUi::Mm(20.0f))[CloseBtn]];
	Outer->AddSlot().AutoHeight().Padding(FMargin(0.0f, 0.0f, 0.0f, FFUi::Mm(1.0f)))[Head];

	TSharedRef<SVerticalBox> TabsBox = SNew(SVerticalBox);
	FFFPageBuilder TB(TabsBox, Focus, [this](FName N) {
		if (Host.UiCue)
		{
			Host.UiCue(N);
		}
	});
	TSharedRef<SWrapBox> Tabs = TB.Flow();
	for (int32 i = 0; i < 6; ++i)
	{
		TB.FlowButton(Tabs, T(kPages[i]), FSimpleDelegate::CreateLambda([this, i]() {
			PageIndex = i;
			bRebuildQueued = true;   // rebuilt next tick (this button is part of the page being replaced)
		}), TAttribute<bool>::CreateLambda([this, i]() { return PageIndex == i; }), FFUi::Accent, 14.0f);
	}
	Outer->AddSlot().AutoHeight()[TabsBox];
	Outer->AddSlot().FillHeight(1.0f).Padding(FMargin(0.0f, FFUi::Mm(1.0f), 0.0f, 0.0f))
	[
		SAssignNew(Scroll, SScrollBox)
		.ScrollBarThickness(FVector2f(FFUi::Mm(1.2f), FFUi::Mm(1.2f)))
		+ SScrollBox::Slot()[Content]
	];

	if (!S())
	{
		B.Label(T(TEXT("No simulation running.")));
	}
	else
	{
		switch (PageIndex)
		{
		case 0: BuildDev(B); break;
		case 1: BuildSpawn(B); break;
		case 2: BuildMoves(B); break;
		case 3: BuildCombos(B); break;
		case 4: BuildMatrix(B); break;
		default: BuildTuning(B); break;
		}
	}
	Focus.Add(CloseBtn);
	Card->SetContent(Outer);
}

// ---------------------------------------------------------------------------------------------- Dev

void SFourfoldLabPanel::BuildDev(FFFPageBuilder& B)
{
	B.Section(T(TEXT("Time")));
	B.Slider(T(TEXT("Time scale")), TAttribute<float>::CreateLambda([this]() { return S() ? S()->Lab().time_scale : 1.0f; }), 0.1f, 1.0f, 0.05f,
	         TAttribute<FText>::CreateLambda([this]() { return FText::FromString(FString::Printf(TEXT("x%.2f"), S() ? S()->Lab().time_scale : 1.0f)); }),
	         FFFOnFloat::CreateLambda([this](float V) { SetLab([V](ff::LabState& L) { L.time_scale = V; }); }));
	TSharedRef<SHorizontalBox> Presets = B.Row();
	for (const float Ts : kTimeScales)
	{
		B.RowButton(Presets, FText::FromString(FString::Printf(TEXT("x%.2f"), Ts)), FSimpleDelegate::CreateLambda([this, Ts]() {
			SetLab([Ts](ff::LabState& L) { L.time_scale = Ts; });
		}), false, FFUi::Accent, TAttribute<bool>::CreateLambda([this, Ts]() { return S() && FMath::IsNearlyEqual(S()->Lab().time_scale, Ts, 0.01f); }));
	}
	B.Toggle(T(TEXT("Freeze simulation")), TAttribute<bool>::CreateLambda([this]() { return S() && S()->Lab().frozen; }),
	         FFFOnBool::CreateLambda([this](bool bOn) { SetLab([bOn](ff::LabState& L) { L.frozen = bOn; }); }));
	TSharedRef<SHorizontalBox> Steps = B.Row();
	const int32 StepCounts[3] = {1, 6, 30};
	for (const int32 N : StepCounts)
	{
		B.RowButton(Steps, FText::FromString(N == 1 ? FString(TEXT("Step 1 tick")) : FString::Printf(TEXT("Step %d"), N)), FSimpleDelegate::CreateLambda([this, N]() {
			SetLab([](ff::LabState& L) { L.frozen = true; });
			if (ff::Session* Ses = S())
			{
				Ses->LabRequestStep(N);
			}
		}));
	}
	B.Label(TAttribute<FText>::CreateLambda([this]() {
		ff::Session* Ses = S();
		if (!Ses)
		{
			return FText::GetEmpty();
		}
		return FText::FromString(FString::Printf(TEXT("%s  |  scenario: %s"), Ses->Lab().frozen ? TEXT("FROZEN") : TEXT("running"), *FFUi::F(Ses->ScenarioId())));
	}), 2.2f);

	B.Section(T(TEXT("Cheats")));
	B.Toggle(T(TEXT("Infinite Focus, heat, water and metal")), TAttribute<bool>::CreateLambda([this]() { return S() && S()->Lab().infinite; }),
	         FFFOnBool::CreateLambda([this](bool bOn) { SetLab([bOn](ff::LabState& L) { L.infinite = bOn; }); }));
	B.Toggle(T(TEXT("God mode (health and balance never drop)")), TAttribute<bool>::CreateLambda([this]() { return S() && S()->Lab().god; }),
	         FFFOnBool::CreateLambda([this](bool bOn) { SetLab([bOn](ff::LabState& L) { L.god = bOn; }); }));
	B.Toggle(T(TEXT("Debug overlay (bodies, zones, power)")), TAttribute<bool>::CreateLambda([this]() { return S() && S()->Lab().overlay; }),
	         FFFOnBool::CreateLambda([this](bool bOn) { SetLab([bOn](ff::LabState& L) { L.overlay = bOn; }); }));

	B.Section(T(TEXT("Rival AI")));
	B.Toggle(T(TEXT("AI acts (off: the rival stands still and only throws what you spawn)")), TAttribute<bool>::CreateLambda([this]() { return S() && S()->Lab().ai_enabled; }),
	         FFFOnBool::CreateLambda([this](bool bOn) { SetLab([bOn](ff::LabState& L) { L.ai_enabled = bOn; }, true); }));
	B.Choice(T(TEXT("Difficulty")), {TEXT("Easy"), TEXT("Normal"), TEXT("Hard")},
	         TAttribute<int32>::CreateLambda([this]() { return S() ? IndexOf(kAiPresets, 3, S()->Lab().ai_preset) : 1; }),
	         FFFOnInt::CreateLambda([this](int32 I) { SetLab([I](ff::LabState& L) { L.ai_preset = kAiPresets[FMath::Clamp(I, 0, 2)]; }, true); }));
	B.Choice(T(TEXT("Kit")), {TEXT("Scenario kit"), TEXT("Earth"), TEXT("Water"), TEXT("Fire"), TEXT("Air"), TEXT("All four")},
	         TAttribute<int32>::CreateLambda([this]() { return S() ? IndexOf(kAiKits, 6, S()->Lab().ai_kit) : 0; }),
	         FFFOnInt::CreateLambda([this](int32 I) {
		         SetLab([I](ff::LabState& L) {
			         L.ai_kit = kAiKits[FMath::Clamp(I, 0, 5)];
			         L.ai_sub = -1;
		         }, true);
		         bRebuildQueued = true;   // sub-element labels follow the kit
	         }));
	// Sub-element: named after the kit's element when the kit is a single element.
	const int32 KitIndex = S() ? IndexOf(kAiKits, 6, S()->Lab().ai_kit) : 0;
	const bool bSingle = KitIndex >= 1 && KitIndex <= 4;
	TArray<FString> SubLabels = {TEXT("Any")};
	for (int32 i = 0; i < 4; ++i)
	{
		SubLabels.Add(bSingle ? FFUi::F(std::string(ff::SubName(KitIndex - 1, i))) : FString::Printf(TEXT("Sub %d"), i + 1));
	}
	B.Choice(T(TEXT("Sub-element (single-element kits)")), SubLabels, TAttribute<int32>::CreateLambda([this]() { return S() ? S()->Lab().ai_sub + 1 : 0; }),
	         FFFOnInt::CreateLambda([this, bSingle](int32 I) {
		         if (bSingle)
		         {
			         SetLab([I](ff::LabState& L) { L.ai_sub = FMath::Clamp(I, 0, 4) - 1; }, true);
		         }
	         }));
	B.Choice(T(TEXT("Behaviour")), {TEXT("Free sparring"), TEXT("Passive"), TEXT("Matrix drill")},
	         TAttribute<int32>::CreateLambda([this]() { return S() ? IndexOf(kAiDrills, 3, S()->Lab().ai_drill) : 0; }),
	         FFFOnInt::CreateLambda([this](int32 I) { SetLab([I](ff::LabState& L) { L.ai_drill = kAiDrills[FMath::Clamp(I, 0, 2)]; }, true); }));

	B.Section(T(TEXT("World")));
	TSharedRef<SHorizontalBox> W1 = B.Row();
	B.RowButton(W1, T(TEXT("Reset scenario")), FSimpleDelegate::CreateLambda([this]() {
		if (Host.RestartScenario)
		{
			Host.RestartScenario();
		}
	}));
	B.RowButton(W1, T(TEXT("Clear bodies")), FSimpleDelegate::CreateLambda([this]() {
		if (ff::Session* Ses = S())
		{
			Ses->LabClear();
		}
	}));
	B.RowButton(W1, T(TEXT("Heal and refill")), FSimpleDelegate::CreateLambda([this]() {
		if (ff::Session* Ses = S())
		{
			Ses->LabHeal();
		}
	}));
	B.Label(T(TEXT("Load a scenario")), 2.3f);
	TSharedRef<SHorizontalBox> W2 = B.Row();
	const TCHAR* const Scen[3][2] = {{TEXT("lab"), TEXT("Lab")}, {TEXT("spar"), TEXT("Free Spar")}, {TEXT("molten_exchange"), TEXT("Molten Exchange")}};
	for (const auto& Sc : Scen)
	{
		const FString Id = Sc[0];
		B.RowButton(W2, T(Sc[1]), FSimpleDelegate::CreateLambda([this, Id]() {
			if (Host.LoadScenario)
			{
				Host.LoadScenario(Id);
			}
		}));
	}
}

// ---------------------------------------------------------------------------------------------- Spawn

void SFourfoldLabPanel::BuildSpawn(FFFPageBuilder& B)
{
	ff::Session* Ses = S();
	const std::vector<ff::LabSpawnEntry> Entries = Ses->LabSpawnEntries();
	if (Entries.empty())
	{
		B.Label(T(TEXT("The spawn catalogue is empty.")));
		return;
	}
	const ff::LabSpawnEntry* Sel = nullptr;
	for (const ff::LabSpawnEntry& E : Entries)
	{
		if (FFUi::F(E.id) == SpawnSelected)
		{
			Sel = &E;
		}
	}
	if (!Sel)
	{
		Sel = &Entries.front();
		SpawnSelected = FFUi::F(Sel->id);
		SpawnParams = ff::LabSpawnParams();
	}
	if (Sel->inert_only)
	{
		bSpawnLaunch = false;
	}
	if (Sel->launch_only)
	{
		bSpawnLaunch = true;
	}
	const bool bInertOnly = Sel->inert_only, bLaunchOnly = Sel->launch_only;
	B.Choice(T(TEXT("Delivery")), {TEXT("Rival throws it at me"), TEXT("Inert at the aim point")},
	         TAttribute<int32>::CreateLambda([this]() { return bSpawnLaunch ? 0 : 1; }), FFFOnInt::CreateLambda([this, bInertOnly, bLaunchOnly](int32 I) {
		         const bool bWant = I == 0;
		         if ((bWant && bInertOnly) || (!bWant && bLaunchOnly))
		         {
			         Say(TEXT("This entry has only one delivery"));
			         return;
		         }
		         bSpawnLaunch = bWant;
	         }));

	// Entries by group, in catalogue order.
	TArray<FString> Groups;
	for (const ff::LabSpawnEntry& E : Entries)
	{
		Groups.AddUnique(FFUi::F(E.group));
	}
	for (const FString& G : Groups)
	{
		B.Section(FText::FromString(G));
		TSharedRef<SWrapBox> Flow = B.Flow();
		for (const ff::LabSpawnEntry& E : Entries)
		{
			if (FFUi::F(E.group) != G)
			{
				continue;
			}
			const FString Id = FFUi::F(E.id);
			B.FlowButton(Flow, T(E.label), FSimpleDelegate::CreateLambda([this, Id]() {
				SpawnSelected = Id;
				SpawnParams = ff::LabSpawnParams();
				bRebuildQueued = true;
			}), TAttribute<bool>::CreateLambda([this, Id]() { return SpawnSelected == Id; }));
		}
	}

	B.Section(T(TEXT("Parameters")));
	const char* Kind = Sel->build == "body" ? "a body" : (Sel->build == "verb" ? "the kit's own move" : (Sel->build == "zone" ? "a field" : "the rival performs the move"));
	B.Label(FText::FromString(FString::Printf(TEXT("%s: %s (%s)"), *FFUi::F(Sel->label), *FFUi::F(Sel->summary), UTF8_TO_TCHAR(Kind))), 2.3f);
	struct FParamRow
	{
		const TCHAR* Label;
		int32 Decimals;
		const TCHAR* Suffix;
		const ff::LabParamSpec* Spec;
		float ff::LabSpawnParams::*Field;
	};
	const FParamRow Rows[3] = {{TEXT("Mass"), 0, TEXT(" kg"), &Sel->mass, &ff::LabSpawnParams::mass},
	                           {TEXT("Speed"), 1, TEXT(" m/s"), &Sel->speed, &ff::LabSpawnParams::speed},
	                           {TEXT("Temperature"), 0, TEXT(" C"), &Sel->temp, &ff::LabSpawnParams::temp}};
	for (const FParamRow& R : Rows)
	{
		if (!R.Spec->present)
		{
			continue;
		}
		const float Def = R.Spec->def, Mn = R.Spec->min, Mx = FMath::Max(R.Spec->max, R.Spec->min + 0.01f);
		float ff::LabSpawnParams::*Field = R.Field;
		const int32 Decimals = R.Decimals;
		const TCHAR* Suffix = R.Suffix;
		const float Step = Field == &ff::LabSpawnParams::mass ? (Mx > 20.0f ? 1.0f : 0.5f) : (Field == &ff::LabSpawnParams::speed ? 0.5f : 10.0f);
		B.Slider(T(R.Label), TAttribute<float>::CreateLambda([this, Field, Def]() { return SpawnParams.*Field >= 0.0f ? SpawnParams.*Field : Def; }), Mn, Mx, Step,
		         TAttribute<FText>::CreateLambda([this, Field, Def, Decimals, Suffix]() {
			         const float V = SpawnParams.*Field >= 0.0f ? SpawnParams.*Field : Def;
			         return FText::FromString(FormatNum(V, Decimals, Suffix));
		         }),
		         FFFOnFloat::CreateLambda([this, Field](float V) { SpawnParams.*Field = V; }));
	}
	if (Sel->tier.present)
	{
		const int32 Def = FMath::RoundToInt(Sel->tier.def);
		B.Slider(T(TEXT("Tier")), TAttribute<float>::CreateLambda([this, Def]() { return float(SpawnParams.tier >= 0 ? SpawnParams.tier : Def); }), Sel->tier.min,
		         FMath::Max(Sel->tier.max, Sel->tier.min + 1.0f), 1.0f,
		         TAttribute<FText>::CreateLambda([this, Def]() { return FText::FromString(FString::Printf(TEXT("T%d"), SpawnParams.tier >= 0 ? SpawnParams.tier : Def)); }),
		         FFFOnFloat::CreateLambda([this](float V) { SpawnParams.tier = FMath::RoundToInt(V); }));
	}
	auto DoSpawn = [this](bool bClose) {
		ff::Session* Sx = S();
		if (!Sx)
		{
			return;
		}
		std::string Msg;
		const bool bOk = Sx->LabSpawn(FFUi::U8(SpawnSelected), SpawnParams, bSpawnLaunch, &Msg);
		Say(bOk ? FString::Printf(TEXT("spawned %s"), *SpawnSelected) : (Msg.empty() ? FString(TEXT("spawn failed")) : FFUi::F(Msg)));
		if (bOk && bClose)
		{
			Close();
		}
	};
	TSharedRef<SHorizontalBox> Go = B.Row();
	B.RowButton(Go, T(TEXT("Spawn")), FSimpleDelegate::CreateLambda([DoSpawn]() { DoSpawn(false); }), true);
	B.RowButton(Go, T(TEXT("Spawn and close")), FSimpleDelegate::CreateLambda([DoSpawn]() { DoSpawn(true); }));
}

// ---------------------------------------------------------------------------------------------- Moves

void SFourfoldLabPanel::BuildMoves(FFFPageBuilder& B)
{
	ff::Session* Ses = S();
	B.Choice(FText::GetEmpty(), {TEXT("Earth"), TEXT("Water"), TEXT("Fire"), TEXT("Air")}, TAttribute<int32>::CreateLambda([this]() { return MovesElement; }),
	         FFFOnInt::CreateLambda([this](int32 I) {
		         MovesElement = FMath::Clamp(I, 0, 3);
		         MovesSub = 0;
		         bRebuildQueued = true;
	         }),
	         TAttribute<FLinearColor>::CreateLambda([this]() { return FFUi::ElementColor(MovesElement); }));
	TArray<FString> Subs;
	for (int32 i = 0; i < 4; ++i)
	{
		Subs.Add(FFUi::F(std::string(ff::SubName(MovesElement, i))));
	}
	B.Choice(FText::GetEmpty(), Subs, TAttribute<int32>::CreateLambda([this]() { return MovesSub; }), FFFOnInt::CreateLambda([this](int32 I) {
		MovesSub = FMath::Clamp(I, 0, 3);
		bRebuildQueued = true;
	}), TAttribute<FLinearColor>::CreateLambda([this]() { return FFUi::ElementColor(MovesElement); }));
	B.Label(FText::FromString(FString::Printf(TEXT("Inputs shown for: %s  (S startup / A active / R recovery in 60 Hz frames)"), *DeviceName())), 2.2f);

	const FLinearColor Ec = FFUi::ElementColor(MovesElement);
	for (const ff::MoveInfo& M : Ses->ListMoves(MovesElement, MovesSub))
	{
		if (M.id.empty())
		{
			continue;
		}
		const int32 SlotI = int32(M.slot);
		const int32 MaxTier = M.max_tier;
		const int32 El = MovesElement, Sb = MovesSub;
		const ff::Slot Slot = M.slot;
		B.Section(FText::FromString(FString::Printf(TEXT("%s  [%s]"), *FFUi::F(M.name), *FFUi::F(std::string(ff::SlotName(M.slot))))));
		TSharedRef<SHorizontalBox> Row = B.Row();
		if (MaxTier > 0)
		{
			B.RowButton(Row, TAttribute<FText>::CreateLambda([this, SlotI]() {
				const int32* Tc = TierChoice.Find(SlotI);
				return FText::FromString(FString::Printf(TEXT("Tier T%d"), Tc ? *Tc : 0));
			}), FSimpleDelegate::CreateLambda([this, SlotI, MaxTier]() {
				int32& Tc = TierChoice.FindOrAdd(SlotI);
				Tc = (Tc + 1) % (MaxTier + 1);
			}), false, Ec);
		}
		B.RowButton(Row, T(TEXT("Try")), FSimpleDelegate::CreateLambda([this, El, Sb, Slot, SlotI]() {
			if (ff::Session* Sx = S())
			{
				const int32* Tc = TierChoice.Find(SlotI);
				Sx->LabTry(El, Sb, Slot, Tc ? *Tc : 0);
				Close();
			}
		}), true, Ec);
		// Costs, frames, tier times, description.
		TArray<FString> Bits;
		if (M.cost_focus > 0.0f) Bits.Add(FString::Printf(TEXT("Focus %s"), *Num(M.cost_focus)));
		if (M.cost_heat > 0.0f) Bits.Add(FString::Printf(TEXT("heat %s HU"), *Num(M.cost_heat)));
		if (M.cost_water > 0.0f) Bits.Add(FString::Printf(TEXT("water %s kg"), *Num(M.cost_water)));
		if (M.cost_metal > 0.0f) Bits.Add(FString::Printf(TEXT("metal %s kg"), *Num(M.cost_metal)));
		const FString Cost = Bits.Num() > 0 ? FString::Join(Bits, TEXT(" / ")) : FString(TEXT("free"));
		const FString Frames = FString::Printf(TEXT("S%d A%d R%d"), FMath::RoundToInt(M.startup * 60.0f), FMath::RoundToInt(M.active * 60.0f),
		                                       FMath::RoundToInt(M.recovery * 60.0f));
		B.Label(FText::FromString(FString::Printf(TEXT("%s   |   %s   |   %s"), *InputText(M.slot), *Cost, *Frames)), 2.3f, false);
		if (!M.tier_times.empty())
		{
			TArray<FString> Tiers;
			for (size_t i = 0; i < M.tier_times.size(); ++i)
			{
				Tiers.Add(FString::Printf(TEXT("T%d at %.2fs"), int32(i) + 1, M.tier_times[i]));
			}
			B.Label(FText::FromString(FString::Join(Tiers, TEXT(",  "))), 2.2f);
		}
		if (!M.desc.empty())
		{
			B.Label(T(M.desc), 2.2f);
		}
	}
}

// ---------------------------------------------------------------------------------------------- Combos

void SFourfoldLabPanel::BuildCombos(FFFPageBuilder& B)
{
	ff::Session* Ses = S();
	const std::vector<ff::LabCombo> Combos = Ses->LabCombos();
	if (Combos.empty())
	{
		B.Label(T(TEXT("No combos defined.")));
		return;
	}
	TSharedRef<SWrapBox> Flow = B.Flow();
	const ff::LabCombo* Sel = nullptr;
	for (size_t i = 0; i < Combos.size(); ++i)
	{
		const FString Id = FFUi::F(Combos[i].id);
		if (Id == ComboSelected)
		{
			Sel = &Combos[i];
		}
		B.FlowButton(Flow, FText::FromString(FString::Printf(TEXT("%d  %s"), int32(i) + 1, *FFUi::F(Combos[i].name))), FSimpleDelegate::CreateLambda([this, Id]() {
			ComboSelected = Id;
			bRebuildQueued = true;
		}), TAttribute<bool>::CreateLambda([this, Id]() { return ComboSelected == Id; }));
	}
	if (!Sel)
	{
		Sel = &Combos.front();
		ComboSelected = FFUi::F(Sel->id);
	}
	B.Title(T(Sel->name), 3.3f);
	B.Label(T(Sel->description), 2.4f);
	const FString Device = DeviceName();
	const FString ComboId = ComboSelected;
	for (size_t i = 0; i < Sel->steps.size(); ++i)
	{
		const ff::LabComboStep& St = Sel->steps[i];
		const FString Input = St.kind == "shape" ? (Device == TEXT("touch") ? FString(TEXT("tap ATTACK")) : (Device == TEXT("keyboard") ? FString(TEXT("J")) : FString(TEXT("X"))))
		                                         : InputText(St.slot);
		const FString Hint = FFUi::F(St.hint);
		B.Label(FText::FromString(FString::Printf(TEXT("%d.  %s    [%s]%s"), int32(i) + 1, *FFUi::F(St.text), *Input, Hint.IsEmpty() ? TEXT("") : *(TEXT("  -  ") + Hint))),
		        2.4f, false);
		const int32 StepIndex = int32(i);
		const float Within = FMath::Max(St.within, 0.01f);
		B.Add(SNew(SFFStepBar)
		          .Accent(FFUi::ElementColor(St.element))
		          .State(TAttribute<int32>::CreateLambda([this, StepIndex, ComboId]() {
			          ff::Session* Sx = S();
			          if (!Sx)
			          {
				          return 0;
			          }
			          const ff::ComboTrackerView V = Sx->LabComboState();
			          if (FFUi::F(V.combo_id) != ComboId || (!V.active && !V.success))
			          {
				          return 0;
			          }
			          if (V.success || StepIndex < V.step)
			          {
				          return 2;
			          }
			          return StepIndex == V.step ? 1 : 0;
		          }))
		          .Used(TAttribute<float>::CreateLambda([this, Within]() {
			          ff::Session* Sx = S();
			          return Sx ? 1.0f - FMath::Clamp(Sx->LabComboState().time_left / Within, 0.0f, 1.0f) : 0.0f;
		          })),
		      0.3f);
	}
	if (!Sel->result_text.empty())
	{
		B.Label(FText::FromString(FString::Printf(TEXT("Result to see: %s"), *FFUi::F(Sel->result_text))), 2.4f, false, FSlateColor(FFUi::Accent));
	}
	B.Label(TAttribute<FText>::CreateLambda([this]() {
		ff::Session* Sx = S();
		return Sx ? FText::FromString(FFUi::F(Sx->LabComboState().message)) : FText::GetEmpty();
	}), 2.6f, false);
	B.Toggle(T(TEXT("Slow motion while training (x0.5)")), TAttribute<bool>::CreateLambda([this]() { return bComboSlow; }),
	         FFFOnBool::CreateLambda([this](bool bOn) { bComboSlow = bOn; }));
	TSharedRef<SHorizontalBox> Btns = B.Row();
	B.RowButton(Btns, T(TEXT("Start trainer")), FSimpleDelegate::CreateLambda([this]() {
		if (ff::Session* Sx = S())
		{
			if (bComboSlow)
			{
				SetLab([](ff::LabState& L) { L.time_scale = 0.5f; });
			}
			Sx->LabStartCombo(FFUi::U8(ComboSelected), false);
			Close();
		}
	}), true);
	B.RowButton(Btns, T(TEXT("Stop")), FSimpleDelegate::CreateLambda([this]() {
		if (ff::Session* Sx = S())
		{
			if (bComboSlow && Sx->Lab().time_scale < 1.0f)
			{
				SetLab([](ff::LabState& L) { L.time_scale = 1.0f; });
			}
			Sx->LabStopCombo();
		}
	}));
	B.RowButton(Btns, T(TEXT("Demo the steps")), FSimpleDelegate::CreateLambda([this]() {
		if (ff::Session* Sx = S())
		{
			Sx->LabStartCombo(FFUi::U8(ComboSelected), true);
			Close();
		}
	}));
}

// ---------------------------------------------------------------------------------------------- Matrix

void SFourfoldLabPanel::UpdateMatrix()
{
	if (ff::Session* Ses = S())
	{
		Matrix = Ses->LabMatrixPredict(FFUi::U8(ThreatId), ThreatParams, FFUi::U8(CounterId), CounterTier, bPerfect);
	}
}

void SFourfoldLabPanel::BuildMatrix(FFFPageBuilder& B)
{
	ff::Session* Ses = S();
	UpdateMatrix();
	B.Section(T(TEXT("Result")));
	B.Label(TAttribute<FText>::CreateLambda([this]() {
		if (!Matrix.ok)
		{
			return FText::FromString(FFUi::F(Matrix.msg));
		}
		return FText::FromString(FString::Printf(TEXT("%s  (%s)"), *FFUi::F(Matrix.outcome).ToUpper(), *FFUi::F(Matrix.band)));
	}), 3.4f, false, TAttribute<FSlateColor>::CreateLambda([this]() {
		const std::string& Band = Matrix.band;
		FLinearColor C = FFUi::Ink;
		if (!Matrix.ok || Band == "fail") C = FFUi::SRGB(0.94f, 0.48f, 0.44f);
		else if (Band == "full") C = FFUi::SRGB(0.5f, 0.88f, 0.54f);
		else if (Band == "partial") C = FFUi::SRGB(0.95f, 0.83f, 0.42f);
		else C = FFUi::SRGB(0.62f, 0.71f, 1.0f);
		return FSlateColor(C);
	}));
	B.Label(TAttribute<FText>::CreateLambda([this]() {
		if (!Matrix.ok)
		{
			return FText::GetEmpty();
		}
		return FText::FromString(FString::Printf(TEXT("threat power %.1f    counter power %.1f (x mods = %.1f)    ratio %.2f\na full block needs counter power of about %.1f"),
		                                         Matrix.tp, Matrix.cp, Matrix.cp_eff, Matrix.ratio, Matrix.needs));
	}), 2.6f, false);
	B.Label(TAttribute<FText>::CreateLambda([this]() {
		if (!Matrix.ok)
		{
			return FText::GetEmpty();
		}
		const FString To = Matrix.to.empty() ? FString() : TEXT("  -> ") + FFUi::F(Matrix.to);
		return FText::FromString(FString::Printf(TEXT("%s  vs  %s%s   rule: %s\n%s"), *FFUi::F(Matrix.threat_cls), *FFUi::F(Matrix.counter_cls), *To,
		                                         Matrix.rule_id.empty() ? TEXT("default") : *FFUi::F(Matrix.rule_id), *FFUi::F(Matrix.summary)));
	}), 2.4f, true);
	TSharedRef<SHorizontalBox> Btns = B.Row();
	auto Stage = [this](bool bClose) {
		if (ff::Session* Sx = S())
		{
			Sx->LabMatrixStage(FFUi::U8(ThreatId), ThreatParams, FFUi::U8(CounterId));
			Say(TEXT("staged"));
			if (bClose)
			{
				Close();
			}
		}
	};
	B.RowButton(Btns, T(TEXT("Stage it")), FSimpleDelegate::CreateLambda([Stage]() { Stage(false); }), true);
	B.RowButton(Btns, T(TEXT("Stage and close")), FSimpleDelegate::CreateLambda([Stage]() { Stage(true); }));

	// Threat picker + parameters.
	B.Section(T(TEXT("Threat")));
	const std::vector<ff::LabSpawnEntry> Entries = Ses->LabSpawnEntries();
	const ff::LabSpawnEntry* Threat = nullptr;
	for (const ff::LabSpawnEntry& E : Entries)
	{
		if (FFUi::F(E.id) == ThreatId)
		{
			Threat = &E;
		}
	}
	B.Button(FText::FromString(FString::Printf(TEXT("%s  %s"), Threat ? *FFUi::F(Threat->group + ": " + Threat->label) : *ThreatId, bThreatPicker ? TEXT("(close list)") : TEXT("(change)"))),
	         FSimpleDelegate::CreateLambda([this]() {
		         bThreatPicker = !bThreatPicker;
		         bRebuildQueued = true;
	         }));
	if (bThreatPicker)
	{
		TSharedRef<SWrapBox> Flow = B.Flow();
		for (const ff::LabSpawnEntry& E : Entries)
		{
			const FString Id = FFUi::F(E.id);
			B.FlowButton(Flow, FText::FromString(FFUi::F(E.label)), FSimpleDelegate::CreateLambda([this, Id]() {
				ThreatId = Id;
				ThreatParams = ff::LabSpawnParams();
				bThreatPicker = false;
				bRebuildQueued = true;
			}), TAttribute<bool>::CreateLambda([this, Id]() { return ThreatId == Id; }));
		}
	}
	if (Threat)
	{
		struct FParamRow
		{
			const TCHAR* Label;
			int32 Decimals;
			const TCHAR* Suffix;
			const ff::LabParamSpec* Spec;
			float ff::LabSpawnParams::*Field;
		};
		const FParamRow Rows[3] = {{TEXT("Mass"), 0, TEXT(" kg"), &Threat->mass, &ff::LabSpawnParams::mass},
		                           {TEXT("Speed"), 1, TEXT(" m/s"), &Threat->speed, &ff::LabSpawnParams::speed},
		                           {TEXT("Temperature"), 0, TEXT(" C"), &Threat->temp, &ff::LabSpawnParams::temp}};
		for (const FParamRow& R : Rows)
		{
			if (!R.Spec->present)
			{
				continue;
			}
			const float Def = R.Spec->def, Mn = R.Spec->min, Mx = FMath::Max(R.Spec->max, R.Spec->min + 0.01f);
			float ff::LabSpawnParams::*Field = R.Field;
			const int32 Decimals = R.Decimals;
			const TCHAR* Suffix = R.Suffix;
			B.Slider(T(R.Label), TAttribute<float>::CreateLambda([this, Field, Def]() { return ThreatParams.*Field >= 0.0f ? ThreatParams.*Field : Def; }), Mn, Mx,
			         Mx > 20.0f ? 1.0f : 0.5f, TAttribute<FText>::CreateLambda([this, Field, Def, Decimals, Suffix]() {
				         return FText::FromString(FormatNum(ThreatParams.*Field >= 0.0f ? ThreatParams.*Field : Def, Decimals, Suffix));
			         }),
			         FFFOnFloat::CreateLambda([this, Field](float V) {
				         ThreatParams.*Field = V;
				         UpdateMatrix();
			         }));
		}
		if (Threat->tier.present)
		{
			const int32 Def = FMath::RoundToInt(Threat->tier.def);
			B.Slider(T(TEXT("Tier")), TAttribute<float>::CreateLambda([this, Def]() { return float(ThreatParams.tier >= 0 ? ThreatParams.tier : Def); }), Threat->tier.min,
			         FMath::Max(Threat->tier.max, Threat->tier.min + 1.0f), 1.0f,
			         TAttribute<FText>::CreateLambda([this, Def]() { return FText::FromString(FString::Printf(TEXT("T%d"), ThreatParams.tier >= 0 ? ThreatParams.tier : Def)); }),
			         FFFOnFloat::CreateLambda([this](float V) {
				         ThreatParams.tier = FMath::RoundToInt(V);
				         UpdateMatrix();
			         }));
		}
	}

	// Counter picker + tier + perfect.
	B.Section(T(TEXT("Counter")));
	const std::vector<ff::MatrixCounter> Counters = Ses->LabMatrixCounters();
	const ff::MatrixCounter* Counter = nullptr;
	for (const ff::MatrixCounter& C : Counters)
	{
		if (FFUi::F(C.id) == CounterId)
		{
			Counter = &C;
		}
	}
	B.Button(FText::FromString(FString::Printf(TEXT("%s  %s"), Counter ? *FFUi::F(Counter->label) : *CounterId, bCounterPicker ? TEXT("(close list)") : TEXT("(change)"))),
	         FSimpleDelegate::CreateLambda([this]() {
		         bCounterPicker = !bCounterPicker;
		         bRebuildQueued = true;
	         }));
	if (bCounterPicker)
	{
		TSharedRef<SWrapBox> Flow = B.Flow();
		for (const ff::MatrixCounter& C : Counters)
		{
			const FString Id = FFUi::F(C.id);
			B.FlowButton(Flow, FText::FromString(FFUi::F(C.label)), FSimpleDelegate::CreateLambda([this, Id]() {
				CounterId = Id;
				CounterTier = 0;
				bCounterPicker = false;
				bRebuildQueued = true;
			}), TAttribute<bool>::CreateLambda([this, Id]() { return CounterId == Id; }),
			             C.element >= 0 ? FFUi::ElementColor(C.element) : FFUi::Accent);
		}
	}
	if (Counter && Counter->max_tier > 0)
	{
		B.Slider(T(TEXT("Counter tier")), TAttribute<float>::CreateLambda([this]() { return float(CounterTier); }), 0.0f, float(Counter->max_tier), 1.0f,
		         TAttribute<FText>::CreateLambda([this]() { return FText::FromString(FString::Printf(TEXT("T%d"), CounterTier)); }),
		         FFFOnFloat::CreateLambda([this](float V) {
			         CounterTier = FMath::RoundToInt(V);
			         UpdateMatrix();
		         }));
	}
	B.Toggle(T(TEXT("Perfect timing (x1.5)")), TAttribute<bool>::CreateLambda([this]() { return bPerfect; }), FFFOnBool::CreateLambda([this](bool bOn) {
		bPerfect = bOn;
		UpdateMatrix();
	}));
}

// ---------------------------------------------------------------------------------------------- Tuning

void SFourfoldLabPanel::RefreshTuningFields()
{
	TuneFields.clear();
	if (ff::Session* Ses = S())
	{
		if (!TuneMove.IsEmpty())
		{
			TuneFields = Ses->LabTuningFields(FFUi::U8(TuneMove));
		}
	}
}

void SFourfoldLabPanel::BuildTuning(FFFPageBuilder& B)
{
	ff::Session* Ses = S();
	TSharedRef<SHorizontalBox> Bar = B.Row();
	B.RowButton(Bar, T(TEXT("Save")), FSimpleDelegate::CreateLambda([this]() {
		const bool bOk = Host.SaveTuning && Host.SaveTuning();
		TuneStatus = bOk ? TEXT("saved to Saved/Fourfold/lab_tuning.json") : TEXT("save failed");
		Say(TuneStatus);
	}));
	B.RowButton(Bar, T(TEXT("Load")), FSimpleDelegate::CreateLambda([this]() {
		const bool bOk = Host.LoadTuning && Host.LoadTuning();
		TuneStatus = bOk ? TEXT("loaded lab_tuning.json") : TEXT("no lab_tuning.json yet");
		Say(TuneStatus);
		RefreshTuningFields();
	}));
	B.RowButton(Bar, T(TEXT("Reset all")), FSimpleDelegate::CreateLambda([this]() {
		if (ff::Session* Sx = S())
		{
			Sx->LabClearTuning();
		}
		TuneStatus = TEXT("everything restored");
		Say(TuneStatus);
		RefreshTuningFields();
	}));
	B.Label(TAttribute<FText>::CreateLambda([this]() { return FText::FromString(TuneStatus); }), 2.2f);

	// Move picker: element, sub-element, then the moves bound there.
	B.Choice(FText::GetEmpty(), {TEXT("Earth"), TEXT("Water"), TEXT("Fire"), TEXT("Air")}, TAttribute<int32>::CreateLambda([this]() { return TuneElement; }),
	         FFFOnInt::CreateLambda([this](int32 I) {
		         TuneElement = FMath::Clamp(I, 0, 3);
		         TuneSub = 0;
		         TuneMove.Reset();
		         bRebuildQueued = true;
	         }),
	         TAttribute<FLinearColor>::CreateLambda([this]() { return FFUi::ElementColor(TuneElement); }));
	TArray<FString> Subs;
	for (int32 i = 0; i < 4; ++i)
	{
		Subs.Add(FFUi::F(std::string(ff::SubName(TuneElement, i))));
	}
	B.Choice(FText::GetEmpty(), Subs, TAttribute<int32>::CreateLambda([this]() { return TuneSub; }), FFFOnInt::CreateLambda([this](int32 I) {
		TuneSub = FMath::Clamp(I, 0, 3);
		TuneMove.Reset();
		bRebuildQueued = true;
	}), TAttribute<FLinearColor>::CreateLambda([this]() { return FFUi::ElementColor(TuneElement); }));
	const std::vector<ff::MoveInfo> Moves = Ses->ListMoves(TuneElement, TuneSub);
	TSharedRef<SWrapBox> Flow = B.Flow();
	std::set<std::string> Seen;
	for (const ff::MoveInfo& M : Moves)
	{
		if (M.id.empty() || !Seen.insert(M.id).second)
		{
			continue;
		}
		const FString Id = FFUi::F(M.id);
		if (TuneMove.IsEmpty())
		{
			TuneMove = Id;
		}
		B.FlowButton(Flow, FText::FromString(FFUi::F(M.name)), FSimpleDelegate::CreateLambda([this, Id]() {
			TuneMove = Id;
			bRebuildQueued = true;
		}), TAttribute<bool>::CreateLambda([this, Id]() { return TuneMove == Id; }), FFUi::ElementColor(TuneElement));
	}
	RefreshTuningFields();
	if (TuneMove.IsEmpty())
	{
		return;
	}
	const ff::MoveInfo Info = Ses->GetMove(FFUi::U8(TuneMove));
	B.Title(FText::FromString(FString::Printf(TEXT("%s  -  %s"), *FFUi::F(Info.name), *TuneMove)), 2.8f);
	if (TuneFields.empty())
	{
		B.Label(T(TEXT("No numeric fields.")));
	}
	for (size_t i = 0; i < TuneFields.size(); ++i)
	{
		const ff::TuningField& F0 = TuneFields[i];
		float Lo, Hi;
		RangeFor(F0.original, Lo, Hi);
		const float Step = StepFor(FMath::Max(FMath::Abs(Lo), FMath::Abs(Hi)));
		const bool bWide = FMath::Abs(F0.original) >= 20.0;
		const FString Key = FFUi::F(F0.key);
		const size_t Index = i;
		B.Slider(FText::FromString(Key), TAttribute<float>::CreateLambda([this, Index, Lo, Hi]() {
			return Index < TuneFields.size() ? FMath::Clamp(float(TuneFields[Index].value), Lo, Hi) : 0.0f;
		}), Lo, Hi, Step, TAttribute<FText>::CreateLambda([this, Index, bWide]() {
			if (Index >= TuneFields.size())
			{
				return FText::GetEmpty();
			}
			const ff::TuningField& Fd = TuneFields[Index];
			return FText::FromString(FormatNum(float(Fd.value), bWide ? 1 : 2, Fd.overridden ? TEXT(" *") : TEXT("")));
		}), FFFOnFloat::CreateLambda([this, Index, Key](float V) {
			if (ff::Session* Sx = S())
			{
				Sx->LabSetTuning(FFUi::U8(TuneMove), FFUi::U8(Key), double(V));
				if (Index < TuneFields.size())
				{
					TuneFields[Index].value = V;
					TuneFields[Index].overridden = true;
				}
			}
		}));
	}
	B.Label(T(TEXT("Counter-rule thresholds are tuned in the Godot Lab for now (not exposed by the session facade).")), 2.1f);
}
