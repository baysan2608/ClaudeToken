// FourfoldFX logic island - small CPU particle sets drawn as camera-facing quads in ONE mesh per effect.
// Each set integrates ballistic motion with drag, gravity, upward buoyancy, a ground plane (bounce / settle), size,
// colour and rotation over life, and builds a fixed-capacity mesh (dead particles become degenerate quads, so the
// Unreal glue can update the section in place). Flipbook frames: uv1.x = frame index (float, the shader blends
// floor / floor+1), uv1.y = per-particle extra (heat / emissive), uv2 = (age 0..1, random).
// Streak mode stretches quads along the velocity (sparks, rain of grit). Deterministic per seed.
// Owner: stream `fx`.
#pragma once

#include "FxBase.h"
#include "FxMesh.h"

#include <cstdint>
#include <vector>

namespace ffx {

struct Particle {
	Vec3 p, v;
	float age = 0.0f, life = 1.0f;
	float size0 = 0.1f, size1 = 0.2f;
	float rot = 0.0f, rotSpeed = 0.0f;
	Color c0, c1;
	float frame0 = 0.0f;     // first flipbook frame (random start for variety)
	float extra = 0.0f;      // uv1.y
	float rnd = 0.0f;
	bool alive = false;
};

struct ParticleLook {
	int frames = 1;          // flipbook frames (64 for an 8x8 atlas; 1 = static sprite)
	bool frameByLife = true; // true: frame = age01 * (frames - 1); false: fps-driven loop
	float fps = 30.0f;
	bool streak = false;     // stretch along velocity
	float streakScale = 0.06f; // seconds of travel the streak covers
	float fadeIn = 0.08f;    // fraction of life
	float fadeOut = 0.45f;   // fraction of life
	bool alignUp = false;    // quads stand up (Y axis) and turn around it toward the camera (fire columns)
};

struct ParticlePhysics {
	float gravity = 0.0f;    // m/s^2 downward
	float drag = 0.0f;       // 1/s
	float buoyancy = 0.0f;   // m/s^2 upward
	float groundY = -1e9f;   // ground plane
	float bounce = 0.25f;    // restitution when hitting the ground (0 = settle)
	Vec3 wind;               // m/s^2
};

class ParticleSet {
public:
	void Reset(int capacity, uint32_t seed);
	int Capacity() const { return static_cast<int>(ps_.size()); }
	Particle* Spawn();                   // nullptr when full
	void Step(float dt, const ParticlePhysics& ph);
	int Alive() const;
	bool AnyAlive() const { return Alive() > 0; }
	Rng& R() { return rng_; }
	std::vector<Particle>& Items() { return ps_; }
	// Rebuilds `m` (cleared): capacity * 4 vertices / 6 indices always (dead ones degenerate).
	void Build(MeshData& m, const Vec3& camPos, const Vec3& camUp, const ParticleLook& look) const;

private:
	std::vector<Particle> ps_;
	Rng rng_;
};

// Colour / size curves used by the bursts.
inline float LifeFade(float t, float fadeIn, float fadeOut) {
	const float a = fadeIn > 0.0f ? Sat(t / fadeIn) : 1.0f;
	const float b = fadeOut > 0.0f ? Sat((1.0f - t) / fadeOut) : 1.0f;
	return a * b;
}

}  // namespace ffx
