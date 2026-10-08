// Fourfold - Slate UI toolkit (see FourfoldUi.h). Colours are authored in sRGB (as the Godot build's ui_style.gd) and
// converted to linear, because Slate tints are linear and get sRGB-encoded on output.
#include "UI/FourfoldUi.h"

#include "Logic/FFGUiScale.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"

namespace FFUi
{
	float Ppm = 6.0f;
	FMargin SafeLocal(0.0f);

	bool UpdateMetrics(const FGeometry& RootGeometry)
	{
		const FVector2D Size = FVector2D(RootGeometry.GetLocalSize());
		const float Scale = RootGeometry.Scale > 1e-4f ? RootGeometry.Scale : 1.0f;
		if (Size.X < 1.0 || Size.Y < 1.0)
		{
			return false;
		}
		int32 Dpi = 0;
		const EScreenPhysicalAccuracy Accuracy = FPlatformApplicationMisc::GetPhysicalScreenDensity(Dpi);
		ffg::UiScaleInput In;
		In.dpi = float(Dpi);
		In.dpi_is_real = Accuracy != EScreenPhysicalAccuracy::Unknown && Dpi > 0;
		In.local_per_pixel = 1.0f / Scale;
		In.viewport_h_local = float(Size.Y);
		In.mobile = (PLATFORM_IOS || PLATFORM_ANDROID) != 0;
		const float NewPpm = ffg::PxPerMm(In);

		FMargin Px(0.0f);
		if (FSlateApplication::IsInitialized())
		{
			FSlateApplication::Get().GetSafeZoneSize(Px, FVector2f(float(Size.X) * Scale, float(Size.Y) * Scale));
		}
		ffg::Insets I;
		I.left = Px.Left / Scale;
		I.top = Px.Top / Scale;
		I.right = Px.Right / Scale;
		I.bottom = Px.Bottom / Scale;
		I = ffg::SanitizeInsets(I, ffg::Vec2(float(Size.X), float(Size.Y)));
		const FMargin NewSafe(I.left, I.top, I.right, I.bottom);
		const bool bChanged = !FMath::IsNearlyEqual(NewPpm, Ppm, 0.01f) || NewSafe != SafeLocal;
		Ppm = NewPpm;
		SafeLocal = NewSafe;
		return bChanged;
	}

	static float SrgbToLinear(float C)
	{
		C = FMath::Clamp(C, 0.0f, 1.0f);
		return C <= 0.04045f ? C / 12.92f : FMath::Pow((C + 0.055f) / 1.055f, 2.4f);
	}

	FLinearColor SRGB(float R, float G, float B, float A)
	{
		return FLinearColor(SrgbToLinear(R), SrgbToLinear(G), SrgbToLinear(B), A);
	}

	FLinearColor ElementColor(int32 Element)
	{
		static const FLinearColor Colors[4] = {
			SRGB(0.84f, 0.66f, 0.38f),   // earth
			SRGB(0.40f, 0.70f, 0.97f),   // water
			SRGB(0.98f, 0.47f, 0.30f),   // fire
			SRGB(0.72f, 0.90f, 0.86f),   // air
		};
		return Colors[FMath::Clamp(Element, 0, 3)];
	}

	FLinearColor WithAlpha(const FLinearColor& C, float A)
	{
		return FLinearColor(C.R, C.G, C.B, FMath::Clamp(A, 0.0f, 1.0f));
	}

	const FLinearColor Ink = SRGB(0.93f, 0.95f, 0.97f);
	const FLinearColor InkDim = SRGB(0.93f, 0.95f, 0.97f, 0.62f);
	const FLinearColor Surface = SRGB(0.075f, 0.085f, 0.105f, 0.985f);
	const FLinearColor SurfaceRaised = SRGB(0.13f, 0.145f, 0.17f, 0.96f);
	const FLinearColor SurfaceHover = SRGB(0.18f, 0.2f, 0.235f, 0.98f);
	const FLinearColor Line = FLinearColor(1.0f, 1.0f, 1.0f, 0.14f);
	const FLinearColor Accent = SRGB(0.62f, 0.82f, 1.0f);
	const FLinearColor Danger = SRGB(0.96f, 0.45f, 0.42f);
	const FLinearColor Gold = SRGB(1.0f, 0.86f, 0.52f);

	FSlateFontInfo Font(float PxHeight, bool bBold, int32 OutlinePx, const FLinearColor& OutlineColor)
	{
		// Slate font sizes are points at 96 dpi: 1 pt = 4/3 local units, so the em is PxHeight units tall.
		FSlateFontInfo F = FCoreStyle::GetDefaultFontStyle(bBold ? TEXT("Bold") : TEXT("Regular"), FMath::Max(4.0f, PxHeight * 0.75f));
		if (OutlinePx > 0)
		{
			F.OutlineSettings.OutlineSize = OutlinePx;
			F.OutlineSettings.OutlineColor = OutlineColor;
		}
		return F;
	}

	FVector2D Measure(const FString& Text, const FSlateFontInfo& InFont)
	{
		if (Text.IsEmpty() || !FSlateApplication::IsInitialized())
		{
			return FVector2D(0.0, InFont.Size / 0.75f * 1.2f);
		}
		const TSharedRef<FSlateFontMeasure> Service = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
		return FVector2D(Service->Measure(Text, InFont));
	}

	// ------------------------------------------------------------------ glyphs (unit space, y down, radius 1)
	using FStroke = TArray<FVector2f>;
	using FGlyphStrokes = TArray<FStroke>;

