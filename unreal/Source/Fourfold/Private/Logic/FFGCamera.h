// Fourfold game logic island - third-person duel camera (grew out of game/presentation/camera_rig.gd), sim space.
//
// Modes (all of them springs, so switching is never a cut):
// - free: orbit behind the player; drags rotate it; an incoming threat turns it gently when the player isn't steering.
// - lock-on two-shot (a locked target): the boom hangs off the player's chest, rotated `theta` off the player->rival
//   axis to one side; the view aims at a point between the two chests. Distance, side angle, aim weight, pitch and
//   FOV follow the separation (see the Lock* constants). 1 s after the player last steered, a spring brings the view
//   back onto that framing. Stick-forward points at the rival (MoveYaw).
// - spectator (title / watch): a side-on two-shot of both fighters around their midpoint, never crossing the line.
// Collision against the analytic arena: a wall behind the boom swings it sideways (or flips the two-shot side), then
// lifts it, never closer than kMinDist; interior solids still in the way are reported for see-through drawing.
// Smoothing: critically damped springs (SmoothDamp) on the pivot, the framing, the yaw / pitch assist; the collision
// pull-in is the only fast exponential. Everything is frame-rate independent.
// Feel (real time, so it keeps moving through hit-stop): rotational trauma shake (pitch / yaw / roll noise, trauma^2,
// <= 1.5 cm of translation) with falloff from the duel midpoint, a spring kick, an FOV punch that eases in, holds
// through hit-stop and eases out, a cinematic dolly + FOV punch for big counters, and the 3 % transformation zoom.
// Reduced motion: shake x 0.3 and no roll, no kick / FOV punch / dolly / zoom.
#pragma once

#include "FFGArena.h"
#include "FFGMath.h"

#include <string>
#include <vector>

