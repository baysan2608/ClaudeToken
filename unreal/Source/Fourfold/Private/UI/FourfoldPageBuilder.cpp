// Fourfold - page builder (see FourfoldPageBuilder.h).
#include "UI/FourfoldPageBuilder.h"

#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Text/STextBlock.h"

FFFPageBuilder::FFFPageBuilder(const TSharedRef<SVerticalBox>& InBox, TArray<TSharedRef<SFFPressable>>& InFocus, FCueFn InCue)
	: Box(InBox)
	, Focus(InFocus)
	, CueFn(MoveTemp(InCue))
{
}

void FFFPageBuilder::Cue(FName Name) const
{
	if (CueFn)
	{
		CueFn(Name);
	}
}

FSimpleDelegate FFFPageBuilder::WithCue(const FSimpleDelegate& D, FName CueName) const
{
	FCueFn C = CueFn;
	return FSimpleDelegate::CreateLambda([D, C, CueName]() {
		if (C)
		{
			C(CueName);
		}
		D.ExecuteIfBound();
	});
}

void FFFPageBuilder::Add(const TSharedRef<SWidget>& Widget, float PadMm)
{
	Box->AddSlot().AutoHeight().Padding(FMargin(0.0f, FFUi::Mm(PadMm) * 0.5f))[Widget];
}

void FFFPageBuilder::Title(const TAttribute<FText>& Text, float Mm)
{
	Box->AddSlot().AutoHeight().Padding(FMargin(FFUi::Mm(0.6f), FFUi::Mm(0.4f), 0.0f, FFUi::Mm(1.2f)))
	[
		SNew(STextBlock).Text(Text).Font(FFUi::Font(FFUi::Mm(Mm), true)).ColorAndOpacity(FSlateColor(FFUi::Ink))
	];
}

void FFFPageBuilder::Section(const TAttribute<FText>& Text)
{
	Box->AddSlot().AutoHeight().Padding(FMargin(FFUi::Mm(0.6f), FFUi::Mm(2.4f), 0.0f, FFUi::Mm(0.6f)))
	[
		SNew(STextBlock).Text(Text).Font(FFUi::Font(FFUi::Mm(2.5f), true)).ColorAndOpacity(FSlateColor(FFUi::WithAlpha(FFUi::Accent, 0.9f)))
	];
}

void FFFPageBuilder::Label(const TAttribute<FText>& Text, float Mm, bool bDim, const TAttribute<FSlateColor>& Color)
{
	TAttribute<FSlateColor> Col = Color;
	if (!Col.IsSet())
	{
		Col = FSlateColor(bDim ? FFUi::InkDim : FFUi::Ink);
	}
	Box->AddSlot().AutoHeight().Padding(FMargin(FFUi::Mm(0.6f), FFUi::Mm(0.3f)))
	[
		SNew(STextBlock).Text(Text).Font(FFUi::Font(FFUi::Mm(Mm))).ColorAndOpacity(Col).AutoWrapText(true)
	];
}

void FFFPageBuilder::Spacer(float Mm)
{
	Box->AddSlot().AutoHeight()[SNew(SSpacer).Size(FVector2D(1.0, FFUi::Mm(Mm)))];
}

TSharedRef<SFFButton> FFFPageBuilder::Button(const TAttribute<FText>& Text, const FSimpleDelegate& OnClick, bool bPrimary, const TAttribute<FText>& Sub,
                                             const TAttribute<FLinearColor>& Accent, const TAttribute<bool>& bEnabled, const TAttribute<bool>& bSelected)
{
	TSharedRef<SFFButton> B = SNew(SFFButton)
		.Label(Text)
		.SubLabel(Sub)
		.bPrimary(bPrimary)
		.Accent(Accent)
		.bEnabled(bEnabled)
		.bSelected(bSelected)
		.OnClicked(WithCue(OnClick, FName(TEXT("ui_tap"))));
	Add(B);
	Focus.Add(B);
	return B;
}

