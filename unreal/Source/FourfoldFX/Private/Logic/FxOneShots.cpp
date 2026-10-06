// FourfoldFX logic island - pooled one-shot effects. Owner: stream `fx`.
#include "FxOneShots.h"

#include "FxMeshLib.h"

#include <cmath>
#include <type_traits>

namespace ffx {

namespace {

constexpr float kDegToRad = kFxPi / 180.0f;

// Unit vector within `spreadDeg` of `dir` (uniform over the cap).
Vec3 ConeDir(Rng& r, const Vec3& dir, float spreadDeg) { return r.InCone(dir, Clamp(spreadDeg, 0.0f, 180.0f) * kDegToRad); }

// Basis turning +Y onto `d` (FireBurstFX._quat_up_to): local x / z perpendicular, deterministic.
Basis UpTo(const Vec3& d) {
	const Vec3 y = Norm(d, Vec3(0.0f, 1.0f, 0.0f));
	const Vec3 ref = std::fabs(y.y) < 0.95f ? Vec3(0.0f, 1.0f, 0.0f) : Vec3(1.0f, 0.0f, 0.0f);
	const Vec3 x = Norm(ref.cross(y), Vec3(1.0f, 0.0f, 0.0f));
	return {x, y, x.cross(y)};
}

// Basis for a ground ring with normal n (+Y of the ground quad along n).
Basis NormalBasis(const Vec3& n) { return UpTo(n); }

void SetColors4(ParamBlock& pb, const std::array<Color, 4>& c) {
	pb.Set(PV::Color, Linear(c[0]));
	pb.Set(PV::Color2, Linear(c[1]));
	pb.Set(PV::Color3, Linear(c[2]));
	pb.Set(PV::Color4, Linear(c[3]));
}

int Scaled(int base, float k) { return MaxI(1, static_cast<int>(std::lround(static_cast<float>(base) * k))); }

}  // namespace

// =============================================================================================== Trail
void Trail::Begin(Ctx& c, float width, const Color& tint, float opacity, float maxAge) {
	key_ = c.keys.New();
	count_ = 0;
	emitting_ = true;
	width_ = MaxF(width, 0.02f);
	tint_ = tint;
	opacity_ = opacity;
	maxAge_ = MaxF(maxAge, 0.1f);
	flow_ = 0.0f;
}

void Trail::Push(float time, const Vec3& p) {
	if (count_ > 0 && pos_[count_ - 1].distance_squared_to(p) < 1e-6f) return;
	if (count_ == kPoints) {
		for (int i = 0; i + 1 < kPoints; ++i) {
			pos_[i] = pos_[i + 1];
			stamp_[i] = stamp_[i + 1];
		}
		--count_;
	}
	pos_[count_] = p;
	stamp_[count_] = time;
	++count_;
}

bool Trail::Draw(Ctx& c, float fade) {
	const float now = static_cast<float>(c.time);
	int drop = 0;
	while (drop < count_ && now - stamp_[drop] > maxAge_) ++drop;
	if (drop > 0) {
		for (int i = drop; i < count_; ++i) {
			pos_[i - drop] = pos_[i];
			stamp_[i - drop] = stamp_[i];
		}
		count_ -= drop;
	}
	flow_ += c.dt * 1.5f;
	if (count_ < 2) return Alive();
	std::vector<Vec3> pts(static_cast<size_t>(count_));
	std::vector<float> w(static_cast<size_t>(count_));
	std::vector<Color> cols(static_cast<size_t>(count_));
	for (int i = 0; i < count_; ++i) {
		const float age01 = Sat((now - stamp_[i]) / maxAge_);
		pts[static_cast<size_t>(i)] = pos_[i];
		w[static_cast<size_t>(i)] = width_ * Lerp(0.25f, 1.0f, 1.0f - age01);
		// vertex colour: r = width fraction, g = age, a = alpha (head fades in over 2 samples)
		const float head = Sat(static_cast<float>(count_ - 1 - i) / 2.0f + 0.35f);
		cols[static_cast<size_t>(i)] = Color(Sat(w[static_cast<size_t>(i)]), age01, 0.0f, (1.0f - age01) * head);
	}
	mesh_.Clear();
	AppendRibbon(mesh_, pts, w, c.in.cam.pos, cols);
	mesh_.Commit();
	DrawItem& it = c.out.Add(key_, MatSlot::Wind, &mesh_);
	it.params.Set(P::Style, 3.0f);   // wind material: trail
	it.params.Set(PV::Color, Linear(tint_));
	it.params.Set(P::Opacity, opacity_ * fade);
	it.params.Set(P::Flow, flow_);
	it.sortPriority = 1;
	return Alive();
}

// =============================================================================================== effects
struct OneShots::RingFx final : OneShot {
	Vec3 pos, normal;
	float r0 = 0.2f, r1 = 1.0f, dur = 0.4f;
	Color col;
	RingOpts o;
	uint32_t key[2] = {0, 0};
	float seed = 0.0f;
	void Step(Ctx& c) override {
		age += c.dt;
		if (age >= dur) {
			active = false;
			return;
		}
		for (int i = 0; i < o.count; ++i) {
			const float fi = static_cast<float>(i);
			const float t = Sat((age - fi * dur * 0.22f) / (dur * (1.0f - 0.22f * static_cast<float>(o.count - 1))));
			float e = o.easeIn ? t * t : 1.0f - std::pow(1.0f - t, 2.5f);
			if (o.hold) e = 1.0f - std::pow(1.0f - MinF(t * 3.0f, 1.0f), 2.0f);
			const float r = Lerp(r0, r1, e);
			const float sq = r / 0.8f;   // the band sits at 0.8 of the quad half size
			const float fade = (1.0f - Smooth(0.55f, 1.0f, t)) * Smooth(0.0f, 0.06f, t);
			Xform x;
			x.pos = pos;
			const MeshData* mesh = &meshlib::GroundQuad();
			if (o.billboard) {
				x.basis = c.Facing(pos).Scaled(sq, sq, 1.0f);
				mesh = &meshlib::FaceQuad();
			} else {
				x.basis = NormalBasis(normal).Scaled(sq, 1.0f, sq);
			}
			DrawItem& it = c.out.Add(key[i], MatSlot::Ring, mesh, x);
			it.params.Set(PV::Color, Linear(col));
			it.params.Set(P::Cover, o.cover);
			it.params.Set(P::Glow, o.glow);
			it.params.Set(P::Style, o.style);
			it.params.Set(P::Opacity, fade * o.alpha * (i == 0 ? 1.0f : 0.7f));
			it.params.Set(P::Width, o.width * 0.8f * (1.0f + 0.5f * t));
			it.params.Set(P::Radius, 0.8f);
			it.params.Set(P::Phase, age);
			it.params.Set(P::Seed, seed + fi * 1.7f);
			it.sortPriority = 2;
		}
	}
};

struct OneShots::BurstFx final : OneShot {
	ParticleSet puffs, sparks;
	ParticleLook puffLook, sparkLook;
	ParticlePhysics puffPh, sparkPh;
	MeshData puffMesh, sparkMesh;
	uint32_t keyPuff = 0, keySpark = 0;
	bool hasPuff = false, hasSpark = false;
	float dur = 1.0f;
	Color tint;
	Flipbook fb = Flipbook::DustPuff;
	BurstFx() {
		puffs.Reset(16, 11u);
		sparks.Reset(16, 13u);
	}
	void Step(Ctx& c) override {
		age += c.dt;
		puffs.Step(c.dt, puffPh);
		sparks.Step(c.dt, sparkPh);
		if (age >= dur + 0.1f || (!puffs.AnyAlive() && !sparks.AnyAlive())) {
			active = false;
			return;
		}
		if (hasPuff) {
			puffs.Build(puffMesh, c.in.cam.pos, c.in.cam.up, puffLook);
			puffMesh.Commit();
			DrawItem& it = c.out.Add(keyPuff, MatSlot::Smoke, &puffMesh);
			it.params.Set(PV::Color, Linear(tint));
			it.params.Set(P::Opacity, 1.0f);
			it.params.Set(P::Erosion, 0.5f);
			it.params.flipbook = fb;
			it.sortPriority = 1;
		}
		if (hasSpark) {
			sparks.Build(sparkMesh, c.in.cam.pos, c.in.cam.up, sparkLook);
			sparkMesh.Commit();
			DrawItem& it = c.out.Add(keySpark, MatSlot::Spark, &sparkMesh);
			it.params.Set(P::EmissiveScale, 2.5f);
			it.sortPriority = 3;
		}
	}
};

struct OneShots::ShardsFx final : OneShot {
	static constexpr int kMax = 14;
	static constexpr float kDur = 0.9f;
	int n = 0;
	Vec3 p[kMax], v[kMax], axis[kMax], dims[kMax];
	float spin[kMax] = {}, size[kMax] = {};
	float ground = 0.0f;
	ShardMat mat = ShardMat::Stone;
	uint32_t key = 0, seed = 0;
	MeshData mesh;
	void Step(Ctx& c) override {
		age += c.dt;
		if (age >= kDur) {
			active = false;
			return;
		}
		const float dt = c.dt;
		for (int i = 0; i < n; ++i) {
			v[i].y -= 9.8f * dt;
			p[i] += v[i] * dt;
			const float half = size[i] * 0.5f;
			if (p[i].y < ground + half && v[i].y < 0.0f) {
				p[i].y = ground + half;
				v[i] = Vec3(v[i].x * 0.45f, -v[i].y * 0.3f, v[i].z * 0.45f);
				spin[i] *= 0.5f;
			}
		}
		const float t = age / kDur;
		const float shrink = 1.0f - Smooth(0.65f, 1.0f, t);
		mesh.Clear();
		const bool crystal = mat == ShardMat::Ice || mat == ShardMat::Glass;
		const MeshData& unit = crystal ? meshlib::Crystal(seed, CrystalMode::Shard)
		                               : (mat == ShardMat::Metal || mat == ShardMat::Plant ? meshlib::Spike() : meshlib::Chip());
		for (int i = 0; i < n; ++i) {
			MeshData one = unit;
			const float s = MaxF(size[i] * shrink, 1e-4f);
			const Basis b = Basis::AxisAngle(axis[i], spin[i] * age).Mul(Basis::Identity().Scaled(s * dims[i].x, s * dims[i].y, s * dims[i].z));
			one.Transform({p[i], b});
			mesh.Append(one);
		}
		mesh.Commit();
		MatSlot slot = MatSlot::Rock;
		switch (mat) {
			case ShardMat::Ice:
			case ShardMat::Glass: slot = MatSlot::Crystal; break;
			case ShardMat::Metal: slot = MatSlot::Metal; break;
			case ShardMat::Plant: slot = MatSlot::Vine; break;
			default: break;
		}
		DrawItem& it = c.out.Add(key, slot, &mesh);
		it.params.Set(P::Seed, 31.5f);
		if (crystal) {
			const CrystalLook& cl = mat == ShardMat::Glass ? c.cfg.glass : c.cfg.ice;
			it.params.Set(PV::Tint, Linear(cl.tint));
			it.params.Set(P::Frost, cl.frost);
			it.params.Set(P::Opacity, 0.75f);
			it.params.Set(P::Glow, 1.0f);
			it.params.Set(P::Rise, 1.0f);
		}
		it.params.Set(P::Fade, 1.0f);
	}
};

struct OneShots::BlastFx final : OneShot {
	static constexpr float kDur = 0.75f;
	Vec3 pos;
	float radius = 1.5f, intensity = 1.0f, ground = 0.0f, rot = 0.0f;
	bool blue = false;
	uint32_t keySprite[3] = {0, 0, 0}, keyRing = 0, keyLight = 0;
	MeshData sprites;
	void Step(Ctx& c) override {
		age += c.dt;
		if (age >= kDur) {
			active = false;
			return;
		}
		const float t = age / kDur;
		const float grow = 1.0f - std::pow(1.0f - MinF(t * 2.2f, 1.0f), 3.0f);
		// three explosion flipbook sprites: a big core and two offset lobes, rotated differently
		sprites.Clear();
		const Basis f = c.Facing(pos);
		const float frame = Sat(t * 1.15f) * 63.0f;
		for (int i = 0; i < 3; ++i) {
			const float fi = static_cast<float>(i);
			const float sz = radius * (i == 0 ? 2.2f : 1.4f) * (0.35f + 0.65f * grow);
			const float a = rot + fi * 2.1f;
			const Vec3 off = i == 0 ? Vec3() : (f.x * std::cos(a) + f.y * (0.4f + 0.3f * std::sin(a))) * (radius * 0.55f * grow);
			const Vec3 hx = (f.x * std::cos(a + fi) + f.y * std::sin(a + fi)) * (sz * 0.5f);
			const Vec3 hy = (f.y * std::cos(a + fi) - f.x * std::sin(a + fi)) * (sz * 0.5f);
			const int b = sprites.NumVerts();
			AppendQuad(sprites, pos + off + Vec3(0.0f, radius * 0.25f * grow, 0.0f), hx, hy, Color(1, 1, 1, 1));
			for (int k = b; k < sprites.NumVerts(); ++k) {
				sprites.uv1[static_cast<size_t>(k)] = Vec2(MinF(frame + fi * 2.0f, 63.0f), 1.0f);
				sprites.uv2[static_cast<size_t>(k)] = Vec2(t, fi * 0.37f);
			}
		}
		sprites.Commit();
		DrawItem& it = c.out.Add(keySprite[0], MatSlot::FireSprite, &sprites);
		it.params.flipbook = Flipbook::Explosion;
		it.params.Set(P::Intensity, intensity);
		it.params.Set(P::Age, t);
		if (blue) {
			it.params.Set(PV::Tint, Linear(Color(0.55f, 0.75f, 1.4f)));
		} else {
			it.params.Set(PV::Tint, Color(1, 1, 1, 1));
		}
		SetColors4(it.params, blue ? c.cfg.blueFlame : c.cfg.flame);
		it.sortPriority = 2;
		// ground shock ring
		const float rt = MinF(t * 1.6f, 1.0f);
		const float rr = radius * (0.4f + 1.8f * (1.0f - (1.0f - rt) * (1.0f - rt)));
		Xform xr;
		xr.pos = Vec3(pos.x, ground + 0.03f, pos.z);
		xr.basis = Basis::Identity().Scaled(rr / 0.8f, 1.0f, rr / 0.8f);
		DrawItem& ring = c.out.Add(keyRing, MatSlot::Ring, &meshlib::GroundQuad(), xr);
		ring.params.Set(PV::Color, Linear(Color(0.95f, 0.85f, 0.7f)));
		ring.params.Set(P::Cover, 0.7f);
		ring.params.Set(P::Glow, 1.0f);
		ring.params.Set(P::Opacity, (1.0f - rt) * 0.9f * intensity);
		ring.params.Set(P::Width, 0.05f + 0.08f * rt);
		ring.params.Set(P::Radius, 0.8f);
		ring.params.Set(P::Phase, age);
		const float le = 3.0f * intensity * (1.0f - Smooth(0.0f, 0.16f, t));
		c.out.Light(keyLight, pos + Vec3(0.0f, 0.4f, 0.0f), blue ? Color(0.6f, 0.75f, 1.0f) : Color(1.0f, 0.75f, 0.45f), le,
		            radius * 4.0f + 2.0f, 3.0f);
	}
};

struct OneShots::BeamFx final : OneShot {
	Vec3 from, to;
	float dur = 0.3f, scroll = 0.0f;
	BeamStyle style = BeamStyle::Blue;
	uint32_t key = 0, keyLight = 0;
	void Step(Ctx& c) override {
		age += c.dt;
		scroll += c.dt;
		if (age >= dur) {
			active = false;
			return;
		}
		const float t = age / dur;
		const BeamLook& bl = c.cfg.Beam(style);
		const Vec3 d = to - from;
		const float len = MaxF(d.length(), 0.05f);
		const Vec3 z = d / len;
		// strip plane faces the camera: +Y of the strip toward the camera, rotated about the beam axis
		const Vec3 mid = from + d * 0.5f;
		Vec3 y = c.ToCam(mid);
		y = y - z * y.dot(z);
		y = Norm(y, Perp(z));
		const Vec3 x = y.cross(z);
		Xform xf;
		xf.pos = from;
		xf.basis = {x * (bl.width * 0.5f), y, z * len};
		DrawItem& it = c.out.Add(key, MatSlot::Beam, &meshlib::Beam(), xf);
		it.params.Set(PV::Color, Linear(bl.core));
		it.params.Set(PV::Color2, Linear(bl.glow));
		it.params.Set(P::Width, bl.width);
		it.params.Set(P::Cover, bl.cover);
		it.params.Set(P::Taper, bl.taper);
		it.params.Set(P::Grain, bl.grain);
		it.params.Set(P::Age, t);
		it.params.Set(P::Scroll, scroll);
		it.params.Set(P::Height, len);
		it.sortPriority = 2;
		if (bl.light && t < 0.6f) c.out.Light(keyLight, mid, Color(0.5f, 0.7f, 1.0f), 1.6f * (1.0f - t), 5.0f, 2.0f);
	}
};

struct OneShots::FireBurstFx final : OneShot {
	static constexpr float kDur = 0.55f;
	Vec3 origin, dir;
	float length = 3.0f, intensity = 1.0f, seed = 0.0f;
	bool blue = false;
	uint32_t keyOuter = 0, keyInner = 0, keyLight = 0;
	void Step(Ctx& c) override {
		age += c.dt;
		if (age >= kDur) {
			active = false;
			return;
		}
		const float n = Sat(age / kDur);
		const float w = length * 0.17f * (0.75f + 0.35f * intensity);
		Xform x;
		x.pos = origin;
		const Basis b = UpTo(dir);
		x.basis = {b.x * w, b.y * length, b.z * w};
		for (int layer = 0; layer < 2; ++layer) {
			const bool core = layer == 1;
			DrawItem& it = c.out.Add(core ? keyInner : keyOuter, MatSlot::Flame, &meshlib::Flame(), x);
			it.params.Set(P::Age, n);
			it.params.Set(P::Intensity, intensity);
			it.params.Set(P::Core, core ? 1.0f : 0.0f);
			it.params.Set(P::Cover, core ? 0.25f : 0.8f);
			it.params.Set(P::Style, 0.0f);   // burst
			it.params.Set(P::Shape, 0.0f);   // trumpet
			it.params.Set(P::Seed, core ? std::fmod(seed + 0.37f, 1.0f) : seed);
			SetColors4(it.params, blue ? c.cfg.blueFlame : c.cfg.flame);
			it.sortPriority = core ? 3 : 2;
		}
		const float e = (1.0f - std::exp(-age * 70.0f)) * std::exp(-age * 7.5f);
		c.out.Light(keyLight, origin + Norm(dir) * (length * 0.42f), blue ? Color(0.5f, 0.7f, 1.0f) : Color(1.0f, 0.52f, 0.18f),
		            3.0f * intensity * e, 2.5f + length * 1.1f, 2.5f);
	}
};

struct OneShots::AirPushFx final : OneShot {
	static constexpr float kDur = 0.55f;
	static constexpr int kStreaks = 14;
	Vec3 origin, dir;
	float radius = 1.0f, length = 3.0f;
	uint32_t keyCone = 0, keyStreaks = 0;
	float sAng[kStreaks] = {}, sRad[kStreaks] = {}, sPhase[kStreaks] = {}, sSpeed[kStreaks] = {};
	Color tint{0.78f, 0.72f, 0.64f, 1.0f};
	MeshData streaks;
	void Step(Ctx& c) override {
		age += c.dt;
		if (age >= kDur) {
			active = false;
			return;
		}
		const float n = Sat(age / kDur);
		const Basis b = UpTo(dir);
		const float grow = 1.0f + 0.12f * n;
		Xform x;
		x.pos = origin;
		x.basis = {b.x * (radius * grow), b.y * length, b.z * (radius * grow)};
		DrawItem& cone = c.out.Add(keyCone, MatSlot::Wind, &meshlib::Cone(), x);
		cone.params.Set(P::Style, 1.0f);   // pressure band
		cone.params.Set(P::Age, n);
		cone.params.Set(P::Opacity, 0.55f);
		cone.params.Set(P::Seed, sAng[0] * 7.0f);
		cone.sortPriority = 1;
		// dust streaks racing along the cone (AirStreak shader, on the CPU)
		streaks.Clear();
		const float fade = 1.0f - Smooth(0.7f, 1.0f, n);
		for (int i = 0; i < kStreaks; ++i) {
			const float travel = Clamp(n * (1.15f + 0.5f * sSpeed[i]) + sPhase[i] * 0.25f, 0.0f, 1.2f);
			const float rr = std::sqrt(sRad[i]) * 0.8f * (0.06f + 0.94f * travel);
			const float a = sAng[i] * kFxTau;
			const Vec3 pc = origin + b.x * (std::cos(a) * rr * radius) + b.z * (std::sin(a) * rr * radius) + b.y * (travel * length);
			const float al = Smooth(0.0f, 0.18f, travel) * (1.0f - Smooth(0.7f, 1.05f, travel)) * fade;
			const std::vector<Vec3> pts = {pc - b.y * 0.6f, pc, pc + b.y * 0.6f};
			const std::vector<Color> cols = {Color(1, 1, 1, 0.0f), Color(1, 1, 1, al * 0.8f), Color(1, 1, 1, 0.0f)};
			AppendRibbon(streaks, pts, {0.056f}, c.in.cam.pos, cols);
		}
		streaks.Commit();
		DrawItem& it = c.out.Add(keyStreaks, MatSlot::Wind, &streaks);
		it.params.Set(P::Style, 2.0f);   // streaks
		it.params.Set(PV::Color, Linear(tint));
		it.params.Set(P::Opacity, 1.0f);
		it.sortPriority = 2;
	}
};

// Single particle system effects: splash (drops + mist), steam, dust, ember.
struct OneShots::PuffFx final : OneShot {
	enum class Kind : uint8_t { Splash, Steam, Dust, Ember } kind = Kind::Dust;
	ParticleSet ps, ps2;
	ParticleLook look, look2;
	ParticlePhysics ph, ph2;
	MeshData mesh, mesh2;
	uint32_t key = 0, key2 = 0;
	float dur = 1.0f;
	Color tint;
	void Init(Kind k) {
		kind = k;
		const int cap = k == Kind::Splash ? 22 : (k == Kind::Steam ? 10 : (k == Kind::Dust ? 14 : 12));
		ps.Reset(cap, 101u + static_cast<uint32_t>(k));
		ps2.Reset(k == Kind::Splash ? 4 : 0, 211u);
	}
	void Step(Ctx& c) override {
		age += c.dt;
		ps.Step(c.dt, ph);
		ps2.Step(c.dt, ph2);
		if (age >= dur || (!ps.AnyAlive() && !ps2.AnyAlive())) {
			active = false;
			return;
		}
		ps.Build(mesh, c.in.cam.pos, c.in.cam.up, look);
		mesh.Commit();
		switch (kind) {
			case Kind::Splash: {
				DrawItem& it = c.out.Add(key, MatSlot::Splash, &mesh);
				it.params.Set(PV::Color, Linear(Color(0.78f, 0.9f, 1.0f)));
				it.params.flipbook = Flipbook::WaterSplash;
				it.sortPriority = 2;
				if (ps2.Capacity() > 0) {
					ps2.Build(mesh2, c.in.cam.pos, c.in.cam.up, look2);
					mesh2.Commit();
					DrawItem& m = c.out.Add(key2, MatSlot::Smoke, &mesh2);
					m.params.Set(PV::Color, Linear(Color(0.86f, 0.93f, 0.98f)));
					m.params.flipbook = Flipbook::SteamPuff;
					m.params.Set(P::Erosion, 0.6f);
					m.sortPriority = 1;
				}
				break;
			}
			case Kind::Steam:
			case Kind::Dust: {
				DrawItem& it = c.out.Add(key, MatSlot::Smoke, &mesh);
				it.params.Set(PV::Color, Linear(tint));
				it.params.flipbook = kind == Kind::Steam ? Flipbook::SteamPuff : Flipbook::DustPuff;
				it.params.Set(P::Erosion, kind == Kind::Steam ? 0.7f : 0.5f);
				it.sortPriority = 1;
				break;
			}
			case Kind::Ember: {
				DrawItem& it = c.out.Add(key, MatSlot::Spark, &mesh);
				it.params.Set(P::EmissiveScale, 3.0f);
				it.sortPriority = 3;
				break;
			}
		}
	}
};

struct OneShots::BoltFx final : OneShot {
	static constexpr float kDur = 0.2f;
	std::vector<BoltLine> lines;
	MeshData mesh;
	uint32_t key = 0, keyLight = 0;
	float intensity = 1.0f;
	bool light = true;
	Vec3 mid;
	Color tint;
	float length = 0.0f;
	void Step(Ctx& c) override {
		age += c.dt;
		if (age >= kDur) {
			active = false;
			return;
		}
		const float n = Sat(age / kDur);
		DrawItem& it = c.out.Add(key, MatSlot::Lightning, &mesh);
		it.params.Set(P::Age, n);
		it.params.Set(P::Intensity, intensity);
		it.params.Set(PV::Color, Linear(tint));
		it.sortPriority = 3;
		if (light) {
			const float flick = 0.65f + 0.35f * std::sin(age * 140.0f);
			const float restrike = age >= 0.09f ? 0.25f * std::exp(-(age - 0.09f) * 30.0f) : 0.0f;
			const float e = std::exp(-age * 22.0f) * flick + restrike;
			c.out.Light(keyLight, mid, Color(0.62f, 0.72f, 1.0f), MinF(2.2f * intensity * e, 2.2f),
			            Clamp(4.0f + length * 0.35f, 4.0f, 9.0f), 2.8f);
		}
	}
};

struct OneShots::PulseFx final : OneShot {
	Vec3 pos;
	Color col;
	float intensity = 1.0f, radius = 3.0f, dur = 0.3f;
	uint32_t key = 0;
	void Step(Ctx& c) override {
		age += c.dt;
		if (age >= dur) {
			active = false;
			return;
		}
		const float t = age / dur;
		c.out.Light(key, pos, col, intensity * (1.0f - t) * (1.0f - t), radius, 2.0f);
	}
};

// =============================================================================================== OneShots
OneShots::OneShots() {
	auto fill = [this](auto& pool, int cap) {
		for (int i = 0; i < cap; ++i) {
			pool.items.emplace_back(std::make_unique<typename std::remove_reference_t<decltype(*pool.items[0])>>());
			all_.push_back(pool.items.back().get());
		}
	};
	// docs/VFX.md pool caps (+ headroom: the cue layer fires several per event)
	fill(rings_, 12);
	fill(bursts_, 10);
	fill(shards_, 4);
	fill(blasts_, 3);
	fill(beams_, 3);
	fill(fireBursts_, 4);
	fill(airPushes_, 3);
	fill(splashes_, 6);
	fill(steams_, 6);
	fill(dusts_, 8);
	fill(embers_, 4);
	fill(bolts_, 6);
	fill(pulses_, 4);
	for (auto& p : splashes_.items) p->Init(PuffFx::Kind::Splash);
	for (auto& p : steams_.items) p->Init(PuffFx::Kind::Steam);
	for (auto& p : dusts_.items) p->Init(PuffFx::Kind::Dust);
	for (auto& p : embers_.items) p->Init(PuffFx::Kind::Ember);
}

OneShots::~OneShots() = default;

void OneShots::Clear() {
	for (OneShot* o : all_) o->active = false;
}

void OneShots::Step(Ctx& c) {
	for (OneShot* o : all_)
		if (o->active) o->Step(c);
}

int OneShots::ActiveCount() const {
	int n = 0;
	for (const OneShot* o : all_) n += o->active ? 1 : 0;
	return n;
}

void OneShots::Ring(Ctx& c, const Vec3& pos, const Vec3& normal, float r0, float r1, float dur, const Color& col,
                    const RingOpts& o) {
	RingFx& r = rings_.Acquire(serial_);
	r.pos = pos;
	r.normal = normal.length_squared() > 1e-6f ? Norm(normal) : Vec3(0.0f, 1.0f, 0.0f);
	r.r0 = MaxF(r0, 0.01f);
	r.r1 = MaxF(r1, 0.01f);
	r.dur = MaxF(dur, 0.05f);
	r.col = col;
	r.o = o;
	r.o.count = ClampI(o.count, 1, 2);
	r.key[0] = c.keys.New();
	r.key[1] = c.keys.New();
	r.seed = pos.x * 0.13f;
}

void OneShots::Burst(Ctx& c, const Vec3& pos, const Vec3& normal, float strength, ffx::Burst style) {
	const BurstStyle& st = c.cfg.Burst(style);
	BurstFx& b = bursts_.Acquire(serial_);
	strength = Clamp(strength, 0.1f, 2.0f);
	const Vec3 n = normal.length_squared() > 1e-6f ? Norm(normal) : Vec3(0.0f, 1.0f, 0.0f);
	const Vec3 at = pos + n * 0.06f;
	b.hasPuff = st.hasPuff;
	b.hasSpark = st.hasSpark;
	b.dur = st.dur;
	b.tint = st.puff;
	b.fb = st.flipbook;
	b.keyPuff = c.keys.New();
	b.keySpark = c.keys.New();
	b.puffs.Reset(16, c.rng.Next());
	b.sparks.Reset(16, c.rng.Next());
	const float qk = c.q.particles;
	if (st.hasPuff) {
		const int count = Scaled(16, Clamp(0.35f + 0.65f * strength, 0.2f, 1.0f) * qk);
		const bool inflow = st.speed < 0.0f;
		const float emitR = inflow ? 0.9f + 0.5f * strength : 0.1f + 0.15f * strength;
		b.puffLook = ParticleLook();
		b.puffLook.frames = FlipbookFrames(st.flipbook);
		b.puffLook.fadeIn = 0.12f;
		b.puffLook.fadeOut = 0.45f;
		b.puffPh = ParticlePhysics();
		b.puffPh.drag = 2.5f;
		b.puffPh.buoyancy = st.gravity;
		Rng& r = b.puffs.R();
		for (int i = 0; i < count; ++i) {
			Particle* p = b.puffs.Spawn();
			if (!p) break;
			const Vec3 off = r.InSphere() * emitR;
			p->p = at + off;
			if (inflow) {
				p->v = Norm(off, Vec3(0, 1, 0)) * (st.speed * r.Range(0.8f, 1.2f));
			} else {
				const float sp = st.speed * Lerp(0.4f + 0.3f * strength, 0.9f + 0.6f * strength, r.F01());
				p->v = ConeDir(r, n, st.spread) * sp;
			}
			p->life = st.dur * r.Range(0.6f, 1.0f);
			const float s = Lerp(0.18f + 0.1f * strength, 0.34f + 0.18f * strength, r.F01());
			p->size0 = s * 0.45f;
			p->size1 = s * 1.5f;
			p->rot = r.Range(0.0f, kFxTau);
			p->rotSpeed = r.Range(-20.0f, 20.0f) * kDegToRad;
			p->c0 = Color(1, 1, 1, st.alpha);
			p->c1 = Color(0.85f, 0.85f, 0.85f, st.alpha * 0.55f);
			p->frame0 = r.Range(0.0f, 8.0f);
		}
	}
	if (st.hasSpark) {
		const int count = Scaled(16, Clamp(0.3f + 0.7f * strength, 0.2f, 1.0f) * qk);
		b.sparkLook = ParticleLook();
		b.sparkLook.streak = true;
		b.sparkLook.streakScale = 0.05f;
		b.sparkLook.fadeIn = 0.0f;
		b.sparkLook.fadeOut = 0.6f;
		b.sparkPh = ParticlePhysics();
		b.sparkPh.gravity = -st.sparkGravity;
		b.sparkPh.drag = 0.7f;
		b.sparkPh.groundY = c.Ground(pos) + 0.01f;
		b.sparkPh.bounce = 0.3f;
		Rng& r = b.sparks.R();
		const Color white(1.0f, 1.0f, 1.0f, 1.0f);
		for (int i = 0; i < count; ++i) {
			Particle* p = b.sparks.Spawn();
			if (!p) break;
			p->p = at + r.InSphere() * 0.08f;
			p->v = ConeDir(r, n, st.spread) * (st.sparkSpeed * Lerp(0.4f, 0.8f + 0.4f * strength, r.F01()));
			p->life = MinF(st.dur, 0.9f) * r.Range(0.5f, 1.0f);
			const float s = r.Range(0.025f, 0.05f);
			p->size0 = s;
			p->size1 = s * 0.25f;
			p->c0 = Linear(LerpC(white, st.spark, 0.3f));
			p->c1 = Linear(Color(st.spark.r * 0.6f, st.spark.g * 0.3f, st.spark.b * 0.2f, 0.0f));
		}
	}
}

void OneShots::Shards(Ctx& c, const Vec3& pos, const Vec3& dir, float strength, ShardMat mat, uint32_t seed, float groundY) {
	ShardsFx& s = shards_.Acquire(serial_);
	strength = Clamp(strength, 0.2f, 2.0f);
	Rng r(HashCombine(seed, 1103u) + 17u);
	s.mat = mat;
	s.seed = seed;
	s.key = c.keys.New();
	s.n = ClampI(static_cast<int>(6.0f + 8.0f * strength * 0.6f), 5, ShardsFx::kMax);
	s.n = MinI(s.n, c.q.shardsMax);
	const Vec3 d = dir.length_squared() > 1e-6f ? Norm(dir) : Vec3(0.0f, 1.0f, 0.0f);
	const bool crystal = mat == ShardMat::Ice || mat == ShardMat::Glass;
	for (int i = 0; i < s.n; ++i) {
		const Vec3 rr = Norm(Vec3(r.Signed(), std::fabs(r.Signed()) * 0.8f + 0.3f, r.Signed()));
		s.p[i] = pos + r.InSphere() * 0.06f;
		s.v[i] = Norm(rr * 0.65f + d * 0.5f) * (r.Range(2.0f, 5.5f) * (0.6f + 0.4f * strength));
		s.axis[i] = r.OnSphere();
		s.spin[i] = r.Range(6.0f, 18.0f);
		s.size[i] = r.Range(0.04f, 0.1f) * (0.7f + 0.3f * strength) * (crystal ? 1.6f : 1.0f);
		s.dims[i] = crystal ? Vec3(1.0f, 1.0f, 1.0f) : Vec3(r.Range(0.7f, 1.2f), r.Range(0.5f, 0.9f), r.Range(0.7f, 1.2f));
		if (mat == ShardMat::Metal || mat == ShardMat::Plant) s.dims[i] = Vec3(0.35f, 1.4f, 0.35f);
	}
	s.ground = groundY > -1e8f ? groundY : pos.y - 1.0f;
}

void OneShots::Blast(Ctx& c, const Vec3& pos, float radius, float intensity, bool blue, float groundY) {
	BlastFx& b = blasts_.Acquire(serial_);
	b.pos = pos;
	b.radius = Clamp(radius, 0.3f, 6.0f);
	b.intensity = Clamp(intensity, 0.2f, 1.5f);
	b.blue = blue;
	b.ground = groundY;
	b.rot = c.rng.Range(0.0f, kFxTau);
	for (uint32_t& k : b.keySprite) k = c.keys.New();
	b.keyRing = c.keys.New();
	b.keyLight = c.keys.New();
	// flying embers and a smoke puff carry the blast on after the fireball
	Burst(c, pos, Vec3(0.0f, 1.0f, 0.0f), MinF(1.0f + b.radius * 0.2f, 2.0f), Burst::Ember);
}

void OneShots::Beam(Ctx& c, const Vec3& from, const Vec3& to, float dur, BeamStyle style) {
	BeamFx& b = beams_.Acquire(serial_);
	b.from = from;
	b.to = to;
	b.dur = MaxF(dur, 0.08f);
	b.style = style;
	b.scroll = 0.0f;
	b.key = c.keys.New();
	b.keyLight = c.keys.New();
}

void OneShots::FireBurst(Ctx& c, const Vec3& origin, const Vec3& dir, float length, float intensity, bool blue) {
	FireBurstFx& f = fireBursts_.Acquire(serial_);
	f.origin = origin;
	f.dir = dir.length_squared() > 1e-8f ? Norm(dir) : Vec3(0.0f, 0.0f, -1.0f);
	f.length = MaxF(length, 0.2f);
	f.intensity = Clamp(intensity, 0.1f, 2.0f);
	f.blue = blue;
	f.seed = std::fabs(std::fmod(origin.x * 0.37f + origin.z * 0.61f + f.dir.x * 3.1f, 1.0f));
	f.keyOuter = c.keys.New();
	f.keyInner = c.keys.New();
	f.keyLight = c.keys.New();
}

void OneShots::AirPush(Ctx& c, const Vec3& origin, const Vec3& dir, float radius, float length) {
	AirPushFx& a = airPushes_.Acquire(serial_);
	a.origin = origin;
	a.dir = dir.length_squared() > 1e-8f ? Norm(dir) : Vec3(0.0f, 0.0f, -1.0f);
	a.radius = MaxF(radius, 0.1f);
	a.length = MaxF(length, 0.3f);
	a.keyCone = c.keys.New();
	a.keyStreaks = c.keys.New();
	Rng r(c.rng.Next());
	for (int i = 0; i < AirPushFx::kStreaks; ++i) {
		a.sAng[i] = r.F01();
		a.sRad[i] = r.F01();
		a.sPhase[i] = r.F01();
		a.sSpeed[i] = r.F01();
	}
	a.tint = c.cfg.DustColor(Fam::Wind);
}

void OneShots::Splash(Ctx& c, const Vec3& pos, const Vec3& normal, float strength) {
	PuffFx& s = splashes_.Acquire(serial_);
	strength = Clamp(strength, 0.1f, 2.0f);
	const Vec3 n = normal.length_squared() > 1e-6f ? Norm(normal) : Vec3(0.0f, 1.0f, 0.0f);
	s.key = c.keys.New();
	s.key2 = c.keys.New();
	s.dur = 0.8f;
	s.ps.Reset(22, c.rng.Next());
	s.ps2.Reset(4, c.rng.Next());
	s.look = ParticleLook();
	s.look.frames = FlipbookFrames(Flipbook::WaterSplash);
	s.look.fadeIn = 0.08f;
	s.look.fadeOut = 0.3f;
	s.look.streak = true;
	s.look.streakScale = 0.03f;
	s.ph = ParticlePhysics();
	s.ph.gravity = 9.8f;
	s.ph.groundY = pos.y - 0.02f;
	s.ph.bounce = 0.0f;
	const int drops = Scaled(22, Clamp(0.35f + 0.65f * strength, 0.3f, 1.0f) * c.q.particles);
	Rng& r = s.ps.R();
	for (int i = 0; i < drops; ++i) {
		Particle* p = s.ps.Spawn();
		if (!p) break;
		p->p = pos + n * 0.02f + r.InSphere() * 0.1f;
		p->v = ConeDir(r, n, 48.0f) * Lerp(1.2f + 0.8f * strength, 3.0f + 1.6f * strength, r.F01());
		p->life = 0.7f * r.Range(0.7f, 1.0f);
		const float sz = r.Range(0.03f, 0.075f);
		p->size0 = sz;
		p->size1 = sz * 0.35f;
		p->c0 = Color(1, 1, 1, 0.9f);
		p->c1 = Color(1, 1, 1, 0.6f);
		p->frame0 = r.Range(0.0f, 63.0f);
	}
	s.look2 = ParticleLook();
	s.look2.frames = FlipbookFrames(Flipbook::SteamPuff);
	s.look2.fadeIn = 0.2f;
	s.look2.fadeOut = 0.8f;
	s.ph2 = ParticlePhysics();
	s.ph2.drag = 2.0f;
	s.ph2.buoyancy = 0.2f;
	Rng& r2 = s.ps2.R();
	for (int i = 0; i < 4; ++i) {
		Particle* p = s.ps2.Spawn();
		if (!p) break;
		p->p = pos + r2.InSphere() * 0.12f;
		p->v = ConeDir(r2, n, 60.0f) * r2.Range(0.4f, 1.1f);
		p->life = 0.7f;
		const float sz = r2.Range(0.22f, 0.4f);
		p->size0 = sz * 0.5f;
		p->size1 = sz * 1.5f;
		p->rot = r2.Range(0.0f, kFxTau);
		p->c0 = Color(1, 1, 1, 0.3f);
		p->c1 = Color(1, 1, 1, 0.1f);
	}
}

void OneShots::Steam(Ctx& c, const Vec3& pos, float amount) {
	PuffFx& s = steams_.Acquire(serial_);
	amount = Clamp(amount, 0.05f, 1.0f);
	s.key = c.keys.New();
	s.dur = 1.4f;
	s.tint = Color(0.93f, 0.95f, 0.97f);
	s.ps.Reset(10, c.rng.Next());
	s.look = ParticleLook();
	s.look.frames = FlipbookFrames(Flipbook::SteamPuff);
	s.look.fadeIn = 0.18f;
	s.look.fadeOut = 0.55f;
	s.ph = ParticlePhysics();
	s.ph.drag = 0.8f;
	s.ph.buoyancy = 0.7f;
	s.ph.wind = Vec3(0.15f, 0.0f, 0.05f);
	const int n = Scaled(10, Clamp(0.25f + 0.75f * amount, 0.2f, 1.0f) * c.q.particles);
	Rng& r = s.ps.R();
	for (int i = 0; i < n; ++i) {
		Particle* p = s.ps.Spawn();
		if (!p) break;
		p->p = pos + r.InSphere() * 0.22f;
		p->v = ConeDir(r, Vec3(0, 1, 0), 22.0f) * Lerp(0.6f, 0.9f + 0.7f * amount, r.F01());
		p->life = 1.2f * r.Range(0.6f, 1.0f);
		const float sz = Lerp(0.25f + 0.2f * amount, 0.4f + 0.3f * amount, r.F01());
		p->size0 = sz * 0.5f;
		p->size1 = sz * 1.7f;
		p->rot = r.Range(0.0f, kFxTau);
		p->rotSpeed = r.Range(-12.0f, 12.0f) * kDegToRad;
		p->c0 = Color(1, 1, 1, 0.42f);
		p->c1 = Color(1, 1, 1, 0.22f);
		p->frame0 = r.Range(0.0f, 6.0f);
		// spawn staggered (explosiveness 0.55): younger particles start later
		p->age = -r.Range(0.0f, 0.3f);
	}
}

void OneShots::Dust(Ctx& c, const Vec3& pos, const Vec3& normal, float strength, const Color& tint) {
	PuffFx& s = dusts_.Acquire(serial_);
	strength = Clamp(strength, 0.1f, 2.0f);
	const Vec3 n = normal.length_squared() > 1e-6f ? Norm(normal) : Vec3(0.0f, 1.0f, 0.0f);
	s.key = c.keys.New();
	s.dur = 1.1f;
	s.tint = tint;
	s.ps.Reset(14, c.rng.Next());
	s.look = ParticleLook();
	s.look.frames = FlipbookFrames(Flipbook::DustPuff);
	s.look.fadeIn = 0.1f;
	s.look.fadeOut = 0.55f;
	s.ph = ParticlePhysics();
	s.ph.drag = 2.7f;
	s.ph.buoyancy = 0.25f;
	s.ph.groundY = pos.y - 0.6f;
	const int count = Scaled(14, Clamp(0.4f + 0.6f * strength, 0.3f, 1.0f) * c.q.particles);
	Rng& r = s.ps.R();
	for (int i = 0; i < count; ++i) {
		Particle* p = s.ps.Spawn();
		if (!p) break;
		p->p = pos + r.InSphere() * 0.18f;
		p->v = ConeDir(r, n, 75.0f) * Lerp(0.8f, 2.2f, r.F01()) * (0.6f + 0.4f * strength);
		p->life = 0.95f * r.Range(0.6f, 1.0f);
		const float sz = Lerp(0.35f, 0.7f, r.F01()) * (0.6f + 0.4f * strength);
		p->size0 = sz * 0.5f;
		p->size1 = sz * 1.4f;
		p->rot = r.Range(0.0f, kFxTau);
		p->rotSpeed = r.Range(-20.0f, 20.0f) * kDegToRad;
		p->c0 = Color(1, 1, 1, 0.55f);
		p->c1 = Color(0.9f, 0.9f, 0.9f, 0.2f);
		p->frame0 = r.Range(0.0f, 6.0f);
	}
}

void OneShots::Ember(Ctx& c, const Vec3& pos, const Vec3& normal, float strength) {
	PuffFx& s = embers_.Acquire(serial_);
	strength = Clamp(strength, 0.1f, 2.0f);
	const Vec3 n = normal.length_squared() > 1e-6f ? Norm(normal) : Vec3(0.0f, 1.0f, 0.0f);
	s.key = c.keys.New();
	s.dur = 1.3f;
	s.ps.Reset(12, c.rng.Next());
	s.look = ParticleLook();
	s.look.streak = true;
	s.look.streakScale = 0.04f;
	s.look.fadeIn = 0.05f;
	s.look.fadeOut = 0.5f;
	s.ph = ParticlePhysics();
	s.ph.drag = 1.2f;
	s.ph.buoyancy = 1.6f;
	s.ph.gravity = 0.0f;
	s.ph.wind = Vec3(0.3f, 0.0f, 0.1f);
	const int count = Scaled(12, Clamp(0.4f + 0.6f * strength, 0.3f, 1.0f) * c.q.particles);
	Rng& r = s.ps.R();
	for (int i = 0; i < count; ++i) {
		Particle* p = s.ps.Spawn();
		if (!p) break;
		p->p = pos + r.InSphere() * 0.15f;
		p->v = ConeDir(r, n, 50.0f) * Lerp(1.0f, 3.2f, r.F01()) * (0.6f + 0.4f * strength);
		p->life = r.Range(0.6f, 1.2f);
		const float sz = r.Range(0.02f, 0.045f);
		p->size0 = sz;
		p->size1 = sz * 0.4f;
		p->c0 = Linear(Color(1.0f, 0.75f, 0.35f, 1.0f));
		p->c1 = Linear(Color(0.9f, 0.2f, 0.03f, 0.0f));
	}
}

void OneShots::Bolt(Ctx& c, const std::vector<Vec3>& nodes, uint32_t seed, float intensity, bool light, float width,
                    const Color& tint) {
	if (nodes.size() < 2) return;
	BoltFx& b = bolts_.Acquire(serial_);
	BoltParams bp;
	bp.maxLevels = c.q.boltLevels;
	bp.width = width;
	float len = 0.0f;
	for (size_t i = 1; i < nodes.size(); ++i) len += nodes[i].distance_to(nodes[i - 1]);
	bp.minBranches = len > 1.2f ? 1 : 0;
	bp.maxBranches = len > 1.2f ? 3 : 0;
	GenerateBolt(nodes, seed, bp, b.lines);
	b.mesh.Clear();
	BuildBoltMesh(b.mesh, b.lines, c.in.cam.pos);
	b.mesh.Commit();
	b.key = c.keys.New();
	b.keyLight = c.keys.New();
	b.intensity = intensity;
	b.light = light;
	b.tint = tint;
	b.length = len;
	b.mid = nodes[nodes.size() / 2];
}

void OneShots::LightPulse(Ctx& c, const Vec3& pos, const Color& col, float intensity, float radius, float dur) {
	PulseFx& p = pulses_.Acquire(serial_);
	p.pos = pos;
	p.col = col;
	p.intensity = intensity;
	p.radius = radius;
	p.dur = MaxF(dur, 0.05f);
	p.key = c.keys.New();
}

}  // namespace ffx
