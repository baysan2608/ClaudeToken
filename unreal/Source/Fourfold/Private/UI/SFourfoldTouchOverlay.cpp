// Fourfold - touch HUD widget (see SFourfoldTouchOverlay.h). Drawing is a line-by-line port of touch_controls.gd
// _draw*(); sizes are the logic layout's (millimetres x ppm), colours the Godot palette (FFUi).
#include "UI/SFourfoldTouchOverlay.h"

#include "FourfoldSettings.h"
#include "Logic/FFGUiScale.h"
#include "UI/FourfoldUi.h"

#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformTime.h"
#include "Rendering/DrawElements.h"

namespace
{
	constexpr float kTau = 6.28318530718f;
	constexpr float kHalfPi = 1.57079632679f;

	FVector2D V(const ffg::Vec2& P) { return FVector2D(P.x, P.y); }
	FString S(const std::string& Str) { return FString(UTF8_TO_TCHAR(Str.c_str())); }
	FLinearColor Col(float R, float G, float B, float A) { return FLinearColor(R, G, B, FMath::Clamp(A, 0.0f, 1.0f)); }
	FLinearColor Tint(const FLinearColor& C, float A) { return FLinearColor(C.R, C.G, C.B, FMath::Clamp(A, 0.0f, 1.0f)); }
	/** Godot's Color.lerp on (linear) colours. */
	FLinearColor Mix(const FLinearColor& A, const FLinearColor& B, float T) { return A + (B - A) * FMath::Clamp(T, 0.0f, 1.0f); }
	/** Dark button fill of the Godot build (sRGB 0.04, 0.05, 0.07). */
	FLinearColor DarkFill(float A) { return FFUi::SRGB(0.04f, 0.05f, 0.07f, A); }
}

struct SFourfoldTouchOverlay::FDrawCtx
{
	FFUi::FPainter P;
	float Op = 0.8f;
	float Ppm = 8.0f;
	float Rw = 1.6f;
	bool bStrong = false;
	bool bReduced = false;
	bool bTechLive = false;
	FVector2D Size;
};

void SFourfoldTouchOverlay::Construct(const FArguments& InArgs)
{
	ForceVolatile(true);   // repainted every frame (fingers, rings, timers)
	SetCanTick(true);
	for (ffg::Vec2& P : LastTouchPos)
	{
		P = ffg::Vec2();
	}
}

void SFourfoldTouchOverlay::ApplySettings(const FFourfoldSettings& Src)
{
	ffg::TouchSettings T;
	T.control_scale = Src.ControlScale;
	T.control_opacity = Src.ControlOpacity;
	T.layout_preset = TCHAR_TO_UTF8(*Src.LayoutPreset);
	T.left_handed = Src.bLeftHanded;
	T.strong_labels = Src.bStrongLabels;
	T.camera_sensitivity = Src.CameraSensitivity;
	T.invert_y = Src.bInvertY;
	T.reduced_motion = Src.bReducedMotion;
	T.show_debug = Src.bShowDebug;
	Touch.ApplySettings(T);
	LastPpm = 0.0f;   // relayout on the next tick
}

void SFourfoldTouchOverlay::SetHud(const ff::HudModel& Hud)
{
	Touch.SetContext(ffg::TouchContextFromHud(Hud));
}

void SFourfoldTouchOverlay::SetActive(bool bInActive)
{
	if (bActive == bInActive)
	{
		return;
	}
	bActive = bInActive;
	if (!bActive)
	{
		ReleaseAll();
	}
	SetVisibility(bActive ? EVisibility::Visible : EVisibility::Collapsed);
}

void SFourfoldTouchOverlay::ReleaseAll()
{
	Touch.ReleaseAll(true);
	bMouseDown = false;
	for (bool& B : TouchDownFlags)
	{
		B = false;
	}
}

void SFourfoldTouchOverlay::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	const FVector2D Size = FVector2D(AllottedGeometry.GetLocalSize());
	if (Size.X < 2.0 || Size.Y < 2.0)
	{
		return;
	}
	if (!Size.Equals(LastSize, 0.5) || !FMath::IsNearlyEqual(FFUi::Ppm, LastPpm, 0.001f) || FFUi::SafeLocal != LastSafe)
	{
		LastSize = Size;
		LastPpm = FFUi::Ppm;
		LastSafe = FFUi::SafeLocal;
		ffg::Insets I;
		I.left = FFUi::SafeLocal.Left;
		I.top = FFUi::SafeLocal.Top;
		I.right = FFUi::SafeLocal.Right;
		I.bottom = FFUi::SafeLocal.Bottom;
		Touch.Relayout(ffg::Vec2(float(Size.X), float(Size.Y)), I, FFUi::Ppm);
	}
	Touch.Update(FMath::Clamp(InDeltaTime, 0.0f, 0.1f));
}

// ---------------------------------------------------------------------------------------------- input

int32 SFourfoldTouchOverlay::FingerOf(const FPointerEvent& E)
{
	const int32 Index = int32(E.GetPointerIndex());
	return (Index >= 0 && Index < ffg::TouchControls::kMouseFinger) ? Index : -1;
}

ffg::Vec2 SFourfoldTouchOverlay::LocalPos(const FGeometry& G, const FPointerEvent& E)
{
	const FVector2D L = FVector2D(G.AbsoluteToLocal(E.GetScreenSpacePosition()));
	return ffg::Vec2(float(L.X), float(L.Y));
}

