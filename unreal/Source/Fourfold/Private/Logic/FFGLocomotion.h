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
	float offset = 0.0f;   // phase of the left-foot touchdown (fraction of one cycle)
	int cycles = 1;        // stride cycles in the clip: the shared phase walks through them one after another
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
	int cycle_index = 0;   // completed phase cycles (selects the cycle of a multi-cycle clip)
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
		cycle_index = 0;
		speed = 0.0f;
		cycle_rate = 0.0f;
	}
	// Sample time of a role's clip of length `length` (gaits on the shared phase, the stance on its own clock).
	float ClipTime(LocoRole role, float length) const;
	float GaitAmount() const;
	LocoRole Dominant() const;
	// Set a gait from its clip: design speed, stride (= speed x one cycle), the left-foot touchdown phase and the
	// number of cycles in the clip (phase0 is a fraction of the whole clip).
	void SetGait(LocoRole role, float design_speed, float clip_len, float phase0 = 0.0f, int clip_cycles = 1) {
		if (design_speed <= 0.0f || clip_len <= 0.0f) return;
		GaitSpec& g = gaits[static_cast<size_t>(role)];
		g.cycles = clip_cycles > 0 ? clip_cycles : 1;
		g.speed = design_speed;
		g.stride = design_speed * clip_len / static_cast<float>(g.cycles);
		const float o = phase0 * static_cast<float>(g.cycles);
		g.offset = o - static_cast<float>(static_cast<int>(o));
	}
};

}  // namespace ffg
