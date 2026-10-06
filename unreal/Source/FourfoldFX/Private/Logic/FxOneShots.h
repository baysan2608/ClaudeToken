// FourfoldFX logic island - pooled one-shot effects (ports + upgrades of RingFX, BurstFX, ShardsFX, BlastFX, BeamFX,
// FireBurstFX, AirPushFX, SplashFX, SteamFX, DustPuffFX, EmberFX, LightningArcFX) and the Trail ribbon (GlideTrailFX)
// that views and fighter effects own. Every play() takes an idle instance or recycles the oldest of its kind (pool
// caps from docs/VFX.md), allocates fresh draw keys and steps / draws itself every frame until it ends.
// Owner: stream `fx`.
#pragma once

#include "FxConfig.h"
#include "FxContext.h"
#include "FxLightning.h"
#include "FxMesh.h"
#include "FxParticles.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace ffx {

struct RingOpts {
	int count = 1;          // 1 or 2 rings (the second trails the first)
	bool easeIn = false;    // shrinking rings (absorb, inrush) ease in
	bool hold = false;      // fast out, then hold
	float width = 0.08f;
	float alpha = 1.0f;
	bool billboard = false; // faces the camera (cast / deflect / perfect rings)
	float cover = 0.3f;     // 0 additive .. 1 alpha
	float glow = 1.5f;
	float style = 0.0f;     // ring material style: 0 band, 1 spiral, 2 sound ripple, 3 spin blur
};

enum class ShardMat : uint8_t { Stone, Ice, Glass, Metal, Plant };

class OneShot {
public:
	virtual ~OneShot() = default;
	bool active = false;
	float age = 0.0f;
	uint64_t serial = 0;    // start order (oldest is recycled first)
	// Advances by c.dt, draws; clears `active` when finished.
	virtual void Step(Ctx& c) = 0;
};

// Ribbon trail of recent positions (glide / dash / charged-throw streak). Owned (not pooled): push() while
// emitting, end() lets it fade out; Draw returns false once empty.
class Trail {
public:
	void Begin(Ctx& c, float width, const Color& tint, float opacity, float maxAge);
	void Push(float time, const Vec3& p);
	void End() { emitting_ = false; }
	bool Emitting() const { return emitting_; }
	bool Alive() const { return emitting_ || count_ > 0; }
	// Drops old samples, rebuilds and draws the ribbon. Returns Alive().
	bool Draw(Ctx& c, float fade = 1.0f);

private:
	static constexpr int kPoints = 24;
	Vec3 pos_[kPoints];
	float stamp_[kPoints] = {};
	int count_ = 0;
	bool emitting_ = false;
	float width_ = 0.5f, opacity_ = 0.32f, maxAge_ = 0.7f;
	Color tint_{0.86f, 0.90f, 0.95f, 1.0f};
	float flow_ = 0.0f;
	uint32_t key_ = 0;
	MeshData mesh_;
};

class OneShots {
public:
	OneShots();
	~OneShots();
	OneShots(const OneShots&) = delete;
	OneShots& operator=(const OneShots&) = delete;

	void Clear();
	// Steps and draws every active one-shot (call once per frame after the cues of this frame were played).
	void Step(Ctx& c);
	int ActiveCount() const;

	// ---- plays (positions in sim space; colours are display sRGB)
	void Ring(Ctx& c, const Vec3& pos, const Vec3& normal, float r0, float r1, float dur, const Color& col,
	          const RingOpts& o = RingOpts());
	void Burst(Ctx& c, const Vec3& pos, const Vec3& normal, float strength, ffx::Burst style);
	void Shards(Ctx& c, const Vec3& pos, const Vec3& dir, float strength, ShardMat mat, uint32_t seed, float groundY);
	void Blast(Ctx& c, const Vec3& pos, float radius, float intensity, bool blue, float groundY);
	void Beam(Ctx& c, const Vec3& from, const Vec3& to, float dur, BeamStyle style);
	void FireBurst(Ctx& c, const Vec3& origin, const Vec3& dir, float length, float intensity, bool blue);
	void AirPush(Ctx& c, const Vec3& origin, const Vec3& dir, float radius, float length);
	void Splash(Ctx& c, const Vec3& pos, const Vec3& normal, float strength);
	void Steam(Ctx& c, const Vec3& pos, float amount);
	void Dust(Ctx& c, const Vec3& pos, const Vec3& normal, float strength, const Color& tint = Color(0.50f, 0.45f, 0.38f));
	void Ember(Ctx& c, const Vec3& pos, const Vec3& normal, float strength);
	// Lightning: the main bolt passes through `nodes`; light flash unless `light` is false.
	void Bolt(Ctx& c, const std::vector<Vec3>& nodes, uint32_t seed, float intensity = 1.0f, bool light = true,
	          float width = 0.1f, const Color& tint = Color(0.74f, 0.78f, 1.0f));
	// Brief shadowless light pulse (perfect flashes, T3 charge, bursts).
	void LightPulse(Ctx& c, const Vec3& pos, const Color& col, float intensity, float radius, float dur);

private:
	template <typename T>
	struct Pool {
		std::vector<std::unique_ptr<T>> items;
		T& Acquire(uint64_t& serial) {
			T* best = nullptr;
			for (auto& it : items)
				if (!it->active) {
					best = it.get();
					break;
				}
			if (!best)
				for (auto& it : items)
					if (!best || it->serial < best->serial) best = it.get();
			best->active = true;
			best->age = 0.0f;
			best->serial = ++serial;
			return *best;
		}
	};
	struct RingFx;
	struct BurstFx;
	struct ShardsFx;
	struct BlastFx;
	struct BeamFx;
	struct FireBurstFx;
	struct AirPushFx;
	struct PuffFx;     // splash / steam / dust / ember (one particle system each)
	struct BoltFx;
	struct PulseFx;
	std::vector<OneShot*> all_;
	Pool<RingFx> rings_;
	Pool<BurstFx> bursts_;
	Pool<ShardsFx> shards_;
	Pool<BlastFx> blasts_;
	Pool<BeamFx> beams_;
	Pool<FireBurstFx> fireBursts_;
	Pool<AirPushFx> airPushes_;
	Pool<PuffFx> splashes_;
	Pool<PuffFx> steams_;
	Pool<PuffFx> dusts_;
	Pool<PuffFx> embers_;
	Pool<BoltFx> bolts_;
	Pool<PulseFx> pulses_;
	uint64_t serial_ = 0;
};

}  // namespace ffx