FReply SFourfoldTouchOverlay::OnTouchStarted(const FGeometry& MyGeometry, const FPointerEvent& InTouchEvent)
{
	bSawTouch = true;
	bGamepadHidden = false;
	const int32 F = FingerOf(InTouchEvent);
	if (!bActive || F < 0)
	{
		return FReply::Unhandled();
	}
	const ffg::Vec2 P = LocalPos(MyGeometry, InTouchEvent);
	LastTouchPos[F] = P;
	TouchDownFlags[F] = true;
	Touch.TouchDown(F, P);
	// Capture this pointer: its moves / end come here even when another widget appears under the finger.
	return FReply::Handled().CaptureMouse(SharedThis(this));
}

FReply SFourfoldTouchOverlay::OnTouchMoved(const FGeometry& MyGeometry, const FPointerEvent& InTouchEvent)
{
	const int32 F = FingerOf(InTouchEvent);
	if (!bActive || F < 0 || !TouchDownFlags[F])
	{
		return FReply::Unhandled();
	}
	const ffg::Vec2 P = LocalPos(MyGeometry, InTouchEvent);
	LastTouchPos[F] = P;
	Touch.TouchMove(F, P);
	return FReply::Handled();
}

FReply SFourfoldTouchOverlay::OnTouchEnded(const FGeometry& MyGeometry, const FPointerEvent& InTouchEvent)
{
	const int32 F = FingerOf(InTouchEvent);
	if (F < 0 || !TouchDownFlags[F])
	{
		return FReply::Unhandled();
	}
	TouchDownFlags[F] = false;
	const ffg::Vec2 P = LocalPos(MyGeometry, InTouchEvent);
	LastTouchPos[F] = P;
	Touch.TouchUp(F, P, false);
	return FReply::Handled().ReleaseMouseCapture();
}

FReply SFourfoldTouchOverlay::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!bActive || !bMouseEmulation || MouseEvent.IsTouchEvent() || MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}
	LastMousePos = LocalPos(MyGeometry, MouseEvent);
	bMouseDown = true;
	Touch.TouchDown(ffg::TouchControls::kMouseFinger, LastMousePos);
	return FReply::Handled().CaptureMouse(SharedThis(this));
}

FReply SFourfoldTouchOverlay::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!bMouseDown || MouseEvent.IsTouchEvent())
	{
		return FReply::Unhandled();
	}
	LastMousePos = LocalPos(MyGeometry, MouseEvent);
	Touch.TouchMove(ffg::TouchControls::kMouseFinger, LastMousePos);
	return FReply::Handled();
}

FReply SFourfoldTouchOverlay::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!bMouseDown || MouseEvent.IsTouchEvent() || MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}
	bMouseDown = false;
	LastMousePos = LocalPos(MyGeometry, MouseEvent);
	Touch.TouchUp(ffg::TouchControls::kMouseFinger, LastMousePos, false);
	return FReply::Handled().ReleaseMouseCapture();
}

void SFourfoldTouchOverlay::OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent)
{
	SLeafWidget::OnMouseCaptureLost(CaptureLostEvent);
	// Capture taken away (window lost focus, another widget grabbed it): that finger is cancelled, never committed.
	const int32 Pointer = CaptureLostEvent.PointerIndex;
	if (bMouseDown && uint32(Pointer) == FSlateApplicationBase::CursorPointerIndex)
	{
		bMouseDown = false;
		Touch.TouchUp(ffg::TouchControls::kMouseFinger, LastMousePos, true);
	}
	else if (Pointer >= 0 && Pointer < ffg::TouchControls::kMouseFinger && TouchDownFlags[Pointer])
	{
		TouchDownFlags[Pointer] = false;
		Touch.TouchUp(Pointer, LastTouchPos[Pointer], true);
	}
}

// ---------------------------------------------------------------------------------------------- drawing

int32 SFourfoldTouchOverlay::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
                                     FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	if (!bActive || bGamepadHidden)
	{
		return LayerId;
	}
	FDrawCtx D{FFUi::FPainter(AllottedGeometry, OutDrawElements, LayerId)};
	D.Op = Touch.settings.control_opacity;
	D.Ppm = Touch.layout.ppm;
	D.Rw = FMath::Max(1.6f, 0.2f * D.Ppm);
	D.bStrong = Touch.settings.strong_labels;
	D.bReduced = Touch.settings.reduced_motion;
	D.bTechLive = Touch.TechLive();
	D.Size = FVector2D(AllottedGeometry.GetLocalSize());

	DrawStick(D);
	DrawCameraTouch(D);
	const float ChipDim = D.bTechLive ? 0.1f : 1.0f;
	for (int32 E = 0; E < 4; ++E)
	{
		DrawChip(D, E, D.Op * ChipDim);
	}
	DrawRoundButton(D, ffg::TI(ffg::TouchId::Guard), int32(FFUi::EGlyph::Guard), TEXT("GUARD"), FLinearColor::White, D.Op);
	if (Touch.ctx.guard_now)
	{
		// the perfect-guard window is open: the button rings bright (the same honest telegraph as the HUD's "NOW" pill)
		const int32 G = ffg::TI(ffg::TouchId::Guard);
		const FVector2D Gc = V(Touch.layout.centers[G]);
		const float Gr = Touch.layout.radii[G];
		D.P.Ring(Gc, Gr * 1.12f, D.Rw * 3.2f, Col(1, 1, 1, 0.9f));
		D.P.Circle(Gc, Gr * 0.96f, Col(1, 1, 1, 0.18f));
	}
	DrawRoundButton(D, ffg::TI(ffg::TouchId::Evade), int32(FFUi::EGlyph::Evade), TEXT("EVADE"), FLinearColor::White, D.Op);
	DrawAttack(D);
	DrawTechnique(D);
	DrawChargeRing(D);
	DrawPetals(D);
	DrawSubLabel(D);
	DrawSubRing(D);
	DrawRoundButton(D, ffg::TI(ffg::TouchId::Target), int32(FFUi::EGlyph::Target), TEXT(""), FLinearColor::White, D.Op * 0.9f);
	DrawRoundButton(D, ffg::TI(ffg::TouchId::Pause), int32(FFUi::EGlyph::Pause), TEXT(""), FLinearColor::White, D.Op * 0.9f);
	DrawTechAim(D);
	DrawCancelZone(D);
	if (Touch.settings.show_debug)
	{
		DrawDebug(D);
	}
	return LayerId + 1;
}

