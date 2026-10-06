// Fourfold - Slate UI toolkit: palette, fonts in millimetres, paint helpers (circles, rings, arcs, pills, outlined
// text, the element / button glyphs of game/ui/ui_style.gd) and a few touch-first widgets (button, toggle, slider,
// segmented choice) sized in physical millimetres (min touch target 9 mm). Engine default font (Roboto).
#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"
#include "Layout/Margin.h"
#include "Layout/Geometry.h"
#include "Rendering/DrawElements.h"
#include "Styling/SlateBrush.h"
#include "Widgets/SLeafWidget.h"

namespace FFUi
{
	/** Slate local units per millimetre of physical screen (UpdateMetrics, once per frame from the UI root). */
	extern float Ppm;
	inline float Mm(float Millimetres) { return Millimetres * Ppm; }
	/** Safe-area insets (notch, home indicator, rounded corners) in the root's local units. */
	extern FMargin SafeLocal;
	/** Recomputes Ppm (physical density, ffg::PxPerMm) and SafeLocal (FSlateApplication::GetSafeZoneSize) for the
	 *  full-viewport root geometry. Returns true when either changed. */
	bool UpdateMetrics(const FGeometry& RootGeometry);

	FLinearColor SRGB(float R, float G, float B, float A = 1.0f);
	FLinearColor ElementColor(int32 Element);
	FLinearColor WithAlpha(const FLinearColor& C, float A);
	extern const FLinearColor Ink;
	extern const FLinearColor InkDim;
	extern const FLinearColor Surface;
	extern const FLinearColor SurfaceRaised;
	extern const FLinearColor SurfaceHover;
	extern const FLinearColor Line;
	extern const FLinearColor Accent;
	extern const FLinearColor Danger;
	extern const FLinearColor Gold;

	/** Engine default font sized so a line is ~`PxHeight` local units tall. */
	FSlateFontInfo Font(float PxHeight, bool bBold = false, int32 OutlinePx = 0, const FLinearColor& OutlineColor = FLinearColor(0, 0, 0, 0.6f));
	FVector2D Measure(const FString& Text, const FSlateFontInfo& InFont);

	enum class EGlyph : uint8 { Earth, Water, Fire, Air, Attack, Guard, Evade, Target, Pause, Cancel };

	/** Paint helper bound to one OnPaint call (local coordinates of the geometry). */
	struct FPainter
	{
		const FGeometry& Geo;
		FSlateWindowElementList& Out;
		int32 Layer;

		FPainter(const FGeometry& InGeo, FSlateWindowElementList& InOut, int32 InLayer) : Geo(InGeo), Out(InOut), Layer(InLayer) {}

		void Circle(const FVector2D& C, float R, const FLinearColor& Fill) const;
		void Ring(const FVector2D& C, float R, float Width, const FLinearColor& Col) const;
		void Arc(const FVector2D& C, float R, float A0, float A1, float Width, const FLinearColor& Col, int32 Segments = 40) const;
		void Polyline(const TArray<FVector2f>& Points, float Width, const FLinearColor& Col) const;
		void Line(const FVector2D& A, const FVector2D& B, float Width, const FLinearColor& Col) const;
		void Rect(const FVector2D& Pos, const FVector2D& Size, const FLinearColor& Fill, float Radius = 0.0f) const;
		void RoundRect(const FVector2D& Pos, const FVector2D& Size, const FLinearColor& Fill, const FLinearColor& Border, float BorderWidth,
		               float Radius) const;
		void Triangle(const FVector2D& A, const FVector2D& B, const FVector2D& C, const FLinearColor& Col) const;
		/** Text with its top-left at Pos. */
		void Text(const FString& S, const FVector2D& Pos, const FSlateFontInfo& InFont, const FLinearColor& Col) const;
		/** Text centred on (X, baseline-centre Y). */
		void TextCentered(const FString& S, const FVector2D& Center, const FSlateFontInfo& InFont, const FLinearColor& Col) const;
		void Glyph(EGlyph G, const FVector2D& C, float R, const FLinearColor& Col, float Width) const;
		/** Glyph with a soft dark halo so it stays legible on bright scenes. */
		void GlyphHalo(EGlyph G, const FVector2D& C, float R, const FLinearColor& Col, float Width) const;
	};
}

DECLARE_DELEGATE_OneParam(FFFOnBool, bool);
DECLARE_DELEGATE_OneParam(FFFOnFloat, float);
DECLARE_DELEGATE_OneParam(FFFOnInt, int32);

/** Base for the touch-first widgets: press tracking that cooperates with SScrollBox (a drag steals the press). */
class SFFPressable : public SLeafWidget
{
public:
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override;
	virtual void OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override;
	virtual bool SupportsKeyboardFocus() const override { return false; }

	/** Gamepad / keyboard navigation highlight (set by the menu root). */
	bool bNavFocus = false;
	/** Activate (Enter / A) and adjust (left / right) from the keyboard or a pad. */
	virtual void NavActivate() {}
	virtual void NavAdjust(int32 Dir) {}
	bool IsPressable() const { return bEnabled.Get(true); }

protected:
	virtual void OnPressStart(const FGeometry& MyGeometry, const FVector2D& Local) {}
	virtual void OnPressMove(const FGeometry& MyGeometry, const FVector2D& Local) {}
	virtual void OnPressEnd(const FGeometry& MyGeometry, const FVector2D& Local, bool bInside) {}
	/** Dragging widgets (slider) capture a touch press; buttons leave touch moves to an enclosing scroll box. */
	virtual bool WantsTouchCapture() const { return false; }
	float PressVisual() const { return bPressed ? 1.0f : (bHovered ? 0.35f : 0.0f); }

