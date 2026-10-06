// Fourfold - UI root (see SFourfoldUiRoot.h).
#include "UI/SFourfoldUiRoot.h"

#include "UI/FourfoldUi.h"
#include "UI/SFourfoldHud.h"
#include "UI/SFourfoldLabPanel.h"
#include "UI/SFourfoldMenus.h"
#include "UI/SFourfoldTouchOverlay.h"

#include "Widgets/Layout/SBorder.h"
#include "Widgets/SOverlay.h"

void SFourfoldUiRoot::Construct(const FArguments& InArgs, const FFourfoldMenuHost& MenuHost, const FFourfoldLabHost& LabHost)
{
	SetCanTick(true);
	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot()[SAssignNew(Hud, SFourfoldHud)]
		+ SOverlay::Slot()[SAssignNew(Touch, SFourfoldTouchOverlay)]
		+ SOverlay::Slot()[SAssignNew(Lab, SFourfoldLabPanel, LabHost)]
		+ SOverlay::Slot()[SAssignNew(Menus, SFourfoldMenus, MenuHost)]
		+ SOverlay::Slot()
		[
			SNew(SBorder)
			.BorderImage(FFUiBrushes::Dim())   // black at 0.55 alpha, scaled by the fade below
			.Visibility_Lambda([this]() { return Fade > 0.003f ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
			.BorderBackgroundColor_Lambda([this]() { return FSlateColor(FLinearColor(0.0f, 0.0f, 0.0f, FMath::Clamp(Fade, 0.0f, 1.0f) / 0.55f)); })
		]
	];
	// The root itself never takes hits: children decide (HUD invisible, overlay / menus / panel when active).
	SetVisibility(EVisibility::SelfHitTestInvisible);
}

void SFourfoldUiRoot::FadeTo(float Target, float Seconds)
{
	FadeTarget = FMath::Clamp(Target, 0.0f, 1.0f);
	FadeRate = Seconds > 0.001f ? 1.0f / Seconds : 1000.0f;
}

void SFourfoldUiRoot::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	PixelToLocal = AllottedGeometry.Scale > 1e-4f ? 1.0f / AllottedGeometry.Scale : 1.0f;
	if (FFUi::UpdateMetrics(AllottedGeometry))
	{
		Menus->Rebuild();
		Lab->Rebuild();
	}
	Fade = FMath::FInterpConstantTo(Fade, FadeTarget, FMath::Clamp(InDeltaTime, 0.0f, 0.1f), FadeRate);
}