void SFourfoldTouchOverlay::DrawStick(const FDrawCtx& D) const
{
	const ffg::TouchLayout& L = Touch.layout;
	const float R = L.stick_radius;
	const float Alpha = Touch.StickAlpha();
	// Resting hint, always faintly there so the left thumb finds its zone.
	if (Touch.StickFinger() == -1)
	{
		const float Hint = D.Op * 0.22f * (1.0f - Alpha);
		D.P.Ring(V(L.stick_ghost), R * 0.9f, D.Rw * 0.8f, Col(1, 1, 1, Hint));
		D.P.Circle(V(L.stick_ghost), R * 0.2f, Col(1, 1, 1, Hint * 0.8f));
	}
	if (Alpha <= 0.0f)
	{
		return;
	}
	const FVector2D C = V(Touch.StickCenter());
	D.P.Circle(C, R * 1.02f, Col(0, 0, 0, 0.12f * Alpha * D.Op));
	D.P.Circle(C, R, Col(1, 1, 1, 0.05f * Alpha));
	D.P.Ring(C, R, D.Rw, Col(1, 1, 1, 0.5f * Alpha * D.Op));
	D.P.Ring(C, R * 0.46f, D.Rw * 0.7f, Col(1, 1, 1, 0.12f * Alpha * D.Op));
	const FVector2D K = V(Touch.StickKnob());
	D.P.Circle(K, R * 0.44f, Col(0, 0, 0, 0.18f * Alpha * D.Op));
	D.P.Circle(K, R * 0.40f, Col(1, 1, 1, 0.26f * Alpha * FMath::Min(1.0f, D.Op + 0.2f)));
	D.P.Ring(K, R * 0.40f, D.Rw, Col(1, 1, 1, 0.85f * Alpha * FMath::Min(1.0f, D.Op + 0.15f)));
}

void SFourfoldTouchOverlay::DrawCameraTouch(const FDrawCtx& D) const
{
	const int32 F = Touch.CameraFinger();
	if (F == -1)
	{
		return;
	}
	const FVector2D P = V(Touch.FingerPos(F));
	const float R = 2.4f * D.Ppm;
	D.P.Ring(P, R, D.Rw * 0.8f, Col(1, 1, 1, 0.3f * D.Op));
	D.P.Circle(P, R * 0.16f, Col(1, 1, 1, 0.4f * D.Op));
}

void SFourfoldTouchOverlay::DrawRoundButton(const FDrawCtx& D, int32 Id, int32 Glyph, const TCHAR* Label, const FLinearColor& TintCol, float Op) const
{
	const ffg::TouchLayout& L = Touch.layout;
	const FVector2D C = V(L.centers[Id]);
	const float Pr = Touch.VisPress(Id);
	const float R = L.radii[Id] * (1.0f - (D.bReduced ? 0.0f : 0.07f) * Pr);
	const float A = FMath::Lerp(Op, 1.0f, Pr);
	const bool bLabel = Label && Label[0] != 0;
	D.P.Circle(C, R * 1.1f, Col(0, 0, 0, 0.14f * A));
	D.P.Circle(C, R - D.Rw * 0.4f, Mix(DarkFill(0.40f * A), Col(1, 1, 1, 0.26f), Pr));
	D.P.Ring(C, R, D.Rw * 2.6f, Col(0, 0, 0, 0.26f * A));
	D.P.Ring(C, R, D.Rw * FMath::Lerp(1.0f, 1.35f, Pr), Tint(TintCol, FMath::Lerp(0.5f, 1.0f, Pr) * A));
	D.P.Ring(C, R * 0.84f, D.Rw * 0.7f, Tint(TintCol, 0.1f * A));
	const float Gy = (D.bStrong && bLabel) ? -0.2f * R : 0.0f;
	const float Gr = R * ((D.bStrong && bLabel) ? 0.38f : 0.5f);
	D.P.GlyphHalo(FFUi::EGlyph(Glyph), C + FVector2D(0.0, Gy), Gr, Tint(TintCol, FMath::Lerp(0.82f, 1.0f, Pr) * A), D.Rw * 1.05f);
	if (D.bStrong && bLabel)
	{
		DrawLabel(D, FString(Label), C + FVector2D(0.0, R * 0.7f), R * 0.25f, Col(1, 1, 1, A), R * 1.7f);
	}
}