	static FStroke ArcPts(FVector2f C, float R, float A0, float A1, int32 Steps)
	{
		FStroke Out;
		Out.Reserve(Steps + 1);
		for (int32 i = 0; i <= Steps; ++i)
		{
			const float A = FMath::Lerp(A0, A1, float(i) / float(Steps));
			Out.Add(C + FVector2f(FMath::Cos(A), FMath::Sin(A)) * R);
		}
		return Out;
	}

	static FStroke Closed(FStroke P)
	{
		if (P.Num() > 0)
		{
			const FVector2f First = P[0];   // a copy: adding an element of the array to itself may reallocate under it
			P.Add(First);
		}
		return P;
	}

	/** Chaikin corner cutting (closed outlines wrap and are closed again). */
	static FStroke Chaikin(const FStroke& P, int32 Iters, bool bClosed)
	{
		FStroke Cur = P;
		for (int32 It = 0; It < Iters; ++It)
		{
			FStroke Next;
			const int32 N = Cur.Num();
			const int32 Last = bClosed ? N : N - 1;
			if (!bClosed)
			{
				Next.Add(Cur[0]);
			}
			for (int32 i = 0; i < Last; ++i)
			{
				const FVector2f A = Cur[i];
				const FVector2f B = Cur[(i + 1) % N];
				Next.Add(FMath::Lerp(A, B, 0.25f));
				Next.Add(FMath::Lerp(A, B, 0.75f));
			}
			if (!bClosed)
			{
				Next.Add(Cur[N - 1]);
			}
			Cur = MoveTemp(Next);
		}
		if (bClosed && Cur.Num() > 0)
		{
			const FVector2f First = Cur[0];
			Cur.Add(First);
		}
		return Cur;
	}

	static TArray<FGlyphStrokes> BuildGlyphs()
	{
		TArray<FGlyphStrokes> G;
		G.SetNum(10);
		const float D = PI / 180.0f;
		// Earth: faceted mountain.
		G[int32(EGlyph::Earth)] = {
			Closed({FVector2f(-1.0f, 0.62f), FVector2f(-0.42f, -0.34f), FVector2f(-0.08f, 0.12f), FVector2f(0.34f, -0.7f), FVector2f(1.0f, 0.62f)}),
			FStroke({FVector2f(0.34f, -0.7f), FVector2f(0.18f, -0.34f), FVector2f(0.5f, -0.34f)}),
		};
		// Water: droplet.
		{
			FStroke Drop;
			Drop.Add(FVector2f(0.0f, -1.0f));
			Drop.Append(ArcPts(FVector2f(0.0f, 0.26f), 0.66f, -31.3f * D, 148.7f * D, 18));
			Drop.Add(FVector2f(0.0f, -1.0f));
			G[int32(EGlyph::Water)] = {Drop, ArcPts(FVector2f(0.0f, 0.3f), 0.3f, 20.0f * D, 100.0f * D, 8)};
		}
		// Fire: flame with an inner lick.
		{
			const FStroke Flame = {FVector2f(0.12f, -1.0f), FVector2f(0.4f, -0.55f), FVector2f(0.66f, 0.05f), FVector2f(0.58f, 0.52f),
			                       FVector2f(0.28f, 0.88f), FVector2f(-0.1f, 0.96f), FVector2f(-0.46f, 0.76f), FVector2f(-0.64f, 0.38f),
			                       FVector2f(-0.55f, -0.02f), FVector2f(-0.34f, -0.3f), FVector2f(-0.14f, -0.04f), FVector2f(-0.1f, -0.5f)};
			const FStroke Inner = {FVector2f(0.02f, 0.28f), FVector2f(0.24f, 0.58f), FVector2f(0.06f, 0.82f), FVector2f(-0.2f, 0.62f)};
			G[int32(EGlyph::Fire)] = {Chaikin(Flame, 2, true), Chaikin(Inner, 2, true)};
		}
		// Air: three wind lines with curls.
		{
			FStroke L1 = {FVector2f(-0.95f, -0.42f), FVector2f(0.28f, -0.42f)};
			L1.Append(ArcPts(FVector2f(0.28f, -0.7f), 0.28f, 90.0f * D, -150.0f * D, 14));
			FStroke L2 = {FVector2f(-0.95f, 0.04f), FVector2f(0.62f, 0.04f)};
			L2.Append(ArcPts(FVector2f(0.62f, 0.32f), 0.28f, -90.0f * D, 150.0f * D, 14));
			FStroke L3 = {FVector2f(-0.6f, 0.5f), FVector2f(0.1f, 0.5f)};
			L3.Append(ArcPts(FVector2f(0.1f, 0.72f), 0.22f, -90.0f * D, 120.0f * D, 10));
			G[int32(EGlyph::Air)] = {L1, L2, L3};
		}
		// Attack: three slashes.
		G[int32(EGlyph::Attack)] = {
			FStroke({FVector2f(-0.72f, 0.78f), FVector2f(0.78f, -0.72f)}),
			FStroke({FVector2f(-0.98f, 0.3f), FVector2f(0.3f, -0.98f)}),
			FStroke({FVector2f(-0.3f, 0.98f), FVector2f(0.98f, -0.3f)}),
		};
		// Guard: shield.
		G[int32(EGlyph::Guard)] = {
			Closed({FVector2f(-0.72f, -0.62f), FVector2f(0.0f, -0.88f), FVector2f(0.72f, -0.62f), FVector2f(0.72f, 0.08f), FVector2f(0.4f, 0.58f),
			        FVector2f(0.0f, 0.9f), FVector2f(-0.4f, 0.58f), FVector2f(-0.72f, 0.08f)}),
			FStroke({FVector2f(0.0f, -0.5f), FVector2f(0.0f, 0.5f)}),
		};
		// Evade: double chevron.
		G[int32(EGlyph::Evade)] = {
			FStroke({FVector2f(-0.82f, -0.6f), FVector2f(-0.22f, 0.0f), FVector2f(-0.82f, 0.6f)}),
			FStroke({FVector2f(0.0f, -0.6f), FVector2f(0.6f, 0.0f), FVector2f(0.0f, 0.6f)}),
		};
		// Target: reticle.
		G[int32(EGlyph::Target)] = {
			ArcPts(FVector2f(0.0f, 0.0f), 0.52f, 0.0f, 2.0f * PI, 28),
			FStroke({FVector2f(0.0f, -1.0f), FVector2f(0.0f, -0.74f)}),
			FStroke({FVector2f(0.0f, 1.0f), FVector2f(0.0f, 0.74f)}),
			FStroke({FVector2f(-1.0f, 0.0f), FVector2f(-0.74f, 0.0f)}),
			FStroke({FVector2f(1.0f, 0.0f), FVector2f(0.74f, 0.0f)}),
		};
		// Pause: two bars.
		G[int32(EGlyph::Pause)] = {
			FStroke({FVector2f(-0.36f, -0.72f), FVector2f(-0.36f, 0.72f)}),
			FStroke({FVector2f(0.36f, -0.72f), FVector2f(0.36f, 0.72f)}),
		};
		// Cancel: cross.
		G[int32(EGlyph::Cancel)] = {
			FStroke({FVector2f(-0.7f, -0.7f), FVector2f(0.7f, 0.7f)}),
			FStroke({FVector2f(0.7f, -0.7f), FVector2f(-0.7f, 0.7f)}),
		};
		return G;
	}