namespace ffg {

// Critically damped spring toward `target` (Game Programming Gems 4, "Critically Damped Ease-In/Out Smoothing"): reaches
// the target in about `smooth_time` seconds without overshoot, any dt. max_speed <= 0 = unlimited. dt = 0 is a no-op.
float SmoothDamp(float cur, float target, float& vel, float smooth_time, float dt, float max_speed = 0.0f);
// Same on an angle (radians, the shortest way round).
float SmoothDampAngle(float cur, float target, float& vel, float smooth_time, float dt, float max_speed = 0.0f);

// Ease in -> hold while time is frozen / slowed -> ease out. Used by the FOV punch and the cinematic dolly.
struct FeelEnvelope {
	float amount = 0.0f;
	float in_s = 0.06f;
	float out_s = 0.3f;
	float t = 0.0f;
	int phase = 0;            // 0 idle, 1 in, 2 hold, 3 out
	void Start(float amt, float ease_in, float ease_out);
	float Step(float rdt, bool slowed);   // returns amount x envelope
	float Value() const;
};

struct CameraOutput {
	Vec3 pos;                 // camera position incl. feel offsets (sim space)
	Vec3 look;                // point looked at (sim space)
	float fov = 54.0f;        // vertical FOV degrees (the UE side converts to horizontal)
	float roll = 0.0f;        // degrees about the view direction (shake only)
	float view_yaw = 0.0f;    // yaw of the view (sim radians; the camera looks along (sin, 0, cos))
	float move_yaw = 0.0f;    // yaw the stick is relative to (= view_yaw, or the lock axis while locked)
	std::vector<std::string> see_through;   // solids between the pivot and the camera (draw them shadow-only)
};

class CameraLogic {
public:
	// ---- free orbit
	static constexpr float kFreeFov = 54.0f;          // vertical degrees
	static constexpr float kFreeDist = 4.4f;
	static constexpr float kFreeHeight = 1.4f;        // pivot above the feet
	static constexpr float kFreePitch = 0.22f;
	static constexpr float kLookAheadS = 0.12f;       // pivot leads the player's velocity by this much ...
	static constexpr float kLookAheadMax = 0.6f;      // ... at most this far
	// ---- lock-on two-shot; k = clamp((sep - kLockSepNear) / kLockSepSpan, 0, 1)
	static constexpr float kLockSepNear = 3.0f;
	static constexpr float kLockSepSpan = 13.0f;
	static constexpr float kLockDist0 = 3.6f, kLockDistK = 1.6f;         // boom length m
	static constexpr float kLockTheta0 = 24.0f, kLockThetaK = -10.0f;    // side angle off the axis, degrees
	static constexpr float kLockW0 = 0.40f, kLockWK = -0.10f;            // aim point player -> rival
	static constexpr float kLockPitch0 = 0.08f, kLockPitchK = 0.10f;     // boom pitch rad
	static constexpr float kLockFov0 = 50.0f, kLockFovK = 4.0f;          // vertical degrees
	static constexpr float kLockPivotH = 1.35f;       // boom pivot on the player's chest
	static constexpr float kLockAimH = 1.2f;          // aim point height above the ground between them
	// ---- spectator two-shot (title / watch)
	static constexpr float kSpecAngle = 70.0f;        // view direction off the fighters' axis, degrees
	static constexpr float kSpecPitch = 0.16f;
	static constexpr float kSpecHeight = 1.1f;
	static constexpr float kSpecFov = 46.0f;
	// ---- springs (seconds)
	static constexpr float kPivotXzS = 0.12f;
	static constexpr float kPivotYS = 0.28f;
	static constexpr float kPivotYDead = 0.25f;       // grounded: vertical moves inside this band don't move the pivot
	static constexpr float kTargetXzS = 0.15f;
	static constexpr float kFramingS = 0.4f;          // distance / FOV / aim weight
	static constexpr float kAssistS = 0.35f;          // yaw / pitch back onto the two-shot
	static constexpr float kAssistMaxRate = 4.0f;     // rad/s
	static constexpr float kReengageS = 1.0f;         // the assist waits this long after the player steered
	static constexpr float kSpecS = 0.7f;
	static constexpr float kCollisionInK = 18.0f;     // exponential pull-in when something gets between
	static constexpr float kCollisionOutS = 0.45f;
	// ---- limits
	static constexpr float kMaxHFov = 90.0f;          // horizontal cap on wide phones
	static constexpr float kReducedShake = 0.3f;
	static constexpr float kMinDist = 3.0f;
	static constexpr float kMaxPitchTarget = 0.62f;
	static constexpr float kMinPull = 1.6f;
	// ---- feel
	static constexpr float kShakePitchDeg = 1.2f;     // at trauma 1 (x trauma^2)
	static constexpr float kShakeYawDeg = 0.9f;
	static constexpr float kShakeRollDeg = 1.2f;      // only for roll requests (T3 / knockdown)
	static constexpr float kShakePosM = 0.015f;
	static constexpr float kShakeHz = 21.0f;
	static constexpr float kShakeFloorPlayer = 0.75f; // falloff floor for hits the player gives / takes
	static constexpr float kKickHz = 5.0f;
	static constexpr float kKickZeta = 0.6f;
	static constexpr float kFovInS = 0.06f;
	static constexpr float kFovOutS = 0.3f;
	static constexpr float kCineFov = -6.0f;
	static constexpr float kCineDolly = 0.07f;        // fraction of the way toward the event

	float yaw = kPi;          // camera looks along (sin yaw, 0, cos yaw)
	float pitch = kFreePitch; // radians above horizontal
	float distance = kFreeDist;
	float height = kFreeHeight;
	float shake_scale = 1.0f;
	bool reduced_motion = false;
	bool spectator = false;   // title / watch: side-on two-shot of the player and the target
	bool player_grounded = true;
	Vec3 player_vel;          // sim m/s (look-ahead)
	float aspect = 16.0f / 9.0f;
	const ArenaGround* arena = nullptr;

	void SnapTo(Vec3 player_pos, Vec3 look_at_pos);
	// Open world: the sim bubble moved by d; keep every sim-space state where it is in the world.
	void Shift(Vec3 d);
	// InputFrame.cam_delta: radians, x = look right +, y = look up + (sensitivity / invert already applied).
	// Call every rendered frame (not per sim tick) so drags are smooth at any frame rate and during hit-stop.
	void AddInput(Vec2 delta);
	float ViewYaw() const { return yaw + orbit_; }
	float MoveYaw() const { return move_yaw_; }
	Vec3 ForwardFlat() const { return Vec3(std::sin(ViewYaw()), 0.0f, std::cos(ViewYaw())); }