void SFourfoldTouchOverlay::DrawAttack(const FDrawCtx& D) const
{
	const int32 Id = ffg::TI(ffg::TouchId::Attack);
	DrawRoundButton(D, Id, int32(FFUi::EGlyph::Attack), TEXT("ATTACK"), FLinearColor::White, D.Op);
	const ffg::TouchLayout& L = Touch.layout;
	if (D.bTechLive && !Touch.ctx.shape_label.empty())
	{
		// T+A: a second-finger tap on ATTACK shapes what the technique holds.
		const FLinearColor Sc = FFUi::ElementColor(Touch.ctx.element);
		const float Pulse = 0.5f + 0.5f * FMath::Sin(float(FPlatformTime::Seconds() * 8.0));
		D.P.Ring(V(L.centers[Id]), L.radii[Id] * 1.14f, D.Rw * 1.4f, Tint(Sc, 0.45f + 0.4f * Pulse));
	}
	if (Touch.AttackHeld())
	{
		const FVector2D C = V(L.centers[Id]);
		const float R = L.radii[Id];
		const float F = Touch.AttackRingFill();
		if (F > 0.05f)
		{
			const FLinearColor RingCol = F >= 1.0f ? FFUi::SRGB(1.0f, 0.92f, 0.7f, 1.0f) : Col(1, 1, 1, 0.9f);
			D.P.Arc(C, R * 1.16f, -kHalfPi, -kHalfPi + kTau * FMath::Min(F, 1.0f), D.Rw * 1.5f, RingCol, 64);
		}
	}
}

void SFourfoldTouchOverlay::DrawTechnique(const FDrawCtx& D) const
{
	const ffg::TouchLayout& L = Touch.layout;
	const int32 Id = ffg::TI(ffg::TouchId::Tech);
	const FVector2D C = V(L.centers[Id]);
	const float Pr = Touch.VisPress(Id);
	const float R = L.radii[Id] * (1.0f - (D.bReduced ? 0.0f : 0.07f) * Pr);
	const FLinearColor EC = FFUi::ElementColor(Touch.ctx.element);
	const float Avail = Touch.ctx.tech_available ? 1.0f : 0.5f;
	const float A = FMath::Lerp(D.Op, 1.0f, Pr) * Avail;
	D.P.Circle(C, R * 1.1f, Col(0, 0, 0, 0.14f * A));
	D.P.Circle(C, R - D.Rw * 0.4f, Mix(DarkFill(0.40f * A), Tint(EC, 0.3f), Pr));
	D.P.Ring(C, R, D.Rw * 2.6f, Col(0, 0, 0, 0.26f * A));
	D.P.Ring(C, R, D.Rw * FMath::Lerp(1.1f, 1.5f, Pr), Tint(EC, FMath::Lerp(0.62f, 1.0f, Pr) * A));
	D.P.Ring(C, R * 0.84f, D.Rw * 0.7f, Tint(EC, 0.12f * A));
	if (Touch.ctx.holding)
	{
		D.P.Circle(C, R * 0.9f, Tint(EC, 0.14f * A));
	}
	const FString Label = S(Touch.ctx.tech_label);
	const bool bLabel = !Label.IsEmpty();
	const float Gy = bLabel ? -0.2f * R : 0.0f;
	const float Gr = R * (bLabel ? 0.4f : 0.5f);
	D.P.GlyphHalo(FFUi::EGlyph(FMath::Clamp(Touch.ctx.element, 0, 3)), C + FVector2D(0.0, Gy), Gr, Tint(EC, 0.95f * A), D.Rw * 1.1f);
	if (bLabel)
	{
		DrawLabel(D, Label, C + FVector2D(0.0, R * 0.64f), R * (D.bStrong ? 0.27f : 0.24f), Col(1, 1, 1, (D.bStrong ? 0.95f : 0.72f) * A), R * 1.7f);
	}
}

void SFourfoldTouchOverlay::DrawChip(const FDrawCtx& D, int32 E, float Op) const
{
	const ffg::TouchLayout& L = Touch.layout;
	const int32 Id = ffg::TI(ffg::TouchId::Elem0) + E;
	const FVector2D C = V(L.centers[Id]);
	const float Pr = Touch.VisPress(Id);
	const float Flash = Touch.ChipFlash(E);
	const float R = L.radii[Id] * (1.0f - (D.bReduced ? 0.0f : 0.08f) * Pr);
	const FLinearColor EC = FFUi::ElementColor(E);
	const bool bUnlocked = Touch.IsElementUnlocked(E);
	const bool bSelected = E == Touch.ShownElement();
	float A = Op * (bUnlocked ? 1.0f : 0.38f);
	A = FMath::Lerp(A, FMath::Min(1.0f, A + 0.4f), FMath::Max(Pr, Flash));
	D.P.Circle(C, R * 1.12f, Col(0, 0, 0, 0.12f * A));
	const FLinearColor Fill = (bSelected && bUnlocked) ? Tint(EC, 0.30f * A) : DarkFill(0.38f * A);
	D.P.Circle(C, R - D.Rw * 0.4f, Fill);
	D.P.Ring(C, R, D.Rw * 2.4f, Col(0, 0, 0, 0.24f * A));
	const float RingW = D.Rw * ((bSelected && bUnlocked) ? 1.6f : 0.9f);
	const float RingA = ((bSelected && bUnlocked) ? 0.95f : 0.5f) * A;
	D.P.Ring(C, R, RingW, Tint(EC, RingA));
	D.P.GlyphHalo(FFUi::EGlyph(E), C, R * 0.5f, Tint(EC, (bUnlocked ? 0.95f : 0.6f) * A), D.Rw * 0.95f);
	if (!bUnlocked)
	{
		// Locked: a thin strike so it reads as unavailable even without colour.
		D.P.Line(C + FVector2D(-R, R) * 0.55, C + FVector2D(R, -R) * 0.55, D.Rw * 0.8f, Col(1, 1, 1, 0.45f * A));
	}
}

