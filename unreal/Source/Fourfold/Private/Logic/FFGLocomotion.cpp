// Fourfold game logic island - locomotion blend (see FFGLocomotion.h).
#include "FFGLocomotion.h"

namespace ffg {

std::array<float, kLocoRoles> LocomotionBlender::TargetWeights(Vec2 local_vel) {
	std::array<float, kLocoRoles> tw{};
	const float spd = local_vel.length();
	const float moving = SmoothStep(kMoveStart, kMoveFull, spd);
	tw[0] = 1.0f - moving;
	if (moving <= 0.0f) return tw;
	float fwd = 1.0f, side = 0.0f, back = 0.0f;
	const float a = std::fabs(std::atan2(local_vel.x, local_vel.y));   // 0 forward, pi backward
	if (a <= kPi * 0.5f) {
		const float t = SmoothStep(0.2f, 0.8f, a / (kPi * 0.5f));
		fwd = 1.0f - t;
		side = t;
	} else {
		const float t = SmoothStep(0.2f, 0.8f, (a - kPi * 0.5f) / (kPi * 0.5f));
		fwd = 0.0f;
		side = 1.0f - t;
		back = t;
	}
	// Running is always forward (the sim faces the run direction); fade the directional set out.
	const float dir_w = 1.0f - SmoothStep(kDirectionalMax - 0.6f, kDirectionalMax + 0.2f, spd);
	fwd = Lerpf(1.0f, fwd, dir_w);
	side *= dir_w;
	back *= dir_w;
	const float run_w = SmoothStep(kRunFrom, kRunTo, spd);
	tw[static_cast<size_t>(LocoRole::Walk)] = moving * fwd * (1.0f - run_w);
	tw[static_cast<size_t>(LocoRole::Run)] = moving * fwd * run_w;
	tw[static_cast<size_t>(local_vel.x < 0.0f ? LocoRole::StrafeL : LocoRole::StrafeR)] = moving * side;
	tw[static_cast<size_t>(LocoRole::Back)] = moving * back;
	return tw;
}

void LocomotionBlender::Update(float dt, Vec2 local_vel) {
	speed = local_vel.length();
	const std::array<float, kLocoRoles> tw = TargetWeights(local_vel);
	const float k = ExpK(kWeightRate, dt);
	for (size_t i = 0; i < weights.size(); ++i) {
		weights[i] += (tw[i] - weights[i]) * k;
		if (tw[i] <= 0.0f && weights[i] < 0.003f) weights[i] = 0.0f;
	}
	float gw = 0.0f, stride = 0.0f, tot = 0.0f;
	for (size_t i = 0; i < weights.size(); ++i) {
		tot += weights[i];
		if (i == 0) continue;
		gw += weights[i];
		stride += weights[i] * gaits[i].stride;
	}
	if (gw > 1e-3f) {
		stride /= gw;
		// Blending with the planted stance shrinks the footprints by the gait's share of the mix: the cadence follows
		// the effective stride, so slow walks take short quick steps (and the feet still travel at ground speed).
		const float eff = stride * Clampf(gw / std::max(tot, 1e-3f), 0.25f, 1.0f);
		cycle_rate = Clampf(speed / std::max(eff, 0.1f), 0.5f, 2.2f);
	} else {
		cycle_rate = 0.0f;
	}
	const float next = phase + cycle_rate * dt;
	if (next >= 1.0f) cycle_index = (cycle_index + static_cast<int>(next)) % 1000000;
	phase = Fposmod(next, 1.0f);
	stance_t += dt;
}

float LocomotionBlender::ClipTime(LocoRole role, float length) const {
	if (length <= 1e-4f) return 0.0f;
	if (role == LocoRole::Stance) return Fposmod(stance_t, length);
	const GaitSpec& g = gaits[static_cast<size_t>(role)];
	if (g.cycles <= 1) return Fposmod(phase + g.offset, 1.0f) * length;
	// Continuous through the cycles: cycle_index steps exactly when phase wraps, so the sum never jumps.
	const float total = static_cast<float>(cycle_index % g.cycles) + phase + g.offset;
	return Fposmod(total, static_cast<float>(g.cycles)) * (length / static_cast<float>(g.cycles));
}

float LocomotionBlender::GaitAmount() const {
	float s = 0.0f;
	for (size_t i = 1; i < weights.size(); ++i) s += weights[i];
	return s;
}

LocoRole LocomotionBlender::Dominant() const {
	size_t best = 0;
	for (size_t i = 1; i < weights.size(); ++i)
		if (weights[i] > weights[best]) best = i;
	return static_cast<LocoRole>(best);
}

}  // namespace ffg
