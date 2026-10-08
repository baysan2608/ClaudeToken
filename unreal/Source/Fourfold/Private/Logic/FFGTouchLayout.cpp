// Fourfold game logic island - touch HUD geometry (see FFGTouchLayout.h).
#include "FFGTouchLayout.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ffg {

void TouchLayout::Configure(Vec2 vp_size, const Insets& new_insets, float new_ppm, float scale, const std::string& new_preset,
                            bool lefty) {
	viewport_size = vp_size;
	insets = new_insets;
	ppm = std::max(new_ppm, 0.5f);
	control_scale = scale;
	preset = new_preset;
	left_handed = lefty;
	Recompute();
}

void TouchLayout::Recompute() {
	const Vec2 vp = viewport_size;
	float spread = 1.0f;
	float size_mul = 1.0f;
	if (preset == "compact") {
		spread = 0.86f;
		size_mul = 0.92f;
	} else if (preset == "wide") {
		spread = 1.16f;
	}
	const float kp = ppm * control_scale * spread;
	const float ks = ppm * control_scale * size_mul;
	margin = 3.0f * ppm;
	// Work in the right-handed frame; mirror at the end.
	const float il = left_handed ? insets.right : insets.left;
	const float ir = left_handed ? insets.left : insets.right;
	usable = Rect(il, insets.top, vp.x - il - ir, vp.y - insets.top - insets.bottom);
	const Vec2 anchor(usable.Right() - margin, usable.Bottom() - margin);

	aim_radius = std::min(0.15f * vp.y, 16.0f * ppm);
	Place(anchor, kp, ks);
	// Shrink the cluster uniformly when it would cross the screen midline or the top edge.
	const float avail_x = anchor.x - (vp.x * 0.5f + margin * 0.4f);
	const float avail_y = anchor.y - (usable.y + margin * 0.5f);
	float fit = 1.0f;
	for (int pass = 0; pass < 3; ++pass) {
		float min_x = std::numeric_limits<float>::max();
		float min_y = std::numeric_limits<float>::max();
		for (int i = 0; i < kTouchCount; ++i) {
			if (i == TI(TouchId::Pause)) continue;
			min_x = std::min(min_x, centers[i].x - radii[i]);
			min_y = std::min(min_y, centers[i].y - radii[i]);
		}
		const float need = std::min(avail_x / std::max(anchor.x - min_x, 1.0f), avail_y / std::max(anchor.y - min_y, 1.0f));
		if (need >= 0.999f) break;
		fit = std::max(fit * need * 0.995f, 0.4f);
		Place(anchor, kp * fit, ks * fit);
	}

	// Pause: top corner.
	centers[TI(TouchId::Pause)] = Vec2(usable.Right() - kPauseInsetMm * ppm, usable.y + kPauseInsetMm * ppm);
	radii[TI(TouchId::Pause)] = kPauseDiameterMm * 0.5f * ppm * std::min(control_scale, 1.15f);

	stick_ghost = Vec2(usable.x + margin + kStickGhostMmX * ppm * control_scale, anchor.y - kStickGhostMmY * ppm * control_scale);
	stick_radius = Clampf(0.11f * vp.y, 9.0f * ppm, 15.0f * ppm) * control_scale;
	split_x = vp.x * 0.5f;

	for (int i = 0; i < kTouchCount; ++i) hit_radii[i] = std::max(radii[i] * kHitSlop, kMinHitMm * ppm);
	hit_radii[TI(TouchId::Cancel)] = radii[TI(TouchId::Cancel)];

	if (left_handed) {
		for (int i = 0; i < kTouchCount; ++i) centers[i].x = vp.x - centers[i].x;
		stick_ghost.x = vp.x - stick_ghost.x;
		const float ux = vp.x - usable.Right();
		usable = Rect(ux, usable.y, usable.w, usable.h);
	}
}

void TouchLayout::Place(Vec2 anchor, float kp, float ks) {
	for (int i = 0; i < 5; ++i) {
		centers[i] = anchor - Vec2(kClusterMm[i][0], kClusterMm[i][1]) * kp;
		radii[i] = kClusterMm[i][2] * 0.5f * ks;
	}
	// Element chips: arc around the technique button, Earth at the low end.
	const Vec2 tech = centers[TI(TouchId::Tech)];
	for (int e = 0; e < 4; ++e) {
		const float a = (kChipArcStartDeg - kChipArcStepDeg * static_cast<float>(e)) * kPi / 180.0f;
		centers[TI(TouchId::Elem0) + e] = tech + Vec2(std::cos(a), -std::sin(a)) * (kChipArcRadiusMm * kp);
		radii[TI(TouchId::Elem0) + e] = kChipDiameterMm * 0.5f * ks;
	}
	// Cancel zone: further out along the corner -> technique direction, always clear of the aim ring.
	const Vec2 dir = (tech - anchor).normalized();
	radii[TI(TouchId::Cancel)] = kCancelRadiusMm * ks;
	const float min_dist = aim_radius + radii[TI(TouchId::Cancel)] + 0.8f * radii[TI(TouchId::Tech)] + 3.0f * ppm;
	centers[TI(TouchId::Cancel)] = tech + dir * std::max(kCancelDistanceMm * kp, min_dist);
}

