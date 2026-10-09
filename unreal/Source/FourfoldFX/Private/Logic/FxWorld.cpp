// FourfoldFX logic island - world reactions (footfall dust, ground scars, pool splashes / ripples / steam, gusts).
// Owner: stream `fx`.
#include "FxWorld.h"

#include "FxFracture.h"
#include "FxMapping.h"
#include "FxMeshLib.h"
#include "FxOneShots.h"

#include <cmath>

namespace ffx {

namespace {

const Vec3 kWUp(0.0f, 1.0f, 0.0f);
constexpr int kVelHist = 8;               // sim ticks of velocity history (0.13 s): run starts / stops / pivots
constexpr uint32_t kChipSeeds[3] = {901u, 902u, 903u};
constexpr int kChipPieces = 4;
constexpr int kKickParticles = 8;     // foot kicks / skids (one small mesh each)
constexpr int kSweepParticles = 24;   // dust shockwave rings, gust sweeps

// The glue sent this fighter's bones and its feet are plausible (a missing bone reads as the world origin).
bool WHasAnchors(const Ctx& c, int actor, const Vec3& feet) {
	if (!c.in.anchors) return false;
	for (const FxAnchors& a : *c.in.anchors)
		if (a.actor == actor) {
			const Vec3& l = a.bones[static_cast<size_t>(Bone::FootL)];
			const Vec3& r = a.bones[static_cast<size_t>(Bone::FootR)];
			return l.distance_squared_to(feet) < 4.0f && r.distance_squared_to(feet) < 4.0f;
		}
	return false;
}

// What a fighter's steps throw up, from ActorView.surface ("stone", "metal", "water", "puddle", "zone:<surface>").
enum class WStep : uint8_t { Dust, Sand, Splash, None };
WStep WStepOf(const std::string& surface) {
	if (surface == "puddle") return WStep::Splash;
	if (surface == "metal" || surface == "water") return WStep::None;
	if (surface.compare(0, 5, "zone:") == 0) {
		if (surface.find("sand") != std::string::npos) return WStep::Sand;
		if (surface.find("ice") != std::string::npos || surface.find("mud") != std::string::npos ||
		    surface.find("slick") != std::string::npos || surface.find("oil") != std::string::npos)
			return WStep::None;
	}
	return WStep::Dust;
}

float WSpeedXZ(const Vec3& v) { return std::sqrt(v.x * v.x + v.z * v.z); }

Vec3 WFlatDir(const Vec3& v, const Vec3& fallback) {
	const Vec3 f(v.x, 0.0f, v.z);
	return f.length_squared() > 1e-6f ? Norm(f) : fallback;
}

// One shared "is this the hot / fire family" test for steam over the pool and scorch looks.
bool WFiery(Fam f) { return f == Fam::Flame || f == Fam::Blue || f == Fam::Blast || f == Fam::Magma; }

float WKindLife(const WorldSettings& w, ScarKind k) {
	switch (k) {
		case ScarKind::Crack: return w.scarLife;
		case ScarKind::Scorch: return w.scorchLife;
		case ScarKind::Bolt: return w.boltLife;
		case ScarKind::Wet: return w.wetLife;
		case ScarKind::Sand: return w.scarLife * 0.8f;
		case ScarKind::Frost: return w.scarLife * 0.6f;
		default: return w.scarLife;
	}
}

// Takes the first idle item, else (or when `cap` items already run) recycles the oldest running one.
template <typename T>
T& WAcquire(std::vector<std::unique_ptr<T>>& pool, int cap, uint64_t& serial) {
	cap = ClampI(cap, 1, static_cast<int>(pool.size()));
	int running = 0;
	T* idle = nullptr;
	T* oldest = nullptr;
	for (auto& it : pool) {
		if (it->active) {
			++running;
			if (!oldest || it->serial < oldest->serial) oldest = it.get();
		} else if (!idle) {
			idle = it.get();
		}
	}
	T* pick = (running >= cap || !idle) ? oldest : idle;
	if (!pick) pick = pool.front().get();
	pick->active = true;
	pick->age = 0.0f;
	pick->serial = ++serial;
	return *pick;
}

}  // namespace

float ScarStyle(ScarKind k) {
	switch (k) {
		case ScarKind::Crack: return 9.0f;
		case ScarKind::Scorch: return 10.0f;
		case ScarKind::Bolt: return 11.0f;
		case ScarKind::Wet: return 8.0f;
		case ScarKind::Sand: return 7.0f;
		case ScarKind::Frost: return 6.0f;
		default: return 9.0f;
	}
}

// =============================================================================================== pooled effects
struct WorldFx::ScarFx {
	bool active = false;
	float age = 0.0f;
	uint64_t serial = 0;
	ScarKind kind = ScarKind::Crack;
	Vec3 pos;
	float radius = 0.5f, yaw = 0.0f, heat0 = 0.0f, life = 10.0f, seed = 0.0f;
	uint32_t key = 0, keyLight = 0;

	void Step(Ctx& c) {
		age += c.dt;
		if (age >= life) {
			active = false;
			return;
		}
		const float t = age / life;
		float fade = 1.0f, heat = 0.0f;
		if (kind == ScarKind::Wet) {
			// a wet mark dries from its edges inward (Heat = dryness in the wet style), the last bit fades
			heat = Smooth(0.04f, 0.94f, t);
			fade = Sat(age / 0.12f) * (1.0f - Smooth(0.86f, 1.0f, t));
		} else {
			const float inTime = kind == ScarKind::Crack || kind == ScarKind::Bolt ? 0.04f : 0.22f;
			fade = Sat(age / inTime) * (1.0f - Smooth(0.62f, 1.0f, t));
			const float tau = kind == ScarKind::Crack ? 2.5f : (kind == ScarKind::Scorch ? 1.4f : 0.22f);
			heat = heat0 * std::exp(-age / tau);
		}
		Xform x;
		// a millimetre apart per scar: overlapping decals keep a stable order
		x.pos = pos + Vec3(0.0f, 0.012f + 0.0012f * static_cast<float>(serial % 5u), 0.0f);
		x.basis = Basis::YawY(yaw).Scaled(radius, 1.0f, radius);
		DrawItem& it = c.out.Add(key, MatSlot::Ground, &meshlib::GroundQuad(), x);
		it.params.Set(P::Style, ScarStyle(kind));
		it.params.Set(P::Fade, fade);
		it.params.Set(P::Phase, age);
		it.params.Set(P::Heat, heat);
		it.params.Set(P::Seed, seed);
		it.sortPriority = -3;
		if ((kind == ScarKind::Crack || kind == ScarKind::Scorch) && heat > 0.12f)
			c.out.Light(keyLight, pos + Vec3(0.0f, 0.35f, 0.0f), Color(1.0f, 0.42f, 0.12f), heat * fade * 0.7f, radius * 2.8f, 0.7f);
	}
};

struct WorldFx::RippleFx {
	bool active = false;
	float age = 0.0f;   // < 0: starts later
	uint64_t serial = 0;
	Vec3 pos;
	float r0 = 0.1f, r1 = 1.2f, life = 1.7f, strength = 1.0f, seed = 0.0f;
	bool inward = false;
	uint32_t key = 0;