void SFourfoldTouchOverlay::DrawLabel(const FDrawCtx& D, const FString& Text, const FVector2D& Base, float SizePx, const FLinearColor& C, float Width) const
{
	// Godot draws on a baseline; the visual centre of a cap-height line sits ~0.35 em above it.
	float Px = FMath::Max(8.0f, SizePx);
	FSlateFontInfo F = FFUi::Font(Px, true, FMath::Max(1, FMath::RoundToInt(Px / 6.0f)), Col(0, 0, 0, C.A * 0.55f));
	const FVector2D M = FFUi::Measure(Text, F);
	if (M.X > Width && M.X > 1.0)
	{
		Px = FMath::Max(8.0f, Px * float(Width / M.X));
		F = FFUi::Font(Px, true, FMath::Max(1, FMath::RoundToInt(Px / 6.0f)), Col(0, 0, 0, C.A * 0.55f));
	}
	D.P.TextCentered(Text, FVector2D(Base.X, Base.Y - SizePx * 0.35f), F, C);
}

void SFourfoldTouchOverlay::DrawChargeRing(const FDrawCtx& D) const
{
	const ffg::TouchContext& Ctx = Touch.ctx;
	if (Ctx.charge_slot.empty())
	{
		return;
	}
	int32 Id = -1;
	if (Ctx.charge_slot == "attack") Id = ffg::TI(ffg::TouchId::Attack);
	else if (Ctx.charge_slot == "guard") Id = ffg::TI(ffg::TouchId::Guard);
	else if (Ctx.charge_slot == "tech") Id = ffg::TI(ffg::TouchId::Tech);
	else if (Ctx.charge_slot == "evade") Id = ffg::TI(ffg::TouchId::Evade);
	const int32 Mx = FMath::Clamp(Ctx.charge_max, 0, 3);
	if (Id < 0 || Mx <= 0)
	{
		return;
	}
	const int32 Tier = FMath::Clamp(Ctx.charge_tier, 0, 3);
	const float Frac = FMath::Clamp(Ctx.charge_frac, 0.0f, 1.0f);
	const ffg::TouchLayout& L = Touch.layout;
	const FVector2D C = V(L.centers[Id]);
	const float R = L.radii[Id] * 1.36f;
	const FLinearColor EC = FFUi::ElementColor(Ctx.element);
	const float Gap = FMath::DegreesToRadians(16.0f);
	const float Seg = (kTau - Gap * 3.0f) / 3.0f;
	const float W = D.Rw * 2.2f;
	for (int32 K = 0; K < 3; ++K)
	{
		const float A0 = -kHalfPi + Gap * 0.5f + float(K) * (Seg + Gap);
		const float A1 = A0 + Seg;
		const bool bUsable = K < Mx;
		// Track.
		D.P.Arc(C, R, A0, A1, W + D.Rw, Col(0, 0, 0, bUsable ? 0.30f : 0.12f), 24);
		D.P.Arc(C, R, A0, A1, W * 0.6f, Col(1, 1, 1, bUsable ? 0.20f : 0.07f), 24);
		if (!bUsable)
		{
			continue;
		}
		float Lit = 0.0f;
		if (K < Tier)
		{
			Lit = 1.0f;
		}
		else if (K == Tier)
		{
			Lit = Frac;
		}
		if (Lit > 0.0f)
		{
			const FLinearColor Bright = Mix(Tint(EC, 0.95f), FLinearColor::White, (K < Tier && Tier >= Mx) ? 0.35f : 0.0f);
			D.P.Arc(C, R, A0, A0 + Seg * Lit, W, Bright, 24);
		}
		// Notch tick at the end of the segment (where the tier is reached).
		const FVector2D Dir(FMath::Cos(A1), FMath::Sin(A1));
		const FVector2D Tip = C + Dir * R;
		D.P.Line(Tip - Dir * W * 0.9f, Tip + Dir * W * 0.9f, FMath::Max(1.5f, D.Rw * 0.9f), Col(1, 1, 1, K < Tier ? 0.85f : 0.4f));
	}
	if (Tier > 0)
	{
		DrawLabel(D, FString::Printf(TEXT("T%d"), Tier), C + FVector2D(-R * 0.78f, R * 0.95f + D.Ppm * 0.6f), FMath::Max(9.0f, D.Ppm * 2.4f), Col(1, 1, 1, 0.95f),
		          D.Ppm * 6.0f);
	}
}

float SFourfoldTouchOverlay::PetalFontPx() const
{
	return FMath::Max(12.0f, FMath::RoundToFloat(2.3f * Touch.layout.ppm * FMath::Min(Touch.settings.control_scale, 1.2f)));
}

