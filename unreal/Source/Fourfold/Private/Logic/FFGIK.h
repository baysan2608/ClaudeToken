// Fourfold game logic island - procedural pose math for the native anim runtime (ports of
// game/presentation/anim/leg_ik.gd and the foot / look parts of fighter_anim_rig.gd):
//  * SolveTwoBone: analytic two-bone IK that keeps the animated knee plane (a target equal to the animated ankle
//    reproduces the clip exactly, so fading the weight never pops), soft reach near full extension, explicit foot
//    orientation (planted soles stay flat).
//  * FootPlanter: per-fighter foot goals - planted feet onto the analytic arena ground (heel / ball, smoothed),
//    ledge / step handling, pelvis drop and hip tilt, and foot LOCKING (a planted foot keeps its world position and
//    heading until it lifts, the body moves too far, or the turn exceeds a limit; then it eases back).
//  * LookAt: clamped, smoothed head / neck / chest turn toward a point.
// Frames: "model" = mesh component space; "world" = any frame where `up` is the world up. Units: metres.
#pragma once

#include "FFGSprings.h"

#include <array>

namespace ffg {

struct TwoBoneResult {
	Quat thigh, calf, foot;   // new model-space rotations
	float reach_error = 0.0f; // > 0 when the target was out of reach
};

// a / b / c: model-space heads of thigh, calf, foot; qa / qb / qc their model-space rotations; target: ankle goal;
// foot_goal: the foot's goal rotation; hinge_fallback: knee hinge axis when the leg is perfectly straight.
TwoBoneResult SolveTwoBone(Vec3 a, Vec3 b, Vec3 c, Quat qa, Quat qb, Quat qc, Vec3 target, Quat foot_goal, float w, Vec3 hinge_fallback);

// Ground under a world point: returns the height (along world up) of the walkable surface reachable from from_up.
class GroundSampler {
public:
	virtual ~GroundSampler() = default;
	virtual float GroundUp(Vec3 world_point, float from_up) const = 0;
};

struct FootInput {
	Vec3 ankle;       // model space (animated, before procedural layers)
	Quat foot_rot;    // model space
	Vec3 ball;        // model space
	int plant_hint = -1;   // clip foot_plants at the clip time: -1 unknown, 0 lifted, 1 planted
};

struct FootFrameInput {
	float dt = 0.0f;
	float ik_w = 0.0f;            // smoothed IK weight 0..1
	float lock_target_w = 0.0f;   // 1 = planted feet may lock to the ground
	float ground_speed = 0.0f;    // m/s, horizontal
	float local_speed = 0.0f;     // |local velocity| m/s
	float model_lift = 0.0f;      // visual height smoothing offset of the model (m)
	Xform world_from_model;       // model -> world
	Vec3 world_up{0.0f, 0.0f, 1.0f};
	Vec3 world_ref{1.0f, 0.0f, 0.0f};   // fixed horizontal reference direction for headings
	std::array<FootInput, 2> feet;      // 0 = left, 1 = right
	std::array<Vec3, 2> thigh_heads;    // model space hip joints
	std::array<float, 2> leg_len{{0.82f, 0.82f}};
};

struct FootFrameOutput {
	std::array<Vec3, 2> goal;       // ankle goals (model space)
	std::array<Quat, 2> goal_rot;   // foot rotations (model space)
	float pelvis_shift = 0.0f;      // along model up (negative = down)
	float hip_tilt = 0.0f;          // rad: tilt toward the lower foot (applied about the model forward axis)
	bool use_ik = false;
};

class FootPlanter {
public:
	static constexpr float kPlantFrom = 0.115f;
	static constexpr float kPlantTo = 0.19f;
	static constexpr float kMaxDrop = 0.38f;
	static constexpr float kMaxLift = 0.45f;
	static constexpr float kPelvisShare = 0.5f;
	static constexpr float kReachFrac = 0.96f;
	static constexpr float kLockMaxDist = 0.2f;
	static constexpr float kLockMaxYaw = 0.7f;
	static constexpr float kLockMaxFootSpeed = 0.7f;
	static constexpr float kLockRelease = 0.22f;
	static constexpr float kStepHeight = 0.4f;

	ModelAxes axes;
	bool enable_lock = true;

	void Reset();
	FootFrameOutput Update(const FootFrameInput& in, const GroundSampler* ground);

	// state (tests / debug)
	std::array<bool, 2> locked{{false, false}};
	std::array<float, 2> lock_w{{0.0f, 0.0f}};
	std::array<float, 2> foot_off{{0.0f, 0.0f}};
	float pelvis_shift = 0.0f;
	float hip_tilt = 0.0f;

private:
	float PlantOf(const FootInput& f) const;
	float YawOf(const FootFrameInput& in, const FootInput& f) const;
	void LockFeet(const FootFrameInput& in, std::array<Vec3, 2>& goal, std::array<Quat, 2>& grot);
	void GroundGoals(const FootFrameInput& in, const GroundSampler* ground, std::array<Vec3, 2>& goal);
	float ReachDrop(const FootFrameInput& in, const std::array<Vec3, 2>& goal, float shift) const;

	std::array<Vec3, 2> lock_pos_{};
	std::array<float, 2> lock_yaw_{{0.0f, 0.0f}};
	std::array<bool, 2> relock_wait_{{false, false}};
	std::array<float, 2> catch_lift_{{0.0f, 0.0f}};
	std::array<float, 2> ground_w_{{0.0f, 0.0f}};
	std::array<bool, 2> ground_valid_{{false, false}};
	std::array<Vec3, 2> prev_anim_w_{};
	Vec3 last_origin_;
	bool have_origin_ = false;
};

// Head look-at: smoothed yaw / pitch (radians) relative to where the animation points the head.
class LookAt {
public:
	static constexpr float kYawMax = 1.05f;     // ~60 deg
	static constexpr float kPitchMax = 0.52f;   // ~30 deg
	ModelAxes axes;
	float yaw = 0.0f, pitch = 0.0f;
	// head_pos / head_fwd: model space (head_fwd = the head's current facing); target: model space.
	void Update(float dt, Vec3 head_pos, Vec3 head_fwd, Vec3 target, float weight);
	void Reset() { yaw = pitch = 0.0f; }
	// Rotation (model space) for a share of the yaw / pitch: yaw about up, pitch about the turned right axis.
	Quat Rotation(float yaw_share, float pitch_share, float pitch_axis_yaw_share) const;
};

}  // namespace ffg
