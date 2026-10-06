// Fourfold - the touch HUD (port of game/ui/touch_controls.gd drawing + input routing). The control grammar itself
// (finger ownership, stick, camera drag, ATTACK / GUARD flicks, TECHNIQUE aim / cancel / shape tap, element chips and
// the sub-element ring, pause, target) lives in the logic island (ffg::TouchControls, unit-tested); this widget only
// forwards Slate touches (FPointerEvent::GetPointerIndex) in its local units, emulates one finger with the mouse when
// the "touch UI: always" setting is on, and paints the controls from the logic's public state.
#pragma once

#include "CoreMinimal.h"
#include "Logic/FFGTouchControls.h"
#include "Widgets/SLeafWidget.h"

struct FFourfoldSettings;
namespace ff { struct HudModel; }

class SFourfoldTouchOverlay : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SFourfoldTouchOverlay) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Control size / opacity / preset / hand / labels / camera settings. */
	void ApplySettings(const FFourfoldSettings& S);
	/** Per frame: the HUD view model drives labels, petals, the charge ring and chip states. */
	void SetHud(const ff::HudModel& Hud);
	/** Active = shown and taking touches (gameplay). Going inactive releases every finger safely. */
	void SetActive(bool bInActive);
	bool IsActive() const { return bActive; }
	/** The mouse drives one finger (desktop testing of the touch UI). */
	void SetMouseEmulation(bool bOn) { bMouseEmulation = bOn; }
	/** Focus loss / pause: stick to zero, held buttons released, a held technique cancelled (never fired). */
	void ReleaseAll();
	/** Mobile "auto" mode: a gamepad in use hides the touch HUD until the next touch (CONTROLS.md). */
	void SetGamepadHidden(bool bHidden) { bGamepadHidden = bHidden; }
	bool IsGamepadHidden() const { return bGamepadHidden; }
	/** True once a real touch arrived this session ("auto" touch UI mode on desktops with a touch screen). */
	bool SawTouch() const { return bSawTouch; }
	/** Once per 60 Hz tick. */
	void FillFrame(ff::InputFrame& Frame) { Touch.FillFrame(Frame); }
	ffg::TouchControls& Controls() { return Touch; }
	const ffg::TouchControls& Controls() const { return Touch; }

	// SWidget
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(64.0, 64.0); }
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
	                      int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FReply OnTouchStarted(const FGeometry& MyGeometry, const FPointerEvent& InTouchEvent) override;
	virtual FReply OnTouchMoved(const FGeometry& MyGeometry, const FPointerEvent& InTouchEvent) override;
	virtual FReply OnTouchEnded(const FGeometry& MyGeometry, const FPointerEvent& InTouchEvent) override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override;
	virtual bool SupportsKeyboardFocus() const override { return false; }

private:
	static int32 FingerOf(const FPointerEvent& E);
	static ffg::Vec2 LocalPos(const FGeometry& G, const FPointerEvent& E);

	// drawing (const: Slate paints from a const widget)
	struct FDrawCtx;
	void DrawStick(const FDrawCtx& D) const;
	void DrawCameraTouch(const FDrawCtx& D) const;
	void DrawRoundButton(const FDrawCtx& D, int32 Id, int32 Glyph, const TCHAR* Label, const FLinearColor& Tint, float Op) const;
	void DrawAttack(const FDrawCtx& D) const;
	void DrawTechnique(const FDrawCtx& D) const;
	void DrawChip(const FDrawCtx& D, int32 E, float Op) const;
	void DrawLabel(const FDrawCtx& D, const FString& Text, const FVector2D& Base, float SizePx, const FLinearColor& Col, float Width) const;
	void DrawChargeRing(const FDrawCtx& D) const;
	FVector4f DrawPill(const FDrawCtx& D, FVector2D Pos, int32 Align, const FString& Text, const FLinearColor& Accent, bool bHot, float Alpha,
	                   int32 Arrow = 0, bool bDim = false) const;
	void DrawArrow(const FDrawCtx& D, const FVector2D& C, int32 Dir, float R, const FLinearColor& Col) const;
	void DrawPetals(const FDrawCtx& D) const;
	void DrawSubLabel(const FDrawCtx& D) const;
	void DrawSubRing(const FDrawCtx& D) const;
	void DrawTechAim(const FDrawCtx& D) const;
	void DrawCancelZone(const FDrawCtx& D) const;
	void DrawDebug(const FDrawCtx& D) const;
	float PetalFontPx() const;

	ffg::TouchControls Touch;
	bool bActive = false;
	bool bMouseEmulation = false;
	bool bMouseDown = false;
	bool bSawTouch = false;
	bool bGamepadHidden = false;
	ffg::Vec2 LastMousePos;
	ffg::Vec2 LastTouchPos[ffg::TouchControls::kMaxFingers];
	bool TouchDownFlags[ffg::TouchControls::kMaxFingers] = {};
	FVector2D LastSize = FVector2D::ZeroVector;
	float LastPpm = 0.0f;
	FMargin LastSafe;
};