FVector4f SFourfoldTouchOverlay::DrawPill(const FDrawCtx& D, FVector2D Pos, int32 Align, const FString& Text, const FLinearColor& Accent, bool bHot, float Alpha,
                                          int32 Arrow, bool bDim) const
{
	const float Fs = PetalFontPx();
	const float Ppm = Touch.layout.ppm;
	FSlateFontInfo F = FFUi::Font(Fs, true);
	const double Tw = FFUi::Measure(Text, F).X;
	const double Pad = 1.6 * Ppm;
	const double ArrowW = Arrow != 0 ? 2.6 * Ppm : 0.0;
	const double W = FMath::Min(Tw + Pad * 2.0 + ArrowW, 27.0 * Ppm);
	const double H = Fs + 1.5 * Ppm;
	double X = Pos.X - W * 0.5;
	if (Align < 0)
	{
		X = Pos.X - W;
	}
	else if (Align > 0)
	{
		X = Pos.X;
	}
	double Y = Pos.Y - H * 0.5;
	// Keep it on screen (the usable rect).
	const ffg::Rect& U = Touch.layout.usable;
	X = FMath::Clamp(X, double(U.x), FMath::Max(double(U.x), double(U.Right()) - W));
	Y = FMath::Clamp(Y, double(U.y), FMath::Max(double(U.y), double(U.Bottom()) - H));
	const FLinearColor Fill = bHot ? Tint(Accent, 0.62f * Alpha) : DarkFill(0.62f * Alpha);
	const FLinearColor Border = Tint(Accent, (bHot ? 1.0f : 0.6f) * Alpha);
	const float Bw = bHot ? FMath::Max(2.0f, FMath::RoundToFloat(0.2f * Ppm)) : FMath::Max(1.0f, FMath::RoundToFloat(0.14f * Ppm));
	D.P.RoundRect(FVector2D(X, Y), FVector2D(W, H), Fill, Border, Bw, float(H * 0.5));
	const FLinearColor TxtCol = Col(1, 1, 1, (bDim ? 0.45f : 1.0f) * Alpha);
	const double Tx = X + Pad + ArrowW;
	if (Arrow != 0)
	{
		DrawArrow(D, FVector2D(X + Pad + ArrowW * 0.45, Y + H * 0.5), Arrow, Fs * 0.28f, Tint(Accent, (bDim ? 0.4f : 0.95f) * Alpha));
	}
	const double Avail = W - Pad * 2.0 - ArrowW;
	if (Tw > Avail && Tw > 1.0)
	{
		F = FFUi::Font(FMath::Max(8.0f, Fs * float(Avail / Tw)), true);
	}
	const FVector2D M = FFUi::Measure(Text, F);
	D.P.Text(Text, FVector2D(Tx, Y + (H - M.Y) * 0.5), F, TxtCol);
	return FVector4f(float(X), float(Y), float(W), float(H));
}

void SFourfoldTouchOverlay::DrawArrow(const FDrawCtx& D, const FVector2D& C, int32 Dir, float R, const FLinearColor& C2) const
{
	switch (ff::Gesture(Dir))
	{
	case ff::Gesture::Up:
		D.P.Triangle(C + FVector2D(0, -R), C + FVector2D(R, R * 0.8f), C + FVector2D(-R, R * 0.8f), C2);
		break;
	case ff::Gesture::Down:
		D.P.Triangle(C + FVector2D(0, R), C + FVector2D(R, -R * 0.8f), C + FVector2D(-R, -R * 0.8f), C2);
		break;
	default:
		D.P.Triangle(C + FVector2D(-R * 1.15f, 0), C + FVector2D(-R * 0.1f, -R * 0.8f), C + FVector2D(-R * 0.1f, R * 0.8f), C2);
		D.P.Triangle(C + FVector2D(R * 1.15f, 0), C + FVector2D(R * 0.1f, -R * 0.8f), C + FVector2D(R * 0.1f, R * 0.8f), C2);
		break;
	}
}

void SFourfoldTouchOverlay::DrawPetals(const FDrawCtx& D) const
{
	const ffg::TouchContext& Ctx = Touch.ctx;
	const FLinearColor EC = FFUi::ElementColor(Ctx.element);
	const bool bAtkHeld = Touch.AttackHeld();
	const bool bAnyAtkPetal = !Ctx.petal_up.empty() || !Ctx.petal_down.empty() || !Ctx.petal_side.empty();
	if ((bAtkHeld || D.bStrong) && bAnyAtkPetal && !D.bTechLive)
	{
		const ffg::FlickRecognizer& Fl = Touch.AttackFlick();
		const ff::Gesture Hot = bAtkHeld ? Fl.hot : ff::Gesture::None;
		const float A = bAtkHeld ? 1.0f : 0.38f;
		const ff::Gesture Gs[3] = {ff::Gesture::Up, ff::Gesture::Down, ff::Gesture::Side};
		const std::string* Names[3] = {&Ctx.petal_up, &Ctx.petal_down, &Ctx.petal_side};
		for (int32 i = 0; i < 3; ++i)
		{
			if (Names[i]->empty())
			{
				continue;
			}
			const ffg::PetalAnchor An = Touch.layout.AttackPetalAnchor(Gs[i]);
			const bool bHot = Hot == Gs[i] || (Fl.fired && Fl.last == Gs[i] && bAtkHeld);
			DrawPill(D, V(An.pos), An.align, S(*Names[i]), EC, bHot, A, int32(Gs[i]));
		}
	}
	const bool bGrdHeld = Touch.ButtonHeld(ffg::TouchId::Guard);
	const bool bAnyGrdPetal = !Ctx.guard_petal_up.empty() || !Ctx.guard_petal_down.empty();
	if ((bGrdHeld || D.bStrong || Ctx.threat) && bAnyGrdPetal)
	{
		const ff::Gesture GHot = bGrdHeld ? Touch.GuardGestureHot() : ff::Gesture::None;
		const ff::Gesture Gs[2] = {ff::Gesture::Up, ff::Gesture::Down};
		const std::string* Names[2] = {&Ctx.guard_petal_up, &Ctx.guard_petal_down};
		// with a threat coming the petals name the answer (counter rule) and take the outcome band's colour
		const std::string* Answers[2] = {&Ctx.counter_up, &Ctx.counter_down};
		const int32 Bands[2] = {Ctx.counter_band_up, Ctx.counter_band_down};
		for (int32 i = 0; i < 2; ++i)
		{
			if (Names[i]->empty())
			{
				continue;
			}
			const bool bAnswer = Ctx.threat && !Answers[i]->empty();
			const FLinearColor BandCol = Bands[i] == 3 ? FLinearColor(0.2f, 1.0f, 0.35f) : Bands[i] == 2 ? FLinearColor(1.0f, 0.6f, 0.08f)
			                           : Bands[i] == 1 ? FLinearColor(1.0f, 0.12f, 0.08f) : FLinearColor(0.45f, 0.45f, 0.45f);
			const float GA = (bGrdHeld || bAnswer) ? 1.0f : 0.38f;
			const ffg::PetalAnchor An = Touch.layout.GuardPetalAnchor(Gs[i]);
			DrawPill(D, V(An.pos), An.align, S(bAnswer ? *Answers[i] : *Names[i]), bAnswer ? BandCol : EC, GHot == Gs[i] || bAnswer, GA, int32(Gs[i]));
		}
	}
	if (D.bTechLive && !Ctx.shape_label.empty())
	{
		const ffg::PetalAnchor An = Touch.layout.AttackPetalAnchor(ff::Gesture::Up);
		DrawPill(D, V(An.pos), An.align, TEXT("A: ") + S(Ctx.shape_label), EC, true, 1.0f);
	}
}

