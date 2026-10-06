// Fourfold - the one widget the player controller adds to the game viewport: HUD (bottom), touch overlay, menus and
// the Lab panel (top), plus a full-screen fade. It owns the physical-size metrics (FFUi::UpdateMetrics) and asks the
// menus / Lab panel to rebuild when the millimetre scale or the safe area changes (rotation, window resize).
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SFourfoldHud;
class SFourfoldTouchOverlay;
class SFourfoldMenus;
class SFourfoldLabPanel;
struct FFourfoldMenuHost;
struct FFourfoldLabHost;

class SFourfoldUiRoot : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SFourfoldUiRoot) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const FFourfoldMenuHost& MenuHost, const FFourfoldLabHost& LabHost);

	TSharedRef<SFourfoldHud> GetHud() const { return Hud.ToSharedRef(); }
	TSharedRef<SFourfoldTouchOverlay> GetTouch() const { return Touch.ToSharedRef(); }
	TSharedRef<SFourfoldMenus> GetMenus() const { return Menus.ToSharedRef(); }
	TSharedRef<SFourfoldLabPanel> GetLab() const { return Lab.ToSharedRef(); }

	/** Fade to / from black (scenario changes), real seconds. */
	void FadeTo(float Target, float Seconds);
	/** Start black and fade in (scenario loads hide the respawn pop). */
	void FadeIn(float Seconds)
	{
		Fade = 1.0f;
		FadeTo(0.0f, Seconds);
	}
	/** Local units per viewport pixel of the last frame (HUD projection). */
	float GetPixelToLocal() const { return PixelToLocal; }

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	TSharedPtr<SFourfoldHud> Hud;
	TSharedPtr<SFourfoldTouchOverlay> Touch;
	TSharedPtr<SFourfoldMenus> Menus;
	TSharedPtr<SFourfoldLabPanel> Lab;
	float Fade = 1.0f;
	float FadeTarget = 0.0f;
	float FadeRate = 2.0f;
	float PixelToLocal = 1.0f;
};