	void Shake(float amount, float decay_s = 0.2f);
	// pos: event position; player: the player gave / took it (falloff floor); roll: add roll (T3 / knockdown).
	void ShakeAt(float amount, Vec3 pos, float decay_s = 0.2f, bool player = false, bool roll = false);
	void Kick(Vec3 dir, float amount = 0.1f);           // peak offset ~amount metres along dir (spring)
	void FovPunch(float deg = -3.0f, float out_s = kFovOutS);
	void ZoomTo(Vec3 at, float amount = 0.03f, float dur = 0.3f);
	void Cinematic(Vec3 at);                            // big counter: FOV punch + dolly toward `at`

	// dt = game (dilated) seconds, real_dt = undilated seconds.
	CameraOutput Update(float dt, float real_dt, Vec3 player_pos, const Vec3* target_pos, const Vec3* threat_pos);

	// tests / debug
	float CurDist() const { return cur_dist_; }
	float Orbit() const { return orbit_; }
	float Lift() const { return lift_; }
	float ShakeAmount() const { return trauma_; }
	float LockSide() const { return side_; }
	float LockAxis() const { return axis_; }
	bool Locked() const { return locked_; }
	Vec3 KickOffset() const { return kick_x_; }
	float FramedFov() const { return fov_f_; }

private:
	float ClearDist(float y, float p, float want, bool boundary_only = false) const;
	Vec3 DirOf(float y, float p) const;
	float BestOrbit(float want, Vec3 player_pos, const Vec3* target_pos) const;
	float SearchOrbit(float want, Vec3 player_pos, const Vec3* target_pos, float half_h, float max_off, float base_d) const;
	float HalfHFov() const;
	float CapFov(float vfov) const;
	bool TargetInView(float y, float dist, Vec3 player_pos, Vec3 target_pos, float half_h) const;
	void Feel(float rdt, bool slowed, Vec3 cam_pos, Vec3& look, float& fov, float& roll, Vec3& trans, Vec3& dolly);
	void FollowPivot(float dt, Vec3 want);

	Vec3 pivot_;
	Vec3 pivot_vel_;
	float pivot_y_hold_ = 0.0f;
	Vec3 tgt_;                 // smoothed target feet
	Vec3 tgt_vel_;
	bool have_tgt_ = false;
	bool had_target_ = false;  // last frame had one
	bool locked_ = false;
	float lock_w_ = 0.0f, lock_w_vel_ = 0.0f;   // 0 free .. 1 two-shot (blends pivot height and the look point)
	float axis_ = 0.0f;
	float side_ = 1.0f;
	float side_hold_ = 0.0f;
	float move_yaw_ = kPi;
	float dist_f_ = kFreeDist, dist_vel_ = 0.0f;
	float fov_f_ = kFreeFov, fov_vel_ = 0.0f;
	float w_f_ = kLockW0, w_vel_ = 0.0f;
	float yaw_vel_ = 0.0f, pitch_vel_ = 0.0f;
	float coll_d_ = kFreeDist + 1.0f, coll_vel_ = 0.0f;   // collision-limited distance (fast in, eased out)
	float cur_dist_ = kFreeDist;
	bool snap_ = true;         // next real frame lands on the framing directly (scenario start, round reset)
	float idle_ = kReengageS;
	float lift_ = 0.0f;
	float orbit_ = 0.0f;
	float frame_w_ = 0.0f;
	// feel
	float trauma_ = 0.0f;
	float trauma_decay_ = 3.5f;
	float roll_trauma_ = 0.0f;
	float shake_t_ = 0.0f;
	Vec3 kick_x_, kick_v_;
	FeelEnvelope fov_env_;
	FeelEnvelope dolly_env_;
	Vec3 dolly_at_;
	float zoom_t_ = 0.0f;
	float zoom_dur_ = 0.3f;
	float zoom_amt_ = 0.0f;
	Vec3 zoom_at_;
	Vec3 focus_;               // duel midpoint (shake falloff)
};

}  // namespace ffg