	static const TArray<FGlyphStrokes>& Glyphs()
	{
		static const TArray<FGlyphStrokes> G = BuildGlyphs();
		return G;
	}

	// ------------------------------------------------------------------ painter
	static FPaintGeometry BoxGeo(const FGeometry& Geo, const FVector2D& Pos, const FVector2D& Size)
	{
		return Geo.ToPaintGeometry(FVector2f(float(Size.X), float(Size.Y)), FSlateLayoutTransform(FVector2f(float(Pos.X), float(Pos.Y))));
	}

	void FPainter::Circle(const FVector2D& C, float R, const FLinearColor& Fill) const
	{
		if (R <= 0.0f || Fill.A <= 0.001f)
		{
			return;
		}
		const FSlateRoundedBoxBrush Brush(FLinearColor::White, R, FLinearColor::Transparent, 0.0f);
		FSlateDrawElement::MakeBox(Out, Layer, BoxGeo(Geo, C - FVector2D(R, R), FVector2D(2.0 * R, 2.0 * R)), &Brush, ESlateDrawEffect::None, Fill);
	}

	void FPainter::Ring(const FVector2D& C, float R, float Width, const FLinearColor& Col) const
	{
		if (R <= 0.0f || Width <= 0.0f || Col.A <= 0.001f)
		{
			return;
		}
		// The rounded-box outline is drawn inside the box: grow the box by half the width so the stroke centres on R.
		const float Outer = R + Width * 0.5f;
		const FSlateRoundedBoxBrush Brush(FLinearColor::White, Outer, Col, Width);
		FSlateDrawElement::MakeBox(Out, Layer, BoxGeo(Geo, C - FVector2D(Outer, Outer), FVector2D(2.0 * Outer, 2.0 * Outer)), &Brush, ESlateDrawEffect::None,
		                           FLinearColor::Transparent);
	}

	void FPainter::Arc(const FVector2D& C, float R, float A0, float A1, float Width, const FLinearColor& Col, int32 Segments) const
	{
		if (R <= 0.0f || Col.A <= 0.001f || FMath::IsNearlyEqual(A0, A1))
		{
			return;
		}
		const int32 N = FMath::Max(2, Segments);
		TArray<FVector2f> Pts;
		Pts.Reserve(N + 1);
		for (int32 i = 0; i <= N; ++i)
		{
			const float A = FMath::Lerp(A0, A1, float(i) / float(N));
			Pts.Add(FVector2f(float(C.X) + FMath::Cos(A) * R, float(C.Y) + FMath::Sin(A) * R));
		}
		Polyline(Pts, Width, Col);
	}

	void FPainter::Polyline(const TArray<FVector2f>& Points, float Width, const FLinearColor& Col) const
	{
		if (Points.Num() < 2 || Col.A <= 0.001f)
		{
			return;
		}
		FSlateDrawElement::MakeLines(Out, Layer, Geo.ToPaintGeometry(), Points, ESlateDrawEffect::None, Col, true, FMath::Max(0.5f, Width));
	}

	void FPainter::Line(const FVector2D& A, const FVector2D& B, float Width, const FLinearColor& Col) const
	{
		TArray<FVector2f> Pts;
		Pts.Add(FVector2f(float(A.X), float(A.Y)));
		Pts.Add(FVector2f(float(B.X), float(B.Y)));
		Polyline(Pts, Width, Col);
	}

