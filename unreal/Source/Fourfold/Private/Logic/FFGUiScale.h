// Fourfold game logic island - physical-size heuristics for the touch UI (port of game/ui/ui_scale.gd) and the HUD ->
// touch-context bridge. Results are in Slate local units (the overlay's coordinate space).
#pragma once

#include "FFGDesktopInput.h"
#include "FFGTouchControls.h"
#include "ff/ViewModels.h"

namespace ffg {

struct UiScaleInput {
	float dpi = 0.0f;                  // physical pixels per inch (0 = unknown)
	bool dpi_is_real = false;          // the platform reported a real density (iOS device table)
	float local_per_pixel = 1.0f;      // Slate local units per physical pixel (1 / geometry scale x pixel ratio)
	float viewport_h_local = 720.0f;   // overlay height in local units
	bool mobile = false;
};

// Local units per millimetre: the real density when known; desktop dev builds are floored so a 96 dpi monitor does
// not shrink the HUD to a sliver; absurd reports are clamped to [0.0040, 0.0225] x viewport height.
inline float PxPerMm(const UiScaleInput& in) {
	constexpr float kFallbackDpi = 160.0f;
	const float h = std::max(in.viewport_h_local, 1.0f);
	const float dpi = in.dpi > 1.0f ? in.dpi : kFallbackDpi;
	float ppm = dpi / 25.4f * std::max(in.local_per_pixel, 1e-4f);
	if (!in.mobile || !in.dpi_is_real) ppm = std::max(ppm, 0.0108f * h);
	return Clampf(ppm, 0.0040f * h, 0.0225f * h);
}

// Safe-area insets that are implausibly large for a real notch / home indicator (a desktop window bigger than the
// screen) are discarded.
inline Insets SanitizeInsets(const Insets& in, Vec2 vp) {
	if (in.left < 0.0f || in.top < 0.0f || in.right < 0.0f || in.bottom < 0.0f) return Insets();
	if (in.left > vp.x * 0.12f || in.right > vp.x * 0.12f || in.top > vp.y * 0.15f || in.bottom > vp.y * 0.15f) return Insets();
	return in;
}

// HudModel (ff::Session::BuildHud) -> the touch overlay's context.
TouchContext TouchContextFromHud(const ff::HudModel& h);
// HudModel -> the keyboard / pad grammar's element / sub context.
void DesktopContextFromHud(const ff::HudModel& h, DesktopInput& d);

}  // namespace ffg
