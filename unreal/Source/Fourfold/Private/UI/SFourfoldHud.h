// Fourfold - in-play HUD (port of game/ui/hud.gd + target_marker.gd): thin vitals that fade when calm, element /
// sub-element line, only the resources that matter, status chips with remaining-time fill, the locked rival's panel,
// the charge bar with the move name and tier, objective / challenge lines, toasts, flashes, off-screen threat arrows,
// the lock-on marker, and the Lab / debug overlays (body labels, zone rings, timing bars, debug lines).
// Data is gathered once per frame inside OnFrame (UpdateFrame, the snapshot is only valid there) and painted later.
#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"
#include "Widgets/SLeafWidget.h"
#include "ff/Snapshot.h"
#include "ff/ViewModels.h"

class SFourfoldHud : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SFourfoldHud) {}
	SLATE_END_ARGS()

	/** Sim position -> viewport pixels (false when behind the camera). */
	using FProjectFn = TFunctionRef<bool(const ff::Vec3& /*SimPos*/, FVector2D& /*OutPixels*/)>;

	struct FFrameInput
	{
		float RealDt = 0.0f;
		ff::Vec3 CamForwardSim{0.0f, 0.0f, 1.0f};   // flat camera forward (threat arrows, zone radii)
		FVector2D ViewportPixels = FVector2D(1280.0, 720.0);
		bool bLabOverlay = false;                    // Lab: bodies / zones / power labels
		bool bShowDebug = false;                     // Settings: timing bars, debug lines, perf
		bool bReducedMotion = false;
		float Flashes = 1.0f;                        // setting 0..1
		FString LabStatus;                           // "FROZEN" / "x0.25" under the rival panel ("" = hidden)
		FString PerfText;
		TArray<FString> DebugLines;
	};

	void Construct(const FArguments& InArgs);

	/** Inside OnFrame: copies what the paint needs and projects world anchors. */
	void UpdateFrame(const ff::HudModel& Hud, const ff::Snapshot& Snap, const FFrameInput& In, FProjectFn Project);
	void Toast(const FString& Text, const FString& Kind = TEXT("info"));
	/** perfect | lightning | evade */
	void Flash(const FString& Kind);
	/** No player (title / attract): the HUD hides its gameplay parts. */
	void SetGameplayVisible(bool bVisible) { bGameplay = bVisible; }

	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(64.0, 64.0); }
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
	                      int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	struct FScreenLabel
	{
		FVector2D Px;
		FString Text;
		FLinearColor Color;
		float ZoneRadiusPx = 0.0f;
		bool bDot = true;
	};
	struct FTimingBar
	{
		FVector2D Px;
		float Startup = 0.0f, Active = 0.0f, Recovery = 0.0f, Elapsed = 0.0f;
		FString Text;
	};
	struct FThreat
	{
		FVector2D Dir;   // screen direction (x right, y down), unit
		bool bHot = false;
	};

	struct FPaintCtx;
	float DrawStatuses(const FPaintCtx& H, const std::vector<ff::StatusView>& Statuses, int32 ActorId, FVector2D Origin, bool bCentred) const;
	void DrawChargeBar(const FPaintCtx& H) const;
	void DrawMarker(const FPaintCtx& H) const;

	ff::HudModel Hud;
	FFrameInput Input;
	bool bGameplay = false;
	bool bHasPlayer = false;
	int32 ChargeElement = 0;

	// calm fade (vitals dim after 2.5 s without anything happening)
	float Alpha = 0.35f;
	float Calm = 0.0f;
	float LastHealth = 100.0f;

	// toast / flash
	FString ToastText;
	float ToastT = 0.0f;
	FString FlashKind;
	float FlashT = 0.0f;

	// lock-on marker (eased)
	bool bMarker = false;
	FVector2D MarkerPx = FVector2D::ZeroVector;
	FVector2D MarkerShown = FVector2D::ZeroVector;
	float MarkerAlpha = 0.0f;
	FString MarkerLabel;

	TArray<FThreat> Threats;
	TArray<FScreenLabel> Labels;
	TArray<FTimingBar> Bars;
	/** "actor/status" -> longest remaining time seen (status icon fill). */
	mutable TMap<FString, float> StatusT0;
};