	void FPainter::Rect(const FVector2D& Pos, const FVector2D& Size, const FLinearColor& Fill, float Radius) const
	{
		if (Size.X <= 0.0 || Size.Y <= 0.0 || Fill.A <= 0.001f)
		{
			return;
		}
		const float Rad = FMath::Min(Radius, float(FMath::Min(Size.X, Size.Y)) * 0.5f);
		const FSlateRoundedBoxBrush Brush(FLinearColor::White, Rad, FLinearColor::Transparent, 0.0f);
		FSlateDrawElement::MakeBox(Out, Layer, BoxGeo(Geo, Pos, Size), &Brush, ESlateDrawEffect::None, Fill);
	}

	void FPainter::RoundRect(const FVector2D& Pos, const FVector2D& Size, const FLinearColor& Fill, const FLinearColor& Border, float BorderWidth, float Radius) const
	{
		if (Size.X <= 0.0 || Size.Y <= 0.0 || (Fill.A <= 0.001f && (Border.A <= 0.001f || BorderWidth <= 0.0f)))
		{
			return;
		}
		const float Rad = FMath::Min(Radius, float(FMath::Min(Size.X, Size.Y)) * 0.5f);
		const FSlateRoundedBoxBrush Brush(FLinearColor::White, Rad, Border, BorderWidth > 0.0f ? BorderWidth : 0.0f);
		FSlateDrawElement::MakeBox(Out, Layer, BoxGeo(Geo, Pos, Size), &Brush, ESlateDrawEffect::None, Fill.A > 0.001f ? Fill : FLinearColor::Transparent);
	}

	void FPainter::Triangle(const FVector2D& A, const FVector2D& B, const FVector2D& C, const FLinearColor& Col) const
	{
		// Slate has no plain filled-polygon element without a texture resource: fill the triangle with its own stroke.
		// Shrink it to half size around the incentre and stroke with the full inradius - the band covers exactly the
		// original triangle (with slightly rounded corners).
		const double La = FVector2D::Distance(B, C), Lb = FVector2D::Distance(C, A), Lc = FVector2D::Distance(A, B);
		const double P = La + Lb + Lc;
		if (P <= 1e-3 || Col.A <= 0.001f)
		{
			return;
		}
		const FVector2D I = (A * La + B * Lb + C * Lc) / P;
		const double Area = FMath::Abs((B.X - A.X) * (C.Y - A.Y) - (C.X - A.X) * (B.Y - A.Y)) * 0.5;
		const double Inradius = 2.0 * Area / P;
		if (Inradius <= 0.05)
		{
			return;
		}
		auto Half = [&I](const FVector2D& V) { return FVector2f(float(I.X + (V.X - I.X) * 0.5), float(I.Y + (V.Y - I.Y) * 0.5)); };
		TArray<FVector2f> Pts = {Half(A), Half(B), Half(C), Half(A), Half(B)};
		FSlateDrawElement::MakeLines(Out, Layer, Geo.ToPaintGeometry(), Pts, ESlateDrawEffect::None, Col, true, float(Inradius));
	}

	void FPainter::Text(const FString& S, const FVector2D& Pos, const FSlateFontInfo& InFont, const FLinearColor& Col) const
	{
		if (S.IsEmpty() || Col.A <= 0.001f)
		{
			return;
		}
		const FVector2D Size = Measure(S, InFont);
		FSlateDrawElement::MakeText(Out, Layer, BoxGeo(Geo, Pos, FVector2D(FMath::Max(Size.X, 1.0), FMath::Max(Size.Y, 1.0))), S, InFont,
		                            ESlateDrawEffect::None, Col);
	}

	void FPainter::TextCentered(const FString& S, const FVector2D& Center, const FSlateFontInfo& InFont, const FLinearColor& Col) const
	{
		if (S.IsEmpty() || Col.A <= 0.001f)
		{
			return;
		}
		const FVector2D Size = Measure(S, InFont);
		FSlateDrawElement::MakeText(Out, Layer, BoxGeo(Geo, Center - Size * 0.5, FVector2D(FMath::Max(Size.X, 1.0), FMath::Max(Size.Y, 1.0))), S,
		                            InFont, ESlateDrawEffect::None, Col);
	}

	void FPainter::Glyph(EGlyph G, const FVector2D& C, float R, const FLinearColor& Col, float Width) const
	{
		const TArray<FGlyphStrokes>& All = Glyphs();
		const int32 Index = int32(G);
		if (!All.IsValidIndex(Index) || Col.A <= 0.001f)
		{
			return;
		}
		const FVector2f Cf(float(C.X), float(C.Y));
		for (const FStroke& S : All[Index])
		{
			TArray<FVector2f> Pts;
			Pts.Reserve(S.Num());
			for (const FVector2f& P : S)
			{
				Pts.Add(Cf + P * R);
			}
			Polyline(Pts, Width, Col);
		}
	}

	void FPainter::GlyphHalo(EGlyph G, const FVector2D& C, float R, const FLinearColor& Col, float Width) const
	{
		Glyph(G, C, R, FLinearColor(0.0f, 0.0f, 0.0f, 0.30f * Col.A), Width * 2.4f);
		Glyph(G, C, R, Col, Width);
	}
}

// ---------------------------------------------------------------------------------------------- SFFPressable

namespace
{
	FVector2D LocalOf(const FGeometry& G, const FPointerEvent& E)
	{
		return FVector2D(G.AbsoluteToLocal(E.GetScreenSpacePosition()));
	}
	bool IsPrimaryPress(const FPointerEvent& E)
	{
		return E.IsTouchEvent() || E.GetEffectingButton() == EKeys::LeftMouseButton;
	}
}