void SFourfoldTouchOverlay::DrawSubLabel(const FDrawCtx& D) const
{
	const ffg::TouchContext& Ctx = Touch.ctx;
	const ffg::TouchLayout& L = Touch.layout;
	const int32 E = FMath::Clamp(Ctx.element, 0, 3);
	const int32 Id = ffg::TI(ffg::TouchId::Elem0) + E;
	const FVector2D C = V(L.centers[Id]);
	const FVector2D Tech = V(L.centers[ffg::TI(ffg::TouchId::Tech)]);
	const FVector2D Out = (C - Tech).GetSafeNormal();
	const FLinearColor EC = FFUi::ElementColor(E);
	const float Fs = FMath::Max(11.0f, FMath::RoundToFloat(2.0f * L.ppm * FMath::Min(Touch.settings.control_scale, 1.2f)));
	const int32 Sub = FMath::Clamp(Ctx.sub, 0, 3);
	FString Text = S(Ctx.sub_names[size_t(Sub)]);
	if (Text.IsEmpty())
	{
		const std::string_view Sv = ff::SubName(E, Sub);
		Text = S(std::string(Sv));
	}
	const float Op = D.Op;
	const FSlateFontInfo F = FFUi::Font(Fs, true, FMath::Max(1, FMath::RoundToInt(Fs / 6.0f)), Col(0, 0, 0, 0.6f * Op));
	const FVector2D M = FFUi::Measure(Text, F);
	const double Tw = M.X;
	const FVector2D P = C + Out * (L.radii[Id] + 1.2f * L.ppm);
	// Place the text outside the arc: left of left-pointing chips, above upward ones (Godot: baseline positions).
	FVector2D Base;
	if (Out.X < -0.3)
	{
		Base = P + FVector2D(-Tw, Fs * 0.35f);
	}
	else
	{
		Base = P + FVector2D(-Tw * 0.5, -Fs * 0.15f);
	}
	if (L.left_handed && Out.X > 0.3)
	{
		Base.X = C.X + (L.radii[Id] + 1.2f * L.ppm);
	}
	const ffg::Rect& U = L.usable;
	Base.X = FMath::Clamp(Base.X, double(U.x), FMath::Max(double(U.x), double(U.Right()) - Tw));
	Base.Y = FMath::Clamp(Base.Y, double(U.y) + Fs, double(U.Bottom()));
	// Baseline -> top-left of the Slate text box (ascent ~0.93 em).
	D.P.Text(Text, FVector2D(Base.X, Base.Y - Fs * 0.93f), F, Tint(EC, 0.5f + 0.5f * Op));
}

void SFourfoldTouchOverlay::DrawSubRing(const FDrawCtx& D) const
{
	if (!Touch.IsRingOpen())
	{
		return;
	}
	const ffg::TouchContext& Ctx = Touch.ctx;
	const ffg::TouchLayout& L = Touch.layout;
	const FLinearColor EC = FFUi::ElementColor(Ctx.element);
	const std::array<ffg::Rect, 4> Rects = L.RingRects();
	// A soft backing so the ring reads on any scene.
	const ffg::Rect Ub = Rects[0].Merge(Rects[3]).Grow(L.ppm * 1.0f);
	D.P.RoundRect(FVector2D(Ub.x, Ub.y), FVector2D(Ub.w, Ub.h), FFUi::SRGB(0.02f, 0.025f, 0.035f, 0.45f), Tint(EC, 0.25f), 1.0f, L.ppm * 2.4f);
	const float Fs = FMath::Max(12.0f, FMath::RoundToFloat(2.4f * L.ppm * FMath::Min(Touch.settings.control_scale, 1.2f)));
	const FSlateFontInfo F = FFUi::Font(Fs, true);
	for (int32 i = 0; i < 4; ++i)
	{
		const ffg::Rect& R = Rects[size_t(i)];
		const bool bUnlocked = Touch.IsSubUnlocked(i);
		const bool bCur = i == Ctx.sub;
		const bool bHot = i == Touch.RingHover();
		FLinearColor Bg = FFUi::SRGB(0.05f, 0.06f, 0.08f, 0.86f);
		if (bCur)
		{
			Bg = Tint(EC, 0.34f);
		}
		if (bHot && bUnlocked)
		{
			Bg = Tint(EC, 0.62f);
		}
		const FLinearColor Border = Tint(EC, ((bCur || bHot) ? 1.0f : 0.55f) * (bUnlocked ? 1.0f : 0.4f));
		const float Bw = (bCur || bHot) ? FMath::Max(2.0f, FMath::RoundToFloat(0.25f * L.ppm)) : 1.0f;
		D.P.RoundRect(FVector2D(R.x, R.y), FVector2D(R.w, R.h), Bg, Border, Bw, R.h * 0.5f);
		FString Label = S(Ctx.sub_names[size_t(i)]);
		if (Label.IsEmpty())
		{
			Label = S(std::string(ff::SubName(FMath::Clamp(Ctx.element, 0, 3), i)));
		}
		const float A = bUnlocked ? 1.0f : 0.4f;
		const ffg::Vec2 Cc = R.Center();
		D.P.TextCentered(Label, FVector2D(Cc.x + L.ppm * 0.8f, Cc.y), F, Col(1, 1, 1, A));
		// Index dot (sub 0..3) at the left end.
		D.P.Circle(FVector2D(R.x + R.h * 0.5f, Cc.y), R.h * 0.12f, Tint(EC, A));
		if (!bUnlocked)
		{
			D.P.Line(FVector2D(R.x + R.h * 0.8f, R.y + R.h * 0.5f), FVector2D(R.Right() - R.h * 0.8f, R.y + R.h * 0.5f), 1.5f, Col(1, 1, 1, 0.3f));
		}
	}
}