TSharedRef<SFFToggle> FFFPageBuilder::Toggle(const TAttribute<FText>& Text, const TAttribute<bool>& Value, const FFFOnBool& OnChanged)
{
	FCueFn C = CueFn;
	TSharedRef<SFFToggle> W = SNew(SFFToggle)
		.Label(Text)
		.Value(Value)
		.OnChanged(FFFOnBool::CreateLambda([OnChanged, C](bool bOn) {
			if (C)
			{
				C(FName(TEXT("ui_toggle")));
			}
			OnChanged.ExecuteIfBound(bOn);
		}));
	Add(W, 0.3f);
	Focus.Add(W);
	return W;
}

TSharedRef<SFFSlider> FFFPageBuilder::Slider(const TAttribute<FText>& Text, const TAttribute<float>& Value, float Min, float Max, float Step,
                                             const TAttribute<FText>& ValueText, const FFFOnFloat& OnChanged)
{
	TSharedRef<SFFSlider> W = SNew(SFFSlider)
		.Label(Text)
		.Value(Value)
		.Min(Min)
		.Max(Max)
		.Step(Step)
		.ValueText(ValueText)
		.OnChanged(OnChanged);
	Add(W, 0.3f);
	Focus.Add(W);
	return W;
}

TSharedRef<SFFChoice> FFFPageBuilder::Choice(const TAttribute<FText>& Text, const TArray<FString>& Options, const TAttribute<int32>& Selected,
                                             const FFFOnInt& OnChanged, const TAttribute<FLinearColor>& Accent)
{
	FCueFn C = CueFn;
	TSharedRef<SFFChoice> W = SNew(SFFChoice)
		.Label(Text)
		.Options(Options)
		.Selected(Selected)
		.Accent(Accent)
		.OnChanged(FFFOnInt::CreateLambda([OnChanged, C](int32 I) {
			if (C)
			{
				C(FName(TEXT("ui_select")));
			}
			OnChanged.ExecuteIfBound(I);
		}));
	Add(W, 0.4f);
	Focus.Add(W);
	return W;
}

TSharedRef<SHorizontalBox> FFFPageBuilder::Row()
{
	TSharedRef<SHorizontalBox> R = SNew(SHorizontalBox);
	Add(R, 0.5f);
	return R;
}

TSharedRef<SFFButton> FFFPageBuilder::RowButton(const TSharedRef<SHorizontalBox>& InRow, const TAttribute<FText>& Text, const FSimpleDelegate& OnClick,
                                                bool bPrimary, const TAttribute<FLinearColor>& Accent, const TAttribute<bool>& bSelected)
{
	TSharedRef<SFFButton> B = SNew(SFFButton)
		.Label(Text)
		.bPrimary(bPrimary)
		.Accent(Accent)
		.bSelected(bSelected)
		.bCentered(true)
		.OnClicked(WithCue(OnClick, FName(TEXT("ui_tap"))));
	const float LeftPad = InRow->NumSlots() > 0 ? FFUi::Mm(0.5f) : 0.0f;
	InRow->AddSlot().FillWidth(1.0f).Padding(FMargin(LeftPad, 0.0f, 0.0f, 0.0f))[B];
	Focus.Add(B);
	return B;
}

TSharedRef<SWrapBox> FFFPageBuilder::Flow()
{
	TSharedRef<SWrapBox> W = SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(FFUi::Mm(0.8f), FFUi::Mm(0.8f)));
	Add(W, 0.5f);
	return W;
}

TSharedRef<SFFButton> FFFPageBuilder::FlowButton(const TSharedRef<SWrapBox>& InFlow, const TAttribute<FText>& Text, const FSimpleDelegate& OnClick,
                                                 const TAttribute<bool>& bSelected, const TAttribute<FLinearColor>& Accent, float MinWidthMm)
{
	TSharedRef<SFFButton> B = SNew(SFFButton)
		.Label(Text)
		.Accent(Accent)
		.bSelected(bSelected)
		.bCentered(true)
		.HeightMm(9.0f)
		.FontMm(2.5f)
		.OnClicked(WithCue(OnClick, FName(TEXT("ui_select"))));
	InFlow->AddSlot()
	[
		SNew(SBox).MinDesiredWidth(MinWidthMm > 0.0f ? FOptionalSize(FFUi::Mm(MinWidthMm)) : FOptionalSize())[B]
	];
	Focus.Add(B);
	return B;
}
