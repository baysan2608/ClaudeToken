// FourfoldFX logic island - the arena reacting to the fighters and their element moves ("world reactions"):
//   * footfall / movement dust: a pale granite puff at every running foot plant (bone anchors; a stride clock when the
//     glue sends none), kicks on run starts, skids on stops, pivots, dashes and landings, Earth stomps;
//   * ground scars: impact cracks / craters (+ a dust shockwave ring and a few physics chips), fire scorch, lightning
//     burns (+ sparks and a short light on the surroundings), wet marks that dry from their edges, sand drifts, frost;
//   * the courtyard pool: anything crossing its surface (stones, fighters, fireballs, waves) splashes and spreads
//     ripple rings, fire close over it raises steam, water drawn from it pulls rings in and out of the draw point;
//   * wind: gusts sweep floor dust along their path, kick up leaves, ruffle the pool, and drive the arena-wide
//     WindGust / WindDir the glue mirrors into MPC_Arena (trees and banners sway harder for a moment).
// Nothing here changes the sim: events come from the cues (FxCues.cpp), the rest is derived from fighter / body state
// deltas between sim ticks. Every effect is pooled (fixed pools built up front, the oldest recycled at the quality
// cap), cheap (one quad per scar / ripple, <= 24 particles per puff set) and reuses the pre-warmed FX materials
// (Ground decal styles 6..11, Ring style 5, Smoke dust puffs). Owner: stream `fx`.
#pragma once

#include "FxContext.h"
#include "FxMesh.h"
#include "FxParticles.h"
#include "FxTypes.h"

#include <cstdint>
#include <map>
#include <memory>
#include <vector>

namespace ffx {

// Ground scar looks (M_FX_Ground decal styles: crack 9, scorch 10, bolt 11, wet 8 (Heat = dryness), sand 7, frost 6).
enum class ScarKind : uint8_t { Crack, Scorch, Bolt, Wet, Sand, Frost, Count };
float ScarStyle(ScarKind k);

class WorldFx {
public:
	WorldFx();
	~WorldFx();
	WorldFx(const WorldFx&) = delete;
	WorldFx& operator=(const WorldFx&) = delete;

	void Reset();
	// Once per rendered frame after the cues: fighter / body trackers (state deltas on new sim ticks), steps and draws
	// the pooled effects, writes the arena wind gust into c.out.env.
	void Update(Ctx& c);
	int ActiveCount() const;
	float Gust() const { return gust_; }

	// ---- cues (sim space). strength ~0..2 (the weight of the hit); radius 0 = from the strength.
	// Ground reaction by material family: crack + dust ring (+ chips) for stone / magma / metal, sand drift, scorch for
	// fire, wet mark for water, frost for ice, bolt burn for lightning, a dust ring for air. Over the pool: a splash,
	// ripples and (fire, magma) steam instead. Nothing when p is high above the ground.
	void Impact(Ctx& c, const Vec3& p, Fam fam, float strength, float radius = 0.0f);
	// One scar decal (merged into a live scar of the same kind close by). life 0 = the kind's configured life.
	void Scar(Ctx& c, const Vec3& p, ScarKind kind, float radius, float heat = 0.0f, float life = 0.0f);
	// Ripple rings on the pool surface at p (x, z); r1 = outer radius at the end; inward: rings close in on p.
	void Ripples(Ctx& c, const Vec3& p, float r1, float strength, int rings = 2, bool inward = false);
	// Pale dust shockwave rolling out along the floor from p.
	void DustRing(Ctx& c, const Vec3& p, float radius, float strength, const Color& tint);
	// A low dust puff thrown along `dir` (feet: steps, skids, dashes). alphaScale x world.footfall_alpha.
	void Kick(Ctx& c, const Vec3& p, const Vec3& dir, float strength, float alphaScale, const Color& tint);
	// Air push / gust from origin along dir: floor dust swept along it (when near the floor), leaves, pool ruffles, and
	// the arena wind gust (MPC) raised to `strength` toward dir.
	void Gust(Ctx& c, const Vec3& origin, const Vec3& dir, float range, float strength);
	void Landing(Ctx& c, int actor, float speed);
	void Stomp(Ctx& c, int actor, float strength);
	void LightningStrike(Ctx& c, const Vec3& p, float strength);
	// Water drawn out of the pool at `at`: rings close in on the draw point, then spread (cooldown per actor).
	void DrawFromPool(Ctx& c, const Vec3& at, int actor);
	// A body / fighter breaks the pool surface at (x, level, z).
	void PoolHit(Ctx& c, const Vec3& at, Fam fam, float strength);

	bool InPool(const Ctx& c, const Vec3& p, float margin = 0.0f) const;
	float PoolLevel(const Ctx& c) const;

private:
	struct ScarFx;
	struct RippleFx;
	struct PuffSet;
	struct ActorTrack;
	struct BodyTrack;

	void TrackActors(Ctx& c, bool newTick);
	void TrackBodies(Ctx& c, bool newTick);
	void Chips(Ctx& c, const Vec3& g, float strength);
	PuffSet& NewPuff(Ctx& c, bool big);
	ScarFx& NewScar(Ctx& c);
	RippleFx& NewRipple(Ctx& c);

	std::vector<std::unique_ptr<ScarFx>> scars_;
	std::vector<std::unique_ptr<RippleFx>> ripples_;
	std::vector<std::unique_ptr<PuffSet>> kicks_, sweeps_;
	std::map<int, std::unique_ptr<ActorTrack>> actors_;
	std::map<int, std::unique_ptr<BodyTrack>> bodies_;
	uint64_t serial_ = 0;
	int64_t lastTick_ = -1;
	float gust_ = 0.0f;
	Vec3 gustDir_{0.8f, 0.0f, 0.6f};
	float chipCd_ = 0.0f;
	float poolSteamT_ = 0.0f;
};

}  // namespace ffx
