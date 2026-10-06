// Fourfold game logic island - synced, stride-matched locomotion blend (port of
// game/presentation/anim/locomotion_blender.gd): stance <- speed -> walk <- speed -> run, and by direction relative to
// the facing: forward / strafe_l / strafe_r / walk_back. All gaits share one normalised phase (left-foot touchdown at 0
// in every clip), so changing gait or direction never restarts a cycle; the phase advances at actual_speed /
// blended_stride, so planted feet travel at ground speed (no skating). Weights are smoothed (no pops).
#pragma once

#include "FFGMath.h"

#include <array>

namespace ffg {

enum class LocoRole : int { Stance = 0, Walk, Run, StrafeL, StrafeR, Back, Count };
inline constexpr int kLocoRoles = static_cast<int>(LocoRole::Count);

struct GaitSpec {
	float speed = 1.0f;    // design ground speed (m/s)
	float stride = 1.0f;   // metres per cycle
	float offset = 0.0f;   // phase of the left-foot touchdown
};

class LocomotionBlender {
public:
	static constexpr float kWeightRate = 9.0f;
	static constexpr float kMoveStart = 0.12f;
	static constexpr float kMoveFull = 1.05f;
	static constexpr float kRunFrom = 2.2f;
	static constexpr float kRunTo = 3.8f;
	static constexpr float kDirectionalMax = 3.2f;

	std::array<GaitSpec, kLocoRoles> gaits{{
		{0.0f, 1.0f, 0.0f},     // stance (unused)
		{1.4f, 1.26f, 0.0f},    // walk
		{5.5f, 3.30f, 0.0f},    // run
		{1.1f, 0.99f, 0.0f},    // strafe_l
		{1.1f, 0.99f, 0.0f},    // strafe_r
		{1.0f, 0.90f, 0.0f},    // walk_back
	}};
	std::array<float, kLocoRoles> weights{};   // smoothed weights
	float phase = 0.0f;
	float stance_t = 0.0f;
	float speed = 0.0f;
	float cycle_rate = 0.0f;

	// local_vel: x = toward the character's right, y = forward (m/s).
	static std::array<float, kLocoRoles> TargetWeights(Vec2 local_vel);
	void Update(float dt, Vec2 local_vel);
	void Reset() {
		weights = {};
		weights[0] = 1.0f;
		phase = 0.0f;
		speed = 0.0f;
		cycle_rate = 0.0f;
	}
	// Sample time of a role's clip of length `length` (gaits on the shared phase, the stance on its own clock).
	float ClipTime(LocoRole role, float length) const;
	float GaitAmount() const;
	LocoRole Dominant() const;
	// Set a gait's design speed from its clip (stride = speed x cycle length).
	void SetGait(LocoRole role, float design_speed, float cycle_len) {
		if (design_speed <= 0.0f || cycle_len <= 0.0f) return;
		gaits[static_cast<size_t>(role)].speed = design_speed;
		gaits[static_cast<size_t>(role)].stride = design_speed * cycle_len;
	}
};

}  // namespace ffg