void SFourfoldTouchOverlay::DrawTechAim(const FDrawCtx& D) const
{
	if (!Touch.TechLive())
	{
		return;
	}
	const FLinearColor EC = FFUi::ElementColor(Touch.ctx.element);
	const FVector2D O = V(Touch.TechOrigin());
	const float R = Touch.layout.aim_radius;
	D.P.Ring(O, R, D.Rw * 0.8f, Tint(EC, 0.28f * FMath::Min(1.0f, D.Op + 0.3f)));
	FVector2D Dlt = V(Touch.TechPos()) - O;
	if (Dlt.Size() > R)
	{
		Dlt = Dlt.GetSafeNormal() * R;
	}
	const FVector2D K = O + Dlt;
	if (Dlt.Size() > 2.0)
	{
		D.P.Line(O, K, D.Rw * 0.8f, Tint(EC, 0.35f));
	}
	D.P.Circle(K, R * 0.16f, Tint(EC, Touch.TechAimActive() ? 0.55f : 0.3f));
	D.P.Ring(K, R * 0.16f, D.Rw * 0.9f, Tint(EC, 0.95f));
}

void SFourfoldTouchOverlay::DrawCancelZone(const FDrawCtx& D) const
{
	const float A = Touch.CancelAlpha();
	if (A <= 0.01f)
	{
		return;
	}
	const ffg::TouchLayout& L = Touch.layout;
	const int32 Id = ffg::TI(ffg::TouchId::Cancel);
	const FVector2D C = V(L.centers[Id]);
	const float R = L.radii[Id];
	const float Near = Touch.CancelNear();
	D.P.Circle(C, R, FFUi::SRGB(0.05f, 0.05f, 0.07f, 0.62f * A));
	D.P.Circle(C, R, Tint(FFUi::Danger, (0.10f + 0.12f * Near) * A));
	D.P.Ring(C, R, D.Rw * (1.0f + 0.4f * Near), Tint(FFUi::Danger, (0.55f + 0.4f * Near) * A));
	D.P.Glyph(FFUi::EGlyph::Cancel, C + FVector2D(0.0, -R * 0.14f), R * 0.34f, Col(1, 1, 1, 0.9f * A), D.Rw * 1.1f);
	DrawLabel(D, TEXT("CANCEL"), C + FVector2D(0.0, R * 0.66f), R * 0.26f, Col(1, 1, 1, 0.85f * A), R * 2.0f);
}

void SFourfoldTouchOverlay::DrawDebug(const FDrawCtx& D) const
{
	const FLinearColor Yellow = Col(1, 0.9f, 0.2f, 0.5f);
	const ffg::TouchLayout& L = Touch.layout;
	D.P.Line(FVector2D(L.split_x, 0), FVector2D(L.split_x, D.Size.Y), 1.0f, Yellow);
	D.P.RoundRect(FVector2D(L.usable.x, L.usable.y), FVector2D(L.usable.w, L.usable.h), FLinearColor::Transparent, Col(0.3f, 1, 0.5f, 0.35f), 1.0f, 0.0f);
	for (int32 i = 0; i < ffg::kTouchCount; ++i)
	{
		D.P.Ring(V(L.centers[i]), L.hit_radii[i], 1.0f, Yellow);
	}
	D.P.Ring(V(L.stick_ghost), L.stick_radius, 1.0f, Yellow);
	const FSlateFontInfo F = FFUi::Font(16.0f);
	for (int32 i = 0; i < ffg::TouchControls::kMaxFingers; ++i)
	{
		const ffg::FingerRole Role = Touch.Role(i);
		if (Role != ffg::FingerRole::None)
		{
			const FVector2D P = V(Touch.FingerPos(i));
			D.P.Circle(P, 6.0f, Col(1, 0.3f, 0.3f, 0.8f));
			D.P.Text(FString::Printf(TEXT("%d:%d"), i, int32(Role)), P + FVector2D(10, -26), F, Col(1, 0.6f, 0.6f, 1));
		}
	}
	const ffg::Vec2 Mv = Touch.StickVec(), Aim = Touch.TechAim();
	const FString Txt = FString::Printf(TEXT("move %.2f,%.2f  tech aim %.2f,%.2f%s"), Mv.x, Mv.y, Aim.x, Aim.y,
	                                    Touch.IsTechniqueCancelled() ? TEXT(" cancelled") : TEXT(""));
	D.P.Text(Txt, FVector2D(L.usable.x + 12.0f, L.usable.y + 8.0f), FFUi::Font(18.0f), Col(1, 1, 0.6f, 0.9f));
}
