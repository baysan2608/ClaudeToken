// Fourfold game logic island - locomotion blend (see FFGLocomotion.h).
#include "FFGLocomotion.h"

#include "FFGAnimLibrary.h"

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

void LocoTransition::Update(float dt, float speed, float fwd, bool allowed, const ClipDef* start, const ClipDef* stop_l,
                            const ClipDef* stop_r, int plant_l, int plant_r) {
	if (dt <= 0.0f) return;
	const float prev = prev_speed_;
	const float accel = (speed - prev) / dt;
	prev_speed_ = speed;
	if (speed < kStandSpeed) {
		since_stand_ = 0.0f;
		travelled_ = 0.0f;
	} else {
		since_stand_ += dt;
		travelled_ += speed * dt;
	}
	if (!allowed) {
		kind = Kind::None;
		clip = nullptr;
		return;
	}
	const auto usable = [](const ClipDef* c) { return c && c->root_dist.size() >= 2 && c->TotalDist() > 0.05f; };

	if (kind == Kind::Stop) {
		if (speed > kStandSpeed && accel > 2.0f) {   // pushed off again: back to the gait blend
			kind = Kind::None;
			clip = nullptr;
		} else {
			const float remaining = speed * speed / (2.0f * kDecel);
			const float target = clip->TimeAtDist(clip->TotalDist() - remaining);
			const bool standing = speed < 0.05f;
			settle_ = standing ? settle_ + dt : 0.0f;
			t = std::max(t + (standing ? dt : 0.0f), target);   // never backwards; settles in real time once stopped
			if (t >= clip->duration || settle_ >= kSettleMax) {
				kind = Kind::None;
				clip = nullptr;
			}
			return;
		}
	} else if (kind == Kind::Start) {
		if (speed < kStartSpeed * 0.6f || fwd < 0.7f * speed) {
			kind = Kind::None;
			clip = nullptr;
		} else {
			t = std::max(t, clip->TimeAtDist(travelled_));
			// hand over to the cycle once the clip itself runs at the fighter's speed (mocap starts keep running for
			// seconds) or runs out of travel
			const float clip_speed = (clip->DistAt(t + 0.1f) - clip->DistAt(t)) / 0.1f;
			if ((t > 0.25f && clip_speed >= 0.9f * speed) || travelled_ >= clip->TotalDist() || t >= clip->duration * 0.92f) {
				kind = Kind::None;
				clip = nullptr;
			}
			return;
		}
	}

	// new transitions
	if (prev >= kStopFrom && prev < 8.0f && accel < -kBrake && fwd > 0.7f * speed) {   // > 8 m/s: a teleport, not a run
		const float remaining = speed * speed / (2.0f * kDecel);
		const ClipDef* best = nullptr;
		int best_score = -1;
		for (const ClipDef* c : {stop_l, stop_r}) {
			if (!usable(c)) continue;
			const float tc = c->TimeAtDist(c->TotalDist() - remaining);
			int score = 0;
			if (plant_l >= 0 && c->PlantedAt(0, tc) == plant_l) ++score;
			if (plant_r >= 0 && c->PlantedAt(1, tc) == plant_r) ++score;
			if (score > best_score) {
				best = c;
				best_score = score;
			}
		}
		if (best) {
			settle_ = 0.0f;
			kind = Kind::Stop;
			clip = best;
			t = best->TimeAtDist(best->TotalDist() - remaining);
			++serial;
		}
	} else if (usable(start) && speed >= kStartSpeed && accel > 0.0f && since_stand_ <= kStartWindow && fwd > 0.7f * speed &&
	           travelled_ < start->TotalDist()) {
		kind = Kind::Start;
		clip = start;
		t = start->TimeAtDist(travelled_);
		++serial;
	}
}

}  // namespace ffg
