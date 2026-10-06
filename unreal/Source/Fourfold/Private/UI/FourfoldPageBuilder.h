// Fourfold - small declarative helper that fills a vertical box with the touch-first widgets of FourfoldUi.h (titles,
// sections, wrapped labels, buttons, toggles, sliders, segmented choices, rows, flows) and records every pressable in
// the page's focus list for keyboard / gamepad navigation. Used by the menus and the Lab panel.
#pragma once

#include "CoreMinimal.h"
#include "UI/FourfoldUi.h"
#include "Widgets/SBoxPanel.h"

class SWrapBox;

class FFFPageBuilder
{
public:
	using FCueFn = TFunction<void(FName)>;

	FFFPageBuilder(const TSharedRef<SVerticalBox>& InBox, TArray<TSharedRef<SFFPressable>>& InFocus, FCueFn InCue);

	void Title(const TAttribute<FText>& Text, float Mm = 5.2f);
	void Section(const TAttribute<FText>& Text);
	void Label(const TAttribute<FText>& Text, float Mm = 2.6f, bool bDim = true, const TAttribute<FSlateColor>& Color = TAttribute<FSlateColor>());
	void Spacer(float Mm);
	void Add(const TSharedRef<SWidget>& Widget, float PadMm = 0.7f);

	TSharedRef<SFFButton> Button(const TAttribute<FText>& Text, const FSimpleDelegate& OnClick, bool bPrimary = false,
	                             const TAttribute<FText>& Sub = TAttribute<FText>(), const TAttribute<FLinearColor>& Accent = FFUi::Accent,
	                             const TAttribute<bool>& bEnabled = true, const TAttribute<bool>& bSelected = false);
	TSharedRef<SFFToggle> Toggle(const TAttribute<FText>& Text, const TAttribute<bool>& Value, const FFFOnBool& OnChanged);
	TSharedRef<SFFSlider> Slider(const TAttribute<FText>& Text, const TAttribute<float>& Value, float Min, float Max, float Step,
	                             const TAttribute<FText>& ValueText, const FFFOnFloat& OnChanged);
	TSharedRef<SFFChoice> Choice(const TAttribute<FText>& Text, const TArray<FString>& Options, const TAttribute<int32>& Selected, const FFFOnInt& OnChanged,
	                             const TAttribute<FLinearColor>& Accent = FFUi::Accent);

	/** A horizontal row of equally wide buttons. */
	TSharedRef<SHorizontalBox> Row();
	TSharedRef<SFFButton> RowButton(const TSharedRef<SHorizontalBox>& InRow, const TAttribute<FText>& Text, const FSimpleDelegate& OnClick,
	                                bool bPrimary = false, const TAttribute<FLinearColor>& Accent = FFUi::Accent, const TAttribute<bool>& bSelected = false);
	/** A wrapping flow of compact buttons (spawn entries, combos, pickers). */
	TSharedRef<SWrapBox> Flow();
	TSharedRef<SFFButton> FlowButton(const TSharedRef<SWrapBox>& InFlow, const TAttribute<FText>& Text, const FSimpleDelegate& OnClick,
	                                 const TAttribute<bool>& bSelected = false, const TAttribute<FLinearColor>& Accent = FFUi::Accent, float MinWidthMm = 0.0f);

	TSharedRef<SVerticalBox> GetBox() const { return Box; }
	FSimpleDelegate WithCue(const FSimpleDelegate& D, FName Cue) const;
	void Cue(FName Name) const;

private:
	TSharedRef<SVerticalBox> Box;
	TArray<TSharedRef<SFFPressable>>& Focus;
	FCueFn CueFn;
};

namespace FFUi
{
	inline FText T(const FString& S) { return FText::FromString(S); }
	inline FText T(const TCHAR* S) { return FText::FromString(FString(S)); }
	inline FText T(const std::string& S) { return FText::FromString(FString(UTF8_TO_TCHAR(S.c_str()))); }
	inline FString F(const std::string& S) { return FString(UTF8_TO_TCHAR(S.c_str())); }
	inline std::string U8(const FString& S) { return std::string(TCHAR_TO_UTF8(*S)); }
}