FReply SFFPressable::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!IsPrimaryPress(MouseEvent) || !IsPressable() || !IsEnabled())
	{
		return FReply::Unhandled();
	}
	bPressed = true;
	bPressIsTouch = MouseEvent.IsTouchEvent();
	PressStartScreen = FVector2D(MouseEvent.GetScreenSpacePosition());
	OnPressStart(MyGeometry, LocalOf(MyGeometry, MouseEvent));
	// A touch press is not captured unless the widget drags (slider): moves then bubble to an enclosing scroll box,
	// which takes the gesture over once it pans (we let go of the press when the finger travels, see OnMouseMove).
	if (bPressIsTouch && !WantsTouchCapture())
	{
		return FReply::Handled();
	}
	return FReply::Handled().CaptureMouse(SharedThis(this));
}

FReply SFFPressable::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!bPressed)
	{
		return FReply::Unhandled();
	}
	bPressed = false;
	const FVector2D Local = LocalOf(MyGeometry, MouseEvent);
	const FVector2D Size = FVector2D(MyGeometry.GetLocalSize());
	const bool bInside = Local.X >= 0.0 && Local.Y >= 0.0 && Local.X <= Size.X && Local.Y <= Size.Y;
	OnPressEnd(MyGeometry, Local, bInside && IsPressable());
	FReply Reply = FReply::Handled();
	if (HasMouseCapture())
	{
		Reply.ReleaseMouseCapture();
	}
	return Reply;
}

FReply SFFPressable::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!bPressed)
	{
		return FReply::Unhandled();
	}
	if (bPressIsTouch && !WantsTouchCapture())
	{
		const double Travel = FVector2D::Distance(FVector2D(MouseEvent.GetScreenSpacePosition()), PressStartScreen);
		const double Slop = FMath::Max(8.0, double(FFUi::Mm(2.5f)) * double(MyGeometry.Scale));
		if (Travel > Slop)
		{
			bPressed = false;   // the finger is scrolling, not tapping
			return FReply::Unhandled();
		}
		return FReply::Unhandled();   // let the scroll box see the move
	}
	OnPressMove(MyGeometry, LocalOf(MyGeometry, MouseEvent));
	return FReply::Handled();
}

void SFFPressable::OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	SLeafWidget::OnMouseEnter(MyGeometry, MouseEvent);
	if (!MouseEvent.IsTouchEvent())
	{
		bHovered = true;
	}
}

void SFFPressable::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	SLeafWidget::OnMouseLeave(MouseEvent);
	bHovered = false;
	if (bPressed && bPressIsTouch && !WantsTouchCapture())
	{
		bPressed = false;   // finger slid off: no click
	}
}

void SFFPressable::OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent)
{
	SLeafWidget::OnMouseCaptureLost(CaptureLostEvent);
	bPressed = false;
}

// ---------------------------------------------------------------------------------------------- SFFButton

void SFFButton::Construct(const FArguments& InArgs)
{
	Label = InArgs._Label;
	SubLabel = InArgs._SubLabel;
	HeightMm = InArgs._HeightMm;
	FontMm = InArgs._FontMm;
	bPrimary = InArgs._bPrimary;
	bEnabled = InArgs._bEnabled;
	Accent = InArgs._Accent;
	bCentered = InArgs._bCentered;
	bSelected = InArgs._bSelected;
	OnClicked = InArgs._OnClicked;
	ForceVolatile(true);
}

FVector2D SFFButton::ComputeDesiredSize(float) const
{
	const FVector2D L = FFUi::Measure(Label.Get(FText::GetEmpty()).ToString(), FFUi::Font(FFUi::Mm(FontMm), true));
	const FVector2D S = FFUi::Measure(SubLabel.Get(FText::GetEmpty()).ToString(), FFUi::Font(FFUi::Mm(FontMm * 0.78f)));
	const bool bSub = !SubLabel.Get(FText::GetEmpty()).IsEmpty();
	const float H = FFUi::Mm(bSub ? FMath::Max(HeightMm, 13.0f) : FMath::Max(HeightMm, 9.0f));
	return FVector2D(FMath::Max(L.X, S.X) + FFUi::Mm(8.0f), H);
}

