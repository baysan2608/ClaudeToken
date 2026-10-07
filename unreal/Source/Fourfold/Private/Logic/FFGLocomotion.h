// Fourfold game logic island - synced, stride-matched locomotion blend (port of
// game/presentation/anim/locomotion_blender.gd): stance <- speed -> walk <- speed -> run, and by direction relative to
// the facing: forward / strafe_l / strafe_r / walk_back. All gaits share one normalised phase (left-foot touchdown at 0
// in every clip), so changing gait or direction never restarts a cycle; the phase advances at actual_speed /
// blended_stride, so planted feet travel at ground speed (no skating). Weights are smoothed (no pops).
#pragma once

#include "FFGMath.h"

#include <array>
#include <cstdint>

namespace ffg {

struct ClipDef;

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

// One-shot transitions over the gait blend (free locomotion only): a braking run plants the feet into a stop clip and
// settles, a run from standing pushes off with a start clip. The sim moves the fighter and the clips are in place, so
// they are distance-matched: a stop samples the clip where its remaining root travel equals the distance the sim still
// needs to stop (v^2 / 2 DECEL), a start where its travel equals the distance covered since leaving the stand. The sim
// brakes in ~0.13 s (5.5 m/s, DECEL 42), so a stop mostly shows the clip's plant and settle.
class LocoTransition {
public:
	enum class Kind { None, Start, Stop };
	static constexpr float kStopFrom = 3.0f;      // m/s: only a run that brakes gets a stop
	static constexpr float kBrake = 15.0f;        // m/s^2 of measured deceleration that counts as braking
	static constexpr float kStandSpeed = 0.4f;    // m/s: below this the fighter stands
	static constexpr float kStartSpeed = 2.2f;    // m/s: a start fires once a run from standing passes this ...
	static constexpr float kStartWindow = 0.25f;  // ... within this many seconds of leaving the stand
	static constexpr float kDecel = 42.0f;        // sim combat_world.DECEL (m/s^2)
	static constexpr float kSettleMax = 0.45f;    // s of a stop's settle after the fighter stands (mocap idles run long)

	Kind kind = Kind::None;
	const ClipDef* clip = nullptr;
	float t = 0.0f;              // clip time
	uint32_t serial = 0;         // bumps on every new transition

	// speed: ground speed (m/s); fwd: its forward part; allowed: free locomotion on the ground this frame.
	// plant_l / plant_r: feet planted by the gait blend right now (-1 unknown) - picks the stop clip that matches.
	void Update(float dt, float speed, float fwd, bool allowed, const ClipDef* start, const ClipDef* stop_l,
	            const ClipDef* stop_r, int plant_l = -1, int plant_r = -1);
	void Reset() {
		kind = Kind::None;
		clip = nullptr;
		t = 0.0f;
		prev_speed_ = 0.0f;
		since_stand_ = 1e3f;
		travelled_ = 0.0f;
		settle_ = 0.0f;
	}

private:
	float prev_speed_ = 0.0f;
	float since_stand_ = 1e3f;   // seconds since the speed was last below kStandSpeed
	float travelled_ = 0.0f;     // metres since then
	float settle_ = 0.0f;        // seconds a stop has been standing
};

}  // namespace ffg