	TAttribute<bool> bEnabled = true;
	bool bPressed = false;
	bool bHovered = false;
	bool bPressIsTouch = false;
	FVector2D PressStartScreen = FVector2D::ZeroVector;
};

/** Rounded menu button: label (+ optional second line), accent edge, press / hover / nav-focus states. */
class SFFButton : public SFFPressable
{
public:
	SLATE_BEGIN_ARGS(SFFButton) : _HeightMm(10.5f), _FontMm(3.0f), _bPrimary(false), _bEnabled(true), _Accent(FFUi::Accent), _bCentered(false) {}
		SLATE_ATTRIBUTE(FText, Label)
		SLATE_ATTRIBUTE(FText, SubLabel)
		SLATE_ARGUMENT(float, HeightMm)
		SLATE_ARGUMENT(float, FontMm)
		SLATE_ARGUMENT(bool, bPrimary)
		SLATE_ATTRIBUTE(bool, bEnabled)
		SLATE_ATTRIBUTE(FLinearColor, Accent)
		SLATE_ARGUMENT(bool, bCentered)
		SLATE_ATTRIBUTE(bool, bSelected)
		SLATE_EVENT(FSimpleDelegate, OnClicked)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual FVector2D ComputeDesiredSize(float) const override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
	                      int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual void NavActivate() override { OnClicked.ExecuteIfBound(); }

protected:
	virtual void OnPressEnd(const FGeometry& MyGeometry, const FVector2D& Local, bool bInside) override;

	TAttribute<FText> Label, SubLabel;
	TAttribute<FLinearColor> Accent;
	TAttribute<bool> bSelected;
	float HeightMm = 10.5f;
	float FontMm = 3.0f;
	bool bPrimary = false;
	bool bCentered = false;
	FSimpleDelegate OnClicked;
};

/** Label + switch. */
class SFFToggle : public SFFPressable
{
public:
	SLATE_BEGIN_ARGS(SFFToggle) {}
		SLATE_ATTRIBUTE(FText, Label)
		SLATE_ATTRIBUTE(bool, Value)
		SLATE_EVENT(FFFOnBool, OnChanged)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual FVector2D ComputeDesiredSize(float) const override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
	                      int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual void NavActivate() override { OnChanged.ExecuteIfBound(!Value.Get(false)); }
	virtual void NavAdjust(int32 Dir) override { OnChanged.ExecuteIfBound(Dir > 0); }

protected:
	virtual void OnPressEnd(const FGeometry& MyGeometry, const FVector2D& Local, bool bInside) override;
	TAttribute<FText> Label;
	TAttribute<bool> Value;
	FFFOnBool OnChanged;
};

/** Label, value text and a draggable track. */
class SFFSlider : public SFFPressable
{
public:
	SLATE_BEGIN_ARGS(SFFSlider) : _Min(0.0f), _Max(1.0f), _Step(0.05f) {}
		SLATE_ATTRIBUTE(FText, Label)
		SLATE_ATTRIBUTE(float, Value)
		SLATE_ARGUMENT(float, Min)
		SLATE_ARGUMENT(float, Max)
		SLATE_ARGUMENT(float, Step)
		SLATE_ATTRIBUTE(FText, ValueText)
		SLATE_EVENT(FFFOnFloat, OnChanged)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual FVector2D ComputeDesiredSize(float) const override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
	                      int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual void NavAdjust(int32 Dir) override;

protected:
	virtual void OnPressStart(const FGeometry& MyGeometry, const FVector2D& Local) override;
	virtual void OnPressMove(const FGeometry& MyGeometry, const FVector2D& Local) override;
	virtual bool WantsTouchCapture() const override { return true; }
	void SetFromX(const FGeometry& MyGeometry, float X);
	void TrackRect(const FGeometry& G, float& X0, float& X1, float& Y) const;
	TAttribute<FText> Label, ValueText;
	TAttribute<float> Value;
	float Min = 0.0f, Max = 1.0f, Step = 0.05f;
	FFFOnFloat OnChanged;
};

/** Label + segmented pills (one choice of N). */
class SFFChoice : public SFFPressable
{
public:
	SLATE_BEGIN_ARGS(SFFChoice) {}
		SLATE_ATTRIBUTE(FText, Label)
		SLATE_ARGUMENT(TArray<FString>, Options)
		SLATE_ATTRIBUTE(int32, Selected)
		SLATE_ATTRIBUTE(FLinearColor, Accent)
		SLATE_EVENT(FFFOnInt, OnChanged)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual FVector2D ComputeDesiredSize(float) const override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
	                      int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual void NavAdjust(int32 Dir) override;
	virtual void NavActivate() override { NavAdjust(1); }

protected:
	virtual void OnPressEnd(const FGeometry& MyGeometry, const FVector2D& Local, bool bInside) override;
	int32 HitOption(const FGeometry& G, const FVector2D& Local) const;
	void OptionRects(const FGeometry& G, TArray<FVector4f>& Out) const;
	TAttribute<FText> Label;
	TArray<FString> Options;
	TAttribute<int32> Selected;
	TAttribute<FLinearColor> Accent;
	FFFOnInt OnChanged;
};

/** Shared Slate brushes for containers (cards, dim backdrop); corner radii follow FFUi::Ppm on every call. */
struct FFUiBrushes
{
	static const FSlateBrush* Card();
	static const FSlateBrush* CardRaised();
	static const FSlateBrush* Dim();
	static const FSlateBrush* None();
};