int32 SFFButton::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
                         int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const FFUi::FPainter P(AllottedGeometry, OutDrawElements, LayerId);
	const FVector2D Size = FVector2D(AllottedGeometry.GetLocalSize());
	const bool bOn = IsPressable() && bParentEnabled;
	const float Dim = bOn ? 1.0f : 0.42f;
	const FLinearColor Acc = Accent.Get(FFUi::Accent);
	const float Press = PressVisual();
	const float Radius = FFUi::Mm(1.6f);

	const bool bSel = bSelected.Get(false);
	const bool bStrong = bPrimary || bSel;
	FLinearColor Fill = bSel ? FMath::Lerp(FFUi::SurfaceRaised, FFUi::WithAlpha(Acc, 0.96f), 0.55f)
	                         : (bPrimary ? FMath::Lerp(FFUi::SurfaceRaised, FFUi::WithAlpha(Acc, 0.96f), 0.28f) : FFUi::SurfaceRaised);
	Fill = FMath::Lerp(Fill, bStrong ? FFUi::WithAlpha(Acc, 0.96f) : FFUi::SurfaceHover, Press * (bStrong ? 0.45f : 1.0f));
	Fill.A *= Dim;
	FLinearColor Border = bStrong ? FFUi::WithAlpha(Acc, bSel ? 1.0f : 0.75f) : (bHovered ? FLinearColor(1, 1, 1, 0.24f) : FFUi::Line);
	Border.A *= Dim;
	P.RoundRect(FVector2D(0, 0), Size, Fill, Border, FMath::Max(1.0f, FFUi::Mm(0.18f)), Radius);
	// Accent edge on the left (element / mode colour).
	P.Rect(FVector2D(FFUi::Mm(0.9f), Size.Y * 0.22), FVector2D(FFUi::Mm(0.7f), Size.Y * 0.56), FFUi::WithAlpha(Acc, (bStrong ? 1.0f : 0.7f) * Dim), FFUi::Mm(0.35f));
	if (bNavFocus)
	{
		const float W = FMath::Max(2.0f, FFUi::Mm(0.32f));
		P.RoundRect(FVector2D(-W, -W), Size + FVector2D(2.0 * W, 2.0 * W), FLinearColor::Transparent, FFUi::WithAlpha(FFUi::Accent, 0.95f), W, Radius + W);
	}

	const FString L = Label.Get(FText::GetEmpty()).ToString();
	const FString S = SubLabel.Get(FText::GetEmpty()).ToString();
	const FSlateFontInfo LF = FFUi::Font(FFUi::Mm(FontMm), true);
	const FSlateFontInfo SF = FFUi::Font(FFUi::Mm(FontMm * 0.78f));
	const FLinearColor Ink = FFUi::WithAlpha(FFUi::Ink, Dim);
	const FLinearColor InkDim = FFUi::WithAlpha(FFUi::InkDim, 0.62f * Dim);
	const double Pad = FFUi::Mm(3.6f);
	if (S.IsEmpty())
	{
		const FVector2D M = FFUi::Measure(L, LF);
		const double X = bCentered ? (Size.X - M.X) * 0.5 : Pad;
		P.Text(L, FVector2D(X, (Size.Y - M.Y) * 0.5), LF, Ink);
	}
	else
	{
		const FVector2D M = FFUi::Measure(L, LF);
		const FVector2D MS = FFUi::Measure(S, SF);
		const double Total = M.Y + MS.Y * 0.95;
		const double Y0 = (Size.Y - Total) * 0.5;
		P.Text(L, FVector2D(bCentered ? (Size.X - M.X) * 0.5 : Pad, Y0), LF, Ink);
		P.Text(S, FVector2D(bCentered ? (Size.X - MS.X) * 0.5 : Pad, Y0 + M.Y * 0.95), SF, InkDim);
	}
	return LayerId + 1;
}

void SFFButton::OnPressEnd(const FGeometry&, const FVector2D&, bool bInside)
{
	if (bInside)
	{
		OnClicked.ExecuteIfBound();
	}
}

// ---------------------------------------------------------------------------------------------- SFFToggle

void SFFToggle::Construct(const FArguments& InArgs)
{
	Label = InArgs._Label;
	Value = InArgs._Value;
	OnChanged = InArgs._OnChanged;
	ForceVolatile(true);
}

FVector2D SFFToggle::ComputeDesiredSize(float) const
{
	const FVector2D L = FFUi::Measure(Label.Get(FText::GetEmpty()).ToString(), FFUi::Font(FFUi::Mm(3.0f)));
	return FVector2D(L.X + FFUi::Mm(16.0f), FFUi::Mm(10.0f));
}

int32 SFFToggle::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
                         int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const FFUi::FPainter P(AllottedGeometry, OutDrawElements, LayerId);
	const FVector2D Size = FVector2D(AllottedGeometry.GetLocalSize());
	const bool bOn = Value.Get(false);
	const float Hover = PressVisual();
	if (Hover > 0.0f || bNavFocus)
	{
		P.RoundRect(FVector2D(0, 0), Size, FLinearColor(1, 1, 1, 0.05f * FMath::Max(Hover, bNavFocus ? 1.0f : 0.0f)),
		            bNavFocus ? FFUi::WithAlpha(FFUi::Accent, 0.9f) : FLinearColor::Transparent, bNavFocus ? FMath::Max(2.0f, FFUi::Mm(0.3f)) : 0.0f, FFUi::Mm(1.6f));
	}
	const FSlateFontInfo F = FFUi::Font(FFUi::Mm(3.0f));
	const FString L = Label.Get(FText::GetEmpty()).ToString();
	const FVector2D M = FFUi::Measure(L, F);
	P.Text(L, FVector2D(FFUi::Mm(2.0f), (Size.Y - M.Y) * 0.5), F, FFUi::Ink);
	// Pill switch.
	const double SW = FFUi::Mm(8.4f), SH = FFUi::Mm(4.6f);
	const FVector2D Pos(Size.X - SW - FFUi::Mm(2.0f), (Size.Y - SH) * 0.5);
	P.Rect(Pos, FVector2D(SW, SH), bOn ? FFUi::WithAlpha(FFUi::Accent, 0.85f) : FLinearColor(1, 1, 1, 0.2f), float(SH * 0.5));
	const double KR = SH * 0.5 * 0.74;
	const FVector2D K(bOn ? Pos.X + SW - SH * 0.5 : Pos.X + SH * 0.5, Pos.Y + SH * 0.5);
	P.Circle(K, float(KR), bOn ? FLinearColor::White : FLinearColor(1, 1, 1, 0.8f));
	return LayerId + 1;
}

void SFFToggle::OnPressEnd(const FGeometry&, const FVector2D&, bool bInside)
{
	if (bInside)
	{
		OnChanged.ExecuteIfBound(!Value.Get(false));
	}
}

// ---------------------------------------------------------------------------------------------- SFFSlider