	void Step(Ctx& c) {
		age += c.dt;
		if (age < 0.0f) return;
		if (age >= life) {
			active = false;
			return;
		}
		const float t = age / life;
		const float e = inward ? t * t * (3.0f - 2.0f * t) : 1.0f - (1.0f - t) * (1.0f - t);
		const float r = Lerp(r0, r1, e);
		const float half = MaxF(r0, r1) * 1.2f + 0.35f;   // fixed quad: the band keeps its metre width as it grows
		const float w = (0.05f + 0.035f * strength) * (1.0f + 0.7f * t);
		Xform x;
		x.pos = pos;
		x.basis = Basis::Identity().Scaled(half, 1.0f, half);
		DrawItem& it = c.out.Add(key, MatSlot::Ring, &meshlib::GroundQuad(), x);
		const WorldSettings& ws = c.cfg.world;
		it.params.Set(PV::Color, Linear(ws.rippleColor));
		it.params.Set(P::Style, 5.0f);   // ring material: water ripple
		it.params.Set(P::Radius, r / half);
		it.params.Set(P::Width, w / half);
		it.params.Set(P::Phase, age);
		it.params.Set(P::Opacity, Sat(strength) * std::pow(1.0f - t, 1.3f) * Smooth(0.0f, 0.08f, t));
		it.params.Set(P::Cover, 0.55f);
		it.params.Set(P::Glow, ws.rippleGlow);
		it.params.Set(P::Seed, seed);
		it.sortPriority = 2;
	}
};

struct WorldFx::PuffSet {
	bool active = false;
	float age = 0.0f;
	uint64_t serial = 0;
	ParticleSet ps;
	ParticleLook look;
	ParticlePhysics ph;
	MeshData mesh;
	uint32_t key = 0;
	float dur = 1.0f;
	float swirl = 0.0f;   // sweeps: lateral curl (m/s^2) around the gust direction
	Vec3 side;
	Color tint;

