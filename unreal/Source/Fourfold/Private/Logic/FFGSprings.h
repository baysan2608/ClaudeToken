// Fourfold game logic island - physical secondary motion:
//  * HitReactor (port of game/presentation/anim/hit_reactor.gd): spring-damper rotation vectors that tip the torso
//    along a blow, whip the head a beat later and flail the arms; blocks give a short recoil.
//  * LandingSpring: under-damped pelvis compression (2.4 Hz, zeta 0.55) kicked by landings and heavy hits.
//  * SpringChain: verlet "spring bone" chains for the ff_hair_* / ff_sash_* / ff_hem_* bones (port of the intent of
//    fighter_secondary_motion.gd: stiffness toward the animated pose, drag, gravity, capsule collision with the thighs).
// All vectors live in the caller's frame (the anim runtime uses mesh component space in metres); axes come in through
// ModelAxes, so nothing here assumes a handedness.
#pragma once

#include "FFGMath.h"

#include <vector>

namespace ffg {

struct ModelAxes {
	Vec3 up{0.0f, 0.0f, 1.0f};
	Vec3 fwd{0.0f, 1.0f, 0.0f};
	Vec3 left{1.0f, 0.0f, 0.0f};
};

struct SpringParams {
	float freq = 2.6f;   // Hz
	float zeta = 0.45f;  // damping ratio
};

class HitReactor {
public:
	static constexpr float kMaxAngle = 0.65f;   // rad, hard clamp per spring
	static constexpr float kHeadDelay = 0.045f; // s, the head lags the torso (whiplash)

	ModelAxes axes;
	SpringParams torso_p{2.6f, 0.45f}, head_p{2.1f, 0.34f}, arm_p{2.3f, 0.4f};
	Vec3 torso, torso_v, head, head_v, arm_l, arm_l_v, arm_r, arm_r_v;   // rotation vectors

	// dir: direction the blow travels (attacker -> victim), model space. strength ~0.3 .. 1.2.
	void Hit(Vec3 dir, float strength);
	// A guarded blow: a short push back of the torso and both forearms, no head whip.
	void Block(Vec3 dir, float strength);
	void Step(float dt);
	void Reset();
	bool Active() const;
	// Axis that tips the up vector toward the horizontal part of `dir`.
	Vec3 TipAxis(Vec3 dir) const;

private:
	static void Integrate(Vec3& x, Vec3& v, const SpringParams& p, float h);
	Vec3 pending_head_;
	float pending_t_ = -1.0f;
};

class LandingSpring {
public:
	float y = 0.0f;   // metres along up (negative = compressed)
	float v = 0.0f;
	void Kick(float down_speed) { v -= Clampf(down_speed, 0.0f, 3.2f); }
	void Step(float dt);
	void Reset() { y = v = 0.0f; }
};

// One chain of n bones simulated as particles at each bone's TAIL (= the next bone's head; the last tail is extended
// along the last bone). Each frame the caller passes the animated heads (incl. the chain root's parent-driven head)
// and gets back per-bone rotations (shortest arc from the animated bone direction to the simulated one).
struct ChainParams {
	float stiffness = 0.9f;     // pull toward the animated direction (1/s scale)
	float drag = 0.35f;         // 0..1 velocity loss per 1/60 s
	float gravity = 0.9f;       // m/s^2 scale
	float radius = 0.025f;      // particle radius for collision
	float max_angle = 1.4f;     // rad from the animated direction
};

struct CapsuleCollider {
	Vec3 a, b;                  // segment end points (same frame as the chain)
	float radius = 0.08f;
};

class SpringChain {
public:
	ChainParams params;
	// world-ish frame: gravity direction (unit) in the chain's frame
	Vec3 gravity_dir{0.0f, 0.0f, -1.0f};

	void Reset() { initialized_ = false; }
	// animated: n+1 points (head of bone 0 ... head of bone n-1, then the animated tail of the last bone).
	// out_dirs: n unit directions each bone should point along (simulated tail - simulated head). The caller walks the
	// chain root first and turns each bone onto its direction (the shortest arc), so children follow their parents.
	void Step(float dt, const std::vector<Vec3>& animated, const std::vector<CapsuleCollider>& colliders, std::vector<Vec3>& out_dirs);
	const std::vector<Vec3>& Tails() const { return cur_; }

private:
	bool initialized_ = false;
	std::vector<Vec3> cur_, prev_;
	std::vector<float> len_;
};

// Closest point on segment ab to p.
Vec3 ClosestOnSegment(Vec3 p, Vec3 a, Vec3 b);

}  // namespace ffg