void SFFSlider::Construct(const FArguments& InArgs)
{
	Label = InArgs._Label;
	Value = InArgs._Value;
	Min = InArgs._Min;
	Max = FMath::Max(InArgs._Max, InArgs._Min + 1e-4f);
	Step = FMath::Max(InArgs._Step, 0.0f);
	ValueText = InArgs._ValueText;
	OnChanged = InArgs._OnChanged;
	ForceVolatile(true);
}

FVector2D SFFSlider::ComputeDesiredSize(float) const
{
	const FVector2D L = FFUi::Measure(Label.Get(FText::GetEmpty()).ToString(), FFUi::Font(FFUi::Mm(3.0f)));
	return FVector2D(FMath::Max(L.X + FFUi::Mm(16.0f), FFUi::Mm(50.0f)), FFUi::Mm(13.5f));
}

void SFFSlider::TrackRect(const FGeometry& G, float& X0, float& X1, float& Y) const
{
	const FVector2D Size = FVector2D(G.GetLocalSize());
	X0 = FFUi::Mm(4.6f);
	X1 = FMath::Max(X0 + 1.0f, float(Size.X) - FFUi::Mm(4.6f));
	Y = float(Size.Y) * 0.70f;
}

void SFFSlider::SetFromX(const FGeometry& MyGeometry, float X)
{
	float X0, X1, Y;
	TrackRect(MyGeometry, X0, X1, Y);
	const float T = FMath::Clamp((X - X0) / (X1 - X0), 0.0f, 1.0f);
	float V = FMath::Lerp(Min, Max, T);
	if (Step > 0.0f)
	{
		V = Min + FMath::RoundToFloat((V - Min) / Step) * Step;
	}
	V = FMath::Clamp(V, Min, Max);
	if (!FMath::IsNearlyEqual(V, Value.Get(Min)))
	{
		OnChanged.ExecuteIfBound(V);
	}
}

void SFFSlider::OnPressStart(const FGeometry& MyGeometry, const FVector2D& Local)
{
	SetFromX(MyGeometry, float(Local.X));
}

void SFFSlider::OnPressMove(const FGeometry& MyGeometry, const FVector2D& Local)
{
	SetFromX(MyGeometry, float(Local.X));
}

void SFFSlider::NavAdjust(int32 Dir)
{
	const float S = Step > 0.0f ? Step : (Max - Min) / 20.0f;
	const float V = FMath::Clamp(Value.Get(Min) + S * float(FMath::Sign(Dir)), Min, Max);
	OnChanged.ExecuteIfBound(V);
}

int32 SFFSlider::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
                         int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const FFUi::FPainter P(AllottedGeometry, OutDrawElements, LayerId);
	const FVector2D Size = FVector2D(AllottedGeometry.GetLocalSize());
	if (bNavFocus)
	{
		P.RoundRect(FVector2D(0, 0), Size, FLinearColor(1, 1, 1, 0.04f), FFUi::WithAlpha(FFUi::Accent, 0.9f), FMath::Max(2.0f, FFUi::Mm(0.3f)), FFUi::Mm(1.6f));
	}
	const FSlateFontInfo F = FFUi::Font(FFUi::Mm(3.0f));
	P.Text(Label.Get(FText::GetEmpty()).ToString(), FVector2D(FFUi::Mm(2.0f), Size.Y * 0.08), F, FFUi::Ink);
	const FString VT = ValueText.IsSet() ? ValueText.Get().ToString() : FString::Printf(TEXT("%.2f"), Value.Get(Min));
	const FVector2D VM = FFUi::Measure(VT, F);
	P.Text(VT, FVector2D(Size.X - VM.X - FFUi::Mm(2.0f), Size.Y * 0.08), F, FFUi::InkDim);

	float X0, X1, Y;
	TrackRect(AllottedGeometry, X0, X1, Y);
	const float T = FMath::Clamp((Value.Get(Min) - Min) / (Max - Min), 0.0f, 1.0f);
	const float TH = FFUi::Mm(1.1f);
	P.Rect(FVector2D(X0, Y - TH * 0.5f), FVector2D(X1 - X0, TH), FLinearColor(1, 1, 1, 0.16f), TH * 0.5f);
	P.Rect(FVector2D(X0, Y - TH * 0.5f), FVector2D((X1 - X0) * T, TH), FFUi::WithAlpha(FFUi::Accent, 0.85f), TH * 0.5f);
	const float KR = FFUi::Mm(2.6f) * (1.0f + 0.12f * PressVisual());
	const FVector2D K(FMath::Lerp(X0, X1, T), Y);
	P.Circle(K, KR + FFUi::Mm(0.3f), FLinearColor(0, 0, 0, 0.25f));
	P.Circle(K, KR, bPressed ? FFUi::Accent : FLinearColor::White);
	return LayerId + 1;
}

// ---------------------------------------------------------------------------------------------- SFFChoice

void SFFChoice::Construct(const FArguments& InArgs)
{
	Label = InArgs._Label;
	Options = InArgs._Options;
	Selected = InArgs._Selected;
	Accent = InArgs._Accent;
	OnChanged = InArgs._OnChanged;
	ForceVolatile(true);
}

FVector2D SFFChoice::ComputeDesiredSize(float) const
{
	const FSlateFontInfo F = FFUi::Font(FFUi::Mm(2.8f), true);
	double W = 0.0;
	for (const FString& O : Options)
	{
		W += FFUi::Measure(O, F).X + FFUi::Mm(5.0f);
	}
	const bool bLabel = !Label.Get(FText::GetEmpty()).IsEmpty();
	return FVector2D(FMath::Max(W, double(FFUi::Mm(40.0f))), FFUi::Mm(bLabel ? 16.5f : 10.5f));
}