	void Step(Ctx& c) {
		age += c.dt;
		if (swirl != 0.0f && c.dt > 0.0f)
			for (Particle& p : ps.Items())
				if (p.alive && p.age >= 0.0f)
					p.v += side * (std::sin(p.age * 6.5f + p.rnd * kFxTau) * swirl * c.dt) + kWUp * (0.35f * swirl * c.dt * p.rnd);
		ps.Step(c.dt, ph);
		if (age >= dur || !ps.AnyAlive()) {
			active = false;
			return;
		}
		ps.Build(mesh, c.in.cam.pos, c.in.cam.up, look);
		mesh.Commit();
		DrawItem& it = c.out.Add(key, MatSlot::Smoke, &mesh);
		it.params.Set(PV::Color, Linear(tint));
		it.params.flipbook = Flipbook::DustPuff;
		it.params.Set(P::Erosion, 0.55f);
		it.sortPriority = 1;
	}
};

struct WorldFx::ActorTrack {
	Vec3 vel[kVelHist];
	int velN = 0, velHead = 0;
	bool init = false, grounded = true, inWater = false;
	float footY[2] = {0.0f, 0.0f}, footVy[2] = {0.0f, 0.0f}, footCd[2] = {0.0f, 0.0f};
	bool footInit = false;
	float stride = 0.0f;
	int strideFoot = 0;
	float skidCd = 0.0f, rippleT = 0.0f, stompCd = 0.0f, drawCd = 0.0f;
	double time = 0.0, drawLast = -10.0;
	void Push(const Vec3& v) {
		vel[velHead] = v;
		velHead = (velHead + 1) % kVelHist;
		velN = MinI(velN + 1, kVelHist);
	}
	const Vec3& Oldest() const { return vel[velN < kVelHist ? 0 : velHead]; }
	const Vec3& Newest() const { return vel[(velHead + kVelHist - 1) % kVelHist]; }
};

struct WorldFx::BodyTrack {
	Vec3 pos;
	float radius = 0.3f;
	bool above = true;     // its bottom was above the pool surface (or it was outside the pool) last tick
	bool puddle = false;
	bool seen = false;
	float steamT = 0.0f, wakeT = 0.0f;
};

// =============================================================================================== WorldFx
WorldFx::WorldFx() {
	for (int i = 0; i < 16; ++i) scars_.push_back(std::make_unique<ScarFx>());
	for (int i = 0; i < 12; ++i) ripples_.push_back(std::make_unique<RippleFx>());
	for (int i = 0; i < 10; ++i) {
		kicks_.push_back(std::make_unique<PuffSet>());
		kicks_.back()->ps.Reset(kKickParticles, 700u + static_cast<uint32_t>(i));
	}
	for (int i = 0; i < 6; ++i) {
		sweeps_.push_back(std::make_unique<PuffSet>());
		sweeps_.back()->ps.Reset(kSweepParticles, 800u + static_cast<uint32_t>(i));
	}
	// impact chips: three cached piece sets, built before the first gameplay frame
	static const bool kChipsWarm = [] {
		for (uint32_t s : kChipSeeds) meshlib::RockPieces(s, kChipPieces);
		return true;
	}();
	(void)kChipsWarm;
}

WorldFx::~WorldFx() = default;

void WorldFx::Reset() {
	for (auto& s : scars_) s->active = false;
	for (auto& r : ripples_) r->active = false;
	for (auto& p : kicks_) p->active = false;
	for (auto& p : sweeps_) p->active = false;
	actors_.clear();
	bodies_.clear();
	lastTick_ = -1;
	gust_ = 0.0f;
	chipCd_ = 0.0f;
	poolSteamT_ = 0.0f;
}

int WorldFx::ActiveCount() const {
	int n = 0;
	for (const auto& s : scars_) n += s->active ? 1 : 0;
	for (const auto& r : ripples_) n += r->active ? 1 : 0;
	for (const auto& p : kicks_) n += p->active ? 1 : 0;
	for (const auto& p : sweeps_) n += p->active ? 1 : 0;
	return n;
}

bool WorldFx::InPool(const Ctx& c, const Vec3& p, float margin) const {
	const ff::ArenaView* a = c.in.arena;
	if (!a || a->pool_max.x <= a->pool_min.x) return false;
	return p.x >= a->pool_min.x - margin && p.x <= a->pool_max.x + margin && p.z >= a->pool_min.y - margin &&
	       p.z <= a->pool_max.y + margin;
}

float WorldFx::PoolLevel(const Ctx& c) const { return c.in.arena ? c.in.arena->pool_level : -0.05f; }

WorldFx::PuffSet& WorldFx::NewPuff(Ctx& c, bool big) {
	// kicks: the scuff cap; dust rings / gust sweeps: about half of it (each is three times the particles)
	PuffSet& p = big ? WAcquire(sweeps_, MaxI(2, c.q.scuffs / 2 + 1), serial_) : WAcquire(kicks_, c.q.scuffs, serial_);
	p.ps.Reset(big ? kSweepParticles : kKickParticles, c.rng.Next());
	p.key = c.keys.New();
	p.look = ParticleLook();
	p.look.frames = FlipbookFrames(Flipbook::DustPuff);
	p.look.fadeIn = 0.12f;
	p.look.fadeOut = 0.6f;
	p.ph = ParticlePhysics();
	p.swirl = 0.0f;
	return p;
}

WorldFx::ScarFx& WorldFx::NewScar(Ctx& c) {
	ScarFx& s = WAcquire(scars_, c.q.scars, serial_);
	s.key = c.keys.New();
	s.keyLight = c.keys.New();
	return s;
}

WorldFx::RippleFx& WorldFx::NewRipple(Ctx& c) {
	RippleFx& r = WAcquire(ripples_, c.q.ripples, serial_);
	r.key = c.keys.New();
	return r;
}

// ----------------------------------------------------------------------------------------------- cues
void WorldFx::Scar(Ctx& c, const Vec3& p, ScarKind kind, float radius, float heat, float life) {
	if (InPool(c, p, 0.1f)) return;   // the water shows ripples instead
	const float gy = c.GroundAt(p.x, p.z, p.y + 0.5f);
	if (p.y - gy > 1.5f) return;      // high in the air: nothing reaches the floor
	radius = Clamp(radius * c.cfg.world.scarScale, 0.15f, 3.5f);
	const Vec3 g(p.x, gy, p.z);
	if (kind == ScarKind::Crack && c.in.arena) {
		// the metal plate dents, it does not crack
		const ff::ArenaView& a = *c.in.arena;
		if (p.x > a.metal_min.x && p.x < a.metal_max.x && p.z > a.metal_min.y && p.z < a.metal_max.y) return;
	}
	// merge into a live scar of the same kind close by (several cues of one hit, repeated pops)
	for (auto& s : scars_) {
		if (!s->active || s->kind != kind) continue;
		const float d = std::sqrt((s->pos.x - g.x) * (s->pos.x - g.x) + (s->pos.z - g.z) * (s->pos.z - g.z));
		if (d < 0.5f * MaxF(s->radius, radius) && std::fabs(s->pos.y - g.y) < 0.2f) {
			s->radius = MinF(MaxF(s->radius, radius) + 0.08f * radius, 3.5f);
			s->heat0 = MaxF(s->heat0 * std::exp(-s->age / 2.0f), heat);
			s->age = MinF(s->age, kind == ScarKind::Wet ? 0.0f : 0.3f);
			return;
		}
	}
	ScarFx& s = NewScar(c);
	s.kind = kind;
	s.pos = g;
	s.radius = radius;
	s.yaw = c.rng.F01() * kFxTau;
	s.heat0 = Sat(heat);
	s.life = MaxF(life > 0.0f ? life : WKindLife(c.cfg.world, kind), 0.5f);
	s.seed = c.rng.Range(0.0f, 9.0f);
}

void WorldFx::Ripples(Ctx& c, const Vec3& p, float r1, float strength, int rings, bool inward) {
	const float level = PoolLevel(c);
	rings = ClampI(rings, 1, 3);
	for (int i = 0; i < rings; ++i) {
		RippleFx& r = NewRipple(c);
		const float fi = static_cast<float>(i);
		r.pos = Vec3(p.x, level + 0.006f, p.z);
		r.inward = inward;
		const float outer = MaxF(r1, 0.3f) * (1.0f + 0.45f * fi);
		r.r0 = inward ? outer : 0.08f + 0.05f * fi;
		r.r1 = inward ? 0.06f : outer;
		r.life = c.cfg.world.rippleLife * (1.0f + 0.25f * fi) * (inward ? 0.55f : 1.0f);
		r.strength = Clamp(strength * (1.0f - 0.25f * fi), 0.05f, 1.5f);
		r.seed = c.rng.Range(0.0f, 9.0f);
		r.age = -0.16f * fi;
	}
}

void WorldFx::DustRing(Ctx& c, const Vec3& p, float radius, float strength, const Color& tint) {
	if (InPool(c, p, 0.0f) && p.y < PoolLevel(c) + 0.6f) return;
	strength = Clamp(strength, 0.1f, 2.0f);
	const float gy = c.GroundAt(p.x, p.z, p.y + 0.5f);
	if (p.y - gy > 1.5f) return;
	PuffSet& s = NewPuff(c, true);
	s.dur = 1.6f;
	s.tint = tint;
	s.ph.drag = 3.0f;
	s.ph.buoyancy = 0.25f;
	s.ph.groundY = gy + 0.02f;
	s.ph.bounce = 0.0f;
	const int n = MaxI(6, static_cast<int>(std::lround(Lerp(10.0f, 20.0f, Sat(strength / 1.5f)) * c.q.particles)));
	Rng& r = s.ps.R();
	const float speed = (2.2f + 2.4f * radius) * (0.6f + 0.4f * Sat(strength));
	for (int i = 0; i < n; ++i) {
		Particle* q = s.ps.Spawn();
		if (!q) break;
		const float a = kFxTau * (static_cast<float>(i) + r.F01() * 0.7f) / static_cast<float>(n);
		const Vec3 d(std::cos(a), 0.0f, std::sin(a));
		q->p = Vec3(p.x, gy + 0.08f, p.z) + d * (0.15f + 0.1f * radius * r.F01());
		q->v = d * (speed * r.Range(0.75f, 1.15f)) + kWUp * r.Range(0.2f, 0.7f);
		q->life = r.Range(0.85f, 1.35f);
		const float sz = Lerp(0.3f, 0.55f, r.F01()) * (0.7f + 0.35f * Sat(strength));
		q->size0 = sz * 0.5f;
		q->size1 = sz * 1.8f;
		q->rot = r.Range(0.0f, kFxTau);
		q->rotSpeed = r.Range(-0.5f, 0.5f);
		q->c0 = Color(1, 1, 1, 0.42f * Clamp(0.6f + 0.4f * strength, 0.5f, 1.2f));
		q->c1 = Color(0.95f, 0.95f, 0.95f, 0.12f);
		q->frame0 = r.Range(0.0f, 6.0f);
	}
}

void WorldFx::Kick(Ctx& c, const Vec3& p, const Vec3& dir, float strength, float alphaScale, const Color& tint) {
	strength = Clamp(strength, 0.1f, 1.6f);
	const float gy = c.GroundAt(p.x, p.z, p.y + 0.5f);
	if (p.y - gy > 0.6f) return;
	PuffSet& s = NewPuff(c, false);
	s.dur = 1.1f;
	s.tint = tint;
	s.ph.drag = 3.4f;
	s.ph.buoyancy = 0.18f;
	s.ph.groundY = gy + 0.02f;
	s.ph.bounce = 0.0f;
	const Vec3 d = WFlatDir(dir, Vec3(0.0f, 0.0f, 1.0f));
	const Vec3 side(d.z, 0.0f, -d.x);
	const int n = MaxI(2, static_cast<int>(std::lround(Lerp(3.0f, 7.0f, Sat(strength)) * c.q.particles)));
	Rng& r = s.ps.R();
	for (int i = 0; i < n; ++i) {
		Particle* q = s.ps.Spawn();
		if (!q) break;
		q->p = Vec3(p.x, gy + 0.05f, p.z) + side * r.Signed() * 0.08f;
		q->v = d * (r.Range(0.6f, 1.7f) * (0.6f + 0.6f * strength)) + side * (r.Signed() * 0.45f) + kWUp * r.Range(0.25f, 0.75f);
		q->life = r.Range(0.55f, 0.95f);
		const float sz = Lerp(0.12f, 0.2f, r.F01()) * (0.7f + 0.5f * strength);
		q->size0 = sz;
		q->size1 = sz * 2.8f;
		q->rot = r.Range(0.0f, kFxTau);
		q->rotSpeed = r.Range(-0.8f, 0.8f);
		q->c0 = Color(1, 1, 1, Clamp(c.cfg.world.footfallAlpha * alphaScale, 0.02f, 0.8f));
		q->c1 = Color(0.95f, 0.95f, 0.95f, 0.0f);
		q->frame0 = r.Range(0.0f, 6.0f);
	}
}

void WorldFx::Gust(Ctx& c, const Vec3& origin, const Vec3& dir, float range, float strength) {
	strength = Clamp(strength, 0.0f, 1.5f);
	const Vec3 d = WFlatDir(dir, gustDir_);
	// arena wind (MPC): the strongest recent gust wins, its direction blends in by strength
	const float g = MinF(strength, c.cfg.world.gustMax);
	const float w = Sat(g / MaxF(gust_ + g, 1e-3f));
	gustDir_ = WFlatDir(gustDir_ * (1.0f - w) + d * w, d);
	gust_ = MaxF(gust_, g);
	// floor dust swept along the gust while it runs low enough to touch the floor
	const float gy = c.GroundAt(origin.x, origin.z, origin.y + 0.5f);
	const float h = origin.y - gy;
	if (h > 2.8f || strength < 0.05f) return;
	const float k = strength * (1.0f - 0.45f * Sat(h / 2.8f));
	range = Clamp(range, 1.0f, 9.0f);
	const Vec3 g0(origin.x, gy, origin.z);
	const float level = PoolLevel(c);
	// over the pool: cat's-paw ruffles instead of dust
	int ruffles = 0;
	for (int i = 1; i <= 3; ++i) {
		const Vec3 at = g0 + d * (range * 0.28f * static_cast<float>(i));
		if (InPool(c, at) && ruffles < 2) {
			Ripples(c, Vec3(at.x, level, at.z), 0.5f + 0.25f * static_cast<float>(i), 0.35f * k, 1);
			++ruffles;
		}
	}
	PuffSet& s = NewPuff(c, true);
	s.dur = 1.8f;
	s.tint = c.cfg.world.dust;
	s.ph.drag = 1.4f;
	s.ph.buoyancy = 0.12f;
	s.ph.groundY = gy + 0.02f;
	s.ph.bounce = 0.0f;
	s.side = Vec3(d.z, 0.0f, -d.x);
	s.swirl = 3.2f * (0.5f + 0.5f * Sat(k));
	const int n = MaxI(6, static_cast<int>(std::lround(Lerp(10.0f, 22.0f, Sat(k)) * c.q.particles)));
	Rng& r = s.ps.R();
	for (int i = 0; i < n; ++i) {
		const float u = r.F01();
		const Vec3 at = g0 + d * (0.5f + range * 0.65f * u) + s.side * (r.Signed() * (0.3f + 0.35f * u));
		if (InPool(c, at)) continue;
		Particle* q = s.ps.Spawn();
		if (!q) break;
		q->p = Vec3(at.x, c.GroundAt(at.x, at.z, gy + 0.5f) + r.Range(0.05f, 0.3f), at.z);
		q->v = d * (r.Range(2.4f, 5.0f) * (0.55f + 0.45f * k)) + kWUp * r.Range(0.1f, 0.6f);
		q->life = r.Range(0.9f, 1.5f);
		q->age = -u * 0.25f;   // the far end lifts a moment later (the gust travels)
		const float sz = Lerp(0.22f, 0.45f, r.F01());
		q->size0 = sz * 0.5f;
		q->size1 = sz * 1.9f;
		q->rot = r.Range(0.0f, kFxTau);
		q->rotSpeed = r.Range(-1.5f, 1.5f);
		q->c0 = Color(1, 1, 1, 0.30f * Clamp(0.5f + 0.5f * k, 0.4f, 1.0f));
		q->c1 = Color(0.95f, 0.95f, 0.95f, 0.0f);
		q->frame0 = r.Range(0.0f, 6.0f);
	}
	// leaves / petals kicked up from the courtyard floor
	if (c.in.quality >= 1 && k > 0.35f) {
		const Vec3 at = g0 + d * (range * 0.45f);
		if (!InPool(c, at)) c.fx.Burst(c, at + Vec3(0.0f, 0.1f, 0.0f), Norm(d + kWUp * 0.8f), 0.35f + 0.3f * k, Burst::Leaves);
	}
}

void WorldFx::Chips(Ctx& c, const Vec3& g, float strength) {
	if (!c.cfg.world.chips || !c.in.physicsDebris || c.q.rockPieces < 2 || chipCd_ > 0.0f) return;
	chipCd_ = 0.5f;
	const uint32_t seed = kChipSeeds[c.rng.Next() % 3u];
	FractureReq& r = c.out.Fracture();
	r.pieces = &meshlib::RockPieces(seed, kChipPieces);
	r.xform.basis = Basis::AxisAngle(Norm(Vec3(c.rng.Signed(), 1.0f, c.rng.Signed())), c.rng.F01() * kFxTau).Scaled(0.14f + 0.05f * Sat(strength));
	r.xform.pos = g + kWUp * 0.1f;
	r.mat = MatSlot::Rock;
	r.params.Set(P::Seed, static_cast<float>(seed % 97u) + 0.5f);
	r.params.Set(P::Rise, 1.0f);
	r.params.Set(P::Detail, 1.0f);
	r.params.Set(P::Fade, 1.0f);
	r.params.Set(PV::Tint, Color(1.45f, 1.4f, 1.32f));   // pale chips of the granite floor
	r.vel = kWUp * (2.2f + 1.6f * Sat(strength));
	r.origin = g - kWUp * 0.25f;
	r.burst = 1.6f + 1.2f * Sat(strength);
	r.scale = 0.9f;
	r.life = 1.6f;
	r.seed = HashCombine(seed, static_cast<uint32_t>(c.in.curr ? c.in.curr->tick : 0));
}

void WorldFx::PoolHit(Ctx& c, const Vec3& at, Fam fam, float strength) {
	strength = Clamp(strength, 0.1f, 1.8f);
	const Vec3 s(at.x, PoolLevel(c), at.z);
	if (WFiery(fam)) {
		// fire / hot stone meets water: a hiss of steam, a smaller splash
		c.fx.Steam(c, s + Vec3(0.0f, 0.15f, 0.0f), Clamp(0.45f + 0.4f * strength, 0.3f, 1.0f));
		if (fam == Fam::Magma) c.fx.Splash(c, s, kWUp, 0.4f + 0.4f * strength);
		Ripples(c, s, 0.8f + 0.6f * strength, 0.6f * strength, 1);
		return;
	}
	c.fx.Splash(c, s, kWUp, 0.45f + 0.55f * strength);
	if (strength > 1.0f) c.fx.Splash(c, s, kWUp, 1.2f + 0.3f * (strength - 1.0f));   // a heavy body: a tall crown
	Ripples(c, s, 0.9f + 0.8f * strength, 0.5f + 0.4f * strength, strength > 0.6f ? 2 : 1);
}

void WorldFx::Impact(Ctx& c, const Vec3& p, Fam fam, float strength, float radius) {
	strength = Clamp(strength, 0.0f, 2.0f);
	if (strength < 0.05f) return;
	const float level = PoolLevel(c);
	if (InPool(c, p, 0.05f) && p.y < level + 1.2f) {
		PoolHit(c, Vec3(p.x, level, p.z), fam, strength);
		return;
	}
	const float gy = c.GroundAt(p.x, p.z, p.y + 0.5f);
	if (p.y - gy > 1.5f) return;
	const Vec3 g(p.x, gy, p.z);
	const float r = radius > 0.0f ? radius : 0.4f + 0.45f * strength;
	const Color& dust = c.cfg.world.dust;
	switch (fam) {
		case Fam::Stone:
		case Fam::Metal:
		case Fam::Glass:
		case Fam::Magma:
			if (strength >= 0.35f) Scar(c, g, ScarKind::Crack, fam == Fam::Metal ? r * 0.6f : r, fam == Fam::Magma ? 1.0f : 0.0f);
			if (fam == Fam::Magma) Scar(c, g, ScarKind::Scorch, r * 1.1f, 0.6f);
			DustRing(c, g, r * 1.5f, strength, dust);
			if (strength >= 0.8f && fam != Fam::Metal) Chips(c, g, strength);
			break;
		case Fam::Sand:
			Scar(c, g, ScarKind::Sand, r * 1.3f);
			DustRing(c, g, r * 1.4f, strength, c.cfg.DustColor(Fam::Sand));
			break;
		case Fam::Flame:
		case Fam::Blue:
		case Fam::Blast:
			Scar(c, g, ScarKind::Scorch, r, 1.0f);
			if (fam == Fam::Blast) DustRing(c, g, r * 1.4f, strength * 0.8f, dust);
			break;
		case Fam::Lightning: LightningStrike(c, g, strength); break;
		case Fam::Water:
		case Fam::Mist: Scar(c, g, ScarKind::Wet, r * 1.15f); break;
		case Fam::Ice: Scar(c, g, ScarKind::Frost, r); break;
		case Fam::Plant: break;
		default: DustRing(c, g, r * 1.4f, strength * 0.8f, dust); break;
	}
}

void WorldFx::LightningStrike(Ctx& c, const Vec3& p, float strength) {
	strength = Clamp(strength, 0.2f, 1.6f);
	const float fl = MaxF(c.in.flashes * c.cfg.flashScale, 0.3f);
	if (InPool(c, p, 0.05f) && p.y < PoolLevel(c) + 1.2f) {
		// a strike into the water boils a little of it off and rings the surface
		const Vec3 s(p.x, PoolLevel(c), p.z);
		c.fx.Steam(c, s + Vec3(0.0f, 0.1f, 0.0f), 0.5f * strength);
		Ripples(c, s, 1.6f * strength, 0.8f, 2);
		c.fx.LightPulse(c, s + Vec3(0.0f, 0.4f, 0.0f), Color(0.72f, 0.80f, 1.0f), 2.4f * strength * fl, 8.0f, 0.16f);
		return;
	}
	const float gy = c.GroundAt(p.x, p.z, p.y + 0.5f);
	if (p.y - gy > 0.8f) return;
	const Vec3 g(p.x, gy, p.z);
	Scar(c, g, ScarKind::Bolt, 0.5f + 0.35f * strength, 1.0f);
	c.fx.Burst(c, g + Vec3(0.0f, 0.04f, 0.0f), kWUp, 0.55f + 0.3f * strength, Burst::Sparks);
	DustRing(c, g, 0.6f + 0.3f * strength, 0.5f * strength, c.cfg.world.dust);
	// the strike lights the surroundings for a moment (low, so walls / fighters nearby catch it)
	c.fx.LightPulse(c, g + Vec3(0.0f, 0.6f, 0.0f), Color(0.72f, 0.80f, 1.0f), 2.6f * strength * fl, 8.0f, 0.16f);
}

void WorldFx::Landing(Ctx& c, int actor, float speed) {
	const ff::ActorView* a = c.Actor(actor);
	if (!a) return;
	const Vec3 feet = c.ActorPos(actor);
	const WorldSettings& ws = c.cfg.world;
	if (a->in_water || (InPool(c, feet, -0.1f) && feet.y < PoolLevel(c) + 0.35f)) {
		if (speed > 1.0f) PoolHit(c, feet, Fam::Water, 0.4f + speed / 8.0f);
		return;
	}
	if (speed < ws.landSpeed) return;
	const float k = Sat((speed - ws.landSpeed) / 7.0f);
	if (a->surface == "metal") {
		c.fx.Burst(c, feet, kWUp, 0.3f + 0.4f * k, Burst::Sparks);
		return;
	}
	DustRing(c, feet, 0.5f + 0.5f * k, 0.35f + 0.8f * k, ws.dust);
	if (speed >= ws.landSpeed * 2.8f) {
		Scar(c, feet, ScarKind::Crack, 0.45f + 0.3f * k);
		Chips(c, feet, 0.6f + 0.4f * k);
	}
}

void WorldFx::Stomp(Ctx& c, int actor, float strength) {
	const ff::ActorView* a = c.Actor(actor);
	if (!a || !a->grounded) return;
	auto it = actors_.find(actor);
	if (it != actors_.end()) {
		if (it->second->stompCd > 0.0f) return;
		it->second->stompCd = 0.4f;
	}
	const Vec3 feet = c.ActorPos(actor);
	if (a->in_water || InPool(c, feet, -0.1f)) {
		Ripples(c, feet, 1.4f + 0.5f * strength, 0.8f, 2);
		c.fx.Splash(c, Vec3(feet.x, PoolLevel(c), feet.z), kWUp, 0.5f + 0.3f * strength);
		return;
	}
	strength = Clamp(strength, 0.2f, 1.5f);
	DustRing(c, feet, 0.8f + 0.5f * strength, 0.5f + 0.5f * strength, c.cfg.world.dust);
	if (strength >= 0.55f) Scar(c, feet, ScarKind::Crack, 0.45f + 0.3f * strength);
}

void WorldFx::DrawFromPool(Ctx& c, const Vec3& at, int actor) {
	if (!InPool(c, at, 0.2f) || at.y > PoolLevel(c) + 1.5f) return;
	auto& tr = actors_[actor];
	if (!tr) tr = std::make_unique<ActorTrack>();
	if (tr->drawCd > 0.0f) return;
	tr->drawCd = 0.32f;
	const Vec3 s(at.x, PoolLevel(c), at.z);
	// the surface is pulled toward the draw point (rings closing in), then rings spread out again
	const bool fresh = tr->time - tr->drawLast > 0.8;
	tr->drawLast = tr->time;
	if (fresh) Ripples(c, s, 1.1f, 0.6f, 1, true);
	Ripples(c, s, 0.9f, 0.45f, 1);
}

// ----------------------------------------------------------------------------------------------- trackers
void WorldFx::Update(Ctx& c) {
	chipCd_ = MaxF(0.0f, chipCd_ - c.dt);
	if (c.in.curr) {
		const bool newTick = c.in.curr->tick != lastTick_;
		lastTick_ = c.in.curr->tick;
		TrackActors(c, newTick);
		TrackBodies(c, newTick);
	}
	for (auto& s : scars_)
		if (s->active) s->Step(c);
	for (auto& r : ripples_)
		if (r->active) r->Step(c);
	for (auto& p : kicks_)
		if (p->active) p->Step(c);
	for (auto& p : sweeps_)
		if (p->active) p->Step(c);
	// arena wind: decays back to calm
	const float decay = MaxF(c.cfg.world.gustDecay, 0.05f);
	gust_ *= std::exp(-c.dt / decay);
	if (gust_ < 0.004f) gust_ = 0.0f;
	float floorGust = 0.0f;
	Vec3 floorDir = gustDir_;
	if (c.in.curr)
		for (const ff::BodyView& b : c.in.curr->bodies)
			if (b.form == ff::Form::Zone && b.zone_radius > 0.5f && SelectView(b).kind == ViewKind::Vortex) {
				floorGust = MaxF(floorGust, c.cfg.world.tornadoGust * Life01(b));
				floorDir = WFlatDir(b.vel, floorDir);
			}
	if (floorGust > gust_) gustDir_ = WFlatDir(gustDir_ * 0.97f + floorDir * 0.03f, floorDir);
	EnvState& env = c.out.env;
	env.windGust = c.cfg.world.mpc ? MinF(MaxF(gust_, floorGust), MaxF(c.cfg.world.gustMax, 0.0f)) : 0.0f;
	env.windDirX = gustDir_.x;   // Unreal X = sim x
	env.windDirY = gustDir_.z;   // Unreal Y = sim z
}

void WorldFx::TrackActors(Ctx& c, bool newTick) {
	const ff::Snapshot& cur = *c.in.curr;
	const WorldSettings& ws = c.cfg.world;
	const float level = PoolLevel(c);
	for (auto it = actors_.begin(); it != actors_.end();) {
		if (!cur.FindActor(it->first)) it = actors_.erase(it);
		else ++it;
	}
	for (const ff::ActorView& a : cur.actors) {
		auto& slot = actors_[a.id];
		if (!slot) slot = std::make_unique<ActorTrack>();
		ActorTrack& tr = *slot;
		tr.time += static_cast<double>(c.dt);
		tr.skidCd = MaxF(0.0f, tr.skidCd - c.dt);
		tr.stompCd = MaxF(0.0f, tr.stompCd - c.dt);
		tr.drawCd = MaxF(0.0f, tr.drawCd - c.dt);
		const Vec3 feet = c.ActorPos(a.id);
		const float speed = WSpeedXZ(a.vel);
		const Vec3 vdir = WFlatDir(a.vel, Vec3(std::sin(a.facing), 0.0f, std::cos(a.facing)));
		const bool inWater = a.in_water || (InPool(c, feet, -0.1f) && feet.y < level + 0.35f);
		if (!tr.init) {
			tr.init = true;
			tr.inWater = inWater;
			tr.grounded = a.grounded;
		}
		// ---- the pool: stepping / falling in, wading
		if (inWater && !tr.inWater && (a.grounded || a.vel.y < 0.0f)) PoolHit(c, feet, Fam::Water, 0.35f + speed / 9.0f + MaxF(-a.vel.y, 0.0f) / 8.0f);
		tr.inWater = inWater;
		if (inWater) {
			tr.rippleT -= c.dt;
			if (tr.rippleT <= 0.0f) {
				const bool moving = speed > 0.8f;
				tr.rippleT = moving ? Clamp(1.4f / MaxF(speed, 1.0f), 0.22f, 0.5f) : 1.3f;
				Ripples(c, feet, moving ? 0.6f + 0.12f * speed : 0.5f, moving ? 0.4f + 0.06f * speed : 0.22f, 1);
				if (speed > 3.5f) c.fx.Splash(c, Vec3(feet.x, level, feet.z), Norm(vdir + kWUp * 2.0f), 0.2f + 0.05f * speed);
			}
		}
		const WStep step = WStepOf(a.surface);
		const bool dusty = a.grounded && !inWater && step != WStep::None && !a.gliding && !a.flying;
		const float earthy = a.element == 0 ? 1.25f : 1.0f;   // Earth fighters grip the floor: a little more grit
		const Color& tint = step == WStep::Sand ? c.cfg.DustColor(Fam::Sand) : ws.dust;
		// one step's reaction: a dust kick, or a little splash in a puddle
		auto stepFx = [&](const Vec3& at, const Vec3& dir, float k, float alpha) {
			if (step == WStep::Splash) c.fx.Splash(c, Vec3(at.x, c.GroundAt(at.x, at.z, at.y + 0.5f) + 0.02f, at.z), Norm(dir * 0.4f + kWUp), 0.12f + 0.12f * k);
			else Kick(c, at, dir, k, alpha, tint);
		};
		// ---- foot plants while running
		if (dusty && c.q.footfalls && speed >= ws.footfallSpeed && c.dt > 0.0f) {
			const float k = Sat((speed - ws.footfallSpeed) / 4.0f) * 0.6f + 0.25f;
			if (WHasAnchors(c, a.id, feet)) {
				for (int f = 0; f < 2; ++f) {
					const Vec3 fp = c.Anchor(a.id, f == 0 ? Bone::FootL : Bone::FootR);
					const float h = fp.y - c.GroundAt(fp.x, fp.z, fp.y + 0.5f);
					tr.footCd[f] = MaxF(0.0f, tr.footCd[f] - c.dt);
					if (!tr.footInit) {
						tr.footY[f] = h;
						tr.footVy[f] = 0.0f;
						continue;
					}
					const float vy = (h - tr.footY[f]) / c.dt;
					// planted: it was coming down and has stopped, close to the floor
					if (tr.footVy[f] < -0.25f && vy > -0.05f && h < 0.25f && tr.footCd[f] <= 0.0f) {
						tr.footCd[f] = 0.2f;
						stepFx(fp, vdir * -1.0f, k * earthy, 1.0f);
					}
					tr.footY[f] = h;
					tr.footVy[f] = Lerp(tr.footVy[f], vy, 0.6f);
				}
				tr.footInit = true;
			} else {
				// no animated feet (tests, clones without fighters): a stride clock, one plant every ~1.3 m
				tr.stride += speed * c.dt / 1.3f;
				if (tr.stride >= 1.0f) {
					tr.stride -= 1.0f;
					tr.strideFoot ^= 1;
					const Vec3 f(std::sin(a.facing), 0.0f, std::cos(a.facing));
					const Vec3 r(f.z, 0.0f, -f.x);
					stepFx(feet + r * (tr.strideFoot ? 0.14f : -0.14f), vdir * -1.0f, k * earthy, 1.0f);
				}
			}
		} else {
			tr.footInit = false;
		}
		// ---- run starts, stops and pivots (speed / direction change over the last 8 sim ticks)
		if (newTick) {
			tr.Push(Vec3(a.vel.x, 0.0f, a.vel.z));
			if (dusty && tr.velN >= kVelHist && tr.skidCd <= 0.0f) {
				const Vec3& vo = tr.Oldest();
				const Vec3& vn = tr.Newest();
				const float so = WSpeedXZ(vo), sn = WSpeedXZ(vn);
				const float sk = ws.skidSpeed;
				const Vec3 f(std::sin(a.facing), 0.0f, std::cos(a.facing));
				const Vec3 r(f.z, 0.0f, -f.x);
				if (so < 1.0f && sn > sk) {
					// a start: both feet push dirt back
					tr.skidCd = 0.35f;
					stepFx(feet - r * 0.14f, vn * -1.0f, 0.55f * earthy, 1.6f);
					stepFx(feet + r * 0.14f, vn * -1.0f, 0.45f * earthy, 1.4f);
				} else if (so > sk + 0.5f && sn < 1.0f) {
					// a stop: the skid throws dust forward
					tr.skidCd = 0.35f;
					stepFx(feet + WFlatDir(vo, f) * 0.25f, vo, 0.75f * earthy, 1.7f);
				} else if (so > sk && sn > sk && vo.dot(vn) < std::cos(1.2f) * so * sn) {
					// a sharp pivot: the outer foot digs in, dust keeps the old momentum
					tr.skidCd = 0.3f;
					const float outer = vo.cross(vn).y > 0.0f ? -1.0f : 1.0f;
					stepFx(feet + r * (0.16f * outer), vo, 0.65f * earthy, 1.6f);
				}
			}
		}
		tr.grounded = a.grounded;
	}
}

void WorldFx::TrackBodies(Ctx& c, bool newTick) {
	const ff::Snapshot& cur = *c.in.curr;
	const WorldSettings& ws = c.cfg.world;
	const float level = PoolLevel(c);
	const ff::ArenaView* ar = c.in.arena;
	const bool havePool = ar && ar->pool_max.x > ar->pool_min.x;
	poolSteamT_ = MaxF(0.0f, poolSteamT_ - c.dt);
	for (auto& kv : bodies_) kv.second->seen = false;
	for (const ff::BodyView& b : cur.bodies) {
		if (b.form == ff::Form::Pool) continue;
		auto& slot = bodies_[b.id];
		const bool fresh = !slot;
		if (!slot) slot = std::make_unique<BodyTrack>();
		BodyTrack& tr = *slot;
		tr.seen = true;
		tr.steamT = MaxF(0.0f, tr.steamT - c.dt);
		tr.wakeT = MaxF(0.0f, tr.wakeT - c.dt);
		const Fam fam = FamOf(b);
		const bool overPool = havePool && InPool(c, b.pos);
		const bool solidish = b.form == ff::Form::Chunk || b.form == ff::Form::Blob || b.form == ff::Form::Shard ||
		                      b.form == ff::Form::Stream;
		const float bottom = b.pos.y - (b.form == ff::Form::Stream ? 0.0f : b.radius * 0.5f);
		if (b.form == ff::Form::Puddle) {
			tr.puddle = true;
			tr.pos = b.pos;
			tr.radius = MaxF(b.radius, 0.2f);
		}
		if (fresh) {
			// water raised out of the pool (draws, waves, whips formed from it)
			if (overPool && (fam == Fam::Water || fam == Fam::Ice) && std::fabs(b.pos.y - level) < 1.2f && b.form != ff::Form::Puddle) {
				c.fx.Splash(c, Vec3(b.pos.x, level, b.pos.z), kWUp, 0.45f);
				Ripples(c, b.pos, 1.0f + b.radius, 0.6f, 2);
			}
			tr.above = !overPool || bottom > level;
			tr.pos = b.pos;
			if (!tr.puddle) tr.radius = b.radius;
			continue;
		}
		if (newTick && overPool && solidish) {
			const bool above = bottom > level;
			if (tr.above && !above && b.vel.y < 0.5f) {
				// entering the water: weight and fall speed decide the splash
				const float vy = MaxF(-b.vel.y, 0.0f) + 0.3f * WSpeedXZ(b.vel);
				const float k = Clamp(std::sqrt(MaxF(b.mass, 1.0f)) / 5.0f * Sat(vy / 7.0f + 0.25f), 0.15f, 1.7f);
				Fam hit = fam;
				if (fam == Fam::Stone && Heat01(b) > 0.4f) hit = Fam::Magma;   // a hot stone hisses
				PoolHit(c, Vec3(b.pos.x, level, b.pos.z), hit, k);
			} else if (!tr.above && above && b.vel.y > 1.5f) {
				// breaking out upward (water lifted, a stone thrown out)
				c.fx.Splash(c, Vec3(b.pos.x, level, b.pos.z), kWUp, 0.35f);
				Ripples(c, b.pos, 0.8f + b.radius, 0.45f, 1);
			}
			tr.above = above;
		} else if (newTick && !overPool) {
			tr.above = true;
		}
		// skimming over / through the surface: wake ripples (waves crossing the pool, fast low bodies)
		if (overPool && tr.wakeT <= 0.0f && WSpeedXZ(b.vel) > 1.5f &&
		    (b.form == ff::Form::Wave || std::fabs(bottom - level) < 0.45f)) {
			tr.wakeT = 0.24f;
			Ripples(c, b.pos, 0.6f + MinF(b.radius, 1.2f), 0.45f, 1);
		}
		// fire close over the water raises steam
		if (overPool && WFiery(fam) && b.pos.y > level - 0.2f && b.pos.y - level < ws.steamNearPool && tr.steamT <= 0.0f &&
		    b.form != ff::Form::Zone) {
			tr.steamT = 0.3f;
			const float near = 1.0f - Sat((b.pos.y - level) / MaxF(ws.steamNearPool, 0.1f));
			c.fx.Steam(c, Vec3(b.pos.x, level + 0.1f, b.pos.z), 0.25f + 0.4f * near);
		}
		// a fire field reaching the pool edge steams where it touches the water
		if (havePool && b.form == ff::Form::Zone && WFiery(fam) && b.zone_radius > 0.3f && poolSteamT_ <= 0.0f) {
			const Vec3 q(Clamp(b.pos.x, ar->pool_min.x, ar->pool_max.x), level, Clamp(b.pos.z, ar->pool_min.y, ar->pool_max.y));
			const float dx = q.x - b.pos.x, dz = q.z - b.pos.z;
			if (dx * dx + dz * dz < b.zone_radius * b.zone_radius) {
				poolSteamT_ = 0.45f;
				c.fx.Steam(c, q + Vec3(0.0f, 0.1f, 0.0f), 0.45f);
			}
		}
		tr.pos = b.pos;
		if (!tr.puddle) tr.radius = b.radius;
	}
	// gone: a puddle leaves a wet mark that dries
	for (auto it = bodies_.begin(); it != bodies_.end();) {
		if (it->second->seen) {
			++it;
			continue;
		}
		const BodyTrack& tr = *it->second;
		if (tr.puddle && !InPool(c, tr.pos, 0.1f)) Scar(c, tr.pos, ScarKind::Wet, tr.radius * 1.05f);
		it = bodies_.erase(it);
	}
}

}  // namespace ffx
