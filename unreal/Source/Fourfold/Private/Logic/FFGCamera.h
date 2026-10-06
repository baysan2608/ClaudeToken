// Fourfold game logic island - third-person orbit camera (port of game/presentation/camera_rig.gd), sim space.
// Player drags rotate it; when idle it turns gently to keep the locked target / an incoming threat in frame (assist,
// never a snap). Collision against the analytic arena: a wall behind the player swings the camera sideways, then lifts
// it, never closer than kMinDist; interior solids still in the way are reported for see-through drawing.
// Feel (real time, so it keeps moving through hit-stop): shake with distance falloff 1 / (1 + d / 8), kick along the
// hit, FOV punch, 3 % transformation zoom. Reduced motion: shake x 0.3, no kick / FOV punch / zoom.
#pragma once

#include "FFGArena.h"
#include "FFGMath.h"

#include <string>
#include <vector>

namespace ffg {

struct CameraOutput {
	Vec3 pos;                 // camera position incl. feel offsets (sim space)
	Vec3 look;                // point looked at (sim space)
	float fov = 62.0f;        // vertical FOV degrees (the UE side converts to horizontal)
	float view_yaw = 0.0f;    // yaw the stick is relative to (sim radians; the camera looks along (sin, 0, cos))
	std::vector<std::string> see_through;   // solids between the pivot and the camera (draw them shadow-only)
};

class CameraLogic {
public:
	static constexpr float kBaseFov = 62.0f;
	static constexpr float kReducedShake = 0.3f;
	static constexpr float kMinDist = 3.0f;
	static constexpr float kMaxPitchTarget = 0.62f;
	static constexpr float kMinPull = 1.6f;

	float yaw = kPi;          // camera looks along (sin yaw, 0, cos yaw)
	float pitch = 0.32f;      // radians above horizontal
	float distance = 5.6f;
	float height = 1.45f;
	float shake_scale = 1.0f;
	bool reduced_motion = false;
	float aspect = 16.0f / 9.0f;
	const ArenaGround* arena = nullptr;

	void SnapTo(Vec3 player_pos, Vec3 look_at_pos);
	// InputFrame.cam_delta: radians, x = look right +, y = look up + (sensitivity / invert already applied).
	void AddInput(Vec2 delta);
	float ViewYaw() const { return yaw + orbit_; }
	Vec3 ForwardFlat() const { return Vec3(std::sin(ViewYaw()), 0.0f, std::cos(ViewYaw())); }

	void Shake(float amount);
	void ShakeAt(float amount, Vec3 pos, float decay_s = 0.2f);
	void Kick(Vec3 dir, float amount = 0.1f);
	void FovPunch(float deg = -3.0f, float dur = 0.25f);
	void ZoomTo(Vec3 at, float amount = 0.03f, float dur = 0.3f);

	// dt = game (dilated) seconds, real_dt = undilated seconds.
	CameraOutput Update(float dt, float real_dt, Vec3 player_pos, const Vec3* target_pos, const Vec3* threat_pos);

	// tests / debug
	float CurDist() const { return cur_dist_; }
	float Orbit() const { return orbit_; }
	float Lift() const { return lift_; }
	float ShakeAmount() const { return shake_; }

private:
	float ClearDist(float y, float p, float want, bool boundary_only = false) const;
	Vec3 DirOf(float y, float p) const;
	float BestOrbit(float want, Vec3 player_pos, const Vec3* target_pos) const;
	float SearchOrbit(float want, Vec3 player_pos, const Vec3* target_pos, float half_h, float max_off, float base_d) const;
	float HalfHFov() const;
	bool TargetInView(float y, float dist, Vec3 player_pos, Vec3 target_pos, float half_h) const;
	Vec3 Feel(float rdt, Vec3 cam_pos, Vec3 right, Vec3 up, float& fov_out);

	Vec3 pivot_;
	float cur_dist_ = 5.6f;
	float idle_ = 0.0f;
	float shake_ = 0.0f;
	float shake_t_ = 0.0f;
	float shake_decay_ = 3.5f;
	float lift_ = 0.0f;
	Vec3 kick_;
	float kick_t_ = 0.0f;
	float fov_punch_ = 0.0f;
	float fov_t_ = 0.0f;
	float fov_dur_ = 0.25f;
	float zoom_t_ = 0.0f;
	float zoom_dur_ = 0.3f;
	float zoom_amt_ = 0.0f;
	Vec3 zoom_at_;
	float frame_w_ = 0.0f;
	float orbit_ = 0.0f;
};

}  // namespace ffg