void SFFChoice::OptionRects(const FGeometry& G, TArray<FVector4f>& Out) const
{
	Out.Reset();
	const FVector2D Size = FVector2D(G.GetLocalSize());
	const int32 N = Options.Num();
	if (N <= 0)
	{
		return;
	}
	const bool bLabel = !Label.Get(FText::GetEmpty()).IsEmpty();
	const float Top = bLabel ? FFUi::Mm(6.0f) : FFUi::Mm(0.75f);
	const float H = float(Size.Y) - Top - FFUi::Mm(0.75f);
	const float Gap = FFUi::Mm(1.0f);
	const float X0 = FFUi::Mm(1.0f);
	const float W = (float(Size.X) - X0 * 2.0f - Gap * float(N - 1)) / float(N);
	for (int32 i = 0; i < N; ++i)
	{
		Out.Add(FVector4f(X0 + float(i) * (W + Gap), Top, W, H));
	}
}

int32 SFFChoice::HitOption(const FGeometry& G, const FVector2D& Local) const
{
	TArray<FVector4f> Rects;
	OptionRects(G, Rects);
	for (int32 i = 0; i < Rects.Num(); ++i)
	{
		const FVector4f& R = Rects[i];
		// Generous vertical slop: the whole row height counts.
		if (Local.X >= R.X - FFUi::Mm(0.5f) && Local.X <= R.X + R.Z + FFUi::Mm(0.5f))
		{
			return i;
		}
	}
	return INDEX_NONE;
}

void SFFChoice::OnPressEnd(const FGeometry& MyGeometry, const FVector2D& Local, bool bInside)
{
	if (!bInside)
	{
		return;
	}
	const int32 Hit = HitOption(MyGeometry, Local);
	if (Hit != INDEX_NONE && Hit != Selected.Get(-1))
	{
		OnChanged.ExecuteIfBound(Hit);
	}
}

void SFFChoice::NavAdjust(int32 Dir)
{
	const int32 N = Options.Num();
	if (N <= 0)
	{
		return;
	}
	const int32 Cur = FMath::Clamp(Selected.Get(0), 0, N - 1);
	OnChanged.ExecuteIfBound((Cur + (Dir >= 0 ? 1 : -1) + N) % N);
}

int32 SFFChoice::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
                         int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const FFUi::FPainter P(AllottedGeometry, OutDrawElements, LayerId);
	const FVector2D Size = FVector2D(AllottedGeometry.GetLocalSize());
	if (bNavFocus)
	{
		P.RoundRect(FVector2D(0, 0), Size, FLinearColor(1, 1, 1, 0.04f), FFUi::WithAlpha(FFUi::Accent, 0.9f), FMath::Max(2.0f, FFUi::Mm(0.3f)), FFUi::Mm(1.6f));
	}
	const FString L = Label.Get(FText::GetEmpty()).ToString();
	if (!L.IsEmpty())
	{
		P.Text(L, FVector2D(FFUi::Mm(1.4f), FFUi::Mm(0.6f)), FFUi::Font(FFUi::Mm(2.9f)), FFUi::InkDim);
	}
	const FLinearColor Acc = Accent.Get(FFUi::Accent);
	const int32 Sel = Selected.Get(-1);
	TArray<FVector4f> Rects;
	OptionRects(AllottedGeometry, Rects);
	const FSlateFontInfo F = FFUi::Font(FFUi::Mm(2.8f), true);
	for (int32 i = 0; i < Rects.Num(); ++i)
	{
		const FVector4f& R = Rects[i];
		const bool bSel = i == Sel;
		const FLinearColor Fill = bSel ? FFUi::WithAlpha(Acc, 0.62f) : FFUi::SurfaceRaised;
		const FLinearColor Border = bSel ? FFUi::WithAlpha(Acc, 1.0f) : FFUi::Line;
		P.RoundRect(FVector2D(R.X, R.Y), FVector2D(R.Z, R.W), Fill, Border, bSel ? FMath::Max(1.5f, FFUi::Mm(0.22f)) : 1.0f, R.W * 0.5f);
		P.TextCentered(Options[i], FVector2D(R.X + R.Z * 0.5f, R.Y + R.W * 0.5f), F, bSel ? FLinearColor::White : FFUi::Ink);
	}
	return LayerId + 1;
}

// ---------------------------------------------------------------------------------------------- brushes

const FSlateBrush* FFUiBrushes::Card()
{
	static FSlateRoundedBoxBrush B(FFUi::Surface, 14.0f, FLinearColor(1, 1, 1, 0.10f), 1.0f);
	const float R = FFUi::Mm(2.4f);
	B.OutlineSettings.CornerRadii = FVector4(R, R, R, R);
	return &B;
}

const FSlateBrush* FFUiBrushes::CardRaised()
{
	static FSlateRoundedBoxBrush B(FFUi::SurfaceRaised, 10.0f, FFUi::Line, 1.0f);
	const float R = FFUi::Mm(1.6f);
	B.OutlineSettings.CornerRadii = FVector4(R, R, R, R);
	return &B;
}

const FSlateBrush* FFUiBrushes::Dim()
{
	static const FSlateRoundedBoxBrush B(FLinearColor(0.0f, 0.0f, 0.0f, 0.55f), 0.0f, FLinearColor::Transparent, 0.0f);
	return &B;
}

const FSlateBrush* FFUiBrushes::None()
{
	static const FSlateNoResource B;
	return &B;
}