int TouchLayout::HitTest(Vec2 pos) const {
	int best = kTouchNone;
	float best_score = 1.0f;
	for (int i = 0; i < TI(TouchId::Cancel); ++i) {
		const float d = pos.distance_to(centers[i]) / std::max(hit_radii[i], 1e-3f);
		if (d < best_score) {
			best_score = d;
			best = i;
		}
	}
	return best;
}

bool TouchLayout::InCancelZone(Vec2 pos) const {
	return pos.distance_to(centers[TI(TouchId::Cancel)]) <= radii[TI(TouchId::Cancel)];
}

std::array<Rect, 4> TouchLayout::RingRects() const {
	const float sw = kRingPetalMmW * ppm * control_scale;
	const float sh = kRingPetalMmH * ppm * control_scale;
	const float gap = kRingGapMm * ppm * control_scale;
	float edge = left_handed ? -std::numeric_limits<float>::max() : std::numeric_limits<float>::max();
	float cy = 0.0f;
	for (int e = 0; e < 4; ++e) {
		const Vec2 c = centers[TI(TouchId::Elem0) + e];
		cy += c.y;
		if (left_handed)
			edge = std::max(edge, c.x + radii[TI(TouchId::Elem0) + e]);
		else
			edge = std::min(edge, c.x - radii[TI(TouchId::Elem0) + e]);
	}
	cy /= 4.0f;
	float x = left_handed ? edge + kRingChipGapMm * ppm : edge - kRingChipGapMm * ppm - sw;
	x = Clampf(x, usable.x, std::max(usable.x, usable.Right() - sw));
	const float total = 4.0f * sh + 3.0f * gap;
	const float top = Clampf(cy - total * 0.5f, usable.y + margin * 0.5f, std::max(usable.y, usable.Bottom() - total - margin * 0.5f));
	std::array<Rect, 4> out;
	for (int i = 0; i < 4; ++i) out[static_cast<size_t>(i)] = Rect(x, top + static_cast<float>(i) * (sh + gap), sw, sh);
	return out;
}

int TouchLayout::RingHit(Vec2 pos) const {
	const std::array<Rect, 4> rects = RingRects();
	const float slack = 0.8f * ppm;
	for (int i = 0; i < 4; ++i)
		if (rects[static_cast<size_t>(i)].Grow(slack).Has(pos)) return i;
	return -1;
}

int TouchLayout::RingAim(int element, Vec2 pos) const {
	const int hit = RingHit(pos);
	if (hit >= 0) return hit;
	const int b = TI(TouchId::Elem0) + std::max(0, std::min(element, 3));
	const Vec2 d = pos - centers[b];
	const float inward = left_handed ? d.x : -d.x;   // toward the petal column
	if (inward < radii[b] * 1.6f || inward < std::abs(d.y) * 0.7f) return -1;
	const std::array<Rect, 4> rects = RingRects();
	int best = 0;
	for (int i = 1; i < 4; ++i)
		if (std::abs(rects[static_cast<size_t>(i)].Center().y - pos.y) < std::abs(rects[static_cast<size_t>(best)].Center().y - pos.y)) best = i;
	return best;
}

PetalAnchor TouchLayout::AttackPetalAnchor(ff::Gesture which) const {
	const Vec2 c = centers[TI(TouchId::Attack)];
	const float r = radii[TI(TouchId::Attack)];
	const float k = ppm * control_scale;
	const float inward = left_handed ? 1.0f : -1.0f;
	PetalAnchor a;
	switch (which) {
		case ff::Gesture::Up: a.pos = c + Vec2(0.0f, -(r + 4.4f * k)); a.align = 0; break;
		case ff::Gesture::Down: a.pos = c + Vec2(0.0f, r + 4.4f * k); a.align = 0; break;
		default: a.pos = c + Vec2(inward * (r + 3.0f * k), -4.0f * k); a.align = static_cast<int>(inward); break;
	}
	return a;
}

PetalAnchor TouchLayout::GuardPetalAnchor(ff::Gesture which) const {
	const Vec2 c = centers[TI(TouchId::Guard)];
	const float r = radii[TI(TouchId::Guard)];
	const float k = ppm * control_scale;
	PetalAnchor a;
	if (which == ff::Gesture::Up)
		a.pos = c + Vec2(0.0f, -(r + 4.2f * k));
	else
		a.pos = c + Vec2(0.0f, r + 3.6f * k);
	return a;
}

}  // namespace ffg
