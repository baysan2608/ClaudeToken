// Fourfold game logic island - touch HUD geometry (port of game/ui/touch_layout.gd).
// Where the buttons, element chips, cancel zone, pause button and stick ghost sit, and which control a touch-down
// belongs to. Authored in millimetres from the bottom-right usable corner for a right-handed player, converted with
// ppm (viewport units per mm); left-handed play mirrors x. Viewport units = Slate local units of the overlay.
#pragma once

#include "FFGMath.h"
#include "ff/Types.h"

#include <array>
#include <string>

namespace ffg {

struct Rect {
	float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
	Rect() = default;
	Rect(float ax, float ay, float aw, float ah) : x(ax), y(ay), w(aw), h(ah) {}
	float Right() const { return x + w; }
	float Bottom() const { return y + h; }
	Vec2 Center() const { return Vec2(x + w * 0.5f, y + h * 0.5f); }
	bool Has(Vec2 p) const { return p.x >= x && p.x < x + w && p.y >= y && p.y < y + h; }
	Rect Grow(float m) const { return Rect(x - m, y - m, w + 2.0f * m, h + 2.0f * m); }
	Rect Merge(const Rect& o) const {
		const float nx = std::min(x, o.x), ny = std::min(y, o.y);
		return Rect(nx, ny, std::max(Right(), o.Right()) - nx, std::max(Bottom(), o.Bottom()) - ny);
	}
};

// Safe-area insets in viewport units.
struct Insets {
	float left = 0.0f, top = 0.0f, right = 0.0f, bottom = 0.0f;
};

enum class TouchId : int { Attack = 0, Guard, Evade, Tech, Target, Pause, Elem0, Elem1, Elem2, Elem3, Cancel, Count };
inline constexpr int kTouchCount = static_cast<int>(TouchId::Count);
inline constexpr int kTouchNone = -1;
inline constexpr int TI(TouchId id) { return static_cast<int>(id); }

struct PetalAnchor {
	Vec2 pos;
	int align = 0;   // -1 the pill grows left of pos, 0 centred, 1 grows right
};

class TouchLayout {
public:
	// [dx, dy, diameter] in mm, measured inward / upward from the usable corner (ATTACK, GUARD, EVADE, TECH, TARGET).
	static constexpr float kClusterMm[5][3] = {
		{17.0f, 18.0f, 17.0f},
		{40.0f, 10.5f, 13.5f},
		{12.0f, 40.0f, 12.5f},
		{36.0f, 34.0f, 14.0f},
		{23.0f, 50.0f, 9.0f},
	};
	static constexpr float kPauseDiameterMm = 9.5f;
	static constexpr float kPauseInsetMm = 7.0f;
	static constexpr float kChipDiameterMm = 8.5f;
	static constexpr float kChipArcRadiusMm = 20.0f;
	static constexpr float kChipArcStartDeg = 185.0f;
	static constexpr float kChipArcStepDeg = 29.0f;
	static constexpr float kCancelDistanceMm = 28.0f;
	static constexpr float kCancelRadiusMm = 7.0f;
	static constexpr float kStickGhostMmX = 25.0f;
	static constexpr float kStickGhostMmY = 24.0f;
	static constexpr float kHitSlop = 1.12f;
	static constexpr float kMinHitMm = 5.0f;
	static constexpr float kRingPetalMmW = 19.0f;
	static constexpr float kRingPetalMmH = 8.0f;
	static constexpr float kRingGapMm = 1.4f;
	static constexpr float kRingChipGapMm = 3.0f;

	// inputs
	Vec2 viewport_size{1280.0f, 720.0f};
	Insets insets;
	float ppm = 8.0f;
	float control_scale = 1.0f;
	std::string preset = "default";
	bool left_handed = false;

	// outputs
	Vec2 centers[kTouchCount] = {};
	float radii[kTouchCount] = {};
	float hit_radii[kTouchCount] = {};
	Vec2 stick_ghost;
	float stick_radius = 80.0f;
	float aim_radius = 100.0f;   // technique drag that maps to a unit aim vector
	float split_x = 640.0f;
	Rect usable;
	float margin = 24.0f;

	void Configure(Vec2 vp_size, const Insets& new_insets, float new_ppm, float scale, const std::string& new_preset, bool lefty);
	void Recompute();
	bool IsStickSide(Vec2 pos) const { return left_handed ? pos.x > split_x : pos.x < split_x; }
	// Control a touch-down lands on (TouchId as int) or kTouchNone; nearest by distance / hit radius; never Cancel.
	int HitTest(Vec2 pos) const;
	bool InCancelZone(Vec2 pos) const;
	// The four sub-element petals (index = sub) of the ring opened from an element chip.
	std::array<Rect, 4> RingRects() const;
	int RingHit(Vec2 pos) const;
	// The petal a finger that started on element chip `element` aims at: the petal under it, else - once it has slid
	// clearly off the chip toward the petal column - the petal nearest its height (a quick flick need not land on it).
	int RingAim(int element, Vec2 pos) const;
	PetalAnchor AttackPetalAnchor(ff::Gesture which) const;
	PetalAnchor GuardPetalAnchor(ff::Gesture which) const;

private:
	void Place(Vec2 anchor, float kp, float ks);
};

}  // namespace ffg
